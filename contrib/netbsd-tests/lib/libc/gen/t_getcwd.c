/*	$NetBSD: t_getcwd.c,v 1.3 2011/07/27 05:04:11 jruoho Exp $ */

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
__RCSID("$NetBSD: t_getcwd.c,v 1.3 2011/07/27 05:04:11 jruoho Exp $");

#include <sys/param.h>
#include <sys/stat.h>

#include <atf-c.h>
#include <errno.h>
#include <fts.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

ATF_TC(getcwd_err);
ATF_TC_HEAD(getcwd_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions in getcwd(3)");
}

ATF_TC_BODY(getcwd_err, tc)
{
	char buf[MAXPATHLEN];

	errno = 0;

	ATF_REQUIRE(getcwd(buf, 0) == NULL);
	ATF_REQUIRE(errno == EINVAL);

#ifdef __NetBSD__
	errno = 0;

	ATF_REQUIRE(getcwd((void *)-1, sizeof(buf)) == NULL);
	ATF_REQUIRE(errno == EFAULT);
#endif
}

ATF_TC(getcwd_fts);
ATF_TC_HEAD(getcwd_fts, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of getcwd(3)");
}

ATF_TC_BODY(getcwd_fts, tc)
{
	const char *str = NULL;
	char buf[MAXPATHLEN];
	char *argv[2];
	FTSENT *ftse;
	FTS *fts;
	int ops;
	short depth;

	/*
	 * Do not traverse too deep; cf. PR bin/45180.
	 */
	depth = 2;

	argv[1] = NULL;
	argv[0] = __UNCONST("/");

	/*
	 * Test that getcwd(3) works with basic
	 * system directories. Note that having
	 * no FTS_NOCHDIR specified should ensure
	 * that the current directory is visited.
	 */
	ops = FTS_PHYSICAL | FTS_NOSTAT;
	fts = fts_open(argv, ops, NULL);

	if (fts == NULL) {
		str = "failed to initialize fts(3)";
		goto out;
	}

	while ((ftse = fts_read(fts)) != NULL) {

		if (ftse->fts_level < 1)
			continue;

		if (ftse->fts_level > depth) {
			(void)fts_set(fts, ftse, FTS_SKIP);
			continue;
		}

		switch(ftse->fts_info) {

		case FTS_DP:

			(void)memset(buf, 0, sizeof(buf));

			if (getcwd(buf, sizeof(buf)) == NULL) {
				str = "getcwd(3) failed";
				goto out;
			}

			if (strstr(ftse->fts_path, buf) == NULL) {
				str = "getcwd(3) returned incorrect path";
				goto out;
			}

			break;

		default:
			break;
		}
	}

out:
	if (fts != NULL)
		(void)fts_close(fts);

	if (str != NULL)
		atf_tc_fail("%s", str);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getcwd_err);
	ATF_TP_ADD_TC(tp, getcwd_fts);

	return atf_no_error();
}
