/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020 Kévin L'hôpital <kevin.lhopital@bootlin.com>
 * Copyright 2020-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN8I_A83T_MIPI_CSI2_H_
#define _SUN8I_A83T_MIPI_CSI2_H_

#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define SUN8I_A83T_MIPI_CSI2_NAME	"sun8i-a83t-mipi-csi2"

enum sun8i_a83t_mipi_csi2_pad {
	SUN8I_A83T_MIPI_CSI2_PAD_SINK	= 0,
	SUN8I_A83T_MIPI_CSI2_PAD_SOURCE	= 1,
	SUN8I_A83T_MIPI_CSI2_PAD_COUNT	= 2,
};

struct sun8i_a83t_mipi_csi2_format {
	u32	mbus_code;
	u8	data_type;
	u32	bpp;
};

struct sun8i_a83t_mipi_csi2_bridge {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[SUN8I_A83T_MIPI_CSI2_PAD_COUNT];
	struct v4l2_fwnode_endpoint	endpoint;
	struct v4l2_async_notifier	notifier;
	struct v4l2_mbus_framefmt	mbus_format;
	struct mutex			lock; /* Mbus format lock. */

	struct v4l2_subdev		*source_subdev;
};

struct sun8i_a83t_mipi_csi2_device {
	struct device				*dev;

	struct regmap				*regmap;
	struct clk				*clock_mod;
	struct clk				*clock_mipi;
	struct clk				*clock_misc;
	struct reset_control			*reset;
	struct phy				*dphy;

	struct sun8i_a83t_mipi_csi2_bridge	bridge;
};

#endif
