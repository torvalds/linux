/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN6I_ISP_PROC_H_
#define _SUN6I_ISP_PROC_H_

#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define SUN6I_ISP_PROC_NAME		"sun6i-isp-proc"

enum sun6i_isp_proc_pad {
	SUN6I_ISP_PROC_PAD_SINK_CSI	= 0,
	SUN6I_ISP_PROC_PAD_SINK_PARAMS	= 1,
	SUN6I_ISP_PROC_PAD_SOURCE	= 2,
	SUN6I_ISP_PROC_PAD_COUNT	= 3,
};

struct sun6i_isp_device;

struct sun6i_isp_proc_format {
	u32	mbus_code;
	u8	input_format;
	u8	input_yuv_seq;
};

struct sun6i_isp_proc_source {
	struct v4l2_subdev		*subdev;
	struct v4l2_fwnode_endpoint	endpoint;
	bool				expected;
};

struct sun6i_isp_proc_async_subdev {
	struct v4l2_async_connection	async_subdev;
	struct sun6i_isp_proc_source	*source;
};

struct sun6i_isp_proc {
	struct v4l2_subdev		subdev;
	struct media_pad		pads[3];
	struct v4l2_async_notifier	notifier;
	struct v4l2_mbus_framefmt	mbus_format;
	struct mutex			lock; /* Mbus format lock. */

	struct sun6i_isp_proc_source	source_csi0;
	struct sun6i_isp_proc_source	source_csi1;
};

/* Helpers */

void sun6i_isp_proc_dimensions(struct sun6i_isp_device *isp_dev,
			       unsigned int *width, unsigned int *height);

/* Format */

const struct sun6i_isp_proc_format *sun6i_isp_proc_format_find(u32 mbus_code);

/* Proc */

int sun6i_isp_proc_setup(struct sun6i_isp_device *isp_dev);
void sun6i_isp_proc_cleanup(struct sun6i_isp_device *isp_dev);

#endif
