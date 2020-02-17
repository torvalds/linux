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
#include <linux/pci.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_internal.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v11_0.h"
#include "smu11_driver_if_navi10.h"
#include "soc15_common.h"
#include "atom.h"
#include "navi10_ppt.h"
#include "smu_v11_0_pptable.h"
#include "smu_v11_0_ppsmc.h"
#include "nbio/nbio_7_4_sh_mask.h"

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
	[SMU_MSG_##msg] = {1, (index)}

static struct smu_11_0_cmn2aisc_mapping navi10_message_map[SMU_MSG_MAX_COUNT] = {
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
	MSG_MAP(ArmD3,			PPSMC_MSG_ArmD3),
	MSG_MAP(DAL_DISABLE_DUMMY_PSTATE_CHANGE,PPSMC_MSG_DALDisableDummyPstateChange),
	MSG_MAP(DAL_ENABLE_DUMMY_PSTATE_CHANGE,	PPSMC_MSG_DALEnableDummyPstateChange),
	MSG_MAP(GetVoltageByDpm,		     PPSMC_MSG_GetVoltageByDpm),
	MSG_MAP(GetVoltageByDpmOverdrive,	     PPSMC_MSG_GetVoltageByDpmOverdrive),
};

static struct smu_11_0_cmn2aisc_mapping navi10_clk_map[SMU_CLK_COUNT] = {
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

static struct smu_11_0_cmn2aisc_mapping navi10_feature_mask_map[SMU_FEATURE_COUNT] = {
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
	FEA_MAP(APCC_DFLL),
};

static struct smu_11_0_cmn2aisc_mapping navi10_table_map[SMU_TABLE_COUNT] = {
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

static struct smu_11_0_cmn2aisc_mapping navi10_pwr_src_map[SMU_POWER_SOURCE_COUNT] = {
	PWR_MAP(AC),
	PWR_MAP(DC),
};

static struct smu_11_0_cmn2aisc_mapping navi10_workload_map[PP_SMC_POWER_PROFILE_COUNT] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT,	WORKLOAD_PPLIB_DEFAULT_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_FULLSCREEN3D,		WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_POWERSAVING,		WORKLOAD_PPLIB_POWER_SAVING_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VR,			WORKLOAD_PPLIB_VR_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_COMPUTE_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
};

static int navi10_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_MSG_MAX_COUNT)
		return -EINVAL;

	mapping = navi10_message_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int navi10_get_smu_clk_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_CLK_COUNT)
		return -EINVAL;

	mapping = navi10_clk_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int navi10_get_smu_feature_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_FEATURE_COUNT)
		return -EINVAL;

	mapping = navi10_feature_mask_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int navi10_get_smu_table_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_TABLE_COUNT)
		return -EINVAL;

	mapping = navi10_table_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int navi10_get_pwr_src_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_POWER_SOURCE_COUNT)
		return -EINVAL;

	mapping = navi10_pwr_src_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}


static int navi10_get_workload_type(struct smu_context *smu, enum PP_SMC_POWER_PROFILE profile)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (profile > PP_SMC_POWER_PROFILE_CUSTOM)
		return -EINVAL;

	mapping = navi10_workload_map[profile];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
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
				| FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT)
				| FEATURE_MASK(FEATURE_RSMU_SMN_CG_BIT)
				| FEATURE_MASK(FEATURE_DS_SOCCLK_BIT)
				| FEATURE_MASK(FEATURE_PPT_BIT)
				| FEATURE_MASK(FEATURE_TDC_BIT)
				| FEATURE_MASK(FEATURE_GFX_EDC_BIT)
				| FEATURE_MASK(FEATURE_APCC_PLUS_BIT)
				| FEATURE_MASK(FEATURE_VR0HOT_BIT)
				| FEATURE_MASK(FEATURE_FAN_CONTROL_BIT)
				| FEATURE_MASK(FEATURE_THERMAL_BIT)
				| FEATURE_MASK(FEATURE_LED_DISPLAY_BIT)
				| FEATURE_MASK(FEATURE_DS_LCLK_BIT)
				| FEATURE_MASK(FEATURE_DS_DCEFCLK_BIT)
				| FEATURE_MASK(FEATURE_FW_DSTATE_BIT)
				| FEATURE_MASK(FEATURE_BACO_BIT)
				| FEATURE_MASK(FEATURE_ACDC_BIT)
				| FEATURE_MASK(FEATURE_GFX_SS_BIT)
				| FEATURE_MASK(FEATURE_APCC_DFLL_BIT)
				| FEATURE_MASK(FEATURE_FW_CTF_BIT)
				| FEATURE_MASK(FEATURE_OUT_OF_BAND_MONITOR_BIT);

	if (adev->pm.pp_feature & PP_SOCCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT);

	if (adev->pm.pp_feature & PP_PCIE_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_LINK_BIT);

	if (adev->pm.pp_feature & PP_DCEFCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT);

	if (adev->pm.pp_feature & PP_MCLK_DPM_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_UCLK_BIT)
				| FEATURE_MASK(FEATURE_MEM_VDDCI_SCALING_BIT)
				| FEATURE_MASK(FEATURE_MEM_MVDD_SCALING_BIT);

	if (adev->pm.pp_feature & PP_ULV_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFX_ULV_BIT);

	if (adev->pm.pp_feature & PP_SCLK_DEEP_SLEEP_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DS_GFXCLK_BIT);

	if (adev->pm.pp_feature & PP_GFXOFF_MASK)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_GFXOFF_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_MMHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_MMHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_ATHUB)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_ATHUB_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_VCN)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_VCN_PG_BIT);

	if (smu->adev->pg_flags & AMD_PG_SUPPORT_JPEG)
		*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_JPEG_PG_BIT);

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
		/* TODO: remove it once SMU fw fix it */
		smc_pptable->DebugOverrides |= DPM_OVERRIDE_DISABLE_DFLL_PLL_SHUTDOWN;
	}

	return 0;
}

static int navi10_store_powerplay_table(struct smu_context *smu)
{
	struct smu_11_0_powerplay_table *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	if (!table_context->power_play_table)
		return -EINVAL;

	powerplay_table = table_context->power_play_table;

	memcpy(table_context->driver_pptable, &powerplay_table->smc_pptable,
	       sizeof(PPTable_t));

	table_context->thermal_controller_type = powerplay_table->thermal_controller_type;

	mutex_lock(&smu_baco->mutex);
	if (powerplay_table->platform_caps & SMU_11_0_PP_PLATFORM_CAP_BACO ||
	    powerplay_table->platform_caps & SMU_11_0_PP_PLATFORM_CAP_MACO)
		smu_baco->platform_support = true;
	mutex_unlock(&smu_baco->mutex);

	return 0;
}

static int navi10_tables_init(struct smu_context *smu, struct smu_table *tables)
{
	struct smu_table_context *smu_table = &smu->smu_table;

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

	smu_table->metrics_table = kzalloc(sizeof(SmuMetrics_t), GFP_KERNEL);
	if (!smu_table->metrics_table)
		return -ENOMEM;
	smu_table->metrics_time = 0;

	smu_table->watermarks_table = kzalloc(sizeof(Watermarks_t), GFP_KERNEL);
	if (!smu_table->watermarks_table)
		return -ENOMEM;

	return 0;
}

static int navi10_get_metrics_table(struct smu_context *smu,
				    SmuMetrics_t *metrics_table)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	int ret = 0;

	mutex_lock(&smu->metrics_lock);
	if (!smu_table->metrics_time || time_after(jiffies, smu_table->metrics_time + msecs_to_jiffies(100))) {
		ret = smu_update_table(smu, SMU_TABLE_SMU_METRICS, 0,
				(void *)smu_table->metrics_table, false);
		if (ret) {
			pr_info("Failed to export SMU metrics table!\n");
			mutex_unlock(&smu->metrics_lock);
			return ret;
		}
		smu_table->metrics_time = jiffies;
	}

	memcpy(metrics_table, smu_table->metrics_table, sizeof(SmuMetrics_t));
	mutex_unlock(&smu->metrics_lock);

	return ret;
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
	int i;

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

	for (i = 0; i < MAX_PCIE_CONF; i++) {
		dpm_context->dpm_tables.pcie_table.pcie_gen[i] = driver_ppt->PcieGenSpeed[i];
		dpm_context->dpm_tables.pcie_table.pcie_lane[i] = driver_ppt->PcieLaneCount[i];
	}

	return 0;
}

static int navi10_dpm_set_uvd_enable(struct smu_context *smu, bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (enable) {
		/* vcn dpm on is a prerequisite for vcn power gate messages */
		if (smu_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpVcn, 1);
			if (ret)
				return ret;
		}
		power_gate->vcn_gated = false;
	} else {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_VCN_PG_BIT)) {
			ret = smu_send_smc_msg(smu, SMU_MSG_PowerDownVcn);
			if (ret)
				return ret;
		}
		power_gate->vcn_gated = true;
	}

	return ret;
}

static int navi10_dpm_set_jpeg_enable(struct smu_context *smu, bool enable)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_power_gate *power_gate = &smu_power->power_gate;
	int ret = 0;

	if (enable) {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_JPEG_PG_BIT)) {
			ret = smu_send_smc_msg(smu, SMU_MSG_PowerUpJpeg);
			if (ret)
				return ret;
		}
		power_gate->jpeg_gated = false;
	} else {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_JPEG_PG_BIT)) {
			ret = smu_send_smc_msg(smu, SMU_MSG_PowerDownJpeg);
			if (ret)
				return ret;
		}
		power_gate->jpeg_gated = true;
	}

	return ret;
}

static int navi10_get_current_clk_freq_by_table(struct smu_context *smu,
				       enum smu_clk_type clk_type,
				       uint32_t *value)
{
	int ret = 0, clk_id = 0;
	SmuMetrics_t metrics;

	ret = navi10_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	*value = metrics.CurrClock[clk_id];

	return ret;
}

static bool navi10_is_support_fine_grained_dpm(struct smu_context *smu, enum smu_clk_type clk_type)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	DpmDescriptor_t *dpm_desc = NULL;
	uint32_t clk_index = 0;

	clk_index = smu_clk_get_index(smu, clk_type);
	dpm_desc = &pptable->DpmDescriptor[clk_index];

	/* 0 - Fine grained DPM, 1 - Discrete DPM */
	return dpm_desc->SnapToDiscrete == 0 ? true : false;
}

static inline bool navi10_od_feature_is_supported(struct smu_11_0_overdrive_table *od_table, enum SMU_11_0_ODFEATURE_CAP cap)
{
	return od_table->cap[cap];
}

static void navi10_od_setting_get_range(struct smu_11_0_overdrive_table *od_table,
					enum SMU_11_0_ODSETTING_ID setting,
					uint32_t *min, uint32_t *max)
{
	if (min)
		*min = od_table->min[setting];
	if (max)
		*max = od_table->max[setting];
}

static int navi10_print_clk_levels(struct smu_context *smu,
			enum smu_clk_type clk_type, char *buf)
{
	uint16_t *curve_settings;
	int i, size = 0, ret = 0;
	uint32_t cur_value = 0, value = 0, count = 0;
	uint32_t freq_values[3] = {0};
	uint32_t mark_index = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	uint32_t gen_speed, lane_width;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	struct amdgpu_device *adev = smu->adev;
	PPTable_t *pptable = (PPTable_t *)table_context->driver_pptable;
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)table_context->overdrive_table;
	struct smu_11_0_overdrive_table *od_settings = smu->od_settings;
	uint32_t min_value, max_value;

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

		ret = smu_get_dpm_level_count(smu, clk_type, &count);
		if (ret)
			return size;

		if (!navi10_is_support_fine_grained_dpm(smu, clk_type)) {
			for (i = 0; i < count; i++) {
				ret = smu_get_dpm_freq_by_index(smu, clk_type, i, &value);
				if (ret)
					return size;

				size += sprintf(buf + size, "%d: %uMhz %s\n", i, value,
						cur_value == value ? "*" : "");
			}
		} else {
			ret = smu_get_dpm_freq_by_index(smu, clk_type, 0, &freq_values[0]);
			if (ret)
				return size;
			ret = smu_get_dpm_freq_by_index(smu, clk_type, count - 1, &freq_values[2]);
			if (ret)
				return size;

			freq_values[1] = cur_value;
			mark_index = cur_value == freq_values[0] ? 0 :
				     cur_value == freq_values[2] ? 2 : 1;
			if (mark_index != 1)
				freq_values[1] = (freq_values[0] + freq_values[2]) / 2;

			for (i = 0; i < 3; i++) {
				size += sprintf(buf + size, "%d: %uMhz %s\n", i, freq_values[i],
						i == mark_index ? "*" : "");
			}

		}
		break;
	case SMU_PCIE:
		gen_speed = (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
			     PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
			>> PSWUSP0_PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
		lane_width = (RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL) &
			      PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
			>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;
		for (i = 0; i < NUM_LINK_LEVELS; i++)
			size += sprintf(buf + size, "%d: %s %s %dMhz %s\n", i,
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 0) ? "2.5GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 1) ? "5.0GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 2) ? "8.0GT/s," :
					(dpm_context->dpm_tables.pcie_table.pcie_gen[i] == 3) ? "16.0GT/s," : "",
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 1) ? "x1" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 2) ? "x2" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 3) ? "x4" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 4) ? "x8" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 5) ? "x12" :
					(dpm_context->dpm_tables.pcie_table.pcie_lane[i] == 6) ? "x16" : "",
					pptable->LclkFreq[i],
					(gen_speed == dpm_context->dpm_tables.pcie_table.pcie_gen[i]) &&
					(lane_width == dpm_context->dpm_tables.pcie_table.pcie_lane[i]) ?
					"*" : "");
		break;
	case SMU_OD_SCLK:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;
		if (!navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_GFXCLK_LIMITS))
			break;
		size += sprintf(buf + size, "OD_SCLK:\n");
		size += sprintf(buf + size, "0: %uMhz\n1: %uMhz\n", od_table->GfxclkFmin, od_table->GfxclkFmax);
		break;
	case SMU_OD_MCLK:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;
		if (!navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_UCLK_MAX))
			break;
		size += sprintf(buf + size, "OD_MCLK:\n");
		size += sprintf(buf + size, "1: %uMHz\n", od_table->UclkFmax);
		break;
	case SMU_OD_VDDC_CURVE:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;
		if (!navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_GFXCLK_CURVE))
			break;
		size += sprintf(buf + size, "OD_VDDC_CURVE:\n");
		for (i = 0; i < 3; i++) {
			switch (i) {
			case 0:
				curve_settings = &od_table->GfxclkFreq1;
				break;
			case 1:
				curve_settings = &od_table->GfxclkFreq2;
				break;
			case 2:
				curve_settings = &od_table->GfxclkFreq3;
				break;
			default:
				break;
			}
			size += sprintf(buf + size, "%d: %uMHz @ %umV\n", i, curve_settings[0], curve_settings[1] / NAVI10_VOLTAGE_SCALE);
		}
		break;
	case SMU_OD_RANGE:
		if (!smu->od_enabled || !od_table || !od_settings)
			break;
		size = sprintf(buf, "%s:\n", "OD_RANGE");

		if (navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_GFXCLK_LIMITS)) {
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_GFXCLKFMIN,
						    &min_value, NULL);
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_GFXCLKFMAX,
						    NULL, &max_value);
			size += sprintf(buf + size, "SCLK: %7uMhz %10uMhz\n",
					min_value, max_value);
		}

		if (navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_UCLK_MAX)) {
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_UCLKFMAX,
						    &min_value, &max_value);
			size += sprintf(buf + size, "MCLK: %7uMhz %10uMhz\n",
					min_value, max_value);
		}

		if (navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_GFXCLK_CURVE)) {
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P1,
						    &min_value, &max_value);
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[0]: %7uMhz %10uMhz\n",
					min_value, max_value);
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P1,
						    &min_value, &max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[0]: %7dmV %11dmV\n",
					min_value, max_value);
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P2,
						    &min_value, &max_value);
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[1]: %7uMhz %10uMhz\n",
					min_value, max_value);
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P2,
						    &min_value, &max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[1]: %7dmV %11dmV\n",
					min_value, max_value);
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P3,
						    &min_value, &max_value);
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[2]: %7uMhz %10uMhz\n",
					min_value, max_value);
			navi10_od_setting_get_range(od_settings, SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P3,
						    &min_value, &max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[2]: %7dmV %11dmV\n",
					min_value, max_value);
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
		/* There is only 2 levels for fine grained DPM */
		if (navi10_is_support_fine_grained_dpm(smu, clk_type)) {
			soft_max_level = (soft_max_level >= 1 ? 1 : 0);
			soft_min_level = (soft_min_level >= 1 ? 1 : 0);
		}

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
	uint32_t min_sclk_freq = 0, min_mclk_freq = 0;

	ret = smu_get_dpm_freq_range(smu, SMU_SCLK, &min_sclk_freq, NULL, false);
	if (ret)
		return ret;

	smu->pstate_sclk = min_sclk_freq * 100;

	ret = smu_get_dpm_freq_range(smu, SMU_MCLK, &min_mclk_freq, NULL, false);
	if (ret)
		return ret;

	smu->pstate_mclk = min_mclk_freq * 100;

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
	case SMU_MCLK:
	case SMU_UCLK:
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
		ret = smu_get_dpm_freq_range(smu, SMU_UCLK, NULL, &max_freq, false);
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
		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq, false);
		if (ret)
			return ret;

		force_freq = highest ? max_freq : min_freq;
		ret = smu_set_soft_freq_range(smu, clk_type, force_freq, force_freq);
		if (ret)
			return ret;
	}

	return ret;
}

static int navi10_unforce_dpm_levels(struct smu_context *smu)
{
	int ret = 0, i = 0;
	uint32_t min_freq, max_freq;
	enum smu_clk_type clk_type;

	enum smu_clk_type clks[] = {
		SMU_GFXCLK,
		SMU_MCLK,
		SMU_SOCCLK,
	};

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		clk_type = clks[i];
		ret = smu_get_dpm_freq_range(smu, clk_type, &min_freq, &max_freq, false);
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

	ret = navi10_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	*value = metrics.AverageSocketPower << 8;

	return 0;
}

static int navi10_get_current_activity_percent(struct smu_context *smu,
					       enum amd_pp_sensors sensor,
					       uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = navi10_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		*value = metrics.AverageGfxActivity;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		*value = metrics.AverageUclkActivity;
		break;
	default:
		pr_err("Invalid sensor for retrieving clock activity\n");
		return -EINVAL;
	}

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

static int navi10_get_fan_speed_rpm(struct smu_context *smu,
				    uint32_t *speed)
{
	SmuMetrics_t metrics;
	int ret = 0;

	if (!speed)
		return -EINVAL;

	ret = navi10_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	*speed = metrics.CurrFanSpeed;

	return ret;
}

static int navi10_get_fan_speed_percent(struct smu_context *smu,
					uint32_t *speed)
{
	int ret = 0;
	uint32_t percent = 0;
	uint32_t current_rpm;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	ret = navi10_get_fan_speed_rpm(smu, &current_rpm);
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
	int16_t workload_type = 0;
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
		if (workload_type < 0)
			return -EINVAL;

		result = smu_update_table(smu,
					  SMU_TABLE_ACTIVITY_MONITOR_COEFF, workload_type,
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
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
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
				       SMU_TABLE_ACTIVITY_MONITOR_COEFF, WORKLOAD_PPLIB_CUSTOM_BIT,
				       (void *)(&activity_monitor), true);
		if (ret) {
			pr_err("[%s] Failed to set activity monitor!", __func__);
			return ret;
		}
	}

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type = smu_workload_get_type(smu, smu->power_profile_mode);
	if (workload_type < 0)
		return -EINVAL;
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
			*mclk_mask = level_count - 1;
		}

		if(soc_mask) {
			ret = smu_get_dpm_level_count(smu, SMU_SOCCLK, &level_count);
			if (ret)
				return ret;
			*soc_mask = level_count - 1;
		}
	}

	return ret;
}

static int navi10_notify_smc_display_config(struct smu_context *smu)
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

		ret = smu_v11_0_display_clock_voltage_request(smu, &clock_req);
		if (!ret) {
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

static int navi10_thermal_get_temperature(struct smu_context *smu,
					     enum amd_pp_sensors sensor,
					     uint32_t *value)
{
	SmuMetrics_t metrics;
	int ret = 0;

	if (!value)
		return -EINVAL;

	ret = navi10_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		*value = metrics.TemperatureHotspot *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		*value = metrics.TemperatureEdge *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		*value = metrics.TemperatureMem *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	default:
		pr_err("Invalid sensor for retrieving temp\n");
		return -EINVAL;
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

	if(!data || !size)
		return -EINVAL;

	mutex_lock(&smu->sensor_lock);
	switch (sensor) {
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*(uint32_t *)data = pptable->FanMaximumRpm;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = navi10_get_current_activity_percent(smu, sensor, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = navi10_get_gpu_power(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = navi10_thermal_get_temperature(smu, sensor, (uint32_t *)data);
		*size = 4;
		break;
	default:
		ret = smu_v11_0_read_sensor(smu, sensor, data, size);
	}
	mutex_unlock(&smu->sensor_lock);

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

static int navi10_set_performance_level(struct smu_context *smu,
					enum amd_dpm_forced_level level);

static int navi10_set_standard_performance_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;
	uint32_t sclk_freq = 0, uclk_freq = 0;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
		sclk_freq = NAVI10_UMD_PSTATE_PROFILING_GFXCLK;
		uclk_freq = NAVI10_UMD_PSTATE_PROFILING_MEMCLK;
		break;
	case CHIP_NAVI14:
		sclk_freq = NAVI14_UMD_PSTATE_PROFILING_GFXCLK;
		uclk_freq = NAVI14_UMD_PSTATE_PROFILING_MEMCLK;
		break;
	default:
		/* by default, this is same as auto performance level */
		return navi10_set_performance_level(smu, AMD_DPM_FORCED_LEVEL_AUTO);
	}

	ret = smu_set_soft_freq_range(smu, SMU_SCLK, sclk_freq, sclk_freq);
	if (ret)
		return ret;
	ret = smu_set_soft_freq_range(smu, SMU_UCLK, uclk_freq, uclk_freq);
	if (ret)
		return ret;

	return ret;
}

static int navi10_set_peak_performance_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;
	uint32_t sclk_freq = 0, uclk_freq = 0;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
		switch (adev->pdev->revision) {
		case 0xf0: /* XTX */
		case 0xc0:
			sclk_freq = NAVI10_PEAK_SCLK_XTX;
			break;
		case 0xf1: /* XT */
		case 0xc1:
			sclk_freq = NAVI10_PEAK_SCLK_XT;
			break;
		default: /* XL */
			sclk_freq = NAVI10_PEAK_SCLK_XL;
			break;
		}
		break;
	case CHIP_NAVI14:
		switch (adev->pdev->revision) {
		case 0xc7: /* XT */
		case 0xf4:
			sclk_freq = NAVI14_UMD_PSTATE_PEAK_XT_GFXCLK;
			break;
		case 0xc1: /* XTM */
		case 0xf2:
			sclk_freq = NAVI14_UMD_PSTATE_PEAK_XTM_GFXCLK;
			break;
		case 0xc3: /* XLM */
		case 0xf3:
			sclk_freq = NAVI14_UMD_PSTATE_PEAK_XLM_GFXCLK;
			break;
		case 0xc5: /* XTX */
		case 0xf6:
			sclk_freq = NAVI14_UMD_PSTATE_PEAK_XLM_GFXCLK;
			break;
		default: /* XL */
			sclk_freq = NAVI14_UMD_PSTATE_PEAK_XL_GFXCLK;
			break;
		}
		break;
	case CHIP_NAVI12:
		sclk_freq = NAVI12_UMD_PSTATE_PEAK_GFXCLK;
		break;
	default:
		ret = smu_get_dpm_level_range(smu, SMU_SCLK, NULL, &sclk_freq);
		if (ret)
			return ret;
	}

	ret = smu_get_dpm_level_range(smu, SMU_UCLK, NULL, &uclk_freq);
	if (ret)
		return ret;

	ret = smu_set_soft_freq_range(smu, SMU_SCLK, sclk_freq, sclk_freq);
	if (ret)
		return ret;
	ret = smu_set_soft_freq_range(smu, SMU_UCLK, uclk_freq, uclk_freq);
	if (ret)
		return ret;

	return ret;
}

static int navi10_set_performance_level(struct smu_context *smu,
					enum amd_dpm_forced_level level)
{
	int ret = 0;
	uint32_t sclk_mask, mclk_mask, soc_mask;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = smu_force_dpm_limit_value(smu, true);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = smu_force_dpm_limit_value(smu, false);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = smu_unforce_dpm_levels(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		ret = navi10_set_standard_performance_level(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
		ret = smu_get_profiling_clk_mask(smu, level,
						 &sclk_mask,
						 &mclk_mask,
						 &soc_mask);
		if (ret)
			return ret;
		smu_force_clk_levels(smu, SMU_SCLK, 1 << sclk_mask, false);
		smu_force_clk_levels(smu, SMU_MCLK, 1 << mclk_mask, false);
		smu_force_clk_levels(smu, SMU_SOCCLK, 1 << soc_mask, false);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = navi10_set_peak_performance_level(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}
	return ret;
}

static int navi10_get_thermal_temperature_range(struct smu_context *smu,
						struct smu_temperature_range *range)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_powerplay_table *powerplay_table = table_context->power_play_table;

	if (!range || !powerplay_table)
		return -EINVAL;

	range->max = powerplay_table->software_shutdown_temp *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int navi10_display_disable_memory_clock_switch(struct smu_context *smu,
						bool disable_memory_clock_switch)
{
	int ret = 0;
	struct smu_11_0_max_sustainable_clocks *max_sustainable_clocks =
		(struct smu_11_0_max_sustainable_clocks *)
			smu->smu_table.max_sustainable_clocks;
	uint32_t min_memory_clock = smu->hard_min_uclk_req_from_dal;
	uint32_t max_memory_clock = max_sustainable_clocks->uclock;

	if(smu->disable_uclk_switch == disable_memory_clock_switch)
		return 0;

	if(disable_memory_clock_switch)
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, max_memory_clock, 0);
	else
		ret = smu_set_hard_freq_range(smu, SMU_UCLK, min_memory_clock, 0);

	if(!ret)
		smu->disable_uclk_switch = disable_memory_clock_switch;

	return ret;
}

static uint32_t navi10_get_pptable_power_limit(struct smu_context *smu)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	return pptable->SocketPowerLimitAc[PPT_THROTTLER_PPT0];
}

static int navi10_get_power_limit(struct smu_context *smu,
				     uint32_t *limit,
				     bool cap)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	uint32_t asic_default_power_limit = 0;
	int ret = 0;
	int power_src;

	if (!smu->power_limit) {
		if (smu_feature_is_enabled(smu, SMU_FEATURE_PPT_BIT)) {
			power_src = smu_power_get_index(smu, SMU_POWER_SOURCE_AC);
			if (power_src < 0)
				return -EINVAL;

			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetPptLimit,
				power_src << 16);
			if (ret) {
				pr_err("[%s] get PPT limit failed!", __func__);
				return ret;
			}
			smu_read_smc_arg(smu, &asic_default_power_limit);
		} else {
			/* the last hope to figure out the ppt limit */
			if (!pptable) {
				pr_err("Cannot get PPT limit due to pptable missing!");
				return -EINVAL;
			}
			asic_default_power_limit =
				pptable->SocketPowerLimitAc[PPT_THROTTLER_PPT0];
		}

		smu->power_limit = asic_default_power_limit;
	}

	if (cap)
		*limit = smu_v11_0_get_max_power_limit(smu);
	else
		*limit = smu->power_limit;

	return 0;
}

static int navi10_update_pcie_parameters(struct smu_context *smu,
				     uint32_t pcie_gen_cap,
				     uint32_t pcie_width_cap)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	int ret, i;
	uint32_t smu_pcie_arg;

	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_11_0_dpm_context *dpm_context = smu_dpm->dpm_context;

	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		smu_pcie_arg = (i << 16) |
			((pptable->PcieGenSpeed[i] <= pcie_gen_cap) ? (pptable->PcieGenSpeed[i] << 8) :
				(pcie_gen_cap << 8)) | ((pptable->PcieLaneCount[i] <= pcie_width_cap) ?
					pptable->PcieLaneCount[i] : pcie_width_cap);
		ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_OverridePcieParameters,
					  smu_pcie_arg);

		if (ret)
			return ret;

		if (pptable->PcieGenSpeed[i] > pcie_gen_cap)
			dpm_context->dpm_tables.pcie_table.pcie_gen[i] = pcie_gen_cap;
		if (pptable->PcieLaneCount[i] > pcie_width_cap)
			dpm_context->dpm_tables.pcie_table.pcie_lane[i] = pcie_width_cap;
	}

	return 0;
}

static inline void navi10_dump_od_table(OverDriveTable_t *od_table) {
	pr_debug("OD: Gfxclk: (%d, %d)\n", od_table->GfxclkFmin, od_table->GfxclkFmax);
	pr_debug("OD: Gfx1: (%d, %d)\n", od_table->GfxclkFreq1, od_table->GfxclkVolt1);
	pr_debug("OD: Gfx2: (%d, %d)\n", od_table->GfxclkFreq2, od_table->GfxclkVolt2);
	pr_debug("OD: Gfx3: (%d, %d)\n", od_table->GfxclkFreq3, od_table->GfxclkVolt3);
	pr_debug("OD: UclkFmax: %d\n", od_table->UclkFmax);
	pr_debug("OD: OverDrivePct: %d\n", od_table->OverDrivePct);
}

static int navi10_od_setting_check_range(struct smu_11_0_overdrive_table *od_table, enum SMU_11_0_ODSETTING_ID setting, uint32_t value)
{
	if (value < od_table->min[setting]) {
		pr_warn("OD setting (%d, %d) is less than the minimum allowed (%d)\n", setting, value, od_table->min[setting]);
		return -EINVAL;
	}
	if (value > od_table->max[setting]) {
		pr_warn("OD setting (%d, %d) is greater than the maximum allowed (%d)\n", setting, value, od_table->max[setting]);
		return -EINVAL;
	}
	return 0;
}

static int navi10_overdrive_get_gfx_clk_base_voltage(struct smu_context *smu,
						     uint16_t *voltage,
						     uint32_t freq)
{
	uint32_t param = (freq & 0xFFFF) | (PPCLK_GFXCLK << 16);
	uint32_t value = 0;
	int ret;

	ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_GetVoltageByDpm,
					  param);
	if (ret) {
		pr_err("[GetBaseVoltage] failed to get GFXCLK AVFS voltage from SMU!");
		return ret;
	}

	smu_read_smc_arg(smu, &value);
	*voltage = (uint16_t)value;

	return 0;
}

static int navi10_setup_od_limits(struct smu_context *smu) {
	struct smu_11_0_overdrive_table *overdrive_table = NULL;
	struct smu_11_0_powerplay_table *powerplay_table = NULL;

	if (!smu->smu_table.power_play_table) {
		pr_err("powerplay table uninitialized!\n");
		return -ENOENT;
	}
	powerplay_table = (struct smu_11_0_powerplay_table *)smu->smu_table.power_play_table;
	overdrive_table = &powerplay_table->overdrive_table;
	if (!smu->od_settings) {
		smu->od_settings = kmemdup(overdrive_table, sizeof(struct smu_11_0_overdrive_table), GFP_KERNEL);
	} else {
		memcpy(smu->od_settings, overdrive_table, sizeof(struct smu_11_0_overdrive_table));
	}
	return 0;
}

static int navi10_set_default_od_settings(struct smu_context *smu, bool initialize) {
	OverDriveTable_t *od_table, *boot_od_table;
	int ret = 0;

	ret = smu_v11_0_set_default_od_settings(smu, initialize, sizeof(OverDriveTable_t));
	if (ret)
		return ret;

	od_table = (OverDriveTable_t *)smu->smu_table.overdrive_table;
	boot_od_table = (OverDriveTable_t *)smu->smu_table.boot_overdrive_table;
	if (initialize) {
		ret = navi10_setup_od_limits(smu);
		if (ret) {
			pr_err("Failed to retrieve board OD limits\n");
			return ret;
		}
		if (od_table) {
			if (!od_table->GfxclkVolt1) {
				ret = navi10_overdrive_get_gfx_clk_base_voltage(smu,
										&od_table->GfxclkVolt1,
										od_table->GfxclkFreq1);
				if (ret)
					od_table->GfxclkVolt1 = 0;
				if (boot_od_table)
					boot_od_table->GfxclkVolt1 = od_table->GfxclkVolt1;
			}

			if (!od_table->GfxclkVolt2) {
				ret = navi10_overdrive_get_gfx_clk_base_voltage(smu,
										&od_table->GfxclkVolt2,
										od_table->GfxclkFreq2);
				if (ret)
					od_table->GfxclkVolt2 = 0;
				if (boot_od_table)
					boot_od_table->GfxclkVolt2 = od_table->GfxclkVolt2;
			}

			if (!od_table->GfxclkVolt3) {
				ret = navi10_overdrive_get_gfx_clk_base_voltage(smu,
										&od_table->GfxclkVolt3,
										od_table->GfxclkFreq3);
				if (ret)
					od_table->GfxclkVolt3 = 0;
				if (boot_od_table)
					boot_od_table->GfxclkVolt3 = od_table->GfxclkVolt3;
			}
		}
	}

	if (od_table) {
		navi10_dump_od_table(od_table);
	}

	return ret;
}

static int navi10_od_edit_dpm_table(struct smu_context *smu, enum PP_OD_DPM_TABLE_COMMAND type, long input[], uint32_t size) {
	int i;
	int ret = 0;
	struct smu_table_context *table_context = &smu->smu_table;
	OverDriveTable_t *od_table;
	struct smu_11_0_overdrive_table *od_settings;
	enum SMU_11_0_ODSETTING_ID freq_setting, voltage_setting;
	uint16_t *freq_ptr, *voltage_ptr;
	od_table = (OverDriveTable_t *)table_context->overdrive_table;

	if (!smu->od_enabled) {
		pr_warn("OverDrive is not enabled!\n");
		return -EINVAL;
	}

	if (!smu->od_settings) {
		pr_err("OD board limits are not set!\n");
		return -ENOENT;
	}

	od_settings = smu->od_settings;

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (!navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_GFXCLK_LIMITS)) {
			pr_warn("GFXCLK_LIMITS not supported!\n");
			return -ENOTSUPP;
		}
		if (!table_context->overdrive_table) {
			pr_err("Overdrive is not initialized\n");
			return -EINVAL;
		}
		for (i = 0; i < size; i += 2) {
			if (i + 2 > size) {
				pr_info("invalid number of input parameters %d\n", size);
				return -EINVAL;
			}
			switch (input[i]) {
			case 0:
				freq_setting = SMU_11_0_ODSETTING_GFXCLKFMIN;
				freq_ptr = &od_table->GfxclkFmin;
				if (input[i + 1] > od_table->GfxclkFmax) {
					pr_info("GfxclkFmin (%ld) must be <= GfxclkFmax (%u)!\n",
						input[i + 1],
						od_table->GfxclkFmin);
					return -EINVAL;
				}
				break;
			case 1:
				freq_setting = SMU_11_0_ODSETTING_GFXCLKFMAX;
				freq_ptr = &od_table->GfxclkFmax;
				if (input[i + 1] < od_table->GfxclkFmin) {
					pr_info("GfxclkFmax (%ld) must be >= GfxclkFmin (%u)!\n",
						input[i + 1],
						od_table->GfxclkFmax);
					return -EINVAL;
				}
				break;
			default:
				pr_info("Invalid SCLK_VDDC_TABLE index: %ld\n", input[i]);
				pr_info("Supported indices: [0:min,1:max]\n");
				return -EINVAL;
			}
			ret = navi10_od_setting_check_range(od_settings, freq_setting, input[i + 1]);
			if (ret)
				return ret;
			*freq_ptr = input[i + 1];
		}
		break;
	case PP_OD_EDIT_MCLK_VDDC_TABLE:
		if (!navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_UCLK_MAX)) {
			pr_warn("UCLK_MAX not supported!\n");
			return -ENOTSUPP;
		}
		if (size < 2) {
			pr_info("invalid number of parameters: %d\n", size);
			return -EINVAL;
		}
		if (input[0] != 1) {
			pr_info("Invalid MCLK_VDDC_TABLE index: %ld\n", input[0]);
			pr_info("Supported indices: [1:max]\n");
			return -EINVAL;
		}
		ret = navi10_od_setting_check_range(od_settings, SMU_11_0_ODSETTING_UCLKFMAX, input[1]);
		if (ret)
			return ret;
		od_table->UclkFmax = input[1];
		break;
	case PP_OD_RESTORE_DEFAULT_TABLE:
		if (!(table_context->overdrive_table && table_context->boot_overdrive_table)) {
			pr_err("Overdrive table was not initialized!\n");
			return -EINVAL;
		}
		memcpy(table_context->overdrive_table, table_context->boot_overdrive_table, sizeof(OverDriveTable_t));
		break;
	case PP_OD_COMMIT_DPM_TABLE:
		navi10_dump_od_table(od_table);
		ret = smu_update_table(smu, SMU_TABLE_OVERDRIVE, 0, (void *)od_table, true);
		if (ret) {
			pr_err("Failed to import overdrive table!\n");
			return ret;
		}
		// no lock needed because smu_od_edit_dpm_table has it
		ret = smu_handle_task(smu, smu->smu_dpm.dpm_level,
			AMD_PP_TASK_READJUST_POWER_STATE,
			false);
		if (ret) {
			return ret;
		}
		break;
	case PP_OD_EDIT_VDDC_CURVE:
		if (!navi10_od_feature_is_supported(od_settings, SMU_11_0_ODCAP_GFXCLK_CURVE)) {
			pr_warn("GFXCLK_CURVE not supported!\n");
			return -ENOTSUPP;
		}
		if (size < 3) {
			pr_info("invalid number of parameters: %d\n", size);
			return -EINVAL;
		}
		if (!od_table) {
			pr_info("Overdrive is not initialized\n");
			return -EINVAL;
		}

		switch (input[0]) {
		case 0:
			freq_setting = SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P1;
			voltage_setting = SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P1;
			freq_ptr = &od_table->GfxclkFreq1;
			voltage_ptr = &od_table->GfxclkVolt1;
			break;
		case 1:
			freq_setting = SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P2;
			voltage_setting = SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P2;
			freq_ptr = &od_table->GfxclkFreq2;
			voltage_ptr = &od_table->GfxclkVolt2;
			break;
		case 2:
			freq_setting = SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P3;
			voltage_setting = SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P3;
			freq_ptr = &od_table->GfxclkFreq3;
			voltage_ptr = &od_table->GfxclkVolt3;
			break;
		default:
			pr_info("Invalid VDDC_CURVE index: %ld\n", input[0]);
			pr_info("Supported indices: [0, 1, 2]\n");
			return -EINVAL;
		}
		ret = navi10_od_setting_check_range(od_settings, freq_setting, input[1]);
		if (ret)
			return ret;
		// Allow setting zero to disable the OverDrive VDDC curve
		if (input[2] != 0) {
			ret = navi10_od_setting_check_range(od_settings, voltage_setting, input[2]);
			if (ret)
				return ret;
			*freq_ptr = input[1];
			*voltage_ptr = ((uint16_t)input[2]) * NAVI10_VOLTAGE_SCALE;
			pr_debug("OD: set curve %ld: (%d, %d)\n", input[0], *freq_ptr, *voltage_ptr);
		} else {
			// If setting 0, disable all voltage curve settings
			od_table->GfxclkVolt1 = 0;
			od_table->GfxclkVolt2 = 0;
			od_table->GfxclkVolt3 = 0;
		}
		navi10_dump_od_table(od_table);
		break;
	default:
		return -ENOSYS;
	}
	return ret;
}

static int navi10_run_btc(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_send_smc_msg(smu, SMU_MSG_RunBtc);
	if (ret)
		pr_err("RunBtc failed!\n");

	return ret;
}

static int navi10_dummy_pstate_control(struct smu_context *smu, bool enable)
{
	int result = 0;

	if (!enable)
		result = smu_send_smc_msg(smu, SMU_MSG_DAL_DISABLE_DUMMY_PSTATE_CHANGE);
	else
		result = smu_send_smc_msg(smu, SMU_MSG_DAL_ENABLE_DUMMY_PSTATE_CHANGE);

	return result;
}

static int navi10_disable_umc_cdr_12gbps_workaround(struct smu_context *smu)
{
	uint32_t uclk_count, uclk_min, uclk_max;
	uint32_t smu_version;
	int ret = 0;

	ret = smu_get_smc_version(smu, NULL, &smu_version);
	if (ret)
		return ret;

	/* This workaround is available only for 42.50 or later SMC firmwares */
	if (smu_version < 0x2A3200)
		return 0;

	ret = smu_get_dpm_level_count(smu, SMU_UCLK, &uclk_count);
	if (ret)
		return ret;

	ret = smu_get_dpm_freq_by_index(smu, SMU_UCLK, (uint16_t)0, &uclk_min);
	if (ret)
		return ret;

	ret = smu_get_dpm_freq_by_index(smu, SMU_UCLK, (uint16_t)(uclk_count - 1), &uclk_max);
	if (ret)
		return ret;

	/* Force UCLK out of the highest DPM */
	ret = smu_set_hard_freq_range(smu, SMU_UCLK, 0, uclk_min);
	if (ret)
		return ret;

	/* Revert the UCLK Hardmax */
	ret = smu_set_hard_freq_range(smu, SMU_UCLK, 0, uclk_max);
	if (ret)
		return ret;

	/*
	 * In this case, SMU already disabled dummy pstate during enablement
	 * of UCLK DPM, we have to re-enabled it.
	 * */
	return navi10_dummy_pstate_control(smu, true);
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
	.dpm_set_jpeg_enable = navi10_dpm_set_jpeg_enable,
	.get_current_clk_freq_by_table = navi10_get_current_clk_freq_by_table,
	.print_clk_levels = navi10_print_clk_levels,
	.force_clk_levels = navi10_force_clk_levels,
	.populate_umd_state_clk = navi10_populate_umd_state_clk,
	.get_clock_by_type_with_latency = navi10_get_clock_by_type_with_latency,
	.pre_display_config_changed = navi10_pre_display_config_changed,
	.display_config_changed = navi10_display_config_changed,
	.notify_smc_display_config = navi10_notify_smc_display_config,
	.force_dpm_limit_value = navi10_force_dpm_limit_value,
	.unforce_dpm_levels = navi10_unforce_dpm_levels,
	.is_dpm_running = navi10_is_dpm_running,
	.get_fan_speed_percent = navi10_get_fan_speed_percent,
	.get_fan_speed_rpm = navi10_get_fan_speed_rpm,
	.get_power_profile_mode = navi10_get_power_profile_mode,
	.set_power_profile_mode = navi10_set_power_profile_mode,
	.get_profiling_clk_mask = navi10_get_profiling_clk_mask,
	.set_watermarks_table = navi10_set_watermarks_table,
	.read_sensor = navi10_read_sensor,
	.get_uclk_dpm_states = navi10_get_uclk_dpm_states,
	.set_performance_level = navi10_set_performance_level,
	.get_thermal_temperature_range = navi10_get_thermal_temperature_range,
	.display_disable_memory_clock_switch = navi10_display_disable_memory_clock_switch,
	.get_power_limit = navi10_get_power_limit,
	.update_pcie_parameters = navi10_update_pcie_parameters,
	.init_microcode = smu_v11_0_init_microcode,
	.load_microcode = smu_v11_0_load_microcode,
	.init_smc_tables = smu_v11_0_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.check_fw_status = smu_v11_0_check_fw_status,
	.setup_pptable = smu_v11_0_setup_pptable,
	.get_vbios_bootup_values = smu_v11_0_get_vbios_bootup_values,
	.get_clk_info_from_vbios = smu_v11_0_get_clk_info_from_vbios,
	.check_pptable = smu_v11_0_check_pptable,
	.parse_pptable = smu_v11_0_parse_pptable,
	.populate_smc_tables = smu_v11_0_populate_smc_pptable,
	.check_fw_version = smu_v11_0_check_fw_version,
	.write_pptable = smu_v11_0_write_pptable,
	.set_min_dcef_deep_sleep = smu_v11_0_set_min_dcef_deep_sleep,
	.set_driver_table_location = smu_v11_0_set_driver_table_location,
	.set_tool_table_location = smu_v11_0_set_tool_table_location,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.system_features_control = smu_v11_0_system_features_control,
	.send_smc_msg_with_param = smu_v11_0_send_msg_with_param,
	.read_smc_arg = smu_v11_0_read_arg,
	.init_display_count = smu_v11_0_init_display_count,
	.set_allowed_mask = smu_v11_0_set_allowed_mask,
	.get_enabled_mask = smu_v11_0_get_enabled_mask,
	.notify_display_change = smu_v11_0_notify_display_change,
	.set_power_limit = smu_v11_0_set_power_limit,
	.get_current_clk_freq = smu_v11_0_get_current_clk_freq,
	.init_max_sustainable_clocks = smu_v11_0_init_max_sustainable_clocks,
	.start_thermal_control = smu_v11_0_start_thermal_control,
	.stop_thermal_control = smu_v11_0_stop_thermal_control,
	.set_deep_sleep_dcefclk = smu_v11_0_set_deep_sleep_dcefclk,
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
	.baco_is_support= smu_v11_0_baco_is_support,
	.baco_get_state = smu_v11_0_baco_get_state,
	.baco_set_state = smu_v11_0_baco_set_state,
	.baco_enter = smu_v11_0_baco_enter,
	.baco_exit = smu_v11_0_baco_exit,
	.get_dpm_ultimate_freq = smu_v11_0_get_dpm_ultimate_freq,
	.set_soft_freq_limited_range = smu_v11_0_set_soft_freq_limited_range,
	.override_pcie_parameters = smu_v11_0_override_pcie_parameters,
	.set_default_od_settings = navi10_set_default_od_settings,
	.od_edit_dpm_table = navi10_od_edit_dpm_table,
	.get_pptable_power_limit = navi10_get_pptable_power_limit,
	.run_btc = navi10_run_btc,
	.disable_umc_cdr_12gbps_workaround = navi10_disable_umc_cdr_12gbps_workaround,
};

void navi10_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &navi10_ppt_funcs;
}
