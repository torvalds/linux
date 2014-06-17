/* ir-sanyo-decoder.c - handle SANYO IR Pulse/Space protocol
 *
 * Copyright (C) 2011 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * This protocol uses the NEC protocol timings. However, data is formatted as:
 *	13 bits Custom Code
 *	13 bits NOT(Custom Code)
 *	8 bits Key data
 *	8 bits NOT(Key data)
 *
 * According with LIRC, this protocol is used on Sanyo, Aiwa and Chinon
 * Information for this protocol is available at the Sanyo LC7461 datasheet.
 */

#include <linux/module.h>
#include <linux/bitrev.h>
#include "rc-core-priv.h"

#define SANYO_NBITS		(13+13+8+8)
#define SANYO_UNIT		562500  /* ns */
#define SANYO_HEADER_PULSE	(16  * SANYO_UNIT)
#define SANYO_HEADER_SPACE	(8   * SANYO_UNIT)
#define SANYO_BIT_PULSE		(1   * SANYO_UNIT)
#define SANYO_BIT_0_SPACE	(1   * SANYO_UNIT)
#define SANYO_BIT_1_SPACE	(3   * SANYO_UNIT)
#define SANYO_REPEAT_SPACE	(150 * SANYO_UNIT)
#define	SANYO_TRAILER_PULSE	(1   * SANYO_UNIT)
#define	SANYO_TRAILER_SPACE	(10  * SANYO_UNIT)	/* in fact, 42 */

enum sanyo_state {
	STATE_INACTIVE,
	STATE_HEADER_SPACE,
	STATE_BIT_PULSE,
	STATE_BIT_SPACE,
	STATE_TRAILER_PULSE,
	STATE_TRAILER_SPACE,
};

/**
 * ir_sanyo_decode() - Decode one SANYO pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @duration:	the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_sanyo_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct sanyo_dec *data = &dev->raw->sanyo;
	u32 scancode;
	u8 address, command, not_command;

	if (!rc_protocols_enabled(dev, RC_BIT_SANYO))
		return 0;

	if (!is_timing_event(ev)) {
		if (ev.reset) {
			IR_dprintk(1, "SANYO event reset received. reset to state 0\n");
			data->state = STATE_INACTIVE;
		}
		return 0;
	}

	IR_dprintk(2, "SANYO decode started at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (eq_margin(ev.duration, SANYO_HEADER_PULSE, SANYO_UNIT / 2)) {
			data->count = 0;
			data->state = STATE_HEADER_SPACE;
			return 0;
		}
		break;


	case STATE_HEADER_SPACE:
		if (ev.pulse)
			break;

		if (eq_margin(ev.duration, SANYO_HEADER_SPACE, SANYO_UNIT / 2)) {
			data->state = STATE_BIT_PULSE;
			return 0;
		}

		break;

	case STATE_BIT_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, SANYO_BIT_PULSE, SANYO_UNIT / 2))
			break;

		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_SPACE:
		if (ev.pulse)
			break;

		if (!data->count && geq_margin(ev.duration, SANYO_REPEAT_SPACE, SANYO_UNIT / 2)) {
			if (!dev->keypressed) {
				IR_dprintk(1, "SANYO discarding last key repeat: event after key up\n");
			} else {
				rc_repeat(dev);
				IR_dprintk(1, "SANYO repeat last key\n");
				data->state = STATE_INACTIVE;
			}
			return 0;
		}

		data->bits <<= 1;
		if (eq_margin(ev.duration, SANYO_BIT_1_SPACE, SANYO_UNIT / 2))
			data->bits |= 1;
		else if (!eq_margin(ev.duration, SANYO_BIT_0_SPACE, SANYO_UNIT / 2))
			break;
		data->count++;

		if (data->count == SANYO_NBITS)
			data->state = STATE_TRAILER_PULSE;
		else
			data->state = STATE_BIT_PULSE;

		return 0;

	case STATE_TRAILER_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, SANYO_TRAILER_PULSE, SANYO_UNIT / 2))
			break;

		data->state = STATE_TRAILER_SPACE;
		return 0;

	case STATE_TRAILER_SPACE:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, SANYO_TRAILER_SPACE, SANYO_UNIT / 2))
			break;

		address     = bitrev16((data->bits >> 29) & 0x1fff) >> 3;
		/* not_address = bitrev16((data->bits >> 16) & 0x1fff) >> 3; */
		command	    = bitrev8((data->bits >>  8) & 0xff);
		not_command = bitrev8((data->bits >>  0) & 0xff);

		if ((command ^ not_command) != 0xff) {
			IR_dprintk(1, "SANYO checksum error: received 0x%08Lx\n",
				   data->bits);
			data->state = STATE_INACTIVE;
			return 0;
		}

		scancode = address << 8 | command;
		IR_dprintk(1, "SANYO scancode: 0x%06x\n", scancode);
		rc_keydown(dev, scancode, 0);
		data->state = STATE_INACTIVE;
		return 0;
	}

	IR_dprintk(1, "SANYO decode failed at count %d state %d (%uus %s)\n",
		   data->count, data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler sanyo_handler = {
	.protocols	= RC_BIT_SANYO,
	.decode		= ir_sanyo_decode,
};

static int __init ir_sanyo_decode_init(void)
{
	ir_raw_handler_register(&sanyo_handler);

	printk(KERN_INFO "IR SANYO protocol handler initialized\n");
	return 0;
}

static void __exit ir_sanyo_decode_exit(void)
{
	ir_raw_handler_unregister(&sanyo_handler);
}

module_init(ir_sanyo_decode_init);
module_exit(ir_sanyo_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("SANYO IR protocol decoder");
