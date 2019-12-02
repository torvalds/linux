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

	/* The timestamp latches on lowest register read. For I210/I211, the
	 * lowest register is SYSTIMR. Since we only need to provide nanosecond
	 * resolution, we can ignore it.
	 */
	rd32(IGC_SYSTIMR);
	nsec = rd32(IGC_SYSTIML);
	sec = rd32(IGC_SYSTIMH);

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static void igc_ptp_write_i225(struct igc_adapter *adapter,
			       const struct timespec64 *ts)
{
	struct igc_hw *hw = &adapter->hw;

	/* Writing the SYSTIMR register is not necessary as it only
	 * provides sub-nanosecond resolution.
	 */
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
	rd32(IGC_SYSTIMR);
	ptp_read_system_postts(sts);
	ts->tv_nsec = rd32(IGC_SYSTIML);
	ts->tv_sec = rd32(IGC_SYSTIMH);

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

static int igc_ptp_set_timestamp_mode(struct igc_adapter *adapter,
				      struct hwtstamp_config *config)
{
	return 0;
}

void igc_ptp_tx_hang(struct igc_adapter *adapter)
{
	bool timeout = time_is_before_jiffies(adapter->ptp_tx_start +
					      IGC_PTP_TX_TIMEOUT);
	struct igc_hw *hw = &adapter->hw;

	if (!adapter->ptp_tx_skb)
		return;

	if (!test_bit(__IGC_PTP_TX_IN_PROGRESS, &adapter->state))
		return;

	/* If we haven't received a timestamp within the timeout, it is
	 * reasonable to assume that it will never occur, so we can unlock the
	 * timestamp bit when this occurs.
	 */
	if (timeout) {
		cancel_work_sync(&adapter->ptp_tx_work);
		dev_kfree_skb_any(adapter->ptp_tx_skb);
		adapter->ptp_tx_skb = NULL;
		clear_bit_unlock(__IGC_PTP_TX_IN_PROGRESS, &adapter->state);
		adapter->tx_hwtstamp_timeouts++;
		/* Clear the Tx valid bit in TSYNCTXCTL register to enable
		 * interrupt
		 */
		rd32(IGC_TXSTMPH);
		dev_warn(&adapter->pdev->dev, "clearing Tx timestamp hang\n");
	}
}

void igc_ptp_tx_work(struct work_struct *work)
{
}

/**
 * igc_ptp_set_ts_config - set hardware time stamping config
 * @netdev: network interface device structure
 * @ifreq: interface request data
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
 * @ifreq: interface request data
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

	igc_ptp_reset(adapter);

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_caps,
						&adapter->pdev->dev);
	if (IS_ERR(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		dev_err(&adapter->pdev->dev, "ptp_clock_register failed\n");
	} else if (adapter->ptp_clock) {
		dev_info(&adapter->pdev->dev, "added PHC on %s\n",
			 adapter->netdev->name);
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
	if (adapter->ptp_tx_skb) {
		dev_kfree_skb_any(adapter->ptp_tx_skb);
		adapter->ptp_tx_skb = NULL;
		clear_bit_unlock(__IGC_PTP_TX_IN_PROGRESS, &adapter->state);
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
		dev_info(&adapter->pdev->dev, "removed PHC on %s\n",
			 adapter->netdev->name);
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
