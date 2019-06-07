/* ir-xmp-decoder.c - handle XMP IR Pulse/Space protocol
 *
 * Copyright (C) 2014 by Marcel Mol
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
 * - Based on info from http://www.hifi-remote.com
 * - Ignore Toggle=9 frames
 * - Ignore XMP-1 XMP-2 difference, always store 16 bit OBC
 */

#include <linux/bitrev.h>
#include <linux/module.h>
#include "rc-core-priv.h"

#define XMP_UNIT		  136000 /* ns */
#define XMP_LEADER		  210000 /* ns */
#define XMP_NIBBLE_PREFIX	  760000 /* ns */
#define	XMP_HALFFRAME_SPACE	13800000 /* ns */
#define	XMP_TRAILER_SPACE	20000000 /* should be 80ms but not all dureation supliers can go that high */

enum xmp_state {
	STATE_INACTIVE,
	STATE_LEADER_PULSE,
	STATE_NIBBLE_SPACE,
};

/**
 * ir_xmp_decode() - Decode one XMP pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_xmp_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct xmp_dec *data = &dev->raw->xmp;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	dev_dbg(&dev->dev, "XMP decode started at state %d %d (%uus %s)\n",
		data->state, data->count, TO_US(ev.duration), TO_STR(ev.pulse));

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (eq_margin(ev.duration, XMP_LEADER, XMP_UNIT / 2)) {
			data->count = 0;
			data->state = STATE_NIBBLE_SPACE;
		}

		return 0;

	case STATE_LEADER_PULSE:
		if (!ev.pulse)
			break;

		if (eq_margin(ev.duration, XMP_LEADER, XMP_UNIT / 2))
			data->state = STATE_NIBBLE_SPACE;

		return 0;

	case STATE_NIBBLE_SPACE:
		if (ev.pulse)
			break;

		if (geq_margin(ev.duration, XMP_TRAILER_SPACE, XMP_NIBBLE_PREFIX)) {
			int divider, i;
			u8 addr, subaddr, subaddr2, toggle, oem, obc1, obc2, sum1, sum2;
			u32 *n;
			u32 scancode;

			if (data->count != 16) {
				dev_dbg(&dev->dev, "received TRAILER period at index %d: %u\n",
					data->count, ev.duration);
				data->state = STATE_INACTIVE;
				return -EINVAL;
			}

			n = data->durations;
			/*
			 * the 4th nibble should be 15 so base the divider on this
			 * to transform durations into nibbles. Subtract 2000 from
			 * the divider to compensate for fluctuations in the signal
			 */
			divider = (n[3] - XMP_NIBBLE_PREFIX) / 15 - 2000;
			if (divider < 50) {
				dev_dbg(&dev->dev, "divider to small %d.\n",
					divider);
				data->state = STATE_INACTIVE;
				return -EINVAL;
			}

			/* convert to nibbles and do some sanity checks */
			for (i = 0; i < 16; i++)
				n[i] = (n[i] - XMP_NIBBLE_PREFIX) / divider;
			sum1 = (15 + n[0] + n[1] + n[2] + n[3] +
				n[4] + n[5] + n[6] + n[7]) % 16;
			sum2 = (15 + n[8] + n[9] + n[10] + n[11] +
				n[12] + n[13] + n[14] + n[15]) % 16;

			if (sum1 != 15 || sum2 != 15) {
				dev_dbg(&dev->dev, "checksum errors sum1=0x%X sum2=0x%X\n",
					sum1, sum2);
				data->state = STATE_INACTIVE;
				return -EINVAL;
			}

			subaddr  = n[0] << 4 | n[2];
			subaddr2 = n[8] << 4 | n[11];
			oem      = n[4] << 4 | n[5];
			addr     = n[6] << 4 | n[7];
			toggle   = n[10];
			obc1 = n[12] << 4 | n[13];
			obc2 = n[14] << 4 | n[15];
			if (subaddr != subaddr2) {
				dev_dbg(&dev->dev, "subaddress nibbles mismatch 0x%02X != 0x%02X\n",
					subaddr, subaddr2);
				data->state = STATE_INACTIVE;
				return -EINVAL;
			}
			if (oem != 0x44)
				dev_dbg(&dev->dev, "Warning: OEM nibbles 0x%02X. Expected 0x44\n",
					oem);

			scancode = addr << 24 | subaddr << 16 |
				   obc1 << 8 | obc2;
			dev_dbg(&dev->dev, "XMP scancode 0x%06x\n", scancode);

			if (toggle == 0) {
				rc_keydown(dev, RC_PROTO_XMP, scancode, 0);
			} else {
				rc_repeat(dev);
				dev_dbg(&dev->dev, "Repeat last key\n");
			}
			data->state = STATE_INACTIVE;

			return 0;

		} else if (geq_margin(ev.duration, XMP_HALFFRAME_SPACE, XMP_NIBBLE_PREFIX)) {
			/* Expect 8 or 16 nibble pulses. 16 in case of 'final' frame */
			if (data->count == 16) {
				dev_dbg(&dev->dev, "received half frame pulse at index %d. Probably a final frame key-up event: %u\n",
					data->count, ev.duration);
				/*
				 * TODO: for now go back to half frame position
				 *	 so trailer can be found and key press
				 *	 can be handled.
				 */
				data->count = 8;
			}

			else if (data->count != 8)
				dev_dbg(&dev->dev, "received half frame pulse at index %d: %u\n",
					data->count, ev.duration);
			data->state = STATE_LEADER_PULSE;

			return 0;

		} else if (geq_margin(ev.duration, XMP_NIBBLE_PREFIX, XMP_UNIT)) {
			/* store nibble raw data, decode after trailer */
			if (data->count == 16) {
				dev_dbg(&dev->dev, "to many pulses (%d) ignoring: %u\n",
					data->count, ev.duration);
				data->state = STATE_INACTIVE;
				return -EINVAL;
			}
			data->durations[data->count] = ev.duration;
			data->count++;
			data->state = STATE_LEADER_PULSE;

			return 0;

		}

		break;
	}

	dev_dbg(&dev->dev, "XMP decode failed at count %d state %d (%uus %s)\n",
		data->count, data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static struct ir_raw_handler xmp_handler = {
	.protocols	= RC_PROTO_BIT_XMP,
	.decode		= ir_xmp_decode,
	.min_timeout	= XMP_TRAILER_SPACE,
};

static int __init ir_xmp_decode_init(void)
{
	ir_raw_handler_register(&xmp_handler);

	printk(KERN_INFO "IR XMP protocol handler initialized\n");
	return 0;
}

static void __exit ir_xmp_decode_exit(void)
{
	ir_raw_handler_unregister(&xmp_handler);
}

module_init(ir_xmp_decode_init);
module_exit(ir_xmp_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marcel Mol <marcel@mesa.nl>");
MODULE_AUTHOR("MESA Consulting (http://www.mesa.nl)");
MODULE_DESCRIPTION("XMP IR protocol decoder");
