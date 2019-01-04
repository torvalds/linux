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
#include "smu11_driver_if.h"
#include "soc15_common.h"
#include "atom.h"
#include "vega20_ppt.h"
#include "vega20_pptable.h"
#include "vega20_ppsmc.h"

#define MSG_MAP(msg, index) \
	[SMU_MSG_##msg] = index

static int vega20_message_map[SMU_MSG_MAX_COUNT] = {
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
	MSG_MAP(RequestI2CBus,			PPSMC_MSG_RequestI2CBus),
	MSG_MAP(ReleaseI2CBus,			PPSMC_MSG_ReleaseI2CBus),
	MSG_MAP(SetFloorSocVoltage,		PPSMC_MSG_SetFloorSocVoltage),
	MSG_MAP(SoftReset,			PPSMC_MSG_SoftReset),
	MSG_MAP(StartBacoMonitor,		PPSMC_MSG_StartBacoMonitor),
	MSG_MAP(CancelBacoMonitor,		PPSMC_MSG_CancelBacoMonitor),
	MSG_MAP(EnterBaco,			PPSMC_MSG_EnterBaco),
	MSG_MAP(SetSoftMinByFreq,		PPSMC_MSG_SetSoftMinByFreq),
	MSG_MAP(SetSoftMaxByFreq,		PPSMC_MSG_SetSoftMaxByFreq),
	MSG_MAP(SetHardMinByFreq,		PPSMC_MSG_SetHardMinByFreq),
	MSG_MAP(SetHardMaxByFreq,		PPSMC_MSG_SetHardMaxByFreq),
	MSG_MAP(GetMinDpmFreq,			PPSMC_MSG_GetMinDpmFreq),
	MSG_MAP(GetMaxDpmFreq,			PPSMC_MSG_GetMaxDpmFreq),
	MSG_MAP(GetDpmFreqByIndex,		PPSMC_MSG_GetDpmFreqByIndex),
	MSG_MAP(GetDpmClockFreq,		PPSMC_MSG_GetDpmClockFreq),
	MSG_MAP(GetSsVoltageByDpm,		PPSMC_MSG_GetSsVoltageByDpm),
	MSG_MAP(SetMemoryChannelConfig,		PPSMC_MSG_SetMemoryChannelConfig),
	MSG_MAP(SetGeminiMode,			PPSMC_MSG_SetGeminiMode),
	MSG_MAP(SetGeminiApertureHigh,		PPSMC_MSG_SetGeminiApertureHigh),
	MSG_MAP(SetGeminiApertureLow,		PPSMC_MSG_SetGeminiApertureLow),
	MSG_MAP(SetMinLinkDpmByIndex,		PPSMC_MSG_SetMinLinkDpmByIndex),
	MSG_MAP(OverridePcieParameters,		PPSMC_MSG_OverridePcieParameters),
	MSG_MAP(OverDriveSetPercentage,		PPSMC_MSG_OverDriveSetPercentage),
	MSG_MAP(SetMinDeepSleepDcefclk,		PPSMC_MSG_SetMinDeepSleepDcefclk),
	MSG_MAP(ReenableAcDcInterrupt,		PPSMC_MSG_ReenableAcDcInterrupt),
	MSG_MAP(NotifyPowerSource,		PPSMC_MSG_NotifyPowerSource),
	MSG_MAP(SetUclkFastSwitch,		PPSMC_MSG_SetUclkFastSwitch),
	MSG_MAP(SetUclkDownHyst,		PPSMC_MSG_SetUclkDownHyst),
	MSG_MAP(GetCurrentRpm,			PPSMC_MSG_GetCurrentRpm),
	MSG_MAP(SetVideoFps,			PPSMC_MSG_SetVideoFps),
	MSG_MAP(SetTjMax,			PPSMC_MSG_SetTjMax),
	MSG_MAP(SetFanTemperatureTarget,	PPSMC_MSG_SetFanTemperatureTarget),
	MSG_MAP(PrepareMp1ForUnload,		PPSMC_MSG_PrepareMp1ForUnload),
	MSG_MAP(DramLogSetDramAddrHigh,		PPSMC_MSG_DramLogSetDramAddrHigh),
	MSG_MAP(DramLogSetDramAddrLow,		PPSMC_MSG_DramLogSetDramAddrLow),
	MSG_MAP(DramLogSetDramSize,		PPSMC_MSG_DramLogSetDramSize),
	MSG_MAP(SetFanMaxRpm,			PPSMC_MSG_SetFanMaxRpm),
	MSG_MAP(SetFanMinPwm,			PPSMC_MSG_SetFanMinPwm),
	MSG_MAP(ConfigureGfxDidt,		PPSMC_MSG_ConfigureGfxDidt),
	MSG_MAP(NumOfDisplays,			PPSMC_MSG_NumOfDisplays),
	MSG_MAP(RemoveMargins,			PPSMC_MSG_RemoveMargins),
	MSG_MAP(ReadSerialNumTop32,		PPSMC_MSG_ReadSerialNumTop32),
	MSG_MAP(ReadSerialNumBottom32,		PPSMC_MSG_ReadSerialNumBottom32),
	MSG_MAP(SetSystemVirtualDramAddrHigh,	PPSMC_MSG_SetSystemVirtualDramAddrHigh),
	MSG_MAP(SetSystemVirtualDramAddrLow,	PPSMC_MSG_SetSystemVirtualDramAddrLow),
	MSG_MAP(WaflTest,			PPSMC_MSG_WaflTest),
	MSG_MAP(SetFclkGfxClkRatio,		PPSMC_MSG_SetFclkGfxClkRatio),
	MSG_MAP(AllowGfxOff,			PPSMC_MSG_AllowGfxOff),
	MSG_MAP(DisallowGfxOff,			PPSMC_MSG_DisallowGfxOff),
	MSG_MAP(GetPptLimit,			PPSMC_MSG_GetPptLimit),
	MSG_MAP(GetDcModeMaxDpmFreq,		PPSMC_MSG_GetDcModeMaxDpmFreq),
	MSG_MAP(GetDebugData,			PPSMC_MSG_GetDebugData),
	MSG_MAP(SetXgmiMode,			PPSMC_MSG_SetXgmiMode),
	MSG_MAP(RunAfllBtc,			PPSMC_MSG_RunAfllBtc),
	MSG_MAP(ExitBaco,			PPSMC_MSG_ExitBaco),
	MSG_MAP(PrepareMp1ForReset,		PPSMC_MSG_PrepareMp1ForReset),
	MSG_MAP(PrepareMp1ForShutdown,		PPSMC_MSG_PrepareMp1ForShutdown),
	MSG_MAP(SetMGpuFanBoostLimitRpm,	PPSMC_MSG_SetMGpuFanBoostLimitRpm),
	MSG_MAP(GetAVFSVoltageByDpm,		PPSMC_MSG_GetAVFSVoltageByDpm),
};

static int vega20_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	if (index > SMU_MSG_MAX_COUNT || index > PPSMC_Message_Count)
		return -EINVAL;
	return vega20_message_map[index];

}

static int vega20_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	smu_dpm->dpm_context = kzalloc(sizeof(struct vega20_dpm_table),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;

	smu_dpm->dpm_context_size = sizeof(struct vega20_dpm_table);

	return 0;
}

static int vega20_store_powerplay_table(struct smu_context *smu)
{
	ATOM_Vega20_POWERPLAYTABLE *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;

	if (!table_context->power_play_table)
		return -EINVAL;

	powerplay_table = table_context->power_play_table;

	memcpy(table_context->driver_pptable, &powerplay_table->smcPPTable,
	       sizeof(PPTable_t));

	table_context->software_shutdown_temp = powerplay_table->usSoftwareShutdownTemp;

	return 0;
}

static int vega20_append_powerplay_table(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	struct atom_smc_dpm_info_v4_4 *smc_dpm_table;
	int index, i, ret;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					   smc_dpm_info);

	ret = smu_get_atom_data_table(smu, index, NULL, NULL, NULL,
				      (uint8_t **)&smc_dpm_table);
	if (ret)
		return ret;

	smc_pptable->MaxVoltageStepGfx = smc_dpm_table->maxvoltagestepgfx;
	smc_pptable->MaxVoltageStepSoc = smc_dpm_table->maxvoltagestepsoc;

	smc_pptable->VddGfxVrMapping = smc_dpm_table->vddgfxvrmapping;
	smc_pptable->VddSocVrMapping = smc_dpm_table->vddsocvrmapping;
	smc_pptable->VddMem0VrMapping = smc_dpm_table->vddmem0vrmapping;
	smc_pptable->VddMem1VrMapping = smc_dpm_table->vddmem1vrmapping;

	smc_pptable->GfxUlvPhaseSheddingMask = smc_dpm_table->gfxulvphasesheddingmask;
	smc_pptable->SocUlvPhaseSheddingMask = smc_dpm_table->soculvphasesheddingmask;
	smc_pptable->ExternalSensorPresent = smc_dpm_table->externalsensorpresent;

	smc_pptable->GfxMaxCurrent = smc_dpm_table->gfxmaxcurrent;
	smc_pptable->GfxOffset = smc_dpm_table->gfxoffset;
	smc_pptable->Padding_TelemetryGfx = smc_dpm_table->padding_telemetrygfx;

	smc_pptable->SocMaxCurrent = smc_dpm_table->socmaxcurrent;
	smc_pptable->SocOffset = smc_dpm_table->socoffset;
	smc_pptable->Padding_TelemetrySoc = smc_dpm_table->padding_telemetrysoc;

	smc_pptable->Mem0MaxCurrent = smc_dpm_table->mem0maxcurrent;
	smc_pptable->Mem0Offset = smc_dpm_table->mem0offset;
	smc_pptable->Padding_TelemetryMem0 = smc_dpm_table->padding_telemetrymem0;

	smc_pptable->Mem1MaxCurrent = smc_dpm_table->mem1maxcurrent;
	smc_pptable->Mem1Offset = smc_dpm_table->mem1offset;
	smc_pptable->Padding_TelemetryMem1 = smc_dpm_table->padding_telemetrymem1;

	smc_pptable->AcDcGpio = smc_dpm_table->acdcgpio;
	smc_pptable->AcDcPolarity = smc_dpm_table->acdcpolarity;
	smc_pptable->VR0HotGpio = smc_dpm_table->vr0hotgpio;
	smc_pptable->VR0HotPolarity = smc_dpm_table->vr0hotpolarity;

	smc_pptable->VR1HotGpio = smc_dpm_table->vr1hotgpio;
	smc_pptable->VR1HotPolarity = smc_dpm_table->vr1hotpolarity;
	smc_pptable->Padding1 = smc_dpm_table->padding1;
	smc_pptable->Padding2 = smc_dpm_table->padding2;

	smc_pptable->LedPin0 = smc_dpm_table->ledpin0;
	smc_pptable->LedPin1 = smc_dpm_table->ledpin1;
	smc_pptable->LedPin2 = smc_dpm_table->ledpin2;

	smc_pptable->PllGfxclkSpreadEnabled = smc_dpm_table->pllgfxclkspreadenabled;
	smc_pptable->PllGfxclkSpreadPercent = smc_dpm_table->pllgfxclkspreadpercent;
	smc_pptable->PllGfxclkSpreadFreq = smc_dpm_table->pllgfxclkspreadfreq;

	smc_pptable->UclkSpreadEnabled = 0;
	smc_pptable->UclkSpreadPercent = smc_dpm_table->uclkspreadpercent;
	smc_pptable->UclkSpreadFreq = smc_dpm_table->uclkspreadfreq;

	smc_pptable->FclkSpreadEnabled = smc_dpm_table->fclkspreadenabled;
	smc_pptable->FclkSpreadPercent = smc_dpm_table->fclkspreadpercent;
	smc_pptable->FclkSpreadFreq = smc_dpm_table->fclkspreadfreq;

	smc_pptable->FllGfxclkSpreadEnabled = smc_dpm_table->fllgfxclkspreadenabled;
	smc_pptable->FllGfxclkSpreadPercent = smc_dpm_table->fllgfxclkspreadpercent;
	smc_pptable->FllGfxclkSpreadFreq = smc_dpm_table->fllgfxclkspreadfreq;

	for (i = 0; i < I2C_CONTROLLER_NAME_COUNT; i++) {
		smc_pptable->I2cControllers[i].Enabled =
			smc_dpm_table->i2ccontrollers[i].enabled;
		smc_pptable->I2cControllers[i].SlaveAddress =
			smc_dpm_table->i2ccontrollers[i].slaveaddress;
		smc_pptable->I2cControllers[i].ControllerPort =
			smc_dpm_table->i2ccontrollers[i].controllerport;
		smc_pptable->I2cControllers[i].ThermalThrottler =
			smc_dpm_table->i2ccontrollers[i].thermalthrottler;
		smc_pptable->I2cControllers[i].I2cProtocol =
			smc_dpm_table->i2ccontrollers[i].i2cprotocol;
		smc_pptable->I2cControllers[i].I2cSpeed =
			smc_dpm_table->i2ccontrollers[i].i2cspeed;
	}

	return 0;
}

static int vega20_check_powerplay_table(struct smu_context *smu)
{
	ATOM_Vega20_POWERPLAYTABLE *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;

	powerplay_table = table_context->power_play_table;

	if (powerplay_table->sHeader.format_revision < ATOM_VEGA20_TABLE_REVISION_VEGA20) {
		pr_err("Unsupported PPTable format!");
		return -EINVAL;
	}

	if (!powerplay_table->sHeader.structuresize) {
		pr_err("Invalid PowerPlay Table!");
		return -EINVAL;
	}

	return 0;
}

static int vega20_run_btc_afll(struct smu_context *smu)
{
	return smu_send_smc_msg(smu, SMU_MSG_RunAfllBtc);
}

static int
vega20_get_unallowed_feature_mask(struct smu_context *smu,
				  uint32_t *feature_mask, uint32_t num)
{
	if (num > 2)
		return -EINVAL;

	feature_mask[0] = 0xE0041C00;
	feature_mask[1] = 0xFFFFFFFE; /* bit32~bit63 is Unsupported */

	return 0;
}

static int
vega20_set_single_dpm_table(struct smu_context *smu,
			    struct vega20_single_dpm_table *single_dpm_table,
			    PPCLK_e clk_id)
{
	int ret = 0;
	uint32_t i, num_of_levels, clk;

	ret = smu_send_smc_msg_with_param(smu,
			SMU_MSG_GetDpmFreqByIndex,
			(clk_id << 16 | 0xFF));
	if (ret) {
		pr_err("[GetNumOfDpmLevel] failed to get dpm levels!");
		return ret;
	}

	smu_read_smc_arg(smu, &num_of_levels);
	if (!num_of_levels) {
		pr_err("[GetNumOfDpmLevel] number of clk levels is invalid!");
		return -EINVAL;
	}

	single_dpm_table->count = num_of_levels;

	for (i = 0; i < num_of_levels; i++) {
		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_GetDpmFreqByIndex,
				(clk_id << 16 | i));
		if (ret) {
			pr_err("[GetDpmFreqByIndex] failed to get dpm freq by index!");
			return ret;
		}
		smu_read_smc_arg(smu, &clk);
		if (!clk) {
			pr_err("[GetDpmFreqByIndex] clk value is invalid!");
			return -EINVAL;
		}
		single_dpm_table->dpm_levels[i].value = clk;
		single_dpm_table->dpm_levels[i].enabled = true;
	}
	return 0;
}

static void vega20_init_single_dpm_state(struct vega20_dpm_state *dpm_state)
{
	dpm_state->soft_min_level = 0x0;
	dpm_state->soft_max_level = 0xffff;
        dpm_state->hard_min_level = 0x0;
        dpm_state->hard_max_level = 0xffff;
}

static int vega20_set_default_dpm_table(struct smu_context *smu)
{
	int ret;

	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;
	struct vega20_single_dpm_table *single_dpm_table;

	dpm_table = smu_dpm->dpm_context;

	/* socclk */
	single_dpm_table = &(dpm_table->soc_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_SOCCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_SOCCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get socclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.socclk / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* gfxclk */
	single_dpm_table = &(dpm_table->gfx_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_GFXCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_GFXCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get gfxclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.gfxclk / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* memclk */
	single_dpm_table = &(dpm_table->mem_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_UCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get memclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.uclk / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

#if 0
	/* eclk */
	single_dpm_table = &(dpm_table->eclk_table);

	if (feature->fea_enabled[FEATURE_DPM_VCE_BIT]) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table, PPCLK_ECLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get eclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.eclock / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* vclk */
	single_dpm_table = &(dpm_table->vclk_table);

	if (feature->fea_enabled[FEATURE_DPM_UVD_BIT]) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table, PPCLK_VCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get vclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.vclock / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* dclk */
	single_dpm_table = &(dpm_table->dclk_table);

	if (feature->fea_enabled[FEATURE_DPM_UVD_BIT]) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table, PPCLK_DCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get dclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dclock / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));
#endif

	/* dcefclk */
	single_dpm_table = &(dpm_table->dcef_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_DCEFCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get dcefclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dcefclk / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* pixclk */
	single_dpm_table = &(dpm_table->pixel_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_PIXCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get pixclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 0;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* dispclk */
	single_dpm_table = &(dpm_table->display_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_DISPCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get dispclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 0;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* phyclk */
	single_dpm_table = &(dpm_table->phy_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_PHYCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get phyclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 0;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* fclk */
	single_dpm_table = &(dpm_table->fclk_table);

	if (smu_feature_is_enabled(smu,FEATURE_DPM_FCLK_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  PPCLK_FCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get fclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 0;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	return 0;
}

static int vega20_populate_umd_state_clk(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;
	struct vega20_single_dpm_table *gfx_table = NULL;
	struct vega20_single_dpm_table *mem_table = NULL;

	dpm_table = smu_dpm->dpm_context;
	gfx_table = &(dpm_table->gfx_table);
	mem_table = &(dpm_table->mem_table);

	smu->pstate_sclk = gfx_table->dpm_levels[0].value;
	smu->pstate_mclk = mem_table->dpm_levels[0].value;

	if (gfx_table->count > VEGA20_UMD_PSTATE_GFXCLK_LEVEL &&
	    mem_table->count > VEGA20_UMD_PSTATE_MCLK_LEVEL) {
		smu->pstate_sclk = gfx_table->dpm_levels[VEGA20_UMD_PSTATE_GFXCLK_LEVEL].value;
		smu->pstate_mclk = mem_table->dpm_levels[VEGA20_UMD_PSTATE_MCLK_LEVEL].value;
	}

	smu->pstate_sclk = smu->pstate_sclk * 100;
	smu->pstate_mclk = smu->pstate_mclk * 100;

	return 0;
}

static int vega20_get_clk_table(struct smu_context *smu,
			struct pp_clock_levels_with_latency *clocks,
			struct vega20_single_dpm_table *dpm_table)
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

static int vega20_print_clk_levels(struct smu_context *smu,
			enum pp_clock_type type, char *buf)
{
	int i, now, size = 0;
	int ret = 0;
	struct pp_clock_levels_with_latency clocks;
	struct vega20_single_dpm_table *single_dpm_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;

	dpm_table = smu_dpm->dpm_context;

	switch (type) {
	case PP_SCLK:
		ret = smu_get_current_clk_freq(smu, PPCLK_GFXCLK, &now);
		if (ret) {
			pr_err("Attempt to get current gfx clk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_table->gfx_table);
		ret = vega20_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			pr_err("Attempt to get gfx clk levels Failed!");
			return ret;
		}

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n", i,
					clocks.data[i].clocks_in_khz / 1000,
					(clocks.data[i].clocks_in_khz == now * 10)
					? "*" : "");
		break;

	case PP_MCLK:
		ret = smu_get_current_clk_freq(smu, PPCLK_UCLK, &now);
		if (ret) {
			pr_err("Attempt to get current mclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_table->mem_table);
		ret = vega20_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			pr_err("Attempt to get memory clk levels Failed!");
			return ret;
		}

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz == now * 10)
				? "*" : "");
		break;
	default:
		break;
	}
	return size;
}

static const struct pptable_funcs vega20_ppt_funcs = {
	.alloc_dpm_context = vega20_allocate_dpm_context,
	.store_powerplay_table = vega20_store_powerplay_table,
	.check_powerplay_table = vega20_check_powerplay_table,
	.append_powerplay_table = vega20_append_powerplay_table,
	.get_smu_msg_index = vega20_get_smu_msg_index,
	.run_afll_btc = vega20_run_btc_afll,
	.get_unallowed_feature_mask = vega20_get_unallowed_feature_mask,
	.set_default_dpm_table = vega20_set_default_dpm_table,
	.populate_umd_state_clk = vega20_populate_umd_state_clk,
	.print_clk_levels = vega20_print_clk_levels,
};

void vega20_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &vega20_ppt_funcs;
}
