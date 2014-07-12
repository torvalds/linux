/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2008-2013 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
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
#include "mali_kernel_linux.h"

static u32 _mali_osk_resource_irq(_mali_osk_resource_t *res)
{
	int i;
	char name[32];
	struct resource *resource;

	snprintf(name, sizeof(name), "%s_IRQ", res->description);
	for (i = 0; i < mali_platform_device->num_resources; i++) {
		resource = &mali_platform_device->resource[i];
		if (IORESOURCE_IRQ == resource_type(resource) &&
		    !strncmp(name, resource->name, sizeof(name)))
			return resource->start;
	}

	return -1;
}

_mali_osk_errcode_t _mali_osk_resource_find(u32 addr, _mali_osk_resource_t *res)
{
	int i;

	if (NULL == mali_platform_device) {
		/* Not connected to a device */
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	for (i = 0; i < mali_platform_device->num_resources; i++) {
		if (IORESOURCE_MEM == resource_type(&(mali_platform_device->resource[i])) &&
		    mali_platform_device->resource[i].start == addr) {
			if (NULL != res) {
				res->base = addr;
				res->description = mali_platform_device->resource[i].name;

				res->irq = _mali_osk_resource_irq(res);
#if 0
				/* Any (optional) IRQ resource belonging to this resource will follow */
				if ((i + 1) < mali_platform_device->num_resources &&
				    IORESOURCE_IRQ == resource_type(&(mali_platform_device->resource[i+1]))) {
					res->irq = mali_platform_device->resource[i+1].start;
				} else {
					res->irq = -1;
				}
#endif
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

	if (NULL != mali_platform_device) {
		int i;
		for (i = 0; i < mali_platform_device->num_resources; i++) {
			if (mali_platform_device->resource[i].flags & IORESOURCE_MEM &&
			    mali_platform_device->resource[i].start < lowest_addr) {
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

	if (NULL != mali_platform_device) {
		struct mali_gpu_device_data* os_data = NULL;

		os_data = (struct mali_gpu_device_data*)mali_platform_device->dev.platform_data;
		if (NULL != os_data) {
			/* Copy data from OS dependant struct to Mali neutral struct (identical!) */
			data->dedicated_mem_start = os_data->dedicated_mem_start;
			data->dedicated_mem_size = os_data->dedicated_mem_size;
			data->shared_mem_size = os_data->shared_mem_size;
			data->fb_start = os_data->fb_start;
			data->fb_size = os_data->fb_size;
			data->max_job_runtime = os_data->max_job_runtime;
			data->utilization_interval = os_data->utilization_interval;
			data->utilization_callback = os_data->utilization_callback;
			data->pmu_switch_delay = os_data->pmu_switch_delay;
			data->set_freq_callback = os_data->set_freq_callback;

			memcpy(data->pmu_domain_config, os_data->pmu_domain_config, sizeof(os_data->pmu_domain_config));
			return _MALI_OSK_ERR_OK;
		}
	}

	return _MALI_OSK_ERR_ITEM_NOT_FOUND;
}

mali_bool _mali_osk_shared_interrupts(void)
{
	u32 irqs[128];
	u32 i, j, irq, num_irqs_found = 0;

	MALI_DEBUG_ASSERT_POINTER(mali_platform_device);
	MALI_DEBUG_ASSERT(128 >= mali_platform_device->num_resources);

	for (i = 0; i < mali_platform_device->num_resources; i++) {
		if (IORESOURCE_IRQ & mali_platform_device->resource[i].flags) {
			irq = mali_platform_device->resource[i].start;

			for (j = 0; j < num_irqs_found; ++j) {
				if (irq == irqs[j]) {
					return MALI_TRUE;
				}
			}

			irqs[num_irqs_found++] = irq;
		}
	}

	return MALI_FALSE;
}
