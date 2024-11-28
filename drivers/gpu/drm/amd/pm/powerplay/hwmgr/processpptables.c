/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include <drm/amdgpu_drm.h>
#include "processpptables.h"
#include <atom-types.h>
#include <atombios.h>
#include "pptable.h"
#include "power_state.h"
#include "hwmgr.h"
#include "hardwaremanager.h"


#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V2 12
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V3 14
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V4 16
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V5 18
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V6 20
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V7 22
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V8 24
#define SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V9 26

#define NUM_BITS_CLOCK_INFO_ARRAY_INDEX 6

static uint16_t get_vce_table_offset(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t vce_table_offset = 0;

	if (le16_to_cpu(powerplay_table->usTableSize) >=
	   sizeof(ATOM_PPLIB_POWERPLAYTABLE3)) {
		const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3 =
			(const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;

		if (powerplay_table3->usExtendendedHeaderOffset > 0) {
			const ATOM_PPLIB_EXTENDEDHEADER  *extended_header =
						(const ATOM_PPLIB_EXTENDEDHEADER *)
						(((unsigned long)powerplay_table3) +
						le16_to_cpu(powerplay_table3->usExtendendedHeaderOffset));
			if (le16_to_cpu(extended_header->usSize) >=
			   SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V2)
				vce_table_offset = le16_to_cpu(extended_header->usVCETableOffset);
		}
	}

	return vce_table_offset;
}

static uint16_t get_vce_clock_info_array_offset(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_vce_table_offset(hwmgr,
						powerplay_table);

	if (table_offset > 0)
		return table_offset + 1;

	return 0;
}

static uint16_t get_vce_clock_info_array_size(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_vce_clock_info_array_offset(hwmgr,
							powerplay_table);
	uint16_t table_size = 0;

	if (table_offset > 0) {
		const VCEClockInfoArray *p = (const VCEClockInfoArray *)
			(((unsigned long) powerplay_table) + table_offset);
		table_size = sizeof(uint8_t) + p->ucNumEntries * sizeof(VCEClockInfo);
	}

	return table_size;
}

static uint16_t get_vce_clock_voltage_limit_table_offset(struct pp_hwmgr *hwmgr,
				const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_vce_clock_info_array_offset(hwmgr,
							powerplay_table);

	if (table_offset > 0)
		return table_offset + get_vce_clock_info_array_size(hwmgr,
							powerplay_table);

	return 0;
}

static uint16_t get_vce_clock_voltage_limit_table_size(struct pp_hwmgr *hwmgr,
							const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_vce_clock_voltage_limit_table_offset(hwmgr, powerplay_table);
	uint16_t table_size = 0;

	if (table_offset > 0) {
		const ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *ptable =
			(const ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *)(((unsigned long) powerplay_table) + table_offset);

		table_size = sizeof(uint8_t) + ptable->numEntries * sizeof(ATOM_PPLIB_VCE_Clock_Voltage_Limit_Record);
	}
	return table_size;
}

static uint16_t get_vce_state_table_offset(struct pp_hwmgr *hwmgr, const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_vce_clock_voltage_limit_table_offset(hwmgr, powerplay_table);

	if (table_offset > 0)
		return table_offset + get_vce_clock_voltage_limit_table_size(hwmgr, powerplay_table);

	return 0;
}

static const ATOM_PPLIB_VCE_State_Table *get_vce_state_table(
						struct pp_hwmgr *hwmgr,
						const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_vce_state_table_offset(hwmgr, powerplay_table);

	if (table_offset > 0)
		return (const ATOM_PPLIB_VCE_State_Table *)(((unsigned long) powerplay_table) + table_offset);

	return NULL;
}

static uint16_t get_uvd_table_offset(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t uvd_table_offset = 0;

	if (le16_to_cpu(powerplay_table->usTableSize) >=
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE3)) {
		const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3 =
			(const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;
		if (powerplay_table3->usExtendendedHeaderOffset > 0) {
			const ATOM_PPLIB_EXTENDEDHEADER  *extended_header =
					(const ATOM_PPLIB_EXTENDEDHEADER *)
					(((unsigned long)powerplay_table3) +
				le16_to_cpu(powerplay_table3->usExtendendedHeaderOffset));
			if (le16_to_cpu(extended_header->usSize) >=
			    SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V3)
				uvd_table_offset = le16_to_cpu(extended_header->usUVDTableOffset);
		}
	}
	return uvd_table_offset;
}

static uint16_t get_uvd_clock_info_array_offset(struct pp_hwmgr *hwmgr,
			 const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_uvd_table_offset(hwmgr,
						    powerplay_table);

	if (table_offset > 0)
		return table_offset + 1;
	return 0;
}

static uint16_t get_uvd_clock_info_array_size(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_uvd_clock_info_array_offset(hwmgr,
						    powerplay_table);
	uint16_t table_size = 0;

	if (table_offset > 0) {
		const UVDClockInfoArray *p = (const UVDClockInfoArray *)
					(((unsigned long) powerplay_table)
					+ table_offset);
		table_size = sizeof(UCHAR) +
			     p->ucNumEntries * sizeof(UVDClockInfo);
	}

	return table_size;
}

static uint16_t get_uvd_clock_voltage_limit_table_offset(
			struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_uvd_clock_info_array_offset(hwmgr,
						     powerplay_table);

	if (table_offset > 0)
		return table_offset +
			get_uvd_clock_info_array_size(hwmgr, powerplay_table);

	return 0;
}

static uint16_t get_samu_table_offset(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t samu_table_offset = 0;

	if (le16_to_cpu(powerplay_table->usTableSize) >=
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE3)) {
		const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3 =
			(const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;
		if (powerplay_table3->usExtendendedHeaderOffset > 0) {
			const ATOM_PPLIB_EXTENDEDHEADER  *extended_header =
				(const ATOM_PPLIB_EXTENDEDHEADER *)
				(((unsigned long)powerplay_table3) +
				le16_to_cpu(powerplay_table3->usExtendendedHeaderOffset));
			if (le16_to_cpu(extended_header->usSize) >=
			    SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V4)
				samu_table_offset = le16_to_cpu(extended_header->usSAMUTableOffset);
		}
	}

	return samu_table_offset;
}

static uint16_t get_samu_clock_voltage_limit_table_offset(
			struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t table_offset = get_samu_table_offset(hwmgr,
					    powerplay_table);

	if (table_offset > 0)
		return table_offset + 1;

	return 0;
}

static uint16_t get_acp_table_offset(struct pp_hwmgr *hwmgr,
				const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t acp_table_offset = 0;

	if (le16_to_cpu(powerplay_table->usTableSize) >=
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE3)) {
		const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3 =
			(const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;
		if (powerplay_table3->usExtendendedHeaderOffset > 0) {
			const ATOM_PPLIB_EXTENDEDHEADER  *pExtendedHeader =
				(const ATOM_PPLIB_EXTENDEDHEADER *)
				(((unsigned long)powerplay_table3) +
				le16_to_cpu(powerplay_table3->usExtendendedHeaderOffset));
			if (le16_to_cpu(pExtendedHeader->usSize) >=
			    SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V6)
				acp_table_offset = le16_to_cpu(pExtendedHeader->usACPTableOffset);
		}
	}

	return acp_table_offset;
}

static uint16_t get_acp_clock_voltage_limit_table_offset(
				struct pp_hwmgr *hwmgr,
				const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t tableOffset = get_acp_table_offset(hwmgr, powerplay_table);

	if (tableOffset > 0)
		return tableOffset + 1;

	return 0;
}

static uint16_t get_cacp_tdp_table_offset(
				struct pp_hwmgr *hwmgr,
				const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t cacTdpTableOffset = 0;

	if (le16_to_cpu(powerplay_table->usTableSize) >=
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE3)) {
		const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3 =
				(const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;
		if (powerplay_table3->usExtendendedHeaderOffset > 0) {
			const ATOM_PPLIB_EXTENDEDHEADER  *pExtendedHeader =
					(const ATOM_PPLIB_EXTENDEDHEADER *)
					(((unsigned long)powerplay_table3) +
				le16_to_cpu(powerplay_table3->usExtendendedHeaderOffset));
			if (le16_to_cpu(pExtendedHeader->usSize) >=
			    SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V7)
				cacTdpTableOffset = le16_to_cpu(pExtendedHeader->usPowerTuneTableOffset);
		}
	}

	return cacTdpTableOffset;
}

static int get_cac_tdp_table(struct pp_hwmgr *hwmgr,
				struct phm_cac_tdp_table **ptable,
				const ATOM_PowerTune_Table *table,
				uint16_t us_maximum_power_delivery_limit)
{
	unsigned long table_size;
	struct phm_cac_tdp_table *tdp_table;

	table_size = sizeof(unsigned long) + sizeof(struct phm_cac_tdp_table);

	tdp_table = kzalloc(table_size, GFP_KERNEL);
	if (NULL == tdp_table)
		return -ENOMEM;

	tdp_table->usTDP = le16_to_cpu(table->usTDP);
	tdp_table->usConfigurableTDP = le16_to_cpu(table->usConfigurableTDP);
	tdp_table->usTDC = le16_to_cpu(table->usTDC);
	tdp_table->usBatteryPowerLimit = le16_to_cpu(table->usBatteryPowerLimit);
	tdp_table->usSmallPowerLimit = le16_to_cpu(table->usSmallPowerLimit);
	tdp_table->usLowCACLeakage = le16_to_cpu(table->usLowCACLeakage);
	tdp_table->usHighCACLeakage = le16_to_cpu(table->usHighCACLeakage);
	tdp_table->usMaximumPowerDeliveryLimit = us_maximum_power_delivery_limit;

	*ptable = tdp_table;

	return 0;
}

static uint16_t get_sclk_vdd_gfx_table_offset(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t sclk_vdd_gfx_table_offset = 0;

	if (le16_to_cpu(powerplay_table->usTableSize) >=
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE3)) {
		const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3 =
				(const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;
		if (powerplay_table3->usExtendendedHeaderOffset > 0) {
			const ATOM_PPLIB_EXTENDEDHEADER  *pExtendedHeader =
				(const ATOM_PPLIB_EXTENDEDHEADER *)
				(((unsigned long)powerplay_table3) +
				le16_to_cpu(powerplay_table3->usExtendendedHeaderOffset));
			if (le16_to_cpu(pExtendedHeader->usSize) >=
			    SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V8)
				sclk_vdd_gfx_table_offset =
					le16_to_cpu(pExtendedHeader->usSclkVddgfxTableOffset);
		}
	}

	return sclk_vdd_gfx_table_offset;
}

static uint16_t get_sclk_vdd_gfx_clock_voltage_dependency_table_offset(
			struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	uint16_t tableOffset = get_sclk_vdd_gfx_table_offset(hwmgr, powerplay_table);

	if (tableOffset > 0)
		return tableOffset;

	return 0;
}


static int get_clock_voltage_dependency_table(struct pp_hwmgr *hwmgr,
		struct phm_clock_voltage_dependency_table **ptable,
		const ATOM_PPLIB_Clock_Voltage_Dependency_Table *table)
{

	unsigned long i;
	struct phm_clock_voltage_dependency_table *dep_table;

	dep_table = kzalloc(struct_size(dep_table, entries, table->ucNumEntries),
			    GFP_KERNEL);
	if (NULL == dep_table)
		return -ENOMEM;

	dep_table->count = (unsigned long)table->ucNumEntries;

	for (i = 0; i < dep_table->count; i++) {
		dep_table->entries[i].clk =
			((unsigned long)table->entries[i].ucClockHigh << 16) |
			le16_to_cpu(table->entries[i].usClockLow);
		dep_table->entries[i].v =
			(unsigned long)le16_to_cpu(table->entries[i].usVoltage);
	}

	*ptable = dep_table;

	return 0;
}

static int get_valid_clk(struct pp_hwmgr *hwmgr,
			struct phm_clock_array **ptable,
			const struct phm_clock_voltage_dependency_table *table)
{
	unsigned long i;
	struct phm_clock_array *clock_table;

	clock_table = kzalloc(struct_size(clock_table, values, table->count), GFP_KERNEL);
	if (!clock_table)
		return -ENOMEM;

	clock_table->count = (unsigned long)table->count;

	for (i = 0; i < clock_table->count; i++)
		clock_table->values[i] = (unsigned long)table->entries[i].clk;

	*ptable = clock_table;

	return 0;
}

static int get_clock_voltage_limit(struct pp_hwmgr *hwmgr,
			struct phm_clock_and_voltage_limits *limits,
			const ATOM_PPLIB_Clock_Voltage_Limit_Table *table)
{
	limits->sclk = ((unsigned long)table->entries[0].ucSclkHigh << 16) |
			le16_to_cpu(table->entries[0].usSclkLow);
	limits->mclk = ((unsigned long)table->entries[0].ucMclkHigh << 16) |
			le16_to_cpu(table->entries[0].usMclkLow);
	limits->vddc = (unsigned long)le16_to_cpu(table->entries[0].usVddc);
	limits->vddci = (unsigned long)le16_to_cpu(table->entries[0].usVddci);

	return 0;
}


static void set_hw_cap(struct pp_hwmgr *hwmgr, bool enable,
		       enum phm_platform_caps cap)
{
	if (enable)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, cap);
	else
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps, cap);
}

static int set_platform_caps(struct pp_hwmgr *hwmgr,
			unsigned long powerplay_caps)
{
	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_POWERPLAY),
		PHM_PlatformCaps_PowerPlaySupport
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_SBIOSPOWERSOURCE),
		PHM_PlatformCaps_BiosPowerSourceControl
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_ASPM_L0s),
		PHM_PlatformCaps_EnableASPML0s
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_ASPM_L1),
		PHM_PlatformCaps_EnableASPML1
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_BACKBIAS),
		PHM_PlatformCaps_EnableBackbias
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_HARDWAREDC),
		PHM_PlatformCaps_AutomaticDCTransition
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_GEMINIPRIMARY),
		PHM_PlatformCaps_GeminiPrimary
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_STEPVDDC),
		PHM_PlatformCaps_StepVddc
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_VOLTAGECONTROL),
		PHM_PlatformCaps_EnableVoltageControl
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_SIDEPORTCONTROL),
		PHM_PlatformCaps_EnableSideportControl
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_TURNOFFPLL_ASPML1),
		PHM_PlatformCaps_TurnOffPll_ASPML1
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_HTLINKCONTROL),
		PHM_PlatformCaps_EnableHTLinkControl
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_MVDDCONTROL),
		PHM_PlatformCaps_EnableMVDDControl
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_VDDCI_CONTROL),
		PHM_PlatformCaps_ControlVDDCI
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_REGULATOR_HOT),
		PHM_PlatformCaps_RegulatorHot
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_GOTO_BOOT_ON_ALERT),
		PHM_PlatformCaps_BootStateOnAlert
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_DONT_WAIT_FOR_VBLANK_ON_ALERT),
		PHM_PlatformCaps_DontWaitForVBlankOnAlert
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_BACO),
		PHM_PlatformCaps_BACO
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_NEW_CAC_VOLTAGE),
		PHM_PlatformCaps_NewCACVoltage
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_REVERT_GPIO5_POLARITY),
		PHM_PlatformCaps_RevertGPIO5Polarity
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_OUTPUT_THERMAL2GPIO17),
		PHM_PlatformCaps_Thermal2GPIO17
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_VRHOT_GPIO_CONFIGURABLE),
		PHM_PlatformCaps_VRHotGPIOConfigurable
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_TEMP_INVERSION),
		PHM_PlatformCaps_TempInversion
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_EVV),
		PHM_PlatformCaps_EVV
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_COMBINE_PCC_WITH_THERMAL_SIGNAL),
		PHM_PlatformCaps_CombinePCCWithThermalSignal
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_LOAD_POST_PRODUCTION_FIRMWARE),
		PHM_PlatformCaps_LoadPostProductionFirmware
	);

	set_hw_cap(
		hwmgr,
		0 != (powerplay_caps & ATOM_PP_PLATFORM_CAP_DISABLE_USING_ACTUAL_TEMPERATURE_FOR_POWER_CALC),
		PHM_PlatformCaps_DisableUsingActualTemperatureForPowerCalc
	);

	return 0;
}

static PP_StateClassificationFlags make_classification_flags(
						   struct pp_hwmgr *hwmgr,
						    USHORT classification,
						   USHORT classification2)
{
	PP_StateClassificationFlags result = 0;

	if (classification & ATOM_PPLIB_CLASSIFICATION_BOOT)
		result |= PP_StateClassificationFlag_Boot;

	if (classification & ATOM_PPLIB_CLASSIFICATION_THERMAL)
		result |= PP_StateClassificationFlag_Thermal;

	if (classification &
			ATOM_PPLIB_CLASSIFICATION_LIMITEDPOWERSOURCE)
		result |= PP_StateClassificationFlag_LimitedPowerSource;

	if (classification & ATOM_PPLIB_CLASSIFICATION_REST)
		result |= PP_StateClassificationFlag_Rest;

	if (classification & ATOM_PPLIB_CLASSIFICATION_FORCED)
		result |= PP_StateClassificationFlag_Forced;

	if (classification & ATOM_PPLIB_CLASSIFICATION_3DPERFORMANCE)
		result |= PP_StateClassificationFlag_3DPerformance;


	if (classification & ATOM_PPLIB_CLASSIFICATION_OVERDRIVETEMPLATE)
		result |= PP_StateClassificationFlag_ACOverdriveTemplate;

	if (classification & ATOM_PPLIB_CLASSIFICATION_UVDSTATE)
		result |= PP_StateClassificationFlag_Uvd;

	if (classification & ATOM_PPLIB_CLASSIFICATION_HDSTATE)
		result |= PP_StateClassificationFlag_UvdHD;

	if (classification & ATOM_PPLIB_CLASSIFICATION_SDSTATE)
		result |= PP_StateClassificationFlag_UvdSD;

	if (classification & ATOM_PPLIB_CLASSIFICATION_HD2STATE)
		result |= PP_StateClassificationFlag_HD2;

	if (classification & ATOM_PPLIB_CLASSIFICATION_ACPI)
		result |= PP_StateClassificationFlag_ACPI;

	if (classification2 & ATOM_PPLIB_CLASSIFICATION2_LIMITEDPOWERSOURCE_2)
		result |= PP_StateClassificationFlag_LimitedPowerSource_2;


	if (classification2 & ATOM_PPLIB_CLASSIFICATION2_ULV)
		result |= PP_StateClassificationFlag_ULV;

	if (classification2 & ATOM_PPLIB_CLASSIFICATION2_MVC)
		result |= PP_StateClassificationFlag_UvdMVC;

	return result;
}

static int init_non_clock_fields(struct pp_hwmgr *hwmgr,
						struct pp_power_state *ps,
							    uint8_t version,
			 const ATOM_PPLIB_NONCLOCK_INFO *pnon_clock_info) {
	unsigned long rrr_index;
	unsigned long tmp;

	ps->classification.ui_label = (le16_to_cpu(pnon_clock_info->usClassification) &
					ATOM_PPLIB_CLASSIFICATION_UI_MASK) >> ATOM_PPLIB_CLASSIFICATION_UI_SHIFT;
	ps->classification.flags = make_classification_flags(hwmgr,
				le16_to_cpu(pnon_clock_info->usClassification),
				le16_to_cpu(pnon_clock_info->usClassification2));

	ps->classification.temporary_state = false;
	ps->classification.to_be_deleted = false;
	tmp = le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
		ATOM_PPLIB_SINGLE_DISPLAY_ONLY;

	ps->validation.singleDisplayOnly = (0 != tmp);

	tmp = le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
		ATOM_PPLIB_DISALLOW_ON_DC;

	ps->validation.disallowOnDC = (0 != tmp);

	ps->pcie.lanes = ((le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
				ATOM_PPLIB_PCIE_LINK_WIDTH_MASK) >>
				ATOM_PPLIB_PCIE_LINK_WIDTH_SHIFT) + 1;

	ps->pcie.lanes = 0;

	ps->display.disableFrameModulation = false;

	rrr_index = (le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
			ATOM_PPLIB_LIMITED_REFRESHRATE_VALUE_MASK) >>
			ATOM_PPLIB_LIMITED_REFRESHRATE_VALUE_SHIFT;

	if (rrr_index != ATOM_PPLIB_LIMITED_REFRESHRATE_UNLIMITED) {
		static const uint8_t look_up[(ATOM_PPLIB_LIMITED_REFRESHRATE_VALUE_MASK >> ATOM_PPLIB_LIMITED_REFRESHRATE_VALUE_SHIFT) + 1] = \
								{ 0, 50, 0 };

		ps->display.refreshrateSource = PP_RefreshrateSource_Explicit;
		ps->display.explicitRefreshrate = look_up[rrr_index];
		ps->display.limitRefreshrate = true;

		if (ps->display.explicitRefreshrate == 0)
			ps->display.limitRefreshrate = false;
	} else
		ps->display.limitRefreshrate = false;

	tmp = le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
		ATOM_PPLIB_ENABLE_VARIBRIGHT;

	ps->display.enableVariBright = (0 != tmp);

	tmp = le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
		ATOM_PPLIB_SWSTATE_MEMORY_DLL_OFF;

	ps->memory.dllOff = (0 != tmp);

	ps->memory.m3arb = (le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
			    ATOM_PPLIB_M3ARB_MASK) >> ATOM_PPLIB_M3ARB_SHIFT;

	ps->temperatures.min = PP_TEMPERATURE_UNITS_PER_CENTIGRADES *
				     pnon_clock_info->ucMinTemperature;

	ps->temperatures.max = PP_TEMPERATURE_UNITS_PER_CENTIGRADES *
				     pnon_clock_info->ucMaxTemperature;

	tmp = le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
		ATOM_PPLIB_SOFTWARE_DISABLE_LOADBALANCING;

	ps->software.disableLoadBalancing = tmp;

	tmp = le32_to_cpu(pnon_clock_info->ulCapsAndSettings) &
		ATOM_PPLIB_SOFTWARE_ENABLE_SLEEP_FOR_TIMESTAMPS;

	ps->software.enableSleepForTimestamps = (0 != tmp);

	ps->validation.supportedPowerLevels = pnon_clock_info->ucRequiredPower;

	if (ATOM_PPLIB_NONCLOCKINFO_VER1 < version) {
		ps->uvd_clocks.VCLK = le32_to_cpu(pnon_clock_info->ulVCLK);
		ps->uvd_clocks.DCLK = le32_to_cpu(pnon_clock_info->ulDCLK);
	} else {
		ps->uvd_clocks.VCLK = 0;
		ps->uvd_clocks.DCLK = 0;
	}

	return 0;
}

static ULONG size_of_entry_v2(ULONG num_dpm_levels)
{
	return (sizeof(UCHAR) + sizeof(UCHAR) +
			(num_dpm_levels * sizeof(UCHAR)));
}

static const ATOM_PPLIB_STATE_V2 *get_state_entry_v2(
					const StateArray * pstate_arrays,
							 ULONG entry_index)
{
	ULONG i;
	const ATOM_PPLIB_STATE_V2 *pstate;

	pstate = pstate_arrays->states;
	if (entry_index <= pstate_arrays->ucNumEntries) {
		for (i = 0; i < entry_index; i++)
			pstate = (ATOM_PPLIB_STATE_V2 *)(
						  (unsigned long)pstate +
			     size_of_entry_v2(pstate->ucNumDPMLevels));
	}
	return pstate;
}

static const unsigned char soft_dummy_pp_table[] = {
	0xe1, 0x01, 0x06, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x00, 0x4a, 0x00, 0x6c, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x42, 0x00, 0x02, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x4e, 0x00, 0x88, 0x00, 0x00, 0x9e, 0x00, 0x17, 0x00, 0x00, 0x00, 0x9e, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00,
	0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x18, 0x05, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1a, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe1, 0x00, 0x43, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x8e, 0x01, 0x00, 0x00, 0xb8, 0x01, 0x00, 0x00, 0x08, 0x30, 0x75, 0x00, 0x80, 0x00, 0xa0, 0x8c,
	0x00, 0x7e, 0x00, 0x71, 0xa5, 0x00, 0x7c, 0x00, 0xe5, 0xc8, 0x00, 0x70, 0x00, 0x91, 0xf4, 0x00,
	0x64, 0x00, 0x40, 0x19, 0x01, 0x5a, 0x00, 0x0e, 0x28, 0x01, 0x52, 0x00, 0x80, 0x38, 0x01, 0x4a,
	0x00, 0x00, 0x09, 0x30, 0x75, 0x00, 0x30, 0x75, 0x00, 0x40, 0x9c, 0x00, 0x40, 0x9c, 0x00, 0x59,
	0xd8, 0x00, 0x59, 0xd8, 0x00, 0x91, 0xf4, 0x00, 0x91, 0xf4, 0x00, 0x0e, 0x28, 0x01, 0x0e, 0x28,
	0x01, 0x90, 0x5f, 0x01, 0x90, 0x5f, 0x01, 0x00, 0x77, 0x01, 0x00, 0x77, 0x01, 0xca, 0x91, 0x01,
	0xca, 0x91, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x80, 0x00, 0x00, 0x7e, 0x00, 0x01,
	0x7c, 0x00, 0x02, 0x70, 0x00, 0x03, 0x64, 0x00, 0x04, 0x5a, 0x00, 0x05, 0x52, 0x00, 0x06, 0x4a,
	0x00, 0x07, 0x08, 0x08, 0x00, 0x08, 0x00, 0x01, 0x02, 0x02, 0x02, 0x01, 0x02, 0x02, 0x02, 0x03,
	0x02, 0x04, 0x02, 0x00, 0x08, 0x40, 0x9c, 0x00, 0x30, 0x75, 0x00, 0x74, 0xb5, 0x00, 0xa0, 0x8c,
	0x00, 0x60, 0xea, 0x00, 0x74, 0xb5, 0x00, 0x0e, 0x28, 0x01, 0x60, 0xea, 0x00, 0x90, 0x5f, 0x01,
	0x40, 0x19, 0x01, 0xb2, 0xb0, 0x01, 0x90, 0x5f, 0x01, 0xc0, 0xd4, 0x01, 0x00, 0x77, 0x01, 0x5e,
	0xff, 0x01, 0xca, 0x91, 0x01, 0x08, 0x80, 0x00, 0x00, 0x7e, 0x00, 0x01, 0x7c, 0x00, 0x02, 0x70,
	0x00, 0x03, 0x64, 0x00, 0x04, 0x5a, 0x00, 0x05, 0x52, 0x00, 0x06, 0x4a, 0x00, 0x07, 0x00, 0x08,
	0x80, 0x00, 0x30, 0x75, 0x00, 0x7e, 0x00, 0x40, 0x9c, 0x00, 0x7c, 0x00, 0x59, 0xd8, 0x00, 0x70,
	0x00, 0xdc, 0x0b, 0x01, 0x64, 0x00, 0x80, 0x38, 0x01, 0x5a, 0x00, 0x80, 0x38, 0x01, 0x52, 0x00,
	0x80, 0x38, 0x01, 0x4a, 0x00, 0x80, 0x38, 0x01, 0x08, 0x30, 0x75, 0x00, 0x80, 0x00, 0xa0, 0x8c,
	0x00, 0x7e, 0x00, 0x71, 0xa5, 0x00, 0x7c, 0x00, 0xe5, 0xc8, 0x00, 0x74, 0x00, 0x91, 0xf4, 0x00,
	0x66, 0x00, 0x40, 0x19, 0x01, 0x58, 0x00, 0x0e, 0x28, 0x01, 0x52, 0x00, 0x80, 0x38, 0x01, 0x4a,
	0x00
};

static const ATOM_PPLIB_POWERPLAYTABLE *get_powerplay_table(
				     struct pp_hwmgr *hwmgr)
{
	const void *table_addr = hwmgr->soft_pp_table;
	uint8_t frev, crev;
	uint16_t size;

	if (!table_addr) {
		if (hwmgr->chip_id == CHIP_RAVEN) {
			table_addr = &soft_dummy_pp_table[0];
			hwmgr->soft_pp_table = &soft_dummy_pp_table[0];
			hwmgr->soft_pp_table_size = sizeof(soft_dummy_pp_table);
		} else {
			table_addr = smu_atom_get_data_table(hwmgr->adev,
					GetIndexIntoMasterTable(DATA, PowerPlayInfo),
					&size, &frev, &crev);
			hwmgr->soft_pp_table = table_addr;
			hwmgr->soft_pp_table_size = size;
		}
	}

	return (const ATOM_PPLIB_POWERPLAYTABLE *)table_addr;
}

int pp_tables_get_response_times(struct pp_hwmgr *hwmgr,
				uint32_t *vol_rep_time, uint32_t *bb_rep_time)
{
	const ATOM_PPLIB_POWERPLAYTABLE *powerplay_tab = get_powerplay_table(hwmgr);

	PP_ASSERT_WITH_CODE(NULL != powerplay_tab,
			    "Missing PowerPlay Table!", return -EINVAL);

	*vol_rep_time = (uint32_t)le16_to_cpu(powerplay_tab->usVoltageTime);
	*bb_rep_time = (uint32_t)le16_to_cpu(powerplay_tab->usBackbiasTime);

	return 0;
}

int pp_tables_get_num_of_entries(struct pp_hwmgr *hwmgr,
				     unsigned long *num_of_entries)
{
	const StateArray *pstate_arrays;
	const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table = get_powerplay_table(hwmgr);

	if (powerplay_table == NULL)
		return -1;

	if (powerplay_table->sHeader.ucTableFormatRevision >= 6) {
		pstate_arrays = (StateArray *)(((unsigned long)powerplay_table) +
					le16_to_cpu(powerplay_table->usStateArrayOffset));

		*num_of_entries = (unsigned long)(pstate_arrays->ucNumEntries);
	} else
		*num_of_entries = (unsigned long)(powerplay_table->ucNumStates);

	return 0;
}

int pp_tables_get_entry(struct pp_hwmgr *hwmgr,
				unsigned long entry_index,
				struct pp_power_state *ps,
			 pp_tables_hw_clock_info_callback func)
{
	int i;
	const StateArray *pstate_arrays;
	const ATOM_PPLIB_STATE_V2 *pstate_entry_v2;
	const ATOM_PPLIB_NONCLOCK_INFO *pnon_clock_info;
	const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table = get_powerplay_table(hwmgr);
	int result = 0;
	int res = 0;

	const ClockInfoArray *pclock_arrays;

	const NonClockInfoArray *pnon_clock_arrays;

	const ATOM_PPLIB_STATE *pstate_entry;

	if (powerplay_table == NULL)
		return -1;

	ps->classification.bios_index = entry_index;

	if (powerplay_table->sHeader.ucTableFormatRevision >= 6) {
		pstate_arrays = (StateArray *)(((unsigned long)powerplay_table) +
					le16_to_cpu(powerplay_table->usStateArrayOffset));

		if (entry_index > pstate_arrays->ucNumEntries)
			return -1;

		pstate_entry_v2 = get_state_entry_v2(pstate_arrays, entry_index);
		pclock_arrays = (ClockInfoArray *)(((unsigned long)powerplay_table) +
					le16_to_cpu(powerplay_table->usClockInfoArrayOffset));

		pnon_clock_arrays = (NonClockInfoArray *)(((unsigned long)powerplay_table) +
						le16_to_cpu(powerplay_table->usNonClockInfoArrayOffset));

		pnon_clock_info = (ATOM_PPLIB_NONCLOCK_INFO *)((unsigned long)(pnon_clock_arrays->nonClockInfo) +
					(pstate_entry_v2->nonClockInfoIndex * pnon_clock_arrays->ucEntrySize));

		result = init_non_clock_fields(hwmgr, ps, pnon_clock_arrays->ucEntrySize, pnon_clock_info);

		for (i = 0; i < pstate_entry_v2->ucNumDPMLevels; i++) {
			const void *pclock_info = (const void *)(
							(unsigned long)(pclock_arrays->clockInfo) +
							(pstate_entry_v2->clockInfoIndex[i] * pclock_arrays->ucEntrySize));
			res = func(hwmgr, &ps->hardware, i, pclock_info);
			if ((0 == result) && (0 != res))
				result = res;
		}
	} else {
		if (entry_index > powerplay_table->ucNumStates)
			return -1;

		pstate_entry = (ATOM_PPLIB_STATE *)((unsigned long)powerplay_table +
						    le16_to_cpu(powerplay_table->usStateArrayOffset) +
						    entry_index * powerplay_table->ucStateEntrySize);

		pnon_clock_info = (ATOM_PPLIB_NONCLOCK_INFO *)((unsigned long)powerplay_table +
						le16_to_cpu(powerplay_table->usNonClockInfoArrayOffset) +
						pstate_entry->ucNonClockStateIndex *
						powerplay_table->ucNonClockSize);

		result = init_non_clock_fields(hwmgr, ps,
							powerplay_table->ucNonClockSize,
							pnon_clock_info);

		for (i = 0; i < powerplay_table->ucStateEntrySize-1; i++) {
			const void *pclock_info = (const void *)((unsigned long)powerplay_table +
						le16_to_cpu(powerplay_table->usClockInfoArrayOffset) +
						pstate_entry->ucClockStateIndices[i] *
						powerplay_table->ucClockInfoSize);

			int res = func(hwmgr, &ps->hardware, i, pclock_info);

			if ((0 == result) && (0 != res))
					result = res;
		}
	}

	if ((0 == result) && (0 != (ps->classification.flags & PP_StateClassificationFlag_Boot))) {
		if (hwmgr->chip_family < AMDGPU_FAMILY_RV)
			result = hwmgr->hwmgr_func->patch_boot_state(hwmgr, &(ps->hardware));
	}

	return result;
}

static int init_powerplay_tables(
			struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table
)
{
	return 0;
}


static int init_thermal_controller(
			struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	struct amdgpu_device *adev = hwmgr->adev;

	hwmgr->thermal_controller.ucType =
			powerplay_table->sThermalController.ucType;
	hwmgr->thermal_controller.ucI2cLine =
			powerplay_table->sThermalController.ucI2cLine;
	hwmgr->thermal_controller.ucI2cAddress =
			powerplay_table->sThermalController.ucI2cAddress;

	hwmgr->thermal_controller.fanInfo.bNoFan =
		(0 != (powerplay_table->sThermalController.ucFanParameters &
			ATOM_PP_FANPARAMETERS_NOFAN));

	hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution =
		powerplay_table->sThermalController.ucFanParameters &
		ATOM_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK;

	hwmgr->thermal_controller.fanInfo.ulMinRPM
		= powerplay_table->sThermalController.ucFanMinRPM * 100UL;
	hwmgr->thermal_controller.fanInfo.ulMaxRPM
		= powerplay_table->sThermalController.ucFanMaxRPM * 100UL;

	set_hw_cap(hwmgr,
		   ATOM_PP_THERMALCONTROLLER_NONE != hwmgr->thermal_controller.ucType,
		   PHM_PlatformCaps_ThermalController);

        if (powerplay_table->usTableSize >= sizeof(ATOM_PPLIB_POWERPLAYTABLE3)) {
		const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3 =
			(const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;

		if (0 == le16_to_cpu(powerplay_table3->usFanTableOffset)) {
			hwmgr->thermal_controller.use_hw_fan_control = 1;
			return 0;
		} else {
			const ATOM_PPLIB_FANTABLE *fan_table =
				(const ATOM_PPLIB_FANTABLE *)(((unsigned long)powerplay_table) +
							      le16_to_cpu(powerplay_table3->usFanTableOffset));

			if (1 <= fan_table->ucFanTableFormat) {
				hwmgr->thermal_controller.advanceFanControlParameters.ucTHyst =
					fan_table->ucTHyst;
				hwmgr->thermal_controller.advanceFanControlParameters.usTMin =
					le16_to_cpu(fan_table->usTMin);
				hwmgr->thermal_controller.advanceFanControlParameters.usTMed =
					le16_to_cpu(fan_table->usTMed);
				hwmgr->thermal_controller.advanceFanControlParameters.usTHigh =
					le16_to_cpu(fan_table->usTHigh);
				hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin =
					le16_to_cpu(fan_table->usPWMMin);
				hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed =
					le16_to_cpu(fan_table->usPWMMed);
				hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh =
					le16_to_cpu(fan_table->usPWMHigh);
				hwmgr->thermal_controller.advanceFanControlParameters.usTMax = 10900;
				hwmgr->thermal_controller.advanceFanControlParameters.ulCycleDelay = 100000;

				phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					    PHM_PlatformCaps_MicrocodeFanControl);
			}

			if (2 <= fan_table->ucFanTableFormat) {
				const ATOM_PPLIB_FANTABLE2 *fan_table2 =
					(const ATOM_PPLIB_FANTABLE2 *)(((unsigned long)powerplay_table) +
								       le16_to_cpu(powerplay_table3->usFanTableOffset));
				hwmgr->thermal_controller.advanceFanControlParameters.usTMax =
					le16_to_cpu(fan_table2->usTMax);
			}

			if (3 <= fan_table->ucFanTableFormat) {
				const ATOM_PPLIB_FANTABLE3 *fan_table3 =
					(const ATOM_PPLIB_FANTABLE3 *) (((unsigned long)powerplay_table) +
									le16_to_cpu(powerplay_table3->usFanTableOffset));

				hwmgr->thermal_controller.advanceFanControlParameters.ucFanControlMode =
					fan_table3->ucFanControlMode;

				if ((3 == fan_table->ucFanTableFormat) &&
				    (0x67B1 == adev->pdev->device))
					hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanPWM =
						47;
				else
					hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanPWM =
						le16_to_cpu(fan_table3->usFanPWMMax);

				hwmgr->thermal_controller.advanceFanControlParameters.usDefaultFanOutputSensitivity =
					4836;
				hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity =
					le16_to_cpu(fan_table3->usFanOutputSensitivity);
			}

			if (6 <= fan_table->ucFanTableFormat) {
				const ATOM_PPLIB_FANTABLE4 *fan_table4 =
					(const ATOM_PPLIB_FANTABLE4 *)(((unsigned long)powerplay_table) +
								       le16_to_cpu(powerplay_table3->usFanTableOffset));

				phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					    PHM_PlatformCaps_FanSpeedInTableIsRPM);

				hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanRPM =
					le16_to_cpu(fan_table4->usFanRPMMax);
			}

			if (7 <= fan_table->ucFanTableFormat) {
				const ATOM_PPLIB_FANTABLE5 *fan_table5 =
					(const ATOM_PPLIB_FANTABLE5 *)(((unsigned long)powerplay_table) +
								       le16_to_cpu(powerplay_table3->usFanTableOffset));

				if (0x67A2 == adev->pdev->device ||
				    0x67A9 == adev->pdev->device ||
				    0x67B9 == adev->pdev->device) {
					phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						    PHM_PlatformCaps_GeminiRegulatorFanControlSupport);
					hwmgr->thermal_controller.advanceFanControlParameters.usFanCurrentLow =
						le16_to_cpu(fan_table5->usFanCurrentLow);
					hwmgr->thermal_controller.advanceFanControlParameters.usFanCurrentHigh =
						le16_to_cpu(fan_table5->usFanCurrentHigh);
					hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMLow =
						le16_to_cpu(fan_table5->usFanRPMLow);
					hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMHigh =
						le16_to_cpu(fan_table5->usFanRPMHigh);
				}
			}
		}
	}

	return 0;
}

static int init_overdrive_limits_V1_4(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table,
			const ATOM_FIRMWARE_INFO_V1_4 *fw_info)
{
	hwmgr->platform_descriptor.overdriveLimit.engineClock =
				le32_to_cpu(fw_info->ulASICMaxEngineClock);

	hwmgr->platform_descriptor.overdriveLimit.memoryClock =
				le32_to_cpu(fw_info->ulASICMaxMemoryClock);

	hwmgr->platform_descriptor.maxOverdriveVDDC =
		le32_to_cpu(fw_info->ul3DAccelerationEngineClock) & 0x7FF;

	hwmgr->platform_descriptor.minOverdriveVDDC =
			   le16_to_cpu(fw_info->usBootUpVDDCVoltage);

	hwmgr->platform_descriptor.maxOverdriveVDDC =
			   le16_to_cpu(fw_info->usBootUpVDDCVoltage);

	hwmgr->platform_descriptor.overdriveVDDCStep = 0;
	return 0;
}

static int init_overdrive_limits_V2_1(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table,
			const ATOM_FIRMWARE_INFO_V2_1 *fw_info)
{
	const ATOM_PPLIB_POWERPLAYTABLE3 *powerplay_table3;
	const ATOM_PPLIB_EXTENDEDHEADER *header;

	if (le16_to_cpu(powerplay_table->usTableSize) <
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE3))
		return 0;

	powerplay_table3 = (const ATOM_PPLIB_POWERPLAYTABLE3 *)powerplay_table;

	if (0 == powerplay_table3->usExtendendedHeaderOffset)
		return 0;

	header = (ATOM_PPLIB_EXTENDEDHEADER *)(((unsigned long) powerplay_table) +
			le16_to_cpu(powerplay_table3->usExtendendedHeaderOffset));

	hwmgr->platform_descriptor.overdriveLimit.engineClock = le32_to_cpu(header->ulMaxEngineClock);
	hwmgr->platform_descriptor.overdriveLimit.memoryClock = le32_to_cpu(header->ulMaxMemoryClock);


	hwmgr->platform_descriptor.minOverdriveVDDC = 0;
	hwmgr->platform_descriptor.maxOverdriveVDDC = 0;
	hwmgr->platform_descriptor.overdriveVDDCStep = 0;

	return 0;
}

static int init_overdrive_limits(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	int result = 0;
	uint8_t frev, crev;
	uint16_t size;

	const ATOM_COMMON_TABLE_HEADER *fw_info = NULL;

	hwmgr->platform_descriptor.overdriveLimit.engineClock = 0;
	hwmgr->platform_descriptor.overdriveLimit.memoryClock = 0;
	hwmgr->platform_descriptor.minOverdriveVDDC = 0;
	hwmgr->platform_descriptor.maxOverdriveVDDC = 0;
	hwmgr->platform_descriptor.overdriveVDDCStep = 0;

	if (hwmgr->chip_id == CHIP_RAVEN)
		return 0;

	/* We assume here that fw_info is unchanged if this call fails.*/
	fw_info = smu_atom_get_data_table(hwmgr->adev,
			 GetIndexIntoMasterTable(DATA, FirmwareInfo),
			 &size, &frev, &crev);
	PP_ASSERT_WITH_CODE(fw_info != NULL,
			    "Missing firmware info!", return -EINVAL);

	if ((fw_info->ucTableFormatRevision == 1)
	    && (le16_to_cpu(fw_info->usStructureSize) >= sizeof(ATOM_FIRMWARE_INFO_V1_4)))
		result = init_overdrive_limits_V1_4(hwmgr,
				powerplay_table,
				(const ATOM_FIRMWARE_INFO_V1_4 *)fw_info);

	else if ((fw_info->ucTableFormatRevision == 2)
		 && (le16_to_cpu(fw_info->usStructureSize) >= sizeof(ATOM_FIRMWARE_INFO_V2_1)))
		result = init_overdrive_limits_V2_1(hwmgr,
				powerplay_table,
				(const ATOM_FIRMWARE_INFO_V2_1 *)fw_info);

	return result;
}

static int get_uvd_clock_voltage_limit_table(struct pp_hwmgr *hwmgr,
		struct phm_uvd_clock_voltage_dependency_table **ptable,
		const ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table *table,
		const UVDClockInfoArray *array)
{
	unsigned long i;
	struct phm_uvd_clock_voltage_dependency_table *uvd_table;

	uvd_table = kzalloc(struct_size(uvd_table, entries, table->numEntries),
			    GFP_KERNEL);
	if (!uvd_table)
		return -ENOMEM;

	uvd_table->count = table->numEntries;

	for (i = 0; i < table->numEntries; i++) {
		const UVDClockInfo *entry =
			&array->entries[table->entries[i].ucUVDClockInfoIndex];
		uvd_table->entries[i].v = (unsigned long)le16_to_cpu(table->entries[i].usVoltage);
		uvd_table->entries[i].vclk = ((unsigned long)entry->ucVClkHigh << 16)
					 | le16_to_cpu(entry->usVClkLow);
		uvd_table->entries[i].dclk = ((unsigned long)entry->ucDClkHigh << 16)
					 | le16_to_cpu(entry->usDClkLow);
	}

	*ptable = uvd_table;

	return 0;
}

static int get_vce_clock_voltage_limit_table(struct pp_hwmgr *hwmgr,
		struct phm_vce_clock_voltage_dependency_table **ptable,
		const ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *table,
		const VCEClockInfoArray    *array)
{
	unsigned long i;
	struct phm_vce_clock_voltage_dependency_table *vce_table = NULL;

	vce_table = kzalloc(struct_size(vce_table, entries, table->numEntries),
			    GFP_KERNEL);
	if (!vce_table)
		return -ENOMEM;

	vce_table->count = table->numEntries;
	for (i = 0; i < table->numEntries; i++) {
		const VCEClockInfo *entry = &array->entries[table->entries[i].ucVCEClockInfoIndex];

		vce_table->entries[i].v = (unsigned long)le16_to_cpu(table->entries[i].usVoltage);
		vce_table->entries[i].evclk = ((unsigned long)entry->ucEVClkHigh << 16)
					| le16_to_cpu(entry->usEVClkLow);
		vce_table->entries[i].ecclk = ((unsigned long)entry->ucECClkHigh << 16)
					| le16_to_cpu(entry->usECClkLow);
	}

	*ptable = vce_table;

	return 0;
}

static int get_samu_clock_voltage_limit_table(struct pp_hwmgr *hwmgr,
		 struct phm_samu_clock_voltage_dependency_table **ptable,
		 const ATOM_PPLIB_SAMClk_Voltage_Limit_Table *table)
{
	unsigned long i;
	struct phm_samu_clock_voltage_dependency_table *samu_table;

	samu_table = kzalloc(struct_size(samu_table, entries, table->numEntries),
			     GFP_KERNEL);
	if (!samu_table)
		return -ENOMEM;

	samu_table->count = table->numEntries;

	for (i = 0; i < table->numEntries; i++) {
		samu_table->entries[i].v = (unsigned long)le16_to_cpu(table->entries[i].usVoltage);
		samu_table->entries[i].samclk = ((unsigned long)table->entries[i].ucSAMClockHigh << 16)
					 | le16_to_cpu(table->entries[i].usSAMClockLow);
	}

	*ptable = samu_table;

	return 0;
}

static int get_acp_clock_voltage_limit_table(struct pp_hwmgr *hwmgr,
		struct phm_acp_clock_voltage_dependency_table **ptable,
		const ATOM_PPLIB_ACPClk_Voltage_Limit_Table *table)
{
	unsigned long i;
	struct phm_acp_clock_voltage_dependency_table *acp_table;

	acp_table = kzalloc(struct_size(acp_table, entries, table->numEntries),
			    GFP_KERNEL);
	if (!acp_table)
		return -ENOMEM;

	acp_table->count = (unsigned long)table->numEntries;

	for (i = 0; i < table->numEntries; i++) {
		acp_table->entries[i].v = (unsigned long)le16_to_cpu(table->entries[i].usVoltage);
		acp_table->entries[i].acpclk = ((unsigned long)table->entries[i].ucACPClockHigh << 16)
					 | le16_to_cpu(table->entries[i].usACPClockLow);
	}

	*ptable = acp_table;

	return 0;
}

static int init_clock_voltage_dependency(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	ATOM_PPLIB_Clock_Voltage_Dependency_Table *table;
	ATOM_PPLIB_Clock_Voltage_Limit_Table *limit_table;
	int result = 0;

	uint16_t vce_clock_info_array_offset;
	uint16_t uvd_clock_info_array_offset;
	uint16_t table_offset;

	hwmgr->dyn_state.vddc_dependency_on_sclk = NULL;
	hwmgr->dyn_state.vddci_dependency_on_mclk = NULL;
	hwmgr->dyn_state.vddc_dependency_on_mclk = NULL;
	hwmgr->dyn_state.vddc_dep_on_dal_pwrl = NULL;
	hwmgr->dyn_state.mvdd_dependency_on_mclk = NULL;
	hwmgr->dyn_state.vce_clock_voltage_dependency_table = NULL;
	hwmgr->dyn_state.uvd_clock_voltage_dependency_table = NULL;
	hwmgr->dyn_state.samu_clock_voltage_dependency_table = NULL;
	hwmgr->dyn_state.acp_clock_voltage_dependency_table = NULL;
	hwmgr->dyn_state.ppm_parameter_table = NULL;
	hwmgr->dyn_state.vdd_gfx_dependency_on_sclk = NULL;

	vce_clock_info_array_offset = get_vce_clock_info_array_offset(
						hwmgr, powerplay_table);
	table_offset = get_vce_clock_voltage_limit_table_offset(hwmgr,
						powerplay_table);
	if (vce_clock_info_array_offset > 0 && table_offset > 0) {
		const VCEClockInfoArray *array = (const VCEClockInfoArray *)
				(((unsigned long) powerplay_table) +
				vce_clock_info_array_offset);
		const ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *table =
				(const ATOM_PPLIB_VCE_Clock_Voltage_Limit_Table *)
				(((unsigned long) powerplay_table) + table_offset);
		result = get_vce_clock_voltage_limit_table(hwmgr,
				&hwmgr->dyn_state.vce_clock_voltage_dependency_table,
				table, array);
	}

	uvd_clock_info_array_offset = get_uvd_clock_info_array_offset(hwmgr, powerplay_table);
	table_offset = get_uvd_clock_voltage_limit_table_offset(hwmgr, powerplay_table);

	if (uvd_clock_info_array_offset > 0 && table_offset > 0) {
		const UVDClockInfoArray *array = (const UVDClockInfoArray *)
				(((unsigned long) powerplay_table) +
				uvd_clock_info_array_offset);
		const ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table *ptable =
				(const ATOM_PPLIB_UVD_Clock_Voltage_Limit_Table *)
				(((unsigned long) powerplay_table) + table_offset);
		result = get_uvd_clock_voltage_limit_table(hwmgr,
				&hwmgr->dyn_state.uvd_clock_voltage_dependency_table, ptable, array);
	}

	table_offset = get_samu_clock_voltage_limit_table_offset(hwmgr,
							    powerplay_table);

	if (table_offset > 0) {
		const ATOM_PPLIB_SAMClk_Voltage_Limit_Table *ptable =
				(const ATOM_PPLIB_SAMClk_Voltage_Limit_Table *)
				(((unsigned long) powerplay_table) + table_offset);
		result = get_samu_clock_voltage_limit_table(hwmgr,
				&hwmgr->dyn_state.samu_clock_voltage_dependency_table, ptable);
	}

	table_offset = get_acp_clock_voltage_limit_table_offset(hwmgr,
							     powerplay_table);

	if (table_offset > 0) {
		const ATOM_PPLIB_ACPClk_Voltage_Limit_Table *ptable =
				(const ATOM_PPLIB_ACPClk_Voltage_Limit_Table *)
				(((unsigned long) powerplay_table) + table_offset);
		result = get_acp_clock_voltage_limit_table(hwmgr,
				&hwmgr->dyn_state.acp_clock_voltage_dependency_table, ptable);
	}

	table_offset = get_cacp_tdp_table_offset(hwmgr, powerplay_table);
	if (table_offset > 0) {
		UCHAR rev_id = *(UCHAR *)(((unsigned long)powerplay_table) + table_offset);

		if (rev_id > 0) {
			const ATOM_PPLIB_POWERTUNE_Table_V1 *tune_table =
				(const ATOM_PPLIB_POWERTUNE_Table_V1 *)
				(((unsigned long) powerplay_table) + table_offset);
			result = get_cac_tdp_table(hwmgr, &hwmgr->dyn_state.cac_dtp_table,
				&tune_table->power_tune_table,
				le16_to_cpu(tune_table->usMaximumPowerDeliveryLimit));
			hwmgr->dyn_state.cac_dtp_table->usDefaultTargetOperatingTemp =
				le16_to_cpu(tune_table->usTjMax);
		} else {
			const ATOM_PPLIB_POWERTUNE_Table *tune_table =
				(const ATOM_PPLIB_POWERTUNE_Table *)
				(((unsigned long) powerplay_table) + table_offset);
			result = get_cac_tdp_table(hwmgr,
				&hwmgr->dyn_state.cac_dtp_table,
				&tune_table->power_tune_table, 255);
		}
	}

	if (le16_to_cpu(powerplay_table->usTableSize) >=
		sizeof(ATOM_PPLIB_POWERPLAYTABLE4)) {
		const ATOM_PPLIB_POWERPLAYTABLE4 *powerplay_table4 =
				(const ATOM_PPLIB_POWERPLAYTABLE4 *)powerplay_table;
		if (0 != powerplay_table4->usVddcDependencyOnSCLKOffset) {
			table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(((unsigned long) powerplay_table4) +
				 le16_to_cpu(powerplay_table4->usVddcDependencyOnSCLKOffset));
			result = get_clock_voltage_dependency_table(hwmgr,
				&hwmgr->dyn_state.vddc_dependency_on_sclk, table);
		}

		if (result == 0 && (0 != powerplay_table4->usVddciDependencyOnMCLKOffset)) {
			table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(((unsigned long) powerplay_table4) +
				 le16_to_cpu(powerplay_table4->usVddciDependencyOnMCLKOffset));
			result = get_clock_voltage_dependency_table(hwmgr,
				&hwmgr->dyn_state.vddci_dependency_on_mclk, table);
		}

		if (result == 0 && (0 != powerplay_table4->usVddcDependencyOnMCLKOffset)) {
			table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(((unsigned long) powerplay_table4) +
				 le16_to_cpu(powerplay_table4->usVddcDependencyOnMCLKOffset));
			result = get_clock_voltage_dependency_table(hwmgr,
				&hwmgr->dyn_state.vddc_dependency_on_mclk, table);
		}

		if (result == 0 && (0 != powerplay_table4->usMaxClockVoltageOnDCOffset)) {
			limit_table = (ATOM_PPLIB_Clock_Voltage_Limit_Table *)
				(((unsigned long) powerplay_table4) +
				 le16_to_cpu(powerplay_table4->usMaxClockVoltageOnDCOffset));
			result = get_clock_voltage_limit(hwmgr,
				&hwmgr->dyn_state.max_clock_voltage_on_dc, limit_table);
		}

		if (result == 0 && (NULL != hwmgr->dyn_state.vddc_dependency_on_mclk) &&
			(0 != hwmgr->dyn_state.vddc_dependency_on_mclk->count))
			result = get_valid_clk(hwmgr, &hwmgr->dyn_state.valid_mclk_values,
					hwmgr->dyn_state.vddc_dependency_on_mclk);

		if(result == 0 && (NULL != hwmgr->dyn_state.vddc_dependency_on_sclk) &&
			(0 != hwmgr->dyn_state.vddc_dependency_on_sclk->count))
			result = get_valid_clk(hwmgr,
				&hwmgr->dyn_state.valid_sclk_values,
				hwmgr->dyn_state.vddc_dependency_on_sclk);

		if (result == 0 && (0 != powerplay_table4->usMvddDependencyOnMCLKOffset)) {
			table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
				(((unsigned long) powerplay_table4) +
				 le16_to_cpu(powerplay_table4->usMvddDependencyOnMCLKOffset));
			result = get_clock_voltage_dependency_table(hwmgr,
				&hwmgr->dyn_state.mvdd_dependency_on_mclk, table);
		}
	}

	table_offset = get_sclk_vdd_gfx_clock_voltage_dependency_table_offset(hwmgr,
								powerplay_table);

	if (table_offset > 0) {
		table = (ATOM_PPLIB_Clock_Voltage_Dependency_Table *)
			(((unsigned long) powerplay_table) + table_offset);
		result = get_clock_voltage_dependency_table(hwmgr,
			&hwmgr->dyn_state.vdd_gfx_dependency_on_sclk, table);
	}

	return result;
}

static int get_cac_leakage_table(struct pp_hwmgr *hwmgr,
				 struct phm_cac_leakage_table **ptable,
				const ATOM_PPLIB_CAC_Leakage_Table *table)
{
	struct phm_cac_leakage_table  *cac_leakage_table;
	unsigned long i;

	if (!hwmgr || !table || !ptable)
		return -EINVAL;

	cac_leakage_table = kzalloc(struct_size(cac_leakage_table, entries, table->ucNumEntries),
				    GFP_KERNEL);
	if (!cac_leakage_table)
		return -ENOMEM;

	cac_leakage_table->count = (ULONG)table->ucNumEntries;

	for (i = 0; i < cac_leakage_table->count; i++) {
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_EVV)) {
			cac_leakage_table->entries[i].Vddc1 = le16_to_cpu(table->entries[i].usVddc1);
			cac_leakage_table->entries[i].Vddc2 = le16_to_cpu(table->entries[i].usVddc2);
			cac_leakage_table->entries[i].Vddc3 = le16_to_cpu(table->entries[i].usVddc3);
		} else {
			cac_leakage_table->entries[i].Vddc    = le16_to_cpu(table->entries[i].usVddc);
			cac_leakage_table->entries[i].Leakage = le32_to_cpu(table->entries[i].ulLeakageValue);
		}
	}

	*ptable = cac_leakage_table;

	return 0;
}

static int get_platform_power_management_table(struct pp_hwmgr *hwmgr,
			ATOM_PPLIB_PPM_Table *atom_ppm_table)
{
	struct phm_ppm_table *ptr = kzalloc(sizeof(struct phm_ppm_table), GFP_KERNEL);

	if (NULL == ptr)
		return -ENOMEM;

	ptr->ppm_design            = atom_ppm_table->ucPpmDesign;
	ptr->cpu_core_number        = le16_to_cpu(atom_ppm_table->usCpuCoreNumber);
	ptr->platform_tdp          = le32_to_cpu(atom_ppm_table->ulPlatformTDP);
	ptr->small_ac_platform_tdp   = le32_to_cpu(atom_ppm_table->ulSmallACPlatformTDP);
	ptr->platform_tdc          = le32_to_cpu(atom_ppm_table->ulPlatformTDC);
	ptr->small_ac_platform_tdc   = le32_to_cpu(atom_ppm_table->ulSmallACPlatformTDC);
	ptr->apu_tdp               = le32_to_cpu(atom_ppm_table->ulApuTDP);
	ptr->dgpu_tdp              = le32_to_cpu(atom_ppm_table->ulDGpuTDP);
	ptr->dgpu_ulv_power         = le32_to_cpu(atom_ppm_table->ulDGpuUlvPower);
	ptr->tj_max                = le32_to_cpu(atom_ppm_table->ulTjmax);
	hwmgr->dyn_state.ppm_parameter_table = ptr;

	return 0;
}

static int init_dpm2_parameters(struct pp_hwmgr *hwmgr,
			const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	int result = 0;

	if (le16_to_cpu(powerplay_table->usTableSize) >=
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE5)) {
		const  ATOM_PPLIB_POWERPLAYTABLE5 *ptable5 =
				(const ATOM_PPLIB_POWERPLAYTABLE5 *)powerplay_table;
		const  ATOM_PPLIB_POWERPLAYTABLE4 *ptable4 =
				(const ATOM_PPLIB_POWERPLAYTABLE4 *)
				(&ptable5->basicTable4);
		const  ATOM_PPLIB_POWERPLAYTABLE3 *ptable3 =
				(const ATOM_PPLIB_POWERPLAYTABLE3 *)
				(&ptable4->basicTable3);
		const  ATOM_PPLIB_EXTENDEDHEADER  *extended_header;
		uint16_t table_offset;
		ATOM_PPLIB_PPM_Table *atom_ppm_table;

		hwmgr->platform_descriptor.TDPLimit     = le32_to_cpu(ptable5->ulTDPLimit);
		hwmgr->platform_descriptor.nearTDPLimit = le32_to_cpu(ptable5->ulNearTDPLimit);

		hwmgr->platform_descriptor.TDPODLimit   = le16_to_cpu(ptable5->usTDPODLimit);
		hwmgr->platform_descriptor.TDPAdjustment = 0;

		hwmgr->platform_descriptor.VidAdjustment = 0;
		hwmgr->platform_descriptor.VidAdjustmentPolarity = 0;
		hwmgr->platform_descriptor.VidMinLimit     = 0;
		hwmgr->platform_descriptor.VidMaxLimit     = 1500000;
		hwmgr->platform_descriptor.VidStep         = 6250;

		hwmgr->platform_descriptor.nearTDPLimitAdjusted = le32_to_cpu(ptable5->ulNearTDPLimit);

		if (hwmgr->platform_descriptor.TDPODLimit != 0)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_PowerControl);

		hwmgr->platform_descriptor.SQRampingThreshold = le32_to_cpu(ptable5->ulSQRampingThreshold);

		hwmgr->platform_descriptor.CACLeakage = le32_to_cpu(ptable5->ulCACLeakage);

		hwmgr->dyn_state.cac_leakage_table = NULL;

		if (0 != ptable5->usCACLeakageTableOffset) {
			const ATOM_PPLIB_CAC_Leakage_Table *pCAC_leakage_table =
				(ATOM_PPLIB_CAC_Leakage_Table *)(((unsigned long)ptable5) +
				le16_to_cpu(ptable5->usCACLeakageTableOffset));
			result = get_cac_leakage_table(hwmgr,
				&hwmgr->dyn_state.cac_leakage_table, pCAC_leakage_table);
		}

		hwmgr->platform_descriptor.LoadLineSlope = le16_to_cpu(ptable5->usLoadLineSlope);

		hwmgr->dyn_state.ppm_parameter_table = NULL;

		if (0 != ptable3->usExtendendedHeaderOffset) {
			extended_header = (const ATOM_PPLIB_EXTENDEDHEADER *)
					(((unsigned long)powerplay_table) +
					le16_to_cpu(ptable3->usExtendendedHeaderOffset));
			if ((extended_header->usPPMTableOffset > 0) &&
				le16_to_cpu(extended_header->usSize) >=
				    SIZE_OF_ATOM_PPLIB_EXTENDEDHEADER_V5) {
				table_offset = le16_to_cpu(extended_header->usPPMTableOffset);
				atom_ppm_table = (ATOM_PPLIB_PPM_Table *)
					(((unsigned long)powerplay_table) + table_offset);
				if (0 == get_platform_power_management_table(hwmgr, atom_ppm_table))
					phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_EnablePlatformPowerManagement);
			}
		}
	}
	return result;
}

static int init_phase_shedding_table(struct pp_hwmgr *hwmgr,
		const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table)
{
	if (le16_to_cpu(powerplay_table->usTableSize) >=
	    sizeof(ATOM_PPLIB_POWERPLAYTABLE4)) {
		const ATOM_PPLIB_POWERPLAYTABLE4 *powerplay_table4 =
				(const ATOM_PPLIB_POWERPLAYTABLE4 *)powerplay_table;

		if (0 != powerplay_table4->usVddcPhaseShedLimitsTableOffset) {
			const ATOM_PPLIB_PhaseSheddingLimits_Table *ptable =
				(ATOM_PPLIB_PhaseSheddingLimits_Table *)
				(((unsigned long)powerplay_table4) +
				le16_to_cpu(powerplay_table4->usVddcPhaseShedLimitsTableOffset));
			struct phm_phase_shedding_limits_table *table;
			unsigned long i;


			table = kzalloc(struct_size(table, entries, ptable->ucNumEntries),
					GFP_KERNEL);
			if (!table)
				return -ENOMEM;

			table->count = (unsigned long)ptable->ucNumEntries;

			for (i = 0; i < table->count; i++) {
				table->entries[i].Voltage = (unsigned long)le16_to_cpu(ptable->entries[i].usVoltage);
				table->entries[i].Sclk    = ((unsigned long)ptable->entries[i].ucSclkHigh << 16)
							| le16_to_cpu(ptable->entries[i].usSclkLow);
				table->entries[i].Mclk    = ((unsigned long)ptable->entries[i].ucMclkHigh << 16)
							| le16_to_cpu(ptable->entries[i].usMclkLow);
			}
			hwmgr->dyn_state.vddc_phase_shed_limits_table = table;
		}
	}

	return 0;
}

static int get_number_of_vce_state_table_entries(
						  struct pp_hwmgr *hwmgr)
{
	const ATOM_PPLIB_POWERPLAYTABLE *table =
					     get_powerplay_table(hwmgr);
	const ATOM_PPLIB_VCE_State_Table *vce_table =
				    get_vce_state_table(hwmgr, table);

	if (vce_table)
		return vce_table->numEntries;

	return 0;
}

static int get_vce_state_table_entry(struct pp_hwmgr *hwmgr,
							unsigned long i,
							struct amd_vce_state *vce_state,
							void **clock_info,
							unsigned long *flag)
{
	const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table = get_powerplay_table(hwmgr);

	const ATOM_PPLIB_VCE_State_Table *vce_state_table = get_vce_state_table(hwmgr, powerplay_table);

	unsigned short vce_clock_info_array_offset = get_vce_clock_info_array_offset(hwmgr, powerplay_table);

	const VCEClockInfoArray *vce_clock_info_array = (const VCEClockInfoArray *)(((unsigned long) powerplay_table) + vce_clock_info_array_offset);

	const ClockInfoArray *clock_arrays = (ClockInfoArray *)(((unsigned long)powerplay_table) +
								le16_to_cpu(powerplay_table->usClockInfoArrayOffset));

	const ATOM_PPLIB_VCE_State_Record *record = &vce_state_table->entries[i];

	const VCEClockInfo *vce_clock_info = &vce_clock_info_array->entries[record->ucVCEClockInfoIndex];

	unsigned long clockInfoIndex = record->ucClockInfoIndex & 0x3F;

	*flag = (record->ucClockInfoIndex >> NUM_BITS_CLOCK_INFO_ARRAY_INDEX);

	vce_state->evclk = ((uint32_t)vce_clock_info->ucEVClkHigh << 16) | le16_to_cpu(vce_clock_info->usEVClkLow);
	vce_state->ecclk = ((uint32_t)vce_clock_info->ucECClkHigh << 16) | le16_to_cpu(vce_clock_info->usECClkLow);

	*clock_info = (void *)((unsigned long)(clock_arrays->clockInfo) + (clockInfoIndex * clock_arrays->ucEntrySize));

	return 0;
}


static int pp_tables_initialize(struct pp_hwmgr *hwmgr)
{
	int result;
	const ATOM_PPLIB_POWERPLAYTABLE *powerplay_table;

	if (hwmgr->chip_id == CHIP_RAVEN)
		return 0;

	hwmgr->need_pp_table_upload = true;

	powerplay_table = get_powerplay_table(hwmgr);

	result = init_powerplay_tables(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_powerplay_tables failed", return result);

	result = set_platform_caps(hwmgr,
				le32_to_cpu(powerplay_table->ulPlatformCaps));

	PP_ASSERT_WITH_CODE((result == 0),
			    "set_platform_caps failed", return result);

	result = init_thermal_controller(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_thermal_controller failed", return result);

	result = init_overdrive_limits(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_overdrive_limits failed", return result);

	result = init_clock_voltage_dependency(hwmgr,
					       powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_clock_voltage_dependency failed", return result);

	result = init_dpm2_parameters(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_dpm2_parameters failed", return result);

	result = init_phase_shedding_table(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_phase_shedding_table failed", return result);

	return result;
}

static int pp_tables_uninitialize(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->chip_id == CHIP_RAVEN)
		return 0;

	kfree(hwmgr->dyn_state.vddc_dependency_on_sclk);
	hwmgr->dyn_state.vddc_dependency_on_sclk = NULL;

	kfree(hwmgr->dyn_state.vddci_dependency_on_mclk);
	hwmgr->dyn_state.vddci_dependency_on_mclk = NULL;

	kfree(hwmgr->dyn_state.vddc_dependency_on_mclk);
	hwmgr->dyn_state.vddc_dependency_on_mclk = NULL;

	kfree(hwmgr->dyn_state.mvdd_dependency_on_mclk);
	hwmgr->dyn_state.mvdd_dependency_on_mclk = NULL;

	kfree(hwmgr->dyn_state.valid_mclk_values);
	hwmgr->dyn_state.valid_mclk_values = NULL;

	kfree(hwmgr->dyn_state.valid_sclk_values);
	hwmgr->dyn_state.valid_sclk_values = NULL;

	kfree(hwmgr->dyn_state.cac_leakage_table);
	hwmgr->dyn_state.cac_leakage_table = NULL;

	kfree(hwmgr->dyn_state.vddc_phase_shed_limits_table);
	hwmgr->dyn_state.vddc_phase_shed_limits_table = NULL;

	kfree(hwmgr->dyn_state.vce_clock_voltage_dependency_table);
	hwmgr->dyn_state.vce_clock_voltage_dependency_table = NULL;

	kfree(hwmgr->dyn_state.uvd_clock_voltage_dependency_table);
	hwmgr->dyn_state.uvd_clock_voltage_dependency_table = NULL;

	kfree(hwmgr->dyn_state.samu_clock_voltage_dependency_table);
	hwmgr->dyn_state.samu_clock_voltage_dependency_table = NULL;

	kfree(hwmgr->dyn_state.acp_clock_voltage_dependency_table);
	hwmgr->dyn_state.acp_clock_voltage_dependency_table = NULL;

	kfree(hwmgr->dyn_state.cac_dtp_table);
	hwmgr->dyn_state.cac_dtp_table = NULL;

	kfree(hwmgr->dyn_state.ppm_parameter_table);
	hwmgr->dyn_state.ppm_parameter_table = NULL;

	kfree(hwmgr->dyn_state.vdd_gfx_dependency_on_sclk);
	hwmgr->dyn_state.vdd_gfx_dependency_on_sclk = NULL;

	return 0;
}

const struct pp_table_func pptable_funcs = {
	.pptable_init = pp_tables_initialize,
	.pptable_fini = pp_tables_uninitialize,
	.pptable_get_number_of_vce_state_table_entries =
				get_number_of_vce_state_table_entries,
	.pptable_get_vce_state_table_entry =
						get_vce_state_table_entry,
};

