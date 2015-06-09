/*
 * ImgTec IR Decoder setup for Sanyo protocol.
 *
 * Copyright 2012-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * From ir-sanyo-decoder.c:
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

#include "img-ir-hw.h"

/* Convert Sanyo data to a scancode */
static int img_ir_sanyo_scancode(int len, u64 raw, u64 enabled_protocols,
				 struct img_ir_scancode_req *request)
{
	unsigned int addr, addr_inv, data, data_inv;
	/* a repeat code has no data */
	if (!len)
		return IMG_IR_REPEATCODE;
	if (len != 42)
		return -EINVAL;
	addr     = (raw >>  0) & 0x1fff;
	addr_inv = (raw >> 13) & 0x1fff;
	data     = (raw >> 26) & 0xff;
	data_inv = (raw >> 34) & 0xff;
	/* Validate data */
	if ((data_inv ^ data) != 0xff)
		return -EINVAL;
	/* Validate address */
	if ((addr_inv ^ addr) != 0x1fff)
		return -EINVAL;

	/* Normal Sanyo */
	request->protocol = RC_TYPE_SANYO;
	request->scancode = addr << 8 | data;
	return IMG_IR_SCANCODE;
}

/* Convert Sanyo scancode to Sanyo data filter */
static int img_ir_sanyo_filter(const struct rc_scancode_filter *in,
			       struct img_ir_filter *out, u64 protocols)
{
	unsigned int addr, addr_inv, data, data_inv;
	unsigned int addr_m, data_m;

	data = in->data & 0xff;
	data_m = in->mask & 0xff;
	data_inv = data ^ 0xff;

	if (in->data & 0xff700000)
		return -EINVAL;

	addr       = (in->data >> 8) & 0x1fff;
	addr_m     = (in->mask >> 8) & 0x1fff;
	addr_inv   = addr ^ 0x1fff;

	out->data = (u64)data_inv << 34 |
		    (u64)data     << 26 |
			 addr_inv << 13 |
			 addr;
	out->mask = (u64)data_m << 34 |
		    (u64)data_m << 26 |
			 addr_m << 13 |
			 addr_m;
	return 0;
}

/* Sanyo decoder */
struct img_ir_decoder img_ir_sanyo = {
	.type = RC_BIT_SANYO,
	.control = {
		.decoden = 1,
		.code_type = IMG_IR_CODETYPE_PULSEDIST,
	},
	/* main timings */
	.unit = 562500, /* 562.5 us */
	.timings = {
		/* leader symbol */
		.ldr = {
			.pulse = { 16	/* 9ms */ },
			.space = { 8	/* 4.5ms */ },
		},
		/* 0 symbol */
		.s00 = {
			.pulse = { 1	/* 562.5 us */ },
			.space = { 1	/* 562.5 us */ },
		},
		/* 1 symbol */
		.s01 = {
			.pulse = { 1	/* 562.5 us */ },
			.space = { 3	/* 1687.5 us */ },
		},
		/* free time */
		.ft = {
			.minlen = 42,
			.maxlen = 42,
			.ft_min = 10,	/* 5.625 ms */
		},
	},
	/* repeat codes */
	.repeat = 108,			/* 108 ms */
	.rtimings = {
		/* leader symbol */
		.ldr = {
			.space = { 4	/* 2.25 ms */ },
		},
		/* free time */
		.ft = {
			.minlen = 0,	/* repeat code has no data */
			.maxlen = 0,
		},
	},
	/* scancode logic */
	.scancode = img_ir_sanyo_scancode,
	.filter = img_ir_sanyo_filter,
};
