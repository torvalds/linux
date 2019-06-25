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

#include "asic_reg/mp/mp_11_0_sh_mask.h"

#define FEATURE_MASK(feature) (1ULL << feature)
#define SMC_DPM_FEATURE ( \
	FEATURE_MASK(FEATURE_DPM_PREFETCHER_BIT) | \
	FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_GFX_PACE_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_UCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_LINK_BIT)	 | \
	FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT))

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
	MSG_MAP(BacoAudioD3PME,		PPSMC_MSG_BacoAudioD3PME),
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

static int navi10_workload_map[] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT,	WORKLOAD_PPLIB_DEFAULT_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_FULLSCREEN3D,		WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_POWERSAVING,		WORKLOAD_PPLIB_POWER_SAVING_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VR,			WORKLOAD_PPLIB_VR_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_CUSTOM_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
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


static int navi10_get_workload_type(struct smu_context *smu, enum PP_SMC_POWER_PROFILE profile)
{
	int val;
	if (profile > PP_SMC_POWER_PROFILE_CUSTOM)
		return -EINVAL;

	val = navi10_workload_map[profile];

	return val;
}

static bool is_asic_secure(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	bool is_secure = true;
	uint32_t mp0_fw_intf;

	mp0_fw_intf = RREG32_PCIE(MP0_Public |
				   (smnMP0_FW_INTF & 0xffffffff));

	if (!(mp0_fw_intf & (1 << 19)))
		is_secure = false;

	return is_secure;
}

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
				| FEATURE_MASK(FEATURE_DS_SOCCLK_BIT)
				| FEATURE_MASK(FEATURE_PPT_BIT)
				| FEATURE_MASK(FEATURE_TDC_BIT)
				| FEATURE_MASK(FEATURE_GFX_EDC_BIT)
				| FEATURE_MASK(FEATURE_VR0HOT_BIT)
				| FEATURE_MASK(FEATURE_FAN_CONTROL_BIT)
				| FEATURE_MASK(FEATURE_THERMAL_BIT)
				| FEATURE_MASK(FEATURE_LED_DISPLAY_BIT)
				| FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_GFXCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_DCEFCLK_BIT)
				| FEATURE_MASK(FEATURE_FW_DSTATE_BIT)
				| FEATURE_MASK(FEATURE_BACO_BIT)
				| FEATURE_MASK(FEATURE_ACDC_BIT);

	if (adev->pm.pp_feature & PP_MCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_UCLK_BIT)
				| FEATURE_MASK(FEATURE_MEM_VDDCI_SCALING_BIT)
				| FEATURE_MASK(FEATURE_MEM_MVDD_SCALING_BIT);

	if (adev->pm.pp_feature & PP_GFXOFF_MASK) {
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_SS_BIT)
				| FEATURE_MASK(FEATURE_GFXOFF_BIT);
		/* TODO: remove it once fw fix the bug */
		*(uint64_t *)feature_mask &= ~FEATURE_MASK(FEATURE_FW_DSTATE_BIT);
	}

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_MMHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MMHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_ATHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_ATHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_VCN)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VCN_PG_BIT);

	/* disable DPM UCLK and DS SOCCLK on navi10 A0 secure board */
	if (is_asic_secure(smu)) {
		/* only for navi10 A0 */
		if ((adev->asic_type == CHIP_NAVI10) &&
			(adev->rev_id == 0)) {
			*(uint64_t *)feature_mask &=
					~(FEATURE_MASK(FEATURE_DPM_UCLK_BIT)
					  | FEATURE_MASK(FEATURE_MEM_VDDCI_SCALING_BIT)
					  | FEATURE_MASK(FEATURE_MEM_MVDD_SCALING_BIT));
			*(uint64_t *)feature_mask &=
					~FEATURE_MASK(FEATURE_DS_SOCCLK_BIT);
		}
	}

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

	table_context->thermal_controller_type = powerplay_table->thermal_controller_type;

	return 0;
}

static int navi10_tables_init(struct smu_context *smu, struct smu_table *tables)
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

	return 0;
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
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;

	if (enable && power_gate->uvd_gated) {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UVD_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 1);
			if (ret)
				return ret;
		}
		power_gate->uvd_gated = false;
	} else {
		if (!enable && !power_gate->uvd_gated) {
			if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UVD_BIT)) {
				ret = smu_send_smc_msg(smu, SMU_MSG_PowerDownVcn);
				if (ret)
					return ret;
			}
			power_gate->uvd_gated = true;
		}
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
		/* 10KHz -> MHz */
		cur_value = cur_value / 100;

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

static int navi10_force_clk_levels(struct smu_context *smu,
				   enum smu_clk_type clk_type, uint32_t mask)
{

	int ret = 0, size = 0;
	uint32_t soft_min_level = 0, soft_max_level = 0, min_freq = 0, max_freq = 0;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
	case SMU_SOCCLK:
	case SMU_MCLK:
	case SMU_UCLK:
	case SMU_DCEFCLK:
	case SMU_FCLK:
		ret = smu_get_dpm_freq_by_index(smu, clk_type, soft_min_level, &min_freq);
		if (ret)
			return size;

		ret = smu_get_dpm_freq_by_index(smu, clk_type, soft_max_level, &max_freq);
		if (ret)
			return size;

		ret = smu_set_soft_freq_range(smu, clk_type, min_freq, max_freq);
		if (ret)
			return size;
		break;
	default:
		break;
	}

	return size;
}

static int navi10_populate_umd_state_clk(struct smu_context *smu)
{
	int ret = 0;
	uint32_t min_sclk_freq = 0;

	ret = smu_get_dpm_freq_range(smu, SMU_SCLK, &min_sclk_freq, NULL);
	if (ret)
		return ret;

	smu->pstate_sclk = min_sclk_freq * 100;

	return ret;
}

static int navi10_get_clock_by_type_with_latency(struct smu_context *smu,
						 enum smu_clk_type clk_type,
						 struct pp_clock_levels_with_latency *clocks)
{
	int ret = 0, i = 0;
	uint32_t level_count = 0, freq = 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_DCEFCLK:
	case SMU_SOCCLK:
		ret = smu_get_dpm_level_count(smu, clk_type, &level_count);
		if (ret)
			return ret;

		level_count = min(level_count, (uint32_t)MAX_NUM_CLOCKS);
		clocks->num_levels = level_count;

		for (i = 0; i < level_count; i++) {
			ret = smu_get_dpm_freq_by_index(smu, clk_type, i, &freq);
			if (ret)
				return ret;

			clocks->data[i].clocks_in_khz = freq * 1000;
			clocks->data[i].latency_in_us = 0;
		}
		break;
	default:
		break;
	}

	return ret;
}

static int navi10_pre_display_config_changed(struct smu_context *smu)
{
	int ret = 0;
	uint32_t max_freq = 0;

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, 0);
	if (ret)
		return ret;

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_get_dpm_freq_range(smu, SMU_UCLK, NULL, &max_freq);
		if (ret)
			return ret;
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, 0, max_freq);
		if (ret)
			return ret;
	}

	return ret;
}

static int navi10_display_config_changed(struct smu_context *smu)
{
	int ret = 0;

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    !(smu->watermarks_bitmap & WATERMARKS_LOADED)) {
		ret = smu_write_watermarks_table(smu);
		if (ret)
			return ret;

		smu->watermarks_bitmap |= WATERMARKS_LOADED;
	}

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    smu_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) &&
	    smu_feature_is_supported(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays,
						  smu->display_config->num_display);
		if (ret)
			return ret;
	}

	return ret;
}

static int navi10_force_dpm_limit_value(struct smu_context *smu, bool highest)
{
	int ret = 0, i = 0;
	uint32_t min_freq, max_freq, force_freq;
	enum smu_clk_type clk_type;

	enum smu_clk_type clks[] = {
		SMU_GFXCLK,
		SMU_MCLK,
		SMU_SOCCLK,
	};

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		clk_type = clks[i];
		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq);
		if (ret)
			return ret;

		force_freq = highest ? max_freq : min_freq;
		ret = smu_set_soft_freq_range(smu, clk_type, force_freq, force_freq);
		if (ret)
			return ret;
	}

	return ret;
}

static int navi10_unforce_dpm_levels(struct smu_context *smu) {

	int ret = 0, i = 0;
	uint32_t min_freq, max_freq;
	enum smu_clk_type clk_type;

	struct clk_feature_map {
		enum smu_clk_type clk_type;
		uint32_t	feature;
	} clk_feature_map[] = {
		{SMU_GFXCLK, SMU_FEATURE_DPM_GFXCLK_BIT},
		{SMU_MCLK,   SMU_FEATURE_DPM_UCLK_BIT},
		{SMU_SOCCLK, SMU_FEATURE_DPM_SOCCLK_BIT},
	};

	for (i = 0; i < ARRAY_SIZE(clk_feature_map); i++) {
		if (!smu_feature_is_enabled(smu, clk_feature_map[i].feature))
			continue;

		clk_type = clk_feature_map[i].clk_type;

		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq);
		if (ret)
			return ret;

		ret = smu_set_soft_freq_range(smu, clk_type, min_freq, max_freq);
		if (ret)
			return ret;
	}

	return ret;
}

static int navi10_get_gpu_power(struct smu_context *smu, uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = smu_update_table(smu, SMU_TABLE_SMU_METRICS, (void *)&metrics,
			       false);
	if (ret)
		return ret;

	*value = metrics.CurrSocketPower << 8;

	return 0;
}

static int navi10_get_current_activity_percent(struct smu_context *smu,
					       uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	msleep(1);

	ret = smu_update_table(smu, SMU_TABLE_SMU_METRICS,
			       (void *)&metrics, false);
	if (ret)
		return ret;

	*value = metrics.AverageGfxActivity;

	return 0;
}

static bool navi10_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint32_t feature_mask[2];
	unsigned long feature_enabled;
	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	feature_enabled = (unsigned long)((uint64_t)feature_mask[0] |
			   ((uint64_t)feature_mask[1] << 32));
	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int navi10_get_fan_speed(struct smu_context *smu, uint16_t *value)
{
	SmuMetrics_t metrics = {0};
	int ret = 0;

	if (!value)
		return -EINVAL;

	ret = smu_update_table(smu, SMU_TABLE_SMU_METRICS,
			       (void *)&metrics, false);
	if (ret)
		return ret;

	*value = metrics.CurrFanSpeed;

	return ret;
}

static int navi10_get_fan_speed_percent(struct smu_context *smu,
					uint32_t *speed)
{
	int ret = 0;
	uint32_t percent = 0;
	uint16_t current_rpm;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	ret = navi10_get_fan_speed(smu, &current_rpm);
	if (ret)
		return ret;

	percent = current_rpm * 100 / pptable->FanMaximumRpm;
	*speed = percent > 100 ? 100 : percent;

	return ret;
}

static int navi10_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	uint32_t i, size = 0;
	uint16_t workload_type = 0;
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
			"MinFreqType",
			"MinActiveFreqType",
			"MinActiveFreq",
			"BoosterFreqType",
			"BoosterFreq",
			"PD_Data_limit_c",
			"PD_Data_error_coeff",
			"PD_Data_error_rate_coeff"};
	int result = 0;

	if (!buf)
		return -EINVAL;

	size += sprintf(buf + size, "%16s %s %s %s %s %s %s %s %s %s %s\n",
			title[0], title[1], title[2], title[3], title[4], title[5],
			title[6], title[7], title[8], title[9], title[10]);

	for (i = 0; i <= PP_SMC_POWER_PROFILE_CUSTOM; i++) {
		/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
		workload_type = smu_workload_get_type(smu, i);
		result = smu_update_table(smu,
					  SMU_TABLE_ACTIVITY_MONITOR_COEFF | workload_type << 16,
					  (void *)(&activity_monitor), false);
		if (result) {
			pr_err("[%s] Failed to get activity monitor!", __func__);
			return result;
		}

		size += sprintf(buf + size, "%2d %14s%s:\n",
			i, profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			0,
			"GFXCLK",
			activity_monitor.Gfx_FPS,
			activity_monitor.Gfx_MinFreqStep,
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
			"SOCCLK",
			activity_monitor.Soc_FPS,
			activity_monitor.Soc_MinFreqStep,
			activity_monitor.Soc_MinActiveFreqType,
			activity_monitor.Soc_MinActiveFreq,
			activity_monitor.Soc_BoosterFreqType,
			activity_monitor.Soc_BoosterFreq,
			activity_monitor.Soc_PD_Data_limit_c,
			activity_monitor.Soc_PD_Data_error_coeff,
			activity_monitor.Soc_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			2,
			"MEMLK",
			activity_monitor.Mem_FPS,
			activity_monitor.Mem_MinFreqStep,
			activity_monitor.Mem_MinActiveFreqType,
			activity_monitor.Mem_MinActiveFreq,
			activity_monitor.Mem_BoosterFreqType,
			activity_monitor.Mem_BoosterFreq,
			activity_monitor.Mem_PD_Data_limit_c,
			activity_monitor.Mem_PD_Data_error_coeff,
			activity_monitor.Mem_PD_Data_error_rate_coeff);
	}

	return size;
}

static int navi10_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	int workload_type, ret = 0;

	smu->power_profile_mode = input[size];

	if (smu->power_profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		pr_err("Invalid power profile mode %d\n", smu->power_profile_mode);
		return -EINVAL;
	}

	if (smu->power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {
		if (size < 0)
			return -EINVAL;

		ret = smu_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF | WORKLOAD_PPLIB_CUSTOM_BIT << 16,
				       (void *)(&activity_monitor), false);
		if (ret) {
			pr_err("[%s] Failed to get activity monitor!", __func__);
			return ret;
		}

		switch (input[0]) {
		case 0: /* Gfxclk */
			activity_monitor.Gfx_FPS = input[1];
			activity_monitor.Gfx_MinFreqStep = input[2];
			activity_monitor.Gfx_MinActiveFreqType = input[3];
			activity_monitor.Gfx_MinActiveFreq = input[4];
			activity_monitor.Gfx_BoosterFreqType = input[5];
			activity_monitor.Gfx_BoosterFreq = input[6];
			activity_monitor.Gfx_PD_Data_limit_c = input[7];
			activity_monitor.Gfx_PD_Data_error_coeff = input[8];
			activity_monitor.Gfx_PD_Data_error_rate_coeff = input[9];
			break;
		case 1: /* Socclk */
			activity_monitor.Soc_FPS = input[1];
			activity_monitor.Soc_MinFreqStep = input[2];
			activity_monitor.Soc_MinActiveFreqType = input[3];
			activity_monitor.Soc_MinActiveFreq = input[4];
			activity_monitor.Soc_BoosterFreqType = input[5];
			activity_monitor.Soc_BoosterFreq = input[6];
			activity_monitor.Soc_PD_Data_limit_c = input[7];
			activity_monitor.Soc_PD_Data_error_coeff = input[8];
			activity_monitor.Soc_PD_Data_error_rate_coeff = input[9];
			break;
		case 2: /* Memlk */
			activity_monitor.Mem_FPS = input[1];
			activity_monitor.Mem_MinFreqStep = input[2];
			activity_monitor.Mem_MinActiveFreqType = input[3];
			activity_monitor.Mem_MinActiveFreq = input[4];
			activity_monitor.Mem_BoosterFreqType = input[5];
			activity_monitor.Mem_BoosterFreq = input[6];
			activity_monitor.Mem_PD_Data_limit_c = input[7];
			activity_monitor.Mem_PD_Data_error_coeff = input[8];
			activity_monitor.Mem_PD_Data_error_rate_coeff = input[9];
			break;
		}

		ret = smu_update_table(smu,
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF | WORKLOAD_PPLIB_CUSTOM_BIT << 16,
				       (void *)(&activity_monitor), true);
		if (ret) {
			pr_err("[%s] Failed to set activity monitor!", __func__);
			return ret;
		}
	}

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type = smu_workload_get_type(smu, smu->power_profile_mode);
	smu_send_smc_msg_with_param(smu, SMU_MSG_SetWorkloadMask,
				    1 << workload_type);

	return ret;
}

static int navi10_get_profiling_clk_mask(struct smu_context *smu,
					 enum amd_dpm_forced_level level,
					 uint32_t *sclk_mask,
					 uint32_t *mclk_mask,
					 uint32_t *soc_mask)
{
	int ret = 0;
	uint32_t level_count = 0;

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
		if (sclk_mask)
			*sclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		if (mclk_mask)
			*mclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		if(sclk_mask) {
			ret = smu_get_dpm_level_count(smu, SMU_SCLK, &level_count);
			if (ret)
				return ret;
			*sclk_mask = level_count - 1;
		}

		if(mclk_mask) {
			ret = smu_get_dpm_level_count(smu, SMU_MCLK, &level_count);
			if (ret)
				return ret;
			*sclk_mask = level_count - 1;
		}

		if(soc_mask) {
			ret = smu_get_dpm_level_count(smu, SMU_SOCCLK, &level_count);
			if (ret)
				return ret;
			*sclk_mask = level_count - 1;
		}
	}

	return ret;
}

static int navi10_notify_smc_dispaly_config(struct smu_context *smu)
{
	struct smu_clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;
	int ret = 0;

	min_clocks.dcef_clock = smu->display_config->min_dcef_set_clk;
	min_clocks.dcef_clock_in_sr = smu->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memory_clock = smu->display_config->min_mem_set_clock;

	if (smu_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcef_clock * 10;
		if (!smu_display_clock_voltage_request(smu, &clock_req)) {
			if (smu_feature_is_supported(smu, SMU_FEATURE_DS_DCEFCLK_BIT)) {
				ret = smu_send_smc_msg_with_param(smu,
								  SMU_MSG_SetMinDeepSleepDcefclk,
								  min_clocks.dcef_clock_in_sr/100);
				if (ret) {
					pr_err("Attempt to set divider for DCEFCLK Failed!");
					return ret;
				}
			}
		} else {
			pr_info("Attempt to set Hard Min for DCEFCLK Failed!");
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, min_clocks.memory_clock/100, 0);
		if (ret) {
			pr_err("[%s] Set hard min uclk failed!", __func__);
			return ret;
		}
	}

	return 0;
}

static int navi10_set_watermarks_table(struct smu_context *smu,
				       void *watermarks, struct
				       dm_pp_wm_sets_with_clock_ranges_soc15
				       *clock_ranges)
{
	int i;
	Watermarks_t *table = watermarks;

	if (!table || !clock_ranges)
		return -EINVAL;

	if (clock_ranges->num_wm_dmif_sets > 4 ||
	    clock_ranges->num_wm_mcif_sets > 4)
		return -EINVAL;

	for (i = 0; i < clock_ranges->num_wm_dmif_sets; i++) {
		table->WatermarkRow[1][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MinUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].WmSetting = (uint8_t)
				clock_ranges->wm_dmif_clocks_ranges[i].wm_set_id;
	}

	for (i = 0; i < clock_ranges->num_wm_mcif_sets; i++) {
		table->WatermarkRow[0][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MinUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].WmSetting = (uint8_t)
				clock_ranges->wm_mcif_clocks_ranges[i].wm_set_id;
	}

	return 0;
}

static int navi10_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*(uint32_t *)data = pptable->FanMaximumRpm;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = navi10_get_current_activity_percent(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = navi10_get_gpu_power(smu, (uint32_t *)data);
		*size = 4;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int navi10_get_uclk_dpm_states(struct smu_context *smu, uint32_t *clocks_in_khz, uint32_t *num_states)
{
	uint32_t num_discrete_levels = 0;
	uint16_t *dpm_levels = NULL;
	uint16_t i = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *driver_ppt = NULL;

	if (!clocks_in_khz || !num_states || !table_context->driver_pptable)
		return -EINVAL;

	driver_ppt = table_context->driver_pptable;
	num_discrete_levels = driver_ppt->DpmDescriptor[PPCLK_UCLK].NumDiscreteLevels;
	dpm_levels = driver_ppt->FreqTableUclk;

	if (num_discrete_levels == 0 || dpm_levels == NULL)
		return -EINVAL;

	*num_states = num_discrete_levels;
	for (i = 0; i < num_discrete_levels; i++) {
		/* convert to khz */
		*clocks_in_khz = (*dpm_levels) * 1000;
		clocks_in_khz++;
		dpm_levels++;
	}

	return 0;
}

static int navi10_get_ppfeature_status(struct smu_context *smu,
				       char *buf)
{
	static const char *ppfeature_name[] = {
				"DPM_PREFETCHER",
				"DPM_GFXCLK",
				"DPM_GFX_PACE",
				"DPM_UCLK",
				"DPM_SOCCLK",
				"DPM_MP0CLK",
				"DPM_LINK",
				"DPM_DCEFCLK",
				"MEM_VDDCI_SCALING",
				"MEM_MVDD_SCALING",
				"DS_GFXCLK",
				"DS_SOCCLK",
				"DS_LCLK",
				"DS_DCEFCLK",
				"DS_UCLK",
				"GFX_ULV",
				"FW_DSTATE",
				"GFXOFF",
				"BACO",
				"VCN_PG",
				"JPEG_PG",
				"USB_PG",
				"RSMU_SMN_CG",
				"PPT",
				"TDC",
				"GFX_EDC",
				"APCC_PLUS",
				"GTHR",
				"ACDC",
				"VR0HOT",
				"VR1HOT",
				"FW_CTF",
				"FAN_CONTROL",
				"THERMAL",
				"GFX_DCS",
				"RM",
				"LED_DISPLAY",
				"GFX_SS",
				"OUT_OF_BAND_MONITOR",
				"TEMP_DEPENDENT_VMIN",
				"MMHUB_PG",
				"ATHUB_PG"};
	static const char *output_title[] = {
				"FEATURES",
				"BITMASK",
				"ENABLEMENT"};
	uint64_t features_enabled;
	uint32_t feature_mask[2];
	int i;
	int ret = 0;
	int size = 0;

	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	PP_ASSERT_WITH_CODE(!ret,
			"[GetPPfeatureStatus] Failed to get enabled smc features!",
			return ret);
	features_enabled = (uint64_t)feature_mask[0] |
			   (uint64_t)feature_mask[1] << 32;

	size += sprintf(buf + size, "Current ppfeatures: 0x%016llx\n", features_enabled);
	size += sprintf(buf + size, "%-19s %-22s %s\n",
				output_title[0],
				output_title[1],
				output_title[2]);
	for (i = 0; i < (sizeof(ppfeature_name) / sizeof(ppfeature_name[0])); i++) {
		size += sprintf(buf + size, "%-19s 0x%016llx %6s\n",
					ppfeature_name[i],
					1ULL << i,
					(features_enabled & (1ULL << i)) ? "Y" : "N");
	}

	return size;
}

static int navi10_enable_smc_features(struct smu_context *smu,
				      bool enabled,
				      uint64_t feature_masks)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_low, feature_high;
	uint32_t feature_mask[2];
	int ret = 0;

	feature_low = (uint32_t)(feature_masks & 0xFFFFFFFF);
	feature_high = (uint32_t)((feature_masks & 0xFFFFFFFF00000000ULL) >> 32);

	if (enabled) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesLow,
						  feature_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesHigh,
						  feature_high);
		if (ret)
			return ret;
	} else {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesLow,
						  feature_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesHigh,
						  feature_high);
		if (ret)
			return ret;
	}

	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	if (ret)
		return ret;

	mutex_lock(&feature->mutex);
	bitmap_copy(feature->enabled, (unsigned long *)&feature_mask,
		    feature->feature_num);
	mutex_unlock(&feature->mutex);

	return 0;
}

static int navi10_set_ppfeature_status(struct smu_context *smu,
				       uint64_t new_ppfeature_masks)
{
	uint64_t features_enabled;
	uint32_t feature_mask[2];
	uint64_t features_to_enable;
	uint64_t features_to_disable;
	int ret = 0;

	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	PP_ASSERT_WITH_CODE(!ret,
			"[SetPPfeatureStatus] Failed to get enabled smc features!",
			return ret);
	features_enabled = (uint64_t)feature_mask[0] |
			   (uint64_t)feature_mask[1] << 32;

	features_to_disable =
		features_enabled & ~new_ppfeature_masks;
	features_to_enable =
		~features_enabled & new_ppfeature_masks;

	pr_debug("features_to_disable 0x%llx\n", features_to_disable);
	pr_debug("features_to_enable 0x%llx\n", features_to_enable);

	if (features_to_disable) {
		ret = navi10_enable_smc_features(smu, false, features_to_disable);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetPPfeatureStatus] Failed to disable smc features!",
				return ret);
	}

	if (features_to_enable) {
		ret = navi10_enable_smc_features(smu, true, features_to_enable);
		PP_ASSERT_WITH_CODE(!ret,
				"[SetPPfeatureStatus] Failed to enable smc features!",
				return ret);
	}

	return 0;
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
	.get_workload_type = navi10_get_workload_type,
	.get_allowed_feature_mask = navi10_get_allowed_feature_mask,
	.set_default_dpm_table = navi10_set_default_dpm_table,
	.dpm_set_uvd_enable = navi10_dpm_set_uvd_enable,
	.get_current_clk_freq_by_table = navi10_get_current_clk_freq_by_table,
	.print_clk_levels = navi10_print_clk_levels,
	.force_clk_levels = navi10_force_clk_levels,
	.populate_umd_state_clk = navi10_populate_umd_state_clk,
	.get_clock_by_type_with_latency = navi10_get_clock_by_type_with_latency,
	.pre_display_config_changed = navi10_pre_display_config_changed,
	.display_config_changed = navi10_display_config_changed,
	.notify_smc_dispaly_config = navi10_notify_smc_dispaly_config,
	.force_dpm_limit_value = navi10_force_dpm_limit_value,
	.unforce_dpm_levels = navi10_unforce_dpm_levels,
	.is_dpm_running = navi10_is_dpm_running,
	.get_fan_speed_percent = navi10_get_fan_speed_percent,
	.get_power_profile_mode = navi10_get_power_profile_mode,
	.set_power_profile_mode = navi10_set_power_profile_mode,
	.get_profiling_clk_mask = navi10_get_profiling_clk_mask,
	.set_watermarks_table = navi10_set_watermarks_table,
	.read_sensor = navi10_read_sensor,
	.get_uclk_dpm_states = navi10_get_uclk_dpm_states,
	.get_ppfeature_status = navi10_get_ppfeature_status,
	.set_ppfeature_status = navi10_set_ppfeature_status,
};

void navi10_set_ppt_funcs(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	smu->ppt_funcs = &navi10_ppt_funcs;
	smu->smc_if_version = SMU11_DRIVER_IF_VERSION;
	smu_table->table_count = TABLE_COUNT;
}
