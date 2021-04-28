/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include "vega12_smumgr.h"
#include "hardwaremanager.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "cgs_common.h"
#include "vega12_inc.h"
#include "pppcielanes.h"
#include "vega12_hwmgr.h"
#include "vega12_processpptables.h"
#include "vega12_pptable.h"
#include "vega12_thermal.h"
#include "vega12_ppsmc.h"
#include "pp_debug.h"
#include "amd_pcie_helpers.h"
#include "ppinterrupt.h"
#include "pp_overdriver.h"
#include "pp_thermal.h"
#include "vega12_baco.h"

#define smnPCIE_LC_SPEED_CNTL			0x11140290
#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288

#define LINK_WIDTH_MAX				6
#define LINK_SPEED_MAX				3
static const int link_width[] = {0, 1, 2, 4, 8, 12, 16};
static const int link_speed[] = {25, 50, 80, 160};

static int vega12_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask);
static int vega12_get_clock_ranges(struct pp_hwmgr *hwmgr,
		uint32_t *clock,
		PPCLK_e clock_select,
		bool max);

static void vega12_set_default_registry_data(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);

	data->gfxclk_average_alpha = PPVEGA12_VEGA12GFXCLKAVERAGEALPHA_DFLT;
	data->socclk_average_alpha = PPVEGA12_VEGA12SOCCLKAVERAGEALPHA_DFLT;
	data->uclk_average_alpha = PPVEGA12_VEGA12UCLKCLKAVERAGEALPHA_DFLT;
	data->gfx_activity_average_alpha = PPVEGA12_VEGA12GFXACTIVITYAVERAGEALPHA_DFLT;
	data->lowest_uclk_reserved_for_ulv = PPVEGA12_VEGA12LOWESTUCLKRESERVEDFORULV_DFLT;

	data->display_voltage_mode = PPVEGA12_VEGA12DISPLAYVOLTAGEMODE_DFLT;
	data->dcef_clk_quad_eqn_a = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->dcef_clk_quad_eqn_b = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->dcef_clk_quad_eqn_c = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_a = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_b = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_c = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_a = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_b = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_c = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_a = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_b = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_c = PPREGKEY_VEGA12QUADRATICEQUATION_DFLT;

	data->registry_data.disallowed_features = 0x0;
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
	data->registry_data.odn_feature_enable = 1;
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
	data->registry_data.auto_wattman_threshold = 50;
	data->registry_data.pcie_dpm_key_disabled = !(hwmgr->feature_mask & PP_PCIE_DPM_MASK);
}

static int vega12_set_features_platform_caps(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	struct amdgpu_device *adev = hwmgr->adev;

	if (data->vddci_control == VEGA12_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ControlVDDCI);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TablelessHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableSMU7ThermalManagement);

	if (adev->pg_flags & AMD_PG_SUPPORT_UVD) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_UVDPowerGating);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_UVDDynamicPowerGating);
	}

	if (adev->pg_flags & AMD_PG_SUPPORT_VCE)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_VCEPowerGating);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UnTabledHardwareInterface);

	if (data->registry_data.odn_feature_enable)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ODNinACSupport);
	else {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_OD6inACSupport);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_OD6PlusinACSupport);
	}

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ActivityReporting);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_FanSpeedInTableIsRPM);

	if (data->registry_data.od_state_in_dc_support) {
		if (data->registry_data.odn_feature_enable)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_ODNinDCSupport);
		else {
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_OD6inDCSupport);
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_OD6PlusinDCSupport);
		}
	}

	if (data->registry_data.thermal_support
			&& data->registry_data.fuzzy_fan_control_support
			&& hwmgr->thermal_controller.advanceFanControlParameters.usTMax)
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
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DiDtSupport);
		if (data->registry_data.sq_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SQRamping);
		if (data->registry_data.db_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DBRamping);
		if (data->registry_data.td_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TDRamping);
		if (data->registry_data.tcp_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_TCPRamping);
		if (data->registry_data.dbr_ramping_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DBRRamping);
		if (data->registry_data.edc_didt_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DiDtEDCEnable);
		if (data->registry_data.gc_didt_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_GCEDC);
		if (data->registry_data.psm_didt_support)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PSM);
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

	if (data->lowest_uclk_reserved_for_ulv != PPVEGA12_VEGA12LOWESTUCLKRESERVEDFORULV_DFLT) {
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

static void vega12_init_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t top32, bottom32;
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
	data->smu_features[GNLD_ACG].smu_feature_id = FEATURE_ACG_BIT;

	for (i = 0; i < GNLD_FEATURES_MAX; i++) {
		data->smu_features[i].smu_feature_bitmap =
			(uint64_t)(1ULL << data->smu_features[i].smu_feature_id);
		data->smu_features[i].allowed =
			((data->registry_data.disallowed_features >> i) & 1) ?
			false : true;
	}

	/* Get the SN to turn into a Unique ID */
	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ReadSerialNumTop32, &top32);
	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ReadSerialNumBottom32, &bottom32);

	adev->unique_id = ((uint64_t)bottom32 << 32) | top32;
}

static int vega12_set_private_data_based_on_pptable(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int vega12_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	kfree(hwmgr->backend);
	hwmgr->backend = NULL;

	return 0;
}

static int vega12_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct vega12_hwmgr *data;
	struct amdgpu_device *adev = hwmgr->adev;

	data = kzalloc(sizeof(struct vega12_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	vega12_set_default_registry_data(hwmgr);

	data->disable_dpm_mask = 0xff;
	data->workload_mask = 0xff;

	/* need to set voltage control types before EVV patching */
	data->vddc_control = VEGA12_VOLTAGE_CONTROL_NONE;
	data->mvdd_control = VEGA12_VOLTAGE_CONTROL_NONE;
	data->vddci_control = VEGA12_VOLTAGE_CONTROL_NONE;

	data->water_marks_bitmap = 0;
	data->avfs_exist = false;

	vega12_set_features_platform_caps(hwmgr);

	vega12_init_dpm_defaults(hwmgr);

	/* Parse pptable data read from VBIOS */
	vega12_set_private_data_based_on_pptable(hwmgr);

	data->is_tlu_enabled = false;

	hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
			VEGA12_MAX_HARDWARE_POWERLEVELS;
	hwmgr->platform_descriptor.hardwarePerformanceLevels = 2;
	hwmgr->platform_descriptor.minimumClocksReductionPercentage = 50;

	hwmgr->platform_descriptor.vbiosInterruptId = 0x20000400; /* IRQ_SOURCE1_SW_INT */
	/* The true clock step depends on the frequency, typically 4.5 or 9 MHz. Here we use 5. */
	hwmgr->platform_descriptor.clockStep.engineClock = 500;
	hwmgr->platform_descriptor.clockStep.memoryClock = 500;

	data->total_active_cus = adev->gfx.cu_info.number;
	/* Setup default Overdrive Fan control settings */
	data->odn_fan_table.target_fan_speed =
			hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM;
	data->odn_fan_table.target_temperature =
			hwmgr->thermal_controller.advanceFanControlParameters.ucTargetTemperature;
	data->odn_fan_table.min_performance_clock =
			hwmgr->thermal_controller.advanceFanControlParameters.ulMinFanSCLKAcousticLimit;
	data->odn_fan_table.min_fan_limit =
			hwmgr->thermal_controller.advanceFanControlParameters.usFanPWMMinLimit *
			hwmgr->thermal_controller.fanInfo.ulMaxRPM / 100;

	if (hwmgr->feature_mask & PP_GFXOFF_MASK)
		data->gfxoff_controlled_by_driver = true;
	else
		data->gfxoff_controlled_by_driver = false;

	return result;
}

static int vega12_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);

	data->low_sclk_interrupt_threshold = 0;

	return 0;
}

static int vega12_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(!vega12_init_sclk_threshold(hwmgr),
			"Failed to init sclk threshold!",
			return -EINVAL);

	return 0;
}

/*
 * @fn vega12_init_dpm_state
 * @brief Function to initialize all Soft Min/Max and Hard Min/Max to 0xff.
 *
 * @param    dpm_state - the address of the DPM Table to initiailize.
 * @return   None.
 */
static void vega12_init_dpm_state(struct vega12_dpm_state *dpm_state)
{
	dpm_state->soft_min_level = 0x0;
	dpm_state->soft_max_level = 0xffff;
	dpm_state->hard_min_level = 0x0;
	dpm_state->hard_max_level = 0xffff;
}

static int vega12_override_pcie_parameters(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)(hwmgr->adev);
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t pcie_gen = 0, pcie_width = 0, smu_pcie_arg, pcie_gen_arg, pcie_width_arg;
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	int i;
	int ret;

	if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN4)
		pcie_gen = 3;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
		pcie_gen = 2;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2)
		pcie_gen = 1;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1)
		pcie_gen = 0;

	if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X16)
		pcie_width = 6;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X12)
		pcie_width = 5;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X8)
		pcie_width = 4;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X4)
		pcie_width = 3;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X2)
		pcie_width = 2;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X1)
		pcie_width = 1;

	/* Bit 31:16: LCLK DPM level. 0 is DPM0, and 1 is DPM1
	 * Bit 15:8:  PCIE GEN, 0 to 3 corresponds to GEN1 to GEN4
	 * Bit 7:0:   PCIE lane width, 1 to 7 corresponds is x1 to x32
	 */
	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		pcie_gen_arg = (pp_table->PcieGenSpeed[i] > pcie_gen) ? pcie_gen :
			pp_table->PcieGenSpeed[i];
		pcie_width_arg = (pp_table->PcieLaneCount[i] > pcie_width) ? pcie_width :
			pp_table->PcieLaneCount[i];

		if (pcie_gen_arg != pp_table->PcieGenSpeed[i] || pcie_width_arg !=
		    pp_table->PcieLaneCount[i]) {
			smu_pcie_arg = (i << 16) | (pcie_gen_arg << 8) | pcie_width_arg;
			ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_OverridePcieParameters, smu_pcie_arg,
				NULL);
			PP_ASSERT_WITH_CODE(!ret,
				"[OverridePcieParameters] Attempt to override pcie params failed!",
				return ret);
		}

		/* update the pptable */
		pp_table->PcieGenSpeed[i] = pcie_gen_arg;
		pp_table->PcieLaneCount[i] = pcie_width_arg;
	}

	/* override to the highest if it's disabled from ppfeaturmask */
	if (data->registry_data.pcie_dpm_key_disabled) {
		for (i = 0; i < NUM_LINK_LEVELS; i++) {
			smu_pcie_arg = (i << 16) | (pcie_gen << 8) | pcie_width;
			ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_OverridePcieParameters, smu_pcie_arg,
				NULL);
			PP_ASSERT_WITH_CODE(!ret,
				"[OverridePcieParameters] Attempt to override pcie params failed!",
				return ret);

			pp_table->PcieGenSpeed[i] = pcie_gen;
			pp_table->PcieLaneCount[i] = pcie_width;
		}
		ret = vega12_enable_smc_features(hwmgr,
				false,
				data->smu_features[GNLD_DPM_LINK].smu_feature_bitmap);
		PP_ASSERT_WITH_CODE(!ret,
				"Attempt to Disable DPM LINK Failed!",
				return ret);
		data->smu_features[GNLD_DPM_LINK].enabled = false;
		data->smu_features[GNLD_DPM_LINK].supported = false;
	}
	return 0;
}

static int vega12_get_number_of_dpm_level(struct pp_hwmgr *hwmgr,
		PPCLK_e clk_id, uint32_t *num_of_levels)
{
	int ret = 0;

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetDpmFreqByIndex,
			(clk_id << 16 | 0xFF),
			num_of_levels);
	PP_ASSERT_WITH_CODE(!ret,
			"[GetNumOfDpmLevel] failed to get dpm levels!",
			return ret);

	return ret;
}

static int vega12_get_dpm_frequency_by_index(struct pp_hwmgr *hwmgr,
		PPCLK_e clkID, uint32_t index, uint32_t *clock)
{
	/*
	 *SMU expects the Clock ID to be in the top 16 bits.
	 *Lower 16 bits specify the level
	 */
	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
		PPSMC_MSG_GetDpmFreqByIndex, (clkID << 16 | index),
		clock) == 0,
		"[GetDpmFrequencyByIndex] Failed to get dpm frequency from SMU!",
		return -EINVAL);

	return 0;
}

static int vega12_setup_single_dpm_table(struct pp_hwmgr *hwmgr,
		struct vega12_single_dpm_table *dpm_table, PPCLK_e clk_id)
{
	int ret = 0;
	uint32_t i, num_of_levels, clk;

	ret = vega12_get_number_of_dpm_level(hwmgr, clk_id, &num_of_levels);
	PP_ASSERT_WITH_CODE(!ret,
			"[SetupSingleDpmTable] failed to get clk levels!",
			return ret);

	dpm_table->count = num_of_levels;

	for (i = 0; i < num_of_levels; i++) {
		ret = vega12_get_dpm_frequency_by_index(hwmgr, clk_id, i, &clk);
		PP_ASSERT_WITH_CODE(!ret,
			"[SetupSingleDpmTable] failed to get clk of specific level!",
			return ret);
		dpm_table->dpm_levels[i].value = clk;
		dpm_table->dpm_levels[i].enabled = true;
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
static int vega12_setup_default_dpm_tables(struct pp_hwmgr *hwmgr)
{

	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *dpm_table;
	int ret = 0;

	memset(&data->dpm_table, 0, sizeof(data->dpm_table));

	/* socclk */
	dpm_table = &(data->dpm_table.soc_table);
	if (data->smu_features[GNLD_DPM_SOCCLK].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_SOCCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get socclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.soc_clock / 100;
	}
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* gfxclk */
	dpm_table = &(data->dpm_table.gfx_table);
	if (data->smu_features[GNLD_DPM_GFXCLK].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_GFXCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get gfxclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.gfx_clock / 100;
	}
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* memclk */
	dpm_table = &(data->dpm_table.mem_table);
	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_UCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get memclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.mem_clock / 100;
	}
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* eclk */
	dpm_table = &(data->dpm_table.eclk_table);
	if (data->smu_features[GNLD_DPM_VCE].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_ECLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get eclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.eclock / 100;
	}
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* vclk */
	dpm_table = &(data->dpm_table.vclk_table);
	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_VCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get vclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.vclock / 100;
	}
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* dclk */
	dpm_table = &(data->dpm_table.dclk_table);
	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_DCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get dclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.dclock / 100;
	}
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* dcefclk */
	dpm_table = &(data->dpm_table.dcef_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_DCEFCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get dcefclk dpm levels!",
				return ret);
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = data->vbios_boot_state.dcef_clock / 100;
	}
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* pixclk */
	dpm_table = &(data->dpm_table.pixel_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_PIXCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get pixclk dpm levels!",
				return ret);
	} else
		dpm_table->count = 0;
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* dispclk */
	dpm_table = &(data->dpm_table.display_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_DISPCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get dispclk dpm levels!",
				return ret);
	} else
		dpm_table->count = 0;
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* phyclk */
	dpm_table = &(data->dpm_table.phy_table);
	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		ret = vega12_setup_single_dpm_table(hwmgr, dpm_table, PPCLK_PHYCLK);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetupDefaultDpmTable] failed to get phyclk dpm levels!",
				return ret);
	} else
		dpm_table->count = 0;
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* save a copy of the default DPM table */
	memcpy(&(data->golden_dpm_table), &(data->dpm_table),
			sizeof(struct vega12_dpm_table));

	return 0;
}

#if 0
static int vega12_save_default_power_profile(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *dpm_table = &(data->dpm_table.gfx_table);
	uint32_t min_level;

	hwmgr->default_gfx_power_profile.type = AMD_PP_GFX_PROFILE;
	hwmgr->default_compute_power_profile.type = AMD_PP_COMPUTE_PROFILE;

	/* Optimize compute power profile: Use only highest
	 * 2 power levels (if more than 2 are available)
	 */
	if (dpm_table->count > 2)
		min_level = dpm_table->count - 2;
	else if (dpm_table->count == 2)
		min_level = 1;
	else
		min_level = 0;

	hwmgr->default_compute_power_profile.min_sclk =
			dpm_table->dpm_levels[min_level].value;

	hwmgr->gfx_power_profile = hwmgr->default_gfx_power_profile;
	hwmgr->compute_power_profile = hwmgr->default_compute_power_profile;

	return 0;
}
#endif

/**
 * Initializes the SMC table and uploads it
 *
 * @hwmgr:  the address of the powerplay hardware manager.
 * return:  always 0
 */
static int vega12_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct pp_atomfwctrl_bios_boot_up_values boot_up_values;
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;

	result = pp_atomfwctrl_get_vbios_bootup_values(hwmgr, &boot_up_values);
	if (!result) {
		data->vbios_boot_state.vddc     = boot_up_values.usVddc;
		data->vbios_boot_state.vddci    = boot_up_values.usVddci;
		data->vbios_boot_state.mvddc    = boot_up_values.usMvddc;
		data->vbios_boot_state.gfx_clock = boot_up_values.ulGfxClk;
		data->vbios_boot_state.mem_clock = boot_up_values.ulUClk;
		data->vbios_boot_state.soc_clock = boot_up_values.ulSocClk;
		data->vbios_boot_state.dcef_clock = boot_up_values.ulDCEFClk;
		data->vbios_boot_state.uc_cooling_id = boot_up_values.ucCoolingID;
		data->vbios_boot_state.eclock = boot_up_values.ulEClk;
		data->vbios_boot_state.dclock = boot_up_values.ulDClk;
		data->vbios_boot_state.vclock = boot_up_values.ulVClk;
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetMinDeepSleepDcefclk,
			(uint32_t)(data->vbios_boot_state.dcef_clock / 100),
				NULL);
	}

	memcpy(pp_table, pptable_information->smc_pptable, sizeof(PPTable_t));

	result = smum_smc_table_manager(hwmgr,
					(uint8_t *)pp_table, TABLE_PPTABLE, false);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload PPtable!", return result);

	return 0;
}

static int vega12_run_acg_btc(struct pp_hwmgr *hwmgr)
{
	uint32_t result;

	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_RunAcgBtc, &result) == 0,
		"[Run_ACG_BTC] Attempt to run ACG BTC failed!",
		return -EINVAL);

	PP_ASSERT_WITH_CODE(result == 1,
			"Failed to run ACG BTC!", return -EINVAL);

	return 0;
}

static int vega12_set_allowed_featuresmask(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	int i;
	uint32_t allowed_features_low = 0, allowed_features_high = 0;

	for (i = 0; i < GNLD_FEATURES_MAX; i++)
		if (data->smu_features[i].allowed)
			data->smu_features[i].smu_feature_id > 31 ?
				(allowed_features_high |= ((data->smu_features[i].smu_feature_bitmap >> SMU_FEATURES_HIGH_SHIFT) & 0xFFFFFFFF)) :
				(allowed_features_low |= ((data->smu_features[i].smu_feature_bitmap >> SMU_FEATURES_LOW_SHIFT) & 0xFFFFFFFF));

	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetAllowedFeaturesMaskHigh, allowed_features_high,
			NULL) == 0,
		"[SetAllowedFeaturesMask] Attempt to set allowed features mask (high) failed!",
		return -1);

	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetAllowedFeaturesMaskLow, allowed_features_low,
			NULL) == 0,
		"[SetAllowedFeaturesMask] Attempt to set allowed features mask (low) failed!",
		return -1);

	return 0;
}

static void vega12_init_powergate_state(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = true;
	data->vce_power_gated = true;

	if (data->smu_features[GNLD_DPM_UVD].enabled)
		data->uvd_power_gated = false;

	if (data->smu_features[GNLD_DPM_VCE].enabled)
		data->vce_power_gated = false;
}

static int vega12_enable_all_smu_features(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint64_t features_enabled;
	int i;
	bool enabled;

	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_EnableAllSmuFeatures, NULL) == 0,
		"[EnableAllSMUFeatures] Failed to enable all smu features!",
		return -1);

	if (vega12_get_enabled_smc_features(hwmgr, &features_enabled) == 0) {
		for (i = 0; i < GNLD_FEATURES_MAX; i++) {
			enabled = (features_enabled & data->smu_features[i].smu_feature_bitmap) ? true : false;
			data->smu_features[i].enabled = enabled;
			data->smu_features[i].supported = enabled;
		}
	}

	vega12_init_powergate_state(hwmgr);

	return 0;
}

static int vega12_disable_all_smu_features(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint64_t features_enabled;
	int i;
	bool enabled;

	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_DisableAllSmuFeatures, NULL) == 0,
		"[DisableAllSMUFeatures] Failed to disable all smu features!",
		return -1);

	if (vega12_get_enabled_smc_features(hwmgr, &features_enabled) == 0) {
		for (i = 0; i < GNLD_FEATURES_MAX; i++) {
			enabled = (features_enabled & data->smu_features[i].smu_feature_bitmap) ? true : false;
			data->smu_features[i].enabled = enabled;
			data->smu_features[i].supported = enabled;
		}
	}

	return 0;
}

static int vega12_odn_initialize_default_settings(
		struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int vega12_set_overdrive_target_percentage(struct pp_hwmgr *hwmgr,
		uint32_t adjust_percent)
{
	return smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_OverDriveSetPercentage, adjust_percent,
			NULL);
}

static int vega12_power_control_set_level(struct pp_hwmgr *hwmgr)
{
	int adjust_percent, result = 0;

	if (PP_CAP(PHM_PlatformCaps_PowerContainment)) {
		adjust_percent =
				hwmgr->platform_descriptor.TDPAdjustmentPolarity ?
				hwmgr->platform_descriptor.TDPAdjustment :
				(-1 * hwmgr->platform_descriptor.TDPAdjustment);
		result = vega12_set_overdrive_target_percentage(hwmgr,
				(uint32_t)adjust_percent);
	}
	return result;
}

static int vega12_get_all_clock_ranges_helper(struct pp_hwmgr *hwmgr,
		PPCLK_e clkid, struct vega12_clock_range *clock)
{
	/* AC Max */
	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetMaxDpmFreq, (clkid << 16),
			&(clock->ACMax)) == 0,
		"[GetClockRanges] Failed to get max ac clock from SMC!",
		return -EINVAL);

	/* AC Min */
	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetMinDpmFreq, (clkid << 16),
			&(clock->ACMin)) == 0,
		"[GetClockRanges] Failed to get min ac clock from SMC!",
		return -EINVAL);

	/* DC Max */
	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetDcModeMaxDpmFreq, (clkid << 16),
			&(clock->DCMax)) == 0,
		"[GetClockRanges] Failed to get max dc clock from SMC!",
		return -EINVAL);

	return 0;
}

static int vega12_get_all_clock_ranges(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t i;

	for (i = 0; i < PPCLK_COUNT; i++)
		PP_ASSERT_WITH_CODE(!vega12_get_all_clock_ranges_helper(hwmgr,
					i, &(data->clk_range[i])),
				"Failed to get clk range from SMC!",
				return -EINVAL);

	return 0;
}

static int vega12_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, 0, NULL);

	result = vega12_set_allowed_featuresmask(hwmgr);
	PP_ASSERT_WITH_CODE(result == 0,
			"[EnableDPMTasks] Failed to set allowed featuresmask!\n",
			return result);

	tmp_result = vega12_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to initialize SMC table!",
			result = tmp_result);

	tmp_result = vega12_run_acg_btc(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to run ACG BTC!",
			result = tmp_result);

	result = vega12_enable_all_smu_features(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to enable all smu features!",
			return result);

	result = vega12_override_pcie_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"[EnableDPMTasks] Failed to override pcie parameters!",
			return result);

	tmp_result = vega12_power_control_set_level(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to power control set level!",
			result = tmp_result);

	result = vega12_get_all_clock_ranges(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to get all clock ranges!",
			return result);

	result = vega12_odn_initialize_default_settings(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to power control set level!",
			return result);

	result = vega12_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to setup default DPM tables!",
			return result);
	return result;
}

static int vega12_patch_boot_state(struct pp_hwmgr *hwmgr,
	     struct pp_hw_power_state *hw_ps)
{
	return 0;
}

static uint32_t vega12_find_lowest_dpm_level(
		struct vega12_single_dpm_table *table)
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

static uint32_t vega12_find_highest_dpm_level(
		struct vega12_single_dpm_table *table)
{
	int32_t i = 0;
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

	return (uint32_t)i;
}

static int vega12_upload_dpm_min_level(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = hwmgr->backend;
	uint32_t min_freq;
	int ret = 0;

	if (data->smu_features[GNLD_DPM_GFXCLK].enabled) {
		min_freq = data->dpm_table.gfx_table.dpm_state.soft_min_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_GFXCLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set soft min gfxclk !",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		min_freq = data->dpm_table.mem_table.dpm_state.soft_min_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_UCLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set soft min memclk !",
					return ret);

		min_freq = data->dpm_table.mem_table.dpm_state.hard_min_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetHardMinByFreq,
					(PPCLK_UCLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set hard min memclk !",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		min_freq = data->dpm_table.vclk_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_VCLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set soft min vclk!",
					return ret);

		min_freq = data->dpm_table.dclk_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_DCLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set soft min dclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_VCE].enabled) {
		min_freq = data->dpm_table.eclk_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_ECLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set soft min eclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_SOCCLK].enabled) {
		min_freq = data->dpm_table.soc_table.dpm_state.soft_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMinByFreq,
					(PPCLK_SOCCLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set soft min socclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_DCEFCLK].enabled) {
		min_freq = data->dpm_table.dcef_table.dpm_state.hard_min_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetHardMinByFreq,
					(PPCLK_DCEFCLK << 16) | (min_freq & 0xffff),
					NULL)),
					"Failed to set hard min dcefclk!",
					return ret);
	}

	return ret;

}

static int vega12_upload_dpm_max_level(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = hwmgr->backend;
	uint32_t max_freq;
	int ret = 0;

	if (data->smu_features[GNLD_DPM_GFXCLK].enabled) {
		max_freq = data->dpm_table.gfx_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_GFXCLK << 16) | (max_freq & 0xffff),
					NULL)),
					"Failed to set soft max gfxclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		max_freq = data->dpm_table.mem_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_UCLK << 16) | (max_freq & 0xffff),
					NULL)),
					"Failed to set soft max memclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_UVD].enabled) {
		max_freq = data->dpm_table.vclk_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_VCLK << 16) | (max_freq & 0xffff),
					NULL)),
					"Failed to set soft max vclk!",
					return ret);

		max_freq = data->dpm_table.dclk_table.dpm_state.soft_max_level;
		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_DCLK << 16) | (max_freq & 0xffff),
					NULL)),
					"Failed to set soft max dclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_VCE].enabled) {
		max_freq = data->dpm_table.eclk_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_ECLK << 16) | (max_freq & 0xffff),
					NULL)),
					"Failed to set soft max eclk!",
					return ret);
	}

	if (data->smu_features[GNLD_DPM_SOCCLK].enabled) {
		max_freq = data->dpm_table.soc_table.dpm_state.soft_max_level;

		PP_ASSERT_WITH_CODE(!(ret = smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetSoftMaxByFreq,
					(PPCLK_SOCCLK << 16) | (max_freq & 0xffff),
					NULL)),
					"Failed to set soft max socclk!",
					return ret);
	}

	return ret;
}

int vega12_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_DPM_VCE].supported) {
		PP_ASSERT_WITH_CODE(!vega12_enable_smc_features(hwmgr,
				enable,
				data->smu_features[GNLD_DPM_VCE].smu_feature_bitmap),
				"Attempt to Enable/Disable DPM VCE Failed!",
				return -1);
		data->smu_features[GNLD_DPM_VCE].enabled = enable;
	}

	return 0;
}

static uint32_t vega12_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t gfx_clk;

	if (!data->smu_features[GNLD_DPM_GFXCLK].enabled)
		return -1;

	if (low)
		PP_ASSERT_WITH_CODE(
			vega12_get_clock_ranges(hwmgr, &gfx_clk, PPCLK_GFXCLK, false) == 0,
			"[GetSclks]: fail to get min PPCLK_GFXCLK\n",
			return -1);
	else
		PP_ASSERT_WITH_CODE(
			vega12_get_clock_ranges(hwmgr, &gfx_clk, PPCLK_GFXCLK, true) == 0,
			"[GetSclks]: fail to get max PPCLK_GFXCLK\n",
			return -1);

	return (gfx_clk * 100);
}

static uint32_t vega12_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t mem_clk;

	if (!data->smu_features[GNLD_DPM_UCLK].enabled)
		return -1;

	if (low)
		PP_ASSERT_WITH_CODE(
			vega12_get_clock_ranges(hwmgr, &mem_clk, PPCLK_UCLK, false) == 0,
			"[GetMclks]: fail to get min PPCLK_UCLK\n",
			return -1);
	else
		PP_ASSERT_WITH_CODE(
			vega12_get_clock_ranges(hwmgr, &mem_clk, PPCLK_UCLK, true) == 0,
			"[GetMclks]: fail to get max PPCLK_UCLK\n",
			return -1);

	return (mem_clk * 100);
}

static int vega12_get_metrics_table(struct pp_hwmgr *hwmgr,
				    SmuMetrics_t *metrics_table,
				    bool bypass_cache)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	int ret = 0;

	if (bypass_cache ||
	    !data->metrics_time ||
	    time_after(jiffies, data->metrics_time + msecs_to_jiffies(1))) {
		ret = smum_smc_table_manager(hwmgr,
					     (uint8_t *)(&data->metrics_table),
					     TABLE_SMU_METRICS,
					     true);
		if (ret) {
			pr_info("Failed to export SMU metrics table!\n");
			return ret;
		}
		data->metrics_time = jiffies;
	}

	if (metrics_table)
		memcpy(metrics_table, &data->metrics_table, sizeof(SmuMetrics_t));

	return ret;
}

static int vega12_get_gpu_power(struct pp_hwmgr *hwmgr, uint32_t *query)
{
	SmuMetrics_t metrics_table;
	int ret = 0;

	ret = vega12_get_metrics_table(hwmgr, &metrics_table, false);
	if (ret)
		return ret;

	*query = metrics_table.CurrSocketPower << 8;

	return ret;
}

static int vega12_get_current_gfx_clk_freq(struct pp_hwmgr *hwmgr, uint32_t *gfx_freq)
{
	uint32_t gfx_clk = 0;

	*gfx_freq = 0;

	PP_ASSERT_WITH_CODE(smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_GetDpmClockFreq, (PPCLK_GFXCLK << 16),
			&gfx_clk) == 0,
			"[GetCurrentGfxClkFreq] Attempt to get Current GFXCLK Frequency Failed!",
			return -EINVAL);

	*gfx_freq = gfx_clk * 100;

	return 0;
}

static int vega12_get_current_mclk_freq(struct pp_hwmgr *hwmgr, uint32_t *mclk_freq)
{
	uint32_t mem_clk = 0;

	*mclk_freq = 0;

	PP_ASSERT_WITH_CODE(
			smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetDpmClockFreq, (PPCLK_UCLK << 16),
				&mem_clk) == 0,
			"[GetCurrentMClkFreq] Attempt to get Current MCLK Frequency Failed!",
			return -EINVAL);

	*mclk_freq = mem_clk * 100;

	return 0;
}

static int vega12_get_current_activity_percent(
		struct pp_hwmgr *hwmgr,
		int idx,
		uint32_t *activity_percent)
{
	SmuMetrics_t metrics_table;
	int ret = 0;

	ret = vega12_get_metrics_table(hwmgr, &metrics_table, false);
	if (ret)
		return ret;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		*activity_percent = metrics_table.AverageGfxActivity;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		*activity_percent = metrics_table.AverageUclkActivity;
		break;
	default:
		pr_err("Invalid index for retrieving clock activity\n");
		return -EINVAL;
	}

	return ret;
}

static int vega12_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			      void *value, int *size)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	SmuMetrics_t metrics_table;
	int ret = 0;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = vega12_get_current_gfx_clk_freq(hwmgr, (uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = vega12_get_current_mclk_freq(hwmgr, (uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		ret = vega12_get_current_activity_percent(hwmgr, idx, (uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = vega12_thermal_get_temperature(hwmgr);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = vega12_get_metrics_table(hwmgr, &metrics_table, false);
		if (ret)
			return ret;

		*((uint32_t *)value) = metrics_table.TemperatureHotspot *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = vega12_get_metrics_table(hwmgr, &metrics_table, false);
		if (ret)
			return ret;

		*((uint32_t *)value) = metrics_table.TemperatureHBM *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
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
		ret = vega12_get_gpu_power(hwmgr, (uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK:
		ret = vega12_get_enabled_smc_features(hwmgr, (uint64_t *)value);
		if (!ret)
			*size = 8;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}

static int vega12_notify_smc_display_change(struct pp_hwmgr *hwmgr,
		bool has_disp)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_DPM_UCLK].enabled)
		return smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetUclkFastSwitch,
			has_disp ? 1 : 0,
			NULL);

	return 0;
}

static int vega12_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
		struct pp_display_clock_request *clock_req)
{
	int result = 0;
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
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
			result = -1;
			break;
		}

		if (!result) {
			clk_request = (clk_select << 16) | clk_freq;
			result = smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetHardMinByFreq,
					clk_request,
					NULL);
		}
	}

	return result;
}

static int vega12_notify_smc_display_config_after_ps_adjustment(
		struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	struct PP_Clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;

	if ((hwmgr->display_config->num_display > 1) &&
	     !hwmgr->display_config->multi_monitor_in_sync &&
	     !hwmgr->display_config->nb_pstate_switch_disable)
		vega12_notify_smc_display_change(hwmgr, false);
	else
		vega12_notify_smc_display_change(hwmgr, true);

	min_clocks.dcefClock = hwmgr->display_config->min_dcef_set_clk;
	min_clocks.dcefClockInSR = hwmgr->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memoryClock = hwmgr->display_config->min_mem_set_clock;

	if (data->smu_features[GNLD_DPM_DCEFCLK].supported) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcefClock/10;
		if (!vega12_display_clock_voltage_request(hwmgr, &clock_req)) {
			if (data->smu_features[GNLD_DS_DCEFCLK].supported)
				PP_ASSERT_WITH_CODE(
					!smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetMinDeepSleepDcefclk,
					min_clocks.dcefClockInSR /100,
					NULL),
					"Attempt to set divider for DCEFCLK Failed!",
					return -1);
		} else {
			pr_info("Attempt to set Hard Min for DCEFCLK Failed!");
		}
	}

	return 0;
}

static int vega12_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);

	uint32_t soft_level;

	soft_level = vega12_find_highest_dpm_level(&(data->dpm_table.gfx_table));

	data->dpm_table.gfx_table.dpm_state.soft_min_level =
		data->dpm_table.gfx_table.dpm_state.soft_max_level =
		data->dpm_table.gfx_table.dpm_levels[soft_level].value;

	soft_level = vega12_find_highest_dpm_level(&(data->dpm_table.mem_table));

	data->dpm_table.mem_table.dpm_state.soft_min_level =
		data->dpm_table.mem_table.dpm_state.soft_max_level =
		data->dpm_table.mem_table.dpm_levels[soft_level].value;

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_min_level(hwmgr),
			"Failed to upload boot level to highest!",
			return -1);

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -1);

	return 0;
}

static int vega12_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t soft_level;

	soft_level = vega12_find_lowest_dpm_level(&(data->dpm_table.gfx_table));

	data->dpm_table.gfx_table.dpm_state.soft_min_level =
		data->dpm_table.gfx_table.dpm_state.soft_max_level =
		data->dpm_table.gfx_table.dpm_levels[soft_level].value;

	soft_level = vega12_find_lowest_dpm_level(&(data->dpm_table.mem_table));

	data->dpm_table.mem_table.dpm_state.soft_min_level =
		data->dpm_table.mem_table.dpm_state.soft_max_level =
		data->dpm_table.mem_table.dpm_levels[soft_level].value;

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_min_level(hwmgr),
			"Failed to upload boot level to highest!",
			return -1);

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -1);

	return 0;

}

static int vega12_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_min_level(hwmgr),
			"Failed to upload DPM Bootup Levels!",
			return -1);

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_max_level(hwmgr),
			"Failed to upload DPM Max Levels!",
			return -1);

	return 0;
}

static int vega12_get_profiling_clk_mask(struct pp_hwmgr *hwmgr, enum amd_dpm_forced_level level,
				uint32_t *sclk_mask, uint32_t *mclk_mask, uint32_t *soc_mask)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *gfx_dpm_table = &(data->dpm_table.gfx_table);
	struct vega12_single_dpm_table *mem_dpm_table = &(data->dpm_table.mem_table);
	struct vega12_single_dpm_table *soc_dpm_table = &(data->dpm_table.soc_table);

	*sclk_mask = 0;
	*mclk_mask = 0;
	*soc_mask  = 0;

	if (gfx_dpm_table->count > VEGA12_UMD_PSTATE_GFXCLK_LEVEL &&
	    mem_dpm_table->count > VEGA12_UMD_PSTATE_MCLK_LEVEL &&
	    soc_dpm_table->count > VEGA12_UMD_PSTATE_SOCCLK_LEVEL) {
		*sclk_mask = VEGA12_UMD_PSTATE_GFXCLK_LEVEL;
		*mclk_mask = VEGA12_UMD_PSTATE_MCLK_LEVEL;
		*soc_mask  = VEGA12_UMD_PSTATE_SOCCLK_LEVEL;
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

static void vega12_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	switch (mode) {
	case AMD_FAN_CTRL_NONE:
		break;
	case AMD_FAN_CTRL_MANUAL:
		if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
			vega12_fan_ctrl_stop_smc_fan_control(hwmgr);
		break;
	case AMD_FAN_CTRL_AUTO:
		if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
			vega12_fan_ctrl_start_smc_fan_control(hwmgr);
		break;
	default:
		break;
	}
}

static int vega12_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;
	uint32_t sclk_mask = 0;
	uint32_t mclk_mask = 0;
	uint32_t soc_mask = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = vega12_force_dpm_highest(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = vega12_force_dpm_lowest(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = vega12_unforce_dpm_levels(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = vega12_get_profiling_clk_mask(hwmgr, level, &sclk_mask, &mclk_mask, &soc_mask);
		if (ret)
			return ret;
		vega12_force_clock_level(hwmgr, PP_SCLK, 1 << sclk_mask);
		vega12_force_clock_level(hwmgr, PP_MCLK, 1 << mclk_mask);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}

	return ret;
}

static uint32_t vega12_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_FAN_CONTROL].enabled == false)
		return AMD_FAN_CTRL_MANUAL;
	else
		return AMD_FAN_CTRL_AUTO;
}

static int vega12_get_dal_power_level(struct pp_hwmgr *hwmgr,
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

static int vega12_get_clock_ranges(struct pp_hwmgr *hwmgr,
		uint32_t *clock,
		PPCLK_e clock_select,
		bool max)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	if (max)
		*clock = data->clk_range[clock_select].ACMax;
	else
		*clock = data->clk_range[clock_select].ACMin;

	return 0;
}

static int vega12_get_sclks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t ucount;
	int i;
	struct vega12_single_dpm_table *dpm_table;

	if (!data->smu_features[GNLD_DPM_GFXCLK].enabled)
		return -1;

	dpm_table = &(data->dpm_table.gfx_table);
	ucount = (dpm_table->count > MAX_NUM_CLOCKS) ?
		MAX_NUM_CLOCKS : dpm_table->count;

	for (i = 0; i < ucount; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;

		clocks->data[i].latency_in_us = 0;
	}

	clocks->num_levels = ucount;

	return 0;
}

static uint32_t vega12_get_mem_latency(struct pp_hwmgr *hwmgr,
		uint32_t clock)
{
	return 25;
}

static int vega12_get_memclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t ucount;
	int i;
	struct vega12_single_dpm_table *dpm_table;
	if (!data->smu_features[GNLD_DPM_UCLK].enabled)
		return -1;

	dpm_table = &(data->dpm_table.mem_table);
	ucount = (dpm_table->count > MAX_NUM_CLOCKS) ?
		MAX_NUM_CLOCKS : dpm_table->count;

	for (i = 0; i < ucount; i++) {
		clocks->data[i].clocks_in_khz = dpm_table->dpm_levels[i].value * 1000;
		data->mclk_latency_table.entries[i].frequency = dpm_table->dpm_levels[i].value * 100;
		clocks->data[i].latency_in_us =
			data->mclk_latency_table.entries[i].latency =
			vega12_get_mem_latency(hwmgr, dpm_table->dpm_levels[i].value);
	}

	clocks->num_levels = data->mclk_latency_table.count = ucount;

	return 0;
}

static int vega12_get_dcefclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t ucount;
	int i;
	struct vega12_single_dpm_table *dpm_table;

	if (!data->smu_features[GNLD_DPM_DCEFCLK].enabled)
		return -1;


	dpm_table = &(data->dpm_table.dcef_table);
	ucount = (dpm_table->count > MAX_NUM_CLOCKS) ?
		MAX_NUM_CLOCKS : dpm_table->count;

	for (i = 0; i < ucount; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;

		clocks->data[i].latency_in_us = 0;
	}

	clocks->num_levels = ucount;

	return 0;
}

static int vega12_get_socclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t ucount;
	int i;
	struct vega12_single_dpm_table *dpm_table;

	if (!data->smu_features[GNLD_DPM_SOCCLK].enabled)
		return -1;


	dpm_table = &(data->dpm_table.soc_table);
	ucount = (dpm_table->count > MAX_NUM_CLOCKS) ?
		MAX_NUM_CLOCKS : dpm_table->count;

	for (i = 0; i < ucount; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;

		clocks->data[i].latency_in_us = 0;
	}

	clocks->num_levels = ucount;

	return 0;

}

static int vega12_get_clock_by_type_with_latency(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks)
{
	int ret;

	switch (type) {
	case amd_pp_sys_clock:
		ret = vega12_get_sclks(hwmgr, clocks);
		break;
	case amd_pp_mem_clock:
		ret = vega12_get_memclocks(hwmgr, clocks);
		break;
	case amd_pp_dcef_clock:
		ret = vega12_get_dcefclocks(hwmgr, clocks);
		break;
	case amd_pp_soc_clock:
		ret = vega12_get_socclocks(hwmgr, clocks);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int vega12_get_clock_by_type_with_voltage(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks)
{
	clocks->num_levels = 0;

	return 0;
}

static int vega12_set_watermarks_for_clocks_ranges(struct pp_hwmgr *hwmgr,
							void *clock_ranges)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
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

static int vega12_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t soft_min_level, soft_max_level, hard_min_level;
	int ret = 0;

	switch (type) {
	case PP_SCLK:
		soft_min_level = mask ? (ffs(mask) - 1) : 0;
		soft_max_level = mask ? (fls(mask) - 1) : 0;

		data->dpm_table.gfx_table.dpm_state.soft_min_level =
			data->dpm_table.gfx_table.dpm_levels[soft_min_level].value;
		data->dpm_table.gfx_table.dpm_state.soft_max_level =
			data->dpm_table.gfx_table.dpm_levels[soft_max_level].value;

		ret = vega12_upload_dpm_min_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to lowest!",
			return ret);

		ret = vega12_upload_dpm_max_level(hwmgr);
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

		ret = vega12_upload_dpm_min_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to lowest!",
			return ret);

		ret = vega12_upload_dpm_max_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload dpm max level to highest!",
			return ret);

		break;

	case PP_SOCCLK:
		soft_min_level = mask ? (ffs(mask) - 1) : 0;
		soft_max_level = mask ? (fls(mask) - 1) : 0;

		if (soft_max_level >= data->dpm_table.soc_table.count) {
			pr_err("Clock level specified %d is over max allowed %d\n",
					soft_max_level,
					data->dpm_table.soc_table.count - 1);
			return -EINVAL;
		}

		data->dpm_table.soc_table.dpm_state.soft_min_level =
			data->dpm_table.soc_table.dpm_levels[soft_min_level].value;
		data->dpm_table.soc_table.dpm_state.soft_max_level =
			data->dpm_table.soc_table.dpm_levels[soft_max_level].value;

		ret = vega12_upload_dpm_min_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to lowest!",
			return ret);

		ret = vega12_upload_dpm_max_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload dpm max level to highest!",
			return ret);

		break;

	case PP_DCEFCLK:
		hard_min_level = mask ? (ffs(mask) - 1) : 0;

		if (hard_min_level >= data->dpm_table.dcef_table.count) {
			pr_err("Clock level specified %d is over max allowed %d\n",
					hard_min_level,
					data->dpm_table.dcef_table.count - 1);
			return -EINVAL;
		}

		data->dpm_table.dcef_table.dpm_state.hard_min_level =
			data->dpm_table.dcef_table.dpm_levels[hard_min_level].value;

		ret = vega12_upload_dpm_min_level(hwmgr);
		PP_ASSERT_WITH_CODE(!ret,
			"Failed to upload boot level to lowest!",
			return ret);

		//TODO: Setting DCEFCLK max dpm level is not supported

		break;

	case PP_PCIE:
		break;

	default:
		break;
	}

	return 0;
}

static int vega12_get_ppfeature_status(struct pp_hwmgr *hwmgr, char *buf)
{
	static const char *ppfeature_name[] = {
			"DPM_PREFETCHER",
			"GFXCLK_DPM",
			"UCLK_DPM",
			"SOCCLK_DPM",
			"UVD_DPM",
			"VCE_DPM",
			"ULV",
			"MP0CLK_DPM",
			"LINK_DPM",
			"DCEFCLK_DPM",
			"GFXCLK_DS",
			"SOCCLK_DS",
			"LCLK_DS",
			"PPT",
			"TDC",
			"THERMAL",
			"GFX_PER_CU_CG",
			"RM",
			"DCEFCLK_DS",
			"ACDC",
			"VR0HOT",
			"VR1HOT",
			"FW_CTF",
			"LED_DISPLAY",
			"FAN_CONTROL",
			"DIDT",
			"GFXOFF",
			"CG",
			"ACG"};
	static const char *output_title[] = {
			"FEATURES",
			"BITMASK",
			"ENABLEMENT"};
	uint64_t features_enabled;
	int i;
	int ret = 0;
	int size = 0;

	ret = vega12_get_enabled_smc_features(hwmgr, &features_enabled);
	PP_ASSERT_WITH_CODE(!ret,
		"[EnableAllSmuFeatures] Failed to get enabled smc features!",
		return ret);

	size += sprintf(buf + size, "Current ppfeatures: 0x%016llx\n", features_enabled);
	size += sprintf(buf + size, "%-19s %-22s %s\n",
				output_title[0],
				output_title[1],
				output_title[2]);
	for (i = 0; i < GNLD_FEATURES_MAX; i++) {
		size += sprintf(buf + size, "%-19s 0x%016llx %6s\n",
				ppfeature_name[i],
				1ULL << i,
				(features_enabled & (1ULL << i)) ? "Y" : "N");
	}

	return size;
}

static int vega12_set_ppfeature_status(struct pp_hwmgr *hwmgr, uint64_t new_ppfeature_masks)
{
	uint64_t features_enabled;
	uint64_t features_to_enable;
	uint64_t features_to_disable;
	int ret = 0;

	if (new_ppfeature_masks >= (1ULL << GNLD_FEATURES_MAX))
		return -EINVAL;

	ret = vega12_get_enabled_smc_features(hwmgr, &features_enabled);
	if (ret)
		return ret;

	features_to_disable =
		features_enabled & ~new_ppfeature_masks;
	features_to_enable =
		~features_enabled & new_ppfeature_masks;

	pr_debug("features_to_disable 0x%llx\n", features_to_disable);
	pr_debug("features_to_enable 0x%llx\n", features_to_enable);

	if (features_to_disable) {
		ret = vega12_enable_smc_features(hwmgr, false, features_to_disable);
		if (ret)
			return ret;
	}

	if (features_to_enable) {
		ret = vega12_enable_smc_features(hwmgr, true, features_to_enable);
		if (ret)
			return ret;
	}

	return 0;
}

static int vega12_get_current_pcie_link_width_level(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	return (RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL) &
		PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
		>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;
}

static int vega12_get_current_pcie_link_width(struct pp_hwmgr *hwmgr)
{
	uint32_t width_level;

	width_level = vega12_get_current_pcie_link_width_level(hwmgr);
	if (width_level > LINK_WIDTH_MAX)
		width_level = 0;

	return link_width[width_level];
}

static int vega12_get_current_pcie_link_speed_level(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	return (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
		PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
		>> PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
}

static int vega12_get_current_pcie_link_speed(struct pp_hwmgr *hwmgr)
{
	uint32_t speed_level;

	speed_level = vega12_get_current_pcie_link_speed_level(hwmgr);
	if (speed_level > LINK_SPEED_MAX)
		speed_level = 0;

	return link_speed[speed_level];
}

static int vega12_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	int i, now, size = 0;
	struct pp_clock_levels_with_latency clocks;

	switch (type) {
	case PP_SCLK:
		PP_ASSERT_WITH_CODE(
				vega12_get_current_gfx_clk_freq(hwmgr, &now) == 0,
				"Attempt to get current gfx clk Failed!",
				return -1);

		PP_ASSERT_WITH_CODE(
				vega12_get_sclks(hwmgr, &clocks) == 0,
				"Attempt to get gfx clk levels Failed!",
				return -1);
		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz / 1000 == now / 100) ? "*" : "");
		break;

	case PP_MCLK:
		PP_ASSERT_WITH_CODE(
				vega12_get_current_mclk_freq(hwmgr, &now) == 0,
				"Attempt to get current mclk freq Failed!",
				return -1);

		PP_ASSERT_WITH_CODE(
				vega12_get_memclocks(hwmgr, &clocks) == 0,
				"Attempt to get memory clk levels Failed!",
				return -1);
		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz / 1000 == now / 100) ? "*" : "");
		break;

	case PP_SOCCLK:
		PP_ASSERT_WITH_CODE(
				smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_GetDpmClockFreq, (PPCLK_SOCCLK << 16),
					&now) == 0,
				"Attempt to get Current SOCCLK Frequency Failed!",
				return -EINVAL);

		PP_ASSERT_WITH_CODE(
				vega12_get_socclocks(hwmgr, &clocks) == 0,
				"Attempt to get soc clk levels Failed!",
				return -1);
		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz / 1000 == now) ? "*" : "");
		break;

	case PP_DCEFCLK:
		PP_ASSERT_WITH_CODE(
				smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_GetDpmClockFreq, (PPCLK_DCEFCLK << 16),
					&now) == 0,
				"Attempt to get Current DCEFCLK Frequency Failed!",
				return -EINVAL);

		PP_ASSERT_WITH_CODE(
				vega12_get_dcefclocks(hwmgr, &clocks) == 0,
				"Attempt to get dcef clk levels Failed!",
				return -1);
		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz / 1000 == now) ? "*" : "");
		break;

	case PP_PCIE:
		break;

	default:
		break;
	}
	return size;
}

static int vega12_apply_clocks_adjust_rules(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *dpm_table;
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
		if (VEGA12_UMD_PSTATE_GFXCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_GFXCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_GFXCLK_LEVEL].value;
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
		if (VEGA12_UMD_PSTATE_MCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_MCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_MCLK_LEVEL].value;
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
		if (VEGA12_UMD_PSTATE_UVDCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_UVDCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_UVDCLK_LEVEL].value;
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
		if (VEGA12_UMD_PSTATE_UVDCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_UVDCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_UVDCLK_LEVEL].value;
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
		if (VEGA12_UMD_PSTATE_SOCCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_SOCCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_SOCCLK_LEVEL].value;
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
		if (VEGA12_UMD_PSTATE_VCEMCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_VCEMCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA12_UMD_PSTATE_VCEMCLK_LEVEL].value;
		}

		if (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	}

	return 0;
}

static int vega12_set_uclk_to_highest_dpm_level(struct pp_hwmgr *hwmgr,
		struct vega12_single_dpm_table *dpm_table)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
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
				(PPCLK_UCLK << 16 ) | dpm_table->dpm_state.hard_min_level,
				NULL)),
				"[SetUclkToHightestDpmLevel] Set hard min uclk failed!",
				return ret);
	}

	return ret;
}

static int vega12_pre_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	int ret = 0;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, 0,
			NULL);

	ret = vega12_set_uclk_to_highest_dpm_level(hwmgr,
			&data->dpm_table.mem_table);

	return ret;
}

static int vega12_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	int result = 0;
	Watermarks_t *wm_table = &(data->smc_state_table.water_marks_table);

	if ((data->water_marks_bitmap & WaterMarksExist) &&
			!(data->water_marks_bitmap & WaterMarksLoaded)) {
		result = smum_smc_table_manager(hwmgr,
						(uint8_t *)wm_table, TABLE_WATERMARKS, false);
		PP_ASSERT_WITH_CODE(result, "Failed to update WMTABLE!", return -EINVAL);
		data->water_marks_bitmap |= WaterMarksLoaded;
	}

	if ((data->water_marks_bitmap & WaterMarksExist) &&
		data->smu_features[GNLD_DPM_DCEFCLK].supported &&
		data->smu_features[GNLD_DPM_SOCCLK].supported)
		smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, hwmgr->display_config->num_display,
			NULL);

	return result;
}

static int vega12_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_DPM_UVD].supported) {
		PP_ASSERT_WITH_CODE(!vega12_enable_smc_features(hwmgr,
				enable,
				data->smu_features[GNLD_DPM_UVD].smu_feature_bitmap),
				"Attempt to Enable/Disable DPM UVD Failed!",
				return -1);
		data->smu_features[GNLD_DPM_UVD].enabled = enable;
	}

	return 0;
}

static void vega12_power_gate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	if (data->vce_power_gated == bgate)
		return;

	data->vce_power_gated = bgate;
	vega12_enable_disable_vce_dpm(hwmgr, !bgate);
}

static void vega12_power_gate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	if (data->uvd_power_gated == bgate)
		return;

	data->uvd_power_gated = bgate;
	vega12_enable_disable_uvd_dpm(hwmgr, !bgate);
}

static bool
vega12_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	bool is_update_required = false;

	if (data->display_timing.num_existing_displays != hwmgr->display_config->num_display)
		is_update_required = true;

	if (data->registry_data.gfx_clk_deep_sleep_support) {
		if (data->display_timing.min_clock_in_sr != hwmgr->display_config->min_core_set_clock_in_sr)
			is_update_required = true;
	}

	return is_update_required;
}

static int vega12_disable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	tmp_result = vega12_disable_all_smu_features(hwmgr);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to disable all smu features!", result = tmp_result);

	return result;
}

static int vega12_power_off_asic(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	int result;

	result = vega12_disable_dpm_tasks(hwmgr);
	PP_ASSERT_WITH_CODE((0 == result),
			"[disable_dpm_tasks] Failed to disable DPM!",
			);
	data->water_marks_bitmap &= ~(WaterMarksLoaded);

	return result;
}

#if 0
static void vega12_find_min_clock_index(struct pp_hwmgr *hwmgr,
		uint32_t *sclk_idx, uint32_t *mclk_idx,
		uint32_t min_sclk, uint32_t min_mclk)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_dpm_table *dpm_table = &(data->dpm_table);
	uint32_t i;

	for (i = 0; i < dpm_table->gfx_table.count; i++) {
		if (dpm_table->gfx_table.dpm_levels[i].enabled &&
			dpm_table->gfx_table.dpm_levels[i].value >= min_sclk) {
			*sclk_idx = i;
			break;
		}
	}

	for (i = 0; i < dpm_table->mem_table.count; i++) {
		if (dpm_table->mem_table.dpm_levels[i].enabled &&
			dpm_table->mem_table.dpm_levels[i].value >= min_mclk) {
			*mclk_idx = i;
			break;
		}
	}
}
#endif

#if 0
static int vega12_set_power_profile_state(struct pp_hwmgr *hwmgr,
		struct amd_pp_profile *request)
{
	return 0;
}

static int vega12_get_sclk_od(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *sclk_table = &(data->dpm_table.gfx_table);
	struct vega12_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.gfx_table);
	int value = sclk_table->dpm_levels[sclk_table->count - 1].value;
	int golden_value = golden_sclk_table->dpm_levels
			[golden_sclk_table->count - 1].value;

	value -= golden_value;
	value = DIV_ROUND_UP(value * 100, golden_value);

	return value;
}

static int vega12_set_sclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	return 0;
}

static int vega12_get_mclk_od(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *mclk_table = &(data->dpm_table.mem_table);
	struct vega12_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mem_table);
	int value = mclk_table->dpm_levels[mclk_table->count - 1].value;
	int golden_value = golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value;

	value -= golden_value;
	value = DIV_ROUND_UP(value * 100, golden_value);

	return value;
}

static int vega12_set_mclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	return 0;
}
#endif

static int vega12_notify_cac_buffer_info(struct pp_hwmgr *hwmgr,
					uint32_t virtual_addr_low,
					uint32_t virtual_addr_hi,
					uint32_t mc_addr_low,
					uint32_t mc_addr_hi,
					uint32_t size)
{
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSystemVirtualDramAddrHigh,
					virtual_addr_hi,
					NULL);
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSystemVirtualDramAddrLow,
					virtual_addr_low,
					NULL);
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramLogSetDramAddrHigh,
					mc_addr_hi,
					NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramLogSetDramAddrLow,
					mc_addr_low,
					NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramLogSetDramSize,
					size,
					NULL);
	return 0;
}

static int vega12_get_thermal_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *thermal_data)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);

	memcpy(thermal_data, &SMU7ThermalWithDelayPolicy[0], sizeof(struct PP_TemperatureRange));

	thermal_data->max = pp_table->TedgeLimit *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	thermal_data->edge_emergency_max = (pp_table->TedgeLimit + CTF_OFFSET_EDGE) *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	thermal_data->hotspot_crit_max = pp_table->ThotspotLimit *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	thermal_data->hotspot_emergency_max = (pp_table->ThotspotLimit + CTF_OFFSET_HOTSPOT) *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	thermal_data->mem_crit_max = pp_table->ThbmLimit *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	thermal_data->mem_emergency_max = (pp_table->ThbmLimit + CTF_OFFSET_HBM)*
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int vega12_enable_gfx_off(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	int ret = 0;

	if (data->gfxoff_controlled_by_driver)
		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_AllowGfxOff, NULL);

	return ret;
}

static int vega12_disable_gfx_off(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	int ret = 0;

	if (data->gfxoff_controlled_by_driver)
		ret = smum_send_msg_to_smc(hwmgr, PPSMC_MSG_DisallowGfxOff, NULL);

	return ret;
}

static int vega12_gfx_off_control(struct pp_hwmgr *hwmgr, bool enable)
{
	if (enable)
		return vega12_enable_gfx_off(hwmgr);
	else
		return vega12_disable_gfx_off(hwmgr);
}

static int vega12_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level)
{
	return 0;
}

static int vega12_set_mp1_state(struct pp_hwmgr *hwmgr,
				enum pp_mp1_state mp1_state)
{
	uint16_t msg;
	int ret;

	switch (mp1_state) {
	case PP_MP1_STATE_UNLOAD:
		msg = PPSMC_MSG_PrepareMp1ForUnload;
		break;
	case PP_MP1_STATE_SHUTDOWN:
	case PP_MP1_STATE_RESET:
	case PP_MP1_STATE_NONE:
	default:
		return 0;
	}

	PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc(hwmgr, msg, NULL)) == 0,
			    "[PrepareMp1] Failed!",
			    return ret);

	return 0;
}

static void vega12_init_gpu_metrics_v1_0(struct gpu_metrics_v1_0 *gpu_metrics)
{
	memset(gpu_metrics, 0xFF, sizeof(struct gpu_metrics_v1_0));

	gpu_metrics->common_header.structure_size =
				sizeof(struct gpu_metrics_v1_0);
	gpu_metrics->common_header.format_revision = 1;
	gpu_metrics->common_header.content_revision = 0;

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();
}

static ssize_t vega12_get_gpu_metrics(struct pp_hwmgr *hwmgr,
				      void **table)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	struct gpu_metrics_v1_0 *gpu_metrics =
			&data->gpu_metrics_table;
	SmuMetrics_t metrics;
	uint32_t fan_speed_rpm;
	int ret;

	ret = vega12_get_metrics_table(hwmgr, &metrics, true);
	if (ret)
		return ret;

	vega12_init_gpu_metrics_v1_0(gpu_metrics);

	gpu_metrics->temperature_edge = metrics.TemperatureEdge;
	gpu_metrics->temperature_hotspot = metrics.TemperatureHotspot;
	gpu_metrics->temperature_mem = metrics.TemperatureHBM;
	gpu_metrics->temperature_vrgfx = metrics.TemperatureVrGfx;
	gpu_metrics->temperature_vrmem = metrics.TemperatureVrMem;

	gpu_metrics->average_gfx_activity = metrics.AverageGfxActivity;
	gpu_metrics->average_umc_activity = metrics.AverageUclkActivity;

	gpu_metrics->average_gfxclk_frequency = metrics.AverageGfxclkFrequency;
	gpu_metrics->average_socclk_frequency = metrics.AverageSocclkFrequency;
	gpu_metrics->average_uclk_frequency = metrics.AverageUclkFrequency;

	gpu_metrics->current_gfxclk = metrics.CurrClock[PPCLK_GFXCLK];
	gpu_metrics->current_socclk = metrics.CurrClock[PPCLK_SOCCLK];
	gpu_metrics->current_uclk = metrics.CurrClock[PPCLK_UCLK];
	gpu_metrics->current_vclk0 = metrics.CurrClock[PPCLK_VCLK];
	gpu_metrics->current_dclk0 = metrics.CurrClock[PPCLK_DCLK];

	gpu_metrics->throttle_status = metrics.ThrottlerStatus;

	vega12_fan_ctrl_get_fan_speed_rpm(hwmgr, &fan_speed_rpm);
	gpu_metrics->current_fan_speed = (uint16_t)fan_speed_rpm;

	gpu_metrics->pcie_link_width =
			vega12_get_current_pcie_link_width(hwmgr);
	gpu_metrics->pcie_link_speed =
			vega12_get_current_pcie_link_speed(hwmgr);

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v1_0);
}

static const struct pp_hwmgr_func vega12_hwmgr_funcs = {
	.backend_init = vega12_hwmgr_backend_init,
	.backend_fini = vega12_hwmgr_backend_fini,
	.asic_setup = vega12_setup_asic_task,
	.dynamic_state_management_enable = vega12_enable_dpm_tasks,
	.dynamic_state_management_disable = vega12_disable_dpm_tasks,
	.patch_boot_state = vega12_patch_boot_state,
	.get_sclk = vega12_dpm_get_sclk,
	.get_mclk = vega12_dpm_get_mclk,
	.notify_smc_display_config_after_ps_adjustment =
			vega12_notify_smc_display_config_after_ps_adjustment,
	.force_dpm_level = vega12_dpm_force_dpm_level,
	.stop_thermal_controller = vega12_thermal_stop_thermal_controller,
	.get_fan_speed_info = vega12_fan_ctrl_get_fan_speed_info,
	.reset_fan_speed_to_default =
			vega12_fan_ctrl_reset_fan_speed_to_default,
	.get_fan_speed_rpm = vega12_fan_ctrl_get_fan_speed_rpm,
	.set_fan_control_mode = vega12_set_fan_control_mode,
	.get_fan_control_mode = vega12_get_fan_control_mode,
	.read_sensor = vega12_read_sensor,
	.get_dal_power_level = vega12_get_dal_power_level,
	.get_clock_by_type_with_latency = vega12_get_clock_by_type_with_latency,
	.get_clock_by_type_with_voltage = vega12_get_clock_by_type_with_voltage,
	.set_watermarks_for_clocks_ranges = vega12_set_watermarks_for_clocks_ranges,
	.display_clock_voltage_request = vega12_display_clock_voltage_request,
	.force_clock_level = vega12_force_clock_level,
	.print_clock_levels = vega12_print_clock_levels,
	.apply_clocks_adjust_rules =
		vega12_apply_clocks_adjust_rules,
	.pre_display_config_changed =
		vega12_pre_display_configuration_changed_task,
	.display_config_changed = vega12_display_configuration_changed_task,
	.powergate_uvd = vega12_power_gate_uvd,
	.powergate_vce = vega12_power_gate_vce,
	.check_smc_update_required_for_display_configuration =
			vega12_check_smc_update_required_for_display_configuration,
	.power_off_asic = vega12_power_off_asic,
	.disable_smc_firmware_ctf = vega12_thermal_disable_alert,
#if 0
	.set_power_profile_state = vega12_set_power_profile_state,
	.get_sclk_od = vega12_get_sclk_od,
	.set_sclk_od = vega12_set_sclk_od,
	.get_mclk_od = vega12_get_mclk_od,
	.set_mclk_od = vega12_set_mclk_od,
#endif
	.notify_cac_buffer_info = vega12_notify_cac_buffer_info,
	.get_thermal_temperature_range = vega12_get_thermal_temperature_range,
	.register_irq_handlers = smu9_register_irq_handlers,
	.start_thermal_controller = vega12_start_thermal_controller,
	.powergate_gfx = vega12_gfx_off_control,
	.get_performance_level = vega12_get_performance_level,
	.get_asic_baco_capability = smu9_baco_get_capability,
	.get_asic_baco_state = smu9_baco_get_state,
	.set_asic_baco_state = vega12_baco_set_state,
	.get_ppfeature_status = vega12_get_ppfeature_status,
	.set_ppfeature_status = vega12_set_ppfeature_status,
	.set_mp1_state = vega12_set_mp1_state,
	.get_gpu_metrics = vega12_get_gpu_metrics,
};

int vega12_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &vega12_hwmgr_funcs;
	hwmgr->pptable_func = &vega12_pptable_funcs;

	return 0;
}
