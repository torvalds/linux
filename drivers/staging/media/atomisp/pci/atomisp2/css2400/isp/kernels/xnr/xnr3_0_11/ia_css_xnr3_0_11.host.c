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

#include "type_support.h"
#include "math_support.h"
#include "sh_css_defs.h"
#include "assert_support.h"
#include "ia_css_xnr3_0_11.host.h"

/*
 * XNR 3.0.11 division look-up table
 */
#define XNR3_0_11_LOOK_UP_TABLE_POINTS 16

static const int16_t x[XNR3_0_11_LOOK_UP_TABLE_POINTS] = {
512, 637, 782, 952, 1147, 1372, 1627, 1917, 2242,
2597, 2992, 3427, 3907, 4432, 5007, 5632};

static const int16_t a[XNR3_0_11_LOOK_UP_TABLE_POINTS] = {
-6587, -4309, -2886, -1970, -1362, -7710, -5508,
-4008, -2931, -2219, -1676, -1280, -999, -769, -616, 0};

static const int16_t b[XNR3_0_11_LOOK_UP_TABLE_POINTS] = {
4096, 3292, 2682, 2203, 1828, 1529, 1289, 1094,
935, 808, 701, 612, 537, 473, 419, 372};

static const int16_t c[XNR3_0_11_LOOK_UP_TABLE_POINTS] = {
1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


/*
 * Default kernel parameters (weights). In general, default is bypass mode or as close
 * to the ineffective values as possible. Due to the chroma down+upsampling,
 * perfect bypass mode is not possible for xnr3.
 */
const struct ia_css_xnr3_0_11_config default_xnr3_0_11_config = {
	7, 7, 7, 7, 7, 2 };


/* (void) = ia_css_xnr3_0_11_vmem_encode(*to, *from)
 * -----------------------------------------------
 * VMEM Encode Function to translate UV parameters from userspace into ISP space
*/
void
ia_css_xnr3_0_11_vmem_encode(
	struct sh_css_isp_xnr3_0_11_vmem_params *to,
	const struct ia_css_xnr3_0_11_config *from,
	unsigned size)
{
	unsigned i, j, base;
	const unsigned total_blocks = 4;
	const unsigned shuffle_block = 16;

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

	for (j = 1; j < XNR3_0_11_LOOK_UP_TABLE_POINTS; j++) {
		assert(x[j] >= 0);
		assert(x[j] > x[j-1]);

	}


	/* The implementation of the calulating 1/x is based on the availability
	 * of the OP_vec_shuffle16 operation.
	 * A 64 element vector is split up in 4 blocks of 16 element. Each array is copied to
	 * a vector 4 times, (starting at 0, 16, 32 and 48). All array elements are copied or
	 * initialised as described in the KFS. The remaining elements of a vector are set to 0.
	 */
	/* TODO: guard this code with above assumptions */
	for(i = 0; i < total_blocks; i++) {
		base = shuffle_block * i;

		for (j = 0; j < XNR3_0_11_LOOK_UP_TABLE_POINTS; j++) {
			to->x[0][base + j] = x[j];
			to->a[0][base + j] = a[j];
			to->b[0][base + j] = b[j];
			to->c[0][base + j] = c[j];
		}
	}

}



/* (void) = ia_css_xnr3_0_11_encode(*to, *from)
 * -----------------------------------------------
 * DMEM Encode Function to translate UV parameters from userspace into ISP space
 */
void
ia_css_xnr3_0_11_encode(
	struct sh_css_isp_xnr3_0_11_params *to,
	const struct ia_css_xnr3_0_11_config *from,
	unsigned size)
{
	int kernel_size = XNR_FILTER_SIZE;
	/* The adjust factor is the next power of 2
	   w.r.t. the kernel size*/
	int adjust_factor = ceil_pow2(kernel_size);

	int32_t weight_y0 = from->weight_y0;
	int32_t weight_y1 = from->weight_y1;
	int32_t weight_u0 = from->weight_u0;
	int32_t weight_u1 = from->weight_u1;
	int32_t weight_v0 = from->weight_v0;
	int32_t weight_v1 = from->weight_v1;

	(void)size;

	to->weight_y0 = weight_y0;
	to->weight_u0 = weight_u0;
	to->weight_v0 = weight_v0;
	to->weight_ydiff = (weight_y1 - weight_y0) * adjust_factor / kernel_size;
	to->weight_udiff = (weight_u1 - weight_u0) * adjust_factor / kernel_size;
	to->weight_vdiff = (weight_v1 - weight_v0) * adjust_factor / kernel_size;
}

/* (void) = ia_css_xnr3_0_11_debug_dtrace(*config, level)
 * -----------------------------------------------
 * Dummy Function added as the tool expects it
 */
void
ia_css_xnr3_0_11_debug_dtrace(
	const struct ia_css_xnr3_0_11_config *config,
	unsigned level)
{
	(void)config;
	(void)level;
}
