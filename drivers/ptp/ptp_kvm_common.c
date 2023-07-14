// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtual PTP 1588 clock for use with KVM guests
 *
 * Copyright (C) 2017 Red Hat Inc.
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/ptp_kvm.h>
#include <uapi/linux/kvm_para.h>
#include <asm/kvm_para.h>
#include <uapi/asm/kvm_para.h>

#include <linux/ptp_clock_kernel.h>

struct kvm_ptp_clock {
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info caps;
};

static DEFINE_SPINLOCK(kvm_ptp_lock);

static int ptp_kvm_get_time_fn(ktime_t *device_time,
			       struct system_counterval_t *system_counter,
			       void *ctx)
{
	long ret;
	u64 cycle;
	struct timespec64 tspec;
	struct clocksource *cs;

	spin_lock(&kvm_ptp_lock);

	preempt_disable_notrace();
	ret = kvm_arch_ptp_get_crosststamp(&cycle, &tspec, &cs);
	if (ret) {
		spin_unlock(&kvm_ptp_lock);
		preempt_enable_notrace();
		return ret;
	}

	preempt_enable_notrace();

	system_counter->cycles = cycle;
	system_counter->cs = cs;

	*device_time = timespec64_to_ktime(tspec);

	spin_unlock(&kvm_ptp_lock);

	return 0;
}

static int ptp_kvm_getcrosststamp(struct ptp_clock_info *ptp,
				  struct system_device_crosststamp *xtstamp)
{
	return get_device_system_crosststamp(ptp_kvm_get_time_fn, NULL,
					     NULL, xtstamp);
}

/*
 * PTP clock operations
 */

static int ptp_kvm_adjfine(struct ptp_clock_info *ptp, long delta)
{
	return -EOPNOTSUPP;
}

static int ptp_kvm_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	return -EOPNOTSUPP;
}

static int ptp_kvm_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static int ptp_kvm_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	long ret;
	struct timespec64 tspec;

	spin_lock(&kvm_ptp_lock);

	ret = kvm_arch_ptp_get_clock(&tspec);
	if (ret) {
		spin_unlock(&kvm_ptp_lock);
		return ret;
	}

	spin_unlock(&kvm_ptp_lock);

	memcpy(ts, &tspec, sizeof(struct timespec64));

	return 0;
}

static int ptp_kvm_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static const struct ptp_clock_info ptp_kvm_caps = {
	.owner		= THIS_MODULE,
	.name		= "KVM virtual PTP",
	.max_adj	= 0,
	.n_ext_ts	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfine	= ptp_kvm_adjfine,
	.adjtime	= ptp_kvm_adjtime,
	.gettime64	= ptp_kvm_gettime,
	.settime64	= ptp_kvm_settime,
	.enable		= ptp_kvm_enable,
	.getcrosststamp = ptp_kvm_getcrosststamp,
};

/* module operations */

static struct kvm_ptp_clock kvm_ptp_clock;

static void __exit ptp_kvm_exit(void)
{
	ptp_clock_unregister(kvm_ptp_clock.ptp_clock);
	kvm_arch_ptp_exit();
}

static int __init ptp_kvm_init(void)
{
	long ret;

	ret = kvm_arch_ptp_init();
	if (ret) {
		if (ret != -EOPNOTSUPP)
			pr_err("fail to initialize ptp_kvm");
		return ret;
	}

	kvm_ptp_clock.caps = ptp_kvm_caps;

	kvm_ptp_clock.ptp_clock = ptp_clock_register(&kvm_ptp_clock.caps, NULL);

	return PTR_ERR_OR_ZERO(kvm_ptp_clock.ptp_clock);
}

module_init(ptp_kvm_init);
module_exit(ptp_kvm_exit);

MODULE_AUTHOR("Marcelo Tosatti <mtosatti@redhat.com>");
MODULE_DESCRIPTION("PTP clock using KVMCLOCK");
MODULE_LICENSE("GPL");
