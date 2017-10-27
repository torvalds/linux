/*
 * ImgTec IR Decoder setup for Philips RC-6 protocol.
 *
 * Copyright 2012-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "img-ir-hw.h"

/* Convert RC6 data to a scancode */
static int img_ir_rc6_scancode(int len, u64 raw, u64 enabled_protocols,
				struct img_ir_scancode_req *request)
{
	unsigned int addr, cmd, mode, trl1, trl2;

	/*
	 * Due to a side effect of the decoder handling the double length
	 * Trailer bit, the header information is a bit scrambled, and the
	 * raw data is shifted incorrectly.
	 * This workaround effectively recovers the header bits.
	 *
	 * The Header field should look like this:
	 *
	 * StartBit ModeBit2 ModeBit1 ModeBit0 TrailerBit
	 *
	 * But what we get is:
	 *
	 * ModeBit2 ModeBit1 ModeBit0 TrailerBit1 TrailerBit2
	 *
	 * The start bit is not important to recover the scancode.
	 */

	raw	>>= 27;

	trl1	= (raw >>  17)	& 0x01;
	trl2	= (raw >>  16)	& 0x01;

	mode	= (raw >>  18)	& 0x07;
	addr	= (raw >>   8)	& 0xff;
	cmd	=  raw		& 0xff;

	/*
	 * Due to the above explained irregularity the trailer bits cannot
	 * have the same value.
	 */
	if (trl1 == trl2)
		return -EINVAL;

	/* Only mode 0 supported for now */
	if (mode)
		return -EINVAL;

	request->protocol = RC_PROTO_RC6_0;
	request->scancode = addr << 8 | cmd;
	request->toggle	  = trl2;
	return IMG_IR_SCANCODE;
}

/* Convert RC6 scancode to RC6 data filter */
static int img_ir_rc6_filter(const struct rc_scancode_filter *in,
				 struct img_ir_filter *out, u64 protocols)
{
	/* Not supported by the hw. */
	return -EINVAL;
}

/*
 * RC-6 decoder
 * see http://www.sbprojects.com/knowledge/ir/rc6.php
 */
struct img_ir_decoder img_ir_rc6 = {
	.type		= RC_PROTO_BIT_RC6_0,
	.control	= {
		.bitorien	= 1,
		.code_type	= IMG_IR_CODETYPE_BIPHASE,
		.decoden	= 1,
		.decodinpol	= 1,
	},
	/* main timings */
	.tolerance	= 20,
	/*
	 * Due to a quirk in the img-ir decoder, default header values do
	 * not work, the values described below were extracted from
	 * successful RTL test cases.
	 */
	.timings	= {
		/* leader symbol */
		.ldr = {
			.pulse	= { 650 },
			.space	= { 660 },
		},
		/* 0 symbol */
		.s00 = {
			.pulse	= { 370 },
			.space	= { 370 },
		},
		/* 01 symbol */
		.s01 = {
			.pulse	= { 370 },
			.space	= { 370 },
		},
		/* free time */
		.ft  = {
			.minlen = 21,
			.maxlen = 21,
			.ft_min = 2666,	/* 2.666 ms */
		},
	},

	/* scancode logic */
	.scancode	= img_ir_rc6_scancode,
	.filter		= img_ir_rc6_filter,
};
