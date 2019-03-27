/*
 * Copyright 2008-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. The name of the author may not be used to endorse or promote products
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
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/util.h"
#include "event2/http.h"
#include "event2/thread.h"

static void http_basic_cb(struct evhttp_request *req, void *arg);

static char *content;
static size_t content_len = 0;

static void
http_basic_cb(struct evhttp_request *req, void *arg)
{
	struct evbuffer *evb = evbuffer_new();

	evbuffer_add(evb, content, content_len);

	/* allow sending of an empty reply */
	evhttp_send_reply(req, HTTP_OK, "Everything is fine", evb);

	evbuffer_free(evb);
}

#if LIBEVENT_VERSION_NUMBER >= 0x02000200
static void
http_ref_cb(struct evhttp_request *req, void *arg)
{
	struct evbuffer *evb = evbuffer_new();

	evbuffer_add_reference(evb, content, content_len, NULL, NULL);

	/* allow sending of an empty reply */
	evhttp_send_reply(req, HTTP_OK, "Everything is fine", evb);

	evbuffer_free(evb);
}
#endif

int
main(int argc, char **argv)
{
	struct event_config *cfg = event_config_new();
	struct event_base *base;
	struct evhttp *http;
	int i;
	int c;
	int use_iocp = 0;
	ev_uint16_t port = 8080;
	char *endptr = NULL;

#ifdef _WIN32
	WSADATA WSAData;
	WSAStartup(0x101, &WSAData);
#else
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return (1);
#endif

	for (i = 1; i < argc; ++i) {
		if (*argv[i] != '-')
			continue;

		c = argv[i][1];

		if ((c == 'p' || c == 'l') && i + 1 >= argc) {
			fprintf(stderr, "-%c requires argument.\n", c);
			exit(1);
		}

		switch (c) {
		case 'p':
			if (i+1 >= argc || !argv[i+1]) {
				fprintf(stderr, "Missing port\n");
				exit(1);
			}
			port = (int)strtol(argv[i+1], &endptr, 10);
			if (*endptr != '\0') {
				fprintf(stderr, "Bad port\n");
				exit(1);
			}
			break;
		case 'l':
			if (i+1 >= argc || !argv[i+1]) {
				fprintf(stderr, "Missing content length\n");
				exit(1);
			}
			content_len = (size_t)strtol(argv[i+1], &endptr, 10);
			if (*endptr != '\0' || content_len == 0) {
				fprintf(stderr, "Bad content length\n");
				exit(1);
			}
			break;
#ifdef _WIN32
		case 'i':
			use_iocp = 1;
#ifdef EVTHREAD_USE_WINDOWS_THREADS_IMPLEMENTED
			evthread_use_windows_threads();
#endif
			event_config_set_flag(cfg,EVENT_BASE_FLAG_STARTUP_IOCP);
			break;
#endif
		default:
			fprintf(stderr, "Illegal argument \"%c\"\n", c);
			exit(1);
		}
	}

	base = event_base_new_with_config(cfg);
	if (!base) {
		fprintf(stderr, "creating event_base failed. Exiting.\n");
		return 1;
	}

	http = evhttp_new(base);

	content = malloc(content_len);
	if (content == NULL) {
		fprintf(stderr, "Cannot allocate content\n");
		exit(1);
	} else {
		int i = 0;
		for (i = 0; i < (int)content_len; ++i)
			content[i] = (i & 255);
	}

	evhttp_set_cb(http, "/ind", http_basic_cb, NULL);
	fprintf(stderr, "/ind - basic content (memory copy)\n");

	evhttp_set_cb(http, "/ref", http_ref_cb, NULL);
	fprintf(stderr, "/ref - basic content (reference)\n");

	fprintf(stderr, "Serving %d bytes on port %d using %s\n",
	    (int)content_len, port,
	    use_iocp? "IOCP" : event_base_get_method(base));

	evhttp_bind_socket(http, "0.0.0.0", port);

#ifdef _WIN32
	if (use_iocp) {
		struct timeval tv={99999999,0};
		event_base_loopexit(base, &tv);
	}
#endif
	event_base_dispatch(base);

#ifdef _WIN32
	WSACleanup();
#endif

	/* NOTREACHED */
	return (0);
}
