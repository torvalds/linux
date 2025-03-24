/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel MID SoC Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
 */
#ifndef ATOMISP_GMIN_PLATFORM_H_
#define ATOMISP_GMIN_PLATFORM_H_

#include "atomisp_platform.h"

int atomisp_register_i2c_module(struct v4l2_subdev *subdev,
				struct camera_sensor_platform_data *plat_data);
int atomisp_gmin_remove_subdev(struct v4l2_subdev *sd);
int gmin_get_var_int(struct device *dev, bool is_gmin,
		     const char *var, int def);
struct camera_sensor_platform_data *
gmin_camera_platform_data(
    struct v4l2_subdev *subdev,
    enum atomisp_input_format csi_format,
    enum atomisp_bayer_order csi_bayer);
#endif
