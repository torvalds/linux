// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ImgTec IR Decoder setup for Sony (SIRC) protocol.
 *
 * Copyright 2012-2014 Imagination Technologies Ltd.
 */

#include "img-ir-hw.h"

/* Convert Sony data to a scancode */
static int img_ir_sony_scancode(int len, u64 raw, u64 enabled_protocols,
				struct img_ir_scancode_req *request)
{
	unsigned int dev, subdev, func;

	switch (len) {
	case 12:
		if (!(enabled_protocols & RC_PROTO_BIT_SONY12))
			return -EINVAL;
		func   = raw & 0x7f;	/* first 7 bits */
		raw    >>= 7;
		dev    = raw & 0x1f;	/* next 5 bits */
		subdev = 0;
		request->protocol = RC_PROTO_SONY12;
		break;
	case 15:
		if (!(enabled_protocols & RC_PROTO_BIT_SONY15))
			return -EINVAL;
		func   = raw & 0x7f;	/* first 7 bits */
		raw    >>= 7;
		dev    = raw & 0xff;	/* next 8 bits */
		subdev = 0;
		request->protocol = RC_PROTO_SONY15;
		break;
	case 20:
		if (!(enabled_protocols & RC_PROTO_BIT_SONY20))
			return -EINVAL;
		func   = raw & 0x7f;	/* first 7 bits */
		raw    >>= 7;
		dev    = raw & 0x1f;	/* next 5 bits */
		raw    >>= 5;
		subdev = raw & 0xff;	/* next 8 bits */
		request->protocol = RC_PROTO_SONY20;
		break;
	default:
		return -EINVAL;
	}
	request->scancode = dev << 16 | subdev << 8 | func;
	return IMG_IR_SCANCODE;
}

/* Convert NEC scancode to NEC data filter */
static int img_ir_sony_filter(const struct rc_scancode_filter *in,
			      struct img_ir_filter *out, u64 protocols)
{
	unsigned int dev, subdev, func;
	unsigned int dev_m, subdev_m, func_m;
	unsigned int len = 0;

	dev      = (in->data >> 16) & 0xff;
	dev_m    = (in->mask >> 16) & 0xff;
	subdev   = (in->data >> 8)  & 0xff;
	subdev_m = (in->mask >> 8)  & 0xff;
	func     = (in->data >> 0)  & 0x7f;
	func_m   = (in->mask >> 0)  & 0x7f;

	protocols &= RC_PROTO_BIT_SONY12 | RC_PROTO_BIT_SONY15 |
							RC_PROTO_BIT_SONY20;

	/*
	 * If only one bit is set, we were requested to do an exact
	 * protocol. This should be the case for wakeup filters; for
	 * normal filters, guess the protocol from the scancode.
	 */
	if (!is_power_of_2(protocols)) {
		if (subdev & subdev_m)
			protocols = RC_PROTO_BIT_SONY20;
		else if (dev & dev_m & 0xe0)
			protocols = RC_PROTO_BIT_SONY15;
		else
			protocols = RC_PROTO_BIT_SONY12;
	}

	if (protocols == RC_PROTO_BIT_SONY20) {
		/* can't encode subdev and higher device bits */
		if (dev & dev_m & 0xe0)
			return -EINVAL;
		len = 20;
		dev_m &= 0x1f;
	} else if (protocols == RC_PROTO_BIT_SONY15) {
		len = 15;
		subdev_m = 0;
	} else {
		/*
		 * The hardware mask cannot distinguish high device bits and low
		 * extended bits, so logically AND those bits of the masks
		 * together.
		 */
		subdev_m &= (dev_m >> 5) | 0xf8;
		dev_m &= 0x1f;
	}

	/* ensure there aren't any bits straying between fields */
	dev &= dev_m;
	subdev &= subdev_m;

	/* write the hardware filter */
	out->data = func          |
		    dev      << 7 |
		    subdev   << 15;
	out->mask = func_m        |
		    dev_m    << 7 |
		    subdev_m << 15;

	if (len) {
		out->minlen = len;
		out->maxlen = len;
	}
	return 0;
}

/*
 * Sony SIRC decoder
 * See also http://www.sbprojects.com/knowledge/ir/sirc.php
 *          http://picprojects.org.uk/projects/sirc/sonysirc.pdf
 */
struct img_ir_decoder img_ir_sony = {
	.type = RC_PROTO_BIT_SONY12 | RC_PROTO_BIT_SONY15 | RC_PROTO_BIT_SONY20,
	.control = {
		.decoden = 1,
		.code_type = IMG_IR_CODETYPE_PULSELEN,
	},
	/* main timings */
	.unit = 600000, /* 600 us */
	.timings = {
		/* leader symbol */
		.ldr = {
			.pulse = { 4	/* 2.4 ms */ },
			.space = { 1	/* 600 us */ },
		},
		/* 0 symbol */
		.s00 = {
			.pulse = { 1	/* 600 us */ },
			.space = { 1	/* 600 us */ },
		},
		/* 1 symbol */
		.s01 = {
			.pulse = { 2	/* 1.2 ms */ },
			.space = { 1	/* 600 us */ },
		},
		/* free time */
		.ft = {
			.minlen = 12,
			.maxlen = 20,
			.ft_min = 10,	/* 6 ms */
		},
	},
	/* scancode logic */
	.scancode = img_ir_sony_scancode,
	.filter = img_ir_sony_filter,
};
