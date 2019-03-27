/*
 * http_client - HTTP client
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
#include "http_client.h"


#define HTTP_CLIENT_TIMEOUT_SEC 30


struct http_client {
	struct sockaddr_in dst;
	int sd;
	struct wpabuf *req;
	size_t req_pos;
	size_t max_response;

	void (*cb)(void *ctx, struct http_client *c,
		   enum http_client_event event);
	void *cb_ctx;
	struct httpread *hread;
	struct wpabuf body;
};


static void http_client_timeout(void *eloop_data, void *user_ctx)
{
	struct http_client *c = eloop_data;
	wpa_printf(MSG_DEBUG, "HTTP: Timeout (c=%p)", c);
	c->cb(c->cb_ctx, c, HTTP_CLIENT_TIMEOUT);
}


static void http_client_got_response(struct httpread *handle, void *cookie,
				     enum httpread_event e)
{
	struct http_client *c = cookie;

	wpa_printf(MSG_DEBUG, "HTTP: httpread callback: handle=%p cookie=%p "
		   "e=%d", handle, cookie, e);

	eloop_cancel_timeout(http_client_timeout, c, NULL);
	switch (e) {
	case HTTPREAD_EVENT_FILE_READY:
		if (httpread_hdr_type_get(c->hread) == HTTPREAD_HDR_TYPE_REPLY)
		{
			int reply_code = httpread_reply_code_get(c->hread);
			if (reply_code == 200 /* OK */) {
				wpa_printf(MSG_DEBUG, "HTTP: Response OK from "
					   "%s:%d",
					   inet_ntoa(c->dst.sin_addr),
					   ntohs(c->dst.sin_port));
				c->cb(c->cb_ctx, c, HTTP_CLIENT_OK);
			} else {
				wpa_printf(MSG_DEBUG, "HTTP: Error %d from "
					   "%s:%d", reply_code,
					   inet_ntoa(c->dst.sin_addr),
					   ntohs(c->dst.sin_port));
				c->cb(c->cb_ctx, c, HTTP_CLIENT_INVALID_REPLY);
			}
		} else
			c->cb(c->cb_ctx, c, HTTP_CLIENT_INVALID_REPLY);
		break;
	case HTTPREAD_EVENT_TIMEOUT:
		c->cb(c->cb_ctx, c, HTTP_CLIENT_TIMEOUT);
		break;
	case HTTPREAD_EVENT_ERROR:
		c->cb(c->cb_ctx, c, HTTP_CLIENT_FAILED);
		break;
	}
}


static void http_client_tx_ready(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct http_client *c = eloop_ctx;
	int res;
	size_t send_len;

	send_len = wpabuf_len(c->req) - c->req_pos;
	wpa_printf(MSG_DEBUG, "HTTP: Send client request to %s:%d (%lu of %lu "
		   "bytes remaining)",
		   inet_ntoa(c->dst.sin_addr), ntohs(c->dst.sin_port),
		   (unsigned long) wpabuf_len(c->req),
		   (unsigned long) send_len);

	res = send(c->sd, wpabuf_head_u8(c->req) + c->req_pos, send_len, 0);
	if (res < 0) {
		wpa_printf(MSG_DEBUG, "HTTP: Failed to send buffer: %s",
			   strerror(errno));
		eloop_unregister_sock(c->sd, EVENT_TYPE_WRITE);
		c->cb(c->cb_ctx, c, HTTP_CLIENT_FAILED);
		return;
	}

	if ((size_t) res < send_len) {
		wpa_printf(MSG_DEBUG, "HTTP: Sent %d of %lu bytes; %lu bytes "
			   "remaining",
			   res, (unsigned long) wpabuf_len(c->req),
			   (unsigned long) send_len - res);
		c->req_pos += res;
		return;
	}

	wpa_printf(MSG_DEBUG, "HTTP: Full client request sent to %s:%d",
		   inet_ntoa(c->dst.sin_addr), ntohs(c->dst.sin_port));
	eloop_unregister_sock(c->sd, EVENT_TYPE_WRITE);
	wpabuf_free(c->req);
	c->req = NULL;

	c->hread = httpread_create(c->sd, http_client_got_response, c,
				   c->max_response, HTTP_CLIENT_TIMEOUT_SEC);
	if (c->hread == NULL) {
		c->cb(c->cb_ctx, c, HTTP_CLIENT_FAILED);
		return;
	}
}


struct http_client * http_client_addr(struct sockaddr_in *dst,
				      struct wpabuf *req, size_t max_response,
				      void (*cb)(void *ctx,
						 struct http_client *c,
						 enum http_client_event event),
				      void *cb_ctx)
{
	struct http_client *c;

	c = os_zalloc(sizeof(*c));
	if (c == NULL)
		return NULL;
	c->sd = -1;
	c->dst = *dst;
	c->max_response = max_response;
	c->cb = cb;
	c->cb_ctx = cb_ctx;

	c->sd = socket(AF_INET, SOCK_STREAM, 0);
	if (c->sd < 0)
		goto fail;

	if (fcntl(c->sd, F_SETFL, O_NONBLOCK) != 0) {
		wpa_printf(MSG_DEBUG, "HTTP: fnctl(O_NONBLOCK) failed: %s",
			   strerror(errno));
		goto fail;
	}

	if (connect(c->sd, (struct sockaddr *) dst, sizeof(*dst))) {
		if (errno != EINPROGRESS) {
			wpa_printf(MSG_DEBUG, "HTTP: Failed to connect: %s",
				   strerror(errno));
			goto fail;
		}

		/*
		 * Continue connecting in the background; eloop will call us
		 * once the connection is ready (or failed).
		 */
	}

	if (eloop_register_sock(c->sd, EVENT_TYPE_WRITE, http_client_tx_ready,
				c, NULL) ||
	    eloop_register_timeout(HTTP_CLIENT_TIMEOUT_SEC, 0,
				   http_client_timeout, c, NULL))
		goto fail;

	c->req = req;

	return c;

fail:
	http_client_free(c);
	return NULL;
}


char * http_client_url_parse(const char *url, struct sockaddr_in *dst,
			     char **ret_path)
{
	char *u, *addr, *port, *path;

	u = os_strdup(url);
	if (u == NULL)
		return NULL;

	os_memset(dst, 0, sizeof(*dst));
	dst->sin_family = AF_INET;
	addr = u + 7;
	path = os_strchr(addr, '/');
	port = os_strchr(addr, ':');
	if (path == NULL) {
		path = "/";
	} else {
		*path = '\0'; /* temporary nul termination for address */
		if (port > path)
			port = NULL;
	}
	if (port)
		*port++ = '\0';

	if (inet_aton(addr, &dst->sin_addr) == 0) {
		/* TODO: name lookup */
		wpa_printf(MSG_DEBUG, "HTTP: Unsupported address in URL '%s' "
			   "(addr='%s' port='%s')",
			   url, addr, port);
		os_free(u);
		return NULL;
	}

	if (port)
		dst->sin_port = htons(atoi(port));
	else
		dst->sin_port = htons(80);

	if (*path == '\0') {
		/* remove temporary nul termination for address */
		*path = '/';
	}

	*ret_path = path;

	return u;
}


struct http_client * http_client_url(const char *url,
				     struct wpabuf *req, size_t max_response,
				     void (*cb)(void *ctx,
						struct http_client *c,
						enum http_client_event event),
				     void *cb_ctx)
{
	struct sockaddr_in dst;
	struct http_client *c;
	char *u, *path;
	struct wpabuf *req_buf = NULL;

	if (os_strncmp(url, "http://", 7) != 0)
		return NULL;
	u = http_client_url_parse(url, &dst, &path);
	if (u == NULL)
		return NULL;

	if (req == NULL) {
		req_buf = wpabuf_alloc(os_strlen(url) + 1000);
		if (req_buf == NULL) {
			os_free(u);
			return NULL;
		}
		req = req_buf;
		wpabuf_printf(req,
			      "GET %s HTTP/1.1\r\n"
			      "Cache-Control: no-cache\r\n"
			      "Pragma: no-cache\r\n"
			      "Accept: text/xml, application/xml\r\n"
			      "User-Agent: wpa_supplicant\r\n"
			      "Host: %s:%d\r\n"
			      "\r\n",
			      path, inet_ntoa(dst.sin_addr),
			      ntohs(dst.sin_port));
	}
	os_free(u);

	c = http_client_addr(&dst, req, max_response, cb, cb_ctx);
	if (c == NULL) {
		wpabuf_free(req_buf);
		return NULL;
	}

	return c;
}


void http_client_free(struct http_client *c)
{
	if (c == NULL)
		return;
	httpread_destroy(c->hread);
	wpabuf_free(c->req);
	if (c->sd >= 0) {
		eloop_unregister_sock(c->sd, EVENT_TYPE_WRITE);
		close(c->sd);
	}
	eloop_cancel_timeout(http_client_timeout, c, NULL);
	os_free(c);
}


struct wpabuf * http_client_get_body(struct http_client *c)
{
	if (c->hread == NULL)
		return NULL;
	wpabuf_set(&c->body, httpread_data_get(c->hread),
		   httpread_length_get(c->hread));
	return &c->body;
}


char * http_client_get_hdr_line(struct http_client *c, const char *tag)
{
	if (c->hread == NULL)
		return NULL;
	return httpread_hdr_line_get(c->hread, tag);
}


char * http_link_update(char *url, const char *base)
{
	char *n;
	size_t len;
	const char *pos;

	/* RFC 2396, Chapter 5.2 */
	/* TODO: consider adding all cases described in RFC 2396 */

	if (url == NULL)
		return NULL;

	if (os_strncmp(url, "http://", 7) == 0)
		return url; /* absolute link */

	if (os_strncmp(base, "http://", 7) != 0)
		return url; /* unable to handle base URL */

	len = os_strlen(url) + 1 + os_strlen(base) + 1;
	n = os_malloc(len);
	if (n == NULL)
		return url; /* failed */

	if (url[0] == '/') {
		pos = os_strchr(base + 7, '/');
		if (pos == NULL) {
			os_snprintf(n, len, "%s%s", base, url);
		} else {
			os_memcpy(n, base, pos - base);
			os_memcpy(n + (pos - base), url, os_strlen(url) + 1);
		}
	} else {
		pos = os_strrchr(base + 7, '/');
		if (pos == NULL) {
			os_snprintf(n, len, "%s/%s", base, url);
		} else {
			os_memcpy(n, base, pos - base + 1);
			os_memcpy(n + (pos - base) + 1, url, os_strlen(url) +
				  1);
		}
	}

	os_free(url);

	return n;
}
