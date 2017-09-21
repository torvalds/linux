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
#include "smumgr.h"
#include "hwmgr.h"
#include "hardwaremanager.h"
#include "rv_ppsmc.h"
#include "rv_hwmgr.h"
#include "power_state.h"
#include "rv_smumgr.h"
#include "pp_soc15.h"

#define RAVEN_MAX_DEEPSLEEP_DIVIDER_ID     5
#define RAVEN_MINIMUM_ENGINE_CLOCK         800   //8Mhz, the low boundary of engine clock allowed on this chip
#define SCLK_MIN_DIV_INTV_SHIFT         12
#define RAVEN_DISPCLK_BYPASS_THRESHOLD     10000 //100mhz
#define SMC_RAM_END                     0x40000

static const unsigned long PhwRaven_Magic = (unsigned long) PHM_Rv_Magic;
int rv_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
		struct pp_display_clock_request *clock_req);

struct phm_vq_budgeting_record rv_vqtable[] = {
	/* _TBD
	 * CUs, SSP low, SSP High, Min Sclk Low, Min Sclk, High, AWD/non-AWD, DCLK, ECLK, Sustainable Sclk, Sustainable CUs */
	{ 8, 0, 45, 0, 0, VQ_DisplayConfig_NoneAWD, 80000, 120000, 4, 0 },
};

static struct rv_power_state *cast_rv_ps(struct pp_hw_power_state *hw_ps)
{
	if (PhwRaven_Magic != hw_ps->magic)
		return NULL;

	return (struct rv_power_state *)hw_ps;
}

static const struct rv_power_state *cast_const_rv_ps(
				const struct pp_hw_power_state *hw_ps)
{
	if (PhwRaven_Magic != hw_ps->magic)
		return NULL;

	return (struct rv_power_state *)hw_ps;
}

static int rv_init_vq_budget_table(struct pp_hwmgr *hwmgr)
{
	uint32_t table_size, i;
	struct phm_vq_budgeting_table *ptable;
	uint32_t num_entries = ARRAY_SIZE(rv_vqtable);

	if (hwmgr->dyn_state.vq_budgeting_table != NULL)
		return 0;

	table_size = sizeof(struct phm_vq_budgeting_table) +
			sizeof(struct phm_vq_budgeting_record) * (num_entries - 1);

	ptable = kzalloc(table_size, GFP_KERNEL);
	if (NULL == ptable)
		return -ENOMEM;

	ptable->numEntries = (uint8_t) num_entries;

	for (i = 0; i < ptable->numEntries; i++) {
		ptable->entries[i].ulCUs = rv_vqtable[i].ulCUs;
		ptable->entries[i].ulSustainableSOCPowerLimitLow = rv_vqtable[i].ulSustainableSOCPowerLimitLow;
		ptable->entries[i].ulSustainableSOCPowerLimitHigh = rv_vqtable[i].ulSustainableSOCPowerLimitHigh;
		ptable->entries[i].ulMinSclkLow = rv_vqtable[i].ulMinSclkLow;
		ptable->entries[i].ulMinSclkHigh = rv_vqtable[i].ulMinSclkHigh;
		ptable->entries[i].ucDispConfig = rv_vqtable[i].ucDispConfig;
		ptable->entries[i].ulDClk = rv_vqtable[i].ulDClk;
		ptable->entries[i].ulEClk = rv_vqtable[i].ulEClk;
		ptable->entries[i].ulSustainableSclk = rv_vqtable[i].ulSustainableSclk;
		ptable->entries[i].ulSustainableCUs = rv_vqtable[i].ulSustainableCUs;
	}

	hwmgr->dyn_state.vq_budgeting_table = ptable;

	return 0;
}

static int rv_initialize_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct rv_hwmgr *rv_hwmgr = (struct rv_hwmgr *)(hwmgr->backend);
	struct cgs_system_info sys_info = {0};
	int result;

	rv_hwmgr->ddi_power_gating_disabled = 0;
	rv_hwmgr->bapm_enabled = 1;
	rv_hwmgr->dce_slow_sclk_threshold = 30000;
	rv_hwmgr->disable_driver_thermal_policy = 1;
	rv_hwmgr->thermal_auto_throttling_treshold = 0;
	rv_hwmgr->is_nb_dpm_enabled = 1;
	rv_hwmgr->dpm_flags = 1;
	rv_hwmgr->disable_smu_acp_s3_handshake = 1;
	rv_hwmgr->disable_notify_smu_vpu_recovery = 0;
	rv_hwmgr->gfx_off_controled_by_driver = false;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicM3Arbiter);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_UVDPowerGating);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_UVDDynamicPowerGating);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_VCEPowerGating);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SamuPowerGating);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_ACP);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SclkDeepSleep);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_GFXDynamicMGPowerGating);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SclkThrottleLowNotification);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_DisableVoltageIsland);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicUVDState);

	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_PG_FLAGS;
	result = cgs_query_system_info(hwmgr->device, &sys_info);
	if (!result) {
		if (sys_info.value & AMD_PG_SUPPORT_GFX_DMG)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				      PHM_PlatformCaps_GFXDynamicMGPowerGating);
	}

	return 0;
}

static int rv_construct_max_power_limits_table(struct pp_hwmgr *hwmgr,
			struct phm_clock_and_voltage_limits *table)
{
	return 0;
}

static int rv_init_dynamic_state_adjustment_rule_settings(
							struct pp_hwmgr *hwmgr)
{
	uint32_t table_size =
		sizeof(struct phm_clock_voltage_dependency_table) +
		(7 * sizeof(struct phm_clock_voltage_dependency_record));

	struct phm_clock_voltage_dependency_table *table_clk_vlt =
					kzalloc(table_size, GFP_KERNEL);

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

static int rv_get_system_info_data(struct pp_hwmgr *hwmgr)
{
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)hwmgr->backend;

	rv_data->sys_info.htc_hyst_lmt = 5;
	rv_data->sys_info.htc_tmp_lmt = 203;

	if (rv_data->thermal_auto_throttling_treshold == 0)
		 rv_data->thermal_auto_throttling_treshold = 203;

	rv_construct_max_power_limits_table (hwmgr,
				    &hwmgr->dyn_state.max_clock_voltage_on_ac);

	rv_init_dynamic_state_adjustment_rule_settings(hwmgr);

	return 0;
}

static int rv_construct_boot_state(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int rv_tf_set_clock_limit(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);
	struct PP_Clocks clocks = {0};
	struct pp_display_clock_request clock_req;

	clocks.dcefClock = hwmgr->display_config.min_dcef_set_clk;
	clocks.dcefClockInSR = hwmgr->display_config.min_dcef_deep_sleep_set_clk;
	clock_req.clock_type = amd_pp_dcf_clock;
	clock_req.clock_freq_in_khz = clocks.dcefClock * 10;

	if (clocks.dcefClock == 0 && clocks.dcefClockInSR == 0)
		clock_req.clock_freq_in_khz = rv_data->dcf_actual_hard_min_freq;

	PP_ASSERT_WITH_CODE(!rv_display_clock_voltage_request(hwmgr, &clock_req),
				"Attempt to set DCF Clock Failed!", return -EINVAL);

	if(rv_data->need_min_deep_sleep_dcefclk && 0 != clocks.dcefClockInSR)
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetMinDeepSleepDcefclk,
					clocks.dcefClockInSR / 100);
	/*
	if(!rv_data->isp_tileA_power_gated || !rv_data->isp_tileB_power_gated) {
		if ((hwmgr->ispArbiter.iclk != 0) && (rv_data->ISPActualHardMinFreq != (hwmgr->ispArbiter.iclk / 100) )) {
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetHardMinIspclkByFreq, hwmgr->ispArbiter.iclk / 100);
			rv_read_arg_from_smc(hwmgr->smumgr, &rv_data->ISPActualHardMinFreq),
		}
	} */

	if((hwmgr->gfx_arbiter.sclk_hard_min != 0) &&
		((hwmgr->gfx_arbiter.sclk_hard_min / 100) != rv_data->soc_actual_hard_min_freq)) {
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetHardMinSocclkByFreq,
					hwmgr->gfx_arbiter.sclk_hard_min / 100);
			rv_read_arg_from_smc(hwmgr->smumgr, &rv_data->soc_actual_hard_min_freq);
	}

	if ((hwmgr->gfx_arbiter.gfxclk != 0) &&
		(rv_data->gfx_actual_soft_min_freq != (hwmgr->gfx_arbiter.gfxclk))) {
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetMinVideoGfxclkFreq,
					hwmgr->gfx_arbiter.gfxclk / 100);
		rv_read_arg_from_smc(hwmgr->smumgr, &rv_data->gfx_actual_soft_min_freq);
	}

	if ((hwmgr->gfx_arbiter.fclk != 0) &&
		(rv_data->fabric_actual_soft_min_freq != (hwmgr->gfx_arbiter.fclk / 100))) {
		smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SetMinVideoFclkFreq,
					hwmgr->gfx_arbiter.fclk / 100);
		rv_read_arg_from_smc(hwmgr->smumgr, &rv_data->fabric_actual_soft_min_freq);
	}

	return 0;
}

static int rv_tf_set_num_active_display(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	uint32_t  num_of_active_displays = 0;
	struct cgs_display_info info = {0};

	cgs_get_active_displays_info(hwmgr->device, &info);
	num_of_active_displays = info.display_count;

	smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetDisplayCount,
				num_of_active_displays);
	return 0;
}

static const struct phm_master_table_item rv_set_power_state_list[] = {
	{ NULL, rv_tf_set_clock_limit },
	{ NULL, rv_tf_set_num_active_display },
	{ }
};

static const struct phm_master_table_header rv_set_power_state_master = {
	0,
	PHM_MasterTableFlag_None,
	rv_set_power_state_list
};

static int rv_tf_init_power_gate_state(struct pp_hwmgr *hwmgr, void *input,
				void *output, void *storage, int result)
{
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);

	rv_data->vcn_power_gated = true;
	rv_data->isp_tileA_power_gated = true;
	rv_data->isp_tileB_power_gated = true;

	return 0;
}

static const struct phm_master_table_item rv_setup_asic_list[] = {
	{ .tableFunction = rv_tf_init_power_gate_state },
	{ }
};

static const struct phm_master_table_header rv_setup_asic_master = {
	0,
	PHM_MasterTableFlag_None,
	rv_setup_asic_list
};

static int rv_tf_reset_cc6_data(struct pp_hwmgr *hwmgr,
					void *input, void *output,
					void *storage, int result)
{
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);

	rv_data->separation_time = 0;
	rv_data->cc6_disable = false;
	rv_data->pstate_disable = false;
	rv_data->cc6_setting_changed = false;

	return 0;
}

static const struct phm_master_table_item rv_power_down_asic_list[] = {
	{ .tableFunction = rv_tf_reset_cc6_data },
	{ }
};

static const struct phm_master_table_header rv_power_down_asic_master = {
	0,
	PHM_MasterTableFlag_None,
	rv_power_down_asic_list
};


static int rv_tf_disable_gfx_off(struct pp_hwmgr *hwmgr,
						void *input, void *output,
						void *storage, int result)
{
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);

	if (rv_data->gfx_off_controled_by_driver)
		smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_DisableGfxOff);

	return 0;
}

static const struct phm_master_table_item rv_disable_dpm_list[] = {
	{NULL, rv_tf_disable_gfx_off},
	{ },
};


static const struct phm_master_table_header rv_disable_dpm_master = {
	0,
	PHM_MasterTableFlag_None,
	rv_disable_dpm_list
};

static int rv_tf_enable_gfx_off(struct pp_hwmgr *hwmgr,
						void *input, void *output,
						void *storage, int result)
{
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);

	if (rv_data->gfx_off_controled_by_driver)
		smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_EnableGfxOff);

	return 0;
}

static const struct phm_master_table_item rv_enable_dpm_list[] = {
	{NULL, rv_tf_enable_gfx_off},
	{ },
};

static const struct phm_master_table_header rv_enable_dpm_master = {
	0,
	PHM_MasterTableFlag_None,
	rv_enable_dpm_list
};

static int rv_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps)
{
	return 0;
}

/* temporary hardcoded clock voltage breakdown tables */
DpmClock_t VddDcfClk[]= {
	{ 300, 2600},
	{ 600, 3200},
	{ 600, 3600},
};

DpmClock_t VddSocClk[]= {
	{ 478, 2600},
	{ 722, 3200},
	{ 722, 3600},
};

DpmClock_t VddFClk[]= {
	{ 400, 2600},
	{1200, 3200},
	{1200, 3600},
};

DpmClock_t VddDispClk[]= {
	{ 435, 2600},
	{ 661, 3200},
	{1086, 3600},
};

DpmClock_t VddDppClk[]= {
	{ 435, 2600},
	{ 661, 3200},
	{ 661, 3600},
};

DpmClock_t VddPhyClk[]= {
	{ 540, 2600},
	{ 810, 3200},
	{ 810, 3600},
};

static int rv_get_clock_voltage_dependency_table(struct pp_hwmgr *hwmgr,
			struct rv_voltage_dependency_table **pptable,
			uint32_t num_entry, DpmClock_t *pclk_dependency_table)
{
	uint32_t table_size, i;
	struct rv_voltage_dependency_table *ptable;

	table_size = sizeof(uint32_t) + sizeof(struct rv_voltage_dependency_table) * num_entry;
	ptable = kzalloc(table_size, GFP_KERNEL);

	if (NULL == ptable)
		return -ENOMEM;

	ptable->count = num_entry;

	for (i = 0; i < ptable->count; i++) {
		ptable->entries[i].clk         = pclk_dependency_table->Freq * 100;
		ptable->entries[i].vol         = pclk_dependency_table->Vol;
		pclk_dependency_table++;
	}

	*pptable = ptable;

	return 0;
}


static int rv_populate_clock_table(struct pp_hwmgr *hwmgr)
{
	int result;

	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);
	DpmClocks_t  *table = &(rv_data->clock_table);
	struct rv_clock_voltage_information *pinfo = &(rv_data->clock_vol_info);

	result = rv_copy_table_from_smc(hwmgr->smumgr, (uint8_t *)table, CLOCKTABLE);

	PP_ASSERT_WITH_CODE((0 == result),
			"Attempt to copy clock table from smc failed",
			return result);

	if (0 == result && table->DcefClocks[0].Freq != 0) {
		rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dcefclk,
						NUM_DCEFCLK_DPM_LEVELS,
						&rv_data->clock_table.DcefClocks[0]);
		rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_socclk,
						NUM_SOCCLK_DPM_LEVELS,
						&rv_data->clock_table.SocClocks[0]);
		rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_fclk,
						NUM_FCLK_DPM_LEVELS,
						&rv_data->clock_table.FClocks[0]);
		rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_mclk,
						NUM_MEMCLK_DPM_LEVELS,
						&rv_data->clock_table.MemClocks[0]);
	} else {
		rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dcefclk,
						ARRAY_SIZE(VddDcfClk),
						&VddDcfClk[0]);
		rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_socclk,
						ARRAY_SIZE(VddSocClk),
						&VddSocClk[0]);
		rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_fclk,
						ARRAY_SIZE(VddFClk),
						&VddFClk[0]);
	}
	rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dispclk,
					ARRAY_SIZE(VddDispClk),
					&VddDispClk[0]);
	rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_dppclk,
					ARRAY_SIZE(VddDppClk), &VddDppClk[0]);
	rv_get_clock_voltage_dependency_table(hwmgr, &pinfo->vdd_dep_on_phyclk,
					ARRAY_SIZE(VddPhyClk), &VddPhyClk[0]);

	return 0;
}

static int rv_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct rv_hwmgr *data;

	data = kzalloc(sizeof(struct rv_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;

	result = rv_initialize_dpm_defaults(hwmgr);
	if (result != 0) {
		pr_err("rv_initialize_dpm_defaults failed\n");
		return result;
	}

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
                PHM_PlatformCaps_PowerPlaySupport);

	rv_populate_clock_table(hwmgr);

	result = rv_get_system_info_data(hwmgr);
	if (result != 0) {
		pr_err("rv_get_system_info_data failed\n");
		return result;
	}

	rv_construct_boot_state(hwmgr);

	result = phm_construct_table(hwmgr, &rv_setup_asic_master,
				&(hwmgr->setup_asic));
	if (result != 0) {
		pr_err("Fail to construct setup ASIC\n");
		return result;
	}

	result = phm_construct_table(hwmgr, &rv_power_down_asic_master,
				&(hwmgr->power_down_asic));
	if (result != 0) {
		pr_err("Fail to construct power down ASIC\n");
		return result;
	}

	result = phm_construct_table(hwmgr, &rv_set_power_state_master,
				&(hwmgr->set_power_state));
	if (result != 0) {
		pr_err("Fail to construct set_power_state\n");
		return result;
	}

	result = phm_construct_table(hwmgr, &rv_disable_dpm_master,
				&(hwmgr->disable_dynamic_state_management));
	if (result != 0) {
		pr_err("Fail to disable_dynamic_state\n");
		return result;
	}
	result = phm_construct_table(hwmgr, &rv_enable_dpm_master,
				&(hwmgr->enable_dynamic_state_management));
	if (result != 0) {
		pr_err("Fail to enable_dynamic_state\n");
		return result;
	}

	hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
						RAVEN_MAX_HARDWARE_POWERLEVELS;

	hwmgr->platform_descriptor.hardwarePerformanceLevels =
						RAVEN_MAX_HARDWARE_POWERLEVELS;

	hwmgr->platform_descriptor.vbiosInterruptId = 0;

	hwmgr->platform_descriptor.clockStep.engineClock = 500;

	hwmgr->platform_descriptor.clockStep.memoryClock = 500;

	hwmgr->platform_descriptor.minimumClocksReductionPercentage = 50;

	rv_init_vq_budget_table(hwmgr);

	return result;
}

static int rv_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);
	struct rv_clock_voltage_information *pinfo = &(rv_data->clock_vol_info);

	phm_destroy_table(hwmgr, &(hwmgr->set_power_state));
	phm_destroy_table(hwmgr, &(hwmgr->enable_dynamic_state_management));
	phm_destroy_table(hwmgr, &(hwmgr->disable_dynamic_state_management));
	phm_destroy_table(hwmgr, &(hwmgr->power_down_asic));
	phm_destroy_table(hwmgr, &(hwmgr->setup_asic));

	if (pinfo->vdd_dep_on_dcefclk) {
		kfree(pinfo->vdd_dep_on_dcefclk);
		pinfo->vdd_dep_on_dcefclk = NULL;
	}
	if (pinfo->vdd_dep_on_socclk) {
		kfree(pinfo->vdd_dep_on_socclk);
		pinfo->vdd_dep_on_socclk = NULL;
	}
	if (pinfo->vdd_dep_on_fclk) {
		kfree(pinfo->vdd_dep_on_fclk);
		pinfo->vdd_dep_on_fclk = NULL;
	}
	if (pinfo->vdd_dep_on_dispclk) {
		kfree(pinfo->vdd_dep_on_dispclk);
		pinfo->vdd_dep_on_dispclk = NULL;
	}
	if (pinfo->vdd_dep_on_dppclk) {
		kfree(pinfo->vdd_dep_on_dppclk);
		pinfo->vdd_dep_on_dppclk = NULL;
	}
	if (pinfo->vdd_dep_on_phyclk) {
		kfree(pinfo->vdd_dep_on_phyclk);
		pinfo->vdd_dep_on_phyclk = NULL;
	}

	if (NULL != hwmgr->dyn_state.vddc_dep_on_dal_pwrl) {
		kfree(hwmgr->dyn_state.vddc_dep_on_dal_pwrl);
		hwmgr->dyn_state.vddc_dep_on_dal_pwrl = NULL;
	}

	if (NULL != hwmgr->dyn_state.vq_budgeting_table) {
		kfree(hwmgr->dyn_state.vq_budgeting_table);
		hwmgr->dyn_state.vq_budgeting_table = NULL;
	}

	kfree(hwmgr->backend);
	hwmgr->backend = NULL;

	return 0;
}

static int rv_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	return 0;
}

static int rv_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	return 0;
}

static int rv_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	return 0;
}

static int rv_dpm_patch_boot_state(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *hw_ps)
{
	return 0;
}

static int rv_dpm_get_pp_table_entry_callback(
						     struct pp_hwmgr *hwmgr,
					   struct pp_hw_power_state *hw_ps,
							  unsigned int index,
						     const void *clock_info)
{
	struct rv_power_state *rv_ps = cast_rv_ps(hw_ps);

	const ATOM_PPLIB_CZ_CLOCK_INFO *rv_clock_info = clock_info;

	struct phm_clock_voltage_dependency_table *table =
				    hwmgr->dyn_state.vddc_dependency_on_sclk;
	uint8_t clock_info_index = rv_clock_info->index;

	if (clock_info_index > (uint8_t)(hwmgr->platform_descriptor.hardwareActivityPerformanceLevels - 1))
		clock_info_index = (uint8_t)(hwmgr->platform_descriptor.hardwareActivityPerformanceLevels - 1);

	rv_ps->levels[index].engine_clock = table->entries[clock_info_index].clk;
	rv_ps->levels[index].vddc_index = (uint8_t)table->entries[clock_info_index].v;

	rv_ps->level = index + 1;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		rv_ps->levels[index].ds_divider_index = 5;
		rv_ps->levels[index].ss_divider_index = 5;
	}

	return 0;
}

static int rv_dpm_get_num_of_pp_table_entries(struct pp_hwmgr *hwmgr)
{
	int result;
	unsigned long ret = 0;

	result = pp_tables_get_num_of_entries(hwmgr, &ret);

	return result ? 0 : ret;
}

static int rv_dpm_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		    unsigned long entry, struct pp_power_state *ps)
{
	int result;
	struct rv_power_state *rv_ps;

	ps->hardware.magic = PhwRaven_Magic;

	rv_ps = cast_rv_ps(&(ps->hardware));

	result = pp_tables_get_entry(hwmgr, entry, ps,
			rv_dpm_get_pp_table_entry_callback);

	rv_ps->uvd_clocks.vclk = ps->uvd_clocks.VCLK;
	rv_ps->uvd_clocks.dclk = ps->uvd_clocks.DCLK;

	return result;
}

static int rv_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct rv_power_state);
}

static int rv_set_cpu_power_state(struct pp_hwmgr *hwmgr)
{
	return 0;
}


static int rv_store_cc6_data(struct pp_hwmgr *hwmgr, uint32_t separation_time,
			bool cc6_disable, bool pstate_disable, bool pstate_switch_disable)
{
	return 0;
}

static int rv_get_dal_power_level(struct pp_hwmgr *hwmgr,
		struct amd_pp_simple_clock_info *info)
{
	return -EINVAL;
}

static int rv_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	return 0;
}

static int rv_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	return 0;
}

static int rv_get_performance_level(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *state,
				PHM_PerformanceLevelDesignation designation, uint32_t index,
				PHM_PerformanceLevel *level)
{
	const struct rv_power_state *ps;
	struct rv_hwmgr *data;
	uint32_t level_index;
	uint32_t i;
	uint32_t vol_dep_record_index = 0;

	if (level == NULL || hwmgr == NULL || state == NULL)
		return -EINVAL;

	data = (struct rv_hwmgr *)(hwmgr->backend);
	ps = cast_const_rv_ps(state);

	level_index = index > ps->level - 1 ? ps->level - 1 : index;
	level->coreClock = ps->levels[level_index].engine_clock;

	if (designation == PHM_PerformanceLevelDesignation_PowerContainment) {
		for (i = 1; i < ps->level; i++) {
			if (ps->levels[i].engine_clock > data->dce_slow_sclk_threshold) {
				level->coreClock = ps->levels[i].engine_clock;
				break;
			}
		}
	}

	if (level_index == 0) {
		vol_dep_record_index = data->clock_vol_info.vdd_dep_on_fclk->count - 1;
		level->memory_clock =
			data->clock_vol_info.vdd_dep_on_fclk->entries[vol_dep_record_index].clk;
	} else
		level->memory_clock = data->clock_vol_info.vdd_dep_on_fclk->entries[0].clk;

	level->nonLocalMemoryFreq = 0;
	level->nonLocalMemoryWidth = 0;

	return 0;
}

static int rv_get_current_shallow_sleep_clocks(struct pp_hwmgr *hwmgr,
	const struct pp_hw_power_state *state, struct pp_clock_info *clock_info)
{
	const struct rv_power_state *ps = cast_const_rv_ps(state);

	clock_info->min_eng_clk = ps->levels[0].engine_clock / (1 << (ps->levels[0].ss_divider_index));
	clock_info->max_eng_clk = ps->levels[ps->level - 1].engine_clock / (1 << (ps->levels[ps->level - 1].ss_divider_index));

	return 0;
}

#define MEM_FREQ_LOW_LATENCY        25000
#define MEM_FREQ_HIGH_LATENCY       80000
#define MEM_LATENCY_HIGH            245
#define MEM_LATENCY_LOW             35
#define MEM_LATENCY_ERR             0xFFFF


static uint32_t rv_get_mem_latency(struct pp_hwmgr *hwmgr,
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

static int rv_get_clock_by_type_with_latency(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_latency *clocks)
{
	uint32_t i;
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);
	struct rv_clock_voltage_information *pinfo = &(rv_data->clock_vol_info);
	struct rv_voltage_dependency_table *pclk_vol_table;
	bool latency_required = false;

	if (pinfo == NULL)
		return -EINVAL;

	switch (type) {
	case amd_pp_mem_clock:
		pclk_vol_table = pinfo->vdd_dep_on_mclk;
		latency_required = true;
		break;
	case amd_pp_f_clock:
		pclk_vol_table = pinfo->vdd_dep_on_fclk;
		latency_required = true;
		break;
	case amd_pp_dcf_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dcefclk;
		break;
	case amd_pp_disp_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dispclk;
		break;
	case amd_pp_phy_clock:
		pclk_vol_table = pinfo->vdd_dep_on_phyclk;
		break;
	case amd_pp_dpp_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dppclk;
	default:
		return -EINVAL;
	}

	if (pclk_vol_table == NULL || pclk_vol_table->count == 0)
		return -EINVAL;

	clocks->num_levels = 0;
	for (i = 0; i < pclk_vol_table->count; i++) {
		clocks->data[i].clocks_in_khz = pclk_vol_table->entries[i].clk;
		clocks->data[i].latency_in_us = latency_required ?
						rv_get_mem_latency(hwmgr,
						pclk_vol_table->entries[i].clk) :
						0;
		clocks->num_levels++;
	}

	return 0;
}

static int rv_get_clock_by_type_with_voltage(struct pp_hwmgr *hwmgr,
		enum amd_pp_clock_type type,
		struct pp_clock_levels_with_voltage *clocks)
{
	uint32_t i;
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);
	struct rv_clock_voltage_information *pinfo = &(rv_data->clock_vol_info);
	struct rv_voltage_dependency_table *pclk_vol_table = NULL;

	if (pinfo == NULL)
		return -EINVAL;

	switch (type) {
	case amd_pp_mem_clock:
		pclk_vol_table = pinfo->vdd_dep_on_mclk;
		break;
	case amd_pp_f_clock:
		pclk_vol_table = pinfo->vdd_dep_on_fclk;
		break;
	case amd_pp_dcf_clock:
		pclk_vol_table = pinfo->vdd_dep_on_dcefclk;
		break;
	case amd_pp_soc_clock:
		pclk_vol_table = pinfo->vdd_dep_on_socclk;
		break;
	default:
		return -EINVAL;
	}

	if (pclk_vol_table == NULL || pclk_vol_table->count == 0)
		return -EINVAL;

	clocks->num_levels = 0;
	for (i = 0; i < pclk_vol_table->count; i++) {
		clocks->data[i].clocks_in_khz = pclk_vol_table->entries[i].clk;
		clocks->data[i].voltage_in_mv = pclk_vol_table->entries[i].vol;
		clocks->num_levels++;
	}

	return 0;
}

int rv_display_clock_voltage_request(struct pp_hwmgr *hwmgr,
		struct pp_display_clock_request *clock_req)
{
	int result = 0;
	struct rv_hwmgr *rv_data = (struct rv_hwmgr *)(hwmgr->backend);
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 1000;
	PPSMC_Msg        msg;

	switch (clk_type) {
	case amd_pp_dcf_clock:
		if (clk_freq == rv_data->dcf_actual_hard_min_freq)
			return 0;
		msg =  PPSMC_MSG_SetHardMinDcefclkByFreq;
		rv_data->dcf_actual_hard_min_freq = clk_freq;
		break;
	case amd_pp_soc_clock:
		 msg = PPSMC_MSG_SetHardMinSocclkByFreq;
		break;
	case amd_pp_f_clock:
		if (clk_freq == rv_data->f_actual_hard_min_freq)
			return 0;
		rv_data->f_actual_hard_min_freq = clk_freq;
		msg = PPSMC_MSG_SetHardMinFclkByFreq;
		break;
	default:
		pr_info("[DisplayClockVoltageRequest]Invalid Clock Type!");
		return -EINVAL;
	}

	result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, msg,
							clk_freq);

	return result;
}

static int rv_get_max_high_clocks(struct pp_hwmgr *hwmgr, struct amd_pp_simple_clock_info *clocks)
{
	return -EINVAL;
}

static int rv_thermal_get_temperature(struct pp_hwmgr *hwmgr)
{
	uint32_t reg_offset = soc15_get_register_offset(THM_HWID, 0,
			mmTHM_TCON_CUR_TMP_BASE_IDX, mmTHM_TCON_CUR_TMP);
	uint32_t reg_value = cgs_read_register(hwmgr->device, reg_offset);
	int cur_temp =
		(reg_value & THM_TCON_CUR_TMP__CUR_TEMP_MASK) >> THM_TCON_CUR_TMP__CUR_TEMP__SHIFT;

	if (cur_temp & THM_TCON_CUR_TMP__CUR_TEMP_RANGE_SEL_MASK)
		cur_temp = ((cur_temp / 8) - 49) * PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	else
		cur_temp = (cur_temp / 8) * PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return cur_temp;
}

static int rv_read_sensor(struct pp_hwmgr *hwmgr, int idx,
			  void *value, int *size)
{
	switch (idx) {
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		*((uint32_t *)value) = rv_thermal_get_temperature(hwmgr);
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct pp_hwmgr_func rv_hwmgr_funcs = {
	.backend_init = rv_hwmgr_backend_init,
	.backend_fini = rv_hwmgr_backend_fini,
	.asic_setup = NULL,
	.apply_state_adjust_rules = rv_apply_state_adjust_rules,
	.force_dpm_level = rv_dpm_force_dpm_level,
	.get_power_state_size = rv_get_power_state_size,
	.powerdown_uvd = NULL,
	.powergate_uvd = NULL,
	.powergate_vce = NULL,
	.get_mclk = rv_dpm_get_mclk,
	.get_sclk = rv_dpm_get_sclk,
	.patch_boot_state = rv_dpm_patch_boot_state,
	.get_pp_table_entry = rv_dpm_get_pp_table_entry,
	.get_num_of_pp_table_entries = rv_dpm_get_num_of_pp_table_entries,
	.set_cpu_power_state = rv_set_cpu_power_state,
	.store_cc6_data = rv_store_cc6_data,
	.force_clock_level = rv_force_clock_level,
	.print_clock_levels = rv_print_clock_levels,
	.get_dal_power_level = rv_get_dal_power_level,
	.get_performance_level = rv_get_performance_level,
	.get_current_shallow_sleep_clocks = rv_get_current_shallow_sleep_clocks,
	.get_clock_by_type_with_latency = rv_get_clock_by_type_with_latency,
	.get_clock_by_type_with_voltage = rv_get_clock_by_type_with_voltage,
	.get_max_high_clocks = rv_get_max_high_clocks,
	.read_sensor = rv_read_sensor,
};

int rv_init_function_pointers(struct pp_hwmgr *hwmgr)
{
	hwmgr->hwmgr_func = &rv_hwmgr_funcs;
	hwmgr->pptable_func = &pptable_funcs;
	return 0;
}
