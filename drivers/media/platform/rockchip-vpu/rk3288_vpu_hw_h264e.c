/*
 * Rockchip rk3288 VPU codec driver
 *
 * Copyright (C) 2014 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 *	Jung Zhao <jung.zhao@rock-chips.com>
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rockchip_vpu_common.h"

#include <linux/types.h>
#include <linux/sort.h>

#include "rk3288_vpu_regs.h"
#include "rockchip_vpu_hw.h"

/* H.264 motion estimation parameters */
static const u32 h264_prev_mode_favor[52] = {
	7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 24, 25, 27, 29, 30, 32, 34, 36, 38, 41, 43, 46,
	49, 51, 55, 58, 61, 65, 69, 73, 78, 82, 87, 93, 98, 104, 110,
	117, 124, 132, 140
};

/* sqrt(2^((qp-12)/3))*8 */
static const u32 h264_diff_mv_penalty[52] = {
	2, 2, 3, 3, 3, 4, 4, 4, 5, 6,
	6, 7, 8, 9, 10, 11, 13, 14, 16, 18,
	20, 23, 26, 29, 32, 36, 40, 45, 51, 57,
	64, 72, 81, 91, 102, 114, 128, 144, 161, 181,
	203, 228, 256, 287, 323, 362, 406, 456, 512, 575,
	645, 724
};

/* 31*sqrt(2^((qp-12)/3))/4 */
static const u32 h264_diff_mv_penalty4p[52] = {
	2, 2, 2, 3, 3, 3, 4, 4, 5, 5,
	6, 7, 8, 9, 10, 11, 12, 14, 16, 17,
	20, 22, 25, 28, 31, 35, 39, 44, 49, 55,
	62, 70, 78, 88, 98, 110, 124, 139, 156, 175,
	197, 221, 248, 278, 312, 351, 394, 442, 496, 557,
	625, 701
};

static const u32 h264_intra16_favor[52] = {
	24, 24, 24, 26, 27, 30, 32, 35, 39, 43, 48, 53, 58, 64, 71, 78,
	85, 93, 102, 111, 121, 131, 142, 154, 167, 180, 195, 211, 229,
	248, 271, 296, 326, 361, 404, 457, 523, 607, 714, 852, 1034,
	1272, 1588, 2008, 2568, 3318, 4323, 5672, 7486, 9928, 13216,
	17648
};

static const u32 h264_inter_favor[52] = {
	40, 40, 41, 42, 43, 44, 45, 48, 51, 53, 55, 60, 62, 67, 69, 72,
	78, 84, 90, 96, 110, 120, 135, 152, 170, 189, 210, 235, 265,
	297, 335, 376, 420, 470, 522, 572, 620, 670, 724, 770, 820,
	867, 915, 970, 1020, 1076, 1132, 1180, 1230, 1275, 1320, 1370
};

static u32 h264_skip_sad_penalty[52] = {
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 233, 205, 182, 163,
	146, 132, 120, 109, 100,  92,  84,  78,  71,  66,  61,  56,  52,  48,
	44,  41,  38,  35,  32,  30,  27,  25,  23,  21,  19,  17,  15,  14,
	12,  11,   9,   8,   7,   5,   4,   3,   2,   1
};

static const s32 h264_context_init_intra[460][2] = {
	/* 0 -> 10 */
	{ 20, -15 }, { 2, 54 }, { 3, 74 }, { 20, -15 },
	{ 2, 54 }, { 3, 74 }, { -28, 127 }, { -23, 104 },
	{ -6, 53 }, { -1, 54 }, { 7, 51 },

	/* 11 -> 23 unsused for I */
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 },

	/* 24 -> 39 */
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },

	/* 40 -> 53 */
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 },

	/* 54 -> 59 */
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 0, 0 }, { 0, 0 },

	/* 60 -> 69 */
	{ 0, 41 }, { 0, 63 }, { 0, 63 }, { 0, 63 },
	{ -9, 83 }, { 4, 86 }, { 0, 97 }, { -7, 72 },
	{ 13, 41 }, { 3, 62 },

	/* 70 -> 87 */
	{ 0, 11 }, { 1, 55 }, { 0, 69 }, { -17, 127 },
	{ -13, 102 }, { 0, 82 }, { -7, 74 }, { -21, 107 },
	{ -27, 127 }, { -31, 127 }, { -24, 127 }, { -18, 95 },
	{ -27, 127 }, { -21, 114 }, { -30, 127 }, { -17, 123 },
	{ -12, 115 }, { -16, 122 },

	/* 88 -> 104 */
	{ -11, 115 }, { -12, 63 }, { -2, 68 }, { -15, 84 },
	{ -13, 104 }, { -3, 70 }, { -8, 93 }, { -10, 90 },
	{ -30, 127 }, { -1, 74 }, { -6, 97 }, { -7, 91 },
	{ -20, 127 }, { -4, 56 }, { -5, 82 }, { -7, 76 },
	{ -22, 125 },

	/* 105 -> 135 */
	{ -7, 93 }, { -11, 87 }, { -3, 77 }, { -5, 71 },
	{ -4, 63 }, { -4, 68 }, { -12, 84 }, { -7, 62 },
	{ -7, 65 }, { 8, 61 }, { 5, 56 }, { -2, 66 },
	{ 1, 64 }, { 0, 61 }, { -2, 78 }, { 1, 50 },
	{ 7, 52 }, { 10, 35 }, { 0, 44 }, { 11, 38 },
	{ 1, 45 }, { 0, 46 }, { 5, 44 }, { 31, 17 },
	{ 1, 51 }, { 7, 50 }, { 28, 19 }, { 16, 33 },
	{ 14, 62 }, { -13, 108 }, { -15, 100 },

	/* 136 -> 165 */
	{ -13, 101 }, { -13, 91 }, { -12, 94 }, { -10, 88 },
	{ -16, 84 }, { -10, 86 }, { -7, 83 }, { -13, 87 },
	{ -19, 94 }, { 1, 70 }, { 0, 72 }, { -5, 74 },
	{ 18, 59 }, { -8, 102 }, { -15, 100 }, { 0, 95 },
	{ -4, 75 }, { 2, 72 }, { -11, 75 }, { -3, 71 },
	{ 15, 46 }, { -13, 69 }, { 0, 62 }, { 0, 65 },
	{ 21, 37 }, { -15, 72 }, { 9, 57 }, { 16, 54 },
	{ 0, 62 }, { 12, 72 },

	/* 166 -> 196 */
	{ 24, 0 }, { 15, 9 }, { 8, 25 }, { 13, 18 },
	{ 15, 9 }, { 13, 19 }, { 10, 37 }, { 12, 18 },
	{ 6, 29 }, { 20, 33 }, { 15, 30 }, { 4, 45 },
	{ 1, 58 }, { 0, 62 }, { 7, 61 }, { 12, 38 },
	{ 11, 45 }, { 15, 39 }, { 11, 42 }, { 13, 44 },
	{ 16, 45 }, { 12, 41 }, { 10, 49 }, { 30, 34 },
	{ 18, 42 }, { 10, 55 }, { 17, 51 }, { 17, 46 },
	{ 0, 89 }, { 26, -19 }, { 22, -17 },

	/* 197 -> 226 */
	{ 26, -17 }, { 30, -25 }, { 28, -20 }, { 33, -23 },
	{ 37, -27 }, { 33, -23 }, { 40, -28 }, { 38, -17 },
	{ 33, -11 }, { 40, -15 }, { 41, -6 }, { 38, 1 },
	{ 41, 17 }, { 30, -6 }, { 27, 3 }, { 26, 22 },
	{ 37, -16 }, { 35, -4 }, { 38, -8 }, { 38, -3 },
	{ 37, 3 }, { 38, 5 }, { 42, 0 }, { 35, 16 },
	{ 39, 22 }, { 14, 48 }, { 27, 37 }, { 21, 60 },
	{ 12, 68 }, { 2, 97 },

	/* 227 -> 251 */
	{ -3, 71 }, { -6, 42 }, { -5, 50 }, { -3, 54 },
	{ -2, 62 }, { 0, 58 }, { 1, 63 }, { -2, 72 },
	{ -1, 74 }, { -9, 91 }, { -5, 67 }, { -5, 27 },
	{ -3, 39 }, { -2, 44 }, { 0, 46 }, { -16, 64 },
	{ -8, 68 }, { -10, 78 }, { -6, 77 }, { -10, 86 },
	{ -12, 92 }, { -15, 55 }, { -10, 60 }, { -6, 62 },
	{ -4, 65 },

	/* 252 -> 275 */
	{ -12, 73 }, { -8, 76 }, { -7, 80 }, { -9, 88 },
	{ -17, 110 }, { -11, 97 }, { -20, 84 }, { -11, 79 },
	{ -6, 73 }, { -4, 74 }, { -13, 86 }, { -13, 96 },
	{ -11, 97 }, { -19, 117 }, { -8, 78 }, { -5, 33 },
	{ -4, 48 }, { -2, 53 }, { -3, 62 }, { -13, 71 },
	{ -10, 79 }, { -12, 86 }, { -13, 90 }, { -14, 97 },

	/* 276 special case, bypass used */
	{ 0, 0 },

	/* 277 -> 307 */
	{ -6, 93 }, { -6, 84 }, { -8, 79 }, { 0, 66 },
	{ -1, 71 }, { 0, 62 }, { -2, 60 }, { -2, 59 },
	{ -5, 75 }, { -3, 62 }, { -4, 58 }, { -9, 66 },
	{ -1, 79 }, { 0, 71 }, { 3, 68 }, { 10, 44 },
	{ -7, 62 }, { 15, 36 }, { 14, 40 }, { 16, 27 },
	{ 12, 29 }, { 1, 44 }, { 20, 36 }, { 18, 32 },
	{ 5, 42 }, { 1, 48 }, { 10, 62 }, { 17, 46 },
	{ 9, 64 }, { -12, 104 }, { -11, 97 },

	/* 308 -> 337 */
	{ -16, 96 }, { -7, 88 }, { -8, 85 }, { -7, 85 },
	{ -9, 85 }, { -13, 88 }, { 4, 66 }, { -3, 77 },
	{ -3, 76 }, { -6, 76 }, { 10, 58 }, { -1, 76 },
	{ -1, 83 }, { -7, 99 }, { -14, 95 }, { 2, 95 },
	{ 0, 76 }, { -5, 74 }, { 0, 70 }, { -11, 75 },
	{ 1, 68 }, { 0, 65 }, { -14, 73 }, { 3, 62 },
	{ 4, 62 }, { -1, 68 }, { -13, 75 }, { 11, 55 },
	{ 5, 64 }, { 12, 70 },

	/* 338 -> 368 */
	{ 15, 6 }, { 6, 19 }, { 7, 16 }, { 12, 14 },
	{ 18, 13 }, { 13, 11 }, { 13, 15 }, { 15, 16 },
	{ 12, 23 }, { 13, 23 }, { 15, 20 }, { 14, 26 },
	{ 14, 44 }, { 17, 40 }, { 17, 47 }, { 24, 17 },
	{ 21, 21 }, { 25, 22 }, { 31, 27 }, { 22, 29 },
	{ 19, 35 }, { 14, 50 }, { 10, 57 }, { 7, 63 },
	{ -2, 77 }, { -4, 82 }, { -3, 94 }, { 9, 69 },
	{ -12, 109 }, { 36, -35 }, { 36, -34 },

	/* 369 -> 398 */
	{ 32, -26 }, { 37, -30 }, { 44, -32 }, { 34, -18 },
	{ 34, -15 }, { 40, -15 }, { 33, -7 }, { 35, -5 },
	{ 33, 0 }, { 38, 2 }, { 33, 13 }, { 23, 35 },
	{ 13, 58 }, { 29, -3 }, { 26, 0 }, { 22, 30 },
	{ 31, -7 }, { 35, -15 }, { 34, -3 }, { 34, 3 },
	{ 36, -1 }, { 34, 5 }, { 32, 11 }, { 35, 5 },
	{ 34, 12 }, { 39, 11 }, { 30, 29 }, { 34, 26 },
	{ 29, 39 }, { 19, 66 },

	/* 399 -> 435 */
	{ 31, 21 }, { 31, 31 }, { 25, 50 },
	{ -17, 120 }, { -20, 112 }, { -18, 114 }, { -11, 85 },
	{ -15, 92 }, { -14, 89 }, { -26, 71 }, { -15, 81 },
	{ -14, 80 }, { 0, 68 }, { -14, 70 }, { -24, 56 },
	{ -23, 68 }, { -24, 50 }, { -11, 74 }, { 23, -13 },
	{ 26, -13 }, { 40, -15 }, { 49, -14 }, { 44, 3 },
	{ 45, 6 }, { 44, 34 }, { 33, 54 }, { 19, 82 },
	{ -3, 75 }, { -1, 23 }, { 1, 34 }, { 1, 43 },
	{ 0, 54 }, { -2, 55 }, { 0, 61 }, { 1, 64 },
	{ 0, 68 }, { -9, 92 },

	/* 436 -> 459 */
	{ -14, 106 }, { -13, 97 }, { -15, 90 }, { -12, 90 },
	{ -18, 88 }, { -10, 73 }, { -9, 79 }, { -14, 86 },
	{ -10, 73 }, { -10, 70 }, { -10, 69 }, { -5, 66 },
	{ -9, 64 }, { -5, 58 }, { 2, 59 }, { 21, -10 },
	{ 24, -11 }, { 28, -8 }, { 28, -1 }, { 29, 3 },
	{ 29, 9 }, { 35, 20 }, { 29, 36 }, { 14, 67 }
};

#define H264E_CABAC_IDC_NUM	3
static const s32 h264_context_init[H264E_CABAC_IDC_NUM][460][2] = {
	/* cabac_init_idc == 0 */
	{
		/* 0 -> 10 */
		{ 20, -15 }, { 2, 54 }, { 3, 74 }, { 20, -15 },
		{ 2, 54 }, { 3, 74 }, { -28, 127 }, { -23, 104 },
		{ -6, 53 }, { -1, 54 }, { 7, 51 },

		/* 11 -> 23 */
		{ 23, 33 }, { 23, 2 }, { 21, 0 }, { 1, 9 },
		{ 0, 49 }, { -37, 118 }, { 5, 57 }, { -13, 78 },
		{ -11, 65 }, { 1, 62 }, { 12, 49 }, { -4, 73 },
		{ 17, 50 },

		/* 24 -> 39 */
		{ 18, 64 }, { 9, 43 }, { 29, 0 }, { 26, 67 },
		{ 16, 90 }, { 9, 104 }, { -46, 127 }, { -20, 104 },
		{ 1, 67 }, { -13, 78 }, { -11, 65 }, { 1, 62 },
		{ -6, 86 }, { -17, 95 }, { -6, 61 }, { 9, 45 },

		/* 40 -> 53 */
		{ -3, 69 }, { -6, 81 }, { -11, 96 }, { 6, 55 },
		{ 7, 67 }, { -5, 86 }, { 2, 88 }, { 0, 58 },
		{ -3, 76 }, { -10, 94 }, { 5, 54 }, { 4, 69 },
		{ -3, 81 }, { 0, 88 },

		/* 54 -> 59 */
		{ -7, 67 }, { -5, 74 }, { -4, 74 }, { -5, 80 },
		{ -7, 72 }, { 1, 58 },

		/* 60 -> 69 */
		{ 0, 41 }, { 0, 63 }, { 0, 63 }, { 0, 63 },
		{ -9, 83 }, { 4, 86 }, { 0, 97 }, { -7, 72 },
		{ 13, 41 }, { 3, 62 },

		/* 70 -> 87 */
		{ 0, 45 }, { -4, 78 }, { -3, 96 }, { -27, 126 },
		{ -28, 98 }, { -25, 101 }, { -23, 67 }, { -28, 82 },
		{ -20, 94 }, { -16, 83 }, { -22, 110 }, { -21, 91 },
		{ -18, 102 }, { -13, 93 }, { -29, 127 }, { -7, 92 },
		{ -5, 89 }, { -7, 96 }, { -13, 108 }, { -3, 46 },
		{ -1, 65 }, { -1, 57 }, { -9, 93 }, { -3, 74 },
		{ -9, 92 }, { -8, 87 }, { -23, 126 }, { 5, 54 },
		{ 6, 60 }, { 6, 59 }, { 6, 69 }, { -1, 48 },
		{ 0, 68 }, { -4, 69 }, { -8, 88 },

		/* 105 -> 165 */
		{ -2, 85 }, { -6, 78 }, { -1, 75 }, { -7, 77 },
		{ 2, 54 }, { 5, 50 }, { -3, 68 }, { 1, 50 },
		{ 6, 42 }, { -4, 81 }, { 1, 63 }, { -4, 70 },
		{ 0, 67 }, { 2, 57 }, { -2, 76 }, { 11, 35 },
		{ 4, 64 }, { 1, 61 }, { 11, 35 }, { 18, 25 },
		{ 12, 24 }, { 13, 29 }, { 13, 36 }, { -10, 93 },
		{ -7, 73 }, { -2, 73 }, { 13, 46 }, { 9, 49 },
		{ -7, 100 }, { 9, 53 }, { 2, 53 }, { 5, 53 },
		{ -2, 61 }, { 0, 56 }, { 0, 56 }, { -13, 63 },
		{ -5, 60 }, { -1, 62 }, { 4, 57 }, { -6, 69 },
		{ 4, 57 }, { 14, 39 }, { 4, 51 }, { 13, 68 },
		{ 3, 64 }, { 1, 61 }, { 9, 63 }, { 7, 50 },
		{ 16, 39 }, { 5, 44 }, { 4, 52 }, { 11, 48 },
		{ -5, 60 }, { -1, 59 }, { 0, 59 }, { 22, 33 },
		{ 5, 44 }, { 14, 43 }, { -1, 78 }, { 0, 60 },
		{ 9, 69 },

		/* 166 -> 226 */
		{ 11, 28 }, { 2, 40 }, { 3, 44 }, { 0, 49 },
		{ 0, 46 }, { 2, 44 }, { 2, 51 }, { 0, 47 },
		{ 4, 39 }, { 2, 62 }, { 6, 46 }, { 0, 54 },
		{ 3, 54 }, { 2, 58 }, { 4, 63 }, { 6, 51 },
		{ 6, 57 }, { 7, 53 }, { 6, 52 }, { 6, 55 },
		{ 11, 45 }, { 14, 36 }, { 8, 53 }, { -1, 82 },
		{ 7, 55 }, { -3, 78 }, { 15, 46 }, { 22, 31 },
		{ -1, 84 }, { 25, 7 }, { 30, -7 }, { 28, 3 },
		{ 28, 4 }, { 32, 0 }, { 34, -1 }, { 30, 6 },
		{ 30, 6 }, { 32, 9 }, { 31, 19 }, { 26, 27 },
		{ 26, 30 }, { 37, 20 }, { 28, 34 }, { 17, 70 },
		{ 1, 67 }, { 5, 59 }, { 9, 67 }, { 16, 30 },
		{ 18, 32 }, { 18, 35 }, { 22, 29 }, { 24, 31 },
		{ 23, 38 }, { 18, 43 }, { 20, 41 }, { 11, 63 },
		{ 9, 59 }, { 9, 64 }, { -1, 94 }, { -2, 89 },
		{ -9, 108 },

		/* 227 -> 275 */
		{ -6, 76 }, { -2, 44 }, { 0, 45 }, { 0, 52 },
		{ -3, 64 }, { -2, 59 }, { -4, 70 }, { -4, 75 },
		{ -8, 82 }, { -17, 102 }, { -9, 77 }, { 3, 24 },
		{ 0, 42 }, { 0, 48 }, { 0, 55 }, { -6, 59 },
		{ -7, 71 }, { -12, 83 }, { -11, 87 }, { -30, 119 },
		{ 1, 58 }, { -3, 29 }, { -1, 36 }, { 1, 38 },
		{ 2, 43 }, { -6, 55 }, { 0, 58 }, { 0, 64 },
		{ -3, 74 }, { -10, 90 }, { 0, 70 }, { -4, 29 },
		{ 5, 31 }, { 7, 42 }, { 1, 59 }, { -2, 58 },
		{ -3, 72 }, { -3, 81 }, { -11, 97 }, { 0, 58 },
		{ 8, 5 }, { 10, 14 }, { 14, 18 }, { 13, 27 },
		{ 2, 40 }, { 0, 58 }, { -3, 70 }, { -6, 79 },
		{ -8, 85 },

		/* 276 special case, bypass used */
		{ 0, 0 },

		/* 277 -> 337 */
		{ -13, 106 }, { -16, 106 }, { -10, 87 }, { -21, 114 },
		{ -18, 110 }, { -14, 98 }, { -22, 110 }, { -21, 106 },
		{ -18, 103 }, { -21, 107 }, { -23, 108 }, { -26, 112 },
		{ -10, 96 }, { -12, 95 }, { -5, 91 }, { -9, 93 },
		{ -22, 94 }, { -5, 86 }, { 9, 67 }, { -4, 80 },
		{ -10, 85 }, { -1, 70 }, { 7, 60 }, { 9, 58 },
		{ 5, 61 }, { 12, 50 }, { 15, 50 }, { 18, 49 },
		{ 17, 54 }, { 10, 41 }, { 7, 46 }, { -1, 51 },
		{ 7, 49 }, { 8, 52 }, { 9, 41 }, { 6, 47 },
		{ 2, 55 }, { 13, 41 }, { 10, 44 }, { 6, 50 },
		{ 5, 53 }, { 13, 49 }, { 4, 63 }, { 6, 64 },
		{ -2, 69 }, { -2, 59 }, { 6, 70 }, { 10, 44 },
		{ 9, 31 }, { 12, 43 }, { 3, 53 }, { 14, 34 },
		{ 10, 38 }, { -3, 52 }, { 13, 40 }, { 17, 32 },
		{ 7, 44 }, { 7, 38 }, { 13, 50 }, { 10, 57 },
		{ 26, 43 },

		/* 338 -> 398 */
		{ 14, 11 }, { 11, 14 }, { 9, 11 }, { 18, 11 },
		{ 21, 9 }, { 23, -2 }, { 32, -15 }, { 32, -15 },
		{ 34, -21 }, { 39, -23 }, { 42, -33 }, { 41, -31 },
		{ 46, -28 }, { 38, -12 }, { 21, 29 }, { 45, -24 },
		{ 53, -45 }, { 48, -26 }, { 65, -43 }, { 43, -19 },
		{ 39, -10 }, { 30, 9 }, { 18, 26 }, { 20, 27 },
		{ 0, 57 }, { -14, 82 }, { -5, 75 }, { -19, 97 },
		{ -35, 125 }, { 27, 0 }, { 28, 0 }, { 31, -4 },
		{ 27, 6 }, { 34, 8 }, { 30, 10 }, { 24, 22 },
		{ 33, 19 }, { 22, 32 }, { 26, 31 }, { 21, 41 },
		{ 26, 44 }, { 23, 47 }, { 16, 65 }, { 14, 71 },
		{ 8, 60 }, { 6, 63 }, { 17, 65 }, { 21, 24 },
		{ 23, 20 }, { 26, 23 }, { 27, 32 }, { 28, 23 },
		{ 28, 24 }, { 23, 40 }, { 24, 32 }, { 28, 29 },
		{ 23, 42 }, { 19, 57 }, { 22, 53 }, { 22, 61 },
		{ 11, 86 },

		/* 399 -> 435 */
		{ 12, 40 }, { 11, 51 }, { 14, 59 },
		{ -4, 79 }, { -7, 71 }, { -5, 69 }, { -9, 70 },
		{ -8, 66 }, { -10, 68 }, { -19, 73 }, { -12, 69 },
		{ -16, 70 }, { -15, 67 }, { -20, 62 }, { -19, 70 },
		{ -16, 66 }, { -22, 65 }, { -20, 63 }, { 9, -2 },
		{ 26, -9 }, { 33, -9 }, { 39, -7 }, { 41, -2 },
		{ 45, 3 }, { 49, 9 }, { 45, 27 }, { 36, 59 },
		{ -6, 66 }, { -7, 35 }, { -7, 42 }, { -8, 45 },
		{ -5, 48 }, { -12, 56 }, { -6, 60 }, { -5, 62 },
		{ -8, 66 }, { -8, 76 },

		/* 436 -> 459 */
		{ -5, 85 }, { -6, 81 }, { -10, 77 }, { -7, 81 },
		{ -17, 80 }, { -18, 73 }, { -4, 74 }, { -10, 83 },
		{ -9, 71 }, { -9, 67 }, { -1, 61 }, { -8, 66 },
		{ -14, 66 }, { 0, 59 }, { 2, 59 }, { 21, -13 },
		{ 33, -14 }, { 39, -7 }, { 46, -2 }, { 51, 2 },
		{ 60, 6 }, { 61, 17 }, { 55, 34 }, { 42, 62 },
	},

	/* cabac_init_idc == 1 */
	{
		/* 0 -> 10 */
		{ 20, -15 }, { 2, 54 }, { 3, 74 }, { 20, -15 },
		{ 2, 54 }, { 3, 74 }, { -28, 127 }, { -23, 104 },
		{ -6, 53 }, { -1, 54 }, { 7, 51 },

		/* 11 -> 23 */
		{ 22, 25 }, { 34, 0 }, { 16, 0 }, { -2, 9 },
		{ 4, 41 }, { -29, 118 }, { 2, 65 }, { -6, 71 },
		{ -13, 79 }, { 5, 52 }, { 9, 50 }, { -3, 70 },
		{ 10, 54 },

		/* 24 -> 39 */
		{ 26, 34 }, { 19, 22 }, { 40, 0 }, { 57, 2 },
		{ 41, 36 }, { 26, 69 }, { -45, 127 }, { -15, 101 },
		{ -4, 76 }, { -6, 71 }, { -13, 79 }, { 5, 52 },
		{ 6, 69 }, { -13, 90 }, { 0, 52 }, { 8, 43 },

		/* 40 -> 53 */
		{ -2, 69 }, { -5, 82 }, { -10, 96 }, { 2, 59 },
		{ 2, 75 }, { -3, 87 }, { -3, 100 }, { 1, 56 },
		{ -3, 74 }, { -6, 85 }, { 0, 59 }, { -3, 81 },
		{ -7, 86 }, { -5, 95 },

		/* 54 -> 59 */
		{ -1, 66 }, { -1, 77 }, { 1, 70 }, { -2, 86 },
		{ -5, 72 }, { 0, 61 },

		/* 60 -> 69 */
		{ 0, 41 }, { 0, 63 }, { 0, 63 }, { 0, 63 },
		{ -9, 83 }, { 4, 86 }, { 0, 97 }, { -7, 72 },
		{ 13, 41 }, { 3, 62 },

		/* 70 -> 104 */
		{ 13, 15 }, { 7, 51 }, { 2, 80 }, { -39, 127 },
		{ -18, 91 }, { -17, 96 }, { -26, 81 }, { -35, 98 },
		{ -24, 102 }, { -23, 97 }, { -27, 119 }, { -24, 99 },
		{ -21, 110 }, { -18, 102 }, { -36, 127 }, { 0, 80 },
		{ -5, 89 }, { -7, 94 }, { -4, 92 }, { 0, 39 },
		{ 0, 65 }, { -15, 84 }, { -35, 127 }, { -2, 73 },
		{ -12, 104 }, { -9, 91 }, { -31, 127 }, { 3, 55 },
		{ 7, 56 }, { 7, 55 }, { 8, 61 }, { -3, 53 },
		{ 0, 68 }, { -7, 74 }, { -9, 88 },

		/* 105 -> 165 */
		{ -13, 103 }, { -13, 91 }, { -9, 89 }, { -14, 92 },
		{ -8, 76 }, { -12, 87 }, { -23, 110 }, { -24, 105 },
		{ -10, 78 }, { -20, 112 }, { -17, 99 }, { -78, 127 },
		{ -70, 127 }, { -50, 127 }, { -46, 127 }, { -4, 66 },
		{ -5, 78 }, { -4, 71 }, { -8, 72 }, { 2, 59 },
		{ -1, 55 }, { -7, 70 }, { -6, 75 }, { -8, 89 },
		{ -34, 119 }, { -3, 75 }, { 32, 20 }, { 30, 22 },
		{ -44, 127 }, { 0, 54 }, { -5, 61 }, { 0, 58 },
		{ -1, 60 }, { -3, 61 }, { -8, 67 }, { -25, 84 },
		{ -14, 74 }, { -5, 65 }, { 5, 52 }, { 2, 57 },
		{ 0, 61 }, { -9, 69 }, { -11, 70 }, { 18, 55 },
		{ -4, 71 }, { 0, 58 }, { 7, 61 }, { 9, 41 },
		{ 18, 25 }, { 9, 32 }, { 5, 43 }, { 9, 47 },
		{ 0, 44 }, { 0, 51 }, { 2, 46 }, { 19, 38 },
		{ -4, 66 }, { 15, 38 }, { 12, 42 }, { 9, 34 },
		{ 0, 89 },

		/* 166 -> 226 */
		{ 4, 45 }, { 10, 28 }, { 10, 31 }, { 33, -11 },
		{ 52, -43 }, { 18, 15 }, { 28, 0 }, { 35, -22 },
		{ 38, -25 }, { 34, 0 }, { 39, -18 }, { 32, -12 },
		{ 102, -94 }, { 0, 0 }, { 56, -15 }, { 33, -4 },
		{ 29, 10 }, { 37, -5 }, { 51, -29 }, { 39, -9 },
		{ 52, -34 }, { 69, -58 }, { 67, -63 }, { 44, -5 },
		{ 32, 7 }, { 55, -29 }, { 32, 1 }, { 0, 0 },
		{ 27, 36 }, { 33, -25 }, { 34, -30 }, { 36, -28 },
		{ 38, -28 }, { 38, -27 }, { 34, -18 }, { 35, -16 },
		{ 34, -14 }, { 32, -8 }, { 37, -6 }, { 35, 0 },
		{ 30, 10 }, { 28, 18 }, { 26, 25 }, { 29, 41 },
		{ 0, 75 }, { 2, 72 }, { 8, 77 }, { 14, 35 },
		{ 18, 31 }, { 17, 35 }, { 21, 30 }, { 17, 45 },
		{ 20, 42 }, { 18, 45 }, { 27, 26 }, { 16, 54 },
		{ 7, 66 }, { 16, 56 }, { 11, 73 }, { 10, 67 },
		{ -10, 116 },

		/* 227 -> 275 */
		{ -23, 112 }, { -15, 71 }, { -7, 61 }, { 0, 53 },
		{ -5, 66 }, { -11, 77 }, { -9, 80 }, { -9, 84 },
		{ -10, 87 }, { -34, 127 }, { -21, 101 }, { -3, 39 },
		{ -5, 53 }, { -7, 61 }, { -11, 75 }, { -15, 77 },
		{ -17, 91 }, { -25, 107 }, { -25, 111 }, { -28, 122 },
		{ -11, 76 }, { -10, 44 }, { -10, 52 }, { -10, 57 },
		{ -9, 58 }, { -16, 72 }, { -7, 69 }, { -4, 69 },
		{ -5, 74 }, { -9, 86 }, { 2, 66 }, { -9, 34 },
		{ 1, 32 }, { 11, 31 }, { 5, 52 }, { -2, 55 },
		{ -2, 67 }, { 0, 73 }, { -8, 89 }, { 3, 52 },
		{ 7, 4 }, { 10, 8 }, { 17, 8 }, { 16, 19 },
		{ 3, 37 }, { -1, 61 }, { -5, 73 }, { -1, 70 },
		{ -4, 78 },

		/* 276 special case, bypass used */
		{ 0, 0 },

		/* 277 -> 337 */
		{ -21, 126 }, { -23, 124 }, { -20, 110 }, { -26, 126 },
		{ -25, 124 }, { -17, 105 }, { -27, 121 }, { -27, 117 },
		{ -17, 102 }, { -26, 117 }, { -27, 116 }, { -33, 122 },
		{ -10, 95 }, { -14, 100 }, { -8, 95 }, { -17, 111 },
		{ -28, 114 }, { -6, 89 }, { -2, 80 }, { -4, 82 },
		{ -9, 85 }, { -8, 81 }, { -1, 72 }, { 5, 64 },
		{ 1, 67 }, { 9, 56 }, { 0, 69 }, { 1, 69 },
		{ 7, 69 }, { -7, 69 }, { -6, 67 }, { -16, 77 },
		{ -2, 64 }, { 2, 61 }, { -6, 67 }, { -3, 64 },
		{ 2, 57 }, { -3, 65 }, { -3, 66 }, { 0, 62 },
		{ 9, 51 }, { -1, 66 }, { -2, 71 }, { -2, 75 },
		{ -1, 70 }, { -9, 72 }, { 14, 60 }, { 16, 37 },
		{ 0, 47 }, { 18, 35 }, { 11, 37 }, { 12, 41 },
		{ 10, 41 }, { 2, 48 }, { 12, 41 }, { 13, 41 },
		{ 0, 59 }, { 3, 50 }, { 19, 40 }, { 3, 66 },
		{ 18, 50 },

		/* 338 -> 398 */
		{ 19, -6 }, { 18, -6 }, { 14, 0 }, { 26, -12 },
		{ 31, -16 }, { 33, -25 }, { 33, -22 }, { 37, -28 },
		{ 39, -30 }, { 42, -30 }, { 47, -42 }, { 45, -36 },
		{ 49, -34 }, { 41, -17 }, { 32, 9 }, { 69, -71 },
		{ 63, -63 }, { 66, -64 }, { 77, -74 }, { 54, -39 },
		{ 52, -35 }, { 41, -10 }, { 36, 0 }, { 40, -1 },
		{ 30, 14 }, { 28, 26 }, { 23, 37 }, { 12, 55 },
		{ 11, 65 }, { 37, -33 }, { 39, -36 }, { 40, -37 },
		{ 38, -30 }, { 46, -33 }, { 42, -30 }, { 40, -24 },
		{ 49, -29 }, { 38, -12 }, { 40, -10 }, { 38, -3 },
		{ 46, -5 }, { 31, 20 }, { 29, 30 }, { 25, 44 },
		{ 12, 48 }, { 11, 49 }, { 26, 45 }, { 22, 22 },
		{ 23, 22 }, { 27, 21 }, { 33, 20 }, { 26, 28 },
		{ 30, 24 }, { 27, 34 }, { 18, 42 }, { 25, 39 },
		{ 18, 50 }, { 12, 70 }, { 21, 54 }, { 14, 71 },
		{ 11, 83 },

		/* 399 -> 435 */
		{ 25, 32 }, { 21, 49 }, { 21, 54 },
		{ -5, 85 }, { -6, 81 }, { -10, 77 }, { -7, 81 },
		{ -17, 80 }, { -18, 73 }, { -4, 74 }, { -10, 83 },
		{ -9, 71 }, { -9, 67 }, { -1, 61 }, { -8, 66 },
		{ -14, 66 }, { 0, 59 }, { 2, 59 }, { 17, -10 },
		{ 32, -13 }, { 42, -9 }, { 49, -5 }, { 53, 0 },
		{ 64, 3 }, { 68, 10 }, { 66, 27 }, { 47, 57 },
		{ -5, 71 }, { 0, 24 }, { -1, 36 }, { -2, 42 },
		{ -2, 52 }, { -9, 57 }, { -6, 63 }, { -4, 65 },
		{ -4, 67 }, { -7, 82 },

		/* 436 -> 459 */
		{ -3, 81 }, { -3, 76 }, { -7, 72 }, { -6, 78 },
		{ -12, 72 }, { -14, 68 }, { -3, 70 }, { -6, 76 },
		{ -5, 66 }, { -5, 62 }, { 0, 57 }, { -4, 61 },
		{ -9, 60 }, { 1, 54 }, { 2, 58 }, { 17, -10 },
		{ 32, -13 }, { 42, -9 }, { 49, -5 }, { 53, 0 },
		{ 64, 3 }, { 68, 10 }, { 66, 27 }, { 47, 57 },
	},

	/* cabac_init_idc == 2 */
	{
		/* 0 -> 10 */
		{ 20, -15 }, { 2, 54 }, { 3, 74 }, { 20, -15 },
		{ 2, 54 }, { 3, 74 }, { -28, 127 }, { -23, 104 },
		{ -6, 53 }, { -1, 54 }, { 7, 51 },

		/* 11 -> 23 */
		{ 29, 16 }, { 25, 0 }, { 14, 0 }, { -10, 51 },
		{ -3, 62 }, { -27, 99 }, { 26, 16 }, { -4, 85 },
		{ -24, 102 }, { 5, 57 }, { 6, 57 }, { -17, 73 },
		{ 14, 57 },

		/* 24 -> 39 */
		{ 20, 40 }, { 20, 10 }, { 29, 0 }, { 54, 0 },
		{ 37, 42 }, { 12, 97 }, { -32, 127 }, { -22, 117 },
		{ -2, 74 }, { -4, 85 }, { -24, 102 }, { 5, 57 },
		{ -6, 93 }, { -14, 88 }, { -6, 44 }, { 4, 55 },

		/* 40 -> 53 */
		{ -11, 89 }, { -15, 103 }, { -21, 116 }, { 19, 57 },
		{ 20, 58 }, { 4, 84 }, { 6, 96 }, { 1, 63 },
		{ -5, 85 }, { -13, 106 }, { 5, 63 }, { 6, 75 },
		{ -3, 90 }, { -1, 101 },

		/* 54 -> 59 */
		{ 3, 55 }, { -4, 79 }, { -2, 75 }, { -12, 97 },
		{ -7, 50 }, { 1, 60 },

		/* 60 -> 69 */
		{ 0, 41 }, { 0, 63 }, { 0, 63 }, { 0, 63 },
		{ -9, 83 }, { 4, 86 }, { 0, 97 }, { -7, 72 },
		{ 13, 41 }, { 3, 62 },

		/* 70 -> 104 */
		{ 7, 34 }, { -9, 88 }, { -20, 127 }, { -36, 127 },
		{ -17, 91 }, { -14, 95 }, { -25, 84 }, { -25, 86 },
		{ -12, 89 }, { -17, 91 }, { -31, 127 }, { -14, 76 },
		{ -18, 103 }, { -13, 90 }, { -37, 127 }, { 11, 80 },
		{ 5, 76 }, { 2, 84 }, { 5, 78 }, { -6, 55 },
		{ 4, 61 }, { -14, 83 }, { -37, 127 }, { -5, 79 },
		{ -11, 104 }, { -11, 91 }, { -30, 127 }, { 0, 65 },
		{ -2, 79 }, { 0, 72 }, { -4, 92 }, { -6, 56 },
		{ 3, 68 }, { -8, 71 }, { -13, 98 },

		/* 105 -> 165 */
		{ -4, 86 }, { -12, 88 }, { -5, 82 }, { -3, 72 },
		{ -4, 67 }, { -8, 72 }, { -16, 89 }, { -9, 69 },
		{ -1, 59 }, { 5, 66 }, { 4, 57 }, { -4, 71 },
		{ -2, 71 }, { 2, 58 }, { -1, 74 }, { -4, 44 },
		{ -1, 69 }, { 0, 62 }, { -7, 51 }, { -4, 47 },
		{ -6, 42 }, { -3, 41 }, { -6, 53 }, { 8, 76 },
		{ -9, 78 }, { -11, 83 }, { 9, 52 }, { 0, 67 },
		{ -5, 90 }, { 1, 67 }, { -15, 72 }, { -5, 75 },
		{ -8, 80 }, { -21, 83 }, { -21, 64 }, { -13, 31 },
		{ -25, 64 }, { -29, 94 }, { 9, 75 }, { 17, 63 },
		{ -8, 74 }, { -5, 35 }, { -2, 27 }, { 13, 91 },
		{ 3, 65 }, { -7, 69 }, { 8, 77 }, { -10, 66 },
		{ 3, 62 }, { -3, 68 }, { -20, 81 }, { 0, 30 },
		{ 1, 7 }, { -3, 23 }, { -21, 74 }, { 16, 66 },
		{ -23, 124 }, { 17, 37 }, { 44, -18 }, { 50, -34 },
		{ -22, 127 },

		/* 166 -> 226 */
		{ 4, 39 }, { 0, 42 }, { 7, 34 }, { 11, 29 },
		{ 8, 31 }, { 6, 37 }, { 7, 42 }, { 3, 40 },
		{ 8, 33 }, { 13, 43 }, { 13, 36 }, { 4, 47 },
		{ 3, 55 }, { 2, 58 }, { 6, 60 }, { 8, 44 },
		{ 11, 44 }, { 14, 42 }, { 7, 48 }, { 4, 56 },
		{ 4, 52 }, { 13, 37 }, { 9, 49 }, { 19, 58 },
		{ 10, 48 }, { 12, 45 }, { 0, 69 }, { 20, 33 },
		{ 8, 63 }, { 35, -18 }, { 33, -25 }, { 28, -3 },
		{ 24, 10 }, { 27, 0 }, { 34, -14 }, { 52, -44 },
		{ 39, -24 }, { 19, 17 }, { 31, 25 }, { 36, 29 },
		{ 24, 33 }, { 34, 15 }, { 30, 20 }, { 22, 73 },
		{ 20, 34 }, { 19, 31 }, { 27, 44 }, { 19, 16 },
		{ 15, 36 }, { 15, 36 }, { 21, 28 }, { 25, 21 },
		{ 30, 20 }, { 31, 12 }, { 27, 16 }, { 24, 42 },
		{ 0, 93 }, { 14, 56 }, { 15, 57 }, { 26, 38 },
		{ -24, 127 },

		/* 227 -> 275 */
		{ -24, 115 }, { -22, 82 }, { -9, 62 }, { 0, 53 },
		{ 0, 59 }, { -14, 85 }, { -13, 89 }, { -13, 94 },
		{ -11, 92 }, { -29, 127 }, { -21, 100 }, { -14, 57 },
		{ -12, 67 }, { -11, 71 }, { -10, 77 }, { -21, 85 },
		{ -16, 88 }, { -23, 104 }, { -15, 98 }, { -37, 127 },
		{ -10, 82 }, { -8, 48 }, { -8, 61 }, { -8, 66 },
		{ -7, 70 }, { -14, 75 }, { -10, 79 }, { -9, 83 },
		{ -12, 92 }, { -18, 108 }, { -4, 79 }, { -22, 69 },
		{ -16, 75 }, { -2, 58 }, { 1, 58 }, { -13, 78 },
		{ -9, 83 }, { -4, 81 }, { -13, 99 }, { -13, 81 },
		{ -6, 38 }, { -13, 62 }, { -6, 58 }, { -2, 59 },
		{ -16, 73 }, { -10, 76 }, { -13, 86 }, { -9, 83 },
		{ -10, 87 },

		/* 276 special case, bypass used */
		{ 0, 0 },

		/* 277 -> 337 */
		{ -22, 127 }, { -25, 127 }, { -25, 120 }, { -27, 127 },
		{ -19, 114 }, { -23, 117 }, { -25, 118 }, { -26, 117 },
		{ -24, 113 }, { -28, 118 }, { -31, 120 }, { -37, 124 },
		{ -10, 94 }, { -15, 102 }, { -10, 99 }, { -13, 106 },
		{ -50, 127 }, { -5, 92 }, { 17, 57 }, { -5, 86 },
		{ -13, 94 }, { -12, 91 }, { -2, 77 }, { 0, 71 },
		{ -1, 73 }, { 4, 64 }, { -7, 81 }, { 5, 64 },
		{ 15, 57 }, { 1, 67 }, { 0, 68 }, { -10, 67 },
		{ 1, 68 }, { 0, 77 }, { 2, 64 }, { 0, 68 },
		{ -5, 78 }, { 7, 55 }, { 5, 59 }, { 2, 65 },
		{ 14, 54 }, { 15, 44 }, { 5, 60 }, { 2, 70 },
		{ -2, 76 }, { -18, 86 }, { 12, 70 }, { 5, 64 },
		{ -12, 70 }, { 11, 55 }, { 5, 56 }, { 0, 69 },
		{ 2, 65 }, { -6, 74 }, { 5, 54 }, { 7, 54 },
		{ -6, 76 }, { -11, 82 }, { -2, 77 }, { -2, 77 },
		{ 25, 42 },

		/* 338 -> 398 */
		{ 17, -13 }, { 16, -9 }, { 17, -12 }, { 27, -21 },
		{ 37, -30 }, { 41, -40 }, { 42, -41 }, { 48, -47 },
		{ 39, -32 }, { 46, -40 }, { 52, -51 }, { 46, -41 },
		{ 52, -39 }, { 43, -19 }, { 32, 11 }, { 61, -55 },
		{ 56, -46 }, { 62, -50 }, { 81, -67 }, { 45, -20 },
		{ 35, -2 }, { 28, 15 }, { 34, 1 }, { 39, 1 },
		{ 30, 17 }, { 20, 38 }, { 18, 45 }, { 15, 54 },
		{ 0, 79 }, { 36, -16 }, { 37, -14 }, { 37, -17 },
		{ 32, 1 }, { 34, 15 }, { 29, 15 }, { 24, 25 },
		{ 34, 22 }, { 31, 16 }, { 35, 18 }, { 31, 28 },
		{ 33, 41 }, { 36, 28 }, { 27, 47 }, { 21, 62 },
		{ 18, 31 }, { 19, 26 }, { 36, 24 }, { 24, 23 },
		{ 27, 16 }, { 24, 30 }, { 31, 29 }, { 22, 41 },
		{ 22, 42 }, { 16, 60 }, { 15, 52 }, { 14, 60 },
		{ 3, 78 }, { -16, 123 }, { 21, 53 }, { 22, 56 },
		{ 25, 61 },

		/* 399 -> 435 */
		{ 21, 33 }, { 19, 50 }, { 17, 61 },
		{ -3, 78 }, { -8, 74 }, { -9, 72 }, { -10, 72 },
		{ -18, 75 }, { -12, 71 }, { -11, 63 }, { -5, 70 },
		{ -17, 75 }, { -14, 72 }, { -16, 67 }, { -8, 53 },
		{ -14, 59 }, { -9, 52 }, { -11, 68 }, { 9, -2 },
		{ 30, -10 }, { 31, -4 }, { 33, -1 }, { 33, 7 },
		{ 31, 12 }, { 37, 23 }, { 31, 38 }, { 20, 64 },
		{ -9, 71 }, { -7, 37 }, { -8, 44 }, { -11, 49 },
		{ -10, 56 }, { -12, 59 }, { -8, 63 }, { -9, 67 },
		{ -6, 68 }, { -10, 79 },

		/* 436 -> 459 */
		{ -3, 78 }, { -8, 74 }, { -9, 72 }, { -10, 72 },
		{ -18, 75 }, { -12, 71 }, { -11, 63 }, { -5, 70 },
		{ -17, 75 }, { -14, 72 }, { -16, 67 }, { -8, 53 },
		{ -14, 59 }, { -9, 52 }, { -11, 68 }, { 9, -2 },
		{ 30, -10 }, { 31, -4 }, { 33, -1 }, { 33, 7 },
		{ 31, 12 }, { 37, 23 }, { 31, 38 }, { 20, 64 },
	}
};

#define CLIP3(v, min, max)  ((v) < (min) ? (min) : ((v) > (max) ? (max) : (v)))

static void rk3288_vpu_h264e_init_cabac_table(struct rockchip_vpu_ctx *ctx)
{
	u8 *table;

	const s32(*context)[460][2];
	s32 i, j, k, qp;

	for (k = 0; k < H264E_CABAC_IDC_NUM; k ++) {
		table = (u8 *)ctx->hw.h264e.cabac_tbl[k].cpu;
		for (qp = 0; qp < 52; qp++) { /* All QP values */
			for (j = 0; j < 2; j++) { /* Intra/Inter */

				context = (j == 0) ?
					&h264_context_init_intra :
					&h264_context_init[k];

				for (i = 0; i < 460; i++) {
					s32 m = (s32)(*context)[i][0];
					s32 n = (s32)(*context)[i][1];

					s32 pre_ctx_st =
						CLIP3(((m * qp) >> 4) + n, 1, 126);
					int idx = qp * 464 * 2 + j * 464 + i;

					if (pre_ctx_st <= 63)
						table[idx] =
							(u8)((63 - pre_ctx_st) << 1);
					else
						table[idx] =
							(u8)(((pre_ctx_st - 64) <<
							      1) | 1);
				}
			}
		}
	}
}

static inline unsigned int ref_luma_size(unsigned int w, unsigned int h)
{
	return round_up(w, MB_DIM) * round_up(h, MB_DIM);
}

static void write_ue(struct stream_s *fifo, u32 val,
		const char *name)
{
	u32 num_bits = 0;

	val++;
	while (val >> ++num_bits);

	if (num_bits > 12) {
		u32 tmp;

		tmp = num_bits - 1;

		if (tmp > 24) {
			tmp -= 24;
			stream_put_bits(fifo, 0, 24, name);
		}

		stream_put_bits(fifo, 0, tmp, name);

		if (num_bits > 24) {
			num_bits -= 24;
			stream_put_bits(fifo, val >> num_bits, 24, name);
			val = val >> num_bits;
		}

		stream_put_bits(fifo, val, num_bits, name);
	} else {
		stream_put_bits(fifo, val, 2 * num_bits - 1, name);
	}
}

static void write_se(struct stream_s *fifo, s32 val, const char *name)
{
	u32 tmp;

	if (val > 0)
		tmp = (u32)(2 * val - 1);
	else
		tmp = (u32)(-2 * val);

	write_ue(fifo, tmp, name);
}

static int rk3288_vpu_h264e_assumble_sps(struct rockchip_vpu_ctx *ctx)
{
	struct stream_s *sps = &ctx->run.h264e.sps;

	stream_buffer_reset(sps);

	stream_put_bits(sps, 0, 8, "start code");
	stream_put_bits(sps, 0, 8, "start code");
	stream_put_bits(sps, 0, 8, "start code");
	stream_put_bits(sps, 1, 8, "start code");

	stream_put_bits(sps, 0, 1, "forbidden_zero_bit");
	stream_put_bits(sps, 1, 2, "nal_ref_idc");
	stream_put_bits(sps, 7, 5, "nal_unit_type");

	stream_put_bits(sps, 77, 8, "profile_idc");
	stream_put_bits(sps, 0, 1, "constraint_set0_flag");
	stream_put_bits(sps, 0, 1, "constraint_set1_flag");
	stream_put_bits(sps, 0, 1, "constraint_set2_flag");
	stream_put_bits(sps, 1, 1, "constraint_set3_flag");

	stream_put_bits(sps, 0, 4, "reserved_zero_4bits");
	stream_put_bits(sps, 30, 8, "level_idc");

	write_ue(sps, 0, "seq_parameter_set_id");

	write_ue(sps, 16 - 4, "log2_max_frame_num_minus4");

	write_ue(sps, 2, "pic_order_cnt_type");

	write_ue(sps, 1, "num_ref_frames");

	stream_put_bits(sps, 0, 1, "gaps_in_frame_num_value_allowed_flag");

	write_ue(sps, MB_WIDTH(ctx->src_fmt.width) - 1,
			"pic_width_in_mbs_minus1");

	write_ue(sps, MB_HEIGHT(ctx->src_fmt.height) - 1,
			"pic_height_in_map_units_minus1");

	stream_put_bits(sps, 1, 1, "frame_mbs_only_flag");

	stream_put_bits(sps, 0, 1, "direct_8x8_inference_flag");

	stream_put_bits(sps, 0, 1, "frame_cropping_flag");

	stream_put_bits(sps, 0, 1, "vui_parameters_present_flag");

	stream_put_bits(sps, 1, 1, "rbsp_stop_one_bit");
	if (sps->buffered_bits > 0)
		stream_put_bits(sps, 0, 8 - sps->buffered_bits,
				"bsp_alignment_zero_bits");


	return 0;
}

static int rk3288_vpu_h264e_assumble_pps(struct rockchip_vpu_ctx *ctx)
{
	const struct rk3288_h264e_reg_params *params =
		(struct rk3288_h264e_reg_params *)ctx->run.h264e.reg_params;
	struct stream_s *pps = &ctx->run.h264e.pps;

	stream_buffer_reset(pps);

	stream_put_bits(pps, 0, 8, "start code");
	stream_put_bits(pps, 0, 8, "start code");
	stream_put_bits(pps, 0, 8, "start code");
	stream_put_bits(pps, 1, 8, "start code");

	stream_put_bits(pps, 0, 1, "forbidden_zero_bit");
	stream_put_bits(pps, 1, 2, "nal_ref_idc");
	stream_put_bits(pps, 8, 5, "nal_unit_type");

	write_ue(pps, 0, "pic_parameter_set_id");
	write_ue(pps, 0, "seq_parameter_set_id");

	stream_put_bits(pps, params->enable_cabac, 1,
			"entropy_coding_mode_flag");
	stream_put_bits(pps, 0, 1, "pic_order_present_flag");

	write_ue(pps, 0, "num_slice_groups_minus1");
	write_ue(pps, 0, "num_ref_idx_l0_active_minus1");
	write_ue(pps, 0, "num_ref_idx_l1_active_minus1");

	stream_put_bits(pps, 0, 1, "weighted_pred_flag");
	stream_put_bits(pps, 0, 2, "weighted_bipred_idc");

	write_se(pps, params->pic_init_qp - 26,
			"pic_init_qp_minus26");
	write_se(pps, 0, "pic_init_qs_minus26");
	write_se(pps, params->chroma_qp_index_offset,
			"chroma_qp_index_offset");

	stream_put_bits(pps, 1, 1,
			"deblocking_filter_control_present_flag");
	stream_put_bits(pps, 0, 1, "constrained_intra_pred_flag");

	stream_put_bits(pps, 0, 1, "redundant_pic_cnt_present_flag");

	if (params->transform8x8_mode) {
		stream_put_bits(pps, 1, 1, "transform_8x8_mode_flag");
		stream_put_bits(pps, 0, 1, "pic_scaling_matrix_present_flag");
		write_se(pps, params->chroma_qp_index_offset,
				"chroma_qp_index_offset");
	}
	stream_put_bits(pps, 1, 1, "rbsp_stop_one_bit");
	if (pps->buffered_bits > 0)
		stream_put_bits(pps, 0, 8 - pps->buffered_bits,
				"bsp_alignment_zero_bits");

	return 0;
}

int rk3288_vpu_h264e_init(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;
	size_t ref_buf_size;
	size_t height = ctx->src_fmt.height;
	size_t width = ctx->src_fmt.width;
	int ret;
	int i;

	vpu_debug_enter();

	for (i = 0; i < H264E_CABAC_IDC_NUM; i++) {
		ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->hw.h264e.cabac_tbl[i],
				52 * 2 * 464);
		if (ret) {
			vpu_err("allocate h264e cabac_tbl failed\n");
			goto err_cabac_tbl_alloc;
		}
	}

	ref_buf_size = ref_luma_size(width, height) * 3 / 2;
	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->hw.h264e.ext_buf,
			2 * ref_buf_size);
	if (ret) {
		vpu_err("allocate ext_buf failed\n");
		goto err_ext_buf_alloc;
	}

	if (0 > stream_buffer_init(&ctx->run.h264e.sps, NULL, 128))
		goto err_init_sps_buffer;

	if (0 > stream_buffer_init(&ctx->run.h264e.pps, NULL, 128))
		goto err_init_pps_buffer;

	rk3288_vpu_h264e_init_cabac_table(ctx);

	return ret;

err_init_pps_buffer:
	kfree(&ctx->run.h264e.sps.buffer);
err_init_sps_buffer:
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.h264e.ext_buf);
err_ext_buf_alloc:
	for (i = 0; i < H264E_CABAC_IDC_NUM; i++)
		if (ctx->hw.h264e.cabac_tbl[i].size != 0)
			rockchip_vpu_aux_buf_free(vpu,
						  &ctx->hw.h264e.cabac_tbl[i]);

err_cabac_tbl_alloc:
	vpu_debug_leave();

	return ret;
}

void rk3288_vpu_h264e_exit(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;
	int i;

	vpu_debug_enter();

	kfree(ctx->run.h264e.sps.buffer);
	kfree(ctx->run.h264e.pps.buffer);
	rockchip_vpu_aux_buf_free(vpu, &ctx->hw.h264e.ext_buf);

	for (i = 0; i < H264E_CABAC_IDC_NUM; i++)
		rockchip_vpu_aux_buf_free(vpu, &ctx->hw.h264e.cabac_tbl[i]);

	vpu_debug_leave();
}

static void rk3288_vpu_h264e_set_buffers(struct rockchip_vpu_dev *vpu,
		struct rockchip_vpu_ctx *ctx)
{
	const struct rk3288_h264e_reg_params *params =
		(struct rk3288_h264e_reg_params *)ctx->run.h264e.reg_params;
	dma_addr_t ref_buf_dma, rec_buf_dma;
	size_t rounded_size;
	dma_addr_t dst_dma;

	dma_addr_t cabac_dma =
		ctx->hw.h264e.cabac_tbl[params->cabac_init_idc].dma;

	dst_dma = vb2_dma_contig_plane_dma_addr(&ctx->run.dst->vb.vb2_buf, 0) +
		ctx->run.h264e.hw_write_offset;

	vepu_write_relaxed(vpu, cabac_dma, VEPU_REG_ADDR_CABAC_TBL);

	vepu_write_relaxed(vpu, dst_dma, VEPU_REG_ADDR_OUTPUT_STREAM);

	vepu_write_relaxed(vpu, 0, VEPU_REG_ADDR_OUTPUT_CTRL);

	rounded_size = ref_luma_size(ctx->src_fmt.width,
			ctx->src_fmt.height);
	ref_buf_dma = rec_buf_dma = ctx->hw.h264e.ext_buf.dma;
	if (ctx->hw.h264e.ref_rec_ptr)
		rec_buf_dma += rounded_size * 3 / 2;
	else
		ref_buf_dma += rounded_size * 3 / 2;
	ctx->hw.h264e.ref_rec_ptr ^= 1;

	vepu_write_relaxed(vpu, ref_buf_dma, VEPU_REG_ADDR_REF_LUMA);
	vepu_write_relaxed(vpu, ref_buf_dma + rounded_size,
			VEPU_REG_ADDR_REF_CHROMA);
	vepu_write_relaxed(vpu, rec_buf_dma, VEPU_REG_ADDR_REC_LUMA);
	vepu_write_relaxed(vpu, rec_buf_dma + rounded_size,
			VEPU_REG_ADDR_REC_CHROMA);

	vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(
				&ctx->run.src->vb.vb2_buf, PLANE_Y),
			VEPU_REG_ADDR_IN_LUMA);
	vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(
				&ctx->run.src->vb.vb2_buf, PLANE_CB),
			VEPU_REG_ADDR_IN_CB);
	vepu_write_relaxed(vpu, vb2_dma_contig_plane_dma_addr(
				&ctx->run.src->vb.vb2_buf, PLANE_CR),
			VEPU_REG_ADDR_IN_CR);

}

static s32 exp_golomb_signed(s32 val)
{
	s32 tmp = 0;

	if (val > 0)
		val = 2 * val;
	else
		val = -2 * val + 1;

	while (val >> ++tmp)
		;

	return tmp * 2 - 1;
}

static void rk3288_vpu_h264e_set_params(struct rockchip_vpu_dev *vpu,
		struct rockchip_vpu_ctx *ctx)
{
	const struct rk3288_h264e_reg_params *params =
		(struct rk3288_h264e_reg_params *)ctx->run.h264e.reg_params;
	struct v4l2_pix_format_mplane *pix_fmt = &ctx->src_fmt;
	s32 scaler, i;
	u32 reg;
	u32 prev_mode_favor = h264_prev_mode_favor[params->qp];

	u32 mbs_in_row = MB_WIDTH(ctx->dst_fmt.width);
	u32 mbs_in_col = MB_HEIGHT(ctx->dst_fmt.height);
	struct v4l2_rect *crop = &ctx->src_crop;
	u32 overfill_r, overfill_b, bytes_per_line;
	u32 first_free_bit = 0;
	u32 skip_penalty;

	u8 dmv_penalty[128];
	u8 dmv_qpel_penalty[128];
	u32 diff_mv_penalty[3];
	u32 split_penalty[4];

	/* If frame encode type for current frame is intra, write sps pps to
	   the output buffer */
	ctx->run.h264e.hw_write_offset = 0;
	if (params->frame_coding_type == 1) {
		ctx->run.h264e.hw_write_offset = ctx->run.h264e.sps.byte_cnt +
			ctx->run.h264e.pps.byte_cnt;
		first_free_bit = (ctx->run.h264e.hw_write_offset & 0x7) * 8;
		ctx->run.h264e.hw_write_offset &= ~0x7;
	}

	/*
	 * The hardware needs only the value for luma plane, because
	 * values of other planes are calculated internally based on
	 * format setting.
	 */
	bytes_per_line = pix_fmt->plane_fmt[0].bytesperline;
	overfill_r = (pix_fmt->width - crop->width) / 4;
	overfill_b = pix_fmt->height - crop->height;

	reg = VEPU_REG_IN_IMG_CTRL_ROW_LEN(bytes_per_line)
		| VEPU_REG_IN_IMG_CTRL_OVRFLR_D4(overfill_r)
		| VEPU_REG_IN_IMG_CTRL_OVRFLB_D4(overfill_b)
		| VEPU_REG_IN_IMG_CTRL_FMT(ctx->vpu_src_fmt->enc_fmt);
	vepu_write_relaxed(vpu, reg, VEPU_REG_IN_IMG_CTRL);

	reg = VEPU_REG_ENC_CTRL0_INIT_QP(params->pic_init_qp) |
		VEPU_REG_ENC_CTRL0_SLICE_ALPHA(params->slice_alpha_offset) |
		VEPU_REG_ENC_CTRL0_SLICE_BETA(params->slice_beta_offset) |
		VEPU_REG_ENC_CTRL0_CHROMA_QP_OFFSET(params->chroma_qp_index_offset) |
		VEPU_REG_ENC_CTRL0_FILTER_DIS(params->filter_disable) |
		VEPU_REG_ENC_CTRL0_IDR_PICID(params->idr_pic_id);
	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_CTRL0);

	reg = VEPU_REG_ENC_CTRL1_PPS_ID(params->pps_id) |
		VEPU_REG_ENC_CTRL1_INTRA_PRED_MODE(prev_mode_favor) |
		VEPU_REG_ENC_CTRL1_FRAME_NUM(params->frame_num);

	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_CTRL1);

	reg = VEPU_REG_ENC_CTRL2_H264_SLICE_SIZE(params->slice_size_mb_rows) |
		VEPU_REG_ENC_CTRL2_CABAC_INIT_IDC(params->cabac_init_idc) |
		VEPU_REG_ENC_CTRL2_INTRA16X16_MODE(h264_intra16_favor[params->qp]);

	if (params->h264_inter4x4_disabled)
		reg |= VEPU_REG_ENC_CTRL2_H264_INTER4X4_MODE;
	if (params->enable_cabac)
		reg |= VEPU_REG_ENC_CTRL2_ENTROPY_CODING_MODE;
	if (params->transform8x8_mode)
		reg |= VEPU_REG_ENC_CTRL2_TRANS8X8_MODE_EN;
	if (mbs_in_row * mbs_in_col > 3600)
		reg |= VEPU_REG_ENC_CTRL2_DISABLE_QUARTER_PIXMV;

	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_CTRL2);

	scaler = max(1U, 200 / (mbs_in_row + mbs_in_col));
	skip_penalty = min(255U, h264_skip_sad_penalty[params->qp] * scaler);

	diff_mv_penalty[0] = h264_diff_mv_penalty4p[params->qp];
	diff_mv_penalty[1] = h264_diff_mv_penalty[params->qp];
	diff_mv_penalty[2] = h264_diff_mv_penalty[params->qp];
	split_penalty[0] = 0;
	split_penalty[1] = 0;
	split_penalty[2] = 0;
	split_penalty[3] = 0;

	reg = VEPU_REG_ENC_CTRL3_MUTIMV_EN |
		VEPU_REG_ENC_CTRL3_MV_PENALTY_1_4P(split_penalty[2]) |
		VEPU_REG_ENC_CTRL3_MV_PENALTY_4P(diff_mv_penalty[0]) |
		VEPU_REG_ENC_CTRL3_MV_PENALTY_1P(diff_mv_penalty[1]);

	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_CTRL3);

	reg = VEPU_REG_MVC_CTRL_MV16X16_FAVOR(10);

	vepu_write_relaxed(vpu, reg, VEPU_REG_MVC_CTRL);

	vepu_write_relaxed(vpu, 0, VEPU_REG_ENC_CTRL4);

	reg = VEPU_REG_ENC_CTRL5_MACROBLOCK_PENALTY(skip_penalty) |
		VEPU_REG_ENC_CTRL5_INTER_MODE(h264_inter_favor[params->qp]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_CTRL5);

	vepu_write_relaxed(vpu, 0, VEPU_REG_STR_HDR_REM_MSB);
	vepu_write_relaxed(vpu, 0, VEPU_REG_STR_HDR_REM_LSB);
	vepu_write_relaxed(vpu, vb2_plane_size(&ctx->run.dst->vb.vb2_buf, 0) -
			ctx->run.h264e.hw_write_offset,
			VEPU_REG_STR_BUF_LIMIT);

	reg = VEPU_REG_MAD_CTRL_QP_ADJUST(params->mad_qp_delta) |
		VEPU_REG_MAD_CTRL_MAD_THREDHOLD(params->mad_threshold);

	vepu_write_relaxed(vpu, reg, VEPU_REG_MAD_CTRL);

	reg = VEPU_REG_QP_VAL_LUM(params->qp) |
		VEPU_REG_QP_VAL_MAX(params->qp_max) |
		VEPU_REG_QP_VAL_MIN(params->qp_min) |
		VEPU_REG_QP_VAL_CHECKPOINT_DISTAN(params->cp_distance_mbs);

	vepu_write_relaxed(vpu, reg, VEPU_REG_QP_VAL);

	reg = VEPU_REG_CHECKPOINT_CHECK1(params->cp_target[0])
		| VEPU_REG_CHECKPOINT_CHECK0(params->cp_target[1]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHECKPOINT(0));

	reg = VEPU_REG_CHECKPOINT_CHECK1(params->cp_target[2])
		| VEPU_REG_CHECKPOINT_CHECK0(params->cp_target[3]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHECKPOINT(1));

	reg = VEPU_REG_CHECKPOINT_CHECK1(params->cp_target[4])
		| VEPU_REG_CHECKPOINT_CHECK0(params->cp_target[5]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHECKPOINT(2));

	reg = VEPU_REG_CHECKPOINT_CHECK1(params->cp_target[6])
		| VEPU_REG_CHECKPOINT_CHECK0(params->cp_target[7]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHECKPOINT(3));

	reg = VEPU_REG_CHECKPOINT_CHECK1(params->cp_target[8])
		| VEPU_REG_CHECKPOINT_CHECK0(params->cp_target[9]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHECKPOINT(4));

	reg = VEPU_REG_CHKPT_WORD_ERR_CHK1(params->target_error[0])
		| VEPU_REG_CHKPT_WORD_ERR_CHK0(params->target_error[1]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHKPT_WORD_ERR(0));

	reg = VEPU_REG_CHKPT_WORD_ERR_CHK1(params->target_error[2])
		| VEPU_REG_CHKPT_WORD_ERR_CHK0(params->target_error[3]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHKPT_WORD_ERR(1));

	reg = VEPU_REG_CHKPT_WORD_ERR_CHK1(params->target_error[4])
		| VEPU_REG_CHKPT_WORD_ERR_CHK0(params->target_error[5]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHKPT_WORD_ERR(2));

	reg = VEPU_REG_CHKPT_DELTA_QP_CHK6(params->delta_qp[6])
		| VEPU_REG_CHKPT_DELTA_QP_CHK5(params->delta_qp[5])
		| VEPU_REG_CHKPT_DELTA_QP_CHK4(params->delta_qp[4])
		| VEPU_REG_CHKPT_DELTA_QP_CHK3(params->delta_qp[3])
		| VEPU_REG_CHKPT_DELTA_QP_CHK2(params->delta_qp[2])
		| VEPU_REG_CHKPT_DELTA_QP_CHK1(params->delta_qp[1])
		| VEPU_REG_CHKPT_DELTA_QP_CHK0(params->delta_qp[0]);
	vepu_write_relaxed(vpu, reg, VEPU_REG_CHKPT_DELTA_QP);

	reg = first_free_bit << VEPU_REG_RLC_CTRL_STR_OFFS_SHIFT;

	vepu_write_relaxed(vpu, reg, VEPU_REG_RLC_CTRL);

	vepu_write_relaxed(vpu, 0, VEPU_REG_ADDR_NEXT_PIC);
	vepu_write_relaxed(vpu, 0, VEPU_REG_STABLILIZATION_OUTPUT);

	vepu_write_relaxed(vpu, 0, VEPU_REG_ADDR_MV_OUT);

	vepu_write_relaxed(vpu, 0, VEPU_REG_RGB_YUV_COEFF(0));
	vepu_write_relaxed(vpu, 0, VEPU_REG_RGB_YUV_COEFF(1));
	//vepu_write_relaxed(vpu, 0, VEPU_REG_RGB_YUV_COEFF(2));
	vepu_write_relaxed(vpu, 0, VEPU_REG_RGB_MASK_MSB);

	vepu_write_relaxed(vpu, 0, VEPU_REG_INTRA_AREA_CTRL);

	vepu_write_relaxed(vpu, 0, VEPU_REG_FIRST_ROI_AREA);
	vepu_write_relaxed(vpu, 0, VEPU_REG_SECOND_ROI_AREA);

	for (i = 0; i < 128; i++) {
		dmv_penalty[i] = i;
		dmv_qpel_penalty[i] = min(255, exp_golomb_signed(i));
	}

	for (i = 0; i < 128; i += 4) {
		reg = VEPU_REG_DMV_4P_1P_PENALTY_BIT(dmv_penalty[i], 3);
		reg |= VEPU_REG_DMV_4P_1P_PENALTY_BIT(dmv_penalty[i + 1], 2);
		reg |= VEPU_REG_DMV_4P_1P_PENALTY_BIT(dmv_penalty[i + 2], 1);
		reg |= VEPU_REG_DMV_4P_1P_PENALTY_BIT(dmv_penalty[i + 3], 0);
		vepu_write_relaxed(vpu, reg, VEPU_REG_DMV_4P_1P_PENALTY(i / 4));

		reg = VEPU_REG_DMV_QPEL_PENALTY_BIT(
				dmv_qpel_penalty[i], 3);
		reg |= VEPU_REG_DMV_QPEL_PENALTY_BIT(
				dmv_qpel_penalty[i + 1], 2);
		reg |= VEPU_REG_DMV_QPEL_PENALTY_BIT(
				dmv_qpel_penalty[i + 2], 1);
		reg |= VEPU_REG_DMV_QPEL_PENALTY_BIT(
				dmv_qpel_penalty[i + 3], 0);
		vepu_write_relaxed(vpu, reg,
				VEPU_REG_DMV_QPEL_PENALTY(i / 4));
	}
}

void rk3288_vpu_h264e_run(struct rockchip_vpu_ctx *ctx)
{
	const struct rk3288_h264e_reg_params *params =
		(struct rk3288_h264e_reg_params *)ctx->run.h264e.reg_params;
	struct rockchip_vpu_dev *vpu = ctx->dev;
	u32 reg;
	u32 mbs_in_row = (ctx->dst_fmt.width + 15) / 16;
	u32 mbs_in_col = (ctx->dst_fmt.height + 15) / 16;

	vpu_debug_enter();

	if (params->frame_coding_type == 1) {
		rk3288_vpu_h264e_assumble_sps(ctx);
		rk3288_vpu_h264e_assumble_pps(ctx);
	}

	/*
	 * Program the hardware.
	 */
	rockchip_vpu_power_on(vpu);

	/* Select encode mode first. */
	vepu_write_relaxed(vpu, VEPU_REG_ENC_CTRL_ENC_MODE_H264,
			VEPU_REG_ENC_CTRL);

	rk3288_vpu_h264e_set_params(vpu, ctx);
	rk3288_vpu_h264e_set_buffers(vpu, ctx);

	/* Make sure that all registers are written at this point. */
	wmb();

	/* Set the watchdog. */
	schedule_delayed_work(&vpu->watchdog_work, msecs_to_jiffies(2000));

	/* Start the hardware. */
	reg = VEPU_REG_AXI_CTRL_OUTPUT_SWAP16
		| VEPU_REG_AXI_CTRL_INPUT_SWAP16
		| VEPU_REG_AXI_CTRL_BURST_LEN(16)
		| VEPU_REG_AXI_CTRL_GATE_BIT
		| VEPU_REG_AXI_CTRL_OUTPUT_SWAP32
		| VEPU_REG_AXI_CTRL_INPUT_SWAP32
		| VEPU_REG_AXI_CTRL_OUTPUT_SWAP8
		| VEPU_REG_AXI_CTRL_INPUT_SWAP8;
	vepu_write(vpu, reg, VEPU_REG_AXI_CTRL);

	vepu_write(vpu, 0, VEPU_REG_INTERRUPT);

	reg = VEPU_REG_ENC_CTRL_WIDTH(mbs_in_row)
		| VEPU_REG_ENC_CTRL_HEIGHT(mbs_in_col)
		| VEPU_REG_ENC_CTRL_ENC_MODE_H264
		| VEPU_REG_PIC_TYPE(params->frame_coding_type)
		| VEPU_REG_ENC_CTRL_EN_BIT;

	/*if (ctx->run.dst->b.v4l2_buf.flags & V4L2_BUF_FLAG_KEYFRAME)
	  reg |= VEPU_REG_ENC_CTRL_KEYFRAME_BIT;*/

	vepu_write_relaxed(vpu, reg, VEPU_REG_ENC_CTRL);

	vpu_debug_leave();
}

void rk3288_vpu_h264e_done(struct rockchip_vpu_ctx *ctx,
		enum vb2_buffer_state result)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;
	struct rockchip_vpu_h264e_feedback *feedback =
		(struct rockchip_vpu_h264e_feedback *)ctx->run.priv_dst.cpu;
	u32 i, reg = VEPU_REG_CHECKPOINT(0);
	u32 cpt_prev = 0, overflow = 0;

	vpu_debug_enter();

	feedback->qp_sum =
		VEPU_REG_MAD_CTRL_QP_SUM_DIV2(vepu_read(vpu, VEPU_REG_MAD_CTRL)) * 2;
	feedback->mad_count =
		VEPU_REG_MB_CNT_OUT(vepu_read(vpu, VEPU_REG_MB_CTRL));
	feedback->rlc_count =
		VEPU_REG_RLC_CTRL_RLC_SUM(vepu_read(vpu, VEPU_REG_RLC_CTRL)) * 4;

	for (i = 0; i < 10; i++) {
		u32 cpt = VEPU_REG_CHECKPOINT_RESULT(vepu_read(vpu, reg));

		if (cpt < cpt_prev)
			overflow += (1 << 21);
		feedback->cp[i] = cpt + overflow;
		reg += (i & 1);
	}

	if (ctx->run.h264e.hw_write_offset) {
		ctx->run.dst->h264e.sps_size =
			ctx->run.h264e.sps.byte_cnt;
		ctx->run.dst->h264e.pps_size =
			ctx->run.h264e.pps.byte_cnt;
		vpu_debug(1, "sps %d, pps %d\n",
				ctx->run.dst->h264e.sps_size,
				ctx->run.dst->h264e.pps_size);
	} else {
		ctx->run.dst->h264e.sps_size = 0;
		ctx->run.dst->h264e.pps_size = 0;
	}

	ctx->run.dst->h264e.slices_size =
		vepu_read(vpu, VEPU_REG_STR_BUF_LIMIT) / 8;

	rockchip_vpu_run_done(ctx, result);

	vpu_debug_leave();
}
