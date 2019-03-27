/* $NetBSD: t_sockaddr_snprintf.c,v 1.1 2010/07/16 13:56:32 jmmv Exp $ */

/*
 * Copyright (c) 2002, 2004, 2008, 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code was contributed to The NetBSD Foundation by Christos Zoulas.
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
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_sockaddr_snprintf.c,v 1.1 2010/07/16 13:56:32 jmmv Exp $");

#include <sys/socket.h>		/* AF_ */
#include <sys/un.h>			/* sun */

#include <net/if_dl.h>		/* sdl */
#include <netatalk/at.h>	/* sat */
#include <netinet/in.h>		/* sin/sin6 */

#include <string.h>
#include <util.h>

#include <atf-c.h>

ATF_TC(sockaddr_snprintf_in);
ATF_TC_HEAD(sockaddr_snprintf_in, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks sockaddr_snprintf(3) with sockaddr_in");
}
ATF_TC_BODY(sockaddr_snprintf_in, tc)
{
	char buf[1024];
	struct sockaddr_in sin4;
	int i;

	memset(&sin4, 0, sizeof(sin4));
	sin4.sin_len = sizeof(sin4);
	sin4.sin_family = AF_INET;
	sin4.sin_port = ntohs(80);
	sin4.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
	i = sockaddr_snprintf(buf, sizeof(buf), "%f %l %p %a",
		(struct sockaddr *)&sin4);

	ATF_REQUIRE_EQ_MSG(i, 17, "bad length for sin4");
	ATF_REQUIRE_STREQ(buf, "2 16 80 127.0.0.1");
}

ATF_TC(sockaddr_snprintf_in6);
ATF_TC_HEAD(sockaddr_snprintf_in6, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks sockaddr_snprintf(3) with sockaddr_in6");
}
ATF_TC_BODY(sockaddr_snprintf_in6, tc)
{
#ifdef INET6
	char buf[1024];
	struct sockaddr_in6 sin6;
	int i;

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_port = ntohs(80);
	sin6.sin6_addr = in6addr_nodelocal_allnodes;
	i = sockaddr_snprintf(buf, sizeof(buf), "%f %l %p %a",
		(struct sockaddr *)&sin6);

	ATF_REQUIRE_EQ_MSG(i, 16, "bad length for sin6");
	ATF_REQUIRE_STREQ(buf, "24 28 80 ff01::1");
#else
	atf_tc_skip("Tests built with USE_INET6=no");
#endif /* INET6 */
}

ATF_TC(sockaddr_snprintf_un);
ATF_TC_HEAD(sockaddr_snprintf_un, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks sockaddr_snprintf(3) with sockaddr_un");
}
ATF_TC_BODY(sockaddr_snprintf_un, tc)
{
	char buf[1024];
	struct sockaddr_un sun;
	int i;

	memset(&sun, 0, sizeof(sun));
	sun.sun_len = sizeof(sun);
	sun.sun_family = AF_UNIX;
	strncpy(sun.sun_path, "/tmp/sock", sizeof(sun.sun_path));
	i = sockaddr_snprintf(buf, sizeof(buf), "%f %l %a",
		(struct sockaddr *)&sun);

	ATF_REQUIRE_EQ_MSG(i, 15, "bad length for sun");
	ATF_REQUIRE_STREQ(buf, "1 106 /tmp/sock");
}

ATF_TC(sockaddr_snprintf_at);
ATF_TC_HEAD(sockaddr_snprintf_at, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks sockaddr_snprintf(3) with sockaddr_at");
}
ATF_TC_BODY(sockaddr_snprintf_at, tc)
{
	char buf[1024];
	struct sockaddr_at sat;
	int i;

	memset(&sat, 0, sizeof(sat));
	sat.sat_len = sizeof(sat);
	sat.sat_family = AF_APPLETALK;
	sat.sat_addr.s_net = ntohs(101);
	sat.sat_addr.s_node = 3;
	i = sockaddr_snprintf(buf, sizeof(buf), "%f %l %a",
		(struct sockaddr *)&sat);

	ATF_REQUIRE_EQ_MSG(i, 11, "bad length for sat");
	ATF_REQUIRE_STREQ(buf, "16 16 101.3");
}

ATF_TC(sockaddr_snprintf_dl);
ATF_TC_HEAD(sockaddr_snprintf_dl, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks sockaddr_snprintf(3) with sockaddr_dl");
}
ATF_TC_BODY(sockaddr_snprintf_dl, tc)
{
	char buf[1024];
	struct sockaddr_dl sdl;
	int i;

	memset(&sdl, 0, sizeof(sdl));
	sdl.sdl_len = sizeof(sdl);
	sdl.sdl_family = AF_LINK;
	sdl.sdl_index = 0;
	sdl.sdl_type = 0;
	sdl.sdl_nlen = 0;
	sdl.sdl_alen = 6;
	sdl.sdl_slen = 0;
	memcpy(sdl.sdl_data, "\01\02\03\04\05\06", 6);
	i = sockaddr_snprintf(buf, sizeof(buf), "%f %l %a",
		(struct sockaddr *)&sdl);

	ATF_REQUIRE_EQ_MSG(i, 17, "bad length for sdl");
	ATF_REQUIRE_STREQ(buf, "18 20 1.2.3.4.5.6");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sockaddr_snprintf_in);
	ATF_TP_ADD_TC(tp, sockaddr_snprintf_in6);
	ATF_TP_ADD_TC(tp, sockaddr_snprintf_un);
	ATF_TP_ADD_TC(tp, sockaddr_snprintf_at);
	ATF_TP_ADD_TC(tp, sockaddr_snprintf_dl);

	return atf_no_error();
}
