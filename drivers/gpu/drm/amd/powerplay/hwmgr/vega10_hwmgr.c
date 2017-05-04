/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include "linux/delay.h"

#include "hwmgr.h"
#include "amd_powerplay.h"
#include "vega10_smumgr.h"
#include "hardwaremanager.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "cgs_common.h"
#include "vega10_powertune.h"
#include "smu9.h"
#include "smu9_driver_if.h"
#include "vega10_inc.h"
#include "pp_soc15.h"
#include "pppcielanes.h"
#include "vega10_hwmgr.h"
#include "vega10_processpptables.h"
#include "vega10_pptable.h"
#include "vega10_thermal.h"
#include "pp_debug.h"
#include "pp_acpi.h"
#include "amd_pcie_helpers.h"
#include "cgs_linux.h"
#include "ppinterrupt.h"


#define VOLTAGE_SCALE  4
#define VOLTAGE_VID_OFFSET_SCALE1   625
#define VOLTAGE_VID_OFFSET_SCALE2   100

#define HBM_MEMORY_CHANNEL_WIDTH    128

uint32_t channel_number[] = {1, 2, 0, 4, 0, 8, 0, 16, 2};

#define MEM_FREQ_LOW_LATENCY        25000
#define MEM_FREQ_HIGH_LATENCY       80000
#define MEM_LATENCY_HIGH            245
#define MEM_LATENCY_LOW             35
#define MEM_LATENCY_ERR             0xFFFF

#define mmDF_CS_AON0_DramBaseAddress0                                                                  0x0044
#define mmDF_CS_AON0_DramBaseAddress0_BASE_IDX                                                         0

//DF_CS_AON0_DramBaseAddress0
#define DF_CS_AON0_DramBaseAddress0__AddrRngVal__SHIFT                                                        0x0
#define DF_CS_AON0_DramBaseAddress0__LgcyMmioHoleEn__SHIFT                                                    0x1
#define DF_CS_AON0_DramBaseAddress0__IntLvNumChan__SHIFT                                                      0x4
#define DF_CS_AON0_DramBaseAddress0__IntLvAddrSel__SHIFT                                                      0x8
#define DF_CS_AON0_DramBaseAddress0__DramBaseAddr__SHIFT                                                      0xc
#define DF_CS_AON0_DramBaseAddress0__AddrRngVal_MASK                                                          0x00000001L
#define DF_CS_AON0_DramBaseAddress0__LgcyMmioHoleEn_MASK                                                      0x00000002L
#define DF_CS_AON0_DramBaseAddress0__IntLvNumChan_MASK                                                        0x000000F0L
#define DF_CS_AON0_DramBaseAddress0__IntLvAddrSel_MASK                                                        0x00000700L
#define DF_CS_AON0_DramBaseAddress0__DramBaseAddr_MASK                                                        0xFFFFF000L

const ULONG PhwVega10_Magic = (ULONG)(PHM_VIslands_Magic);

struct vega10_power_state *cast_phw_vega10_power_state(
				  struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwVega10_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (struct vega10_power_state *)hw_ps;
}

const struct vega10_power_state *cast_const_phw_vega10_power_state(
				 const struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwVega10_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (const struct vega10_power_state *)hw_ps;
}

static void vega10_set_default_registry_data(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	data->registry_data.sclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_SCLK_DPM_MASK ? false : true;
	data->registry_data.socclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_SOCCLK_DPM_MASK ? false : true;
	data->registry_data.mclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_MCLK_DPM_MASK ? false : true;

	data->registry_data.dcefclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_DCEFCLK_DPM_MASK ? false : true;

	if (hwmgr->feature_mask & PP_POWER_CONTAINMENT_MASK) {
		data->registry_data.power_containment_support = 1;
		data->registry_data.enable_pkg_pwr_tracking_feature = 1;
		data->registry_data.enable_tdc_limit_feature = 1;
	}

	data->registry_data.pcie_dpm_key_disabled = 1;
	data->registry_data.disable_water_mark = 0;

	data->registry_data.fan_control_support = 1;
	data->registry_data.thermal_support = 1;
	data->registry_data.fw_ctf_enabled = 1;

	data->registry_data.avfs_support = 1;
	data->registry_data.led_dpm_enabled = 1;

	data->registry_data.vr0hot_enabled = 1;
	data->registry_data.vr1hot_enabled = 1;
	data->registry_data.regulator_hot_gpio_support = 1;

	data->display_voltage_mode = PPVEGA10_VEGA10DISPLAYVOLTAGEMODE_DFLT;
	data->dcef_clk_quad_eqn_a = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->dcef_clk_quad_eqn_b = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->dcef_clk_quad_eqn_c = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_a = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_b = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->disp_clk_quad_eqn_c = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_a = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_b = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->pixel_clk_quad_eqn_c = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_a = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_b = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;
	data->phy_clk_quad_eqn_c = PPREGKEY_VEGA10QUADRATICEQUATION_DFLT;

	data->gfxclk_average_alpha = PPVEGA10_VEGA10GFXCLKAVERAGEALPHA_DFLT;
	data->socclk_average_alpha = PPVEGA10_VEGA10SOCCLKAVERAGEALPHA_DFLT;
	data->uclk_average_alpha = PPVEGA10_VEGA10UCLKCLKAVERAGEALPHA_DFLT;
	data->gfx_activity_average_alpha = PPVEGA10_VEGA10GFXACTIVITYAVERAGEALPHA_DFLT;
}

static int vega10_set_features_platform_caps(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct cgs_system_info sys_info = {0};
	int result;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPatchPowerState);

	if (data->vddci_control == VEGA10_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ControlVDDCI);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TablelessHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableSMU7ThermalManagement);

	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_PG_FLAGS;
	result = cgs_query_system_info(hwmgr->device, &sys_info);

	if (!result && (sys_info.value & AMD_PG_SUPPORT_UVD))
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_UVDPowerGating);

	if (!result && (sys_info.value & AMD_PG_SUPPORT_VCE))
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_VCEPowerGating);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UnTabledHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_FanSpeedInTableIsRPM);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ODFuzzyFanControlSupport);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_DynamicPowerManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SMC);

	/* power tune caps */
	/* assume disabled */
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);

	if (data->registry_data.power_containment_support)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_PowerContainment);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC);

	if (table_info->tdp_table->usClockStretchAmount &&
			data->registry_data.clock_stretcher_support)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ClockStretcher);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UVDDPM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_VCEDPM);

	return 0;
}

static void vega10_init_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	int i;

	vega10_initialize_power_tune_defaults(hwmgr);

	for (i = 0; i < GNLD_FEATURES_MAX; i++) {
		data->smu_features[i].smu_feature_id = 0xffff;
		data->smu_features[i].smu_feature_bitmap = 1 << i;
		data->smu_features[i].enabled = false;
		data->smu_features[i].supported = false;
	}

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
	data->smu_features[GNLD_DPM_MP0CLK].smu_feature_id =
			FEATURE_DPM_MP0CLK_BIT;
	data->smu_features[GNLD_DPM_LINK].smu_feature_id =
			FEATURE_DPM_LINK_BIT;
	data->smu_features[GNLD_DPM_DCEFCLK].smu_feature_id =
			FEATURE_DPM_DCEFCLK_BIT;
	data->smu_features[GNLD_ULV].smu_feature_id =
			FEATURE_ULV_BIT;
	data->smu_features[GNLD_AVFS].smu_feature_id =
			FEATURE_AVFS_BIT;
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
	data->smu_features[GNLD_VOLTAGE_CONTROLLER].smu_feature_id =
			FEATURE_VOLTAGE_CONTROLLER_BIT;

	if (!data->registry_data.prefetcher_dpm_key_disabled)
		data->smu_features[GNLD_DPM_PREFETCHER].supported = true;

	if (!data->registry_data.sclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_GFXCLK].supported = true;

	if (!data->registry_data.mclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_UCLK].supported = true;

	if (!data->registry_data.socclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_SOCCLK].supported = true;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UVDDPM))
		data->smu_features[GNLD_DPM_UVD].supported = true;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_VCEDPM))
		data->smu_features[GNLD_DPM_VCE].supported = true;

	if (!data->registry_data.pcie_dpm_key_disabled)
		data->smu_features[GNLD_DPM_LINK].supported = true;

	if (!data->registry_data.dcefclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_DCEFCLK].supported = true;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep) &&
			data->registry_data.sclk_deep_sleep_support) {
		data->smu_features[GNLD_DS_GFXCLK].supported = true;
		data->smu_features[GNLD_DS_SOCCLK].supported = true;
		data->smu_features[GNLD_DS_LCLK].supported = true;
	}

	if (data->registry_data.enable_pkg_pwr_tracking_feature)
		data->smu_features[GNLD_PPT].supported = true;

	if (data->registry_data.enable_tdc_limit_feature)
		data->smu_features[GNLD_TDC].supported = true;

	if (data->registry_data.thermal_support)
		data->smu_features[GNLD_THERMAL].supported = true;

	if (data->registry_data.fan_control_support)
		data->smu_features[GNLD_FAN_CONTROL].supported = true;

	if (data->registry_data.fw_ctf_enabled)
		data->smu_features[GNLD_FW_CTF].supported = true;

	if (data->registry_data.avfs_support)
		data->smu_features[GNLD_AVFS].supported = true;

	if (data->registry_data.led_dpm_enabled)
		data->smu_features[GNLD_LED_DISPLAY].supported = true;

	if (data->registry_data.vr1hot_enabled)
		data->smu_features[GNLD_VR1HOT].supported = true;

	if (data->registry_data.vr0hot_enabled)
		data->smu_features[GNLD_VR0HOT].supported = true;

}

#ifdef PPLIB_VEGA10_EVV_SUPPORT
static int vega10_get_socclk_for_voltage_evv(struct pp_hwmgr *hwmgr,
	phm_ppt_v1_voltage_lookup_table *lookup_table,
	uint16_t virtual_voltage_id, int32_t *socclk)
{
	uint8_t entry_id;
	uint8_t voltage_id;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);

	PP_ASSERT_WITH_CODE(lookup_table->count != 0,
			"Lookup table is empty",
			return -EINVAL);

	/* search for leakage voltage ID 0xff01 ~ 0xff08 and sclk */
	for (entry_id = 0; entry_id < table_info->vdd_dep_on_sclk->count; entry_id++) {
		voltage_id = table_info->vdd_dep_on_socclk->entries[entry_id].vddInd;
		if (lookup_table->entries[voltage_id].us_vdd == virtual_voltage_id)
			break;
	}

	PP_ASSERT_WITH_CODE(entry_id < table_info->vdd_dep_on_socclk->count,
			"Can't find requested voltage id in vdd_dep_on_socclk table!",
			return -EINVAL);

	*socclk = table_info->vdd_dep_on_socclk->entries[entry_id].clk;

	return 0;
}

#define ATOM_VIRTUAL_VOLTAGE_ID0             0xff01
/**
* Get Leakage VDDC based on leakage ID.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0.
*/
static int vega10_get_evv_voltages(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	uint16_t vv_id;
	uint32_t vddc = 0;
	uint16_t i, j;
	uint32_t sclk = 0;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *socclk_table =
			table_info->vdd_dep_on_socclk;
	int result;

	for (i = 0; i < VEGA10_MAX_LEAKAGE_COUNT; i++) {
		vv_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;

		if (!vega10_get_socclk_for_voltage_evv(hwmgr,
				table_info->vddc_lookup_table, vv_id, &sclk)) {
			if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_ClockStretcher)) {
				for (j = 1; j < socclk_table->count; j++) {
					if (socclk_table->entries[j].clk == sclk &&
							socclk_table->entries[j].cks_enable == 0) {
						sclk += 5000;
						break;
					}
				}
			}

			PP_ASSERT_WITH_CODE(!atomctrl_get_voltage_evv_on_sclk_ai(hwmgr,
					VOLTAGE_TYPE_VDDC, sclk, vv_id, &vddc),
					"Error retrieving EVV voltage value!",
					continue);


			/* need to make sure vddc is less than 2v or else, it could burn the ASIC. */
			PP_ASSERT_WITH_CODE((vddc < 2000 && vddc != 0),
					"Invalid VDDC value", result = -EINVAL;);

			/* the voltage should not be zero nor equal to leakage ID */
			if (vddc != 0 && vddc != vv_id) {
				data->vddc_leakage.actual_voltage[data->vddc_leakage.count] = (uint16_t)(vddc/100);
				data->vddc_leakage.leakage_id[data->vddc_leakage.count] = vv_id;
				data->vddc_leakage.count++;
			}
		}
	}

	return 0;
}

/**
 * Change virtual leakage voltage to actual value.
 *
 * @param     hwmgr  the address of the powerplay hardware manager.
 * @param     pointer to changing voltage
 * @param     pointer to leakage table
 */
static void vega10_patch_with_vdd_leakage(struct pp_hwmgr *hwmgr,
		uint16_t *voltage, struct vega10_leakage_voltage *leakage_table)
{
	uint32_t index;

	/* search for leakage voltage ID 0xff01 ~ 0xff08 */
	for (index = 0; index < leakage_table->count; index++) {
		/* if this voltage matches a leakage voltage ID */
		/* patch with actual leakage voltage */
		if (leakage_table->leakage_id[index] == *voltage) {
			*voltage = leakage_table->actual_voltage[index];
			break;
		}
	}

	if (*voltage > ATOM_VIRTUAL_VOLTAGE_ID0)
		pr_info("Voltage value looks like a Leakage ID \
				but it's not patched\n");
}

/**
* Patch voltage lookup table by EVV leakages.
*
* @param     hwmgr  the address of the powerplay hardware manager.
* @param     pointer to voltage lookup table
* @param     pointer to leakage table
* @return     always 0
*/
static int vega10_patch_lookup_table_with_leakage(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_voltage_lookup_table *lookup_table,
		struct vega10_leakage_voltage *leakage_table)
{
	uint32_t i;

	for (i = 0; i < lookup_table->count; i++)
		vega10_patch_with_vdd_leakage(hwmgr,
				&lookup_table->entries[i].us_vdd, leakage_table);

	return 0;
}

static int vega10_patch_clock_voltage_limits_with_vddc_leakage(
		struct pp_hwmgr *hwmgr, struct vega10_leakage_voltage *leakage_table,
		uint16_t *vddc)
{
	vega10_patch_with_vdd_leakage(hwmgr, (uint16_t *)vddc, leakage_table);

	return 0;
}
#endif

static int vega10_patch_voltage_dependency_tables_with_lookup_table(
		struct pp_hwmgr *hwmgr)
{
	uint8_t entry_id;
	uint8_t voltage_id;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *socclk_table =
			table_info->vdd_dep_on_socclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *gfxclk_table =
			table_info->vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dcefclk_table =
			table_info->vdd_dep_on_dcefclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *pixclk_table =
			table_info->vdd_dep_on_pixclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dspclk_table =
			table_info->vdd_dep_on_dispclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *phyclk_table =
			table_info->vdd_dep_on_phyclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *mclk_table =
			table_info->vdd_dep_on_mclk;
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;

	for (entry_id = 0; entry_id < socclk_table->count; entry_id++) {
		voltage_id = socclk_table->entries[entry_id].vddInd;
		socclk_table->entries[entry_id].vddc =
				table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
	}

	for (entry_id = 0; entry_id < gfxclk_table->count; entry_id++) {
		voltage_id = gfxclk_table->entries[entry_id].vddInd;
		gfxclk_table->entries[entry_id].vddc =
				table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
	}

	for (entry_id = 0; entry_id < dcefclk_table->count; entry_id++) {
		voltage_id = dcefclk_table->entries[entry_id].vddInd;
		dcefclk_table->entries[entry_id].vddc =
				table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
	}

	for (entry_id = 0; entry_id < pixclk_table->count; entry_id++) {
		voltage_id = pixclk_table->entries[entry_id].vddInd;
		pixclk_table->entries[entry_id].vddc =
				table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
	}

	for (entry_id = 0; entry_id < dspclk_table->count; entry_id++) {
		voltage_id = dspclk_table->entries[entry_id].vddInd;
		dspclk_table->entries[entry_id].vddc =
				table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
	}

	for (entry_id = 0; entry_id < phyclk_table->count; entry_id++) {
		voltage_id = phyclk_table->entries[entry_id].vddInd;
		phyclk_table->entries[entry_id].vddc =
				table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
	}

	for (entry_id = 0; entry_id < mclk_table->count; ++entry_id) {
		voltage_id = mclk_table->entries[entry_id].vddInd;
		mclk_table->entries[entry_id].vddc =
				table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
		voltage_id = mclk_table->entries[entry_id].vddciInd;
		mclk_table->entries[entry_id].vddci =
				table_info->vddci_lookup_table->entries[voltage_id].us_vdd;
		voltage_id = mclk_table->entries[entry_id].mvddInd;
		mclk_table->entries[entry_id].mvdd =
				table_info->vddmem_lookup_table->entries[voltage_id].us_vdd;
	}

	for (entry_id = 0; entry_id < mm_table->count; ++entry_id) {
		voltage_id = mm_table->entries[entry_id].vddcInd;
		mm_table->entries[entry_id].vddc =
			table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
	}

	return 0;

}

static int vega10_sort_lookup_table(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_voltage_lookup_table *lookup_table)
{
	uint32_t table_size, i, j;
	struct phm_ppt_v1_voltage_lookup_record tmp_voltage_lookup_record;

	PP_ASSERT_WITH_CODE(lookup_table && lookup_table->count,
		"Lookup table is empty", return -EINVAL);

	table_size = lookup_table->count;

	/* Sorting voltages */
	for (i = 0; i < table_size - 1; i++) {
		for (j = i + 1; j > 0; j--) {
			if (lookup_table->entries[j].us_vdd <
					lookup_table->entries[j - 1].us_vdd) {
				tmp_voltage_lookup_record = lookup_table->entries[j - 1];
				lookup_table->entries[j - 1] = lookup_table->entries[j];
				lookup_table->entries[j] = tmp_voltage_lookup_record;
			}
		}
	}

	return 0;
}

static int vega10_complete_dependency_tables(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	int tmp_result;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
#ifdef PPLIB_VEGA10_EVV_SUPPORT
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);

	tmp_result = vega10_patch_lookup_table_with_leakage(hwmgr,
			table_info->vddc_lookup_table, &(data->vddc_leakage));
	if (tmp_result)
		result = tmp_result;

	tmp_result = vega10_patch_clock_voltage_limits_with_vddc_leakage(hwmgr,
			&(data->vddc_leakage), &table_info->max_clock_voltage_on_dc.vddc);
	if (tmp_result)
		result = tmp_result;
#endif

	tmp_result = vega10_patch_voltage_dependency_tables_with_lookup_table(hwmgr);
	if (tmp_result)
		result = tmp_result;

	tmp_result = vega10_sort_lookup_table(hwmgr, table_info->vddc_lookup_table);
	if (tmp_result)
		result = tmp_result;

	return result;
}

static int vega10_set_private_data_based_on_pptable(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *allowed_sclk_vdd_table =
			table_info->vdd_dep_on_socclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *allowed_mclk_vdd_table =
			table_info->vdd_dep_on_mclk;

	PP_ASSERT_WITH_CODE(allowed_sclk_vdd_table,
		"VDD dependency on SCLK table is missing. \
		This table is mandatory", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_sclk_vdd_table->count >= 1,
		"VDD dependency on SCLK table is empty. \
		This table is mandatory", return -EINVAL);

	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table,
		"VDD dependency on MCLK table is missing. \
		This table is mandatory", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table->count >= 1,
		"VDD dependency on MCLK table is empty. \
		This table is mandatory", return -EINVAL);

	table_info->max_clock_voltage_on_ac.sclk =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].clk;
	table_info->max_clock_voltage_on_ac.mclk =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].clk;
	table_info->max_clock_voltage_on_ac.vddc =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].vddc;
	table_info->max_clock_voltage_on_ac.vddci =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].vddci;

	hwmgr->dyn_state.max_clock_voltage_on_ac.sclk =
		table_info->max_clock_voltage_on_ac.sclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.mclk =
		table_info->max_clock_voltage_on_ac.mclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddc =
		table_info->max_clock_voltage_on_ac.vddc;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddci =
		table_info->max_clock_voltage_on_ac.vddci;

	return 0;
}

static int vega10_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	kfree(hwmgr->dyn_state.vddc_dep_on_dal_pwrl);
	hwmgr->dyn_state.vddc_dep_on_dal_pwrl = NULL;

	kfree(hwmgr->backend);
	hwmgr->backend = NULL;

	return 0;
}

static int vega10_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct vega10_hwmgr *data;
	uint32_t config_telemetry = 0;
	struct pp_atomfwctrl_voltage_table vol_table;
	struct cgs_system_info sys_info = {0};

	data = kzalloc(sizeof(struct vega10_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	vega10_set_default_registry_data(hwmgr);

	data->disable_dpm_mask = 0xff;
	data->workload_mask = 0xff;

	/* need to set voltage control types before EVV patching */
	data->vddc_control = VEGA10_VOLTAGE_CONTROL_NONE;
	data->mvdd_control = VEGA10_VOLTAGE_CONTROL_NONE;
	data->vddci_control = VEGA10_VOLTAGE_CONTROL_NONE;

	/* VDDCR_SOC */
	if (pp_atomfwctrl_is_voltage_controlled_by_gpio_v4(hwmgr,
			VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_SVID2)) {
		if (!pp_atomfwctrl_get_voltage_table_v4(hwmgr,
				VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_SVID2,
				&vol_table)) {
			config_telemetry = ((vol_table.telemetry_slope << 8) & 0xff00) |
					(vol_table.telemetry_offset & 0xff);
			data->vddc_control = VEGA10_VOLTAGE_CONTROL_BY_SVID2;
		}
	} else {
		kfree(hwmgr->backend);
		hwmgr->backend = NULL;
		PP_ASSERT_WITH_CODE(false,
				"VDDCR_SOC is not SVID2!",
				return -1);
	}

	/* MVDDC */
	if (pp_atomfwctrl_is_voltage_controlled_by_gpio_v4(hwmgr,
			VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_SVID2)) {
		if (!pp_atomfwctrl_get_voltage_table_v4(hwmgr,
				VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_SVID2,
				&vol_table)) {
			config_telemetry |=
					((vol_table.telemetry_slope << 24) & 0xff000000) |
					((vol_table.telemetry_offset << 16) & 0xff0000);
			data->mvdd_control = VEGA10_VOLTAGE_CONTROL_BY_SVID2;
		}
	}

	 /* VDDCI_MEM */
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ControlVDDCI)) {
		if (pp_atomfwctrl_is_voltage_controlled_by_gpio_v4(hwmgr,
				VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT))
			data->vddci_control = VEGA10_VOLTAGE_CONTROL_BY_GPIO;
	}

	data->config_telemetry = config_telemetry;

	vega10_set_features_platform_caps(hwmgr);

	vega10_init_dpm_defaults(hwmgr);

#ifdef PPLIB_VEGA10_EVV_SUPPORT
	/* Get leakage voltage based on leakage ID. */
	PP_ASSERT_WITH_CODE(!vega10_get_evv_voltages(hwmgr),
			"Get EVV Voltage Failed.  Abort Driver loading!",
			return -1);
#endif

	/* Patch our voltage dependency table with actual leakage voltage
	 * We need to perform leakage translation before it's used by other functions
	 */
	vega10_complete_dependency_tables(hwmgr);

	/* Parse pptable data read from VBIOS */
	vega10_set_private_data_based_on_pptable(hwmgr);

	data->is_tlu_enabled = false;

	hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
			VEGA10_MAX_HARDWARE_POWERLEVELS;
	hwmgr->platform_descriptor.hardwarePerformanceLevels = 2;
	hwmgr->platform_descriptor.minimumClocksReductionPercentage = 50;

	hwmgr->platform_descriptor.vbiosInterruptId = 0x20000400; /* IRQ_SOURCE1_SW_INT */
	/* The true clock step depends on the frequency, typically 4.5 or 9 MHz. Here we use 5. */
	hwmgr->platform_descriptor.clockStep.engineClock = 500;
	hwmgr->platform_descriptor.clockStep.memoryClock = 500;

	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_GFX_CU_INFO;
	result = cgs_query_system_info(hwmgr->device, &sys_info);
	data->total_active_cus = sys_info.value;
	/* Setup default Overdrive Fan control settings */
	data->odn_fan_table.target_fan_speed =
			hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM;
	data->odn_fan_table.target_temperature =
			hwmgr->thermal_controller.
			advanceFanControlParameters.ucTargetTemperature;
	data->odn_fan_table.min_performance_clock =
			hwmgr->thermal_controller.advanceFanControlParameters.
			ulMinFanSCLKAcousticLimit;
	data->odn_fan_table.min_fan_limit =
			hwmgr->thermal_controller.
			advanceFanControlParameters.usFanPWMMinLimit *
			hwmgr->thermal_controller.fanInfo.ulMaxRPM / 100;

	return result;
}

static int vega10_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	data->low_sclk_interrupt_threshold = 0;

	return 0;
}

static int vega10_setup_dpm_led_config(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);

	struct pp_atomfwctrl_voltage_table table;
	uint8_t i, j;
	uint32_t mask = 0;
	uint32_t tmp;
	int32_t ret = 0;

	ret = pp_atomfwctrl_get_voltage_table_v4(hwmgr, VOLTAGE_TYPE_LEDDPM,
						VOLTAGE_OBJ_GPIO_LUT, &table);

	if (!ret) {
		tmp = table.mask_low;
		for (i = 0, j = 0; i < 32; i++) {
			if (tmp & 1) {
				mask |= (uint32_t)(i << (8 * j));
				if (++j >= 3)
					break;
			}
			tmp >>= 1;
		}
	}

	pp_table->LedPin0 = (uint8_t)(mask & 0xff);
	pp_table->LedPin1 = (uint8_t)((mask >> 8) & 0xff);
	pp_table->LedPin2 = (uint8_t)((mask >> 16) & 0xff);
	return 0;
}

static int vega10_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(!vega10_init_sclk_threshold(hwmgr),
			"Failed to init sclk threshold!",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(!vega10_setup_dpm_led_config(hwmgr),
			"Failed to set up led dpm config!",
			return -EINVAL);

	return 0;
}

static bool vega10_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	uint32_t features_enabled;

	if (!vega10_get_smc_features(hwmgr->smumgr, &features_enabled)) {
		if (features_enabled & SMC_DPM_FEATURES)
			return true;
	}
	return false;
}

/**
* Remove repeated voltage values and create table with unique values.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    vol_table  the pointer to changing voltage table
* @return    0 in success
*/

static int vega10_trim_voltage_table(struct pp_hwmgr *hwmgr,
		struct pp_atomfwctrl_voltage_table *vol_table)
{
	uint32_t i, j;
	uint16_t vvalue;
	bool found = false;
	struct pp_atomfwctrl_voltage_table *table;

	PP_ASSERT_WITH_CODE(vol_table,
			"Voltage Table empty.", return -EINVAL);
	table = kzalloc(sizeof(struct pp_atomfwctrl_voltage_table),
			GFP_KERNEL);

	if (!table)
		return -ENOMEM;

	table->mask_low = vol_table->mask_low;
	table->phase_delay = vol_table->phase_delay;

	for (i = 0; i < vol_table->count; i++) {
		vvalue = vol_table->entries[i].value;
		found = false;

		for (j = 0; j < table->count; j++) {
			if (vvalue == table->entries[j].value) {
				found = true;
				break;
			}
		}

		if (!found) {
			table->entries[table->count].value = vvalue;
			table->entries[table->count].smio_low =
					vol_table->entries[i].smio_low;
			table->count++;
		}
	}

	memcpy(vol_table, table, sizeof(struct pp_atomfwctrl_voltage_table));
	kfree(table);

	return 0;
}

static int vega10_get_mvdd_voltage_table(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table *dep_table,
		struct pp_atomfwctrl_voltage_table *vol_table)
{
	int i;

	PP_ASSERT_WITH_CODE(dep_table->count,
			"Voltage Dependency Table empty.",
			return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;
	vol_table->count = dep_table->count;

	for (i = 0; i < vol_table->count; i++) {
		vol_table->entries[i].value = dep_table->entries[i].mvdd;
		vol_table->entries[i].smio_low = 0;
	}

	PP_ASSERT_WITH_CODE(!vega10_trim_voltage_table(hwmgr,
			vol_table),
			"Failed to trim MVDD Table!",
			return -1);

	return 0;
}

static int vega10_get_vddci_voltage_table(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table *dep_table,
		struct pp_atomfwctrl_voltage_table *vol_table)
{
	uint32_t i;

	PP_ASSERT_WITH_CODE(dep_table->count,
			"Voltage Dependency Table empty.",
			return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;
	vol_table->count = dep_table->count;

	for (i = 0; i < dep_table->count; i++) {
		vol_table->entries[i].value = dep_table->entries[i].vddci;
		vol_table->entries[i].smio_low = 0;
	}

	PP_ASSERT_WITH_CODE(!vega10_trim_voltage_table(hwmgr, vol_table),
			"Failed to trim VDDCI table.",
			return -1);

	return 0;
}

static int vega10_get_vdd_voltage_table(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table *dep_table,
		struct pp_atomfwctrl_voltage_table *vol_table)
{
	int i;

	PP_ASSERT_WITH_CODE(dep_table->count,
			"Voltage Dependency Table empty.",
			return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;
	vol_table->count = dep_table->count;

	for (i = 0; i < vol_table->count; i++) {
		vol_table->entries[i].value = dep_table->entries[i].vddc;
		vol_table->entries[i].smio_low = 0;
	}

	return 0;
}

/* ---- Voltage Tables ----
 * If the voltage table would be bigger than
 * what will fit into the state table on
 * the SMC keep only the higher entries.
 */
static void vega10_trim_voltage_table_to_fit_state_table(
		struct pp_hwmgr *hwmgr,
		uint32_t max_vol_steps,
		struct pp_atomfwctrl_voltage_table *vol_table)
{
	unsigned int i, diff;

	if (vol_table->count <= max_vol_steps)
		return;

	diff = vol_table->count - max_vol_steps;

	for (i = 0; i < max_vol_steps; i++)
		vol_table->entries[i] = vol_table->entries[i + diff];

	vol_table->count = max_vol_steps;
}

/**
* Create Voltage Tables.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int vega10_construct_voltage_tables(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	int result;

	if (data->mvdd_control == VEGA10_VOLTAGE_CONTROL_BY_SVID2 ||
			data->mvdd_control == VEGA10_VOLTAGE_CONTROL_NONE) {
		result = vega10_get_mvdd_voltage_table(hwmgr,
				table_info->vdd_dep_on_mclk,
				&(data->mvdd_voltage_table));
		PP_ASSERT_WITH_CODE(!result,
				"Failed to retrieve MVDDC table!",
				return result);
	}

	if (data->vddci_control == VEGA10_VOLTAGE_CONTROL_NONE) {
		result = vega10_get_vddci_voltage_table(hwmgr,
				table_info->vdd_dep_on_mclk,
				&(data->vddci_voltage_table));
		PP_ASSERT_WITH_CODE(!result,
				"Failed to retrieve VDDCI_MEM table!",
				return result);
	}

	if (data->vddc_control == VEGA10_VOLTAGE_CONTROL_BY_SVID2 ||
			data->vddc_control == VEGA10_VOLTAGE_CONTROL_NONE) {
		result = vega10_get_vdd_voltage_table(hwmgr,
				table_info->vdd_dep_on_sclk,
				&(data->vddc_voltage_table));
		PP_ASSERT_WITH_CODE(!result,
				"Failed to retrieve VDDCR_SOC table!",
				return result);
	}

	PP_ASSERT_WITH_CODE(data->vddc_voltage_table.count <= 16,
			"Too many voltage values for VDDC. Trimming to fit state table.",
			vega10_trim_voltage_table_to_fit_state_table(hwmgr,
					16, &(data->vddc_voltage_table)));

	PP_ASSERT_WITH_CODE(data->vddci_voltage_table.count <= 16,
			"Too many voltage values for VDDCI. Trimming to fit state table.",
			vega10_trim_voltage_table_to_fit_state_table(hwmgr,
					16, &(data->vddci_voltage_table)));

	PP_ASSERT_WITH_CODE(data->mvdd_voltage_table.count <= 16,
			"Too many voltage values for MVDD. Trimming to fit state table.",
			vega10_trim_voltage_table_to_fit_state_table(hwmgr,
					16, &(data->mvdd_voltage_table)));


	return 0;
}

/*
 * @fn vega10_init_dpm_state
 * @brief Function to initialize all Soft Min/Max and Hard Min/Max to 0xff.
 *
 * @param    dpm_state - the address of the DPM Table to initiailize.
 * @return   None.
 */
static void vega10_init_dpm_state(struct vega10_dpm_state *dpm_state)
{
	dpm_state->soft_min_level = 0xff;
	dpm_state->soft_max_level = 0xff;
	dpm_state->hard_min_level = 0xff;
	dpm_state->hard_max_level = 0xff;
}

static void vega10_setup_default_single_dpm_table(struct pp_hwmgr *hwmgr,
		struct vega10_single_dpm_table *dpm_table,
		struct phm_ppt_v1_clock_voltage_dependency_table *dep_table)
{
	int i;

	for (i = 0; i < dep_table->count; i++) {
		if (i == 0 || dpm_table->dpm_levels[dpm_table->count - 1].value !=
				dep_table->entries[i].clk) {
			dpm_table->dpm_levels[dpm_table->count].value =
					dep_table->entries[i].clk;
			dpm_table->dpm_levels[dpm_table->count].enabled = true;
			dpm_table->count++;
		}
	}
}
static int vega10_setup_default_pcie_table(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct vega10_pcie_table *pcie_table = &(data->dpm_table.pcie_table);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_pcie_table *bios_pcie_table =
			table_info->pcie_table;
	uint32_t i;

	PP_ASSERT_WITH_CODE(bios_pcie_table->count,
			"Incorrect number of PCIE States from VBIOS!",
			return -1);

	for (i = 0; i < NUM_LINK_LEVELS - 1; i++) {
		if (data->registry_data.pcieSpeedOverride)
			pcie_table->pcie_gen[i] =
					data->registry_data.pcieSpeedOverride;
		else
			pcie_table->pcie_gen[i] =
					bios_pcie_table->entries[i].gen_speed;

		if (data->registry_data.pcieLaneOverride)
			pcie_table->pcie_lane[i] =
					data->registry_data.pcieLaneOverride;
		else
			pcie_table->pcie_lane[i] =
					bios_pcie_table->entries[i].lane_width;

		if (data->registry_data.pcieClockOverride)
			pcie_table->lclk[i] =
					data->registry_data.pcieClockOverride;
		else
			pcie_table->lclk[i] =
					bios_pcie_table->entries[i].pcie_sclk;

		pcie_table->count++;
	}

	if (data->registry_data.pcieSpeedOverride)
		pcie_table->pcie_gen[i] = data->registry_data.pcieSpeedOverride;
	else
		pcie_table->pcie_gen[i] =
			bios_pcie_table->entries[bios_pcie_table->count - 1].gen_speed;

	if (data->registry_data.pcieLaneOverride)
		pcie_table->pcie_lane[i] = data->registry_data.pcieLaneOverride;
	else
		pcie_table->pcie_lane[i] =
			bios_pcie_table->entries[bios_pcie_table->count - 1].lane_width;

	if (data->registry_data.pcieClockOverride)
		pcie_table->lclk[i] = data->registry_data.pcieClockOverride;
	else
		pcie_table->lclk[i] =
			bios_pcie_table->entries[bios_pcie_table->count - 1].pcie_sclk;

	pcie_table->count++;

	return 0;
}

/*
 * This function is to initialize all DPM state tables
 * for SMU based on the dependency table.
 * Dynamic state patching function will then trim these
 * state tables to the allowed range based
 * on the power policy or external client requests,
 * such as UVD request, etc.
 */
static int vega10_setup_default_dpm_tables(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct vega10_single_dpm_table *dpm_table;
	uint32_t i;

	struct phm_ppt_v1_clock_voltage_dependency_table *dep_soc_table =
			table_info->vdd_dep_on_socclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_gfx_table =
			table_info->vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_mclk_table =
			table_info->vdd_dep_on_mclk;
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *dep_mm_table =
			table_info->mm_dep_table;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_dcef_table =
			table_info->vdd_dep_on_dcefclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_pix_table =
			table_info->vdd_dep_on_pixclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_disp_table =
			table_info->vdd_dep_on_dispclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_phy_table =
			table_info->vdd_dep_on_phyclk;

	PP_ASSERT_WITH_CODE(dep_soc_table,
			"SOCCLK dependency table is missing. This table is mandatory",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(dep_soc_table->count >= 1,
			"SOCCLK dependency table is empty. This table is mandatory",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(dep_gfx_table,
			"GFXCLK dependency table is missing. This table is mandatory",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(dep_gfx_table->count >= 1,
			"GFXCLK dependency table is empty. This table is mandatory",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(dep_mclk_table,
			"MCLK dependency table is missing. This table is mandatory",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(dep_mclk_table->count >= 1,
			"MCLK dependency table has to have is missing. This table is mandatory",
			return -EINVAL);

	/* Initialize Sclk DPM table based on allow Sclk values */
	data->dpm_table.soc_table.count = 0;
	data->dpm_table.gfx_table.count = 0;
	data->dpm_table.dcef_table.count = 0;

	dpm_table = &(data->dpm_table.soc_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_soc_table);

	vega10_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.gfx_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_gfx_table);
	vega10_init_dpm_state(&(dpm_table->dpm_state));

	/* Initialize Mclk DPM table based on allow Mclk values */
	data->dpm_table.mem_table.count = 0;
	dpm_table = &(data->dpm_table.mem_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_mclk_table);
	vega10_init_dpm_state(&(dpm_table->dpm_state));

	data->dpm_table.eclk_table.count = 0;
	dpm_table = &(data->dpm_table.eclk_table);
	for (i = 0; i < dep_mm_table->count; i++) {
		if (i == 0 || dpm_table->dpm_levels
				[dpm_table->count - 1].value !=
						dep_mm_table->entries[i].eclk) {
			dpm_table->dpm_levels[dpm_table->count].value =
					dep_mm_table->entries[i].eclk;
			dpm_table->dpm_levels[dpm_table->count].enabled =
					(i == 0) ? true : false;
			dpm_table->count++;
		}
	}
	vega10_init_dpm_state(&(dpm_table->dpm_state));

	data->dpm_table.vclk_table.count = 0;
	data->dpm_table.dclk_table.count = 0;
	dpm_table = &(data->dpm_table.vclk_table);
	for (i = 0; i < dep_mm_table->count; i++) {
		if (i == 0 || dpm_table->dpm_levels
				[dpm_table->count - 1].value !=
						dep_mm_table->entries[i].vclk) {
			dpm_table->dpm_levels[dpm_table->count].value =
					dep_mm_table->entries[i].vclk;
			dpm_table->dpm_levels[dpm_table->count].enabled =
					(i == 0) ? true : false;
			dpm_table->count++;
		}
	}
	vega10_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.dclk_table);
	for (i = 0; i < dep_mm_table->count; i++) {
		if (i == 0 || dpm_table->dpm_levels
				[dpm_table->count - 1].value !=
						dep_mm_table->entries[i].dclk) {
			dpm_table->dpm_levels[dpm_table->count].value =
					dep_mm_table->entries[i].dclk;
			dpm_table->dpm_levels[dpm_table->count].enabled =
					(i == 0) ? true : false;
			dpm_table->count++;
		}
	}
	vega10_init_dpm_state(&(dpm_table->dpm_state));

	/* Assume there is no headless Vega10 for now */
	dpm_table = &(data->dpm_table.dcef_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_dcef_table);

	vega10_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.pixel_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_pix_table);

	vega10_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.display_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_disp_table);

	vega10_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.phy_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_phy_table);

	vega10_init_dpm_state(&(dpm_table->dpm_state));

	vega10_setup_default_pcie_table(hwmgr);

	/* save a copy of the default DPM table */
	memcpy(&(data->golden_dpm_table), &(data->dpm_table),
			sizeof(struct vega10_dpm_table));

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ODNinACSupport) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ODNinDCSupport)) {
		data->odn_dpm_table.odn_core_clock_dpm_levels.
		number_of_performance_levels = data->dpm_table.gfx_table.count;
		for (i = 0; i < data->dpm_table.gfx_table.count; i++) {
			data->odn_dpm_table.odn_core_clock_dpm_levels.
			performance_level_entries[i].clock =
					data->dpm_table.gfx_table.dpm_levels[i].value;
			data->odn_dpm_table.odn_core_clock_dpm_levels.
			performance_level_entries[i].enabled = true;
		}

		data->odn_dpm_table.vdd_dependency_on_sclk.count =
				dep_gfx_table->count;
		for (i = 0; i < dep_gfx_table->count; i++) {
			data->odn_dpm_table.vdd_dependency_on_sclk.entries[i].clk =
					dep_gfx_table->entries[i].clk;
			data->odn_dpm_table.vdd_dependency_on_sclk.entries[i].vddInd =
					dep_gfx_table->entries[i].vddInd;
			data->odn_dpm_table.vdd_dependency_on_sclk.entries[i].cks_enable =
					dep_gfx_table->entries[i].cks_enable;
			data->odn_dpm_table.vdd_dependency_on_sclk.entries[i].cks_voffset =
					dep_gfx_table->entries[i].cks_voffset;
		}

		data->odn_dpm_table.odn_memory_clock_dpm_levels.
		number_of_performance_levels = data->dpm_table.mem_table.count;
		for (i = 0; i < data->dpm_table.mem_table.count; i++) {
			data->odn_dpm_table.odn_memory_clock_dpm_levels.
			performance_level_entries[i].clock =
					data->dpm_table.mem_table.dpm_levels[i].value;
			data->odn_dpm_table.odn_memory_clock_dpm_levels.
			performance_level_entries[i].enabled = true;
		}

		data->odn_dpm_table.vdd_dependency_on_mclk.count = dep_mclk_table->count;
		for (i = 0; i < dep_mclk_table->count; i++) {
			data->odn_dpm_table.vdd_dependency_on_mclk.entries[i].clk =
					dep_mclk_table->entries[i].clk;
			data->odn_dpm_table.vdd_dependency_on_mclk.entries[i].vddInd =
					dep_mclk_table->entries[i].vddInd;
			data->odn_dpm_table.vdd_dependency_on_mclk.entries[i].vddci =
					dep_mclk_table->entries[i].vddci;
		}
	}

	return 0;
}

/*
 * @fn vega10_populate_ulv_state
 * @brief Function to provide parameters for Utral Low Voltage state to SMC.
 *
 * @param    hwmgr - the address of the hardware manager.
 * @return   Always 0.
 */
static int vega10_populate_ulv_state(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);

	data->smc_state_table.pp_table.UlvOffsetVid =
			(uint8_t)(table_info->us_ulv_voltage_offset *
					VOLTAGE_VID_OFFSET_SCALE2 /
					VOLTAGE_VID_OFFSET_SCALE1);

	data->smc_state_table.pp_table.UlvSmnclkDid =
			(uint8_t)(table_info->us_ulv_smnclk_did);
	data->smc_state_table.pp_table.UlvMp1clkDid =
			(uint8_t)(table_info->us_ulv_mp1clk_did);
	data->smc_state_table.pp_table.UlvGfxclkBypass =
			(uint8_t)(table_info->us_ulv_gfxclk_bypass);
	data->smc_state_table.pp_table.UlvPhaseSheddingPsi0 =
			(uint8_t)(data->vddc_voltage_table.psi0_enable);
	data->smc_state_table.pp_table.UlvPhaseSheddingPsi1 =
			(uint8_t)(data->vddc_voltage_table.psi1_enable);

	return 0;
}

static int vega10_populate_single_lclk_level(struct pp_hwmgr *hwmgr,
		uint32_t lclock, uint8_t *curr_lclk_did)
{
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;

	PP_ASSERT_WITH_CODE(!pp_atomfwctrl_get_gpu_pll_dividers_vega10(
			hwmgr,
			COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
			lclock, &dividers),
			"Failed to get LCLK clock settings from VBIOS!",
			return -1);

	*curr_lclk_did = dividers.ulDid;

	return 0;
}

static int vega10_populate_smc_link_levels(struct pp_hwmgr *hwmgr)
{
	int result = -1;
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega10_pcie_table *pcie_table =
			&(data->dpm_table.pcie_table);
	uint32_t i, j;

	for (i = 0; i < pcie_table->count; i++) {
		pp_table->PcieGenSpeed[i] = pcie_table->pcie_gen[i];
		pp_table->PcieLaneCount[i] = pcie_table->pcie_lane[i];

		result = vega10_populate_single_lclk_level(hwmgr,
				pcie_table->lclk[i], &(pp_table->LclkDid[i]));
		if (result) {
			pr_info("Populate LClock Level %d Failed!\n", i);
			return result;
		}
	}

	j = i - 1;
	while (i < NUM_LINK_LEVELS) {
		pp_table->PcieGenSpeed[i] = pcie_table->pcie_gen[j];
		pp_table->PcieLaneCount[i] = pcie_table->pcie_lane[j];

		result = vega10_populate_single_lclk_level(hwmgr,
				pcie_table->lclk[j], &(pp_table->LclkDid[i]));
		if (result) {
			pr_info("Populate LClock Level %d Failed!\n", i);
			return result;
		}
		i++;
	}

	return result;
}

/**
* Populates single SMC GFXSCLK structure using the provided engine clock
*
* @param    hwmgr      the address of the hardware manager
* @param    gfx_clock  the GFX clock to use to populate the structure.
* @param    current_gfxclk_level  location in PPTable for the SMC GFXCLK structure.
*/

static int vega10_populate_single_gfx_level(struct pp_hwmgr *hwmgr,
		uint32_t gfx_clock, PllSetting_t *current_gfxclk_level)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_on_sclk =
			table_info->vdd_dep_on_sclk;
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;
	uint32_t i;

	if (data->apply_overdrive_next_settings_mask &
			DPMTABLE_OD_UPDATE_VDDC)
		dep_on_sclk = (struct phm_ppt_v1_clock_voltage_dependency_table *)
						&(data->odn_dpm_table.vdd_dependency_on_sclk);

	PP_ASSERT_WITH_CODE(dep_on_sclk,
			"Invalid SOC_VDD-GFX_CLK Dependency Table!",
			return -EINVAL);

	for (i = 0; i < dep_on_sclk->count; i++) {
		if (dep_on_sclk->entries[i].clk == gfx_clock)
			break;
	}

	PP_ASSERT_WITH_CODE(dep_on_sclk->count > i,
			"Cannot find gfx_clk in SOC_VDD-GFX_CLK!",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(!pp_atomfwctrl_get_gpu_pll_dividers_vega10(hwmgr,
			COMPUTE_GPUCLK_INPUT_FLAG_GFXCLK,
			gfx_clock, &dividers),
			"Failed to get GFX Clock settings from VBIOS!",
			return -EINVAL);

	/* Feedback Multiplier: bit 0:8 int, bit 15:12 post_div, bit 31:16 frac */
	current_gfxclk_level->FbMult =
			cpu_to_le32(dividers.ulPll_fb_mult);
	/* Spread FB Multiplier bit: bit 0:8 int, bit 31:16 frac */
	current_gfxclk_level->SsOn = dividers.ucPll_ss_enable;
	current_gfxclk_level->SsFbMult =
			cpu_to_le32(dividers.ulPll_ss_fbsmult);
	current_gfxclk_level->SsSlewFrac =
			cpu_to_le16(dividers.usPll_ss_slew_frac);
	current_gfxclk_level->Did = (uint8_t)(dividers.ulDid);

	return 0;
}

/**
 * @brief Populates single SMC SOCCLK structure using the provided clock.
 *
 * @param    hwmgr - the address of the hardware manager.
 * @param    soc_clock - the SOC clock to use to populate the structure.
 * @param    current_socclk_level - location in PPTable for the SMC SOCCLK structure.
 * @return   0 on success..
 */
static int vega10_populate_single_soc_level(struct pp_hwmgr *hwmgr,
		uint32_t soc_clock, uint8_t *current_soc_did,
		uint8_t *current_vol_index)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_on_soc =
			table_info->vdd_dep_on_socclk;
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;
	uint32_t i;

	PP_ASSERT_WITH_CODE(dep_on_soc,
			"Invalid SOC_VDD-SOC_CLK Dependency Table!",
			return -EINVAL);
	for (i = 0; i < dep_on_soc->count; i++) {
		if (dep_on_soc->entries[i].clk == soc_clock)
			break;
	}
	PP_ASSERT_WITH_CODE(dep_on_soc->count > i,
			"Cannot find SOC_CLK in SOC_VDD-SOC_CLK Dependency Table",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(!pp_atomfwctrl_get_gpu_pll_dividers_vega10(hwmgr,
			COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
			soc_clock, &dividers),
			"Failed to get SOC Clock settings from VBIOS!",
			return -EINVAL);

	*current_soc_did = (uint8_t)dividers.ulDid;
	*current_vol_index = (uint8_t)(dep_on_soc->entries[i].vddInd);

	return 0;
}

uint16_t vega10_locate_vddc_given_clock(struct pp_hwmgr *hwmgr,
		uint32_t clk,
		struct phm_ppt_v1_clock_voltage_dependency_table *dep_table)
{
	uint16_t i;

	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].clk == clk)
			return dep_table->entries[i].vddc;
	}

	pr_info("[LocateVddcGivenClock] Cannot locate SOC Vddc for this clock!");
	return 0;
}

/**
* Populates all SMC SCLK levels' structure based on the trimmed allowed dpm engine clock states
*
* @param    hwmgr      the address of the hardware manager
*/
static int vega10_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_socclk;
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega10_single_dpm_table *dpm_table = &(data->dpm_table.gfx_table);
	int result = 0;
	uint32_t i, j;

	for (i = 0; i < dpm_table->count; i++) {
		result = vega10_populate_single_gfx_level(hwmgr,
				dpm_table->dpm_levels[i].value,
				&(pp_table->GfxclkLevel[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_GFXCLK_DPM_LEVELS) {
		result = vega10_populate_single_gfx_level(hwmgr,
				dpm_table->dpm_levels[j].value,
				&(pp_table->GfxclkLevel[i]));
		if (result)
			return result;
		i++;
	}

	pp_table->GfxclkSlewRate =
			cpu_to_le16(table_info->us_gfxclk_slew_rate);

	dpm_table = &(data->dpm_table.soc_table);
	for (i = 0; i < dpm_table->count; i++) {
		pp_table->SocVid[i] =
				(uint8_t)convert_to_vid(
				vega10_locate_vddc_given_clock(hwmgr,
						dpm_table->dpm_levels[i].value,
						dep_table));
		result = vega10_populate_single_soc_level(hwmgr,
				dpm_table->dpm_levels[i].value,
				&(pp_table->SocclkDid[i]),
				&(pp_table->SocDpmVoltageIndex[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_SOCCLK_DPM_LEVELS) {
		pp_table->SocVid[i] = pp_table->SocVid[j];
		result = vega10_populate_single_soc_level(hwmgr,
				dpm_table->dpm_levels[j].value,
				&(pp_table->SocclkDid[i]),
				&(pp_table->SocDpmVoltageIndex[i]));
		if (result)
			return result;
		i++;
	}

	return result;
}

/**
 * @brief Populates single SMC GFXCLK structure using the provided clock.
 *
 * @param    hwmgr - the address of the hardware manager.
 * @param    mem_clock - the memory clock to use to populate the structure.
 * @return   0 on success..
 */
static int vega10_populate_single_memory_level(struct pp_hwmgr *hwmgr,
		uint32_t mem_clock, uint8_t *current_mem_vid,
		PllSetting_t *current_memclk_level, uint8_t *current_mem_soc_vind)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_on_mclk =
			table_info->vdd_dep_on_mclk;
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;
	uint32_t i;

	if (data->apply_overdrive_next_settings_mask &
			DPMTABLE_OD_UPDATE_VDDC)
		dep_on_mclk = (struct phm_ppt_v1_clock_voltage_dependency_table *)
					&data->odn_dpm_table.vdd_dependency_on_mclk;

	PP_ASSERT_WITH_CODE(dep_on_mclk,
			"Invalid SOC_VDD-UCLK Dependency Table!",
			return -EINVAL);

	for (i = 0; i < dep_on_mclk->count; i++) {
		if (dep_on_mclk->entries[i].clk == mem_clock)
			break;
	}

	PP_ASSERT_WITH_CODE(dep_on_mclk->count > i,
			"Cannot find UCLK in SOC_VDD-UCLK Dependency Table!",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(!pp_atomfwctrl_get_gpu_pll_dividers_vega10(
			hwmgr, COMPUTE_GPUCLK_INPUT_FLAG_UCLK, mem_clock, &dividers),
			"Failed to get UCLK settings from VBIOS!",
			return -1);

	*current_mem_vid =
			(uint8_t)(convert_to_vid(dep_on_mclk->entries[i].mvdd));
	*current_mem_soc_vind =
			(uint8_t)(dep_on_mclk->entries[i].vddInd);
	current_memclk_level->FbMult = cpu_to_le32(dividers.ulPll_fb_mult);
	current_memclk_level->Did = (uint8_t)(dividers.ulDid);

	PP_ASSERT_WITH_CODE(current_memclk_level->Did >= 1,
			"Invalid Divider ID!",
			return -EINVAL);

	return 0;
}

/**
 * @brief Populates all SMC MCLK levels' structure based on the trimmed allowed dpm memory clock states.
 *
 * @param    pHwMgr - the address of the hardware manager.
 * @return   PP_Result_OK on success.
 */
static int vega10_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega10_single_dpm_table *dpm_table =
			&(data->dpm_table.mem_table);
	int result = 0;
	uint32_t i, j, reg, mem_channels;

	for (i = 0; i < dpm_table->count; i++) {
		result = vega10_populate_single_memory_level(hwmgr,
				dpm_table->dpm_levels[i].value,
				&(pp_table->MemVid[i]),
				&(pp_table->UclkLevel[i]),
				&(pp_table->MemSocVoltageIndex[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_UCLK_DPM_LEVELS) {
		result = vega10_populate_single_memory_level(hwmgr,
				dpm_table->dpm_levels[j].value,
				&(pp_table->MemVid[i]),
				&(pp_table->UclkLevel[i]),
				&(pp_table->MemSocVoltageIndex[i]));
		if (result)
			return result;
		i++;
	}

	reg = soc15_get_register_offset(DF_HWID, 0,
			mmDF_CS_AON0_DramBaseAddress0_BASE_IDX,
			mmDF_CS_AON0_DramBaseAddress0);
	mem_channels = (cgs_read_register(hwmgr->device, reg) &
			DF_CS_AON0_DramBaseAddress0__IntLvNumChan_MASK) >>
			DF_CS_AON0_DramBaseAddress0__IntLvNumChan__SHIFT;
	pp_table->NumMemoryChannels = cpu_to_le16(mem_channels);
	pp_table->MemoryChannelWidth =
			cpu_to_le16(HBM_MEMORY_CHANNEL_WIDTH *
					channel_number[mem_channels]);

	pp_table->LowestUclkReservedForUlv =
			(uint8_t)(data->lowest_uclk_reserved_for_ulv);

	return result;
}

static int vega10_populate_single_display_type(struct pp_hwmgr *hwmgr,
		DSPCLK_e disp_clock)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)
			(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table;
	uint32_t i;
	uint16_t clk = 0, vddc = 0;
	uint8_t vid = 0;

	switch (disp_clock) {
	case DSPCLK_DCEFCLK:
		dep_table = table_info->vdd_dep_on_dcefclk;
		break;
	case DSPCLK_DISPCLK:
		dep_table = table_info->vdd_dep_on_dispclk;
		break;
	case DSPCLK_PIXCLK:
		dep_table = table_info->vdd_dep_on_pixclk;
		break;
	case DSPCLK_PHYCLK:
		dep_table = table_info->vdd_dep_on_phyclk;
		break;
	default:
		return -1;
	}

	PP_ASSERT_WITH_CODE(dep_table->count <= NUM_DSPCLK_LEVELS,
			"Number Of Entries Exceeded maximum!",
			return -1);

	for (i = 0; i < dep_table->count; i++) {
		clk = (uint16_t)(dep_table->entries[i].clk / 100);
		vddc = table_info->vddc_lookup_table->
				entries[dep_table->entries[i].vddInd].us_vdd;
		vid = (uint8_t)convert_to_vid(vddc);
		pp_table->DisplayClockTable[disp_clock][i].Freq =
				cpu_to_le16(clk);
		pp_table->DisplayClockTable[disp_clock][i].Vid =
				cpu_to_le16(vid);
	}

	while (i < NUM_DSPCLK_LEVELS) {
		pp_table->DisplayClockTable[disp_clock][i].Freq =
				cpu_to_le16(clk);
		pp_table->DisplayClockTable[disp_clock][i].Vid =
				cpu_to_le16(vid);
		i++;
	}

	return 0;
}

static int vega10_populate_all_display_clock_levels(struct pp_hwmgr *hwmgr)
{
	uint32_t i;

	for (i = 0; i < DSPCLK_COUNT; i++) {
		PP_ASSERT_WITH_CODE(!vega10_populate_single_display_type(hwmgr, i),
				"Failed to populate Clock in DisplayClockTable!",
				return -1);
	}

	return 0;
}

static int vega10_populate_single_eclock_level(struct pp_hwmgr *hwmgr,
		uint32_t eclock, uint8_t *current_eclk_did,
		uint8_t *current_soc_vol)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *dep_table =
			table_info->mm_dep_table;
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;
	uint32_t i;

	PP_ASSERT_WITH_CODE(!pp_atomfwctrl_get_gpu_pll_dividers_vega10(hwmgr,
			COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
			eclock, &dividers),
			"Failed to get ECLK clock settings from VBIOS!",
			return -1);

	*current_eclk_did = (uint8_t)dividers.ulDid;

	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].eclk == eclock)
			*current_soc_vol = dep_table->entries[i].vddcInd;
	}

	return 0;
}

static int vega10_populate_smc_vce_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega10_single_dpm_table *dpm_table = &(data->dpm_table.eclk_table);
	int result = -EINVAL;
	uint32_t i, j;

	for (i = 0; i < dpm_table->count; i++) {
		result = vega10_populate_single_eclock_level(hwmgr,
				dpm_table->dpm_levels[i].value,
				&(pp_table->EclkDid[i]),
				&(pp_table->VceDpmVoltageIndex[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_VCE_DPM_LEVELS) {
		result = vega10_populate_single_eclock_level(hwmgr,
				dpm_table->dpm_levels[j].value,
				&(pp_table->EclkDid[i]),
				&(pp_table->VceDpmVoltageIndex[i]));
		if (result)
			return result;
		i++;
	}

	return result;
}

static int vega10_populate_single_vclock_level(struct pp_hwmgr *hwmgr,
		uint32_t vclock, uint8_t *current_vclk_did)
{
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;

	PP_ASSERT_WITH_CODE(!pp_atomfwctrl_get_gpu_pll_dividers_vega10(hwmgr,
			COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
			vclock, &dividers),
			"Failed to get VCLK clock settings from VBIOS!",
			return -EINVAL);

	*current_vclk_did = (uint8_t)dividers.ulDid;

	return 0;
}

static int vega10_populate_single_dclock_level(struct pp_hwmgr *hwmgr,
		uint32_t dclock, uint8_t *current_dclk_did)
{
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;

	PP_ASSERT_WITH_CODE(!pp_atomfwctrl_get_gpu_pll_dividers_vega10(hwmgr,
			COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK,
			dclock, &dividers),
			"Failed to get DCLK clock settings from VBIOS!",
			return -EINVAL);

	*current_dclk_did = (uint8_t)dividers.ulDid;

	return 0;
}

static int vega10_populate_smc_uvd_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega10_single_dpm_table *vclk_dpm_table =
			&(data->dpm_table.vclk_table);
	struct vega10_single_dpm_table *dclk_dpm_table =
			&(data->dpm_table.dclk_table);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *dep_table =
			table_info->mm_dep_table;
	int result = -EINVAL;
	uint32_t i, j;

	for (i = 0; i < vclk_dpm_table->count; i++) {
		result = vega10_populate_single_vclock_level(hwmgr,
				vclk_dpm_table->dpm_levels[i].value,
				&(pp_table->VclkDid[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_UVD_DPM_LEVELS) {
		result = vega10_populate_single_vclock_level(hwmgr,
				vclk_dpm_table->dpm_levels[j].value,
				&(pp_table->VclkDid[i]));
		if (result)
			return result;
		i++;
	}

	for (i = 0; i < dclk_dpm_table->count; i++) {
		result = vega10_populate_single_dclock_level(hwmgr,
				dclk_dpm_table->dpm_levels[i].value,
				&(pp_table->DclkDid[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_UVD_DPM_LEVELS) {
		result = vega10_populate_single_dclock_level(hwmgr,
				dclk_dpm_table->dpm_levels[j].value,
				&(pp_table->DclkDid[i]));
		if (result)
			return result;
		i++;
	}

	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].vclk ==
				vclk_dpm_table->dpm_levels[i].value &&
			dep_table->entries[i].dclk ==
				dclk_dpm_table->dpm_levels[i].value)
			pp_table->UvdDpmVoltageIndex[i] =
					dep_table->entries[i].vddcInd;
		else
			return -1;
	}

	j = i - 1;
	while (i < NUM_UVD_DPM_LEVELS) {
		pp_table->UvdDpmVoltageIndex[i] = dep_table->entries[j].vddcInd;
		i++;
	}

	return 0;
}

static int vega10_populate_clock_stretcher_table(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_sclk;
	uint32_t i;

	for (i = 0; dep_table->count; i++) {
		pp_table->CksEnable[i] = dep_table->entries[i].cks_enable;
		pp_table->CksVidOffset[i] = convert_to_vid(
				dep_table->entries[i].cks_voffset);
	}

	return 0;
}

static int vega10_populate_avfs_parameters(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_sclk;
	struct pp_atomfwctrl_avfs_parameters avfs_params = {0};
	int result = 0;
	uint32_t i;

	pp_table->MinVoltageVid = (uint8_t)0xff;
	pp_table->MaxVoltageVid = (uint8_t)0;

	if (data->smu_features[GNLD_AVFS].supported) {
		result = pp_atomfwctrl_get_avfs_information(hwmgr, &avfs_params);
		if (!result) {
			pp_table->MinVoltageVid = (uint8_t)
					convert_to_vid((uint16_t)(avfs_params.ulMaxVddc));
			pp_table->MaxVoltageVid = (uint8_t)
					convert_to_vid((uint16_t)(avfs_params.ulMinVddc));
			pp_table->BtcGbVdroopTableCksOn.a0 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksonA0);
			pp_table->BtcGbVdroopTableCksOn.a1 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksonA1);
			pp_table->BtcGbVdroopTableCksOn.a2 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksonA2);

			pp_table->BtcGbVdroopTableCksOff.a0 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksoffA0);
			pp_table->BtcGbVdroopTableCksOff.a1 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksoffA1);
			pp_table->BtcGbVdroopTableCksOff.a2 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksoffA2);

			pp_table->AvfsGbCksOn.m1 =
					cpu_to_le32(avfs_params.ulGbFuseTableCksonM1);
			pp_table->AvfsGbCksOn.m2 =
					cpu_to_le16(avfs_params.usGbFuseTableCksonM2);
			pp_table->AvfsGbCksOn.b =
					cpu_to_le32(avfs_params.ulGbFuseTableCksonB);
			pp_table->AvfsGbCksOn.m1_shift = 24;
			pp_table->AvfsGbCksOn.m2_shift = 12;

			pp_table->AvfsGbCksOff.m1 =
					cpu_to_le32(avfs_params.ulGbFuseTableCksoffM1);
			pp_table->AvfsGbCksOff.m2 =
					cpu_to_le16(avfs_params.usGbFuseTableCksoffM2);
			pp_table->AvfsGbCksOff.b =
					cpu_to_le32(avfs_params.ulGbFuseTableCksoffB);
			pp_table->AvfsGbCksOff.m1_shift = 24;
			pp_table->AvfsGbCksOff.m2_shift = 12;

			pp_table->AConstant[0] =
					cpu_to_le32(avfs_params.ulMeanNsigmaAcontant0);
			pp_table->AConstant[1] =
					cpu_to_le32(avfs_params.ulMeanNsigmaAcontant1);
			pp_table->AConstant[2] =
					cpu_to_le32(avfs_params.ulMeanNsigmaAcontant2);
			pp_table->DC_tol_sigma =
					cpu_to_le16(avfs_params.usMeanNsigmaDcTolSigma);
			pp_table->Platform_mean =
					cpu_to_le16(avfs_params.usMeanNsigmaPlatformMean);
			pp_table->PSM_Age_CompFactor =
					cpu_to_le16(avfs_params.usPsmAgeComfactor);
			pp_table->Platform_sigma =
					cpu_to_le16(avfs_params.usMeanNsigmaDcTolSigma);

			for (i = 0; i < dep_table->count; i++)
				pp_table->StaticVoltageOffsetVid[i] = (uint8_t)
						(dep_table->entries[i].sclk_offset *
								VOLTAGE_VID_OFFSET_SCALE2 /
								VOLTAGE_VID_OFFSET_SCALE1);

			pp_table->OverrideBtcGbCksOn =
					avfs_params.ucEnableGbVdroopTableCkson;
			pp_table->OverrideAvfsGbCksOn =
					avfs_params.ucEnableGbFuseTableCkson;

			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->disp_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->disp_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m1 =
						(int32_t)data->disp_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m2 =
						(int16_t)data->disp_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].b =
						(int32_t)data->disp_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m1 =
						(int32_t)avfs_params.ulDispclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m2 =
						(int16_t)avfs_params.usDispclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].b =
						(int32_t)avfs_params.ulDispclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m2_shift = 12;

			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->dcef_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->dcef_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m1 =
						(int32_t)data->dcef_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m2 =
						(int16_t)data->dcef_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].b =
						(int32_t)data->dcef_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m1 =
						(int32_t)avfs_params.ulDcefclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m2 =
						(int16_t)avfs_params.usDcefclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].b =
						(int32_t)avfs_params.ulDcefclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m2_shift = 12;

			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->pixel_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->pixel_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m1 =
						(int32_t)data->pixel_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m2 =
						(int16_t)data->pixel_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].b =
						(int32_t)data->pixel_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m1 =
						(int32_t)avfs_params.ulPixelclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m2 =
						(int16_t)avfs_params.usPixelclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].b =
						(int32_t)avfs_params.ulPixelclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m2_shift = 12;

			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->phy_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->phy_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m1 =
						(int32_t)data->phy_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m2 =
						(int16_t)data->phy_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].b =
						(int32_t)data->phy_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m1 =
						(int32_t)avfs_params.ulPhyclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m2 =
						(int16_t)avfs_params.usPhyclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].b =
						(int32_t)avfs_params.ulPhyclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m2_shift = 12;
		} else {
			data->smu_features[GNLD_AVFS].supported = false;
		}
	}

	return 0;
}

static int vega10_populate_gpio_parameters(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct pp_atomfwctrl_gpio_parameters gpio_params = {0};
	int result;

	result = pp_atomfwctrl_get_gpio_information(hwmgr, &gpio_params);
	if (!result) {
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_RegulatorHot) &&
				(data->registry_data.regulator_hot_gpio_support)) {
			pp_table->VR0HotGpio = gpio_params.ucVR0HotGpio;
			pp_table->VR0HotPolarity = gpio_params.ucVR0HotPolarity;
			pp_table->VR1HotGpio = gpio_params.ucVR1HotGpio;
			pp_table->VR1HotPolarity = gpio_params.ucVR1HotPolarity;
		} else {
			pp_table->VR0HotGpio = 0;
			pp_table->VR0HotPolarity = 0;
			pp_table->VR1HotGpio = 0;
			pp_table->VR1HotPolarity = 0;
		}

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition) &&
				(data->registry_data.ac_dc_switch_gpio_support)) {
			pp_table->AcDcGpio = gpio_params.ucAcDcGpio;
			pp_table->AcDcPolarity = gpio_params.ucAcDcPolarity;
		} else {
			pp_table->AcDcGpio = 0;
			pp_table->AcDcPolarity = 0;
		}
	}

	return result;
}

static int vega10_avfs_enable(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_AVFS].supported) {
		if (enable) {
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
					true,
					data->smu_features[GNLD_AVFS].smu_feature_bitmap),
					"[avfs_control] Attempt to Enable AVFS feature Failed!",
					return -1);
			data->smu_features[GNLD_AVFS].enabled = true;
		} else {
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
					false,
					data->smu_features[GNLD_AVFS].smu_feature_id),
					"[avfs_control] Attempt to Disable AVFS feature Failed!",
					return -1);
			data->smu_features[GNLD_AVFS].enabled = false;
		}
	}

	return 0;
}

/**
* Initializes the SMC table and uploads it
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput  the pointer to input data (PowerState)
* @return   always 0
*/
static int vega10_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct pp_atomfwctrl_voltage_table voltage_table;

	result = vega10_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to setup default DPM tables!",
			return result);

	pp_atomfwctrl_get_voltage_table_v4(hwmgr, VOLTAGE_TYPE_VDDC,
			VOLTAGE_OBJ_SVID2,  &voltage_table);
	pp_table->MaxVidStep = voltage_table.max_vid_step;

	pp_table->GfxDpmVoltageMode =
			(uint8_t)(table_info->uc_gfx_dpm_voltage_mode);
	pp_table->SocDpmVoltageMode =
			(uint8_t)(table_info->uc_soc_dpm_voltage_mode);
	pp_table->UclkDpmVoltageMode =
			(uint8_t)(table_info->uc_uclk_dpm_voltage_mode);
	pp_table->UvdDpmVoltageMode =
			(uint8_t)(table_info->uc_uvd_dpm_voltage_mode);
	pp_table->VceDpmVoltageMode =
			(uint8_t)(table_info->uc_vce_dpm_voltage_mode);
	pp_table->Mp0DpmVoltageMode =
			(uint8_t)(table_info->uc_mp0_dpm_voltage_mode);
	pp_table->DisplayDpmVoltageMode =
			(uint8_t)(table_info->uc_dcef_dpm_voltage_mode);

	if (data->registry_data.ulv_support &&
			table_info->us_ulv_voltage_offset) {
		result = vega10_populate_ulv_state(hwmgr);
		PP_ASSERT_WITH_CODE(!result,
				"Failed to initialize ULV state!",
				return result);
	}

	result = vega10_populate_smc_link_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize Link Level!",
			return result);

	result = vega10_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize Graphics Level!",
			return result);

	result = vega10_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize Memory Level!",
			return result);

	result = vega10_populate_all_display_clock_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize Display Level!",
			return result);

	result = vega10_populate_smc_vce_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize VCE Level!",
			return result);

	result = vega10_populate_smc_uvd_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize UVD Level!",
			return result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ClockStretcher)) {
		result = vega10_populate_clock_stretcher_table(hwmgr);
		PP_ASSERT_WITH_CODE(!result,
				"Failed to populate Clock Stretcher Table!",
				return result);
	}

	result = vega10_populate_avfs_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize AVFS Parameters!",
			return result);

	result = vega10_populate_gpio_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize GPIO Parameters!",
			return result);

	pp_table->GfxclkAverageAlpha = (uint8_t)
			(data->gfxclk_average_alpha);
	pp_table->SocclkAverageAlpha = (uint8_t)
			(data->socclk_average_alpha);
	pp_table->UclkAverageAlpha = (uint8_t)
			(data->uclk_average_alpha);
	pp_table->GfxActivityAverageAlpha = (uint8_t)
			(data->gfx_activity_average_alpha);

	result = vega10_copy_table_to_smc(hwmgr->smumgr,
			(uint8_t *)pp_table, PPTABLE);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload PPtable!", return result);

	if (data->smu_features[GNLD_AVFS].supported) {
		uint32_t features_enabled;
		result = vega10_get_smc_features(hwmgr->smumgr, &features_enabled);
		PP_ASSERT_WITH_CODE(!result,
				"Failed to Retrieve Enabled Features!",
				return result);
		if (!(features_enabled & (1 << FEATURE_AVFS_BIT))) {
			result = vega10_perform_btc(hwmgr->smumgr);
			PP_ASSERT_WITH_CODE(!result,
					"Failed to Perform BTC!",
					return result);
			result = vega10_avfs_enable(hwmgr, true);
			PP_ASSERT_WITH_CODE(!result,
					"Attempt to enable AVFS feature Failed!",
					return result);
			result = vega10_save_vft_table(hwmgr->smumgr,
					(uint8_t *)&(data->smc_state_table.avfs_table));
			PP_ASSERT_WITH_CODE(!result,
					"Attempt to save VFT table Failed!",
					return result);
		} else {
			data->smu_features[GNLD_AVFS].enabled = true;
			result = vega10_restore_vft_table(hwmgr->smumgr,
					(uint8_t *)&(data->smc_state_table.avfs_table));
			PP_ASSERT_WITH_CODE(!result,
					"Attempt to restore VFT table Failed!",
					return result;);
		}
	}

	return 0;
}

static int vega10_enable_thermal_protection(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_THERMAL].supported) {
		if (data->smu_features[GNLD_THERMAL].enabled)
			pr_info("THERMAL Feature Already enabled!");

		PP_ASSERT_WITH_CODE(
				!vega10_enable_smc_features(hwmgr->smumgr,
				true,
				data->smu_features[GNLD_THERMAL].smu_feature_bitmap),
				"Enable THERMAL Feature Failed!",
				return -1);
		data->smu_features[GNLD_THERMAL].enabled = true;
	}

	return 0;
}

static int vega10_enable_vrhot_feature(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot)) {
		if (data->smu_features[GNLD_VR0HOT].supported) {
			PP_ASSERT_WITH_CODE(
					!vega10_enable_smc_features(hwmgr->smumgr,
					true,
					data->smu_features[GNLD_VR0HOT].smu_feature_bitmap),
					"Attempt to Enable VR0 Hot feature Failed!",
					return -1);
			data->smu_features[GNLD_VR0HOT].enabled = true;
		} else {
			if (data->smu_features[GNLD_VR1HOT].supported) {
				PP_ASSERT_WITH_CODE(
						!vega10_enable_smc_features(hwmgr->smumgr,
						true,
						data->smu_features[GNLD_VR1HOT].smu_feature_bitmap),
						"Attempt to Enable VR0 Hot feature Failed!",
						return -1);
				data->smu_features[GNLD_VR1HOT].enabled = true;
			}
		}
	}
	return 0;
}

static int vega10_enable_ulv(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (data->registry_data.ulv_support) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
				true, data->smu_features[GNLD_ULV].smu_feature_bitmap),
				"Enable ULV Feature Failed!",
				return -1);
		data->smu_features[GNLD_ULV].enabled = true;
	}

	return 0;
}

static int vega10_enable_deep_sleep_master_switch(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_DS_GFXCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
				true, data->smu_features[GNLD_DS_GFXCLK].smu_feature_bitmap),
				"Attempt to Enable DS_GFXCLK Feature Failed!",
				return -1);
		data->smu_features[GNLD_DS_GFXCLK].enabled = true;
	}

	if (data->smu_features[GNLD_DS_SOCCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
				true, data->smu_features[GNLD_DS_SOCCLK].smu_feature_bitmap),
				"Attempt to Enable DS_GFXCLK Feature Failed!",
				return -1);
		data->smu_features[GNLD_DS_SOCCLK].enabled = true;
	}

	if (data->smu_features[GNLD_DS_LCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
				true, data->smu_features[GNLD_DS_LCLK].smu_feature_bitmap),
				"Attempt to Enable DS_GFXCLK Feature Failed!",
				return -1);
		data->smu_features[GNLD_DS_LCLK].enabled = true;
	}

	return 0;
}

/**
 * @brief Tell SMC to enabled the supported DPMs.
 *
 * @param    hwmgr - the address of the powerplay hardware manager.
 * @Param    bitmap - bitmap for the features to enabled.
 * @return   0 on at least one DPM is successfully enabled.
 */
static int vega10_start_dpm(struct pp_hwmgr *hwmgr, uint32_t bitmap)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	uint32_t i, feature_mask = 0;

	for (i = 0; i < GNLD_DPM_MAX; i++) {
		if (data->smu_features[i].smu_feature_bitmap & bitmap) {
			if (data->smu_features[i].supported) {
				if (!data->smu_features[i].enabled) {
					feature_mask |= data->smu_features[i].
							smu_feature_bitmap;
					data->smu_features[i].enabled = true;
				}
			}
		}
	}

	if (vega10_enable_smc_features(hwmgr->smumgr,
			true, feature_mask)) {
		for (i = 0; i < GNLD_DPM_MAX; i++) {
			if (data->smu_features[i].smu_feature_bitmap &
					feature_mask)
				data->smu_features[i].enabled = false;
		}
	}

	if(data->smu_features[GNLD_LED_DISPLAY].supported == true){
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
				true, data->smu_features[GNLD_LED_DISPLAY].smu_feature_bitmap),
		"Attempt to Enable LED DPM feature Failed!", return -EINVAL);
		data->smu_features[GNLD_LED_DISPLAY].enabled = true;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_Falcon_QuickTransition)) {
		if (data->smu_features[GNLD_ACDC].supported) {
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
					true, data->smu_features[GNLD_ACDC].smu_feature_bitmap),
					"Attempt to Enable DS_GFXCLK Feature Failed!",
					return -1);
			data->smu_features[GNLD_ACDC].enabled = true;
		}
	}

	return 0;
}

static int vega10_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	int tmp_result, result = 0;

	tmp_result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_ConfigureTelemetry, data->config_telemetry);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to configure telemetry!",
			return tmp_result);

	vega10_set_tools_address(hwmgr->smumgr);

	smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_NumOfDisplays, 0);

	tmp_result = (!vega10_is_dpm_running(hwmgr)) ? 0 : -1;
	PP_ASSERT_WITH_CODE(!tmp_result,
			"DPM is already running right , skipping re-enablement!",
			return 0);

	tmp_result = vega10_construct_voltage_tables(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to contruct voltage tables!",
			result = tmp_result);

	tmp_result = vega10_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to initialize SMC table!",
			result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalController)) {
		tmp_result = vega10_enable_thermal_protection(hwmgr);
		PP_ASSERT_WITH_CODE(!tmp_result,
				"Failed to enable thermal protection!",
				result = tmp_result);
	}

	tmp_result = vega10_enable_vrhot_feature(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to enable VR hot feature!",
			result = tmp_result);

	tmp_result = vega10_enable_ulv(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to enable ULV!",
			result = tmp_result);

	tmp_result = vega10_enable_deep_sleep_master_switch(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to enable deep sleep master switch!",
			result = tmp_result);

	tmp_result = vega10_start_dpm(hwmgr, SMC_DPM_FEATURES);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to start DPM!", result = tmp_result);

	tmp_result = vega10_enable_power_containment(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to enable power containment!",
			result = tmp_result);

	tmp_result = vega10_power_control_set_level(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to power control set level!",
			result = tmp_result);

	return result;
}

static int vega10_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct vega10_power_state);
}

static int vega10_get_pp_table_entry_callback_func(struct pp_hwmgr *hwmgr,
		void *state, struct pp_power_state *power_state,
		void *pp_table, uint32_t classification_flag)
{
	struct vega10_power_state *vega10_power_state =
			cast_phw_vega10_power_state(&(power_state->hardware));
	struct vega10_performance_level *performance_level;
	ATOM_Vega10_State *state_entry = (ATOM_Vega10_State *)state;
	ATOM_Vega10_POWERPLAYTABLE *powerplay_table =
			(ATOM_Vega10_POWERPLAYTABLE *)pp_table;
	ATOM_Vega10_SOCCLK_Dependency_Table *socclk_dep_table =
			(ATOM_Vega10_SOCCLK_Dependency_Table *)
			(((unsigned long)powerplay_table) +
			le16_to_cpu(powerplay_table->usSocclkDependencyTableOffset));
	ATOM_Vega10_GFXCLK_Dependency_Table *gfxclk_dep_table =
			(ATOM_Vega10_GFXCLK_Dependency_Table *)
			(((unsigned long)powerplay_table) +
			le16_to_cpu(powerplay_table->usGfxclkDependencyTableOffset));
	ATOM_Vega10_MCLK_Dependency_Table *mclk_dep_table =
			(ATOM_Vega10_MCLK_Dependency_Table *)
			(((unsigned long)powerplay_table) +
			le16_to_cpu(powerplay_table->usMclkDependencyTableOffset));


	/* The following fields are not initialized here:
	 * id orderedList allStatesList
	 */
	power_state->classification.ui_label =
			(le16_to_cpu(state_entry->usClassification) &
			ATOM_PPLIB_CLASSIFICATION_UI_MASK) >>
			ATOM_PPLIB_CLASSIFICATION_UI_SHIFT;
	power_state->classification.flags = classification_flag;
	/* NOTE: There is a classification2 flag in BIOS
	 * that is not being used right now
	 */
	power_state->classification.temporary_state = false;
	power_state->classification.to_be_deleted = false;

	power_state->validation.disallowOnDC =
			((le32_to_cpu(state_entry->ulCapsAndSettings) &
					ATOM_Vega10_DISALLOW_ON_DC) != 0);

	power_state->display.disableFrameModulation = false;
	power_state->display.limitRefreshrate = false;
	power_state->display.enableVariBright =
			((le32_to_cpu(state_entry->ulCapsAndSettings) &
					ATOM_Vega10_ENABLE_VARIBRIGHT) != 0);

	power_state->validation.supportedPowerLevels = 0;
	power_state->uvd_clocks.VCLK = 0;
	power_state->uvd_clocks.DCLK = 0;
	power_state->temperatures.min = 0;
	power_state->temperatures.max = 0;

	performance_level = &(vega10_power_state->performance_levels
			[vega10_power_state->performance_level_count++]);

	PP_ASSERT_WITH_CODE(
			(vega10_power_state->performance_level_count <
					NUM_GFXCLK_DPM_LEVELS),
			"Performance levels exceeds SMC limit!",
			return -1);

	PP_ASSERT_WITH_CODE(
			(vega10_power_state->performance_level_count <=
					hwmgr->platform_descriptor.
					hardwareActivityPerformanceLevels),
			"Performance levels exceeds Driver limit!",
			return -1);

	/* Performance levels are arranged from low to high. */
	performance_level->soc_clock = socclk_dep_table->entries
			[state_entry->ucSocClockIndexLow].ulClk;
	performance_level->gfx_clock = gfxclk_dep_table->entries
			[state_entry->ucGfxClockIndexLow].ulClk;
	performance_level->mem_clock = mclk_dep_table->entries
			[state_entry->ucMemClockIndexLow].ulMemClk;

	performance_level = &(vega10_power_state->performance_levels
				[vega10_power_state->performance_level_count++]);

	performance_level->soc_clock = socclk_dep_table->entries
			[state_entry->ucSocClockIndexHigh].ulClk;
	performance_level->gfx_clock = gfxclk_dep_table->entries
			[state_entry->ucGfxClockIndexHigh].ulClk;
	performance_level->mem_clock = mclk_dep_table->entries
			[state_entry->ucMemClockIndexHigh].ulMemClk;
	return 0;
}

static int vega10_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		unsigned long entry_index, struct pp_power_state *state)
{
	int result;
	struct vega10_power_state *ps;

	state->hardware.magic = PhwVega10_Magic;

	ps = cast_phw_vega10_power_state(&state->hardware);

	result = vega10_get_powerplay_table_entry(hwmgr, entry_index, state,
			vega10_get_pp_table_entry_callback_func);

	/*
	 * This is the earliest time we have all the dependency table
	 * and the VBIOS boot state
	 */
	/* set DC compatible flag if this state supports DC */
	if (!state->validation.disallowOnDC)
		ps->dc_compatible = true;

	ps->uvd_clks.vclk = state->uvd_clocks.VCLK;
	ps->uvd_clks.dclk = state->uvd_clocks.DCLK;

	return 0;
}

static int vega10_patch_boot_state(struct pp_hwmgr *hwmgr,
	     struct pp_hw_power_state *hw_ps)
{
	return 0;
}

static int vega10_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *request_ps,
			const struct pp_power_state *current_ps)
{
	struct vega10_power_state *vega10_ps =
				cast_phw_vega10_power_state(&request_ps->hardware);
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
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	int32_t count;
	uint32_t stable_pstate_sclk_dpm_percentage;
	uint32_t stable_pstate_sclk = 0, stable_pstate_mclk = 0;
	uint32_t latency;

	data->battery_state = (PP_StateUILabel_Battery ==
			request_ps->classification.ui_label);

	if (vega10_ps->performance_level_count != 2)
		pr_info("VI should always have 2 performance levels");

	max_limits = (PP_PowerSource_AC == hwmgr->power_source) ?
			&(hwmgr->dyn_state.max_clock_voltage_on_ac) :
			&(hwmgr->dyn_state.max_clock_voltage_on_dc);

	/* Cap clock DPM tables at DC MAX if it is in DC. */
	if (PP_PowerSource_DC == hwmgr->power_source) {
		for (i = 0; i < vega10_ps->performance_level_count; i++) {
			if (vega10_ps->performance_levels[i].mem_clock >
				max_limits->mclk)
				vega10_ps->performance_levels[i].mem_clock =
						max_limits->mclk;
			if (vega10_ps->performance_levels[i].gfx_clock >
				max_limits->sclk)
				vega10_ps->performance_levels[i].gfx_clock =
						max_limits->sclk;
		}
	}

	vega10_ps->vce_clks.evclk = hwmgr->vce_arbiter.evclk;
	vega10_ps->vce_clks.ecclk = hwmgr->vce_arbiter.ecclk;

	cgs_get_active_displays_info(hwmgr->device, &info);

	/* result = PHM_CheckVBlankTime(hwmgr, &vblankTooShort);*/
	minimum_clocks.engineClock = hwmgr->display_config.min_core_set_clock;
	/* minimum_clocks.memoryClock = hwmgr->display_config.min_mem_set_clock; */

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StablePState)) {
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

	if (minimum_clocks.engineClock < hwmgr->gfx_arbiter.sclk)
		minimum_clocks.engineClock = hwmgr->gfx_arbiter.sclk;

	if (minimum_clocks.memoryClock < hwmgr->gfx_arbiter.mclk)
		minimum_clocks.memoryClock = hwmgr->gfx_arbiter.mclk;

	vega10_ps->sclk_threshold = hwmgr->gfx_arbiter.sclk_threshold;

	if (hwmgr->gfx_arbiter.sclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.sclk_over_drive <=
				hwmgr->platform_descriptor.overdriveLimit.engineClock),
				"Overdrive sclk exceeds limit",
				hwmgr->gfx_arbiter.sclk_over_drive =
						hwmgr->platform_descriptor.overdriveLimit.engineClock);

		if (hwmgr->gfx_arbiter.sclk_over_drive >= hwmgr->gfx_arbiter.sclk)
			vega10_ps->performance_levels[1].gfx_clock =
					hwmgr->gfx_arbiter.sclk_over_drive;
	}

	if (hwmgr->gfx_arbiter.mclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.mclk_over_drive <=
				hwmgr->platform_descriptor.overdriveLimit.memoryClock),
				"Overdrive mclk exceeds limit",
				hwmgr->gfx_arbiter.mclk_over_drive =
						hwmgr->platform_descriptor.overdriveLimit.memoryClock);

		if (hwmgr->gfx_arbiter.mclk_over_drive >= hwmgr->gfx_arbiter.mclk)
			vega10_ps->performance_levels[1].mem_clock =
					hwmgr->gfx_arbiter.mclk_over_drive;
	}

	disable_mclk_switching_for_frame_lock = phm_cap_enabled(
				    hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_DisableMclkSwitchingForFrameLock);
	disable_mclk_switching_for_vr = phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DisableMclkSwitchForVR);
	force_mclk_high = phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ForceMclkHigh);

	disable_mclk_switching = (info.display_count > 1) ||
				    disable_mclk_switching_for_frame_lock ||
				    disable_mclk_switching_for_vr ||
				    force_mclk_high;

	sclk = vega10_ps->performance_levels[0].gfx_clock;
	mclk = vega10_ps->performance_levels[0].mem_clock;

	if (sclk < minimum_clocks.engineClock)
		sclk = (minimum_clocks.engineClock > max_limits->sclk) ?
				max_limits->sclk : minimum_clocks.engineClock;

	if (mclk < minimum_clocks.memoryClock)
		mclk = (minimum_clocks.memoryClock > max_limits->mclk) ?
				max_limits->mclk : minimum_clocks.memoryClock;

	vega10_ps->performance_levels[0].gfx_clock = sclk;
	vega10_ps->performance_levels[0].mem_clock = mclk;

	vega10_ps->performance_levels[1].gfx_clock =
		(vega10_ps->performance_levels[1].gfx_clock >=
				vega10_ps->performance_levels[0].gfx_clock) ?
						vega10_ps->performance_levels[1].gfx_clock :
						vega10_ps->performance_levels[0].gfx_clock;

	if (disable_mclk_switching) {
		/* Set Mclk the max of level 0 and level 1 */
		if (mclk < vega10_ps->performance_levels[1].mem_clock)
			mclk = vega10_ps->performance_levels[1].mem_clock;

		/* Find the lowest MCLK frequency that is within
		 * the tolerable latency defined in DAL
		 */
		latency = 0;
		for (i = 0; i < data->mclk_latency_table.count; i++) {
			if ((data->mclk_latency_table.entries[i].latency <= latency) &&
				(data->mclk_latency_table.entries[i].frequency >=
						vega10_ps->performance_levels[0].mem_clock) &&
				(data->mclk_latency_table.entries[i].frequency <=
						vega10_ps->performance_levels[1].mem_clock))
				mclk = data->mclk_latency_table.entries[i].frequency;
		}
		vega10_ps->performance_levels[0].mem_clock = mclk;
	} else {
		if (vega10_ps->performance_levels[1].mem_clock <
				vega10_ps->performance_levels[0].mem_clock)
			vega10_ps->performance_levels[1].mem_clock =
					vega10_ps->performance_levels[0].mem_clock;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StablePState)) {
		for (i = 0; i < vega10_ps->performance_level_count; i++) {
			vega10_ps->performance_levels[i].gfx_clock = stable_pstate_sclk;
			vega10_ps->performance_levels[i].mem_clock = stable_pstate_mclk;
		}
	}

	return 0;
}

static int vega10_find_dpm_states_clocks_in_dpm_table(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct vega10_power_state *vega10_ps =
			cast_const_phw_vega10_power_state(states->pnew_state);
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct vega10_single_dpm_table *sclk_table =
			&(data->dpm_table.gfx_table);
	uint32_t sclk = vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].gfx_clock;
	struct vega10_single_dpm_table *mclk_table =
			&(data->dpm_table.mem_table);
	uint32_t mclk = vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].mem_clock;
	struct PP_Clocks min_clocks = {0};
	uint32_t i;
	struct cgs_display_info info = {0};

	data->need_update_dpm_table = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ODNinACSupport) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ODNinDCSupport)) {
		for (i = 0; i < sclk_table->count; i++) {
			if (sclk == sclk_table->dpm_levels[i].value)
				break;
		}

		if (!(data->apply_overdrive_next_settings_mask &
				DPMTABLE_OD_UPDATE_SCLK) && i >= sclk_table->count) {
			/* Check SCLK in DAL's minimum clocks
			 * in case DeepSleep divider update is required.
			 */
			if (data->display_timing.min_clock_in_sr !=
					min_clocks.engineClockInSR &&
				(min_clocks.engineClockInSR >=
						VEGA10_MINIMUM_ENGINE_CLOCK ||
					data->display_timing.min_clock_in_sr >=
						VEGA10_MINIMUM_ENGINE_CLOCK))
				data->need_update_dpm_table |= DPMTABLE_UPDATE_SCLK;
		}

		cgs_get_active_displays_info(hwmgr->device, &info);

		if (data->display_timing.num_existing_displays !=
				info.display_count)
			data->need_update_dpm_table |= DPMTABLE_UPDATE_MCLK;
	} else {
		for (i = 0; i < sclk_table->count; i++) {
			if (sclk == sclk_table->dpm_levels[i].value)
				break;
		}

		if (i >= sclk_table->count)
			data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_SCLK;
		else {
			/* Check SCLK in DAL's minimum clocks
			 * in case DeepSleep divider update is required.
			 */
			if (data->display_timing.min_clock_in_sr !=
					min_clocks.engineClockInSR &&
				(min_clocks.engineClockInSR >=
						VEGA10_MINIMUM_ENGINE_CLOCK ||
					data->display_timing.min_clock_in_sr >=
						VEGA10_MINIMUM_ENGINE_CLOCK))
				data->need_update_dpm_table |= DPMTABLE_UPDATE_SCLK;
		}

		for (i = 0; i < mclk_table->count; i++) {
			if (mclk == mclk_table->dpm_levels[i].value)
				break;
		}

		cgs_get_active_displays_info(hwmgr->device, &info);

		if (i >= mclk_table->count)
			data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_MCLK;

		if (data->display_timing.num_existing_displays !=
				info.display_count ||
				i >= mclk_table->count)
			data->need_update_dpm_table |= DPMTABLE_UPDATE_MCLK;
	}
	return 0;
}

static int vega10_populate_and_upload_sclk_mclk_dpm_levels(
		struct pp_hwmgr *hwmgr, const void *input)
{
	int result = 0;
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct vega10_power_state *vega10_ps =
			cast_const_phw_vega10_power_state(states->pnew_state);
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	uint32_t sclk = vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].gfx_clock;
	uint32_t mclk = vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].mem_clock;
	struct vega10_dpm_table *dpm_table = &data->dpm_table;
	struct vega10_dpm_table *golden_dpm_table =
			&data->golden_dpm_table;
	uint32_t dpm_count, clock_percent;
	uint32_t i;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ODNinACSupport) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ODNinDCSupport)) {

		if (!data->need_update_dpm_table &&
			!data->apply_optimized_settings &&
			!data->apply_overdrive_next_settings_mask)
			return 0;

		if (data->apply_overdrive_next_settings_mask &
				DPMTABLE_OD_UPDATE_SCLK) {
			for (dpm_count = 0;
					dpm_count < dpm_table->gfx_table.count;
					dpm_count++) {
				dpm_table->gfx_table.dpm_levels[dpm_count].enabled =
						data->odn_dpm_table.odn_core_clock_dpm_levels.
						performance_level_entries[dpm_count].enabled;
				dpm_table->gfx_table.dpm_levels[dpm_count].value =
						data->odn_dpm_table.odn_core_clock_dpm_levels.
						performance_level_entries[dpm_count].clock;
			}
		}

		if (data->apply_overdrive_next_settings_mask &
				DPMTABLE_OD_UPDATE_MCLK) {
			for (dpm_count = 0;
					dpm_count < dpm_table->mem_table.count;
					dpm_count++) {
				dpm_table->mem_table.dpm_levels[dpm_count].enabled =
						data->odn_dpm_table.odn_memory_clock_dpm_levels.
						performance_level_entries[dpm_count].enabled;
				dpm_table->mem_table.dpm_levels[dpm_count].value =
						data->odn_dpm_table.odn_memory_clock_dpm_levels.
						performance_level_entries[dpm_count].clock;
			}
		}

		if ((data->need_update_dpm_table & DPMTABLE_UPDATE_SCLK) ||
			data->apply_optimized_settings ||
			(data->apply_overdrive_next_settings_mask &
					DPMTABLE_OD_UPDATE_SCLK)) {
			result = vega10_populate_all_graphic_levels(hwmgr);
			PP_ASSERT_WITH_CODE(!result,
					"Failed to populate SCLK during \
					PopulateNewDPMClocksStates Function!",
					return result);
		}

		if ((data->need_update_dpm_table & DPMTABLE_UPDATE_MCLK) ||
			(data->apply_overdrive_next_settings_mask &
					DPMTABLE_OD_UPDATE_MCLK)){
			result = vega10_populate_all_memory_levels(hwmgr);
			PP_ASSERT_WITH_CODE(!result,
					"Failed to populate MCLK during \
					PopulateNewDPMClocksStates Function!",
					return result);
		}
	} else {
		if (!data->need_update_dpm_table &&
				!data->apply_optimized_settings)
			return 0;

		if (data->need_update_dpm_table & DPMTABLE_OD_UPDATE_SCLK &&
				data->smu_features[GNLD_DPM_GFXCLK].supported) {
				dpm_table->
				gfx_table.dpm_levels[dpm_table->gfx_table.count - 1].
				value = sclk;

				if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_OD6PlusinACSupport) ||
					phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_OD6PlusinDCSupport)) {
					/* Need to do calculation based on the golden DPM table
					 * as the Heatmap GPU Clock axis is also based on
					 * the default values
					 */
					PP_ASSERT_WITH_CODE(
							golden_dpm_table->gfx_table.dpm_levels
							[golden_dpm_table->gfx_table.count - 1].value,
							"Divide by 0!",
							return -1);

					dpm_count = dpm_table->gfx_table.count < 2 ?
							0 : dpm_table->gfx_table.count - 2;
					for (i = dpm_count; i > 1; i--) {
						if (sclk > golden_dpm_table->gfx_table.dpm_levels
							[golden_dpm_table->gfx_table.count - 1].value) {
							clock_percent =
								((sclk - golden_dpm_table->gfx_table.dpm_levels
								[golden_dpm_table->gfx_table.count - 1].value) *
								100) /
								golden_dpm_table->gfx_table.dpm_levels
								[golden_dpm_table->gfx_table.count - 1].value;

							dpm_table->gfx_table.dpm_levels[i].value =
								golden_dpm_table->gfx_table.dpm_levels[i].value +
								(golden_dpm_table->gfx_table.dpm_levels[i].value *
								clock_percent) / 100;
						} else if (golden_dpm_table->
								gfx_table.dpm_levels[dpm_table->gfx_table.count-1].value >
								sclk) {
							clock_percent =
								((golden_dpm_table->gfx_table.dpm_levels
								[golden_dpm_table->gfx_table.count - 1].value -
								sclk) *	100) /
								golden_dpm_table->gfx_table.dpm_levels
								[golden_dpm_table->gfx_table.count-1].value;

							dpm_table->gfx_table.dpm_levels[i].value =
								golden_dpm_table->gfx_table.dpm_levels[i].value -
								(golden_dpm_table->gfx_table.dpm_levels[i].value *
								clock_percent) / 100;
						} else
							dpm_table->gfx_table.dpm_levels[i].value =
								golden_dpm_table->gfx_table.dpm_levels[i].value;
					}
				}
			}

		if (data->need_update_dpm_table & DPMTABLE_OD_UPDATE_MCLK &&
				data->smu_features[GNLD_DPM_UCLK].supported) {
			dpm_table->
			mem_table.dpm_levels[dpm_table->mem_table.count - 1].
			value = mclk;

			if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_OD6PlusinACSupport) ||
				phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_OD6PlusinDCSupport)) {

				PP_ASSERT_WITH_CODE(
					golden_dpm_table->mem_table.dpm_levels
					[golden_dpm_table->mem_table.count - 1].value,
					"Divide by 0!",
					return -1);

				dpm_count = dpm_table->mem_table.count < 2 ?
						0 : dpm_table->mem_table.count - 2;
				for (i = dpm_count; i > 1; i--) {
					if (mclk > golden_dpm_table->mem_table.dpm_levels
						[golden_dpm_table->mem_table.count-1].value) {
						clock_percent = ((mclk -
							golden_dpm_table->mem_table.dpm_levels
							[golden_dpm_table->mem_table.count-1].value) *
							100) /
							golden_dpm_table->mem_table.dpm_levels
							[golden_dpm_table->mem_table.count-1].value;

						dpm_table->mem_table.dpm_levels[i].value =
							golden_dpm_table->mem_table.dpm_levels[i].value +
							(golden_dpm_table->mem_table.dpm_levels[i].value *
							clock_percent) / 100;
					} else if (golden_dpm_table->mem_table.dpm_levels
							[dpm_table->mem_table.count-1].value > mclk) {
						clock_percent = ((golden_dpm_table->mem_table.dpm_levels
							[golden_dpm_table->mem_table.count-1].value - mclk) *
							100) /
							golden_dpm_table->mem_table.dpm_levels
							[golden_dpm_table->mem_table.count-1].value;

						dpm_table->mem_table.dpm_levels[i].value =
							golden_dpm_table->mem_table.dpm_levels[i].value -
							(golden_dpm_table->mem_table.dpm_levels[i].value *
							clock_percent) / 100;
					} else
						dpm_table->mem_table.dpm_levels[i].value =
							golden_dpm_table->mem_table.dpm_levels[i].value;
				}
			}
		}

		if ((data->need_update_dpm_table &
			(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK)) ||
			data->apply_optimized_settings) {
			result = vega10_populate_all_graphic_levels(hwmgr);
			PP_ASSERT_WITH_CODE(!result,
					"Failed to populate SCLK during \
					PopulateNewDPMClocksStates Function!",
					return result);
		}

		if (data->need_update_dpm_table &
				(DPMTABLE_OD_UPDATE_MCLK + DPMTABLE_UPDATE_MCLK)) {
			result = vega10_populate_all_memory_levels(hwmgr);
			PP_ASSERT_WITH_CODE(!result,
					"Failed to populate MCLK during \
					PopulateNewDPMClocksStates Function!",
					return result);
		}
	}

	return result;
}

static int vega10_trim_single_dpm_states(struct pp_hwmgr *hwmgr,
		struct vega10_single_dpm_table *dpm_table,
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

static int vega10_trim_single_dpm_states_with_mask(struct pp_hwmgr *hwmgr,
		struct vega10_single_dpm_table *dpm_table,
		uint32_t low_limit, uint32_t high_limit,
		uint32_t disable_dpm_mask)
{
	uint32_t i;

	for (i = 0; i < dpm_table->count; i++) {
		if ((dpm_table->dpm_levels[i].value < low_limit) ||
		    (dpm_table->dpm_levels[i].value > high_limit))
			dpm_table->dpm_levels[i].enabled = false;
		else if (!((1 << i) & disable_dpm_mask))
			dpm_table->dpm_levels[i].enabled = false;
		else
			dpm_table->dpm_levels[i].enabled = true;
	}
	return 0;
}

static int vega10_trim_dpm_states(struct pp_hwmgr *hwmgr,
		const struct vega10_power_state *vega10_ps)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	uint32_t high_limit_count;

	PP_ASSERT_WITH_CODE((vega10_ps->performance_level_count >= 1),
			"power state did not have any performance level",
			return -1);

	high_limit_count = (vega10_ps->performance_level_count == 1) ? 0 : 1;

	vega10_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.soc_table),
			vega10_ps->performance_levels[0].soc_clock,
			vega10_ps->performance_levels[high_limit_count].soc_clock);

	vega10_trim_single_dpm_states_with_mask(hwmgr,
			&(data->dpm_table.gfx_table),
			vega10_ps->performance_levels[0].gfx_clock,
			vega10_ps->performance_levels[high_limit_count].gfx_clock,
			data->disable_dpm_mask);

	vega10_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.mem_table),
			vega10_ps->performance_levels[0].mem_clock,
			vega10_ps->performance_levels[high_limit_count].mem_clock);

	return 0;
}

static uint32_t vega10_find_lowest_dpm_level(
		struct vega10_single_dpm_table *table)
{
	uint32_t i;

	for (i = 0; i < table->count; i++) {
		if (table->dpm_levels[i].enabled)
			break;
	}

	return i;
}

static uint32_t vega10_find_highest_dpm_level(
		struct vega10_single_dpm_table *table)
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

static void vega10_apply_dal_minimum_voltage_request(
		struct pp_hwmgr *hwmgr)
{
	return;
}

static int vega10_upload_dpm_bootup_level(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	vega10_apply_dal_minimum_voltage_request(hwmgr);

	if (!data->registry_data.sclk_dpm_key_disabled) {
		if (data->smc_state_table.gfx_boot_level !=
				data->dpm_table.gfx_table.dpm_state.soft_min_level) {
				PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
				hwmgr->smumgr,
				PPSMC_MSG_SetSoftMinGfxclkByIndex,
				data->smc_state_table.gfx_boot_level),
				"Failed to set soft min sclk index!",
				return -EINVAL);
			data->dpm_table.gfx_table.dpm_state.soft_min_level =
					data->smc_state_table.gfx_boot_level;
		}
	}

	if (!data->registry_data.mclk_dpm_key_disabled) {
		if (data->smc_state_table.mem_boot_level !=
				data->dpm_table.mem_table.dpm_state.soft_min_level) {
				PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
				hwmgr->smumgr,
				 PPSMC_MSG_SetSoftMinUclkByIndex,
				data->smc_state_table.mem_boot_level),
				"Failed to set soft min mclk index!",
				return -EINVAL);

			data->dpm_table.mem_table.dpm_state.soft_min_level =
					data->smc_state_table.mem_boot_level;
		}
	}

	return 0;
}

static int vega10_upload_dpm_max_level(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	vega10_apply_dal_minimum_voltage_request(hwmgr);

	if (!data->registry_data.sclk_dpm_key_disabled) {
		if (data->smc_state_table.gfx_max_level !=
				data->dpm_table.gfx_table.dpm_state.soft_max_level) {
				PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
				hwmgr->smumgr,
				PPSMC_MSG_SetSoftMaxGfxclkByIndex,
				data->smc_state_table.gfx_max_level),
				"Failed to set soft max sclk index!",
				return -EINVAL);
			data->dpm_table.gfx_table.dpm_state.soft_max_level =
					data->smc_state_table.gfx_max_level;
		}
	}

	if (!data->registry_data.mclk_dpm_key_disabled) {
		if (data->smc_state_table.mem_max_level !=
				data->dpm_table.mem_table.dpm_state.soft_max_level) {
				PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
				hwmgr->smumgr,
				PPSMC_MSG_SetSoftMaxUclkByIndex,
				data->smc_state_table.mem_max_level),
				"Failed to set soft max mclk index!",
				return -EINVAL);
			data->dpm_table.mem_table.dpm_state.soft_max_level =
					data->smc_state_table.mem_max_level;
		}
	}

	return 0;
}

static int vega10_generate_dpm_level_enable_mask(
		struct pp_hwmgr *hwmgr, const void *input)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct vega10_power_state *vega10_ps =
			cast_const_phw_vega10_power_state(states->pnew_state);
	int i;

	PP_ASSERT_WITH_CODE(!vega10_trim_dpm_states(hwmgr, vega10_ps),
			"Attempt to Trim DPM States Failed!",
			return -1);

	data->smc_state_table.gfx_boot_level =
			vega10_find_lowest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.gfx_max_level =
			vega10_find_highest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
			vega10_find_lowest_dpm_level(&(data->dpm_table.mem_table));
	data->smc_state_table.mem_max_level =
			vega10_find_highest_dpm_level(&(data->dpm_table.mem_table));

	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_bootup_level(hwmgr),
			"Attempt to upload DPM Bootup Levels Failed!",
			return -1);
	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_max_level(hwmgr),
			"Attempt to upload DPM Max Levels Failed!",
			return -1);
	for(i = data->smc_state_table.gfx_boot_level; i < data->smc_state_table.gfx_max_level; i++)
		data->dpm_table.gfx_table.dpm_levels[i].enabled = true;


	for(i = data->smc_state_table.mem_boot_level; i < data->smc_state_table.mem_max_level; i++)
		data->dpm_table.mem_table.dpm_levels[i].enabled = true;

	return 0;
}

int vega10_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_DPM_VCE].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
				enable,
				data->smu_features[GNLD_DPM_VCE].smu_feature_bitmap),
				"Attempt to Enable/Disable DPM VCE Failed!",
				return -1);
		data->smu_features[GNLD_DPM_VCE].enabled = enable;
	}

	return 0;
}

static int vega10_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	int result = 0;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification)
		&& (hwmgr->gfx_arbiter.sclk_threshold !=
				data->low_sclk_interrupt_threshold)) {
		data->low_sclk_interrupt_threshold =
				hwmgr->gfx_arbiter.sclk_threshold;
		low_sclk_interrupt_threshold =
				data->low_sclk_interrupt_threshold;

		data->smc_state_table.pp_table.LowGfxclkInterruptThreshold =
				cpu_to_le32(low_sclk_interrupt_threshold);

		/* This message will also enable SmcToHost Interrupt */
		result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetLowGfxclkInterruptThreshold,
				(uint32_t)low_sclk_interrupt_threshold);
	}

	return result;
}

static int vega10_set_power_state_tasks(struct pp_hwmgr *hwmgr,
		const void *input)
{
	int tmp_result, result = 0;
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);

	tmp_result = vega10_find_dpm_states_clocks_in_dpm_table(hwmgr, input);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to find DPM states clocks in DPM table!",
			result = tmp_result);

	tmp_result = vega10_populate_and_upload_sclk_mclk_dpm_levels(hwmgr, input);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to populate and upload SCLK MCLK DPM levels!",
			result = tmp_result);

	tmp_result = vega10_generate_dpm_level_enable_mask(hwmgr, input);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to generate DPM level enabled mask!",
			result = tmp_result);

	tmp_result = vega10_update_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			"Failed to update SCLK threshold!",
			result = tmp_result);

	result = vega10_copy_table_to_smc(hwmgr->smumgr,
			(uint8_t *)pp_table, PPTABLE);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload PPtable!", return result);

	data->apply_optimized_settings = false;
	data->apply_overdrive_next_settings_mask = 0;

	return 0;
}

static int vega10_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state *ps;
	struct vega10_power_state *vega10_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	vega10_ps = cast_phw_vega10_power_state(&ps->hardware);

	if (low)
		return vega10_ps->performance_levels[0].gfx_clock;
	else
		return vega10_ps->performance_levels
				[vega10_ps->performance_level_count - 1].gfx_clock;
}

static int vega10_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state *ps;
	struct vega10_power_state *vega10_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	vega10_ps = cast_phw_vega10_power_state(&ps->hardware);

	if (low)
		return vega10_ps->performance_levels[0].mem_clock;
	else
		return vega10_ps->performance_levels
				[vega10_ps->performance_level_count-1].mem_clock;
}

static int vega10_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			      void *value, int *size)
{
	uint32_t sclk_idx, mclk_idx, activity_percent = 0;
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	struct vega10_dpm_table *dpm_table = &data->dpm_table;
	int ret = 0;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_GetCurrentGfxclkIndex);
		if (!ret) {
			vega10_read_arg_from_smc(hwmgr->smumgr, &sclk_idx);
			*((uint32_t *)value) = dpm_table->gfx_table.dpm_levels[sclk_idx].value;
			*size = 4;
		}
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_GetCurrentUclkIndex);
		if (!ret) {
			vega10_read_arg_from_smc(hwmgr->smumgr, &mclk_idx);
			*((uint32_t *)value) = dpm_table->mem_table.dpm_levels[mclk_idx].value;
			*size = 4;
		}
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, PPSMC_MSG_GetAverageGfxActivity, 0);
		if (!ret) {
			vega10_read_arg_from_smc(hwmgr->smumgr, &activity_percent);
			*((uint32_t *)value) = activity_percent > 100 ? 100 : activity_percent;
			*size = 4;
		}
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = vega10_thermal_get_temperature(hwmgr);
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
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int vega10_notify_smc_display_change(struct pp_hwmgr *hwmgr,
		bool has_disp)
{
	return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_SetUclkFastSwitch,
			has_disp ? 0 : 1);
}

int vega10_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
		struct pp_display_clock_request *clock_req)
{
	int result = 0;
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 100;
	DSPCLK_e clk_select = 0;
	uint32_t clk_request = 0;

	switch (clk_type) {
	case amd_pp_dcef_clock:
		clk_select = DSPCLK_DCEFCLK;
		break;
	case amd_pp_disp_clock:
		clk_select = DSPCLK_DISPCLK;
		break;
	case amd_pp_pixel_clock:
		clk_select = DSPCLK_PIXCLK;
		break;
	case amd_pp_phy_clock:
		clk_select = DSPCLK_PHYCLK;
		break;
	default:
		pr_info("[DisplayClockVoltageRequest]Invalid Clock Type!");
		result = -1;
		break;
	}

	if (!result) {
		clk_request = (clk_freq << 16) | clk_select;
		result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_RequestDisplayClockByFreq,
				clk_request);
	}

	return result;
}

static int vega10_notify_smc_display_config_after_ps_adjustment(
		struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct vega10_single_dpm_table *dpm_table =
			&data->dpm_table.dcef_table;
	uint32_t num_active_disps = 0;
	struct cgs_display_info info = {0};
	struct PP_Clocks min_clocks = {0};
	uint32_t i;
	struct pp_display_clock_request clock_req;

	info.mode_info = NULL;

	cgs_get_active_displays_info(hwmgr->device, &info);

	num_active_disps = info.display_count;

	if (num_active_disps > 1)
		vega10_notify_smc_display_change(hwmgr, false);
	else
		vega10_notify_smc_display_change(hwmgr, true);

	min_clocks.dcefClock = hwmgr->display_config.min_dcef_set_clk;
	min_clocks.dcefClockInSR = hwmgr->display_config.min_dcef_deep_sleep_set_clk;

	for (i = 0; i < dpm_table->count; i++) {
		if (dpm_table->dpm_levels[i].value == min_clocks.dcefClock)
			break;
	}

	if (i < dpm_table->count) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = dpm_table->dpm_levels[i].value;
		if (!vega10_display_clock_voltage_request(hwmgr, &clock_req)) {
			PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
					hwmgr->smumgr, PPSMC_MSG_SetMinDeepSleepDcefclk,
					min_clocks.dcefClockInSR),
					"Attempt to set divider for DCEFCLK Failed!",);
		} else
			pr_info("Attempt to set Hard Min for DCEFCLK Failed!");
	} else
		pr_info("Cannot find requested DCEFCLK!");

	return 0;
}

static int vega10_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	data->smc_state_table.gfx_boot_level =
	data->smc_state_table.gfx_max_level =
			vega10_find_highest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
	data->smc_state_table.mem_max_level =
			vega10_find_highest_dpm_level(&(data->dpm_table.mem_table));

	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_bootup_level(hwmgr),
			"Failed to upload boot level to highest!",
			return -1);

	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -1);

	return 0;
}

static int vega10_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	data->smc_state_table.gfx_boot_level =
	data->smc_state_table.gfx_max_level =
			vega10_find_lowest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
	data->smc_state_table.mem_max_level =
			vega10_find_lowest_dpm_level(&(data->dpm_table.mem_table));

	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_bootup_level(hwmgr),
			"Failed to upload boot level to highest!",
			return -1);

	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -1);

	return 0;

}

static int vega10_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);

	data->smc_state_table.gfx_boot_level =
			vega10_find_lowest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.gfx_max_level =
			vega10_find_highest_dpm_level(&(data->dpm_table.gfx_table));
	data->smc_state_table.mem_boot_level =
			vega10_find_lowest_dpm_level(&(data->dpm_table.mem_table));
	data->smc_state_table.mem_max_level =
			vega10_find_highest_dpm_level(&(data->dpm_table.mem_table));

	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_bootup_level(hwmgr),
			"Failed to upload DPM Bootup Levels!",
			return -1);

	PP_ASSERT_WITH_CODE(!vega10_upload_dpm_max_level(hwmgr),
			"Failed to upload DPM Max Levels!",
			return -1);
	return 0;
}

static int vega10_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = vega10_force_dpm_highest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = vega10_force_dpm_lowest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = vega10_unforce_dpm_levels(hwmgr);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	hwmgr->dpm_level = level;

	return ret;
}

static int vega10_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	if (mode) {
		/* stop auto-manage */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl))
			vega10_fan_ctrl_stop_smc_fan_control(hwmgr);
		vega10_fan_ctrl_set_static_mode(hwmgr, mode);
	} else
		/* restart auto-manage */
		vega10_fan_ctrl_reset_fan_speed_to_default(hwmgr);

	return 0;
}

static int vega10_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	uint32_t reg;

	if (hwmgr->fan_ctrl_is_in_default_mode) {
		return hwmgr->fan_ctrl_default_mode;
	} else {
		reg = soc15_get_register_offset(THM_HWID, 0,
			mmCG_FDO_CTRL2_BASE_IDX, mmCG_FDO_CTRL2);
		return (cgs_read_register(hwmgr->device, reg) &
				CG_FDO_CTRL2__FDO_PWM_MODE_MASK) >>
				CG_FDO_CTRL2__FDO_PWM_MODE__SHIFT;
	}
}

static int vega10_get_dal_power_level(struct pp_hwmgr *hwmgr,
		struct amd_pp_simple_clock_info *info)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_clock_and_voltage_limits *max_limits =
			&table_info->max_clock_voltage_on_ac;

	info->engine_max_clock = max_limits->sclk;
	info->memory_max_clock = max_limits->mclk;

	return 0;
}

static void vega10_get_sclks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_sclk;
	uint32_t i;

	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].clk) {
			clocks->data[clocks->num_levels].clocks_in_khz =
					dep_table->entries[i].clk;
			clocks->num_levels++;
		}
	}

}

static uint32_t vega10_get_mem_latency(struct pp_hwmgr *hwmgr,
		uint32_t clock)
{
	if (clock >= MEM_FREQ_LOW_LATENCY &&
			clock < MEM_FREQ_HIGH_LATENCY)
		return MEM_LATENCY_HIGH;
	else if (clock >= MEM_FREQ_HIGH_LATENCY)
		return MEM_LATENCY_LOW;
	else
		return MEM_LATENCY_ERR;
}

static void vega10_get_memclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_mclk;
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	uint32_t i;

	clocks->num_levels = 0;
	data->mclk_latency_table.count = 0;

	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].clk) {
			clocks->data[clocks->num_levels].clocks_in_khz =
			data->mclk_latency_table.entries
			[data->mclk_latency_table.count].frequency =
					dep_table->entries[i].clk;
			clocks->data[clocks->num_levels].latency_in_us =
			data->mclk_latency_table.entries
			[data->mclk_latency_table.count].latency =
					vega10_get_mem_latency(hwmgr,
						dep_table->entries[i].clk);
			clocks->num_levels++;
			data->mclk_latency_table.count++;
		}
	}
}

static void vega10_get_dcefclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_dcefclk;
	uint32_t i;

	for (i = 0; i < dep_table->count; i++) {
		clocks->data[i].clocks_in_khz = dep_table->entries[i].clk;
		clocks->data[i].latency_in_us = 0;
		clocks->num_levels++;
	}
}

static void vega10_get_socclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_socclk;
	uint32_t i;

	for (i = 0; i < dep_table->count; i++) {
		clocks->data[i].clocks_in_khz = dep_table->entries[i].clk;
		clocks->data[i].latency_in_us = 0;
		clocks->num_levels++;
	}
}

static int vega10_get_clock_by_type_with_latency(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks)
{
	switch (type) {
	case amd_pp_sys_clock:
		vega10_get_sclks(hwmgr, clocks);
		break;
	case amd_pp_mem_clock:
		vega10_get_memclocks(hwmgr, clocks);
		break;
	case amd_pp_dcef_clock:
		vega10_get_dcefclocks(hwmgr, clocks);
		break;
	case amd_pp_soc_clock:
		vega10_get_socclocks(hwmgr, clocks);
		break;
	default:
		return -1;
	}

	return 0;
}

static int vega10_get_clock_by_type_with_voltage(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table;
	uint32_t i;

	switch (type) {
	case amd_pp_mem_clock:
		dep_table = table_info->vdd_dep_on_mclk;
		break;
	case amd_pp_dcef_clock:
		dep_table = table_info->vdd_dep_on_dcefclk;
		break;
	case amd_pp_disp_clock:
		dep_table = table_info->vdd_dep_on_dispclk;
		break;
	case amd_pp_pixel_clock:
		dep_table = table_info->vdd_dep_on_pixclk;
		break;
	case amd_pp_phy_clock:
		dep_table = table_info->vdd_dep_on_phyclk;
		break;
	default:
		return -1;
	}

	for (i = 0; i < dep_table->count; i++) {
		clocks->data[i].clocks_in_khz = dep_table->entries[i].clk;
		clocks->data[i].voltage_in_mv = (uint32_t)(table_info->vddc_lookup_table->
				entries[dep_table->entries[i].vddInd].us_vdd);
		clocks->num_levels++;
	}

	if (i < dep_table->count)
		return -1;

	return 0;
}

static int vega10_set_watermarks_for_clocks_ranges(struct pp_hwmgr *hwmgr,
		struct pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	Watermarks_t *table = &(data->smc_state_table.water_marks_table);
	int result = 0;
	uint32_t i;

	if (!data->registry_data.disable_water_mark) {
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
		data->water_marks_bitmap = WaterMarksExist;
	}

	return result;
}

static int vega10_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	uint32_t i;

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	switch (type) {
	case PP_SCLK:
		if (data->registry_data.sclk_dpm_key_disabled)
			break;

		for (i = 0; i < 32; i++) {
			if (mask & (1 << i))
				break;
		}

		PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
				hwmgr->smumgr,
				PPSMC_MSG_SetSoftMinGfxclkByIndex,
				i),
				"Failed to set soft min sclk index!",
				return -1);
		break;

	case PP_MCLK:
		if (data->registry_data.mclk_dpm_key_disabled)
			break;

		for (i = 0; i < 32; i++) {
			if (mask & (1 << i))
				break;
		}

		PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
				hwmgr->smumgr,
				PPSMC_MSG_SetSoftMinUclkByIndex,
				i),
				"Failed to set soft min mclk index!",
				return -1);
		break;

	case PP_PCIE:
		if (data->registry_data.pcie_dpm_key_disabled)
			break;

		for (i = 0; i < 32; i++) {
			if (mask & (1 << i))
				break;
		}

		PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc_with_parameter(
				hwmgr->smumgr,
				PPSMC_MSG_SetMinLinkDpmByIndex,
				i),
				"Failed to set min pcie index!",
				return -1);
		break;
	default:
		break;
	}

	return 0;
}

static int vega10_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	struct vega10_single_dpm_table *sclk_table = &(data->dpm_table.gfx_table);
	struct vega10_single_dpm_table *mclk_table = &(data->dpm_table.mem_table);
	struct vega10_pcie_table *pcie_table = &(data->dpm_table.pcie_table);
	int i, now, size = 0;

	switch (type) {
	case PP_SCLK:
		if (data->registry_data.sclk_dpm_key_disabled)
			break;

		PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_GetCurrentGfxclkIndex),
				"Attempt to get current sclk index Failed!",
				return -1);
		PP_ASSERT_WITH_CODE(!vega10_read_arg_from_smc(hwmgr->smumgr,
				&now),
				"Attempt to read sclk index Failed!",
				return -1);

		for (i = 0; i < sclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, sclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_MCLK:
		if (data->registry_data.mclk_dpm_key_disabled)
			break;

		PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_GetCurrentUclkIndex),
				"Attempt to get current mclk index Failed!",
				return -1);
		PP_ASSERT_WITH_CODE(!vega10_read_arg_from_smc(hwmgr->smumgr,
				&now),
				"Attempt to read mclk index Failed!",
				return -1);

		for (i = 0; i < mclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, mclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_PCIE:
		PP_ASSERT_WITH_CODE(!smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_GetCurrentLinkIndex),
				"Attempt to get current mclk index Failed!",
				return -1);
		PP_ASSERT_WITH_CODE(!vega10_read_arg_from_smc(hwmgr->smumgr,
				&now),
				"Attempt to read mclk index Failed!",
				return -1);

		for (i = 0; i < pcie_table->count; i++)
			size += sprintf(buf + size, "%d: %s %s\n", i,
					(pcie_table->pcie_gen[i] == 0) ? "2.5GB, x1" :
					(pcie_table->pcie_gen[i] == 1) ? "5.0GB, x16" :
					(pcie_table->pcie_gen[i] == 2) ? "8.0GB, x16" : "",
					(i == now) ? "*" : "");
		break;
	default:
		break;
	}
	return size;
}

static int vega10_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	int result = 0;
	uint32_t num_turned_on_displays = 1;
	Watermarks_t *wm_table = &(data->smc_state_table.water_marks_table);
	struct cgs_display_info info = {0};

	if ((data->water_marks_bitmap & WaterMarksExist) &&
			!(data->water_marks_bitmap & WaterMarksLoaded)) {
		result = vega10_copy_table_to_smc(hwmgr->smumgr,
			(uint8_t *)wm_table, WMTABLE);
		PP_ASSERT_WITH_CODE(result, "Failed to update WMTABLE!", return EINVAL);
		data->water_marks_bitmap |= WaterMarksLoaded;
	}

	if (data->water_marks_bitmap & WaterMarksLoaded) {
		cgs_get_active_displays_info(hwmgr->device, &info);
		num_turned_on_displays = info.display_count;
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_NumOfDisplays, num_turned_on_displays);
	}

	return result;
}

int vega10_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (data->smu_features[GNLD_DPM_UVD].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
				enable,
				data->smu_features[GNLD_DPM_UVD].smu_feature_bitmap),
				"Attempt to Enable/Disable DPM UVD Failed!",
				return -1);
		data->smu_features[GNLD_DPM_UVD].enabled = enable;
	}
	return 0;
}

static int vega10_power_gate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);

	data->vce_power_gated = bgate;
	return vega10_enable_disable_vce_dpm(hwmgr, !bgate);
}

static int vega10_power_gate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = bgate;
	return vega10_enable_disable_uvd_dpm(hwmgr, !bgate);
}

static inline bool vega10_are_power_levels_equal(
				const struct vega10_performance_level *pl1,
				const struct vega10_performance_level *pl2)
{
	return ((pl1->soc_clock == pl2->soc_clock) &&
			(pl1->gfx_clock == pl2->gfx_clock) &&
			(pl1->mem_clock == pl2->mem_clock));
}

static int vega10_check_states_equal(struct pp_hwmgr *hwmgr,
				const struct pp_hw_power_state *pstate1,
			const struct pp_hw_power_state *pstate2, bool *equal)
{
	const struct vega10_power_state *psa;
	const struct vega10_power_state *psb;
	int i;

	if (pstate1 == NULL || pstate2 == NULL || equal == NULL)
		return -EINVAL;

	psa = cast_const_phw_vega10_power_state(pstate1);
	psb = cast_const_phw_vega10_power_state(pstate2);
	/* If the two states don't even have the same number of performance levels they cannot be the same state. */
	if (psa->performance_level_count != psb->performance_level_count) {
		*equal = false;
		return 0;
	}

	for (i = 0; i < psa->performance_level_count; i++) {
		if (!vega10_are_power_levels_equal(&(psa->performance_levels[i]), &(psb->performance_levels[i]))) {
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
vega10_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	bool is_update_required = false;
	struct cgs_display_info info = {0, 0, NULL};

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (data->display_timing.num_existing_displays != info.display_count)
		is_update_required = true;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		if (data->display_timing.min_clock_in_sr != hwmgr->display_config.min_core_set_clock_in_sr)
			is_update_required = true;
	}

	return is_update_required;
}

static const struct pp_hwmgr_func vega10_hwmgr_funcs = {
	.backend_init = vega10_hwmgr_backend_init,
	.backend_fini = vega10_hwmgr_backend_fini,
	.asic_setup = vega10_setup_asic_task,
	.dynamic_state_management_enable = vega10_enable_dpm_tasks,
	.get_num_of_pp_table_entries =
			vega10_get_number_of_powerplay_table_entries,
	.get_power_state_size = vega10_get_power_state_size,
	.get_pp_table_entry = vega10_get_pp_table_entry,
	.patch_boot_state = vega10_patch_boot_state,
	.apply_state_adjust_rules = vega10_apply_state_adjust_rules,
	.power_state_set = vega10_set_power_state_tasks,
	.get_sclk = vega10_dpm_get_sclk,
	.get_mclk = vega10_dpm_get_mclk,
	.notify_smc_display_config_after_ps_adjustment =
			vega10_notify_smc_display_config_after_ps_adjustment,
	.force_dpm_level = vega10_dpm_force_dpm_level,
	.get_temperature = vega10_thermal_get_temperature,
	.stop_thermal_controller = vega10_thermal_stop_thermal_controller,
	.get_fan_speed_info = vega10_fan_ctrl_get_fan_speed_info,
	.get_fan_speed_percent = vega10_fan_ctrl_get_fan_speed_percent,
	.set_fan_speed_percent = vega10_fan_ctrl_set_fan_speed_percent,
	.reset_fan_speed_to_default =
			vega10_fan_ctrl_reset_fan_speed_to_default,
	.get_fan_speed_rpm = vega10_fan_ctrl_get_fan_speed_rpm,
	.set_fan_speed_rpm = vega10_fan_ctrl_set_fan_speed_rpm,
	.uninitialize_thermal_controller =
			vega10_thermal_ctrl_uninitialize_thermal_controller,
	.set_fan_control_mode = vega10_set_fan_control_mode,
	.get_fan_control_mode = vega10_get_fan_control_mode,
	.read_sensor = vega10_read_sensor,
	.get_dal_power_level = vega10_get_dal_power_level,
	.get_clock_by_type_with_latency = vega10_get_clock_by_type_with_latency,
	.get_clock_by_type_with_voltage = vega10_get_clock_by_type_with_voltage,
	.set_watermarks_for_clocks_ranges = vega10_set_watermarks_for_clocks_ranges,
	.display_clock_voltage_request = vega10_display_clock_voltage_request,
	.force_clock_level = vega10_force_clock_level,
	.print_clock_levels = vega10_print_clock_levels,
	.display_config_changed = vega10_display_configuration_changed_task,
	.powergate_uvd = vega10_power_gate_uvd,
	.powergate_vce = vega10_power_gate_vce,
	.check_states_equal = vega10_check_states_equal,
	.check_smc_update_required_for_display_configuration =
			vega10_check_smc_update_required_for_display_configuration,
};

int vega10_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &vega10_hwmgr_funcs;
	hwmgr->pptable_func = &vega10_pptable_funcs;
	pp_vega10_thermal_initialize(hwmgr);
	return 0;
}
