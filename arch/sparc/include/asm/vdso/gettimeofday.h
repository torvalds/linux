/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 */

#ifndef _ASM_SPARC_VDSO_GETTIMEOFDAY_H
#define _ASM_SPARC_VDSO_GETTIMEOFDAY_H

#include <uapi/linux/time.h>
#include <uapi/linux/unistd.h>

#include <linux/types.h>
#include <asm/vvar.h>

#ifdef	CONFIG_SPARC64
static __always_inline u64 vdso_shift_ns(u64 val, u32 amt)
{
	return val >> amt;
}

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

static __always_inline u64 __arch_get_hw_counter(struct vvar_data *vvar)
{
	if (likely(vvar->vclock_mode == VCLOCK_STICK))
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

static __always_inline
long clock_gettime_fallback(clockid_t clock, struct __kernel_old_timespec *ts)
{
	register long num __asm__("g1") = __NR_clock_gettime;
	register long o0 __asm__("o0") = clock;
	register long o1 __asm__("o1") = (long) ts;

	__asm__ __volatile__(SYSCALL_STRING : "=r" (o0) : "r" (num),
			     "0" (o0), "r" (o1) : SYSCALL_CLOBBERS);
	return o0;
}

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

#endif /* _ASM_SPARC_VDSO_GETTIMEOFDAY_H */
