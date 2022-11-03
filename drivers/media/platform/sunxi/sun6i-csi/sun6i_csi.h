/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * Author: Yong Deng <yong.deng@magewell.com>
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_CSI_H_
#define _SUN6I_CSI_H_

#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_csi_bridge.h"
#include "sun6i_csi_capture.h"

#define SUN6I_CSI_NAME		"sun6i-csi"
#define SUN6I_CSI_DESCRIPTION	"Allwinner A31 CSI Device"

enum sun6i_csi_port {
	SUN6I_CSI_PORT_PARALLEL		= 0,
	SUN6I_CSI_PORT_MIPI_CSI2	= 1,
	SUN6I_CSI_PORT_ISP		= 2,
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
	struct v4l2_device		*v4l2_dev;
	struct media_device		*media_dev;

	struct sun6i_csi_v4l2		v4l2;
	struct sun6i_csi_bridge		bridge;
	struct sun6i_csi_capture	capture;

	struct regmap			*regmap;
	struct clk			*clock_mod;
	struct clk			*clock_ram;
	struct reset_control		*reset;

	bool				isp_available;
};

struct sun6i_csi_variant {
	unsigned long	clock_mod_rate;
};

/* ISP */

int sun6i_csi_isp_complete(struct sun6i_csi_device *csi_dev,
			   struct v4l2_device *v4l2_dev);

#endif
