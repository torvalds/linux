/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 */

#ifndef _ASM_SPARC_VDSO_GETTIMEOFDAY_H
#define _ASM_SPARC_VDSO_GETTIMEOFDAY_H

#include <uapi/linux/time.h>
#include <uapi/linux/unistd.h>

#include <vdso/align.h>
#include <vdso/clocksource.h>
#include <vdso/datapage.h>
#include <vdso/page.h>

#include <linux/types.h>

#ifdef	CONFIG_SPARC64
static __always_inline u64 vread_tick(void)
{
	u64	ret;

	__asm__ __volatile__("rd %%tick, %0" : "=r" (ret));
	return ret;
}

static __always_inline u64 vread_tick_stick(void)
{
	u64	ret;

	__asm__ __volatile__("rd %%asr24, %0" : "=r" (ret));
	return ret;
}
#else
static __always_inline u64 vdso_shift_ns(u64 val, u32 amt)
{
	u64 ret;

	__asm__ __volatile__("sllx %H1, 32, %%g1\n\t"
			     "srl %L1, 0, %L1\n\t"
			     "or %%g1, %L1, %%g1\n\t"
			     "srlx %%g1, %2, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret)
			     : "r" (val), "r" (amt)
			     : "g1");
	return ret;
}
#define vdso_shift_ns vdso_shift_ns

static __always_inline u64 vread_tick(void)
{
	register unsigned long long ret asm("o4");

	__asm__ __volatile__("rd %%tick, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret));
	return ret;
}

static __always_inline u64 vread_tick_stick(void)
{
	register unsigned long long ret asm("o4");

	__asm__ __volatile__("rd %%asr24, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret));
	return ret;
}
#endif

static __always_inline u64 __arch_get_hw_counter(s32 clock_mode, const struct vdso_time_data *vd)
{
	if (likely(clock_mode == VDSO_CLOCKMODE_STICK))
		return vread_tick_stick();
	else
		return vread_tick();
}

#ifdef	CONFIG_SPARC64
#define SYSCALL_STRING							\
	"ta	0x6d;"							\
	"bcs,a	1f;"							\
	" sub	%%g0, %%o0, %%o0;"					\
	"1:"
#else
#define SYSCALL_STRING							\
	"ta	0x10;"							\
	"bcs,a	1f;"							\
	" sub	%%g0, %%o0, %%o0;"					\
	"1:"
#endif

#define SYSCALL_CLOBBERS						\
	"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",			\
	"f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",		\
	"f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",		\
	"f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",		\
	"f32", "f34", "f36", "f38", "f40", "f42", "f44", "f46",		\
	"f48", "f50", "f52", "f54", "f56", "f58", "f60", "f62",		\
	"cc", "memory"

#ifdef CONFIG_SPARC64

static __always_inline
long clock_gettime_fallback(clockid_t clock, struct __kernel_timespec *ts)
{
	register long num __asm__("g1") = __NR_clock_gettime;
	register long o0 __asm__("o0") = clock;
	register long o1 __asm__("o1") = (long) ts;

	__asm__ __volatile__(SYSCALL_STRING : "=r" (o0) : "r" (num),
			     "0" (o0), "r" (o1) : SYSCALL_CLOBBERS);
	return o0;
}

#else /* !CONFIG_SPARC64 */

static __always_inline
long clock_gettime_fallback(clockid_t clock, struct __kernel_timespec *ts)
{
	register long num __asm__("g1") = __NR_clock_gettime64;
	register long o0 __asm__("o0") = clock;
	register long o1 __asm__("o1") = (long) ts;

	__asm__ __volatile__(SYSCALL_STRING : "=r" (o0) : "r" (num),
			     "0" (o0), "r" (o1) : SYSCALL_CLOBBERS);
	return o0;
}

static __always_inline
long clock_gettime32_fallback(clockid_t clock, struct old_timespec32 *ts)
{
	register long num __asm__("g1") = __NR_clock_gettime;
	register long o0 __asm__("o0") = clock;
	register long o1 __asm__("o1") = (long) ts;

	__asm__ __volatile__(SYSCALL_STRING : "=r" (o0) : "r" (num),
			     "0" (o0), "r" (o1) : SYSCALL_CLOBBERS);
	return o0;
}

#endif /* CONFIG_SPARC64 */

static __always_inline
long gettimeofday_fallback(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	register long num __asm__("g1") = __NR_gettimeofday;
	register long o0 __asm__("o0") = (long) tv;
	register long o1 __asm__("o1") = (long) tz;

	__asm__ __volatile__(SYSCALL_STRING : "=r" (o0) : "r" (num),
			     "0" (o0), "r" (o1) : SYSCALL_CLOBBERS);
	return o0;
}

static __always_inline const struct vdso_time_data *__arch_get_vdso_u_time_data(void)
{
	unsigned long ret;

	/*
	 * SPARC does not support native PC-relative code relocations.
	 * Calculate the address manually, works for 32 and 64 bit code.
	 */
	__asm__ __volatile__(
		"1:\n"
		"call 3f\n"                     // Jump over the embedded data and set up %o7
		"nop\n"                         // Delay slot
		"2:\n"
		".word vdso_u_time_data - .\n"  // Embedded offset to external symbol
		"3:\n"
		"add %%o7, 2b - 1b, %%o7\n"     // Point %o7 to the embedded offset
		"ldsw [%%o7], %0\n"             // Load the offset
		"add %0, %%o7, %0\n"            // Calculate the absolute address
		: "=r" (ret)
		:
		: "o7");

	return (const struct vdso_time_data *)ret;
}
#define __arch_get_vdso_u_time_data __arch_get_vdso_u_time_data

#endif /* _ASM_SPARC_VDSO_GETTIMEOFDAY_H */
