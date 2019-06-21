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
#include <asm/mshyperv.h>

#define __vdso_data (VVAR(_vdso_data))

#define VDSO_HAS_TIME 1

#ifdef CONFIG_PARAVIRT_CLOCK
extern u8 pvclock_page[PAGE_SIZE]
	__attribute__((visibility("hidden")));
#endif

#ifdef CONFIG_HYPERV_TSCPAGE
extern u8 hvclock_page[PAGE_SIZE]
	__attribute__((visibility("hidden")));
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

#endif

#ifdef CONFIG_PARAVIRT_CLOCK
static const struct pvclock_vsyscall_time_info *get_pvti0(void)
{
	return (const struct pvclock_vsyscall_time_info *)&pvclock_page;
}

static u64 vread_pvclock(void)
{
	const struct pvclock_vcpu_time_info *pvti = &get_pvti0()->pvti;
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

#ifdef CONFIG_HYPERV_TSCPAGE
static u64 vread_hvclock(void)
{
	const struct ms_hyperv_tsc_page *tsc_pg =
		(const struct ms_hyperv_tsc_page *)&hvclock_page;

	return hv_read_tsc_page(tsc_pg);
}
#endif

static inline u64 __arch_get_hw_counter(s32 clock_mode)
{
	if (clock_mode == VCLOCK_TSC)
		return (u64)rdtsc_ordered();
	/*
	 * For any memory-mapped vclock type, we need to make sure that gcc
	 * doesn't cleverly hoist a load before the mode check.  Otherwise we
	 * might end up touching the memory-mapped page even if the vclock in
	 * question isn't enabled, which will segfault.  Hence the barriers.
	 */
#ifdef CONFIG_PARAVIRT_CLOCK
	if (clock_mode == VCLOCK_PVCLOCK) {
		barrier();
		return vread_pvclock();
	}
#endif
#ifdef CONFIG_HYPERV_TSCPAGE
	if (clock_mode == VCLOCK_HVCLOCK) {
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

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_GETTIMEOFDAY_H */
