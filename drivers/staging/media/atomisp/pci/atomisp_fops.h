/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 */

#ifndef	__ATOMISP_FOPS_H__
#define	__ATOMISP_FOPS_H__
#include "atomisp_subdev.h"

/*
 * Memory help functions for image frame and private parameters
 */

int atomisp_qbuffers_to_css(struct atomisp_sub_device *asd);

extern const struct vb2_ops atomisp_vb2_ops;
extern const struct v4l2_file_operations atomisp_fops;

#endif /* __ATOMISP_FOPS_H__ */
