/* $NetBSD: t_sethostname.c,v 1.3 2012/03/25 08:17:54 joerg Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_sethostname.c,v 1.3 2012/03/25 08:17:54 joerg Exp $");

#include <sys/param.h>

#include <atf-c.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static char host[MAXHOSTNAMELEN];

static const char hosts[][MAXHOSTNAMELEN] = {
	"1234567890",
	"abcdefghijklmnopqrst",
	"!#\xa4%&/(..xasS812=!=!(I(!;X;;X.as.dasa=?;,..<>|**^\xa8",
	"--------------------------------------------------------------------"
};

ATF_TC_WITH_CLEANUP(sethostname_basic);
ATF_TC_HEAD(sethostname_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of sethostname(3)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sethostname_basic, tc)
{
	char name[MAXHOSTNAMELEN];
	size_t i;

	atf_tc_skip("screws up the test host's hostname on FreeBSD");

	for (i = 0; i < __arraycount(hosts); i++) {

		(void)memset(name, 0, sizeof(name));

#ifdef __FreeBSD__
		/*
		 * Sanity checks to ensure that the wrong invariant isn't being
		 * tested for per PR # 181127
		 */
		ATF_REQUIRE_EQ(sizeof(hosts[i]), MAXHOSTNAMELEN);
		ATF_REQUIRE_EQ(sizeof(name), MAXHOSTNAMELEN);

		ATF_REQUIRE(sethostname(hosts[i], sizeof(hosts[i]) - 1) == 0);
		ATF_REQUIRE(gethostname(name, sizeof(name) - 1) == 0);
#else
		ATF_REQUIRE(sethostname(hosts[i], sizeof(hosts[i])) == 0);
		ATF_REQUIRE(gethostname(name, sizeof(name)) == 0);
#endif
		ATF_REQUIRE(strcmp(hosts[i], name) == 0);
	}

	(void)sethostname(host, sizeof(host));
}

ATF_TC_CLEANUP(sethostname_basic, tc)
{
	(void)sethostname(host, sizeof(host));
}

ATF_TC_WITH_CLEANUP(sethostname_limit);
ATF_TC_HEAD(sethostname_limit, tc)
{
	atf_tc_set_md_var(tc, "descr", "Too long host name errors out?");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sethostname_limit, tc)
{
	char name[MAXHOSTNAMELEN + 1];

	(void)memset(name, 0, sizeof(name));

	ATF_REQUIRE(sethostname(name, sizeof(name)) == -1);
}

ATF_TC_CLEANUP(sethostname_limit, tc)
{
#ifdef __FreeBSD__
	ATF_REQUIRE(sethostname(host, MAXHOSTNAMELEN - 1 ) == 0);
	ATF_REQUIRE(sethostname(host, MAXHOSTNAMELEN) == -1);
#endif
	(void)sethostname(host, sizeof(host));
}

ATF_TC_WITH_CLEANUP(sethostname_perm);
ATF_TC_HEAD(sethostname_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Can normal user set the host name?");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(sethostname_perm, tc)
{

	errno = 0;

	ATF_REQUIRE_ERRNO(EPERM, sethostname(host, sizeof(host)) == -1);
}

ATF_TC_CLEANUP(sethostname_perm, tc)
{
	(void)sethostname(host, sizeof(host));
}

ATF_TP_ADD_TCS(tp)
{

	(void)memset(host, 0, sizeof(host));

	ATF_REQUIRE(gethostname(host, sizeof(host)) == 0);

	ATF_TP_ADD_TC(tp, sethostname_basic);
	ATF_TP_ADD_TC(tp, sethostname_limit);
	ATF_TP_ADD_TC(tp, sethostname_perm);

	return atf_no_error();
}
