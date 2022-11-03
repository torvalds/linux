/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#ifndef __SUN6I_CSI_H__
#define __SUN6I_CSI_H__

#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_csi_bridge.h"
#include "sun6i_csi_capture.h"

#define SUN6I_CSI_NAME		"sun6i-csi"
#define SUN6I_CSI_DESCRIPTION	"Allwinner A31 CSI Device"

enum sun6i_csi_port {
	SUN6I_CSI_PORT_PARALLEL		= 0,
};

struct sun6i_csi_buffer {
	struct vb2_v4l2_buffer		v4l2_buffer;
	struct list_head		list;
};

struct sun6i_csi_v4l2 {
	struct v4l2_device		v4l2_dev;
	struct media_device		media_dev;
};

struct sun6i_csi_device {
	struct device			*dev;

	struct sun6i_csi_v4l2		v4l2;
	struct sun6i_csi_bridge		bridge;
	struct sun6i_csi_capture	capture;

	struct regmap			*regmap;
	struct clk			*clock_mod;
	struct clk			*clock_ram;
	struct reset_control		*reset;
};

struct sun6i_csi_variant {
	unsigned long	clock_mod_rate;
};

/**
 * sun6i_csi_is_format_supported() - check if the format supported by csi
 * @csi_dev:	pointer to the csi device
 * @pixformat:	v4l2 pixel format (V4L2_PIX_FMT_*)
 * @mbus_code:	media bus format code (MEDIA_BUS_FMT_*)
 *
 * Return: true if format is supported, false otherwise.
 */
bool sun6i_csi_is_format_supported(struct sun6i_csi_device *csi_dev,
				   u32 pixformat, u32 mbus_code);

#endif /* __SUN6I_CSI_H__ */
