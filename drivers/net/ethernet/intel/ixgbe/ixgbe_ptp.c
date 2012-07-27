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

/**
 * ixgbe_ptp_read - read raw cycle counter (to be used by time counter)
 * @cc - the cyclecounter structure
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
 * @ptp - the ptp clock structure
 * @ppb - parts per billion adjustment from base
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
 * @ptp - the ptp clock structure
 * @delta - offset to adjust the cycle counter by
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
	return 0;
}

/**
 * ixgbe_ptp_gettime
 * @ptp - the ptp clock structure
 * @ts - timespec structure to hold the current time value
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
 * @ptp - the ptp clock structure
 * @ts - the timespec containing the new time for the cycle counter
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

	return 0;
}

/**
 * ixgbe_ptp_enable
 * @ptp - the ptp clock structure
 * @rq - the requested feature to change
 * @on - whether to enable or disable the feature
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
				adapter->flags2 &=
					~IXGBE_FLAG2_PTP_PPS_ENABLED;
			return 0;
		default:
			break;
		}
	}

	return -ENOTSUPP;
}

/**
 * ixgbe_ptp_check_pps_event
 * @adapter - the private adapter structure
 * @eicr - the interrupt cause register value
 *
 * This function is called by the interrupt routine when checking for
 * interrupts. It will check and handle a pps event.
 */
void ixgbe_ptp_check_pps_event(struct ixgbe_adapter *adapter, u32 eicr)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ptp_clock_event event;

	event.type = PTP_CLOCK_PPS;

	/* Make sure ptp clock is valid, and PPS event enabled */
	if (!adapter->ptp_clock ||
	    !(adapter->flags2 & IXGBE_FLAG2_PTP_PPS_ENABLED))
		return;

	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		if (eicr & IXGBE_EICR_TIMESYNC)
			ptp_clock_event(adapter->ptp_clock, &event);
		break;
	default:
		break;
	}
}

/**
 * ixgbe_ptp_enable_sdp
 * @hw - the hardware private structure
 * @shift - the clock shift for calculating nanoseconds
 *
 * this function enables the clock out feature on the sdp0 for the
 * X540 device. It will create a 1second periodic output that can be
 * used as the PPS (via an interrupt).
 *
 * It calculates when the systime will be on an exact second, and then
 * aligns the start of the PPS signal to that value. The shift is
 * necessary because it can change based on the link speed.
 */
static void ixgbe_ptp_enable_sdp(struct ixgbe_hw *hw, int shift)
{
	u32 esdp, tsauxc, clktiml, clktimh, trgttiml, trgttimh;
	u64 clock_edge = 0;
	u32 rem;

	switch (hw->mac.type) {
	case ixgbe_mac_X540:
		esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);

		/*
		 * enable the SDP0 pin as output, and connected to the native
		 * function for Timesync (ClockOut)
		 */
		esdp |= (IXGBE_ESDP_SDP0_DIR |
			 IXGBE_ESDP_SDP0_NATIVE);

		/*
		 * enable the Clock Out feature on SDP0, and allow interrupts
		 * to occur when the pin changes
		 */
		tsauxc = (IXGBE_TSAUXC_EN_CLK |
			  IXGBE_TSAUXC_SYNCLK |
			  IXGBE_TSAUXC_SDP0_INT);

		/* clock period (or pulse length) */
		clktiml = (u32)(NSECS_PER_SEC << shift);
		clktimh = (u32)((NSECS_PER_SEC << shift) >> 32);

		clock_edge |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIML);
		clock_edge |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIMH) << 32;

		/*
		 * account for the fact that we can't do u64 division
		 * with remainder, by converting the clock values into
		 * nanoseconds first
		 */
		clock_edge >>= shift;
		div_u64_rem(clock_edge, NSECS_PER_SEC, &rem);
		clock_edge += (NSECS_PER_SEC - rem);
		clock_edge <<= shift;

		/* specify the initial clock start time */
		trgttiml = (u32)clock_edge;
		trgttimh = (u32)(clock_edge >> 32);

		IXGBE_WRITE_REG(hw, IXGBE_CLKTIML, clktiml);
		IXGBE_WRITE_REG(hw, IXGBE_CLKTIMH, clktimh);
		IXGBE_WRITE_REG(hw, IXGBE_TRGTTIML0, trgttiml);
		IXGBE_WRITE_REG(hw, IXGBE_TRGTTIMH0, trgttimh);

		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
		IXGBE_WRITE_REG(hw, IXGBE_TSAUXC, tsauxc);

		IXGBE_WRITE_REG(hw, IXGBE_EIMS, IXGBE_EICR_TIMESYNC);
		break;
	default:
		break;
	}
}

/**
 * ixgbe_ptp_disable_sdp
 * @hw - the private hardware structure
 *
 * this function disables the auxiliary SDP clock out feature
 */
static void ixgbe_ptp_disable_sdp(struct ixgbe_hw *hw)
{
	IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_EICR_TIMESYNC);
	IXGBE_WRITE_REG(hw, IXGBE_TSAUXC, 0);
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

	if ((adapter->flags2 & IXGBE_FLAG2_OVERFLOW_CHECK_ENABLED) &&
	    (elapsed_jiffies >= IXGBE_OVERFLOW_PERIOD)) {
		ixgbe_ptp_gettime(&adapter->ptp_caps, &ts);
		adapter->last_overflow_check = jiffies;
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
 * @skb: particular skb to send timestamp with
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the shhwtstamps structure which
 * is passed up the network stack
 */
void ixgbe_ptp_rx_hwtstamp(struct ixgbe_q_vector *q_vector,
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

	tsyncrxctl = IXGBE_READ_REG(hw, IXGBE_TSYNCRXCTL);
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_RXSTMPL);
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_RXSTMPH) << 32;

	/*
	 * If this bit is set, then the RX registers contain the time stamp. No
	 * other packet will be time stamped until we read these registers, so
	 * read the registers to make them available again. Because only one
	 * packet can be time stamped at a time, we know that the register
	 * values must belong to this one here and therefore we don't need to
	 * compare any of the additional attributes stored for it.
	 *
	 * If nothing went wrong, then it should have a skb_shared_tx that we
	 * can turn into a skb_shared_hwtstamps.
	 */
	if (!(tsyncrxctl & IXGBE_TSYNCRXCTL_VALID))
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
 */
int ixgbe_ptp_hwtstamp_ioctl(struct ixgbe_adapter *adapter,
			     struct ifreq *ifr, int cmd)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct hwtstamp_config config;
	u32 tsync_tx_ctl = IXGBE_TSYNCTXCTL_ENABLED;
	u32 tsync_rx_ctl = IXGBE_TSYNCRXCTL_ENABLED;
	u32 tsync_rx_mtrl = 0;
	bool is_l4 = false;
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
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_mtrl = IXGBE_RXMTRL_V1_SYNC_MSG;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_mtrl = IXGBE_RXMTRL_V1_DELAY_REQ_MSG;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L2_L4_V2;
		tsync_rx_mtrl = IXGBE_RXMTRL_V2_SYNC_MSG;
		is_l2 = true;
		is_l4 = true;
		config.rx_filter = HWTSTAMP_FILTER_SOME;
		break;
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L2_L4_V2;
		tsync_rx_mtrl = IXGBE_RXMTRL_V2_DELAY_REQ_MSG;
		is_l2 = true;
		is_l4 = true;
		config.rx_filter = HWTSTAMP_FILTER_SOME;
		break;
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_EVENT_V2;
		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		is_l2 = true;
		is_l4 = true;
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_ALL:
	default:
		/*
		 * register RXMTRL must be set, therefore it is not
		 * possible to time stamp both V1 Sync and Delay_Req messages
		 * and hardware does not support timestamping all packets
		 * => return error
		 */
		return -ERANGE;
	}

	if (hw->mac.type == ixgbe_mac_82598EB) {
		if (tsync_rx_ctl | tsync_tx_ctl)
			return -ERANGE;
		return 0;
	}

	/* define ethertype filter for timestamped packets */
	if (is_l2)
		IXGBE_WRITE_REG(hw, IXGBE_ETQF(3),
				(IXGBE_ETQF_FILTER_EN | /* enable filter */
				 IXGBE_ETQF_1588 | /* enable timestamping */
				 ETH_P_1588));     /* 1588 eth protocol type */
	else
		IXGBE_WRITE_REG(hw, IXGBE_ETQF(3), 0);

#define PTP_PORT 319
	/* L4 Queue Filter[3]: filter by destination port and protocol */
	if (is_l4) {
		u32 ftqf = (IXGBE_FTQF_PROTOCOL_UDP /* UDP */
			    | IXGBE_FTQF_POOL_MASK_EN /* Pool not compared */
			    | IXGBE_FTQF_QUEUE_ENABLE);

		ftqf |= ((IXGBE_FTQF_PROTOCOL_COMP_MASK /* protocol check */
			  & IXGBE_FTQF_DEST_PORT_MASK /* dest check */
			  & IXGBE_FTQF_SOURCE_PORT_MASK) /* source check */
			 << IXGBE_FTQF_5TUPLE_MASK_SHIFT);

		IXGBE_WRITE_REG(hw, IXGBE_L34T_IMIR(3),
				(3 << IXGBE_IMIR_RX_QUEUE_SHIFT_82599 |
				 IXGBE_IMIR_SIZE_BP_82599));

		/* enable port check */
		IXGBE_WRITE_REG(hw, IXGBE_SDPQF(3),
				(htons(PTP_PORT) |
				 htons(PTP_PORT) << 16));

		IXGBE_WRITE_REG(hw, IXGBE_FTQF(3), ftqf);

		tsync_rx_mtrl |= PTP_PORT << 16;
	} else {
		IXGBE_WRITE_REG(hw, IXGBE_FTQF(3), 0);
	}

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
 * @adapter - pointer to the adapter structure
 *
 * this function initializes the timecounter and cyclecounter
 * structures for use in generated a ns counter from the arbitrary
 * fixed point cycles registers in the hardware.
 *
 * A change in link speed impacts the frequency of the DMA clock on
 * the device, which is used to generate the cycle counter
 * registers. Therefor this function is called whenever the link speed
 * changes.
 *
 * This function also turns on the SDP pin for clock out feature (X540
 * only), because this is where the shift is first calculated.
 */
void ixgbe_ptp_start_cyclecounter(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 incval = 0;
	u32 timinca = 0;
	u32 shift = 0;
	u32 cycle_speed;
	unsigned long flags;

	/**
	 * Determine what speed we need to set the cyclecounter
	 * for. It should be different for 100Mb, 1Gb, and 10Gb. Treat
	 * unknown speeds as 10Gb. (Hence why we can't just copy the
	 * link_speed.
	 */
	switch (adapter->link_speed) {
	case IXGBE_LINK_SPEED_100_FULL:
	case IXGBE_LINK_SPEED_1GB_FULL:
	case IXGBE_LINK_SPEED_10GB_FULL:
		cycle_speed = adapter->link_speed;
		break;
	default:
		/* cycle speed should be 10Gb when there is no link */
		cycle_speed = IXGBE_LINK_SPEED_10GB_FULL;
		break;
	}

	/*
	 * grab the current TIMINCA value from the register so that it can be
	 * double checked. If the register value has been cleared, it must be
	 * reset to the correct value for generating a cyclecounter. If
	 * TIMINCA is zero, the SYSTIME registers do not increment at all.
	 */
	timinca = IXGBE_READ_REG(hw, IXGBE_TIMINCA);

	/* Bail if the cycle speed didn't change and TIMINCA is non-zero */
	if (adapter->cycle_speed == cycle_speed && timinca)
		return;

	/* disable the SDP clock out */
	ixgbe_ptp_disable_sdp(hw);

	/**
	 * Scale the NIC cycle counter by a large factor so that
	 * relatively small corrections to the frequency can be added
	 * or subtracted. The drawbacks of a large factor include
	 * (a) the clock register overflows more quickly, (b) the cycle
	 * counter structure must be able to convert the systime value
	 * to nanoseconds using only a multiplier and a right-shift,
	 * and (c) the value must fit within the timinca register space
	 * => math based on internal DMA clock rate and available bits
	 */
	switch (cycle_speed) {
	case IXGBE_LINK_SPEED_100_FULL:
		incval = IXGBE_INCVAL_100;
		shift = IXGBE_INCVAL_SHIFT_100;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		incval = IXGBE_INCVAL_1GB;
		shift = IXGBE_INCVAL_SHIFT_1GB;
		break;
	case IXGBE_LINK_SPEED_10GB_FULL:
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

	/* reset the system time registers */
	IXGBE_WRITE_REG(hw, IXGBE_SYSTIML, 0x00000000);
	IXGBE_WRITE_REG(hw, IXGBE_SYSTIMH, 0x00000000);
	IXGBE_WRITE_FLUSH(hw);

	/* now that the shift has been calculated and the systime
	 * registers reset, (re-)enable the Clock out feature*/
	ixgbe_ptp_enable_sdp(hw, shift);

	/* store the new cycle speed */
	adapter->cycle_speed = cycle_speed;

	ACCESS_ONCE(adapter->base_incval) = incval;
	smp_mb();

	/* grab the ptp lock */
	spin_lock_irqsave(&adapter->tmreg_lock, flags);

	memset(&adapter->cc, 0, sizeof(adapter->cc));
	adapter->cc.read = ixgbe_ptp_read;
	adapter->cc.mask = CLOCKSOURCE_MASK(64);
	adapter->cc.shift = shift;
	adapter->cc.mult = 1;

	/* reset the ns time counter */
	timecounter_init(&adapter->tc, &adapter->cc,
			 ktime_to_ns(ktime_get_real()));

	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);
}

/**
 * ixgbe_ptp_init
 * @adapter - the ixgbe private adapter structure
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
		snprintf(adapter->ptp_caps.name, 16, "%pm", netdev->dev_addr);
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
		snprintf(adapter->ptp_caps.name, 16, "%pm", netdev->dev_addr);
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

	spin_lock_init(&adapter->tmreg_lock);

	ixgbe_ptp_start_cyclecounter(adapter);

	/* (Re)start the overflow check */
	adapter->flags2 |= IXGBE_FLAG2_OVERFLOW_CHECK_ENABLED;

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_caps);
	if (IS_ERR(adapter->ptp_clock)) {
		adapter->ptp_clock = NULL;
		e_dev_err("ptp_clock_register failed\n");
	} else
		e_dev_info("registered PHC device on %s\n", netdev->name);

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
	ixgbe_ptp_disable_sdp(&adapter->hw);

	/* stop the overflow check task */
	adapter->flags2 &= ~IXGBE_FLAG2_OVERFLOW_CHECK_ENABLED;

	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
		e_dev_info("removed PHC on %s\n",
			   adapter->netdev->name);
	}
}
