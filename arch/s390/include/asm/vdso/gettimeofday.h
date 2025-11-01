/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ASM_VDSO_GETTIMEOFDAY_H
#define ASM_VDSO_GETTIMEOFDAY_H

#define VDSO_HAS_TIME 1

#define VDSO_HAS_CLOCK_GETRES 1

#define VDSO_DELTA_NOMASK 1

#include <asm/syscall.h>
#include <asm/timex.h>
#include <asm/unistd.h>
#include <linux/compiler.h>


static inline u64 __arch_get_hw_counter(s32 clock_mode, const struct vdso_time_data *vd)
{
	return get_tod_clock() - vd->arch_data.tod_delta;
}

static __always_inline
long clock_gettime_fallback(clockid_t clkid, struct __kernel_timespec *ts)
{
	return syscall2(__NR_clock_gettime, (long)clkid, (long)ts);
}

static __always_inline
long gettimeofday_fallback(register struct __kernel_old_timeval *tv,
			   register struct timezone *tz)
{
	return syscall2(__NR_gettimeofday, (long)tv, (long)tz);
}

static __always_inline
long clock_getres_fallback(clockid_t clkid, struct __kernel_timespec *ts)
{
	return syscall2(__NR_clock_getres, (long)clkid, (long)ts);
}

#endif
