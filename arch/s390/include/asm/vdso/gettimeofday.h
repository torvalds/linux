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


static __always_inline const struct vdso_data *__arch_get_vdso_data(void)
{
	return _vdso_data;
}

static inline u64 __arch_get_hw_counter(s32 clock_mode, const struct vdso_data *vd)
{
	u64 adj, now;

	now = get_tod_clock();
	adj = vd->arch_data.tod_steering_end - now;
	if (unlikely((s64) adj > 0))
		now += (vd->arch_data.tod_steering_delta < 0) ? (adj >> 15) : -(adj >> 15);
	return now;
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

#ifdef CONFIG_TIME_NS
static __always_inline
const struct vdso_data *__arch_get_timens_vdso_data(const struct vdso_data *vd)
{
	return _timens_data;
}
#endif

#endif
