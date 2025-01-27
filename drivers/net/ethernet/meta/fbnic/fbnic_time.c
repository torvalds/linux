// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <linux/jiffies.h>
#include <linux/limits.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/timer.h>

#include "fbnic.h"
#include "fbnic_csr.h"
#include "fbnic_netdev.h"

/* FBNIC timing & PTP implementation
 * Datapath uses truncated 40b timestamps for scheduling and event reporting.
 * We need to promote those to full 64b, hence we periodically cache the top
 * 32bit of the HW time counter. Since this makes our time reporting non-atomic
 * we leave the HW clock free running and adjust time offsets in SW as needed.
 * Time offset is 64bit - we need a seq counter for 32bit machines.
 * Time offset and the cache of top bits are independent so we don't need
 * a coherent snapshot of both - READ_ONCE()/WRITE_ONCE() + writer side lock
 * are enough.
 */

/* Period of refresh of top bits of timestamp, give ourselves a 8x margin.
 * This should translate to once a minute.
 * The use of nsecs_to_jiffies() should be safe for a <=40b nsec value.
 */
#define FBNIC_TS_HIGH_REFRESH_JIF	nsecs_to_jiffies((1ULL << 40) / 16)

static struct fbnic_dev *fbnic_from_ptp_info(struct ptp_clock_info *ptp)
{
	return container_of(ptp, struct fbnic_dev, ptp_info);
}

/* This function is "slow" because we could try guessing which high part
 * is correct based on low instead of re-reading, and skip reading @hi
 * twice altogether if @lo is far enough from 0.
 */
static u64 __fbnic_time_get_slow(struct fbnic_dev *fbd)
{
	u32 hi, lo;

	lockdep_assert_held(&fbd->time_lock);

	do {
		hi = fbnic_rd32(fbd, FBNIC_PTP_CTR_VAL_HI);
		lo = fbnic_rd32(fbd, FBNIC_PTP_CTR_VAL_LO);
	} while (hi != fbnic_rd32(fbd, FBNIC_PTP_CTR_VAL_HI));

	return (u64)hi << 32 | lo;
}

static void __fbnic_time_set_addend(struct fbnic_dev *fbd, u64 addend)
{
	lockdep_assert_held(&fbd->time_lock);

	fbnic_wr32(fbd, FBNIC_PTP_ADD_VAL_NS,
		   FIELD_PREP(FBNIC_PTP_ADD_VAL_NS_MASK, addend >> 32));
	fbnic_wr32(fbd, FBNIC_PTP_ADD_VAL_SUBNS, (u32)addend);
}

static void fbnic_ptp_fresh_check(struct fbnic_dev *fbd)
{
	if (time_is_after_jiffies(fbd->last_read +
				  FBNIC_TS_HIGH_REFRESH_JIF * 3 / 2))
		return;

	dev_warn(fbd->dev, "NIC timestamp refresh stall, delayed by %lu sec\n",
		 (jiffies - fbd->last_read - FBNIC_TS_HIGH_REFRESH_JIF) / HZ);
}

static void fbnic_ptp_refresh_time(struct fbnic_dev *fbd, struct fbnic_net *fbn)
{
	unsigned long flags;
	u32 hi;

	spin_lock_irqsave(&fbd->time_lock, flags);
	hi = fbnic_rd32(fbn->fbd, FBNIC_PTP_CTR_VAL_HI);
	if (!fbnic_present(fbd))
		goto out; /* Don't bother handling, reset is pending */
	/* Let's keep high cached value a bit lower to avoid race with
	 * incoming timestamps. The logic in fbnic_ts40_to_ns() will
	 * take care of overflow in this case. It will make cached time
	 * ~1 minute lower and incoming timestamp will always be later
	 * then cached time.
	 */
	WRITE_ONCE(fbn->time_high, hi - 16);
	fbd->last_read = jiffies;
 out:
	spin_unlock_irqrestore(&fbd->time_lock, flags);
}

static long fbnic_ptp_do_aux_work(struct ptp_clock_info *ptp)
{
	struct fbnic_dev *fbd = fbnic_from_ptp_info(ptp);
	struct fbnic_net *fbn;

	fbn = netdev_priv(fbd->netdev);

	fbnic_ptp_fresh_check(fbd);
	fbnic_ptp_refresh_time(fbd, fbn);

	return FBNIC_TS_HIGH_REFRESH_JIF;
}

static int fbnic_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct fbnic_dev *fbd = fbnic_from_ptp_info(ptp);
	u64 addend, dclk_period;
	unsigned long flags;

	/* d_clock is 600 MHz; which in Q16.32 fixed point ns is: */
	dclk_period = (((u64)1000000000) << 32) / FBNIC_CLOCK_FREQ;
	addend = adjust_by_scaled_ppm(dclk_period, scaled_ppm);

	spin_lock_irqsave(&fbd->time_lock, flags);
	__fbnic_time_set_addend(fbd, addend);
	fbnic_wr32(fbd, FBNIC_PTP_ADJUST, FBNIC_PTP_ADJUST_ADDEND_SET);

	/* Flush, make sure FBNIC_PTP_ADD_VAL_* is stable for at least 4 clks */
	fbnic_rd32(fbd, FBNIC_PTP_SPARE);
	spin_unlock_irqrestore(&fbd->time_lock, flags);

	return fbnic_present(fbd) ? 0 : -EIO;
}

static int fbnic_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct fbnic_dev *fbd = fbnic_from_ptp_info(ptp);
	struct fbnic_net *fbn;
	unsigned long flags;

	fbn = netdev_priv(fbd->netdev);

	spin_lock_irqsave(&fbd->time_lock, flags);
	u64_stats_update_begin(&fbn->time_seq);
	WRITE_ONCE(fbn->time_offset, READ_ONCE(fbn->time_offset) + delta);
	u64_stats_update_end(&fbn->time_seq);
	spin_unlock_irqrestore(&fbd->time_lock, flags);

	return 0;
}

static int
fbnic_ptp_gettimex64(struct ptp_clock_info *ptp, struct timespec64 *ts,
		     struct ptp_system_timestamp *sts)
{
	struct fbnic_dev *fbd = fbnic_from_ptp_info(ptp);
	struct fbnic_net *fbn;
	unsigned long flags;
	u64 time_ns;
	u32 hi, lo;

	fbn = netdev_priv(fbd->netdev);

	spin_lock_irqsave(&fbd->time_lock, flags);

	do {
		hi = fbnic_rd32(fbd, FBNIC_PTP_CTR_VAL_HI);
		ptp_read_system_prets(sts);
		lo = fbnic_rd32(fbd, FBNIC_PTP_CTR_VAL_LO);
		ptp_read_system_postts(sts);
		/* Similarly to comment above __fbnic_time_get_slow()
		 * - this can be optimized if needed.
		 */
	} while (hi != fbnic_rd32(fbd, FBNIC_PTP_CTR_VAL_HI));

	time_ns = ((u64)hi << 32 | lo) + fbn->time_offset;
	spin_unlock_irqrestore(&fbd->time_lock, flags);

	if (!fbnic_present(fbd))
		return -EIO;

	*ts = ns_to_timespec64(time_ns);

	return 0;
}

static int
fbnic_ptp_settime64(struct ptp_clock_info *ptp, const struct timespec64 *ts)
{
	struct fbnic_dev *fbd = fbnic_from_ptp_info(ptp);
	struct fbnic_net *fbn;
	unsigned long flags;
	u64 dev_ns, host_ns;
	int ret;

	fbn = netdev_priv(fbd->netdev);

	host_ns = timespec64_to_ns(ts);

	spin_lock_irqsave(&fbd->time_lock, flags);

	dev_ns = __fbnic_time_get_slow(fbd);

	if (fbnic_present(fbd)) {
		u64_stats_update_begin(&fbn->time_seq);
		WRITE_ONCE(fbn->time_offset, host_ns - dev_ns);
		u64_stats_update_end(&fbn->time_seq);
		ret = 0;
	} else {
		ret = -EIO;
	}
	spin_unlock_irqrestore(&fbd->time_lock, flags);

	return ret;
}

static const struct ptp_clock_info fbnic_ptp_info = {
	.owner			= THIS_MODULE,
	/* 1,000,000,000 - 1 PPB to ensure increment is positive
	 * after max negative adjustment.
	 */
	.max_adj		= 999999999,
	.do_aux_work		= fbnic_ptp_do_aux_work,
	.adjfine		= fbnic_ptp_adjfine,
	.adjtime		= fbnic_ptp_adjtime,
	.gettimex64		= fbnic_ptp_gettimex64,
	.settime64		= fbnic_ptp_settime64,
};

static void fbnic_ptp_reset(struct fbnic_dev *fbd)
{
	struct fbnic_net *fbn = netdev_priv(fbd->netdev);
	u64 dclk_period;

	fbnic_wr32(fbd, FBNIC_PTP_CTRL,
		   FBNIC_PTP_CTRL_EN |
		   FIELD_PREP(FBNIC_PTP_CTRL_TICK_IVAL, 1));

	/* d_clock is 600 MHz; which in Q16.32 fixed point ns is: */
	dclk_period = (((u64)1000000000) << 32) / FBNIC_CLOCK_FREQ;

	__fbnic_time_set_addend(fbd, dclk_period);

	fbnic_wr32(fbd, FBNIC_PTP_INIT_HI, 0);
	fbnic_wr32(fbd, FBNIC_PTP_INIT_LO, 0);

	fbnic_wr32(fbd, FBNIC_PTP_ADJUST, FBNIC_PTP_ADJUST_INIT);

	fbnic_wr32(fbd, FBNIC_PTP_CTRL,
		   FBNIC_PTP_CTRL_EN |
		   FBNIC_PTP_CTRL_TQS_OUT_EN |
		   FIELD_PREP(FBNIC_PTP_CTRL_MAC_OUT_IVAL, 3) |
		   FIELD_PREP(FBNIC_PTP_CTRL_TICK_IVAL, 1));

	fbnic_rd32(fbd, FBNIC_PTP_SPARE);

	fbn->time_offset = 0;
	fbn->time_high = 0;
}

void fbnic_time_init(struct fbnic_net *fbn)
{
	/* This is not really a statistic, but the lockng primitive fits
	 * our usecase perfectly, we need an atomic 8 bytes READ_ONCE() /
	 * WRITE_ONCE() behavior.
	 */
	u64_stats_init(&fbn->time_seq);
}

int fbnic_time_start(struct fbnic_net *fbn)
{
	fbnic_ptp_refresh_time(fbn->fbd, fbn);
	/* Assume that fbnic_ptp_do_aux_work() will never be called if not
	 * scheduled here
	 */
	return ptp_schedule_worker(fbn->fbd->ptp, FBNIC_TS_HIGH_REFRESH_JIF);
}

void fbnic_time_stop(struct fbnic_net *fbn)
{
	ptp_cancel_worker_sync(fbn->fbd->ptp);
	fbnic_ptp_fresh_check(fbn->fbd);
}

int fbnic_ptp_setup(struct fbnic_dev *fbd)
{
	struct device *dev = fbd->dev;
	unsigned long flags;

	spin_lock_init(&fbd->time_lock);

	spin_lock_irqsave(&fbd->time_lock, flags); /* Appease lockdep */
	fbnic_ptp_reset(fbd);
	spin_unlock_irqrestore(&fbd->time_lock, flags);

	memcpy(&fbd->ptp_info, &fbnic_ptp_info, sizeof(fbnic_ptp_info));

	fbd->ptp = ptp_clock_register(&fbd->ptp_info, dev);
	if (IS_ERR(fbd->ptp))
		dev_err(dev, "Failed to register PTP: %pe\n", fbd->ptp);

	return PTR_ERR_OR_ZERO(fbd->ptp);
}

void fbnic_ptp_destroy(struct fbnic_dev *fbd)
{
	if (!fbd->ptp)
		return;
	ptp_clock_unregister(fbd->ptp);
}
