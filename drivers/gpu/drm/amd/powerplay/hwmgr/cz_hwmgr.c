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
#include "cgs_common.h"
#include "smu/smu_8_0_d.h"
#include "smumgr.h"
#include "hwmgr.h"
#include "hardwaremanager.h"
#include "cz_ppsmc.h"
#include "cz_hwmgr.h"
#include "power_state.h"

static const unsigned long PhwCz_Magic = (unsigned long) PHM_Cz_Magic;

static struct cz_power_state *cast_PhwCzPowerState(struct pp_hw_power_state *hw_ps)
{
	if (PhwCz_Magic != hw_ps->magic)
		return NULL;

	return (struct cz_power_state *)hw_ps;
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

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					   PHM_PlatformCaps_SclkDeepSleep);

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

	cz_hwmgr->is_nb_dpm_enabled_by_driver = 1;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				   PHM_PlatformCaps_DisableVoltageIsland);

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
	return 0;
}

static int cz_tf_init_sclk_limit(struct pp_hwmgr *hwmgr, void *input,
				 void *output, void *storage, int result)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_clock_voltage_dependency_table *table =
					hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long clock = 0, level;

	if (NULL == table && table->count <= 0)
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
				hwmgr->dyn_state.uvd_clocl_voltage_dependency_table;
	unsigned long clock = 0, level;

	if (NULL == table && table->count <= 0)
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
				hwmgr->dyn_state.vce_clocl_voltage_dependency_table;
	unsigned long clock = 0, level;

	if (NULL == table && table->count <= 0)
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

	if (NULL == table && table->count <= 0)
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

static struct phm_master_table_item cz_setup_asic_list[] = {
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

static struct phm_master_table_header cz_setup_asic_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_setup_asic_list
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

int cz_tf_reset_acp_boot_level(struct pp_hwmgr *hwmgr, void *input,
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

static struct phm_master_table_item cz_disable_dpm_list[] = {
	{ NULL, cz_tf_check_for_dpm_enabled},
	{NULL, NULL},
};


static struct phm_master_table_header cz_disable_dpm_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_disable_dpm_list
};

static struct phm_master_table_item cz_enable_dpm_list[] = {
	{ NULL, cz_tf_check_for_dpm_disabled },
	{ NULL, cz_tf_program_voting_clients },
	{ NULL, cz_tf_start_dpm},
	{ NULL, cz_tf_program_bootup_state},
	{ NULL, cz_tf_enable_didt },
	{ NULL, cz_tf_reset_acp_boot_level },
	{NULL, NULL},
};

static struct phm_master_table_header cz_enable_dpm_master = {
	0,
	PHM_MasterTableFlag_None,
	cz_enable_dpm_list
};

static int cz_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;

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

	result = phm_construct_table(hwmgr, &cz_disable_dpm_master,
				&(hwmgr->disable_dynamic_state_management));

	result = phm_construct_table(hwmgr, &cz_enable_dpm_master,
				&(hwmgr->enable_dynamic_state_management));

	return result;
}

static int cz_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	if (hwmgr != NULL || hwmgr->backend != NULL) {
		kfree(hwmgr->backend);
		kfree(hwmgr);
	}
	return 0;
}

int cz_phm_force_dpm_highest(struct pp_hwmgr *hwmgr)
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

int cz_phm_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr = (struct cz_hwmgr *)(hwmgr->backend);
	struct phm_clock_voltage_dependency_table *table =
				hwmgr->dyn_state.vddc_dependency_on_sclk;
	unsigned long clock = 0, level;

	if (NULL == table && table->count <= 0)
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

int cz_phm_force_dpm_lowest(struct pp_hwmgr *hwmgr)
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

int cz_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct cz_power_state);
}

static const struct pp_hwmgr_func cz_hwmgr_funcs = {
	.backend_init = cz_hwmgr_backend_init,
	.backend_fini = cz_hwmgr_backend_fini,
	.asic_setup = NULL,
	.force_dpm_level = cz_dpm_force_dpm_level,
	.get_power_state_size = cz_get_power_state_size,
	.patch_boot_state = cz_dpm_patch_boot_state,
	.get_pp_table_entry = cz_dpm_get_pp_table_entry,
	.get_num_of_pp_table_entries = cz_dpm_get_num_of_pp_table_entries,
};

int cz_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	struct cz_hwmgr *cz_hwmgr;
	int ret = 0;

	cz_hwmgr = kzalloc(sizeof(struct cz_hwmgr), GFP_KERNEL);
	if (cz_hwmgr == NULL)
		return -ENOMEM;

	hwmgr->backend = cz_hwmgr;
	hwmgr->hwmgr_func = &cz_hwmgr_funcs;
	hwmgr->pptable_func = &pptable_funcs;
	return ret;
}
