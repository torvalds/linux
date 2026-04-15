/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 *
 * Based on vs_dc_hw.h, which is:
 *   Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_CRTC_REGS_H_
#define _VS_CRTC_REGS_H_

#include <linux/bits.h>

#define VSDC_DISP_DITHER_CONFIG(n)		(0x1410 + 0x4 * (n))

#define VSDC_DISP_DITHER_TABLE_LOW(n)		(0x1420 + 0x4 * (n))
#define VSDC_DISP_DITHER_TABLE_LOW_DEFAULT	0x7B48F3C0

#define VSDC_DISP_DITHER_TABLE_HIGH(n)		(0x1428 + 0x4 * (n))
#define VSDC_DISP_DITHER_TABLE_HIGH_DEFAULT	0x596AD1E2

#define VSDC_DISP_HSIZE(n)			(0x1430 + 0x4 * (n))
#define VSDC_DISP_HSIZE_DISP_MASK		GENMASK(14, 0)
#define VSDC_DISP_HSIZE_DISP(v)			((v) << 0)
#define VSDC_DISP_HSIZE_TOTAL_MASK		GENMASK(30, 16)
#define VSDC_DISP_HSIZE_TOTAL(v)		((v) << 16)

#define VSDC_DISP_HSYNC(n)			(0x1438 + 0x4 * (n))
#define VSDC_DISP_HSYNC_START_MASK		GENMASK(14, 0)
#define VSDC_DISP_HSYNC_START(v)		((v) << 0)
#define VSDC_DISP_HSYNC_END_MASK		GENMASK(29, 15)
#define VSDC_DISP_HSYNC_END(v)			((v) << 15)
#define VSDC_DISP_HSYNC_EN			BIT(30)
#define VSDC_DISP_HSYNC_POL			BIT(31)

#define VSDC_DISP_VSIZE(n)			(0x1440 + 0x4 * (n))
#define VSDC_DISP_VSIZE_DISP_MASK		GENMASK(14, 0)
#define VSDC_DISP_VSIZE_DISP(v)			((v) << 0)
#define VSDC_DISP_VSIZE_TOTAL_MASK		GENMASK(30, 16)
#define VSDC_DISP_VSIZE_TOTAL(v)		((v) << 16)

#define VSDC_DISP_VSYNC(n)			(0x1448 + 0x4 * (n))
#define VSDC_DISP_VSYNC_START_MASK		GENMASK(14, 0)
#define VSDC_DISP_VSYNC_START(v)		((v) << 0)
#define VSDC_DISP_VSYNC_END_MASK		GENMASK(29, 15)
#define VSDC_DISP_VSYNC_END(v)			((v) << 15)
#define VSDC_DISP_VSYNC_EN			BIT(30)
#define VSDC_DISP_VSYNC_POL			BIT(31)

#define VSDC_DISP_CURRENT_LOCATION(n)		(0x1450 + 0x4 * (n))

#define VSDC_DISP_GAMMA_INDEX(n)		(0x1458 + 0x4 * (n))

#define VSDC_DISP_GAMMA_DATA(n)			(0x1460 + 0x4 * (n))

#define VSDC_DISP_IRQ_STA			0x147C

#define VSDC_DISP_IRQ_EN			0x1480

#endif /* _VS_CRTC_REGS_H_ */
