/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#ifndef _RKCIF_SDITF_H
#define _RKCIF_SDITF_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>
#include <linux/rk-camera-module.h>
#include "hw.h"

struct sditf_priv {
	struct device *dev;
	struct v4l2_subdev sd;
	struct media_pad pads;
	struct rkcif_device *cif_dev;
};

extern struct platform_driver rkcif_subdev_driver;

#endif
