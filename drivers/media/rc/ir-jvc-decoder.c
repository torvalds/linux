// SPDX-License-Identifier: GPL-2.0-only
/* ir-jvc-decoder.c - handle JVC IR Pulse/Space protocol
 *
 * Copyright (C) 2010 by David Härdeman <david@hardeman.nu>
 */

#include <linux/bitrev.h>
#include <linux/module.h>
#include "rc-core-priv.h"

#define JVC_NBITS		16		/* dev(8) + func(8) */
#define JVC_UNIT		525		/* us */
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
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:   the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_jvc_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct jvc_dec *data = &dev->raw->jvc;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, JVC_UNIT, JVC_UNIT / 2))
		goto out;

	dev_dbg(&dev->dev, "JVC decode started at state %d (%uus %s)\n",
		data->state, ev.duration, TO_STR(ev.pulse));

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
			dev_dbg(&dev->dev, "JVC scancode 0x%04x\n", scancode);
			rc_keydown(dev, RC_PROTO_JVC, scancode, data->toggle);
			data->first = false;
			data->old_bits = data->bits;
		} else if (data->bits == data->old_bits) {
			dev_dbg(&dev->dev, "JVC repeat\n");
			rc_repeat(dev);
		} else {
			dev_dbg(&dev->dev, "JVC invalid repeat msg\n");
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
	dev_dbg(&dev->dev, "JVC decode failed at state %d (%uus %s)\n",
		data->state, ev.duration, TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static const struct ir_raw_timings_pd ir_jvc_timings = {
	.header_pulse  = JVC_HEADER_PULSE,
	.header_space  = JVC_HEADER_SPACE,
	.bit_pulse     = JVC_BIT_PULSE,
	.bit_space[0]  = JVC_BIT_0_SPACE,
	.bit_space[1]  = JVC_BIT_1_SPACE,
	.trailer_pulse = JVC_TRAILER_PULSE,
	.trailer_space = JVC_TRAILER_SPACE,
	.msb_first     = 1,
};

/**
 * ir_jvc_encode() - Encode a scancode as a stream of raw events
 *
 * @protocol:	protocol to encode
 * @scancode:	scancode to encode
 * @events:	array of raw ir events to write into
 * @max:	maximum size of @events
 *
 * Returns:	The number of events written.
 *		-ENOBUFS if there isn't enough space in the array to fit the
 *		encoding. In this case all @max events will have been written.
 */
static int ir_jvc_encode(enum rc_proto protocol, u32 scancode,
			 struct ir_raw_event *events, unsigned int max)
{
	struct ir_raw_event *e = events;
	int ret;
	u32 raw = (bitrev8((scancode >> 8) & 0xff) << 8) |
		  (bitrev8((scancode >> 0) & 0xff) << 0);

	ret = ir_raw_gen_pd(&e, max, &ir_jvc_timings, JVC_NBITS, raw);
	if (ret < 0)
		return ret;

	return e - events;
}

static struct ir_raw_handler jvc_handler = {
	.protocols	= RC_PROTO_BIT_JVC,
	.decode		= ir_jvc_decode,
	.encode		= ir_jvc_encode,
	.carrier	= 38000,
	.min_timeout	= JVC_TRAILER_SPACE,
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
