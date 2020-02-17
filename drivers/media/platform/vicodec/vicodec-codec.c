// SPDX-License-Identifier: GPL-2.0+
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
#include "vicodec-codec.h"

#define ALL_ZEROS 15
#define DEADZONE_WIDTH 20

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


static int rlc(const s16 *in, __be16 *output, int blocktype)
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
static s16 derlc(const __be16 **rlc_in, s16 *dwht_out)
{
	/* header */
	const __be16 *input = *rlc_in;
	s16 ret = ntohs(*input++);
	int dec_count = 0;
	s16 block[8 * 8 + 16];
	s16 *wp = block;
	int i;

	/*
	 * Now de-compress, it expands one byte to up to 15 bytes
	 * (or fills the remainder of the 64 bytes with zeroes if it
	 * is the last byte to expand).
	 *
	 * So block has to be 8 * 8 + 16 bytes, the '+ 16' is to
	 * allow for overflow if the incoming data was malformed.
	 */
	while (dec_count < 8 * 8) {
		s16 in = ntohs(*input++);
		int length = in & 0xf;
		int coeff = in >> 4;

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
	return ret;
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

static void quantize_intra(s16 *coeff, s16 *de_coeff)
{
	const int *quant = quant_table;
	int i, j;

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 8; i++, quant++, coeff++, de_coeff++) {
			*coeff >>= *quant;
			if (*coeff >= -DEADZONE_WIDTH &&
			    *coeff <= DEADZONE_WIDTH)
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

static void quantize_inter(s16 *coeff, s16 *de_coeff)
{
	const int *quant = quant_table_p;
	int i, j;

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 8; i++, quant++, coeff++, de_coeff++) {
			*coeff >>= *quant;
			if (*coeff >= -DEADZONE_WIDTH &&
			    *coeff <= DEADZONE_WIDTH)
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

static void fwht(const u8 *block, s16 *output_block, unsigned int stride,
		 unsigned int input_step, bool intra)
{
	/* we'll need more than 8 bits for the transformed coefficients */
	s32 workspace1[8], workspace2[8];
	const u8 *tmp = block;
	s16 *out = output_block;
	int add = intra ? 256 : 0;
	unsigned int i;

	/* stage 1 */
	stride *= input_step;

	for (i = 0; i < 8; i++, tmp += stride, out += 8) {
		if (input_step == 1) {
			workspace1[0]  = tmp[0] + tmp[1] - add;
			workspace1[1]  = tmp[0] - tmp[1];

			workspace1[2]  = tmp[2] + tmp[3] - add;
			workspace1[3]  = tmp[2] - tmp[3];

			workspace1[4]  = tmp[4] + tmp[5] - add;
			workspace1[5]  = tmp[4] - tmp[5];

			workspace1[6]  = tmp[6] + tmp[7] - add;
			workspace1[7]  = tmp[6] - tmp[7];
		} else {
			workspace1[0]  = tmp[0] + tmp[2] - add;
			workspace1[1]  = tmp[0] - tmp[2];

			workspace1[2]  = tmp[4] + tmp[6] - add;
			workspace1[3]  = tmp[4] - tmp[6];

			workspace1[4]  = tmp[8] + tmp[10] - add;
			workspace1[5]  = tmp[8] - tmp[10];

			workspace1[6]  = tmp[12] + tmp[14] - add;
			workspace1[7]  = tmp[12] - tmp[14];
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
static void fwht16(const s16 *block, s16 *output_block, int stride, int intra)
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

static void ifwht(const s16 *block, s16 *output_block, int intra)
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
		input += (stride - 8) * input_step;
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

static int decide_blocktype(const u8 *cur, const u8 *reference,
			    s16 *deltablock, unsigned int stride,
			    unsigned int input_step)
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

static void fill_decoder_block(u8 *dst, const s16 *input, int stride)
{
	int i, j;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < 8; j++, input++, dst++) {
			if (*input < 0)
				*dst = 0;
			else if (*input > 255)
				*dst = 255;
			else
				*dst = *input;
		}
		dst += stride - 8;
	}
}

static void add_deltas(s16 *deltas, const u8 *ref, int stride)
{
	int k, l;

	for (k = 0; k < 8; k++) {
		for (l = 0; l < 8; l++) {
			*deltas += *ref++;
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
		ref += stride - 8;
	}
}

static u32 encode_plane(u8 *input, u8 *refp, __be16 **rlco, __be16 *rlco_max,
			struct cframe *cf, u32 height, u32 width,
			unsigned int input_step,
			bool is_intra, bool next_is_intra)
{
	u8 *input_start = input;
	__be16 *rlco_start = *rlco;
	s16 deltablock[64];
	__be16 pframe_bit = htons(PFRAME_BIT);
	u32 encoding = 0;
	unsigned int last_size = 0;
	unsigned int i, j;

	for (j = 0; j < height / 8; j++) {
		for (i = 0; i < width / 8; i++) {
			/* intra code, first frame is always intra coded. */
			int blocktype = IBLOCK;
			unsigned int size;

			if (!is_intra)
				blocktype = decide_blocktype(input, refp,
					deltablock, width, input_step);
			if (is_intra || blocktype == IBLOCK) {
				fwht(input, cf->coeffs, width, input_step, 1);
				quantize_intra(cf->coeffs, cf->de_coeffs);
				blocktype = IBLOCK;
			} else {
				/* inter code */
				encoding |= FRAME_PCODED;
				fwht16(deltablock, cf->coeffs, 8, 0);
				quantize_inter(cf->coeffs, cf->de_coeffs);
			}
			if (!next_is_intra) {
				ifwht(cf->de_coeffs, cf->de_fwht, blocktype);

				if (blocktype == PBLOCK)
					add_deltas(cf->de_fwht, refp, 8);
				fill_decoder_block(refp, cf->de_fwht, 8);
			}

			input += 8 * input_step;
			refp += 8 * 8;

			if (encoding & FRAME_UNENCODED)
				continue;

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
			if (*rlco >= rlco_max)
				encoding |= FRAME_UNENCODED;
			last_size = size;
		}
		input += width * 7 * input_step;
	}
	if (encoding & FRAME_UNENCODED) {
		u8 *out = (u8 *)rlco_start;

		input = input_start;
		/*
		 * The compressed stream should never contain the magic
		 * header, so when we copy the YUV data we replace 0xff
		 * by 0xfe. Since YUV is limited range such values
		 * shouldn't appear anyway.
		 */
		for (i = 0; i < height * width; i++, input += input_step)
			*out++ = (*input == 0xff) ? 0xfe : *input;
		*rlco = (__be16 *)out;
	}
	return encoding;
}

u32 encode_frame(struct raw_frame *frm, struct raw_frame *ref_frm,
		 struct cframe *cf, bool is_intra, bool next_is_intra)
{
	unsigned int size = frm->height * frm->width;
	__be16 *rlco = cf->rlc_data;
	__be16 *rlco_max;
	u32 encoding;

	rlco_max = rlco + size / 2 - 256;
	encoding = encode_plane(frm->luma, ref_frm->luma, &rlco, rlco_max, cf,
				  frm->height, frm->width,
				  1, is_intra, next_is_intra);
	if (encoding & FRAME_UNENCODED)
		encoding |= LUMA_UNENCODED;
	encoding &= ~FRAME_UNENCODED;
	rlco_max = rlco + size / 8 - 256;
	encoding |= encode_plane(frm->cb, ref_frm->cb, &rlco, rlco_max, cf,
				   frm->height / 2, frm->width / 2,
				   frm->chroma_step, is_intra, next_is_intra);
	if (encoding & FRAME_UNENCODED)
		encoding |= CB_UNENCODED;
	encoding &= ~FRAME_UNENCODED;
	rlco_max = rlco + size / 8 - 256;
	encoding |= encode_plane(frm->cr, ref_frm->cr, &rlco, rlco_max, cf,
				   frm->height / 2, frm->width / 2,
				   frm->chroma_step, is_intra, next_is_intra);
	if (encoding & FRAME_UNENCODED)
		encoding |= CR_UNENCODED;
	encoding &= ~FRAME_UNENCODED;
	cf->size = (rlco - cf->rlc_data) * sizeof(*rlco);
	return encoding;
}

static void decode_plane(struct cframe *cf, const __be16 **rlco, u8 *ref,
			 u32 height, u32 width, bool uncompressed)
{
	unsigned int copies = 0;
	s16 copy[8 * 8];
	s16 stat;
	unsigned int i, j;

	if (uncompressed) {
		memcpy(ref, *rlco, width * height);
		*rlco += width * height / 2;
		return;
	}

	/*
	 * When decoding each macroblock the rlco pointer will be increased
	 * by 65 * 2 bytes worst-case.
	 * To avoid overflow the buffer has to be 65/64th of the actual raw
	 * image size, just in case someone feeds it malicious data.
	 */
	for (j = 0; j < height / 8; j++) {
		for (i = 0; i < width / 8; i++) {
			u8 *refp = ref + j * 8 * width + i * 8;

			if (copies) {
				memcpy(cf->de_fwht, copy, sizeof(copy));
				if (stat & PFRAME_BIT)
					add_deltas(cf->de_fwht, refp, width);
				fill_decoder_block(refp, cf->de_fwht, width);
				copies--;
				continue;
			}

			stat = derlc(rlco, cf->coeffs);

			if (stat & PFRAME_BIT)
				dequantize_inter(cf->coeffs);
			else
				dequantize_intra(cf->coeffs);

			ifwht(cf->coeffs, cf->de_fwht,
			      (stat & PFRAME_BIT) ? 0 : 1);

			copies = (stat & DUPS_MASK) >> 1;
			if (copies)
				memcpy(copy, cf->de_fwht, sizeof(copy));
			if (stat & PFRAME_BIT)
				add_deltas(cf->de_fwht, refp, width);
			fill_decoder_block(refp, cf->de_fwht, width);
		}
	}
}

void decode_frame(struct cframe *cf, struct raw_frame *ref, u32 hdr_flags)
{
	const __be16 *rlco = cf->rlc_data;

	decode_plane(cf, &rlco, ref->luma, cf->height, cf->width,
		     hdr_flags & VICODEC_FL_LUMA_IS_UNCOMPRESSED);
	decode_plane(cf, &rlco, ref->cb, cf->height / 2, cf->width / 2,
		     hdr_flags & VICODEC_FL_CB_IS_UNCOMPRESSED);
	decode_plane(cf, &rlco, ref->cr, cf->height / 2, cf->width / 2,
		     hdr_flags & VICODEC_FL_CR_IS_UNCOMPRESSED);
}
