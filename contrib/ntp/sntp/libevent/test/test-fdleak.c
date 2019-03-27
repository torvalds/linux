/*
 * Copyright (c) 2012 Ross Lagerwall <rosslagerwall@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "event2/event-config.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef EVENT__HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include "event2/event.h"
#include "event2/bufferevent.h"
#include "event2/buffer.h"
#include "event2/listener.h"

/* Number of requests to make. Setting this too high might result in the machine
   running out of ephemeral ports */
#ifdef _WIN32
#define MAX_REQUESTS 1000
#else
#define MAX_REQUESTS 4000
#endif

/* Provide storage for the address, both for the server & the clients */
static struct sockaddr_in saddr;

/* Number of sucessful requests so far */
static int num_requests;

static void start_client(struct event_base *base);

static void
my_perror(const char *s)
{
	fprintf(stderr, "%s: %s",
	    s, evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
}

/*
===============================================
Server functions
===============================================
*/

/* Read a byte from the client and write it back */
static void
server_read_cb(struct bufferevent *bev, void *ctx)
{
	while (evbuffer_get_length(bufferevent_get_input(bev))) {
		unsigned char tmp;
		bufferevent_read(bev, &tmp, 1);
		bufferevent_write(bev, &tmp, 1);
	}
}

/* Wait for an EOF and then free the bufferevent */
static void
server_event_cb(struct bufferevent *bev, short events, void *ctx)
{
	if (events & BEV_EVENT_ERROR) {
		my_perror("Error from bufferevent");
		exit(1);
	} else if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		bufferevent_free(bev);
	}
}

/* Accept a client socket and set it up to for reading & writing */
static void
listener_accept_cb(struct evconnlistener *listener, evutil_socket_t sock,
                   struct sockaddr *addr, int len, void *ptr)
{
	struct event_base *base = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(base, sock,
                                                         BEV_OPT_CLOSE_ON_FREE);

	bufferevent_setcb(bev, server_read_cb, NULL, server_event_cb, NULL);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

/* Start the server listening on a random port and start the first client. */
static void
start_loop(void)
{
	struct event_base *base;
	struct evconnlistener *listener;
	struct sockaddr_storage ss;
	ev_socklen_t socklen = sizeof(ss);
	evutil_socket_t fd;

	base = event_base_new();
	if (base == NULL) {
		puts("Could not open event base!");
		exit(1);
	}

	listener = evconnlistener_new_bind(base, listener_accept_cb, NULL,
	    LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE,
	    -1, (struct sockaddr *)&saddr, sizeof(saddr));
	if (listener == NULL) {
		my_perror("Could not create listener!");
		exit(1);
	}
	fd = evconnlistener_get_fd(listener);
	if (fd < 0) {
		puts("Couldn't get fd from listener");
		exit(1);
	}
	if (getsockname(fd, (struct sockaddr *)&ss, &socklen) < 0) {
		my_perror("getsockname()");
		exit(1);
	}
	memcpy(&saddr, &ss, sizeof(saddr));
	if (saddr.sin_family != AF_INET) {
		puts("AF mismatch from getsockname().");
		exit(1);
	}

	start_client(base);

	event_base_dispatch(base);
}

/*
===============================================
Client functions
===============================================
*/

/* Check that the server sends back the same byte that the client sent.
   If MAX_REQUESTS have been reached, exit. Otherwise, start another client. */
static void
client_read_cb(struct bufferevent *bev, void *ctx)
{
	unsigned char tmp;
	struct event_base *base = bufferevent_get_base(bev);

	bufferevent_read(bev, &tmp, 1);
	if (tmp != 'A') {
		puts("Incorrect data received!");
		exit(2);
	}
	bufferevent_free(bev);

	num_requests++;
	if (num_requests == MAX_REQUESTS) {
		event_base_loopbreak(base);
	} else {
		start_client(base);
	}
}

/* Send a byte to the server. */
static void
client_event_cb(struct bufferevent *bev, short events, void *ctx)
{
	if (events & BEV_EVENT_CONNECTED) {
		unsigned char tmp = 'A';
		bufferevent_write(bev, &tmp, 1);
	} else if (events & BEV_EVENT_ERROR) {
		puts("Client socket got error!");
		exit(2);
	}

	bufferevent_enable(bev, EV_READ);
}

/* Open a client socket to connect to localhost on sin */
static void
start_client(struct event_base *base)
{
	struct bufferevent *bev = bufferevent_socket_new(base, -1,
                                                         BEV_OPT_CLOSE_ON_FREE);
	bufferevent_setcb(bev, client_read_cb, NULL, client_event_cb, NULL);

	if (bufferevent_socket_connect(bev, (struct sockaddr *)&saddr,
                                       sizeof(saddr)) < 0) {
		my_perror("Could not connect!");
		bufferevent_free(bev);
		exit(2);
	}
}

int
main(int argc, char **argv)
{
#ifdef EVENT__HAVE_SETRLIMIT
	/* Set the fd limit to a low value so that any fd leak is caught without
	making many requests. */
	struct rlimit rl;
	rl.rlim_cur = rl.rlim_max = 20;
	if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
		my_perror("setrlimit");
		exit(3);
	}
#endif

#ifdef _WIN32
	WSADATA WSAData;
	WSAStartup(0x101, &WSAData);
#endif

	/* Set up an address, used by both client & server. */
	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(0x7f000001);
	saddr.sin_port = 0; /* Tell the implementation to pick a port. */

	start_loop();

	return 0;
}

/* XXX why does this test cause so much latency sometimes (OSX 10.5)? */
