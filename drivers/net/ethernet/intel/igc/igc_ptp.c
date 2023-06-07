// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2019 Intel Corporation */

#include "igc.h"

#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/ptp_classify.h>
#include <linux/clocksource.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/iopoll.h>

#define INCVALUE_MASK		0x7fffffff
#define ISGN			0x80000000

#define IGC_PTP_TX_TIMEOUT		(HZ * 15)

#define IGC_PTM_STAT_SLEEP		2
#define IGC_PTM_STAT_TIMEOUT		100

/* SYSTIM read access for I225 */
void igc_ptp_read(struct igc_adapter *adapter, struct timespec64 *ts)
{
	struct igc_hw *hw = &adapter->hw;
	u32 sec, nsec;

	/* The timestamp is latched when SYSTIML is read. */
	nsec = rd32(IGC_SYSTIML);
	sec = rd32(IGC_SYSTIMH);

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static void igc_ptp_write_i225(struct igc_adapter *adapter,
			       const struct timespec64 *ts)
{
	struct igc_hw *hw = &adapter->hw;

	wr32(IGC_SYSTIML, ts->tv_nsec);
	wr32(IGC_SYSTIMH, ts->tv_sec);
}

static int igc_ptp_adjfine_i225(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct igc_adapter *igc = container_of(ptp, struct igc_adapter,
					       ptp_caps);
	struct igc_hw *hw = &igc->hw;
	int neg_adj = 0;
	u64 rate;
	u32 inca;

	if (scaled_ppm < 0) {
		neg_adj = 1;
		scaled_ppm = -scaled_ppm;
	}
	rate = scaled_ppm;
	rate <<= 14;
	rate = div_u64(rate, 78125);

	inca = rate & INCVALUE_MASK;
	if (neg_adj)
		inca |= ISGN;

	wr32(IGC_TIMINCA, inca);

	return 0;
}

static int igc_ptp_adjtime_i225(struct ptp_clock_info *ptp, s64 delta)
{
	struct igc_adapter *igc = container_of(ptp, struct igc_adapter,
					       ptp_caps);
	struct timespec64 now, then = ns_to_timespec64(delta);
	unsigned long flags;

	spin_lock_irqsave(&igc->tmreg_lock, flags);

	igc_ptp_read(igc, &now);
	now = timespec64_add(now, then);
	igc_ptp_write_i225(igc, (const struct timespec64 *)&now);

	spin_unlock_irqrestore(&igc->tmreg_lock, flags);

	return 0;
}

static int igc_ptp_gettimex64_i225(struct ptp_clock_info *ptp,
				   struct timespec64 *ts,
				   struct ptp_system_timestamp *sts)
{
	struct igc_adapter *igc = container_of(ptp, struct igc_adapter,
					       ptp_caps);
	struct igc_hw *hw = &igc->hw;
	unsigned long flags;

	spin_lock_irqsave(&igc->tmreg_lock, flags);

	ptp_read_system_prets(sts);
	ts->tv_nsec = rd32(IGC_SYSTIML);
	ts->tv_sec = rd32(IGC_SYSTIMH);
	ptp_read_system_postts(sts);

	spin_unlock_irqrestore(&igc->tmreg_lock, flags);

	return 0;
}

static int igc_ptp_settime_i225(struct ptp_clock_info *ptp,
				const struct timespec64 *ts)
{
	struct igc_adapter *igc = container_of(ptp, struct igc_adapter,
					       ptp_caps);
	unsigned long flags;

	spin_lock_irqsave(&igc->tmreg_lock, flags);

	igc_ptp_write_i225(igc, ts);

	spin_unlock_irqrestore(&igc->tmreg_lock, flags);

	return 0;
}

static void igc_pin_direction(int pin, int input, u32 *ctrl, u32 *ctrl_ext)
{
	u32 *ptr = pin < 2 ? ctrl : ctrl_ext;
	static const u32 mask[IGC_N_SDP] = {
		IGC_CTRL_SDP0_DIR,
		IGC_CTRL_SDP1_DIR,
		IGC_CTRL_EXT_SDP2_DIR,
		IGC_CTRL_EXT_SDP3_DIR,
	};

	if (input)
		*ptr &= ~mask[pin];
	else
		*ptr |= mask[pin];
}

static void igc_pin_perout(struct igc_adapter *igc, int chan, int pin, int freq)
{
	static const u32 igc_aux0_sel_sdp[IGC_N_SDP] = {
		IGC_AUX0_SEL_SDP0, IGC_AUX0_SEL_SDP1, IGC_AUX0_SEL_SDP2, IGC_AUX0_SEL_SDP3,
	};
	static const u32 igc_aux1_sel_sdp[IGC_N_SDP] = {
		IGC_AUX1_SEL_SDP0, IGC_AUX1_SEL_SDP1, IGC_AUX1_SEL_SDP2, IGC_AUX1_SEL_SDP3,
	};
	static const u32 igc_ts_sdp_en[IGC_N_SDP] = {
		IGC_TS_SDP0_EN, IGC_TS_SDP1_EN, IGC_TS_SDP2_EN, IGC_TS_SDP3_EN,
	};
	static const u32 igc_ts_sdp_sel_tt0[IGC_N_SDP] = {
		IGC_TS_SDP0_SEL_TT0, IGC_TS_SDP1_SEL_TT0,
		IGC_TS_SDP2_SEL_TT0, IGC_TS_SDP3_SEL_TT0,
	};
	static const u32 igc_ts_sdp_sel_tt1[IGC_N_SDP] = {
		IGC_TS_SDP0_SEL_TT1, IGC_TS_SDP1_SEL_TT1,
		IGC_TS_SDP2_SEL_TT1, IGC_TS_SDP3_SEL_TT1,
	};
	static const u32 igc_ts_sdp_sel_fc0[IGC_N_SDP] = {
		IGC_TS_SDP0_SEL_FC0, IGC_TS_SDP1_SEL_FC0,
		IGC_TS_SDP2_SEL_FC0, IGC_TS_SDP3_SEL_FC0,
	};
	static const u32 igc_ts_sdp_sel_fc1[IGC_N_SDP] = {
		IGC_TS_SDP0_SEL_FC1, IGC_TS_SDP1_SEL_FC1,
		IGC_TS_SDP2_SEL_FC1, IGC_TS_SDP3_SEL_FC1,
	};
	static const u32 igc_ts_sdp_sel_clr[IGC_N_SDP] = {
		IGC_TS_SDP0_SEL_FC1, IGC_TS_SDP1_SEL_FC1,
		IGC_TS_SDP2_SEL_FC1, IGC_TS_SDP3_SEL_FC1,
	};
	struct igc_hw *hw = &igc->hw;
	u32 ctrl, ctrl_ext, tssdp = 0;

	ctrl = rd32(IGC_CTRL);
	ctrl_ext = rd32(IGC_CTRL_EXT);
	tssdp = rd32(IGC_TSSDP);

	igc_pin_direction(pin, 0, &ctrl, &ctrl_ext);

	/* Make sure this pin is not enabled as an input. */
	if ((tssdp & IGC_AUX0_SEL_SDP3) == igc_aux0_sel_sdp[pin])
		tssdp &= ~IGC_AUX0_TS_SDP_EN;

	if ((tssdp & IGC_AUX1_SEL_SDP3) == igc_aux1_sel_sdp[pin])
		tssdp &= ~IGC_AUX1_TS_SDP_EN;

	tssdp &= ~igc_ts_sdp_sel_clr[pin];
	if (freq) {
		if (chan == 1)
			tssdp |= igc_ts_sdp_sel_fc1[pin];
		else
			tssdp |= igc_ts_sdp_sel_fc0[pin];
	} else {
		if (chan == 1)
			tssdp |= igc_ts_sdp_sel_tt1[pin];
		else
			tssdp |= igc_ts_sdp_sel_tt0[pin];
	}
	tssdp |= igc_ts_sdp_en[pin];

	wr32(IGC_TSSDP, tssdp);
	wr32(IGC_CTRL, ctrl);
	wr32(IGC_CTRL_EXT, ctrl_ext);
}

static void igc_pin_extts(struct igc_adapter *igc, int chan, int pin)
{
	static const u32 igc_aux0_sel_sdp[IGC_N_SDP] = {
		IGC_AUX0_SEL_SDP0, IGC_AUX0_SEL_SDP1, IGC_AUX0_SEL_SDP2, IGC_AUX0_SEL_SDP3,
	};
	static const u32 igc_aux1_sel_sdp[IGC_N_SDP] = {
		IGC_AUX1_SEL_SDP0, IGC_AUX1_SEL_SDP1, IGC_AUX1_SEL_SDP2, IGC_AUX1_SEL_SDP3,
	};
	static const u32 igc_ts_sdp_en[IGC_N_SDP] = {
		IGC_TS_SDP0_EN, IGC_TS_SDP1_EN, IGC_TS_SDP2_EN, IGC_TS_SDP3_EN,
	};
	struct igc_hw *hw = &igc->hw;
	u32 ctrl, ctrl_ext, tssdp = 0;

	ctrl = rd32(IGC_CTRL);
	ctrl_ext = rd32(IGC_CTRL_EXT);
	tssdp = rd32(IGC_TSSDP);

	igc_pin_direction(pin, 1, &ctrl, &ctrl_ext);

	/* Make sure this pin is not enabled as an output. */
	tssdp &= ~igc_ts_sdp_en[pin];

	if (chan == 1) {
		tssdp &= ~IGC_AUX1_SEL_SDP3;
		tssdp |= igc_aux1_sel_sdp[pin] | IGC_AUX1_TS_SDP_EN;
	} else {
		tssdp &= ~IGC_AUX0_SEL_SDP3;
		tssdp |= igc_aux0_sel_sdp[pin] | IGC_AUX0_TS_SDP_EN;
	}

	wr32(IGC_TSSDP, tssdp);
	wr32(IGC_CTRL, ctrl);
	wr32(IGC_CTRL_EXT, ctrl_ext);
}

static int igc_ptp_feature_enable_i225(struct ptp_clock_info *ptp,
				       struct ptp_clock_request *rq, int on)
{
	struct igc_adapter *igc =
		container_of(ptp, struct igc_adapter, ptp_caps);
	struct igc_hw *hw = &igc->hw;
	unsigned long flags;
	struct timespec64 ts;
	int use_freq = 0, pin = -1;
	u32 tsim, tsauxc, tsauxc_mask, tsim_mask, trgttiml, trgttimh, freqout;
	s64 ns;

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		/* Reject requests with unsupported flags */
		if (rq->extts.flags & ~(PTP_ENABLE_FEATURE |
					PTP_RISING_EDGE |
					PTP_FALLING_EDGE |
					PTP_STRICT_FLAGS))
			return -EOPNOTSUPP;

		/* Reject requests failing to enable both edges. */
		if ((rq->extts.flags & PTP_STRICT_FLAGS) &&
		    (rq->extts.flags & PTP_ENABLE_FEATURE) &&
		    (rq->extts.flags & PTP_EXTTS_EDGES) != PTP_EXTTS_EDGES)
			return -EOPNOTSUPP;

		if (on) {
			pin = ptp_find_pin(igc->ptp_clock, PTP_PF_EXTTS,
					   rq->extts.index);
			if (pin < 0)
				return -EBUSY;
		}
		if (rq->extts.index == 1) {
			tsauxc_mask = IGC_TSAUXC_EN_TS1;
			tsim_mask = IGC_TSICR_AUTT1;
		} else {
			tsauxc_mask = IGC_TSAUXC_EN_TS0;
			tsim_mask = IGC_TSICR_AUTT0;
		}
		spin_lock_irqsave(&igc->tmreg_lock, flags);
		tsauxc = rd32(IGC_TSAUXC);
		tsim = rd32(IGC_TSIM);
		if (on) {
			igc_pin_extts(igc, rq->extts.index, pin);
			tsauxc |= tsauxc_mask;
			tsim |= tsim_mask;
		} else {
			tsauxc &= ~tsauxc_mask;
			tsim &= ~tsim_mask;
		}
		wr32(IGC_TSAUXC, tsauxc);
		wr32(IGC_TSIM, tsim);
		spin_unlock_irqrestore(&igc->tmreg_lock, flags);
		return 0;

	case PTP_CLK_REQ_PEROUT:
		/* Reject requests with unsupported flags */
		if (rq->perout.flags)
			return -EOPNOTSUPP;

		if (on) {
			pin = ptp_find_pin(igc->ptp_clock, PTP_PF_PEROUT,
					   rq->perout.index);
			if (pin < 0)
				return -EBUSY;
		}
		ts.tv_sec = rq->perout.period.sec;
		ts.tv_nsec = rq->perout.period.nsec;
		ns = timespec64_to_ns(&ts);
		ns = ns >> 1;
		if (on && (ns <= 70000000LL || ns == 125000000LL ||
			   ns == 250000000LL || ns == 500000000LL)) {
			if (ns < 8LL)
				return -EINVAL;
			use_freq = 1;
		}
		ts = ns_to_timespec64(ns);
		if (rq->perout.index == 1) {
			if (use_freq) {
				tsauxc_mask = IGC_TSAUXC_EN_CLK1 | IGC_TSAUXC_ST1;
				tsim_mask = 0;
			} else {
				tsauxc_mask = IGC_TSAUXC_EN_TT1;
				tsim_mask = IGC_TSICR_TT1;
			}
			trgttiml = IGC_TRGTTIML1;
			trgttimh = IGC_TRGTTIMH1;
			freqout = IGC_FREQOUT1;
		} else {
			if (use_freq) {
				tsauxc_mask = IGC_TSAUXC_EN_CLK0 | IGC_TSAUXC_ST0;
				tsim_mask = 0;
			} else {
				tsauxc_mask = IGC_TSAUXC_EN_TT0;
				tsim_mask = IGC_TSICR_TT0;
			}
			trgttiml = IGC_TRGTTIML0;
			trgttimh = IGC_TRGTTIMH0;
			freqout = IGC_FREQOUT0;
		}
		spin_lock_irqsave(&igc->tmreg_lock, flags);
		tsauxc = rd32(IGC_TSAUXC);
		tsim = rd32(IGC_TSIM);
		if (rq->perout.index == 1) {
			tsauxc &= ~(IGC_TSAUXC_EN_TT1 | IGC_TSAUXC_EN_CLK1 |
				    IGC_TSAUXC_ST1);
			tsim &= ~IGC_TSICR_TT1;
		} else {
			tsauxc &= ~(IGC_TSAUXC_EN_TT0 | IGC_TSAUXC_EN_CLK0 |
				    IGC_TSAUXC_ST0);
			tsim &= ~IGC_TSICR_TT0;
		}
		if (on) {
			int i = rq->perout.index;

			igc_pin_perout(igc, i, pin, use_freq);
			igc->perout[i].start.tv_sec = rq->perout.start.sec;
			igc->perout[i].start.tv_nsec = rq->perout.start.nsec;
			igc->perout[i].period.tv_sec = ts.tv_sec;
			igc->perout[i].period.tv_nsec = ts.tv_nsec;
			wr32(trgttimh, rq->perout.start.sec);
			/* For now, always select timer 0 as source. */
			wr32(trgttiml, rq->perout.start.nsec | IGC_TT_IO_TIMER_SEL_SYSTIM0);
			if (use_freq)
				wr32(freqout, ns);
			tsauxc |= tsauxc_mask;
			tsim |= tsim_mask;
		}
		wr32(IGC_TSAUXC, tsauxc);
		wr32(IGC_TSIM, tsim);
		spin_unlock_irqrestore(&igc->tmreg_lock, flags);
		return 0;

	case PTP_CLK_REQ_PPS:
		spin_lock_irqsave(&igc->tmreg_lock, flags);
		tsim = rd32(IGC_TSIM);
		if (on)
			tsim |= IGC_TSICR_SYS_WRAP;
		else
			tsim &= ~IGC_TSICR_SYS_WRAP;
		igc->pps_sys_wrap_on = on;
		wr32(IGC_TSIM, tsim);
		spin_unlock_irqrestore(&igc->tmreg_lock, flags);
		return 0;

	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int igc_ptp_verify_pin(struct ptp_clock_info *ptp, unsigned int pin,
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
 * igc_ptp_systim_to_hwtstamp - convert system time value to HW timestamp
 * @adapter: board private structure
 * @hwtstamps: timestamp structure to update
 * @systim: unsigned 64bit system time value
 *
 * We need to convert the system time value stored in the RX/TXSTMP registers
 * into a hwtstamp which can be used by the upper level timestamping functions.
 *
 * Returns 0 on success.
 **/
static int igc_ptp_systim_to_hwtstamp(struct igc_adapter *adapter,
				      struct skb_shared_hwtstamps *hwtstamps,
				      u64 systim)
{
	switch (adapter->hw.mac.type) {
	case igc_i225:
		memset(hwtstamps, 0, sizeof(*hwtstamps));
		/* Upper 32 bits contain s, lower 32 bits contain ns. */
		hwtstamps->hwtstamp = ktime_set(systim >> 32,
						systim & 0xFFFFFFFF);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/**
 * igc_ptp_rx_pktstamp - Retrieve timestamp from Rx packet buffer
 * @adapter: Pointer to adapter the packet buffer belongs to
 * @buf: Pointer to packet buffer
 *
 * This function retrieves the timestamp saved in the beginning of packet
 * buffer. While two timestamps are available, one in timer0 reference and the
 * other in timer1 reference, this function considers only the timestamp in
 * timer0 reference.
 *
 * Returns timestamp value.
 */
ktime_t igc_ptp_rx_pktstamp(struct igc_adapter *adapter, __le32 *buf)
{
	ktime_t timestamp;
	u32 secs, nsecs;
	int adjust;

	/* Timestamps are saved in little endian at the beginning of the packet
	 * buffer following the layout:
	 *
	 * DWORD: | 0              | 1              | 2              | 3              |
	 * Field: | Timer1 SYSTIML | Timer1 SYSTIMH | Timer0 SYSTIML | Timer0 SYSTIMH |
	 *
	 * SYSTIML holds the nanoseconds part while SYSTIMH holds the seconds
	 * part of the timestamp.
	 */
	nsecs = le32_to_cpu(buf[2]);
	secs = le32_to_cpu(buf[3]);

	timestamp = ktime_set(secs, nsecs);

	/* Adjust timestamp for the RX latency based on link speed */
	switch (adapter->link_speed) {
	case SPEED_10:
		adjust = IGC_I225_RX_LATENCY_10;
		break;
	case SPEED_100:
		adjust = IGC_I225_RX_LATENCY_100;
		break;
	case SPEED_1000:
		adjust = IGC_I225_RX_LATENCY_1000;
		break;
	case SPEED_2500:
		adjust = IGC_I225_RX_LATENCY_2500;
		break;
	default:
		adjust = 0;
		netdev_warn_once(adapter->netdev, "Imprecise timestamp\n");
		break;
	}

	return ktime_sub_ns(timestamp, adjust);
}

static void igc_ptp_disable_rx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 val;
	int i;

	wr32(IGC_TSYNCRXCTL, 0);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		val = rd32(IGC_SRRCTL(i));
		val &= ~IGC_SRRCTL_TIMESTAMP;
		wr32(IGC_SRRCTL(i), val);
	}

	val = rd32(IGC_RXPBS);
	val &= ~IGC_RXPBS_CFG_TS_EN;
	wr32(IGC_RXPBS, val);
}

static void igc_ptp_enable_rx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 val;
	int i;

	val = rd32(IGC_RXPBS);
	val |= IGC_RXPBS_CFG_TS_EN;
	wr32(IGC_RXPBS, val);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		val = rd32(IGC_SRRCTL(i));
		/* FIXME: For now, only support retrieving RX timestamps from
		 * timer 0.
		 */
		val |= IGC_SRRCTL_TIMER1SEL(0) | IGC_SRRCTL_TIMER0SEL(0) |
		       IGC_SRRCTL_TIMESTAMP;
		wr32(IGC_SRRCTL(i), val);
	}

	val = IGC_TSYNCRXCTL_ENABLED | IGC_TSYNCRXCTL_TYPE_ALL |
	      IGC_TSYNCRXCTL_RXSYNSIG;
	wr32(IGC_TSYNCRXCTL, val);
}

static void igc_ptp_clear_tx_tstamp(struct igc_adapter *adapter)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->ptp_tx_lock, flags);

	dev_kfree_skb_any(adapter->ptp_tx_skb);
	adapter->ptp_tx_skb = NULL;

	spin_unlock_irqrestore(&adapter->ptp_tx_lock, flags);
}

static void igc_ptp_disable_tx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	int i;

	/* Clear the flags first to avoid new packets to be enqueued
	 * for TX timestamping.
	 */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *tx_ring = adapter->tx_ring[i];

		clear_bit(IGC_RING_FLAG_TX_HWTSTAMP, &tx_ring->flags);
	}

	/* Now we can clean the pending TX timestamp requests. */
	igc_ptp_clear_tx_tstamp(adapter);

	wr32(IGC_TSYNCTXCTL, 0);
}

static void igc_ptp_enable_tx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	int i;

	wr32(IGC_TSYNCTXCTL, IGC_TSYNCTXCTL_ENABLED | IGC_TSYNCTXCTL_TXSYNSIG);

	/* Read TXSTMP registers to discard any timestamp previously stored. */
	rd32(IGC_TXSTMPL);
	rd32(IGC_TXSTMPH);

	/* The hardware is ready to accept TX timestamp requests,
	 * notify the transmit path.
	 */
	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *tx_ring = adapter->tx_ring[i];

		set_bit(IGC_RING_FLAG_TX_HWTSTAMP, &tx_ring->flags);
	}

}

/**
 * igc_ptp_set_timestamp_mode - setup hardware for timestamping
 * @adapter: networking device structure
 * @config: hwtstamp configuration
 *
 * Return: 0 in case of success, negative errno code otherwise.
 */
static int igc_ptp_set_timestamp_mode(struct igc_adapter *adapter,
				      struct hwtstamp_config *config)
{
	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		igc_ptp_disable_tx_timestamp(adapter);
		break;
	case HWTSTAMP_TX_ON:
		igc_ptp_enable_tx_timestamp(adapter);
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		igc_ptp_disable_rx_timestamp(adapter);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_NTP_ALL:
	case HWTSTAMP_FILTER_ALL:
		igc_ptp_enable_rx_timestamp(adapter);
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

/* Requires adapter->ptp_tx_lock held by caller. */
static void igc_ptp_tx_timeout(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	dev_kfree_skb_any(adapter->ptp_tx_skb);
	adapter->ptp_tx_skb = NULL;
	adapter->tx_hwtstamp_timeouts++;
	/* Clear the tx valid bit in TSYNCTXCTL register to enable interrupt. */
	rd32(IGC_TXSTMPH);
	netdev_warn(adapter->netdev, "Tx timestamp timeout\n");
}

void igc_ptp_tx_hang(struct igc_adapter *adapter)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->ptp_tx_lock, flags);

	if (!adapter->ptp_tx_skb)
		goto unlock;

	if (time_is_after_jiffies(adapter->ptp_tx_start + IGC_PTP_TX_TIMEOUT))
		goto unlock;

	igc_ptp_tx_timeout(adapter);

unlock:
	spin_unlock_irqrestore(&adapter->ptp_tx_lock, flags);
}

/**
 * igc_ptp_tx_hwtstamp - utility function which checks for TX time stamp
 * @adapter: Board private structure
 *
 * If we were asked to do hardware stamping and such a time stamp is
 * available, then it must have been for this skb here because we only
 * allow only one such packet into the queue.
 *
 * Context: Expects adapter->ptp_tx_lock to be held by caller.
 */
static void igc_ptp_tx_hwtstamp(struct igc_adapter *adapter)
{
	struct sk_buff *skb = adapter->ptp_tx_skb;
	struct skb_shared_hwtstamps shhwtstamps;
	struct igc_hw *hw = &adapter->hw;
	u32 tsynctxctl;
	int adjust = 0;
	u64 regval;

	if (WARN_ON_ONCE(!skb))
		return;

	tsynctxctl = rd32(IGC_TSYNCTXCTL);
	tsynctxctl &= IGC_TSYNCTXCTL_TXTT_0;
	if (tsynctxctl) {
		regval = rd32(IGC_TXSTMPL);
		regval |= (u64)rd32(IGC_TXSTMPH) << 32;
	} else {
		/* There's a bug in the hardware that could cause
		 * missing interrupts for TX timestamping. The issue
		 * is that for new interrupts to be triggered, the
		 * IGC_TXSTMPH_0 register must be read.
		 *
		 * To avoid discarding a valid timestamp that just
		 * happened at the "wrong" time, we need to confirm
		 * that there was no timestamp captured, we do that by
		 * assuming that no two timestamps in sequence have
		 * the same nanosecond value.
		 *
		 * So, we read the "low" register, read the "high"
		 * register (to latch a new timestamp) and read the
		 * "low" register again, if "old" and "new" versions
		 * of the "low" register are different, a valid
		 * timestamp was captured, we can read the "high"
		 * register again.
		 */
		u32 txstmpl_old, txstmpl_new;

		txstmpl_old = rd32(IGC_TXSTMPL);
		rd32(IGC_TXSTMPH);
		txstmpl_new = rd32(IGC_TXSTMPL);

		if (txstmpl_old == txstmpl_new)
			return;

		regval = txstmpl_new;
		regval |= (u64)rd32(IGC_TXSTMPH) << 32;
	}
	if (igc_ptp_systim_to_hwtstamp(adapter, &shhwtstamps, regval))
		return;

	switch (adapter->link_speed) {
	case SPEED_10:
		adjust = IGC_I225_TX_LATENCY_10;
		break;
	case SPEED_100:
		adjust = IGC_I225_TX_LATENCY_100;
		break;
	case SPEED_1000:
		adjust = IGC_I225_TX_LATENCY_1000;
		break;
	case SPEED_2500:
		adjust = IGC_I225_TX_LATENCY_2500;
		break;
	}

	shhwtstamps.hwtstamp =
		ktime_add_ns(shhwtstamps.hwtstamp, adjust);

	adapter->ptp_tx_skb = NULL;

	/* Notify the stack and free the skb after we've unlocked */
	skb_tstamp_tx(skb, &shhwtstamps);
	dev_kfree_skb_any(skb);
}

/**
 * igc_ptp_tx_tstamp_event
 * @adapter: board private structure
 *
 * Called when a TX timestamp interrupt happens to retrieve the
 * timestamp and send it up to the socket.
 */
void igc_ptp_tx_tstamp_event(struct igc_adapter *adapter)
{
	unsigned long flags;

	spin_lock_irqsave(&adapter->ptp_tx_lock, flags);

	if (!adapter->ptp_tx_skb)
		goto unlock;

	igc_ptp_tx_hwtstamp(adapter);

unlock:
	spin_unlock_irqrestore(&adapter->ptp_tx_lock, flags);
}

/**
 * igc_ptp_set_ts_config - set hardware time stamping config
 * @netdev: network interface device structure
 * @ifr: interface request data
 *
 **/
int igc_ptp_set_ts_config(struct net_device *netdev, struct ifreq *ifr)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = igc_ptp_set_timestamp_mode(adapter, &config);
	if (err)
		return err;

	/* save these settings for future reference */
	memcpy(&adapter->tstamp_config, &config,
	       sizeof(adapter->tstamp_config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/**
 * igc_ptp_get_ts_config - get hardware time stamping config
 * @netdev: network interface device structure
 * @ifr: interface request data
 *
 * Get the hwtstamp_config settings to return to the user. Rather than attempt
 * to deconstruct the settings from the registers, just return a shadow copy
 * of the last known settings.
 **/
int igc_ptp_get_ts_config(struct net_device *netdev, struct ifreq *ifr)
{
	struct igc_adapter *adapter = netdev_priv(netdev);
	struct hwtstamp_config *config = &adapter->tstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/* The two conditions below must be met for cross timestamping via
 * PCIe PTM:
 *
 * 1. We have an way to convert the timestamps in the PTM messages
 *    to something related to the system clocks (right now, only
 *    X86 systems with support for the Always Running Timer allow that);
 *
 * 2. We have PTM enabled in the path from the device to the PCIe root port.
 */
static bool igc_is_crosststamp_supported(struct igc_adapter *adapter)
{
	if (!IS_ENABLED(CONFIG_X86_TSC))
		return false;

	/* FIXME: it was noticed that enabling support for PCIe PTM in
	 * some i225-V models could cause lockups when bringing the
	 * interface up/down. There should be no downsides to
	 * disabling crosstimestamping support for i225-V, as it
	 * doesn't have any PTP support. That way we gain some time
	 * while root causing the issue.
	 */
	if (adapter->pdev->device == IGC_DEV_ID_I225_V)
		return false;

	return pcie_ptm_enabled(adapter->pdev);
}

static struct system_counterval_t igc_device_tstamp_to_system(u64 tstamp)
{
#if IS_ENABLED(CONFIG_X86_TSC) && !defined(CONFIG_UML)
	return convert_art_ns_to_tsc(tstamp);
#else
	return (struct system_counterval_t) { };
#endif
}

static void igc_ptm_log_error(struct igc_adapter *adapter, u32 ptm_stat)
{
	struct net_device *netdev = adapter->netdev;

	switch (ptm_stat) {
	case IGC_PTM_STAT_RET_ERR:
		netdev_err(netdev, "PTM Error: Root port timeout\n");
		break;
	case IGC_PTM_STAT_BAD_PTM_RES:
		netdev_err(netdev, "PTM Error: Bad response, PTM Response Data expected\n");
		break;
	case IGC_PTM_STAT_T4M1_OVFL:
		netdev_err(netdev, "PTM Error: T4 minus T1 overflow\n");
		break;
	case IGC_PTM_STAT_ADJUST_1ST:
		netdev_err(netdev, "PTM Error: 1588 timer adjusted during first PTM cycle\n");
		break;
	case IGC_PTM_STAT_ADJUST_CYC:
		netdev_err(netdev, "PTM Error: 1588 timer adjusted during non-first PTM cycle\n");
		break;
	default:
		netdev_err(netdev, "PTM Error: Unknown error (%#x)\n", ptm_stat);
		break;
	}
}

static int igc_phc_get_syncdevicetime(ktime_t *device,
				      struct system_counterval_t *system,
				      void *ctx)
{
	u32 stat, t2_curr_h, t2_curr_l, ctrl;
	struct igc_adapter *adapter = ctx;
	struct igc_hw *hw = &adapter->hw;
	int err, count = 100;
	ktime_t t1, t2_curr;

	/* Get a snapshot of system clocks to use as historic value. */
	ktime_get_snapshot(&adapter->snapshot);

	do {
		/* Doing this in a loop because in the event of a
		 * badly timed (ha!) system clock adjustment, we may
		 * get PTM errors from the PCI root, but these errors
		 * are transitory. Repeating the process returns valid
		 * data eventually.
		 */

		/* To "manually" start the PTM cycle we need to clear and
		 * then set again the TRIG bit.
		 */
		ctrl = rd32(IGC_PTM_CTRL);
		ctrl &= ~IGC_PTM_CTRL_TRIG;
		wr32(IGC_PTM_CTRL, ctrl);
		ctrl |= IGC_PTM_CTRL_TRIG;
		wr32(IGC_PTM_CTRL, ctrl);

		/* The cycle only starts "for real" when software notifies
		 * that it has read the registers, this is done by setting
		 * VALID bit.
		 */
		wr32(IGC_PTM_STAT, IGC_PTM_STAT_VALID);

		err = readx_poll_timeout(rd32, IGC_PTM_STAT, stat,
					 stat, IGC_PTM_STAT_SLEEP,
					 IGC_PTM_STAT_TIMEOUT);
		if (err < 0) {
			netdev_err(adapter->netdev, "Timeout reading IGC_PTM_STAT register\n");
			return err;
		}

		if ((stat & IGC_PTM_STAT_VALID) == IGC_PTM_STAT_VALID)
			break;

		if (stat & ~IGC_PTM_STAT_VALID) {
			/* An error occurred, log it. */
			igc_ptm_log_error(adapter, stat);
			/* The STAT register is write-1-to-clear (W1C),
			 * so write the previous error status to clear it.
			 */
			wr32(IGC_PTM_STAT, stat);
			continue;
		}
	} while (--count);

	if (!count) {
		netdev_err(adapter->netdev, "Exceeded number of tries for PTM cycle\n");
		return -ETIMEDOUT;
	}

	t1 = ktime_set(rd32(IGC_PTM_T1_TIM0_H), rd32(IGC_PTM_T1_TIM0_L));

	t2_curr_l = rd32(IGC_PTM_CURR_T2_L);
	t2_curr_h = rd32(IGC_PTM_CURR_T2_H);

	/* FIXME: When the register that tells the endianness of the
	 * PTM registers are implemented, check them here and add the
	 * appropriate conversion.
	 */
	t2_curr_h = swab32(t2_curr_h);

	t2_curr = ((s64)t2_curr_h << 32 | t2_curr_l);

	*device = t1;
	*system = igc_device_tstamp_to_system(t2_curr);

	return 0;
}

static int igc_ptp_getcrosststamp(struct ptp_clock_info *ptp,
				  struct system_device_crosststamp *cts)
{
	struct igc_adapter *adapter = container_of(ptp, struct igc_adapter,
						   ptp_caps);

	return get_device_system_crosststamp(igc_phc_get_syncdevicetime,
					     adapter, &adapter->snapshot, cts);
}

/**
 * igc_ptp_init - Initialize PTP functionality
 * @adapter: Board private structure
 *
 * This function is called at device probe to initialize the PTP
 * functionality.
 */
void igc_ptp_init(struct igc_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct igc_hw *hw = &adapter->hw;
	int i;

	switch (hw->mac.type) {
	case igc_i225:
		for (i = 0; i < IGC_N_SDP; i++) {
			struct ptp_pin_desc *ppd = &adapter->sdp_config[i];

			snprintf(ppd->name, sizeof(ppd->name), "SDP%d", i);
			ppd->index = i;
			ppd->func = PTP_PF_NONE;
		}
		snprintf(adapter->ptp_caps.name, 16, "%pm", netdev->dev_addr);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 62499999;
		adapter->ptp_caps.adjfine = igc_ptp_adjfine_i225;
		adapter->ptp_caps.adjtime = igc_ptp_adjtime_i225;
		adapter->ptp_caps.gettimex64 = igc_ptp_gettimex64_i225;
		adapter->ptp_caps.settime64 = igc_ptp_settime_i225;
		adapter->ptp_caps.enable = igc_ptp_feature_enable_i225;
		adapter->ptp_caps.pps = 1;
		adapter->ptp_caps.pin_config = adapter->sdp_config;
		adapter->ptp_caps.n_ext_ts = IGC_N_EXTTS;
		adapter->ptp_caps.n_per_out = IGC_N_PEROUT;
		adapter->ptp_caps.n_pins = IGC_N_SDP;
		adapter->ptp_caps.verify = igc_ptp_verify_pin;

		if (!igc_is_crosststamp_supported(adapter))
			break;

		adapter->ptp_caps.getcrosststamp = igc_ptp_getcrosststamp;
		break;
	default:
		adapter->ptp_clock = NULL;
		return;
	}

	spin_lock_init(&adapter->ptp_tx_lock);
	spin_lock_init(&adapter->tmreg_lock);

	adapter->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	adapter->tstamp_config.tx_type = HWTSTAMP_TX_OFF;

	adapter->prev_ptp_time = ktime_to_timespec64(ktime_get_real());
	adapter->ptp_reset_start = ktime_get();

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_caps,
						&adapter->pdev->dev);
	if (IS_ERR(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		netdev_err(netdev, "ptp_clock_register failed\n");
	} else if (adapter->ptp_clock) {
		netdev_info(netdev, "PHC added\n");
		adapter->ptp_flags |= IGC_PTP_ENABLED;
	}
}

static void igc_ptp_time_save(struct igc_adapter *adapter)
{
	igc_ptp_read(adapter, &adapter->prev_ptp_time);
	adapter->ptp_reset_start = ktime_get();
}

static void igc_ptp_time_restore(struct igc_adapter *adapter)
{
	struct timespec64 ts = adapter->prev_ptp_time;
	ktime_t delta;

	delta = ktime_sub(ktime_get(), adapter->ptp_reset_start);

	timespec64_add_ns(&ts, ktime_to_ns(delta));

	igc_ptp_write_i225(adapter, &ts);
}

static void igc_ptm_stop(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 ctrl;

	ctrl = rd32(IGC_PTM_CTRL);
	ctrl &= ~IGC_PTM_CTRL_EN;

	wr32(IGC_PTM_CTRL, ctrl);
}

/**
 * igc_ptp_suspend - Disable PTP work items and prepare for suspend
 * @adapter: Board private structure
 *
 * This function stops the overflow check work and PTP Tx timestamp work, and
 * will prepare the device for OS suspend.
 */
void igc_ptp_suspend(struct igc_adapter *adapter)
{
	if (!(adapter->ptp_flags & IGC_PTP_ENABLED))
		return;

	igc_ptp_clear_tx_tstamp(adapter);

	if (pci_device_is_present(adapter->pdev)) {
		igc_ptp_time_save(adapter);
		igc_ptm_stop(adapter);
	}
}

/**
 * igc_ptp_stop - Disable PTP device and stop the overflow check.
 * @adapter: Board private structure.
 *
 * This function stops the PTP support and cancels the delayed work.
 **/
void igc_ptp_stop(struct igc_adapter *adapter)
{
	igc_ptp_suspend(adapter);

	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		netdev_info(adapter->netdev, "PHC removed\n");
		adapter->ptp_flags &= ~IGC_PTP_ENABLED;
	}
}

/**
 * igc_ptp_reset - Re-enable the adapter for PTP following a reset.
 * @adapter: Board private structure.
 *
 * This function handles the reset work required to re-enable the PTP device.
 **/
void igc_ptp_reset(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 cycle_ctrl, ctrl;
	unsigned long flags;
	u32 timadj;

	/* reset the tstamp_config */
	igc_ptp_set_timestamp_mode(adapter, &adapter->tstamp_config);

	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	switch (adapter->hw.mac.type) {
	case igc_i225:
		timadj = rd32(IGC_TIMADJ);
		timadj |= IGC_TIMADJ_ADJUST_METH;
		wr32(IGC_TIMADJ, timadj);

		wr32(IGC_TSAUXC, 0x0);
		wr32(IGC_TSSDP, 0x0);
		wr32(IGC_TSIM,
		     IGC_TSICR_INTERRUPTS |
		     (adapter->pps_sys_wrap_on ? IGC_TSICR_SYS_WRAP : 0));
		wr32(IGC_IMS, IGC_IMS_TS);

		if (!igc_is_crosststamp_supported(adapter))
			break;

		wr32(IGC_PCIE_DIG_DELAY, IGC_PCIE_DIG_DELAY_DEFAULT);
		wr32(IGC_PCIE_PHY_DELAY, IGC_PCIE_PHY_DELAY_DEFAULT);

		cycle_ctrl = IGC_PTM_CYCLE_CTRL_CYC_TIME(IGC_PTM_CYC_TIME_DEFAULT);

		wr32(IGC_PTM_CYCLE_CTRL, cycle_ctrl);

		ctrl = IGC_PTM_CTRL_EN |
			IGC_PTM_CTRL_START_NOW |
			IGC_PTM_CTRL_SHRT_CYC(IGC_PTM_SHORT_CYC_DEFAULT) |
			IGC_PTM_CTRL_PTM_TO(IGC_PTM_TIMEOUT_DEFAULT) |
			IGC_PTM_CTRL_TRIG;

		wr32(IGC_PTM_CTRL, ctrl);

		/* Force the first cycle to run. */
		wr32(IGC_PTM_STAT, IGC_PTM_STAT_VALID);

		break;
	default:
		/* No work to do. */
		goto out;
	}

	/* Re-initialize the timer. */
	if (hw->mac.type == igc_i225) {
		igc_ptp_time_restore(adapter);
	} else {
		timecounter_init(&adapter->tc, &adapter->cc,
				 ktime_to_ns(ktime_get_real()));
	}
out:
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	wrfl();
}
