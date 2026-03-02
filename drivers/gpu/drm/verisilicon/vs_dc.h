/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 *
 * Based on vs_dc_hw.h, which is:
 *   Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_DC_H_
#define _VS_DC_H_

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_device.h>

#include "vs_hwdb.h"

#define VSDC_MAX_OUTPUTS 2
#define VSDC_RESET_COUNT 3

struct vs_drm_dev;
struct vs_crtc;

struct vs_dc {
	struct regmap *regs;
	struct clk *core_clk;
	struct clk *axi_clk;
	struct clk *ahb_clk;
	struct clk *pix_clk[VSDC_MAX_OUTPUTS];
	struct reset_control_bulk_data rsts[VSDC_RESET_COUNT];

	struct vs_drm_dev *drm_dev;
	struct vs_chip_identity identity;
};

#endif /* _VS_DC_H_ */
