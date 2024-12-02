// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtual PTP 1588 clock for use with KVM guests
 *
 * Copyright (C) 2017 Red Hat Inc.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <asm/pvclock.h>
#include <asm/kvmclock.h>
#include <linux/module.h>
#include <uapi/asm/kvm_para.h>
#include <uapi/linux/kvm_para.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_kvm.h>

static phys_addr_t clock_pair_gpa;
static struct kvm_clock_pairing clock_pair;

int kvm_arch_ptp_init(void)
{
	long ret;

	if (!kvm_para_available())
		return -ENODEV;

	clock_pair_gpa = slow_virt_to_phys(&clock_pair);
	if (!pvclock_get_pvti_cpu0_va())
		return -ENODEV;

	ret = kvm_hypercall2(KVM_HC_CLOCK_PAIRING, clock_pair_gpa,
			     KVM_CLOCK_PAIRING_WALLCLOCK);
	if (ret == -KVM_ENOSYS)
		return -ENODEV;

	return ret;
}

int kvm_arch_ptp_get_clock(struct timespec64 *ts)
{
	long ret;

	ret = kvm_hypercall2(KVM_HC_CLOCK_PAIRING,
			     clock_pair_gpa,
			     KVM_CLOCK_PAIRING_WALLCLOCK);
	if (ret != 0) {
		pr_err_ratelimited("clock offset hypercall ret %lu\n", ret);
		return -EOPNOTSUPP;
	}

	ts->tv_sec = clock_pair.sec;
	ts->tv_nsec = clock_pair.nsec;

	return 0;
}

int kvm_arch_ptp_get_crosststamp(u64 *cycle, struct timespec64 *tspec,
			      struct clocksource **cs)
{
	struct pvclock_vcpu_time_info *src;
	unsigned int version;
	long ret;

	src = this_cpu_pvti();

	do {
		/*
		 * We are using a TSC value read in the hosts
		 * kvm_hc_clock_pairing handling.
		 * So any changes to tsc_to_system_mul
		 * and tsc_shift or any other pvclock
		 * data invalidate that measurement.
		 */
		version = pvclock_read_begin(src);

		ret = kvm_hypercall2(KVM_HC_CLOCK_PAIRING,
				     clock_pair_gpa,
				     KVM_CLOCK_PAIRING_WALLCLOCK);
		if (ret != 0) {
			pr_err_ratelimited("clock pairing hypercall ret %lu\n", ret);
			return -EOPNOTSUPP;
		}
		tspec->tv_sec = clock_pair.sec;
		tspec->tv_nsec = clock_pair.nsec;
		*cycle = __pvclock_read_cycles(src, clock_pair.tsc);
	} while (pvclock_read_retry(src, version));

	*cs = &kvm_clock;

	return 0;
}
