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

#ifndef	__ATOMISP_FOPS_H__
#define	__ATOMISP_FOPS_H__
#include "atomisp_subdev.h"

unsigned int atomisp_dev_users(struct atomisp_device *isp);
unsigned int atomisp_sub_dev_users(struct atomisp_sub_device *asd);

/*
 * Memory help functions for image frame and private parameters
 */

int atomisp_qbuffers_to_css(struct atomisp_sub_device *asd);

extern const struct v4l2_file_operations atomisp_fops;

extern bool defer_fw_load;

#endif /* __ATOMISP_FOPS_H__ */
