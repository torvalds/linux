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
#include "pp_acpi.h"
#include "hwmgr.h"
#include <atombios.h>
#include "tonga_hwmgr.h"
#include "pptable.h"
#include "processpptables.h"
#include "tonga_processpptables.h"
#include "tonga_pptable.h"
#include "pp_debug.h"
#include "tonga_ppsmc.h"
#include "cgs_common.h"
#include "pppcielanes.h"
#include "tonga_dyn_defaults.h"
#include "smumgr.h"
#include "tonga_smumgr.h"
#include "tonga_clockpowergating.h"
#include "tonga_thermal.h"

#include "smu/smu_7_1_2_d.h"
#include "smu/smu_7_1_2_sh_mask.h"

#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "cgs_linux.h"
#include "eventmgr.h"
#include "amd_pcie_helpers.h"

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define MC_CG_SEQ_DRAMCONF_S0       0x05
#define MC_CG_SEQ_DRAMCONF_S1       0x06
#define MC_CG_SEQ_YCLK_SUSPEND      0x04
#define MC_CG_SEQ_YCLK_RESUME       0x0a

#define PCIE_BUS_CLK                10000
#define TCLK                        (PCIE_BUS_CLK / 10)

#define SMC_RAM_END 0x40000
#define SMC_CG_IND_START            0xc0030000
#define SMC_CG_IND_END              0xc0040000  /* First byte after SMC_CG_IND*/

#define VOLTAGE_SCALE               4
#define VOLTAGE_VID_OFFSET_SCALE1   625
#define VOLTAGE_VID_OFFSET_SCALE2   100

#define VDDC_VDDCI_DELTA            200
#define VDDC_VDDGFX_DELTA           300

#define MC_SEQ_MISC0_GDDR5_SHIFT 28
#define MC_SEQ_MISC0_GDDR5_MASK  0xf0000000
#define MC_SEQ_MISC0_GDDR5_VALUE 5

typedef uint32_t PECI_RegistryValue;

/* [2.5%,~2.5%] Clock stretched is multiple of 2.5% vs not and [Fmin, Fmax, LDO_REFSEL, USE_FOR_LOW_FREQ] */
uint16_t PP_ClockStretcherLookupTable[2][4] = {
	{600, 1050, 3, 0},
	{600, 1050, 6, 1} };

/* [FF, SS] type, [] 4 voltage ranges, and [Floor Freq, Boundary Freq, VID min , VID max] */
uint32_t PP_ClockStretcherDDTTable[2][4][4] = {
	{ {265, 529, 120, 128}, {325, 650, 96, 119}, {430, 860, 32, 95}, {0, 0, 0, 31} },
	{ {275, 550, 104, 112}, {319, 638, 96, 103}, {360, 720, 64, 95}, {384, 768, 32, 63} } };

/* [Use_For_Low_freq] value, [0%, 5%, 10%, 7.14%, 14.28%, 20%] (coming from PWR_CKS_CNTL.stretch_amount reg spec) */
uint8_t PP_ClockStretchAmountConversion[2][6] = {
	{0, 1, 3, 2, 4, 5},
	{0, 2, 4, 5, 6, 5} };

/* Values for the CG_THERMAL_CTRL::DPM_EVENT_SRC field. */
enum DPM_EVENT_SRC {
	DPM_EVENT_SRC_ANALOG = 0,               /* Internal analog trip point */
	DPM_EVENT_SRC_EXTERNAL = 1,             /* External (GPIO 17) signal */
	DPM_EVENT_SRC_DIGITAL = 2,              /* Internal digital trip point (DIG_THERM_DPM) */
	DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,   /* Internal analog or external */
	DPM_EVENT_SRC_DIGITAL_OR_EXTERNAL = 4   /* Internal digital or external */
};
typedef enum DPM_EVENT_SRC DPM_EVENT_SRC;

const unsigned long PhwTonga_Magic = (unsigned long)(PHM_VIslands_Magic);

struct tonga_power_state *cast_phw_tonga_power_state(
				  struct pp_hw_power_state *hw_ps)
{
	if (hw_ps == NULL)
		return NULL;

	PP_ASSERT_WITH_CODE((PhwTonga_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL);

	return (struct tonga_power_state *)hw_ps;
}

const struct tonga_power_state *cast_const_phw_tonga_power_state(
				 const struct pp_hw_power_state *hw_ps)
{
	if (hw_ps == NULL)
		return NULL;

	PP_ASSERT_WITH_CODE((PhwTonga_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL);

	return (const struct tonga_power_state *)hw_ps;
}

int tonga_add_voltage(struct pp_hwmgr *hwmgr,
	phm_ppt_v1_voltage_lookup_table *look_up_table,
	phm_ppt_v1_voltage_lookup_record *record)
{
	uint32_t i;
	PP_ASSERT_WITH_CODE((NULL != look_up_table),
		"Lookup Table empty.", return -1;);
	PP_ASSERT_WITH_CODE((0 != look_up_table->count),
		"Lookup Table empty.", return -1;);
	PP_ASSERT_WITH_CODE((SMU72_MAX_LEVELS_VDDGFX >= look_up_table->count),
		"Lookup Table is full.", return -1;);

	/* This is to avoid entering duplicate calculated records. */
	for (i = 0; i < look_up_table->count; i++) {
		if (look_up_table->entries[i].us_vdd == record->us_vdd) {
			if (look_up_table->entries[i].us_calculated == 1)
				return 0;
			else
				break;
		}
	}

	look_up_table->entries[i].us_calculated = 1;
	look_up_table->entries[i].us_vdd = record->us_vdd;
	look_up_table->entries[i].us_cac_low = record->us_cac_low;
	look_up_table->entries[i].us_cac_mid = record->us_cac_mid;
	look_up_table->entries[i].us_cac_high = record->us_cac_high;
	/* Only increment the count when we're appending, not replacing duplicate entry. */
	if (i == look_up_table->count)
		look_up_table->count++;

	return 0;
}

int tonga_notify_smc_display_change(struct pp_hwmgr *hwmgr, bool has_display)
{
	PPSMC_Msg msg = has_display? (PPSMC_Msg)PPSMC_HasDisplay : (PPSMC_Msg)PPSMC_NoDisplay;

	return (smum_send_msg_to_smc(hwmgr->smumgr, msg) == 0) ?  0 : -1;
}

uint8_t tonga_get_voltage_id(pp_atomctrl_voltage_table *voltage_table,
		uint32_t voltage)
{
	uint8_t count = (uint8_t) (voltage_table->count);
	uint8_t i = 0;

	PP_ASSERT_WITH_CODE((NULL != voltage_table),
		"Voltage Table empty.", return 0;);
	PP_ASSERT_WITH_CODE((0 != count),
		"Voltage Table empty.", return 0;);

	for (i = 0; i < count; i++) {
		/* find first voltage bigger than requested */
		if (voltage_table->entries[i].value >= voltage)
			return i;
	}

	/* voltage is bigger than max voltage in the table */
	return i - 1;
}

/**
 * @brief PhwTonga_GetVoltageOrder
 *  Returns index of requested voltage record in lookup(table)
 * @param hwmgr - pointer to hardware manager
 * @param lookupTable - lookup list to search in
 * @param voltage - voltage to look for
 * @return 0 on success
 */
uint8_t tonga_get_voltage_index(phm_ppt_v1_voltage_lookup_table *look_up_table,
		uint16_t voltage)
{
	uint8_t count = (uint8_t) (look_up_table->count);
	uint8_t i;

	PP_ASSERT_WITH_CODE((NULL != look_up_table), "Lookup Table empty.", return 0;);
	PP_ASSERT_WITH_CODE((0 != count), "Lookup Table empty.", return 0;);

	for (i = 0; i < count; i++) {
		/* find first voltage equal or bigger than requested */
		if (look_up_table->entries[i].us_vdd >= voltage)
			return i;
	}

	/* voltage is bigger than max voltage in the table */
	return i-1;
}

bool tonga_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	/*
	 * We return the status of Voltage Control instead of checking SCLK/MCLK DPM
	 * because we may have test scenarios that need us intentionly disable SCLK/MCLK DPM,
	 * whereas voltage control is a fundemental change that will not be disabled
	 */

	return (0 == PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					FEATURE_STATUS, VOLTAGE_CONTROLLER_ON) ? 1 : 0);
}

/**
 * Re-generate the DPM level mask value
 * @param    hwmgr      the address of the hardware manager
 */
static uint32_t tonga_get_dpm_level_enable_mask_value(
			struct tonga_single_dpm_table * dpm_table)
{
	uint32_t i;
	uint32_t mask_value = 0;

	for (i = dpm_table->count; i > 0; i--) {
		mask_value = mask_value << 1;

		if (dpm_table->dpm_levels[i-1].enabled)
			mask_value |= 0x1;
		else
			mask_value &= 0xFFFFFFFE;
	}
	return mask_value;
}

/**
 * Retrieve DPM default values from registry (if available)
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 */
void tonga_initialize_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	phw_tonga_ulv_parm *ulv = &(data->ulv);
	uint32_t tmp;

	ulv->ch_ulv_parameter = PPTONGA_CGULVPARAMETER_DFLT;
	data->voting_rights_clients0 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT0;
	data->voting_rights_clients1 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT1;
	data->voting_rights_clients2 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT2;
	data->voting_rights_clients3 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT3;
	data->voting_rights_clients4 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT4;
	data->voting_rights_clients5 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT5;
	data->voting_rights_clients6 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT6;
	data->voting_rights_clients7 = PPTONGA_VOTINGRIGHTSCLIENTS_DFLT7;

	data->static_screen_threshold_unit = PPTONGA_STATICSCREENTHRESHOLDUNIT_DFLT;
	data->static_screen_threshold = PPTONGA_STATICSCREENTHRESHOLD_DFLT;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_ABM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_NonABMSupportInPPLib);

	tmp = 0;
	if (tmp == 0)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicACTiming);

	tmp = 0;
	if (0 != tmp)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DisableMemoryTransition);

	data->mclk_strobe_mode_threshold = 40000;
	data->mclk_stutter_mode_threshold = 30000;
	data->mclk_edc_enable_threshold = 40000;
	data->mclk_edc_wr_enable_threshold = 40000;

	tmp = 0;
	if (tmp != 0)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DisableMCLS);

	data->pcie_gen_performance.max = PP_PCIEGen1;
	data->pcie_gen_performance.min = PP_PCIEGen3;
	data->pcie_gen_power_saving.max = PP_PCIEGen1;
	data->pcie_gen_power_saving.min = PP_PCIEGen3;

	data->pcie_lane_performance.max = 0;
	data->pcie_lane_performance.min = 16;
	data->pcie_lane_power_saving.max = 0;
	data->pcie_lane_power_saving.min = 16;

	tmp = 0;

	if (tmp)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_DynamicUVDState);

}

int tonga_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	int result = 0;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification)
		&& (hwmgr->gfx_arbiter.sclk_threshold != data->low_sclk_interrupt_threshold)) {
		data->low_sclk_interrupt_threshold = hwmgr->gfx_arbiter.sclk_threshold;
		low_sclk_interrupt_threshold = data->low_sclk_interrupt_threshold;

		CONVERT_FROM_HOST_TO_SMC_UL(low_sclk_interrupt_threshold);

		result = tonga_copy_bytes_to_smc(
				hwmgr->smumgr,
				data->dpm_table_start + offsetof(SMU72_Discrete_DpmTable,
				LowSclkInterruptThreshold),
				(uint8_t *)&low_sclk_interrupt_threshold,
				sizeof(uint32_t),
				data->sram_end
				);
	}

	return result;
}

/**
 * Find SCLK value that is associated with specified virtual_voltage_Id.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @param    virtual_voltage_Id  voltageId to look for.
 * @param    sclk output value .
 * @return   always 0 if success and 2 if association not found
 */
static int tonga_get_sclk_for_voltage_evv(struct pp_hwmgr *hwmgr,
	phm_ppt_v1_voltage_lookup_table *lookup_table,
	uint16_t virtual_voltage_id, uint32_t *sclk)
{
	uint8_t entryId;
	uint8_t voltageId;
	struct phm_ppt_v1_information *pptable_info =
					(struct phm_ppt_v1_information *)(hwmgr->pptable);

	PP_ASSERT_WITH_CODE(lookup_table->count != 0, "Lookup table is empty", return -1);

	/* search for leakage voltage ID 0xff01 ~ 0xff08 and sckl */
	for (entryId = 0; entryId < pptable_info->vdd_dep_on_sclk->count; entryId++) {
		voltageId = pptable_info->vdd_dep_on_sclk->entries[entryId].vddInd;
		if (lookup_table->entries[voltageId].us_vdd == virtual_voltage_id)
			break;
	}

	PP_ASSERT_WITH_CODE(entryId < pptable_info->vdd_dep_on_sclk->count,
			"Can't find requested voltage id in vdd_dep_on_sclk table!",
			return -1;
			);

	*sclk = pptable_info->vdd_dep_on_sclk->entries[entryId].clk;

	return 0;
}

/**
 * Get Leakage VDDC based on leakage ID.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   2 if vddgfx returned is greater than 2V or if BIOS
 */
int tonga_get_evv_voltage(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_clock_voltage_dependency_table *sclk_table = pptable_info->vdd_dep_on_sclk;
	uint16_t    virtual_voltage_id;
	uint16_t    vddc = 0;
	uint16_t    vddgfx = 0;
	uint16_t    i, j;
	uint32_t  sclk = 0;

	/* retrieve voltage for leakage ID (0xff01 + i) */
	for (i = 0; i < TONGA_MAX_LEAKAGE_COUNT; i++) {
		virtual_voltage_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;

		/* in split mode we should have only vddgfx EVV leakages */
		if (data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) {
			if (0 == tonga_get_sclk_for_voltage_evv(hwmgr,
						pptable_info->vddgfx_lookup_table, virtual_voltage_id, &sclk)) {
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
				PP_ASSERT_WITH_CODE(0 == atomctrl_get_voltage_evv_on_sclk
						(hwmgr, VOLTAGE_TYPE_VDDGFX, sclk,
						 virtual_voltage_id, &vddgfx),
						"Error retrieving EVV voltage value!", continue);

				/* need to make sure vddgfx is less than 2v or else, it could burn the ASIC. */
				PP_ASSERT_WITH_CODE((vddgfx < 2000 && vddgfx != 0), "Invalid VDDGFX value!", return -1);

				/* the voltage should not be zero nor equal to leakage ID */
				if (vddgfx != 0 && vddgfx != virtual_voltage_id) {
					data->vddcgfx_leakage.actual_voltage[data->vddcgfx_leakage.count] = vddgfx;
					data->vddcgfx_leakage.leakage_id[data->vddcgfx_leakage.count] = virtual_voltage_id;
					data->vddcgfx_leakage.count++;
				}
			}
		} else {
			/*  in merged mode we have only vddc EVV leakages */
			if (0 == tonga_get_sclk_for_voltage_evv(hwmgr,
						pptable_info->vddc_lookup_table,
						virtual_voltage_id, &sclk)) {
				PP_ASSERT_WITH_CODE(0 == atomctrl_get_voltage_evv_on_sclk
						(hwmgr, VOLTAGE_TYPE_VDDC, sclk,
						 virtual_voltage_id, &vddc),
						"Error retrieving EVV voltage value!", continue);

				/* need to make sure vddc is less than 2v or else, it could burn the ASIC. */
				if (vddc > 2000)
					printk(KERN_ERR "[ powerplay ] Invalid VDDC value! \n");

				/* the voltage should not be zero nor equal to leakage ID */
				if (vddc != 0 && vddc != virtual_voltage_id) {
					data->vddc_leakage.actual_voltage[data->vddc_leakage.count] = vddc;
					data->vddc_leakage.leakage_id[data->vddc_leakage.count] = virtual_voltage_id;
					data->vddc_leakage.count++;
				}
			}
		}
	}

	return 0;
}

int tonga_enable_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	/* enable SCLK dpm */
	if (0 == data->sclk_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						   PPSMC_MSG_DPM_Enable)),
				"Failed to enable SCLK DPM during DPM Start Function!",
				return -1);
	}

	/* enable MCLK dpm */
	if (0 == data->mclk_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
					     PPSMC_MSG_MCLKDPM_Enable)),
				"Failed to enable MCLK DPM during DPM Start Function!",
				return -1);

		PHM_WRITE_FIELD(hwmgr->device, MC_SEQ_CNTL_3, CAC_EN, 0x1);

		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixLCAC_MC0_CNTL, 0x05);/* CH0,1 read */
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixLCAC_MC1_CNTL, 0x05);/* CH2,3 read */
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixLCAC_CPL_CNTL, 0x100005);/*Read */

		udelay(10);

		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixLCAC_MC0_CNTL, 0x400005);/* CH0,1 write */
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixLCAC_MC1_CNTL, 0x400005);/* CH2,3 write */
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixLCAC_CPL_CNTL, 0x500005);/* write */

	}

	return 0;
}

int tonga_start_dpm(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	/* enable general power management */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, 1);
	/* enable sclk deep sleep */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL, DYNAMIC_PM_EN, 1);

	/* prepare for PCIE DPM */
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, data->soft_regs_start +
			offsetof(SMU72_SoftRegisters, VoltageChangeTimeout), 0x1000);

	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__PCIE, SWRST_COMMAND_1, RESETLC, 0x0);

	PP_ASSERT_WITH_CODE(
			(0 == smum_send_msg_to_smc(hwmgr->smumgr,
					PPSMC_MSG_Voltage_Cntl_Enable)),
			"Failed to enable voltage DPM during DPM Start Function!",
			return -1);

	if (0 != tonga_enable_sclk_mclk_dpm(hwmgr)) {
		PP_ASSERT_WITH_CODE(0, "Failed to enable Sclk DPM and Mclk DPM!", return -1);
	}

	/* enable PCIE dpm */
	if (0 == data->pcie_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_PCIeDPM_Enable)),
				"Failed to enable pcie DPM during DPM Start Function!",
				return -1
				);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_Falcon_QuickTransition)) {
				   smum_send_msg_to_smc(hwmgr->smumgr,
				    PPSMC_MSG_EnableACDCGPIOInterrupt);
	}

	return 0;
}

int tonga_disable_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	/* disable SCLK dpm */
	if (0 == data->sclk_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
		PP_ASSERT_WITH_CODE(
				(0 == tonga_is_dpm_running(hwmgr)),
				"Trying to Disable SCLK DPM when DPM is disabled",
				return -1
				);

		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						  PPSMC_MSG_DPM_Disable)),
				"Failed to disable SCLK DPM during DPM stop Function!",
				return -1);
	}

	/* disable MCLK dpm */
	if (0 == data->mclk_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this, we should skip this message. */
		PP_ASSERT_WITH_CODE(
				(0 == tonga_is_dpm_running(hwmgr)),
				"Trying to Disable MCLK DPM when DPM is disabled",
				return -1
				);

		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
					    PPSMC_MSG_MCLKDPM_Disable)),
				"Failed to Disable MCLK DPM during DPM stop Function!",
				return -1);
	}

	return 0;
}

int tonga_stop_dpm(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, 0);
	/* disable sclk deep sleep*/
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL, DYNAMIC_PM_EN, 0);

	/* disable PCIE dpm */
	if (0 == data->pcie_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
		PP_ASSERT_WITH_CODE(
				(0 == tonga_is_dpm_running(hwmgr)),
				"Trying to Disable PCIE DPM when DPM is disabled",
				return -1
				);
		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_PCIeDPM_Disable)),
				"Failed to disable pcie DPM during DPM stop Function!",
				return -1);
	}

	if (0 != tonga_disable_sclk_mclk_dpm(hwmgr))
		PP_ASSERT_WITH_CODE(0, "Failed to disable Sclk DPM and Mclk DPM!", return -1);

	/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
	PP_ASSERT_WITH_CODE(
			(0 == tonga_is_dpm_running(hwmgr)),
			"Trying to Disable Voltage CNTL when DPM is disabled",
			return -1
			);

	PP_ASSERT_WITH_CODE(
			(0 == smum_send_msg_to_smc(hwmgr->smumgr,
					PPSMC_MSG_Voltage_Cntl_Disable)),
			"Failed to disable voltage DPM during DPM stop Function!",
			return -1);

	return 0;
}

int tonga_enable_sclk_control(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL, SCLK_PWRMGT_OFF, 0);

	return 0;
}

/**
 * Send a message to the SMC and return a parameter
 *
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    msg: the message to send.
 * @param    parameter: pointer to the received parameter
 * @return   The response that came from the SMC.
 */
PPSMC_Result tonga_send_msg_to_smc_return_parameter(
		struct pp_hwmgr *hwmgr,
		PPSMC_Msg msg,
		uint32_t *parameter)
{
	int result;

	result = smum_send_msg_to_smc(hwmgr->smumgr, msg);

	if ((0 == result) && parameter) {
		*parameter = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);
	}

	return result;
}

/**
 * force DPM power State
 *
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    n     :  DPM level
 * @return   The response that came from the SMC.
 */
int tonga_dpm_force_state(struct pp_hwmgr *hwmgr, uint32_t n)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	uint32_t level_mask = 1 << n;

	/* Checking if DPM is running.  If we discover hang because of this, we should skip this message. */
	PP_ASSERT_WITH_CODE(0 == tonga_is_dpm_running(hwmgr),
			"Trying to force SCLK when DPM is disabled", return -1;);
	if (0 == data->sclk_dpm_key_disabled)
		return (0 == smum_send_msg_to_smc_with_parameter(
							     hwmgr->smumgr,
		     (PPSMC_Msg)(PPSMC_MSG_SCLKDPM_SetEnabledMask),
						    level_mask) ? 0 : 1);

	return 0;
}

/**
 * force DPM power State
 *
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    n     :  DPM level
 * @return   The response that came from the SMC.
 */
int tonga_dpm_force_state_mclk(struct pp_hwmgr *hwmgr, uint32_t n)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	uint32_t level_mask = 1 << n;

	/* Checking if DPM is running.  If we discover hang because of this, we should skip this message. */
	PP_ASSERT_WITH_CODE(0 == tonga_is_dpm_running(hwmgr),
			"Trying to Force MCLK when DPM is disabled", return -1;);
	if (0 == data->mclk_dpm_key_disabled)
		return (0 == smum_send_msg_to_smc_with_parameter(
								hwmgr->smumgr,
								(PPSMC_Msg)(PPSMC_MSG_MCLKDPM_SetEnabledMask),
								level_mask) ? 0 : 1);

	return 0;
}

/**
 * force DPM power State
 *
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    n     :  DPM level
 * @return   The response that came from the SMC.
 */
int tonga_dpm_force_state_pcie(struct pp_hwmgr *hwmgr, uint32_t n)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
	PP_ASSERT_WITH_CODE(0 == tonga_is_dpm_running(hwmgr),
			"Trying to Force PCIE level when DPM is disabled", return -1;);
	if (0 == data->pcie_dpm_key_disabled)
		return (0 == smum_send_msg_to_smc_with_parameter(
							     hwmgr->smumgr,
			   (PPSMC_Msg)(PPSMC_MSG_PCIeDPM_ForceLevel),
								n) ? 0 : 1);

	return 0;
}

/**
 * Set the initial state by calling SMC to switch to this state directly
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_set_boot_state(struct pp_hwmgr *hwmgr)
{
	/*
	* SMC only stores one state that SW will ask to switch too,
	* so we switch the the just uploaded one
	*/
	return (0 == tonga_disable_sclk_mclk_dpm(hwmgr)) ? 0 : 1;
}

/**
 * Get the location of various tables inside the FW image.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *tonga_smu = (struct tonga_smumgr *)(hwmgr->smumgr->backend);

	uint32_t tmp;
	int result;
	bool error = 0;

	result = tonga_read_smc_sram_dword(hwmgr->smumgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, DpmTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->dpm_table_start = tmp;
	}

	error |= (0 != result);

	result = tonga_read_smc_sram_dword(hwmgr->smumgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, SoftRegisters),
				&tmp, data->sram_end);

	if (0 == result) {
		data->soft_regs_start = tmp;
		tonga_smu->ulSoftRegsStart = tmp;
	}

	error |= (0 != result);


	result = tonga_read_smc_sram_dword(hwmgr->smumgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, mcRegisterTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->mc_reg_table_start = tmp;
	}

	result = tonga_read_smc_sram_dword(hwmgr->smumgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, FanTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->fan_table_start = tmp;
	}

	error |= (0 != result);

	result = tonga_read_smc_sram_dword(hwmgr->smumgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, mcArbDramTimingTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->arb_table_start = tmp;
	}

	error |= (0 != result);


	result = tonga_read_smc_sram_dword(hwmgr->smumgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, Version),
				&tmp, data->sram_end);

	if (0 == result) {
		hwmgr->microcode_version_info.SMC = tmp;
	}

	error |= (0 != result);

	return error ? 1 : 0;
}

/**
 * Read clock related registers.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_read_clock_registers(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	data->clock_registers.vCG_SPLL_FUNC_CNTL         =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_SPLL_FUNC_CNTL);
	data->clock_registers.vCG_SPLL_FUNC_CNTL_2       =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_SPLL_FUNC_CNTL_2);
	data->clock_registers.vCG_SPLL_FUNC_CNTL_3       =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_SPLL_FUNC_CNTL_3);
	data->clock_registers.vCG_SPLL_FUNC_CNTL_4       =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_SPLL_FUNC_CNTL_4);
	data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM   =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_SPLL_SPREAD_SPECTRUM);
	data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2 =
		cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_SPLL_SPREAD_SPECTRUM_2);
	data->clock_registers.vDLL_CNTL                  =
		cgs_read_register(hwmgr->device, mmDLL_CNTL);
	data->clock_registers.vMCLK_PWRMGT_CNTL          =
		cgs_read_register(hwmgr->device, mmMCLK_PWRMGT_CNTL);
	data->clock_registers.vMPLL_AD_FUNC_CNTL         =
		cgs_read_register(hwmgr->device, mmMPLL_AD_FUNC_CNTL);
	data->clock_registers.vMPLL_DQ_FUNC_CNTL         =
		cgs_read_register(hwmgr->device, mmMPLL_DQ_FUNC_CNTL);
	data->clock_registers.vMPLL_FUNC_CNTL            =
		cgs_read_register(hwmgr->device, mmMPLL_FUNC_CNTL);
	data->clock_registers.vMPLL_FUNC_CNTL_1          =
		cgs_read_register(hwmgr->device, mmMPLL_FUNC_CNTL_1);
	data->clock_registers.vMPLL_FUNC_CNTL_2          =
		cgs_read_register(hwmgr->device, mmMPLL_FUNC_CNTL_2);
	data->clock_registers.vMPLL_SS1                  =
		cgs_read_register(hwmgr->device, mmMPLL_SS1);
	data->clock_registers.vMPLL_SS2                  =
		cgs_read_register(hwmgr->device, mmMPLL_SS2);

	return 0;
}

/**
 * Find out if memory is GDDR5.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_get_memory_type(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	uint32_t temp;

	temp = cgs_read_register(hwmgr->device, mmMC_SEQ_MISC0);

	data->is_memory_GDDR5 = (MC_SEQ_MISC0_GDDR5_VALUE ==
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
int tonga_enable_acpi_power_management(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT, STATIC_PM_EN, 1);

	return 0;
}

/**
 * Initialize PowerGating States for different engines
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_init_power_gate_state(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = 0;
	data->vce_power_gated = 0;
	data->samu_power_gated = 0;
	data->acp_power_gated = 0;
	data->pg_acp_init = 1;

	return 0;
}

/**
 * Checks if DPM is enabled
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_check_for_dpm_running(struct pp_hwmgr *hwmgr)
{
	/*
	 * We return the status of Voltage Control instead of checking SCLK/MCLK DPM
	 * because we may have test scenarios that need us intentionly disable SCLK/MCLK DPM,
	 * whereas voltage control is a fundemental change that will not be disabled
	 */
	return (0 == tonga_is_dpm_running(hwmgr) ? 0 : 1);
}

/**
 * Checks if DPM is stopped
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_check_for_dpm_stopped(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	if (0 != tonga_is_dpm_running(hwmgr)) {
		/* If HW Virtualization is enabled, dpm_table_start will not have a valid value */
		if (!data->dpm_table_start) {
			return 1;
		}
	}

	return 0;
}

/**
 * Remove repeated voltage values and create table with unique values.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @param    voltage_table  the pointer to changing voltage table
 * @return    1 in success
 */

static int tonga_trim_voltage_table(struct pp_hwmgr *hwmgr,
			pp_atomctrl_voltage_table *voltage_table)
{
	uint32_t table_size, i, j;
	uint16_t vvalue;
	bool bVoltageFound = 0;
	pp_atomctrl_voltage_table *table;

	PP_ASSERT_WITH_CODE((NULL != voltage_table), "Voltage Table empty.", return -1;);
	table_size = sizeof(pp_atomctrl_voltage_table);
	table = kzalloc(table_size, GFP_KERNEL);

	if (NULL == table)
		return -ENOMEM;

	memset(table, 0x00, table_size);
	table->mask_low = voltage_table->mask_low;
	table->phase_delay = voltage_table->phase_delay;

	for (i = 0; i < voltage_table->count; i++) {
		vvalue = voltage_table->entries[i].value;
		bVoltageFound = 0;

		for (j = 0; j < table->count; j++) {
			if (vvalue == table->entries[j].value) {
				bVoltageFound = 1;
				break;
			}
		}

		if (!bVoltageFound) {
			table->entries[table->count].value = vvalue;
			table->entries[table->count].smio_low =
				voltage_table->entries[i].smio_low;
			table->count++;
		}
	}

	memcpy(table, voltage_table, sizeof(pp_atomctrl_voltage_table));

	kfree(table);

	return 0;
}

static int tonga_get_svi2_vdd_ci_voltage_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_clock_voltage_dependency_table *voltage_dependency_table)
{
	uint32_t i;
	int result;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	pp_atomctrl_voltage_table *vddci_voltage_table = &(data->vddci_voltage_table);

	PP_ASSERT_WITH_CODE((0 != voltage_dependency_table->count),
			"Voltage Dependency Table empty.", return -1;);

	vddci_voltage_table->mask_low = 0;
	vddci_voltage_table->phase_delay = 0;
	vddci_voltage_table->count = voltage_dependency_table->count;

	for (i = 0; i < voltage_dependency_table->count; i++) {
		vddci_voltage_table->entries[i].value =
			voltage_dependency_table->entries[i].vddci;
		vddci_voltage_table->entries[i].smio_low = 0;
	}

	result = tonga_trim_voltage_table(hwmgr, vddci_voltage_table);
	PP_ASSERT_WITH_CODE((0 == result),
			"Failed to trim VDDCI table.", return result;);

	return 0;
}



static int tonga_get_svi2_vdd_voltage_table(
		struct pp_hwmgr *hwmgr,
		phm_ppt_v1_voltage_lookup_table *look_up_table,
		pp_atomctrl_voltage_table *voltage_table)
{
	uint8_t i = 0;

	PP_ASSERT_WITH_CODE((0 != look_up_table->count),
			"Voltage Lookup Table empty.", return -1;);

	voltage_table->mask_low = 0;
	voltage_table->phase_delay = 0;

	voltage_table->count = look_up_table->count;

	for (i = 0; i < voltage_table->count; i++) {
		voltage_table->entries[i].value = look_up_table->entries[i].us_vdd;
		voltage_table->entries[i].smio_low = 0;
	}

	return 0;
}

/*
 * -------------------------------------------------------- Voltage Tables --------------------------------------------------------------------------
 * If the voltage table would be bigger than what will fit into the state table on the SMC keep only the higher entries.
 */

static void tonga_trim_voltage_table_to_fit_state_table(
		struct pp_hwmgr *hwmgr,
		uint32_t max_voltage_steps,
		pp_atomctrl_voltage_table *voltage_table)
{
	unsigned int i, diff;

	if (voltage_table->count <= max_voltage_steps) {
		return;
	}

	diff = voltage_table->count - max_voltage_steps;

	for (i = 0; i < max_voltage_steps; i++) {
		voltage_table->entries[i] = voltage_table->entries[i + diff];
	}

	voltage_table->count = max_voltage_steps;

	return;
}

/**
 * Create Voltage Tables.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_construct_voltage_tables(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	int result;

	/* MVDD has only GPIO voltage control */
	if (TONGA_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
					VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT, &(data->mvdd_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve MVDD table.", return result;);
	}

	if (TONGA_VOLTAGE_CONTROL_BY_GPIO == data->vdd_ci_control) {
		/* GPIO voltage */
		result = atomctrl_get_voltage_table_v3(hwmgr,
					VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT, &(data->vddci_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve VDDCI table.", return result;);
	} else if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_ci_control) {
		/* SVI2 voltage */
		result = tonga_get_svi2_vdd_ci_voltage_table(hwmgr,
					pptable_info->vdd_dep_on_mclk);
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve SVI2 VDDCI table from dependancy table.", return result;);
	}

	if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_gfx_control) {
		/* VDDGFX has only SVI2 voltage control */
		result = tonga_get_svi2_vdd_voltage_table(hwmgr,
					pptable_info->vddgfx_lookup_table, &(data->vddgfx_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve SVI2 VDDGFX table from lookup table.", return result;);
	}

	if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		/* VDDC has only SVI2 voltage control */
		result = tonga_get_svi2_vdd_voltage_table(hwmgr,
					pptable_info->vddc_lookup_table, &(data->vddc_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve SVI2 VDDC table from lookup table.", return result;);
	}

	PP_ASSERT_WITH_CODE(
			(data->vddc_voltage_table.count <= (SMU72_MAX_LEVELS_VDDC)),
			"Too many voltage values for VDDC. Trimming to fit state table.",
			tonga_trim_voltage_table_to_fit_state_table(hwmgr,
			SMU72_MAX_LEVELS_VDDC, &(data->vddc_voltage_table));
			);

	PP_ASSERT_WITH_CODE(
			(data->vddgfx_voltage_table.count <= (SMU72_MAX_LEVELS_VDDGFX)),
			"Too many voltage values for VDDGFX. Trimming to fit state table.",
			tonga_trim_voltage_table_to_fit_state_table(hwmgr,
			SMU72_MAX_LEVELS_VDDGFX, &(data->vddgfx_voltage_table));
			);

	PP_ASSERT_WITH_CODE(
			(data->vddci_voltage_table.count <= (SMU72_MAX_LEVELS_VDDCI)),
			"Too many voltage values for VDDCI. Trimming to fit state table.",
			tonga_trim_voltage_table_to_fit_state_table(hwmgr,
			SMU72_MAX_LEVELS_VDDCI, &(data->vddci_voltage_table));
			);

	PP_ASSERT_WITH_CODE(
			(data->mvdd_voltage_table.count <= (SMU72_MAX_LEVELS_MVDD)),
			"Too many voltage values for MVDD. Trimming to fit state table.",
			tonga_trim_voltage_table_to_fit_state_table(hwmgr,
			SMU72_MAX_LEVELS_MVDD, &(data->mvdd_voltage_table));
			);

	return 0;
}

/**
 * Vddc table preparation for SMC.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    table     the SMC DPM table structure to be populated
 * @return   always 0
 */
static int tonga_populate_smc_vddc_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	unsigned int count;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		table->VddcLevelCount = data->vddc_voltage_table.count;
		for (count = 0; count < table->VddcLevelCount; count++) {
			table->VddcTable[count] =
				PP_HOST_TO_SMC_US(data->vddc_voltage_table.entries[count].value * VOLTAGE_SCALE);
		}
		CONVERT_FROM_HOST_TO_SMC_UL(table->VddcLevelCount);
	}
	return 0;
}

/**
 * VddGfx table preparation for SMC.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    table     the SMC DPM table structure to be populated
 * @return   always 0
 */
static int tonga_populate_smc_vdd_gfx_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	unsigned int count;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_gfx_control) {
		table->VddGfxLevelCount = data->vddgfx_voltage_table.count;
		for (count = 0; count < data->vddgfx_voltage_table.count; count++) {
			table->VddGfxTable[count] =
				PP_HOST_TO_SMC_US(data->vddgfx_voltage_table.entries[count].value * VOLTAGE_SCALE);
		}
		CONVERT_FROM_HOST_TO_SMC_UL(table->VddGfxLevelCount);
	}
	return 0;
}

/**
 * Vddci table preparation for SMC.
 *
 * @param    *hwmgr The address of the hardware manager.
 * @param    *table The SMC DPM table structure to be populated.
 * @return   0
 */
static int tonga_populate_smc_vdd_ci_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	uint32_t count;

	table->VddciLevelCount = data->vddci_voltage_table.count;
	for (count = 0; count < table->VddciLevelCount; count++) {
		if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_ci_control) {
			table->VddciTable[count] =
				PP_HOST_TO_SMC_US(data->vddci_voltage_table.entries[count].value * VOLTAGE_SCALE);
		} else if (TONGA_VOLTAGE_CONTROL_BY_GPIO == data->vdd_ci_control) {
			table->SmioTable1.Pattern[count].Voltage =
				PP_HOST_TO_SMC_US(data->vddci_voltage_table.entries[count].value * VOLTAGE_SCALE);
			/* Index into DpmTable.Smio. Drive bits from Smio entry to get this voltage level. */
			table->SmioTable1.Pattern[count].Smio =
				(uint8_t) count;
			table->Smio[count] |=
				data->vddci_voltage_table.entries[count].smio_low;
			table->VddciTable[count] =
				PP_HOST_TO_SMC_US(data->vddci_voltage_table.entries[count].value * VOLTAGE_SCALE);
		}
	}

	table->SmioMask1 = data->vddci_voltage_table.mask_low;
	CONVERT_FROM_HOST_TO_SMC_UL(table->VddciLevelCount);

	return 0;
}

/**
 * Mvdd table preparation for SMC.
 *
 * @param    *hwmgr The address of the hardware manager.
 * @param    *table The SMC DPM table structure to be populated.
 * @return   0
 */
static int tonga_populate_smc_mvdd_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	uint32_t count;

	if (TONGA_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		table->MvddLevelCount = data->mvdd_voltage_table.count;
		for (count = 0; count < table->MvddLevelCount; count++) {
			table->SmioTable2.Pattern[count].Voltage =
				PP_HOST_TO_SMC_US(data->mvdd_voltage_table.entries[count].value * VOLTAGE_SCALE);
			/* Index into DpmTable.Smio. Drive bits from Smio entry to get this voltage level.*/
			table->SmioTable2.Pattern[count].Smio =
				(uint8_t) count;
			table->Smio[count] |=
				data->mvdd_voltage_table.entries[count].smio_low;
		}
		table->SmioMask2 = data->vddci_voltage_table.mask_low;

		CONVERT_FROM_HOST_TO_SMC_UL(table->MvddLevelCount);
	}

	return 0;
}

/**
 * Convert a voltage value in mv unit to VID number required by SMU firmware
 */
static uint8_t convert_to_vid(uint16_t vddc)
{
	return (uint8_t) ((6200 - (vddc * VOLTAGE_SCALE)) / 25);
}


/**
 * Preparation of vddc and vddgfx CAC tables for SMC.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    table     the SMC DPM table structure to be populated
 * @return   always 0
 */
static int tonga_populate_cac_tables(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	uint32_t count;
	uint8_t index;
	int result = 0;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_voltage_lookup_table *vddgfx_lookup_table = pptable_info->vddgfx_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddc_lookup_table = pptable_info->vddc_lookup_table;

	/* pTables is already swapped, so in order to use the value from it, we need to swap it back. */
	uint32_t vddcLevelCount = PP_SMC_TO_HOST_UL(table->VddcLevelCount);
	uint32_t vddgfxLevelCount = PP_SMC_TO_HOST_UL(table->VddGfxLevelCount);

	for (count = 0; count < vddcLevelCount; count++) {
		/* We are populating vddc CAC data to BapmVddc table in split and merged mode */
		index = tonga_get_voltage_index(vddc_lookup_table,
			data->vddc_voltage_table.entries[count].value);
		table->BapmVddcVidLoSidd[count] =
			convert_to_vid(vddc_lookup_table->entries[index].us_cac_low);
		table->BapmVddcVidHiSidd[count] =
			convert_to_vid(vddc_lookup_table->entries[index].us_cac_mid);
		table->BapmVddcVidHiSidd2[count] =
			convert_to_vid(vddc_lookup_table->entries[index].us_cac_high);
	}

	if ((data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2)) {
		/* We are populating vddgfx CAC data to BapmVddgfx table in split mode */
		for (count = 0; count < vddgfxLevelCount; count++) {
			index = tonga_get_voltage_index(vddgfx_lookup_table,
				data->vddgfx_voltage_table.entries[count].value);
			table->BapmVddGfxVidLoSidd[count] =
				convert_to_vid(vddgfx_lookup_table->entries[index].us_cac_low);
			table->BapmVddGfxVidHiSidd[count] =
				convert_to_vid(vddgfx_lookup_table->entries[index].us_cac_mid);
			table->BapmVddGfxVidHiSidd2[count] =
				convert_to_vid(vddgfx_lookup_table->entries[index].us_cac_high);
		}
	} else {
		for (count = 0; count < vddcLevelCount; count++) {
			index = tonga_get_voltage_index(vddc_lookup_table,
				data->vddc_voltage_table.entries[count].value);
			table->BapmVddGfxVidLoSidd[count] =
				convert_to_vid(vddc_lookup_table->entries[index].us_cac_low);
			table->BapmVddGfxVidHiSidd[count] =
				convert_to_vid(vddc_lookup_table->entries[index].us_cac_mid);
			table->BapmVddGfxVidHiSidd2[count] =
				convert_to_vid(vddc_lookup_table->entries[index].us_cac_high);
		}
	}

	return result;
}


/**
 * Preparation of voltage tables for SMC.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    table     the SMC DPM table structure to be populated
 * @return   always 0
 */

int tonga_populate_smc_voltage_tables(struct pp_hwmgr *hwmgr,
	SMU72_Discrete_DpmTable *table)
{
	int result;

	result = tonga_populate_smc_vddc_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate VDDC voltage table to SMC", return -1);

	result = tonga_populate_smc_vdd_ci_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate VDDCI voltage table to SMC", return -1);

	result = tonga_populate_smc_vdd_gfx_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate VDDGFX voltage table to SMC", return -1);

	result = tonga_populate_smc_mvdd_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate MVDD voltage table to SMC", return -1);

	result = tonga_populate_cac_tables(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate CAC voltage tables to SMC", return -1);

	return 0;
}

/**
 * Populates the SMC VRConfig field in DPM table.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    table     the SMC DPM table structure to be populated
 * @return   always 0
 */
static int tonga_populate_vr_config(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	uint16_t config;

	if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_gfx_control) {
		/*  Splitted mode */
		config = VR_SVI2_PLANE_1;
		table->VRConfig |= (config<<VRCONF_VDDGFX_SHIFT);

		if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
			config = VR_SVI2_PLANE_2;
			table->VRConfig |= config;
		} else {
			printk(KERN_ERR "[ powerplay ] VDDC and VDDGFX should be both on SVI2 control in splitted mode! \n");
		}
	} else {
		/* Merged mode  */
		config = VR_MERGED_WITH_VDDC;
		table->VRConfig |= (config<<VRCONF_VDDGFX_SHIFT);

		/* Set Vddc Voltage Controller  */
		if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
			config = VR_SVI2_PLANE_1;
			table->VRConfig |= config;
		} else {
			printk(KERN_ERR "[ powerplay ] VDDC should be on SVI2 control in merged mode! \n");
		}
	}

	/* Set Vddci Voltage Controller  */
	if (TONGA_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_ci_control) {
		config = VR_SVI2_PLANE_2;  /* only in merged mode */
		table->VRConfig |= (config<<VRCONF_VDDCI_SHIFT);
	} else if (TONGA_VOLTAGE_CONTROL_BY_GPIO == data->vdd_ci_control) {
		config = VR_SMIO_PATTERN_1;
		table->VRConfig |= (config<<VRCONF_VDDCI_SHIFT);
	}

	/* Set Mvdd Voltage Controller */
	if (TONGA_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		config = VR_SMIO_PATTERN_2;
		table->VRConfig |= (config<<VRCONF_MVDD_SHIFT);
	}

	return 0;
}

static int tonga_get_dependecy_volt_by_clk(struct pp_hwmgr *hwmgr,
	phm_ppt_v1_clock_voltage_dependency_table *allowed_clock_voltage_table,
	uint32_t clock, SMU_VoltageLevel *voltage, uint32_t *mvdd)
{
	uint32_t i = 0;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	/* clock - voltage dependency table is empty table */
	if (allowed_clock_voltage_table->count == 0)
		return -1;

	for (i = 0; i < allowed_clock_voltage_table->count; i++) {
		/* find first sclk bigger than request */
		if (allowed_clock_voltage_table->entries[i].clk >= clock) {
			voltage->VddGfx = tonga_get_voltage_index(pptable_info->vddgfx_lookup_table,
								allowed_clock_voltage_table->entries[i].vddgfx);

			voltage->Vddc = tonga_get_voltage_index(pptable_info->vddc_lookup_table,
								allowed_clock_voltage_table->entries[i].vddc);

			if (allowed_clock_voltage_table->entries[i].vddci) {
				voltage->Vddci = tonga_get_voltage_id(&data->vddci_voltage_table,
									allowed_clock_voltage_table->entries[i].vddci);
			} else {
				voltage->Vddci = tonga_get_voltage_id(&data->vddci_voltage_table,
									allowed_clock_voltage_table->entries[i].vddc - data->vddc_vddci_delta);
			}

			if (allowed_clock_voltage_table->entries[i].mvdd) {
				*mvdd = (uint32_t) allowed_clock_voltage_table->entries[i].mvdd;
			}

			voltage->Phases = 1;
			return 0;
		}
	}

	/* sclk is bigger than max sclk in the dependence table */
	voltage->VddGfx = tonga_get_voltage_index(pptable_info->vddgfx_lookup_table,
		allowed_clock_voltage_table->entries[i-1].vddgfx);
	voltage->Vddc = tonga_get_voltage_index(pptable_info->vddc_lookup_table,
		allowed_clock_voltage_table->entries[i-1].vddc);

	if (allowed_clock_voltage_table->entries[i-1].vddci) {
		voltage->Vddci = tonga_get_voltage_id(&data->vddci_voltage_table,
			allowed_clock_voltage_table->entries[i-1].vddci);
	}
	if (allowed_clock_voltage_table->entries[i-1].mvdd) {
		*mvdd = (uint32_t) allowed_clock_voltage_table->entries[i-1].mvdd;
	}

	return 0;
}

/**
 * Call SMC to reset S0/S1 to S1 and Reset SMIO to initial value
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_reset_to_default(struct pp_hwmgr *hwmgr)
{
	return (smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_ResetToDefaults) == 0) ? 0 : 1;
}

int tonga_populate_memory_timing_parameters(
		struct pp_hwmgr *hwmgr,
		uint32_t engine_clock,
		uint32_t memory_clock,
		struct SMU72_Discrete_MCArbDramTimingTableEntry *arb_regs
		)
{
	uint32_t dramTiming;
	uint32_t dramTiming2;
	uint32_t burstTime;
	int result;

	result = atomctrl_set_engine_dram_timings_rv770(hwmgr,
				engine_clock, memory_clock);

	PP_ASSERT_WITH_CODE(result == 0,
		"Error calling VBIOS to set DRAM_TIMING.", return result);

	dramTiming  = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING);
	dramTiming2 = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2);
	burstTime = PHM_READ_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE0);

	arb_regs->McArbDramTiming  = PP_HOST_TO_SMC_UL(dramTiming);
	arb_regs->McArbDramTiming2 = PP_HOST_TO_SMC_UL(dramTiming2);
	arb_regs->McArbBurstTime = (uint8_t)burstTime;

	return 0;
}

/**
 * Setup parameters for the MC ARB.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 * This function is to be called from the SetPowerState table.
 */
int tonga_program_memory_timing_parameters(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	int result = 0;
	SMU72_Discrete_MCArbDramTimingTable  arb_regs;
	uint32_t i, j;

	memset(&arb_regs, 0x00, sizeof(SMU72_Discrete_MCArbDramTimingTable));

	for (i = 0; i < data->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < data->dpm_table.mclk_table.count; j++) {
			result = tonga_populate_memory_timing_parameters
				(hwmgr, data->dpm_table.sclk_table.dpm_levels[i].value,
				 data->dpm_table.mclk_table.dpm_levels[j].value,
				 &arb_regs.entries[i][j]);

			if (0 != result) {
				break;
			}
		}
	}

	if (0 == result) {
		result = tonga_copy_bytes_to_smc(
				hwmgr->smumgr,
				data->arb_table_start,
				(uint8_t *)&arb_regs,
				sizeof(SMU72_Discrete_MCArbDramTimingTable),
				data->sram_end
				);
	}

	return result;
}

static int tonga_populate_smc_link_level(struct pp_hwmgr *hwmgr, SMU72_Discrete_DpmTable *table)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct tonga_dpm_table *dpm_table = &data->dpm_table;
	uint32_t i;

	/* Index (dpm_table->pcie_speed_table.count) is reserved for PCIE boot level. */
	for (i = 0; i <= dpm_table->pcie_speed_table.count; i++) {
		table->LinkLevel[i].PcieGenSpeed  =
			(uint8_t)dpm_table->pcie_speed_table.dpm_levels[i].value;
		table->LinkLevel[i].PcieLaneCount =
			(uint8_t)encode_pcie_lane_width(dpm_table->pcie_speed_table.dpm_levels[i].param1);
		table->LinkLevel[i].EnabledForActivity =
			1;
		table->LinkLevel[i].SPC =
			(uint8_t)(data->pcie_spc_cap & 0xff);
		table->LinkLevel[i].DownThreshold =
			PP_HOST_TO_SMC_UL(5);
		table->LinkLevel[i].UpThreshold =
			PP_HOST_TO_SMC_UL(30);
	}

	data->smc_state_table.LinkLevelCount =
		(uint8_t)dpm_table->pcie_speed_table.count;
	data->dpm_level_enable_mask.pcie_dpm_enable_mask =
		tonga_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);

	return 0;
}

static int tonga_populate_smc_uvd_level(struct pp_hwmgr *hwmgr,
					SMU72_Discrete_DpmTable *table)
{
	int result = 0;

	uint8_t count;
	pp_atomctrl_clock_dividers_vi dividers;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table = pptable_info->mm_dep_table;

	table->UvdLevelCount = (uint8_t) (mm_table->count);
	table->UvdBootLevel = 0;

	for (count = 0; count < table->UvdLevelCount; count++) {
		table->UvdLevel[count].VclkFrequency = mm_table->entries[count].vclk;
		table->UvdLevel[count].DclkFrequency = mm_table->entries[count].dclk;
		table->UvdLevel[count].MinVoltage.Vddc =
			tonga_get_voltage_index(pptable_info->vddc_lookup_table,
						mm_table->entries[count].vddc);
		table->UvdLevel[count].MinVoltage.VddGfx =
			(data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) ?
			tonga_get_voltage_index(pptable_info->vddgfx_lookup_table,
						mm_table->entries[count].vddgfx) : 0;
		table->UvdLevel[count].MinVoltage.Vddci =
			tonga_get_voltage_id(&data->vddci_voltage_table,
					     mm_table->entries[count].vddc - data->vddc_vddci_delta);
		table->UvdLevel[count].MinVoltage.Phases = 1;

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
		//CONVERT_FROM_HOST_TO_SMC_UL((uint32_t)table->UvdLevel[count].MinVoltage);
	}

	return result;

}

static int tonga_populate_smc_vce_level(struct pp_hwmgr *hwmgr,
		SMU72_Discrete_DpmTable *table)
{
	int result = 0;

	uint8_t count;
	pp_atomctrl_clock_dividers_vi dividers;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table = pptable_info->mm_dep_table;

	table->VceLevelCount = (uint8_t) (mm_table->count);
	table->VceBootLevel = 0;

	for (count = 0; count < table->VceLevelCount; count++) {
		table->VceLevel[count].Frequency =
			mm_table->entries[count].eclk;
		table->VceLevel[count].MinVoltage.Vddc =
			tonga_get_voltage_index(pptable_info->vddc_lookup_table,
				mm_table->entries[count].vddc);
		table->VceLevel[count].MinVoltage.VddGfx =
			(data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) ?
			tonga_get_voltage_index(pptable_info->vddgfx_lookup_table,
				mm_table->entries[count].vddgfx) : 0;
		table->VceLevel[count].MinVoltage.Vddci =
			tonga_get_voltage_id(&data->vddci_voltage_table,
				mm_table->entries[count].vddc - data->vddc_vddci_delta);
		table->VceLevel[count].MinVoltage.Phases = 1;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
					table->VceLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find divide id for VCE engine clock", return result);

		table->VceLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->VceLevel[count].Frequency);
	}

	return result;
}

static int tonga_populate_smc_acp_level(struct pp_hwmgr *hwmgr,
		SMU72_Discrete_DpmTable *table)
{
	int result = 0;
	uint8_t count;
	pp_atomctrl_clock_dividers_vi dividers;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table = pptable_info->mm_dep_table;

	table->AcpLevelCount = (uint8_t) (mm_table->count);
	table->AcpBootLevel = 0;

	for (count = 0; count < table->AcpLevelCount; count++) {
		table->AcpLevel[count].Frequency =
			pptable_info->mm_dep_table->entries[count].aclk;
		table->AcpLevel[count].MinVoltage.Vddc =
			tonga_get_voltage_index(pptable_info->vddc_lookup_table,
			mm_table->entries[count].vddc);
		table->AcpLevel[count].MinVoltage.VddGfx =
			(data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) ?
			tonga_get_voltage_index(pptable_info->vddgfx_lookup_table,
				mm_table->entries[count].vddgfx) : 0;
		table->AcpLevel[count].MinVoltage.Vddci =
			tonga_get_voltage_id(&data->vddci_voltage_table,
				mm_table->entries[count].vddc - data->vddc_vddci_delta);
		table->AcpLevel[count].MinVoltage.Phases = 1;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
			table->AcpLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find divide id for engine clock", return result);

		table->AcpLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->AcpLevel[count].Frequency);
	}

	return result;
}

static int tonga_populate_smc_samu_level(struct pp_hwmgr *hwmgr,
	SMU72_Discrete_DpmTable *table)
{
	int result = 0;
	uint8_t count;
	pp_atomctrl_clock_dividers_vi dividers;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table = pptable_info->mm_dep_table;

	table->SamuBootLevel = 0;
	table->SamuLevelCount = (uint8_t) (mm_table->count);

	for (count = 0; count < table->SamuLevelCount; count++) {
		/* not sure whether we need evclk or not */
		table->SamuLevel[count].Frequency =
			pptable_info->mm_dep_table->entries[count].samclock;
		table->SamuLevel[count].MinVoltage.Vddc =
			tonga_get_voltage_index(pptable_info->vddc_lookup_table,
				mm_table->entries[count].vddc);
		table->SamuLevel[count].MinVoltage.VddGfx =
			(data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) ?
			tonga_get_voltage_index(pptable_info->vddgfx_lookup_table,
				mm_table->entries[count].vddgfx) : 0;
		table->SamuLevel[count].MinVoltage.Vddci =
			tonga_get_voltage_id(&data->vddci_voltage_table,
				mm_table->entries[count].vddc - data->vddc_vddci_delta);
		table->SamuLevel[count].MinVoltage.Phases = 1;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
					table->SamuLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find divide id for samu clock", return result);

		table->SamuLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->SamuLevel[count].Frequency);
	}

	return result;
}

/**
 * Populates the SMC MCLK structure using the provided memory clock
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    memory_clock the memory clock to use to populate the structure
 * @param    sclk        the SMC SCLK structure to be populated
 */
static int tonga_calculate_mclk_params(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU72_Discrete_MemoryLevel *mclk,
		bool strobe_mode,
		bool dllStateOn
		)
{
	const tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	uint32_t  dll_cntl = data->clock_registers.vDLL_CNTL;
	uint32_t  mclk_pwrmgt_cntl = data->clock_registers.vMCLK_PWRMGT_CNTL;
	uint32_t  mpll_ad_func_cntl = data->clock_registers.vMPLL_AD_FUNC_CNTL;
	uint32_t  mpll_dq_func_cntl = data->clock_registers.vMPLL_DQ_FUNC_CNTL;
	uint32_t  mpll_func_cntl = data->clock_registers.vMPLL_FUNC_CNTL;
	uint32_t  mpll_func_cntl_1 = data->clock_registers.vMPLL_FUNC_CNTL_1;
	uint32_t  mpll_func_cntl_2 = data->clock_registers.vMPLL_FUNC_CNTL_2;
	uint32_t  mpll_ss1 = data->clock_registers.vMPLL_SS1;
	uint32_t  mpll_ss2 = data->clock_registers.vMPLL_SS2;

	pp_atomctrl_memory_clock_param mpll_param;
	int result;

	result = atomctrl_get_memory_pll_dividers_si(hwmgr,
				memory_clock, &mpll_param, strobe_mode);
	PP_ASSERT_WITH_CODE(0 == result,
		"Error retrieving Memory Clock Parameters from VBIOS.", return result);

	/* MPLL_FUNC_CNTL setup*/
	mpll_func_cntl = PHM_SET_FIELD(mpll_func_cntl, MPLL_FUNC_CNTL, BWCTRL, mpll_param.bw_ctrl);

	/* MPLL_FUNC_CNTL_1 setup*/
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
							MPLL_FUNC_CNTL_1, CLKF, mpll_param.mpll_fb_divider.cl_kf);
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
							MPLL_FUNC_CNTL_1, CLKFRAC, mpll_param.mpll_fb_divider.clk_frac);
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
							MPLL_FUNC_CNTL_1, VCO_MODE, mpll_param.vco_mode);

	/* MPLL_AD_FUNC_CNTL setup*/
	mpll_ad_func_cntl = PHM_SET_FIELD(mpll_ad_func_cntl,
							MPLL_AD_FUNC_CNTL, YCLK_POST_DIV, mpll_param.mpll_post_divider);

	if (data->is_memory_GDDR5) {
		/* MPLL_DQ_FUNC_CNTL setup*/
		mpll_dq_func_cntl  = PHM_SET_FIELD(mpll_dq_func_cntl,
								MPLL_DQ_FUNC_CNTL, YCLK_SEL, mpll_param.yclk_sel);
		mpll_dq_func_cntl  = PHM_SET_FIELD(mpll_dq_func_cntl,
								MPLL_DQ_FUNC_CNTL, YCLK_POST_DIV, mpll_param.mpll_post_divider);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MemorySpreadSpectrumSupport)) {
		/*
		 ************************************
		 Fref = Reference Frequency
		 NF = Feedback divider ratio
		 NR = Reference divider ratio
		 Fnom = Nominal VCO output frequency = Fref * NF / NR
		 Fs = Spreading Rate
		 D = Percentage down-spread / 2
		 Fint = Reference input frequency to PFD = Fref / NR
		 NS = Spreading rate divider ratio = int(Fint / (2 * Fs))
		 CLKS = NS - 1 = ISS_STEP_NUM[11:0]
		 NV = D * Fs / Fnom * 4 * ((Fnom/Fref * NR) ^ 2)
		 CLKV = 65536 * NV = ISS_STEP_SIZE[25:0]
		 *************************************
		 */
		pp_atomctrl_internal_ss_info ss_info;
		uint32_t freq_nom;
		uint32_t tmp;
		uint32_t reference_clock = atomctrl_get_mpll_reference_clock(hwmgr);

		/* for GDDR5 for all modes and DDR3 */
		if (1 == mpll_param.qdr)
			freq_nom = memory_clock * 4 * (1 << mpll_param.mpll_post_divider);
		else
			freq_nom = memory_clock * 2 * (1 << mpll_param.mpll_post_divider);

		/* tmp = (freq_nom / reference_clock * reference_divider) ^ 2  Note: S.I. reference_divider = 1*/
		tmp = (freq_nom / reference_clock);
		tmp = tmp * tmp;

		if (0 == atomctrl_get_memory_clock_spread_spectrum(hwmgr, freq_nom, &ss_info)) {
			/* ss_info.speed_spectrum_percentage -- in unit of 0.01% */
			/* ss.Info.speed_spectrum_rate -- in unit of khz */
			/* CLKS = reference_clock / (2 * speed_spectrum_rate * reference_divider) * 10 */
			/*     = reference_clock * 5 / speed_spectrum_rate */
			uint32_t clks = reference_clock * 5 / ss_info.speed_spectrum_rate;

			/* CLKV = 65536 * speed_spectrum_percentage / 2 * spreadSpecrumRate / freq_nom * 4 / 100000 * ((freq_nom / reference_clock) ^ 2) */
			/*     = 131 * speed_spectrum_percentage * speed_spectrum_rate / 100 * ((freq_nom / reference_clock) ^ 2) / freq_nom */
			uint32_t clkv =
				(uint32_t)((((131 * ss_info.speed_spectrum_percentage *
							ss_info.speed_spectrum_rate) / 100) * tmp) / freq_nom);

			mpll_ss1 = PHM_SET_FIELD(mpll_ss1, MPLL_SS1, CLKV, clkv);
			mpll_ss2 = PHM_SET_FIELD(mpll_ss2, MPLL_SS2, CLKS, clks);
		}
	}

	/* MCLK_PWRMGT_CNTL setup */
	mclk_pwrmgt_cntl = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, DLL_SPEED, mpll_param.dll_speed);
	mclk_pwrmgt_cntl = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK0_PDNB, dllStateOn);
	mclk_pwrmgt_cntl = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK1_PDNB, dllStateOn);


	/* Save the result data to outpupt memory level structure */
	mclk->MclkFrequency   = memory_clock;
	mclk->MpllFuncCntl    = mpll_func_cntl;
	mclk->MpllFuncCntl_1  = mpll_func_cntl_1;
	mclk->MpllFuncCntl_2  = mpll_func_cntl_2;
	mclk->MpllAdFuncCntl  = mpll_ad_func_cntl;
	mclk->MpllDqFuncCntl  = mpll_dq_func_cntl;
	mclk->MclkPwrmgtCntl  = mclk_pwrmgt_cntl;
	mclk->DllCntl         = dll_cntl;
	mclk->MpllSs1         = mpll_ss1;
	mclk->MpllSs2         = mpll_ss2;

	return 0;
}

static uint8_t tonga_get_mclk_frequency_ratio(uint32_t memory_clock,
		bool strobe_mode)
{
	uint8_t mc_para_index;

	if (strobe_mode) {
		if (memory_clock < 12500) {
			mc_para_index = 0x00;
		} else if (memory_clock > 47500) {
			mc_para_index = 0x0f;
		} else {
			mc_para_index = (uint8_t)((memory_clock - 10000) / 2500);
		}
	} else {
		if (memory_clock < 65000) {
			mc_para_index = 0x00;
		} else if (memory_clock > 135000) {
			mc_para_index = 0x0f;
		} else {
			mc_para_index = (uint8_t)((memory_clock - 60000) / 5000);
		}
	}

	return mc_para_index;
}

static uint8_t tonga_get_ddr3_mclk_frequency_ratio(uint32_t memory_clock)
{
	uint8_t mc_para_index;

	if (memory_clock < 10000) {
		mc_para_index = 0;
	} else if (memory_clock >= 80000) {
		mc_para_index = 0x0f;
	} else {
		mc_para_index = (uint8_t)((memory_clock - 10000) / 5000 + 1);
	}

	return mc_para_index;
}

static int tonga_populate_single_memory_level(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU72_Discrete_MemoryLevel *memory_level
		)
{
	uint32_t minMvdd = 0;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	int result = 0;
	bool dllStateOn;
	struct cgs_display_info info = {0};


	if (NULL != pptable_info->vdd_dep_on_mclk) {
		result = tonga_get_dependecy_volt_by_clk(hwmgr,
			pptable_info->vdd_dep_on_mclk, memory_clock, &memory_level->MinVoltage, &minMvdd);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find MinVddc voltage value from memory VDDC voltage dependency table", return result);
	}

	if (data->mvdd_control == TONGA_VOLTAGE_CONTROL_NONE) {
		memory_level->MinMvdd = data->vbios_boot_state.mvdd_bootup_value;
	} else {
		memory_level->MinMvdd = minMvdd;
	}
	memory_level->EnabledForThrottle = 1;
	memory_level->EnabledForActivity = 0;
	memory_level->UpHyst = 0;
	memory_level->DownHyst = 100;
	memory_level->VoltageDownHyst = 0;

	/* Indicates maximum activity level for this performance level.*/
	memory_level->ActivityLevel = (uint16_t)data->mclk_activity_target;
	memory_level->StutterEnable = 0;
	memory_level->StrobeEnable = 0;
	memory_level->EdcReadEnable = 0;
	memory_level->EdcWriteEnable = 0;
	memory_level->RttEnable = 0;

	/* default set to low watermark. Highest level will be set to high later.*/
	memory_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	cgs_get_active_displays_info(hwmgr->device, &info);
	data->display_timing.num_existing_displays = info.display_count;

	if ((data->mclk_stutter_mode_threshold != 0) &&
			(memory_clock <= data->mclk_stutter_mode_threshold) &&
			(data->is_uvd_enabled == 0)
#if defined(LINUX)
			&& (PHM_READ_FIELD(hwmgr->device, DPG_PIPE_STUTTER_CONTROL, STUTTER_ENABLE) & 0x1)
			&& (data->display_timing.num_existing_displays <= 2)
			&& (data->display_timing.num_existing_displays != 0)
#endif
	)
		memory_level->StutterEnable = 1;

	/* decide strobe mode*/
	memory_level->StrobeEnable = (data->mclk_strobe_mode_threshold != 0) &&
		(memory_clock <= data->mclk_strobe_mode_threshold);

	/* decide EDC mode and memory clock ratio*/
	if (data->is_memory_GDDR5) {
		memory_level->StrobeRatio = tonga_get_mclk_frequency_ratio(memory_clock,
					memory_level->StrobeEnable);

		if ((data->mclk_edc_enable_threshold != 0) &&
				(memory_clock > data->mclk_edc_enable_threshold)) {
			memory_level->EdcReadEnable = 1;
		}

		if ((data->mclk_edc_wr_enable_threshold != 0) &&
				(memory_clock > data->mclk_edc_wr_enable_threshold)) {
			memory_level->EdcWriteEnable = 1;
		}

		if (memory_level->StrobeEnable) {
			if (tonga_get_mclk_frequency_ratio(memory_clock, 1) >=
					((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC7) >> 16) & 0xf)) {
				dllStateOn = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC5) >> 1) & 0x1) ? 1 : 0;
			} else {
				dllStateOn = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC6) >> 1) & 0x1) ? 1 : 0;
			}

		} else {
			dllStateOn = data->dll_defaule_on;
		}
	} else {
		memory_level->StrobeRatio =
			tonga_get_ddr3_mclk_frequency_ratio(memory_clock);
		dllStateOn = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC5) >> 1) & 0x1) ? 1 : 0;
	}

	result = tonga_calculate_mclk_params(hwmgr,
		memory_clock, memory_level, memory_level->StrobeEnable, dllStateOn);

	if (0 == result) {
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MinMvdd);
		/* MCLK frequency in units of 10KHz*/
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MclkFrequency);
		/* Indicates maximum activity level for this performance level.*/
		CONVERT_FROM_HOST_TO_SMC_US(memory_level->ActivityLevel);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllFuncCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllFuncCntl_1);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllFuncCntl_2);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllAdFuncCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllDqFuncCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MclkPwrmgtCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->DllCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllSs1);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllSs2);
	}

	return result;
}

/**
 * Populates the SMC MVDD structure using the provided memory clock.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    mclk        the MCLK value to be used in the decision if MVDD should be high or low.
 * @param    voltage     the SMC VOLTAGE structure to be populated
 */
int tonga_populate_mvdd_value(struct pp_hwmgr *hwmgr, uint32_t mclk, SMIO_Pattern *smio_pattern)
{
	const tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t i = 0;

	if (TONGA_VOLTAGE_CONTROL_NONE != data->mvdd_control) {
		/* find mvdd value which clock is more than request */
		for (i = 0; i < pptable_info->vdd_dep_on_mclk->count; i++) {
			if (mclk <= pptable_info->vdd_dep_on_mclk->entries[i].clk) {
				/* Always round to higher voltage. */
				smio_pattern->Voltage = data->mvdd_voltage_table.entries[i].value;
				break;
			}
		}

		PP_ASSERT_WITH_CODE(i < pptable_info->vdd_dep_on_mclk->count,
			"MVDD Voltage is outside the supported range.", return -1);

	} else {
		return -1;
	}

	return 0;
}


static int tonga_populate_smv_acpi_level(struct pp_hwmgr *hwmgr,
	SMU72_Discrete_DpmTable *table)
{
	int result = 0;
	const tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	pp_atomctrl_clock_dividers_vi dividers;
	SMIO_Pattern voltage_level;
	uint32_t spll_func_cntl    = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_2  = data->clock_registers.vCG_SPLL_FUNC_CNTL_2;
	uint32_t dll_cntl          = data->clock_registers.vDLL_CNTL;
	uint32_t mclk_pwrmgt_cntl  = data->clock_registers.vMCLK_PWRMGT_CNTL;

	/* The ACPI state should not do DPM on DC (or ever).*/
	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	table->ACPILevel.MinVoltage = data->smc_state_table.GraphicsLevel[0].MinVoltage;

	/* assign zero for now*/
	table->ACPILevel.SclkFrequency = atomctrl_get_reference_clock(hwmgr);

	/* get the engine clock dividers for this clock value*/
	result = atomctrl_get_engine_pll_dividers_vi(hwmgr,
		table->ACPILevel.SclkFrequency,  &dividers);

	PP_ASSERT_WITH_CODE(result == 0,
		"Error retrieving Engine Clock dividers from VBIOS.", return result);

	/* divider ID for required SCLK*/
	table->ACPILevel.SclkDid = (uint8_t)dividers.pll_post_divider;
	table->ACPILevel.DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;
	table->ACPILevel.DeepSleepDivId = 0;

	spll_func_cntl      = PHM_SET_FIELD(spll_func_cntl,
							CG_SPLL_FUNC_CNTL,   SPLL_PWRON,     0);
	spll_func_cntl      = PHM_SET_FIELD(spll_func_cntl,
							CG_SPLL_FUNC_CNTL,   SPLL_RESET,     1);
	spll_func_cntl_2    = PHM_SET_FIELD(spll_func_cntl_2,
							CG_SPLL_FUNC_CNTL_2, SCLK_MUX_SEL,   4);

	table->ACPILevel.CgSpllFuncCntl = spll_func_cntl;
	table->ACPILevel.CgSpllFuncCntl2 = spll_func_cntl_2;
	table->ACPILevel.CgSpllFuncCntl3 = data->clock_registers.vCG_SPLL_FUNC_CNTL_3;
	table->ACPILevel.CgSpllFuncCntl4 = data->clock_registers.vCG_SPLL_FUNC_CNTL_4;
	table->ACPILevel.SpllSpreadSpectrum = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM;
	table->ACPILevel.SpllSpreadSpectrum2 = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2;
	table->ACPILevel.CcPwrDynRm = 0;
	table->ACPILevel.CcPwrDynRm1 = 0;


	/* For various features to be enabled/disabled while this level is active.*/
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.Flags);
	/* SCLK frequency in units of 10KHz*/
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl3);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl4);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SpllSpreadSpectrum);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SpllSpreadSpectrum2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm1);

	/* table->MemoryACPILevel.MinVddcPhases = table->ACPILevel.MinVddcPhases;*/
	table->MemoryACPILevel.MinVoltage = data->smc_state_table.MemoryLevel[0].MinVoltage;

	/*  CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MinVoltage);*/

	if (0 == tonga_populate_mvdd_value(hwmgr, 0, &voltage_level))
		table->MemoryACPILevel.MinMvdd =
			PP_HOST_TO_SMC_UL(voltage_level.Voltage * VOLTAGE_SCALE);
	else
		table->MemoryACPILevel.MinMvdd = 0;

	/* Force reset on DLL*/
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK0_RESET, 0x1);
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK1_RESET, 0x1);

	/* Disable DLL in ACPIState*/
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK0_PDNB, 0);
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK1_PDNB, 0);

	/* Enable DLL bypass signal*/
	dll_cntl            = PHM_SET_FIELD(dll_cntl,
		DLL_CNTL, MRDCK0_BYPASS, 0);
	dll_cntl            = PHM_SET_FIELD(dll_cntl,
		DLL_CNTL, MRDCK1_BYPASS, 0);

	table->MemoryACPILevel.DllCntl            =
		PP_HOST_TO_SMC_UL(dll_cntl);
	table->MemoryACPILevel.MclkPwrmgtCntl     =
		PP_HOST_TO_SMC_UL(mclk_pwrmgt_cntl);
	table->MemoryACPILevel.MpllAdFuncCntl     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_AD_FUNC_CNTL);
	table->MemoryACPILevel.MpllDqFuncCntl     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_DQ_FUNC_CNTL);
	table->MemoryACPILevel.MpllFuncCntl       =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_FUNC_CNTL);
	table->MemoryACPILevel.MpllFuncCntl_1     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_FUNC_CNTL_1);
	table->MemoryACPILevel.MpllFuncCntl_2     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_FUNC_CNTL_2);
	table->MemoryACPILevel.MpllSs1            =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_SS1);
	table->MemoryACPILevel.MpllSs2            =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_SS2);

	table->MemoryACPILevel.EnabledForThrottle = 0;
	table->MemoryACPILevel.EnabledForActivity = 0;
	table->MemoryACPILevel.UpHyst = 0;
	table->MemoryACPILevel.DownHyst = 100;
	table->MemoryACPILevel.VoltageDownHyst = 0;
	/* Indicates maximum activity level for this performance level.*/
	table->MemoryACPILevel.ActivityLevel = PP_HOST_TO_SMC_US((uint16_t)data->mclk_activity_target);

	table->MemoryACPILevel.StutterEnable = 0;
	table->MemoryACPILevel.StrobeEnable = 0;
	table->MemoryACPILevel.EdcReadEnable = 0;
	table->MemoryACPILevel.EdcWriteEnable = 0;
	table->MemoryACPILevel.RttEnable = 0;

	return result;
}

static int tonga_find_boot_level(struct tonga_single_dpm_table *table, uint32_t value, uint32_t *boot_level)
{
	int result = 0;
	uint32_t i;

	for (i = 0; i < table->count; i++) {
		if (value == table->dpm_levels[i].value) {
			*boot_level = i;
			result = 0;
		}
	}
	return result;
}

static int tonga_populate_smc_boot_level(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	int result = 0;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	table->GraphicsBootLevel  = 0;        /* 0 == DPM[0] (low), etc. */
	table->MemoryBootLevel    = 0;        /* 0 == DPM[0] (low), etc. */

	/* find boot level from dpm table*/
	result = tonga_find_boot_level(&(data->dpm_table.sclk_table),
	data->vbios_boot_state.sclk_bootup_value,
	(uint32_t *)&(data->smc_state_table.GraphicsBootLevel));

	if (0 != result) {
		data->smc_state_table.GraphicsBootLevel = 0;
		printk(KERN_ERR "[ powerplay ] VBIOS did not find boot engine clock value \
			in dependency table. Using Graphics DPM level 0!");
		result = 0;
	}

	result = tonga_find_boot_level(&(data->dpm_table.mclk_table),
		data->vbios_boot_state.mclk_bootup_value,
		(uint32_t *)&(data->smc_state_table.MemoryBootLevel));

	if (0 != result) {
		data->smc_state_table.MemoryBootLevel = 0;
		printk(KERN_ERR "[ powerplay ] VBIOS did not find boot engine clock value \
			in dependency table. Using Memory DPM level 0!");
		result = 0;
	}

	table->BootVoltage.Vddc =
		tonga_get_voltage_id(&(data->vddc_voltage_table),
			data->vbios_boot_state.vddc_bootup_value);
	table->BootVoltage.VddGfx =
		tonga_get_voltage_id(&(data->vddgfx_voltage_table),
			data->vbios_boot_state.vddgfx_bootup_value);
	table->BootVoltage.Vddci =
		tonga_get_voltage_id(&(data->vddci_voltage_table),
			data->vbios_boot_state.vddci_bootup_value);
	table->BootMVdd = data->vbios_boot_state.mvdd_bootup_value;

	CONVERT_FROM_HOST_TO_SMC_US(table->BootMVdd);

	return result;
}


/**
 * Calculates the SCLK dividers using the provided engine clock
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    engine_clock the engine clock to use to populate the structure
 * @param    sclk        the SMC SCLK structure to be populated
 */
int tonga_calculate_sclk_params(struct pp_hwmgr *hwmgr,
		uint32_t engine_clock, SMU72_Discrete_GraphicsLevel *sclk)
{
	const tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	pp_atomctrl_clock_dividers_vi dividers;
	uint32_t spll_func_cntl            = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_3          = data->clock_registers.vCG_SPLL_FUNC_CNTL_3;
	uint32_t spll_func_cntl_4          = data->clock_registers.vCG_SPLL_FUNC_CNTL_4;
	uint32_t cg_spll_spread_spectrum   = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM;
	uint32_t cg_spll_spread_spectrum_2 = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2;
	uint32_t    reference_clock;
	uint32_t reference_divider;
	uint32_t fbdiv;
	int result;

	/* get the engine clock dividers for this clock value*/
	result = atomctrl_get_engine_pll_dividers_vi(hwmgr, engine_clock,  &dividers);

	PP_ASSERT_WITH_CODE(result == 0,
		"Error retrieving Engine Clock dividers from VBIOS.", return result);

	/* To get FBDIV we need to multiply this by 16384 and divide it by Fref.*/
	reference_clock = atomctrl_get_reference_clock(hwmgr);

	reference_divider = 1 + dividers.uc_pll_ref_div;

	/* low 14 bits is fraction and high 12 bits is divider*/
	fbdiv = dividers.ul_fb_div.ul_fb_divider & 0x3FFFFFF;

	/* SPLL_FUNC_CNTL setup*/
	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl,
		CG_SPLL_FUNC_CNTL, SPLL_REF_DIV, dividers.uc_pll_ref_div);
	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl,
		CG_SPLL_FUNC_CNTL, SPLL_PDIV_A,  dividers.uc_pll_post_div);

	/* SPLL_FUNC_CNTL_3 setup*/
	spll_func_cntl_3 = PHM_SET_FIELD(spll_func_cntl_3,
		CG_SPLL_FUNC_CNTL_3, SPLL_FB_DIV, fbdiv);

	/* set to use fractional accumulation*/
	spll_func_cntl_3 = PHM_SET_FIELD(spll_func_cntl_3,
		CG_SPLL_FUNC_CNTL_3, SPLL_DITHEN, 1);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EngineSpreadSpectrumSupport)) {
		pp_atomctrl_internal_ss_info ss_info;

		uint32_t vcoFreq = engine_clock * dividers.uc_pll_post_div;
		if (0 == atomctrl_get_engine_clock_spread_spectrum(hwmgr, vcoFreq, &ss_info)) {
			/*
			* ss_info.speed_spectrum_percentage -- in unit of 0.01%
			* ss_info.speed_spectrum_rate -- in unit of khz
			*/
			/* clks = reference_clock * 10 / (REFDIV + 1) / speed_spectrum_rate / 2 */
			uint32_t clkS = reference_clock * 5 / (reference_divider * ss_info.speed_spectrum_rate);

			/* clkv = 2 * D * fbdiv / NS */
			uint32_t clkV = 4 * ss_info.speed_spectrum_percentage * fbdiv / (clkS * 10000);

			cg_spll_spread_spectrum =
				PHM_SET_FIELD(cg_spll_spread_spectrum, CG_SPLL_SPREAD_SPECTRUM, CLKS, clkS);
			cg_spll_spread_spectrum =
				PHM_SET_FIELD(cg_spll_spread_spectrum, CG_SPLL_SPREAD_SPECTRUM, SSEN, 1);
			cg_spll_spread_spectrum_2 =
				PHM_SET_FIELD(cg_spll_spread_spectrum_2, CG_SPLL_SPREAD_SPECTRUM_2, CLKV, clkV);
		}
	}

	sclk->SclkFrequency        = engine_clock;
	sclk->CgSpllFuncCntl3      = spll_func_cntl_3;
	sclk->CgSpllFuncCntl4      = spll_func_cntl_4;
	sclk->SpllSpreadSpectrum   = cg_spll_spread_spectrum;
	sclk->SpllSpreadSpectrum2  = cg_spll_spread_spectrum_2;
	sclk->SclkDid              = (uint8_t)dividers.pll_post_divider;

	return 0;
}

/**
 * Populates single SMC SCLK structure using the provided engine clock
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    engine_clock the engine clock to use to populate the structure
 * @param    sclk        the SMC SCLK structure to be populated
 */
static int tonga_populate_single_graphic_level(struct pp_hwmgr *hwmgr, uint32_t engine_clock, uint16_t sclk_activity_level_threshold, SMU72_Discrete_GraphicsLevel *graphic_level)
{
	int result;
	uint32_t threshold;
	uint32_t mvdd;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	result = tonga_calculate_sclk_params(hwmgr, engine_clock, graphic_level);


	/* populate graphics levels*/
	result = tonga_get_dependecy_volt_by_clk(hwmgr,
		pptable_info->vdd_dep_on_sclk, engine_clock,
		&graphic_level->MinVoltage, &mvdd);
	PP_ASSERT_WITH_CODE((0 == result),
		"can not find VDDC voltage value for VDDC	\
		engine clock dependency table", return result);

	/* SCLK frequency in units of 10KHz*/
	graphic_level->SclkFrequency = engine_clock;

	/* Indicates maximum activity level for this performance level. 50% for now*/
	graphic_level->ActivityLevel = sclk_activity_level_threshold;

	graphic_level->CcPwrDynRm = 0;
	graphic_level->CcPwrDynRm1 = 0;
	/* this level can be used if activity is high enough.*/
	graphic_level->EnabledForActivity = 0;
	/* this level can be used for throttling.*/
	graphic_level->EnabledForThrottle = 1;
	graphic_level->UpHyst = 0;
	graphic_level->DownHyst = 0;
	graphic_level->VoltageDownHyst = 0;
	graphic_level->PowerThrottle = 0;

	threshold = engine_clock * data->fast_watemark_threshold / 100;
/*
	*get the DAL clock. do it in funture.
	PECI_GetMinClockSettings(hwmgr->peci, &minClocks);
	data->display_timing.min_clock_insr = minClocks.engineClockInSR;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep))
	{
		graphic_level->DeepSleepDivId = PhwTonga_GetSleepDividerIdFromClock(hwmgr, engine_clock, minClocks.engineClockInSR);
	}
*/

	/* Default to slow, highest DPM level will be set to PPSMC_DISPLAY_WATERMARK_LOW later.*/
	graphic_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	if (0 == result) {
		/* CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->MinVoltage);*/
		/* CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->MinVddcPhases);*/
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->SclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_US(graphic_level->ActivityLevel);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CgSpllFuncCntl3);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CgSpllFuncCntl4);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->SpllSpreadSpectrum);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->SpllSpreadSpectrum2);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CcPwrDynRm);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CcPwrDynRm1);
	}

	return result;
}

/**
 * Populates all SMC SCLK levels' structure based on the trimmed allowed dpm engine clock states
 *
 * @param    hwmgr      the address of the hardware manager
 */
static int tonga_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct tonga_dpm_table *dpm_table = &data->dpm_table;
	phm_ppt_v1_pcie_table *pcie_table = pptable_info->pcie_table;
	uint8_t pcie_entry_count = (uint8_t) data->dpm_table.pcie_speed_table.count;
	int result = 0;
	uint32_t level_array_adress = data->dpm_table_start +
		offsetof(SMU72_Discrete_DpmTable, GraphicsLevel);
	uint32_t level_array_size = sizeof(SMU72_Discrete_GraphicsLevel) *
		SMU72_MAX_LEVELS_GRAPHICS;   /* 64 -> long; 32 -> int*/
	SMU72_Discrete_GraphicsLevel *levels = data->smc_state_table.GraphicsLevel;
	uint32_t i, maxEntry;
	uint8_t highest_pcie_level_enabled = 0, lowest_pcie_level_enabled = 0, mid_pcie_level_enabled = 0, count = 0;
	PECI_RegistryValue reg_value;
	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		result = tonga_populate_single_graphic_level(hwmgr,
					dpm_table->sclk_table.dpm_levels[i].value,
					(uint16_t)data->activity_target[i],
					&(data->smc_state_table.GraphicsLevel[i]));

		if (0 != result)
			return result;

		/* Making sure only DPM level 0-1 have Deep Sleep Div ID populated. */
		if (i > 1)
			data->smc_state_table.GraphicsLevel[i].DeepSleepDivId = 0;

		if (0 == i) {
			reg_value = 0;
			if (reg_value != 0)
				data->smc_state_table.GraphicsLevel[0].UpHyst = (uint8_t)reg_value;
		}

		if (1 == i) {
			reg_value = 0;
			if (reg_value != 0)
				data->smc_state_table.GraphicsLevel[1].UpHyst = (uint8_t)reg_value;
		}
	}

	/* Only enable level 0 for now. */
	data->smc_state_table.GraphicsLevel[0].EnabledForActivity = 1;

	/* set highest level watermark to high */
	if (dpm_table->sclk_table.count > 1)
		data->smc_state_table.GraphicsLevel[dpm_table->sclk_table.count-1].DisplayWatermark =
			PPSMC_DISPLAY_WATERMARK_HIGH;

	data->smc_state_table.GraphicsDpmLevelCount =
		(uint8_t)dpm_table->sclk_table.count;
	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
		tonga_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);

	if (pcie_table != NULL) {
		PP_ASSERT_WITH_CODE((pcie_entry_count >= 1),
			"There must be 1 or more PCIE levels defined in PPTable.", return -1);
		maxEntry = pcie_entry_count - 1; /* for indexing, we need to decrement by 1.*/
		for (i = 0; i < dpm_table->sclk_table.count; i++) {
			data->smc_state_table.GraphicsLevel[i].pcieDpmLevel =
				(uint8_t) ((i < maxEntry) ? i : maxEntry);
		}
	} else {
		if (0 == data->dpm_level_enable_mask.pcie_dpm_enable_mask)
			printk(KERN_ERR "[ powerplay ] Pcie Dpm Enablemask is 0!");

		while (data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
					(1<<(highest_pcie_level_enabled+1))) != 0)) {
			highest_pcie_level_enabled++;
		}

		while (data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
					(1<<lowest_pcie_level_enabled)) == 0)) {
			lowest_pcie_level_enabled++;
		}

		while ((count < highest_pcie_level_enabled) &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
					(1<<(lowest_pcie_level_enabled+1+count))) == 0)) {
			count++;
		}
		mid_pcie_level_enabled = (lowest_pcie_level_enabled+1+count) < highest_pcie_level_enabled ?
			(lowest_pcie_level_enabled+1+count) : highest_pcie_level_enabled;


		/* set pcieDpmLevel to highest_pcie_level_enabled*/
		for (i = 2; i < dpm_table->sclk_table.count; i++) {
			data->smc_state_table.GraphicsLevel[i].pcieDpmLevel = highest_pcie_level_enabled;
		}

		/* set pcieDpmLevel to lowest_pcie_level_enabled*/
		data->smc_state_table.GraphicsLevel[0].pcieDpmLevel = lowest_pcie_level_enabled;

		/* set pcieDpmLevel to mid_pcie_level_enabled*/
		data->smc_state_table.GraphicsLevel[1].pcieDpmLevel = mid_pcie_level_enabled;
	}
	/* level count will send to smc once at init smc table and never change*/
	result = tonga_copy_bytes_to_smc(hwmgr->smumgr, level_array_adress, (uint8_t *)levels, (uint32_t)level_array_size, data->sram_end);

	if (0 != result)
		return result;

	return 0;
}

/**
 * Populates all SMC MCLK levels' structure based on the trimmed allowed dpm memory clock states
 *
 * @param    hwmgr      the address of the hardware manager
 */

static int tonga_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct tonga_dpm_table *dpm_table = &data->dpm_table;
	int result;
	/* populate MCLK dpm table to SMU7 */
	uint32_t level_array_adress = data->dpm_table_start + offsetof(SMU72_Discrete_DpmTable, MemoryLevel);
	uint32_t level_array_size = sizeof(SMU72_Discrete_MemoryLevel) * SMU72_MAX_LEVELS_MEMORY;
	SMU72_Discrete_MemoryLevel *levels = data->smc_state_table.MemoryLevel;
	uint32_t i;

	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		PP_ASSERT_WITH_CODE((0 != dpm_table->mclk_table.dpm_levels[i].value),
			"can not populate memory level as memory clock is zero", return -1);
		result = tonga_populate_single_memory_level(hwmgr, dpm_table->mclk_table.dpm_levels[i].value,
			&(data->smc_state_table.MemoryLevel[i]));
		if (0 != result) {
			return result;
		}
	}

	/* Only enable level 0 for now.*/
	data->smc_state_table.MemoryLevel[0].EnabledForActivity = 1;

	/*
	* in order to prevent MC activity from stutter mode to push DPM up.
	* the UVD change complements this by putting the MCLK in a higher state
	* by default such that we are not effected by up threshold or and MCLK DPM latency.
	*/
	data->smc_state_table.MemoryLevel[0].ActivityLevel = 0x1F;
	CONVERT_FROM_HOST_TO_SMC_US(data->smc_state_table.MemoryLevel[0].ActivityLevel);

	data->smc_state_table.MemoryDpmLevelCount = (uint8_t)dpm_table->mclk_table.count;
	data->dpm_level_enable_mask.mclk_dpm_enable_mask = tonga_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);
	/* set highest level watermark to high*/
	data->smc_state_table.MemoryLevel[dpm_table->mclk_table.count-1].DisplayWatermark = PPSMC_DISPLAY_WATERMARK_HIGH;

	/* level count will send to smc once at init smc table and never change*/
	result = tonga_copy_bytes_to_smc(hwmgr->smumgr,
		level_array_adress, (uint8_t *)levels, (uint32_t)level_array_size, data->sram_end);

	if (0 != result) {
		return result;
	}

	return 0;
}

struct TONGA_DLL_SPEED_SETTING {
	uint16_t            Min;                          /* Minimum Data Rate*/
	uint16_t            Max;                          /* Maximum Data Rate*/
	uint32_t 			dll_speed;                     /* The desired DLL_SPEED setting*/
};

static int tonga_populate_clock_stretcher_data_table(struct pp_hwmgr *hwmgr)
{
	return 0;
}

/* ---------------------------------------- ULV related functions ----------------------------------------------------*/


static int tonga_reset_single_dpm_table(
	struct pp_hwmgr *hwmgr,
	struct tonga_single_dpm_table *dpm_table,
	uint32_t count)
{
	uint32_t i;
	if (!(count <= MAX_REGULAR_DPM_NUMBER))
		printk(KERN_ERR "[ powerplay ] Fatal error, can not set up single DPM \
			table entries to exceed max number! \n");

	dpm_table->count = count;
	for (i = 0; i < MAX_REGULAR_DPM_NUMBER; i++) {
		dpm_table->dpm_levels[i].enabled = 0;
	}

	return 0;
}

static void tonga_setup_pcie_table_entry(
	struct tonga_single_dpm_table *dpm_table,
	uint32_t index, uint32_t pcie_gen,
	uint32_t pcie_lanes)
{
	dpm_table->dpm_levels[index].value = pcie_gen;
	dpm_table->dpm_levels[index].param1 = pcie_lanes;
	dpm_table->dpm_levels[index].enabled = 1;
}

static int tonga_setup_default_pcie_tables(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_pcie_table *pcie_table = pptable_info->pcie_table;
	uint32_t i, maxEntry;

	if (data->use_pcie_performance_levels && !data->use_pcie_power_saving_levels) {
		data->pcie_gen_power_saving = data->pcie_gen_performance;
		data->pcie_lane_power_saving = data->pcie_lane_performance;
	} else if (!data->use_pcie_performance_levels && data->use_pcie_power_saving_levels) {
		data->pcie_gen_performance = data->pcie_gen_power_saving;
		data->pcie_lane_performance = data->pcie_lane_power_saving;
	}

	tonga_reset_single_dpm_table(hwmgr, &data->dpm_table.pcie_speed_table, SMU72_MAX_LEVELS_LINK);

	if (pcie_table != NULL) {
		/*
		* maxEntry is used to make sure we reserve one PCIE level for boot level (fix for A+A PSPP issue).
		* If PCIE table from PPTable have ULV entry + 8 entries, then ignore the last entry.
		*/
		maxEntry = (SMU72_MAX_LEVELS_LINK < pcie_table->count) ?
						SMU72_MAX_LEVELS_LINK : pcie_table->count;
		for (i = 1; i < maxEntry; i++) {
			tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, i-1,
				get_pcie_gen_support(data->pcie_gen_cap, pcie_table->entries[i].gen_speed),
				get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
		}
		data->dpm_table.pcie_speed_table.count = maxEntry - 1;
	} else {
		/* Hardcode Pcie Table */
		tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 0,
			get_pcie_gen_support(data->pcie_gen_cap, PP_Min_PCIEGen),
			get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
		tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 1,
			get_pcie_gen_support(data->pcie_gen_cap, PP_Min_PCIEGen),
			get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
		tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 2,
			get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
			get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
		tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 3,
			get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
			get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
		tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 4,
			get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
			get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
		tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 5,
			get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
			get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
		data->dpm_table.pcie_speed_table.count = 6;
	}
	/* Populate last level for boot PCIE level, but do not increment count. */
	tonga_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table,
		data->dpm_table.pcie_speed_table.count,
		get_pcie_gen_support(data->pcie_gen_cap, PP_Min_PCIEGen),
		get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));

	return 0;

}

/*
 * This function is to initalize all DPM state tables for SMU7 based on the dependency table.
 * Dynamic state patching function will then trim these state tables to the allowed range based
 * on the power policy or external client requests, such as UVD request, etc.
 */
static int tonga_setup_default_dpm_tables(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t i;

	phm_ppt_v1_clock_voltage_dependency_table *allowed_vdd_sclk_table =
		pptable_info->vdd_dep_on_sclk;
	phm_ppt_v1_clock_voltage_dependency_table *allowed_vdd_mclk_table =
		pptable_info->vdd_dep_on_mclk;

	PP_ASSERT_WITH_CODE(allowed_vdd_sclk_table != NULL,
		"SCLK dependency table is missing. This table is mandatory", return -1);
	PP_ASSERT_WITH_CODE(allowed_vdd_sclk_table->count >= 1,
		"SCLK dependency table has to have is missing. This table is mandatory", return -1);

	PP_ASSERT_WITH_CODE(allowed_vdd_mclk_table != NULL,
		"MCLK dependency table is missing. This table is mandatory", return -1);
	PP_ASSERT_WITH_CODE(allowed_vdd_mclk_table->count >= 1,
		"VMCLK dependency table has to have is missing. This table is mandatory", return -1);

	/* clear the state table to reset everything to default */
	memset(&(data->dpm_table), 0x00, sizeof(data->dpm_table));
	tonga_reset_single_dpm_table(hwmgr, &data->dpm_table.sclk_table, SMU72_MAX_LEVELS_GRAPHICS);
	tonga_reset_single_dpm_table(hwmgr, &data->dpm_table.mclk_table, SMU72_MAX_LEVELS_MEMORY);
	/* tonga_reset_single_dpm_table(hwmgr, &tonga_hwmgr->dpm_table.VddcTable, SMU72_MAX_LEVELS_VDDC); */
	/* tonga_reset_single_dpm_table(hwmgr, &tonga_hwmgr->dpm_table.vdd_gfx_table, SMU72_MAX_LEVELS_VDDGFX);*/
	/* tonga_reset_single_dpm_table(hwmgr, &tonga_hwmgr->dpm_table.vdd_ci_table, SMU72_MAX_LEVELS_VDDCI);*/
	/* tonga_reset_single_dpm_table(hwmgr, &tonga_hwmgr->dpm_table.mvdd_table, SMU72_MAX_LEVELS_MVDD);*/

	PP_ASSERT_WITH_CODE(allowed_vdd_sclk_table != NULL,
		"SCLK dependency table is missing. This table is mandatory", return -1);
	/* Initialize Sclk DPM table based on allow Sclk values*/
	data->dpm_table.sclk_table.count = 0;

	for (i = 0; i < allowed_vdd_sclk_table->count; i++) {
		if (i == 0 || data->dpm_table.sclk_table.dpm_levels[data->dpm_table.sclk_table.count-1].value !=
				allowed_vdd_sclk_table->entries[i].clk) {
			data->dpm_table.sclk_table.dpm_levels[data->dpm_table.sclk_table.count].value =
				allowed_vdd_sclk_table->entries[i].clk;
			data->dpm_table.sclk_table.dpm_levels[data->dpm_table.sclk_table.count].enabled = 1; /*(i==0) ? 1 : 0; to do */
			data->dpm_table.sclk_table.count++;
		}
	}

	PP_ASSERT_WITH_CODE(allowed_vdd_mclk_table != NULL,
		"MCLK dependency table is missing. This table is mandatory", return -1);
	/* Initialize Mclk DPM table based on allow Mclk values */
	data->dpm_table.mclk_table.count = 0;
	for (i = 0; i < allowed_vdd_mclk_table->count; i++) {
		if (i == 0 || data->dpm_table.mclk_table.dpm_levels[data->dpm_table.mclk_table.count-1].value !=
			allowed_vdd_mclk_table->entries[i].clk) {
			data->dpm_table.mclk_table.dpm_levels[data->dpm_table.mclk_table.count].value =
				allowed_vdd_mclk_table->entries[i].clk;
			data->dpm_table.mclk_table.dpm_levels[data->dpm_table.mclk_table.count].enabled = 1; /*(i==0) ? 1 : 0; */
			data->dpm_table.mclk_table.count++;
		}
	}

	/* Initialize Vddc DPM table based on allow Vddc values.  And populate corresponding std values. */
	for (i = 0; i < allowed_vdd_sclk_table->count; i++) {
		data->dpm_table.vddc_table.dpm_levels[i].value = allowed_vdd_mclk_table->entries[i].vddc;
		/* tonga_hwmgr->dpm_table.VddcTable.dpm_levels[i].param1 = stdVoltageTable->entries[i].Leakage; */
		/* param1 is for corresponding std voltage */
		data->dpm_table.vddc_table.dpm_levels[i].enabled = 1;
	}
	data->dpm_table.vddc_table.count = allowed_vdd_sclk_table->count;

	if (NULL != allowed_vdd_mclk_table) {
		/* Initialize Vddci DPM table based on allow Mclk values */
		for (i = 0; i < allowed_vdd_mclk_table->count; i++) {
			data->dpm_table.vdd_ci_table.dpm_levels[i].value = allowed_vdd_mclk_table->entries[i].vddci;
			data->dpm_table.vdd_ci_table.dpm_levels[i].enabled = 1;
			data->dpm_table.mvdd_table.dpm_levels[i].value = allowed_vdd_mclk_table->entries[i].mvdd;
			data->dpm_table.mvdd_table.dpm_levels[i].enabled = 1;
		}
		data->dpm_table.vdd_ci_table.count = allowed_vdd_mclk_table->count;
		data->dpm_table.mvdd_table.count = allowed_vdd_mclk_table->count;
	}

	/* setup PCIE gen speed levels*/
	tonga_setup_default_pcie_tables(hwmgr);

	/* save a copy of the default DPM table*/
	memcpy(&(data->golden_dpm_table), &(data->dpm_table), sizeof(struct tonga_dpm_table));

	return 0;
}

int tonga_populate_smc_initial_state(struct pp_hwmgr *hwmgr,
		const struct tonga_power_state *bootState)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint8_t count, level;

	count = (uint8_t) (pptable_info->vdd_dep_on_sclk->count);
	for (level = 0; level < count; level++) {
		if (pptable_info->vdd_dep_on_sclk->entries[level].clk >=
			bootState->performance_levels[0].engine_clock) {
			data->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	count = (uint8_t) (pptable_info->vdd_dep_on_mclk->count);
	for (level = 0; level < count; level++) {
		if (pptable_info->vdd_dep_on_mclk->entries[level].clk >=
			bootState->performance_levels[0].memory_clock) {
			data->smc_state_table.MemoryBootLevel = level;
			break;
		}
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
int tonga_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	SMU72_Discrete_DpmTable  *table = &(data->smc_state_table);
	const phw_tonga_ulv_parm *ulv = &(data->ulv);
	uint8_t i;
	PECI_RegistryValue reg_value;
	pp_atomctrl_gpio_pin_assignment gpio_pin_assignment;

	result = tonga_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to setup default DPM tables!", return result;);
	memset(&(data->smc_state_table), 0x00, sizeof(data->smc_state_table));
	if (TONGA_VOLTAGE_CONTROL_NONE != data->voltage_control) {
		tonga_populate_smc_voltage_tables(hwmgr, table);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition)) {
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StepVddc)) {
		table->SystemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;
	}

	if (data->is_memory_GDDR5) {
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GDDR5;
	}

	i = PHM_READ_FIELD(hwmgr->device, CC_MC_MAX_CHANNEL, NOOFCHAN);

	if (i == 1 || i == 0) {
		table->SystemFlags |= PPSMC_SYSTEMFLAG_12CHANNEL;
	}

	if (ulv->ulv_supported && pptable_info->us_ulv_voltage_offset) {
		PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize ULV state!", return result;);

		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_ULV_PARAMETER, ulv->ch_ulv_parameter);
	}

	result = tonga_populate_smc_link_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Link Level!", return result;);

	result = tonga_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Graphics Level!", return result;);

	result = tonga_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Memory Level!", return result;);

	result = tonga_populate_smv_acpi_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize ACPI Level!", return result;);

	result = tonga_populate_smc_vce_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize VCE Level!", return result;);

	result = tonga_populate_smc_acp_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize ACP Level!", return result;);

	result = tonga_populate_smc_samu_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize SAMU Level!", return result;);

	/* Since only the initial state is completely set up at this point (the other states are just copies of the boot state) we only */
	/* need to populate the  ARB settings for the initial state. */
	result = tonga_program_memory_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to Write ARB settings for the initial state.", return result;);

	result = tonga_populate_smc_uvd_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize UVD Level!", return result;);

	result = tonga_populate_smc_boot_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Boot Level!", return result;);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ClockStretcher)) {
		result = tonga_populate_clock_stretcher_data_table(hwmgr);
		PP_ASSERT_WITH_CODE(0 == result,
			"Failed to populate Clock Stretcher Data Table!", return result;);
	}
	table->GraphicsVoltageChangeEnable  = 1;
	table->GraphicsThermThrottleEnable  = 1;
	table->GraphicsInterval = 1;
	table->VoltageInterval  = 1;
	table->ThermalInterval  = 1;
	table->TemperatureLimitHigh =
		pptable_info->cac_dtp_table->usTargetOperatingTemp *
		TONGA_Q88_FORMAT_CONVERSION_UNIT;
	table->TemperatureLimitLow =
		(pptable_info->cac_dtp_table->usTargetOperatingTemp - 1) *
		TONGA_Q88_FORMAT_CONVERSION_UNIT;
	table->MemoryVoltageChangeEnable  = 1;
	table->MemoryInterval  = 1;
	table->VoltageResponseTime  = 0;
	table->PhaseResponseTime  = 0;
	table->MemoryThermThrottleEnable  = 1;

	/*
	* Cail reads current link status and reports it as cap (we cannot change this due to some previous issues we had)
	* SMC drops the link status to lowest level after enabling DPM by PowerPlay. After pnp or toggling CF, driver gets reloaded again
	* but this time Cail reads current link status which was set to low by SMC and reports it as cap to powerplay
	* To avoid it, we set PCIeBootLinkLevel to highest dpm level
	*/
	PP_ASSERT_WITH_CODE((1 <= data->dpm_table.pcie_speed_table.count),
			"There must be 1 or more PCIE levels defined in PPTable.",
			return -1);

	table->PCIeBootLinkLevel = (uint8_t) (data->dpm_table.pcie_speed_table.count);

	table->PCIeGenInterval  = 1;

	result = tonga_populate_vr_config(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to populate VRConfig setting!", return result);

	table->ThermGpio  = 17;
	table->SclkStepSize = 0x4000;

	reg_value = 0;
	if ((0 == reg_value) &&
		(0 == atomctrl_get_pp_assign_pin(hwmgr,
			VDDC_VRHOT_GPIO_PINID, &gpio_pin_assignment))) {
		table->VRHotGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot);
	} else {
		table->VRHotGpio = TONGA_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot);
	}

	/* ACDC Switch GPIO */
	reg_value = 0;
	if ((0 == reg_value) &&
		(0 == atomctrl_get_pp_assign_pin(hwmgr,
			PP_AC_DC_SWITCH_GPIO_PINID, &gpio_pin_assignment))) {
		table->AcDcGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition);
	} else {
		table->AcDcGpio = TONGA_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition);
	}

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_Falcon_QuickTransition);

	reg_value = 0;
	if (1 == reg_value) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_Falcon_QuickTransition);
	}

	reg_value = 0;
	if ((0 == reg_value) &&
		(0 == atomctrl_get_pp_assign_pin(hwmgr,
			THERMAL_INT_OUTPUT_GPIO_PINID, &gpio_pin_assignment))) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalOutGPIO);

		table->ThermOutGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;

		table->ThermOutPolarity =
			(0 == (cgs_read_register(hwmgr->device, mmGPIOPAD_A) &
			(1 << gpio_pin_assignment.uc_gpio_pin_bit_shift))) ? 1:0;

		table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_ONLY;

		/* if required, combine VRHot/PCC with thermal out GPIO*/
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot) &&
			phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CombinePCCWithThermalSignal)){
			table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_VRHOT;
		}
	} else {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalOutGPIO);

		table->ThermOutGpio = 17;
		table->ThermOutPolarity = 1;
		table->ThermOutMode = SMU7_THERM_OUT_MODE_DISABLE;
	}

	for (i = 0; i < SMU72_MAX_ENTRIES_SMIO; i++) {
		table->Smio[i] = PP_HOST_TO_SMC_UL(table->Smio[i]);
	}
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
	result = tonga_copy_bytes_to_smc(hwmgr->smumgr, data->dpm_table_start +
										offsetof(SMU72_Discrete_DpmTable, SystemFlags),
										(uint8_t *)&(table->SystemFlags),
										sizeof(SMU72_Discrete_DpmTable)-3 * sizeof(SMU72_PIDController),
										data->sram_end);

	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to upload dpm data to SMC memory!", return result;);

	return result;
}

/* Look up the voltaged based on DAL's requested level. and then send the requested VDDC voltage to SMC*/
static void tonga_apply_dal_minimum_voltage_request(struct pp_hwmgr *hwmgr)
{
	return;
}

int tonga_upload_dpm_level_enable_mask(struct pp_hwmgr *hwmgr)
{
	PPSMC_Result result;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	/* Apply minimum voltage based on DAL's request level */
	tonga_apply_dal_minimum_voltage_request(hwmgr);

	if (0 == data->sclk_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
		if (0 != tonga_is_dpm_running(hwmgr))
			printk(KERN_ERR "[ powerplay ] Trying to set Enable Mask when DPM is disabled \n");

		if (0 != data->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			result = smum_send_msg_to_smc_with_parameter(
								hwmgr->smumgr,
				(PPSMC_Msg)PPSMC_MSG_SCLKDPM_SetEnabledMask,
				data->dpm_level_enable_mask.sclk_dpm_enable_mask);
			PP_ASSERT_WITH_CODE((0 == result),
				"Set Sclk Dpm enable Mask failed", return -1);
		}
	}

	if (0 == data->mclk_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
		if (0 != tonga_is_dpm_running(hwmgr))
			printk(KERN_ERR "[ powerplay ] Trying to set Enable Mask when DPM is disabled \n");

		if (0 != data->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			result = smum_send_msg_to_smc_with_parameter(
								hwmgr->smumgr,
				(PPSMC_Msg)PPSMC_MSG_MCLKDPM_SetEnabledMask,
				data->dpm_level_enable_mask.mclk_dpm_enable_mask);
			PP_ASSERT_WITH_CODE((0 == result),
				"Set Mclk Dpm enable Mask failed", return -1);
		}
	}

	return 0;
}


int tonga_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	uint32_t level, tmp;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	if (0 == data->pcie_dpm_key_disabled) {
		/* PCIE */
		if (data->dpm_level_enable_mask.pcie_dpm_enable_mask != 0) {
			level = 0;
			tmp = data->dpm_level_enable_mask.pcie_dpm_enable_mask;
			while (tmp >>= 1)
				level++ ;

			if (0 != level) {
				PP_ASSERT_WITH_CODE((0 == tonga_dpm_force_state_pcie(hwmgr, level)),
					"force highest pcie dpm state failed!", return -1);
			}
		}
	}

	if (0 == data->sclk_dpm_key_disabled) {
		/* SCLK */
		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask != 0) {
			level = 0;
			tmp = data->dpm_level_enable_mask.sclk_dpm_enable_mask;
			while (tmp >>= 1)
				level++ ;

			if (0 != level) {
				PP_ASSERT_WITH_CODE((0 == tonga_dpm_force_state(hwmgr, level)),
					"force highest sclk dpm state failed!", return -1);
				if (PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device,
					CGS_IND_REG__SMC, TARGET_AND_CURRENT_PROFILE_INDEX, CURR_SCLK_INDEX) != level)
					printk(KERN_ERR "[ powerplay ] Target_and_current_Profile_Index. \
						Curr_Sclk_Index does not match the level \n");

			}
		}
	}

	if (0 == data->mclk_dpm_key_disabled) {
		/* MCLK */
		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask != 0) {
			level = 0;
			tmp = data->dpm_level_enable_mask.mclk_dpm_enable_mask;
			while (tmp >>= 1)
				level++ ;

			if (0 != level) {
				PP_ASSERT_WITH_CODE((0 == tonga_dpm_force_state_mclk(hwmgr, level)),
					"force highest mclk dpm state failed!", return -1);
				if (PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					TARGET_AND_CURRENT_PROFILE_INDEX, CURR_MCLK_INDEX) != level)
					printk(KERN_ERR "[ powerplay ] Target_and_current_Profile_Index. \
						Curr_Mclk_Index does not match the level \n");
			}
		}
	}

	return 0;
}

/**
 * Find the MC microcode version and store it in the HwMgr struct
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_get_mc_microcode_version (struct pp_hwmgr *hwmgr)
{
	cgs_write_register(hwmgr->device, mmMC_SEQ_IO_DEBUG_INDEX, 0x9F);

	hwmgr->microcode_version_info.MC = cgs_read_register(hwmgr->device, mmMC_SEQ_IO_DEBUG_DATA);

	return 0;
}

/**
 * Initialize Dynamic State Adjustment Rule Settings
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 */
int tonga_initializa_dynamic_state_adjustment_rule_settings(struct pp_hwmgr *hwmgr)
{
	uint32_t table_size;
	struct phm_clock_voltage_dependency_table *table_clk_vlt;
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	hwmgr->dyn_state.mclk_sclk_ratio = 4;
	hwmgr->dyn_state.sclk_mclk_delta = 15000;      /* 150 MHz */
	hwmgr->dyn_state.vddc_vddci_delta = 200;       /* 200mV */

	/* initialize vddc_dep_on_dal_pwrl table */
	table_size = sizeof(uint32_t) + 4 * sizeof(struct phm_clock_voltage_dependency_record);
	table_clk_vlt = (struct phm_clock_voltage_dependency_table *)kzalloc(table_size, GFP_KERNEL);

	if (NULL == table_clk_vlt) {
		printk(KERN_ERR "[ powerplay ] Can not allocate space for vddc_dep_on_dal_pwrl! \n");
		return -ENOMEM;
	} else {
		table_clk_vlt->count = 4;
		table_clk_vlt->entries[0].clk = PP_DAL_POWERLEVEL_ULTRALOW;
		table_clk_vlt->entries[0].v = 0;
		table_clk_vlt->entries[1].clk = PP_DAL_POWERLEVEL_LOW;
		table_clk_vlt->entries[1].v = 720;
		table_clk_vlt->entries[2].clk = PP_DAL_POWERLEVEL_NOMINAL;
		table_clk_vlt->entries[2].v = 810;
		table_clk_vlt->entries[3].clk = PP_DAL_POWERLEVEL_PERFORMANCE;
		table_clk_vlt->entries[3].v = 900;
		pptable_info->vddc_dep_on_dal_pwrl = table_clk_vlt;
		hwmgr->dyn_state.vddc_dep_on_dal_pwrl = table_clk_vlt;
	}

	return 0;
}

static int tonga_set_private_var_based_on_pptale(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	phm_ppt_v1_clock_voltage_dependency_table *allowed_sclk_vdd_table =
		pptable_info->vdd_dep_on_sclk;
	phm_ppt_v1_clock_voltage_dependency_table *allowed_mclk_vdd_table =
		pptable_info->vdd_dep_on_mclk;

	PP_ASSERT_WITH_CODE(allowed_sclk_vdd_table != NULL,
		"VDD dependency on SCLK table is missing. 	\
		This table is mandatory", return -1);
	PP_ASSERT_WITH_CODE(allowed_sclk_vdd_table->count >= 1,
		"VDD dependency on SCLK table has to have is missing. 	\
		This table is mandatory", return -1);

	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table != NULL,
		"VDD dependency on MCLK table is missing. 	\
		This table is mandatory", return -1);
	PP_ASSERT_WITH_CODE(allowed_mclk_vdd_table->count >= 1,
		"VDD dependency on MCLK table has to have is missing.	 \
		This table is mandatory", return -1);

	data->min_vddc_in_pp_table = (uint16_t)allowed_sclk_vdd_table->entries[0].vddc;
	data->max_vddc_in_pp_table = (uint16_t)allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].vddc;

	pptable_info->max_clock_voltage_on_ac.sclk =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].clk;
	pptable_info->max_clock_voltage_on_ac.mclk =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].clk;
	pptable_info->max_clock_voltage_on_ac.vddc =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].vddc;
	pptable_info->max_clock_voltage_on_ac.vddci =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].vddci;

	hwmgr->dyn_state.max_clock_voltage_on_ac.sclk =
		pptable_info->max_clock_voltage_on_ac.sclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.mclk =
		pptable_info->max_clock_voltage_on_ac.mclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddc =
		pptable_info->max_clock_voltage_on_ac.vddc;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddci =
		pptable_info->max_clock_voltage_on_ac.vddci;

	return 0;
}

int tonga_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	int result = 1;

	PP_ASSERT_WITH_CODE (0 == tonga_is_dpm_running(hwmgr),
		"Trying to Unforce DPM when DPM is disabled. Returning without sending SMC message.",
							return result);

	if (0 == data->pcie_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE((0 == smum_send_msg_to_smc(
							     hwmgr->smumgr,
					PPSMC_MSG_PCIeDPM_UnForceLevel)),
					   "unforce pcie level failed!",
								return -1);
	}

	result = tonga_upload_dpm_level_enable_mask(hwmgr);

	return result;
}

static uint32_t tonga_get_lowest_enable_level(
				struct pp_hwmgr *hwmgr, uint32_t level_mask)
{
	uint32_t level = 0;

	while (0 == (level_mask & (1 << level)))
		level++;

	return level;
}

static int tonga_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	uint32_t level;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	if (0 == data->pcie_dpm_key_disabled) {
		/* PCIE */
		if (data->dpm_level_enable_mask.pcie_dpm_enable_mask != 0) {
			level = tonga_get_lowest_enable_level(hwmgr,
							      data->dpm_level_enable_mask.pcie_dpm_enable_mask);
			PP_ASSERT_WITH_CODE((0 == tonga_dpm_force_state_pcie(hwmgr, level)),
					    "force lowest pcie dpm state failed!", return -1);
		}
	}

	if (0 == data->sclk_dpm_key_disabled) {
		/* SCLK */
		if (0 != data->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			level = tonga_get_lowest_enable_level(hwmgr,
							      data->dpm_level_enable_mask.sclk_dpm_enable_mask);

			PP_ASSERT_WITH_CODE((0 == tonga_dpm_force_state(hwmgr, level)),
					    "force sclk dpm state failed!", return -1);

			if (PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device,
							 CGS_IND_REG__SMC, TARGET_AND_CURRENT_PROFILE_INDEX, CURR_SCLK_INDEX) != level)
				printk(KERN_ERR "[ powerplay ] Target_and_current_Profile_Index.	\
				Curr_Sclk_Index does not match the level \n");
		}
	}

	if (0 == data->mclk_dpm_key_disabled) {
		/* MCLK */
		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask != 0) {
			level = tonga_get_lowest_enable_level(hwmgr,
							      data->dpm_level_enable_mask.mclk_dpm_enable_mask);
			PP_ASSERT_WITH_CODE((0 == tonga_dpm_force_state_mclk(hwmgr, level)),
					    "force lowest mclk dpm state failed!", return -1);
			if (PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
							 TARGET_AND_CURRENT_PROFILE_INDEX, CURR_MCLK_INDEX) != level)
				printk(KERN_ERR "[ powerplay ] Target_and_current_Profile_Index. \
						Curr_Mclk_Index does not match the level \n");
		}
	}

	return 0;
}

static int tonga_patch_voltage_dependency_tables_with_lookup_table(struct pp_hwmgr *hwmgr)
{
	uint8_t entryId;
	uint8_t voltageId;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	phm_ppt_v1_clock_voltage_dependency_table *sclk_table = pptable_info->vdd_dep_on_sclk;
	phm_ppt_v1_clock_voltage_dependency_table *mclk_table = pptable_info->vdd_dep_on_mclk;
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table = pptable_info->mm_dep_table;

	if (data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) {
		for (entryId = 0; entryId < sclk_table->count; ++entryId) {
			voltageId = sclk_table->entries[entryId].vddInd;
			sclk_table->entries[entryId].vddgfx =
				pptable_info->vddgfx_lookup_table->entries[voltageId].us_vdd;
		}
	} else {
		for (entryId = 0; entryId < sclk_table->count; ++entryId) {
			voltageId = sclk_table->entries[entryId].vddInd;
			sclk_table->entries[entryId].vddc =
				pptable_info->vddc_lookup_table->entries[voltageId].us_vdd;
		}
	}

	for (entryId = 0; entryId < mclk_table->count; ++entryId) {
		voltageId = mclk_table->entries[entryId].vddInd;
		mclk_table->entries[entryId].vddc =
			pptable_info->vddc_lookup_table->entries[voltageId].us_vdd;
	}

	for (entryId = 0; entryId < mm_table->count; ++entryId) {
		voltageId = mm_table->entries[entryId].vddcInd;
		mm_table->entries[entryId].vddc =
			pptable_info->vddc_lookup_table->entries[voltageId].us_vdd;
	}

	return 0;

}

static int tonga_calc_voltage_dependency_tables(struct pp_hwmgr *hwmgr)
{
	uint8_t entryId;
	phm_ppt_v1_voltage_lookup_record v_record;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	phm_ppt_v1_clock_voltage_dependency_table *sclk_table = pptable_info->vdd_dep_on_sclk;
	phm_ppt_v1_clock_voltage_dependency_table *mclk_table = pptable_info->vdd_dep_on_mclk;

	if (data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) {
		for (entryId = 0; entryId < sclk_table->count; ++entryId) {
			if (sclk_table->entries[entryId].vdd_offset & (1 << 15))
				v_record.us_vdd = sclk_table->entries[entryId].vddgfx +
					sclk_table->entries[entryId].vdd_offset - 0xFFFF;
			else
				v_record.us_vdd = sclk_table->entries[entryId].vddgfx +
					sclk_table->entries[entryId].vdd_offset;

			sclk_table->entries[entryId].vddc =
				v_record.us_cac_low = v_record.us_cac_mid =
				v_record.us_cac_high = v_record.us_vdd;

			tonga_add_voltage(hwmgr, pptable_info->vddc_lookup_table, &v_record);
		}

		for (entryId = 0; entryId < mclk_table->count; ++entryId) {
			if (mclk_table->entries[entryId].vdd_offset & (1 << 15))
				v_record.us_vdd = mclk_table->entries[entryId].vddc +
					mclk_table->entries[entryId].vdd_offset - 0xFFFF;
			else
				v_record.us_vdd = mclk_table->entries[entryId].vddc +
					mclk_table->entries[entryId].vdd_offset;

			mclk_table->entries[entryId].vddgfx = v_record.us_cac_low =
				v_record.us_cac_mid = v_record.us_cac_high = v_record.us_vdd;
			tonga_add_voltage(hwmgr, pptable_info->vddgfx_lookup_table, &v_record);
		}
	}

	return 0;

}

static int tonga_calc_mm_voltage_dependency_table(struct pp_hwmgr *hwmgr)
{
	uint32_t entryId;
	phm_ppt_v1_voltage_lookup_record v_record;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table = pptable_info->mm_dep_table;

	if (data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) {
		for (entryId = 0; entryId < mm_table->count; entryId++) {
			if (mm_table->entries[entryId].vddgfx_offset & (1 << 15))
				v_record.us_vdd = mm_table->entries[entryId].vddc +
					mm_table->entries[entryId].vddgfx_offset - 0xFFFF;
			else
				v_record.us_vdd = mm_table->entries[entryId].vddc +
					mm_table->entries[entryId].vddgfx_offset;

			/* Add the calculated VDDGFX to the VDDGFX lookup table */
			mm_table->entries[entryId].vddgfx = v_record.us_cac_low =
				v_record.us_cac_mid = v_record.us_cac_high = v_record.us_vdd;
			tonga_add_voltage(hwmgr, pptable_info->vddgfx_lookup_table, &v_record);
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
static void tonga_patch_with_vdd_leakage(struct pp_hwmgr *hwmgr,
		uint16_t *voltage, phw_tonga_leakage_voltage *pLeakageTable)
{
	uint32_t leakage_index;

	/* search for leakage voltage ID 0xff01 ~ 0xff08 */
	for (leakage_index = 0; leakage_index < pLeakageTable->count; leakage_index++) {
		/* if this voltage matches a leakage voltage ID */
		/* patch with actual leakage voltage */
		if (pLeakageTable->leakage_id[leakage_index] == *voltage) {
			*voltage = pLeakageTable->actual_voltage[leakage_index];
			break;
		}
	}

	if (*voltage > ATOM_VIRTUAL_VOLTAGE_ID0)
		printk(KERN_ERR "[ powerplay ] Voltage value looks like a Leakage ID but it's not patched \n");
}

/**
 * Patch voltage lookup table by EVV leakages.
 *
 * @param     hwmgr  the address of the powerplay hardware manager.
 * @param     pointer to voltage lookup table
 * @param     pointer to leakage table
 * @return     always 0
 */
static int tonga_patch_lookup_table_with_leakage(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_voltage_lookup_table *lookup_table,
		phw_tonga_leakage_voltage *pLeakageTable)
{
	uint32_t i;

	for (i = 0; i < lookup_table->count; i++) {
		tonga_patch_with_vdd_leakage(hwmgr,
			&lookup_table->entries[i].us_vdd, pLeakageTable);
	}

	return 0;
}

static int tonga_patch_clock_voltage_lomits_with_vddc_leakage(struct pp_hwmgr *hwmgr,
		phw_tonga_leakage_voltage *pLeakageTable, uint16_t *Vddc)
{
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	tonga_patch_with_vdd_leakage(hwmgr, (uint16_t *)Vddc, pLeakageTable);
	hwmgr->dyn_state.max_clock_voltage_on_dc.vddc =
		pptable_info->max_clock_voltage_on_dc.vddc;

	return 0;
}

static int tonga_patch_clock_voltage_limits_with_vddgfx_leakage(
		struct pp_hwmgr *hwmgr, phw_tonga_leakage_voltage *pLeakageTable,
		uint16_t *Vddgfx)
{
	tonga_patch_with_vdd_leakage(hwmgr, (uint16_t *)Vddgfx, pLeakageTable);
	return 0;
}

int tonga_sort_lookup_table(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_voltage_lookup_table *lookup_table)
{
	uint32_t table_size, i, j;
	phm_ppt_v1_voltage_lookup_record tmp_voltage_lookup_record;
	table_size = lookup_table->count;

	PP_ASSERT_WITH_CODE(0 != lookup_table->count,
		"Lookup table is empty", return -1);

	/* Sorting voltages */
	for (i = 0; i < table_size - 1; i++) {
		for (j = i + 1; j > 0; j--) {
			if (lookup_table->entries[j].us_vdd < lookup_table->entries[j-1].us_vdd) {
				tmp_voltage_lookup_record = lookup_table->entries[j-1];
				lookup_table->entries[j-1] = lookup_table->entries[j];
				lookup_table->entries[j] = tmp_voltage_lookup_record;
			}
		}
	}

	return 0;
}

static int tonga_complete_dependency_tables(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	int tmp_result;
	tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (data->vdd_gfx_control == TONGA_VOLTAGE_CONTROL_BY_SVID2) {
		tmp_result = tonga_patch_lookup_table_with_leakage(hwmgr,
			pptable_info->vddgfx_lookup_table, &(data->vddcgfx_leakage));
		if (tmp_result != 0)
			result = tmp_result;

		tmp_result = tonga_patch_clock_voltage_limits_with_vddgfx_leakage(hwmgr,
			&(data->vddcgfx_leakage), &pptable_info->max_clock_voltage_on_dc.vddgfx);
		if (tmp_result != 0)
			result = tmp_result;
	} else {
		tmp_result = tonga_patch_lookup_table_with_leakage(hwmgr,
			pptable_info->vddc_lookup_table, &(data->vddc_leakage));
		if (tmp_result != 0)
			result = tmp_result;

		tmp_result = tonga_patch_clock_voltage_lomits_with_vddc_leakage(hwmgr,
			&(data->vddc_leakage), &pptable_info->max_clock_voltage_on_dc.vddc);
		if (tmp_result != 0)
			result = tmp_result;
	}

	tmp_result = tonga_patch_voltage_dependency_tables_with_lookup_table(hwmgr);
	if (tmp_result != 0)
		result = tmp_result;

	tmp_result = tonga_calc_voltage_dependency_tables(hwmgr);
	if (tmp_result != 0)
		result = tmp_result;

	tmp_result = tonga_calc_mm_voltage_dependency_table(hwmgr);
	if (tmp_result != 0)
		result = tmp_result;

	tmp_result = tonga_sort_lookup_table(hwmgr, pptable_info->vddgfx_lookup_table);
	if (tmp_result != 0)
		result = tmp_result;

	tmp_result = tonga_sort_lookup_table(hwmgr, pptable_info->vddc_lookup_table);
	if (tmp_result != 0)
		result = tmp_result;

	return result;
}

int tonga_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	data->low_sclk_interrupt_threshold = 0;

	return 0;
}

int tonga_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	tmp_result = tonga_read_clock_registers(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to read clock registers!", result = tmp_result);

	tmp_result = tonga_get_memory_type(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to get memory type!", result = tmp_result);

	tmp_result = tonga_enable_acpi_power_management(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable ACPI power management!", result = tmp_result);

	tmp_result = tonga_init_power_gate_state(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to init power gate state!", result = tmp_result);

	tmp_result = tonga_get_mc_microcode_version(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to get MC microcode version!", result = tmp_result);

	tmp_result = tonga_init_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to init sclk threshold!", result = tmp_result);

	return result;
}

/**
 * Enable voltage control
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_enable_voltage_control(struct pp_hwmgr *hwmgr)
{
	/* enable voltage control */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT, VOLT_PWRMGT_EN, 1);

	return 0;
}

/**
 * Checks if we want to support voltage control
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 */
bool cf_tonga_voltage_control(const struct pp_hwmgr *hwmgr)
{
	const struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	return(TONGA_VOLTAGE_CONTROL_NONE != data->voltage_control);
}

/*---------------------------MC----------------------------*/

uint8_t tonga_get_memory_modile_index(struct pp_hwmgr *hwmgr)
{
	return (uint8_t) (0xFF & (cgs_read_register(hwmgr->device, mmBIOS_SCRATCH_4) >> 16));
}

bool tonga_check_s0_mc_reg_index(uint16_t inReg, uint16_t *outReg)
{
	bool result = 1;

	switch (inReg) {
	case  mmMC_SEQ_RAS_TIMING:
		*outReg = mmMC_SEQ_RAS_TIMING_LP;
		break;

	case  mmMC_SEQ_DLL_STBY:
		*outReg = mmMC_SEQ_DLL_STBY_LP;
		break;

	case  mmMC_SEQ_G5PDX_CMD0:
		*outReg = mmMC_SEQ_G5PDX_CMD0_LP;
		break;

	case  mmMC_SEQ_G5PDX_CMD1:
		*outReg = mmMC_SEQ_G5PDX_CMD1_LP;
		break;

	case  mmMC_SEQ_G5PDX_CTRL:
		*outReg = mmMC_SEQ_G5PDX_CTRL_LP;
		break;

	case mmMC_SEQ_CAS_TIMING:
		*outReg = mmMC_SEQ_CAS_TIMING_LP;
		break;

	case mmMC_SEQ_MISC_TIMING:
		*outReg = mmMC_SEQ_MISC_TIMING_LP;
		break;

	case mmMC_SEQ_MISC_TIMING2:
		*outReg = mmMC_SEQ_MISC_TIMING2_LP;
		break;

	case mmMC_SEQ_PMG_DVS_CMD:
		*outReg = mmMC_SEQ_PMG_DVS_CMD_LP;
		break;

	case mmMC_SEQ_PMG_DVS_CTL:
		*outReg = mmMC_SEQ_PMG_DVS_CTL_LP;
		break;

	case mmMC_SEQ_RD_CTL_D0:
		*outReg = mmMC_SEQ_RD_CTL_D0_LP;
		break;

	case mmMC_SEQ_RD_CTL_D1:
		*outReg = mmMC_SEQ_RD_CTL_D1_LP;
		break;

	case mmMC_SEQ_WR_CTL_D0:
		*outReg = mmMC_SEQ_WR_CTL_D0_LP;
		break;

	case mmMC_SEQ_WR_CTL_D1:
		*outReg = mmMC_SEQ_WR_CTL_D1_LP;
		break;

	case mmMC_PMG_CMD_EMRS:
		*outReg = mmMC_SEQ_PMG_CMD_EMRS_LP;
		break;

	case mmMC_PMG_CMD_MRS:
		*outReg = mmMC_SEQ_PMG_CMD_MRS_LP;
		break;

	case mmMC_PMG_CMD_MRS1:
		*outReg = mmMC_SEQ_PMG_CMD_MRS1_LP;
		break;

	case mmMC_SEQ_PMG_TIMING:
		*outReg = mmMC_SEQ_PMG_TIMING_LP;
		break;

	case mmMC_PMG_CMD_MRS2:
		*outReg = mmMC_SEQ_PMG_CMD_MRS2_LP;
		break;

	case mmMC_SEQ_WR_CTL_2:
		*outReg = mmMC_SEQ_WR_CTL_2_LP;
		break;

	default:
		result = 0;
		break;
	}

	return result;
}

int tonga_set_s0_mc_reg_index(phw_tonga_mc_reg_table *table)
{
	uint32_t i;
	uint16_t address;

	for (i = 0; i < table->last; i++) {
		table->mc_reg_address[i].s0 =
			tonga_check_s0_mc_reg_index(table->mc_reg_address[i].s1, &address)
			? address : table->mc_reg_address[i].s1;
	}
	return 0;
}

int tonga_copy_vbios_smc_reg_table(const pp_atomctrl_mc_reg_table *table, phw_tonga_mc_reg_table *ni_table)
{
	uint8_t i, j;

	PP_ASSERT_WITH_CODE((table->last <= SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
		"Invalid VramInfo table.", return -1);
	PP_ASSERT_WITH_CODE((table->num_entries <= MAX_AC_TIMING_ENTRIES),
		"Invalid VramInfo table.", return -1);

	for (i = 0; i < table->last; i++) {
		ni_table->mc_reg_address[i].s1 = table->mc_reg_address[i].s1;
	}
	ni_table->last = table->last;

	for (i = 0; i < table->num_entries; i++) {
		ni_table->mc_reg_table_entry[i].mclk_max =
			table->mc_reg_table_entry[i].mclk_max;
		for (j = 0; j < table->last; j++) {
			ni_table->mc_reg_table_entry[i].mc_data[j] =
				table->mc_reg_table_entry[i].mc_data[j];
		}
	}

	ni_table->num_entries = table->num_entries;

	return 0;
}

/**
 * VBIOS omits some information to reduce size, we need to recover them here.
 * 1.   when we see mmMC_SEQ_MISC1, bit[31:16] EMRS1, need to be write to  mmMC_PMG_CMD_EMRS /_LP[15:0].
 *      Bit[15:0] MRS, need to be update mmMC_PMG_CMD_MRS/_LP[15:0]
 * 2.   when we see mmMC_SEQ_RESERVE_M, bit[15:0] EMRS2, need to be write to mmMC_PMG_CMD_MRS1/_LP[15:0].
 * 3.   need to set these data for each clock range
 *
 * @param    hwmgr the address of the powerplay hardware manager.
 * @param    table the address of MCRegTable
 * @return   always 0
 */
int tonga_set_mc_special_registers(struct pp_hwmgr *hwmgr, phw_tonga_mc_reg_table *table)
{
	uint8_t i, j, k;
	uint32_t temp_reg;
	const tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	for (i = 0, j = table->last; i < table->last; i++) {
		PP_ASSERT_WITH_CODE((j < SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
			"Invalid VramInfo table.", return -1);
		switch (table->mc_reg_address[i].s1) {
		/*
		* mmMC_SEQ_MISC1, bit[31:16] EMRS1, need to be write to  mmMC_PMG_CMD_EMRS /_LP[15:0].
		* Bit[15:0] MRS, need to be update mmMC_PMG_CMD_MRS/_LP[15:0]
		*/
		case mmMC_SEQ_MISC1:
			temp_reg = cgs_read_register(hwmgr->device, mmMC_PMG_CMD_EMRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_EMRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_EMRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					((temp_reg & 0xffff0000)) |
					((table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16);
			}
			j++;
			PP_ASSERT_WITH_CODE((j < SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
				"Invalid VramInfo table.", return -1);

			temp_reg = cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_MRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_MRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);

				if (!data->is_memory_GDDR5) {
					table->mc_reg_table_entry[k].mc_data[j] |= 0x100;
				}
			}
			j++;
			PP_ASSERT_WITH_CODE((j <= SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
				"Invalid VramInfo table.", return -1);

			if (!data->is_memory_GDDR5) {
				table->mc_reg_address[j].s1 = mmMC_PMG_AUTO_CMD;
				table->mc_reg_address[j].s0 = mmMC_PMG_AUTO_CMD;
				for (k = 0; k < table->num_entries; k++) {
					table->mc_reg_table_entry[k].mc_data[j] =
						(table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16;
				}
				j++;
				PP_ASSERT_WITH_CODE((j <= SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
					"Invalid VramInfo table.", return -1);
			}

			break;

		case mmMC_SEQ_RESERVE_M:
			temp_reg = cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS1);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_MRS1;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_MRS1_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
			}
			j++;
			PP_ASSERT_WITH_CODE((j <= SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
				"Invalid VramInfo table.", return -1);
			break;

		default:
			break;
		}

	}

	table->last = j;

	return 0;
}

int tonga_set_valid_flag(phw_tonga_mc_reg_table *table)
{
	uint8_t i, j;
	for (i = 0; i < table->last; i++) {
		for (j = 1; j < table->num_entries; j++) {
			if (table->mc_reg_table_entry[j-1].mc_data[i] !=
				table->mc_reg_table_entry[j].mc_data[i]) {
				table->validflag |= (1<<i);
				break;
			}
		}
	}

	return 0;
}

int tonga_initialize_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);
	pp_atomctrl_mc_reg_table *table;
	phw_tonga_mc_reg_table *ni_table = &data->tonga_mc_reg_table;
	uint8_t module_index = tonga_get_memory_modile_index(hwmgr);

	table = kzalloc(sizeof(pp_atomctrl_mc_reg_table), GFP_KERNEL);

	if (NULL == table)
		return -ENOMEM;

	/* Program additional LP registers that are no longer programmed by VBIOS */
	cgs_write_register(hwmgr->device, mmMC_SEQ_RAS_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_RAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_CAS_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_CAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_DLL_STBY_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_DLL_STBY));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD0_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD1_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CTRL_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CTRL));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CMD_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CMD));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CTL_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CTL));
	cgs_write_register(hwmgr->device, mmMC_SEQ_MISC_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_MISC_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_EMRS_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_EMRS));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS1_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_D0_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_D0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS2_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS2));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_2_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_2));

	memset(table, 0x00, sizeof(pp_atomctrl_mc_reg_table));

	result = atomctrl_initialize_mc_reg_table(hwmgr, module_index, table);

	if (0 == result)
		result = tonga_copy_vbios_smc_reg_table(table, ni_table);

	if (0 == result) {
		tonga_set_s0_mc_reg_index(ni_table);
		result = tonga_set_mc_special_registers(hwmgr, ni_table);
	}

	if (0 == result)
		tonga_set_valid_flag(ni_table);

	kfree(table);
	return result;
}

/*
* Copy one arb setting to another and then switch the active set.
* arbFreqSrc and arbFreqDest is one of the MC_CG_ARB_FREQ_Fx constants.
*/
int tonga_copy_and_switch_arb_sets(struct pp_hwmgr *hwmgr,
		uint32_t arbFreqSrc, uint32_t arbFreqDest)
{
	uint32_t mc_arb_dram_timing;
	uint32_t mc_arb_dram_timing2;
	uint32_t burst_time;
	uint32_t mc_cg_config;

	switch (arbFreqSrc) {
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
		return -1;
	}

	switch (arbFreqDest) {
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
		return -1;
	}

	mc_cg_config = cgs_read_register(hwmgr->device, mmMC_CG_CONFIG);
	mc_cg_config |= 0x0000000F;
	cgs_write_register(hwmgr->device, mmMC_CG_CONFIG, mc_cg_config);
	PHM_WRITE_FIELD(hwmgr->device, MC_ARB_CG, CG_ARB_REQ, arbFreqDest);

	return 0;
}

/**
 * Initial switch from ARB F0->F1
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 * This function is to be called from the SetPowerState table.
 */
int tonga_initial_switch_from_arb_f0_to_f1(struct pp_hwmgr *hwmgr)
{
	return tonga_copy_and_switch_arb_sets(hwmgr, MC_CG_ARB_FREQ_F0, MC_CG_ARB_FREQ_F1);
}

/**
 * Initialize the ARB DRAM timing table's index field.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_init_arb_table_index(struct pp_hwmgr *hwmgr)
{
	const tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	uint32_t tmp;
	int result;

	/*
	* This is a read-modify-write on the first byte of the ARB table.
	* The first byte in the SMU72_Discrete_MCArbDramTimingTable structure is the field 'current'.
	* This solution is ugly, but we never write the whole table only individual fields in it.
	* In reality this field should not be in that structure but in a soft register.
	*/
	result = tonga_read_smc_sram_dword(hwmgr->smumgr,
				data->arb_table_start, &tmp, data->sram_end);

	if (0 != result)
		return result;

	tmp &= 0x00FFFFFF;
	tmp |= ((uint32_t)MC_CG_ARB_FREQ_F1) << 24;

	return tonga_write_smc_sram_dword(hwmgr->smumgr,
			data->arb_table_start,  tmp, data->sram_end);
}

int tonga_populate_mc_reg_address(struct pp_hwmgr *hwmgr, SMU72_Discrete_MCRegisters *mc_reg_table)
{
	const struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	uint32_t i, j;

	for (i = 0, j = 0; j < data->tonga_mc_reg_table.last; j++) {
		if (data->tonga_mc_reg_table.validflag & 1<<j) {
			PP_ASSERT_WITH_CODE(i < SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE,
				"Index of mc_reg_table->address[] array out of boundary", return -1);
			mc_reg_table->address[i].s0 =
				PP_HOST_TO_SMC_US(data->tonga_mc_reg_table.mc_reg_address[j].s0);
			mc_reg_table->address[i].s1 =
				PP_HOST_TO_SMC_US(data->tonga_mc_reg_table.mc_reg_address[j].s1);
			i++;
		}
	}

	mc_reg_table->last = (uint8_t)i;

	return 0;
}

/*convert register values from driver to SMC format */
void tonga_convert_mc_registers(
	const phw_tonga_mc_reg_entry * pEntry,
	SMU72_Discrete_MCRegisterSet *pData,
	uint32_t numEntries, uint32_t validflag)
{
	uint32_t i, j;

	for (i = 0, j = 0; j < numEntries; j++) {
		if (validflag & 1<<j) {
			pData->value[i] = PP_HOST_TO_SMC_UL(pEntry->mc_data[j]);
			i++;
		}
	}
}

/* find the entry in the memory range table, then populate the value to SMC's tonga_mc_reg_table */
int tonga_convert_mc_reg_table_entry_to_smc(
		struct pp_hwmgr *hwmgr,
		const uint32_t memory_clock,
		SMU72_Discrete_MCRegisterSet *mc_reg_table_data
		)
{
	const tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	uint32_t i = 0;

	for (i = 0; i < data->tonga_mc_reg_table.num_entries; i++) {
		if (memory_clock <=
			data->tonga_mc_reg_table.mc_reg_table_entry[i].mclk_max) {
			break;
		}
	}

	if ((i == data->tonga_mc_reg_table.num_entries) && (i > 0))
		--i;

	tonga_convert_mc_registers(&data->tonga_mc_reg_table.mc_reg_table_entry[i],
		mc_reg_table_data, data->tonga_mc_reg_table.last, data->tonga_mc_reg_table.validflag);

	return 0;
}

int tonga_convert_mc_reg_table_to_smc(struct pp_hwmgr *hwmgr,
		SMU72_Discrete_MCRegisters *mc_reg_table)
{
	int result = 0;
	tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	int res;
	uint32_t i;

	for (i = 0; i < data->dpm_table.mclk_table.count; i++) {
		res = tonga_convert_mc_reg_table_entry_to_smc(
				hwmgr,
				data->dpm_table.mclk_table.dpm_levels[i].value,
				&mc_reg_table->data[i]
				);

		if (0 != res)
			result = res;
	}

	return result;
}

int tonga_populate_initial_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	memset(&data->mc_reg_table, 0x00, sizeof(SMU72_Discrete_MCRegisters));
	result = tonga_populate_mc_reg_address(hwmgr, &(data->mc_reg_table));
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize MCRegTable for the MC register addresses!", return result;);

	result = tonga_convert_mc_reg_table_to_smc(hwmgr, &data->mc_reg_table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize MCRegTable for driver state!", return result;);

	return tonga_copy_bytes_to_smc(hwmgr->smumgr, data->mc_reg_table_start,
			(uint8_t *)&data->mc_reg_table, sizeof(SMU72_Discrete_MCRegisters), data->sram_end);
}

/**
 * Programs static screed detection parameters
 *
 * @param   hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_program_static_screen_threshold_parameters(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	/* Set static screen threshold unit*/
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device,
		CGS_IND_REG__SMC, CG_STATIC_SCREEN_PARAMETER, STATIC_SCREEN_THRESHOLD_UNIT,
		data->static_screen_threshold_unit);
	/* Set static screen threshold*/
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device,
		CGS_IND_REG__SMC, CG_STATIC_SCREEN_PARAMETER, STATIC_SCREEN_THRESHOLD,
		data->static_screen_threshold);

	return 0;
}

/**
 * Setup display gap for glitch free memory clock switching.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_enable_display_gap(struct pp_hwmgr *hwmgr)
{
	uint32_t display_gap = cgs_read_ind_register(hwmgr->device,
							CGS_IND_REG__SMC, ixCG_DISPLAY_GAP_CNTL);

	display_gap = PHM_SET_FIELD(display_gap,
					CG_DISPLAY_GAP_CNTL, DISP_GAP, DISPLAY_GAP_IGNORE);

	display_gap = PHM_SET_FIELD(display_gap,
					CG_DISPLAY_GAP_CNTL, DISP_GAP_MCHG, DISPLAY_GAP_VBLANK);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
		ixCG_DISPLAY_GAP_CNTL, display_gap);

	return 0;
}

/**
 * Programs activity state transition voting clients
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int tonga_program_voting_clients(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr *data = (tonga_hwmgr *)(hwmgr->backend);

	/* Clear reset for voting clients before enabling DPM */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SCLK_PWRMGT_CNTL, RESET_SCLK_CNT, 0);
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
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


int tonga_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	tmp_result = tonga_check_for_dpm_stopped(hwmgr);

	if (cf_tonga_voltage_control(hwmgr)) {
		tmp_result = tonga_enable_voltage_control(hwmgr);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable voltage control!", result = tmp_result);

		tmp_result = tonga_construct_voltage_tables(hwmgr);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to contruct voltage tables!", result = tmp_result);
	}

	tmp_result = tonga_initialize_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to initialize MC reg table!", result = tmp_result);

	tmp_result = tonga_program_static_screen_threshold_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to program static screen threshold parameters!", result = tmp_result);

	tmp_result = tonga_enable_display_gap(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable display gap!", result = tmp_result);

	tmp_result = tonga_program_voting_clients(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to program voting clients!", result = tmp_result);

	tmp_result = tonga_process_firmware_header(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to process firmware header!", result = tmp_result);

	tmp_result = tonga_initial_switch_from_arb_f0_to_f1(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to initialize switch from ArbF0 to F1!", result = tmp_result);

	tmp_result = tonga_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to initialize SMC table!", result = tmp_result);

	tmp_result = tonga_init_arb_table_index(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to initialize ARB table index!", result = tmp_result);

	tmp_result = tonga_populate_initial_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to populate initialize MC Reg table!", result = tmp_result);

	tmp_result = tonga_notify_smc_display_change(hwmgr, false);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to notify no display!", result = tmp_result);

	/* enable SCLK control */
	tmp_result = tonga_enable_sclk_control(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable SCLK control!", result = tmp_result);

	/* enable DPM */
	tmp_result = tonga_start_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to start DPM!", result = tmp_result);

	return result;
}

int tonga_disable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	tmp_result = tonga_check_for_dpm_running(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"SMC is still running!", return 0);

	tmp_result = tonga_stop_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to stop DPM!", result = tmp_result);

	tmp_result = tonga_reset_to_default(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to reset to default!", result = tmp_result);

	return result;
}

int tonga_reset_asic_tasks(struct pp_hwmgr *hwmgr)
{
	int result;

	result = tonga_set_boot_state(hwmgr);
	if (0 != result)
		printk(KERN_ERR "[ powerplay ] Failed to reset asic via set boot state! \n");

	return result;
}

int tonga_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	if (NULL != hwmgr->dyn_state.vddc_dep_on_dal_pwrl) {
		kfree(hwmgr->dyn_state.vddc_dep_on_dal_pwrl);
		hwmgr->dyn_state.vddc_dep_on_dal_pwrl = NULL;
	}

	if (NULL != hwmgr->backend) {
		kfree(hwmgr->backend);
		hwmgr->backend = NULL;
	}

	return 0;
}

/**
 * Initializes the Volcanic Islands Hardware Manager
 *
 * @param   hwmgr the address of the powerplay hardware manager.
 * @return   1 if success; otherwise appropriate error code.
 */
int tonga_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	SMU72_Discrete_DpmTable  *table = NULL;
	tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	pp_atomctrl_gpio_pin_assignment gpio_pin_assignment;
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phw_tonga_ulv_parm *ulv;
	struct cgs_system_info sys_info = {0};

	PP_ASSERT_WITH_CODE((NULL != hwmgr),
		"Invalid Parameter!", return -1;);

	data->dll_defaule_on = 0;
	data->sram_end = SMC_RAM_END;

	data->activity_target[0] = PPTONGA_TARGETACTIVITY_DFLT;
	data->activity_target[1] = PPTONGA_TARGETACTIVITY_DFLT;
	data->activity_target[2] = PPTONGA_TARGETACTIVITY_DFLT;
	data->activity_target[3] = PPTONGA_TARGETACTIVITY_DFLT;
	data->activity_target[4] = PPTONGA_TARGETACTIVITY_DFLT;
	data->activity_target[5] = PPTONGA_TARGETACTIVITY_DFLT;
	data->activity_target[6] = PPTONGA_TARGETACTIVITY_DFLT;
	data->activity_target[7] = PPTONGA_TARGETACTIVITY_DFLT;

	data->vddc_vddci_delta = VDDC_VDDCI_DELTA;
	data->vddc_vddgfx_delta = VDDC_VDDGFX_DELTA;
	data->mclk_activity_target = PPTONGA_MCLK_TARGETACTIVITY_DFLT;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_DisableVoltageIsland);

	data->sclk_dpm_key_disabled = 0;
	data->mclk_dpm_key_disabled = 0;
	data->pcie_dpm_key_disabled = 0;
	data->pcc_monitor_enabled = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_UnTabledHardwareInterface);

	data->gpio_debug = 0;
	data->engine_clock_data = 0;
	data->memory_clock_data = 0;
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_DynamicPatchPowerState);

	/* need to set voltage control types before EVV patching*/
	data->voltage_control = TONGA_VOLTAGE_CONTROL_NONE;
	data->vdd_ci_control = TONGA_VOLTAGE_CONTROL_NONE;
	data->vdd_gfx_control = TONGA_VOLTAGE_CONTROL_NONE;
	data->mvdd_control = TONGA_VOLTAGE_CONTROL_NONE;

	if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_SVID2)) {
		data->voltage_control = TONGA_VOLTAGE_CONTROL_BY_SVID2;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ControlVDDGFX)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
			VOLTAGE_TYPE_VDDGFX, VOLTAGE_OBJ_SVID2)) {
			data->vdd_gfx_control = TONGA_VOLTAGE_CONTROL_BY_SVID2;
		}
	}

	if (TONGA_VOLTAGE_CONTROL_NONE == data->vdd_gfx_control) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ControlVDDGFX);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableMVDDControl)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
					VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT)) {
			data->mvdd_control = TONGA_VOLTAGE_CONTROL_BY_GPIO;
		}
	}

	if (TONGA_VOLTAGE_CONTROL_NONE == data->mvdd_control) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableMVDDControl);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ControlVDDCI)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
					VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT))
			data->vdd_ci_control = TONGA_VOLTAGE_CONTROL_BY_GPIO;
		else if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
						VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_SVID2))
			data->vdd_ci_control = TONGA_VOLTAGE_CONTROL_BY_SVID2;
	}

	if (TONGA_VOLTAGE_CONTROL_NONE == data->vdd_ci_control)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_ControlVDDCI);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_TablelessHardwareInterface);

	if (pptable_info->cac_dtp_table->usClockStretchAmount != 0)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ClockStretcher);

	/* Initializes DPM default values*/
	tonga_initialize_dpm_defaults(hwmgr);

	/* Get leakage voltage based on leakage ID.*/
	PP_ASSERT_WITH_CODE((0 == tonga_get_evv_voltage(hwmgr)),
		"Get EVV Voltage Failed.  Abort Driver loading!", return -1);

	tonga_complete_dependency_tables(hwmgr);

	/* Parse pptable data read from VBIOS*/
	tonga_set_private_var_based_on_pptale(hwmgr);

	/* ULV Support*/
	ulv = &(data->ulv);
	ulv->ulv_supported = 0;

	/* Initalize Dynamic State Adjustment Rule Settings*/
	result = tonga_initializa_dynamic_state_adjustment_rule_settings(hwmgr);
	if (result)
		printk(KERN_ERR "[ powerplay ] tonga_initializa_dynamic_state_adjustment_rule_settings failed!\n");
	data->uvd_enabled = 0;

	table = &(data->smc_state_table);

	/*
	* if ucGPIO_ID=VDDC_PCC_GPIO_PINID in GPIO_LUTable,
	* Peak Current Control feature is enabled and we should program PCC HW register
	*/
	if (0 == atomctrl_get_pp_assign_pin(hwmgr, VDDC_PCC_GPIO_PINID, &gpio_pin_assignment)) {
		uint32_t temp_reg = cgs_read_ind_register(hwmgr->device,
										CGS_IND_REG__SMC, ixCNB_PWRMGT_CNTL);

		switch (gpio_pin_assignment.uc_gpio_pin_bit_shift) {
		case 0:
			temp_reg = PHM_SET_FIELD(temp_reg,
				CNB_PWRMGT_CNTL, GNB_SLOW_MODE, 0x1);
			break;
		case 1:
			temp_reg = PHM_SET_FIELD(temp_reg,
				CNB_PWRMGT_CNTL, GNB_SLOW_MODE, 0x2);
			break;
		case 2:
			temp_reg = PHM_SET_FIELD(temp_reg,
				CNB_PWRMGT_CNTL, GNB_SLOW, 0x1);
			break;
		case 3:
			temp_reg = PHM_SET_FIELD(temp_reg,
				CNB_PWRMGT_CNTL, FORCE_NB_PS1, 0x1);
			break;
		case 4:
			temp_reg = PHM_SET_FIELD(temp_reg,
				CNB_PWRMGT_CNTL, DPM_ENABLED, 0x1);
			break;
		default:
			printk(KERN_ERR "[ powerplay ] Failed to setup PCC HW register!	\
				Wrong GPIO assigned for VDDC_PCC_GPIO_PINID! \n");
			break;
		}
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCNB_PWRMGT_CNTL, temp_reg);
	}

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_EnableSMU7ThermalManagement);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_SMU7);

	data->vddc_phase_shed_control = 0;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_UVDPowerGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_VCEPowerGating);
	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_PG_FLAGS;
	result = cgs_query_system_info(hwmgr->device, &sys_info);
	if (!result) {
		if (sys_info.value & AMD_PG_SUPPORT_UVD)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				      PHM_PlatformCaps_UVDPowerGating);
		if (sys_info.value & AMD_PG_SUPPORT_VCE)
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				      PHM_PlatformCaps_VCEPowerGating);
	}

	if (0 == result) {
		data->is_tlu_enabled = 0;
		hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
			TONGA_MAX_HARDWARE_POWERLEVELS;
		hwmgr->platform_descriptor.hardwarePerformanceLevels = 2;
		hwmgr->platform_descriptor.minimumClocksReductionPercentage  = 50;

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
		tonga_hwmgr_backend_fini(hwmgr);
	}

	return result;
}

static int tonga_force_dpm_level(struct pp_hwmgr *hwmgr,
		enum amd_dpm_forced_level level)
{
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = tonga_force_dpm_highest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = tonga_force_dpm_lowest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = tonga_unforce_dpm_levels(hwmgr);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	hwmgr->dpm_level = level;
	return ret;
}

static int tonga_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps)
{
	struct tonga_power_state *tonga_ps =
				cast_phw_tonga_power_state(&prequest_ps->hardware);

	uint32_t sclk;
	uint32_t mclk;
	struct PP_Clocks minimum_clocks = {0};
	bool disable_mclk_switching;
	bool disable_mclk_switching_for_frame_lock;
	struct cgs_display_info info = {0};
	const struct phm_clock_and_voltage_limits *max_limits;
	uint32_t i;
	tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	int32_t count;
	int32_t stable_pstate_sclk = 0, stable_pstate_mclk = 0;

	data->battery_state = (PP_StateUILabel_Battery == prequest_ps->classification.ui_label);

	PP_ASSERT_WITH_CODE(tonga_ps->performance_level_count == 2,
				 "VI should always have 2 performance levels",
				 );

	max_limits = (PP_PowerSource_AC == hwmgr->power_source) ?
			&(hwmgr->dyn_state.max_clock_voltage_on_ac) :
			&(hwmgr->dyn_state.max_clock_voltage_on_dc);

	if (PP_PowerSource_DC == hwmgr->power_source) {
		for (i = 0; i < tonga_ps->performance_level_count; i++) {
			if (tonga_ps->performance_levels[i].memory_clock > max_limits->mclk)
				tonga_ps->performance_levels[i].memory_clock = max_limits->mclk;
			if (tonga_ps->performance_levels[i].engine_clock > max_limits->sclk)
				tonga_ps->performance_levels[i].engine_clock = max_limits->sclk;
		}
	}

	tonga_ps->vce_clocks.EVCLK = hwmgr->vce_arbiter.evclk;
	tonga_ps->vce_clocks.ECCLK = hwmgr->vce_arbiter.ecclk;

	tonga_ps->acp_clk = hwmgr->acp_arbiter.acpclk;

	cgs_get_active_displays_info(hwmgr->device, &info);

	/*TO DO result = PHM_CheckVBlankTime(hwmgr, &vblankTooShort);*/

	/* TO DO GetMinClockSettings(hwmgr->pPECI, &minimum_clocks); */

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState)) {

		max_limits = &(hwmgr->dyn_state.max_clock_voltage_on_ac);
		stable_pstate_sclk = (max_limits->sclk * 75) / 100;

		for (count = pptable_info->vdd_dep_on_sclk->count-1; count >= 0; count--) {
			if (stable_pstate_sclk >= pptable_info->vdd_dep_on_sclk->entries[count].clk) {
				stable_pstate_sclk = pptable_info->vdd_dep_on_sclk->entries[count].clk;
				break;
			}
		}

		if (count < 0)
			stable_pstate_sclk = pptable_info->vdd_dep_on_sclk->entries[0].clk;

		stable_pstate_mclk = max_limits->mclk;

		minimum_clocks.engineClock = stable_pstate_sclk;
		minimum_clocks.memoryClock = stable_pstate_mclk;
	}

	if (minimum_clocks.engineClock < hwmgr->gfx_arbiter.sclk)
		minimum_clocks.engineClock = hwmgr->gfx_arbiter.sclk;

	if (minimum_clocks.memoryClock < hwmgr->gfx_arbiter.mclk)
		minimum_clocks.memoryClock = hwmgr->gfx_arbiter.mclk;

	tonga_ps->sclk_threshold = hwmgr->gfx_arbiter.sclk_threshold;

	if (0 != hwmgr->gfx_arbiter.sclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.sclk_over_drive <= hwmgr->platform_descriptor.overdriveLimit.engineClock),
					"Overdrive sclk exceeds limit",
					hwmgr->gfx_arbiter.sclk_over_drive = hwmgr->platform_descriptor.overdriveLimit.engineClock);

		if (hwmgr->gfx_arbiter.sclk_over_drive >= hwmgr->gfx_arbiter.sclk)
			tonga_ps->performance_levels[1].engine_clock = hwmgr->gfx_arbiter.sclk_over_drive;
	}

	if (0 != hwmgr->gfx_arbiter.mclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.mclk_over_drive <= hwmgr->platform_descriptor.overdriveLimit.memoryClock),
			"Overdrive mclk exceeds limit",
			hwmgr->gfx_arbiter.mclk_over_drive = hwmgr->platform_descriptor.overdriveLimit.memoryClock);

		if (hwmgr->gfx_arbiter.mclk_over_drive >= hwmgr->gfx_arbiter.mclk)
			tonga_ps->performance_levels[1].memory_clock = hwmgr->gfx_arbiter.mclk_over_drive;
	}

	disable_mclk_switching_for_frame_lock = phm_cap_enabled(
				    hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_DisableMclkSwitchingForFrameLock);

	disable_mclk_switching = (1 < info.display_count) ||
				    disable_mclk_switching_for_frame_lock;

	sclk  = tonga_ps->performance_levels[0].engine_clock;
	mclk  = tonga_ps->performance_levels[0].memory_clock;

	if (disable_mclk_switching)
		mclk  = tonga_ps->performance_levels[tonga_ps->performance_level_count - 1].memory_clock;

	if (sclk < minimum_clocks.engineClock)
		sclk = (minimum_clocks.engineClock > max_limits->sclk) ? max_limits->sclk : minimum_clocks.engineClock;

	if (mclk < minimum_clocks.memoryClock)
		mclk = (minimum_clocks.memoryClock > max_limits->mclk) ? max_limits->mclk : minimum_clocks.memoryClock;

	tonga_ps->performance_levels[0].engine_clock = sclk;
	tonga_ps->performance_levels[0].memory_clock = mclk;

	tonga_ps->performance_levels[1].engine_clock =
		(tonga_ps->performance_levels[1].engine_clock >= tonga_ps->performance_levels[0].engine_clock) ?
			      tonga_ps->performance_levels[1].engine_clock :
			      tonga_ps->performance_levels[0].engine_clock;

	if (disable_mclk_switching) {
		if (mclk < tonga_ps->performance_levels[1].memory_clock)
			mclk = tonga_ps->performance_levels[1].memory_clock;

		tonga_ps->performance_levels[0].memory_clock = mclk;
		tonga_ps->performance_levels[1].memory_clock = mclk;
	} else {
		if (tonga_ps->performance_levels[1].memory_clock < tonga_ps->performance_levels[0].memory_clock)
			tonga_ps->performance_levels[1].memory_clock = tonga_ps->performance_levels[0].memory_clock;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState)) {
		for (i=0; i < tonga_ps->performance_level_count; i++) {
			tonga_ps->performance_levels[i].engine_clock = stable_pstate_sclk;
			tonga_ps->performance_levels[i].memory_clock = stable_pstate_mclk;
			tonga_ps->performance_levels[i].pcie_gen = data->pcie_gen_performance.max;
			tonga_ps->performance_levels[i].pcie_lane = data->pcie_gen_performance.max;
		}
	}

	return 0;
}

int tonga_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct tonga_power_state);
}

static int tonga_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct tonga_power_state  *tonga_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	tonga_ps = cast_phw_tonga_power_state(&ps->hardware);

	if (low)
		return tonga_ps->performance_levels[0].memory_clock;
	else
		return tonga_ps->performance_levels[tonga_ps->performance_level_count-1].memory_clock;
}

static int tonga_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct tonga_power_state  *tonga_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	tonga_ps = cast_phw_tonga_power_state(&ps->hardware);

	if (low)
		return tonga_ps->performance_levels[0].engine_clock;
	else
		return tonga_ps->performance_levels[tonga_ps->performance_level_count-1].engine_clock;
}

static uint16_t tonga_get_current_pcie_speed(
						   struct pp_hwmgr *hwmgr)
{
	uint32_t speed_cntl = 0;

	speed_cntl = cgs_read_ind_register(hwmgr->device,
						   CGS_IND_REG__PCIE,
						   ixPCIE_LC_SPEED_CNTL);
	return((uint16_t)PHM_GET_FIELD(speed_cntl,
			PCIE_LC_SPEED_CNTL, LC_CURRENT_DATA_RATE));
}

static int tonga_get_current_pcie_lane_number(
						   struct pp_hwmgr *hwmgr)
{
	uint32_t link_width;

	link_width = PHM_READ_INDIRECT_FIELD(hwmgr->device,
							CGS_IND_REG__PCIE,
						  PCIE_LC_LINK_WIDTH_CNTL,
							LC_LINK_WIDTH_RD);

	PP_ASSERT_WITH_CODE((7 >= link_width),
			"Invalid PCIe lane width!", return 0);

	return decode_pcie_lane_width(link_width);
}

static int tonga_dpm_patch_boot_state(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *hw_ps)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct tonga_power_state *ps = (struct tonga_power_state *)hw_ps;
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
	data->vbios_boot_state.sclk_bootup_value  = le32_to_cpu(fw_info->ulDefaultEngineClock);
	data->vbios_boot_state.mclk_bootup_value  = le32_to_cpu(fw_info->ulDefaultMemoryClock);
	data->vbios_boot_state.mvdd_bootup_value  = le16_to_cpu(fw_info->usBootUpMVDDCVoltage);
	data->vbios_boot_state.vddc_bootup_value  = le16_to_cpu(fw_info->usBootUpVDDCVoltage);
	data->vbios_boot_state.vddci_bootup_value = le16_to_cpu(fw_info->usBootUpVDDCIVoltage);
	data->vbios_boot_state.pcie_gen_bootup_value = tonga_get_current_pcie_speed(hwmgr);
	data->vbios_boot_state.pcie_lane_bootup_value =
			(uint16_t)tonga_get_current_pcie_lane_number(hwmgr);

	/* set boot power state */
	ps->performance_levels[0].memory_clock = data->vbios_boot_state.mclk_bootup_value;
	ps->performance_levels[0].engine_clock = data->vbios_boot_state.sclk_bootup_value;
	ps->performance_levels[0].pcie_gen = data->vbios_boot_state.pcie_gen_bootup_value;
	ps->performance_levels[0].pcie_lane = data->vbios_boot_state.pcie_lane_bootup_value;

	return 0;
}

static int tonga_get_pp_table_entry_callback_func(struct pp_hwmgr *hwmgr,
		void *state, struct pp_power_state *power_state,
		void *pp_table, uint32_t classification_flag)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	struct tonga_power_state  *tonga_ps =
			(struct tonga_power_state *)(&(power_state->hardware));

	struct tonga_performance_level *performance_level;

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
			(0 != (le32_to_cpu(state_entry->ulCapsAndSettings) & ATOM_Tonga_DISALLOW_ON_DC));

	power_state->pcie.lanes = 0;

	power_state->display.disableFrameModulation = false;
	power_state->display.limitRefreshrate = false;
	power_state->display.enableVariBright =
			(0 != (le32_to_cpu(state_entry->ulCapsAndSettings) & ATOM_Tonga_ENABLE_VARIBRIGHT));

	power_state->validation.supportedPowerLevels = 0;
	power_state->uvd_clocks.VCLK = 0;
	power_state->uvd_clocks.DCLK = 0;
	power_state->temperatures.min = 0;
	power_state->temperatures.max = 0;

	performance_level = &(tonga_ps->performance_levels
			[tonga_ps->performance_level_count++]);

	PP_ASSERT_WITH_CODE(
			(tonga_ps->performance_level_count < SMU72_MAX_LEVELS_GRAPHICS),
			"Performance levels exceeds SMC limit!",
			return -1);

	PP_ASSERT_WITH_CODE(
			(tonga_ps->performance_level_count <=
					hwmgr->platform_descriptor.hardwareActivityPerformanceLevels),
			"Performance levels exceeds Driver limit!",
			return -1);

	/* Performance levels are arranged from low to high. */
	performance_level->memory_clock =
				le32_to_cpu(mclk_dep_table->entries[state_entry->ucMemoryClockIndexLow].ulMclk);

	performance_level->engine_clock =
				le32_to_cpu(sclk_dep_table->entries[state_entry->ucEngineClockIndexLow].ulSclk);

	performance_level->pcie_gen = get_pcie_gen_support(
							data->pcie_gen_cap,
					     state_entry->ucPCIEGenLow);

	performance_level->pcie_lane = get_pcie_lane_support(
						    data->pcie_lane_cap,
					   state_entry->ucPCIELaneHigh);

	performance_level =
			&(tonga_ps->performance_levels[tonga_ps->performance_level_count++]);

	performance_level->memory_clock =
				le32_to_cpu(mclk_dep_table->entries[state_entry->ucMemoryClockIndexHigh].ulMclk);

	performance_level->engine_clock =
				le32_to_cpu(sclk_dep_table->entries[state_entry->ucEngineClockIndexHigh].ulSclk);

	performance_level->pcie_gen = get_pcie_gen_support(
							data->pcie_gen_cap,
					    state_entry->ucPCIEGenHigh);

	performance_level->pcie_lane = get_pcie_lane_support(
						    data->pcie_lane_cap,
					   state_entry->ucPCIELaneHigh);

	return 0;
}

static int tonga_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		    unsigned long entry_index, struct pp_power_state *ps)
{
	int result;
	struct tonga_power_state *tonga_ps;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	struct phm_ppt_v1_clock_voltage_dependency_table *dep_mclk_table =
					   table_info->vdd_dep_on_mclk;

	ps->hardware.magic = PhwTonga_Magic;

	tonga_ps = cast_phw_tonga_power_state(&(ps->hardware));

	result = tonga_get_powerplay_table_entry(hwmgr, entry_index, ps,
			tonga_get_pp_table_entry_callback_func);

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
	if (!ps->validation.disallowOnDC)
		tonga_ps->dc_compatible = true;

	if (ps->classification.flags & PP_StateClassificationFlag_ACPI)
		data->acpi_pcie_gen = tonga_ps->performance_levels[0].pcie_gen;
	else if (ps->classification.flags & PP_StateClassificationFlag_Boot) {
		if (data->bacos.best_match == 0xffff) {
			/* For V.I. use boot state as base BACO state */
			data->bacos.best_match = PP_StateClassificationFlag_Boot;
			data->bacos.performance_level = tonga_ps->performance_levels[0];
		}
	}

	tonga_ps->uvd_clocks.VCLK = ps->uvd_clocks.VCLK;
	tonga_ps->uvd_clocks.DCLK = ps->uvd_clocks.DCLK;

	if (!result) {
		uint32_t i;

		switch (ps->classification.ui_label) {
		case PP_StateUILabel_Performance:
			data->use_pcie_performance_levels = true;

			for (i = 0; i < tonga_ps->performance_level_count; i++) {
				if (data->pcie_gen_performance.max <
						tonga_ps->performance_levels[i].pcie_gen)
					data->pcie_gen_performance.max =
							tonga_ps->performance_levels[i].pcie_gen;

				if (data->pcie_gen_performance.min >
						tonga_ps->performance_levels[i].pcie_gen)
					data->pcie_gen_performance.min =
							tonga_ps->performance_levels[i].pcie_gen;

				if (data->pcie_lane_performance.max <
						tonga_ps->performance_levels[i].pcie_lane)
					data->pcie_lane_performance.max =
							tonga_ps->performance_levels[i].pcie_lane;

				if (data->pcie_lane_performance.min >
						tonga_ps->performance_levels[i].pcie_lane)
					data->pcie_lane_performance.min =
							tonga_ps->performance_levels[i].pcie_lane;
			}
			break;
		case PP_StateUILabel_Battery:
			data->use_pcie_power_saving_levels = true;

			for (i = 0; i < tonga_ps->performance_level_count; i++) {
				if (data->pcie_gen_power_saving.max <
						tonga_ps->performance_levels[i].pcie_gen)
					data->pcie_gen_power_saving.max =
							tonga_ps->performance_levels[i].pcie_gen;

				if (data->pcie_gen_power_saving.min >
						tonga_ps->performance_levels[i].pcie_gen)
					data->pcie_gen_power_saving.min =
							tonga_ps->performance_levels[i].pcie_gen;

				if (data->pcie_lane_power_saving.max <
						tonga_ps->performance_levels[i].pcie_lane)
					data->pcie_lane_power_saving.max =
							tonga_ps->performance_levels[i].pcie_lane;

				if (data->pcie_lane_power_saving.min >
						tonga_ps->performance_levels[i].pcie_lane)
					data->pcie_lane_power_saving.min =
							tonga_ps->performance_levels[i].pcie_lane;
			}
			break;
		default:
			break;
		}
	}
	return 0;
}

static void
tonga_print_current_perforce_level(struct pp_hwmgr *hwmgr, struct seq_file *m)
{
	uint32_t sclk, mclk, activity_percent;
	uint32_t offset;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	smum_send_msg_to_smc(hwmgr->smumgr, (PPSMC_Msg)(PPSMC_MSG_API_GetSclkFrequency));

	sclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);

	smum_send_msg_to_smc(hwmgr->smumgr, (PPSMC_Msg)(PPSMC_MSG_API_GetMclkFrequency));

	mclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);
	seq_printf(m, "\n [  mclk  ]: %u MHz\n\n [  sclk  ]: %u MHz\n", mclk/100, sclk/100);

	offset = data->soft_regs_start + offsetof(SMU72_SoftRegisters, AverageGraphicsActivity);
	activity_percent = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset);
	activity_percent += 0x80;
	activity_percent >>= 8;

	seq_printf(m, "\n [GPU load]: %u%%\n\n", activity_percent > 100 ? 100 : activity_percent);

	seq_printf(m, "uvd    %sabled\n", data->uvd_power_gated ? "dis" : "en");

	seq_printf(m, "vce    %sabled\n", data->vce_power_gated ? "dis" : "en");
}

static int tonga_find_dpm_states_clocks_in_dpm_table(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	const struct tonga_power_state *tonga_ps = cast_const_phw_tonga_power_state(states->pnew_state);
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct tonga_single_dpm_table *psclk_table = &(data->dpm_table.sclk_table);
	uint32_t sclk = tonga_ps->performance_levels[tonga_ps->performance_level_count-1].engine_clock;
	struct tonga_single_dpm_table *pmclk_table = &(data->dpm_table.mclk_table);
	uint32_t mclk = tonga_ps->performance_levels[tonga_ps->performance_level_count-1].memory_clock;
	struct PP_Clocks min_clocks = {0};
	uint32_t i;
	struct cgs_display_info info = {0};

	data->need_update_smu7_dpm_table = 0;

	for (i = 0; i < psclk_table->count; i++) {
		if (sclk == psclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= psclk_table->count)
		data->need_update_smu7_dpm_table |= DPMTABLE_OD_UPDATE_SCLK;
	else {
	/* TODO: Check SCLK in DAL's minimum clocks in case DeepSleep divider update is required.*/
		if(data->display_timing.min_clock_insr != min_clocks.engineClockInSR)
			data->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_SCLK;
	}

	for (i=0; i < pmclk_table->count; i++) {
		if (mclk == pmclk_table->dpm_levels[i].value)
			break;
	}

	if (i >= pmclk_table->count)
		data->need_update_smu7_dpm_table |= DPMTABLE_OD_UPDATE_MCLK;

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (data->display_timing.num_existing_displays != info.display_count)
		data->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_MCLK;

	return 0;
}

static uint16_t tonga_get_maximum_link_speed(struct pp_hwmgr *hwmgr, const struct tonga_power_state *hw_ps)
{
	uint32_t i;
	uint32_t sclk, max_sclk = 0;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct tonga_dpm_table *pdpm_table = &data->dpm_table;

	for (i = 0; i < hw_ps->performance_level_count; i++) {
		sclk = hw_ps->performance_levels[i].engine_clock;
		if (max_sclk < sclk)
			max_sclk = sclk;
	}

	for (i = 0; i < pdpm_table->sclk_table.count; i++) {
		if (pdpm_table->sclk_table.dpm_levels[i].value == max_sclk)
			return (uint16_t) ((i >= pdpm_table->pcie_speed_table.count) ?
					pdpm_table->pcie_speed_table.dpm_levels[pdpm_table->pcie_speed_table.count-1].value :
					pdpm_table->pcie_speed_table.dpm_levels[i].value);
	}

	return 0;
}

static int tonga_request_link_speed_change_before_state_change(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	const struct tonga_power_state *tonga_nps = cast_const_phw_tonga_power_state(states->pnew_state);
	const struct tonga_power_state *tonga_cps = cast_const_phw_tonga_power_state(states->pcurrent_state);

	uint16_t target_link_speed = tonga_get_maximum_link_speed(hwmgr, tonga_nps);
	uint16_t current_link_speed;

	if (data->force_pcie_gen == PP_PCIEGenInvalid)
		current_link_speed = tonga_get_maximum_link_speed(hwmgr, tonga_cps);
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
			data->force_pcie_gen = tonga_get_current_pcie_speed(hwmgr);
			break;
		}
	} else {
		if (target_link_speed < current_link_speed)
			data->pspp_notify_required = true;
	}

	return 0;
}

static int tonga_freeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {
		PP_ASSERT_WITH_CODE(
			true == tonga_is_dpm_running(hwmgr),
			"Trying to freeze SCLK DPM when DPM is disabled",
			);
		PP_ASSERT_WITH_CODE(
			0 == smum_send_msg_to_smc(hwmgr->smumgr,
					  PPSMC_MSG_SCLKDPM_FreezeLevel),
			"Failed to freeze SCLK DPM during FreezeSclkMclkDPM Function!",
			return -1);
	}

	if ((0 == data->mclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		 DPMTABLE_OD_UPDATE_MCLK)) {
		PP_ASSERT_WITH_CODE(true == tonga_is_dpm_running(hwmgr),
			"Trying to freeze MCLK DPM when DPM is disabled",
			);
		PP_ASSERT_WITH_CODE(
			0 == smum_send_msg_to_smc(hwmgr->smumgr,
							PPSMC_MSG_MCLKDPM_FreezeLevel),
			"Failed to freeze MCLK DPM during FreezeSclkMclkDPM Function!",
			return -1);
	}

	return 0;
}

static int tonga_populate_and_upload_sclk_mclk_dpm_levels(struct pp_hwmgr *hwmgr, const void *input)
{
	int result = 0;

	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	const struct tonga_power_state *tonga_ps = cast_const_phw_tonga_power_state(states->pnew_state);
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	uint32_t sclk = tonga_ps->performance_levels[tonga_ps->performance_level_count-1].engine_clock;
	uint32_t mclk = tonga_ps->performance_levels[tonga_ps->performance_level_count-1].memory_clock;
	struct tonga_dpm_table *pdpm_table = &data->dpm_table;

	struct tonga_dpm_table *pgolden_dpm_table = &data->golden_dpm_table;
	uint32_t dpm_count, clock_percent;
	uint32_t i;

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_SCLK) {
		pdpm_table->sclk_table.dpm_levels[pdpm_table->sclk_table.count-1].value = sclk;

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinACSupport) ||
		    phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinDCSupport)) {
		/* Need to do calculation based on the golden DPM table
		 * as the Heatmap GPU Clock axis is also based on the default values
		 */
			PP_ASSERT_WITH_CODE(
				(pgolden_dpm_table->sclk_table.dpm_levels[pgolden_dpm_table->sclk_table.count-1].value != 0),
				"Divide by 0!",
				return -1);
			dpm_count = pdpm_table->sclk_table.count < 2 ? 0 : pdpm_table->sclk_table.count-2;
			for (i = dpm_count; i > 1; i--) {
				if (sclk > pgolden_dpm_table->sclk_table.dpm_levels[pgolden_dpm_table->sclk_table.count-1].value) {
					clock_percent = ((sclk - pgolden_dpm_table->sclk_table.dpm_levels[pgolden_dpm_table->sclk_table.count-1].value)*100) /
							pgolden_dpm_table->sclk_table.dpm_levels[pgolden_dpm_table->sclk_table.count-1].value;

					pdpm_table->sclk_table.dpm_levels[i].value =
							pgolden_dpm_table->sclk_table.dpm_levels[i].value +
							(pgolden_dpm_table->sclk_table.dpm_levels[i].value * clock_percent)/100;

				} else if (pgolden_dpm_table->sclk_table.dpm_levels[pdpm_table->sclk_table.count-1].value > sclk) {
					clock_percent = ((pgolden_dpm_table->sclk_table.dpm_levels[pgolden_dpm_table->sclk_table.count-1].value - sclk)*100) /
								pgolden_dpm_table->sclk_table.dpm_levels[pgolden_dpm_table->sclk_table.count-1].value;

					pdpm_table->sclk_table.dpm_levels[i].value =
							pgolden_dpm_table->sclk_table.dpm_levels[i].value -
							(pgolden_dpm_table->sclk_table.dpm_levels[i].value * clock_percent)/100;
				} else
					pdpm_table->sclk_table.dpm_levels[i].value =
							pgolden_dpm_table->sclk_table.dpm_levels[i].value;
			}
		}
	}

	if (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK) {
		pdpm_table->mclk_table.dpm_levels[pdpm_table->mclk_table.count-1].value = mclk;

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinACSupport) ||
			phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinDCSupport)) {

			PP_ASSERT_WITH_CODE(
					(pgolden_dpm_table->mclk_table.dpm_levels[pgolden_dpm_table->mclk_table.count-1].value != 0),
					"Divide by 0!",
					return -1);
			dpm_count = pdpm_table->mclk_table.count < 2? 0 : pdpm_table->mclk_table.count-2;
			for (i = dpm_count; i > 1; i--) {
				if (mclk > pgolden_dpm_table->mclk_table.dpm_levels[pgolden_dpm_table->mclk_table.count-1].value) {
						clock_percent = ((mclk - pgolden_dpm_table->mclk_table.dpm_levels[pgolden_dpm_table->mclk_table.count-1].value)*100) /
								    pgolden_dpm_table->mclk_table.dpm_levels[pgolden_dpm_table->mclk_table.count-1].value;

						pdpm_table->mclk_table.dpm_levels[i].value =
										pgolden_dpm_table->mclk_table.dpm_levels[i].value +
										(pgolden_dpm_table->mclk_table.dpm_levels[i].value * clock_percent)/100;

				} else if (pgolden_dpm_table->mclk_table.dpm_levels[pdpm_table->mclk_table.count-1].value > mclk) {
						clock_percent = ((pgolden_dpm_table->mclk_table.dpm_levels[pgolden_dpm_table->mclk_table.count-1].value - mclk)*100) /
								    pgolden_dpm_table->mclk_table.dpm_levels[pgolden_dpm_table->mclk_table.count-1].value;

						pdpm_table->mclk_table.dpm_levels[i].value =
									pgolden_dpm_table->mclk_table.dpm_levels[i].value -
									(pgolden_dpm_table->mclk_table.dpm_levels[i].value * clock_percent)/100;
				} else
					pdpm_table->mclk_table.dpm_levels[i].value = pgolden_dpm_table->mclk_table.dpm_levels[i].value;
			}
		}
	}

	if (data->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK)) {
		result = tonga_populate_all_memory_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to populate SCLK during PopulateNewDPMClocksStates Function!",
			return result);
	}

	if (data->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_MCLK + DPMTABLE_UPDATE_MCLK)) {
		/*populate MCLK dpm table to SMU7 */
		result = tonga_populate_all_memory_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate MCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	return result;
}

static	int tonga_trim_single_dpm_states(struct pp_hwmgr *hwmgr,
			  struct tonga_single_dpm_table * pdpm_table,
			     uint32_t low_limit, uint32_t high_limit)
{
	uint32_t i;

	for (i = 0; i < pdpm_table->count; i++) {
		if ((pdpm_table->dpm_levels[i].value < low_limit) ||
		    (pdpm_table->dpm_levels[i].value > high_limit))
			pdpm_table->dpm_levels[i].enabled = false;
		else
			pdpm_table->dpm_levels[i].enabled = true;
	}
	return 0;
}

static int tonga_trim_dpm_states(struct pp_hwmgr *hwmgr, const struct tonga_power_state *hw_state)
{
	int result = 0;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	uint32_t high_limit_count;

	PP_ASSERT_WITH_CODE((hw_state->performance_level_count >= 1),
				"power state did not have any performance level",
				 return -1);

	high_limit_count = (1 == hw_state->performance_level_count) ? 0: 1;

	tonga_trim_single_dpm_states(hwmgr,
					&(data->dpm_table.sclk_table),
					hw_state->performance_levels[0].engine_clock,
					hw_state->performance_levels[high_limit_count].engine_clock);

	tonga_trim_single_dpm_states(hwmgr,
						&(data->dpm_table.mclk_table),
						hw_state->performance_levels[0].memory_clock,
						hw_state->performance_levels[high_limit_count].memory_clock);

	return result;
}

static int tonga_generate_dpm_level_enable_mask(struct pp_hwmgr *hwmgr, const void *input)
{
	int result;
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	const struct tonga_power_state *tonga_ps = cast_const_phw_tonga_power_state(states->pnew_state);

	result = tonga_trim_dpm_states(hwmgr, tonga_ps);
	if (0 != result)
		return result;

	data->dpm_level_enable_mask.sclk_dpm_enable_mask = tonga_get_dpm_level_enable_mask_value(&data->dpm_table.sclk_table);
	data->dpm_level_enable_mask.mclk_dpm_enable_mask = tonga_get_dpm_level_enable_mask_value(&data->dpm_table.mclk_table);
	data->last_mclk_dpm_enable_mask = data->dpm_level_enable_mask.mclk_dpm_enable_mask;
	if (data->uvd_enabled)
		data->dpm_level_enable_mask.mclk_dpm_enable_mask &= 0xFFFFFFFE;

	data->dpm_level_enable_mask.pcie_dpm_enable_mask = tonga_get_dpm_level_enable_mask_value(&data->dpm_table.pcie_speed_table);

	return 0;
}

int tonga_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable ?
				  (PPSMC_Msg)PPSMC_MSG_VCEDPM_Enable :
				  (PPSMC_Msg)PPSMC_MSG_VCEDPM_Disable);
}

int tonga_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable ?
				  (PPSMC_Msg)PPSMC_MSG_UVDDPM_Enable :
				  (PPSMC_Msg)PPSMC_MSG_UVDDPM_Disable);
}

int tonga_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *ptable_information = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (!bgate) {
		data->smc_state_table.UvdBootLevel = (uint8_t) (ptable_information->mm_dep_table->count - 1);
		mm_boot_level_offset = data->dpm_table_start + offsetof(SMU72_Discrete_DpmTable, UvdBootLevel);
		mm_boot_level_offset /= 4;
		mm_boot_level_offset *= 4;
		mm_boot_level_value = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, mm_boot_level_offset);
		mm_boot_level_value &= 0x00FFFFFF;
		mm_boot_level_value |= data->smc_state_table.UvdBootLevel << 24;
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

		if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_UVDDPM) ||
		    phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState))
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_UVDDPM_SetEnabledMask,
						(uint32_t)(1 << data->smc_state_table.UvdBootLevel));
	}

	return tonga_enable_disable_uvd_dpm(hwmgr, !bgate);
}

int tonga_update_vce_dpm(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	const struct tonga_power_state *tonga_nps = cast_const_phw_tonga_power_state(states->pnew_state);
	const struct tonga_power_state *tonga_cps = cast_const_phw_tonga_power_state(states->pcurrent_state);

	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (tonga_nps->vce_clocks.EVCLK > 0 && (tonga_cps == NULL || tonga_cps->vce_clocks.EVCLK == 0)) {
		data->smc_state_table.VceBootLevel = (uint8_t) (pptable_info->mm_dep_table->count - 1);

		mm_boot_level_offset = data->dpm_table_start + offsetof(SMU72_Discrete_DpmTable, VceBootLevel);
		mm_boot_level_offset /= 4;
		mm_boot_level_offset *= 4;
		mm_boot_level_value = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, mm_boot_level_offset);
		mm_boot_level_value &= 0xFF00FFFF;
		mm_boot_level_value |= data->smc_state_table.VceBootLevel << 16;
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState))
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_VCEDPM_SetEnabledMask,
				(uint32_t)(1 << data->smc_state_table.VceBootLevel));

		tonga_enable_disable_vce_dpm(hwmgr, true);
	} else if (tonga_nps->vce_clocks.EVCLK == 0 && tonga_cps != NULL && tonga_cps->vce_clocks.EVCLK > 0)
		tonga_enable_disable_vce_dpm(hwmgr, false);

	return 0;
}

static int tonga_update_and_upload_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	uint32_t address;
	int32_t result;

	if (0 == (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK))
		return 0;


	memset(&data->mc_reg_table, 0, sizeof(SMU72_Discrete_MCRegisters));

	result = tonga_convert_mc_reg_table_to_smc(hwmgr, &(data->mc_reg_table));

	if(result != 0)
		return result;


	address = data->mc_reg_table_start + (uint32_t)offsetof(SMU72_Discrete_MCRegisters, data[0]);

	return  tonga_copy_bytes_to_smc(hwmgr->smumgr, address,
				 (uint8_t *)&data->mc_reg_table.data[0],
				sizeof(SMU72_Discrete_MCRegisterSet) * data->dpm_table.mclk_table.count,
				data->sram_end);
}

static int tonga_program_memory_timing_parameters_conditionally(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	if (data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_OD_UPDATE_MCLK))
		return tonga_program_memory_timing_parameters(hwmgr);

	return 0;
}

static int tonga_unfreeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {

		PP_ASSERT_WITH_CODE(true == tonga_is_dpm_running(hwmgr),
			"Trying to Unfreeze SCLK DPM when DPM is disabled",
			);
		PP_ASSERT_WITH_CODE(
			 0 == smum_send_msg_to_smc(hwmgr->smumgr,
					 PPSMC_MSG_SCLKDPM_UnfreezeLevel),
			"Failed to unfreeze SCLK DPM during UnFreezeSclkMclkDPM Function!",
			return -1);
	}

	if ((0 == data->mclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)) {

		PP_ASSERT_WITH_CODE(
				true == tonga_is_dpm_running(hwmgr),
				"Trying to Unfreeze MCLK DPM when DPM is disabled",
				);
		PP_ASSERT_WITH_CODE(
			 0 == smum_send_msg_to_smc(hwmgr->smumgr,
					 PPSMC_MSG_SCLKDPM_UnfreezeLevel),
		    "Failed to unfreeze MCLK DPM during UnFreezeSclkMclkDPM Function!",
		    return -1);
	}

	data->need_update_smu7_dpm_table = 0;

	return 0;
}

static int tonga_notify_link_speed_change_after_state_change(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	const struct tonga_power_state *tonga_ps = cast_const_phw_tonga_power_state(states->pnew_state);
	uint16_t target_link_speed = tonga_get_maximum_link_speed(hwmgr, tonga_ps);
	uint8_t  request;

	if (data->pspp_notify_required  ||
	    data->pcie_performance_request) {
		if (target_link_speed == PP_PCIEGen3)
			request = PCIE_PERF_REQ_GEN3;
		else if (target_link_speed == PP_PCIEGen2)
			request = PCIE_PERF_REQ_GEN2;
		else
			request = PCIE_PERF_REQ_GEN1;

		if(request == PCIE_PERF_REQ_GEN1 && tonga_get_current_pcie_speed(hwmgr) > 0) {
			data->pcie_performance_request = false;
			return 0;
		}

		if (0 != acpi_pcie_perf_request(hwmgr->device, request, false)) {
			if (PP_PCIEGen2 == target_link_speed)
				printk("PSPP request to switch to Gen2 from Gen3 Failed!");
			else
				printk("PSPP request to switch to Gen1 from Gen2 Failed!");
		}
	}

	data->pcie_performance_request = false;
	return 0;
}

static int tonga_set_power_state_tasks(struct pp_hwmgr *hwmgr, const void *input)
{
	int tmp_result, result = 0;

	tmp_result = tonga_find_dpm_states_clocks_in_dpm_table(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to find DPM states clocks in DPM table!", result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result = tonga_request_link_speed_change_before_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to request link speed change before state change!", result = tmp_result);
	}

	tmp_result = tonga_freeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to freeze SCLK MCLK DPM!", result = tmp_result);

	tmp_result = tonga_populate_and_upload_sclk_mclk_dpm_levels(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to populate and upload SCLK MCLK DPM levels!", result = tmp_result);

	tmp_result = tonga_generate_dpm_level_enable_mask(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to generate DPM level enabled mask!", result = tmp_result);

	tmp_result = tonga_update_vce_dpm(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to update VCE DPM!", result = tmp_result);

	tmp_result = tonga_update_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to update SCLK threshold!", result = tmp_result);

	tmp_result = tonga_update_and_upload_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to upload MC reg table!", result = tmp_result);

	tmp_result = tonga_program_memory_timing_parameters_conditionally(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to program memory timing parameters!", result = tmp_result);

	tmp_result = tonga_unfreeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to unfreeze SCLK MCLK DPM!", result = tmp_result);

	tmp_result = tonga_upload_dpm_level_enable_mask(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to upload DPM level enabled mask!", result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result = tonga_notify_link_speed_change_after_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to notify link speed change after state change!", result = tmp_result);
	}

	return result;
}

/**
*  Set maximum target operating fan output PWM
*
* @param    pHwMgr:  the address of the powerplay hardware manager.
* @param    usMaxFanPwm:  max operating fan PWM in percents
* @return   The response that came from the SMC.
*/
static int tonga_set_max_fan_pwm_output(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_pwm)
{
	hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanPWM = us_max_fan_pwm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return (0 == smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, PPSMC_MSG_SetFanPwmMax, us_max_fan_pwm) ? 0 : -1);
}

int tonga_notify_smc_display_config_after_ps_adjustment(struct pp_hwmgr *hwmgr)
{
	uint32_t num_active_displays = 0;
	struct cgs_display_info info = {0};
	info.mode_info = NULL;

	cgs_get_active_displays_info(hwmgr->device, &info);

	num_active_displays = info.display_count;

	if (num_active_displays > 1)  /* to do && (pHwMgr->pPECI->displayConfiguration.bMultiMonitorInSync != TRUE)) */
		tonga_notify_smc_display_change(hwmgr, false);
	else
		tonga_notify_smc_display_change(hwmgr, true);

	return 0;
}

/**
* Programs the display gap
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always OK
*/
int tonga_program_display_gap(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	uint32_t num_active_displays = 0;
	uint32_t display_gap = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_DISPLAY_GAP_CNTL);
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

	display_gap = PHM_SET_FIELD(display_gap, CG_DISPLAY_GAP_CNTL, DISP_GAP, (num_active_displays > 0)? DISPLAY_GAP_VBLANK_OR_WM : DISPLAY_GAP_IGNORE);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_DISPLAY_GAP_CNTL, display_gap);

	ref_clock = mode_info.ref_clock;
	refresh_rate = mode_info.refresh_rate;

	if(0 == refresh_rate)
		refresh_rate = 60;

	frame_time_in_us = 1000000 / refresh_rate;

	pre_vbi_time_in_us = frame_time_in_us - 200 - mode_info.vblank_time_us;
	display_gap2 = pre_vbi_time_in_us * (ref_clock / 100);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_DISPLAY_GAP_CNTL2, display_gap2);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, data->soft_regs_start + offsetof(SMU72_SoftRegisters, PreVBlankGap), 0x64);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, data->soft_regs_start + offsetof(SMU72_SoftRegisters, VBlankTimeout), (frame_time_in_us - pre_vbi_time_in_us));

	if (num_active_displays == 1)
		tonga_notify_smc_display_change(hwmgr, true);

	return 0;
}

int tonga_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{

	tonga_program_display_gap(hwmgr);

	/* to do PhwTonga_CacUpdateDisplayConfiguration(pHwMgr); */
	return 0;
}

/**
*  Set maximum target operating fan output RPM
*
* @param    pHwMgr:  the address of the powerplay hardware manager.
* @param    usMaxFanRpm:  max operating fan RPM value.
* @return   The response that came from the SMC.
*/
static int tonga_set_max_fan_rpm_output(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_pwm)
{
	hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM = us_max_fan_pwm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return (0 == smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, PPSMC_MSG_SetFanRpmMax, us_max_fan_pwm) ? 0 : -1);
}

uint32_t tonga_get_xclk(struct pp_hwmgr *hwmgr)
{
	uint32_t reference_clock;
	uint32_t tc;
	uint32_t divide;

	ATOM_FIRMWARE_INFO *fw_info;
	uint16_t size;
	uint8_t frev, crev;
	int index = GetIndexIntoMasterTable(DATA, FirmwareInfo);

	tc = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_CLKPIN_CNTL_2, MUX_TCLK_TO_XCLK);

	if (tc)
		return TCLK;

	fw_info = (ATOM_FIRMWARE_INFO *)cgs_atom_get_data_table(hwmgr->device, index,
						  &size, &frev, &crev);

	if (!fw_info)
		return 0;

	reference_clock = le16_to_cpu(fw_info->usMinPixelClockPLL_Output);

	divide = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_CLKPIN_CNTL, XTALIN_DIVIDE);

	if (0 != divide)
		return reference_clock / 4;

	return reference_clock;
}

int tonga_dpm_set_interrupt_state(void *private_data,
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
			cg_thermal_int = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		} else {
			cg_thermal_int = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		}
		break;

	case AMD_THERMAL_IRQ_HIGH_TO_LOW:
		if (enabled) {
			cg_thermal_int = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int |= CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		} else {
			cg_thermal_int = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT);
			cg_thermal_int &= ~CG_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_THERMAL_INT, cg_thermal_int);
		}
		break;
	default:
		break;
	}
	return 0;
}

int tonga_register_internal_thermal_interrupt(struct pp_hwmgr *hwmgr,
					const void *thermal_interrupt_info)
{
	int result;
	const struct pp_interrupt_registration_info *info =
			(const struct pp_interrupt_registration_info *)thermal_interrupt_info;

	if (info == NULL)
		return -EINVAL;

	result = cgs_add_irq_source(hwmgr->device, 230, AMD_THERMAL_IRQ_LAST,
				tonga_dpm_set_interrupt_state,
				info->call_back, info->context);

	if (result)
		return -EINVAL;

	result = cgs_add_irq_source(hwmgr->device, 231, AMD_THERMAL_IRQ_LAST,
				tonga_dpm_set_interrupt_state,
				info->call_back, info->context);

	if (result)
		return -EINVAL;

	return 0;
}

bool tonga_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	bool is_update_required = false;
	struct cgs_display_info info = {0,0,NULL};

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (data->display_timing.num_existing_displays != info.display_count)
		is_update_required = true;
/* TO DO NEED TO GET DEEP SLEEP CLOCK FROM DAL
	if (phm_cap_enabled(hwmgr->hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		cgs_get_min_clock_settings(hwmgr->device, &min_clocks);
		if(min_clocks.engineClockInSR != data->display_timing.minClockInSR)
			is_update_required = true;
*/
	return is_update_required;
}

static inline bool tonga_are_power_levels_equal(const struct tonga_performance_level *pl1,
							   const struct tonga_performance_level *pl2)
{
	return ((pl1->memory_clock == pl2->memory_clock) &&
		  (pl1->engine_clock == pl2->engine_clock) &&
		  (pl1->pcie_gen == pl2->pcie_gen) &&
		  (pl1->pcie_lane == pl2->pcie_lane));
}

int tonga_check_states_equal(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *pstate1, const struct pp_hw_power_state *pstate2, bool *equal)
{
	const struct tonga_power_state *psa = cast_const_phw_tonga_power_state(pstate1);
	const struct tonga_power_state *psb = cast_const_phw_tonga_power_state(pstate2);
	int i;

	if (equal == NULL || psa == NULL || psb == NULL)
		return -EINVAL;

	/* If the two states don't even have the same number of performance levels they cannot be the same state. */
	if (psa->performance_level_count != psb->performance_level_count) {
		*equal = false;
		return 0;
	}

	for (i = 0; i < psa->performance_level_count; i++) {
		if (!tonga_are_power_levels_equal(&(psa->performance_levels[i]), &(psb->performance_levels[i]))) {
			/* If we have found even one performance level pair that is different the states are different. */
			*equal = false;
			return 0;
		}
	}

	/* If all performance levels are the same try to use the UVD clocks to break the tie.*/
	*equal = ((psa->uvd_clocks.VCLK == psb->uvd_clocks.VCLK) && (psa->uvd_clocks.DCLK == psb->uvd_clocks.DCLK));
	*equal &= ((psa->vce_clocks.EVCLK == psb->vce_clocks.EVCLK) && (psa->vce_clocks.ECCLK == psb->vce_clocks.ECCLK));
	*equal &= (psa->sclk_threshold == psb->sclk_threshold);
	*equal &= (psa->acp_clk == psb->acp_clk);

	return 0;
}

static int tonga_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	if (mode) {
		/* stop auto-manage */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl))
			tonga_fan_ctrl_stop_smc_fan_control(hwmgr);
		tonga_fan_ctrl_set_static_mode(hwmgr, mode);
	} else
		/* restart auto-manage */
		tonga_fan_ctrl_reset_fan_speed_to_default(hwmgr);

	return 0;
}

static int tonga_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->fan_ctrl_is_in_default_mode)
		return hwmgr->fan_ctrl_default_mode;
	else
		return PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_FDO_CTRL2, FDO_PWM_MODE);
}

static int tonga_get_pp_table(struct pp_hwmgr *hwmgr, char **table)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	*table = (char *)&data->smc_state_table;

	return sizeof(struct SMU72_Discrete_DpmTable);
}

static int tonga_set_pp_table(struct pp_hwmgr *hwmgr, const char *buf, size_t size)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	void *table = (void *)&data->smc_state_table;

	memcpy(table, buf, size);

	return 0;
}

static int tonga_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, int level)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	if (hwmgr->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	switch (type) {
	case PP_SCLK:
		if (!data->sclk_dpm_key_disabled)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SCLKDPM_SetEnabledMask,
					(1 << level));
		break;
	case PP_MCLK:
		if (!data->mclk_dpm_key_disabled)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_MCLKDPM_SetEnabledMask,
					(1 << level));
		break;
	case PP_PCIE:
		if (!data->pcie_dpm_key_disabled)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_PCIeDPM_ForceLevel,
					(1 << level));
		break;
	default:
		break;
	}

	return 0;
}

static int tonga_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct tonga_single_dpm_table *sclk_table = &(data->dpm_table.sclk_table);
	struct tonga_single_dpm_table *mclk_table = &(data->dpm_table.mclk_table);
	struct tonga_single_dpm_table *pcie_table = &(data->dpm_table.pcie_speed_table);
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
		pcie_speed = tonga_get_current_pcie_speed(hwmgr);
		for (i = 0; i < pcie_table->count; i++) {
			if (pcie_speed != pcie_table->dpm_levels[i].value)
				continue;
			break;
		}
		now = i;

		for (i = 0; i < pcie_table->count; i++)
			size += sprintf(buf + size, "%d: %s %s\n", i,
					(pcie_table->dpm_levels[i].value == 0) ? "2.5GB, x8" :
					(pcie_table->dpm_levels[i].value == 1) ? "5.0GB, x16" :
					(pcie_table->dpm_levels[i].value == 2) ? "8.0GB, x16" : "",
					(i == now) ? "*" : "");
		break;
	default:
		break;
	}
	return size;
}

static const struct pp_hwmgr_func tonga_hwmgr_funcs = {
	.backend_init = &tonga_hwmgr_backend_init,
	.backend_fini = &tonga_hwmgr_backend_fini,
	.asic_setup = &tonga_setup_asic_task,
	.dynamic_state_management_enable = &tonga_enable_dpm_tasks,
	.apply_state_adjust_rules = tonga_apply_state_adjust_rules,
	.force_dpm_level = &tonga_force_dpm_level,
	.power_state_set = tonga_set_power_state_tasks,
	.get_power_state_size = tonga_get_power_state_size,
	.get_mclk = tonga_dpm_get_mclk,
	.get_sclk = tonga_dpm_get_sclk,
	.patch_boot_state = tonga_dpm_patch_boot_state,
	.get_pp_table_entry = tonga_get_pp_table_entry,
	.get_num_of_pp_table_entries = tonga_get_number_of_powerplay_table_entries,
	.print_current_perforce_level = tonga_print_current_perforce_level,
	.powerdown_uvd = tonga_phm_powerdown_uvd,
	.powergate_uvd = tonga_phm_powergate_uvd,
	.powergate_vce = tonga_phm_powergate_vce,
	.disable_clock_power_gating = tonga_phm_disable_clock_power_gating,
	.notify_smc_display_config_after_ps_adjustment = tonga_notify_smc_display_config_after_ps_adjustment,
	.display_config_changed = tonga_display_configuration_changed_task,
	.set_max_fan_pwm_output = tonga_set_max_fan_pwm_output,
	.set_max_fan_rpm_output = tonga_set_max_fan_rpm_output,
	.get_temperature = tonga_thermal_get_temperature,
	.stop_thermal_controller = tonga_thermal_stop_thermal_controller,
	.get_fan_speed_info = tonga_fan_ctrl_get_fan_speed_info,
	.get_fan_speed_percent = tonga_fan_ctrl_get_fan_speed_percent,
	.set_fan_speed_percent = tonga_fan_ctrl_set_fan_speed_percent,
	.reset_fan_speed_to_default = tonga_fan_ctrl_reset_fan_speed_to_default,
	.get_fan_speed_rpm = tonga_fan_ctrl_get_fan_speed_rpm,
	.set_fan_speed_rpm = tonga_fan_ctrl_set_fan_speed_rpm,
	.uninitialize_thermal_controller = tonga_thermal_ctrl_uninitialize_thermal_controller,
	.register_internal_thermal_interrupt = tonga_register_internal_thermal_interrupt,
	.check_smc_update_required_for_display_configuration = tonga_check_smc_update_required_for_display_configuration,
	.check_states_equal = tonga_check_states_equal,
	.set_fan_control_mode = tonga_set_fan_control_mode,
	.get_fan_control_mode = tonga_get_fan_control_mode,
	.get_pp_table = tonga_get_pp_table,
	.set_pp_table = tonga_set_pp_table,
	.force_clock_level = tonga_force_clock_level,
	.print_clock_levels = tonga_print_clock_levels,
};

int tonga_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	tonga_hwmgr  *data;

	data = kzalloc (sizeof(tonga_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	memset(data, 0x00, sizeof(tonga_hwmgr));

	hwmgr->backend = data;
	hwmgr->hwmgr_func = &tonga_hwmgr_funcs;
	hwmgr->pptable_func = &tonga_pptable_funcs;
	pp_tonga_thermal_initialize(hwmgr);
	return 0;
}

