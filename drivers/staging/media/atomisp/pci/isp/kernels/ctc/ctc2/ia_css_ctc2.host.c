// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "ia_css_types.h"
#include "sh_css_defs.h"
#include "assert_support.h"

#include "ia_css_ctc2.host.h"

#define INEFFECTIVE_VAL 4096
#define BASIC_VAL 819

/*Default configuration of parameters for Ctc2*/
const struct ia_css_ctc2_config default_ctc2_config = {
	INEFFECTIVE_VAL, INEFFECTIVE_VAL, INEFFECTIVE_VAL,
	INEFFECTIVE_VAL, INEFFECTIVE_VAL, INEFFECTIVE_VAL,
	BASIC_VAL * 2, BASIC_VAL * 4, BASIC_VAL * 6,
	BASIC_VAL * 8, INEFFECTIVE_VAL, INEFFECTIVE_VAL,
	BASIC_VAL >> 1, BASIC_VAL
};

/* (dydx) = ctc2_slope(y1, y0, x1, x0)
 * -----------------------------------------------
 * Calculation of the Slope of a Line = ((y1 - y0) >> 8)/(x1 - x0)
 *
 * Note: y1, y0 , x1 & x0 must lie within the range 0 <-> 8191
 */
static int ctc2_slope(int y1, int y0, int x1, int x0)
{
	const int shift_val = 8;
	const int max_slope = (1 << IA_CSS_CTC_COEF_SHIFT) - 1;
	int dy = y1 - y0;
	int dx = x1 - x0;
	int rounding = (dx + 1) >> 1;
	int dy_shift = dy << shift_val;
	int slope, dydx;

	/*Protection for parameter values, & avoiding zero divisions*/
	assert(y0 >= 0 && y0 <= max_slope);
	assert(y1 >= 0 && y1 <= max_slope);
	assert(x0 >= 0 && x0 <= max_slope);
	assert(x1 > 0 && x1 <= max_slope);
	assert(dx > 0);

	if (dy < 0)
		rounding = -rounding;
	slope = (int)(dy_shift + rounding) / dx;

	/*the slope must lie within the range
	  (-max_slope-1) >= (dydx) >= (max_slope)
	*/
	if (slope <= -max_slope - 1) {
		dydx = -max_slope - 1;
	} else if (slope >= max_slope) {
		dydx = max_slope;
	} else {
		dydx = slope;
	}

	return dydx;
}

/* (void) = ia_css_ctc2_vmem_encode(*to, *from)
 * -----------------------------------------------
 * VMEM Encode Function to translate Y parameters from userspace into ISP space
 */
void ia_css_ctc2_vmem_encode(struct ia_css_isp_ctc2_vmem_params *to,
			     const struct ia_css_ctc2_config *from,
			     size_t size)
{
	unsigned int i, j;
	const unsigned int shffl_blck = 4;
	const unsigned int length_zeros = 11;
	short dydx0, dydx1, dydx2, dydx3, dydx4;

	(void)size;
	/*
	*  Calculation of slopes of lines interconnecting
	*  0.0 -> y_x1 -> y_x2 -> y _x3 -> y_x4 -> 1.0
	*/
	dydx0 = ctc2_slope(from->y_y1, from->y_y0,
			   from->y_x1, 0);
	dydx1 = ctc2_slope(from->y_y2, from->y_y1,
			   from->y_x2, from->y_x1);
	dydx2 = ctc2_slope(from->y_y3, from->y_y2,
			   from->y_x3, from->y_x2);
	dydx3 = ctc2_slope(from->y_y4, from->y_y3,
			   from->y_x4, from->y_x3);
	dydx4 = ctc2_slope(from->y_y5, from->y_y4,
			   SH_CSS_BAYER_MAXVAL, from->y_x4);

	/*Fill 3 arrays with:
	 * - Luma input gain values y_y0, y_y1, y_y2, y_3, y_y4
	 * - Luma kneepoints 0, y_x1, y_x2, y_x3, y_x4
	 * - Calculated slopes dydx0, dyxd1, dydx2, dydx3, dydx4
	 *
	 * - Each 64-element array is divided in blocks of 16 elements:
	 *   the 5 parameters + zeros in the remaining 11 positions
	 * - All blocks of the same array will contain the same data
	 */
	for (i = 0; i < shffl_blck; i++) {
		to->y_x[0][(i << shffl_blck)]     = 0;
		to->y_x[0][(i << shffl_blck) + 1] = from->y_x1;
		to->y_x[0][(i << shffl_blck) + 2] = from->y_x2;
		to->y_x[0][(i << shffl_blck) + 3] = from->y_x3;
		to->y_x[0][(i << shffl_blck) + 4] = from->y_x4;

		to->y_y[0][(i << shffl_blck)]     = from->y_y0;
		to->y_y[0][(i << shffl_blck) + 1] = from->y_y1;
		to->y_y[0][(i << shffl_blck) + 2] = from->y_y2;
		to->y_y[0][(i << shffl_blck) + 3] = from->y_y3;
		to->y_y[0][(i << shffl_blck) + 4] = from->y_y4;

		to->e_y_slope[0][(i << shffl_blck)]    = dydx0;
		to->e_y_slope[0][(i << shffl_blck) + 1] = dydx1;
		to->e_y_slope[0][(i << shffl_blck) + 2] = dydx2;
		to->e_y_slope[0][(i << shffl_blck) + 3] = dydx3;
		to->e_y_slope[0][(i << shffl_blck) + 4] = dydx4;

		for (j = 0; j < length_zeros; j++) {
			to->y_x[0][(i << shffl_blck) + 5 + j] = 0;
			to->y_y[0][(i << shffl_blck) + 5 + j] = 0;
			to->e_y_slope[0][(i << shffl_blck) + 5 + j] = 0;
		}
	}
}

/* (void) = ia_css_ctc2_encode(*to, *from)
 * -----------------------------------------------
 * DMEM Encode Function to translate UV parameters from userspace into ISP space
 */
void ia_css_ctc2_encode(struct ia_css_isp_ctc2_dmem_params *to,
			struct ia_css_ctc2_config *from,
			size_t size)
{
	(void)size;

	to->uv_y0 = from->uv_y0;
	to->uv_y1 = from->uv_y1;
	to->uv_x0 = from->uv_x0;
	to->uv_x1 = from->uv_x1;

	/*Slope Calculation*/
	to->uv_dydx = ctc2_slope(from->uv_y1, from->uv_y0,
				 from->uv_x1, from->uv_x0);
}
