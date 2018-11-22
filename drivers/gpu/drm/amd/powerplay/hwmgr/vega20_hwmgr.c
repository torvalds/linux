/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "hwmgr.h"
#include "amd_powerplay.h"
#include "vega20_smumgr.h"
#include "hardwaremanager.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "cgs_common.h"
#include "vega20_powertune.h"
#include "vega20_inc.h"
#include "pppcielanes.h"
#include "vega20_hwmgr.h"
#include "vega20_processpptables.h"
#include "vega20_pptable.h"
#include "vega20_thermal.h"
#include "vega20_ppsmc.h"
#include "pp_debug.h"
#include "amd_pcie_helpers.h"
#include "ppinterrupt.h"
#include "pp_overdriver.h"
#include "pp_thermal.h"
#include "soc15_common.h"
#include "smuio/smuio_9_0_offset.h"
#include "smuio/smuio_9_0_sh_mask.h"

static void vega20_set_default_registry_data(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);

	data->gfxclk_average_alpha = PPVEGA20_VEGA20GFXCLKAVERAGEALPHA_DFLT;
	data->socclk_average_alpha = PPVEGA20_VEGA20SOCCLKAVERAGEALPHA_DFLT;
	data->uclk_average_alpha = PPVEGA20_VEGA20UCLKCLKAVERAGEALPHA_DFLT;
	data->gfx_activity_average_alpha = PPVEGA20_VEGA20GFXACTIVITYAVERAGEALPHA_DFLT;
	data->lowest_uclk_reserved_for_ulv = PPVEGA20_VEGA20LOWESTUCLKRESERVEDFORULV_DFLT;

	data->display_voltage_mode = PPVEGA20_VEGA20DISPLAYVOLTAGEMODE_DFLT;
	data->dcef_clk_quad_eqn_a = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->dcef_clk_quad_eqn_b = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->dcef_clk_quad_eqn_c = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_a = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_b = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_c = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_a = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_b = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_c = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_a = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_b = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_c = PPREGKEY_VEGA20QUADRATICEQUATION_DFLT;

	/*
	 * Disable the following features for now:
	 *   GFXCLK DS
	 *   SOCLK DS
	 *   LCLK DS
	 *   DCEFCLK DS
	 *   FCLK DS
	 *   MP1CLK DS
	 *   MP0CLK DS
	 */
	data->registry_data.disallowed_features = 0xE0041C00;
	data->registry_data.od_state_in_dc_support = 0;
	data->registry_data.thermal_support = 1;
	data->registry_data.skip_baco_hardware = 0;

	data->registry_data.log_avfs_param = 0;
	data->registry_data.sclk_throttle_low_notification = 1;
	data->registry_data.force_dpm_high = 0;
	data->registry_data.stable_pstate_sclk_dpm_percentage = 75;

	data->registry_data.didt_support = 0;
	if (data->registry_data.didt_support) {
		data->registry_data.didt_mode = 6;
		data->registry_data.sq_ramping_support = 1;
		data->registry_data.db_ramping_support = 0;
		data->registry_data.td_ramping_support = 0;
		data->registry_data.tcp_ramping_support = 0;
		data->registry_data.dbr_ramping_support = 0;
		data->registry_data.edc_didt_support = 1;
		data->registry_data.gc_didt_support = 0;
		data->registry_data.psm_didt_support = 0;
	}

	data->registry_data.pcie_lane_override = 0xff;
	data->registry_data.pcie_speed_override = 0xff;
	data->registry_data.pcie_clock_override = 0xffffffff;
	data->registry_data.regulator_hot_gpio_support = 1;
	data->registry_data.ac_dc_switch_gpio_support = 0;
	data->registry_data.quick_transition_support = 0;
	data->registry_data.zrpm_start_temp = 0xffff;
	data->registry_data.zrpm_stop_temp = 0xffff;
	data->registry_data.od8_feature_enable = 1;
	data->registry_data.disable_water_mark = 0;
	data->registry_data.disable_pp_tuning = 0;
	data->registry_data.disable_xlpp_tuning = 0;
	data->registry_data.disable_workload_policy = 0;
	data->registry_data.perf_ui_tuning_profile_turbo = 0x19190F0F;
	data->registry_data.perf_ui_tuning_profile_powerSave = 0x19191919;
	data->registry_data.perf_ui_tuning_profile_xl = 0x00000F0A;
	data->registry_data.force_workload_policy_mask = 0;
	data->registry_data.disable_3d_fs_detection = 0;
	data->registry_data.fps_support = 1;
	data->registry_data.disable_auto_wattman = 1;
	data->registry_data.auto_wattman_debug = 0;
	data->registry_data.auto_wattman_sample_period = 100;
	data->registry_data.fclk_gfxclk_ratio = 0x3F6CCCCD;
	data->registry_data.auto_wattman_threshold = 50;
	data->registry_data.gfxoff_controlled_by_driver = 1;
	data->gfxoff_allowed = false;
	data->counter_gfxoff = 0;
}

static int vega20_set_features_platform_caps(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	struct amdgpu_device *adev = hwmgr->adev;

	if (data->vddci_control == VEGA20_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ControlVDDCI);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TablelessHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableSMU7ThermalManagement);

	if (adev->pg_flags & AMD_PG_SUPPORT_UVD)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_UVDPowerGating);

	if (adev->pg_flags & AMD_PG_SUPPORT_VCE)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_VCEPowerGating);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UnTabledHardwareInterface);

	if (data->registry_data.od8_feature_enable)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_OD8inACSupport);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ActivityReporting);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_FanSpeedInTableIsRPM);

	if (data->registry_data.od_state_in_dc_support) {
		if (data->registry_data.od8_feature_enable)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_OD8inDCSupport);
	}

	if (data->registry_data.thermal_support &&
	    data->registry_data.fuzzy_fan_control_support &&
	    hwmgr->thermal_controller.advanceFanControlParameters.usTMax)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ODFuzzyFanControlSupport);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPowerManagement);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SMC);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalPolicyDelay);

	if (data->registry_data.force_dpm_high)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ExclusiveModeAlwaysHigh);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicUVDState);

	if (data->registry_data.sclk_throttle_low_notification)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SclkThrottleLowNotification);

	/* power tune caps */
	/* assume disabled */
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DiDtSupport);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DiDtEDCEnable);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_GCEDC);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PSM);

	if (data->registry_data.didt_support) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_DiDtSupport);
		if (data->registry_data.sq_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SQRamping);
		if (data->registry_data.db_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DBRamping);
		if (data->registry_data.td_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_TDRamping);
		if (data->registry_data.tcp_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_TCPRamping);
		if (data->registry_data.dbr_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DBRRamping);
		if (data->registry_data.edc_didt_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DiDtEDCEnable);
		if (data->registry_data.gc_didt_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_GCEDC);
		if (data->registry_data.psm_didt_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_PSM);
	}

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot);

	if (data->registry_data.ac_dc_switch_gpio_support) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SMCtoPPLIBAcdcGpioScheme);
	}

	if (data->registry_data.quick_transition_support) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition);
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SMCtoPPLIBAcdcGpioScheme);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_Falcon_QuickTransition);
	}

	if (data->lowest_uclk_reserved_for_ulv != PPVEGA20_VEGA20LOWESTUCLKRESERVEDFORULV_DFLT) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_LowestUclkReservedForUlv);
		if (data->lowest_uclk_reserved_for_ulv == 1)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_LowestUclkReservedForUlv);
	}

	if (data->registry_data.custom_fan_support)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_CustomFanControlSupport);

	return 0;
}

static void vega20_init_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	int i;

	data->smu_features[GNLD_DPM_PREFETCHER].smu_feature_id =
			FEATURE_DPM_PREFETCHER_BIT;
	data->smu_features[GNLD_DPM_GFXCLK].smu_feature_id =
			FEATURE_DPM_GFXCLK_BIT;
	data->smu_features[GNLD_DPM_UCLK].smu_feature_id =
			FEATURE_DPM_UCLK_BIT;
	data->smu_features[GNLD_DPM_SOCCLK].smu_feature_id =
			FEATURE_DPM_SOCCLK_BIT;
	data->smu_features[GNLD_DPM_UVD].smu_feature_id =
			FEATURE_DPM_UVD_BIT;
	data->smu_features[GNLD_DPM_VCE].smu_feature_id =
			FEATURE_DPM_VCE_BIT;
	data->smu_features[GNLD_ULV].smu_feature_id =
			FEATURE_ULV_BIT;
	data->smu_features[GNLD_DPM_MP0CLK].smu_feature_id =
			FEATURE_DPM_MP0CLK_BIT;
	data->smu_features[GNLD_DPM_LINK].smu_feature_id =
			FEATURE_DPM_LINK_BIT;
	data->smu_features[GNLD_DPM_DCEFCLK].smu_feature_id =
			FEATURE_DPM_DCEFCLK_BIT;
	data->smu_features[GNLD_DS_GFXCLK].smu_feature_id =
			FEATURE_DS_GFXCLK_BIT;
	data->smu_features[GNLD_DS_SOCCLK].smu_feature_id =
			FEATURE_DS_SOCCLK_BIT;
	data->smu_features[GNLD_DS_LCLK].smu_feature_id =
			FEATURE_DS_LCLK_BIT;
	data->smu_features[GNLD_PPT].smu_feature_id =
			FEATURE_PPT_BIT;
	data->smu_features[GNLD_TDC].smu_feature_id =
			FEATURE_TDC_BIT;
	data->smu_features[GNLD_THERMAL].smu_feature_id =
			FEATURE_THERMAL_BIT;
	data->smu_features[GNLD_GFX_PER_CU_CG].smu_feature_id =
			FEATURE_GFX_PER_CU_CG_BIT;
	data->smu_features[GNLD_RM].smu_feature_id =
			FEATURE_RM_BIT;
	data->smu_features[GNLD_DS_DCEFCLK].smu_feature_id =
			FEATURE_DS_DCEFCLK_BIT;
	data->smu_features[GNLD_ACDC].smu_feature_id =
			FEATURE_ACDC_BIT;
	data->smu_features[GNLD_VR0HOT].smu_feature_id =
			FEATURE_VR0HOT_BIT;
	data->smu_features[GNLD_VR1HOT].smu_feature_id =
			FEATURE_VR1HOT_BIT;
	data->smu_features[GNLD_FW_CTF].smu_feature_id =
			FEATURE_FW_CTF_BIT;
	data->smu_features[GNLD_LED_DISPLAY].smu_feature_id =
			FEATURE_LED_DISPLAY_BIT;
	data->smu_features[GNLD_FAN_CONTROL].smu_feature_id =
			FEATURE_FAN_CONTROL_BIT;
	data->smu_features[GNLD_DIDT].smu_feature_id = FEATURE_GFX_EDC_BIT;
	data->smu_features[GNLD_GFXOFF].smu_feature_id = FEATURE_GFXOFF_BIT;
	data->smu_features[GNLD_CG].smu_feature_id = FEATURE_CG_BIT;
	data->smu_features[GNLD_DPM_FCLK].smu_feature_id = FEATURE_DPM_FCLK_BIT;
	data->smu_features[GNLD_DS_FCLK].smu_feature_id = FEATURE_DS_FCLK_BIT;
	data->smu_features[GNLD_DS_MP1CLK].smu_feature_id = FEATURE_DS_MP1CLK_BIT;
	data->smu_features[GNLD_DS_MP0CLK].smu_feature_id = FEATURE_DS_MP0CLK_BIT;
	data->smu_features[GNLD_XGMI].smu_feature_id = FEATURE_XGMI_BIT;

	for (i = 0; i < GNLD_FEATURES_MAX; i++) {
		data->smu_features[i].smu_feature_bitmap =
			(uint64_t)(1ULL << data->smu_features[i].smu_feature_id);
		data->smu_features[i].allowed =
			((data->registry_data.disallowed_features >> i) & 1) ?
			false : true;
	}
}

static int vega20_set_private_data_based_on_pptable(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int vega20_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	kfree(hwmgr->backend);
	hwmgr->backend = NULL;

	return 0;
}

static int vega20_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data;
	struct amdgpu_device *adev = hwmgr->adev;

	data = kzalloc(sizeof(struct vega20_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	hwmgr->workload_mask = 1 << hwmgr->workload_prority[PP_SMC_POWER_PROFILE_VIDEO];
	hwmgr->power_profile_mode = PP_SMC_POWER_PROFILE_VIDEO;
	hwmgr->default_power_profile_mode = PP_SMC_POWER_PROFILE_VIDEO;

	vega20_set_default_registry_data(hwmgr);

	data->disable_dpm_mask = 0xff;

	/* need to set voltage control types before EVV patching */
	data->vddc_control = VEGA20_VOLTAGE_CONTROL_NONE;
	data->mvdd_control = VEGA20_VOLTAGE_CONTROL_NONE;
	data->vddci_control = VEGA20_VOLTAGE_CONTROL_NONE;

	data->water_marks_bitmap = 0;
	data->avfs_exist = false;

	vega20_set_features_platform_caps(hwmgr);

	vega20_init_dpm_defaults(hwmgr);

	/* Parse pptable data read from VBIOS */
	vega20_set_private_data_based_on_pptable(hwmgr);

	data->is_tlu_enabled = false;

	hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
			VEGA20_MAX_HARDWARE_POWERLEVELS;
	hwmgr->platform_descriptor.hardwarePerformanceLevels = 2;
	hwmgr->platform_descriptor.minimumClocksReductionPercentage = 50;

	hwmgr->platform_descriptor.vbiosInterruptId = 0x20000400; /* IRQ_SOURCE1_SW_INT */
	/* The true clock step depends on the frequency, typically 4.5 or 9 MHz. Here we use 5. */
	hwmgr->platform_descriptor.clockStep.engineClock = 500;
	hwmgr->platform_descriptor.clockStep.memoryClock = 500;

	data->total_active_cus = adev->gfx.cu_info.number;

	return 0;
}

static int vega20_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);

	data->low_sclk_interrupt_threshold = 0;

	return 0;
}

static int vega20_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	ret = vega20_init_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to init sclk threshold!",
			return ret);

	return 0;
}

/*
 * @fn vega20_init_dpm_state
 * @brief Function to initialize all Soft Min/Max and Hard Min/Max to 0xff.
 *
 * @param    dpm_state - the address of the DPM Table to initiailize.
 * @return   None.
 */
static void vega20_init_dpm_state(struct vega20_dpm_state *dpm_state)
{
	dpm_state->soft_min_level = 0x0;
	dpm_state->soft_max_level = 0xffff;
	dpm_state->hard_min_level = 0x0;
	dpm_state->hard_max_level = 0xffff;
}

static int vega20_get_number_of_dpm_level(struct pp_hwmgr *hwmgr,
		PPCLK_e clk_id, uint32_t *num_of_levels)
{
	int ret = 0;

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetDpmFreqByIndex,
			(clk_id << 16 | 0xFF));
	PP_ASSERT_WITH_CODE(!ret,
			"[GetNumOfDpmLevel] failed to get dpm levels!",
			return ret);

	*num_of_levels = smum_get_argument(hwmgr);
	PP_ASSERT_WITH_CODE(*num_of_levels > 0,
			"[GetNumOfDpmLevel] number of clk levels is invalid!",
			return -EINVAL);

	return ret;
}

static int vega20_get_dpm_frequency_by_index(struct pp_hwmgr *hwmgr,
		PPCLK_e clk_id, uint32_t index, uint32_t *clk)
{
	int ret = 0;

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetDpmFreqByIndex,
			(clk_id << 16 | index));
	PP_ASSERT_WITH_CODE(!ret,
			"[GetDpmFreqByIndex] failed to get dpm freq by index!",
			return ret);

	*clk = smum_get_argument(hwmgr);
	PP_ASSERT_WITH_CODE(*clk,
			"[GetDpmFreqByIndex] clk value is invalid!",
			return -EINVAL);

	return ret;
}

static int vega20_setup_single_dpm_table(struct pp_hwmgr *hwmgr,
		struct vega20_single_dpm_table *dpm_table, PPCLK_e clk_id)
{
	int ret = 0;
	uint32_t i, num_of_levels, clk;

	ret = vega20_get_number_of_dpm_level(hwmgr, clk_id, &num_of_levels);
	PP_ASSERT_WITH_CODE(!ret,
			"[SetupSingleDpmTable] failed to get clk levels!",
			return ret);

	dpm_table->count = num_of_levels;

	for (i = 0; i < num_of_levels; i++) {
		ret = vega20_get_dpm_frequency_by_index(hwmgr, clk_id, i, &clk);
		PP_ASSERT_WITH_CODE(!ret,
			"[SetupSingleDpmTable] failed to get clk of specific level!",
			return ret);
		dpm_table->dpm_levels[i].value = clk;
		dpm_table->dpm_levels[i].enabled = true;
	}

	return ret;
}

static int vega20_setup_gfxclk_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table;
	int ret = 0;

	dpm_table = &(data->dpm_table.gfx_table);
	if (data->smu_features[GNLD_DPM_GFXCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_GFXCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get gfxclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.gfx_clock / 100;
	}

	return ret;
}

static int vega20_setup_memclk_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table;
	int ret = 0;

	dpm_table = &(data->dpm_table.mem_table);
	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_UCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get memclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.mem_clock / 100;
	}

	return ret;
}

/*
 * This function is to initialize all DPM state tables
 * for SMU based on the dependency table.
 * Dynamic state patching function will then trim these
 * state tables to the allowed range based
 * on the power policy or external client requests,
 * such as UVD request, etc.
 */
static int vega20_setup_default_dpm_tables(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table;
	int ret = 0;

	memset(&data->dpm_table, 0, sizeof(data->dpm_table));

	/* socclk */
	dpm_table = &(data->dpm_table.soc_table);
	if (data->smu_features[GNLD_DPM_SOCCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_SOCCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get socclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.soc_clock / 100;
	}
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* gfxclk */
	dpm_table = &(data->dpm_table.gfx_table);
	ret = vega20_setup_gfxclk_dpm_table(hwmgr);
	if (ret)
		return ret;
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* memclk */
	dpm_table = &(data->dpm_table.mem_table);
	ret = vega20_setup_memclk_dpm_table(hwmgr);
	if (ret)
		return ret;
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* eclk */
	dpm_table = &(data->dpm_table.eclk_table);
	if (data->smu_features[GNLD_DPM_VCE].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_ECLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get eclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.eclock / 100;
	}
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* vclk */
	dpm_table = &(data->dpm_table.vclk_table);
	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_VCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get vclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.vclock / 100;
	}
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* dclk */
	dpm_table = &(data->dpm_table.dclk_table);
	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_DCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get dclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.dclock / 100;
	}
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* dcefclk */
	dpm_table = &(data->dpm_table.dcef_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_DCEFCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get dcefclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.dcef_clock / 100;
	}
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* pixclk */
	dpm_table = &(data->dpm_table.pixel_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_PIXCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get pixclk dpm levels!",
				return ret);
	} else
		dpm_table->count = 0;
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* dispclk */
	dpm_table = &(data->dpm_table.display_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_DISPCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get dispclk dpm levels!",
				return ret);
	} else
		dpm_table->count = 0;
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* phyclk */
	dpm_table = &(data->dpm_table.phy_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_PHYCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get phyclk dpm levels!",
				return ret);
	} else
		dpm_table->count = 0;
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* fclk */
	dpm_table = &(data->dpm_table.fclk_table);
	if (data->smu_features[GNLD_DPM_FCLK].enabled) {
		ret = vega20_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_FCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get fclk dpm levels!",
				return ret);
	} else
		dpm_table->count = 0;
	vega20_init_dpm_state(&(dpm_table->dpm_state));

	/* save a copy of the default DPM table */
	memcpy(&(data->golden_dpm_table), &(data->dpm_table),
			sizeof(struct vega20_dpm_table));

	return 0;
}

/**
* Initializes the SMC table and uploads it
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput  the pointer to input data (PowerState)
* @return   always 0
*/
static int vega20_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct pp_atomfwctrl_bios_boot_up_values boot_up_values;
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;

	result = pp_atomfwctrl_get_vbios_bootup_values(hwmgr, &boot_up_values);
	PP_ASSERT_WITH_CODE(!result,
			"[InitSMCTable] Failed to get vbios bootup values!",
			return result);

	data->vbios_boot_state.vddc     = boot_up_values.usVddc;
	data->vbios_boot_state.vddci    = boot_up_values.usVddci;
	data->vbios_boot_state.mvddc    = boot_up_values.usMvddc;
	data->vbios_boot_state.gfx_clock = boot_up_values.ulGfxClk;
	data->vbios_boot_state.mem_clock = boot_up_values.ulUClk;
	data->vbios_boot_state.soc_clock = boot_up_values.ulSocClk;
	data->vbios_boot_state.dcef_clock = boot_up_values.ulDCEFClk;
	data->vbios_boot_state.eclock = boot_up_values.ulEClk;
	data->vbios_boot_state.vclock = boot_up_values.ulVClk;
	data->vbios_boot_state.dclock = boot_up_values.ulDClk;
	data->vbios_boot_state.uc_cooling_id = boot_up_values.ucCoolingID;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetMinDeepSleepDcefclk,
		(uint32_t)(data->vbios_boot_state.dcef_clock / 100));

	memcpy(pp_table, pptable_information->smc_pptable, sizeof(PPTable_t));

	result = smum_smc_table_manager(hwmgr,
					(uint8_t *)pp_table, TABLE_PPTABLE, false);
	PP_ASSERT_WITH_CODE(!result,
			"[InitSMCTable] Failed to upload PPtable!",
			return result);

	return 0;
}

static int vega20_set_allowed_featuresmask(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t allowed_features_low = 0, allowed_features_high = 0;
	int i;
	int ret = 0;

	for (i = 0; i < GNLD_FEATURES_MAX; i++)
		if (data->smu_features[i].allowed)
			data->smu_features[i].smu_feature_id > 31 ?
				(allowed_features_high |=
				 ((data->smu_features[i].smu_feature_bitmap >> SMU_FEATURES_HIGH_SHIFT)
				  & 0xFFFFFFFF)) :
				(allowed_features_low |=
				 ((data->smu_features[i].smu_feature_bitmap >> SMU_FEATURES_LOW_SHIFT)
				  & 0xFFFFFFFF));

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
		PPSMC_MSG_SetAllowedFeaturesMaskHigh, allowed_features_high);
	PP_ASSERT_WITH_CODE(!ret,
		"[SetAllowedFeaturesMask] Attempt to set allowed features mask(high) failed!",
		return ret);

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
		PPSMC_MSG_SetAllowedFeaturesMaskLow, allowed_features_low);
	PP_ASSERT_WITH_CODE(!ret,
		"[SetAllowedFeaturesMask] Attempt to set allowed features mask (low) failed!",
		return ret);

	return 0;
}

static int vega20_run_btc_afll(struct pp_hwmgr *hwmgr)
{
	return smum_send_msg_to_smc(hwmgr, PPSMC_MSG_RunAfllBtc);
}

static int vega20_enable_all_smu_features(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint64_t features_enabled;
	int i;
	bool enabled;
	int ret = 0;

	PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc(hwmgr,
			PPSMC_MSG_EnableAllSmuFeatures)) == 0,
			"[EnableAllSMUFeatures] Failed to enable all smu features!",
			return ret);

	ret = vega20_get_enabled_smc_features(hwmgr, &features_enabled);
	PP_ASSERT_WITH_CODE(!ret,
			"[EnableAllSmuFeatures] Failed to get enabled smc features!",
			return ret);

	for (i = 0; i < GNLD_FEATURES_MAX; i++) {
		enabled = (features_enabled & data->smu_features[i].smu_feature_bitmap) ?
			true : false;
		data->smu_features[i].enabled = enabled;
		data->smu_features[i].supported = enabled;

#if 0
		if (data->smu_features[i].allowed && !enabled)
			pr_info("[EnableAllSMUFeatures] feature %d is expected enabled!", i);
		else if (!data->smu_features[i].allowed && enabled)
			pr_info("[EnableAllSMUFeatures] feature %d is expected disabled!", i);
#endif
	}

	return 0;
}

static int vega20_notify_smc_display_change(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_DPM_UCLK].enabled)
		return smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetUclkFastSwitch,
			1);

	return 0;
}

static int vega20_send_clock_ratio(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);

	return smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetFclkGfxClkRatio,
			data->registry_data.fclk_gfxclk_ratio);
}

static int vega20_disable_all_smu_features(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint64_t features_enabled;
	int i;
	bool enabled;
	int ret = 0;

	PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc(hwmgr,
			PPSMC_MSG_DisableAllSmuFeatures)) == 0,
			"[DisableAllSMUFeatures] Failed to disable all smu features!",
			return ret);

	ret = vega20_get_enabled_smc_features(hwmgr, &features_enabled);
	PP_ASSERT_WITH_CODE(!ret,
			"[DisableAllSMUFeatures] Failed to get enabled smc features!",
			return ret);

	for (i = 0; i < GNLD_FEATURES_MAX; i++) {
		enabled = (features_enabled & data->smu_features[i].smu_feature_bitmap) ?
			true : false;
		data->smu_features[i].enabled = enabled;
		data->smu_features[i].supported = enabled;
	}

	return 0;
}

static int vega20_od8_set_feature_capabilities(
		struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega20_od8_settings *od_settings = &(data->od8_settings);

	od_settings->overdrive8_capabilities = 0;

	if (data->smu_features[GNLD_DPM_GFXCLK].enabled) {
		if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_GFXCLK_LIMITS] &&
		    pptable_information->od_settings_max[OD8_SETTING_GFXCLK_FMAX] > 0 &&
		    pptable_information->od_settings_min[OD8_SETTING_GFXCLK_FMIN] > 0 &&
		    (pptable_information->od_settings_max[OD8_SETTING_GFXCLK_FMAX] >=
		    pptable_information->od_settings_min[OD8_SETTING_GFXCLK_FMIN]))
			od_settings->overdrive8_capabilities |= OD8_GFXCLK_LIMITS;

		if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_GFXCLK_CURVE] &&
		    (pptable_information->od_settings_min[OD8_SETTING_GFXCLK_VOLTAGE1] >=
		     pp_table->MinVoltageGfx / VOLTAGE_SCALE) &&
		    (pptable_information->od_settings_max[OD8_SETTING_GFXCLK_VOLTAGE3] <=
		     pp_table->MaxVoltageGfx / VOLTAGE_SCALE) &&
		    (pptable_information->od_settings_max[OD8_SETTING_GFXCLK_VOLTAGE3] >=
		     pptable_information->od_settings_min[OD8_SETTING_GFXCLK_VOLTAGE1]))
			od_settings->overdrive8_capabilities |= OD8_GFXCLK_CURVE;
	}

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_UCLK_MAX] &&
		    pptable_information->od_settings_min[OD8_SETTING_UCLK_FMAX] > 0 &&
		    pptable_information->od_settings_max[OD8_SETTING_UCLK_FMAX] > 0 &&
		    (pptable_information->od_settings_max[OD8_SETTING_UCLK_FMAX] >=
		    pptable_information->od_settings_min[OD8_SETTING_UCLK_FMAX]))
			od_settings->overdrive8_capabilities |= OD8_UCLK_MAX;
	}

	if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_POWER_LIMIT] &&
	    pptable_information->od_settings_max[OD8_SETTING_POWER_PERCENTAGE] > 0 &&
	    pptable_information->od_settings_max[OD8_SETTING_POWER_PERCENTAGE] <= 100 &&
	    pptable_information->od_settings_min[OD8_SETTING_POWER_PERCENTAGE] > 0 &&
	    pptable_information->od_settings_min[OD8_SETTING_POWER_PERCENTAGE] <= 100)
		od_settings->overdrive8_capabilities |= OD8_POWER_LIMIT;

	if (data->smu_features[GNLD_FAN_CONTROL].enabled) {
		if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_FAN_ACOUSTIC_LIMIT] &&
		    pptable_information->od_settings_min[OD8_SETTING_FAN_ACOUSTIC_LIMIT] > 0 &&
		    pptable_information->od_settings_max[OD8_SETTING_FAN_ACOUSTIC_LIMIT] > 0 &&
		    (pptable_information->od_settings_max[OD8_SETTING_FAN_ACOUSTIC_LIMIT] >=
		     pptable_information->od_settings_min[OD8_SETTING_FAN_ACOUSTIC_LIMIT]))
			od_settings->overdrive8_capabilities |= OD8_ACOUSTIC_LIMIT_SCLK;

		if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_FAN_SPEED_MIN] &&
		    (pptable_information->od_settings_min[OD8_SETTING_FAN_MIN_SPEED] >=
		    (pp_table->FanPwmMin * pp_table->FanMaximumRpm / 100)) &&
		    pptable_information->od_settings_max[OD8_SETTING_FAN_MIN_SPEED] > 0 &&
		    (pptable_information->od_settings_max[OD8_SETTING_FAN_MIN_SPEED] >=
		     pptable_information->od_settings_min[OD8_SETTING_FAN_MIN_SPEED]))
			od_settings->overdrive8_capabilities |= OD8_FAN_SPEED_MIN;
	}

	if (data->smu_features[GNLD_THERMAL].enabled) {
		if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_TEMPERATURE_FAN] &&
		    pptable_information->od_settings_max[OD8_SETTING_FAN_TARGET_TEMP] > 0 &&
		    pptable_information->od_settings_min[OD8_SETTING_FAN_TARGET_TEMP] > 0 &&
		    (pptable_information->od_settings_max[OD8_SETTING_FAN_TARGET_TEMP] >=
		     pptable_information->od_settings_min[OD8_SETTING_FAN_TARGET_TEMP]))
			od_settings->overdrive8_capabilities |= OD8_TEMPERATURE_FAN;

		if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_TEMPERATURE_SYSTEM] &&
		    pptable_information->od_settings_max[OD8_SETTING_OPERATING_TEMP_MAX] > 0 &&
		    pptable_information->od_settings_min[OD8_SETTING_OPERATING_TEMP_MAX] > 0 &&
		    (pptable_information->od_settings_max[OD8_SETTING_OPERATING_TEMP_MAX] >=
		     pptable_information->od_settings_min[OD8_SETTING_OPERATING_TEMP_MAX]))
			od_settings->overdrive8_capabilities |= OD8_TEMPERATURE_SYSTEM;
	}

	if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_MEMORY_TIMING_TUNE])
		od_settings->overdrive8_capabilities |= OD8_MEMORY_TIMING_TUNE;

	if (pptable_information->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_FAN_ZERO_RPM_CONTROL] &&
	    pp_table->FanZeroRpmEnable)
		od_settings->overdrive8_capabilities |= OD8_FAN_ZERO_RPM_CONTROL;

	return 0;
}

static int vega20_od8_set_feature_id(
		struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_od8_settings *od_settings = &(data->od8_settings);

	if (od_settings->overdrive8_capabilities & OD8_GFXCLK_LIMITS) {
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id =
			OD8_GFXCLK_LIMITS;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id =
			OD8_GFXCLK_LIMITS;
	} else {
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id =
			0;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id =
			0;
	}

	if (od_settings->overdrive8_capabilities & OD8_GFXCLK_CURVE) {
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].feature_id =
			OD8_GFXCLK_CURVE;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id =
			OD8_GFXCLK_CURVE;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].feature_id =
			OD8_GFXCLK_CURVE;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id =
			OD8_GFXCLK_CURVE;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].feature_id =
			OD8_GFXCLK_CURVE;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id =
			OD8_GFXCLK_CURVE;
	} else {
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].feature_id =
			0;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id =
			0;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].feature_id =
			0;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id =
			0;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].feature_id =
			0;
		od_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id =
			0;
	}

	if (od_settings->overdrive8_capabilities & OD8_UCLK_MAX)
		od_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id = OD8_UCLK_MAX;
	else
		od_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id = 0;

	if (od_settings->overdrive8_capabilities & OD8_POWER_LIMIT)
		od_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].feature_id = OD8_POWER_LIMIT;
	else
		od_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].feature_id = 0;

	if (od_settings->overdrive8_capabilities & OD8_ACOUSTIC_LIMIT_SCLK)
		od_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].feature_id =
			OD8_ACOUSTIC_LIMIT_SCLK;
	else
		od_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].feature_id =
			0;

	if (od_settings->overdrive8_capabilities & OD8_FAN_SPEED_MIN)
		od_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].feature_id =
			OD8_FAN_SPEED_MIN;
	else
		od_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].feature_id =
			0;

	if (od_settings->overdrive8_capabilities & OD8_TEMPERATURE_FAN)
		od_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].feature_id =
			OD8_TEMPERATURE_FAN;
	else
		od_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].feature_id =
			0;

	if (od_settings->overdrive8_capabilities & OD8_TEMPERATURE_SYSTEM)
		od_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].feature_id =
			OD8_TEMPERATURE_SYSTEM;
	else
		od_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].feature_id =
			0;

	return 0;
}

static int vega20_od8_get_gfx_clock_base_voltage(
		struct pp_hwmgr *hwmgr,
		uint32_t *voltage,
		uint32_t freq)
{
	int ret = 0;

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetAVFSVoltageByDpm,
			((AVFS_CURVE << 24) | (OD8_HOTCURVE_TEMPERATURE << 16) | freq));
	PP_ASSERT_WITH_CODE(!ret,
			"[GetBaseVoltage] failed to get GFXCLK AVFS voltage from SMU!",
			return ret);

	*voltage = smum_get_argument(hwmgr);
	*voltage = *voltage / VOLTAGE_SCALE;

	return 0;
}

static int vega20_od8_initialize_default_settings(
		struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_od8_settings *od8_settings = &(data->od8_settings);
	OverDriveTable_t *od_table = &(data->smc_state_table.overdrive_table);
	int i, ret = 0;

	/* Set Feature Capabilities */
	vega20_od8_set_feature_capabilities(hwmgr);

	/* Map FeatureID to individual settings */
	vega20_od8_set_feature_id(hwmgr);

	/* Set default values */
	ret = smum_smc_table_manager(hwmgr, (uint8_t *)od_table, TABLE_OVERDRIVE, true);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to export over drive table!",
			return ret);

	if (od8_settings->overdrive8_capabilities & OD8_GFXCLK_LIMITS) {
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].default_value =
			od_table->GfxclkFmin;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].default_value =
			od_table->GfxclkFmax;
	} else {
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].default_value =
			0;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].default_value =
			0;
	}

	if (od8_settings->overdrive8_capabilities & OD8_GFXCLK_CURVE) {
		od_table->GfxclkFreq1 = od_table->GfxclkFmin;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].default_value =
			od_table->GfxclkFreq1;

		od_table->GfxclkFreq3 = od_table->GfxclkFmax;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].default_value =
			od_table->GfxclkFreq3;

		od_table->GfxclkFreq2 = (od_table->GfxclkFreq1 + od_table->GfxclkFreq3) / 2;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].default_value =
			od_table->GfxclkFreq2;

		PP_ASSERT_WITH_CODE(!vega20_od8_get_gfx_clock_base_voltage(hwmgr,
				   &(od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].default_value),
				     od_table->GfxclkFreq1),
				"[PhwVega20_OD8_InitializeDefaultSettings] Failed to get Base clock voltage from SMU!",
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].default_value = 0);
		od_table->GfxclkVolt1 = od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].default_value
			* VOLTAGE_SCALE;

		PP_ASSERT_WITH_CODE(!vega20_od8_get_gfx_clock_base_voltage(hwmgr,
				   &(od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].default_value),
				     od_table->GfxclkFreq2),
				"[PhwVega20_OD8_InitializeDefaultSettings] Failed to get Base clock voltage from SMU!",
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].default_value = 0);
		od_table->GfxclkVolt2 = od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].default_value
			* VOLTAGE_SCALE;

		PP_ASSERT_WITH_CODE(!vega20_od8_get_gfx_clock_base_voltage(hwmgr,
				   &(od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].default_value),
				     od_table->GfxclkFreq3),
				"[PhwVega20_OD8_InitializeDefaultSettings] Failed to get Base clock voltage from SMU!",
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].default_value = 0);
		od_table->GfxclkVolt3 = od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].default_value
			* VOLTAGE_SCALE;
	} else {
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].default_value =
			0;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].default_value =
			0;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].default_value =
			0;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].default_value =
			0;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].default_value =
			0;
		od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].default_value =
			0;
	}

	if (od8_settings->overdrive8_capabilities & OD8_UCLK_MAX)
		od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].default_value =
			od_table->UclkFmax;
	else
		od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].default_value =
			0;

	if (od8_settings->overdrive8_capabilities & OD8_POWER_LIMIT)
		od8_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].default_value =
			od_table->OverDrivePct;
	else
		od8_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].default_value =
			0;

	if (od8_settings->overdrive8_capabilities & OD8_ACOUSTIC_LIMIT_SCLK)
		od8_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].default_value =
			od_table->FanMaximumRpm;
	else
		od8_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].default_value =
			0;

	if (od8_settings->overdrive8_capabilities & OD8_FAN_SPEED_MIN)
		od8_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].default_value =
			od_table->FanMinimumPwm * data->smc_state_table.pp_table.FanMaximumRpm / 100;
	else
		od8_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].default_value =
			0;

	if (od8_settings->overdrive8_capabilities & OD8_TEMPERATURE_FAN)
		od8_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].default_value =
			od_table->FanTargetTemperature;
	else
		od8_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].default_value =
			0;

	if (od8_settings->overdrive8_capabilities & OD8_TEMPERATURE_SYSTEM)
		od8_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].default_value =
			od_table->MaxOpTemp;
	else
		od8_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].default_value =
			0;

	for (i = 0; i < OD8_SETTING_COUNT; i++) {
		if (od8_settings->od8_settings_array[i].feature_id) {
			od8_settings->od8_settings_array[i].min_value =
				pptable_information->od_settings_min[i];
			od8_settings->od8_settings_array[i].max_value =
				pptable_information->od_settings_max[i];
			od8_settings->od8_settings_array[i].current_value =
				od8_settings->od8_settings_array[i].default_value;
		} else {
			od8_settings->od8_settings_array[i].min_value =
				0;
			od8_settings->od8_settings_array[i].max_value =
				0;
			od8_settings->od8_settings_array[i].current_value =
				0;
		}
	}

	ret = smum_smc_table_manager(hwmgr, (uint8_t *)od_table, TABLE_OVERDRIVE, false);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to import over drive table!",
			return ret);

	return 0;
}

static int vega20_od8_set_settings(
		struct pp_hwmgr *hwmgr,
		uint32_t index,
		uint32_t value)
{
	OverDriveTable_t od_table;
	int ret = 0;
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_od8_single_setting *od8_settings =
			data->od8_settings.od8_settings_array;

	ret = smum_smc_table_manager(hwmgr, (uint8_t *)(&od_table), TABLE_OVERDRIVE, true);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to export over drive table!",
			return ret);

	switch(index) {
	case OD8_SETTING_GFXCLK_FMIN:
		od_table.GfxclkFmin = (uint16_t)value;
		break;
	case OD8_SETTING_GFXCLK_FMAX:
		if (value < od8_settings[OD8_SETTING_GFXCLK_FMAX].min_value ||
		    value > od8_settings[OD8_SETTING_GFXCLK_FMAX].max_value)
			return -EINVAL;

		od_table.GfxclkFmax = (uint16_t)value;
		break;
	case OD8_SETTING_GFXCLK_FREQ1:
		od_table.GfxclkFreq1 = (uint16_t)value;
		break;
	case OD8_SETTING_GFXCLK_VOLTAGE1:
		od_table.GfxclkVolt1 = (uint16_t)value;
		break;
	case OD8_SETTING_GFXCLK_FREQ2:
		od_table.GfxclkFreq2 = (uint16_t)value;
		break;
	case OD8_SETTING_GFXCLK_VOLTAGE2:
		od_table.GfxclkVolt2 = (uint16_t)value;
		break;
	case OD8_SETTING_GFXCLK_FREQ3:
		od_table.GfxclkFreq3 = (uint16_t)value;
		break;
	case OD8_SETTING_GFXCLK_VOLTAGE3:
		od_table.GfxclkVolt3 = (uint16_t)value;
		break;
	case OD8_SETTING_UCLK_FMAX:
		if (value < od8_settings[OD8_SETTING_UCLK_FMAX].min_value ||
		    value > od8_settings[OD8_SETTING_UCLK_FMAX].max_value)
			return -EINVAL;
		od_table.UclkFmax = (uint16_t)value;
		break;
	case OD8_SETTING_POWER_PERCENTAGE:
		od_table.OverDrivePct = (int16_t)value;
		break;
	case OD8_SETTING_FAN_ACOUSTIC_LIMIT:
		od_table.FanMaximumRpm = (uint16_t)value;
		break;
	case OD8_SETTING_FAN_MIN_SPEED:
		od_table.FanMinimumPwm = (uint16_t)value;
		break;
	case OD8_SETTING_FAN_TARGET_TEMP:
		od_table.FanTargetTemperature = (uint16_t)value;
		break;
	case OD8_SETTING_OPERATING_TEMP_MAX:
		od_table.MaxOpTemp = (uint16_t)value;
		break;
	}

	ret = smum_smc_table_manager(hwmgr, (uint8_t *)(&od_table), TABLE_OVERDRIVE, false);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to import over drive table!",
			return ret);

	return 0;
}

static int vega20_get_sclk_od(
		struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = hwmgr->backend;
	struct vega20_single_dpm_table *sclk_table =
			&(data->dpm_table.gfx_table);
	struct vega20_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.gfx_table);
	int value = sclk_table->dpm_levels[sclk_table->count - 1].value;
	int golden_value = golden_sclk_table->dpm_levels
			[golden_sclk_table->count - 1].value;

	/* od percentage */
	value -= golden_value;
	value = DIV_ROUND_UP(value * 100, golden_value);

	return value;
}

static int vega20_set_sclk_od(
		struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct vega20_hwmgr *data = hwmgr->backend;
	struct vega20_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.gfx_table);
	uint32_t od_sclk;
	int ret = 0;

	od_sclk = golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value * value;
	od_sclk /= 100;
	od_sclk += golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value;

	ret = vega20_od8_set_settings(hwmgr, OD8_SETTING_GFXCLK_FMAX, od_sclk);
	PP_ASSERT_WITH_CODE(!ret,
			"[SetSclkOD] failed to set od gfxclk!",
			return ret);

	/* retrieve updated gfxclk table */
	ret = vega20_setup_gfxclk_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"[SetSclkOD] failed to refresh gfxclk table!",
			return ret);

	return 0;
}

static int vega20_get_mclk_od(
		struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = hwmgr->backend;
	struct vega20_single_dpm_table *mclk_table =
			&(data->dpm_table.mem_table);
	struct vega20_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mem_table);
	int value = mclk_table->dpm_levels[mclk_table->count - 1].value;
	int golden_value = golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value;

	/* od percentage */
	value -= golden_value;
	value = DIV_ROUND_UP(value * 100, golden_value);

	return value;
}

static int vega20_set_mclk_od(
		struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct vega20_hwmgr *data = hwmgr->backend;
	struct vega20_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mem_table);
	uint32_t od_mclk;
	int ret = 0;

	od_mclk = golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value * value;
	od_mclk /= 100;
	od_mclk += golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value;

	ret = vega20_od8_set_settings(hwmgr, OD8_SETTING_UCLK_FMAX, od_mclk);
	PP_ASSERT_WITH_CODE(!ret,
			"[SetMclkOD] failed to set od memclk!",
			return ret);

	/* retrieve updated memclk table */
	ret = vega20_setup_memclk_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"[SetMclkOD] failed to refresh memclk table!",
			return ret);

	return 0;
}

static int vega20_populate_umdpstate_clocks(
		struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *gfx_table = &(data->dpm_table.gfx_table);
	struct vega20_single_dpm_table *mem_table = &(data->dpm_table.mem_table);

	hwmgr->pstate_sclk = gfx_table->dpm_levels[0].value;
	hwmgr->pstate_mclk = mem_table->dpm_levels[0].value;

	if (gfx_table->count > VEGA20_UMD_PSTATE_GFXCLK_LEVEL &&
	    mem_table->count > VEGA20_UMD_PSTATE_MCLK_LEVEL) {
		hwmgr->pstate_sclk = gfx_table->dpm_levels[VEGA20_UMD_PSTATE_GFXCLK_LEVEL].value;
		hwmgr->pstate_mclk = mem_table->dpm_levels[VEGA20_UMD_PSTATE_MCLK_LEVEL].value;
	}

	hwmgr->pstate_sclk = hwmgr->pstate_sclk * 100;
	hwmgr->pstate_mclk = hwmgr->pstate_mclk * 100;

	return 0;
}

static int vega20_get_max_sustainable_clock(struct pp_hwmgr *hwmgr,
		PP_Clock *clock, PPCLK_e clock_select)
{
	int ret = 0;

	PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetDcModeMaxDpmFreq,
			(clock_select << 16))) == 0,
			"[GetMaxSustainableClock] Failed to get max DC clock from SMC!",
			return ret);
	*clock = smum_get_argument(hwmgr);

	/* if DC limit is zero, return AC limit */
	if (*clock == 0) {
		PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetMaxDpmFreq,
			(clock_select << 16))) == 0,
			"[GetMaxSustainableClock] failed to get max AC clock from SMC!",
			return ret);
		*clock = smum_get_argument(hwmgr);
	}

	return 0;
}

static int vega20_init_max_sustainable_clocks(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
		(struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_max_sustainable_clocks *max_sustainable_clocks =
		&(data->max_sustainable_clocks);
	int ret = 0;

	max_sustainable_clocks->uclock = data->vbios_boot_state.mem_clock / 100;
	max_sustainable_clocks->soc_clock = data->vbios_boot_state.soc_clock / 100;
	max_sustainable_clocks->dcef_clock = data->vbios_boot_state.dcef_clock / 100;
	max_sustainable_clocks->display_clock = 0xFFFFFFFF;
	max_sustainable_clocks->phy_clock = 0xFFFFFFFF;
	max_sustainable_clocks->pixel_clock = 0xFFFFFFFF;

	if (data->smu_features[GNLD_DPM_UCLK].enabled)
		PP_ASSERT_WITH_CODE((ret = vega20_get_max_sustainable_clock(hwmgr,
				&(max_sustainable_clocks->uclock),
				PPCLK_UCLK)) == 0,
				"[InitMaxSustainableClocks] failed to get max UCLK from SMC!",
				return ret);

	if (data->smu_features[GNLD_DPM_SOCCLK].enabled)
		PP_ASSERT_WITH_CODE((ret = vega20_get_max_sustainable_clock(hwmgr,
				&(max_sustainable_clocks->soc_clock),
				PPCLK_SOCCLK)) == 0,
				"[InitMaxSustainableClocks] failed to get max SOCCLK from SMC!",
				return ret);

	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		PP_ASSERT_WITH_CODE((ret = vega20_get_max_sustainable_clock(hwmgr,
				&(max_sustainable_clocks->dcef_clock),
				PPCLK_DCEFCLK)) == 0,
				"[InitMaxSustainableClocks] failed to get max DCEFCLK from SMC!",
				return ret);
		PP_ASSERT_WITH_CODE((ret = vega20_get_max_sustainable_clock(hwmgr,
				&(max_sustainable_clocks->display_clock),
				PPCLK_DISPCLK)) == 0,
				"[InitMaxSustainableClocks] failed to get max DISPCLK from SMC!",
				return ret);
		PP_ASSERT_WITH_CODE((ret = vega20_get_max_sustainable_clock(hwmgr,
				&(max_sustainable_clocks->phy_clock),
				PPCLK_PHYCLK)) == 0,
				"[InitMaxSustainableClocks] failed to get max PHYCLK from SMC!",
				return ret);
		PP_ASSERT_WITH_CODE((ret = vega20_get_max_sustainable_clock(hwmgr,
				&(max_sustainable_clocks->pixel_clock),
				PPCLK_PIXCLK)) == 0,
				"[InitMaxSustainableClocks] failed to get max PIXCLK from SMC!",
				return ret);
	}

	if (max_sustainable_clocks->soc_clock < max_sustainable_clocks->uclock)
		max_sustainable_clocks->uclock = max_sustainable_clocks->soc_clock;

	return 0;
}

static int vega20_enable_mgpu_fan_boost(struct pp_hwmgr *hwmgr)
{
	int result;

	result = smum_send_msg_to_smc(hwmgr,
		PPSMC_MSG_SetMGpuFanBoostLimitRpm);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableMgpuFan] Failed to enable mgpu fan boost!",
			return result);

	return 0;
}

static void vega20_init_powergate_state(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
		(struct vega20_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = true;
	data->vce_power_gated = true;

	if (data->smu_features[GNLD_DPM_UVD].enabled)
		data->uvd_power_gated = false;

	if (data->smu_features[GNLD_DPM_VCE].enabled)
		data->vce_power_gated = false;
}

static int vega20_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int result = 0;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, 0);

	result = vega20_set_allowed_featuresmask(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to set allowed featuresmask!\n",
			return result);

	result = vega20_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to initialize SMC table!",
			return result);

	result = vega20_run_btc_afll(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to run btc afll!",
			return result);

	result = vega20_enable_all_smu_features(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to enable all smu features!",
			return result);

	result = vega20_notify_smc_display_change(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to notify smc display change!",
			return result);

	result = vega20_send_clock_ratio(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to send clock ratio!",
			return result);

	/* Initialize UVD/VCE powergating state */
	vega20_init_powergate_state(hwmgr);

	result = vega20_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to setup default DPM tables!",
			return result);

	result = vega20_init_max_sustainable_clocks(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to get maximum sustainable clocks!",
			return result);

	result = vega20_power_control_set_level(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to power control set level!",
			return result);

	result = vega20_od8_initialize_default_settings(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to initialize odn settings!",
			return result);

	result = vega20_populate_umdpstate_clocks(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to populate umdpstate clocks!",
			return result);

	result = smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetPptLimit,
			POWER_SOURCE_AC << 16);
	PP_ASSERT_WITH_CODE(!result,
			"[GetPptLimit] get default PPT limit failed!",
			return result);
	hwmgr->power_limit =
		hwmgr->default_power_limit = smum_get_argument(hwmgr);

	return 0;
}

static uint32_t vega20_find_lowest_dpm_level(
		struct vega20_single_dpm_table *table)
{
	uint32_t i;

	for (i = 0; i < table->count; i++) {
		if (table->dpm_levels[i].enabled)
			break;
	}
	if (i >= table->count) {
		i = 0;
		table->dpm_levels[i].enabled = true;
	}

	return i;
}

static uint32_t vega20_find_highest_dpm_level(
		struct vega20_single_dpm_table *table)
{
	int i = 0;

	PP_ASSERT_WITH_CODE(table != NULL,
			"[FindHighestDPMLevel] DPM Table does not exist!",
			return 0);
	PP_ASSERT_WITH_CODE(table->count > 0,
			"[FindHighestDPMLevel] DPM Table has no entry!",
			return 0);
	PP_ASSERT_WITH_CODE(table->count <= MAX_REGULAR_DPM_NUMBER,
			"[FindHighestDPMLevel] DPM Table has too many entries!",
			return MAX_REGULAR_DPM_NUMBER - 1);

	for (i = table->count - 1; i >= 0; i--) {
		if (table->dpm_levels[i].enabled)
			break;
	}
	if (i < 0) {
		i = 0;
		table->dpm_levels[i].enabled = true;
	}

	return i;
}

static int vega20_upload_dpm_min_level(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t min_freq;
	int ret = 0;

	if (data->smu_features[GNLD_DPM_GFXCLK].enabled) {
		min_freq = data->dpm_table.gfx_table.dpm_state.soft_min_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_GFXCLK << 16) | (min_freq & 0xffff))),
					"Failed to set soft min gfxclk !",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		min_freq = data->dpm_table.mem_table.dpm_state.soft_min_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_UCLK << 16) | (min_freq & 0xffff))),
					"Failed to set soft min memclk !",
					return ret);

		min_freq = data->dpm_table.mem_table.dpm_state.hard_min_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetHardMinByFreq,
					(PPCLK_UCLK << 16) | (min_freq & 0xffff))),
					"Failed to set hard min memclk !",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		min_freq = data->dpm_table.vclk_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_VCLK << 16) | (min_freq & 0xffff))),
					"Failed to set soft min vclk!",
					return ret);

		min_freq = data->dpm_table.dclk_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_DCLK << 16) | (min_freq & 0xffff))),
					"Failed to set soft min dclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_VCE].enabled) {
		min_freq = data->dpm_table.eclk_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_ECLK << 16) | (min_freq & 0xffff))),
					"Failed to set soft min eclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_SOCCLK].enabled) {
		min_freq = data->dpm_table.soc_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_SOCCLK << 16) | (min_freq & 0xffff))),
					"Failed to set soft min socclk!",
					return ret);
	}

	return ret;
}

static int vega20_upload_dpm_max_level(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t max_freq;
	int ret = 0;

	if (data->smu_features[GNLD_DPM_GFXCLK].enabled) {
		max_freq = data->dpm_table.gfx_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_GFXCLK << 16) | (max_freq & 0xffff))),
					"Failed to set soft max gfxclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		max_freq = data->dpm_table.mem_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_UCLK << 16) | (max_freq & 0xffff))),
					"Failed to set soft max memclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		max_freq = data->dpm_table.vclk_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_VCLK << 16) | (max_freq & 0xffff))),
					"Failed to set soft max vclk!",
					return ret);

		max_freq = data->dpm_table.dclk_table.dpm_state.soft_max_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_DCLK << 16) | (max_freq & 0xffff))),
					"Failed to set soft max dclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_VCE].enabled) {
		max_freq = data->dpm_table.eclk_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_ECLK << 16) | (max_freq & 0xffff))),
					"Failed to set soft max eclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_SOCCLK].enabled) {
		max_freq = data->dpm_table.soc_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_SOCCLK << 16) | (max_freq & 0xffff))),
					"Failed to set soft max socclk!",
					return ret);
	}

	return ret;
}

int vega20_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	int ret = 0;

	if (data->smu_features[GNLD_DPM_VCE].supported) {
		if (data->smu_features[GNLD_DPM_VCE].enabled == enable) {
			if (enable)
				PP_DBG_LOG("[EnableDisableVCEDPM] feature VCE DPM already enabled!\n");
			else
				PP_DBG_LOG("[EnableDisableVCEDPM] feature VCE DPM already disabled!\n");
		}

		ret = vega20_enable_smc_features(hwmgr,
				enable,
				data->smu_features[GNLD_DPM_VCE].smu_feature_bitmap);
		PP_ASSERT_WITH_CODE(!ret,
				"Attempt to Enable/Disable DPM VCE Failed!",
				return ret);
		data->smu_features[GNLD_DPM_VCE].enabled = enable;
	}

	return 0;
}

static int vega20_get_clock_ranges(struct pp_hwmgr *hwmgr,
		uint32_t *clock,
		PPCLK_e clock_select,
		bool max)
{
	int ret;
	*clock = 0;

	if (max) {
		PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_GetMaxDpmFreq, (clock_select << 16))) == 0,
				"[GetClockRanges] Failed to get max clock from SMC!",
				return ret);
		*clock = smum_get_argument(hwmgr);
	} else {
		PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_GetMinDpmFreq,
				(clock_select << 16))) == 0,
				"[GetClockRanges] Failed to get min clock from SMC!",
				return ret);
		*clock = smum_get_argument(hwmgr);
	}

	return 0;
}

static uint32_t vega20_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t gfx_clk;
	int ret = 0;

	PP_ASSERT_WITH_CODE(data->smu_features[GNLD_DPM_GFXCLK].enabled,
			"[GetSclks]: gfxclk dpm not enabled!\n",
			return -EPERM);

	if (low) {
		ret = vega20_get_clock_ranges(hwmgr, &gfx_clk, PPCLK_GFXCLK, false);
		PP_ASSERT_WITH_CODE(!ret,
			"[GetSclks]: fail to get min PPCLK_GFXCLK\n",
			return ret);
	} else {
		ret = vega20_get_clock_ranges(hwmgr, &gfx_clk, PPCLK_GFXCLK, true);
		PP_ASSERT_WITH_CODE(!ret,
			"[GetSclks]: fail to get max PPCLK_GFXCLK\n",
			return ret);
	}

	return (gfx_clk * 100);
}

static uint32_t vega20_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t mem_clk;
	int ret = 0;

	PP_ASSERT_WITH_CODE(data->smu_features[GNLD_DPM_UCLK].enabled,
			"[MemMclks]: memclk dpm not enabled!\n",
			return -EPERM);

	if (low) {
		ret = vega20_get_clock_ranges(hwmgr, &mem_clk, PPCLK_UCLK, false);
		PP_ASSERT_WITH_CODE(!ret,
			"[GetMclks]: fail to get min PPCLK_UCLK\n",
			return ret);
	} else {
		ret = vega20_get_clock_ranges(hwmgr, &mem_clk, PPCLK_UCLK, true);
		PP_ASSERT_WITH_CODE(!ret,
			"[GetMclks]: fail to get max PPCLK_UCLK\n",
			return ret);
	}

	return (mem_clk * 100);
}

static int vega20_get_gpu_power(struct pp_hwmgr *hwmgr,
		uint32_t *query)
{
	int ret = 0;
	SmuMetrics_t metrics_table;

	ret = smum_smc_table_manager(hwmgr, (uint8_t *)&metrics_table, TABLE_SMU_METRICS, true);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to export SMU METRICS table!",
			return ret);

	*query = metrics_table.CurrSocketPower << 8;

	return ret;
}

static int vega20_get_current_clk_freq(struct pp_hwmgr *hwmgr,
		PPCLK_e clk_id, uint32_t *clk_freq)
{
	int ret = 0;

	*clk_freq = 0;

	PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetDpmClockFreq, (clk_id << 16))) == 0,
			"[GetCurrentClkFreq] Attempt to get Current Frequency Failed!",
			return ret);
	*clk_freq = smum_get_argument(hwmgr);

	*clk_freq = *clk_freq * 100;

	return 0;
}

static int vega20_get_current_activity_percent(struct pp_hwmgr *hwmgr,
		uint32_t *activity_percent)
{
	int ret = 0;
	SmuMetrics_t metrics_table;

	ret = smum_smc_table_manager(hwmgr, (uint8_t *)&metrics_table, TABLE_SMU_METRICS, true);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to export SMU METRICS table!",
			return ret);

	*activity_percent = metrics_table.AverageGfxActivity;

	return ret;
}

static int vega20_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			      void *value, int *size)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t val_vid;
	int ret = 0;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = vega20_get_current_clk_freq(hwmgr,
				PPCLK_GFXCLK,
				(uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = vega20_get_current_clk_freq(hwmgr,
				PPCLK_UCLK,
				(uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = vega20_get_current_activity_percent(hwmgr, (uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = vega20_thermal_get_temperature(hwmgr);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_UVD_POWER:
		*((uint32_t *)value) = data->uvd_power_gated ? 0 : 1;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VCE_POWER:
		*((uint32_t *)value) = data->vce_power_gated ? 0 : 1;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		*size = 16;
		ret = vega20_get_gpu_power(hwmgr, (uint32_t *)value);
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		val_vid = (RREG32_SOC15(SMUIO, 0, mmSMUSVI0_TEL_PLANE0) &
			SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR_MASK) >>
			SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR__SHIFT;
		*((uint32_t *)value) =
			(uint32_t)convert_to_vddc((uint8_t)val_vid);
		break;
	case AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK:
		ret = vega20_get_enabled_smc_features(hwmgr, (uint64_t *)value);
		if (!ret)
			*size = 8;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

int vega20_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
		struct pp_display_clock_request *clock_req)
{
	int result = 0;
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 1000;
	PPCLK_e clk_select = 0;
	uint32_t clk_request = 0;

	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		switch (clk_type) {
		case amd_pp_dcef_clock:
			clk_select = PPCLK_DCEFCLK;
			break;
		case amd_pp_disp_clock:
			clk_select = PPCLK_DISPCLK;
			break;
		case amd_pp_pixel_clock:
			clk_select = PPCLK_PIXCLK;
			break;
		case amd_pp_phy_clock:
			clk_select = PPCLK_PHYCLK;
			break;
		default:
			pr_info("[DisplayClockVoltageRequest]Invalid Clock Type!");
			result = -EINVAL;
			break;
		}

		if (!result) {
			clk_request = (clk_select << 16) | clk_freq;
			result = smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetHardMinByFreq,
					clk_request);
		}
	}

	return result;
}

static int vega20_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level)
{
	return 0;
}

static int vega20_notify_smc_display_config_after_ps_adjustment(
		struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table =
			&data->dpm_table.mem_table;
	struct PP_Clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;
	int ret = 0;

	min_clocks.dcefClock = hwmgr->display_config->min_dcef_set_clk;
	min_clocks.dcefClockInSR = hwmgr->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memoryClock = hwmgr->display_config->min_mem_set_clock;

	if (data->smu_features[GNLD_DPM_DCEFCLK].supported) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcefClock * 10;
		if (!vega20_display_clock_voltage_request(hwmgr, &clock_req)) {
			if (data->smu_features[GNLD_DS_DCEFCLK].supported)
				PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetMinDeepSleepDcefclk,
					min_clocks.dcefClockInSR / 100)) == 0,
					"Attempt to set divider for DCEFCLK Failed!",
					return ret);
		} else {
			pr_info("Attempt to set Hard Min for DCEFCLK Failed!");
		}
	}

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		dpm_table->dpm_state.hard_min_level = min_clocks.memoryClock / 100;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetHardMinByFreq,
				(PPCLK_UCLK << 16 ) | dpm_table->dpm_state.hard_min_level)),
				"[SetHardMinFreq] Set hard min uclk failed!",
				return ret);
	}

	return 0;
}

static int vega20_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t soft_level;
	int ret = 0;

	soft_level = vega20_find_highest_dpm_level(&(data->dpm_table.gfx_table));

	data->dpm_table.gfx_table.dpm_state.soft_min_level =
		data->dpm_table.gfx_table.dpm_state.soft_max_level =
		data->dpm_table.gfx_table.dpm_levels[soft_level].value;

	soft_level = vega20_find_highest_dpm_level(&(data->dpm_table.mem_table));

	data->dpm_table.mem_table.dpm_state.soft_min_level =
		data->dpm_table.mem_table.dpm_state.soft_max_level =
		data->dpm_table.mem_table.dpm_levels[soft_level].value;

	ret = vega20_upload_dpm_min_level(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to highest!",
			return ret);

	ret = vega20_upload_dpm_max_level(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload dpm max level to highest!",
			return ret);

	return 0;
}

static int vega20_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t soft_level;
	int ret = 0;

	soft_level = vega20_find_lowest_dpm_level(&(data->dpm_table.gfx_table));

	data->dpm_table.gfx_table.dpm_state.soft_min_level =
		data->dpm_table.gfx_table.dpm_state.soft_max_level =
		data->dpm_table.gfx_table.dpm_levels[soft_level].value;

	soft_level = vega20_find_lowest_dpm_level(&(data->dpm_table.mem_table));

	data->dpm_table.mem_table.dpm_state.soft_min_level =
		data->dpm_table.mem_table.dpm_state.soft_max_level =
		data->dpm_table.mem_table.dpm_levels[soft_level].value;

	ret = vega20_upload_dpm_min_level(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to highest!",
			return ret);

	ret = vega20_upload_dpm_max_level(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload dpm max level to highest!",
			return ret);

	return 0;

}

static int vega20_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	ret = vega20_upload_dpm_min_level(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload DPM Bootup Levels!",
			return ret);

	ret = vega20_upload_dpm_max_level(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload DPM Max Levels!",
			return ret);

	return 0;
}

static int vega20_get_profiling_clk_mask(struct pp_hwmgr *hwmgr, enum amd_dpm_forced_level level,
				uint32_t *sclk_mask, uint32_t *mclk_mask, uint32_t *soc_mask)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *gfx_dpm_table = &(data->dpm_table.gfx_table);
	struct vega20_single_dpm_table *mem_dpm_table = &(data->dpm_table.mem_table);
	struct vega20_single_dpm_table *soc_dpm_table = &(data->dpm_table.soc_table);

	*sclk_mask = 0;
	*mclk_mask = 0;
	*soc_mask  = 0;

	if (gfx_dpm_table->count > VEGA20_UMD_PSTATE_GFXCLK_LEVEL &&
	    mem_dpm_table->count > VEGA20_UMD_PSTATE_MCLK_LEVEL &&
	    soc_dpm_table->count > VEGA20_UMD_PSTATE_SOCCLK_LEVEL) {
		*sclk_mask = VEGA20_UMD_PSTATE_GFXCLK_LEVEL;
		*mclk_mask = VEGA20_UMD_PSTATE_MCLK_LEVEL;
		*soc_mask  = VEGA20_UMD_PSTATE_SOCCLK_LEVEL;
	}

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
		*sclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		*mclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		*sclk_mask = gfx_dpm_table->count - 1;
		*mclk_mask = mem_dpm_table->count - 1;
		*soc_mask  = soc_dpm_table->count - 1;
	}

	return 0;
}

static int vega20_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	uint32_t soft_min_level, soft_max_level;
	int ret = 0;

	switch (type) {
	case PP_SCLK:
		soft_min_level = mask ? (ffs(mask) - 1) : 0;
		soft_max_level = mask ? (fls(mask) - 1) : 0;

		data->dpm_table.gfx_table.dpm_state.soft_min_level =
			data->dpm_table.gfx_table.dpm_levels[soft_min_level].value;
		data->dpm_table.gfx_table.dpm_state.soft_max_level =
			data->dpm_table.gfx_table.dpm_levels[soft_max_level].value;

		ret = vega20_upload_dpm_min_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to lowest!",
			return ret);

		ret = vega20_upload_dpm_max_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload dpm max level to highest!",
			return ret);
		break;

	case PP_MCLK:
		soft_min_level = mask ? (ffs(mask) - 1) : 0;
		soft_max_level = mask ? (fls(mask) - 1) : 0;

		data->dpm_table.mem_table.dpm_state.soft_min_level =
			data->dpm_table.mem_table.dpm_levels[soft_min_level].value;
		data->dpm_table.mem_table.dpm_state.soft_max_level =
			data->dpm_table.mem_table.dpm_levels[soft_max_level].value;

		ret = vega20_upload_dpm_min_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to lowest!",
			return ret);

		ret = vega20_upload_dpm_max_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload dpm max level to highest!",
			return ret);

		break;

	case PP_PCIE:
		break;

	default:
		break;
	}

	return 0;
}

static int vega20_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;
	uint32_t sclk_mask, mclk_mask, soc_mask;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = vega20_force_dpm_highest(hwmgr);
		break;

	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = vega20_force_dpm_lowest(hwmgr);
		break;

	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = vega20_unforce_dpm_levels(hwmgr);
		break;

	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = vega20_get_profiling_clk_mask(hwmgr, level, &sclk_mask, &mclk_mask, &soc_mask);
		if (ret)
			return ret;
		vega20_force_clock_level(hwmgr, PP_SCLK, 1 << sclk_mask);
		vega20_force_clock_level(hwmgr, PP_MCLK, 1 << mclk_mask);
		break;

	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}

	return ret;
}

static uint32_t vega20_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_FAN_CONTROL].enabled == false)
		return AMD_FAN_CTRL_MANUAL;
	else
		return AMD_FAN_CTRL_AUTO;
}

static void vega20_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	switch (mode) {
	case AMD_FAN_CTRL_NONE:
		vega20_fan_ctrl_set_fan_speed_percent(hwmgr, 100);
		break;
	case AMD_FAN_CTRL_MANUAL:
		if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
			vega20_fan_ctrl_stop_smc_fan_control(hwmgr);
		break;
	case AMD_FAN_CTRL_AUTO:
		if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
			vega20_fan_ctrl_start_smc_fan_control(hwmgr);
		break;
	default:
		break;
	}
}

static int vega20_get_dal_power_level(struct pp_hwmgr *hwmgr,
		struct amd_pp_simple_clock_info *info)
{
#if 0
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_clock_and_voltage_limits *max_limits =
			&table_info->max_clock_voltage_on_ac;

	info->engine_max_clock = max_limits->sclk;
	info->memory_max_clock = max_limits->mclk;
#endif
	return 0;
}


static int vega20_get_sclks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table = &(data->dpm_table.gfx_table);
	int i, count;

	PP_ASSERT_WITH_CODE(data->smu_features[GNLD_DPM_GFXCLK].enabled,
		"[GetSclks]: gfxclk dpm not enabled!\n",
		return -EPERM);

	count = (dpm_table->count > MAX_NUM_CLOCKS) ? MAX_NUM_CLOCKS : dpm_table->count;
	clocks->num_levels = count;

	for (i = 0; i < count; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;
		clocks->data[i].latency_in_us = 0;
	}

	return 0;
}

static uint32_t vega20_get_mem_latency(struct pp_hwmgr *hwmgr,
		uint32_t clock)
{
	return 25;
}

static int vega20_get_memclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table = &(data->dpm_table.mem_table);
	int i, count;

	PP_ASSERT_WITH_CODE(data->smu_features[GNLD_DPM_UCLK].enabled,
		"[GetMclks]: uclk dpm not enabled!\n",
		return -EPERM);

	count = (dpm_table->count > MAX_NUM_CLOCKS) ? MAX_NUM_CLOCKS : dpm_table->count;
	clocks->num_levels = data->mclk_latency_table.count = count;

	for (i = 0; i < count; i++) {
		clocks->data[i].clocks_in_khz =
			data->mclk_latency_table.entries[i].frequency =
			dpm_table->dpm_levels[i].value * 1000;
		clocks->data[i].latency_in_us =
			data->mclk_latency_table.entries[i].latency =
			vega20_get_mem_latency(hwmgr, dpm_table->dpm_levels[i].value);
	}

	return 0;
}

static int vega20_get_dcefclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table = &(data->dpm_table.dcef_table);
	int i, count;

	PP_ASSERT_WITH_CODE(data->smu_features[GNLD_DPM_DCEFCLK].enabled,
		"[GetDcfclocks]: dcefclk dpm not enabled!\n",
		return -EPERM);

	count = (dpm_table->count > MAX_NUM_CLOCKS) ? MAX_NUM_CLOCKS : dpm_table->count;
	clocks->num_levels = count;

	for (i = 0; i < count; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;
		clocks->data[i].latency_in_us = 0;
	}

	return 0;
}

static int vega20_get_socclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table = &(data->dpm_table.soc_table);
	int i, count;

	PP_ASSERT_WITH_CODE(data->smu_features[GNLD_DPM_SOCCLK].enabled,
		"[GetSocclks]: socclk dpm not enabled!\n",
		return -EPERM);

	count = (dpm_table->count > MAX_NUM_CLOCKS) ? MAX_NUM_CLOCKS : dpm_table->count;
	clocks->num_levels = count;

	for (i = 0; i < count; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;
		clocks->data[i].latency_in_us = 0;
	}

	return 0;

}

static int vega20_get_clock_by_type_with_latency(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks)
{
	int ret;

	switch (type) {
	case amd_pp_sys_clock:
		ret = vega20_get_sclks(hwmgr, clocks);
		break;
	case amd_pp_mem_clock:
		ret = vega20_get_memclocks(hwmgr, clocks);
		break;
	case amd_pp_dcef_clock:
		ret = vega20_get_dcefclocks(hwmgr, clocks);
		break;
	case amd_pp_soc_clock:
		ret = vega20_get_socclocks(hwmgr, clocks);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int vega20_get_clock_by_type_with_voltage(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks)
{
	clocks->num_levels = 0;

	return 0;
}

static int vega20_set_watermarks_for_clocks_ranges(struct pp_hwmgr *hwmgr,
						   void *clock_ranges)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	Watermarks_t *table = &(data->smc_state_table.water_marks_table);
	struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges = clock_ranges;

	if (!data->registry_data.disable_water_mark &&
	    data->smu_features[GNLD_DPM_DCEFCLK].supported &&
	    data->smu_features[GNLD_DPM_SOCCLK].supported) {
		smu_set_watermarks_for_clocks_ranges(table, wm_with_clock_ranges);
		data->water_marks_bitmap |= WaterMarksExist;
		data->water_marks_bitmap &= ~WaterMarksLoaded;
	}

	return 0;
}

static int vega20_odn_edit_dpm_table(struct pp_hwmgr *hwmgr,
					enum PP_OD_DPM_TABLE_COMMAND type,
					long *input, uint32_t size)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_od8_single_setting *od8_settings =
			data->od8_settings.od8_settings_array;
	OverDriveTable_t *od_table =
			&(data->smc_state_table.overdrive_table);
	struct pp_clock_levels_with_latency clocks;
	int32_t input_index, input_clk, input_vol, i;
	int od8_id;
	int ret;

	PP_ASSERT_WITH_CODE(input, "NULL user input for clock and voltage",
				return -EINVAL);

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (!(od8_settings[OD8_SETTING_GFXCLK_FMIN].feature_id &&
		      od8_settings[OD8_SETTING_GFXCLK_FMAX].feature_id)) {
			pr_info("Sclk min/max frequency overdrive not supported\n");
			return -EOPNOTSUPP;
		}

		for (i = 0; i < size; i += 2) {
			if (i + 2 > size) {
				pr_info("invalid number of input parameters %d\n",
					size);
				return -EINVAL;
			}

			input_index = input[i];
			input_clk = input[i + 1];

			if (input_index != 0 && input_index != 1) {
				pr_info("Invalid index %d\n", input_index);
				pr_info("Support min/max sclk frequency setting only which index by 0/1\n");
				return -EINVAL;
			}

			if (input_clk < od8_settings[OD8_SETTING_GFXCLK_FMIN].min_value ||
			    input_clk > od8_settings[OD8_SETTING_GFXCLK_FMAX].max_value) {
				pr_info("clock freq %d is not within allowed range [%d - %d]\n",
					input_clk,
					od8_settings[OD8_SETTING_GFXCLK_FMIN].min_value,
					od8_settings[OD8_SETTING_GFXCLK_FMAX].max_value);
				return -EINVAL;
			}

			if ((input_index == 0 && od_table->GfxclkFmin != input_clk) ||
			    (input_index == 1 && od_table->GfxclkFmax != input_clk))
				data->gfxclk_overdrive = true;

			if (input_index == 0)
				od_table->GfxclkFmin = input_clk;
			else
				od_table->GfxclkFmax = input_clk;
		}

		break;

	case PP_OD_EDIT_MCLK_VDDC_TABLE:
		if (!od8_settings[OD8_SETTING_UCLK_FMAX].feature_id) {
			pr_info("Mclk max frequency overdrive not supported\n");
			return -EOPNOTSUPP;
		}

		ret = vega20_get_memclocks(hwmgr, &clocks);
		PP_ASSERT_WITH_CODE(!ret,
				"Attempt to get memory clk levels failed!",
				return ret);

		for (i = 0; i < size; i += 2) {
			if (i + 2 > size) {
				pr_info("invalid number of input parameters %d\n",
					size);
				return -EINVAL;
			}

			input_index = input[i];
			input_clk = input[i + 1];

			if (input_index != 1) {
				pr_info("Invalid index %d\n", input_index);
				pr_info("Support max Mclk frequency setting only which index by 1\n");
				return -EINVAL;
			}

			if (input_clk < clocks.data[0].clocks_in_khz / 1000 ||
			    input_clk > od8_settings[OD8_SETTING_UCLK_FMAX].max_value) {
				pr_info("clock freq %d is not within allowed range [%d - %d]\n",
					input_clk,
					clocks.data[0].clocks_in_khz / 1000,
					od8_settings[OD8_SETTING_UCLK_FMAX].max_value);
				return -EINVAL;
			}

			if (input_index == 1 && od_table->UclkFmax != input_clk)
				data->memclk_overdrive = true;

			od_table->UclkFmax = input_clk;
		}

		break;

	case PP_OD_EDIT_VDDC_CURVE:
		if (!(od8_settings[OD8_SETTING_GFXCLK_FREQ1].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FREQ2].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FREQ3].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id)) {
			pr_info("Voltage curve calibrate not supported\n");
			return -EOPNOTSUPP;
		}

		for (i = 0; i < size; i += 3) {
			if (i + 3 > size) {
				pr_info("invalid number of input parameters %d\n",
					size);
				return -EINVAL;
			}

			input_index = input[i];
			input_clk = input[i + 1];
			input_vol = input[i + 2];

			if (input_index > 2) {
				pr_info("Setting for point %d is not supported\n",
						input_index + 1);
				pr_info("Three supported points index by 0, 1, 2\n");
				return -EINVAL;
			}

			od8_id = OD8_SETTING_GFXCLK_FREQ1 + 2 * input_index;
			if (input_clk < od8_settings[od8_id].min_value ||
			    input_clk > od8_settings[od8_id].max_value) {
				pr_info("clock freq %d is not within allowed range [%d - %d]\n",
					input_clk,
					od8_settings[od8_id].min_value,
					od8_settings[od8_id].max_value);
				return -EINVAL;
			}

			od8_id = OD8_SETTING_GFXCLK_VOLTAGE1 + 2 * input_index;
			if (input_vol < od8_settings[od8_id].min_value ||
			    input_vol > od8_settings[od8_id].max_value) {
				pr_info("clock voltage %d is not within allowed range [%d - %d]\n",
					input_vol,
					od8_settings[od8_id].min_value,
					od8_settings[od8_id].max_value);
				return -EINVAL;
			}

			switch (input_index) {
			case 0:
				od_table->GfxclkFreq1 = input_clk;
				od_table->GfxclkVolt1 = input_vol * VOLTAGE_SCALE;
				break;
			case 1:
				od_table->GfxclkFreq2 = input_clk;
				od_table->GfxclkVolt2 = input_vol * VOLTAGE_SCALE;
				break;
			case 2:
				od_table->GfxclkFreq3 = input_clk;
				od_table->GfxclkVolt3 = input_vol * VOLTAGE_SCALE;
				break;
			}
		}
		break;

	case PP_OD_RESTORE_DEFAULT_TABLE:
		data->gfxclk_overdrive = false;
		data->memclk_overdrive = false;

		ret = smum_smc_table_manager(hwmgr,
					     (uint8_t *)od_table,
					     TABLE_OVERDRIVE, true);
		PP_ASSERT_WITH_CODE(!ret,
				"Failed to export overdrive table!",
				return ret);
		break;

	case PP_OD_COMMIT_DPM_TABLE:
		ret = smum_smc_table_manager(hwmgr,
					     (uint8_t *)od_table,
					     TABLE_OVERDRIVE, false);
		PP_ASSERT_WITH_CODE(!ret,
				"Failed to import overdrive table!",
				return ret);

		/* retrieve updated gfxclk table */
		if (data->gfxclk_overdrive) {
			data->gfxclk_overdrive = false;

			ret = vega20_setup_gfxclk_dpm_table(hwmgr);
			if (ret)
				return ret;
		}

		/* retrieve updated memclk table */
		if (data->memclk_overdrive) {
			data->memclk_overdrive = false;

			ret = vega20_setup_memclk_dpm_table(hwmgr);
			if (ret)
				return ret;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int vega20_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_od8_single_setting *od8_settings =
			data->od8_settings.od8_settings_array;
	OverDriveTable_t *od_table =
			&(data->smc_state_table.overdrive_table);
	struct pp_clock_levels_with_latency clocks;
	int i, now, size = 0;
	int ret = 0;

	switch (type) {
	case PP_SCLK:
		ret = vega20_get_current_clk_freq(hwmgr, PPCLK_GFXCLK, &now);
		PP_ASSERT_WITH_CODE(!ret,
				"Attempt to get current gfx clk Failed!",
				return ret);

		ret = vega20_get_sclks(hwmgr, &clocks);
		PP_ASSERT_WITH_CODE(!ret,
				"Attempt to get gfx clk levels Failed!",
				return ret);

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz == now) ? "*" : "");
		break;

	case PP_MCLK:
		ret = vega20_get_current_clk_freq(hwmgr, PPCLK_UCLK, &now);
		PP_ASSERT_WITH_CODE(!ret,
				"Attempt to get current mclk freq Failed!",
				return ret);

		ret = vega20_get_memclocks(hwmgr, &clocks);
		PP_ASSERT_WITH_CODE(!ret,
				"Attempt to get memory clk levels Failed!",
				return ret);

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz == now) ? "*" : "");
		break;

	case PP_PCIE:
		break;

	case OD_SCLK:
		if (od8_settings[OD8_SETTING_GFXCLK_FMIN].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FMAX].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_SCLK");
			size += sprintf(buf + size, "0: %10uMhz\n",
				od_table->GfxclkFmin);
			size += sprintf(buf + size, "1: %10uMhz\n",
				od_table->GfxclkFmax);
		}
		break;

	case OD_MCLK:
		if (od8_settings[OD8_SETTING_UCLK_FMAX].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_MCLK");
			size += sprintf(buf + size, "1: %10uMhz\n",
				od_table->UclkFmax);
		}

		break;

	case OD_VDDC_CURVE:
		if (od8_settings[OD8_SETTING_GFXCLK_FREQ1].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FREQ2].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FREQ3].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_VDDC_CURVE");
			size += sprintf(buf + size, "0: %10uMhz %10dmV\n",
				od_table->GfxclkFreq1,
				od_table->GfxclkVolt1 / VOLTAGE_SCALE);
			size += sprintf(buf + size, "1: %10uMhz %10dmV\n",
				od_table->GfxclkFreq2,
				od_table->GfxclkVolt2 / VOLTAGE_SCALE);
			size += sprintf(buf + size, "2: %10uMhz %10dmV\n",
				od_table->GfxclkFreq3,
				od_table->GfxclkVolt3 / VOLTAGE_SCALE);
		}

		break;

	case OD_RANGE:
		size = sprintf(buf, "%s:\n", "OD_RANGE");

		if (od8_settings[OD8_SETTING_GFXCLK_FMIN].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FMAX].feature_id) {
			size += sprintf(buf + size, "SCLK: %7uMhz %10uMhz\n",
				od8_settings[OD8_SETTING_GFXCLK_FMIN].min_value,
				od8_settings[OD8_SETTING_GFXCLK_FMAX].max_value);
		}

		if (od8_settings[OD8_SETTING_UCLK_FMAX].feature_id) {
			ret = vega20_get_memclocks(hwmgr, &clocks);
			PP_ASSERT_WITH_CODE(!ret,
					"Fail to get memory clk levels!",
					return ret);

			size += sprintf(buf + size, "MCLK: %7uMhz %10uMhz\n",
				clocks.data[0].clocks_in_khz / 1000,
				od8_settings[OD8_SETTING_UCLK_FMAX].max_value);
		}

		if (od8_settings[OD8_SETTING_GFXCLK_FREQ1].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FREQ2].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_FREQ3].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id &&
		    od8_settings[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id) {
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[0]: %7uMhz %10uMhz\n",
				od8_settings[OD8_SETTING_GFXCLK_FREQ1].min_value,
				od8_settings[OD8_SETTING_GFXCLK_FREQ1].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[0]: %7dmV %11dmV\n",
				od8_settings[OD8_SETTING_GFXCLK_VOLTAGE1].min_value,
				od8_settings[OD8_SETTING_GFXCLK_VOLTAGE1].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[1]: %7uMhz %10uMhz\n",
				od8_settings[OD8_SETTING_GFXCLK_FREQ2].min_value,
				od8_settings[OD8_SETTING_GFXCLK_FREQ2].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[1]: %7dmV %11dmV\n",
				od8_settings[OD8_SETTING_GFXCLK_VOLTAGE2].min_value,
				od8_settings[OD8_SETTING_GFXCLK_VOLTAGE2].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[2]: %7uMhz %10uMhz\n",
				od8_settings[OD8_SETTING_GFXCLK_FREQ3].min_value,
				od8_settings[OD8_SETTING_GFXCLK_FREQ3].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[2]: %7dmV %11dmV\n",
				od8_settings[OD8_SETTING_GFXCLK_VOLTAGE3].min_value,
				od8_settings[OD8_SETTING_GFXCLK_VOLTAGE3].max_value);
		}

		break;
	default:
		break;
	}
	return size;
}

static int vega20_set_uclk_to_highest_dpm_level(struct pp_hwmgr *hwmgr,
		struct vega20_single_dpm_table *dpm_table)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	int ret = 0;

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		PP_ASSERT_WITH_CODE(dpm_table->count > 0,
				"[SetUclkToHightestDpmLevel] Dpm table has no entry!",
				return -EINVAL);
		PP_ASSERT_WITH_CODE(dpm_table->count <= NUM_UCLK_DPM_LEVELS,
				"[SetUclkToHightestDpmLevel] Dpm table has too many entries!",
				return -EINVAL);

		dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetHardMinByFreq,
				(PPCLK_UCLK << 16 ) | dpm_table->dpm_state.hard_min_level)),
				"[SetUclkToHightestDpmLevel] Set hard min uclk failed!",
				return ret);
	}

	return ret;
}

static int vega20_pre_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	int ret = 0;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, 0);

	ret = vega20_set_uclk_to_highest_dpm_level(hwmgr,
			&data->dpm_table.mem_table);

	return ret;
}

static int vega20_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	int result = 0;
	Watermarks_t *wm_table = &(data->smc_state_table.water_marks_table);

	if ((data->water_marks_bitmap & WaterMarksExist) &&
	    !(data->water_marks_bitmap & WaterMarksLoaded)) {
		result = smum_smc_table_manager(hwmgr,
						(uint8_t *)wm_table, TABLE_WATERMARKS, false);
		PP_ASSERT_WITH_CODE(!result,
				"Failed to update WMTABLE!",
				return result);
		data->water_marks_bitmap |= WaterMarksLoaded;
	}

	if ((data->water_marks_bitmap & WaterMarksExist) &&
	    data->smu_features[GNLD_DPM_DCEFCLK].supported &&
	    data->smu_features[GNLD_DPM_SOCCLK].supported) {
		result = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays,
			hwmgr->display_config->num_display);
	}

	return result;
}

int vega20_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega20_hwmgr *data =
			(struct vega20_hwmgr *)(hwmgr->backend);
	int ret = 0;

	if (data->smu_features[GNLD_DPM_UVD].supported) {
		if (data->smu_features[GNLD_DPM_UVD].enabled == enable) {
			if (enable)
				PP_DBG_LOG("[EnableDisableUVDDPM] feature DPM UVD already enabled!\n");
			else
				PP_DBG_LOG("[EnableDisableUVDDPM] feature DPM UVD already disabled!\n");
		}

		ret = vega20_enable_smc_features(hwmgr,
				enable,
				data->smu_features[GNLD_DPM_UVD].smu_feature_bitmap);
		PP_ASSERT_WITH_CODE(!ret,
				"[EnableDisableUVDDPM] Attempt to Enable/Disable DPM UVD Failed!",
				return ret);
		data->smu_features[GNLD_DPM_UVD].enabled = enable;
	}

	return 0;
}

static void vega20_power_gate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);

	if (data->vce_power_gated == bgate)
		return ;

	data->vce_power_gated = bgate;
	vega20_enable_disable_vce_dpm(hwmgr, !bgate);
}

static void vega20_power_gate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);

	if (data->uvd_power_gated == bgate)
		return ;

	data->uvd_power_gated = bgate;
	vega20_enable_disable_uvd_dpm(hwmgr, !bgate);
}

static int vega20_apply_clocks_adjust_rules(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	struct vega20_single_dpm_table *dpm_table;
	bool vblank_too_short = false;
	bool disable_mclk_switching;
	uint32_t i, latency;

	disable_mclk_switching = ((1 < hwmgr->display_config->num_display) &&
                           !hwmgr->display_config->multi_monitor_in_sync) ||
                            vblank_too_short;
    latency = hwmgr->display_config->dce_tolerable_mclk_in_active_latency;

	/* gfxclk */
	dpm_table = &(data->dpm_table.gfx_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	if (PP_CAP(PHM_PlatformCaps_UMDPState)) {
		if (VEGA20_UMD_PSTATE_GFXCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_GFXCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_GFXCLK_LEVEL].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[0].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	}

	/* memclk */
	dpm_table = &(data->dpm_table.mem_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	if (PP_CAP(PHM_PlatformCaps_UMDPState)) {
		if (VEGA20_UMD_PSTATE_MCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_MCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_MCLK_LEVEL].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[0].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	}

	/* honour DAL's UCLK Hardmin */
	if (dpm_table->dpm_state.hard_min_level < (hwmgr->display_config->min_mem_set_clock / 100))
		dpm_table->dpm_state.hard_min_level = hwmgr->display_config->min_mem_set_clock / 100;

	/* Hardmin is dependent on displayconfig */
	if (disable_mclk_switching) {
		dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		for (i = 0; i < data->mclk_latency_table.count - 1; i++) {
			if (data->mclk_latency_table.entries[i].latency <= latency) {
				if (dpm_table->dpm_levels[i].value >= (hwmgr->display_config->min_mem_set_clock / 100)) {
					dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[i].value;
					break;
				}
			}
		}
	}

	if (hwmgr->display_config->nb_pstate_switch_disable)
		dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	/* vclk */
	dpm_table = &(data->dpm_table.vclk_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	if (PP_CAP(PHM_PlatformCaps_UMDPState)) {
		if (VEGA20_UMD_PSTATE_UVDCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	}

	/* dclk */
	dpm_table = &(data->dpm_table.dclk_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	if (PP_CAP(PHM_PlatformCaps_UMDPState)) {
		if (VEGA20_UMD_PSTATE_UVDCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	}

	/* socclk */
	dpm_table = &(data->dpm_table.soc_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	if (PP_CAP(PHM_PlatformCaps_UMDPState)) {
		if (VEGA20_UMD_PSTATE_SOCCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_SOCCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_SOCCLK_LEVEL].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	}

	/* eclk */
	dpm_table = &(data->dpm_table.eclk_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	if (PP_CAP(PHM_PlatformCaps_UMDPState)) {
		if (VEGA20_UMD_PSTATE_VCEMCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_VCEMCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_VCEMCLK_LEVEL].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	}

	return 0;
}

static bool
vega20_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	bool is_update_required = false;

	if (data->display_timing.num_existing_displays !=
			hwmgr->display_config->num_display)
		is_update_required = true;

	if (data->registry_data.gfx_clk_deep_sleep_support &&
	   (data->display_timing.min_clock_in_sr !=
	    hwmgr->display_config->min_core_set_clock_in_sr))
		is_update_required = true;

	return is_update_required;
}

static int vega20_disable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	ret = vega20_disable_all_smu_features(hwmgr);
	PP_ASSERT_WITH_CODE(!ret,
			"[DisableDpmTasks] Failed to disable all smu features!",
			return ret);

	return 0;
}

static int vega20_power_off_asic(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	int result;

	result = vega20_disable_dpm_tasks(hwmgr);
	PP_ASSERT_WITH_CODE((0 == result),
			"[PowerOffAsic] Failed to disable DPM!",
			);
	data->water_marks_bitmap &= ~(WaterMarksLoaded);

	return result;
}

static int conv_power_profile_to_pplib_workload(int power_profile)
{
	int pplib_workload = 0;

	switch (power_profile) {
	case PP_SMC_POWER_PROFILE_FULLSCREEN3D:
		pplib_workload = WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT;
		break;
	case PP_SMC_POWER_PROFILE_POWERSAVING:
		pplib_workload = WORKLOAD_PPLIB_POWER_SAVING_BIT;
		break;
	case PP_SMC_POWER_PROFILE_VIDEO:
		pplib_workload = WORKLOAD_PPLIB_VIDEO_BIT;
		break;
	case PP_SMC_POWER_PROFILE_VR:
		pplib_workload = WORKLOAD_PPLIB_VR_BIT;
		break;
	case PP_SMC_POWER_PROFILE_COMPUTE:
		pplib_workload = WORKLOAD_PPLIB_COMPUTE_BIT;
		break;
	case PP_SMC_POWER_PROFILE_CUSTOM:
		pplib_workload = WORKLOAD_PPLIB_CUSTOM_BIT;
		break;
	}

	return pplib_workload;
}

static int vega20_get_power_profile_mode(struct pp_hwmgr *hwmgr, char *buf)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	uint32_t i, size = 0;
	uint16_t workload_type = 0;
	static const char *profile_name[] = {
					"3D_FULL_SCREEN",
					"POWER_SAVING",
					"VIDEO",
					"VR",
					"COMPUTE",
					"CUSTOM"};
	static const char *title[] = {
			"PROFILE_INDEX(NAME)",
			"CLOCK_TYPE(NAME)",
			"FPS",
			"UseRlcBusy",
			"MinActiveFreqType",
			"MinActiveFreq",
			"BoosterFreqType",
			"BoosterFreq",
			"PD_Data_limit_c",
			"PD_Data_error_coeff",
			"PD_Data_error_rate_coeff"};
	int result = 0;

	if (!buf)
		return -EINVAL;

	size += sprintf(buf + size, "%16s %s %s %s %s %s %s %s %s %s %s\n",
			title[0], title[1], title[2], title[3], title[4], title[5],
			title[6], title[7], title[8], title[9], title[10]);

	for (i = 0; i <= PP_SMC_POWER_PROFILE_CUSTOM; i++) {
		/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
		workload_type = conv_power_profile_to_pplib_workload(i);
		result = vega20_get_activity_monitor_coeff(hwmgr,
				(uint8_t *)(&activity_monitor), workload_type);
		PP_ASSERT_WITH_CODE(!result,
				"[GetPowerProfile] Failed to get activity monitor!",
				return result);

		size += sprintf(buf + size, "%2d %14s%s:\n",
			i, profile_name[i], (i == hwmgr->power_profile_mode) ? "*" : " ");

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			0,
			"GFXCLK",
			activity_monitor.Gfx_FPS,
			activity_monitor.Gfx_UseRlcBusy,
			activity_monitor.Gfx_MinActiveFreqType,
			activity_monitor.Gfx_MinActiveFreq,
			activity_monitor.Gfx_BoosterFreqType,
			activity_monitor.Gfx_BoosterFreq,
			activity_monitor.Gfx_PD_Data_limit_c,
			activity_monitor.Gfx_PD_Data_error_coeff,
			activity_monitor.Gfx_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			1,
			"SOCCLK",
			activity_monitor.Soc_FPS,
			activity_monitor.Soc_UseRlcBusy,
			activity_monitor.Soc_MinActiveFreqType,
			activity_monitor.Soc_MinActiveFreq,
			activity_monitor.Soc_BoosterFreqType,
			activity_monitor.Soc_BoosterFreq,
			activity_monitor.Soc_PD_Data_limit_c,
			activity_monitor.Soc_PD_Data_error_coeff,
			activity_monitor.Soc_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			2,
			"UCLK",
			activity_monitor.Mem_FPS,
			activity_monitor.Mem_UseRlcBusy,
			activity_monitor.Mem_MinActiveFreqType,
			activity_monitor.Mem_MinActiveFreq,
			activity_monitor.Mem_BoosterFreqType,
			activity_monitor.Mem_BoosterFreq,
			activity_monitor.Mem_PD_Data_limit_c,
			activity_monitor.Mem_PD_Data_error_coeff,
			activity_monitor.Mem_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			3,
			"FCLK",
			activity_monitor.Fclk_FPS,
			activity_monitor.Fclk_UseRlcBusy,
			activity_monitor.Fclk_MinActiveFreqType,
			activity_monitor.Fclk_MinActiveFreq,
			activity_monitor.Fclk_BoosterFreqType,
			activity_monitor.Fclk_BoosterFreq,
			activity_monitor.Fclk_PD_Data_limit_c,
			activity_monitor.Fclk_PD_Data_error_coeff,
			activity_monitor.Fclk_PD_Data_error_rate_coeff);
	}

	return size;
}

static int vega20_set_power_profile_mode(struct pp_hwmgr *hwmgr, long *input, uint32_t size)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	int workload_type, result = 0;

	hwmgr->power_profile_mode = input[size];

	if (hwmgr->power_profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		pr_err("Invalid power profile mode %d\n", hwmgr->power_profile_mode);
		return -EINVAL;
	}

	if (hwmgr->power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {
		if (size < 10)
			return -EINVAL;

		result = vega20_get_activity_monitor_coeff(hwmgr,
				(uint8_t *)(&activity_monitor),
				WORKLOAD_PPLIB_CUSTOM_BIT);
		PP_ASSERT_WITH_CODE(!result,
				"[SetPowerProfile] Failed to get activity monitor!",
				return result);

		switch (input[0]) {
		case 0: /* Gfxclk */
			activity_monitor.Gfx_FPS = input[1];
			activity_monitor.Gfx_UseRlcBusy = input[2];
			activity_monitor.Gfx_MinActiveFreqType = input[3];
			activity_monitor.Gfx_MinActiveFreq = input[4];
			activity_monitor.Gfx_BoosterFreqType = input[5];
			activity_monitor.Gfx_BoosterFreq = input[6];
			activity_monitor.Gfx_PD_Data_limit_c = input[7];
			activity_monitor.Gfx_PD_Data_error_coeff = input[8];
			activity_monitor.Gfx_PD_Data_error_rate_coeff = input[9];
			break;
		case 1: /* Socclk */
			activity_monitor.Soc_FPS = input[1];
			activity_monitor.Soc_UseRlcBusy = input[2];
			activity_monitor.Soc_MinActiveFreqType = input[3];
			activity_monitor.Soc_MinActiveFreq = input[4];
			activity_monitor.Soc_BoosterFreqType = input[5];
			activity_monitor.Soc_BoosterFreq = input[6];
			activity_monitor.Soc_PD_Data_limit_c = input[7];
			activity_monitor.Soc_PD_Data_error_coeff = input[8];
			activity_monitor.Soc_PD_Data_error_rate_coeff = input[9];
			break;
		case 2: /* Uclk */
			activity_monitor.Mem_FPS = input[1];
			activity_monitor.Mem_UseRlcBusy = input[2];
			activity_monitor.Mem_MinActiveFreqType = input[3];
			activity_monitor.Mem_MinActiveFreq = input[4];
			activity_monitor.Mem_BoosterFreqType = input[5];
			activity_monitor.Mem_BoosterFreq = input[6];
			activity_monitor.Mem_PD_Data_limit_c = input[7];
			activity_monitor.Mem_PD_Data_error_coeff = input[8];
			activity_monitor.Mem_PD_Data_error_rate_coeff = input[9];
			break;
		case 3: /* Fclk */
			activity_monitor.Fclk_FPS = input[1];
			activity_monitor.Fclk_UseRlcBusy = input[2];
			activity_monitor.Fclk_MinActiveFreqType = input[3];
			activity_monitor.Fclk_MinActiveFreq = input[4];
			activity_monitor.Fclk_BoosterFreqType = input[5];
			activity_monitor.Fclk_BoosterFreq = input[6];
			activity_monitor.Fclk_PD_Data_limit_c = input[7];
			activity_monitor.Fclk_PD_Data_error_coeff = input[8];
			activity_monitor.Fclk_PD_Data_error_rate_coeff = input[9];
			break;
		}

		result = vega20_set_activity_monitor_coeff(hwmgr,
				(uint8_t *)(&activity_monitor),
				WORKLOAD_PPLIB_CUSTOM_BIT);
		PP_ASSERT_WITH_CODE(!result,
				"[SetPowerProfile] Failed to set activity monitor!",
				return result);
	}

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type =
		conv_power_profile_to_pplib_workload(hwmgr->power_profile_mode);
	smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetWorkloadMask,
						1 << workload_type);

	return 0;
}

static int vega20_notify_cac_buffer_info(struct pp_hwmgr *hwmgr,
					uint32_t virtual_addr_low,
					uint32_t virtual_addr_hi,
					uint32_t mc_addr_low,
					uint32_t mc_addr_hi,
					uint32_t size)
{
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSystemVirtualDramAddrHigh,
					virtual_addr_hi);
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSystemVirtualDramAddrLow,
					virtual_addr_low);
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramLogSetDramAddrHigh,
					mc_addr_hi);

	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramLogSetDramAddrLow,
					mc_addr_low);

	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramLogSetDramSize,
					size);
	return 0;
}

static int vega20_get_thermal_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *thermal_data)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;

	memcpy(thermal_data, &SMU7ThermalWithDelayPolicy[0], sizeof(struct PP_TemperatureRange));

	thermal_data->max = pptable_information->us_software_shutdown_temp *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static const struct pp_hwmgr_func vega20_hwmgr_funcs = {
	/* init/fini related */
	.backend_init =
		vega20_hwmgr_backend_init,
	.backend_fini =
		vega20_hwmgr_backend_fini,
	.asic_setup =
		vega20_setup_asic_task,
	.power_off_asic =
		vega20_power_off_asic,
	.dynamic_state_management_enable =
		vega20_enable_dpm_tasks,
	.dynamic_state_management_disable =
		vega20_disable_dpm_tasks,
	/* power state related */
	.apply_clocks_adjust_rules =
		vega20_apply_clocks_adjust_rules,
	.pre_display_config_changed =
		vega20_pre_display_configuration_changed_task,
	.display_config_changed =
		vega20_display_configuration_changed_task,
	.check_smc_update_required_for_display_configuration =
		vega20_check_smc_update_required_for_display_configuration,
	.notify_smc_display_config_after_ps_adjustment =
		vega20_notify_smc_display_config_after_ps_adjustment,
	/* export to DAL */
	.get_sclk =
		vega20_dpm_get_sclk,
	.get_mclk =
		vega20_dpm_get_mclk,
	.get_dal_power_level =
		vega20_get_dal_power_level,
	.get_clock_by_type_with_latency =
		vega20_get_clock_by_type_with_latency,
	.get_clock_by_type_with_voltage =
		vega20_get_clock_by_type_with_voltage,
	.set_watermarks_for_clocks_ranges =
		vega20_set_watermarks_for_clocks_ranges,
	.display_clock_voltage_request =
		vega20_display_clock_voltage_request,
	.get_performance_level =
		vega20_get_performance_level,
	/* UMD pstate, profile related */
	.force_dpm_level =
		vega20_dpm_force_dpm_level,
	.get_power_profile_mode =
		vega20_get_power_profile_mode,
	.set_power_profile_mode =
		vega20_set_power_profile_mode,
	/* od related */
	.set_power_limit =
		vega20_set_power_limit,
	.get_sclk_od =
		vega20_get_sclk_od,
	.set_sclk_od =
		vega20_set_sclk_od,
	.get_mclk_od =
		vega20_get_mclk_od,
	.set_mclk_od =
		vega20_set_mclk_od,
	.odn_edit_dpm_table =
		vega20_odn_edit_dpm_table,
	/* for sysfs to retrive/set gfxclk/memclk */
	.force_clock_level =
		vega20_force_clock_level,
	.print_clock_levels =
		vega20_print_clock_levels,
	.read_sensor =
		vega20_read_sensor,
	/* powergate related */
	.powergate_uvd =
		vega20_power_gate_uvd,
	.powergate_vce =
		vega20_power_gate_vce,
	/* thermal related */
	.start_thermal_controller =
		vega20_start_thermal_controller,
	.stop_thermal_controller =
		vega20_thermal_stop_thermal_controller,
	.get_thermal_temperature_range =
		vega20_get_thermal_temperature_range,
	.register_irq_handlers =
		smu9_register_irq_handlers,
	.disable_smc_firmware_ctf =
		vega20_thermal_disable_alert,
	/* fan control related */
	.get_fan_speed_percent =
		vega20_fan_ctrl_get_fan_speed_percent,
	.set_fan_speed_percent =
		vega20_fan_ctrl_set_fan_speed_percent,
	.get_fan_speed_info =
		vega20_fan_ctrl_get_fan_speed_info,
	.get_fan_speed_rpm =
		vega20_fan_ctrl_get_fan_speed_rpm,
	.set_fan_speed_rpm =
		vega20_fan_ctrl_set_fan_speed_rpm,
	.get_fan_control_mode =
		vega20_get_fan_control_mode,
	.set_fan_control_mode =
		vega20_set_fan_control_mode,
	/* smu memory related */
	.notify_cac_buffer_info =
		vega20_notify_cac_buffer_info,
	.enable_mgpu_fan_boost =
		vega20_enable_mgpu_fan_boost,
};

int vega20_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &vega20_hwmgr_funcs;
	hwmgr->pptable_func = &vega20_pptable_funcs;

	return 0;
}
