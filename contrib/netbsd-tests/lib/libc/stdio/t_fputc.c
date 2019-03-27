/*	$NetBSD: t_fputc.c,v 1.1 2011/09/11 09:02:46 jruoho Exp $ */

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
__RCSID("$NetBSD: t_fputc.c,v 1.1 2011/09/11 09:02:46 jruoho Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char	*path = "fputc";
static void		 puterr(int (*)(int, FILE *));
static void		 putstr(int (*)(int, FILE *));

static void
puterr(int (*func)(int, FILE *))
{
	FILE *f;

	f = fopen(path, "w+");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fclose(f) == 0);
	ATF_REQUIRE(unlink(path) == 0);
	ATF_REQUIRE(func('x', f) == EOF);
}

static void
putstr(int (*func)(int, FILE *))
{
	const char *str = "1234567890x";
	unsigned short i = 0;
	char buf[10];
	FILE *f;

	(void)memset(buf, 'x', sizeof(buf));

	f = fopen(path, "w+");
	ATF_REQUIRE(f != NULL);

	while (str[i] != 'x') {
		ATF_REQUIRE(func(str[i], f) == str[i]);
		i++;
	}

	ATF_REQUIRE(fclose(f) == 0);

	f = fopen(path, "r");
	ATF_REQUIRE(f != NULL);

	ATF_REQUIRE(fread(buf, 1, 10, f) == 10);
	ATF_REQUIRE(strncmp(buf, str, 10) == 0);

	ATF_REQUIRE(fclose(f) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_WITH_CLEANUP(fputc_basic);
ATF_TC_HEAD(fputc_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of fputc(3)");
}

ATF_TC_BODY(fputc_basic, tc)
{
	putstr(fputc);
}

ATF_TC_CLEANUP(fputc_basic, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fputc_err);
ATF_TC_HEAD(fputc_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from fputc(3)");
}

ATF_TC_BODY(fputc_err, tc)
{
	puterr(fputc);
}

ATF_TC_CLEANUP(fputc_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(putc_basic);
ATF_TC_HEAD(putc_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of putc(3)");
}

ATF_TC_BODY(putc_basic, tc)
{
	putstr(putc);
}

ATF_TC_CLEANUP(putc_basic, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(putc_err);
ATF_TC_HEAD(putc_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from putc(3)");
}

ATF_TC_BODY(putc_err, tc)
{
	puterr(putc);
}

ATF_TC_CLEANUP(putc_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(putc_unlocked_basic);
ATF_TC_HEAD(putc_unlocked_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of putc_unlocked(3)");
}

ATF_TC_BODY(putc_unlocked_basic, tc)
{
	putstr(putc_unlocked);
}

ATF_TC_CLEANUP(putc_unlocked_basic, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(putc_unlocked_err);
ATF_TC_HEAD(putc_unlocked_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from putc_unlocked(3)");
}

ATF_TC_BODY(putc_unlocked_err, tc)
{
	puterr(putc_unlocked);
}

ATF_TC_CLEANUP(putc_unlocked_err, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fputc_basic);
	ATF_TP_ADD_TC(tp, fputc_err);
	ATF_TP_ADD_TC(tp, putc_basic);
	ATF_TP_ADD_TC(tp, putc_err);
	ATF_TP_ADD_TC(tp, putc_unlocked_basic);
	ATF_TP_ADD_TC(tp, putc_unlocked_err);

	return atf_no_error();
}
