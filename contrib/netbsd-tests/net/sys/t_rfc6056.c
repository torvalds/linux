/* $NetBSD: t_rfc6056.c,v 1.3 2012/06/22 14:54:35 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_rfc6056.c,v 1.3 2012/06/22 14:54:35 christos Exp $");

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <netdb.h>
#include <err.h>

#include <atf-c.h>

static void
test(const char *hostname, const char *service, int family, int al)
{
	static const char hello[] = "hello\n";
	int s, error, proto, option;
	struct sockaddr_storage ss;
	struct addrinfo hints, *res;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;

	switch (family) {
	case AF_INET:
		proto = IPPROTO_IP;
		option = IP_PORTALGO;
		break;
	case AF_INET6:
		proto = IPPROTO_IPV6;
		option = IPV6_PORTALGO;
		break;
	default:
		abort();
	}

	error = getaddrinfo(hostname, service, &hints, &res);
	if (error)
		errx(EXIT_FAILURE, "Cannot get address for %s (%s)",
		    hostname, gai_strerror(error));
	
	s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s == -1)
		err(EXIT_FAILURE, "socket");
	
	if (setsockopt(s, proto, option, &al, sizeof(al)) == -1)
		err(EXIT_FAILURE, "setsockopt");

	memset(&ss, 0, sizeof(ss));
	ss.ss_len = res->ai_addrlen;
	ss.ss_family = res->ai_family;

	if (bind(s, (struct sockaddr *)&ss, ss.ss_len) == -1)
		err(EXIT_FAILURE, "bind");
		
	if (sendto(s, hello, sizeof(hello) - 1, 0,
	    res->ai_addr, res->ai_addrlen) == -1)
		err(EXIT_FAILURE, "sendto");

	if (close(s) == -1)
		err(EXIT_FAILURE, "close");

	s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s == -1)
		err(EXIT_FAILURE, "socket");

	if (setsockopt(s, proto, option, &al, sizeof(al)) == -1)
		err(EXIT_FAILURE, "setsockopt");

	if (connect(s, res->ai_addr, res->ai_addrlen) == -1)
		err(EXIT_FAILURE, "connect");

	if (send(s, hello, sizeof(hello) - 1, 0) == -1)
		err(EXIT_FAILURE, "send");

	if (close(s) == -1)
		err(EXIT_FAILURE, "close");

	freeaddrinfo(res);
}

ATF_TC(inet4);
ATF_TC_HEAD(inet4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks random port allocation "
	    "for ipv4");
}

ATF_TC_BODY(inet4, tc)
{
	for (int i = 0; i < 6; i++)
		test("localhost", "http", AF_INET, i);
}

ATF_TC(inet6);
ATF_TC_HEAD(inet6, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks random port allocation "
	    "for ipv6");
}

ATF_TC_BODY(inet6, tc)
{
	for (int i = 0; i < 6; i++)
		test("localhost", "http", AF_INET6, i);
}

ATF_TP_ADD_TCS(tp)
{
        ATF_TP_ADD_TC(tp, inet4);
        ATF_TP_ADD_TC(tp, inet6);

	return atf_no_error();
}
