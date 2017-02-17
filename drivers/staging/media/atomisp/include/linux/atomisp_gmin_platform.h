/*
 * Support for Intel MID SoC Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef ATOMISP_GMIN_PLATFORM_H_
#define ATOMISP_GMIN_PLATFORM_H_

#include <linux/atomisp_platform.h>

const struct atomisp_camera_caps *atomisp_get_default_camera_caps(void);
const struct atomisp_platform_data *atomisp_get_platform_data(void);
const struct camera_af_platform_data *camera_get_af_platform_data(void);
int atomisp_register_i2c_module(struct v4l2_subdev *subdev,
                                struct camera_sensor_platform_data *plat_data,
                                enum intel_v4l2_subdev_type type);
struct v4l2_subdev *atomisp_gmin_find_subdev(struct i2c_adapter *adapter,
					     struct i2c_board_info *board_info);
int atomisp_gmin_remove_subdev(struct v4l2_subdev *sd);
int gmin_get_config_var(struct device *dev, const char *var, char *out, size_t *out_len);
int gmin_get_var_int(struct device *dev, const char *var, int def);
int camera_sensor_csi(struct v4l2_subdev *sd, u32 port,
                      u32 lanes, u32 format, u32 bayer_order, int flag);
struct camera_sensor_platform_data *gmin_camera_platform_data(
		struct v4l2_subdev *subdev,
		enum atomisp_input_format csi_format,
		enum atomisp_bayer_order csi_bayer);

int atomisp_gmin_register_vcm_control(struct camera_vcm_control *);

#endif
