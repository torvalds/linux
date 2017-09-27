/*
 * ImgTec IR Decoder setup for Philips RC-5 protocol.
 *
 * Copyright 2012-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "img-ir-hw.h"

/* Convert RC5 data to a scancode */
static int img_ir_rc5_scancode(int len, u64 raw, u64 enabled_protocols,
				struct img_ir_scancode_req *request)
{
	unsigned int addr, cmd, tgl, start;

	/* Quirk in the decoder shifts everything by 2 to the left. */
	raw   >>= 2;

	start	=  (raw >> 13)	& 0x01;
	tgl	=  (raw >> 11)	& 0x01;
	addr	=  (raw >>  6)	& 0x1f;
	cmd	=   raw		& 0x3f;
	/*
	 * 12th bit is used to extend the command in extended RC5 and has
	 * no effect on standard RC5.
	 */
	cmd	+= ((raw >> 12) & 0x01) ? 0 : 0x40;

	if (!start)
		return -EINVAL;

	request->protocol = RC_PROTO_RC5;
	request->scancode = addr << 8 | cmd;
	request->toggle   = tgl;
	return IMG_IR_SCANCODE;
}

/* Convert RC5 scancode to RC5 data filter */
static int img_ir_rc5_filter(const struct rc_scancode_filter *in,
				 struct img_ir_filter *out, u64 protocols)
{
	/* Not supported by the hw. */
	return -EINVAL;
}

/*
 * RC-5 decoder
 * see http://www.sbprojects.com/knowledge/ir/rc5.php
 */
struct img_ir_decoder img_ir_rc5 = {
	.type      = RC_PROTO_BIT_RC5,
	.control   = {
		.bitoriend2	= 1,
		.code_type	= IMG_IR_CODETYPE_BIPHASE,
		.decodend2	= 1,
	},
	/* main timings */
	.tolerance	= 16,
	.unit		= 888888, /* 1/36k*32=888.888microseconds */
	.timings	= {
		/* 10 symbol */
		.s10 = {
			.pulse	= { 1 },
			.space	= { 1 },
		},

		/* 11 symbol */
		.s11 = {
			.pulse	= { 1 },
			.space	= { 1 },
		},

		/* free time */
		.ft  = {
			.minlen = 14,
			.maxlen = 14,
			.ft_min = 5,
		},
	},

	/* scancode logic */
	.scancode	= img_ir_rc5_scancode,
	.filter		= img_ir_rc5_filter,
};
