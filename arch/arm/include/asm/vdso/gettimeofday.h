/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 ARM Limited
 */
#ifndef __ASM_VDSO_GETTIMEOFDAY_H
#define __ASM_VDSO_GETTIMEOFDAY_H

#ifndef __ASSEMBLY__

#include <asm/barrier.h>
#include <asm/cp15.h>
#include <asm/unistd.h>
#include <uapi/linux/time.h>

#define VDSO_HAS_CLOCK_GETRES		1

extern struct vdso_data *__get_datapage(void);

static __always_inline int gettimeofday_fallback(
				struct __kernel_old_timeval *_tv,
				struct timezone *_tz)
{
	register struct timezone *tz asm("r1") = _tz;
	register struct __kernel_old_timeval *tv asm("r0") = _tv;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_gettimeofday;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (tv), "r" (tz), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline long clock_gettime_fallback(
					clockid_t _clkid,
					struct __kernel_timespec *_ts)
{
	register struct __kernel_timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_clock_gettime64;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline int clock_getres_fallback(
					clockid_t _clkid,
					struct __kernel_timespec *_ts)
{
	register struct __kernel_timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_clock_getres_time64;

	asm volatile(
	"       swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline u64 __arch_get_hw_counter(int clock_mode)
{
#ifdef CONFIG_ARM_ARCH_TIMER
	u64 cycle_now;

	if (!clock_mode)
		return -EINVAL;

	isb();
	cycle_now = read_sysreg(CNTVCT);

	return cycle_now;
#else
	return -EINVAL; /* use fallback */
#endif
}

static __always_inline const struct vdso_data *__arch_get_vdso_data(void)
{
	return __get_datapage();
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETTIMEOFDAY_H */
