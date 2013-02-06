/* linux/drivers/media/video/samsung/tvout/hw_if/vp.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * Hardware interface functions for video processor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/delay.h>

#include <mach/regs-vp.h>

#include "../s5p_tvout_common_lib.h"
#include "hw_if.h"

#undef tvout_dbg

#ifdef CONFIG_VP_DEBUG
#define tvout_dbg(fmt, ...)					\
		printk(KERN_INFO "\t[VP] %s(): " fmt,		\
			__func__, ##__VA_ARGS__)
#else
#define tvout_dbg(fmt, ...)
#endif

/*
 * Area for definitions to be used in only this file.
 * This area can include #define, enum and struct defintition.
 */
#define H_RATIO(s_w, d_w) (((s_w) << 16) / (d_w))
#define V_RATIO(s_h, d_h, ipc_2d) (((s_h) << ((ipc_2d) ? 17 : 16)) / (d_h))

enum s5p_vp_poly_coeff {
	VP_POLY8_Y0_LL = 0,
	VP_POLY8_Y0_LH,
	VP_POLY8_Y0_HL,
	VP_POLY8_Y0_HH,
	VP_POLY8_Y1_LL,
	VP_POLY8_Y1_LH,
	VP_POLY8_Y1_HL,
	VP_POLY8_Y1_HH,
	VP_POLY8_Y2_LL,
	VP_POLY8_Y2_LH,
	VP_POLY8_Y2_HL,
	VP_POLY8_Y2_HH,
	VP_POLY8_Y3_LL,
	VP_POLY8_Y3_LH,
	VP_POLY8_Y3_HL,
	VP_POLY8_Y3_HH,
	VP_POLY4_Y0_LL = 32,
	VP_POLY4_Y0_LH,
	VP_POLY4_Y0_HL,
	VP_POLY4_Y0_HH,
	VP_POLY4_Y1_LL,
	VP_POLY4_Y1_LH,
	VP_POLY4_Y1_HL,
	VP_POLY4_Y1_HH,
	VP_POLY4_Y2_LL,
	VP_POLY4_Y2_LH,
	VP_POLY4_Y2_HL,
	VP_POLY4_Y2_HH,
	VP_POLY4_Y3_LL,
	VP_POLY4_Y3_LH,
	VP_POLY4_Y3_HL,
	VP_POLY4_Y3_HH,
	VP_POLY4_C0_LL,
	VP_POLY4_C0_LH,
	VP_POLY4_C0_HL,
	VP_POLY4_C0_HH,
	VP_POLY4_C1_LL,
	VP_POLY4_C1_LH,
	VP_POLY4_C1_HL,
	VP_POLY4_C1_HH
};

enum s5p_vp_filter_h_pp {
	VP_PP_H_NORMAL,
	VP_PP_H_8_9,
	VP_PP_H_1_2,
	VP_PP_H_1_3,
	VP_PP_H_1_4
};

enum s5p_vp_filter_v_pp {
	VP_PP_V_NORMAL,
	VP_PP_V_5_6,
	VP_PP_V_3_4,
	VP_PP_V_1_2,
	VP_PP_V_1_3,
	VP_PP_V_1_4
};

/*
 * Area for global variables to be used in only this file.
 */

static void __iomem *vp_base;

/*  Horizontal Y 8tap  */
const signed char g_s_vp8tap_coef_y_h[] = {
	/* VP_PP_H_NORMAL */
	0,	0,	0,	0,	127,	0,	0,	0,
	0,	1,	-2,	8,	126,	-6,	2,	-1,
	0,	1,	-5,	16,	125,	-12,	4,	-1,
	0,	2,	-8,	25,	121,	-16,	5,	-1,
	-1,	3,	-10,	35,	114,	-18,	6,	-1,
	-1,	4,	-13,	46,	107,	-20,	6,	-1,
	-1,	5,	-16,	57,	99,	-21,	6,	-1,
	-1,	5,	-18,	68,	89,	-20,	6,	-1,
	-1,	6,	-20,	79,	79,	-20,	6,	-1,
	-1,	6,	-20,	89,	68,	-18,	5,	-1,
	-1,	6,	-21,	99,	57,	-16,	5,	-1,
	-1,	6,	-20,	107,	46,	-13,	4,	-1,
	-1,	6,	-18,	114,	35,	-10,	3,	-1,
	-1,	5,	-16,	121,	25,	-8,	2,	0,
	-1,	4,	-12,	125,	16,	-5,	1,	0,
	-1,	2,	-6,	126,	8,	-2,	1,	0,

	/* VP_PP_H_8_9 */
	0,	3,	-7,	12,	112,	12,	-7,	3,
	-1,	3,	-9,	19,	113,	6,	-5,	2,
	-1,	3,	-11,	27,	111,	0,	-3,	2,
	-1,	4,	-13,	35,	108,	-5,	-1,	1,
	-1,	4,	-14,	43,	104,	-9,	0,	1,
	-1,	5,	-16,	52,	99,	-12,	1,	0,
	-1,	5,	-17,	61,	92,	-14,	2,	0,
	0,	4,	-17,	69,	85,	-16,	3,	0,
	0,	4,	-17,	77,	77,	-17,	4,	0,
	0,	3,	-16,	85,	69,	-17,	4,	0,
	0,	2,	-14,	92,	61,	-17,	5,	-1,
	0,	1,	-12,	99,	52,	-16,	5,	-1,
	1,	0,	-9,	104,	43,	-14,	4,	-1,
	1,	-1,	-5,	108,	35,	-13,	4,	-1,
	2,	-3,	0,	111,	27,	-11,	3,	-1,
	2,	-5,	6,	113,	19,	-9,	3,	-1,

	/* VP_PP_H_1_2 */
	0,	-3,	0,	35,	64,	35,	0,	-3,
	0,	-3,	1,	38,	64,	32,	-1,	-3,
	0,	-3,	2,	41,	63,	29,	-2,	-2,
	0,	-4,	4,	43,	63,	27,	-3,	-2,
	0,	-4,	5,	46,	62,	24,	-3,	-2,
	0,	-4,	7,	49,	60,	21,	-3,	-2,
	-1,	-4,	9,	51,	59,	19,	-4,	-1,
	-1,	-4,	12,	53,	57,	16,	-4,	-1,
	-1,	-4,	14,	55,	55,	14,	-4,	-1,
	-1,	-4,	16,	57,	53,	12,	-4,	-1,
	-1,	-4,	19,	59,	51,	9,	-4,	-1,
	-2,	-3,	21,	60,	49,	7,	-4,	0,
	-2,	-3,	24,	62,	46,	5,	-4,	0,
	-2,	-3,	27,	63,	43,	4,	-4,	0,
	-2,	-2,	29,	63,	41,	2,	-3,	0,
	-3,	-1,	32,	64,	38,	1,	-3,	0,

	/* VP_PP_H_1_3 */
	0,	0,	10,	32,	44,	32,	10,	0,
	-1,	0,	11,	33,	45,	31,	9,	0,
	-1,	0,	12,	35,	45,	29,	8,	0,
	-1,	1,	13,	36,	44,	28,	7,	0,
	-1,	1,	15,	37,	44,	26,	6,	0,
	-1,	2,	16,	38,	43,	25,	5,	0,
	-1,	2,	18,	39,	43,	23,	5,	-1,
	-1,	3,	19,	40,	42,	22,	4,	-1,
	-1,	3,	21,	41,	41,	21,	3,	-1,
	-1,	4,	22,	42,	40,	19,	3,	-1,
	-1,	5,	23,	43,	39,	18,	2,	-1,
	0,	5,	25,	43,	38,	16,	2,	-1,
	0,	6,	26,	44,	37,	15,	1,	-1,
	0,	7,	28,	44,	36,	13,	1,	-1,
	0,	8,	29,	45,	35,	12,	0,	-1,
	0,	9,	31,	45,	33,	11,	0,	-1,

	/* VP_PP_H_1_4 */
	0,	2,	13,	30,	38,	30,	13,	2,
	0,	3,	14,	30,	38,	29,	12,	2,
	0,	3,	15,	31,	38,	28,	11,	2,
	0,	4,	16,	32,	38,	27,	10,	1,
	0,	4,	17,	33,	37,	26,	10,	1,
	0,	5,	18,	34,	37,	24,	9,	1,
	0,	5,	19,	34,	37,	24,	8,	1,
	1,	6,	20,	35,	36,	22,	7,	1,
	1,	6,	21,	36,	36,	21,	6,	1,
	1,	7,	22,	36,	35,	20,	6,	1,
	1,	8,	24,	37,	34,	19,	5,	0,
	1,	9,	24,	37,	34,	18,	5,	0,
	1,	10,	26,	37,	33,	17,	4,	0,
	1,	10,	27,	38,	32,	16,	4,	0,
	2,	11,	28,	38,	31,	15,	3,	0,
	2,	12,	29,	38,	30,	14,	3,	0
};

/* Horizontal C 4tap */
const signed char g_s_vp4tap_coef_c_h[] = {
	/* VP_PP_H_NORMAL */
	0,	0,	128,	0,	0,	5,	126,	-3,
	-1,	11,	124,	-6,	-1,	19,	118,	-8,
	-2,	27,	111,	-8,	-3,	37,	102,	-8,
	-4,	48,	92,	-8,	-5,	59,	81,	-7,
	-6,	70,	70,	-6,	-7,	81,	59,	-5,
	-8,	92,	48,	-4,	-8,	102,	37,	-3,
	-8,	111,	27,	-2,	-8,	118,	19,	-1,
	-6,	124,	11,	-1,	-3,	126,	5,	0,

	/* VP_PP_H_8_9 */
	0,	8,	112,	8,	-1,	13,	113,	3,
	-2,	19,	111,	0,	-2,	26,	107,	-3,
	-3,	34,	101,	-4,	-3,	42,	94,	-5,
	-4,	51,	86,	-5,	-5,	60,	78,	-5,
	-5,	69,	69,	-5,	-5,	78,	60,	-5,
	-5,	86,	51,	-4,	-5,	94,	42,	-3,
	-4,	101,	34,	-3,	-3,	107,	26,	-2,
	0,	111,	19,	-2,	3,	113,	13,	-1,

	/* VP_PP_H_1_2 */
	0,	26,	76,	26,	0,	30,	76,	22,
	0,	34,	75,	19,	1,	38,	73,	16,
	1,	43,	71,	13,	2,	47,	69,	10,
	3,	51,	66,	8,	4,	55,	63,	6,
	5,	59,	59,	5,	6,	63,	55,	4,
	8,	66,	51,	3,	10,	69,	47,	2,
	13,	71,	43,	1,	16,	73,	38,	1,
	19,	75,	34,	0,	22,	76,	30,	0,

	/* VP_PP_H_1_3 */
	0,	30,	68,	30,	2,	33,	66,	27,
	3,	36,	66,	23,	3,	39,	65,	21,
	4,	43,	63,	18,	5,	46,	62,	15,
	6,	49,	60,	13,	8,	52,	57,	11,
	9,	55,	55,	9,	11,	57,	52,	8,
	13,	60,	49,	6,	15,	62,	46,	5,
	18,	63,	43,	4,	21,	65,	39,	3,
	23,	66,	36,	3,	27,	66,	33,	2,

	/*  VP_PP_H_1_4 */
	0,	31,	66,	31,	3,	34,	63,	28,
	4,	37,	62,	25,	4,	40,	62,	22,
	5,	43,	61,	19,	6,	46,	59,	17,
	7,	48,	58,	15,	9,	51,	55,	13,
	11,	53,	53,	11,	13,	55,	51,	9,
	15,	58,	48,	7,	17,	59,	46,	6,
	19,	61,	43,	5,	22,	62,	40,	4,
	25,	62,	37,	4,	28,	63,	34,	3,
};


/*  Vertical Y 8tap  */
const signed char g_s_vp4tap_coef_y_v[] = {
	/* VP_PP_V_NORMAL  */
	0,	0,	127,	0,	0,	5,	126,	-3,
	-1,	11,	124,	-6,	-1,	19,	118,	-8,
	-2,	27,	111,	-8,	-3,	37,	102,	-8,
	-4,	48,	92,	-8,	-5,	59,	81,	-7,
	-6,	70,	70,	-6,	-7,	81,	59,	-5,
	-8,	92,	48,	-4,	-8,	102,	37,	-3,
	-8,	111,	27,	-2,	-8,	118,	19,	-1,
	-6,	124,	11,	-1,	-3,	126,	5,	0,

	/* VP_PP_V_5_6  */
	0,	11,	106,	11,	-2,	16,	107,	7,
	-2,	22,	105,	3,	-2,	29,	101,	0,
	-3,	36,	96,	-1,	-3,	44,	90,	-3,
	-4,	52,	84,	-4,	-4,	60,	76,	-4,
	-4,	68,	68,	-4,	-4,	76,	60,	-4,
	-4,	84,	52,	-4,	-3,	90,	44,	-3,
	-1,	96,	36,	-3,	0,	101,	29,	-2,
	3,	105,	22,	-2,	7,	107,	16,	-2,

	/* VP_PP_V_3_4  */
	0,	15,	98,	15,	-2,	21,	97,	12,
	-2,	26,	96,	8,	-2,	32,	93,	5,
	-2,	39,	89,	2,	-2,	46,	84,	0,
	-3,	53,	79,	-1,	-2,	59,	73,	-2,
	-2,	66,	66,	-2,	-2,	73,	59,	-2,
	-1,	79,	53,	-3,	0,	84,	46,	-2,
	2,	89,	39,	-2,	5,	93,	32,	-2,
	8,	96,	26,	-2,	12,	97,	21,	-2,

	/* VP_PP_V_1_2  */
	0,	26,	76,	26,	0,	30,	76,	22,
	0,	34,	75,	19,	1,	38,	73,	16,
	1,	43,	71,	13,	2,	47,	69,	10,
	3,	51,	66,	8,	4,	55,	63,	6,
	5,	59,	59,	5,	6,	63,	55,	4,
	8,	66,	51,	3,	10,	69,	47,	2,
	13,	71,	43,	1,	16,	73,	38,	1,
	19,	75,	34,	0,	22,	76,	30,	0,

	/* VP_PP_V_1_3  */
	0,	30,	68,	30,	2,	33,	66,	27,
	3,	36,	66,	23,	3,	39,	65,	21,
	4,	43,	63,	18,	5,	46,	62,	15,
	6,	49,	60,	13,	8,	52,	57,	11,
	9,	55,	55,	9,	11,	57,	52,	8,
	13,	60,	49,	6,	15,	62,	46,	5,
	18,	63,	43,	4,	21,	65,	39,	3,
	23,	66,	36,	3,	27,	66,	33,	2,

	/* VP_PP_V_1_4  */
	0,	31,	66,	31,	3,	34,	63,	28,
	4,	37,	62,	25,	4,	40,	62,	22,
	5,	43,	61,	19,	6,	46,	59,	17,
	7,	48,	58,	15,	9,	51,	55,	13,
	11,	53,	53,	11,	13,	55,	51,	9,
	15,	58,	48,	7,	17,	59,	46,	6,
	19,	61,	43,	5,	22,	62,	40,	4,
	25,	62,	37,	4,	28,	63,	34,	3
};

/*
 * Area for functions to be used in only this file.
 * Functions of this area are defined by static
 */
static int s5p_vp_set_poly_filter_coef(
		enum s5p_vp_poly_coeff poly_coeff,
		signed char ch0, signed char ch1,
		signed char ch2, signed char ch3)
{
	if (poly_coeff > VP_POLY4_C1_HH || poly_coeff < VP_POLY8_Y0_LL ||
	   (poly_coeff > VP_POLY8_Y3_HH && poly_coeff < VP_POLY4_Y0_LL)) {
		tvout_err("invaild poly_coeff parameter\n");

		return -1;
	}

	writel((((0xff & ch0) << 24) | ((0xff & ch1) << 16) |
		((0xff & ch2) << 8) | (0xff & ch3)),
			vp_base + S5P_VP_POLY8_Y0_LL + poly_coeff * 4);

	return 0;
}

/*
 * Area for functions to be used by other files.
 * Functions of this area must be defined in header file.
 */
void s5p_vp_set_poly_filter_coef_default(
		u32 src_width, u32 src_height,
		u32 dst_width, u32 dst_height, bool ipc_2d)
{
	enum s5p_vp_filter_h_pp e_h_filter;
	enum s5p_vp_filter_v_pp e_v_filter;
	u8 *poly_flt_coeff;
	int i, j;

	u32 h_ratio = H_RATIO(src_width, dst_width);
	u32 v_ratio = V_RATIO(src_height, dst_height, ipc_2d);

	/*
	* For the real interlace mode, the vertical ratio should be
	* used after divided by 2. Because in the interlace mode, all
	* the VP output is used for SDOUT display and it should be the
	* same as one field of the progressive mode. Therefore the same
	* filter coefficients should be used for the same the final
	* output video. When half of the interlace V_RATIO is same as
	* the progressive V_RATIO, the final output video scale is same.
	*/

	if (h_ratio <= (0x1 << 16))		/* 720->720 or zoom in */
		e_h_filter = VP_PP_H_NORMAL;
	else if (h_ratio <= (0x9 << 13))	/* 720->640 */
		e_h_filter = VP_PP_H_8_9;
	else if (h_ratio <= (0x1 << 17))	/* 2->1 */
		e_h_filter = VP_PP_H_1_2;
	else if (h_ratio <= (0x3 << 16))	/* 2->1 */
		e_h_filter = VP_PP_H_1_3;
	else
		e_h_filter = VP_PP_H_1_4;	/* 4->1 */

	/* Vertical Y 4tap */

	if (v_ratio <= (0x1 << 16))		/* 720->720 or zoom in*/
		e_v_filter = VP_PP_V_NORMAL;
	else if (v_ratio <= (0x5 << 14))	/* 4->3*/
		e_v_filter = VP_PP_V_3_4;
	else if (v_ratio <= (0x3 << 15))	/*6->5*/
		e_v_filter = VP_PP_V_5_6;
	else if (v_ratio <= (0x1 << 17))	/* 2->1*/
		e_v_filter = VP_PP_V_1_2;
	else if (v_ratio <= (0x3 << 16))	/* 3->1*/
		e_v_filter = VP_PP_V_1_3;
	else
		e_v_filter = VP_PP_V_1_4;

	poly_flt_coeff = (u8 *)(g_s_vp8tap_coef_y_h + e_h_filter * 16 * 8);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			s5p_vp_set_poly_filter_coef(
				VP_POLY8_Y0_LL + (i*4) + j,
				*(poly_flt_coeff + 4*j*8 + (7 - i)),
				*(poly_flt_coeff + (4*j + 1)*8 + (7 - i)),
				*(poly_flt_coeff + (4*j + 2)*8 + (7 - i)),
				*(poly_flt_coeff + (4*j + 3)*8 + (7 - i)));
		}
	}

	poly_flt_coeff = (u8 *)(g_s_vp4tap_coef_c_h + e_h_filter * 16 * 4);

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 4; j++) {
			s5p_vp_set_poly_filter_coef(
				VP_POLY4_C0_LL + (i*4) + j,
				*(poly_flt_coeff + 4*j*4 + (3 - i)),
				*(poly_flt_coeff + (4*j + 1)*4 + (3 - i)),
				*(poly_flt_coeff + (4*j + 2)*4 + (3 - i)),
				*(poly_flt_coeff + (4*j + 3)*4 + (3 - i)));
		}
	}

	poly_flt_coeff = (u8 *)(g_s_vp4tap_coef_y_v + e_v_filter * 16 * 4);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			s5p_vp_set_poly_filter_coef(
				VP_POLY4_Y0_LL + (i*4) + j,
				*(poly_flt_coeff + 4*j*4 + (3 - i)),
				*(poly_flt_coeff + (4*j + 1)*4 + (3 - i)),
				*(poly_flt_coeff + (4*j + 2)*4 + (3 - i)),
				*(poly_flt_coeff + (4*j + 3)*4 + (3 - i)));
		}
	}
}

void s5p_vp_set_field_id(enum s5p_vp_field mode)
{
	writel((mode == VP_TOP_FIELD) ? VP_TOP_FIELD : VP_BOTTOM_FIELD,
		vp_base + S5P_VP_FIELD_ID);
}

int s5p_vp_set_top_field_address(u32 top_y_addr, u32 top_c_addr)
{
	if (S5P_VP_PTR_ILLEGAL(top_y_addr) || S5P_VP_PTR_ILLEGAL(top_c_addr)) {
		tvout_err("address is not double word align = 0x%x, 0x%x\n",
			top_y_addr, top_c_addr);

		return -1;
	}

	writel(top_y_addr, vp_base + S5P_VP_TOP_Y_PTR);
	writel(top_c_addr, vp_base + S5P_VP_TOP_C_PTR);

	return 0;
}

int s5p_vp_get_top_field_address(u32* top_y_addr, u32* top_c_addr)
{
	*top_y_addr = readl(vp_base + S5P_VP_TOP_Y_PTR);
	*top_c_addr = readl(vp_base + S5P_VP_TOP_C_PTR);

	return 0;
}

int s5p_vp_set_bottom_field_address(
		u32 bottom_y_addr, u32 bottom_c_addr)
{
	if (S5P_VP_PTR_ILLEGAL(bottom_y_addr) ||
			S5P_VP_PTR_ILLEGAL(bottom_c_addr)) {
		tvout_err("address is not double word align = 0x%x, 0x%x\n",
			bottom_y_addr, bottom_c_addr);

		return -1;
	}

	writel(bottom_y_addr, vp_base + S5P_VP_BOT_Y_PTR);
	writel(bottom_c_addr, vp_base + S5P_VP_BOT_C_PTR);

	return 0;
}

int s5p_vp_set_img_size(u32 img_width, u32 img_height)
{
	if (S5P_VP_IMG_SIZE_ILLEGAL(img_width) ||
			S5P_VP_IMG_SIZE_ILLEGAL(img_height)) {
		tvout_err("full image size is not double word align ="
			"%d, %d\n", img_width, img_height);

		return -1;
	}

	writel(S5P_VP_IMG_HSIZE(img_width) | S5P_VP_IMG_VSIZE(img_height),
		vp_base + S5P_VP_IMG_SIZE_Y);
	writel(S5P_VP_IMG_HSIZE(img_width) | S5P_VP_IMG_VSIZE(img_height / 2),
		vp_base + S5P_VP_IMG_SIZE_C);

	return 0;
}

void s5p_vp_set_src_position(
		u32 src_off_x, u32 src_x_fract_step, u32 src_off_y)
{
	writel(S5P_VP_SRC_H_POSITION_VAL(src_off_x) |
		S5P_VP_SRC_X_FRACT_STEP(src_x_fract_step),
			vp_base + S5P_VP_SRC_H_POSITION);
	writel(S5P_VP_SRC_V_POSITION_VAL(src_off_y),
			vp_base + S5P_VP_SRC_V_POSITION);
}

void s5p_vp_set_dest_position(u32 dst_off_x, u32 dst_off_y)
{
	writel(S5P_VP_DST_H_POSITION_VAL(dst_off_x),
			vp_base + S5P_VP_DST_H_POSITION);
	writel(S5P_VP_DST_V_POSITION_VAL(dst_off_y),
			vp_base + S5P_VP_DST_V_POSITION);
}

void s5p_vp_set_src_dest_size(
		u32 src_width, u32 src_height,
		u32 dst_width, u32 dst_height, bool ipc_2d)
{
	u32 h_ratio = H_RATIO(src_width, dst_width);
	u32 v_ratio = V_RATIO(src_height, dst_height, ipc_2d);

	writel(S5P_VP_SRC_WIDTH_VAL(src_width), vp_base + S5P_VP_SRC_WIDTH);
	writel(S5P_VP_SRC_HEIGHT_VAL(src_height), vp_base + S5P_VP_SRC_HEIGHT);
	writel(S5P_VP_DST_WIDTH_VAL(dst_width), vp_base + S5P_VP_DST_WIDTH);
	writel(S5P_VP_DST_HEIGHT_VAL(dst_height), vp_base + S5P_VP_DST_HEIGHT);
	writel(S5P_VP_H_RATIO_VAL(h_ratio), vp_base + S5P_VP_H_RATIO);
	writel(S5P_VP_V_RATIO_VAL(v_ratio), vp_base + S5P_VP_V_RATIO);

	writel((ipc_2d) ?
		(readl(vp_base + S5P_VP_MODE) | S5P_VP_MODE_2D_IPC_ENABLE) :
		(readl(vp_base + S5P_VP_MODE) & ~S5P_VP_MODE_2D_IPC_ENABLE),
		vp_base + S5P_VP_MODE);
}

void s5p_vp_set_op_mode(
		bool line_skip,
		enum s5p_vp_mem_type mem_type,
		enum s5p_vp_mem_mode mem_mode,
		enum s5p_vp_chroma_expansion chroma_exp,
		bool auto_toggling)
{
	u32 temp_reg;

	temp_reg = (mem_type) ?
		S5P_VP_MODE_IMG_TYPE_YUV420_NV21 :
		S5P_VP_MODE_IMG_TYPE_YUV420_NV12;
	temp_reg |= (line_skip) ?
		S5P_VP_MODE_LINE_SKIP_ON : S5P_VP_MODE_LINE_SKIP_OFF;
	temp_reg |= (mem_mode == VP_2D_TILE_MODE) ?
		S5P_VP_MODE_MEM_MODE_2D_TILE :
		S5P_VP_MODE_MEM_MODE_LINEAR;
	temp_reg |= (chroma_exp == VP_C_TOP_BOTTOM) ?
		S5P_VP_MODE_CROMA_EXP_C_TOPBOTTOM_PTR :
		S5P_VP_MODE_CROMA_EXP_C_TOP_PTR;
	temp_reg |= (auto_toggling) ?
		S5P_VP_MODE_FIELD_ID_AUTO_TOGGLING :
		S5P_VP_MODE_FIELD_ID_MAN_TOGGLING;

	writel(temp_reg, vp_base + S5P_VP_MODE);
}

void s5p_vp_set_pixel_rate_control(enum s5p_vp_pxl_rate rate)
{
	writel(S5P_VP_PEL_RATE_CTRL(rate), vp_base + S5P_VP_PER_RATE_CTRL);
}

void s5p_vp_set_endian(enum s5p_tvout_endian endian)
{
	writel(endian, vp_base + S5P_VP_ENDIAN_MODE);
}

void s5p_vp_set_bypass_post_process(bool bypass)
{
	writel((bypass) ? S5P_VP_BY_PASS_ENABLE : S5P_VP_BY_PASS_DISABLE,
			vp_base + S5P_PP_BYPASS);
}

void s5p_vp_set_saturation(u32 sat)
{
	writel(S5P_VP_SATURATION(sat), vp_base + S5P_PP_SATURATION);
}

void s5p_vp_set_sharpness(
		u32 th_h_noise,	enum s5p_vp_sharpness_control sharpness)
{
	writel(S5P_VP_TH_HNOISE(th_h_noise) | S5P_VP_SHARPNESS(sharpness),
			vp_base + S5P_PP_SHARPNESS);
}

void s5p_vp_set_brightness_contrast(u16 b, u8 c)
{
	int i;

	for (i = 0; i < 8; i++)
		writel(S5P_VP_LINE_INTC(b) | S5P_VP_LINE_SLOPE(c),
			vp_base + S5P_PP_LINE_EQ0 + i*4);
}

void s5p_vp_set_brightness_offset(u32 offset)
{
	writel(S5P_VP_BRIGHT_OFFSET(offset), vp_base + S5P_PP_BRIGHT_OFFSET);
}

int s5p_vp_set_brightness_contrast_control(
		enum s5p_vp_line_eq eq_num, u32 intc, u32 slope)
{
	if (eq_num > VP_LINE_EQ_7 || eq_num < VP_LINE_EQ_0) {
		tvout_err("invaild eq_num parameter\n");

		return -1;
	}

	writel(S5P_VP_LINE_INTC(intc) | S5P_VP_LINE_SLOPE(slope),
	       vp_base + S5P_PP_LINE_EQ0 + eq_num*4);

	return 0;
}

void s5p_vp_set_csc_control(bool sub_y_offset_en, bool csc_en)
{
	u32 temp_reg;

	temp_reg = (sub_y_offset_en) ? S5P_VP_SUB_Y_OFFSET_ENABLE :
					S5P_VP_SUB_Y_OFFSET_DISABLE;
	temp_reg |= (csc_en) ? S5P_VP_CSC_ENABLE : S5P_VP_CSC_DISABLE;

	writel(temp_reg, vp_base + S5P_PP_CSC_EN);
}

int s5p_vp_set_csc_coef(enum s5p_vp_csc_coeff csc_coeff, u32 coeff)
{
	if (csc_coeff > VP_CSC_CR2CR_COEF ||
			csc_coeff < VP_CSC_Y2Y_COEF) {
		tvout_err("invaild csc_coeff parameter\n");

		return -1;
	}

	writel(S5P_PP_CSC_COEF(coeff),
			vp_base + S5P_PP_CSC_Y2Y_COEF + csc_coeff*4);

	return 0;
}

int s5p_vp_set_csc_coef_default(enum s5p_vp_csc_type csc_type)
{
	switch (csc_type) {
	case VP_CSC_SD_HD:
		writel(S5P_PP_Y2Y_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_Y2Y_COEF);
		writel(S5P_PP_CB2Y_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_CB2Y_COEF);
		writel(S5P_PP_CR2Y_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_CR2Y_COEF);
		writel(S5P_PP_Y2CB_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_Y2CB_COEF);
		writel(S5P_PP_CB2CB_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_CB2CB_COEF);
		writel(S5P_PP_CR2CB_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_CR2CB_COEF);
		writel(S5P_PP_Y2CR_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_Y2CR_COEF);
		writel(S5P_PP_CB2CR_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_CB2CR_COEF);
		writel(S5P_PP_CR2CR_COEF_601_TO_709,
				vp_base + S5P_PP_CSC_CR2CR_COEF);
		break;

	case VP_CSC_HD_SD:
		writel(S5P_PP_Y2Y_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_Y2Y_COEF);
		writel(S5P_PP_CB2Y_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_CB2Y_COEF);
		writel(S5P_PP_CR2Y_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_CR2Y_COEF);
		writel(S5P_PP_Y2CB_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_Y2CB_COEF);
		writel(S5P_PP_CB2CB_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_CB2CB_COEF);
		writel(S5P_PP_CR2CB_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_CR2CB_COEF);
		writel(S5P_PP_Y2CR_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_Y2CR_COEF);
		writel(S5P_PP_CB2CR_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_CB2CR_COEF);
		writel(S5P_PP_CR2CR_COEF_709_TO_601,
				vp_base + S5P_PP_CSC_CR2CR_COEF);
		break;

	default:
		tvout_err("invalid csc_type parameter = %d\n", csc_type);
		return -1;
	}

	return 0;
}

int s5p_vp_update(void)
{
	writel(readl(vp_base + S5P_VP_SHADOW_UPDATE) |
		S5P_VP_SHADOW_UPDATE_ENABLE,
			vp_base + S5P_VP_SHADOW_UPDATE);

	return 0;
}

int s5p_vp_get_update_status(void)
{
	if (readl(vp_base + S5P_VP_SHADOW_UPDATE) & S5P_VP_SHADOW_UPDATE_ENABLE)
		return 0;
	else
		return -1;
}

void s5p_vp_sw_reset(void)
{
	writel((readl(vp_base + S5P_VP_SRESET) | S5P_VP_SRESET_PROCESSING),
		vp_base + S5P_VP_SRESET);

	while (readl(vp_base + S5P_VP_SRESET) & S5P_VP_SRESET_PROCESSING)
		msleep(10);
}

int s5p_vp_start(void)
{
	writel(S5P_VP_ENABLE_ON, vp_base + S5P_VP_ENABLE);

	s5p_vp_update();

	return 0;
}

int s5p_vp_stop(void)
{
	u32 val;

	val = readl(vp_base + S5P_VP_ENABLE);
	val &= ~S5P_VP_ENABLE_ON;
	writel(val, vp_base + S5P_VP_ENABLE);

	do {
		val = readl(vp_base + S5P_VP_ENABLE);
	} while (!(val & S5P_VP_ENABLE_OPERATING));

	return 0;
}

void s5p_vp_init(void __iomem *addr)
{
	vp_base = addr;
}
