// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#include <linux/ptp_classify.h>
#include <linux/posix-clock.h>
#include "i40e.h"
#include "i40e_devids.h"

/* The XL710 timesync is very much like Intel's 82599 design when it comes to
 * the fundamental clock design. However, the clock operations are much simpler
 * in the XL710 because the device supports a full 64 bits of nanoseconds.
 * Because the field is so wide, we can forgo the cycle counter and just
 * operate with the nanosecond field directly without fear of overflow.
 *
 * Much like the 82599, the update period is dependent upon the link speed:
 * At 40Gb, 25Gb, or no link, the period is 1.6ns.
 * At 10Gb or 5Gb link, the period is multiplied by 2. (3.2ns)
 * At 1Gb link, the period is multiplied by 20. (32ns)
 * 1588 functionality is not supported at 100Mbps.
 */
#define I40E_PTP_40GB_INCVAL		0x0199999999ULL
#define I40E_PTP_10GB_INCVAL_MULT	2
#define I40E_PTP_5GB_INCVAL_MULT	2
#define I40E_PTP_1GB_INCVAL_MULT	20
#define I40E_ISGN			0x80000000

#define I40E_PRTTSYN_CTL1_TSYNTYPE_V1  BIT(I40E_PRTTSYN_CTL1_TSYNTYPE_SHIFT)
#define I40E_PRTTSYN_CTL1_TSYNTYPE_V2  (2 << \
					I40E_PRTTSYN_CTL1_TSYNTYPE_SHIFT)
#define I40E_SUBDEV_ID_25G_PTP_PIN	0xB

enum i40e_ptp_pin {
	SDP3_2 = 0,
	SDP3_3,
	GPIO_4
};

enum i40e_can_set_pins {
	CANT_DO_PINS = -1,
	CAN_SET_PINS,
	CAN_DO_PINS
};

static struct ptp_pin_desc sdp_desc[] = {
	/* name     idx      func      chan */
	{"SDP3_2", SDP3_2, PTP_PF_NONE, 0},
	{"SDP3_3", SDP3_3, PTP_PF_NONE, 1},
	{"GPIO_4", GPIO_4, PTP_PF_NONE, 1},
};

enum i40e_ptp_gpio_pin_state {
	end = -2,
	invalid,
	off,
	in_A,
	in_B,
	out_A,
	out_B,
};

static const char * const i40e_ptp_gpio_pin_state2str[] = {
	"off", "in_A", "in_B", "out_A", "out_B"
};

enum i40e_ptp_led_pin_state {
	led_end = -2,
	low = 0,
	high,
};

struct i40e_ptp_pins_settings {
	enum i40e_ptp_gpio_pin_state sdp3_2;
	enum i40e_ptp_gpio_pin_state sdp3_3;
	enum i40e_ptp_gpio_pin_state gpio_4;
	enum i40e_ptp_led_pin_state led2_0;
	enum i40e_ptp_led_pin_state led2_1;
	enum i40e_ptp_led_pin_state led3_0;
	enum i40e_ptp_led_pin_state led3_1;
};

static const struct i40e_ptp_pins_settings
	i40e_ptp_pin_led_allowed_states[] = {
	{off,	off,	off,		high,	high,	high,	high},
	{off,	in_A,	off,		high,	high,	high,	low},
	{off,	out_A,	off,		high,	low,	high,	high},
	{off,	in_B,	off,		high,	high,	high,	low},
	{off,	out_B,	off,		high,	low,	high,	high},
	{in_A,	off,	off,		high,	high,	high,	low},
	{in_A,	in_B,	off,		high,	high,	high,	low},
	{in_A,	out_B,	off,		high,	low,	high,	high},
	{out_A,	off,	off,		high,	low,	high,	high},
	{out_A,	in_B,	off,		high,	low,	high,	high},
	{in_B,	off,	off,		high,	high,	high,	low},
	{in_B,	in_A,	off,		high,	high,	high,	low},
	{in_B,	out_A,	off,		high,	low,	high,	high},
	{out_B,	off,	off,		high,	low,	high,	high},
	{out_B,	in_A,	off,		high,	low,	high,	high},
	{off,	off,	in_A,		high,	high,	low,	high},
	{off,	out_A,	in_A,		high,	low,	low,	high},
	{off,	in_B,	in_A,		high,	high,	low,	low},
	{off,	out_B,	in_A,		high,	low,	low,	high},
	{out_A,	off,	in_A,		high,	low,	low,	high},
	{out_A,	in_B,	in_A,		high,	low,	low,	high},
	{in_B,	off,	in_A,		high,	high,	low,	low},
	{in_B,	out_A,	in_A,		high,	low,	low,	high},
	{out_B,	off,	in_A,		high,	low,	low,	high},
	{off,	off,	out_A,		low,	high,	high,	high},
	{off,	in_A,	out_A,		low,	high,	high,	low},
	{off,	in_B,	out_A,		low,	high,	high,	low},
	{off,	out_B,	out_A,		low,	low,	high,	high},
	{in_A,	off,	out_A,		low,	high,	high,	low},
	{in_A,	in_B,	out_A,		low,	high,	high,	low},
	{in_A,	out_B,	out_A,		low,	low,	high,	high},
	{in_B,	off,	out_A,		low,	high,	high,	low},
	{in_B,	in_A,	out_A,		low,	high,	high,	low},
	{out_B,	off,	out_A,		low,	low,	high,	high},
	{out_B,	in_A,	out_A,		low,	low,	high,	high},
	{off,	off,	in_B,		high,	high,	low,	high},
	{off,	in_A,	in_B,		high,	high,	low,	low},
	{off,	out_A,	in_B,		high,	low,	low,	high},
	{off,	out_B,	in_B,		high,	low,	low,	high},
	{in_A,	off,	in_B,		high,	high,	low,	low},
	{in_A,	out_B,	in_B,		high,	low,	low,	high},
	{out_A,	off,	in_B,		high,	low,	low,	high},
	{out_B,	off,	in_B,		high,	low,	low,	high},
	{out_B,	in_A,	in_B,		high,	low,	low,	high},
	{off,	off,	out_B,		low,	high,	high,	high},
	{off,	in_A,	out_B,		low,	high,	high,	low},
	{off,	out_A,	out_B,		low,	low,	high,	high},
	{off,	in_B,	out_B,		low,	high,	high,	low},
	{in_A,	off,	out_B,		low,	high,	high,	low},
	{in_A,	in_B,	out_B,		low,	high,	high,	low},
	{out_A,	off,	out_B,		low,	low,	high,	high},
	{out_A,	in_B,	out_B,		low,	low,	high,	high},
	{in_B,	off,	out_B,		low,	high,	high,	low},
	{in_B,	in_A,	out_B,		low,	high,	high,	low},
	{in_B,	out_A,	out_B,		low,	low,	high,	high},
	{end,	end,	end,	led_end, led_end, led_end, led_end}
};

static int i40e_ptp_set_pins(struct i40e_pf *pf,
			     struct i40e_ptp_pins_settings *pins);

/**
 * i40e_ptp_extts0_work - workqueue task function
 * @work: workqueue task structure
 *
 * Service for PTP external clock event
 **/
static void i40e_ptp_extts0_work(struct work_struct *work)
{
	struct i40e_pf *pf = container_of(work, struct i40e_pf,
					  ptp_extts0_work);
	struct i40e_hw *hw = &pf->hw;
	struct ptp_clock_event event;
	u32 hi, lo;

	/* Event time is captured by one of the two matched registers
	 *      PRTTSYN_EVNT_L: 32 LSB of sampled time event
	 *      PRTTSYN_EVNT_H: 32 MSB of sampled time event
	 * Event is defined in PRTTSYN_EVNT_0 register
	 */
	lo = rd32(hw, I40E_PRTTSYN_EVNT_L(0));
	hi = rd32(hw, I40E_PRTTSYN_EVNT_H(0));

	event.timestamp = (((u64)hi) << 32) | lo;

	event.type = PTP_CLOCK_EXTTS;
	event.index = hw->pf_id;

	/* fire event */
	ptp_clock_event(pf->ptp_clock, &event);
}

/**
 * i40e_is_ptp_pin_dev - check if device supports PTP pins
 * @hw: pointer to the hardware structure
 *
 * Return true if device supports PTP pins, false otherwise.
 **/
static bool i40e_is_ptp_pin_dev(struct i40e_hw *hw)
{
	return hw->device_id == I40E_DEV_ID_25G_SFP28 &&
	       hw->subsystem_device_id == I40E_SUBDEV_ID_25G_PTP_PIN;
}

/**
 * i40e_can_set_pins - check possibility of manipulating the pins
 * @pf: board private structure
 *
 * Check if all conditions are satisfied to manipulate PTP pins.
 * Return CAN_SET_PINS if pins can be set on a specific PF or
 * return CAN_DO_PINS if pins can be manipulated within a NIC or
 * return CANT_DO_PINS otherwise.
 **/
static enum i40e_can_set_pins i40e_can_set_pins(struct i40e_pf *pf)
{
	if (!i40e_is_ptp_pin_dev(&pf->hw)) {
		dev_warn(&pf->pdev->dev,
			 "PTP external clock not supported.\n");
		return CANT_DO_PINS;
	}

	if (!pf->ptp_pins) {
		dev_warn(&pf->pdev->dev,
			 "PTP PIN manipulation not allowed.\n");
		return CANT_DO_PINS;
	}

	if (pf->hw.pf_id) {
		dev_warn(&pf->pdev->dev,
			 "PTP PINs should be accessed via PF0.\n");
		return CAN_DO_PINS;
	}

	return CAN_SET_PINS;
}

/**
 * i40_ptp_reset_timing_events - Reset PTP timing events
 * @pf: Board private structure
 *
 * This function resets timing events for pf.
 **/
static void i40_ptp_reset_timing_events(struct i40e_pf *pf)
{
	u32 i;

	spin_lock_bh(&pf->ptp_rx_lock);
	for (i = 0; i <= I40E_PRTTSYN_RXTIME_L_MAX_INDEX; i++) {
		/* reading and automatically clearing timing events registers */
		rd32(&pf->hw, I40E_PRTTSYN_RXTIME_L(i));
		rd32(&pf->hw, I40E_PRTTSYN_RXTIME_H(i));
		pf->latch_events[i] = 0;
	}
	/* reading and automatically clearing timing events registers */
	rd32(&pf->hw, I40E_PRTTSYN_TXTIME_L);
	rd32(&pf->hw, I40E_PRTTSYN_TXTIME_H);

	pf->tx_hwtstamp_timeouts = 0;
	pf->tx_hwtstamp_skipped = 0;
	pf->rx_hwtstamp_cleared = 0;
	pf->latch_event_flags = 0;
	spin_unlock_bh(&pf->ptp_rx_lock);
}

/**
 * i40e_ptp_verify - check pins
 * @ptp: ptp clock
 * @pin: pin index
 * @func: assigned function
 * @chan: channel
 *
 * Check pins consistency.
 * Return 0 on success or error on failure.
 **/
static int i40e_ptp_verify(struct ptp_clock_info *ptp, unsigned int pin,
			   enum ptp_pin_function func, unsigned int chan)
{
	switch (func) {
	case PTP_PF_NONE:
	case PTP_PF_EXTTS:
	case PTP_PF_PEROUT:
		break;
	case PTP_PF_PHYSYNC:
		return -EOPNOTSUPP;
	}
	return 0;
}

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
 * i40e_ptp_adjfine - Adjust the PHC frequency
 * @ptp: The PTP clock structure
 * @scaled_ppm: Scaled parts per million adjustment from base
 *
 * Adjust the frequency of the PHC by the indicated delta from the base
 * frequency.
 *
 * Scaled parts per million is ppm with a 16 bit binary fractional field.
 **/
static int i40e_ptp_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);
	struct i40e_hw *hw = &pf->hw;
	u64 adj, base_adj;

	smp_mb(); /* Force any pending update before accessing. */
	base_adj = I40E_PTP_40GB_INCVAL * READ_ONCE(pf->ptp_adj_mult);

	adj = adjust_by_scaled_ppm(base_adj, scaled_ppm);

	wr32(hw, I40E_PRTTSYN_INC_L, adj & 0xFFFFFFFF);
	wr32(hw, I40E_PRTTSYN_INC_H, adj >> 32);

	return 0;
}

/**
 * i40e_ptp_set_1pps_signal_hw - configure 1PPS PTP signal for pins
 * @pf: the PF private data structure
 *
 * Configure 1PPS signal used for PTP pins
 **/
static void i40e_ptp_set_1pps_signal_hw(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	struct timespec64 now;
	u64 ns;

	wr32(hw, I40E_PRTTSYN_AUX_0(1), 0);
	wr32(hw, I40E_PRTTSYN_AUX_1(1), I40E_PRTTSYN_AUX_1_INSTNT);
	wr32(hw, I40E_PRTTSYN_AUX_0(1), I40E_PRTTSYN_AUX_0_OUT_ENABLE);

	i40e_ptp_read(pf, &now, NULL);
	now.tv_sec += I40E_PTP_2_SEC_DELAY;
	now.tv_nsec = 0;
	ns = timespec64_to_ns(&now);

	/* I40E_PRTTSYN_TGT_L(1) */
	wr32(hw, I40E_PRTTSYN_TGT_L(1), ns & 0xFFFFFFFF);
	/* I40E_PRTTSYN_TGT_H(1) */
	wr32(hw, I40E_PRTTSYN_TGT_H(1), ns >> 32);
	wr32(hw, I40E_PRTTSYN_CLKO(1), I40E_PTP_HALF_SECOND);
	wr32(hw, I40E_PRTTSYN_AUX_1(1), I40E_PRTTSYN_AUX_1_INSTNT);
	wr32(hw, I40E_PRTTSYN_AUX_0(1),
	     I40E_PRTTSYN_AUX_0_OUT_ENABLE_CLK_MOD);
}

/**
 * i40e_ptp_adjtime - Adjust the PHC time
 * @ptp: The PTP clock structure
 * @delta: Offset in nanoseconds to adjust the PHC time by
 *
 * Adjust the current clock time by a delta specified in nanoseconds.
 **/
static int i40e_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);
	struct i40e_hw *hw = &pf->hw;

	mutex_lock(&pf->tmreg_lock);

	if (delta > -999999900LL && delta < 999999900LL) {
		int neg_adj = 0;
		u32 timadj;
		u64 tohw;

		if (delta < 0) {
			neg_adj = 1;
			tohw = -delta;
		} else {
			tohw = delta;
		}

		timadj = tohw & 0x3FFFFFFF;
		if (neg_adj)
			timadj |= I40E_ISGN;
		wr32(hw, I40E_PRTTSYN_ADJ, timadj);
	} else {
		struct timespec64 then, now;

		then = ns_to_timespec64(delta);
		i40e_ptp_read(pf, &now, NULL);
		now = timespec64_add(now, then);
		i40e_ptp_write(pf, (const struct timespec64 *)&now);
		i40e_ptp_set_1pps_signal_hw(pf);
	}

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
 * @ts: timespec64 structure that holds the new time value
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
 * i40e_pps_configure - configure PPS events
 * @ptp: ptp clock
 * @rq: clock request
 * @on: status
 *
 * Configure PPS events for external clock source.
 * Return 0 on success or error on failure.
 **/
static int i40e_pps_configure(struct ptp_clock_info *ptp,
			      struct ptp_clock_request *rq,
			      int on)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);

	if (!!on)
		i40e_ptp_set_1pps_signal_hw(pf);

	return 0;
}

/**
 * i40e_pin_state - determine PIN state
 * @index: PIN index
 * @func: function assigned to PIN
 *
 * Determine PIN state based on PIN index and function assigned.
 * Return PIN state.
 **/
static enum i40e_ptp_gpio_pin_state i40e_pin_state(int index, int func)
{
	enum i40e_ptp_gpio_pin_state state = off;

	if (index == 0 && func == PTP_PF_EXTTS)
		state = in_A;
	if (index == 1 && func == PTP_PF_EXTTS)
		state = in_B;
	if (index == 0 && func == PTP_PF_PEROUT)
		state = out_A;
	if (index == 1 && func == PTP_PF_PEROUT)
		state = out_B;

	return state;
}

/**
 * i40e_ptp_enable_pin - enable PINs.
 * @pf: private board structure
 * @chan: channel
 * @func: PIN function
 * @on: state
 *
 * Enable PTP pins for external clock source.
 * Return 0 on success or error code on failure.
 **/
static int i40e_ptp_enable_pin(struct i40e_pf *pf, unsigned int chan,
			       enum ptp_pin_function func, int on)
{
	enum i40e_ptp_gpio_pin_state *pin = NULL;
	struct i40e_ptp_pins_settings pins;
	int pin_index;

	/* Use PF0 to set pins. Return success for user space tools */
	if (pf->hw.pf_id)
		return 0;

	/* Preserve previous state of pins that we don't touch */
	pins.sdp3_2 = pf->ptp_pins->sdp3_2;
	pins.sdp3_3 = pf->ptp_pins->sdp3_3;
	pins.gpio_4 = pf->ptp_pins->gpio_4;

	/* To turn on the pin - find the corresponding one based on
	 * the given index. To turn the function off - find
	 * which pin had it assigned. Don't use ptp_find_pin here
	 * because it tries to lock the pincfg_mux which is locked by
	 * ptp_pin_store() that calls here.
	 */
	if (on) {
		pin_index = ptp_find_pin(pf->ptp_clock, func, chan);
		if (pin_index < 0)
			return -EBUSY;

		switch (pin_index) {
		case SDP3_2:
			pin = &pins.sdp3_2;
			break;
		case SDP3_3:
			pin = &pins.sdp3_3;
			break;
		case GPIO_4:
			pin = &pins.gpio_4;
			break;
		default:
			return -EINVAL;
		}

		*pin = i40e_pin_state(chan, func);
	} else {
		pins.sdp3_2 = off;
		pins.sdp3_3 = off;
		pins.gpio_4 = off;
	}

	return i40e_ptp_set_pins(pf, &pins) ? -EINVAL : 0;
}

/**
 * i40e_ptp_feature_enable - Enable external clock pins
 * @ptp: The PTP clock structure
 * @rq: The PTP clock request structure
 * @on: To turn feature on/off
 *
 * Setting on/off PTP PPS feature for pin.
 **/
static int i40e_ptp_feature_enable(struct ptp_clock_info *ptp,
				   struct ptp_clock_request *rq,
				   int on)
{
	struct i40e_pf *pf = container_of(ptp, struct i40e_pf, ptp_caps);

	enum ptp_pin_function func;
	unsigned int chan;

	/* TODO: Implement flags handling for EXTTS and PEROUT */
	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		func = PTP_PF_EXTTS;
		chan = rq->extts.index;
		break;
	case PTP_CLK_REQ_PEROUT:
		func = PTP_PF_PEROUT;
		chan = rq->perout.index;
		break;
	case PTP_CLK_REQ_PPS:
		return i40e_pps_configure(ptp, rq, on);
	default:
		return -EOPNOTSUPP;
	}

	return i40e_ptp_enable_pin(pf, chan, func, on);
}

/**
 * i40e_ptp_get_rx_events - Read I40E_PRTTSYN_STAT_1 and latch events
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
	if (!test_bit(I40E_FLAG_PTP_ENA, pf->flags) || !pf->ptp_rx)
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

	if (!test_bit(I40E_FLAG_PTP_ENA, pf->flags) || !pf->ptp_tx)
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

	if (!test_bit(I40E_FLAG_PTP_ENA, pf->flags) || !pf->ptp_tx)
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
	if (!test_bit(I40E_FLAG_PTP_ENA, pf->flags) || !pf->ptp_rx)
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
	case I40E_LINK_SPEED_5GB:
		mult = I40E_PTP_5GB_INCVAL_MULT;
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
 * i40e_ptp_hwtstamp_get - interface to read the HW timestamping
 * @netdev: Network device structure
 * @config: Timestamping configuration structure
 *
 * Obtain the current hardware timestamping settigs as requested. To do this,
 * keep a shadow copy of the timestamp settings rather than attempting to
 * deconstruct it from the registers.
 **/
int i40e_ptp_hwtstamp_get(struct net_device *netdev,
			  struct kernel_hwtstamp_config *config)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;

	if (!test_bit(I40E_FLAG_PTP_ENA, pf->flags))
		return -EOPNOTSUPP;

	*config = pf->tstamp_config;

	return 0;
}

/**
 * i40e_ptp_free_pins - free memory used by PTP pins
 * @pf: Board private structure
 *
 * Release memory allocated for PTP pins.
 **/
static void i40e_ptp_free_pins(struct i40e_pf *pf)
{
	if (i40e_is_ptp_pin_dev(&pf->hw)) {
		kfree(pf->ptp_pins);
		kfree(pf->ptp_caps.pin_config);
		pf->ptp_pins = NULL;
	}
}

/**
 * i40e_ptp_set_pin_hw - Set HW GPIO pin
 * @hw: pointer to the hardware structure
 * @pin: pin index
 * @state: pin state
 *
 * Set status of GPIO pin for external clock handling.
 **/
static void i40e_ptp_set_pin_hw(struct i40e_hw *hw,
				unsigned int pin,
				enum i40e_ptp_gpio_pin_state state)
{
	switch (state) {
	case off:
		wr32(hw, I40E_GLGEN_GPIO_CTL(pin), 0);
		break;
	case in_A:
		wr32(hw, I40E_GLGEN_GPIO_CTL(pin),
		     I40E_GLGEN_GPIO_CTL_PORT_0_IN_TIMESYNC_0);
		break;
	case in_B:
		wr32(hw, I40E_GLGEN_GPIO_CTL(pin),
		     I40E_GLGEN_GPIO_CTL_PORT_1_IN_TIMESYNC_0);
		break;
	case out_A:
		wr32(hw, I40E_GLGEN_GPIO_CTL(pin),
		     I40E_GLGEN_GPIO_CTL_PORT_0_OUT_TIMESYNC_1);
		break;
	case out_B:
		wr32(hw, I40E_GLGEN_GPIO_CTL(pin),
		     I40E_GLGEN_GPIO_CTL_PORT_1_OUT_TIMESYNC_1);
		break;
	default:
		break;
	}
}

/**
 * i40e_ptp_set_led_hw - Set HW GPIO led
 * @hw: pointer to the hardware structure
 * @led: led index
 * @state: led state
 *
 * Set status of GPIO led for external clock handling.
 **/
static void i40e_ptp_set_led_hw(struct i40e_hw *hw,
				unsigned int led,
				enum i40e_ptp_led_pin_state state)
{
	switch (state) {
	case low:
		wr32(hw, I40E_GLGEN_GPIO_SET,
		     I40E_GLGEN_GPIO_SET_DRV_SDP_DATA | led);
		break;
	case high:
		wr32(hw, I40E_GLGEN_GPIO_SET,
		     I40E_GLGEN_GPIO_SET_DRV_SDP_DATA |
		     I40E_GLGEN_GPIO_SET_SDP_DATA_HI | led);
		break;
	default:
		break;
	}
}

/**
 * i40e_ptp_init_leds_hw - init LEDs
 * @hw: pointer to a hardware structure
 *
 * Set initial state of LEDs
 **/
static void i40e_ptp_init_leds_hw(struct i40e_hw *hw)
{
	wr32(hw, I40E_GLGEN_GPIO_CTL(I40E_LED2_0),
	     I40E_GLGEN_GPIO_CTL_LED_INIT);
	wr32(hw, I40E_GLGEN_GPIO_CTL(I40E_LED2_1),
	     I40E_GLGEN_GPIO_CTL_LED_INIT);
	wr32(hw, I40E_GLGEN_GPIO_CTL(I40E_LED3_0),
	     I40E_GLGEN_GPIO_CTL_LED_INIT);
	wr32(hw, I40E_GLGEN_GPIO_CTL(I40E_LED3_1),
	     I40E_GLGEN_GPIO_CTL_LED_INIT);
}

/**
 * i40e_ptp_set_pins_hw - Set HW GPIO pins
 * @pf: Board private structure
 *
 * This function sets GPIO pins for PTP
 **/
static void i40e_ptp_set_pins_hw(struct i40e_pf *pf)
{
	const struct i40e_ptp_pins_settings *pins = pf->ptp_pins;
	struct i40e_hw *hw = &pf->hw;

	/* pin must be disabled before it may be used */
	i40e_ptp_set_pin_hw(hw, I40E_SDP3_2, off);
	i40e_ptp_set_pin_hw(hw, I40E_SDP3_3, off);
	i40e_ptp_set_pin_hw(hw, I40E_GPIO_4, off);

	i40e_ptp_set_pin_hw(hw, I40E_SDP3_2, pins->sdp3_2);
	i40e_ptp_set_pin_hw(hw, I40E_SDP3_3, pins->sdp3_3);
	i40e_ptp_set_pin_hw(hw, I40E_GPIO_4, pins->gpio_4);

	i40e_ptp_set_led_hw(hw, I40E_LED2_0, pins->led2_0);
	i40e_ptp_set_led_hw(hw, I40E_LED2_1, pins->led2_1);
	i40e_ptp_set_led_hw(hw, I40E_LED3_0, pins->led3_0);
	i40e_ptp_set_led_hw(hw, I40E_LED3_1, pins->led3_1);

	dev_info(&pf->pdev->dev,
		 "PTP configuration set to: SDP3_2: %s,  SDP3_3: %s,  GPIO_4: %s.\n",
		 i40e_ptp_gpio_pin_state2str[pins->sdp3_2],
		 i40e_ptp_gpio_pin_state2str[pins->sdp3_3],
		 i40e_ptp_gpio_pin_state2str[pins->gpio_4]);
}

/**
 * i40e_ptp_set_pins - set PTP pins in HW
 * @pf: Board private structure
 * @pins: PTP pins to be applied
 *
 * Validate and set PTP pins in HW for specific PF.
 * Return 0 on success or negative value on error.
 **/
static int i40e_ptp_set_pins(struct i40e_pf *pf,
			     struct i40e_ptp_pins_settings *pins)
{
	enum i40e_can_set_pins pin_caps = i40e_can_set_pins(pf);
	int i = 0;

	if (pin_caps == CANT_DO_PINS)
		return -EOPNOTSUPP;
	else if (pin_caps == CAN_DO_PINS)
		return 0;

	if (pins->sdp3_2 == invalid)
		pins->sdp3_2 = pf->ptp_pins->sdp3_2;
	if (pins->sdp3_3 == invalid)
		pins->sdp3_3 = pf->ptp_pins->sdp3_3;
	if (pins->gpio_4 == invalid)
		pins->gpio_4 = pf->ptp_pins->gpio_4;
	while (i40e_ptp_pin_led_allowed_states[i].sdp3_2 != end) {
		if (pins->sdp3_2 == i40e_ptp_pin_led_allowed_states[i].sdp3_2 &&
		    pins->sdp3_3 == i40e_ptp_pin_led_allowed_states[i].sdp3_3 &&
		    pins->gpio_4 == i40e_ptp_pin_led_allowed_states[i].gpio_4) {
			pins->led2_0 =
				i40e_ptp_pin_led_allowed_states[i].led2_0;
			pins->led2_1 =
				i40e_ptp_pin_led_allowed_states[i].led2_1;
			pins->led3_0 =
				i40e_ptp_pin_led_allowed_states[i].led3_0;
			pins->led3_1 =
				i40e_ptp_pin_led_allowed_states[i].led3_1;
			break;
		}
		i++;
	}
	if (i40e_ptp_pin_led_allowed_states[i].sdp3_2 == end) {
		dev_warn(&pf->pdev->dev,
			 "Unsupported PTP pin configuration: SDP3_2: %s,  SDP3_3: %s,  GPIO_4: %s.\n",
			 i40e_ptp_gpio_pin_state2str[pins->sdp3_2],
			 i40e_ptp_gpio_pin_state2str[pins->sdp3_3],
			 i40e_ptp_gpio_pin_state2str[pins->gpio_4]);

		return -EPERM;
	}
	memcpy(pf->ptp_pins, pins, sizeof(*pins));
	i40e_ptp_set_pins_hw(pf);
	i40_ptp_reset_timing_events(pf);

	return 0;
}

/**
 * i40e_ptp_alloc_pins - allocate PTP pins structure
 * @pf: Board private structure
 *
 * allocate PTP pins structure
 **/
int i40e_ptp_alloc_pins(struct i40e_pf *pf)
{
	if (!i40e_is_ptp_pin_dev(&pf->hw))
		return 0;

	pf->ptp_pins =
		kzalloc(sizeof(struct i40e_ptp_pins_settings), GFP_KERNEL);

	if (!pf->ptp_pins) {
		dev_warn(&pf->pdev->dev, "Cannot allocate memory for PTP pins structure.\n");
		return -ENOMEM;
	}

	pf->ptp_pins->sdp3_2 = off;
	pf->ptp_pins->sdp3_3 = off;
	pf->ptp_pins->gpio_4 = off;
	pf->ptp_pins->led2_0 = high;
	pf->ptp_pins->led2_1 = high;
	pf->ptp_pins->led3_0 = high;
	pf->ptp_pins->led3_1 = high;

	/* Use PF0 to set pins in HW. Return success for user space tools */
	if (pf->hw.pf_id)
		return 0;

	i40e_ptp_init_leds_hw(&pf->hw);
	i40e_ptp_set_pins_hw(pf);

	return 0;
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
				       struct kernel_hwtstamp_config *config)
{
	struct i40e_hw *hw = &pf->hw;
	u32 tsyntype, regval;

	/* Selects external trigger to cause event */
	regval = rd32(hw, I40E_PRTTSYN_AUX_0(0));
	/* Bit 17:16 is EVNTLVL, 01B rising edge */
	regval &= 0;
	regval |= (1 << I40E_PRTTSYN_AUX_0_EVNTLVL_SHIFT);
	/* regval: 0001 0000 0000 0000 0000 */
	wr32(hw, I40E_PRTTSYN_AUX_0(0), regval);

	/* Enabel interrupts */
	regval = rd32(hw, I40E_PRTTSYN_CTL0);
	regval |= 1 << I40E_PRTTSYN_CTL0_EVENT_INT_ENA_SHIFT;
	wr32(hw, I40E_PRTTSYN_CTL0, regval);

	INIT_WORK(&pf->ptp_extts0_work, i40e_ptp_extts0_work);

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
		if (!test_bit(I40E_HW_CAP_PTP_L4, pf->hw.caps))
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
		if (!test_bit(I40E_HW_CAP_PTP_L4, pf->hw.caps))
			return -ERANGE;
		fallthrough;
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
		pf->ptp_rx = true;
		tsyntype = I40E_PRTTSYN_CTL1_V2MESSTYPE0_MASK |
			   I40E_PRTTSYN_CTL1_TSYNTYPE_V2;
		if (test_bit(I40E_HW_CAP_PTP_L4, pf->hw.caps)) {
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
 * i40e_ptp_hwtstamp_set - interface to control the HW timestamping
 * @netdev: Network device structure
 * @config: Timestamping configuration structure
 * @extack: Netlink extended ack structure for error reporting
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
int i40e_ptp_hwtstamp_set(struct net_device *netdev,
			  struct kernel_hwtstamp_config *config,
			  struct netlink_ext_ack *extack)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_pf *pf = np->vsi->back;
	int err;

	if (!test_bit(I40E_FLAG_PTP_ENA, pf->flags))
		return -EOPNOTSUPP;

	err = i40e_ptp_set_timestamp_mode(pf, config);
	if (err)
		return err;

	/* save these settings for future reference */
	pf->tstamp_config = *config;

	return 0;
}

/**
 * i40e_init_pin_config - initialize pins.
 * @pf: private board structure
 *
 * Initialize pins for external clock source.
 * Return 0 on success or error code on failure.
 **/
static int i40e_init_pin_config(struct i40e_pf *pf)
{
	int i;

	pf->ptp_caps.n_pins = 3;
	pf->ptp_caps.n_ext_ts = 2;
	pf->ptp_caps.pps = 1;
	pf->ptp_caps.n_per_out = 2;

	pf->ptp_caps.pin_config = kcalloc(pf->ptp_caps.n_pins,
					  sizeof(*pf->ptp_caps.pin_config),
					  GFP_KERNEL);
	if (!pf->ptp_caps.pin_config)
		return -ENOMEM;

	for (i = 0; i < pf->ptp_caps.n_pins; i++) {
		snprintf(pf->ptp_caps.pin_config[i].name,
			 sizeof(pf->ptp_caps.pin_config[i].name),
			 "%s", sdp_desc[i].name);
		pf->ptp_caps.pin_config[i].index = sdp_desc[i].index;
		pf->ptp_caps.pin_config[i].func = PTP_PF_NONE;
		pf->ptp_caps.pin_config[i].chan = sdp_desc[i].chan;
	}

	pf->ptp_caps.verify = i40e_ptp_verify;
	pf->ptp_caps.enable = i40e_ptp_feature_enable;

	pf->ptp_caps.pps = 1;

	return 0;
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

	strscpy(pf->ptp_caps.name, i40e_driver_name,
		sizeof(pf->ptp_caps.name) - 1);
	pf->ptp_caps.owner = THIS_MODULE;
	pf->ptp_caps.max_adj = 999999999;
	pf->ptp_caps.adjfine = i40e_ptp_adjfine;
	pf->ptp_caps.adjtime = i40e_ptp_adjtime;
	pf->ptp_caps.gettimex64 = i40e_ptp_gettimex;
	pf->ptp_caps.settime64 = i40e_ptp_settime;
	if (i40e_is_ptp_pin_dev(&pf->hw)) {
		int err = i40e_init_pin_config(pf);

		if (err)
			return err;
	}

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

	/* Set the previous "reset" time to the current Kernel clock time */
	ktime_get_real_ts64(&pf->ptp_prev_hw_time);
	pf->ptp_reset_start = ktime_get();

	return 0;
}

/**
 * i40e_ptp_save_hw_time - Save the current PTP time as ptp_prev_hw_time
 * @pf: Board private structure
 *
 * Read the current PTP time and save it into pf->ptp_prev_hw_time. This should
 * be called at the end of preparing to reset, just before hardware reset
 * occurs, in order to preserve the PTP time as close as possible across
 * resets.
 */
void i40e_ptp_save_hw_time(struct i40e_pf *pf)
{
	/* don't try to access the PTP clock if it's not enabled */
	if (!test_bit(I40E_FLAG_PTP_ENA, pf->flags))
		return;

	i40e_ptp_gettimex(&pf->ptp_caps, &pf->ptp_prev_hw_time, NULL);
	/* Get a monotonic starting time for this reset */
	pf->ptp_reset_start = ktime_get();
}

/**
 * i40e_ptp_restore_hw_time - Restore the ptp_prev_hw_time + delta to PTP regs
 * @pf: Board private structure
 *
 * Restore the PTP hardware clock registers. We previously cached the PTP
 * hardware time as pf->ptp_prev_hw_time. To be as accurate as possible,
 * update this value based on the time delta since the time was saved, using
 * CLOCK_MONOTONIC (via ktime_get()) to calculate the time difference.
 *
 * This ensures that the hardware clock is restored to nearly what it should
 * have been if a reset had not occurred.
 */
void i40e_ptp_restore_hw_time(struct i40e_pf *pf)
{
	ktime_t delta = ktime_sub(ktime_get(), pf->ptp_reset_start);

	/* Update the previous HW time with the ktime delta */
	timespec64_add_ns(&pf->ptp_prev_hw_time, ktime_to_ns(delta));

	/* Restore the hardware clock registers */
	i40e_ptp_settime(&pf->ptp_caps, &pf->ptp_prev_hw_time);
}

/**
 * i40e_ptp_init - Initialize the 1588 support after device probe or reset
 * @pf: Board private structure
 *
 * This function sets device up for 1588 support. The first time it is run, it
 * will create a PHC clock device. It does not create a clock device if one
 * already exists. It also reconfigures the device after a reset.
 *
 * The first time a clock is created, i40e_ptp_create_clock will set
 * pf->ptp_prev_hw_time to the current system time. During resets, it is
 * expected that this timespec will be set to the last known PTP clock time,
 * in order to preserve the clock time as close as possible across a reset.
 **/
void i40e_ptp_init(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi = i40e_pf_get_main_vsi(pf);
	struct net_device *netdev = vsi->netdev;
	struct i40e_hw *hw = &pf->hw;
	u32 pf_id;
	long err;

	/* Only one PF is assigned to control 1588 logic per port. Do not
	 * enable any support for PFs not assigned via PRTTSYN_CTL0.PF_ID
	 */
	pf_id = FIELD_GET(I40E_PRTTSYN_CTL0_PF_ID_MASK,
			  rd32(hw, I40E_PRTTSYN_CTL0));
	if (hw->pf_id != pf_id) {
		clear_bit(I40E_FLAG_PTP_ENA, pf->flags);
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
		u32 regval;

		if (pf->hw.debug_mask & I40E_DEBUG_LAN)
			dev_info(&pf->pdev->dev, "PHC enabled\n");
		set_bit(I40E_FLAG_PTP_ENA, pf->flags);

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

		/* Restore the clock time based on last known value */
		i40e_ptp_restore_hw_time(pf);
	}

	i40e_ptp_set_1pps_signal_hw(pf);
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
	struct i40e_vsi *main_vsi = i40e_pf_get_main_vsi(pf);
	struct i40e_hw *hw = &pf->hw;
	u32 regval;

	clear_bit(I40E_FLAG_PTP_ENA, pf->flags);
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
			 main_vsi->netdev->name);
	}

	if (i40e_is_ptp_pin_dev(&pf->hw)) {
		i40e_ptp_set_pin_hw(hw, I40E_SDP3_2, off);
		i40e_ptp_set_pin_hw(hw, I40E_SDP3_3, off);
		i40e_ptp_set_pin_hw(hw, I40E_GPIO_4, off);
	}

	regval = rd32(hw, I40E_PRTTSYN_AUX_0(0));
	regval &= ~I40E_PRTTSYN_AUX_0_PTPFLAG_MASK;
	wr32(hw, I40E_PRTTSYN_AUX_0(0), regval);

	/* Disable interrupts */
	regval = rd32(hw, I40E_PRTTSYN_CTL0);
	regval &= ~I40E_PRTTSYN_CTL0_EVENT_INT_ENA_MASK;
	wr32(hw, I40E_PRTTSYN_CTL0, regval);

	i40e_ptp_free_pins(pf);
}
