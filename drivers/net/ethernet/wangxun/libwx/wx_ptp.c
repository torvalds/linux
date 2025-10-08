// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */
/* Copyright (c) 1999 - 2025 Intel Corporation. */

#include <linux/ptp_classify.h>
#include <linux/clocksource.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_ptp.h"
#include "wx_hw.h"

#define WX_INCVAL_10GB        0xCCCCCC
#define WX_INCVAL_1GB         0x800000
#define WX_INCVAL_100         0xA00000
#define WX_INCVAL_10          0xC7F380
#define WX_INCVAL_EM          0x2000000
#define WX_INCVAL_AML         0xA00000

#define WX_INCVAL_SHIFT_10GB  20
#define WX_INCVAL_SHIFT_1GB   18
#define WX_INCVAL_SHIFT_100   15
#define WX_INCVAL_SHIFT_10    12
#define WX_INCVAL_SHIFT_EM    22
#define WX_INCVAL_SHIFT_AML   21

#define WX_OVERFLOW_PERIOD    (HZ * 30)
#define WX_PTP_TX_TIMEOUT     (HZ)

#define WX_1588_PPS_WIDTH_EM  120

#define WX_NS_PER_SEC         1000000000ULL

static u64 wx_ptp_timecounter_cyc2time(struct wx *wx, u64 timestamp)
{
	unsigned int seq;
	u64 ns;

	do {
		seq = read_seqbegin(&wx->hw_tc_lock);
		ns = timecounter_cyc2time(&wx->hw_tc, timestamp);
	} while (read_seqretry(&wx->hw_tc_lock, seq));

	return ns;
}

static u64 wx_ptp_readtime(struct wx *wx, struct ptp_system_timestamp *sts)
{
	u32 timeh1, timeh2, timel;

	timeh1 = rd32ptp(wx, WX_TSC_1588_SYSTIMH);
	ptp_read_system_prets(sts);
	timel = rd32ptp(wx, WX_TSC_1588_SYSTIML);
	ptp_read_system_postts(sts);
	timeh2 = rd32ptp(wx, WX_TSC_1588_SYSTIMH);

	if (timeh1 != timeh2) {
		ptp_read_system_prets(sts);
		timel = rd32ptp(wx, WX_TSC_1588_SYSTIML);
		ptp_read_system_prets(sts);
	}
	return (u64)timel | (u64)timeh2 << 32;
}

static int wx_ptp_adjfine(struct ptp_clock_info *ptp, long ppb)
{
	struct wx *wx = container_of(ptp, struct wx, ptp_caps);
	u64 incval, mask;

	smp_mb(); /* Force any pending update before accessing. */
	incval = READ_ONCE(wx->base_incval);
	incval = adjust_by_scaled_ppm(incval, ppb);

	mask = (wx->mac.type == wx_mac_em) ? 0x7FFFFFF : 0xFFFFFF;
	incval &= mask;
	if (wx->mac.type != wx_mac_em)
		incval |= 2 << 24;

	wr32ptp(wx, WX_TSC_1588_INC, incval);

	return 0;
}

static int wx_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct wx *wx = container_of(ptp, struct wx, ptp_caps);
	unsigned long flags;

	write_seqlock_irqsave(&wx->hw_tc_lock, flags);
	timecounter_adjtime(&wx->hw_tc, delta);
	write_sequnlock_irqrestore(&wx->hw_tc_lock, flags);

	if (wx->ptp_setup_sdp)
		wx->ptp_setup_sdp(wx);

	return 0;
}

static int wx_ptp_gettimex64(struct ptp_clock_info *ptp,
			     struct timespec64 *ts,
			     struct ptp_system_timestamp *sts)
{
	struct wx *wx = container_of(ptp, struct wx, ptp_caps);
	u64 ns, stamp;

	stamp = wx_ptp_readtime(wx, sts);
	ns = wx_ptp_timecounter_cyc2time(wx, stamp);
	*ts = ns_to_timespec64(ns);

	return 0;
}

static int wx_ptp_settime64(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	struct wx *wx = container_of(ptp, struct wx, ptp_caps);
	unsigned long flags;
	u64 ns;

	ns = timespec64_to_ns(ts);
	/* reset the timecounter */
	write_seqlock_irqsave(&wx->hw_tc_lock, flags);
	timecounter_init(&wx->hw_tc, &wx->hw_cc, ns);
	write_sequnlock_irqrestore(&wx->hw_tc_lock, flags);

	if (wx->ptp_setup_sdp)
		wx->ptp_setup_sdp(wx);

	return 0;
}

/**
 * wx_ptp_clear_tx_timestamp - utility function to clear Tx timestamp state
 * @wx: the private board structure
 *
 * This function should be called whenever the state related to a Tx timestamp
 * needs to be cleared. This helps ensure that all related bits are reset for
 * the next Tx timestamp event.
 */
static void wx_ptp_clear_tx_timestamp(struct wx *wx)
{
	rd32ptp(wx, WX_TSC_1588_STMPH);
	if (wx->ptp_tx_skb) {
		dev_kfree_skb_any(wx->ptp_tx_skb);
		wx->ptp_tx_skb = NULL;
	}
	clear_bit_unlock(WX_STATE_PTP_TX_IN_PROGRESS, wx->state);
}

/**
 * wx_ptp_convert_to_hwtstamp - convert register value to hw timestamp
 * @wx: private board structure
 * @hwtstamp: stack timestamp structure
 * @timestamp: unsigned 64bit system time value
 *
 * We need to convert the adapter's RX/TXSTMP registers into a hwtstamp value
 * which can be used by the stack's ptp functions.
 *
 * The lock is used to protect consistency of the cyclecounter and the SYSTIME
 * registers. However, it does not need to protect against the Rx or Tx
 * timestamp registers, as there can't be a new timestamp until the old one is
 * unlatched by reading.
 *
 * In addition to the timestamp in hardware, some controllers need a software
 * overflow cyclecounter, and this function takes this into account as well.
 **/
static void wx_ptp_convert_to_hwtstamp(struct wx *wx,
				       struct skb_shared_hwtstamps *hwtstamp,
				       u64 timestamp)
{
	u64 ns;

	ns = wx_ptp_timecounter_cyc2time(wx, timestamp);
	hwtstamp->hwtstamp = ns_to_ktime(ns);
}

/**
 * wx_ptp_tx_hwtstamp - utility function which checks for TX time stamp
 * @wx: the private board struct
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the shhwtstamps structure which
 * is passed up the network stack
 */
static void wx_ptp_tx_hwtstamp(struct wx *wx)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb = wx->ptp_tx_skb;
	u64 regval = 0;

	regval |= (u64)rd32ptp(wx, WX_TSC_1588_STMPL);
	regval |= (u64)rd32ptp(wx, WX_TSC_1588_STMPH) << 32;

	wx_ptp_convert_to_hwtstamp(wx, &shhwtstamps, regval);

	wx->ptp_tx_skb = NULL;
	clear_bit_unlock(WX_STATE_PTP_TX_IN_PROGRESS, wx->state);
	skb_tstamp_tx(skb, &shhwtstamps);
	dev_kfree_skb_any(skb);
	wx->tx_hwtstamp_pkts++;
}

static int wx_ptp_tx_hwtstamp_work(struct wx *wx)
{
	u32 tsynctxctl;

	/* we have to have a valid skb to poll for a timestamp */
	if (!wx->ptp_tx_skb) {
		wx_ptp_clear_tx_timestamp(wx);
		return 0;
	}

	/* stop polling once we have a valid timestamp */
	tsynctxctl = rd32ptp(wx, WX_TSC_1588_CTL);
	if (tsynctxctl & WX_TSC_1588_CTL_VALID) {
		wx_ptp_tx_hwtstamp(wx);
		return 0;
	}

	return -1;
}

/**
 * wx_ptp_overflow_check - watchdog task to detect SYSTIME overflow
 * @wx: pointer to wx struct
 *
 * this watchdog task periodically reads the timecounter
 * in order to prevent missing when the system time registers wrap
 * around. This needs to be run approximately twice a minute for the fastest
 * overflowing hardware. We run it for all hardware since it shouldn't have a
 * large impact.
 */
static void wx_ptp_overflow_check(struct wx *wx)
{
	bool timeout = time_is_before_jiffies(wx->last_overflow_check +
					      WX_OVERFLOW_PERIOD);
	unsigned long flags;

	if (timeout) {
		/* Update the timecounter */
		write_seqlock_irqsave(&wx->hw_tc_lock, flags);
		timecounter_read(&wx->hw_tc);
		write_sequnlock_irqrestore(&wx->hw_tc_lock, flags);

		wx->last_overflow_check = jiffies;
	}
}

/**
 * wx_ptp_rx_hang - detect error case when Rx timestamp registers latched
 * @wx: pointer to wx struct
 *
 * this watchdog task is scheduled to detect error case where hardware has
 * dropped an Rx packet that was timestamped when the ring is full. The
 * particular error is rare but leaves the device in a state unable to
 * timestamp any future packets.
 */
static void wx_ptp_rx_hang(struct wx *wx)
{
	struct wx_ring *rx_ring;
	unsigned long rx_event;
	u32 tsyncrxctl;
	int n;

	tsyncrxctl = rd32(wx, WX_PSR_1588_CTL);

	/* if we don't have a valid timestamp in the registers, just update the
	 * timeout counter and exit
	 */
	if (!(tsyncrxctl & WX_PSR_1588_CTL_VALID)) {
		wx->last_rx_ptp_check = jiffies;
		return;
	}

	/* determine the most recent watchdog or rx_timestamp event */
	rx_event = wx->last_rx_ptp_check;
	for (n = 0; n < wx->num_rx_queues; n++) {
		rx_ring = wx->rx_ring[n];
		if (time_after(rx_ring->last_rx_timestamp, rx_event))
			rx_event = rx_ring->last_rx_timestamp;
	}

	/* only need to read the high RXSTMP register to clear the lock */
	if (time_is_before_jiffies(rx_event + 5 * HZ)) {
		rd32(wx, WX_PSR_1588_STMPH);
		wx->last_rx_ptp_check = jiffies;

		wx->rx_hwtstamp_cleared++;
		dev_warn(&wx->pdev->dev, "clearing RX Timestamp hang");
	}
}

/**
 * wx_ptp_tx_hang - detect error case where Tx timestamp never finishes
 * @wx: private network wx structure
 */
static void wx_ptp_tx_hang(struct wx *wx)
{
	bool timeout = time_is_before_jiffies(wx->ptp_tx_start +
					      WX_PTP_TX_TIMEOUT);

	if (!wx->ptp_tx_skb)
		return;

	if (!test_bit(WX_STATE_PTP_TX_IN_PROGRESS, wx->state))
		return;

	/* If we haven't received a timestamp within the timeout, it is
	 * reasonable to assume that it will never occur, so we can unlock the
	 * timestamp bit when this occurs.
	 */
	if (timeout) {
		wx_ptp_clear_tx_timestamp(wx);
		wx->tx_hwtstamp_timeouts++;
		dev_warn(&wx->pdev->dev, "clearing Tx timestamp hang\n");
	}
}

static long wx_ptp_do_aux_work(struct ptp_clock_info *ptp)
{
	struct wx *wx = container_of(ptp, struct wx, ptp_caps);
	int ts_done;

	ts_done = wx_ptp_tx_hwtstamp_work(wx);

	wx_ptp_overflow_check(wx);
	if (unlikely(test_bit(WX_FLAG_RX_HWTSTAMP_IN_REGISTER,
			      wx->flags)))
		wx_ptp_rx_hang(wx);
	wx_ptp_tx_hang(wx);

	return ts_done ? 1 : HZ;
}

static u64 wx_ptp_trigger_calc(struct wx *wx)
{
	struct cyclecounter *cc = &wx->hw_cc;
	unsigned long flags;
	u64 ns = 0;
	u32 rem;

	/* Read the current clock time, and save the cycle counter value */
	write_seqlock_irqsave(&wx->hw_tc_lock, flags);
	ns = timecounter_read(&wx->hw_tc);
	wx->pps_edge_start = wx->hw_tc.cycle_last;
	write_sequnlock_irqrestore(&wx->hw_tc_lock, flags);
	wx->pps_edge_end = wx->pps_edge_start;

	/* Figure out how far past the next second we are */
	div_u64_rem(ns, WX_NS_PER_SEC, &rem);

	/* Figure out how many nanoseconds to add to round the clock edge up
	 * to the next full second
	 */
	rem = (WX_NS_PER_SEC - rem);

	/* Adjust the clock edge to align with the next full second. */
	wx->pps_edge_start += div_u64(((u64)rem << cc->shift), cc->mult);
	wx->pps_edge_end += div_u64(((u64)(rem + wx->pps_width) <<
				     cc->shift), cc->mult);

	return (ns + rem);
}

static int wx_ptp_setup_sdp(struct wx *wx)
{
	struct cyclecounter *cc = &wx->hw_cc;
	u32 tsauxc;
	u64 nsec;

	if (wx->pps_width >= WX_NS_PER_SEC) {
		wx_err(wx, "PTP pps width cannot be longer than 1s!\n");
		return -EINVAL;
	}

	/* disable the pin first */
	wr32ptp(wx, WX_TSC_1588_AUX_CTL, 0);
	WX_WRITE_FLUSH(wx);

	if (!test_bit(WX_FLAG_PTP_PPS_ENABLED, wx->flags)) {
		if (wx->pps_enabled) {
			wx->pps_enabled = false;
			wx_set_pps(wx, false, 0, 0);
		}
		return 0;
	}

	wx->pps_enabled = true;
	nsec = wx_ptp_trigger_calc(wx);
	wx_set_pps(wx, wx->pps_enabled, nsec, wx->pps_edge_start);

	tsauxc = WX_TSC_1588_AUX_CTL_PLSG | WX_TSC_1588_AUX_CTL_EN_TT0 |
		WX_TSC_1588_AUX_CTL_EN_TT1 | WX_TSC_1588_AUX_CTL_EN_TS0;
	wr32ptp(wx, WX_TSC_1588_TRGT_L(0), (u32)wx->pps_edge_start);
	wr32ptp(wx, WX_TSC_1588_TRGT_H(0), (u32)(wx->pps_edge_start >> 32));
	wr32ptp(wx, WX_TSC_1588_TRGT_L(1), (u32)wx->pps_edge_end);
	wr32ptp(wx, WX_TSC_1588_TRGT_H(1), (u32)(wx->pps_edge_end >> 32));
	wr32ptp(wx, WX_TSC_1588_SDP(0),
		WX_TSC_1588_SDP_FUN_SEL_TT0 | WX_TSC_1588_SDP_OUT_LEVEL_H);
	wr32ptp(wx, WX_TSC_1588_SDP(1), WX_TSC_1588_SDP_FUN_SEL_TS0);
	wr32ptp(wx, WX_TSC_1588_AUX_CTL, tsauxc);
	wr32ptp(wx, WX_TSC_1588_INT_EN, WX_TSC_1588_INT_EN_TT1);
	WX_WRITE_FLUSH(wx);

	/* Adjust the clock edge to align with the next full second. */
	wx->sec_to_cc = div_u64(((u64)WX_NS_PER_SEC << cc->shift), cc->mult);

	return 0;
}

static int wx_ptp_feature_enable(struct ptp_clock_info *ptp,
				 struct ptp_clock_request *rq, int on)
{
	struct wx *wx = container_of(ptp, struct wx, ptp_caps);

	/**
	 * When PPS is enabled, unmask the interrupt for the ClockOut
	 * feature, so that the interrupt handler can send the PPS
	 * event when the clock SDP triggers. Clear mask when PPS is
	 * disabled
	 */
	if (rq->type != PTP_CLK_REQ_PEROUT || !wx->ptp_setup_sdp)
		return -EOPNOTSUPP;

	/* Reject requests with unsupported flags */
	if (rq->perout.flags & ~(PTP_PEROUT_DUTY_CYCLE |
				 PTP_PEROUT_PHASE))
		return -EOPNOTSUPP;

	if (rq->perout.phase.sec || rq->perout.phase.nsec) {
		wx_err(wx, "Absolute start time not supported.\n");
		return -EINVAL;
	}

	if (rq->perout.period.sec != 1 || rq->perout.period.nsec) {
		wx_err(wx, "Only 1pps is supported.\n");
		return -EINVAL;
	}

	if (rq->perout.flags & PTP_PEROUT_DUTY_CYCLE) {
		struct timespec64 ts_on;

		ts_on.tv_sec = rq->perout.on.sec;
		ts_on.tv_nsec = rq->perout.on.nsec;
		wx->pps_width = timespec64_to_ns(&ts_on);
	} else {
		wx->pps_width = 120000000;
	}

	if (on)
		set_bit(WX_FLAG_PTP_PPS_ENABLED, wx->flags);
	else
		clear_bit(WX_FLAG_PTP_PPS_ENABLED, wx->flags);

	return wx->ptp_setup_sdp(wx);
}

void wx_ptp_check_pps_event(struct wx *wx)
{
	u32 tsauxc, int_status;

	/* this check is necessary in case the interrupt was enabled via some
	 * alternative means (ex. debug_fs). Better to check here than
	 * everywhere that calls this function.
	 */
	if (!wx->ptp_clock)
		return;

	int_status = rd32ptp(wx, WX_TSC_1588_INT_ST);
	if (int_status & WX_TSC_1588_INT_ST_TT1) {
		/* disable the pin first */
		wr32ptp(wx, WX_TSC_1588_AUX_CTL, 0);
		WX_WRITE_FLUSH(wx);

		wx_ptp_trigger_calc(wx);

		tsauxc = WX_TSC_1588_AUX_CTL_PLSG | WX_TSC_1588_AUX_CTL_EN_TT0 |
			 WX_TSC_1588_AUX_CTL_EN_TT1 | WX_TSC_1588_AUX_CTL_EN_TS0;
		wr32ptp(wx, WX_TSC_1588_TRGT_L(0), (u32)wx->pps_edge_start);
		wr32ptp(wx, WX_TSC_1588_TRGT_H(0), (u32)(wx->pps_edge_start >> 32));
		wr32ptp(wx, WX_TSC_1588_TRGT_L(1), (u32)wx->pps_edge_end);
		wr32ptp(wx, WX_TSC_1588_TRGT_H(1), (u32)(wx->pps_edge_end >> 32));
		wr32ptp(wx, WX_TSC_1588_AUX_CTL, tsauxc);
		WX_WRITE_FLUSH(wx);
	}
}
EXPORT_SYMBOL(wx_ptp_check_pps_event);

static long wx_ptp_create_clock(struct wx *wx)
{
	struct net_device *netdev = wx->netdev;
	long err;

	/* do nothing if we already have a clock device */
	if (!IS_ERR_OR_NULL(wx->ptp_clock))
		return 0;

	snprintf(wx->ptp_caps.name, sizeof(wx->ptp_caps.name),
		 "%s", netdev->name);
	wx->ptp_caps.owner = THIS_MODULE;
	wx->ptp_caps.n_alarm = 0;
	wx->ptp_caps.n_ext_ts = 0;
	wx->ptp_caps.pps = 0;
	wx->ptp_caps.adjfine = wx_ptp_adjfine;
	wx->ptp_caps.adjtime = wx_ptp_adjtime;
	wx->ptp_caps.gettimex64 = wx_ptp_gettimex64;
	wx->ptp_caps.settime64 = wx_ptp_settime64;
	wx->ptp_caps.do_aux_work = wx_ptp_do_aux_work;
	switch (wx->mac.type) {
	case wx_mac_aml:
	case wx_mac_aml40:
		wx->ptp_caps.max_adj = 250000000;
		wx->ptp_caps.n_per_out = 1;
		wx->ptp_setup_sdp = wx_ptp_setup_sdp;
		wx->ptp_caps.enable = wx_ptp_feature_enable;
		break;
	case wx_mac_sp:
		wx->ptp_caps.max_adj = 250000000;
		wx->ptp_caps.n_per_out = 0;
		wx->ptp_setup_sdp = NULL;
		break;
	case wx_mac_em:
		wx->ptp_caps.max_adj = 500000000;
		wx->ptp_caps.n_per_out = 1;
		wx->ptp_setup_sdp = wx_ptp_setup_sdp;
		wx->ptp_caps.enable = wx_ptp_feature_enable;
		break;
	default:
		return -EOPNOTSUPP;
	}

	wx->ptp_clock = ptp_clock_register(&wx->ptp_caps, &wx->pdev->dev);
	if (IS_ERR(wx->ptp_clock)) {
		err = PTR_ERR(wx->ptp_clock);
		wx->ptp_clock = NULL;
		wx_err(wx, "ptp clock register failed\n");
		return err;
	} else if (wx->ptp_clock) {
		dev_info(&wx->pdev->dev, "registered PHC device on %s\n",
			 netdev->name);
	}

	/* Set the default timestamp mode to disabled here. We do this in
	 * create_clock instead of initialization, because we don't want to
	 * override the previous settings during a suspend/resume cycle.
	 */
	wx->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	wx->tstamp_config.tx_type = HWTSTAMP_TX_OFF;

	return 0;
}

static int wx_ptp_set_timestamp_mode(struct wx *wx,
				     struct kernel_hwtstamp_config *config)
{
	u32 tsync_tx_ctl = WX_TSC_1588_CTL_ENABLED;
	u32 tsync_rx_ctl = WX_PSR_1588_CTL_ENABLED;
	DECLARE_BITMAP(flags, WX_PF_FLAGS_NBITS);
	u32 tsync_rx_mtrl = PTP_EV_PORT << 16;
	bool is_l2 = false;
	u32 regval;

	memcpy(flags, wx->flags, sizeof(wx->flags));

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		tsync_tx_ctl = 0;
		break;
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tsync_rx_ctl = 0;
		tsync_rx_mtrl = 0;
		clear_bit(WX_FLAG_RX_HWTSTAMP_ENABLED, flags);
		clear_bit(WX_FLAG_RX_HWTSTAMP_IN_REGISTER, flags);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		tsync_rx_ctl |= WX_PSR_1588_CTL_TYPE_L4_V1;
		tsync_rx_mtrl |= WX_PSR_1588_MSG_V1_SYNC;
		set_bit(WX_FLAG_RX_HWTSTAMP_ENABLED, flags);
		set_bit(WX_FLAG_RX_HWTSTAMP_IN_REGISTER, flags);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		tsync_rx_ctl |= WX_PSR_1588_CTL_TYPE_L4_V1;
		tsync_rx_mtrl |= WX_PSR_1588_MSG_V1_DELAY_REQ;
		set_bit(WX_FLAG_RX_HWTSTAMP_ENABLED, flags);
		set_bit(WX_FLAG_RX_HWTSTAMP_IN_REGISTER, flags);
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
		tsync_rx_ctl |= WX_PSR_1588_CTL_TYPE_EVENT_V2;
		is_l2 = true;
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		set_bit(WX_FLAG_RX_HWTSTAMP_ENABLED, flags);
		set_bit(WX_FLAG_RX_HWTSTAMP_IN_REGISTER, flags);
		break;
	default:
		/* register PSR_1588_MSG must be set in order to do V1 packets,
		 * therefore it is not possible to time stamp both V1 Sync and
		 * Delay_Req messages unless hardware supports timestamping all
		 * packets => return error
		 */
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	/* define ethertype filter for timestamping L2 packets */
	if (is_l2)
		wr32(wx, WX_PSR_ETYPE_SWC(WX_PSR_ETYPE_SWC_FILTER_1588),
		     (WX_PSR_ETYPE_SWC_FILTER_EN | /* enable filter */
		      WX_PSR_ETYPE_SWC_1588 | /* enable timestamping */
		      ETH_P_1588)); /* 1588 eth protocol type */
	else
		wr32(wx, WX_PSR_ETYPE_SWC(WX_PSR_ETYPE_SWC_FILTER_1588), 0);

	/* enable/disable TX */
	regval = rd32ptp(wx, WX_TSC_1588_CTL);
	regval &= ~WX_TSC_1588_CTL_ENABLED;
	regval |= tsync_tx_ctl;
	wr32ptp(wx, WX_TSC_1588_CTL, regval);

	/* enable/disable RX */
	regval = rd32(wx, WX_PSR_1588_CTL);
	regval &= ~(WX_PSR_1588_CTL_ENABLED | WX_PSR_1588_CTL_TYPE_MASK);
	regval |= tsync_rx_ctl;
	wr32(wx, WX_PSR_1588_CTL, regval);

	/* define which PTP packets are time stamped */
	wr32(wx, WX_PSR_1588_MSG, tsync_rx_mtrl);

	WX_WRITE_FLUSH(wx);

	/* configure adapter flags only when HW is actually configured */
	memcpy(wx->flags, flags, sizeof(wx->flags));

	/* clear TX/RX timestamp state, just to be sure */
	wx_ptp_clear_tx_timestamp(wx);
	rd32(wx, WX_PSR_1588_STMPH);

	return 0;
}

static u64 wx_ptp_read(struct cyclecounter *hw_cc)
{
	struct wx *wx = container_of(hw_cc, struct wx, hw_cc);

	return wx_ptp_readtime(wx, NULL);
}

static void wx_ptp_link_speed_adjust(struct wx *wx, u32 *shift, u32 *incval)
{
	switch (wx->mac.type) {
	case wx_mac_aml:
	case wx_mac_aml40:
		*shift = WX_INCVAL_SHIFT_AML;
		*incval = WX_INCVAL_AML;
		return;
	case wx_mac_em:
		*shift = WX_INCVAL_SHIFT_EM;
		*incval = WX_INCVAL_EM;
		return;
	default:
		break;
	}

	switch (wx->speed) {
	case SPEED_10:
		*shift = WX_INCVAL_SHIFT_10;
		*incval = WX_INCVAL_10;
		break;
	case SPEED_100:
		*shift = WX_INCVAL_SHIFT_100;
		*incval = WX_INCVAL_100;
		break;
	case SPEED_1000:
		*shift = WX_INCVAL_SHIFT_1GB;
		*incval = WX_INCVAL_1GB;
		break;
	case SPEED_10000:
	default:
		*shift = WX_INCVAL_SHIFT_10GB;
		*incval = WX_INCVAL_10GB;
		break;
	}
}

/**
 * wx_ptp_reset_cyclecounter - create the cycle counter from hw
 * @wx: pointer to the wx structure
 *
 * This function should be called to set the proper values for the TSC_1588_INC
 * register and tell the cyclecounter structure what the tick rate of SYSTIME
 * is. It does not directly modify SYSTIME registers or the timecounter
 * structure. It should be called whenever a new TSC_1588_INC value is
 * necessary, such as during initialization or when the link speed changes.
 */
void wx_ptp_reset_cyclecounter(struct wx *wx)
{
	u32 incval = 0, mask = 0;
	struct cyclecounter cc;
	unsigned long flags;

	/* For some of the boards below this mask is technically incorrect.
	 * The timestamp mask overflows at approximately 61bits. However the
	 * particular hardware does not overflow on an even bitmask value.
	 * Instead, it overflows due to conversion of upper 32bits billions of
	 * cycles. Timecounters are not really intended for this purpose so
	 * they do not properly function if the overflow point isn't 2^N-1.
	 * However, the actual SYSTIME values in question take ~138 years to
	 * overflow. In practice this means they won't actually overflow. A
	 * proper fix to this problem would require modification of the
	 * timecounter delta calculations.
	 */
	cc.mask = CLOCKSOURCE_MASK(64);
	cc.mult = 1;
	cc.shift = 0;

	cc.read = wx_ptp_read;
	wx_ptp_link_speed_adjust(wx, &cc.shift, &incval);

	/* update the base incval used to calculate frequency adjustment */
	WRITE_ONCE(wx->base_incval, incval);

	mask = (wx->mac.type == wx_mac_em) ? 0x7FFFFFF : 0xFFFFFF;
	incval &= mask;
	if (wx->mac.type != wx_mac_em)
		incval |= 2 << 24;
	wr32ptp(wx, WX_TSC_1588_INC, incval);

	smp_mb(); /* Force the above update. */

	/* need lock to prevent incorrect read while modifying cyclecounter */
	write_seqlock_irqsave(&wx->hw_tc_lock, flags);
	memcpy(&wx->hw_cc, &cc, sizeof(wx->hw_cc));
	write_sequnlock_irqrestore(&wx->hw_tc_lock, flags);
}
EXPORT_SYMBOL(wx_ptp_reset_cyclecounter);

void wx_ptp_reset(struct wx *wx)
{
	unsigned long flags;

	/* reset the hardware timestamping mode */
	wx_ptp_set_timestamp_mode(wx, &wx->tstamp_config);
	wx_ptp_reset_cyclecounter(wx);

	wr32ptp(wx, WX_TSC_1588_SYSTIML, 0);
	wr32ptp(wx, WX_TSC_1588_SYSTIMH, 0);
	WX_WRITE_FLUSH(wx);

	write_seqlock_irqsave(&wx->hw_tc_lock, flags);
	timecounter_init(&wx->hw_tc, &wx->hw_cc,
			 ktime_to_ns(ktime_get_real()));
	write_sequnlock_irqrestore(&wx->hw_tc_lock, flags);

	wx->last_overflow_check = jiffies;
	ptp_schedule_worker(wx->ptp_clock, HZ);

	/* Now that the shift has been calculated and the systime
	 * registers reset, (re-)enable the Clock out feature
	 */
	if (wx->ptp_setup_sdp)
		wx->ptp_setup_sdp(wx);
}
EXPORT_SYMBOL(wx_ptp_reset);

void wx_ptp_init(struct wx *wx)
{
	/* Initialize the seqlock_t first, since the user might call the clock
	 * functions any time after we've initialized the ptp clock device.
	 */
	seqlock_init(&wx->hw_tc_lock);

	/* obtain a ptp clock device, or re-use an existing device */
	if (wx_ptp_create_clock(wx))
		return;

	wx->tx_hwtstamp_pkts = 0;
	wx->tx_hwtstamp_timeouts = 0;
	wx->tx_hwtstamp_skipped = 0;
	wx->tx_hwtstamp_errors = 0;
	wx->rx_hwtstamp_cleared = 0;
	/* reset the ptp related hardware bits */
	wx_ptp_reset(wx);

	/* enter the WX_STATE_PTP_RUNNING state */
	set_bit(WX_STATE_PTP_RUNNING, wx->state);
}
EXPORT_SYMBOL(wx_ptp_init);

/**
 * wx_ptp_suspend - stop ptp work items
 * @wx: pointer to wx struct
 *
 * This function suspends ptp activity, and prevents more work from being
 * generated, but does not destroy the clock device.
 */
void wx_ptp_suspend(struct wx *wx)
{
	/* leave the WX_STATE_PTP_RUNNING STATE */
	if (!test_and_clear_bit(WX_STATE_PTP_RUNNING, wx->state))
		return;

	clear_bit(WX_FLAG_PTP_PPS_ENABLED, wx->flags);
	if (wx->ptp_setup_sdp)
		wx->ptp_setup_sdp(wx);

	wx_ptp_clear_tx_timestamp(wx);
}
EXPORT_SYMBOL(wx_ptp_suspend);

/**
 * wx_ptp_stop - destroy the ptp_clock device
 * @wx: pointer to wx struct
 *
 * Completely destroy the ptp_clock device, and disable all PTP related
 * features. Intended to be run when the device is being closed.
 */
void wx_ptp_stop(struct wx *wx)
{
	/* first, suspend ptp activity */
	wx_ptp_suspend(wx);

	/* now destroy the ptp clock device */
	if (wx->ptp_clock) {
		ptp_clock_unregister(wx->ptp_clock);
		wx->ptp_clock = NULL;
		dev_info(&wx->pdev->dev, "removed PHC on %s\n", wx->netdev->name);
	}
}
EXPORT_SYMBOL(wx_ptp_stop);

/**
 * wx_ptp_rx_hwtstamp - utility function which checks for RX time stamp
 * @wx: pointer to wx struct
 * @skb: particular skb to send timestamp with
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the shhwtstamps structure which
 * is passed up the network stack
 */
void wx_ptp_rx_hwtstamp(struct wx *wx, struct sk_buff *skb)
{
	u64 regval = 0;
	u32 tsyncrxctl;

	/* Read the tsyncrxctl register afterwards in order to prevent taking an
	 * I/O hit on every packet.
	 */
	tsyncrxctl = rd32(wx, WX_PSR_1588_CTL);
	if (!(tsyncrxctl & WX_PSR_1588_CTL_VALID))
		return;

	regval |= (u64)rd32(wx, WX_PSR_1588_STMPL);
	regval |= (u64)rd32(wx, WX_PSR_1588_STMPH) << 32;

	wx_ptp_convert_to_hwtstamp(wx, skb_hwtstamps(skb), regval);
}

int wx_hwtstamp_get(struct net_device *dev,
		    struct kernel_hwtstamp_config *cfg)
{
	struct wx *wx = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	*cfg = wx->tstamp_config;

	return 0;
}
EXPORT_SYMBOL(wx_hwtstamp_get);

int wx_hwtstamp_set(struct net_device *dev,
		    struct kernel_hwtstamp_config *cfg,
		    struct netlink_ext_ack *extack)
{
	struct wx *wx = netdev_priv(dev);
	int err;

	if (!netif_running(dev))
		return -EINVAL;

	err = wx_ptp_set_timestamp_mode(wx, cfg);
	if (err)
		return err;

	/* save these settings for future reference */
	memcpy(&wx->tstamp_config, cfg, sizeof(wx->tstamp_config));

	return 0;
}
EXPORT_SYMBOL(wx_hwtstamp_set);
