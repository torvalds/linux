/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/
#include "ixgbe.h"
#include <linux/export.h>
#include <linux/ptp_classify.h>

/*
 * The 82599 and the X540 do not have true 64bit nanosecond scale
 * counter registers. Instead, SYSTIME is defined by a fixed point
 * system which allows the user to define the scale counter increment
 * value at every level change of the oscillator driving the SYSTIME
 * value. For both devices the TIMINCA:IV field defines this
 * increment. On the X540 device, 31 bits are provided. However on the
 * 82599 only provides 24 bits. The time unit is determined by the
 * clock frequency of the oscillator in combination with the TIMINCA
 * register. When these devices link at 10Gb the oscillator has a
 * period of 6.4ns. In order to convert the scale counter into
 * nanoseconds the cyclecounter and timecounter structures are
 * used. The SYSTIME registers need to be converted to ns values by use
 * of only a right shift (division by power of 2). The following math
 * determines the largest incvalue that will fit into the available
 * bits in the TIMINCA register.
 *
 * PeriodWidth: Number of bits to store the clock period
 * MaxWidth: The maximum width value of the TIMINCA register
 * Period: The clock period for the oscillator
 * round(): discard the fractional portion of the calculation
 *
 * Period * [ 2 ^ ( MaxWidth - PeriodWidth ) ]
 *
 * For the X540, MaxWidth is 31 bits, and the base period is 6.4 ns
 * For the 82599, MaxWidth is 24 bits, and the base period is 6.4 ns
 *
 * The period also changes based on the link speed:
 * At 10Gb link or no link, the period remains the same.
 * At 1Gb link, the period is multiplied by 10. (64ns)
 * At 100Mb link, the period is multiplied by 100. (640ns)
 *
 * The calculated value allows us to right shift the SYSTIME register
 * value in order to quickly convert it into a nanosecond clock,
 * while allowing for the maximum possible adjustment value.
 *
 * These diagrams are only for the 10Gb link period
 *
 *           SYSTIMEH            SYSTIMEL
 *       +--------------+  +--------------+
 * X540  |      32      |  | 1 | 3 |  28  |
 *       *--------------+  +--------------+
 *        \________ 36 bits ______/  fract
 *
 *       +--------------+  +--------------+
 * 82599 |      32      |  | 8 | 3 |  21  |
 *       *--------------+  +--------------+
 *        \________ 43 bits ______/  fract
 *
 * The 36 bit X540 SYSTIME overflows every
 *   2^36 * 10^-9 / 60 = 1.14 minutes or 69 seconds
 *
 * The 43 bit 82599 SYSTIME overflows every
 *   2^43 * 10^-9 / 3600 = 2.4 hours
 */
#define IXGBE_INCVAL_10GB 0x66666666
#define IXGBE_INCVAL_1GB  0x40000000
#define IXGBE_INCVAL_100  0x50000000

#define IXGBE_INCVAL_SHIFT_10GB  28
#define IXGBE_INCVAL_SHIFT_1GB   24
#define IXGBE_INCVAL_SHIFT_100   21

#define IXGBE_INCVAL_SHIFT_82599 7
#define IXGBE_INCPER_SHIFT_82599 24
#define IXGBE_MAX_TIMEADJ_VALUE  0x7FFFFFFFFFFFFFFFULL

#define IXGBE_OVERFLOW_PERIOD    (HZ * 30)

#ifndef NSECS_PER_SEC
#define NSECS_PER_SEC 1000000000ULL
#endif

static struct sock_filter ptp_filter[] = {
	PTP_FILTER
};

/**
 * ixgbe_ptp_setup_sdp
 * @hw: the hardware private structure
 *
 * this function enables or disables the clock out feature on SDP0 for
 * the X540 device. It will create a 1second periodic output that can
 * be used as the PPS (via an interrupt).
 *
 * It calculates when the systime will be on an exact second, and then
 * aligns the start of the PPS signal to that value. The shift is
 * necessary because it can change based on the link speed.
 */
static void ixgbe_ptp_setup_sdp(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int shift = adapter->cc.shift;
	u32 esdp, tsauxc, clktiml, clktimh, trgttiml, trgttimh, rem;
	u64 ns = 0, clock_edge = 0;

	if ((adapter->flags2 & IXGBE_FLAG2_PTP_PPS_ENABLED) &&
	    (hw->mac.type == ixgbe_mac_X540)) {

		/* disable the pin first */
		IXGBE_WRITE_REG(hw, IXGBE_TSAUXC, 0x0);
		IXGBE_WRITE_FLUSH(hw);

		esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);

		/*
		 * enable the SDP0 pin as output, and connected to the
		 * native function for Timesync (ClockOut)
		 */
		esdp |= (IXGBE_ESDP_SDP0_DIR |
			 IXGBE_ESDP_SDP0_NATIVE);

		/*
		 * enable the Clock Out feature on SDP0, and allow
		 * interrupts to occur when the pin changes
		 */
		tsauxc = (IXGBE_TSAUXC_EN_CLK |
			  IXGBE_TSAUXC_SYNCLK |
			  IXGBE_TSAUXC_SDP0_INT);

		/* clock period (or pulse length) */
		clktiml = (u32)(NSECS_PER_SEC << shift);
		clktimh = (u32)((NSECS_PER_SEC << shift) >> 32);

		/*
		 * Account for the cyclecounter wrap-around value by
		 * using the converted ns value of the current time to
		 * check for when the next aligned second would occur.
		 */
		clock_edge |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIML);
		clock_edge |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIMH) << 32;
		ns = timecounter_cyc2time(&adapter->tc, clock_edge);

		div_u64_rem(ns, NSECS_PER_SEC, &rem);
		clock_edge += ((NSECS_PER_SEC - (u64)rem) << shift);

		/* specify the initial clock start time */
		trgttiml = (u32)clock_edge;
		trgttimh = (u32)(clock_edge >> 32);

		IXGBE_WRITE_REG(hw, IXGBE_CLKTIML, clktiml);
		IXGBE_WRITE_REG(hw, IXGBE_CLKTIMH, clktimh);
		IXGBE_WRITE_REG(hw, IXGBE_TRGTTIML0, trgttiml);
		IXGBE_WRITE_REG(hw, IXGBE_TRGTTIMH0, trgttimh);

		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
		IXGBE_WRITE_REG(hw, IXGBE_TSAUXC, tsauxc);
	} else {
		IXGBE_WRITE_REG(hw, IXGBE_TSAUXC, 0x0);
	}

	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_ptp_read - read raw cycle counter (to be used by time counter)
 * @cc: the cyclecounter structure
 *
 * this function reads the cyclecounter registers and is called by the
 * cyclecounter structure used to construct a ns counter from the
 * arbitrary fixed point registers
 */
static cycle_t ixgbe_ptp_read(const struct cyclecounter *cc)
{
	struct ixgbe_adapter *adapter =
		container_of(cc, struct ixgbe_adapter, cc);
	struct ixgbe_hw *hw = &adapter->hw;
	u64 stamp = 0;

	stamp |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIML);
	stamp |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIMH) << 32;

	return stamp;
}

/**
 * ixgbe_ptp_adjfreq
 * @ptp: the ptp clock structure
 * @ppb: parts per billion adjustment from base
 *
 * adjust the frequency of the ptp cycle counter by the
 * indicated ppb from the base frequency.
 */
static int ixgbe_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);
	struct ixgbe_hw *hw = &adapter->hw;
	u64 freq;
	u32 diff, incval;
	int neg_adj = 0;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	smp_mb();
	incval = ACCESS_ONCE(adapter->base_incval);

	freq = incval;
	freq *= ppb;
	diff = div_u64(freq, 1000000000ULL);

	incval = neg_adj ? (incval - diff) : (incval + diff);

	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA, incval);
		break;
	case ixgbe_mac_82599EB:
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA,
				(1 << IXGBE_INCPER_SHIFT_82599) |
				incval);
		break;
	default:
		break;
	}

	return 0;
}

/**
 * ixgbe_ptp_adjtime
 * @ptp: the ptp clock structure
 * @delta: offset to adjust the cycle counter by
 *
 * adjust the timer by resetting the timecounter structure.
 */
static int ixgbe_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);
	unsigned long flags;
	u64 now;

	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	now = timecounter_read(&adapter->tc);
	now += delta;

	/* reset the timecounter */
	timecounter_init(&adapter->tc,
			 &adapter->cc,
			 now);

	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	ixgbe_ptp_setup_sdp(adapter);

	return 0;
}

/**
 * ixgbe_ptp_gettime
 * @ptp: the ptp clock structure
 * @ts: timespec structure to hold the current time value
 *
 * read the timecounter and return the correct value on ns,
 * after converting it into a struct timespec.
 */
static int ixgbe_ptp_gettime(struct ptp_clock_info *ptp, struct timespec *ts)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);
	u64 ns;
	u32 remainder;
	unsigned long flags;

	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	ns = timecounter_read(&adapter->tc);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	ts->tv_sec = div_u64_rem(ns, 1000000000ULL, &remainder);
	ts->tv_nsec = remainder;

	return 0;
}

/**
 * ixgbe_ptp_settime
 * @ptp: the ptp clock structure
 * @ts: the timespec containing the new time for the cycle counter
 *
 * reset the timecounter to use a new base value instead of the kernel
 * wall timer value.
 */
static int ixgbe_ptp_settime(struct ptp_clock_info *ptp,
			     const struct timespec *ts)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);
	u64 ns;
	unsigned long flags;

	ns = ts->tv_sec * 1000000000ULL;
	ns += ts->tv_nsec;

	/* reset the timecounter */
	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	timecounter_init(&adapter->tc, &adapter->cc, ns);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	ixgbe_ptp_setup_sdp(adapter);
	return 0;
}

/**
 * ixgbe_ptp_enable
 * @ptp: the ptp clock structure
 * @rq: the requested feature to change
 * @on: whether to enable or disable the feature
 *
 * enable (or disable) ancillary features of the phc subsystem.
 * our driver only supports the PPS feature on the X540
 */
static int ixgbe_ptp_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *rq, int on)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);

	/**
	 * When PPS is enabled, unmask the interrupt for the ClockOut
	 * feature, so that the interrupt handler can send the PPS
	 * event when the clock SDP triggers. Clear mask when PPS is
	 * disabled
	 */
	if (rq->type == PTP_CLK_REQ_PPS) {
		switch (adapter->hw.mac.type) {
		case ixgbe_mac_X540:
			if (on)
				adapter->flags2 |= IXGBE_FLAG2_PTP_PPS_ENABLED;
			else
				adapter->flags2 &= ~IXGBE_FLAG2_PTP_PPS_ENABLED;

			ixgbe_ptp_setup_sdp(adapter);
			return 0;
		default:
			break;
		}
	}

	return -ENOTSUPP;
}

/**
 * ixgbe_ptp_check_pps_event
 * @adapter: the private adapter structure
 * @eicr: the interrupt cause register value
 *
 * This function is called by the interrupt routine when checking for
 * interrupts. It will check and handle a pps event.
 */
void ixgbe_ptp_check_pps_event(struct ixgbe_adapter *adapter, u32 eicr)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ptp_clock_event event;

	event.type = PTP_CLOCK_PPS;

	/* this check is necessary in case the interrupt was enabled via some
	 * alternative means (ex. debug_fs). Better to check here than
	 * everywhere that calls this function.
	 */
	if (!adapter->ptp_clock)
		return;

	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		ptp_clock_event(adapter->ptp_clock, &event);
		break;
	default:
		break;
	}
}


/**
 * ixgbe_ptp_overflow_check - delayed work to detect SYSTIME overflow
 * @work: structure containing information about this work task
 *
 * this work function is scheduled to continue reading the timecounter
 * in order to prevent missing when the system time registers wrap
 * around. This needs to be run approximately twice a minute when no
 * PTP activity is occurring.
 */
void ixgbe_ptp_overflow_check(struct ixgbe_adapter *adapter)
{
	unsigned long elapsed_jiffies = adapter->last_overflow_check - jiffies;
	struct timespec ts;

	if ((adapter->flags2 & IXGBE_FLAG2_PTP_ENABLED) &&
	    (elapsed_jiffies >= IXGBE_OVERFLOW_PERIOD)) {
		ixgbe_ptp_gettime(&adapter->ptp_caps, &ts);
		adapter->last_overflow_check = jiffies;
	}
}

/**
 * ixgbe_ptp_match - determine if this skb matches a ptp packet
 * @skb: pointer to the skb
 * @hwtstamp: pointer to the hwtstamp_config to check
 *
 * Determine whether the skb should have been timestamped, assuming the
 * hwtstamp was set via the hwtstamp ioctl. Returns non-zero when the packet
 * should have a timestamp waiting in the registers, and 0 otherwise.
 *
 * V1 packets have to check the version type to determine whether they are
 * correct. However, we can't directly access the data because it might be
 * fragmented in the SKB, in paged memory. In order to work around this, we
 * use skb_copy_bits which will properly copy the data whether it is in the
 * paged memory fragments or not. We have to copy the IP header as well as the
 * message type.
 */
static int ixgbe_ptp_match(struct sk_buff *skb, int rx_filter)
{
	struct iphdr iph;
	u8 msgtype;
	unsigned int type, offset;

	if (rx_filter == HWTSTAMP_FILTER_NONE)
		return 0;

	type = sk_run_filter(skb, ptp_filter);

	if (likely(rx_filter == HWTSTAMP_FILTER_PTP_V2_EVENT))
		return type & PTP_CLASS_V2;

	/* For the remaining cases actually check message type */
	switch (type) {
	case PTP_CLASS_V1_IPV4:
		skb_copy_bits(skb, OFF_IHL, &iph, sizeof(iph));
		offset = ETH_HLEN + (iph.ihl << 2) + UDP_HLEN + OFF_PTP_CONTROL;
		break;
	case PTP_CLASS_V1_IPV6:
		offset = OFF_PTP6 + OFF_PTP_CONTROL;
		break;
	default:
		/* other cases invalid or handled above */
		return 0;
	}

	/* Make sure our buffer is long enough */
	if (skb->len < offset)
		return 0;

	skb_copy_bits(skb, offset, &msgtype, sizeof(msgtype));

	switch (rx_filter) {
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		return (msgtype == IXGBE_RXMTRL_V1_SYNC_MSG);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		return (msgtype == IXGBE_RXMTRL_V1_DELAY_REQ_MSG);
		break;
	default:
		return 0;
	}
}

/**
 * ixgbe_ptp_tx_hwtstamp - utility function which checks for TX time stamp
 * @q_vector: structure containing interrupt and ring information
 * @skb: particular skb to send timestamp with
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the shhwtstamps structure which
 * is passed up the network stack
 */
void ixgbe_ptp_tx_hwtstamp(struct ixgbe_q_vector *q_vector,
			   struct sk_buff *skb)
{
	struct ixgbe_adapter *adapter;
	struct ixgbe_hw *hw;
	struct skb_shared_hwtstamps shhwtstamps;
	u64 regval = 0, ns;
	u32 tsynctxctl;
	unsigned long flags;

	/* we cannot process timestamps on a ring without a q_vector */
	if (!q_vector || !q_vector->adapter)
		return;

	adapter = q_vector->adapter;
	hw = &adapter->hw;

	tsynctxctl = IXGBE_READ_REG(hw, IXGBE_TSYNCTXCTL);
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_TXSTMPL);
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_TXSTMPH) << 32;

	/*
	 * if TX timestamp is not valid, exit after clearing the
	 * timestamp registers
	 */
	if (!(tsynctxctl & IXGBE_TSYNCTXCTL_VALID))
		return;

	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	ns = timecounter_cyc2time(&adapter->tc, regval);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	memset(&shhwtstamps, 0, sizeof(shhwtstamps));
	shhwtstamps.hwtstamp = ns_to_ktime(ns);
	skb_tstamp_tx(skb, &shhwtstamps);
}

/**
 * ixgbe_ptp_rx_hwtstamp - utility function which checks for RX time stamp
 * @q_vector: structure containing interrupt and ring information
 * @rx_desc: the rx descriptor
 * @skb: particular skb to send timestamp with
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the shhwtstamps structure which
 * is passed up the network stack
 */
void ixgbe_ptp_rx_hwtstamp(struct ixgbe_q_vector *q_vector,
			   union ixgbe_adv_rx_desc *rx_desc,
			   struct sk_buff *skb)
{
	struct ixgbe_adapter *adapter;
	struct ixgbe_hw *hw;
	struct skb_shared_hwtstamps *shhwtstamps;
	u64 regval = 0, ns;
	u32 tsyncrxctl;
	unsigned long flags;

	/* we cannot process timestamps on a ring without a q_vector */
	if (!q_vector || !q_vector->adapter)
		return;

	adapter = q_vector->adapter;
	hw = &adapter->hw;

	if (likely(!ixgbe_ptp_match(skb, adapter->rx_hwtstamp_filter)))
		return;

	tsyncrxctl = IXGBE_READ_REG(hw, IXGBE_TSYNCRXCTL);

	/* Check if we have a valid timestamp and make sure the skb should
	 * have been timestamped */
	if (!(tsyncrxctl & IXGBE_TSYNCRXCTL_VALID))
		return;

	/*
	 * Always read the registers, in order to clear a possible fault
	 * because of stagnant RX timestamp values for a packet that never
	 * reached the queue.
	 */
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_RXSTMPL);
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_RXSTMPH) << 32;

	/*
	 * If the timestamp bit is set in the packet's descriptor, we know the
	 * timestamp belongs to this packet. No other packet can be
	 * timestamped until the registers for timestamping have been read.
	 * Therefor only one packet with this bit can be in the queue at a
	 * time, and the rx timestamp values that were in the registers belong
	 * to this packet.
	 *
	 * If nothing went wrong, then it should have a skb_shared_tx that we
	 * can turn into a skb_shared_hwtstamps.
	 */
	if (unlikely(!ixgbe_test_staterr(rx_desc, IXGBE_RXDADV_STAT_TS)))
		return;

	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	ns = timecounter_cyc2time(&adapter->tc, regval);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	shhwtstamps = skb_hwtstamps(skb);
	shhwtstamps->hwtstamp = ns_to_ktime(ns);
}

/**
 * ixgbe_ptp_hwtstamp_ioctl - control hardware time stamping
 * @adapter: pointer to adapter struct
 * @ifreq: ioctl data
 * @cmd: particular ioctl requested
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
 *
 * Since hardware always timestamps Path delay packets when timestamping V2
 * packets, regardless of the type specified in the register, only use V2
 * Event mode. This more accurately tells the user what the hardware is going
 * to do anyways.
 */
int ixgbe_ptp_hwtstamp_ioctl(struct ixgbe_adapter *adapter,
			     struct ifreq *ifr, int cmd)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct hwtstamp_config config;
	u32 tsync_tx_ctl = IXGBE_TSYNCTXCTL_ENABLED;
	u32 tsync_rx_ctl = IXGBE_TSYNCRXCTL_ENABLED;
	u32 tsync_rx_mtrl = PTP_EV_PORT << 16;
	bool is_l2 = false;
	u32 regval;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		tsync_tx_ctl = 0;
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		tsync_rx_ctl = 0;
		tsync_rx_mtrl = 0;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_mtrl |= IXGBE_RXMTRL_V1_SYNC_MSG;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_mtrl |= IXGBE_RXMTRL_V1_DELAY_REQ_MSG;
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
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_EVENT_V2;
		is_l2 = true;
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_ALL:
	default:
		/*
		 * register RXMTRL must be set in order to do V1 packets,
		 * therefore it is not possible to time stamp both V1 Sync and
		 * Delay_Req messages and hardware does not support
		 * timestamping all packets => return error
		 */
		config.rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	if (hw->mac.type == ixgbe_mac_82598EB) {
		if (tsync_rx_ctl | tsync_tx_ctl)
			return -ERANGE;
		return 0;
	}

	/* Store filter value for later use */
	adapter->rx_hwtstamp_filter = config.rx_filter;

	/* define ethertype filter for timestamping L2 packets */
	if (is_l2)
		IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_1588),
				(IXGBE_ETQF_FILTER_EN | /* enable filter */
				 IXGBE_ETQF_1588 | /* enable timestamping */
				 ETH_P_1588));     /* 1588 eth protocol type */
	else
		IXGBE_WRITE_REG(hw, IXGBE_ETQF(IXGBE_ETQF_FILTER_1588), 0);


	/* enable/disable TX */
	regval = IXGBE_READ_REG(hw, IXGBE_TSYNCTXCTL);
	regval &= ~IXGBE_TSYNCTXCTL_ENABLED;
	regval |= tsync_tx_ctl;
	IXGBE_WRITE_REG(hw, IXGBE_TSYNCTXCTL, regval);

	/* enable/disable RX */
	regval = IXGBE_READ_REG(hw, IXGBE_TSYNCRXCTL);
	regval &= ~(IXGBE_TSYNCRXCTL_ENABLED | IXGBE_TSYNCRXCTL_TYPE_MASK);
	regval |= tsync_rx_ctl;
	IXGBE_WRITE_REG(hw, IXGBE_TSYNCRXCTL, regval);

	/* define which PTP packets are time stamped */
	IXGBE_WRITE_REG(hw, IXGBE_RXMTRL, tsync_rx_mtrl);

	IXGBE_WRITE_FLUSH(hw);

	/* clear TX/RX time stamp registers, just to be sure */
	regval = IXGBE_READ_REG(hw, IXGBE_TXSTMPH);
	regval = IXGBE_READ_REG(hw, IXGBE_RXSTMPH);

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/**
 * ixgbe_ptp_start_cyclecounter - create the cycle counter from hw
 * @adapter: pointer to the adapter structure
 *
 * This function should be called to set the proper values for the TIMINCA
 * register and tell the cyclecounter structure what the tick rate of SYSTIME
 * is. It does not directly modify SYSTIME registers or the timecounter
 * structure. It should be called whenever a new TIMINCA value is necessary,
 * such as during initialization or when the link speed changes.
 */
void ixgbe_ptp_start_cyclecounter(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 incval = 0;
	u32 shift = 0;
	unsigned long flags;

	/**
	 * Scale the NIC cycle counter by a large factor so that
	 * relatively small corrections to the frequency can be added
	 * or subtracted. The drawbacks of a large factor include
	 * (a) the clock register overflows more quickly, (b) the cycle
	 * counter structure must be able to convert the systime value
	 * to nanoseconds using only a multiplier and a right-shift,
	 * and (c) the value must fit within the timinca register space
	 * => math based on internal DMA clock rate and available bits
	 *
	 * Note that when there is no link, internal DMA clock is same as when
	 * link speed is 10Gb. Set the registers correctly even when link is
	 * down to preserve the clock setting
	 */
	switch (adapter->link_speed) {
	case IXGBE_LINK_SPEED_100_FULL:
		incval = IXGBE_INCVAL_100;
		shift = IXGBE_INCVAL_SHIFT_100;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		incval = IXGBE_INCVAL_1GB;
		shift = IXGBE_INCVAL_SHIFT_1GB;
		break;
	case IXGBE_LINK_SPEED_10GB_FULL:
	default:
		incval = IXGBE_INCVAL_10GB;
		shift = IXGBE_INCVAL_SHIFT_10GB;
		break;
	}

	/**
	 * Modify the calculated values to fit within the correct
	 * number of bits specified by the hardware. The 82599 doesn't
	 * have the same space as the X540, so bitshift the calculated
	 * values to fit.
	 */
	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA, incval);
		break;
	case ixgbe_mac_82599EB:
		incval >>= IXGBE_INCVAL_SHIFT_82599;
		shift -= IXGBE_INCVAL_SHIFT_82599;
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA,
				(1 << IXGBE_INCPER_SHIFT_82599) |
				incval);
		break;
	default:
		/* other devices aren't supported */
		return;
	}

	/* update the base incval used to calculate frequency adjustment */
	ACCESS_ONCE(adapter->base_incval) = incval;
	smp_mb();

	/* need lock to prevent incorrect read while modifying cyclecounter */
	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	memset(&adapter->cc, 0, sizeof(adapter->cc));
	adapter->cc.read = ixgbe_ptp_read;
	adapter->cc.mask = CLOCKSOURCE_MASK(64);
	adapter->cc.shift = shift;
	adapter->cc.mult = 1;

	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);
}

/**
 * ixgbe_ptp_reset
 * @adapter: the ixgbe private board structure
 *
 * When the MAC resets, all timesync features are reset. This function should be
 * called to re-enable the PTP clock structure. It will re-init the timecounter
 * structure based on the kernel time as well as setup the cycle counter data.
 */
void ixgbe_ptp_reset(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	unsigned long flags;

	/* set SYSTIME registers to 0 just in case */
	IXGBE_WRITE_REG(hw, IXGBE_SYSTIML, 0x00000000);
	IXGBE_WRITE_REG(hw, IXGBE_SYSTIMH, 0x00000000);
	IXGBE_WRITE_FLUSH(hw);

	ixgbe_ptp_start_cyclecounter(adapter);

	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	/* reset the ns time counter */
	timecounter_init(&adapter->tc, &adapter->cc,
			 ktime_to_ns(ktime_get_real()));

	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	/*
	 * Now that the shift has been calculated and the systime
	 * registers reset, (re-)enable the Clock out feature
	 */
	ixgbe_ptp_setup_sdp(adapter);
}

/**
 * ixgbe_ptp_init
 * @adapter: the ixgbe private adapter structure
 *
 * This function performs the required steps for enabling ptp
 * support. If ptp support has already been loaded it simply calls the
 * cyclecounter init routine and exits.
 */
void ixgbe_ptp_init(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_X540:
		snprintf(adapter->ptp_caps.name, 16, "%s", netdev->name);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 250000000;
		adapter->ptp_caps.n_alarm = 0;
		adapter->ptp_caps.n_ext_ts = 0;
		adapter->ptp_caps.n_per_out = 0;
		adapter->ptp_caps.pps = 1;
		adapter->ptp_caps.adjfreq = ixgbe_ptp_adjfreq;
		adapter->ptp_caps.adjtime = ixgbe_ptp_adjtime;
		adapter->ptp_caps.gettime = ixgbe_ptp_gettime;
		adapter->ptp_caps.settime = ixgbe_ptp_settime;
		adapter->ptp_caps.enable = ixgbe_ptp_enable;
		break;
	case ixgbe_mac_82599EB:
		snprintf(adapter->ptp_caps.name, 16, "%s", netdev->name);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 250000000;
		adapter->ptp_caps.n_alarm = 0;
		adapter->ptp_caps.n_ext_ts = 0;
		adapter->ptp_caps.n_per_out = 0;
		adapter->ptp_caps.pps = 0;
		adapter->ptp_caps.adjfreq = ixgbe_ptp_adjfreq;
		adapter->ptp_caps.adjtime = ixgbe_ptp_adjtime;
		adapter->ptp_caps.gettime = ixgbe_ptp_gettime;
		adapter->ptp_caps.settime = ixgbe_ptp_settime;
		adapter->ptp_caps.enable = ixgbe_ptp_enable;
		break;
	default:
		adapter->ptp_clock = NULL;
		return;
	}

	/* initialize the ptp filter */
	if (ptp_filter_init(ptp_filter, ARRAY_SIZE(ptp_filter)))
		e_dev_warn("ptp_filter_init failed\n");

	spin_lock_init(&adapter->tmreg_lock);

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_caps,
						&adapter->pdev->dev);
	if (IS_ERR(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		e_dev_err("ptp_clock_register failed\n");
	} else
		e_dev_info("registered PHC device on %s\n", netdev->name);

	ixgbe_ptp_reset(adapter);

	/* set the flag that PTP has been enabled */
	adapter->flags2 |= IXGBE_FLAG2_PTP_ENABLED;

	return;
}

/**
 * ixgbe_ptp_stop - disable ptp device and stop the overflow check
 * @adapter: pointer to adapter struct
 *
 * this function stops the ptp support, and cancels the delayed work.
 */
void ixgbe_ptp_stop(struct ixgbe_adapter *adapter)
{
	/* stop the overflow check task */
	adapter->flags2 &= ~(IXGBE_FLAG2_PTP_ENABLED |
			     IXGBE_FLAG2_PTP_PPS_ENABLED);

	ixgbe_ptp_setup_sdp(adapter);

	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
		e_dev_info("removed PHC on %s\n",
			   adapter->netdev->name);
	}
}
