/* ir-sony-decoder.c - handle Sony IR Pulse/Space protocol
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
#include <linux/module.h>
#include "rc-core-priv.h"

#define SONY_UNIT		600000 /* ns */
#define SONY_HEADER_PULSE	(4 * SONY_UNIT)
#define	SONY_HEADER_SPACE	(1 * SONY_UNIT)
#define SONY_BIT_0_PULSE	(1 * SONY_UNIT)
#define SONY_BIT_1_PULSE	(2 * SONY_UNIT)
#define SONY_BIT_SPACE		(1 * SONY_UNIT)
#define SONY_TRAILER_SPACE	(10 * SONY_UNIT) /* minimum */

enum sony_state {
	STATE_INACTIVE,
	STATE_HEADER_SPACE,
	STATE_BIT_PULSE,
	STATE_BIT_SPACE,
	STATE_FINISHED,
};

/**
 * ir_sony_decode() - Decode one Sony pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:         the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_sony_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct sony_dec *data = &dev->raw->sony;
	u32 scancode;
	u8 device, subdevice, function;

	if (!(dev->enabled_protocols &
	      (RC_BIT_SONY12 | RC_BIT_SONY15 | RC_BIT_SONY20)))
		return 0;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, SONY_UNIT, SONY_UNIT / 2))
		goto out;

	IR_dprintk(2, "Sony decode started at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, SONY_HEADER_PULSE, SONY_UNIT / 2))
			break;

		data->count = 0;
		data->state = STATE_HEADER_SPACE;
		return 0;

	case STATE_HEADER_SPACE:
		if (ev.pulse)
			break;

		if (!eq_margin(ev.duration, SONY_HEADER_SPACE, SONY_UNIT / 2))
			break;

		data->state = STATE_BIT_PULSE;
		return 0;

	case STATE_BIT_PULSE:
		if (!ev.pulse)
			break;

		data->bits <<= 1;
		if (eq_margin(ev.duration, SONY_BIT_1_PULSE, SONY_UNIT / 2))
			data->bits |= 1;
		else if (!eq_margin(ev.duration, SONY_BIT_0_PULSE, SONY_UNIT / 2))
			break;

		data->count++;
		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_SPACE:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, SONY_BIT_SPACE, SONY_UNIT / 2))
			break;

		decrease_duration(&ev, SONY_BIT_SPACE);

		if (!geq_margin(ev.duration, SONY_UNIT, SONY_UNIT / 2)) {
			data->state = STATE_BIT_PULSE;
			return 0;
		}

		data->state = STATE_FINISHED;
		/* Fall through */

	case STATE_FINISHED:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, SONY_TRAILER_SPACE, SONY_UNIT / 2))
			break;

		switch (data->count) {
		case 12:
			if (!(dev->enabled_protocols & RC_BIT_SONY12)) {
				data->state = STATE_INACTIVE;
				return 0;
			}
			device    = bitrev8((data->bits <<  3) & 0xF8);
			subdevice = 0;
			function  = bitrev8((data->bits >>  4) & 0xFE);
			break;
		case 15:
			if (!(dev->enabled_protocols & RC_BIT_SONY15)) {
				data->state = STATE_INACTIVE;
				return 0;
			}
			device    = bitrev8((data->bits >>  0) & 0xFF);
			subdevice = 0;
			function  = bitrev8((data->bits >>  7) & 0xFE);
			break;
		case 20:
			if (!(dev->enabled_protocols & RC_BIT_SONY20)) {
				data->state = STATE_INACTIVE;
				return 0;
			}
			device    = bitrev8((data->bits >>  5) & 0xF8);
			subdevice = bitrev8((data->bits >>  0) & 0xFF);
			function  = bitrev8((data->bits >> 12) & 0xFE);
			break;
		default:
			IR_dprintk(1, "Sony invalid bitcount %u\n", data->count);
			goto out;
		}

		scancode = device << 16 | subdevice << 8 | function;
		IR_dprintk(1, "Sony(%u) scancode 0x%05x\n", data->count, scancode);
		rc_keydown(dev, scancode, 0);
		data->state = STATE_INACTIVE;
		return 0;
	}

out:
	IR_dprintk(1, "Sony decode failed at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler sony_handler = {
	.protocols	= RC_BIT_SONY12 | RC_BIT_SONY15 | RC_BIT_SONY20,
	.decode		= ir_sony_decode,
};

static int __init ir_sony_decode_init(void)
{
	ir_raw_handler_register(&sony_handler);

	printk(KERN_INFO "IR Sony protocol handler initialized\n");
	return 0;
}

static void __exit ir_sony_decode_exit(void)
{
	ir_raw_handler_unregister(&sony_handler);
}

module_init(ir_sony_decode_init);
module_exit(ir_sony_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Härdeman <david@hardeman.nu>");
MODULE_DESCRIPTION("Sony IR protocol decoder");
