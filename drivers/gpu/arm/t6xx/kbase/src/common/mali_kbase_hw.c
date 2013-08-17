/*
 *
 * (C) COPYRIGHT 2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file
 * Run-time work-arounds helpers
 */

#include <kbase/mali_base_hwconfig.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include "mali_kbase.h"
#include "mali_kbase_hw.h"

mali_error kbase_hw_set_issues_mask(kbase_device *kbdev)
{
	const base_hw_issue *issues;
	u32 gpu_id;

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CORE))
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	gpu_id = kbase_os_reg_read(kbdev, GPU_CONTROL_REG(GPU_ID));

	switch (gpu_id)
	{
		case GPU_ID_MAKE(GPU_ID_PI_T60X, 0, 0, GPU_ID_S_15DEV0):
			issues = base_hw_issues_t60x_r0p0_15dev0;
			break;
		case GPU_ID_MAKE(GPU_ID_PI_T60X, 0, 0, GPU_ID_S_EAC):
			issues = base_hw_issues_t60x_r0p0_eac;
			break;
		case GPU_ID_MAKE(GPU_ID_PI_T60X, 0, 1, 0):
			issues = base_hw_issues_t60x_r0p1;
			break;
		case GPU_ID_MAKE(GPU_ID_PI_T65X, 0, 1, 0):
			issues = base_hw_issues_t65x_r0p1;
			break;
		case GPU_ID_MAKE(GPU_ID_PI_T62X, 0, 0, 0):
		case GPU_ID_MAKE(GPU_ID_PI_T62X, 0, 0, 1):
			issues = base_hw_issues_t62x_r0p0;
			break;
		case GPU_ID_MAKE(GPU_ID_PI_T67X, 0, 0, 0):
		case GPU_ID_MAKE(GPU_ID_PI_T67X, 0, 0, 1):
			issues = base_hw_issues_t67x_r0p0;
			break;
		default:
			OSK_PRINT_ERROR(OSK_BASE_CORE, "Unknown GPU ID %x", gpu_id);
			return MALI_ERROR_FUNCTION_FAILED;
	}

	OSK_PRINT_INFO(OSK_BASE_CORE, "GPU identified as 0x%04x r%dp%d status %d",
				(gpu_id & GPU_ID_VERSION_PRODUCT_ID) >> GPU_ID_VERSION_PRODUCT_ID_SHIFT,
				(gpu_id & GPU_ID_VERSION_MAJOR) >> GPU_ID_VERSION_MAJOR_SHIFT,
				(gpu_id & GPU_ID_VERSION_MINOR) >> GPU_ID_VERSION_MINOR_SHIFT,
				(gpu_id & GPU_ID_VERSION_STATUS) >> GPU_ID_VERSION_STATUS_SHIFT);

	for (; *issues != BASE_HW_ISSUE_END; issues++)
	{
		osk_bitarray_set_bit(*issues, &kbdev->hw_issues_mask[0]);
	}

	return MALI_ERROR_NONE;
}
