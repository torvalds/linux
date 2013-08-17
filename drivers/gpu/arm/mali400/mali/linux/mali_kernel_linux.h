/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
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
extern "C"
{
#endif

#include <linux/cdev.h>     /* character device definitions */
#include "mali_kernel_license.h"
#include "mali_osk.h"

extern struct platform_device *mali_platform_device;

#if MALI_LICENSE_IS_GPL
/* Defined in mali_osk_irq.h */
extern struct workqueue_struct * mali_wq;
#endif

void mali_osk_low_level_mem_init(void);
void mali_osk_low_level_mem_term(void);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_KERNEL_LINUX_H__ */
