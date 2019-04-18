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

#include "pp_debug.h"
#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_navi10.h"
#include "soc15_common.h"
#include "atom.h"
#include "navi10_ppt.h"
#include "smu_v11_0_pptable.h"
#include "smu_v11_0_ppsmc.h"

#define MSG_MAP(msg, index) \
	[SMU_MSG_##msg] = index

static int navi10_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			PPSMC_MSG_TestMessage),
	MSG_MAP(GetSmuVersion,			PPSMC_MSG_GetSmuVersion),
	MSG_MAP(GetDriverIfVersion,		PPSMC_MSG_GetDriverIfVersion),
	MSG_MAP(SetAllowedFeaturesMaskLow,	PPSMC_MSG_SetAllowedFeaturesMaskLow),
	MSG_MAP(SetAllowedFeaturesMaskHigh,	PPSMC_MSG_SetAllowedFeaturesMaskHigh),
	MSG_MAP(EnableAllSmuFeatures,		PPSMC_MSG_EnableAllSmuFeatures),
	MSG_MAP(DisableAllSmuFeatures,		PPSMC_MSG_DisableAllSmuFeatures),
	MSG_MAP(EnableSmuFeaturesLow,		PPSMC_MSG_EnableSmuFeaturesLow),
	MSG_MAP(EnableSmuFeaturesHigh,		PPSMC_MSG_EnableSmuFeaturesHigh),
	MSG_MAP(DisableSmuFeaturesLow,		PPSMC_MSG_DisableSmuFeaturesLow),
	MSG_MAP(DisableSmuFeaturesHigh,		PPSMC_MSG_DisableSmuFeaturesHigh),
	MSG_MAP(GetEnabledSmuFeaturesLow,	PPSMC_MSG_GetEnabledSmuFeaturesLow),
	MSG_MAP(GetEnabledSmuFeaturesHigh,	PPSMC_MSG_GetEnabledSmuFeaturesHigh),
	MSG_MAP(SetWorkloadMask,		PPSMC_MSG_SetWorkloadMask),
	MSG_MAP(SetPptLimit,			PPSMC_MSG_SetPptLimit),
	MSG_MAP(SetDriverDramAddrHigh,		PPSMC_MSG_SetDriverDramAddrHigh),
	MSG_MAP(SetDriverDramAddrLow,		PPSMC_MSG_SetDriverDramAddrLow),
	MSG_MAP(SetToolsDramAddrHigh,		PPSMC_MSG_SetToolsDramAddrHigh),
	MSG_MAP(SetToolsDramAddrLow,		PPSMC_MSG_SetToolsDramAddrLow),
	MSG_MAP(TransferTableSmu2Dram,		PPSMC_MSG_TransferTableSmu2Dram),
	MSG_MAP(TransferTableDram2Smu,		PPSMC_MSG_TransferTableDram2Smu),
	MSG_MAP(UseDefaultPPTable,		PPSMC_MSG_UseDefaultPPTable),
	MSG_MAP(UseBackupPPTable,		PPSMC_MSG_UseBackupPPTable),
	MSG_MAP(RunBtc,				PPSMC_MSG_RunBtc),
	MSG_MAP(EnterBaco,			PPSMC_MSG_EnterBaco),
	MSG_MAP(SetSoftMinByFreq,		PPSMC_MSG_SetSoftMinByFreq),
	MSG_MAP(SetSoftMaxByFreq,		PPSMC_MSG_SetSoftMaxByFreq),
	MSG_MAP(SetHardMinByFreq,		PPSMC_MSG_SetHardMinByFreq),
	MSG_MAP(SetHardMaxByFreq,		PPSMC_MSG_SetHardMaxByFreq),
	MSG_MAP(GetMinDpmFreq,			PPSMC_MSG_GetMinDpmFreq),
	MSG_MAP(GetMaxDpmFreq,			PPSMC_MSG_GetMaxDpmFreq),
	MSG_MAP(GetDpmFreqByIndex,		PPSMC_MSG_GetDpmFreqByIndex),
	MSG_MAP(SetMemoryChannelConfig,		PPSMC_MSG_SetMemoryChannelConfig),
	MSG_MAP(SetGeminiMode,			PPSMC_MSG_SetGeminiMode),
	MSG_MAP(SetGeminiApertureHigh,		PPSMC_MSG_SetGeminiApertureHigh),
	MSG_MAP(SetGeminiApertureLow,		PPSMC_MSG_SetGeminiApertureLow),
	MSG_MAP(OverridePcieParameters,		PPSMC_MSG_OverridePcieParameters),
	MSG_MAP(SetMinDeepSleepDcefclk,		PPSMC_MSG_SetMinDeepSleepDcefclk),
	MSG_MAP(ReenableAcDcInterrupt,		PPSMC_MSG_ReenableAcDcInterrupt),
	MSG_MAP(NotifyPowerSource,		PPSMC_MSG_NotifyPowerSource),
	MSG_MAP(SetUclkFastSwitch,		PPSMC_MSG_SetUclkFastSwitch),
	MSG_MAP(SetVideoFps,			PPSMC_MSG_SetVideoFps),
	MSG_MAP(PrepareMp1ForUnload,		PPSMC_MSG_PrepareMp1ForUnload),
	MSG_MAP(DramLogSetDramAddrHigh,		PPSMC_MSG_DramLogSetDramAddrHigh),
	MSG_MAP(DramLogSetDramAddrLow,		PPSMC_MSG_DramLogSetDramAddrLow),
	MSG_MAP(DramLogSetDramSize,		PPSMC_MSG_DramLogSetDramSize),
	MSG_MAP(ConfigureGfxDidt,		PPSMC_MSG_ConfigureGfxDidt),
	MSG_MAP(NumOfDisplays,			PPSMC_MSG_NumOfDisplays),
	MSG_MAP(SetSystemVirtualDramAddrHigh,	PPSMC_MSG_SetSystemVirtualDramAddrHigh),
	MSG_MAP(SetSystemVirtualDramAddrLow,	PPSMC_MSG_SetSystemVirtualDramAddrLow),
	MSG_MAP(AllowGfxOff,			PPSMC_MSG_AllowGfxOff),
	MSG_MAP(DisallowGfxOff,			PPSMC_MSG_DisallowGfxOff),
	MSG_MAP(GetPptLimit,			PPSMC_MSG_GetPptLimit),
	MSG_MAP(GetDcModeMaxDpmFreq,		PPSMC_MSG_GetDcModeMaxDpmFreq),
	MSG_MAP(GetDebugData,			PPSMC_MSG_GetDebugData),
	MSG_MAP(ExitBaco,			PPSMC_MSG_ExitBaco),
	MSG_MAP(PrepareMp1ForReset,		PPSMC_MSG_PrepareMp1ForReset),
	MSG_MAP(PrepareMp1ForShutdown,		PPSMC_MSG_PrepareMp1ForShutdown),
	MSG_MAP(PowerUpVcn,		PPSMC_MSG_PowerUpVcn),
	MSG_MAP(PowerDownVcn,		PPSMC_MSG_PowerDownVcn),
	MSG_MAP(PowerUpJpeg,		PPSMC_MSG_PowerUpJpeg),
	MSG_MAP(PowerDownJpeg,		PPSMC_MSG_PowerDownJpeg),
};

static int navi10_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK, PPCLK_GFXCLK),
	CLK_MAP(SCLK,	PPCLK_GFXCLK),
	CLK_MAP(SOCCLK, PPCLK_SOCCLK),
	CLK_MAP(FCLK, PPCLK_SOCCLK),
	CLK_MAP(UCLK, PPCLK_UCLK),
	CLK_MAP(MCLK, PPCLK_UCLK),
	CLK_MAP(DCLK, PPCLK_DCLK),
	CLK_MAP(VCLK, PPCLK_VCLK),
	CLK_MAP(DCEFCLK, PPCLK_DCEFCLK),
	CLK_MAP(DISPCLK, PPCLK_DISPCLK),
	CLK_MAP(PIXCLK, PPCLK_PIXCLK),
	CLK_MAP(PHYCLK, PPCLK_PHYCLK),
};

static int navi10_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(DPM_PREFETCHER),
	FEA_MAP(DPM_GFXCLK),
	FEA_MAP(DPM_GFX_PACE),
	FEA_MAP(DPM_UCLK),
	FEA_MAP(DPM_SOCCLK),
	FEA_MAP(DPM_MP0CLK),
	FEA_MAP(DPM_LINK),
	FEA_MAP(DPM_DCEFCLK),
	FEA_MAP(MEM_VDDCI_SCALING),
	FEA_MAP(MEM_MVDD_SCALING),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
	FEA_MAP(DS_LCLK),
	FEA_MAP(DS_DCEFCLK),
	FEA_MAP(DS_UCLK),
	FEA_MAP(GFX_ULV),
	FEA_MAP(FW_DSTATE),
	FEA_MAP(GFXOFF),
	FEA_MAP(BACO),
	FEA_MAP(VCN_PG),
	FEA_MAP(JPEG_PG),
	FEA_MAP(USB_PG),
	FEA_MAP(RSMU_SMN_CG),
	FEA_MAP(PPT),
	FEA_MAP(TDC),
	FEA_MAP(GFX_EDC),
	FEA_MAP(APCC_PLUS),
	FEA_MAP(GTHR),
	FEA_MAP(ACDC),
	FEA_MAP(VR0HOT),
	FEA_MAP(VR1HOT),
	FEA_MAP(FW_CTF),
	FEA_MAP(FAN_CONTROL),
	FEA_MAP(THERMAL),
	FEA_MAP(GFX_DCS),
	FEA_MAP(RM),
	FEA_MAP(LED_DISPLAY),
	FEA_MAP(GFX_SS),
	FEA_MAP(OUT_OF_BAND_MONITOR),
	FEA_MAP(TEMP_DEPENDENT_VMIN),
	FEA_MAP(MMHUB_PG),
	FEA_MAP(ATHUB_PG),
};

static int navi10_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PPTABLE),
	TAB_MAP(WATERMARKS),
	TAB_MAP(AVFS),
	TAB_MAP(AVFS_PSM_DEBUG),
	TAB_MAP(AVFS_FUSE_OVERRIDE),
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(DRIVER_SMU_CONFIG),
	TAB_MAP(ACTIVITY_MONITOR_COEFF),
	TAB_MAP(OVERDRIVE),
	TAB_MAP(I2C_COMMANDS),
	TAB_MAP(PACE),
};

static int navi10_pwr_src_map[SMU_POWER_SOURCE_COUNT] = {
	PWR_MAP(AC),
	PWR_MAP(DC),
};

static int navi10_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	int val;
	if (index > SMU_MSG_MAX_COUNT)
		return -EINVAL;

	val = navi10_message_map[index];
	if (val > PPSMC_Message_Count)
		return -EINVAL;

	return val;
}

static int navi10_get_smu_clk_index(struct smu_context *smc, uint32_t index)
{
	int val;
	if (index >= SMU_CLK_COUNT)
		return -EINVAL;

	val = navi10_clk_map[index];
	if (val >= PPCLK_COUNT)
		return -EINVAL;

	return val;
}

static int navi10_get_smu_feature_index(struct smu_context *smc, uint32_t index)
{
	int val;
	if (index >= SMU_FEATURE_COUNT)
		return -EINVAL;

	val = navi10_feature_mask_map[index];
	if (val > 64)
		return -EINVAL;

	return val;
}

static int navi10_get_smu_table_index(struct smu_context *smc, uint32_t index)
{
	int val;
	if (index >= SMU_TABLE_COUNT)
		return -EINVAL;

	val = navi10_table_map[index];
	if (val >= TABLE_COUNT)
		return -EINVAL;

	return val;
}

static int navi10_get_pwr_src_index(struct smu_context *smc, uint32_t index)
{
	int val;
	if (index >= SMU_POWER_SOURCE_COUNT)
		return -EINVAL;

	val = navi10_pwr_src_map[index];
	if (val >= POWER_SOURCE_COUNT)
		return -EINVAL;

	return val;
}

#define FEATURE_MASK(feature) (1UL << feature)
static int
navi10_get_allowed_feature_mask(struct smu_context *smu,
				  uint32_t *feature_mask, uint32_t num)
{
	struct amdgpu_device *adev = smu->adev;

	if (num > 2)
		return -EINVAL;

	memset(feature_mask, 0, sizeof(uint32_t) * num);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_PREFETCHER_BIT)
				| FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_LINK_BIT)
				| FEATURE_MASK(FEATURE_GFX_ULV_BIT)
				| FEATURE_MASK(FEATURE_RSMU_SMN_CG_BIT)
				| FEATURE_MASK(FEATURE_PPT_BIT)
				| FEATURE_MASK(FEATURE_TDC_BIT)
				| FEATURE_MASK(FEATURE_GFX_EDC_BIT)
				| FEATURE_MASK(FEATURE_VR0HOT_BIT)
				| FEATURE_MASK(FEATURE_FAN_CONTROL_BIT)
				| FEATURE_MASK(FEATURE_THERMAL_BIT)
				| FEATURE_MASK(FEATURE_LED_DISPLAY_BIT)
				| FEATURE_MASK(FEATURE_MMHUB_PG_BIT)
				| FEATURE_MASK(FEATURE_ATHUB_PG_BIT)
				| FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT);

	if (adev->pm.pp_feature & PP_MCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_UCLK_BIT)
				| FEATURE_MASK(FEATURE_MEM_VDDCI_SCALING_BIT)
				| FEATURE_MASK(FEATURE_MEM_MVDD_SCALING_BIT);

	if (adev->pm.pp_feature & PP_GFXOFF_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_SS_BIT)
				| FEATURE_MASK(FEATURE_GFXOFF_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_VCN)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VCN_PG_BIT);

	return 0;
}

static int navi10_check_powerplay_table(struct smu_context *smu)
{
	return 0;
}

static int navi10_append_powerplay_table(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	struct atom_smc_dpm_info_v4_5 *smc_dpm_table;
	int index, ret;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					   smc_dpm_info);

	ret = smu_get_atom_data_table(smu, index, NULL, NULL, NULL,
				      (uint8_t **)&smc_dpm_table);
	if (ret)
		return ret;

	memcpy(smc_pptable->I2cControllers, smc_dpm_table->I2cControllers,
	       sizeof(I2cControllerConfig_t) * NUM_I2C_CONTROLLERS);

	/* SVI2 Board Parameters */
	smc_pptable->MaxVoltageStepGfx = smc_dpm_table->MaxVoltageStepGfx;
	smc_pptable->MaxVoltageStepSoc = smc_dpm_table->MaxVoltageStepSoc;
	smc_pptable->VddGfxVrMapping = smc_dpm_table->VddGfxVrMapping;
	smc_pptable->VddSocVrMapping = smc_dpm_table->VddSocVrMapping;
	smc_pptable->VddMem0VrMapping = smc_dpm_table->VddMem0VrMapping;
	smc_pptable->VddMem1VrMapping = smc_dpm_table->VddMem1VrMapping;
	smc_pptable->GfxUlvPhaseSheddingMask = smc_dpm_table->GfxUlvPhaseSheddingMask;
	smc_pptable->SocUlvPhaseSheddingMask = smc_dpm_table->SocUlvPhaseSheddingMask;
	smc_pptable->ExternalSensorPresent = smc_dpm_table->ExternalSensorPresent;
	smc_pptable->Padding8_V = smc_dpm_table->Padding8_V;

	/* Telemetry Settings */
	smc_pptable->GfxMaxCurrent = smc_dpm_table->GfxMaxCurrent;
	smc_pptable->GfxOffset = smc_dpm_table->GfxOffset;
	smc_pptable->Padding_TelemetryGfx = smc_dpm_table->Padding_TelemetryGfx;
	smc_pptable->SocMaxCurrent = smc_dpm_table->SocMaxCurrent;
	smc_pptable->SocOffset = smc_dpm_table->SocOffset;
	smc_pptable->Padding_TelemetrySoc = smc_dpm_table->Padding_TelemetrySoc;
	smc_pptable->Mem0MaxCurrent = smc_dpm_table->Mem0MaxCurrent;
	smc_pptable->Mem0Offset = smc_dpm_table->Mem0Offset;
	smc_pptable->Padding_TelemetryMem0 = smc_dpm_table->Padding_TelemetryMem0;
	smc_pptable->Mem1MaxCurrent = smc_dpm_table->Mem1MaxCurrent;
	smc_pptable->Mem1Offset = smc_dpm_table->Mem1Offset;
	smc_pptable->Padding_TelemetryMem1 = smc_dpm_table->Padding_TelemetryMem1;

	/* GPIO Settings */
	smc_pptable->AcDcGpio = smc_dpm_table->AcDcGpio;
	smc_pptable->AcDcPolarity = smc_dpm_table->AcDcPolarity;
	smc_pptable->VR0HotGpio = smc_dpm_table->VR0HotGpio;
	smc_pptable->VR0HotPolarity = smc_dpm_table->VR0HotPolarity;
	smc_pptable->VR1HotGpio = smc_dpm_table->VR1HotGpio;
	smc_pptable->VR1HotPolarity = smc_dpm_table->VR1HotPolarity;
	smc_pptable->GthrGpio = smc_dpm_table->GthrGpio;
	smc_pptable->GthrPolarity = smc_dpm_table->GthrPolarity;

	/* LED Display Settings */
	smc_pptable->LedPin0 = smc_dpm_table->LedPin0;
	smc_pptable->LedPin1 = smc_dpm_table->LedPin1;
	smc_pptable->LedPin2 = smc_dpm_table->LedPin2;
	smc_pptable->padding8_4 = smc_dpm_table->padding8_4;

	/* GFXCLK PLL Spread Spectrum */
	smc_pptable->PllGfxclkSpreadEnabled = smc_dpm_table->PllGfxclkSpreadEnabled;
	smc_pptable->PllGfxclkSpreadPercent = smc_dpm_table->PllGfxclkSpreadPercent;
	smc_pptable->PllGfxclkSpreadFreq = smc_dpm_table->PllGfxclkSpreadFreq;

	/* GFXCLK DFLL Spread Spectrum */
	smc_pptable->DfllGfxclkSpreadEnabled = smc_dpm_table->DfllGfxclkSpreadEnabled;
	smc_pptable->DfllGfxclkSpreadPercent = smc_dpm_table->DfllGfxclkSpreadPercent;
	smc_pptable->DfllGfxclkSpreadFreq = smc_dpm_table->DfllGfxclkSpreadFreq;

	/* UCLK Spread Spectrum */
	smc_pptable->UclkSpreadEnabled = smc_dpm_table->UclkSpreadEnabled;
	smc_pptable->UclkSpreadPercent = smc_dpm_table->UclkSpreadPercent;
	smc_pptable->UclkSpreadFreq = smc_dpm_table->UclkSpreadFreq;

	/* SOCCLK Spread Spectrum */
	smc_pptable->SoclkSpreadEnabled = smc_dpm_table->SoclkSpreadEnabled;
	smc_pptable->SocclkSpreadPercent = smc_dpm_table->SocclkSpreadPercent;
	smc_pptable->SocclkSpreadFreq = smc_dpm_table->SocclkSpreadFreq;

	/* Total board power */
	smc_pptable->TotalBoardPower = smc_dpm_table->TotalBoardPower;
	smc_pptable->BoardPadding = smc_dpm_table->BoardPadding;

	/* Mvdd Svi2 Div Ratio Setting */
	smc_pptable->MvddRatio = smc_dpm_table->MvddRatio;

	if (adev->pm.pp_feature & PP_GFXOFF_MASK) {
		*(uint64_t *)smc_pptable->FeaturesToRun |= FEATURE_MASK(FEATURE_GFX_SS_BIT)
					| FEATURE_MASK(FEATURE_GFXOFF_BIT);

		/* TODO: remove it once SMU fw fix it */
		smc_pptable->DebugOverrides |= DPM_OVERRIDE_DISABLE_DFLL_PLL_SHUTDOWN;
	}

	return 0;
}

static int navi10_store_powerplay_table(struct smu_context *smu)
{
	struct smu_11_0_powerplay_table *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;

	if (!table_context->power_play_table)
		return -EINVAL;

	powerplay_table = table_context->power_play_table;

	memcpy(table_context->driver_pptable, &powerplay_table->smc_pptable,
	       sizeof(PPTable_t));

	return 0;
}

static void navi10_tables_init(struct smu_context *smu, struct smu_table *tables)
{
	SMU_TABLE_INIT(tables, SMU_TABLE_PPTABLE, sizeof(PPTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_OVERDRIVE, sizeof(OverDriveTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, SMU_TABLE_ACTIVITY_MONITOR_COEFF,
		       sizeof(DpmActivityMonitorCoeffInt_t), PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM);
}

static int navi10_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	if (smu_dpm->dpm_context)
		return -EINVAL;

	smu_dpm->dpm_context = kzalloc(sizeof(struct smu_11_0_dpm_context),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;

	smu_dpm->dpm_context_size = sizeof(struct smu_11_0_dpm_context);

	return 0;
}

static int navi10_set_default_dpm_table(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	PPTable_t *driver_ppt = NULL;

	driver_ppt = table_context->driver_pptable;

	dpm_context->dpm_tables.soc_table.min = driver_ppt->FreqTableSocclk[0];
	dpm_context->dpm_tables.soc_table.max = driver_ppt->FreqTableSocclk[NUM_SOCCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.gfx_table.min = driver_ppt->FreqTableGfx[0];
	dpm_context->dpm_tables.gfx_table.max = driver_ppt->FreqTableGfx[NUM_GFXCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.uclk_table.min = driver_ppt->FreqTableUclk[0];
	dpm_context->dpm_tables.uclk_table.max = driver_ppt->FreqTableUclk[NUM_UCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.vclk_table.min = driver_ppt->FreqTableVclk[0];
	dpm_context->dpm_tables.vclk_table.max = driver_ppt->FreqTableVclk[NUM_VCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.dclk_table.min = driver_ppt->FreqTableDclk[0];
	dpm_context->dpm_tables.dclk_table.max = driver_ppt->FreqTableDclk[NUM_DCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.dcef_table.min = driver_ppt->FreqTableDcefclk[0];
	dpm_context->dpm_tables.dcef_table.max = driver_ppt->FreqTableDcefclk[NUM_DCEFCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.pixel_table.min = driver_ppt->FreqTablePixclk[0];
	dpm_context->dpm_tables.pixel_table.max = driver_ppt->FreqTablePixclk[NUM_PIXCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.display_table.min = driver_ppt->FreqTableDispclk[0];
	dpm_context->dpm_tables.display_table.max = driver_ppt->FreqTableDispclk[NUM_DISPCLK_DPM_LEVELS - 1];

	dpm_context->dpm_tables.phy_table.min = driver_ppt->FreqTablePhyclk[0];
	dpm_context->dpm_tables.phy_table.max = driver_ppt->FreqTablePhyclk[NUM_PHYCLK_DPM_LEVELS - 1];

	return 0;
}

static int navi10_dpm_set_uvd_enable(struct smu_context *smu, bool enable)
{
	int ret = 0;

	if (enable) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 1);
		if (ret)
			return ret;
	} else {
		ret = smu_send_smc_msg(smu, SMU_MSG_PowerDownVcn);
		if (ret)
			return ret;
	}

	return 0;
}

static int navi10_get_current_clk_freq_by_table(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       uint32_t *value)
{
	static SmuMetrics_t metrics = {0};
	int ret = 0, clk_id = 0;

	if (!value)
		return -EINVAL;

	ret = smu_update_table(smu, SMU_TABLE_SMU_METRICS, (void *)&metrics, false);
	if (ret)
		return ret;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	*value = metrics.CurrClock[clk_id];

	return ret;
}

static int navi10_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_FCLK:
	case SMU_DCEFCLK:
		ret = smu_get_current_clk_freq(smu, clk_type, &cur_value);
		if (ret)
			return size;

		size += sprintf(buf, "current clk: %uMhz\n", cur_value);

		ret = smu_get_dpm_level_count(smu, clk_type, &count);
		if (ret)
			return size;

		for (i = 0; i < count; i++) {
			ret = smu_get_dpm_freq_by_index(smu, clk_type, i, &value);
			if (ret)
				return size;

			size += sprintf(buf + size, "%d: %uMhz %s\n", i, value,
					cur_value == value ? "*" : "");
		}
		break;
	default:
		break;
	}

	return size;
}

static const struct pptable_funcs navi10_ppt_funcs = {
	.tables_init = navi10_tables_init,
	.alloc_dpm_context = navi10_allocate_dpm_context,
	.store_powerplay_table = navi10_store_powerplay_table,
	.check_powerplay_table = navi10_check_powerplay_table,
	.append_powerplay_table = navi10_append_powerplay_table,
	.get_smu_msg_index = navi10_get_smu_msg_index,
	.get_smu_clk_index = navi10_get_smu_clk_index,
	.get_smu_feature_index = navi10_get_smu_feature_index,
	.get_smu_table_index = navi10_get_smu_table_index,
	.get_smu_power_index = navi10_get_pwr_src_index,
	.get_allowed_feature_mask = navi10_get_allowed_feature_mask,
	.set_default_dpm_table = navi10_set_default_dpm_table,
	.dpm_set_uvd_enable = navi10_dpm_set_uvd_enable,
	.get_current_clk_freq_by_table = navi10_get_current_clk_freq_by_table,
	.print_clk_levels = navi10_print_clk_levels,
};

void navi10_set_ppt_funcs(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	smu->ppt_funcs = &navi10_ppt_funcs;
	smu->smc_if_version = SMU11_DRIVER_IF_VERSION;
	smu_table->table_count = TABLE_COUNT;
}
