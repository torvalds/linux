/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/fb.h>

#include "vega10_processpptables.h"
#include "ppatomfwctrl.h"
#include "atomfirmware.h"
#include "pp_debug.h"
#include "cgs_common.h"
#include "vega10_pptable.h"

#define NUM_DSPCLK_LEVELS 8
#define VEGA10_ENGINECLOCK_HARDMAX 198000

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
		table_address = (ATOM_Vega10_POWERPLAYTABLE *)
				smu_atom_get_data_table(hwmgr->adev, index,
						&size, &frev, &crev);

		hwmgr->soft_pp_table = table_address;	/*Cache the result in RAM.*/
		hwmgr->soft_pp_table_size = size;
	}

	return table_address;
}

static int check_powerplay_tables(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega10_POWERPLAYTABLE *powerplay_table)
{
	const ATOM_Vega10_State_Array *state_arrays;

	state_arrays = (ATOM_Vega10_State_Array *)(((unsigned long)powerplay_table) +
		le16_to_cpu(powerplay_table->usStateArrayOffset));

	PP_ASSERT_WITH_CODE((powerplay_table->sHeader.format_revision >=
			ATOM_Vega10_TABLE_REVISION_VEGA10),
		"Unsupported PPTable format!", return -1);
	PP_ASSERT_WITH_CODE(powerplay_table->usStateArrayOffset,
		"State table is not set!", return -1);
	PP_ASSERT_WITH_CODE(powerplay_table->sHeader.structuresize > 0,
		"Invalid PowerPlay Table!", return -1);
	PP_ASSERT_WITH_CODE(state_arrays->ucNumEntries > 0,
		"Invalid PowerPlay Table!", return -1);

	return 0;
}

static int set_platform_caps(struct pp_hwmgr *hwmgr, uint32_t powerplay_caps)
{
	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA10_PP_PLATFORM_CAP_POWERPLAY),
			PHM_PlatformCaps_PowerPlaySupport);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA10_PP_PLATFORM_CAP_SBIOSPOWERSOURCE),
			PHM_PlatformCaps_BiosPowerSourceControl);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA10_PP_PLATFORM_CAP_HARDWAREDC),
			PHM_PlatformCaps_AutomaticDCTransition);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA10_PP_PLATFORM_CAP_BACO),
			PHM_PlatformCaps_BACO);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_VEGA10_PP_PLATFORM_COMBINE_PCC_WITH_THERMAL_SIGNAL),
			PHM_PlatformCaps_CombinePCCWithThermalSignal);

	return 0;
}

static int init_thermal_controller(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega10_POWERPLAYTABLE *powerplay_table)
{
	const ATOM_Vega10_Thermal_Controller *thermal_controller;
	const Vega10_PPTable_Generic_SubTable_Header *header;
	const ATOM_Vega10_Fan_Table *fan_table_v1;
	const ATOM_Vega10_Fan_Table_V2 *fan_table_v2;
	const ATOM_Vega10_Fan_Table_V3 *fan_table_v3;

	thermal_controller = (ATOM_Vega10_Thermal_Controller *)
			(((unsigned long)powerplay_table) +
			le16_to_cpu(powerplay_table->usThermalControllerOffset));

	PP_ASSERT_WITH_CODE((powerplay_table->usThermalControllerOffset != 0),
			"Thermal controller table not set!", return -EINVAL);

	hwmgr->thermal_controller.ucType = thermal_controller->ucType;
	hwmgr->thermal_controller.ucI2cLine = thermal_controller->ucI2cLine;
	hwmgr->thermal_controller.ucI2cAddress = thermal_controller->ucI2cAddress;

	hwmgr->thermal_controller.fanInfo.bNoFan =
			(0 != (thermal_controller->ucFanParameters &
			ATOM_VEGA10_PP_FANPARAMETERS_NOFAN));

	hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution =
			thermal_controller->ucFanParameters &
			ATOM_VEGA10_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK;

	hwmgr->thermal_controller.fanInfo.ulMinRPM =
			thermal_controller->ucFanMinRPM * 100UL;
	hwmgr->thermal_controller.fanInfo.ulMaxRPM =
			thermal_controller->ucFanMaxRPM * 100UL;

	hwmgr->thermal_controller.advanceFanControlParameters.ulCycleDelay
			= 100000;

	set_hw_cap(
			hwmgr,
			ATOM_VEGA10_PP_THERMALCONTROLLER_NONE != hwmgr->thermal_controller.ucType,
			PHM_PlatformCaps_ThermalController);

	if (!powerplay_table->usFanTableOffset)
		return 0;

	header = (const Vega10_PPTable_Generic_SubTable_Header *)
			(((unsigned long)powerplay_table) +
			le16_to_cpu(powerplay_table->usFanTableOffset));

	if (header->ucRevId == 10) {
		fan_table_v1 = (ATOM_Vega10_Fan_Table *)header;

		PP_ASSERT_WITH_CODE((fan_table_v1->ucRevId >= 8),
				"Invalid Input Fan Table!", return -EINVAL);

		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);

		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity =
				le16_to_cpu(fan_table_v1->usFanOutputSensitivity);
		hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM =
				le16_to_cpu(fan_table_v1->usFanRPMMax);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMMaxLimit =
				le16_to_cpu(fan_table_v1->usThrottlingRPM);
		hwmgr->thermal_controller.advanceFanControlParameters.ulMinFanSCLKAcousticLimit =
				le16_to_cpu(fan_table_v1->usFanAcousticLimit);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMax =
				le16_to_cpu(fan_table_v1->usTargetTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin =
				le16_to_cpu(fan_table_v1->usMinimumPWMLimit);
		hwmgr->thermal_controller.advanceFanControlParameters.ulTargetGfxClk =
				le16_to_cpu(fan_table_v1->usTargetGfxClk);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainEdge =
				le16_to_cpu(fan_table_v1->usFanGainEdge);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHotspot =
				le16_to_cpu(fan_table_v1->usFanGainHotspot);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainLiquid =
				le16_to_cpu(fan_table_v1->usFanGainLiquid);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrVddc =
				le16_to_cpu(fan_table_v1->usFanGainVrVddc);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrMvdd =
				le16_to_cpu(fan_table_v1->usFanGainVrMvdd);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainPlx =
				le16_to_cpu(fan_table_v1->usFanGainPlx);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHbm =
				le16_to_cpu(fan_table_v1->usFanGainHbm);

		hwmgr->thermal_controller.advanceFanControlParameters.ucEnableZeroRPM =
				fan_table_v1->ucEnableZeroRPM;
		hwmgr->thermal_controller.advanceFanControlParameters.usZeroRPMStopTemperature =
				le16_to_cpu(fan_table_v1->usFanStopTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.usZeroRPMStartTemperature =
				le16_to_cpu(fan_table_v1->usFanStartTemperature);
	} else if (header->ucRevId == 0xb) {
		fan_table_v2 = (ATOM_Vega10_Fan_Table_V2 *)header;

		hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution =
				fan_table_v2->ucFanParameters & ATOM_VEGA10_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK;
		hwmgr->thermal_controller.fanInfo.ulMinRPM = fan_table_v2->ucFanMinRPM * 100UL;
		hwmgr->thermal_controller.fanInfo.ulMaxRPM = fan_table_v2->ucFanMaxRPM * 100UL;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity =
				le16_to_cpu(fan_table_v2->usFanOutputSensitivity);
		hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM =
				fan_table_v2->ucFanMaxRPM * 100UL;
		hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMMaxLimit =
				le16_to_cpu(fan_table_v2->usThrottlingRPM);
		hwmgr->thermal_controller.advanceFanControlParameters.ulMinFanSCLKAcousticLimit =
				le16_to_cpu(fan_table_v2->usFanAcousticLimitRpm);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMax =
				le16_to_cpu(fan_table_v2->usTargetTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin =
				le16_to_cpu(fan_table_v2->usMinimumPWMLimit);
		hwmgr->thermal_controller.advanceFanControlParameters.ulTargetGfxClk =
				le16_to_cpu(fan_table_v2->usTargetGfxClk);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainEdge =
				le16_to_cpu(fan_table_v2->usFanGainEdge);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHotspot =
				le16_to_cpu(fan_table_v2->usFanGainHotspot);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainLiquid =
				le16_to_cpu(fan_table_v2->usFanGainLiquid);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrVddc =
				le16_to_cpu(fan_table_v2->usFanGainVrVddc);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrMvdd =
				le16_to_cpu(fan_table_v2->usFanGainVrMvdd);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainPlx =
				le16_to_cpu(fan_table_v2->usFanGainPlx);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHbm =
				le16_to_cpu(fan_table_v2->usFanGainHbm);

		hwmgr->thermal_controller.advanceFanControlParameters.ucEnableZeroRPM =
				fan_table_v2->ucEnableZeroRPM;
		hwmgr->thermal_controller.advanceFanControlParameters.usZeroRPMStopTemperature =
				le16_to_cpu(fan_table_v2->usFanStopTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.usZeroRPMStartTemperature =
				le16_to_cpu(fan_table_v2->usFanStartTemperature);
	} else if (header->ucRevId > 0xb) {
		fan_table_v3 = (ATOM_Vega10_Fan_Table_V3 *)header;

		hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution =
				fan_table_v3->ucFanParameters & ATOM_VEGA10_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK;
		hwmgr->thermal_controller.fanInfo.ulMinRPM = fan_table_v3->ucFanMinRPM * 100UL;
		hwmgr->thermal_controller.fanInfo.ulMaxRPM = fan_table_v3->ucFanMaxRPM * 100UL;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity =
				le16_to_cpu(fan_table_v3->usFanOutputSensitivity);
		hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM =
				fan_table_v3->ucFanMaxRPM * 100UL;
		hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMMaxLimit =
				le16_to_cpu(fan_table_v3->usThrottlingRPM);
		hwmgr->thermal_controller.advanceFanControlParameters.ulMinFanSCLKAcousticLimit =
				le16_to_cpu(fan_table_v3->usFanAcousticLimitRpm);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMax =
				le16_to_cpu(fan_table_v3->usTargetTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin =
				le16_to_cpu(fan_table_v3->usMinimumPWMLimit);
		hwmgr->thermal_controller.advanceFanControlParameters.ulTargetGfxClk =
				le16_to_cpu(fan_table_v3->usTargetGfxClk);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainEdge =
				le16_to_cpu(fan_table_v3->usFanGainEdge);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHotspot =
				le16_to_cpu(fan_table_v3->usFanGainHotspot);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainLiquid =
				le16_to_cpu(fan_table_v3->usFanGainLiquid);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrVddc =
				le16_to_cpu(fan_table_v3->usFanGainVrVddc);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrMvdd =
				le16_to_cpu(fan_table_v3->usFanGainVrMvdd);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainPlx =
				le16_to_cpu(fan_table_v3->usFanGainPlx);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHbm =
				le16_to_cpu(fan_table_v3->usFanGainHbm);

		hwmgr->thermal_controller.advanceFanControlParameters.ucEnableZeroRPM =
				fan_table_v3->ucEnableZeroRPM;
		hwmgr->thermal_controller.advanceFanControlParameters.usZeroRPMStopTemperature =
				le16_to_cpu(fan_table_v3->usFanStopTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.usZeroRPMStartTemperature =
				le16_to_cpu(fan_table_v3->usFanStartTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.usMGpuThrottlingRPMLimit =
				le16_to_cpu(fan_table_v3->usMGpuThrottlingRPM);
	}

	return 0;
}

static int init_over_drive_limits(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega10_POWERPLAYTABLE *powerplay_table)
{
	const ATOM_Vega10_GFXCLK_Dependency_Table *gfxclk_dep_table =
			(const ATOM_Vega10_GFXCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usGfxclkDependencyTableOffset));
	bool is_acg_enabled = false;
	ATOM_Vega10_GFXCLK_Dependency_Record_V2 *patom_record_v2;

	if (gfxclk_dep_table->ucRevId == 1) {
		patom_record_v2 =
			(ATOM_Vega10_GFXCLK_Dependency_Record_V2 *)gfxclk_dep_table->entries;
		is_acg_enabled =
			(bool)patom_record_v2[gfxclk_dep_table->ucNumEntries-1].ucACGEnable;
	}

	if (powerplay_table->ulMaxODEngineClock > VEGA10_ENGINECLOCK_HARDMAX &&
		!is_acg_enabled)
		hwmgr->platform_descriptor.overdriveLimit.engineClock =
			VEGA10_ENGINECLOCK_HARDMAX;
	else
		hwmgr->platform_descriptor.overdriveLimit.engineClock =
			le32_to_cpu(powerplay_table->ulMaxODEngineClock);
	hwmgr->platform_descriptor.overdriveLimit.memoryClock =
			le32_to_cpu(powerplay_table->ulMaxODMemoryClock);

	hwmgr->platform_descriptor.minOverdriveVDDC = 0;
	hwmgr->platform_descriptor.maxOverdriveVDDC = 0;
	hwmgr->platform_descriptor.overdriveVDDCStep = 0;

	return 0;
}

static int get_mm_clock_voltage_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_mm_clock_voltage_dependency_table **vega10_mm_table,
		const ATOM_Vega10_MM_Dependency_Table *mm_dependency_table)
{
	uint32_t i;
	const ATOM_Vega10_MM_Dependency_Record *mm_dependency_record;
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table;

	PP_ASSERT_WITH_CODE((mm_dependency_table->ucNumEntries != 0),
			"Invalid PowerPlay Table!", return -1);

	mm_table = kzalloc(struct_size(mm_table, entries, mm_dependency_table->ucNumEntries),
			   GFP_KERNEL);
	if (!mm_table)
		return -ENOMEM;

	mm_table->count = mm_dependency_table->ucNumEntries;

	for (i = 0; i < mm_dependency_table->ucNumEntries; i++) {
		mm_dependency_record = &mm_dependency_table->entries[i];
		mm_table->entries[i].vddcInd = mm_dependency_record->ucVddcInd;
		mm_table->entries[i].samclock =
				le32_to_cpu(mm_dependency_record->ulPSPClk);
		mm_table->entries[i].eclk = le32_to_cpu(mm_dependency_record->ulEClk);
		mm_table->entries[i].vclk = le32_to_cpu(mm_dependency_record->ulVClk);
		mm_table->entries[i].dclk = le32_to_cpu(mm_dependency_record->ulDClk);
	}

	*vega10_mm_table = mm_table;

	return 0;
}

static void get_scl_sda_value(uint8_t line, uint8_t *scl, uint8_t* sda)
{
	switch(line){
	case Vega10_I2CLineID_DDC1:
		*scl = Vega10_I2C_DDC1CLK;
		*sda = Vega10_I2C_DDC1DATA;
		break;
	case Vega10_I2CLineID_DDC2:
		*scl = Vega10_I2C_DDC2CLK;
		*sda = Vega10_I2C_DDC2DATA;
		break;
	case Vega10_I2CLineID_DDC3:
		*scl = Vega10_I2C_DDC3CLK;
		*sda = Vega10_I2C_DDC3DATA;
		break;
	case Vega10_I2CLineID_DDC4:
		*scl = Vega10_I2C_DDC4CLK;
		*sda = Vega10_I2C_DDC4DATA;
		break;
	case Vega10_I2CLineID_DDC5:
		*scl = Vega10_I2C_DDC5CLK;
		*sda = Vega10_I2C_DDC5DATA;
		break;
	case Vega10_I2CLineID_DDC6:
		*scl = Vega10_I2C_DDC6CLK;
		*sda = Vega10_I2C_DDC6DATA;
		break;
	case Vega10_I2CLineID_SCLSDA:
		*scl = Vega10_I2C_SCL;
		*sda = Vega10_I2C_SDA;
		break;
	case Vega10_I2CLineID_DDCVGA:
		*scl = Vega10_I2C_DDCVGACLK;
		*sda = Vega10_I2C_DDCVGADATA;
		break;
	default:
		*scl = 0;
		*sda = 0;
		break;
	}
}

static int get_tdp_table(
		struct pp_hwmgr *hwmgr,
		struct phm_tdp_table **info_tdp_table,
		const Vega10_PPTable_Generic_SubTable_Header *table)
{
	uint32_t table_size;
	struct phm_tdp_table *tdp_table;
	uint8_t scl;
	uint8_t sda;
	const ATOM_Vega10_PowerTune_Table *power_tune_table;
	const ATOM_Vega10_PowerTune_Table_V2 *power_tune_table_v2;
	const ATOM_Vega10_PowerTune_Table_V3 *power_tune_table_v3;

	table_size = sizeof(uint32_t) + sizeof(struct phm_tdp_table);

	tdp_table = kzalloc(table_size, GFP_KERNEL);

	if (!tdp_table)
		return -ENOMEM;

	if (table->ucRevId == 5) {
		power_tune_table = (ATOM_Vega10_PowerTune_Table *)table;
		tdp_table->usMaximumPowerDeliveryLimit = le16_to_cpu(power_tune_table->usSocketPowerLimit);
		tdp_table->usTDC = le16_to_cpu(power_tune_table->usTdcLimit);
		tdp_table->usEDCLimit = le16_to_cpu(power_tune_table->usEdcLimit);
		tdp_table->usSoftwareShutdownTemp =
				le16_to_cpu(power_tune_table->usSoftwareShutdownTemp);
		tdp_table->usTemperatureLimitTedge =
				le16_to_cpu(power_tune_table->usTemperatureLimitTedge);
		tdp_table->usTemperatureLimitHotspot =
				le16_to_cpu(power_tune_table->usTemperatureLimitHotSpot);
		tdp_table->usTemperatureLimitLiquid1 =
				le16_to_cpu(power_tune_table->usTemperatureLimitLiquid1);
		tdp_table->usTemperatureLimitLiquid2 =
				le16_to_cpu(power_tune_table->usTemperatureLimitLiquid2);
		tdp_table->usTemperatureLimitHBM =
				le16_to_cpu(power_tune_table->usTemperatureLimitHBM);
		tdp_table->usTemperatureLimitVrVddc =
				le16_to_cpu(power_tune_table->usTemperatureLimitVrSoc);
		tdp_table->usTemperatureLimitVrMvdd =
				le16_to_cpu(power_tune_table->usTemperatureLimitVrMem);
		tdp_table->usTemperatureLimitPlx =
				le16_to_cpu(power_tune_table->usTemperatureLimitPlx);
		tdp_table->ucLiquid1_I2C_address = power_tune_table->ucLiquid1_I2C_address;
		tdp_table->ucLiquid2_I2C_address = power_tune_table->ucLiquid2_I2C_address;
		tdp_table->ucLiquid_I2C_Line = power_tune_table->ucLiquid_I2C_LineSCL;
		tdp_table->ucLiquid_I2C_LineSDA = power_tune_table->ucLiquid_I2C_LineSDA;
		tdp_table->ucVr_I2C_address = power_tune_table->ucVr_I2C_address;
		tdp_table->ucVr_I2C_Line = power_tune_table->ucVr_I2C_LineSCL;
		tdp_table->ucVr_I2C_LineSDA = power_tune_table->ucVr_I2C_LineSDA;
		tdp_table->ucPlx_I2C_address = power_tune_table->ucPlx_I2C_address;
		tdp_table->ucPlx_I2C_Line = power_tune_table->ucPlx_I2C_LineSCL;
		tdp_table->ucPlx_I2C_LineSDA = power_tune_table->ucPlx_I2C_LineSDA;
		hwmgr->platform_descriptor.LoadLineSlope = le16_to_cpu(power_tune_table->usLoadLineResistance);
	} else if (table->ucRevId == 6) {
		power_tune_table_v2 = (ATOM_Vega10_PowerTune_Table_V2 *)table;
		tdp_table->usMaximumPowerDeliveryLimit = le16_to_cpu(power_tune_table_v2->usSocketPowerLimit);
		tdp_table->usTDC = le16_to_cpu(power_tune_table_v2->usTdcLimit);
		tdp_table->usEDCLimit = le16_to_cpu(power_tune_table_v2->usEdcLimit);
		tdp_table->usSoftwareShutdownTemp =
				le16_to_cpu(power_tune_table_v2->usSoftwareShutdownTemp);
		tdp_table->usTemperatureLimitTedge =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitTedge);
		tdp_table->usTemperatureLimitHotspot =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitHotSpot);
		tdp_table->usTemperatureLimitLiquid1 =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitLiquid1);
		tdp_table->usTemperatureLimitLiquid2 =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitLiquid2);
		tdp_table->usTemperatureLimitHBM =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitHBM);
		tdp_table->usTemperatureLimitVrVddc =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitVrSoc);
		tdp_table->usTemperatureLimitVrMvdd =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitVrMem);
		tdp_table->usTemperatureLimitPlx =
				le16_to_cpu(power_tune_table_v2->usTemperatureLimitPlx);
		tdp_table->ucLiquid1_I2C_address = power_tune_table_v2->ucLiquid1_I2C_address;
		tdp_table->ucLiquid2_I2C_address = power_tune_table_v2->ucLiquid2_I2C_address;

		get_scl_sda_value(power_tune_table_v2->ucLiquid_I2C_Line, &scl, &sda);

		tdp_table->ucLiquid_I2C_Line = scl;
		tdp_table->ucLiquid_I2C_LineSDA = sda;

		tdp_table->ucVr_I2C_address = power_tune_table_v2->ucVr_I2C_address;

		get_scl_sda_value(power_tune_table_v2->ucVr_I2C_Line, &scl, &sda);

		tdp_table->ucVr_I2C_Line = scl;
		tdp_table->ucVr_I2C_LineSDA = sda;
		tdp_table->ucPlx_I2C_address = power_tune_table_v2->ucPlx_I2C_address;

		get_scl_sda_value(power_tune_table_v2->ucPlx_I2C_Line, &scl, &sda);

		tdp_table->ucPlx_I2C_Line = scl;
		tdp_table->ucPlx_I2C_LineSDA = sda;

		hwmgr->platform_descriptor.LoadLineSlope =
					le16_to_cpu(power_tune_table_v2->usLoadLineResistance);
	} else {
		power_tune_table_v3 = (ATOM_Vega10_PowerTune_Table_V3 *)table;
		tdp_table->usMaximumPowerDeliveryLimit   = le16_to_cpu(power_tune_table_v3->usSocketPowerLimit);
		tdp_table->usTDC                         = le16_to_cpu(power_tune_table_v3->usTdcLimit);
		tdp_table->usEDCLimit                    = le16_to_cpu(power_tune_table_v3->usEdcLimit);
		tdp_table->usSoftwareShutdownTemp        = le16_to_cpu(power_tune_table_v3->usSoftwareShutdownTemp);
		tdp_table->usTemperatureLimitTedge       = le16_to_cpu(power_tune_table_v3->usTemperatureLimitTedge);
		tdp_table->usTemperatureLimitHotspot     = le16_to_cpu(power_tune_table_v3->usTemperatureLimitHotSpot);
		tdp_table->usTemperatureLimitLiquid1     = le16_to_cpu(power_tune_table_v3->usTemperatureLimitLiquid1);
		tdp_table->usTemperatureLimitLiquid2     = le16_to_cpu(power_tune_table_v3->usTemperatureLimitLiquid2);
		tdp_table->usTemperatureLimitHBM         = le16_to_cpu(power_tune_table_v3->usTemperatureLimitHBM);
		tdp_table->usTemperatureLimitVrVddc      = le16_to_cpu(power_tune_table_v3->usTemperatureLimitVrSoc);
		tdp_table->usTemperatureLimitVrMvdd      = le16_to_cpu(power_tune_table_v3->usTemperatureLimitVrMem);
		tdp_table->usTemperatureLimitPlx         = le16_to_cpu(power_tune_table_v3->usTemperatureLimitPlx);
		tdp_table->ucLiquid1_I2C_address         = power_tune_table_v3->ucLiquid1_I2C_address;
		tdp_table->ucLiquid2_I2C_address         = power_tune_table_v3->ucLiquid2_I2C_address;
		tdp_table->usBoostStartTemperature       = le16_to_cpu(power_tune_table_v3->usBoostStartTemperature);
		tdp_table->usBoostStopTemperature        = le16_to_cpu(power_tune_table_v3->usBoostStopTemperature);
		tdp_table->ulBoostClock                  = le32_to_cpu(power_tune_table_v3->ulBoostClock);

		get_scl_sda_value(power_tune_table_v3->ucLiquid_I2C_Line, &scl, &sda);

		tdp_table->ucLiquid_I2C_Line             = scl;
		tdp_table->ucLiquid_I2C_LineSDA          = sda;

		tdp_table->ucVr_I2C_address              = power_tune_table_v3->ucVr_I2C_address;

		get_scl_sda_value(power_tune_table_v3->ucVr_I2C_Line, &scl, &sda);

		tdp_table->ucVr_I2C_Line                 = scl;
		tdp_table->ucVr_I2C_LineSDA              = sda;

		tdp_table->ucPlx_I2C_address             = power_tune_table_v3->ucPlx_I2C_address;

		get_scl_sda_value(power_tune_table_v3->ucPlx_I2C_Line, &scl, &sda);

		tdp_table->ucPlx_I2C_Line                = scl;
		tdp_table->ucPlx_I2C_LineSDA             = sda;

		hwmgr->platform_descriptor.LoadLineSlope =
					le16_to_cpu(power_tune_table_v3->usLoadLineResistance);
	}

	*info_tdp_table = tdp_table;

	return 0;
}

static int get_socclk_voltage_dependency_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table **pp_vega10_clk_dep_table,
		const ATOM_Vega10_SOCCLK_Dependency_Table *clk_dep_table)
{
	uint32_t i;
	phm_ppt_v1_clock_voltage_dependency_table *clk_table;

	PP_ASSERT_WITH_CODE(clk_dep_table->ucNumEntries,
		"Invalid PowerPlay Table!", return -1);

	clk_table = kzalloc(struct_size(clk_table, entries, clk_dep_table->ucNumEntries),
			    GFP_KERNEL);
	if (!clk_table)
		return -ENOMEM;

	clk_table->count = (uint32_t)clk_dep_table->ucNumEntries;

	for (i = 0; i < clk_dep_table->ucNumEntries; i++) {
		clk_table->entries[i].vddInd =
				clk_dep_table->entries[i].ucVddInd;
		clk_table->entries[i].clk =
				le32_to_cpu(clk_dep_table->entries[i].ulClk);
	}

	*pp_vega10_clk_dep_table = clk_table;

	return 0;
}

static int get_mclk_voltage_dependency_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table **pp_vega10_mclk_dep_table,
		const ATOM_Vega10_MCLK_Dependency_Table *mclk_dep_table)
{
	uint32_t i;
	phm_ppt_v1_clock_voltage_dependency_table *mclk_table;

	PP_ASSERT_WITH_CODE(mclk_dep_table->ucNumEntries,
		"Invalid PowerPlay Table!", return -1);

	mclk_table = kzalloc(struct_size(mclk_table, entries, mclk_dep_table->ucNumEntries),
			    GFP_KERNEL);
	if (!mclk_table)
		return -ENOMEM;

	mclk_table->count = (uint32_t)mclk_dep_table->ucNumEntries;

	for (i = 0; i < mclk_dep_table->ucNumEntries; i++) {
		mclk_table->entries[i].vddInd =
				mclk_dep_table->entries[i].ucVddInd;
		mclk_table->entries[i].vddciInd =
				mclk_dep_table->entries[i].ucVddciInd;
		mclk_table->entries[i].mvddInd =
				mclk_dep_table->entries[i].ucVddMemInd;
		mclk_table->entries[i].clk =
				le32_to_cpu(mclk_dep_table->entries[i].ulMemClk);
	}

	*pp_vega10_mclk_dep_table = mclk_table;

	return 0;
}

static int get_gfxclk_voltage_dependency_table(
		struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_clock_voltage_dependency_table
			**pp_vega10_clk_dep_table,
		const ATOM_Vega10_GFXCLK_Dependency_Table *clk_dep_table)
{
	uint32_t i;
	struct phm_ppt_v1_clock_voltage_dependency_table
				*clk_table;
	ATOM_Vega10_GFXCLK_Dependency_Record_V2 *patom_record_v2;

	PP_ASSERT_WITH_CODE((clk_dep_table->ucNumEntries != 0),
			"Invalid PowerPlay Table!", return -1);

	clk_table = kzalloc(struct_size(clk_table, entries, clk_dep_table->ucNumEntries),
			    GFP_KERNEL);
	if (!clk_table)
		return -ENOMEM;

	clk_table->count = clk_dep_table->ucNumEntries;

	if (clk_dep_table->ucRevId == 0) {
		for (i = 0; i < clk_table->count; i++) {
			clk_table->entries[i].vddInd =
				clk_dep_table->entries[i].ucVddInd;
			clk_table->entries[i].clk =
				le32_to_cpu(clk_dep_table->entries[i].ulClk);
			clk_table->entries[i].cks_enable =
				(((le16_to_cpu(clk_dep_table->entries[i].usCKSVOffsetandDisable) & 0x8000)
						>> 15) == 0) ? 1 : 0;
			clk_table->entries[i].cks_voffset =
				le16_to_cpu(clk_dep_table->entries[i].usCKSVOffsetandDisable) & 0x7F;
			clk_table->entries[i].sclk_offset =
				le16_to_cpu(clk_dep_table->entries[i].usAVFSOffset);
		}
	} else if (clk_dep_table->ucRevId == 1) {
		patom_record_v2 = (ATOM_Vega10_GFXCLK_Dependency_Record_V2 *)clk_dep_table->entries;
		for (i = 0; i < clk_table->count; i++) {
			clk_table->entries[i].vddInd =
					patom_record_v2->ucVddInd;
			clk_table->entries[i].clk =
					le32_to_cpu(patom_record_v2->ulClk);
			clk_table->entries[i].cks_enable =
					(((le16_to_cpu(patom_record_v2->usCKSVOffsetandDisable) & 0x8000)
							>> 15) == 0) ? 1 : 0;
			clk_table->entries[i].cks_voffset =
					le16_to_cpu(patom_record_v2->usCKSVOffsetandDisable) & 0x7F;
			clk_table->entries[i].sclk_offset =
					le16_to_cpu(patom_record_v2->usAVFSOffset);
			patom_record_v2++;
		}
	} else {
		kfree(clk_table);
		PP_ASSERT_WITH_CODE(false,
			"Unsupported GFXClockDependencyTable Revision!",
			return -EINVAL);
	}

	*pp_vega10_clk_dep_table = clk_table;

	return 0;
}

static int get_pix_clk_voltage_dependency_table(
		struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_clock_voltage_dependency_table
			**pp_vega10_clk_dep_table,
		const  ATOM_Vega10_PIXCLK_Dependency_Table *clk_dep_table)
{
	uint32_t i;
	struct phm_ppt_v1_clock_voltage_dependency_table
				*clk_table;

	PP_ASSERT_WITH_CODE((clk_dep_table->ucNumEntries != 0),
			"Invalid PowerPlay Table!", return -1);

	clk_table = kzalloc(struct_size(clk_table, entries, clk_dep_table->ucNumEntries),
			    GFP_KERNEL);
	if (!clk_table)
		return -ENOMEM;

	clk_table->count = clk_dep_table->ucNumEntries;

	for (i = 0; i < clk_table->count; i++) {
		clk_table->entries[i].vddInd =
				clk_dep_table->entries[i].ucVddInd;
		clk_table->entries[i].clk =
				le32_to_cpu(clk_dep_table->entries[i].ulClk);
	}

	*pp_vega10_clk_dep_table = clk_table;

	return 0;
}

static int get_dcefclk_voltage_dependency_table(
		struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_clock_voltage_dependency_table
			**pp_vega10_clk_dep_table,
		const ATOM_Vega10_DCEFCLK_Dependency_Table *clk_dep_table)
{
	uint32_t i;
	uint8_t num_entries;
	struct phm_ppt_v1_clock_voltage_dependency_table
				*clk_table;
	uint32_t dev_id;
	uint32_t rev_id;
	struct amdgpu_device *adev = hwmgr->adev;

	PP_ASSERT_WITH_CODE((clk_dep_table->ucNumEntries != 0),
			"Invalid PowerPlay Table!", return -1);

/*
 * workaround needed to add another DPM level for pioneer cards
 * as VBIOS is locked down.
 * This DPM level was added to support 3DPM monitors @ 4K120Hz
 *
 */
	dev_id = adev->pdev->device;
	rev_id = adev->pdev->revision;

	if (dev_id == 0x6863 && rev_id == 0 &&
		clk_dep_table->entries[clk_dep_table->ucNumEntries - 1].ulClk < 90000)
		num_entries = clk_dep_table->ucNumEntries + 1 > NUM_DSPCLK_LEVELS ?
				NUM_DSPCLK_LEVELS : clk_dep_table->ucNumEntries + 1;
	else
		num_entries = clk_dep_table->ucNumEntries;


	clk_table = kzalloc(struct_size(clk_table, entries, num_entries),
			    GFP_KERNEL);
	if (!clk_table)
		return -ENOMEM;

	clk_table->count = (uint32_t)num_entries;

	for (i = 0; i < clk_dep_table->ucNumEntries; i++) {
		clk_table->entries[i].vddInd =
				clk_dep_table->entries[i].ucVddInd;
		clk_table->entries[i].clk =
				le32_to_cpu(clk_dep_table->entries[i].ulClk);
	}

	if (i < num_entries) {
		clk_table->entries[i].vddInd = clk_dep_table->entries[i-1].ucVddInd;
		clk_table->entries[i].clk = 90000;
	}

	*pp_vega10_clk_dep_table = clk_table;

	return 0;
}

static int get_pcie_table(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_pcie_table **vega10_pcie_table,
		const Vega10_PPTable_Generic_SubTable_Header *table)
{
	uint32_t table_size, i, pcie_count;
	struct phm_ppt_v1_pcie_table *pcie_table;
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	const ATOM_Vega10_PCIE_Table *atom_pcie_table =
			(ATOM_Vega10_PCIE_Table *)table;

	PP_ASSERT_WITH_CODE(atom_pcie_table->ucNumEntries,
			"Invalid PowerPlay Table!",
			return 0);

	table_size = sizeof(uint32_t) +
			sizeof(struct phm_ppt_v1_pcie_record) *
			atom_pcie_table->ucNumEntries;

	pcie_table = kzalloc(table_size, GFP_KERNEL);

	if (!pcie_table)
		return -ENOMEM;

	pcie_count = table_info->vdd_dep_on_sclk->count;
	if (atom_pcie_table->ucNumEntries <= pcie_count)
		pcie_count = atom_pcie_table->ucNumEntries;
	else
		pr_info("Number of Pcie Entries exceed the number of"
				" GFXCLK Dpm Levels!"
				" Disregarding the excess entries...\n");

	pcie_table->count = pcie_count;

	for (i = 0; i < pcie_count; i++) {
		pcie_table->entries[i].gen_speed =
				atom_pcie_table->entries[i].ucPCIEGenSpeed;
		pcie_table->entries[i].lane_width =
				atom_pcie_table->entries[i].ucPCIELaneWidth;
		pcie_table->entries[i].pcie_sclk =
				atom_pcie_table->entries[i].ulLCLK;
	}

	*vega10_pcie_table = pcie_table;

	return 0;
}

static int get_hard_limits(
		struct pp_hwmgr *hwmgr,
		struct phm_clock_and_voltage_limits *limits,
		const ATOM_Vega10_Hard_Limit_Table *limit_table)
{
	PP_ASSERT_WITH_CODE(limit_table->ucNumEntries,
			"Invalid PowerPlay Table!", return -1);

	/* currently we always take entries[0] parameters */
	limits->sclk = le32_to_cpu(limit_table->entries[0].ulSOCCLKLimit);
	limits->mclk = le32_to_cpu(limit_table->entries[0].ulMCLKLimit);
	limits->gfxclk = le32_to_cpu(limit_table->entries[0].ulGFXCLKLimit);
	limits->vddc = le16_to_cpu(limit_table->entries[0].usVddcLimit);
	limits->vddci = le16_to_cpu(limit_table->entries[0].usVddciLimit);
	limits->vddmem = le16_to_cpu(limit_table->entries[0].usVddMemLimit);

	return 0;
}

static int get_valid_clk(
		struct pp_hwmgr *hwmgr,
		struct phm_clock_array **clk_table,
		const phm_ppt_v1_clock_voltage_dependency_table *clk_volt_pp_table)
{
	uint32_t i;
	struct phm_clock_array *table;

	PP_ASSERT_WITH_CODE(clk_volt_pp_table->count,
			"Invalid PowerPlay Table!", return -1);

	table = kzalloc(struct_size(table, values, clk_volt_pp_table->count),
			GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table->count = (uint32_t)clk_volt_pp_table->count;

	for (i = 0; i < table->count; i++)
		table->values[i] = (uint32_t)clk_volt_pp_table->entries[i].clk;

	*clk_table = table;

	return 0;
}

static int init_powerplay_extended_tables(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega10_POWERPLAYTABLE *powerplay_table)
{
	int result = 0;
	struct phm_ppt_v2_information *pp_table_info =
		(struct phm_ppt_v2_information *)(hwmgr->pptable);

	const ATOM_Vega10_MM_Dependency_Table *mm_dependency_table =
			(const ATOM_Vega10_MM_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usMMDependencyTableOffset));
	const Vega10_PPTable_Generic_SubTable_Header *power_tune_table =
			(const Vega10_PPTable_Generic_SubTable_Header *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usPowerTuneTableOffset));
	const ATOM_Vega10_SOCCLK_Dependency_Table *socclk_dep_table =
			(const ATOM_Vega10_SOCCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usSocclkDependencyTableOffset));
	const ATOM_Vega10_GFXCLK_Dependency_Table *gfxclk_dep_table =
			(const ATOM_Vega10_GFXCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usGfxclkDependencyTableOffset));
	const ATOM_Vega10_DCEFCLK_Dependency_Table *dcefclk_dep_table =
			(const ATOM_Vega10_DCEFCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usDcefclkDependencyTableOffset));
	const ATOM_Vega10_MCLK_Dependency_Table *mclk_dep_table =
			(const ATOM_Vega10_MCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usMclkDependencyTableOffset));
	const ATOM_Vega10_Hard_Limit_Table *hard_limits =
			(const ATOM_Vega10_Hard_Limit_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usHardLimitTableOffset));
	const Vega10_PPTable_Generic_SubTable_Header *pcie_table =
			(const Vega10_PPTable_Generic_SubTable_Header *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usPCIETableOffset));
	const ATOM_Vega10_PIXCLK_Dependency_Table *pixclk_dep_table =
			(const ATOM_Vega10_PIXCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usPixclkDependencyTableOffset));
	const ATOM_Vega10_PHYCLK_Dependency_Table *phyclk_dep_table =
			(const ATOM_Vega10_PHYCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usPhyClkDependencyTableOffset));
	const ATOM_Vega10_DISPCLK_Dependency_Table *dispclk_dep_table =
			(const ATOM_Vega10_DISPCLK_Dependency_Table *)
			(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table->usDispClkDependencyTableOffset));

	pp_table_info->vdd_dep_on_socclk = NULL;
	pp_table_info->vdd_dep_on_sclk = NULL;
	pp_table_info->vdd_dep_on_mclk = NULL;
	pp_table_info->vdd_dep_on_dcefclk = NULL;
	pp_table_info->mm_dep_table = NULL;
	pp_table_info->tdp_table = NULL;
	pp_table_info->vdd_dep_on_pixclk = NULL;
	pp_table_info->vdd_dep_on_phyclk = NULL;
	pp_table_info->vdd_dep_on_dispclk = NULL;

	if (powerplay_table->usMMDependencyTableOffset)
		result = get_mm_clock_voltage_table(hwmgr,
				&pp_table_info->mm_dep_table,
				mm_dependency_table);

	if (!result && powerplay_table->usPowerTuneTableOffset)
		result = get_tdp_table(hwmgr,
				&pp_table_info->tdp_table,
				power_tune_table);

	if (!result && powerplay_table->usSocclkDependencyTableOffset)
		result = get_socclk_voltage_dependency_table(hwmgr,
				&pp_table_info->vdd_dep_on_socclk,
				socclk_dep_table);

	if (!result && powerplay_table->usGfxclkDependencyTableOffset)
		result = get_gfxclk_voltage_dependency_table(hwmgr,
				&pp_table_info->vdd_dep_on_sclk,
				gfxclk_dep_table);

	if (!result && powerplay_table->usPixclkDependencyTableOffset)
		result = get_pix_clk_voltage_dependency_table(hwmgr,
				&pp_table_info->vdd_dep_on_pixclk,
				(const ATOM_Vega10_PIXCLK_Dependency_Table*)
				pixclk_dep_table);

	if (!result && powerplay_table->usPhyClkDependencyTableOffset)
		result = get_pix_clk_voltage_dependency_table(hwmgr,
				&pp_table_info->vdd_dep_on_phyclk,
				(const ATOM_Vega10_PIXCLK_Dependency_Table *)
				phyclk_dep_table);

	if (!result && powerplay_table->usDispClkDependencyTableOffset)
		result = get_pix_clk_voltage_dependency_table(hwmgr,
				&pp_table_info->vdd_dep_on_dispclk,
				(const ATOM_Vega10_PIXCLK_Dependency_Table *)
				dispclk_dep_table);

	if (!result && powerplay_table->usDcefclkDependencyTableOffset)
		result = get_dcefclk_voltage_dependency_table(hwmgr,
				&pp_table_info->vdd_dep_on_dcefclk,
				dcefclk_dep_table);

	if (!result && powerplay_table->usMclkDependencyTableOffset)
		result = get_mclk_voltage_dependency_table(hwmgr,
				&pp_table_info->vdd_dep_on_mclk,
				mclk_dep_table);

	if (!result && powerplay_table->usPCIETableOffset)
		result = get_pcie_table(hwmgr,
				&pp_table_info->pcie_table,
				pcie_table);

	if (!result && powerplay_table->usHardLimitTableOffset)
		result = get_hard_limits(hwmgr,
				&pp_table_info->max_clock_voltage_on_dc,
				hard_limits);

	hwmgr->dyn_state.max_clock_voltage_on_dc.sclk =
			pp_table_info->max_clock_voltage_on_dc.sclk;
	hwmgr->dyn_state.max_clock_voltage_on_dc.mclk =
			pp_table_info->max_clock_voltage_on_dc.mclk;
	hwmgr->dyn_state.max_clock_voltage_on_dc.vddc =
			pp_table_info->max_clock_voltage_on_dc.vddc;
	hwmgr->dyn_state.max_clock_voltage_on_dc.vddci =
			pp_table_info->max_clock_voltage_on_dc.vddci;

	if (!result &&
		pp_table_info->vdd_dep_on_socclk &&
		pp_table_info->vdd_dep_on_socclk->count)
		result = get_valid_clk(hwmgr,
				&pp_table_info->valid_socclk_values,
				pp_table_info->vdd_dep_on_socclk);

	if (!result &&
		pp_table_info->vdd_dep_on_sclk &&
		pp_table_info->vdd_dep_on_sclk->count)
		result = get_valid_clk(hwmgr,
				&pp_table_info->valid_sclk_values,
				pp_table_info->vdd_dep_on_sclk);

	if (!result &&
		pp_table_info->vdd_dep_on_dcefclk &&
		pp_table_info->vdd_dep_on_dcefclk->count)
		result = get_valid_clk(hwmgr,
				&pp_table_info->valid_dcefclk_values,
				pp_table_info->vdd_dep_on_dcefclk);

	if (!result &&
		pp_table_info->vdd_dep_on_mclk &&
		pp_table_info->vdd_dep_on_mclk->count)
		result = get_valid_clk(hwmgr,
				&pp_table_info->valid_mclk_values,
				pp_table_info->vdd_dep_on_mclk);

	return result;
}

static int get_vddc_lookup_table(
		struct pp_hwmgr	*hwmgr,
		phm_ppt_v1_voltage_lookup_table	**lookup_table,
		const ATOM_Vega10_Voltage_Lookup_Table *vddc_lookup_pp_tables,
		uint32_t max_levels)
{
	uint32_t table_size, i;
	phm_ppt_v1_voltage_lookup_table *table;

	PP_ASSERT_WITH_CODE((vddc_lookup_pp_tables->ucNumEntries != 0),
			"Invalid SOC_VDDD Lookup Table!", return 1);

	table_size = sizeof(uint32_t) +
			sizeof(phm_ppt_v1_voltage_lookup_record) * max_levels;

	table = kzalloc(table_size, GFP_KERNEL);

	if (table == NULL)
		return -ENOMEM;

	table->count = vddc_lookup_pp_tables->ucNumEntries;

	for (i = 0; i < vddc_lookup_pp_tables->ucNumEntries; i++)
		table->entries[i].us_vdd =
				le16_to_cpu(vddc_lookup_pp_tables->entries[i].usVdd);

	*lookup_table = table;

	return 0;
}

static int init_dpm_2_parameters(
		struct pp_hwmgr *hwmgr,
		const ATOM_Vega10_POWERPLAYTABLE *powerplay_table)
{
	int result = 0;
	struct phm_ppt_v2_information *pp_table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	uint32_t disable_power_control = 0;

	pp_table_info->us_ulv_voltage_offset =
		le16_to_cpu(powerplay_table->usUlvVoltageOffset);

	pp_table_info->us_ulv_smnclk_did =
			le16_to_cpu(powerplay_table->usUlvSmnclkDid);
	pp_table_info->us_ulv_mp1clk_did =
			le16_to_cpu(powerplay_table->usUlvMp1clkDid);
	pp_table_info->us_ulv_gfxclk_bypass =
			le16_to_cpu(powerplay_table->usUlvGfxclkBypass);
	pp_table_info->us_gfxclk_slew_rate =
			le16_to_cpu(powerplay_table->usGfxclkSlewRate);
	pp_table_info->uc_gfx_dpm_voltage_mode  =
			le16_to_cpu(powerplay_table->ucGfxVoltageMode);
	pp_table_info->uc_soc_dpm_voltage_mode  =
			le16_to_cpu(powerplay_table->ucSocVoltageMode);
	pp_table_info->uc_uclk_dpm_voltage_mode =
			le16_to_cpu(powerplay_table->ucUclkVoltageMode);
	pp_table_info->uc_uvd_dpm_voltage_mode  =
			le16_to_cpu(powerplay_table->ucUvdVoltageMode);
	pp_table_info->uc_vce_dpm_voltage_mode  =
			le16_to_cpu(powerplay_table->ucVceVoltageMode);
	pp_table_info->uc_mp0_dpm_voltage_mode  =
			le16_to_cpu(powerplay_table->ucMp0VoltageMode);
	pp_table_info->uc_dcef_dpm_voltage_mode =
			le16_to_cpu(powerplay_table->ucDcefVoltageMode);

	pp_table_info->ppm_parameter_table = NULL;
	pp_table_info->vddc_lookup_table = NULL;
	pp_table_info->vddmem_lookup_table = NULL;
	pp_table_info->vddci_lookup_table = NULL;

	/* TDP limits */
	hwmgr->platform_descriptor.TDPODLimit =
		le16_to_cpu(powerplay_table->usPowerControlLimit);
	hwmgr->platform_descriptor.TDPAdjustment = 0;
	hwmgr->platform_descriptor.VidAdjustment = 0;
	hwmgr->platform_descriptor.VidAdjustmentPolarity = 0;
	hwmgr->platform_descriptor.VidMinLimit = 0;
	hwmgr->platform_descriptor.VidMaxLimit = 1500000;
	hwmgr->platform_descriptor.VidStep = 6250;

	disable_power_control = 0;
	if (!disable_power_control) {
		/* enable TDP overdrive (PowerControl) feature as well if supported */
		if (hwmgr->platform_descriptor.TDPODLimit)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerControl);
	}

	if (powerplay_table->usVddcLookupTableOffset) {
		const ATOM_Vega10_Voltage_Lookup_Table *vddc_table =
				(ATOM_Vega10_Voltage_Lookup_Table *)
				(((unsigned long)powerplay_table) +
				le16_to_cpu(powerplay_table->usVddcLookupTableOffset));
		result = get_vddc_lookup_table(hwmgr,
				&pp_table_info->vddc_lookup_table, vddc_table, 8);
	}

	if (powerplay_table->usVddmemLookupTableOffset) {
		const ATOM_Vega10_Voltage_Lookup_Table *vdd_mem_table =
				(ATOM_Vega10_Voltage_Lookup_Table *)
				(((unsigned long)powerplay_table) +
				le16_to_cpu(powerplay_table->usVddmemLookupTableOffset));
		result = get_vddc_lookup_table(hwmgr,
				&pp_table_info->vddmem_lookup_table, vdd_mem_table, 4);
	}

	if (powerplay_table->usVddciLookupTableOffset) {
		const ATOM_Vega10_Voltage_Lookup_Table *vddci_table =
				(ATOM_Vega10_Voltage_Lookup_Table *)
				(((unsigned long)powerplay_table) +
				le16_to_cpu(powerplay_table->usVddciLookupTableOffset));
		result = get_vddc_lookup_table(hwmgr,
				&pp_table_info->vddci_lookup_table, vddci_table, 4);
	}

	return result;
}

int vega10_pp_tables_initialize(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	const ATOM_Vega10_POWERPLAYTABLE *powerplay_table;

	hwmgr->pptable = kzalloc(sizeof(struct phm_ppt_v2_information), GFP_KERNEL);

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

	result = init_thermal_controller(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_thermal_controller failed", return result);

	result = init_over_drive_limits(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_over_drive_limits failed", return result);

	result = init_powerplay_extended_tables(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_powerplay_extended_tables failed", return result);

	result = init_dpm_2_parameters(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_dpm_2_parameters failed", return result);

	return result;
}

static int vega10_pp_tables_uninitialize(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v2_information *pp_table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);

	kfree(pp_table_info->vdd_dep_on_sclk);
	pp_table_info->vdd_dep_on_sclk = NULL;

	kfree(pp_table_info->vdd_dep_on_mclk);
	pp_table_info->vdd_dep_on_mclk = NULL;

	kfree(pp_table_info->valid_mclk_values);
	pp_table_info->valid_mclk_values = NULL;

	kfree(pp_table_info->valid_sclk_values);
	pp_table_info->valid_sclk_values = NULL;

	kfree(pp_table_info->vddc_lookup_table);
	pp_table_info->vddc_lookup_table = NULL;

	kfree(pp_table_info->vddmem_lookup_table);
	pp_table_info->vddmem_lookup_table = NULL;

	kfree(pp_table_info->vddci_lookup_table);
	pp_table_info->vddci_lookup_table = NULL;

	kfree(pp_table_info->ppm_parameter_table);
	pp_table_info->ppm_parameter_table = NULL;

	kfree(pp_table_info->mm_dep_table);
	pp_table_info->mm_dep_table = NULL;

	kfree(pp_table_info->cac_dtp_table);
	pp_table_info->cac_dtp_table = NULL;

	kfree(hwmgr->dyn_state.cac_dtp_table);
	hwmgr->dyn_state.cac_dtp_table = NULL;

	kfree(pp_table_info->tdp_table);
	pp_table_info->tdp_table = NULL;

	kfree(hwmgr->pptable);
	hwmgr->pptable = NULL;

	return 0;
}

const struct pp_table_func vega10_pptable_funcs = {
	.pptable_init = vega10_pp_tables_initialize,
	.pptable_fini = vega10_pp_tables_uninitialize,
};

int vega10_get_number_of_powerplay_table_entries(struct pp_hwmgr *hwmgr)
{
	const ATOM_Vega10_State_Array *state_arrays;
	const ATOM_Vega10_POWERPLAYTABLE *pp_table = get_powerplay_table(hwmgr);

	PP_ASSERT_WITH_CODE((pp_table != NULL),
			"Missing PowerPlay Table!", return -1);
	PP_ASSERT_WITH_CODE((pp_table->sHeader.format_revision >=
			ATOM_Vega10_TABLE_REVISION_VEGA10),
			"Incorrect PowerPlay table revision!", return -1);

	state_arrays = (ATOM_Vega10_State_Array *)(((unsigned long)pp_table) +
			le16_to_cpu(pp_table->usStateArrayOffset));

	return (uint32_t)(state_arrays->ucNumEntries);
}

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

int vega10_get_powerplay_table_entry(struct pp_hwmgr *hwmgr,
		uint32_t entry_index, struct pp_power_state *power_state,
		int (*call_back_func)(struct pp_hwmgr *, void *,
				struct pp_power_state *, void *, uint32_t))
{
	int result = 0;
	const ATOM_Vega10_State_Array *state_arrays;
	const ATOM_Vega10_State *state_entry;
	const ATOM_Vega10_POWERPLAYTABLE *pp_table =
			get_powerplay_table(hwmgr);

	PP_ASSERT_WITH_CODE(pp_table, "Missing PowerPlay Table!",
			return -1;);
	power_state->classification.bios_index = entry_index;

	if (pp_table->sHeader.format_revision >=
			ATOM_Vega10_TABLE_REVISION_VEGA10) {
		state_arrays = (ATOM_Vega10_State_Array *)
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

int vega10_baco_set_cap(struct pp_hwmgr *hwmgr)
{
	int result = 0;

	const ATOM_Vega10_POWERPLAYTABLE *powerplay_table;

	powerplay_table = get_powerplay_table(hwmgr);

	PP_ASSERT_WITH_CODE((powerplay_table != NULL),
		"Missing PowerPlay Table!", return -1);

	result = check_powerplay_tables(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "check_powerplay_tables failed", return result);

	set_hw_cap(
			hwmgr,
			0 != (le32_to_cpu(powerplay_table->ulPlatformCaps) & ATOM_VEGA10_PP_PLATFORM_CAP_BACO),
			PHM_PlatformCaps_BACO);
	return result;
}

