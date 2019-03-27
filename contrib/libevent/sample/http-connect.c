#include "event2/event-config.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/buffer.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#define VERIFY(cond) do {                       \
	if (!(cond)) {                              \
		fprintf(stderr, "[error] %s\n", #cond); \
	}                                           \
} while (0);                                    \

#define URL_MAX 4096

struct connect_base
{
	struct evhttp_connection *evcon;
	struct evhttp_uri *location;
};

static void get_cb(struct evhttp_request *req, void *arg)
{
	ev_ssize_t len;
	struct evbuffer *evbuf;

	VERIFY(req);

	evbuf = evhttp_request_get_input_buffer(req);
	len = evbuffer_get_length(evbuf);
	fwrite(evbuffer_pullup(evbuf, len), len, 1, stdout);
	evbuffer_drain(evbuf, len);
}

static void connect_cb(struct evhttp_request *proxy_req, void *arg)
{
	char buffer[URL_MAX];

	struct connect_base *base = arg;
	struct evhttp_connection *evcon = base->evcon;
	struct evhttp_uri *location = base->location;

	VERIFY(proxy_req);
	if (evcon) {
		struct evhttp_request *req = evhttp_request_new(get_cb, NULL);
		evhttp_add_header(req->output_headers, "Connection", "close");
		VERIFY(!evhttp_make_request(evcon, req, EVHTTP_REQ_GET,
			evhttp_uri_join(location, buffer, URL_MAX)));
	}
}

int main(int argc, const char **argv)
{
	char buffer[URL_MAX];

	struct evhttp_uri *host_port;
	struct evhttp_uri *location;
	struct evhttp_uri *proxy;

	struct event_base *base;
	struct evhttp_connection *evcon;
	struct evhttp_request *req;

	struct connect_base connect_base;

	if (argc != 3) {
		printf("Usage: %s proxy url\n", argv[0]);
		return 1;
	}

	{
		proxy = evhttp_uri_parse(argv[1]);
		VERIFY(evhttp_uri_get_host(proxy));
		VERIFY(evhttp_uri_get_port(proxy) > 0);
	}
	{
		host_port = evhttp_uri_parse(argv[2]);
		evhttp_uri_set_scheme(host_port, NULL);
		evhttp_uri_set_userinfo(host_port, NULL);
		evhttp_uri_set_path(host_port, NULL);
		evhttp_uri_set_query(host_port, NULL);
		evhttp_uri_set_fragment(host_port, NULL);
		VERIFY(evhttp_uri_get_host(host_port));
		VERIFY(evhttp_uri_get_port(host_port) > 0);
	}
	{
		location = evhttp_uri_parse(argv[2]);
		evhttp_uri_set_scheme(location, NULL);
		evhttp_uri_set_userinfo(location, 0);
		evhttp_uri_set_host(location, NULL);
		evhttp_uri_set_port(location, -1);
	}

	VERIFY(base = event_base_new());
	VERIFY(evcon = evhttp_connection_base_new(base, NULL,
		evhttp_uri_get_host(proxy), evhttp_uri_get_port(proxy)));
	connect_base.evcon = evcon;
	connect_base.location = location;
	VERIFY(req = evhttp_request_new(connect_cb, &connect_base));

	evhttp_add_header(req->output_headers, "Connection", "keep-alive");
	evhttp_add_header(req->output_headers, "Proxy-Connection", "keep-alive");
	evutil_snprintf(buffer, URL_MAX, "%s:%d",
		evhttp_uri_get_host(host_port), evhttp_uri_get_port(host_port));
	evhttp_make_request(evcon, req, EVHTTP_REQ_CONNECT, buffer);

	event_base_dispatch(base);
	evhttp_connection_free(evcon);
	event_base_free(base);
	evhttp_uri_free(proxy);
	evhttp_uri_free(host_port);
	evhttp_uri_free(location);
	return 0;
}
