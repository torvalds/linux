/* ir-sharp-decoder.c - handle Sharp IR Pulse/Space protocol
 *
 * Copyright (C) 2013-2014 Imagination Technologies Ltd.
 *
 * Based on NEC decoder:
 * Copyright (C) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/bitrev.h>
#include <linux/module.h>
#include "rc-core-priv.h"

#define SHARP_NBITS		15
#define SHARP_UNIT		40000  /* ns */
#define SHARP_BIT_PULSE		(8    * SHARP_UNIT) /* 320us */
#define SHARP_BIT_0_PERIOD	(25   * SHARP_UNIT) /* 1ms (680us space) */
#define SHARP_BIT_1_PERIOD	(50   * SHARP_UNIT) /* 2ms (1680ms space) */
#define SHARP_ECHO_SPACE	(1000 * SHARP_UNIT) /* 40 ms */
#define SHARP_TRAILER_SPACE	(125  * SHARP_UNIT) /* 5 ms (even longer) */

enum sharp_state {
	STATE_INACTIVE,
	STATE_BIT_PULSE,
	STATE_BIT_SPACE,
	STATE_TRAILER_PULSE,
	STATE_ECHO_SPACE,
	STATE_TRAILER_SPACE,
};

/**
 * ir_sharp_decode() - Decode one Sharp pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @duration:	the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_sharp_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct sharp_dec *data = &dev->raw->sharp;
	u32 msg, echo, address, command, scancode;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	IR_dprintk(2, "Sharp decode started at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, SHARP_BIT_PULSE,
			       SHARP_BIT_PULSE / 2))
			break;

		data->count = 0;
		data->pulse_len = ev.duration;
		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, SHARP_BIT_PULSE,
			       SHARP_BIT_PULSE / 2))
			break;

		data->pulse_len = ev.duration;
		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_SPACE:
		if (ev.pulse)
			break;

		data->bits <<= 1;
		if (eq_margin(data->pulse_len + ev.duration, SHARP_BIT_1_PERIOD,
			      SHARP_BIT_PULSE * 2))
			data->bits |= 1;
		else if (!eq_margin(data->pulse_len + ev.duration,
				    SHARP_BIT_0_PERIOD, SHARP_BIT_PULSE * 2))
			break;
		data->count++;

		if (data->count == SHARP_NBITS ||
		    data->count == SHARP_NBITS * 2)
			data->state = STATE_TRAILER_PULSE;
		else
			data->state = STATE_BIT_PULSE;

		return 0;

	case STATE_TRAILER_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, SHARP_BIT_PULSE,
			       SHARP_BIT_PULSE / 2))
			break;

		if (data->count == SHARP_NBITS) {
			/* exp,chk bits should be 1,0 */
			if ((data->bits & 0x3) != 0x2 &&
			/* DENON variant, both chk bits 0 */
			    (data->bits & 0x3) != 0x0)
				break;
			data->state = STATE_ECHO_SPACE;
		} else {
			data->state = STATE_TRAILER_SPACE;
		}
		return 0;

	case STATE_ECHO_SPACE:
		if (ev.pulse)
			break;

		if (!eq_margin(ev.duration, SHARP_ECHO_SPACE,
			       SHARP_ECHO_SPACE / 4))
			break;

		data->state = STATE_BIT_PULSE;

		return 0;

	case STATE_TRAILER_SPACE:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, SHARP_TRAILER_SPACE,
				SHARP_BIT_PULSE / 2))
			break;

		/* Validate - command, ext, chk should be inverted in 2nd */
		msg = (data->bits >> 15) & 0x7fff;
		echo = data->bits & 0x7fff;
		if ((msg ^ echo) != 0x3ff) {
			IR_dprintk(1,
				   "Sharp checksum error: received 0x%04x, 0x%04x\n",
				   msg, echo);
			break;
		}

		address = bitrev8((msg >> 7) & 0xf8);
		command = bitrev8((msg >> 2) & 0xff);

		scancode = address << 8 | command;
		IR_dprintk(1, "Sharp scancode 0x%04x\n", scancode);

		rc_keydown(dev, RC_TYPE_SHARP, scancode, 0);
		data->state = STATE_INACTIVE;
		return 0;
	}

	IR_dprintk(1, "Sharp decode failed at count %d state %d (%uus %s)\n",
		   data->count, data->state, TO_US(ev.duration),
		   TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler sharp_handler = {
	.protocols	= RC_BIT_SHARP,
	.decode		= ir_sharp_decode,
};

static int __init ir_sharp_decode_init(void)
{
	ir_raw_handler_register(&sharp_handler);

	pr_info("IR Sharp protocol handler initialized\n");
	return 0;
}

static void __exit ir_sharp_decode_exit(void)
{
	ir_raw_handler_unregister(&sharp_handler);
}

module_init(ir_sharp_decode_init);
module_exit(ir_sharp_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Hogan <james.hogan@imgtec.com>");
MODULE_DESCRIPTION("Sharp IR protocol decoder");
