// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include "i40e.h"
#include <linux/ptp_classify.h>

/* The XL710 timesync is very much like Intel's 82599 design when it comes to
 * the fundamental clock design. However, the clock operations are much simpler
 * in the XL710 because the device supports a full 64 bits of nanoseconds.
 * Because the field is so wide, we can forgo the cycle counter and just
 * operate with the nanosecond field directly without fear of overflow.
 *
 * Much like the 82599, the update period is dependent upon the link speed:
 * At 40Gb link or no link, the period is 1.6ns.
 * At 10Gb link, the period is multiplied by 2. (3.2ns)
 * At 1Gb link, the period is multiplied by 20. (32ns)
 * 1588 functionality is not supported at 100Mbps.
 */
#define I40E_PTP_40GB_INCVAL		0x0199999999ULL
#define I40E_PTP_10GB_INCVAL_MULT	2
#define I40E_PTP_1GB_INCVAL_MULT	20

#define I40E_PRTTSYN_CTL1_TSYNTYPE_V1  BIT(I40E_PRTTSYN_CTL1_TSYNTYPE_SHIFT)
#define I40E_PRTTSYN_CTL1_TSYNTYPE_V2  (2 << \
					I40E_PRTTSYN_CTL1_TSYNTYPE_SHIFT)

/**
 * i40e_ptp_read - Read the PHC time from the device
 * @pf: Board private structure
 * @ts: timespec structure to hold the current time value
 * @sts: structure to hold the system time before and after reading the PHC
 *
 * This function reads the PRTTSYN_TIME registers and stores them in a
 * timespec. However, since the registers are 64 bits of nanoseconds, we must
 * convert the result to a timespec before we can return.
 **/
static void i40e_ptp_read(struct i40e_pf *pf, struct timespec64 *ts,
			  struct ptp_system_timestamp *sts)
{
	struct i40e_hw *hw = &pf->hw;
	u32 hi, lo;
	u64 ns;

	/* The timer latches on the lowest register read. */
	ptp_read_system_prets(sts);
	lo = rd32(hw, I40E_PRTTSYN_TIME_L);
	ptp_read_system_postts(sts);
	hi = rd32(hw, I40E_PRTTSYN_TIME_H);

	ns = (((u64)hi) << 32) | lo;

	*ts = ns_to_timespec64(ns);
}

/**
 * i40e_ptp_write - Write the PHC time to the device
 * @pf: Board private structure
 * @ts: timespec structure that holds the new time value
 *
 * This function writes the PRTTSYN_TIME registers with the user value. Since
 * we receive a timespec from the stack, we must convert that timespec into
 * nanoseconds before programming the registers.
 **/
static void i40e_ptp_write(struct i40e_pf *pf, const struct timespec64 *ts)
{
	struct i40e_hw *hw = &pf->hw;
	u64 ns = timespec64_to_ns(ts);

	/* The timer will not update until the high register is written, so
	 * write the low register first.
	 */
	wr32(hw, I40E_PRTTSYN_TIME_L, ns & 0xFFFFFFFF);
	wr32(hw, I40E_PRTTSYN_TIME_H, ns >> 32);
}

/**
 * i40e_ptp_convert_to_hwtstamp - Convert device clock to system time
 * @hwtstamps: Timestamp structure to update
 * @timestamp: Timestamp from the hardware
 *
 * We need to convert the NIC clock value into a hwtstamp which can be used by
 * the upper level timestamping functions. Since the timestamp is simply a 64-
 * bit nanosecond value, we can call ns_to_ktime directly to handle this.
 **/
static void i40e_ptp_convert_to_hwtstamp(struct skb_shared_hwtstamps *hwtstamps,
					 u64 timestamp)
{
	memset(hwtstamps, 0, sizeof(*hwtstamps));

	hwtstamps->hwtstamp = ns_to_ktime(timestamp);
}

/**
 * i40e_ptp_adjfreq - Adjust the PHC frequency
 * @ptp: The PTP clock structure
 * @ppb: Parts per billion adjustment from the base
 *
 * Adjust the frequency of the PHC by the indicated parts per billion from the
 * base frequency.
 **/
static int i40e_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);
	struct i40e_hw *hw = &pf->hw;
	u64 adj, freq, diff;
	int neg_adj = 0;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	freq = I40E_PTP_40GB_INCVAL;
	freq *= ppb;
	diff = div_u64(freq, 1000000000ULL);

	if (neg_adj)
		adj = I40E_PTP_40GB_INCVAL - diff;
	else
		adj = I40E_PTP_40GB_INCVAL + diff;

	/* At some link speeds, the base incval is so large that directly
	 * multiplying by ppb would result in arithmetic overflow even when
	 * using a u64. Avoid this by instead calculating the new incval
	 * always in terms of the 40GbE clock rate and then multiplying by the
	 * link speed factor afterwards. This does result in slightly lower
	 * precision at lower link speeds, but it is fairly minor.
	 */
	smp_mb(); /* Force any pending update before accessing. */
	adj *= READ_ONCE(pf->ptp_adj_mult);

	wr32(hw, I40E_PRTTSYN_INC_L, adj & 0xFFFFFFFF);
	wr32(hw, I40E_PRTTSYN_INC_H, adj >> 32);

	return 0;
}

/**
 * i40e_ptp_adjtime - Adjust the PHC time
 * @ptp: The PTP clock structure
 * @delta: Offset in nanoseconds to adjust the PHC time by
 *
 * Adjust the frequency of the PHC by the indicated parts per billion from the
 * base frequency.
 **/
static int i40e_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);
	struct timespec64 now;

	mutex_lock(&pf->tmreg_lock);

	i40e_ptp_read(pf, &now, NULL);
	timespec64_add_ns(&now, delta);
	i40e_ptp_write(pf, (const struct timespec64 *)&now);

	mutex_unlock(&pf->tmreg_lock);

	return 0;
}

/**
 * i40e_ptp_gettimex - Get the time of the PHC
 * @ptp: The PTP clock structure
 * @ts: timespec structure to hold the current time value
 * @sts: structure to hold the system time before and after reading the PHC
 *
 * Read the device clock and return the correct value on ns, after converting it
 * into a timespec struct.
 **/
static int i40e_ptp_gettimex(struct ptp_clock_info *ptp, struct timespec64 *ts,
			     struct ptp_system_timestamp *sts)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);

	mutex_lock(&pf->tmreg_lock);
	i40e_ptp_read(pf, ts, sts);
	mutex_unlock(&pf->tmreg_lock);

	return 0;
}

/**
 * i40e_ptp_settime - Set the time of the PHC
 * @ptp: The PTP clock structure
 * @ts: timespec structure that holds the new time value
 *
 * Set the device clock to the user input value. The conversion from timespec
 * to ns happens in the write function.
 **/
static int i40e_ptp_settime(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);

	mutex_lock(&pf->tmreg_lock);
	i40e_ptp_write(pf, ts);
	mutex_unlock(&pf->tmreg_lock);

	return 0;
}

/**
 * i40e_ptp_feature_enable - Enable/disable ancillary features of the PHC subsystem
 * @ptp: The PTP clock structure
 * @rq: The requested feature to change
 * @on: Enable/disable flag
 *
 * The XL710 does not support any of the ancillary features of the PHC
 * subsystem, so this function may just return.
 **/
static int i40e_ptp_feature_enable(struct ptp_clock_info *ptp,
				   struct ptp_clock_request *rq, int on)
{
	return -EOPNOTSUPP;
}

/**
 * i40e_ptp_update_latch_events - Read I40E_PRTTSYN_STAT_1 and latch events
 * @pf: the PF data structure
 *
 * This function reads I40E_PRTTSYN_STAT_1 and updates the corresponding timers
 * for noticed latch events. This allows the driver to keep track of the first
 * time a latch event was noticed which will be used to help clear out Rx
 * timestamps for packets that got dropped or lost.
 *
 * This function will return the current value of I40E_PRTTSYN_STAT_1 and is
 * expected to be called only while under the ptp_rx_lock.
 **/
static u32 i40e_ptp_get_rx_events(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 prttsyn_stat, new_latch_events;
	int  i;

	prttsyn_stat = rd32(hw, I40E_PRTTSYN_STAT_1);
	new_latch_events = prttsyn_stat & ~pf->latch_event_flags;

	/* Update the jiffies time for any newly latched timestamp. This
	 * ensures that we store the time that we first discovered a timestamp
	 * was latched by the hardware. The service task will later determine
	 * if we should free the latch and drop that timestamp should too much
	 * time pass. This flow ensures that we only update jiffies for new
	 * events latched since the last time we checked, and not all events
	 * currently latched, so that the service task accounting remains
	 * accurate.
	 */
	for (i = 0; i < 4; i++) {
		if (new_latch_events & BIT(i))
			pf->latch_events[i] = jiffies;
	}

	/* Finally, we store the current status of the Rx timestamp latches */
	pf->latch_event_flags = prttsyn_stat;

	return prttsyn_stat;
}

/**
 * i40e_ptp_rx_hang - Detect error case when Rx timestamp registers are hung
 * @pf: The PF private data structure
 * @vsi: The VSI with the rings relevant to 1588
 *
 * This watchdog task is scheduled to detect error case where hardware has
 * dropped an Rx packet that was timestamped when the ring is full. The
 * particular error is rare but leaves the device in a state unable to timestamp
 * any future packets.
 **/
void i40e_ptp_rx_hang(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	unsigned int i, cleared = 0;

	/* Since we cannot turn off the Rx timestamp logic if the device is
	 * configured for Tx timestamping, we check if Rx timestamping is
	 * configured. We don't want to spuriously warn about Rx timestamp
	 * hangs if we don't care about the timestamps.
	 */
	if (!(pf->flags & I40E_FLAG_PTP) || !pf->ptp_rx)
		return;

	spin_lock_bh(&pf->ptp_rx_lock);

	/* Update current latch times for Rx events */
	i40e_ptp_get_rx_events(pf);

	/* Check all the currently latched Rx events and see whether they have
	 * been latched for over a second. It is assumed that any timestamp
	 * should have been cleared within this time, or else it was captured
	 * for a dropped frame that the driver never received. Thus, we will
	 * clear any timestamp that has been latched for over 1 second.
	 */
	for (i = 0; i < 4; i++) {
		if ((pf->latch_event_flags & BIT(i)) &&
		    time_is_before_jiffies(pf->latch_events[i] + HZ)) {
			rd32(hw, I40E_PRTTSYN_RXTIME_H(i));
			pf->latch_event_flags &= ~BIT(i);
			cleared++;
		}
	}

	spin_unlock_bh(&pf->ptp_rx_lock);

	/* Log a warning if more than 2 timestamps got dropped in the same
	 * check. We don't want to warn about all drops because it can occur
	 * in normal scenarios such as PTP frames on multicast addresses we
	 * aren't listening to. However, administrator should know if this is
	 * the reason packets aren't receiving timestamps.
	 */
	if (cleared > 2)
		dev_dbg(&pf->pdev->dev,
			"Dropped %d missed RXTIME timestamp events\n",
			cleared);

	/* Finally, update the rx_hwtstamp_cleared counter */
	pf->rx_hwtstamp_cleared += cleared;
}

/**
 * i40e_ptp_tx_hang - Detect error case when Tx timestamp register is hung
 * @pf: The PF private data structure
 *
 * This watchdog task is run periodically to make sure that we clear the Tx
 * timestamp logic if we don't obtain a timestamp in a reasonable amount of
 * time. It is unexpected in the normal case but if it occurs it results in
 * permanently preventing timestamps of future packets.
 **/
void i40e_ptp_tx_hang(struct i40e_pf *pf)
{
	struct sk_buff *skb;

	if (!(pf->flags & I40E_FLAG_PTP) || !pf->ptp_tx)
		return;

	/* Nothing to do if we're not already waiting for a timestamp */
	if (!test_bit(__I40E_PTP_TX_IN_PROGRESS, pf->state))
		return;

	/* We already have a handler routine which is run when we are notified
	 * of a Tx timestamp in the hardware. If we don't get an interrupt
	 * within a second it is reasonable to assume that we never will.
	 */
	if (time_is_before_jiffies(pf->ptp_tx_start + HZ)) {
		skb = pf->ptp_tx_skb;
		pf->ptp_tx_skb = NULL;
		clear_bit_unlock(__I40E_PTP_TX_IN_PROGRESS, pf->state);

		/* Free the skb after we clear the bitlock */
		dev_kfree_skb_any(skb);
		pf->tx_hwtstamp_timeouts++;
	}
}

/**
 * i40e_ptp_tx_hwtstamp - Utility function which returns the Tx timestamp
 * @pf: Board private structure
 *
 * Read the value of the Tx timestamp from the registers, convert it into a
 * value consumable by the stack, and store that result into the shhwtstamps
 * struct before returning it up the stack.
 **/
void i40e_ptp_tx_hwtstamp(struct i40e_pf *pf)
{
	struct skb_shared_hwtstamps shhwtstamps;
	struct sk_buff *skb = pf->ptp_tx_skb;
	struct i40e_hw *hw = &pf->hw;
	u32 hi, lo;
	u64 ns;

	if (!(pf->flags & I40E_FLAG_PTP) || !pf->ptp_tx)
		return;

	/* don't attempt to timestamp if we don't have an skb */
	if (!pf->ptp_tx_skb)
		return;

	lo = rd32(hw, I40E_PRTTSYN_TXTIME_L);
	hi = rd32(hw, I40E_PRTTSYN_TXTIME_H);

	ns = (((u64)hi) << 32) | lo;
	i40e_ptp_convert_to_hwtstamp(&shhwtstamps, ns);

	/* Clear the bit lock as soon as possible after reading the register,
	 * and prior to notifying the stack via skb_tstamp_tx(). Otherwise
	 * applications might wake up and attempt to request another transmit
	 * timestamp prior to the bit lock being cleared.
	 */
	pf->ptp_tx_skb = NULL;
	clear_bit_unlock(__I40E_PTP_TX_IN_PROGRESS, pf->state);

	/* Notify the stack and free the skb after we've unlocked */
	skb_tstamp_tx(skb, &shhwtstamps);
	dev_kfree_skb_any(skb);
}

/**
 * i40e_ptp_rx_hwtstamp - Utility function which checks for an Rx timestamp
 * @pf: Board private structure
 * @skb: Particular skb to send timestamp with
 * @index: Index into the receive timestamp registers for the timestamp
 *
 * The XL710 receives a notification in the receive descriptor with an offset
 * into the set of RXTIME registers where the timestamp is for that skb. This
 * function goes and fetches the receive timestamp from that offset, if a valid
 * one exists. The RXTIME registers are in ns, so we must convert the result
 * first.
 **/
void i40e_ptp_rx_hwtstamp(struct i40e_pf *pf, struct sk_buff *skb, u8 index)
{
	u32 prttsyn_stat, hi, lo;
	struct i40e_hw *hw;
	u64 ns;

	/* Since we cannot turn off the Rx timestamp logic if the device is
	 * doing Tx timestamping, check if Rx timestamping is configured.
	 */
	if (!(pf->flags & I40E_FLAG_PTP) || !pf->ptp_rx)
		return;

	hw = &pf->hw;

	spin_lock_bh(&pf->ptp_rx_lock);

	/* Get current Rx events and update latch times */
	prttsyn_stat = i40e_ptp_get_rx_events(pf);

	/* TODO: Should we warn about missing Rx timestamp event? */
	if (!(prttsyn_stat & BIT(index))) {
		spin_unlock_bh(&pf->ptp_rx_lock);
		return;
	}

	/* Clear the latched event since we're about to read its register */
	pf->latch_event_flags &= ~BIT(index);

	lo = rd32(hw, I40E_PRTTSYN_RXTIME_L(index));
	hi = rd32(hw, I40E_PRTTSYN_RXTIME_H(index));

	spin_unlock_bh(&pf->ptp_rx_lock);

	ns = (((u64)hi) << 32) | lo;

	i40e_ptp_convert_to_hwtstamp(skb_hwtstamps(skb), ns);
}

/**
 * i40e_ptp_set_increment - Utility function to update clock increment rate
 * @pf: Board private structure
 *
 * During a link change, the DMA frequency that drives the 1588 logic will
 * change. In order to keep the PRTTSYN_TIME registers in units of nanoseconds,
 * we must update the increment value per clock tick.
 **/
void i40e_ptp_set_increment(struct i40e_pf *pf)
{
	struct i40e_link_status *hw_link_info;
	struct i40e_hw *hw = &pf->hw;
	u64 incval;
	u32 mult;

	hw_link_info = &hw->phy.link_info;

	i40e_aq_get_link_info(&pf->hw, true, NULL, NULL);

	switch (hw_link_info->link_speed) {
	case I40E_LINK_SPEED_10GB:
		mult = I40E_PTP_10GB_INCVAL_MULT;
		break;
	case I40E_LINK_SPEED_1GB:
		mult = I40E_PTP_1GB_INCVAL_MULT;
		break;
	case I40E_LINK_SPEED_100MB:
	{
		static int warn_once;

		if (!warn_once) {
			dev_warn(&pf->pdev->dev,
				 "1588 functionality is not supported at 100 Mbps. Stopping the PHC.\n");
			warn_once++;
		}
		mult = 0;
		break;
	}
	case I40E_LINK_SPEED_40GB:
	default:
		mult = 1;
		break;
	}

	/* The increment value is calculated by taking the base 40GbE incvalue
	 * and multiplying it by a factor based on the link speed.
	 */
	incval = I40E_PTP_40GB_INCVAL * mult;

	/* Write the new increment value into the increment register. The
	 * hardware will not update the clock until both registers have been
	 * written.
	 */
	wr32(hw, I40E_PRTTSYN_INC_L, incval & 0xFFFFFFFF);
	wr32(hw, I40E_PRTTSYN_INC_H, incval >> 32);

	/* Update the base adjustement value. */
	WRITE_ONCE(pf->ptp_adj_mult, mult);
	smp_mb(); /* Force the above update. */
}

/**
 * i40e_ptp_get_ts_config - ioctl interface to read the HW timestamping
 * @pf: Board private structure
 * @ifr: ioctl data
 *
 * Obtain the current hardware timestamping settigs as requested. To do this,
 * keep a shadow copy of the timestamp settings rather than attempting to
 * deconstruct it from the registers.
 **/
int i40e_ptp_get_ts_config(struct i40e_pf *pf, struct ifreq *ifr)
{
	struct hwtstamp_config *config = &pf->tstamp_config;

	if (!(pf->flags & I40E_FLAG_PTP))
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/**
 * i40e_ptp_set_timestamp_mode - setup hardware for requested timestamp mode
 * @pf: Board private structure
 * @config: hwtstamp settings requested or saved
 *
 * Control hardware registers to enter the specific mode requested by the
 * user. Also used during reset path to ensure that timestamp settings are
 * maintained.
 *
 * Note: modifies config in place, and may update the requested mode to be
 * more broad if the specific filter is not directly supported.
 **/
static int i40e_ptp_set_timestamp_mode(struct i40e_pf *pf,
				       struct hwtstamp_config *config)
{
	struct i40e_hw *hw = &pf->hw;
	u32 tsyntype, regval;

	/* Reserved for future extensions. */
	if (config->flags)
		return -EINVAL;

	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		pf->ptp_tx = false;
		break;
	case HWTSTAMP_TX_ON:
		pf->ptp_tx = true;
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		pf->ptp_rx = false;
		/* We set the type to V1, but do not enable UDP packet
		 * recognition. In this way, we should be as close to
		 * disabling PTP Rx timestamps as possible since V1 packets
		 * are always UDP, since L2 packets are a V2 feature.
		 */
		tsyntype = I40E_PRTTSYN_CTL1_TSYNTYPE_V1;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
		if (!(pf->hw_features & I40E_HW_PTP_L4_CAPABLE))
			return -ERANGE;
		pf->ptp_rx = true;
		tsyntype = I40E_PRTTSYN_CTL1_V1MESSTYPE0_MASK |
			   I40E_PRTTSYN_CTL1_TSYNTYPE_V1 |
			   I40E_PRTTSYN_CTL1_UDP_ENA_MASK;
		config->rx_filter = HWTSTAMP_FILTER_PTP_V1_L4_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		if (!(pf->hw_features & I40E_HW_PTP_L4_CAPABLE))
			return -ERANGE;
		/* fall through */
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		pf->ptp_rx = true;
		tsyntype = I40E_PRTTSYN_CTL1_V2MESSTYPE0_MASK |
			   I40E_PRTTSYN_CTL1_TSYNTYPE_V2;
		if (pf->hw_features & I40E_HW_PTP_L4_CAPABLE) {
			tsyntype |= I40E_PRTTSYN_CTL1_UDP_ENA_MASK;
			config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		} else {
			config->rx_filter = HWTSTAMP_FILTER_PTP_V2_L2_EVENT;
		}
		break;
	case HWTSTAMP_FILTER_NTP_ALL:
	case HWTSTAMP_FILTER_ALL:
	default:
		return -ERANGE;
	}

	/* Clear out all 1588-related registers to clear and unlatch them. */
	spin_lock_bh(&pf->ptp_rx_lock);
	rd32(hw, I40E_PRTTSYN_STAT_0);
	rd32(hw, I40E_PRTTSYN_TXTIME_H);
	rd32(hw, I40E_PRTTSYN_RXTIME_H(0));
	rd32(hw, I40E_PRTTSYN_RXTIME_H(1));
	rd32(hw, I40E_PRTTSYN_RXTIME_H(2));
	rd32(hw, I40E_PRTTSYN_RXTIME_H(3));
	pf->latch_event_flags = 0;
	spin_unlock_bh(&pf->ptp_rx_lock);

	/* Enable/disable the Tx timestamp interrupt based on user input. */
	regval = rd32(hw, I40E_PRTTSYN_CTL0);
	if (pf->ptp_tx)
		regval |= I40E_PRTTSYN_CTL0_TXTIME_INT_ENA_MASK;
	else
		regval &= ~I40E_PRTTSYN_CTL0_TXTIME_INT_ENA_MASK;
	wr32(hw, I40E_PRTTSYN_CTL0, regval);

	regval = rd32(hw, I40E_PFINT_ICR0_ENA);
	if (pf->ptp_tx)
		regval |= I40E_PFINT_ICR0_ENA_TIMESYNC_MASK;
	else
		regval &= ~I40E_PFINT_ICR0_ENA_TIMESYNC_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, regval);

	/* Although there is no simple on/off switch for Rx, we "disable" Rx
	 * timestamps by setting to V1 only mode and clear the UDP
	 * recognition. This ought to disable all PTP Rx timestamps as V1
	 * packets are always over UDP. Note that software is configured to
	 * ignore Rx timestamps via the pf->ptp_rx flag.
	 */
	regval = rd32(hw, I40E_PRTTSYN_CTL1);
	/* clear everything but the enable bit */
	regval &= I40E_PRTTSYN_CTL1_TSYNENA_MASK;
	/* now enable bits for desired Rx timestamps */
	regval |= tsyntype;
	wr32(hw, I40E_PRTTSYN_CTL1, regval);

	return 0;
}

/**
 * i40e_ptp_set_ts_config - ioctl interface to control the HW timestamping
 * @pf: Board private structure
 * @ifr: ioctl data
 *
 * Respond to the user filter requests and make the appropriate hardware
 * changes here. The XL710 cannot support splitting of the Tx/Rx timestamping
 * logic, so keep track in software of whether to indicate these timestamps
 * or not.
 *
 * It is permissible to "upgrade" the user request to a broader filter, as long
 * as the user receives the timestamps they care about and the user is notified
 * the filter has been broadened.
 **/
int i40e_ptp_set_ts_config(struct i40e_pf *pf, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (!(pf->flags & I40E_FLAG_PTP))
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = i40e_ptp_set_timestamp_mode(pf, &config);
	if (err)
		return err;

	/* save these settings for future reference */
	pf->tstamp_config = config;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/**
 * i40e_ptp_create_clock - Create PTP clock device for userspace
 * @pf: Board private structure
 *
 * This function creates a new PTP clock device. It only creates one if we
 * don't already have one, so it is safe to call. Will return error if it
 * can't create one, but success if we already have a device. Should be used
 * by i40e_ptp_init to create clock initially, and prevent global resets from
 * creating new clock devices.
 **/
static long i40e_ptp_create_clock(struct i40e_pf *pf)
{
	/* no need to create a clock device if we already have one */
	if (!IS_ERR_OR_NULL(pf->ptp_clock))
		return 0;

	strlcpy(pf->ptp_caps.name, i40e_driver_name,
		sizeof(pf->ptp_caps.name) - 1);
	pf->ptp_caps.owner = THIS_MODULE;
	pf->ptp_caps.max_adj = 999999999;
	pf->ptp_caps.n_ext_ts = 0;
	pf->ptp_caps.pps = 0;
	pf->ptp_caps.adjfreq = i40e_ptp_adjfreq;
	pf->ptp_caps.adjtime = i40e_ptp_adjtime;
	pf->ptp_caps.gettimex64 = i40e_ptp_gettimex;
	pf->ptp_caps.settime64 = i40e_ptp_settime;
	pf->ptp_caps.enable = i40e_ptp_feature_enable;

	/* Attempt to register the clock before enabling the hardware. */
	pf->ptp_clock = ptp_clock_register(&pf->ptp_caps, &pf->pdev->dev);
	if (IS_ERR(pf->ptp_clock))
		return PTR_ERR(pf->ptp_clock);

	/* clear the hwtstamp settings here during clock create, instead of
	 * during regular init, so that we can maintain settings across a
	 * reset or suspend.
	 */
	pf->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	pf->tstamp_config.tx_type = HWTSTAMP_TX_OFF;

	return 0;
}

/**
 * i40e_ptp_init - Initialize the 1588 support after device probe or reset
 * @pf: Board private structure
 *
 * This function sets device up for 1588 support. The first time it is run, it
 * will create a PHC clock device. It does not create a clock device if one
 * already exists. It also reconfigures the device after a reset.
 **/
void i40e_ptp_init(struct i40e_pf *pf)
{
	struct net_device *netdev = pf->vsi[pf->lan_vsi]->netdev;
	struct i40e_hw *hw = &pf->hw;
	u32 pf_id;
	long err;

	/* Only one PF is assigned to control 1588 logic per port. Do not
	 * enable any support for PFs not assigned via PRTTSYN_CTL0.PF_ID
	 */
	pf_id = (rd32(hw, I40E_PRTTSYN_CTL0) & I40E_PRTTSYN_CTL0_PF_ID_MASK) >>
		I40E_PRTTSYN_CTL0_PF_ID_SHIFT;
	if (hw->pf_id != pf_id) {
		pf->flags &= ~I40E_FLAG_PTP;
		dev_info(&pf->pdev->dev, "%s: PTP not supported on %s\n",
			 __func__,
			 netdev->name);
		return;
	}

	mutex_init(&pf->tmreg_lock);
	spin_lock_init(&pf->ptp_rx_lock);

	/* ensure we have a clock device */
	err = i40e_ptp_create_clock(pf);
	if (err) {
		pf->ptp_clock = NULL;
		dev_err(&pf->pdev->dev, "%s: ptp_clock_register failed\n",
			__func__);
	} else if (pf->ptp_clock) {
		struct timespec64 ts;
		u32 regval;

		if (pf->hw.debug_mask & I40E_DEBUG_LAN)
			dev_info(&pf->pdev->dev, "PHC enabled\n");
		pf->flags |= I40E_FLAG_PTP;

		/* Ensure the clocks are running. */
		regval = rd32(hw, I40E_PRTTSYN_CTL0);
		regval |= I40E_PRTTSYN_CTL0_TSYNENA_MASK;
		wr32(hw, I40E_PRTTSYN_CTL0, regval);
		regval = rd32(hw, I40E_PRTTSYN_CTL1);
		regval |= I40E_PRTTSYN_CTL1_TSYNENA_MASK;
		wr32(hw, I40E_PRTTSYN_CTL1, regval);

		/* Set the increment value per clock tick. */
		i40e_ptp_set_increment(pf);

		/* reset timestamping mode */
		i40e_ptp_set_timestamp_mode(pf, &pf->tstamp_config);

		/* Set the clock value. */
		ts = ktime_to_timespec64(ktime_get_real());
		i40e_ptp_settime(&pf->ptp_caps, &ts);
	}
}

/**
 * i40e_ptp_stop - Disable the driver/hardware support and unregister the PHC
 * @pf: Board private structure
 *
 * This function handles the cleanup work required from the initialization by
 * clearing out the important information and unregistering the PHC.
 **/
void i40e_ptp_stop(struct i40e_pf *pf)
{
	pf->flags &= ~I40E_FLAG_PTP;
	pf->ptp_tx = false;
	pf->ptp_rx = false;

	if (pf->ptp_tx_skb) {
		struct sk_buff *skb = pf->ptp_tx_skb;

		pf->ptp_tx_skb = NULL;
		clear_bit_unlock(__I40E_PTP_TX_IN_PROGRESS, pf->state);
		dev_kfree_skb_any(skb);
	}

	if (pf->ptp_clock) {
		ptp_clock_unregister(pf->ptp_clock);
		pf->ptp_clock = NULL;
		dev_info(&pf->pdev->dev, "%s: removed PHC on %s\n", __func__,
			 pf->vsi[pf->lan_vsi]->netdev->name);
	}
}
