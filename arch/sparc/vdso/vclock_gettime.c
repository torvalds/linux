// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 *
 * Fast user context implementation of clock_gettime, gettimeofday, and time.
 *
 * The code should have no internal unresolved relocations.
 * Check with readelf after changing.
 * Also alternative() doesn't work.
 */
/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#include <linux/compiler.h>
#include <linux/types.h>

#include <vdso/gettime.h>

#include <asm/vdso/gettimeofday.h>

#include "../../../../lib/vdso/gettimeofday.c"

int __vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	return __cvdso_gettimeofday(tv, tz);
}

int gettimeofday(struct __kernel_old_timeval *, struct timezone *)
	__weak __alias(__vdso_gettimeofday);

#if defined(CONFIG_SPARC64)
int __vdso_clock_gettime(clockid_t clock, struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

int clock_gettime(clockid_t, struct __kernel_timespec *)
	__weak __alias(__vdso_clock_gettime);

#else

int __vdso_clock_gettime(clockid_t clock, struct old_timespec32 *ts)
{
	return __cvdso_clock_gettime32(clock, ts);
}

int clock_gettime(clockid_t, struct old_timespec32 *)
	__weak __alias(__vdso_clock_gettime);

int __vdso_clock_gettime64(clockid_t clock, struct __kernel_timespec *ts)
{
	return __cvdso_clock_gettime(clock, ts);
}

int clock_gettime64(clockid_t, struct __kernel_timespec *)
	__weak __alias(__vdso_clock_gettime64);

#endif
