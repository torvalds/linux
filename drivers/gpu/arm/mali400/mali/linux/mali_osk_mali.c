/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_mali.c
 * Implementation of the OS abstraction layer which is specific for the Mali kernel device driver
 */
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <linux/mali/mali_utgard.h>

#include "mali_osk_mali.h"
#include "mali_kernel_common.h" /* MALI_xxx macros */
#include "mali_osk.h"           /* kernel side OS functions */
#include "mali_uk_types.h"
#include "mali_kernel_linux.h"

_mali_osk_errcode_t _mali_osk_resource_find(u32 addr, _mali_osk_resource_t *res)
{
	int i;

	if (NULL == mali_platform_device)
	{
		/* Not connected to a device */
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	for (i = 0; i < mali_platform_device->num_resources; i++)
	{
		if (IORESOURCE_MEM == resource_type(&(mali_platform_device->resource[i])) &&
		    mali_platform_device->resource[i].start == addr)
		{
			if (NULL != res)
			{
				res->base = addr;
				res->description = mali_platform_device->resource[i].name;

				/* Any (optional) IRQ resource belonging to this resource will follow */
				if ((i + 1) < mali_platform_device->num_resources &&
				    IORESOURCE_IRQ == resource_type(&(mali_platform_device->resource[i+1])))
				{
					res->irq = mali_platform_device->resource[i+1].start;
				}
				else
				{
					res->irq = -1;
				}
			}
			return _MALI_OSK_ERR_OK;
		}
	}

	return _MALI_OSK_ERR_ITEM_NOT_FOUND;
}

u32 _mali_osk_resource_base_address(void)
{
	u32 lowest_addr = 0xFFFFFFFF;
	u32 ret = 0;

	if (NULL != mali_platform_device)
	{
		int i;
		for (i = 0; i < mali_platform_device->num_resources; i++)
		{
			if (mali_platform_device->resource[i].flags & IORESOURCE_MEM &&
			    mali_platform_device->resource[i].start < lowest_addr)
			{
				lowest_addr = mali_platform_device->resource[i].start;
				ret = lowest_addr;
			}
		}
	}

	return ret;
}

_mali_osk_errcode_t _mali_osk_device_data_get(struct _mali_osk_device_data *data)
{
	MALI_DEBUG_ASSERT_POINTER(data);

	if (NULL != mali_platform_device)
	{
		struct mali_gpu_device_data* os_data = NULL;

		os_data = (struct mali_gpu_device_data*)mali_platform_device->dev.platform_data;
		if (NULL != os_data)
		{
			/* Copy data from OS dependant struct to Mali neutral struct (identical!) */
			data->dedicated_mem_start = os_data->dedicated_mem_start;
			data->dedicated_mem_size = os_data->dedicated_mem_size;
			data->shared_mem_size = os_data->shared_mem_size;
			data->fb_start = os_data->fb_start;
			data->fb_size = os_data->fb_size;
			data->utilization_interval = os_data->utilization_interval;
			data->utilization_handler = os_data->utilization_handler;
			return _MALI_OSK_ERR_OK;
		}
	}

	return _MALI_OSK_ERR_ITEM_NOT_FOUND;
}
