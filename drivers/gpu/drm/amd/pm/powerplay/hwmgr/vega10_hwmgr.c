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

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "hwmgr.h"
#include "amd_powerplay.h"
#include "hardwaremanager.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "cgs_common.h"
#include "vega10_powertune.h"
#include "smu9.h"
#include "smu9_driver_if.h"
#include "vega10_inc.h"
#include "soc15_common.h"
#include "pppcielanes.h"
#include "vega10_hwmgr.h"
#include "vega10_smumgr.h"
#include "vega10_processpptables.h"
#include "vega10_pptable.h"
#include "vega10_thermal.h"
#include "pp_debug.h"
#include "amd_pcie_helpers.h"
#include "ppinterrupt.h"
#include "pp_overdriver.h"
#include "pp_thermal.h"
#include "vega10_baco.h"

#include "smuio/smuio_9_0_offset.h"
#include "smuio/smuio_9_0_sh_mask.h"

#define smnPCIE_LC_SPEED_CNTL			0x11140290
#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288

#define HBM_MEMORY_CHANNEL_WIDTH    128

static const uint32_t channel_number[] = {1, 2, 0, 4, 0, 8, 0, 16, 2};

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

typedef enum {
	CLK_SMNCLK = 0,
	CLK_SOCCLK,
	CLK_MP0CLK,
	CLK_MP1CLK,
	CLK_LCLK,
	CLK_DCEFCLK,
	CLK_VCLK,
	CLK_DCLK,
	CLK_ECLK,
	CLK_UCLK,
	CLK_GFXCLK,
	CLK_COUNT,
} CLOCK_ID_e;

static const ULONG PhwVega10_Magic = (ULONG)(PHM_VIslands_Magic);

static struct vega10_power_state *cast_phw_vega10_power_state(
				  struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwVega10_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (struct vega10_power_state *)hw_ps;
}

static const struct vega10_power_state *cast_const_phw_vega10_power_state(
				 const struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwVega10_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (const struct vega10_power_state *)hw_ps;
}

static void vega10_set_default_registry_data(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	data->registry_data.sclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_SCLK_DPM_MASK ? false : true;
	data->registry_data.socclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_SOCCLK_DPM_MASK ? false : true;
	data->registry_data.mclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_MCLK_DPM_MASK ? false : true;
	data->registry_data.pcie_dpm_key_disabled =
			hwmgr->feature_mask & PP_PCIE_DPM_MASK ? false : true;

	data->registry_data.dcefclk_dpm_key_disabled =
			hwmgr->feature_mask & PP_DCEFCLK_DPM_MASK ? false : true;

	if (hwmgr->feature_mask & PP_POWER_CONTAINMENT_MASK) {
		data->registry_data.power_containment_support = 1;
		data->registry_data.enable_pkg_pwr_tracking_feature = 1;
		data->registry_data.enable_tdc_limit_feature = 1;
	}

	data->registry_data.clock_stretcher_support =
			hwmgr->feature_mask & PP_CLOCK_STRETCH_MASK ? true : false;

	data->registry_data.ulv_support =
			hwmgr->feature_mask & PP_ULV_MASK ? true : false;

	data->registry_data.sclk_deep_sleep_support =
			hwmgr->feature_mask & PP_SCLK_DEEP_SLEEP_MASK ? true : false;

	data->registry_data.disable_water_mark = 0;

	data->registry_data.fan_control_support = 1;
	data->registry_data.thermal_support = 1;
	data->registry_data.fw_ctf_enabled = 1;

	data->registry_data.avfs_support =
		hwmgr->feature_mask & PP_AVFS_MASK ? true : false;
	data->registry_data.led_dpm_enabled = 1;

	data->registry_data.vr0hot_enabled = 1;
	data->registry_data.vr1hot_enabled = 1;
	data->registry_data.regulator_hot_gpio_support = 1;

	data->registry_data.didt_support = 1;
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
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct amdgpu_device *adev = hwmgr->adev;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPatchPowerState);

	if (data->vddci_control == VEGA10_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ControlVDDCI);

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

static int vega10_odn_initial_default_setting(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct vega10_odn_dpm_table *odn_table = &(data->odn_dpm_table);
	struct vega10_odn_vddc_lookup_table *od_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddc_lookup_table;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table[3];
	struct phm_ppt_v1_clock_voltage_dependency_table *od_table[3];
	struct pp_atomfwctrl_avfs_parameters avfs_params = {0};
	uint32_t i;
	int result;

	result = pp_atomfwctrl_get_avfs_information(hwmgr, &avfs_params);
	if (!result) {
		data->odn_dpm_table.max_vddc = avfs_params.ulMaxVddc;
		data->odn_dpm_table.min_vddc = avfs_params.ulMinVddc;
	}

	od_lookup_table = &odn_table->vddc_lookup_table;
	vddc_lookup_table = table_info->vddc_lookup_table;

	for (i = 0; i < vddc_lookup_table->count; i++)
		od_lookup_table->entries[i].us_vdd = vddc_lookup_table->entries[i].us_vdd;

	od_lookup_table->count = vddc_lookup_table->count;

	dep_table[0] = table_info->vdd_dep_on_sclk;
	dep_table[1] = table_info->vdd_dep_on_mclk;
	dep_table[2] = table_info->vdd_dep_on_socclk;
	od_table[0] = (struct phm_ppt_v1_clock_voltage_dependency_table *)&odn_table->vdd_dep_on_sclk;
	od_table[1] = (struct phm_ppt_v1_clock_voltage_dependency_table *)&odn_table->vdd_dep_on_mclk;
	od_table[2] = (struct phm_ppt_v1_clock_voltage_dependency_table *)&odn_table->vdd_dep_on_socclk;

	for (i = 0; i < 3; i++)
		smu_get_voltage_dependency_table_ppt_v1(dep_table[i], od_table[i]);

	if (odn_table->max_vddc == 0 || odn_table->max_vddc > 2000)
		odn_table->max_vddc = dep_table[0]->entries[dep_table[0]->count - 1].vddc;
	if (odn_table->min_vddc == 0 || odn_table->min_vddc > 2000)
		odn_table->min_vddc = dep_table[0]->entries[0].vddc;

	i = od_table[2]->count - 1;
	od_table[2]->entries[i].clk = hwmgr->platform_descriptor.overdriveLimit.memoryClock > od_table[2]->entries[i].clk ?
					hwmgr->platform_descriptor.overdriveLimit.memoryClock :
					od_table[2]->entries[i].clk;
	od_table[2]->entries[i].vddc = odn_table->max_vddc > od_table[2]->entries[i].vddc ?
					odn_table->max_vddc :
					od_table[2]->entries[i].vddc;

	return 0;
}

static void vega10_init_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	int i;
	uint32_t sub_vendor_id, hw_revision;
	uint32_t top32, bottom32;
	struct amdgpu_device *adev = hwmgr->adev;

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
	data->smu_features[GNLD_ACG].smu_feature_id = FEATURE_ACG_BIT;
	data->smu_features[GNLD_DIDT].smu_feature_id = FEATURE_GFX_EDC_BIT;
	data->smu_features[GNLD_PCC_LIMIT].smu_feature_id = FEATURE_PCC_LIMIT_CONTROL_BIT;

	if (!data->registry_data.prefetcher_dpm_key_disabled)
		data->smu_features[GNLD_DPM_PREFETCHER].supported = true;

	if (!data->registry_data.sclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_GFXCLK].supported = true;

	if (!data->registry_data.mclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_UCLK].supported = true;

	if (!data->registry_data.socclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_SOCCLK].supported = true;

	if (PP_CAP(PHM_PlatformCaps_UVDDPM))
		data->smu_features[GNLD_DPM_UVD].supported = true;

	if (PP_CAP(PHM_PlatformCaps_VCEDPM))
		data->smu_features[GNLD_DPM_VCE].supported = true;

	data->smu_features[GNLD_DPM_LINK].supported = true;

	if (!data->registry_data.dcefclk_dpm_key_disabled)
		data->smu_features[GNLD_DPM_DCEFCLK].supported = true;

	if (PP_CAP(PHM_PlatformCaps_SclkDeepSleep) &&
	    data->registry_data.sclk_deep_sleep_support) {
		data->smu_features[GNLD_DS_GFXCLK].supported = true;
		data->smu_features[GNLD_DS_SOCCLK].supported = true;
		data->smu_features[GNLD_DS_LCLK].supported = true;
		data->smu_features[GNLD_DS_DCEFCLK].supported = true;
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

	smum_send_msg_to_smc(hwmgr,
			PPSMC_MSG_GetSmuVersion,
			&hwmgr->smu_version);
		/* ACG firmware has major version 5 */
	if ((hwmgr->smu_version & 0xff000000) == 0x5000000)
		data->smu_features[GNLD_ACG].supported = true;
	if (data->registry_data.didt_support)
		data->smu_features[GNLD_DIDT].supported = true;

	hw_revision = adev->pdev->revision;
	sub_vendor_id = adev->pdev->subsystem_vendor;

	if ((hwmgr->chip_id == 0x6862 ||
		hwmgr->chip_id == 0x6861 ||
		hwmgr->chip_id == 0x6868) &&
		(hw_revision == 0) &&
		(sub_vendor_id != 0x1002))
		data->smu_features[GNLD_PCC_LIMIT].supported = true;

	/* Get the SN to turn into a Unique ID */
	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ReadSerialNumTop32, &top32);
	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ReadSerialNumBottom32, &bottom32);

	adev->unique_id = ((uint64_t)bottom32 << 32) | top32;
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
 * vega10_get_evv_voltages - Get Leakage VDDC based on leakage ID.
 *
 * @hwmgr:  the address of the powerplay hardware manager.
 * return:  always 0.
 */
static int vega10_get_evv_voltages(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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
			if (PP_CAP(PHM_PlatformCaps_ClockStretcher)) {
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
 * vega10_patch_with_vdd_leakage - Change virtual leakage voltage to actual value.
 *
 * @hwmgr:         the address of the powerplay hardware manager.
 * @voltage:       pointer to changing voltage
 * @leakage_table: pointer to leakage table
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
		pr_info("Voltage value looks like a Leakage ID but it's not patched\n");
}

/**
 * vega10_patch_lookup_table_with_leakage - Patch voltage lookup table by EVV leakages.
 *
 * @hwmgr:         the address of the powerplay hardware manager.
 * @lookup_table:  pointer to voltage lookup table
 * @leakage_table: pointer to leakage table
 * return:         always 0
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
	uint8_t entry_id, voltage_id;
	unsigned i;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct phm_ppt_v1_clock_voltage_dependency_table *mclk_table =
			table_info->vdd_dep_on_mclk;

	for (i = 0; i < 6; i++) {
		struct phm_ppt_v1_clock_voltage_dependency_table *vdt;
		switch (i) {
			case 0: vdt = table_info->vdd_dep_on_socclk; break;
			case 1: vdt = table_info->vdd_dep_on_sclk; break;
			case 2: vdt = table_info->vdd_dep_on_dcefclk; break;
			case 3: vdt = table_info->vdd_dep_on_pixclk; break;
			case 4: vdt = table_info->vdd_dep_on_dispclk; break;
			case 5: vdt = table_info->vdd_dep_on_phyclk; break;
		}

		for (entry_id = 0; entry_id < vdt->count; entry_id++) {
			voltage_id = vdt->entries[entry_id].vddInd;
			vdt->entries[entry_id].vddc =
					table_info->vddc_lookup_table->entries[voltage_id].us_vdd;
		}
	}

	for (entry_id = 0; entry_id < mm_table->count; ++entry_id) {
		voltage_id = mm_table->entries[entry_id].vddcInd;
		mm_table->entries[entry_id].vddc =
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


	return 0;

}

static int vega10_sort_lookup_table(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_voltage_lookup_table *lookup_table)
{
	uint32_t table_size, i, j;

	PP_ASSERT_WITH_CODE(lookup_table && lookup_table->count,
		"Lookup table is empty", return -EINVAL);

	table_size = lookup_table->count;

	/* Sorting voltages */
	for (i = 0; i < table_size - 1; i++) {
		for (j = i + 1; j > 0; j--) {
			if (lookup_table->entries[j].us_vdd <
					lookup_table->entries[j - 1].us_vdd) {
				swap(lookup_table->entries[j - 1],
				     lookup_table->entries[j]);
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
	struct vega10_hwmgr *data = hwmgr->backend;

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
		"VDD dependency on SCLK table is missing. This table is mandatory", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_sclk_vdd_table->count >= 1,
		"VDD dependency on SCLK table is empty. This table is mandatory", return -EINVAL);

	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table,
		"VDD dependency on MCLK table is missing.  This table is mandatory", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table->count >= 1,
		"VDD dependency on MCLK table is empty.  This table is mandatory", return -EINVAL);

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
	struct amdgpu_device *adev = hwmgr->adev;

	data = kzalloc(sizeof(struct vega10_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	hwmgr->workload_mask = 1 << hwmgr->workload_prority[PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT];
	hwmgr->power_profile_mode = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;
	hwmgr->default_power_profile_mode = PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT;

	vega10_set_default_registry_data(hwmgr);
	data->disable_dpm_mask = 0xff;

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
	if (PP_CAP(PHM_PlatformCaps_ControlVDDCI)) {
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

	data->total_active_cus = adev->gfx.cu_info.number;
	if (!hwmgr->not_vf)
		return result;

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

	data->mem_channels = (RREG32_SOC15(DF, 0, mmDF_CS_AON0_DramBaseAddress0) &
			DF_CS_AON0_DramBaseAddress0__IntLvNumChan_MASK) >>
			DF_CS_AON0_DramBaseAddress0__IntLvNumChan__SHIFT;
	PP_ASSERT_WITH_CODE(data->mem_channels < ARRAY_SIZE(channel_number),
			"Mem Channel Index Exceeded maximum!",
			return -EINVAL);

	return result;
}

static int vega10_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	data->low_sclk_interrupt_threshold = 0;

	return 0;
}

static int vega10_setup_dpm_led_config(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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
	if (!hwmgr->not_vf)
		return 0;

	PP_ASSERT_WITH_CODE(!vega10_init_sclk_threshold(hwmgr),
			"Failed to init sclk threshold!",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(!vega10_setup_dpm_led_config(hwmgr),
			"Failed to set up led dpm config!",
			return -EINVAL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_NumOfDisplays,
				0,
				NULL);

	return 0;
}

/**
 * vega10_trim_voltage_table - Remove repeated voltage values and create table with unique values.
 *
 * @hwmgr:      the address of the powerplay hardware manager.
 * @vol_table:  the pointer to changing voltage table
 * return:      0 in success
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
 * vega10_construct_voltage_tables - Create Voltage Tables.
 *
 * @hwmgr:  the address of the powerplay hardware manager.
 * return:  always 0
 */
static int vega10_construct_voltage_tables(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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
 * vega10_init_dpm_state
 * Function to initialize all Soft Min/Max and Hard Min/Max to 0xff.
 *
 * @dpm_state: - the address of the DPM Table to initiailize.
 * return:   None.
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

	dpm_table->count = 0;

	for (i = 0; i < dep_table->count; i++) {
		if (i == 0 || dpm_table->dpm_levels[dpm_table->count - 1].value <=
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
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_pcie_table *pcie_table = &(data->dpm_table.pcie_table);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_pcie_table *bios_pcie_table =
			table_info->pcie_table;
	uint32_t i;

	PP_ASSERT_WITH_CODE(bios_pcie_table->count,
			"Incorrect number of PCIE States from VBIOS!",
			return -1);

	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		if (data->registry_data.pcieSpeedOverride)
			pcie_table->pcie_gen[i] =
					data->registry_data.pcieSpeedOverride;
		else
			pcie_table->pcie_gen[i] =
					bios_pcie_table->entries[i].gen_speed;

		if (data->registry_data.pcieLaneOverride)
			pcie_table->pcie_lane[i] = (uint8_t)encode_pcie_lane_width(
					data->registry_data.pcieLaneOverride);
		else
			pcie_table->pcie_lane[i] = (uint8_t)encode_pcie_lane_width(
							bios_pcie_table->entries[i].lane_width);
		if (data->registry_data.pcieClockOverride)
			pcie_table->lclk[i] =
					data->registry_data.pcieClockOverride;
		else
			pcie_table->lclk[i] =
					bios_pcie_table->entries[i].pcie_sclk;
	}

	pcie_table->count = NUM_LINK_LEVELS;

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
	struct vega10_hwmgr *data = hwmgr->backend;
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
	dpm_table = &(data->dpm_table.soc_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_soc_table);

	vega10_init_dpm_state(&(dpm_table->dpm_state));

	dpm_table = &(data->dpm_table.gfx_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_gfx_table);
	if (hwmgr->platform_descriptor.overdriveLimit.engineClock == 0)
		hwmgr->platform_descriptor.overdriveLimit.engineClock =
					dpm_table->dpm_levels[dpm_table->count-1].value;
	vega10_init_dpm_state(&(dpm_table->dpm_state));

	/* Initialize Mclk DPM table based on allow Mclk values */
	data->dpm_table.mem_table.count = 0;
	dpm_table = &(data->dpm_table.mem_table);
	vega10_setup_default_single_dpm_table(hwmgr,
			dpm_table,
			dep_mclk_table);
	if (hwmgr->platform_descriptor.overdriveLimit.memoryClock == 0)
		hwmgr->platform_descriptor.overdriveLimit.memoryClock =
					dpm_table->dpm_levels[dpm_table->count-1].value;
	vega10_init_dpm_state(&(dpm_table->dpm_state));

	data->dpm_table.eclk_table.count = 0;
	dpm_table = &(data->dpm_table.eclk_table);
	for (i = 0; i < dep_mm_table->count; i++) {
		if (i == 0 || dpm_table->dpm_levels
				[dpm_table->count - 1].value <=
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
				[dpm_table->count - 1].value <=
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
				[dpm_table->count - 1].value <=
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

	/* Zero out the saved copy of the CUSTOM profile
	 * This will be checked when trying to set the profile
	 * and will require that new values be passed in
	 */
	data->custom_profile_mode[0] = 0;
	data->custom_profile_mode[1] = 0;
	data->custom_profile_mode[2] = 0;
	data->custom_profile_mode[3] = 0;

	/* save a copy of the default DPM table */
	memcpy(&(data->golden_dpm_table), &(data->dpm_table),
			sizeof(struct vega10_dpm_table));

	return 0;
}

/*
 * vega10_populate_ulv_state
 * Function to provide parameters for Utral Low Voltage state to SMC.
 *
 * @hwmgr: - the address of the hardware manager.
 * return:   Always 0.
 */
static int vega10_populate_ulv_state(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);

	data->smc_state_table.pp_table.UlvOffsetVid =
			(uint8_t)table_info->us_ulv_voltage_offset;

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

static int vega10_override_pcie_parameters(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)(hwmgr->adev);
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	uint32_t pcie_gen = 0, pcie_width = 0;
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	int i;

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

	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		if (pp_table->PcieGenSpeed[i] > pcie_gen)
			pp_table->PcieGenSpeed[i] = pcie_gen;

		if (pp_table->PcieLaneCount[i] > pcie_width)
			pp_table->PcieLaneCount[i] = pcie_width;
	}

	if (data->registry_data.pcie_dpm_key_disabled) {
		for (i = 0; i < NUM_LINK_LEVELS; i++) {
			pp_table->PcieGenSpeed[i] = pcie_gen;
			pp_table->PcieLaneCount[i] = pcie_width;
		}
	}

	return 0;
}

static int vega10_populate_smc_link_levels(struct pp_hwmgr *hwmgr)
{
	int result = -1;
	struct vega10_hwmgr *data = hwmgr->backend;
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
 * vega10_populate_single_gfx_level - Populates single SMC GFXSCLK structure
 *                                    using the provided engine clock
 *
 * @hwmgr:      the address of the hardware manager
 * @gfx_clock:  the GFX clock to use to populate the structure.
 * @current_gfxclk_level:  location in PPTable for the SMC GFXCLK structure.
 * @acg_freq:   ACG frequenty to return (MHz)
 */
static int vega10_populate_single_gfx_level(struct pp_hwmgr *hwmgr,
		uint32_t gfx_clock, PllSetting_t *current_gfxclk_level,
		uint32_t *acg_freq)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_on_sclk;
	struct vega10_hwmgr *data = hwmgr->backend;
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;
	uint32_t gfx_max_clock =
			hwmgr->platform_descriptor.overdriveLimit.engineClock;
	uint32_t i = 0;

	if (hwmgr->od_enabled)
		dep_on_sclk = (struct phm_ppt_v1_clock_voltage_dependency_table *)
						&(data->odn_dpm_table.vdd_dep_on_sclk);
	else
		dep_on_sclk = table_info->vdd_dep_on_sclk;

	PP_ASSERT_WITH_CODE(dep_on_sclk,
			"Invalid SOC_VDD-GFX_CLK Dependency Table!",
			return -EINVAL);

	if (data->need_update_dpm_table & DPMTABLE_OD_UPDATE_SCLK)
		gfx_clock = gfx_clock > gfx_max_clock ? gfx_max_clock : gfx_clock;
	else {
		for (i = 0; i < dep_on_sclk->count; i++) {
			if (dep_on_sclk->entries[i].clk == gfx_clock)
				break;
		}
		PP_ASSERT_WITH_CODE(dep_on_sclk->count > i,
				"Cannot find gfx_clk in SOC_VDD-GFX_CLK!",
				return -EINVAL);
	}

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

	*acg_freq = gfx_clock / 100; /* 100 Khz to Mhz conversion */

	return 0;
}

/**
 * vega10_populate_single_soc_level - Populates single SMC SOCCLK structure
 *                                    using the provided clock.
 *
 * @hwmgr:     the address of the hardware manager.
 * @soc_clock: the SOC clock to use to populate the structure.
 * @current_soc_did:   DFS divider to pass back to caller
 * @current_vol_index: index of current VDD to pass back to caller
 * return:      0 on success
 */
static int vega10_populate_single_soc_level(struct pp_hwmgr *hwmgr,
		uint32_t soc_clock, uint8_t *current_soc_did,
		uint8_t *current_vol_index)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_on_soc;
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;
	uint32_t i;

	if (hwmgr->od_enabled) {
		dep_on_soc = (struct phm_ppt_v1_clock_voltage_dependency_table *)
						&data->odn_dpm_table.vdd_dep_on_socclk;
		for (i = 0; i < dep_on_soc->count; i++) {
			if (dep_on_soc->entries[i].clk >= soc_clock)
				break;
		}
	} else {
		dep_on_soc = table_info->vdd_dep_on_socclk;
		for (i = 0; i < dep_on_soc->count; i++) {
			if (dep_on_soc->entries[i].clk == soc_clock)
				break;
		}
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

/**
 * vega10_populate_all_graphic_levels - Populates all SMC SCLK levels' structure
 *                                      based on the trimmed allowed dpm engine clock states
 *
 * @hwmgr:      the address of the hardware manager
 */
static int vega10_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega10_single_dpm_table *dpm_table = &(data->dpm_table.gfx_table);
	int result = 0;
	uint32_t i, j;

	for (i = 0; i < dpm_table->count; i++) {
		result = vega10_populate_single_gfx_level(hwmgr,
				dpm_table->dpm_levels[i].value,
				&(pp_table->GfxclkLevel[i]),
				&(pp_table->AcgFreqTable[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_GFXCLK_DPM_LEVELS) {
		result = vega10_populate_single_gfx_level(hwmgr,
				dpm_table->dpm_levels[j].value,
				&(pp_table->GfxclkLevel[i]),
				&(pp_table->AcgFreqTable[i]));
		if (result)
			return result;
		i++;
	}

	pp_table->GfxclkSlewRate =
			cpu_to_le16(table_info->us_gfxclk_slew_rate);

	dpm_table = &(data->dpm_table.soc_table);
	for (i = 0; i < dpm_table->count; i++) {
		result = vega10_populate_single_soc_level(hwmgr,
				dpm_table->dpm_levels[i].value,
				&(pp_table->SocclkDid[i]),
				&(pp_table->SocDpmVoltageIndex[i]));
		if (result)
			return result;
	}

	j = i - 1;
	while (i < NUM_SOCCLK_DPM_LEVELS) {
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

static void vega10_populate_vddc_soc_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct phm_ppt_v2_information *table_info = hwmgr->pptable;
	struct phm_ppt_v1_voltage_lookup_table *vddc_lookup_table;

	uint8_t soc_vid = 0;
	uint32_t i, max_vddc_level;

	if (hwmgr->od_enabled)
		vddc_lookup_table = (struct phm_ppt_v1_voltage_lookup_table *)&data->odn_dpm_table.vddc_lookup_table;
	else
		vddc_lookup_table = table_info->vddc_lookup_table;

	max_vddc_level = vddc_lookup_table->count;
	for (i = 0; i < max_vddc_level; i++) {
		soc_vid = (uint8_t)convert_to_vid(vddc_lookup_table->entries[i].us_vdd);
		pp_table->SocVid[i] = soc_vid;
	}
	while (i < MAX_REGULAR_DPM_NUMBER) {
		pp_table->SocVid[i] = soc_vid;
		i++;
	}
}

/*
 * Populates single SMC GFXCLK structure using the provided clock.
 *
 * @hwmgr:     the address of the hardware manager.
 * @mem_clock: the memory clock to use to populate the structure.
 * return:     0 on success..
 */
static int vega10_populate_single_memory_level(struct pp_hwmgr *hwmgr,
		uint32_t mem_clock, uint8_t *current_mem_vid,
		PllSetting_t *current_memclk_level, uint8_t *current_mem_soc_vind)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_on_mclk;
	struct pp_atomfwctrl_clock_dividers_soc15 dividers;
	uint32_t mem_max_clock =
			hwmgr->platform_descriptor.overdriveLimit.memoryClock;
	uint32_t i = 0;

	if (hwmgr->od_enabled)
		dep_on_mclk = (struct phm_ppt_v1_clock_voltage_dependency_table *)
					&data->odn_dpm_table.vdd_dep_on_mclk;
	else
		dep_on_mclk = table_info->vdd_dep_on_mclk;

	PP_ASSERT_WITH_CODE(dep_on_mclk,
			"Invalid SOC_VDD-UCLK Dependency Table!",
			return -EINVAL);

	if (data->need_update_dpm_table & DPMTABLE_OD_UPDATE_MCLK) {
		mem_clock = mem_clock > mem_max_clock ? mem_max_clock : mem_clock;
	} else {
		for (i = 0; i < dep_on_mclk->count; i++) {
			if (dep_on_mclk->entries[i].clk == mem_clock)
				break;
		}
		PP_ASSERT_WITH_CODE(dep_on_mclk->count > i,
				"Cannot find UCLK in SOC_VDD-UCLK Dependency Table!",
				return -EINVAL);
	}

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
 * vega10_populate_all_memory_levels - Populates all SMC MCLK levels' structure
 *                                     based on the trimmed allowed dpm memory clock states.
 *
 * @hwmgr:  the address of the hardware manager.
 * return:   PP_Result_OK on success.
 */
static int vega10_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct vega10_single_dpm_table *dpm_table =
			&(data->dpm_table.mem_table);
	int result = 0;
	uint32_t i, j;

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

	pp_table->NumMemoryChannels = (uint16_t)(data->mem_channels);
	pp_table->MemoryChannelWidth =
			(uint16_t)(HBM_MEMORY_CHANNEL_WIDTH *
					channel_number[data->mem_channels]);

	pp_table->LowestUclkReservedForUlv =
			(uint8_t)(data->lowest_uclk_reserved_for_ulv);

	return result;
}

static int vega10_populate_single_display_type(struct pp_hwmgr *hwmgr,
		DSPCLK_e disp_clock)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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
	struct vega10_hwmgr *data = hwmgr->backend;
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
	struct vega10_hwmgr *data = hwmgr->backend;
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
	struct vega10_hwmgr *data = hwmgr->backend;
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_sclk;
	uint32_t i;

	for (i = 0; i < dep_table->count; i++) {
		pp_table->CksEnable[i] = dep_table->entries[i].cks_enable;
		pp_table->CksVidOffset[i] = (uint8_t)(dep_table->entries[i].cks_voffset
				* VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1);
	}

	return 0;
}

static int vega10_populate_avfs_parameters(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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
					convert_to_vid((uint16_t)(avfs_params.ulMinVddc));
			pp_table->MaxVoltageVid = (uint8_t)
					convert_to_vid((uint16_t)(avfs_params.ulMaxVddc));

			pp_table->AConstant[0] = cpu_to_le32(avfs_params.ulMeanNsigmaAcontant0);
			pp_table->AConstant[1] = cpu_to_le32(avfs_params.ulMeanNsigmaAcontant1);
			pp_table->AConstant[2] = cpu_to_le32(avfs_params.ulMeanNsigmaAcontant2);
			pp_table->DC_tol_sigma = cpu_to_le16(avfs_params.usMeanNsigmaDcTolSigma);
			pp_table->Platform_mean = cpu_to_le16(avfs_params.usMeanNsigmaPlatformMean);
			pp_table->Platform_sigma = cpu_to_le16(avfs_params.usMeanNsigmaDcTolSigma);
			pp_table->PSM_Age_CompFactor = cpu_to_le16(avfs_params.usPsmAgeComfactor);

			pp_table->BtcGbVdroopTableCksOff.a0 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksoffA0);
			pp_table->BtcGbVdroopTableCksOff.a0_shift = 20;
			pp_table->BtcGbVdroopTableCksOff.a1 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksoffA1);
			pp_table->BtcGbVdroopTableCksOff.a1_shift = 20;
			pp_table->BtcGbVdroopTableCksOff.a2 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksoffA2);
			pp_table->BtcGbVdroopTableCksOff.a2_shift = 20;

			pp_table->OverrideBtcGbCksOn = avfs_params.ucEnableGbVdroopTableCkson;
			pp_table->BtcGbVdroopTableCksOn.a0 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksonA0);
			pp_table->BtcGbVdroopTableCksOn.a0_shift = 20;
			pp_table->BtcGbVdroopTableCksOn.a1 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksonA1);
			pp_table->BtcGbVdroopTableCksOn.a1_shift = 20;
			pp_table->BtcGbVdroopTableCksOn.a2 =
					cpu_to_le32(avfs_params.ulGbVdroopTableCksonA2);
			pp_table->BtcGbVdroopTableCksOn.a2_shift = 20;

			pp_table->AvfsGbCksOn.m1 =
					cpu_to_le32(avfs_params.ulGbFuseTableCksonM1);
			pp_table->AvfsGbCksOn.m2 =
					cpu_to_le32(avfs_params.ulGbFuseTableCksonM2);
			pp_table->AvfsGbCksOn.b =
					cpu_to_le32(avfs_params.ulGbFuseTableCksonB);
			pp_table->AvfsGbCksOn.m1_shift = 24;
			pp_table->AvfsGbCksOn.m2_shift = 12;
			pp_table->AvfsGbCksOn.b_shift = 0;

			pp_table->OverrideAvfsGbCksOn =
					avfs_params.ucEnableGbFuseTableCkson;
			pp_table->AvfsGbCksOff.m1 =
					cpu_to_le32(avfs_params.ulGbFuseTableCksoffM1);
			pp_table->AvfsGbCksOff.m2 =
					cpu_to_le32(avfs_params.ulGbFuseTableCksoffM2);
			pp_table->AvfsGbCksOff.b =
					cpu_to_le32(avfs_params.ulGbFuseTableCksoffB);
			pp_table->AvfsGbCksOff.m1_shift = 24;
			pp_table->AvfsGbCksOff.m2_shift = 12;
			pp_table->AvfsGbCksOff.b_shift = 0;

			for (i = 0; i < dep_table->count; i++)
				pp_table->StaticVoltageOffsetVid[i] =
						convert_to_vid((uint8_t)(dep_table->entries[i].sclk_offset));

			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->disp_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->disp_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m1 =
						(int32_t)data->disp_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m2 =
						(int32_t)data->disp_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].b =
						(int32_t)data->disp_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m1 =
						(int32_t)avfs_params.ulDispclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m2 =
						(int32_t)avfs_params.ulDispclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].b =
						(int32_t)avfs_params.ulDispclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].m2_shift = 12;
			pp_table->DisplayClock2Gfxclk[DSPCLK_DISPCLK].b_shift = 12;

			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->dcef_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->dcef_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m1 =
						(int32_t)data->dcef_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m2 =
						(int32_t)data->dcef_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].b =
						(int32_t)data->dcef_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m1 =
						(int32_t)avfs_params.ulDcefclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m2 =
						(int32_t)avfs_params.ulDcefclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].b =
						(int32_t)avfs_params.ulDcefclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].m2_shift = 12;
			pp_table->DisplayClock2Gfxclk[DSPCLK_DCEFCLK].b_shift = 12;

			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->pixel_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->pixel_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m1 =
						(int32_t)data->pixel_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m2 =
						(int32_t)data->pixel_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].b =
						(int32_t)data->pixel_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m1 =
						(int32_t)avfs_params.ulPixelclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m2 =
						(int32_t)avfs_params.ulPixelclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].b =
						(int32_t)avfs_params.ulPixelclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].m2_shift = 12;
			pp_table->DisplayClock2Gfxclk[DSPCLK_PIXCLK].b_shift = 12;
			if ((PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->phy_clk_quad_eqn_a) &&
				(PPREGKEY_VEGA10QUADRATICEQUATION_DFLT !=
					data->phy_clk_quad_eqn_b)) {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m1 =
						(int32_t)data->phy_clk_quad_eqn_a;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m2 =
						(int32_t)data->phy_clk_quad_eqn_b;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].b =
						(int32_t)data->phy_clk_quad_eqn_c;
			} else {
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m1 =
						(int32_t)avfs_params.ulPhyclk2GfxclkM1;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m2 =
						(int32_t)avfs_params.ulPhyclk2GfxclkM2;
				pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].b =
						(int32_t)avfs_params.ulPhyclk2GfxclkB;
			}

			pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m1_shift = 24;
			pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].m2_shift = 12;
			pp_table->DisplayClock2Gfxclk[DSPCLK_PHYCLK].b_shift = 12;

			pp_table->AcgBtcGbVdroopTable.a0       = avfs_params.ulAcgGbVdroopTableA0;
			pp_table->AcgBtcGbVdroopTable.a0_shift = 20;
			pp_table->AcgBtcGbVdroopTable.a1       = avfs_params.ulAcgGbVdroopTableA1;
			pp_table->AcgBtcGbVdroopTable.a1_shift = 20;
			pp_table->AcgBtcGbVdroopTable.a2       = avfs_params.ulAcgGbVdroopTableA2;
			pp_table->AcgBtcGbVdroopTable.a2_shift = 20;

			pp_table->AcgAvfsGb.m1                   = avfs_params.ulAcgGbFuseTableM1;
			pp_table->AcgAvfsGb.m2                   = avfs_params.ulAcgGbFuseTableM2;
			pp_table->AcgAvfsGb.b                    = avfs_params.ulAcgGbFuseTableB;
			pp_table->AcgAvfsGb.m1_shift             = 24;
			pp_table->AcgAvfsGb.m2_shift             = 12;
			pp_table->AcgAvfsGb.b_shift              = 0;

		} else {
			data->smu_features[GNLD_AVFS].supported = false;
		}
	}

	return 0;
}

static int vega10_acg_enable(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t agc_btc_response;

	if (data->smu_features[GNLD_ACG].supported) {
		if (0 == vega10_enable_smc_features(hwmgr, true,
					data->smu_features[GNLD_DPM_PREFETCHER].smu_feature_bitmap))
			data->smu_features[GNLD_DPM_PREFETCHER].enabled = true;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_InitializeAcg, NULL);

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_RunAcgBtc, &agc_btc_response);

		if (1 == agc_btc_response) {
			if (1 == data->acg_loop_state)
				smum_send_msg_to_smc(hwmgr, PPSMC_MSG_RunAcgInClosedLoop, NULL);
			else if (2 == data->acg_loop_state)
				smum_send_msg_to_smc(hwmgr, PPSMC_MSG_RunAcgInOpenLoop, NULL);
			if (0 == vega10_enable_smc_features(hwmgr, true,
				data->smu_features[GNLD_ACG].smu_feature_bitmap))
					data->smu_features[GNLD_ACG].enabled = true;
		} else {
			pr_info("[ACG_Enable] ACG BTC Returned Failed Status!\n");
			data->smu_features[GNLD_ACG].enabled = false;
		}
	}

	return 0;
}

static int vega10_acg_disable(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_ACG].supported &&
	    data->smu_features[GNLD_ACG].enabled)
		if (!vega10_enable_smc_features(hwmgr, false,
			data->smu_features[GNLD_ACG].smu_feature_bitmap))
			data->smu_features[GNLD_ACG].enabled = false;

	return 0;
}

static int vega10_populate_gpio_parameters(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct pp_atomfwctrl_gpio_parameters gpio_params = {0};
	int result;

	result = pp_atomfwctrl_get_gpio_information(hwmgr, &gpio_params);
	if (!result) {
		if (PP_CAP(PHM_PlatformCaps_RegulatorHot) &&
		    data->registry_data.regulator_hot_gpio_support) {
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

		if (PP_CAP(PHM_PlatformCaps_AutomaticDCTransition) &&
		    data->registry_data.ac_dc_switch_gpio_support) {
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
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_AVFS].supported) {
		/* Already enabled or disabled */
		if (!(enable ^ data->smu_features[GNLD_AVFS].enabled))
			return 0;

		if (enable) {
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
					true,
					data->smu_features[GNLD_AVFS].smu_feature_bitmap),
					"[avfs_control] Attempt to Enable AVFS feature Failed!",
					return -1);
			data->smu_features[GNLD_AVFS].enabled = true;
		} else {
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
					false,
					data->smu_features[GNLD_AVFS].smu_feature_bitmap),
					"[avfs_control] Attempt to Disable AVFS feature Failed!",
					return -1);
			data->smu_features[GNLD_AVFS].enabled = false;
		}
	}

	return 0;
}

static int vega10_update_avfs(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->need_update_dpm_table & DPMTABLE_OD_UPDATE_VDDC) {
		vega10_avfs_enable(hwmgr, false);
	} else if (data->need_update_dpm_table) {
		vega10_avfs_enable(hwmgr, false);
		vega10_avfs_enable(hwmgr, true);
	} else {
		vega10_avfs_enable(hwmgr, true);
	}

	return 0;
}

static int vega10_populate_and_upload_avfs_fuse_override(struct pp_hwmgr *hwmgr)
{
	int result = 0;

	uint64_t serial_number = 0;
	uint32_t top32, bottom32;
	struct phm_fuses_default fuse;

	struct vega10_hwmgr *data = hwmgr->backend;
	AvfsFuseOverride_t *avfs_fuse_table = &(data->smc_state_table.avfs_fuse_override_table);

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ReadSerialNumTop32, &top32);

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ReadSerialNumBottom32, &bottom32);

	serial_number = ((uint64_t)bottom32 << 32) | top32;

	if (pp_override_get_default_fuse_value(serial_number, &fuse) == 0) {
		avfs_fuse_table->VFT0_b  = fuse.VFT0_b;
		avfs_fuse_table->VFT0_m1 = fuse.VFT0_m1;
		avfs_fuse_table->VFT0_m2 = fuse.VFT0_m2;
		avfs_fuse_table->VFT1_b  = fuse.VFT1_b;
		avfs_fuse_table->VFT1_m1 = fuse.VFT1_m1;
		avfs_fuse_table->VFT1_m2 = fuse.VFT1_m2;
		avfs_fuse_table->VFT2_b  = fuse.VFT2_b;
		avfs_fuse_table->VFT2_m1 = fuse.VFT2_m1;
		avfs_fuse_table->VFT2_m2 = fuse.VFT2_m2;
		result = smum_smc_table_manager(hwmgr,  (uint8_t *)avfs_fuse_table,
						AVFSFUSETABLE, false);
		PP_ASSERT_WITH_CODE(!result,
			"Failed to upload FuseOVerride!",
			);
	}

	return result;
}

static void vega10_check_dpm_table_updated(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_odn_dpm_table *odn_table = &(data->odn_dpm_table);
	struct phm_ppt_v2_information *table_info = hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table;
	struct phm_ppt_v1_clock_voltage_dependency_table *odn_dep_table;
	uint32_t i;

	dep_table = table_info->vdd_dep_on_mclk;
	odn_dep_table = (struct phm_ppt_v1_clock_voltage_dependency_table *)&(odn_table->vdd_dep_on_mclk);

	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].vddc != odn_dep_table->entries[i].vddc) {
			data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_VDDC | DPMTABLE_OD_UPDATE_MCLK;
			return;
		}
	}

	dep_table = table_info->vdd_dep_on_sclk;
	odn_dep_table = (struct phm_ppt_v1_clock_voltage_dependency_table *)&(odn_table->vdd_dep_on_sclk);
	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].vddc != odn_dep_table->entries[i].vddc) {
			data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_VDDC | DPMTABLE_OD_UPDATE_SCLK;
			return;
		}
	}
}

/**
 * vega10_init_smc_table - Initializes the SMC table and uploads it
 *
 * @hwmgr:  the address of the powerplay hardware manager.
 * return:  always 0
 */
static int vega10_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	PPTable_t *pp_table = &(data->smc_state_table.pp_table);
	struct pp_atomfwctrl_voltage_table voltage_table;
	struct pp_atomfwctrl_bios_boot_up_values boot_up_values;
	struct vega10_odn_dpm_table *odn_table = &(data->odn_dpm_table);

	result = vega10_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to setup default DPM tables!",
			return result);

	if (!hwmgr->not_vf)
		return 0;

	/* initialize ODN table */
	if (hwmgr->od_enabled) {
		if (odn_table->max_vddc) {
			data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_OD_UPDATE_MCLK;
			vega10_check_dpm_table_updated(hwmgr);
		} else {
			vega10_odn_initial_default_setting(hwmgr);
		}
	}

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

	data->vddc_voltage_table.psi0_enable = voltage_table.psi0_enable;
	data->vddc_voltage_table.psi1_enable = voltage_table.psi1_enable;

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

	result = vega10_override_pcie_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to override pcie parameters!",
			return result);

	result = vega10_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize Graphics Level!",
			return result);

	result = vega10_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize Memory Level!",
			return result);

	vega10_populate_vddc_soc_levels(hwmgr);

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

	if (data->registry_data.clock_stretcher_support) {
		result = vega10_populate_clock_stretcher_table(hwmgr);
		PP_ASSERT_WITH_CODE(!result,
				"Failed to populate Clock Stretcher Table!",
				return result);
	}

	result = pp_atomfwctrl_get_vbios_bootup_values(hwmgr, &boot_up_values);
	if (!result) {
		data->vbios_boot_state.vddc     = boot_up_values.usVddc;
		data->vbios_boot_state.vddci    = boot_up_values.usVddci;
		data->vbios_boot_state.mvddc    = boot_up_values.usMvddc;
		data->vbios_boot_state.gfx_clock = boot_up_values.ulGfxClk;
		data->vbios_boot_state.mem_clock = boot_up_values.ulUClk;
		pp_atomfwctrl_get_clk_information_by_clkid(hwmgr,
				SMU9_SYSPLL0_SOCCLK_ID, 0, &boot_up_values.ulSocClk);

		pp_atomfwctrl_get_clk_information_by_clkid(hwmgr,
				SMU9_SYSPLL0_DCEFCLK_ID, 0, &boot_up_values.ulDCEFClk);

		data->vbios_boot_state.soc_clock = boot_up_values.ulSocClk;
		data->vbios_boot_state.dcef_clock = boot_up_values.ulDCEFClk;
		if (0 != boot_up_values.usVddc) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetFloorSocVoltage,
						(boot_up_values.usVddc * 4),
						NULL);
			data->vbios_boot_state.bsoc_vddc_lock = true;
		} else {
			data->vbios_boot_state.bsoc_vddc_lock = false;
		}
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetMinDeepSleepDcefclk,
			(uint32_t)(data->vbios_boot_state.dcef_clock / 100),
				NULL);
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

	vega10_populate_and_upload_avfs_fuse_override(hwmgr);

	result = smum_smc_table_manager(hwmgr, (uint8_t *)pp_table, PPTABLE, false);

	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload PPtable!", return result);

	result = vega10_avfs_enable(hwmgr, true);
	PP_ASSERT_WITH_CODE(!result, "Attempt to enable AVFS feature Failed!",
					return result);
	vega10_acg_enable(hwmgr);

	return 0;
}

static int vega10_enable_thermal_protection(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_THERMAL].supported) {
		if (data->smu_features[GNLD_THERMAL].enabled)
			pr_info("THERMAL Feature Already enabled!");

		PP_ASSERT_WITH_CODE(
				!vega10_enable_smc_features(hwmgr,
				true,
				data->smu_features[GNLD_THERMAL].smu_feature_bitmap),
				"Enable THERMAL Feature Failed!",
				return -1);
		data->smu_features[GNLD_THERMAL].enabled = true;
	}

	return 0;
}

static int vega10_disable_thermal_protection(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_THERMAL].supported) {
		if (!data->smu_features[GNLD_THERMAL].enabled)
			pr_info("THERMAL Feature Already disabled!");

		PP_ASSERT_WITH_CODE(
				!vega10_enable_smc_features(hwmgr,
				false,
				data->smu_features[GNLD_THERMAL].smu_feature_bitmap),
				"disable THERMAL Feature Failed!",
				return -1);
		data->smu_features[GNLD_THERMAL].enabled = false;
	}

	return 0;
}

static int vega10_enable_vrhot_feature(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (PP_CAP(PHM_PlatformCaps_RegulatorHot)) {
		if (data->smu_features[GNLD_VR0HOT].supported) {
			PP_ASSERT_WITH_CODE(
					!vega10_enable_smc_features(hwmgr,
					true,
					data->smu_features[GNLD_VR0HOT].smu_feature_bitmap),
					"Attempt to Enable VR0 Hot feature Failed!",
					return -1);
			data->smu_features[GNLD_VR0HOT].enabled = true;
		} else {
			if (data->smu_features[GNLD_VR1HOT].supported) {
				PP_ASSERT_WITH_CODE(
						!vega10_enable_smc_features(hwmgr,
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
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->registry_data.ulv_support) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				true, data->smu_features[GNLD_ULV].smu_feature_bitmap),
				"Enable ULV Feature Failed!",
				return -1);
		data->smu_features[GNLD_ULV].enabled = true;
	}

	return 0;
}

static int vega10_disable_ulv(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->registry_data.ulv_support) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				false, data->smu_features[GNLD_ULV].smu_feature_bitmap),
				"disable ULV Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_ULV].enabled = false;
	}

	return 0;
}

static int vega10_enable_deep_sleep_master_switch(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_DS_GFXCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				true, data->smu_features[GNLD_DS_GFXCLK].smu_feature_bitmap),
				"Attempt to Enable DS_GFXCLK Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_GFXCLK].enabled = true;
	}

	if (data->smu_features[GNLD_DS_SOCCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				true, data->smu_features[GNLD_DS_SOCCLK].smu_feature_bitmap),
				"Attempt to Enable DS_SOCCLK Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_SOCCLK].enabled = true;
	}

	if (data->smu_features[GNLD_DS_LCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				true, data->smu_features[GNLD_DS_LCLK].smu_feature_bitmap),
				"Attempt to Enable DS_LCLK Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_LCLK].enabled = true;
	}

	if (data->smu_features[GNLD_DS_DCEFCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				true, data->smu_features[GNLD_DS_DCEFCLK].smu_feature_bitmap),
				"Attempt to Enable DS_DCEFCLK Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_DCEFCLK].enabled = true;
	}

	return 0;
}

static int vega10_disable_deep_sleep_master_switch(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_DS_GFXCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				false, data->smu_features[GNLD_DS_GFXCLK].smu_feature_bitmap),
				"Attempt to disable DS_GFXCLK Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_GFXCLK].enabled = false;
	}

	if (data->smu_features[GNLD_DS_SOCCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				false, data->smu_features[GNLD_DS_SOCCLK].smu_feature_bitmap),
				"Attempt to disable DS_ Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_SOCCLK].enabled = false;
	}

	if (data->smu_features[GNLD_DS_LCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				false, data->smu_features[GNLD_DS_LCLK].smu_feature_bitmap),
				"Attempt to disable DS_LCLK Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_LCLK].enabled = false;
	}

	if (data->smu_features[GNLD_DS_DCEFCLK].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				false, data->smu_features[GNLD_DS_DCEFCLK].smu_feature_bitmap),
				"Attempt to disable DS_DCEFCLK Feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_DS_DCEFCLK].enabled = false;
	}

	return 0;
}

static int vega10_stop_dpm(struct pp_hwmgr *hwmgr, uint32_t bitmap)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t i, feature_mask = 0;

	if (!hwmgr->not_vf)
		return 0;

	if(data->smu_features[GNLD_LED_DISPLAY].supported == true){
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				false, data->smu_features[GNLD_LED_DISPLAY].smu_feature_bitmap),
		"Attempt to disable LED DPM feature failed!", return -EINVAL);
		data->smu_features[GNLD_LED_DISPLAY].enabled = false;
	}

	for (i = 0; i < GNLD_DPM_MAX; i++) {
		if (data->smu_features[i].smu_feature_bitmap & bitmap) {
			if (data->smu_features[i].supported) {
				if (data->smu_features[i].enabled) {
					feature_mask |= data->smu_features[i].
							smu_feature_bitmap;
					data->smu_features[i].enabled = false;
				}
			}
		}
	}

	vega10_enable_smc_features(hwmgr, false, feature_mask);

	return 0;
}

/**
 * vega10_start_dpm - Tell SMC to enabled the supported DPMs.
 *
 * @hwmgr:   the address of the powerplay hardware manager.
 * @bitmap:  bitmap for the features to enabled.
 * return:  0 on at least one DPM is successfully enabled.
 */
static int vega10_start_dpm(struct pp_hwmgr *hwmgr, uint32_t bitmap)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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

	if (vega10_enable_smc_features(hwmgr,
			true, feature_mask)) {
		for (i = 0; i < GNLD_DPM_MAX; i++) {
			if (data->smu_features[i].smu_feature_bitmap &
					feature_mask)
				data->smu_features[i].enabled = false;
		}
	}

	if(data->smu_features[GNLD_LED_DISPLAY].supported == true){
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				true, data->smu_features[GNLD_LED_DISPLAY].smu_feature_bitmap),
		"Attempt to Enable LED DPM feature Failed!", return -EINVAL);
		data->smu_features[GNLD_LED_DISPLAY].enabled = true;
	}

	if (data->vbios_boot_state.bsoc_vddc_lock) {
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetFloorSocVoltage, 0,
						NULL);
		data->vbios_boot_state.bsoc_vddc_lock = false;
	}

	if (PP_CAP(PHM_PlatformCaps_Falcon_QuickTransition)) {
		if (data->smu_features[GNLD_ACDC].supported) {
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
					true, data->smu_features[GNLD_ACDC].smu_feature_bitmap),
					"Attempt to Enable DS_GFXCLK Feature Failed!",
					return -1);
			data->smu_features[GNLD_ACDC].enabled = true;
		}
	}

	if (data->registry_data.pcie_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				false, data->smu_features[GNLD_DPM_LINK].smu_feature_bitmap),
		"Attempt to Disable Link DPM feature Failed!", return -EINVAL);
		data->smu_features[GNLD_DPM_LINK].enabled = false;
		data->smu_features[GNLD_DPM_LINK].supported = false;
	}

	return 0;
}


static int vega10_enable_disable_PCC_limit_feature(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_PCC_LIMIT].supported) {
		if (enable == data->smu_features[GNLD_PCC_LIMIT].enabled)
			pr_info("GNLD_PCC_LIMIT has been %s \n", enable ? "enabled" : "disabled");
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				enable, data->smu_features[GNLD_PCC_LIMIT].smu_feature_bitmap),
				"Attempt to Enable PCC Limit feature Failed!",
				return -EINVAL);
		data->smu_features[GNLD_PCC_LIMIT].enabled = enable;
	}

	return 0;
}

static int vega10_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	int tmp_result, result = 0;

	if (hwmgr->not_vf) {
		vega10_enable_disable_PCC_limit_feature(hwmgr, true);

		smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_ConfigureTelemetry, data->config_telemetry,
			NULL);

		tmp_result = vega10_construct_voltage_tables(hwmgr);
		PP_ASSERT_WITH_CODE(!tmp_result,
				    "Failed to construct voltage tables!",
				    result = tmp_result);
	}

	if (hwmgr->not_vf || hwmgr->pp_one_vf) {
		tmp_result = vega10_init_smc_table(hwmgr);
		PP_ASSERT_WITH_CODE(!tmp_result,
				    "Failed to initialize SMC table!",
				    result = tmp_result);
	}

	if (hwmgr->not_vf) {
		if (PP_CAP(PHM_PlatformCaps_ThermalController)) {
			tmp_result = vega10_enable_thermal_protection(hwmgr);
			PP_ASSERT_WITH_CODE(!tmp_result,
					    "Failed to enable thermal protection!",
					    result = tmp_result);
		}

		tmp_result = vega10_enable_vrhot_feature(hwmgr);
		PP_ASSERT_WITH_CODE(!tmp_result,
				    "Failed to enable VR hot feature!",
				    result = tmp_result);

		tmp_result = vega10_enable_deep_sleep_master_switch(hwmgr);
		PP_ASSERT_WITH_CODE(!tmp_result,
				    "Failed to enable deep sleep master switch!",
				    result = tmp_result);
	}

	if (hwmgr->not_vf) {
		tmp_result = vega10_start_dpm(hwmgr, SMC_DPM_FEATURES);
		PP_ASSERT_WITH_CODE(!tmp_result,
				    "Failed to start DPM!", result = tmp_result);
	}

	if (hwmgr->not_vf) {
		/* enable didt, do not abort if failed didt */
		tmp_result = vega10_enable_didt_config(hwmgr);
		PP_ASSERT(!tmp_result,
			  "Failed to enable didt config!");
	}

	tmp_result = vega10_enable_power_containment(hwmgr);
	PP_ASSERT_WITH_CODE(!tmp_result,
			    "Failed to enable power containment!",
			    result = tmp_result);

	if (hwmgr->not_vf) {
		tmp_result = vega10_power_control_set_level(hwmgr);
		PP_ASSERT_WITH_CODE(!tmp_result,
				    "Failed to power control set level!",
				    result = tmp_result);

		tmp_result = vega10_enable_ulv(hwmgr);
		PP_ASSERT_WITH_CODE(!tmp_result,
				    "Failed to enable ULV!",
				    result = tmp_result);
	}

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
	ATOM_Vega10_GFXCLK_Dependency_Record_V2 *patom_record_V2;
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
	if (gfxclk_dep_table->ucRevId == 0) {
		/* under vega10 pp one vf mode, the gfx clk dpm need be lower
		 * to level-4 due to the limited 110w-power
		 */
		if (hwmgr->pp_one_vf && (state_entry->ucGfxClockIndexHigh > 0))
			performance_level->gfx_clock =
				gfxclk_dep_table->entries[4].ulClk;
		else
			performance_level->gfx_clock = gfxclk_dep_table->entries
				[state_entry->ucGfxClockIndexHigh].ulClk;
	} else if (gfxclk_dep_table->ucRevId == 1) {
		patom_record_V2 = (ATOM_Vega10_GFXCLK_Dependency_Record_V2 *)gfxclk_dep_table->entries;
		if (hwmgr->pp_one_vf && (state_entry->ucGfxClockIndexHigh > 0))
			performance_level->gfx_clock = patom_record_V2[4].ulClk;
		else
			performance_level->gfx_clock =
				patom_record_V2[state_entry->ucGfxClockIndexHigh].ulClk;
	}

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
	if (result)
		return result;

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
	struct amdgpu_device *adev = hwmgr->adev;
	struct vega10_power_state *vega10_ps =
				cast_phw_vega10_power_state(&request_ps->hardware);
	uint32_t sclk;
	uint32_t mclk;
	struct PP_Clocks minimum_clocks = {0};
	bool disable_mclk_switching;
	bool disable_mclk_switching_for_frame_lock;
	bool disable_mclk_switching_for_vr;
	bool force_mclk_high;
	const struct phm_clock_and_voltage_limits *max_limits;
	uint32_t i;
	struct vega10_hwmgr *data = hwmgr->backend;
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

	max_limits = adev->pm.ac_power ?
			&(hwmgr->dyn_state.max_clock_voltage_on_ac) :
			&(hwmgr->dyn_state.max_clock_voltage_on_dc);

	/* Cap clock DPM tables at DC MAX if it is in DC. */
	if (!adev->pm.ac_power) {
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

	/* result = PHM_CheckVBlankTime(hwmgr, &vblankTooShort);*/
	minimum_clocks.engineClock = hwmgr->display_config->min_core_set_clock;
	minimum_clocks.memoryClock = hwmgr->display_config->min_mem_set_clock;

	if (PP_CAP(PHM_PlatformCaps_StablePState)) {
		stable_pstate_sclk_dpm_percentage =
			data->registry_data.stable_pstate_sclk_dpm_percentage;
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

	disable_mclk_switching_for_frame_lock =
		PP_CAP(PHM_PlatformCaps_DisableMclkSwitchingForFrameLock);
	disable_mclk_switching_for_vr =
		PP_CAP(PHM_PlatformCaps_DisableMclkSwitchForVR);
	force_mclk_high = PP_CAP(PHM_PlatformCaps_ForceMclkHigh);

	if (hwmgr->display_config->num_display == 0)
		disable_mclk_switching = false;
	else
		disable_mclk_switching = ((1 < hwmgr->display_config->num_display) &&
					  !hwmgr->display_config->multi_monitor_in_sync) ||
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

	if (vega10_ps->performance_levels[1].gfx_clock <
			vega10_ps->performance_levels[0].gfx_clock)
		vega10_ps->performance_levels[0].gfx_clock =
				vega10_ps->performance_levels[1].gfx_clock;

	if (disable_mclk_switching) {
		/* Set Mclk the max of level 0 and level 1 */
		if (mclk < vega10_ps->performance_levels[1].mem_clock)
			mclk = vega10_ps->performance_levels[1].mem_clock;

		/* Find the lowest MCLK frequency that is within
		 * the tolerable latency defined in DAL
		 */
		latency = hwmgr->display_config->dce_tolerable_mclk_in_active_latency;
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
			vega10_ps->performance_levels[0].mem_clock =
					vega10_ps->performance_levels[1].mem_clock;
	}

	if (PP_CAP(PHM_PlatformCaps_StablePState)) {
		for (i = 0; i < vega10_ps->performance_level_count; i++) {
			vega10_ps->performance_levels[i].gfx_clock = stable_pstate_sclk;
			vega10_ps->performance_levels[i].mem_clock = stable_pstate_mclk;
		}
	}

	return 0;
}

static int vega10_find_dpm_states_clocks_in_dpm_table(struct pp_hwmgr *hwmgr, const void *input)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct vega10_power_state *vega10_ps =
			cast_const_phw_vega10_power_state(states->pnew_state);
	struct vega10_single_dpm_table *sclk_table = &(data->dpm_table.gfx_table);
	uint32_t sclk = vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].gfx_clock;
	struct vega10_single_dpm_table *mclk_table = &(data->dpm_table.mem_table);
	uint32_t mclk = vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].mem_clock;
	uint32_t i;

	for (i = 0; i < sclk_table->count; i++) {
		if (sclk == sclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= sclk_table->count) {
		if (sclk > sclk_table->dpm_levels[i-1].value) {
			data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_SCLK;
			sclk_table->dpm_levels[i-1].value = sclk;
		}
	}

	for (i = 0; i < mclk_table->count; i++) {
		if (mclk == mclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= mclk_table->count) {
		if (mclk > mclk_table->dpm_levels[i-1].value) {
			data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_MCLK;
			mclk_table->dpm_levels[i-1].value = mclk;
		}
	}

	if (data->display_timing.num_existing_displays != hwmgr->display_config->num_display)
		data->need_update_dpm_table |= DPMTABLE_UPDATE_MCLK;

	return 0;
}

static int vega10_populate_and_upload_sclk_mclk_dpm_levels(
		struct pp_hwmgr *hwmgr, const void *input)
{
	int result = 0;
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_dpm_table *dpm_table = &data->dpm_table;
	struct vega10_odn_dpm_table *odn_table = &data->odn_dpm_table;
	struct vega10_odn_clock_voltage_dependency_table *odn_clk_table = &odn_table->vdd_dep_on_sclk;
	int count;

	if (!data->need_update_dpm_table)
		return 0;

	if (hwmgr->od_enabled && data->need_update_dpm_table & DPMTABLE_OD_UPDATE_SCLK) {
		for (count = 0; count < dpm_table->gfx_table.count; count++)
			dpm_table->gfx_table.dpm_levels[count].value = odn_clk_table->entries[count].clk;
	}

	odn_clk_table = &odn_table->vdd_dep_on_mclk;
	if (hwmgr->od_enabled && data->need_update_dpm_table & DPMTABLE_OD_UPDATE_MCLK) {
		for (count = 0; count < dpm_table->mem_table.count; count++)
			dpm_table->mem_table.dpm_levels[count].value = odn_clk_table->entries[count].clk;
	}

	if (data->need_update_dpm_table &
			(DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_UPDATE_SCLK | DPMTABLE_UPDATE_SOCCLK)) {
		result = vega10_populate_all_graphic_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate SCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	if (data->need_update_dpm_table &
			(DPMTABLE_OD_UPDATE_MCLK | DPMTABLE_UPDATE_MCLK)) {
		result = vega10_populate_all_memory_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate MCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	vega10_populate_vddc_soc_levels(hwmgr);

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
	struct vega10_hwmgr *data = hwmgr->backend;
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

static int vega10_get_soc_index_for_max_uclk(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_table_on_mclk;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);

	vdd_dep_table_on_mclk  = table_info->vdd_dep_on_mclk;

	return vdd_dep_table_on_mclk->entries[NUM_UCLK_DPM_LEVELS - 1].vddInd + 1;
}

static int vega10_upload_dpm_bootup_level(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t socclk_idx;

	vega10_apply_dal_minimum_voltage_request(hwmgr);

	if (!data->registry_data.sclk_dpm_key_disabled) {
		if (data->smc_state_table.gfx_boot_level !=
				data->dpm_table.gfx_table.dpm_state.soft_min_level) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSoftMinGfxclkByIndex,
				data->smc_state_table.gfx_boot_level,
				NULL);

			data->dpm_table.gfx_table.dpm_state.soft_min_level =
					data->smc_state_table.gfx_boot_level;
		}
	}

	if (!data->registry_data.mclk_dpm_key_disabled) {
		if (data->smc_state_table.mem_boot_level !=
				data->dpm_table.mem_table.dpm_state.soft_min_level) {
			if ((data->smc_state_table.mem_boot_level == NUM_UCLK_DPM_LEVELS - 1)
			    && hwmgr->not_vf) {
				socclk_idx = vega10_get_soc_index_for_max_uclk(hwmgr);
				smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMinSocclkByIndex,
						socclk_idx,
						NULL);
			} else {
				smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSoftMinUclkByIndex,
						data->smc_state_table.mem_boot_level,
						NULL);
			}
			data->dpm_table.mem_table.dpm_state.soft_min_level =
					data->smc_state_table.mem_boot_level;
		}
	}

	if (!hwmgr->not_vf)
		return 0;

	if (!data->registry_data.socclk_dpm_key_disabled) {
		if (data->smc_state_table.soc_boot_level !=
				data->dpm_table.soc_table.dpm_state.soft_min_level) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSoftMinSocclkByIndex,
				data->smc_state_table.soc_boot_level,
				NULL);
			data->dpm_table.soc_table.dpm_state.soft_min_level =
					data->smc_state_table.soc_boot_level;
		}
	}

	return 0;
}

static int vega10_upload_dpm_max_level(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	vega10_apply_dal_minimum_voltage_request(hwmgr);

	if (!data->registry_data.sclk_dpm_key_disabled) {
		if (data->smc_state_table.gfx_max_level !=
			data->dpm_table.gfx_table.dpm_state.soft_max_level) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSoftMaxGfxclkByIndex,
				data->smc_state_table.gfx_max_level,
				NULL);
			data->dpm_table.gfx_table.dpm_state.soft_max_level =
					data->smc_state_table.gfx_max_level;
		}
	}

	if (!data->registry_data.mclk_dpm_key_disabled) {
		if (data->smc_state_table.mem_max_level !=
			data->dpm_table.mem_table.dpm_state.soft_max_level) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSoftMaxUclkByIndex,
					data->smc_state_table.mem_max_level,
					NULL);
			data->dpm_table.mem_table.dpm_state.soft_max_level =
					data->smc_state_table.mem_max_level;
		}
	}

	if (!hwmgr->not_vf)
		return 0;

	if (!data->registry_data.socclk_dpm_key_disabled) {
		if (data->smc_state_table.soc_max_level !=
			data->dpm_table.soc_table.dpm_state.soft_max_level) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSoftMaxSocclkByIndex,
				data->smc_state_table.soc_max_level,
				NULL);
			data->dpm_table.soc_table.dpm_state.soft_max_level =
					data->smc_state_table.soc_max_level;
		}
	}

	return 0;
}

static int vega10_generate_dpm_level_enable_mask(
		struct pp_hwmgr *hwmgr, const void *input)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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
	data->smc_state_table.soc_boot_level =
			vega10_find_lowest_dpm_level(&(data->dpm_table.soc_table));
	data->smc_state_table.soc_max_level =
			vega10_find_highest_dpm_level(&(data->dpm_table.soc_table));

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

	for (i = data->smc_state_table.soc_boot_level; i < data->smc_state_table.soc_max_level; i++)
		data->dpm_table.soc_table.dpm_levels[i].enabled = true;

	return 0;
}

int vega10_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_DPM_VCE].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
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
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (PP_CAP(PHM_PlatformCaps_SclkThrottleLowNotification) &&
		(data->low_sclk_interrupt_threshold != 0)) {
		low_sclk_interrupt_threshold =
				data->low_sclk_interrupt_threshold;

		data->smc_state_table.pp_table.LowGfxclkInterruptThreshold =
				cpu_to_le32(low_sclk_interrupt_threshold);

		/* This message will also enable SmcToHost Interrupt */
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetLowGfxclkInterruptThreshold,
				(uint32_t)low_sclk_interrupt_threshold,
				NULL);
	}

	return 0;
}

static int vega10_set_power_state_tasks(struct pp_hwmgr *hwmgr,
		const void *input)
{
	int tmp_result, result = 0;
	struct vega10_hwmgr *data = hwmgr->backend;
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

	result = smum_smc_table_manager(hwmgr, (uint8_t *)pp_table, PPTABLE, false);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload PPtable!", return result);

	/*
	 * If a custom pp table is loaded, set DPMTABLE_OD_UPDATE_VDDC flag.
	 * That effectively disables AVFS feature.
	 */
	if(hwmgr->hardcode_pp_table != NULL)
		data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_VDDC;

	vega10_update_avfs(hwmgr);

	/*
	 * Clear all OD flags except DPMTABLE_OD_UPDATE_VDDC.
	 * That will help to keep AVFS disabled.
	 */
	data->need_update_dpm_table &= DPMTABLE_OD_UPDATE_VDDC;

	return 0;
}

static uint32_t vega10_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
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

static uint32_t vega10_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
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

static int vega10_get_gpu_power(struct pp_hwmgr *hwmgr,
		uint32_t *query)
{
	uint32_t value;

	if (!query)
		return -EINVAL;

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetCurrPkgPwr, &value);

	/* SMC returning actual watts, keep consistent with legacy asics, low 8 bit as 8 fractional bits */
	*query = value << 8;

	return 0;
}

static int vega10_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			      void *value, int *size)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t sclk_mhz, mclk_idx, activity_percent = 0;
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_dpm_table *dpm_table = &data->dpm_table;
	int ret = 0;
	uint32_t val_vid;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetAverageGfxclkActualFrequency, &sclk_mhz);
		*((uint32_t *)value) = sclk_mhz * 100;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetCurrentUclkIndex, &mclk_idx);
		if (mclk_idx < dpm_table->mem_table.count) {
			*((uint32_t *)value) = dpm_table->mem_table.dpm_levels[mclk_idx].value;
			*size = 4;
		} else {
			ret = -EINVAL;
		}
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetAverageGfxActivity, 0,
						&activity_percent);
		*((uint32_t *)value) = activity_percent > 100 ? 100 : activity_percent;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = vega10_thermal_get_temperature(hwmgr);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetTemperatureHotspot, (uint32_t *)value);
		*((uint32_t *)value) = *((uint32_t *)value) *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetTemperatureHBM, (uint32_t *)value);
		*((uint32_t *)value) = *((uint32_t *)value) *
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
		ret = vega10_get_gpu_power(hwmgr, (uint32_t *)value);
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		val_vid = (RREG32_SOC15(SMUIO, 0, mmSMUSVI0_PLANE0_CURRENTVID) &
			SMUSVI0_PLANE0_CURRENTVID__CURRENT_SVI0_PLANE0_VID_MASK) >>
			SMUSVI0_PLANE0_CURRENTVID__CURRENT_SVI0_PLANE0_VID__SHIFT;
		*((uint32_t *)value) = (uint32_t)convert_to_vddc((uint8_t)val_vid);
		return 0;
	case AMDGPU_PP_SENSOR_ENABLED_SMC_FEATURES_MASK:
		ret = vega10_get_enabled_smc_features(hwmgr, (uint64_t *)value);
		if (!ret)
			*size = 8;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static void vega10_notify_smc_display_change(struct pp_hwmgr *hwmgr,
		bool has_disp)
{
	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetUclkFastSwitch,
			has_disp ? 1 : 0,
			NULL);
}

static int vega10_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
		struct pp_display_clock_request *clock_req)
{
	int result = 0;
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 1000;
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
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_RequestDisplayClockByFreq,
				clk_request,
				NULL);
	}

	return result;
}

static uint8_t vega10_get_uclk_index(struct pp_hwmgr *hwmgr,
			struct phm_ppt_v1_clock_voltage_dependency_table *mclk_table,
						uint32_t frequency)
{
	uint8_t count;
	uint8_t i;

	if (mclk_table == NULL || mclk_table->count == 0)
		return 0;

	count = (uint8_t)(mclk_table->count);

	for(i = 0; i < count; i++) {
		if(mclk_table->entries[i].clk >= frequency)
			return i;
	}

	return i-1;
}

static int vega10_notify_smc_display_config_after_ps_adjustment(
		struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_single_dpm_table *dpm_table =
			&data->dpm_table.dcef_table;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *mclk_table = table_info->vdd_dep_on_mclk;
	uint32_t idx;
	struct PP_Clocks min_clocks = {0};
	uint32_t i;
	struct pp_display_clock_request clock_req;

	if ((hwmgr->display_config->num_display > 1) &&
	     !hwmgr->display_config->multi_monitor_in_sync &&
	     !hwmgr->display_config->nb_pstate_switch_disable)
		vega10_notify_smc_display_change(hwmgr, false);
	else
		vega10_notify_smc_display_change(hwmgr, true);

	min_clocks.dcefClock = hwmgr->display_config->min_dcef_set_clk;
	min_clocks.dcefClockInSR = hwmgr->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memoryClock = hwmgr->display_config->min_mem_set_clock;

	for (i = 0; i < dpm_table->count; i++) {
		if (dpm_table->dpm_levels[i].value == min_clocks.dcefClock)
			break;
	}

	if (i < dpm_table->count) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = dpm_table->dpm_levels[i].value * 10;
		if (!vega10_display_clock_voltage_request(hwmgr, &clock_req)) {
			smum_send_msg_to_smc_with_parameter(
					hwmgr, PPSMC_MSG_SetMinDeepSleepDcefclk,
					min_clocks.dcefClockInSR / 100,
					NULL);
		} else {
			pr_info("Attempt to set Hard Min for DCEFCLK Failed!");
		}
	} else {
		pr_debug("Cannot find requested DCEFCLK!");
	}

	if (min_clocks.memoryClock != 0) {
		idx = vega10_get_uclk_index(hwmgr, mclk_table, min_clocks.memoryClock);
		smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetSoftMinUclkByIndex, idx,
						NULL);
		data->dpm_table.mem_table.dpm_state.soft_min_level= idx;
	}

	return 0;
}

static int vega10_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

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
	struct vega10_hwmgr *data = hwmgr->backend;

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
	struct vega10_hwmgr *data = hwmgr->backend;

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

static int vega10_get_profiling_clk_mask(struct pp_hwmgr *hwmgr, enum amd_dpm_forced_level level,
				uint32_t *sclk_mask, uint32_t *mclk_mask, uint32_t *soc_mask)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);

	if (table_info->vdd_dep_on_sclk->count > VEGA10_UMD_PSTATE_GFXCLK_LEVEL &&
		table_info->vdd_dep_on_socclk->count > VEGA10_UMD_PSTATE_SOCCLK_LEVEL &&
		table_info->vdd_dep_on_mclk->count > VEGA10_UMD_PSTATE_MCLK_LEVEL) {
		*sclk_mask = VEGA10_UMD_PSTATE_GFXCLK_LEVEL;
		*soc_mask = VEGA10_UMD_PSTATE_SOCCLK_LEVEL;
		*mclk_mask = VEGA10_UMD_PSTATE_MCLK_LEVEL;
		hwmgr->pstate_sclk = table_info->vdd_dep_on_sclk->entries[VEGA10_UMD_PSTATE_GFXCLK_LEVEL].clk;
		hwmgr->pstate_mclk = table_info->vdd_dep_on_mclk->entries[VEGA10_UMD_PSTATE_MCLK_LEVEL].clk;
	}

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
		*sclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		*mclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		/* under vega10  pp one vf mode, the gfx clk dpm need be lower
		 * to level-4 due to the limited power
		 */
		if (hwmgr->pp_one_vf)
			*sclk_mask = 4;
		else
			*sclk_mask = table_info->vdd_dep_on_sclk->count - 1;
		*soc_mask = table_info->vdd_dep_on_socclk->count - 1;
		*mclk_mask = table_info->vdd_dep_on_mclk->count - 1;
	}

	return 0;
}

static void vega10_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	if (!hwmgr->not_vf)
		return;

	switch (mode) {
	case AMD_FAN_CTRL_NONE:
		vega10_fan_ctrl_set_fan_speed_percent(hwmgr, 100);
		break;
	case AMD_FAN_CTRL_MANUAL:
		if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
			vega10_fan_ctrl_stop_smc_fan_control(hwmgr);
		break;
	case AMD_FAN_CTRL_AUTO:
		if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
			vega10_fan_ctrl_start_smc_fan_control(hwmgr);
		break;
	default:
		break;
	}
}

static int vega10_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	switch (type) {
	case PP_SCLK:
		data->smc_state_table.gfx_boot_level = mask ? (ffs(mask) - 1) : 0;
		data->smc_state_table.gfx_max_level = mask ? (fls(mask) - 1) : 0;

		PP_ASSERT_WITH_CODE(!vega10_upload_dpm_bootup_level(hwmgr),
			"Failed to upload boot level to lowest!",
			return -EINVAL);

		PP_ASSERT_WITH_CODE(!vega10_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -EINVAL);
		break;

	case PP_MCLK:
		data->smc_state_table.mem_boot_level = mask ? (ffs(mask) - 1) : 0;
		data->smc_state_table.mem_max_level = mask ? (fls(mask) - 1) : 0;

		PP_ASSERT_WITH_CODE(!vega10_upload_dpm_bootup_level(hwmgr),
			"Failed to upload boot level to lowest!",
			return -EINVAL);

		PP_ASSERT_WITH_CODE(!vega10_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -EINVAL);

		break;

	case PP_SOCCLK:
		data->smc_state_table.soc_boot_level = mask ? (ffs(mask) - 1) : 0;
		data->smc_state_table.soc_max_level = mask ? (fls(mask) - 1) : 0;

		PP_ASSERT_WITH_CODE(!vega10_upload_dpm_bootup_level(hwmgr),
			"Failed to upload boot level to lowest!",
			return -EINVAL);

		PP_ASSERT_WITH_CODE(!vega10_upload_dpm_max_level(hwmgr),
			"Failed to upload dpm max level to highest!",
			return -EINVAL);

		break;

	case PP_DCEFCLK:
		pr_info("Setting DCEFCLK min/max dpm level is not supported!\n");
		break;

	case PP_PCIE:
	default:
		break;
	}

	return 0;
}

static int vega10_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;
	uint32_t sclk_mask = 0;
	uint32_t mclk_mask = 0;
	uint32_t soc_mask = 0;

	if (hwmgr->pstate_sclk == 0)
		vega10_get_profiling_clk_mask(hwmgr, level, &sclk_mask, &mclk_mask, &soc_mask);

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = vega10_force_dpm_highest(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = vega10_force_dpm_lowest(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = vega10_unforce_dpm_levels(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = vega10_get_profiling_clk_mask(hwmgr, level, &sclk_mask, &mclk_mask, &soc_mask);
		if (ret)
			return ret;
		vega10_force_clock_level(hwmgr, PP_SCLK, 1<<sclk_mask);
		vega10_force_clock_level(hwmgr, PP_MCLK, 1<<mclk_mask);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}

	if (!hwmgr->not_vf)
		return ret;

	if (!ret) {
		if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK && hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_PROFILE_PEAK)
			vega10_set_fan_control_mode(hwmgr, AMD_FAN_CTRL_NONE);
		else if (level != AMD_DPM_FORCED_LEVEL_PROFILE_PEAK && hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK)
			vega10_set_fan_control_mode(hwmgr, AMD_FAN_CTRL_AUTO);
	}

	return ret;
}

static uint32_t vega10_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_FAN_CONTROL].enabled == false)
		return AMD_FAN_CTRL_MANUAL;
	else
		return AMD_FAN_CTRL_AUTO;
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

	clocks->num_levels = 0;
	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].clk) {
			clocks->data[clocks->num_levels].clocks_in_khz =
					dep_table->entries[i].clk * 10;
			clocks->num_levels++;
		}
	}

}

static void vega10_get_memclocks(struct pp_hwmgr *hwmgr,
		struct pp_clock_levels_with_latency *clocks)
{
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table =
			table_info->vdd_dep_on_mclk;
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t j = 0;
	uint32_t i;

	for (i = 0; i < dep_table->count; i++) {
		if (dep_table->entries[i].clk) {

			clocks->data[j].clocks_in_khz =
						dep_table->entries[i].clk * 10;
			data->mclk_latency_table.entries[j].frequency =
							dep_table->entries[i].clk;
			clocks->data[j].latency_in_us =
				data->mclk_latency_table.entries[j].latency = 25;
			j++;
		}
	}
	clocks->num_levels = data->mclk_latency_table.count = j;
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
		clocks->data[i].clocks_in_khz = dep_table->entries[i].clk * 10;
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
		clocks->data[i].clocks_in_khz = dep_table->entries[i].clk * 10;
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
		clocks->data[i].clocks_in_khz = dep_table->entries[i].clk  * 10;
		clocks->data[i].voltage_in_mv = (uint32_t)(table_info->vddc_lookup_table->
				entries[dep_table->entries[i].vddInd].us_vdd);
		clocks->num_levels++;
	}

	if (i < dep_table->count)
		return -1;

	return 0;
}

static int vega10_set_watermarks_for_clocks_ranges(struct pp_hwmgr *hwmgr,
							void *clock_range)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct dm_pp_wm_sets_with_clock_ranges_soc15 *wm_with_clock_ranges = clock_range;
	Watermarks_t *table = &(data->smc_state_table.water_marks_table);

	if (!data->registry_data.disable_water_mark) {
		smu_set_watermarks_for_clocks_ranges(table, wm_with_clock_ranges);
		data->water_marks_bitmap = WaterMarksExist;
	}

	return 0;
}

static int vega10_get_ppfeature_status(struct pp_hwmgr *hwmgr, char *buf)
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
				"AVFS",
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
				"FAST_PPT",
				"DIDT",
				"ACG",
				"PCC_LIMIT"};
	static const char *output_title[] = {
				"FEATURES",
				"BITMASK",
				"ENABLEMENT"};
	uint64_t features_enabled;
	int i;
	int ret = 0;
	int size = 0;

	ret = vega10_get_enabled_smc_features(hwmgr, &features_enabled);
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

static int vega10_set_ppfeature_status(struct pp_hwmgr *hwmgr, uint64_t new_ppfeature_masks)
{
	uint64_t features_enabled;
	uint64_t features_to_enable;
	uint64_t features_to_disable;
	int ret = 0;

	if (new_ppfeature_masks >= (1ULL << GNLD_FEATURES_MAX))
		return -EINVAL;

	ret = vega10_get_enabled_smc_features(hwmgr, &features_enabled);
	if (ret)
		return ret;

	features_to_disable =
		features_enabled & ~new_ppfeature_masks;
	features_to_enable =
		~features_enabled & new_ppfeature_masks;

	pr_debug("features_to_disable 0x%llx\n", features_to_disable);
	pr_debug("features_to_enable 0x%llx\n", features_to_enable);

	if (features_to_disable) {
		ret = vega10_enable_smc_features(hwmgr, false, features_to_disable);
		if (ret)
			return ret;
	}

	if (features_to_enable) {
		ret = vega10_enable_smc_features(hwmgr, true, features_to_enable);
		if (ret)
			return ret;
	}

	return 0;
}

static int vega10_get_current_pcie_link_width_level(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	return (RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL) &
		PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
		>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;
}

static int vega10_get_current_pcie_link_speed_level(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	return (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
		PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
		>> PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
}

static int vega10_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_single_dpm_table *sclk_table = &(data->dpm_table.gfx_table);
	struct vega10_single_dpm_table *mclk_table = &(data->dpm_table.mem_table);
	struct vega10_single_dpm_table *soc_table = &(data->dpm_table.soc_table);
	struct vega10_single_dpm_table *dcef_table = &(data->dpm_table.dcef_table);
	struct vega10_odn_clock_voltage_dependency_table *podn_vdd_dep = NULL;
	uint32_t gen_speed, lane_width, current_gen_speed, current_lane_width;
	PPTable_t *pptable = &(data->smc_state_table.pp_table);

	int i, now, size = 0, count = 0;

	switch (type) {
	case PP_SCLK:
		if (data->registry_data.sclk_dpm_key_disabled)
			break;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetCurrentGfxclkIndex, &now);

		if (hwmgr->pp_one_vf &&
		    (hwmgr->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK))
			count = 5;
		else
			count = sclk_table->count;
		for (i = 0; i < count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, sclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_MCLK:
		if (data->registry_data.mclk_dpm_key_disabled)
			break;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetCurrentUclkIndex, &now);

		for (i = 0; i < mclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, mclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_SOCCLK:
		if (data->registry_data.socclk_dpm_key_disabled)
			break;

		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetCurrentSocclkIndex, &now);

		for (i = 0; i < soc_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, soc_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_DCEFCLK:
		if (data->registry_data.dcefclk_dpm_key_disabled)
			break;

		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_GetClockFreqMHz, CLK_DCEFCLK, &now);

		for (i = 0; i < dcef_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, dcef_table->dpm_levels[i].value / 100,
					(dcef_table->dpm_levels[i].value / 100 == now) ?
					"*" : "");
		break;
	case PP_PCIE:
		current_gen_speed =
			vega10_get_current_pcie_link_speed_level(hwmgr);
		current_lane_width =
			vega10_get_current_pcie_link_width_level(hwmgr);
		for (i = 0; i < NUM_LINK_LEVELS; i++) {
			gen_speed = pptable->PcieGenSpeed[i];
			lane_width = pptable->PcieLaneCount[i];

			size += sprintf(buf + size, "%d: %s %s %s\n", i,
					(gen_speed == 0) ? "2.5GT/s," :
					(gen_speed == 1) ? "5.0GT/s," :
					(gen_speed == 2) ? "8.0GT/s," :
					(gen_speed == 3) ? "16.0GT/s," : "",
					(lane_width == 1) ? "x1" :
					(lane_width == 2) ? "x2" :
					(lane_width == 3) ? "x4" :
					(lane_width == 4) ? "x8" :
					(lane_width == 5) ? "x12" :
					(lane_width == 6) ? "x16" : "",
					(current_gen_speed == gen_speed) &&
					(current_lane_width == lane_width) ?
					"*" : "");
		}
		break;

	case OD_SCLK:
		if (hwmgr->od_enabled) {
			size = sprintf(buf, "%s:\n", "OD_SCLK");
			podn_vdd_dep = &data->odn_dpm_table.vdd_dep_on_sclk;
			for (i = 0; i < podn_vdd_dep->count; i++)
				size += sprintf(buf + size, "%d: %10uMhz %10umV\n",
					i, podn_vdd_dep->entries[i].clk / 100,
						podn_vdd_dep->entries[i].vddc);
		}
		break;
	case OD_MCLK:
		if (hwmgr->od_enabled) {
			size = sprintf(buf, "%s:\n", "OD_MCLK");
			podn_vdd_dep = &data->odn_dpm_table.vdd_dep_on_mclk;
			for (i = 0; i < podn_vdd_dep->count; i++)
				size += sprintf(buf + size, "%d: %10uMhz %10umV\n",
					i, podn_vdd_dep->entries[i].clk/100,
						podn_vdd_dep->entries[i].vddc);
		}
		break;
	case OD_RANGE:
		if (hwmgr->od_enabled) {
			size = sprintf(buf, "%s:\n", "OD_RANGE");
			size += sprintf(buf + size, "SCLK: %7uMHz %10uMHz\n",
				data->golden_dpm_table.gfx_table.dpm_levels[0].value/100,
				hwmgr->platform_descriptor.overdriveLimit.engineClock/100);
			size += sprintf(buf + size, "MCLK: %7uMHz %10uMHz\n",
				data->golden_dpm_table.mem_table.dpm_levels[0].value/100,
				hwmgr->platform_descriptor.overdriveLimit.memoryClock/100);
			size += sprintf(buf + size, "VDDC: %7umV %11umV\n",
				data->odn_dpm_table.min_vddc,
				data->odn_dpm_table.max_vddc);
		}
		break;
	default:
		break;
	}
	return size;
}

static int vega10_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	Watermarks_t *wm_table = &(data->smc_state_table.water_marks_table);
	int result = 0;

	if ((data->water_marks_bitmap & WaterMarksExist) &&
			!(data->water_marks_bitmap & WaterMarksLoaded)) {
		result = smum_smc_table_manager(hwmgr, (uint8_t *)wm_table, WMTABLE, false);
		PP_ASSERT_WITH_CODE(result, "Failed to update WMTABLE!", return -EINVAL);
		data->water_marks_bitmap |= WaterMarksLoaded;
	}

	if (data->water_marks_bitmap & WaterMarksLoaded) {
		smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_NumOfDisplays, hwmgr->display_config->num_display,
			NULL);
	}

	return result;
}

static int vega10_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_DPM_UVD].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				enable,
				data->smu_features[GNLD_DPM_UVD].smu_feature_bitmap),
				"Attempt to Enable/Disable DPM UVD Failed!",
				return -1);
		data->smu_features[GNLD_DPM_UVD].enabled = enable;
	}
	return 0;
}

static void vega10_power_gate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	data->vce_power_gated = bgate;
	vega10_enable_disable_vce_dpm(hwmgr, !bgate);
}

static void vega10_power_gate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	data->uvd_power_gated = bgate;
	vega10_enable_disable_uvd_dpm(hwmgr, !bgate);
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
	struct vega10_hwmgr *data = hwmgr->backend;
	bool is_update_required = false;

	if (data->display_timing.num_existing_displays != hwmgr->display_config->num_display)
		is_update_required = true;

	if (PP_CAP(PHM_PlatformCaps_SclkDeepSleep)) {
		if (data->display_timing.min_clock_in_sr != hwmgr->display_config->min_core_set_clock_in_sr)
			is_update_required = true;
	}

	return is_update_required;
}

static int vega10_disable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	if (!hwmgr->not_vf)
		return 0;

	if (PP_CAP(PHM_PlatformCaps_ThermalController))
		vega10_disable_thermal_protection(hwmgr);

	tmp_result = vega10_disable_power_containment(hwmgr);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to disable power containment!", result = tmp_result);

	tmp_result = vega10_disable_didt_config(hwmgr);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to disable didt config!", result = tmp_result);

	tmp_result = vega10_avfs_enable(hwmgr, false);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to disable AVFS!", result = tmp_result);

	tmp_result = vega10_stop_dpm(hwmgr, SMC_DPM_FEATURES);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to stop DPM!", result = tmp_result);

	tmp_result = vega10_disable_deep_sleep_master_switch(hwmgr);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to disable deep sleep!", result = tmp_result);

	tmp_result = vega10_disable_ulv(hwmgr);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to disable ulv!", result = tmp_result);

	tmp_result =  vega10_acg_disable(hwmgr);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
			"Failed to disable acg!", result = tmp_result);

	vega10_enable_disable_PCC_limit_feature(hwmgr, false);
	return result;
}

static int vega10_power_off_asic(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	int result;

	result = vega10_disable_dpm_tasks(hwmgr);
	PP_ASSERT_WITH_CODE((0 == result),
			"[disable_dpm_tasks] Failed to disable DPM!",
			);
	data->water_marks_bitmap &= ~(WaterMarksLoaded);

	return result;
}

static int vega10_get_sclk_od(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_single_dpm_table *sclk_table = &(data->dpm_table.gfx_table);
	struct vega10_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.gfx_table);
	int value = sclk_table->dpm_levels[sclk_table->count - 1].value;
	int golden_value = golden_sclk_table->dpm_levels
			[golden_sclk_table->count - 1].value;

	value -= golden_value;
	value = DIV_ROUND_UP(value * 100, golden_value);

	return value;
}

static int vega10_set_sclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.gfx_table);
	struct pp_power_state *ps;
	struct vega10_power_state *vega10_ps;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	vega10_ps = cast_phw_vega10_power_state(&ps->hardware);

	vega10_ps->performance_levels
	[vega10_ps->performance_level_count - 1].gfx_clock =
			golden_sclk_table->dpm_levels
			[golden_sclk_table->count - 1].value *
			value / 100 +
			golden_sclk_table->dpm_levels
			[golden_sclk_table->count - 1].value;

	if (vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].gfx_clock >
			hwmgr->platform_descriptor.overdriveLimit.engineClock) {
		vega10_ps->performance_levels
		[vega10_ps->performance_level_count - 1].gfx_clock =
				hwmgr->platform_descriptor.overdriveLimit.engineClock;
		pr_warn("max sclk supported by vbios is %d\n",
				hwmgr->platform_descriptor.overdriveLimit.engineClock);
	}
	return 0;
}

static int vega10_get_mclk_od(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_single_dpm_table *mclk_table = &(data->dpm_table.mem_table);
	struct vega10_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mem_table);
	int value = mclk_table->dpm_levels[mclk_table->count - 1].value;
	int golden_value = golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value;

	value -= golden_value;
	value = DIV_ROUND_UP(value * 100, golden_value);

	return value;
}

static int vega10_set_mclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mem_table);
	struct pp_power_state  *ps;
	struct vega10_power_state  *vega10_ps;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	vega10_ps = cast_phw_vega10_power_state(&ps->hardware);

	vega10_ps->performance_levels
	[vega10_ps->performance_level_count - 1].mem_clock =
			golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value *
			value / 100 +
			golden_mclk_table->dpm_levels
			[golden_mclk_table->count - 1].value;

	if (vega10_ps->performance_levels
			[vega10_ps->performance_level_count - 1].mem_clock >
			hwmgr->platform_descriptor.overdriveLimit.memoryClock) {
		vega10_ps->performance_levels
		[vega10_ps->performance_level_count - 1].mem_clock =
				hwmgr->platform_descriptor.overdriveLimit.memoryClock;
		pr_warn("max mclk supported by vbios is %d\n",
				hwmgr->platform_descriptor.overdriveLimit.memoryClock);
	}

	return 0;
}

static int vega10_notify_cac_buffer_info(struct pp_hwmgr *hwmgr,
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

static int vega10_get_thermal_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *thermal_data)
{
	struct vega10_hwmgr *data = hwmgr->backend;
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

static int vega10_get_power_profile_mode(struct pp_hwmgr *hwmgr, char *buf)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t i, size = 0;
	static const uint8_t profile_mode_setting[6][4] = {{70, 60, 0, 0,},
						{70, 60, 1, 3,},
						{90, 60, 0, 0,},
						{70, 60, 0, 0,},
						{70, 90, 0, 0,},
						{30, 60, 0, 6,},
						};
	static const char *profile_name[7] = {"BOOTUP_DEFAULT",
					"3D_FULL_SCREEN",
					"POWER_SAVING",
					"VIDEO",
					"VR",
					"COMPUTE",
					"CUSTOM"};
	static const char *title[6] = {"NUM",
			"MODE_NAME",
			"BUSY_SET_POINT",
			"FPS",
			"USE_RLC_BUSY",
			"MIN_ACTIVE_LEVEL"};

	if (!buf)
		return -EINVAL;

	size += sprintf(buf + size, "%s %16s %s %s %s %s\n",title[0],
			title[1], title[2], title[3], title[4], title[5]);

	for (i = 0; i < PP_SMC_POWER_PROFILE_CUSTOM; i++)
		size += sprintf(buf + size, "%3d %14s%s: %14d %3d %10d %14d\n",
			i, profile_name[i], (i == hwmgr->power_profile_mode) ? "*" : " ",
			profile_mode_setting[i][0], profile_mode_setting[i][1],
			profile_mode_setting[i][2], profile_mode_setting[i][3]);
	size += sprintf(buf + size, "%3d %14s%s: %14d %3d %10d %14d\n", i,
			profile_name[i], (i == hwmgr->power_profile_mode) ? "*" : " ",
			data->custom_profile_mode[0], data->custom_profile_mode[1],
			data->custom_profile_mode[2], data->custom_profile_mode[3]);
	return size;
}

static int vega10_set_power_profile_mode(struct pp_hwmgr *hwmgr, long *input, uint32_t size)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	uint8_t busy_set_point;
	uint8_t FPS;
	uint8_t use_rlc_busy;
	uint8_t min_active_level;
	uint32_t power_profile_mode = input[size];

	if (power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {
		if (size != 0 && size != 4)
			return -EINVAL;

		/* If size = 0 and the CUSTOM profile has been set already
		 * then just apply the profile. The copy stored in the hwmgr
		 * is zeroed out on init
		 */
		if (size == 0) {
			if (data->custom_profile_mode[0] != 0)
				goto out;
			else
				return -EINVAL;
		}

		data->custom_profile_mode[0] = busy_set_point = input[0];
		data->custom_profile_mode[1] = FPS = input[1];
		data->custom_profile_mode[2] = use_rlc_busy = input[2];
		data->custom_profile_mode[3] = min_active_level = input[3];
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetCustomGfxDpmParameters,
					busy_set_point | FPS<<8 |
					use_rlc_busy << 16 | min_active_level<<24,
					NULL);
	}

out:
	smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_SetWorkloadMask,
						(!power_profile_mode) ? 0 : 1 << (power_profile_mode - 1),
						NULL);
	hwmgr->power_profile_mode = power_profile_mode;

	return 0;
}


static bool vega10_check_clk_voltage_valid(struct pp_hwmgr *hwmgr,
					enum PP_OD_DPM_TABLE_COMMAND type,
					uint32_t clk,
					uint32_t voltage)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_odn_dpm_table *odn_table = &(data->odn_dpm_table);
	struct vega10_single_dpm_table *golden_table;

	if (voltage < odn_table->min_vddc || voltage > odn_table->max_vddc) {
		pr_info("OD voltage is out of range [%d - %d] mV\n", odn_table->min_vddc, odn_table->max_vddc);
		return false;
	}

	if (type == PP_OD_EDIT_SCLK_VDDC_TABLE) {
		golden_table = &(data->golden_dpm_table.gfx_table);
		if (golden_table->dpm_levels[0].value > clk ||
			hwmgr->platform_descriptor.overdriveLimit.engineClock < clk) {
			pr_info("OD engine clock is out of range [%d - %d] MHz\n",
				golden_table->dpm_levels[0].value/100,
				hwmgr->platform_descriptor.overdriveLimit.engineClock/100);
			return false;
		}
	} else if (type == PP_OD_EDIT_MCLK_VDDC_TABLE) {
		golden_table = &(data->golden_dpm_table.mem_table);
		if (golden_table->dpm_levels[0].value > clk ||
			hwmgr->platform_descriptor.overdriveLimit.memoryClock < clk) {
			pr_info("OD memory clock is out of range [%d - %d] MHz\n",
				golden_table->dpm_levels[0].value/100,
				hwmgr->platform_descriptor.overdriveLimit.memoryClock/100);
			return false;
		}
	} else {
		return false;
	}

	return true;
}

static void vega10_odn_update_power_state(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct pp_power_state *ps = hwmgr->request_ps;
	struct vega10_power_state *vega10_ps;
	struct vega10_single_dpm_table *gfx_dpm_table =
		&data->dpm_table.gfx_table;
	struct vega10_single_dpm_table *soc_dpm_table =
		&data->dpm_table.soc_table;
	struct vega10_single_dpm_table *mem_dpm_table =
		&data->dpm_table.mem_table;
	int max_level;

	if (!ps)
		return;

	vega10_ps = cast_phw_vega10_power_state(&ps->hardware);
	max_level = vega10_ps->performance_level_count - 1;

	if (vega10_ps->performance_levels[max_level].gfx_clock !=
	    gfx_dpm_table->dpm_levels[gfx_dpm_table->count - 1].value)
		vega10_ps->performance_levels[max_level].gfx_clock =
			gfx_dpm_table->dpm_levels[gfx_dpm_table->count - 1].value;

	if (vega10_ps->performance_levels[max_level].soc_clock !=
	    soc_dpm_table->dpm_levels[soc_dpm_table->count - 1].value)
		vega10_ps->performance_levels[max_level].soc_clock =
			soc_dpm_table->dpm_levels[soc_dpm_table->count - 1].value;

	if (vega10_ps->performance_levels[max_level].mem_clock !=
	    mem_dpm_table->dpm_levels[mem_dpm_table->count - 1].value)
		vega10_ps->performance_levels[max_level].mem_clock =
			mem_dpm_table->dpm_levels[mem_dpm_table->count - 1].value;

	if (!hwmgr->ps)
		return;

	ps = (struct pp_power_state *)((unsigned long)(hwmgr->ps) + hwmgr->ps_size * (hwmgr->num_ps - 1));
	vega10_ps = cast_phw_vega10_power_state(&ps->hardware);
	max_level = vega10_ps->performance_level_count - 1;

	if (vega10_ps->performance_levels[max_level].gfx_clock !=
	    gfx_dpm_table->dpm_levels[gfx_dpm_table->count - 1].value)
		vega10_ps->performance_levels[max_level].gfx_clock =
			gfx_dpm_table->dpm_levels[gfx_dpm_table->count - 1].value;

	if (vega10_ps->performance_levels[max_level].soc_clock !=
	    soc_dpm_table->dpm_levels[soc_dpm_table->count - 1].value)
		vega10_ps->performance_levels[max_level].soc_clock =
			soc_dpm_table->dpm_levels[soc_dpm_table->count - 1].value;

	if (vega10_ps->performance_levels[max_level].mem_clock !=
	    mem_dpm_table->dpm_levels[mem_dpm_table->count - 1].value)
		vega10_ps->performance_levels[max_level].mem_clock =
			mem_dpm_table->dpm_levels[mem_dpm_table->count - 1].value;
}

static void vega10_odn_update_soc_table(struct pp_hwmgr *hwmgr,
						enum PP_OD_DPM_TABLE_COMMAND type)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct phm_ppt_v2_information *table_info = hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_table = table_info->vdd_dep_on_socclk;
	struct vega10_single_dpm_table *dpm_table = &data->golden_dpm_table.mem_table;

	struct vega10_odn_clock_voltage_dependency_table *podn_vdd_dep_on_socclk =
							&data->odn_dpm_table.vdd_dep_on_socclk;
	struct vega10_odn_vddc_lookup_table *od_vddc_lookup_table = &data->odn_dpm_table.vddc_lookup_table;

	struct vega10_odn_clock_voltage_dependency_table *podn_vdd_dep;
	uint8_t i, j;

	if (type == PP_OD_EDIT_SCLK_VDDC_TABLE) {
		podn_vdd_dep = &data->odn_dpm_table.vdd_dep_on_sclk;
		for (i = 0; i < podn_vdd_dep->count; i++)
			od_vddc_lookup_table->entries[i].us_vdd = podn_vdd_dep->entries[i].vddc;
	} else if (type == PP_OD_EDIT_MCLK_VDDC_TABLE) {
		podn_vdd_dep = &data->odn_dpm_table.vdd_dep_on_mclk;
		for (i = 0; i < dpm_table->count; i++) {
			for (j = 0; j < od_vddc_lookup_table->count; j++) {
				if (od_vddc_lookup_table->entries[j].us_vdd >
					podn_vdd_dep->entries[i].vddc)
					break;
			}
			if (j == od_vddc_lookup_table->count) {
				j = od_vddc_lookup_table->count - 1;
				od_vddc_lookup_table->entries[j].us_vdd =
					podn_vdd_dep->entries[i].vddc;
				data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_VDDC;
			}
			podn_vdd_dep->entries[i].vddInd = j;
		}
		dpm_table = &data->dpm_table.soc_table;
		for (i = 0; i < dep_table->count; i++) {
			if (dep_table->entries[i].vddInd == podn_vdd_dep->entries[podn_vdd_dep->count-1].vddInd &&
					dep_table->entries[i].clk < podn_vdd_dep->entries[podn_vdd_dep->count-1].clk) {
				data->need_update_dpm_table |= DPMTABLE_UPDATE_SOCCLK;
				for (; (i < dep_table->count) &&
				       (dep_table->entries[i].clk < podn_vdd_dep->entries[podn_vdd_dep->count - 1].clk); i++) {
					podn_vdd_dep_on_socclk->entries[i].clk = podn_vdd_dep->entries[podn_vdd_dep->count-1].clk;
					dpm_table->dpm_levels[i].value = podn_vdd_dep_on_socclk->entries[i].clk;
				}
				break;
			} else {
				dpm_table->dpm_levels[i].value = dep_table->entries[i].clk;
				podn_vdd_dep_on_socclk->entries[i].vddc = dep_table->entries[i].vddc;
				podn_vdd_dep_on_socclk->entries[i].vddInd = dep_table->entries[i].vddInd;
				podn_vdd_dep_on_socclk->entries[i].clk = dep_table->entries[i].clk;
			}
		}
		if (podn_vdd_dep_on_socclk->entries[podn_vdd_dep_on_socclk->count - 1].clk <
					podn_vdd_dep->entries[podn_vdd_dep->count - 1].clk) {
			data->need_update_dpm_table |= DPMTABLE_UPDATE_SOCCLK;
			podn_vdd_dep_on_socclk->entries[podn_vdd_dep_on_socclk->count - 1].clk =
				podn_vdd_dep->entries[podn_vdd_dep->count - 1].clk;
			dpm_table->dpm_levels[podn_vdd_dep_on_socclk->count - 1].value =
				podn_vdd_dep->entries[podn_vdd_dep->count - 1].clk;
		}
		if (podn_vdd_dep_on_socclk->entries[podn_vdd_dep_on_socclk->count - 1].vddInd <
					podn_vdd_dep->entries[podn_vdd_dep->count - 1].vddInd) {
			data->need_update_dpm_table |= DPMTABLE_UPDATE_SOCCLK;
			podn_vdd_dep_on_socclk->entries[podn_vdd_dep_on_socclk->count - 1].vddInd =
				podn_vdd_dep->entries[podn_vdd_dep->count - 1].vddInd;
		}
	}
	vega10_odn_update_power_state(hwmgr);
}

static int vega10_odn_edit_dpm_table(struct pp_hwmgr *hwmgr,
					enum PP_OD_DPM_TABLE_COMMAND type,
					long *input, uint32_t size)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	struct vega10_odn_clock_voltage_dependency_table *podn_vdd_dep_table;
	struct vega10_single_dpm_table *dpm_table;

	uint32_t input_clk;
	uint32_t input_vol;
	uint32_t input_level;
	uint32_t i;

	PP_ASSERT_WITH_CODE(input, "NULL user input for clock and voltage",
				return -EINVAL);

	if (!hwmgr->od_enabled) {
		pr_info("OverDrive feature not enabled\n");
		return -EINVAL;
	}

	if (PP_OD_EDIT_SCLK_VDDC_TABLE == type) {
		dpm_table = &data->dpm_table.gfx_table;
		podn_vdd_dep_table = &data->odn_dpm_table.vdd_dep_on_sclk;
		data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_SCLK;
	} else if (PP_OD_EDIT_MCLK_VDDC_TABLE == type) {
		dpm_table = &data->dpm_table.mem_table;
		podn_vdd_dep_table = &data->odn_dpm_table.vdd_dep_on_mclk;
		data->need_update_dpm_table |= DPMTABLE_OD_UPDATE_MCLK;
	} else if (PP_OD_RESTORE_DEFAULT_TABLE == type) {
		memcpy(&(data->dpm_table), &(data->golden_dpm_table), sizeof(struct vega10_dpm_table));
		vega10_odn_initial_default_setting(hwmgr);
		vega10_odn_update_power_state(hwmgr);
		/* force to update all clock tables */
		data->need_update_dpm_table = DPMTABLE_UPDATE_SCLK |
					      DPMTABLE_UPDATE_MCLK |
					      DPMTABLE_UPDATE_SOCCLK;
		return 0;
	} else if (PP_OD_COMMIT_DPM_TABLE == type) {
		vega10_check_dpm_table_updated(hwmgr);
		return 0;
	} else {
		return -EINVAL;
	}

	for (i = 0; i < size; i += 3) {
		if (i + 3 > size || input[i] >= podn_vdd_dep_table->count) {
			pr_info("invalid clock voltage input\n");
			return 0;
		}
		input_level = input[i];
		input_clk = input[i+1] * 100;
		input_vol = input[i+2];

		if (vega10_check_clk_voltage_valid(hwmgr, type, input_clk, input_vol)) {
			dpm_table->dpm_levels[input_level].value = input_clk;
			podn_vdd_dep_table->entries[input_level].clk = input_clk;
			podn_vdd_dep_table->entries[input_level].vddc = input_vol;
		} else {
			return -EINVAL;
		}
	}
	vega10_odn_update_soc_table(hwmgr, type);
	return 0;
}

static int vega10_set_mp1_state(struct pp_hwmgr *hwmgr,
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

static int vega10_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level)
{
	const struct vega10_power_state *ps;
	uint32_t i;

	if (level == NULL || hwmgr == NULL || state == NULL)
		return -EINVAL;

	ps = cast_const_phw_vega10_power_state(state);

	i = index > ps->performance_level_count - 1 ?
			ps->performance_level_count - 1 : index;

	level->coreClock = ps->performance_levels[i].gfx_clock;
	level->memory_clock = ps->performance_levels[i].mem_clock;

	return 0;
}

static int vega10_disable_power_features_for_compute_performance(struct pp_hwmgr *hwmgr, bool disable)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t feature_mask = 0;

	if (disable) {
		feature_mask |= data->smu_features[GNLD_ULV].enabled ?
			data->smu_features[GNLD_ULV].smu_feature_bitmap : 0;
		feature_mask |= data->smu_features[GNLD_DS_GFXCLK].enabled ?
			data->smu_features[GNLD_DS_GFXCLK].smu_feature_bitmap : 0;
		feature_mask |= data->smu_features[GNLD_DS_SOCCLK].enabled ?
			data->smu_features[GNLD_DS_SOCCLK].smu_feature_bitmap : 0;
		feature_mask |= data->smu_features[GNLD_DS_LCLK].enabled ?
			data->smu_features[GNLD_DS_LCLK].smu_feature_bitmap : 0;
		feature_mask |= data->smu_features[GNLD_DS_DCEFCLK].enabled ?
			data->smu_features[GNLD_DS_DCEFCLK].smu_feature_bitmap : 0;
	} else {
		feature_mask |= (!data->smu_features[GNLD_ULV].enabled) ?
			data->smu_features[GNLD_ULV].smu_feature_bitmap : 0;
		feature_mask |= (!data->smu_features[GNLD_DS_GFXCLK].enabled) ?
			data->smu_features[GNLD_DS_GFXCLK].smu_feature_bitmap : 0;
		feature_mask |= (!data->smu_features[GNLD_DS_SOCCLK].enabled) ?
			data->smu_features[GNLD_DS_SOCCLK].smu_feature_bitmap : 0;
		feature_mask |= (!data->smu_features[GNLD_DS_LCLK].enabled) ?
			data->smu_features[GNLD_DS_LCLK].smu_feature_bitmap : 0;
		feature_mask |= (!data->smu_features[GNLD_DS_DCEFCLK].enabled) ?
			data->smu_features[GNLD_DS_DCEFCLK].smu_feature_bitmap : 0;
	}

	if (feature_mask)
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				!disable, feature_mask),
				"enable/disable power features for compute performance Failed!",
				return -EINVAL);

	if (disable) {
		data->smu_features[GNLD_ULV].enabled = false;
		data->smu_features[GNLD_DS_GFXCLK].enabled = false;
		data->smu_features[GNLD_DS_SOCCLK].enabled = false;
		data->smu_features[GNLD_DS_LCLK].enabled = false;
		data->smu_features[GNLD_DS_DCEFCLK].enabled = false;
	} else {
		data->smu_features[GNLD_ULV].enabled = true;
		data->smu_features[GNLD_DS_GFXCLK].enabled = true;
		data->smu_features[GNLD_DS_SOCCLK].enabled = true;
		data->smu_features[GNLD_DS_LCLK].enabled = true;
		data->smu_features[GNLD_DS_DCEFCLK].enabled = true;
	}

	return 0;

}

static const struct pp_hwmgr_func vega10_hwmgr_funcs = {
	.backend_init = vega10_hwmgr_backend_init,
	.backend_fini = vega10_hwmgr_backend_fini,
	.asic_setup = vega10_setup_asic_task,
	.dynamic_state_management_enable = vega10_enable_dpm_tasks,
	.dynamic_state_management_disable = vega10_disable_dpm_tasks,
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
	.power_off_asic = vega10_power_off_asic,
	.disable_smc_firmware_ctf = vega10_thermal_disable_alert,
	.get_sclk_od = vega10_get_sclk_od,
	.set_sclk_od = vega10_set_sclk_od,
	.get_mclk_od = vega10_get_mclk_od,
	.set_mclk_od = vega10_set_mclk_od,
	.avfs_control = vega10_avfs_enable,
	.notify_cac_buffer_info = vega10_notify_cac_buffer_info,
	.get_thermal_temperature_range = vega10_get_thermal_temperature_range,
	.register_irq_handlers = smu9_register_irq_handlers,
	.start_thermal_controller = vega10_start_thermal_controller,
	.get_power_profile_mode = vega10_get_power_profile_mode,
	.set_power_profile_mode = vega10_set_power_profile_mode,
	.set_power_limit = vega10_set_power_limit,
	.odn_edit_dpm_table = vega10_odn_edit_dpm_table,
	.get_performance_level = vega10_get_performance_level,
	.get_asic_baco_capability = smu9_baco_get_capability,
	.get_asic_baco_state = smu9_baco_get_state,
	.set_asic_baco_state = vega10_baco_set_state,
	.enable_mgpu_fan_boost = vega10_enable_mgpu_fan_boost,
	.get_ppfeature_status = vega10_get_ppfeature_status,
	.set_ppfeature_status = vega10_set_ppfeature_status,
	.set_mp1_state = vega10_set_mp1_state,
	.disable_power_features_for_compute_performance =
			vega10_disable_power_features_for_compute_performance,
};

int vega10_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	hwmgr->hwmgr_func = &vega10_hwmgr_funcs;
	hwmgr->pptable_func = &vega10_pptable_funcs;
	if (amdgpu_passthrough(adev))
		return vega10_baco_set_cap(hwmgr);

	return 0;
}
