/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __VIDEO_ADF_ADF_SYSFS_H
#define __VIDEO_ADF_ADF_SYSFS_H

struct adf_device;
struct adf_interface;
struct adf_overlay_engine;

int adf_device_sysfs_init(struct adf_device *dev);
void adf_device_sysfs_destroy(struct adf_device *dev);
int adf_interface_sysfs_init(struct adf_interface *intf);
void adf_interface_sysfs_destroy(struct adf_interface *intf);
int adf_overlay_engine_sysfs_init(struct adf_overlay_engine *eng);
void adf_overlay_engine_sysfs_destroy(struct adf_overlay_engine *eng);
struct adf_obj *adf_obj_sysfs_find(int minor);

int adf_sysfs_init(void);
void adf_sysfs_destroy(void);

#endif /* __VIDEO_ADF_ADF_SYSFS_H */
