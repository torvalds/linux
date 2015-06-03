/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/ptp_classify.h>
#include <linux/ptp_clock_kernel.h>

#include "fm10k.h"

#define FM10K_TS_TX_TIMEOUT		(HZ * 15)

void fm10k_systime_to_hwtstamp(struct fm10k_intfc *interface,
			       struct skb_shared_hwtstamps *hwtstamp,
			       u64 systime)
{
	unsigned long flags;

	read_lock_irqsave(&interface->systime_lock, flags);
	systime += interface->ptp_adjust;
	read_unlock_irqrestore(&interface->systime_lock, flags);

	hwtstamp->hwtstamp = ns_to_ktime(systime);
}

static struct sk_buff *fm10k_ts_tx_skb(struct fm10k_intfc *interface,
				       __le16 dglort)
{
	struct sk_buff_head *list = &interface->ts_tx_skb_queue;
	struct sk_buff *skb;

	skb_queue_walk(list, skb) {
		if (FM10K_CB(skb)->fi.w.dglort == dglort)
			return skb;
	}

	return NULL;
}

void fm10k_ts_tx_enqueue(struct fm10k_intfc *interface, struct sk_buff *skb)
{
	struct sk_buff_head *list = &interface->ts_tx_skb_queue;
	struct sk_buff *clone;
	unsigned long flags;

	/* create clone for us to return on the Tx path */
	clone = skb_clone_sk(skb);
	if (!clone)
		return;

	FM10K_CB(clone)->ts_tx_timeout = jiffies + FM10K_TS_TX_TIMEOUT;
	spin_lock_irqsave(&list->lock, flags);

	/* attempt to locate any buffers with the same dglort,
	 * if none are present then insert skb in tail of list
	 */
	skb = fm10k_ts_tx_skb(interface, FM10K_CB(clone)->fi.w.dglort);
	if (!skb) {
		skb_shinfo(clone)->tx_flags |= SKBTX_IN_PROGRESS;
		__skb_queue_tail(list, clone);
	}

	spin_unlock_irqrestore(&list->lock, flags);

	/* if list is already has one then we just free the clone */
	if (skb)
		dev_kfree_skb(clone);
}

void fm10k_ts_tx_hwtstamp(struct fm10k_intfc *interface, __le16 dglort,
			  u64 systime)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff_head *list = &interface->ts_tx_skb_queue;
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);

	/* attempt to locate and pull the sk_buff out of the list */
	skb = fm10k_ts_tx_skb(interface, dglort);
	if (skb)
		__skb_unlink(skb, list);

	spin_unlock_irqrestore(&list->lock, flags);

	/* if not found do nothing */
	if (!skb)
		return;

	/* timestamp the sk_buff and free out copy */
	fm10k_systime_to_hwtstamp(interface, &shhwtstamps, systime);
	skb_tstamp_tx(skb, &shhwtstamps);
	dev_kfree_skb_any(skb);
}

void fm10k_ts_tx_subtask(struct fm10k_intfc *interface)
{
	struct sk_buff_head *list = &interface->ts_tx_skb_queue;
	struct sk_buff *skb, *tmp;
	unsigned long flags;

	/* If we're down or resetting, just bail */
	if (test_bit(__FM10K_DOWN, &interface->state) ||
	    test_bit(__FM10K_RESETTING, &interface->state))
		return;

	spin_lock_irqsave(&list->lock, flags);

	/* walk though the list and flush any expired timestamp packets */
	skb_queue_walk_safe(list, skb, tmp) {
		if (!time_is_after_jiffies(FM10K_CB(skb)->ts_tx_timeout))
			continue;
		__skb_unlink(skb, list);
		kfree_skb(skb);
		interface->tx_hwtstamp_timeouts++;
	}

	spin_unlock_irqrestore(&list->lock, flags);
}

static u64 fm10k_systime_read(struct fm10k_intfc *interface)
{
	struct fm10k_hw *hw = &interface->hw;

	return hw->mac.ops.read_systime(hw);
}

void fm10k_ts_reset(struct fm10k_intfc *interface)
{
	s64 ns = ktime_to_ns(ktime_get_real());
	unsigned long flags;

	/* reinitialize the clock */
	write_lock_irqsave(&interface->systime_lock, flags);
	interface->ptp_adjust = fm10k_systime_read(interface) - ns;
	write_unlock_irqrestore(&interface->systime_lock, flags);
}

void fm10k_ts_init(struct fm10k_intfc *interface)
{
	/* Initialize lock protecting systime access */
	rwlock_init(&interface->systime_lock);

	/* Initialize skb queue for pending timestamp requests */
	skb_queue_head_init(&interface->ts_tx_skb_queue);

	/* reset the clock to current kernel time */
	fm10k_ts_reset(interface);
}

/**
 * fm10k_get_ts_config - get current hardware timestamping configuration
 * @netdev: network interface device structure
 * @ifreq: ioctl data
 *
 * This function returns the current timestamping settings. Rather than
 * attempt to deconstruct registers to fill in the values, simply keep a copy
 * of the old settings around, and return a copy when requested.
 */
int fm10k_get_ts_config(struct net_device *netdev, struct ifreq *ifr)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct hwtstamp_config *config = &interface->ts_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/**
 * fm10k_set_ts_config - control hardware time stamping
 * @netdev: network interface device structure
 * @ifreq: ioctl data
 *
 * Outgoing time stamping can be enabled and disabled. Play nice and
 * disable it when requested, although it shouldn't cause any overhead
 * when no packet needs it. At most one packet in the queue may be
 * marked for time stamping, otherwise it would be impossible to tell
 * for sure to which packet the hardware time stamp belongs.
 *
 * Incoming time stamping has to be configured via the hardware
 * filters. Not all combinations are supported, in particular event
 * type has to be specified. Matching the kind of event packet is
 * not supported, with the exception of "all V2 events regardless of
 * level 2 or 4".
 *
 * Since hardware always timestamps Path delay packets when timestamping V2
 * packets, regardless of the type specified in the register, only use V2
 * Event mode. This more accurately tells the user what the hardware is going
 * to do anyways.
 */
int fm10k_set_ts_config(struct net_device *netdev, struct ifreq *ifr)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct hwtstamp_config ts_config;

	if (copy_from_user(&ts_config, ifr->ifr_data, sizeof(ts_config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (ts_config.flags)
		return -EINVAL;

	switch (ts_config.tx_type) {
	case HWTSTAMP_TX_OFF:
		break;
	case HWTSTAMP_TX_ON:
		/* we likely need some check here to see if this is supported */
		break;
	default:
		return -ERANGE;
	}

	switch (ts_config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		interface->flags &= ~FM10K_FLAG_RX_TS_ENABLED;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_ALL:
		interface->flags |= FM10K_FLAG_RX_TS_ENABLED;
		ts_config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	/* save these settings for future reference */
	interface->ts_config = ts_config;

	return copy_to_user(ifr->ifr_data, &ts_config, sizeof(ts_config)) ?
		-EFAULT : 0;
}

static int fm10k_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct fm10k_intfc *interface;
	struct fm10k_hw *hw;
	int err;

	interface = container_of(ptp, struct fm10k_intfc, ptp_caps);
	hw = &interface->hw;

	err = hw->mac.ops.adjust_systime(hw, ppb);

	/* the only error we should see is if the value is out of range */
	return (err == FM10K_ERR_PARAM) ? -ERANGE : err;
}

static int fm10k_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct fm10k_intfc *interface;
	unsigned long flags;

	interface = container_of(ptp, struct fm10k_intfc, ptp_caps);

	write_lock_irqsave(&interface->systime_lock, flags);
	interface->ptp_adjust += delta;
	write_unlock_irqrestore(&interface->systime_lock, flags);

	return 0;
}

static int fm10k_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct fm10k_intfc *interface;
	unsigned long flags;
	u64 now;

	interface = container_of(ptp, struct fm10k_intfc, ptp_caps);

	read_lock_irqsave(&interface->systime_lock, flags);
	now = fm10k_systime_read(interface) + interface->ptp_adjust;
	read_unlock_irqrestore(&interface->systime_lock, flags);

	*ts = ns_to_timespec64(now);

	return 0;
}

static int fm10k_ptp_settime(struct ptp_clock_info *ptp,
			     const struct timespec64 *ts)
{
	struct fm10k_intfc *interface;
	unsigned long flags;
	u64 ns = timespec64_to_ns(ts);

	interface = container_of(ptp, struct fm10k_intfc, ptp_caps);

	write_lock_irqsave(&interface->systime_lock, flags);
	interface->ptp_adjust = fm10k_systime_read(interface) - ns;
	write_unlock_irqrestore(&interface->systime_lock, flags);

	return 0;
}

static int fm10k_ptp_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *rq,
			    int __always_unused on)
{
	struct ptp_clock_time *t = &rq->perout.period;
	struct fm10k_intfc *interface;
	struct fm10k_hw *hw;
	u64 period;
	u32 step;

	/* we can only support periodic output */
	if (rq->type != PTP_CLK_REQ_PEROUT)
		return -EINVAL;

	/* verify the requested channel is there */
	if (rq->perout.index >= ptp->n_per_out)
		return -EINVAL;

	/* we cannot enforce start time as there is no
	 * mechanism for that in the hardware, we can only control
	 * the period.
	 */

	/* we cannot support periods greater than 4 seconds due to reg limit */
	if (t->sec > 4 || t->sec < 0)
		return -ERANGE;

	interface = container_of(ptp, struct fm10k_intfc, ptp_caps);
	hw = &interface->hw;

	/* we simply cannot support the operation if we don't have BAR4 */
	if (!hw->sw_addr)
		return -ENOTSUPP;

	/* convert to unsigned 64b ns, verify we can put it in a 32b register */
	period = t->sec * 1000000000LL + t->nsec;

	/* determine the minimum size for period */
	step = 2 * (fm10k_read_reg(hw, FM10K_SYSTIME_CFG) &
		    FM10K_SYSTIME_CFG_STEP_MASK);

	/* verify the value is in range supported by hardware */
	if ((period && (period < step)) || (period > U32_MAX))
		return -ERANGE;

	/* notify hardware of request to being sending pulses */
	fm10k_write_sw_reg(hw, FM10K_SW_SYSTIME_PULSE(rq->perout.index),
			   (u32)period);

	return 0;
}

static struct ptp_pin_desc fm10k_ptp_pd[2] = {
	{
		.name = "IEEE1588_PULSE0",
		.index = 0,
		.func = PTP_PF_PEROUT,
		.chan = 0
	},
	{
		.name = "IEEE1588_PULSE1",
		.index = 1,
		.func = PTP_PF_PEROUT,
		.chan = 1
	}
};

static int fm10k_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
			    enum ptp_pin_function func, unsigned int chan)
{
	/* verify the requested pin is there */
	if (pin >= ptp->n_pins || !ptp->pin_config)
		return -EINVAL;

	/* enforce locked channels, no changing them */
	if (chan != ptp->pin_config[pin].chan)
		return -EINVAL;

	/* we want to keep the functions locked as well */
	if (func != ptp->pin_config[pin].func)
		return -EINVAL;

	return 0;
}

void fm10k_ptp_register(struct fm10k_intfc *interface)
{
	struct ptp_clock_info *ptp_caps = &interface->ptp_caps;
	struct device *dev = &interface->pdev->dev;
	struct ptp_clock *ptp_clock;

	snprintf(ptp_caps->name, sizeof(ptp_caps->name),
		 "%s", interface->netdev->name);
	ptp_caps->owner		= THIS_MODULE;
	/* This math is simply the inverse of the math in
	 * fm10k_adjust_systime_pf applied to an adjustment value
	 * of 2^30 - 1 which is the maximum value of the register:
	 * 	max_ppb == ((2^30 - 1) * 5^9) / 2^31
	 */
	ptp_caps->max_adj	= 976562;
	ptp_caps->adjfreq	= fm10k_ptp_adjfreq;
	ptp_caps->adjtime	= fm10k_ptp_adjtime;
	ptp_caps->gettime64	= fm10k_ptp_gettime;
	ptp_caps->settime64	= fm10k_ptp_settime;

	/* provide pins if BAR4 is accessible */
	if (interface->sw_addr) {
		/* enable periodic outputs */
		ptp_caps->n_per_out = 2;
		ptp_caps->enable	= fm10k_ptp_enable;

		/* enable clock pins */
		ptp_caps->verify	= fm10k_ptp_verify;
		ptp_caps->n_pins = 2;
		ptp_caps->pin_config = fm10k_ptp_pd;
	}

	ptp_clock = ptp_clock_register(ptp_caps, dev);
	if (IS_ERR(ptp_clock)) {
		ptp_clock = NULL;
		dev_err(dev, "ptp_clock_register failed\n");
	} else {
		dev_info(dev, "registered PHC device %s\n", ptp_caps->name);
	}

	interface->ptp_clock = ptp_clock;
}

void fm10k_ptp_unregister(struct fm10k_intfc *interface)
{
	struct ptp_clock *ptp_clock = interface->ptp_clock;
	struct device *dev = &interface->pdev->dev;

	if (!ptp_clock)
		return;

	interface->ptp_clock = NULL;

	ptp_clock_unregister(ptp_clock);
	dev_info(dev, "removed PHC %s\n", interface->ptp_caps.name);
}
