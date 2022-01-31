// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/sched/types.h>

#include <media/cec-pin.h>
#include "cec-pin-priv.h"

/* All timings are in microseconds */

/* start bit timings */
#define CEC_TIM_START_BIT_LOW		3700
#define CEC_TIM_START_BIT_LOW_MIN	3500
#define CEC_TIM_START_BIT_LOW_MAX	3900
#define CEC_TIM_START_BIT_TOTAL		4500
#define CEC_TIM_START_BIT_TOTAL_MIN	4300
#define CEC_TIM_START_BIT_TOTAL_MAX	4700

/* data bit timings */
#define CEC_TIM_DATA_BIT_0_LOW		1500
#define CEC_TIM_DATA_BIT_0_LOW_MIN	1300
#define CEC_TIM_DATA_BIT_0_LOW_MAX	1700
#define CEC_TIM_DATA_BIT_1_LOW		600
#define CEC_TIM_DATA_BIT_1_LOW_MIN	400
#define CEC_TIM_DATA_BIT_1_LOW_MAX	800
#define CEC_TIM_DATA_BIT_TOTAL		2400
#define CEC_TIM_DATA_BIT_TOTAL_MIN	2050
#define CEC_TIM_DATA_BIT_TOTAL_MAX	2750
/* earliest safe time to sample the bit state */
#define CEC_TIM_DATA_BIT_SAMPLE		850
/* earliest time the bit is back to 1 (T7 + 50) */
#define CEC_TIM_DATA_BIT_HIGH		1750

/* when idle, sample once per millisecond */
#define CEC_TIM_IDLE_SAMPLE		1000
/* when processing the start bit, sample twice per millisecond */
#define CEC_TIM_START_BIT_SAMPLE	500
/* when polling for a state change, sample once every 50 microseconds */
#define CEC_TIM_SAMPLE			50

#define CEC_TIM_LOW_DRIVE_ERROR		(1.5 * CEC_TIM_DATA_BIT_TOTAL)

/*
 * Total data bit time that is too short/long for a valid bit,
 * used for error injection.
 */
#define CEC_TIM_DATA_BIT_TOTAL_SHORT	1800
#define CEC_TIM_DATA_BIT_TOTAL_LONG	2900

/*
 * Total start bit time that is too short/long for a valid bit,
 * used for error injection.
 */
#define CEC_TIM_START_BIT_TOTAL_SHORT	4100
#define CEC_TIM_START_BIT_TOTAL_LONG	5000

/* Data bits are 0-7, EOM is bit 8 and ACK is bit 9 */
#define EOM_BIT				8
#define ACK_BIT				9

struct cec_state {
	const char * const name;
	unsigned int usecs;
};

static const struct cec_state states[CEC_PIN_STATES] = {
	{ "Off",		   0 },
	{ "Idle",		   CEC_TIM_IDLE_SAMPLE },
	{ "Tx Wait",		   CEC_TIM_SAMPLE },
	{ "Tx Wait for High",	   CEC_TIM_IDLE_SAMPLE },
	{ "Tx Start Bit Low",	   CEC_TIM_START_BIT_LOW },
	{ "Tx Start Bit High",	   CEC_TIM_START_BIT_TOTAL - CEC_TIM_START_BIT_LOW },
	{ "Tx Start Bit High Short", CEC_TIM_START_BIT_TOTAL_SHORT - CEC_TIM_START_BIT_LOW },
	{ "Tx Start Bit High Long", CEC_TIM_START_BIT_TOTAL_LONG - CEC_TIM_START_BIT_LOW },
	{ "Tx Start Bit Low Custom", 0 },
	{ "Tx Start Bit High Custom", 0 },
	{ "Tx Data 0 Low",	   CEC_TIM_DATA_BIT_0_LOW },
	{ "Tx Data 0 High",	   CEC_TIM_DATA_BIT_TOTAL - CEC_TIM_DATA_BIT_0_LOW },
	{ "Tx Data 0 High Short",  CEC_TIM_DATA_BIT_TOTAL_SHORT - CEC_TIM_DATA_BIT_0_LOW },
	{ "Tx Data 0 High Long",   CEC_TIM_DATA_BIT_TOTAL_LONG - CEC_TIM_DATA_BIT_0_LOW },
	{ "Tx Data 1 Low",	   CEC_TIM_DATA_BIT_1_LOW },
	{ "Tx Data 1 High",	   CEC_TIM_DATA_BIT_TOTAL - CEC_TIM_DATA_BIT_1_LOW },
	{ "Tx Data 1 High Short",  CEC_TIM_DATA_BIT_TOTAL_SHORT - CEC_TIM_DATA_BIT_1_LOW },
	{ "Tx Data 1 High Long",   CEC_TIM_DATA_BIT_TOTAL_LONG - CEC_TIM_DATA_BIT_1_LOW },
	{ "Tx Data 1 High Pre Sample", CEC_TIM_DATA_BIT_SAMPLE - CEC_TIM_DATA_BIT_1_LOW },
	{ "Tx Data 1 High Post Sample", CEC_TIM_DATA_BIT_TOTAL - CEC_TIM_DATA_BIT_SAMPLE },
	{ "Tx Data 1 High Post Sample Short", CEC_TIM_DATA_BIT_TOTAL_SHORT - CEC_TIM_DATA_BIT_SAMPLE },
	{ "Tx Data 1 High Post Sample Long", CEC_TIM_DATA_BIT_TOTAL_LONG - CEC_TIM_DATA_BIT_SAMPLE },
	{ "Tx Data Bit Low Custom", 0 },
	{ "Tx Data Bit High Custom", 0 },
	{ "Tx Pulse Low Custom",   0 },
	{ "Tx Pulse High Custom",  0 },
	{ "Tx Low Drive",	   CEC_TIM_LOW_DRIVE_ERROR },
	{ "Rx Start Bit Low",	   CEC_TIM_SAMPLE },
	{ "Rx Start Bit High",	   CEC_TIM_SAMPLE },
	{ "Rx Data Sample",	   CEC_TIM_DATA_BIT_SAMPLE },
	{ "Rx Data Post Sample",   CEC_TIM_DATA_BIT_HIGH - CEC_TIM_DATA_BIT_SAMPLE },
	{ "Rx Data Wait for Low",  CEC_TIM_SAMPLE },
	{ "Rx Ack Low",		   CEC_TIM_DATA_BIT_0_LOW },
	{ "Rx Ack Low Post",	   CEC_TIM_DATA_BIT_HIGH - CEC_TIM_DATA_BIT_0_LOW },
	{ "Rx Ack High Post",	   CEC_TIM_DATA_BIT_HIGH },
	{ "Rx Ack Finish",	   CEC_TIM_DATA_BIT_TOTAL_MIN - CEC_TIM_DATA_BIT_HIGH },
	{ "Rx Low Drive",	   CEC_TIM_LOW_DRIVE_ERROR },
	{ "Rx Irq",		   0 },
};

static void cec_pin_update(struct cec_pin *pin, bool v, bool force)
{
	if (!force && v == pin->adap->cec_pin_is_high)
		return;

	pin->adap->cec_pin_is_high = v;
	if (atomic_read(&pin->work_pin_num_events) < CEC_NUM_PIN_EVENTS) {
		u8 ev = v;

		if (pin->work_pin_events_dropped) {
			pin->work_pin_events_dropped = false;
			ev |= CEC_PIN_EVENT_FL_DROPPED;
		}
		pin->work_pin_events[pin->work_pin_events_wr] = ev;
		pin->work_pin_ts[pin->work_pin_events_wr] = ktime_get();
		pin->work_pin_events_wr =
			(pin->work_pin_events_wr + 1) % CEC_NUM_PIN_EVENTS;
		atomic_inc(&pin->work_pin_num_events);
	} else {
		pin->work_pin_events_dropped = true;
		pin->work_pin_events_dropped_cnt++;
	}
	wake_up_interruptible(&pin->kthread_waitq);
}

static bool cec_pin_read(struct cec_pin *pin)
{
	bool v = pin->ops->read(pin->adap);

	cec_pin_update(pin, v, false);
	return v;
}

static void cec_pin_low(struct cec_pin *pin)
{
	pin->ops->low(pin->adap);
	cec_pin_update(pin, false, false);
}

static bool cec_pin_high(struct cec_pin *pin)
{
	pin->ops->high(pin->adap);
	return cec_pin_read(pin);
}

static bool rx_error_inj(struct cec_pin *pin, unsigned int mode_offset,
			 int arg_idx, u8 *arg)
{
#ifdef CONFIG_CEC_PIN_ERROR_INJ
	u16 cmd = cec_pin_rx_error_inj(pin);
	u64 e = pin->error_inj[cmd];
	unsigned int mode = (e >> mode_offset) & CEC_ERROR_INJ_MODE_MASK;

	if (arg_idx >= 0) {
		u8 pos = pin->error_inj_args[cmd][arg_idx];

		if (arg)
			*arg = pos;
		else if (pos != pin->rx_bit)
			return false;
	}

	switch (mode) {
	case CEC_ERROR_INJ_MODE_ONCE:
		pin->error_inj[cmd] &=
			~(CEC_ERROR_INJ_MODE_MASK << mode_offset);
		return true;
	case CEC_ERROR_INJ_MODE_ALWAYS:
		return true;
	case CEC_ERROR_INJ_MODE_TOGGLE:
		return pin->rx_toggle;
	default:
		return false;
	}
#else
	return false;
#endif
}

static bool rx_nack(struct cec_pin *pin)
{
	return rx_error_inj(pin, CEC_ERROR_INJ_RX_NACK_OFFSET, -1, NULL);
}

static bool rx_low_drive(struct cec_pin *pin)
{
	return rx_error_inj(pin, CEC_ERROR_INJ_RX_LOW_DRIVE_OFFSET,
			    CEC_ERROR_INJ_RX_LOW_DRIVE_ARG_IDX, NULL);
}

static bool rx_add_byte(struct cec_pin *pin)
{
	return rx_error_inj(pin, CEC_ERROR_INJ_RX_ADD_BYTE_OFFSET, -1, NULL);
}

static bool rx_remove_byte(struct cec_pin *pin)
{
	return rx_error_inj(pin, CEC_ERROR_INJ_RX_REMOVE_BYTE_OFFSET, -1, NULL);
}

static bool rx_arb_lost(struct cec_pin *pin, u8 *poll)
{
	return pin->tx_msg.len == 0 &&
		rx_error_inj(pin, CEC_ERROR_INJ_RX_ARB_LOST_OFFSET,
			     CEC_ERROR_INJ_RX_ARB_LOST_ARG_IDX, poll);
}

static bool tx_error_inj(struct cec_pin *pin, unsigned int mode_offset,
			 int arg_idx, u8 *arg)
{
#ifdef CONFIG_CEC_PIN_ERROR_INJ
	u16 cmd = cec_pin_tx_error_inj(pin);
	u64 e = pin->error_inj[cmd];
	unsigned int mode = (e >> mode_offset) & CEC_ERROR_INJ_MODE_MASK;

	if (arg_idx >= 0) {
		u8 pos = pin->error_inj_args[cmd][arg_idx];

		if (arg)
			*arg = pos;
		else if (pos != pin->tx_bit)
			return false;
	}

	switch (mode) {
	case CEC_ERROR_INJ_MODE_ONCE:
		pin->error_inj[cmd] &=
			~(CEC_ERROR_INJ_MODE_MASK << mode_offset);
		return true;
	case CEC_ERROR_INJ_MODE_ALWAYS:
		return true;
	case CEC_ERROR_INJ_MODE_TOGGLE:
		return pin->tx_toggle;
	default:
		return false;
	}
#else
	return false;
#endif
}

static bool tx_no_eom(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_NO_EOM_OFFSET, -1, NULL);
}

static bool tx_early_eom(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_EARLY_EOM_OFFSET, -1, NULL);
}

static bool tx_short_bit(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_SHORT_BIT_OFFSET,
			    CEC_ERROR_INJ_TX_SHORT_BIT_ARG_IDX, NULL);
}

static bool tx_long_bit(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_LONG_BIT_OFFSET,
			    CEC_ERROR_INJ_TX_LONG_BIT_ARG_IDX, NULL);
}

static bool tx_custom_bit(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_CUSTOM_BIT_OFFSET,
			    CEC_ERROR_INJ_TX_CUSTOM_BIT_ARG_IDX, NULL);
}

static bool tx_short_start(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_SHORT_START_OFFSET, -1, NULL);
}

static bool tx_long_start(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_LONG_START_OFFSET, -1, NULL);
}

static bool tx_custom_start(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_CUSTOM_START_OFFSET,
			    -1, NULL);
}

static bool tx_last_bit(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_LAST_BIT_OFFSET,
			    CEC_ERROR_INJ_TX_LAST_BIT_ARG_IDX, NULL);
}

static u8 tx_add_bytes(struct cec_pin *pin)
{
	u8 bytes;

	if (tx_error_inj(pin, CEC_ERROR_INJ_TX_ADD_BYTES_OFFSET,
			 CEC_ERROR_INJ_TX_ADD_BYTES_ARG_IDX, &bytes))
		return bytes;
	return 0;
}

static bool tx_remove_byte(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_REMOVE_BYTE_OFFSET, -1, NULL);
}

static bool tx_low_drive(struct cec_pin *pin)
{
	return tx_error_inj(pin, CEC_ERROR_INJ_TX_LOW_DRIVE_OFFSET,
			    CEC_ERROR_INJ_TX_LOW_DRIVE_ARG_IDX, NULL);
}

static void cec_pin_to_idle(struct cec_pin *pin)
{
	/*
	 * Reset all status fields, release the bus and
	 * go to idle state.
	 */
	pin->rx_bit = pin->tx_bit = 0;
	pin->rx_msg.len = 0;
	memset(pin->rx_msg.msg, 0, sizeof(pin->rx_msg.msg));
	pin->ts = ns_to_ktime(0);
	pin->tx_generated_poll = false;
	pin->tx_post_eom = false;
	if (pin->state >= CEC_ST_TX_WAIT &&
	    pin->state <= CEC_ST_TX_LOW_DRIVE)
		pin->tx_toggle ^= 1;
	if (pin->state >= CEC_ST_RX_START_BIT_LOW &&
	    pin->state <= CEC_ST_RX_LOW_DRIVE)
		pin->rx_toggle ^= 1;
	pin->state = CEC_ST_IDLE;
}

/*
 * Handle Transmit-related states
 *
 * Basic state changes when transmitting:
 *
 * Idle -> Tx Wait (waiting for the end of signal free time) ->
 *	Tx Start Bit Low -> Tx Start Bit High ->
 *
 *   Regular data bits + EOM:
 *	Tx Data 0 Low -> Tx Data 0 High ->
 *   or:
 *	Tx Data 1 Low -> Tx Data 1 High ->
 *
 *   First 4 data bits or Ack bit:
 *	Tx Data 0 Low -> Tx Data 0 High ->
 *   or:
 *	Tx Data 1 Low -> Tx Data 1 High -> Tx Data 1 Pre Sample ->
 *		Tx Data 1 Post Sample ->
 *
 *   After the last Ack go to Idle.
 *
 * If it detects a Low Drive condition then:
 *	Tx Wait For High -> Idle
 *
 * If it loses arbitration, then it switches to state Rx Data Post Sample.
 */
static void cec_pin_tx_states(struct cec_pin *pin, ktime_t ts)
{
	bool v;
	bool is_ack_bit, ack;

	switch (pin->state) {
	case CEC_ST_TX_WAIT_FOR_HIGH:
		if (cec_pin_read(pin))
			cec_pin_to_idle(pin);
		break;

	case CEC_ST_TX_START_BIT_LOW:
		if (tx_short_start(pin)) {
			/*
			 * Error Injection: send an invalid (too short)
			 * start pulse.
			 */
			pin->state = CEC_ST_TX_START_BIT_HIGH_SHORT;
		} else if (tx_long_start(pin)) {
			/*
			 * Error Injection: send an invalid (too long)
			 * start pulse.
			 */
			pin->state = CEC_ST_TX_START_BIT_HIGH_LONG;
		} else {
			pin->state = CEC_ST_TX_START_BIT_HIGH;
		}
		/* Generate start bit */
		cec_pin_high(pin);
		break;

	case CEC_ST_TX_START_BIT_LOW_CUSTOM:
		pin->state = CEC_ST_TX_START_BIT_HIGH_CUSTOM;
		/* Generate start bit */
		cec_pin_high(pin);
		break;

	case CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE:
	case CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_SHORT:
	case CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_LONG:
		if (pin->tx_nacked) {
			cec_pin_to_idle(pin);
			pin->tx_msg.len = 0;
			if (pin->tx_generated_poll)
				break;
			pin->work_tx_ts = ts;
			pin->work_tx_status = CEC_TX_STATUS_NACK;
			wake_up_interruptible(&pin->kthread_waitq);
			break;
		}
		fallthrough;
	case CEC_ST_TX_DATA_BIT_0_HIGH:
	case CEC_ST_TX_DATA_BIT_0_HIGH_SHORT:
	case CEC_ST_TX_DATA_BIT_0_HIGH_LONG:
	case CEC_ST_TX_DATA_BIT_1_HIGH:
	case CEC_ST_TX_DATA_BIT_1_HIGH_SHORT:
	case CEC_ST_TX_DATA_BIT_1_HIGH_LONG:
		/*
		 * If the read value is 1, then all is OK, otherwise we have a
		 * low drive condition.
		 *
		 * Special case: when we generate a poll message due to an
		 * Arbitration Lost error injection, then ignore this since
		 * the pin can actually be low in that case.
		 */
		if (!cec_pin_read(pin) && !pin->tx_generated_poll) {
			/*
			 * It's 0, so someone detected an error and pulled the
			 * line low for 1.5 times the nominal bit period.
			 */
			pin->tx_msg.len = 0;
			pin->state = CEC_ST_TX_WAIT_FOR_HIGH;
			pin->work_tx_ts = ts;
			pin->work_tx_status = CEC_TX_STATUS_LOW_DRIVE;
			pin->tx_low_drive_cnt++;
			wake_up_interruptible(&pin->kthread_waitq);
			break;
		}
		fallthrough;
	case CEC_ST_TX_DATA_BIT_HIGH_CUSTOM:
		if (tx_last_bit(pin)) {
			/* Error Injection: just stop sending after this bit */
			cec_pin_to_idle(pin);
			pin->tx_msg.len = 0;
			if (pin->tx_generated_poll)
				break;
			pin->work_tx_ts = ts;
			pin->work_tx_status = CEC_TX_STATUS_OK;
			wake_up_interruptible(&pin->kthread_waitq);
			break;
		}
		pin->tx_bit++;
		fallthrough;
	case CEC_ST_TX_START_BIT_HIGH:
	case CEC_ST_TX_START_BIT_HIGH_SHORT:
	case CEC_ST_TX_START_BIT_HIGH_LONG:
	case CEC_ST_TX_START_BIT_HIGH_CUSTOM:
		if (tx_low_drive(pin)) {
			/* Error injection: go to low drive */
			cec_pin_low(pin);
			pin->state = CEC_ST_TX_LOW_DRIVE;
			pin->tx_msg.len = 0;
			if (pin->tx_generated_poll)
				break;
			pin->work_tx_ts = ts;
			pin->work_tx_status = CEC_TX_STATUS_LOW_DRIVE;
			pin->tx_low_drive_cnt++;
			wake_up_interruptible(&pin->kthread_waitq);
			break;
		}
		if (pin->tx_bit / 10 >= pin->tx_msg.len + pin->tx_extra_bytes) {
			cec_pin_to_idle(pin);
			pin->tx_msg.len = 0;
			if (pin->tx_generated_poll)
				break;
			pin->work_tx_ts = ts;
			pin->work_tx_status = CEC_TX_STATUS_OK;
			wake_up_interruptible(&pin->kthread_waitq);
			break;
		}

		switch (pin->tx_bit % 10) {
		default: {
			/*
			 * In the CEC_ERROR_INJ_TX_ADD_BYTES case we transmit
			 * extra bytes, so pin->tx_bit / 10 can become >= 16.
			 * Generate bit values for those extra bytes instead
			 * of reading them from the transmit buffer.
			 */
			unsigned int idx = (pin->tx_bit / 10);
			u8 val = idx;

			if (idx < pin->tx_msg.len)
				val = pin->tx_msg.msg[idx];
			v = val & (1 << (7 - (pin->tx_bit % 10)));

			pin->state = v ? CEC_ST_TX_DATA_BIT_1_LOW :
					 CEC_ST_TX_DATA_BIT_0_LOW;
			break;
		}
		case EOM_BIT: {
			unsigned int tot_len = pin->tx_msg.len +
					       pin->tx_extra_bytes;
			unsigned int tx_byte_idx = pin->tx_bit / 10;

			v = !pin->tx_post_eom && tx_byte_idx == tot_len - 1;
			if (tot_len > 1 && tx_byte_idx == tot_len - 2 &&
			    tx_early_eom(pin)) {
				/* Error injection: set EOM one byte early */
				v = true;
				pin->tx_post_eom = true;
			} else if (v && tx_no_eom(pin)) {
				/* Error injection: no EOM */
				v = false;
			}
			pin->state = v ? CEC_ST_TX_DATA_BIT_1_LOW :
					 CEC_ST_TX_DATA_BIT_0_LOW;
			break;
		}
		case ACK_BIT:
			pin->state = CEC_ST_TX_DATA_BIT_1_LOW;
			break;
		}
		if (tx_custom_bit(pin))
			pin->state = CEC_ST_TX_DATA_BIT_LOW_CUSTOM;
		cec_pin_low(pin);
		break;

	case CEC_ST_TX_DATA_BIT_0_LOW:
	case CEC_ST_TX_DATA_BIT_1_LOW:
		v = pin->state == CEC_ST_TX_DATA_BIT_1_LOW;
		is_ack_bit = pin->tx_bit % 10 == ACK_BIT;
		if (v && (pin->tx_bit < 4 || is_ack_bit)) {
			pin->state = CEC_ST_TX_DATA_BIT_1_HIGH_PRE_SAMPLE;
		} else if (!is_ack_bit && tx_short_bit(pin)) {
			/* Error Injection: send an invalid (too short) bit */
			pin->state = v ? CEC_ST_TX_DATA_BIT_1_HIGH_SHORT :
					 CEC_ST_TX_DATA_BIT_0_HIGH_SHORT;
		} else if (!is_ack_bit && tx_long_bit(pin)) {
			/* Error Injection: send an invalid (too long) bit */
			pin->state = v ? CEC_ST_TX_DATA_BIT_1_HIGH_LONG :
					 CEC_ST_TX_DATA_BIT_0_HIGH_LONG;
		} else {
			pin->state = v ? CEC_ST_TX_DATA_BIT_1_HIGH :
					 CEC_ST_TX_DATA_BIT_0_HIGH;
		}
		cec_pin_high(pin);
		break;

	case CEC_ST_TX_DATA_BIT_LOW_CUSTOM:
		pin->state = CEC_ST_TX_DATA_BIT_HIGH_CUSTOM;
		cec_pin_high(pin);
		break;

	case CEC_ST_TX_DATA_BIT_1_HIGH_PRE_SAMPLE:
		/* Read the CEC value at the sample time */
		v = cec_pin_read(pin);
		is_ack_bit = pin->tx_bit % 10 == ACK_BIT;
		/*
		 * If v == 0 and we're within the first 4 bits
		 * of the initiator, then someone else started
		 * transmitting and we lost the arbitration
		 * (i.e. the logical address of the other
		 * transmitter has more leading 0 bits in the
		 * initiator).
		 */
		if (!v && !is_ack_bit && !pin->tx_generated_poll) {
			pin->tx_msg.len = 0;
			pin->work_tx_ts = ts;
			pin->work_tx_status = CEC_TX_STATUS_ARB_LOST;
			wake_up_interruptible(&pin->kthread_waitq);
			pin->rx_bit = pin->tx_bit;
			pin->tx_bit = 0;
			memset(pin->rx_msg.msg, 0, sizeof(pin->rx_msg.msg));
			pin->rx_msg.msg[0] = pin->tx_msg.msg[0];
			pin->rx_msg.msg[0] &= (0xff << (8 - pin->rx_bit));
			pin->rx_msg.len = 0;
			pin->ts = ktime_sub_us(ts, CEC_TIM_DATA_BIT_SAMPLE);
			pin->state = CEC_ST_RX_DATA_POST_SAMPLE;
			pin->rx_bit++;
			break;
		}
		pin->state = CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE;
		if (!is_ack_bit && tx_short_bit(pin)) {
			/* Error Injection: send an invalid (too short) bit */
			pin->state = CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_SHORT;
		} else if (!is_ack_bit && tx_long_bit(pin)) {
			/* Error Injection: send an invalid (too long) bit */
			pin->state = CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_LONG;
		}
		if (!is_ack_bit)
			break;
		/* Was the message ACKed? */
		ack = cec_msg_is_broadcast(&pin->tx_msg) ? v : !v;
		if (!ack && (!pin->tx_ignore_nack_until_eom ||
		    pin->tx_bit / 10 == pin->tx_msg.len - 1) &&
		    !pin->tx_post_eom) {
			/*
			 * Note: the CEC spec is ambiguous regarding
			 * what action to take when a NACK appears
			 * before the last byte of the payload was
			 * transmitted: either stop transmitting
			 * immediately, or wait until the last byte
			 * was transmitted.
			 *
			 * Most CEC implementations appear to stop
			 * immediately, and that's what we do here
			 * as well.
			 */
			pin->tx_nacked = true;
		}
		break;

	case CEC_ST_TX_PULSE_LOW_CUSTOM:
		cec_pin_high(pin);
		pin->state = CEC_ST_TX_PULSE_HIGH_CUSTOM;
		break;

	case CEC_ST_TX_PULSE_HIGH_CUSTOM:
		cec_pin_to_idle(pin);
		break;

	default:
		break;
	}
}

/*
 * Handle Receive-related states
 *
 * Basic state changes when receiving:
 *
 *	Rx Start Bit Low -> Rx Start Bit High ->
 *   Regular data bits + EOM:
 *	Rx Data Sample -> Rx Data Post Sample -> Rx Data High ->
 *   Ack bit 0:
 *	Rx Ack Low -> Rx Ack Low Post -> Rx Data High ->
 *   Ack bit 1:
 *	Rx Ack High Post -> Rx Data High ->
 *   Ack bit 0 && EOM:
 *	Rx Ack Low -> Rx Ack Low Post -> Rx Ack Finish -> Idle
 */
static void cec_pin_rx_states(struct cec_pin *pin, ktime_t ts)
{
	s32 delta;
	bool v;
	bool ack;
	bool bcast, for_us;
	u8 dest;
	u8 poll;

	switch (pin->state) {
	/* Receive states */
	case CEC_ST_RX_START_BIT_LOW:
		v = cec_pin_read(pin);
		if (!v)
			break;
		pin->state = CEC_ST_RX_START_BIT_HIGH;
		delta = ktime_us_delta(ts, pin->ts);
		/* Start bit low is too short, go back to idle */
		if (delta < CEC_TIM_START_BIT_LOW_MIN - CEC_TIM_IDLE_SAMPLE) {
			if (!pin->rx_start_bit_low_too_short_cnt++) {
				pin->rx_start_bit_low_too_short_ts = ktime_to_ns(pin->ts);
				pin->rx_start_bit_low_too_short_delta = delta;
			}
			cec_pin_to_idle(pin);
			break;
		}
		if (rx_arb_lost(pin, &poll)) {
			cec_msg_init(&pin->tx_msg, poll >> 4, poll & 0xf);
			pin->tx_generated_poll = true;
			pin->tx_extra_bytes = 0;
			pin->state = CEC_ST_TX_START_BIT_HIGH;
			pin->ts = ts;
		}
		break;

	case CEC_ST_RX_START_BIT_HIGH:
		v = cec_pin_read(pin);
		delta = ktime_us_delta(ts, pin->ts);
		/*
		 * Unfortunately the spec does not specify when to give up
		 * and go to idle. We just pick TOTAL_LONG.
		 */
		if (v && delta > CEC_TIM_START_BIT_TOTAL_LONG) {
			pin->rx_start_bit_too_long_cnt++;
			cec_pin_to_idle(pin);
			break;
		}
		if (v)
			break;
		/* Start bit is too short, go back to idle */
		if (delta < CEC_TIM_START_BIT_TOTAL_MIN - CEC_TIM_IDLE_SAMPLE) {
			if (!pin->rx_start_bit_too_short_cnt++) {
				pin->rx_start_bit_too_short_ts = ktime_to_ns(pin->ts);
				pin->rx_start_bit_too_short_delta = delta;
			}
			cec_pin_to_idle(pin);
			break;
		}
		if (rx_low_drive(pin)) {
			/* Error injection: go to low drive */
			cec_pin_low(pin);
			pin->state = CEC_ST_RX_LOW_DRIVE;
			pin->rx_low_drive_cnt++;
			break;
		}
		pin->state = CEC_ST_RX_DATA_SAMPLE;
		pin->ts = ts;
		pin->rx_eom = false;
		break;

	case CEC_ST_RX_DATA_SAMPLE:
		v = cec_pin_read(pin);
		pin->state = CEC_ST_RX_DATA_POST_SAMPLE;
		switch (pin->rx_bit % 10) {
		default:
			if (pin->rx_bit / 10 < CEC_MAX_MSG_SIZE)
				pin->rx_msg.msg[pin->rx_bit / 10] |=
					v << (7 - (pin->rx_bit % 10));
			break;
		case EOM_BIT:
			pin->rx_eom = v;
			pin->rx_msg.len = pin->rx_bit / 10 + 1;
			break;
		case ACK_BIT:
			break;
		}
		pin->rx_bit++;
		break;

	case CEC_ST_RX_DATA_POST_SAMPLE:
		pin->state = CEC_ST_RX_DATA_WAIT_FOR_LOW;
		break;

	case CEC_ST_RX_DATA_WAIT_FOR_LOW:
		v = cec_pin_read(pin);
		delta = ktime_us_delta(ts, pin->ts);
		/*
		 * Unfortunately the spec does not specify when to give up
		 * and go to idle. We just pick TOTAL_LONG.
		 */
		if (v && delta > CEC_TIM_DATA_BIT_TOTAL_LONG) {
			pin->rx_data_bit_too_long_cnt++;
			cec_pin_to_idle(pin);
			break;
		}
		if (v)
			break;

		if (rx_low_drive(pin)) {
			/* Error injection: go to low drive */
			cec_pin_low(pin);
			pin->state = CEC_ST_RX_LOW_DRIVE;
			pin->rx_low_drive_cnt++;
			break;
		}

		/*
		 * Go to low drive state when the total bit time is
		 * too short.
		 */
		if (delta < CEC_TIM_DATA_BIT_TOTAL_MIN) {
			if (!pin->rx_data_bit_too_short_cnt++) {
				pin->rx_data_bit_too_short_ts = ktime_to_ns(pin->ts);
				pin->rx_data_bit_too_short_delta = delta;
			}
			cec_pin_low(pin);
			pin->state = CEC_ST_RX_LOW_DRIVE;
			pin->rx_low_drive_cnt++;
			break;
		}
		pin->ts = ts;
		if (pin->rx_bit % 10 != 9) {
			pin->state = CEC_ST_RX_DATA_SAMPLE;
			break;
		}

		dest = cec_msg_destination(&pin->rx_msg);
		bcast = dest == CEC_LOG_ADDR_BROADCAST;
		/* for_us == broadcast or directed to us */
		for_us = bcast || (pin->la_mask & (1 << dest));
		/* ACK bit value */
		ack = bcast ? 1 : !for_us;

		if (for_us && rx_nack(pin)) {
			/* Error injection: toggle the ACK bit */
			ack = !ack;
		}

		if (ack) {
			/* No need to write to the bus, just wait */
			pin->state = CEC_ST_RX_ACK_HIGH_POST;
			break;
		}
		cec_pin_low(pin);
		pin->state = CEC_ST_RX_ACK_LOW;
		break;

	case CEC_ST_RX_ACK_LOW:
		cec_pin_high(pin);
		pin->state = CEC_ST_RX_ACK_LOW_POST;
		break;

	case CEC_ST_RX_ACK_LOW_POST:
	case CEC_ST_RX_ACK_HIGH_POST:
		v = cec_pin_read(pin);
		if (v && pin->rx_eom) {
			pin->work_rx_msg = pin->rx_msg;
			pin->work_rx_msg.rx_ts = ktime_to_ns(ts);
			wake_up_interruptible(&pin->kthread_waitq);
			pin->ts = ts;
			pin->state = CEC_ST_RX_ACK_FINISH;
			break;
		}
		pin->rx_bit++;
		pin->state = CEC_ST_RX_DATA_WAIT_FOR_LOW;
		break;

	case CEC_ST_RX_ACK_FINISH:
		cec_pin_to_idle(pin);
		break;

	default:
		break;
	}
}

/*
 * Main timer function
 *
 */
static enum hrtimer_restart cec_pin_timer(struct hrtimer *timer)
{
	struct cec_pin *pin = container_of(timer, struct cec_pin, timer);
	struct cec_adapter *adap = pin->adap;
	ktime_t ts;
	s32 delta;
	u32 usecs;

	ts = ktime_get();
	if (ktime_to_ns(pin->timer_ts)) {
		delta = ktime_us_delta(ts, pin->timer_ts);
		pin->timer_cnt++;
		if (delta > 100 && pin->state != CEC_ST_IDLE) {
			/* Keep track of timer overruns */
			pin->timer_sum_overrun += delta;
			pin->timer_100us_overruns++;
			if (delta > 300)
				pin->timer_300us_overruns++;
			if (delta > pin->timer_max_overrun)
				pin->timer_max_overrun = delta;
		}
	}
	if (adap->monitor_pin_cnt)
		cec_pin_read(pin);

	if (pin->wait_usecs) {
		/*
		 * If we are monitoring the pin, then we have to
		 * sample at regular intervals.
		 */
		if (pin->wait_usecs > 150) {
			pin->wait_usecs -= 100;
			pin->timer_ts = ktime_add_us(ts, 100);
			hrtimer_forward_now(timer, ns_to_ktime(100000));
			return HRTIMER_RESTART;
		}
		if (pin->wait_usecs > 100) {
			pin->wait_usecs /= 2;
			pin->timer_ts = ktime_add_us(ts, pin->wait_usecs);
			hrtimer_forward_now(timer,
					ns_to_ktime(pin->wait_usecs * 1000));
			return HRTIMER_RESTART;
		}
		pin->timer_ts = ktime_add_us(ts, pin->wait_usecs);
		hrtimer_forward_now(timer,
				    ns_to_ktime(pin->wait_usecs * 1000));
		pin->wait_usecs = 0;
		return HRTIMER_RESTART;
	}

	switch (pin->state) {
	/* Transmit states */
	case CEC_ST_TX_WAIT_FOR_HIGH:
	case CEC_ST_TX_START_BIT_LOW:
	case CEC_ST_TX_START_BIT_HIGH:
	case CEC_ST_TX_START_BIT_HIGH_SHORT:
	case CEC_ST_TX_START_BIT_HIGH_LONG:
	case CEC_ST_TX_START_BIT_LOW_CUSTOM:
	case CEC_ST_TX_START_BIT_HIGH_CUSTOM:
	case CEC_ST_TX_DATA_BIT_0_LOW:
	case CEC_ST_TX_DATA_BIT_0_HIGH:
	case CEC_ST_TX_DATA_BIT_0_HIGH_SHORT:
	case CEC_ST_TX_DATA_BIT_0_HIGH_LONG:
	case CEC_ST_TX_DATA_BIT_1_LOW:
	case CEC_ST_TX_DATA_BIT_1_HIGH:
	case CEC_ST_TX_DATA_BIT_1_HIGH_SHORT:
	case CEC_ST_TX_DATA_BIT_1_HIGH_LONG:
	case CEC_ST_TX_DATA_BIT_1_HIGH_PRE_SAMPLE:
	case CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE:
	case CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_SHORT:
	case CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_LONG:
	case CEC_ST_TX_DATA_BIT_LOW_CUSTOM:
	case CEC_ST_TX_DATA_BIT_HIGH_CUSTOM:
	case CEC_ST_TX_PULSE_LOW_CUSTOM:
	case CEC_ST_TX_PULSE_HIGH_CUSTOM:
		cec_pin_tx_states(pin, ts);
		break;

	/* Receive states */
	case CEC_ST_RX_START_BIT_LOW:
	case CEC_ST_RX_START_BIT_HIGH:
	case CEC_ST_RX_DATA_SAMPLE:
	case CEC_ST_RX_DATA_POST_SAMPLE:
	case CEC_ST_RX_DATA_WAIT_FOR_LOW:
	case CEC_ST_RX_ACK_LOW:
	case CEC_ST_RX_ACK_LOW_POST:
	case CEC_ST_RX_ACK_HIGH_POST:
	case CEC_ST_RX_ACK_FINISH:
		cec_pin_rx_states(pin, ts);
		break;

	case CEC_ST_IDLE:
	case CEC_ST_TX_WAIT:
		if (!cec_pin_high(pin)) {
			/* Start bit, switch to receive state */
			pin->ts = ts;
			pin->state = CEC_ST_RX_START_BIT_LOW;
			/*
			 * If a transmit is pending, then that transmit should
			 * use a signal free time of no more than
			 * CEC_SIGNAL_FREE_TIME_NEW_INITIATOR since it will
			 * have a new initiator due to the receive that is now
			 * starting.
			 */
			if (pin->tx_msg.len && pin->tx_signal_free_time >
			    CEC_SIGNAL_FREE_TIME_NEW_INITIATOR)
				pin->tx_signal_free_time =
					CEC_SIGNAL_FREE_TIME_NEW_INITIATOR;
			break;
		}
		if (ktime_to_ns(pin->ts) == 0)
			pin->ts = ts;
		if (pin->tx_msg.len) {
			/*
			 * Check if the bus has been free for long enough
			 * so we can kick off the pending transmit.
			 */
			delta = ktime_us_delta(ts, pin->ts);
			if (delta / CEC_TIM_DATA_BIT_TOTAL >
			    pin->tx_signal_free_time) {
				pin->tx_nacked = false;
				if (tx_custom_start(pin))
					pin->state = CEC_ST_TX_START_BIT_LOW_CUSTOM;
				else
					pin->state = CEC_ST_TX_START_BIT_LOW;
				/* Generate start bit */
				cec_pin_low(pin);
				break;
			}
			if (delta / CEC_TIM_DATA_BIT_TOTAL >
			    pin->tx_signal_free_time - 1)
				pin->state = CEC_ST_TX_WAIT;
			break;
		}
		if (pin->tx_custom_pulse && pin->state == CEC_ST_IDLE) {
			pin->tx_custom_pulse = false;
			/* Generate custom pulse */
			cec_pin_low(pin);
			pin->state = CEC_ST_TX_PULSE_LOW_CUSTOM;
			break;
		}
		if (pin->state != CEC_ST_IDLE || pin->ops->enable_irq == NULL ||
		    pin->enable_irq_failed || adap->is_configuring ||
		    adap->is_configured || adap->monitor_all_cnt)
			break;
		/* Switch to interrupt mode */
		atomic_set(&pin->work_irq_change, CEC_PIN_IRQ_ENABLE);
		pin->state = CEC_ST_RX_IRQ;
		wake_up_interruptible(&pin->kthread_waitq);
		return HRTIMER_NORESTART;

	case CEC_ST_TX_LOW_DRIVE:
	case CEC_ST_RX_LOW_DRIVE:
		cec_pin_high(pin);
		cec_pin_to_idle(pin);
		break;

	default:
		break;
	}

	switch (pin->state) {
	case CEC_ST_TX_START_BIT_LOW_CUSTOM:
	case CEC_ST_TX_DATA_BIT_LOW_CUSTOM:
	case CEC_ST_TX_PULSE_LOW_CUSTOM:
		usecs = pin->tx_custom_low_usecs;
		break;
	case CEC_ST_TX_START_BIT_HIGH_CUSTOM:
	case CEC_ST_TX_DATA_BIT_HIGH_CUSTOM:
	case CEC_ST_TX_PULSE_HIGH_CUSTOM:
		usecs = pin->tx_custom_high_usecs;
		break;
	default:
		usecs = states[pin->state].usecs;
		break;
	}

	if (!adap->monitor_pin_cnt || usecs <= 150) {
		pin->wait_usecs = 0;
		pin->timer_ts = ktime_add_us(ts, usecs);
		hrtimer_forward_now(timer,
				ns_to_ktime(usecs * 1000));
		return HRTIMER_RESTART;
	}
	pin->wait_usecs = usecs - 100;
	pin->timer_ts = ktime_add_us(ts, 100);
	hrtimer_forward_now(timer, ns_to_ktime(100000));
	return HRTIMER_RESTART;
}

static int cec_pin_thread_func(void *_adap)
{
	struct cec_adapter *adap = _adap;
	struct cec_pin *pin = adap->pin;
	bool irq_enabled = false;

	for (;;) {
		wait_event_interruptible(pin->kthread_waitq,
			kthread_should_stop() ||
			pin->work_rx_msg.len ||
			pin->work_tx_status ||
			atomic_read(&pin->work_irq_change) ||
			atomic_read(&pin->work_pin_num_events));

		if (pin->work_rx_msg.len) {
			struct cec_msg *msg = &pin->work_rx_msg;

			if (msg->len > 1 && msg->len < CEC_MAX_MSG_SIZE &&
			    rx_add_byte(pin)) {
				/* Error injection: add byte to the message */
				msg->msg[msg->len++] = 0x55;
			}
			if (msg->len > 2 && rx_remove_byte(pin)) {
				/* Error injection: remove byte from message */
				msg->len--;
			}
			if (msg->len > CEC_MAX_MSG_SIZE)
				msg->len = CEC_MAX_MSG_SIZE;
			cec_received_msg_ts(adap, msg,
				ns_to_ktime(pin->work_rx_msg.rx_ts));
			msg->len = 0;
		}

		if (pin->work_tx_status) {
			unsigned int tx_status = pin->work_tx_status;

			pin->work_tx_status = 0;
			cec_transmit_attempt_done_ts(adap, tx_status,
						     pin->work_tx_ts);
		}

		while (atomic_read(&pin->work_pin_num_events)) {
			unsigned int idx = pin->work_pin_events_rd;
			u8 v = pin->work_pin_events[idx];

			cec_queue_pin_cec_event(adap,
						v & CEC_PIN_EVENT_FL_IS_HIGH,
						v & CEC_PIN_EVENT_FL_DROPPED,
						pin->work_pin_ts[idx]);
			pin->work_pin_events_rd = (idx + 1) % CEC_NUM_PIN_EVENTS;
			atomic_dec(&pin->work_pin_num_events);
		}

		switch (atomic_xchg(&pin->work_irq_change,
				    CEC_PIN_IRQ_UNCHANGED)) {
		case CEC_PIN_IRQ_DISABLE:
			if (irq_enabled) {
				pin->ops->disable_irq(adap);
				irq_enabled = false;
			}
			cec_pin_high(pin);
			cec_pin_to_idle(pin);
			hrtimer_start(&pin->timer, ns_to_ktime(0),
				      HRTIMER_MODE_REL);
			break;
		case CEC_PIN_IRQ_ENABLE:
			if (irq_enabled)
				break;
			pin->enable_irq_failed = !pin->ops->enable_irq(adap);
			if (pin->enable_irq_failed) {
				cec_pin_to_idle(pin);
				hrtimer_start(&pin->timer, ns_to_ktime(0),
					      HRTIMER_MODE_REL);
			} else {
				irq_enabled = true;
			}
			break;
		default:
			break;
		}
		if (kthread_should_stop())
			break;
	}
	if (pin->ops->disable_irq && irq_enabled)
		pin->ops->disable_irq(adap);
	hrtimer_cancel(&pin->timer);
	cec_pin_read(pin);
	cec_pin_to_idle(pin);
	pin->state = CEC_ST_OFF;
	return 0;
}

static int cec_pin_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct cec_pin *pin = adap->pin;

	pin->enabled = enable;
	if (enable) {
		atomic_set(&pin->work_pin_num_events, 0);
		pin->work_pin_events_rd = pin->work_pin_events_wr = 0;
		pin->work_pin_events_dropped = false;
		cec_pin_read(pin);
		cec_pin_to_idle(pin);
		pin->tx_msg.len = 0;
		pin->timer_ts = ns_to_ktime(0);
		atomic_set(&pin->work_irq_change, CEC_PIN_IRQ_UNCHANGED);
		pin->kthread = kthread_run(cec_pin_thread_func, adap,
					   "cec-pin");
		if (IS_ERR(pin->kthread)) {
			pr_err("cec-pin: kernel_thread() failed\n");
			return PTR_ERR(pin->kthread);
		}
		hrtimer_start(&pin->timer, ns_to_ktime(0),
			      HRTIMER_MODE_REL);
	} else {
		kthread_stop(pin->kthread);
	}
	return 0;
}

static int cec_pin_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct cec_pin *pin = adap->pin;

	if (log_addr == CEC_LOG_ADDR_INVALID)
		pin->la_mask = 0;
	else
		pin->la_mask |= (1 << log_addr);
	return 0;
}

void cec_pin_start_timer(struct cec_pin *pin)
{
	if (pin->state != CEC_ST_RX_IRQ)
		return;

	atomic_set(&pin->work_irq_change, CEC_PIN_IRQ_DISABLE);
	wake_up_interruptible(&pin->kthread_waitq);
}

static int cec_pin_adap_transmit(struct cec_adapter *adap, u8 attempts,
				      u32 signal_free_time, struct cec_msg *msg)
{
	struct cec_pin *pin = adap->pin;

	/*
	 * If a receive is in progress, then this transmit should use
	 * a signal free time of max CEC_SIGNAL_FREE_TIME_NEW_INITIATOR
	 * since when it starts transmitting it will have a new initiator.
	 */
	if (pin->state != CEC_ST_IDLE &&
	    signal_free_time > CEC_SIGNAL_FREE_TIME_NEW_INITIATOR)
		signal_free_time = CEC_SIGNAL_FREE_TIME_NEW_INITIATOR;

	pin->tx_signal_free_time = signal_free_time;
	pin->tx_extra_bytes = 0;
	pin->tx_msg = *msg;
	if (msg->len > 1) {
		/* Error injection: add byte to the message */
		pin->tx_extra_bytes = tx_add_bytes(pin);
	}
	if (msg->len > 2 && tx_remove_byte(pin)) {
		/* Error injection: remove byte from the message */
		pin->tx_msg.len--;
	}
	pin->work_tx_status = 0;
	pin->tx_bit = 0;
	cec_pin_start_timer(pin);
	return 0;
}

static void cec_pin_adap_status(struct cec_adapter *adap,
				       struct seq_file *file)
{
	struct cec_pin *pin = adap->pin;

	seq_printf(file, "state: %s\n", states[pin->state].name);
	seq_printf(file, "tx_bit: %d\n", pin->tx_bit);
	seq_printf(file, "rx_bit: %d\n", pin->rx_bit);
	seq_printf(file, "cec pin: %d\n", pin->ops->read(adap));
	seq_printf(file, "cec pin events dropped: %u\n",
		   pin->work_pin_events_dropped_cnt);
	seq_printf(file, "irq failed: %d\n", pin->enable_irq_failed);
	if (pin->timer_100us_overruns) {
		seq_printf(file, "timer overruns > 100us: %u of %u\n",
			   pin->timer_100us_overruns, pin->timer_cnt);
		seq_printf(file, "timer overruns > 300us: %u of %u\n",
			   pin->timer_300us_overruns, pin->timer_cnt);
		seq_printf(file, "max timer overrun: %u usecs\n",
			   pin->timer_max_overrun);
		seq_printf(file, "avg timer overrun: %u usecs\n",
			   pin->timer_sum_overrun / pin->timer_100us_overruns);
	}
	if (pin->rx_start_bit_low_too_short_cnt)
		seq_printf(file,
			   "rx start bit low too short: %u (delta %u, ts %llu)\n",
			   pin->rx_start_bit_low_too_short_cnt,
			   pin->rx_start_bit_low_too_short_delta,
			   pin->rx_start_bit_low_too_short_ts);
	if (pin->rx_start_bit_too_short_cnt)
		seq_printf(file,
			   "rx start bit too short: %u (delta %u, ts %llu)\n",
			   pin->rx_start_bit_too_short_cnt,
			   pin->rx_start_bit_too_short_delta,
			   pin->rx_start_bit_too_short_ts);
	if (pin->rx_start_bit_too_long_cnt)
		seq_printf(file, "rx start bit too long: %u\n",
			   pin->rx_start_bit_too_long_cnt);
	if (pin->rx_data_bit_too_short_cnt)
		seq_printf(file,
			   "rx data bit too short: %u (delta %u, ts %llu)\n",
			   pin->rx_data_bit_too_short_cnt,
			   pin->rx_data_bit_too_short_delta,
			   pin->rx_data_bit_too_short_ts);
	if (pin->rx_data_bit_too_long_cnt)
		seq_printf(file, "rx data bit too long: %u\n",
			   pin->rx_data_bit_too_long_cnt);
	seq_printf(file, "rx initiated low drive: %u\n", pin->rx_low_drive_cnt);
	seq_printf(file, "tx detected low drive: %u\n", pin->tx_low_drive_cnt);
	pin->work_pin_events_dropped_cnt = 0;
	pin->timer_cnt = 0;
	pin->timer_100us_overruns = 0;
	pin->timer_300us_overruns = 0;
	pin->timer_max_overrun = 0;
	pin->timer_sum_overrun = 0;
	pin->rx_start_bit_low_too_short_cnt = 0;
	pin->rx_start_bit_too_short_cnt = 0;
	pin->rx_start_bit_too_long_cnt = 0;
	pin->rx_data_bit_too_short_cnt = 0;
	pin->rx_data_bit_too_long_cnt = 0;
	pin->rx_low_drive_cnt = 0;
	pin->tx_low_drive_cnt = 0;
	if (pin->ops->status)
		pin->ops->status(adap, file);
}

static int cec_pin_adap_monitor_all_enable(struct cec_adapter *adap,
						  bool enable)
{
	struct cec_pin *pin = adap->pin;

	pin->monitor_all = enable;
	return 0;
}

static void cec_pin_adap_free(struct cec_adapter *adap)
{
	struct cec_pin *pin = adap->pin;

	if (pin->ops->free)
		pin->ops->free(adap);
	adap->pin = NULL;
	kfree(pin);
}

static int cec_pin_received(struct cec_adapter *adap, struct cec_msg *msg)
{
	struct cec_pin *pin = adap->pin;

	if (pin->ops->received)
		return pin->ops->received(adap, msg);
	return -ENOMSG;
}

void cec_pin_changed(struct cec_adapter *adap, bool value)
{
	struct cec_pin *pin = adap->pin;

	cec_pin_update(pin, value, false);
	if (!value && (adap->is_configuring || adap->is_configured ||
		       adap->monitor_all_cnt))
		atomic_set(&pin->work_irq_change, CEC_PIN_IRQ_DISABLE);
}
EXPORT_SYMBOL_GPL(cec_pin_changed);

static const struct cec_adap_ops cec_pin_adap_ops = {
	.adap_enable = cec_pin_adap_enable,
	.adap_monitor_all_enable = cec_pin_adap_monitor_all_enable,
	.adap_log_addr = cec_pin_adap_log_addr,
	.adap_transmit = cec_pin_adap_transmit,
	.adap_status = cec_pin_adap_status,
	.adap_free = cec_pin_adap_free,
#ifdef CONFIG_CEC_PIN_ERROR_INJ
	.error_inj_parse_line = cec_pin_error_inj_parse_line,
	.error_inj_show = cec_pin_error_inj_show,
#endif
	.received = cec_pin_received,
};

struct cec_adapter *cec_pin_allocate_adapter(const struct cec_pin_ops *pin_ops,
					void *priv, const char *name, u32 caps)
{
	struct cec_adapter *adap;
	struct cec_pin *pin = kzalloc(sizeof(*pin), GFP_KERNEL);

	if (pin == NULL)
		return ERR_PTR(-ENOMEM);
	pin->ops = pin_ops;
	hrtimer_init(&pin->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pin->timer.function = cec_pin_timer;
	init_waitqueue_head(&pin->kthread_waitq);
	pin->tx_custom_low_usecs = CEC_TIM_CUSTOM_DEFAULT;
	pin->tx_custom_high_usecs = CEC_TIM_CUSTOM_DEFAULT;

	adap = cec_allocate_adapter(&cec_pin_adap_ops, priv, name,
			    caps | CEC_CAP_MONITOR_ALL | CEC_CAP_MONITOR_PIN,
			    CEC_MAX_LOG_ADDRS);

	if (IS_ERR(adap)) {
		kfree(pin);
		return adap;
	}

	adap->pin = pin;
	pin->adap = adap;
	cec_pin_update(pin, cec_pin_high(pin), true);
	return adap;
}
EXPORT_SYMBOL_GPL(cec_pin_allocate_adapter);
