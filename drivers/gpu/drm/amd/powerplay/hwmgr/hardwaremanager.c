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
#include "power_state.h"
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

bool phm_is_hw_access_blocked(struct pp_hwmgr *hwmgr)
{
	return hwmgr->block_hw_access;
}

int phm_block_hw_access(struct pp_hwmgr *hwmgr, bool block)
{
	hwmgr->block_hw_access = block;
	return 0;
}

int phm_setup_asic(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_TablelessHardwareInterface)) {
		if (NULL != hwmgr->hwmgr_func->asic_setup)
			return hwmgr->hwmgr_func->asic_setup(hwmgr);
	} else {
		return phm_dispatch_table(hwmgr, &(hwmgr->setup_asic),
					  NULL, NULL);
	}

	return 0;
}

int phm_set_power_state(struct pp_hwmgr *hwmgr,
		    const struct pp_hw_power_state *pcurrent_state,
		    const struct pp_hw_power_state *pnew_power_state)
{
	struct phm_set_power_state_input states;

	states.pcurrent_state = pcurrent_state;
	states.pnew_state = pnew_power_state;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_TablelessHardwareInterface)) {
		if (NULL != hwmgr->hwmgr_func->power_state_set)
			return hwmgr->hwmgr_func->power_state_set(hwmgr, &states);
	} else {
		return phm_dispatch_table(hwmgr, &(hwmgr->set_power_state), &states, NULL);
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
		return phm_dispatch_table(hwmgr,
				&(hwmgr->enable_dynamic_state_management),
				NULL, NULL);
	}
	return 0;
}

int phm_force_dpm_levels(struct pp_hwmgr *hwmgr, enum amd_dpm_forced_level level)
{
	if (hwmgr->hwmgr_func->force_dpm_level != NULL)
		return hwmgr->hwmgr_func->force_dpm_level(hwmgr, level);

	return 0;
}

int phm_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				   struct pp_power_state *adjusted_ps,
			     const struct pp_power_state *current_ps)
{
	if (hwmgr->hwmgr_func->apply_state_adjust_rules != NULL)
		return hwmgr->hwmgr_func->apply_state_adjust_rules(
									hwmgr,
								 adjusted_ps,
								 current_ps);
	return 0;
}

int phm_powerdown_uvd(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->hwmgr_func->powerdown_uvd != NULL)
		return hwmgr->hwmgr_func->powerdown_uvd(hwmgr);
	return 0;
}

int phm_powergate_uvd(struct pp_hwmgr *hwmgr, bool gate)
{
	if (hwmgr->hwmgr_func->powergate_uvd != NULL)
		return hwmgr->hwmgr_func->powergate_uvd(hwmgr, gate);
	return 0;
}

int phm_powergate_vce(struct pp_hwmgr *hwmgr, bool gate)
{
	if (hwmgr->hwmgr_func->powergate_vce != NULL)
		return hwmgr->hwmgr_func->powergate_vce(hwmgr, gate);
	return 0;
}

int phm_enable_clock_power_gatings(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_TablelessHardwareInterface)) {
		if (NULL != hwmgr->hwmgr_func->enable_clock_power_gating)
			return hwmgr->hwmgr_func->enable_clock_power_gating(hwmgr);
	} else {
		return phm_dispatch_table(hwmgr, &(hwmgr->enable_clock_power_gatings), NULL, NULL);
	}
	return 0;
}

int phm_display_configuration_changed(struct pp_hwmgr *hwmgr)
{
	if (hwmgr == NULL)
		return -EINVAL;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				 PHM_PlatformCaps_TablelessHardwareInterface)) {
		if (NULL != hwmgr->hwmgr_func->display_config_changed)
			hwmgr->hwmgr_func->display_config_changed(hwmgr);
	} else
		return phm_dispatch_table(hwmgr, &hwmgr->display_configuration_changed, NULL, NULL);
    return 0;
}

int phm_notify_smc_display_config_after_ps_adjustment(struct pp_hwmgr *hwmgr)
{
	if (hwmgr == NULL)
		return -EINVAL;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				 PHM_PlatformCaps_TablelessHardwareInterface))
		if (NULL != hwmgr->hwmgr_func->display_config_changed)
			hwmgr->hwmgr_func->notify_smc_display_config_after_ps_adjustment(hwmgr);

    return 0;
}
