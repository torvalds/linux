/* $NetBSD: t_mmap.c,v 1.12 2017/01/16 16:31:05 christos Exp $ */

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

/*-
 * Copyright (c)2004 YAMAMOTO Takashi,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__RCSID("$NetBSD: t_mmap.c,v 1.12 2017/01/16 16:31:05 christos Exp $");

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#ifdef __FreeBSD__
#include <stdint.h>
#endif

static long	page = 0;
static char	path[] = "mmap";
static void	map_check(void *, int);
static void	map_sighandler(int);
static void	testloan(void *, void *, char, int);

#define	BUFSIZE	(32 * 1024)	/* enough size to trigger sosend_loan */

static void
map_check(void *map, int flag)
{

	if (flag != 0) {
		ATF_REQUIRE(map == MAP_FAILED);
		return;
	}

	ATF_REQUIRE(map != MAP_FAILED);
	ATF_REQUIRE(munmap(map, page) == 0);
}

void
testloan(void *vp, void *vp2, char pat, int docheck)
{
	char buf[BUFSIZE];
	char backup[BUFSIZE];
	ssize_t nwritten;
	ssize_t nread;
	int fds[2];
	int val;

	val = BUFSIZE;

	if (docheck != 0)
		(void)memcpy(backup, vp, BUFSIZE);

	if (socketpair(AF_LOCAL, SOCK_STREAM, PF_UNSPEC, fds) != 0)
		atf_tc_fail("socketpair() failed");

	val = BUFSIZE;

	if (setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) != 0)
		atf_tc_fail("setsockopt() failed, SO_RCVBUF");

	val = BUFSIZE;

	if (setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) != 0)
		atf_tc_fail("setsockopt() failed, SO_SNDBUF");

	if (fcntl(fds[0], F_SETFL, O_NONBLOCK) != 0)
		atf_tc_fail("fcntl() failed");

	nwritten = write(fds[0], (char *)vp + page, BUFSIZE - page);

	if (nwritten == -1)
		atf_tc_fail("write() failed");

	/* Break loan. */
	(void)memset(vp2, pat, BUFSIZE);

	nread = read(fds[1], buf + page, BUFSIZE - page);

	if (nread == -1)
		atf_tc_fail("read() failed");

	if (nread != nwritten)
		atf_tc_fail("too short read");

	if (docheck != 0 && memcmp(backup, buf + page, nread) != 0)
		atf_tc_fail("data mismatch");

	ATF_REQUIRE(close(fds[0]) == 0);
	ATF_REQUIRE(close(fds[1]) == 0);
}

static void
map_sighandler(int signo)
{
	_exit(signo);
}

#ifdef __NetBSD__
ATF_TC(mmap_block);
ATF_TC_HEAD(mmap_block, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mmap(2) with a block device");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(mmap_block, tc)
{
	static const int mib[] = { CTL_HW, HW_DISKNAMES };
	static const unsigned int miblen = __arraycount(mib);
	char *map, *dk, *drives, dev[PATH_MAX];
	size_t len;
	int fd = -1;

	atf_tc_skip("The test case causes a panic (PR kern/38889, kern/46592)");

	ATF_REQUIRE(sysctl(mib, miblen, NULL, &len, NULL, 0) == 0);
	drives = malloc(len);
	ATF_REQUIRE(drives != NULL);
	ATF_REQUIRE(sysctl(mib, miblen, drives, &len, NULL, 0) == 0);
	for (dk = strtok(drives, " "); dk != NULL; dk = strtok(NULL, " ")) {
		sprintf(dev, _PATH_DEV "%s%c", dk, 'a'+RAW_PART);
		fprintf(stderr, "trying: %s\n", dev);

		if ((fd = open(dev, O_RDONLY)) >= 0) {
			(void)fprintf(stderr, "using %s\n", dev);
			break;
		}
	}
	free(drives);

	if (fd < 0)
		atf_tc_skip("failed to find suitable block device");

	map = mmap(NULL, 4096, PROT_READ, MAP_FILE, fd, 0);
	ATF_REQUIRE(map != MAP_FAILED);

	(void)fprintf(stderr, "first byte %x\n", *map);
	ATF_REQUIRE(close(fd) == 0);
	(void)fprintf(stderr, "first byte %x\n", *map);

	ATF_REQUIRE(munmap(map, 4096) == 0);
}
#endif

ATF_TC(mmap_err);
ATF_TC_HEAD(mmap_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions of mmap(2)");
}

ATF_TC_BODY(mmap_err, tc)
{
	size_t addr = SIZE_MAX;
	void *map;

	errno = 0;
	map = mmap(NULL, 3, PROT_READ, MAP_FILE|MAP_PRIVATE, -1, 0);

	ATF_REQUIRE(map == MAP_FAILED);
	ATF_REQUIRE(errno == EBADF);

	errno = 0;
	map = mmap(&addr, page, PROT_READ, MAP_FIXED|MAP_PRIVATE, -1, 0);

	ATF_REQUIRE(map == MAP_FAILED);
	ATF_REQUIRE(errno == EINVAL);

	errno = 0;
	map = mmap(NULL, page, PROT_READ, MAP_ANON|MAP_PRIVATE, INT_MAX, 0);

	ATF_REQUIRE(map == MAP_FAILED);
	ATF_REQUIRE(errno == EINVAL);
}

ATF_TC_WITH_CLEANUP(mmap_loan);
ATF_TC_HEAD(mmap_loan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test uvm page loanout with mmap(2)");
}

ATF_TC_BODY(mmap_loan, tc)
{
	char buf[BUFSIZE];
	char *vp, *vp2;
	int fd;

	fd = open(path, O_RDWR | O_CREAT, 0600);
	ATF_REQUIRE(fd >= 0);

	(void)memset(buf, 'x', sizeof(buf));
	(void)write(fd, buf, sizeof(buf));

	vp = mmap(NULL, BUFSIZE, PROT_READ | PROT_WRITE,
	    MAP_FILE | MAP_PRIVATE, fd, 0);

	ATF_REQUIRE(vp != MAP_FAILED);

	vp2 = vp;

	testloan(vp, vp2, 'A', 0);
	testloan(vp, vp2, 'B', 1);

	ATF_REQUIRE(munmap(vp, BUFSIZE) == 0);

	vp = mmap(NULL, BUFSIZE, PROT_READ | PROT_WRITE,
	    MAP_FILE | MAP_SHARED, fd, 0);

	vp2 = mmap(NULL, BUFSIZE, PROT_READ | PROT_WRITE,
	    MAP_FILE | MAP_SHARED, fd, 0);

	ATF_REQUIRE(vp != MAP_FAILED);
	ATF_REQUIRE(vp2 != MAP_FAILED);

	testloan(vp, vp2, 'E', 1);

	ATF_REQUIRE(munmap(vp, BUFSIZE) == 0);
	ATF_REQUIRE(munmap(vp2, BUFSIZE) == 0);
}

ATF_TC_CLEANUP(mmap_loan, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mmap_prot_1);
ATF_TC_HEAD(mmap_prot_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mmap(2) protections, #1");
}

ATF_TC_BODY(mmap_prot_1, tc)
{
	void *map;
	int fd;

	/*
	 * Open a file write-only and try to
	 * map it read-only. This should fail.
	 */
	fd = open(path, O_WRONLY | O_CREAT, 0700);

	if (fd < 0)
		return;

	ATF_REQUIRE(write(fd, "XXX", 3) == 3);

	map = mmap(NULL, 3, PROT_READ, MAP_FILE|MAP_PRIVATE, fd, 0);
	map_check(map, 1);

	map = mmap(NULL, 3, PROT_WRITE, MAP_FILE|MAP_PRIVATE, fd, 0);
	map_check(map, 0);

	ATF_REQUIRE(close(fd) == 0);
}

ATF_TC_CLEANUP(mmap_prot_1, tc)
{
	(void)unlink(path);
}

ATF_TC(mmap_prot_2);
ATF_TC_HEAD(mmap_prot_2, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mmap(2) protections, #2");
}

ATF_TC_BODY(mmap_prot_2, tc)
{
	char buf[2];
	void *map;
	pid_t pid;
	int sta;

	/*
	 * Make a PROT_NONE mapping and try to access it.
	 * If we catch a SIGSEGV, all works as expected.
	 */
	map = mmap(NULL, page, PROT_NONE, MAP_ANON|MAP_PRIVATE, -1, 0);
	ATF_REQUIRE(map != MAP_FAILED);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {
		ATF_REQUIRE(signal(SIGSEGV, map_sighandler) != SIG_ERR);
		ATF_REQUIRE(strlcpy(buf, map, sizeof(buf)) != 0);
	}

	(void)wait(&sta);

	ATF_REQUIRE(WIFEXITED(sta) != 0);
	ATF_REQUIRE(WEXITSTATUS(sta) == SIGSEGV);
	ATF_REQUIRE(munmap(map, page) == 0);
}

ATF_TC_WITH_CLEANUP(mmap_prot_3);
ATF_TC_HEAD(mmap_prot_3, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mmap(2) protections, #3");
}

ATF_TC_BODY(mmap_prot_3, tc)
{
	char buf[2];
	int fd, sta;
	void *map;
	pid_t pid;

	/*
	 * Open a file, change the permissions
	 * to read-only, and try to map it as
	 * PROT_NONE. This should succeed, but
	 * the access should generate SIGSEGV.
	 */
	fd = open(path, O_RDWR | O_CREAT, 0700);

	if (fd < 0)
#ifdef	__FreeBSD__
		atf_tc_skip("opening %s failed; skipping testcase: %s",
		    path, strerror(errno));
#else
		return;
#endif

	ATF_REQUIRE(write(fd, "XXX", 3) == 3);
	ATF_REQUIRE(close(fd) == 0);
	ATF_REQUIRE(chmod(path, 0444) == 0);

	fd = open(path, O_RDONLY);
	ATF_REQUIRE(fd != -1);

	map = mmap(NULL, 3, PROT_NONE, MAP_FILE | MAP_SHARED, fd, 0);
	ATF_REQUIRE(map != MAP_FAILED);

	pid = fork();

	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {
		ATF_REQUIRE(signal(SIGSEGV, map_sighandler) != SIG_ERR);
		ATF_REQUIRE(strlcpy(buf, map, sizeof(buf)) != 0);
	}

	(void)wait(&sta);

	ATF_REQUIRE(WIFEXITED(sta) != 0);
	ATF_REQUIRE(WEXITSTATUS(sta) == SIGSEGV);
	ATF_REQUIRE(munmap(map, 3) == 0);
#ifdef	__FreeBSD__
	(void)close(fd);
#endif
}

ATF_TC_CLEANUP(mmap_prot_3, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mmap_truncate);
ATF_TC_HEAD(mmap_truncate, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mmap(2) and ftruncate(2)");
}

ATF_TC_BODY(mmap_truncate, tc)
{
	char *map;
	long i;
	int fd;

	fd = open(path, O_RDWR | O_CREAT, 0700);

	if (fd < 0)
		return;

	/*
	 * See that ftruncate(2) works
	 * while the file is mapped.
	 */
	ATF_REQUIRE(ftruncate(fd, page) == 0);

	map = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_FILE|MAP_PRIVATE,
	     fd, 0);
	ATF_REQUIRE(map != MAP_FAILED);

	for (i = 0; i < page; i++)
		map[i] = 'x';

	ATF_REQUIRE(ftruncate(fd, 0) == 0);
	ATF_REQUIRE(ftruncate(fd, page / 8) == 0);
	ATF_REQUIRE(ftruncate(fd, page / 4) == 0);
	ATF_REQUIRE(ftruncate(fd, page / 2) == 0);
	ATF_REQUIRE(ftruncate(fd, page / 12) == 0);
	ATF_REQUIRE(ftruncate(fd, page / 64) == 0);

	(void)munmap(map, page);
	ATF_REQUIRE(close(fd) == 0);
}

ATF_TC_CLEANUP(mmap_truncate, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(mmap_truncate_signal);
ATF_TC_HEAD(mmap_truncate_signal, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test mmap(2) ftruncate(2) causing signal");
}

ATF_TC_BODY(mmap_truncate_signal, tc)
{
	char *map;
	long i;
	int fd, sta;
	pid_t pid;

#ifdef __FreeBSD__
	atf_tc_expect_fail("testcase fails with SIGSEGV on FreeBSD; bug # 211924");
#endif

	fd = open(path, O_RDWR | O_CREAT, 0700);

	if (fd < 0)
		return;

	ATF_REQUIRE(write(fd, "foo\n", 5) == 5);

	map = mmap(NULL, page, PROT_READ, MAP_FILE|MAP_PRIVATE, fd, 0);
	ATF_REQUIRE(map != MAP_FAILED);

	sta = 0;
	for (i = 0; i < 5; i++)
		sta += map[i];
	ATF_REQUIRE(sta == 334);

	ATF_REQUIRE(ftruncate(fd, 0) == 0);
	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {
		ATF_REQUIRE(signal(SIGBUS, map_sighandler) != SIG_ERR);
		ATF_REQUIRE(signal(SIGSEGV, map_sighandler) != SIG_ERR);
		sta = 0;
		for (i = 0; i < page; i++)
			sta += map[i];
		/* child never will get this far, but the compiler will
		   not know, so better use the values calculated to
		   prevent the access to be optimized out */
		ATF_REQUIRE(i == 0);
		ATF_REQUIRE(sta == 0);
		(void)munmap(map, page);
		(void)close(fd);
		return;
	}

	(void)wait(&sta);

	ATF_REQUIRE(WIFEXITED(sta) != 0);
	if (WEXITSTATUS(sta) == SIGSEGV)
		atf_tc_fail("child process got SIGSEGV instead of SIGBUS");
	ATF_REQUIRE(WEXITSTATUS(sta) == SIGBUS);
	ATF_REQUIRE(munmap(map, page) == 0);
	ATF_REQUIRE(close(fd) == 0);
}

ATF_TC_CLEANUP(mmap_truncate_signal, tc)
{
	(void)unlink(path);
}

ATF_TC(mmap_va0);
ATF_TC_HEAD(mmap_va0, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mmap(2) and vm.user_va0_disable");
}

ATF_TC_BODY(mmap_va0, tc)
{
	int flags = MAP_ANON | MAP_FIXED | MAP_PRIVATE;
	size_t len = sizeof(int);
	void *map;
	int val;

	/*
	 * Make an anonymous fixed mapping at zero address. If the address
	 * is restricted as noted in security(7), the syscall should fail.
	 */
#ifdef __FreeBSD__
	if (sysctlbyname("security.bsd.map_at_zero", &val, &len, NULL, 0) != 0)
		atf_tc_fail("failed to read security.bsd.map_at_zero");
	val = !val; /* 1 == enable  map at zero */
#endif
#ifdef __NetBSD__
	if (sysctlbyname("vm.user_va0_disable", &val, &len, NULL, 0) != 0)
		atf_tc_fail("failed to read vm.user_va0_disable");
#endif

	map = mmap(NULL, page, PROT_EXEC, flags, -1, 0);
	map_check(map, val);

	map = mmap(NULL, page, PROT_READ, flags, -1, 0);
	map_check(map, val);

	map = mmap(NULL, page, PROT_WRITE, flags, -1, 0);
	map_check(map, val);

	map = mmap(NULL, page, PROT_READ|PROT_WRITE, flags, -1, 0);
	map_check(map, val);

	map = mmap(NULL, page, PROT_EXEC|PROT_READ|PROT_WRITE, flags, -1, 0);
	map_check(map, val);
}

ATF_TP_ADD_TCS(tp)
{
	page = sysconf(_SC_PAGESIZE);
	ATF_REQUIRE(page >= 0);

#ifdef __NetBSD__
	ATF_TP_ADD_TC(tp, mmap_block);
#endif
	ATF_TP_ADD_TC(tp, mmap_err);
	ATF_TP_ADD_TC(tp, mmap_loan);
	ATF_TP_ADD_TC(tp, mmap_prot_1);
	ATF_TP_ADD_TC(tp, mmap_prot_2);
	ATF_TP_ADD_TC(tp, mmap_prot_3);
	ATF_TP_ADD_TC(tp, mmap_truncate);
	ATF_TP_ADD_TC(tp, mmap_truncate_signal);
	ATF_TP_ADD_TC(tp, mmap_va0);

	return atf_no_error();
}
