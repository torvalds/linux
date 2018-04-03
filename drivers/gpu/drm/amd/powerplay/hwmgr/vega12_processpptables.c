/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include <linux/fb.h>

#include "vega12/smu9_driver_if.h"
#include "vega12_processpptables.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "pp_debug.h"
#include "cgs_common.h"
#include "vega12_pptable.h"

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
		table_address = (ATOM_Vega12_POWERPLAYTABLE *)
				cgs_atom_get_data_table(hwmgr->device, index,
						&size, &frev, &crev);

		hwmgr->soft_pp_table = table_address;	/*Cache the result in RAM.*/
		hwmgr->soft_pp_table_size = size;
	}

	return table_address;
}

static int check_powerplay_tables(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega12_POWERPLAYTABLE *powerplay_table)
{
	PP_ASSERT_WITH_CODE((powerplay_table->sHeader.format_revision >=
		ATOM_VEGA12_TABLE_REVISION_VEGA12),
		"Unsupported PPTable format!", return -1);
	PP_ASSERT_WITH_CODE(powerplay_table->sHeader.structuresize > 0,
		"Invalid PowerPlay Table!", return -1);

	return 0;
}

static int set_platform_caps(struct pp_hwmgr *hwmgr, uint32_t powerplay_caps)
{
	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA12_PP_PLATFORM_CAP_POWERPLAY),
			PHM_PlatformCaps_PowerPlaySupport);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA12_PP_PLATFORM_CAP_SBIOSPOWERSOURCE),
			PHM_PlatformCaps_BiosPowerSourceControl);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA12_PP_PLATFORM_CAP_BACO),
			PHM_PlatformCaps_BACO);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA12_PP_PLATFORM_CAP_BAMACO),
			 PHM_PlatformCaps_BAMACO);

	return 0;
}

static int copy_clock_limits_array(
	struct pp_hwmgr *hwmgr,
	uint32_t **pptable_info_array,
	const uint32_t *pptable_array)
{
	uint32_t array_size, i;
	uint32_t *table;

	array_size = sizeof(uint32_t) * ATOM_VEGA12_PPCLOCK_COUNT;

	table = kzalloc(array_size, GFP_KERNEL);
	if (NULL == table)
		return -ENOMEM;

	for (i = 0; i < ATOM_VEGA12_PPCLOCK_COUNT; i++)
		table[i] = pptable_array[i];

	*pptable_info_array = table;

	return 0;
}

static int copy_overdrive_settings_limits_array(
		struct pp_hwmgr *hwmgr,
		uint32_t **pptable_info_array,
		const uint32_t *pptable_array)
{
	uint32_t array_size, i;
	uint32_t *table;

	array_size = sizeof(uint32_t) * ATOM_VEGA12_ODSETTING_COUNT;

	table = kzalloc(array_size, GFP_KERNEL);
	if (NULL == table)
		return -ENOMEM;

	for (i = 0; i < ATOM_VEGA12_ODSETTING_COUNT; i++)
		table[i] = pptable_array[i];

	*pptable_info_array = table;

	return 0;
}

static int append_vbios_pptable(struct pp_hwmgr *hwmgr, PPTable_t *ppsmc_pptable)
{
	struct pp_atomfwctrl_smc_dpm_parameters smc_dpm_table;

	PP_ASSERT_WITH_CODE(
		pp_atomfwctrl_get_smc_dpm_information(hwmgr, &smc_dpm_table) == 0,
		"[appendVbiosPPTable] Failed to retrieve Smc Dpm Table from VBIOS!",
		return -1);

	ppsmc_pptable->Liquid1_I2C_address = smc_dpm_table.liquid1_i2c_address;
	ppsmc_pptable->Liquid2_I2C_address = smc_dpm_table.liquid2_i2c_address;
	ppsmc_pptable->Vr_I2C_address = smc_dpm_table.vr_i2c_address;
	ppsmc_pptable->Plx_I2C_address = smc_dpm_table.plx_i2c_address;

	ppsmc_pptable->Liquid_I2C_LineSCL = smc_dpm_table.liquid_i2c_linescl;
	ppsmc_pptable->Liquid_I2C_LineSDA = smc_dpm_table.liquid_i2c_linesda;
	ppsmc_pptable->Vr_I2C_LineSCL = smc_dpm_table.vr_i2c_linescl;
	ppsmc_pptable->Vr_I2C_LineSDA = smc_dpm_table.vr_i2c_linesda;

	ppsmc_pptable->Plx_I2C_LineSCL = smc_dpm_table.plx_i2c_linescl;
	ppsmc_pptable->Plx_I2C_LineSDA = smc_dpm_table.plx_i2c_linesda;
	ppsmc_pptable->VrSensorPresent = smc_dpm_table.vrsensorpresent;
	ppsmc_pptable->LiquidSensorPresent = smc_dpm_table.liquidsensorpresent;

	ppsmc_pptable->MaxVoltageStepGfx = smc_dpm_table.maxvoltagestepgfx;
	ppsmc_pptable->MaxVoltageStepSoc = smc_dpm_table.maxvoltagestepsoc;

	ppsmc_pptable->VddGfxVrMapping = smc_dpm_table.vddgfxvrmapping;
	ppsmc_pptable->VddSocVrMapping = smc_dpm_table.vddsocvrmapping;
	ppsmc_pptable->VddMem0VrMapping = smc_dpm_table.vddmem0vrmapping;
	ppsmc_pptable->VddMem1VrMapping = smc_dpm_table.vddmem1vrmapping;

	ppsmc_pptable->GfxUlvPhaseSheddingMask = smc_dpm_table.gfxulvphasesheddingmask;
	ppsmc_pptable->SocUlvPhaseSheddingMask = smc_dpm_table.soculvphasesheddingmask;

	ppsmc_pptable->GfxMaxCurrent = smc_dpm_table.gfxmaxcurrent;
	ppsmc_pptable->GfxOffset = smc_dpm_table.gfxoffset;
	ppsmc_pptable->Padding_TelemetryGfx = smc_dpm_table.padding_telemetrygfx;

	ppsmc_pptable->SocMaxCurrent = smc_dpm_table.socmaxcurrent;
	ppsmc_pptable->SocOffset = smc_dpm_table.socoffset;
	ppsmc_pptable->Padding_TelemetrySoc = smc_dpm_table.padding_telemetrysoc;

	ppsmc_pptable->Mem0MaxCurrent = smc_dpm_table.mem0maxcurrent;
	ppsmc_pptable->Mem0Offset = smc_dpm_table.mem0offset;
	ppsmc_pptable->Padding_TelemetryMem0 = smc_dpm_table.padding_telemetrymem0;

	ppsmc_pptable->Mem1MaxCurrent = smc_dpm_table.mem1maxcurrent;
	ppsmc_pptable->Mem1Offset = smc_dpm_table.mem1offset;
	ppsmc_pptable->Padding_TelemetryMem1 = smc_dpm_table.padding_telemetrymem1;

	ppsmc_pptable->AcDcGpio = smc_dpm_table.acdcgpio;
	ppsmc_pptable->AcDcPolarity = smc_dpm_table.acdcpolarity;
	ppsmc_pptable->VR0HotGpio = smc_dpm_table.vr0hotgpio;
	ppsmc_pptable->VR0HotPolarity = smc_dpm_table.vr0hotpolarity;

	ppsmc_pptable->VR1HotGpio = smc_dpm_table.vr1hotgpio;
	ppsmc_pptable->VR1HotPolarity = smc_dpm_table.vr1hotpolarity;
	ppsmc_pptable->Padding1 = smc_dpm_table.padding1;
	ppsmc_pptable->Padding2 = smc_dpm_table.padding2;

	ppsmc_pptable->LedPin0 = smc_dpm_table.ledpin0;
	ppsmc_pptable->LedPin1 = smc_dpm_table.ledpin1;
	ppsmc_pptable->LedPin2 = smc_dpm_table.ledpin2;

	ppsmc_pptable->GfxclkSpreadEnabled = smc_dpm_table.gfxclkspreadenabled;
	ppsmc_pptable->GfxclkSpreadPercent = smc_dpm_table.gfxclkspreadpercent;
	ppsmc_pptable->GfxclkSpreadFreq = smc_dpm_table.gfxclkspreadfreq;

	ppsmc_pptable->UclkSpreadEnabled = 0;
	ppsmc_pptable->UclkSpreadPercent = smc_dpm_table.uclkspreadpercent;
	ppsmc_pptable->UclkSpreadFreq = smc_dpm_table.uclkspreadfreq;

	ppsmc_pptable->SocclkSpreadEnabled = 0;
	ppsmc_pptable->SocclkSpreadPercent = smc_dpm_table.socclkspreadpercent;
	ppsmc_pptable->SocclkSpreadFreq = smc_dpm_table.socclkspreadfreq;

	return 0;
}

#define VEGA12_ENGINECLOCK_HARDMAX 198000
static int init_powerplay_table_information(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega12_POWERPLAYTABLE *powerplay_table)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;
	uint32_t disable_power_control = 0;
	int result;

	hwmgr->thermal_controller.ucType = powerplay_table->ucThermalControllerType;
	pptable_information->uc_thermal_controller_type = powerplay_table->ucThermalControllerType;

	set_hw_cap(hwmgr,
		ATOM_VEGA12_PP_THERMALCONTROLLER_NONE != hwmgr->thermal_controller.ucType,
		PHM_PlatformCaps_ThermalController);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_MicrocodeFanControl);

	if (powerplay_table->ODSettingsMax[ATOM_VEGA12_ODSETTING_GFXCLKFMAX] > VEGA12_ENGINECLOCK_HARDMAX)
		hwmgr->platform_descriptor.overdriveLimit.engineClock = VEGA12_ENGINECLOCK_HARDMAX;
	else
		hwmgr->platform_descriptor.overdriveLimit.engineClock = powerplay_table->ODSettingsMax[ATOM_VEGA12_ODSETTING_GFXCLKFMAX];
	hwmgr->platform_descriptor.overdriveLimit.memoryClock = powerplay_table->ODSettingsMax[ATOM_VEGA12_ODSETTING_UCLKFMAX];

	copy_overdrive_settings_limits_array(hwmgr, &pptable_information->od_settings_max, powerplay_table->ODSettingsMax);
	copy_overdrive_settings_limits_array(hwmgr, &pptable_information->od_settings_min, powerplay_table->ODSettingsMin);

	/* hwmgr->platformDescriptor.minOverdriveVDDC = 0;
	hwmgr->platformDescriptor.maxOverdriveVDDC = 0;
	hwmgr->platformDescriptor.overdriveVDDCStep = 0; */

	if (hwmgr->platform_descriptor.overdriveLimit.engineClock > 0
		&& hwmgr->platform_descriptor.overdriveLimit.memoryClock > 0)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_ACOverdriveSupport);

	pptable_information->us_small_power_limit1 = powerplay_table->usSmallPowerLimit1;
	pptable_information->us_small_power_limit2 = powerplay_table->usSmallPowerLimit2;
	pptable_information->us_boost_power_limit = powerplay_table->usBoostPowerLimit;
	pptable_information->us_od_turbo_power_limit = powerplay_table->usODTurboPowerLimit;
	pptable_information->us_od_powersave_power_limit = powerplay_table->usODPowerSavePowerLimit;

	pptable_information->us_software_shutdown_temp = powerplay_table->usSoftwareShutdownTemp;

	hwmgr->platform_descriptor.TDPODLimit = (uint16_t)powerplay_table->ODSettingsMax[ATOM_VEGA12_ODSETTING_POWERPERCENTAGE];

	disable_power_control = 0;
	if (!disable_power_control) {
		/* enable TDP overdrive (PowerControl) feature as well if supported */
		if (hwmgr->platform_descriptor.TDPODLimit)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_PowerControl);
	}

	copy_clock_limits_array(hwmgr, &pptable_information->power_saving_clock_max, powerplay_table->PowerSavingClockMax);
	copy_clock_limits_array(hwmgr, &pptable_information->power_saving_clock_min, powerplay_table->PowerSavingClockMin);

	pptable_information->smc_pptable = (PPTable_t *)kmalloc(sizeof(PPTable_t), GFP_KERNEL);
	if (pptable_information->smc_pptable == NULL)
		return -ENOMEM;

	memcpy(pptable_information->smc_pptable, &(powerplay_table->smcPPTable), sizeof(PPTable_t));

	result = append_vbios_pptable(hwmgr, (pptable_information->smc_pptable));

	return result;
}

int vega12_pp_tables_initialize(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	const ATOM_Vega12_POWERPLAYTABLE *powerplay_table;

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

static int vega12_pp_tables_uninitialize(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v3_information *pp_table_info =
			(struct phm_ppt_v3_information *)(hwmgr->pptable);

	kfree(pp_table_info->power_saving_clock_max);
	pp_table_info->power_saving_clock_max = NULL;

	kfree(pp_table_info->power_saving_clock_min);
	pp_table_info->power_saving_clock_min = NULL;

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

const struct pp_table_func vega12_pptable_funcs = {
	.pptable_init = vega12_pp_tables_initialize,
	.pptable_fini = vega12_pp_tables_uninitialize,
};

#if 0
static uint32_t make_classification_flags(struct pp_hwmgr *hwmgr,
		uint16_t classification, uint16_t classification2)
{
	uint32_t result = 0;

	if (classification & ATOM_PPLIB_CLASSIFICATION_BOOT)
		result |= PP_StateClassificationFlag_Boot;

	if (classification & ATOM_PPLIB_CLASSIFICATION_THERMAL)
		result |= PP_StateClassificationFlag_Thermal;

	if (classification & ATOM_PPLIB_CLASSIFICATION_LIMITEDPOWERSOURCE)
		result |= PP_StateClassificationFlag_LimitedPowerSource;

	if (classification & ATOM_PPLIB_CLASSIFICATION_REST)
		result |= PP_StateClassificationFlag_Rest;

	if (classification & ATOM_PPLIB_CLASSIFICATION_FORCED)
		result |= PP_StateClassificationFlag_Forced;

	if (classification & ATOM_PPLIB_CLASSIFICATION_ACPI)
		result |= PP_StateClassificationFlag_ACPI;

	if (classification2 & ATOM_PPLIB_CLASSIFICATION2_LIMITEDPOWERSOURCE_2)
		result |= PP_StateClassificationFlag_LimitedPowerSource_2;

	return result;
}

int vega12_get_powerplay_table_entry(struct pp_hwmgr *hwmgr,
		uint32_t entry_index, struct pp_power_state *power_state,
		int (*call_back_func)(struct pp_hwmgr *, void *,
				struct pp_power_state *, void *, uint32_t))
{
	int result = 0;
	const ATOM_Vega12_State_Array *state_arrays;
	const ATOM_Vega12_State *state_entry;
	const ATOM_Vega12_POWERPLAYTABLE *pp_table =
			get_powerplay_table(hwmgr);

	PP_ASSERT_WITH_CODE(pp_table, "Missing PowerPlay Table!",
			return -1;);
	power_state->classification.bios_index = entry_index;

	if (pp_table->sHeader.format_revision >=
			ATOM_Vega12_TABLE_REVISION_VEGA12) {
		state_arrays = (ATOM_Vega12_State_Array *)
				(((unsigned long)pp_table) +
				le16_to_cpu(pp_table->usStateArrayOffset));

		PP_ASSERT_WITH_CODE(pp_table->usStateArrayOffset > 0,
				"Invalid PowerPlay Table State Array Offset.",
				return -1);
		PP_ASSERT_WITH_CODE(state_arrays->ucNumEntries > 0,
				"Invalid PowerPlay Table State Array.",
				return -1);
		PP_ASSERT_WITH_CODE((entry_index <= state_arrays->ucNumEntries),
				"Invalid PowerPlay Table State Array Entry.",
				return -1);

		state_entry = &(state_arrays->states[entry_index]);

		result = call_back_func(hwmgr, (void *)state_entry, power_state,
				(void *)pp_table,
				make_classification_flags(hwmgr,
					le16_to_cpu(state_entry->usClassification),
					le16_to_cpu(state_entry->usClassification2)));
	}

	if (!result && (power_state->classification.flags &
			PP_StateClassificationFlag_Boot))
		result = hwmgr->hwmgr_func->patch_boot_state(hwmgr, &(power_state->hardware));

	return result;
}
#endif
