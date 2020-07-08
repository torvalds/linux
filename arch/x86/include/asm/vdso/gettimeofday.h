/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fast user context implementation of clock_gettime, gettimeofday, and time.
 *
 * Copyright (C) 2019 ARM Limited.
 * Copyright 2006 Andi Kleen, SUSE Labs.
 * 32 Bit compat layer by Stefani Seibold <stefani@seibold.net>
 *  sponsored by Rohde & Schwarz GmbH & Co. KG Munich/Germany
 */
#ifndef __ASM_VDSO_GETTIMEOFDAY_H
#define __ASM_VDSO_GETTIMEOFDAY_H

#ifndef __ASSEMBLY__

#include <uapi/linux/time.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>
#include <asm/unistd.h>
#include <asm/msr.h>
#include <asm/pvclock.h>
#include <clocksource/hyperv_timer.h>

#define __vdso_data (VVAR(_vdso_data))
#define __timens_vdso_data (TIMENS(_vdso_data))

#define VDSO_HAS_TIME 1

#define VDSO_HAS_CLOCK_GETRES 1

/*
 * Declare the memory-mapped vclock data pages.  These come from hypervisors.
 * If we ever reintroduce something like direct access to an MMIO clock like
 * the HPET again, it will go here as well.
 *
 * A load from any of these pages will segfault if the clock in question is
 * disabled, so appropriate compiler barriers and checks need to be used
 * to prevent stray loads.
 *
 * These declarations MUST NOT be const.  The compiler will assume that
 * an extern const variable has genuinely constant contents, and the
 * resulting code won't work, since the whole point is that these pages
 * change over time, possibly while we're accessing them.
 */

#ifdef CONFIG_PARAVIRT_CLOCK
/*
 * This is the vCPU 0 pvclock page.  We only use pvclock from the vDSO
 * if the hypervisor tells us that all vCPUs can get valid data from the
 * vCPU 0 page.
 */
extern struct pvclock_vsyscall_time_info pvclock_page
	__attribute__((visibility("hidden")));
#endif

#ifdef CONFIG_HYPERV_TIMER
extern struct ms_hyperv_tsc_page hvclock_page
	__attribute__((visibility("hidden")));
#endif

#ifdef CONFIG_TIME_NS
static __always_inline const struct vdso_data *__arch_get_timens_vdso_data(void)
{
	return __timens_vdso_data;
}
#endif

#ifndef BUILD_VDSO32

static __always_inline
long clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	long ret;

	asm ("syscall" : "=a" (ret), "=m" (*_ts) :
	     "0" (__NR_clock_gettime), "D" (_clkid), "S" (_ts) :
	     "rcx", "r11");

	return ret;
}

static __always_inline
long gettimeofday_fallback(struct __kernel_old_timeval *_tv,
			   struct timezone *_tz)
{
	long ret;

	asm("syscall" : "=a" (ret) :
	    "0" (__NR_gettimeofday), "D" (_tv), "S" (_tz) : "memory");

	return ret;
}

static __always_inline
long clock_getres_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	long ret;

	asm ("syscall" : "=a" (ret), "=m" (*_ts) :
	     "0" (__NR_clock_getres), "D" (_clkid), "S" (_ts) :
	     "rcx", "r11");

	return ret;
}

#else

static __always_inline
long clock_gettime_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*_ts)
		: "0" (__NR_clock_gettime64), [clock] "g" (_clkid), "c" (_ts)
		: "edx");

	return ret;
}

static __always_inline
long clock_gettime32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*_ts)
		: "0" (__NR_clock_gettime), [clock] "g" (_clkid), "c" (_ts)
		: "edx");

	return ret;
}

static __always_inline
long gettimeofday_fallback(struct __kernel_old_timeval *_tv,
			   struct timezone *_tz)
{
	long ret;

	asm(
		"mov %%ebx, %%edx \n"
		"mov %2, %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret)
		: "0" (__NR_gettimeofday), "g" (_tv), "c" (_tz)
		: "memory", "edx");

	return ret;
}

static __always_inline long
clock_getres_fallback(clockid_t _clkid, struct __kernel_timespec *_ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*_ts)
		: "0" (__NR_clock_getres_time64), [clock] "g" (_clkid), "c" (_ts)
		: "edx");

	return ret;
}

static __always_inline
long clock_getres32_fallback(clockid_t _clkid, struct old_timespec32 *_ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*_ts)
		: "0" (__NR_clock_getres), [clock] "g" (_clkid), "c" (_ts)
		: "edx");

	return ret;
}

#endif

#ifdef CONFIG_PARAVIRT_CLOCK
static u64 vread_pvclock(void)
{
	const struct pvclock_vcpu_time_info *pvti = &pvclock_page.pvti;
	u32 version;
	u64 ret;

	/*
	 * Note: The kernel and hypervisor must guarantee that cpu ID
	 * number maps 1:1 to per-CPU pvclock time info.
	 *
	 * Because the hypervisor is entirely unaware of guest userspace
	 * preemption, it cannot guarantee that per-CPU pvclock time
	 * info is updated if the underlying CPU changes or that that
	 * version is increased whenever underlying CPU changes.
	 *
	 * On KVM, we are guaranteed that pvti updates for any vCPU are
	 * atomic as seen by *all* vCPUs.  This is an even stronger
	 * guarantee than we get with a normal seqlock.
	 *
	 * On Xen, we don't appear to have that guarantee, but Xen still
	 * supplies a valid seqlock using the version field.
	 *
	 * We only do pvclock vdso timing at all if
	 * PVCLOCK_TSC_STABLE_BIT is set, and we interpret that bit to
	 * mean that all vCPUs have matching pvti and that the TSC is
	 * synced, so we can just look at vCPU 0's pvti.
	 */

	do {
		version = pvclock_read_begin(pvti);

		if (unlikely(!(pvti->flags & PVCLOCK_TSC_STABLE_BIT)))
			return U64_MAX;

		ret = __pvclock_read_cycles(pvti, rdtsc_ordered());
	} while (pvclock_read_retry(pvti, version));

	return ret;
}
#endif

#ifdef CONFIG_HYPERV_TIMER
static u64 vread_hvclock(void)
{
	return hv_read_tsc_page(&hvclock_page);
}
#endif

static inline u64 __arch_get_hw_counter(s32 clock_mode)
{
	if (likely(clock_mode == VDSO_CLOCKMODE_TSC))
		return (u64)rdtsc_ordered();
	/*
	 * For any memory-mapped vclock type, we need to make sure that gcc
	 * doesn't cleverly hoist a load before the mode check.  Otherwise we
	 * might end up touching the memory-mapped page even if the vclock in
	 * question isn't enabled, which will segfault.  Hence the barriers.
	 */
#ifdef CONFIG_PARAVIRT_CLOCK
	if (clock_mode == VDSO_CLOCKMODE_PVCLOCK) {
		barrier();
		return vread_pvclock();
	}
#endif
#ifdef CONFIG_HYPERV_TIMER
	if (clock_mode == VDSO_CLOCKMODE_HVCLOCK) {
		barrier();
		return vread_hvclock();
	}
#endif
	return U64_MAX;
}

static __always_inline const struct vdso_data *__arch_get_vdso_data(void)
{
	return __vdso_data;
}

static inline bool arch_vdso_clocksource_ok(const struct vdso_data *vd)
{
	return true;
}
#define vdso_clocksource_ok arch_vdso_clocksource_ok

/*
 * Clocksource read value validation to handle PV and HyperV clocksources
 * which can be invalidated asynchronously and indicate invalidation by
 * returning U64_MAX, which can be effectively tested by checking for a
 * negative value after casting it to s64.
 */
static inline bool arch_vdso_cycles_ok(u64 cycles)
{
	return (s64)cycles >= 0;
}
#define vdso_cycles_ok arch_vdso_cycles_ok

/*
 * x86 specific delta calculation.
 *
 * The regular implementation assumes that clocksource reads are globally
 * monotonic. The TSC can be slightly off across sockets which can cause
 * the regular delta calculation (@cycles - @last) to return a huge time
 * jump.
 *
 * Therefore it needs to be verified that @cycles are greater than
 * @last. If not then use @last, which is the base time of the current
 * conversion period.
 *
 * This variant also removes the masking of the subtraction because the
 * clocksource mask of all VDSO capable clocksources on x86 is U64_MAX
 * which would result in a pointless operation. The compiler cannot
 * optimize it away as the mask comes from the vdso data and is not compile
 * time constant.
 */
static __always_inline
u64 vdso_calc_delta(u64 cycles, u64 last, u64 mask, u32 mult)
{
	if (cycles > last)
		return (cycles - last) * mult;
	return 0;
}
#define vdso_calc_delta vdso_calc_delta

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETTIMEOFDAY_H */
