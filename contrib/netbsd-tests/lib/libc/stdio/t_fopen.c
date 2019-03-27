/*	$NetBSD: t_fopen.c,v 1.3 2011/09/14 14:34:37 martin Exp $ */

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
__RCSID("$NetBSD: t_fopen.c,v 1.3 2011/09/14 14:34:37 martin Exp $");

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *path = "fopen";

ATF_TC_WITH_CLEANUP(fdopen_close);
ATF_TC_HEAD(fdopen_close, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that descriptors are closed");
}

ATF_TC_BODY(fdopen_close, tc)
{
	FILE *f;
	int fd;

	/*
	 * Check that the file descriptor
	 * used to fdopen(3) a stream is
	 * closed once the stream is closed.
	 */
	fd = open(path, O_RDWR | O_CREAT);

	ATF_REQUIRE(fd >= 0);

	f = fdopen(fd, "w+");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fclose(f) == 0);
	ATF_REQUIRE(close(fd) == -1);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(fdopen_close, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fdopen_err);
ATF_TC_HEAD(fdopen_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from fdopen(3)");
}

ATF_TC_BODY(fdopen_err, tc)
{
	int fd;

	fd = open(path, O_RDONLY | O_CREAT);
	ATF_REQUIRE(fd >= 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, fdopen(fd, "w") == NULL);

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, fdopen(fd, "a") == NULL);

	ATF_REQUIRE(close(fd) == 0);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, fdopen(fd, "r") == NULL);

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, fdopen(-1, "w+") == NULL);

	(void)unlink(path);
}

ATF_TC_CLEANUP(fdopen_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fdopen_seek);
ATF_TC_HEAD(fdopen_seek, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test stream position with fdopen(3)");
}

ATF_TC_BODY(fdopen_seek, tc)
{
	FILE *f;
	int fd;

	/*
	 * Verify that the file position associated
	 * with the stream corresponds with the offset
	 * set earlier for the file descriptor.
	 */
	fd = open(path, O_RDWR | O_CREAT);

	ATF_REQUIRE(fd >= 0);
	ATF_REQUIRE(write(fd, "garbage", 7) == 7);
	ATF_REQUIRE(lseek(fd, 3, SEEK_SET) == 3);

	f = fdopen(fd, "r+");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(ftell(f) == 3);
	ATF_REQUIRE(fclose(f) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(fdopen_seek, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fopen_err);
ATF_TC_HEAD(fopen_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from fopen(3)");
}

ATF_TC_BODY(fopen_err, tc)
{
	static const char *mode[] = {
		"x", "xr", "xr", "+r+", "R", "W+", " aXX", "Xr", " r+", "" };

	char buf[PATH_MAX + 1];
	size_t i;
	FILE *f;

	f = fopen(path, "w+");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fclose(f) == 0);

	/*
	 * Note that also "invalid" characters
	 * may follow the mode-string whenever
	 * the first character is valid.
	 */
	for (i = 0; i < __arraycount(mode); i++) {

		errno = 0;
		f = fopen(path, mode[i]);

		if (f == NULL && errno == EINVAL)
			continue;

		if (f != NULL)
			(void)fclose(f);

		atf_tc_fail_nonfatal("opened file as '%s'",  mode[i]);
	}

	(void)unlink(path);
	(void)memset(buf, 'x', sizeof(buf));

	errno = 0;
	ATF_REQUIRE_ERRNO(EISDIR, fopen("/usr/bin", "w") == NULL);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOENT, fopen("/a/b/c/d/e/f", "r") == NULL);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENAMETOOLONG, fopen(buf, "r+") == NULL);
}

ATF_TC_CLEANUP(fopen_err, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fopen_append);
ATF_TC_HEAD(fopen_append, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that append-mode works");
}

ATF_TC_BODY(fopen_append, tc)
{
	char buf[15];
	FILE *f;

	(void)memset(buf, 'x', sizeof(buf));

	f = fopen(path, "w+");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fwrite("garbage", 1, 7, f) == 7);
	ATF_REQUIRE(fclose(f) == 0);

	f = fopen(path, "a");

	ATF_REQUIRE(fwrite("garbage", 1, 7, f) == 7);
	ATF_REQUIRE(fclose(f) == 0);

	f = fopen(path, "r");

	ATF_REQUIRE(fread(buf, 1, sizeof(buf), f) == 14);
	ATF_REQUIRE(strncmp(buf, "garbagegarbage", 14) == 0);

	ATF_REQUIRE(fclose(f) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(fopen_append, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(fopen_mode);
ATF_TC_HEAD(fopen_mode, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test fopen(3) modes");
}

ATF_TC_BODY(fopen_mode, tc)
{
	size_t i;
	FILE *f;

	static const char *mode[] = {
		"r", "r+", "w", "w+", "a", "a+",
		"rb", "r+b", "wb", "w+b", "ab", "a+b"
		"re", "r+e", "we", "w+e", "ae", "a+e"
		"rf", "r+f", "wf", "w+f", "af", "a+f"
	};

	f = fopen(path, "w+");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fclose(f) == 0);

	/*
	 * Verify that various modes work.
	 */
	for (i = 0; i < __arraycount(mode); i++) {

		f = fopen(path, mode[i]);

		if (f != NULL) {
			ATF_REQUIRE(fclose(f) == 0);
			continue;
		}

		atf_tc_fail_nonfatal("failed to open file as %s",  mode[i]);
	}

	(void)unlink(path);
}

ATF_TC_CLEANUP(fopen_mode, tc)
{
	(void)unlink(path);
}

ATF_TC(fopen_perm);
ATF_TC_HEAD(fopen_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test permissions with fopen(3)");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(fopen_perm, tc)
{

	errno = 0;
	ATF_REQUIRE_ERRNO(EACCES, fopen("/bin/ls", "a+") == NULL);

	errno = 0;
	ATF_REQUIRE_ERRNO(EACCES, fopen("/bin/ls", "w+") == NULL);
}

#ifdef __NetBSD__
ATF_TC(fopen_regular);
ATF_TC_HEAD(fopen_regular, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test fopen(3) with 'f' mode");
}

ATF_TC_BODY(fopen_regular, tc)
{
	static const char *mode[] = { "rf", "r+f", "wf", "w+f", "af", "a+f" };
	static const char *devs[] = { _PATH_DEVNULL };

	size_t i, j;
	FILE *f;

	for (i = 0; i < __arraycount(devs); i++) {

		for (j = 0; j < __arraycount(mode); j++) {

			errno = 0;
			f = fopen(devs[i], mode[j]);

			if (f == NULL && errno == EFTYPE)
				continue;

			if (f != NULL)
				(void)fclose(f);

			atf_tc_fail_nonfatal("opened %s as %s",
			    devs[i], mode[j]);
		}
	}
}
#endif

ATF_TC_WITH_CLEANUP(fopen_seek);
ATF_TC_HEAD(fopen_seek, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test initial stream position");
}

ATF_TC_BODY(fopen_seek, tc)
{
	FILE *f;

	f = fopen(path, "w+");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(fwrite("garbage", 1, 7, f) == 7);
	ATF_REQUIRE(fclose(f) == 0);

	/*
	 * The position of the stream should be
	 * at the start, except for append-mode.
	 */
	f = fopen(path, "r");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(ftello(f) == 0);
	ATF_REQUIRE(fclose(f) == 0);

	f = fopen(path, "a");

	ATF_REQUIRE(f != NULL);
	ATF_REQUIRE(ftello(f) == 7);
	ATF_REQUIRE(fclose(f) == 0);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(fopen_seek, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(freopen_std);
ATF_TC_HEAD(freopen_std, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of freopen(3)");
}

ATF_TC_BODY(freopen_std, tc)
{
	FILE *std[2] = { stdin, stdout };
	char buf[15];
	size_t i;
	FILE *f;

	/*
	 * Associate a standard stream with a custom stream.
	 * Then write to the standard stream and verify that
	 * the result now appears in the custom stream.
	 */
	for (i = 0; i < __arraycount(std); i++) {

		(void)memset(buf, 'x', sizeof(buf));

		f = fopen(path, "w+");
		ATF_REQUIRE(f != NULL);

		f = freopen(path, "w+", std[i]);
		ATF_REQUIRE(f != NULL);

		ATF_REQUIRE(fwrite("garbage", 1, 7, f) == 7);
		ATF_REQUIRE(fprintf(std[i], "garbage") == 7);
		ATF_REQUIRE(fclose(f) == 0);

		f = fopen(path, "r");

		ATF_REQUIRE(f != NULL);
		ATF_REQUIRE(fread(buf, 1, sizeof(buf), f) == 14);
		ATF_REQUIRE(strncmp(buf, "garbagegarbage", 14) == 0);

		ATF_REQUIRE(fclose(f) == 0);
	}

	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(freopen_std, tc)
{
	(void)unlink(path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fdopen_close);
	ATF_TP_ADD_TC(tp, fdopen_err);
	ATF_TP_ADD_TC(tp, fdopen_seek);
	ATF_TP_ADD_TC(tp, fopen_append);
	ATF_TP_ADD_TC(tp, fopen_err);
	ATF_TP_ADD_TC(tp, fopen_mode);
	ATF_TP_ADD_TC(tp, fopen_perm);
#ifdef __NetBSD__
	ATF_TP_ADD_TC(tp, fopen_regular);
#endif
	ATF_TP_ADD_TC(tp, fopen_seek);
	ATF_TP_ADD_TC(tp, freopen_std);

	return atf_no_error();
}
