/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for atomisp driver sysfs interface.
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
 *
 *
 */

#ifndef	__ATOMISP_DRVFS_H__
#define	__ATOMISP_DRVFS_H__

int atomisp_drvfs_init(struct device_driver *drv, struct atomisp_device *isp);
void atomisp_drvfs_exit(void);

#endif /* __ATOMISP_DRVFS_H__ */
