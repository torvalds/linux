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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "atom-types.h"
#include "atombios.h"
#include "processpptables.h"
#include "cgs_common.h"
#include "smu/smu_8_0_d.h"
#include "smu8_fusion.h"
#include "smu/smu_8_0_sh_mask.h"
#include "smumgr.h"
#include "hwmgr.h"
#include "hardwaremanager.h"
#include "cz_ppsmc.h"
#include "smu8_hwmgr.h"
#include "power_state.h"
#include "pp_thermal.h"

#define ixSMUSVI_NB_CURRENTVID 0xD8230044
#define CURRENT_NB_VID_MASK 0xff000000
#define CURRENT_NB_VID__SHIFT 24
#define ixSMUSVI_GFX_CURRENTVID  0xD8230048
#define CURRENT_GFX_VID_MASK 0xff000000
#define CURRENT_GFX_VID__SHIFT 24

static const unsigned long smu8_magic = (unsigned long) PHM_Cz_Magic;

static struct smu8_power_state *cast_smu8_power_state(struct pp_hw_power_state *hw_ps)
{
	if (smu8_magic != hw_ps->magic)
		return NULL;

	return (struct smu8_power_state *)hw_ps;
}

static const struct smu8_power_state *cast_const_smu8_power_state(
				const struct pp_hw_power_state *hw_ps)
{
	if (smu8_magic != hw_ps->magic)
		return NULL;

	return (struct smu8_power_state *)hw_ps;
}

static uint32_t smu8_get_eclk_level(struct pp_hwmgr *hwmgr,
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

static uint32_t smu8_get_sclk_level(struct pp_hwmgr *hwmgr,
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

static uint32_t smu8_get_uvd_level(struct pp_hwmgr *hwmgr,
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

static uint32_t smu8_get_max_sclk_level(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	if (data->max_sclk_level == 0) {
		smum_send_msg_to_smc(hwmgr,
				PPSMC_MSG_GetMaxSclkLevel,
				&data->max_sclk_level);
		data->max_sclk_level += 1;
	}

	return data->max_sclk_level;
}

static int smu8_initialize_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct amdgpu_device *adev = hwmgr->adev;

	data->gfx_ramp_step = 256*25/100;
	data->gfx_ramp_delay = 1; /* by default, we delay 1us */

	data->mgcg_cgtt_local0 = 0x00000000;
	data->mgcg_cgtt_local1 = 0x00000000;
	data->clock_slow_down_freq = 25000;
	data->skip_clock_slow_down = 1;
	data->enable_nb_ps_policy = 1; /* disable until UNB is ready, Enabled */
	data->voltage_drop_in_dce_power_gating = 0; /* disable until fully verified */
	data->voting_rights_clients = 0x00C00033;
	data->static_screen_threshold = 8;
	data->ddi_power_gating_disabled = 0;
	data->bapm_enabled = 1;
	data->voltage_drop_threshold = 0;
	data->gfx_power_gating_threshold = 500;
	data->vce_slow_sclk_threshold = 20000;
	data->dce_slow_sclk_threshold = 30000;
	data->disable_driver_thermal_policy = 1;
	data->disable_nb_ps3_in_battery = 0;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_ABM);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_NonABMSupportInPPLib);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicM3Arbiter);

	data->override_dynamic_mgpg = 1;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				  PHM_PlatformCaps_DynamicPatchPowerState);

	data->thermal_auto_throttling_treshold = 0;
	data->tdr_clock = 0;
	data->disable_gfx_power_gating_in_uvd = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicUVDState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UVDDPM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_VCEDPM);

	data->cc6_settings.cpu_cc6_disable = false;
	data->cc6_settings.cpu_pstate_disable = false;
	data->cc6_settings.nb_pstate_switch_disable = false;
	data->cc6_settings.cpu_pstate_separation_time = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				   PHM_PlatformCaps_DisableVoltageIsland);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_UVDPowerGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_VCEPowerGating);

	if (adev->pg_flags & AMD_PG_SUPPORT_UVD)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			      PHM_PlatformCaps_UVDPowerGating);
	if (adev->pg_flags & AMD_PG_SUPPORT_VCE)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			      PHM_PlatformCaps_VCEPowerGating);


	return 0;
}

/* convert form 8bit vid to real voltage in mV*4 */
static uint32_t smu8_convert_8Bit_index_to_voltage(
			struct pp_hwmgr *hwmgr, uint16_t voltage)
{
	return 6200 - (voltage * 25);
}

static int smu8_construct_max_power_limits_table(struct pp_hwmgr *hwmgr,
			struct phm_clock_and_voltage_limits *table)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct smu8_sys_info *sys_info = &data->sys_info;
	struct phm_clock_voltage_dependency_table *dep_table =
				hwmgr->dyn_state.vddc_dependency_on_sclk;

	if (dep_table->count > 0) {
		table->sclk = dep_table->entries[dep_table->count-1].clk;
		table->vddc = smu8_convert_8Bit_index_to_voltage(hwmgr,
		   (uint16_t)dep_table->entries[dep_table->count-1].v);
	}
	table->mclk = sys_info->nbp_memory_clock[0];
	return 0;
}

static int smu8_init_dynamic_state_adjustment_rule_settings(
			struct pp_hwmgr *hwmgr,
			ATOM_CLK_VOLT_CAPABILITY *disp_voltage_table)
{
	struct phm_clock_voltage_dependency_table *table_clk_vlt;

	table_clk_vlt = kzalloc(struct_size(table_clk_vlt, entries, 8),
				GFP_KERNEL);

	if (NULL == table_clk_vlt) {
		pr_err("Can not allocate memory!\n");
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

static int smu8_get_system_info_data(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	ATOM_INTEGRATED_SYSTEM_INFO_V1_9 *info = NULL;
	uint32_t i;
	int result = 0;
	uint8_t frev, crev;
	uint16_t size;

	info = (ATOM_INTEGRATED_SYSTEM_INFO_V1_9 *)smu_atom_get_data_table(hwmgr->adev,
			GetIndexIntoMasterTable(DATA, IntegratedSystemInfo),
			&size, &frev, &crev);

	if (info == NULL) {
		pr_err("Could not retrieve the Integrated System Info Table!\n");
		return -EINVAL;
	}

	if (crev != 9) {
		pr_err("Unsupported IGP table: %d %d\n", frev, crev);
		return -EINVAL;
	}

	data->sys_info.bootup_uma_clock =
				   le32_to_cpu(info->ulBootUpUMAClock);

	data->sys_info.bootup_engine_clock =
				le32_to_cpu(info->ulBootUpEngineClock);

	data->sys_info.dentist_vco_freq =
				   le32_to_cpu(info->ulDentistVCOFreq);

	data->sys_info.system_config =
				     le32_to_cpu(info->ulSystemConfig);

	data->sys_info.bootup_nb_voltage_index =
				  le16_to_cpu(info->usBootUpNBVoltage);

	data->sys_info.htc_hyst_lmt =
			(info->ucHtcHystLmt == 0) ? 5 : info->ucHtcHystLmt;

	data->sys_info.htc_tmp_lmt =
			(info->ucHtcTmpLmt == 0) ? 203 : info->ucHtcTmpLmt;

	if (data->sys_info.htc_tmp_lmt <=
			data->sys_info.htc_hyst_lmt) {
		pr_err("The htcTmpLmt should be larger than htcHystLmt.\n");
		return -EINVAL;
	}

	data->sys_info.nb_dpm_enable =
				data->enable_nb_ps_policy &&
				(le32_to_cpu(info->ulSystemConfig) >> 3 & 0x1);

	for (i = 0; i < SMU8_NUM_NBPSTATES; i++) {
		if (i < SMU8_NUM_NBPMEMORYCLOCK) {
			data->sys_info.nbp_memory_clock[i] =
			  le32_to_cpu(info->ulNbpStateMemclkFreq[i]);
		}
		data->sys_info.nbp_n_clock[i] =
			    le32_to_cpu(info->ulNbpStateNClkFreq[i]);
	}

	for (i = 0; i < MAX_DISPLAY_CLOCK_LEVEL; i++) {
		data->sys_info.display_clock[i] =
					le32_to_cpu(info->sDispClkVoltageMapping[i].ulMaximumSupportedCLK);
	}

	/* Here use 4 levels, make sure not exceed */
	for (i = 0; i < SMU8_NUM_NBPSTATES; i++) {
		data->sys_info.nbp_voltage_index[i] =
			     le16_to_cpu(info->usNBPStateVoltage[i]);
	}

	if (!data->sys_info.nb_dpm_enable) {
		for (i = 1; i < SMU8_NUM_NBPSTATES; i++) {
			if (i < SMU8_NUM_NBPMEMORYCLOCK) {
				data->sys_info.nbp_memory_clock[i] =
				    data->sys_info.nbp_memory_clock[0];
			}
			data->sys_info.nbp_n_clock[i] =
				    data->sys_info.nbp_n_clock[0];
			data->sys_info.nbp_voltage_index[i] =
				    data->sys_info.nbp_voltage_index[0];
		}
	}

	if (le32_to_cpu(info->ulGPUCapInfo) &
		SYS_INFO_GPUCAPS__ENABEL_DFS_BYPASS) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_EnableDFSBypass);
	}

	data->sys_info.uma_channel_number = info->ucUMAChannelNumber;

	smu8_construct_max_power_limits_table (hwmgr,
				    &hwmgr->dyn_state.max_clock_voltage_on_ac);

	smu8_init_dynamic_state_adjustment_rule_settings(hwmgr,
				    &info->sDISPCLK_Voltage[0]);

	return result;
}

static int smu8_construct_boot_state(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	data->boot_power_level.engineClock =
				data->sys_info.bootup_engine_clock;

	data->boot_power_level.vddcIndex =
			(uint8_t)data->sys_info.bootup_nb_voltage_index;

	data->boot_power_level.dsDividerIndex = 0;
	data->boot_power_level.ssDividerIndex = 0;
	data->boot_power_level.allowGnbSlow = 1;
	data->boot_power_level.forceNBPstate = 0;
	data->boot_power_level.hysteresis_up = 0;
	data->boot_power_level.numSIMDToPowerDown = 0;
	data->boot_power_level.display_wm = 0;
	data->boot_power_level.vce_wm = 0;

	return 0;
}

static int smu8_upload_pptable_to_smu(struct pp_hwmgr *hwmgr)
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

	ret = smum_download_powerplay_table(hwmgr, &table);

	PP_ASSERT_WITH_CODE((0 == ret && NULL != table),
			    "Fail to get clock table from SMU!", return -EINVAL;);

	clock_table = (struct SMU8_Fusion_ClkTable *)table;

	/* patch clock table */
	PP_ASSERT_WITH_CODE((vddc_table->count <= SMU8_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((vdd_gfx_table->count <= SMU8_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((acp_table->count <= SMU8_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((uvd_table->count <= SMU8_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);
	PP_ASSERT_WITH_CODE((vce_table->count <= SMU8_MAX_HARDWARE_POWERLEVELS),
			    "Dependency table entry exceeds max limit!", return -EINVAL;);

	for (i = 0; i < SMU8_MAX_HARDWARE_POWERLEVELS; i++) {

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
	ret = smum_upload_powerplay_table(hwmgr);

	return ret;
}

static int smu8_init_sclk_limit(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_clock_voltage_dependency_table *table =
					hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long clock = 0, level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	data->sclk_dpm.soft_min_clk = table->entries[0].clk;
	data->sclk_dpm.hard_min_clk = table->entries[0].clk;

	level = smu8_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		clock = table->entries[level].clk;
	else
		clock = table->entries[table->count - 1].clk;

	data->sclk_dpm.soft_max_clk = clock;
	data->sclk_dpm.hard_max_clk = clock;

	return 0;
}

static int smu8_init_uvd_limit(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_uvd_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.uvd_clock_voltage_dependency_table;
	unsigned long clock = 0;
	uint32_t level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	data->uvd_dpm.soft_min_clk = 0;
	data->uvd_dpm.hard_min_clk = 0;

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxUvdLevel, &level);

	if (level < table->count)
		clock = table->entries[level].vclk;
	else
		clock = table->entries[table->count - 1].vclk;

	data->uvd_dpm.soft_max_clk = clock;
	data->uvd_dpm.hard_max_clk = clock;

	return 0;
}

static int smu8_init_vce_limit(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_vce_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.vce_clock_voltage_dependency_table;
	unsigned long clock = 0;
	uint32_t level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	data->vce_dpm.soft_min_clk = 0;
	data->vce_dpm.hard_min_clk = 0;

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxEclkLevel, &level);

	if (level < table->count)
		clock = table->entries[level].ecclk;
	else
		clock = table->entries[table->count - 1].ecclk;

	data->vce_dpm.soft_max_clk = clock;
	data->vce_dpm.hard_max_clk = clock;

	return 0;
}

static int smu8_init_acp_limit(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_acp_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.acp_clock_voltage_dependency_table;
	unsigned long clock = 0;
	uint32_t level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	data->acp_dpm.soft_min_clk = 0;
	data->acp_dpm.hard_min_clk = 0;

	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetMaxAclkLevel, &level);

	if (level < table->count)
		clock = table->entries[level].acpclk;
	else
		clock = table->entries[table->count - 1].acpclk;

	data->acp_dpm.soft_max_clk = clock;
	data->acp_dpm.hard_max_clk = clock;
	return 0;
}

static void smu8_init_power_gate_state(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	data->uvd_power_gated = false;
	data->vce_power_gated = false;
	data->samu_power_gated = false;
#ifdef CONFIG_DRM_AMD_ACP
	data->acp_power_gated = false;
#else
	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ACPPowerOFF, NULL);
	data->acp_power_gated = true;
#endif

}

static void smu8_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	data->low_sclk_interrupt_threshold = 0;
}

static int smu8_update_sclk_limit(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_clock_voltage_dependency_table *table =
					hwmgr->dyn_state.vddc_dependency_on_sclk;

	unsigned long clock = 0;
	unsigned long level;
	unsigned long stable_pstate_sclk;
	unsigned long percentage;

	data->sclk_dpm.soft_min_clk = table->entries[0].clk;
	level = smu8_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		data->sclk_dpm.soft_max_clk  = table->entries[level].clk;
	else
		data->sclk_dpm.soft_max_clk  = table->entries[table->count - 1].clk;

	clock = hwmgr->display_config->min_core_set_clock;
	if (clock == 0)
		pr_debug("min_core_set_clock not set\n");

	if (data->sclk_dpm.hard_min_clk != clock) {
		data->sclk_dpm.hard_min_clk = clock;

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSclkHardMin,
						 smu8_get_sclk_level(hwmgr,
					data->sclk_dpm.hard_min_clk,
					     PPSMC_MSG_SetSclkHardMin),
						 NULL);
	}

	clock = data->sclk_dpm.soft_min_clk;

	/* update minimum clocks for Stable P-State feature */
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				     PHM_PlatformCaps_StablePState)) {
		percentage = 75;
		/*Sclk - calculate sclk value based on percentage and find FLOOR sclk from VddcDependencyOnSCLK table  */
		stable_pstate_sclk = (hwmgr->dyn_state.max_clock_voltage_on_ac.mclk *
					percentage) / 100;

		if (clock < stable_pstate_sclk)
			clock = stable_pstate_sclk;
	}

	if (data->sclk_dpm.soft_min_clk != clock) {
		data->sclk_dpm.soft_min_clk = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSclkSoftMin,
						smu8_get_sclk_level(hwmgr,
					data->sclk_dpm.soft_min_clk,
					     PPSMC_MSG_SetSclkSoftMin),
						NULL);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_StablePState) &&
			 data->sclk_dpm.soft_max_clk != clock) {
		data->sclk_dpm.soft_max_clk = clock;
		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetSclkSoftMax,
						smu8_get_sclk_level(hwmgr,
					data->sclk_dpm.soft_max_clk,
					PPSMC_MSG_SetSclkSoftMax),
						NULL);
	}

	return 0;
}

static int smu8_set_deep_sleep_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SclkDeepSleep)) {
		uint32_t clks = hwmgr->display_config->min_core_set_clock_in_sr;
		if (clks == 0)
			clks = SMU8_MIN_DEEP_SLEEP_SCLK;

		PP_DBG_LOG("Setting Deep Sleep Clock: %d\n", clks);

		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetMinDeepSleepSclk,
				clks,
				NULL);
	}

	return 0;
}

static int smu8_set_watermark_threshold(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data =
				  hwmgr->backend;

	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetWatermarkFrequency,
					data->sclk_dpm.soft_max_clk,
					NULL);

	return 0;
}

static int smu8_nbdpm_pstate_enable_disable(struct pp_hwmgr *hwmgr, bool enable, bool lock)
{
	struct smu8_hwmgr *hw_data = hwmgr->backend;

	if (hw_data->is_nb_dpm_enabled) {
		if (enable) {
			PP_DBG_LOG("enable Low Memory PState.\n");

			return smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_EnableLowMemoryPstate,
						(lock ? 1 : 0),
						NULL);
		} else {
			PP_DBG_LOG("disable Low Memory PState.\n");

			return smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_DisableLowMemoryPstate,
						(lock ? 1 : 0),
						NULL);
		}
	}

	return 0;
}

static int smu8_disable_nb_dpm(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	struct smu8_hwmgr *data = hwmgr->backend;
	unsigned long dpm_features = 0;

	if (data->is_nb_dpm_enabled) {
		smu8_nbdpm_pstate_enable_disable(hwmgr, true, true);
		dpm_features |= NB_DPM_MASK;
		ret = smum_send_msg_to_smc_with_parameter(
							  hwmgr,
							  PPSMC_MSG_DisableAllSmuFeatures,
							  dpm_features,
							  NULL);
		if (ret == 0)
			data->is_nb_dpm_enabled = false;
	}

	return ret;
}

static int smu8_enable_nb_dpm(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	struct smu8_hwmgr *data = hwmgr->backend;
	unsigned long dpm_features = 0;

	if (!data->is_nb_dpm_enabled) {
		PP_DBG_LOG("enabling ALL SMU features.\n");
		dpm_features |= NB_DPM_MASK;
		ret = smum_send_msg_to_smc_with_parameter(
							  hwmgr,
							  PPSMC_MSG_EnableAllSmuFeatures,
							  dpm_features,
							  NULL);
		if (ret == 0)
			data->is_nb_dpm_enabled = true;
	}

	return ret;
}

static int smu8_update_low_mem_pstate(struct pp_hwmgr *hwmgr, const void *input)
{
	bool disable_switch;
	bool enable_low_mem_state;
	struct smu8_hwmgr *hw_data = hwmgr->backend;
	const struct phm_set_power_state_input *states = (struct phm_set_power_state_input *)input;
	const struct smu8_power_state *pnew_state = cast_const_smu8_power_state(states->pnew_state);

	if (hw_data->sys_info.nb_dpm_enable) {
		disable_switch = hw_data->cc6_settings.nb_pstate_switch_disable ? true : false;
		enable_low_mem_state = hw_data->cc6_settings.nb_pstate_switch_disable ? false : true;

		if (pnew_state->action == FORCE_HIGH)
			smu8_nbdpm_pstate_enable_disable(hwmgr, false, disable_switch);
		else if (pnew_state->action == CANCEL_FORCE_HIGH)
			smu8_nbdpm_pstate_enable_disable(hwmgr, true, disable_switch);
		else
			smu8_nbdpm_pstate_enable_disable(hwmgr, enable_low_mem_state, disable_switch);
	}
	return 0;
}

static int smu8_set_power_state_tasks(struct pp_hwmgr *hwmgr, const void *input)
{
	int ret = 0;

	smu8_update_sclk_limit(hwmgr);
	smu8_set_deep_sleep_sclk_threshold(hwmgr);
	smu8_set_watermark_threshold(hwmgr);
	ret = smu8_enable_nb_dpm(hwmgr);
	if (ret)
		return ret;
	smu8_update_low_mem_pstate(hwmgr, input);

	return 0;
}


static int smu8_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	int ret;

	ret = smu8_upload_pptable_to_smu(hwmgr);
	if (ret)
		return ret;
	ret = smu8_init_sclk_limit(hwmgr);
	if (ret)
		return ret;
	ret = smu8_init_uvd_limit(hwmgr);
	if (ret)
		return ret;
	ret = smu8_init_vce_limit(hwmgr);
	if (ret)
		return ret;
	ret = smu8_init_acp_limit(hwmgr);
	if (ret)
		return ret;

	smu8_init_power_gate_state(hwmgr);
	smu8_init_sclk_threshold(hwmgr);

	return 0;
}

static void smu8_power_up_display_clock_sys_pll(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *hw_data = hwmgr->backend;

	hw_data->disp_clk_bypass_pending = false;
	hw_data->disp_clk_bypass = false;
}

static void smu8_clear_nb_dpm_flag(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *hw_data = hwmgr->backend;

	hw_data->is_nb_dpm_enabled = false;
}

static void smu8_reset_cc6_data(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *hw_data = hwmgr->backend;

	hw_data->cc6_settings.cc6_setting_changed = false;
	hw_data->cc6_settings.cpu_pstate_separation_time = 0;
	hw_data->cc6_settings.cpu_cc6_disable = false;
	hw_data->cc6_settings.cpu_pstate_disable = false;
}

static void smu8_program_voting_clients(struct pp_hwmgr *hwmgr)
{
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_FREQ_TRAN_VOTING_0,
				SMU8_VOTINGRIGHTSCLIENTS_DFLT0);
}

static void smu8_clear_voting_clients(struct pp_hwmgr *hwmgr)
{
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_FREQ_TRAN_VOTING_0, 0);
}

static int smu8_start_dpm(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	data->dpm_flags |= DPMFlags_SCLK_Enabled;

	return smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_EnableAllSmuFeatures,
				SCLK_DPM_MASK,
				NULL);
}

static int smu8_stop_dpm(struct pp_hwmgr *hwmgr)
{
	int ret = 0;
	struct smu8_hwmgr *data = hwmgr->backend;
	unsigned long dpm_features = 0;

	if (data->dpm_flags & DPMFlags_SCLK_Enabled) {
		dpm_features |= SCLK_DPM_MASK;
		data->dpm_flags &= ~DPMFlags_SCLK_Enabled;
		ret = smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DisableAllSmuFeatures,
					dpm_features,
					NULL);
	}
	return ret;
}

static int smu8_program_bootup_state(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	data->sclk_dpm.soft_min_clk = data->sys_info.bootup_engine_clock;
	data->sclk_dpm.soft_max_clk = data->sys_info.bootup_engine_clock;

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMin,
				smu8_get_sclk_level(hwmgr,
				data->sclk_dpm.soft_min_clk,
				PPSMC_MSG_SetSclkSoftMin),
				NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMax,
				smu8_get_sclk_level(hwmgr,
				data->sclk_dpm.soft_max_clk,
				PPSMC_MSG_SetSclkSoftMax),
				NULL);

	return 0;
}

static void smu8_reset_acp_boot_level(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	data->acp_boot_level = 0xff;
}

static int smu8_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	smu8_program_voting_clients(hwmgr);
	if (smu8_start_dpm(hwmgr))
		return -EINVAL;
	smu8_program_bootup_state(hwmgr);
	smu8_reset_acp_boot_level(hwmgr);

	return 0;
}

static int smu8_disable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	smu8_disable_nb_dpm(hwmgr);

	smu8_clear_voting_clients(hwmgr);
	if (smu8_stop_dpm(hwmgr))
		return -EINVAL;

	return 0;
}

static int smu8_power_off_asic(struct pp_hwmgr *hwmgr)
{
	smu8_disable_dpm_tasks(hwmgr);
	smu8_power_up_display_clock_sys_pll(hwmgr);
	smu8_clear_nb_dpm_flag(hwmgr);
	smu8_reset_cc6_data(hwmgr);
	return 0;
}

static int smu8_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps)
{
	struct smu8_power_state *smu8_ps =
				cast_smu8_power_state(&prequest_ps->hardware);

	const struct smu8_power_state *smu8_current_ps =
				cast_const_smu8_power_state(&pcurrent_ps->hardware);

	struct smu8_hwmgr *data = hwmgr->backend;
	struct PP_Clocks clocks = {0, 0, 0, 0};
	bool force_high;

	smu8_ps->need_dfs_bypass = true;

	data->battery_state = (PP_StateUILabel_Battery == prequest_ps->classification.ui_label);

	clocks.memoryClock = hwmgr->display_config->min_mem_set_clock != 0 ?
				hwmgr->display_config->min_mem_set_clock :
				data->sys_info.nbp_memory_clock[1];


	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState))
		clocks.memoryClock = hwmgr->dyn_state.max_clock_voltage_on_ac.mclk;

	force_high = (clocks.memoryClock > data->sys_info.nbp_memory_clock[SMU8_NUM_NBPMEMORYCLOCK - 1])
			|| (hwmgr->display_config->num_display >= 3);

	smu8_ps->action = smu8_current_ps->action;

	if (hwmgr->request_dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK)
		smu8_nbdpm_pstate_enable_disable(hwmgr, false, false);
	else if (hwmgr->request_dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD)
		smu8_nbdpm_pstate_enable_disable(hwmgr, false, true);
	else if (!force_high && (smu8_ps->action == FORCE_HIGH))
		smu8_ps->action = CANCEL_FORCE_HIGH;
	else if (force_high && (smu8_ps->action != FORCE_HIGH))
		smu8_ps->action = FORCE_HIGH;
	else
		smu8_ps->action = DO_NOTHING;

	return 0;
}

static int smu8_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct smu8_hwmgr *data;

	data = kzalloc(sizeof(struct smu8_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	result = smu8_initialize_dpm_defaults(hwmgr);
	if (result != 0) {
		pr_err("smu8_initialize_dpm_defaults failed\n");
		return result;
	}

	result = smu8_get_system_info_data(hwmgr);
	if (result != 0) {
		pr_err("smu8_get_system_info_data failed\n");
		return result;
	}

	smu8_construct_boot_state(hwmgr);

	hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =  SMU8_MAX_HARDWARE_POWERLEVELS;

	return result;
}

static int smu8_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	if (hwmgr != NULL) {
		kfree(hwmgr->dyn_state.vddc_dep_on_dal_pwrl);
		hwmgr->dyn_state.vddc_dep_on_dal_pwrl = NULL;

		kfree(hwmgr->backend);
		hwmgr->backend = NULL;
	}
	return 0;
}

static int smu8_phm_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetSclkSoftMin,
					smu8_get_sclk_level(hwmgr,
					data->sclk_dpm.soft_max_clk,
					PPSMC_MSG_SetSclkSoftMin),
					NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMax,
				smu8_get_sclk_level(hwmgr,
				data->sclk_dpm.soft_max_clk,
				PPSMC_MSG_SetSclkSoftMax),
				NULL);

	return 0;
}

static int smu8_phm_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long clock = 0, level;

	if (NULL == table || table->count <= 0)
		return -EINVAL;

	data->sclk_dpm.soft_min_clk = table->entries[0].clk;
	data->sclk_dpm.hard_min_clk = table->entries[0].clk;
	hwmgr->pstate_sclk = table->entries[0].clk;
	hwmgr->pstate_mclk = 0;

	level = smu8_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		clock = table->entries[level].clk;
	else
		clock = table->entries[table->count - 1].clk;

	data->sclk_dpm.soft_max_clk = clock;
	data->sclk_dpm.hard_max_clk = clock;

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMin,
				smu8_get_sclk_level(hwmgr,
				data->sclk_dpm.soft_min_clk,
				PPSMC_MSG_SetSclkSoftMin),
				NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMax,
				smu8_get_sclk_level(hwmgr,
				data->sclk_dpm.soft_max_clk,
				PPSMC_MSG_SetSclkSoftMax),
				NULL);

	return 0;
}

static int smu8_phm_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetSclkSoftMax,
			smu8_get_sclk_level(hwmgr,
			data->sclk_dpm.soft_min_clk,
			PPSMC_MSG_SetSclkSoftMax),
			NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMin,
				smu8_get_sclk_level(hwmgr,
				data->sclk_dpm.soft_min_clk,
				PPSMC_MSG_SetSclkSoftMin),
				NULL);

	return 0;
}

static int smu8_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = smu8_phm_force_dpm_highest(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		ret = smu8_phm_force_dpm_lowest(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = smu8_phm_unforce_dpm_levels(hwmgr);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}

	return ret;
}

static int smu8_dpm_powerdown_uvd(struct pp_hwmgr *hwmgr)
{
	if (PP_CAP(PHM_PlatformCaps_UVDPowerGating))
		return smum_send_msg_to_smc(hwmgr, PPSMC_MSG_UVDPowerOFF, NULL);
	return 0;
}

static int smu8_dpm_powerup_uvd(struct pp_hwmgr *hwmgr)
{
	if (PP_CAP(PHM_PlatformCaps_UVDPowerGating)) {
		return smum_send_msg_to_smc_with_parameter(
			hwmgr,
			PPSMC_MSG_UVDPowerON,
			PP_CAP(PHM_PlatformCaps_UVDDynamicPowerGating) ? 1 : 0,
			NULL);
	}

	return 0;
}

static int  smu8_dpm_update_vce_dpm(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_vce_clock_voltage_dependency_table *ptable =
		hwmgr->dyn_state.vce_clock_voltage_dependency_table;

	/* Stable Pstate is enabled and we need to set the VCE DPM to highest level */
	if (PP_CAP(PHM_PlatformCaps_StablePState) ||
	    hwmgr->en_umd_pstate) {
		data->vce_dpm.hard_min_clk =
				  ptable->entries[ptable->count - 1].ecclk;

		smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_SetEclkHardMin,
			smu8_get_eclk_level(hwmgr,
				data->vce_dpm.hard_min_clk,
				PPSMC_MSG_SetEclkHardMin),
			NULL);
	} else {

		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetEclkHardMin,
					0,
					NULL);
		/* disable ECLK DPM 0. Otherwise VCE could hang if
		 * switching SCLK from DPM 0 to 6/7 */
		smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_SetEclkSoftMin,
					1,
					NULL);
	}
	return 0;
}

static int smu8_dpm_powerdown_vce(struct pp_hwmgr *hwmgr)
{
	if (PP_CAP(PHM_PlatformCaps_VCEPowerGating))
		return smum_send_msg_to_smc(hwmgr,
					    PPSMC_MSG_VCEPowerOFF,
					    NULL);
	return 0;
}

static int smu8_dpm_powerup_vce(struct pp_hwmgr *hwmgr)
{
	if (PP_CAP(PHM_PlatformCaps_VCEPowerGating))
		return smum_send_msg_to_smc(hwmgr,
					    PPSMC_MSG_VCEPowerON,
					    NULL);
	return 0;
}

static uint32_t smu8_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	return data->sys_info.bootup_uma_clock;
}

static uint32_t smu8_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct smu8_power_state  *smu8_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	smu8_ps = cast_smu8_power_state(&ps->hardware);

	if (low)
		return smu8_ps->levels[0].engineClock;
	else
		return smu8_ps->levels[smu8_ps->level-1].engineClock;
}

static int smu8_dpm_patch_boot_state(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *hw_ps)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct smu8_power_state *smu8_ps = cast_smu8_power_state(hw_ps);

	smu8_ps->level = 1;
	smu8_ps->nbps_flags = 0;
	smu8_ps->bapm_flags = 0;
	smu8_ps->levels[0] = data->boot_power_level;

	return 0;
}

static int smu8_dpm_get_pp_table_entry_callback(
						     struct pp_hwmgr *hwmgr,
					   struct pp_hw_power_state *hw_ps,
							  unsigned int index,
						     const void *clock_info)
{
	struct smu8_power_state *smu8_ps = cast_smu8_power_state(hw_ps);

	const ATOM_PPLIB_CZ_CLOCK_INFO *smu8_clock_info = clock_info;

	struct phm_clock_voltage_dependency_table *table =
				    hwmgr->dyn_state.vddc_dependency_on_sclk;
	uint8_t clock_info_index = smu8_clock_info->index;

	if (clock_info_index > (uint8_t)(hwmgr->platform_descriptor.hardwareActivityPerformanceLevels - 1))
		clock_info_index = (uint8_t)(hwmgr->platform_descriptor.hardwareActivityPerformanceLevels - 1);

	smu8_ps->levels[index].engineClock = table->entries[clock_info_index].clk;
	smu8_ps->levels[index].vddcIndex = (uint8_t)table->entries[clock_info_index].v;

	smu8_ps->level = index + 1;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		smu8_ps->levels[index].dsDividerIndex = 5;
		smu8_ps->levels[index].ssDividerIndex = 5;
	}

	return 0;
}

static int smu8_dpm_get_num_of_pp_table_entries(struct pp_hwmgr *hwmgr)
{
	int result;
	unsigned long ret = 0;

	result = pp_tables_get_num_of_entries(hwmgr, &ret);

	return result ? 0 : ret;
}

static int smu8_dpm_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		    unsigned long entry, struct pp_power_state *ps)
{
	int result;
	struct smu8_power_state *smu8_ps;

	ps->hardware.magic = smu8_magic;

	smu8_ps = cast_smu8_power_state(&(ps->hardware));

	result = pp_tables_get_entry(hwmgr, entry, ps,
			smu8_dpm_get_pp_table_entry_callback);

	smu8_ps->uvd_clocks.vclk = ps->uvd_clocks.VCLK;
	smu8_ps->uvd_clocks.dclk = ps->uvd_clocks.DCLK;

	return result;
}

static int smu8_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct smu8_power_state);
}

static void smu8_hw_print_display_cfg(
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

 static int smu8_set_cpu_power_state(struct pp_hwmgr *hwmgr)
{
	struct smu8_hwmgr *hw_data = hwmgr->backend;
	uint32_t data = 0;

	if (hw_data->cc6_settings.cc6_setting_changed) {

		hw_data->cc6_settings.cc6_setting_changed = false;

		smu8_hw_print_display_cfg(&hw_data->cc6_settings);

		data |= (hw_data->cc6_settings.cpu_pstate_separation_time
			& PWRMGT_SEPARATION_TIME_MASK)
			<< PWRMGT_SEPARATION_TIME_SHIFT;

		data |= (hw_data->cc6_settings.cpu_cc6_disable ? 0x1 : 0x0)
			<< PWRMGT_DISABLE_CPU_CSTATES_SHIFT;

		data |= (hw_data->cc6_settings.cpu_pstate_disable ? 0x1 : 0x0)
			<< PWRMGT_DISABLE_CPU_PSTATES_SHIFT;

		PP_DBG_LOG("SetDisplaySizePowerParams data: 0x%X\n",
			data);

		smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SetDisplaySizePowerParams,
						data,
						NULL);
	}

	return 0;
}


static int smu8_store_cc6_data(struct pp_hwmgr *hwmgr, uint32_t separation_time,
			bool cc6_disable, bool pstate_disable, bool pstate_switch_disable)
{
	struct smu8_hwmgr *hw_data = hwmgr->backend;

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

static int smu8_get_dal_power_level(struct pp_hwmgr *hwmgr,
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

static int smu8_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	switch (type) {
	case PP_SCLK:
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMin,
				mask,
				NULL);
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetSclkSoftMax,
				mask,
				NULL);
		break;
	default:
		break;
	}

	return 0;
}

static int smu8_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_clock_voltage_dependency_table *sclk_table =
			hwmgr->dyn_state.vddc_dependency_on_sclk;
	uint32_t i, now;
	int size = 0;

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
	case PP_MCLK:
		now = PHM_GET_FIELD(cgs_read_ind_register(hwmgr->device,
				CGS_IND_REG__SMC,
				ixTARGET_AND_CURRENT_PROFILE_INDEX),
				TARGET_AND_CURRENT_PROFILE_INDEX,
				CURR_MCLK_INDEX);

		for (i = SMU8_NUM_NBPMEMORYCLOCK; i > 0; i--)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					SMU8_NUM_NBPMEMORYCLOCK-i, data->sys_info.nbp_memory_clock[i-1] / 100,
					(SMU8_NUM_NBPMEMORYCLOCK-i == now) ? "*" : "");
		break;
	default:
		break;
	}
	return size;
}

static int smu8_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level)
{
	const struct smu8_power_state *ps;
	struct smu8_hwmgr *data;
	uint32_t level_index;
	uint32_t i;

	if (level == NULL || hwmgr == NULL || state == NULL)
		return -EINVAL;

	data = hwmgr->backend;
	ps = cast_const_smu8_power_state(state);

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
		level->memory_clock = data->sys_info.nbp_memory_clock[SMU8_NUM_NBPMEMORYCLOCK - 1];
	else
		level->memory_clock = data->sys_info.nbp_memory_clock[0];

	level->vddc = (smu8_convert_8Bit_index_to_voltage(hwmgr, ps->levels[level_index].vddcIndex) + 2) / 4;
	level->nonLocalMemoryFreq = 0;
	level->nonLocalMemoryWidth = 0;

	return 0;
}

static int smu8_get_current_shallow_sleep_clocks(struct pp_hwmgr *hwmgr,
	const struct pp_hw_power_state *state, struct pp_clock_info *clock_info)
{
	const struct smu8_power_state *ps = cast_const_smu8_power_state(state);

	clock_info->min_eng_clk = ps->levels[0].engineClock / (1 << (ps->levels[0].ssDividerIndex));
	clock_info->max_eng_clk = ps->levels[ps->level - 1].engineClock / (1 << (ps->levels[ps->level - 1].ssDividerIndex));

	return 0;
}

static int smu8_get_clock_by_type(struct pp_hwmgr *hwmgr, enum amd_pp_clock_type type,
						struct amd_pp_clocks *clocks)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	int i;
	struct phm_clock_voltage_dependency_table *table;

	clocks->count = smu8_get_max_sclk_level(hwmgr);
	switch (type) {
	case amd_pp_disp_clock:
		for (i = 0; i < clocks->count; i++)
			clocks->clock[i] = data->sys_info.display_clock[i] * 10;
		break;
	case amd_pp_sys_clock:
		table = hwmgr->dyn_state.vddc_dependency_on_sclk;
		for (i = 0; i < clocks->count; i++)
			clocks->clock[i] = table->entries[i].clk * 10;
		break;
	case amd_pp_mem_clock:
		clocks->count = SMU8_NUM_NBPMEMORYCLOCK;
		for (i = 0; i < clocks->count; i++)
			clocks->clock[i] = data->sys_info.nbp_memory_clock[clocks->count - 1 - i] * 10;
		break;
	default:
		return -1;
	}

	return 0;
}

static int smu8_get_max_high_clocks(struct pp_hwmgr *hwmgr, struct amd_pp_simple_clock_info *clocks)
{
	struct phm_clock_voltage_dependency_table *table =
					hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long level;
	const struct phm_clock_and_voltage_limits *limits =
			&hwmgr->dyn_state.max_clock_voltage_on_ac;

	if ((NULL == table) || (table->count <= 0) || (clocks == NULL))
		return -EINVAL;

	level = smu8_get_max_sclk_level(hwmgr) - 1;

	if (level < table->count)
		clocks->engine_max_clock = table->entries[level].clk;
	else
		clocks->engine_max_clock = table->entries[table->count - 1].clk;

	clocks->memory_max_clock = limits->mclk;

	return 0;
}

static int smu8_thermal_get_temperature(struct pp_hwmgr *hwmgr)
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

static int smu8_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			  void *value, int *size)
{
	struct smu8_hwmgr *data = hwmgr->backend;

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

	/* size must be at least 4 bytes for all sensors */
	if (*size < 4)
		return -EINVAL;
	*size = 4;

	switch (idx) {
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		if (sclk_index < NUM_SCLK_LEVELS) {
			sclk = table->entries[sclk_index].clk;
			*((uint32_t *)value) = sclk;
			return 0;
		}
		return -EINVAL;
	case AMDGPU_PP_SENSOR_VDDNB:
		tmp = (cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixSMUSVI_NB_CURRENTVID) &
			CURRENT_NB_VID_MASK) >> CURRENT_NB_VID__SHIFT;
		vddnb = smu8_convert_8Bit_index_to_voltage(hwmgr, tmp) / 4;
		*((uint32_t *)value) = vddnb;
		return 0;
	case AMDGPU_PP_SENSOR_VDDGFX:
		tmp = (cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixSMUSVI_GFX_CURRENTVID) &
			CURRENT_GFX_VID_MASK) >> CURRENT_GFX_VID__SHIFT;
		vddgfx = smu8_convert_8Bit_index_to_voltage(hwmgr, (u16)tmp) / 4;
		*((uint32_t *)value) = vddgfx;
		return 0;
	case AMDGPU_PP_SENSOR_UVD_VCLK:
		if (!data->uvd_power_gated) {
			if (uvd_index >= SMU8_MAX_HARDWARE_POWERLEVELS) {
				return -EINVAL;
			} else {
				vclk = uvd_table->entries[uvd_index].vclk;
				*((uint32_t *)value) = vclk;
				return 0;
			}
		}
		*((uint32_t *)value) = 0;
		return 0;
	case AMDGPU_PP_SENSOR_UVD_DCLK:
		if (!data->uvd_power_gated) {
			if (uvd_index >= SMU8_MAX_HARDWARE_POWERLEVELS) {
				return -EINVAL;
			} else {
				dclk = uvd_table->entries[uvd_index].dclk;
				*((uint32_t *)value) = dclk;
				return 0;
			}
		}
		*((uint32_t *)value) = 0;
		return 0;
	case AMDGPU_PP_SENSOR_VCE_ECCLK:
		if (!data->vce_power_gated) {
			if (vce_index >= SMU8_MAX_HARDWARE_POWERLEVELS) {
				return -EINVAL;
			} else {
				ecclk = vce_table->entries[vce_index].ecclk;
				*((uint32_t *)value) = ecclk;
				return 0;
			}
		}
		*((uint32_t *)value) = 0;
		return 0;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		result = smum_send_msg_to_smc(hwmgr,
				PPSMC_MSG_GetAverageGraphicsActivity,
				&activity_percent);
		if (0 == result)
			activity_percent = activity_percent > 100 ? 100 : activity_percent;
		else
			return -EIO;
		*((uint32_t *)value) = activity_percent;
		return 0;
	case AMDGPU_PP_SENSOR_UVD_POWER:
		*((uint32_t *)value) = data->uvd_power_gated ? 0 : 1;
		return 0;
	case AMDGPU_PP_SENSOR_VCE_POWER:
		*((uint32_t *)value) = data->vce_power_gated ? 0 : 1;
		return 0;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = smu8_thermal_get_temperature(hwmgr);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int smu8_notify_cac_buffer_info(struct pp_hwmgr *hwmgr,
					uint32_t virtual_addr_low,
					uint32_t virtual_addr_hi,
					uint32_t mc_addr_low,
					uint32_t mc_addr_hi,
					uint32_t size)
{
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramAddrHiVirtual,
					mc_addr_hi,
					NULL);
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramAddrLoVirtual,
					mc_addr_low,
					NULL);
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramAddrHiPhysical,
					virtual_addr_hi,
					NULL);
	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramAddrLoPhysical,
					virtual_addr_low,
					NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DramBufferSize,
					size,
					NULL);
	return 0;
}

static int smu8_get_thermal_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *thermal_data)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	memcpy(thermal_data, &SMU7ThermalPolicy[0], sizeof(struct PP_TemperatureRange));

	thermal_data->max = (data->thermal_auto_throttling_treshold +
			data->sys_info.htc_hyst_lmt) *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int smu8_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	uint32_t dpm_features = 0;

	if (enable &&
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				  PHM_PlatformCaps_UVDDPM)) {
		data->dpm_flags |= DPMFlags_UVD_Enabled;
		dpm_features |= UVD_DPM_MASK;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			    PPSMC_MSG_EnableAllSmuFeatures,
			    dpm_features,
			    NULL);
	} else {
		dpm_features |= UVD_DPM_MASK;
		data->dpm_flags &= ~DPMFlags_UVD_Enabled;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			   PPSMC_MSG_DisableAllSmuFeatures,
			   dpm_features,
			   NULL);
	}
	return 0;
}

static int smu8_dpm_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	struct phm_uvd_clock_voltage_dependency_table *ptable =
		hwmgr->dyn_state.uvd_clock_voltage_dependency_table;

	if (!bgate) {
		/* Stable Pstate is enabled and we need to set the UVD DPM to highest level */
		if (PP_CAP(PHM_PlatformCaps_StablePState) ||
		    hwmgr->en_umd_pstate) {
			data->uvd_dpm.hard_min_clk =
				   ptable->entries[ptable->count - 1].vclk;

			smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetUvdHardMin,
				smu8_get_uvd_level(hwmgr,
					data->uvd_dpm.hard_min_clk,
					PPSMC_MSG_SetUvdHardMin),
				NULL);

			smu8_enable_disable_uvd_dpm(hwmgr, true);
		} else {
			smu8_enable_disable_uvd_dpm(hwmgr, true);
		}
	} else {
		smu8_enable_disable_uvd_dpm(hwmgr, false);
	}

	return 0;
}

static int smu8_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	struct smu8_hwmgr *data = hwmgr->backend;
	uint32_t dpm_features = 0;

	if (enable && phm_cap_enabled(
				hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_VCEDPM)) {
		data->dpm_flags |= DPMFlags_VCE_Enabled;
		dpm_features |= VCE_DPM_MASK;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			    PPSMC_MSG_EnableAllSmuFeatures,
			    dpm_features,
			    NULL);
	} else {
		dpm_features |= VCE_DPM_MASK;
		data->dpm_flags &= ~DPMFlags_VCE_Enabled;
		smum_send_msg_to_smc_with_parameter(hwmgr,
			   PPSMC_MSG_DisableAllSmuFeatures,
			   dpm_features,
			   NULL);
	}

	return 0;
}


static void smu8_dpm_powergate_acp(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	if (data->acp_power_gated == bgate)
		return;

	if (bgate)
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ACPPowerOFF, NULL);
	else
		smum_send_msg_to_smc(hwmgr, PPSMC_MSG_ACPPowerON, NULL);
}

static void smu8_dpm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	data->uvd_power_gated = bgate;

	if (bgate) {
		amdgpu_device_ip_set_powergating_state(hwmgr->adev,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_PG_STATE_GATE);
		amdgpu_device_ip_set_clockgating_state(hwmgr->adev,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_CG_STATE_GATE);
		smu8_dpm_update_uvd_dpm(hwmgr, true);
		smu8_dpm_powerdown_uvd(hwmgr);
	} else {
		smu8_dpm_powerup_uvd(hwmgr);
		amdgpu_device_ip_set_clockgating_state(hwmgr->adev,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_CG_STATE_UNGATE);
		amdgpu_device_ip_set_powergating_state(hwmgr->adev,
						AMD_IP_BLOCK_TYPE_UVD,
						AMD_PG_STATE_UNGATE);
		smu8_dpm_update_uvd_dpm(hwmgr, false);
	}

}

static void smu8_dpm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct smu8_hwmgr *data = hwmgr->backend;

	if (bgate) {
		amdgpu_device_ip_set_powergating_state(hwmgr->adev,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_PG_STATE_GATE);
		amdgpu_device_ip_set_clockgating_state(hwmgr->adev,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_CG_STATE_GATE);
		smu8_enable_disable_vce_dpm(hwmgr, false);
		smu8_dpm_powerdown_vce(hwmgr);
		data->vce_power_gated = true;
	} else {
		smu8_dpm_powerup_vce(hwmgr);
		data->vce_power_gated = false;
		amdgpu_device_ip_set_clockgating_state(hwmgr->adev,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_CG_STATE_UNGATE);
		amdgpu_device_ip_set_powergating_state(hwmgr->adev,
					AMD_IP_BLOCK_TYPE_VCE,
					AMD_PG_STATE_UNGATE);
		smu8_dpm_update_vce_dpm(hwmgr);
		smu8_enable_disable_vce_dpm(hwmgr, true);
	}
}

static const struct pp_hwmgr_func smu8_hwmgr_funcs = {
	.backend_init = smu8_hwmgr_backend_init,
	.backend_fini = smu8_hwmgr_backend_fini,
	.apply_state_adjust_rules = smu8_apply_state_adjust_rules,
	.force_dpm_level = smu8_dpm_force_dpm_level,
	.get_power_state_size = smu8_get_power_state_size,
	.powerdown_uvd = smu8_dpm_powerdown_uvd,
	.powergate_uvd = smu8_dpm_powergate_uvd,
	.powergate_vce = smu8_dpm_powergate_vce,
	.powergate_acp = smu8_dpm_powergate_acp,
	.get_mclk = smu8_dpm_get_mclk,
	.get_sclk = smu8_dpm_get_sclk,
	.patch_boot_state = smu8_dpm_patch_boot_state,
	.get_pp_table_entry = smu8_dpm_get_pp_table_entry,
	.get_num_of_pp_table_entries = smu8_dpm_get_num_of_pp_table_entries,
	.set_cpu_power_state = smu8_set_cpu_power_state,
	.store_cc6_data = smu8_store_cc6_data,
	.force_clock_level = smu8_force_clock_level,
	.print_clock_levels = smu8_print_clock_levels,
	.get_dal_power_level = smu8_get_dal_power_level,
	.get_performance_level = smu8_get_performance_level,
	.get_current_shallow_sleep_clocks = smu8_get_current_shallow_sleep_clocks,
	.get_clock_by_type = smu8_get_clock_by_type,
	.get_max_high_clocks = smu8_get_max_high_clocks,
	.read_sensor = smu8_read_sensor,
	.power_off_asic = smu8_power_off_asic,
	.asic_setup = smu8_setup_asic_task,
	.dynamic_state_management_enable = smu8_enable_dpm_tasks,
	.power_state_set = smu8_set_power_state_tasks,
	.dynamic_state_management_disable = smu8_disable_dpm_tasks,
	.notify_cac_buffer_info = smu8_notify_cac_buffer_info,
	.update_nbdpm_pstate = smu8_nbdpm_pstate_enable_disable,
	.get_thermal_temperature_range = smu8_get_thermal_temperature_range,
};

int smu8_init_function_pointers(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &smu8_hwmgr_funcs;
	hwmgr->pptable_func = &pptable_funcs;
	return 0;
}
