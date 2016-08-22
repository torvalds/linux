/* PTP Hardware Clock (PHC) driver for the Intel 82576 and 82580
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/ptp_classify.h>

#include "igb.h"

#define INCVALUE_MASK		0x7fffffff
#define ISGN			0x80000000

/* The 82580 timesync updates the system timer every 8ns by 8ns,
 * and this update value cannot be reprogrammed.
 *
 * Neither the 82576 nor the 82580 offer registers wide enough to hold
 * nanoseconds time values for very long. For the 82580, SYSTIM always
 * counts nanoseconds, but the upper 24 bits are not available. The
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

#define IGB_SYSTIM_OVERFLOW_PERIOD	(HZ * 60 * 9)
#define IGB_PTP_TX_TIMEOUT		(HZ * 15)
#define INCPERIOD_82576			BIT(E1000_TIMINCA_16NS_SHIFT)
#define INCVALUE_82576_MASK		GENMASK(E1000_TIMINCA_16NS_SHIFT - 1, 0)
#define INCVALUE_82576			(16u << IGB_82576_TSYNC_SHIFT)
#define IGB_NBITS_82580			40

static void igb_ptp_tx_hwtstamp(struct igb_adapter *adapter);

/* SYSTIM read access for the 82576 */
static cycle_t igb_ptp_read_82576(const struct cyclecounter *cc)
{
	struct igb_adapter *igb = container_of(cc, struct igb_adapter, cc);
	struct e1000_hw *hw = &igb->hw;
	u64 val;
	u32 lo, hi;

	lo = rd32(E1000_SYSTIML);
	hi = rd32(E1000_SYSTIMH);

	val = ((u64) hi) << 32;
	val |= lo;

	return val;
}

/* SYSTIM read access for the 82580 */
static cycle_t igb_ptp_read_82580(const struct cyclecounter *cc)
{
	struct igb_adapter *igb = container_of(cc, struct igb_adapter, cc);
	struct e1000_hw *hw = &igb->hw;
	u32 lo, hi;
	u64 val;

	/* The timestamp latches on lowest register read. For the 82580
	 * the lowest register is SYSTIMR instead of SYSTIML.  However we only
	 * need to provide nanosecond resolution, so we just ignore it.
	 */
	rd32(E1000_SYSTIMR);
	lo = rd32(E1000_SYSTIML);
	hi = rd32(E1000_SYSTIMH);

	val = ((u64) hi) << 32;
	val |= lo;

	return val;
}

/* SYSTIM read access for I210/I211 */
static void igb_ptp_read_i210(struct igb_adapter *adapter,
			      struct timespec64 *ts)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 sec, nsec;

	/* The timestamp latches on lowest register read. For I210/I211, the
	 * lowest register is SYSTIMR. Since we only need to provide nanosecond
	 * resolution, we can ignore it.
	 */
	rd32(E1000_SYSTIMR);
	nsec = rd32(E1000_SYSTIML);
	sec = rd32(E1000_SYSTIMH);

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static void igb_ptp_write_i210(struct igb_adapter *adapter,
			       const struct timespec64 *ts)
{
	struct e1000_hw *hw = &adapter->hw;

	/* Writing the SYSTIMR register is not necessary as it only provides
	 * sub-nanosecond resolution.
	 */
	wr32(E1000_SYSTIML, ts->tv_nsec);
	wr32(E1000_SYSTIMH, (u32)ts->tv_sec);
}

/**
 * igb_ptp_systim_to_hwtstamp - convert system time value to hw timestamp
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
static void igb_ptp_systim_to_hwtstamp(struct igb_adapter *adapter,
				       struct skb_shared_hwtstamps *hwtstamps,
				       u64 systim)
{
	unsigned long flags;
	u64 ns;

	switch (adapter->hw.mac.type) {
	case e1000_82576:
	case e1000_82580:
	case e1000_i354:
	case e1000_i350:
		spin_lock_irqsave(&adapter->tmreg_lock, flags);

		ns = timecounter_cyc2time(&adapter->tc, systim);

		spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

		memset(hwtstamps, 0, sizeof(*hwtstamps));
		hwtstamps->hwtstamp = ns_to_ktime(ns);
		break;
	case e1000_i210:
	case e1000_i211:
		memset(hwtstamps, 0, sizeof(*hwtstamps));
		/* Upper 32 bits contain s, lower 32 bits contain ns. */
		hwtstamps->hwtstamp = ktime_set(systim >> 32,
						systim & 0xFFFFFFFF);
		break;
	default:
		break;
	}
}

/* PTP clock operations */
static int igb_ptp_adjfreq_82576(struct ptp_clock_info *ptp, s32 ppb)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	struct e1000_hw *hw = &igb->hw;
	int neg_adj = 0;
	u64 rate;
	u32 incvalue;

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

static int igb_ptp_adjfreq_82580(struct ptp_clock_info *ptp, s32 ppb)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	struct e1000_hw *hw = &igb->hw;
	int neg_adj = 0;
	u64 rate;
	u32 inca;

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

static int igb_ptp_adjtime_82576(struct ptp_clock_info *ptp, s64 delta)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	unsigned long flags;

	spin_lock_irqsave(&igb->tmreg_lock, flags);
	timecounter_adjtime(&igb->tc, delta);
	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	return 0;
}

static int igb_ptp_adjtime_i210(struct ptp_clock_info *ptp, s64 delta)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	unsigned long flags;
	struct timespec64 now, then = ns_to_timespec64(delta);

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	igb_ptp_read_i210(igb, &now);
	now = timespec64_add(now, then);
	igb_ptp_write_i210(igb, (const struct timespec64 *)&now);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	return 0;
}

static int igb_ptp_gettime_82576(struct ptp_clock_info *ptp,
				 struct timespec64 *ts)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	ns = timecounter_read(&igb->tc);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int igb_ptp_gettime_i210(struct ptp_clock_info *ptp,
				struct timespec64 *ts)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	unsigned long flags;

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	igb_ptp_read_i210(igb, ts);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	return 0;
}

static int igb_ptp_settime_82576(struct ptp_clock_info *ptp,
				 const struct timespec64 *ts)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	unsigned long flags;
	u64 ns;

	ns = timespec64_to_ns(ts);

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	timecounter_init(&igb->tc, &igb->cc, ns);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	return 0;
}

static int igb_ptp_settime_i210(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct igb_adapter *igb = container_of(ptp, struct igb_adapter,
					       ptp_caps);
	unsigned long flags;

	spin_lock_irqsave(&igb->tmreg_lock, flags);

	igb_ptp_write_i210(igb, ts);

	spin_unlock_irqrestore(&igb->tmreg_lock, flags);

	return 0;
}

static void igb_pin_direction(int pin, int input, u32 *ctrl, u32 *ctrl_ext)
{
	u32 *ptr = pin < 2 ? ctrl : ctrl_ext;
	static const u32 mask[IGB_N_SDP] = {
		E1000_CTRL_SDP0_DIR,
		E1000_CTRL_SDP1_DIR,
		E1000_CTRL_EXT_SDP2_DIR,
		E1000_CTRL_EXT_SDP3_DIR,
	};

	if (input)
		*ptr &= ~mask[pin];
	else
		*ptr |= mask[pin];
}

static void igb_pin_extts(struct igb_adapter *igb, int chan, int pin)
{
	static const u32 aux0_sel_sdp[IGB_N_SDP] = {
		AUX0_SEL_SDP0, AUX0_SEL_SDP1, AUX0_SEL_SDP2, AUX0_SEL_SDP3,
	};
	static const u32 aux1_sel_sdp[IGB_N_SDP] = {
		AUX1_SEL_SDP0, AUX1_SEL_SDP1, AUX1_SEL_SDP2, AUX1_SEL_SDP3,
	};
	static const u32 ts_sdp_en[IGB_N_SDP] = {
		TS_SDP0_EN, TS_SDP1_EN, TS_SDP2_EN, TS_SDP3_EN,
	};
	struct e1000_hw *hw = &igb->hw;
	u32 ctrl, ctrl_ext, tssdp = 0;

	ctrl = rd32(E1000_CTRL);
	ctrl_ext = rd32(E1000_CTRL_EXT);
	tssdp = rd32(E1000_TSSDP);

	igb_pin_direction(pin, 1, &ctrl, &ctrl_ext);

	/* Make sure this pin is not enabled as an output. */
	tssdp &= ~ts_sdp_en[pin];

	if (chan == 1) {
		tssdp &= ~AUX1_SEL_SDP3;
		tssdp |= aux1_sel_sdp[pin] | AUX1_TS_SDP_EN;
	} else {
		tssdp &= ~AUX0_SEL_SDP3;
		tssdp |= aux0_sel_sdp[pin] | AUX0_TS_SDP_EN;
	}

	wr32(E1000_TSSDP, tssdp);
	wr32(E1000_CTRL, ctrl);
	wr32(E1000_CTRL_EXT, ctrl_ext);
}

static void igb_pin_perout(struct igb_adapter *igb, int chan, int pin, int freq)
{
	static const u32 aux0_sel_sdp[IGB_N_SDP] = {
		AUX0_SEL_SDP0, AUX0_SEL_SDP1, AUX0_SEL_SDP2, AUX0_SEL_SDP3,
	};
	static const u32 aux1_sel_sdp[IGB_N_SDP] = {
		AUX1_SEL_SDP0, AUX1_SEL_SDP1, AUX1_SEL_SDP2, AUX1_SEL_SDP3,
	};
	static const u32 ts_sdp_en[IGB_N_SDP] = {
		TS_SDP0_EN, TS_SDP1_EN, TS_SDP2_EN, TS_SDP3_EN,
	};
	static const u32 ts_sdp_sel_tt0[IGB_N_SDP] = {
		TS_SDP0_SEL_TT0, TS_SDP1_SEL_TT0,
		TS_SDP2_SEL_TT0, TS_SDP3_SEL_TT0,
	};
	static const u32 ts_sdp_sel_tt1[IGB_N_SDP] = {
		TS_SDP0_SEL_TT1, TS_SDP1_SEL_TT1,
		TS_SDP2_SEL_TT1, TS_SDP3_SEL_TT1,
	};
	static const u32 ts_sdp_sel_fc0[IGB_N_SDP] = {
		TS_SDP0_SEL_FC0, TS_SDP1_SEL_FC0,
		TS_SDP2_SEL_FC0, TS_SDP3_SEL_FC0,
	};
	static const u32 ts_sdp_sel_fc1[IGB_N_SDP] = {
		TS_SDP0_SEL_FC1, TS_SDP1_SEL_FC1,
		TS_SDP2_SEL_FC1, TS_SDP3_SEL_FC1,
	};
	static const u32 ts_sdp_sel_clr[IGB_N_SDP] = {
		TS_SDP0_SEL_FC1, TS_SDP1_SEL_FC1,
		TS_SDP2_SEL_FC1, TS_SDP3_SEL_FC1,
	};
	struct e1000_hw *hw = &igb->hw;
	u32 ctrl, ctrl_ext, tssdp = 0;

	ctrl = rd32(E1000_CTRL);
	ctrl_ext = rd32(E1000_CTRL_EXT);
	tssdp = rd32(E1000_TSSDP);

	igb_pin_direction(pin, 0, &ctrl, &ctrl_ext);

	/* Make sure this pin is not enabled as an input. */
	if ((tssdp & AUX0_SEL_SDP3) == aux0_sel_sdp[pin])
		tssdp &= ~AUX0_TS_SDP_EN;

	if ((tssdp & AUX1_SEL_SDP3) == aux1_sel_sdp[pin])
		tssdp &= ~AUX1_TS_SDP_EN;

	tssdp &= ~ts_sdp_sel_clr[pin];
	if (freq) {
		if (chan == 1)
			tssdp |= ts_sdp_sel_fc1[pin];
		else
			tssdp |= ts_sdp_sel_fc0[pin];
	} else {
		if (chan == 1)
			tssdp |= ts_sdp_sel_tt1[pin];
		else
			tssdp |= ts_sdp_sel_tt0[pin];
	}
	tssdp |= ts_sdp_en[pin];

	wr32(E1000_TSSDP, tssdp);
	wr32(E1000_CTRL, ctrl);
	wr32(E1000_CTRL_EXT, ctrl_ext);
}

static int igb_ptp_feature_enable_i210(struct ptp_clock_info *ptp,
				       struct ptp_clock_request *rq, int on)
{
	struct igb_adapter *igb =
		container_of(ptp, struct igb_adapter, ptp_caps);
	struct e1000_hw *hw = &igb->hw;
	u32 tsauxc, tsim, tsauxc_mask, tsim_mask, trgttiml, trgttimh, freqout;
	unsigned long flags;
	struct timespec64 ts;
	int use_freq = 0, pin = -1;
	s64 ns;

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		if (on) {
			pin = ptp_find_pin(igb->ptp_clock, PTP_PF_EXTTS,
					   rq->extts.index);
			if (pin < 0)
				return -EBUSY;
		}
		if (rq->extts.index == 1) {
			tsauxc_mask = TSAUXC_EN_TS1;
			tsim_mask = TSINTR_AUTT1;
		} else {
			tsauxc_mask = TSAUXC_EN_TS0;
			tsim_mask = TSINTR_AUTT0;
		}
		spin_lock_irqsave(&igb->tmreg_lock, flags);
		tsauxc = rd32(E1000_TSAUXC);
		tsim = rd32(E1000_TSIM);
		if (on) {
			igb_pin_extts(igb, rq->extts.index, pin);
			tsauxc |= tsauxc_mask;
			tsim |= tsim_mask;
		} else {
			tsauxc &= ~tsauxc_mask;
			tsim &= ~tsim_mask;
		}
		wr32(E1000_TSAUXC, tsauxc);
		wr32(E1000_TSIM, tsim);
		spin_unlock_irqrestore(&igb->tmreg_lock, flags);
		return 0;

	case PTP_CLK_REQ_PEROUT:
		if (on) {
			pin = ptp_find_pin(igb->ptp_clock, PTP_PF_PEROUT,
					   rq->perout.index);
			if (pin < 0)
				return -EBUSY;
		}
		ts.tv_sec = rq->perout.period.sec;
		ts.tv_nsec = rq->perout.period.nsec;
		ns = timespec64_to_ns(&ts);
		ns = ns >> 1;
		if (on && ((ns <= 70000000LL) || (ns == 125000000LL) ||
			   (ns == 250000000LL) || (ns == 500000000LL))) {
			if (ns < 8LL)
				return -EINVAL;
			use_freq = 1;
		}
		ts = ns_to_timespec64(ns);
		if (rq->perout.index == 1) {
			if (use_freq) {
				tsauxc_mask = TSAUXC_EN_CLK1 | TSAUXC_ST1;
				tsim_mask = 0;
			} else {
				tsauxc_mask = TSAUXC_EN_TT1;
				tsim_mask = TSINTR_TT1;
			}
			trgttiml = E1000_TRGTTIML1;
			trgttimh = E1000_TRGTTIMH1;
			freqout = E1000_FREQOUT1;
		} else {
			if (use_freq) {
				tsauxc_mask = TSAUXC_EN_CLK0 | TSAUXC_ST0;
				tsim_mask = 0;
			} else {
				tsauxc_mask = TSAUXC_EN_TT0;
				tsim_mask = TSINTR_TT0;
			}
			trgttiml = E1000_TRGTTIML0;
			trgttimh = E1000_TRGTTIMH0;
			freqout = E1000_FREQOUT0;
		}
		spin_lock_irqsave(&igb->tmreg_lock, flags);
		tsauxc = rd32(E1000_TSAUXC);
		tsim = rd32(E1000_TSIM);
		if (rq->perout.index == 1) {
			tsauxc &= ~(TSAUXC_EN_TT1 | TSAUXC_EN_CLK1 | TSAUXC_ST1);
			tsim &= ~TSINTR_TT1;
		} else {
			tsauxc &= ~(TSAUXC_EN_TT0 | TSAUXC_EN_CLK0 | TSAUXC_ST0);
			tsim &= ~TSINTR_TT0;
		}
		if (on) {
			int i = rq->perout.index;
			igb_pin_perout(igb, i, pin, use_freq);
			igb->perout[i].start.tv_sec = rq->perout.start.sec;
			igb->perout[i].start.tv_nsec = rq->perout.start.nsec;
			igb->perout[i].period.tv_sec = ts.tv_sec;
			igb->perout[i].period.tv_nsec = ts.tv_nsec;
			wr32(trgttimh, rq->perout.start.sec);
			wr32(trgttiml, rq->perout.start.nsec);
			if (use_freq)
				wr32(freqout, ns);
			tsauxc |= tsauxc_mask;
			tsim |= tsim_mask;
		}
		wr32(E1000_TSAUXC, tsauxc);
		wr32(E1000_TSIM, tsim);
		spin_unlock_irqrestore(&igb->tmreg_lock, flags);
		return 0;

	case PTP_CLK_REQ_PPS:
		spin_lock_irqsave(&igb->tmreg_lock, flags);
		tsim = rd32(E1000_TSIM);
		if (on)
			tsim |= TSINTR_SYS_WRAP;
		else
			tsim &= ~TSINTR_SYS_WRAP;
		wr32(E1000_TSIM, tsim);
		spin_unlock_irqrestore(&igb->tmreg_lock, flags);
		return 0;
	}

	return -EOPNOTSUPP;
}

static int igb_ptp_feature_enable(struct ptp_clock_info *ptp,
				  struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

static int igb_ptp_verify_pin(struct ptp_clock_info *ptp, unsigned int pin,
			      enum ptp_pin_function func, unsigned int chan)
{
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_EXTTS:
	case PTP_PF_PEROUT:
		break;
	case PTP_PF_PHYSYNC:
		return -1;
	}
	return 0;
}

/**
 * igb_ptp_tx_work
 * @work: pointer to work struct
 *
 * This work function polls the TSYNCTXCTL valid bit to determine when a
 * timestamp has been taken for the current stored skb.
 **/
static void igb_ptp_tx_work(struct work_struct *work)
{
	struct igb_adapter *adapter = container_of(work, struct igb_adapter,
						   ptp_tx_work);
	struct e1000_hw *hw = &adapter->hw;
	u32 tsynctxctl;

	if (!adapter->ptp_tx_skb)
		return;

	if (time_is_before_jiffies(adapter->ptp_tx_start +
				   IGB_PTP_TX_TIMEOUT)) {
		dev_kfree_skb_any(adapter->ptp_tx_skb);
		adapter->ptp_tx_skb = NULL;
		clear_bit_unlock(__IGB_PTP_TX_IN_PROGRESS, &adapter->state);
		adapter->tx_hwtstamp_timeouts++;
		dev_warn(&adapter->pdev->dev, "clearing Tx timestamp hang\n");
		return;
	}

	tsynctxctl = rd32(E1000_TSYNCTXCTL);
	if (tsynctxctl & E1000_TSYNCTXCTL_VALID)
		igb_ptp_tx_hwtstamp(adapter);
	else
		/* reschedule to check later */
		schedule_work(&adapter->ptp_tx_work);
}

static void igb_ptp_overflow_check(struct work_struct *work)
{
	struct igb_adapter *igb =
		container_of(work, struct igb_adapter, ptp_overflow_work.work);
	struct timespec64 ts;

	igb->ptp_caps.gettime64(&igb->ptp_caps, &ts);

	pr_debug("igb overflow check at %lld.%09lu\n",
		 (long long) ts.tv_sec, ts.tv_nsec);

	schedule_delayed_work(&igb->ptp_overflow_work,
			      IGB_SYSTIM_OVERFLOW_PERIOD);
}

/**
 * igb_ptp_rx_hang - detect error case when Rx timestamp registers latched
 * @adapter: private network adapter structure
 *
 * This watchdog task is scheduled to detect error case where hardware has
 * dropped an Rx packet that was timestamped when the ring is full. The
 * particular error is rare but leaves the device in a state unable to timestamp
 * any future packets.
 **/
void igb_ptp_rx_hang(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 tsyncrxctl = rd32(E1000_TSYNCRXCTL);
	unsigned long rx_event;

	/* Other hardware uses per-packet timestamps */
	if (hw->mac.type != e1000_82576)
		return;

	/* If we don't have a valid timestamp in the registers, just update the
	 * timeout counter and exit
	 */
	if (!(tsyncrxctl & E1000_TSYNCRXCTL_VALID)) {
		adapter->last_rx_ptp_check = jiffies;
		return;
	}

	/* Determine the most recent watchdog or rx_timestamp event */
	rx_event = adapter->last_rx_ptp_check;
	if (time_after(adapter->last_rx_timestamp, rx_event))
		rx_event = adapter->last_rx_timestamp;

	/* Only need to read the high RXSTMP register to clear the lock */
	if (time_is_before_jiffies(rx_event + 5 * HZ)) {
		rd32(E1000_RXSTMPH);
		adapter->last_rx_ptp_check = jiffies;
		adapter->rx_hwtstamp_cleared++;
		dev_warn(&adapter->pdev->dev, "clearing Rx timestamp hang\n");
	}
}

/**
 * igb_ptp_tx_hwtstamp - utility function which checks for TX time stamp
 * @adapter: Board private structure.
 *
 * If we were asked to do hardware stamping and such a time stamp is
 * available, then it must have been for this skb here because we only
 * allow only one such packet into the queue.
 **/
static void igb_ptp_tx_hwtstamp(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct skb_shared_hwtstamps shhwtstamps;
	u64 regval;
	int adjust = 0;

	regval = rd32(E1000_TXSTMPL);
	regval |= (u64)rd32(E1000_TXSTMPH) << 32;

	igb_ptp_systim_to_hwtstamp(adapter, &shhwtstamps, regval);
	/* adjust timestamp for the TX latency based on link speed */
	if (adapter->hw.mac.type == e1000_i210) {
		switch (adapter->link_speed) {
		case SPEED_10:
			adjust = IGB_I210_TX_LATENCY_10;
			break;
		case SPEED_100:
			adjust = IGB_I210_TX_LATENCY_100;
			break;
		case SPEED_1000:
			adjust = IGB_I210_TX_LATENCY_1000;
			break;
		}
	}

	shhwtstamps.hwtstamp =
		ktime_add_ns(shhwtstamps.hwtstamp, adjust);

	skb_tstamp_tx(adapter->ptp_tx_skb, &shhwtstamps);
	dev_kfree_skb_any(adapter->ptp_tx_skb);
	adapter->ptp_tx_skb = NULL;
	clear_bit_unlock(__IGB_PTP_TX_IN_PROGRESS, &adapter->state);
}

/**
 * igb_ptp_rx_pktstamp - retrieve Rx per packet timestamp
 * @q_vector: Pointer to interrupt specific structure
 * @va: Pointer to address containing Rx buffer
 * @skb: Buffer containing timestamp and packet
 *
 * This function is meant to retrieve a timestamp from the first buffer of an
 * incoming frame.  The value is stored in little endian format starting on
 * byte 8.
 **/
void igb_ptp_rx_pktstamp(struct igb_q_vector *q_vector,
			 unsigned char *va,
			 struct sk_buff *skb)
{
	__le64 *regval = (__le64 *)va;
	struct igb_adapter *adapter = q_vector->adapter;
	int adjust = 0;

	/* The timestamp is recorded in little endian format.
	 * DWORD: 0        1        2        3
	 * Field: Reserved Reserved SYSTIML  SYSTIMH
	 */
	igb_ptp_systim_to_hwtstamp(adapter, skb_hwtstamps(skb),
				   le64_to_cpu(regval[1]));

	/* adjust timestamp for the RX latency based on link speed */
	if (adapter->hw.mac.type == e1000_i210) {
		switch (adapter->link_speed) {
		case SPEED_10:
			adjust = IGB_I210_RX_LATENCY_10;
			break;
		case SPEED_100:
			adjust = IGB_I210_RX_LATENCY_100;
			break;
		case SPEED_1000:
			adjust = IGB_I210_RX_LATENCY_1000;
			break;
		}
	}
	skb_hwtstamps(skb)->hwtstamp =
		ktime_sub_ns(skb_hwtstamps(skb)->hwtstamp, adjust);
}

/**
 * igb_ptp_rx_rgtstamp - retrieve Rx timestamp stored in register
 * @q_vector: Pointer to interrupt specific structure
 * @skb: Buffer containing timestamp and packet
 *
 * This function is meant to retrieve a timestamp from the internal registers
 * of the adapter and store it in the skb.
 **/
void igb_ptp_rx_rgtstamp(struct igb_q_vector *q_vector,
			 struct sk_buff *skb)
{
	struct igb_adapter *adapter = q_vector->adapter;
	struct e1000_hw *hw = &adapter->hw;
	u64 regval;
	int adjust = 0;

	/* If this bit is set, then the RX registers contain the time stamp. No
	 * other packet will be time stamped until we read these registers, so
	 * read the registers to make them available again. Because only one
	 * packet can be time stamped at a time, we know that the register
	 * values must belong to this one here and therefore we don't need to
	 * compare any of the additional attributes stored for it.
	 *
	 * If nothing went wrong, then it should have a shared tx_flags that we
	 * can turn into a skb_shared_hwtstamps.
	 */
	if (!(rd32(E1000_TSYNCRXCTL) & E1000_TSYNCRXCTL_VALID))
		return;

	regval = rd32(E1000_RXSTMPL);
	regval |= (u64)rd32(E1000_RXSTMPH) << 32;

	igb_ptp_systim_to_hwtstamp(adapter, skb_hwtstamps(skb), regval);

	/* adjust timestamp for the RX latency based on link speed */
	if (adapter->hw.mac.type == e1000_i210) {
		switch (adapter->link_speed) {
		case SPEED_10:
			adjust = IGB_I210_RX_LATENCY_10;
			break;
		case SPEED_100:
			adjust = IGB_I210_RX_LATENCY_100;
			break;
		case SPEED_1000:
			adjust = IGB_I210_RX_LATENCY_1000;
			break;
		}
	}
	skb_hwtstamps(skb)->hwtstamp =
		ktime_sub_ns(skb_hwtstamps(skb)->hwtstamp, adjust);

	/* Update the last_rx_timestamp timer in order to enable watchdog check
	 * for error case of latched timestamp on a dropped packet.
	 */
	adapter->last_rx_timestamp = jiffies;
}

/**
 * igb_ptp_get_ts_config - get hardware time stamping config
 * @netdev:
 * @ifreq:
 *
 * Get the hwtstamp_config settings to return to the user. Rather than attempt
 * to deconstruct the settings from the registers, just return a shadow copy
 * of the last known settings.
 **/
int igb_ptp_get_ts_config(struct net_device *netdev, struct ifreq *ifr)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct hwtstamp_config *config = &adapter->tstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/**
 * igb_ptp_set_timestamp_mode - setup hardware for timestamping
 * @adapter: networking device structure
 * @config: hwtstamp configuration
 *
 * Outgoing time stamping can be enabled and disabled. Play nice and
 * disable it when requested, although it shouldn't case any overhead
 * when no packet needs it. At most one packet in the queue may be
 * marked for time stamping, otherwise it would be impossible to tell
 * for sure to which packet the hardware time stamp belongs.
 *
 * Incoming time stamping has to be configured via the hardware
 * filters. Not all combinations are supported, in particular event
 * type has to be specified. Matching the kind of event packet is
 * not supported, with the exception of "all V2 events regardless of
 * level 2 or 4".
 */
static int igb_ptp_set_timestamp_mode(struct igb_adapter *adapter,
				      struct hwtstamp_config *config)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 tsync_tx_ctl = E1000_TSYNCTXCTL_ENABLED;
	u32 tsync_rx_ctl = E1000_TSYNCRXCTL_ENABLED;
	u32 tsync_rx_cfg = 0;
	bool is_l4 = false;
	bool is_l2 = false;
	u32 regval;

	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		tsync_tx_ctl = 0;
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tsync_rx_ctl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_cfg = E1000_TSYNCRXCFG_PTP_V1_SYNC_MESSAGE;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_cfg = E1000_TSYNCRXCFG_PTP_V1_DELAY_REQ_MESSAGE;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_EVENT_V2;
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		is_l2 = true;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_ALL:
		/* 82576 cannot timestamp all packets, which it needs to do to
		 * support both V1 Sync and Delay_Req messages
		 */
		if (hw->mac.type != e1000_82576) {
			tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_ALL;
			config->rx_filter = HWTSTAMP_FILTER_ALL;
			break;
		}
		/* fall through */
	default:
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	if (hw->mac.type == e1000_82575) {
		if (tsync_rx_ctl | tsync_tx_ctl)
			return -EINVAL;
		return 0;
	}

	/* Per-packet timestamping only works if all packets are
	 * timestamped, so enable timestamping in all packets as
	 * long as one Rx filter was configured.
	 */
	if ((hw->mac.type >= e1000_82580) && tsync_rx_ctl) {
		tsync_rx_ctl = E1000_TSYNCRXCTL_ENABLED;
		tsync_rx_ctl |= E1000_TSYNCRXCTL_TYPE_ALL;
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		is_l2 = true;
		is_l4 = true;

		if ((hw->mac.type == e1000_i210) ||
		    (hw->mac.type == e1000_i211)) {
			regval = rd32(E1000_RXPBS);
			regval |= E1000_RXPBS_CFG_TS_EN;
			wr32(E1000_RXPBS, regval);
		}
	}

	/* enable/disable TX */
	regval = rd32(E1000_TSYNCTXCTL);
	regval &= ~E1000_TSYNCTXCTL_ENABLED;
	regval |= tsync_tx_ctl;
	wr32(E1000_TSYNCTXCTL, regval);

	/* enable/disable RX */
	regval = rd32(E1000_TSYNCRXCTL);
	regval &= ~(E1000_TSYNCRXCTL_ENABLED | E1000_TSYNCRXCTL_TYPE_MASK);
	regval |= tsync_rx_ctl;
	wr32(E1000_TSYNCRXCTL, regval);

	/* define which PTP packets are time stamped */
	wr32(E1000_TSYNCRXCFG, tsync_rx_cfg);

	/* define ethertype filter for timestamped packets */
	if (is_l2)
		wr32(E1000_ETQF(3),
		     (E1000_ETQF_FILTER_ENABLE | /* enable filter */
		      E1000_ETQF_1588 | /* enable timestamping */
		      ETH_P_1588));     /* 1588 eth protocol type */
	else
		wr32(E1000_ETQF(3), 0);

	/* L4 Queue Filter[3]: filter by destination port and protocol */
	if (is_l4) {
		u32 ftqf = (IPPROTO_UDP /* UDP */
			| E1000_FTQF_VF_BP /* VF not compared */
			| E1000_FTQF_1588_TIME_STAMP /* Enable Timestamping */
			| E1000_FTQF_MASK); /* mask all inputs */
		ftqf &= ~E1000_FTQF_MASK_PROTO_BP; /* enable protocol check */

		wr32(E1000_IMIR(3), htons(PTP_EV_PORT));
		wr32(E1000_IMIREXT(3),
		     (E1000_IMIREXT_SIZE_BP | E1000_IMIREXT_CTRL_BP));
		if (hw->mac.type == e1000_82576) {
			/* enable source port check */
			wr32(E1000_SPQF(3), htons(PTP_EV_PORT));
			ftqf &= ~E1000_FTQF_MASK_SOURCE_PORT_BP;
		}
		wr32(E1000_FTQF(3), ftqf);
	} else {
		wr32(E1000_FTQF(3), E1000_FTQF_MASK);
	}
	wrfl();

	/* clear TX/RX time stamp registers, just to be sure */
	regval = rd32(E1000_TXSTMPL);
	regval = rd32(E1000_TXSTMPH);
	regval = rd32(E1000_RXSTMPL);
	regval = rd32(E1000_RXSTMPH);

	return 0;
}

/**
 * igb_ptp_set_ts_config - set hardware time stamping config
 * @netdev:
 * @ifreq:
 *
 **/
int igb_ptp_set_ts_config(struct net_device *netdev, struct ifreq *ifr)
{
	struct igb_adapter *adapter = netdev_priv(netdev);
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = igb_ptp_set_timestamp_mode(adapter, &config);
	if (err)
		return err;

	/* save these settings for future reference */
	memcpy(&adapter->tstamp_config, &config,
	       sizeof(adapter->tstamp_config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/**
 * igb_ptp_init - Initialize PTP functionality
 * @adapter: Board private structure
 *
 * This function is called at device probe to initialize the PTP
 * functionality.
 */
void igb_ptp_init(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	int i;

	switch (hw->mac.type) {
	case e1000_82576:
		snprintf(adapter->ptp_caps.name, 16, "%pm", netdev->dev_addr);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 999999881;
		adapter->ptp_caps.n_ext_ts = 0;
		adapter->ptp_caps.pps = 0;
		adapter->ptp_caps.adjfreq = igb_ptp_adjfreq_82576;
		adapter->ptp_caps.adjtime = igb_ptp_adjtime_82576;
		adapter->ptp_caps.gettime64 = igb_ptp_gettime_82576;
		adapter->ptp_caps.settime64 = igb_ptp_settime_82576;
		adapter->ptp_caps.enable = igb_ptp_feature_enable;
		adapter->cc.read = igb_ptp_read_82576;
		adapter->cc.mask = CYCLECOUNTER_MASK(64);
		adapter->cc.mult = 1;
		adapter->cc.shift = IGB_82576_TSYNC_SHIFT;
		adapter->ptp_flags |= IGB_PTP_OVERFLOW_CHECK;
		break;
	case e1000_82580:
	case e1000_i354:
	case e1000_i350:
		snprintf(adapter->ptp_caps.name, 16, "%pm", netdev->dev_addr);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 62499999;
		adapter->ptp_caps.n_ext_ts = 0;
		adapter->ptp_caps.pps = 0;
		adapter->ptp_caps.adjfreq = igb_ptp_adjfreq_82580;
		adapter->ptp_caps.adjtime = igb_ptp_adjtime_82576;
		adapter->ptp_caps.gettime64 = igb_ptp_gettime_82576;
		adapter->ptp_caps.settime64 = igb_ptp_settime_82576;
		adapter->ptp_caps.enable = igb_ptp_feature_enable;
		adapter->cc.read = igb_ptp_read_82580;
		adapter->cc.mask = CYCLECOUNTER_MASK(IGB_NBITS_82580);
		adapter->cc.mult = 1;
		adapter->cc.shift = 0;
		adapter->ptp_flags |= IGB_PTP_OVERFLOW_CHECK;
		break;
	case e1000_i210:
	case e1000_i211:
		for (i = 0; i < IGB_N_SDP; i++) {
			struct ptp_pin_desc *ppd = &adapter->sdp_config[i];

			snprintf(ppd->name, sizeof(ppd->name), "SDP%d", i);
			ppd->index = i;
			ppd->func = PTP_PF_NONE;
		}
		snprintf(adapter->ptp_caps.name, 16, "%pm", netdev->dev_addr);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 62499999;
		adapter->ptp_caps.n_ext_ts = IGB_N_EXTTS;
		adapter->ptp_caps.n_per_out = IGB_N_PEROUT;
		adapter->ptp_caps.n_pins = IGB_N_SDP;
		adapter->ptp_caps.pps = 1;
		adapter->ptp_caps.pin_config = adapter->sdp_config;
		adapter->ptp_caps.adjfreq = igb_ptp_adjfreq_82580;
		adapter->ptp_caps.adjtime = igb_ptp_adjtime_i210;
		adapter->ptp_caps.gettime64 = igb_ptp_gettime_i210;
		adapter->ptp_caps.settime64 = igb_ptp_settime_i210;
		adapter->ptp_caps.enable = igb_ptp_feature_enable_i210;
		adapter->ptp_caps.verify = igb_ptp_verify_pin;
		break;
	default:
		adapter->ptp_clock = NULL;
		return;
	}

	spin_lock_init(&adapter->tmreg_lock);
	INIT_WORK(&adapter->ptp_tx_work, igb_ptp_tx_work);

	if (adapter->ptp_flags & IGB_PTP_OVERFLOW_CHECK)
		INIT_DELAYED_WORK(&adapter->ptp_overflow_work,
				  igb_ptp_overflow_check);

	adapter->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	adapter->tstamp_config.tx_type = HWTSTAMP_TX_OFF;

	igb_ptp_reset(adapter);

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_caps,
						&adapter->pdev->dev);
	if (IS_ERR(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		dev_err(&adapter->pdev->dev, "ptp_clock_register failed\n");
	} else {
		dev_info(&adapter->pdev->dev, "added PHC on %s\n",
			 adapter->netdev->name);
		adapter->ptp_flags |= IGB_PTP_ENABLED;
	}
}

/**
 * igb_ptp_suspend - Disable PTP work items and prepare for suspend
 * @adapter: Board private structure
 *
 * This function stops the overflow check work and PTP Tx timestamp work, and
 * will prepare the device for OS suspend.
 */
void igb_ptp_suspend(struct igb_adapter *adapter)
{
	if (!(adapter->ptp_flags & IGB_PTP_ENABLED))
		return;

	if (adapter->ptp_flags & IGB_PTP_OVERFLOW_CHECK)
		cancel_delayed_work_sync(&adapter->ptp_overflow_work);

	cancel_work_sync(&adapter->ptp_tx_work);
	if (adapter->ptp_tx_skb) {
		dev_kfree_skb_any(adapter->ptp_tx_skb);
		adapter->ptp_tx_skb = NULL;
		clear_bit_unlock(__IGB_PTP_TX_IN_PROGRESS, &adapter->state);
	}
}

/**
 * igb_ptp_stop - Disable PTP device and stop the overflow check.
 * @adapter: Board private structure.
 *
 * This function stops the PTP support and cancels the delayed work.
 **/
void igb_ptp_stop(struct igb_adapter *adapter)
{
	igb_ptp_suspend(adapter);

	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		dev_info(&adapter->pdev->dev, "removed PHC on %s\n",
			 adapter->netdev->name);
		adapter->ptp_flags &= ~IGB_PTP_ENABLED;
	}
}

/**
 * igb_ptp_reset - Re-enable the adapter for PTP following a reset.
 * @adapter: Board private structure.
 *
 * This function handles the reset work required to re-enable the PTP device.
 **/
void igb_ptp_reset(struct igb_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	unsigned long flags;

	/* reset the tstamp_config */
	igb_ptp_set_timestamp_mode(adapter, &adapter->tstamp_config);

	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	switch (adapter->hw.mac.type) {
	case e1000_82576:
		/* Dial the nominal frequency. */
		wr32(E1000_TIMINCA, INCPERIOD_82576 | INCVALUE_82576);
		break;
	case e1000_82580:
	case e1000_i354:
	case e1000_i350:
	case e1000_i210:
	case e1000_i211:
		wr32(E1000_TSAUXC, 0x0);
		wr32(E1000_TSSDP, 0x0);
		wr32(E1000_TSIM, TSYNC_INTERRUPTS);
		wr32(E1000_IMS, E1000_IMS_TS);
		break;
	default:
		/* No work to do. */
		goto out;
	}

	/* Re-initialize the timer. */
	if ((hw->mac.type == e1000_i210) || (hw->mac.type == e1000_i211)) {
		struct timespec64 ts = ktime_to_timespec64(ktime_get_real());

		igb_ptp_write_i210(adapter, &ts);
	} else {
		timecounter_init(&adapter->tc, &adapter->cc,
				 ktime_to_ns(ktime_get_real()));
	}
out:
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	wrfl();

	if (adapter->ptp_flags & IGB_PTP_OVERFLOW_CHECK)
		schedule_delayed_work(&adapter->ptp_overflow_work,
				      IGB_SYSTIM_OVERFLOW_PERIOD);
}
