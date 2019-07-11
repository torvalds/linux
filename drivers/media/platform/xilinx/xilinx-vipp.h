// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Video IP Composite Device
 *
 * Copyright (C) 2013-2015 Ideas on Board
 * Copyright (C) 2013-2015 Xilinx, Inc.
 *
 * Contacts: Hyun Kwon <hyun.kwon@xilinx.com>
 *           Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#ifndef __XILINX_VIPP_H__
#define __XILINX_VIPP_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

/**
 * struct xvip_composite_device - Xilinx Video IP device structure
 * @v4l2_dev: V4L2 device
 * @media_dev: media device
 * @dev: (OF) device
 * @notifier: V4L2 asynchronous subdevs notifier
 * @dmas: list of DMA channels at the pipeline output and input
 * @v4l2_caps: V4L2 capabilities of the whole device (see VIDIOC_QUERYCAP)
 */
struct xvip_composite_device {
	struct v4l2_device v4l2_dev;
	struct media_device media_dev;
	struct device *dev;

	struct v4l2_async_notifier notifier;

	struct list_head dmas;
	u32 v4l2_caps;
};

#endif /* __XILINX_VIPP_H__ */
