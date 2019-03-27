/*	$NetBSD: t_types.c,v 1.4 2012/03/18 07:14:08 jruoho Exp $ */

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
__RCSID("$NetBSD: t_types.c,v 1.4 2012/03/18 07:14:08 jruoho Exp $");

#include <sys/types.h>

#include <atf-c.h>
#include <limits.h>
#include <stdint.h>

#include <stdio.h>

ATF_TC(types_limits);
ATF_TC_HEAD(types_limits, tc)
{
	atf_tc_set_md_var(tc, "descr", "Known limits for types(3)");
}

ATF_TC_BODY(types_limits, tc)
{
	useconds_t usec;
	ssize_t size;

	/*
	 * IEEE Std 1003.1-2008:
	 *
	 * "The type ssize_t shall be capable of storing
	 *  values at least in the range [-1, {SSIZE_MAX}]."
	 *
	 */
	size = SSIZE_MAX;
	ATF_REQUIRE(size > 0);

	size = size + 1;
	ATF_REQUIRE(size < 0);

	/*
	 * IEEE Std 1003.1-2008:
	 *
	 * "The type suseconds_t shall be a signed integer type capable
	 *  of storing values at least in the range [-1, 1000000]."
	 */
	usec = 1000000;
	ATF_REQUIRE(usec > 0);
}

ATF_TC(types_signed);
ATF_TC_HEAD(types_signed, tc)
{
	atf_tc_set_md_var(tc, "descr", "Signed types(3)"
	    " (PR standards/44847)");
}

ATF_TC_BODY(types_signed, tc)
{
	blkcnt_t bc;
	blksize_t bs;
	ssize_t size;
	off_t off;
	pid_t pid;

	/*
	 * As noted in types(3), the following
	 * types should be signed integers.
	 */
	bc = 0;
	bs = 0;
	off = 0;
	pid = 0;
	size = 0;

	ATF_CHECK((bc - 1) <= 0);
	ATF_CHECK((bs - 1) <= 0);
	ATF_CHECK((off - 1) <= 0);
	ATF_CHECK((pid - 1) <= 0);
	ATF_CHECK((size - 1) <= 0);
}

ATF_TC(types_unsigned);
ATF_TC_HEAD(types_unsigned, tc)
{
	atf_tc_set_md_var(tc, "descr", "Unsigned types(3)"
		" (PR standards/18067)");
}

ATF_TC_BODY(types_unsigned, tc)
{
	fsblkcnt_t fb;
	fsfilcnt_t ff;
	size_t size;
	rlim_t lim;
	ino_t ino;

	fb = 0;
	ff = 0;
	ino = 0;
	lim = 0;
	size = 0;

	ATF_CHECK((fb - 1) > 0);
	ATF_CHECK((ff - 1) > 0);
	ATF_CHECK((ino - 1) > 0);
	ATF_CHECK((lim - 1) > 0);
	ATF_CHECK((size - 1) > 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, types_limits);
	ATF_TP_ADD_TC(tp, types_signed);
	ATF_TP_ADD_TC(tp, types_unsigned);

	return atf_no_error();
}
