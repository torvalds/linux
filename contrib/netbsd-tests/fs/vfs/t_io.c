/*	$NetBSD: t_io.c,v 1.17 2017/01/13 21:30:40 christos Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <atf-c.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include "../common/h_fsmacros.h"
#include "h_macros.h"

#define TESTSTR "this is a string.  collect enough and you'll have Em"
#define TESTSZ sizeof(TESTSTR)

static void
holywrite(const atf_tc_t *tc, const char *mp)
{
	char buf[1024];
	char *b2, *b3;
	size_t therange = getpagesize()+1;
	int fd;

	FSTEST_ENTER();

	RL(fd = rump_sys_open("file", O_RDWR|O_CREAT|O_TRUNC, 0666));

	memset(buf, 'A', sizeof(buf));
	RL(rump_sys_pwrite(fd, buf, 1, getpagesize()));

	memset(buf, 'B', sizeof(buf));
	RL(rump_sys_pwrite(fd, buf, 2, getpagesize()-1));

	REQUIRE_LIBC(b2 = malloc(2 * getpagesize()), NULL);
	REQUIRE_LIBC(b3 = malloc(2 * getpagesize()), NULL);

	RL(rump_sys_pread(fd, b2, therange, 0));

	memset(b3, 0, therange);
	memset(b3 + getpagesize() - 1, 'B', 2);

	ATF_REQUIRE_EQ(memcmp(b2, b3, therange), 0);

	rump_sys_close(fd);
	FSTEST_EXIT();
}

static void
extendbody(const atf_tc_t *tc, off_t seekcnt)
{
	char buf[TESTSZ+1];
	struct stat sb;
	int fd;

	FSTEST_ENTER();
	RL(fd = rump_sys_open("testfile",
	    O_CREAT | O_RDWR | (seekcnt ? O_APPEND : 0)));
	RL(rump_sys_ftruncate(fd, seekcnt));
	RL(rump_sys_fstat(fd, &sb));
	ATF_REQUIRE_EQ(sb.st_size, seekcnt);

	ATF_REQUIRE_EQ(rump_sys_write(fd, TESTSTR, TESTSZ), TESTSZ);
	ATF_REQUIRE_EQ(rump_sys_pread(fd, buf, TESTSZ, seekcnt), TESTSZ);
	ATF_REQUIRE_STREQ(buf, TESTSTR);

	RL(rump_sys_fstat(fd, &sb));
	ATF_REQUIRE_EQ(sb.st_size, (off_t)TESTSZ + seekcnt);
	RL(rump_sys_close(fd));
	FSTEST_EXIT();
}

static void
extendfile(const atf_tc_t *tc, const char *mp)
{

	extendbody(tc, 0);
}

static void
extendfile_append(const atf_tc_t *tc, const char *mp)
{

	extendbody(tc, 37);
}

static void
overwritebody(const atf_tc_t *tc, off_t count, bool dotrunc)
{
	char *buf;
	int fd;

	REQUIRE_LIBC(buf = malloc(count), NULL);
	FSTEST_ENTER();
	RL(fd = rump_sys_open("testi", O_CREAT | O_RDWR, 0666));
	ATF_REQUIRE_EQ(rump_sys_write(fd, buf, count), count);
	RL(rump_sys_close(fd));

	RL(fd = rump_sys_open("testi", O_RDWR));
	if (dotrunc)
		RL(rump_sys_ftruncate(fd, 0));
	ATF_REQUIRE_EQ(rump_sys_write(fd, buf, count), count);
	RL(rump_sys_close(fd));
	FSTEST_EXIT();
}

static void
overwrite512(const atf_tc_t *tc, const char *mp)
{

	overwritebody(tc, 512, false);
}

static void
overwrite64k(const atf_tc_t *tc, const char *mp)
{

	overwritebody(tc, 1<<16, false);
}

static void
overwrite_trunc(const atf_tc_t *tc, const char *mp)
{

	overwritebody(tc, 1<<16, true);
}

static void
shrinkfile(const atf_tc_t *tc, const char *mp)
{
	int fd;

	FSTEST_ENTER();
	RL(fd = rump_sys_open("file", O_RDWR|O_CREAT|O_TRUNC, 0666));
	RL(rump_sys_ftruncate(fd, 2));
	RL(rump_sys_ftruncate(fd, 1));
	rump_sys_close(fd);
	FSTEST_EXIT();
}

#define TBSIZE 9000
static void
read_after_unlink(const atf_tc_t *tc, const char *mp)
{
	char buf[TBSIZE], buf2[TBSIZE];
	int fd;

	FSTEST_ENTER();

	/* create file and put some content into it */
	RL(fd = rump_sys_open("file", O_RDWR|O_CREAT, 0666));
	memset(buf, 'D', TBSIZE);
	ATF_REQUIRE_EQ(rump_sys_write(fd, buf, TBSIZE), TBSIZE);
	rump_sys_close(fd);

	/* flush buffers from UBC to file system */
	ATF_REQUIRE_ERRNO(EBUSY, rump_sys_unmount(mp, 0) == -1);

	RL(fd = rump_sys_open("file", O_RDWR));
	RL(rump_sys_unlink("file"));

	ATF_REQUIRE_EQ(rump_sys_read(fd, buf2, TBSIZE), TBSIZE);
	ATF_REQUIRE_EQ(memcmp(buf, buf2, TBSIZE), 0);
	rump_sys_close(fd);

	FSTEST_EXIT();
}

static void
wrrd_after_unlink(const atf_tc_t *tc, const char *mp)
{
	int value = 0x11;
	int v2;
	int fd;

	FSTEST_ENTER();

	RL(fd = rump_sys_open("file", O_RDWR|O_CREAT, 0666));
	RL(rump_sys_unlink("file"));

	RL(rump_sys_pwrite(fd, &value, sizeof(value), 654321));

	/*
	 * We can't easily invalidate the buffer since we hold a
	 * reference, but try to get them to flush anyway.
	 */
	RL(rump_sys_fsync(fd));
	RL(rump_sys_pread(fd, &v2, sizeof(v2), 654321));
	rump_sys_close(fd);

	ATF_REQUIRE_EQ(value, v2);
	FSTEST_EXIT();
}

static void
read_fault(const atf_tc_t *tc, const char *mp)
{
	char ch = 123;
	int fd;

	FSTEST_ENTER();
	RL(fd = rump_sys_open("file", O_CREAT | O_RDWR, 0777));
	ATF_REQUIRE_EQ(rump_sys_write(fd, &ch, 1), 1);
	RL(rump_sys_close(fd));
	RL(fd = rump_sys_open("file", O_RDONLY | O_SYNC | O_RSYNC));
	ATF_REQUIRE_ERRNO(EFAULT, rump_sys_read(fd, NULL, 1) == -1);
	RL(rump_sys_close(fd));
	FSTEST_EXIT();
}

ATF_TC_FSAPPLY(holywrite, "create a sparse file and fill hole");
ATF_TC_FSAPPLY(extendfile, "check that extending a file works");
ATF_TC_FSAPPLY(extendfile_append, "check that extending a file works "
				  "with a append-only fd (PR kern/44307)");
ATF_TC_FSAPPLY(overwrite512, "write a 512 byte file twice");
ATF_TC_FSAPPLY(overwrite64k, "write a 64k byte file twice");
ATF_TC_FSAPPLY(overwrite_trunc, "write 64k + truncate + rewrite");
ATF_TC_FSAPPLY(shrinkfile, "shrink file");
ATF_TC_FSAPPLY(read_after_unlink, "contents can be read off disk after unlink");
ATF_TC_FSAPPLY(wrrd_after_unlink, "file can be written and read after unlink");
ATF_TC_FSAPPLY(read_fault, "read at bad address must return EFAULT");

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_FSAPPLY(holywrite);
	ATF_TP_FSAPPLY(extendfile);
	ATF_TP_FSAPPLY(extendfile_append);
	ATF_TP_FSAPPLY(overwrite512);
	ATF_TP_FSAPPLY(overwrite64k);
	ATF_TP_FSAPPLY(overwrite_trunc);
	ATF_TP_FSAPPLY(shrinkfile);
	ATF_TP_FSAPPLY(read_after_unlink);
	ATF_TP_FSAPPLY(wrrd_after_unlink);
	ATF_TP_FSAPPLY(read_fault);

	return atf_no_error();
}
