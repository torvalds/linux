// SPDX-License-Identifier: LGPL-2.1+
/*
 * Copyright 2016 Tom aan de Wiel
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * 8x8 Fast Walsh Hadamard Transform in sequency order based on the paper:
 *
 * A Recursive Algorithm for Sequency-Ordered Fast Walsh Transforms,
 * R.D. Brown, 1977
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include "codec-fwht.h"

#define OVERFLOW_BIT BIT(14)

/*
 * Note: bit 0 of the header must always be 0. Otherwise it cannot
 * be guaranteed that the magic 8 byte sequence (see below) can
 * never occur in the rlc output.
 */
#define PFRAME_BIT BIT(15)
#define DUPS_MASK 0x1ffe

#define PBLOCK 0
#define IBLOCK 1

#define ALL_ZEROS 15

static const uint8_t zigzag[64] = {
	0,
	1,  8,
	2,  9, 16,
	3, 10, 17, 24,
	4, 11, 18, 25, 32,
	5, 12, 19, 26, 33, 40,
	6, 13, 20, 27, 34, 41, 48,
	7, 14, 21, 28, 35, 42, 49, 56,
	15, 22, 29, 36, 43, 50, 57,
	23, 30, 37, 44, 51, 58,
	31, 38, 45, 52, 59,
	39, 46, 53, 60,
	47, 54, 61,
	55, 62,
	63,
};

/*
 * noinline_for_stack to work around
 * https://bugs.llvm.org/show_bug.cgi?id=38809
 */
static int noinline_for_stack
rlc(const s16 *in, __be16 *output, int blocktype)
{
	s16 block[8 * 8];
	s16 *wp = block;
	int i = 0;
	int x, y;
	int ret = 0;

	/* read in block from framebuffer */
	int lastzero_run = 0;
	int to_encode;

	for (y = 0; y < 8; y++) {
		for (x = 0; x < 8; x++) {
			*wp = in[x + y * 8];
			wp++;
		}
	}

	/* keep track of amount of trailing zeros */
	for (i = 63; i >= 0 && !block[zigzag[i]]; i--)
		lastzero_run++;

	*output++ = (blocktype == PBLOCK ? htons(PFRAME_BIT) : 0);
	ret++;

	to_encode = 8 * 8 - (lastzero_run > 14 ? lastzero_run : 0);

	i = 0;
	while (i < to_encode) {
		int cnt = 0;
		int tmp;

		/* count leading zeros */
		while ((tmp = block[zigzag[i]]) == 0 && cnt < 14) {
			cnt++;
			i++;
			if (i == to_encode) {
				cnt--;
				break;
			}
		}
		/* 4 bits for run, 12 for coefficient (quantization by 4) */
		*output++ = htons((cnt | tmp << 4));
		i++;
		ret++;
	}
	if (lastzero_run > 14) {
		*output = htons(ALL_ZEROS | 0);
		ret++;
	}

	return ret;
}

/*
 * This function will worst-case increase rlc_in by 65*2 bytes:
 * one s16 value for the header and 8 * 8 coefficients of type s16.
 */
static noinline_for_stack u16
derlc(const __be16 **rlc_in, s16 *dwht_out, const __be16 *end_of_input)
{
	/* header */
	const __be16 *input = *rlc_in;
	u16 stat;
	int dec_count = 0;
	s16 block[8 * 8 + 16];
	s16 *wp = block;
	int i;

	if (input > end_of_input)
		return OVERFLOW_BIT;
	stat = ntohs(*input++);

	/*
	 * Now de-compress, it expands one byte to up to 15 bytes
	 * (or fills the remainder of the 64 bytes with zeroes if it
	 * is the last byte to expand).
	 *
	 * So block has to be 8 * 8 + 16 bytes, the '+ 16' is to
	 * allow for overflow if the incoming data was malformed.
	 */
	while (dec_count < 8 * 8) {
		s16 in;
		int length;
		int coeff;

		if (input > end_of_input)
			return OVERFLOW_BIT;
		in = ntohs(*input++);
		length = in & 0xf;
		coeff = in >> 4;

		/* fill remainder with zeros */
		if (length == 15) {
			for (i = 0; i < 64 - dec_count; i++)
				*wp++ = 0;
			break;
		}

		for (i = 0; i < length; i++)
			*wp++ = 0;
		*wp++ = coeff;
		dec_count += length + 1;
	}

	wp = block;

	for (i = 0; i < 64; i++) {
		int pos = zigzag[i];
		int y = pos / 8;
		int x = pos % 8;

		dwht_out[x + y * 8] = *wp++;
	}
	*rlc_in = input;
	return stat;
}

static const int quant_table[] = {
	2, 2, 2, 2, 2, 2,  2,  2,
	2, 2, 2, 2, 2, 2,  2,  2,
	2, 2, 2, 2, 2, 2,  2,  3,
	2, 2, 2, 2, 2, 2,  3,  6,
	2, 2, 2, 2, 2, 3,  6,  6,
	2, 2, 2, 2, 3, 6,  6,  6,
	2, 2, 2, 3, 6, 6,  6,  6,
	2, 2, 3, 6, 6, 6,  6,  8,
};

static const int quant_table_p[] = {
	3, 3, 3, 3, 3, 3,  3,  3,
	3, 3, 3, 3, 3, 3,  3,  3,
	3, 3, 3, 3, 3, 3,  3,  3,
	3, 3, 3, 3, 3, 3,  3,  6,
	3, 3, 3, 3, 3, 3,  6,  6,
	3, 3, 3, 3, 3, 6,  6,  9,
	3, 3, 3, 3, 6, 6,  9,  9,
	3, 3, 3, 6, 6, 9,  9,  10,
};

static void quantize_intra(s16 *coeff, s16 *de_coeff, u16 qp)
{
	const int *quant = quant_table;
	int i, j;

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 8; i++, quant++, coeff++, de_coeff++) {
			*coeff >>= *quant;
			if (*coeff >= -qp && *coeff <= qp)
				*coeff = *de_coeff = 0;
			else
				*de_coeff = *coeff << *quant;
		}
	}
}

static void dequantize_intra(s16 *coeff)
{
	const int *quant = quant_table;
	int i, j;

	for (j = 0; j < 8; j++)
		for (i = 0; i < 8; i++, quant++, coeff++)
			*coeff <<= *quant;
}

static void quantize_inter(s16 *coeff, s16 *de_coeff, u16 qp)
{
	const int *quant = quant_table_p;
	int i, j;

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 8; i++, quant++, coeff++, de_coeff++) {
			*coeff >>= *quant;
			if (*coeff >= -qp && *coeff <= qp)
				*coeff = *de_coeff = 0;
			else
				*de_coeff = *coeff << *quant;
		}
	}
}

static void dequantize_inter(s16 *coeff)
{
	const int *quant = quant_table_p;
	int i, j;

	for (j = 0; j < 8; j++)
		for (i = 0; i < 8; i++, quant++, coeff++)
			*coeff <<= *quant;
}

static void noinline_for_stack fwht(const u8 *block, s16 *output_block,
				    unsigned int stride,
				    unsigned int input_step, bool intra)
{
	/* we'll need more than 8 bits for the transformed coefficients */
	s32 workspace1[8], workspace2[8];
	const u8 *tmp = block;
	s16 *out = output_block;
	int add = intra ? 256 : 0;
	unsigned int i;

	/* stage 1 */
	for (i = 0; i < 8; i++, tmp += stride, out += 8) {
		switch (input_step) {
		case 1:
			workspace1[0]  = tmp[0] + tmp[1] - add;
			workspace1[1]  = tmp[0] - tmp[1];

			workspace1[2]  = tmp[2] + tmp[3] - add;
			workspace1[3]  = tmp[2] - tmp[3];

			workspace1[4]  = tmp[4] + tmp[5] - add;
			workspace1[5]  = tmp[4] - tmp[5];

			workspace1[6]  = tmp[6] + tmp[7] - add;
			workspace1[7]  = tmp[6] - tmp[7];
			break;
		case 2:
			workspace1[0]  = tmp[0] + tmp[2] - add;
			workspace1[1]  = tmp[0] - tmp[2];

			workspace1[2]  = tmp[4] + tmp[6] - add;
			workspace1[3]  = tmp[4] - tmp[6];

			workspace1[4]  = tmp[8] + tmp[10] - add;
			workspace1[5]  = tmp[8] - tmp[10];

			workspace1[6]  = tmp[12] + tmp[14] - add;
			workspace1[7]  = tmp[12] - tmp[14];
			break;
		case 3:
			workspace1[0]  = tmp[0] + tmp[3] - add;
			workspace1[1]  = tmp[0] - tmp[3];

			workspace1[2]  = tmp[6] + tmp[9] - add;
			workspace1[3]  = tmp[6] - tmp[9];

			workspace1[4]  = tmp[12] + tmp[15] - add;
			workspace1[5]  = tmp[12] - tmp[15];

			workspace1[6]  = tmp[18] + tmp[21] - add;
			workspace1[7]  = tmp[18] - tmp[21];
			break;
		default:
			workspace1[0]  = tmp[0] + tmp[4] - add;
			workspace1[1]  = tmp[0] - tmp[4];

			workspace1[2]  = tmp[8] + tmp[12] - add;
			workspace1[3]  = tmp[8] - tmp[12];

			workspace1[4]  = tmp[16] + tmp[20] - add;
			workspace1[5]  = tmp[16] - tmp[20];

			workspace1[6]  = tmp[24] + tmp[28] - add;
			workspace1[7]  = tmp[24] - tmp[28];
			break;
		}

		/* stage 2 */
		workspace2[0] = workspace1[0] + workspace1[2];
		workspace2[1] = workspace1[0] - workspace1[2];
		workspace2[2] = workspace1[1] - workspace1[3];
		workspace2[3] = workspace1[1] + workspace1[3];

		workspace2[4] = workspace1[4] + workspace1[6];
		workspace2[5] = workspace1[4] - workspace1[6];
		workspace2[6] = workspace1[5] - workspace1[7];
		workspace2[7] = workspace1[5] + workspace1[7];

		/* stage 3 */
		out[0] = workspace2[0] + workspace2[4];
		out[1] = workspace2[0] - workspace2[4];
		out[2] = workspace2[1] - workspace2[5];
		out[3] = workspace2[1] + workspace2[5];
		out[4] = workspace2[2] + workspace2[6];
		out[5] = workspace2[2] - workspace2[6];
		out[6] = workspace2[3] - workspace2[7];
		out[7] = workspace2[3] + workspace2[7];
	}

	out = output_block;

	for (i = 0; i < 8; i++, out++) {
		/* stage 1 */
		workspace1[0]  = out[0] + out[1 * 8];
		workspace1[1]  = out[0] - out[1 * 8];

		workspace1[2]  = out[2 * 8] + out[3 * 8];
		workspace1[3]  = out[2 * 8] - out[3 * 8];

		workspace1[4]  = out[4 * 8] + out[5 * 8];
		workspace1[5]  = out[4 * 8] - out[5 * 8];

		workspace1[6]  = out[6 * 8] + out[7 * 8];
		workspace1[7]  = out[6 * 8] - out[7 * 8];

		/* stage 2 */
		workspace2[0] = workspace1[0] + workspace1[2];
		workspace2[1] = workspace1[0] - workspace1[2];
		workspace2[2] = workspace1[1] - workspace1[3];
		workspace2[3] = workspace1[1] + workspace1[3];

		workspace2[4] = workspace1[4] + workspace1[6];
		workspace2[5] = workspace1[4] - workspace1[6];
		workspace2[6] = workspace1[5] - workspace1[7];
		workspace2[7] = workspace1[5] + workspace1[7];
		/* stage 3 */
		out[0 * 8] = workspace2[0] + workspace2[4];
		out[1 * 8] = workspace2[0] - workspace2[4];
		out[2 * 8] = workspace2[1] - workspace2[5];
		out[3 * 8] = workspace2[1] + workspace2[5];
		out[4 * 8] = workspace2[2] + workspace2[6];
		out[5 * 8] = workspace2[2] - workspace2[6];
		out[6 * 8] = workspace2[3] - workspace2[7];
		out[7 * 8] = workspace2[3] + workspace2[7];
	}
}

/*
 * Not the nicest way of doing it, but P-blocks get twice the range of
 * that of the I-blocks. Therefore we need a type bigger than 8 bits.
 * Furthermore values can be negative... This is just a version that
 * works with 16 signed data
 */
static void noinline_for_stack
fwht16(const s16 *block, s16 *output_block, int stride, int intra)
{
	/* we'll need more than 8 bits for the transformed coefficients */
	s32 workspace1[8], workspace2[8];
	const s16 *tmp = block;
	s16 *out = output_block;
	int i;

	for (i = 0; i < 8; i++, tmp += stride, out += 8) {
		/* stage 1 */
		workspace1[0]  = tmp[0] + tmp[1];
		workspace1[1]  = tmp[0] - tmp[1];

		workspace1[2]  = tmp[2] + tmp[3];
		workspace1[3]  = tmp[2] - tmp[3];

		workspace1[4]  = tmp[4] + tmp[5];
		workspace1[5]  = tmp[4] - tmp[5];

		workspace1[6]  = tmp[6] + tmp[7];
		workspace1[7]  = tmp[6] - tmp[7];

		/* stage 2 */
		workspace2[0] = workspace1[0] + workspace1[2];
		workspace2[1] = workspace1[0] - workspace1[2];
		workspace2[2] = workspace1[1] - workspace1[3];
		workspace2[3] = workspace1[1] + workspace1[3];

		workspace2[4] = workspace1[4] + workspace1[6];
		workspace2[5] = workspace1[4] - workspace1[6];
		workspace2[6] = workspace1[5] - workspace1[7];
		workspace2[7] = workspace1[5] + workspace1[7];

		/* stage 3 */
		out[0] = workspace2[0] + workspace2[4];
		out[1] = workspace2[0] - workspace2[4];
		out[2] = workspace2[1] - workspace2[5];
		out[3] = workspace2[1] + workspace2[5];
		out[4] = workspace2[2] + workspace2[6];
		out[5] = workspace2[2] - workspace2[6];
		out[6] = workspace2[3] - workspace2[7];
		out[7] = workspace2[3] + workspace2[7];
	}

	out = output_block;

	for (i = 0; i < 8; i++, out++) {
		/* stage 1 */
		workspace1[0]  = out[0] + out[1*8];
		workspace1[1]  = out[0] - out[1*8];

		workspace1[2]  = out[2*8] + out[3*8];
		workspace1[3]  = out[2*8] - out[3*8];

		workspace1[4]  = out[4*8] + out[5*8];
		workspace1[5]  = out[4*8] - out[5*8];

		workspace1[6]  = out[6*8] + out[7*8];
		workspace1[7]  = out[6*8] - out[7*8];

		/* stage 2 */
		workspace2[0] = workspace1[0] + workspace1[2];
		workspace2[1] = workspace1[0] - workspace1[2];
		workspace2[2] = workspace1[1] - workspace1[3];
		workspace2[3] = workspace1[1] + workspace1[3];

		workspace2[4] = workspace1[4] + workspace1[6];
		workspace2[5] = workspace1[4] - workspace1[6];
		workspace2[6] = workspace1[5] - workspace1[7];
		workspace2[7] = workspace1[5] + workspace1[7];

		/* stage 3 */
		out[0*8] = workspace2[0] + workspace2[4];
		out[1*8] = workspace2[0] - workspace2[4];
		out[2*8] = workspace2[1] - workspace2[5];
		out[3*8] = workspace2[1] + workspace2[5];
		out[4*8] = workspace2[2] + workspace2[6];
		out[5*8] = workspace2[2] - workspace2[6];
		out[6*8] = workspace2[3] - workspace2[7];
		out[7*8] = workspace2[3] + workspace2[7];
	}
}

static noinline_for_stack void
ifwht(const s16 *block, s16 *output_block, int intra)
{
	/*
	 * we'll need more than 8 bits for the transformed coefficients
	 * use native unit of cpu
	 */
	int workspace1[8], workspace2[8];
	int inter = intra ? 0 : 1;
	const s16 *tmp = block;
	s16 *out = output_block;
	int i;

	for (i = 0; i < 8; i++, tmp += 8, out += 8) {
		/* stage 1 */
		workspace1[0]  = tmp[0] + tmp[1];
		workspace1[1]  = tmp[0] - tmp[1];

		workspace1[2]  = tmp[2] + tmp[3];
		workspace1[3]  = tmp[2] - tmp[3];

		workspace1[4]  = tmp[4] + tmp[5];
		workspace1[5]  = tmp[4] - tmp[5];

		workspace1[6]  = tmp[6] + tmp[7];
		workspace1[7]  = tmp[6] - tmp[7];

		/* stage 2 */
		workspace2[0] = workspace1[0] + workspace1[2];
		workspace2[1] = workspace1[0] - workspace1[2];
		workspace2[2] = workspace1[1] - workspace1[3];
		workspace2[3] = workspace1[1] + workspace1[3];

		workspace2[4] = workspace1[4] + workspace1[6];
		workspace2[5] = workspace1[4] - workspace1[6];
		workspace2[6] = workspace1[5] - workspace1[7];
		workspace2[7] = workspace1[5] + workspace1[7];

		/* stage 3 */
		out[0] = workspace2[0] + workspace2[4];
		out[1] = workspace2[0] - workspace2[4];
		out[2] = workspace2[1] - workspace2[5];
		out[3] = workspace2[1] + workspace2[5];
		out[4] = workspace2[2] + workspace2[6];
		out[5] = workspace2[2] - workspace2[6];
		out[6] = workspace2[3] - workspace2[7];
		out[7] = workspace2[3] + workspace2[7];
	}

	out = output_block;

	for (i = 0; i < 8; i++, out++) {
		/* stage 1 */
		workspace1[0]  = out[0] + out[1 * 8];
		workspace1[1]  = out[0] - out[1 * 8];

		workspace1[2]  = out[2 * 8] + out[3 * 8];
		workspace1[3]  = out[2 * 8] - out[3 * 8];

		workspace1[4]  = out[4 * 8] + out[5 * 8];
		workspace1[5]  = out[4 * 8] - out[5 * 8];

		workspace1[6]  = out[6 * 8] + out[7 * 8];
		workspace1[7]  = out[6 * 8] - out[7 * 8];

		/* stage 2 */
		workspace2[0] = workspace1[0] + workspace1[2];
		workspace2[1] = workspace1[0] - workspace1[2];
		workspace2[2] = workspace1[1] - workspace1[3];
		workspace2[3] = workspace1[1] + workspace1[3];

		workspace2[4] = workspace1[4] + workspace1[6];
		workspace2[5] = workspace1[4] - workspace1[6];
		workspace2[6] = workspace1[5] - workspace1[7];
		workspace2[7] = workspace1[5] + workspace1[7];

		/* stage 3 */
		if (inter) {
			int d;

			out[0 * 8] = workspace2[0] + workspace2[4];
			out[1 * 8] = workspace2[0] - workspace2[4];
			out[2 * 8] = workspace2[1] - workspace2[5];
			out[3 * 8] = workspace2[1] + workspace2[5];
			out[4 * 8] = workspace2[2] + workspace2[6];
			out[5 * 8] = workspace2[2] - workspace2[6];
			out[6 * 8] = workspace2[3] - workspace2[7];
			out[7 * 8] = workspace2[3] + workspace2[7];

			for (d = 0; d < 8; d++)
				out[8 * d] >>= 6;
		} else {
			int d;

			out[0 * 8] = workspace2[0] + workspace2[4];
			out[1 * 8] = workspace2[0] - workspace2[4];
			out[2 * 8] = workspace2[1] - workspace2[5];
			out[3 * 8] = workspace2[1] + workspace2[5];
			out[4 * 8] = workspace2[2] + workspace2[6];
			out[5 * 8] = workspace2[2] - workspace2[6];
			out[6 * 8] = workspace2[3] - workspace2[7];
			out[7 * 8] = workspace2[3] + workspace2[7];

			for (d = 0; d < 8; d++) {
				out[8 * d] >>= 6;
				out[8 * d] += 128;
			}
		}
	}
}

static void fill_encoder_block(const u8 *input, s16 *dst,
			       unsigned int stride, unsigned int input_step)
{
	int i, j;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++, input += input_step)
			*dst++ = *input;
		input += stride - 8 * input_step;
	}
}

static int var_intra(const s16 *input)
{
	int32_t mean = 0;
	int32_t ret = 0;
	const s16 *tmp = input;
	int i;

	for (i = 0; i < 8 * 8; i++, tmp++)
		mean += *tmp;
	mean /= 64;
	tmp = input;
	for (i = 0; i < 8 * 8; i++, tmp++)
		ret += (*tmp - mean) < 0 ? -(*tmp - mean) : (*tmp - mean);
	return ret;
}

static int var_inter(const s16 *old, const s16 *new)
{
	int32_t ret = 0;
	int i;

	for (i = 0; i < 8 * 8; i++, old++, new++)
		ret += (*old - *new) < 0 ? -(*old - *new) : (*old - *new);
	return ret;
}

static noinline_for_stack int
decide_blocktype(const u8 *cur, const u8 *reference, s16 *deltablock,
		 unsigned int stride, unsigned int input_step)
{
	s16 tmp[64];
	s16 old[64];
	s16 *work = tmp;
	unsigned int k, l;
	int vari;
	int vard;

	fill_encoder_block(cur, tmp, stride, input_step);
	fill_encoder_block(reference, old, 8, 1);
	vari = var_intra(tmp);

	for (k = 0; k < 8; k++) {
		for (l = 0; l < 8; l++) {
			*deltablock = *work - *reference;
			deltablock++;
			work++;
			reference++;
		}
	}
	deltablock -= 64;
	vard = var_inter(old, tmp);
	return vari <= vard ? IBLOCK : PBLOCK;
}

static void fill_decoder_block(u8 *dst, const s16 *input, int stride,
			       unsigned int dst_step)
{
	int i, j;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++, input++, dst += dst_step) {
			if (*input < 0)
				*dst = 0;
			else if (*input > 255)
				*dst = 255;
			else
				*dst = *input;
		}
		dst += stride - (8 * dst_step);
	}
}

static void add_deltas(s16 *deltas, const u8 *ref, int stride,
		       unsigned int ref_step)
{
	int k, l;

	for (k = 0; k < 8; k++) {
		for (l = 0; l < 8; l++) {
			*deltas += *ref;
			ref += ref_step;
			/*
			 * Due to quantizing, it might possible that the
			 * decoded coefficients are slightly out of range
			 */
			if (*deltas < 0)
				*deltas = 0;
			else if (*deltas > 255)
				*deltas = 255;
			deltas++;
		}
		ref += stride - (8 * ref_step);
	}
}

static u32 encode_plane(u8 *input, u8 *refp, __be16 **rlco, __be16 *rlco_max,
			struct fwht_cframe *cf, u32 height, u32 width,
			u32 stride, unsigned int input_step,
			bool is_intra, bool next_is_intra)
{
	u8 *input_start = input;
	__be16 *rlco_start = *rlco;
	s16 deltablock[64];
	__be16 pframe_bit = htons(PFRAME_BIT);
	u32 encoding = 0;
	unsigned int last_size = 0;
	unsigned int i, j;

	width = round_up(width, 8);
	height = round_up(height, 8);

	for (j = 0; j < height / 8; j++) {
		input = input_start + j * 8 * stride;
		for (i = 0; i < width / 8; i++) {
			/* intra code, first frame is always intra coded. */
			int blocktype = IBLOCK;
			unsigned int size;

			if (!is_intra)
				blocktype = decide_blocktype(input, refp,
					deltablock, stride, input_step);
			if (blocktype == IBLOCK) {
				fwht(input, cf->coeffs, stride, input_step, 1);
				quantize_intra(cf->coeffs, cf->de_coeffs,
					       cf->i_frame_qp);
			} else {
				/* inter code */
				encoding |= FWHT_FRAME_PCODED;
				fwht16(deltablock, cf->coeffs, 8, 0);
				quantize_inter(cf->coeffs, cf->de_coeffs,
					       cf->p_frame_qp);
			}
			if (!next_is_intra) {
				ifwht(cf->de_coeffs, cf->de_fwht, blocktype);

				if (blocktype == PBLOCK)
					add_deltas(cf->de_fwht, refp, 8, 1);
				fill_decoder_block(refp, cf->de_fwht, 8, 1);
			}

			input += 8 * input_step;
			refp += 8 * 8;

			size = rlc(cf->coeffs, *rlco, blocktype);
			if (last_size == size &&
			    !memcmp(*rlco + 1, *rlco - size + 1, 2 * size - 2)) {
				__be16 *last_rlco = *rlco - size;
				s16 hdr = ntohs(*last_rlco);

				if (!((*last_rlco ^ **rlco) & pframe_bit) &&
				    (hdr & DUPS_MASK) < DUPS_MASK)
					*last_rlco = htons(hdr + 2);
				else
					*rlco += size;
			} else {
				*rlco += size;
			}
			if (*rlco >= rlco_max) {
				encoding |= FWHT_FRAME_UNENCODED;
				goto exit_loop;
			}
			last_size = size;
		}
	}

exit_loop:
	if (encoding & FWHT_FRAME_UNENCODED) {
		u8 *out = (u8 *)rlco_start;
		u8 *p;

		input = input_start;
		/*
		 * The compressed stream should never contain the magic
		 * header, so when we copy the YUV data we replace 0xff
		 * by 0xfe. Since YUV is limited range such values
		 * shouldn't appear anyway.
		 */
		for (j = 0; j < height; j++) {
			for (i = 0, p = input; i < width; i++, p += input_step)
				*out++ = (*p == 0xff) ? 0xfe : *p;
			input += stride;
		}
		*rlco = (__be16 *)out;
		encoding &= ~FWHT_FRAME_PCODED;
	}
	return encoding;
}

u32 fwht_encode_frame(struct fwht_raw_frame *frm,
		      struct fwht_raw_frame *ref_frm,
		      struct fwht_cframe *cf,
		      bool is_intra, bool next_is_intra,
		      unsigned int width, unsigned int height,
		      unsigned int stride, unsigned int chroma_stride)
{
	unsigned int size = height * width;
	__be16 *rlco = cf->rlc_data;
	__be16 *rlco_max;
	u32 encoding;

	rlco_max = rlco + size / 2 - 256;
	encoding = encode_plane(frm->luma, ref_frm->luma, &rlco, rlco_max, cf,
				height, width, stride,
				frm->luma_alpha_step, is_intra, next_is_intra);
	if (encoding & FWHT_FRAME_UNENCODED)
		encoding |= FWHT_LUMA_UNENCODED;
	encoding &= ~FWHT_FRAME_UNENCODED;

	if (frm->components_num >= 3) {
		u32 chroma_h = height / frm->height_div;
		u32 chroma_w = width / frm->width_div;
		unsigned int chroma_size = chroma_h * chroma_w;

		rlco_max = rlco + chroma_size / 2 - 256;
		encoding |= encode_plane(frm->cb, ref_frm->cb, &rlco, rlco_max,
					 cf, chroma_h, chroma_w,
					 chroma_stride, frm->chroma_step,
					 is_intra, next_is_intra);
		if (encoding & FWHT_FRAME_UNENCODED)
			encoding |= FWHT_CB_UNENCODED;
		encoding &= ~FWHT_FRAME_UNENCODED;
		rlco_max = rlco + chroma_size / 2 - 256;
		encoding |= encode_plane(frm->cr, ref_frm->cr, &rlco, rlco_max,
					 cf, chroma_h, chroma_w,
					 chroma_stride, frm->chroma_step,
					 is_intra, next_is_intra);
		if (encoding & FWHT_FRAME_UNENCODED)
			encoding |= FWHT_CR_UNENCODED;
		encoding &= ~FWHT_FRAME_UNENCODED;
	}

	if (frm->components_num == 4) {
		rlco_max = rlco + size / 2 - 256;
		encoding |= encode_plane(frm->alpha, ref_frm->alpha, &rlco,
					 rlco_max, cf, height, width,
					 stride, frm->luma_alpha_step,
					 is_intra, next_is_intra);
		if (encoding & FWHT_FRAME_UNENCODED)
			encoding |= FWHT_ALPHA_UNENCODED;
		encoding &= ~FWHT_FRAME_UNENCODED;
	}

	cf->size = (rlco - cf->rlc_data) * sizeof(*rlco);
	return encoding;
}

static bool decode_plane(struct fwht_cframe *cf, const __be16 **rlco,
			 u32 height, u32 width, const u8 *ref, u32 ref_stride,
			 unsigned int ref_step, u8 *dst,
			 unsigned int dst_stride, unsigned int dst_step,
			 bool uncompressed, const __be16 *end_of_rlco_buf)
{
	unsigned int copies = 0;
	s16 copy[8 * 8];
	u16 stat;
	unsigned int i, j;
	bool is_intra = !ref;

	width = round_up(width, 8);
	height = round_up(height, 8);

	if (uncompressed) {
		int i;

		if (end_of_rlco_buf + 1 < *rlco + width * height / 2)
			return false;
		for (i = 0; i < height; i++) {
			memcpy(dst, *rlco, width);
			dst += dst_stride;
			*rlco += width / 2;
		}
		return true;
	}

	/*
	 * When decoding each macroblock the rlco pointer will be increased
	 * by 65 * 2 bytes worst-case.
	 * To avoid overflow the buffer has to be 65/64th of the actual raw
	 * image size, just in case someone feeds it malicious data.
	 */
	for (j = 0; j < height / 8; j++) {
		for (i = 0; i < width / 8; i++) {
			const u8 *refp = ref + j * 8 * ref_stride +
				i * 8 * ref_step;
			u8 *dstp = dst + j * 8 * dst_stride + i * 8 * dst_step;

			if (copies) {
				memcpy(cf->de_fwht, copy, sizeof(copy));
				if ((stat & PFRAME_BIT) && !is_intra)
					add_deltas(cf->de_fwht, refp,
						   ref_stride, ref_step);
				fill_decoder_block(dstp, cf->de_fwht,
						   dst_stride, dst_step);
				copies--;
				continue;
			}

			stat = derlc(rlco, cf->coeffs, end_of_rlco_buf);
			if (stat & OVERFLOW_BIT)
				return false;
			if ((stat & PFRAME_BIT) && !is_intra)
				dequantize_inter(cf->coeffs);
			else
				dequantize_intra(cf->coeffs);

			ifwht(cf->coeffs, cf->de_fwht,
			      ((stat & PFRAME_BIT) && !is_intra) ? 0 : 1);

			copies = (stat & DUPS_MASK) >> 1;
			if (copies)
				memcpy(copy, cf->de_fwht, sizeof(copy));
			if ((stat & PFRAME_BIT) && !is_intra)
				add_deltas(cf->de_fwht, refp,
					   ref_stride, ref_step);
			fill_decoder_block(dstp, cf->de_fwht, dst_stride,
					   dst_step);
		}
	}
	return true;
}

bool fwht_decode_frame(struct fwht_cframe *cf, u32 hdr_flags,
		       unsigned int components_num, unsigned int width,
		       unsigned int height, const struct fwht_raw_frame *ref,
		       unsigned int ref_stride, unsigned int ref_chroma_stride,
		       struct fwht_raw_frame *dst, unsigned int dst_stride,
		       unsigned int dst_chroma_stride)
{
	const __be16 *rlco = cf->rlc_data;
	const __be16 *end_of_rlco_buf = cf->rlc_data +
			(cf->size / sizeof(*rlco)) - 1;

	if (!decode_plane(cf, &rlco, height, width, ref->luma, ref_stride,
			  ref->luma_alpha_step, dst->luma, dst_stride,
			  dst->luma_alpha_step,
			  hdr_flags & FWHT_FL_LUMA_IS_UNCOMPRESSED,
			  end_of_rlco_buf))
		return false;

	if (components_num >= 3) {
		u32 h = height;
		u32 w = width;

		if (!(hdr_flags & FWHT_FL_CHROMA_FULL_HEIGHT))
			h /= 2;
		if (!(hdr_flags & FWHT_FL_CHROMA_FULL_WIDTH))
			w /= 2;

		if (!decode_plane(cf, &rlco, h, w, ref->cb, ref_chroma_stride,
				  ref->chroma_step, dst->cb, dst_chroma_stride,
				  dst->chroma_step,
				  hdr_flags & FWHT_FL_CB_IS_UNCOMPRESSED,
				  end_of_rlco_buf))
			return false;
		if (!decode_plane(cf, &rlco, h, w, ref->cr, ref_chroma_stride,
				  ref->chroma_step, dst->cr, dst_chroma_stride,
				  dst->chroma_step,
				  hdr_flags & FWHT_FL_CR_IS_UNCOMPRESSED,
				  end_of_rlco_buf))
			return false;
	}

	if (components_num == 4)
		if (!decode_plane(cf, &rlco, height, width, ref->alpha, ref_stride,
				  ref->luma_alpha_step, dst->alpha, dst_stride,
				  dst->luma_alpha_step,
				  hdr_flags & FWHT_FL_ALPHA_IS_UNCOMPRESSED,
				  end_of_rlco_buf))
			return false;
	return true;
}
