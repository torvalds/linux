/*	$NetBSD: t_ttyname.c,v 1.4 2017/01/10 15:33:40 christos Exp $ */

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
__RCSID("$NetBSD: t_ttyname.c,v 1.4 2017/01/10 15:33:40 christos Exp $");

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long ttymax = 0;

ATF_TC(ttyname_err);
ATF_TC_HEAD(ttyname_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors in ttyname(3)");
}

ATF_TC_BODY(ttyname_err, tc)
{
	int fd;

	fd = open("XXX", O_RDONLY);

	if (fd < 0) {

		errno = 0;

		ATF_REQUIRE(isatty(fd) != -1);
		ATF_REQUIRE(errno == EBADF);

		errno = 0;

		ATF_REQUIRE(ttyname(fd) == NULL);
		ATF_REQUIRE(errno == EBADF);
	}

	fd = open("/etc/passwd", O_RDONLY);

	if (fd >= 0) {

		errno = 0;

		ATF_REQUIRE(isatty(fd) != -1);
		ATF_REQUIRE(errno == ENOTTY);

		errno = 0;

		ATF_REQUIRE(ttyname(fd) == NULL);
		ATF_REQUIRE(errno == ENOTTY);
		(void)close(fd);
	}
}

ATF_TC(ttyname_r_err);
ATF_TC_HEAD(ttyname_r_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors in ttyname_r(3)");
}

ATF_TC_BODY(ttyname_r_err, tc)
{
	char sbuf[0];
	char *buf;
	int fd;
	int rv;

	buf = malloc(ttymax + 1);

	if (buf == NULL)
		return;

	(void)memset(buf, '\0', ttymax + 1);

	if (isatty(STDIN_FILENO) != 0) {

		rv = ttyname_r(STDIN_FILENO, sbuf, sizeof(sbuf));
		ATF_REQUIRE(rv == ERANGE);
	}

	rv = ttyname_r(-1, buf, ttymax);
	ATF_REQUIRE(rv == EBADF);

	fd = open("/etc/passwd", O_RDONLY);

	if (fd >= 0) {
		rv = ttyname_r(fd, buf, ttymax);
		ATF_REQUIRE(rv == ENOTTY);
		ATF_REQUIRE(close(fd) == 0);
	}

	free(buf);
}

ATF_TC(ttyname_r_stdin);
ATF_TC_HEAD(ttyname_r_stdin, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ttyname_r(3) with stdin(3)");
}

ATF_TC_BODY(ttyname_r_stdin, tc)
{
	const char *str;
	char *buf;
	int rv;

	if (isatty(STDIN_FILENO) == 0)
		return;

	buf = malloc(ttymax + 1);

	if (buf == NULL)
		return;

	(void)memset(buf, '\0', ttymax + 1);

	str = ttyname(STDIN_FILENO);
	rv = ttyname_r(STDIN_FILENO, buf, ttymax);

	ATF_REQUIRE(rv == 0);
	ATF_REQUIRE(str != NULL);

	if (strcmp(str, buf) != 0)
		atf_tc_fail("ttyname(3) and ttyname_r(3) conflict");

	free(buf);
}

ATF_TC(ttyname_stdin);
ATF_TC_HEAD(ttyname_stdin, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ttyname(3) with stdin(3)");
}

ATF_TC_BODY(ttyname_stdin, tc)
{

	if (isatty(STDIN_FILENO) != 0)
		ATF_REQUIRE(ttyname(STDIN_FILENO) != NULL);

	(void)close(STDIN_FILENO);

	ATF_REQUIRE(isatty(STDIN_FILENO) != 1);
	ATF_REQUIRE(ttyname(STDIN_FILENO) == NULL);
}

ATF_TP_ADD_TCS(tp)
{

	ttymax = sysconf(_SC_TTY_NAME_MAX);
	ATF_REQUIRE(ttymax >= 0);

	ATF_TP_ADD_TC(tp, ttyname_err);
	ATF_TP_ADD_TC(tp, ttyname_r_err);
	ATF_TP_ADD_TC(tp, ttyname_r_stdin);
	ATF_TP_ADD_TC(tp, ttyname_stdin);

	return atf_no_error();
}
