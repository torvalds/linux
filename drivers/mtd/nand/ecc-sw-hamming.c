// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains an ECC algorithm that detects and corrects 1 bit
 * errors in a 256 byte block of data.
 *
 * Copyright © 2008 Koninklijke Philips Electronics NV.
 *                  Author: Frans Meulenbroeks
 *
 * Completely replaces the previous ECC implementation which was written by:
 *   Steven J. Hill (sjhill@realitydiluted.com)
 *   Thomas Gleixner (tglx@linutronix.de)
 *
 * Information on how this algorithm works and how it was developed
 * can be found in Documentation/driver-api/mtd/nand_ecc.rst
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand-ecc-sw-hamming.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

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
 * ECC data that identify the faulty location.
 * this is only used for repairing parity
 * see the comments in nand_ecc_sw_hamming_correct for more details
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

int ecc_sw_hamming_calculate(const unsigned char *buf, unsigned int step_size,
			     unsigned char *code, bool sm_order)
{
	const u32 *bp = (uint32_t *)buf;
	const u32 eccsize_mult = (step_size == 256) ? 1 : 2;
	/* current value in buffer */
	u32 cur;
	/* rp0..rp17 are the various accumulated parities (per byte) */
	u32 rp0, rp1, rp2, rp3, rp4, rp5, rp6, rp7, rp8, rp9, rp10, rp11, rp12,
		rp13, rp14, rp15, rp16, rp17;
	/* Cumulative parity for all data */
	u32 par;
	/* Cumulative parity at the end of the loop (rp12, rp14, rp16) */
	u32 tmppar;
	int i;

	par = 0;
	rp4 = 0;
	rp6 = 0;
	rp8 = 0;
	rp10 = 0;
	rp12 = 0;
	rp14 = 0;
	rp16 = 0;
	rp17 = 0;

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
	 * Finally calculate the ECC bits.
	 * Again here it might seem that there are performance optimisations
	 * possible, but benchmarks showed that on the system this is developed
	 * the code below is the fastest
	 */
	if (sm_order) {
		code[0] = (invparity[rp7] << 7) | (invparity[rp6] << 6) |
			  (invparity[rp5] << 5) | (invparity[rp4] << 4) |
			  (invparity[rp3] << 3) | (invparity[rp2] << 2) |
			  (invparity[rp1] << 1) | (invparity[rp0]);
		code[1] = (invparity[rp15] << 7) | (invparity[rp14] << 6) |
			  (invparity[rp13] << 5) | (invparity[rp12] << 4) |
			  (invparity[rp11] << 3) | (invparity[rp10] << 2) |
			  (invparity[rp9] << 1) | (invparity[rp8]);
	} else {
		code[1] = (invparity[rp7] << 7) | (invparity[rp6] << 6) |
			  (invparity[rp5] << 5) | (invparity[rp4] << 4) |
			  (invparity[rp3] << 3) | (invparity[rp2] << 2) |
			  (invparity[rp1] << 1) | (invparity[rp0]);
		code[0] = (invparity[rp15] << 7) | (invparity[rp14] << 6) |
			  (invparity[rp13] << 5) | (invparity[rp12] << 4) |
			  (invparity[rp11] << 3) | (invparity[rp10] << 2) |
			  (invparity[rp9] << 1) | (invparity[rp8]);
	}

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
EXPORT_SYMBOL(ecc_sw_hamming_calculate);

/**
 * nand_ecc_sw_hamming_calculate - Calculate 3-byte ECC for 256/512-byte block
 * @nand: NAND device
 * @buf: Input buffer with raw data
 * @code: Output buffer with ECC
 */
int nand_ecc_sw_hamming_calculate(struct nand_device *nand,
				  const unsigned char *buf, unsigned char *code)
{
	struct nand_ecc_sw_hamming_conf *engine_conf = nand->ecc.ctx.priv;
	unsigned int step_size = nand->ecc.ctx.conf.step_size;

	return ecc_sw_hamming_calculate(buf, step_size, code,
					engine_conf->sm_order);
}
EXPORT_SYMBOL(nand_ecc_sw_hamming_calculate);

int ecc_sw_hamming_correct(unsigned char *buf, unsigned char *read_ecc,
			   unsigned char *calc_ecc, unsigned int step_size,
			   bool sm_order)
{
	const u32 eccsize_mult = step_size >> 8;
	unsigned char b0, b1, b2, bit_addr;
	unsigned int byte_addr;

	/*
	 * b0 to b2 indicate which bit is faulty (if any)
	 * we might need the xor result  more than once,
	 * so keep them in a local var
	*/
	if (sm_order) {
		b0 = read_ecc[0] ^ calc_ecc[0];
		b1 = read_ecc[1] ^ calc_ecc[1];
	} else {
		b0 = read_ecc[1] ^ calc_ecc[1];
		b1 = read_ecc[0] ^ calc_ecc[0];
	}

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
		 * performance it does not make any difference
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
		return 1;	/* error in ECC data; no action needed */

	pr_err("%s: uncorrectable ECC error\n", __func__);
	return -EBADMSG;
}
EXPORT_SYMBOL(ecc_sw_hamming_correct);

/**
 * nand_ecc_sw_hamming_correct - Detect and correct bit error(s)
 * @nand: NAND device
 * @buf: Raw data read from the chip
 * @read_ecc: ECC bytes read from the chip
 * @calc_ecc: ECC calculated from the raw data
 *
 * Detect and correct up to 1 bit error per 256/512-byte block.
 */
int nand_ecc_sw_hamming_correct(struct nand_device *nand, unsigned char *buf,
				unsigned char *read_ecc,
				unsigned char *calc_ecc)
{
	struct nand_ecc_sw_hamming_conf *engine_conf = nand->ecc.ctx.priv;
	unsigned int step_size = nand->ecc.ctx.conf.step_size;

	return ecc_sw_hamming_correct(buf, read_ecc, calc_ecc, step_size,
				      engine_conf->sm_order);
}
EXPORT_SYMBOL(nand_ecc_sw_hamming_correct);

int nand_ecc_sw_hamming_init_ctx(struct nand_device *nand)
{
	struct nand_ecc_props *conf = &nand->ecc.ctx.conf;
	struct nand_ecc_sw_hamming_conf *engine_conf;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int ret;

	if (!mtd->ooblayout) {
		switch (mtd->oobsize) {
		case 8:
		case 16:
			mtd_set_ooblayout(mtd, nand_get_small_page_ooblayout());
			break;
		case 64:
		case 128:
			mtd_set_ooblayout(mtd,
					  nand_get_large_page_hamming_ooblayout());
			break;
		default:
			return -ENOTSUPP;
		}
	}

	conf->engine_type = NAND_ECC_ENGINE_TYPE_SOFT;
	conf->algo = NAND_ECC_ALGO_HAMMING;
	conf->step_size = nand->ecc.user_conf.step_size;
	conf->strength = 1;

	/* Use the strongest configuration by default */
	if (conf->step_size != 256 && conf->step_size != 512)
		conf->step_size = 256;

	engine_conf = kzalloc(sizeof(*engine_conf), GFP_KERNEL);
	if (!engine_conf)
		return -ENOMEM;

	ret = nand_ecc_init_req_tweaking(&engine_conf->req_ctx, nand);
	if (ret)
		goto free_engine_conf;

	engine_conf->code_size = 3;
	engine_conf->nsteps = mtd->writesize / conf->step_size;
	engine_conf->calc_buf = kzalloc(mtd->oobsize, GFP_KERNEL);
	engine_conf->code_buf = kzalloc(mtd->oobsize, GFP_KERNEL);
	if (!engine_conf->calc_buf || !engine_conf->code_buf) {
		ret = -ENOMEM;
		goto free_bufs;
	}

	nand->ecc.ctx.priv = engine_conf;
	nand->ecc.ctx.total = engine_conf->nsteps * engine_conf->code_size;

	return 0;

free_bufs:
	nand_ecc_cleanup_req_tweaking(&engine_conf->req_ctx);
	kfree(engine_conf->calc_buf);
	kfree(engine_conf->code_buf);
free_engine_conf:
	kfree(engine_conf);

	return ret;
}
EXPORT_SYMBOL(nand_ecc_sw_hamming_init_ctx);

void nand_ecc_sw_hamming_cleanup_ctx(struct nand_device *nand)
{
	struct nand_ecc_sw_hamming_conf *engine_conf = nand->ecc.ctx.priv;

	if (engine_conf) {
		nand_ecc_cleanup_req_tweaking(&engine_conf->req_ctx);
		kfree(engine_conf->calc_buf);
		kfree(engine_conf->code_buf);
		kfree(engine_conf);
	}
}
EXPORT_SYMBOL(nand_ecc_sw_hamming_cleanup_ctx);

static int nand_ecc_sw_hamming_prepare_io_req(struct nand_device *nand,
					      struct nand_page_io_req *req)
{
	struct nand_ecc_sw_hamming_conf *engine_conf = nand->ecc.ctx.priv;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int eccsize = nand->ecc.ctx.conf.step_size;
	int eccbytes = engine_conf->code_size;
	int eccsteps = engine_conf->nsteps;
	int total = nand->ecc.ctx.total;
	u8 *ecccalc = engine_conf->calc_buf;
	const u8 *data;
	int i;

	/* Nothing to do for a raw operation */
	if (req->mode == MTD_OPS_RAW)
		return 0;

	/* This engine does not provide BBM/free OOB bytes protection */
	if (!req->datalen)
		return 0;

	nand_ecc_tweak_req(&engine_conf->req_ctx, req);

	/* No more preparation for page read */
	if (req->type == NAND_PAGE_READ)
		return 0;

	/* Preparation for page write: derive the ECC bytes and place them */
	for (i = 0, data = req->databuf.out;
	     eccsteps;
	     eccsteps--, i += eccbytes, data += eccsize)
		nand_ecc_sw_hamming_calculate(nand, data, &ecccalc[i]);

	return mtd_ooblayout_set_eccbytes(mtd, ecccalc, (void *)req->oobbuf.out,
					  0, total);
}

static int nand_ecc_sw_hamming_finish_io_req(struct nand_device *nand,
					     struct nand_page_io_req *req)
{
	struct nand_ecc_sw_hamming_conf *engine_conf = nand->ecc.ctx.priv;
	struct mtd_info *mtd = nanddev_to_mtd(nand);
	int eccsize = nand->ecc.ctx.conf.step_size;
	int total = nand->ecc.ctx.total;
	int eccbytes = engine_conf->code_size;
	int eccsteps = engine_conf->nsteps;
	u8 *ecccalc = engine_conf->calc_buf;
	u8 *ecccode = engine_conf->code_buf;
	unsigned int max_bitflips = 0;
	u8 *data = req->databuf.in;
	int i, ret;

	/* Nothing to do for a raw operation */
	if (req->mode == MTD_OPS_RAW)
		return 0;

	/* This engine does not provide BBM/free OOB bytes protection */
	if (!req->datalen)
		return 0;

	/* No more preparation for page write */
	if (req->type == NAND_PAGE_WRITE) {
		nand_ecc_restore_req(&engine_conf->req_ctx, req);
		return 0;
	}

	/* Finish a page read: retrieve the (raw) ECC bytes*/
	ret = mtd_ooblayout_get_eccbytes(mtd, ecccode, req->oobbuf.in, 0,
					 total);
	if (ret)
		return ret;

	/* Calculate the ECC bytes */
	for (i = 0; eccsteps; eccsteps--, i += eccbytes, data += eccsize)
		nand_ecc_sw_hamming_calculate(nand, data, &ecccalc[i]);

	/* Finish a page read: compare and correct */
	for (eccsteps = engine_conf->nsteps, i = 0, data = req->databuf.in;
	     eccsteps;
	     eccsteps--, i += eccbytes, data += eccsize) {
		int stat =  nand_ecc_sw_hamming_correct(nand, data,
							&ecccode[i],
							&ecccalc[i]);
		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}

	nand_ecc_restore_req(&engine_conf->req_ctx, req);

	return max_bitflips;
}

static struct nand_ecc_engine_ops nand_ecc_sw_hamming_engine_ops = {
	.init_ctx = nand_ecc_sw_hamming_init_ctx,
	.cleanup_ctx = nand_ecc_sw_hamming_cleanup_ctx,
	.prepare_io_req = nand_ecc_sw_hamming_prepare_io_req,
	.finish_io_req = nand_ecc_sw_hamming_finish_io_req,
};

static struct nand_ecc_engine nand_ecc_sw_hamming_engine = {
	.ops = &nand_ecc_sw_hamming_engine_ops,
};

struct nand_ecc_engine *nand_ecc_sw_hamming_get_engine(void)
{
	return &nand_ecc_sw_hamming_engine;
}
EXPORT_SYMBOL(nand_ecc_sw_hamming_get_engine);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frans Meulenbroeks <fransmeulenbroeks@gmail.com>");
MODULE_DESCRIPTION("NAND software Hamming ECC support");
