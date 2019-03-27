/*	$NetBSD: t_unix.c,v 1.11 2013/11/13 21:41:23 christos Exp $	*/

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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
#ifdef __RCSID
__RCSID("$Id: t_unix.c,v 1.11 2013/11/13 21:41:23 christos Exp $");
#else
#define getprogname() argv[0]
#endif

#ifdef __linux__
#define LX -1
#else
#define LX
#endif
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#ifdef TEST
#define FAIL(msg, ...)	err(EXIT_FAILURE, msg, ## __VA_ARGS__)
#else

#include <atf-c.h>
#define FAIL(msg, ...)	\
	do { \
		ATF_CHECK_MSG(0, msg, ## __VA_ARGS__); \
		goto fail; \
	} while (/*CONSTCOND*/0)

#endif

#define OF offsetof(struct sockaddr_un, sun_path)

static void
print(const char *msg, struct sockaddr_un *addr, socklen_t len)
{
	size_t i;

	printf("%s: client socket length: %zu\n", msg, (size_t)len);
	printf("%s: client family %d\n", msg, addr->sun_family);
#ifdef BSD4_4
	printf("%s: client len %d\n", msg, addr->sun_len);
#endif
	printf("%s: socket name: ", msg);
	for (i = 0; i < len - OF; i++) {
		int ch = addr->sun_path[i];
		if (ch < ' ' || '~' < ch)
			printf("\\x%02x", ch);
		else
			printf("%c", ch);
	}
	printf("\n");
}

static int
acc(int s)
{
	char guard1;
	struct sockaddr_un sun;
	char guard2;
	socklen_t len;

	guard1 = guard2 = 's';

	memset(&sun, 0, sizeof(sun));
	len = sizeof(sun);
	if ((s = accept(s, (struct sockaddr *)&sun, &len)) == -1)
		FAIL("accept");
	if (guard1 != 's')
		FAIL("guard1 = '%c'", guard1);
	if (guard2 != 's')
		FAIL("guard2 = '%c'", guard2);
#ifdef DEBUG
	print("accept", &sun, len);
#endif
	if (len != 2)
		FAIL("len %d != 2", len);
	if (sun.sun_family != AF_UNIX)
		FAIL("sun->sun_family %d != AF_UNIX", sun.sun_family);
#ifdef BSD4_4
	if (sun.sun_len != 2)
		FAIL("sun->sun_len %d != 2", sun.sun_len);
#endif
	for (size_t i = 0; i < sizeof(sun.sun_path); i++)
		if (sun.sun_path[i])
			FAIL("sun.sun_path[%zu] %d != NULL", i,
			    sun.sun_path[i]);
	return s;
fail:
	if (s != -1)
		close(s);
	return -1;
}

static int
test(bool closeit, size_t len)
{
	size_t slen;
	socklen_t sl;
	int srvr = -1, clnt = -1, acpt = -1;
	struct sockaddr_un *sock_addr = NULL, *sun = NULL;
	socklen_t sock_addrlen;

	srvr = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srvr == -1)
		FAIL("socket(srvrer)");

	slen = len + OF + 1;
	
	if ((sun = calloc(1, slen)) == NULL)
		FAIL("calloc");

	srvr = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srvr == -1)
		FAIL("socket");

	memset(sun->sun_path, 'a', len);
	sun->sun_path[len] = '\0';
	(void)unlink(sun->sun_path);

	sl = SUN_LEN(sun);
#ifdef BSD4_4
	sun->sun_len = sl;
#endif
	sun->sun_family = AF_UNIX;

	if (bind(srvr, (struct sockaddr *)sun, sl) == -1) {
		if (errno == EINVAL && sl >= 256) {
			close(srvr);
			return -1;
		}
		FAIL("bind");
	}

	if (listen(srvr, SOMAXCONN) == -1)
		FAIL("listen");

	clnt = socket(AF_UNIX, SOCK_STREAM, 0);
	if (clnt == -1)
		FAIL("socket(client)");

	if (connect(clnt, (const struct sockaddr *)sun, sl) == -1)
		FAIL("connect");

	if (closeit) {
		if (close(clnt) == -1)
			FAIL("close");
		clnt = -1;
	}

	acpt = acc(srvr);
#if 0
	/*
	 * Both linux and NetBSD return ENOTCONN, why?
	 */
	if (!closeit) {
		socklen_t peer_addrlen;
		sockaddr_un peer_addr;

		peer_addrlen = sizeof(peer_addr);
		memset(&peer_addr, 0, sizeof(peer_addr));
		if (getpeername(srvr, (struct sockaddr *)&peer_addr,
		    &peer_addrlen) == -1)
			FAIL("getpeername");
		print("peer", &peer_addr, peer_addrlen);
	}
#endif

	if ((sock_addr = calloc(1, slen)) == NULL)
		FAIL("calloc");
	sock_addrlen = slen;
	if (getsockname(srvr, (struct sockaddr *)sock_addr, &sock_addrlen)
	    == -1)
		FAIL("getsockname");
	print("sock", sock_addr, sock_addrlen);

	if (sock_addr->sun_family != AF_UNIX)
		FAIL("sock_addr->sun_family %d != AF_UNIX",
		    sock_addr->sun_family);

	len += OF;
	if (sock_addrlen LX != len)
		FAIL("sock_addr_len %zu != %zu", (size_t)sock_addrlen, len);
#ifdef BSD4_4
	if (sock_addr->sun_len != sl)
		FAIL("sock_addr.sun_len %d != %zu", sock_addr->sun_len,
		    (size_t)sl);
#endif
	for (size_t i = 0; i < slen - OF; i++)
		if (sock_addr->sun_path[i] != sun->sun_path[i])
			FAIL("sock_addr.sun_path[%zu] %d != "
			    "sun->sun_path[%zu] %d\n", i, 
			    sock_addr->sun_path[i], i, sun->sun_path[i]);

	if (acpt != -1)
		(void)close(acpt);
	if (srvr != -1)
		(void)close(srvr);
	if (clnt != -1 && !closeit)
		(void)close(clnt);

	free(sock_addr);
	free(sun);
	return 0;
fail:
	if (acpt != -1)
		(void)close(acpt);
	if (srvr != -1)
		(void)close(srvr);
	if (clnt != -1 && !closeit)
		(void)close(clnt);
	free(sock_addr);
	free(sun);
	return -1;
}

#ifndef TEST

ATF_TC(sockaddr_un_len_exceed);
ATF_TC_HEAD(sockaddr_un_len_exceed, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that exceeding the size of "
	    "unix domain sockets does not trash memory or kernel when "
	    "exceeding the size of the fixed sun_path");
}

ATF_TC_BODY(sockaddr_un_len_exceed, tc)
{
	ATF_REQUIRE_MSG(test(false, 254) == -1, "test(false, 254): %s",
	    strerror(errno));
}

ATF_TC(sockaddr_un_len_max);
ATF_TC_HEAD(sockaddr_un_len_max, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that we can use the maximum "
	    "unix domain socket pathlen (253): 255 - sizeof(sun_len) - "
	    "sizeof(sun_family)");
}

ATF_TC_BODY(sockaddr_un_len_max, tc)
{
	ATF_REQUIRE_MSG(test(false, 253) == 0, "test(false, 253): %s",
	    strerror(errno));
}

ATF_TC(sockaddr_un_closed);
ATF_TC_HEAD(sockaddr_un_closed, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that we can use the accepted "
	    "address of unix domain socket when closed");
}

ATF_TC_BODY(sockaddr_un_closed, tc)
{
	ATF_REQUIRE_MSG(test(true, 100) == 0, "test(true, 100): %s",
	    strerror(errno));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sockaddr_un_len_exceed);
	ATF_TP_ADD_TC(tp, sockaddr_un_len_max);
	ATF_TP_ADD_TC(tp, sockaddr_un_closed);
	return atf_no_error();
}
#else
int
main(int argc, char *argv[])
{
	size_t len;

	if (argc == 1) {
		fprintf(stderr, "Usage: %s <len>\n", getprogname());
		return EXIT_FAILURE;
	}
	test(false, atoi(argv[1]));
	test(true, atoi(argv[1]));
}
#endif
