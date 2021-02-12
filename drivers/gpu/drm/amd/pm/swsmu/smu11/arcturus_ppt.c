/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#define SWSMU_CODE_LAYER_L2

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_atombios.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_arcturus.h"
#include "soc15_common.h"
#include "atom.h"
#include "power_state.h"
#include "arcturus_ppt.h"
#include "smu_v11_0_pptable.h"
#include "arcturus_ppsmc.h"
#include "nbio/nbio_7_4_offset.h"
#include "nbio/nbio_7_4_sh_mask.h"
#include "thm/thm_11_0_2_offset.h"
#include "thm/thm_11_0_2_sh_mask.h"
#include "amdgpu_xgmi.h"
#include <linux/i2c.h>
#include <linux/pci.h>
#include "amdgpu_ras.h"
#include "smu_cmn.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

#define to_amdgpu_device(x) (container_of(x, struct amdgpu_device, pm.smu_i2c))

#define ARCTURUS_FEA_MAP(smu_feature, arcturus_feature) \
	[smu_feature] = {1, (arcturus_feature)}

#define SMU_FEATURES_LOW_MASK        0x00000000FFFFFFFF
#define SMU_FEATURES_LOW_SHIFT       0
#define SMU_FEATURES_HIGH_MASK       0xFFFFFFFF00000000
#define SMU_FEATURES_HIGH_SHIFT      32

#define SMC_DPM_FEATURE ( \
	FEATURE_DPM_PREFETCHER_MASK | \
	FEATURE_DPM_GFXCLK_MASK | \
	FEATURE_DPM_UCLK_MASK | \
	FEATURE_DPM_SOCCLK_MASK | \
	FEATURE_DPM_MP0CLK_MASK | \
	FEATURE_DPM_FCLK_MASK | \
	FEATURE_DPM_XGMI_MASK)

/* possible frequency drift (1Mhz) */
#define EPSILON				1

#define smnPCIE_ESM_CTRL			0x111003D0

static const struct cmn2asic_msg_mapping arcturus_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			     PPSMC_MSG_TestMessage,			0),
	MSG_MAP(GetSmuVersion,			     PPSMC_MSG_GetSmuVersion,			1),
	MSG_MAP(GetDriverIfVersion,		     PPSMC_MSG_GetDriverIfVersion,		1),
	MSG_MAP(SetAllowedFeaturesMaskLow,	     PPSMC_MSG_SetAllowedFeaturesMaskLow,	0),
	MSG_MAP(SetAllowedFeaturesMaskHigh,	     PPSMC_MSG_SetAllowedFeaturesMaskHigh,	0),
	MSG_MAP(EnableAllSmuFeatures,		     PPSMC_MSG_EnableAllSmuFeatures,		0),
	MSG_MAP(DisableAllSmuFeatures,		     PPSMC_MSG_DisableAllSmuFeatures,		0),
	MSG_MAP(EnableSmuFeaturesLow,		     PPSMC_MSG_EnableSmuFeaturesLow,		1),
	MSG_MAP(EnableSmuFeaturesHigh,		     PPSMC_MSG_EnableSmuFeaturesHigh,		1),
	MSG_MAP(DisableSmuFeaturesLow,		     PPSMC_MSG_DisableSmuFeaturesLow,		0),
	MSG_MAP(DisableSmuFeaturesHigh,		     PPSMC_MSG_DisableSmuFeaturesHigh,		0),
	MSG_MAP(GetEnabledSmuFeaturesLow,	     PPSMC_MSG_GetEnabledSmuFeaturesLow,	0),
	MSG_MAP(GetEnabledSmuFeaturesHigh,	     PPSMC_MSG_GetEnabledSmuFeaturesHigh,	0),
	MSG_MAP(SetDriverDramAddrHigh,		     PPSMC_MSG_SetDriverDramAddrHigh,		1),
	MSG_MAP(SetDriverDramAddrLow,		     PPSMC_MSG_SetDriverDramAddrLow,		1),
	MSG_MAP(SetToolsDramAddrHigh,		     PPSMC_MSG_SetToolsDramAddrHigh,		0),
	MSG_MAP(SetToolsDramAddrLow,		     PPSMC_MSG_SetToolsDramAddrLow,		0),
	MSG_MAP(TransferTableSmu2Dram,		     PPSMC_MSG_TransferTableSmu2Dram,		1),
	MSG_MAP(TransferTableDram2Smu,		     PPSMC_MSG_TransferTableDram2Smu,		0),
	MSG_MAP(UseDefaultPPTable,		     PPSMC_MSG_UseDefaultPPTable,		0),
	MSG_MAP(UseBackupPPTable,		     PPSMC_MSG_UseBackupPPTable,		0),
	MSG_MAP(SetSystemVirtualDramAddrHigh,	     PPSMC_MSG_SetSystemVirtualDramAddrHigh,	0),
	MSG_MAP(SetSystemVirtualDramAddrLow,	     PPSMC_MSG_SetSystemVirtualDramAddrLow,	0),
	MSG_MAP(EnterBaco,			     PPSMC_MSG_EnterBaco,			0),
	MSG_MAP(ExitBaco,			     PPSMC_MSG_ExitBaco,			0),
	MSG_MAP(ArmD3,				     PPSMC_MSG_ArmD3,				0),
	MSG_MAP(SetSoftMinByFreq,		     PPSMC_MSG_SetSoftMinByFreq,		0),
	MSG_MAP(SetSoftMaxByFreq,		     PPSMC_MSG_SetSoftMaxByFreq,		0),
	MSG_MAP(SetHardMinByFreq,		     PPSMC_MSG_SetHardMinByFreq,		0),
	MSG_MAP(SetHardMaxByFreq,		     PPSMC_MSG_SetHardMaxByFreq,		0),
	MSG_MAP(GetMinDpmFreq,			     PPSMC_MSG_GetMinDpmFreq,			0),
	MSG_MAP(GetMaxDpmFreq,			     PPSMC_MSG_GetMaxDpmFreq,			0),
	MSG_MAP(GetDpmFreqByIndex,		     PPSMC_MSG_GetDpmFreqByIndex,		1),
	MSG_MAP(SetWorkloadMask,		     PPSMC_MSG_SetWorkloadMask,			1),
	MSG_MAP(SetDfSwitchType,		     PPSMC_MSG_SetDfSwitchType,			0),
	MSG_MAP(GetVoltageByDpm,		     PPSMC_MSG_GetVoltageByDpm,			0),
	MSG_MAP(GetVoltageByDpmOverdrive,	     PPSMC_MSG_GetVoltageByDpmOverdrive,	0),
	MSG_MAP(SetPptLimit,			     PPSMC_MSG_SetPptLimit,			0),
	MSG_MAP(GetPptLimit,			     PPSMC_MSG_GetPptLimit,			1),
	MSG_MAP(PowerUpVcn0,			     PPSMC_MSG_PowerUpVcn0,			0),
	MSG_MAP(PowerDownVcn0,			     PPSMC_MSG_PowerDownVcn0,			0),
	MSG_MAP(PowerUpVcn1,			     PPSMC_MSG_PowerUpVcn1,			0),
	MSG_MAP(PowerDownVcn1,			     PPSMC_MSG_PowerDownVcn1,			0),
	MSG_MAP(PrepareMp1ForUnload,		     PPSMC_MSG_PrepareMp1ForUnload,		0),
	MSG_MAP(PrepareMp1ForReset,		     PPSMC_MSG_PrepareMp1ForReset,		0),
	MSG_MAP(PrepareMp1ForShutdown,		     PPSMC_MSG_PrepareMp1ForShutdown,		0),
	MSG_MAP(SoftReset,			     PPSMC_MSG_SoftReset,			0),
	MSG_MAP(RunAfllBtc,			     PPSMC_MSG_RunAfllBtc,			0),
	MSG_MAP(RunDcBtc,			     PPSMC_MSG_RunDcBtc,			0),
	MSG_MAP(DramLogSetDramAddrHigh,		     PPSMC_MSG_DramLogSetDramAddrHigh,		0),
	MSG_MAP(DramLogSetDramAddrLow,		     PPSMC_MSG_DramLogSetDramAddrLow,		0),
	MSG_MAP(DramLogSetDramSize,		     PPSMC_MSG_DramLogSetDramSize,		0),
	MSG_MAP(GetDebugData,			     PPSMC_MSG_GetDebugData,			0),
	MSG_MAP(WaflTest,			     PPSMC_MSG_WaflTest,			0),
	MSG_MAP(SetXgmiMode,			     PPSMC_MSG_SetXgmiMode,			0),
	MSG_MAP(SetMemoryChannelEnable,		     PPSMC_MSG_SetMemoryChannelEnable,		0),
	MSG_MAP(DFCstateControl,		     PPSMC_MSG_DFCstateControl,			0),
	MSG_MAP(GmiPwrDnControl,		     PPSMC_MSG_GmiPwrDnControl,			0),
	MSG_MAP(ReadSerialNumTop32,		     PPSMC_MSG_ReadSerialNumTop32,		1),
	MSG_MAP(ReadSerialNumBottom32,		     PPSMC_MSG_ReadSerialNumBottom32,		1),
};

static const struct cmn2asic_mapping arcturus_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK, PPCLK_GFXCLK),
	CLK_MAP(SCLK,	PPCLK_GFXCLK),
	CLK_MAP(SOCCLK, PPCLK_SOCCLK),
	CLK_MAP(FCLK, PPCLK_FCLK),
	CLK_MAP(UCLK, PPCLK_UCLK),
	CLK_MAP(MCLK, PPCLK_UCLK),
	CLK_MAP(DCLK, PPCLK_DCLK),
	CLK_MAP(VCLK, PPCLK_VCLK),
};

static const struct cmn2asic_mapping arcturus_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(DPM_PREFETCHER),
	FEA_MAP(DPM_GFXCLK),
	FEA_MAP(DPM_UCLK),
	FEA_MAP(DPM_SOCCLK),
	FEA_MAP(DPM_FCLK),
	FEA_MAP(DPM_MP0CLK),
	ARCTURUS_FEA_MAP(SMU_FEATURE_XGMI_BIT, FEATURE_DPM_XGMI_BIT),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
	FEA_MAP(DS_LCLK),
	FEA_MAP(DS_FCLK),
	FEA_MAP(DS_UCLK),
	FEA_MAP(GFX_ULV),
	ARCTURUS_FEA_MAP(SMU_FEATURE_VCN_PG_BIT, FEATURE_DPM_VCN_BIT),
	FEA_MAP(RSMU_SMN_CG),
	FEA_MAP(WAFL_CG),
	FEA_MAP(PPT),
	FEA_MAP(TDC),
	FEA_MAP(APCC_PLUS),
	FEA_MAP(VR0HOT),
	FEA_MAP(VR1HOT),
	FEA_MAP(FW_CTF),
	FEA_MAP(FAN_CONTROL),
	FEA_MAP(THERMAL),
	FEA_MAP(OUT_OF_BAND_MONITOR),
	FEA_MAP(TEMP_DEPENDENT_VMIN),
};

static const struct cmn2asic_mapping arcturus_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PPTABLE),
	TAB_MAP(AVFS),
	TAB_MAP(AVFS_PSM_DEBUG),
	TAB_MAP(AVFS_FUSE_OVERRIDE),
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(DRIVER_SMU_CONFIG),
	TAB_MAP(OVERDRIVE),
	TAB_MAP(I2C_COMMANDS),
	TAB_MAP(ACTIVITY_MONITOR_COEFF),
};

static const struct cmn2asic_mapping arcturus_pwr_src_map[SMU_POWER_SOURCE_COUNT] = {
	PWR_MAP(AC),
	PWR_MAP(DC),
};

static const struct cmn2asic_mapping arcturus_workload_map[PP_SMC_POWER_PROFILE_COUNT] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT,	WORKLOAD_PPLIB_DEFAULT_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_POWERSAVING,		WORKLOAD_PPLIB_POWER_SAVING_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_COMPUTE_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
};

static int arcturus_tables_init(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;

	SMU_TABLE_INIT(tables, SMU_TABLE_PPTABLE, sizeof(PPTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	SMU_TABLE_INIT(tables, SMU_TABLE_I2C_COMMANDS, sizeof(SwI2cRequest_t),
			       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF,
		       sizeof(DpmActivityMonitorCoeffInt_t), PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM);

	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		return -ENOMEM;
	smu_table->metrics_time = 0;

	smu_table->gpu_metrics_table_size = sizeof(struct gpu_metrics_v1_0);
	smu_table->gpu_metrics_table = kzalloc(smu_table->gpu_metrics_table_size, GFP_KERNEL);
	if (!smu_table->gpu_metrics_table) {
		kfree(smu_table->metrics_table);
		return -ENOMEM;
	}

	return 0;
}

static int arcturus_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	smu_dpm->dpm_context = kzalloc(sizeof(struct smu_11_0_dpm_context),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;
	smu_dpm->dpm_context_size = sizeof(struct smu_11_0_dpm_context);

	smu_dpm->dpm_current_power_state = kzalloc(sizeof(struct smu_power_state),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_current_power_state)
		return -ENOMEM;

	smu_dpm->dpm_request_power_state = kzalloc(sizeof(struct smu_power_state),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_request_power_state)
		return -ENOMEM;

	return 0;
}

static int arcturus_init_smc_tables(struct smu_context *smu)
{
	int ret = 0;

	ret = arcturus_tables_init(smu);
	if (ret)
		return ret;

	ret = arcturus_allocate_dpm_context(smu);
	if (ret)
		return ret;

	return smu_v11_0_init_smc_tables(smu);
}

static int
arcturus_get_allowed_feature_mask(struct smu_context *smu,
				  uint32_t *feature_mask, uint32_t num)
{
	if (num > 2)
		return -EINVAL;

	/* pptable will handle the features to enable */
	memset(feature_mask, 0xFF, sizeof(uint32_t) * num);

	return 0;
}

static int arcturus_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_11_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	PPTable_t *driver_ppt = smu->smu_table.driver_pptable;
	struct smu_11_0_dpm_table *dpm_table = NULL;
	int ret = 0;

	/* socclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.soc_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_SOCCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!driver_ppt->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.socclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* gfxclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.gfx_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_GFXCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!driver_ppt->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.gfxclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* memclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.uclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_UCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!driver_ppt->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.uclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	/* fclk dpm table setup */
	dpm_table = &dpm_context->dpm_tables.fclk_table;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_FCLK_BIT)) {
		ret = smu_v11_0_set_single_dpm_table(smu,
						     SMU_FCLK,
						     dpm_table);
		if (ret)
			return ret;
		dpm_table->is_fine_grained =
			!driver_ppt->DpmDescriptor[PPCLK_FCLK].SnapToDiscrete;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.fclk / 100;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->min = dpm_table->dpm_levels[0].value;
		dpm_table->max = dpm_table->dpm_levels[0].value;
	}

	return 0;
}

static int arcturus_check_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_powerplay_table *powerplay_table =
		table_context->power_play_table;
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	if (powerplay_table->platform_caps & SMU_11_0_PP_PLATFORM_CAP_BACO ||
	    powerplay_table->platform_caps & SMU_11_0_PP_PLATFORM_CAP_MACO)
		smu_baco->platform_support = true;

	table_context->thermal_controller_type =
		powerplay_table->thermal_controller_type;

	return 0;
}

static int arcturus_store_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_powerplay_table *powerplay_table =
		table_context->power_play_table;

	memcpy(table_context->driver_pptable, &powerplay_table->smc_pptable,
	       sizeof(PPTable_t));

	return 0;
}

static int arcturus_append_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	struct atom_smc_dpm_info_v4_6 *smc_dpm_table;
	int index, ret;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					   smc_dpm_info);

	ret = amdgpu_atombios_get_data_table(smu->adev, index, NULL, NULL, NULL,
				      (uint8_t **)&smc_dpm_table);
	if (ret)
		return ret;

	dev_info(smu->adev->dev, "smc_dpm_info table revision(format.content): %d.%d\n",
			smc_dpm_table->table_header.format_revision,
			smc_dpm_table->table_header.content_revision);

	if ((smc_dpm_table->table_header.format_revision == 4) &&
	    (smc_dpm_table->table_header.content_revision == 6))
		memcpy(&smc_pptable->MaxVoltageStepGfx,
		       &smc_dpm_table->maxvoltagestepgfx,
		       sizeof(*smc_dpm_table) - offsetof(struct atom_smc_dpm_info_v4_6, maxvoltagestepgfx));

	return 0;
}

static int arcturus_setup_pptable(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v11_0_setup_pptable(smu);
	if (ret)
		return ret;

	ret = arcturus_store_powerplay_table(smu);
	if (ret)
		return ret;

	ret = arcturus_append_powerplay_table(smu);
	if (ret)
		return ret;

	ret = arcturus_check_powerplay_table(smu);
	if (ret)
		return ret;

	return ret;
}

static int arcturus_run_btc(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_RunAfllBtc, NULL);
	if (ret) {
		dev_err(smu->adev->dev, "RunAfllBtc failed!\n");
		return ret;
	}

	return smu_cmn_send_smc_msg(smu, SMU_MSG_RunDcBtc, NULL);
}

static int arcturus_populate_umd_state_clk(struct smu_context *smu)
{
	struct smu_11_0_dpm_context *dpm_context =
				smu->smu_dpm.dpm_context;
	struct smu_11_0_dpm_table *gfx_table =
				&dpm_context->dpm_tables.gfx_table;
	struct smu_11_0_dpm_table *mem_table =
				&dpm_context->dpm_tables.uclk_table;
	struct smu_11_0_dpm_table *soc_table =
				&dpm_context->dpm_tables.soc_table;
	struct smu_umd_pstate_table *pstate_table =
				&smu->pstate_table;

	pstate_table->gfxclk_pstate.min = gfx_table->min;
	pstate_table->gfxclk_pstate.peak = gfx_table->max;

	pstate_table->uclk_pstate.min = mem_table->min;
	pstate_table->uclk_pstate.peak = mem_table->max;

	pstate_table->socclk_pstate.min = soc_table->min;
	pstate_table->socclk_pstate.peak = soc_table->max;

	if (gfx_table->count > ARCTURUS_UMD_PSTATE_GFXCLK_LEVEL &&
	    mem_table->count > ARCTURUS_UMD_PSTATE_MCLK_LEVEL &&
	    soc_table->count > ARCTURUS_UMD_PSTATE_SOCCLK_LEVEL) {
		pstate_table->gfxclk_pstate.standard =
			gfx_table->dpm_levels[ARCTURUS_UMD_PSTATE_GFXCLK_LEVEL].value;
		pstate_table->uclk_pstate.standard =
			mem_table->dpm_levels[ARCTURUS_UMD_PSTATE_MCLK_LEVEL].value;
		pstate_table->socclk_pstate.standard =
			soc_table->dpm_levels[ARCTURUS_UMD_PSTATE_SOCCLK_LEVEL].value;
	} else {
		pstate_table->gfxclk_pstate.standard =
			pstate_table->gfxclk_pstate.min;
		pstate_table->uclk_pstate.standard =
			pstate_table->uclk_pstate.min;
		pstate_table->socclk_pstate.standard =
			pstate_table->socclk_pstate.min;
	}

	return 0;
}

static int arcturus_get_clk_table(struct smu_context *smu,
			struct pp_clock_levels_with_latency *clocks,
			struct smu_11_0_dpm_table *dpm_table)
{
	int i, count;

	count = (dpm_table->count > MAX_NUM_CLOCKS) ? MAX_NUM_CLOCKS : dpm_table->count;
	clocks->num_levels = count;

	for (i = 0; i < count; i++) {
		clocks->data[i].clocks_in_khz =
			dpm_table->dpm_levels[i].value * 1000;
		clocks->data[i].latency_in_us = 0;
	}

	return 0;
}

static int arcturus_freqs_in_same_level(int32_t frequency1,
					int32_t frequency2)
{
	return (abs(frequency1 - frequency2) <= EPSILON);
}

static int arcturus_get_smu_metrics_data(struct smu_context *smu,
					 MetricsMember_t member,
					 uint32_t *value)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	SmuMetrics_t *metrics = (SmuMetrics_t *)smu_table->metrics_table;
	int ret = 0;

	mutex_lock(&smu->metrics_lock);

	ret = smu_cmn_get_metrics_table_locked(smu,
					       NULL,
					       false);
	if (ret) {
		mutex_unlock(&smu->metrics_lock);
		return ret;
	}

	switch (member) {
	case METRICS_CURR_GFXCLK:
		*value = metrics->CurrClock[PPCLK_GFXCLK];
		break;
	case METRICS_CURR_SOCCLK:
		*value = metrics->CurrClock[PPCLK_SOCCLK];
		break;
	case METRICS_CURR_UCLK:
		*value = metrics->CurrClock[PPCLK_UCLK];
		break;
	case METRICS_CURR_VCLK:
		*value = metrics->CurrClock[PPCLK_VCLK];
		break;
	case METRICS_CURR_DCLK:
		*value = metrics->CurrClock[PPCLK_DCLK];
		break;
	case METRICS_CURR_FCLK:
		*value = metrics->CurrClock[PPCLK_FCLK];
		break;
	case METRICS_AVERAGE_GFXCLK:
		*value = metrics->AverageGfxclkFrequency;
		break;
	case METRICS_AVERAGE_SOCCLK:
		*value = metrics->AverageSocclkFrequency;
		break;
	case METRICS_AVERAGE_UCLK:
		*value = metrics->AverageUclkFrequency;
		break;
	case METRICS_AVERAGE_VCLK:
		*value = metrics->AverageVclkFrequency;
		break;
	case METRICS_AVERAGE_DCLK:
		*value = metrics->AverageDclkFrequency;
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = metrics->AverageGfxActivity;
		break;
	case METRICS_AVERAGE_MEMACTIVITY:
		*value = metrics->AverageUclkActivity;
		break;
	case METRICS_AVERAGE_VCNACTIVITY:
		*value = metrics->VcnActivityPercentage;
		break;
	case METRICS_AVERAGE_SOCKETPOWER:
		*value = metrics->AverageSocketPower << 8;
		break;
	case METRICS_TEMPERATURE_EDGE:
		*value = metrics->TemperatureEdge *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = metrics->TemperatureHotspot *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_MEM:
		*value = metrics->TemperatureHBM *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRGFX:
		*value = metrics->TemperatureVrGfx *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRSOC:
		*value = metrics->TemperatureVrSoc *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_VRMEM:
		*value = metrics->TemperatureVrMem *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_THROTTLER_STATUS:
		*value = metrics->ThrottlerStatus;
		break;
	case METRICS_CURR_FANSPEED:
		*value = metrics->CurrFanSpeed;
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	mutex_unlock(&smu->metrics_lock);

	return ret;
}

static int arcturus_get_current_clk_freq_by_table(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       uint32_t *value)
{
	MetricsMember_t member_type;
	int clk_id = 0;

	if (!value)
		return -EINVAL;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return -EINVAL;

	switch (clk_id) {
	case PPCLK_GFXCLK:
		/*
		 * CurrClock[clk_id] can provide accurate
		 *   output only when the dpm feature is enabled.
		 * We can use Average_* for dpm disabled case.
		 *   But this is available for gfxclk/uclk/socclk/vclk/dclk.
		 */
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT))
			member_type = METRICS_CURR_GFXCLK;
		else
			member_type = METRICS_AVERAGE_GFXCLK;
		break;
	case PPCLK_UCLK:
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT))
			member_type = METRICS_CURR_UCLK;
		else
			member_type = METRICS_AVERAGE_UCLK;
		break;
	case PPCLK_SOCCLK:
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT))
			member_type = METRICS_CURR_SOCCLK;
		else
			member_type = METRICS_AVERAGE_SOCCLK;
		break;
	case PPCLK_VCLK:
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT))
			member_type = METRICS_CURR_VCLK;
		else
			member_type = METRICS_AVERAGE_VCLK;
		break;
	case PPCLK_DCLK:
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT))
			member_type = METRICS_CURR_DCLK;
		else
			member_type = METRICS_AVERAGE_DCLK;
		break;
	case PPCLK_FCLK:
		member_type = METRICS_CURR_FCLK;
		break;
	default:
		return -EINVAL;
	}

	return arcturus_get_smu_metrics_data(smu,
					     member_type,
					     value);
}

static int arcturus_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type type, char *buf)
{
	int i, now, size = 0;
	int ret = 0;
	struct pp_clock_levels_with_latency clocks;
	struct smu_11_0_dpm_table *single_dpm_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_11_0_dpm_context *dpm_context = NULL;
	uint32_t gen_speed, lane_width;

	if (amdgpu_ras_intr_triggered())
		return snprintf(buf, PAGE_SIZE, "unavailable\n");

	dpm_context = smu_dpm->dpm_context;

	switch (type) {
	case SMU_SCLK:
		ret = arcturus_get_current_clk_freq_by_table(smu, SMU_GFXCLK, &now);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get current gfx clk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.gfx_table);
		ret = arcturus_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get gfx clk levels Failed!");
			return ret;
		}

		/*
		 * For DPM disabled case, there will be only one clock level.
		 * And it's safe to assume that is always the current clock.
		 */
		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n", i,
					clocks.data[i].clocks_in_khz / 1000,
					(clocks.num_levels == 1) ? "*" :
					(arcturus_freqs_in_same_level(
					clocks.data[i].clocks_in_khz / 1000,
					now) ? "*" : ""));
		break;

	case SMU_MCLK:
		ret = arcturus_get_current_clk_freq_by_table(smu, SMU_UCLK, &now);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get current mclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.uclk_table);
		ret = arcturus_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get memory clk levels Failed!");
			return ret;
		}

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.num_levels == 1) ? "*" :
				(arcturus_freqs_in_same_level(
				clocks.data[i].clocks_in_khz / 1000,
				now) ? "*" : ""));
		break;

	case SMU_SOCCLK:
		ret = arcturus_get_current_clk_freq_by_table(smu, SMU_SOCCLK, &now);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get current socclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.soc_table);
		ret = arcturus_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get socclk levels Failed!");
			return ret;
		}

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.num_levels == 1) ? "*" :
				(arcturus_freqs_in_same_level(
				clocks.data[i].clocks_in_khz / 1000,
				now) ? "*" : ""));
		break;

	case SMU_FCLK:
		ret = arcturus_get_current_clk_freq_by_table(smu, SMU_FCLK, &now);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get current fclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_context->dpm_tables.fclk_table);
		ret = arcturus_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			dev_err(smu->adev->dev, "Attempt to get fclk levels Failed!");
			return ret;
		}

		for (i = 0; i < single_dpm_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, single_dpm_table->dpm_levels[i].value,
				(clocks.num_levels == 1) ? "*" :
				(arcturus_freqs_in_same_level(
				clocks.data[i].clocks_in_khz / 1000,
				now) ? "*" : ""));
		break;

	case SMU_PCIE:
		gen_speed = smu_v11_0_get_current_pcie_link_speed_level(smu);
		lane_width = smu_v11_0_get_current_pcie_link_width_level(smu);
		size += sprintf(buf + size, "0: %s %s %dMhz *\n",
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
				smu->smu_table.boot_values.lclk / 100);
		break;

	default:
		break;
	}

	return size;
}

static int arcturus_upload_dpm_level(struct smu_context *smu,
				     bool max,
				     uint32_t feature_mask,
				     uint32_t level)
{
	struct smu_11_0_dpm_context *dpm_context =
			smu->smu_dpm.dpm_context;
	uint32_t freq;
	int ret = 0;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_GFXCLK_MASK)) {
		freq = dpm_context->dpm_tables.gfx_table.dpm_levels[level].value;
		ret = smu_cmn_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_GFXCLK << 16) | (freq & 0xffff),
			NULL);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to set soft %s gfxclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_UCLK_MASK)) {
		freq = dpm_context->dpm_tables.uclk_table.dpm_levels[level].value;
		ret = smu_cmn_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_UCLK << 16) | (freq & 0xffff),
			NULL);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to set soft %s memclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_SOCCLK_MASK)) {
		freq = dpm_context->dpm_tables.soc_table.dpm_levels[level].value;
		ret = smu_cmn_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_SOCCLK << 16) | (freq & 0xffff),
			NULL);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to set soft %s socclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	return ret;
}

static int arcturus_force_clk_levels(struct smu_context *smu,
			enum smu_clk_type type, uint32_t mask)
{
	struct smu_11_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_11_0_dpm_table *single_dpm_table = NULL;
	uint32_t soft_min_level, soft_max_level;
	uint32_t smu_version;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to get smu version!\n");
		return ret;
	}

	if ((smu_version >= 0x361200) &&
	    (smu_version <= 0x361a00)) {
		dev_err(smu->adev->dev, "Forcing clock level is not supported with "
		       "54.18 - 54.26(included) SMU firmwares\n");
		return -EOPNOTSUPP;
	}

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	switch (type) {
	case SMU_SCLK:
		single_dpm_table = &(dpm_context->dpm_tables.gfx_table);
		if (soft_max_level >= single_dpm_table->count) {
			dev_err(smu->adev->dev, "Clock level specified %d is over max allowed %d\n",
					soft_max_level, single_dpm_table->count - 1);
			ret = -EINVAL;
			break;
		}

		ret = arcturus_upload_dpm_level(smu,
						false,
						FEATURE_DPM_GFXCLK_MASK,
						soft_min_level);
		if (ret) {
			dev_err(smu->adev->dev, "Failed to upload boot level to lowest!\n");
			break;
		}

		ret = arcturus_upload_dpm_level(smu,
						true,
						FEATURE_DPM_GFXCLK_MASK,
						soft_max_level);
		if (ret)
			dev_err(smu->adev->dev, "Failed to upload dpm max level to highest!\n");

		break;

	case SMU_MCLK:
	case SMU_SOCCLK:
	case SMU_FCLK:
		/*
		 * Should not arrive here since Arcturus does not
		 * support mclk/socclk/fclk softmin/softmax settings
		 */
		ret = -EINVAL;
		break;

	default:
		break;
	}

	return ret;
}

static int arcturus_get_thermal_temperature_range(struct smu_context *smu,
						struct smu_temperature_range *range)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_powerplay_table *powerplay_table =
				table_context->power_play_table;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	if (!range)
		return -EINVAL;

	memcpy(range, &smu11_thermal_policy[0], sizeof(struct smu_temperature_range));

	range->max = pptable->TedgeLimit *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->edge_emergency_max = (pptable->TedgeLimit + CTF_OFFSET_EDGE) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_crit_max = pptable->ThotspotLimit *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_emergency_max = (pptable->ThotspotLimit + CTF_OFFSET_HOTSPOT) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_crit_max = pptable->TmemLimit *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_emergency_max = (pptable->TmemLimit + CTF_OFFSET_MEM)*
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->software_shutdown_temp = powerplay_table->software_shutdown_temp;

	return 0;
}

static int arcturus_read_sensor(struct smu_context *smu,
				enum amd_pp_sensors sensor,
				void *data, uint32_t *size)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	int ret = 0;

	if (amdgpu_ras_intr_triggered())
		return 0;

	if (!data || !size)
		return -EINVAL;

	mutex_lock(&smu->sensor_lock);
	switch (sensor) {
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*(uint32_t *)data = pptable->FanMaximumRpm;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		ret = arcturus_get_smu_metrics_data(smu,
						    METRICS_AVERAGE_MEMACTIVITY,
						    (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = arcturus_get_smu_metrics_data(smu,
						    METRICS_AVERAGE_GFXACTIVITY,
						    (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = arcturus_get_smu_metrics_data(smu,
						    METRICS_AVERAGE_SOCKETPOWER,
						    (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = arcturus_get_smu_metrics_data(smu,
						    METRICS_TEMPERATURE_HOTSPOT,
						    (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		ret = arcturus_get_smu_metrics_data(smu,
						    METRICS_TEMPERATURE_EDGE,
						    (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = arcturus_get_smu_metrics_data(smu,
						    METRICS_TEMPERATURE_MEM,
						    (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = arcturus_get_current_clk_freq_by_table(smu, SMU_UCLK, (uint32_t *)data);
		/* the output clock frequency in 10K unit */
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = arcturus_get_current_clk_freq_by_table(smu, SMU_GFXCLK, (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = smu_v11_0_get_gfx_vdd(smu, (uint32_t *)data);
		*size = 4;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&smu->sensor_lock);

	return ret;
}

static int arcturus_get_fan_speed_rpm(struct smu_context *smu,
				      uint32_t *speed)
{
	if (!speed)
		return -EINVAL;

	return arcturus_get_smu_metrics_data(smu,
					     METRICS_CURR_FANSPEED,
					     speed);
}

static int arcturus_get_fan_parameters(struct smu_context *smu)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	smu->fan_max_rpm = pptable->FanMaximumRpm;

	return 0;
}

static int arcturus_get_power_limit(struct smu_context *smu)
{
	struct smu_11_0_powerplay_table *powerplay_table =
		(struct smu_11_0_powerplay_table *)smu->smu_table.power_play_table;
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	uint32_t power_limit, od_percent;

	if (smu_v11_0_get_current_power_limit(smu, &power_limit)) {
		/* the last hope to figure out the ppt limit */
		if (!pptable) {
			dev_err(smu->adev->dev, "Cannot get PPT limit due to pptable missing!");
			return -EINVAL;
		}
		power_limit =
			pptable->SocketPowerLimitAc[PPT_THROTTLER_PPT0];
	}
	smu->current_power_limit = power_limit;

	if (smu->od_enabled) {
		od_percent = le32_to_cpu(powerplay_table->overdrive_table.max[SMU_11_0_ODSETTING_POWERPERCENTAGE]);

		dev_dbg(smu->adev->dev, "ODSETTING_POWERPERCENTAGE: %d (default: %d)\n", od_percent, power_limit);

		power_limit *= (100 + od_percent);
		power_limit /= 100;
	}
	smu->max_power_limit = power_limit;

	return 0;
}

static int arcturus_get_power_profile_mode(struct smu_context *smu,
					   char *buf)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	static const char *profile_name[] = {
					"BOOTUP_DEFAULT",
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
	uint32_t i, size = 0;
	int16_t workload_type = 0;
	int result = 0;
	uint32_t smu_version;

	if (!buf)
		return -EINVAL;

	result = smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (result)
		return result;

	if (smu_version >= 0x360d00)
		size += sprintf(buf + size, "%16s %s %s %s %s %s %s %s %s %s %s\n",
			title[0], title[1], title[2], title[3], title[4], title[5],
			title[6], title[7], title[8], title[9], title[10]);
	else
		size += sprintf(buf + size, "%16s\n",
			title[0]);

	for (i = 0; i <= PP_SMC_POWER_PROFILE_CUSTOM; i++) {
		/*
		 * Conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT
		 * Not all profile modes are supported on arcturus.
		 */
		workload_type = smu_cmn_to_asic_specific_index(smu,
							       CMN2ASIC_MAPPING_WORKLOAD,
							       i);
		if (workload_type < 0)
			continue;

		if (smu_version >= 0x360d00) {
			result = smu_cmn_update_table(smu,
						  SMU_TABLE_ACTIVITY_MONITOR_COEFF,
						  workload_type,
						  (void *)(&activity_monitor),
						  false);
			if (result) {
				dev_err(smu->adev->dev, "[%s] Failed to get activity monitor!", __func__);
				return result;
			}
		}

		size += sprintf(buf + size, "%2d %14s%s\n",
			i, profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");

		if (smu_version >= 0x360d00) {
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
		}
	}

	return size;
}

static int arcturus_set_power_profile_mode(struct smu_context *smu,
					   long *input,
					   uint32_t size)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	int workload_type = 0;
	uint32_t profile_mode = input[size];
	int ret = 0;
	uint32_t smu_version;

	if (profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		dev_err(smu->adev->dev, "Invalid power profile mode %d\n", profile_mode);
		return -EINVAL;
	}

	ret = smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (ret)
		return ret;

	if ((profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) &&
	     (smu_version >=0x360d00)) {
		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF,
				       WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor),
				       false);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] Failed to get activity monitor!", __func__);
			return ret;
		}

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
		case 1: /* Uclk */
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
		}

		ret = smu_cmn_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF,
				       WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor),
				       true);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] Failed to set activity monitor!", __func__);
			return ret;
		}
	}

	/*
	 * Conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT
	 * Not all profile modes are supported on arcturus.
	 */
	workload_type = smu_cmn_to_asic_specific_index(smu,
						       CMN2ASIC_MAPPING_WORKLOAD,
						       profile_mode);
	if (workload_type < 0) {
		dev_err(smu->adev->dev, "Unsupported power profile mode %d on arcturus\n", profile_mode);
		return -EINVAL;
	}

	ret = smu_cmn_send_smc_msg_with_param(smu,
					  SMU_MSG_SetWorkloadMask,
					  1 << workload_type,
					  NULL);
	if (ret) {
		dev_err(smu->adev->dev, "Fail to set workload type %d\n", workload_type);
		return ret;
	}

	smu->power_profile_mode = profile_mode;

	return 0;
}

static int arcturus_set_performance_level(struct smu_context *smu,
					  enum amd_dpm_forced_level level)
{
	uint32_t smu_version;
	int ret;

	ret = smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to get smu version!\n");
		return ret;
	}

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
	case AMD_DPM_FORCED_LEVEL_LOW:
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		if ((smu_version >= 0x361200) &&
		    (smu_version <= 0x361a00)) {
			dev_err(smu->adev->dev, "Forcing clock level is not supported with "
			       "54.18 - 54.26(included) SMU firmwares\n");
			return -EOPNOTSUPP;
		}
		break;
	default:
		break;
	}

	return smu_v11_0_set_performance_level(smu, level);
}

static void arcturus_dump_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	int i;

	dev_info(smu->adev->dev, "Dumped PPTable:\n");

	dev_info(smu->adev->dev, "Version = 0x%08x\n", pptable->Version);

	dev_info(smu->adev->dev, "FeaturesToRun[0] = 0x%08x\n", pptable->FeaturesToRun[0]);
	dev_info(smu->adev->dev, "FeaturesToRun[1] = 0x%08x\n", pptable->FeaturesToRun[1]);

	for (i = 0; i < PPT_THROTTLER_COUNT; i++) {
		dev_info(smu->adev->dev, "SocketPowerLimitAc[%d] = %d\n", i, pptable->SocketPowerLimitAc[i]);
		dev_info(smu->adev->dev, "SocketPowerLimitAcTau[%d] = %d\n", i, pptable->SocketPowerLimitAcTau[i]);
	}

	dev_info(smu->adev->dev, "TdcLimitSoc = %d\n", pptable->TdcLimitSoc);
	dev_info(smu->adev->dev, "TdcLimitSocTau = %d\n", pptable->TdcLimitSocTau);
	dev_info(smu->adev->dev, "TdcLimitGfx = %d\n", pptable->TdcLimitGfx);
	dev_info(smu->adev->dev, "TdcLimitGfxTau = %d\n", pptable->TdcLimitGfxTau);

	dev_info(smu->adev->dev, "TedgeLimit = %d\n", pptable->TedgeLimit);
	dev_info(smu->adev->dev, "ThotspotLimit = %d\n", pptable->ThotspotLimit);
	dev_info(smu->adev->dev, "TmemLimit = %d\n", pptable->TmemLimit);
	dev_info(smu->adev->dev, "Tvr_gfxLimit = %d\n", pptable->Tvr_gfxLimit);
	dev_info(smu->adev->dev, "Tvr_memLimit = %d\n", pptable->Tvr_memLimit);
	dev_info(smu->adev->dev, "Tvr_socLimit = %d\n", pptable->Tvr_socLimit);
	dev_info(smu->adev->dev, "FitLimit = %d\n", pptable->FitLimit);

	dev_info(smu->adev->dev, "PpmPowerLimit = %d\n", pptable->PpmPowerLimit);
	dev_info(smu->adev->dev, "PpmTemperatureThreshold = %d\n", pptable->PpmTemperatureThreshold);

	dev_info(smu->adev->dev, "ThrottlerControlMask = %d\n", pptable->ThrottlerControlMask);

	dev_info(smu->adev->dev, "UlvVoltageOffsetGfx = %d\n", pptable->UlvVoltageOffsetGfx);
	dev_info(smu->adev->dev, "UlvPadding = 0x%08x\n", pptable->UlvPadding);

	dev_info(smu->adev->dev, "UlvGfxclkBypass = %d\n", pptable->UlvGfxclkBypass);
	dev_info(smu->adev->dev, "Padding234[0] = 0x%02x\n", pptable->Padding234[0]);
	dev_info(smu->adev->dev, "Padding234[1] = 0x%02x\n", pptable->Padding234[1]);
	dev_info(smu->adev->dev, "Padding234[2] = 0x%02x\n", pptable->Padding234[2]);

	dev_info(smu->adev->dev, "MinVoltageGfx = %d\n", pptable->MinVoltageGfx);
	dev_info(smu->adev->dev, "MinVoltageSoc = %d\n", pptable->MinVoltageSoc);
	dev_info(smu->adev->dev, "MaxVoltageGfx = %d\n", pptable->MaxVoltageGfx);
	dev_info(smu->adev->dev, "MaxVoltageSoc = %d\n", pptable->MaxVoltageSoc);

	dev_info(smu->adev->dev, "LoadLineResistanceGfx = %d\n", pptable->LoadLineResistanceGfx);
	dev_info(smu->adev->dev, "LoadLineResistanceSoc = %d\n", pptable->LoadLineResistanceSoc);

	dev_info(smu->adev->dev, "[PPCLK_GFXCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_GFXCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_GFXCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_GFXCLK].padding,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_GFXCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_GFXCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_GFXCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_VCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_VCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_VCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_VCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_VCLK].padding,
			pptable->DpmDescriptor[PPCLK_VCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_VCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_VCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_VCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_VCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_VCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_VCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_DCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_DCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_DCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_DCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_DCLK].padding,
			pptable->DpmDescriptor[PPCLK_DCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_DCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_DCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_DCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_DCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_DCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_DCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_SOCCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_SOCCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_SOCCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_SOCCLK].padding,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_SOCCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_SOCCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_SOCCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_UCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_UCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_UCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_UCLK].padding,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_UCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_UCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_UCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_UCLK].Padding16);

	dev_info(smu->adev->dev, "[PPCLK_FCLK]\n"
			"  .VoltageMode          = 0x%02x\n"
			"  .SnapToDiscrete       = 0x%02x\n"
			"  .NumDiscreteLevels    = 0x%02x\n"
			"  .padding              = 0x%02x\n"
			"  .ConversionToAvfsClk{m = 0x%08x b = 0x%08x}\n"
			"  .SsCurve            {a = 0x%08x b = 0x%08x c = 0x%08x}\n"
			"  .SsFmin               = 0x%04x\n"
			"  .Padding_16           = 0x%04x\n",
			pptable->DpmDescriptor[PPCLK_FCLK].VoltageMode,
			pptable->DpmDescriptor[PPCLK_FCLK].SnapToDiscrete,
			pptable->DpmDescriptor[PPCLK_FCLK].NumDiscreteLevels,
			pptable->DpmDescriptor[PPCLK_FCLK].padding,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.m,
			pptable->DpmDescriptor[PPCLK_FCLK].ConversionToAvfsClk.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.a,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.b,
			pptable->DpmDescriptor[PPCLK_FCLK].SsCurve.c,
			pptable->DpmDescriptor[PPCLK_FCLK].SsFmin,
			pptable->DpmDescriptor[PPCLK_FCLK].Padding16);


	dev_info(smu->adev->dev, "FreqTableGfx\n");
	for (i = 0; i < NUM_GFXCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = %d\n", i, pptable->FreqTableGfx[i]);

	dev_info(smu->adev->dev, "FreqTableVclk\n");
	for (i = 0; i < NUM_VCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = %d\n", i, pptable->FreqTableVclk[i]);

	dev_info(smu->adev->dev, "FreqTableDclk\n");
	for (i = 0; i < NUM_DCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = %d\n", i, pptable->FreqTableDclk[i]);

	dev_info(smu->adev->dev, "FreqTableSocclk\n");
	for (i = 0; i < NUM_SOCCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = %d\n", i, pptable->FreqTableSocclk[i]);

	dev_info(smu->adev->dev, "FreqTableUclk\n");
	for (i = 0; i < NUM_UCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = %d\n", i, pptable->FreqTableUclk[i]);

	dev_info(smu->adev->dev, "FreqTableFclk\n");
	for (i = 0; i < NUM_FCLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%02d] = %d\n", i, pptable->FreqTableFclk[i]);

	dev_info(smu->adev->dev, "Mp0clkFreq\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = %d\n", i, pptable->Mp0clkFreq[i]);

	dev_info(smu->adev->dev, "Mp0DpmVoltage\n");
	for (i = 0; i < NUM_MP0CLK_DPM_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = %d\n", i, pptable->Mp0DpmVoltage[i]);

	dev_info(smu->adev->dev, "GfxclkFidle = 0x%x\n", pptable->GfxclkFidle);
	dev_info(smu->adev->dev, "GfxclkSlewRate = 0x%x\n", pptable->GfxclkSlewRate);
	dev_info(smu->adev->dev, "Padding567[0] = 0x%x\n", pptable->Padding567[0]);
	dev_info(smu->adev->dev, "Padding567[1] = 0x%x\n", pptable->Padding567[1]);
	dev_info(smu->adev->dev, "Padding567[2] = 0x%x\n", pptable->Padding567[2]);
	dev_info(smu->adev->dev, "Padding567[3] = 0x%x\n", pptable->Padding567[3]);
	dev_info(smu->adev->dev, "GfxclkDsMaxFreq = %d\n", pptable->GfxclkDsMaxFreq);
	dev_info(smu->adev->dev, "GfxclkSource = 0x%x\n", pptable->GfxclkSource);
	dev_info(smu->adev->dev, "Padding456 = 0x%x\n", pptable->Padding456);

	dev_info(smu->adev->dev, "EnableTdpm = %d\n", pptable->EnableTdpm);
	dev_info(smu->adev->dev, "TdpmHighHystTemperature = %d\n", pptable->TdpmHighHystTemperature);
	dev_info(smu->adev->dev, "TdpmLowHystTemperature = %d\n", pptable->TdpmLowHystTemperature);
	dev_info(smu->adev->dev, "GfxclkFreqHighTempLimit = %d\n", pptable->GfxclkFreqHighTempLimit);

	dev_info(smu->adev->dev, "FanStopTemp = %d\n", pptable->FanStopTemp);
	dev_info(smu->adev->dev, "FanStartTemp = %d\n", pptable->FanStartTemp);

	dev_info(smu->adev->dev, "FanGainEdge = %d\n", pptable->FanGainEdge);
	dev_info(smu->adev->dev, "FanGainHotspot = %d\n", pptable->FanGainHotspot);
	dev_info(smu->adev->dev, "FanGainVrGfx = %d\n", pptable->FanGainVrGfx);
	dev_info(smu->adev->dev, "FanGainVrSoc = %d\n", pptable->FanGainVrSoc);
	dev_info(smu->adev->dev, "FanGainVrMem = %d\n", pptable->FanGainVrMem);
	dev_info(smu->adev->dev, "FanGainHbm = %d\n", pptable->FanGainHbm);

	dev_info(smu->adev->dev, "FanPwmMin = %d\n", pptable->FanPwmMin);
	dev_info(smu->adev->dev, "FanAcousticLimitRpm = %d\n", pptable->FanAcousticLimitRpm);
	dev_info(smu->adev->dev, "FanThrottlingRpm = %d\n", pptable->FanThrottlingRpm);
	dev_info(smu->adev->dev, "FanMaximumRpm = %d\n", pptable->FanMaximumRpm);
	dev_info(smu->adev->dev, "FanTargetTemperature = %d\n", pptable->FanTargetTemperature);
	dev_info(smu->adev->dev, "FanTargetGfxclk = %d\n", pptable->FanTargetGfxclk);
	dev_info(smu->adev->dev, "FanZeroRpmEnable = %d\n", pptable->FanZeroRpmEnable);
	dev_info(smu->adev->dev, "FanTachEdgePerRev = %d\n", pptable->FanTachEdgePerRev);
	dev_info(smu->adev->dev, "FanTempInputSelect = %d\n", pptable->FanTempInputSelect);

	dev_info(smu->adev->dev, "FuzzyFan_ErrorSetDelta = %d\n", pptable->FuzzyFan_ErrorSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_ErrorRateSetDelta = %d\n", pptable->FuzzyFan_ErrorRateSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_PwmSetDelta = %d\n", pptable->FuzzyFan_PwmSetDelta);
	dev_info(smu->adev->dev, "FuzzyFan_Reserved = %d\n", pptable->FuzzyFan_Reserved);

	dev_info(smu->adev->dev, "OverrideAvfsGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "OverrideAvfsGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->OverrideAvfsGb[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "Padding8_Avfs[0] = %d\n", pptable->Padding8_Avfs[0]);
	dev_info(smu->adev->dev, "Padding8_Avfs[1] = %d\n", pptable->Padding8_Avfs[1]);

	dev_info(smu->adev->dev, "dBtcGbGfxPll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxPll.a,
			pptable->dBtcGbGfxPll.b,
			pptable->dBtcGbGfxPll.c);
	dev_info(smu->adev->dev, "dBtcGbGfxAfll{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbGfxAfll.a,
			pptable->dBtcGbGfxAfll.b,
			pptable->dBtcGbGfxAfll.c);
	dev_info(smu->adev->dev, "dBtcGbSoc{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->dBtcGbSoc.a,
			pptable->dBtcGbSoc.b,
			pptable->dBtcGbSoc.c);

	dev_info(smu->adev->dev, "qAgingGb[AVFS_VOLTAGE_GFX]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].m,
			pptable->qAgingGb[AVFS_VOLTAGE_GFX].b);
	dev_info(smu->adev->dev, "qAgingGb[AVFS_VOLTAGE_SOC]{m = 0x%x b = 0x%x}\n",
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].m,
			pptable->qAgingGb[AVFS_VOLTAGE_SOC].b);

	dev_info(smu->adev->dev, "qStaticVoltageOffset[AVFS_VOLTAGE_GFX]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_GFX].c);
	dev_info(smu->adev->dev, "qStaticVoltageOffset[AVFS_VOLTAGE_SOC]{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].a,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].b,
			pptable->qStaticVoltageOffset[AVFS_VOLTAGE_SOC].c);

	dev_info(smu->adev->dev, "DcTol[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcTol[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcTol[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "DcBtcEnabled[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcEnabled[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcEnabled[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "Padding8_GfxBtc[0] = 0x%x\n", pptable->Padding8_GfxBtc[0]);
	dev_info(smu->adev->dev, "Padding8_GfxBtc[1] = 0x%x\n", pptable->Padding8_GfxBtc[1]);

	dev_info(smu->adev->dev, "DcBtcMin[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcMin[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMin[AVFS_VOLTAGE_SOC]);
	dev_info(smu->adev->dev, "DcBtcMax[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcMax[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcMax[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "DcBtcGb[AVFS_VOLTAGE_GFX] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_GFX]);
	dev_info(smu->adev->dev, "DcBtcGb[AVFS_VOLTAGE_SOC] = 0x%x\n", pptable->DcBtcGb[AVFS_VOLTAGE_SOC]);

	dev_info(smu->adev->dev, "XgmiDpmPstates\n");
	for (i = 0; i < NUM_XGMI_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = %d\n", i, pptable->XgmiDpmPstates[i]);
	dev_info(smu->adev->dev, "XgmiDpmSpare[0] = 0x%02x\n", pptable->XgmiDpmSpare[0]);
	dev_info(smu->adev->dev, "XgmiDpmSpare[1] = 0x%02x\n", pptable->XgmiDpmSpare[1]);

	dev_info(smu->adev->dev, "VDDGFX_TVmin = %d\n", pptable->VDDGFX_TVmin);
	dev_info(smu->adev->dev, "VDDSOC_TVmin = %d\n", pptable->VDDSOC_TVmin);
	dev_info(smu->adev->dev, "VDDGFX_Vmin_HiTemp = %d\n", pptable->VDDGFX_Vmin_HiTemp);
	dev_info(smu->adev->dev, "VDDGFX_Vmin_LoTemp = %d\n", pptable->VDDGFX_Vmin_LoTemp);
	dev_info(smu->adev->dev, "VDDSOC_Vmin_HiTemp = %d\n", pptable->VDDSOC_Vmin_HiTemp);
	dev_info(smu->adev->dev, "VDDSOC_Vmin_LoTemp = %d\n", pptable->VDDSOC_Vmin_LoTemp);
	dev_info(smu->adev->dev, "VDDGFX_TVminHystersis = %d\n", pptable->VDDGFX_TVminHystersis);
	dev_info(smu->adev->dev, "VDDSOC_TVminHystersis = %d\n", pptable->VDDSOC_TVminHystersis);

	dev_info(smu->adev->dev, "DebugOverrides = 0x%x\n", pptable->DebugOverrides);
	dev_info(smu->adev->dev, "ReservedEquation0{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation0.a,
			pptable->ReservedEquation0.b,
			pptable->ReservedEquation0.c);
	dev_info(smu->adev->dev, "ReservedEquation1{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation1.a,
			pptable->ReservedEquation1.b,
			pptable->ReservedEquation1.c);
	dev_info(smu->adev->dev, "ReservedEquation2{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation2.a,
			pptable->ReservedEquation2.b,
			pptable->ReservedEquation2.c);
	dev_info(smu->adev->dev, "ReservedEquation3{a = 0x%x b = 0x%x c = 0x%x}\n",
			pptable->ReservedEquation3.a,
			pptable->ReservedEquation3.b,
			pptable->ReservedEquation3.c);

	dev_info(smu->adev->dev, "MinVoltageUlvGfx = %d\n", pptable->MinVoltageUlvGfx);
	dev_info(smu->adev->dev, "PaddingUlv = %d\n", pptable->PaddingUlv);

	dev_info(smu->adev->dev, "TotalPowerConfig = %d\n", pptable->TotalPowerConfig);
	dev_info(smu->adev->dev, "TotalPowerSpare1 = %d\n", pptable->TotalPowerSpare1);
	dev_info(smu->adev->dev, "TotalPowerSpare2 = %d\n", pptable->TotalPowerSpare2);

	dev_info(smu->adev->dev, "PccThresholdLow = %d\n", pptable->PccThresholdLow);
	dev_info(smu->adev->dev, "PccThresholdHigh = %d\n", pptable->PccThresholdHigh);

	dev_info(smu->adev->dev, "Board Parameters:\n");
	dev_info(smu->adev->dev, "MaxVoltageStepGfx = 0x%x\n", pptable->MaxVoltageStepGfx);
	dev_info(smu->adev->dev, "MaxVoltageStepSoc = 0x%x\n", pptable->MaxVoltageStepSoc);

	dev_info(smu->adev->dev, "VddGfxVrMapping = 0x%x\n", pptable->VddGfxVrMapping);
	dev_info(smu->adev->dev, "VddSocVrMapping = 0x%x\n", pptable->VddSocVrMapping);
	dev_info(smu->adev->dev, "VddMemVrMapping = 0x%x\n", pptable->VddMemVrMapping);
	dev_info(smu->adev->dev, "BoardVrMapping = 0x%x\n", pptable->BoardVrMapping);

	dev_info(smu->adev->dev, "GfxUlvPhaseSheddingMask = 0x%x\n", pptable->GfxUlvPhaseSheddingMask);
	dev_info(smu->adev->dev, "ExternalSensorPresent = 0x%x\n", pptable->ExternalSensorPresent);

	dev_info(smu->adev->dev, "GfxMaxCurrent = 0x%x\n", pptable->GfxMaxCurrent);
	dev_info(smu->adev->dev, "GfxOffset = 0x%x\n", pptable->GfxOffset);
	dev_info(smu->adev->dev, "Padding_TelemetryGfx = 0x%x\n", pptable->Padding_TelemetryGfx);

	dev_info(smu->adev->dev, "SocMaxCurrent = 0x%x\n", pptable->SocMaxCurrent);
	dev_info(smu->adev->dev, "SocOffset = 0x%x\n", pptable->SocOffset);
	dev_info(smu->adev->dev, "Padding_TelemetrySoc = 0x%x\n", pptable->Padding_TelemetrySoc);

	dev_info(smu->adev->dev, "MemMaxCurrent = 0x%x\n", pptable->MemMaxCurrent);
	dev_info(smu->adev->dev, "MemOffset = 0x%x\n", pptable->MemOffset);
	dev_info(smu->adev->dev, "Padding_TelemetryMem = 0x%x\n", pptable->Padding_TelemetryMem);

	dev_info(smu->adev->dev, "BoardMaxCurrent = 0x%x\n", pptable->BoardMaxCurrent);
	dev_info(smu->adev->dev, "BoardOffset = 0x%x\n", pptable->BoardOffset);
	dev_info(smu->adev->dev, "Padding_TelemetryBoardInput = 0x%x\n", pptable->Padding_TelemetryBoardInput);

	dev_info(smu->adev->dev, "VR0HotGpio = %d\n", pptable->VR0HotGpio);
	dev_info(smu->adev->dev, "VR0HotPolarity = %d\n", pptable->VR0HotPolarity);
	dev_info(smu->adev->dev, "VR1HotGpio = %d\n", pptable->VR1HotGpio);
	dev_info(smu->adev->dev, "VR1HotPolarity = %d\n", pptable->VR1HotPolarity);

	dev_info(smu->adev->dev, "PllGfxclkSpreadEnabled = %d\n", pptable->PllGfxclkSpreadEnabled);
	dev_info(smu->adev->dev, "PllGfxclkSpreadPercent = %d\n", pptable->PllGfxclkSpreadPercent);
	dev_info(smu->adev->dev, "PllGfxclkSpreadFreq = %d\n", pptable->PllGfxclkSpreadFreq);

	dev_info(smu->adev->dev, "UclkSpreadEnabled = %d\n", pptable->UclkSpreadEnabled);
	dev_info(smu->adev->dev, "UclkSpreadPercent = %d\n", pptable->UclkSpreadPercent);
	dev_info(smu->adev->dev, "UclkSpreadFreq = %d\n", pptable->UclkSpreadFreq);

	dev_info(smu->adev->dev, "FclkSpreadEnabled = %d\n", pptable->FclkSpreadEnabled);
	dev_info(smu->adev->dev, "FclkSpreadPercent = %d\n", pptable->FclkSpreadPercent);
	dev_info(smu->adev->dev, "FclkSpreadFreq = %d\n", pptable->FclkSpreadFreq);

	dev_info(smu->adev->dev, "FllGfxclkSpreadEnabled = %d\n", pptable->FllGfxclkSpreadEnabled);
	dev_info(smu->adev->dev, "FllGfxclkSpreadPercent = %d\n", pptable->FllGfxclkSpreadPercent);
	dev_info(smu->adev->dev, "FllGfxclkSpreadFreq = %d\n", pptable->FllGfxclkSpreadFreq);

	for (i = 0; i < NUM_I2C_CONTROLLERS; i++) {
		dev_info(smu->adev->dev, "I2cControllers[%d]:\n", i);
		dev_info(smu->adev->dev, "                   .Enabled = %d\n",
				pptable->I2cControllers[i].Enabled);
		dev_info(smu->adev->dev, "                   .SlaveAddress = 0x%x\n",
				pptable->I2cControllers[i].SlaveAddress);
		dev_info(smu->adev->dev, "                   .ControllerPort = %d\n",
				pptable->I2cControllers[i].ControllerPort);
		dev_info(smu->adev->dev, "                   .ControllerName = %d\n",
				pptable->I2cControllers[i].ControllerName);
		dev_info(smu->adev->dev, "                   .ThermalThrottler = %d\n",
				pptable->I2cControllers[i].ThermalThrotter);
		dev_info(smu->adev->dev, "                   .I2cProtocol = %d\n",
				pptable->I2cControllers[i].I2cProtocol);
		dev_info(smu->adev->dev, "                   .Speed = %d\n",
				pptable->I2cControllers[i].Speed);
	}

	dev_info(smu->adev->dev, "MemoryChannelEnabled = %d\n", pptable->MemoryChannelEnabled);
	dev_info(smu->adev->dev, "DramBitWidth = %d\n", pptable->DramBitWidth);

	dev_info(smu->adev->dev, "TotalBoardPower = %d\n", pptable->TotalBoardPower);

	dev_info(smu->adev->dev, "XgmiLinkSpeed\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = %d\n", i, pptable->XgmiLinkSpeed[i]);
	dev_info(smu->adev->dev, "XgmiLinkWidth\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = %d\n", i, pptable->XgmiLinkWidth[i]);
	dev_info(smu->adev->dev, "XgmiFclkFreq\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = %d\n", i, pptable->XgmiFclkFreq[i]);
	dev_info(smu->adev->dev, "XgmiSocVoltage\n");
	for (i = 0; i < NUM_XGMI_PSTATE_LEVELS; i++)
		dev_info(smu->adev->dev, "  .[%d] = %d\n", i, pptable->XgmiSocVoltage[i]);

}

static bool arcturus_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint32_t feature_mask[2];
	uint64_t feature_enabled;

	ret = smu_cmn_get_enabled_mask(smu, feature_mask, 2);
	if (ret)
		return false;

	feature_enabled = (uint64_t)feature_mask[1] << 32 | feature_mask[0];

	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int arcturus_dpm_set_vcn_enable(struct smu_context *smu, bool enable)
{
	int ret = 0;

	if (enable) {
		if (!smu_cmn_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_VCN_PG_BIT, 1);
			if (ret) {
				dev_err(smu->adev->dev, "[EnableVCNDPM] failed!\n");
				return ret;
			}
		}
	} else {
		if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_VCN_PG_BIT, 0);
			if (ret) {
				dev_err(smu->adev->dev, "[DisableVCNDPM] failed!\n");
				return ret;
			}
		}
	}

	return ret;
}

static void arcturus_fill_i2c_req(SwI2cRequest_t  *req, bool write,
				  uint8_t address, uint32_t numbytes,
				  uint8_t *data)
{
	int i;

	req->I2CcontrollerPort = 0;
	req->I2CSpeed = 2;
	req->SlaveAddress = address;
	req->NumCmds = numbytes;

	for (i = 0; i < numbytes; i++) {
		SwI2cCmd_t *cmd =  &req->SwI2cCmds[i];

		/* First 2 bytes are always write for lower 2b EEPROM address */
		if (i < 2)
			cmd->Cmd = 1;
		else
			cmd->Cmd = write;


		/* Add RESTART for read  after address filled */
		cmd->CmdConfig |= (i == 2 && !write) ? CMDCONFIG_RESTART_MASK : 0;

		/* Add STOP in the end */
		cmd->CmdConfig |= (i == (numbytes - 1)) ? CMDCONFIG_STOP_MASK : 0;

		/* Fill with data regardless if read or write to simplify code */
		cmd->RegisterAddr = data[i];
	}
}

static int arcturus_i2c_read_data(struct i2c_adapter *control,
					       uint8_t address,
					       uint8_t *data,
					       uint32_t numbytes)
{
	uint32_t  i, ret = 0;
	SwI2cRequest_t req;
	struct amdgpu_device *adev = to_amdgpu_device(control);
	struct smu_table_context *smu_table = &adev->smu.smu_table;
	struct smu_table *table = &smu_table->driver_table;

	if (numbytes > MAX_SW_I2C_COMMANDS) {
		dev_err(adev->dev, "numbytes requested %d is over max allowed %d\n",
			numbytes, MAX_SW_I2C_COMMANDS);
		return -EINVAL;
	}

	memset(&req, 0, sizeof(req));
	arcturus_fill_i2c_req(&req, false, address, numbytes, data);

	mutex_lock(&adev->smu.mutex);
	/* Now read data starting with that address */
	ret = smu_cmn_update_table(&adev->smu, SMU_TABLE_I2C_COMMANDS, 0, &req,
					true);
	mutex_unlock(&adev->smu.mutex);

	if (!ret) {
		SwI2cRequest_t *res = (SwI2cRequest_t *)table->cpu_addr;

		/* Assume SMU  fills res.SwI2cCmds[i].Data with read bytes */
		for (i = 0; i < numbytes; i++)
			data[i] = res->SwI2cCmds[i].Data;

		dev_dbg(adev->dev, "arcturus_i2c_read_data, address = %x, bytes = %d, data :",
				  (uint16_t)address, numbytes);

		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE,
			       8, 1, data, numbytes, false);
	} else
		dev_err(adev->dev, "arcturus_i2c_read_data - error occurred :%x", ret);

	return ret;
}

static int arcturus_i2c_write_data(struct i2c_adapter *control,
						uint8_t address,
						uint8_t *data,
						uint32_t numbytes)
{
	uint32_t ret;
	SwI2cRequest_t req;
	struct amdgpu_device *adev = to_amdgpu_device(control);

	if (numbytes > MAX_SW_I2C_COMMANDS) {
		dev_err(adev->dev, "numbytes requested %d is over max allowed %d\n",
			numbytes, MAX_SW_I2C_COMMANDS);
		return -EINVAL;
	}

	memset(&req, 0, sizeof(req));
	arcturus_fill_i2c_req(&req, true, address, numbytes, data);

	mutex_lock(&adev->smu.mutex);
	ret = smu_cmn_update_table(&adev->smu, SMU_TABLE_I2C_COMMANDS, 0, &req, true);
	mutex_unlock(&adev->smu.mutex);

	if (!ret) {
		dev_dbg(adev->dev, "arcturus_i2c_write(), address = %x, bytes = %d , data: ",
					 (uint16_t)address, numbytes);

		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_NONE,
			       8, 1, data, numbytes, false);
		/*
		 * According to EEPROM spec there is a MAX of 10 ms required for
		 * EEPROM to flush internal RX buffer after STOP was issued at the
		 * end of write transaction. During this time the EEPROM will not be
		 * responsive to any more commands - so wait a bit more.
		 */
		msleep(10);

	} else
		dev_err(adev->dev, "arcturus_i2c_write- error occurred :%x", ret);

	return ret;
}

static int arcturus_i2c_xfer(struct i2c_adapter *i2c_adap,
			      struct i2c_msg *msgs, int num)
{
	uint32_t  i, j, ret, data_size, data_chunk_size, next_eeprom_addr = 0;
	uint8_t *data_ptr, data_chunk[MAX_SW_I2C_COMMANDS] = { 0 };

	for (i = 0; i < num; i++) {
		/*
		 * SMU interface allows at most MAX_SW_I2C_COMMANDS bytes of data at
		 * once and hence the data needs to be spliced into chunks and sent each
		 * chunk separately
		 */
		data_size = msgs[i].len - 2;
		data_chunk_size = MAX_SW_I2C_COMMANDS - 2;
		next_eeprom_addr = (msgs[i].buf[0] << 8 & 0xff00) | (msgs[i].buf[1] & 0xff);
		data_ptr = msgs[i].buf + 2;

		for (j = 0; j < data_size / data_chunk_size; j++) {
			/* Insert the EEPROM dest addess, bits 0-15 */
			data_chunk[0] = ((next_eeprom_addr >> 8) & 0xff);
			data_chunk[1] = (next_eeprom_addr & 0xff);

			if (msgs[i].flags & I2C_M_RD) {
				ret = arcturus_i2c_read_data(i2c_adap,
							     (uint8_t)msgs[i].addr,
							     data_chunk, MAX_SW_I2C_COMMANDS);

				memcpy(data_ptr, data_chunk + 2, data_chunk_size);
			} else {

				memcpy(data_chunk + 2, data_ptr, data_chunk_size);

				ret = arcturus_i2c_write_data(i2c_adap,
							      (uint8_t)msgs[i].addr,
							      data_chunk, MAX_SW_I2C_COMMANDS);
			}

			if (ret) {
				num = -EIO;
				goto fail;
			}

			next_eeprom_addr += data_chunk_size;
			data_ptr += data_chunk_size;
		}

		if (data_size % data_chunk_size) {
			data_chunk[0] = ((next_eeprom_addr >> 8) & 0xff);
			data_chunk[1] = (next_eeprom_addr & 0xff);

			if (msgs[i].flags & I2C_M_RD) {
				ret = arcturus_i2c_read_data(i2c_adap,
							     (uint8_t)msgs[i].addr,
							     data_chunk, (data_size % data_chunk_size) + 2);

				memcpy(data_ptr, data_chunk + 2, data_size % data_chunk_size);
			} else {
				memcpy(data_chunk + 2, data_ptr, data_size % data_chunk_size);

				ret = arcturus_i2c_write_data(i2c_adap,
							      (uint8_t)msgs[i].addr,
							      data_chunk, (data_size % data_chunk_size) + 2);
			}

			if (ret) {
				num = -EIO;
				goto fail;
			}
		}
	}

fail:
	return num;
}

static u32 arcturus_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}


static const struct i2c_algorithm arcturus_i2c_algo = {
	.master_xfer = arcturus_i2c_xfer,
	.functionality = arcturus_i2c_func,
};

static int arcturus_i2c_control_init(struct smu_context *smu, struct i2c_adapter *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	int res;

	control->owner = THIS_MODULE;
	control->class = I2C_CLASS_SPD;
	control->dev.parent = &adev->pdev->dev;
	control->algo = &arcturus_i2c_algo;
	snprintf(control->name, sizeof(control->name), "AMDGPU SMU");

	res = i2c_add_adapter(control);
	if (res)
		DRM_ERROR("Failed to register hw i2c, err: %d\n", res);

	return res;
}

static void arcturus_i2c_control_fini(struct smu_context *smu, struct i2c_adapter *control)
{
	i2c_del_adapter(control);
}

static void arcturus_get_unique_id(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t top32 = 0, bottom32 = 0, smu_version;
	uint64_t id;

	if (smu_cmn_get_smc_version(smu, NULL, &smu_version)) {
		dev_warn(adev->dev, "Failed to get smu version, cannot get unique_id or serial_number\n");
		return;
	}

	/* PPSMC_MSG_ReadSerial* is supported by 54.23.0 and onwards */
	if (smu_version < 0x361700) {
		dev_warn(adev->dev, "ReadSerial is only supported by PMFW 54.23.0 and onwards\n");
		return;
	}

	/* Get the SN to turn into a Unique ID */
	smu_cmn_send_smc_msg(smu, SMU_MSG_ReadSerialNumTop32, &top32);
	smu_cmn_send_smc_msg(smu, SMU_MSG_ReadSerialNumBottom32, &bottom32);

	id = ((uint64_t)bottom32 << 32) | top32;
	adev->unique_id = id;
	/* For Arcturus-and-later, unique_id == serial_number, so convert it to a
	 * 16-digit HEX string for convenience and backwards-compatibility
	 */
	sprintf(adev->serial, "%llx", id);
}

static bool arcturus_is_baco_supported(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t val;

	if (!smu_v11_0_baco_is_support(smu) || amdgpu_sriov_vf(adev))
		return false;

	val = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP0);
	return (val & RCC_BIF_STRAP0__STRAP_PX_CAPABLE_MASK) ? true : false;
}

static int arcturus_set_df_cstate(struct smu_context *smu,
				  enum pp_df_cstate state)
{
	uint32_t smu_version;
	int ret;

	ret = smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to get smu version!\n");
		return ret;
	}

	/* PPSMC_MSG_DFCstateControl is supported by 54.15.0 and onwards */
	if (smu_version < 0x360F00) {
		dev_err(smu->adev->dev, "DFCstateControl is only supported by PMFW 54.15.0 and onwards\n");
		return -EINVAL;
	}

	return smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_DFCstateControl, state, NULL);
}

static int arcturus_allow_xgmi_power_down(struct smu_context *smu, bool en)
{
	uint32_t smu_version;
	int ret;

	ret = smu_cmn_get_smc_version(smu, NULL, &smu_version);
	if (ret) {
		dev_err(smu->adev->dev, "Failed to get smu version!\n");
		return ret;
	}

	/* PPSMC_MSG_GmiPwrDnControl is supported by 54.23.0 and onwards */
	if (smu_version < 0x00361700) {
		dev_err(smu->adev->dev, "XGMI power down control is only supported by PMFW 54.23.0 and onwards\n");
		return -EINVAL;
	}

	if (en)
		return smu_cmn_send_smc_msg_with_param(smu,
						   SMU_MSG_GmiPwrDnControl,
						   1,
						   NULL);

	return smu_cmn_send_smc_msg_with_param(smu,
					   SMU_MSG_GmiPwrDnControl,
					   0,
					   NULL);
}

static const struct throttling_logging_label {
	uint32_t feature_mask;
	const char *label;
} logging_label[] = {
	{(1U << THROTTLER_TEMP_HOTSPOT_BIT), "GPU"},
	{(1U << THROTTLER_TEMP_MEM_BIT), "HBM"},
	{(1U << THROTTLER_TEMP_VR_GFX_BIT), "VR of GFX rail"},
	{(1U << THROTTLER_TEMP_VR_MEM_BIT), "VR of HBM rail"},
	{(1U << THROTTLER_TEMP_VR_SOC_BIT), "VR of SOC rail"},
	{(1U << THROTTLER_VRHOT0_BIT), "VR0 HOT"},
	{(1U << THROTTLER_VRHOT1_BIT), "VR1 HOT"},
};
static void arcturus_log_thermal_throttling_event(struct smu_context *smu)
{
	int ret;
	int throttler_idx, throtting_events = 0, buf_idx = 0;
	struct amdgpu_device *adev = smu->adev;
	uint32_t throttler_status;
	char log_buf[256];

	ret = arcturus_get_smu_metrics_data(smu,
					    METRICS_THROTTLER_STATUS,
					    &throttler_status);
	if (ret)
		return;

	memset(log_buf, 0, sizeof(log_buf));
	for (throttler_idx = 0; throttler_idx < ARRAY_SIZE(logging_label);
	     throttler_idx++) {
		if (throttler_status & logging_label[throttler_idx].feature_mask) {
			throtting_events++;
			buf_idx += snprintf(log_buf + buf_idx,
					    sizeof(log_buf) - buf_idx,
					    "%s%s",
					    throtting_events > 1 ? " and " : "",
					    logging_label[throttler_idx].label);
			if (buf_idx >= sizeof(log_buf)) {
				dev_err(adev->dev, "buffer overflow!\n");
				log_buf[sizeof(log_buf) - 1] = '\0';
				break;
			}
		}
	}

	dev_warn(adev->dev, "WARN: GPU thermal throttling temperature reached, expect performance decrease. %s.\n",
			log_buf);
	kgd2kfd_smi_event_throttle(smu->adev->kfd.dev, throttler_status);
}

static int arcturus_get_current_pcie_link_speed(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t esm_ctrl;

	/* TODO: confirm this on real target */
	esm_ctrl = RREG32_PCIE(smnPCIE_ESM_CTRL);
	if ((esm_ctrl >> 15) & 0x1FFFF)
		return (((esm_ctrl >> 8) & 0x3F) + 128);

	return smu_v11_0_get_current_pcie_link_speed(smu);
}

static ssize_t arcturus_get_gpu_metrics(struct smu_context *smu,
					void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v1_0 *gpu_metrics =
		(struct gpu_metrics_v1_0 *)smu_table->gpu_metrics_table;
	SmuMetrics_t metrics;
	int ret = 0;

	ret = smu_cmn_get_metrics_table(smu,
					&metrics,
					true);
	if (ret)
		return ret;

	smu_v11_0_init_gpu_metrics_v1_0(gpu_metrics);

	gpu_metrics->temperature_edge = metrics.TemperatureEdge;
	gpu_metrics->temperature_hotspot = metrics.TemperatureHotspot;
	gpu_metrics->temperature_mem = metrics.TemperatureHBM;
	gpu_metrics->temperature_vrgfx = metrics.TemperatureVrGfx;
	gpu_metrics->temperature_vrsoc = metrics.TemperatureVrSoc;
	gpu_metrics->temperature_vrmem = metrics.TemperatureVrMem;

	gpu_metrics->average_gfx_activity = metrics.AverageGfxActivity;
	gpu_metrics->average_umc_activity = metrics.AverageUclkActivity;
	gpu_metrics->average_mm_activity = metrics.VcnActivityPercentage;

	gpu_metrics->average_socket_power = metrics.AverageSocketPower;
	gpu_metrics->energy_accumulator = metrics.EnergyAccumulator;

	gpu_metrics->average_gfxclk_frequency = metrics.AverageGfxclkFrequency;
	gpu_metrics->average_socclk_frequency = metrics.AverageSocclkFrequency;
	gpu_metrics->average_uclk_frequency = metrics.AverageUclkFrequency;
	gpu_metrics->average_vclk0_frequency = metrics.AverageVclkFrequency;
	gpu_metrics->average_dclk0_frequency = metrics.AverageDclkFrequency;

	gpu_metrics->current_gfxclk = metrics.CurrClock[PPCLK_GFXCLK];
	gpu_metrics->current_socclk = metrics.CurrClock[PPCLK_SOCCLK];
	gpu_metrics->current_uclk = metrics.CurrClock[PPCLK_UCLK];
	gpu_metrics->current_vclk0 = metrics.CurrClock[PPCLK_VCLK];
	gpu_metrics->current_dclk0 = metrics.CurrClock[PPCLK_DCLK];

	gpu_metrics->throttle_status = metrics.ThrottlerStatus;

	gpu_metrics->current_fan_speed = metrics.CurrFanSpeed;

	gpu_metrics->pcie_link_width =
			smu_v11_0_get_current_pcie_link_width(smu);
	gpu_metrics->pcie_link_speed =
			arcturus_get_current_pcie_link_speed(smu);

	*table = (void *)gpu_metrics;

	return sizeof(struct gpu_metrics_v1_0);
}

static const struct pptable_funcs arcturus_ppt_funcs = {
	/* init dpm */
	.get_allowed_feature_mask = arcturus_get_allowed_feature_mask,
	/* btc */
	.run_btc = arcturus_run_btc,
	/* dpm/clk tables */
	.set_default_dpm_table = arcturus_set_default_dpm_table,
	.populate_umd_state_clk = arcturus_populate_umd_state_clk,
	.get_thermal_temperature_range = arcturus_get_thermal_temperature_range,
	.print_clk_levels = arcturus_print_clk_levels,
	.force_clk_levels = arcturus_force_clk_levels,
	.read_sensor = arcturus_read_sensor,
	.get_fan_speed_rpm = arcturus_get_fan_speed_rpm,
	.get_power_profile_mode = arcturus_get_power_profile_mode,
	.set_power_profile_mode = arcturus_set_power_profile_mode,
	.set_performance_level = arcturus_set_performance_level,
	/* debug (internal used) */
	.dump_pptable = arcturus_dump_pptable,
	.get_power_limit = arcturus_get_power_limit,
	.is_dpm_running = arcturus_is_dpm_running,
	.dpm_set_vcn_enable = arcturus_dpm_set_vcn_enable,
	.i2c_init = arcturus_i2c_control_init,
	.i2c_fini = arcturus_i2c_control_fini,
	.get_unique_id = arcturus_get_unique_id,
	.init_microcode = smu_v11_0_init_microcode,
	.load_microcode = smu_v11_0_load_microcode,
	.fini_microcode = smu_v11_0_fini_microcode,
	.init_smc_tables = arcturus_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.check_fw_status = smu_v11_0_check_fw_status,
	/* pptable related */
	.setup_pptable = arcturus_setup_pptable,
	.get_vbios_bootup_values = smu_v11_0_get_vbios_bootup_values,
	.check_fw_version = smu_v11_0_check_fw_version,
	.write_pptable = smu_cmn_write_pptable,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.set_tool_table_location = smu_v11_0_set_tool_table_location,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.system_features_control = smu_v11_0_system_features_control,
	.send_smc_msg_with_param = smu_cmn_send_smc_msg_with_param,
	.send_smc_msg = smu_cmn_send_smc_msg,
	.init_display_count = NULL,
	.set_allowed_mask = smu_v11_0_set_allowed_mask,
	.get_enabled_mask = smu_cmn_get_enabled_mask,
	.feature_is_enabled = smu_cmn_feature_is_enabled,
	.disable_all_features_with_exception = smu_cmn_disable_all_features_with_exception,
	.notify_display_change = NULL,
	.set_power_limit = smu_v11_0_set_power_limit,
	.init_max_sustainable_clocks = smu_v11_0_init_max_sustainable_clocks,
	.enable_thermal_alert = smu_v11_0_enable_thermal_alert,
	.disable_thermal_alert = smu_v11_0_disable_thermal_alert,
	.set_min_dcef_deep_sleep = NULL,
	.display_clock_voltage_request = smu_v11_0_display_clock_voltage_request,
	.get_fan_control_mode = smu_v11_0_get_fan_control_mode,
	.set_fan_control_mode = smu_v11_0_set_fan_control_mode,
	.set_fan_speed_percent = smu_v11_0_set_fan_speed_percent,
	.set_fan_speed_rpm = smu_v11_0_set_fan_speed_rpm,
	.set_xgmi_pstate = smu_v11_0_set_xgmi_pstate,
	.gfx_off_control = smu_v11_0_gfx_off_control,
	.register_irq_handler = smu_v11_0_register_irq_handler,
	.set_azalia_d3_pme = smu_v11_0_set_azalia_d3_pme,
	.get_max_sustainable_clocks_by_dc = smu_v11_0_get_max_sustainable_clocks_by_dc,
	.baco_is_support= arcturus_is_baco_supported,
	.baco_get_state = smu_v11_0_baco_get_state,
	.baco_set_state = smu_v11_0_baco_set_state,
	.baco_enter = smu_v11_0_baco_enter,
	.baco_exit = smu_v11_0_baco_exit,
	.get_dpm_ultimate_freq = smu_v11_0_get_dpm_ultimate_freq,
	.set_soft_freq_limited_range = smu_v11_0_set_soft_freq_limited_range,
	.set_df_cstate = arcturus_set_df_cstate,
	.allow_xgmi_power_down = arcturus_allow_xgmi_power_down,
	.log_thermal_throttling_event = arcturus_log_thermal_throttling_event,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.set_pp_feature_mask = smu_cmn_set_pp_feature_mask,
	.get_gpu_metrics = arcturus_get_gpu_metrics,
	.gfx_ulv_control = smu_v11_0_gfx_ulv_control,
	.deep_sleep_control = smu_v11_0_deep_sleep_control,
	.get_fan_parameters = arcturus_get_fan_parameters,
	.interrupt_work = smu_v11_0_interrupt_work,
};

void arcturus_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &arcturus_ppt_funcs;
	smu->message_map = arcturus_message_map;
	smu->clock_map = arcturus_clk_map;
	smu->feature_map = arcturus_feature_mask_map;
	smu->table_map = arcturus_table_map;
	smu->pwr_src_map = arcturus_pwr_src_map;
	smu->workload_map = arcturus_workload_map;
}
