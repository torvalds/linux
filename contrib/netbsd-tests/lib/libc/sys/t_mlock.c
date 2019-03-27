/* $NetBSD: t_mlock.c,v 1.6 2016/08/09 12:02:44 kre Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_mlock.c,v 1.6 2016/08/09 12:02:44 kre Exp $");

#ifdef __FreeBSD__
#include <sys/param.h> /* NetBSD requires sys/param.h for sysctl(3), unlike FreeBSD */
#endif
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <errno.h>
#include <atf-c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <limits.h>
#define _KMEMUSER
#include <machine/vmparam.h>

void set_vm_max_wired(int);
void restore_vm_max_wired(void);
#endif

static long page = 0;

ATF_TC(mlock_clip);
ATF_TC_HEAD(mlock_clip, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test with mlock(2) that UVM only "
	    "clips if the clip address is within the entry (PR kern/44788)");
}

ATF_TC_BODY(mlock_clip, tc)
{
	void *buf;

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);

	if (page < 1024)
		atf_tc_skip("page size too small");

	for (size_t i = page; i >= 1; i = i - 1024) {
		(void)mlock(buf, page - i);
		(void)munlock(buf, page - i);
	}

	free(buf);
}

#ifdef __FreeBSD__
ATF_TC_WITH_CLEANUP(mlock_err);
#else
ATF_TC(mlock_err);
#endif
ATF_TC_HEAD(mlock_err, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test error conditions in mlock(2) and munlock(2)");
#ifdef __FreeBSD__
	atf_tc_set_md_var(tc, "require.config", "allow_sysctl_side_effects");
	atf_tc_set_md_var(tc, "require.user", "root");
#endif
}

ATF_TC_BODY(mlock_err, tc)
{
#ifdef __NetBSD__
	unsigned long vmin = 0;
	size_t len = sizeof(vmin);
#endif
#if !defined(__aarch64__) && !defined(__riscv)
	void *invalid_ptr;
#endif
	int null_errno = ENOMEM;	/* error expected for NULL */
	void *buf;

#ifdef __FreeBSD__
#ifdef VM_MIN_ADDRESS
	if ((uintptr_t)VM_MIN_ADDRESS > 0)
		null_errno = EINVAL;	/* NULL is not inside user VM */
#endif
	/* Set max_wired really really high to avoid EAGAIN */
	set_vm_max_wired(INT_MAX);
#else
	if (sysctlbyname("vm.minaddress", &vmin, &len, NULL, 0) != 0)
		atf_tc_fail("failed to read vm.minaddress");
	/*
	 * Any bad address must return ENOMEM (for lock & unlock)
	 */
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock(NULL, page) == -1);

	if (vmin > 0)
		null_errno = EINVAL;	/* NULL is not inside user VM */
#endif

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock((char *)0, page) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock(NULL, page) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock((char *)0, page) == -1);

#ifdef __FreeBSD__
	/* Wrap around should return EINVAL */
	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, mlock((char *)-1, page) == -1);
	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, munlock((char *)-1, page) == -1);
#else
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock((char *)-1, page) == -1);
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock((char *)-1, page) == -1);
#endif

	buf = malloc(page);	/* Get a valid address */
	ATF_REQUIRE(buf != NULL);
#ifdef __FreeBSD__
	errno = 0;
	/* Wrap around should return EINVAL */
	ATF_REQUIRE_ERRNO(EINVAL, mlock(buf, -page) == -1);
	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, munlock(buf, -page) == -1);
#else
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock(buf, -page) == -1);
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock(buf, -page) == -1);
#endif
	(void)free(buf);

/* There is no sbrk on AArch64 and RISC-V */
#if !defined(__aarch64__) && !defined(__riscv)
	/*
	 * Try to create a pointer to an unmapped page - first after current
	 * brk will likely do.
	 */
	invalid_ptr = (void*)(((uintptr_t)sbrk(0)+page) & ~(page-1));
	printf("testing with (hopefully) invalid pointer %p\n", invalid_ptr);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, mlock(invalid_ptr, page) == -1);

	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock(invalid_ptr, page) == -1);
#endif
}

#ifdef __FreeBSD__
ATF_TC_CLEANUP(mlock_err, tc)
{

	restore_vm_max_wired();
}
#endif

ATF_TC(mlock_limits);
ATF_TC_HEAD(mlock_limits, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test system limits with mlock(2)");
}

ATF_TC_BODY(mlock_limits, tc)
{
	struct rlimit res;
	void *buf;
	pid_t pid;
	int sta;

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		for (ssize_t i = page; i >= 2; i -= 100) {

			res.rlim_cur = i - 1;
			res.rlim_max = i - 1;

			(void)fprintf(stderr, "trying to lock %zd bytes "
			    "with %zu byte limit\n", i, (size_t)res.rlim_cur);

			if (setrlimit(RLIMIT_MEMLOCK, &res) != 0)
				_exit(EXIT_FAILURE);

			errno = 0;

#ifdef __FreeBSD__
			/*
			 * NetBSD doesn't conform to POSIX with ENOMEM requirement;
			 * FreeBSD does.
			 *
			 * See: NetBSD PR # kern/48962 for more details.
			 */
			if (mlock(buf, i) != -1 || errno != ENOMEM) {
#else
			if (mlock(buf, i) != -1 || errno != EAGAIN) {
#endif
				(void)munlock(buf, i);
				_exit(EXIT_FAILURE);
			}
		}

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("mlock(2) locked beyond system limits");

	free(buf);
}

#ifdef __FreeBSD__
ATF_TC_WITH_CLEANUP(mlock_mmap);
#else
ATF_TC(mlock_mmap);
#endif
ATF_TC_HEAD(mlock_mmap, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test mlock(2)-mmap(2) interaction");
#ifdef __FreeBSD__
	atf_tc_set_md_var(tc, "require.config", "allow_sysctl_side_effects");
	atf_tc_set_md_var(tc, "require.user", "root");
#endif
}

ATF_TC_BODY(mlock_mmap, tc)
{
#ifdef __NetBSD__
	static const int flags = MAP_ANON | MAP_PRIVATE | MAP_WIRED;
#else
	static const int flags = MAP_ANON | MAP_PRIVATE;
#endif
	void *buf;

#ifdef __FreeBSD__
	/* Set max_wired really really high to avoid EAGAIN */
	set_vm_max_wired(INT_MAX);
#endif

	/*
	 * Make a wired RW mapping and check that mlock(2)
	 * does not fail for the (already locked) mapping.
	 */
	buf = mmap(NULL, page, PROT_READ | PROT_WRITE, flags, -1, 0);

	ATF_REQUIRE(buf != MAP_FAILED);
#ifdef __FreeBSD__
	/*
	 * The duplicate mlock call is added to ensure that the call works
	 * as described above without MAP_WIRED support.
	 */
	ATF_REQUIRE(mlock(buf, page) == 0);
#endif
	ATF_REQUIRE(mlock(buf, page) == 0);
	ATF_REQUIRE(munlock(buf, page) == 0);
	ATF_REQUIRE(munmap(buf, page) == 0);
	ATF_REQUIRE(munlock(buf, page) != 0);

	/*
	 * But it should be impossible to mlock(2) a PROT_NONE mapping.
	 */
	buf = mmap(NULL, page, PROT_NONE, flags, -1, 0);

	ATF_REQUIRE(buf != MAP_FAILED);
#ifdef __FreeBSD__
	ATF_REQUIRE_ERRNO(ENOMEM, mlock(buf, page) != 0);
#else
	ATF_REQUIRE(mlock(buf, page) != 0);
#endif
	ATF_REQUIRE(munmap(buf, page) == 0);
}

#ifdef __FreeBSD__
ATF_TC_CLEANUP(mlock_mmap, tc)
{

	restore_vm_max_wired();
}
#endif

#ifdef __FreeBSD__
ATF_TC_WITH_CLEANUP(mlock_nested);
#else
ATF_TC(mlock_nested);
#endif
ATF_TC_HEAD(mlock_nested, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that consecutive mlock(2) calls succeed");
#ifdef __FreeBSD__
	atf_tc_set_md_var(tc, "require.config", "allow_sysctl_side_effects");
	atf_tc_set_md_var(tc, "require.user", "root");
#endif
}

ATF_TC_BODY(mlock_nested, tc)
{
	const size_t maxiter = 100;
	void *buf;

#ifdef __FreeBSD__
	/* Set max_wired really really high to avoid EAGAIN */
	set_vm_max_wired(INT_MAX);
#endif

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);

	for (size_t i = 0; i < maxiter; i++)
		ATF_REQUIRE(mlock(buf, page) == 0);

	ATF_REQUIRE(munlock(buf, page) == 0);
	free(buf);
}

#ifdef __FreeBSD__
ATF_TC_CLEANUP(mlock_nested, tc)
{

	restore_vm_max_wired();
}
#endif

#ifdef __FreeBSD__
ATF_TC_WITH_CLEANUP(mlock_unaligned);
#else
ATF_TC(mlock_unaligned);
#endif
ATF_TC_HEAD(mlock_unaligned, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that mlock(2) can lock page-unaligned memory");
#ifdef __FreeBSD__
	atf_tc_set_md_var(tc, "require.config", "allow_sysctl_side_effects");
	atf_tc_set_md_var(tc, "require.user", "root");
#endif
}

ATF_TC_BODY(mlock_unaligned, tc)
{
	void *buf, *addr;

#ifdef __FreeBSD__
	/* Set max_wired really really high to avoid EAGAIN */
	set_vm_max_wired(INT_MAX);
#endif

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);

	if ((uintptr_t)buf & ((uintptr_t)page - 1))
		addr = buf;
	else
		addr = (void *)(((uintptr_t)buf) + page/3);

	ATF_REQUIRE_EQ(mlock(addr, page/5), 0);
	ATF_REQUIRE_EQ(munlock(addr, page/5), 0);

	(void)free(buf);
}

#ifdef __FreeBSD__
ATF_TC_CLEANUP(mlock_unaligned, tc)
{

	restore_vm_max_wired();
}
#endif

ATF_TC(munlock_unlocked);
ATF_TC_HEAD(munlock_unlocked, tc)
{
	atf_tc_set_md_var(tc, "descr",
#ifdef __FreeBSD__
	    "munlock(2) accepts unlocked memory");
#else
	    "munlock(2) of unlocked memory is an error");
#endif
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(munlock_unlocked, tc)
{
	void *buf;

	buf = malloc(page);
	ATF_REQUIRE(buf != NULL);

#ifdef __FreeBSD__
	ATF_REQUIRE_EQ(munlock(buf, page), 0);
#else
	errno = 0;
	ATF_REQUIRE_ERRNO(ENOMEM, munlock(buf, page) == -1);
#endif
	(void)free(buf);
}

ATF_TP_ADD_TCS(tp)
{

	page = sysconf(_SC_PAGESIZE);
	ATF_REQUIRE(page >= 0);

	ATF_TP_ADD_TC(tp, mlock_clip);
	ATF_TP_ADD_TC(tp, mlock_err);
	ATF_TP_ADD_TC(tp, mlock_limits);
	ATF_TP_ADD_TC(tp, mlock_mmap);
	ATF_TP_ADD_TC(tp, mlock_nested);
	ATF_TP_ADD_TC(tp, mlock_unaligned);
	ATF_TP_ADD_TC(tp, munlock_unlocked);

	return atf_no_error();
}
