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
#include "ia_css_iefd2_6.host.h"

/* Copy parameters to VMEM
 */
void
ia_css_iefd2_6_vmem_encode(
	struct iefd2_6_vmem_params *to,
	const struct ia_css_iefd2_6_config *from,
	size_t size)
{
	const unsigned total_blocks = 4;
	const unsigned shuffle_block = 16;
	unsigned i, j, base;
	(void)size;

	/* For configurable units parameters are copied to vmem. Per CU 3 arrays are copied:
	 * x containing the x coordinates
	 * a containing the slopes
	 * b containing the intercept values.
	 *
	 * A 64 element vector is split up in 4 blocks of 16 element. Each array is copied to
	 * a vector 4 times, (starting at 0, 16, 32 and 48). All array elements are copied or
	 * initialised as described in the KFS. The remaining elements of a vector are set to 0.
	 */
	/* first init the vectors */
	for(i = 0; i < total_blocks*shuffle_block; i++) {
		to->e_cued_x[0][i] = 0;
		to->e_cued_a[0][i] = 0;
		to->e_cued_b[0][i] = 0;

		to->e_cu_dir_x[0][i] = 0;
		to->e_cu_dir_a[0][i] = 0;
		to->e_cu_dir_b[0][i] = 0;

		to->e_cu_non_dir_x[0][i] = 0;
		to->e_cu_non_dir_a[0][i] = 0;
		to->e_cu_non_dir_b[0][i] = 0;

		to->e_curad_x[0][i] = 0;
		to->e_curad_a[0][i] = 0;
		to->e_curad_b[0][i] = 0;
	}

	/* Copy all data */
	for(i = 0; i < total_blocks; i++) {
		base = shuffle_block*i;


		to->e_cued_x[0][base] = 0;
		to->e_cued_a[0][base] = 0;
		to->e_cued_b[0][base] = from->cu_ed_slopes_b[0];

		to->e_cu_dir_x[0][base] = 0;
		to->e_cu_dir_a[0][base] = 0;
		to->e_cu_dir_b[0][base] = from->cu_dir_sharp_slopes_b[0];

		to->e_cu_non_dir_x[0][base] = 0;
		to->e_cu_non_dir_a[0][base] = 0;
		to->e_cu_non_dir_b[0][base] = from->cu_non_dir_sharp_slopes_b[0];

		to->e_curad_x[0][base] = 0;
		to->e_curad_a[0][base] = 0;
		to->e_curad_b[0][base] = from->cu_radial_slopes_b[0];

		for (j = 1; j < 4; j++) {
			to->e_cu_dir_a[0][base+j] = from->cu_dir_sharp_slopes_a[j-1];
			to->e_cu_dir_b[0][base+j] = from->cu_dir_sharp_slopes_b[j-1];
			to->e_cu_non_dir_a[0][base+j] = from->cu_non_dir_sharp_slopes_a[j-1];
			to->e_cu_non_dir_b[0][base+j] = from->cu_non_dir_sharp_slopes_b[j-1];
		}

		for (j = 1; j < 5; j++) {
			to->e_cu_dir_x[0][base+j] = from->cu_dir_sharp_points_x[j-1];
			to->e_cu_non_dir_x[0][base+j] = from->cu_non_dir_sharp_points_x[j-1];
		}


		for (j = 1; j < 6; j++) {
			to->e_cued_x[0][base+j] = from->cu_ed_points_x[j-1];
			to->e_cued_a[0][base+j] = from->cu_ed_slopes_a[j-1];
			to->e_cued_b[0][base+j] = from->cu_ed_slopes_b[j-1];
		}
		to->e_cued_x[0][base+6] = from->cu_ed_points_x[5];

		for (j = 1; j < 6; j++) {
			to->e_curad_x[0][base+j] = from->cu_radial_points_x[j-1];
			to->e_curad_a[0][base+j] = from->cu_radial_slopes_a[j-1];
			to->e_curad_b[0][base+j] = from->cu_radial_slopes_b[j-1];
		}
		to->e_curad_x[0][base+6] = from->cu_radial_points_x[5];

		/* Init asrrnd_lut */
		to->asrrnd_lut[0][base] = 8192;
		to->asrrnd_lut[0][base+1] = 4096;
		to->asrrnd_lut[0][base+2] = 2048;
		to->asrrnd_lut[0][base+3] = 1024;
		to->asrrnd_lut[0][base+4] = 512;
		to->asrrnd_lut[0][base+5] = 256;
		to->asrrnd_lut[0][base+6] = 128;
		to->asrrnd_lut[0][base+7] = 64;
		to->asrrnd_lut[0][base+8] = 32;
	}

}

void
ia_css_iefd2_6_encode(
	struct iefd2_6_dmem_params *to,
	const struct ia_css_iefd2_6_config *from,
	size_t size)
{
	(void)size;

	/* Copy parameters to dmem, as described in the KFS
	 */
	to->horver_diag_coeff		= from->horver_diag_coeff;
	to->ed_horver_diag_coeff	= from->ed_horver_diag_coeff;
	to->dir_smooth_enable		= from->dir_smooth_enable;
	to->dir_metric_update		= from->dir_metric_update;
	to->unsharp_c00			= from->unsharp_c00;
	to->unsharp_c01			= from->unsharp_c01;
	to->unsharp_c02			= from->unsharp_c02;
	to->unsharp_c11			= from->unsharp_c11;
	to->unsharp_c12			= from->unsharp_c12;
	to->unsharp_c22			= from->unsharp_c22;
	to->unsharp_weight		= from->unsharp_weight;
	to->unsharp_amount		= from->unsharp_amount;
	to->cu_dir_sharp_pow		= from->cu_dir_sharp_pow;
	to->cu_dir_sharp_pow_bright	= from->cu_dir_sharp_pow_bright;
	to->cu_non_dir_sharp_pow	= from->cu_non_dir_sharp_pow;
	to->cu_non_dir_sharp_pow_bright	= from->cu_non_dir_sharp_pow_bright;
	to->dir_far_sharp_weight	= from->dir_far_sharp_weight;
	to->rad_cu_dir_sharp_x1		= from->rad_cu_dir_sharp_x1;
	to->rad_cu_non_dir_sharp_x1	= from->rad_cu_non_dir_sharp_x1;
	to->rad_dir_far_sharp_weight	= from->rad_dir_far_sharp_weight;
	to->sharp_nega_lmt_txt		= from->sharp_nega_lmt_txt;
	to->sharp_posi_lmt_txt		= from->sharp_posi_lmt_txt;
	to->sharp_nega_lmt_dir		= from->sharp_nega_lmt_dir;
	to->sharp_posi_lmt_dir		= from->sharp_posi_lmt_dir;
	to->clamp_stitch		= from->clamp_stitch;
	to->rad_enable			= from->rad_enable;
	to->rad_x_origin		= from->rad_x_origin;
	to->rad_y_origin		= from->rad_y_origin;
	to->rad_nf			= from->rad_nf;
	to->rad_inv_r2			= from->rad_inv_r2;
	to->vssnlm_enable		= from->vssnlm_enable;
	to->vssnlm_x0			= from->vssnlm_x0;
	to->vssnlm_x1			= from->vssnlm_x1;
	to->vssnlm_x2			= from->vssnlm_x2;
	to->vssnlm_y1			= from->vssnlm_y1;
	to->vssnlm_y2			= from->vssnlm_y2;
	to->vssnlm_y3			= from->vssnlm_y3;

	/* Setup for configurable units */
	to->e_cued2_a		= from->cu_ed2_slopes_a;
	to->e_cu_vssnlm_a	= from->cu_vssnlm_slopes_a;
	to->e_cued2_x1		= from->cu_ed2_points_x[0];
	to->e_cued2_x_diff	= from->cu_ed2_points_x[1] - from->cu_ed2_points_x[0];
	to->e_cu_vssnlm_x1	= from->cu_vssnlm_points_x[0];
	to->e_cu_vssnlm_x_diff  = from->cu_vssnlm_points_x[1] - from->cu_vssnlm_points_x[0];
}

/* TODO: AM: This needs a proper implementation. */
void
ia_css_init_iefd2_6_state(
	void *state,
	size_t size)
{
	(void)state;
	(void)size;
}

#ifndef IA_CSS_NO_DEBUG
/* TODO: AM: This needs a proper implementation. */
void
ia_css_iefd2_6_debug_dtrace(
	const struct ia_css_iefd2_6_config *config,
	unsigned level)
{
	(void)config;
	(void)level;
}
#endif
