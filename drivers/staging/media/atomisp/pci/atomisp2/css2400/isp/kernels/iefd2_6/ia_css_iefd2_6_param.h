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

#ifndef __IA_CSS_IEFD2_6_PARAM_H
#define __IA_CSS_IEFD2_6_PARAM_H

#include "type_support.h"
#include "vmem.h" /* needed for VMEM_ARRAY */

struct iefd2_6_vmem_params {
	VMEM_ARRAY(e_cued_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cued_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cued_b, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cu_dir_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cu_dir_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cu_dir_b, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cu_non_dir_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cu_non_dir_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_cu_non_dir_b, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_curad_x, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_curad_a, ISP_VEC_NELEMS);
	VMEM_ARRAY(e_curad_b, ISP_VEC_NELEMS);
	VMEM_ARRAY(asrrnd_lut, ISP_VEC_NELEMS);
};

struct iefd2_6_dmem_params {
	int32_t horver_diag_coeff;
	int32_t ed_horver_diag_coeff;
	bool dir_smooth_enable;
	int32_t dir_metric_update;
	int32_t unsharp_c00;
	int32_t unsharp_c01;
	int32_t unsharp_c02;
	int32_t unsharp_c11;
	int32_t unsharp_c12;
	int32_t unsharp_c22;
	int32_t unsharp_weight;
	int32_t unsharp_amount;
	int32_t cu_dir_sharp_pow;
	int32_t cu_dir_sharp_pow_bright;
	int32_t cu_non_dir_sharp_pow;
	int32_t cu_non_dir_sharp_pow_bright;
	int32_t dir_far_sharp_weight;
	int32_t rad_cu_dir_sharp_x1;
	int32_t rad_cu_non_dir_sharp_x1;
	int32_t rad_dir_far_sharp_weight;
	int32_t sharp_nega_lmt_txt;
	int32_t sharp_posi_lmt_txt;
	int32_t sharp_nega_lmt_dir;
	int32_t sharp_posi_lmt_dir;
	int32_t clamp_stitch;
	bool rad_enable;
	int32_t rad_x_origin;
	int32_t rad_y_origin;
	int32_t rad_nf;
	int32_t rad_inv_r2;
	bool vssnlm_enable;
	int32_t vssnlm_x0;
	int32_t vssnlm_x1;
	int32_t vssnlm_x2;
	int32_t vssnlm_y1;
	int32_t vssnlm_y2;
	int32_t vssnlm_y3;
	int32_t e_cued2_a;
	int32_t e_cued2_x1;
	int32_t e_cued2_x_diff;
	int32_t e_cu_vssnlm_a;
	int32_t e_cu_vssnlm_x1;
	int32_t e_cu_vssnlm_x_diff;
};

#endif /* __IA_CSS_IEFD2_6_PARAM_H */
