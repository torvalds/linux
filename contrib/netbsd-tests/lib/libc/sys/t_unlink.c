/* $NetBSD: t_unlink.c,v 1.4 2017/01/14 20:55:26 christos Exp $ */

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
__RCSID("$NetBSD: t_unlink.c,v 1.4 2017/01/14 20:55:26 christos Exp $");

#include <sys/stat.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

static char	 path[] = "unlink";

ATF_TC_WITH_CLEANUP(unlink_basic);
ATF_TC_HEAD(unlink_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of unlink(2)");
}

ATF_TC_BODY(unlink_basic, tc)
{
	const size_t n = 512;
	size_t i;
	int fd;

	for (i = 0; i < n; i++) {

		fd = open(path, O_RDWR | O_CREAT, 0666);

		ATF_REQUIRE(fd != -1);
		ATF_REQUIRE(close(fd) == 0);
		ATF_REQUIRE(unlink(path) == 0);

		errno = 0;
		ATF_REQUIRE_ERRNO(ENOENT, open(path, O_RDONLY) == -1);
	}
}

ATF_TC_CLEANUP(unlink_basic, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(unlink_err);
ATF_TC_HEAD(unlink_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of unlink(2)");
}

ATF_TC_BODY(unlink_err, tc)
{
	char buf[PATH_MAX + 1];

	(void)memset(buf, 'x', sizeof(buf));

	errno = 0;
#ifdef __FreeBSD__
	ATF_REQUIRE_ERRNO(EISDIR, unlink("/") == -1);
#else
	ATF_REQUIRE_ERRNO(EBUSY, unlink("/") == -1);
#endif

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, unlink(buf) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, unlink("/a/b/c/d/e/f/g/h/i/j/k/l/m") == -1);
}

ATF_TC_CLEANUP(unlink_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(unlink_fifo);
ATF_TC_HEAD(unlink_fifo, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test unlink(2) for a FIFO");
}

ATF_TC_BODY(unlink_fifo, tc)
{

	ATF_REQUIRE(mkfifo(path, 0666) == 0);
	ATF_REQUIRE(unlink(path) == 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, open(path, O_RDONLY) == -1);
}

ATF_TC_CLEANUP(unlink_fifo, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(unlink_perm);
ATF_TC_HEAD(unlink_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with unlink(2)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(unlink_perm, tc)
{
	int rv;

	errno = 0;
	rv = unlink("/etc");
	ATF_REQUIRE_MSG(rv == -1 && (errno == EACCES || errno == EPERM),
	    "unlinking a directory did not fail with EPERM or EACCESS; "
	    "unlink() returned %d, errno %d", rv, errno);

	errno = 0;
	ATF_REQUIRE_ERRNO(EACCES, unlink("/root/.profile") == -1);
}

ATF_TC_CLEANUP(unlink_perm, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, unlink_basic);
	ATF_TP_ADD_TC(tp, unlink_err);
	ATF_TP_ADD_TC(tp, unlink_fifo);
	ATF_TP_ADD_TC(tp, unlink_perm);

	return atf_no_error();
}
