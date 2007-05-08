/*
 *  ecard.h
 *
 *  Copyright 2007 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Definitions internal to ecard.c - for it's use only!!
 *
 * External expansion card header as read from the card
 */
struct ex_ecid {
	unsigned char	r_irq:1;
	unsigned char	r_zero:1;
	unsigned char	r_fiq:1;
	unsigned char	r_id:4;
	unsigned char	r_a:1;

	unsigned char	r_cd:1;
	unsigned char	r_is:1;
	unsigned char	r_w:2;
	unsigned char	r_r1:4;

	unsigned char	r_r2:8;

	unsigned char	r_prod[2];

	unsigned char	r_manu[2];

	unsigned char	r_country;

	unsigned char	r_fiqmask;
	unsigned char	r_fiqoff[3];

	unsigned char	r_irqmask;
	unsigned char	r_irqoff[3];
};

/*
 * Chunk directory entry as read from the card
 */
struct ex_chunk_dir {
	unsigned char r_id;
	unsigned char r_len[3];
	unsigned long r_start;
	union {
		char string[256];
		char data[1];
	} d;
#define c_id(x)		((x)->r_id)
#define c_len(x)	((x)->r_len[0]|((x)->r_len[1]<<8)|((x)->r_len[2]<<16))
#define c_start(x)	((x)->r_start)
};
