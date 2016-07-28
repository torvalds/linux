/*
 * Copyright © 2006-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

/** @file
 * Integrated TV-out support for the 915GM and 945GM.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include "intel_drv.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

enum tv_margin {
	TV_MARGIN_LEFT, TV_MARGIN_TOP,
	TV_MARGIN_RIGHT, TV_MARGIN_BOTTOM
};

/** Private structure for the integrated TV support */
struct intel_tv {
	struct intel_encoder base;

	int type;
	const char *tv_format;
	int margin[4];
	u32 save_TV_H_CTL_1;
	u32 save_TV_H_CTL_2;
	u32 save_TV_H_CTL_3;
	u32 save_TV_V_CTL_1;
	u32 save_TV_V_CTL_2;
	u32 save_TV_V_CTL_3;
	u32 save_TV_V_CTL_4;
	u32 save_TV_V_CTL_5;
	u32 save_TV_V_CTL_6;
	u32 save_TV_V_CTL_7;
	u32 save_TV_SC_CTL_1, save_TV_SC_CTL_2, save_TV_SC_CTL_3;

	u32 save_TV_CSC_Y;
	u32 save_TV_CSC_Y2;
	u32 save_TV_CSC_U;
	u32 save_TV_CSC_U2;
	u32 save_TV_CSC_V;
	u32 save_TV_CSC_V2;
	u32 save_TV_CLR_KNOBS;
	u32 save_TV_CLR_LEVEL;
	u32 save_TV_WIN_POS;
	u32 save_TV_WIN_SIZE;
	u32 save_TV_FILTER_CTL_1;
	u32 save_TV_FILTER_CTL_2;
	u32 save_TV_FILTER_CTL_3;

	u32 save_TV_H_LUMA[60];
	u32 save_TV_H_CHROMA[60];
	u32 save_TV_V_LUMA[43];
	u32 save_TV_V_CHROMA[43];

	u32 save_TV_DAC;
	u32 save_TV_CTL;
};

struct video_levels {
	int blank, black, burst;
};

struct color_conversion {
	u16 ry, gy, by, ay;
	u16 ru, gu, bu, au;
	u16 rv, gv, bv, av;
};

static const u32 filter_table[] = {
	0xB1403000, 0x2E203500, 0x35002E20, 0x3000B140,
	0x35A0B160, 0x2DC02E80, 0xB1403480, 0xB1603000,
	0x2EA03640, 0x34002D80, 0x3000B120, 0x36E0B160,
	0x2D202EF0, 0xB1203380, 0xB1603000, 0x2F303780,
	0x33002CC0, 0x3000B100, 0x3820B160, 0x2C802F50,
	0xB10032A0, 0xB1603000, 0x2F9038C0, 0x32202C20,
	0x3000B0E0, 0x3980B160, 0x2BC02FC0, 0xB0E031C0,
	0xB1603000, 0x2FF03A20, 0x31602B60, 0xB020B0C0,
	0x3AE0B160, 0x2B001810, 0xB0C03120, 0xB140B020,
	0x18283BA0, 0x30C02A80, 0xB020B0A0, 0x3C60B140,
	0x2A201838, 0xB0A03080, 0xB120B020, 0x18383D20,
	0x304029C0, 0xB040B080, 0x3DE0B100, 0x29601848,
	0xB0803000, 0xB100B040, 0x18483EC0, 0xB0402900,
	0xB040B060, 0x3F80B0C0, 0x28801858, 0xB060B080,
	0xB0A0B060, 0x18602820, 0xB0A02820, 0x0000B060,
	0xB1403000, 0x2E203500, 0x35002E20, 0x3000B140,
	0x35A0B160, 0x2DC02E80, 0xB1403480, 0xB1603000,
	0x2EA03640, 0x34002D80, 0x3000B120, 0x36E0B160,
	0x2D202EF0, 0xB1203380, 0xB1603000, 0x2F303780,
	0x33002CC0, 0x3000B100, 0x3820B160, 0x2C802F50,
	0xB10032A0, 0xB1603000, 0x2F9038C0, 0x32202C20,
	0x3000B0E0, 0x3980B160, 0x2BC02FC0, 0xB0E031C0,
	0xB1603000, 0x2FF03A20, 0x31602B60, 0xB020B0C0,
	0x3AE0B160, 0x2B001810, 0xB0C03120, 0xB140B020,
	0x18283BA0, 0x30C02A80, 0xB020B0A0, 0x3C60B140,
	0x2A201838, 0xB0A03080, 0xB120B020, 0x18383D20,
	0x304029C0, 0xB040B080, 0x3DE0B100, 0x29601848,
	0xB0803000, 0xB100B040, 0x18483EC0, 0xB0402900,
	0xB040B060, 0x3F80B0C0, 0x28801858, 0xB060B080,
	0xB0A0B060, 0x18602820, 0xB0A02820, 0x0000B060,
	0x36403000, 0x2D002CC0, 0x30003640, 0x2D0036C0,
	0x35C02CC0, 0x37403000, 0x2C802D40, 0x30003540,
	0x2D8037C0, 0x34C02C40, 0x38403000, 0x2BC02E00,
	0x30003440, 0x2E2038C0, 0x34002B80, 0x39803000,
	0x2B402E40, 0x30003380, 0x2E603A00, 0x33402B00,
	0x3A803040, 0x2A802EA0, 0x30403300, 0x2EC03B40,
	0x32802A40, 0x3C003040, 0x2A002EC0, 0x30803240,
	0x2EC03C80, 0x320029C0, 0x3D403080, 0x29402F00,
	0x308031C0, 0x2F203DC0, 0x31802900, 0x3E8030C0,
	0x28802F40, 0x30C03140, 0x2F203F40, 0x31402840,
	0x28003100, 0x28002F00, 0x00003100, 0x36403000,
	0x2D002CC0, 0x30003640, 0x2D0036C0,
	0x35C02CC0, 0x37403000, 0x2C802D40, 0x30003540,
	0x2D8037C0, 0x34C02C40, 0x38403000, 0x2BC02E00,
	0x30003440, 0x2E2038C0, 0x34002B80, 0x39803000,
	0x2B402E40, 0x30003380, 0x2E603A00, 0x33402B00,
	0x3A803040, 0x2A802EA0, 0x30403300, 0x2EC03B40,
	0x32802A40, 0x3C003040, 0x2A002EC0, 0x30803240,
	0x2EC03C80, 0x320029C0, 0x3D403080, 0x29402F00,
	0x308031C0, 0x2F203DC0, 0x31802900, 0x3E8030C0,
	0x28802F40, 0x30C03140, 0x2F203F40, 0x31402840,
	0x28003100, 0x28002F00, 0x00003100,
};

/*
 * Color conversion values have 3 separate fixed point formats:
 *
 * 10 bit fields (ay, au)
 *   1.9 fixed point (b.bbbbbbbbb)
 * 11 bit fields (ry, by, ru, gu, gv)
 *   exp.mantissa (ee.mmmmmmmmm)
 *   ee = 00 = 10^-1 (0.mmmmmmmmm)
 *   ee = 01 = 10^-2 (0.0mmmmmmmmm)
 *   ee = 10 = 10^-3 (0.00mmmmmmmmm)
 *   ee = 11 = 10^-4 (0.000mmmmmmmmm)
 * 12 bit fields (gy, rv, bu)
 *   exp.mantissa (eee.mmmmmmmmm)
 *   eee = 000 = 10^-1 (0.mmmmmmmmm)
 *   eee = 001 = 10^-2 (0.0mmmmmmmmm)
 *   eee = 010 = 10^-3 (0.00mmmmmmmmm)
 *   eee = 011 = 10^-4 (0.000mmmmmmmmm)
 *   eee = 100 = reserved
 *   eee = 101 = reserved
 *   eee = 110 = reserved
 *   eee = 111 = 10^0 (m.mmmmmmmm) (only usable for 1.0 representation)
 *
 * Saturation and contrast are 8 bits, with their own representation:
 * 8 bit field (saturation, contrast)
 *   exp.mantissa (ee.mmmmmm)
 *   ee = 00 = 10^-1 (0.mmmmmm)
 *   ee = 01 = 10^0 (m.mmmmm)
 *   ee = 10 = 10^1 (mm.mmmm)
 *   ee = 11 = 10^2 (mmm.mmm)
 *
 * Simple conversion function:
 *
 * static u32
 * float_to_csc_11(float f)
 * {
 *     u32 exp;
 *     u32 mant;
 *     u32 ret;
 *
 *     if (f < 0)
 *         f = -f;
 *
 *     if (f >= 1) {
 *         exp = 0x7;
 *	   mant = 1 << 8;
 *     } else {
 *         for (exp = 0; exp < 3 && f < 0.5; exp++)
 *	   f *= 2.0;
 *         mant = (f * (1 << 9) + 0.5);
 *         if (mant >= (1 << 9))
 *             mant = (1 << 9) - 1;
 *     }
 *     ret = (exp << 9) | mant;
 *     return ret;
 * }
 */

/*
 * Behold, magic numbers!  If we plant them they might grow a big
 * s-video cable to the sky... or something.
 *
 * Pre-converted to appropriate hex value.
 */

/*
 * PAL & NTSC values for composite & s-video connections
 */
static const struct color_conversion ntsc_m_csc_composite = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0104,
	.ru = 0x0733, .gu = 0x052d, .bu = 0x05c7, .au = 0x0200,
	.rv = 0x0340, .gv = 0x030c, .bv = 0x06d0, .av = 0x0200,
};

static const struct video_levels ntsc_m_levels_composite = {
	.blank = 225, .black = 267, .burst = 113,
};

static const struct color_conversion ntsc_m_csc_svideo = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0133,
	.ru = 0x076a, .gu = 0x0564, .bu = 0x030d, .au = 0x0200,
	.rv = 0x037a, .gv = 0x033d, .bv = 0x06f6, .av = 0x0200,
};

static const struct video_levels ntsc_m_levels_svideo = {
	.blank = 266, .black = 316, .burst = 133,
};

static const struct color_conversion ntsc_j_csc_composite = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0119,
	.ru = 0x074c, .gu = 0x0546, .bu = 0x05ec, .au = 0x0200,
	.rv = 0x035a, .gv = 0x0322, .bv = 0x06e1, .av = 0x0200,
};

static const struct video_levels ntsc_j_levels_composite = {
	.blank = 225, .black = 225, .burst = 113,
};

static const struct color_conversion ntsc_j_csc_svideo = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x014c,
	.ru = 0x0788, .gu = 0x0581, .bu = 0x0322, .au = 0x0200,
	.rv = 0x0399, .gv = 0x0356, .bv = 0x070a, .av = 0x0200,
};

static const struct video_levels ntsc_j_levels_svideo = {
	.blank = 266, .black = 266, .burst = 133,
};

static const struct color_conversion pal_csc_composite = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0113,
	.ru = 0x0745, .gu = 0x053f, .bu = 0x05e1, .au = 0x0200,
	.rv = 0x0353, .gv = 0x031c, .bv = 0x06dc, .av = 0x0200,
};

static const struct video_levels pal_levels_composite = {
	.blank = 237, .black = 237, .burst = 118,
};

static const struct color_conversion pal_csc_svideo = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0145,
	.ru = 0x0780, .gu = 0x0579, .bu = 0x031c, .au = 0x0200,
	.rv = 0x0390, .gv = 0x034f, .bv = 0x0705, .av = 0x0200,
};

static const struct video_levels pal_levels_svideo = {
	.blank = 280, .black = 280, .burst = 139,
};

static const struct color_conversion pal_m_csc_composite = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0104,
	.ru = 0x0733, .gu = 0x052d, .bu = 0x05c7, .au = 0x0200,
	.rv = 0x0340, .gv = 0x030c, .bv = 0x06d0, .av = 0x0200,
};

static const struct video_levels pal_m_levels_composite = {
	.blank = 225, .black = 267, .burst = 113,
};

static const struct color_conversion pal_m_csc_svideo = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0133,
	.ru = 0x076a, .gu = 0x0564, .bu = 0x030d, .au = 0x0200,
	.rv = 0x037a, .gv = 0x033d, .bv = 0x06f6, .av = 0x0200,
};

static const struct video_levels pal_m_levels_svideo = {
	.blank = 266, .black = 316, .burst = 133,
};

static const struct color_conversion pal_n_csc_composite = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0104,
	.ru = 0x0733, .gu = 0x052d, .bu = 0x05c7, .au = 0x0200,
	.rv = 0x0340, .gv = 0x030c, .bv = 0x06d0, .av = 0x0200,
};

static const struct video_levels pal_n_levels_composite = {
	.blank = 225, .black = 267, .burst = 118,
};

static const struct color_conversion pal_n_csc_svideo = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0133,
	.ru = 0x076a, .gu = 0x0564, .bu = 0x030d, .au = 0x0200,
	.rv = 0x037a, .gv = 0x033d, .bv = 0x06f6, .av = 0x0200,
};

static const struct video_levels pal_n_levels_svideo = {
	.blank = 266, .black = 316, .burst = 139,
};

/*
 * Component connections
 */
static const struct color_conversion sdtv_csc_yprpb = {
	.ry = 0x0332, .gy = 0x012d, .by = 0x07d3, .ay = 0x0145,
	.ru = 0x0559, .gu = 0x0353, .bu = 0x0100, .au = 0x0200,
	.rv = 0x0100, .gv = 0x03ad, .bv = 0x074d, .av = 0x0200,
};

static const struct color_conversion hdtv_csc_yprpb = {
	.ry = 0x05b3, .gy = 0x016e, .by = 0x0728, .ay = 0x0145,
	.ru = 0x07d5, .gu = 0x038b, .bu = 0x0100, .au = 0x0200,
	.rv = 0x0100, .gv = 0x03d1, .bv = 0x06bc, .av = 0x0200,
};

static const struct video_levels component_levels = {
	.blank = 279, .black = 279, .burst = 0,
};


struct tv_mode {
	const char *name;
	int clock;
	int refresh; /* in millihertz (for precision) */
	u32 oversample;
	int hsync_end, hblank_start, hblank_end, htotal;
	bool progressive, trilevel_sync, component_only;
	int vsync_start_f1, vsync_start_f2, vsync_len;
	bool veq_ena;
	int veq_start_f1, veq_start_f2, veq_len;
	int vi_end_f1, vi_end_f2, nbr_end;
	bool burst_ena;
	int hburst_start, hburst_len;
	int vburst_start_f1, vburst_end_f1;
	int vburst_start_f2, vburst_end_f2;
	int vburst_start_f3, vburst_end_f3;
	int vburst_start_f4, vburst_end_f4;
	/*
	 * subcarrier programming
	 */
	int dda2_size, dda3_size, dda1_inc, dda2_inc, dda3_inc;
	u32 sc_reset;
	bool pal_burst;
	/*
	 * blank/black levels
	 */
	const struct video_levels *composite_levels, *svideo_levels;
	const struct color_conversion *composite_color, *svideo_color;
	const u32 *filter_table;
	int max_srcw;
};


/*
 * Sub carrier DDA
 *
 *  I think this works as follows:
 *
 *  subcarrier freq = pixel_clock * (dda1_inc + dda2_inc / dda2_size) / 4096
 *
 * Presumably, when dda3 is added in, it gets to adjust the dda2_inc value
 *
 * So,
 *  dda1_ideal = subcarrier/pixel * 4096
 *  dda1_inc = floor (dda1_ideal)
 *  dda2 = dda1_ideal - dda1_inc
 *
 *  then pick a ratio for dda2 that gives the closest approximation. If
 *  you can't get close enough, you can play with dda3 as well. This
 *  seems likely to happen when dda2 is small as the jumps would be larger
 *
 * To invert this,
 *
 *  pixel_clock = subcarrier * 4096 / (dda1_inc + dda2_inc / dda2_size)
 *
 * The constants below were all computed using a 107.520MHz clock
 */

/**
 * Register programming values for TV modes.
 *
 * These values account for -1s required.
 */

static const struct tv_mode tv_modes[] = {
	{
		.name		= "NTSC-M",
		.clock		= 108000,
		.refresh	= 59940,
		.oversample	= TV_OVERSAMPLE_8X,
		.component_only = 0,
		/* 525 Lines, 60 Fields, 15.734KHz line, Sub-Carrier 3.580MHz */

		.hsync_end	= 64,		    .hblank_end		= 124,
		.hblank_start	= 836,		    .htotal		= 857,

		.progressive	= false,	    .trilevel_sync = false,

		.vsync_start_f1	= 6,		    .vsync_start_f2	= 7,
		.vsync_len	= 6,

		.veq_ena	= true,		    .veq_start_f1	= 0,
		.veq_start_f2	= 1,		    .veq_len		= 18,

		.vi_end_f1	= 20,		    .vi_end_f2		= 21,
		.nbr_end	= 240,

		.burst_ena	= true,
		.hburst_start	= 72,		    .hburst_len		= 34,
		.vburst_start_f1 = 9,		    .vburst_end_f1	= 240,
		.vburst_start_f2 = 10,		    .vburst_end_f2	= 240,
		.vburst_start_f3 = 9,		    .vburst_end_f3	= 240,
		.vburst_start_f4 = 10,		    .vburst_end_f4	= 240,

		/* desired 3.5800000 actual 3.5800000 clock 107.52 */
		.dda1_inc	=    135,
		.dda2_inc	=  20800,	    .dda2_size		=  27456,
		.dda3_inc	=      0,	    .dda3_size		=      0,
		.sc_reset	= TV_SC_RESET_EVERY_4,
		.pal_burst	= false,

		.composite_levels = &ntsc_m_levels_composite,
		.composite_color = &ntsc_m_csc_composite,
		.svideo_levels  = &ntsc_m_levels_svideo,
		.svideo_color = &ntsc_m_csc_svideo,

		.filter_table = filter_table,
	},
	{
		.name		= "NTSC-443",
		.clock		= 108000,
		.refresh	= 59940,
		.oversample	= TV_OVERSAMPLE_8X,
		.component_only = 0,
		/* 525 Lines, 60 Fields, 15.734KHz line, Sub-Carrier 4.43MHz */
		.hsync_end	= 64,		    .hblank_end		= 124,
		.hblank_start	= 836,		    .htotal		= 857,

		.progressive	= false,	    .trilevel_sync = false,

		.vsync_start_f1 = 6,		    .vsync_start_f2	= 7,
		.vsync_len	= 6,

		.veq_ena	= true,		    .veq_start_f1	= 0,
		.veq_start_f2	= 1,		    .veq_len		= 18,

		.vi_end_f1	= 20,		    .vi_end_f2		= 21,
		.nbr_end	= 240,

		.burst_ena	= true,
		.hburst_start	= 72,		    .hburst_len		= 34,
		.vburst_start_f1 = 9,		    .vburst_end_f1	= 240,
		.vburst_start_f2 = 10,		    .vburst_end_f2	= 240,
		.vburst_start_f3 = 9,		    .vburst_end_f3	= 240,
		.vburst_start_f4 = 10,		    .vburst_end_f4	= 240,

		/* desired 4.4336180 actual 4.4336180 clock 107.52 */
		.dda1_inc       =    168,
		.dda2_inc       =   4093,       .dda2_size      =  27456,
		.dda3_inc       =    310,       .dda3_size      =    525,
		.sc_reset   = TV_SC_RESET_NEVER,
		.pal_burst  = false,

		.composite_levels = &ntsc_m_levels_composite,
		.composite_color = &ntsc_m_csc_composite,
		.svideo_levels  = &ntsc_m_levels_svideo,
		.svideo_color = &ntsc_m_csc_svideo,

		.filter_table = filter_table,
	},
	{
		.name		= "NTSC-J",
		.clock		= 108000,
		.refresh	= 59940,
		.oversample	= TV_OVERSAMPLE_8X,
		.component_only = 0,

		/* 525 Lines, 60 Fields, 15.734KHz line, Sub-Carrier 3.580MHz */
		.hsync_end	= 64,		    .hblank_end		= 124,
		.hblank_start = 836,	    .htotal		= 857,

		.progressive	= false,    .trilevel_sync = false,

		.vsync_start_f1	= 6,	    .vsync_start_f2	= 7,
		.vsync_len	= 6,

		.veq_ena      = true,	    .veq_start_f1	= 0,
		.veq_start_f2 = 1,	    .veq_len		= 18,

		.vi_end_f1	= 20,		    .vi_end_f2		= 21,
		.nbr_end	= 240,

		.burst_ena	= true,
		.hburst_start	= 72,		    .hburst_len		= 34,
		.vburst_start_f1 = 9,		    .vburst_end_f1	= 240,
		.vburst_start_f2 = 10,		    .vburst_end_f2	= 240,
		.vburst_start_f3 = 9,		    .vburst_end_f3	= 240,
		.vburst_start_f4 = 10,		    .vburst_end_f4	= 240,

		/* desired 3.5800000 actual 3.5800000 clock 107.52 */
		.dda1_inc	=    135,
		.dda2_inc	=  20800,	    .dda2_size		=  27456,
		.dda3_inc	=      0,	    .dda3_size		=      0,
		.sc_reset	= TV_SC_RESET_EVERY_4,
		.pal_burst	= false,

		.composite_levels = &ntsc_j_levels_composite,
		.composite_color = &ntsc_j_csc_composite,
		.svideo_levels  = &ntsc_j_levels_svideo,
		.svideo_color = &ntsc_j_csc_svideo,

		.filter_table = filter_table,
	},
	{
		.name		= "PAL-M",
		.clock		= 108000,
		.refresh	= 59940,
		.oversample	= TV_OVERSAMPLE_8X,
		.component_only = 0,

		/* 525 Lines, 60 Fields, 15.734KHz line, Sub-Carrier 3.580MHz */
		.hsync_end	= 64,		  .hblank_end		= 124,
		.hblank_start = 836,	  .htotal		= 857,

		.progressive	= false,	    .trilevel_sync = false,

		.vsync_start_f1	= 6,		    .vsync_start_f2	= 7,
		.vsync_len	= 6,

		.veq_ena	= true,		    .veq_start_f1	= 0,
		.veq_start_f2	= 1,		    .veq_len		= 18,

		.vi_end_f1	= 20,		    .vi_end_f2		= 21,
		.nbr_end	= 240,

		.burst_ena	= true,
		.hburst_start	= 72,		    .hburst_len		= 34,
		.vburst_start_f1 = 9,		    .vburst_end_f1	= 240,
		.vburst_start_f2 = 10,		    .vburst_end_f2	= 240,
		.vburst_start_f3 = 9,		    .vburst_end_f3	= 240,
		.vburst_start_f4 = 10,		    .vburst_end_f4	= 240,

		/* desired 3.5800000 actual 3.5800000 clock 107.52 */
		.dda1_inc	=    135,
		.dda2_inc	=  16704,	    .dda2_size		=  27456,
		.dda3_inc	=      0,	    .dda3_size		=      0,
		.sc_reset	= TV_SC_RESET_EVERY_8,
		.pal_burst  = true,

		.composite_levels = &pal_m_levels_composite,
		.composite_color = &pal_m_csc_composite,
		.svideo_levels  = &pal_m_levels_svideo,
		.svideo_color = &pal_m_csc_svideo,

		.filter_table = filter_table,
	},
	{
		/* 625 Lines, 50 Fields, 15.625KHz line, Sub-Carrier 4.434MHz */
		.name	    = "PAL-N",
		.clock		= 108000,
		.refresh	= 50000,
		.oversample	= TV_OVERSAMPLE_8X,
		.component_only = 0,

		.hsync_end	= 64,		    .hblank_end		= 128,
		.hblank_start = 844,	    .htotal		= 863,

		.progressive  = false,    .trilevel_sync = false,


		.vsync_start_f1	= 6,	   .vsync_start_f2	= 7,
		.vsync_len	= 6,

		.veq_ena	= true,		    .veq_start_f1	= 0,
		.veq_start_f2	= 1,		    .veq_len		= 18,

		.vi_end_f1	= 24,		    .vi_end_f2		= 25,
		.nbr_end	= 286,

		.burst_ena	= true,
		.hburst_start = 73,	    .hburst_len		= 34,
		.vburst_start_f1 = 8,	    .vburst_end_f1	= 285,
		.vburst_start_f2 = 8,	    .vburst_end_f2	= 286,
		.vburst_start_f3 = 9,	    .vburst_end_f3	= 286,
		.vburst_start_f4 = 9,	    .vburst_end_f4	= 285,


		/* desired 4.4336180 actual 4.4336180 clock 107.52 */
		.dda1_inc       =    135,
		.dda2_inc       =  23578,       .dda2_size      =  27648,
		.dda3_inc       =    134,       .dda3_size      =    625,
		.sc_reset   = TV_SC_RESET_EVERY_8,
		.pal_burst  = true,

		.composite_levels = &pal_n_levels_composite,
		.composite_color = &pal_n_csc_composite,
		.svideo_levels  = &pal_n_levels_svideo,
		.svideo_color = &pal_n_csc_svideo,

		.filter_table = filter_table,
	},
	{
		/* 625 Lines, 50 Fields, 15.625KHz line, Sub-Carrier 4.434MHz */
		.name	    = "PAL",
		.clock		= 108000,
		.refresh	= 50000,
		.oversample	= TV_OVERSAMPLE_8X,
		.component_only = 0,

		.hsync_end	= 64,		    .hblank_end		= 142,
		.hblank_start	= 844,	    .htotal		= 863,

		.progressive	= false,    .trilevel_sync = false,

		.vsync_start_f1	= 5,	    .vsync_start_f2	= 6,
		.vsync_len	= 5,

		.veq_ena	= true,	    .veq_start_f1	= 0,
		.veq_start_f2	= 1,	    .veq_len		= 15,

		.vi_end_f1	= 24,		    .vi_end_f2		= 25,
		.nbr_end	= 286,

		.burst_ena	= true,
		.hburst_start	= 73,		    .hburst_len		= 32,
		.vburst_start_f1 = 8,		    .vburst_end_f1	= 285,
		.vburst_start_f2 = 8,		    .vburst_end_f2	= 286,
		.vburst_start_f3 = 9,		    .vburst_end_f3	= 286,
		.vburst_start_f4 = 9,		    .vburst_end_f4	= 285,

		/* desired 4.4336180 actual 4.4336180 clock 107.52 */
		.dda1_inc       =    168,
		.dda2_inc       =   4122,       .dda2_size      =  27648,
		.dda3_inc       =     67,       .dda3_size      =    625,
		.sc_reset   = TV_SC_RESET_EVERY_8,
		.pal_burst  = true,

		.composite_levels = &pal_levels_composite,
		.composite_color = &pal_csc_composite,
		.svideo_levels  = &pal_levels_svideo,
		.svideo_color = &pal_csc_svideo,

		.filter_table = filter_table,
	},
	{
		.name       = "480p",
		.clock		= 107520,
		.refresh	= 59940,
		.oversample     = TV_OVERSAMPLE_4X,
		.component_only = 1,

		.hsync_end      = 64,               .hblank_end         = 122,
		.hblank_start   = 842,              .htotal             = 857,

		.progressive    = true,		    .trilevel_sync = false,

		.vsync_start_f1 = 12,               .vsync_start_f2     = 12,
		.vsync_len      = 12,

		.veq_ena        = false,

		.vi_end_f1      = 44,               .vi_end_f2          = 44,
		.nbr_end        = 479,

		.burst_ena      = false,

		.filter_table = filter_table,
	},
	{
		.name       = "576p",
		.clock		= 107520,
		.refresh	= 50000,
		.oversample     = TV_OVERSAMPLE_4X,
		.component_only = 1,

		.hsync_end      = 64,               .hblank_end         = 139,
		.hblank_start   = 859,              .htotal             = 863,

		.progressive    = true,		    .trilevel_sync = false,

		.vsync_start_f1 = 10,               .vsync_start_f2     = 10,
		.vsync_len      = 10,

		.veq_ena        = false,

		.vi_end_f1      = 48,               .vi_end_f2          = 48,
		.nbr_end        = 575,

		.burst_ena      = false,

		.filter_table = filter_table,
	},
	{
		.name       = "720p@60Hz",
		.clock		= 148800,
		.refresh	= 60000,
		.oversample     = TV_OVERSAMPLE_2X,
		.component_only = 1,

		.hsync_end      = 80,               .hblank_end         = 300,
		.hblank_start   = 1580,             .htotal             = 1649,

		.progressive	= true,		    .trilevel_sync = true,

		.vsync_start_f1 = 10,               .vsync_start_f2     = 10,
		.vsync_len      = 10,

		.veq_ena        = false,

		.vi_end_f1      = 29,               .vi_end_f2          = 29,
		.nbr_end        = 719,

		.burst_ena      = false,

		.filter_table = filter_table,
	},
	{
		.name       = "720p@50Hz",
		.clock		= 148800,
		.refresh	= 50000,
		.oversample     = TV_OVERSAMPLE_2X,
		.component_only = 1,

		.hsync_end      = 80,               .hblank_end         = 300,
		.hblank_start   = 1580,             .htotal             = 1979,

		.progressive	= true,		    .trilevel_sync = true,

		.vsync_start_f1 = 10,               .vsync_start_f2     = 10,
		.vsync_len      = 10,

		.veq_ena        = false,

		.vi_end_f1      = 29,               .vi_end_f2          = 29,
		.nbr_end        = 719,

		.burst_ena      = false,

		.filter_table = filter_table,
		.max_srcw = 800
	},
	{
		.name       = "1080i@50Hz",
		.clock		= 148800,
		.refresh	= 50000,
		.oversample     = TV_OVERSAMPLE_2X,
		.component_only = 1,

		.hsync_end      = 88,               .hblank_end         = 235,
		.hblank_start   = 2155,             .htotal             = 2639,

		.progressive	= false,	  .trilevel_sync = true,

		.vsync_start_f1 = 4,              .vsync_start_f2     = 5,
		.vsync_len      = 10,

		.veq_ena	= true,	    .veq_start_f1	= 4,
		.veq_start_f2   = 4,	    .veq_len		= 10,


		.vi_end_f1      = 21,           .vi_end_f2          = 22,
		.nbr_end        = 539,

		.burst_ena      = false,

		.filter_table = filter_table,
	},
	{
		.name       = "1080i@60Hz",
		.clock		= 148800,
		.refresh	= 60000,
		.oversample     = TV_OVERSAMPLE_2X,
		.component_only = 1,

		.hsync_end      = 88,               .hblank_end         = 235,
		.hblank_start   = 2155,             .htotal             = 2199,

		.progressive	= false,	    .trilevel_sync = true,

		.vsync_start_f1 = 4,               .vsync_start_f2     = 5,
		.vsync_len      = 10,

		.veq_ena	= true,		    .veq_start_f1	= 4,
		.veq_start_f2	= 4,		    .veq_len		= 10,


		.vi_end_f1      = 21,               .vi_end_f2          = 22,
		.nbr_end        = 539,

		.burst_ena      = false,

		.filter_table = filter_table,
	},
};

static struct intel_tv *enc_to_tv(struct intel_encoder *encoder)
{
	return container_of(encoder, struct intel_tv, base);
}

static struct intel_tv *intel_attached_tv(struct drm_connector *connector)
{
	return enc_to_tv(intel_attached_encoder(connector));
}

static bool
intel_tv_get_hw_state(struct intel_encoder *encoder, enum pipe *pipe)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tmp = I915_READ(TV_CTL);

	if (!(tmp & TV_ENC_ENABLE))
		return false;

	*pipe = PORT_TO_PIPE(tmp);

	return true;
}

static void
intel_enable_tv(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Prevents vblank waits from timing out in intel_tv_detect_type() */
	intel_wait_for_vblank(encoder->base.dev,
			      to_intel_crtc(encoder->base.crtc)->pipe);

	I915_WRITE(TV_CTL, I915_READ(TV_CTL) | TV_ENC_ENABLE);
}

static void
intel_disable_tv(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE(TV_CTL, I915_READ(TV_CTL) & ~TV_ENC_ENABLE);
}

static const struct tv_mode *
intel_tv_mode_lookup(const char *tv_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tv_modes); i++) {
		const struct tv_mode *tv_mode = &tv_modes[i];

		if (!strcmp(tv_format, tv_mode->name))
			return tv_mode;
	}
	return NULL;
}

static const struct tv_mode *
intel_tv_mode_find(struct intel_tv *intel_tv)
{
	return intel_tv_mode_lookup(intel_tv->tv_format);
}

static enum drm_mode_status
intel_tv_mode_valid(struct drm_connector *connector,
		    struct drm_display_mode *mode)
{
	struct intel_tv *intel_tv = intel_attached_tv(connector);
	const struct tv_mode *tv_mode = intel_tv_mode_find(intel_tv);
	int max_dotclk = to_i915(connector->dev)->max_dotclk_freq;

	if (mode->clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	/* Ensure TV refresh is close to desired refresh */
	if (tv_mode && abs(tv_mode->refresh - drm_mode_vrefresh(mode) * 1000)
				< 1000)
		return MODE_OK;

	return MODE_CLOCK_RANGE;
}


static void
intel_tv_get_config(struct intel_encoder *encoder,
		    struct intel_crtc_state *pipe_config)
{
	pipe_config->base.adjusted_mode.crtc_clock = pipe_config->port_clock;
}

static bool
intel_tv_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_state *pipe_config)
{
	struct intel_tv *intel_tv = enc_to_tv(encoder);
	const struct tv_mode *tv_mode = intel_tv_mode_find(intel_tv);

	if (!tv_mode)
		return false;

	pipe_config->base.adjusted_mode.crtc_clock = tv_mode->clock;
	DRM_DEBUG_KMS("forcing bpc to 8 for TV\n");
	pipe_config->pipe_bpp = 8*3;

	/* TV has it's own notion of sync and other mode flags, so clear them. */
	pipe_config->base.adjusted_mode.flags = 0;

	/*
	 * FIXME: We don't check whether the input mode is actually what we want
	 * or whether userspace is doing something stupid.
	 */

	return true;
}

static void
set_tv_mode_timings(struct drm_i915_private *dev_priv,
		    const struct tv_mode *tv_mode,
		    bool burst_ena)
{
	u32 hctl1, hctl2, hctl3;
	u32 vctl1, vctl2, vctl3, vctl4, vctl5, vctl6, vctl7;

	hctl1 = (tv_mode->hsync_end << TV_HSYNC_END_SHIFT) |
		(tv_mode->htotal << TV_HTOTAL_SHIFT);

	hctl2 = (tv_mode->hburst_start << 16) |
		(tv_mode->hburst_len << TV_HBURST_LEN_SHIFT);

	if (burst_ena)
		hctl2 |= TV_BURST_ENA;

	hctl3 = (tv_mode->hblank_start << TV_HBLANK_START_SHIFT) |
		(tv_mode->hblank_end << TV_HBLANK_END_SHIFT);

	vctl1 = (tv_mode->nbr_end << TV_NBR_END_SHIFT) |
		(tv_mode->vi_end_f1 << TV_VI_END_F1_SHIFT) |
		(tv_mode->vi_end_f2 << TV_VI_END_F2_SHIFT);

	vctl2 = (tv_mode->vsync_len << TV_VSYNC_LEN_SHIFT) |
		(tv_mode->vsync_start_f1 << TV_VSYNC_START_F1_SHIFT) |
		(tv_mode->vsync_start_f2 << TV_VSYNC_START_F2_SHIFT);

	vctl3 = (tv_mode->veq_len << TV_VEQ_LEN_SHIFT) |
		(tv_mode->veq_start_f1 << TV_VEQ_START_F1_SHIFT) |
		(tv_mode->veq_start_f2 << TV_VEQ_START_F2_SHIFT);

	if (tv_mode->veq_ena)
		vctl3 |= TV_EQUAL_ENA;

	vctl4 = (tv_mode->vburst_start_f1 << TV_VBURST_START_F1_SHIFT) |
		(tv_mode->vburst_end_f1 << TV_VBURST_END_F1_SHIFT);

	vctl5 = (tv_mode->vburst_start_f2 << TV_VBURST_START_F2_SHIFT) |
		(tv_mode->vburst_end_f2 << TV_VBURST_END_F2_SHIFT);

	vctl6 = (tv_mode->vburst_start_f3 << TV_VBURST_START_F3_SHIFT) |
		(tv_mode->vburst_end_f3 << TV_VBURST_END_F3_SHIFT);

	vctl7 = (tv_mode->vburst_start_f4 << TV_VBURST_START_F4_SHIFT) |
		(tv_mode->vburst_end_f4 << TV_VBURST_END_F4_SHIFT);

	I915_WRITE(TV_H_CTL_1, hctl1);
	I915_WRITE(TV_H_CTL_2, hctl2);
	I915_WRITE(TV_H_CTL_3, hctl3);
	I915_WRITE(TV_V_CTL_1, vctl1);
	I915_WRITE(TV_V_CTL_2, vctl2);
	I915_WRITE(TV_V_CTL_3, vctl3);
	I915_WRITE(TV_V_CTL_4, vctl4);
	I915_WRITE(TV_V_CTL_5, vctl5);
	I915_WRITE(TV_V_CTL_6, vctl6);
	I915_WRITE(TV_V_CTL_7, vctl7);
}

static void set_color_conversion(struct drm_i915_private *dev_priv,
				 const struct color_conversion *color_conversion)
{
	if (!color_conversion)
		return;

	I915_WRITE(TV_CSC_Y, (color_conversion->ry << 16) |
		   color_conversion->gy);
	I915_WRITE(TV_CSC_Y2, (color_conversion->by << 16) |
		   color_conversion->ay);
	I915_WRITE(TV_CSC_U, (color_conversion->ru << 16) |
		   color_conversion->gu);
	I915_WRITE(TV_CSC_U2, (color_conversion->bu << 16) |
		   color_conversion->au);
	I915_WRITE(TV_CSC_V, (color_conversion->rv << 16) |
		   color_conversion->gv);
	I915_WRITE(TV_CSC_V2, (color_conversion->bv << 16) |
		   color_conversion->av);
}

static void intel_tv_pre_enable(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->base.crtc);
	struct intel_tv *intel_tv = enc_to_tv(encoder);
	const struct tv_mode *tv_mode = intel_tv_mode_find(intel_tv);
	u32 tv_ctl;
	u32 scctl1, scctl2, scctl3;
	int i, j;
	const struct video_levels *video_levels;
	const struct color_conversion *color_conversion;
	bool burst_ena;
	int xpos = 0x0, ypos = 0x0;
	unsigned int xsize, ysize;

	if (!tv_mode)
		return;	/* can't happen (mode_prepare prevents this) */

	tv_ctl = I915_READ(TV_CTL);
	tv_ctl &= TV_CTL_SAVE;

	switch (intel_tv->type) {
	default:
	case DRM_MODE_CONNECTOR_Unknown:
	case DRM_MODE_CONNECTOR_Composite:
		tv_ctl |= TV_ENC_OUTPUT_COMPOSITE;
		video_levels = tv_mode->composite_levels;
		color_conversion = tv_mode->composite_color;
		burst_ena = tv_mode->burst_ena;
		break;
	case DRM_MODE_CONNECTOR_Component:
		tv_ctl |= TV_ENC_OUTPUT_COMPONENT;
		video_levels = &component_levels;
		if (tv_mode->burst_ena)
			color_conversion = &sdtv_csc_yprpb;
		else
			color_conversion = &hdtv_csc_yprpb;
		burst_ena = false;
		break;
	case DRM_MODE_CONNECTOR_SVIDEO:
		tv_ctl |= TV_ENC_OUTPUT_SVIDEO;
		video_levels = tv_mode->svideo_levels;
		color_conversion = tv_mode->svideo_color;
		burst_ena = tv_mode->burst_ena;
		break;
	}

	if (intel_crtc->pipe == 1)
		tv_ctl |= TV_ENC_PIPEB_SELECT;
	tv_ctl |= tv_mode->oversample;

	if (tv_mode->progressive)
		tv_ctl |= TV_PROGRESSIVE;
	if (tv_mode->trilevel_sync)
		tv_ctl |= TV_TRILEVEL_SYNC;
	if (tv_mode->pal_burst)
		tv_ctl |= TV_PAL_BURST;

	scctl1 = 0;
	if (tv_mode->dda1_inc)
		scctl1 |= TV_SC_DDA1_EN;
	if (tv_mode->dda2_inc)
		scctl1 |= TV_SC_DDA2_EN;
	if (tv_mode->dda3_inc)
		scctl1 |= TV_SC_DDA3_EN;
	scctl1 |= tv_mode->sc_reset;
	if (video_levels)
		scctl1 |= video_levels->burst << TV_BURST_LEVEL_SHIFT;
	scctl1 |= tv_mode->dda1_inc << TV_SCDDA1_INC_SHIFT;

	scctl2 = tv_mode->dda2_size << TV_SCDDA2_SIZE_SHIFT |
		tv_mode->dda2_inc << TV_SCDDA2_INC_SHIFT;

	scctl3 = tv_mode->dda3_size << TV_SCDDA3_SIZE_SHIFT |
		tv_mode->dda3_inc << TV_SCDDA3_INC_SHIFT;

	/* Enable two fixes for the chips that need them. */
	if (IS_I915GM(dev))
		tv_ctl |= TV_ENC_C0_FIX | TV_ENC_SDP_FIX;

	set_tv_mode_timings(dev_priv, tv_mode, burst_ena);

	I915_WRITE(TV_SC_CTL_1, scctl1);
	I915_WRITE(TV_SC_CTL_2, scctl2);
	I915_WRITE(TV_SC_CTL_3, scctl3);

	set_color_conversion(dev_priv, color_conversion);

	if (INTEL_INFO(dev)->gen >= 4)
		I915_WRITE(TV_CLR_KNOBS, 0x00404000);
	else
		I915_WRITE(TV_CLR_KNOBS, 0x00606000);

	if (video_levels)
		I915_WRITE(TV_CLR_LEVEL,
			   ((video_levels->black << TV_BLACK_LEVEL_SHIFT) |
			    (video_levels->blank << TV_BLANK_LEVEL_SHIFT)));

	assert_pipe_disabled(dev_priv, intel_crtc->pipe);

	/* Filter ctl must be set before TV_WIN_SIZE */
	I915_WRITE(TV_FILTER_CTL_1, TV_AUTO_SCALE);
	xsize = tv_mode->hblank_start - tv_mode->hblank_end;
	if (tv_mode->progressive)
		ysize = tv_mode->nbr_end + 1;
	else
		ysize = 2*tv_mode->nbr_end + 1;

	xpos += intel_tv->margin[TV_MARGIN_LEFT];
	ypos += intel_tv->margin[TV_MARGIN_TOP];
	xsize -= (intel_tv->margin[TV_MARGIN_LEFT] +
		  intel_tv->margin[TV_MARGIN_RIGHT]);
	ysize -= (intel_tv->margin[TV_MARGIN_TOP] +
		  intel_tv->margin[TV_MARGIN_BOTTOM]);
	I915_WRITE(TV_WIN_POS, (xpos<<16)|ypos);
	I915_WRITE(TV_WIN_SIZE, (xsize<<16)|ysize);

	j = 0;
	for (i = 0; i < 60; i++)
		I915_WRITE(TV_H_LUMA(i), tv_mode->filter_table[j++]);
	for (i = 0; i < 60; i++)
		I915_WRITE(TV_H_CHROMA(i), tv_mode->filter_table[j++]);
	for (i = 0; i < 43; i++)
		I915_WRITE(TV_V_LUMA(i), tv_mode->filter_table[j++]);
	for (i = 0; i < 43; i++)
		I915_WRITE(TV_V_CHROMA(i), tv_mode->filter_table[j++]);
	I915_WRITE(TV_DAC, I915_READ(TV_DAC) & TV_DAC_SAVE);
	I915_WRITE(TV_CTL, tv_ctl);
}

static const struct drm_display_mode reported_modes[] = {
	{
		.name = "NTSC 480i",
		.clock = 107520,
		.hdisplay = 1280,
		.hsync_start = 1368,
		.hsync_end = 1496,
		.htotal = 1712,

		.vdisplay = 1024,
		.vsync_start = 1027,
		.vsync_end = 1034,
		.vtotal = 1104,
		.type = DRM_MODE_TYPE_DRIVER,
	},
};

/**
 * Detects TV presence by checking for load.
 *
 * Requires that the current pipe's DPLL is active.

 * \return true if TV is connected.
 * \return false if TV is disconnected.
 */
static int
intel_tv_detect_type(struct intel_tv *intel_tv,
		      struct drm_connector *connector)
{
	struct drm_crtc *crtc = connector->state->crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 tv_ctl, save_tv_ctl;
	u32 tv_dac, save_tv_dac;
	int type;

	/* Disable TV interrupts around load detect or we'll recurse */
	if (connector->polled & DRM_CONNECTOR_POLL_HPD) {
		spin_lock_irq(&dev_priv->irq_lock);
		i915_disable_pipestat(dev_priv, 0,
				      PIPE_HOTPLUG_INTERRUPT_STATUS |
				      PIPE_HOTPLUG_TV_INTERRUPT_STATUS);
		spin_unlock_irq(&dev_priv->irq_lock);
	}

	save_tv_dac = tv_dac = I915_READ(TV_DAC);
	save_tv_ctl = tv_ctl = I915_READ(TV_CTL);

	/* Poll for TV detection */
	tv_ctl &= ~(TV_ENC_ENABLE | TV_TEST_MODE_MASK);
	tv_ctl |= TV_TEST_MODE_MONITOR_DETECT;
	if (intel_crtc->pipe == 1)
		tv_ctl |= TV_ENC_PIPEB_SELECT;
	else
		tv_ctl &= ~TV_ENC_PIPEB_SELECT;

	tv_dac &= ~(TVDAC_SENSE_MASK | DAC_A_MASK | DAC_B_MASK | DAC_C_MASK);
	tv_dac |= (TVDAC_STATE_CHG_EN |
		   TVDAC_A_SENSE_CTL |
		   TVDAC_B_SENSE_CTL |
		   TVDAC_C_SENSE_CTL |
		   DAC_CTL_OVERRIDE |
		   DAC_A_0_7_V |
		   DAC_B_0_7_V |
		   DAC_C_0_7_V);


	/*
	 * The TV sense state should be cleared to zero on cantiga platform. Otherwise
	 * the TV is misdetected. This is hardware requirement.
	 */
	if (IS_GM45(dev))
		tv_dac &= ~(TVDAC_STATE_CHG_EN | TVDAC_A_SENSE_CTL |
			    TVDAC_B_SENSE_CTL | TVDAC_C_SENSE_CTL);

	I915_WRITE(TV_CTL, tv_ctl);
	I915_WRITE(TV_DAC, tv_dac);
	POSTING_READ(TV_DAC);

	intel_wait_for_vblank(dev, intel_crtc->pipe);

	type = -1;
	tv_dac = I915_READ(TV_DAC);
	DRM_DEBUG_KMS("TV detected: %x, %x\n", tv_ctl, tv_dac);
	/*
	 *  A B C
	 *  0 1 1 Composite
	 *  1 0 X svideo
	 *  0 0 0 Component
	 */
	if ((tv_dac & TVDAC_SENSE_MASK) == (TVDAC_B_SENSE | TVDAC_C_SENSE)) {
		DRM_DEBUG_KMS("Detected Composite TV connection\n");
		type = DRM_MODE_CONNECTOR_Composite;
	} else if ((tv_dac & (TVDAC_A_SENSE|TVDAC_B_SENSE)) == TVDAC_A_SENSE) {
		DRM_DEBUG_KMS("Detected S-Video TV connection\n");
		type = DRM_MODE_CONNECTOR_SVIDEO;
	} else if ((tv_dac & TVDAC_SENSE_MASK) == 0) {
		DRM_DEBUG_KMS("Detected Component TV connection\n");
		type = DRM_MODE_CONNECTOR_Component;
	} else {
		DRM_DEBUG_KMS("Unrecognised TV connection\n");
		type = -1;
	}

	I915_WRITE(TV_DAC, save_tv_dac & ~TVDAC_STATE_CHG_EN);
	I915_WRITE(TV_CTL, save_tv_ctl);
	POSTING_READ(TV_CTL);

	/* For unknown reasons the hw barfs if we don't do this vblank wait. */
	intel_wait_for_vblank(dev, intel_crtc->pipe);

	/* Restore interrupt config */
	if (connector->polled & DRM_CONNECTOR_POLL_HPD) {
		spin_lock_irq(&dev_priv->irq_lock);
		i915_enable_pipestat(dev_priv, 0,
				     PIPE_HOTPLUG_INTERRUPT_STATUS |
				     PIPE_HOTPLUG_TV_INTERRUPT_STATUS);
		spin_unlock_irq(&dev_priv->irq_lock);
	}

	return type;
}

/*
 * Here we set accurate tv format according to connector type
 * i.e Component TV should not be assigned by NTSC or PAL
 */
static void intel_tv_find_better_format(struct drm_connector *connector)
{
	struct intel_tv *intel_tv = intel_attached_tv(connector);
	const struct tv_mode *tv_mode = intel_tv_mode_find(intel_tv);
	int i;

	if ((intel_tv->type == DRM_MODE_CONNECTOR_Component) ==
		tv_mode->component_only)
		return;


	for (i = 0; i < ARRAY_SIZE(tv_modes); i++) {
		tv_mode = tv_modes + i;

		if ((intel_tv->type == DRM_MODE_CONNECTOR_Component) ==
			tv_mode->component_only)
			break;
	}

	intel_tv->tv_format = tv_mode->name;
	drm_object_property_set_value(&connector->base,
		connector->dev->mode_config.tv_mode_property, i);
}

/**
 * Detect the TV connection.
 *
 * Currently this always returns CONNECTOR_STATUS_UNKNOWN, as we need to be sure
 * we have a pipe programmed in order to probe the TV.
 */
static enum drm_connector_status
intel_tv_detect(struct drm_connector *connector, bool force)
{
	struct drm_display_mode mode;
	struct intel_tv *intel_tv = intel_attached_tv(connector);
	enum drm_connector_status status;
	int type;

	DRM_DEBUG_KMS("[CONNECTOR:%d:%s] force=%d\n",
		      connector->base.id, connector->name,
		      force);

	mode = reported_modes[0];

	if (force) {
		struct intel_load_detect_pipe tmp;
		struct drm_modeset_acquire_ctx ctx;

		drm_modeset_acquire_init(&ctx, 0);

		if (intel_get_load_detect_pipe(connector, &mode, &tmp, &ctx)) {
			type = intel_tv_detect_type(intel_tv, connector);
			intel_release_load_detect_pipe(connector, &tmp, &ctx);
			status = type < 0 ?
				connector_status_disconnected :
				connector_status_connected;
		} else
			status = connector_status_unknown;

		drm_modeset_drop_locks(&ctx);
		drm_modeset_acquire_fini(&ctx);
	} else
		return connector->status;

	if (status != connector_status_connected)
		return status;

	intel_tv->type = type;
	intel_tv_find_better_format(connector);

	return connector_status_connected;
}

static const struct input_res {
	const char *name;
	int w, h;
} input_res_table[] = {
	{"640x480", 640, 480},
	{"800x600", 800, 600},
	{"1024x768", 1024, 768},
	{"1280x1024", 1280, 1024},
	{"848x480", 848, 480},
	{"1280x720", 1280, 720},
	{"1920x1080", 1920, 1080},
};

/*
 * Chose preferred mode  according to line number of TV format
 */
static void
intel_tv_chose_preferred_modes(struct drm_connector *connector,
			       struct drm_display_mode *mode_ptr)
{
	struct intel_tv *intel_tv = intel_attached_tv(connector);
	const struct tv_mode *tv_mode = intel_tv_mode_find(intel_tv);

	if (tv_mode->nbr_end < 480 && mode_ptr->vdisplay == 480)
		mode_ptr->type |= DRM_MODE_TYPE_PREFERRED;
	else if (tv_mode->nbr_end > 480) {
		if (tv_mode->progressive == true && tv_mode->nbr_end < 720) {
			if (mode_ptr->vdisplay == 720)
				mode_ptr->type |= DRM_MODE_TYPE_PREFERRED;
		} else if (mode_ptr->vdisplay == 1080)
				mode_ptr->type |= DRM_MODE_TYPE_PREFERRED;
	}
}

/**
 * Stub get_modes function.
 *
 * This should probably return a set of fixed modes, unless we can figure out
 * how to probe modes off of TV connections.
 */

static int
intel_tv_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode_ptr;
	struct intel_tv *intel_tv = intel_attached_tv(connector);
	const struct tv_mode *tv_mode = intel_tv_mode_find(intel_tv);
	int j, count = 0;
	u64 tmp;

	for (j = 0; j < ARRAY_SIZE(input_res_table);
	     j++) {
		const struct input_res *input = &input_res_table[j];
		unsigned int hactive_s = input->w;
		unsigned int vactive_s = input->h;

		if (tv_mode->max_srcw && input->w > tv_mode->max_srcw)
			continue;

		if (input->w > 1024 && (!tv_mode->progressive
					&& !tv_mode->component_only))
			continue;

		mode_ptr = drm_mode_create(connector->dev);
		if (!mode_ptr)
			continue;
		strncpy(mode_ptr->name, input->name, DRM_DISPLAY_MODE_LEN);
		mode_ptr->name[DRM_DISPLAY_MODE_LEN - 1] = '\0';

		mode_ptr->hdisplay = hactive_s;
		mode_ptr->hsync_start = hactive_s + 1;
		mode_ptr->hsync_end = hactive_s + 64;
		if (mode_ptr->hsync_end <= mode_ptr->hsync_start)
			mode_ptr->hsync_end = mode_ptr->hsync_start + 1;
		mode_ptr->htotal = hactive_s + 96;

		mode_ptr->vdisplay = vactive_s;
		mode_ptr->vsync_start = vactive_s + 1;
		mode_ptr->vsync_end = vactive_s + 32;
		if (mode_ptr->vsync_end <= mode_ptr->vsync_start)
			mode_ptr->vsync_end = mode_ptr->vsync_start  + 1;
		mode_ptr->vtotal = vactive_s + 33;

		tmp = (u64) tv_mode->refresh * mode_ptr->vtotal;
		tmp *= mode_ptr->htotal;
		tmp = div_u64(tmp, 1000000);
		mode_ptr->clock = (int) tmp;

		mode_ptr->type = DRM_MODE_TYPE_DRIVER;
		intel_tv_chose_preferred_modes(connector, mode_ptr);
		drm_mode_probed_add(connector, mode_ptr);
		count++;
	}

	return count;
}

static void
intel_tv_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
	kfree(connector);
}


static int
intel_tv_set_property(struct drm_connector *connector, struct drm_property *property,
		      uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct intel_tv *intel_tv = intel_attached_tv(connector);
	struct drm_crtc *crtc = intel_tv->base.base.crtc;
	int ret = 0;
	bool changed = false;

	ret = drm_object_property_set_value(&connector->base, property, val);
	if (ret < 0)
		goto out;

	if (property == dev->mode_config.tv_left_margin_property &&
		intel_tv->margin[TV_MARGIN_LEFT] != val) {
		intel_tv->margin[TV_MARGIN_LEFT] = val;
		changed = true;
	} else if (property == dev->mode_config.tv_right_margin_property &&
		intel_tv->margin[TV_MARGIN_RIGHT] != val) {
		intel_tv->margin[TV_MARGIN_RIGHT] = val;
		changed = true;
	} else if (property == dev->mode_config.tv_top_margin_property &&
		intel_tv->margin[TV_MARGIN_TOP] != val) {
		intel_tv->margin[TV_MARGIN_TOP] = val;
		changed = true;
	} else if (property == dev->mode_config.tv_bottom_margin_property &&
		intel_tv->margin[TV_MARGIN_BOTTOM] != val) {
		intel_tv->margin[TV_MARGIN_BOTTOM] = val;
		changed = true;
	} else if (property == dev->mode_config.tv_mode_property) {
		if (val >= ARRAY_SIZE(tv_modes)) {
			ret = -EINVAL;
			goto out;
		}
		if (!strcmp(intel_tv->tv_format, tv_modes[val].name))
			goto out;

		intel_tv->tv_format = tv_modes[val].name;
		changed = true;
	} else {
		ret = -EINVAL;
		goto out;
	}

	if (changed && crtc)
		intel_crtc_restore_mode(crtc);
out:
	return ret;
}

static const struct drm_connector_funcs intel_tv_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.detect = intel_tv_detect,
	.destroy = intel_tv_destroy,
	.set_property = intel_tv_set_property,
	.atomic_get_property = intel_connector_atomic_get_property,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
};

static const struct drm_connector_helper_funcs intel_tv_connector_helper_funcs = {
	.mode_valid = intel_tv_mode_valid,
	.get_modes = intel_tv_get_modes,
	.best_encoder = intel_best_encoder,
};

static const struct drm_encoder_funcs intel_tv_enc_funcs = {
	.destroy = intel_encoder_destroy,
};

void
intel_tv_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;
	struct intel_tv *intel_tv;
	struct intel_encoder *intel_encoder;
	struct intel_connector *intel_connector;
	u32 tv_dac_on, tv_dac_off, save_tv_dac;
	const char *tv_format_names[ARRAY_SIZE(tv_modes)];
	int i, initial_mode = 0;

	if ((I915_READ(TV_CTL) & TV_FUSE_STATE_MASK) == TV_FUSE_STATE_DISABLED)
		return;

	if (!intel_bios_is_tv_present(dev_priv)) {
		DRM_DEBUG_KMS("Integrated TV is not present.\n");
		return;
	}

	/*
	 * Sanity check the TV output by checking to see if the
	 * DAC register holds a value
	 */
	save_tv_dac = I915_READ(TV_DAC);

	I915_WRITE(TV_DAC, save_tv_dac | TVDAC_STATE_CHG_EN);
	tv_dac_on = I915_READ(TV_DAC);

	I915_WRITE(TV_DAC, save_tv_dac & ~TVDAC_STATE_CHG_EN);
	tv_dac_off = I915_READ(TV_DAC);

	I915_WRITE(TV_DAC, save_tv_dac);

	/*
	 * If the register does not hold the state change enable
	 * bit, (either as a 0 or a 1), assume it doesn't really
	 * exist
	 */
	if ((tv_dac_on & TVDAC_STATE_CHG_EN) == 0 ||
	    (tv_dac_off & TVDAC_STATE_CHG_EN) != 0)
		return;

	intel_tv = kzalloc(sizeof(*intel_tv), GFP_KERNEL);
	if (!intel_tv) {
		return;
	}

	intel_connector = intel_connector_alloc();
	if (!intel_connector) {
		kfree(intel_tv);
		return;
	}

	intel_encoder = &intel_tv->base;
	connector = &intel_connector->base;

	/* The documentation, for the older chipsets at least, recommend
	 * using a polling method rather than hotplug detection for TVs.
	 * This is because in order to perform the hotplug detection, the PLLs
	 * for the TV must be kept alive increasing power drain and starving
	 * bandwidth from other encoders. Notably for instance, it causes
	 * pipe underruns on Crestline when this encoder is supposedly idle.
	 *
	 * More recent chipsets favour HDMI rather than integrated S-Video.
	 */
	intel_connector->polled = DRM_CONNECTOR_POLL_CONNECT;

	drm_connector_init(dev, connector, &intel_tv_connector_funcs,
			   DRM_MODE_CONNECTOR_SVIDEO);

	drm_encoder_init(dev, &intel_encoder->base, &intel_tv_enc_funcs,
			 DRM_MODE_ENCODER_TVDAC, NULL);

	intel_encoder->compute_config = intel_tv_compute_config;
	intel_encoder->get_config = intel_tv_get_config;
	intel_encoder->pre_enable = intel_tv_pre_enable;
	intel_encoder->enable = intel_enable_tv;
	intel_encoder->disable = intel_disable_tv;
	intel_encoder->get_hw_state = intel_tv_get_hw_state;
	intel_connector->get_hw_state = intel_connector_get_hw_state;
	intel_connector->unregister = intel_connector_unregister;

	intel_connector_attach_encoder(intel_connector, intel_encoder);
	intel_encoder->type = INTEL_OUTPUT_TVOUT;
	intel_encoder->crtc_mask = (1 << 0) | (1 << 1);
	intel_encoder->cloneable = 0;
	intel_encoder->base.possible_crtcs = ((1 << 0) | (1 << 1));
	intel_tv->type = DRM_MODE_CONNECTOR_Unknown;

	/* BIOS margin values */
	intel_tv->margin[TV_MARGIN_LEFT] = 54;
	intel_tv->margin[TV_MARGIN_TOP] = 36;
	intel_tv->margin[TV_MARGIN_RIGHT] = 46;
	intel_tv->margin[TV_MARGIN_BOTTOM] = 37;

	intel_tv->tv_format = tv_modes[initial_mode].name;

	drm_connector_helper_add(connector, &intel_tv_connector_helper_funcs);
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	/* Create TV properties then attach current values */
	for (i = 0; i < ARRAY_SIZE(tv_modes); i++)
		tv_format_names[i] = tv_modes[i].name;
	drm_mode_create_tv_properties(dev,
				      ARRAY_SIZE(tv_modes),
				      tv_format_names);

	drm_object_attach_property(&connector->base, dev->mode_config.tv_mode_property,
				   initial_mode);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_left_margin_property,
				   intel_tv->margin[TV_MARGIN_LEFT]);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_top_margin_property,
				   intel_tv->margin[TV_MARGIN_TOP]);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_right_margin_property,
				   intel_tv->margin[TV_MARGIN_RIGHT]);
	drm_object_attach_property(&connector->base,
				   dev->mode_config.tv_bottom_margin_property,
				   intel_tv->margin[TV_MARGIN_BOTTOM]);
	drm_connector_register(connector);
}
