/* ir-rc6-decoder.c - A decoder for the RC6 IR protocol
 *
 * Copyright (C) 2010 by David Härdeman <david@hardeman.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "ir-core-priv.h"

/*
 * This decoder currently supports:
 * RC6-0-16	(standard toggle bit in header)
 * RC6-6A-24	(no toggle bit)
 * RC6-6A-32	(MCE version with toggle bit in body)
 */

#define RC6_UNIT		444444	/* us */
#define RC6_HEADER_NBITS	4	/* not including toggle bit */
#define RC6_0_NBITS		16
#define RC6_6A_SMALL_NBITS	24
#define RC6_6A_LARGE_NBITS	32
#define RC6_PREFIX_PULSE	(6 * RC6_UNIT)
#define RC6_PREFIX_SPACE	(2 * RC6_UNIT)
#define RC6_BIT_START		(1 * RC6_UNIT)
#define RC6_BIT_END		(1 * RC6_UNIT)
#define RC6_TOGGLE_START	(2 * RC6_UNIT)
#define RC6_TOGGLE_END		(2 * RC6_UNIT)
#define RC6_MODE_MASK		0x07	/* for the header bits */
#define RC6_STARTBIT_MASK	0x08	/* for the header bits */
#define RC6_6A_MCE_TOGGLE_MASK	0x8000	/* for the body bits */

enum rc6_mode {
	RC6_MODE_0,
	RC6_MODE_6A,
	RC6_MODE_UNKNOWN,
};

enum rc6_state {
	STATE_INACTIVE,
	STATE_PREFIX_SPACE,
	STATE_HEADER_BIT_START,
	STATE_HEADER_BIT_END,
	STATE_TOGGLE_START,
	STATE_TOGGLE_END,
	STATE_BODY_BIT_START,
	STATE_BODY_BIT_END,
	STATE_FINISHED,
};

static enum rc6_mode rc6_mode(struct rc6_dec *data)
{
	switch (data->header & RC6_MODE_MASK) {
	case 0:
		return RC6_MODE_0;
	case 6:
		if (!data->toggle)
			return RC6_MODE_6A;
		/* fall through */
	default:
		return RC6_MODE_UNKNOWN;
	}
}

/**
 * ir_rc6_decode() - Decode one RC6 pulse or space
 * @input_dev:	the struct input_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rc6_decode(struct input_dev *input_dev, struct ir_raw_event ev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	struct rc6_dec *data = &ir_dev->raw->rc6;
	u32 scancode;
	u8 toggle;

	if (!(ir_dev->raw->enabled_protocols & IR_TYPE_RC6))
		return 0;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, RC6_UNIT, RC6_UNIT / 2))
		goto out;

again:
	IR_dprintk(2, "RC6 decode started at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	if (!geq_margin(ev.duration, RC6_UNIT, RC6_UNIT / 2))
		return 0;

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		/* Note: larger margin on first pulse since each RC6_UNIT
		   is quite short and some hardware takes some time to
		   adjust to the signal */
		if (!eq_margin(ev.duration, RC6_PREFIX_PULSE, RC6_UNIT))
			break;

		data->state = STATE_PREFIX_SPACE;
		data->count = 0;
		return 0;

	case STATE_PREFIX_SPACE:
		if (ev.pulse)
			break;

		if (!eq_margin(ev.duration, RC6_PREFIX_SPACE, RC6_UNIT / 2))
			break;

		data->state = STATE_HEADER_BIT_START;
		return 0;

	case STATE_HEADER_BIT_START:
		if (!eq_margin(ev.duration, RC6_BIT_START, RC6_UNIT / 2))
			break;

		data->header <<= 1;
		if (ev.pulse)
			data->header |= 1;
		data->count++;
		data->state = STATE_HEADER_BIT_END;
		return 0;

	case STATE_HEADER_BIT_END:
		if (!is_transition(&ev, &ir_dev->raw->prev_ev))
			break;

		if (data->count == RC6_HEADER_NBITS)
			data->state = STATE_TOGGLE_START;
		else
			data->state = STATE_HEADER_BIT_START;

		decrease_duration(&ev, RC6_BIT_END);
		goto again;

	case STATE_TOGGLE_START:
		if (!eq_margin(ev.duration, RC6_TOGGLE_START, RC6_UNIT / 2))
			break;

		data->toggle = ev.pulse;
		data->state = STATE_TOGGLE_END;
		return 0;

	case STATE_TOGGLE_END:
		if (!is_transition(&ev, &ir_dev->raw->prev_ev) ||
		    !geq_margin(ev.duration, RC6_TOGGLE_END, RC6_UNIT / 2))
			break;

		if (!(data->header & RC6_STARTBIT_MASK)) {
			IR_dprintk(1, "RC6 invalid start bit\n");
			break;
		}

		data->state = STATE_BODY_BIT_START;
		decrease_duration(&ev, RC6_TOGGLE_END);
		data->count = 0;

		switch (rc6_mode(data)) {
		case RC6_MODE_0:
			data->wanted_bits = RC6_0_NBITS;
			break;
		case RC6_MODE_6A:
			/* This might look weird, but we basically
			   check the value of the first body bit to
			   determine the number of bits in mode 6A */
			if ((!ev.pulse && !geq_margin(ev.duration, RC6_UNIT, RC6_UNIT / 2)) ||
			    geq_margin(ev.duration, RC6_UNIT, RC6_UNIT / 2))
				data->wanted_bits = RC6_6A_LARGE_NBITS;
			else
				data->wanted_bits = RC6_6A_SMALL_NBITS;
			break;
		default:
			IR_dprintk(1, "RC6 unknown mode\n");
			goto out;
		}
		goto again;

	case STATE_BODY_BIT_START:
		if (!eq_margin(ev.duration, RC6_BIT_START, RC6_UNIT / 2))
			break;

		data->body <<= 1;
		if (ev.pulse)
			data->body |= 1;
		data->count++;
		data->state = STATE_BODY_BIT_END;
		return 0;

	case STATE_BODY_BIT_END:
		if (!is_transition(&ev, &ir_dev->raw->prev_ev))
			break;

		if (data->count == data->wanted_bits)
			data->state = STATE_FINISHED;
		else
			data->state = STATE_BODY_BIT_START;

		decrease_duration(&ev, RC6_BIT_END);
		goto again;

	case STATE_FINISHED:
		if (ev.pulse)
			break;

		switch (rc6_mode(data)) {
		case RC6_MODE_0:
			scancode = data->body & 0xffff;
			toggle = data->toggle;
			IR_dprintk(1, "RC6(0) scancode 0x%04x (toggle: %u)\n",
				   scancode, toggle);
			break;
		case RC6_MODE_6A:
			if (data->wanted_bits == RC6_6A_LARGE_NBITS) {
				toggle = data->body & RC6_6A_MCE_TOGGLE_MASK ? 1 : 0;
				scancode = data->body & ~RC6_6A_MCE_TOGGLE_MASK;
			} else {
				toggle = 0;
				scancode = data->body & 0xffffff;
			}

			IR_dprintk(1, "RC6(6A) scancode 0x%08x (toggle: %u)\n",
				   scancode, toggle);
			break;
		default:
			IR_dprintk(1, "RC6 unknown mode\n");
			goto out;
		}

		ir_keydown(input_dev, scancode, toggle);
		data->state = STATE_INACTIVE;
		return 0;
	}

out:
	IR_dprintk(1, "RC6 decode failed at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler rc6_handler = {
	.protocols	= IR_TYPE_RC6,
	.decode		= ir_rc6_decode,
};

static int __init ir_rc6_decode_init(void)
{
	ir_raw_handler_register(&rc6_handler);

	printk(KERN_INFO "IR RC6 protocol handler initialized\n");
	return 0;
}

static void __exit ir_rc6_decode_exit(void)
{
	ir_raw_handler_unregister(&rc6_handler);
}

module_init(ir_rc6_decode_init);
module_exit(ir_rc6_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Härdeman <david@hardeman.nu>");
MODULE_DESCRIPTION("RC6 IR protocol decoder");
