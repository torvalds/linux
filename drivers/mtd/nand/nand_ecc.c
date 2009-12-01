/*
 * This file contains an ECC algorithm that detects and corrects 1 bit
 * errors in a 256 byte block of data.
 *
 * drivers/mtd/nand/nand_ecc.c
 *
 * Copyright © 2008 Koninklijke Philips Electronics NV.
 *                  Author: Frans Meulenbroeks
 *
 * Completely replaces the previous ECC implementation which was written by:
 *   Steven J. Hill (sjhill@realitydiluted.com)
 *   Thomas Gleixner (tglx@linutronix.de)
 *
 * Information on how this algorithm works and how it was developed
 * can be found in Documentation/mtd/nand_ecc.txt
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this file; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 */

/*
 * The STANDALONE macro is useful when running the code outside the kernel
 * e.g. when running the code in a testbed or a benchmark program.
 * When STANDALONE is used, the module related macros are commented out
 * as well as the linux include files.
 * Instead a private definition of mtd_info is given to satisfy the compiler
 * (the code does not use mtd_info, so the code does not care)
 */
#ifndef STANDALONE
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <asm/byteorder.h>
#else
#include <stdint.h>
struct mtd_info;
#define EXPORT_SYMBOL(x)  /* x */

#define MODULE_LICENSE(x)	/* x */
#define MODULE_AUTHOR(x)	/* x */
#define MODULE_DESCRIPTION(x)	/* x */

#define printk printf
#define KERN_ERR		""
#endif

/*
 * invparity is a 256 byte table that contains the odd parity
 * for each byte. So if the number of bits in a byte is even,
 * the array element is 1, and when the number of bits is odd
 * the array eleemnt is 0.
 */
static const char invparity[256] = {
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

/*
 * bitsperbyte contains the number of bits per byte
 * this is only used for testing and repairing parity
 * (a precalculated value slightly improves performance)
 */
static const char bitsperbyte[256] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,
};

/*
 * addressbits is a lookup table to filter out the bits from the xor-ed
 * ecc data that identify the faulty location.
 * this is only used for repairing parity
 * see the comments in nand_correct_data for more details
 */
static const char addressbits[256] = {
	0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01,
	0x02, 0x02, 0x03, 0x03, 0x02, 0x02, 0x03, 0x03,
	0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01,
	0x02, 0x02, 0x03, 0x03, 0x02, 0x02, 0x03, 0x03,
	0x04, 0x04, 0x05, 0x05, 0x04, 0x04, 0x05, 0x05,
	0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07,
	0x04, 0x04, 0x05, 0x05, 0x04, 0x04, 0x05, 0x05,
	0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07,
	0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01,
	0x02, 0x02, 0x03, 0x03, 0x02, 0x02, 0x03, 0x03,
	0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x01,
	0x02, 0x02, 0x03, 0x03, 0x02, 0x02, 0x03, 0x03,
	0x04, 0x04, 0x05, 0x05, 0x04, 0x04, 0x05, 0x05,
	0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07,
	0x04, 0x04, 0x05, 0x05, 0x04, 0x04, 0x05, 0x05,
	0x06, 0x06, 0x07, 0x07, 0x06, 0x06, 0x07, 0x07,
	0x08, 0x08, 0x09, 0x09, 0x08, 0x08, 0x09, 0x09,
	0x0a, 0x0a, 0x0b, 0x0b, 0x0a, 0x0a, 0x0b, 0x0b,
	0x08, 0x08, 0x09, 0x09, 0x08, 0x08, 0x09, 0x09,
	0x0a, 0x0a, 0x0b, 0x0b, 0x0a, 0x0a, 0x0b, 0x0b,
	0x0c, 0x0c, 0x0d, 0x0d, 0x0c, 0x0c, 0x0d, 0x0d,
	0x0e, 0x0e, 0x0f, 0x0f, 0x0e, 0x0e, 0x0f, 0x0f,
	0x0c, 0x0c, 0x0d, 0x0d, 0x0c, 0x0c, 0x0d, 0x0d,
	0x0e, 0x0e, 0x0f, 0x0f, 0x0e, 0x0e, 0x0f, 0x0f,
	0x08, 0x08, 0x09, 0x09, 0x08, 0x08, 0x09, 0x09,
	0x0a, 0x0a, 0x0b, 0x0b, 0x0a, 0x0a, 0x0b, 0x0b,
	0x08, 0x08, 0x09, 0x09, 0x08, 0x08, 0x09, 0x09,
	0x0a, 0x0a, 0x0b, 0x0b, 0x0a, 0x0a, 0x0b, 0x0b,
	0x0c, 0x0c, 0x0d, 0x0d, 0x0c, 0x0c, 0x0d, 0x0d,
	0x0e, 0x0e, 0x0f, 0x0f, 0x0e, 0x0e, 0x0f, 0x0f,
	0x0c, 0x0c, 0x0d, 0x0d, 0x0c, 0x0c, 0x0d, 0x0d,
	0x0e, 0x0e, 0x0f, 0x0f, 0x0e, 0x0e, 0x0f, 0x0f
};

/**
 * nand_calculate_ecc - [NAND Interface] Calculate 3-byte ECC for 256/512-byte
 *			 block
 * @mtd:	MTD block structure
 * @buf:	input buffer with raw data
 * @code:	output buffer with ECC
 */
int nand_calculate_ecc(struct mtd_info *mtd, const unsigned char *buf,
		       unsigned char *code)
{
	int i;
	const uint32_t *bp = (uint32_t *)buf;
	/* 256 or 512 bytes/ecc  */
	const uint32_t eccsize_mult =
			(((struct nand_chip *)mtd->priv)->ecc.size) >> 8;
	uint32_t cur;		/* current value in buffer */
	/* rp0..rp15..rp17 are the various accumulated parities (per byte) */
	uint32_t rp0, rp1, rp2, rp3, rp4, rp5, rp6, rp7;
	uint32_t rp8, rp9, rp10, rp11, rp12, rp13, rp14, rp15, rp16;
	uint32_t uninitialized_var(rp17);	/* to make compiler happy */
	uint32_t par;		/* the cumulative parity for all data */
	uint32_t tmppar;	/* the cumulative parity for this iteration;
				   for rp12, rp14 and rp16 at the end of the
				   loop */

	par = 0;
	rp4 = 0;
	rp6 = 0;
	rp8 = 0;
	rp10 = 0;
	rp12 = 0;
	rp14 = 0;
	rp16 = 0;

	/*
	 * The loop is unrolled a number of times;
	 * This avoids if statements to decide on which rp value to update
	 * Also we process the data by longwords.
	 * Note: passing unaligned data might give a performance penalty.
	 * It is assumed that the buffers are aligned.
	 * tmppar is the cumulative sum of this iteration.
	 * needed for calculating rp12, rp14, rp16 and par
	 * also used as a performance improvement for rp6, rp8 and rp10
	 */
	for (i = 0; i < eccsize_mult << 2; i++) {
		cur = *bp++;
		tmppar = cur;
		rp4 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp6 ^= tmppar;
		cur = *bp++;
		tmppar ^= cur;
		rp4 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp8 ^= tmppar;

		cur = *bp++;
		tmppar ^= cur;
		rp4 ^= cur;
		rp6 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp6 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp4 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp10 ^= tmppar;

		cur = *bp++;
		tmppar ^= cur;
		rp4 ^= cur;
		rp6 ^= cur;
		rp8 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp6 ^= cur;
		rp8 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp4 ^= cur;
		rp8 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp8 ^= cur;

		cur = *bp++;
		tmppar ^= cur;
		rp4 ^= cur;
		rp6 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp6 ^= cur;
		cur = *bp++;
		tmppar ^= cur;
		rp4 ^= cur;
		cur = *bp++;
		tmppar ^= cur;

		par ^= tmppar;
		if ((i & 0x1) == 0)
			rp12 ^= tmppar;
		if ((i & 0x2) == 0)
			rp14 ^= tmppar;
		if (eccsize_mult == 2 && (i & 0x4) == 0)
			rp16 ^= tmppar;
	}

	/*
	 * handle the fact that we use longword operations
	 * we'll bring rp4..rp14..rp16 back to single byte entities by
	 * shifting and xoring first fold the upper and lower 16 bits,
	 * then the upper and lower 8 bits.
	 */
	rp4 ^= (rp4 >> 16);
	rp4 ^= (rp4 >> 8);
	rp4 &= 0xff;
	rp6 ^= (rp6 >> 16);
	rp6 ^= (rp6 >> 8);
	rp6 &= 0xff;
	rp8 ^= (rp8 >> 16);
	rp8 ^= (rp8 >> 8);
	rp8 &= 0xff;
	rp10 ^= (rp10 >> 16);
	rp10 ^= (rp10 >> 8);
	rp10 &= 0xff;
	rp12 ^= (rp12 >> 16);
	rp12 ^= (rp12 >> 8);
	rp12 &= 0xff;
	rp14 ^= (rp14 >> 16);
	rp14 ^= (rp14 >> 8);
	rp14 &= 0xff;
	if (eccsize_mult == 2) {
		rp16 ^= (rp16 >> 16);
		rp16 ^= (rp16 >> 8);
		rp16 &= 0xff;
	}

	/*
	 * we also need to calculate the row parity for rp0..rp3
	 * This is present in par, because par is now
	 * rp3 rp3 rp2 rp2 in little endian and
	 * rp2 rp2 rp3 rp3 in big endian
	 * as well as
	 * rp1 rp0 rp1 rp0 in little endian and
	 * rp0 rp1 rp0 rp1 in big endian
	 * First calculate rp2 and rp3
	 */
#ifdef __BIG_ENDIAN
	rp2 = (par >> 16);
	rp2 ^= (rp2 >> 8);
	rp2 &= 0xff;
	rp3 = par & 0xffff;
	rp3 ^= (rp3 >> 8);
	rp3 &= 0xff;
#else
	rp3 = (par >> 16);
	rp3 ^= (rp3 >> 8);
	rp3 &= 0xff;
	rp2 = par & 0xffff;
	rp2 ^= (rp2 >> 8);
	rp2 &= 0xff;
#endif

	/* reduce par to 16 bits then calculate rp1 and rp0 */
	par ^= (par >> 16);
#ifdef __BIG_ENDIAN
	rp0 = (par >> 8) & 0xff;
	rp1 = (par & 0xff);
#else
	rp1 = (par >> 8) & 0xff;
	rp0 = (par & 0xff);
#endif

	/* finally reduce par to 8 bits */
	par ^= (par >> 8);
	par &= 0xff;

	/*
	 * and calculate rp5..rp15..rp17
	 * note that par = rp4 ^ rp5 and due to the commutative property
	 * of the ^ operator we can say:
	 * rp5 = (par ^ rp4);
	 * The & 0xff seems superfluous, but benchmarking learned that
	 * leaving it out gives slightly worse results. No idea why, probably
	 * it has to do with the way the pipeline in pentium is organized.
	 */
	rp5 = (par ^ rp4) & 0xff;
	rp7 = (par ^ rp6) & 0xff;
	rp9 = (par ^ rp8) & 0xff;
	rp11 = (par ^ rp10) & 0xff;
	rp13 = (par ^ rp12) & 0xff;
	rp15 = (par ^ rp14) & 0xff;
	if (eccsize_mult == 2)
		rp17 = (par ^ rp16) & 0xff;

	/*
	 * Finally calculate the ecc bits.
	 * Again here it might seem that there are performance optimisations
	 * possible, but benchmarks showed that on the system this is developed
	 * the code below is the fastest
	 */
#ifdef CONFIG_MTD_NAND_ECC_SMC
	code[0] =
	    (invparity[rp7] << 7) |
	    (invparity[rp6] << 6) |
	    (invparity[rp5] << 5) |
	    (invparity[rp4] << 4) |
	    (invparity[rp3] << 3) |
	    (invparity[rp2] << 2) |
	    (invparity[rp1] << 1) |
	    (invparity[rp0]);
	code[1] =
	    (invparity[rp15] << 7) |
	    (invparity[rp14] << 6) |
	    (invparity[rp13] << 5) |
	    (invparity[rp12] << 4) |
	    (invparity[rp11] << 3) |
	    (invparity[rp10] << 2) |
	    (invparity[rp9] << 1)  |
	    (invparity[rp8]);
#else
	code[1] =
	    (invparity[rp7] << 7) |
	    (invparity[rp6] << 6) |
	    (invparity[rp5] << 5) |
	    (invparity[rp4] << 4) |
	    (invparity[rp3] << 3) |
	    (invparity[rp2] << 2) |
	    (invparity[rp1] << 1) |
	    (invparity[rp0]);
	code[0] =
	    (invparity[rp15] << 7) |
	    (invparity[rp14] << 6) |
	    (invparity[rp13] << 5) |
	    (invparity[rp12] << 4) |
	    (invparity[rp11] << 3) |
	    (invparity[rp10] << 2) |
	    (invparity[rp9] << 1)  |
	    (invparity[rp8]);
#endif
	if (eccsize_mult == 1)
		code[2] =
		    (invparity[par & 0xf0] << 7) |
		    (invparity[par & 0x0f] << 6) |
		    (invparity[par & 0xcc] << 5) |
		    (invparity[par & 0x33] << 4) |
		    (invparity[par & 0xaa] << 3) |
		    (invparity[par & 0x55] << 2) |
		    3;
	else
		code[2] =
		    (invparity[par & 0xf0] << 7) |
		    (invparity[par & 0x0f] << 6) |
		    (invparity[par & 0xcc] << 5) |
		    (invparity[par & 0x33] << 4) |
		    (invparity[par & 0xaa] << 3) |
		    (invparity[par & 0x55] << 2) |
		    (invparity[rp17] << 1) |
		    (invparity[rp16] << 0);
	return 0;
}
EXPORT_SYMBOL(nand_calculate_ecc);

/**
 * __nand_correct_data - [NAND Interface] Detect and correct bit error(s)
 * @buf:	raw data read from the chip
 * @read_ecc:	ECC from the chip
 * @calc_ecc:	the ECC calculated from raw data
 * @eccsize:	data bytes per ecc step (256 or 512)
 *
 * Detect and correct a 1 bit error for eccsize byte block
 */
int __nand_correct_data(unsigned char *buf,
			unsigned char *read_ecc, unsigned char *calc_ecc,
			unsigned int eccsize)
{
	unsigned char b0, b1, b2, bit_addr;
	unsigned int byte_addr;
	/* 256 or 512 bytes/ecc  */
	const uint32_t eccsize_mult = eccsize >> 8;

	/*
	 * b0 to b2 indicate which bit is faulty (if any)
	 * we might need the xor result  more than once,
	 * so keep them in a local var
	*/
#ifdef CONFIG_MTD_NAND_ECC_SMC
	b0 = read_ecc[0] ^ calc_ecc[0];
	b1 = read_ecc[1] ^ calc_ecc[1];
#else
	b0 = read_ecc[1] ^ calc_ecc[1];
	b1 = read_ecc[0] ^ calc_ecc[0];
#endif
	b2 = read_ecc[2] ^ calc_ecc[2];

	/* check if there are any bitfaults */

	/* repeated if statements are slightly more efficient than switch ... */
	/* ordered in order of likelihood */

	if ((b0 | b1 | b2) == 0)
		return 0;	/* no error */

	if ((((b0 ^ (b0 >> 1)) & 0x55) == 0x55) &&
	    (((b1 ^ (b1 >> 1)) & 0x55) == 0x55) &&
	    ((eccsize_mult == 1 && ((b2 ^ (b2 >> 1)) & 0x54) == 0x54) ||
	     (eccsize_mult == 2 && ((b2 ^ (b2 >> 1)) & 0x55) == 0x55))) {
	/* single bit error */
		/*
		 * rp17/rp15/13/11/9/7/5/3/1 indicate which byte is the faulty
		 * byte, cp 5/3/1 indicate the faulty bit.
		 * A lookup table (called addressbits) is used to filter
		 * the bits from the byte they are in.
		 * A marginal optimisation is possible by having three
		 * different lookup tables.
		 * One as we have now (for b0), one for b2
		 * (that would avoid the >> 1), and one for b1 (with all values
		 * << 4). However it was felt that introducing two more tables
		 * hardly justify the gain.
		 *
		 * The b2 shift is there to get rid of the lowest two bits.
		 * We could also do addressbits[b2] >> 1 but for the
		 * performace it does not make any difference
		 */
		if (eccsize_mult == 1)
			byte_addr = (addressbits[b1] << 4) + addressbits[b0];
		else
			byte_addr = (addressbits[b2 & 0x3] << 8) +
				    (addressbits[b1] << 4) + addressbits[b0];
		bit_addr = addressbits[b2 >> 2];
		/* flip the bit */
		buf[byte_addr] ^= (1 << bit_addr);
		return 1;

	}
	/* count nr of bits; use table lookup, faster than calculating it */
	if ((bitsperbyte[b0] + bitsperbyte[b1] + bitsperbyte[b2]) == 1)
		return 1;	/* error in ecc data; no action needed */

	printk(KERN_ERR "uncorrectable error : ");
	return -1;
}
EXPORT_SYMBOL(__nand_correct_data);

/**
 * nand_correct_data - [NAND Interface] Detect and correct bit error(s)
 * @mtd:	MTD block structure
 * @buf:	raw data read from the chip
 * @read_ecc:	ECC from the chip
 * @calc_ecc:	the ECC calculated from raw data
 *
 * Detect and correct a 1 bit error for 256/512 byte block
 */
int nand_correct_data(struct mtd_info *mtd, unsigned char *buf,
		      unsigned char *read_ecc, unsigned char *calc_ecc)
{
	return __nand_correct_data(buf, read_ecc, calc_ecc,
				   ((struct nand_chip *)mtd->priv)->ecc.size);
}
EXPORT_SYMBOL(nand_correct_data);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frans Meulenbroeks <fransmeulenbroeks@gmail.com>");
MODULE_DESCRIPTION("Generic NAND ECC support");
