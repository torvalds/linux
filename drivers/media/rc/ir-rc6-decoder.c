// SPDX-License-Identifier: GPL-2.0-only
/* ir-rc6-decoder.c - A decoder for the RC6 IR protocol
 *
 * Copyright (C) 2010 by David Härdeman <david@hardeman.nu>
 */

#include "rc-core-priv.h"
#include <linux/module.h>

/*
 * This decoder currently supports:
 * RC6-0-16	(standard toggle bit in header)
 * RC6-6A-20	(no toggle bit)
 * RC6-6A-24	(no toggle bit)
 * RC6-6A-32	(MCE version with toggle bit in body)
 */

#define RC6_UNIT		444444	/* nanosecs */
#define RC6_HEADER_NBITS	4	/* not including toggle bit */
#define RC6_0_NBITS		16
#define RC6_6A_32_NBITS		32
#define RC6_6A_NBITS		128	/* Variable 8..128 */
#define RC6_PREFIX_PULSE	(6 * RC6_UNIT)
#define RC6_PREFIX_SPACE	(2 * RC6_UNIT)
#define RC6_BIT_START		(1 * RC6_UNIT)
#define RC6_BIT_END		(1 * RC6_UNIT)
#define RC6_TOGGLE_START	(2 * RC6_UNIT)
#define RC6_TOGGLE_END		(2 * RC6_UNIT)
#define RC6_SUFFIX_SPACE	(6 * RC6_UNIT)
#define RC6_MODE_MASK		0x07	/* for the header bits */
#define RC6_STARTBIT_MASK	0x08	/* for the header bits */
#define RC6_6A_MCE_TOGGLE_MASK	0x8000	/* for the body bits */
#define RC6_6A_LCC_MASK		0xffff0000 /* RC6-6A-32 long customer code mask */
#define RC6_6A_MCE_CC		0x800f0000 /* MCE customer code */
#define RC6_6A_ZOTAC_CC		0x80340000 /* Zotac customer code */
#define RC6_6A_KATHREIN_CC	0x80460000 /* Kathrein RCU-676 customer code */
#ifndef CHAR_BIT
#define CHAR_BIT 8	/* Normally in <limits.h> */
#endif

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
		fallthrough;
	default:
		return RC6_MODE_UNKNOWN;
	}
}

/**
 * ir_rc6_decode() - Decode one RC6 pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rc6_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct rc6_dec *data = &dev->raw->rc6;
	u32 scancode;
	u8 toggle;
	enum rc_proto protocol;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, RC6_UNIT, RC6_UNIT / 2))
		goto out;

again:
	dev_dbg(&dev->dev, "RC6 decode started at state %i (%uus %s)\n",
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
		data->header = 0;
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
		if (!(data->header & RC6_STARTBIT_MASK)) {
			dev_dbg(&dev->dev, "RC6 invalid start bit\n");
			break;
		}

		data->state = STATE_BODY_BIT_START;
		decrease_duration(&ev, RC6_TOGGLE_END);
		data->count = 0;
		data->body = 0;

		switch (rc6_mode(data)) {
		case RC6_MODE_0:
			data->wanted_bits = RC6_0_NBITS;
			break;
		case RC6_MODE_6A:
			data->wanted_bits = RC6_6A_NBITS;
			break;
		default:
			dev_dbg(&dev->dev, "RC6 unknown mode\n");
			goto out;
		}
		goto again;

	case STATE_BODY_BIT_START:
		if (eq_margin(ev.duration, RC6_BIT_START, RC6_UNIT / 2)) {
			/* Discard LSB's that won't fit in data->body */
			if (data->count++ < CHAR_BIT * sizeof data->body) {
				data->body <<= 1;
				if (ev.pulse)
					data->body |= 1;
			}
			data->state = STATE_BODY_BIT_END;
			return 0;
		} else if (RC6_MODE_6A == rc6_mode(data) && !ev.pulse &&
				geq_margin(ev.duration, RC6_SUFFIX_SPACE, RC6_UNIT / 2)) {
			data->state = STATE_FINISHED;
			goto again;
		}
		break;

	case STATE_BODY_BIT_END:
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
			scancode = data->body;
			toggle = data->toggle;
			protocol = RC_PROTO_RC6_0;
			dev_dbg(&dev->dev, "RC6(0) scancode 0x%04x (toggle: %u)\n",
				scancode, toggle);
			break;

		case RC6_MODE_6A:
			if (data->count > CHAR_BIT * sizeof data->body) {
				dev_dbg(&dev->dev, "RC6 too many (%u) data bits\n",
					data->count);
				goto out;
			}

			scancode = data->body;
			switch (data->count) {
			case 20:
				protocol = RC_PROTO_RC6_6A_20;
				toggle = 0;
				break;
			case 24:
				protocol = RC_PROTO_RC6_6A_24;
				toggle = 0;
				break;
			case 32:
				switch (scancode & RC6_6A_LCC_MASK) {
				case RC6_6A_MCE_CC:
				case RC6_6A_KATHREIN_CC:
				case RC6_6A_ZOTAC_CC:
					protocol = RC_PROTO_RC6_MCE;
					toggle = !!(scancode & RC6_6A_MCE_TOGGLE_MASK);
					scancode &= ~RC6_6A_MCE_TOGGLE_MASK;
					break;
				default:
					protocol = RC_PROTO_RC6_6A_32;
					toggle = 0;
					break;
				}
				break;
			default:
				dev_dbg(&dev->dev, "RC6(6A) unsupported length\n");
				goto out;
			}

			dev_dbg(&dev->dev, "RC6(6A) proto 0x%04x, scancode 0x%08x (toggle: %u)\n",
				protocol, scancode, toggle);
			break;
		default:
			dev_dbg(&dev->dev, "RC6 unknown mode\n");
			goto out;
		}

		rc_keydown(dev, protocol, scancode, toggle);
		data->state = STATE_INACTIVE;
		return 0;
	}

out:
	dev_dbg(&dev->dev, "RC6 decode failed at state %i (%uus %s)\n",
		data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static const struct ir_raw_timings_manchester ir_rc6_timings[4] = {
	{
		.leader_pulse		= RC6_PREFIX_PULSE,
		.leader_space		= RC6_PREFIX_SPACE,
		.clock			= RC6_UNIT,
		.invert			= 1,
	},
	{
		.clock			= RC6_UNIT * 2,
		.invert			= 1,
	},
	{
		.clock			= RC6_UNIT,
		.invert			= 1,
		.trailer_space		= RC6_SUFFIX_SPACE,
	},
};

/**
 * ir_rc6_encode() - Encode a scancode as a stream of raw events
 *
 * @protocol:	protocol to encode
 * @scancode:	scancode to encode
 * @events:	array of raw ir events to write into
 * @max:	maximum size of @events
 *
 * Returns:	The number of events written.
 *		-ENOBUFS if there isn't enough space in the array to fit the
 *		encoding. In this case all @max events will have been written.
 *		-EINVAL if the scancode is ambiguous or invalid.
 */
static int ir_rc6_encode(enum rc_proto protocol, u32 scancode,
			 struct ir_raw_event *events, unsigned int max)
{
	int ret;
	struct ir_raw_event *e = events;

	if (protocol == RC_PROTO_RC6_0) {
		/* Modulate the header (Start Bit & Mode-0) */
		ret = ir_raw_gen_manchester(&e, max - (e - events),
					    &ir_rc6_timings[0],
					    RC6_HEADER_NBITS, (1 << 3));
		if (ret < 0)
			return ret;

		/* Modulate Trailer Bit */
		ret = ir_raw_gen_manchester(&e, max - (e - events),
					    &ir_rc6_timings[1], 1, 0);
		if (ret < 0)
			return ret;

		/* Modulate rest of the data */
		ret = ir_raw_gen_manchester(&e, max - (e - events),
					    &ir_rc6_timings[2], RC6_0_NBITS,
					    scancode);
		if (ret < 0)
			return ret;

	} else {
		int bits;

		switch (protocol) {
		case RC_PROTO_RC6_MCE:
		case RC_PROTO_RC6_6A_32:
			bits = 32;
			break;
		case RC_PROTO_RC6_6A_24:
			bits = 24;
			break;
		case RC_PROTO_RC6_6A_20:
			bits = 20;
			break;
		default:
			return -EINVAL;
		}

		/* Modulate the header (Start Bit & Header-version 6 */
		ret = ir_raw_gen_manchester(&e, max - (e - events),
					    &ir_rc6_timings[0],
					    RC6_HEADER_NBITS, (1 << 3 | 6));
		if (ret < 0)
			return ret;

		/* Modulate Trailer Bit */
		ret = ir_raw_gen_manchester(&e, max - (e - events),
					    &ir_rc6_timings[1], 1, 0);
		if (ret < 0)
			return ret;

		/* Modulate rest of the data */
		ret = ir_raw_gen_manchester(&e, max - (e - events),
					    &ir_rc6_timings[2],
					    bits,
					    scancode);
		if (ret < 0)
			return ret;
	}

	return e - events;
}

static struct ir_raw_handler rc6_handler = {
	.protocols	= RC_PROTO_BIT_RC6_0 | RC_PROTO_BIT_RC6_6A_20 |
			  RC_PROTO_BIT_RC6_6A_24 | RC_PROTO_BIT_RC6_6A_32 |
			  RC_PROTO_BIT_RC6_MCE,
	.decode		= ir_rc6_decode,
	.encode		= ir_rc6_encode,
	.carrier	= 36000,
	.min_timeout	= RC6_SUFFIX_SPACE,
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
