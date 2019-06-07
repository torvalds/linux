// SPDX-License-Identifier: GPL-2.0
// ir-nec-decoder.c - handle NEC IR Pulse/Space protocol
//
// Copyright (C) 2010 by Mauro Carvalho Chehab

#include <linux/bitrev.h>
#include <linux/module.h>
#include "rc-core-priv.h"

#define NEC_NBITS		32
#define NEC_UNIT		562500  /* ns */
#define NEC_HEADER_PULSE	(16 * NEC_UNIT)
#define NECX_HEADER_PULSE	(8  * NEC_UNIT) /* Less common NEC variant */
#define NEC_HEADER_SPACE	(8  * NEC_UNIT)
#define NEC_REPEAT_SPACE	(4  * NEC_UNIT)
#define NEC_BIT_PULSE		(1  * NEC_UNIT)
#define NEC_BIT_0_SPACE		(1  * NEC_UNIT)
#define NEC_BIT_1_SPACE		(3  * NEC_UNIT)
#define	NEC_TRAILER_PULSE	(1  * NEC_UNIT)
#define	NEC_TRAILER_SPACE	(10 * NEC_UNIT) /* even longer in reality */
#define NECX_REPEAT_BITS	1

enum nec_state {
	STATE_INACTIVE,
	STATE_HEADER_SPACE,
	STATE_BIT_PULSE,
	STATE_BIT_SPACE,
	STATE_TRAILER_PULSE,
	STATE_TRAILER_SPACE,
};

/**
 * ir_nec_decode() - Decode one NEC pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_nec_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct nec_dec *data = &dev->raw->nec;
	u32 scancode;
	enum rc_proto rc_proto;
	u8 address, not_address, command, not_command;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	dev_dbg(&dev->dev, "NEC decode started at state %d (%uus %s)\n",
		data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (eq_margin(ev.duration, NEC_HEADER_PULSE, NEC_UNIT * 2)) {
			data->is_nec_x = false;
			data->necx_repeat = false;
		} else if (eq_margin(ev.duration, NECX_HEADER_PULSE, NEC_UNIT / 2))
			data->is_nec_x = true;
		else
			break;

		data->count = 0;
		data->state = STATE_HEADER_SPACE;
		return 0;

	case STATE_HEADER_SPACE:
		if (ev.pulse)
			break;

		if (eq_margin(ev.duration, NEC_HEADER_SPACE, NEC_UNIT)) {
			data->state = STATE_BIT_PULSE;
			return 0;
		} else if (eq_margin(ev.duration, NEC_REPEAT_SPACE, NEC_UNIT / 2)) {
			data->state = STATE_TRAILER_PULSE;
			return 0;
		}

		break;

	case STATE_BIT_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, NEC_BIT_PULSE, NEC_UNIT / 2))
			break;

		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_SPACE:
		if (ev.pulse)
			break;

		if (data->necx_repeat && data->count == NECX_REPEAT_BITS &&
		    geq_margin(ev.duration, NEC_TRAILER_SPACE, NEC_UNIT / 2)) {
			dev_dbg(&dev->dev, "Repeat last key\n");
			rc_repeat(dev);
			data->state = STATE_INACTIVE;
			return 0;
		} else if (data->count > NECX_REPEAT_BITS)
			data->necx_repeat = false;

		data->bits <<= 1;
		if (eq_margin(ev.duration, NEC_BIT_1_SPACE, NEC_UNIT / 2))
			data->bits |= 1;
		else if (!eq_margin(ev.duration, NEC_BIT_0_SPACE, NEC_UNIT / 2))
			break;
		data->count++;

		if (data->count == NEC_NBITS)
			data->state = STATE_TRAILER_PULSE;
		else
			data->state = STATE_BIT_PULSE;

		return 0;

	case STATE_TRAILER_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, NEC_TRAILER_PULSE, NEC_UNIT / 2))
			break;

		data->state = STATE_TRAILER_SPACE;
		return 0;

	case STATE_TRAILER_SPACE:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, NEC_TRAILER_SPACE, NEC_UNIT / 2))
			break;

		if (data->count == NEC_NBITS) {
			address     = bitrev8((data->bits >> 24) & 0xff);
			not_address = bitrev8((data->bits >> 16) & 0xff);
			command	    = bitrev8((data->bits >>  8) & 0xff);
			not_command = bitrev8((data->bits >>  0) & 0xff);

			scancode = ir_nec_bytes_to_scancode(address,
							    not_address,
							    command,
							    not_command,
							    &rc_proto);

			if (data->is_nec_x)
				data->necx_repeat = true;

			rc_keydown(dev, rc_proto, scancode, 0);
		} else {
			rc_repeat(dev);
		}

		data->state = STATE_INACTIVE;
		return 0;
	}

	dev_dbg(&dev->dev, "NEC decode failed at count %d state %d (%uus %s)\n",
		data->count, data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

/**
 * ir_nec_scancode_to_raw() - encode an NEC scancode ready for modulation.
 * @protocol:	specific protocol to use
 * @scancode:	a single NEC scancode.
 */
static u32 ir_nec_scancode_to_raw(enum rc_proto protocol, u32 scancode)
{
	unsigned int addr, addr_inv, data, data_inv;

	data = scancode & 0xff;

	if (protocol == RC_PROTO_NEC32) {
		/* 32-bit NEC (used by Apple and TiVo remotes) */
		/* scan encoding: aaAAddDD */
		addr_inv   = (scancode >> 24) & 0xff;
		addr       = (scancode >> 16) & 0xff;
		data_inv   = (scancode >>  8) & 0xff;
	} else if (protocol == RC_PROTO_NECX) {
		/* Extended NEC */
		/* scan encoding AAaaDD */
		addr       = (scancode >> 16) & 0xff;
		addr_inv   = (scancode >>  8) & 0xff;
		data_inv   = data ^ 0xff;
	} else {
		/* Normal NEC */
		/* scan encoding: AADD */
		addr       = (scancode >>  8) & 0xff;
		addr_inv   = addr ^ 0xff;
		data_inv   = data ^ 0xff;
	}

	/* raw encoding: ddDDaaAA */
	return data_inv << 24 |
	       data     << 16 |
	       addr_inv <<  8 |
	       addr;
}

static const struct ir_raw_timings_pd ir_nec_timings = {
	.header_pulse	= NEC_HEADER_PULSE,
	.header_space	= NEC_HEADER_SPACE,
	.bit_pulse	= NEC_BIT_PULSE,
	.bit_space[0]	= NEC_BIT_0_SPACE,
	.bit_space[1]	= NEC_BIT_1_SPACE,
	.trailer_pulse	= NEC_TRAILER_PULSE,
	.trailer_space	= NEC_TRAILER_SPACE,
	.msb_first	= 0,
};

/**
 * ir_nec_encode() - Encode a scancode as a stream of raw events
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
static int ir_nec_encode(enum rc_proto protocol, u32 scancode,
			 struct ir_raw_event *events, unsigned int max)
{
	struct ir_raw_event *e = events;
	int ret;
	u32 raw;

	/* Convert a NEC scancode to raw NEC data */
	raw = ir_nec_scancode_to_raw(protocol, scancode);

	/* Modulate the raw data using a pulse distance modulation */
	ret = ir_raw_gen_pd(&e, max, &ir_nec_timings, NEC_NBITS, raw);
	if (ret < 0)
		return ret;

	return e - events;
}

static struct ir_raw_handler nec_handler = {
	.protocols	= RC_PROTO_BIT_NEC | RC_PROTO_BIT_NECX |
							RC_PROTO_BIT_NEC32,
	.decode		= ir_nec_decode,
	.encode		= ir_nec_encode,
	.carrier	= 38000,
	.min_timeout	= NEC_TRAILER_SPACE,
};

static int __init ir_nec_decode_init(void)
{
	ir_raw_handler_register(&nec_handler);

	printk(KERN_INFO "IR NEC protocol handler initialized\n");
	return 0;
}

static void __exit ir_nec_decode_exit(void)
{
	ir_raw_handler_unregister(&nec_handler);
}

module_init(ir_nec_decode_init);
module_exit(ir_nec_decode_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("NEC IR protocol decoder");
