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
#include "power_state.h"
#include "vega20_ppt.h"
#include "vega20_pptable.h"
#include "vega20_ppsmc.h"
#include "nbio/nbio_7_4_sh_mask.h"

#define smnPCIE_LC_SPEED_CNTL			0x11140290
#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288

#define MSG_MAP(msg) \
	[SMU_MSG_##msg] = PPSMC_MSG_##msg

static int vega20_message_map[SMU_MSG_MAX_COUNT] = {
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
};

static int vega20_get_smu_msg_index(struct smu_context *smc, uint32_t index)
{
	int val;

	if (index >= SMU_MSG_MAX_COUNT)
		return -EINVAL;

	val = vega20_message_map[index];
	if (val > PPSMC_Message_Count)
		return -EINVAL;

	return val;
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

		if (table_context->od_feature_capabilities)
			return -EINVAL;

		table_context->od_feature_capabilities = kmemdup(&powerplay_table->OverDrive8Table.ODFeatureCapabilities,
								 od_feature_array_size,
								 GFP_KERNEL);
		if (!table_context->od_feature_capabilities)
			return -ENOMEM;

		/* Setup correct ODSettingCount, and store ODSettingArray from
		 * powerplay table to od_settings_max and od_setting_min */
		od_setting_count =
			(le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount) >
			 ATOM_VEGA20_ODSETTING_COUNT) ?
			ATOM_VEGA20_ODSETTING_COUNT :
			le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount);

		od_setting_array_size = sizeof(uint32_t) * od_setting_count;

		if (table_context->od_settings_max)
			return -EINVAL;

		table_context->od_settings_max = kmemdup(&powerplay_table->OverDrive8Table.ODSettingsMax,
							 od_setting_array_size,
							 GFP_KERNEL);

		if (!table_context->od_settings_max) {
			kfree(table_context->od_feature_capabilities);
			table_context->od_feature_capabilities = NULL;
			return -ENOMEM;
		}

		if (table_context->od_settings_min)
			return -EINVAL;

		table_context->od_settings_min = kmemdup(&powerplay_table->OverDrive8Table.ODSettingsMin,
							 od_setting_array_size,
							 GFP_KERNEL);

		if (!table_context->od_settings_min) {
			kfree(table_context->od_feature_capabilities);
			table_context->od_feature_capabilities = NULL;
			kfree(table_context->od_settings_max);
			table_context->od_settings_max = NULL;
			return -ENOMEM;
		}
	}

	return 0;
}

static int vega20_store_powerplay_table(struct smu_context *smu)
{
	ATOM_Vega20_POWERPLAYTABLE *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;
	int ret;

	if (!table_context->power_play_table)
		return -EINVAL;

	powerplay_table = table_context->power_play_table;

	memcpy(table_context->driver_pptable, &powerplay_table->smcPPTable,
	       sizeof(PPTable_t));

	table_context->software_shutdown_temp = powerplay_table->usSoftwareShutdownTemp;
	table_context->thermal_controller_type = powerplay_table->ucThermalControllerType;
	table_context->TDPODLimit = le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingsMax[ATOM_VEGA20_ODSETTING_POWERPERCENTAGE]);

	ret = vega20_setup_od8_information(smu);

	return ret;
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

static enum
amd_pm_state_type vega20_get_current_power_state(struct smu_context *smu)
{
	enum amd_pm_state_type pm_type;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!smu_dpm_ctx->dpm_context ||
	    !smu_dpm_ctx->dpm_current_power_state)
		return -EINVAL;

	mutex_lock(&(smu->mutex));
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
	mutex_unlock(&(smu->mutex));

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

	/* eclk */
	single_dpm_table = &(dpm_table->eclk_table);

	if (smu_feature_is_enabled(smu, FEATURE_DPM_VCE_BIT)) {
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

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UVD_BIT)) {
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

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UVD_BIT)) {
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
			enum pp_clock_type type, char *buf)
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
		(struct vega20_od8_settings *)table_context->od8_settings;
	OverDriveTable_t *od_table =
		(OverDriveTable_t *)(table_context->overdrive_table);
	PPTable_t *pptable = (PPTable_t *)table_context->driver_pptable;

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

	case PP_SOCCLK:
		ret = smu_get_current_clk_freq(smu, PPCLK_SOCCLK, &now);
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

	case PP_FCLK:
		ret = smu_get_current_clk_freq(smu, PPCLK_FCLK, &now);
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

	case PP_DCEFCLK:
		ret = smu_get_current_clk_freq(smu, PPCLK_DCEFCLK, &now);
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

	case PP_PCIE:
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

	case OD_SCLK:
		if (od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id &&
		    od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_SCLK");
			size += sprintf(buf + size, "0: %10uMhz\n",
					od_table->GfxclkFmin);
			size += sprintf(buf + size, "1: %10uMhz\n",
					od_table->GfxclkFmax);
		}

		break;

	case OD_MCLK:
		if (od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id) {
			size = sprintf(buf, "%s:\n", "OD_MCLK");
			size += sprintf(buf + size, "1: %10uMhz\n",
					 od_table->UclkFmax);
		}

		break;

	case OD_VDDC_CURVE:
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

	case OD_RANGE:
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

	if (smu_feature_is_enabled(smu, FEATURE_DPM_GFXCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_GFXCLK_MASK)) {
		single_dpm_table = &(dpm_table->gfx_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_GFXCLK << 16) | (freq & 0xffff));
		if (ret) {
			pr_err("Failed to set soft %s gfxclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_UCLK_MASK)) {
		single_dpm_table = &(dpm_table->mem_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_UCLK << 16) | (freq & 0xffff));
		if (ret) {
			pr_err("Failed to set soft %s memclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, FEATURE_DPM_SOCCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_SOCCLK_MASK)) {
		single_dpm_table = &(dpm_table->soc_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_SOCCLK << 16) | (freq & 0xffff));
		if (ret) {
			pr_err("Failed to set soft %s socclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, FEATURE_DPM_FCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_FCLK_MASK)) {
		single_dpm_table = &(dpm_table->fclk_table);
		freq = max ? single_dpm_table->dpm_state.soft_max_level :
			single_dpm_table->dpm_state.soft_min_level;
		ret = smu_send_smc_msg_with_param(smu,
			(max ? SMU_MSG_SetSoftMaxByFreq : SMU_MSG_SetSoftMinByFreq),
			(PPCLK_FCLK << 16) | (freq & 0xffff));
		if (ret) {
			pr_err("Failed to set soft %s fclk !\n",
						max ? "max" : "min");
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT) &&
	    (feature_mask & FEATURE_DPM_DCEFCLK_MASK)) {
		single_dpm_table = &(dpm_table->dcef_table);
		freq = single_dpm_table->dpm_state.hard_min_level;
		if (!max) {
			ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetHardMinByFreq,
				(PPCLK_DCEFCLK << 16) | (freq & 0xffff));
			if (ret) {
				pr_err("Failed to set hard min dcefclk !\n");
				return ret;
			}
		}
	}

	return ret;
}

static int vega20_force_clk_levels(struct smu_context *smu,
			enum pp_clock_type type, uint32_t mask)
{
	struct vega20_dpm_table *dpm_table;
	struct vega20_single_dpm_table *single_dpm_table;
	uint32_t soft_min_level, soft_max_level, hard_min_level;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	int ret = 0;

	if (smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL) {
		pr_info("force clock level is for dpm manual mode only.\n");
		return -EINVAL;
	}

	mutex_lock(&(smu->mutex));

	soft_min_level = mask ? (ffs(mask) - 1) : 0;
	soft_max_level = mask ? (fls(mask) - 1) : 0;

	dpm_table = smu->smu_dpm.dpm_context;

	switch (type) {
	case PP_SCLK:
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

	case PP_MCLK:
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

	case PP_SOCCLK:
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

	case PP_FCLK:
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

	case PP_DCEFCLK:
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

	case PP_PCIE:
		if (soft_min_level >= NUM_LINK_LEVELS ||
		    soft_max_level >= NUM_LINK_LEVELS) {
			ret = -EINVAL;
			break;
		}

		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetMinLinkDpmByIndex, soft_min_level);
		if (ret)
			pr_err("Failed to set min link dpm level!\n");

		break;

	default:
		break;
	}

	mutex_unlock(&(smu->mutex));
	return ret;
}

static int vega20_get_clock_by_type_with_latency(struct smu_context *smu,
						 enum amd_pp_clock_type type,
						 struct pp_clock_levels_with_latency *clocks)
{
	int ret;
	struct vega20_single_dpm_table *single_dpm_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;

	dpm_table = smu_dpm->dpm_context;

	mutex_lock(&smu->mutex);

	switch (type) {
	case amd_pp_sys_clock:
		single_dpm_table = &(dpm_table->gfx_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	case amd_pp_mem_clock:
		single_dpm_table = &(dpm_table->mem_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	case amd_pp_dcef_clock:
		single_dpm_table = &(dpm_table->dcef_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	case amd_pp_soc_clock:
		single_dpm_table = &(dpm_table->soc_table);
		ret = vega20_get_clk_table(smu, clocks, single_dpm_table);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&smu->mutex);
	return ret;
}

static int vega20_overdrive_get_gfx_clk_base_voltage(struct smu_context *smu,
						     uint32_t *voltage,
						     uint32_t freq)
{
	int ret;

	ret = smu_send_smc_msg_with_param(smu,
			SMU_MSG_GetAVFSVoltageByDpm,
			((AVFS_CURVE << 24) | (OD8_HOTCURVE_TEMPERATURE << 16) | freq));
	if (ret) {
		pr_err("[GetBaseVoltage] failed to get GFXCLK AVFS voltage from SMU!");
		return ret;
	}

	smu_read_smc_arg(smu, voltage);
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

	if (table_context->od8_settings)
		return -EINVAL;

	table_context->od8_settings = kzalloc(sizeof(struct vega20_od8_settings), GFP_KERNEL);

	if (!table_context->od8_settings)
		return -ENOMEM;

	memset(table_context->od8_settings, 0, sizeof(struct vega20_od8_settings));
	od8_settings = (struct vega20_od8_settings *)table_context->od8_settings;

	if (smu_feature_is_enabled(smu, FEATURE_DPM_SOCCLK_BIT)) {
		if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_GFXCLK_LIMITS] &&
		    table_context->od_settings_max[OD8_SETTING_GFXCLK_FMAX] > 0 &&
		    table_context->od_settings_min[OD8_SETTING_GFXCLK_FMIN] > 0 &&
		    (table_context->od_settings_max[OD8_SETTING_GFXCLK_FMAX] >=
		     table_context->od_settings_min[OD8_SETTING_GFXCLK_FMIN])) {
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].feature_id =
				OD8_GFXCLK_LIMITS;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].feature_id =
				OD8_GFXCLK_LIMITS;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMIN].default_value =
				od_table->GfxclkFmin;
			od8_settings->od8_settings_array[OD8_SETTING_GFXCLK_FMAX].default_value =
				od_table->GfxclkFmax;
		}

		if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_GFXCLK_CURVE] &&
		    (table_context->od_settings_min[OD8_SETTING_GFXCLK_VOLTAGE1] >=
		     smc_pptable->MinVoltageGfx / VOLTAGE_SCALE) &&
		    (table_context->od_settings_max[OD8_SETTING_GFXCLK_VOLTAGE3] <=
		     smc_pptable->MaxVoltageGfx / VOLTAGE_SCALE) &&
		    (table_context->od_settings_min[OD8_SETTING_GFXCLK_VOLTAGE1] <=
		     table_context->od_settings_max[OD8_SETTING_GFXCLK_VOLTAGE3])) {
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

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT)) {
		if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_UCLK_MAX] &&
		    table_context->od_settings_min[OD8_SETTING_UCLK_FMAX] > 0 &&
		    table_context->od_settings_max[OD8_SETTING_UCLK_FMAX] > 0 &&
		    (table_context->od_settings_max[OD8_SETTING_UCLK_FMAX] >=
		     table_context->od_settings_min[OD8_SETTING_UCLK_FMAX])) {
			od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].feature_id =
				OD8_UCLK_MAX;
			od8_settings->od8_settings_array[OD8_SETTING_UCLK_FMAX].default_value =
				od_table->UclkFmax;
		}
	}

	if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_POWER_LIMIT] &&
	    table_context->od_settings_min[OD8_SETTING_POWER_PERCENTAGE] > 0 &&
	    table_context->od_settings_min[OD8_SETTING_POWER_PERCENTAGE] <= 100 &&
	    table_context->od_settings_max[OD8_SETTING_POWER_PERCENTAGE] > 0 &&
	    table_context->od_settings_max[OD8_SETTING_POWER_PERCENTAGE] <= 100) {
		od8_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].feature_id =
			OD8_POWER_LIMIT;
		od8_settings->od8_settings_array[OD8_SETTING_POWER_PERCENTAGE].default_value =
			od_table->OverDrivePct;
	}

	if (smu_feature_is_enabled(smu, FEATURE_FAN_CONTROL_BIT)) {
		if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_FAN_ACOUSTIC_LIMIT] &&
		    table_context->od_settings_min[OD8_SETTING_FAN_ACOUSTIC_LIMIT] > 0 &&
		    table_context->od_settings_max[OD8_SETTING_FAN_ACOUSTIC_LIMIT] > 0 &&
		    (table_context->od_settings_max[OD8_SETTING_FAN_ACOUSTIC_LIMIT] >=
		     table_context->od_settings_min[OD8_SETTING_FAN_ACOUSTIC_LIMIT])) {
			od8_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].feature_id =
				OD8_ACOUSTIC_LIMIT_SCLK;
			od8_settings->od8_settings_array[OD8_SETTING_FAN_ACOUSTIC_LIMIT].default_value =
				od_table->FanMaximumRpm;
		}

		if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_FAN_SPEED_MIN] &&
		    table_context->od_settings_min[OD8_SETTING_FAN_MIN_SPEED] > 0 &&
		    table_context->od_settings_max[OD8_SETTING_FAN_MIN_SPEED] > 0 &&
		    (table_context->od_settings_max[OD8_SETTING_FAN_MIN_SPEED] >=
		     table_context->od_settings_min[OD8_SETTING_FAN_MIN_SPEED])) {
			od8_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].feature_id =
				OD8_FAN_SPEED_MIN;
			od8_settings->od8_settings_array[OD8_SETTING_FAN_MIN_SPEED].default_value =
				od_table->FanMinimumPwm * smc_pptable->FanMaximumRpm / 100;
		}
	}

	if (smu_feature_is_enabled(smu, FEATURE_THERMAL_BIT)) {
		if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_TEMPERATURE_FAN] &&
		    table_context->od_settings_min[OD8_SETTING_FAN_TARGET_TEMP] > 0 &&
		    table_context->od_settings_max[OD8_SETTING_FAN_TARGET_TEMP] > 0 &&
		    (table_context->od_settings_max[OD8_SETTING_FAN_TARGET_TEMP] >=
		     table_context->od_settings_min[OD8_SETTING_FAN_TARGET_TEMP])) {
			od8_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].feature_id =
				OD8_TEMPERATURE_FAN;
			od8_settings->od8_settings_array[OD8_SETTING_FAN_TARGET_TEMP].default_value =
				od_table->FanTargetTemperature;
		}

		if (table_context->od_feature_capabilities[ATOM_VEGA20_ODFEATURE_TEMPERATURE_SYSTEM] &&
		    table_context->od_settings_min[OD8_SETTING_OPERATING_TEMP_MAX] > 0 &&
		    table_context->od_settings_max[OD8_SETTING_OPERATING_TEMP_MAX] > 0 &&
		    (table_context->od_settings_max[OD8_SETTING_OPERATING_TEMP_MAX] >=
		     table_context->od_settings_min[OD8_SETTING_OPERATING_TEMP_MAX])) {
			od8_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].feature_id =
				OD8_TEMPERATURE_SYSTEM;
			od8_settings->od8_settings_array[OD8_SETTING_OPERATING_TEMP_MAX].default_value =
				od_table->MaxOpTemp;
		}
	}

	for (i = 0; i < OD8_SETTING_COUNT; i++) {
		if (od8_settings->od8_settings_array[i].feature_id) {
			od8_settings->od8_settings_array[i].min_value =
				table_context->od_settings_min[i];
			od8_settings->od8_settings_array[i].max_value =
				table_context->od_settings_max[i];
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

static int vega20_get_od_percentage(struct smu_context *smu,
				    enum pp_clock_type type)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct vega20_dpm_table *dpm_table = NULL;
	struct vega20_dpm_table *golden_table = NULL;
	struct vega20_single_dpm_table *single_dpm_table;
	struct vega20_single_dpm_table *golden_dpm_table;
	int value, golden_value;

	dpm_table = smu_dpm->dpm_context;
	golden_table = smu_dpm->golden_dpm_context;

	switch (type) {
	case OD_SCLK:
		single_dpm_table = &(dpm_table->gfx_table);
		golden_dpm_table = &(golden_table->gfx_table);
		break;
	case OD_MCLK:
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

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT)) {
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
				(PPCLK_UCLK << 16) | dpm_table->dpm_state.hard_min_level);
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

	smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, 0);
	ret = vega20_set_uclk_to_highest_dpm_level(smu,
						   &dpm_table->mem_table);
	if (ret)
		pr_err("Failed to set uclk to highest dpm level");
	return ret;
}

static int vega20_display_config_changed(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->funcs)
		return -EINVAL;

	if (!smu->smu_dpm.dpm_context ||
	    !smu->smu_table.tables ||
	    !smu->smu_table.tables[TABLE_WATERMARKS].cpu_addr)
		return -EINVAL;

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    !(smu->watermarks_bitmap & WATERMARKS_LOADED)) {
		ret = smu->funcs->write_watermarks_table(smu);
		if (ret) {
			pr_err("Failed to update WMTABLE!");
			return ret;
		}
		smu->watermarks_bitmap |= WATERMARKS_LOADED;
	}

	if ((smu->watermarks_bitmap & WATERMARKS_EXIST) &&
	    smu_feature_is_supported(smu, FEATURE_DPM_DCEFCLK_BIT) &&
	    smu_feature_is_supported(smu, FEATURE_DPM_SOCCLK_BIT)) {
		smu_send_smc_msg_with_param(smu,
					    SMU_MSG_NumOfDisplays,
					    smu->display_config->num_display);
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
vega20_notify_smc_dispaly_config(struct smu_context *smu)
{
	struct vega20_dpm_table *dpm_table = smu->smu_dpm.dpm_context;
	struct vega20_single_dpm_table *memtable = &dpm_table->mem_table;
	struct smu_clocks min_clocks = {0};
	struct pp_display_clock_request clock_req;
	int ret = 0;

	min_clocks.dcef_clock = smu->display_config->min_dcef_set_clk;
	min_clocks.dcef_clock_in_sr = smu->display_config->min_dcef_deep_sleep_set_clk;
	min_clocks.memory_clock = smu->display_config->min_mem_set_clock;

	if (smu_feature_is_supported(smu, FEATURE_DPM_DCEFCLK_BIT)) {
		clock_req.clock_type = amd_pp_dcef_clock;
		clock_req.clock_freq_in_khz = min_clocks.dcef_clock * 10;
		if (!smu->funcs->display_clock_voltage_request(smu, &clock_req)) {
			if (smu_feature_is_supported(smu, FEATURE_DS_DCEFCLK_BIT)) {
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

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT)) {
		memtable->dpm_state.hard_min_level = min_clocks.memory_clock/100;
		ret = smu_send_smc_msg_with_param(smu,
						  SMU_MSG_SetHardMinByFreq,
						  (PPCLK_UCLK << 16) | memtable->dpm_state.hard_min_level);
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

	ret = smu_upload_dpm_level(smu, false, 0xFFFFFFFF);
	if (ret) {
		pr_err("Failed to upload DPM Bootup Levels!");
		return ret;
	}

	ret = smu_upload_dpm_level(smu, true, 0xFFFFFFFF);
	if (ret) {
		pr_err("Failed to upload DPM Max Levels!");
		return ret;
	}

	return ret;
}

static enum amd_dpm_forced_level vega20_get_performance_level(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);
	if (!smu_dpm_ctx->dpm_context)
		return -EINVAL;

	if (smu_dpm_ctx->dpm_level != smu_dpm_ctx->saved_dpm_level) {
		mutex_lock(&(smu->mutex));
		smu_dpm_ctx->saved_dpm_level = smu_dpm_ctx->dpm_level;
		mutex_unlock(&(smu->mutex));
	}
	return smu_dpm_ctx->dpm_level;
}

static int
vega20_force_performance_level(struct smu_context *smu, enum amd_dpm_forced_level level)
{
	int ret = 0;
	int i;
	struct smu_dpm_context *smu_dpm_ctx = &(smu->smu_dpm);

	if (!smu_dpm_ctx->dpm_context)
		return -EINVAL;

	for (i = 0; i < smu->adev->num_ip_blocks; i++) {
		if (smu->adev->ip_blocks[i].version->type == AMD_IP_BLOCK_TYPE_SMC)
			break;
	}

	mutex_lock(&smu->mutex);

	smu->adev->ip_blocks[i].version->funcs->enable_umd_pstate(smu, &level);
	ret = smu_handle_task(smu, level,
			      AMD_PP_TASK_READJUST_POWER_STATE);

	mutex_unlock(&smu->mutex);

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
		(struct vega20_od8_settings *)table_context->od8_settings;

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

static int vega20_set_od_percentage(struct smu_context *smu,
				    enum pp_clock_type type,
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

	mutex_lock(&(smu->mutex));

	dpm_table = smu_dpm->dpm_context;
	golden_table = smu_dpm->golden_dpm_context;

	switch (type) {
	case OD_SCLK:
		single_dpm_table = &(dpm_table->gfx_table);
		golden_dpm_table = &(golden_table->gfx_table);
		feature_enabled = smu_feature_is_enabled(smu, FEATURE_DPM_GFXCLK_BIT);
		clk_id = PPCLK_GFXCLK;
		index = OD8_SETTING_GFXCLK_FMAX;
		break;
	case OD_MCLK:
		single_dpm_table = &(dpm_table->mem_table);
		golden_dpm_table = &(golden_table->mem_table);
		feature_enabled = smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT);
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

	ret = smu_update_od8_settings(smu, index, od_clk);
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
			      AMD_PP_TASK_READJUST_POWER_STATE);

set_od_failed:
	mutex_unlock(&(smu->mutex));

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
		(struct vega20_od8_settings *)table_context->od8_settings;
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
				table_context->od_gfxclk_update = true;
			} else if (input_index == 1 && od_table->GfxclkFmax != input_clk) {
				od_table->GfxclkFmax = input_clk;
				table_context->od_gfxclk_update = true;
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
				table_context->od_gfxclk_update = true;
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
		ret = smu_update_table(smu, TABLE_OVERDRIVE, table_context->overdrive_table, false);
		if (ret) {
			pr_err("Failed to export over drive table!\n");
			return ret;
		}

		break;

	case PP_OD_COMMIT_DPM_TABLE:
		ret = smu_update_table(smu, TABLE_OVERDRIVE, table_context->overdrive_table, true);
		if (ret) {
			pr_err("Failed to import over drive table!\n");
			return ret;
		}

		/* retrieve updated gfxclk table */
		if (table_context->od_gfxclk_update) {
			table_context->od_gfxclk_update = false;
			single_dpm_table = &(dpm_table->gfx_table);

			if (smu_feature_is_enabled(smu, FEATURE_DPM_GFXCLK_BIT)) {
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
		mutex_lock(&(smu->mutex));
		ret = smu_handle_task(smu, smu_dpm->dpm_level,
				      AMD_PP_TASK_READJUST_POWER_STATE);
		mutex_unlock(&(smu->mutex));
	}

	return ret;
}

static int vega20_get_enabled_smc_features(struct smu_context *smu,
		uint64_t *features_enabled)
{
	uint32_t feature_mask[2] = {0, 0};
	int ret = 0;

	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	if (ret)
		return ret;

	*features_enabled = ((((uint64_t)feature_mask[0] << SMU_FEATURES_LOW_SHIFT) & SMU_FEATURES_LOW_MASK) |
			(((uint64_t)feature_mask[1] << SMU_FEATURES_HIGH_SHIFT) & SMU_FEATURES_HIGH_MASK));

	return ret;
}

static int vega20_enable_smc_features(struct smu_context *smu,
		bool enable, uint64_t feature_mask)
{
	uint32_t smu_features_low, smu_features_high;
	int ret = 0;

	smu_features_low = (uint32_t)((feature_mask & SMU_FEATURES_LOW_MASK) >> SMU_FEATURES_LOW_SHIFT);
	smu_features_high = (uint32_t)((feature_mask & SMU_FEATURES_HIGH_MASK) >> SMU_FEATURES_HIGH_SHIFT);

	if (enable) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesLow,
						  smu_features_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesHigh,
						  smu_features_high);
		if (ret)
			return ret;
	} else {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesLow,
						  smu_features_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesHigh,
						  smu_features_high);
		if (ret)
			return ret;
	}

	return 0;

}

static int vega20_get_ppfeature_status(struct smu_context *smu, char *buf)
{
	static const char *ppfeature_name[] = {
				"DPM_PREFETCHER",
				"GFXCLK_DPM",
				"UCLK_DPM",
				"SOCCLK_DPM",
				"UVD_DPM",
				"VCE_DPM",
				"ULV",
				"MP0CLK_DPM",
				"LINK_DPM",
				"DCEFCLK_DPM",
				"GFXCLK_DS",
				"SOCCLK_DS",
				"LCLK_DS",
				"PPT",
				"TDC",
				"THERMAL",
				"GFX_PER_CU_CG",
				"RM",
				"DCEFCLK_DS",
				"ACDC",
				"VR0HOT",
				"VR1HOT",
				"FW_CTF",
				"LED_DISPLAY",
				"FAN_CONTROL",
				"GFX_EDC",
				"GFXOFF",
				"CG",
				"FCLK_DPM",
				"FCLK_DS",
				"MP1CLK_DS",
				"MP0CLK_DS",
				"XGMI",
				"ECC"};
	static const char *output_title[] = {
				"FEATURES",
				"BITMASK",
				"ENABLEMENT"};
	uint64_t features_enabled;
	int i;
	int ret = 0;
	int size = 0;

	ret = vega20_get_enabled_smc_features(smu, &features_enabled);
	if (ret)
		return ret;

	size += sprintf(buf + size, "Current ppfeatures: 0x%016llx\n", features_enabled);
	size += sprintf(buf + size, "%-19s %-22s %s\n",
				output_title[0],
				output_title[1],
				output_title[2]);
	for (i = 0; i < GNLD_FEATURES_MAX; i++) {
		size += sprintf(buf + size, "%-19s 0x%016llx %6s\n",
					ppfeature_name[i],
					1ULL << i,
					(features_enabled & (1ULL << i)) ? "Y" : "N");
	}

	return size;
}

static int vega20_set_ppfeature_status(struct smu_context *smu, uint64_t new_ppfeature_masks)
{
	uint64_t features_enabled;
	uint64_t features_to_enable;
	uint64_t features_to_disable;
	int ret = 0;

	if (new_ppfeature_masks >= (1ULL << GNLD_FEATURES_MAX))
		return -EINVAL;

	ret = vega20_get_enabled_smc_features(smu, &features_enabled);
	if (ret)
		return ret;

	features_to_disable =
		features_enabled & ~new_ppfeature_masks;
	features_to_enable =
		~features_enabled & new_ppfeature_masks;

	pr_debug("features_to_disable 0x%llx\n", features_to_disable);
	pr_debug("features_to_enable 0x%llx\n", features_to_enable);

	if (features_to_disable) {
		ret = vega20_enable_smc_features(smu, false, features_to_disable);
		if (ret)
			return ret;
	}

	if (features_to_enable) {
		ret = vega20_enable_smc_features(smu, true, features_to_enable);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pptable_funcs vega20_ppt_funcs = {
	.alloc_dpm_context = vega20_allocate_dpm_context,
	.store_powerplay_table = vega20_store_powerplay_table,
	.check_powerplay_table = vega20_check_powerplay_table,
	.append_powerplay_table = vega20_append_powerplay_table,
	.get_smu_msg_index = vega20_get_smu_msg_index,
	.run_afll_btc = vega20_run_btc_afll,
	.get_unallowed_feature_mask = vega20_get_unallowed_feature_mask,
	.get_current_power_state = vega20_get_current_power_state,
	.set_default_dpm_table = vega20_set_default_dpm_table,
	.set_power_state = NULL,
	.populate_umd_state_clk = vega20_populate_umd_state_clk,
	.print_clk_levels = vega20_print_clk_levels,
	.force_clk_levels = vega20_force_clk_levels,
	.get_clock_by_type_with_latency = vega20_get_clock_by_type_with_latency,
	.set_default_od8_settings = vega20_set_default_od8_setttings,
	.get_od_percentage = vega20_get_od_percentage,
	.get_performance_level = vega20_get_performance_level,
	.force_performance_level = vega20_force_performance_level,
	.update_specified_od8_value = vega20_update_specified_od8_value,
	.set_od_percentage = vega20_set_od_percentage,
	.od_edit_dpm_table = vega20_odn_edit_dpm_table,
	.pre_display_config_changed = vega20_pre_display_config_changed,
	.display_config_changed = vega20_display_config_changed,
	.apply_clocks_adjust_rules = vega20_apply_clocks_adjust_rules,
	.notify_smc_dispaly_config = vega20_notify_smc_dispaly_config,
	.force_dpm_limit_value = vega20_force_dpm_limit_value,
	.unforce_dpm_levels = vega20_unforce_dpm_levels,
	.upload_dpm_level = vega20_upload_dpm_level,
	.get_profiling_clk_mask = vega20_get_profiling_clk_mask,
	.set_ppfeature_status = vega20_set_ppfeature_status,
	.get_ppfeature_status = vega20_get_ppfeature_status,
};

void vega20_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &vega20_ppt_funcs;
	smu->smc_if_version = SMU11_DRIVER_IF_VERSION;
}
