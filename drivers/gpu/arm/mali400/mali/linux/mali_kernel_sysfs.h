/*
 * Copyright (C) 2011-2013, 2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_SYSFS_H__
#define __MALI_KERNEL_SYSFS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/device.h>

#define MALI_PROC_DIR "driver/mali"

int mali_sysfs_register(const char *mali_dev_name);
int mali_sysfs_unregister(void);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_KERNEL_LINUX_H__ */
