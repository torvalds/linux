// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef IA_CSS_NO_DEBUG
#include "ia_css_debug.h"
#endif

#include "type_support.h"
#include "assert_support.h"
#include "math_support.h" /* for min and max */

#include "ia_css_eed1_8.host.h"

/* WARNING1: Number of inv points should be less or equal to 16,
 * due to implementation limitation. See kernel design document
 * for more details.
 * WARNING2: Do not modify the number of inv points without correcting
 * the EED1_8 kernel implementation assumptions.
 */
#define NUMBER_OF_CHGRINV_POINTS 15
#define NUMBER_OF_TCINV_POINTS 9
#define NUMBER_OF_FCINV_POINTS 9

static const s16 chgrinv_x[NUMBER_OF_CHGRINV_POINTS] = {
	0, 16, 64, 144, 272, 448, 672, 976,
	1376, 1888, 2528, 3312, 4256, 5376, 6688
};

static const s16 chgrinv_a[NUMBER_OF_CHGRINV_POINTS] = {
	-7171, -256, -29, -3456, -1071, -475, -189, -102,
	    -48, -38, -10, -9, -7, -6, 0
    };

static const s16 chgrinv_b[NUMBER_OF_CHGRINV_POINTS] = {
	8191, 1021, 256, 114, 60, 37, 24, 17,
	12, 9, 6, 5, 4, 3, 2
};

static const s16 chgrinv_c[NUMBER_OF_CHGRINV_POINTS] = {
	1, 1, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0
};

static const s16 tcinv_x[NUMBER_OF_TCINV_POINTS] = {
	0, 4, 11, 23, 42, 68, 102, 148, 205
};

static const s16 tcinv_a[NUMBER_OF_TCINV_POINTS] = {
	-6364, -631, -126, -34, -13, -6, -4452, -2156, 0
    };

static const s16 tcinv_b[NUMBER_OF_TCINV_POINTS] = {
	8191, 1828, 726, 352, 197, 121, 80, 55, 40
};

static const s16 tcinv_c[NUMBER_OF_TCINV_POINTS] = {
	1, 1, 1, 1, 1, 1, 0, 0, 0
};

static const s16 fcinv_x[NUMBER_OF_FCINV_POINTS] = {
	0, 80, 216, 456, 824, 1344, 2040, 2952, 4096
};

static const s16 fcinv_a[NUMBER_OF_FCINV_POINTS] = {
	-5244, -486, -86, -2849, -961, -400, -180, -86, 0
    };

static const s16 fcinv_b[NUMBER_OF_FCINV_POINTS] = {
	8191, 1637, 607, 287, 159, 98, 64, 44, 32
};

static const s16 fcinv_c[NUMBER_OF_FCINV_POINTS] = {
	1, 1, 1, 0, 0, 0, 0, 0, 0
};

void
ia_css_eed1_8_vmem_encode(
    struct eed1_8_vmem_params *to,
    const struct ia_css_eed1_8_config *from,
    size_t size)
{
	unsigned int i, j, base;
	const unsigned int total_blocks = 4;
	const unsigned int shuffle_block = 16;

	(void)size;

	/* Init */
	for (i = 0; i < ISP_VEC_NELEMS; i++) {
		to->e_dew_enh_x[0][i] = 0;
		to->e_dew_enh_y[0][i] = 0;
		to->e_dew_enh_a[0][i] = 0;
		to->e_dew_enh_f[0][i] = 0;
		to->chgrinv_x[0][i] = 0;
		to->chgrinv_a[0][i] = 0;
		to->chgrinv_b[0][i] = 0;
		to->chgrinv_c[0][i] = 0;
		to->tcinv_x[0][i] = 0;
		to->tcinv_a[0][i] = 0;
		to->tcinv_b[0][i] = 0;
		to->tcinv_c[0][i] = 0;
		to->fcinv_x[0][i] = 0;
		to->fcinv_a[0][i] = 0;
		to->fcinv_b[0][i] = 0;
		to->fcinv_c[0][i] = 0;
	}

	/* Constraints on dew_enhance_seg_x and dew_enhance_seg_y:
	 * - values should be greater or equal to 0.
	 * - values should be ascending.
	 * - value of index zero is equal to 0.
	 */

	/* Checking constraints: */
	/* TODO: investigate if an assert is the right way to report that
	 * the constraints are violated.
	 */
	for (j = 0; j < IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS; j++) {
		assert(from->dew_enhance_seg_x[j] > -1);
		assert(from->dew_enhance_seg_y[j] > -1);
	}

	for (j = 1; j < IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS; j++) {
		assert(from->dew_enhance_seg_x[j] > from->dew_enhance_seg_x[j - 1]);
		assert(from->dew_enhance_seg_y[j] > from->dew_enhance_seg_y[j - 1]);
	}

	assert(from->dew_enhance_seg_x[0] == 0);
	assert(from->dew_enhance_seg_y[0] == 0);

	/* Constraints on chgrinv_x, tcinv_x and fcinv_x:
	 * - values should be greater or equal to 0.
	 * - values should be ascending.
	 * - value of index zero is equal to 0.
	 */
	assert(chgrinv_x[0] == 0);
	assert(tcinv_x[0] == 0);
	assert(fcinv_x[0] == 0);

	for (j = 1; j < NUMBER_OF_CHGRINV_POINTS; j++) {
		assert(chgrinv_x[j] > chgrinv_x[j - 1]);
	}

	for (j = 1; j < NUMBER_OF_TCINV_POINTS; j++) {
		assert(tcinv_x[j] > tcinv_x[j - 1]);
	}

	for (j = 1; j < NUMBER_OF_FCINV_POINTS; j++) {
		assert(fcinv_x[j] > fcinv_x[j - 1]);
	}

	/* The implementation of the calulating 1/x is based on the availability
	 * of the OP_vec_shuffle16 operation.
	 * A 64 element vector is split up in 4 blocks of 16 element. Each array is copied to
	 * a vector 4 times, (starting at 0, 16, 32 and 48). All array elements are copied or
	 * initialised as described in the KFS. The remaining elements of a vector are set to 0.
	 */
	/* TODO: guard this code with above assumptions */
	for (i = 0; i < total_blocks; i++) {
		base = shuffle_block * i;

		for (j = 0; j < IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS; j++) {
			to->e_dew_enh_x[0][base + j] = min_t(int, max_t(int,
							     from->dew_enhance_seg_x[j], 0),
							     8191);
			to->e_dew_enh_y[0][base + j] = min_t(int, max_t(int,
							     from->dew_enhance_seg_y[j], -8192),
							     8191);
		}

		for (j = 0; j < (IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS - 1); j++) {
			to->e_dew_enh_a[0][base + j] = min_t(int, max_t(int,
							     from->dew_enhance_seg_slope[j],
							     -8192), 8191);
			/* Convert dew_enhance_seg_exp to flag:
			 * 0 -> 0
			 * 1...13 -> 1
			 */
			to->e_dew_enh_f[0][base + j] = (min_t(int, max_t(int,
							      from->dew_enhance_seg_exp[j],
							      0), 13) > 0);
		}

		/* Hard-coded to 0, in order to be able to handle out of
		 * range input in the same way as the other segments.
		 * See KFS for more details.
		 */
		to->e_dew_enh_a[0][base + (IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS - 1)] = 0;
		to->e_dew_enh_f[0][base + (IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS - 1)] = 0;

		for (j = 0; j < NUMBER_OF_CHGRINV_POINTS; j++) {
			to->chgrinv_x[0][base + j] = chgrinv_x[j];
			to->chgrinv_a[0][base + j] = chgrinv_a[j];
			to->chgrinv_b[0][base + j] = chgrinv_b[j];
			to->chgrinv_c[0][base + j] = chgrinv_c[j];
		}

		for (j = 0; j < NUMBER_OF_TCINV_POINTS; j++) {
			to->tcinv_x[0][base + j] = tcinv_x[j];
			to->tcinv_a[0][base + j] = tcinv_a[j];
			to->tcinv_b[0][base + j] = tcinv_b[j];
			to->tcinv_c[0][base + j] = tcinv_c[j];
		}

		for (j = 0; j < NUMBER_OF_FCINV_POINTS; j++) {
			to->fcinv_x[0][base + j] = fcinv_x[j];
			to->fcinv_a[0][base + j] = fcinv_a[j];
			to->fcinv_b[0][base + j] = fcinv_b[j];
			to->fcinv_c[0][base + j] = fcinv_c[j];
		}
	}
}

void
ia_css_eed1_8_encode(
    struct eed1_8_dmem_params *to,
    const struct ia_css_eed1_8_config *from,
    size_t size)
{
	int i;
	int min_exp = 0;

	(void)size;

	to->rbzp_strength = from->rbzp_strength;

	to->fcstrength = from->fcstrength;
	to->fcthres_0 = from->fcthres_0;
	to->fc_sat_coef = from->fc_sat_coef;
	to->fc_coring_prm = from->fc_coring_prm;
	to->fc_slope = from->fcthres_1 - from->fcthres_0;

	to->aerel_thres0 = from->aerel_thres0;
	to->aerel_gain0 = from->aerel_gain0;
	to->aerel_thres_diff = from->aerel_thres1 - from->aerel_thres0;
	to->aerel_gain_diff = from->aerel_gain1 - from->aerel_gain0;

	to->derel_thres0 = from->derel_thres0;
	to->derel_gain0 = from->derel_gain0;
	to->derel_thres_diff = (from->derel_thres1 - from->derel_thres0);
	to->derel_gain_diff = (from->derel_gain1 - from->derel_gain0);

	to->coring_pos0 = from->coring_pos0;
	to->coring_pos_diff = (from->coring_pos1 - from->coring_pos0);
	to->coring_neg0 = from->coring_neg0;
	to->coring_neg_diff = (from->coring_neg1 - from->coring_neg0);

	/* Note: (ISP_VEC_ELEMBITS -1)
	 * TODO: currently the testbench does not support to use
	 * ISP_VEC_ELEMBITS. Investigate how to fix this
	 */
	to->gain_exp = (13 - from->gain_exp);
	to->gain_pos0 = from->gain_pos0;
	to->gain_pos_diff = (from->gain_pos1 - from->gain_pos0);
	to->gain_neg0 = from->gain_neg0;
	to->gain_neg_diff = (from->gain_neg1 - from->gain_neg0);

	to->margin_pos0 = from->pos_margin0;
	to->margin_pos_diff = (from->pos_margin1 - from->pos_margin0);
	to->margin_neg0 = from->neg_margin0;
	to->margin_neg_diff = (from->neg_margin1 - from->neg_margin0);

	/* Encode DEWEnhance exp (e_dew_enh_asr) */
	for (i = 0; i < (IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS - 1); i++) {
		min_exp = max(min_exp, from->dew_enhance_seg_exp[i]);
	}
	to->e_dew_enh_asr = 13 - min(max(min_exp, 0), 13);

	to->dedgew_max = from->dedgew_max;
}

void
ia_css_init_eed1_8_state(
    void *state,
    size_t size)
{
	memset(state, 0, size);
}

#ifndef IA_CSS_NO_DEBUG
void
ia_css_eed1_8_debug_dtrace(
    const struct ia_css_eed1_8_config *eed,
    unsigned int level)
{
	if (!eed)
		return;

	ia_css_debug_dtrace(level, "Edge Enhancing Demosaic 1.8:\n");
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "rbzp_strength",
			    eed->rbzp_strength);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "fcstrength", eed->fcstrength);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "fcthres_0", eed->fcthres_0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "fcthres_1", eed->fcthres_1);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "fc_sat_coef", eed->fc_sat_coef);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "fc_coring_prm",
			    eed->fc_coring_prm);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "aerel_thres0", eed->aerel_thres0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "aerel_gain0", eed->aerel_gain0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "aerel_thres1", eed->aerel_thres1);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "aerel_gain1", eed->aerel_gain1);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "derel_thres0", eed->derel_thres0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "derel_gain0", eed->derel_gain0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "derel_thres1", eed->derel_thres1);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "derel_gain1", eed->derel_gain1);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "coring_pos0", eed->coring_pos0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "coring_pos1", eed->coring_pos1);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "coring_neg0", eed->coring_neg0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "coring_neg1", eed->coring_neg1);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "gain_exp", eed->gain_exp);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "gain_pos0", eed->gain_pos0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "gain_pos1", eed->gain_pos1);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "gain_neg0", eed->gain_neg0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "gain_neg1", eed->gain_neg1);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "pos_margin0", eed->pos_margin0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "pos_margin1", eed->pos_margin1);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "neg_margin0", eed->neg_margin0);
	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "neg_margin1", eed->neg_margin1);

	ia_css_debug_dtrace(level, "\t%-32s = %d\n", "dedgew_max", eed->dedgew_max);
}
#endif
