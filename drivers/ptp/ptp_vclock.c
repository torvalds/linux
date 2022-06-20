// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP virtual clock driver
 *
 * Copyright 2021 NXP
 */
#include <linux/slab.h>
#include <linux/hashtable.h>
#include "ptp_private.h"

#define PTP_VCLOCK_CC_SHIFT		31
#define PTP_VCLOCK_CC_MULT		(1 << PTP_VCLOCK_CC_SHIFT)
#define PTP_VCLOCK_FADJ_SHIFT		9
#define PTP_VCLOCK_FADJ_DENOMINATOR	15625ULL
#define PTP_VCLOCK_REFRESH_INTERVAL	(HZ * 2)

/* protects vclock_hash addition/deletion */
static DEFINE_SPINLOCK(vclock_hash_lock);

static DEFINE_READ_MOSTLY_HASHTABLE(vclock_hash, 8);

static void ptp_vclock_hash_add(struct ptp_vclock *vclock)
{
	spin_lock(&vclock_hash_lock);

	hlist_add_head_rcu(&vclock->vclock_hash_node,
			   &vclock_hash[vclock->clock->index % HASH_SIZE(vclock_hash)]);

	spin_unlock(&vclock_hash_lock);
}

static void ptp_vclock_hash_del(struct ptp_vclock *vclock)
{
	spin_lock(&vclock_hash_lock);

	hlist_del_init_rcu(&vclock->vclock_hash_node);

	spin_unlock(&vclock_hash_lock);

	synchronize_rcu();
}

static int ptp_vclock_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	unsigned long flags;
	s64 adj;

	adj = (s64)scaled_ppm << PTP_VCLOCK_FADJ_SHIFT;
	adj = div_s64(adj, PTP_VCLOCK_FADJ_DENOMINATOR);

	spin_lock_irqsave(&vclock->lock, flags);
	timecounter_read(&vclock->tc);
	vclock->cc.mult = PTP_VCLOCK_CC_MULT + adj;
	spin_unlock_irqrestore(&vclock->lock, flags);

	return 0;
}

static int ptp_vclock_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	unsigned long flags;

	spin_lock_irqsave(&vclock->lock, flags);
	timecounter_adjtime(&vclock->tc, delta);
	spin_unlock_irqrestore(&vclock->lock, flags);

	return 0;
}

static int ptp_vclock_gettime(struct ptp_clock_info *ptp,
			      struct timespec64 *ts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&vclock->lock, flags);
	ns = timecounter_read(&vclock->tc);
	spin_unlock_irqrestore(&vclock->lock, flags);
	*ts = ns_to_timespec64(ns);

	return 0;
}

static int ptp_vclock_gettimex(struct ptp_clock_info *ptp,
			       struct timespec64 *ts,
			       struct ptp_system_timestamp *sts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	struct ptp_clock *pptp = vclock->pclock;
	struct timespec64 pts;
	unsigned long flags;
	int err;
	u64 ns;

	err = pptp->info->getcyclesx64(pptp->info, &pts, sts);
	if (err)
		return err;

	spin_lock_irqsave(&vclock->lock, flags);
	ns = timecounter_cyc2time(&vclock->tc, timespec64_to_ns(&pts));
	spin_unlock_irqrestore(&vclock->lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int ptp_vclock_settime(struct ptp_clock_info *ptp,
			      const struct timespec64 *ts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	u64 ns = timespec64_to_ns(ts);
	unsigned long flags;

	spin_lock_irqsave(&vclock->lock, flags);
	timecounter_init(&vclock->tc, &vclock->cc, ns);
	spin_unlock_irqrestore(&vclock->lock, flags);

	return 0;
}

static int ptp_vclock_getcrosststamp(struct ptp_clock_info *ptp,
				     struct system_device_crosststamp *xtstamp)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	struct ptp_clock *pptp = vclock->pclock;
	unsigned long flags;
	int err;
	u64 ns;

	err = pptp->info->getcrosscycles(pptp->info, xtstamp);
	if (err)
		return err;

	spin_lock_irqsave(&vclock->lock, flags);
	ns = timecounter_cyc2time(&vclock->tc, ktime_to_ns(xtstamp->device));
	spin_unlock_irqrestore(&vclock->lock, flags);

	xtstamp->device = ns_to_ktime(ns);

	return 0;
}

static long ptp_vclock_refresh(struct ptp_clock_info *ptp)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	struct timespec64 ts;

	ptp_vclock_gettime(&vclock->info, &ts);

	return PTP_VCLOCK_REFRESH_INTERVAL;
}

static const struct ptp_clock_info ptp_vclock_info = {
	.owner		= THIS_MODULE,
	.name		= "ptp virtual clock",
	.max_adj	= 500000000,
	.adjfine	= ptp_vclock_adjfine,
	.adjtime	= ptp_vclock_adjtime,
	.settime64	= ptp_vclock_settime,
	.do_aux_work	= ptp_vclock_refresh,
};

static u64 ptp_vclock_read(const struct cyclecounter *cc)
{
	struct ptp_vclock *vclock = cc_to_vclock(cc);
	struct ptp_clock *ptp = vclock->pclock;
	struct timespec64 ts = {};

	ptp->info->getcycles64(ptp->info, &ts);

	return timespec64_to_ns(&ts);
}

static const struct cyclecounter ptp_vclock_cc = {
	.read	= ptp_vclock_read,
	.mask	= CYCLECOUNTER_MASK(32),
	.mult	= PTP_VCLOCK_CC_MULT,
	.shift	= PTP_VCLOCK_CC_SHIFT,
};

struct ptp_vclock *ptp_vclock_register(struct ptp_clock *pclock)
{
	struct ptp_vclock *vclock;

	vclock = kzalloc(sizeof(*vclock), GFP_KERNEL);
	if (!vclock)
		return NULL;

	vclock->pclock = pclock;
	vclock->info = ptp_vclock_info;
	if (pclock->info->getcyclesx64)
		vclock->info.gettimex64 = ptp_vclock_gettimex;
	else
		vclock->info.gettime64 = ptp_vclock_gettime;
	if (pclock->info->getcrosscycles)
		vclock->info.getcrosststamp = ptp_vclock_getcrosststamp;
	vclock->cc = ptp_vclock_cc;

	snprintf(vclock->info.name, PTP_CLOCK_NAME_LEN, "ptp%d_virt",
		 pclock->index);

	INIT_HLIST_NODE(&vclock->vclock_hash_node);

	spin_lock_init(&vclock->lock);

	vclock->clock = ptp_clock_register(&vclock->info, &pclock->dev);
	if (IS_ERR_OR_NULL(vclock->clock)) {
		kfree(vclock);
		return NULL;
	}

	timecounter_init(&vclock->tc, &vclock->cc, 0);
	ptp_schedule_worker(vclock->clock, PTP_VCLOCK_REFRESH_INTERVAL);

	ptp_vclock_hash_add(vclock);

	return vclock;
}

void ptp_vclock_unregister(struct ptp_vclock *vclock)
{
	ptp_vclock_hash_del(vclock);

	ptp_clock_unregister(vclock->clock);
	kfree(vclock);
}

#if IS_BUILTIN(CONFIG_PTP_1588_CLOCK)
int ptp_get_vclocks_index(int pclock_index, int **vclock_index)
{
	char name[PTP_CLOCK_NAME_LEN] = "";
	struct ptp_clock *ptp;
	struct device *dev;
	int num = 0;

	if (pclock_index < 0)
		return num;

	snprintf(name, PTP_CLOCK_NAME_LEN, "ptp%d", pclock_index);
	dev = class_find_device_by_name(ptp_class, name);
	if (!dev)
		return num;

	ptp = dev_get_drvdata(dev);

	if (mutex_lock_interruptible(&ptp->n_vclocks_mux)) {
		put_device(dev);
		return num;
	}

	*vclock_index = kzalloc(sizeof(int) * ptp->n_vclocks, GFP_KERNEL);
	if (!(*vclock_index))
		goto out;

	memcpy(*vclock_index, ptp->vclock_index, sizeof(int) * ptp->n_vclocks);
	num = ptp->n_vclocks;
out:
	mutex_unlock(&ptp->n_vclocks_mux);
	put_device(dev);
	return num;
}
EXPORT_SYMBOL(ptp_get_vclocks_index);

ktime_t ptp_convert_timestamp(const ktime_t *hwtstamp, int vclock_index)
{
	unsigned int hash = vclock_index % HASH_SIZE(vclock_hash);
	struct ptp_vclock *vclock;
	unsigned long flags;
	u64 ns;
	u64 vclock_ns = 0;

	ns = ktime_to_ns(*hwtstamp);

	rcu_read_lock();

	hlist_for_each_entry_rcu(vclock, &vclock_hash[hash], vclock_hash_node) {
		if (vclock->clock->index != vclock_index)
			continue;

		spin_lock_irqsave(&vclock->lock, flags);
		vclock_ns = timecounter_cyc2time(&vclock->tc, ns);
		spin_unlock_irqrestore(&vclock->lock, flags);
		break;
	}

	rcu_read_unlock();

	return ns_to_ktime(vclock_ns);
}
EXPORT_SYMBOL(ptp_convert_timestamp);
#endif
