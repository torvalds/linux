/* $NetBSD: t_clearerr.c,v 1.1 2011/05/01 16:36:37 jruoho Exp $ */

/*
 * Copyright (c) 2009, Stathis Kamperis
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_clearerr.c,v 1.1 2011/05/01 16:36:37 jruoho Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>

static const char	path[] = "/etc/passwd";

ATF_TC(clearerr_basic);
ATF_TC_HEAD(clearerr_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of clearerr(3)");
}

ATF_TC_BODY(clearerr_basic, tc)
{
	char buf[2048];
	FILE *fp;

	fp = fopen(path, "r");
	ATF_REQUIRE(fp != NULL);

	while (feof(fp) == 0)
		(void)fread(buf, sizeof(buf), 1, fp);

	ATF_REQUIRE(feof(fp) != 0);

	errno = 0;
	clearerr(fp);

	ATF_REQUIRE(errno == 0);
	ATF_REQUIRE(feof(fp) == 0);
	ATF_REQUIRE(fclose(fp) != EOF);
}

ATF_TC(clearerr_err);
ATF_TC_HEAD(clearerr_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that clearerr(3) does not fail");
}

ATF_TC_BODY(clearerr_err, tc)
{
	FILE *fp;

	fp = fopen(path, "r");

	ATF_REQUIRE(fp != NULL);
	ATF_REQUIRE(fclose(fp) == 0);

	errno = 0;
	clearerr(fp);

	ATF_REQUIRE(errno == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, clearerr_basic);
	ATF_TP_ADD_TC(tp, clearerr_err);

	return atf_no_error();
}
