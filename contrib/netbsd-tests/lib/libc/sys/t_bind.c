/*	$NetBSD: t_bind.c,v 1.3 2015/04/05 23:28:10 rtr Exp $	*/
/*
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <atf-c.h>

ATF_TC(bind_foreign_family);

ATF_TC_HEAD(bind_foreign_family, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that binding a socket "
	    "with a different address family fails");
}

ATF_TC_BODY(bind_foreign_family, tc)
{
	struct sockaddr_in addr;

	/* addr.sin_family = AF_UNSPEC = 0 */
	memset(&addr, 0, sizeof(addr));

	/*
	 * it is not necessary to initialize sin_{addr,port} since
	 * those structure members shall not be accessed if bind
	 * fails correctly.
	 */

	int sock = socket(AF_LOCAL, SOCK_STREAM, 0);
	ATF_REQUIRE(sock != -1);

	/* should fail but currently doesn't */
	ATF_REQUIRE(-1 == bind(sock, (struct sockaddr *)&addr, sizeof(addr)));
	ATF_REQUIRE(EAFNOSUPPORT == errno);

	close(sock);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bind_foreign_family);

	return atf_no_error();
}
