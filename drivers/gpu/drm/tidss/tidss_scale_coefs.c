// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>

#include "tidss_scale_coefs.h"

/*
 * These are interpolated with a custom python script from DSS5
 * (drivers/gpu/drm/omapdrm/dss/dispc_coef.c) coefficients.
 */
static const struct tidss_scale_coefs coef5_m32 = {
	.c2 = { 28, 34, 40, 46, 52, 58, 64, 70, 0, 2, 4, 8, 12, 16, 20, 24, },
	.c1 = { 132, 138, 144, 150, 156, 162, 168, 174, 76, 84, 92, 98, 104, 110, 116, 124, },
	.c0 = { 192, 192, 192, 190, 188, 186, 184, 182, 180, },
};

static const struct tidss_scale_coefs coef5_m26 = {
	.c2 = { 24, 28, 32, 38, 44, 50, 56, 64, 0, 2, 4, 6, 8, 12, 16, 20, },
	.c1 = { 132, 138, 144, 152, 160, 166, 172, 178, 72, 80, 88, 94, 100, 108, 116, 124, },
	.c0 = { 200, 202, 204, 202, 200, 196, 192, 188, 184, },
};

static const struct tidss_scale_coefs coef5_m22 = {
	.c2 = { 16, 20, 24, 30, 36, 42, 48, 56, 0, 0, 0, 2, 4, 8, 12, 14, },
	.c1 = { 132, 140, 148, 156, 164, 172, 180, 186, 64, 72, 80, 88, 96, 104, 112, 122, },
	.c0 = { 216, 216, 216, 214, 212, 208, 204, 198, 192, },
};

static const struct tidss_scale_coefs coef5_m19 = {
	.c2 = { 12, 14, 16, 22, 28, 34, 40, 48, 0, 0, 0, 2, 4, 4, 4, 8, },
	.c1 = { 128, 140, 152, 160, 168, 176, 184, 192, 56, 64, 72, 82, 92, 100, 108, 118, },
	.c0 = { 232, 232, 232, 226, 220, 218, 216, 208, 200, },
};

static const struct tidss_scale_coefs coef5_m16 = {
	.c2 = { 0, 2, 4, 8, 12, 18, 24, 32, 0, 0, 0, -2, -4, -4, -4, -2, },
	.c1 = { 124, 138, 152, 164, 176, 186, 196, 206, 40, 48, 56, 68, 80, 90, 100, 112, },
	.c0 = { 264, 262, 260, 254, 248, 242, 236, 226, 216, },
};

static const struct tidss_scale_coefs coef5_m14 = {
	.c2 = { -8, -6, -4, -2, 0, 6, 12, 18, 0, -2, -4, -6, -8, -8, -8, -8, },
	.c1 = { 120, 134, 148, 164, 180, 194, 208, 220, 24, 32, 40, 52, 64, 78, 92, 106, },
	.c0 = { 288, 286, 284, 280, 276, 266, 256, 244, 232, },
};

static const struct tidss_scale_coefs coef5_m13 = {
	.c2 = { -12, -12, -12, -10, -8, -4, 0, 6, 0, -2, -4, -6, -8, -10, -12, -12, },
	.c1 = { 112, 130, 148, 164, 180, 196, 212, 228, 12, 22, 32, 44, 56, 70, 84, 98, },
	.c0 = { 312, 308, 304, 298, 292, 282, 272, 258, 244, },
};

static const struct tidss_scale_coefs coef5_m12 = {
	.c2 = { -16, -18, -20, -18, -16, -14, -12, -6, 0, -2, -4, -6, -8, -10, -12, -14, },
	.c1 = { 104, 124, 144, 164, 184, 202, 220, 238, 0, 10, 20, 30, 40, 56, 72, 88, },
	.c0 = { 336, 332, 328, 320, 312, 300, 288, 272, 256, },
};

static const struct tidss_scale_coefs coef5_m11 = {
	.c2 = { -20, -22, -24, -24, -24, -24, -24, -20, 0, -2, -4, -6, -8, -10, -12, -16, },
	.c1 = { 92, 114, 136, 158, 180, 204, 228, 250, -16, -8, 0, 12, 24, 38, 52, 72, },
	.c0 = { 368, 364, 360, 350, 340, 326, 312, 292, 272, },
};

static const struct tidss_scale_coefs coef5_m10 = {
	.c2 = { -16, -20, -24, -28, -32, -34, -36, -34, 0, 0, 0, -2, -4, -8, -12, -14, },
	.c1 = { 72, 96, 120, 148, 176, 204, 232, 260, -32, -26, -20, -10, 0, 16, 32, 52, },
	.c0 = { 400, 398, 396, 384, 372, 354, 336, 312, 288, },
};

static const struct tidss_scale_coefs coef5_m9 = {
	.c2 = { -12, -18, -24, -28, -32, -38, -44, -46, 0, 2, 4, 2, 0, -2, -4, -8, },
	.c1 = { 40, 68, 96, 128, 160, 196, 232, 268, -48, -46, -44, -36, -28, -14, 0, 20, },
	.c0 = { 456, 450, 444, 428, 412, 388, 364, 334, 304, },
};

static const struct tidss_scale_coefs coef5_m8 = {
	.c2 = { 0, -4, -8, -16, -24, -32, -40, -48, 0, 2, 4, 6, 8, 6, 4, 2, },
	.c1 = { 0, 28, 56, 94, 132, 176, 220, 266, -56, -60, -64, -62, -60, -50, -40, -20, },
	.c0 = { 512, 506, 500, 478, 456, 424, 392, 352, 312, },
};

static const struct tidss_scale_coefs coef3_m32 = {
	.c1 = { 108, 92, 76, 62, 48, 36, 24, 140, 256, 236, 216, 198, 180, 162, 144, 126, },
	.c0 = { 296, 294, 292, 288, 284, 278, 272, 136, 256, },
};

static const struct tidss_scale_coefs coef3_m26 = {
	.c1 = { 104, 90, 76, 60, 44, 32, 20, 138, 256, 236, 216, 198, 180, 160, 140, 122, },
	.c0 = { 304, 300, 296, 292, 288, 282, 276, 138, 256, },
};

static const struct tidss_scale_coefs coef3_m22 = {
	.c1 = { 100, 84, 68, 54, 40, 30, 20, 138, 256, 236, 216, 196, 176, 156, 136, 118, },
	.c0 = { 312, 310, 308, 302, 296, 286, 276, 138, 256, },
};

static const struct tidss_scale_coefs coef3_m19 = {
	.c1 = { 96, 80, 64, 50, 36, 26, 16, 136, 256, 236, 216, 194, 172, 152, 132, 114, },
	.c0 = { 320, 318, 316, 310, 304, 292, 280, 140, 256, },
};

static const struct tidss_scale_coefs coef3_m16 = {
	.c1 = { 88, 72, 56, 44, 32, 22, 12, 134, 256, 234, 212, 190, 168, 148, 128, 108, },
	.c0 = { 336, 332, 328, 320, 312, 300, 288, 144, 256, },
};

static const struct tidss_scale_coefs coef3_m14 = {
	.c1 = { 80, 64, 48, 36, 24, 16, 8, 132, 256, 232, 208, 186, 164, 142, 120, 100, },
	.c0 = { 352, 348, 344, 334, 324, 310, 296, 148, 256, },
};

static const struct tidss_scale_coefs coef3_m13 = {
	.c1 = { 72, 56, 40, 30, 20, 12, 4, 130, 256, 232, 208, 184, 160, 136, 112, 92, },
	.c0 = { 368, 364, 360, 346, 332, 316, 300, 150, 256, },
};

static const struct tidss_scale_coefs coef3_m12 = {
	.c1 = { 64, 50, 36, 26, 16, 10, 4, 130, 256, 230, 204, 178, 152, 128, 104, 84, },
	.c0 = { 384, 378, 372, 358, 344, 324, 304, 152, 256, },
};

static const struct tidss_scale_coefs coef3_m11 = {
	.c1 = { 56, 40, 24, 16, 8, 4, 0, 128, 256, 228, 200, 172, 144, 120, 96, 76, },
	.c0 = { 400, 396, 392, 376, 360, 336, 312, 156, 256, },
};

static const struct tidss_scale_coefs coef3_m10 = {
	.c1 = { 40, 26, 12, 6, 0, -2, -4, 126, 256, 226, 196, 166, 136, 110, 84, 62, },
	.c0 = { 432, 424, 416, 396, 376, 348, 320, 160, 256, },
};

static const struct tidss_scale_coefs coef3_m9 = {
	.c1 = { 24, 12, 0, -4, -8, -8, -8, 124, 256, 222, 188, 154, 120, 92, 64, 44, },
	.c0 = { 464, 456, 448, 424, 400, 366, 332, 166, 256, },
};

static const struct tidss_scale_coefs coef3_m8 = {
	.c1 = { 0, -8, -16, -16, -16, -12, -8, 124, 256, 214, 172, 134, 96, 66, 36, 18, },
	.c0 = { 512, 502, 492, 462, 432, 390, 348, 174, 256, },
};

const struct tidss_scale_coefs *tidss_get_scale_coefs(struct device *dev,
						      u32 firinc,
						      bool five_taps)
{
	int i;
	int inc;
	static const struct {
		int mmin;
		int mmax;
		const struct tidss_scale_coefs *coef3;
		const struct tidss_scale_coefs *coef5;
		const char *name;
	} coefs[] = {
		{ 27, 32, &coef3_m32, &coef5_m32, "M32" },
		{ 23, 26, &coef3_m26, &coef5_m26, "M26" },
		{ 20, 22, &coef3_m22, &coef5_m22, "M22" },
		{ 17, 19, &coef3_m19, &coef5_m19, "M19" },
		{ 15, 16, &coef3_m16, &coef5_m16, "M16" },
		{ 14, 14, &coef3_m14, &coef5_m14, "M14" },
		{ 13, 13, &coef3_m13, &coef5_m13, "M13" },
		{ 12, 12, &coef3_m12, &coef5_m12, "M12" },
		{ 11, 11, &coef3_m11, &coef5_m11, "M11" },
		{ 10, 10, &coef3_m10, &coef5_m10, "M10" },
		{  9,  9, &coef3_m9, &coef5_m9, "M9" },
		{  4,  8, &coef3_m8, &coef5_m8, "M8" },
		/*
		 * When upscaling more than two times, blockiness and outlines
		 * around the image are observed when M8 tables are used. M11,
		 * M16 and M19 tables are used to prevent this.
		 */
		{  3,  3, &coef3_m11, &coef5_m11, "M11" },
		{  2,  2, &coef3_m16, &coef5_m16, "M16" },
		{  0,  1, &coef3_m19, &coef5_m19, "M19" },
	};

	/*
	 * inc is result of 0x200000 * in_size / out_size. This dividing
	 * by 0x40000 scales it down to 8 * in_size / out_size. After
	 * division the actual scaling factor is 8/inc.
	 */
	inc = firinc / 0x40000;
	for (i = 0; i < ARRAY_SIZE(coefs); ++i) {
		if (inc >= coefs[i].mmin && inc <= coefs[i].mmax) {
			if (five_taps)
				return coefs[i].coef5;
			else
				return coefs[i].coef3;
		}
	}

	dev_err(dev, "%s: Coefficients not found for firinc 0x%08x, inc %d\n",
		__func__, firinc, inc);

	return NULL;
}
