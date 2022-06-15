/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Clovertrail PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
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

#ifndef __ATOMISP_ACC_H__
#define __ATOMISP_ACC_H__

#include "../../include/linux/atomisp.h"
#include "atomisp_internal.h"

#include "ia_css_types.h"

/*
 * Interface functions for AtomISP driver acceleration API implementation.
 */

struct atomisp_sub_device;

void atomisp_acc_cleanup(struct atomisp_device *isp);

/*
 * Free up any allocated resources.
 * Must be called each time when the device is closed.
 * Note that there isn't corresponding open() call;
 * this function may be called sequentially multiple times.
 * Must be called to free up resources before driver is unloaded.
 */
void atomisp_acc_release(struct atomisp_sub_device *asd);

/*
 * Used by ISR to notify ACC stage finished.
 * This is internally used and does not export as IOCTL.
 */
void atomisp_acc_done(struct atomisp_sub_device *asd, unsigned int handle);

/*
 * Appends the loaded acceleration binary extensions to the
 * current ISP mode. Must be called just before atomisp_css_start().
 */
int atomisp_acc_load_extensions(struct atomisp_sub_device *asd);

/*
 * Must be called after streaming is stopped:
 * unloads any loaded acceleration extensions.
 */
void atomisp_acc_unload_extensions(struct atomisp_sub_device *asd);

#endif /* __ATOMISP_ACC_H__ */
