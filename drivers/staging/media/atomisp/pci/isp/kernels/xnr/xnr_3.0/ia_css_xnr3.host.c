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

#include <linux/log2.h>

#include "type_support.h"
#include "math_support.h"
#include "sh_css_defs.h"
#include "ia_css_types.h"
#include "assert_support.h"
#include "ia_css_xnr3.host.h"

/* Maximum value for alpha on ISP interface */
#define XNR_MAX_ALPHA  ((1 << (ISP_VEC_ELEMBITS - 1)) - 1)

/* Minimum value for sigma on host interface. Lower values translate to
 * max_alpha.
 */
#define XNR_MIN_SIGMA  (IA_CSS_XNR3_SIGMA_SCALE / 100)

/*
 * division look-up table
 * Refers to XNR3.0.5
 */
#define XNR3_LOOK_UP_TABLE_POINTS 16

static const s16 x[XNR3_LOOK_UP_TABLE_POINTS] = {
	1024, 1164, 1320, 1492, 1680, 1884, 2108, 2352,
	2616, 2900, 3208, 3540, 3896, 4276, 4684, 5120
};

static const s16 a[XNR3_LOOK_UP_TABLE_POINTS] = {
	-7213, -5580, -4371, -3421, -2722, -2159, -6950, -5585,
	    -4529, -3697, -3010, -2485, -2070, -1727, -1428, 0
    };

static const s16 b[XNR3_LOOK_UP_TABLE_POINTS] = {
	4096, 3603, 3178, 2811, 2497, 2226, 1990, 1783,
	1603, 1446, 1307, 1185, 1077, 981, 895, 819
};

static const s16 c[XNR3_LOOK_UP_TABLE_POINTS] = {
	1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * Default kernel parameters. In general, default is bypass mode or as close
 * to the ineffective values as possible. Due to the chroma down+upsampling,
 * perfect bypass mode is not possible for xnr3 filter itself. Instead, the
 * 'blending' parameter is used to create a bypass.
 */
const struct ia_css_xnr3_config default_xnr3_config = {
	/* sigma */
	{ 0, 0, 0, 0, 0, 0 },
	/* coring */
	{ 0, 0, 0, 0 },
	/* blending */
	{ 0 }
};

/*
 * Compute an alpha value for the ISP kernel from sigma value on the host
 * parameter interface as: alpha_scale * 1/(sigma/sigma_scale)
 */
static int32_t
compute_alpha(int sigma)
{
	s32 alpha;
	int offset = sigma / 2;

	if (sigma < XNR_MIN_SIGMA) {
		alpha = XNR_MAX_ALPHA;
	} else {
		alpha = ((IA_CSS_XNR3_SIGMA_SCALE * XNR_ALPHA_SCALE_FACTOR) + offset) / sigma;

		if (alpha > XNR_MAX_ALPHA)
			alpha = XNR_MAX_ALPHA;
	}

	return alpha;
}

/*
 * Compute the scaled coring value for the ISP kernel from the value on the
 * host parameter interface.
 */
static int32_t
compute_coring(int coring)
{
	s32 isp_coring;
	s32 isp_scale = XNR_CORING_SCALE_FACTOR;
	s32 host_scale = IA_CSS_XNR3_CORING_SCALE;
	s32 offset = host_scale / 2; /* fixed-point 0.5 */

	/* Convert from public host-side scale factor to isp-side scale
	 * factor. Clip to [0, isp_scale-1).
	 */
	isp_coring = ((coring * isp_scale) + offset) / host_scale;
	return min(max(isp_coring, 0), isp_scale - 1);
}

/*
 * Compute the scaled blending strength for the ISP kernel from the value on
 * the host parameter interface.
 */
static int32_t
compute_blending(int strength)
{
	s32 isp_strength;
	s32 isp_scale = XNR_BLENDING_SCALE_FACTOR;
	s32 host_scale = IA_CSS_XNR3_BLENDING_SCALE;
	s32 offset = host_scale / 2; /* fixed-point 0.5 */

	/* Convert from public host-side scale factor to isp-side scale
	 * factor. The blending factor is positive on the host side, but
	 * negative on the ISP side because +1.0 cannot be represented
	 * exactly as s0.11 fixed point, but -1.0 can.
	 */
	isp_strength = -(((strength * isp_scale) + offset) / host_scale);
	return MAX(MIN(isp_strength, 0), -isp_scale);
}

void
ia_css_xnr3_encode(
    struct sh_css_isp_xnr3_params *to,
    const struct ia_css_xnr3_config *from,
    unsigned int size)
{
	int kernel_size = XNR_FILTER_SIZE;
	int adjust_factor = roundup_pow_of_two(kernel_size);
	s32 max_diff = (1 << (ISP_VEC_ELEMBITS - 1)) - 1;
	s32 min_diff = -(1 << (ISP_VEC_ELEMBITS - 1));

	s32 alpha_y0 = compute_alpha(from->sigma.y0);
	s32 alpha_y1 = compute_alpha(from->sigma.y1);
	s32 alpha_u0 = compute_alpha(from->sigma.u0);
	s32 alpha_u1 = compute_alpha(from->sigma.u1);
	s32 alpha_v0 = compute_alpha(from->sigma.v0);
	s32 alpha_v1 = compute_alpha(from->sigma.v1);
	s32 alpha_ydiff = (alpha_y1 - alpha_y0) * adjust_factor / kernel_size;
	s32 alpha_udiff = (alpha_u1 - alpha_u0) * adjust_factor / kernel_size;
	s32 alpha_vdiff = (alpha_v1 - alpha_v0) * adjust_factor / kernel_size;

	s32 coring_u0 = compute_coring(from->coring.u0);
	s32 coring_u1 = compute_coring(from->coring.u1);
	s32 coring_v0 = compute_coring(from->coring.v0);
	s32 coring_v1 = compute_coring(from->coring.v1);
	s32 coring_udiff = (coring_u1 - coring_u0) * adjust_factor / kernel_size;
	s32 coring_vdiff = (coring_v1 - coring_v0) * adjust_factor / kernel_size;

	s32 blending = compute_blending(from->blending.strength);

	(void)size;

	/* alpha's are represented in qN.5 format */
	to->alpha.y0 = alpha_y0;
	to->alpha.u0 = alpha_u0;
	to->alpha.v0 = alpha_v0;
	to->alpha.ydiff = min(max(alpha_ydiff, min_diff), max_diff);
	to->alpha.udiff = min(max(alpha_udiff, min_diff), max_diff);
	to->alpha.vdiff = min(max(alpha_vdiff, min_diff), max_diff);

	/* coring parameters are expressed in q1.NN format */
	to->coring.u0 = coring_u0;
	to->coring.v0 = coring_v0;
	to->coring.udiff = min(max(coring_udiff, min_diff), max_diff);
	to->coring.vdiff = min(max(coring_vdiff, min_diff), max_diff);

	/* blending strength is expressed in q1.NN format */
	to->blending.strength = blending;
}

/* ISP2401 */
/* (void) = ia_css_xnr3_vmem_encode(*to, *from)
 * -----------------------------------------------
 * VMEM Encode Function to translate UV parameters from userspace into ISP space
*/
void
ia_css_xnr3_vmem_encode(
    struct sh_css_isp_xnr3_vmem_params *to,
    const struct ia_css_xnr3_config *from,
    unsigned int size)
{
	unsigned int i, j, base;
	const unsigned int total_blocks = 4;
	const unsigned int shuffle_block = 16;

	(void)from;
	(void)size;

	/* Init */
	for (i = 0; i < ISP_VEC_NELEMS; i++) {
		to->x[0][i] = 0;
		to->a[0][i] = 0;
		to->b[0][i] = 0;
		to->c[0][i] = 0;
	}

	/* Constraints on "x":
	 * - values should be greater or equal to 0.
	 * - values should be ascending.
	 */
	assert(x[0] >= 0);

	for (j = 1; j < XNR3_LOOK_UP_TABLE_POINTS; j++) {
		assert(x[j] >= 0);
		assert(x[j] > x[j - 1]);
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

		for (j = 0; j < XNR3_LOOK_UP_TABLE_POINTS; j++) {
			to->x[0][base + j] = x[j];
			to->a[0][base + j] = a[j];
			to->b[0][base + j] = b[j];
			to->c[0][base + j] = c[j];
		}
	}
}

/* Dummy Function added as the tool expects it*/
void
ia_css_xnr3_debug_dtrace(
    const struct ia_css_xnr3_config *config,
    unsigned int level)
{
	(void)config;
	(void)level;
}
