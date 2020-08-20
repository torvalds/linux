// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2019 Intel Corporation */

#include "igc.h"

#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/ptp_classify.h>
#include <linux/clocksource.h>

#define INCVALUE_MASK		0x7fffffff
#define ISGN			0x80000000

#define IGC_SYSTIM_OVERFLOW_PERIOD	(HZ * 60 * 9)
#define IGC_PTP_TX_TIMEOUT		(HZ * 15)

/* SYSTIM read access for I225 */
static void igc_ptp_read_i225(struct igc_adapter *adapter,
			      struct timespec64 *ts)
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

	igc_ptp_read_i225(igc, &now);
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

static int igc_ptp_feature_enable_i225(struct ptp_clock_info *ptp,
				       struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

/**
 * igc_ptp_systim_to_hwtstamp - convert system time value to HW timestamp
 * @adapter: board private structure
 * @hwtstamps: timestamp structure to update
 * @systim: unsigned 64bit system time value
 *
 * We need to convert the system time value stored in the RX/TXSTMP registers
 * into a hwtstamp which can be used by the upper level timestamping functions.
 **/
static void igc_ptp_systim_to_hwtstamp(struct igc_adapter *adapter,
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
		break;
	}
}

/**
 * igc_ptp_rx_pktstamp - retrieve Rx per packet timestamp
 * @q_vector: Pointer to interrupt specific structure
 * @va: Pointer to address containing Rx buffer
 * @skb: Buffer containing timestamp and packet
 *
 * This function is meant to retrieve the first timestamp from the
 * first buffer of an incoming frame. The value is stored in little
 * endian format starting on byte 0. There's a second timestamp
 * starting on byte 8.
 **/
void igc_ptp_rx_pktstamp(struct igc_q_vector *q_vector, void *va,
			 struct sk_buff *skb)
{
	struct igc_adapter *adapter = q_vector->adapter;
	__le64 *regval = (__le64 *)va;
	int adjust = 0;

	/* The timestamp is recorded in little endian format.
	 * DWORD: | 0          | 1           | 2          | 3
	 * Field: | Timer0 Low | Timer0 High | Timer1 Low | Timer1 High
	 */
	igc_ptp_systim_to_hwtstamp(adapter, skb_hwtstamps(skb),
				   le64_to_cpu(regval[0]));

	/* adjust timestamp for the RX latency based on link speed */
	if (adapter->hw.mac.type == igc_i225) {
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
		}
	}
	skb_hwtstamps(skb)->hwtstamp =
		ktime_sub_ns(skb_hwtstamps(skb)->hwtstamp, adjust);
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

static void igc_ptp_disable_tx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	wr32(IGC_TSYNCTXCTL, 0);
}

static void igc_ptp_enable_tx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	wr32(IGC_TSYNCTXCTL, IGC_TSYNCTXCTL_ENABLED | IGC_TSYNCTXCTL_TXSYNSIG);

	/* Read TXSTMP registers to discard any timestamp previously stored. */
	rd32(IGC_TXSTMPL);
	rd32(IGC_TXSTMPH);
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
	/* reserved for future extensions */
	if (config->flags)
		return -EINVAL;

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

static void igc_ptp_tx_timeout(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	dev_kfree_skb_any(adapter->ptp_tx_skb);
	adapter->ptp_tx_skb = NULL;
	adapter->tx_hwtstamp_timeouts++;
	clear_bit_unlock(__IGC_PTP_TX_IN_PROGRESS, &adapter->state);
	/* Clear the tx valid bit in TSYNCTXCTL register to enable interrupt. */
	rd32(IGC_TXSTMPH);
	netdev_warn(adapter->netdev, "Tx timestamp timeout\n");
}

void igc_ptp_tx_hang(struct igc_adapter *adapter)
{
	bool timeout = time_is_before_jiffies(adapter->ptp_tx_start +
					      IGC_PTP_TX_TIMEOUT);

	if (!test_bit(__IGC_PTP_TX_IN_PROGRESS, &adapter->state))
		return;

	/* If we haven't received a timestamp within the timeout, it is
	 * reasonable to assume that it will never occur, so we can unlock the
	 * timestamp bit when this occurs.
	 */
	if (timeout) {
		cancel_work_sync(&adapter->ptp_tx_work);
		igc_ptp_tx_timeout(adapter);
	}
}

/**
 * igc_ptp_tx_hwtstamp - utility function which checks for TX time stamp
 * @adapter: Board private structure
 *
 * If we were asked to do hardware stamping and such a time stamp is
 * available, then it must have been for this skb here because we only
 * allow only one such packet into the queue.
 */
static void igc_ptp_tx_hwtstamp(struct igc_adapter *adapter)
{
	struct sk_buff *skb = adapter->ptp_tx_skb;
	struct skb_shared_hwtstamps shhwtstamps;
	struct igc_hw *hw = &adapter->hw;
	int adjust = 0;
	u64 regval;

	if (WARN_ON_ONCE(!skb))
		return;

	regval = rd32(IGC_TXSTMPL);
	regval |= (u64)rd32(IGC_TXSTMPH) << 32;
	igc_ptp_systim_to_hwtstamp(adapter, &shhwtstamps, regval);

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

	/* Clear the lock early before calling skb_tstamp_tx so that
	 * applications are not woken up before the lock bit is clear. We use
	 * a copy of the skb pointer to ensure other threads can't change it
	 * while we're notifying the stack.
	 */
	adapter->ptp_tx_skb = NULL;
	clear_bit_unlock(__IGC_PTP_TX_IN_PROGRESS, &adapter->state);

	/* Notify the stack and free the skb after we've unlocked */
	skb_tstamp_tx(skb, &shhwtstamps);
	dev_kfree_skb_any(skb);
}

/**
 * igc_ptp_tx_work
 * @work: pointer to work struct
 *
 * This work function polls the TSYNCTXCTL valid bit to determine when a
 * timestamp has been taken for the current stored skb.
 */
static void igc_ptp_tx_work(struct work_struct *work)
{
	struct igc_adapter *adapter = container_of(work, struct igc_adapter,
						   ptp_tx_work);
	struct igc_hw *hw = &adapter->hw;
	u32 tsynctxctl;

	if (!test_bit(__IGC_PTP_TX_IN_PROGRESS, &adapter->state))
		return;

	tsynctxctl = rd32(IGC_TSYNCTXCTL);
	if (WARN_ON_ONCE(!(tsynctxctl & IGC_TSYNCTXCTL_TXTT_0)))
		return;

	igc_ptp_tx_hwtstamp(adapter);
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

	switch (hw->mac.type) {
	case igc_i225:
		snprintf(adapter->ptp_caps.name, 16, "%pm", netdev->dev_addr);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 62499999;
		adapter->ptp_caps.adjfine = igc_ptp_adjfine_i225;
		adapter->ptp_caps.adjtime = igc_ptp_adjtime_i225;
		adapter->ptp_caps.gettimex64 = igc_ptp_gettimex64_i225;
		adapter->ptp_caps.settime64 = igc_ptp_settime_i225;
		adapter->ptp_caps.enable = igc_ptp_feature_enable_i225;
		break;
	default:
		adapter->ptp_clock = NULL;
		return;
	}

	spin_lock_init(&adapter->tmreg_lock);
	INIT_WORK(&adapter->ptp_tx_work, igc_ptp_tx_work);

	adapter->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	adapter->tstamp_config.tx_type = HWTSTAMP_TX_OFF;

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

	cancel_work_sync(&adapter->ptp_tx_work);
	dev_kfree_skb_any(adapter->ptp_tx_skb);
	adapter->ptp_tx_skb = NULL;
	clear_bit_unlock(__IGC_PTP_TX_IN_PROGRESS, &adapter->state);
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
	unsigned long flags;

	/* reset the tstamp_config */
	igc_ptp_set_timestamp_mode(adapter, &adapter->tstamp_config);

	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	switch (adapter->hw.mac.type) {
	case igc_i225:
		wr32(IGC_TSAUXC, 0x0);
		wr32(IGC_TSSDP, 0x0);
		wr32(IGC_TSIM, IGC_TSICR_INTERRUPTS);
		wr32(IGC_IMS, IGC_IMS_TS);
		break;
	default:
		/* No work to do. */
		goto out;
	}

	/* Re-initialize the timer. */
	if (hw->mac.type == igc_i225) {
		struct timespec64 ts64 = ktime_to_timespec64(ktime_get_real());

		igc_ptp_write_i225(adapter, &ts64);
	} else {
		timecounter_init(&adapter->tc, &adapter->cc,
				 ktime_to_ns(ktime_get_real()));
	}
out:
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	wrfl();
}
