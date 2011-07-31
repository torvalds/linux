/* Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include "linux/proc_fs.h"

#include <mach/hardware.h>
#include <linux/io.h>

#include <asm/system.h>
#include <asm/mach-types.h>
#include <linux/semaphore.h>
#include <asm/div64.h>

#include "mdp.h"
#include "msm_fb.h"

#define MDP_SCALE_COEFF_NUM      32
#define MDP_SCALE_0P2_TO_0P4_INDEX 0
#define MDP_SCALE_0P4_TO_0P6_INDEX 32
#define MDP_SCALE_0P6_TO_0P8_INDEX 64
#define MDP_SCALE_0P8_TO_8P0_INDEX 96
#define MDP_SCALE_COEFF_MASK 0x3ff

#define MDP_SCALE_PR  0
#define MDP_SCALE_FIR 1

static uint32 mdp_scale_0p8_to_8p0_mode;
static uint32 mdp_scale_0p6_to_0p8_mode;
static uint32 mdp_scale_0p4_to_0p6_mode;
static uint32 mdp_scale_0p2_to_0p4_mode;

/* -------- All scaling range, "pixel repeat" -------- */
static int16 mdp_scale_pixel_repeat_C0[MDP_SCALE_COEFF_NUM] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

static int16 mdp_scale_pixel_repeat_C1[MDP_SCALE_COEFF_NUM] = {
	511, 511, 511, 511, 511, 511, 511, 511,
	511, 511, 511, 511, 511, 511, 511, 511,
	511, 511, 511, 511, 511, 511, 511, 511,
	511, 511, 511, 511, 511, 511, 511, 511
};

static int16 mdp_scale_pixel_repeat_C2[MDP_SCALE_COEFF_NUM] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

static int16 mdp_scale_pixel_repeat_C3[MDP_SCALE_COEFF_NUM] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

/* --------------------------- FIR ------------------------------------- */
/* -------- Downscale, ranging from 0.8x to 8.0x of original size -------- */

static int16 mdp_scale_0p8_to_8p0_C0[MDP_SCALE_COEFF_NUM] = {
	0, -7, -13, -19, -24, -28, -32, -34, -37, -39,
	-40, -41, -41, -41, -40, -40, -38, -37, -35, -33,
	-31, -29, -26, -24, -21, -18, -15, -13, -10, -7,
	-5, -2
};

static int16 mdp_scale_0p8_to_8p0_C1[MDP_SCALE_COEFF_NUM] = {
	511, 507, 501, 494, 485, 475, 463, 450, 436, 422,
	405, 388, 370, 352, 333, 314, 293, 274, 253, 233,
	213, 193, 172, 152, 133, 113, 95, 77, 60, 43,
	28, 13
};

static int16 mdp_scale_0p8_to_8p0_C2[MDP_SCALE_COEFF_NUM] = {
	0, 13, 28, 43, 60, 77, 95, 113, 133, 152,
	172, 193, 213, 233, 253, 274, 294, 314, 333, 352,
	370, 388, 405, 422, 436, 450, 463, 475, 485, 494,
	501, 507,
};

static int16 mdp_scale_0p8_to_8p0_C3[MDP_SCALE_COEFF_NUM] = {
	0, -2, -5, -7, -10, -13, -15, -18, -21, -24,
	-26, -29, -31, -33, -35, -37, -38, -40, -40, -41,
	-41, -41, -40, -39, -37, -34, -32, -28, -24, -19,
	-13, -7
};

/* -------- Downscale, ranging from 0.6x to 0.8x of original size -------- */

static int16 mdp_scale_0p6_to_0p8_C0[MDP_SCALE_COEFF_NUM] = {
	104, 96, 89, 82, 75, 68, 61, 55, 49, 43,
	38, 33, 28, 24, 20, 16, 12, 9, 6, 4,
	2, 0, -2, -4, -5, -6, -7, -7, -8, -8,
	-8, -8
};

static int16 mdp_scale_0p6_to_0p8_C1[MDP_SCALE_COEFF_NUM] = {
	303, 303, 302, 300, 298, 296, 293, 289, 286, 281,
	276, 270, 265, 258, 252, 245, 238, 230, 223, 214,
	206, 197, 189, 180, 172, 163, 154, 145, 137, 128,
	120, 112
};

static int16 mdp_scale_0p6_to_0p8_C2[MDP_SCALE_COEFF_NUM] = {
	112, 120, 128, 137, 145, 154, 163, 172, 180, 189,
	197, 206, 214, 223, 230, 238, 245, 252, 258, 265,
	270, 276, 281, 286, 289, 293, 296, 298, 300, 302,
	303, 303
};

static int16 mdp_scale_0p6_to_0p8_C3[MDP_SCALE_COEFF_NUM] = {
	-8, -8, -8, -8, -7, -7, -6, -5, -4, -2,
	0, 2, 4, 6, 9, 12, 16, 20, 24, 28,
	33, 38, 43, 49, 55, 61, 68, 75, 82, 89,
	96, 104
};

/* -------- Downscale, ranging from 0.4x to 0.6x of original size -------- */

static int16 mdp_scale_0p4_to_0p6_C0[MDP_SCALE_COEFF_NUM] = {
	136, 132, 128, 123, 119, 115, 111, 107, 103, 98,
	95, 91, 87, 84, 80, 76, 73, 69, 66, 62,
	59, 57, 54, 50, 47, 44, 41, 39, 36, 33,
	32, 29
};

static int16 mdp_scale_0p4_to_0p6_C1[MDP_SCALE_COEFF_NUM] = {
	206, 205, 204, 204, 201, 200, 199, 197, 196, 194,
	191, 191, 189, 185, 184, 182, 180, 178, 176, 173,
	170, 168, 165, 162, 160, 157, 155, 152, 148, 146,
	142, 140
};

static int16 mdp_scale_0p4_to_0p6_C2[MDP_SCALE_COEFF_NUM] = {
	140, 142, 146, 148, 152, 155, 157, 160, 162, 165,
	168, 170, 173, 176, 178, 180, 182, 184, 185, 189,
	191, 191, 194, 196, 197, 199, 200, 201, 204, 204,
	205, 206
};

static int16 mdp_scale_0p4_to_0p6_C3[MDP_SCALE_COEFF_NUM] = {
	29, 32, 33, 36, 39, 41, 44, 47, 50, 54,
	57, 59, 62, 66, 69, 73, 76, 80, 84, 87,
	91, 95, 98, 103, 107, 111, 115, 119, 123, 128,
	132, 136
};

/* -------- Downscale, ranging from 0.2x to 0.4x of original size -------- */

static int16 mdp_scale_0p2_to_0p4_C0[MDP_SCALE_COEFF_NUM] = {
	131, 131, 130, 129, 128, 127, 127, 126, 125, 125,
	124, 123, 123, 121, 120, 119, 119, 118, 117, 117,
	116, 115, 115, 114, 113, 112, 111, 110, 109, 109,
	108, 107
};

static int16 mdp_scale_0p2_to_0p4_C1[MDP_SCALE_COEFF_NUM] = {
	141, 140, 140, 140, 140, 139, 138, 138, 138, 137,
	137, 137, 136, 137, 137, 137, 136, 136, 136, 135,
	135, 135, 134, 134, 134, 134, 134, 133, 133, 132,
	132, 132
};

static int16 mdp_scale_0p2_to_0p4_C2[MDP_SCALE_COEFF_NUM] = {
	132, 132, 132, 133, 133, 134, 134, 134, 134, 134,
	135, 135, 135, 136, 136, 136, 137, 137, 137, 136,
	137, 137, 137, 138, 138, 138, 139, 140, 140, 140,
	140, 141
};

static int16 mdp_scale_0p2_to_0p4_C3[MDP_SCALE_COEFF_NUM] = {
	107, 108, 109, 109, 110, 111, 112, 113, 114, 115,
	115, 116, 117, 117, 118, 119, 119, 120, 121, 123,
	123, 124, 125, 125, 126, 127, 127, 128, 129, 130,
	131, 131
};

static void mdp_update_scale_table(int index, int16 *c0, int16 *c1,
				   int16 *c2, int16 *c3)
{
	int i, val;

	for (i = 0; i < MDP_SCALE_COEFF_NUM; i++) {
		val =
		    ((MDP_SCALE_COEFF_MASK & c1[i]) << 16) |
		    (MDP_SCALE_COEFF_MASK & c0[i]);
		MDP_OUTP(MDP_PPP_SCALE_COEFF_LSBn(index), val);
		val =
		    ((MDP_SCALE_COEFF_MASK & c3[i]) << 16) |
		    (MDP_SCALE_COEFF_MASK & c2[i]);
		MDP_OUTP(MDP_PPP_SCALE_COEFF_MSBn(index), val);
		index++;
	}
}

void mdp_init_scale_table(void)
{
	mdp_scale_0p2_to_0p4_mode = MDP_SCALE_FIR;
	mdp_update_scale_table(MDP_SCALE_0P2_TO_0P4_INDEX,
			       mdp_scale_0p2_to_0p4_C0,
			       mdp_scale_0p2_to_0p4_C1,
			       mdp_scale_0p2_to_0p4_C2,
			       mdp_scale_0p2_to_0p4_C3);

	mdp_scale_0p4_to_0p6_mode = MDP_SCALE_FIR;
	mdp_update_scale_table(MDP_SCALE_0P4_TO_0P6_INDEX,
			       mdp_scale_0p4_to_0p6_C0,
			       mdp_scale_0p4_to_0p6_C1,
			       mdp_scale_0p4_to_0p6_C2,
			       mdp_scale_0p4_to_0p6_C3);

	mdp_scale_0p6_to_0p8_mode = MDP_SCALE_FIR;
	mdp_update_scale_table(MDP_SCALE_0P6_TO_0P8_INDEX,
			       mdp_scale_0p6_to_0p8_C0,
			       mdp_scale_0p6_to_0p8_C1,
			       mdp_scale_0p6_to_0p8_C2,
			       mdp_scale_0p6_to_0p8_C3);

	mdp_scale_0p8_to_8p0_mode = MDP_SCALE_FIR;
	mdp_update_scale_table(MDP_SCALE_0P8_TO_8P0_INDEX,
			       mdp_scale_0p8_to_8p0_C0,
			       mdp_scale_0p8_to_8p0_C1,
			       mdp_scale_0p8_to_8p0_C2,
			       mdp_scale_0p8_to_8p0_C3);
}

static long long mdp_do_div(long long num, long long den)
{
	do_div(num, den);
	return num;
}

#define SCALER_PHASE_BITS 29
#define HAL_MDP_PHASE_STEP_2P50    0x50000000
#define HAL_MDP_PHASE_STEP_1P66    0x35555555
#define HAL_MDP_PHASE_STEP_1P25    0x28000000

struct phase_val {
	int phase_init_x;
	int phase_init_y;
	int phase_step_x;
	int phase_step_y;
};

static void mdp_calc_scaleInitPhase_3p1(uint32 in_w,
					uint32 in_h,
					uint32 out_w,
					uint32 out_h,
					boolean is_rotate,
					boolean is_pp_x,
					boolean is_pp_y, struct phase_val *pval)
{
	uint64 dst_ROI_width;
	uint64 dst_ROI_height;
	uint64 src_ROI_width;
	uint64 src_ROI_height;

	/*
	 * phase_step_x, phase_step_y, phase_init_x and phase_init_y
	 * are represented in fixed-point, unsigned 3.29 format
	 */
	uint32 phase_step_x = 0;
	uint32 phase_step_y = 0;
	uint32 phase_init_x = 0;
	uint32 phase_init_y = 0;
	uint32 yscale_filter_sel, xscale_filter_sel;
	uint32 scale_unit_sel_x, scale_unit_sel_y;

	uint64 numerator, denominator;
	uint64 temp_dim;

	src_ROI_width = in_w;
	src_ROI_height = in_h;
	dst_ROI_width = out_w;
	dst_ROI_height = out_h;

	/* if there is a 90 degree rotation */
	if (is_rotate) {
		/* decide whether to use FIR or M/N for scaling */

		/* if down-scaling by a factor smaller than 1/4 */
		if (src_ROI_width > (4 * dst_ROI_height))
			scale_unit_sel_x = 1;	/* use M/N scalar */
		else
			scale_unit_sel_x = 0;	/* use FIR scalar */

		/* if down-scaling by a factor smaller than 1/4 */
		if (src_ROI_height > (4 * dst_ROI_width))
			scale_unit_sel_y = 1;	/* use M/N scalar */
		else
			scale_unit_sel_y = 0;	/* use FIR scalar */
	} else {
		/* decide whether to use FIR or M/N for scaling */

		if (src_ROI_width > (4 * dst_ROI_width))
			scale_unit_sel_x = 1;	/* use M/N scalar */
		else
			scale_unit_sel_x = 0;	/* use FIR scalar */

		if (src_ROI_height > (4 * dst_ROI_height))
			scale_unit_sel_y = 1;	/* use M/N scalar */
		else
			scale_unit_sel_y = 0;	/* use FIR scalar */

	}

	/* if there is a 90 degree rotation */
	if (is_rotate) {
		/* swap the width and height of dst ROI */
		temp_dim = dst_ROI_width;
		dst_ROI_width = dst_ROI_height;
		dst_ROI_height = temp_dim;
	}

	/* calculate phase step for the x direction */

	/* if destination is only 1 pixel wide, the value of phase_step_x
	   is unimportant. Assigning phase_step_x to src ROI width
	   as an arbitrary value. */
	if (dst_ROI_width == 1)
		phase_step_x = (uint32) ((src_ROI_width) << SCALER_PHASE_BITS);

	/* if using FIR scalar */
	else if (scale_unit_sel_x == 0) {

		/* Calculate the quotient ( src_ROI_width - 1 ) / ( dst_ROI_width - 1)
		   with u3.29 precision. Quotient is rounded up to the larger
		   29th decimal point. */
		numerator = (src_ROI_width - 1) << SCALER_PHASE_BITS;
		denominator = (dst_ROI_width - 1);	/* never equals to 0 because of the "( dst_ROI_width == 1 ) case" */
		phase_step_x = (uint32) mdp_do_div((numerator + denominator - 1), denominator);	/* divide and round up to the larger 29th decimal point. */

	}

	/* if M/N scalar */
	else if (scale_unit_sel_x == 1) {
		/* Calculate the quotient ( src_ROI_width ) / ( dst_ROI_width)
		   with u3.29 precision. Quotient is rounded down to the
		   smaller 29th decimal point. */
		numerator = (src_ROI_width) << SCALER_PHASE_BITS;
		denominator = (dst_ROI_width);
		phase_step_x = (uint32) mdp_do_div(numerator, denominator);
	}
	/* calculate phase step for the y direction */

	/* if destination is only 1 pixel wide, the value of
	   phase_step_x is unimportant. Assigning phase_step_x
	   to src ROI width as an arbitrary value. */
	if (dst_ROI_height == 1)
		phase_step_y = (uint32) ((src_ROI_height) << SCALER_PHASE_BITS);

	/* if FIR scalar */
	else if (scale_unit_sel_y == 0) {
		/* Calculate the quotient ( src_ROI_height - 1 ) / ( dst_ROI_height - 1)
		   with u3.29 precision. Quotient is rounded up to the larger
		   29th decimal point. */
		numerator = (src_ROI_height - 1) << SCALER_PHASE_BITS;
		denominator = (dst_ROI_height - 1);	/* never equals to 0 because of the "( dst_ROI_height == 1 )" case */
		phase_step_y = (uint32) mdp_do_div((numerator + denominator - 1), denominator);	/* Quotient is rounded up to the larger 29th decimal point. */

	}

	/* if M/N scalar */
	else if (scale_unit_sel_y == 1) {
		/* Calculate the quotient ( src_ROI_height ) / ( dst_ROI_height)
		   with u3.29 precision. Quotient is rounded down to the smaller
		   29th decimal point. */
		numerator = (src_ROI_height) << SCALER_PHASE_BITS;
		denominator = (dst_ROI_height);
		phase_step_y = (uint32) mdp_do_div(numerator, denominator);
	}

	/* decide which set of FIR coefficients to use */
	if (phase_step_x > HAL_MDP_PHASE_STEP_2P50)
		xscale_filter_sel = 0;
	else if (phase_step_x > HAL_MDP_PHASE_STEP_1P66)
		xscale_filter_sel = 1;
	else if (phase_step_x > HAL_MDP_PHASE_STEP_1P25)
		xscale_filter_sel = 2;
	else
		xscale_filter_sel = 3;

	if (phase_step_y > HAL_MDP_PHASE_STEP_2P50)
		yscale_filter_sel = 0;
	else if (phase_step_y > HAL_MDP_PHASE_STEP_1P66)
		yscale_filter_sel = 1;
	else if (phase_step_y > HAL_MDP_PHASE_STEP_1P25)
		yscale_filter_sel = 2;
	else
		yscale_filter_sel = 3;

	/* calculate phase init for the x direction */

	/* if using FIR scalar */
	if (scale_unit_sel_x == 0) {
		if (dst_ROI_width == 1)
			phase_init_x =
			    (uint32) ((src_ROI_width - 1) << SCALER_PHASE_BITS);
		else
			phase_init_x = 0;

	}
	/* M over N scalar  */
	else if (scale_unit_sel_x == 1)
		phase_init_x = 0;

	/* calculate phase init for the y direction
	   if using FIR scalar */
	if (scale_unit_sel_y == 0) {
		if (dst_ROI_height == 1)
			phase_init_y =
			    (uint32) ((src_ROI_height -
				       1) << SCALER_PHASE_BITS);
		else
			phase_init_y = 0;

	}
	/* M over N scalar   */
	else if (scale_unit_sel_y == 1)
		phase_init_y = 0;

	/* write registers */
	pval->phase_step_x = (uint32) phase_step_x;
	pval->phase_step_y = (uint32) phase_step_y;
	pval->phase_init_x = (uint32) phase_init_x;
	pval->phase_init_y = (uint32) phase_init_y;

	return;
}

void mdp_set_scale(MDPIBUF *iBuf,
		   uint32 dst_roi_width,
		   uint32 dst_roi_height,
		   boolean inputRGB, boolean outputRGB, uint32 *pppop_reg_ptr)
{
	uint32 dst_roi_width_scale;
	uint32 dst_roi_height_scale;
	struct phase_val pval;
	boolean use_pr;
	uint32 ppp_scale_config = 0;

	if (!inputRGB)
		ppp_scale_config |= BIT(6);

	if (iBuf->mdpImg.mdpOp & MDPOP_ASCALE) {
		if (iBuf->mdpImg.mdpOp & MDPOP_ROT90) {
			dst_roi_width_scale = dst_roi_height;
			dst_roi_height_scale = dst_roi_width;
		} else {
			dst_roi_width_scale = dst_roi_width;
			dst_roi_height_scale = dst_roi_height;
		}

		if ((dst_roi_width_scale != iBuf->roi.width) ||
		    (dst_roi_height_scale != iBuf->roi.height) ||
			(iBuf->mdpImg.mdpOp & MDPOP_SHARPENING)) {
			*pppop_reg_ptr |=
			    (PPP_OP_SCALE_Y_ON | PPP_OP_SCALE_X_ON);

			mdp_calc_scaleInitPhase_3p1(iBuf->roi.width,
						    iBuf->roi.height,
						    dst_roi_width,
						    dst_roi_height,
						    iBuf->mdpImg.
						    mdpOp & MDPOP_ROT90, 1, 1,
						    &pval);

			MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x013c,
				 pval.phase_init_x);
			MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x0140,
				 pval.phase_init_y);
			MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x0144,
				 pval.phase_step_x);
			MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x0148,
				 pval.phase_step_y);

			use_pr = (inputRGB) && (outputRGB);

			/* x-direction */
			if ((dst_roi_width_scale == iBuf->roi.width) &&
				!(iBuf->mdpImg.mdpOp & MDPOP_SHARPENING)) {
				*pppop_reg_ptr &= ~PPP_OP_SCALE_X_ON;
			} else
			    if (((dst_roi_width_scale * 10) / iBuf->roi.width) >
				8) {
				if ((use_pr)
				    && (mdp_scale_0p8_to_8p0_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p8_to_8p0_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P8_TO_8P0_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p8_to_8p0_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p8_to_8p0_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P8_TO_8P0_INDEX,
					     mdp_scale_0p8_to_8p0_C0,
					     mdp_scale_0p8_to_8p0_C1,
					     mdp_scale_0p8_to_8p0_C2,
					     mdp_scale_0p8_to_8p0_C3);
				}
				ppp_scale_config |= (SCALE_U1_SET << 2);
			} else
			    if (((dst_roi_width_scale * 10) / iBuf->roi.width) >
				6) {
				if ((use_pr)
				    && (mdp_scale_0p6_to_0p8_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p6_to_0p8_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P6_TO_0P8_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p6_to_0p8_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p6_to_0p8_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P6_TO_0P8_INDEX,
					     mdp_scale_0p6_to_0p8_C0,
					     mdp_scale_0p6_to_0p8_C1,
					     mdp_scale_0p6_to_0p8_C2,
					     mdp_scale_0p6_to_0p8_C3);
				}
				ppp_scale_config |= (SCALE_D2_SET << 2);
			} else
			    if (((dst_roi_width_scale * 10) / iBuf->roi.width) >
				4) {
				if ((use_pr)
				    && (mdp_scale_0p4_to_0p6_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p4_to_0p6_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P4_TO_0P6_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p4_to_0p6_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p4_to_0p6_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P4_TO_0P6_INDEX,
					     mdp_scale_0p4_to_0p6_C0,
					     mdp_scale_0p4_to_0p6_C1,
					     mdp_scale_0p4_to_0p6_C2,
					     mdp_scale_0p4_to_0p6_C3);
				}
				ppp_scale_config |= (SCALE_D1_SET << 2);
			} else
			    if (((dst_roi_width_scale * 4) / iBuf->roi.width) >=
				1) {
				if ((use_pr)
				    && (mdp_scale_0p2_to_0p4_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p2_to_0p4_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P2_TO_0P4_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p2_to_0p4_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p2_to_0p4_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P2_TO_0P4_INDEX,
					     mdp_scale_0p2_to_0p4_C0,
					     mdp_scale_0p2_to_0p4_C1,
					     mdp_scale_0p2_to_0p4_C2,
					     mdp_scale_0p2_to_0p4_C3);
				}
				ppp_scale_config |= (SCALE_D0_SET << 2);
			} else
				ppp_scale_config |= BIT(0);

			/* y-direction */
			if ((dst_roi_height_scale == iBuf->roi.height) &&
				!(iBuf->mdpImg.mdpOp & MDPOP_SHARPENING)) {
				*pppop_reg_ptr &= ~PPP_OP_SCALE_Y_ON;
			} else if (((dst_roi_height_scale * 10) /
					iBuf->roi.height) > 8) {
				if ((use_pr)
				    && (mdp_scale_0p8_to_8p0_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p8_to_8p0_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P8_TO_8P0_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p8_to_8p0_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p8_to_8p0_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P8_TO_8P0_INDEX,
					     mdp_scale_0p8_to_8p0_C0,
					     mdp_scale_0p8_to_8p0_C1,
					     mdp_scale_0p8_to_8p0_C2,
					     mdp_scale_0p8_to_8p0_C3);
				}
				ppp_scale_config |= (SCALE_U1_SET << 4);
			} else
			    if (((dst_roi_height_scale * 10) /
				 iBuf->roi.height) > 6) {
				if ((use_pr)
				    && (mdp_scale_0p6_to_0p8_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p6_to_0p8_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P6_TO_0P8_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p6_to_0p8_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p6_to_0p8_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P6_TO_0P8_INDEX,
					     mdp_scale_0p6_to_0p8_C0,
					     mdp_scale_0p6_to_0p8_C1,
					     mdp_scale_0p6_to_0p8_C2,
					     mdp_scale_0p6_to_0p8_C3);
				}
				ppp_scale_config |= (SCALE_D2_SET << 4);
			} else
			    if (((dst_roi_height_scale * 10) /
				 iBuf->roi.height) > 4) {
				if ((use_pr)
				    && (mdp_scale_0p4_to_0p6_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p4_to_0p6_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P4_TO_0P6_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p4_to_0p6_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p4_to_0p6_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P4_TO_0P6_INDEX,
					     mdp_scale_0p4_to_0p6_C0,
					     mdp_scale_0p4_to_0p6_C1,
					     mdp_scale_0p4_to_0p6_C2,
					     mdp_scale_0p4_to_0p6_C3);
				}
				ppp_scale_config |= (SCALE_D1_SET << 4);
			} else
			    if (((dst_roi_height_scale * 4) /
				 iBuf->roi.height) >= 1) {
				if ((use_pr)
				    && (mdp_scale_0p2_to_0p4_mode !=
					MDP_SCALE_PR)) {
					mdp_scale_0p2_to_0p4_mode =
					    MDP_SCALE_PR;
					mdp_update_scale_table
					    (MDP_SCALE_0P2_TO_0P4_INDEX,
					     mdp_scale_pixel_repeat_C0,
					     mdp_scale_pixel_repeat_C1,
					     mdp_scale_pixel_repeat_C2,
					     mdp_scale_pixel_repeat_C3);
				} else if ((!use_pr)
					   && (mdp_scale_0p2_to_0p4_mode !=
					       MDP_SCALE_FIR)) {
					mdp_scale_0p2_to_0p4_mode =
					    MDP_SCALE_FIR;
					mdp_update_scale_table
					    (MDP_SCALE_0P2_TO_0P4_INDEX,
					     mdp_scale_0p2_to_0p4_C0,
					     mdp_scale_0p2_to_0p4_C1,
					     mdp_scale_0p2_to_0p4_C2,
					     mdp_scale_0p2_to_0p4_C3);
				}
				ppp_scale_config |= (SCALE_D0_SET << 4);
			} else
				ppp_scale_config |= BIT(1);

			if (iBuf->mdpImg.mdpOp & MDPOP_SHARPENING) {
				ppp_scale_config |= BIT(7);
				MDP_OUTP(MDP_BASE + 0x50020,
						iBuf->mdpImg.sp_value);
			}

			MDP_OUTP(MDP_BASE + 0x10230, ppp_scale_config);
		} else {
			iBuf->mdpImg.mdpOp &= ~(MDPOP_ASCALE);
		}
	}
}

void mdp_adjust_start_addr(uint8 **src0,
			   uint8 **src1,
			   int v_slice,
			   int h_slice,
			   int x,
			   int y,
			   uint32 width,
			   uint32 height, int bpp, MDPIBUF *iBuf, int layer)
{
	switch (layer) {
	case 0:
		MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x0200, (y << 16) | (x));
		MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x0208,
			 (height << 16) | (width));
		break;

	case 1:
		/* MDP 3.1 HW bug workaround */
		if (iBuf->ibuf_type == MDP_YCRYCB_H2V1) {
			*src0 += (x + y * width) * bpp;
			x = y = 0;
			width = iBuf->roi.dst_width;
			height = iBuf->roi.dst_height;
		}

		MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x0204, (y << 16) | (x));
		MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x020c,
			 (height << 16) | (width));
		break;

	case 2:
		MDP_OUTP(MDP_CMD_DEBUG_ACCESS_BASE + 0x019c, (y << 16) | (x));
		break;
	}
}

void mdp_set_blend_attr(MDPIBUF *iBuf,
			uint32 *alpha,
			uint32 *tpVal,
			uint32 perPixelAlpha, uint32 *pppop_reg_ptr)
{
	int bg_alpha;

	*alpha = iBuf->mdpImg.alpha;
	*tpVal = iBuf->mdpImg.tpVal;

	if (iBuf->mdpImg.mdpOp & MDPOP_FG_PM_ALPHA) {
		*pppop_reg_ptr |= PPP_OP_ROT_ON |
		    PPP_OP_BLEND_ON | PPP_OP_BLEND_CONSTANT_ALPHA;

		bg_alpha = PPP_BLEND_BG_USE_ALPHA_SEL |
				PPP_BLEND_BG_ALPHA_REVERSE;

		if (perPixelAlpha)
			bg_alpha |= PPP_BLEND_BG_SRCPIXEL_ALPHA;
		else
			bg_alpha |= PPP_BLEND_BG_CONSTANT_ALPHA;

		outpdw(MDP_BASE + 0x70010, bg_alpha);

		if (iBuf->mdpImg.mdpOp & MDPOP_TRANSP)
			*pppop_reg_ptr |= PPP_BLEND_CALPHA_TRNASP;
	} else if (perPixelAlpha) {
		*pppop_reg_ptr |= PPP_OP_ROT_ON |
		    PPP_OP_BLEND_ON | PPP_OP_BLEND_SRCPIXEL_ALPHA;
	} else {
		if ((iBuf->mdpImg.mdpOp & MDPOP_ALPHAB)
		    && (iBuf->mdpImg.alpha == 0xff)) {
			iBuf->mdpImg.mdpOp &= ~(MDPOP_ALPHAB);
		}

		if ((iBuf->mdpImg.mdpOp & MDPOP_ALPHAB)
		    || (iBuf->mdpImg.mdpOp & MDPOP_TRANSP)) {
			*pppop_reg_ptr |=
			    PPP_OP_ROT_ON | PPP_OP_BLEND_ON |
			    PPP_OP_BLEND_CONSTANT_ALPHA |
			    PPP_OP_BLEND_ALPHA_BLEND_NORMAL;
		}

		if (iBuf->mdpImg.mdpOp & MDPOP_TRANSP)
			*pppop_reg_ptr |= PPP_BLEND_CALPHA_TRNASP;
	}
}
