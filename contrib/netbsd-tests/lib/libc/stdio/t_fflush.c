/*	$NetBSD: t_fflush.c,v 1.1 2011/09/11 05:15:55 jruoho Exp $ */

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
__RCSID("$NetBSD: t_fflush.c,v 1.1 2011/09/11 05:15:55 jruoho Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static const char *path = "fflush";

ATF_TC_WITH_CLEANUP(fflush_err);
ATF_TC_HEAD(fflush_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from fflush(3)");
}

ATF_TC_BODY(fflush_err, tc)
{
	FILE *f;

#ifdef __FreeBSD__
	atf_tc_expect_fail("the EOF invariant fails on FreeBSD; this is new");
#endif

	f = fopen(path, "w");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fflush(NULL) == 0);
	ATF_REQUIRE(fclose(f) == 0);

	f = fopen(path, "r");
	ATF_REQUIRE(f != NULL);

	/*
	 * In NetBSD the call should fail if the supplied
	 * parameteris not an open stream or the stream is
	 * not open for writing.
	 */
	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, fflush(f) == EOF);

	ATF_REQUIRE(fclose(f) == 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, fflush(f) == EOF);

	(void)unlink(path);
}

ATF_TC_CLEANUP(fflush_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fflush_seek);
ATF_TC_HEAD(fflush_seek, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test file offsets with fflush(3)");
}

ATF_TC_BODY(fflush_seek, tc)
{
	char buf[12];
	int fd = -1;
	FILE *f;

	/*
	 * IEEE Std 1003.1-2008:
	 *
	 * "For a stream open for reading, if the file
	 *  is not already at EOF, and the file is one
	 *  capable of seeking, the file offset of the
	 *  underlying open file description shall be
	 *  adjusted so that the next operation on the
	 *  open file description deals with the byte
	 *  after the last one read from or written to
	 *  the stream being flushed."
	 */
	f = fopen(path, "w");
	ATF_REQUIRE(f != NULL);

	ATF_REQUIRE(fwrite("garbage", 1, 7, f) == 7);
	ATF_REQUIRE(fclose(f) == 0);

	f = fopen(path, "r+");
	ATF_REQUIRE(f != NULL);

	fd = fileno(f);
	ATF_REQUIRE(fd != -1);

	ATF_REQUIRE(fread(buf, 1, 3, f) == 3);
	ATF_REQUIRE(fflush(f) == 0);
	ATF_REQUIRE(fseek(f, 0, SEEK_CUR) == 0);

	/*
	 * Verify that the offsets are right and that
	 * a read operation resumes at the correct location.
	 */
	ATF_REQUIRE(ftell(f) == 3);
	ATF_REQUIRE(lseek(fd, 0, SEEK_CUR) == 3);
	ATF_REQUIRE(fgetc(f) == 'b');

	ATF_REQUIRE(fclose(f) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(fflush_seek, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fpurge_err);
ATF_TC_HEAD(fpurge_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from fpurge(3)");
}

ATF_TC_BODY(fpurge_err, tc)
{
	FILE *f;

	f = fopen(path, "w");
	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fclose(f) == 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, fpurge(f) == EOF);

	(void)unlink(path);
}

ATF_TC_CLEANUP(fpurge_err, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fflush_err);
	ATF_TP_ADD_TC(tp, fflush_seek);
	ATF_TP_ADD_TC(tp, fpurge_err);

	return atf_no_error();
}
