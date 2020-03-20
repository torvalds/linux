/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 ARM Limited
 */
#ifndef __ASM_VDSO_GETTIMEOFDAY_H
#define __ASM_VDSO_GETTIMEOFDAY_H

#ifndef __ASSEMBLY__

#include <asm/unistd.h>
#include <uapi/linux/time.h>

#include <asm/vdso/compat_barrier.h>

#define VDSO_HAS_CLOCK_GETRES		1

#define BUILD_VDSO32			1

static __always_inline
int gettimeofday_fallback(struct __kernel_old_timeval *_tv,
			  struct timezone *_tz)
{
	register struct timezone *tz asm("r1") = _tz;
	register struct __kernel_old_timeval *tv asm("r0") = _tv;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_compat_gettimeofday;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (tv), "r" (tz), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline
long clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	register struct __kernel_timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_compat_clock_gettime64;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline
long clock_gettime32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	register struct old_timespec32 *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_compat_clock_gettime;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline
int clock_getres_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	register struct __kernel_timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_compat_clock_getres_time64;

	asm volatile(
	"       swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline
int clock_getres32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	register struct old_timespec32 *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_compat_clock_getres;

	asm volatile(
	"       swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static __always_inline u64 __arch_get_hw_counter(s32 clock_mode)
{
	u64 res;

	/*
	 * Core checks for mode already, so this raced against a concurrent
	 * update. Return something. Core will do another round and then
	 * see the mode change and fallback to the syscall.
	 */
	if (clock_mode == VDSO_CLOCKMODE_NONE)
		return 0;

	/*
	 * This isb() is required to prevent that the counter value
	 * is speculated.
	 */
	isb();
	asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (res));
	/*
	 * This isb() is required to prevent that the seq lock is
	 * speculated.
	 */
	isb();

	return res;
}

static __always_inline const struct vdso_data *__arch_get_vdso_data(void)
{
	const struct vdso_data *ret;

	/*
	 * This simply puts &_vdso_data into ret. The reason why we don't use
	 * `ret = _vdso_data` is that the compiler tends to optimise this in a
	 * very suboptimal way: instead of keeping &_vdso_data in a register,
	 * it goes through a relocation almost every time _vdso_data must be
	 * accessed (even in subfunctions). This is both time and space
	 * consuming: each relocation uses a word in the code section, and it
	 * has to be loaded at runtime.
	 *
	 * This trick hides the assignment from the compiler. Since it cannot
	 * track where the pointer comes from, it will only use one relocation
	 * where __arch_get_vdso_data() is called, and then keep the result in
	 * a register.
	 */
	asm volatile("mov %0, %1" : "=r"(ret) : "r"(_vdso_data));

	return ret;
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETTIMEOFDAY_H */
