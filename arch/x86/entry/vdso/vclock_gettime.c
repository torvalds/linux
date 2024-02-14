// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fast user context implementation of clock_gettime, gettimeofday, and time.
 *
 * Copyright 2006 Andi Kleen, SUSE Labs.
 * Copyright 2019 ARM Limited
 *
 * 32 Bit compat layer by Stefani Seibold <stefani@seibold.net>
 *  sponsored by Rohde & Schwarz GmbH & Co. KG Munich/Germany
 */
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "../../../../lib/vdso/gettimeofday.c"

extern int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz);
extern __kernel_old_time_t __vdso_time(__kernel_old_time_t *t);

int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

int gettimeofday(struct __kernel_old_timeval *, struct timezone *)
	__attribute__((weak, alias("__vdso_gettimeofday")));

__kernel_old_time_t __vdso_time(__kernel_old_time_t *t)
{
	return __cvdso_time(t);
}

__kernel_old_time_t time(__kernel_old_time_t *t)	__attribute__((weak, alias("__vdso_time")));


#if defined(CONFIG_X86_64) && !defined(BUILD_VDSO32_64)
/* both 64-bit and x32 use these */
extern int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts);
extern int __vdso_clock_getres(clockid_t clock, struct __kernel_timespec *res);

int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

int clock_gettime(clockid_t, struct __kernel_timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

int __vdso_clock_getres(clockid_t clock,
			struct __kernel_timespec *res)
{
	return __cvdso_clock_getres(clock, res);
}
int clock_getres(clockid_t, struct __kernel_timespec *)
	__attribute__((weak, alias("__vdso_clock_getres")));

#else
/* i386 only */
extern int __vdso_clock_gettime(clockid_t clock, struct old_timespec32 *ts);
extern int __vdso_clock_getres(clockid_t clock, struct old_timespec32 *res);

int __vdso_clock_gettime(clockid_t clock, struct old_timespec32 *ts)
{
	return __cvdso_clock_gettime32(clock, ts);
}

int clock_gettime(clockid_t, struct old_timespec32 *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

int __vdso_clock_gettime64(clockid_t clock, struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

int clock_gettime64(clockid_t, struct __kernel_timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime64")));

int __vdso_clock_getres(clockid_t clock, struct old_timespec32 *res)
{
	return __cvdso_clock_getres_time32(clock, res);
}

int clock_getres(clockid_t, struct old_timespec32 *)
	__attribute__((weak, alias("__vdso_clock_getres")));
#endif
