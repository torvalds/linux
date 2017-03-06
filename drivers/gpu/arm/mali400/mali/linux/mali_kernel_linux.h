/*
 * Copyright (C) 2010-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_LINUX_H__
#define __MALI_KERNEL_LINUX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/cdev.h>     /* character device definitions */
#include <linux/idr.h>
#include <linux/rbtree.h>
#include "mali_kernel_license.h"
#include "mali_osk_types.h"
#include <linux/version.h>

extern struct platform_device *mali_platform_device;

/* After 3.19.0 kenrel droped CONFIG_PM_RUNTIME define,define by ourself */
#if defined(CONFIG_PM) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
#define CONFIG_PM_RUNTIME 1
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MALI_KERNEL_LINUX_H__ */
