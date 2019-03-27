/* $NetBSD: t_memset.c,v 1.4 2015/09/11 09:25:52 martin Exp $ */

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
__RCSID("$NetBSD: t_memset.c,v 1.4 2015/09/11 09:25:52 martin Exp $");

#include <sys/stat.h>

#include <atf-c.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long	page = 0;
static void	fill(char *, size_t, char);
static bool	check(char *, size_t, char);

int zero;	/* always zero, but the compiler does not know */

ATF_TC(memset_array);
ATF_TC_HEAD(memset_array, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test memset(3) with arrays");
}

ATF_TC_BODY(memset_array, tc)
{
	char buf[1024];

	(void)memset(buf, 0, sizeof(buf));

	if (check(buf, sizeof(buf), 0) != true)
		atf_tc_fail("memset(3) did not fill a static buffer");

	(void)memset(buf, 'x', sizeof(buf));

	if (check(buf, sizeof(buf), 'x') != true)
		atf_tc_fail("memset(3) did not fill a static buffer");
}

ATF_TC(memset_return);
ATF_TC_HEAD(memset_return, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test memset(3) return value");
}

ATF_TC_BODY(memset_return, tc)
{
	char *b = (char *)0x1;
	char c[2];
	ATF_REQUIRE_EQ(memset(b, 0, 0), b);
	ATF_REQUIRE_EQ(memset(c, 2, sizeof(c)), c);
}

ATF_TC(memset_basic);
ATF_TC_HEAD(memset_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of memset(3)");
}

ATF_TC_BODY(memset_basic, tc)
{
	char *buf, *ret;

	buf = malloc(page);
	ret = malloc(page);

	ATF_REQUIRE(buf != NULL);
	ATF_REQUIRE(ret != NULL);

	fill(ret, page, 0);
	memset(buf, 0, page);

	ATF_REQUIRE(memcmp(ret, buf, page) == 0);

	fill(ret, page, 'x');
	memset(buf, 'x', page);

	ATF_REQUIRE(memcmp(ret, buf, page) == 0);

	free(buf);
	free(ret);
}

ATF_TC(memset_nonzero);
ATF_TC_HEAD(memset_nonzero, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test memset(3) with non-zero params");
}

ATF_TC_BODY(memset_nonzero, tc)
{
	const size_t n = 0x7f;
	char *buf;
	size_t i;

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);

	for (i = 0x21; i < n; i++) {

		(void)memset(buf, i, page);

		if (check(buf, page, i) != true)
			atf_tc_fail("memset(3) did not fill properly");
	}

	free(buf);
}

ATF_TC(memset_zero_size);

ATF_TC_HEAD(memset_zero_size, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test memset(3) with zero size");
}

ATF_TC_BODY(memset_zero_size, tc)
{
	char buf[1024];

	(void)memset(buf, 'x', sizeof(buf));

	if (check(buf, sizeof(buf), 'x') != true)
		atf_tc_fail("memset(3) did not fill a static buffer");

	(void)memset(buf+sizeof(buf)/2, 0, zero);

	if (check(buf, sizeof(buf), 'x') != true)
		atf_tc_fail("memset(3) with 0 size did change the buffer");
}

ATF_TC(bzero_zero_size);

ATF_TC_HEAD(bzero_zero_size, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test bzero(3) with zero size");
}

ATF_TC_BODY(bzero_zero_size, tc)
{
	char buf[1024];

	(void)memset(buf, 'x', sizeof(buf));

	if (check(buf, sizeof(buf), 'x') != true)
		atf_tc_fail("memset(3) did not fill a static buffer");

	(void)bzero(buf+sizeof(buf)/2, zero);

	if (check(buf, sizeof(buf), 'x') != true)
		atf_tc_fail("bzero(3) with 0 size did change the buffer");
}

ATF_TC(memset_struct);
ATF_TC_HEAD(memset_struct, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test memset(3) with a structure");
}

ATF_TC_BODY(memset_struct, tc)
{
	struct stat st;

	st.st_dev = 0;
	st.st_ino = 1;
	st.st_mode = 2;
	st.st_nlink = 3;
	st.st_uid = 4;
	st.st_gid = 5;
	st.st_rdev = 6;
	st.st_size = 7;
	st.st_atime = 8;
	st.st_mtime = 9;

	(void)memset(&st, 0, sizeof(struct stat));

	ATF_CHECK(st.st_dev == 0);
	ATF_CHECK(st.st_ino == 0);
	ATF_CHECK(st.st_mode == 0);
	ATF_CHECK(st.st_nlink == 0);
	ATF_CHECK(st.st_uid == 0);
	ATF_CHECK(st.st_gid == 0);
	ATF_CHECK(st.st_rdev == 0);
	ATF_CHECK(st.st_size == 0);
	ATF_CHECK(st.st_atime == 0);
	ATF_CHECK(st.st_mtime == 0);
}

static void
fill(char *buf, size_t len, char x)
{
	size_t i;

	for (i = 0; i < len; i++)
		buf[i] = x;
}

static bool
check(char *buf, size_t len, char x)
{
	size_t i;

	for (i = 0; i < len; i++) {

		if (buf[i] != x)
			return false;
	}

	return true;
}

ATF_TP_ADD_TCS(tp)
{

	page = sysconf(_SC_PAGESIZE);
	ATF_REQUIRE(page >= 0);

	ATF_TP_ADD_TC(tp, memset_array);
	ATF_TP_ADD_TC(tp, memset_basic);
	ATF_TP_ADD_TC(tp, memset_nonzero);
	ATF_TP_ADD_TC(tp, memset_struct);
	ATF_TP_ADD_TC(tp, memset_return);
	ATF_TP_ADD_TC(tp, memset_zero_size);
	ATF_TP_ADD_TC(tp, bzero_zero_size);

	return atf_no_error();
}
