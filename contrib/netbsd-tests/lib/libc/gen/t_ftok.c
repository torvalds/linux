/*	$NetBSD: t_ftok.c,v 1.2 2017/01/10 15:19:52 christos Exp $ */

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
__RCSID("$NetBSD: t_ftok.c,v 1.2 2017/01/10 15:19:52 christos Exp $");

#include <sys/types.h>
#include <sys/ipc.h>

#include <atf-c.h>
#include <fcntl.h>
#include <unistd.h>

static const char *path = "ftok";
static const char *hlnk = "hlnk";
static const char *slnk = "slnk";
static const int key = 123456789;

ATF_TC(ftok_err);
ATF_TC_HEAD(ftok_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from ftok(3)");
}

ATF_TC_BODY(ftok_err, tc)
{
	ATF_REQUIRE(ftok("/a/b/c/d/e/f/g/h/i", key) == -1);
}

ATF_TC_WITH_CLEANUP(ftok_link);
ATF_TC_HEAD(ftok_link, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that links return the same key");
}

ATF_TC_BODY(ftok_link, tc)
{
	key_t k1, k2, k3;
	int fd;

	fd = open(path, O_RDONLY | O_CREAT);

	ATF_REQUIRE(fd >= 0);
	(void)close(fd);
	ATF_REQUIRE(link(path, hlnk) == 0);
	ATF_REQUIRE(symlink(path, slnk) == 0);

	k1 = ftok(path, key);
	k2 = ftok(hlnk, key);
	k3 = ftok(slnk, key);

	ATF_REQUIRE(k1 != -1);
	ATF_REQUIRE(k2 != -1);
	ATF_REQUIRE(k3 != -1);

	if (k1 != k2)
		atf_tc_fail("ftok(3) gave different key for a hard link");

	if (k1 != k3)
		atf_tc_fail("ftok(3) gave different key for a symbolic link");

	ATF_REQUIRE(unlink(path) == 0);
	ATF_REQUIRE(unlink(hlnk) == 0);
	ATF_REQUIRE(unlink(slnk) == 0);
}

ATF_TC_CLEANUP(ftok_link, tc)
{
	(void)unlink(path);
	(void)unlink(hlnk);
	(void)unlink(slnk);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ftok_err);
	ATF_TP_ADD_TC(tp, ftok_link);

	return atf_no_error();
}
