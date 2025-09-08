/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 */
#ifndef __ATOMISP_CSI2_H__
#define __ATOMISP_CSI2_H__

#include <linux/gpio/consumer.h>
#include <linux/property.h>

#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#include "../../include/linux/atomisp.h"

#define CSI2_PAD_SINK		0
#define CSI2_PAD_SOURCE		1
#define CSI2_PADS_NUM		2

struct v4l2_device;

struct atomisp_device;
struct atomisp_sub_device;

struct atomisp_mipi_csi2_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[CSI2_PADS_NUM];
	struct v4l2_mbus_framefmt formats[CSI2_PADS_NUM];

	struct v4l2_ctrl_handler ctrls;
	struct atomisp_device *isp;
};

int atomisp_csi2_set_ffmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  unsigned int which, uint16_t pad,
			  struct v4l2_mbus_framefmt *ffmt);
int atomisp_mipi_csi2_init(struct atomisp_device *isp);
void atomisp_mipi_csi2_cleanup(struct atomisp_device *isp);
void atomisp_mipi_csi2_unregister_entities(
    struct atomisp_mipi_csi2_device *csi2);
int atomisp_mipi_csi2_register_entities(struct atomisp_mipi_csi2_device *csi2,
					struct v4l2_device *vdev);
int atomisp_csi2_bridge_init(struct atomisp_device *isp);
int atomisp_csi2_bridge_parse_firmware(struct atomisp_device *isp);

void atomisp_csi2_configure(struct atomisp_sub_device *asd);

#endif /* __ATOMISP_CSI2_H__ */
