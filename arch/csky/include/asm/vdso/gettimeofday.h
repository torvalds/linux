/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_VDSO_CSKY_GETTIMEOFDAY_H
#define __ASM_VDSO_CSKY_GETTIMEOFDAY_H

#ifndef __ASSEMBLY__

#include <asm/barrier.h>
#include <asm/unistd.h>
#include <abi/regdef.h>
#include <uapi/linux/time.h>

#define VDSO_HAS_CLOCK_GETRES	1

static __always_inline
int gettimeofday_fallback(struct __kernel_old_timeval *_tv,
			  struct timezone *_tz)
{
	register struct __kernel_old_timeval *tv asm("a0") = _tv;
	register struct timezone *tz asm("a1") = _tz;
	register long ret asm("a0");
	register long nr asm(syscallid) = __NR_gettimeofday;

	asm volatile ("trap 0\n"
		      : "=r" (ret)
		      : "r"(tv), "r"(tz), "r"(nr)
		      : "memory");

	return ret;
}

static __always_inline
long clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	register clockid_t clkid asm("a0") = _clkid;
	register struct __kernel_timespec *ts asm("a1") = _ts;
	register long ret asm("a0");
	register long nr asm(syscallid) = __NR_clock_gettime64;

	asm volatile ("trap 0\n"
		      : "=r" (ret)
		      : "r"(clkid), "r"(ts), "r"(nr)
		      : "memory");

	return ret;
}

static __always_inline
long clock_gettime32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	register clockid_t clkid asm("a0") = _clkid;
	register struct old_timespec32 *ts asm("a1") = _ts;
	register long ret asm("a0");
	register long nr asm(syscallid) = __NR_clock_gettime;

	asm volatile ("trap 0\n"
		      : "=r" (ret)
		      : "r"(clkid), "r"(ts), "r"(nr)
		      : "memory");

	return ret;
}

static __always_inline
int clock_getres_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	register clockid_t clkid asm("a0") = _clkid;
	register struct __kernel_timespec *ts asm("a1") = _ts;
	register long ret asm("a0");
	register long nr asm(syscallid) = __NR_clock_getres_time64;

	asm volatile ("trap 0\n"
		      : "=r" (ret)
		      : "r"(clkid), "r"(ts), "r"(nr)
		      : "memory");

	return ret;
}

static __always_inline
int clock_getres32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	register clockid_t clkid asm("a0") = _clkid;
	register struct old_timespec32 *ts asm("a1") = _ts;
	register long ret asm("a0");
	register long nr asm(syscallid) = __NR_clock_getres;

	asm volatile ("trap 0\n"
		      : "=r" (ret)
		      : "r"(clkid), "r"(ts), "r"(nr)
		      : "memory");

	return ret;
}

uint64_t csky_pmu_read_cc(void);
static __always_inline u64 __arch_get_hw_counter(s32 clock_mode,
						 const struct vdso_data *vd)
{
#ifdef CONFIG_CSKY_PMU_V1
	return csky_pmu_read_cc();
#else
	return 0;
#endif
}

static __always_inline const struct vdso_data *__arch_get_vdso_data(void)
{
	return _vdso_data;
}

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_CSKY_GETTIMEOFDAY_H */
