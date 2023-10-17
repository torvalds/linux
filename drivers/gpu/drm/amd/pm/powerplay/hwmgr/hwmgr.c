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

#include "pp_debug.h"
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <drm/amdgpu_drm.h>
#include "power_state.h"
#include "hwmgr.h"
#include "ppsmc.h"
#include "amd_acpi.h"
#include "pp_psm.h"
#include "vega10_hwmgr.h"

extern const struct pp_smumgr_func ci_smu_funcs;
extern const struct pp_smumgr_func smu8_smu_funcs;
extern const struct pp_smumgr_func iceland_smu_funcs;
extern const struct pp_smumgr_func tonga_smu_funcs;
extern const struct pp_smumgr_func fiji_smu_funcs;
extern const struct pp_smumgr_func polaris10_smu_funcs;
extern const struct pp_smumgr_func vegam_smu_funcs;
extern const struct pp_smumgr_func vega10_smu_funcs;
extern const struct pp_smumgr_func vega12_smu_funcs;
extern const struct pp_smumgr_func smu10_smu_funcs;
extern const struct pp_smumgr_func vega20_smu_funcs;

extern int smu10_init_function_pointers(struct pp_hwmgr *hwmgr);

static int polaris_set_asic_special_caps(struct pp_hwmgr *hwmgr);
static void hwmgr_init_default_caps(struct pp_hwmgr *hwmgr);
static int hwmgr_set_user_specify_caps(struct pp_hwmgr *hwmgr);
static int fiji_set_asic_special_caps(struct pp_hwmgr *hwmgr);
static int tonga_set_asic_special_caps(struct pp_hwmgr *hwmgr);
static int topaz_set_asic_special_caps(struct pp_hwmgr *hwmgr);
static int ci_set_asic_special_caps(struct pp_hwmgr *hwmgr);


static void hwmgr_init_workload_prority(struct pp_hwmgr *hwmgr)
{
	hwmgr->workload_prority[PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT] = 0;
	hwmgr->workload_prority[PP_SMC_POWER_PROFILE_FULLSCREEN3D] = 1;
	hwmgr->workload_prority[PP_SMC_POWER_PROFILE_POWERSAVING] = 2;
	hwmgr->workload_prority[PP_SMC_POWER_PROFILE_VIDEO] = 3;
	hwmgr->workload_prority[PP_SMC_POWER_PROFILE_VR] = 4;
	hwmgr->workload_prority[PP_SMC_POWER_PROFILE_COMPUTE] = 5;

	hwmgr->workload_setting[0] = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;
	hwmgr->workload_setting[1] = PP_SMC_POWER_PROFILE_FULLSCREEN3D;
	hwmgr->workload_setting[2] = PP_SMC_POWER_PROFILE_POWERSAVING;
	hwmgr->workload_setting[3] = PP_SMC_POWER_PROFILE_VIDEO;
	hwmgr->workload_setting[4] = PP_SMC_POWER_PROFILE_VR;
	hwmgr->workload_setting[5] = PP_SMC_POWER_PROFILE_COMPUTE;
}

int hwmgr_early_init(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev;

	if (!hwmgr)
		return -EINVAL;

	hwmgr->usec_timeout = AMD_MAX_USEC_TIMEOUT;
	hwmgr->pp_table_version = PP_TABLE_V1;
	hwmgr->dpm_level = AMD_DPM_FORCED_LEVEL_AUTO;
	hwmgr->request_dpm_level = AMD_DPM_FORCED_LEVEL_AUTO;
	hwmgr_init_default_caps(hwmgr);
	hwmgr_set_user_specify_caps(hwmgr);
	hwmgr->fan_ctrl_is_in_default_mode = true;
	hwmgr_init_workload_prority(hwmgr);
	hwmgr->gfxoff_state_changed_by_workload = false;

	adev = hwmgr->adev;

	switch (hwmgr->chip_family) {
	case AMDGPU_FAMILY_CI:
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		hwmgr->smumgr_funcs = &ci_smu_funcs;
		ci_set_asic_special_caps(hwmgr);
		hwmgr->feature_mask &= ~(PP_VBI_TIME_SUPPORT_MASK |
					 PP_ENABLE_GFX_CG_THRU_SMU |
					 PP_GFXOFF_MASK);
		hwmgr->pp_table_version = PP_TABLE_V0;
		hwmgr->od_enabled = false;
		smu7_init_function_pointers(hwmgr);
		break;
	case AMDGPU_FAMILY_CZ:
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		hwmgr->od_enabled = false;
		hwmgr->smumgr_funcs = &smu8_smu_funcs;
		hwmgr->feature_mask &= ~PP_GFXOFF_MASK;
		smu8_init_function_pointers(hwmgr);
		break;
	case AMDGPU_FAMILY_VI:
		adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
		hwmgr->feature_mask &= ~PP_GFXOFF_MASK;
		switch (hwmgr->chip_id) {
		case CHIP_TOPAZ:
			hwmgr->smumgr_funcs = &iceland_smu_funcs;
			topaz_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~(PP_VBI_TIME_SUPPORT_MASK |
						PP_ENABLE_GFX_CG_THRU_SMU);
			hwmgr->pp_table_version = PP_TABLE_V0;
			hwmgr->od_enabled = false;
			break;
		case CHIP_TONGA:
			hwmgr->smumgr_funcs = &tonga_smu_funcs;
			tonga_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~PP_VBI_TIME_SUPPORT_MASK;
			break;
		case CHIP_FIJI:
			hwmgr->smumgr_funcs = &fiji_smu_funcs;
			fiji_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~(PP_VBI_TIME_SUPPORT_MASK |
						PP_ENABLE_GFX_CG_THRU_SMU);
			break;
		case CHIP_POLARIS11:
		case CHIP_POLARIS10:
		case CHIP_POLARIS12:
			hwmgr->smumgr_funcs = &polaris10_smu_funcs;
			polaris_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~(PP_UVD_HANDSHAKE_MASK);
			break;
		case CHIP_VEGAM:
			hwmgr->smumgr_funcs = &vegam_smu_funcs;
			polaris_set_asic_special_caps(hwmgr);
			hwmgr->feature_mask &= ~(PP_UVD_HANDSHAKE_MASK);
			break;
		default:
			return -EINVAL;
		}
		smu7_init_function_pointers(hwmgr);
		break;
	case AMDGPU_FAMILY_AI:
		switch (hwmgr->chip_id) {
		case CHIP_VEGA10:
			adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
			hwmgr->feature_mask &= ~PP_GFXOFF_MASK;
			hwmgr->smumgr_funcs = &vega10_smu_funcs;
			vega10_hwmgr_init(hwmgr);
			break;
		case CHIP_VEGA12:
			hwmgr->smumgr_funcs = &vega12_smu_funcs;
			vega12_hwmgr_init(hwmgr);
			break;
		case CHIP_VEGA20:
			adev->pm.pp_feature &= ~PP_GFXOFF_MASK;
			hwmgr->feature_mask &= ~PP_GFXOFF_MASK;
			hwmgr->smumgr_funcs = &vega20_smu_funcs;
			vega20_hwmgr_init(hwmgr);
			break;
		default:
			return -EINVAL;
		}
		break;
	case AMDGPU_FAMILY_RV:
		switch (hwmgr->chip_id) {
		case CHIP_RAVEN:
			hwmgr->od_enabled = false;
			hwmgr->smumgr_funcs = &smu10_smu_funcs;
			smu10_init_function_pointers(hwmgr);
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int hwmgr_sw_init(struct pp_hwmgr *hwmgr)
{
	if (!hwmgr || !hwmgr->smumgr_funcs || !hwmgr->smumgr_funcs->smu_init)
		return -EINVAL;

	phm_register_irq_handlers(hwmgr);
	pr_info("hwmgr_sw_init smu backed is %s\n", hwmgr->smumgr_funcs->name);

	return hwmgr->smumgr_funcs->smu_init(hwmgr);
}


int hwmgr_sw_fini(struct pp_hwmgr *hwmgr)
{
	if (hwmgr && hwmgr->smumgr_funcs && hwmgr->smumgr_funcs->smu_fini)
		hwmgr->smumgr_funcs->smu_fini(hwmgr);

	return 0;
}

int hwmgr_hw_init(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	hwmgr->pp_one_vf = amdgpu_sriov_is_pp_one_vf((struct amdgpu_device *)hwmgr->adev);
	hwmgr->pm_en = (amdgpu_dpm && (hwmgr->not_vf || hwmgr->pp_one_vf))
			? true : false;
	if (!hwmgr->pm_en)
		return 0;

	if (!hwmgr->pptable_func ||
	    !hwmgr->pptable_func->pptable_init ||
	    !hwmgr->hwmgr_func->backend_init) {
		hwmgr->pm_en = false;
		pr_info("dpm not supported \n");
		return 0;
	}

	ret = hwmgr->pptable_func->pptable_init(hwmgr);
	if (ret)
		goto err;

	((struct amdgpu_device *)hwmgr->adev)->pm.no_fan =
				hwmgr->thermal_controller.fanInfo.bNoFan;

	ret = hwmgr->hwmgr_func->backend_init(hwmgr);
	if (ret)
		goto err1;
 /* make sure dc limits are valid */
	if ((hwmgr->dyn_state.max_clock_voltage_on_dc.sclk == 0) ||
			(hwmgr->dyn_state.max_clock_voltage_on_dc.mclk == 0))
			hwmgr->dyn_state.max_clock_voltage_on_dc =
					hwmgr->dyn_state.max_clock_voltage_on_ac;

	ret = psm_init_power_state_table(hwmgr);
	if (ret)
		goto err2;

	ret = phm_setup_asic(hwmgr);
	if (ret)
		goto err2;

	ret = phm_enable_dynamic_state_management(hwmgr);
	if (ret)
		goto err2;
	ret = phm_start_thermal_controller(hwmgr);
	ret |= psm_set_performance_states(hwmgr);
	if (ret)
		goto err2;

	((struct amdgpu_device *)hwmgr->adev)->pm.dpm_enabled = true;

	return 0;
err2:
	if (hwmgr->hwmgr_func->backend_fini)
		hwmgr->hwmgr_func->backend_fini(hwmgr);
err1:
	if (hwmgr->pptable_func->pptable_fini)
		hwmgr->pptable_func->pptable_fini(hwmgr);
err:
	return ret;
}

int hwmgr_hw_fini(struct pp_hwmgr *hwmgr)
{
	if (!hwmgr || !hwmgr->pm_en || !hwmgr->not_vf)
		return 0;

	phm_stop_thermal_controller(hwmgr);
	psm_set_boot_states(hwmgr);
	psm_adjust_power_state_dynamic(hwmgr, true, NULL);
	phm_disable_dynamic_state_management(hwmgr);
	phm_disable_clock_power_gatings(hwmgr);

	if (hwmgr->hwmgr_func->backend_fini)
		hwmgr->hwmgr_func->backend_fini(hwmgr);
	if (hwmgr->pptable_func->pptable_fini)
		hwmgr->pptable_func->pptable_fini(hwmgr);
	return psm_fini_power_state_table(hwmgr);
}

int hwmgr_suspend(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	if (!hwmgr || !hwmgr->pm_en || !hwmgr->not_vf)
		return 0;

	phm_disable_smc_firmware_ctf(hwmgr);
	ret = psm_set_boot_states(hwmgr);
	if (ret)
		return ret;
	ret = psm_adjust_power_state_dynamic(hwmgr, true, NULL);
	if (ret)
		return ret;
	ret = phm_power_down_asic(hwmgr);

	return ret;
}

int hwmgr_resume(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	if (!hwmgr)
		return -EINVAL;

	if (!hwmgr->not_vf || !hwmgr->pm_en)
		return 0;

	ret = phm_setup_asic(hwmgr);
	if (ret)
		return ret;

	ret = phm_enable_dynamic_state_management(hwmgr);
	if (ret)
		return ret;
	ret = phm_start_thermal_controller(hwmgr);
	ret |= psm_set_performance_states(hwmgr);
	if (ret)
		return ret;

	ret = psm_adjust_power_state_dynamic(hwmgr, false, NULL);

	return ret;
}

static enum PP_StateUILabel power_state_convert(enum amd_pm_state_type  state)
{
	switch (state) {
	case POWER_STATE_TYPE_BATTERY:
		return PP_StateUILabel_Battery;
	case POWER_STATE_TYPE_BALANCED:
		return PP_StateUILabel_Balanced;
	case POWER_STATE_TYPE_PERFORMANCE:
		return PP_StateUILabel_Performance;
	default:
		return PP_StateUILabel_None;
	}
}

int hwmgr_handle_task(struct pp_hwmgr *hwmgr, enum amd_pp_task task_id,
		enum amd_pm_state_type *user_state)
{
	int ret = 0;

	if (hwmgr == NULL)
		return -EINVAL;

	switch (task_id) {
	case AMD_PP_TASK_DISPLAY_CONFIG_CHANGE:
		if (!hwmgr->not_vf)
			return ret;
		ret = phm_pre_display_configuration_changed(hwmgr);
		if (ret)
			return ret;
		ret = phm_set_cpu_power_state(hwmgr);
		if (ret)
			return ret;
		ret = psm_set_performance_states(hwmgr);
		if (ret)
			return ret;
		ret = psm_adjust_power_state_dynamic(hwmgr, false, NULL);
		break;
	case AMD_PP_TASK_ENABLE_USER_STATE:
	{
		enum PP_StateUILabel requested_ui_label;
		struct pp_power_state *requested_ps = NULL;

		if (!hwmgr->not_vf)
			return ret;
		if (user_state == NULL) {
			ret = -EINVAL;
			break;
		}

		requested_ui_label = power_state_convert(*user_state);
		ret = psm_set_user_performance_state(hwmgr, requested_ui_label, &requested_ps);
		if (ret)
			return ret;
		ret = psm_adjust_power_state_dynamic(hwmgr, true, requested_ps);
		break;
	}
	case AMD_PP_TASK_COMPLETE_INIT:
	case AMD_PP_TASK_READJUST_POWER_STATE:
		ret = psm_adjust_power_state_dynamic(hwmgr, true, NULL);
		break;
	default:
		break;
	}
	return ret;
}

void hwmgr_init_default_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_UVDDPM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_VCEDPM);

#if defined(CONFIG_ACPI)
	if (amdgpu_acpi_is_pcie_performance_request_supported(hwmgr->adev))
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest);
#endif

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_DynamicPatchPowerState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_EnableSMU7ThermalManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPowerManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SMC);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicUVDState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_FanSpeedInTableIsRPM);
	return;
}

int hwmgr_set_user_specify_caps(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->feature_mask & PP_SCLK_DEEP_SLEEP_MASK)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);
	else
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);

	if (hwmgr->feature_mask & PP_POWER_CONTAINMENT_MASK) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_PowerContainment);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC);
	} else {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_PowerContainment);
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC);
	}

	if (hwmgr->feature_mask & PP_OVERDRIVE_MASK)
		hwmgr->od_enabled = true;

	return 0;
}

int polaris_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_EVV);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_SQRamping);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_RegulatorHot);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MemorySpreadSpectrumSupport);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EngineSpreadSpectrumSupport);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_AutomaticDCTransition);

	if (((hwmgr->chip_id == CHIP_POLARIS11) && !hwmgr->is_kicker) ||
	    (hwmgr->chip_id == CHIP_POLARIS12))
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SPLLShutdownSupport);

	if (hwmgr->chip_id != CHIP_POLARIS11) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_DBRamping);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_TDRamping);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_TCPRamping);
	}
	return 0;
}

int fiji_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_EVV);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);
	return 0;
}

int tonga_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_EVV);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_UVDPowerGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_VCEPowerGating);
	return 0;
}

int topaz_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_EVV);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);
	return 0;
}

int ci_set_asic_special_caps(struct pp_hwmgr *hwmgr)
{
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MemorySpreadSpectrumSupport);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EngineSpreadSpectrumSupport);
	return 0;
}
