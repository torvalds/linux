/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_VDSO_GETTIMEOFDAY_H
#define __ASM_VDSO_GETTIMEOFDAY_H

#ifndef __ASSEMBLER__

#include <asm/unistd.h>
#include <asm/vdso/vdso.h>

#define VDSO_HAS_CLOCK_GETRES		1

static __always_inline long gettimeofday_fallback(
				struct __kernel_old_timeval *_tv,
				struct timezone *_tz)
{
	register struct __kernel_old_timeval *tv asm("a0") = _tv;
	register struct timezone *tz asm("a1") = _tz;
	register long nr asm("a7") = __NR_gettimeofday;
	register long ret asm("a0");

	asm volatile(
	"       syscall 0\n"
	: "=r" (ret)
	: "r" (nr), "r" (tv), "r" (tz)
	: "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
	  "$t8", "memory");

	return ret;
}

static __always_inline long clock_gettime_fallback(
					clockid_t _clkid,
					struct __kernel_timespec *_ts)
{
	register clockid_t clkid asm("a0") = _clkid;
	register struct __kernel_timespec *ts asm("a1") = _ts;
	register long nr asm("a7") = __NR_clock_gettime;
	register long ret asm("a0");

	asm volatile(
	"       syscall 0\n"
	: "=r" (ret)
	: "r" (nr), "r" (clkid), "r" (ts)
	: "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
	  "$t8", "memory");

	return ret;
}

static __always_inline int clock_getres_fallback(
					clockid_t _clkid,
					struct __kernel_timespec *_ts)
{
	register clockid_t clkid asm("a0") = _clkid;
	register struct __kernel_timespec *ts asm("a1") = _ts;
	register long nr asm("a7") = __NR_clock_getres;
	register long ret asm("a0");

	asm volatile(
	"       syscall 0\n"
	: "=r" (ret)
	: "r" (nr), "r" (clkid), "r" (ts)
	: "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
	  "$t8", "memory");

	return ret;
}

static __always_inline u64 __arch_get_hw_counter(s32 clock_mode,
						 const struct vdso_time_data *vd)
{
	uint64_t count;

	__asm__ __volatile__(
	"	rdtime.d %0, $zero\n"
	: "=r" (count));

	return count;
}

static inline bool loongarch_vdso_hres_capable(void)
{
	return true;
}
#define __arch_vdso_hres_capable loongarch_vdso_hres_capable

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_VDSO_GETTIMEOFDAY_H */
