/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_VDSO_GETTIMEOFDAY_H
#define _ASM_POWERPC_VDSO_GETTIMEOFDAY_H

#include <asm/page.h>

#ifdef __ASSEMBLY__

#include <asm/ppc_asm.h>

/*
 * The macros sets two stack frames, one for the caller and one for the callee
 * because there are no requirement for the caller to set a stack frame when
 * calling VDSO so it may have omitted to set one, especially on PPC64
 */

.macro cvdso_call funct
  .cfi_startproc
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
	mflr		r0
  .cfi_register lr, r0
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
	PPC_STL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
#ifdef __powerpc64__
	PPC_STL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
#endif
	get_datapage	r5
	addi		r5, r5, VDSO_DATA_OFFSET
	bl		DOTSYM(\funct)
	PPC_LL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
#ifdef __powerpc64__
	PPC_LL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
#endif
	cmpwi		r3, 0
	mtlr		r0
  .cfi_restore lr
	addi		r1, r1, 2 * PPC_MIN_STKFRM
	crclr		so
	beqlr+
	crset		so
	neg		r3, r3
	blr
  .cfi_endproc
.endm

.macro cvdso_call_time funct
  .cfi_startproc
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
	mflr		r0
  .cfi_register lr, r0
	PPC_STLU	r1, -PPC_MIN_STKFRM(r1)
	PPC_STL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
#ifdef __powerpc64__
	PPC_STL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
#endif
	get_datapage	r4
	addi		r4, r4, VDSO_DATA_OFFSET
	bl		DOTSYM(\funct)
	PPC_LL		r0, PPC_MIN_STKFRM + PPC_LR_STKOFF(r1)
#ifdef __powerpc64__
	PPC_LL		r2, PPC_MIN_STKFRM + STK_GOT(r1)
#endif
	crclr		so
	mtlr		r0
  .cfi_restore lr
	addi		r1, r1, 2 * PPC_MIN_STKFRM
	blr
  .cfi_endproc
.endm

#else

#include <asm/vdso/timebase.h>
#include <asm/barrier.h>
#include <asm/unistd.h>
#include <uapi/linux/time.h>

#define VDSO_HAS_CLOCK_GETRES		1

#define VDSO_HAS_TIME			1

static __always_inline int do_syscall_2(const unsigned long _r0, const unsigned long _r3,
					const unsigned long _r4)
{
	register long r0 asm("r0") = _r0;
	register unsigned long r3 asm("r3") = _r3;
	register unsigned long r4 asm("r4") = _r4;
	register int ret asm ("r3");

	asm volatile(
		"       sc\n"
		"	bns+	1f\n"
		"	neg	%0, %0\n"
		"1:\n"
	: "=r" (ret), "+r" (r4), "+r" (r0)
	: "r" (r3)
	: "memory", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "cr0", "ctr");

	return ret;
}

static __always_inline
int gettimeofday_fallback(struct __kernel_old_timeval *_tv, struct timezone *_tz)
{
	return do_syscall_2(__NR_gettimeofday, (unsigned long)_tv, (unsigned long)_tz);
}

#ifdef __powerpc64__

static __always_inline
int clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	return do_syscall_2(__NR_clock_gettime, _clkid, (unsigned long)_ts);
}

static __always_inline
int clock_getres_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	return do_syscall_2(__NR_clock_getres, _clkid, (unsigned long)_ts);
}

#else

#define BUILD_VDSO32		1

static __always_inline
int clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	return do_syscall_2(__NR_clock_gettime64, _clkid, (unsigned long)_ts);
}

static __always_inline
int clock_getres_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	return do_syscall_2(__NR_clock_getres_time64, _clkid, (unsigned long)_ts);
}

static __always_inline
int clock_gettime32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	return do_syscall_2(__NR_clock_gettime, _clkid, (unsigned long)_ts);
}

static __always_inline
int clock_getres32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	return do_syscall_2(__NR_clock_getres, _clkid, (unsigned long)_ts);
}
#endif

static __always_inline u64 __arch_get_hw_counter(s32 clock_mode,
						 const struct vdso_data *vd)
{
	return get_tb();
}

const struct vdso_data *__arch_get_vdso_data(void);

#ifdef CONFIG_TIME_NS
static __always_inline
const struct vdso_data *__arch_get_timens_vdso_data(const struct vdso_data *vd)
{
	return (void *)vd + PAGE_SIZE;
}
#endif

static inline bool vdso_clocksource_ok(const struct vdso_data *vd)
{
	return true;
}
#define vdso_clocksource_ok vdso_clocksource_ok

/*
 * powerpc specific delta calculation.
 *
 * This variant removes the masking of the subtraction because the
 * clocksource mask of all VDSO capable clocksources on powerpc is U64_MAX
 * which would result in a pointless operation. The compiler cannot
 * optimize it away as the mask comes from the vdso data and is not compile
 * time constant.
 */
static __always_inline u64 vdso_calc_delta(u64 cycles, u64 last, u64 mask, u32 mult)
{
	return (cycles - last) * mult;
}
#define vdso_calc_delta vdso_calc_delta

#ifndef __powerpc64__
static __always_inline u64 vdso_shift_ns(u64 ns, unsigned long shift)
{
	u32 hi = ns >> 32;
	u32 lo = ns;

	lo >>= shift;
	lo |= hi << (32 - shift);
	hi >>= shift;

	if (likely(hi == 0))
		return lo;

	return ((u64)hi << 32) | lo;
}
#define vdso_shift_ns vdso_shift_ns
#endif

#ifdef __powerpc64__
int __c_kernel_clock_gettime(clockid_t clock, struct __kernel_timespec *ts,
			     const struct vdso_data *vd);
int __c_kernel_clock_getres(clockid_t clock_id, struct __kernel_timespec *res,
			    const struct vdso_data *vd);
#else
int __c_kernel_clock_gettime(clockid_t clock, struct old_timespec32 *ts,
			     const struct vdso_data *vd);
int __c_kernel_clock_gettime64(clockid_t clock, struct __kernel_timespec *ts,
			       const struct vdso_data *vd);
int __c_kernel_clock_getres(clockid_t clock_id, struct old_timespec32 *res,
			    const struct vdso_data *vd);
#endif
int __c_kernel_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz,
			    const struct vdso_data *vd);
__kernel_old_time_t __c_kernel_time(__kernel_old_time_t *time,
				    const struct vdso_data *vd);
#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_VDSO_GETTIMEOFDAY_H */
