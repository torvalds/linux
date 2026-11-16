// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PTP virtual clock driver
 *
 * Copyright 2021 NXP
 */
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/hashtable.h>
#include "ptp_private.h"

#define PTP_VCLOCK_CC_SHIFT		31
#define PTP_VCLOCK_CC_MULT		(1 << PTP_VCLOCK_CC_SHIFT)
#define PTP_VCLOCK_FADJ_SHIFT		9
#define PTP_VCLOCK_FADJ_DENOMINATOR	15625ULL
#define PTP_VCLOCK_REFRESH_INTERVAL	(HZ * 2)
#define PTP_VCLOCK_RETRY_INTERVAL		msecs_to_jiffies(10)

/* protects vclock_hash addition/deletion */
static DEFINE_SPINLOCK(vclock_hash_lock);

static DEFINE_READ_MOSTLY_HASHTABLE(vclock_hash, 8);

DEFINE_STATIC_SRCU(vclock_srcu);

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

	synchronize_srcu(&vclock_srcu);
}

/* The caller holds vclock->lock, or has not published the clock yet. */
static int ptp_vclock_sample(struct ptp_vclock *vclock)
{
	struct ptp_clock *ptp = vclock->pclock;
	struct timespec64 ts;
	int err;

	err = ptp->info->getcycles64(ptp->info, &ts);
	if (!err)
		vclock->cycles = timespec64_to_ns(&ts);

	return err;
}

static int ptp_vclock_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	s64 adj;
	int err;

	adj = (s64)scaled_ppm << PTP_VCLOCK_FADJ_SHIFT;
	adj = div_s64(adj, PTP_VCLOCK_FADJ_DENOMINATOR);

	if (mutex_lock_interruptible(&vclock->lock))
		return -EINTR;
	err = ptp_vclock_sample(vclock);
	if (!err) {
		timecounter_read(&vclock->tc);
		vclock->cc.mult = PTP_VCLOCK_CC_MULT + adj;
	}
	mutex_unlock(&vclock->lock);

	return err;
}

static int ptp_vclock_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);

	if (mutex_lock_interruptible(&vclock->lock))
		return -EINTR;
	timecounter_adjtime(&vclock->tc, delta);
	mutex_unlock(&vclock->lock);

	return 0;
}

static int ptp_vclock_gettime(struct ptp_clock_info *ptp,
			      struct timespec64 *ts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	int err;

	if (mutex_lock_interruptible(&vclock->lock))
		return -EINTR;
	err = ptp_vclock_sample(vclock);
	if (!err)
		*ts = ns_to_timespec64(timecounter_read(&vclock->tc));
	mutex_unlock(&vclock->lock);

	return err;
}

static int ptp_vclock_gettimex(struct ptp_clock_info *ptp,
			       struct timespec64 *ts,
			       struct ptp_system_timestamp *sts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	struct ptp_clock *pptp = vclock->pclock;
	struct timespec64 pts;
	int err;

	if (mutex_lock_interruptible(&vclock->lock))
		return -EINTR;
	err = pptp->info->getcyclesx64(pptp->info, &pts, sts);
	if (!err) {
		u64 ns = timecounter_cyc2time(&vclock->tc, timespec64_to_ns(&pts));

		*ts = ns_to_timespec64(ns);
	}
	mutex_unlock(&vclock->lock);

	return err;
}

static int ptp_vclock_settime(struct ptp_clock_info *ptp,
			      const struct timespec64 *ts)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	u64 ns = timespec64_to_ns(ts);
	int err;

	if (mutex_lock_interruptible(&vclock->lock))
		return -EINTR;
	err = ptp_vclock_sample(vclock);
	if (!err)
		timecounter_init(&vclock->tc, &vclock->cc, ns);
	mutex_unlock(&vclock->lock);

	return err;
}

static int ptp_vclock_getcrosststamp(struct ptp_clock_info *ptp,
				     struct system_device_crosststamp *xtstamp)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	struct ptp_clock *pptp = vclock->pclock;
	int err;

	if (mutex_lock_interruptible(&vclock->lock))
		return -EINTR;
	err = pptp->info->getcrosscycles(pptp->info, xtstamp);
	if (!err) {
		u64 ns = timecounter_cyc2time(&vclock->tc, ktime_to_ns(xtstamp->device));

		xtstamp->device = ns_to_ktime(ns);
	}
	mutex_unlock(&vclock->lock);

	return err;
}

static long ptp_vclock_refresh(struct ptp_clock_info *ptp)
{
	struct ptp_vclock *vclock = info_to_vclock(ptp);
	struct timespec64 ts;

	/* Retry transient failures promptly, but back off during long outages. */
	if (ptp_vclock_gettime(&vclock->info, &ts)) {
		unsigned long now = jiffies;

		/* Retries must not postpone the next regular refresh attempt. */
		if (!vclock->retry_delay || time_after_eq(now, vclock->refresh_deadline))
			vclock->refresh_deadline = now + PTP_VCLOCK_REFRESH_INTERVAL;

		if (!vclock->retry_delay)
			vclock->retry_delay = PTP_VCLOCK_RETRY_INTERVAL;
		else
			vclock->retry_delay = min_t(unsigned long,
						    vclock->retry_delay * 2,
						    PTP_VCLOCK_REFRESH_INTERVAL);
		return min(vclock->retry_delay, vclock->refresh_deadline - now);
	}

	vclock->retry_delay = 0;
	return PTP_VCLOCK_REFRESH_INTERVAL;
}

static void ptp_vclock_set_subclass(struct ptp_clock *ptp)
{
	lockdep_set_subclass(&ptp->clock.rwsem, PTP_LOCK_VIRTUAL);
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

static u64 ptp_vclock_read(struct cyclecounter *cc)
{
	struct ptp_vclock *vclock = cc_to_vclock(cc);

	/* The cyclecounter callback cannot report a failed PHC read. */
	return vclock->cycles;
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
	int err;

	vclock = kzalloc_obj(*vclock);
	if (!vclock)
		return ERR_PTR(-ENOMEM);

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

	mutex_init(&vclock->lock);

	err = ptp_vclock_sample(vclock);
	if (err)
		goto free_vclock;
	timecounter_init(&vclock->tc, &vclock->cc, 0);

	vclock->clock = ptp_clock_register(&vclock->info, &pclock->dev);
	if (IS_ERR(vclock->clock)) {
		err = PTR_ERR(vclock->clock);
		goto free_vclock;
	}

	ptp_vclock_set_subclass(vclock->clock);

	/* The initial sample predates registration; refresh without more delay. */
	ptp_schedule_worker(vclock->clock, 0);

	ptp_vclock_hash_add(vclock);

	return vclock;

free_vclock:
	kfree(vclock);
	return ERR_PTR(err);
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
	dev = class_find_device_by_name(&ptp_class, name);
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
	u64 vclock_ns = 0;
	int srcu_idx;
	u64 ns;

	ns = ktime_to_ns(*hwtstamp);
	if (!ns)
		return 0;

	srcu_idx = srcu_read_lock(&vclock_srcu);

	hlist_for_each_entry_srcu(vclock, &vclock_hash[hash], vclock_hash_node,
				  srcu_read_lock_held(&vclock_srcu)) {
		if (vclock->clock->index != vclock_index)
			continue;

		if (mutex_lock_interruptible(&vclock->lock))
			break;
		vclock_ns = timecounter_cyc2time(&vclock->tc, ns);
		mutex_unlock(&vclock->lock);
		break;
	}

	srcu_read_unlock(&vclock_srcu, srcu_idx);

	return ns_to_ktime(vclock_ns);
}
EXPORT_SYMBOL(ptp_convert_timestamp);
#endif
