/* $NetBSD: t_fsync.c,v 1.2 2012/03/18 07:00:52 jruoho Exp $ */

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
__RCSID("$NetBSD: t_fsync.c,v 1.2 2012/03/18 07:00:52 jruoho Exp $");

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC(fsync_err);
ATF_TC_HEAD(fsync_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test error conditions of fsync(2) (PR kern/30)");
}

ATF_TC_BODY(fsync_err, tc)
{
	int i, fd[2];

	/*
	 * The fsync(2) call should fail with EBADF
	 * when the 'fd' is not a valid descriptor.
	 */
	for (i = 1; i < 1024; i = i + 128) {

		errno = 0;

		ATF_REQUIRE(fsync(-i) == -1);
		ATF_REQUIRE(errno == EBADF);
	}

	/*
	 * On the other hand, EINVAL should follow
	 * if the operation is not possible with
	 * the file descriptor.
	 */
	ATF_REQUIRE(pipe(fd) == 0);

	errno = 0;

	ATF_REQUIRE(fsync(fd[0]) == -1);
	ATF_REQUIRE(errno == EINVAL);

	errno = 0;

	ATF_REQUIRE(fsync(fd[1]) == -1);
	ATF_REQUIRE(errno == EINVAL);

	ATF_REQUIRE(close(fd[0]) == 0);
	ATF_REQUIRE(close(fd[1]) == 0);
}

ATF_TC(fsync_sync);
ATF_TC_HEAD(fsync_sync, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of fsync(2)");
}

ATF_TC_BODY(fsync_sync, tc)
{
	char buf[128];
	int fd, i;

	for (i = 0; i < 10; i++) {

		(void)snprintf(buf, sizeof(buf), "t_fsync-%d", i);

		fd = mkstemp(buf);

		ATF_REQUIRE(fd != -1);
		ATF_REQUIRE(write(fd, "0", 1) == 1);
		ATF_REQUIRE(fsync(fd) == 0);

		ATF_REQUIRE(unlink(buf) == 0);
		ATF_REQUIRE(close(fd) == 0);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fsync_err);
	ATF_TP_ADD_TC(tp, fsync_sync);

	return atf_no_error();
}
