/* $NetBSD: t_extent.c,v 1.5 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_extent.c,v 1.5 2017/01/13 21:30:41 christos Exp $");

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/extent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "h_macros.h"

static int ret;
static struct extent *ex;

#define h_create(name, start, end, flags) \
	ATF_REQUIRE((ex = extent_create(name, \
	    start, end, 0, 0, flags)) != NULL);

#define h_alloc_region(start, size) \
	ATF_REQUIRE_EQ_MSG(ret = extent_alloc_region(ex, \
	    start, size, 0), 0, "%s", strerror(ret));

#define h_free(start, size) \
	ATF_REQUIRE_EQ_MSG(ret = extent_free(ex, \
	    start, size, 0), 0, "%s", strerror(ret));

static void
h_alloc_subregion(u_long substart, u_long subend, u_long size,
    u_long alignment, u_long boundary, int expret, u_long expres)
{
	u_long result;

#define FAIL(fmt, ...) \
	atf_tc_fail("extent_alloc_subregion1(ex, %#lx, %#lx, %#lx, %#lx, 0, " \
	    "%#lx, 0, &result): " fmt, substart, subend, size, alignment, \
	    boundary, ##__VA_ARGS__)

	ret = extent_alloc_subregion1(ex, substart, subend, size,
	    alignment, 0, boundary, 0, &result);

	if (ret != expret)
		FAIL("%s", strerror(errno));

	if (expret == 0 && result != expres)
		FAIL("result should be: %#lx, got: %#lx", expres, result);
#undef FAIL
}

static void
h_require(const char *name, u_long start,
	u_long end, int flags, const char *exp)
{
	char buf[4096];
	struct extent_region *rp;
	int n = 0;

	ATF_REQUIRE_STREQ_MSG(ex->ex_name, name,
	    "expected: \"%s\", got: \"%s\"", name, ex->ex_name);
	ATF_REQUIRE_EQ_MSG(ex->ex_start, start,
	    "expected: %#lx, got: %#lx", start, ex->ex_start);
	ATF_REQUIRE_EQ_MSG(ex->ex_end, end,
	    "expected: %#lx, got: %#lx", end, ex->ex_end);
	ATF_REQUIRE_EQ_MSG(ex->ex_flags, flags,
	    "expected: %#x, got: %#x", flags, ex->ex_flags);

	(void)memset(buf, 0, sizeof(buf));
	LIST_FOREACH(rp, &ex->ex_regions, er_link)
		n += snprintf(buf + n, sizeof(buf) - n,
		    "0x%lx - 0x%lx\n", rp->er_start, rp->er_end);

	if (strcmp(buf, exp) == 0)
		return;

	printf("Incorrect extent map\n");
	printf("Expected:\n%s\n", exp);
	printf("Got:\n%s\n", buf);
	atf_tc_fail("incorrect extent map");
}

ATF_TC(coalesce);
ATF_TC_HEAD(coalesce, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks coalescing of regions");
}
ATF_TC_BODY(coalesce, tc)
{
	h_create("test1", 0, 0x4f, 0);

	h_alloc_region(0x00, 0x10);
	h_alloc_region(0x20, 0x10);
	h_alloc_region(0x40, 0x10);
	h_alloc_region(0x10, 0x10);
	h_alloc_subregion(0, 0x4f, 0x10, EX_NOALIGN, EX_NOBOUNDARY, 0, 0x30);

	h_require("test1", 0x00, 0x4f, 0x00,
	    "0x0 - 0x4f\n");

	extent_destroy(ex);
}

ATF_TC(subregion1);
ATF_TC_HEAD(subregion1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks that subregions work (PR kern/7539)");
}
ATF_TC_BODY(subregion1, tc)
{
	h_create("test2", 0, 0x2f, EX_NOCOALESCE);

	h_alloc_region(0x00, 0x10);
	h_alloc_subregion(0x20, 0x30, 0x10, EX_NOALIGN, EX_NOBOUNDARY, 0, 0x20);

	h_require("test2", 0x00, 0x2f, 0x2,
	    "0x0 - 0xf\n"
	    "0x20 - 0x2f\n");

	extent_destroy(ex);
}

ATF_TC(subregion2);
ATF_TC_HEAD(subregion2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks that subregion allocations don't overlap with existing "
	    "ones (fixed in 1.25)");
}
ATF_TC_BODY(subregion2, tc)
{
	h_create("test3", 0, 0x3f, EX_NOCOALESCE);

	h_alloc_region(0x00, 0x20);
	h_alloc_region(0x30, 0x10);
	h_alloc_subregion(0x10, 0x3f, 0x10,
	    EX_NOALIGN, EX_NOBOUNDARY, 0, 0x20);

	h_require("test3", 0x00, 0x3f, 0x2,
	    "0x0 - 0x1f\n"
	    "0x20 - 0x2f\n"
	    "0x30 - 0x3f\n");

	extent_destroy(ex);
}

ATF_TC(bound1);
ATF_TC_HEAD(bound1, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks for overflow in boundary check, before an allocated region "
	    "(fixed in 1.32)");
}
ATF_TC_BODY(bound1, tc)
{
	h_create("test4", 0xf0000000, 0xffffffff, 0);

	h_alloc_region(0xf1000000, 0x1);
	h_alloc_subregion(0xf0000000, 0xffffffff, 0x1,
	    EX_NOALIGN, 0x20000000, 0, 0xf0000000);

	h_require("test4", 0xf0000000, 0xffffffff, 0x0,
	    "0xf0000000 - 0xf0000000\n"
	    "0xf1000000 - 0xf1000000\n");

	extent_destroy(ex);
}

ATF_TC(bound2);
ATF_TC_HEAD(bound2, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks for overflow in boundary checks, before the subregion end "
	    "(fixed in 1.32)");
}
ATF_TC_BODY(bound2, tc)
{
	h_create("test5", 0xf0000000, 0xffffffff, 0);

	h_alloc_subregion(0xf0000000, 0xffffffff, 0x1,
	    EX_NOALIGN, 0x20000000, 0, 0xf0000000);

	h_require("test5", 0xf0000000, 0xffffffff, 0x0,
	    "0xf0000000 - 0xf0000000\n");

	extent_destroy(ex);
}

ATF_TC(bound3);
ATF_TC_HEAD(bound3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks allocation beyond last boundary line: last two "
	    "allocations should succeed without boundary \"fixups\"");
}
ATF_TC_BODY(bound3, tc)
{
	h_create("test6", 0, 11, 0);

	h_alloc_subregion(0, 11, 8, EX_NOALIGN, 8, 0, 0);
	h_alloc_subregion(0, 11, 2, EX_NOALIGN, 8, 0, 0x8);
	h_alloc_subregion(0, 11, 2, EX_NOALIGN, 8, 0, 0xa);

	h_require("test6", 0x0, 0xb, 0x0, "0x0 - 0xb\n");

	extent_destroy(ex);
}

ATF_TC(bound4);
ATF_TC_HEAD(bound4, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks allocation beyond last boundary line: last allocation "
	    "should be bumped to the next boundary and exactly fit the "
	    "remaining space");
}
ATF_TC_BODY(bound4, tc)
{
	h_create("test7", 0, 11, 0);

	h_alloc_subregion(0, 11, 7, EX_NOALIGN, 8, 0, 0);
	h_alloc_subregion(0, 11, 4, EX_NOALIGN, 8, 0, 8);

	h_require("test7", 0x0, 0xb, 0x0,
	    "0x0 - 0x6\n"
	    "0x8 - 0xb\n");

	extent_destroy(ex);
}

ATF_TC(subregion3);
ATF_TC_HEAD(subregion3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks that we don't allocate a region pasts the end of "
	    "subregion (i.e., the second alloc_subregion should fail). "
	    "subr_extent.c prior to rev. 1.43 allocated region starting "
	    "from 0x10");
}
ATF_TC_BODY(subregion3, tc)
{
	h_create("test8", 0, 0x4f, EX_NOCOALESCE);

	h_alloc_region(0x30, 0x10);
	h_alloc_subregion(0, 0xf, 0x10, EX_NOALIGN, EX_NOBOUNDARY, 0, 0);
	h_alloc_subregion(0, 0xf, 0x10, EX_NOALIGN, EX_NOBOUNDARY, EAGAIN, 0);

	h_require("test8", 0x0, 0x4f, 0x2,
	    "0x0 - 0xf\n"
	    "0x30 - 0x3f\n");

	extent_destroy(ex);
}

ATF_TC(bound5);
ATF_TC_HEAD(bound5, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "When allocating a region with a boundary constraint, checks "
	    "proper detection of overflaps once the candidate region has "
	    "been aligned. subr_extent.c prior 1.45 could corrupt the extent "
	    "map in this situation");
}
ATF_TC_BODY(bound5, tc)
{
	h_create("test9", 0, 0x4f, 0);

	h_alloc_subregion(0, 0x10, 4, EX_NOALIGN, 0, 0, 0);
	h_alloc_subregion(0xd, 0x20, 2, EX_NOALIGN, 0, 0, 0xd);
	h_alloc_subregion(0, 0x4f, 8, EX_NOALIGN, 8, 0, 0x10);

	h_require("test9", 0x0, 0x4f, 0x0,
	    "0x0 - 0x3\n"
	    "0xd - 0xe\n"
	    "0x10 - 0x17\n");

	extent_destroy(ex);
}

ATF_TC(free);
ATF_TC_HEAD(free, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks extent_free()");
}
ATF_TC_BODY(free, tc)
{
	h_create("test10", 0xc0002000, 0xffffe000, EX_BOUNDZERO);

	h_alloc_subregion(0xc0002000, 0xffffe000, 0x2000,
	    0x10000, 0x10000, 0, 0xc0010000);
	h_alloc_subregion(0xc0002000, 0xffffe000, 0x2000,
	    0x10000, 0x10000, 0, 0xc0020000);

	h_require("test10", 0xc0002000, 0xffffe000, 0x0,
	    "0xc0010000 - 0xc0011fff\n"
	    "0xc0020000 - 0xc0021fff\n");

	h_free(0xc0020000, 0x2000);
	h_require("test10", 0xc0002000, 0xffffe000, 0x0,
	    "0xc0010000 - 0xc0011fff\n");

	h_alloc_subregion(0xc0002000, 0xffffe000, 0x10000,
	    0x10000, 0x10000, 0, 0xc0022000);

	h_require("test10", 0xc0002000, 0xffffe000, 0x0,
	    "0xc0010000 - 0xc0011fff\n"
	    "0xc0022000 - 0xc0031fff\n");

	extent_destroy(ex);
}

ATF_TC(subregion4);
ATF_TC_HEAD(subregion4, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks for off-by-one bug which would cause a region at the end "
	    "of the extent to be allocated multiple times (fixed in 1.51)");
}
ATF_TC_BODY(subregion4, tc)
{
	h_create("test11", 0x10, 0x20, EX_NOCOALESCE);

	h_alloc_subregion(0x10, 0x13, 0x4, EX_NOALIGN, EX_NOBOUNDARY, 0, 0x10);
	h_alloc_subregion(0x1e, 0x1f, 0x2, EX_NOALIGN, EX_NOBOUNDARY, 0, 0x1e);
	h_alloc_subregion(0x20, 0x20, 0x1, EX_NOALIGN, EX_NOBOUNDARY, 0, 0x20);
	h_alloc_subregion(0x20, 0x20, 0x1, EX_NOALIGN, EX_NOBOUNDARY, EAGAIN, 0);
	h_alloc_subregion(0x10, 0x20, 0x1, EX_NOALIGN, EX_NOBOUNDARY, 0, 0x14);

	h_require("test11", 0x10, 0x20, 0x2,
	    "0x10 - 0x13\n"
	    "0x14 - 0x14\n"
	    "0x1e - 0x1f\n"
	    "0x20 - 0x20\n");

	extent_destroy(ex);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, coalesce);
	ATF_TP_ADD_TC(tp, subregion1);
	ATF_TP_ADD_TC(tp, subregion2);
	ATF_TP_ADD_TC(tp, bound1);
	ATF_TP_ADD_TC(tp, bound2);
	ATF_TP_ADD_TC(tp, bound3);
	ATF_TP_ADD_TC(tp, bound4);
	ATF_TP_ADD_TC(tp, subregion3);
	ATF_TP_ADD_TC(tp, bound5);
	ATF_TP_ADD_TC(tp, free);
	ATF_TP_ADD_TC(tp, subregion4);

	return atf_no_error();
}
