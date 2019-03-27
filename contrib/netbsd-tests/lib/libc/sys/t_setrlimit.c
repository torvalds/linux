/* $NetBSD: t_setrlimit.c,v 1.6 2017/01/13 21:16:38 christos Exp $ */

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
__RCSID("$NetBSD: t_setrlimit.c,v 1.6 2017/01/13 21:16:38 christos Exp $");

#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#ifdef __NetBSD__
#include <lwp.h>
#endif
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#ifdef __FreeBSD__
void set_vm_max_wired(int);
void restore_vm_max_wired(void);
#endif

static void		 sighandler(int);
static const char	 path[] = "setrlimit";

static const int rlimit[] = {
	RLIMIT_AS,
	RLIMIT_CORE,
	RLIMIT_CPU,
	RLIMIT_DATA,
	RLIMIT_FSIZE,
	RLIMIT_MEMLOCK,
	RLIMIT_NOFILE,
	RLIMIT_NPROC,
	RLIMIT_RSS,
	RLIMIT_SBSIZE,
	RLIMIT_STACK
};

ATF_TC(setrlimit_basic);
ATF_TC_HEAD(setrlimit_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic soft limit test");
}

ATF_TC_BODY(setrlimit_basic, tc)
{
	struct rlimit res;
	int *buf, lim;
	size_t i;

	buf = calloc(__arraycount(rlimit), sizeof(int));

	if (buf == NULL)
		atf_tc_fail("initialization failed");

	for (i = lim = 0; i < __arraycount(rlimit); i++) {

		(void)memset(&res, 0, sizeof(struct rlimit));

		if (getrlimit(rlimit[i], &res) != 0)
			continue;

		if (res.rlim_cur == RLIM_INFINITY || res.rlim_cur == 0)
			continue;

		if (res.rlim_cur == res.rlim_max) /* An unprivileged run. */
			continue;

		buf[i] = res.rlim_cur;
		res.rlim_cur = res.rlim_cur - 1;

		if (setrlimit(rlimit[i], &res) != 0) {
			lim = rlimit[i];
			goto out;
		}
	}

out:
	for (i = 0; i < __arraycount(rlimit); i++) {

		(void)memset(&res, 0, sizeof(struct rlimit));

		if (buf[i] == 0)
			continue;

		if (getrlimit(rlimit[i], &res) != 0)
			continue;

		res.rlim_cur = buf[i];

		(void)setrlimit(rlimit[i], &res);
	}

	if (lim != 0)
		atf_tc_fail("failed to set limit (%d)", lim);
	free(buf);
}

ATF_TC(setrlimit_current);
ATF_TC_HEAD(setrlimit_current, tc)
{
	atf_tc_set_md_var(tc, "descr", "setrlimit(3) with current limits");
}

ATF_TC_BODY(setrlimit_current, tc)
{
	struct rlimit res;
	size_t i;

	for (i = 0; i < __arraycount(rlimit); i++) {

		(void)memset(&res, 0, sizeof(struct rlimit));

		ATF_REQUIRE(getrlimit(rlimit[i], &res) == 0);
		ATF_REQUIRE(setrlimit(rlimit[i], &res) == 0);
	}
}

ATF_TC(setrlimit_err);
ATF_TC_HEAD(setrlimit_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions");
}

ATF_TC_BODY(setrlimit_err, tc)
{
	struct rlimit res;
	size_t i;

	for (i = 0; i < __arraycount(rlimit); i++) {

		errno = 0;

		ATF_REQUIRE(getrlimit(rlimit[i], (void *)0) != 0);
		ATF_REQUIRE(errno == EFAULT);
	}

	errno = 0;

	ATF_REQUIRE(getrlimit(INT_MAX, &res) != 0);
	ATF_REQUIRE(errno == EINVAL);
}

ATF_TC_WITH_CLEANUP(setrlimit_fsize);
ATF_TC_HEAD(setrlimit_fsize, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2), RLIMIT_FSIZE");
}

ATF_TC_BODY(setrlimit_fsize, tc)
{
	struct rlimit res;
	int fd, sta;
	pid_t pid;

	fd = open(path, O_RDWR | O_CREAT, 0700);

	if (fd < 0)
		atf_tc_fail("initialization failed");

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		res.rlim_cur = 2;
		res.rlim_max = 2;

		if (setrlimit(RLIMIT_FSIZE, &res) != 0)
			_exit(EXIT_FAILURE);

		if (signal(SIGXFSZ, sighandler) == SIG_ERR)
			_exit(EXIT_FAILURE);

		/*
		 * The third call should generate a SIGXFSZ.
		 */
		(void)write(fd, "X", 1);
		(void)write(fd, "X", 1);
		(void)write(fd, "X", 1);

		_exit(EXIT_FAILURE);
	}

	(void)close(fd);
	(void)wait(&sta);
	(void)unlink(path);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("RLIMIT_FSIZE not enforced");
}

ATF_TC_CLEANUP(setrlimit_fsize, tc)
{
	(void)unlink(path);
}

static void
sighandler(int signo)
{

	if (signo != SIGXFSZ)
		_exit(EXIT_FAILURE);

	_exit(EXIT_SUCCESS);
}

#ifdef __FreeBSD__
ATF_TC_WITH_CLEANUP(setrlimit_memlock);
#else
ATF_TC(setrlimit_memlock);
#endif
ATF_TC_HEAD(setrlimit_memlock, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2), RLIMIT_MEMLOCK");
#ifdef __FreeBSD__
	atf_tc_set_md_var(tc, "require.config", "allow_sysctl_side_effects");
	atf_tc_set_md_var(tc, "require.user", "root");
#endif
}

ATF_TC_BODY(setrlimit_memlock, tc)
{
	struct rlimit res;
	void *buf;
	long page;
	pid_t pid;
	int sta;

#ifdef __FreeBSD__
	/* Set max_wired really really high to avoid EAGAIN */
	set_vm_max_wired(INT_MAX);
#endif

	page = sysconf(_SC_PAGESIZE);
	ATF_REQUIRE(page >= 0);

	buf = malloc(page);
	pid = fork();

	if (buf == NULL || pid < 0)
		atf_tc_fail("initialization failed");

	if (pid == 0) {

		/*
		 * Try to lock a page while
		 * RLIMIT_MEMLOCK is zero.
		 */
		if (mlock(buf, page) != 0)
			_exit(EXIT_FAILURE);

		if (munlock(buf, page) != 0)
			_exit(EXIT_FAILURE);

		res.rlim_cur = 0;
		res.rlim_max = 0;

		if (setrlimit(RLIMIT_MEMLOCK, &res) != 0)
			_exit(EXIT_FAILURE);

		if (mlock(buf, page) != 0)
			_exit(EXIT_SUCCESS);

		(void)munlock(buf, page);

		_exit(EXIT_FAILURE);
	}

	free(buf);

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("RLIMIT_MEMLOCK not enforced");
}

#ifdef __FreeBSD__
ATF_TC_CLEANUP(setrlimit_memlock, tc)
{

	restore_vm_max_wired();
}
#endif

ATF_TC(setrlimit_nofile_1);
ATF_TC_HEAD(setrlimit_nofile_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2), RLIMIT_NOFILE, #1");
}

ATF_TC_BODY(setrlimit_nofile_1, tc)
{
	struct rlimit res;
	int fd, i, rv, sta;
	pid_t pid;

	res.rlim_cur = 0;
	res.rlim_max = 0;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * Close all descriptors, set RLIMIT_NOFILE
		 * to zero, and try to open a random file.
		 * This should fail with EMFILE.
		 */
		for (i = 0; i < 1024; i++)
			(void)close(i);

		rv = setrlimit(RLIMIT_NOFILE, &res);

		if (rv != 0)
			_exit(EXIT_FAILURE);

		errno = 0;
		fd = open("/etc/passwd", O_RDONLY);

		if (fd >= 0 || errno != EMFILE)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("RLIMIT_NOFILE not enforced");
}

ATF_TC(setrlimit_nofile_2);
ATF_TC_HEAD(setrlimit_nofile_2, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2), RLIMIT_NOFILE, #2");
}

ATF_TC_BODY(setrlimit_nofile_2, tc)
{
	static const rlim_t lim = 12;
	struct rlimit res;
	int fd, i, rv, sta;
	pid_t pid;

	/*
	 * See that an arbitrary limit on
	 * open files is being enforced.
	 */
	res.rlim_cur = lim;
	res.rlim_max = lim;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		for (i = 0; i < 1024; i++)
			(void)close(i);

		rv = setrlimit(RLIMIT_NOFILE, &res);

		if (rv != 0)
			_exit(EXIT_FAILURE);

		for (i = 0; i < (int)lim; i++) {

			fd = open("/etc/passwd", O_RDONLY);

			if (fd < 0)
				_exit(EXIT_FAILURE);
		}

		/*
		 * After the limit has been reached,
		 * EMFILE should again follow.
		 */
		fd = open("/etc/passwd", O_RDONLY);

		if (fd >= 0 || errno != EMFILE)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("RLIMIT_NOFILE not enforced");
}

ATF_TC(setrlimit_nproc);
ATF_TC_HEAD(setrlimit_nproc, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2), RLIMIT_NPROC");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(setrlimit_nproc, tc)
{
	struct rlimit res;
	pid_t pid, cpid;
	int sta;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * Set RLIMIT_NPROC to zero and try to fork.
		 */
		res.rlim_cur = 0;
		res.rlim_max = 0;

		if (setrlimit(RLIMIT_NPROC, &res) != 0)
			_exit(EXIT_FAILURE);

		cpid = fork();

		if (cpid < 0)
			_exit(EXIT_SUCCESS);

		_exit(EXIT_FAILURE);
	}

	(void)waitpid(pid, &sta, 0);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("RLIMIT_NPROC not enforced");
}

#ifdef __NetBSD__
ATF_TC(setrlimit_nthr);
ATF_TC_HEAD(setrlimit_nthr, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2), RLIMIT_NTHR");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

static void
func(lwpid_t *id)
{
	printf("thread %d\n", *id);
	fflush(stdout);
	_lwp_exit();
}

ATF_TC_BODY(setrlimit_nthr, tc)
{
	struct rlimit res;
	lwpid_t lwpid;
	ucontext_t c;

	/*
	 * Set RLIMIT_NTHR to zero and try to create a thread.
	 */
	res.rlim_cur = 0;
	res.rlim_max = 0;
	ATF_REQUIRE(setrlimit(RLIMIT_NTHR, &res) == 0);
	ATF_REQUIRE(getcontext(&c) == 0);
	c.uc_link = NULL;
	sigemptyset(&c.uc_sigmask);
	c.uc_stack.ss_flags = 0;
	c.uc_stack.ss_size = 4096;
	ATF_REQUIRE((c.uc_stack.ss_sp = malloc(c.uc_stack.ss_size)) != NULL);
	makecontext(&c, func, 1, &lwpid);
	ATF_CHECK_ERRNO(EAGAIN, _lwp_create(&c, 0, &lwpid) == -1);
}
#endif

ATF_TC(setrlimit_perm);
ATF_TC_HEAD(setrlimit_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2) for EPERM");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(setrlimit_perm, tc)
{
	struct rlimit res;
	size_t i;

	/*
	 * Try to raise the maximum limits as an user.
	 */
	for (i = 0; i < __arraycount(rlimit); i++) {

		ATF_REQUIRE(getrlimit(rlimit[i], &res) == 0);

#ifdef __FreeBSD__
		if (res.rlim_max == INT64_MAX) /* Overflow. */
#else
		if (res.rlim_max == UINT64_MAX) /* Overflow. */
#endif
			continue;

		errno = 0;
		res.rlim_max = res.rlim_max + 1;

		ATF_CHECK_ERRNO(EPERM, setrlimit(rlimit[i], &res) != 0);
	}
}

ATF_TC(setrlimit_stack);
ATF_TC_HEAD(setrlimit_stack, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setrlimit(2), RLIMIT_STACK");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(setrlimit_stack, tc)
{
	struct rlimit res;

	/* Ensure soft limit is not bigger than hard limit */
	res.rlim_cur = res.rlim_max = 4192256;
	ATF_REQUIRE(setrlimit(RLIMIT_STACK, &res) == 0);
	ATF_REQUIRE(getrlimit(RLIMIT_STACK, &res) == 0);
	ATF_CHECK(res.rlim_cur <= res.rlim_max);

}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, setrlimit_basic);
	ATF_TP_ADD_TC(tp, setrlimit_current);
	ATF_TP_ADD_TC(tp, setrlimit_err);
	ATF_TP_ADD_TC(tp, setrlimit_fsize);
	ATF_TP_ADD_TC(tp, setrlimit_memlock);
	ATF_TP_ADD_TC(tp, setrlimit_nofile_1);
	ATF_TP_ADD_TC(tp, setrlimit_nofile_2);
	ATF_TP_ADD_TC(tp, setrlimit_nproc);
	ATF_TP_ADD_TC(tp, setrlimit_perm);
#ifdef __NetBSD__
	ATF_TP_ADD_TC(tp, setrlimit_nthr);
#endif
	ATF_TP_ADD_TC(tp, setrlimit_stack);

	return atf_no_error();
}
