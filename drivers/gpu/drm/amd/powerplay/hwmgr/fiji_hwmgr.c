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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include "linux/delay.h"

#include "hwmgr.h"
#include "fiji_smumgr.h"
#include "atombios.h"
#include "hardwaremanager.h"
#include "ppatomctrl.h"
#include "atombios.h"
#include "cgs_common.h"
#include "fiji_dyn_defaults.h"
#include "fiji_powertune.h"
#include "smu73.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"
#include "pppcielanes.h"
#include "fiji_hwmgr.h"
#include "tonga_processpptables.h"
#include "tonga_pptable.h"
#include "pp_debug.h"
#include "pp_acpi.h"
#include "amd_pcie_helpers.h"
#include "cgs_linux.h"
#include "ppinterrupt.h"

#include "fiji_clockpowergating.h"
#include "fiji_thermal.h"

#define VOLTAGE_SCALE	4
#define SMC_RAM_END		0x40000
#define VDDC_VDDCI_DELTA	300

#define MC_SEQ_MISC0_GDDR5_SHIFT 28
#define MC_SEQ_MISC0_GDDR5_MASK  0xf0000000
#define MC_SEQ_MISC0_GDDR5_VALUE 5

#define MC_CG_ARB_FREQ_F0           0x0a /* boot-up default */
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

/* From smc_reg.h */
#define SMC_CG_IND_START            0xc0030000
#define SMC_CG_IND_END              0xc0040000  /* First byte after SMC_CG_IND */

#define VOLTAGE_SCALE               4
#define VOLTAGE_VID_OFFSET_SCALE1   625
#define VOLTAGE_VID_OFFSET_SCALE2   100

#define VDDC_VDDCI_DELTA            300

#define ixSWRST_COMMAND_1           0x1400103
#define MC_SEQ_CNTL__CAC_EN_MASK    0x40000000

/** Values for the CG_THERMAL_CTRL::DPM_EVENT_SRC field. */
enum DPM_EVENT_SRC {
    DPM_EVENT_SRC_ANALOG = 0,               /* Internal analog trip point */
    DPM_EVENT_SRC_EXTERNAL = 1,             /* External (GPIO 17) signal */
    DPM_EVENT_SRC_DIGITAL = 2,              /* Internal digital trip point (DIG_THERM_DPM) */
    DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,   /* Internal analog or external */
    DPM_EVENT_SRC_DIGITAL_OR_EXTERNAL = 4   /* Internal digital or external */
};


/* [2.5%,~2.5%] Clock stretched is multiple of 2.5% vs
 * not and [Fmin, Fmax, LDO_REFSEL, USE_FOR_LOW_FREQ]
 */
static const uint16_t fiji_clock_stretcher_lookup_table[2][4] =
{ {600, 1050, 3, 0}, {600, 1050, 6, 1} };

/* [FF, SS] type, [] 4 voltage ranges, and
 * [Floor Freq, Boundary Freq, VID min , VID max]
 */
static const uint32_t fiji_clock_stretcher_ddt_table[2][4][4] =
{ { {265, 529, 120, 128}, {325, 650, 96, 119}, {430, 860, 32, 95}, {0, 0, 0, 31} },
  { {275, 550, 104, 112}, {319, 638, 96, 103}, {360, 720, 64, 95}, {384, 768, 32, 63} } };

/* [Use_For_Low_freq] value, [0%, 5%, 10%, 7.14%, 14.28%, 20%]
 * (coming from PWR_CKS_CNTL.stretch_amount reg spec)
 */
static const uint8_t fiji_clock_stretch_amount_conversion[2][6] =
{ {0, 1, 3, 2, 4, 5}, {0, 2, 4, 5, 6, 5} };

static const unsigned long PhwFiji_Magic = (unsigned long)(PHM_VIslands_Magic);

struct fiji_power_state *cast_phw_fiji_power_state(
				  struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwFiji_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (struct fiji_power_state *)hw_ps;
}

const struct fiji_power_state *cast_const_phw_fiji_power_state(
				 const struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwFiji_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL;);

	return (const struct fiji_power_state *)hw_ps;
}

static bool fiji_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	return (1 == PHM_READ_INDIRECT_FIELD(hwmgr->device,
			CGS_IND_REG__SMC, FEATURE_STATUS, VOLTAGE_CONTROLLER_ON))
			? true : false;
}

static void fiji_init_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_ulv_parm *ulv = &data->ulv;

	ulv->cg_ulv_parameter = PPFIJI_CGULVPARAMETER_DFLT;
	data->voting_rights_clients0 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT0;
	data->voting_rights_clients1 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT1;
	data->voting_rights_clients2 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT2;
	data->voting_rights_clients3 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT3;
	data->voting_rights_clients4 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT4;
	data->voting_rights_clients5 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT5;
	data->voting_rights_clients6 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT6;
	data->voting_rights_clients7 = PPFIJI_VOTINGRIGHTSCLIENTS_DFLT7;

	data->static_screen_threshold_unit =
			PPFIJI_STATICSCREENTHRESHOLDUNIT_DFLT;
	data->static_screen_threshold =
			PPFIJI_STATICSCREENTHRESHOLD_DFLT;

	/* Unset ABM cap as it moved to DAL.
	 * Add PHM_PlatformCaps_NonABMSupportInPPLib
	 * for re-direct ABM related request to DAL
	 */
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ABM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_NonABMSupportInPPLib);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicACTiming);

	fiji_initialize_power_tune_defaults(hwmgr);

	data->mclk_stutter_mode_threshold = 60000;
	data->pcie_gen_performance.max = PP_PCIEGen1;
	data->pcie_gen_performance.min = PP_PCIEGen3;
	data->pcie_gen_power_saving.max = PP_PCIEGen1;
	data->pcie_gen_power_saving.min = PP_PCIEGen3;
	data->pcie_lane_performance.max = 0;
	data->pcie_lane_performance.min = 16;
	data->pcie_lane_power_saving.max = 0;
	data->pcie_lane_power_saving.min = 16;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicUVDState);
}

static int fiji_get_sclk_for_voltage_evv(struct pp_hwmgr *hwmgr,
	phm_ppt_v1_voltage_lookup_table *lookup_table,
	uint16_t virtual_voltage_id, int32_t *sclk)
{
	uint8_t entryId;
	uint8_t voltageId;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	PP_ASSERT_WITH_CODE(lookup_table->count != 0, "Lookup table is empty", return -EINVAL);

	/* search for leakage voltage ID 0xff01 ~ 0xff08 and sckl */
	for (entryId = 0; entryId < table_info->vdd_dep_on_sclk->count; entryId++) {
		voltageId = table_info->vdd_dep_on_sclk->entries[entryId].vddInd;
		if (lookup_table->entries[voltageId].us_vdd == virtual_voltage_id)
			break;
	}

	PP_ASSERT_WITH_CODE(entryId < table_info->vdd_dep_on_sclk->count,
			"Can't find requested voltage id in vdd_dep_on_sclk table!",
			return -EINVAL;
			);

	*sclk = table_info->vdd_dep_on_sclk->entries[entryId].clk;

	return 0;
}

/**
* Get Leakage VDDC based on leakage ID.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int fiji_get_evv_voltages(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint16_t    vv_id;
	uint16_t    vddc = 0;
	uint16_t    evv_default = 1150;
	uint16_t    i, j;
	uint32_t  sclk = 0;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *sclk_table =
			table_info->vdd_dep_on_sclk;
	int result;

	for (i = 0; i < FIJI_MAX_LEAKAGE_COUNT; i++) {
		vv_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;
		if (!fiji_get_sclk_for_voltage_evv(hwmgr,
				table_info->vddc_lookup_table, vv_id, &sclk)) {
			if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_ClockStretcher)) {
				for (j = 1; j < sclk_table->count; j++) {
					if (sclk_table->entries[j].clk == sclk &&
							sclk_table->entries[j].cks_enable == 0) {
						sclk += 5000;
						break;
					}
				}
			}

			if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_EnableDriverEVV))
				result = atomctrl_calculate_voltage_evv_on_sclk(hwmgr,
						VOLTAGE_TYPE_VDDC, sclk, vv_id, &vddc, i, true);
			else
				result = -EINVAL;

			if (result)
				result = atomctrl_get_voltage_evv_on_sclk(hwmgr,
						VOLTAGE_TYPE_VDDC, sclk,vv_id, &vddc);

			/* need to make sure vddc is less than 2v or else, it could burn the ASIC. */
			PP_ASSERT_WITH_CODE((vddc < 2000),
					"Invalid VDDC value, greater than 2v!", result = -EINVAL;);

			if (result)
				/* 1.15V is the default safe value for Fiji */
				vddc = evv_default;

			/* the voltage should not be zero nor equal to leakage ID */
			if (vddc != 0 && vddc != vv_id) {
				data->vddc_leakage.actual_voltage
				[data->vddc_leakage.count] = vddc;
				data->vddc_leakage.leakage_id
				[data->vddc_leakage.count] = vv_id;
				data->vddc_leakage.count++;
			}
		}
	}
	return 0;
}

/**
 * Change virtual leakage voltage to actual value.
 *
 * @param     hwmgr  the address of the powerplay hardware manager.
 * @param     pointer to changing voltage
 * @param     pointer to leakage table
 */
static void fiji_patch_with_vdd_leakage(struct pp_hwmgr *hwmgr,
		uint16_t *voltage, struct fiji_leakage_voltage *leakage_table)
{
	uint32_t index;

	/* search for leakage voltage ID 0xff01 ~ 0xff08 */
	for (index = 0; index < leakage_table->count; index++) {
		/* if this voltage matches a leakage voltage ID */
		/* patch with actual leakage voltage */
		if (leakage_table->leakage_id[index] == *voltage) {
			*voltage = leakage_table->actual_voltage[index];
			break;
		}
	}

	if (*voltage > ATOM_VIRTUAL_VOLTAGE_ID0)
		printk(KERN_ERR "Voltage value looks like a Leakage ID but it's not patched \n");
}

/**
* Patch voltage lookup table by EVV leakages.
*
* @param     hwmgr  the address of the powerplay hardware manager.
* @param     pointer to voltage lookup table
* @param     pointer to leakage table
* @return     always 0
*/
static int fiji_patch_lookup_table_with_leakage(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_voltage_lookup_table *lookup_table,
		struct fiji_leakage_voltage *leakage_table)
{
	uint32_t i;

	for (i = 0; i < lookup_table->count; i++)
		fiji_patch_with_vdd_leakage(hwmgr,
				&lookup_table->entries[i].us_vdd, leakage_table);

	return 0;
}

static int fiji_patch_clock_voltage_limits_with_vddc_leakage(
		struct pp_hwmgr *hwmgr, struct fiji_leakage_voltage *leakage_table,
		uint16_t *vddc)
{
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	fiji_patch_with_vdd_leakage(hwmgr, (uint16_t *)vddc, leakage_table);
	hwmgr->dyn_state.max_clock_voltage_on_dc.vddc =
			table_info->max_clock_voltage_on_dc.vddc;
	return 0;
}

static int fiji_patch_voltage_dependency_tables_with_lookup_table(
		struct pp_hwmgr *hwmgr)
{
	uint8_t entryId;
	uint8_t voltageId;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	struct phm_ppt_v1_clock_voltage_dependency_table *sclk_table =
			table_info->vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *mclk_table =
			table_info->vdd_dep_on_mclk;
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;

	for (entryId = 0; entryId < sclk_table->count; ++entryId) {
		voltageId = sclk_table->entries[entryId].vddInd;
		sclk_table->entries[entryId].vddc =
				table_info->vddc_lookup_table->entries[voltageId].us_vdd;
	}

	for (entryId = 0; entryId < mclk_table->count; ++entryId) {
		voltageId = mclk_table->entries[entryId].vddInd;
		mclk_table->entries[entryId].vddc =
			table_info->vddc_lookup_table->entries[voltageId].us_vdd;
	}

	for (entryId = 0; entryId < mm_table->count; ++entryId) {
		voltageId = mm_table->entries[entryId].vddcInd;
		mm_table->entries[entryId].vddc =
			table_info->vddc_lookup_table->entries[voltageId].us_vdd;
	}

	return 0;

}

static int fiji_calc_voltage_dependency_tables(struct pp_hwmgr *hwmgr)
{
	/* Need to determine if we need calculated voltage. */
	return 0;
}

static int fiji_calc_mm_voltage_dependency_table(struct pp_hwmgr *hwmgr)
{
	/* Need to determine if we need calculated voltage from mm table. */
	return 0;
}

static int fiji_sort_lookup_table(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_voltage_lookup_table *lookup_table)
{
	uint32_t table_size, i, j;
	struct phm_ppt_v1_voltage_lookup_record tmp_voltage_lookup_record;
	table_size = lookup_table->count;

	PP_ASSERT_WITH_CODE(0 != lookup_table->count,
		"Lookup table is empty", return -EINVAL);

	/* Sorting voltages */
	for (i = 0; i < table_size - 1; i++) {
		for (j = i + 1; j > 0; j--) {
			if (lookup_table->entries[j].us_vdd <
					lookup_table->entries[j - 1].us_vdd) {
				tmp_voltage_lookup_record = lookup_table->entries[j - 1];
				lookup_table->entries[j - 1] = lookup_table->entries[j];
				lookup_table->entries[j] = tmp_voltage_lookup_record;
			}
		}
	}

	return 0;
}

static int fiji_complete_dependency_tables(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	int tmp_result;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	tmp_result = fiji_patch_lookup_table_with_leakage(hwmgr,
			table_info->vddc_lookup_table, &(data->vddc_leakage));
	if (tmp_result)
		result = tmp_result;

	tmp_result = fiji_patch_clock_voltage_limits_with_vddc_leakage(hwmgr,
			&(data->vddc_leakage), &table_info->max_clock_voltage_on_dc.vddc);
	if (tmp_result)
		result = tmp_result;

	tmp_result = fiji_patch_voltage_dependency_tables_with_lookup_table(hwmgr);
	if (tmp_result)
		result = tmp_result;

	tmp_result = fiji_calc_voltage_dependency_tables(hwmgr);
	if (tmp_result)
		result = tmp_result;

	tmp_result = fiji_calc_mm_voltage_dependency_table(hwmgr);
	if (tmp_result)
		result = tmp_result;

	tmp_result = fiji_sort_lookup_table(hwmgr, table_info->vddc_lookup_table);
	if(tmp_result)
		result = tmp_result;

	return result;
}

static int fiji_set_private_data_based_on_pptable(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	struct phm_ppt_v1_clock_voltage_dependency_table *allowed_sclk_vdd_table =
			table_info->vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *allowed_mclk_vdd_table =
			table_info->vdd_dep_on_mclk;

	PP_ASSERT_WITH_CODE(allowed_sclk_vdd_table != NULL,
		"VDD dependency on SCLK table is missing.	\
		This table is mandatory", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_sclk_vdd_table->count >= 1,
		"VDD dependency on SCLK table has to have is missing.	\
		This table is mandatory", return -EINVAL);

	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table != NULL,
		"VDD dependency on MCLK table is missing.	\
		This table is mandatory", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table->count >= 1,
		"VDD dependency on MCLK table has to have is missing.	 \
		This table is mandatory", return -EINVAL);

	data->min_vddc_in_pptable = (uint16_t)allowed_sclk_vdd_table->entries[0].vddc;
	data->max_vddc_in_pptable =	(uint16_t)allowed_sclk_vdd_table->
			entries[allowed_sclk_vdd_table->count - 1].vddc;

	table_info->max_clock_voltage_on_ac.sclk =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].clk;
	table_info->max_clock_voltage_on_ac.mclk =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].clk;
	table_info->max_clock_voltage_on_ac.vddc =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].vddc;
	table_info->max_clock_voltage_on_ac.vddci =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].vddci;

	hwmgr->dyn_state.max_clock_voltage_on_ac.sclk =
		table_info->max_clock_voltage_on_ac.sclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.mclk =
		table_info->max_clock_voltage_on_ac.mclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddc =
		table_info->max_clock_voltage_on_ac.vddc;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddci =
		table_info->max_clock_voltage_on_ac.vddci;

	return 0;
}

static uint16_t fiji_get_current_pcie_speed(struct pp_hwmgr *hwmgr)
{
	uint32_t speedCntl = 0;

	/* mmPCIE_PORT_INDEX rename as mmPCIE_INDEX */
	speedCntl = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__PCIE,
			ixPCIE_LC_SPEED_CNTL);
	return((uint16_t)PHM_GET_FIELD(speedCntl,
			PCIE_LC_SPEED_CNTL, LC_CURRENT_DATA_RATE));
}

static int fiji_get_current_pcie_lane_number(struct pp_hwmgr *hwmgr)
{
	uint32_t link_width;

	/* mmPCIE_PORT_INDEX rename as mmPCIE_INDEX */
	link_width = PHM_READ_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__PCIE,
			PCIE_LC_LINK_WIDTH_CNTL, LC_LINK_WIDTH_RD);

	PP_ASSERT_WITH_CODE((7 >= link_width),
			"Invalid PCIe lane width!", return 0);

	return decode_pcie_lane_width(link_width);
}

/** Patch the Boot State to match VBIOS boot clocks and voltage.
*
* @param hwmgr Pointer to the hardware manager.
* @param pPowerState The address of the PowerState instance being created.
*
*/
static int fiji_patch_boot_state(struct pp_hwmgr *hwmgr,
		struct pp_hw_power_state *hw_ps)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_power_state *ps = (struct fiji_power_state *)hw_ps;
	ATOM_FIRMWARE_INFO_V2_2 *fw_info;
	uint16_t size;
	uint8_t frev, crev;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);

	/* First retrieve the Boot clocks and VDDC from the firmware info table.
	 * We assume here that fw_info is unchanged if this call fails.
	 */
	fw_info = (ATOM_FIRMWARE_INFO_V2_2 *)cgs_atom_get_data_table(
			hwmgr->device, index,
			&size, &frev, &crev);
	if (!fw_info)
		/* During a test, there is no firmware info table. */
		return 0;

	/* Patch the state. */
	data->vbios_boot_state.sclk_bootup_value =
			le32_to_cpu(fw_info->ulDefaultEngineClock);
	data->vbios_boot_state.mclk_bootup_value =
			le32_to_cpu(fw_info->ulDefaultMemoryClock);
	data->vbios_boot_state.mvdd_bootup_value =
			le16_to_cpu(fw_info->usBootUpMVDDCVoltage);
	data->vbios_boot_state.vddc_bootup_value =
			le16_to_cpu(fw_info->usBootUpVDDCVoltage);
	data->vbios_boot_state.vddci_bootup_value =
			le16_to_cpu(fw_info->usBootUpVDDCIVoltage);
	data->vbios_boot_state.pcie_gen_bootup_value =
			fiji_get_current_pcie_speed(hwmgr);
	data->vbios_boot_state.pcie_lane_bootup_value =
			(uint16_t)fiji_get_current_pcie_lane_number(hwmgr);

	/* set boot power state */
	ps->performance_levels[0].memory_clock = data->vbios_boot_state.mclk_bootup_value;
	ps->performance_levels[0].engine_clock = data->vbios_boot_state.sclk_bootup_value;
	ps->performance_levels[0].pcie_gen = data->vbios_boot_state.pcie_gen_bootup_value;
	ps->performance_levels[0].pcie_lane = data->vbios_boot_state.pcie_lane_bootup_value;

	return 0;
}

static int fiji_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (data->soft_pp_table) {
		kfree(data->soft_pp_table);
		data->soft_pp_table = NULL;
	}

	return phm_hwmgr_backend_fini(hwmgr);
}

static int fiji_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t i;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	bool stay_in_boot;
	int result;

	data->dll_default_on = false;
	data->sram_end = SMC_RAM_END;

	for (i = 0; i < SMU73_MAX_LEVELS_GRAPHICS; i++)
		data->activity_target[i] = FIJI_AT_DFLT;

	data->vddc_vddci_delta = VDDC_VDDCI_DELTA;

	data->mclk_activity_target = PPFIJI_MCLK_TARGETACTIVITY_DFLT;
	data->mclk_dpm0_activity_target = 0xa;

	data->sclk_dpm_key_disabled = 0;
	data->mclk_dpm_key_disabled = 0;
	data->pcie_dpm_key_disabled = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UnTabledHardwareInterface);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TablelessHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);

	data->gpio_debug = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPatchPowerState);

	/* need to set voltage control types before EVV patching */
	data->voltage_control = FIJI_VOLTAGE_CONTROL_NONE;
	data->vddci_control = FIJI_VOLTAGE_CONTROL_NONE;
	data->mvdd_control = FIJI_VOLTAGE_CONTROL_NONE;

	if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
			VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_SVID2))
		data->voltage_control = FIJI_VOLTAGE_CONTROL_BY_SVID2;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableMVDDControl))
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT))
			data->mvdd_control = FIJI_VOLTAGE_CONTROL_BY_GPIO;

	if (data->mvdd_control == FIJI_VOLTAGE_CONTROL_NONE)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableMVDDControl);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ControlVDDCI)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT))
			data->vddci_control = FIJI_VOLTAGE_CONTROL_BY_GPIO;
		else if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_SVID2))
			data->vddci_control = FIJI_VOLTAGE_CONTROL_BY_SVID2;
	}

	if (data->vddci_control == FIJI_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ControlVDDCI);

	if (table_info && table_info->cac_dtp_table->usClockStretchAmount)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ClockStretcher);

	fiji_init_dpm_defaults(hwmgr);

	/* Get leakage voltage based on leakage ID. */
	fiji_get_evv_voltages(hwmgr);

	/* Patch our voltage dependency table with actual leakage voltage
	 * We need to perform leakage translation before it's used by other functions
	 */
	fiji_complete_dependency_tables(hwmgr);

	/* Parse pptable data read from VBIOS */
	fiji_set_private_data_based_on_pptable(hwmgr);

	/* ULV Support */
	data->ulv.ulv_supported = true; /* ULV feature is enabled by default */

	/* Initalize Dynamic State Adjustment Rule Settings */
	result = tonga_initializa_dynamic_state_adjustment_rule_settings(hwmgr);

	if (!result) {
		data->uvd_enabled = false;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_EnableSMU7ThermalManagement);
		data->vddc_phase_shed_control = false;
	}

	stay_in_boot = phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StayInBootState);

	if (0 == result) {
		struct cgs_system_info sys_info = {0};

		data->is_tlu_enabled = 0;
		hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
				FIJI_MAX_HARDWARE_POWERLEVELS;
		hwmgr->platform_descriptor.hardwarePerformanceLevels = 2;
		hwmgr->platform_descriptor.minimumClocksReductionPercentage  = 50;

		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_FanSpeedInTableIsRPM);

		if (table_info->cac_dtp_table->usDefaultTargetOperatingTemp &&
				hwmgr->thermal_controller.
				advanceFanControlParameters.ucFanControlMode) {
			hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanPWM =
					hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanPWM;
			hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM =
					hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanRPM;
			hwmgr->dyn_state.cac_dtp_table->usOperatingTempMinLimit =
					table_info->cac_dtp_table->usOperatingTempMinLimit;
			hwmgr->dyn_state.cac_dtp_table->usOperatingTempMaxLimit =
					table_info->cac_dtp_table->usOperatingTempMaxLimit;
			hwmgr->dyn_state.cac_dtp_table->usDefaultTargetOperatingTemp =
					table_info->cac_dtp_table->usDefaultTargetOperatingTemp;
			hwmgr->dyn_state.cac_dtp_table->usOperatingTempStep =
					table_info->cac_dtp_table->usOperatingTempStep;
			hwmgr->dyn_state.cac_dtp_table->usTargetOperatingTemp =
					table_info->cac_dtp_table->usTargetOperatingTemp;

			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_ODFuzzyFanControlSupport);
		}

		sys_info.size = sizeof(struct cgs_system_info);
		sys_info.info_id = CGS_SYSTEM_INFO_PCIE_GEN_INFO;
		result = cgs_query_system_info(hwmgr->device, &sys_info);
		if (result)
			data->pcie_gen_cap = 0x30007;
		else
			data->pcie_gen_cap = (uint32_t)sys_info.value;
		if (data->pcie_gen_cap & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
			data->pcie_spc_cap = 20;
		sys_info.size = sizeof(struct cgs_system_info);
		sys_info.info_id = CGS_SYSTEM_INFO_PCIE_MLW;
		result = cgs_query_system_info(hwmgr->device, &sys_info);
		if (result)
			data->pcie_lane_cap = 0x2f0000;
		else
			data->pcie_lane_cap = (uint32_t)sys_info.value;
	} else {
		/* Ignore return value in here, we are cleaning up a mess. */
		fiji_hwmgr_backend_fini(hwmgr);
	}

	return 0;
}

/**
 * Read clock related registers.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
static int fiji_read_clock_registers(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	data->clock_registers.vCG_SPLL_FUNC_CNTL =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_SPLL_FUNC_CNTL);
	data->clock_registers.vCG_SPLL_FUNC_CNTL_2 =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_SPLL_FUNC_CNTL_2);
	data->clock_registers.vCG_SPLL_FUNC_CNTL_3 =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_SPLL_FUNC_CNTL_3);
	data->clock_registers.vCG_SPLL_FUNC_CNTL_4 =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_SPLL_FUNC_CNTL_4);
	data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_SPLL_SPREAD_SPECTRUM);
	data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2 =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_SPLL_SPREAD_SPECTRUM_2);

	return 0;
}

/**
 * Find out if memory is GDDR5.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
static int fiji_get_memory_type(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t temp;

	temp = cgs_read_register(hwmgr->device, mmMC_SEQ_MISC0);

	data->is_memory_gddr5 = (MC_SEQ_MISC0_GDDR5_VALUE ==
			((temp & MC_SEQ_MISC0_GDDR5_MASK) >>
			 MC_SEQ_MISC0_GDDR5_SHIFT));

	return 0;
}

/**
 * Enables Dynamic Power Management by SMC
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
static int fiji_enable_acpi_power_management(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			GENERAL_PWRMGT, STATIC_PM_EN, 1);

	return 0;
}

/**
 * Initialize PowerGating States for different engines
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
static int fiji_init_power_gate_state(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = false;
	data->vce_power_gated = false;
	data->samu_power_gated = false;
	data->acp_power_gated = false;
	data->pg_acp_init = true;

	return 0;
}

static int fiji_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	data->low_sclk_interrupt_threshold = 0;

	return 0;
}

static int fiji_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	tmp_result = fiji_read_clock_registers(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to read clock registers!", result = tmp_result);

	tmp_result = fiji_get_memory_type(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to get memory type!", result = tmp_result);

	tmp_result = fiji_enable_acpi_power_management(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable ACPI power management!", result = tmp_result);

	tmp_result = fiji_init_power_gate_state(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to init power gate state!", result = tmp_result);

	tmp_result = tonga_get_mc_microcode_version(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to get MC microcode version!", result = tmp_result);

	tmp_result = fiji_init_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to init sclk threshold!", result = tmp_result);

	return result;
}

/**
* Checks if we want to support voltage control
*
* @param    hwmgr  the address of the powerplay hardware manager.
*/
static bool fiji_voltage_control(const struct pp_hwmgr *hwmgr)
{
	const struct fiji_hwmgr *data =
			(const struct fiji_hwmgr *)(hwmgr->backend);

	return (FIJI_VOLTAGE_CONTROL_NONE != data->voltage_control);
}

/**
* Enable voltage control
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int fiji_enable_voltage_control(struct pp_hwmgr *hwmgr)
{
	/* enable voltage control */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			GENERAL_PWRMGT, VOLT_PWRMGT_EN, 1);

	return 0;
}

/**
* Remove repeated voltage values and create table with unique values.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    vol_table  the pointer to changing voltage table
* @return    0 in success
*/

static int fiji_trim_voltage_table(struct pp_hwmgr *hwmgr,
		struct pp_atomctrl_voltage_table *vol_table)
{
	uint32_t i, j;
	uint16_t vvalue;
	bool found = false;
	struct pp_atomctrl_voltage_table *table;

	PP_ASSERT_WITH_CODE((NULL != vol_table),
			"Voltage Table empty.", return -EINVAL);
	table = kzalloc(sizeof(struct pp_atomctrl_voltage_table),
			GFP_KERNEL);

	if (NULL == table)
		return -ENOMEM;

	table->mask_low = vol_table->mask_low;
	table->phase_delay = vol_table->phase_delay;

	for (i = 0; i < vol_table->count; i++) {
		vvalue = vol_table->entries[i].value;
		found = false;

		for (j = 0; j < table->count; j++) {
			if (vvalue == table->entries[j].value) {
				found = true;
				break;
			}
		}

		if (!found) {
			table->entries[table->count].value = vvalue;
			table->entries[table->count].smio_low =
					vol_table->entries[i].smio_low;
			table->count++;
		}
	}

	memcpy(vol_table, table, sizeof(struct pp_atomctrl_voltage_table));
	kfree(table);

	return 0;
}

static int fiji_get_svi2_mvdd_voltage_table(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table *dep_table)
{
	uint32_t i;
	int result;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_voltage_table *vol_table = &(data->mvdd_voltage_table);

	PP_ASSERT_WITH_CODE((0 != dep_table->count),
			"Voltage Dependency Table empty.", return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;
	vol_table->count = dep_table->count;

	for (i = 0; i < dep_table->count; i++) {
		vol_table->entries[i].value = dep_table->entries[i].mvdd;
		vol_table->entries[i].smio_low = 0;
	}

	result = fiji_trim_voltage_table(hwmgr, vol_table);
	PP_ASSERT_WITH_CODE((0 == result),
			"Failed to trim MVDD table.", return result);

	return 0;
}

static int fiji_get_svi2_vddci_voltage_table(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table *dep_table)
{
	uint32_t i;
	int result;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_voltage_table *vol_table = &(data->vddci_voltage_table);

	PP_ASSERT_WITH_CODE((0 != dep_table->count),
			"Voltage Dependency Table empty.", return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;
	vol_table->count = dep_table->count;

	for (i = 0; i < dep_table->count; i++) {
		vol_table->entries[i].value = dep_table->entries[i].vddci;
		vol_table->entries[i].smio_low = 0;
	}

	result = fiji_trim_voltage_table(hwmgr, vol_table);
	PP_ASSERT_WITH_CODE((0 == result),
			"Failed to trim VDDCI table.", return result);

	return 0;
}

static int fiji_get_svi2_vdd_voltage_table(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_voltage_lookup_table *lookup_table)
{
	int i = 0;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_voltage_table *vol_table = &(data->vddc_voltage_table);

	PP_ASSERT_WITH_CODE((0 != lookup_table->count),
			"Voltage Lookup Table empty.", return -EINVAL);

	vol_table->mask_low = 0;
	vol_table->phase_delay = 0;

	vol_table->count = lookup_table->count;

	for (i = 0; i < vol_table->count; i++) {
		vol_table->entries[i].value = lookup_table->entries[i].us_vdd;
		vol_table->entries[i].smio_low = 0;
	}

	return 0;
}

/* ---- Voltage Tables ----
 * If the voltage table would be bigger than
 * what will fit into the state table on
 * the SMC keep only the higher entries.
 */
static void fiji_trim_voltage_table_to_fit_state_table(struct pp_hwmgr *hwmgr,
		uint32_t max_vol_steps, struct pp_atomctrl_voltage_table *vol_table)
{
	unsigned int i, diff;

	if (vol_table->count <= max_vol_steps)
		return;

	diff = vol_table->count - max_vol_steps;

	for (i = 0; i < max_vol_steps; i++)
		vol_table->entries[i] = vol_table->entries[i + diff];

	vol_table->count = max_vol_steps;

	return;
}

/**
* Create Voltage Tables.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int fiji_construct_voltage_tables(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)hwmgr->pptable;
	int result;

	if (FIJI_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
				VOLTAGE_TYPE_MVDDC,	VOLTAGE_OBJ_GPIO_LUT,
				&(data->mvdd_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve MVDD table.",
				return result);
	} else if (FIJI_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control) {
		result = fiji_get_svi2_mvdd_voltage_table(hwmgr,
				table_info->vdd_dep_on_mclk);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve SVI2 MVDD table from dependancy table.",
				return result;);
	}

	if (FIJI_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
				VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT,
				&(data->vddci_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve VDDCI table.",
				return result);
	} else if (FIJI_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control) {
		result = fiji_get_svi2_vddci_voltage_table(hwmgr,
				table_info->vdd_dep_on_mclk);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve SVI2 VDDCI table from dependancy table.",
				return result);
	}

	if(FIJI_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		result = fiji_get_svi2_vdd_voltage_table(hwmgr,
				table_info->vddc_lookup_table);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve SVI2 VDDC table from lookup table.",
				return result);
	}

	PP_ASSERT_WITH_CODE(
			(data->vddc_voltage_table.count <= (SMU73_MAX_LEVELS_VDDC)),
			"Too many voltage values for VDDC. Trimming to fit state table.",
			fiji_trim_voltage_table_to_fit_state_table(hwmgr,
					SMU73_MAX_LEVELS_VDDC, &(data->vddc_voltage_table)));

	PP_ASSERT_WITH_CODE(
			(data->vddci_voltage_table.count <= (SMU73_MAX_LEVELS_VDDCI)),
			"Too many voltage values for VDDCI. Trimming to fit state table.",
			fiji_trim_voltage_table_to_fit_state_table(hwmgr,
					SMU73_MAX_LEVELS_VDDCI, &(data->vddci_voltage_table)));

	PP_ASSERT_WITH_CODE(
			(data->mvdd_voltage_table.count <= (SMU73_MAX_LEVELS_MVDD)),
			"Too many voltage values for MVDD. Trimming to fit state table.",
			fiji_trim_voltage_table_to_fit_state_table(hwmgr,
					SMU73_MAX_LEVELS_MVDD, &(data->mvdd_voltage_table)));

	return 0;
}

static int fiji_initialize_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	/* Program additional LP registers
	 * that are no longer programmed by VBIOS
	 */
	cgs_write_register(hwmgr->device, mmMC_SEQ_RAS_TIMING_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_RAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_CAS_TIMING_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_CAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_TIMING_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_TIMING));

	return 0;
}

/**
* Programs static screed detection parameters
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int fiji_program_static_screen_threshold_parameters(
		struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	/* Set static screen threshold unit */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_STATIC_SCREEN_PARAMETER, STATIC_SCREEN_THRESHOLD_UNIT,
			data->static_screen_threshold_unit);
	/* Set static screen threshold */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_STATIC_SCREEN_PARAMETER, STATIC_SCREEN_THRESHOLD,
			data->static_screen_threshold);

	return 0;
}

/**
* Setup display gap for glitch free memory clock switching.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always  0
*/
static int fiji_enable_display_gap(struct pp_hwmgr *hwmgr)
{
	uint32_t displayGap =
			cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixCG_DISPLAY_GAP_CNTL);

	displayGap = PHM_SET_FIELD(displayGap, CG_DISPLAY_GAP_CNTL,
			DISP_GAP, DISPLAY_GAP_IGNORE);

	displayGap = PHM_SET_FIELD(displayGap, CG_DISPLAY_GAP_CNTL,
			DISP_GAP_MCHG, DISPLAY_GAP_VBLANK);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_DISPLAY_GAP_CNTL, displayGap);

	return 0;
}

/**
* Programs activity state transition voting clients
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always  0
*/
static int fiji_program_voting_clients(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	/* Clear reset for voting clients before enabling DPM */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			SCLK_PWRMGT_CNTL, RESET_SCLK_CNT, 0);
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			SCLK_PWRMGT_CNTL, RESET_BUSY_CNT, 0);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_0, data->voting_rights_clients0);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_1, data->voting_rights_clients1);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_2, data->voting_rights_clients2);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_3, data->voting_rights_clients3);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_4, data->voting_rights_clients4);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_5, data->voting_rights_clients5);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_6, data->voting_rights_clients6);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_FREQ_TRAN_VOTING_7, data->voting_rights_clients7);

	return 0;
}

/**
* Get the location of various tables inside the FW image.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always  0
*/
static int fiji_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_smumgr *smu_data = (struct fiji_smumgr *)(hwmgr->smumgr->backend);
	uint32_t tmp;
	int result;
	bool error = false;

	result = fiji_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, DpmTable),
			&tmp, data->sram_end);

	if (0 == result)
		data->dpm_table_start = tmp;

	error |= (0 != result);

	result = fiji_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, SoftRegisters),
			&tmp, data->sram_end);

	if (!result) {
		data->soft_regs_start = tmp;
		smu_data->soft_regs_start = tmp;
	}

	error |= (0 != result);

	result = fiji_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, mcRegisterTable),
			&tmp, data->sram_end);

	if (!result)
		data->mc_reg_table_start = tmp;

	result = fiji_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, FanTable),
			&tmp, data->sram_end);

	if (!result)
		data->fan_table_start = tmp;

	error |= (0 != result);

	result = fiji_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, mcArbDramTimingTable),
			&tmp, data->sram_end);

	if (!result)
		data->arb_table_start = tmp;

	error |= (0 != result);

	result = fiji_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, Version),
			&tmp, data->sram_end);

	if (!result)
		hwmgr->microcode_version_info.SMC = tmp;

	error |= (0 != result);

	return error ? -1 : 0;
}

/* Copy one arb setting to another and then switch the active set.
 * arb_src and arb_dest is one of the MC_CG_ARB_FREQ_Fx constants.
 */
static int fiji_copy_and_switch_arb_sets(struct pp_hwmgr *hwmgr,
		uint32_t arb_src, uint32_t arb_dest)
{
	uint32_t mc_arb_dram_timing;
	uint32_t mc_arb_dram_timing2;
	uint32_t burst_time;
	uint32_t mc_cg_config;

	switch (arb_src) {
	case MC_CG_ARB_FREQ_F0:
		mc_arb_dram_timing  = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING);
		mc_arb_dram_timing2 = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2);
		burst_time = PHM_READ_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE0);
		break;
	case MC_CG_ARB_FREQ_F1:
		mc_arb_dram_timing  = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING_1);
		mc_arb_dram_timing2 = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2_1);
		burst_time = PHM_READ_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE1);
		break;
	default:
		return -EINVAL;
	}

	switch (arb_dest) {
	case MC_CG_ARB_FREQ_F0:
		cgs_write_register(hwmgr->device, mmMC_ARB_DRAM_TIMING, mc_arb_dram_timing);
		cgs_write_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2, mc_arb_dram_timing2);
		PHM_WRITE_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE0, burst_time);
		break;
	case MC_CG_ARB_FREQ_F1:
		cgs_write_register(hwmgr->device, mmMC_ARB_DRAM_TIMING_1, mc_arb_dram_timing);
		cgs_write_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2_1, mc_arb_dram_timing2);
		PHM_WRITE_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE1, burst_time);
		break;
	default:
		return -EINVAL;
	}

	mc_cg_config = cgs_read_register(hwmgr->device, mmMC_CG_CONFIG);
	mc_cg_config |= 0x0000000F;
	cgs_write_register(hwmgr->device, mmMC_CG_CONFIG, mc_cg_config);
	PHM_WRITE_FIELD(hwmgr->device, MC_ARB_CG, CG_ARB_REQ, arb_dest);

	return 0;
}

/**
* Initial switch from ARB F0->F1
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
* This function is to be called from the SetPowerState table.
*/
static int fiji_initial_switch_from_arbf0_to_f1(struct pp_hwmgr *hwmgr)
{
	return fiji_copy_and_switch_arb_sets(hwmgr,
			MC_CG_ARB_FREQ_F0, MC_CG_ARB_FREQ_F1);
}

static int fiji_reset_single_dpm_table(struct pp_hwmgr *hwmgr,
		struct fiji_single_dpm_table *dpm_table, uint32_t count)
{
	int i;
	PP_ASSERT_WITH_CODE(count <= MAX_REGULAR_DPM_NUMBER,
			"Fatal error, can not set up single DPM table entries "
			"to exceed max number!",);

	dpm_table->count = count;
	for (i = 0; i < MAX_REGULAR_DPM_NUMBER; i++)
		dpm_table->dpm_levels[i].enabled = false;

	return 0;
}

static void fiji_setup_pcie_table_entry(
	struct fiji_single_dpm_table *dpm_table,
	uint32_t index, uint32_t pcie_gen,
	uint32_t pcie_lanes)
{
	dpm_table->dpm_levels[index].value = pcie_gen;
	dpm_table->dpm_levels[index].param1 = pcie_lanes;
	dpm_table->dpm_levels[index].enabled = 1;
}

static int fiji_setup_default_pcie_table(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_pcie_table *pcie_table = table_info->pcie_table;
	uint32_t i, max_entry;

	PP_ASSERT_WITH_CODE((data->use_pcie_performance_levels ||
			data->use_pcie_power_saving_levels), "No pcie performance levels!",
			return -EINVAL);

	if (data->use_pcie_performance_levels &&
			!data->use_pcie_power_saving_levels) {
		data->pcie_gen_power_saving = data->pcie_gen_performance;
		data->pcie_lane_power_saving = data->pcie_lane_performance;
	} else if (!data->use_pcie_performance_levels &&
			data->use_pcie_power_saving_levels) {
		data->pcie_gen_performance = data->pcie_gen_power_saving;
		data->pcie_lane_performance = data->pcie_lane_power_saving;
	}

	fiji_reset_single_dpm_table(hwmgr,
			&data->dpm_table.pcie_speed_table, SMU73_MAX_LEVELS_LINK);

	if (pcie_table != NULL) {
		/* max_entry is used to make sure we reserve one PCIE level
		 * for boot level (fix for A+A PSPP issue).
		 * If PCIE table from PPTable have ULV entry + 8 entries,
		 * then ignore the last entry.*/
		max_entry = (SMU73_MAX_LEVELS_LINK < pcie_table->count) ?
				SMU73_MAX_LEVELS_LINK : pcie_table->count;
		for (i = 1; i < max_entry; i++) {
			fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, i - 1,
					get_pcie_gen_support(data->pcie_gen_cap,
							pcie_table->entries[i].gen_speed),
					get_pcie_lane_support(data->pcie_lane_cap,
							pcie_table->entries[i].lane_width));
		}
		data->dpm_table.pcie_speed_table.count = max_entry - 1;
	} else {
		/* Hardcode Pcie Table */
		fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 0,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Min_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 1,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Min_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 2,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 3,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 4,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 5,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));

		data->dpm_table.pcie_speed_table.count = 6;
	}
	/* Populate last level for boot PCIE level, but do not increment count. */
	fiji_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table,
			data->dpm_table.pcie_speed_table.count,
			get_pcie_gen_support(data->pcie_gen_cap,
					PP_Min_PCIEGen),
			get_pcie_lane_support(data->pcie_lane_cap,
					PP_Max_PCIELane));

	return 0;
}

/*
 * This function is to initalize all DPM state tables
 * for SMU7 based on the dependency table.
 * Dynamic state patching function will then trim these
 * state tables to the allowed range based
 * on the power policy or external client requests,
 * such as UVD request, etc.
 */
static int fiji_setup_default_dpm_tables(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t i;

	struct phm_ppt_v1_clock_voltage_dependency_table *dep_sclk_table =
			table_info->vdd_dep_on_sclk;
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_mclk_table =
			table_info->vdd_dep_on_mclk;

	PP_ASSERT_WITH_CODE(dep_sclk_table != NULL,
			"SCLK dependency table is missing. This table is mandatory",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(dep_sclk_table->count >= 1,
			"SCLK dependency table has to have is missing. "
			"This table is mandatory",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(dep_mclk_table != NULL,
			"MCLK dependency table is missing. This table is mandatory",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(dep_mclk_table->count >= 1,
			"MCLK dependency table has to have is missing. "
			"This table is mandatory",
			return -EINVAL);

	/* clear the state table to reset everything to default */
	fiji_reset_single_dpm_table(hwmgr,
			&data->dpm_table.sclk_table, SMU73_MAX_LEVELS_GRAPHICS);
	fiji_reset_single_dpm_table(hwmgr,
			&data->dpm_table.mclk_table, SMU73_MAX_LEVELS_MEMORY);

	/* Initialize Sclk DPM table based on allow Sclk values */
	data->dpm_table.sclk_table.count = 0;
	for (i = 0; i < dep_sclk_table->count; i++) {
		if (i == 0 || data->dpm_table.sclk_table.dpm_levels
				[data->dpm_table.sclk_table.count - 1].value !=
						dep_sclk_table->entries[i].clk) {
			data->dpm_table.sclk_table.dpm_levels
			[data->dpm_table.sclk_table.count].value =
					dep_sclk_table->entries[i].clk;
			data->dpm_table.sclk_table.dpm_levels
			[data->dpm_table.sclk_table.count].enabled =
					(i == 0) ? true : false;
			data->dpm_table.sclk_table.count++;
		}
	}

	/* Initialize Mclk DPM table based on allow Mclk values */
	data->dpm_table.mclk_table.count = 0;
	for (i=0; i<dep_mclk_table->count; i++) {
		if ( i==0 || data->dpm_table.mclk_table.dpm_levels
				[data->dpm_table.mclk_table.count - 1].value !=
						dep_mclk_table->entries[i].clk) {
			data->dpm_table.mclk_table.dpm_levels
			[data->dpm_table.mclk_table.count].value =
					dep_mclk_table->entries[i].clk;
			data->dpm_table.mclk_table.dpm_levels
			[data->dpm_table.mclk_table.count].enabled =
					(i == 0) ? true : false;
			data->dpm_table.mclk_table.count++;
		}
	}

	/* setup PCIE gen speed levels */
	fiji_setup_default_pcie_table(hwmgr);

	/* save a copy of the default DPM table */
	memcpy(&(data->golden_dpm_table), &(data->dpm_table),
			sizeof(struct fiji_dpm_table));

	return 0;
}

/**
 * @brief PhwFiji_GetVoltageOrder
 *  Returns index of requested voltage record in lookup(table)
 * @param lookup_table - lookup list to search in
 * @param voltage - voltage to look for
 * @return 0 on success
 */
uint8_t fiji_get_voltage_index(
		struct phm_ppt_v1_voltage_lookup_table *lookup_table, uint16_t voltage)
{
	uint8_t count = (uint8_t) (lookup_table->count);
	uint8_t i;

	PP_ASSERT_WITH_CODE((NULL != lookup_table),
			"Lookup Table empty.", return 0);
	PP_ASSERT_WITH_CODE((0 != count),
			"Lookup Table empty.", return 0);

	for (i = 0; i < lookup_table->count; i++) {
		/* find first voltage equal or bigger than requested */
		if (lookup_table->entries[i].us_vdd >= voltage)
			return i;
	}
	/* voltage is bigger than max voltage in the table */
	return i - 1;
}

/**
* Preparation of vddc and vddgfx CAC tables for SMC.
*
* @param    hwmgr  the address of the hardware manager
* @param    table  the SMC DPM table structure to be populated
* @return   always 0
*/
static int fiji_populate_cac_table(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_DpmTable *table)
{
	uint32_t count;
	uint8_t index;
	int result = 0;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_voltage_lookup_table *lookup_table =
			table_info->vddc_lookup_table;
	/* tables is already swapped, so in order to use the value from it,
	 * we need to swap it back.
	 * We are populating vddc CAC data to BapmVddc table
	 * in split and merged mode
	 */
	for( count = 0; count<lookup_table->count; count++) {
		index = fiji_get_voltage_index(lookup_table,
				data->vddc_voltage_table.entries[count].value);
		table->BapmVddcVidLoSidd[count] = (uint8_t) ((6200 -
				(lookup_table->entries[index].us_cac_low *
						VOLTAGE_SCALE)) / 25);
		table->BapmVddcVidHiSidd[count] = (uint8_t) ((6200 -
				(lookup_table->entries[index].us_cac_high *
						VOLTAGE_SCALE)) / 25);
	}

	return result;
}

/**
* Preparation of voltage tables for SMC.
*
* @param    hwmgr   the address of the hardware manager
* @param    table   the SMC DPM table structure to be populated
* @return   always  0
*/

int fiji_populate_smc_voltage_tables(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_DpmTable *table)
{
	int result;

	result = fiji_populate_cac_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate CAC voltage tables to SMC",
			return -EINVAL);

	return 0;
}

static int fiji_populate_ulv_level(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_Ulv *state)
{
	int result = 0;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	state->CcPwrDynRm = 0;
	state->CcPwrDynRm1 = 0;

	state->VddcOffset = (uint16_t) table_info->us_ulv_voltage_offset;
	state->VddcOffsetVid = (uint8_t)( table_info->us_ulv_voltage_offset *
			VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1 );

	state->VddcPhase = (data->vddc_phase_shed_control) ? 0 : 1;

	if (!result) {
		CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm);
		CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm1);
		CONVERT_FROM_HOST_TO_SMC_US(state->VddcOffset);
	}
	return result;
}

static int fiji_populate_ulv_state(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_DpmTable *table)
{
	return fiji_populate_ulv_level(hwmgr, &table->Ulv);
}

static int32_t fiji_get_dpm_level_enable_mask_value(
		struct fiji_single_dpm_table* dpm_table)
{
	int32_t i;
	int32_t mask = 0;

	for (i = dpm_table->count; i > 0; i--) {
		mask = mask << 1;
		if (dpm_table->dpm_levels[i - 1].enabled)
			mask |= 0x1;
		else
			mask &= 0xFFFFFFFE;
	}
	return mask;
}

static int fiji_populate_smc_link_level(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_DpmTable *table)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_dpm_table *dpm_table = &data->dpm_table;
	int i;

	/* Index (dpm_table->pcie_speed_table.count)
	 * is reserved for PCIE boot level. */
	for (i = 0; i <= dpm_table->pcie_speed_table.count; i++) {
		table->LinkLevel[i].PcieGenSpeed  =
				(uint8_t)dpm_table->pcie_speed_table.dpm_levels[i].value;
		table->LinkLevel[i].PcieLaneCount = (uint8_t)encode_pcie_lane_width(
				dpm_table->pcie_speed_table.dpm_levels[i].param1);
		table->LinkLevel[i].EnabledForActivity = 1;
		table->LinkLevel[i].SPC = (uint8_t)(data->pcie_spc_cap & 0xff);
		table->LinkLevel[i].DownThreshold = PP_HOST_TO_SMC_UL(5);
		table->LinkLevel[i].UpThreshold = PP_HOST_TO_SMC_UL(30);
	}

	data->smc_state_table.LinkLevelCount =
			(uint8_t)dpm_table->pcie_speed_table.count;
	data->dpm_level_enable_mask.pcie_dpm_enable_mask =
			fiji_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);

	return 0;
}

/**
* Calculates the SCLK dividers using the provided engine clock
*
* @param    hwmgr  the address of the hardware manager
* @param    clock  the engine clock to use to populate the structure
* @param    sclk   the SMC SCLK structure to be populated
*/
static int fiji_calculate_sclk_params(struct pp_hwmgr *hwmgr,
		uint32_t clock, struct SMU73_Discrete_GraphicsLevel *sclk)
{
	const struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_clock_dividers_vi dividers;
	uint32_t spll_func_cntl            = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_3          = data->clock_registers.vCG_SPLL_FUNC_CNTL_3;
	uint32_t spll_func_cntl_4          = data->clock_registers.vCG_SPLL_FUNC_CNTL_4;
	uint32_t cg_spll_spread_spectrum   = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM;
	uint32_t cg_spll_spread_spectrum_2 = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2;
	uint32_t ref_clock;
	uint32_t ref_divider;
	uint32_t fbdiv;
	int result;

	/* get the engine clock dividers for this clock value */
	result = atomctrl_get_engine_pll_dividers_vi(hwmgr, clock,  &dividers);

	PP_ASSERT_WITH_CODE(result == 0,
			"Error retrieving Engine Clock dividers from VBIOS.",
			return result);

	/* To get FBDIV we need to multiply this by 16384 and divide it by Fref. */
	ref_clock = atomctrl_get_reference_clock(hwmgr);
	ref_divider = 1 + dividers.uc_pll_ref_div;

	/* low 14 bits is fraction and high 12 bits is divider */
	fbdiv = dividers.ul_fb_div.ul_fb_divider & 0x3FFFFFF;

	/* SPLL_FUNC_CNTL setup */
	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl, CG_SPLL_FUNC_CNTL,
			SPLL_REF_DIV, dividers.uc_pll_ref_div);
	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl, CG_SPLL_FUNC_CNTL,
			SPLL_PDIV_A,  dividers.uc_pll_post_div);

	/* SPLL_FUNC_CNTL_3 setup*/
	spll_func_cntl_3 = PHM_SET_FIELD(spll_func_cntl_3, CG_SPLL_FUNC_CNTL_3,
			SPLL_FB_DIV, fbdiv);

	/* set to use fractional accumulation*/
	spll_func_cntl_3 = PHM_SET_FIELD(spll_func_cntl_3, CG_SPLL_FUNC_CNTL_3,
			SPLL_DITHEN, 1);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_EngineSpreadSpectrumSupport)) {
		struct pp_atomctrl_internal_ss_info ssInfo;

		uint32_t vco_freq = clock * dividers.uc_pll_post_div;
		if (!atomctrl_get_engine_clock_spread_spectrum(hwmgr,
				vco_freq, &ssInfo)) {
			/*
			 * ss_info.speed_spectrum_percentage -- in unit of 0.01%
			 * ss_info.speed_spectrum_rate -- in unit of khz
			 *
			 * clks = reference_clock * 10 / (REFDIV + 1) / speed_spectrum_rate / 2
			 */
			uint32_t clk_s = ref_clock * 5 /
					(ref_divider * ssInfo.speed_spectrum_rate);
			/* clkv = 2 * D * fbdiv / NS */
			uint32_t clk_v = 4 * ssInfo.speed_spectrum_percentage *
					fbdiv / (clk_s * 10000);

			cg_spll_spread_spectrum = PHM_SET_FIELD(cg_spll_spread_spectrum,
					CG_SPLL_SPREAD_SPECTRUM, CLKS, clk_s);
			cg_spll_spread_spectrum = PHM_SET_FIELD(cg_spll_spread_spectrum,
					CG_SPLL_SPREAD_SPECTRUM, SSEN, 1);
			cg_spll_spread_spectrum_2 = PHM_SET_FIELD(cg_spll_spread_spectrum_2,
					CG_SPLL_SPREAD_SPECTRUM_2, CLKV, clk_v);
		}
	}

	sclk->SclkFrequency        = clock;
	sclk->CgSpllFuncCntl3      = spll_func_cntl_3;
	sclk->CgSpllFuncCntl4      = spll_func_cntl_4;
	sclk->SpllSpreadSpectrum   = cg_spll_spread_spectrum;
	sclk->SpllSpreadSpectrum2  = cg_spll_spread_spectrum_2;
	sclk->SclkDid              = (uint8_t)dividers.pll_post_divider;

	return 0;
}

static uint16_t fiji_find_closest_vddci(struct pp_hwmgr *hwmgr, uint16_t vddci)
{
	uint32_t  i;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_voltage_table *vddci_table =
			&(data->vddci_voltage_table);

	for (i = 0; i < vddci_table->count; i++) {
		if (vddci_table->entries[i].value >= vddci)
			return vddci_table->entries[i].value;
	}

	PP_ASSERT_WITH_CODE(false,
			"VDDCI is larger than max VDDCI in VDDCI Voltage Table!",
			return vddci_table->entries[i-1].value);
}

static int fiji_get_dependency_volt_by_clk(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_clock_voltage_dependency_table* dep_table,
		uint32_t clock, SMU_VoltageLevel *voltage, uint32_t *mvdd)
{
	uint32_t i;
	uint16_t vddci;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	*voltage = *mvdd = 0;

	/* clock - voltage dependency table is empty table */
	if (dep_table->count == 0)
		return -EINVAL;

	for (i = 0; i < dep_table->count; i++) {
		/* find first sclk bigger than request */
		if (dep_table->entries[i].clk >= clock) {
			*voltage |= (dep_table->entries[i].vddc *
					VOLTAGE_SCALE) << VDDC_SHIFT;
			if (FIJI_VOLTAGE_CONTROL_NONE == data->vddci_control)
				*voltage |= (data->vbios_boot_state.vddci_bootup_value *
						VOLTAGE_SCALE) << VDDCI_SHIFT;
			else if (dep_table->entries[i].vddci)
				*voltage |= (dep_table->entries[i].vddci *
						VOLTAGE_SCALE) << VDDCI_SHIFT;
			else {
				vddci = fiji_find_closest_vddci(hwmgr,
						(dep_table->entries[i].vddc -
								(uint16_t)data->vddc_vddci_delta));
				*voltage |= (vddci * VOLTAGE_SCALE) <<	VDDCI_SHIFT;
			}

			if (FIJI_VOLTAGE_CONTROL_NONE == data->mvdd_control)
				*mvdd = data->vbios_boot_state.mvdd_bootup_value *
					VOLTAGE_SCALE;
			else if (dep_table->entries[i].mvdd)
				*mvdd = (uint32_t) dep_table->entries[i].mvdd *
					VOLTAGE_SCALE;

			*voltage |= 1 << PHASES_SHIFT;
			return 0;
		}
	}

	/* sclk is bigger than max sclk in the dependence table */
	*voltage |= (dep_table->entries[i - 1].vddc * VOLTAGE_SCALE) << VDDC_SHIFT;

	if (FIJI_VOLTAGE_CONTROL_NONE == data->vddci_control)
		*voltage |= (data->vbios_boot_state.vddci_bootup_value *
				VOLTAGE_SCALE) << VDDCI_SHIFT;
	else if (dep_table->entries[i-1].vddci) {
		vddci = fiji_find_closest_vddci(hwmgr,
				(dep_table->entries[i].vddc -
						(uint16_t)data->vddc_vddci_delta));
		*voltage |= (vddci * VOLTAGE_SCALE) << VDDCI_SHIFT;
	}

	if (FIJI_VOLTAGE_CONTROL_NONE == data->mvdd_control)
		*mvdd = data->vbios_boot_state.mvdd_bootup_value * VOLTAGE_SCALE;
	else if (dep_table->entries[i].mvdd)
		*mvdd = (uint32_t) dep_table->entries[i - 1].mvdd * VOLTAGE_SCALE;

	return 0;
}

static uint8_t fiji_get_sleep_divider_id_from_clock(uint32_t clock,
		uint32_t clock_insr)
{
	uint8_t i;
	uint32_t temp;
	uint32_t min = max(clock_insr, (uint32_t)FIJI_MINIMUM_ENGINE_CLOCK);

	PP_ASSERT_WITH_CODE((clock >= min), "Engine clock can't satisfy stutter requirement!", return 0);
	for (i = FIJI_MAX_DEEPSLEEP_DIVIDER_ID;  ; i--) {
		temp = clock >> i;

		if (temp >= min || i == 0)
			break;
	}
	return i;
}
/**
* Populates single SMC SCLK structure using the provided engine clock
*
* @param    hwmgr      the address of the hardware manager
* @param    clock the engine clock to use to populate the structure
* @param    sclk        the SMC SCLK structure to be populated
*/

static int fiji_populate_single_graphic_level(struct pp_hwmgr *hwmgr,
		uint32_t clock, uint16_t sclk_al_threshold,
		struct SMU73_Discrete_GraphicsLevel *level)
{
	int result;
	/* PP_Clocks minClocks; */
	uint32_t threshold, mvdd;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	result = fiji_calculate_sclk_params(hwmgr, clock, level);

	/* populate graphics levels */
	result = fiji_get_dependency_volt_by_clk(hwmgr,
			table_info->vdd_dep_on_sclk, clock,
			&level->MinVoltage, &mvdd);
	PP_ASSERT_WITH_CODE((0 == result),
			"can not find VDDC voltage value for "
			"VDDC engine clock dependency table",
			return result);

	level->SclkFrequency = clock;
	level->ActivityLevel = sclk_al_threshold;
	level->CcPwrDynRm = 0;
	level->CcPwrDynRm1 = 0;
	level->EnabledForActivity = 0;
	level->EnabledForThrottle = 1;
	level->UpHyst = 10;
	level->DownHyst = 0;
	level->VoltageDownHyst = 0;
	level->PowerThrottle = 0;

	threshold = clock * data->fast_watermark_threshold / 100;


	data->display_timing.min_clock_in_sr = hwmgr->display_config.min_core_set_clock_in_sr;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep))
		level->DeepSleepDivId = fiji_get_sleep_divider_id_from_clock(clock,
								hwmgr->display_config.min_core_set_clock_in_sr);


	/* Default to slow, highest DPM level will be
	 * set to PPSMC_DISPLAY_WATERMARK_LOW later.
	 */
	level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	CONVERT_FROM_HOST_TO_SMC_UL(level->MinVoltage);
	CONVERT_FROM_HOST_TO_SMC_UL(level->SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_US(level->ActivityLevel);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CgSpllFuncCntl3);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CgSpllFuncCntl4);
	CONVERT_FROM_HOST_TO_SMC_UL(level->SpllSpreadSpectrum);
	CONVERT_FROM_HOST_TO_SMC_UL(level->SpllSpreadSpectrum2);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CcPwrDynRm1);

	return 0;
}
/**
* Populates all SMC SCLK levels' structure based on the trimmed allowed dpm engine clock states
*
* @param    hwmgr      the address of the hardware manager
*/
static int fiji_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_dpm_table *dpm_table = &data->dpm_table;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_pcie_table *pcie_table = table_info->pcie_table;
	uint8_t pcie_entry_cnt = (uint8_t) data->dpm_table.pcie_speed_table.count;
	int result = 0;
	uint32_t array = data->dpm_table_start +
			offsetof(SMU73_Discrete_DpmTable, GraphicsLevel);
	uint32_t array_size = sizeof(struct SMU73_Discrete_GraphicsLevel) *
			SMU73_MAX_LEVELS_GRAPHICS;
	struct SMU73_Discrete_GraphicsLevel *levels =
			data->smc_state_table.GraphicsLevel;
	uint32_t i, max_entry;
	uint8_t hightest_pcie_level_enabled = 0,
			lowest_pcie_level_enabled = 0,
			mid_pcie_level_enabled = 0,
			count = 0;

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		result = fiji_populate_single_graphic_level(hwmgr,
				dpm_table->sclk_table.dpm_levels[i].value,
				(uint16_t)data->activity_target[i],
				&levels[i]);
		if (result)
			return result;

		/* Making sure only DPM level 0-1 have Deep Sleep Div ID populated. */
		if (i > 1)
			levels[i].DeepSleepDivId = 0;
	}

	/* Only enable level 0 for now.*/
	levels[0].EnabledForActivity = 1;

	/* set highest level watermark to high */
	levels[dpm_table->sclk_table.count - 1].DisplayWatermark =
			PPSMC_DISPLAY_WATERMARK_HIGH;

	data->smc_state_table.GraphicsDpmLevelCount =
			(uint8_t)dpm_table->sclk_table.count;
	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
			fiji_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);

	if (pcie_table != NULL) {
		PP_ASSERT_WITH_CODE((1 <= pcie_entry_cnt),
				"There must be 1 or more PCIE levels defined in PPTable.",
				return -EINVAL);
		max_entry = pcie_entry_cnt - 1;
		for (i = 0; i < dpm_table->sclk_table.count; i++)
			levels[i].pcieDpmLevel =
					(uint8_t) ((i < max_entry)? i : max_entry);
	} else {
		while (data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << (hightest_pcie_level_enabled + 1))) != 0 ))
			hightest_pcie_level_enabled++;

		while (data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << lowest_pcie_level_enabled)) == 0 ))
			lowest_pcie_level_enabled++;

		while ((count < hightest_pcie_level_enabled) &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << (lowest_pcie_level_enabled + 1 + count))) == 0 ))
			count++;

		mid_pcie_level_enabled = (lowest_pcie_level_enabled + 1+ count) <
				hightest_pcie_level_enabled?
						(lowest_pcie_level_enabled + 1 + count) :
						hightest_pcie_level_enabled;

		/* set pcieDpmLevel to hightest_pcie_level_enabled */
		for(i = 2; i < dpm_table->sclk_table.count; i++)
			levels[i].pcieDpmLevel = hightest_pcie_level_enabled;

		/* set pcieDpmLevel to lowest_pcie_level_enabled */
		levels[0].pcieDpmLevel = lowest_pcie_level_enabled;

		/* set pcieDpmLevel to mid_pcie_level_enabled */
		levels[1].pcieDpmLevel = mid_pcie_level_enabled;
	}
	/* level count will send to smc once at init smc table and never change */
	result = fiji_copy_bytes_to_smc(hwmgr->smumgr, array, (uint8_t *)levels,
			(uint32_t)array_size, data->sram_end);

	return result;
}

/**
 * MCLK Frequency Ratio
 * SEQ_CG_RESP  Bit[31:24] - 0x0
 * Bit[27:24] \96 DDR3 Frequency ratio
 * 0x0 <= 100MHz,       450 < 0x8 <= 500MHz
 * 100 < 0x1 <= 150MHz,       500 < 0x9 <= 550MHz
 * 150 < 0x2 <= 200MHz,       550 < 0xA <= 600MHz
 * 200 < 0x3 <= 250MHz,       600 < 0xB <= 650MHz
 * 250 < 0x4 <= 300MHz,       650 < 0xC <= 700MHz
 * 300 < 0x5 <= 350MHz,       700 < 0xD <= 750MHz
 * 350 < 0x6 <= 400MHz,       750 < 0xE <= 800MHz
 * 400 < 0x7 <= 450MHz,       800 < 0xF
 */
static uint8_t fiji_get_mclk_frequency_ratio(uint32_t mem_clock)
{
	if (mem_clock <= 10000) return 0x0;
	if (mem_clock <= 15000) return 0x1;
	if (mem_clock <= 20000) return 0x2;
	if (mem_clock <= 25000) return 0x3;
	if (mem_clock <= 30000) return 0x4;
	if (mem_clock <= 35000) return 0x5;
	if (mem_clock <= 40000) return 0x6;
	if (mem_clock <= 45000) return 0x7;
	if (mem_clock <= 50000) return 0x8;
	if (mem_clock <= 55000) return 0x9;
	if (mem_clock <= 60000) return 0xa;
	if (mem_clock <= 65000) return 0xb;
	if (mem_clock <= 70000) return 0xc;
	if (mem_clock <= 75000) return 0xd;
	if (mem_clock <= 80000) return 0xe;
	/* mem_clock > 800MHz */
	return 0xf;
}

/**
* Populates the SMC MCLK structure using the provided memory clock
*
* @param    hwmgr   the address of the hardware manager
* @param    clock   the memory clock to use to populate the structure
* @param    sclk    the SMC SCLK structure to be populated
*/
static int fiji_calculate_mclk_params(struct pp_hwmgr *hwmgr,
    uint32_t clock, struct SMU73_Discrete_MemoryLevel *mclk)
{
	struct pp_atomctrl_memory_clock_param mem_param;
	int result;

	result = atomctrl_get_memory_pll_dividers_vi(hwmgr, clock, &mem_param);
	PP_ASSERT_WITH_CODE((0 == result),
			"Failed to get Memory PLL Dividers.",);

	/* Save the result data to outpupt memory level structure */
	mclk->MclkFrequency   = clock;
	mclk->MclkDivider     = (uint8_t)mem_param.mpll_post_divider;
	mclk->FreqRange       = fiji_get_mclk_frequency_ratio(clock);

	return result;
}

static int fiji_populate_single_memory_level(struct pp_hwmgr *hwmgr,
		uint32_t clock, struct SMU73_Discrete_MemoryLevel *mem_level)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	int result = 0;

	if (table_info->vdd_dep_on_mclk) {
		result = fiji_get_dependency_volt_by_clk(hwmgr,
				table_info->vdd_dep_on_mclk, clock,
				&mem_level->MinVoltage, &mem_level->MinMvdd);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find MinVddc voltage value from memory "
				"VDDC voltage dependency table", return result);
	}

	mem_level->EnabledForThrottle = 1;
	mem_level->EnabledForActivity = 0;
	mem_level->UpHyst = 0;
	mem_level->DownHyst = 100;
	mem_level->VoltageDownHyst = 0;
	mem_level->ActivityLevel = (uint16_t)data->mclk_activity_target;
	mem_level->StutterEnable = false;

	mem_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	/* enable stutter mode if all the follow condition applied
	 * PECI_GetNumberOfActiveDisplays(hwmgr->pPECI,
	 * &(data->DisplayTiming.numExistingDisplays));
	 */
	data->display_timing.num_existing_displays = 1;

	if ((data->mclk_stutter_mode_threshold) &&
		(clock <= data->mclk_stutter_mode_threshold) &&
		(!data->is_uvd_enabled) &&
		(PHM_READ_FIELD(hwmgr->device, DPG_PIPE_STUTTER_CONTROL,
				STUTTER_ENABLE) & 0x1))
		mem_level->StutterEnable = true;

	result = fiji_calculate_mclk_params(hwmgr, clock, mem_level);
	if (!result) {
		CONVERT_FROM_HOST_TO_SMC_UL(mem_level->MinMvdd);
		CONVERT_FROM_HOST_TO_SMC_UL(mem_level->MclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_US(mem_level->ActivityLevel);
		CONVERT_FROM_HOST_TO_SMC_UL(mem_level->MinVoltage);
	}
	return result;
}

/**
* Populates all SMC MCLK levels' structure based on the trimmed allowed dpm memory clock states
*
* @param    hwmgr      the address of the hardware manager
*/
static int fiji_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_dpm_table *dpm_table = &data->dpm_table;
	int result;
	/* populate MCLK dpm table to SMU7 */
	uint32_t array = data->dpm_table_start +
			offsetof(SMU73_Discrete_DpmTable, MemoryLevel);
	uint32_t array_size = sizeof(SMU73_Discrete_MemoryLevel) *
			SMU73_MAX_LEVELS_MEMORY;
	struct SMU73_Discrete_MemoryLevel *levels =
			data->smc_state_table.MemoryLevel;
	uint32_t i;

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		PP_ASSERT_WITH_CODE((0 != dpm_table->mclk_table.dpm_levels[i].value),
				"can not populate memory level as memory clock is zero",
				return -EINVAL);
		result = fiji_populate_single_memory_level(hwmgr,
				dpm_table->mclk_table.dpm_levels[i].value,
				&levels[i]);
		if (result)
			return result;
	}

	/* Only enable level 0 for now. */
	levels[0].EnabledForActivity = 1;

	/* in order to prevent MC activity from stutter mode to push DPM up.
	 * the UVD change complements this by putting the MCLK in
	 * a higher state by default such that we are not effected by
	 * up threshold or and MCLK DPM latency.
	 */
	levels[0].ActivityLevel = (uint16_t)data->mclk_dpm0_activity_target;
	CONVERT_FROM_HOST_TO_SMC_US(levels[0].ActivityLevel);

	data->smc_state_table.MemoryDpmLevelCount =
			(uint8_t)dpm_table->mclk_table.count;
	data->dpm_level_enable_mask.mclk_dpm_enable_mask =
			fiji_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);
	/* set highest level watermark to high */
	levels[dpm_table->mclk_table.count - 1].DisplayWatermark =
			PPSMC_DISPLAY_WATERMARK_HIGH;

	/* level count will send to smc once at init smc table and never change */
	result = fiji_copy_bytes_to_smc(hwmgr->smumgr, array, (uint8_t *)levels,
			(uint32_t)array_size, data->sram_end);

	return result;
}

/**
* Populates the SMC MVDD structure using the provided memory clock.
*
* @param    hwmgr      the address of the hardware manager
* @param    mclk        the MCLK value to be used in the decision if MVDD should be high or low.
* @param    voltage     the SMC VOLTAGE structure to be populated
*/
int fiji_populate_mvdd_value(struct pp_hwmgr *hwmgr,
		uint32_t mclk, SMIO_Pattern *smio_pat)
{
	const struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t i = 0;

	if (FIJI_VOLTAGE_CONTROL_NONE != data->mvdd_control) {
		/* find mvdd value which clock is more than request */
		for (i = 0; i < table_info->vdd_dep_on_mclk->count; i++) {
			if (mclk <= table_info->vdd_dep_on_mclk->entries[i].clk) {
				smio_pat->Voltage = data->mvdd_voltage_table.entries[i].value;
				break;
			}
		}
		PP_ASSERT_WITH_CODE(i < table_info->vdd_dep_on_mclk->count,
				"MVDD Voltage is outside the supported range.",
				return -EINVAL);
	} else
		return -EINVAL;

	return 0;
}

static int fiji_populate_smc_acpi_level(struct pp_hwmgr *hwmgr,
		SMU73_Discrete_DpmTable *table)
{
	int result = 0;
	const struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct pp_atomctrl_clock_dividers_vi dividers;
	SMIO_Pattern vol_level;
	uint32_t mvdd;
	uint16_t us_mvdd;
	uint32_t spll_func_cntl    = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_2  = data->clock_registers.vCG_SPLL_FUNC_CNTL_2;

	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (!data->sclk_dpm_key_disabled) {
		/* Get MinVoltage and Frequency from DPM0,
		 * already converted to SMC_UL */
		table->ACPILevel.SclkFrequency =
				data->dpm_table.sclk_table.dpm_levels[0].value;
		result = fiji_get_dependency_volt_by_clk(hwmgr,
				table_info->vdd_dep_on_sclk,
				table->ACPILevel.SclkFrequency,
				&table->ACPILevel.MinVoltage, &mvdd);
		PP_ASSERT_WITH_CODE((0 == result),
				"Cannot find ACPI VDDC voltage value "
				"in Clock Dependency Table",);
	} else {
		table->ACPILevel.SclkFrequency =
				data->vbios_boot_state.sclk_bootup_value;
		table->ACPILevel.MinVoltage =
				data->vbios_boot_state.vddc_bootup_value * VOLTAGE_SCALE;
	}

	/* get the engine clock dividers for this clock value */
	result = atomctrl_get_engine_pll_dividers_vi(hwmgr,
			table->ACPILevel.SclkFrequency,  &dividers);
	PP_ASSERT_WITH_CODE(result == 0,
			"Error retrieving Engine Clock dividers from VBIOS.",
			return result);

	table->ACPILevel.SclkDid = (uint8_t)dividers.pll_post_divider;
	table->ACPILevel.DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;
	table->ACPILevel.DeepSleepDivId = 0;

	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl, CG_SPLL_FUNC_CNTL,
			SPLL_PWRON, 0);
	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl, CG_SPLL_FUNC_CNTL,
			SPLL_RESET, 1);
	spll_func_cntl_2 = PHM_SET_FIELD(spll_func_cntl_2, CG_SPLL_FUNC_CNTL_2,
			SCLK_MUX_SEL, 4);

	table->ACPILevel.CgSpllFuncCntl = spll_func_cntl;
	table->ACPILevel.CgSpllFuncCntl2 = spll_func_cntl_2;
	table->ACPILevel.CgSpllFuncCntl3 = data->clock_registers.vCG_SPLL_FUNC_CNTL_3;
	table->ACPILevel.CgSpllFuncCntl4 = data->clock_registers.vCG_SPLL_FUNC_CNTL_4;
	table->ACPILevel.SpllSpreadSpectrum = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM;
	table->ACPILevel.SpllSpreadSpectrum2 = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2;
	table->ACPILevel.CcPwrDynRm = 0;
	table->ACPILevel.CcPwrDynRm1 = 0;

	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.Flags);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.MinVoltage);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl3);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl4);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SpllSpreadSpectrum);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SpllSpreadSpectrum2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm1);

	if (!data->mclk_dpm_key_disabled) {
		/* Get MinVoltage and Frequency from DPM0, already converted to SMC_UL */
		table->MemoryACPILevel.MclkFrequency =
				data->dpm_table.mclk_table.dpm_levels[0].value;
		result = fiji_get_dependency_volt_by_clk(hwmgr,
				table_info->vdd_dep_on_mclk,
				table->MemoryACPILevel.MclkFrequency,
				&table->MemoryACPILevel.MinVoltage, &mvdd);
		PP_ASSERT_WITH_CODE((0 == result),
				"Cannot find ACPI VDDCI voltage value "
				"in Clock Dependency Table",);
	} else {
		table->MemoryACPILevel.MclkFrequency =
				data->vbios_boot_state.mclk_bootup_value;
		table->MemoryACPILevel.MinVoltage =
				data->vbios_boot_state.vddci_bootup_value * VOLTAGE_SCALE;
	}

	us_mvdd = 0;
	if ((FIJI_VOLTAGE_CONTROL_NONE == data->mvdd_control) ||
			(data->mclk_dpm_key_disabled))
		us_mvdd = data->vbios_boot_state.mvdd_bootup_value;
	else {
		if (!fiji_populate_mvdd_value(hwmgr,
				data->dpm_table.mclk_table.dpm_levels[0].value,
				&vol_level))
			us_mvdd = vol_level.Voltage;
	}

	table->MemoryACPILevel.MinMvdd =
			PP_HOST_TO_SMC_UL(us_mvdd * VOLTAGE_SCALE);

	table->MemoryACPILevel.EnabledForThrottle = 0;
	table->MemoryACPILevel.EnabledForActivity = 0;
	table->MemoryACPILevel.UpHyst = 0;
	table->MemoryACPILevel.DownHyst = 100;
	table->MemoryACPILevel.VoltageDownHyst = 0;
	table->MemoryACPILevel.ActivityLevel =
			PP_HOST_TO_SMC_US((uint16_t)data->mclk_activity_target);

	table->MemoryACPILevel.StutterEnable = false;
	CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MinVoltage);

	return result;
}

static int fiji_populate_smc_vce_level(struct pp_hwmgr *hwmgr,
		SMU73_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	table->VceLevelCount = (uint8_t)(mm_table->count);
	table->VceBootLevel = 0;

	for(count = 0; count < table->VceLevelCount; count++) {
		table->VceLevel[count].Frequency = mm_table->entries[count].eclk;
		table->VceLevel[count].MinVoltage = 0;
		table->VceLevel[count].MinVoltage |=
				(mm_table->entries[count].vddc * VOLTAGE_SCALE) << VDDC_SHIFT;
		table->VceLevel[count].MinVoltage |=
				((mm_table->entries[count].vddc - data->vddc_vddci_delta) *
						VOLTAGE_SCALE) << VDDCI_SHIFT;
		table->VceLevel[count].MinVoltage |= 1 << PHASES_SHIFT;

		/*retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->VceLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for VCE engine clock",
				return result);

		table->VceLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->VceLevel[count].Frequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->VceLevel[count].MinVoltage);
	}
	return result;
}

static int fiji_populate_smc_acp_level(struct pp_hwmgr *hwmgr,
		SMU73_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	table->AcpLevelCount = (uint8_t)(mm_table->count);
	table->AcpBootLevel = 0;

	for (count = 0; count < table->AcpLevelCount; count++) {
		table->AcpLevel[count].Frequency = mm_table->entries[count].aclk;
		table->AcpLevel[count].MinVoltage |= (mm_table->entries[count].vddc *
				VOLTAGE_SCALE) << VDDC_SHIFT;
		table->AcpLevel[count].MinVoltage |= ((mm_table->entries[count].vddc -
				data->vddc_vddci_delta) * VOLTAGE_SCALE) << VDDCI_SHIFT;
		table->AcpLevel[count].MinVoltage |= 1 << PHASES_SHIFT;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->AcpLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for engine clock", return result);

		table->AcpLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->AcpLevel[count].Frequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->AcpLevel[count].MinVoltage);
	}
	return result;
}

static int fiji_populate_smc_samu_level(struct pp_hwmgr *hwmgr,
		SMU73_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	table->SamuBootLevel = 0;
	table->SamuLevelCount = (uint8_t)(mm_table->count);

	for (count = 0; count < table->SamuLevelCount; count++) {
		/* not sure whether we need evclk or not */
		table->SamuLevel[count].MinVoltage = 0;
		table->SamuLevel[count].Frequency = mm_table->entries[count].samclock;
		table->SamuLevel[count].MinVoltage |= (mm_table->entries[count].vddc *
				VOLTAGE_SCALE) << VDDC_SHIFT;
		table->SamuLevel[count].MinVoltage |= ((mm_table->entries[count].vddc -
				data->vddc_vddci_delta) * VOLTAGE_SCALE) << VDDCI_SHIFT;
		table->SamuLevel[count].MinVoltage |= 1 << PHASES_SHIFT;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->SamuLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for samu clock", return result);

		table->SamuLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->SamuLevel[count].Frequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->SamuLevel[count].MinVoltage);
	}
	return result;
}

static int fiji_populate_memory_timing_parameters(struct pp_hwmgr *hwmgr,
		int32_t eng_clock, int32_t mem_clock,
		struct SMU73_Discrete_MCArbDramTimingTableEntry *arb_regs)
{
	uint32_t dram_timing;
	uint32_t dram_timing2;
	uint32_t burstTime;
	ULONG state, trrds, trrdl;
	int result;

	result = atomctrl_set_engine_dram_timings_rv770(hwmgr,
			eng_clock, mem_clock);
	PP_ASSERT_WITH_CODE(result == 0,
			"Error calling VBIOS to set DRAM_TIMING.", return result);

	dram_timing = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING);
	dram_timing2 = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2);
	burstTime = cgs_read_register(hwmgr->device, mmMC_ARB_BURST_TIME);

	state = PHM_GET_FIELD(burstTime, MC_ARB_BURST_TIME, STATE0);
	trrds = PHM_GET_FIELD(burstTime, MC_ARB_BURST_TIME, TRRDS0);
	trrdl = PHM_GET_FIELD(burstTime, MC_ARB_BURST_TIME, TRRDL0);

	arb_regs->McArbDramTiming  = PP_HOST_TO_SMC_UL(dram_timing);
	arb_regs->McArbDramTiming2 = PP_HOST_TO_SMC_UL(dram_timing2);
	arb_regs->McArbBurstTime   = (uint8_t)burstTime;
	arb_regs->TRRDS            = (uint8_t)trrds;
	arb_regs->TRRDL            = (uint8_t)trrdl;

	return 0;
}

static int fiji_program_memory_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct SMU73_Discrete_MCArbDramTimingTable arb_regs;
	uint32_t i, j;
	int result = 0;

	for (i = 0; i < data->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < data->dpm_table.mclk_table.count; j++) {
			result = fiji_populate_memory_timing_parameters(hwmgr,
					data->dpm_table.sclk_table.dpm_levels[i].value,
					data->dpm_table.mclk_table.dpm_levels[j].value,
					&arb_regs.entries[i][j]);
			if (result)
				break;
		}
	}

	if (!result)
		result = fiji_copy_bytes_to_smc(
				hwmgr->smumgr,
				data->arb_table_start,
				(uint8_t *)&arb_regs,
				sizeof(SMU73_Discrete_MCArbDramTimingTable),
				data->sram_end);
	return result;
}

static int fiji_populate_smc_uvd_level(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	table->UvdLevelCount = (uint8_t)(mm_table->count);
	table->UvdBootLevel = 0;

	for (count = 0; count < table->UvdLevelCount; count++) {
		table->UvdLevel[count].MinVoltage = 0;
		table->UvdLevel[count].VclkFrequency = mm_table->entries[count].vclk;
		table->UvdLevel[count].DclkFrequency = mm_table->entries[count].dclk;
		table->UvdLevel[count].MinVoltage |= (mm_table->entries[count].vddc *
				VOLTAGE_SCALE) << VDDC_SHIFT;
		table->UvdLevel[count].MinVoltage |= ((mm_table->entries[count].vddc -
				data->vddc_vddci_delta) * VOLTAGE_SCALE) << VDDCI_SHIFT;
		table->UvdLevel[count].MinVoltage |= 1 << PHASES_SHIFT;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->UvdLevel[count].VclkFrequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for Vclk clock", return result);

		table->UvdLevel[count].VclkDivider = (uint8_t)dividers.pll_post_divider;

		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
				table->UvdLevel[count].DclkFrequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for Dclk clock", return result);

		table->UvdLevel[count].DclkDivider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].VclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].DclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].MinVoltage);

	}
	return result;
}

static int fiji_find_boot_level(struct fiji_single_dpm_table *table,
		uint32_t value, uint32_t *boot_level)
{
	int result = -EINVAL;
	uint32_t i;

	for (i = 0; i < table->count; i++) {
		if (value == table->dpm_levels[i].value) {
			*boot_level = i;
			result = 0;
		}
	}
	return result;
}

static int fiji_populate_smc_boot_level(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_DpmTable *table)
{
	int result = 0;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	table->GraphicsBootLevel = 0;
	table->MemoryBootLevel = 0;

	/* find boot level from dpm table */
	result = fiji_find_boot_level(&(data->dpm_table.sclk_table),
			data->vbios_boot_state.sclk_bootup_value,
			(uint32_t *)&(table->GraphicsBootLevel));

	result = fiji_find_boot_level(&(data->dpm_table.mclk_table),
			data->vbios_boot_state.mclk_bootup_value,
			(uint32_t *)&(table->MemoryBootLevel));

	table->BootVddc  = data->vbios_boot_state.vddc_bootup_value *
			VOLTAGE_SCALE;
	table->BootVddci = data->vbios_boot_state.vddci_bootup_value *
			VOLTAGE_SCALE;
	table->BootMVdd  = data->vbios_boot_state.mvdd_bootup_value *
			VOLTAGE_SCALE;

	CONVERT_FROM_HOST_TO_SMC_US(table->BootVddc);
	CONVERT_FROM_HOST_TO_SMC_US(table->BootVddci);
	CONVERT_FROM_HOST_TO_SMC_US(table->BootMVdd);

	return 0;
}

static int fiji_populate_smc_initailial_state(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint8_t count, level;

	count = (uint8_t)(table_info->vdd_dep_on_sclk->count);
	for (level = 0; level < count; level++) {
		if(table_info->vdd_dep_on_sclk->entries[level].clk >=
				data->vbios_boot_state.sclk_bootup_value) {
			data->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	count = (uint8_t)(table_info->vdd_dep_on_mclk->count);
	for (level = 0; level < count; level++) {
		if(table_info->vdd_dep_on_mclk->entries[level].clk >=
				data->vbios_boot_state.mclk_bootup_value) {
			data->smc_state_table.MemoryBootLevel = level;
			break;
		}
	}

	return 0;
}

static int fiji_populate_clock_stretcher_data_table(struct pp_hwmgr *hwmgr)
{
	uint32_t ro, efuse, efuse2, clock_freq, volt_without_cks,
			volt_with_cks, value;
	uint16_t clock_freq_u16;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint8_t type, i, j, cks_setting, stretch_amount, stretch_amount2,
			volt_offset = 0;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *sclk_table =
			table_info->vdd_dep_on_sclk;

	stretch_amount = (uint8_t)table_info->cac_dtp_table->usClockStretchAmount;

	/* Read SMU_Eefuse to read and calculate RO and determine
	 * if the part is SS or FF. if RO >= 1660MHz, part is FF.
	 */
	efuse = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixSMU_EFUSE_0 + (146 * 4));
	efuse2 = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixSMU_EFUSE_0 + (148 * 4));
	efuse &= 0xFF000000;
	efuse = efuse >> 24;
	efuse2 &= 0xF;

	if (efuse2 == 1)
		ro = (2300 - 1350) * efuse / 255 + 1350;
	else
		ro = (2500 - 1000) * efuse / 255 + 1000;

	if (ro >= 1660)
		type = 0;
	else
		type = 1;

	/* Populate Stretch amount */
	data->smc_state_table.ClockStretcherAmount = stretch_amount;

	/* Populate Sclk_CKS_masterEn0_7 and Sclk_voltageOffset */
	for (i = 0; i < sclk_table->count; i++) {
		data->smc_state_table.Sclk_CKS_masterEn0_7 |=
				sclk_table->entries[i].cks_enable << i;
		volt_without_cks = (uint32_t)((14041 *
			(sclk_table->entries[i].clk/100) / 10000 + 3571 + 75 - ro) * 1000 /
			(4026 - (13924 * (sclk_table->entries[i].clk/100) / 10000)));
		volt_with_cks = (uint32_t)((13946 *
			(sclk_table->entries[i].clk/100) / 10000 + 3320 + 45 - ro) * 1000 /
			(3664 - (11454 * (sclk_table->entries[i].clk/100) / 10000)));
		if (volt_without_cks >= volt_with_cks)
			volt_offset = (uint8_t)(((volt_without_cks - volt_with_cks +
					sclk_table->entries[i].cks_voffset) * 100 / 625) + 1);
		data->smc_state_table.Sclk_voltageOffset[i] = volt_offset;
	}

	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, PWR_CKS_ENABLE,
			STRETCH_ENABLE, 0x0);
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, PWR_CKS_ENABLE,
			masterReset, 0x1);
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, PWR_CKS_ENABLE,
			staticEnable, 0x1);
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, PWR_CKS_ENABLE,
			masterReset, 0x0);

	/* Populate CKS Lookup Table */
	if (stretch_amount == 1 || stretch_amount == 2 || stretch_amount == 5)
		stretch_amount2 = 0;
	else if (stretch_amount == 3 || stretch_amount == 4)
		stretch_amount2 = 1;
	else {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ClockStretcher);
		PP_ASSERT_WITH_CODE(false,
				"Stretch Amount in PPTable not supported\n",
				return -EINVAL);
	}

	value = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixPWR_CKS_CNTL);
	value &= 0xFFC2FF87;
	data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].minFreq =
			fiji_clock_stretcher_lookup_table[stretch_amount2][0];
	data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].maxFreq =
			fiji_clock_stretcher_lookup_table[stretch_amount2][1];
	clock_freq_u16 = (uint16_t)(PP_SMC_TO_HOST_UL(data->smc_state_table.
			GraphicsLevel[data->smc_state_table.GraphicsDpmLevelCount - 1].
			SclkFrequency) / 100);
	if (fiji_clock_stretcher_lookup_table[stretch_amount2][0] <
			clock_freq_u16 &&
	    fiji_clock_stretcher_lookup_table[stretch_amount2][1] >
			clock_freq_u16) {
		/* Program PWR_CKS_CNTL. CKS_USE_FOR_LOW_FREQ */
		value |= (fiji_clock_stretcher_lookup_table[stretch_amount2][3]) << 16;
		/* Program PWR_CKS_CNTL. CKS_LDO_REFSEL */
		value |= (fiji_clock_stretcher_lookup_table[stretch_amount2][2]) << 18;
		/* Program PWR_CKS_CNTL. CKS_STRETCH_AMOUNT */
		value |= (fiji_clock_stretch_amount_conversion
				[fiji_clock_stretcher_lookup_table[stretch_amount2][3]]
				 [stretch_amount]) << 3;
	}
	CONVERT_FROM_HOST_TO_SMC_US(data->smc_state_table.CKS_LOOKUPTable.
			CKS_LOOKUPTableEntry[0].minFreq);
	CONVERT_FROM_HOST_TO_SMC_US(data->smc_state_table.CKS_LOOKUPTable.
			CKS_LOOKUPTableEntry[0].maxFreq);
	data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].setting =
			fiji_clock_stretcher_lookup_table[stretch_amount2][2] & 0x7F;
	data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].setting |=
			(fiji_clock_stretcher_lookup_table[stretch_amount2][3]) << 7;

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixPWR_CKS_CNTL, value);

	/* Populate DDT Lookup Table */
	for (i = 0; i < 4; i++) {
		/* Assign the minimum and maximum VID stored
		 * in the last row of Clock Stretcher Voltage Table.
		 */
		data->smc_state_table.ClockStretcherDataTable.
		ClockStretcherDataTableEntry[i].minVID =
				(uint8_t) fiji_clock_stretcher_ddt_table[type][i][2];
		data->smc_state_table.ClockStretcherDataTable.
		ClockStretcherDataTableEntry[i].maxVID =
				(uint8_t) fiji_clock_stretcher_ddt_table[type][i][3];
		/* Loop through each SCLK and check the frequency
		 * to see if it lies within the frequency for clock stretcher.
		 */
		for (j = 0; j < data->smc_state_table.GraphicsDpmLevelCount; j++) {
			cks_setting = 0;
			clock_freq = PP_SMC_TO_HOST_UL(
					data->smc_state_table.GraphicsLevel[j].SclkFrequency);
			/* Check the allowed frequency against the sclk level[j].
			 *  Sclk's endianness has already been converted,
			 *  and it's in 10Khz unit,
			 *  as opposed to Data table, which is in Mhz unit.
			 */
			if (clock_freq >=
					(fiji_clock_stretcher_ddt_table[type][i][0]) * 100) {
				cks_setting |= 0x2;
				if (clock_freq <
						(fiji_clock_stretcher_ddt_table[type][i][1]) * 100)
					cks_setting |= 0x1;
			}
			data->smc_state_table.ClockStretcherDataTable.
			ClockStretcherDataTableEntry[i].setting |= cks_setting << (j * 2);
		}
		CONVERT_FROM_HOST_TO_SMC_US(data->smc_state_table.
				ClockStretcherDataTable.
				ClockStretcherDataTableEntry[i].setting);
	}

	value = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixPWR_CKS_CNTL);
	value &= 0xFFFFFFFE;
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixPWR_CKS_CNTL, value);

	return 0;
}

/**
* Populates the SMC VRConfig field in DPM table.
*
* @param    hwmgr   the address of the hardware manager
* @param    table   the SMC DPM table structure to be populated
* @return   always 0
*/
static int fiji_populate_vr_config(struct pp_hwmgr *hwmgr,
		struct SMU73_Discrete_DpmTable *table)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint16_t config;

	config = VR_MERGED_WITH_VDDC;
	table->VRConfig |= (config << VRCONF_VDDGFX_SHIFT);

	/* Set Vddc Voltage Controller */
	if(FIJI_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		config = VR_SVI2_PLANE_1;
		table->VRConfig |= config;
	} else {
		PP_ASSERT_WITH_CODE(false,
				"VDDC should be on SVI2 control in merged mode!",);
	}
	/* Set Vddci Voltage Controller */
	if(FIJI_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control) {
		config = VR_SVI2_PLANE_2;  /* only in merged mode */
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	} else if (FIJI_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		config = VR_SMIO_PATTERN_1;
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	} else {
		config = VR_STATIC_VOLTAGE;
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	}
	/* Set Mvdd Voltage Controller */
	if(FIJI_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control) {
		config = VR_SVI2_PLANE_2;
		table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
	} else if(FIJI_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		config = VR_SMIO_PATTERN_2;
		table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
	} else {
		config = VR_STATIC_VOLTAGE;
		table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
	}

	return 0;
}

/**
* Initializes the SMC table and uploads it
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput  the pointer to input data (PowerState)
* @return   always 0
*/
static int fiji_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct SMU73_Discrete_DpmTable *table = &(data->smc_state_table);
	const struct fiji_ulv_parm *ulv = &(data->ulv);
	uint8_t i;
	struct pp_atomctrl_gpio_pin_assignment gpio_pin;

	result = fiji_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to setup default DPM tables!", return result);

	if(FIJI_VOLTAGE_CONTROL_NONE != data->voltage_control)
		fiji_populate_smc_voltage_tables(hwmgr, table);

	table->SystemFlags = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StepVddc))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (data->is_memory_gddr5)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	if (ulv->ulv_supported && table_info->us_ulv_voltage_offset) {
		result = fiji_populate_ulv_state(hwmgr, table);
		PP_ASSERT_WITH_CODE(0 == result,
				"Failed to initialize ULV state!", return result);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_ULV_PARAMETER, ulv->cg_ulv_parameter);
	}

	result = fiji_populate_smc_link_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Link Level!", return result);

	result = fiji_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Graphics Level!", return result);

	result = fiji_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Memory Level!", return result);

	result = fiji_populate_smc_acpi_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize ACPI Level!", return result);

	result = fiji_populate_smc_vce_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize VCE Level!", return result);

	result = fiji_populate_smc_acp_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize ACP Level!", return result);

	result = fiji_populate_smc_samu_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize SAMU Level!", return result);

	/* Since only the initial state is completely set up at this point
	 * (the other states are just copies of the boot state) we only
	 * need to populate the  ARB settings for the initial state.
	 */
	result = fiji_program_memory_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to Write ARB settings for the initial state.", return result);

	result = fiji_populate_smc_uvd_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize UVD Level!", return result);

	result = fiji_populate_smc_boot_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Boot Level!", return result);

	result = fiji_populate_smc_initailial_state(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Boot State!", return result);

	result = fiji_populate_bapm_parameters_in_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to populate BAPM Parameters!", return result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ClockStretcher)) {
		result = fiji_populate_clock_stretcher_data_table(hwmgr);
		PP_ASSERT_WITH_CODE(0 == result,
				"Failed to populate Clock Stretcher Data Table!",
				return result);
	}

	table->GraphicsVoltageChangeEnable  = 1;
	table->GraphicsThermThrottleEnable  = 1;
	table->GraphicsInterval = 1;
	table->VoltageInterval  = 1;
	table->ThermalInterval  = 1;
	table->TemperatureLimitHigh =
			table_info->cac_dtp_table->usTargetOperatingTemp *
			FIJI_Q88_FORMAT_CONVERSION_UNIT;
	table->TemperatureLimitLow  =
			(table_info->cac_dtp_table->usTargetOperatingTemp - 1) *
			FIJI_Q88_FORMAT_CONVERSION_UNIT;
	table->MemoryVoltageChangeEnable = 1;
	table->MemoryInterval = 1;
	table->VoltageResponseTime = 0;
	table->PhaseResponseTime = 0;
	table->MemoryThermThrottleEnable = 1;
	table->PCIeBootLinkLevel = 0;      /* 0:Gen1 1:Gen2 2:Gen3*/
	table->PCIeGenInterval = 1;
	table->VRConfig = 0;

	result = fiji_populate_vr_config(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to populate VRConfig setting!", return result);

	table->ThermGpio = 17;
	table->SclkStepSize = 0x4000;

	if (atomctrl_get_pp_assign_pin(hwmgr, VDDC_VRHOT_GPIO_PINID, &gpio_pin)) {
		table->VRHotGpio = gpio_pin.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_RegulatorHot);
	} else {
		table->VRHotGpio = FIJI_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_RegulatorHot);
	}

	if (atomctrl_get_pp_assign_pin(hwmgr, PP_AC_DC_SWITCH_GPIO_PINID,
			&gpio_pin)) {
		table->AcDcGpio = gpio_pin.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition);
	} else {
		table->AcDcGpio = FIJI_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition);
	}

	/* Thermal Output GPIO */
	if (atomctrl_get_pp_assign_pin(hwmgr, THERMAL_INT_OUTPUT_GPIO_PINID,
			&gpio_pin)) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ThermalOutGPIO);

		table->ThermOutGpio = gpio_pin.uc_gpio_pin_bit_shift;

		/* For porlarity read GPIOPAD_A with assigned Gpio pin
		 * since VBIOS will program this register to set 'inactive state',
		 * driver can then determine 'active state' from this and
		 * program SMU with correct polarity
		 */
		table->ThermOutPolarity = (0 == (cgs_read_register(hwmgr->device, mmGPIOPAD_A) &
				(1 << gpio_pin.uc_gpio_pin_bit_shift))) ? 1:0;
		table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_ONLY;

		/* if required, combine VRHot/PCC with thermal out GPIO */
		if(phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_RegulatorHot) &&
			phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_CombinePCCWithThermalSignal))
			table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_VRHOT;
	} else {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ThermalOutGPIO);
		table->ThermOutGpio = 17;
		table->ThermOutPolarity = 1;
		table->ThermOutMode = SMU7_THERM_OUT_MODE_DISABLE;
	}

	for (i = 0; i < SMU73_MAX_ENTRIES_SMIO; i++)
		table->Smio[i] = PP_HOST_TO_SMC_UL(table->Smio[i]);

	CONVERT_FROM_HOST_TO_SMC_UL(table->SystemFlags);
	CONVERT_FROM_HOST_TO_SMC_UL(table->VRConfig);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMask1);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMask2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SclkStepSize);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitHigh);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitLow);
	CONVERT_FROM_HOST_TO_SMC_US(table->VoltageResponseTime);
	CONVERT_FROM_HOST_TO_SMC_US(table->PhaseResponseTime);

	/* Upload all dpm data to SMC memory.(dpm level, dpm level count etc) */
	result = fiji_copy_bytes_to_smc(hwmgr->smumgr,
			data->dpm_table_start +
			offsetof(SMU73_Discrete_DpmTable, SystemFlags),
			(uint8_t *)&(table->SystemFlags),
			sizeof(SMU73_Discrete_DpmTable) - 3 * sizeof(SMU73_PIDController),
			data->sram_end);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to upload dpm data to SMC memory!", return result);

	return 0;
}

/**
* Initialize the ARB DRAM timing table's index field.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int fiji_init_arb_table_index(struct pp_hwmgr *hwmgr)
{
	const struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t tmp;
	int result;

	/* This is a read-modify-write on the first byte of the ARB table.
	 * The first byte in the SMU73_Discrete_MCArbDramTimingTable structure
	 * is the field 'current'.
	 * This solution is ugly, but we never write the whole table only
	 * individual fields in it.
	 * In reality this field should not be in that structure
	 * but in a soft register.
	 */
	result = fiji_read_smc_sram_dword(hwmgr->smumgr,
			data->arb_table_start, &tmp, data->sram_end);

	if (result)
		return result;

	tmp &= 0x00FFFFFF;
	tmp |= ((uint32_t)MC_CG_ARB_FREQ_F1) << 24;

	return fiji_write_smc_sram_dword(hwmgr->smumgr,
			data->arb_table_start,  tmp, data->sram_end);
}

static int fiji_enable_vrhot_gpio_interrupt(struct pp_hwmgr *hwmgr)
{
	if(phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_EnableVRHotGPIOInterrupt);

	return 0;
}

static int fiji_enable_sclk_control(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL,
			SCLK_PWRMGT_OFF, 0);
	return 0;
}

static int fiji_enable_ulv(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_ulv_parm *ulv = &(data->ulv);

	if (ulv->ulv_supported)
		return smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_EnableULV);

	return 0;
}

static int fiji_enable_deep_sleep_master_switch(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep)) {
		if (smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_MASTER_DeepSleep_ON))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to enable Master Deep Sleep switch failed!",
					return -1);
	} else {
		if (smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_MASTER_DeepSleep_OFF)) {
			PP_ASSERT_WITH_CODE(false,
					"Attempt to disable Master Deep Sleep switch failed!",
					return -1);
		}
	}

	return 0;
}

static int fiji_enable_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t   val, val0, val2;
	uint32_t   i, cpl_cntl, cpl_threshold, mc_threshold;

	/* enable SCLK dpm */
	if(!data->sclk_dpm_key_disabled)
		PP_ASSERT_WITH_CODE(
		(0 == smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_DPM_Enable)),
		"Failed to enable SCLK DPM during DPM Start Function!",
		return -1);

	/* enable MCLK dpm */
	if(0 == data->mclk_dpm_key_disabled) {
		cpl_threshold = 0;
		mc_threshold = 0;

		/* Read per MCD tile (0 - 7) */
		for (i = 0; i < 8; i++) {
			PHM_WRITE_FIELD(hwmgr->device, MC_CONFIG_MCD, MC_RD_ENABLE, i);
			val = cgs_read_register(hwmgr->device, mmMC_SEQ_RESERVE_0_S) & 0xf0000000;
			if (0xf0000000 != val) {
				/* count number of MCQ that has channel(s) enabled */
				cpl_threshold++;
				/* only harvest 3 or full 4 supported */
				mc_threshold = val ? 3 : 4;
			}
		}
		PP_ASSERT_WITH_CODE(0 != cpl_threshold,
				"Number of MCQ is zero!", return -EINVAL;);

		mc_threshold = ((mc_threshold & LCAC_MC0_CNTL__MC0_THRESHOLD_MASK) <<
				LCAC_MC0_CNTL__MC0_THRESHOLD__SHIFT) |
						LCAC_MC0_CNTL__MC0_ENABLE_MASK;
		cpl_cntl = ((cpl_threshold & LCAC_CPL_CNTL__CPL_THRESHOLD_MASK) <<
				LCAC_CPL_CNTL__CPL_THRESHOLD__SHIFT) |
						LCAC_CPL_CNTL__CPL_ENABLE_MASK;
		cpl_cntl = (cpl_cntl | (8 << LCAC_CPL_CNTL__CPL_BLOCK_ID__SHIFT));
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixLCAC_MC0_CNTL, mc_threshold);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixLCAC_MC1_CNTL, mc_threshold);
		if (8 == cpl_threshold) {
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC2_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC3_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC4_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC5_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC6_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC7_CNTL, mc_threshold);
		}
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixLCAC_CPL_CNTL, cpl_cntl);

		udelay(5);

		mc_threshold = mc_threshold |
				(1 << LCAC_MC0_CNTL__MC0_SIGNAL_ID__SHIFT);
		cpl_cntl = cpl_cntl | (1 << LCAC_CPL_CNTL__CPL_SIGNAL_ID__SHIFT);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixLCAC_MC0_CNTL, mc_threshold);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixLCAC_MC1_CNTL, mc_threshold);
		if (8 == cpl_threshold) {
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC2_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC3_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC4_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC5_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC6_CNTL, mc_threshold);
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixLCAC_MC7_CNTL, mc_threshold);
		}
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixLCAC_CPL_CNTL, cpl_cntl);

		/* Program CAC_EN per MCD (0-7) Tile */
		val0 = val = cgs_read_register(hwmgr->device, mmMC_CONFIG_MCD);
		val &= ~(MC_CONFIG_MCD__MCD0_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MCD1_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MCD2_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MCD3_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MCD4_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MCD5_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MCD6_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MCD7_WR_ENABLE_MASK |
				MC_CONFIG_MCD__MC_RD_ENABLE_MASK);

		for (i = 0; i < 8; i++) {
			/* Enable MCD i Tile read & write */
			val2  = (val | (i << MC_CONFIG_MCD__MC_RD_ENABLE__SHIFT) |
					(1 << i));
			cgs_write_register(hwmgr->device, mmMC_CONFIG_MCD, val2);
			/* Enbale CAC_ON MCD i Tile */
			val2 = cgs_read_register(hwmgr->device, mmMC_SEQ_CNTL);
			val2 |= MC_SEQ_CNTL__CAC_EN_MASK;
			cgs_write_register(hwmgr->device, mmMC_SEQ_CNTL, val2);
		}
		/* Set MC_CONFIG_MCD back to its default setting val0 */
		cgs_write_register(hwmgr->device, mmMC_CONFIG_MCD, val0);

		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_MCLKDPM_Enable)),
				"Failed to enable MCLK DPM during DPM Start Function!",
				return -1);
	}
	return 0;
}

static int fiji_start_dpm(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	/*enable general power management */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT,
			GLOBAL_PWRMGT_EN, 1);
	/* enable sclk deep sleep */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL,
			DYNAMIC_PM_EN, 1);
	/* prepare for PCIE DPM */
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			data->soft_regs_start + offsetof(SMU73_SoftRegisters,
					VoltageChangeTimeout), 0x1000);
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__PCIE,
			SWRST_COMMAND_1, RESETLC, 0x0);

	PP_ASSERT_WITH_CODE(
			(0 == smum_send_msg_to_smc(hwmgr->smumgr,
					PPSMC_MSG_Voltage_Cntl_Enable)),
			"Failed to enable voltage DPM during DPM Start Function!",
			return -1);

	if (fiji_enable_sclk_mclk_dpm(hwmgr)) {
		printk(KERN_ERR "Failed to enable Sclk DPM and Mclk DPM!");
		return -1;
	}

	/* enable PCIE dpm */
	if(!data->pcie_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_PCIeDPM_Enable)),
				"Failed to enable pcie DPM during DPM Start Function!",
				return -1);
	}

	return 0;
}

static void fiji_set_dpm_event_sources(struct pp_hwmgr *hwmgr,
		uint32_t sources)
{
	bool protection;
	enum DPM_EVENT_SRC src;

	switch (sources) {
	default:
		printk(KERN_ERR "Unknown throttling event sources.");
		/* fall through */
	case 0:
		protection = false;
		/* src is unused */
		break;
	case (1 << PHM_AutoThrottleSource_Thermal):
		protection = true;
		src = DPM_EVENT_SRC_DIGITAL;
		break;
	case (1 << PHM_AutoThrottleSource_External):
		protection = true;
		src = DPM_EVENT_SRC_EXTERNAL;
		break;
	case (1 << PHM_AutoThrottleSource_External) |
			(1 << PHM_AutoThrottleSource_Thermal):
		protection = true;
		src = DPM_EVENT_SRC_DIGITAL_OR_EXTERNAL;
		break;
	}
	/* Order matters - don't enable thermal protection for the wrong source. */
	if (protection) {
		PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_THERMAL_CTRL,
				DPM_EVENT_SRC, src);
		PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT,
				THERMAL_PROTECTION_DIS,
				!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_ThermalController));
	} else
		PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT,
				THERMAL_PROTECTION_DIS, 1);
}

static int fiji_enable_auto_throttle_source(struct pp_hwmgr *hwmgr,
		PHM_AutoThrottleSource source)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (!(data->active_auto_throttle_sources & (1 << source))) {
		data->active_auto_throttle_sources |= 1 << source;
		fiji_set_dpm_event_sources(hwmgr, data->active_auto_throttle_sources);
	}
	return 0;
}

static int fiji_enable_thermal_auto_throttle(struct pp_hwmgr *hwmgr)
{
	return fiji_enable_auto_throttle_source(hwmgr, PHM_AutoThrottleSource_Thermal);
}

static int fiji_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	tmp_result = (!fiji_is_dpm_running(hwmgr))? 0 : -1;
	PP_ASSERT_WITH_CODE(result == 0,
			"DPM is already running right now, no need to enable DPM!",
			return 0);

	if (fiji_voltage_control(hwmgr)) {
		tmp_result = fiji_enable_voltage_control(hwmgr);
		PP_ASSERT_WITH_CODE(tmp_result == 0,
				"Failed to enable voltage control!",
				result = tmp_result);
	}

	if (fiji_voltage_control(hwmgr)) {
		tmp_result = fiji_construct_voltage_tables(hwmgr);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
				"Failed to contruct voltage tables!",
				result = tmp_result);
	}

	tmp_result = fiji_initialize_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to initialize MC reg table!", result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EngineSpreadSpectrumSupport))
		PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				GENERAL_PWRMGT, DYN_SPREAD_SPECTRUM_EN, 1);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalController))
		PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				GENERAL_PWRMGT, THERMAL_PROTECTION_DIS, 0);

	tmp_result = fiji_program_static_screen_threshold_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to program static screen threshold parameters!",
			result = tmp_result);

	tmp_result = fiji_enable_display_gap(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable display gap!", result = tmp_result);

	tmp_result = fiji_program_voting_clients(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to program voting clients!", result = tmp_result);

	tmp_result = fiji_process_firmware_header(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to process firmware header!", result = tmp_result);

	tmp_result = fiji_initial_switch_from_arbf0_to_f1(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to initialize switch from ArbF0 to F1!",
			result = tmp_result);

	tmp_result = fiji_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to initialize SMC table!", result = tmp_result);

	tmp_result = fiji_init_arb_table_index(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to initialize ARB table index!", result = tmp_result);

	tmp_result = fiji_populate_pm_fuses(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to populate PM fuses!", result = tmp_result);

	tmp_result = fiji_enable_vrhot_gpio_interrupt(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable VR hot GPIO interrupt!", result = tmp_result);

	tmp_result = tonga_notify_smc_display_change(hwmgr, false);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to notify no display!", result = tmp_result);

	tmp_result = fiji_enable_sclk_control(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable SCLK control!", result = tmp_result);

	tmp_result = fiji_enable_ulv(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable ULV!", result = tmp_result);

	tmp_result = fiji_enable_deep_sleep_master_switch(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable deep sleep master switch!", result = tmp_result);

	tmp_result = fiji_start_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to start DPM!", result = tmp_result);

	tmp_result = fiji_enable_smc_cac(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable SMC CAC!", result = tmp_result);

	tmp_result = fiji_enable_power_containment(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable power containment!", result = tmp_result);

	tmp_result = fiji_power_control_set_level(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to power control set level!", result = tmp_result);

	tmp_result = fiji_enable_thermal_auto_throttle(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable thermal auto throttle!", result = tmp_result);

	return result;
}

static int fiji_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t level, tmp;

	if (!data->sclk_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			level = 0;
			tmp = data->dpm_level_enable_mask.sclk_dpm_enable_mask;
			while (tmp >>= 1)
				level++;
			if (level)
				smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_SCLKDPM_SetEnabledMask,
						(1 << level));
		}
	}

	if (!data->mclk_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			level = 0;
			tmp = data->dpm_level_enable_mask.mclk_dpm_enable_mask;
			while (tmp >>= 1)
				level++;
			if (level)
				smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_MCLKDPM_SetEnabledMask,
						(1 << level));
		}
	}

	if (!data->pcie_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			level = 0;
			tmp = data->dpm_level_enable_mask.pcie_dpm_enable_mask;
			while (tmp >>= 1)
				level++;
			if (level)
				smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_PCIeDPM_ForceLevel,
						(1 << level));
		}
	}
	return 0;
}

static int fiji_upload_dpmlevel_enable_mask(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	phm_apply_dal_min_voltage_request(hwmgr);

	if (!data->sclk_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SCLKDPM_SetEnabledMask,
					data->dpm_level_enable_mask.sclk_dpm_enable_mask);
	}
	return 0;
}

static int fiji_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (!fiji_is_dpm_running(hwmgr))
		return -EINVAL;

	if (!data->pcie_dpm_key_disabled) {
		smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_PCIeDPM_UnForceLevel);
	}

	return fiji_upload_dpmlevel_enable_mask(hwmgr);
}

static uint32_t fiji_get_lowest_enabled_level(
		struct pp_hwmgr *hwmgr, uint32_t mask)
{
	uint32_t level = 0;

	while(0 == (mask & (1 << level)))
		level++;

	return level;
}

static int fiji_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data =
			(struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t level;

	if (!data->sclk_dpm_key_disabled)
		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			level = fiji_get_lowest_enabled_level(hwmgr,
							      data->dpm_level_enable_mask.sclk_dpm_enable_mask);
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
							    PPSMC_MSG_SCLKDPM_SetEnabledMask,
							    (1 << level));

	}

	if (!data->mclk_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			level = fiji_get_lowest_enabled_level(hwmgr,
							      data->dpm_level_enable_mask.mclk_dpm_enable_mask);
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
							    PPSMC_MSG_MCLKDPM_SetEnabledMask,
							    (1 << level));
		}
	}

	if (!data->pcie_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			level = fiji_get_lowest_enabled_level(hwmgr,
							      data->dpm_level_enable_mask.pcie_dpm_enable_mask);
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
							    PPSMC_MSG_PCIeDPM_ForceLevel,
							    (1 << level));
		}
	}

	return 0;

}
static int fiji_dpm_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = fiji_force_dpm_highest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = fiji_force_dpm_lowest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = fiji_unforce_dpm_levels(hwmgr);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	hwmgr->dpm_level = level;

	return ret;
}

static int fiji_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct fiji_power_state);
}

static int fiji_get_pp_table_entry_callback_func(struct pp_hwmgr *hwmgr,
		void *state, struct pp_power_state *power_state,
		void *pp_table, uint32_t classification_flag)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_power_state  *fiji_power_state =
			(struct fiji_power_state *)(&(power_state->hardware));
	struct fiji_performance_level *performance_level;
	ATOM_Tonga_State *state_entry = (ATOM_Tonga_State *)state;
	ATOM_Tonga_POWERPLAYTABLE *powerplay_table =
			(ATOM_Tonga_POWERPLAYTABLE *)pp_table;
	ATOM_Tonga_SCLK_Dependency_Table *sclk_dep_table =
			(ATOM_Tonga_SCLK_Dependency_Table *)
			(((unsigned long)powerplay_table) +
				le16_to_cpu(powerplay_table->usSclkDependencyTableOffset));
	ATOM_Tonga_MCLK_Dependency_Table *mclk_dep_table =
			(ATOM_Tonga_MCLK_Dependency_Table *)
			(((unsigned long)powerplay_table) +
				le16_to_cpu(powerplay_table->usMclkDependencyTableOffset));

	/* The following fields are not initialized here: id orderedList allStatesList */
	power_state->classification.ui_label =
			(le16_to_cpu(state_entry->usClassification) &
			ATOM_PPLIB_CLASSIFICATION_UI_MASK) >>
			ATOM_PPLIB_CLASSIFICATION_UI_SHIFT;
	power_state->classification.flags = classification_flag;
	/* NOTE: There is a classification2 flag in BIOS that is not being used right now */

	power_state->classification.temporary_state = false;
	power_state->classification.to_be_deleted = false;

	power_state->validation.disallowOnDC =
			(0 != (le32_to_cpu(state_entry->ulCapsAndSettings) &
					ATOM_Tonga_DISALLOW_ON_DC));

	power_state->pcie.lanes = 0;

	power_state->display.disableFrameModulation = false;
	power_state->display.limitRefreshrate = false;
	power_state->display.enableVariBright =
			(0 != (le32_to_cpu(state_entry->ulCapsAndSettings) &
					ATOM_Tonga_ENABLE_VARIBRIGHT));

	power_state->validation.supportedPowerLevels = 0;
	power_state->uvd_clocks.VCLK = 0;
	power_state->uvd_clocks.DCLK = 0;
	power_state->temperatures.min = 0;
	power_state->temperatures.max = 0;

	performance_level = &(fiji_power_state->performance_levels
			[fiji_power_state->performance_level_count++]);

	PP_ASSERT_WITH_CODE(
			(fiji_power_state->performance_level_count < SMU73_MAX_LEVELS_GRAPHICS),
			"Performance levels exceeds SMC limit!",
			return -1);

	PP_ASSERT_WITH_CODE(
			(fiji_power_state->performance_level_count <=
					hwmgr->platform_descriptor.hardwareActivityPerformanceLevels),
			"Performance levels exceeds Driver limit!",
			return -1);

	/* Performance levels are arranged from low to high. */
	performance_level->memory_clock = mclk_dep_table->entries
			[state_entry->ucMemoryClockIndexLow].ulMclk;
	performance_level->engine_clock = sclk_dep_table->entries
			[state_entry->ucEngineClockIndexLow].ulSclk;
	performance_level->pcie_gen = get_pcie_gen_support(data->pcie_gen_cap,
			state_entry->ucPCIEGenLow);
	performance_level->pcie_lane = get_pcie_lane_support(data->pcie_lane_cap,
			state_entry->ucPCIELaneHigh);

	performance_level = &(fiji_power_state->performance_levels
			[fiji_power_state->performance_level_count++]);
	performance_level->memory_clock = mclk_dep_table->entries
			[state_entry->ucMemoryClockIndexHigh].ulMclk;
	performance_level->engine_clock = sclk_dep_table->entries
			[state_entry->ucEngineClockIndexHigh].ulSclk;
	performance_level->pcie_gen = get_pcie_gen_support(data->pcie_gen_cap,
			state_entry->ucPCIEGenHigh);
	performance_level->pcie_lane = get_pcie_lane_support(data->pcie_lane_cap,
			state_entry->ucPCIELaneHigh);

	return 0;
}

static int fiji_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		unsigned long entry_index, struct pp_power_state *state)
{
	int result;
	struct fiji_power_state *ps;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_mclk_table =
			table_info->vdd_dep_on_mclk;

	state->hardware.magic = PHM_VIslands_Magic;

	ps = (struct fiji_power_state *)(&state->hardware);

	result = tonga_get_powerplay_table_entry(hwmgr, entry_index, state,
			fiji_get_pp_table_entry_callback_func);

	/* This is the earliest time we have all the dependency table and the VBIOS boot state
	 * as PP_Tables_GetPowerPlayTableEntry retrieves the VBIOS boot state
	 * if there is only one VDDCI/MCLK level, check if it's the same as VBIOS boot state
	 */
	if (dep_mclk_table != NULL && dep_mclk_table->count == 1) {
		if (dep_mclk_table->entries[0].clk !=
				data->vbios_boot_state.mclk_bootup_value)
			printk(KERN_ERR "Single MCLK entry VDDCI/MCLK dependency table "
					"does not match VBIOS boot MCLK level");
		if (dep_mclk_table->entries[0].vddci !=
				data->vbios_boot_state.vddci_bootup_value)
			printk(KERN_ERR "Single VDDCI entry VDDCI/MCLK dependency table "
					"does not match VBIOS boot VDDCI level");
	}

	/* set DC compatible flag if this state supports DC */
	if (!state->validation.disallowOnDC)
		ps->dc_compatible = true;

	if (state->classification.flags & PP_StateClassificationFlag_ACPI)
		data->acpi_pcie_gen = ps->performance_levels[0].pcie_gen;

	ps->uvd_clks.vclk = state->uvd_clocks.VCLK;
	ps->uvd_clks.dclk = state->uvd_clocks.DCLK;

	if (!result) {
		uint32_t i;

		switch (state->classification.ui_label) {
		case PP_StateUILabel_Performance:
			data->use_pcie_performance_levels = true;

			for (i = 0; i < ps->performance_level_count; i++) {
				if (data->pcie_gen_performance.max <
						ps->performance_levels[i].pcie_gen)
					data->pcie_gen_performance.max =
							ps->performance_levels[i].pcie_gen;

				if (data->pcie_gen_performance.min >
						ps->performance_levels[i].pcie_gen)
					data->pcie_gen_performance.min =
							ps->performance_levels[i].pcie_gen;

				if (data->pcie_lane_performance.max <
						ps->performance_levels[i].pcie_lane)
					data->pcie_lane_performance.max =
							ps->performance_levels[i].pcie_lane;

				if (data->pcie_lane_performance.min >
						ps->performance_levels[i].pcie_lane)
					data->pcie_lane_performance.min =
							ps->performance_levels[i].pcie_lane;
			}
			break;
		case PP_StateUILabel_Battery:
			data->use_pcie_power_saving_levels = true;

			for (i = 0; i < ps->performance_level_count; i++) {
				if (data->pcie_gen_power_saving.max <
						ps->performance_levels[i].pcie_gen)
					data->pcie_gen_power_saving.max =
							ps->performance_levels[i].pcie_gen;

				if (data->pcie_gen_power_saving.min >
						ps->performance_levels[i].pcie_gen)
					data->pcie_gen_power_saving.min =
							ps->performance_levels[i].pcie_gen;

				if (data->pcie_lane_power_saving.max <
						ps->performance_levels[i].pcie_lane)
					data->pcie_lane_power_saving.max =
							ps->performance_levels[i].pcie_lane;

				if (data->pcie_lane_power_saving.min >
						ps->performance_levels[i].pcie_lane)
					data->pcie_lane_power_saving.min =
							ps->performance_levels[i].pcie_lane;
			}
			break;
		default:
			break;
		}
	}
	return 0;
}

static int fiji_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *request_ps,
			const struct pp_power_state *current_ps)
{
	struct fiji_power_state *fiji_ps =
				cast_phw_fiji_power_state(&request_ps->hardware);
	uint32_t sclk;
	uint32_t mclk;
	struct PP_Clocks minimum_clocks = {0};
	bool disable_mclk_switching;
	bool disable_mclk_switching_for_frame_lock;
	struct cgs_display_info info = {0};
	const struct phm_clock_and_voltage_limits *max_limits;
	uint32_t i;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	int32_t count;
	int32_t stable_pstate_sclk = 0, stable_pstate_mclk = 0;

	data->battery_state = (PP_StateUILabel_Battery ==
			request_ps->classification.ui_label);

	PP_ASSERT_WITH_CODE(fiji_ps->performance_level_count == 2,
				 "VI should always have 2 performance levels",);

	max_limits = (PP_PowerSource_AC == hwmgr->power_source) ?
			&(hwmgr->dyn_state.max_clock_voltage_on_ac) :
			&(hwmgr->dyn_state.max_clock_voltage_on_dc);

	/* Cap clock DPM tables at DC MAX if it is in DC. */
	if (PP_PowerSource_DC == hwmgr->power_source) {
		for (i = 0; i < fiji_ps->performance_level_count; i++) {
			if (fiji_ps->performance_levels[i].memory_clock > max_limits->mclk)
				fiji_ps->performance_levels[i].memory_clock = max_limits->mclk;
			if (fiji_ps->performance_levels[i].engine_clock > max_limits->sclk)
				fiji_ps->performance_levels[i].engine_clock = max_limits->sclk;
		}
	}

	fiji_ps->vce_clks.evclk = hwmgr->vce_arbiter.evclk;
	fiji_ps->vce_clks.ecclk = hwmgr->vce_arbiter.ecclk;

	fiji_ps->acp_clk = hwmgr->acp_arbiter.acpclk;

	cgs_get_active_displays_info(hwmgr->device, &info);

	/*TO DO result = PHM_CheckVBlankTime(hwmgr, &vblankTooShort);*/

	/* TO DO GetMinClockSettings(hwmgr->pPECI, &minimum_clocks); */

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StablePState)) {
		max_limits = &(hwmgr->dyn_state.max_clock_voltage_on_ac);
		stable_pstate_sclk = (max_limits->sclk * 75) / 100;

		for (count = table_info->vdd_dep_on_sclk->count - 1;
				count >= 0; count--) {
			if (stable_pstate_sclk >=
					table_info->vdd_dep_on_sclk->entries[count].clk) {
				stable_pstate_sclk =
						table_info->vdd_dep_on_sclk->entries[count].clk;
				break;
			}
		}

		if (count < 0)
			stable_pstate_sclk = table_info->vdd_dep_on_sclk->entries[0].clk;

		stable_pstate_mclk = max_limits->mclk;

		minimum_clocks.engineClock = stable_pstate_sclk;
		minimum_clocks.memoryClock = stable_pstate_mclk;
	}

	if (minimum_clocks.engineClock < hwmgr->gfx_arbiter.sclk)
		minimum_clocks.engineClock = hwmgr->gfx_arbiter.sclk;

	if (minimum_clocks.memoryClock < hwmgr->gfx_arbiter.mclk)
		minimum_clocks.memoryClock = hwmgr->gfx_arbiter.mclk;

	fiji_ps->sclk_threshold = hwmgr->gfx_arbiter.sclk_threshold;

	if (0 != hwmgr->gfx_arbiter.sclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.sclk_over_drive <=
				hwmgr->platform_descriptor.overdriveLimit.engineClock),
				"Overdrive sclk exceeds limit",
				hwmgr->gfx_arbiter.sclk_over_drive =
						hwmgr->platform_descriptor.overdriveLimit.engineClock);

		if (hwmgr->gfx_arbiter.sclk_over_drive >= hwmgr->gfx_arbiter.sclk)
			fiji_ps->performance_levels[1].engine_clock =
					hwmgr->gfx_arbiter.sclk_over_drive;
	}

	if (0 != hwmgr->gfx_arbiter.mclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.mclk_over_drive <=
				hwmgr->platform_descriptor.overdriveLimit.memoryClock),
				"Overdrive mclk exceeds limit",
				hwmgr->gfx_arbiter.mclk_over_drive =
						hwmgr->platform_descriptor.overdriveLimit.memoryClock);

		if (hwmgr->gfx_arbiter.mclk_over_drive >= hwmgr->gfx_arbiter.mclk)
			fiji_ps->performance_levels[1].memory_clock =
					hwmgr->gfx_arbiter.mclk_over_drive;
	}

	disable_mclk_switching_for_frame_lock = phm_cap_enabled(
				    hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_DisableMclkSwitchingForFrameLock);

	disable_mclk_switching = (1 < info.display_count) ||
				    disable_mclk_switching_for_frame_lock;

	sclk = fiji_ps->performance_levels[0].engine_clock;
	mclk = fiji_ps->performance_levels[0].memory_clock;

	if (disable_mclk_switching)
		mclk = fiji_ps->performance_levels
		[fiji_ps->performance_level_count - 1].memory_clock;

	if (sclk < minimum_clocks.engineClock)
		sclk = (minimum_clocks.engineClock > max_limits->sclk) ?
				max_limits->sclk : minimum_clocks.engineClock;

	if (mclk < minimum_clocks.memoryClock)
		mclk = (minimum_clocks.memoryClock > max_limits->mclk) ?
				max_limits->mclk : minimum_clocks.memoryClock;

	fiji_ps->performance_levels[0].engine_clock = sclk;
	fiji_ps->performance_levels[0].memory_clock = mclk;

	fiji_ps->performance_levels[1].engine_clock =
		(fiji_ps->performance_levels[1].engine_clock >=
				fiji_ps->performance_levels[0].engine_clock) ?
						fiji_ps->performance_levels[1].engine_clock :
						fiji_ps->performance_levels[0].engine_clock;

	if (disable_mclk_switching) {
		if (mclk < fiji_ps->performance_levels[1].memory_clock)
			mclk = fiji_ps->performance_levels[1].memory_clock;

		fiji_ps->performance_levels[0].memory_clock = mclk;
		fiji_ps->performance_levels[1].memory_clock = mclk;
	} else {
		if (fiji_ps->performance_levels[1].memory_clock <
				fiji_ps->performance_levels[0].memory_clock)
			fiji_ps->performance_levels[1].memory_clock =
					fiji_ps->performance_levels[0].memory_clock;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StablePState)) {
		for (i = 0; i < fiji_ps->performance_level_count; i++) {
			fiji_ps->performance_levels[i].engine_clock = stable_pstate_sclk;
			fiji_ps->performance_levels[i].memory_clock = stable_pstate_mclk;
			fiji_ps->performance_levels[i].pcie_gen = data->pcie_gen_performance.max;
			fiji_ps->performance_levels[i].pcie_lane = data->pcie_gen_performance.max;
		}
	}

	return 0;
}

static int fiji_find_dpm_states_clocks_in_dpm_table(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct fiji_power_state *fiji_ps =
			cast_const_phw_fiji_power_state(states->pnew_state);
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_single_dpm_table *sclk_table = &(data->dpm_table.sclk_table);
	uint32_t sclk = fiji_ps->performance_levels
			[fiji_ps->performance_level_count - 1].engine_clock;
	struct fiji_single_dpm_table *mclk_table = &(data->dpm_table.mclk_table);
	uint32_t mclk = fiji_ps->performance_levels
			[fiji_ps->performance_level_count - 1].memory_clock;
	uint32_t i;
	struct cgs_display_info info = {0};

	data->need_update_smu7_dpm_table = 0;

	for (i = 0; i < sclk_table->count; i++) {
		if (sclk == sclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= sclk_table->count)
		data->need_update_smu7_dpm_table |= DPMTABLE_OD_UPDATE_SCLK;
	else {
		if(data->display_timing.min_clock_in_sr !=
			hwmgr->display_config.min_core_set_clock_in_sr)
			data->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_SCLK;
	}

	for (i = 0; i < mclk_table->count; i++) {
		if (mclk == mclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= mclk_table->count)
		data->need_update_smu7_dpm_table |= DPMTABLE_OD_UPDATE_MCLK;

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (data->display_timing.num_existing_displays != info.display_count)
		data->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_MCLK;

	return 0;
}

static uint16_t fiji_get_maximum_link_speed(struct pp_hwmgr *hwmgr,
		const struct fiji_power_state *fiji_ps)
{
	uint32_t i;
	uint32_t sclk, max_sclk = 0;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_dpm_table *dpm_table = &data->dpm_table;

	for (i = 0; i < fiji_ps->performance_level_count; i++) {
		sclk = fiji_ps->performance_levels[i].engine_clock;
		if (max_sclk < sclk)
			max_sclk = sclk;
	}

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		if (dpm_table->sclk_table.dpm_levels[i].value == max_sclk)
			return (uint16_t) ((i >= dpm_table->pcie_speed_table.count) ?
					dpm_table->pcie_speed_table.dpm_levels
					[dpm_table->pcie_speed_table.count - 1].value :
					dpm_table->pcie_speed_table.dpm_levels[i].value);
	}

	return 0;
}

static int fiji_request_link_speed_change_before_state_change(
		struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	const struct fiji_power_state *fiji_nps =
			cast_const_phw_fiji_power_state(states->pnew_state);
	const struct fiji_power_state *fiji_cps =
			cast_const_phw_fiji_power_state(states->pcurrent_state);

	uint16_t target_link_speed = fiji_get_maximum_link_speed(hwmgr, fiji_nps);
	uint16_t current_link_speed;

	if (data->force_pcie_gen == PP_PCIEGenInvalid)
		current_link_speed = fiji_get_maximum_link_speed(hwmgr, fiji_cps);
	else
		current_link_speed = data->force_pcie_gen;

	data->force_pcie_gen = PP_PCIEGenInvalid;
	data->pspp_notify_required = false;
	if (target_link_speed > current_link_speed) {
		switch(target_link_speed) {
		case PP_PCIEGen3:
			if (0 == acpi_pcie_perf_request(hwmgr->device, PCIE_PERF_REQ_GEN3, false))
				break;
			data->force_pcie_gen = PP_PCIEGen2;
			if (current_link_speed == PP_PCIEGen2)
				break;
		case PP_PCIEGen2:
			if (0 == acpi_pcie_perf_request(hwmgr->device, PCIE_PERF_REQ_GEN2, false))
				break;
		default:
			data->force_pcie_gen = fiji_get_current_pcie_speed(hwmgr);
			break;
		}
	} else {
		if (target_link_speed < current_link_speed)
			data->pspp_notify_required = true;
	}

	return 0;
}

static int fiji_freeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
			(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {
		PP_ASSERT_WITH_CODE(true == fiji_is_dpm_running(hwmgr),
				"Trying to freeze SCLK DPM when DPM is disabled",);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SCLKDPM_FreezeLevel),
				"Failed to freeze SCLK DPM during FreezeSclkMclkDPM Function!",
				return -1);
	}

	if ((0 == data->mclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		 DPMTABLE_OD_UPDATE_MCLK)) {
		PP_ASSERT_WITH_CODE(true == fiji_is_dpm_running(hwmgr),
				"Trying to freeze MCLK DPM when DPM is disabled",);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_MCLKDPM_FreezeLevel),
				"Failed to freeze MCLK DPM during FreezeSclkMclkDPM Function!",
				return -1);
	}

	return 0;
}

static int fiji_populate_and_upload_sclk_mclk_dpm_levels(
		struct pp_hwmgr *hwmgr, const void *input)
{
	int result = 0;
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct fiji_power_state *fiji_ps =
			cast_const_phw_fiji_power_state(states->pnew_state);
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t sclk = fiji_ps->performance_levels
			[fiji_ps->performance_level_count - 1].engine_clock;
	uint32_t mclk = fiji_ps->performance_levels
			[fiji_ps->performance_level_count - 1].memory_clock;
	struct fiji_dpm_table *dpm_table = &data->dpm_table;

	struct fiji_dpm_table *golden_dpm_table = &data->golden_dpm_table;
	uint32_t dpm_count, clock_percent;
	uint32_t i;

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_SCLK) {
		dpm_table->sclk_table.dpm_levels
		[dpm_table->sclk_table.count - 1].value = sclk;

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_OD6PlusinACSupport) ||
			phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_OD6PlusinDCSupport)) {
		/* Need to do calculation based on the golden DPM table
		 * as the Heatmap GPU Clock axis is also based on the default values
		 */
			PP_ASSERT_WITH_CODE(
				(golden_dpm_table->sclk_table.dpm_levels
						[golden_dpm_table->sclk_table.count - 1].value != 0),
				"Divide by 0!",
				return -1);
			dpm_count = dpm_table->sclk_table.count < 2 ?
					0 : dpm_table->sclk_table.count - 2;
			for (i = dpm_count; i > 1; i--) {
				if (sclk > golden_dpm_table->sclk_table.dpm_levels
						[golden_dpm_table->sclk_table.count-1].value) {
					clock_percent =
						((sclk - golden_dpm_table->sclk_table.dpm_levels
							[golden_dpm_table->sclk_table.count-1].value) * 100) /
						golden_dpm_table->sclk_table.dpm_levels
							[golden_dpm_table->sclk_table.count-1].value;

					dpm_table->sclk_table.dpm_levels[i].value =
							golden_dpm_table->sclk_table.dpm_levels[i].value +
							(golden_dpm_table->sclk_table.dpm_levels[i].value *
								clock_percent)/100;

				} else if (golden_dpm_table->sclk_table.dpm_levels
						[dpm_table->sclk_table.count-1].value > sclk) {
					clock_percent =
						((golden_dpm_table->sclk_table.dpm_levels
						[golden_dpm_table->sclk_table.count - 1].value - sclk) *
								100) /
						golden_dpm_table->sclk_table.dpm_levels
							[golden_dpm_table->sclk_table.count-1].value;

					dpm_table->sclk_table.dpm_levels[i].value =
							golden_dpm_table->sclk_table.dpm_levels[i].value -
							(golden_dpm_table->sclk_table.dpm_levels[i].value *
									clock_percent) / 100;
				} else
					dpm_table->sclk_table.dpm_levels[i].value =
							golden_dpm_table->sclk_table.dpm_levels[i].value;
			}
		}
	}

	if (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK) {
		dpm_table->mclk_table.dpm_levels
			[dpm_table->mclk_table.count - 1].value = mclk;
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_OD6PlusinACSupport) ||
			phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_OD6PlusinDCSupport)) {

			PP_ASSERT_WITH_CODE(
					(golden_dpm_table->mclk_table.dpm_levels
						[golden_dpm_table->mclk_table.count-1].value != 0),
					"Divide by 0!",
					return -1);
			dpm_count = dpm_table->mclk_table.count < 2 ?
					0 : dpm_table->mclk_table.count - 2;
			for (i = dpm_count; i > 1; i--) {
				if (mclk > golden_dpm_table->mclk_table.dpm_levels
						[golden_dpm_table->mclk_table.count-1].value) {
					clock_percent = ((mclk -
							golden_dpm_table->mclk_table.dpm_levels
							[golden_dpm_table->mclk_table.count-1].value) * 100) /
							golden_dpm_table->mclk_table.dpm_levels
							[golden_dpm_table->mclk_table.count-1].value;

					dpm_table->mclk_table.dpm_levels[i].value =
							golden_dpm_table->mclk_table.dpm_levels[i].value +
							(golden_dpm_table->mclk_table.dpm_levels[i].value *
									clock_percent) / 100;

				} else if (golden_dpm_table->mclk_table.dpm_levels
						[dpm_table->mclk_table.count-1].value > mclk) {
					clock_percent = ((golden_dpm_table->mclk_table.dpm_levels
							[golden_dpm_table->mclk_table.count-1].value - mclk) * 100) /
									golden_dpm_table->mclk_table.dpm_levels
									[golden_dpm_table->mclk_table.count-1].value;

					dpm_table->mclk_table.dpm_levels[i].value =
							golden_dpm_table->mclk_table.dpm_levels[i].value -
							(golden_dpm_table->mclk_table.dpm_levels[i].value *
									clock_percent) / 100;
				} else
					dpm_table->mclk_table.dpm_levels[i].value =
							golden_dpm_table->mclk_table.dpm_levels[i].value;
			}
		}
	}

	if (data->need_update_smu7_dpm_table &
			(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK)) {
		result = fiji_populate_all_graphic_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate SCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	if (data->need_update_smu7_dpm_table &
			(DPMTABLE_OD_UPDATE_MCLK + DPMTABLE_UPDATE_MCLK)) {
		/*populate MCLK dpm table to SMU7 */
		result = fiji_populate_all_memory_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate MCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	return result;
}

static int fiji_trim_single_dpm_states(struct pp_hwmgr *hwmgr,
			  struct fiji_single_dpm_table * dpm_table,
			     uint32_t low_limit, uint32_t high_limit)
{
	uint32_t i;

	for (i = 0; i < dpm_table->count; i++) {
		if ((dpm_table->dpm_levels[i].value < low_limit) ||
		    (dpm_table->dpm_levels[i].value > high_limit))
			dpm_table->dpm_levels[i].enabled = false;
		else
			dpm_table->dpm_levels[i].enabled = true;
	}
	return 0;
}

static int fiji_trim_dpm_states(struct pp_hwmgr *hwmgr,
		const struct fiji_power_state *fiji_ps)
{
	int result = 0;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t high_limit_count;

	PP_ASSERT_WITH_CODE((fiji_ps->performance_level_count >= 1),
			"power state did not have any performance level",
			return -1);

	high_limit_count = (1 == fiji_ps->performance_level_count) ? 0 : 1;

	fiji_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.sclk_table),
			fiji_ps->performance_levels[0].engine_clock,
			fiji_ps->performance_levels[high_limit_count].engine_clock);

	fiji_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.mclk_table),
			fiji_ps->performance_levels[0].memory_clock,
			fiji_ps->performance_levels[high_limit_count].memory_clock);

	return result;
}

static int fiji_generate_dpm_level_enable_mask(
		struct pp_hwmgr *hwmgr, const void *input)
{
	int result;
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	const struct fiji_power_state *fiji_ps =
			cast_const_phw_fiji_power_state(states->pnew_state);

	result = fiji_trim_dpm_states(hwmgr, fiji_ps);
	if (result)
		return result;

	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
			fiji_get_dpm_level_enable_mask_value(&data->dpm_table.sclk_table);
	data->dpm_level_enable_mask.mclk_dpm_enable_mask =
			fiji_get_dpm_level_enable_mask_value(&data->dpm_table.mclk_table);
	data->last_mclk_dpm_enable_mask =
			data->dpm_level_enable_mask.mclk_dpm_enable_mask;

	if (data->uvd_enabled) {
		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask & 1)
			data->dpm_level_enable_mask.mclk_dpm_enable_mask &= 0xFFFFFFFE;
	}

	data->dpm_level_enable_mask.pcie_dpm_enable_mask =
			fiji_get_dpm_level_enable_mask_value(&data->dpm_table.pcie_speed_table);

	return 0;
}

int fiji_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable ?
				  (PPSMC_Msg)PPSMC_MSG_UVDDPM_Enable :
				  (PPSMC_Msg)PPSMC_MSG_UVDDPM_Disable);
}

int fiji_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable?
			PPSMC_MSG_VCEDPM_Enable :
			PPSMC_MSG_VCEDPM_Disable);
}

int fiji_enable_disable_samu_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable?
			PPSMC_MSG_SAMUDPM_Enable :
			PPSMC_MSG_SAMUDPM_Disable);
}

int fiji_enable_disable_acp_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable?
			PPSMC_MSG_ACPDPM_Enable :
			PPSMC_MSG_ACPDPM_Disable);
}

int fiji_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (!bgate) {
		data->smc_state_table.UvdBootLevel = 0;
		if (table_info->mm_dep_table->count > 0)
			data->smc_state_table.UvdBootLevel =
					(uint8_t) (table_info->mm_dep_table->count - 1);
		mm_boot_level_offset = data->dpm_table_start +
				offsetof(SMU73_Discrete_DpmTable, UvdBootLevel);
		mm_boot_level_offset /= 4;
		mm_boot_level_offset *= 4;
		mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset);
		mm_boot_level_value &= 0x00FFFFFF;
		mm_boot_level_value |= data->smc_state_table.UvdBootLevel << 24;
		cgs_write_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

		if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_UVDDPM) ||
			phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_StablePState))
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_UVDDPM_SetEnabledMask,
					(uint32_t)(1 << data->smc_state_table.UvdBootLevel));
	}

	return fiji_enable_disable_uvd_dpm(hwmgr, !bgate);
}

int fiji_update_vce_dpm(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	const struct fiji_power_state *fiji_nps =
			cast_const_phw_fiji_power_state(states->pnew_state);
	const struct fiji_power_state *fiji_cps =
			cast_const_phw_fiji_power_state(states->pcurrent_state);

	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (fiji_nps->vce_clks.evclk >0 &&
	(fiji_cps == NULL || fiji_cps->vce_clks.evclk == 0)) {
		data->smc_state_table.VceBootLevel =
				(uint8_t) (table_info->mm_dep_table->count - 1);

		mm_boot_level_offset = data->dpm_table_start +
				offsetof(SMU73_Discrete_DpmTable, VceBootLevel);
		mm_boot_level_offset /= 4;
		mm_boot_level_offset *= 4;
		mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset);
		mm_boot_level_value &= 0xFF00FFFF;
		mm_boot_level_value |= data->smc_state_table.VceBootLevel << 16;
		cgs_write_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_StablePState)) {
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_VCEDPM_SetEnabledMask,
					(uint32_t)1 << data->smc_state_table.VceBootLevel);

			fiji_enable_disable_vce_dpm(hwmgr, true);
		} else if (fiji_nps->vce_clks.evclk == 0 &&
				fiji_cps != NULL &&
				fiji_cps->vce_clks.evclk > 0)
			fiji_enable_disable_vce_dpm(hwmgr, false);
	}

	return 0;
}

int fiji_update_samu_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (!bgate) {
		data->smc_state_table.SamuBootLevel =
				(uint8_t) (table_info->mm_dep_table->count - 1);
		mm_boot_level_offset = data->dpm_table_start +
				offsetof(SMU73_Discrete_DpmTable, SamuBootLevel);
		mm_boot_level_offset /= 4;
		mm_boot_level_offset *= 4;
		mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset);
		mm_boot_level_value &= 0xFFFFFF00;
		mm_boot_level_value |= data->smc_state_table.SamuBootLevel << 0;
		cgs_write_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_StablePState))
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SAMUDPM_SetEnabledMask,
					(uint32_t)(1 << data->smc_state_table.SamuBootLevel));
	}

	return fiji_enable_disable_samu_dpm(hwmgr, !bgate);
}

int fiji_update_acp_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (!bgate) {
		data->smc_state_table.AcpBootLevel =
				(uint8_t) (table_info->mm_dep_table->count - 1);
		mm_boot_level_offset = data->dpm_table_start +
				offsetof(SMU73_Discrete_DpmTable, AcpBootLevel);
		mm_boot_level_offset /= 4;
		mm_boot_level_offset *= 4;
		mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset);
		mm_boot_level_value &= 0xFFFF00FF;
		mm_boot_level_value |= data->smc_state_table.AcpBootLevel << 8;
		cgs_write_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_StablePState))
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_ACPDPM_SetEnabledMask,
						(uint32_t)(1 << data->smc_state_table.AcpBootLevel));
	}

	return fiji_enable_disable_acp_dpm(hwmgr, !bgate);
}

static int fiji_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	int result = 0;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification)
		&& (hwmgr->gfx_arbiter.sclk_threshold !=
				data->low_sclk_interrupt_threshold)) {
		data->low_sclk_interrupt_threshold =
				hwmgr->gfx_arbiter.sclk_threshold;
		low_sclk_interrupt_threshold =
				data->low_sclk_interrupt_threshold;

		CONVERT_FROM_HOST_TO_SMC_UL(low_sclk_interrupt_threshold);

		result = fiji_copy_bytes_to_smc(
				hwmgr->smumgr,
				data->dpm_table_start +
				offsetof(SMU73_Discrete_DpmTable,
					LowSclkInterruptThreshold),
				(uint8_t *)&low_sclk_interrupt_threshold,
				sizeof(uint32_t),
				data->sram_end);
	}

	return result;
}

static int fiji_program_mem_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_OD_UPDATE_MCLK))
		return fiji_program_memory_timing_parameters(hwmgr);

	return 0;
}

static int fiji_unfreeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {

		PP_ASSERT_WITH_CODE(true == fiji_is_dpm_running(hwmgr),
				"Trying to Unfreeze SCLK DPM when DPM is disabled",);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SCLKDPM_UnfreezeLevel),
			"Failed to unfreeze SCLK DPM during UnFreezeSclkMclkDPM Function!",
			return -1);
	}

	if ((0 == data->mclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)) {

		PP_ASSERT_WITH_CODE(true == fiji_is_dpm_running(hwmgr),
				"Trying to Unfreeze MCLK DPM when DPM is disabled",);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SCLKDPM_UnfreezeLevel),
		    "Failed to unfreeze MCLK DPM during UnFreezeSclkMclkDPM Function!",
		    return -1);
	}

	data->need_update_smu7_dpm_table = 0;

	return 0;
}

/* Look up the voltaged based on DAL's requested level.
 * and then send the requested VDDC voltage to SMC
 */
static void fiji_apply_dal_minimum_voltage_request(struct pp_hwmgr *hwmgr)
{
	return;
}

int fiji_upload_dpm_level_enable_mask(struct pp_hwmgr *hwmgr)
{
	int result;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	/* Apply minimum voltage based on DAL's request level */
	fiji_apply_dal_minimum_voltage_request(hwmgr);

	if (0 == data->sclk_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this,
		 *  we should skip this message.
		 */
		if (!fiji_is_dpm_running(hwmgr))
			printk(KERN_ERR "[ powerplay ] "
					"Trying to set Enable Mask when DPM is disabled \n");

		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SCLKDPM_SetEnabledMask,
					data->dpm_level_enable_mask.sclk_dpm_enable_mask);
			PP_ASSERT_WITH_CODE((0 == result),
				"Set Sclk Dpm enable Mask failed", return -1);
		}
	}

	if (0 == data->mclk_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this,
		 *  we should skip this message.
		 */
		if (!fiji_is_dpm_running(hwmgr))
			printk(KERN_ERR "[ powerplay ]"
					" Trying to set Enable Mask when DPM is disabled \n");

		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_MCLKDPM_SetEnabledMask,
					data->dpm_level_enable_mask.mclk_dpm_enable_mask);
			PP_ASSERT_WITH_CODE((0 == result),
				"Set Mclk Dpm enable Mask failed", return -1);
		}
	}

	return 0;
}

static int fiji_notify_link_speed_change_after_state_change(
		struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	const struct fiji_power_state *fiji_ps =
			cast_const_phw_fiji_power_state(states->pnew_state);
	uint16_t target_link_speed = fiji_get_maximum_link_speed(hwmgr, fiji_ps);
	uint8_t  request;

	if (data->pspp_notify_required) {
		if (target_link_speed == PP_PCIEGen3)
			request = PCIE_PERF_REQ_GEN3;
		else if (target_link_speed == PP_PCIEGen2)
			request = PCIE_PERF_REQ_GEN2;
		else
			request = PCIE_PERF_REQ_GEN1;

		if(request == PCIE_PERF_REQ_GEN1 &&
				fiji_get_current_pcie_speed(hwmgr) > 0)
			return 0;

		if (acpi_pcie_perf_request(hwmgr->device, request, false)) {
			if (PP_PCIEGen2 == target_link_speed)
				printk("PSPP request to switch to Gen2 from Gen3 Failed!");
			else
				printk("PSPP request to switch to Gen1 from Gen2 Failed!");
		}
	}

	return 0;
}

static int fiji_set_power_state_tasks(struct pp_hwmgr *hwmgr,
		const void *input)
{
	int tmp_result, result = 0;

	tmp_result = fiji_find_dpm_states_clocks_in_dpm_table(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to find DPM states clocks in DPM table!",
			result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result =
			fiji_request_link_speed_change_before_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
				"Failed to request link speed change before state change!",
				result = tmp_result);
	}

	tmp_result = fiji_freeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to freeze SCLK MCLK DPM!", result = tmp_result);

	tmp_result = fiji_populate_and_upload_sclk_mclk_dpm_levels(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to populate and upload SCLK MCLK DPM levels!",
			result = tmp_result);

	tmp_result = fiji_generate_dpm_level_enable_mask(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to generate DPM level enabled mask!",
			result = tmp_result);

	tmp_result = fiji_update_vce_dpm(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to update VCE DPM!",
			result = tmp_result);

	tmp_result = fiji_update_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to update SCLK threshold!",
			result = tmp_result);

	tmp_result = fiji_program_mem_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to program memory timing parameters!",
			result = tmp_result);

	tmp_result = fiji_unfreeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to unfreeze SCLK MCLK DPM!",
			result = tmp_result);

	tmp_result = fiji_upload_dpm_level_enable_mask(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to upload DPM level enabled mask!",
			result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result =
			fiji_notify_link_speed_change_after_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
				"Failed to notify link speed change after state change!",
				result = tmp_result);
	}

	return result;
}

static int fiji_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct fiji_power_state  *fiji_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	fiji_ps = cast_phw_fiji_power_state(&ps->hardware);

	if (low)
		return fiji_ps->performance_levels[0].engine_clock;
	else
		return fiji_ps->performance_levels
				[fiji_ps->performance_level_count-1].engine_clock;
}

static int fiji_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct fiji_power_state  *fiji_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	fiji_ps = cast_phw_fiji_power_state(&ps->hardware);

	if (low)
		return fiji_ps->performance_levels[0].memory_clock;
	else
		return fiji_ps->performance_levels
				[fiji_ps->performance_level_count-1].memory_clock;
}

static void fiji_print_current_perforce_level(
		struct pp_hwmgr *hwmgr, struct seq_file *m)
{
	uint32_t sclk, mclk, activity_percent = 0;
	uint32_t offset;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_API_GetSclkFrequency);

	sclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);

	smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_API_GetMclkFrequency);

	mclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);
	seq_printf(m, "\n [  mclk  ]: %u MHz\n\n [  sclk  ]: %u MHz\n",
			mclk / 100, sclk / 100);

	offset = data->soft_regs_start + offsetof(SMU73_SoftRegisters, AverageGraphicsActivity);
	activity_percent = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset);
	activity_percent += 0x80;
	activity_percent >>= 8;

	seq_printf(m, "\n [GPU load]: %u%%\n\n", activity_percent > 100 ? 100 : activity_percent);

	seq_printf(m, "uvd    %sabled\n", data->uvd_power_gated ? "dis" : "en");

	seq_printf(m, "vce    %sabled\n", data->vce_power_gated ? "dis" : "en");
}

static int fiji_program_display_gap(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t num_active_displays = 0;
	uint32_t display_gap = cgs_read_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, ixCG_DISPLAY_GAP_CNTL);
	uint32_t display_gap2;
	uint32_t pre_vbi_time_in_us;
	uint32_t frame_time_in_us;
	uint32_t ref_clock;
	uint32_t refresh_rate = 0;
	struct cgs_display_info info = {0};
	struct cgs_mode_info mode_info;

	info.mode_info = &mode_info;

	cgs_get_active_displays_info(hwmgr->device, &info);
	num_active_displays = info.display_count;

	display_gap = PHM_SET_FIELD(display_gap, CG_DISPLAY_GAP_CNTL,
			DISP_GAP, (num_active_displays > 0)?
			DISPLAY_GAP_VBLANK_OR_WM : DISPLAY_GAP_IGNORE);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_DISPLAY_GAP_CNTL, display_gap);

	ref_clock = mode_info.ref_clock;
	refresh_rate = mode_info.refresh_rate;

	if (refresh_rate == 0)
		refresh_rate = 60;

	frame_time_in_us = 1000000 / refresh_rate;

	pre_vbi_time_in_us = frame_time_in_us - 200 - mode_info.vblank_time_us;
	display_gap2 = pre_vbi_time_in_us * (ref_clock / 100);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_DISPLAY_GAP_CNTL2, display_gap2);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			data->soft_regs_start +
			offsetof(SMU73_SoftRegisters, PreVBlankGap), 0x64);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			data->soft_regs_start +
			offsetof(SMU73_SoftRegisters, VBlankTimeout),
			(frame_time_in_us - pre_vbi_time_in_us));

	if (num_active_displays == 1)
		tonga_notify_smc_display_change(hwmgr, true);

	return 0;
}

int fiji_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	return fiji_program_display_gap(hwmgr);
}

static int fiji_set_max_fan_pwm_output(struct pp_hwmgr *hwmgr,
		uint16_t us_max_fan_pwm)
{
	hwmgr->thermal_controller.
	advanceFanControlParameters.usMaxFanPWM = us_max_fan_pwm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_SetFanPwmMax, us_max_fan_pwm);
}

static int fiji_set_max_fan_rpm_output(struct pp_hwmgr *hwmgr,
		uint16_t us_max_fan_rpm)
{
	hwmgr->thermal_controller.
	advanceFanControlParameters.usMaxFanRPM = us_max_fan_rpm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_SetFanRpmMax, us_max_fan_rpm);
}

int fiji_dpm_set_interrupt_state(void *private_data,
					 unsigned src_id, unsigned type,
					 int enabled)
{
	uint32_t cg_thermal_int;
	struct pp_hwmgr *hwmgr = ((struct pp_eventmgr *)private_data)->hwmgr;

	if (hwmgr == NULL)
		return -EINVAL;

	switch (type) {
	case AMD_THERMAL_IRQ_LOW_TO_HIGH:
		if (enabled) {
			cg_thermal_int = cgs_read_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			cgs_write_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		} else {
			cg_thermal_int = cgs_read_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			cgs_write_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		}
		break;

	case AMD_THERMAL_IRQ_HIGH_TO_LOW:
		if (enabled) {
			cg_thermal_int = cgs_read_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			cgs_write_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		} else {
			cg_thermal_int = cgs_read_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			cgs_write_ind_register(hwmgr->device,
					CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		}
		break;
	default:
		break;
	}
	return 0;
}

int fiji_register_internal_thermal_interrupt(struct pp_hwmgr *hwmgr,
					const void *thermal_interrupt_info)
{
	int result;
	const struct pp_interrupt_registration_info *info =
			(const struct pp_interrupt_registration_info *)
			thermal_interrupt_info;

	if (info == NULL)
		return -EINVAL;

	result = cgs_add_irq_source(hwmgr->device, 230, AMD_THERMAL_IRQ_LAST,
				fiji_dpm_set_interrupt_state,
				info->call_back, info->context);

	if (result)
		return -EINVAL;

	result = cgs_add_irq_source(hwmgr->device, 231, AMD_THERMAL_IRQ_LAST,
				fiji_dpm_set_interrupt_state,
				info->call_back, info->context);

	if (result)
		return -EINVAL;

	return 0;
}

static int fiji_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	if (mode) {
		/* stop auto-manage */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl))
			fiji_fan_ctrl_stop_smc_fan_control(hwmgr);
		fiji_fan_ctrl_set_static_mode(hwmgr, mode);
	} else
		/* restart auto-manage */
		fiji_fan_ctrl_reset_fan_speed_to_default(hwmgr);

	return 0;
}

static int fiji_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->fan_ctrl_is_in_default_mode)
		return hwmgr->fan_ctrl_default_mode;
	else
		return PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_FDO_CTRL2, FDO_PWM_MODE);
}

static int fiji_get_pp_table(struct pp_hwmgr *hwmgr, char **table)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (!data->soft_pp_table) {
		data->soft_pp_table = kmemdup(hwmgr->soft_pp_table,
					      hwmgr->soft_pp_table_size,
					      GFP_KERNEL);
		if (!data->soft_pp_table)
			return -ENOMEM;
	}

	*table = (char *)&data->soft_pp_table;

	return hwmgr->soft_pp_table_size;
}

static int fiji_set_pp_table(struct pp_hwmgr *hwmgr, const char *buf, size_t size)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (!data->soft_pp_table) {
		data->soft_pp_table = kzalloc(hwmgr->soft_pp_table_size, GFP_KERNEL);
		if (!data->soft_pp_table)
			return -ENOMEM;
	}

	memcpy(data->soft_pp_table, buf, size);

	hwmgr->soft_pp_table = data->soft_pp_table;

	/* TODO: re-init powerplay to implement modified pptable */

	return 0;
}

static int fiji_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	switch (type) {
	case PP_SCLK:
		if (!data->sclk_dpm_key_disabled)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SCLKDPM_SetEnabledMask,
					data->dpm_level_enable_mask.sclk_dpm_enable_mask & mask);
		break;

	case PP_MCLK:
		if (!data->mclk_dpm_key_disabled)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_MCLKDPM_SetEnabledMask,
					data->dpm_level_enable_mask.mclk_dpm_enable_mask & mask);
		break;

	case PP_PCIE:
	{
		uint32_t tmp = mask & data->dpm_level_enable_mask.pcie_dpm_enable_mask;
		uint32_t level = 0;

		while (tmp >>= 1)
			level++;

		if (!data->pcie_dpm_key_disabled)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_PCIeDPM_ForceLevel,
					level);
		break;
	}
	default:
		break;
	}

	return 0;
}

static int fiji_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct fiji_single_dpm_table *sclk_table = &(data->dpm_table.sclk_table);
	struct fiji_single_dpm_table *mclk_table = &(data->dpm_table.mclk_table);
	struct fiji_single_dpm_table *pcie_table = &(data->dpm_table.pcie_speed_table);
	int i, now, size = 0;
	uint32_t clock, pcie_speed;

	switch (type) {
	case PP_SCLK:
		smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_API_GetSclkFrequency);
		clock = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);

		for (i = 0; i < sclk_table->count; i++) {
			if (clock > sclk_table->dpm_levels[i].value)
				continue;
			break;
		}
		now = i;

		for (i = 0; i < sclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, sclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_MCLK:
		smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_API_GetMclkFrequency);
		clock = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);

		for (i = 0; i < mclk_table->count; i++) {
			if (clock > mclk_table->dpm_levels[i].value)
				continue;
			break;
		}
		now = i;

		for (i = 0; i < mclk_table->count; i++)
			size += sprintf(buf + size, "%d: %uMhz %s\n",
					i, mclk_table->dpm_levels[i].value / 100,
					(i == now) ? "*" : "");
		break;
	case PP_PCIE:
		pcie_speed = fiji_get_current_pcie_speed(hwmgr);
		for (i = 0; i < pcie_table->count; i++) {
			if (pcie_speed != pcie_table->dpm_levels[i].value)
				continue;
			break;
		}
		now = i;

		for (i = 0; i < pcie_table->count; i++)
			size += sprintf(buf + size, "%d: %s %s\n", i,
					(pcie_table->dpm_levels[i].value == 0) ? "2.5GB, x1" :
					(pcie_table->dpm_levels[i].value == 1) ? "5.0GB, x16" :
					(pcie_table->dpm_levels[i].value == 2) ? "8.0GB, x16" : "",
					(i == now) ? "*" : "");
		break;
	default:
		break;
	}
	return size;
}

static inline bool fiji_are_power_levels_equal(const struct fiji_performance_level *pl1,
							   const struct fiji_performance_level *pl2)
{
	return ((pl1->memory_clock == pl2->memory_clock) &&
		  (pl1->engine_clock == pl2->engine_clock) &&
		  (pl1->pcie_gen == pl2->pcie_gen) &&
		  (pl1->pcie_lane == pl2->pcie_lane));
}

int fiji_check_states_equal(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *pstate1, const struct pp_hw_power_state *pstate2, bool *equal)
{
	const struct fiji_power_state *psa = cast_const_phw_fiji_power_state(pstate1);
	const struct fiji_power_state *psb = cast_const_phw_fiji_power_state(pstate2);
	int i;

	if (equal == NULL || psa == NULL || psb == NULL)
		return -EINVAL;

	/* If the two states don't even have the same number of performance levels they cannot be the same state. */
	if (psa->performance_level_count != psb->performance_level_count) {
		*equal = false;
		return 0;
	}

	for (i = 0; i < psa->performance_level_count; i++) {
		if (!fiji_are_power_levels_equal(&(psa->performance_levels[i]), &(psb->performance_levels[i]))) {
			/* If we have found even one performance level pair that is different the states are different. */
			*equal = false;
			return 0;
		}
	}

	/* If all performance levels are the same try to use the UVD clocks to break the tie.*/
	*equal = ((psa->uvd_clks.vclk == psb->uvd_clks.vclk) && (psa->uvd_clks.dclk == psb->uvd_clks.dclk));
	*equal &= ((psa->vce_clks.evclk == psb->vce_clks.evclk) && (psa->vce_clks.ecclk == psb->vce_clks.ecclk));
	*equal &= (psa->sclk_threshold == psb->sclk_threshold);
	*equal &= (psa->acp_clk == psb->acp_clk);

	return 0;
}

bool fiji_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	bool is_update_required = false;
	struct cgs_display_info info = {0,0,NULL};

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (data->display_timing.num_existing_displays != info.display_count)
		is_update_required = true;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		if(hwmgr->display_config.min_core_set_clock_in_sr != data->display_timing.min_clock_in_sr)
			is_update_required = true;
	}

	return is_update_required;
}


static const struct pp_hwmgr_func fiji_hwmgr_funcs = {
	.backend_init = &fiji_hwmgr_backend_init,
	.backend_fini = &fiji_hwmgr_backend_fini,
	.asic_setup = &fiji_setup_asic_task,
	.dynamic_state_management_enable = &fiji_enable_dpm_tasks,
	.force_dpm_level = &fiji_dpm_force_dpm_level,
	.get_num_of_pp_table_entries = &tonga_get_number_of_powerplay_table_entries,
	.get_power_state_size = &fiji_get_power_state_size,
	.get_pp_table_entry = &fiji_get_pp_table_entry,
	.patch_boot_state = &fiji_patch_boot_state,
	.apply_state_adjust_rules = &fiji_apply_state_adjust_rules,
	.power_state_set = &fiji_set_power_state_tasks,
	.get_sclk = &fiji_dpm_get_sclk,
	.get_mclk = &fiji_dpm_get_mclk,
	.print_current_perforce_level = &fiji_print_current_perforce_level,
	.powergate_uvd = &fiji_phm_powergate_uvd,
	.powergate_vce = &fiji_phm_powergate_vce,
	.disable_clock_power_gating = &fiji_phm_disable_clock_power_gating,
	.notify_smc_display_config_after_ps_adjustment =
			&tonga_notify_smc_display_config_after_ps_adjustment,
	.display_config_changed = &fiji_display_configuration_changed_task,
	.set_max_fan_pwm_output = fiji_set_max_fan_pwm_output,
	.set_max_fan_rpm_output = fiji_set_max_fan_rpm_output,
	.get_temperature = fiji_thermal_get_temperature,
	.stop_thermal_controller = fiji_thermal_stop_thermal_controller,
	.get_fan_speed_info = fiji_fan_ctrl_get_fan_speed_info,
	.get_fan_speed_percent = fiji_fan_ctrl_get_fan_speed_percent,
	.set_fan_speed_percent = fiji_fan_ctrl_set_fan_speed_percent,
	.reset_fan_speed_to_default = fiji_fan_ctrl_reset_fan_speed_to_default,
	.get_fan_speed_rpm = fiji_fan_ctrl_get_fan_speed_rpm,
	.set_fan_speed_rpm = fiji_fan_ctrl_set_fan_speed_rpm,
	.uninitialize_thermal_controller = fiji_thermal_ctrl_uninitialize_thermal_controller,
	.register_internal_thermal_interrupt = fiji_register_internal_thermal_interrupt,
	.set_fan_control_mode = fiji_set_fan_control_mode,
	.get_fan_control_mode = fiji_get_fan_control_mode,
	.check_states_equal = fiji_check_states_equal,
	.check_smc_update_required_for_display_configuration = fiji_check_smc_update_required_for_display_configuration,
	.get_pp_table = fiji_get_pp_table,
	.set_pp_table = fiji_set_pp_table,
	.force_clock_level = fiji_force_clock_level,
	.print_clock_levels = fiji_print_clock_levels,
};

int fiji_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr  *data;
	int ret = 0;

	data = kzalloc(sizeof(struct fiji_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;
	hwmgr->hwmgr_func = &fiji_hwmgr_funcs;
	hwmgr->pptable_func = &tonga_pptable_funcs;
	pp_fiji_thermal_initialize(hwmgr);
	return ret;
}
