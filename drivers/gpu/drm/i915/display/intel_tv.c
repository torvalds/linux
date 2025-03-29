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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_driver.h"
#include "intel_display_types.h"
#include "intel_dpll.h"
#include "intel_hotplug.h"
#include "intel_load_detect.h"
#include "intel_tv.h"
#include "intel_tv_regs.h"

enum tv_margin {
	TV_MARGIN_LEFT, TV_MARGIN_TOP,
	TV_MARGIN_RIGHT, TV_MARGIN_BOTTOM
};

struct intel_tv {
	struct intel_encoder base;

	int type;
};

struct video_levels {
	u16 blank, black;
	u8 burst;
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

	u32 clock;
	u16 refresh; /* in millihertz (for precision) */
	u8 oversample;
	u8 hsync_end;
	u16 hblank_start, hblank_end, htotal;
	bool progressive : 1, trilevel_sync : 1, component_only : 1;
	u8 vsync_start_f1, vsync_start_f2, vsync_len;
	bool veq_ena : 1;
	u8 veq_start_f1, veq_start_f2, veq_len;
	u8 vi_end_f1, vi_end_f2;
	u16 nbr_end;
	bool burst_ena : 1;
	u8 hburst_start, hburst_len;
	u8 vburst_start_f1;
	u16 vburst_end_f1;
	u8 vburst_start_f2;
	u16 vburst_end_f2;
	u8 vburst_start_f3;
	u16 vburst_end_f3;
	u8 vburst_start_f4;
	u16 vburst_end_f4;
	/*
	 * subcarrier programming
	 */
	u16 dda2_size, dda3_size;
	u8 dda1_inc;
	u16 dda2_inc, dda3_inc;
	u32 sc_reset;
	bool pal_burst : 1;
	/*
	 * blank/black levels
	 */
	const struct video_levels *composite_levels, *svideo_levels;
	const struct color_conversion *composite_color, *svideo_color;
	const u32 *filter_table;
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

/*
 * Register programming values for TV modes.
 *
 * These values account for -1s required.
 */
static const struct tv_mode tv_modes[] = {
	{
		.name		= "NTSC-M",
		.clock		= 108000,
		.refresh	= 59940,
		.oversample	= 8,
		.component_only = false,
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
		.oversample	= 8,
		.component_only = false,
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
		.oversample	= 8,
		.component_only = false,

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
		.oversample	= 8,
		.component_only = false,

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
		.oversample	= 8,
		.component_only = false,

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
		.oversample	= 8,
		.component_only = false,

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
		.clock		= 108000,
		.refresh	= 59940,
		.oversample     = 4,
		.component_only = true,

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
		.clock		= 108000,
		.refresh	= 50000,
		.oversample     = 4,
		.component_only = true,

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
		.clock		= 148500,
		.refresh	= 60000,
		.oversample     = 2,
		.component_only = true,

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
		.clock		= 148500,
		.refresh	= 50000,
		.oversample     = 2,
		.component_only = true,

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
	},
	{
		.name       = "1080i@50Hz",
		.clock		= 148500,
		.refresh	= 50000,
		.oversample     = 2,
		.component_only = true,

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
		.clock		= 148500,
		.refresh	= 60000,
		.oversample     = 2,
		.component_only = true,

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

	{
		.name       = "1080p@30Hz",
		.clock		= 148500,
		.refresh	= 30000,
		.oversample     = 2,
		.component_only = true,

		.hsync_end      = 88,               .hblank_end         = 235,
		.hblank_start   = 2155,             .htotal             = 2199,

		.progressive	= true,		    .trilevel_sync = true,

		.vsync_start_f1 = 8,               .vsync_start_f2     = 8,
		.vsync_len      = 10,

		.veq_ena	= false,	.veq_start_f1	= 0,
		.veq_start_f2	= 0,		    .veq_len		= 0,

		.vi_end_f1      = 44,               .vi_end_f2          = 44,
		.nbr_end        = 1079,

		.burst_ena      = false,

		.filter_table = filter_table,
	},

	{
		.name       = "1080p@50Hz",
		.clock		= 148500,
		.refresh	= 50000,
		.oversample     = 1,
		.component_only = true,

		.hsync_end      = 88,               .hblank_end         = 235,
		.hblank_start   = 2155,             .htotal             = 2639,

		.progressive	= true,		    .trilevel_sync = true,

		.vsync_start_f1 = 8,               .vsync_start_f2     = 8,
		.vsync_len      = 10,

		.veq_ena	= false,	.veq_start_f1	= 0,
		.veq_start_f2	= 0,		    .veq_len		= 0,

		.vi_end_f1      = 44,               .vi_end_f2          = 44,
		.nbr_end        = 1079,

		.burst_ena      = false,

		.filter_table = filter_table,
	},

	{
		.name       = "1080p@60Hz",
		.clock		= 148500,
		.refresh	= 60000,
		.oversample     = 1,
		.component_only = true,

		.hsync_end      = 88,               .hblank_end         = 235,
		.hblank_start   = 2155,             .htotal             = 2199,

		.progressive	= true,		    .trilevel_sync = true,

		.vsync_start_f1 = 8,               .vsync_start_f2     = 8,
		.vsync_len      = 10,

		.veq_ena	= false,		    .veq_start_f1	= 0,
		.veq_start_f2	= 0,		    .veq_len		= 0,

		.vi_end_f1      = 44,               .vi_end_f2          = 44,
		.nbr_end        = 1079,

		.burst_ena      = false,

		.filter_table = filter_table,
	},
};

struct intel_tv_connector_state {
	struct drm_connector_state base;

	/*
	 * May need to override the user margins for
	 * gen3 >1024 wide source vertical centering.
	 */
	struct {
		u16 top, bottom;
	} margins;

	bool bypass_vfilter;
};

#define to_intel_tv_connector_state(conn_state) \
	container_of_const((conn_state), struct intel_tv_connector_state, base)

static struct drm_connector_state *
intel_tv_connector_duplicate_state(struct drm_connector *connector)
{
	struct intel_tv_connector_state *state;

	state = kmemdup(connector->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_connector_duplicate_state(connector, &state->base);
	return &state->base;
}

static struct intel_tv *enc_to_tv(struct intel_encoder *encoder)
{
	return container_of(encoder, struct intel_tv, base);
}

static struct intel_tv *intel_attached_tv(struct intel_connector *connector)
{
	return enc_to_tv(intel_attached_encoder(connector));
}

static bool
intel_tv_get_hw_state(struct intel_encoder *encoder, enum pipe *pipe)
{
	struct intel_display *display = to_intel_display(encoder);
	u32 tmp = intel_de_read(display, TV_CTL);

	*pipe = (tmp & TV_ENC_PIPE_SEL_MASK) >> TV_ENC_PIPE_SEL_SHIFT;

	return tmp & TV_ENC_ENABLE;
}

static void
intel_enable_tv(struct intel_atomic_state *state,
		struct intel_encoder *encoder,
		const struct intel_crtc_state *pipe_config,
		const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);

	/* Prevents vblank waits from timing out in intel_tv_detect_type() */
	intel_crtc_wait_for_next_vblank(to_intel_crtc(pipe_config->uapi.crtc));

	intel_de_rmw(display, TV_CTL, 0, TV_ENC_ENABLE);
}

static void
intel_disable_tv(struct intel_atomic_state *state,
		 struct intel_encoder *encoder,
		 const struct intel_crtc_state *old_crtc_state,
		 const struct drm_connector_state *old_conn_state)
{
	struct intel_display *display = to_intel_display(encoder);

	intel_de_rmw(display, TV_CTL, TV_ENC_ENABLE, 0);
}

static const struct tv_mode *intel_tv_mode_find(const struct drm_connector_state *conn_state)
{
	int format = conn_state->tv.legacy_mode;

	return &tv_modes[format];
}

static enum drm_mode_status
intel_tv_mode_valid(struct drm_connector *connector,
		    const struct drm_display_mode *mode)
{
	struct intel_display *display = to_intel_display(connector->dev);
	const struct tv_mode *tv_mode = intel_tv_mode_find(connector->state);
	int max_dotclk = display->cdclk.max_dotclk_freq;
	enum drm_mode_status status;

	status = intel_cpu_transcoder_mode_valid(display, mode);
	if (status != MODE_OK)
		return status;

	if (mode->clock > max_dotclk)
		return MODE_CLOCK_HIGH;

	/* Ensure TV refresh is close to desired refresh */
	if (abs(tv_mode->refresh - drm_mode_vrefresh(mode) * 1000) >= 1000)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static int
intel_tv_mode_vdisplay(const struct tv_mode *tv_mode)
{
	if (tv_mode->progressive)
		return tv_mode->nbr_end + 1;
	else
		return 2 * (tv_mode->nbr_end + 1);
}

static void
intel_tv_mode_to_mode(struct drm_display_mode *mode,
		      const struct tv_mode *tv_mode,
		      int clock)
{
	mode->clock = clock / (tv_mode->oversample >> !tv_mode->progressive);

	/*
	 * tv_mode horizontal timings:
	 *
	 * hsync_end
	 *    | hblank_end
	 *    |    | hblank_start
	 *    |    |       | htotal
	 *    |     _______    |
	 *     ____/       \___
	 * \__/                \
	 */
	mode->hdisplay =
		tv_mode->hblank_start - tv_mode->hblank_end;
	mode->hsync_start = mode->hdisplay +
		tv_mode->htotal - tv_mode->hblank_start;
	mode->hsync_end = mode->hsync_start +
		tv_mode->hsync_end;
	mode->htotal = tv_mode->htotal + 1;

	/*
	 * tv_mode vertical timings:
	 *
	 * vsync_start
	 *    | vsync_end
	 *    |  | vi_end nbr_end
	 *    |  |    |       |
	 *    |  |     _______
	 * \__    ____/       \
	 *    \__/
	 */
	mode->vdisplay = intel_tv_mode_vdisplay(tv_mode);
	if (tv_mode->progressive) {
		mode->vsync_start = mode->vdisplay +
			tv_mode->vsync_start_f1 + 1;
		mode->vsync_end = mode->vsync_start +
			tv_mode->vsync_len;
		mode->vtotal = mode->vdisplay +
			tv_mode->vi_end_f1 + 1;
	} else {
		mode->vsync_start = mode->vdisplay +
			tv_mode->vsync_start_f1 + 1 +
			tv_mode->vsync_start_f2 + 1;
		mode->vsync_end = mode->vsync_start +
			2 * tv_mode->vsync_len;
		mode->vtotal = mode->vdisplay +
			tv_mode->vi_end_f1 + 1 +
			tv_mode->vi_end_f2 + 1;
	}

	/* TV has it's own notion of sync and other mode flags, so clear them. */
	mode->flags = 0;

	snprintf(mode->name, sizeof(mode->name),
		 "%dx%d%c (%s)",
		 mode->hdisplay, mode->vdisplay,
		 tv_mode->progressive ? 'p' : 'i',
		 tv_mode->name);
}

static void intel_tv_scale_mode_horiz(struct drm_display_mode *mode,
				      int hdisplay, int left_margin,
				      int right_margin)
{
	int hsync_start = mode->hsync_start - mode->hdisplay + right_margin;
	int hsync_end = mode->hsync_end - mode->hdisplay + right_margin;
	int new_htotal = mode->htotal * hdisplay /
		(mode->hdisplay - left_margin - right_margin);

	mode->clock = mode->clock * new_htotal / mode->htotal;

	mode->hdisplay = hdisplay;
	mode->hsync_start = hdisplay + hsync_start * new_htotal / mode->htotal;
	mode->hsync_end = hdisplay + hsync_end * new_htotal / mode->htotal;
	mode->htotal = new_htotal;
}

static void intel_tv_scale_mode_vert(struct drm_display_mode *mode,
				     int vdisplay, int top_margin,
				     int bottom_margin)
{
	int vsync_start = mode->vsync_start - mode->vdisplay + bottom_margin;
	int vsync_end = mode->vsync_end - mode->vdisplay + bottom_margin;
	int new_vtotal = mode->vtotal * vdisplay /
		(mode->vdisplay - top_margin - bottom_margin);

	mode->clock = mode->clock * new_vtotal / mode->vtotal;

	mode->vdisplay = vdisplay;
	mode->vsync_start = vdisplay + vsync_start * new_vtotal / mode->vtotal;
	mode->vsync_end = vdisplay + vsync_end * new_vtotal / mode->vtotal;
	mode->vtotal = new_vtotal;
}

static void
intel_tv_get_config(struct intel_encoder *encoder,
		    struct intel_crtc_state *pipe_config)
{
	struct intel_display *display = to_intel_display(encoder);
	struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	struct drm_display_mode mode = {};
	u32 tv_ctl, hctl1, hctl3, vctl1, vctl2, tmp;
	struct tv_mode tv_mode = {};
	int hdisplay = adjusted_mode->crtc_hdisplay;
	int vdisplay = adjusted_mode->crtc_vdisplay;
	int xsize, ysize, xpos, ypos;

	pipe_config->output_types |= BIT(INTEL_OUTPUT_TVOUT);

	tv_ctl = intel_de_read(display, TV_CTL);
	hctl1 = intel_de_read(display, TV_H_CTL_1);
	hctl3 = intel_de_read(display, TV_H_CTL_3);
	vctl1 = intel_de_read(display, TV_V_CTL_1);
	vctl2 = intel_de_read(display, TV_V_CTL_2);

	tv_mode.htotal = (hctl1 & TV_HTOTAL_MASK) >> TV_HTOTAL_SHIFT;
	tv_mode.hsync_end = (hctl1 & TV_HSYNC_END_MASK) >> TV_HSYNC_END_SHIFT;

	tv_mode.hblank_start = (hctl3 & TV_HBLANK_START_MASK) >> TV_HBLANK_START_SHIFT;
	tv_mode.hblank_end = (hctl3 & TV_HSYNC_END_MASK) >> TV_HBLANK_END_SHIFT;

	tv_mode.nbr_end = (vctl1 & TV_NBR_END_MASK) >> TV_NBR_END_SHIFT;
	tv_mode.vi_end_f1 = (vctl1 & TV_VI_END_F1_MASK) >> TV_VI_END_F1_SHIFT;
	tv_mode.vi_end_f2 = (vctl1 & TV_VI_END_F2_MASK) >> TV_VI_END_F2_SHIFT;

	tv_mode.vsync_len = (vctl2 & TV_VSYNC_LEN_MASK) >> TV_VSYNC_LEN_SHIFT;
	tv_mode.vsync_start_f1 = (vctl2 & TV_VSYNC_START_F1_MASK) >> TV_VSYNC_START_F1_SHIFT;
	tv_mode.vsync_start_f2 = (vctl2 & TV_VSYNC_START_F2_MASK) >> TV_VSYNC_START_F2_SHIFT;

	tv_mode.clock = pipe_config->port_clock;

	tv_mode.progressive = tv_ctl & TV_PROGRESSIVE;

	switch (tv_ctl & TV_OVERSAMPLE_MASK) {
	case TV_OVERSAMPLE_8X:
		tv_mode.oversample = 8;
		break;
	case TV_OVERSAMPLE_4X:
		tv_mode.oversample = 4;
		break;
	case TV_OVERSAMPLE_2X:
		tv_mode.oversample = 2;
		break;
	default:
		tv_mode.oversample = 1;
		break;
	}

	tmp = intel_de_read(display, TV_WIN_POS);
	xpos = tmp >> 16;
	ypos = tmp & 0xffff;

	tmp = intel_de_read(display, TV_WIN_SIZE);
	xsize = tmp >> 16;
	ysize = tmp & 0xffff;

	intel_tv_mode_to_mode(&mode, &tv_mode, pipe_config->port_clock);

	drm_dbg_kms(display->drm, "TV mode: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(&mode));

	intel_tv_scale_mode_horiz(&mode, hdisplay,
				  xpos, mode.hdisplay - xsize - xpos);
	intel_tv_scale_mode_vert(&mode, vdisplay,
				 ypos, mode.vdisplay - ysize - ypos);

	adjusted_mode->crtc_clock = mode.clock;
	if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
		adjusted_mode->crtc_clock /= 2;

	/* pixel counter doesn't work on i965gm TV output */
	if (display->platform.i965gm)
		pipe_config->mode_flags |=
			I915_MODE_FLAG_USE_SCANLINE_COUNTER;
}

static bool intel_tv_source_too_wide(struct intel_display *display,
				     int hdisplay)
{
	return DISPLAY_VER(display) == 3 && hdisplay > 1024;
}

static bool intel_tv_vert_scaling(const struct drm_display_mode *tv_mode,
				  const struct drm_connector_state *conn_state,
				  int vdisplay)
{
	return tv_mode->crtc_vdisplay -
		conn_state->tv.margins.top -
		conn_state->tv.margins.bottom !=
		vdisplay;
}

static int
intel_tv_compute_config(struct intel_encoder *encoder,
			struct intel_crtc_state *pipe_config,
			struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_atomic_state *state =
		to_intel_atomic_state(pipe_config->uapi.state);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_tv_connector_state *tv_conn_state =
		to_intel_tv_connector_state(conn_state);
	const struct tv_mode *tv_mode = intel_tv_mode_find(conn_state);
	struct drm_display_mode *adjusted_mode =
		&pipe_config->hw.adjusted_mode;
	int hdisplay = adjusted_mode->crtc_hdisplay;
	int vdisplay = adjusted_mode->crtc_vdisplay;
	int ret;

	if (!tv_mode)
		return -EINVAL;

	if (adjusted_mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return -EINVAL;

	pipe_config->sink_format = INTEL_OUTPUT_FORMAT_RGB;
	pipe_config->output_format = INTEL_OUTPUT_FORMAT_RGB;

	drm_dbg_kms(display->drm, "forcing bpc to 8 for TV\n");
	pipe_config->pipe_bpp = 8*3;

	pipe_config->port_clock = tv_mode->clock;

	ret = intel_dpll_crtc_compute_clock(state, crtc);
	if (ret)
		return ret;

	pipe_config->clock_set = true;

	intel_tv_mode_to_mode(adjusted_mode, tv_mode, pipe_config->port_clock);
	drm_mode_set_crtcinfo(adjusted_mode, 0);

	if (intel_tv_source_too_wide(display, hdisplay) ||
	    !intel_tv_vert_scaling(adjusted_mode, conn_state, vdisplay)) {
		int extra, top, bottom;

		extra = adjusted_mode->crtc_vdisplay - vdisplay;

		if (extra < 0) {
			drm_dbg_kms(display->drm,
				    "No vertical scaling for >1024 pixel wide modes\n");
			return -EINVAL;
		}

		/* Need to turn off the vertical filter and center the image */

		/* Attempt to maintain the relative sizes of the margins */
		top = conn_state->tv.margins.top;
		bottom = conn_state->tv.margins.bottom;

		if (top + bottom)
			top = extra * top / (top + bottom);
		else
			top = extra / 2;
		bottom = extra - top;

		tv_conn_state->margins.top = top;
		tv_conn_state->margins.bottom = bottom;

		tv_conn_state->bypass_vfilter = true;

		if (!tv_mode->progressive) {
			adjusted_mode->clock /= 2;
			adjusted_mode->crtc_clock /= 2;
			adjusted_mode->flags |= DRM_MODE_FLAG_INTERLACE;
		}
	} else {
		tv_conn_state->margins.top = conn_state->tv.margins.top;
		tv_conn_state->margins.bottom = conn_state->tv.margins.bottom;

		tv_conn_state->bypass_vfilter = false;
	}

	drm_dbg_kms(display->drm, "TV mode: " DRM_MODE_FMT "\n",
		    DRM_MODE_ARG(adjusted_mode));

	/*
	 * The pipe scanline counter behaviour looks as follows when
	 * using the TV encoder:
	 *
	 * time ->
	 *
	 * dsl=vtotal-1       |             |
	 *                   ||            ||
	 *               ___| |        ___| |
	 *              /     |       /     |
	 *             /      |      /      |
	 * dsl=0   ___/       |_____/       |
	 *        | | |  |  | |
	 *         ^ ^ ^   ^ ^
	 *         | | |   | pipe vblank/first part of tv vblank
	 *         | | |   bottom margin
	 *         | | active
	 *         | top margin
	 *         remainder of tv vblank
	 *
	 * When the TV encoder is used the pipe wants to run faster
	 * than expected rate. During the active portion the TV
	 * encoder stalls the pipe every few lines to keep it in
	 * check. When the TV encoder reaches the bottom margin the
	 * pipe simply stops. Once we reach the TV vblank the pipe is
	 * no longer stalled and it runs at the max rate (apparently
	 * oversample clock on gen3, cdclk on gen4). Once the pipe
	 * reaches the pipe vtotal the pipe stops for the remainder
	 * of the TV vblank/top margin. The pipe starts up again when
	 * the TV encoder exits the top margin.
	 *
	 * To avoid huge hassles for vblank timestamping we scale
	 * the pipe timings as if the pipe always runs at the average
	 * rate it maintains during the active period. This also
	 * gives us a reasonable guesstimate as to the pixel rate.
	 * Due to the variation in the actual pipe speed the scanline
	 * counter will give us slightly erroneous results during the
	 * TV vblank/margins. But since vtotal was selected such that
	 * it matches the average rate of the pipe during the active
	 * portion the error shouldn't cause any serious grief to
	 * vblank timestamps.
	 *
	 * For posterity here is the empirically derived formula
	 * that gives us the maximum length of the pipe vblank
	 * we can use without causing display corruption. Following
	 * this would allow us to have a ticking scanline counter
	 * everywhere except during the bottom margin (there the
	 * pipe always stops). Ie. this would eliminate the second
	 * flat portion of the above graph. However this would also
	 * complicate vblank timestamping as the pipe vtotal would
	 * no longer match the average rate the pipe runs at during
	 * the active portion. Hence following this formula seems
	 * more trouble that it's worth.
	 *
	 * if (DISPLAY_VER(dev_priv) == 4) {
	 *	num = cdclk * (tv_mode->oversample >> !tv_mode->progressive);
	 *	den = tv_mode->clock;
	 * } else {
	 *	num = tv_mode->oversample >> !tv_mode->progressive;
	 *	den = 1;
	 * }
	 * max_pipe_vblank_len ~=
	 *	(num * tv_htotal * (tv_vblank_len + top_margin)) /
	 *	(den * pipe_htotal);
	 */
	intel_tv_scale_mode_horiz(adjusted_mode, hdisplay,
				  conn_state->tv.margins.left,
				  conn_state->tv.margins.right);
	intel_tv_scale_mode_vert(adjusted_mode, vdisplay,
				 tv_conn_state->margins.top,
				 tv_conn_state->margins.bottom);
	drm_mode_set_crtcinfo(adjusted_mode, 0);
	adjusted_mode->name[0] = '\0';

	/* pixel counter doesn't work on i965gm TV output */
	if (display->platform.i965gm)
		pipe_config->mode_flags |=
			I915_MODE_FLAG_USE_SCANLINE_COUNTER;

	return 0;
}

static void
set_tv_mode_timings(struct intel_display *display,
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

	intel_de_write(display, TV_H_CTL_1, hctl1);
	intel_de_write(display, TV_H_CTL_2, hctl2);
	intel_de_write(display, TV_H_CTL_3, hctl3);
	intel_de_write(display, TV_V_CTL_1, vctl1);
	intel_de_write(display, TV_V_CTL_2, vctl2);
	intel_de_write(display, TV_V_CTL_3, vctl3);
	intel_de_write(display, TV_V_CTL_4, vctl4);
	intel_de_write(display, TV_V_CTL_5, vctl5);
	intel_de_write(display, TV_V_CTL_6, vctl6);
	intel_de_write(display, TV_V_CTL_7, vctl7);
}

static void set_color_conversion(struct intel_display *display,
				 const struct color_conversion *color_conversion)
{
	intel_de_write(display, TV_CSC_Y,
		       (color_conversion->ry << 16) | color_conversion->gy);
	intel_de_write(display, TV_CSC_Y2,
		       (color_conversion->by << 16) | color_conversion->ay);
	intel_de_write(display, TV_CSC_U,
		       (color_conversion->ru << 16) | color_conversion->gu);
	intel_de_write(display, TV_CSC_U2,
		       (color_conversion->bu << 16) | color_conversion->au);
	intel_de_write(display, TV_CSC_V,
		       (color_conversion->rv << 16) | color_conversion->gv);
	intel_de_write(display, TV_CSC_V2,
		       (color_conversion->bv << 16) | color_conversion->av);
}

static void intel_tv_pre_enable(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct intel_tv *intel_tv = enc_to_tv(encoder);
	const struct intel_tv_connector_state *tv_conn_state =
		to_intel_tv_connector_state(conn_state);
	const struct tv_mode *tv_mode = intel_tv_mode_find(conn_state);
	u32 tv_ctl, tv_filter_ctl;
	u32 scctl1, scctl2, scctl3;
	int i, j;
	const struct video_levels *video_levels;
	const struct color_conversion *color_conversion;
	bool burst_ena;
	int xpos, ypos;
	unsigned int xsize, ysize;

	tv_ctl = intel_de_read(display, TV_CTL);
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

	tv_ctl |= TV_ENC_PIPE_SEL(crtc->pipe);

	switch (tv_mode->oversample) {
	case 8:
		tv_ctl |= TV_OVERSAMPLE_8X;
		break;
	case 4:
		tv_ctl |= TV_OVERSAMPLE_4X;
		break;
	case 2:
		tv_ctl |= TV_OVERSAMPLE_2X;
		break;
	default:
		tv_ctl |= TV_OVERSAMPLE_NONE;
		break;
	}

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
	if (display->platform.i915gm)
		tv_ctl |= TV_ENC_C0_FIX | TV_ENC_SDP_FIX;

	set_tv_mode_timings(display, tv_mode, burst_ena);

	intel_de_write(display, TV_SC_CTL_1, scctl1);
	intel_de_write(display, TV_SC_CTL_2, scctl2);
	intel_de_write(display, TV_SC_CTL_3, scctl3);

	set_color_conversion(display, color_conversion);

	if (DISPLAY_VER(display) >= 4)
		intel_de_write(display, TV_CLR_KNOBS, 0x00404000);
	else
		intel_de_write(display, TV_CLR_KNOBS, 0x00606000);

	if (video_levels)
		intel_de_write(display, TV_CLR_LEVEL,
			       ((video_levels->black << TV_BLACK_LEVEL_SHIFT) | (video_levels->blank << TV_BLANK_LEVEL_SHIFT)));

	assert_transcoder_disabled(display, pipe_config->cpu_transcoder);

	/* Filter ctl must be set before TV_WIN_SIZE */
	tv_filter_ctl = TV_AUTO_SCALE;
	if (tv_conn_state->bypass_vfilter)
		tv_filter_ctl |= TV_V_FILTER_BYPASS;
	intel_de_write(display, TV_FILTER_CTL_1, tv_filter_ctl);

	xsize = tv_mode->hblank_start - tv_mode->hblank_end;
	ysize = intel_tv_mode_vdisplay(tv_mode);

	xpos = conn_state->tv.margins.left;
	ypos = tv_conn_state->margins.top;
	xsize -= (conn_state->tv.margins.left +
		  conn_state->tv.margins.right);
	ysize -= (tv_conn_state->margins.top +
		  tv_conn_state->margins.bottom);
	intel_de_write(display, TV_WIN_POS, (xpos << 16) | ypos);
	intel_de_write(display, TV_WIN_SIZE, (xsize << 16) | ysize);

	j = 0;
	for (i = 0; i < 60; i++)
		intel_de_write(display, TV_H_LUMA(i),
			       tv_mode->filter_table[j++]);
	for (i = 0; i < 60; i++)
		intel_de_write(display, TV_H_CHROMA(i),
			       tv_mode->filter_table[j++]);
	for (i = 0; i < 43; i++)
		intel_de_write(display, TV_V_LUMA(i),
			       tv_mode->filter_table[j++]);
	for (i = 0; i < 43; i++)
		intel_de_write(display, TV_V_CHROMA(i),
			       tv_mode->filter_table[j++]);
	intel_de_write(display, TV_DAC,
		       intel_de_read(display, TV_DAC) & TV_DAC_SAVE);
	intel_de_write(display, TV_CTL, tv_ctl);
}

static int
intel_tv_detect_type(struct intel_tv *intel_tv,
		      struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_crtc *crtc = to_intel_crtc(connector->state->crtc);
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
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

	save_tv_dac = tv_dac = intel_de_read(display, TV_DAC);
	save_tv_ctl = tv_ctl = intel_de_read(display, TV_CTL);

	/* Poll for TV detection */
	tv_ctl &= ~(TV_ENC_ENABLE | TV_ENC_PIPE_SEL_MASK | TV_TEST_MODE_MASK);
	tv_ctl |= TV_TEST_MODE_MONITOR_DETECT;
	tv_ctl |= TV_ENC_PIPE_SEL(crtc->pipe);

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
	if (display->platform.gm45)
		tv_dac &= ~(TVDAC_STATE_CHG_EN | TVDAC_A_SENSE_CTL |
			    TVDAC_B_SENSE_CTL | TVDAC_C_SENSE_CTL);

	intel_de_write(display, TV_CTL, tv_ctl);
	intel_de_write(display, TV_DAC, tv_dac);
	intel_de_posting_read(display, TV_DAC);

	intel_crtc_wait_for_next_vblank(crtc);

	type = -1;
	tv_dac = intel_de_read(display, TV_DAC);
	drm_dbg_kms(display->drm, "TV detected: %x, %x\n", tv_ctl, tv_dac);
	/*
	 *  A B C
	 *  0 1 1 Composite
	 *  1 0 X svideo
	 *  0 0 0 Component
	 */
	if ((tv_dac & TVDAC_SENSE_MASK) == (TVDAC_B_SENSE | TVDAC_C_SENSE)) {
		drm_dbg_kms(display->drm,
			    "Detected Composite TV connection\n");
		type = DRM_MODE_CONNECTOR_Composite;
	} else if ((tv_dac & (TVDAC_A_SENSE|TVDAC_B_SENSE)) == TVDAC_A_SENSE) {
		drm_dbg_kms(display->drm,
			    "Detected S-Video TV connection\n");
		type = DRM_MODE_CONNECTOR_SVIDEO;
	} else if ((tv_dac & TVDAC_SENSE_MASK) == 0) {
		drm_dbg_kms(display->drm,
			    "Detected Component TV connection\n");
		type = DRM_MODE_CONNECTOR_Component;
	} else {
		drm_dbg_kms(display->drm, "Unrecognised TV connection\n");
		type = -1;
	}

	intel_de_write(display, TV_DAC, save_tv_dac & ~TVDAC_STATE_CHG_EN);
	intel_de_write(display, TV_CTL, save_tv_ctl);
	intel_de_posting_read(display, TV_CTL);

	/* For unknown reasons the hw barfs if we don't do this vblank wait. */
	intel_crtc_wait_for_next_vblank(crtc);

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
	struct intel_tv *intel_tv = intel_attached_tv(to_intel_connector(connector));
	const struct tv_mode *tv_mode = intel_tv_mode_find(connector->state);
	int i;

	/* Component supports everything so we can keep the current mode */
	if (intel_tv->type == DRM_MODE_CONNECTOR_Component)
		return;

	/* If the current mode is fine don't change it */
	if (!tv_mode->component_only)
		return;

	for (i = 0; i < ARRAY_SIZE(tv_modes); i++) {
		tv_mode = &tv_modes[i];

		if (!tv_mode->component_only)
			break;
	}

	connector->state->tv.legacy_mode = i;
}

static int
intel_tv_detect(struct drm_connector *connector,
		struct drm_modeset_acquire_ctx *ctx,
		bool force)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct intel_tv *intel_tv = intel_attached_tv(to_intel_connector(connector));
	enum drm_connector_status status;
	int type;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] force=%d\n",
		    connector->base.id, connector->name, force);

	if (!intel_display_device_enabled(display))
		return connector_status_disconnected;

	if (!intel_display_driver_check_access(display))
		return connector->status;

	if (force) {
		struct drm_atomic_state *state;

		state = intel_load_detect_get_pipe(connector, ctx);
		if (IS_ERR(state))
			return PTR_ERR(state);

		if (state) {
			type = intel_tv_detect_type(intel_tv, connector);
			intel_load_detect_release_pipe(connector, state, ctx);
			status = type < 0 ?
				connector_status_disconnected :
				connector_status_connected;
		} else {
			status = connector_status_unknown;
		}

		if (status == connector_status_connected) {
			intel_tv->type = type;
			intel_tv_find_better_format(connector);
		}

		return status;
	} else
		return connector->status;
}

static const struct input_res {
	u16 w, h;
} input_res_table[] = {
	{ 640, 480 },
	{ 800, 600 },
	{ 1024, 768 },
	{ 1280, 1024 },
	{ 848, 480 },
	{ 1280, 720 },
	{ 1920, 1080 },
};

/* Choose preferred mode according to line number of TV format */
static bool
intel_tv_is_preferred_mode(const struct drm_display_mode *mode,
			   const struct tv_mode *tv_mode)
{
	int vdisplay = intel_tv_mode_vdisplay(tv_mode);

	/* prefer 480 line modes for all SD TV modes */
	if (vdisplay <= 576)
		vdisplay = 480;

	return vdisplay == mode->vdisplay;
}

static void
intel_tv_set_mode_type(struct drm_display_mode *mode,
		       const struct tv_mode *tv_mode)
{
	mode->type = DRM_MODE_TYPE_DRIVER;

	if (intel_tv_is_preferred_mode(mode, tv_mode))
		mode->type |= DRM_MODE_TYPE_PREFERRED;
}

static int
intel_tv_get_modes(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	const struct tv_mode *tv_mode = intel_tv_mode_find(connector->state);
	int i, count = 0;

	for (i = 0; i < ARRAY_SIZE(input_res_table); i++) {
		const struct input_res *input = &input_res_table[i];
		struct drm_display_mode *mode;

		if (input->w > 1024 &&
		    !tv_mode->progressive &&
		    !tv_mode->component_only)
			continue;

		/* no vertical scaling with wide sources on gen3 */
		if (DISPLAY_VER(display) == 3 && input->w > 1024 &&
		    input->h > intel_tv_mode_vdisplay(tv_mode))
			continue;

		mode = drm_mode_create(connector->dev);
		if (!mode)
			continue;

		/*
		 * We take the TV mode and scale it to look
		 * like it had the expected h/vdisplay. This
		 * provides the most information to userspace
		 * about the actual timings of the mode. We
		 * do ignore the margins though.
		 */
		intel_tv_mode_to_mode(mode, tv_mode, tv_mode->clock);
		if (count == 0) {
			drm_dbg_kms(display->drm,
				    "TV mode: " DRM_MODE_FMT "\n",
				    DRM_MODE_ARG(mode));
		}
		intel_tv_scale_mode_horiz(mode, input->w, 0, 0);
		intel_tv_scale_mode_vert(mode, input->h, 0, 0);
		intel_tv_set_mode_type(mode, tv_mode);

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		count++;
	}

	return count;
}

static const struct drm_connector_funcs intel_tv_connector_funcs = {
	.late_register = intel_connector_register,
	.early_unregister = intel_connector_unregister,
	.destroy = intel_connector_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = intel_tv_connector_duplicate_state,
};

static int intel_tv_atomic_check(struct drm_connector *connector,
				 struct drm_atomic_state *state)
{
	struct drm_connector_state *new_state;
	struct drm_crtc_state *new_crtc_state;
	struct drm_connector_state *old_state;

	new_state = drm_atomic_get_new_connector_state(state, connector);
	if (!new_state->crtc)
		return 0;

	old_state = drm_atomic_get_old_connector_state(state, connector);
	new_crtc_state = drm_atomic_get_new_crtc_state(state, new_state->crtc);

	if (old_state->tv.legacy_mode != new_state->tv.legacy_mode ||
	    old_state->tv.margins.left != new_state->tv.margins.left ||
	    old_state->tv.margins.right != new_state->tv.margins.right ||
	    old_state->tv.margins.top != new_state->tv.margins.top ||
	    old_state->tv.margins.bottom != new_state->tv.margins.bottom) {
		/* Force a modeset. */

		new_crtc_state->connectors_changed = true;
	}

	return 0;
}

static const struct drm_connector_helper_funcs intel_tv_connector_helper_funcs = {
	.detect_ctx = intel_tv_detect,
	.mode_valid = intel_tv_mode_valid,
	.get_modes = intel_tv_get_modes,
	.atomic_check = intel_tv_atomic_check,
};

static const struct drm_encoder_funcs intel_tv_enc_funcs = {
	.destroy = intel_encoder_destroy,
};

static void intel_tv_add_properties(struct drm_connector *connector)
{
	struct intel_display *display = to_intel_display(connector->dev);
	struct drm_connector_state *conn_state = connector->state;
	const char *tv_format_names[ARRAY_SIZE(tv_modes)];
	int i;

	/* BIOS margin values */
	conn_state->tv.margins.left = 54;
	conn_state->tv.margins.top = 36;
	conn_state->tv.margins.right = 46;
	conn_state->tv.margins.bottom = 37;

	conn_state->tv.legacy_mode = 0;

	/* Create TV properties then attach current values */
	for (i = 0; i < ARRAY_SIZE(tv_modes); i++) {
		/* 1080p50/1080p60 not supported on gen3 */
		if (DISPLAY_VER(display) == 3 && tv_modes[i].oversample == 1)
			break;

		tv_format_names[i] = tv_modes[i].name;
	}
	drm_mode_create_tv_properties_legacy(display->drm, i, tv_format_names);

	drm_object_attach_property(&connector->base,
				   display->drm->mode_config.legacy_tv_mode_property,
				   conn_state->tv.legacy_mode);
	drm_object_attach_property(&connector->base,
				   display->drm->mode_config.tv_left_margin_property,
				   conn_state->tv.margins.left);
	drm_object_attach_property(&connector->base,
				   display->drm->mode_config.tv_top_margin_property,
				   conn_state->tv.margins.top);
	drm_object_attach_property(&connector->base,
				   display->drm->mode_config.tv_right_margin_property,
				   conn_state->tv.margins.right);
	drm_object_attach_property(&connector->base,
				   display->drm->mode_config.tv_bottom_margin_property,
				   conn_state->tv.margins.bottom);
}

void
intel_tv_init(struct intel_display *display)
{
	struct drm_connector *connector;
	struct intel_tv *intel_tv;
	struct intel_encoder *intel_encoder;
	struct intel_connector *intel_connector;
	u32 tv_dac_on, tv_dac_off, save_tv_dac;

	if ((intel_de_read(display, TV_CTL) & TV_FUSE_STATE_MASK) == TV_FUSE_STATE_DISABLED)
		return;

	if (!intel_bios_is_tv_present(display)) {
		drm_dbg_kms(display->drm, "Integrated TV is not present.\n");
		return;
	}

	/*
	 * Sanity check the TV output by checking to see if the
	 * DAC register holds a value
	 */
	save_tv_dac = intel_de_read(display, TV_DAC);

	intel_de_write(display, TV_DAC, save_tv_dac | TVDAC_STATE_CHG_EN);
	tv_dac_on = intel_de_read(display, TV_DAC);

	intel_de_write(display, TV_DAC, save_tv_dac & ~TVDAC_STATE_CHG_EN);
	tv_dac_off = intel_de_read(display, TV_DAC);

	intel_de_write(display, TV_DAC, save_tv_dac);

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

	/*
	 * The documentation, for the older chipsets at least, recommend
	 * using a polling method rather than hotplug detection for TVs.
	 * This is because in order to perform the hotplug detection, the PLLs
	 * for the TV must be kept alive increasing power drain and starving
	 * bandwidth from other encoders. Notably for instance, it causes
	 * pipe underruns on Crestline when this encoder is supposedly idle.
	 *
	 * More recent chipsets favour HDMI rather than integrated S-Video.
	 */
	intel_connector->polled = DRM_CONNECTOR_POLL_CONNECT;
	intel_connector->base.polled = intel_connector->polled;

	drm_connector_init(display->drm, connector, &intel_tv_connector_funcs,
			   DRM_MODE_CONNECTOR_SVIDEO);

	drm_encoder_init(display->drm, &intel_encoder->base,
			 &intel_tv_enc_funcs,
			 DRM_MODE_ENCODER_TVDAC, "TV");

	intel_encoder->compute_config = intel_tv_compute_config;
	intel_encoder->get_config = intel_tv_get_config;
	intel_encoder->pre_enable = intel_tv_pre_enable;
	intel_encoder->enable = intel_enable_tv;
	intel_encoder->disable = intel_disable_tv;
	intel_encoder->get_hw_state = intel_tv_get_hw_state;
	intel_connector->get_hw_state = intel_connector_get_hw_state;

	intel_connector_attach_encoder(intel_connector, intel_encoder);

	intel_encoder->type = INTEL_OUTPUT_TVOUT;
	intel_encoder->power_domain = POWER_DOMAIN_PORT_OTHER;
	intel_encoder->port = PORT_NONE;
	intel_encoder->pipe_mask = ~0;
	intel_encoder->cloneable = 0;
	intel_tv->type = DRM_MODE_CONNECTOR_Unknown;

	drm_connector_helper_add(connector, &intel_tv_connector_helper_funcs);

	intel_tv_add_properties(connector);
}
