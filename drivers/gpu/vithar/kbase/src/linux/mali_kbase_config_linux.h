/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _KBASE_CONFIG_LINUX_H_
#define _KBASE_CONFIG_LINUX_H_

#include <kbase/mali_kbase_config.h>
#include <linux/ioport.h>

#if !MALI_LICENSE_IS_GPL || MALI_FAKE_PLATFORM_DEVICE
#define PLATFORM_CONFIG_RESOURCE_COUNT 4
/**
 * @brief Convert data in kbase_io_resources struct to Linux-specific resources
 *
 * Function converts data in kbase_io_resources struct to an array of Linux resource structures. Note that function
 * assumes that size of linux_resource array is at least PLATFORM_CONFIG_RESOURCE_COUNT.
 * Resources are put in fixed order: I/O memory region, job IRQ, MMU IRQ, GPU IRQ.
 *
 * @param[in]  io_resource      Input IO resource data
 * @param[out] linux_resources  Pointer to output array of Linux resource structures
 */
void kbasep_config_parse_io_resources(const kbase_io_resources *io_resource, struct resource *linux_resources);
#endif /* !MALI_LICENSE_IS_GPL || MALI_FAKE_PLATFORM_DEVICE */


#endif /* _KBASE_CONFIG_LINUX_H_ */
