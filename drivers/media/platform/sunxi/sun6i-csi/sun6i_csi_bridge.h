/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_CSI_BRIDGE_H_
#define _SUN6I_CSI_BRIDGE_H_

#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define SUN6I_CSI_BRIDGE_NAME	"sun6i-csi-bridge"

enum sun6i_csi_bridge_pad {
	SUN6I_CSI_BRIDGE_PAD_SINK	= 0,
	SUN6I_CSI_BRIDGE_PAD_SOURCE	= 1,
	SUN6I_CSI_BRIDGE_PAD_COUNT	= 2,
};

struct sun6i_csi_device;

struct sun6i_csi_bridge_format {
	u32	mbus_code;
	u8	input_format;
	u8	input_yuv_seq;
	u8	input_yuv_seq_invert;
};

struct sun6i_csi_bridge_source {
	struct v4l2_subdev		*subdev;
	struct v4l2_fwnode_endpoint	endpoint;
	bool				expected;
};

struct sun6i_csi_bridge_async_subdev {
	struct v4l2_async_subdev	async_subdev;
	struct sun6i_csi_bridge_source	*source;
};

struct sun6i_csi_bridge {
	struct v4l2_subdev		subdev;
	struct v4l2_async_notifier	notifier;
	struct media_pad		pads[2];
	struct v4l2_mbus_framefmt	mbus_format;
	struct mutex			lock; /* Mbus format lock. */

	struct sun6i_csi_bridge_source	source_parallel;
	struct sun6i_csi_bridge_source	source_mipi_csi2;
};

/* Helpers */

void sun6i_csi_bridge_dimensions(struct sun6i_csi_device *csi_dev,
				 unsigned int *width, unsigned int *height);
void sun6i_csi_bridge_format(struct sun6i_csi_device *csi_dev,
			     u32 *mbus_code, u32 *field);

/* Format */

const struct sun6i_csi_bridge_format *
sun6i_csi_bridge_format_find(u32 mbus_code);

/* Bridge */

int sun6i_csi_bridge_setup(struct sun6i_csi_device *csi_dev);
void sun6i_csi_bridge_cleanup(struct sun6i_csi_device *csi_dev);

#endif
