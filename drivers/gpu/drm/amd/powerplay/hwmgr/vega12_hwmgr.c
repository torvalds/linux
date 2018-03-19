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
#include "vega12_powertune.h"
#include "vega12_inc.h"
#include "pp_soc15.h"
#include "pppcielanes.h"
#include "vega12_hwmgr.h"
#include "vega12_processpptables.h"
#include "vega12_pptable.h"
#include "vega12_thermal.h"
#include "vega12_ppsmc.h"
#include "pp_debug.h"
#include "amd_pcie_helpers.h"
#include "cgs_linux.h"
#include "ppinterrupt.h"
#include "pp_overdriver.h"
#include "pp_thermal.h"

static const ULONG PhwVega12_Magic = (ULONG)(PHM_VIslands_Magic);

static int vega12_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask);
static int vega12_get_clock_ranges(struct pp_hwmgr *hwmgr,
		uint32_t *clock,
		PPCLK_e clock_select,
		bool max);

struct vega12_power_state *cast_phw_vega12_power_state(
				  struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwVega12_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (struct vega12_power_state *)hw_ps;
}

const struct vega12_power_state *cast_const_phw_vega12_power_state(
				 const struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwVega12_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (const struct vega12_power_state *)hw_ps;
}

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
	dpm_state->soft_min_level = 0xff;
	dpm_state->soft_max_level = 0xff;
	dpm_state->hard_min_level = 0xff;
	dpm_state->hard_max_level = 0xff;
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

	memset(&data->dpm_table, 0, sizeof(data->dpm_table));

	/* Initialize Sclk DPM table based on allow Sclk values */
	dpm_table = &(data->dpm_table.soc_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.gfx_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* Initialize Mclk DPM table based on allow Mclk values */
	dpm_table = &(data->dpm_table.mem_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.eclk_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.vclk_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.dclk_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	/* Assume there is no headless Vega12 for now */
	dpm_table = &(data->dpm_table.dcef_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.pixel_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.display_table);
	vega12_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.phy_table);
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
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput  the pointer to input data (PowerState)
* @return   always 0
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

	result = vega12_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to setup default DPM tables!",
			return result);

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
		if (0 != boot_up_values.usVddc) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetFloorSocVoltage,
						(boot_up_values.usVddc * 4));
			data->vbios_boot_state.bsoc_vddc_lock = true;
		} else {
			data->vbios_boot_state.bsoc_vddc_lock = false;
		}
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetMinDeepSleepDcefclk,
			(uint32_t)(data->vbios_boot_state.dcef_clock / 100));
	}

	memcpy(pp_table, pptable_information->smc_pptable, sizeof(PPTable_t));

	result = vega12_copy_table_to_smc(hwmgr,
			(uint8_t *)pp_table, TABLE_PPTABLE);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload PPtable!", return result);

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
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetAllowedFeaturesMaskHigh, allowed_features_high) == 0,
		"[SetAllowedFeaturesMask] Attempt to set allowed features mask (high) failed!",
		return -1);

	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetAllowedFeaturesMaskLow, allowed_features_low) == 0,
		"[SetAllowedFeaturesMask] Attempt to set allowed features mask (low) failed!",
		return -1);

	return 0;
}

static int vega12_enable_all_smu_features(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint64_t features_enabled;
	int i;
	bool enabled;

	PP_ASSERT_WITH_CODE(
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_EnableAllSmuFeatures) == 0,
		"[EnableAllSMUFeatures] Failed to enable all smu features!",
		return -1);

	if (vega12_get_enabled_smc_features(hwmgr, &features_enabled) == 0) {
		for (i = 0; i < GNLD_FEATURES_MAX; i++) {
			enabled = (features_enabled & data->smu_features[i].smu_feature_bitmap) ? true : false;
			data->smu_features[i].enabled = enabled;
			data->smu_features[i].supported = enabled;
			PP_ASSERT(
				!data->smu_features[i].allowed || enabled,
				"[EnableAllSMUFeatures] Enabled feature is different from allowed, expected disabled!");
		}
	}

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
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_DisableAllSmuFeatures) == 0,
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

static int vega12_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, 0);

	result = vega12_set_allowed_featuresmask(hwmgr);
	PP_ASSERT_WITH_CODE(result == 0,
			"[EnableDPMTasks] Failed to set allowed featuresmask!\n",
			return result);

	tmp_result = vega12_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to initialize SMC table!",
			result = tmp_result);

	result = vega12_enable_all_smu_features(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to enable all smu features!",
			return result);

	tmp_result = vega12_power_control_set_level(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to power control set level!",
			result = tmp_result);

	result = vega12_odn_initialize_default_settings(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to power control set level!",
			return result);

	return result;
}

static int vega12_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct vega12_power_state);
}

static int vega12_get_number_of_pp_table_entries(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int vega12_patch_boot_state(struct pp_hwmgr *hwmgr,
	     struct pp_hw_power_state *hw_ps)
{
	return 0;
}

static int vega12_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *request_ps,
			const struct pp_power_state *current_ps)
{
	struct vega12_power_state *vega12_ps =
				cast_phw_vega12_power_state(&request_ps->hardware);
	uint32_t sclk;
	uint32_t mclk;
	struct PP_Clocks minimum_clocks = {0};
	bool disable_mclk_switching;
	bool disable_mclk_switching_for_frame_lock;
	bool disable_mclk_switching_for_vr;
	bool force_mclk_high;
	struct cgs_display_info info = {0};
	const struct phm_clock_and_voltage_limits *max_limits;
	uint32_t i;
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	int32_t count;
	uint32_t stable_pstate_sclk_dpm_percentage;
	uint32_t stable_pstate_sclk = 0, stable_pstate_mclk = 0;
	uint32_t latency;

	data->battery_state = (PP_StateUILabel_Battery ==
			request_ps->classification.ui_label);

	if (vega12_ps->performance_level_count != 2)
		pr_info("VI should always have 2 performance levels");

	max_limits = (PP_PowerSource_AC == hwmgr->power_source) ?
			&(hwmgr->dyn_state.max_clock_voltage_on_ac) :
			&(hwmgr->dyn_state.max_clock_voltage_on_dc);

	/* Cap clock DPM tables at DC MAX if it is in DC. */
	if (PP_PowerSource_DC == hwmgr->power_source) {
		for (i = 0; i < vega12_ps->performance_level_count; i++) {
			if (vega12_ps->performance_levels[i].mem_clock >
				max_limits->mclk)
				vega12_ps->performance_levels[i].mem_clock =
						max_limits->mclk;
			if (vega12_ps->performance_levels[i].gfx_clock >
				max_limits->sclk)
				vega12_ps->performance_levels[i].gfx_clock =
						max_limits->sclk;
		}
	}

	cgs_get_active_displays_info(hwmgr->device, &info);

	/* result = PHM_CheckVBlankTime(hwmgr, &vblankTooShort);*/
	minimum_clocks.engineClock = hwmgr->display_config.min_core_set_clock;
	minimum_clocks.memoryClock = hwmgr->display_config.min_mem_set_clock;

	if (PP_CAP(PHM_PlatformCaps_StablePState)) {
		PP_ASSERT_WITH_CODE(
			data->registry_data.stable_pstate_sclk_dpm_percentage >= 1 &&
			data->registry_data.stable_pstate_sclk_dpm_percentage <= 100,
			"percent sclk value must range from 1% to 100%, setting default value",
			stable_pstate_sclk_dpm_percentage = 75);

		max_limits = &(hwmgr->dyn_state.max_clock_voltage_on_ac);
		stable_pstate_sclk = (max_limits->sclk *
				stable_pstate_sclk_dpm_percentage) / 100;

		for (count = table_info->vdd_dep_on_sclk->count - 1;
				count >= 0; count--) {
			if (stable_pstate_sclk >=
					table_info->vdd_dep_on_sclk->entries[count].clk) {
				stable_pstate_sclk =
						table_info->vdd_dep_on_sclk->entries[count].clk;
				break;
			}
		}

		if (count < 0)
			stable_pstate_sclk = table_info->vdd_dep_on_sclk->entries[0].clk;

		stable_pstate_mclk = max_limits->mclk;

		minimum_clocks.engineClock = stable_pstate_sclk;
		minimum_clocks.memoryClock = stable_pstate_mclk;
	}

	disable_mclk_switching_for_frame_lock = phm_cap_enabled(
				    hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_DisableMclkSwitchingForFrameLock);
	disable_mclk_switching_for_vr = PP_CAP(PHM_PlatformCaps_DisableMclkSwitchForVR);
	force_mclk_high = PP_CAP(PHM_PlatformCaps_ForceMclkHigh);

	if (info.display_count == 0)
		disable_mclk_switching = false;
	else
		disable_mclk_switching = (info.display_count > 1) ||
			disable_mclk_switching_for_frame_lock ||
			disable_mclk_switching_for_vr ||
			force_mclk_high;

	sclk = vega12_ps->performance_levels[0].gfx_clock;
	mclk = vega12_ps->performance_levels[0].mem_clock;

	if (sclk < minimum_clocks.engineClock)
		sclk = (minimum_clocks.engineClock > max_limits->sclk) ?
				max_limits->sclk : minimum_clocks.engineClock;

	if (mclk < minimum_clocks.memoryClock)
		mclk = (minimum_clocks.memoryClock > max_limits->mclk) ?
				max_limits->mclk : minimum_clocks.memoryClock;

	vega12_ps->performance_levels[0].gfx_clock = sclk;
	vega12_ps->performance_levels[0].mem_clock = mclk;

	if (vega12_ps->performance_levels[1].gfx_clock <
			vega12_ps->performance_levels[0].gfx_clock)
		vega12_ps->performance_levels[0].gfx_clock =
				vega12_ps->performance_levels[1].gfx_clock;

	if (disable_mclk_switching) {
		/* Set Mclk the max of level 0 and level 1 */
		if (mclk < vega12_ps->performance_levels[1].mem_clock)
			mclk = vega12_ps->performance_levels[1].mem_clock;
		/* Find the lowest MCLK frequency that is within
		 * the tolerable latency defined in DAL
		 */
		latency = 0;
		for (i = 0; i < data->mclk_latency_table.count; i++) {
			if ((data->mclk_latency_table.entries[i].latency <= latency) &&
				(data->mclk_latency_table.entries[i].frequency >=
						vega12_ps->performance_levels[0].mem_clock) &&
				(data->mclk_latency_table.entries[i].frequency <=
						vega12_ps->performance_levels[1].mem_clock))
				mclk = data->mclk_latency_table.entries[i].frequency;
		}
		vega12_ps->performance_levels[0].mem_clock = mclk;
	} else {
		if (vega12_ps->performance_levels[1].mem_clock <
				vega12_ps->performance_levels[0].mem_clock)
			vega12_ps->performance_levels[0].mem_clock =
					vega12_ps->performance_levels[1].mem_clock;
	}

	if (PP_CAP(PHM_PlatformCaps_StablePState)) {
		for (i = 0; i < vega12_ps->performance_level_count; i++) {
			vega12_ps->performance_levels[i].gfx_clock = stable_pstate_sclk;
			vega12_ps->performance_levels[i].mem_clock = stable_pstate_mclk;
		}
	}

	return 0;
}

static int vega12_find_dpm_states_clocks_in_dpm_table(struct pp_hwmgr *hwmgr, const void *input)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct PP_Clocks min_clocks = {0};
	struct cgs_display_info info = {0};

	data->need_update_dpm_table = 0;

	min_clocks.engineClockInSR = hwmgr->display_config.min_core_set_clock_in_sr;
	if (data->display_timing.min_clock_in_sr != min_clocks.engineClockInSR &&
			(min_clocks.engineClockInSR >= VEGA12_MINIMUM_ENGINE_CLOCK ||
			 data->display_timing.min_clock_in_sr >= VEGA12_MINIMUM_ENGINE_CLOCK))
		data->need_update_dpm_table |= DPMTABLE_UPDATE_SCLK;

	cgs_get_active_displays_info(hwmgr->device, &info);
	if (data->display_timing.num_existing_displays != info.display_count)
		data->need_update_dpm_table |= DPMTABLE_UPDATE_MCLK;

	return 0;
}

static int vega12_trim_single_dpm_states(struct pp_hwmgr *hwmgr,
		struct vega12_single_dpm_table *dpm_table,
		uint32_t low_limit, uint32_t high_limit)
{
	uint32_t i;

	for (i = 0; i < dpm_table->count; i++) {
		if ((dpm_table->dpm_levels[i].value < low_limit) ||
		    (dpm_table->dpm_levels[i].value > high_limit))
			dpm_table->dpm_levels[i].enabled = false;
		else
			dpm_table->dpm_levels[i].enabled = true;
	}
	return 0;
}

static int vega12_trim_single_dpm_states_with_mask(struct pp_hwmgr *hwmgr,
		struct vega12_single_dpm_table *dpm_table,
		uint32_t low_limit, uint32_t high_limit,
		uint32_t disable_dpm_mask)
{
	uint32_t i;

	for (i = 0; i < dpm_table->count; i++) {
		if ((dpm_table->dpm_levels[i].value < low_limit) ||
		    (dpm_table->dpm_levels[i].value > high_limit))
			dpm_table->dpm_levels[i].enabled = false;
		else if ((!((1 << i) & disable_dpm_mask)) &&
				!(low_limit == high_limit))
			dpm_table->dpm_levels[i].enabled = false;
		else
			dpm_table->dpm_levels[i].enabled = true;
	}
	return 0;
}

static int vega12_trim_dpm_states(struct pp_hwmgr *hwmgr,
		const struct vega12_power_state *vega12_ps)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t high_limit_count;

	PP_ASSERT_WITH_CODE((vega12_ps->performance_level_count >= 1),
			"power state did not have any performance level",
			return -1);

	high_limit_count = (vega12_ps->performance_level_count == 1) ? 0 : 1;

	vega12_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.soc_table),
			vega12_ps->performance_levels[0].soc_clock,
			vega12_ps->performance_levels[high_limit_count].soc_clock);

	vega12_trim_single_dpm_states_with_mask(hwmgr,
			&(data->dpm_table.gfx_table),
			vega12_ps->performance_levels[0].gfx_clock,
			vega12_ps->performance_levels[high_limit_count].gfx_clock,
			data->disable_dpm_mask);

	vega12_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.mem_table),
			vega12_ps->performance_levels[0].mem_clock,
			vega12_ps->performance_levels[high_limit_count].mem_clock);

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

	return i;
}

static uint32_t vega12_find_highest_dpm_level(
		struct vega12_single_dpm_table *table)
{
	uint32_t i = 0;

	if (table->count <= MAX_REGULAR_DPM_NUMBER) {
		for (i = table->count; i > 0; i--) {
			if (table->dpm_levels[i - 1].enabled)
				return i - 1;
		}
	} else {
		pr_info("DPM Table Has Too Many Entries!");
		return MAX_REGULAR_DPM_NUMBER - 1;
	}

	return i;
}

static int vega12_upload_dpm_min_level(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int vega12_upload_dpm_max_level(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int vega12_generate_dpm_level_enable_mask(
		struct pp_hwmgr *hwmgr, const void *input)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct vega12_power_state *vega12_ps =
			cast_const_phw_vega12_power_state(states->pnew_state);
	int i;

	PP_ASSERT_WITH_CODE(!vega12_trim_dpm_states(hwmgr, vega12_ps),
			"Attempt to Trim DPM States Failed!",
			return -1);

	data->smc_state_table.gfx_boot_level =
			vega12_find_lowest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.gfx_max_level =
			vega12_find_highest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
			vega12_find_lowest_dpm_level(&(data->dpm_table.mem_table));
	data->smc_state_table.mem_max_level =
			vega12_find_highest_dpm_level(&(data->dpm_table.mem_table));

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_min_level(hwmgr),
			"Attempt to upload DPM Bootup Levels Failed!",
			return -1);
	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_max_level(hwmgr),
			"Attempt to upload DPM Max Levels Failed!",
			return -1);
	for (i = data->smc_state_table.gfx_boot_level; i < data->smc_state_table.gfx_max_level; i++)
		data->dpm_table.gfx_table.dpm_levels[i].enabled = true;


	for (i = data->smc_state_table.mem_boot_level; i < data->smc_state_table.mem_max_level; i++)
		data->dpm_table.mem_table.dpm_levels[i].enabled = true;

	return 0;
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

static int vega12_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int vega12_set_power_state_tasks(struct pp_hwmgr *hwmgr,
		const void *input)
{
	int tmp_result, result = 0;
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);

	tmp_result = vega12_find_dpm_states_clocks_in_dpm_table(hwmgr, input);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to find DPM states clocks in DPM table!",
			result = tmp_result);

	tmp_result = vega12_generate_dpm_level_enable_mask(hwmgr, input);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to generate DPM level enabled mask!",
			result = tmp_result);

	tmp_result = vega12_update_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to update SCLK threshold!",
			result = tmp_result);

	result = vega12_copy_table_to_smc(hwmgr,
			(uint8_t *)pp_table, TABLE_PPTABLE);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload PPtable!", return result);

	data->apply_optimized_settings = false;
	data->apply_overdrive_next_settings_mask = 0;

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

static int vega12_get_gpu_power(struct pp_hwmgr *hwmgr,
		struct pp_gpu_power *query)
{
#if 0
	uint32_t value;

	PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetCurrPkgPwr),
			"Failed to get current package power!",
			return -EINVAL);

	vega12_read_arg_from_smc(hwmgr, &value);
	/* power value is an integer */
	query->average_gpu_power = value << 8;
#endif
	return 0;
}

static int vega12_get_current_gfx_clk_freq(struct pp_hwmgr *hwmgr, uint32_t *gfx_freq)
{
	uint32_t gfx_clk = 0;

	*gfx_freq = 0;

	PP_ASSERT_WITH_CODE(
			smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetDpmClockFreq, (PPCLK_GFXCLK << 16)) == 0,
			"[GetCurrentGfxClkFreq] Attempt to get Current GFXCLK Frequency Failed!",
			return -1);
	PP_ASSERT_WITH_CODE(
			vega12_read_arg_from_smc(hwmgr, &gfx_clk) == 0,
			"[GetCurrentGfxClkFreq] Attempt to read arg from SMC Failed",
			return -1);

	*gfx_freq = gfx_clk * 100;

	return 0;
}

static int vega12_get_current_mclk_freq(struct pp_hwmgr *hwmgr, uint32_t *mclk_freq)
{
	uint32_t mem_clk = 0;

	*mclk_freq = 0;

	PP_ASSERT_WITH_CODE(
			smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetDpmClockFreq, (PPCLK_UCLK << 16)) == 0,
			"[GetCurrentMClkFreq] Attempt to get Current MCLK Frequency Failed!",
			return -1);
	PP_ASSERT_WITH_CODE(
			vega12_read_arg_from_smc(hwmgr, &mem_clk) == 0,
			"[GetCurrentMClkFreq] Attempt to read arg from SMC Failed",
			return -1);

	*mclk_freq = mem_clk * 100;

	return 0;
}

static int vega12_get_current_activity_percent(
		struct pp_hwmgr *hwmgr,
		uint32_t *activity_percent)
{
	int ret = 0;
	uint32_t current_activity = 50;

#if 0
	ret = smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetAverageGfxActivity, 0);
	if (!ret) {
		ret = vega12_read_arg_from_smc(hwmgr, &current_activity);
		if (!ret) {
			if (current_activity > 100) {
				PP_ASSERT(false,
					"[GetCurrentActivityPercent] Activity Percentage Exceeds 100!");
				current_activity = 100;
			}
		} else
			PP_ASSERT(false,
				"[GetCurrentActivityPercent] Attempt To Read Average Graphics Activity from SMU Failed!");
	} else
		PP_ASSERT(false,
			"[GetCurrentActivityPercent] Attempt To Send Get Average Graphics Activity to SMU Failed!");
#endif
	*activity_percent = current_activity;

	return ret;
}

static int vega12_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			      void *value, int *size)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
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
		ret = vega12_get_current_activity_percent(hwmgr, (uint32_t *)value);
		if (!ret)
			*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = vega12_thermal_get_temperature(hwmgr);
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
		if (*size < sizeof(struct pp_gpu_power))
			ret = -EINVAL;
		else {
			*size = sizeof(struct pp_gpu_power);
			ret = vega12_get_gpu_power(hwmgr, (struct pp_gpu_power *)value);
		}
		break;
	default:
		ret = -EINVAL;
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
			has_disp ? 0 : 1);

	return 0;
}

int vega12_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
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
			clk_freq = clock_req->clock_freq_in_khz / 100;
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
					clk_request);
		}
	}

	return result;
}

static int vega12_notify_smc_display_config_after_ps_adjustment(
		struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t num_active_disps = 0;
	struct cgs_display_info info = {0};
	struct PP_Clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;
	uint32_t clk_request;

	info.mode_info = NULL;
	cgs_get_active_displays_info(hwmgr->device, &info);
	num_active_disps = info.display_count;
	if (num_active_disps > 1)
		vega12_notify_smc_display_change(hwmgr, false);
	else
		vega12_notify_smc_display_change(hwmgr, true);

	min_clocks.dcefClock = hwmgr->display_config.min_dcef_set_clk;
	min_clocks.dcefClockInSR = hwmgr->display_config.min_dcef_deep_sleep_set_clk;
	min_clocks.memoryClock = hwmgr->display_config.min_mem_set_clock;

	if (data->smu_features[GNLD_DPM_DCEFCLK].supported) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcefClock;
		if (!vega12_display_clock_voltage_request(hwmgr, &clock_req)) {
			if (data->smu_features[GNLD_DS_DCEFCLK].supported)
				PP_ASSERT_WITH_CODE(
					!smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetMinDeepSleepDcefclk,
					min_clocks.dcefClockInSR /100),
					"Attempt to set divider for DCEFCLK Failed!",
					return -1);
		} else {
			pr_info("Attempt to set Hard Min for DCEFCLK Failed!");
		}
	}

	if (data->smu_features[GNLD_DPM_UCLK].enabled) {
		clk_request = (PPCLK_UCLK << 16) | (min_clocks.memoryClock) / 100;
		PP_ASSERT_WITH_CODE(
			smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetHardMinByFreq, clk_request) == 0,
			"[PhwVega12_NotifySMCDisplayConfigAfterPowerStateAdjustment] Attempt to set UCLK HardMin Failed!",
			return -1);
		data->dpm_table.mem_table.dpm_state.hard_min_level = min_clocks.memoryClock;
	}

	return 0;
}

static int vega12_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data =
			(struct vega12_hwmgr *)(hwmgr->backend);

	data->smc_state_table.gfx_boot_level =
	data->smc_state_table.gfx_max_level =
			vega12_find_highest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
	data->smc_state_table.mem_max_level =
			vega12_find_highest_dpm_level(&(data->dpm_table.mem_table));

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

	data->smc_state_table.gfx_boot_level =
	data->smc_state_table.gfx_max_level =
			vega12_find_lowest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
	data->smc_state_table.mem_max_level =
			vega12_find_lowest_dpm_level(&(data->dpm_table.mem_table));

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
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	data->smc_state_table.gfx_boot_level =
			vega12_find_lowest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.gfx_max_level =
			vega12_find_highest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
			vega12_find_lowest_dpm_level(&(data->dpm_table.mem_table));
	data->smc_state_table.mem_max_level =
			vega12_find_highest_dpm_level(&(data->dpm_table.mem_table));

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_min_level(hwmgr),
			"Failed to upload DPM Bootup Levels!",
			return -1);

	PP_ASSERT_WITH_CODE(!vega12_upload_dpm_max_level(hwmgr),
			"Failed to upload DPM Max Levels!",
			return -1);
	return 0;
}

#if 0
static int vega12_get_profiling_clk_mask(struct pp_hwmgr *hwmgr, enum amd_dpm_forced_level level,
				uint32_t *sclk_mask, uint32_t *mclk_mask, uint32_t *soc_mask)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);

	if (table_info->vdd_dep_on_sclk->count > VEGA12_UMD_PSTATE_GFXCLK_LEVEL &&
		table_info->vdd_dep_on_socclk->count > VEGA12_UMD_PSTATE_SOCCLK_LEVEL &&
		table_info->vdd_dep_on_mclk->count > VEGA12_UMD_PSTATE_MCLK_LEVEL) {
		*sclk_mask = VEGA12_UMD_PSTATE_GFXCLK_LEVEL;
		*soc_mask = VEGA12_UMD_PSTATE_SOCCLK_LEVEL;
		*mclk_mask = VEGA12_UMD_PSTATE_MCLK_LEVEL;
	}

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
		*sclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		*mclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		*sclk_mask = table_info->vdd_dep_on_sclk->count - 1;
		*soc_mask = table_info->vdd_dep_on_socclk->count - 1;
		*mclk_mask = table_info->vdd_dep_on_mclk->count - 1;
	}
	return 0;
}
#endif

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
#if 0
	uint32_t sclk_mask = 0;
	uint32_t mclk_mask = 0;
	uint32_t soc_mask = 0;
#endif

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
#if 0
		ret = vega12_get_profiling_clk_mask(hwmgr, level, &sclk_mask, &mclk_mask, &soc_mask);
		if (ret)
			return ret;
		vega12_force_clock_level(hwmgr, PP_SCLK, 1<<sclk_mask);
		vega12_force_clock_level(hwmgr, PP_MCLK, 1<<mclk_mask);
#endif
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}
#if 0
	if (!ret) {
		if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK && hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_PROFILE_PEAK)
			vega12_set_fan_control_mode(hwmgr, AMD_FAN_CTRL_NONE);
		else if (level != AMD_DPM_FORCED_LEVEL_PROFILE_PEAK && hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK)
			vega12_set_fan_control_mode(hwmgr, AMD_FAN_CTRL_AUTO);
	}
#endif
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
	int result;
	*clock = 0;

	if (max) {
		 PP_ASSERT_WITH_CODE(
			smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetMaxDpmFreq, (clock_select << 16)) == 0,
			"[GetClockRanges] Failed to get max clock from SMC!",
			return -1);
		result = vega12_read_arg_from_smc(hwmgr, clock);
	} else {
		PP_ASSERT_WITH_CODE(
			smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetMinDpmFreq, (clock_select << 16)) == 0,
			"[GetClockRanges] Failed to get min clock from SMC!",
			return -1);
		result = vega12_read_arg_from_smc(hwmgr, clock);
	}

	return result;
}

static int vega12_get_sclks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	int i;
	uint32_t min, max, increments;

	if (!data->smu_features[GNLD_DPM_GFXCLK].enabled)
		return -1;

	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &min, PPCLK_GFXCLK, false) == 0,
		"[GetSclks]: fail to get min PPCLK_GFXCLK\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &max, PPCLK_GFXCLK, true) == 0,
		"[GetSclks]: fail to get max PPCLK_GFXCLK\n",
		return -1);

	clocks->data[0].clocks_in_khz = min * 100;
	increments = (max - min) / (VG12_PSUEDO_NUM_GFXCLK_DPM_LEVELS - 1);

	for (i = 1; i < (VG12_PSUEDO_NUM_GFXCLK_DPM_LEVELS - 1); i++) {
		if ((min + (increments * i)) != 0) {
			clocks->data[i].clocks_in_khz =
				(min + increments * i) * 100;
			clocks->data[i].latency_in_us = 0;
		}
	}
	clocks->data[i].clocks_in_khz = max * 100;
	clocks->num_levels = i + 1;

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
	uint32_t min, max, increments;
	int i;

	if (!data->smu_features[GNLD_DPM_UCLK].enabled)
		return -1;

	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &min, PPCLK_UCLK, false) == 0,
		"[GetMclks]: fail to get min PPCLK_UCLK\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &max, PPCLK_UCLK, true) == 0,
		"[GetMclks]: fail to get max PPCLK_UCLK\n",
		return -1);

	clocks->data[0].clocks_in_khz = min * 100;
	clocks->data[0].latency_in_us =
		data->mclk_latency_table.entries[0].latency =
		vega12_get_mem_latency(hwmgr, min);

	increments = (max - min) / (VG12_PSUEDO_NUM_UCLK_DPM_LEVELS - 1);

	for (i = 1; i < (VG12_PSUEDO_NUM_UCLK_DPM_LEVELS - 1); i++) {
		if ((min + (increments * i)) != 0) {
			clocks->data[i].clocks_in_khz =
				(min + (increments * i)) * 100;
			clocks->data[i].latency_in_us =
				data->mclk_latency_table.entries[i].latency =
				vega12_get_mem_latency(hwmgr, min + increments * i);
		}
	}

	clocks->data[i].clocks_in_khz = max * 100;
	clocks->data[i].latency_in_us =
		data->mclk_latency_table.entries[i].latency =
		vega12_get_mem_latency(hwmgr, max);

	clocks->num_levels = data->mclk_latency_table.count = i + 1;

	return 0;
}

static int vega12_get_dcefclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	int i;
	uint32_t min, max, increments;

	if (!data->smu_features[GNLD_DPM_DCEFCLK].enabled)
		return -1;

	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &min, PPCLK_DCEFCLK, false) == 0,
		"[GetDcfclocks]: fail to get min PPCLK_DCEFCLK\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &max, PPCLK_DCEFCLK, true) == 0,
		"[GetDcfclocks]: fail to get max PPCLK_DCEFCLK\n",
		return -1);

	clocks->data[0].clocks_in_khz = min * 100;
	increments = (max - min) / (VG12_PSUEDO_NUM_DCEFCLK_DPM_LEVELS - 1);

	for (i = 1; i < (VG12_PSUEDO_NUM_DCEFCLK_DPM_LEVELS - 1); i++) {
		if ((min + (increments * i)) != 0) {
			clocks->data[i].clocks_in_khz =
				(min + increments * i) * 100;
			clocks->data[i].latency_in_us = 0;
		}
	}
	clocks->data[i].clocks_in_khz = max * 100;
	clocks->num_levels = i + 1;

	return 0;
}

static int vega12_get_socclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	int i;
	uint32_t min, max, increments;

	if (!data->smu_features[GNLD_DPM_SOCCLK].enabled)
		return -1;

	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &min, PPCLK_SOCCLK, false) == 0,
		"[GetSocclks]: fail to get min PPCLK_SOCCLK\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		vega12_get_clock_ranges(hwmgr, &max, PPCLK_SOCCLK, true) == 0,
		"[GetSocclks]: fail to get max PPCLK_SOCCLK\n",
		return -1);

	clocks->data[0].clocks_in_khz = min * 100;
	increments = (max - min) / (VG12_PSUEDO_NUM_SOCCLK_DPM_LEVELS - 1);

	for (i = 1; i < (VG12_PSUEDO_NUM_SOCCLK_DPM_LEVELS - 1); i++) {
		if ((min + (increments * i)) != 0) {
			clocks->data[i].clocks_in_khz =
				(min + increments * i) * 100;
			clocks->data[i].latency_in_us = 0;
		}
	}

	clocks->data[i].clocks_in_khz = max * 100;
	clocks->num_levels = i + 1;

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
		struct pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	Watermarks_t *table = &(data->smc_state_table.water_marks_table);
	int result = 0;
	uint32_t i;

	if (!data->registry_data.disable_water_mark &&
			data->smu_features[GNLD_DPM_DCEFCLK].supported &&
			data->smu_features[GNLD_DPM_SOCCLK].supported) {
		for (i = 0; i < wm_with_clock_ranges->num_wm_sets_dmif; i++) {
			table->WatermarkRow[WM_DCEFCLK][i].MinClock =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_dmif[i].wm_min_dcefclk_in_khz) /
				100);
			table->WatermarkRow[WM_DCEFCLK][i].MaxClock =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_dmif[i].wm_max_dcefclk_in_khz) /
				100);
			table->WatermarkRow[WM_DCEFCLK][i].MinUclk =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_dmif[i].wm_min_memclk_in_khz) /
				100);
			table->WatermarkRow[WM_DCEFCLK][i].MaxUclk =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_dmif[i].wm_max_memclk_in_khz) /
				100);
			table->WatermarkRow[WM_DCEFCLK][i].WmSetting = (uint8_t)
					wm_with_clock_ranges->wm_sets_dmif[i].wm_set_id;
		}

		for (i = 0; i < wm_with_clock_ranges->num_wm_sets_mcif; i++) {
			table->WatermarkRow[WM_SOCCLK][i].MinClock =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_mcif[i].wm_min_socclk_in_khz) /
				100);
			table->WatermarkRow[WM_SOCCLK][i].MaxClock =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_mcif[i].wm_max_socclk_in_khz) /
				100);
			table->WatermarkRow[WM_SOCCLK][i].MinUclk =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_mcif[i].wm_min_memclk_in_khz) /
				100);
			table->WatermarkRow[WM_SOCCLK][i].MaxUclk =
				cpu_to_le16((uint16_t)
				(wm_with_clock_ranges->wm_sets_mcif[i].wm_max_memclk_in_khz) /
				100);
			table->WatermarkRow[WM_SOCCLK][i].WmSetting = (uint8_t)
					wm_with_clock_ranges->wm_sets_mcif[i].wm_set_id;
		}
		data->water_marks_bitmap |= WaterMarksExist;
		data->water_marks_bitmap &= ~WaterMarksLoaded;
	}

	return result;
}

static int vega12_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	if (hwmgr->request_dpm_level & (AMD_DPM_FORCED_LEVEL_AUTO |
				AMD_DPM_FORCED_LEVEL_LOW |
				AMD_DPM_FORCED_LEVEL_HIGH))
		return -EINVAL;

	switch (type) {
	case PP_SCLK:
		data->smc_state_table.gfx_boot_level = mask ? (ffs(mask) - 1) : 0;
		data->smc_state_table.gfx_max_level = mask ? (fls(mask) - 1) : 0;

		PP_ASSERT_WITH_CODE(!vega12_upload_dpm_min_level(hwmgr),
			"Failed to upload boot level to lowest!",
			return -EINVAL);

		PP_ASSERT_WITH_CODE(!vega12_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -EINVAL);
		break;

	case PP_MCLK:
		data->smc_state_table.mem_boot_level = mask ? (ffs(mask) - 1) : 0;
		data->smc_state_table.mem_max_level = mask ? (fls(mask) - 1) : 0;

		PP_ASSERT_WITH_CODE(!vega12_upload_dpm_min_level(hwmgr),
			"Failed to upload boot level to lowest!",
			return -EINVAL);

		PP_ASSERT_WITH_CODE(!vega12_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -EINVAL);

		break;

	case PP_PCIE:
		break;

	default:
		break;
	}

	return 0;
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
				i, clocks.data[i].clocks_in_khz / 100,
				(clocks.data[i].clocks_in_khz == now) ? "*" : "");
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
				i, clocks.data[i].clocks_in_khz / 100,
				(clocks.data[i].clocks_in_khz == now) ? "*" : "");
		break;

	case PP_PCIE:
		break;

	default:
		break;
	}
	return size;
}

static int vega12_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	int result = 0;
	uint32_t num_turned_on_displays = 1;
	Watermarks_t *wm_table = &(data->smc_state_table.water_marks_table);
	struct cgs_display_info info = {0};

	if ((data->water_marks_bitmap & WaterMarksExist) &&
			!(data->water_marks_bitmap & WaterMarksLoaded)) {
		result = vega12_copy_table_to_smc(hwmgr,
			(uint8_t *)wm_table, TABLE_WATERMARKS);
		PP_ASSERT_WITH_CODE(result, "Failed to update WMTABLE!", return EINVAL);
		data->water_marks_bitmap |= WaterMarksLoaded;
	}

	if ((data->water_marks_bitmap & WaterMarksExist) &&
		data->smu_features[GNLD_DPM_DCEFCLK].supported &&
		data->smu_features[GNLD_DPM_SOCCLK].supported) {
		cgs_get_active_displays_info(hwmgr->device, &info);
		num_turned_on_displays = info.display_count;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, num_turned_on_displays);
	}

	return result;
}

int vega12_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
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

	data->vce_power_gated = bgate;
	vega12_enable_disable_vce_dpm(hwmgr, !bgate);
}

static void vega12_power_gate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = bgate;
	vega12_enable_disable_uvd_dpm(hwmgr, !bgate);
}

static inline bool vega12_are_power_levels_equal(
				const struct vega12_performance_level *pl1,
				const struct vega12_performance_level *pl2)
{
	return ((pl1->soc_clock == pl2->soc_clock) &&
			(pl1->gfx_clock == pl2->gfx_clock) &&
			(pl1->mem_clock == pl2->mem_clock));
}

static int vega12_check_states_equal(struct pp_hwmgr *hwmgr,
				const struct pp_hw_power_state *pstate1,
			const struct pp_hw_power_state *pstate2, bool *equal)
{
	const struct vega12_power_state *psa;
	const struct vega12_power_state *psb;
	int i;

	if (pstate1 == NULL || pstate2 == NULL || equal == NULL)
		return -EINVAL;

	psa = cast_const_phw_vega12_power_state(pstate1);
	psb = cast_const_phw_vega12_power_state(pstate2);
	/* If the two states don't even have the same number of performance levels they cannot be the same state. */
	if (psa->performance_level_count != psb->performance_level_count) {
		*equal = false;
		return 0;
	}

	for (i = 0; i < psa->performance_level_count; i++) {
		if (!vega12_are_power_levels_equal(&(psa->performance_levels[i]), &(psb->performance_levels[i]))) {
			/* If we have found even one performance level pair that is different the states are different. */
			*equal = false;
			return 0;
		}
	}

	/* If all performance levels are the same try to use the UVD clocks to break the tie.*/
	*equal = ((psa->uvd_clks.vclk == psb->uvd_clks.vclk) && (psa->uvd_clks.dclk == psb->uvd_clks.dclk));
	*equal &= ((psa->vce_clks.evclk == psb->vce_clks.evclk) && (psa->vce_clks.ecclk == psb->vce_clks.ecclk));
	*equal &= (psa->sclk_threshold == psb->sclk_threshold);

	return 0;
}

static bool
vega12_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	bool is_update_required = false;
	struct cgs_display_info info = {0, 0, NULL};

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (data->display_timing.num_existing_displays != info.display_count)
		is_update_required = true;

	if (data->registry_data.gfx_clk_deep_sleep_support) {
		if (data->display_timing.min_clock_in_sr != hwmgr->display_config.min_core_set_clock_in_sr)
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
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	uint32_t sclk_idx = ~0, mclk_idx = ~0;

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_AUTO)
		return -EINVAL;

	vega12_find_min_clock_index(hwmgr, &sclk_idx, &mclk_idx,
			request->min_sclk, request->min_mclk);

	if (sclk_idx != ~0) {
		if (!data->registry_data.sclk_dpm_key_disabled)
			PP_ASSERT_WITH_CODE(
					!smum_send_msg_to_smc_with_parameter(
					hwmgr,
					PPSMC_MSG_SetSoftMinGfxclkByIndex,
					sclk_idx),
					"Failed to set soft min sclk index!",
					return -EINVAL);
	}

	if (mclk_idx != ~0) {
		if (!data->registry_data.mclk_dpm_key_disabled)
			PP_ASSERT_WITH_CODE(
					!smum_send_msg_to_smc_with_parameter(
					hwmgr,
					PPSMC_MSG_SetSoftMinUclkByIndex,
					mclk_idx),
					"Failed to set soft min mclk index!",
					return -EINVAL);
	}

	return 0;
}

static int vega12_get_sclk_od(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *sclk_table = &(data->dpm_table.gfx_table);
	struct vega12_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.gfx_table);
	int value;

	value = (sclk_table->dpm_levels[sclk_table->count - 1].value -
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value) *
			100 /
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value;

	return value;
}

static int vega12_set_sclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.gfx_table);
	struct pp_power_state *ps;
	struct vega12_power_state *vega12_ps;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	vega12_ps = cast_phw_vega12_power_state(&ps->hardware);

	vega12_ps->performance_levels[vega12_ps->performance_level_count - 1].gfx_clock =
		golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value * value / 100 +
		golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value;

	if (vega12_ps->performance_levels[vega12_ps->performance_level_count - 1].gfx_clock >
			hwmgr->platform_descriptor.overdriveLimit.engineClock)
		vega12_ps->performance_levels[vega12_ps->performance_level_count - 1].gfx_clock =
			hwmgr->platform_descriptor.overdriveLimit.engineClock;

	return 0;
}

static int vega12_get_mclk_od(struct pp_hwmgr *hwmgr)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *mclk_table = &(data->dpm_table.mem_table);
	struct vega12_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mem_table);
	int value;

	value = (mclk_table->dpm_levels
			[mclk_table->count - 1].value -
			golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value) *
			100 /
			golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value;

	return value;
}

static int vega12_set_mclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct vega12_hwmgr *data = (struct vega12_hwmgr *)(hwmgr->backend);
	struct vega12_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mem_table);
	struct pp_power_state  *ps;
	struct vega12_power_state  *vega12_ps;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	vega12_ps = cast_phw_vega12_power_state(&ps->hardware);

	vega12_ps->performance_levels
	[vega12_ps->performance_level_count - 1].mem_clock =
			golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value *
			value / 100 +
			golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value;

	if (vega12_ps->performance_levels
			[vega12_ps->performance_level_count - 1].mem_clock >
			hwmgr->platform_descriptor.overdriveLimit.memoryClock)
		vega12_ps->performance_levels
		[vega12_ps->performance_level_count - 1].mem_clock =
				hwmgr->platform_descriptor.overdriveLimit.memoryClock;

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

static int vega12_get_thermal_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *thermal_data)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;

	memcpy(thermal_data, &SMU7ThermalWithDelayPolicy[0], sizeof(struct PP_TemperatureRange));

	thermal_data->max = pptable_information->us_software_shutdown_temp *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int vega12_is_hardware_ctf_enabled(struct pp_hwmgr *hwmgr)
{
	uint32_t reg;

	reg = soc15_get_register_offset(THM_HWID, 0,
			mmTHM_TCON_THERM_TRIP_BASE_IDX,
			mmTHM_TCON_THERM_TRIP);

	return (((cgs_read_register(hwmgr->device, reg) &
		THM_TCON_THERM_TRIP__THERM_TP_EN_MASK) >>
		THM_TCON_THERM_TRIP__THERM_TP_EN__SHIFT) == 1);
}

static int vega12_register_thermal_interrupt(struct pp_hwmgr *hwmgr,
		const void *info)
{
	struct cgs_irq_src_funcs *irq_src =
			(struct cgs_irq_src_funcs *)info;

	if (hwmgr->thermal_controller.ucType ==
			ATOM_VEGA12_PP_THERMALCONTROLLER_VEGA12) {
		PP_ASSERT_WITH_CODE(!cgs_add_irq_source(hwmgr->device,
				0xf, /* AMDGPU_IH_CLIENTID_THM */
				0, 0, irq_src[0].set, irq_src[0].handler, hwmgr),
				"Failed to register high thermal interrupt!",
				return -EINVAL);
		PP_ASSERT_WITH_CODE(!cgs_add_irq_source(hwmgr->device,
				0xf, /* AMDGPU_IH_CLIENTID_THM */
				1, 0, irq_src[1].set, irq_src[1].handler, hwmgr),
				"Failed to register low thermal interrupt!",
				return -EINVAL);
	}

	if (vega12_is_hardware_ctf_enabled(hwmgr))
		/* Register CTF(GPIO_19) interrupt */
		PP_ASSERT_WITH_CODE(!cgs_add_irq_source(hwmgr->device,
				0x16, /* AMDGPU_IH_CLIENTID_ROM_SMUIO, */
				83, 0, irq_src[2].set, irq_src[2].handler, hwmgr),
				"Failed to register CTF thermal interrupt!",
				return -EINVAL);

	return 0;
}

static const struct pp_hwmgr_func vega12_hwmgr_funcs = {
	.backend_init = vega12_hwmgr_backend_init,
	.backend_fini = vega12_hwmgr_backend_fini,
	.asic_setup = vega12_setup_asic_task,
	.dynamic_state_management_enable = vega12_enable_dpm_tasks,
	.dynamic_state_management_disable = vega12_disable_dpm_tasks,
	.get_num_of_pp_table_entries =
			vega12_get_number_of_pp_table_entries,
	.get_power_state_size = vega12_get_power_state_size,
	.patch_boot_state = vega12_patch_boot_state,
	.apply_state_adjust_rules = vega12_apply_state_adjust_rules,
	.power_state_set = vega12_set_power_state_tasks,
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
	.display_config_changed = vega12_display_configuration_changed_task,
	.powergate_uvd = vega12_power_gate_uvd,
	.powergate_vce = vega12_power_gate_vce,
	.check_states_equal = vega12_check_states_equal,
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
	.register_internal_thermal_interrupt = vega12_register_thermal_interrupt,
	.start_thermal_controller = vega12_start_thermal_controller,
};

int vega12_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &vega12_hwmgr_funcs;
	hwmgr->pptable_func = &vega12_pptable_funcs;

	return 0;
}
