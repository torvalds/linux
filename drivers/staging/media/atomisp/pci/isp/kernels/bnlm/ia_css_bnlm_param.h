/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_BNLM_PARAM_H
#define __IA_CSS_BNLM_PARAM_H

#include "type_support.h"
#include "vmem.h" /* needed for VMEM_ARRAY */

struct bnlm_lut {
	VMEM_ARRAY(thr, ISP_VEC_NELEMS); /* thresholds */
	VMEM_ARRAY(val, ISP_VEC_NELEMS); /* values */
};

struct bnlm_vmem_params {
	VMEM_ARRAY(nl_th, ISP_VEC_NELEMS);
	VMEM_ARRAY(match_quality_max_idx, ISP_VEC_NELEMS);
	struct bnlm_lut mu_root_lut;
	struct bnlm_lut sad_norm_lut;
	struct bnlm_lut sig_detail_lut;
	struct bnlm_lut sig_rad_lut;
	struct bnlm_lut rad_pow_lut;
	struct bnlm_lut nl_0_lut;
	struct bnlm_lut nl_1_lut;
	struct bnlm_lut nl_2_lut;
	struct bnlm_lut nl_3_lut;

	/* LUTs used for division approximiation */
	struct bnlm_lut div_lut;

	VMEM_ARRAY(div_lut_intercepts, ISP_VEC_NELEMS);

	/* 240x does not have an ISP instruction to left shift each element of a
	 * vector by different shift value. Hence it will be simulated by multiplying
	 * the elements by required 2^shift. */
	VMEM_ARRAY(power_of_2, ISP_VEC_NELEMS);
};

/* BNLM ISP parameters */
struct bnlm_dmem_params {
	bool rad_enable;
	s32 rad_x_origin;
	s32 rad_y_origin;
	s32 avg_min_th;
	s32 max_min_th;

	s32 exp_coeff_a;
	u32 exp_coeff_b;
	s32 exp_coeff_c;
	u32 exp_exponent;
};

#endif /* __IA_CSS_BNLM_PARAM_H */
