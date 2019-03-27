/*
 * http_server - HTTP server
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <fcntl.h>

#include "common.h"
#include "eloop.h"
#include "httpread.h"
#include "http_server.h"

#define HTTP_SERVER_TIMEOUT 30
#define HTTP_SERVER_MAX_REQ_LEN 8000
#define HTTP_SERVER_MAX_CONNECTIONS 10

struct http_request {
	struct http_request *next;
	struct http_server *srv;
	int fd;
	struct sockaddr_in cli;
	struct httpread *hread;
};

struct http_server {
	void (*cb)(void *ctx, struct http_request *req);
	void *cb_ctx;

	int fd;
	int port;

	struct http_request *requests;
	unsigned int request_count;
};


static void http_request_cb(struct httpread *handle, void *cookie,
			    enum httpread_event en)
{
	struct http_request *req = cookie;
	struct http_server *srv = req->srv;

	if (en == HTTPREAD_EVENT_FILE_READY) {
		wpa_printf(MSG_DEBUG, "HTTP: Request from %s:%d received",
			   inet_ntoa(req->cli.sin_addr),
			   ntohs(req->cli.sin_port));
		srv->cb(srv->cb_ctx, req);
		return;
	}
	wpa_printf(MSG_DEBUG, "HTTP: Request from %s:%d could not be received "
		   "completely", inet_ntoa(req->cli.sin_addr),
		   ntohs(req->cli.sin_port));
	http_request_deinit(req);
}


static struct http_request * http_request_init(struct http_server *srv, int fd,
					       struct sockaddr_in *cli)
{
	struct http_request *req;

	if (srv->request_count >= HTTP_SERVER_MAX_CONNECTIONS) {
		wpa_printf(MSG_DEBUG, "HTTP: Too many concurrent requests");
		return NULL;
	}

	req = os_zalloc(sizeof(*req));
	if (req == NULL)
		return NULL;

	req->srv = srv;
	req->fd = fd;
	req->cli = *cli;

	req->hread = httpread_create(req->fd, http_request_cb, req,
				     HTTP_SERVER_MAX_REQ_LEN,
				     HTTP_SERVER_TIMEOUT);
	if (req->hread == NULL) {
		http_request_deinit(req);
		return NULL;
	}

	return req;
}


void http_request_deinit(struct http_request *req)
{
	struct http_request *r, *p;
	struct http_server *srv;

	if (req == NULL)
		return;

	srv = req->srv;
	p = NULL;
	r = srv->requests;
	while (r) {
		if (r == req) {
			if (p)
				p->next = r->next;
			else
				srv->requests = r->next;
			srv->request_count--;
			break;
		}
		p = r;
		r = r->next;
	}

	httpread_destroy(req->hread);
	close(req->fd);
	os_free(req);
}


static void http_request_free_all(struct http_request *req)
{
	struct http_request *prev;
	while (req) {
		prev = req;
		req = req->next;
		http_request_deinit(prev);
	}
}


void http_request_send(struct http_request *req, struct wpabuf *resp)
{
	int res;

	wpa_printf(MSG_DEBUG, "HTTP: Send %lu byte response to %s:%d",
		   (unsigned long) wpabuf_len(resp),
		   inet_ntoa(req->cli.sin_addr),
		   ntohs(req->cli.sin_port));

	res = send(req->fd, wpabuf_head(resp), wpabuf_len(resp), 0);
	if (res < 0) {
		wpa_printf(MSG_DEBUG, "HTTP: Send failed: %s",
			   strerror(errno));
	} else if ((size_t) res < wpabuf_len(resp)) {
		wpa_printf(MSG_DEBUG, "HTTP: Sent only %d of %lu bytes",
			   res, (unsigned long) wpabuf_len(resp));
		/* TODO: add eloop handler for sending rest of the data */
	}

	wpabuf_free(resp);
}


void http_request_send_and_deinit(struct http_request *req,
				  struct wpabuf *resp)
{
	http_request_send(req, resp);
	http_request_deinit(req);
}


enum httpread_hdr_type http_request_get_type(struct http_request *req)
{
	return httpread_hdr_type_get(req->hread);
}


char * http_request_get_uri(struct http_request *req)
{
	return httpread_uri_get(req->hread);
}


char * http_request_get_hdr(struct http_request *req)
{
	return httpread_hdr_get(req->hread);
}


char * http_request_get_data(struct http_request *req)
{
	return httpread_data_get(req->hread);
}


char * http_request_get_hdr_line(struct http_request *req, const char *tag)
{
	return httpread_hdr_line_get(req->hread, tag);
}


struct sockaddr_in * http_request_get_cli_addr(struct http_request *req)
{
	return &req->cli;
}


static void http_server_cb(int sd, void *eloop_ctx, void *sock_ctx)
{
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	struct http_server *srv = eloop_ctx;
	int conn;
	struct http_request *req;

	conn = accept(srv->fd, (struct sockaddr *) &addr, &addr_len);
	if (conn < 0) {
		wpa_printf(MSG_DEBUG, "HTTP: Failed to accept new connection: "
			   "%s", strerror(errno));
		return;
	}
	wpa_printf(MSG_DEBUG, "HTTP: Connection from %s:%d",
		   inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

	req = http_request_init(srv, conn, &addr);
	if (req == NULL) {
		close(conn);
		return;
	}

	req->next = srv->requests;
	srv->requests = req;
	srv->request_count++;
}


struct http_server * http_server_init(struct in_addr *addr, int port,
				      void (*cb)(void *ctx,
						 struct http_request *req),
				      void *cb_ctx)
{
	struct sockaddr_in sin;
	struct http_server *srv;
	int on = 1;

	srv = os_zalloc(sizeof(*srv));
	if (srv == NULL)
		return NULL;
	srv->cb = cb;
	srv->cb_ctx = cb_ctx;

	srv->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (srv->fd < 0)
		goto fail;

	if (setsockopt(srv->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
	{
		wpa_printf(MSG_DEBUG,
			   "HTTP: setsockopt(SO_REUSEADDR) failed: %s",
			   strerror(errno));
		/* try to continue anyway */
	}

	if (fcntl(srv->fd, F_SETFL, O_NONBLOCK) < 0)
		goto fail;
	if (port < 0)
		srv->port = 49152;
	else
		srv->port = port;

	os_memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = addr->s_addr;

	for (;;) {
		sin.sin_port = htons(srv->port);
		if (bind(srv->fd, (struct sockaddr *) &sin, sizeof(sin)) == 0)
			break;
		if (errno == EADDRINUSE) {
			/* search for unused port */
			if (++srv->port == 65535 || port >= 0)
				goto fail;
			continue;
		}
		wpa_printf(MSG_DEBUG, "HTTP: Failed to bind server port %d: "
			   "%s", srv->port, strerror(errno));
		goto fail;
	}
	if (listen(srv->fd, 10 /* max backlog */) < 0 ||
	    fcntl(srv->fd, F_SETFL, O_NONBLOCK) < 0 ||
	    eloop_register_sock(srv->fd, EVENT_TYPE_READ, http_server_cb,
				srv, NULL))
		goto fail;

	wpa_printf(MSG_DEBUG, "HTTP: Started server on %s:%d",
		   inet_ntoa(*addr), srv->port);

	return srv;

fail:
	http_server_deinit(srv);
	return NULL;
}


void http_server_deinit(struct http_server *srv)
{
	if (srv == NULL)
		return;
	if (srv->fd >= 0) {
		eloop_unregister_sock(srv->fd, EVENT_TYPE_READ);
		close(srv->fd);
	}
	http_request_free_all(srv->requests);

	os_free(srv);
}


int http_server_get_port(struct http_server *srv)
{
	return srv->port;
}
