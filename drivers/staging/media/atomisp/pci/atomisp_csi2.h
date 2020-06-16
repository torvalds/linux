/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#ifndef __ATOMISP_CSI2_H__
#define __ATOMISP_CSI2_H__

#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>

#define CSI2_PAD_SINK		0
#define CSI2_PAD_SOURCE		1
#define CSI2_PADS_NUM		2

#define CSI2_OUTPUT_ISP_SUBDEV	BIT(0)
#define CSI2_OUTPUT_MEMORY	BIT(1)

struct atomisp_device;
struct v4l2_device;
struct atomisp_sub_device;

struct atomisp_mipi_csi2_device {
	struct v4l2_subdev subdev;
	struct media_pad pads[CSI2_PADS_NUM];
	struct v4l2_mbus_framefmt formats[CSI2_PADS_NUM];

	struct v4l2_ctrl_handler ctrls;
	struct atomisp_device *isp;

	u32 output; /* output direction */
};

int atomisp_csi2_set_ffmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  unsigned int which, uint16_t pad,
			  struct v4l2_mbus_framefmt *ffmt);
int atomisp_mipi_csi2_init(struct atomisp_device *isp);
void atomisp_mipi_csi2_cleanup(struct atomisp_device *isp);
void atomisp_mipi_csi2_unregister_entities(
    struct atomisp_mipi_csi2_device *csi2);
int atomisp_mipi_csi2_register_entities(struct atomisp_mipi_csi2_device *csi2,
					struct v4l2_device *vdev);

void atomisp_csi2_configure(struct atomisp_sub_device *asd);

#endif /* __ATOMISP_CSI2_H__ */
