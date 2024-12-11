/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#include <linux/module.h>
#include <linux/slab.h>

#include "smu11_driver_if.h"
#include "vega20_processpptables.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "pp_debug.h"
#include "cgs_common.h"
#include "vega20_pptable.h"

#define VEGA20_FAN_TARGET_TEMPERATURE_OVERRIDE 105

static void set_hw_cap(struct pp_hwmgr *hwmgr, bool enable,
		enum phm_platform_caps cap)
{
	if (enable)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, cap);
	else
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps, cap);
}

static const void *get_powerplay_table(struct pp_hwmgr *hwmgr)
{
	int index = GetIndexIntoMasterDataTable(powerplayinfo);

	u16 size;
	u8 frev, crev;
	const void *table_address = hwmgr->soft_pp_table;

	if (!table_address) {
		table_address = (ATOM_Vega20_POWERPLAYTABLE *)
				smu_atom_get_data_table(hwmgr->adev, index,
						&size, &frev, &crev);

		hwmgr->soft_pp_table = table_address;
		hwmgr->soft_pp_table_size = size;
	}

	return table_address;
}

static int check_powerplay_tables(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega20_POWERPLAYTABLE *powerplay_table)
{
	PP_ASSERT_WITH_CODE((powerplay_table->sHeader.format_revision >=
		ATOM_VEGA20_TABLE_REVISION_VEGA20),
		"Unsupported PPTable format!", return -1);
	PP_ASSERT_WITH_CODE(powerplay_table->sHeader.structuresize > 0,
		"Invalid PowerPlay Table!", return -1);

	if (powerplay_table->smcPPTable.Version != PPTABLE_V20_SMU_VERSION) {
		pr_info("Unmatch PPTable version: "
			"pptable from VBIOS is V%d while driver supported is V%d!",
			powerplay_table->smcPPTable.Version,
			PPTABLE_V20_SMU_VERSION);
		return -EINVAL;
	}

	return 0;
}

static int set_platform_caps(struct pp_hwmgr *hwmgr, uint32_t powerplay_caps)
{
	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_POWERPLAY),
		PHM_PlatformCaps_PowerPlaySupport);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_SBIOSPOWERSOURCE),
		PHM_PlatformCaps_BiosPowerSourceControl);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_BACO),
		PHM_PlatformCaps_BACO);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_VEGA20_PP_PLATFORM_CAP_BAMACO),
		 PHM_PlatformCaps_BAMACO);

	return 0;
}

static int copy_overdrive_feature_capabilities_array(
		struct pp_hwmgr *hwmgr,
		uint8_t **pptable_info_array,
		const uint8_t *pptable_array,
		uint8_t od_feature_count)
{
	uint32_t array_size, i;
	uint8_t *table;
	bool od_supported = false;

	array_size = sizeof(uint8_t) * od_feature_count;
	table = kzalloc(array_size, GFP_KERNEL);
	if (NULL == table)
		return -ENOMEM;

	for (i = 0; i < od_feature_count; i++) {
		table[i] = le32_to_cpu(pptable_array[i]);
		if (table[i])
			od_supported = true;
	}

	*pptable_info_array = table;

	if (od_supported)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ACOverdriveSupport);

	return 0;
}

static int append_vbios_pptable(struct pp_hwmgr *hwmgr, PPTable_t *ppsmc_pptable)
{
	struct atom_smc_dpm_info_v4_4 *smc_dpm_table;
	int index = GetIndexIntoMasterDataTable(smc_dpm_info);
	int i;

	PP_ASSERT_WITH_CODE(
		smc_dpm_table = smu_atom_get_data_table(hwmgr->adev, index, NULL, NULL, NULL),
		"[appendVbiosPPTable] Failed to retrieve Smc Dpm Table from VBIOS!",
		return -1);

	ppsmc_pptable->MaxVoltageStepGfx = smc_dpm_table->maxvoltagestepgfx;
	ppsmc_pptable->MaxVoltageStepSoc = smc_dpm_table->maxvoltagestepsoc;

	ppsmc_pptable->VddGfxVrMapping = smc_dpm_table->vddgfxvrmapping;
	ppsmc_pptable->VddSocVrMapping = smc_dpm_table->vddsocvrmapping;
	ppsmc_pptable->VddMem0VrMapping = smc_dpm_table->vddmem0vrmapping;
	ppsmc_pptable->VddMem1VrMapping = smc_dpm_table->vddmem1vrmapping;

	ppsmc_pptable->GfxUlvPhaseSheddingMask = smc_dpm_table->gfxulvphasesheddingmask;
	ppsmc_pptable->SocUlvPhaseSheddingMask = smc_dpm_table->soculvphasesheddingmask;
	ppsmc_pptable->ExternalSensorPresent = smc_dpm_table->externalsensorpresent;

	ppsmc_pptable->GfxMaxCurrent = smc_dpm_table->gfxmaxcurrent;
	ppsmc_pptable->GfxOffset = smc_dpm_table->gfxoffset;
	ppsmc_pptable->Padding_TelemetryGfx = smc_dpm_table->padding_telemetrygfx;

	ppsmc_pptable->SocMaxCurrent = smc_dpm_table->socmaxcurrent;
	ppsmc_pptable->SocOffset = smc_dpm_table->socoffset;
	ppsmc_pptable->Padding_TelemetrySoc = smc_dpm_table->padding_telemetrysoc;

	ppsmc_pptable->Mem0MaxCurrent = smc_dpm_table->mem0maxcurrent;
	ppsmc_pptable->Mem0Offset = smc_dpm_table->mem0offset;
	ppsmc_pptable->Padding_TelemetryMem0 = smc_dpm_table->padding_telemetrymem0;

	ppsmc_pptable->Mem1MaxCurrent = smc_dpm_table->mem1maxcurrent;
	ppsmc_pptable->Mem1Offset = smc_dpm_table->mem1offset;
	ppsmc_pptable->Padding_TelemetryMem1 = smc_dpm_table->padding_telemetrymem1;

	ppsmc_pptable->AcDcGpio = smc_dpm_table->acdcgpio;
	ppsmc_pptable->AcDcPolarity = smc_dpm_table->acdcpolarity;
	ppsmc_pptable->VR0HotGpio = smc_dpm_table->vr0hotgpio;
	ppsmc_pptable->VR0HotPolarity = smc_dpm_table->vr0hotpolarity;

	ppsmc_pptable->VR1HotGpio = smc_dpm_table->vr1hotgpio;
	ppsmc_pptable->VR1HotPolarity = smc_dpm_table->vr1hotpolarity;
	ppsmc_pptable->Padding1 = smc_dpm_table->padding1;
	ppsmc_pptable->Padding2 = smc_dpm_table->padding2;

	ppsmc_pptable->LedPin0 = smc_dpm_table->ledpin0;
	ppsmc_pptable->LedPin1 = smc_dpm_table->ledpin1;
	ppsmc_pptable->LedPin2 = smc_dpm_table->ledpin2;

	ppsmc_pptable->PllGfxclkSpreadEnabled = smc_dpm_table->pllgfxclkspreadenabled;
	ppsmc_pptable->PllGfxclkSpreadPercent = smc_dpm_table->pllgfxclkspreadpercent;
	ppsmc_pptable->PllGfxclkSpreadFreq = smc_dpm_table->pllgfxclkspreadfreq;

	ppsmc_pptable->UclkSpreadEnabled = 0;
	ppsmc_pptable->UclkSpreadPercent = smc_dpm_table->uclkspreadpercent;
	ppsmc_pptable->UclkSpreadFreq = smc_dpm_table->uclkspreadfreq;

	ppsmc_pptable->FclkSpreadEnabled = smc_dpm_table->fclkspreadenabled;
	ppsmc_pptable->FclkSpreadPercent = smc_dpm_table->fclkspreadpercent;
	ppsmc_pptable->FclkSpreadFreq = smc_dpm_table->fclkspreadfreq;

	ppsmc_pptable->FllGfxclkSpreadEnabled = smc_dpm_table->fllgfxclkspreadenabled;
	ppsmc_pptable->FllGfxclkSpreadPercent = smc_dpm_table->fllgfxclkspreadpercent;
	ppsmc_pptable->FllGfxclkSpreadFreq = smc_dpm_table->fllgfxclkspreadfreq;

	for (i = 0; i < I2C_CONTROLLER_NAME_COUNT; i++) {
		ppsmc_pptable->I2cControllers[i].Enabled =
			smc_dpm_table->i2ccontrollers[i].enabled;
		ppsmc_pptable->I2cControllers[i].SlaveAddress =
			smc_dpm_table->i2ccontrollers[i].slaveaddress;
		ppsmc_pptable->I2cControllers[i].ControllerPort =
			smc_dpm_table->i2ccontrollers[i].controllerport;
		ppsmc_pptable->I2cControllers[i].ThermalThrottler =
			smc_dpm_table->i2ccontrollers[i].thermalthrottler;
		ppsmc_pptable->I2cControllers[i].I2cProtocol =
			smc_dpm_table->i2ccontrollers[i].i2cprotocol;
		ppsmc_pptable->I2cControllers[i].I2cSpeed =
			smc_dpm_table->i2ccontrollers[i].i2cspeed;
	}

	return 0;
}

static int override_powerplay_table_fantargettemperature(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;
	PPTable_t *ppsmc_pptable = (PPTable_t *)(pptable_information->smc_pptable);

	ppsmc_pptable->FanTargetTemperature = VEGA20_FAN_TARGET_TEMPERATURE_OVERRIDE;

	return 0;
}

#define VEGA20_ENGINECLOCK_HARDMAX 198000
static int init_powerplay_table_information(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega20_POWERPLAYTABLE *powerplay_table)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;
	uint32_t disable_power_control = 0;
	uint32_t od_feature_count, od_setting_count, power_saving_clock_count;
	int result;

	hwmgr->thermal_controller.ucType = powerplay_table->ucThermalControllerType;
	pptable_information->uc_thermal_controller_type = powerplay_table->ucThermalControllerType;
	hwmgr->thermal_controller.fanInfo.ulMinRPM = 0;
	hwmgr->thermal_controller.fanInfo.ulMaxRPM = powerplay_table->smcPPTable.FanMaximumRpm;

	set_hw_cap(hwmgr,
		ATOM_VEGA20_PP_THERMALCONTROLLER_NONE != hwmgr->thermal_controller.ucType,
		PHM_PlatformCaps_ThermalController);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_MicrocodeFanControl);

	if (powerplay_table->OverDrive8Table.ucODTableRevision == 1) {
		od_feature_count =
			(le32_to_cpu(powerplay_table->OverDrive8Table.ODFeatureCount) >
			 ATOM_VEGA20_ODFEATURE_COUNT) ?
			ATOM_VEGA20_ODFEATURE_COUNT :
			le32_to_cpu(powerplay_table->OverDrive8Table.ODFeatureCount);
		od_setting_count =
			(le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount) >
			 ATOM_VEGA20_ODSETTING_COUNT) ?
			ATOM_VEGA20_ODSETTING_COUNT :
			le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingCount);

		copy_overdrive_feature_capabilities_array(hwmgr,
				&pptable_information->od_feature_capabilities,
				powerplay_table->OverDrive8Table.ODFeatureCapabilities,
				od_feature_count);
		phm_copy_overdrive_settings_limits_array(hwmgr,
				&pptable_information->od_settings_max,
				powerplay_table->OverDrive8Table.ODSettingsMax,
				od_setting_count);
		phm_copy_overdrive_settings_limits_array(hwmgr,
				&pptable_information->od_settings_min,
				powerplay_table->OverDrive8Table.ODSettingsMin,
				od_setting_count);
	}

	pptable_information->us_small_power_limit1 = le16_to_cpu(powerplay_table->usSmallPowerLimit1);
	pptable_information->us_small_power_limit2 = le16_to_cpu(powerplay_table->usSmallPowerLimit2);
	pptable_information->us_boost_power_limit = le16_to_cpu(powerplay_table->usBoostPowerLimit);
	pptable_information->us_od_turbo_power_limit = le16_to_cpu(powerplay_table->usODTurboPowerLimit);
	pptable_information->us_od_powersave_power_limit = le16_to_cpu(powerplay_table->usODPowerSavePowerLimit);

	pptable_information->us_software_shutdown_temp = le16_to_cpu(powerplay_table->usSoftwareShutdownTemp);

	hwmgr->platform_descriptor.TDPODLimit = le32_to_cpu(powerplay_table->OverDrive8Table.ODSettingsMax[ATOM_VEGA20_ODSETTING_POWERPERCENTAGE]);

	disable_power_control = 0;
	if (!disable_power_control && hwmgr->platform_descriptor.TDPODLimit)
		/* enable TDP overdrive (PowerControl) feature as well if supported */
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PowerControl);

	if (powerplay_table->PowerSavingClockTable.ucTableRevision == 1) {
		power_saving_clock_count =
			(le32_to_cpu(powerplay_table->PowerSavingClockTable.PowerSavingClockCount) >=
			 ATOM_VEGA20_PPCLOCK_COUNT) ?
			ATOM_VEGA20_PPCLOCK_COUNT :
			le32_to_cpu(powerplay_table->PowerSavingClockTable.PowerSavingClockCount);
		phm_copy_clock_limits_array(hwmgr,
				&pptable_information->power_saving_clock_max,
				powerplay_table->PowerSavingClockTable.PowerSavingClockMax,
				power_saving_clock_count);
		phm_copy_clock_limits_array(hwmgr,
				&pptable_information->power_saving_clock_min,
				powerplay_table->PowerSavingClockTable.PowerSavingClockMin,
				power_saving_clock_count);
	}

	pptable_information->smc_pptable = kmemdup(&(powerplay_table->smcPPTable),
						   sizeof(PPTable_t),
						   GFP_KERNEL);
	if (pptable_information->smc_pptable == NULL)
		return -ENOMEM;


	result = append_vbios_pptable(hwmgr, (pptable_information->smc_pptable));
	if (result)
		return result;

	result = override_powerplay_table_fantargettemperature(hwmgr);

	return result;
}

static int vega20_pp_tables_initialize(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	const ATOM_Vega20_POWERPLAYTABLE *powerplay_table;

	hwmgr->pptable = kzalloc(sizeof(struct phm_ppt_v3_information), GFP_KERNEL);
	PP_ASSERT_WITH_CODE((hwmgr->pptable != NULL),
		"Failed to allocate hwmgr->pptable!", return -ENOMEM);

	powerplay_table = get_powerplay_table(hwmgr);
	PP_ASSERT_WITH_CODE((powerplay_table != NULL),
		"Missing PowerPlay Table!", return -1);

	result = check_powerplay_tables(hwmgr, powerplay_table);
	PP_ASSERT_WITH_CODE((result == 0),
		"check_powerplay_tables failed", return result);

	result = set_platform_caps(hwmgr,
			le32_to_cpu(powerplay_table->ulPlatformCaps));
	PP_ASSERT_WITH_CODE((result == 0),
		"set_platform_caps failed", return result);

	result = init_powerplay_table_information(hwmgr, powerplay_table);
	PP_ASSERT_WITH_CODE((result == 0),
		"init_powerplay_table_information failed", return result);

	return result;
}

static int vega20_pp_tables_uninitialize(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v3_information *pp_table_info =
			(struct phm_ppt_v3_information *)(hwmgr->pptable);

	kfree(pp_table_info->power_saving_clock_max);
	pp_table_info->power_saving_clock_max = NULL;

	kfree(pp_table_info->power_saving_clock_min);
	pp_table_info->power_saving_clock_min = NULL;

	kfree(pp_table_info->od_feature_capabilities);
	pp_table_info->od_feature_capabilities = NULL;

	kfree(pp_table_info->od_settings_max);
	pp_table_info->od_settings_max = NULL;

	kfree(pp_table_info->od_settings_min);
	pp_table_info->od_settings_min = NULL;

	kfree(pp_table_info->smc_pptable);
	pp_table_info->smc_pptable = NULL;

	kfree(hwmgr->pptable);
	hwmgr->pptable = NULL;

	return 0;
}

const struct pp_table_func vega20_pptable_funcs = {
	.pptable_init = vega20_pp_tables_initialize,
	.pptable_fini = vega20_pp_tables_uninitialize,
};
