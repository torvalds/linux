// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARM userspace implementations of gettimeofday() and similar.
 *
 * Copyright 2015 Mentor Graphics Corporation.
 */
#include <linux/time.h>
#include <linux/types.h>
#include <asm/vdso.h>
#include <asm/unwind.h>
#include <vdso/gettime.h>

int __vdso_clock_gettime(clockid_t clock,
			 struct old_timespec32 *ts)
{
	return __cvdso_clock_gettime32(clock, ts);
}

int __vdso_clock_gettime64(clockid_t clock,
			   struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

int __vdso_gettimeofday(struct __kernel_old_timeval *tv,
			struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

int __vdso_clock_getres(clockid_t clock_id,
			struct old_timespec32 *res)
{
	return __cvdso_clock_getres_time32(clock_id, res);
}

/* Avoid unresolved references emitted by GCC */

void __aeabi_unwind_cpp_pr0(void)
{
}

void __aeabi_unwind_cpp_pr1(void)
{
}

void __aeabi_unwind_cpp_pr2(void)
{
}
