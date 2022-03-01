/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
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

#ifndef __ATOMISP_V4L2_H__
#define __ATOMISP_V4L2_H__

struct atomisp_video_pipe;
struct atomisp_acc_pipe;
struct v4l2_device;
struct atomisp_device;
struct firmware;

int atomisp_video_init(struct atomisp_video_pipe *video, const char *name,
		       unsigned int run_mode);
void atomisp_acc_init(struct atomisp_acc_pipe *video, const char *name);
void atomisp_video_unregister(struct atomisp_video_pipe *video);
void atomisp_acc_unregister(struct atomisp_acc_pipe *video);
const struct firmware *atomisp_load_firmware(struct atomisp_device *isp);
int atomisp_csi_lane_config(struct atomisp_device *isp);

#endif /* __ATOMISP_V4L2_H__ */
