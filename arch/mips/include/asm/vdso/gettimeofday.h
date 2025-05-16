/*
 * Copyright (C) 2018 ARM Limited
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __ASM_VDSO_GETTIMEOFDAY_H
#define __ASM_VDSO_GETTIMEOFDAY_H

#ifndef __ASSEMBLY__

#include <asm/vdso/vdso.h>
#include <asm/clocksource.h>
#include <asm/unistd.h>
#include <asm/vdso.h>

#define VDSO_HAS_CLOCK_GETRES		1

#if MIPS_ISA_REV < 6
#define VDSO_SYSCALL_CLOBBERS "hi", "lo",
#else
#define VDSO_SYSCALL_CLOBBERS
#endif

static __always_inline long gettimeofday_fallback(
				struct __kernel_old_timeval *_tv,
				struct timezone *_tz)
{
	register struct timezone *tz asm("a1") = _tz;
	register struct __kernel_old_timeval *tv asm("a0") = _tv;
	register long ret asm("v0");
	register long nr asm("v0") = __NR_gettimeofday;
	register long error asm("a3");

	asm volatile(
	"       syscall\n"
	: "=r" (ret), "=r" (error)
	: "r" (tv), "r" (tz), "r" (nr)
	: "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13",
	  "$14", "$15", "$24", "$25",
	  VDSO_SYSCALL_CLOBBERS
	  "memory");

	return error ? -ret : ret;
}

static __always_inline long clock_gettime_fallback(
					clockid_t _clkid,
					struct __kernel_timespec *_ts)
{
	register struct __kernel_timespec *ts asm("a1") = _ts;
	register clockid_t clkid asm("a0") = _clkid;
	register long ret asm("v0");
#if _MIPS_SIM == _MIPS_SIM_ABI64
	register long nr asm("v0") = __NR_clock_gettime;
#else
	register long nr asm("v0") = __NR_clock_gettime64;
#endif
	register long error asm("a3");

	asm volatile(
	"       syscall\n"
	: "=r" (ret), "=r" (error)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13",
	  "$14", "$15", "$24", "$25",
	  VDSO_SYSCALL_CLOBBERS
	  "memory");

	return error ? -ret : ret;
}

static __always_inline int clock_getres_fallback(
					clockid_t _clkid,
					struct __kernel_timespec *_ts)
{
	register struct __kernel_timespec *ts asm("a1") = _ts;
	register clockid_t clkid asm("a0") = _clkid;
	register long ret asm("v0");
#if _MIPS_SIM == _MIPS_SIM_ABI64
	register long nr asm("v0") = __NR_clock_getres;
#else
	register long nr asm("v0") = __NR_clock_getres_time64;
#endif
	register long error asm("a3");

	asm volatile(
	"       syscall\n"
	: "=r" (ret), "=r" (error)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13",
	  "$14", "$15", "$24", "$25",
	  VDSO_SYSCALL_CLOBBERS
	  "memory");

	return error ? -ret : ret;
}

#if _MIPS_SIM != _MIPS_SIM_ABI64

static __always_inline long clock_gettime32_fallback(
					clockid_t _clkid,
					struct old_timespec32 *_ts)
{
	register struct old_timespec32 *ts asm("a1") = _ts;
	register clockid_t clkid asm("a0") = _clkid;
	register long ret asm("v0");
	register long nr asm("v0") = __NR_clock_gettime;
	register long error asm("a3");

	asm volatile(
	"       syscall\n"
	: "=r" (ret), "=r" (error)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13",
	  "$14", "$15", "$24", "$25",
	  VDSO_SYSCALL_CLOBBERS
	  "memory");

	return error ? -ret : ret;
}

static __always_inline int clock_getres32_fallback(
					clockid_t _clkid,
					struct old_timespec32 *_ts)
{
	register struct old_timespec32 *ts asm("a1") = _ts;
	register clockid_t clkid asm("a0") = _clkid;
	register long ret asm("v0");
	register long nr asm("v0") = __NR_clock_getres;
	register long error asm("a3");

	asm volatile(
	"       syscall\n"
	: "=r" (ret), "=r" (error)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "$1", "$3", "$8", "$9", "$10", "$11", "$12", "$13",
	  "$14", "$15", "$24", "$25",
	  VDSO_SYSCALL_CLOBBERS
	  "memory");

	return error ? -ret : ret;
}
#endif

#ifdef CONFIG_CSRC_R4K

static __always_inline u64 read_r4k_count(void)
{
	unsigned int count;

	__asm__ __volatile__(
	"	.set push\n"
	"	.set mips32r2\n"
	"	rdhwr	%0, $2\n"
	"	.set pop\n"
	: "=r" (count));

	return count;
}

#endif

#ifdef CONFIG_CLKSRC_MIPS_GIC

static __always_inline u64 read_gic_count(const struct vdso_time_data *data)
{
	void __iomem *gic = get_gic(data);
	u32 hi, hi2, lo;

	do {
		hi = __raw_readl(gic + sizeof(lo));
		lo = __raw_readl(gic);
		hi2 = __raw_readl(gic + sizeof(lo));
	} while (hi2 != hi);

	return (((u64)hi) << 32) + lo;
}

#endif

static __always_inline u64 __arch_get_hw_counter(s32 clock_mode,
						 const struct vdso_time_data *vd)
{
#ifdef CONFIG_CSRC_R4K
	if (clock_mode == VDSO_CLOCKMODE_R4K)
		return read_r4k_count();
#endif
#ifdef CONFIG_CLKSRC_MIPS_GIC
	if (clock_mode == VDSO_CLOCKMODE_GIC)
		return read_gic_count(vd);
#endif
	/*
	 * Core checks mode already. So this raced against a concurrent
	 * update. Return something. Core will do another round see the
	 * change and fallback to syscall.
	 */
	return 0;
}

static inline bool mips_vdso_hres_capable(void)
{
	return IS_ENABLED(CONFIG_CSRC_R4K) ||
	       IS_ENABLED(CONFIG_CLKSRC_MIPS_GIC);
}
#define __arch_vdso_hres_capable mips_vdso_hres_capable

static __always_inline const struct vdso_time_data *__arch_get_vdso_u_time_data(void)
{
	return get_vdso_time_data();
}
#define __arch_get_vdso_u_time_data __arch_get_vdso_u_time_data

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETTIMEOFDAY_H */
