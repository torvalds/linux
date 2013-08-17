/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Register interface file for Exynos Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include "scaler.h"

/* Scaling coefficient value */
int sc_coef_8t[7][16][8] = {
	{
		/* 8:8  or zoom-in */
		{0, 0, 0, 0, 128, 0, 0, 0},
		{0, 1, -2, 7, 127, -6, 2, -1},
		{0, 1, -5, 16, 125, -12, 4, -1},
		{0, 2, -8, 25, 120, -15, 5, -1},
		{-1, 3, -10, 35, 114, -18, 6, -1},
		{-1, 4, -13, 46, 107, -20, 6, -1},
		{-1, 5, -16, 57, 99, -21, 7, -2},
		{-1, 5, -18, 68, 89, -20, 6, -1},
		{-1, 6, -20, 79, 79, -20, 6, -1},
		{-1, 6, -20, 89, 68, -18, 5, -1},
		{-2, 7, -21, 99, 57, -16, 5, -1},
		{-1, 6, -20, 107, 46, -13, 4, -1},
		{-1, 6, -18, 114, 35, -10, 3, -1},
		{-1, 5, -15, 120, 25, -8, 2, 0},
		{-1, 4, -12, 125, 16, -5, 1, 0},
		{-1, 2, -6, 127, 7, -2, 1, 0}
	}, {
		/* 8:7 Zoom-out */
		{0, 3, -8, 13,	111, 14, -8, 3},
		{-1, 3, -10, 21, 112, 7, -6, 2},
		{-1, 4, -12, 28, 110, 1, -4, 2},
		{-1, 4, -13, 36, 106, -3, -2, 1},
		{-1, 4, -15, 44, 103, -7, -1, 1},
		{-1, 4, -16, 53, 97, -11, 1, 1},
		{-1, 4, -16, 61, 91, -13, 2, 0},
		{-1, 4, -17, 69, 85, -15, 3, 0},
		{0, 3, -16, 77, 77, -16, 3, 0},
		{0, 3, -15, 85, 69, -17, 4, -1},
		{0, 2, -13, 91, 61, -16, 4, -1},
		{1, 1, -11, 97, 53, -16, 4, -1},
		{1, -1, -7, 103, 44, -15, 4, -1},
		{1, -2, -3, 106, 36, -13, 4, -1},
		{2, -4, 1, 110,	28, -12, 4, -1},
		{2, -6, 7, 112,	21, -10, 3, -1}
	}, {
		/* 8:6 Zoom-out */
		{0, 2, -11, 25, 96, 25, -11, 2},
		{0, 2, -12, 31, 96, 19, -10, 2},
		{0, 2, -12, 37, 94, 14, -9, 2},
		{0, 1, -12, 43, 92, 10, -8, 2},
		{0, 1, -12, 49, 90, 5, -7, 2},
		{1, 0, -12, 55, 86, 1, -5, 2},
		{1, -1, -11, 61, 82, -2, -4, 2},
		{1, -1, -9, 67, 77, -5, -3, 1},
		{1, -2, -7, 72, 72, -7, -2, 1},
		{1, -3, -5, 77, 67, -9, -1, 1},
		{2, -4, -2, 82, 61, -11, -1, 1},
		{2, -5, 1, 86, 55, -12, 0, 1},
		{2, -7, 5, 90, 49, -12, 1, 0},
		{2, -8, 10, 92, 43, -12, 1, 0},
		{2, -9, 14, 94, 37, -12, 2, 0},
		{2, -10, 19, 96, 31, -12, 2, 0}
	}, {
		/* 8:5 Zoom-out */
		{0, -1, -8, 33, 80, 33, -8, -1},
		{1, -2, -7, 37, 80, 28, -8, -1},
		{1, -2, -7, 41, 79, 24, -8, 0},
		{1, -3, -6, 46, 78, 20, -8, 0},
		{1, -3, -4, 50, 76, 16, -8, 0},
		{1, -4, -3, 54, 74, 13, -7, 0},
		{1, -5, -1, 58, 71, 10, -7, 1},
		{1, -5, 1, 62, 68, 6, -6, 1},
		{1, -6, 4, 65, 65, 4, -6, 1},
		{1, -6, 6, 68, 62, 1, -5, 1},
		{1, -7, 10, 71, 58, -1, -5, 1},
		{0, -7, 13, 74, 54, -3, -4, 1},
		{0, -8, 16, 76, 50, -4, -3, 1},
		{0, -8, 20, 78, 46, -6, -3, 1},
		{0, -8, 24, 79, 41, -7, -2, 1},
		{-1, -8, 28, 80, 37, -7, -2, 1}
	}, {
		/* 8:4 Zoom-out */
		{0, -3, 0, 35, 64, 35, 0, -3},
		{0, -3, 1, 38, 64, 32, -1, -3},
		{0, -3, 2, 41, 63, 29, -2, -2},
		{0, -4, 4, 43, 63, 27, -3, -2},
		{0, -4, 6, 46, 61, 24, -3, -2},
		{0, -4, 7, 49, 60, 21, -3, -2},
		{-1, -4, 9, 51, 59, 19, -4, -1},
		{-1, -4, 12, 53, 57, 16, -4, -1},
		{-1, -4, 14, 55, 55, 14, -4, -1},
		{-1, -4, 16, 57, 53, 12, -4, -1},
		{-1, -4, 19, 59, 51, 9, -4, -1},
		{-2, -3, 21, 60, 49, 7, -4, 0},
		{-2, -3, 24, 61, 46, 6, -4, 0},
		{-2, -3, 27, 63, 43, 4, -4, 0},
		{-2, -2, 29, 63, 41, 2, -3, 0},
		{-3, -1, 32, 64, 38, 1, -3, 0}
	}, {
		/* 8:3 Zoom-out */
		{0, -1, 8, 33, 48, 33, 8, -1},
		{-1, -1, 9, 35, 49, 31, 7, -1},
		{-1, -1, 10, 36, 49, 30, 6, -1},
		{-1, -1, 12, 38, 48, 28, 5, -1},
		{-1, 0, 13, 39, 48, 26, 4, -1},
		{-1, 0, 15, 41, 47, 24, 3, -1},
		{-1, 0, 16, 42, 47, 23, 2, -1},
		{-1, 1, 18, 43, 45, 21, 2, -1},
		{-1, 1, 19, 45, 45, 19, 1, -1},
		{-1, 2, 21, 45, 43, 18, 1, -1},
		{-1, 2, 23, 47, 42, 16, 0, -1},
		{-1, 3, 24, 47, 41, 15, 0, -1},
		{-1, 4, 26, 48, 39, 13, 0, -1},
		{-1, 5, 28, 48, 38, 12, -1, -1},
		{-1, 6, 30, 49, 36, 10, -1, -1},
		{-1, 7, 31, 49, 35, 9, -1, -1}
	},

	{	/* 8:2 Zoom-out */
		{0, 2, 13, 30, 38, 30, 13, 2},
		{0, 3, 14, 30, 38, 29, 12, 2},
		{0, 3, 15, 31, 38, 28, 11, 2},
		{0, 4, 16, 32, 38, 26, 10, 2},
		{0, 4, 17, 33, 37, 26, 10, 1},
		{0, 5, 18, 34, 37, 24, 9, 1},
		{0, 5, 19, 34, 37, 24, 8, 1},
		{1, 6, 20, 35, 36, 22, 7, 1},
		{1, 6, 21, 36, 36, 21, 6, 1},
		{1, 7, 22, 36, 35, 20, 6, 1},
		{1, 8, 24, 37, 34, 19, 5, 0},
		{1, 9, 24, 37, 34, 18, 5, 0},
		{1, 10, 26, 37, 33, 17, 4, 0},
		{2, 10, 26, 38, 32, 16, 4, 0},
		{2, 11, 28, 38, 31, 15, 3, 0},
		{2, 12, 29, 38, 30, 14, 3, 0}
	}
};

int sc_coef_4t[7][16][4] = {
	{
		/* 8:8  or zoom-in */
		{0, 0, 128, 0},
		{0, 5, 127, -4},
		{-1, 11, 124, -6},
		{-1, 19, 118, -8},
		{-2, 27, 111, -8},
		{-3, 37, 102, -8},
		{-4, 48, 92, -8},
		{-5, 59, 81, -7},
		{-6, 70, 70, -6},
		{-7, 81, 59, -5},
		{-8, 92, 48, -4},
		{-8, 102, 37, -3},
		{-8, 111, 27, -2},
		{-8, 118, 19, -1},
		{-6, 124, 11, -1},
		{-4, 127, 5, 0}
	}, {
		/* 8:7 Zoom-out  */
		{0, 8, 112, 8},
		{-1, 14, 111, 4},
		{-2, 20, 109, 1},
		{-2, 27, 105, -2},
		{-3, 34, 100, -3},
		{-3, 43, 93, -5},
		{-4, 51, 86, -5},
		{-4, 60, 77, -5},
		{-5, 69, 69, -5},
		{-5, 77, 60, -4},
		{-5, 86, 51, -4},
		{-5, 93, 43, -3},
		{-3, 100, 34, -3},
		{-2, 105, 27, -2},
		{1, 109, 20, -2},
		{4, 111, 14, -1}
	}, {
		/* 8:6 Zoom-out  */
		{0, 16, 96, 16},
		{-2, 21, 97, 12},
		{-2, 26, 96, 8},
		{-2, 32, 93, 5},
		{-2, 39, 89, 2},
		{-2, 46, 84, 0},
		{-3, 53, 79, -1},
		{-2, 59, 73, -2},
		{-2, 66, 66, -2},
		{-2, 73, 59, -2},
		{-1, 79, 53, -3},
		{0, 84, 46, -2},
		{2, 89, 39, -2},
		{5, 93, 32, -2},
		{8, 96, 26, -2},
		{12, 97, 21, -2}
	}, {
		/* 8:5 Zoom-out  */
		{0, 22, 84, 22},
		{-1, 26, 85, 18},
		{-1, 31, 84, 14},
		{-1, 36, 82, 11},
		{-1, 42, 79, 8},
		{-1, 47, 76, 6},
		{0, 52, 72, 4},
		{0, 58, 68, 2},
		{1, 63, 63, 1},
		{2, 68, 58, 0},
		{4, 72, 52, 0},
		{6, 76, 47, -1},
		{8, 79, 42, -1},
		{11, 82, 36, -1},
		{14, 84, 31, -1},
		{18, 85, 26, -1}
	}, {
		/* 8:4 Zoom-out  */
		{0, 26, 76, 26},
		{0, 30, 76, 22},
		{0, 34, 75, 19},
		{1, 38, 73, 16},
		{1, 43, 71, 13},
		{2, 47, 69, 10},
		{3, 51, 66, 8},
		{4, 55, 63, 6},
		{5, 59, 59, 5},
		{6, 63, 55, 4},
		{8, 66, 51, 3},
		{10, 69, 47, 2},
		{13, 71, 43, 1},
		{16, 73, 38, 1},
		{19, 75, 34, 0},
		{22, 76, 30, 0}
	}, {
		/* 8:3 Zoom-out */
		{0, 29, 70, 29},
		{2, 32, 68, 26},
		{2, 36, 67, 23},
		{3, 39, 66, 20},
		{3, 43, 65, 17},
		{4, 46, 63, 15},
		{5, 50, 61, 12},
		{7, 53, 58, 10},
		{8, 56, 56, 8},
		{10, 58, 53, 7},
		{12, 61, 50, 5},
		{15, 63, 46, 4},
		{17, 65, 43, 3},
		{20, 66, 39, 3},
		{23, 67, 36, 2},
		{26, 68, 32, 2}
	}, {
		/* 8:2 Zoom-out  */
		{0, 32, 64, 32},
		{3, 34, 63, 28},
		{4, 37, 62, 25},
		{4, 40, 62, 22},
		{5, 43, 61, 19},
		{6, 46, 59, 17},
		{7, 48, 58, 15},
		{9, 51, 55, 13},
		{11, 53, 53, 11},
		{13, 55, 51, 9},
		{15, 58, 48, 7},
		{17, 59, 46, 6},
		{19, 61, 43, 5},
		{22, 62, 40, 4},
		{25, 62, 37, 4},
		{28, 63, 34, 3}
	},
};

/* CSC(Color Space Conversion) coefficient value */
static struct sc_csc_tab sc_no_csc = {
	{ 0x200, 0x000, 0x000, 0x000, 0x200, 0x000, 0x000, 0x000, 0x200 },
};

static struct sc_csc_tab sc_y2r = {
	/* (0,1) 601 Narrow */
	{ 0x254, 0x000, 0x331, 0x254, 0xF38, 0xE60, 0x254, 0x409, 0x000 },
	/* (0,1) 601 Wide */
	{ 0x200, 0x000, 0x2BE, 0x200, 0xF54, 0xE9B, 0x200, 0x377, 0x000 },
	/* (0,1) 709 Narrow */
	{ 0x254, 0x000, 0x331, 0x254, 0xF38, 0xE60, 0x254, 0x409, 0x000 },
	/* (0,1) 709 Wide */
	{ 0x200, 0x000, 0x314, 0x200, 0xFA2, 0xF15, 0x200, 0x3A2, 0x000 },
};

static struct sc_csc_tab sc_r2y = {
	/* (1,0) 601 Narrow */
	{ 0x084, 0x102, 0x032, 0xFB4, 0xF6B, 0x0E1, 0x0E1, 0xF44, 0xFDC },
	/* (1,0) 601 Wide  */
	{ 0x099, 0x12D, 0x03A, 0xFA8, 0xF52, 0x106, 0x106, 0xF25, 0xFD6 },
	/* (1,0) 709 Narrow */
	{ 0x05E, 0x13A, 0x020, 0xFCC, 0xF53, 0x0E1, 0x0E1, 0xF34, 0xFEC },
	/* (1,0) 709 Wide */
	{ 0x06D, 0x16E, 0x025, 0xFC4, 0xF36, 0x106, 0x106, 0xF12, 0xFE8 },
};

static struct sc_csc_tab *sc_csc_list[] = {
	[0] = &sc_no_csc,
	[1] = &sc_y2r,
	[2] = &sc_r2y,
};

static struct sc_bl_op_val sc_bl_op_tbl[] = {
	/* Sc,	 Sa,	Dc,	Da */
	{ZERO,	 ZERO,	ZERO,	ZERO},		/* CLEAR */
	{ ONE,	 ONE,	ZERO,	ZERO},		/* SRC */
	{ZERO,	 ZERO,	ONE,	ONE},		/* DST */
	{ ONE,	 ONE,	INV_SA,	INV_SA},	/* SRC_OVER */
	{INV_DA, ONE,	ONE,	INV_SA},	/* DST_OVER */
	{DST_A,	 DST_A,	ZERO,	ZERO},		/* SRC_IN */
	{ZERO,	 ZERO,	SRC_A,	SRC_A},		/* DST_IN */
	{INV_DA, INV_DA, ZERO,	ZERO},		/* SRC_OUT */
	{ZERO,	 ZERO,	INV_SA,	INV_SA},	/* DST_OUT */
	{DST_A,	 ZERO,	INV_SA,	ONE},		/* SRC_ATOP */
	{INV_DA, ONE,	SRC_A,	ZERO},		/* DST_ATOP */
	{INV_DA, ONE,	INV_SA,	ONE},		/* XOR: need to WA */
	{INV_DA, ONE,	INV_SA,	INV_SA},	/* DARKEN */
	{INV_DA, ONE,	INV_SA,	INV_SA},	/* LIGHTEN */
	{INV_DA, ONE,	INV_SA,	INV_SA},	/* MULTIPLY */
	{ONE,	 ONE,	INV_SC,	INV_SA},	/* SCREEN */
	{ONE,	 ONE,	ONE,	ONE},		/* ADD */
};


int sc_hwset_src_image_format(struct sc_dev *sc, u32 pixelformat)
{
	unsigned long cfg = readl(sc->regs + SCALER_SRC_CFG);

	cfg &= ~SCALER_CFG_FMT_MASK;

	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB565:
		cfg |= SCALER_CFG_FMT_RGB565;
		break;
	case V4L2_PIX_FMT_RGB555X:
		cfg |= SCALER_CFG_FMT_ARGB1555;
		break;
	case V4L2_PIX_FMT_RGB444:
		cfg |= SCALER_CFG_FMT_ARGB4444;
		break;
	case V4L2_PIX_FMT_RGB32:
		cfg |= SCALER_CFG_FMT_ARGB8888;
		break;
	case V4L2_PIX_FMT_BGR32:
		cfg |= SCALER_CFG_FMT_ARGB8888;
		cfg |= SCALER_CFG_BYTE_SWAP;
		break;
	case V4L2_PIX_FMT_YUYV:
		cfg |= SCALER_CFG_FMT_YUYV;
		break;
	case V4L2_PIX_FMT_UYVY:
		cfg |= SCALER_CFG_FMT_UYVY;
		break;
	case V4L2_PIX_FMT_YVYU:
		cfg |= SCALER_CFG_FMT_YVYU;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
		cfg |= SCALER_CFG_FMT_YCBCR420_2P;
		break;
	case V4L2_PIX_FMT_NV12MT_16X16:
		cfg |= SCALER_CFG_TILE_EN;
		cfg |= SCALER_CFG_FMT_YCBCR420_2P;
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV21M:
		if (sc_ver_is_5a(sc)) {
			cfg |= SCALER_CFG_FMT_YCBCR420_2P;
			cfg |= SCALER_CFG_HWORD_SWAP;
		} else {
			cfg |= SCALER_CFG_FMT_YCRCB420_2P;
		}
		break;
	case V4L2_PIX_FMT_NV16:
		cfg |= SCALER_CFG_FMT_YCBCR422_2P;
		break;
	case V4L2_PIX_FMT_NV61:
		if (sc_ver_is_5a(sc)) {
			cfg |= SCALER_CFG_FMT_YCBCR422_2P;
			cfg |= SCALER_CFG_HWORD_SWAP;
		} else {
			cfg |= SCALER_CFG_FMT_YCRCB422_2P;
		}
		break;
	case V4L2_PIX_FMT_NV24:
		cfg |= SCALER_CFG_FMT_YCBCR444_2P;
		break;
	case V4L2_PIX_FMT_NV42:
		if (sc_ver_is_5a(sc)) {
			cfg |= SCALER_CFG_FMT_YCBCR444_2P;
			cfg |= SCALER_CFG_HWORD_SWAP;
		} else {
			cfg |= SCALER_CFG_FMT_YCRCB444_2P;
		}
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YVU420M:
		cfg |= SCALER_CFG_FMT_YCBCR420_3P;
		break;
	/* TODO: add L8A8 and L8 source format */
	default:
		dev_err(sc->dev, "invalid pixelformat type\n");
		return -EINVAL;
	}
	writel(cfg, sc->regs + SCALER_SRC_CFG);
	return 0;
}

int sc_hwset_dst_image_format(struct sc_dev *sc, u32 pixelformat)
{
	unsigned long cfg = readl(sc->regs + SCALER_DST_CFG);
	bool is_rgb = false;

	cfg &= ~SCALER_CFG_FMT_MASK;

	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB565:
		cfg |= SCALER_CFG_FMT_RGB565;
		is_rgb = true;
		break;
	case V4L2_PIX_FMT_RGB555X:
		cfg |= SCALER_CFG_FMT_ARGB1555;
		is_rgb = true;
		break;
	case V4L2_PIX_FMT_RGB444:
		cfg |= SCALER_CFG_FMT_ARGB4444;
		is_rgb = true;
		break;
	case V4L2_PIX_FMT_RGB32:
		cfg |= SCALER_CFG_FMT_ARGB8888;
		is_rgb = true;
		break;
	case V4L2_PIX_FMT_BGR32:
		cfg |= SCALER_CFG_FMT_ARGB8888;
		cfg |= SCALER_CFG_BYTE_SWAP;
		is_rgb = true;
		break;
	case V4L2_PIX_FMT_YUYV:
		cfg |= SCALER_CFG_FMT_YUYV;
		break;
	case V4L2_PIX_FMT_UYVY:
		cfg |= SCALER_CFG_FMT_UYVY;
		break;
	case V4L2_PIX_FMT_YVYU:
		cfg |= SCALER_CFG_FMT_YVYU;
		break;
	case V4L2_PIX_FMT_NV12:
		cfg |= SCALER_CFG_FMT_YCBCR420_2P;
		break;
	case V4L2_PIX_FMT_NV21:
		if (sc_ver_is_5a(sc)) {
			cfg |= SCALER_CFG_FMT_YCBCR420_2P;
			cfg |= SCALER_CFG_HWORD_SWAP;
		} else {
			cfg |= SCALER_CFG_FMT_YCRCB420_2P;
		}
		break;
	case V4L2_PIX_FMT_NV12M:
		cfg |= SCALER_CFG_FMT_YCBCR420_2P;
		break;
	case V4L2_PIX_FMT_NV21M:
		if (sc_ver_is_5a(sc)) {
			cfg |= SCALER_CFG_FMT_YCBCR420_2P;
			cfg |= SCALER_CFG_HWORD_SWAP;
		} else {
			cfg |= SCALER_CFG_FMT_YCRCB420_2P;
		}
		break;
	case V4L2_PIX_FMT_NV16:
		cfg |= SCALER_CFG_FMT_YCBCR422_2P;
		break;
	case V4L2_PIX_FMT_NV61:
		if (sc_ver_is_5a(sc)) {
			cfg |= SCALER_CFG_FMT_YCBCR422_2P;
			cfg |= SCALER_CFG_HWORD_SWAP;
		} else {
			cfg |= SCALER_CFG_FMT_YCRCB422_2P;
		}
		break;
	case V4L2_PIX_FMT_NV24:
		cfg |= SCALER_CFG_FMT_YCBCR444_2P;
		break;
	case V4L2_PIX_FMT_NV42:
		if (sc_ver_is_5a(sc)) {
			cfg |= SCALER_CFG_FMT_YCBCR444_2P;
			cfg |= SCALER_CFG_HWORD_SWAP;
		} else {
			cfg |= SCALER_CFG_FMT_YCRCB444_2P;
		}
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YVU420M:
		cfg |= SCALER_CFG_FMT_YCBCR420_3P;
		break;
	default:
		dev_err(sc->dev, "invalid pixelformat type\n");
		return -EINVAL;
	}
	writel(cfg, sc->regs + SCALER_DST_CFG);

	/*
	 * When output format is RGB,
	 * CSC_Y_OFFSET_DST_EN should be 0
	 * to avoid color distortion
	 */
	if (is_rgb) {
		cfg = readl(sc->regs + SCALER_CFG);
		cfg &= ~SCALER_CFG_CSC_Y_OFFSET_DST;
		writel(cfg, sc->regs + SCALER_CFG);
	}

	return 0;
}

void sc_hwset_pre_multi_format(struct sc_dev *sc)
{
	unsigned long cfg = readl(sc->regs + SCALER_SRC_CFG);

	if ((cfg & SCALER_CFG_FMT_MASK) == SCALER_CFG_FMT_ARGB8888) {
		cfg &= ~SCALER_CFG_FMT_MASK;
		cfg |= SCALER_CFG_FMT_P_ARGB8888;
		writel(cfg, sc->regs + SCALER_SRC_CFG);
	} else {
		dev_err(sc->dev, "only support src pre-multiplied argb888\n");
	}

	cfg = readl(sc->regs + SCALER_DST_CFG);
	if ((cfg & SCALER_CFG_FMT_MASK) == SCALER_CFG_FMT_ARGB8888) {
		cfg &= ~SCALER_CFG_FMT_MASK;
		cfg |= SCALER_CFG_FMT_P_ARGB8888;
		writel(cfg, sc->regs + SCALER_DST_CFG);
	} else {
		dev_err(sc->dev, "only support dst pre-multiplied argb888\n");
	}
}

void get_blend_value(unsigned int *cfg, u32 val, bool pre_multi)
{
	unsigned int tmp;

	*cfg &= ~(SCALER_SEL_INV_MASK | SCALER_SEL_MASK |
			SCALER_OP_SEL_INV_MASK | SCALER_OP_SEL_MASK);

	if (val == 0xff) {
		*cfg |= (1 << SCALER_SEL_INV_SHIFT);
	} else {
		if (pre_multi)
			*cfg |= (1 << SCALER_SEL_SHIFT);
		else
			*cfg |= (2 << SCALER_SEL_SHIFT);
		tmp = val & 0xf;
		*cfg |= (tmp << SCALER_OP_SEL_SHIFT);
	}

	if (val >= BL_INV_BIT_OFFSET)
		*cfg |= (1 << SCALER_OP_SEL_INV_SHIFT);
}

void sc_hwset_blend(struct sc_dev *sc, enum sc_blend_op bl_op, bool pre_multi)
{
	unsigned int cfg = readl(sc->regs + SCALER_CFG);
	int idx = bl_op - 1;

	cfg |= SCALER_CFG_BLEND_EN;
	writel(cfg, sc->regs + SCALER_CFG);

	cfg = readl(sc->regs + SCALER_SRC_BLEND_COLOR);
	get_blend_value(&cfg, sc_bl_op_tbl[idx].src_color, pre_multi);
	writel(cfg, sc->regs + SCALER_SRC_BLEND_COLOR);
	sc_dbg("src_blend_color is 0x%x, %d\n", cfg, pre_multi);

	cfg = readl(sc->regs + SCALER_SRC_BLEND_ALPHA);
	get_blend_value(&cfg, sc_bl_op_tbl[idx].src_alpha, 1);
	writel(cfg, sc->regs + SCALER_SRC_BLEND_ALPHA);
	sc_dbg("src_blend_alpha is 0x%x\n", cfg);

	cfg = readl(sc->regs + SCALER_DST_BLEND_COLOR);
	get_blend_value(&cfg, sc_bl_op_tbl[idx].dst_color, pre_multi);
	writel(cfg, sc->regs + SCALER_DST_BLEND_COLOR);
	sc_dbg("dst_blend_color is 0x%x\n", cfg);

	cfg = readl(sc->regs + SCALER_DST_BLEND_ALPHA);
	get_blend_value(&cfg, sc_bl_op_tbl[idx].dst_alpha, 1);
	writel(cfg, sc->regs + SCALER_DST_BLEND_ALPHA);
	sc_dbg("dst_blend_alpha is 0x%x\n", cfg);

	/*
	 * If dst format is non-premultiplied format
	 * and blending operation is enabled,
	 * result image should be divided by alpha value
	 * because the result is always pre-multiplied.
	 */
	if (!pre_multi) {
		cfg = readl(sc->regs + SCALER_CFG);
		cfg |= SCALER_CFG_BL_DIV_ALPHA_EN;
		writel(cfg, sc->regs + SCALER_CFG);
	}
}

void sc_hwset_color_fill(struct sc_dev *sc, unsigned int val)
{
	unsigned int cfg = readl(sc->regs + SCALER_CFG);

	cfg |= SCALER_CFG_FILL_EN;
	writel(cfg, sc->regs + SCALER_CFG);

	cfg = readl(sc->regs + SCALER_FILL_COLOR);
	cfg = val;
	writel(cfg, sc->regs + SCALER_FILL_COLOR);
	sc_dbg("color filled is 0x%08x\n", val);
}

void sc_hwset_dith(struct sc_dev *sc, unsigned int val)
{
	unsigned int cfg = readl(sc->regs + SCALER_DITH_CFG);

	cfg &= ~(SCALER_DITH_R_MASK | SCALER_DITH_G_MASK | SCALER_DITH_B_MASK);
	cfg |= val;
	writel(cfg, sc->regs + SCALER_DITH_CFG);
}

void sc_hwset_csc_coef(struct sc_dev *sc, enum sc_csc_idx idx,
			struct sc_csc *csc)
{
	unsigned int i, j, tmp;
	unsigned long cfg;
	int *csc_eq_val;

	if (idx == NO_CSC) {
		csc_eq_val = sc_csc_list[idx]->narrow_601;
	} else {
		if (csc->csc_eq == SC_CSC_601) {
			if (csc->csc_range == SC_CSC_NARROW)
				csc_eq_val = sc_csc_list[idx]->narrow_601;
			else
				csc_eq_val = sc_csc_list[idx]->wide_601;
		} else {
			if (csc->csc_range == SC_CSC_NARROW)
				csc_eq_val = sc_csc_list[idx]->narrow_709;
			else
				csc_eq_val = sc_csc_list[idx]->wide_709;
		}
	}

	tmp = SCALER_CSC_COEF22 - SCALER_CSC_COEF00;

	for (i = 0, j = 0; i < 9; i++, j += 4) {
		cfg = readl(sc->regs + SCALER_CSC_COEF00 + j);
		cfg &= ~SCALER_CSC_COEF_MASK;
		cfg |= csc_eq_val[i];
		writel(cfg, sc->regs + SCALER_CSC_COEF00 + j);
		sc_dbg("csc value %d - %d\n", i, csc_eq_val[i]);
	}

	/* set CSC_Y_OFFSET_EN */
	cfg = readl(sc->regs + SCALER_CFG);
	if (idx == CSC_Y2R) {
		if (csc->csc_range == SC_CSC_WIDE)
			cfg &= ~SCALER_CFG_CSC_Y_OFFSET_SRC;
		else
			cfg |= SCALER_CFG_CSC_Y_OFFSET_SRC;
	} else if (idx == CSC_R2Y) {
		if (csc->csc_range == SC_CSC_WIDE)
			cfg |= SCALER_CFG_CSC_Y_OFFSET_DST;
		else
			cfg &= ~SCALER_CFG_CSC_Y_OFFSET_DST;
	}
	writel(cfg, sc->regs + SCALER_CFG);
}

void sc_hwset_flip_rotation(struct sc_dev *sc, u32 direction, int degree)
{
	unsigned long cfg = readl(sc->regs + SCALER_ROT_CFG);

	sc_dbg("flip %d rotation %d\n", direction, degree);
	cfg &= ~SCALER_FLIP_MASK;
	if (direction & SC_VFLIP)
		cfg |= SCALER_FLIP_X_EN;
	if (direction & SC_HFLIP)
		cfg |= SCALER_FLIP_Y_EN;

	cfg &= ~SCALER_ROT_MASK;
	if (degree == 90)
		cfg |= SCALER_ROT_90;
	else if (degree == 180)
		cfg |= SCALER_ROT_180;
	else if (degree == 270)
		cfg |= SCALER_ROT_270;

	writel(cfg, sc->regs + SCALER_ROT_CFG);
}

void sc_hwset_hratio(struct sc_dev *sc, u32 ratio)
{
	unsigned long cfg = readl(sc->regs + SCALER_H_RATIO);

	sc_dbg("h-ratio is %d\n", ratio);
	cfg &= ~SCALER_RATIO_MASK;
	cfg |= ratio;
	writel(cfg, sc->regs + SCALER_H_RATIO);
}

void sc_hwset_vratio(struct sc_dev *sc, u32 ratio)
{
	unsigned long cfg = readl(sc->regs + SCALER_V_RATIO);

	sc_dbg("v-ratio is %d\n", ratio);
	cfg &= ~SCALER_RATIO_MASK;
	cfg |= ratio;
	writel(cfg, sc->regs + SCALER_V_RATIO);
}

void sc_hwset_hcoef(struct sc_dev *sc, int coef)
{
	unsigned int phase, tab, cnt = 0;
	unsigned int cfg, val_h, val_l;

	for (phase = 0; phase < 9; phase++) {
		for (tab = 8; tab > 0; tab -= 2, cnt++) {
			val_h = sc_coef_8t[coef][phase][tab - 1] & 0x1FF;
			val_l = sc_coef_8t[coef][phase][tab - 2] & 0x1FF;
			cfg = (val_h << 16) | (val_l << 0);
			writel(cfg, sc->regs + SCALER_YHCOEF + cnt * 0x4);
			writel(cfg, sc->regs + SCALER_CHCOEF + cnt * 0x4);
		}
	}
}

void sc_hwset_vcoef(struct sc_dev *sc, int coef)
{
	unsigned int phase, tab, cnt = 0;
	unsigned int cfg, val_h, val_l;

	for (phase = 0; phase < 9; phase++) {
		for (tab = 4; tab > 0; tab -= 2, cnt++) {
			val_h = sc_coef_4t[coef][phase][tab - 1] & 0x1FF;
			val_l = sc_coef_4t[coef][phase][tab - 2] & 0x1FF;
			cfg = (val_h << 16) | (val_l << 0);
			writel(cfg, sc->regs + SCALER_YVCOEF + cnt * 0x4);
			writel(cfg, sc->regs + SCALER_CVCOEF + cnt * 0x4);
		}
	}
}

void sc_hwset_src_imgsize(struct sc_dev *sc, struct sc_frame *frame)
{
	unsigned long cfg = 0;

	cfg &= ~(SCALER_SRC_CSPAN_MASK | SCALER_SRC_YSPAN_MASK);
	cfg |= frame->pix_mp.width;

	/*
	 * TODO: C width should be half of Y width
	 * but, how to get the diffferent c width from user
	 * like AYV12 format
	 */
	if (frame->sc_fmt->num_comp == 2)
		cfg |= frame->pix_mp.width << 16;
	if (frame->sc_fmt->num_comp == 3)
		cfg |= (frame->pix_mp.width >> 1) << 16;

	writel(cfg, sc->regs + SCALER_SRC_SPAN);
}

void sc_hwset_dst_imgsize(struct sc_dev *sc, struct sc_frame *frame)
{
	unsigned long cfg = 0;

	cfg &= ~(SCALER_DST_CSPAN_MASK | SCALER_DST_YSPAN_MASK);
	cfg |= frame->pix_mp.width;

	/*
	 * TODO: C width should be half of Y width
	 * but, how to get the diffferent c width from user
	 * like AYV12 format
	 */
	if (frame->sc_fmt->num_comp == 2)
		cfg |= frame->pix_mp.width << 16;
	if (frame->sc_fmt->num_comp == 3)
		cfg |= (frame->pix_mp.width >> 1) << 16;
	writel(cfg, sc->regs + SCALER_DST_SPAN);
}

void sc_hwset_src_crop(struct sc_dev *sc, struct v4l2_rect *rect)
{
	unsigned long cfg;

	cfg = SCALER_SRC_YX(rect->left) | SCALER_SRC_YY(rect->top);
	writel(cfg, sc->regs + SCALER_SRC_Y_POS);

	cfg = SCALER_SRC_W(rect->width) | SCALER_SRC_H(rect->height);
	writel(cfg, sc->regs + SCALER_SRC_WH);
}

void sc_hwset_dst_crop(struct sc_dev *sc, struct v4l2_rect *rect)
{
	unsigned long cfg;

	cfg = SCALER_DST_X(rect->left) | SCALER_DST_Y(rect->top);
	writel(cfg, sc->regs + SCALER_DST_POS);

	cfg = SCALER_DST_W(rect->width) | SCALER_DST_H(rect->height);
	writel(cfg, sc->regs + SCALER_DST_WH);
}

void sc_hwset_src_addr(struct sc_dev *sc, struct sc_addr *addr)
{
	writel(addr->y, sc->regs + SCALER_SRC_Y_BASE);
	writel(addr->cb, sc->regs + SCALER_SRC_CB_BASE);
	if (!sc_ver_is_5a(sc))
		writel(addr->cr, sc->regs + SCALER_SRC_CR_BASE);
}

void sc_hwset_dst_addr(struct sc_dev *sc, struct sc_addr *addr)
{
	writel(addr->y, sc->regs + SCALER_DST_Y_BASE);
	writel(addr->cb, sc->regs + SCALER_DST_CB_BASE);
	if (!sc_ver_is_5a(sc))
		writel(addr->cr, sc->regs + SCALER_DST_CR_BASE);
}

void sc_hwset_int_en(struct sc_dev *sc, u32 enable)
{
	unsigned long cfg = readl(sc->regs + SCALER_INT_EN);
	int val;

	val = sc_ver_is_5a(sc) ? \
	      SCALER_INT_EN_FRAME_END : SCALER_INT_EN_ALL;

	if (enable)
		cfg |= val;
	else
		cfg &= ~val;

	writel(cfg, sc->regs + SCALER_INT_EN);
}

int sc_hwget_int_status(struct sc_dev *sc)
{
	unsigned long cfg = readl(sc->regs + SCALER_INT_STATUS);
	return cfg;
}

void sc_hwset_int_clear(struct sc_dev *sc)
{
	unsigned long cfg = readl(sc->regs + SCALER_INT_STATUS);

	writel(cfg, sc->regs + SCALER_INT_STATUS);
}

int sc_hwget_version(struct sc_dev *sc)
{
	unsigned long cfg = readl(sc->regs + SCALER_VER);

	sc_dbg("This scaler version is 0x%x\n", (unsigned int)cfg);
	return cfg & 0xffff;
}

void sc_hwset_soft_reset(struct sc_dev *sc)
{
	unsigned long cfg = readl(sc->regs + SCALER_CFG);

	cfg |= SCALER_CFG_SOFT_RST;
	cfg &= ~((1 << 10) | (1 << 9));
	writel(cfg, sc->regs + SCALER_CFG);
	sc_dbg("done soft reset\n");

	/*
	 * TODO: check the reset completion
	 * 1. wait until it becomes '0'
	 * 2. backup the SCALER_INT_EN value
	 * 3. write 0x1 to SCALER_INT_EN and re-read
	 * 4. if re-read is 0x1, successfully complete
	 * 5. restore the SCALER_INT_EN register
	 */
}

void sc_hwset_start(struct sc_dev *sc)
{
	unsigned long cfg = readl(sc->regs + SCALER_CFG);

	cfg |= SCALER_CFG_START_CMD;
	writel(cfg, sc->regs + SCALER_CFG);
}
