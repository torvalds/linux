/* Linux driver for Philips webcam
   Decompression for chipset version 2 et 3
   (C) 2004-2006  Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#include "pwc-timon.h"
#include "pwc-kiara.h"
#include "pwc-dec23.h"

#include <linux/string.h>
#include <linux/slab.h>

/*
 * USE_LOOKUP_TABLE_TO_CLAMP
 *   0: use a C version of this tests:  {  a<0?0:(a>255?255:a) }
 *   1: use a faster lookup table for cpu with a big cache (intel)
 */
#define USE_LOOKUP_TABLE_TO_CLAMP	1
/*
 * UNROLL_LOOP_FOR_COPYING_BLOCK
 *   0: use a loop for a smaller code (but little slower)
 *   1: when unrolling the loop, gcc produces some faster code (perhaps only
 *   valid for intel processor class). Activating this option, automaticaly
 *   activate USE_LOOKUP_TABLE_TO_CLAMP
 */
#define UNROLL_LOOP_FOR_COPY		1
#if UNROLL_LOOP_FOR_COPY
# undef USE_LOOKUP_TABLE_TO_CLAMP
# define USE_LOOKUP_TABLE_TO_CLAMP 1
#endif

static void build_subblock_pattern(struct pwc_dec23_private *pdec)
{
	static const unsigned int initial_values[12] = {
		-0x526500, -0x221200, 0x221200, 0x526500,
			   -0x3de200, 0x3de200,
		-0x6db480, -0x2d5d00, 0x2d5d00, 0x6db480,
			   -0x12c200, 0x12c200

	};
	static const unsigned int values_derivated[12] = {
		0xa4ca, 0x4424, -0x4424, -0xa4ca,
			0x7bc4, -0x7bc4,
		0xdb69, 0x5aba, -0x5aba, -0xdb69,
			0x2584, -0x2584
	};
	unsigned int temp_values[12];
	int i, j;

	memcpy(temp_values, initial_values, sizeof(initial_values));
	for (i = 0; i < 256; i++) {
		for (j = 0; j < 12; j++) {
			pdec->table_subblock[i][j] = temp_values[j];
			temp_values[j] += values_derivated[j];
		}
	}
}

static void build_bit_powermask_table(struct pwc_dec23_private *pdec)
{
	unsigned char *p;
	unsigned int bit, byte, mask, val;
	unsigned int bitpower = 1;

	for (bit = 0; bit < 8; bit++) {
		mask = bitpower - 1;
		p = pdec->table_bitpowermask[bit];
		for (byte = 0; byte < 256; byte++) {
			val = (byte & mask);
			if (byte & bitpower)
				val = -val;
			*p++ = val;
		}
		bitpower<<=1;
	}
}


static void build_table_color(const unsigned int romtable[16][8],
			      unsigned char p0004[16][1024],
			      unsigned char p8004[16][256])
{
	int compression_mode, j, k, bit, pw;
	unsigned char *p0, *p8;
	const unsigned int *r;

	/* We have 16 compressions tables */
	for (compression_mode = 0; compression_mode < 16; compression_mode++) {
		p0 = p0004[compression_mode];
		p8 = p8004[compression_mode];
		r  = romtable[compression_mode];

		for (j = 0; j < 8; j++, r++, p0 += 128) {

			for (k = 0; k < 16; k++) {
				if (k == 0)
					bit = 1;
				else if (k >= 1 && k < 3)
					bit = (r[0] >> 15) & 7;
				else if (k >= 3 && k < 6)
					bit = (r[0] >> 12) & 7;
				else if (k >= 6 && k < 10)
					bit = (r[0] >> 9) & 7;
				else if (k >= 10 && k < 13)
					bit = (r[0] >> 6) & 7;
				else if (k >= 13 && k < 15)
					bit = (r[0] >> 3) & 7;
				else
					bit = (r[0]) & 7;
				if (k == 0)
					*p8++ = 8;
				else
					*p8++ = j - bit;
				*p8++ = bit;

				pw = 1 << bit;
				p0[k + 0x00] = (1 * pw) + 0x80;
				p0[k + 0x10] = (2 * pw) + 0x80;
				p0[k + 0x20] = (3 * pw) + 0x80;
				p0[k + 0x30] = (4 * pw) + 0x80;
				p0[k + 0x40] = (-1 * pw) + 0x80;
				p0[k + 0x50] = (-2 * pw) + 0x80;
				p0[k + 0x60] = (-3 * pw) + 0x80;
				p0[k + 0x70] = (-4 * pw) + 0x80;
			}	/* end of for (k=0; k<16; k++, p8++) */
		}	/* end of for (j=0; j<8; j++ , table++) */
	} /* end of foreach compression_mode */
}

/*
 *
 */
static void fill_table_dc00_d800(struct pwc_dec23_private *pdec)
{
#define SCALEBITS 15
#define ONE_HALF  (1UL << (SCALEBITS - 1))
	int i;
	unsigned int offset1 = ONE_HALF;
	unsigned int offset2 = 0x0000;

	for (i=0; i<256; i++) {
		pdec->table_dc00[i] = offset1 & ~(ONE_HALF);
		pdec->table_d800[i] = offset2;

		offset1 += 0x7bc4;
		offset2 += 0x7bc4;
	}
}

/*
 * To decode the stream:
 *   if look_bits(2) == 0:	# op == 2 in the lookup table
 *      skip_bits(2)
 *      end of the stream
 *   elif look_bits(3) == 7:	# op == 1 in the lookup table
 *      skip_bits(3)
 *      yyyy = get_bits(4)
 *      xxxx = get_bits(8)
 *   else:			# op == 0 in the lookup table
 *      skip_bits(x)
 *
 * For speedup processing, we build a lookup table and we takes the first 6 bits.
 *
 * struct {
 *   unsigned char op;	    // operation to execute
 *   unsigned char bits;    // bits use to perform operation
 *   unsigned char offset1; // offset to add to access in the table_0004 % 16
 *   unsigned char offset2; // offset to add to access in the table_0004
 * }
 *
 * How to build this table ?
 *   op == 2 when (i%4)==0
 *   op == 1 when (i%8)==7
 *   op == 0 otherwise
 *
 */
static const unsigned char hash_table_ops[64*4] = {
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x10,
	0x00, 0x06, 0x01, 0x30,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x01, 0x20,
	0x01, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x50,
	0x00, 0x05, 0x02, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x03, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x10,
	0x00, 0x06, 0x02, 0x10,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x01, 0x60,
	0x01, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x50,
	0x00, 0x05, 0x02, 0x40,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x03, 0x40,
	0x01, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x10,
	0x00, 0x06, 0x01, 0x70,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x01, 0x20,
	0x01, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x50,
	0x00, 0x05, 0x02, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x03, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x10,
	0x00, 0x06, 0x02, 0x50,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x01, 0x60,
	0x01, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x00,
	0x00, 0x04, 0x01, 0x50,
	0x00, 0x05, 0x02, 0x40,
	0x02, 0x00, 0x00, 0x00,
	0x00, 0x03, 0x01, 0x40,
	0x00, 0x05, 0x03, 0x40,
	0x01, 0x00, 0x00, 0x00
};

/*
 *
 */
static const unsigned int MulIdx[16][16] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,},
	{0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,},
	{0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,},
	{4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4,},
	{6, 7, 8, 9, 7, 10, 11, 8, 8, 11, 10, 7, 9, 8, 7, 6,},
	{4, 5, 5, 4, 4, 5, 5, 4, 4, 5, 5, 4, 4, 5, 5, 4,},
	{1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 0, 2,},
	{0, 3, 3, 0, 1, 2, 2, 1, 2, 1, 1, 2, 3, 0, 0, 3,},
	{0, 1, 2, 3, 3, 2, 1, 0, 3, 2, 1, 0, 0, 1, 2, 3,},
	{1, 1, 1, 1, 3, 3, 3, 3, 0, 0, 0, 0, 2, 2, 2, 2,},
	{7, 10, 11, 8, 9, 8, 7, 6, 6, 7, 8, 9, 8, 11, 10, 7,},
	{4, 5, 5, 4, 5, 4, 4, 5, 5, 4, 4, 5, 4, 5, 5, 4,},
	{7, 9, 6, 8, 10, 8, 7, 11, 11, 7, 8, 10, 8, 6, 9, 7,},
	{1, 3, 0, 2, 2, 0, 3, 1, 2, 0, 3, 1, 1, 3, 0, 2,},
	{1, 2, 2, 1, 3, 0, 0, 3, 0, 3, 3, 0, 2, 1, 1, 2,},
	{10, 8, 7, 11, 8, 6, 9, 7, 7, 9, 6, 8, 11, 7, 8, 10}
};

#if USE_LOOKUP_TABLE_TO_CLAMP
#define MAX_OUTER_CROP_VALUE	(512)
static unsigned char pwc_crop_table[256 + 2*MAX_OUTER_CROP_VALUE];
#define CLAMP(x) (pwc_crop_table[MAX_OUTER_CROP_VALUE+(x)])
#else
#define CLAMP(x) ((x)>255?255:((x)<0?0:x))
#endif


/* If the type or the command change, we rebuild the lookup table */
void pwc_dec23_init(struct pwc_device *pdev, const unsigned char *cmd)
{
	int flags, version, shift, i;
	struct pwc_dec23_private *pdec = &pdev->dec23;

	mutex_init(&pdec->lock);

	if (pdec->last_cmd_valid && pdec->last_cmd == cmd[2])
		return;

	if (DEVICE_USE_CODEC3(pdev->type)) {
		flags = cmd[2] & 0x18;
		if (flags == 8)
			pdec->nbits = 7;	/* More bits, mean more bits to encode the stream, but better quality */
		else if (flags == 0x10)
			pdec->nbits = 8;
		else
			pdec->nbits = 6;

		version = cmd[2] >> 5;
		build_table_color(KiaraRomTable[version][0], pdec->table_0004_pass1, pdec->table_8004_pass1);
		build_table_color(KiaraRomTable[version][1], pdec->table_0004_pass2, pdec->table_8004_pass2);

	} else {

		flags = cmd[2] & 6;
		if (flags == 2)
			pdec->nbits = 7;
		else if (flags == 4)
			pdec->nbits = 8;
		else
			pdec->nbits = 6;

		version = cmd[2] >> 3;
		build_table_color(TimonRomTable[version][0], pdec->table_0004_pass1, pdec->table_8004_pass1);
		build_table_color(TimonRomTable[version][1], pdec->table_0004_pass2, pdec->table_8004_pass2);
	}

	/* Informations can be coded on a variable number of bits but never less than 8 */
	shift = 8 - pdec->nbits;
	pdec->scalebits = SCALEBITS - shift;
	pdec->nbitsmask = 0xFF >> shift;

	fill_table_dc00_d800(pdec);
	build_subblock_pattern(pdec);
	build_bit_powermask_table(pdec);

#if USE_LOOKUP_TABLE_TO_CLAMP
	/* Build the static table to clamp value [0-255] */
	for (i=0;i<MAX_OUTER_CROP_VALUE;i++)
		pwc_crop_table[i] = 0;
	for (i=0; i<256; i++)
		pwc_crop_table[MAX_OUTER_CROP_VALUE+i] = i;
	for (i=0; i<MAX_OUTER_CROP_VALUE; i++)
		pwc_crop_table[MAX_OUTER_CROP_VALUE+256+i] = 255;
#endif

	pdec->last_cmd = cmd[2];
	pdec->last_cmd_valid = 1;
}

/*
 * Copy the 4x4 image block to Y plane buffer
 */
static void copy_image_block_Y(const int *src, unsigned char *dst, unsigned int bytes_per_line, unsigned int scalebits)
{
#if UNROLL_LOOP_FOR_COPY
	const unsigned char *cm = pwc_crop_table+MAX_OUTER_CROP_VALUE;
	const int *c = src;
	unsigned char *d = dst;

	*d++ = cm[c[0] >> scalebits];
	*d++ = cm[c[1] >> scalebits];
	*d++ = cm[c[2] >> scalebits];
	*d++ = cm[c[3] >> scalebits];

	d = dst + bytes_per_line;
	*d++ = cm[c[4] >> scalebits];
	*d++ = cm[c[5] >> scalebits];
	*d++ = cm[c[6] >> scalebits];
	*d++ = cm[c[7] >> scalebits];

	d = dst + bytes_per_line*2;
	*d++ = cm[c[8] >> scalebits];
	*d++ = cm[c[9] >> scalebits];
	*d++ = cm[c[10] >> scalebits];
	*d++ = cm[c[11] >> scalebits];

	d = dst + bytes_per_line*3;
	*d++ = cm[c[12] >> scalebits];
	*d++ = cm[c[13] >> scalebits];
	*d++ = cm[c[14] >> scalebits];
	*d++ = cm[c[15] >> scalebits];
#else
	int i;
	const int *c = src;
	unsigned char *d = dst;
	for (i = 0; i < 4; i++, c++)
		*d++ = CLAMP((*c) >> scalebits);

	d = dst + bytes_per_line;
	for (i = 0; i < 4; i++, c++)
		*d++ = CLAMP((*c) >> scalebits);

	d = dst + bytes_per_line*2;
	for (i = 0; i < 4; i++, c++)
		*d++ = CLAMP((*c) >> scalebits);

	d = dst + bytes_per_line*3;
	for (i = 0; i < 4; i++, c++)
		*d++ = CLAMP((*c) >> scalebits);
#endif
}

/*
 * Copy the 4x4 image block to a CrCb plane buffer
 *
 */
static void copy_image_block_CrCb(const int *src, unsigned char *dst, unsigned int bytes_per_line, unsigned int scalebits)
{
#if UNROLL_LOOP_FOR_COPY
	/* Unroll all loops */
	const unsigned char *cm = pwc_crop_table+MAX_OUTER_CROP_VALUE;
	const int *c = src;
	unsigned char *d = dst;

	*d++ = cm[c[0] >> scalebits];
	*d++ = cm[c[4] >> scalebits];
	*d++ = cm[c[1] >> scalebits];
	*d++ = cm[c[5] >> scalebits];
	*d++ = cm[c[2] >> scalebits];
	*d++ = cm[c[6] >> scalebits];
	*d++ = cm[c[3] >> scalebits];
	*d++ = cm[c[7] >> scalebits];

	d = dst + bytes_per_line;
	*d++ = cm[c[12] >> scalebits];
	*d++ = cm[c[8] >> scalebits];
	*d++ = cm[c[13] >> scalebits];
	*d++ = cm[c[9] >> scalebits];
	*d++ = cm[c[14] >> scalebits];
	*d++ = cm[c[10] >> scalebits];
	*d++ = cm[c[15] >> scalebits];
	*d++ = cm[c[11] >> scalebits];
#else
	int i;
	const int *c1 = src;
	const int *c2 = src + 4;
	unsigned char *d = dst;

	for (i = 0; i < 4; i++, c1++, c2++) {
		*d++ = CLAMP((*c1) >> scalebits);
		*d++ = CLAMP((*c2) >> scalebits);
	}
	c1 = src + 12;
	d = dst + bytes_per_line;
	for (i = 0; i < 4; i++, c1++, c2++) {
		*d++ = CLAMP((*c1) >> scalebits);
		*d++ = CLAMP((*c2) >> scalebits);
	}
#endif
}

/*
 * To manage the stream, we keep bits in a 32 bits register.
 * fill_nbits(n): fill the reservoir with at least n bits
 * skip_bits(n): discard n bits from the reservoir
 * get_bits(n): fill the reservoir, returns the first n bits and discard the
 *              bits from the reservoir.
 * __get_nbits(n): faster version of get_bits(n), but asumes that the reservoir
 *                 contains at least n bits. bits returned is discarded.
 */
#define fill_nbits(pdec, nbits_wanted) do { \
   while (pdec->nbits_in_reservoir<(nbits_wanted)) \
    { \
      pdec->reservoir |= (*(pdec->stream)++) << (pdec->nbits_in_reservoir); \
      pdec->nbits_in_reservoir += 8; \
    } \
}  while(0);

#define skip_nbits(pdec, nbits_to_skip) do { \
   pdec->reservoir >>= (nbits_to_skip); \
   pdec->nbits_in_reservoir -= (nbits_to_skip); \
}  while(0);

#define get_nbits(pdec, nbits_wanted, result) do { \
   fill_nbits(pdec, nbits_wanted); \
   result = (pdec->reservoir) & ((1U<<(nbits_wanted))-1); \
   skip_nbits(pdec, nbits_wanted); \
}  while(0);

#define __get_nbits(pdec, nbits_wanted, result) do { \
   result = (pdec->reservoir) & ((1U<<(nbits_wanted))-1); \
   skip_nbits(pdec, nbits_wanted); \
}  while(0);

#define look_nbits(pdec, nbits_wanted) \
   ((pdec->reservoir) & ((1U<<(nbits_wanted))-1))

/*
 * Decode a 4x4 pixel block
 */
static void decode_block(struct pwc_dec23_private *pdec,
			 const unsigned char *ptable0004,
			 const unsigned char *ptable8004)
{
	unsigned int primary_color;
	unsigned int channel_v, offset1, op;
	int i;

	fill_nbits(pdec, 16);
	__get_nbits(pdec, pdec->nbits, primary_color);

	if (look_nbits(pdec,2) == 0) {
		skip_nbits(pdec, 2);
		/* Very simple, the color is the same for all pixels of the square */
		for (i = 0; i < 16; i++)
			pdec->temp_colors[i] = pdec->table_dc00[primary_color];

		return;
	}

	/* This block is encoded with small pattern */
	for (i = 0; i < 16; i++)
		pdec->temp_colors[i] = pdec->table_d800[primary_color];

	__get_nbits(pdec, 3, channel_v);
	channel_v = ((channel_v & 1) << 2) | (channel_v & 2) | ((channel_v & 4) >> 2);

	ptable0004 += (channel_v * 128);
	ptable8004 += (channel_v * 32);

	offset1 = 0;
	do
	{
		unsigned int htable_idx, rows = 0;
		const unsigned int *block;

		/* [  zzzz y x x ]
		 *     xx == 00 :=> end of the block def, remove the two bits from the stream
		 *    yxx == 111
		 *    yxx == any other value
		 *
		 */
		fill_nbits(pdec, 16);
		htable_idx = look_nbits(pdec, 6);
		op = hash_table_ops[htable_idx * 4];

		if (op == 2) {
			skip_nbits(pdec, 2);

		} else if (op == 1) {
			/* 15bits [ xxxx xxxx yyyy 111 ]
			 * yyy => offset in the table8004
			 * xxx => offset in the tabled004 (tree)
			 */
			unsigned int mask, shift;
			unsigned int nbits, col1;
			unsigned int yyyy;

			skip_nbits(pdec, 3);
			/* offset1 += yyyy */
			__get_nbits(pdec, 4, yyyy);
			offset1 += 1 + yyyy;
			offset1 &= 0x0F;
			nbits = ptable8004[offset1 * 2];

			/* col1 = xxxx xxxx */
			__get_nbits(pdec, nbits+1, col1);

			/* Bit mask table */
			mask = pdec->table_bitpowermask[nbits][col1];
			shift = ptable8004[offset1 * 2 + 1];
			rows = ((mask << shift) + 0x80) & 0xFF;

			block = pdec->table_subblock[rows];
			for (i = 0; i < 16; i++)
				pdec->temp_colors[i] += block[MulIdx[offset1][i]];

		} else {
			/* op == 0
			 * offset1 is coded on 3 bits
			 */
			unsigned int shift;

			offset1 += hash_table_ops [htable_idx * 4 + 2];
			offset1 &= 0x0F;

			rows = ptable0004[offset1 + hash_table_ops [htable_idx * 4 + 3]];
			block = pdec->table_subblock[rows];
			for (i = 0; i < 16; i++)
				pdec->temp_colors[i] += block[MulIdx[offset1][i]];

			shift = hash_table_ops[htable_idx * 4 + 1];
			skip_nbits(pdec, shift);
		}

	} while (op != 2);

}

static void DecompressBand23(struct pwc_dec23_private *pdec,
			     const unsigned char *rawyuv,
			     unsigned char *planar_y,
			     unsigned char *planar_u,
			     unsigned char *planar_v,
			     unsigned int   compressed_image_width,
			     unsigned int   real_image_width)
{
	int compression_index, nblocks;
	const unsigned char *ptable0004;
	const unsigned char *ptable8004;

	pdec->reservoir = 0;
	pdec->nbits_in_reservoir = 0;
	pdec->stream = rawyuv + 1;	/* The first byte of the stream is skipped */

	get_nbits(pdec, 4, compression_index);

	/* pass 1: uncompress Y component */
	nblocks = compressed_image_width / 4;

	ptable0004 = pdec->table_0004_pass1[compression_index];
	ptable8004 = pdec->table_8004_pass1[compression_index];

	/* Each block decode a square of 4x4 */
	while (nblocks) {
		decode_block(pdec, ptable0004, ptable8004);
		copy_image_block_Y(pdec->temp_colors, planar_y, real_image_width, pdec->scalebits);
		planar_y += 4;
		nblocks--;
	}

	/* pass 2: uncompress UV component */
	nblocks = compressed_image_width / 8;

	ptable0004 = pdec->table_0004_pass2[compression_index];
	ptable8004 = pdec->table_8004_pass2[compression_index];

	/* Each block decode a square of 4x4 */
	while (nblocks) {
		decode_block(pdec, ptable0004, ptable8004);
		copy_image_block_CrCb(pdec->temp_colors, planar_u, real_image_width/2, pdec->scalebits);

		decode_block(pdec, ptable0004, ptable8004);
		copy_image_block_CrCb(pdec->temp_colors, planar_v, real_image_width/2, pdec->scalebits);

		planar_v += 8;
		planar_u += 8;
		nblocks -= 2;
	}

}

/**
 * Uncompress a pwc23 buffer.
 * @pdev: pointer to pwc device's internal struct
 * @src: raw data
 * @dst: image output
 */
void pwc_dec23_decompress(struct pwc_device *pdev,
			  const void *src,
			  void *dst)
{
	int bandlines_left, bytes_per_block;
	struct pwc_dec23_private *pdec = &pdev->dec23;

	/* YUV420P image format */
	unsigned char *pout_planar_y;
	unsigned char *pout_planar_u;
	unsigned char *pout_planar_v;
	unsigned int   plane_size;

	mutex_lock(&pdec->lock);

	bandlines_left = pdev->height / 4;
	bytes_per_block = pdev->width * 4;
	plane_size = pdev->height * pdev->width;

	pout_planar_y = dst;
	pout_planar_u = dst + plane_size;
	pout_planar_v = dst + plane_size + plane_size / 4;

	while (bandlines_left--) {
		DecompressBand23(pdec, src,
				 pout_planar_y, pout_planar_u, pout_planar_v,
				 pdev->width, pdev->width);
		src += pdev->vbandlength;
		pout_planar_y += bytes_per_block;
		pout_planar_u += pdev->width;
		pout_planar_v += pdev->width;
	}
	mutex_unlock(&pdec->lock);
}
