/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2011-2014 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
