#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>


#include "xkcp_util.h"
#include "tcp_proxy.h"
#include "xkcp_client.h"

static int
xkcp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
	struct xkcp_proxy_param *ptr = user;
	return sendto(ptr->udp_fd, buf, len, 0, &ptr->serveraddr, sizeof(ptr->serveraddr));
}

static void
tcp_proxy_read_cb(struct bufferevent *bev, void *ctx) 
{
	ikcpcb *kcp = ctx;
	struct evbuffer *src;
	size_t	len;

	src = bufferevent_get_input(bev);
	len = evbuffer_get_length(src);

	if (len > 0) {
		char *data = malloc(len);
		memset(data, 0, len);
		evbuffer_copyout(src, data, len);
		ikcp_send(kcp, data, len);
		free(data);
	}
}

static void
tcp_proxy_event_cb(struct bufferevent *bev, short what, void *ctx)
{
	struct xkcp_task *task = ctx;

	if (what & (BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
		if (task) {
			del_task(task);
			ikcp_release(task->kcp);
			if (task->b_in != bev) {
				bufferevent_free(task->b_in);
				printf("impossible here\n");
			}
			free(task);
		}
		bufferevent_free(bev);
	}
}

void
tcp_proxy_accept_cb(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *a, int slen, void *param)
{
	struct xkcp_proxy_param *p = param;
	struct bufferevent *b_in = NULL;
	struct event_base *base = p->base;

	b_in = bufferevent_socket_new(base, fd,
	    BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
	assert(b_in);

	static int conv = 100;
	ikcpcb *kcp_client 	= ikcp_create(conv, param);
	kcp_client->output	= xkcp_output;
	ikcp_wndsize(kcp_client, 128, 128);
	ikcp_nodelay(kcp_client, 0, 10, 0, 1);
	if (conv++ >= 200)
		conv = 100;

	struct xkcp_task *task = malloc(sizeof(struct xkcp_task));
	assert(task);
	task->kcp = kcp_client;
	task->b_in = b_in;
	add_task_tail(task);

	bufferevent_setcb(b_in, tcp_proxy_read_cb, NULL, tcp_proxy_event_cb, task);
	bufferevent_enable(b_in,  EV_READ | EV_WRITE | EV_PERSIST);
}
