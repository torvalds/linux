/*
 * Copyright (c) 2010-2012 Niels Provos and Nick Mathewson
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
#include "../util-internal.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#endif

#include "event2/event-config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#ifdef EVENT__HAVE_NETINET_IN6_H
#include <netinet/in6.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "event2/dns.h"
#include "event2/dns_struct.h"
#include "event2/event.h"
#include "event2/event_compat.h"
#include "event2/util.h"
#include "event2/listener.h"
#include "event2/bufferevent.h"
#include "log-internal.h"
#include "regress.h"
#include "regress_testutils.h"

/* globals */
static struct evdns_server_port *dns_port;
evutil_socket_t dns_sock = -1;

/* Helper: return the port that a socket is bound on, in host order. */
int
regress_get_socket_port(evutil_socket_t fd)
{
	struct sockaddr_storage ss;
	ev_socklen_t socklen = sizeof(ss);
	if (getsockname(fd, (struct sockaddr*)&ss, &socklen) != 0)
		return -1;
	if (ss.ss_family == AF_INET)
		return ntohs( ((struct sockaddr_in*)&ss)->sin_port);
	else if (ss.ss_family == AF_INET6)
		return ntohs( ((struct sockaddr_in6*)&ss)->sin6_port);
	else
		return -1;
}

struct evdns_server_port *
regress_get_dnsserver(struct event_base *base,
    ev_uint16_t *portnum,
    evutil_socket_t *psock,
    evdns_request_callback_fn_type cb,
    void *arg)
{
	struct evdns_server_port *port = NULL;
	evutil_socket_t sock;
	struct sockaddr_in my_addr;

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		tt_abort_perror("socket");
	}

	evutil_make_socket_nonblocking(sock);

	memset(&my_addr, 0, sizeof(my_addr));
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(*portnum);
	my_addr.sin_addr.s_addr = htonl(0x7f000001UL);
	if (bind(sock, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
		evutil_closesocket(sock);
		tt_abort_perror("bind");
	}
	port = evdns_add_server_port_with_base(base, sock, 0, cb, arg);
	if (!*portnum)
		*portnum = regress_get_socket_port(sock);
	if (psock)
		*psock = sock;

	return port;
end:
	return NULL;
}

void
regress_clean_dnsserver(void)
{
	if (dns_port) {
		evdns_close_server_port(dns_port);
		dns_port = NULL;
	}
	if (dns_sock >= 0) {
		evutil_closesocket(dns_sock);
		dns_sock = -1;
	}
}

static void strtolower(char *s)
{
	while (*s) {
		*s = EVUTIL_TOLOWER_(*s);
		++s;
	}
}
void
regress_dns_server_cb(struct evdns_server_request *req, void *data)
{
	struct regress_dns_server_table *tab = data;
	char *question;

	if (req->nquestions != 1)
		TT_DIE(("Only handling one question at a time; got %d",
			req->nquestions));

	question = req->questions[0]->name;

	while (tab->q && evutil_ascii_strcasecmp(question, tab->q) &&
	    strcmp("*", tab->q))
		++tab;
	if (tab->q == NULL)
		TT_DIE(("Unexpected question: '%s'", question));

	++tab->seen;

	if (tab->lower)
		strtolower(question);

	if (!strcmp(tab->anstype, "err")) {
		int err = atoi(tab->ans);
		tt_assert(! evdns_server_request_respond(req, err));
		return;
	} else if (!strcmp(tab->anstype, "errsoa")) {
		int err = atoi(tab->ans);
		char soa_record[] =
			"\x04" "dns1" "\x05" "icann" "\x03" "org" "\0"
			"\x0a" "hostmaster" "\x05" "icann" "\x03" "org" "\0"
			"\x77\xde\x5e\xba" /* serial */
			"\x00\x00\x1c\x20" /* refreshtime = 2h */
			"\x00\x00\x0e\x10" /* retry = 1h */
			"\x00\x12\x75\x00" /* expiration = 14d */
			"\x00\x00\x0e\x10" /* min.ttl = 1h */
			;
		evdns_server_request_add_reply(
			req, EVDNS_AUTHORITY_SECTION,
			"example.com", EVDNS_TYPE_SOA, EVDNS_CLASS_INET,
			42, sizeof(soa_record) - 1, 0, soa_record);
		tt_assert(! evdns_server_request_respond(req, err));
		return;
	} else if (!strcmp(tab->anstype, "A")) {
		struct in_addr in;
		if (!evutil_inet_pton(AF_INET, tab->ans, &in)) {
			TT_DIE(("Bad A value %s in table", tab->ans));
		}
		evdns_server_request_add_a_reply(req, question, 1, &in.s_addr,
		    100);
	} else if (!strcmp(tab->anstype, "AAAA")) {
		struct in6_addr in6;
		if (!evutil_inet_pton(AF_INET6, tab->ans, &in6)) {
			TT_DIE(("Bad AAAA value %s in table", tab->ans));
		}
		evdns_server_request_add_aaaa_reply(req,
		    question, 1, &in6.s6_addr, 100);
	} else {
		TT_DIE(("Weird table entry with type '%s'", tab->anstype));
	}
	tt_assert(! evdns_server_request_respond(req, 0))
	return;
end:
	tt_want(! evdns_server_request_drop(req));
}

int
regress_dnsserver(struct event_base *base, ev_uint16_t *port,
    struct regress_dns_server_table *search_table)
{
	dns_port = regress_get_dnsserver(base, port, &dns_sock,
	    regress_dns_server_cb, search_table);
	return dns_port != NULL;
}

int
regress_get_listener_addr(struct evconnlistener *lev,
    struct sockaddr *sa, ev_socklen_t *socklen)
{
	evutil_socket_t s = evconnlistener_get_fd(lev);
	if (s <= 0)
		return -1;
	return getsockname(s, sa, socklen);
}
