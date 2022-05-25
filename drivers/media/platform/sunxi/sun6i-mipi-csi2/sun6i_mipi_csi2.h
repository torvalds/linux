/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_MIPI_CSI2_H_
#define _SUN6I_MIPI_CSI2_H_

#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define SUN6I_MIPI_CSI2_NAME	"sun6i-mipi-csi2"

enum sun6i_mipi_csi2_pad {
	SUN6I_MIPI_CSI2_PAD_SINK	= 0,
	SUN6I_MIPI_CSI2_PAD_SOURCE	= 1,
	SUN6I_MIPI_CSI2_PAD_COUNT	= 2,
};

struct sun6i_mipi_csi2_format {
	u32	mbus_code;
	u8	data_type;
	u32	bpp;
};

struct sun6i_mipi_csi2_bridge {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[SUN6I_MIPI_CSI2_PAD_COUNT];
	struct v4l2_fwnode_endpoint	endpoint;
	struct v4l2_async_notifier	notifier;
	struct v4l2_mbus_framefmt	mbus_format;
	struct mutex			lock; /* Mbus format lock. */

	struct v4l2_subdev		*source_subdev;
};

struct sun6i_mipi_csi2_device {
	struct device			*dev;

	struct regmap			*regmap;
	struct clk			*clock_mod;
	struct reset_control		*reset;
	struct phy			*dphy;

	struct sun6i_mipi_csi2_bridge	bridge;
};

#endif
