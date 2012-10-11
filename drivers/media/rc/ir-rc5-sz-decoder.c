/* ir-rc5-sz-decoder.c - handle RC5 Streamzap IR Pulse/Space protocol
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 * Copyright (C) 2010 by Jarod Wilson <jarod@redhat.com>
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

/*
 * This code handles the 15 bit RC5-ish protocol used by the Streamzap
 * PC Remote.
 * It considers a carrier of 36 kHz, with a total of 15 bits, where
 * the first two bits are start bits, and a third one is a filing bit
 */

#include "rc-core-priv.h"
#include <linux/module.h>

#define RC5_SZ_NBITS		15
#define RC5_UNIT		888888 /* ns */
#define RC5_BIT_START		(1 * RC5_UNIT)
#define RC5_BIT_END		(1 * RC5_UNIT)

enum rc5_sz_state {
	STATE_INACTIVE,
	STATE_BIT_START,
	STATE_BIT_END,
	STATE_FINISHED,
};

/**
 * ir_rc5_sz_decode() - Decode one RC-5 Streamzap pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rc5_sz_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct rc5_sz_dec *data = &dev->raw->rc5_sz;
	u8 toggle, command, system;
	u32 scancode;

	if (!(dev->raw->enabled_protocols & RC_BIT_RC5_SZ))
		return 0;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, RC5_UNIT, RC5_UNIT / 2))
		goto out;

again:
	IR_dprintk(2, "RC5-sz decode started at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	if (!geq_margin(ev.duration, RC5_UNIT, RC5_UNIT / 2))
		return 0;

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		data->state = STATE_BIT_START;
		data->count = 1;
		data->wanted_bits = RC5_SZ_NBITS;
		decrease_duration(&ev, RC5_BIT_START);
		goto again;

	case STATE_BIT_START:
		if (!eq_margin(ev.duration, RC5_BIT_START, RC5_UNIT / 2))
			break;

		data->bits <<= 1;
		if (!ev.pulse)
			data->bits |= 1;
		data->count++;
		data->state = STATE_BIT_END;
		return 0;

	case STATE_BIT_END:
		if (!is_transition(&ev, &dev->raw->prev_ev))
			break;

		if (data->count == data->wanted_bits)
			data->state = STATE_FINISHED;
		else
			data->state = STATE_BIT_START;

		decrease_duration(&ev, RC5_BIT_END);
		goto again;

	case STATE_FINISHED:
		if (ev.pulse)
			break;

		/* RC5-sz */
		command  = (data->bits & 0x0003F) >> 0;
		system   = (data->bits & 0x02FC0) >> 6;
		toggle   = (data->bits & 0x01000) ? 1 : 0;
		scancode = system << 6 | command;

		IR_dprintk(1, "RC5-sz scancode 0x%04x (toggle: %u)\n",
			   scancode, toggle);

		rc_keydown(dev, scancode, toggle);
		data->state = STATE_INACTIVE;
		return 0;
	}

out:
	IR_dprintk(1, "RC5-sz decode failed at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler rc5_sz_handler = {
	.protocols	= RC_BIT_RC5_SZ,
	.decode		= ir_rc5_sz_decode,
};

static int __init ir_rc5_sz_decode_init(void)
{
	ir_raw_handler_register(&rc5_sz_handler);

	printk(KERN_INFO "IR RC5 (streamzap) protocol handler initialized\n");
	return 0;
}

static void __exit ir_rc5_sz_decode_exit(void)
{
	ir_raw_handler_unregister(&rc5_sz_handler);
}

module_init(ir_rc5_sz_decode_init);
module_exit(ir_rc5_sz_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("RC5 (streamzap) IR protocol decoder");
