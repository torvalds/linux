/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/errno.h>
#include "hwmgr.h"
#include "hardwaremanager.h"
#include "pp_acpi.h"
#include "amd_acpi.h"

void phm_init_dynamic_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableVoltageTransition);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableEngineTransition);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMemoryTransition);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMGClockGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMGCGTSSM);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableLSClockGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_Force3DClockSupport);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableLightSleep);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableMCLS);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisablePowerGating);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableDPM);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DisableSMUUVDHandshake);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_ThermalAutoThrottling);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_NoOD5Support);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_UserMaxClockForMultiDisplays);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_VpuRecoveryInProgress);

	if (acpi_atcs_functions_supported(hwmgr->device, ATCS_FUNCTION_PCIE_PERFORMANCE_REQUEST) &&
		acpi_atcs_functions_supported(hwmgr->device, ATCS_FUNCTION_PCIE_DEVICE_READY_NOTIFICATION))
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest);
}

int phm_setup_asic(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_TablelessHardwareInterface)) {
		if (NULL != hwmgr->hwmgr_func->asic_setup)
			return hwmgr->hwmgr_func->asic_setup(hwmgr);
	} else {
		return phm_dispatch_table (hwmgr, &(hwmgr->setup_asic),
					  NULL, NULL);
	}

	return 0;
}

int phm_enable_dynamic_state_management(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_TablelessHardwareInterface)) {
		if (NULL != hwmgr->hwmgr_func->dynamic_state_management_enable)
			return hwmgr->hwmgr_func->dynamic_state_management_enable(hwmgr);
	} else {
		return phm_dispatch_table (hwmgr,
				&(hwmgr->enable_dynamic_state_management),
				NULL, NULL);
	}
	return 0;
}
