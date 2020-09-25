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
#include <linux/module.h>
#include <linux/slab.h>

#include "process_pptables_v1_0.h"
#include "ppatomctrl.h"
#include "atombios.h"
#include "hwmgr.h"
#include "cgs_common.h"
#include "pptable_v1_0.h"

/**
 * Private Function used during initialization.
 * @param hwmgr Pointer to the hardware manager.
 * @param setIt A flag indication if the capability should be set (TRUE) or reset (FALSE).
 * @param cap Which capability to set/reset.
 */
static void set_hw_cap(struct pp_hwmgr *hwmgr, bool setIt, enum phm_platform_caps cap)
{
	if (setIt)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps, cap);
	else
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps, cap);
}


/**
 * Private Function used during initialization.
 * @param hwmgr Pointer to the hardware manager.
 * @param powerplay_caps the bit array (from BIOS) of capability bits.
 * @exception the current implementation always returns 1.
 */
static int set_platform_caps(struct pp_hwmgr *hwmgr, uint32_t powerplay_caps)
{
	PP_ASSERT_WITH_CODE((~powerplay_caps & ____RETIRE16____),
		"ATOM_PP_PLATFORM_CAP_ASPM_L1 is not supported!", continue);
	PP_ASSERT_WITH_CODE((~powerplay_caps & ____RETIRE64____),
		"ATOM_PP_PLATFORM_CAP_GEMINIPRIMARY is not supported!", continue);
	PP_ASSERT_WITH_CODE((~powerplay_caps & ____RETIRE512____),
		"ATOM_PP_PLATFORM_CAP_SIDEPORTCONTROL is not supported!", continue);
	PP_ASSERT_WITH_CODE((~powerplay_caps & ____RETIRE1024____),
		"ATOM_PP_PLATFORM_CAP_TURNOFFPLL_ASPML1 is not supported!", continue);
	PP_ASSERT_WITH_CODE((~powerplay_caps & ____RETIRE2048____),
		"ATOM_PP_PLATFORM_CAP_HTLINKCONTROL is not supported!", continue);

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_POWERPLAY),
			PHM_PlatformCaps_PowerPlaySupport
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_SBIOSPOWERSOURCE),
			PHM_PlatformCaps_BiosPowerSourceControl
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_HARDWAREDC),
			PHM_PlatformCaps_AutomaticDCTransition
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_MVDD_CONTROL),
			PHM_PlatformCaps_EnableMVDDControl
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_VDDCI_CONTROL),
			PHM_PlatformCaps_ControlVDDCI
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_VDDGFX_CONTROL),
			PHM_PlatformCaps_ControlVDDGFX
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_BACO),
			PHM_PlatformCaps_BACO
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_CAP_DISABLE_VOLTAGE_ISLAND),
			PHM_PlatformCaps_DisableVoltageIsland
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PP_PLATFORM_COMBINE_PCC_WITH_THERMAL_SIGNAL),
			PHM_PlatformCaps_CombinePCCWithThermalSignal
		  );

	set_hw_cap(
			hwmgr,
			0 != (powerplay_caps & ATOM_TONGA_PLATFORM_LOAD_POST_PRODUCTION_FIRMWARE),
			PHM_PlatformCaps_LoadPostProductionFirmware
		  );

	return 0;
}

/**
 * Private Function to get the PowerPlay Table Address.
 */
static const void *get_powerplay_table(struct pp_hwmgr *hwmgr)
{
	int index = GetIndexIntoMasterTable(DATA, PowerPlayInfo);

	u16 size;
	u8 frev, crev;
	void *table_address = (void *)hwmgr->soft_pp_table;

	if (!table_address) {
		table_address = (ATOM_Tonga_POWERPLAYTABLE *)
				smu_atom_get_data_table(hwmgr->adev,
						index, &size, &frev, &crev);
		hwmgr->soft_pp_table = table_address;	/*Cache the result in RAM.*/
		hwmgr->soft_pp_table_size = size;
	}

	return table_address;
}

static int get_vddc_lookup_table(
		struct pp_hwmgr	*hwmgr,
		phm_ppt_v1_voltage_lookup_table	**lookup_table,
		const ATOM_Tonga_Voltage_Lookup_Table *vddc_lookup_pp_tables,
		uint32_t max_levels
		)
{
	uint32_t i;
	phm_ppt_v1_voltage_lookup_table *table;
	phm_ppt_v1_voltage_lookup_record *record;
	ATOM_Tonga_Voltage_Lookup_Record *atom_record;

	PP_ASSERT_WITH_CODE((0 != vddc_lookup_pp_tables->ucNumEntries),
		"Invalid CAC Leakage PowerPlay Table!", return 1);

	table = kzalloc(struct_size(table, entries, max_levels), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table->count = vddc_lookup_pp_tables->ucNumEntries;

	for (i = 0; i < vddc_lookup_pp_tables->ucNumEntries; i++) {
		record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					phm_ppt_v1_voltage_lookup_record,
					entries, table, i);
		atom_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					ATOM_Tonga_Voltage_Lookup_Record,
					entries, vddc_lookup_pp_tables, i);
		record->us_calculated = 0;
		record->us_vdd = le16_to_cpu(atom_record->usVdd);
		record->us_cac_low = le16_to_cpu(atom_record->usCACLow);
		record->us_cac_mid = le16_to_cpu(atom_record->usCACMid);
		record->us_cac_high = le16_to_cpu(atom_record->usCACHigh);
	}

	*lookup_table = table;

	return 0;
}

/**
 * Private Function used during initialization.
 * Initialize Platform Power Management Parameter table
 * @param hwmgr Pointer to the hardware manager.
 * @param atom_ppm_table Pointer to PPM table in VBIOS
 */
static int get_platform_power_management_table(
		struct pp_hwmgr *hwmgr,
		ATOM_Tonga_PPM_Table *atom_ppm_table)
{
	struct phm_ppm_table *ptr = kzalloc(sizeof(ATOM_Tonga_PPM_Table), GFP_KERNEL);
	struct phm_ppt_v1_information *pp_table_information =
		(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (NULL == ptr)
		return -ENOMEM;

	ptr->ppm_design
		= atom_ppm_table->ucPpmDesign;
	ptr->cpu_core_number
		= le16_to_cpu(atom_ppm_table->usCpuCoreNumber);
	ptr->platform_tdp
		= le32_to_cpu(atom_ppm_table->ulPlatformTDP);
	ptr->small_ac_platform_tdp
		= le32_to_cpu(atom_ppm_table->ulSmallACPlatformTDP);
	ptr->platform_tdc
		= le32_to_cpu(atom_ppm_table->ulPlatformTDC);
	ptr->small_ac_platform_tdc
		= le32_to_cpu(atom_ppm_table->ulSmallACPlatformTDC);
	ptr->apu_tdp
		= le32_to_cpu(atom_ppm_table->ulApuTDP);
	ptr->dgpu_tdp
		= le32_to_cpu(atom_ppm_table->ulDGpuTDP);
	ptr->dgpu_ulv_power
		= le32_to_cpu(atom_ppm_table->ulDGpuUlvPower);
	ptr->tj_max
		= le32_to_cpu(atom_ppm_table->ulTjmax);

	pp_table_information->ppm_parameter_table = ptr;

	return 0;
}

/**
 * Private Function used during initialization.
 * Initialize TDP limits for DPM2
 * @param hwmgr Pointer to the hardware manager.
 * @param powerplay_table Pointer to the PowerPlay Table.
 */
static int init_dpm_2_parameters(
		struct pp_hwmgr *hwmgr,
		const ATOM_Tonga_POWERPLAYTABLE *powerplay_table
		)
{
	int result = 0;
	struct phm_ppt_v1_information *pp_table_information = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	ATOM_Tonga_PPM_Table *atom_ppm_table;
	uint32_t disable_ppm = 0;
	uint32_t disable_power_control = 0;

	pp_table_information->us_ulv_voltage_offset =
		le16_to_cpu(powerplay_table->usUlvVoltageOffset);

	pp_table_information->ppm_parameter_table = NULL;
	pp_table_information->vddc_lookup_table = NULL;
	pp_table_information->vddgfx_lookup_table = NULL;
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
	if (0 == disable_power_control) {
		/* enable TDP overdrive (PowerControl) feature as well if supported */
		if (hwmgr->platform_descriptor.TDPODLimit != 0)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerControl);
	}

	if (0 != powerplay_table->usVddcLookupTableOffset) {
		const ATOM_Tonga_Voltage_Lookup_Table *pVddcCACTable =
			(ATOM_Tonga_Voltage_Lookup_Table *)(((unsigned long)powerplay_table) +
			le16_to_cpu(powerplay_table->usVddcLookupTableOffset));

		result = get_vddc_lookup_table(hwmgr,
			&pp_table_information->vddc_lookup_table, pVddcCACTable, 16);
	}

	if (0 != powerplay_table->usVddgfxLookupTableOffset) {
		const ATOM_Tonga_Voltage_Lookup_Table *pVddgfxCACTable =
			(ATOM_Tonga_Voltage_Lookup_Table *)(((unsigned long)powerplay_table) +
			le16_to_cpu(powerplay_table->usVddgfxLookupTableOffset));

		result = get_vddc_lookup_table(hwmgr,
			&pp_table_information->vddgfx_lookup_table, pVddgfxCACTable, 16);
	}

	disable_ppm = 0;
	if (0 == disable_ppm) {
		atom_ppm_table = (ATOM_Tonga_PPM_Table *)
			(((unsigned long)powerplay_table) + le16_to_cpu(powerplay_table->usPPMTableOffset));

		if (0 != powerplay_table->usPPMTableOffset) {
			if (get_platform_power_management_table(hwmgr, atom_ppm_table) == 0) {
				phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_EnablePlatformPowerManagement);
			}
		}
	}

	return result;
}

static int get_valid_clk(
		struct pp_hwmgr *hwmgr,
		struct phm_clock_array **clk_table,
		phm_ppt_v1_clock_voltage_dependency_table const *clk_volt_pp_table
		)
{
	uint32_t i;
	struct phm_clock_array *table;
	phm_ppt_v1_clock_voltage_dependency_record *dep_record;

	PP_ASSERT_WITH_CODE((0 != clk_volt_pp_table->count),
		"Invalid PowerPlay Table!", return -1);

	table = kzalloc(struct_size(table, values, clk_volt_pp_table->count),
			GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	table->count = (uint32_t)clk_volt_pp_table->count;

	for (i = 0; i < table->count; i++) {
		dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
				phm_ppt_v1_clock_voltage_dependency_record,
				entries, clk_volt_pp_table, i);
		table->values[i] = (uint32_t)dep_record->clk;
	}
	*clk_table = table;

	return 0;
}

static int get_hard_limits(
		struct pp_hwmgr *hwmgr,
		struct phm_clock_and_voltage_limits *limits,
		ATOM_Tonga_Hard_Limit_Table const *limitable
		)
{
	PP_ASSERT_WITH_CODE((0 != limitable->ucNumEntries), "Invalid PowerPlay Table!", return -1);

	/* currently we always take entries[0] parameters */
	limits->sclk = le32_to_cpu(limitable->entries[0].ulSCLKLimit);
	limits->mclk = le32_to_cpu(limitable->entries[0].ulMCLKLimit);
	limits->vddc = le16_to_cpu(limitable->entries[0].usVddcLimit);
	limits->vddci = le16_to_cpu(limitable->entries[0].usVddciLimit);
	limits->vddgfx = le16_to_cpu(limitable->entries[0].usVddgfxLimit);

	return 0;
}

static int get_mclk_voltage_dependency_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table **pp_tonga_mclk_dep_table,
		ATOM_Tonga_MCLK_Dependency_Table const *mclk_dep_table
		)
{
	uint32_t i;
	phm_ppt_v1_clock_voltage_dependency_table *mclk_table;
	phm_ppt_v1_clock_voltage_dependency_record *mclk_table_record;
	ATOM_Tonga_MCLK_Dependency_Record *mclk_dep_record;

	PP_ASSERT_WITH_CODE((0 != mclk_dep_table->ucNumEntries),
		"Invalid PowerPlay Table!", return -1);

	mclk_table = kzalloc(struct_size(mclk_table, entries, mclk_dep_table->ucNumEntries),
			     GFP_KERNEL);
	if (!mclk_table)
		return -ENOMEM;

	mclk_table->count = (uint32_t)mclk_dep_table->ucNumEntries;

	for (i = 0; i < mclk_dep_table->ucNumEntries; i++) {
		mclk_table_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					phm_ppt_v1_clock_voltage_dependency_record,
						entries, mclk_table, i);
		mclk_dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					ATOM_Tonga_MCLK_Dependency_Record,
						entries, mclk_dep_table, i);
		mclk_table_record->vddInd = mclk_dep_record->ucVddcInd;
		mclk_table_record->vdd_offset = le16_to_cpu(mclk_dep_record->usVddgfxOffset);
		mclk_table_record->vddci = le16_to_cpu(mclk_dep_record->usVddci);
		mclk_table_record->mvdd = le16_to_cpu(mclk_dep_record->usMvdd);
		mclk_table_record->clk = le32_to_cpu(mclk_dep_record->ulMclk);
	}

	*pp_tonga_mclk_dep_table = mclk_table;

	return 0;
}

static int get_sclk_voltage_dependency_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table **pp_tonga_sclk_dep_table,
		PPTable_Generic_SubTable_Header const  *sclk_dep_table
		)
{
	uint32_t i;
	phm_ppt_v1_clock_voltage_dependency_table *sclk_table;
	phm_ppt_v1_clock_voltage_dependency_record *sclk_table_record;

	if (sclk_dep_table->ucRevId < 1) {
		const ATOM_Tonga_SCLK_Dependency_Table *tonga_table =
			    (ATOM_Tonga_SCLK_Dependency_Table *)sclk_dep_table;
		ATOM_Tonga_SCLK_Dependency_Record *sclk_dep_record;

		PP_ASSERT_WITH_CODE((0 != tonga_table->ucNumEntries),
			"Invalid PowerPlay Table!", return -1);

		sclk_table = kzalloc(struct_size(sclk_table, entries, tonga_table->ucNumEntries),
				     GFP_KERNEL);
		if (!sclk_table)
			return -ENOMEM;

		sclk_table->count = (uint32_t)tonga_table->ucNumEntries;

		for (i = 0; i < tonga_table->ucNumEntries; i++) {
			sclk_dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						ATOM_Tonga_SCLK_Dependency_Record,
						entries, tonga_table, i);
			sclk_table_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						phm_ppt_v1_clock_voltage_dependency_record,
						entries, sclk_table, i);
			sclk_table_record->vddInd = sclk_dep_record->ucVddInd;
			sclk_table_record->vdd_offset = le16_to_cpu(sclk_dep_record->usVddcOffset);
			sclk_table_record->clk = le32_to_cpu(sclk_dep_record->ulSclk);
			sclk_table_record->cks_enable =
				(((sclk_dep_record->ucCKSVOffsetandDisable & 0x80) >> 7) == 0) ? 1 : 0;
			sclk_table_record->cks_voffset = (sclk_dep_record->ucCKSVOffsetandDisable & 0x7F);
		}
	} else {
		const ATOM_Polaris_SCLK_Dependency_Table *polaris_table =
			    (ATOM_Polaris_SCLK_Dependency_Table *)sclk_dep_table;
		ATOM_Polaris_SCLK_Dependency_Record *sclk_dep_record;

		PP_ASSERT_WITH_CODE((0 != polaris_table->ucNumEntries),
			"Invalid PowerPlay Table!", return -1);

		sclk_table = kzalloc(struct_size(sclk_table, entries, polaris_table->ucNumEntries),
				     GFP_KERNEL);
		if (!sclk_table)
			return -ENOMEM;

		sclk_table->count = (uint32_t)polaris_table->ucNumEntries;

		for (i = 0; i < polaris_table->ucNumEntries; i++) {
			sclk_dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						ATOM_Polaris_SCLK_Dependency_Record,
						entries, polaris_table, i);
			sclk_table_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						phm_ppt_v1_clock_voltage_dependency_record,
						entries, sclk_table, i);
			sclk_table_record->vddInd = sclk_dep_record->ucVddInd;
			sclk_table_record->vdd_offset = le16_to_cpu(sclk_dep_record->usVddcOffset);
			sclk_table_record->clk = le32_to_cpu(sclk_dep_record->ulSclk);
			sclk_table_record->cks_enable =
				(((sclk_dep_record->ucCKSVOffsetandDisable & 0x80) >> 7) == 0) ? 1 : 0;
			sclk_table_record->cks_voffset = (sclk_dep_record->ucCKSVOffsetandDisable & 0x7F);
			sclk_table_record->sclk_offset = le32_to_cpu(sclk_dep_record->ulSclkOffset);
		}
	}
	*pp_tonga_sclk_dep_table = sclk_table;

	return 0;
}

static int get_pcie_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_pcie_table **pp_tonga_pcie_table,
		PPTable_Generic_SubTable_Header const *ptable
		)
{
	uint32_t i, pcie_count;
	phm_ppt_v1_pcie_table *pcie_table;
	struct phm_ppt_v1_information *pp_table_information =
		(struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_pcie_record *pcie_record;

	if (ptable->ucRevId < 1) {
		const ATOM_Tonga_PCIE_Table *atom_pcie_table = (ATOM_Tonga_PCIE_Table *)ptable;
		ATOM_Tonga_PCIE_Record *atom_pcie_record;

		PP_ASSERT_WITH_CODE((atom_pcie_table->ucNumEntries != 0),
			"Invalid PowerPlay Table!", return -1);

		pcie_table = kzalloc(struct_size(pcie_table, entries,
						 atom_pcie_table->ucNumEntries),
				     GFP_KERNEL);
		if (!pcie_table)
			return -ENOMEM;

		/*
		* Make sure the number of pcie entries are less than or equal to sclk dpm levels.
		* Since first PCIE entry is for ULV, #pcie has to be <= SclkLevel + 1.
		*/
		pcie_count = (pp_table_information->vdd_dep_on_sclk->count) + 1;
		if ((uint32_t)atom_pcie_table->ucNumEntries <= pcie_count)
			pcie_count = (uint32_t)atom_pcie_table->ucNumEntries;
		else
			pr_err("Number of Pcie Entries exceed the number of SCLK Dpm Levels! Disregarding the excess entries...\n");

		pcie_table->count = pcie_count;
		for (i = 0; i < pcie_count; i++) {
			pcie_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						phm_ppt_v1_pcie_record,
						entries, pcie_table, i);
			atom_pcie_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						ATOM_Tonga_PCIE_Record,
						entries, atom_pcie_table, i);
			pcie_record->gen_speed = atom_pcie_record->ucPCIEGenSpeed;
			pcie_record->lane_width = le16_to_cpu(atom_pcie_record->usPCIELaneWidth);
		}

		*pp_tonga_pcie_table = pcie_table;
	} else {
		/* Polaris10/Polaris11 and newer. */
		const ATOM_Polaris10_PCIE_Table *atom_pcie_table = (ATOM_Polaris10_PCIE_Table *)ptable;
		ATOM_Polaris10_PCIE_Record *atom_pcie_record;

		PP_ASSERT_WITH_CODE((atom_pcie_table->ucNumEntries != 0),
			"Invalid PowerPlay Table!", return -1);

		pcie_table = kzalloc(struct_size(pcie_table, entries,
						 atom_pcie_table->ucNumEntries),
				     GFP_KERNEL);
		if (!pcie_table)
			return -ENOMEM;

		/*
		* Make sure the number of pcie entries are less than or equal to sclk dpm levels.
		* Since first PCIE entry is for ULV, #pcie has to be <= SclkLevel + 1.
		*/
		pcie_count = (pp_table_information->vdd_dep_on_sclk->count) + 1;
		if ((uint32_t)atom_pcie_table->ucNumEntries <= pcie_count)
			pcie_count = (uint32_t)atom_pcie_table->ucNumEntries;
		else
			pr_err("Number of Pcie Entries exceed the number of SCLK Dpm Levels! Disregarding the excess entries...\n");

		pcie_table->count = pcie_count;

		for (i = 0; i < pcie_count; i++) {
			pcie_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						phm_ppt_v1_pcie_record,
						entries, pcie_table, i);
			atom_pcie_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						ATOM_Polaris10_PCIE_Record,
						entries, atom_pcie_table, i);
			pcie_record->gen_speed = atom_pcie_record->ucPCIEGenSpeed;
			pcie_record->lane_width = le16_to_cpu(atom_pcie_record->usPCIELaneWidth);
			pcie_record->pcie_sclk = le32_to_cpu(atom_pcie_record->ulPCIE_Sclk);
		}

		*pp_tonga_pcie_table = pcie_table;
	}

	return 0;
}

static int get_cac_tdp_table(
		struct pp_hwmgr *hwmgr,
		struct phm_cac_tdp_table **cac_tdp_table,
		const PPTable_Generic_SubTable_Header * table
		)
{
	uint32_t table_size;
	struct phm_cac_tdp_table *tdp_table;

	table_size = sizeof(uint32_t) + sizeof(struct phm_cac_tdp_table);
	tdp_table = kzalloc(table_size, GFP_KERNEL);

	if (NULL == tdp_table)
		return -ENOMEM;

	hwmgr->dyn_state.cac_dtp_table = kzalloc(table_size, GFP_KERNEL);

	if (NULL == hwmgr->dyn_state.cac_dtp_table) {
		kfree(tdp_table);
		return -ENOMEM;
	}

	if (table->ucRevId < 3) {
		const ATOM_Tonga_PowerTune_Table *tonga_table =
			(ATOM_Tonga_PowerTune_Table *)table;
		tdp_table->usTDP = le16_to_cpu(tonga_table->usTDP);
		tdp_table->usConfigurableTDP =
			le16_to_cpu(tonga_table->usConfigurableTDP);
		tdp_table->usTDC = le16_to_cpu(tonga_table->usTDC);
		tdp_table->usBatteryPowerLimit =
			le16_to_cpu(tonga_table->usBatteryPowerLimit);
		tdp_table->usSmallPowerLimit =
			le16_to_cpu(tonga_table->usSmallPowerLimit);
		tdp_table->usLowCACLeakage =
			le16_to_cpu(tonga_table->usLowCACLeakage);
		tdp_table->usHighCACLeakage =
			le16_to_cpu(tonga_table->usHighCACLeakage);
		tdp_table->usMaximumPowerDeliveryLimit =
			le16_to_cpu(tonga_table->usMaximumPowerDeliveryLimit);
		tdp_table->usDefaultTargetOperatingTemp =
			le16_to_cpu(tonga_table->usTjMax);
		tdp_table->usTargetOperatingTemp =
			le16_to_cpu(tonga_table->usTjMax); /*Set the initial temp to the same as default */
		tdp_table->usPowerTuneDataSetID =
			le16_to_cpu(tonga_table->usPowerTuneDataSetID);
		tdp_table->usSoftwareShutdownTemp =
			le16_to_cpu(tonga_table->usSoftwareShutdownTemp);
		tdp_table->usClockStretchAmount =
			le16_to_cpu(tonga_table->usClockStretchAmount);
	} else if (table->ucRevId < 4) {   /* Fiji and newer */
		const ATOM_Fiji_PowerTune_Table *fijitable =
			(ATOM_Fiji_PowerTune_Table *)table;
		tdp_table->usTDP = le16_to_cpu(fijitable->usTDP);
		tdp_table->usConfigurableTDP = le16_to_cpu(fijitable->usConfigurableTDP);
		tdp_table->usTDC = le16_to_cpu(fijitable->usTDC);
		tdp_table->usBatteryPowerLimit = le16_to_cpu(fijitable->usBatteryPowerLimit);
		tdp_table->usSmallPowerLimit = le16_to_cpu(fijitable->usSmallPowerLimit);
		tdp_table->usLowCACLeakage = le16_to_cpu(fijitable->usLowCACLeakage);
		tdp_table->usHighCACLeakage = le16_to_cpu(fijitable->usHighCACLeakage);
		tdp_table->usMaximumPowerDeliveryLimit =
			le16_to_cpu(fijitable->usMaximumPowerDeliveryLimit);
		tdp_table->usDefaultTargetOperatingTemp =
			le16_to_cpu(fijitable->usTjMax);
		tdp_table->usTargetOperatingTemp =
			le16_to_cpu(fijitable->usTjMax); /*Set the initial temp to the same as default */
		tdp_table->usPowerTuneDataSetID =
			le16_to_cpu(fijitable->usPowerTuneDataSetID);
		tdp_table->usSoftwareShutdownTemp =
			le16_to_cpu(fijitable->usSoftwareShutdownTemp);
		tdp_table->usClockStretchAmount =
			le16_to_cpu(fijitable->usClockStretchAmount);
		tdp_table->usTemperatureLimitHotspot =
			le16_to_cpu(fijitable->usTemperatureLimitHotspot);
		tdp_table->usTemperatureLimitLiquid1 =
			le16_to_cpu(fijitable->usTemperatureLimitLiquid1);
		tdp_table->usTemperatureLimitLiquid2 =
			le16_to_cpu(fijitable->usTemperatureLimitLiquid2);
		tdp_table->usTemperatureLimitVrVddc =
			le16_to_cpu(fijitable->usTemperatureLimitVrVddc);
		tdp_table->usTemperatureLimitVrMvdd =
			le16_to_cpu(fijitable->usTemperatureLimitVrMvdd);
		tdp_table->usTemperatureLimitPlx =
			le16_to_cpu(fijitable->usTemperatureLimitPlx);
		tdp_table->ucLiquid1_I2C_address =
			fijitable->ucLiquid1_I2C_address;
		tdp_table->ucLiquid2_I2C_address =
			fijitable->ucLiquid2_I2C_address;
		tdp_table->ucLiquid_I2C_Line =
			fijitable->ucLiquid_I2C_Line;
		tdp_table->ucVr_I2C_address = fijitable->ucVr_I2C_address;
		tdp_table->ucVr_I2C_Line = fijitable->ucVr_I2C_Line;
		tdp_table->ucPlx_I2C_address = fijitable->ucPlx_I2C_address;
		tdp_table->ucPlx_I2C_Line = fijitable->ucPlx_I2C_Line;
	} else {
		const ATOM_Polaris_PowerTune_Table *polaristable =
			(ATOM_Polaris_PowerTune_Table *)table;
		tdp_table->usTDP = le16_to_cpu(polaristable->usTDP);
		tdp_table->usConfigurableTDP = le16_to_cpu(polaristable->usConfigurableTDP);
		tdp_table->usTDC = le16_to_cpu(polaristable->usTDC);
		tdp_table->usBatteryPowerLimit = le16_to_cpu(polaristable->usBatteryPowerLimit);
		tdp_table->usSmallPowerLimit = le16_to_cpu(polaristable->usSmallPowerLimit);
		tdp_table->usLowCACLeakage = le16_to_cpu(polaristable->usLowCACLeakage);
		tdp_table->usHighCACLeakage = le16_to_cpu(polaristable->usHighCACLeakage);
		tdp_table->usMaximumPowerDeliveryLimit =
			le16_to_cpu(polaristable->usMaximumPowerDeliveryLimit);
		tdp_table->usDefaultTargetOperatingTemp =
			le16_to_cpu(polaristable->usTjMax);
		tdp_table->usTargetOperatingTemp =
			le16_to_cpu(polaristable->usTjMax); /*Set the initial temp to the same as default */
		tdp_table->usPowerTuneDataSetID =
			le16_to_cpu(polaristable->usPowerTuneDataSetID);
		tdp_table->usSoftwareShutdownTemp =
			le16_to_cpu(polaristable->usSoftwareShutdownTemp);
		tdp_table->usClockStretchAmount =
			le16_to_cpu(polaristable->usClockStretchAmount);
		tdp_table->usTemperatureLimitHotspot =
			le16_to_cpu(polaristable->usTemperatureLimitHotspot);
		tdp_table->usTemperatureLimitLiquid1 =
			le16_to_cpu(polaristable->usTemperatureLimitLiquid1);
		tdp_table->usTemperatureLimitLiquid2 =
			le16_to_cpu(polaristable->usTemperatureLimitLiquid2);
		tdp_table->usTemperatureLimitVrVddc =
			le16_to_cpu(polaristable->usTemperatureLimitVrVddc);
		tdp_table->usTemperatureLimitVrMvdd =
			le16_to_cpu(polaristable->usTemperatureLimitVrMvdd);
		tdp_table->usTemperatureLimitPlx =
			le16_to_cpu(polaristable->usTemperatureLimitPlx);
		tdp_table->ucLiquid1_I2C_address =
			polaristable->ucLiquid1_I2C_address;
		tdp_table->ucLiquid2_I2C_address =
			polaristable->ucLiquid2_I2C_address;
		tdp_table->ucLiquid_I2C_Line =
			polaristable->ucLiquid_I2C_Line;
		tdp_table->ucVr_I2C_address = polaristable->ucVr_I2C_address;
		tdp_table->ucVr_I2C_Line = polaristable->ucVr_I2C_Line;
		tdp_table->ucPlx_I2C_address = polaristable->ucPlx_I2C_address;
		tdp_table->ucPlx_I2C_Line = polaristable->ucPlx_I2C_Line;
		tdp_table->usBoostPowerLimit = polaristable->usBoostPowerLimit;
		tdp_table->ucCKS_LDO_REFSEL = polaristable->ucCKS_LDO_REFSEL;
		tdp_table->ucHotSpotOnly = polaristable->ucHotSpotOnly;
	}

	*cac_tdp_table = tdp_table;

	return 0;
}

static int get_mm_clock_voltage_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_mm_clock_voltage_dependency_table **tonga_mm_table,
		const ATOM_Tonga_MM_Dependency_Table * mm_dependency_table
		)
{
	uint32_t i;
	const ATOM_Tonga_MM_Dependency_Record *mm_dependency_record;
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table;
	phm_ppt_v1_mm_clock_voltage_dependency_record *mm_table_record;

	PP_ASSERT_WITH_CODE((0 != mm_dependency_table->ucNumEntries),
		"Invalid PowerPlay Table!", return -1);
	mm_table = kzalloc(struct_size(mm_table, entries, mm_dependency_table->ucNumEntries),
			   GFP_KERNEL);
	if (!mm_table)
		return -ENOMEM;

	mm_table->count = mm_dependency_table->ucNumEntries;

	for (i = 0; i < mm_dependency_table->ucNumEntries; i++) {
		mm_dependency_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						ATOM_Tonga_MM_Dependency_Record,
						entries, mm_dependency_table, i);
		mm_table_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					phm_ppt_v1_mm_clock_voltage_dependency_record,
					entries, mm_table, i);
		mm_table_record->vddcInd = mm_dependency_record->ucVddcInd;
		mm_table_record->vddgfx_offset = le16_to_cpu(mm_dependency_record->usVddgfxOffset);
		mm_table_record->aclk = le32_to_cpu(mm_dependency_record->ulAClk);
		mm_table_record->samclock = le32_to_cpu(mm_dependency_record->ulSAMUClk);
		mm_table_record->eclk = le32_to_cpu(mm_dependency_record->ulEClk);
		mm_table_record->vclk = le32_to_cpu(mm_dependency_record->ulVClk);
		mm_table_record->dclk = le32_to_cpu(mm_dependency_record->ulDClk);
	}

	*tonga_mm_table = mm_table;

	return 0;
}

static int get_gpio_table(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_gpio_table **pp_tonga_gpio_table,
		const ATOM_Tonga_GPIO_Table *atom_gpio_table)
{
	uint32_t table_size;
	struct phm_ppt_v1_gpio_table *pp_gpio_table;
	struct phm_ppt_v1_information *pp_table_information =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	table_size = sizeof(struct phm_ppt_v1_gpio_table);
	pp_gpio_table = kzalloc(table_size, GFP_KERNEL);
	if (!pp_gpio_table)
		return -ENOMEM;

	if (pp_table_information->vdd_dep_on_sclk->count <
			atom_gpio_table->ucVRHotTriggeredSclkDpmIndex)
		PP_ASSERT_WITH_CODE(false,
				"SCLK DPM index for VRHot cannot exceed the total sclk level count!",);
	else
		pp_gpio_table->vrhot_triggered_sclk_dpm_index =
				atom_gpio_table->ucVRHotTriggeredSclkDpmIndex;

	*pp_tonga_gpio_table = pp_gpio_table;

	return 0;
}
/**
 * Private Function used during initialization.
 * Initialize clock voltage dependency
 * @param hwmgr Pointer to the hardware manager.
 * @param powerplay_table Pointer to the PowerPlay Table.
 */
static int init_clock_voltage_dependency(
		struct pp_hwmgr *hwmgr,
		const ATOM_Tonga_POWERPLAYTABLE *powerplay_table
		)
{
	int result = 0;
	struct phm_ppt_v1_information *pp_table_information =
		(struct phm_ppt_v1_information *)(hwmgr->pptable);

	const ATOM_Tonga_MM_Dependency_Table *mm_dependency_table =
		(const ATOM_Tonga_MM_Dependency_Table *)(((unsigned long) powerplay_table) +
		le16_to_cpu(powerplay_table->usMMDependencyTableOffset));
	const PPTable_Generic_SubTable_Header *pPowerTuneTable =
		(const PPTable_Generic_SubTable_Header *)(((unsigned long) powerplay_table) +
		le16_to_cpu(powerplay_table->usPowerTuneTableOffset));
	const ATOM_Tonga_MCLK_Dependency_Table *mclk_dep_table =
		(const ATOM_Tonga_MCLK_Dependency_Table *)(((unsigned long) powerplay_table) +
		le16_to_cpu(powerplay_table->usMclkDependencyTableOffset));
	const PPTable_Generic_SubTable_Header *sclk_dep_table =
		(const PPTable_Generic_SubTable_Header *)(((unsigned long) powerplay_table) +
		le16_to_cpu(powerplay_table->usSclkDependencyTableOffset));
	const ATOM_Tonga_Hard_Limit_Table *pHardLimits =
		(const ATOM_Tonga_Hard_Limit_Table *)(((unsigned long) powerplay_table) +
		le16_to_cpu(powerplay_table->usHardLimitTableOffset));
	const PPTable_Generic_SubTable_Header *pcie_table =
		(const PPTable_Generic_SubTable_Header *)(((unsigned long) powerplay_table) +
		le16_to_cpu(powerplay_table->usPCIETableOffset));
	const ATOM_Tonga_GPIO_Table *gpio_table =
		(const ATOM_Tonga_GPIO_Table *)(((unsigned long) powerplay_table) +
		le16_to_cpu(powerplay_table->usGPIOTableOffset));

	pp_table_information->vdd_dep_on_sclk = NULL;
	pp_table_information->vdd_dep_on_mclk = NULL;
	pp_table_information->mm_dep_table = NULL;
	pp_table_information->pcie_table = NULL;
	pp_table_information->gpio_table = NULL;

	if (powerplay_table->usMMDependencyTableOffset != 0)
		result = get_mm_clock_voltage_table(hwmgr,
		&pp_table_information->mm_dep_table, mm_dependency_table);

	if (result == 0 && powerplay_table->usPowerTuneTableOffset != 0)
		result = get_cac_tdp_table(hwmgr,
		&pp_table_information->cac_dtp_table, pPowerTuneTable);

	if (result == 0 && powerplay_table->usSclkDependencyTableOffset != 0)
		result = get_sclk_voltage_dependency_table(hwmgr,
		&pp_table_information->vdd_dep_on_sclk, sclk_dep_table);

	if (result == 0 && powerplay_table->usMclkDependencyTableOffset != 0)
		result = get_mclk_voltage_dependency_table(hwmgr,
		&pp_table_information->vdd_dep_on_mclk, mclk_dep_table);

	if (result == 0 && powerplay_table->usPCIETableOffset != 0)
		result = get_pcie_table(hwmgr,
		&pp_table_information->pcie_table, pcie_table);

	if (result == 0 && powerplay_table->usHardLimitTableOffset != 0)
		result = get_hard_limits(hwmgr,
		&pp_table_information->max_clock_voltage_on_dc, pHardLimits);

	hwmgr->dyn_state.max_clock_voltage_on_dc.sclk =
		pp_table_information->max_clock_voltage_on_dc.sclk;
	hwmgr->dyn_state.max_clock_voltage_on_dc.mclk =
		pp_table_information->max_clock_voltage_on_dc.mclk;
	hwmgr->dyn_state.max_clock_voltage_on_dc.vddc =
		pp_table_information->max_clock_voltage_on_dc.vddc;
	hwmgr->dyn_state.max_clock_voltage_on_dc.vddci =
		pp_table_information->max_clock_voltage_on_dc.vddci;

	if (result == 0 && (NULL != pp_table_information->vdd_dep_on_mclk)
		&& (0 != pp_table_information->vdd_dep_on_mclk->count))
		result = get_valid_clk(hwmgr, &pp_table_information->valid_mclk_values,
		pp_table_information->vdd_dep_on_mclk);

	if (result == 0 && (NULL != pp_table_information->vdd_dep_on_sclk)
		&& (0 != pp_table_information->vdd_dep_on_sclk->count))
		result = get_valid_clk(hwmgr, &pp_table_information->valid_sclk_values,
		pp_table_information->vdd_dep_on_sclk);

	if (!result && gpio_table)
		result = get_gpio_table(hwmgr, &pp_table_information->gpio_table,
				gpio_table);

	return result;
}

/** Retrieves the (signed) Overdrive limits from VBIOS.
 * The max engine clock, memory clock and max temperature come from the firmware info table.
 *
 * The information is placed into the platform descriptor.
 *
 * @param hwmgr source of the VBIOS table and owner of the platform descriptor to be updated.
 * @param powerplay_table the address of the PowerPlay table.
 *
 * @return 1 as long as the firmware info table was present and of a supported version.
 */
static int init_over_drive_limits(
		struct pp_hwmgr *hwmgr,
		const ATOM_Tonga_POWERPLAYTABLE *powerplay_table)
{
	hwmgr->platform_descriptor.overdriveLimit.engineClock =
		le32_to_cpu(powerplay_table->ulMaxODEngineClock);
	hwmgr->platform_descriptor.overdriveLimit.memoryClock =
		le32_to_cpu(powerplay_table->ulMaxODMemoryClock);

	hwmgr->platform_descriptor.minOverdriveVDDC = 0;
	hwmgr->platform_descriptor.maxOverdriveVDDC = 0;
	hwmgr->platform_descriptor.overdriveVDDCStep = 0;

	return 0;
}

/**
 * Private Function used during initialization.
 * Inspect the PowerPlay table for obvious signs of corruption.
 * @param hwmgr Pointer to the hardware manager.
 * @param powerplay_table Pointer to the PowerPlay Table.
 * @exception This implementation always returns 1.
 */
static int init_thermal_controller(
		struct pp_hwmgr *hwmgr,
		const ATOM_Tonga_POWERPLAYTABLE *powerplay_table
		)
{
	const PPTable_Generic_SubTable_Header *fan_table;
	ATOM_Tonga_Thermal_Controller *thermal_controller;

	thermal_controller = (ATOM_Tonga_Thermal_Controller *)
		(((unsigned long)powerplay_table) +
		le16_to_cpu(powerplay_table->usThermalControllerOffset));
	PP_ASSERT_WITH_CODE((0 != powerplay_table->usThermalControllerOffset),
		"Thermal controller table not set!", return -1);

	hwmgr->thermal_controller.ucType = thermal_controller->ucType;
	hwmgr->thermal_controller.ucI2cLine = thermal_controller->ucI2cLine;
	hwmgr->thermal_controller.ucI2cAddress = thermal_controller->ucI2cAddress;

	hwmgr->thermal_controller.fanInfo.bNoFan =
		(0 != (thermal_controller->ucFanParameters & ATOM_TONGA_PP_FANPARAMETERS_NOFAN));

	hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution =
		thermal_controller->ucFanParameters &
		ATOM_TONGA_PP_FANPARAMETERS_TACHOMETER_PULSES_PER_REVOLUTION_MASK;

	hwmgr->thermal_controller.fanInfo.ulMinRPM
		= thermal_controller->ucFanMinRPM * 100UL;
	hwmgr->thermal_controller.fanInfo.ulMaxRPM
		= thermal_controller->ucFanMaxRPM * 100UL;

	set_hw_cap(
			hwmgr,
			ATOM_TONGA_PP_THERMALCONTROLLER_NONE != hwmgr->thermal_controller.ucType,
			PHM_PlatformCaps_ThermalController
		  );

	if (0 == powerplay_table->usFanTableOffset) {
		hwmgr->thermal_controller.use_hw_fan_control = 1;
		return 0;
	}

	fan_table = (const PPTable_Generic_SubTable_Header *)
		(((unsigned long)powerplay_table) +
		le16_to_cpu(powerplay_table->usFanTableOffset));

	PP_ASSERT_WITH_CODE((0 != powerplay_table->usFanTableOffset),
		"Fan table not set!", return -1);
	PP_ASSERT_WITH_CODE((0 < fan_table->ucRevId),
		"Unsupported fan table format!", return -1);

	hwmgr->thermal_controller.advanceFanControlParameters.ulCycleDelay
		= 100000;
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_MicrocodeFanControl);

	if (fan_table->ucRevId < 8) {
		const ATOM_Tonga_Fan_Table *tonga_fan_table =
			(ATOM_Tonga_Fan_Table *)fan_table;
		hwmgr->thermal_controller.advanceFanControlParameters.ucTHyst
			= tonga_fan_table->ucTHyst;
		hwmgr->thermal_controller.advanceFanControlParameters.usTMin
			= le16_to_cpu(tonga_fan_table->usTMin);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMed
			= le16_to_cpu(tonga_fan_table->usTMed);
		hwmgr->thermal_controller.advanceFanControlParameters.usTHigh
			= le16_to_cpu(tonga_fan_table->usTHigh);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin
			= le16_to_cpu(tonga_fan_table->usPWMMin);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed
			= le16_to_cpu(tonga_fan_table->usPWMMed);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh
			= le16_to_cpu(tonga_fan_table->usPWMHigh);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMax
			= 10900;                  /* hard coded */
		hwmgr->thermal_controller.advanceFanControlParameters.usTMax
			= le16_to_cpu(tonga_fan_table->usTMax);
		hwmgr->thermal_controller.advanceFanControlParameters.ucFanControlMode
			= tonga_fan_table->ucFanControlMode;
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanPWM
			= le16_to_cpu(tonga_fan_table->usFanPWMMax);
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultFanOutputSensitivity
			= 4836;
		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity
			= le16_to_cpu(tonga_fan_table->usFanOutputSensitivity);
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanRPM
			= le16_to_cpu(tonga_fan_table->usFanRPMMax);
		hwmgr->thermal_controller.advanceFanControlParameters.ulMinFanSCLKAcousticLimit
			= (le32_to_cpu(tonga_fan_table->ulMinFanSCLKAcousticLimit) / 100); /* PPTable stores it in 10Khz unit for 2 decimal places.  SMC wants MHz. */
		hwmgr->thermal_controller.advanceFanControlParameters.ucTargetTemperature
			= tonga_fan_table->ucTargetTemperature;
		hwmgr->thermal_controller.advanceFanControlParameters.ucMinimumPWMLimit
			= tonga_fan_table->ucMinimumPWMLimit;
	} else if (fan_table->ucRevId == 8) {
		const ATOM_Fiji_Fan_Table *fiji_fan_table =
			(ATOM_Fiji_Fan_Table *)fan_table;
		hwmgr->thermal_controller.advanceFanControlParameters.ucTHyst
			= fiji_fan_table->ucTHyst;
		hwmgr->thermal_controller.advanceFanControlParameters.usTMin
			= le16_to_cpu(fiji_fan_table->usTMin);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMed
			= le16_to_cpu(fiji_fan_table->usTMed);
		hwmgr->thermal_controller.advanceFanControlParameters.usTHigh
			= le16_to_cpu(fiji_fan_table->usTHigh);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin
			= le16_to_cpu(fiji_fan_table->usPWMMin);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed
			= le16_to_cpu(fiji_fan_table->usPWMMed);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh
			= le16_to_cpu(fiji_fan_table->usPWMHigh);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMax
			= le16_to_cpu(fiji_fan_table->usTMax);
		hwmgr->thermal_controller.advanceFanControlParameters.ucFanControlMode
			= fiji_fan_table->ucFanControlMode;
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanPWM
			= le16_to_cpu(fiji_fan_table->usFanPWMMax);
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultFanOutputSensitivity
			= 4836;
		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity
			= le16_to_cpu(fiji_fan_table->usFanOutputSensitivity);
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanRPM
			= le16_to_cpu(fiji_fan_table->usFanRPMMax);
		hwmgr->thermal_controller.advanceFanControlParameters.ulMinFanSCLKAcousticLimit
			= (le32_to_cpu(fiji_fan_table->ulMinFanSCLKAcousticLimit) / 100); /* PPTable stores it in 10Khz unit for 2 decimal places.  SMC wants MHz. */
		hwmgr->thermal_controller.advanceFanControlParameters.ucTargetTemperature
			= fiji_fan_table->ucTargetTemperature;
		hwmgr->thermal_controller.advanceFanControlParameters.ucMinimumPWMLimit
			= fiji_fan_table->ucMinimumPWMLimit;

		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainEdge
			= le16_to_cpu(fiji_fan_table->usFanGainEdge);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHotspot
			= le16_to_cpu(fiji_fan_table->usFanGainHotspot);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainLiquid
			= le16_to_cpu(fiji_fan_table->usFanGainLiquid);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrVddc
			= le16_to_cpu(fiji_fan_table->usFanGainVrVddc);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrMvdd
			= le16_to_cpu(fiji_fan_table->usFanGainVrMvdd);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainPlx
			= le16_to_cpu(fiji_fan_table->usFanGainPlx);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHbm
			= le16_to_cpu(fiji_fan_table->usFanGainHbm);
	} else if (fan_table->ucRevId >= 9) {
		const ATOM_Polaris_Fan_Table *polaris_fan_table =
			(ATOM_Polaris_Fan_Table *)fan_table;
		hwmgr->thermal_controller.advanceFanControlParameters.ucTHyst
			= polaris_fan_table->ucTHyst;
		hwmgr->thermal_controller.advanceFanControlParameters.usTMin
			= le16_to_cpu(polaris_fan_table->usTMin);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMed
			= le16_to_cpu(polaris_fan_table->usTMed);
		hwmgr->thermal_controller.advanceFanControlParameters.usTHigh
			= le16_to_cpu(polaris_fan_table->usTHigh);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin
			= le16_to_cpu(polaris_fan_table->usPWMMin);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed
			= le16_to_cpu(polaris_fan_table->usPWMMed);
		hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh
			= le16_to_cpu(polaris_fan_table->usPWMHigh);
		hwmgr->thermal_controller.advanceFanControlParameters.usTMax
			= le16_to_cpu(polaris_fan_table->usTMax);
		hwmgr->thermal_controller.advanceFanControlParameters.ucFanControlMode
			= polaris_fan_table->ucFanControlMode;
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanPWM
			= le16_to_cpu(polaris_fan_table->usFanPWMMax);
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultFanOutputSensitivity
			= 4836;
		hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity
			= le16_to_cpu(polaris_fan_table->usFanOutputSensitivity);
		hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanRPM
			= le16_to_cpu(polaris_fan_table->usFanRPMMax);
		hwmgr->thermal_controller.advanceFanControlParameters.ulMinFanSCLKAcousticLimit
			= (le32_to_cpu(polaris_fan_table->ulMinFanSCLKAcousticLimit) / 100); /* PPTable stores it in 10Khz unit for 2 decimal places.  SMC wants MHz. */
		hwmgr->thermal_controller.advanceFanControlParameters.ucTargetTemperature
			= polaris_fan_table->ucTargetTemperature;
		hwmgr->thermal_controller.advanceFanControlParameters.ucMinimumPWMLimit
			= polaris_fan_table->ucMinimumPWMLimit;

		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainEdge
			= le16_to_cpu(polaris_fan_table->usFanGainEdge);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHotspot
			= le16_to_cpu(polaris_fan_table->usFanGainHotspot);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainLiquid
			= le16_to_cpu(polaris_fan_table->usFanGainLiquid);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrVddc
			= le16_to_cpu(polaris_fan_table->usFanGainVrVddc);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainVrMvdd
			= le16_to_cpu(polaris_fan_table->usFanGainVrMvdd);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainPlx
			= le16_to_cpu(polaris_fan_table->usFanGainPlx);
		hwmgr->thermal_controller.advanceFanControlParameters.usFanGainHbm
			= le16_to_cpu(polaris_fan_table->usFanGainHbm);
		hwmgr->thermal_controller.advanceFanControlParameters.ucEnableZeroRPM
			= le16_to_cpu(polaris_fan_table->ucEnableZeroRPM);
		hwmgr->thermal_controller.advanceFanControlParameters.ucFanStopTemperature
			= le16_to_cpu(polaris_fan_table->ucFanStopTemperature);
		hwmgr->thermal_controller.advanceFanControlParameters.ucFanStartTemperature
			= le16_to_cpu(polaris_fan_table->ucFanStartTemperature);
	}

	return 0;
}

/**
 * Private Function used during initialization.
 * Inspect the PowerPlay table for obvious signs of corruption.
 * @param hwmgr Pointer to the hardware manager.
 * @param powerplay_table Pointer to the PowerPlay Table.
 * @exception 2 if the powerplay table is incorrect.
 */
static int check_powerplay_tables(
		struct pp_hwmgr *hwmgr,
		const ATOM_Tonga_POWERPLAYTABLE *powerplay_table
		)
{
	const ATOM_Tonga_State_Array *state_arrays;

	state_arrays = (ATOM_Tonga_State_Array *)(((unsigned long)powerplay_table) +
		le16_to_cpu(powerplay_table->usStateArrayOffset));

	PP_ASSERT_WITH_CODE((ATOM_Tonga_TABLE_REVISION_TONGA <=
		powerplay_table->sHeader.ucTableFormatRevision),
		"Unsupported PPTable format!", return -1);
	PP_ASSERT_WITH_CODE((0 != powerplay_table->usStateArrayOffset),
		"State table is not set!", return -1);
	PP_ASSERT_WITH_CODE((0 < powerplay_table->sHeader.usStructureSize),
		"Invalid PowerPlay Table!", return -1);
	PP_ASSERT_WITH_CODE((0 < state_arrays->ucNumEntries),
		"Invalid PowerPlay Table!", return -1);

	return 0;
}

static int pp_tables_v1_0_initialize(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	const ATOM_Tonga_POWERPLAYTABLE *powerplay_table;

	hwmgr->pptable = kzalloc(sizeof(struct phm_ppt_v1_information), GFP_KERNEL);

	PP_ASSERT_WITH_CODE((NULL != hwmgr->pptable),
			    "Failed to allocate hwmgr->pptable!", return -ENOMEM);

	powerplay_table = get_powerplay_table(hwmgr);

	PP_ASSERT_WITH_CODE((NULL != powerplay_table),
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

	result = init_clock_voltage_dependency(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_clock_voltage_dependency failed", return result);

	result = init_dpm_2_parameters(hwmgr, powerplay_table);

	PP_ASSERT_WITH_CODE((result == 0),
			    "init_dpm_2_parameters failed", return result);

	return result;
}

static int pp_tables_v1_0_uninitialize(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v1_information *pp_table_information =
		(struct phm_ppt_v1_information *)(hwmgr->pptable);

	kfree(pp_table_information->vdd_dep_on_sclk);
	pp_table_information->vdd_dep_on_sclk = NULL;

	kfree(pp_table_information->vdd_dep_on_mclk);
	pp_table_information->vdd_dep_on_mclk = NULL;

	kfree(pp_table_information->valid_mclk_values);
	pp_table_information->valid_mclk_values = NULL;

	kfree(pp_table_information->valid_sclk_values);
	pp_table_information->valid_sclk_values = NULL;

	kfree(pp_table_information->vddc_lookup_table);
	pp_table_information->vddc_lookup_table = NULL;

	kfree(pp_table_information->vddgfx_lookup_table);
	pp_table_information->vddgfx_lookup_table = NULL;

	kfree(pp_table_information->mm_dep_table);
	pp_table_information->mm_dep_table = NULL;

	kfree(pp_table_information->cac_dtp_table);
	pp_table_information->cac_dtp_table = NULL;

	kfree(hwmgr->dyn_state.cac_dtp_table);
	hwmgr->dyn_state.cac_dtp_table = NULL;

	kfree(pp_table_information->ppm_parameter_table);
	pp_table_information->ppm_parameter_table = NULL;

	kfree(pp_table_information->pcie_table);
	pp_table_information->pcie_table = NULL;

	kfree(pp_table_information->gpio_table);
	pp_table_information->gpio_table = NULL;

	kfree(hwmgr->pptable);
	hwmgr->pptable = NULL;

	return 0;
}

const struct pp_table_func pptable_v1_0_funcs = {
	.pptable_init = pp_tables_v1_0_initialize,
	.pptable_fini = pp_tables_v1_0_uninitialize,
};

int get_number_of_powerplay_table_entries_v1_0(struct pp_hwmgr *hwmgr)
{
	ATOM_Tonga_State_Array const *state_arrays;
	const ATOM_Tonga_POWERPLAYTABLE *pp_table = get_powerplay_table(hwmgr);

	PP_ASSERT_WITH_CODE((NULL != pp_table),
			"Missing PowerPlay Table!", return -1);
	PP_ASSERT_WITH_CODE((pp_table->sHeader.ucTableFormatRevision >=
			ATOM_Tonga_TABLE_REVISION_TONGA),
			"Incorrect PowerPlay table revision!", return -1);

	state_arrays = (ATOM_Tonga_State_Array *)(((unsigned long)pp_table) +
			le16_to_cpu(pp_table->usStateArrayOffset));

	return (uint32_t)(state_arrays->ucNumEntries);
}

/**
* Private function to convert flags stored in the BIOS to software flags in PowerPlay.
*/
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

static int ppt_get_num_of_vce_state_table_entries_v1_0(struct pp_hwmgr *hwmgr)
{
	const ATOM_Tonga_POWERPLAYTABLE *pp_table = get_powerplay_table(hwmgr);
	const ATOM_Tonga_VCE_State_Table *vce_state_table;


	if (pp_table == NULL)
		return 0;

	vce_state_table = (void *)pp_table +
			le16_to_cpu(pp_table->usVCEStateTableOffset);

	return vce_state_table->ucNumEntries;
}

static int ppt_get_vce_state_table_entry_v1_0(struct pp_hwmgr *hwmgr, uint32_t i,
		struct amd_vce_state *vce_state, void **clock_info, uint32_t *flag)
{
	const ATOM_Tonga_VCE_State_Record *vce_state_record;
	ATOM_Tonga_SCLK_Dependency_Record *sclk_dep_record;
	ATOM_Tonga_MCLK_Dependency_Record *mclk_dep_record;
	ATOM_Tonga_MM_Dependency_Record *mm_dep_record;
	const ATOM_Tonga_POWERPLAYTABLE *pptable = get_powerplay_table(hwmgr);
	const ATOM_Tonga_VCE_State_Table *vce_state_table = (ATOM_Tonga_VCE_State_Table *)(((unsigned long)pptable)
							  + le16_to_cpu(pptable->usVCEStateTableOffset));
	const ATOM_Tonga_SCLK_Dependency_Table *sclk_dep_table = (ATOM_Tonga_SCLK_Dependency_Table *)(((unsigned long)pptable)
							  + le16_to_cpu(pptable->usSclkDependencyTableOffset));
	const ATOM_Tonga_MCLK_Dependency_Table *mclk_dep_table = (ATOM_Tonga_MCLK_Dependency_Table *)(((unsigned long)pptable)
							  + le16_to_cpu(pptable->usMclkDependencyTableOffset));
	const ATOM_Tonga_MM_Dependency_Table *mm_dep_table = (ATOM_Tonga_MM_Dependency_Table *)(((unsigned long)pptable)
							  + le16_to_cpu(pptable->usMMDependencyTableOffset));

	PP_ASSERT_WITH_CODE((i < vce_state_table->ucNumEntries),
			 "Requested state entry ID is out of range!",
			 return -EINVAL);

	vce_state_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					ATOM_Tonga_VCE_State_Record,
					entries, vce_state_table, i);
	sclk_dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					ATOM_Tonga_SCLK_Dependency_Record,
					entries, sclk_dep_table,
					vce_state_record->ucSCLKIndex);
	mm_dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					ATOM_Tonga_MM_Dependency_Record,
					entries, mm_dep_table,
					vce_state_record->ucVCEClockIndex);
	*flag = vce_state_record->ucFlag;

	vce_state->evclk = le32_to_cpu(mm_dep_record->ulEClk);
	vce_state->ecclk = le32_to_cpu(mm_dep_record->ulEClk);
	vce_state->sclk = le32_to_cpu(sclk_dep_record->ulSclk);

	if (vce_state_record->ucMCLKIndex >= mclk_dep_table->ucNumEntries)
		mclk_dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					ATOM_Tonga_MCLK_Dependency_Record,
					entries, mclk_dep_table,
					mclk_dep_table->ucNumEntries - 1);
	else
		mclk_dep_record = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
					ATOM_Tonga_MCLK_Dependency_Record,
					entries, mclk_dep_table,
					vce_state_record->ucMCLKIndex);

	vce_state->mclk = le32_to_cpu(mclk_dep_record->ulMclk);
	return 0;
}

/**
* Create a Power State out of an entry in the PowerPlay table.
* This function is called by the hardware back-end.
* @param hwmgr Pointer to the hardware manager.
* @param entry_index The index of the entry to be extracted from the table.
* @param power_state The address of the PowerState instance being created.
* @return -1 if the entry cannot be retrieved.
*/
int get_powerplay_table_entry_v1_0(struct pp_hwmgr *hwmgr,
		uint32_t entry_index, struct pp_power_state *power_state,
		int (*call_back_func)(struct pp_hwmgr *, void *,
				struct pp_power_state *, void *, uint32_t))
{
	int result = 0;
	const ATOM_Tonga_State_Array *state_arrays;
	const ATOM_Tonga_State *state_entry;
	const ATOM_Tonga_POWERPLAYTABLE *pp_table = get_powerplay_table(hwmgr);
	int i, j;
	uint32_t flags = 0;

	PP_ASSERT_WITH_CODE((NULL != pp_table), "Missing PowerPlay Table!", return -1;);
	power_state->classification.bios_index = entry_index;

	if (pp_table->sHeader.ucTableFormatRevision >=
			ATOM_Tonga_TABLE_REVISION_TONGA) {
		state_arrays = (ATOM_Tonga_State_Array *)(((unsigned long)pp_table) +
				le16_to_cpu(pp_table->usStateArrayOffset));

		PP_ASSERT_WITH_CODE((0 < pp_table->usStateArrayOffset),
				"Invalid PowerPlay Table State Array Offset.", return -1);
		PP_ASSERT_WITH_CODE((0 < state_arrays->ucNumEntries),
				"Invalid PowerPlay Table State Array.", return -1);
		PP_ASSERT_WITH_CODE((entry_index <= state_arrays->ucNumEntries),
				"Invalid PowerPlay Table State Array Entry.", return -1);

		state_entry = GET_FLEXIBLE_ARRAY_MEMBER_ADDR(
						ATOM_Tonga_State, entries,
						state_arrays, entry_index);

		result = call_back_func(hwmgr, (void *)state_entry, power_state,
				(void *)pp_table,
				make_classification_flags(hwmgr,
					le16_to_cpu(state_entry->usClassification),
					le16_to_cpu(state_entry->usClassification2)));
	}

	if (!result && (power_state->classification.flags &
			PP_StateClassificationFlag_Boot))
		result = hwmgr->hwmgr_func->patch_boot_state(hwmgr, &(power_state->hardware));

	hwmgr->num_vce_state_tables = i = ppt_get_num_of_vce_state_table_entries_v1_0(hwmgr);

	if ((i != 0) && (i <= AMD_MAX_VCE_LEVELS)) {
		for (j = 0; j < i; j++)
			ppt_get_vce_state_table_entry_v1_0(hwmgr, j, &(hwmgr->vce_states[j]), NULL, &flags);
	}

	return result;
}

