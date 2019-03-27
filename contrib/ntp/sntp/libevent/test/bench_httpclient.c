/*
 * Copyright 2009-2012 Niels Provos and Nick Mathewson
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

/* for EVUTIL_ERR_CONNECT_RETRIABLE macro */
#include "util-internal.h"

#include <sys/types.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "event2/event.h"
#include "event2/bufferevent.h"
#include "event2/buffer.h"
#include "event2/util.h"

const char *resource = NULL;
struct event_base *base = NULL;

int total_n_handled = 0;
int total_n_errors = 0;
int total_n_launched = 0;
size_t total_n_bytes = 0;
struct timeval total_time = {0,0};
int n_errors = 0;

const int PARALLELISM = 200;
const int N_REQUESTS = 20000;

struct request_info {
	size_t n_read;
	struct timeval started;
};

static int launch_request(void);
static void readcb(struct bufferevent *b, void *arg);
static void errorcb(struct bufferevent *b, short what, void *arg);

static void
readcb(struct bufferevent *b, void *arg)
{
	struct request_info *ri = arg;
	struct evbuffer *input = bufferevent_get_input(b);
	size_t n = evbuffer_get_length(input);

	ri->n_read += n;
	evbuffer_drain(input, n);
}

static void
errorcb(struct bufferevent *b, short what, void *arg)
{
	struct request_info *ri = arg;
	struct timeval now, diff;
	if (what & BEV_EVENT_EOF) {
		++total_n_handled;
		total_n_bytes += ri->n_read;
		evutil_gettimeofday(&now, NULL);
		evutil_timersub(&now, &ri->started, &diff);
		evutil_timeradd(&diff, &total_time, &total_time);

		if (total_n_handled && (total_n_handled%1000)==0)
			printf("%d requests done\n",total_n_handled);

		if (total_n_launched < N_REQUESTS) {
			if (launch_request() < 0)
				perror("Can't launch");
		}
	} else {
		++total_n_errors;
		perror("Unexpected error");
	}

	bufferevent_setcb(b, NULL, NULL, NULL, NULL);
	free(ri);
	bufferevent_disable(b, EV_READ|EV_WRITE);
	bufferevent_free(b);
}

static void
frob_socket(evutil_socket_t sock)
{
#ifdef HAVE_SO_LINGER
	struct linger l;
#endif
	int one = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)&one, sizeof(one))<0)
		perror("setsockopt(SO_REUSEADDR)");
#ifdef HAVE_SO_LINGER
	l.l_onoff = 1;
	l.l_linger = 0;
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (void*)&l, sizeof(l))<0)
		perror("setsockopt(SO_LINGER)");
#endif
}

static int
launch_request(void)
{
	evutil_socket_t sock;
	struct sockaddr_in sin;
	struct bufferevent *b;

	struct request_info *ri;

	memset(&sin, 0, sizeof(sin));

	++total_n_launched;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0x7f000001);
	sin.sin_port = htons(8080);
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;
	if (evutil_make_socket_nonblocking(sock) < 0) {
		evutil_closesocket(sock);
		return -1;
	}
	frob_socket(sock);
	if (connect(sock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
		int e = errno;
		if (! EVUTIL_ERR_CONNECT_RETRIABLE(e)) {
			evutil_closesocket(sock);
			return -1;
		}
	}

	ri = malloc(sizeof(*ri));
	if (ri == NULL) {
		printf("Unable to allocate memory in launch_request()\n");
		return -1;
	}
	ri->n_read = 0;
	evutil_gettimeofday(&ri->started, NULL);

	b = bufferevent_socket_new(base, sock, BEV_OPT_CLOSE_ON_FREE);

	bufferevent_setcb(b, readcb, NULL, errorcb, ri);
	bufferevent_enable(b, EV_READ|EV_WRITE);

	evbuffer_add_printf(bufferevent_get_output(b),
	    "GET %s HTTP/1.0\r\n\r\n", resource);

	return 0;
}


int
main(int argc, char **argv)
{
	int i;
	struct timeval start, end, total;
	long long usec;
	double throughput;
	resource = "/ref";

	setvbuf(stdout, NULL, _IONBF, 0);

	base = event_base_new();

	for (i=0; i < PARALLELISM; ++i) {
		if (launch_request() < 0)
			perror("launch");
	}

	evutil_gettimeofday(&start, NULL);

	event_base_dispatch(base);

	evutil_gettimeofday(&end, NULL);
	evutil_timersub(&end, &start, &total);
	usec = total_time.tv_sec * (long long)1000000 + total_time.tv_usec;

	if (!total_n_handled) {
		puts("Nothing worked.  You probably did something dumb.");
		return 0;
	}


	throughput = total_n_handled /
	    (total.tv_sec+ ((double)total.tv_usec)/1000000.0);

#ifdef _WIN32
#define I64_FMT "%I64d"
#define I64_TYP __int64
#else
#define I64_FMT "%lld"
#define I64_TYP long long int
#endif

	printf("\n%d requests in %d.%06d sec. (%.2f throughput)\n"
	    "Each took about %.02f msec latency\n"
	    I64_FMT "bytes read. %d errors.\n",
	    total_n_handled,
	    (int)total.tv_sec, (int)total.tv_usec,
	    throughput,
	    (double)(usec/1000) / total_n_handled,
	    (I64_TYP)total_n_bytes, n_errors);

	return 0;
}
