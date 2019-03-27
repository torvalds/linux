/* $NetBSD: t_minherit.c,v 1.1 2014/07/18 12:34:52 christos Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas
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
__RCSID("$NetBSD: t_minherit.c,v 1.1 2014/07/18 12:34:52 christos Exp $");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

static long page;

static void *
makemap(int v, int f) {
	void *map = mmap(NULL, page, PROT_READ|PROT_WRITE,
	    MAP_SHARED|MAP_ANON, -1, 0);
	ATF_REQUIRE(map != MAP_FAILED);
	memset(map, v, page);
	if (f != 666)
		ATF_REQUIRE(minherit(map, page, f) == 0);
	else
		ATF_REQUIRE(minherit(map, page, f) == -1);
	return map;
}

ATF_TC(minherit_copy);
ATF_TC_HEAD(minherit_copy, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test for MAP_INHERIT_COPY from minherit(2)");
}

ATF_TC_BODY(minherit_copy, tc)
{
	void *map1 = makemap(1, MAP_INHERIT_COPY);
	void *map2 = makemap(1, MAP_INHERIT_COPY);
	switch (fork()) {
	default:
		ATF_REQUIRE(wait(NULL) != -1);
		ATF_REQUIRE(memcmp(map1, map2, page) == 0);
		break;
	case -1:
		ATF_REQUIRE(0);
		break;
	case 0:
		ATF_REQUIRE(memcmp(map1, map2, page) == 0);
		memset(map1, 0, page);
		exit(0);
	}
}

ATF_TC(minherit_share);
ATF_TC_HEAD(minherit_share, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test for MAP_INHERIT_SHARE from minherit(2)");
}

ATF_TC_BODY(minherit_share, tc)
{
	void *map1 = makemap(1, MAP_INHERIT_SHARE);
	void *map2 = makemap(1, MAP_INHERIT_SHARE);

	switch (fork()) {
	default:
		ATF_REQUIRE(wait(NULL) != -1);
		memset(map2, 0, page);
		ATF_REQUIRE(memcmp(map1, map2, page) == 0);
		break;
	case -1:
		ATF_REQUIRE(0);
		break;
	case 0:
		ATF_REQUIRE(memcmp(map1, map2, page) == 0);
		memset(map1, 0, page);
		exit(0);
	}
}

static void
segv(int n) {
	_exit(n);
}

ATF_TC(minherit_none);
ATF_TC_HEAD(minherit_none, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test for MAP_INHERIT_NONE from minherit(2)");
}

ATF_TC_BODY(minherit_none, tc)
{
	void *map1 = makemap(0, MAP_INHERIT_NONE);
	int status;

	switch (fork()) {
	default:
		ATF_REQUIRE(wait(&status) != -1);
		ATF_REQUIRE(WEXITSTATUS(status) == SIGSEGV);
		break;
	case -1:
		ATF_REQUIRE(0);
		break;
	case 0:
		ATF_REQUIRE(signal(SIGSEGV, segv) != SIG_ERR);
		memset(map1, 0, page);
		exit(0);
	}
}

ATF_TC(minherit_zero);
ATF_TC_HEAD(minherit_zero, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test for MAP_INHERIT_ZERO from minherit(2)");
}

ATF_TC_BODY(minherit_zero, tc)
{
	void *map1 = makemap(1, MAP_INHERIT_ZERO);
	void *map2 = makemap(0, MAP_INHERIT_SHARE);

	switch (fork()) {
	default:
		ATF_REQUIRE(wait(NULL) != -1);
		memset(map2, 1, page);
		ATF_REQUIRE(memcmp(map1, map2, page) == 0);
		break;
	case -1:
		ATF_REQUIRE(0);
		break;
	case 0:
		ATF_REQUIRE(memcmp(map1, map2, page) == 0);
		memset(map1, 2, page);
		exit(0);
	}
}

ATF_TC(minherit_bad);
ATF_TC_HEAD(minherit_bad, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test for bad minherit(2)");
}

ATF_TC_BODY(minherit_bad, tc)
{
	(void)makemap(0, 666);
}

ATF_TP_ADD_TCS(tp)
{
	page = sysconf(_SC_PAGESIZE);
	ATF_REQUIRE(page >= 0);

	ATF_TP_ADD_TC(tp, minherit_copy);
	ATF_TP_ADD_TC(tp, minherit_share);
	ATF_TP_ADD_TC(tp, minherit_none);
	ATF_TP_ADD_TC(tp, minherit_zero);
	ATF_TP_ADD_TC(tp, minherit_bad);

	return atf_no_error();
}
