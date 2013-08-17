/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_kbase_cpuprops.c
 * Base kernel property query APIs
 */

#include "mali_kbase.h"
#include "mali_kbase_cpuprops.h"
#include "mali_kbase_uku.h"
#include <kbase/mali_kbase_config.h>
#include <osk/mali_osk.h>

int kbase_cpuprops_get_default_clock_speed(u32 *clock_speed)
{
	OSK_ASSERT( NULL != clock_speed );

	*clock_speed = 100;
	return 0;
}

mali_error kbase_cpuprops_uk_get_props(kbase_context *kctx, kbase_uk_cpuprops * kbase_props)
{
	int result;
	kbase_cpuprops_clock_speed_function kbase_cpuprops_uk_get_clock_speed;

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	kbase_props->props.cpu_l1_dcache_line_size_log2 = OSK_L1_DCACHE_LINE_SIZE_LOG2;
	kbase_props->props.cpu_l1_dcache_size           = OSK_L1_DCACHE_SIZE;
	kbase_props->props.cpu_flags                    = BASE_CPU_PROPERTY_FLAG_LITTLE_ENDIAN;

	kbase_props->props.nr_cores = NR_CPUS;
	kbase_props->props.cpu_page_size_log2 = PAGE_SHIFT;
	kbase_props->props.available_memory_size = totalram_pages << PAGE_SHIFT;

	kbase_cpuprops_uk_get_clock_speed = (kbase_cpuprops_clock_speed_function)kbasep_get_config_value( kctx->kbdev, kctx->kbdev->config_attributes, KBASE_CONFIG_ATTR_CPU_SPEED_FUNC );
	result = kbase_cpuprops_uk_get_clock_speed(&kbase_props->props.max_cpu_clock_speed_mhz);
	if (result != 0)
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	return MALI_ERROR_NONE;
}
