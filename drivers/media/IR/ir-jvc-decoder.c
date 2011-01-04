/* ir-jvc-decoder.c - handle JVC IR Pulse/Space protocol
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

#include <linux/bitrev.h>
#include "ir-core-priv.h"

#define JVC_NBITS		16		/* dev(8) + func(8) */
#define JVC_UNIT		525000		/* ns */
#define JVC_HEADER_PULSE	(16 * JVC_UNIT) /* lack of header -> repeat */
#define JVC_HEADER_SPACE	(8  * JVC_UNIT)
#define JVC_BIT_PULSE		(1  * JVC_UNIT)
#define JVC_BIT_0_SPACE		(1  * JVC_UNIT)
#define JVC_BIT_1_SPACE		(3  * JVC_UNIT)
#define JVC_TRAILER_PULSE	(1  * JVC_UNIT)
#define	JVC_TRAILER_SPACE	(35 * JVC_UNIT)

enum jvc_state {
	STATE_INACTIVE,
	STATE_HEADER_SPACE,
	STATE_BIT_PULSE,
	STATE_BIT_SPACE,
	STATE_TRAILER_PULSE,
	STATE_TRAILER_SPACE,
	STATE_CHECK_REPEAT,
};

/**
 * ir_jvc_decode() - Decode one JVC pulse or space
 * @input_dev:	the struct input_dev descriptor of the device
 * @duration:   the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_jvc_decode(struct input_dev *input_dev, struct ir_raw_event ev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	struct jvc_dec *data = &ir_dev->raw->jvc;

	if (!(ir_dev->raw->enabled_protocols & IR_TYPE_JVC))
		return 0;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, JVC_UNIT, JVC_UNIT / 2))
		goto out;

	IR_dprintk(2, "JVC decode started at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

again:
	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_HEADER_PULSE, JVC_UNIT / 2))
			break;

		data->count = 0;
		data->first = true;
		data->toggle = !data->toggle;
		data->state = STATE_HEADER_SPACE;
		return 0;

	case STATE_HEADER_SPACE:
		if (ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_HEADER_SPACE, JVC_UNIT / 2))
			break;

		data->state = STATE_BIT_PULSE;
		return 0;

	case STATE_BIT_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_BIT_PULSE, JVC_UNIT / 2))
			break;

		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_SPACE:
		if (ev.pulse)
			break;

		data->bits <<= 1;
		if (eq_margin(ev.duration, JVC_BIT_1_SPACE, JVC_UNIT / 2)) {
			data->bits |= 1;
			decrease_duration(&ev, JVC_BIT_1_SPACE);
		} else if (eq_margin(ev.duration, JVC_BIT_0_SPACE, JVC_UNIT / 2))
			decrease_duration(&ev, JVC_BIT_0_SPACE);
		else
			break;
		data->count++;

		if (data->count == JVC_NBITS)
			data->state = STATE_TRAILER_PULSE;
		else
			data->state = STATE_BIT_PULSE;
		return 0;

	case STATE_TRAILER_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_TRAILER_PULSE, JVC_UNIT / 2))
			break;

		data->state = STATE_TRAILER_SPACE;
		return 0;

	case STATE_TRAILER_SPACE:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, JVC_TRAILER_SPACE, JVC_UNIT / 2))
			break;

		if (data->first) {
			u32 scancode;
			scancode = (bitrev8((data->bits >> 8) & 0xff) << 8) |
				   (bitrev8((data->bits >> 0) & 0xff) << 0);
			IR_dprintk(1, "JVC scancode 0x%04x\n", scancode);
			ir_keydown(input_dev, scancode, data->toggle);
			data->first = false;
			data->old_bits = data->bits;
		} else if (data->bits == data->old_bits) {
			IR_dprintk(1, "JVC repeat\n");
			ir_repeat(input_dev);
		} else {
			IR_dprintk(1, "JVC invalid repeat msg\n");
			break;
		}

		data->count = 0;
		data->state = STATE_CHECK_REPEAT;
		return 0;

	case STATE_CHECK_REPEAT:
		if (!ev.pulse)
			break;

		if (eq_margin(ev.duration, JVC_HEADER_PULSE, JVC_UNIT / 2))
			data->state = STATE_INACTIVE;
  else
			data->state = STATE_BIT_PULSE;
		goto again;
	}

out:
	IR_dprintk(1, "JVC decode failed at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler jvc_handler = {
	.protocols	= IR_TYPE_JVC,
	.decode		= ir_jvc_decode,
};

static int __init ir_jvc_decode_init(void)
{
	ir_raw_handler_register(&jvc_handler);

	printk(KERN_INFO "IR JVC protocol handler initialized\n");
	return 0;
}

static void __exit ir_jvc_decode_exit(void)
{
	ir_raw_handler_unregister(&jvc_handler);
}

module_init(ir_jvc_decode_init);
module_exit(ir_jvc_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Härdeman <david@hardeman.nu>");
MODULE_DESCRIPTION("JVC IR protocol decoder");
