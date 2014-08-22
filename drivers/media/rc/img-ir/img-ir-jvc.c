/*
 * ImgTec IR Decoder setup for JVC protocol.
 *
 * Copyright 2012-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "img-ir-hw.h"

/* Convert JVC data to a scancode */
static int img_ir_jvc_scancode(int len, u64 raw, enum rc_type *protocol,
			       u32 *scancode, u64 enabled_protocols)
{
	unsigned int cust, data;

	if (len != 16)
		return -EINVAL;

	cust = (raw >> 0) & 0xff;
	data = (raw >> 8) & 0xff;

	*protocol = RC_TYPE_JVC;
	*scancode = cust << 8 | data;
	return IMG_IR_SCANCODE;
}

/* Convert JVC scancode to JVC data filter */
static int img_ir_jvc_filter(const struct rc_scancode_filter *in,
			     struct img_ir_filter *out, u64 protocols)
{
	unsigned int cust, data;
	unsigned int cust_m, data_m;

	cust   = (in->data >> 8) & 0xff;
	cust_m = (in->mask >> 8) & 0xff;
	data   = (in->data >> 0) & 0xff;
	data_m = (in->mask >> 0) & 0xff;

	out->data = cust   | data << 8;
	out->mask = cust_m | data_m << 8;

	return 0;
}

/*
 * JVC decoder
 * See also http://www.sbprojects.com/knowledge/ir/jvc.php
 *          http://support.jvc.com/consumer/support/documents/RemoteCodes.pdf
 */
struct img_ir_decoder img_ir_jvc = {
	.type = RC_BIT_JVC,
	.control = {
		.decoden = 1,
		.code_type = IMG_IR_CODETYPE_PULSEDIST,
	},
	/* main timings */
	.unit = 527500, /* 527.5 us */
	.timings = {
		/* leader symbol */
		.ldr = {
			.pulse = { 16	/* 8.44 ms */ },
			.space = { 8	/* 4.22 ms */ },
		},
		/* 0 symbol */
		.s00 = {
			.pulse = { 1	/* 527.5 us +-60 us */ },
			.space = { 1	/* 527.5 us */ },
		},
		/* 1 symbol */
		.s01 = {
			.pulse = { 1	/* 527.5 us +-60 us */ },
			.space = { 3	/* 1.5825 ms +-40 us */ },
		},
		/* free time */
		.ft = {
			.minlen = 16,
			.maxlen = 16,
			.ft_min = 10,	/* 5.275 ms */
		},
	},
	/* scancode logic */
	.scancode = img_ir_jvc_scancode,
	.filter = img_ir_jvc_filter,
};
