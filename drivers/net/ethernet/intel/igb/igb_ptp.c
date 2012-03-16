/*
 * PTP Hardware Clock (PHC) driver for the Intel 82576 and 82580
 *
 * Copyright (C) 2011 Richard Cochran <richardcochran@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>

#include "igb.h"

#define INCVALUE_MASK		0x7fffffff
#define ISGN			0x80000000

/*
 * The 82580 timesync updates the system timer every 8ns by 8ns,
 * and this update value cannot be reprogrammed.
 *
 * Neither the 82576 nor the 82580 offer registers wide enough to hold
 * nanoseconds time values for very long. For the 82580, SYSTIM always
 * counts nanoseconds, but the upper 24 bits are not availible. The
 * frequency is adjusted by changing the 32 bit fractional nanoseconds
 * register, TIMINCA.
 *
 * For the 82576, the SYSTIM register time unit is affect by the
 * choice of the 24 bit TININCA:IV (incvalue) field. Five bits of this
 * field are needed to provide the nominal 16 nanosecond period,
 * leaving 19 bits for fractional nanoseconds.
 *
 * We scale the NIC clock cycle by a large factor so that relatively
 * small clock corrections can be added or subtracted at each clock
 * tick. The drawbacks of a large factor are a) that the clock
 * register overflows more quickly (not such a big deal) and b) that
 * the increment per tick has to fit into 24 bits.  As a result we
 * need to use a shift of 19 so we can fit a value of 16 into the
 * TIMINCA register.
 *
 *
 *             SYSTIMH            SYSTIML
 *        +--------------+   +---+---+------+
 *  82576 |      32      |   | 8 | 5 |  19  |
 *        +--------------+   +---+---+------+
 *         \________ 45 bits _______/  fract
 *
 *        +----------+---+   +--------------+
 *  82580 |    24    | 8 |   |      32      |
 *        +----------+---+   +--------------+
 *          reserved  \______ 40 bits _____/
 *
 *
 * The 45 bit 82576 SYSTIM overflows every
 *   2^45 * 10^-9 / 3600 = 9.77 hours.
 *
 * The 40 bit 82580 SYSTIM overflows every
 *   2^40 * 10^-9 /  60  = 18.3 minutes.
 */

#define IGB_OVERFLOW_PERIOD	(HZ * 60 * 9)
#define INCPERIOD_82576		(1 << E1000_TIMINCA_16NS_SHIFT)
#define INCVALUE_82576_MASK	((1 << E1000_TIMINCA_16NS_SHIFT) - 1)
#define INCVALUE_82576		(16 << IGB_82576_TSYNC_SHIFT)
#define IGB_NBITS_82580		40

/*
 * SYSTIM read access for the 82576
 */

static cycle_t igb_82576_systim_read(const struct cyclecounter *cc)
{
	u64 val;
	u32 lo, hi;
	struct igb_adapter *igb = container_of(cc, struct igb_adapter, cc);
	struct e1000_hw *hw = &igb->hw;

	lo = rd32(E1000_SYSTIML);
	hi = rd32(E1000_SYSTIMH);

	val = ((u64) hi) << 32;
	val |= lo;

	return val;
}

/*
 * SYSTIM read access for the 82580
 */

static cycle_t igb_82580_systim_read(const struct cyclecounter *cc)
{
	u64 val;
	u32 lo, hi, jk;
	struct igb_adapter *igb = container_of(cc, struct igb_adapter, cc);
	struct e1000_hw *hw = &igb->hw;

	/*
	 * The timestamp latches on lowest register read. For the 82580
	 * the lowest register is SYSTIMR instead of SYSTIML.  However we only
	 * need to provide nanosecond resolution, so we just ignore it.
	 */
	jk = rd32(E1000_SYSTIMR);
	lo = rd32(E1000_SYSTIML);
	hi = rd32(E1000_SYSTIMH);

	val = ((u64) hi) << 32;
	val |= lo;

	return val;
}

/*
 * PTP clock operations
 */

static int ptp_82576_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	u64 rate;
	u32 incvalue;
	int neg_adj = 0;
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter, caps);
	struct e1000_hw *hw = &igb->hw;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	rate = ppb;
	rate <<= 14;
	rate = div_u64(rate, 1953125);

	incvalue = 16 << IGB_82576_TSYNC_SHIFT;

	if (neg_adj)
		incvalue -= rate;
	else
		incvalue += rate;

	wr32(E1000_TIMINCA, INCPERIOD_82576 | (incvalue & INCVALUE_82576_MASK));

	return 0;
}

static int ptp_82580_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	u64 rate;
	u32 inca;
	int neg_adj = 0;
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter, caps);
	struct e1000_hw *hw = &igb->hw;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	rate = ppb;
	rate <<= 26;
	rate = div_u64(rate, 1953125);

	inca = rate & INCVALUE_MASK;
	if (neg_adj)
		inca |= ISGN;

	wr32(E1000_TIMINCA, inca);

	return 0;
}

static int igb_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	s64 now;
	unsigned long flags;
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter, caps);

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	now = timecounter_read(&igb->tc);
	now += delta;
	timecounter_init(&igb->tc, &igb->cc, now);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	return 0;
}

static int igb_gettime(struct ptp_clock_info *ptp, struct timespec *ts)
{
	u64 ns;
	u32 remainder;
	unsigned long flags;
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter, caps);

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	ns = timecounter_read(&igb->tc);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	ts->tv_sec = div_u64_rem(ns, 1000000000, &remainder);
	ts->tv_nsec = remainder;

	return 0;
}

static int igb_settime(struct ptp_clock_info *ptp, const struct timespec *ts)
{
	u64 ns;
	unsigned long flags;
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter, caps);

	ns = ts->tv_sec * 1000000000ULL;
	ns += ts->tv_nsec;

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	timecounter_init(&igb->tc, &igb->cc, ns);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	return 0;
}

static int ptp_82576_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static int ptp_82580_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static void igb_overflow_check(struct work_struct *work)
{
	struct timespec ts;
	struct igb_adapter *igb =
		container_of(work, struct igb_adapter, overflow_work.work);

	igb_gettime(&igb->caps, &ts);

	pr_debug("igb overflow check at %ld.%09lu\n", ts.tv_sec, ts.tv_nsec);

	schedule_delayed_work(&igb->overflow_work, IGB_OVERFLOW_PERIOD);
}

void igb_ptp_init(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	switch (hw->mac.type) {
	case e1000_i350:
	case e1000_82580:
		adapter->caps.owner	= THIS_MODULE;
		strcpy(adapter->caps.name, "igb-82580");
		adapter->caps.max_adj	= 62499999;
		adapter->caps.n_ext_ts	= 0;
		adapter->caps.pps	= 0;
		adapter->caps.adjfreq	= ptp_82580_adjfreq;
		adapter->caps.adjtime	= igb_adjtime;
		adapter->caps.gettime	= igb_gettime;
		adapter->caps.settime	= igb_settime;
		adapter->caps.enable	= ptp_82580_enable;
		adapter->cc.read	= igb_82580_systim_read;
		adapter->cc.mask	= CLOCKSOURCE_MASK(IGB_NBITS_82580);
		adapter->cc.mult	= 1;
		adapter->cc.shift	= 0;
		/* Enable the timer functions by clearing bit 31. */
		wr32(E1000_TSAUXC, 0x0);
		break;

	case e1000_82576:
		adapter->caps.owner	= THIS_MODULE;
		strcpy(adapter->caps.name, "igb-82576");
		adapter->caps.max_adj	= 1000000000;
		adapter->caps.n_ext_ts	= 0;
		adapter->caps.pps	= 0;
		adapter->caps.adjfreq	= ptp_82576_adjfreq;
		adapter->caps.adjtime	= igb_adjtime;
		adapter->caps.gettime	= igb_gettime;
		adapter->caps.settime	= igb_settime;
		adapter->caps.enable	= ptp_82576_enable;
		adapter->cc.read	= igb_82576_systim_read;
		adapter->cc.mask	= CLOCKSOURCE_MASK(64);
		adapter->cc.mult	= 1;
		adapter->cc.shift	= IGB_82576_TSYNC_SHIFT;
		/* Dial the nominal frequency. */
		wr32(E1000_TIMINCA, INCPERIOD_82576 | INCVALUE_82576);
		break;

	default:
		adapter->ptp_clock = NULL;
		return;
	}

	wrfl();

	timecounter_init(&adapter->tc, &adapter->cc,
			 ktime_to_ns(ktime_get_real()));

	INIT_DELAYED_WORK(&adapter->overflow_work, igb_overflow_check);

	spin_lock_init(&adapter->tmreg_lock);

	schedule_delayed_work(&adapter->overflow_work, IGB_OVERFLOW_PERIOD);

	adapter->ptp_clock = ptp_clock_register(&adapter->caps);
	if (IS_ERR(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		dev_err(&adapter->pdev->dev, "ptp_clock_register failed\n");
	} else
		dev_info(&adapter->pdev->dev, "added PHC on %s\n",
			 adapter->netdev->name);
}

void igb_ptp_remove(struct igb_adapter *adapter)
{
	cancel_delayed_work_sync(&adapter->overflow_work);

	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		dev_info(&adapter->pdev->dev, "removed PHC on %s\n",
			 adapter->netdev->name);
	}
}

/**
 * igb_systim_to_hwtstamp - convert system time value to hw timestamp
 * @adapter: board private structure
 * @hwtstamps: timestamp structure to update
 * @systim: unsigned 64bit system time value.
 *
 * We need to convert the system time value stored in the RX/TXSTMP registers
 * into a hwtstamp which can be used by the upper level timestamping functions.
 *
 * The 'tmreg_lock' spinlock is used to protect the consistency of the
 * system time value. This is needed because reading the 64 bit time
 * value involves reading two (or three) 32 bit registers. The first
 * read latches the value. Ditto for writing.
 *
 * In addition, here have extended the system time with an overflow
 * counter in software.
 **/
void igb_systim_to_hwtstamp(struct igb_adapter *adapter,
			    struct skb_shared_hwtstamps *hwtstamps,
			    u64 systim)
{
	u64 ns;
	unsigned long flags;

	switch (adapter->hw.mac.type) {
	case e1000_i350:
	case e1000_82580:
	case e1000_82576:
		break;
	default:
		return;
	}

	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	ns = timecounter_cyc2time(&adapter->tc, systim);

	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	memset(hwtstamps, 0, sizeof(*hwtstamps));
	hwtstamps->hwtstamp = ns_to_ktime(ns);
}
