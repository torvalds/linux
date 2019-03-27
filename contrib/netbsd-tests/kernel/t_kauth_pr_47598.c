/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2013\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_kauth_pr_47598.c,v 1.3 2014/04/28 08:34:16 martin Exp $");

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atf-c.h>

/*
 * helper function
 */
static const char curtain_name[] = "security.models.bsd44.curtain";
static const char securelevel_name[] = "security.models.bsd44.securelevel";

static bool may_lower_curtain(void);
static int get_curtain(void);
static void set_curtain(int newval);

static bool
may_lower_curtain(void)
{
	int seclevel;
	size_t len = sizeof(seclevel);

	if (sysctlbyname(securelevel_name, &seclevel, &len, NULL, 0) != 0)
		atf_tc_fail("failed to read %s", securelevel_name);

	return seclevel <= 0;
}

static int
get_curtain(void)
{
	int curtain;
	size_t len = sizeof(curtain);

	if (sysctlbyname(curtain_name, &curtain, &len, NULL, 0) != 0)
		atf_tc_fail("failed to read %s", curtain_name);

	return curtain;
}

static void
set_curtain(int newval)
{

	if (sysctlbyname(curtain_name, NULL, 0, &newval, sizeof(newval)) != 0)
		atf_tc_fail("failed to set %s to %d", curtain_name, newval);
}

/*
 * PR kern/47598: if security.models.extensions.curtain = 1 we crash when
 * doing a netstat while an embryonic (not yet fully accepted) connection
 * exists.
 * This has been fixed with rev. 1.5 of
 *   src/sys/secmodel/extensions/secmodel_extensions.c.
 */


ATF_TC(kauth_curtain);
ATF_TC_HEAD(kauth_curtain, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
	atf_tc_set_md_var(tc, "require.progs", "netstat");
	atf_tc_set_md_var(tc, "descr",
	    "Checks for kernel crash with curtain active (PR kern/47598)");
}

ATF_TC_BODY(kauth_curtain, tc)
{

	int old_curtain, s, s2, err;
	socklen_t slen;
	struct sockaddr_in sa;

	/*
	 * save old value of "curtain" and enable it
	 */
	old_curtain = get_curtain();
	if (old_curtain < 1 && !may_lower_curtain())
		atf_tc_skip("curtain is not enabled and we would not be able"
		    " to drop it later due to securelevel settings");

	set_curtain(1);

	/*
	 * create a socket and bind it to some arbitray free port
	 */
	s = socket(PF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
	ATF_REQUIRE(s != -1);
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_len = sizeof(sa);
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");
	ATF_REQUIRE(bind(s, (struct sockaddr *)&sa, sizeof(sa))==0);
	ATF_REQUIRE(listen(s, 16)==0);

	/*
	 * extract address and open a connection to the port
	 */
	slen = sizeof(sa);
	ATF_REQUIRE(getsockname(s, (struct sockaddr *)&sa, &slen)==0);
	s2 = socket(PF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
	ATF_REQUIRE(s2 != -1);
	printf("port is %d\n", ntohs(sa.sin_port));
	err = connect(s2, (struct sockaddr *)&sa, sizeof(sa));
	ATF_REQUIRE_MSG(err == -1 && errno == EINPROGRESS,
	    "conect returned %d with errno %d", err, errno);
	fflush(stdout);
	fflush(stderr);

	/*
	 * we now have a pending, not yet accepted connection - run netstat
	 */
	system("netstat -aA");

	/*
	 * cleanup
	 */
	close(s2);
	close(s);

	/*
	 * restore old value of curtain
	 */
	set_curtain(old_curtain);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, kauth_curtain);

	return atf_no_error();
}


