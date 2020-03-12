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
#include "smu_internal.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v11_0.h"
#include "smu11_driver_if.h"
#include "soc15_common.h"
#include "atom.h"
#include "power_state.h"
#include "vega20_ppt.h"
#include "vega20_pptable.h"
#include "vega20_ppsmc.h"
#include "nbio/nbio_7_4_sh_mask.h"
#include "asic_reg/thm/thm_11_0_2_offset.h"
#include "asic_reg/thm/thm_11_0_2_sh_mask.h"

#define smnPCIE_LC_SPEED_CNTL			0x11140290
#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288

#define CTF_OFFSET_EDGE			5
#define CTF_OFFSET_HOTSPOT		5
#define CTF_OFFSET_HBM			5

#define MSG_MAP(msg) \
	[SMU_MSG_##msg] = {1, PPSMC_MSG_##msg}

#define SMC_DPM_FEATURE (FEATURE_DPM_PREFETCHER_MASK | \
			 FEATURE_DPM_GFXCLK_MASK | \
			 FEATURE_DPM_UCLK_MASK | \
			 FEATURE_DPM_SOCCLK_MASK | \
			 FEATURE_DPM_UVD_MASK | \
			 FEATURE_DPM_VCE_MASK | \
			 FEATURE_DPM_MP0CLK_MASK | \
			 FEATURE_DPM_LINK_MASK | \
			 FEATURE_DPM_DCEFCLK_MASK)

static struct smu_11_0_cmn2aisc_mapping vega20_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage),
	MSG_MAP(GetSmuVersion),
	MSG_MAP(GetDriverIfVersion),
	MSG_MAP(SetAllowedFeaturesMaskLow),
	MSG_MAP(SetAllowedFeaturesMaskHigh),
	MSG_MAP(EnableAllSmuFeatures),
	MSG_MAP(DisableAllSmuFeatures),
	MSG_MAP(EnableSmuFeaturesLow),
	MSG_MAP(EnableSmuFeaturesHigh),
	MSG_MAP(DisableSmuFeaturesLow),
	MSG_MAP(DisableSmuFeaturesHigh),
	MSG_MAP(GetEnabledSmuFeaturesLow),
	MSG_MAP(GetEnabledSmuFeaturesHigh),
	MSG_MAP(SetWorkloadMask),
	MSG_MAP(SetPptLimit),
	MSG_MAP(SetDriverDramAddrHigh),
	MSG_MAP(SetDriverDramAddrLow),
	MSG_MAP(SetToolsDramAddrHigh),
	MSG_MAP(SetToolsDramAddrLow),
	MSG_MAP(TransferTableSmu2Dram),
	MSG_MAP(TransferTableDram2Smu),
	MSG_MAP(UseDefaultPPTable),
	MSG_MAP(UseBackupPPTable),
	MSG_MAP(RunBtc),
	MSG_MAP(RequestI2CBus),
	MSG_MAP(ReleaseI2CBus),
	MSG_MAP(SetFloorSocVoltage),
	MSG_MAP(SoftReset),
	MSG_MAP(StartBacoMonitor),
	MSG_MAP(CancelBacoMonitor),
	MSG_MAP(EnterBaco),
	MSG_MAP(SetSoftMinByFreq),
	MSG_MAP(SetSoftMaxByFreq),
	MSG_MAP(SetHardMinByFreq),
	MSG_MAP(SetHardMaxByFreq),
	MSG_MAP(GetMinDpmFreq),
	MSG_MAP(GetMaxDpmFreq),
	MSG_MAP(GetDpmFreqByIndex),
	MSG_MAP(GetDpmClockFreq),
	MSG_MAP(GetSsVoltageByDpm),
	MSG_MAP(SetMemoryChannelConfig),
	MSG_MAP(SetGeminiMode),
	MSG_MAP(SetGeminiApertureHigh),
	MSG_MAP(SetGeminiApertureLow),
	MSG_MAP(SetMinLinkDpmByIndex),
	MSG_MAP(OverridePcieParameters),
	MSG_MAP(OverDriveSetPercentage),
	MSG_MAP(SetMinDeepSleepDcefclk),
	MSG_MAP(ReenableAcDcInterrupt),
	MSG_MAP(NotifyPowerSource),
	MSG_MAP(SetUclkFastSwitch),
	MSG_MAP(SetUclkDownHyst),
	MSG_MAP(GetCurrentRpm),
	MSG_MAP(SetVideoFps),
	MSG_MAP(SetTjMax),
	MSG_MAP(SetFanTemperatureTarget),
	MSG_MAP(PrepareMp1ForUnload),
	MSG_MAP(DramLogSetDramAddrHigh),
	MSG_MAP(DramLogSetDramAddrLow),
	MSG_MAP(DramLogSetDramSize),
	MSG_MAP(SetFanMaxRpm),
	MSG_MAP(SetFanMinPwm),
	MSG_MAP(ConfigureGfxDidt),
	MSG_MAP(NumOfDisplays),
	MSG_MAP(RemoveMargins),
	MSG_MAP(ReadSerialNumTop32),
	MSG_MAP(ReadSerialNumBottom32),
	MSG_MAP(SetSystemVirtualDramAddrHigh),
	MSG_MAP(SetSystemVirtualDramAddrLow),
	MSG_MAP(WaflTest),
	MSG_MAP(SetFclkGfxClkRatio),
	MSG_MAP(AllowGfxOff),
	MSG_MAP(DisallowGfxOff),
	MSG_MAP(GetPptLimit),
	MSG_MAP(GetDcModeMaxDpmFreq),
	MSG_MAP(GetDebugData),
	MSG_MAP(SetXgmiMode),
	MSG_MAP(RunAfllBtc),
	MSG_MAP(ExitBaco),
	MSG_MAP(PrepareMp1ForReset),
	MSG_MAP(PrepareMp1ForShutdown),
	MSG_MAP(SetMGpuFanBoostLimitRpm),
	MSG_MAP(GetAVFSVoltageByDpm),
	MSG_MAP(DFCstateControl),
};

static struct smu_11_0_cmn2aisc_mapping vega20_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(GFXCLK, PPCLK_GFXCLK),
	CLK_MAP(VCLK, PPCLK_VCLK),
	CLK_MAP(DCLK, PPCLK_DCLK),
	CLK_MAP(ECLK, PPCLK_ECLK),
	CLK_MAP(SOCCLK, PPCLK_SOCCLK),
	CLK_MAP(UCLK, PPCLK_UCLK),
	CLK_MAP(DCEFCLK, PPCLK_DCEFCLK),
	CLK_MAP(DISPCLK, PPCLK_DISPCLK),
	CLK_MAP(PIXCLK, PPCLK_PIXCLK),
	CLK_MAP(PHYCLK, PPCLK_PHYCLK),
	CLK_MAP(FCLK, PPCLK_FCLK),
};

static struct smu_11_0_cmn2aisc_mapping vega20_feature_mask_map[SMU_FEATURE_COUNT] = {
	FEA_MAP(DPM_PREFETCHER),
	FEA_MAP(DPM_GFXCLK),
	FEA_MAP(DPM_UCLK),
	FEA_MAP(DPM_SOCCLK),
	FEA_MAP(DPM_UVD),
	FEA_MAP(DPM_VCE),
	FEA_MAP(ULV),
	FEA_MAP(DPM_MP0CLK),
	FEA_MAP(DPM_LINK),
	FEA_MAP(DPM_DCEFCLK),
	FEA_MAP(DS_GFXCLK),
	FEA_MAP(DS_SOCCLK),
	FEA_MAP(DS_LCLK),
	FEA_MAP(PPT),
	FEA_MAP(TDC),
	FEA_MAP(THERMAL),
	FEA_MAP(GFX_PER_CU_CG),
	FEA_MAP(RM),
	FEA_MAP(DS_DCEFCLK),
	FEA_MAP(ACDC),
	FEA_MAP(VR0HOT),
	FEA_MAP(VR1HOT),
	FEA_MAP(FW_CTF),
	FEA_MAP(LED_DISPLAY),
	FEA_MAP(FAN_CONTROL),
	FEA_MAP(GFX_EDC),
	FEA_MAP(GFXOFF),
	FEA_MAP(CG),
	FEA_MAP(DPM_FCLK),
	FEA_MAP(DS_FCLK),
	FEA_MAP(DS_MP1CLK),
	FEA_MAP(DS_MP0CLK),
	FEA_MAP(XGMI),
};

static struct smu_11_0_cmn2aisc_mapping vega20_table_map[SMU_TABLE_COUNT] = {
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
};

static struct smu_11_0_cmn2aisc_mapping vega20_pwr_src_map[SMU_POWER_SOURCE_COUNT] = {
	PWR_MAP(AC),
	PWR_MAP(DC),
};

static struct smu_11_0_cmn2aisc_mapping vega20_workload_map[PP_SMC_POWER_PROFILE_COUNT] = {
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT,	WORKLOAD_DEFAULT_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_FULLSCREEN3D,		WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_POWERSAVING,		WORKLOAD_PPLIB_POWER_SAVING_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VIDEO,		WORKLOAD_PPLIB_VIDEO_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_VR,			WORKLOAD_PPLIB_VR_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_COMPUTE,		WORKLOAD_PPLIB_COMPUTE_BIT),
	WORKLOAD_MAP(PP_SMC_POWER_PROFILE_CUSTOM,		WORKLOAD_PPLIB_CUSTOM_BIT),
};

static int vega20_get_smu_table_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_TABLE_COUNT)
		return -EINVAL;

	mapping = vega20_table_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int vega20_get_pwr_src_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_POWER_SOURCE_COUNT)
		return -EINVAL;

	mapping = vega20_pwr_src_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int vega20_get_smu_feature_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_FEATURE_COUNT)
		return -EINVAL;

	mapping = vega20_feature_mask_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int vega20_get_smu_clk_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_CLK_COUNT)
		return -EINVAL;

	mapping = vega20_clk_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int vega20_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (index >= SMU_MSG_MAX_COUNT)
		return -EINVAL;

	mapping = vega20_message_map[index];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int vega20_get_workload_type(struct smu_context *smu, enum PP_SMC_POWER_PROFILE profile)
{
	struct smu_11_0_cmn2aisc_mapping mapping;

	if (profile > PP_SMC_POWER_PROFILE_CUSTOM)
		return -EINVAL;

	mapping = vega20_workload_map[profile];
	if (!(mapping.valid_mapping)) {
		return -EINVAL;
	}

	return mapping.map_to;
}

static int vega20_tables_init(struct smu_context *smu, struct smu_table *tables)
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

static int vega20_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	if (smu_dpm->dpm_context)
		return -EINVAL;

	smu_dpm->dpm_context = kzalloc(sizeof(struct vega20_dpm_table),
				       GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;

	if (smu_dpm->golden_dpm_context)
		return -EINVAL;

	smu_dpm->golden_dpm_context = kzalloc(sizeof(struct vega20_dpm_table),
					      GFP_KERNEL);
	if (!smu_dpm->golden_dpm_context)
		return -ENOMEM;

	smu_dpm->dpm_context_size = sizeof(struct vega20_dpm_table);

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

static int vega20_setup_od8_information(struct smu_context *smu)
{
	ATOM_Vega20_POWERPLAYTABLE *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;
	struct vega20_od8_settings *od8_settings = (struct vega20_od8_settings *)smu->od_settings;

	uint32_t od_feature_count, od_feature_array_size,
		 od_setting_count, od_setting_array_size;

	if (!table_context->power_play_table)
		return -EINVAL;

	powerplay_table = table_context->power_play_table;

	if (powerplay_table->OverDrive8Table.ucODTableRevision == 1) {
		/* Setup correct ODFeatureCount, and store ODFeatureArray from
		 * powerplay table to od_feature_capabilities */
		od_feature_count =
			(le32_to_cpu(powerplay_table->OverDrive8Table.ODFeatureCount) >
			 ATOM_VEGA20_ODFEATURE_COUNT) ?
			ATOM_VEGA20_ODFEATURE_COUNT :
			le32_to_cpu(powerplay_table->OverDrive8Table.ODFeatureCount);

		od_feature_array_size = sizeof(uint8_t) * od_feature_count;

		if (od8_settings->od_feature_capabilities)
			return -EINVAL;

		od8_settings->od_feature_capabilities = kmemdup(&powerplay_table->OverDrive8Table.ODFeatureCapabilities,
								 od_feature_array_size,
								 GFP_KERNEL);
		if (!od8_settings->od_feature_capabilities)
			return -ENOMEM;

		/* Setup correct ODSettingCount, and store ODSettingArray from
		 * powerplay table to od_settings_max and od_setting_min */
		od_setting_count =
			(le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount) >
			 ATOM_VEGA20_ODSETTING_COUNT) ?
			ATOM_VEGA20_ODSETTING_COUNT :
			le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount);

		od_setting_array_size = sizeof(uint32_t) * od_setting_count;

		if (od8_settings->od_settings_max)
			return -EINVAL;

		od8_settings->od_settings_max = kmemdup(&powerplay_table->OverDrive8Table.ODSettingsMax,
							 od_setting_array_size,
							 GFP_KERNEL);

		if (!od8_settings->od_settings_max) {
			kfree(od8_settings->od_feature_capabilities);
			od8_settings->od_feature_capabilities = NULL;
			return -ENOMEM;
		}

		if (od8_settings->od_settings_min)
			return -EINVAL;

		od8_settings->od_settings_min = kmemdup(&powerplay_table->OverDrive8Table.ODSettingsMin,
							 od_setting_array_size,
							 GFP_KERNEL);

		if (!od8_settings->od_settings_min) {
			kfree(od8_settings->od_feature_capabilities);
			od8_settings->od_feature_capabilities = NULL;
			kfree(od8_settings->od_settings_max);
			od8_settings->od_settings_max = NULL;
			return -ENOMEM;
		}
	}

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

	table_context->thermal_controller_type = powerplay_table->ucThermalControllerType;

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
	return smu_send_smc_msg(smu, SMU_MSG_RunAfllBtc, NULL);
}

#define FEATURE_MASK(feature) (1ULL << feature)
static int
vega20_get_allowed_feature_mask(struct smu_context *smu,
				  uint32_t *feature_mask, uint32_t num)
{
	if (num > 2)
		return -EINVAL;

	memset(feature_mask, 0, sizeof(uint32_t) * num);

	*(uint64_t *)feature_mask |= FEATURE_MASK(FEATURE_DPM_PREFETCHER_BIT)
				| FEATURE_MASK(FEATURE_DPM_GFXCLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_UCLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_SOCCLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_UVD_BIT)
				| FEATURE_MASK(FEATURE_DPM_VCE_BIT)
				| FEATURE_MASK(FEATURE_ULV_BIT)
				| FEATURE_MASK(FEATURE_DPM_MP0CLK_BIT)
				| FEATURE_MASK(FEATURE_DPM_LINK_BIT)
				| FEATURE_MASK(FEATURE_DPM_DCEFCLK_BIT)
				| FEATURE_MASK(FEATURE_PPT_BIT)
				| FEATURE_MASK(FEATURE_TDC_BIT)
				| FEATURE_MASK(FEATURE_THERMAL_BIT)
				| FEATURE_MASK(FEATURE_GFX_PER_CU_CG_BIT)
				| FEATURE_MASK(FEATURE_RM_BIT)
				| FEATURE_MASK(FEATURE_ACDC_BIT)
				| FEATURE_MASK(FEATURE_VR0HOT_BIT)
				| FEATURE_MASK(FEATURE_VR1HOT_BIT)
				| FEATURE_MASK(FEATURE_FW_CTF_BIT)
				| FEATURE_MASK(FEATURE_LED_DISPLAY_BIT)
				| FEATURE_MASK(FEATURE_FAN_CONTROL_BIT)
				| FEATURE_MASK(FEATURE_GFX_EDC_BIT)
				| FEATURE_MASK(FEATURE_GFXOFF_BIT)
				| FEATURE_MASK(FEATURE_CG_BIT)
				| FEATURE_MASK(FEATURE_DPM_FCLK_BIT)
				| FEATURE_MASK(FEATURE_XGMI_BIT);
	return 0;
}

static enum
amd_pm_state_type vega20_get_current_power_state(struct smu_context *smu)
{
	enum amd_pm_state_type pm_type;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!smu_dpm_ctx->dpm_context ||
	    !smu_dpm_ctx->dpm_current_power_state)
		return -EINVAL;

	switch (smu_dpm_ctx->dpm_current_power_state->classification.ui_label) {
	case SMU_STATE_UI_LABEL_BATTERY:
		pm_type = POWER_STATE_TYPE_BATTERY;
		break;
	case SMU_STATE_UI_LABEL_BALLANCED:
		pm_type = POWER_STATE_TYPE_BALANCED;
		break;
	case SMU_STATE_UI_LABEL_PERFORMANCE:
		pm_type = POWER_STATE_TYPE_PERFORMANCE;
		break;
	default:
		if (smu_dpm_ctx->dpm_current_power_state->classification.flags & SMU_STATE_CLASSIFICATION_FLAG_BOOT)
			pm_type = POWER_STATE_TYPE_INTERNAL_BOOT;
		else
			pm_type = POWER_STATE_TYPE_DEFAULT;
		break;
	}

	return pm_type;
}

static int
vega20_set_single_dpm_table(struct smu_context *smu,
			    struct vega20_single_dpm_table *single_dpm_table,
			    PPCLK_e clk_id)
{
	int ret = 0;
	uint32_t i, num_of_levels = 0, clk;

	ret = smu_send_smc_msg_with_param(smu,
			SMU_MSG_GetDpmFreqByIndex,
			(clk_id << 16 | 0xFF),
			&num_of_levels);
	if (ret) {
		pr_err("[GetNumOfDpmLevel] failed to get dpm levels!");
		return ret;
	}

	if (!num_of_levels) {
		pr_err("[GetNumOfDpmLevel] number of clk levels is invalid!");
		return -EINVAL;
	}

	single_dpm_table->count = num_of_levels;

	for (i = 0; i < num_of_levels; i++) {
		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_GetDpmFreqByIndex,
				(clk_id << 16 | i),
				&clk);
		if (ret) {
			pr_err("[GetDpmFreqByIndex] failed to get dpm freq by index!");
			return ret;
		}
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

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
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

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT)) {
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

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
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

	/* eclk */
	single_dpm_table = &(dpm_table->eclk_table);

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_VCE_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table, PPCLK_ECLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get eclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.eclk / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* vclk */
	single_dpm_table = &(dpm_table->vclk_table);

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UVD_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table, PPCLK_VCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get vclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.vclk / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* dclk */
	single_dpm_table = &(dpm_table->dclk_table);

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UVD_BIT)) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table, PPCLK_DCLK);
		if (ret) {
			pr_err("[SetupDefaultDpmTable] failed to get dclk dpm levels!");
			return ret;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.dclk / 100;
	}
	vega20_init_single_dpm_state(&(single_dpm_table->dpm_state));

	/* dcefclk */
	single_dpm_table = &(dpm_table->dcef_table);

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
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

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
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

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
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

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
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

	memcpy(smu_dpm->golden_dpm_context, dpm_table,
	       sizeof(struct vega20_dpm_table));

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
			enum smu_clk_type type, char *buf)
{
	int i, now, size = 0;
	int ret = 0;
	uint32_t gen_speed, lane_width;
	struct amdgpu_device *adev = smu->adev;
	struct pp_clock_levels_with_latency clocks;
	struct vega20_single_dpm_table *single_dpm_table;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;
	struct vega20_od8_settings *od8_settings =
		(struct vega20_od8_settings *)smu->od_settings;
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)(table_context->overdrive_table);
	PPTable_t *pptable = (PPTable_t *)table_context->driver_pptable;

	dpm_table = smu_dpm->dpm_context;

	switch (type) {
	case SMU_SCLK:
		ret = smu_get_current_clk_freq(smu, SMU_GFXCLK, &now);
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

	case SMU_MCLK:
		ret = smu_get_current_clk_freq(smu, SMU_UCLK, &now);
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

	case SMU_SOCCLK:
		ret = smu_get_current_clk_freq(smu, SMU_SOCCLK, &now);
		if (ret) {
			pr_err("Attempt to get current socclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_table->soc_table);
		ret = vega20_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			pr_err("Attempt to get socclk levels Failed!");
			return ret;
		}

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz == now * 10)
				? "*" : "");
		break;

	case SMU_FCLK:
		ret = smu_get_current_clk_freq(smu, SMU_FCLK, &now);
		if (ret) {
			pr_err("Attempt to get current fclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_table->fclk_table);
		for (i = 0; i < single_dpm_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, single_dpm_table->dpm_levels[i].value,
				(single_dpm_table->dpm_levels[i].value == now / 100)
				? "*" : "");
		break;

	case SMU_DCEFCLK:
		ret = smu_get_current_clk_freq(smu, SMU_DCEFCLK, &now);
		if (ret) {
			pr_err("Attempt to get current dcefclk Failed!");
			return ret;
		}

		single_dpm_table = &(dpm_table->dcef_table);
		ret = vega20_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			pr_err("Attempt to get dcefclk levels Failed!");
			return ret;
		}

		for (i = 0; i < clocks.num_levels; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
				i, clocks.data[i].clocks_in_khz / 1000,
				(clocks.data[i].clocks_in_khz == now * 10) ? "*" : "");
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
					(pptable->PcieGenSpeed[i] == 0) ? "2.5GT/s," :
					(pptable->PcieGenSpeed[i] == 1) ? "5.0GT/s," :
					(pptable->PcieGenSpeed[i] == 2) ? "8.0GT/s," :
					(pptable->PcieGenSpeed[i] == 3) ? "16.0GT/s," : "",
					(pptable->PcieLaneCount[i] == 1) ? "x1" :
					(pptable->PcieLaneCount[i] == 2) ? "x2" :
					(pptable->PcieLaneCount[i] == 3) ? "x4" :
					(pptable->PcieLaneCount[i] == 4) ? "x8" :
					(pptable->PcieLaneCount[i] == 5) ? "x12" :
					(pptable->PcieLaneCount[i] == 6) ? "x16" : "",
					pptable->LclkFreq[i],
					(gen_speed == pptable->PcieGenSpeed[i]) &&
					(lane_width == pptable->PcieLaneCount[i]) ?
					"*" : "");
		break;

	case SMU_OD_SCLK:
		if (od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_SCLK");
			size += sprintf(buf + size, "0: %10uMhz\n",
					od_table->GfxclkFmin);
			size += sprintf(buf + size, "1: %10uMhz\n",
					od_table->GfxclkFmax);
		}

		break;

	case SMU_OD_MCLK:
		if (od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_MCLK");
			size += sprintf(buf + size, "1: %10uMhz\n",
					 od_table->UclkFmax);
		}

		break;

	case SMU_OD_VDDC_CURVE:
		if (od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_VDDC_CURVE");
			size += sprintf(buf + size, "0: %10uMhz %10dmV\n",
					od_table->GfxclkFreq1,
					od_table->GfxclkVolt1 / VOLTAGE_SCALE);
			size += sprintf(buf + size, "1: %10uMhz %10dmV\n",
					od_table->GfxclkFreq2,
					od_table->GfxclkVolt2 / VOLTAGE_SCALE);
			size += sprintf(buf + size, "2: %10uMhz %10dmV\n",
					od_table->GfxclkFreq3,
					od_table->GfxclkVolt3 / VOLTAGE_SCALE);
		}

		break;

	case SMU_OD_RANGE:
		size = sprintf(buf, "%s:\n", "OD_RANGE");

		if (od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id) {
			size += sprintf(buf + size, "SCLK: %7uMhz %10uMhz\n",
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].max_value);
		}

		if (od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id) {
			single_dpm_table = &(dpm_table->mem_table);
			ret = vega20_get_clk_table(smu, &clocks, single_dpm_table);
			if (ret) {
				pr_err("Attempt to get memory clk levels Failed!");
				return ret;
			}

			size += sprintf(buf + size, "MCLK: %7uMhz %10uMhz\n",
					clocks.data[0].clocks_in_khz / 1000,
					od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].max_value);
		}

		if (od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id) {
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[0]: %7uMhz %10uMhz\n",
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[0]: %7dmV %11dmV\n",
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[1]: %7uMhz %10uMhz\n",
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[1]: %7dmV %11dmV\n",
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_SCLK[2]: %7uMhz %10uMhz\n",
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].max_value);
			size += sprintf(buf + size, "VDDC_CURVE_VOLT[2]: %7dmV %11dmV\n",
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].max_value);
		}

		break;

	default:
		break;
	}
	return size;
}

static int vega20_upload_dpm_level(struct smu_context *smu, bool max,
				   uint32_t feature_mask)
{
	struct vega20_dpm_table *dpm_table;
	struct vega20_single_dpm_table *single_dpm_table;
	uint32_t freq;
	int ret = 0;

	dpm_table = smu->smu_dpm.dpm_context;

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_GFXCLK_MASK)) {
		single_dpm_table = &(dpm_table->gfx_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_GFXCLK << 16) | (freq & 0xffff),
			NULL);
		if (ret) {
			pr_err("Failed to set soft %s gfxclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_UCLK_MASK)) {
		single_dpm_table = &(dpm_table->mem_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_UCLK << 16) | (freq & 0xffff),
			NULL);
		if (ret) {
			pr_err("Failed to set soft %s memclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_SOCCLK_MASK)) {
		single_dpm_table = &(dpm_table->soc_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_SOCCLK << 16) | (freq & 0xffff),
			NULL);
		if (ret) {
			pr_err("Failed to set soft %s socclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_FCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_FCLK_MASK)) {
		single_dpm_table = &(dpm_table->fclk_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_FCLK << 16) | (freq & 0xffff),
			NULL);
		if (ret) {
			pr_err("Failed to set soft %s fclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_DCEFCLK_MASK)) {
		single_dpm_table = &(dpm_table->dcef_table);
		freq = single_dpm_table->dpm_state.hard_min_level;
		if (!max) {
			ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetHardMinByFreq,
				(PPCLK_DCEFCLK << 16) | (freq & 0xffff),
				NULL);
			if (ret) {
				pr_err("Failed to set hard min dcefclk !\n");
				return ret;
			}
		}
	}

	return ret;
}

static int vega20_force_clk_levels(struct smu_context *smu,
			enum  smu_clk_type clk_type, uint32_t mask)
{
	struct vega20_dpm_table *dpm_table;
	struct vega20_single_dpm_table *single_dpm_table;
	uint32_t soft_min_level, soft_max_level, hard_min_level;
	int ret = 0;

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	dpm_table = smu->smu_dpm.dpm_context;

	switch (clk_type) {
	case SMU_SCLK:
		single_dpm_table = &(dpm_table->gfx_table);

		if (soft_max_level >= single_dpm_table->count) {
			pr_err("Clock level specified %d is over max allowed %d\n",
					soft_max_level, single_dpm_table->count - 1);
			ret = -EINVAL;
			break;
		}

		single_dpm_table->dpm_state.soft_min_level =
			single_dpm_table->dpm_levels[soft_min_level].value;
		single_dpm_table->dpm_state.soft_max_level =
			single_dpm_table->dpm_levels[soft_max_level].value;

		ret = vega20_upload_dpm_level(smu, false, FEATURE_DPM_GFXCLK_MASK);
		if (ret) {
			pr_err("Failed to upload boot level to lowest!\n");
			break;
		}

		ret = vega20_upload_dpm_level(smu, true, FEATURE_DPM_GFXCLK_MASK);
		if (ret)
			pr_err("Failed to upload dpm max level to highest!\n");

		break;

	case SMU_MCLK:
		single_dpm_table = &(dpm_table->mem_table);

		if (soft_max_level >= single_dpm_table->count) {
			pr_err("Clock level specified %d is over max allowed %d\n",
					soft_max_level, single_dpm_table->count - 1);
			ret = -EINVAL;
			break;
		}

		single_dpm_table->dpm_state.soft_min_level =
			single_dpm_table->dpm_levels[soft_min_level].value;
		single_dpm_table->dpm_state.soft_max_level =
			single_dpm_table->dpm_levels[soft_max_level].value;

		ret = vega20_upload_dpm_level(smu, false, FEATURE_DPM_UCLK_MASK);
		if (ret) {
			pr_err("Failed to upload boot level to lowest!\n");
			break;
		}

		ret = vega20_upload_dpm_level(smu, true, FEATURE_DPM_UCLK_MASK);
		if (ret)
			pr_err("Failed to upload dpm max level to highest!\n");

		break;

	case SMU_SOCCLK:
		single_dpm_table = &(dpm_table->soc_table);

		if (soft_max_level >= single_dpm_table->count) {
			pr_err("Clock level specified %d is over max allowed %d\n",
					soft_max_level, single_dpm_table->count - 1);
			ret = -EINVAL;
			break;
		}

		single_dpm_table->dpm_state.soft_min_level =
			single_dpm_table->dpm_levels[soft_min_level].value;
		single_dpm_table->dpm_state.soft_max_level =
			single_dpm_table->dpm_levels[soft_max_level].value;

		ret = vega20_upload_dpm_level(smu, false, FEATURE_DPM_SOCCLK_MASK);
		if (ret) {
			pr_err("Failed to upload boot level to lowest!\n");
			break;
		}

		ret = vega20_upload_dpm_level(smu, true, FEATURE_DPM_SOCCLK_MASK);
		if (ret)
			pr_err("Failed to upload dpm max level to highest!\n");

		break;

	case SMU_FCLK:
		single_dpm_table = &(dpm_table->fclk_table);

		if (soft_max_level >= single_dpm_table->count) {
			pr_err("Clock level specified %d is over max allowed %d\n",
					soft_max_level, single_dpm_table->count - 1);
			ret = -EINVAL;
			break;
		}

		single_dpm_table->dpm_state.soft_min_level =
			single_dpm_table->dpm_levels[soft_min_level].value;
		single_dpm_table->dpm_state.soft_max_level =
			single_dpm_table->dpm_levels[soft_max_level].value;

		ret = vega20_upload_dpm_level(smu, false, FEATURE_DPM_FCLK_MASK);
		if (ret) {
			pr_err("Failed to upload boot level to lowest!\n");
			break;
		}

		ret = vega20_upload_dpm_level(smu, true, FEATURE_DPM_FCLK_MASK);
		if (ret)
			pr_err("Failed to upload dpm max level to highest!\n");

		break;

	case SMU_DCEFCLK:
		hard_min_level = soft_min_level;
		single_dpm_table = &(dpm_table->dcef_table);

		if (hard_min_level >= single_dpm_table->count) {
			pr_err("Clock level specified %d is over max allowed %d\n",
					hard_min_level, single_dpm_table->count - 1);
			ret = -EINVAL;
			break;
		}

		single_dpm_table->dpm_state.hard_min_level =
			single_dpm_table->dpm_levels[hard_min_level].value;

		ret = vega20_upload_dpm_level(smu, false, FEATURE_DPM_DCEFCLK_MASK);
		if (ret)
			pr_err("Failed to upload boot level to lowest!\n");

		break;

	case SMU_PCIE:
		if (soft_min_level >= NUM_LINK_LEVELS ||
		    soft_max_level >= NUM_LINK_LEVELS) {
			ret = -EINVAL;
			break;
		}

		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetMinLinkDpmByIndex,
				soft_min_level,
				NULL);
		if (ret)
			pr_err("Failed to set min link dpm level!\n");

		break;

	default:
		break;
	}

	return ret;
}

static int vega20_get_clock_by_type_with_latency(struct smu_context *smu,
						 enum smu_clk_type clk_type,
						 struct pp_clock_levels_with_latency *clocks)
{
	int ret;
	struct vega20_single_dpm_table *single_dpm_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;

	dpm_table = smu_dpm->dpm_context;

	switch (clk_type) {
	case SMU_GFXCLK:
		single_dpm_table = &(dpm_table->gfx_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	case SMU_MCLK:
		single_dpm_table = &(dpm_table->mem_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	case SMU_DCEFCLK:
		single_dpm_table = &(dpm_table->dcef_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	case SMU_SOCCLK:
		single_dpm_table = &(dpm_table->soc_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int vega20_overdrive_get_gfx_clk_base_voltage(struct smu_context *smu,
						     uint32_t *voltage,
						     uint32_t freq)
{
	int ret;

	ret = smu_send_smc_msg_with_param(smu,
			SMU_MSG_GetAVFSVoltageByDpm,
			((AVFS_CURVE << 24) | (OD8_HOTCURVE_TEMPERATURE << 16) | freq),
			voltage);
	if (ret) {
		pr_err("[GetBaseVoltage] failed to get GFXCLK AVFS voltage from SMU!");
		return ret;
	}

	*voltage = *voltage / VOLTAGE_SCALE;

	return 0;
}

static int vega20_set_default_od8_setttings(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	OverDriveTable_t *od_table = (OverDriveTable_t *)(table_context->overdrive_table);
	struct vega20_od8_settings *od8_settings = NULL;
	PPTable_t *smc_pptable = table_context->driver_pptable;
	int i, ret;

	if (smu->od_settings)
		return -EINVAL;

	od8_settings = kzalloc(sizeof(struct vega20_od8_settings), GFP_KERNEL);

	if (!od8_settings)
		return -ENOMEM;

	smu->od_settings = (void *)od8_settings;

	ret = vega20_setup_od8_information(smu);
	if (ret) {
		pr_err("Retrieve board OD limits failed!\n");
		return ret;
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_GFXCLK_LIMITS] &&
		    od8_settings->od_settings_max[OD8_SETTING_GFXCLK_FMAX] > 0 &&
		    od8_settings->od_settings_min[OD8_SETTING_GFXCLK_FMIN] > 0 &&
		    (od8_settings->od_settings_max[OD8_SETTING_GFXCLK_FMAX] >=
		     od8_settings->od_settings_min[OD8_SETTING_GFXCLK_FMIN])) {
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id =
				OD8_GFXCLK_LIMITS;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id =
				OD8_GFXCLK_LIMITS;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].default_value =
				od_table->GfxclkFmin;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].default_value =
				od_table->GfxclkFmax;
		}

		if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_GFXCLK_CURVE] &&
		    (od8_settings->od_settings_min[OD8_SETTING_GFXCLK_VOLTAGE1] >=
		     smc_pptable->MinVoltageGfx / VOLTAGE_SCALE) &&
		    (od8_settings->od_settings_max[OD8_SETTING_GFXCLK_VOLTAGE3] <=
		     smc_pptable->MaxVoltageGfx / VOLTAGE_SCALE) &&
		    (od8_settings->od_settings_min[OD8_SETTING_GFXCLK_VOLTAGE1] <=
		     od8_settings->od_settings_max[OD8_SETTING_GFXCLK_VOLTAGE3])) {
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].feature_id =
				OD8_GFXCLK_CURVE;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id =
				OD8_GFXCLK_CURVE;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].feature_id =
				OD8_GFXCLK_CURVE;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id =
				OD8_GFXCLK_CURVE;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].feature_id =
				OD8_GFXCLK_CURVE;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id =
				OD8_GFXCLK_CURVE;

			od_table->GfxclkFreq1 = od_table->GfxclkFmin;
			od_table->GfxclkFreq2 = (od_table->GfxclkFmin + od_table->GfxclkFmax) / 2;
			od_table->GfxclkFreq3 = od_table->GfxclkFmax;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].default_value =
				od_table->GfxclkFreq1;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].default_value =
				od_table->GfxclkFreq2;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].default_value =
				od_table->GfxclkFreq3;

			ret = vega20_overdrive_get_gfx_clk_base_voltage(smu,
				&od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].default_value,
				od_table->GfxclkFreq1);
			if (ret)
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].default_value = 0;
			od_table->GfxclkVolt1 =
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].default_value
				* VOLTAGE_SCALE;
			ret = vega20_overdrive_get_gfx_clk_base_voltage(smu,
				&od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].default_value,
				od_table->GfxclkFreq2);
			if (ret)
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].default_value = 0;
			od_table->GfxclkVolt2 =
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].default_value
				* VOLTAGE_SCALE;
			ret = vega20_overdrive_get_gfx_clk_base_voltage(smu,
				&od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].default_value,
				od_table->GfxclkFreq3);
			if (ret)
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].default_value = 0;
			od_table->GfxclkVolt3 =
				od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].default_value
				* VOLTAGE_SCALE;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_UCLK_MAX] &&
		    od8_settings->od_settings_min[OD8_SETTING_UCLK_FMAX] > 0 &&
		    od8_settings->od_settings_max[OD8_SETTING_UCLK_FMAX] > 0 &&
		    (od8_settings->od_settings_max[OD8_SETTING_UCLK_FMAX] >=
		     od8_settings->od_settings_min[OD8_SETTING_UCLK_FMAX])) {
			od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id =
				OD8_UCLK_MAX;
			od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].default_value =
				od_table->UclkFmax;
		}
	}

	if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_POWER_LIMIT] &&
	    od8_settings->od_settings_min[OD8_SETTING_POWER_PERCENTAGE] > 0 &&
	    od8_settings->od_settings_min[OD8_SETTING_POWER_PERCENTAGE] <= 100 &&
	    od8_settings->od_settings_max[OD8_SETTING_POWER_PERCENTAGE] > 0 &&
	    od8_settings->od_settings_max[OD8_SETTING_POWER_PERCENTAGE] <= 100) {
		od8_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].feature_id =
			OD8_POWER_LIMIT;
		od8_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].default_value =
			od_table->OverDrivePct;
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_FAN_CONTROL_BIT)) {
		if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_FAN_ACOUSTIC_LIMIT] &&
		    od8_settings->od_settings_min[OD8_SETTING_FAN_ACOUSTIC_LIMIT] > 0 &&
		    od8_settings->od_settings_max[OD8_SETTING_FAN_ACOUSTIC_LIMIT] > 0 &&
		    (od8_settings->od_settings_max[OD8_SETTING_FAN_ACOUSTIC_LIMIT] >=
		     od8_settings->od_settings_min[OD8_SETTING_FAN_ACOUSTIC_LIMIT])) {
			od8_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].feature_id =
				OD8_ACOUSTIC_LIMIT_SCLK;
			od8_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].default_value =
				od_table->FanMaximumRpm;
		}

		if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_FAN_SPEED_MIN] &&
		    od8_settings->od_settings_min[OD8_SETTING_FAN_MIN_SPEED] > 0 &&
		    od8_settings->od_settings_max[OD8_SETTING_FAN_MIN_SPEED] > 0 &&
		    (od8_settings->od_settings_max[OD8_SETTING_FAN_MIN_SPEED] >=
		     od8_settings->od_settings_min[OD8_SETTING_FAN_MIN_SPEED])) {
			od8_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].feature_id =
				OD8_FAN_SPEED_MIN;
			od8_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].default_value =
				od_table->FanMinimumPwm * smc_pptable->FanMaximumRpm / 100;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_THERMAL_BIT)) {
		if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_TEMPERATURE_FAN] &&
		    od8_settings->od_settings_min[OD8_SETTING_FAN_TARGET_TEMP] > 0 &&
		    od8_settings->od_settings_max[OD8_SETTING_FAN_TARGET_TEMP] > 0 &&
		    (od8_settings->od_settings_max[OD8_SETTING_FAN_TARGET_TEMP] >=
		     od8_settings->od_settings_min[OD8_SETTING_FAN_TARGET_TEMP])) {
			od8_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].feature_id =
				OD8_TEMPERATURE_FAN;
			od8_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].default_value =
				od_table->FanTargetTemperature;
		}

		if (od8_settings->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_TEMPERATURE_SYSTEM] &&
		    od8_settings->od_settings_min[OD8_SETTING_OPERATING_TEMP_MAX] > 0 &&
		    od8_settings->od_settings_max[OD8_SETTING_OPERATING_TEMP_MAX] > 0 &&
		    (od8_settings->od_settings_max[OD8_SETTING_OPERATING_TEMP_MAX] >=
		     od8_settings->od_settings_min[OD8_SETTING_OPERATING_TEMP_MAX])) {
			od8_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].feature_id =
				OD8_TEMPERATURE_SYSTEM;
			od8_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].default_value =
				od_table->MaxOpTemp;
		}
	}

	for (i = 0; i < OD8_SETTING_COUNT; i++) {
		if (od8_settings->od8_settings_array[i].feature_id) {
			od8_settings->od8_settings_array[i].min_value =
				od8_settings->od_settings_min[i];
			od8_settings->od8_settings_array[i].max_value =
				od8_settings->od_settings_max[i];
			od8_settings->od8_settings_array[i].current_value =
				od8_settings->od8_settings_array[i].default_value;
		} else {
			od8_settings->od8_settings_array[i].min_value = 0;
			od8_settings->od8_settings_array[i].max_value = 0;
			od8_settings->od8_settings_array[i].current_value = 0;
		}
	}

	return 0;
}

static int vega20_get_metrics_table(struct smu_context *smu,
				    SmuMetrics_t *metrics_table)
{
	struct smu_table_context *smu_table= &smu->smu_table;
	int ret = 0;

	mutex_lock(&smu->metrics_lock);
	if (!smu_table->metrics_time || time_after(jiffies, smu_table->metrics_time + HZ / 1000)) {
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

static int vega20_set_default_od_settings(struct smu_context *smu,
					  bool initialize)
{
	struct smu_table_context *table_context = &smu->smu_table;
	int ret;

	ret = smu_v11_0_set_default_od_settings(smu, initialize, sizeof(OverDriveTable_t));
	if (ret)
		return ret;

	if (initialize) {
		ret = vega20_set_default_od8_setttings(smu);
		if (ret)
			return ret;
	}

	ret = smu_update_table(smu, SMU_TABLE_OVERDRIVE, 0,
			       table_context->overdrive_table, true);
	if (ret) {
		pr_err("Failed to import over drive table!\n");
		return ret;
	}

	return 0;
}

static int vega20_get_od_percentage(struct smu_context *smu,
				    enum smu_clk_type clk_type)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;
	struct vega20_dpm_table *golden_table = NULL;
	struct vega20_single_dpm_table *single_dpm_table;
	struct vega20_single_dpm_table *golden_dpm_table;
	int value, golden_value;

	dpm_table = smu_dpm->dpm_context;
	golden_table = smu_dpm->golden_dpm_context;

	switch (clk_type) {
	case SMU_OD_SCLK:
		single_dpm_table = &(dpm_table->gfx_table);
		golden_dpm_table = &(golden_table->gfx_table);
		break;
	case SMU_OD_MCLK:
		single_dpm_table = &(dpm_table->mem_table);
		golden_dpm_table = &(golden_table->mem_table);
		break;
	default:
		return -EINVAL;
		break;
	}

	value = single_dpm_table->dpm_levels[single_dpm_table->count - 1].value;
	golden_value = golden_dpm_table->dpm_levels[golden_dpm_table->count - 1].value;

	value -= golden_value;
	value = DIV_ROUND_UP(value * 100, golden_value);

	return value;
}

static int vega20_get_power_profile_mode(struct smu_context *smu, char *buf)
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
			"UseRlcBusy",
			"MinActiveFreqType",
			"MinActiveFreq",
			"BoosterFreqType",
			"BoosterFreq",
			"PD_Data_limit_c",
			"PD_Data_error_coeff",
			"PD_Data_error_rate_coeff"};
	int result = 0;

	if (!smu->pm_enabled || !buf)
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
			"SOCCLK",
			activity_monitor.Soc_FPS,
			activity_monitor.Soc_UseRlcBusy,
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

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			3,
			"FCLK",
			activity_monitor.Fclk_FPS,
			activity_monitor.Fclk_UseRlcBusy,
			activity_monitor.Fclk_MinActiveFreqType,
			activity_monitor.Fclk_MinActiveFreq,
			activity_monitor.Fclk_BoosterFreqType,
			activity_monitor.Fclk_BoosterFreq,
			activity_monitor.Fclk_PD_Data_limit_c,
			activity_monitor.Fclk_PD_Data_error_coeff,
			activity_monitor.Fclk_PD_Data_error_rate_coeff);
	}

	return size;
}

static int vega20_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	int workload_type = 0, ret = 0;

	smu->power_profile_mode = input[size];

	if (!smu->pm_enabled)
		return ret;
	if (smu->power_profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		pr_err("Invalid power profile mode %d\n", smu->power_profile_mode);
		return -EINVAL;
	}

	if (smu->power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {
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
			activity_monitor.Gfx_UseRlcBusy = input[2];
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
			activity_monitor.Soc_UseRlcBusy = input[2];
			activity_monitor.Soc_MinActiveFreqType = input[3];
			activity_monitor.Soc_MinActiveFreq = input[4];
			activity_monitor.Soc_BoosterFreqType = input[5];
			activity_monitor.Soc_BoosterFreq = input[6];
			activity_monitor.Soc_PD_Data_limit_c = input[7];
			activity_monitor.Soc_PD_Data_error_coeff = input[8];
			activity_monitor.Soc_PD_Data_error_rate_coeff = input[9];
			break;
		case 2: /* Uclk */
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
		case 3: /* Fclk */
			activity_monitor.Fclk_FPS = input[1];
			activity_monitor.Fclk_UseRlcBusy = input[2];
			activity_monitor.Fclk_MinActiveFreqType = input[3];
			activity_monitor.Fclk_MinActiveFreq = input[4];
			activity_monitor.Fclk_BoosterFreqType = input[5];
			activity_monitor.Fclk_BoosterFreq = input[6];
			activity_monitor.Fclk_PD_Data_limit_c = input[7];
			activity_monitor.Fclk_PD_Data_error_coeff = input[8];
			activity_monitor.Fclk_PD_Data_error_rate_coeff = input[9];
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
	smu_send_smc_msg_with_param(smu,
			SMU_MSG_SetWorkloadMask,
			1 << workload_type,
			NULL);

	return ret;
}

static int
vega20_get_profiling_clk_mask(struct smu_context *smu,
			      enum amd_dpm_forced_level level,
			      uint32_t *sclk_mask,
			      uint32_t *mclk_mask,
			      uint32_t *soc_mask)
{
	struct vega20_dpm_table *dpm_table = (struct vega20_dpm_table *)smu->smu_dpm.dpm_context;
	struct vega20_single_dpm_table *gfx_dpm_table;
	struct vega20_single_dpm_table *mem_dpm_table;
	struct vega20_single_dpm_table *soc_dpm_table;

	if (!smu->smu_dpm.dpm_context)
		return -EINVAL;

	gfx_dpm_table = &dpm_table->gfx_table;
	mem_dpm_table = &dpm_table->mem_table;
	soc_dpm_table = &dpm_table->soc_table;

	*sclk_mask = 0;
	*mclk_mask = 0;
	*soc_mask  = 0;

	if (gfx_dpm_table->count > VEGA20_UMD_PSTATE_GFXCLK_LEVEL &&
	    mem_dpm_table->count > VEGA20_UMD_PSTATE_MCLK_LEVEL &&
	    soc_dpm_table->count > VEGA20_UMD_PSTATE_SOCCLK_LEVEL) {
		*sclk_mask = VEGA20_UMD_PSTATE_GFXCLK_LEVEL;
		*mclk_mask = VEGA20_UMD_PSTATE_MCLK_LEVEL;
		*soc_mask  = VEGA20_UMD_PSTATE_SOCCLK_LEVEL;
	}

	if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
		*sclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
		*mclk_mask = 0;
	} else if (level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
		*sclk_mask = gfx_dpm_table->count - 1;
		*mclk_mask = mem_dpm_table->count - 1;
		*soc_mask  = soc_dpm_table->count - 1;
	}

	return 0;
}

static int
vega20_set_uclk_to_highest_dpm_level(struct smu_context *smu,
				     struct vega20_single_dpm_table *dpm_table)
{
	int ret = 0;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	if (!smu_dpm_ctx->dpm_context)
		return -EINVAL;

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		if (dpm_table->count <= 0) {
			pr_err("[%s] Dpm table has no entry!", __func__);
				return -EINVAL;
		}

		if (dpm_table->count > NUM_UCLK_DPM_LEVELS) {
			pr_err("[%s] Dpm table has too many entries!", __func__);
				return -EINVAL;
		}

		dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetHardMinByFreq,
				(PPCLK_UCLK << 16) | dpm_table->dpm_state.hard_min_level,
				NULL);
		if (ret) {
			pr_err("[%s] Set hard min uclk failed!", __func__);
				return ret;
		}
	}

	return ret;
}

static int vega20_pre_display_config_changed(struct smu_context *smu)
{
	int ret = 0;
	struct vega20_dpm_table *dpm_table = smu->smu_dpm.dpm_context;

	if (!smu->smu_dpm.dpm_context)
		return -EINVAL;

	smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, 0, NULL);
	ret = vega20_set_uclk_to_highest_dpm_level(smu,
						   &dpm_table->mem_table);
	if (ret)
		pr_err("Failed to set uclk to highest dpm level");
	return ret;
}

static int vega20_display_config_changed(struct smu_context *smu)
{
	int ret = 0;

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    !(smu->watermarks_bitmap & WATERMARKS_LOADED)) {
		ret = smu_write_watermarks_table(smu);
		if (ret) {
			pr_err("Failed to update WMTABLE!");
			return ret;
		}
		smu->watermarks_bitmap |= WATERMARKS_LOADED;
	}

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    smu_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) &&
	    smu_feature_is_supported(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		smu_send_smc_msg_with_param(smu,
					    SMU_MSG_NumOfDisplays,
					    smu->display_config->num_display,
					    NULL);
	}

	return ret;
}

static int vega20_apply_clocks_adjust_rules(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	struct vega20_dpm_table *dpm_ctx = (struct vega20_dpm_table *)(smu_dpm_ctx->dpm_context);
	struct vega20_single_dpm_table *dpm_table;
	bool vblank_too_short = false;
	bool disable_mclk_switching;
	uint32_t i, latency;

	disable_mclk_switching = ((1 < smu->display_config->num_display) &&
				  !smu->display_config->multi_monitor_in_sync) || vblank_too_short;
	latency = smu->display_config->dce_tolerable_mclk_in_active_latency;

	/* gfxclk */
	dpm_table = &(dpm_ctx->gfx_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

		if (VEGA20_UMD_PSTATE_GFXCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_GFXCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_GFXCLK_LEVEL].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[0].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}

	/* memclk */
	dpm_table = &(dpm_ctx->mem_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

		if (VEGA20_UMD_PSTATE_MCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_MCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_MCLK_LEVEL].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[0].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}

	/* honour DAL's UCLK Hardmin */
	if (dpm_table->dpm_state.hard_min_level < (smu->display_config->min_mem_set_clock / 100))
		dpm_table->dpm_state.hard_min_level = smu->display_config->min_mem_set_clock / 100;

	/* Hardmin is dependent on displayconfig */
	if (disable_mclk_switching) {
		dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		for (i = 0; i < smu_dpm_ctx->mclk_latency_table->count - 1; i++) {
			if (smu_dpm_ctx->mclk_latency_table->entries[i].latency <= latency) {
				if (dpm_table->dpm_levels[i].value >= (smu->display_config->min_mem_set_clock / 100)) {
					dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[i].value;
					break;
				}
			}
		}
	}

	if (smu->display_config->nb_pstate_switch_disable)
		dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

	/* vclk */
	dpm_table = &(dpm_ctx->vclk_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

		if (VEGA20_UMD_PSTATE_UVDCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}

	/* dclk */
	dpm_table = &(dpm_ctx->dclk_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

		if (VEGA20_UMD_PSTATE_UVDCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_UVDCLK_LEVEL].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}

	/* socclk */
	dpm_table = &(dpm_ctx->soc_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

		if (VEGA20_UMD_PSTATE_SOCCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_SOCCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_SOCCLK_LEVEL].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}

	/* eclk */
	dpm_table = &(dpm_ctx->eclk_table);
	dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
	dpm_table->dpm_state.hard_min_level = dpm_table->dpm_levels[0].value;
	dpm_table->dpm_state.hard_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;

		if (VEGA20_UMD_PSTATE_VCEMCLK_LEVEL < dpm_table->count) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_VCEMCLK_LEVEL].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[VEGA20_UMD_PSTATE_VCEMCLK_LEVEL].value;
		}

		if (smu_dpm_ctx->dpm_level == AMD_DPM_FORCED_LEVEL_PROFILE_PEAK) {
			dpm_table->dpm_state.soft_min_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
			dpm_table->dpm_state.soft_max_level = dpm_table->dpm_levels[dpm_table->count - 1].value;
		}
	return 0;
}

static int
vega20_notify_smc_display_config(struct smu_context *smu)
{
	struct vega20_dpm_table *dpm_table = smu->smu_dpm.dpm_context;
	struct vega20_single_dpm_table *memtable = &dpm_table->mem_table;
	struct smu_clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;
	int ret = 0;

	min_clocks.dcef_clock = smu->display_config->min_dcef_set_clk;
	min_clocks.dcef_clock_in_sr = smu->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memory_clock = smu->display_config->min_mem_set_clock;

	if (smu_feature_is_supported(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcef_clock * 10;
		if (!smu_v11_0_display_clock_voltage_request(smu, &clock_req)) {
			if (smu_feature_is_supported(smu, SMU_FEATURE_DS_DCEFCLK_BIT)) {
				ret = smu_send_smc_msg_with_param(smu,
								  SMU_MSG_SetMinDeepSleepDcefclk,
								  min_clocks.dcef_clock_in_sr/100,
								  NULL);
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
		memtable->dpm_state.hard_min_level = min_clocks.memory_clock/100;
		ret = smu_send_smc_msg_with_param(smu,
						  SMU_MSG_SetHardMinByFreq,
						  (PPCLK_UCLK << 16) | memtable->dpm_state.hard_min_level,
						  NULL);
		if (ret) {
			pr_err("[%s] Set hard min uclk failed!", __func__);
			return ret;
		}
	}

	return 0;
}

static uint32_t vega20_find_lowest_dpm_level(struct vega20_single_dpm_table *table)
{
	uint32_t i;

	for (i = 0; i < table->count; i++) {
		if (table->dpm_levels[i].enabled)
			break;
	}
	if (i >= table->count) {
		i = 0;
		table->dpm_levels[i].enabled = true;
	}

	return i;
}

static uint32_t vega20_find_highest_dpm_level(struct vega20_single_dpm_table *table)
{
	int i = 0;

	if (!table) {
		pr_err("[%s] DPM Table does not exist!", __func__);
		return 0;
	}
	if (table->count <= 0) {
		pr_err("[%s] DPM Table has no entry!", __func__);
		return 0;
	}
	if (table->count > MAX_REGULAR_DPM_NUMBER) {
		pr_err("[%s] DPM Table has too many entries!", __func__);
		return MAX_REGULAR_DPM_NUMBER - 1;
	}

	for (i = table->count - 1; i >= 0; i--) {
		if (table->dpm_levels[i].enabled)
			break;
	}
	if (i < 0) {
		i = 0;
		table->dpm_levels[i].enabled = true;
	}

	return i;
}

static int vega20_force_dpm_limit_value(struct smu_context *smu, bool highest)
{
	uint32_t soft_level;
	int ret = 0;
	struct vega20_dpm_table *dpm_table =
		(struct vega20_dpm_table *)smu->smu_dpm.dpm_context;

	if (highest)
		soft_level = vega20_find_highest_dpm_level(&(dpm_table->gfx_table));
	else
		soft_level = vega20_find_lowest_dpm_level(&(dpm_table->gfx_table));

	dpm_table->gfx_table.dpm_state.soft_min_level =
		dpm_table->gfx_table.dpm_state.soft_max_level =
		dpm_table->gfx_table.dpm_levels[soft_level].value;

	if (highest)
		soft_level = vega20_find_highest_dpm_level(&(dpm_table->mem_table));
	else
		soft_level = vega20_find_lowest_dpm_level(&(dpm_table->mem_table));

	dpm_table->mem_table.dpm_state.soft_min_level =
		dpm_table->mem_table.dpm_state.soft_max_level =
		dpm_table->mem_table.dpm_levels[soft_level].value;

	if (highest)
		soft_level = vega20_find_highest_dpm_level(&(dpm_table->soc_table));
	else
		soft_level = vega20_find_lowest_dpm_level(&(dpm_table->soc_table));

	dpm_table->soc_table.dpm_state.soft_min_level =
		dpm_table->soc_table.dpm_state.soft_max_level =
		dpm_table->soc_table.dpm_levels[soft_level].value;

	ret = vega20_upload_dpm_level(smu, false, 0xFFFFFFFF);
	if (ret) {
		pr_err("Failed to upload boot level to %s!\n",
				highest ? "highest" : "lowest");
		return ret;
	}

	ret = vega20_upload_dpm_level(smu, true, 0xFFFFFFFF);
	if (ret) {
		pr_err("Failed to upload dpm max level to %s!\n!",
				highest ? "highest" : "lowest");
		return ret;
	}

	return ret;
}

static int vega20_unforce_dpm_levels(struct smu_context *smu)
{
	uint32_t soft_min_level, soft_max_level;
	int ret = 0;
	struct vega20_dpm_table *dpm_table =
		(struct vega20_dpm_table *)smu->smu_dpm.dpm_context;

	soft_min_level = vega20_find_lowest_dpm_level(&(dpm_table->gfx_table));
	soft_max_level = vega20_find_highest_dpm_level(&(dpm_table->gfx_table));
	dpm_table->gfx_table.dpm_state.soft_min_level =
		dpm_table->gfx_table.dpm_levels[soft_min_level].value;
	dpm_table->gfx_table.dpm_state.soft_max_level =
		dpm_table->gfx_table.dpm_levels[soft_max_level].value;

	soft_min_level = vega20_find_lowest_dpm_level(&(dpm_table->mem_table));
	soft_max_level = vega20_find_highest_dpm_level(&(dpm_table->mem_table));
	dpm_table->mem_table.dpm_state.soft_min_level =
		dpm_table->gfx_table.dpm_levels[soft_min_level].value;
	dpm_table->mem_table.dpm_state.soft_max_level =
		dpm_table->gfx_table.dpm_levels[soft_max_level].value;

	soft_min_level = vega20_find_lowest_dpm_level(&(dpm_table->soc_table));
	soft_max_level = vega20_find_highest_dpm_level(&(dpm_table->soc_table));
	dpm_table->soc_table.dpm_state.soft_min_level =
		dpm_table->soc_table.dpm_levels[soft_min_level].value;
	dpm_table->soc_table.dpm_state.soft_max_level =
		dpm_table->soc_table.dpm_levels[soft_max_level].value;

	ret = vega20_upload_dpm_level(smu, false, 0xFFFFFFFF);
	if (ret) {
		pr_err("Failed to upload DPM Bootup Levels!");
		return ret;
	}

	ret = vega20_upload_dpm_level(smu, true, 0xFFFFFFFF);
	if (ret) {
		pr_err("Failed to upload DPM Max Levels!");
		return ret;
	}

	return ret;
}

static int vega20_update_specified_od8_value(struct smu_context *smu,
					     uint32_t index,
					     uint32_t value)
{
	struct smu_table_context *table_context = &smu->smu_table;
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)(table_context->overdrive_table);
	struct vega20_od8_settings *od8_settings =
		(struct vega20_od8_settings *)smu->od_settings;

	switch (index) {
	case OD8_SETTING_GFXCLK_FMIN:
		od_table->GfxclkFmin = (uint16_t)value;
		break;

	case OD8_SETTING_GFXCLK_FMAX:
		if (value < od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].min_value ||
		    value > od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].max_value)
			return -EINVAL;
		od_table->GfxclkFmax = (uint16_t)value;
		break;

	case OD8_SETTING_GFXCLK_FREQ1:
		od_table->GfxclkFreq1 = (uint16_t)value;
		break;

	case OD8_SETTING_GFXCLK_VOLTAGE1:
		od_table->GfxclkVolt1 = (uint16_t)value;
		break;

	case OD8_SETTING_GFXCLK_FREQ2:
		od_table->GfxclkFreq2 = (uint16_t)value;
		break;

	case OD8_SETTING_GFXCLK_VOLTAGE2:
		od_table->GfxclkVolt2 = (uint16_t)value;
		break;

	case OD8_SETTING_GFXCLK_FREQ3:
		od_table->GfxclkFreq3 = (uint16_t)value;
		break;

	case OD8_SETTING_GFXCLK_VOLTAGE3:
		od_table->GfxclkVolt3 = (uint16_t)value;
		break;

	case OD8_SETTING_UCLK_FMAX:
		if (value < od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].min_value ||
		    value > od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].max_value)
			return -EINVAL;
		od_table->UclkFmax = (uint16_t)value;
		break;

	case OD8_SETTING_POWER_PERCENTAGE:
		od_table->OverDrivePct = (int16_t)value;
		break;

	case OD8_SETTING_FAN_ACOUSTIC_LIMIT:
		od_table->FanMaximumRpm = (uint16_t)value;
		break;

	case OD8_SETTING_FAN_MIN_SPEED:
		od_table->FanMinimumPwm = (uint16_t)value;
		break;

	case OD8_SETTING_FAN_TARGET_TEMP:
		od_table->FanTargetTemperature = (uint16_t)value;
		break;

	case OD8_SETTING_OPERATING_TEMP_MAX:
		od_table->MaxOpTemp = (uint16_t)value;
		break;
	}

	return 0;
}

static int vega20_update_od8_settings(struct smu_context *smu,
				      uint32_t index,
				      uint32_t value)
{
	struct smu_table_context *table_context = &smu->smu_table;
	int ret;

	ret = smu_update_table(smu, SMU_TABLE_OVERDRIVE, 0,
			       table_context->overdrive_table, false);
	if (ret) {
		pr_err("Failed to export over drive table!\n");
		return ret;
	}

	ret = vega20_update_specified_od8_value(smu, index, value);
	if (ret)
		return ret;

	ret = smu_update_table(smu, SMU_TABLE_OVERDRIVE, 0,
			       table_context->overdrive_table, true);
	if (ret) {
		pr_err("Failed to import over drive table!\n");
		return ret;
	}

	return 0;
}

static int vega20_set_od_percentage(struct smu_context *smu,
				    enum smu_clk_type clk_type,
				    uint32_t value)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;
	struct vega20_dpm_table *golden_table = NULL;
	struct vega20_single_dpm_table *single_dpm_table;
	struct vega20_single_dpm_table *golden_dpm_table;
	uint32_t od_clk, index;
	int ret = 0;
	int feature_enabled;
	PPCLK_e clk_id;

	dpm_table = smu_dpm->dpm_context;
	golden_table = smu_dpm->golden_dpm_context;

	switch (clk_type) {
	case SMU_OD_SCLK:
		single_dpm_table = &(dpm_table->gfx_table);
		golden_dpm_table = &(golden_table->gfx_table);
		feature_enabled = smu_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT);
		clk_id = PPCLK_GFXCLK;
		index = OD8_SETTING_GFXCLK_FMAX;
		break;
	case SMU_OD_MCLK:
		single_dpm_table = &(dpm_table->mem_table);
		golden_dpm_table = &(golden_table->mem_table);
		feature_enabled = smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT);
		clk_id = PPCLK_UCLK;
		index = OD8_SETTING_UCLK_FMAX;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		goto set_od_failed;

	od_clk = golden_dpm_table->dpm_levels[golden_dpm_table->count - 1].value * value;
	od_clk /= 100;
	od_clk += golden_dpm_table->dpm_levels[golden_dpm_table->count - 1].value;

	ret = vega20_update_od8_settings(smu, index, od_clk);
	if (ret) {
		pr_err("[Setoverdrive] failed to set od clk!\n");
		goto set_od_failed;
	}

	if (feature_enabled) {
		ret = vega20_set_single_dpm_table(smu, single_dpm_table,
						  clk_id);
		if (ret) {
			pr_err("[Setoverdrive] failed to refresh dpm table!\n");
			goto set_od_failed;
		}
	} else {
		single_dpm_table->count = 1;
		single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.gfxclk / 100;
	}

	ret = smu_handle_task(smu, smu_dpm->dpm_level,
			      AMD_PP_TASK_READJUST_POWER_STATE,
			      false);

set_od_failed:
	return ret;
}

static int vega20_odn_edit_dpm_table(struct smu_context *smu,
				     enum PP_OD_DPM_TABLE_COMMAND type,
				     long *input, uint32_t size)
{
	struct smu_table_context *table_context = &smu->smu_table;
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)(table_context->overdrive_table);
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;
	struct vega20_single_dpm_table *single_dpm_table;
	struct vega20_od8_settings *od8_settings =
		(struct vega20_od8_settings *)smu->od_settings;
	struct pp_clock_levels_with_latency clocks;
	int32_t input_index, input_clk, input_vol, i;
	int od8_id;
	int ret = 0;

	dpm_table = smu_dpm->dpm_context;

	if (!input) {
		pr_warn("NULL user input for clock and voltage\n");
		return -EINVAL;
	}

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (!(od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id &&
		      od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id)) {
			pr_info("Sclk min/max frequency overdrive not supported\n");
			return -EOPNOTSUPP;
		}

		for (i = 0; i < size; i += 2) {
			if (i + 2 > size) {
				pr_info("invalid number of input parameters %d\n", size);
				return -EINVAL;
			}

			input_index = input[i];
			input_clk = input[i + 1];

			if (input_index != 0 && input_index != 1) {
				pr_info("Invalid index %d\n", input_index);
				pr_info("Support min/max sclk frequency settingonly which index by 0/1\n");
				return -EINVAL;
			}

			if (input_clk < od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].min_value ||
			    input_clk > od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].max_value) {
				pr_info("clock freq %d is not within allowed range [%d - %d]\n",
					input_clk,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].min_value,
					od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].max_value);
				return -EINVAL;
			}

			if (input_index == 0 && od_table->GfxclkFmin != input_clk) {
				od_table->GfxclkFmin = input_clk;
				od8_settings->od_gfxclk_update = true;
			} else if (input_index == 1 && od_table->GfxclkFmax != input_clk) {
				od_table->GfxclkFmax = input_clk;
				od8_settings->od_gfxclk_update = true;
			}
		}

		break;

	case PP_OD_EDIT_MCLK_VDDC_TABLE:
		if (!od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id) {
			pr_info("Mclk max frequency overdrive not supported\n");
			return -EOPNOTSUPP;
		}

		single_dpm_table = &(dpm_table->mem_table);
		ret = vega20_get_clk_table(smu, &clocks, single_dpm_table);
		if (ret) {
			pr_err("Attempt to get memory clk levels Failed!");
			return ret;
		}

		for (i = 0; i < size; i += 2) {
			if (i + 2 > size) {
				pr_info("invalid number of input parameters %d\n",
					 size);
				return -EINVAL;
			}

			input_index = input[i];
			input_clk = input[i + 1];

			if (input_index != 1) {
				pr_info("Invalid index %d\n", input_index);
				pr_info("Support max Mclk frequency setting only which index by 1\n");
				return -EINVAL;
			}

			if (input_clk < clocks.data[0].clocks_in_khz / 1000 ||
			    input_clk > od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].max_value) {
				pr_info("clock freq %d is not within allowed range [%d - %d]\n",
					input_clk,
					clocks.data[0].clocks_in_khz / 1000,
					od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].max_value);
				return -EINVAL;
			}

			if (input_index == 1 && od_table->UclkFmax != input_clk) {
				od8_settings->od_gfxclk_update = true;
				od_table->UclkFmax = input_clk;
			}
		}

		break;

	case PP_OD_EDIT_VDDC_CURVE:
		if (!(od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ1].feature_id &&
		      od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ2].feature_id &&
		      od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FREQ3].feature_id &&
		      od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE1].feature_id &&
		      od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE2].feature_id &&
		      od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_VOLTAGE3].feature_id)) {
			pr_info("Voltage curve calibrate not supported\n");
			return -EOPNOTSUPP;
		}

		for (i = 0; i < size; i += 3) {
			if (i + 3 > size) {
				pr_info("invalid number of input parameters %d\n",
					size);
				return -EINVAL;
			}

			input_index = input[i];
			input_clk = input[i + 1];
			input_vol = input[i + 2];

			if (input_index > 2) {
				pr_info("Setting for point %d is not supported\n",
					input_index + 1);
				pr_info("Three supported points index by 0, 1, 2\n");
				return -EINVAL;
			}

			od8_id = OD8_SETTING_GFXCLK_FREQ1 + 2 * input_index;
			if (input_clk < od8_settings->od8_settings_array[od8_id].min_value ||
			    input_clk > od8_settings->od8_settings_array[od8_id].max_value) {
				pr_info("clock freq %d is not within allowed range [%d - %d]\n",
					input_clk,
					od8_settings->od8_settings_array[od8_id].min_value,
					od8_settings->od8_settings_array[od8_id].max_value);
				return -EINVAL;
			}

			od8_id = OD8_SETTING_GFXCLK_VOLTAGE1 + 2 * input_index;
			if (input_vol < od8_settings->od8_settings_array[od8_id].min_value ||
			    input_vol > od8_settings->od8_settings_array[od8_id].max_value) {
				pr_info("clock voltage %d is not within allowed range [%d- %d]\n",
					input_vol,
					od8_settings->od8_settings_array[od8_id].min_value,
					od8_settings->od8_settings_array[od8_id].max_value);
				return -EINVAL;
			}

			switch (input_index) {
			case 0:
				od_table->GfxclkFreq1 = input_clk;
				od_table->GfxclkVolt1 = input_vol * VOLTAGE_SCALE;
				break;
			case 1:
				od_table->GfxclkFreq2 = input_clk;
				od_table->GfxclkVolt2 = input_vol * VOLTAGE_SCALE;
				break;
			case 2:
				od_table->GfxclkFreq3 = input_clk;
				od_table->GfxclkVolt3 = input_vol * VOLTAGE_SCALE;
				break;
			}
		}

		break;

	case PP_OD_RESTORE_DEFAULT_TABLE:
		if (!(table_context->overdrive_table && table_context->boot_overdrive_table)) {
			pr_err("Overdrive table was not initialized!\n");
			return -EINVAL;
		}
		memcpy(table_context->overdrive_table, table_context->boot_overdrive_table, sizeof(OverDriveTable_t));
		break;

	case PP_OD_COMMIT_DPM_TABLE:
		ret = smu_update_table(smu, SMU_TABLE_OVERDRIVE, 0, table_context->overdrive_table, true);
		if (ret) {
			pr_err("Failed to import over drive table!\n");
			return ret;
		}

		/* retrieve updated gfxclk table */
		if (od8_settings->od_gfxclk_update) {
			od8_settings->od_gfxclk_update = false;
			single_dpm_table = &(dpm_table->gfx_table);

			if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT)) {
				ret = vega20_set_single_dpm_table(smu, single_dpm_table,
								  PPCLK_GFXCLK);
				if (ret) {
					pr_err("[Setoverdrive] failed to refresh dpm table!\n");
					return ret;
				}
			} else {
				single_dpm_table->count = 1;
				single_dpm_table->dpm_levels[0].value = smu->smu_table.boot_values.gfxclk / 100;
			}
		}

		break;

	default:
		return -EINVAL;
	}

	if (type == PP_OD_COMMIT_DPM_TABLE) {
		ret = smu_handle_task(smu, smu_dpm->dpm_level,
				      AMD_PP_TASK_READJUST_POWER_STATE,
				      false);
	}

	return ret;
}

static int vega20_dpm_set_uvd_enable(struct smu_context *smu, bool enable)
{
	if (!smu_feature_is_supported(smu, SMU_FEATURE_DPM_UVD_BIT))
		return 0;

	if (enable == smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UVD_BIT))
		return 0;

	return smu_feature_set_enabled(smu, SMU_FEATURE_DPM_UVD_BIT, enable);
}

static int vega20_dpm_set_vce_enable(struct smu_context *smu, bool enable)
{
	if (!smu_feature_is_supported(smu, SMU_FEATURE_DPM_VCE_BIT))
		return 0;

	if (enable == smu_feature_is_enabled(smu, SMU_FEATURE_DPM_VCE_BIT))
		return 0;

	return smu_feature_set_enabled(smu, SMU_FEATURE_DPM_VCE_BIT, enable);
}

static bool vega20_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint32_t feature_mask[2];
	unsigned long feature_enabled;
	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	feature_enabled = (unsigned long)((uint64_t)feature_mask[0] |
			   ((uint64_t)feature_mask[1] << 32));
	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int vega20_set_thermal_fan_table(struct smu_context *smu)
{
	int ret;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;

	ret = smu_send_smc_msg_with_param(smu,
			SMU_MSG_SetFanTemperatureTarget,
			(uint32_t)pptable->FanTargetTemperature,
			NULL);

	return ret;
}

static int vega20_get_fan_speed_rpm(struct smu_context *smu,
				    uint32_t *speed)
{
	int ret;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetCurrentRpm, speed);

	if (ret) {
		pr_err("Attempt to get current RPM from SMC Failed!\n");
		return ret;
	}

	return 0;
}

static int vega20_get_fan_speed_percent(struct smu_context *smu,
					uint32_t *speed)
{
	int ret = 0;
	uint32_t current_rpm = 0, percent = 0;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	ret = vega20_get_fan_speed_rpm(smu, &current_rpm);
	if (ret)
		return ret;

	percent = current_rpm * 100 / pptable->FanMaximumRpm;
	*speed = percent > 100 ? 100 : percent;

	return 0;
}

static int vega20_get_gpu_power(struct smu_context *smu, uint32_t *value)
{
	uint32_t smu_version;
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = vega20_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	ret = smu_get_smc_version(smu, NULL, &smu_version);
	if (ret)
		return ret;

	/* For the 40.46 release, they changed the value name */
	if (smu_version == 0x282e00)
		*value = metrics.AverageSocketPower << 8;
	else
		*value = metrics.CurrSocketPower << 8;

	return 0;
}

static int vega20_get_current_activity_percent(struct smu_context *smu,
					       enum amd_pp_sensors sensor,
					       uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = vega20_get_metrics_table(smu, &metrics);
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

static int vega20_thermal_get_temperature(struct smu_context *smu,
					     enum amd_pp_sensors sensor,
					     uint32_t *value)
{
	struct amdgpu_device *adev = smu->adev;
	SmuMetrics_t metrics;
	uint32_t temp = 0;
	int ret = 0;

	if (!value)
		return -EINVAL;

	ret = vega20_get_metrics_table(smu, &metrics);
	if (ret)
		return ret;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		temp = RREG32_SOC15(THM, 0, mmCG_MULT_THERMAL_STATUS);
		temp = (temp & CG_MULT_THERMAL_STATUS__CTF_TEMP_MASK) >>
				CG_MULT_THERMAL_STATUS__CTF_TEMP__SHIFT;

		temp = temp & 0x1ff;
		temp *= SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

		*value = temp;
		break;
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
		*value = metrics.TemperatureEdge *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		*value = metrics.TemperatureHBM *
			SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	default:
		pr_err("Invalid sensor for retrieving temp\n");
		return -EINVAL;
	}

	return 0;
}
static int vega20_read_sensor(struct smu_context *smu,
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
		ret = vega20_get_current_activity_percent(smu,
						sensor,
						(uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = vega20_get_gpu_power(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
	case AMDGPU_PP_SENSOR_EDGE_TEMP:
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = vega20_thermal_get_temperature(smu, sensor, (uint32_t *)data);
		*size = 4;
		break;
	default:
		ret = smu_v11_0_read_sensor(smu, sensor, data, size);
	}
	mutex_unlock(&smu->sensor_lock);

	return ret;
}

static int vega20_set_watermarks_table(struct smu_context *smu,
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

static int vega20_get_thermal_temperature_range(struct smu_context *smu,
						struct smu_temperature_range *range)
{
	struct smu_table_context *table_context = &smu->smu_table;
	ATOM_Vega20_POWERPLAYTABLE *powerplay_table = table_context->power_play_table;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	if (!range || !powerplay_table)
		return -EINVAL;

	range->max = powerplay_table->usSoftwareShutdownTemp *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->edge_emergency_max = (pptable->TedgeLimit + CTF_OFFSET_EDGE) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_crit_max = pptable->ThotspotLimit *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->hotspot_emergency_max = (pptable->ThotspotLimit + CTF_OFFSET_HOTSPOT) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_crit_max = pptable->ThbmLimit *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
	range->mem_emergency_max = (pptable->ThbmLimit + CTF_OFFSET_HBM) *
		SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;


	return 0;
}

static int vega20_set_df_cstate(struct smu_context *smu,
				enum pp_df_cstate state)
{
	uint32_t smu_version;
	int ret;

	ret = smu_get_smc_version(smu, NULL, &smu_version);
	if (ret) {
		pr_err("Failed to get smu version!\n");
		return ret;
	}

	/* PPSMC_MSG_DFCstateControl is supported with 40.50 and later fws */
	if (smu_version < 0x283200) {
		pr_err("Df cstate control is supported with 40.50 and later SMC fw!\n");
		return -EINVAL;
	}

	return smu_send_smc_msg_with_param(smu, SMU_MSG_DFCstateControl, state, NULL);
}

static int vega20_update_pcie_parameters(struct smu_context *smu,
				     uint32_t pcie_gen_cap,
				     uint32_t pcie_width_cap)
{
	PPTable_t *pptable = smu->smu_table.driver_pptable;
	int ret, i;
	uint32_t smu_pcie_arg;

	for (i = 0; i < NUM_LINK_LEVELS; i++) {
		smu_pcie_arg = (i << 16) |
			((pptable->PcieGenSpeed[i] <= pcie_gen_cap) ? (pptable->PcieGenSpeed[i] << 8) :
				(pcie_gen_cap << 8)) | ((pptable->PcieLaneCount[i] <= pcie_width_cap) ?
					pptable->PcieLaneCount[i] : pcie_width_cap);
		ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_OverridePcieParameters,
					  smu_pcie_arg,
					  NULL);
	}

	return ret;
}


static const struct pptable_funcs vega20_ppt_funcs = {
	.tables_init = vega20_tables_init,
	.alloc_dpm_context = vega20_allocate_dpm_context,
	.store_powerplay_table = vega20_store_powerplay_table,
	.check_powerplay_table = vega20_check_powerplay_table,
	.append_powerplay_table = vega20_append_powerplay_table,
	.get_smu_msg_index = vega20_get_smu_msg_index,
	.get_smu_clk_index = vega20_get_smu_clk_index,
	.get_smu_feature_index = vega20_get_smu_feature_index,
	.get_smu_table_index = vega20_get_smu_table_index,
	.get_smu_power_index = vega20_get_pwr_src_index,
	.get_workload_type = vega20_get_workload_type,
	.run_btc = vega20_run_btc_afll,
	.get_allowed_feature_mask = vega20_get_allowed_feature_mask,
	.get_current_power_state = vega20_get_current_power_state,
	.set_default_dpm_table = vega20_set_default_dpm_table,
	.set_power_state = NULL,
	.populate_umd_state_clk = vega20_populate_umd_state_clk,
	.print_clk_levels = vega20_print_clk_levels,
	.force_clk_levels = vega20_force_clk_levels,
	.get_clock_by_type_with_latency = vega20_get_clock_by_type_with_latency,
	.get_od_percentage = vega20_get_od_percentage,
	.get_power_profile_mode = vega20_get_power_profile_mode,
	.set_power_profile_mode = vega20_set_power_profile_mode,
	.set_performance_level = smu_v11_0_set_performance_level,
	.set_od_percentage = vega20_set_od_percentage,
	.set_default_od_settings = vega20_set_default_od_settings,
	.od_edit_dpm_table = vega20_odn_edit_dpm_table,
	.dpm_set_uvd_enable = vega20_dpm_set_uvd_enable,
	.dpm_set_vce_enable = vega20_dpm_set_vce_enable,
	.read_sensor = vega20_read_sensor,
	.pre_display_config_changed = vega20_pre_display_config_changed,
	.display_config_changed = vega20_display_config_changed,
	.apply_clocks_adjust_rules = vega20_apply_clocks_adjust_rules,
	.notify_smc_display_config = vega20_notify_smc_display_config,
	.force_dpm_limit_value = vega20_force_dpm_limit_value,
	.unforce_dpm_levels = vega20_unforce_dpm_levels,
	.get_profiling_clk_mask = vega20_get_profiling_clk_mask,
	.is_dpm_running = vega20_is_dpm_running,
	.set_thermal_fan_table = vega20_set_thermal_fan_table,
	.get_fan_speed_percent = vega20_get_fan_speed_percent,
	.get_fan_speed_rpm = vega20_get_fan_speed_rpm,
	.set_watermarks_table = vega20_set_watermarks_table,
	.get_thermal_temperature_range = vega20_get_thermal_temperature_range,
	.set_df_cstate = vega20_set_df_cstate,
	.update_pcie_parameters = vega20_update_pcie_parameters,
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
};

void vega20_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &vega20_ppt_funcs;
}
