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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "atom-types.h"
#include "atombios.h"
#include "processpptables.h"
#include "pp_debug.h"
#include "cgs_common.h"
#include "smu/smu_8_0_d.h"
#include "smu8_fusion.h"
#include "smu/smu_8_0_sh_mask.h"
#include "smumgr.h"
#include "hwmgr.h"
#include "hardwaremanager.h"
#include "cz_ppsmc.h"
#include "cz_hwmgr.h"
#include "power_state.h"
#include "cz_clockpowergating.h"
#include "pp_debug.h"

#define ixSMUSVI_NB_CURRENTVID 0xD8230044
#define CURRENT_NB_VID_MASK 0xff000000
#define CURRENT_NB_VID__SHIFT 24
#define ixSMUSVI_GFX_CURRENTVID  0xD8230048
#define CURRENT_GFX_VID_MASK 0xff000000
#define CURRENT_GFX_VID__SHIFT 24

static const unsigned long PhwCz_Magic = (unsigned long) PHM_Cz_Magic;

static struct cz_power_state *cast_PhwCzPowerState(struct pp_hw_power_state *hw_ps)
{
	if (PhwCz_Magic != hw_ps->magic)
		return NULL;

	return (struct cz_power_state *)hw_ps;
}

static const struct cz_power_state *cast_const_PhwCzPowerState(
				const struct pp_hw_power_state *hw_ps)
{
	if (PhwCz_Magic != hw_ps->magic)
		return NULL;

	return (struct cz_power_state *)hw_ps;
}

static uint32_t cz_get_eclk_level(struct pp_hwmgr *hwmgr,
					uint32_t clock, uint32_t msg)
{
	int i = 0;
	struct phm_vce_clock_voltage_dependency_table *ptable =
		hwmgr->dyn_state.vce_clock_voltage_dependency_table;

	switch (msg) {
	case PPSMC_MSG_SetEclkSoftMin:
	case PPSMC_MSG_SetEclkHardMin:
		for (i = 0; i < (int)ptable->count; i++) {
			if (clock <= ptable->entries[i].ecclk)
				break;
		}
		break;

	case PPSMC_MSG_SetEclkSoftMax:
	case PPSMC_MSG_SetEclkHardMax:
		for (i = ptable->count - 1; i >= 0; i--) {
			if (clock >= ptable->entries[i].ecclk)
				break;
		}
		break;

	default:
		break;
	}

	return i;
}

static uint32_t cz_get_sclk_level(struct pp_hwmgr *hwmgr,
				uint32_t clock, uint32_t msg)
{
	int i = 0;
	struct phm_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.vddc_dependency_on_sclk;

	switch (msg) {
	case PPSMC_MSG_SetSclkSoftMin:
	case PPSMC_MSG_SetSclkHardMin:
		for (i = 0; i < (int)table->count; i++) {
			if (clock <= table->entries[i].clk)
				break;
		}
		break;

	case PPSMC_MSG_SetSclkSoftMax:
	case PPSMC_MSG_SetSclkHardMax:
		for (i = table->count - 1; i >= 0; i--) {
			if (clock >= table->entries[i].clk)
				break;
		}
		break;

	default:
		break;
	}
	return i;
}

static uint32_t cz_get_uvd_level(struct pp_hwmgr *hwmgr,
					uint32_t clock, uint32_t msg)
{
	int i = 0;
	struct phm_uvd_clock_voltage_dependency_table *ptable =
		hwmgr->dyn_state.uvd_clock_voltage_dependency_table;

	switch (msg) {
	case PPSMC_MSG_SetUvdSoftMin:
	case PPSMC_MSG_SetUvdHardMin:
		for (i = 0; i < (int)ptable->count; i++) {
			if (clock <= ptable->entries[i].vclk)
				break;
		}
		break;

	case PPSMC_MSG_SetUvdSoftMax:
	case PPSMC_MSG_SetUvdHardMax:
		for (i = ptable->count - 1; i >= 0; i--) {
			if (clock >= ptable->entries[i].vclk)
				break;
		}
		break;

	default:
		break;
	}

	return i;
}

static uint32_t cz_get_max_sclk_level(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	if (cz_hwmgr->max_sclk_level == 0) {
		smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_GetMaxSclkLevel);
		cz_hwmgr->max_sclk_level = smum_get_argument(hwmgr->smumgr) + 1;
	}

	return cz_hwmgr->max_sclk_level;
}

static int cz_initialize_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	uint32_t i;
	struct cgs_system_info sys_info = {0};
	int result;

	cz_hwmgr->gfx_ramp_step = 256*25/100;
	cz_hwmgr->gfx_ramp_delay = 1; /* by default, we delay 1us */

	for (i = 0; i < CZ_MAX_HARDWARE_POWERLEVELS; i++)
		cz_hwmgr->activity_target[i] = CZ_AT_DFLT;

	cz_hwmgr->mgcg_cgtt_local0 = 0x00000000;
	cz_hwmgr->mgcg_cgtt_local1 = 0x00000000;
	cz_hwmgr->clock_slow_down_freq = 25000;
	cz_hwmgr->skip_clock_slow_down = 1;
	cz_hwmgr->enable_nb_ps_policy = 1; /* disable until UNB is ready, Enabled */
	cz_hwmgr->voltage_drop_in_dce_power_gating = 0; /* disable until fully verified */
	cz_hwmgr->voting_rights_clients = 0x00C00033;
	cz_hwmgr->static_screen_threshold = 8;
	cz_hwmgr->ddi_power_gating_disabled = 0;
	cz_hwmgr->bapm_enabled = 1;
	cz_hwmgr->voltage_drop_threshold = 0;
	cz_hwmgr->gfx_power_gating_threshold = 500;
	cz_hwmgr->vce_slow_sclk_threshold = 20000;
	cz_hwmgr->dce_slow_sclk_threshold = 30000;
	cz_hwmgr->disable_driver_thermal_policy = 1;
	cz_hwmgr->disable_nb_ps3_in_battery = 0;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_ABM);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_NonABMSupportInPPLib);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicM3Arbiter);

	cz_hwmgr->override_dynamic_mgpg = 1;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				  PHM_PlatformCaps_DynamicPatchPowerState);

	cz_hwmgr->thermal_auto_throttling_treshold = 0;
	cz_hwmgr->tdr_clock = 0;
	cz_hwmgr->disable_gfx_power_gating_in_uvd = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicUVDState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UVDDPM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_VCEDPM);

	cz_hwmgr->cc6_settings.cpu_cc6_disable = false;
	cz_hwmgr->cc6_settings.cpu_pstate_disable = false;
	cz_hwmgr->cc6_settings.nb_pstate_switch_disable = false;
	cz_hwmgr->cc6_settings.cpu_pstate_separation_time = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				   PHM_PlatformCaps_DisableVoltageIsland);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_UVDPowerGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_VCEPowerGating);
	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_PG_FLAGS;
	result = cgs_query_system_info(hwmgr->device, &sys_info);
	if (!result) {
		if (sys_info.value & AMD_PG_SUPPORT_UVD)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				      PHM_PlatformCaps_UVDPowerGating);
		if (sys_info.value & AMD_PG_SUPPORT_VCE)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				      PHM_PlatformCaps_VCEPowerGating);
	}

	return 0;
}

static uint32_t cz_convert_8Bit_index_to_voltage(
			struct pp_hwmgr *hwmgr, uint16_t voltage)
{
	return 6200 - (voltage * 25);
}

static int cz_construct_max_power_limits_table(struct pp_hwmgr *hwmgr,
			struct phm_clock_and_voltage_limits *table)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)hwmgr->backend;
	struct cz_sys_info *sys_info = &cz_hwmgr->sys_info;
	struct phm_clock_voltage_dependency_table *dep_table =
				hwmgr->dyn_state.vddc_dependency_on_sclk;

	if (dep_table->count > 0) {
		table->sclk = dep_table->entries[dep_table->count-1].clk;
		table->vddc = cz_convert_8Bit_index_to_voltage(hwmgr,
		   (uint16_t)dep_table->entries[dep_table->count-1].v);
	}
	table->mclk = sys_info->nbp_memory_clock[0];
	return 0;
}

static int cz_init_dynamic_state_adjustment_rule_settings(
			struct pp_hwmgr *hwmgr,
			ATOM_CLK_VOLT_CAPABILITY *disp_voltage_table)
{
	uint32_t table_size =
		sizeof(struct phm_clock_voltage_dependency_table) +
		(7 * sizeof(struct phm_clock_voltage_dependency_record));

	struct phm_clock_voltage_dependency_table *table_clk_vlt =
					kzalloc(table_size, GFP_KERNEL);

	if (NULL == table_clk_vlt) {
		printk(KERN_ERR "[ powerplay ] Can not allocate memory!\n");
		return -ENOMEM;
	}

	table_clk_vlt->count = 8;
	table_clk_vlt->entries[0].clk = PP_DAL_POWERLEVEL_0;
	table_clk_vlt->entries[0].v = 0;
	table_clk_vlt->entries[1].clk = PP_DAL_POWERLEVEL_1;
	table_clk_vlt->entries[1].v = 1;
	table_clk_vlt->entries[2].clk = PP_DAL_POWERLEVEL_2;
	table_clk_vlt->entries[2].v = 2;
	table_clk_vlt->entries[3].clk = PP_DAL_POWERLEVEL_3;
	table_clk_vlt->entries[3].v = 3;
	table_clk_vlt->entries[4].clk = PP_DAL_POWERLEVEL_4;
	table_clk_vlt->entries[4].v = 4;
	table_clk_vlt->entries[5].clk = PP_DAL_POWERLEVEL_5;
	table_clk_vlt->entries[5].v = 5;
	table_clk_vlt->entries[6].clk = PP_DAL_POWERLEVEL_6;
	table_clk_vlt->entries[6].v = 6;
	table_clk_vlt->entries[7].clk = PP_DAL_POWERLEVEL_7;
	table_clk_vlt->entries[7].v = 7;
	hwmgr->dyn_state.vddc_dep_on_dal_pwrl = table_clk_vlt;

	return 0;
}

static int cz_get_system_info_data(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)hwmgr->backend;
	ATOM_INTEGRATED_SYSTEM_INFO_V1_9 *info = NULL;
	uint32_t i;
	int result = 0;
	uint8_t frev, crev;
	uint16_t size;

	info = (ATOM_INTEGRATED_SYSTEM_INFO_V1_9 *) cgs_atom_get_data_table(
			hwmgr->device,
			GetIndexIntoMasterTable(DATA, IntegratedSystemInfo),
			&size, &frev, &crev);

	if (crev != 9) {
		printk(KERN_ERR "[ powerplay ] Unsupported IGP table: %d %d\n", frev, crev);
		return -EINVAL;
	}

	if (info == NULL) {
		printk(KERN_ERR "[ powerplay ] Could not retrieve the Integrated System Info Table!\n");
		return -EINVAL;
	}

	cz_hwmgr->sys_info.bootup_uma_clock =
				   le32_to_cpu(info->ulBootUpUMAClock);

	cz_hwmgr->sys_info.bootup_engine_clock =
				le32_to_cpu(info->ulBootUpEngineClock);

	cz_hwmgr->sys_info.dentist_vco_freq =
				   le32_to_cpu(info->ulDentistVCOFreq);

	cz_hwmgr->sys_info.system_config =
				     le32_to_cpu(info->ulSystemConfig);

	cz_hwmgr->sys_info.bootup_nb_voltage_index =
				  le16_to_cpu(info->usBootUpNBVoltage);

	cz_hwmgr->sys_info.htc_hyst_lmt =
			(info->ucHtcHystLmt == 0) ? 5 : info->ucHtcHystLmt;

	cz_hwmgr->sys_info.htc_tmp_lmt =
			(info->ucHtcTmpLmt == 0) ? 203 : info->ucHtcTmpLmt;

	if (cz_hwmgr->sys_info.htc_tmp_lmt <=
			cz_hwmgr->sys_info.htc_hyst_lmt) {
		printk(KERN_ERR "[ powerplay ] The htcTmpLmt should be larger than htcHystLmt.\n");
		return -EINVAL;
	}

	cz_hwmgr->sys_info.nb_dpm_enable =
				cz_hwmgr->enable_nb_ps_policy &&
				(le32_to_cpu(info->ulSystemConfig) >> 3 & 0x1);

	for (i = 0; i < CZ_NUM_NBPSTATES; i++) {
		if (i < CZ_NUM_NBPMEMORYCLOCK) {
			cz_hwmgr->sys_info.nbp_memory_clock[i] =
			  le32_to_cpu(info->ulNbpStateMemclkFreq[i]);
		}
		cz_hwmgr->sys_info.nbp_n_clock[i] =
			    le32_to_cpu(info->ulNbpStateNClkFreq[i]);
	}

	for (i = 0; i < MAX_DISPLAY_CLOCK_LEVEL; i++) {
		cz_hwmgr->sys_info.display_clock[i] =
					le32_to_cpu(info->sDispClkVoltageMapping[i].ulMaximumSupportedCLK);
	}

	/* Here use 4 levels, make sure not exceed */
	for (i = 0; i < CZ_NUM_NBPSTATES; i++) {
		cz_hwmgr->sys_info.nbp_voltage_index[i] =
			     le16_to_cpu(info->usNBPStateVoltage[i]);
	}

	if (!cz_hwmgr->sys_info.nb_dpm_enable) {
		for (i = 1; i < CZ_NUM_NBPSTATES; i++) {
			if (i < CZ_NUM_NBPMEMORYCLOCK) {
				cz_hwmgr->sys_info.nbp_memory_clock[i] =
				    cz_hwmgr->sys_info.nbp_memory_clock[0];
			}
			cz_hwmgr->sys_info.nbp_n_clock[i] =
				    cz_hwmgr->sys_info.nbp_n_clock[0];
			cz_hwmgr->sys_info.nbp_voltage_index[i] =
				    cz_hwmgr->sys_info.nbp_voltage_index[0];
		}
	}

	if (le32_to_cpu(info->ulGPUCapInfo) &
		SYS_INFO_GPUCAPS__ENABEL_DFS_BYPASS) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_EnableDFSBypass);
	}

	cz_hwmgr->sys_info.uma_channel_number = info->ucUMAChannelNumber;

	cz_construct_max_power_limits_table (hwmgr,
				    &hwmgr->dyn_state.max_clock_voltage_on_ac);

	cz_init_dynamic_state_adjustment_rule_settings(hwmgr,
				    &info->sDISPCLK_Voltage[0]);

	return result;
}

static int cz_construct_boot_state(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	cz_hwmgr->boot_power_level.engineClock =
				cz_hwmgr->sys_info.bootup_engine_clock;

	cz_hwmgr->boot_power_level.vddcIndex =
			(uint8_t)cz_hwmgr->sys_info.bootup_nb_voltage_index;

	cz_hwmgr->boot_power_level.dsDividerIndex = 0;
	cz_hwmgr->boot_power_level.ssDividerIndex = 0;
	cz_hwmgr->boot_power_level.allowGnbSlow = 1;
	cz_hwmgr->boot_power_level.forceNBPstate = 0;
	cz_hwmgr->boot_power_level.hysteresis_up = 0;
	cz_hwmgr->boot_power_level.numSIMDToPowerDown = 0;
	cz_hwmgr->boot_power_level.display_wm = 0;
	cz_hwmgr->boot_power_level.vce_wm = 0;

	return 0;
}

static int cz_tf_reset_active_process_mask(struct pp_hwmgr *hwmgr, void *input,
					void *output, void *storage, int result)
{
	return 0;
}

static int cz_tf_upload_pptable_to_smu(struct pp_hwmgr *hwmgr, void *input,
				       void *output, void *storage, int result)
{
	struct SMU8_Fusion_ClkTable *clock_table;
	int ret;
	uint32_t i;
	void *table = NULL;
	pp_atomctrl_clock_dividers_kong dividers;

	struct phm_clock_voltage_dependency_table *vddc_table =
		hwmgr->dyn_state.vddc_dependency_on_sclk;
	struct phm_clock_voltage_dependency_table *vdd_gfx_table =
		hwmgr->dyn_state.vdd_gfx_dependency_on_sclk;
	struct phm_acp_clock_voltage_dependency_table *acp_table =
		hwmgr->dyn_state.acp_clock_voltage_dependency_table;
	struct phm_uvd_clock_voltage_dependency_table *uvd_table =
		hwmgr->dyn_state.uvd_clock_voltage_dependency_table;
	struct phm_vce_clock_voltage_dependency_table *vce_table =
		hwmgr->dyn_state.vce_clock_voltage_dependency_table;

	if (!hwmgr->need_pp_table_upload)
		return 0;

	ret = smum_download_powerplay_table(hwmgr->smumgr, &table);

	PP_ASSERT_WITH_CODE((0 == ret && NULL != table),
			    "Fail to get clock table from SMU!", return -EINVAL;);

	clock_table = (struct SMU8_Fusion_ClkTable *)table;

	/* patch clock table */
	PP_ASSERT_WITH_CODE((vddc_table->count <= CZ_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((vdd_gfx_table->count <= CZ_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((acp_table->count <= CZ_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((uvd_table->count <= CZ_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((vce_table->count <= CZ_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);

	for (i = 0; i < CZ_MAX_HARDWARE_POWERLEVELS; i++) {

		/* vddc_sclk */
		clock_table->SclkBreakdownTable.ClkLevel[i].GnbVid =
			(i < vddc_table->count) ? (uint8_t)vddc_table->entries[i].v : 0;
		clock_table->SclkBreakdownTable.ClkLevel[i].Frequency =
			(i < vddc_table->count) ? vddc_table->entries[i].clk : 0;

		atomctrl_get_engine_pll_dividers_kong(hwmgr,
						      clock_table->SclkBreakdownTable.ClkLevel[i].Frequency,
						      &dividers);

		clock_table->SclkBreakdownTable.ClkLevel[i].DfsDid =
			(uint8_t)dividers.pll_post_divider;

		/* vddgfx_sclk */
		clock_table->SclkBreakdownTable.ClkLevel[i].GfxVid =
			(i < vdd_gfx_table->count) ? (uint8_t)vdd_gfx_table->entries[i].v : 0;

		/* acp breakdown */
		clock_table->AclkBreakdownTable.ClkLevel[i].GfxVid =
			(i < acp_table->count) ? (uint8_t)acp_table->entries[i].v : 0;
		clock_table->AclkBreakdownTable.ClkLevel[i].Frequency =
			(i < acp_table->count) ? acp_table->entries[i].acpclk : 0;

		atomctrl_get_engine_pll_dividers_kong(hwmgr,
						      clock_table->AclkBreakdownTable.ClkLevel[i].Frequency,
						      &dividers);

		clock_table->AclkBreakdownTable.ClkLevel[i].DfsDid =
			(uint8_t)dividers.pll_post_divider;


		/* uvd breakdown */
		clock_table->VclkBreakdownTable.ClkLevel[i].GfxVid =
			(i < uvd_table->count) ? (uint8_t)uvd_table->entries[i].v : 0;
		clock_table->VclkBreakdownTable.ClkLevel[i].Frequency =
			(i < uvd_table->count) ? uvd_table->entries[i].vclk : 0;

		atomctrl_get_engine_pll_dividers_kong(hwmgr,
						      clock_table->VclkBreakdownTable.ClkLevel[i].Frequency,
						      &dividers);

		clock_table->VclkBreakdownTable.ClkLevel[i].DfsDid =
			(uint8_t)dividers.pll_post_divider;

		clock_table->DclkBreakdownTable.ClkLevel[i].GfxVid =
			(i < uvd_table->count) ? (uint8_t)uvd_table->entries[i].v : 0;
		clock_table->DclkBreakdownTable.ClkLevel[i].Frequency =
			(i < uvd_table->count) ? uvd_table->entries[i].dclk : 0;

		atomctrl_get_engine_pll_dividers_kong(hwmgr,
						      clock_table->DclkBreakdownTable.ClkLevel[i].Frequency,
						      &dividers);

		clock_table->DclkBreakdownTable.ClkLevel[i].DfsDid =
			(uint8_t)dividers.pll_post_divider;

		/* vce breakdown */
		clock_table->EclkBreakdownTable.ClkLevel[i].GfxVid =
			(i < vce_table->count) ? (uint8_t)vce_table->entries[i].v : 0;
		clock_table->EclkBreakdownTable.ClkLevel[i].Frequency =
			(i < vce_table->count) ? vce_table->entries[i].ecclk : 0;


		atomctrl_get_engine_pll_dividers_kong(hwmgr,
						      clock_table->EclkBreakdownTable.ClkLevel[i].Frequency,
						      &dividers);

		clock_table->EclkBreakdownTable.ClkLevel[i].DfsDid =
			(uint8_t)dividers.pll_post_divider;

	}
	ret = smum_upload_powerplay_table(hwmgr->smumgr);

	return ret;
}

static int cz_tf_init_sclk_limit(struct pp_hwmgr *hwmgr, void *input,
				 void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_clock_voltage_dependency_table *table =
					hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long clock = 0, level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	cz_hwmgr->sclk_dpm.soft_min_clk = table->entries[0].clk;
	cz_hwmgr->sclk_dpm.hard_min_clk = table->entries[0].clk;

	level = cz_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		clock = table->entries[level].clk;
	else
		clock = table->entries[table->count - 1].clk;

	cz_hwmgr->sclk_dpm.soft_max_clk = clock;
	cz_hwmgr->sclk_dpm.hard_max_clk = clock;

	return 0;
}

static int cz_tf_init_uvd_limit(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_uvd_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.uvd_clock_voltage_dependency_table;
	unsigned long clock = 0, level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	cz_hwmgr->uvd_dpm.soft_min_clk = 0;
	cz_hwmgr->uvd_dpm.hard_min_clk = 0;

	smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_GetMaxUvdLevel);
	level = smum_get_argument(hwmgr->smumgr);

	if (level < table->count)
		clock = table->entries[level].vclk;
	else
		clock = table->entries[table->count - 1].vclk;

	cz_hwmgr->uvd_dpm.soft_max_clk = clock;
	cz_hwmgr->uvd_dpm.hard_max_clk = clock;

	return 0;
}

static int cz_tf_init_vce_limit(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_vce_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.vce_clock_voltage_dependency_table;
	unsigned long clock = 0, level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	cz_hwmgr->vce_dpm.soft_min_clk = 0;
	cz_hwmgr->vce_dpm.hard_min_clk = 0;

	smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_GetMaxEclkLevel);
	level = smum_get_argument(hwmgr->smumgr);

	if (level < table->count)
		clock = table->entries[level].ecclk;
	else
		clock = table->entries[table->count - 1].ecclk;

	cz_hwmgr->vce_dpm.soft_max_clk = clock;
	cz_hwmgr->vce_dpm.hard_max_clk = clock;

	return 0;
}

static int cz_tf_init_acp_limit(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_acp_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.acp_clock_voltage_dependency_table;
	unsigned long clock = 0, level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	cz_hwmgr->acp_dpm.soft_min_clk = 0;
	cz_hwmgr->acp_dpm.hard_min_clk = 0;

	smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_GetMaxAclkLevel);
	level = smum_get_argument(hwmgr->smumgr);

	if (level < table->count)
		clock = table->entries[level].acpclk;
	else
		clock = table->entries[table->count - 1].acpclk;

	cz_hwmgr->acp_dpm.soft_max_clk = clock;
	cz_hwmgr->acp_dpm.hard_max_clk = clock;
	return 0;
}

static int cz_tf_init_power_gate_state(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	cz_hwmgr->uvd_power_gated = false;
	cz_hwmgr->vce_power_gated = false;
	cz_hwmgr->samu_power_gated = false;
	cz_hwmgr->acp_power_gated = false;
	cz_hwmgr->pgacpinit = true;

	return 0;
}

static int cz_tf_init_sclk_threshold(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	cz_hwmgr->low_sclk_interrupt_threshold = 0;

	return 0;
}
static int cz_tf_update_sclk_limit(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_clock_voltage_dependency_table *table =
					hwmgr->dyn_state.vddc_dependency_on_sclk;

	unsigned long clock = 0;
	unsigned long level;
	unsigned long stable_pstate_sclk;
	unsigned long percentage;

	cz_hwmgr->sclk_dpm.soft_min_clk = table->entries[0].clk;
	level = cz_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		cz_hwmgr->sclk_dpm.soft_max_clk  = table->entries[level].clk;
	else
		cz_hwmgr->sclk_dpm.soft_max_clk  = table->entries[table->count - 1].clk;

	clock = hwmgr->display_config.min_core_set_clock;
	if (clock == 0)
		printk(KERN_INFO "[ powerplay ] min_core_set_clock not set\n");

	if (cz_hwmgr->sclk_dpm.hard_min_clk != clock) {
		cz_hwmgr->sclk_dpm.hard_min_clk = clock;

		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_SetSclkHardMin,
						 cz_get_sclk_level(hwmgr,
					cz_hwmgr->sclk_dpm.hard_min_clk,
					     PPSMC_MSG_SetSclkHardMin));
	}

	clock = cz_hwmgr->sclk_dpm.soft_min_clk;

	/* update minimum clocks for Stable P-State feature */
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				     PHM_PlatformCaps_StablePState)) {
		percentage = 75;
		/*Sclk - calculate sclk value based on percentage and find FLOOR sclk from VddcDependencyOnSCLK table  */
		stable_pstate_sclk = (hwmgr->dyn_state.max_clock_voltage_on_ac.mclk *
					percentage) / 100;

		if (clock < stable_pstate_sclk)
			clock = stable_pstate_sclk;
	} else {
		if (clock < hwmgr->gfx_arbiter.sclk)
			clock = hwmgr->gfx_arbiter.sclk;
	}

	if (cz_hwmgr->sclk_dpm.soft_min_clk != clock) {
		cz_hwmgr->sclk_dpm.soft_min_clk = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_SetSclkSoftMin,
						cz_get_sclk_level(hwmgr,
					cz_hwmgr->sclk_dpm.soft_min_clk,
					     PPSMC_MSG_SetSclkSoftMin));
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_StablePState) &&
			 cz_hwmgr->sclk_dpm.soft_max_clk != clock) {
		cz_hwmgr->sclk_dpm.soft_max_clk = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_SetSclkSoftMax,
						cz_get_sclk_level(hwmgr,
					cz_hwmgr->sclk_dpm.soft_max_clk,
					PPSMC_MSG_SetSclkSoftMax));
	}

	return 0;
}

static int cz_tf_set_deep_sleep_sclk_threshold(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SclkDeepSleep)) {
		uint32_t clks = hwmgr->display_config.min_core_set_clock_in_sr;
		if (clks == 0)
			clks = CZ_MIN_DEEP_SLEEP_SCLK;

		PP_DBG_LOG("Setting Deep Sleep Clock: %d\n", clks);

		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetMinDeepSleepSclk,
				clks);
	}

	return 0;
}

static int cz_tf_set_watermark_threshold(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr =
				  (struct cz_hwmgr *)(hwmgr->backend);

	smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetWatermarkFrequency,
					cz_hwmgr->sclk_dpm.soft_max_clk);

	return 0;
}

static int cz_tf_set_enabled_levels(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	return 0;
}


static int cz_tf_enable_nb_dpm(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	int ret = 0;

	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	unsigned long dpm_features = 0;

	if (!cz_hwmgr->is_nb_dpm_enabled) {
		PP_DBG_LOG("enabling ALL SMU features.\n");
		dpm_features |= NB_DPM_MASK;
		ret = smum_send_msg_to_smc_with_parameter(
							  hwmgr->smumgr,
							  PPSMC_MSG_EnableAllSmuFeatures,
							  dpm_features);
		if (ret == 0)
			cz_hwmgr->is_nb_dpm_enabled = true;
	}

	return ret;
}

static int cz_nbdpm_pstate_enable_disable(struct pp_hwmgr *hwmgr, bool enable, bool lock)
{
	struct cz_hwmgr *hw_data = (struct cz_hwmgr *)(hwmgr->backend);

	if (hw_data->is_nb_dpm_enabled) {
		if (enable) {
			PP_DBG_LOG("enable Low Memory PState.\n");

			return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_EnableLowMemoryPstate,
						(lock ? 1 : 0));
		} else {
			PP_DBG_LOG("disable Low Memory PState.\n");

			return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_DisableLowMemoryPstate,
						(lock ? 1 : 0));
		}
	}

	return 0;
}

static int cz_tf_update_low_mem_pstate(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	bool disable_switch;
	bool enable_low_mem_state;
	struct cz_hwmgr *hw_data = (struct cz_hwmgr *)(hwmgr->backend);
	const struct phm_set_power_state_input *states = (struct phm_set_power_state_input *)input;
	const struct cz_power_state *pnew_state = cast_const_PhwCzPowerState(states->pnew_state);

	if (hw_data->sys_info.nb_dpm_enable) {
		disable_switch = hw_data->cc6_settings.nb_pstate_switch_disable ? true : false;
		enable_low_mem_state = hw_data->cc6_settings.nb_pstate_switch_disable ? false : true;

		if (pnew_state->action == FORCE_HIGH)
			cz_nbdpm_pstate_enable_disable(hwmgr, false, disable_switch);
		else if (pnew_state->action == CANCEL_FORCE_HIGH)
			cz_nbdpm_pstate_enable_disable(hwmgr, true, disable_switch);
		else
			cz_nbdpm_pstate_enable_disable(hwmgr, enable_low_mem_state, disable_switch);
	}
	return 0;
}

static const struct phm_master_table_item cz_set_power_state_list[] = {
	{NULL, cz_tf_update_sclk_limit},
	{NULL, cz_tf_set_deep_sleep_sclk_threshold},
	{NULL, cz_tf_set_watermark_threshold},
	{NULL, cz_tf_set_enabled_levels},
	{NULL, cz_tf_enable_nb_dpm},
	{NULL, cz_tf_update_low_mem_pstate},
	{NULL, NULL}
};

static const struct phm_master_table_header cz_set_power_state_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_set_power_state_list
};

static const struct phm_master_table_item cz_setup_asic_list[] = {
	{NULL, cz_tf_reset_active_process_mask},
	{NULL, cz_tf_upload_pptable_to_smu},
	{NULL, cz_tf_init_sclk_limit},
	{NULL, cz_tf_init_uvd_limit},
	{NULL, cz_tf_init_vce_limit},
	{NULL, cz_tf_init_acp_limit},
	{NULL, cz_tf_init_power_gate_state},
	{NULL, cz_tf_init_sclk_threshold},
	{NULL, NULL}
};

static const struct phm_master_table_header cz_setup_asic_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_setup_asic_list
};

static int cz_tf_power_up_display_clock_sys_pll(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	struct cz_hwmgr *hw_data = (struct cz_hwmgr *)(hwmgr->backend);
	hw_data->disp_clk_bypass_pending = false;
	hw_data->disp_clk_bypass = false;

	return 0;
}

static int cz_tf_clear_nb_dpm_flag(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	struct cz_hwmgr *hw_data = (struct cz_hwmgr *)(hwmgr->backend);
	hw_data->is_nb_dpm_enabled = false;

	return 0;
}

static int cz_tf_reset_cc6_data(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	struct cz_hwmgr *hw_data = (struct cz_hwmgr *)(hwmgr->backend);

	hw_data->cc6_settings.cc6_setting_changed = false;
	hw_data->cc6_settings.cpu_pstate_separation_time = 0;
	hw_data->cc6_settings.cpu_cc6_disable = false;
	hw_data->cc6_settings.cpu_pstate_disable = false;

	return 0;
}

static const struct phm_master_table_item cz_power_down_asic_list[] = {
	{NULL, cz_tf_power_up_display_clock_sys_pll},
	{NULL, cz_tf_clear_nb_dpm_flag},
	{NULL, cz_tf_reset_cc6_data},
	{NULL, NULL}
};

static const struct phm_master_table_header cz_power_down_asic_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_power_down_asic_list
};

static int cz_tf_program_voting_clients(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	PHMCZ_WRITE_SMC_REGISTER(hwmgr->device, CG_FREQ_TRAN_VOTING_0,
				PPCZ_VOTINGRIGHTSCLIENTS_DFLT0);
	return 0;
}

static int cz_tf_start_dpm(struct pp_hwmgr *hwmgr, void *input, void *output,
			   void *storage, int result)
{
	int res = 0xff;
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	unsigned long dpm_features = 0;

	cz_hwmgr->dpm_flags |= DPMFlags_SCLK_Enabled;
	dpm_features |= SCLK_DPM_MASK;

	res = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_EnableAllSmuFeatures,
				dpm_features);

	return res;
}

static int cz_tf_program_bootup_state(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	cz_hwmgr->sclk_dpm.soft_min_clk = cz_hwmgr->sys_info.bootup_engine_clock;
	cz_hwmgr->sclk_dpm.soft_max_clk = cz_hwmgr->sys_info.bootup_engine_clock;

	smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetSclkSoftMin,
				cz_get_sclk_level(hwmgr,
				cz_hwmgr->sclk_dpm.soft_min_clk,
				PPSMC_MSG_SetSclkSoftMin));

	smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetSclkSoftMax,
				cz_get_sclk_level(hwmgr,
				cz_hwmgr->sclk_dpm.soft_max_clk,
				PPSMC_MSG_SetSclkSoftMax));

	return 0;
}

static int cz_tf_reset_acp_boot_level(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	cz_hwmgr->acp_boot_level = 0xff;
	return 0;
}

static bool cz_dpm_check_smu_features(struct pp_hwmgr *hwmgr,
				unsigned long check_feature)
{
	int result;
	unsigned long features;

	result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, PPSMC_MSG_GetFeatureStatus, 0);
	if (result == 0) {
		features = smum_get_argument(hwmgr->smumgr);
		if (features & check_feature)
			return true;
	}

	return result;
}

static int cz_tf_check_for_dpm_disabled(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	if (cz_dpm_check_smu_features(hwmgr, SMU_EnabledFeatureScoreboard_SclkDpmOn))
		return PP_Result_TableImmediateExit;
	return 0;
}

static int cz_tf_enable_didt(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	/* TO DO */
	return 0;
}

static int cz_tf_check_for_dpm_enabled(struct pp_hwmgr *hwmgr,
						void *input, void *output,
						void *storage, int result)
{
	if (!cz_dpm_check_smu_features(hwmgr,
			     SMU_EnabledFeatureScoreboard_SclkDpmOn))
		return PP_Result_TableImmediateExit;
	return 0;
}

static const struct phm_master_table_item cz_disable_dpm_list[] = {
	{ NULL, cz_tf_check_for_dpm_enabled},
	{NULL, NULL},
};


static const struct phm_master_table_header cz_disable_dpm_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_disable_dpm_list
};

static const struct phm_master_table_item cz_enable_dpm_list[] = {
	{ NULL, cz_tf_check_for_dpm_disabled },
	{ NULL, cz_tf_program_voting_clients },
	{ NULL, cz_tf_start_dpm},
	{ NULL, cz_tf_program_bootup_state},
	{ NULL, cz_tf_enable_didt },
	{ NULL, cz_tf_reset_acp_boot_level },
	{NULL, NULL},
};

static const struct phm_master_table_header cz_enable_dpm_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_enable_dpm_list
};

static int cz_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps)
{
	struct cz_power_state *cz_ps =
				cast_PhwCzPowerState(&prequest_ps->hardware);

	const struct cz_power_state *cz_current_ps =
				cast_const_PhwCzPowerState(&pcurrent_ps->hardware);

	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct PP_Clocks clocks = {0, 0, 0, 0};
	bool force_high;
	uint32_t  num_of_active_displays = 0;
	struct cgs_display_info info = {0};

	cz_ps->evclk = hwmgr->vce_arbiter.evclk;
	cz_ps->ecclk = hwmgr->vce_arbiter.ecclk;

	cz_ps->need_dfs_bypass = true;

	cz_hwmgr->video_start = (hwmgr->uvd_arbiter.vclk != 0 || hwmgr->uvd_arbiter.dclk != 0 ||
				hwmgr->vce_arbiter.evclk != 0 || hwmgr->vce_arbiter.ecclk != 0);

	cz_hwmgr->battery_state = (PP_StateUILabel_Battery == prequest_ps->classification.ui_label);

	clocks.memoryClock = hwmgr->display_config.min_mem_set_clock != 0 ?
				hwmgr->display_config.min_mem_set_clock :
				cz_hwmgr->sys_info.nbp_memory_clock[1];

	cgs_get_active_displays_info(hwmgr->device, &info);
	num_of_active_displays = info.display_count;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState))
		clocks.memoryClock = hwmgr->dyn_state.max_clock_voltage_on_ac.mclk;

	if (clocks.memoryClock < hwmgr->gfx_arbiter.mclk)
		clocks.memoryClock = hwmgr->gfx_arbiter.mclk;

	force_high = (clocks.memoryClock > cz_hwmgr->sys_info.nbp_memory_clock[CZ_NUM_NBPMEMORYCLOCK - 1])
			|| (num_of_active_displays >= 3);

	cz_ps->action = cz_current_ps->action;

	if (!force_high && (cz_ps->action == FORCE_HIGH))
		cz_ps->action = CANCEL_FORCE_HIGH;
	else if (force_high && (cz_ps->action != FORCE_HIGH))
		cz_ps->action = FORCE_HIGH;
	else
		cz_ps->action = DO_NOTHING;

	return 0;
}

static int cz_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct cz_hwmgr *data;

	data = kzalloc(sizeof(struct cz_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	result = cz_initialize_dpm_defaults(hwmgr);
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] cz_initialize_dpm_defaults failed\n");
		return result;
	}

	result = cz_get_system_info_data(hwmgr);
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] cz_get_system_info_data failed\n");
		return result;
	}

	cz_construct_boot_state(hwmgr);

	result = phm_construct_table(hwmgr, &cz_setup_asic_master,
				&(hwmgr->setup_asic));
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] Fail to construct setup ASIC\n");
		return result;
	}

	result = phm_construct_table(hwmgr, &cz_power_down_asic_master,
				&(hwmgr->power_down_asic));
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] Fail to construct power down ASIC\n");
		return result;
	}

	result = phm_construct_table(hwmgr, &cz_disable_dpm_master,
				&(hwmgr->disable_dynamic_state_management));
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] Fail to disable_dynamic_state\n");
		return result;
	}
	result = phm_construct_table(hwmgr, &cz_enable_dpm_master,
				&(hwmgr->enable_dynamic_state_management));
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] Fail to enable_dynamic_state\n");
		return result;
	}
	result = phm_construct_table(hwmgr, &cz_set_power_state_master,
				&(hwmgr->set_power_state));
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] Fail to construct set_power_state\n");
		return result;
	}
	hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =  CZ_MAX_HARDWARE_POWERLEVELS;

	result = phm_construct_table(hwmgr, &cz_phm_enable_clock_power_gatings_master, &(hwmgr->enable_clock_power_gatings));
	if (result != 0) {
		printk(KERN_ERR "[ powerplay ] Fail to construct enable_clock_power_gatings\n");
		return result;
	}
	return result;
}

static int cz_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	if (hwmgr != NULL && hwmgr->backend != NULL) {
		kfree(hwmgr->backend);
		kfree(hwmgr);
	}
	return 0;
}

static int cz_phm_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	if (cz_hwmgr->sclk_dpm.soft_min_clk !=
				cz_hwmgr->sclk_dpm.soft_max_clk)
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_SetSclkSoftMin,
						cz_get_sclk_level(hwmgr,
						cz_hwmgr->sclk_dpm.soft_max_clk,
						PPSMC_MSG_SetSclkSoftMin));
	return 0;
}

static int cz_phm_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long clock = 0, level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	cz_hwmgr->sclk_dpm.soft_min_clk = table->entries[0].clk;
	cz_hwmgr->sclk_dpm.hard_min_clk = table->entries[0].clk;

	level = cz_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		clock = table->entries[level].clk;
	else
		clock = table->entries[table->count - 1].clk;

	cz_hwmgr->sclk_dpm.soft_max_clk = clock;
	cz_hwmgr->sclk_dpm.hard_max_clk = clock;

	smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetSclkSoftMin,
				cz_get_sclk_level(hwmgr,
				cz_hwmgr->sclk_dpm.soft_min_clk,
				PPSMC_MSG_SetSclkSoftMin));

	smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetSclkSoftMax,
				cz_get_sclk_level(hwmgr,
				cz_hwmgr->sclk_dpm.soft_max_clk,
				PPSMC_MSG_SetSclkSoftMax));

	return 0;
}

static int cz_phm_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	if (cz_hwmgr->sclk_dpm.soft_min_clk !=
				cz_hwmgr->sclk_dpm.soft_max_clk) {
		cz_hwmgr->sclk_dpm.soft_max_clk =
			cz_hwmgr->sclk_dpm.soft_min_clk;

		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetSclkSoftMax,
				cz_get_sclk_level(hwmgr,
				cz_hwmgr->sclk_dpm.soft_max_clk,
				PPSMC_MSG_SetSclkSoftMax));
	}

	return 0;
}

static int cz_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = cz_phm_force_dpm_highest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = cz_phm_force_dpm_lowest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = cz_phm_unforce_dpm_levels(hwmgr);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	hwmgr->dpm_level = level;

	return ret;
}

int cz_dpm_powerdown_uvd(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					 PHM_PlatformCaps_UVDPowerGating))
		return smum_send_msg_to_smc(hwmgr->smumgr,
						     PPSMC_MSG_UVDPowerOFF);
	return 0;
}

int cz_dpm_powerup_uvd(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					 PHM_PlatformCaps_UVDPowerGating)) {
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				  PHM_PlatformCaps_UVDDynamicPowerGating)) {
			return smum_send_msg_to_smc_with_parameter(
								hwmgr->smumgr,
						   PPSMC_MSG_UVDPowerON, 1);
		} else {
			return smum_send_msg_to_smc_with_parameter(
								hwmgr->smumgr,
						   PPSMC_MSG_UVDPowerON, 0);
		}
	}

	return 0;
}

int cz_dpm_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_uvd_clock_voltage_dependency_table *ptable =
		hwmgr->dyn_state.uvd_clock_voltage_dependency_table;

	if (!bgate) {
		/* Stable Pstate is enabled and we need to set the UVD DPM to highest level */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					 PHM_PlatformCaps_StablePState)) {
			cz_hwmgr->uvd_dpm.hard_min_clk =
				   ptable->entries[ptable->count - 1].vclk;

			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						     PPSMC_MSG_SetUvdHardMin,
						      cz_get_uvd_level(hwmgr,
					     cz_hwmgr->uvd_dpm.hard_min_clk,
						   PPSMC_MSG_SetUvdHardMin));

			cz_enable_disable_uvd_dpm(hwmgr, true);
		} else {
			cz_enable_disable_uvd_dpm(hwmgr, true);
		}
	} else {
		cz_enable_disable_uvd_dpm(hwmgr, false);
	}

	return 0;
}

int  cz_dpm_update_vce_dpm(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_vce_clock_voltage_dependency_table *ptable =
		hwmgr->dyn_state.vce_clock_voltage_dependency_table;

	/* Stable Pstate is enabled and we need to set the VCE DPM to highest level */
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					 PHM_PlatformCaps_StablePState)) {
		cz_hwmgr->vce_dpm.hard_min_clk =
				  ptable->entries[ptable->count - 1].ecclk;

		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetEclkHardMin,
					cz_get_eclk_level(hwmgr,
					     cz_hwmgr->vce_dpm.hard_min_clk,
						PPSMC_MSG_SetEclkHardMin));
	} else {
		/*Program HardMin based on the vce_arbiter.ecclk */
		if (hwmgr->vce_arbiter.ecclk == 0) {
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					    PPSMC_MSG_SetEclkHardMin, 0);
		/* disable ECLK DPM 0. Otherwise VCE could hang if
		 * switching SCLK from DPM 0 to 6/7 */
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetEclkSoftMin, 1);
		} else {
			cz_hwmgr->vce_dpm.hard_min_clk = hwmgr->vce_arbiter.ecclk;
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_SetEclkHardMin,
						cz_get_eclk_level(hwmgr,
						cz_hwmgr->vce_dpm.hard_min_clk,
						PPSMC_MSG_SetEclkHardMin));
		}
	}
	return 0;
}

int cz_dpm_powerdown_vce(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					 PHM_PlatformCaps_VCEPowerGating))
		return smum_send_msg_to_smc(hwmgr->smumgr,
						     PPSMC_MSG_VCEPowerOFF);
	return 0;
}

int cz_dpm_powerup_vce(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					 PHM_PlatformCaps_VCEPowerGating))
		return smum_send_msg_to_smc(hwmgr->smumgr,
						     PPSMC_MSG_VCEPowerON);
	return 0;
}

static int cz_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	return cz_hwmgr->sys_info.bootup_uma_clock;
}

static int cz_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct cz_power_state  *cz_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	cz_ps = cast_PhwCzPowerState(&ps->hardware);

	if (low)
		return cz_ps->levels[0].engineClock;
	else
		return cz_ps->levels[cz_ps->level-1].engineClock;
}

static int cz_dpm_patch_boot_state(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *hw_ps)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct cz_power_state *cz_ps = cast_PhwCzPowerState(hw_ps);

	cz_ps->level = 1;
	cz_ps->nbps_flags = 0;
	cz_ps->bapm_flags = 0;
	cz_ps->levels[0] = cz_hwmgr->boot_power_level;

	return 0;
}

static int cz_dpm_get_pp_table_entry_callback(
						     struct pp_hwmgr *hwmgr,
					   struct pp_hw_power_state *hw_ps,
							  unsigned int index,
						     const void *clock_info)
{
	struct cz_power_state *cz_ps = cast_PhwCzPowerState(hw_ps);

	const ATOM_PPLIB_CZ_CLOCK_INFO *cz_clock_info = clock_info;

	struct phm_clock_voltage_dependency_table *table =
				    hwmgr->dyn_state.vddc_dependency_on_sclk;
	uint8_t clock_info_index = cz_clock_info->index;

	if (clock_info_index > (uint8_t)(hwmgr->platform_descriptor.hardwareActivityPerformanceLevels - 1))
		clock_info_index = (uint8_t)(hwmgr->platform_descriptor.hardwareActivityPerformanceLevels - 1);

	cz_ps->levels[index].engineClock = table->entries[clock_info_index].clk;
	cz_ps->levels[index].vddcIndex = (uint8_t)table->entries[clock_info_index].v;

	cz_ps->level = index + 1;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		cz_ps->levels[index].dsDividerIndex = 5;
		cz_ps->levels[index].ssDividerIndex = 5;
	}

	return 0;
}

static int cz_dpm_get_num_of_pp_table_entries(struct pp_hwmgr *hwmgr)
{
	int result;
	unsigned long ret = 0;

	result = pp_tables_get_num_of_entries(hwmgr, &ret);

	return result ? 0 : ret;
}

static int cz_dpm_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		    unsigned long entry, struct pp_power_state *ps)
{
	int result;
	struct cz_power_state *cz_ps;

	ps->hardware.magic = PhwCz_Magic;

	cz_ps = cast_PhwCzPowerState(&(ps->hardware));

	result = pp_tables_get_entry(hwmgr, entry, ps,
			cz_dpm_get_pp_table_entry_callback);

	cz_ps->uvd_clocks.vclk = ps->uvd_clocks.VCLK;
	cz_ps->uvd_clocks.dclk = ps->uvd_clocks.DCLK;

	return result;
}

static int cz_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct cz_power_state);
}

static void cz_hw_print_display_cfg(
	const struct cc6_settings *cc6_settings)
{
	PP_DBG_LOG("New Display Configuration:\n");

	PP_DBG_LOG("   cpu_cc6_disable: %d\n",
			cc6_settings->cpu_cc6_disable);
	PP_DBG_LOG("   cpu_pstate_disable: %d\n",
			cc6_settings->cpu_pstate_disable);
	PP_DBG_LOG("   nb_pstate_switch_disable: %d\n",
			cc6_settings->nb_pstate_switch_disable);
	PP_DBG_LOG("   cpu_pstate_separation_time: %d\n\n",
			cc6_settings->cpu_pstate_separation_time);
}

 static int cz_set_cpu_power_state(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *hw_data = (struct cz_hwmgr *)(hwmgr->backend);
	uint32_t data = 0;

	if (hw_data->cc6_settings.cc6_setting_changed) {

		hw_data->cc6_settings.cc6_setting_changed = false;

		cz_hw_print_display_cfg(&hw_data->cc6_settings);

		data |= (hw_data->cc6_settings.cpu_pstate_separation_time
			& PWRMGT_SEPARATION_TIME_MASK)
			<< PWRMGT_SEPARATION_TIME_SHIFT;

		data |= (hw_data->cc6_settings.cpu_cc6_disable ? 0x1 : 0x0)
			<< PWRMGT_DISABLE_CPU_CSTATES_SHIFT;

		data |= (hw_data->cc6_settings.cpu_pstate_disable ? 0x1 : 0x0)
			<< PWRMGT_DISABLE_CPU_PSTATES_SHIFT;

		PP_DBG_LOG("SetDisplaySizePowerParams data: 0x%X\n",
			data);

		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_SetDisplaySizePowerParams,
						data);
	}

	return 0;
}


static int cz_store_cc6_data(struct pp_hwmgr *hwmgr, uint32_t separation_time,
			bool cc6_disable, bool pstate_disable, bool pstate_switch_disable)
{
	struct cz_hwmgr *hw_data = (struct cz_hwmgr *)(hwmgr->backend);

	if (separation_time !=
	    hw_data->cc6_settings.cpu_pstate_separation_time ||
	    cc6_disable != hw_data->cc6_settings.cpu_cc6_disable ||
	    pstate_disable != hw_data->cc6_settings.cpu_pstate_disable ||
	    pstate_switch_disable != hw_data->cc6_settings.nb_pstate_switch_disable) {

		hw_data->cc6_settings.cc6_setting_changed = true;

		hw_data->cc6_settings.cpu_pstate_separation_time =
			separation_time;
		hw_data->cc6_settings.cpu_cc6_disable =
			cc6_disable;
		hw_data->cc6_settings.cpu_pstate_disable =
			pstate_disable;
		hw_data->cc6_settings.nb_pstate_switch_disable =
			pstate_switch_disable;

	}

	return 0;
}

static int cz_get_dal_power_level(struct pp_hwmgr *hwmgr,
		struct amd_pp_simple_clock_info *info)
{
	uint32_t i;
	const struct phm_clock_voltage_dependency_table *table =
			hwmgr->dyn_state.vddc_dep_on_dal_pwrl;
	const struct phm_clock_and_voltage_limits *limits =
			&hwmgr->dyn_state.max_clock_voltage_on_ac;

	info->engine_max_clock = limits->sclk;
	info->memory_max_clock = limits->mclk;

	for (i = table->count - 1; i > 0; i--) {
		if (limits->vddc >= table->entries[i].v) {
			info->level = table->entries[i].clk;
			return 0;
		}
	}
	return -EINVAL;
}

static int cz_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	switch (type) {
	case PP_SCLK:
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetSclkSoftMin,
				mask);
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetSclkSoftMax,
				mask);
		break;
	default:
		break;
	}

	return 0;
}

static int cz_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct phm_clock_voltage_dependency_table *sclk_table =
			hwmgr->dyn_state.vddc_dependency_on_sclk;
	int i, now, size = 0;

	switch (type) {
	case PP_SCLK:
		now = PHM_GET_FIELD(cgs_read_ind_register(hwmgr->device,
				CGS_IND_REG__SMC,
				ixTARGET_AND_CURRENT_PROFILE_INDEX),
				TARGET_AND_CURRENT_PROFILE_INDEX,
				CURR_SCLK_INDEX);

		for (i = 0; i < sclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, sclk_table->entries[i].clk / 100,
					(i == now) ? "*" : "");
		break;
	default:
		break;
	}
	return size;
}

static int cz_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level)
{
	const struct cz_power_state *ps;
	struct cz_hwmgr *data;
	uint32_t level_index;
	uint32_t i;

	if (level == NULL || hwmgr == NULL || state == NULL)
		return -EINVAL;

	data = (struct cz_hwmgr *)(hwmgr->backend);
	ps = cast_const_PhwCzPowerState(state);

	level_index = index > ps->level - 1 ? ps->level - 1 : index;
	level->coreClock = ps->levels[level_index].engineClock;

	if (designation == PHM_PerformanceLevelDesignation_PowerContainment) {
		for (i = 1; i < ps->level; i++) {
			if (ps->levels[i].engineClock > data->dce_slow_sclk_threshold) {
				level->coreClock = ps->levels[i].engineClock;
				break;
			}
		}
	}

	if (level_index == 0)
		level->memory_clock = data->sys_info.nbp_memory_clock[CZ_NUM_NBPMEMORYCLOCK - 1];
	else
		level->memory_clock = data->sys_info.nbp_memory_clock[0];

	level->vddc = (cz_convert_8Bit_index_to_voltage(hwmgr, ps->levels[level_index].vddcIndex) + 2) / 4;
	level->nonLocalMemoryFreq = 0;
	level->nonLocalMemoryWidth = 0;

	return 0;
}

static int cz_get_current_shallow_sleep_clocks(struct pp_hwmgr *hwmgr,
	const struct pp_hw_power_state *state, struct pp_clock_info *clock_info)
{
	const struct cz_power_state *ps = cast_const_PhwCzPowerState(state);

	clock_info->min_eng_clk = ps->levels[0].engineClock / (1 << (ps->levels[0].ssDividerIndex));
	clock_info->max_eng_clk = ps->levels[ps->level - 1].engineClock / (1 << (ps->levels[ps->level - 1].ssDividerIndex));

	return 0;
}

static int cz_get_clock_by_type(struct pp_hwmgr *hwmgr, enum amd_pp_clock_type type,
						struct amd_pp_clocks *clocks)
{
	struct cz_hwmgr *data = (struct cz_hwmgr *)(hwmgr->backend);
	int i;
	struct phm_clock_voltage_dependency_table *table;

	clocks->count = cz_get_max_sclk_level(hwmgr);
	switch (type) {
	case amd_pp_disp_clock:
		for (i = 0; i < clocks->count; i++)
			clocks->clock[i] = data->sys_info.display_clock[i];
		break;
	case amd_pp_sys_clock:
		table = hwmgr->dyn_state.vddc_dependency_on_sclk;
		for (i = 0; i < clocks->count; i++)
			clocks->clock[i] = table->entries[i].clk;
		break;
	case amd_pp_mem_clock:
		clocks->count = CZ_NUM_NBPMEMORYCLOCK;
		for (i = 0; i < clocks->count; i++)
			clocks->clock[i] = data->sys_info.nbp_memory_clock[clocks->count - 1 - i];
		break;
	default:
		return -1;
	}

	return 0;
}

static int cz_get_max_high_clocks(struct pp_hwmgr *hwmgr, struct amd_pp_simple_clock_info *clocks)
{
	struct phm_clock_voltage_dependency_table *table =
					hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long level;
	const struct phm_clock_and_voltage_limits *limits =
			&hwmgr->dyn_state.max_clock_voltage_on_ac;

	if ((NULL == table) || (table->count <= 0) || (clocks == NULL))
		return -EINVAL;

	level = cz_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		clocks->engine_max_clock = table->entries[level].clk;
	else
		clocks->engine_max_clock = table->entries[table->count - 1].clk;

	clocks->memory_max_clock = limits->mclk;

	return 0;
}

static int cz_thermal_get_temperature(struct pp_hwmgr *hwmgr)
{
	int actual_temp = 0;
	uint32_t val = cgs_read_ind_register(hwmgr->device,
					     CGS_IND_REG__SMC, ixTHM_TCON_CUR_TMP);
	uint32_t temp = PHM_GET_FIELD(val, THM_TCON_CUR_TMP, CUR_TEMP);

	if (PHM_GET_FIELD(val, THM_TCON_CUR_TMP, CUR_TEMP_RANGE_SEL))
		actual_temp = ((temp / 8) - 49) * PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	else
		actual_temp = (temp / 8) * PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return actual_temp;
}

static int cz_read_sensor(struct pp_hwmgr *hwmgr, int idx, int32_t *value)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);

	struct phm_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.vddc_dependency_on_sclk;

	struct phm_vce_clock_voltage_dependency_table *vce_table =
		hwmgr->dyn_state.vce_clock_voltage_dependency_table;

	struct phm_uvd_clock_voltage_dependency_table *uvd_table =
		hwmgr->dyn_state.uvd_clock_voltage_dependency_table;

	uint32_t sclk_index = PHM_GET_FIELD(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixTARGET_AND_CURRENT_PROFILE_INDEX),
					TARGET_AND_CURRENT_PROFILE_INDEX, CURR_SCLK_INDEX);
	uint32_t uvd_index = PHM_GET_FIELD(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixTARGET_AND_CURRENT_PROFILE_INDEX_2),
					TARGET_AND_CURRENT_PROFILE_INDEX_2, CURR_UVD_INDEX);
	uint32_t vce_index = PHM_GET_FIELD(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixTARGET_AND_CURRENT_PROFILE_INDEX_2),
					TARGET_AND_CURRENT_PROFILE_INDEX_2, CURR_VCE_INDEX);

	uint32_t sclk, vclk, dclk, ecclk, tmp, activity_percent;
	uint16_t vddnb, vddgfx;
	int result;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		if (sclk_index < NUM_SCLK_LEVELS) {
			sclk = table->entries[sclk_index].clk;
			*value = sclk;
			return 0;
		}
		return -EINVAL;
	case AMDGPU_PP_SENSOR_VDDNB:
		tmp = (cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixSMUSVI_NB_CURRENTVID) &
			CURRENT_NB_VID_MASK) >> CURRENT_NB_VID__SHIFT;
		vddnb = cz_convert_8Bit_index_to_voltage(hwmgr, tmp);
		*value = vddnb;
		return 0;
	case AMDGPU_PP_SENSOR_VDDGFX:
		tmp = (cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixSMUSVI_GFX_CURRENTVID) &
			CURRENT_GFX_VID_MASK) >> CURRENT_GFX_VID__SHIFT;
		vddgfx = cz_convert_8Bit_index_to_voltage(hwmgr, (u16)tmp);
		*value = vddgfx;
		return 0;
	case AMDGPU_PP_SENSOR_UVD_VCLK:
		if (!cz_hwmgr->uvd_power_gated) {
			if (uvd_index >= CZ_MAX_HARDWARE_POWERLEVELS) {
				return -EINVAL;
			} else {
				vclk = uvd_table->entries[uvd_index].vclk;
				*value = vclk;
				return 0;
			}
		}
		*value = 0;
		return 0;
	case AMDGPU_PP_SENSOR_UVD_DCLK:
		if (!cz_hwmgr->uvd_power_gated) {
			if (uvd_index >= CZ_MAX_HARDWARE_POWERLEVELS) {
				return -EINVAL;
			} else {
				dclk = uvd_table->entries[uvd_index].dclk;
				*value = dclk;
				return 0;
			}
		}
		*value = 0;
		return 0;
	case AMDGPU_PP_SENSOR_VCE_ECCLK:
		if (!cz_hwmgr->vce_power_gated) {
			if (vce_index >= CZ_MAX_HARDWARE_POWERLEVELS) {
				return -EINVAL;
			} else {
				ecclk = vce_table->entries[vce_index].ecclk;
				*value = ecclk;
				return 0;
			}
		}
		*value = 0;
		return 0;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		result = smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_GetAverageGraphicsActivity);
		if (0 == result) {
			activity_percent = cgs_read_register(hwmgr->device, mmSMU_MP1_SRBM2P_ARG_0);
			activity_percent = activity_percent > 100 ? 100 : activity_percent;
		} else {
			activity_percent = 50;
		}
		*value = activity_percent;
		return 0;
	case AMDGPU_PP_SENSOR_UVD_POWER:
		*value = cz_hwmgr->uvd_power_gated ? 0 : 1;
		return 0;
	case AMDGPU_PP_SENSOR_VCE_POWER:
		*value = cz_hwmgr->vce_power_gated ? 0 : 1;
		return 0;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*value = cz_thermal_get_temperature(hwmgr);
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct pp_hwmgr_func cz_hwmgr_funcs = {
	.backend_init = cz_hwmgr_backend_init,
	.backend_fini = cz_hwmgr_backend_fini,
	.asic_setup = NULL,
	.apply_state_adjust_rules = cz_apply_state_adjust_rules,
	.force_dpm_level = cz_dpm_force_dpm_level,
	.get_power_state_size = cz_get_power_state_size,
	.powerdown_uvd = cz_dpm_powerdown_uvd,
	.powergate_uvd = cz_dpm_powergate_uvd,
	.powergate_vce = cz_dpm_powergate_vce,
	.get_mclk = cz_dpm_get_mclk,
	.get_sclk = cz_dpm_get_sclk,
	.patch_boot_state = cz_dpm_patch_boot_state,
	.get_pp_table_entry = cz_dpm_get_pp_table_entry,
	.get_num_of_pp_table_entries = cz_dpm_get_num_of_pp_table_entries,
	.set_cpu_power_state = cz_set_cpu_power_state,
	.store_cc6_data = cz_store_cc6_data,
	.force_clock_level = cz_force_clock_level,
	.print_clock_levels = cz_print_clock_levels,
	.get_dal_power_level = cz_get_dal_power_level,
	.get_performance_level = cz_get_performance_level,
	.get_current_shallow_sleep_clocks = cz_get_current_shallow_sleep_clocks,
	.get_clock_by_type = cz_get_clock_by_type,
	.get_max_high_clocks = cz_get_max_high_clocks,
	.read_sensor = cz_read_sensor,
};

int cz_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &cz_hwmgr_funcs;
	hwmgr->pptable_func = &pptable_funcs;
	return 0;
}
