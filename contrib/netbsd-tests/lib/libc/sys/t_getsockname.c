/*	$NetBSD: t_getsockname.c,v 1.1 2016/07/30 11:03:54 njoly Exp $	*/
/*
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>
#include <sys/un.h>

#include <string.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC(getsockname_unix);

ATF_TC_HEAD(getsockname_unix, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks getsockname with UNIX domain");
}

ATF_TC_BODY(getsockname_unix, tc)
{
	const char *path = "sock.unix";
	int sd;
	socklen_t len;
	struct sockaddr_un sun;

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	ATF_REQUIRE(sd != -1);

	len = sizeof(sun);
	memset(&sun, 0, sizeof(sun));
	ATF_REQUIRE(getsockname(sd, (struct sockaddr *)&sun, &len) != -1);
	ATF_CHECK(sun.sun_family == AF_UNIX);
	ATF_CHECK(strcmp(sun.sun_path, "") == 0);

	len = sizeof(sun);
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strcpy(sun.sun_path, path);
	ATF_REQUIRE(bind(sd, (struct sockaddr *)&sun, len) != -1);

	len = sizeof(sun);
	memset(&sun, 0, sizeof(sun));
	ATF_REQUIRE(getsockname(sd, (struct sockaddr *)&sun, &len) != -1);
	ATF_CHECK(sun.sun_family == AF_UNIX);
	ATF_CHECK(strcmp(sun.sun_path, path) == 0);

	ATF_REQUIRE(close(sd) != -1);
	ATF_REQUIRE(unlink(path) != -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getsockname_unix);

	return atf_no_error();
}
