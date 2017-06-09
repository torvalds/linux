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

#ifndef __IA_CSS_EED1_8_PARAM_H
#define __IA_CSS_EED1_8_PARAM_H

#include "type_support.h"
#include "vmem.h" /* needed for VMEM_ARRAY */

#include "ia_css_eed1_8_types.h" /* IA_CSS_NUMBER_OF_DEW_ENHANCE_SEGMENTS */


/* Configuration parameters: */

/* Enable median for false color correction
 * 0: Do not use median
 * 1: Use median
 * Default: 1
 */
#define EED1_8_FC_ENABLE_MEDIAN		1

/* Coring Threshold minima
 * Used in Tint color suppression.
 * Default: 1
 */
#define EED1_8_CORINGTHMIN	1

/* Define size of the state..... TODO: check if this is the correct place */
/* 4 planes : GR, R, B, GB */
#define NUM_PLANES	4

/* 5 lines state per color plane input_line_state */
#define EED1_8_STATE_INPUT_BUFFER_HEIGHT	(5 * NUM_PLANES)

/* Each plane has width equal to half frame line */
#define EED1_8_STATE_INPUT_BUFFER_WIDTH	CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 1 line state per color plane LD_H state */
#define EED1_8_STATE_LD_H_HEIGHT	(1 * NUM_PLANES)
#define EED1_8_STATE_LD_H_WIDTH		CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 1 line state per color plane LD_V state */
#define EED1_8_STATE_LD_V_HEIGHT	(1 * NUM_PLANES)
#define EED1_8_STATE_LD_V_WIDTH		CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 1 line (single plane) state for D_Hr state */
#define EED1_8_STATE_D_HR_HEIGHT	1
#define EED1_8_STATE_D_HR_WIDTH		CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 1 line (single plane) state for D_Hb state */
#define EED1_8_STATE_D_HB_HEIGHT	1
#define EED1_8_STATE_D_HB_WIDTH		CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 2 lines (single plane) state for D_Vr state */
#define EED1_8_STATE_D_VR_HEIGHT	2
#define EED1_8_STATE_D_VR_WIDTH		CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 2 line (single plane) state for D_Vb state */
#define EED1_8_STATE_D_VB_HEIGHT	2
#define EED1_8_STATE_D_VB_WIDTH		CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 2 lines state for R and B (= 2 planes) rb_zipped_state */
#define EED1_8_STATE_RB_ZIPPED_HEIGHT	(2 * 2)
#define EED1_8_STATE_RB_ZIPPED_WIDTH	CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

#if EED1_8_FC_ENABLE_MEDIAN
/* 1 full input line (GR-R color line) for Yc state */
#define EED1_8_STATE_YC_HEIGHT	1
#define EED1_8_STATE_YC_WIDTH	MAX_FRAME_SIMDWIDTH

/* 1 line state per color plane Cg_state */
#define EED1_8_STATE_CG_HEIGHT	(1 * NUM_PLANES)
#define EED1_8_STATE_CG_WIDTH	CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 1 line state per color plane Co_state */
#define EED1_8_STATE_CO_HEIGHT	(1 * NUM_PLANES)
#define EED1_8_STATE_CO_WIDTH	CEIL_DIV(MAX_FRAME_SIMDWIDTH, 2)

/* 1 full input line (GR-R color line) for AbsK state */
#define EED1_8_STATE_ABSK_HEIGHT	1
#define EED1_8_STATE_ABSK_WIDTH		MAX_FRAME_SIMDWIDTH
#endif

struct eed1_8_vmem_params {
	VMEM_ARRAY(e_dew_enh_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_dew_enh_y, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_dew_enh_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_dew_enh_f, ISP_VEC_NELEMS);
	VMEM_ARRAY(chgrinv_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(chgrinv_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(chgrinv_b, ISP_VEC_NELEMS);
	VMEM_ARRAY(chgrinv_c, ISP_VEC_NELEMS);
	VMEM_ARRAY(fcinv_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(fcinv_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(fcinv_b, ISP_VEC_NELEMS);
	VMEM_ARRAY(fcinv_c, ISP_VEC_NELEMS);
	VMEM_ARRAY(tcinv_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(tcinv_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(tcinv_b, ISP_VEC_NELEMS);
	VMEM_ARRAY(tcinv_c, ISP_VEC_NELEMS);
};

/* EED (Edge Enhancing Demosaic) ISP parameters */
struct eed1_8_dmem_params {
	int32_t rbzp_strength;

	int32_t fcstrength;
	int32_t fcthres_0;
	int32_t fc_sat_coef;
	int32_t fc_coring_prm;
	int32_t fc_slope;

	int32_t aerel_thres0;
	int32_t aerel_gain0;
	int32_t aerel_thres_diff;
	int32_t aerel_gain_diff;

	int32_t derel_thres0;
	int32_t derel_gain0;
	int32_t derel_thres_diff;
	int32_t derel_gain_diff;

	int32_t coring_pos0;
	int32_t coring_pos_diff;
	int32_t coring_neg0;
	int32_t coring_neg_diff;

	int32_t gain_exp;
	int32_t gain_pos0;
	int32_t gain_pos_diff;
	int32_t gain_neg0;
	int32_t gain_neg_diff;

	int32_t margin_pos0;
	int32_t margin_pos_diff;
	int32_t margin_neg0;
	int32_t margin_neg_diff;

	int32_t e_dew_enh_asr;
	int32_t dedgew_max;
};

#endif /* __IA_CSS_EED1_8_PARAM_H */
