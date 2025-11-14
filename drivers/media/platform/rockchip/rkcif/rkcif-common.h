/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 * Copyright (C) 2023 Mehdi Djait <mehdi.djait@bootlin.com>
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#ifndef _RKCIF_COMMON_H
#define _RKCIF_COMMON_H

#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>

#define RKCIF_DRIVER_NAME "rockchip-cif"
#define RKCIF_CLK_MAX	  4

struct rkcif_remote {
	struct v4l2_async_connection async_conn;
	struct v4l2_subdev *sd;
};

struct rkcif_match_data {
	const char *const *clks;
	unsigned int clks_num;
};

struct rkcif_device {
	struct device *dev;

	const struct rkcif_match_data *match_data;
	struct clk_bulk_data clks[RKCIF_CLK_MAX];
	unsigned int clks_num;
	struct regmap *grf;
	struct reset_control *reset;
	void __iomem *base_addr;

	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
};

#endif
