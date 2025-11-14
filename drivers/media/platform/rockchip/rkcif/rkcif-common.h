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

enum rkcif_format_type {
	RKCIF_FMT_TYPE_INVALID,
	RKCIF_FMT_TYPE_YUV,
	RKCIF_FMT_TYPE_RAW,
};

enum rkcif_interface_index {
	RKCIF_DVP,
	RKCIF_MIPI_BASE,
	RKCIF_MIPI1 = RKCIF_MIPI_BASE,
	RKCIF_MIPI2,
	RKCIF_MIPI3,
	RKCIF_MIPI4,
	RKCIF_MIPI5,
	RKCIF_MIPI6,
	RKCIF_MIPI_MAX,
	RKCIF_IF_MAX = RKCIF_MIPI_MAX
};

enum rkcif_interface_pad_index {
	RKCIF_IF_PAD_SINK,
	RKCIF_IF_PAD_SRC,
	RKCIF_IF_PAD_MAX
};

enum rkcif_interface_status {
	RKCIF_IF_INACTIVE,
	RKCIF_IF_ACTIVE,
};

enum rkcif_interface_type {
	RKCIF_IF_INVALID,
	RKCIF_IF_DVP,
	RKCIF_IF_MIPI,
};

struct rkcif_input_fmt {
	u32 mbus_code;

	enum rkcif_format_type fmt_type;
	enum v4l2_field field;
};

struct rkcif_interface;

struct rkcif_remote {
	struct v4l2_async_connection async_conn;
	struct v4l2_subdev *sd;

	struct rkcif_interface *interface;
};

struct rkcif_dvp {
	u32 dvp_clk_delay;
};

struct rkcif_interface {
	enum rkcif_interface_type type;
	enum rkcif_interface_status status;
	enum rkcif_interface_index index;
	struct rkcif_device *rkcif;
	struct rkcif_remote *remote;
	const struct rkcif_input_fmt *in_fmts;
	unsigned int in_fmts_num;

	struct media_pad pads[RKCIF_IF_PAD_MAX];
	struct v4l2_fwnode_endpoint vep;
	struct v4l2_subdev sd;

	union {
		struct rkcif_dvp dvp;
	};
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

	struct rkcif_interface interfaces[RKCIF_IF_MAX];

	struct media_device media_dev;
	struct v4l2_device v4l2_dev;
	struct v4l2_async_notifier notifier;
};

#endif
