/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2016 Intel Corporation.

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
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/
#include "ixgbe.h"
#include <linux/ptp_classify.h>
#include <linux/clocksource.h>

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

#define IXGBE_OVERFLOW_PERIOD    (HZ * 30)
#define IXGBE_PTP_TX_TIMEOUT     (HZ * 15)

/* half of a one second clock period, for use with PPS signal. We have to use
 * this instead of something pre-defined like IXGBE_PTP_PPS_HALF_SECOND, in
 * order to force at least 64bits of precision for shifting
 */
#define IXGBE_PTP_PPS_HALF_SECOND 500000000ULL

/* In contrast, the X550 controller has two registers, SYSTIMEH and SYSTIMEL
 * which contain measurements of seconds and nanoseconds respectively. This
 * matches the standard linux representation of time in the kernel. In addition,
 * the X550 also has a SYSTIMER register which represents residue, or
 * subnanosecond overflow adjustments. To control clock adjustment, the TIMINCA
 * register is used, but it is unlike the X540 and 82599 devices. TIMINCA
 * represents units of 2^-32 nanoseconds, and uses 31 bits for this, with the
 * high bit representing whether the adjustent is positive or negative. Every
 * clock cycle, the X550 will add 12.5 ns + TIMINCA which can result in a range
 * of 12 to 13 nanoseconds adjustment. Unlike the 82599 and X540 devices, the
 * X550's clock for purposes of SYSTIME generation is constant and not dependent
 * on the link speed.
 *
 *           SYSTIMEH           SYSTIMEL        SYSTIMER
 *       +--------------+  +--------------+  +-------------+
 * X550  |      32      |  |      32      |  |     32      |
 *       *--------------+  +--------------+  +-------------+
 *       \____seconds___/   \_nanoseconds_/  \__2^-32 ns__/
 *
 * This results in a full 96 bits to represent the clock, with 32 bits for
 * seconds, 32 bits for nanoseconds (largest value is 0d999999999 or just under
 * 1 second) and an additional 32 bits to measure sub nanosecond adjustments for
 * underflow of adjustments.
 *
 * The 32 bits of seconds for the X550 overflows every
 *   2^32 / ( 365.25 * 24 * 60 * 60 ) = ~136 years.
 *
 * In order to adjust the clock frequency for the X550, the TIMINCA register is
 * provided. This register represents a + or minus nearly 0.5 ns adjustment to
 * the base frequency. It is measured in 2^-32 ns units, with the high bit being
 * the sign bit. This register enables software to calculate frequency
 * adjustments and apply them directly to the clock rate.
 *
 * The math for converting ppb into TIMINCA values is fairly straightforward.
 *   TIMINCA value = ( Base_Frequency * ppb ) / 1000000000ULL
 *
 * This assumes that ppb is never high enough to create a value bigger than
 * TIMINCA's 31 bits can store. This is ensured by the stack. Calculating this
 * value is also simple.
 *   Max ppb = ( Max Adjustment / Base Frequency ) / 1000000000ULL
 *
 * For the X550, the Max adjustment is +/- 0.5 ns, and the base frequency is
 * 12.5 nanoseconds. This means that the Max ppb is 39999999
 *   Note: We subtract one in order to ensure no overflow, because the TIMINCA
 *         register can only hold slightly under 0.5 nanoseconds.
 *
 * Because TIMINCA is measured in 2^-32 ns units, we have to convert 12.5 ns
 * into 2^-32 units, which is
 *
 *  12.5 * 2^32 = C80000000
 *
 * Some revisions of hardware have a faster base frequency than the registers
 * were defined for. To fix this, we use a timecounter structure with the
 * proper mult and shift to convert the cycles into nanoseconds of time.
 */
#define IXGBE_X550_BASE_PERIOD 0xC80000000ULL
#define INCVALUE_MASK	0x7FFFFFFF
#define ISGN		0x80000000
#define MAX_TIMADJ	0x7FFFFFFF

/**
 * ixgbe_ptp_setup_sdp_x540
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
static void ixgbe_ptp_setup_sdp_x540(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int shift = adapter->hw_cc.shift;
	u32 esdp, tsauxc, clktiml, clktimh, trgttiml, trgttimh, rem;
	u64 ns = 0, clock_edge = 0;

	/* disable the pin first */
	IXGBE_WRITE_REG(hw, IXGBE_TSAUXC, 0x0);
	IXGBE_WRITE_FLUSH(hw);

	if (!(adapter->flags2 & IXGBE_FLAG2_PTP_PPS_ENABLED))
		return;

	esdp = IXGBE_READ_REG(hw, IXGBE_ESDP);

	/* enable the SDP0 pin as output, and connected to the
	 * native function for Timesync (ClockOut)
	 */
	esdp |= IXGBE_ESDP_SDP0_DIR |
		IXGBE_ESDP_SDP0_NATIVE;

	/* enable the Clock Out feature on SDP0, and allow
	 * interrupts to occur when the pin changes
	 */
	tsauxc = IXGBE_TSAUXC_EN_CLK |
		 IXGBE_TSAUXC_SYNCLK |
		 IXGBE_TSAUXC_SDP0_INT;

	/* clock period (or pulse length) */
	clktiml = (u32)(IXGBE_PTP_PPS_HALF_SECOND << shift);
	clktimh = (u32)((IXGBE_PTP_PPS_HALF_SECOND << shift) >> 32);

	/* Account for the cyclecounter wrap-around value by
	 * using the converted ns value of the current time to
	 * check for when the next aligned second would occur.
	 */
	clock_edge |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIML);
	clock_edge |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIMH) << 32;
	ns = timecounter_cyc2time(&adapter->hw_tc, clock_edge);

	div_u64_rem(ns, IXGBE_PTP_PPS_HALF_SECOND, &rem);
	clock_edge += ((IXGBE_PTP_PPS_HALF_SECOND - (u64)rem) << shift);

	/* specify the initial clock start time */
	trgttiml = (u32)clock_edge;
	trgttimh = (u32)(clock_edge >> 32);

	IXGBE_WRITE_REG(hw, IXGBE_CLKTIML, clktiml);
	IXGBE_WRITE_REG(hw, IXGBE_CLKTIMH, clktimh);
	IXGBE_WRITE_REG(hw, IXGBE_TRGTTIML0, trgttiml);
	IXGBE_WRITE_REG(hw, IXGBE_TRGTTIMH0, trgttimh);

	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp);
	IXGBE_WRITE_REG(hw, IXGBE_TSAUXC, tsauxc);

	IXGBE_WRITE_FLUSH(hw);
}

/**
 * ixgbe_ptp_read_X550 - read cycle counter value
 * @hw_cc: cyclecounter structure
 *
 * This function reads SYSTIME registers. It is called by the cyclecounter
 * structure to convert from internal representation into nanoseconds. We need
 * this for X550 since some skews do not have expected clock frequency and
 * result of SYSTIME is 32bits of "billions of cycles" and 32 bits of
 * "cycles", rather than seconds and nanoseconds.
 */
static u64 ixgbe_ptp_read_X550(const struct cyclecounter *hw_cc)
{
	struct ixgbe_adapter *adapter =
			container_of(hw_cc, struct ixgbe_adapter, hw_cc);
	struct ixgbe_hw *hw = &adapter->hw;
	struct timespec64 ts;

	/* storage is 32 bits of 'billions of cycles' and 32 bits of 'cycles'.
	 * Some revisions of hardware run at a higher frequency and so the
	 * cycles are not guaranteed to be nanoseconds. The timespec64 created
	 * here is used for its math/conversions but does not necessarily
	 * represent nominal time.
	 *
	 * It should be noted that this cyclecounter will overflow at a
	 * non-bitmask field since we have to convert our billions of cycles
	 * into an actual cycles count. This results in some possible weird
	 * situations at high cycle counter stamps. However given that 32 bits
	 * of "seconds" is ~138 years this isn't a problem. Even at the
	 * increased frequency of some revisions, this is still ~103 years.
	 * Since the SYSTIME values start at 0 and we never write them, it is
	 * highly unlikely for the cyclecounter to overflow in practice.
	 */
	IXGBE_READ_REG(hw, IXGBE_SYSTIMR);
	ts.tv_nsec = IXGBE_READ_REG(hw, IXGBE_SYSTIML);
	ts.tv_sec = IXGBE_READ_REG(hw, IXGBE_SYSTIMH);

	return (u64)timespec64_to_ns(&ts);
}

/**
 * ixgbe_ptp_read_82599 - read raw cycle counter (to be used by time counter)
 * @cc: the cyclecounter structure
 *
 * this function reads the cyclecounter registers and is called by the
 * cyclecounter structure used to construct a ns counter from the
 * arbitrary fixed point registers
 */
static u64 ixgbe_ptp_read_82599(const struct cyclecounter *cc)
{
	struct ixgbe_adapter *adapter =
		container_of(cc, struct ixgbe_adapter, hw_cc);
	struct ixgbe_hw *hw = &adapter->hw;
	u64 stamp = 0;

	stamp |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIML);
	stamp |= (u64)IXGBE_READ_REG(hw, IXGBE_SYSTIMH) << 32;

	return stamp;
}

/**
 * ixgbe_ptp_convert_to_hwtstamp - convert register value to hw timestamp
 * @adapter: private adapter structure
 * @hwtstamp: stack timestamp structure
 * @systim: unsigned 64bit system time value
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
static void ixgbe_ptp_convert_to_hwtstamp(struct ixgbe_adapter *adapter,
					  struct skb_shared_hwtstamps *hwtstamp,
					  u64 timestamp)
{
	unsigned long flags;
	struct timespec64 systime;
	u64 ns;

	memset(hwtstamp, 0, sizeof(*hwtstamp));

	switch (adapter->hw.mac.type) {
	/* X550 and later hardware supposedly represent time using a seconds
	 * and nanoseconds counter, instead of raw 64bits nanoseconds. We need
	 * to convert the timestamp into cycles before it can be fed to the
	 * cyclecounter. We need an actual cyclecounter because some revisions
	 * of hardware run at a higher frequency and thus the counter does
	 * not represent seconds/nanoseconds. Instead it can be thought of as
	 * cycles and billions of cycles.
	 */
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_x550em_a:
		/* Upper 32 bits represent billions of cycles, lower 32 bits
		 * represent cycles. However, we use timespec64_to_ns for the
		 * correct math even though the units haven't been corrected
		 * yet.
		 */
		systime.tv_sec = timestamp >> 32;
		systime.tv_nsec = timestamp & 0xFFFFFFFF;

		timestamp = timespec64_to_ns(&systime);
		break;
	default:
		break;
	}

	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	ns = timecounter_cyc2time(&adapter->hw_tc, timestamp);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	hwtstamp->hwtstamp = ns_to_ktime(ns);
}

/**
 * ixgbe_ptp_adjfreq_82599
 * @ptp: the ptp clock structure
 * @ppb: parts per billion adjustment from base
 *
 * adjust the frequency of the ptp cycle counter by the
 * indicated ppb from the base frequency.
 */
static int ixgbe_ptp_adjfreq_82599(struct ptp_clock_info *ptp, s32 ppb)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);
	struct ixgbe_hw *hw = &adapter->hw;
	u64 freq, incval;
	u32 diff;
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
		if (incval > 0xFFFFFFFFULL)
			e_dev_warn("PTP ppb adjusted SYSTIME rate overflowed!\n");
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA, (u32)incval);
		break;
	case ixgbe_mac_82599EB:
		if (incval > 0x00FFFFFFULL)
			e_dev_warn("PTP ppb adjusted SYSTIME rate overflowed!\n");
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA,
				BIT(IXGBE_INCPER_SHIFT_82599) |
				((u32)incval & 0x00FFFFFFUL));
		break;
	default:
		break;
	}

	return 0;
}

/**
 * ixgbe_ptp_adjfreq_X550
 * @ptp: the ptp clock structure
 * @ppb: parts per billion adjustment from base
 *
 * adjust the frequency of the SYSTIME registers by the indicated ppb from base
 * frequency
 */
static int ixgbe_ptp_adjfreq_X550(struct ptp_clock_info *ptp, s32 ppb)
{
	struct ixgbe_adapter *adapter =
			container_of(ptp, struct ixgbe_adapter, ptp_caps);
	struct ixgbe_hw *hw = &adapter->hw;
	int neg_adj = 0;
	u64 rate = IXGBE_X550_BASE_PERIOD;
	u32 inca;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	rate *= ppb;
	rate = div_u64(rate, 1000000000ULL);

	/* warn if rate is too large */
	if (rate >= INCVALUE_MASK)
		e_dev_warn("PTP ppb adjusted SYSTIME rate overflowed!\n");

	inca = rate & INCVALUE_MASK;
	if (neg_adj)
		inca |= ISGN;

	IXGBE_WRITE_REG(hw, IXGBE_TIMINCA, inca);

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

	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	timecounter_adjtime(&adapter->hw_tc, delta);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	if (adapter->ptp_setup_sdp)
		adapter->ptp_setup_sdp(adapter);

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
static int ixgbe_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);
	unsigned long flags;
	u64 ns;

	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	ns = timecounter_read(&adapter->hw_tc);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	*ts = ns_to_timespec64(ns);

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
			     const struct timespec64 *ts)
{
	struct ixgbe_adapter *adapter =
		container_of(ptp, struct ixgbe_adapter, ptp_caps);
	unsigned long flags;
	u64 ns = timespec64_to_ns(ts);

	/* reset the timecounter */
	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	timecounter_init(&adapter->hw_tc, &adapter->hw_cc, ns);
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	if (adapter->ptp_setup_sdp)
		adapter->ptp_setup_sdp(adapter);
	return 0;
}

/**
 * ixgbe_ptp_feature_enable
 * @ptp: the ptp clock structure
 * @rq: the requested feature to change
 * @on: whether to enable or disable the feature
 *
 * enable (or disable) ancillary features of the phc subsystem.
 * our driver only supports the PPS feature on the X540
 */
static int ixgbe_ptp_feature_enable(struct ptp_clock_info *ptp,
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
	if (rq->type != PTP_CLK_REQ_PPS || !adapter->ptp_setup_sdp)
		return -ENOTSUPP;

	if (on)
		adapter->flags2 |= IXGBE_FLAG2_PTP_PPS_ENABLED;
	else
		adapter->flags2 &= ~IXGBE_FLAG2_PTP_PPS_ENABLED;

	adapter->ptp_setup_sdp(adapter);
	return 0;
}

/**
 * ixgbe_ptp_check_pps_event
 * @adapter: the private adapter structure
 *
 * This function is called by the interrupt routine when checking for
 * interrupts. It will check and handle a pps event.
 */
void ixgbe_ptp_check_pps_event(struct ixgbe_adapter *adapter)
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
 * ixgbe_ptp_overflow_check - watchdog task to detect SYSTIME overflow
 * @adapter: private adapter struct
 *
 * this watchdog task periodically reads the timecounter
 * in order to prevent missing when the system time registers wrap
 * around. This needs to be run approximately twice a minute.
 */
void ixgbe_ptp_overflow_check(struct ixgbe_adapter *adapter)
{
	bool timeout = time_is_before_jiffies(adapter->last_overflow_check +
					     IXGBE_OVERFLOW_PERIOD);
	struct timespec64 ts;

	if (timeout) {
		ixgbe_ptp_gettime(&adapter->ptp_caps, &ts);
		adapter->last_overflow_check = jiffies;
	}
}

/**
 * ixgbe_ptp_rx_hang - detect error case when Rx timestamp registers latched
 * @adapter: private network adapter structure
 *
 * this watchdog task is scheduled to detect error case where hardware has
 * dropped an Rx packet that was timestamped when the ring is full. The
 * particular error is rare but leaves the device in a state unable to timestamp
 * any future packets.
 */
void ixgbe_ptp_rx_hang(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 tsyncrxctl = IXGBE_READ_REG(hw, IXGBE_TSYNCRXCTL);
	struct ixgbe_ring *rx_ring;
	unsigned long rx_event;
	int n;

	/* if we don't have a valid timestamp in the registers, just update the
	 * timeout counter and exit
	 */
	if (!(tsyncrxctl & IXGBE_TSYNCRXCTL_VALID)) {
		adapter->last_rx_ptp_check = jiffies;
		return;
	}

	/* determine the most recent watchdog or rx_timestamp event */
	rx_event = adapter->last_rx_ptp_check;
	for (n = 0; n < adapter->num_rx_queues; n++) {
		rx_ring = adapter->rx_ring[n];
		if (time_after(rx_ring->last_rx_timestamp, rx_event))
			rx_event = rx_ring->last_rx_timestamp;
	}

	/* only need to read the high RXSTMP register to clear the lock */
	if (time_is_before_jiffies(rx_event + 5 * HZ)) {
		IXGBE_READ_REG(hw, IXGBE_RXSTMPH);
		adapter->last_rx_ptp_check = jiffies;

		adapter->rx_hwtstamp_cleared++;
		e_warn(drv, "clearing RX Timestamp hang\n");
	}
}

/**
 * ixgbe_ptp_clear_tx_timestamp - utility function to clear Tx timestamp state
 * @adapter: the private adapter structure
 *
 * This function should be called whenever the state related to a Tx timestamp
 * needs to be cleared. This helps ensure that all related bits are reset for
 * the next Tx timestamp event.
 */
static void ixgbe_ptp_clear_tx_timestamp(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;

	IXGBE_READ_REG(hw, IXGBE_TXSTMPH);
	if (adapter->ptp_tx_skb) {
		dev_kfree_skb_any(adapter->ptp_tx_skb);
		adapter->ptp_tx_skb = NULL;
	}
	clear_bit_unlock(__IXGBE_PTP_TX_IN_PROGRESS, &adapter->state);
}

/**
 * ixgbe_ptp_tx_hwtstamp - utility function which checks for TX time stamp
 * @adapter: the private adapter struct
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the shhwtstamps structure which
 * is passed up the network stack
 */
static void ixgbe_ptp_tx_hwtstamp(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct skb_shared_hwtstamps shhwtstamps;
	u64 regval = 0;

	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_TXSTMPL);
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_TXSTMPH) << 32;

	ixgbe_ptp_convert_to_hwtstamp(adapter, &shhwtstamps, regval);
	skb_tstamp_tx(adapter->ptp_tx_skb, &shhwtstamps);

	ixgbe_ptp_clear_tx_timestamp(adapter);
}

/**
 * ixgbe_ptp_tx_hwtstamp_work
 * @work: pointer to the work struct
 *
 * This work item polls TSYNCTXCTL valid bit to determine when a Tx hardware
 * timestamp has been taken for the current skb. It is necessary, because the
 * descriptor's "done" bit does not correlate with the timestamp event.
 */
static void ixgbe_ptp_tx_hwtstamp_work(struct work_struct *work)
{
	struct ixgbe_adapter *adapter = container_of(work, struct ixgbe_adapter,
						     ptp_tx_work);
	struct ixgbe_hw *hw = &adapter->hw;
	bool timeout = time_is_before_jiffies(adapter->ptp_tx_start +
					      IXGBE_PTP_TX_TIMEOUT);
	u32 tsynctxctl;

	/* we have to have a valid skb to poll for a timestamp */
	if (!adapter->ptp_tx_skb) {
		ixgbe_ptp_clear_tx_timestamp(adapter);
		return;
	}

	/* stop polling once we have a valid timestamp */
	tsynctxctl = IXGBE_READ_REG(hw, IXGBE_TSYNCTXCTL);
	if (tsynctxctl & IXGBE_TSYNCTXCTL_VALID) {
		ixgbe_ptp_tx_hwtstamp(adapter);
		return;
	}

	if (timeout) {
		ixgbe_ptp_clear_tx_timestamp(adapter);
		adapter->tx_hwtstamp_timeouts++;
		e_warn(drv, "clearing Tx Timestamp hang\n");
	} else {
		/* reschedule to keep checking if it's not available yet */
		schedule_work(&adapter->ptp_tx_work);
	}
}

/**
 * ixgbe_ptp_rx_pktstamp - utility function to get RX time stamp from buffer
 * @q_vector: structure containing interrupt and ring information
 * @skb: the packet
 *
 * This function will be called by the Rx routine of the timestamp for this
 * packet is stored in the buffer. The value is stored in little endian format
 * starting at the end of the packet data.
 */
void ixgbe_ptp_rx_pktstamp(struct ixgbe_q_vector *q_vector,
			   struct sk_buff *skb)
{
	__le64 regval;

	/* copy the bits out of the skb, and then trim the skb length */
	skb_copy_bits(skb, skb->len - IXGBE_TS_HDR_LEN, &regval,
		      IXGBE_TS_HDR_LEN);
	__pskb_trim(skb, skb->len - IXGBE_TS_HDR_LEN);

	/* The timestamp is recorded in little endian format, and is stored at
	 * the end of the packet.
	 *
	 * DWORD: N              N + 1      N + 2
	 * Field: End of Packet  SYSTIMH    SYSTIML
	 */
	ixgbe_ptp_convert_to_hwtstamp(q_vector->adapter, skb_hwtstamps(skb),
				      le64_to_cpu(regval));
}

/**
 * ixgbe_ptp_rx_rgtstamp - utility function which checks for RX time stamp
 * @q_vector: structure containing interrupt and ring information
 * @skb: particular skb to send timestamp with
 *
 * if the timestamp is valid, we convert it into the timecounter ns
 * value, then store that result into the shhwtstamps structure which
 * is passed up the network stack
 */
void ixgbe_ptp_rx_rgtstamp(struct ixgbe_q_vector *q_vector,
			   struct sk_buff *skb)
{
	struct ixgbe_adapter *adapter;
	struct ixgbe_hw *hw;
	u64 regval = 0;
	u32 tsyncrxctl;

	/* we cannot process timestamps on a ring without a q_vector */
	if (!q_vector || !q_vector->adapter)
		return;

	adapter = q_vector->adapter;
	hw = &adapter->hw;

	/* Read the tsyncrxctl register afterwards in order to prevent taking an
	 * I/O hit on every packet.
	 */

	tsyncrxctl = IXGBE_READ_REG(hw, IXGBE_TSYNCRXCTL);
	if (!(tsyncrxctl & IXGBE_TSYNCRXCTL_VALID))
		return;

	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_RXSTMPL);
	regval |= (u64)IXGBE_READ_REG(hw, IXGBE_RXSTMPH) << 32;

	ixgbe_ptp_convert_to_hwtstamp(adapter, skb_hwtstamps(skb), regval);
}

int ixgbe_ptp_get_ts_config(struct ixgbe_adapter *adapter, struct ifreq *ifr)
{
	struct hwtstamp_config *config = &adapter->tstamp_config;

	return copy_to_user(ifr->ifr_data, config,
			    sizeof(*config)) ? -EFAULT : 0;
}

/**
 * ixgbe_ptp_set_timestamp_mode - setup the hardware for the requested mode
 * @adapter: the private ixgbe adapter structure
 * @config: the hwtstamp configuration requested
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
 *
 * Note: this may modify the hwtstamp configuration towards a more general
 * mode, if required to support the specifically requested mode.
 */
static int ixgbe_ptp_set_timestamp_mode(struct ixgbe_adapter *adapter,
				 struct hwtstamp_config *config)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 tsync_tx_ctl = IXGBE_TSYNCTXCTL_ENABLED;
	u32 tsync_rx_ctl = IXGBE_TSYNCRXCTL_ENABLED;
	u32 tsync_rx_mtrl = PTP_EV_PORT << 16;
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
		tsync_rx_mtrl = 0;
		adapter->flags &= ~(IXGBE_FLAG_RX_HWTSTAMP_ENABLED |
				    IXGBE_FLAG_RX_HWTSTAMP_IN_REGISTER);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_mtrl |= IXGBE_RXMTRL_V1_SYNC_MSG;
		adapter->flags |= (IXGBE_FLAG_RX_HWTSTAMP_ENABLED |
				   IXGBE_FLAG_RX_HWTSTAMP_IN_REGISTER);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
		tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_L4_V1;
		tsync_rx_mtrl |= IXGBE_RXMTRL_V1_DELAY_REQ_MSG;
		adapter->flags |= (IXGBE_FLAG_RX_HWTSTAMP_ENABLED |
				   IXGBE_FLAG_RX_HWTSTAMP_IN_REGISTER);
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
		config->rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
		adapter->flags |= (IXGBE_FLAG_RX_HWTSTAMP_ENABLED |
				   IXGBE_FLAG_RX_HWTSTAMP_IN_REGISTER);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_ALL:
		/* The X550 controller is capable of timestamping all packets,
		 * which allows it to accept any filter.
		 */
		if (hw->mac.type >= ixgbe_mac_X550) {
			tsync_rx_ctl |= IXGBE_TSYNCRXCTL_TYPE_ALL;
			config->rx_filter = HWTSTAMP_FILTER_ALL;
			adapter->flags |= IXGBE_FLAG_RX_HWTSTAMP_ENABLED;
			break;
		}
		/* fall through */
	default:
		/*
		 * register RXMTRL must be set in order to do V1 packets,
		 * therefore it is not possible to time stamp both V1 Sync and
		 * Delay_Req messages and hardware does not support
		 * timestamping all packets => return error
		 */
		adapter->flags &= ~(IXGBE_FLAG_RX_HWTSTAMP_ENABLED |
				    IXGBE_FLAG_RX_HWTSTAMP_IN_REGISTER);
		config->rx_filter = HWTSTAMP_FILTER_NONE;
		return -ERANGE;
	}

	if (hw->mac.type == ixgbe_mac_82598EB) {
		adapter->flags &= ~(IXGBE_FLAG_RX_HWTSTAMP_ENABLED |
				    IXGBE_FLAG_RX_HWTSTAMP_IN_REGISTER);
		if (tsync_rx_ctl | tsync_tx_ctl)
			return -ERANGE;
		return 0;
	}

	/* Per-packet timestamping only works if the filter is set to all
	 * packets. Since this is desired, always timestamp all packets as long
	 * as any Rx filter was configured.
	 */
	switch (hw->mac.type) {
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_x550em_a:
		/* enable timestamping all packets only if at least some
		 * packets were requested. Otherwise, play nice and disable
		 * timestamping
		 */
		if (config->rx_filter == HWTSTAMP_FILTER_NONE)
			break;

		tsync_rx_ctl = IXGBE_TSYNCRXCTL_ENABLED |
			       IXGBE_TSYNCRXCTL_TYPE_ALL |
			       IXGBE_TSYNCRXCTL_TSIP_UT_EN;
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		adapter->flags |= IXGBE_FLAG_RX_HWTSTAMP_ENABLED;
		adapter->flags &= ~IXGBE_FLAG_RX_HWTSTAMP_IN_REGISTER;
		is_l2 = true;
		break;
	default:
		break;
	}

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
	ixgbe_ptp_clear_tx_timestamp(adapter);
	IXGBE_READ_REG(hw, IXGBE_RXSTMPH);

	return 0;
}

/**
 * ixgbe_ptp_set_ts_config - user entry point for timestamp mode
 * @adapter: pointer to adapter struct
 * @ifreq: ioctl data
 *
 * Set hardware to requested mode. If unsupported, return an error with no
 * changes. Otherwise, store the mode for future reference.
 */
int ixgbe_ptp_set_ts_config(struct ixgbe_adapter *adapter, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = ixgbe_ptp_set_timestamp_mode(adapter, &config);
	if (err)
		return err;

	/* save these settings for future reference */
	memcpy(&adapter->tstamp_config, &config,
	       sizeof(adapter->tstamp_config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

static void ixgbe_ptp_link_speed_adjust(struct ixgbe_adapter *adapter,
					u32 *shift, u32 *incval)
{
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
		*shift = IXGBE_INCVAL_SHIFT_100;
		*incval = IXGBE_INCVAL_100;
		break;
	case IXGBE_LINK_SPEED_1GB_FULL:
		*shift = IXGBE_INCVAL_SHIFT_1GB;
		*incval = IXGBE_INCVAL_1GB;
		break;
	case IXGBE_LINK_SPEED_10GB_FULL:
	default:
		*shift = IXGBE_INCVAL_SHIFT_10GB;
		*incval = IXGBE_INCVAL_10GB;
		break;
	}
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
	struct cyclecounter cc;
	unsigned long flags;
	u32 incval = 0;
	u32 tsauxc = 0;
	u32 fuse0 = 0;

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

	switch (hw->mac.type) {
	case ixgbe_mac_X550EM_x:
		/* SYSTIME assumes X550EM_x board frequency is 300Mhz, and is
		 * designed to represent seconds and nanoseconds when this is
		 * the case. However, some revisions of hardware have a 400Mhz
		 * clock and we have to compensate for this frequency
		 * variation using corrected mult and shift values.
		 */
		fuse0 = IXGBE_READ_REG(hw, IXGBE_FUSES0_GROUP(0));
		if (!(fuse0 & IXGBE_FUSES0_300MHZ)) {
			cc.mult = 3;
			cc.shift = 2;
		}
		/* fallthrough */
	case ixgbe_mac_x550em_a:
	case ixgbe_mac_X550:
		cc.read = ixgbe_ptp_read_X550;

		/* enable SYSTIME counter */
		IXGBE_WRITE_REG(hw, IXGBE_SYSTIMR, 0);
		IXGBE_WRITE_REG(hw, IXGBE_SYSTIML, 0);
		IXGBE_WRITE_REG(hw, IXGBE_SYSTIMH, 0);
		tsauxc = IXGBE_READ_REG(hw, IXGBE_TSAUXC);
		IXGBE_WRITE_REG(hw, IXGBE_TSAUXC,
				tsauxc & ~IXGBE_TSAUXC_DISABLE_SYSTIME);
		IXGBE_WRITE_REG(hw, IXGBE_TSIM, IXGBE_TSIM_TXTS);
		IXGBE_WRITE_REG(hw, IXGBE_EIMS, IXGBE_EIMS_TIMESYNC);

		IXGBE_WRITE_FLUSH(hw);
		break;
	case ixgbe_mac_X540:
		cc.read = ixgbe_ptp_read_82599;

		ixgbe_ptp_link_speed_adjust(adapter, &cc.shift, &incval);
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA, incval);
		break;
	case ixgbe_mac_82599EB:
		cc.read = ixgbe_ptp_read_82599;

		ixgbe_ptp_link_speed_adjust(adapter, &cc.shift, &incval);
		incval >>= IXGBE_INCVAL_SHIFT_82599;
		cc.shift -= IXGBE_INCVAL_SHIFT_82599;
		IXGBE_WRITE_REG(hw, IXGBE_TIMINCA,
				BIT(IXGBE_INCPER_SHIFT_82599) | incval);
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
	memcpy(&adapter->hw_cc, &cc, sizeof(adapter->hw_cc));
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);
}

/**
 * ixgbe_ptp_reset
 * @adapter: the ixgbe private board structure
 *
 * When the MAC resets, all the hardware bits for timesync are reset. This
 * function is used to re-enable the device for PTP based on current settings.
 * We do lose the current clock time, so just reset the cyclecounter to the
 * system real clock time.
 *
 * This function will maintain hwtstamp_config settings, and resets the SDP
 * output if it was enabled.
 */
void ixgbe_ptp_reset(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	unsigned long flags;

	/* reset the hardware timestamping mode */
	ixgbe_ptp_set_timestamp_mode(adapter, &adapter->tstamp_config);

	/* 82598 does not support PTP */
	if (hw->mac.type == ixgbe_mac_82598EB)
		return;

	ixgbe_ptp_start_cyclecounter(adapter);

	spin_lock_irqsave(&adapter->tmreg_lock, flags);
	timecounter_init(&adapter->hw_tc, &adapter->hw_cc,
			 ktime_to_ns(ktime_get_real()));
	spin_unlock_irqrestore(&adapter->tmreg_lock, flags);

	adapter->last_overflow_check = jiffies;

	/* Now that the shift has been calculated and the systime
	 * registers reset, (re-)enable the Clock out feature
	 */
	if (adapter->ptp_setup_sdp)
		adapter->ptp_setup_sdp(adapter);
}

/**
 * ixgbe_ptp_create_clock
 * @adapter: the ixgbe private adapter structure
 *
 * This function performs setup of the user entry point function table and
 * initializes the PTP clock device, which is used to access the clock-like
 * features of the PTP core. It will be called by ixgbe_ptp_init, and may
 * reuse a previously initialized clock (such as during a suspend/resume
 * cycle).
 */
static long ixgbe_ptp_create_clock(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	long err;

	/* do nothing if we already have a clock device */
	if (!IS_ERR_OR_NULL(adapter->ptp_clock))
		return 0;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_X540:
		snprintf(adapter->ptp_caps.name,
			 sizeof(adapter->ptp_caps.name),
			 "%s", netdev->name);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 250000000;
		adapter->ptp_caps.n_alarm = 0;
		adapter->ptp_caps.n_ext_ts = 0;
		adapter->ptp_caps.n_per_out = 0;
		adapter->ptp_caps.pps = 1;
		adapter->ptp_caps.adjfreq = ixgbe_ptp_adjfreq_82599;
		adapter->ptp_caps.adjtime = ixgbe_ptp_adjtime;
		adapter->ptp_caps.gettime64 = ixgbe_ptp_gettime;
		adapter->ptp_caps.settime64 = ixgbe_ptp_settime;
		adapter->ptp_caps.enable = ixgbe_ptp_feature_enable;
		adapter->ptp_setup_sdp = ixgbe_ptp_setup_sdp_x540;
		break;
	case ixgbe_mac_82599EB:
		snprintf(adapter->ptp_caps.name,
			 sizeof(adapter->ptp_caps.name),
			 "%s", netdev->name);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 250000000;
		adapter->ptp_caps.n_alarm = 0;
		adapter->ptp_caps.n_ext_ts = 0;
		adapter->ptp_caps.n_per_out = 0;
		adapter->ptp_caps.pps = 0;
		adapter->ptp_caps.adjfreq = ixgbe_ptp_adjfreq_82599;
		adapter->ptp_caps.adjtime = ixgbe_ptp_adjtime;
		adapter->ptp_caps.gettime64 = ixgbe_ptp_gettime;
		adapter->ptp_caps.settime64 = ixgbe_ptp_settime;
		adapter->ptp_caps.enable = ixgbe_ptp_feature_enable;
		break;
	case ixgbe_mac_X550:
	case ixgbe_mac_X550EM_x:
	case ixgbe_mac_x550em_a:
		snprintf(adapter->ptp_caps.name, 16, "%s", netdev->name);
		adapter->ptp_caps.owner = THIS_MODULE;
		adapter->ptp_caps.max_adj = 30000000;
		adapter->ptp_caps.n_alarm = 0;
		adapter->ptp_caps.n_ext_ts = 0;
		adapter->ptp_caps.n_per_out = 0;
		adapter->ptp_caps.pps = 0;
		adapter->ptp_caps.adjfreq = ixgbe_ptp_adjfreq_X550;
		adapter->ptp_caps.adjtime = ixgbe_ptp_adjtime;
		adapter->ptp_caps.gettime64 = ixgbe_ptp_gettime;
		adapter->ptp_caps.settime64 = ixgbe_ptp_settime;
		adapter->ptp_caps.enable = ixgbe_ptp_feature_enable;
		adapter->ptp_setup_sdp = NULL;
		break;
	default:
		adapter->ptp_clock = NULL;
		adapter->ptp_setup_sdp = NULL;
		return -EOPNOTSUPP;
	}

	adapter->ptp_clock = ptp_clock_register(&adapter->ptp_caps,
						&adapter->pdev->dev);
	if (IS_ERR(adapter->ptp_clock)) {
		err = PTR_ERR(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
		e_dev_err("ptp_clock_register failed\n");
		return err;
	} else if (adapter->ptp_clock)
		e_dev_info("registered PHC device on %s\n", netdev->name);

	/* set default timestamp mode to disabled here. We do this in
	 * create_clock instead of init, because we don't want to override the
	 * previous settings during a resume cycle.
	 */
	adapter->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
	adapter->tstamp_config.tx_type = HWTSTAMP_TX_OFF;

	return 0;
}

/**
 * ixgbe_ptp_init
 * @adapter: the ixgbe private adapter structure
 *
 * This function performs the required steps for enabling PTP
 * support. If PTP support has already been loaded it simply calls the
 * cyclecounter init routine and exits.
 */
void ixgbe_ptp_init(struct ixgbe_adapter *adapter)
{
	/* initialize the spin lock first since we can't control when a user
	 * will call the entry functions once we have initialized the clock
	 * device
	 */
	spin_lock_init(&adapter->tmreg_lock);

	/* obtain a PTP device, or re-use an existing device */
	if (ixgbe_ptp_create_clock(adapter))
		return;

	/* we have a clock so we can initialize work now */
	INIT_WORK(&adapter->ptp_tx_work, ixgbe_ptp_tx_hwtstamp_work);

	/* reset the PTP related hardware bits */
	ixgbe_ptp_reset(adapter);

	/* enter the IXGBE_PTP_RUNNING state */
	set_bit(__IXGBE_PTP_RUNNING, &adapter->state);

	return;
}

/**
 * ixgbe_ptp_suspend - stop PTP work items
 * @ adapter: pointer to adapter struct
 *
 * this function suspends PTP activity, and prevents more PTP work from being
 * generated, but does not destroy the PTP clock device.
 */
void ixgbe_ptp_suspend(struct ixgbe_adapter *adapter)
{
	/* Leave the IXGBE_PTP_RUNNING state. */
	if (!test_and_clear_bit(__IXGBE_PTP_RUNNING, &adapter->state))
		return;

	adapter->flags2 &= ~IXGBE_FLAG2_PTP_PPS_ENABLED;
	if (adapter->ptp_setup_sdp)
		adapter->ptp_setup_sdp(adapter);

	/* ensure that we cancel any pending PTP Tx work item in progress */
	cancel_work_sync(&adapter->ptp_tx_work);
	ixgbe_ptp_clear_tx_timestamp(adapter);
}

/**
 * ixgbe_ptp_stop - close the PTP device
 * @adapter: pointer to adapter struct
 *
 * completely destroy the PTP device, should only be called when the device is
 * being fully closed.
 */
void ixgbe_ptp_stop(struct ixgbe_adapter *adapter)
{
	/* first, suspend PTP activity */
	ixgbe_ptp_suspend(adapter);

	/* disable the PTP clock device */
	if (adapter->ptp_clock) {
		ptp_clock_unregister(adapter->ptp_clock);
		adapter->ptp_clock = NULL;
		e_dev_info("removed PHC on %s\n",
			   adapter->netdev->name);
	}
}
