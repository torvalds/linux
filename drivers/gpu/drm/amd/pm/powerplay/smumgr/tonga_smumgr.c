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
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include "smumgr.h"
#include "tonga_smumgr.h"
#include "smu_ucode_xfer_vi.h"
#include "tonga_ppsmc.h"
#include "smu/smu_7_1_2_d.h"
#include "smu/smu_7_1_2_sh_mask.h"
#include "cgs_common.h"
#include "smu7_smumgr.h"

#include "smu7_dyn_defaults.h"

#include "smu7_hwmgr.h"
#include "hardwaremanager.h"
#include "ppatomctrl.h"

#include "atombios.h"

#include "pppcielanes.h"
#include "pp_endian.h"

#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#define POWERTUNE_DEFAULT_SET_MAX    1
#define MC_CG_ARB_FREQ_F1           0x0b
#define VDDC_VDDCI_DELTA            200


static const struct tonga_pt_defaults tonga_power_tune_data_set_array[POWERTUNE_DEFAULT_SET_MAX] = {
/* sviLoadLIneEn, SviLoadLineVddC, TDC_VDDC_ThrottleReleaseLimitPerc,  TDC_MAWt,
 * TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac,        BAPM_TEMP_GRADIENT
 */
	{1,               0xF,             0xFD,                0x19,
	 5,               45,                 0,              0xB0000,
	 {0x79, 0x253, 0x25D, 0xAE, 0x72, 0x80, 0x83, 0x86, 0x6F, 0xC8,
		0xC9, 0xC9, 0x2F, 0x4D, 0x61},
	 {0x17C, 0x172, 0x180, 0x1BC, 0x1B3, 0x1BD, 0x206, 0x200, 0x203,
		0x25D, 0x25A, 0x255, 0x2C3, 0x2C5, 0x2B4}
	},
};

/* [Fmin, Fmax, LDO_REFSEL, USE_FOR_LOW_FREQ] */
static const uint16_t tonga_clock_stretcher_lookup_table[2][4] = {
	{600, 1050, 3, 0},
	{600, 1050, 6, 1}
};

/* [FF, SS] type, [] 4 voltage ranges,
 * and [Floor Freq, Boundary Freq, VID min , VID max]
 */
static const uint32_t tonga_clock_stretcher_ddt_table[2][4][4] = {
	{ {265, 529, 120, 128}, {325, 650, 96, 119}, {430, 860, 32, 95}, {0, 0, 0, 31} },
	{ {275, 550, 104, 112}, {319, 638, 96, 103}, {360, 720, 64, 95}, {384, 768, 32, 63} }
};

/* [Use_For_Low_freq] value, [0%, 5%, 10%, 7.14%, 14.28%, 20%] */
static const uint8_t tonga_clock_stretch_amount_conversion[2][6] = {
	{0, 1, 3, 2, 4, 5},
	{0, 2, 4, 5, 6, 5}
};

static int tonga_start_in_protection_mode(struct pp_hwmgr *hwmgr)
{
	int result;

	/* Assert reset */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(hwmgr);
	if (result)
		return result;

	/* Clear status */
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
		ixSMU_STATUS, 0);

	/* Enable clock */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Set SMU Auto Start */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SMU_INPUT_DATA, AUTO_START, 1);

	/* Clear firmware interrupt enable flag */
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
		ixFIRMWARE_FLAGS, 0);

	PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, SMC_IND,
		RCU_UC_EVENTS, INTERRUPTS_ENABLED, 1);

	/**
	 * Call Test SMU message with 0x20000 offset to trigger SMU start
	 */
	smu7_send_msg_to_smc_offset(hwmgr);

	/* Wait for done bit to be set */
	PHM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(hwmgr, SMC_IND,
		SMU_STATUS, SMU_DONE, 0);

	/* Check pass/failed indicator */
	if (1 != PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device,
				CGS_IND_REG__SMC, SMU_STATUS, SMU_PASS)) {
		pr_err("SMU Firmware start failed\n");
		return -EINVAL;
	}

	/* Wait for firmware to initialize */
	PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, SMC_IND,
		FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return 0;
}

static int tonga_start_in_non_protection_mode(struct pp_hwmgr *hwmgr)
{
	int result = 0;

	/* wait for smc boot up */
	PHM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(hwmgr, SMC_IND,
		RCU_UC_EVENTS, boot_seq_done, 0);

	/*Clear firmware interrupt enable flag*/
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
		ixFIRMWARE_FLAGS, 0);


	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(hwmgr);

	if (result != 0)
		return result;

	/* Set smc instruct start point at 0x0 */
	smu7_program_jump_on_start(hwmgr);


	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/*De-assert reset*/
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */
	PHM_WAIT_VFPF_INDIRECT_FIELD(hwmgr, SMC_IND,
		FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int tonga_start_smu(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *priv = hwmgr->smu_backend;
	int result;

	/* Only start SMC if SMC RAM is not running */
	if (!smu7_is_smc_ram_running(hwmgr) && hwmgr->not_vf) {
		/*Check if SMU is running in protected mode*/
		if (0 == PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
					SMU_FIRMWARE, SMU_MODE)) {
			result = tonga_start_in_non_protection_mode(hwmgr);
			if (result)
				return result;
		} else {
			result = tonga_start_in_protection_mode(hwmgr);
			if (result)
				return result;
		}
	}

	/* Setup SoftRegsStart here to visit the register UcodeLoadStatus
	 * to check fw loading state
	 */
	smu7_read_smc_sram_dword(hwmgr,
			SMU72_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU72_Firmware_Header, SoftRegisters),
			&(priv->smu7_data.soft_regs_start), 0x40000);

	result = smu7_request_smu_load_fw(hwmgr);

	return result;
}

static int tonga_smu_init(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *tonga_priv;

	tonga_priv = kzalloc(sizeof(struct tonga_smumgr), GFP_KERNEL);
	if (tonga_priv == NULL)
		return -ENOMEM;

	hwmgr->smu_backend = tonga_priv;

	if (smu7_init(hwmgr)) {
		kfree(tonga_priv);
		return -EINVAL;
	}

	return 0;
}


static int tonga_get_dependency_volt_by_clk(struct pp_hwmgr *hwmgr,
	phm_ppt_v1_clock_voltage_dependency_table *allowed_clock_voltage_table,
	uint32_t clock, SMU_VoltageLevel *voltage, uint32_t *mvdd)
{
	uint32_t i = 0;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info =
			   (struct phm_ppt_v1_information *)(hwmgr->pptable);

	/* clock - voltage dependency table is empty table */
	if (allowed_clock_voltage_table->count == 0)
		return -EINVAL;

	for (i = 0; i < allowed_clock_voltage_table->count; i++) {
		/* find first sclk bigger than request */
		if (allowed_clock_voltage_table->entries[i].clk >= clock) {
			voltage->VddGfx = phm_get_voltage_index(
					pptable_info->vddgfx_lookup_table,
				allowed_clock_voltage_table->entries[i].vddgfx);
			voltage->Vddc = phm_get_voltage_index(
						pptable_info->vddc_lookup_table,
				  allowed_clock_voltage_table->entries[i].vddc);

			if (allowed_clock_voltage_table->entries[i].vddci)
				voltage->Vddci =
					phm_get_voltage_id(&data->vddci_voltage_table, allowed_clock_voltage_table->entries[i].vddci);
			else
				voltage->Vddci =
					phm_get_voltage_id(&data->vddci_voltage_table,
						allowed_clock_voltage_table->entries[i].vddc - VDDC_VDDCI_DELTA);


			if (allowed_clock_voltage_table->entries[i].mvdd)
				*mvdd = (uint32_t) allowed_clock_voltage_table->entries[i].mvdd;

			voltage->Phases = 1;
			return 0;
		}
	}

	/* sclk is bigger than max sclk in the dependence table */
	voltage->VddGfx = phm_get_voltage_index(pptable_info->vddgfx_lookup_table,
		allowed_clock_voltage_table->entries[i-1].vddgfx);
	voltage->Vddc = phm_get_voltage_index(pptable_info->vddc_lookup_table,
		allowed_clock_voltage_table->entries[i-1].vddc);

	if (allowed_clock_voltage_table->entries[i-1].vddci)
		voltage->Vddci = phm_get_voltage_id(&data->vddci_voltage_table,
			allowed_clock_voltage_table->entries[i-1].vddci);

	if (allowed_clock_voltage_table->entries[i-1].mvdd)
		*mvdd = (uint32_t) allowed_clock_voltage_table->entries[i-1].mvdd;

	return 0;
}

static int tonga_populate_smc_vddc_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	unsigned int count;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		table->VddcLevelCount = data->vddc_voltage_table.count;
		for (count = 0; count < table->VddcLevelCount; count++) {
			table->VddcTable[count] =
				PP_HOST_TO_SMC_US(data->vddc_voltage_table.entries[count].value * VOLTAGE_SCALE);
		}
		CONVERT_FROM_HOST_TO_SMC_UL(table->VddcLevelCount);
	}
	return 0;
}

static int tonga_populate_smc_vdd_gfx_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	unsigned int count;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_gfx_control) {
		table->VddGfxLevelCount = data->vddgfx_voltage_table.count;
		for (count = 0; count < data->vddgfx_voltage_table.count; count++) {
			table->VddGfxTable[count] =
				PP_HOST_TO_SMC_US(data->vddgfx_voltage_table.entries[count].value * VOLTAGE_SCALE);
		}
		CONVERT_FROM_HOST_TO_SMC_UL(table->VddGfxLevelCount);
	}
	return 0;
}

static int tonga_populate_smc_vdd_ci_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t count;

	table->VddciLevelCount = data->vddci_voltage_table.count;
	for (count = 0; count < table->VddciLevelCount; count++) {
		if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control) {
			table->VddciTable[count] =
				PP_HOST_TO_SMC_US(data->vddci_voltage_table.entries[count].value * VOLTAGE_SCALE);
		} else if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
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

static int tonga_populate_smc_mvdd_table(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t count;

	if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
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
		table->SmioMask2 = data->mvdd_voltage_table.mask_low;

		CONVERT_FROM_HOST_TO_SMC_UL(table->MvddLevelCount);
	}

	return 0;
}

static int tonga_populate_cac_tables(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	uint32_t count;
	uint8_t index = 0;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_voltage_lookup_table *vddgfx_lookup_table =
					   pptable_info->vddgfx_lookup_table;
	struct phm_ppt_v1_voltage_lookup_table *vddc_lookup_table =
						pptable_info->vddc_lookup_table;

	/* table is already swapped, so in order to use the value from it
	 * we need to swap it back.
	 */
	uint32_t vddc_level_count = PP_SMC_TO_HOST_UL(table->VddcLevelCount);
	uint32_t vddgfx_level_count = PP_SMC_TO_HOST_UL(table->VddGfxLevelCount);

	for (count = 0; count < vddc_level_count; count++) {
		/* We are populating vddc CAC data to BapmVddc table in split and merged mode */
		index = phm_get_voltage_index(vddc_lookup_table,
			data->vddc_voltage_table.entries[count].value);
		table->BapmVddcVidLoSidd[count] =
			convert_to_vid(vddc_lookup_table->entries[index].us_cac_low);
		table->BapmVddcVidHiSidd[count] =
			convert_to_vid(vddc_lookup_table->entries[index].us_cac_mid);
		table->BapmVddcVidHiSidd2[count] =
			convert_to_vid(vddc_lookup_table->entries[index].us_cac_high);
	}

	if (data->vdd_gfx_control == SMU7_VOLTAGE_CONTROL_BY_SVID2) {
		/* We are populating vddgfx CAC data to BapmVddgfx table in split mode */
		for (count = 0; count < vddgfx_level_count; count++) {
			index = phm_get_voltage_index(vddgfx_lookup_table,
				convert_to_vid(vddgfx_lookup_table->entries[index].us_cac_mid));
			table->BapmVddGfxVidHiSidd2[count] =
				convert_to_vid(vddgfx_lookup_table->entries[index].us_cac_high);
		}
	} else {
		for (count = 0; count < vddc_level_count; count++) {
			index = phm_get_voltage_index(vddc_lookup_table,
				data->vddc_voltage_table.entries[count].value);
			table->BapmVddGfxVidLoSidd[count] =
				convert_to_vid(vddc_lookup_table->entries[index].us_cac_low);
			table->BapmVddGfxVidHiSidd[count] =
				convert_to_vid(vddc_lookup_table->entries[index].us_cac_mid);
			table->BapmVddGfxVidHiSidd2[count] =
				convert_to_vid(vddc_lookup_table->entries[index].us_cac_high);
		}
	}

	return 0;
}

static int tonga_populate_smc_voltage_tables(struct pp_hwmgr *hwmgr,
	SMU72_Discrete_DpmTable *table)
{
	int result;

	result = tonga_populate_smc_vddc_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
			"can not populate VDDC voltage table to SMC",
			return -EINVAL);

	result = tonga_populate_smc_vdd_ci_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
			"can not populate VDDCI voltage table to SMC",
			return -EINVAL);

	result = tonga_populate_smc_vdd_gfx_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
			"can not populate VDDGFX voltage table to SMC",
			return -EINVAL);

	result = tonga_populate_smc_mvdd_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
			"can not populate MVDD voltage table to SMC",
			return -EINVAL);

	result = tonga_populate_cac_tables(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
			"can not populate CAC voltage tables to SMC",
			return -EINVAL);

	return 0;
}

static int tonga_populate_ulv_level(struct pp_hwmgr *hwmgr,
		struct SMU72_Discrete_Ulv *state)
{
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	state->CcPwrDynRm = 0;
	state->CcPwrDynRm1 = 0;

	state->VddcOffset = (uint16_t) table_info->us_ulv_voltage_offset;
	state->VddcOffsetVid = (uint8_t)(table_info->us_ulv_voltage_offset *
			VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1);

	state->VddcPhase = 1;

	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm1);
	CONVERT_FROM_HOST_TO_SMC_US(state->VddcOffset);

	return 0;
}

static int tonga_populate_ulv_state(struct pp_hwmgr *hwmgr,
		struct SMU72_Discrete_DpmTable *table)
{
	return tonga_populate_ulv_level(hwmgr, &table->Ulv);
}

static int tonga_populate_smc_link_level(struct pp_hwmgr *hwmgr, SMU72_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct smu7_dpm_table *dpm_table = &data->dpm_table;
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);
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

	smu_data->smc_state_table.LinkLevelCount =
		(uint8_t)dpm_table->pcie_speed_table.count;
	data->dpm_level_enable_mask.pcie_dpm_enable_mask =
		phm_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);

	return 0;
}

static int tonga_calculate_sclk_params(struct pp_hwmgr *hwmgr,
		uint32_t engine_clock, SMU72_Discrete_GraphicsLevel *sclk)
{
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
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

static int tonga_populate_single_graphic_level(struct pp_hwmgr *hwmgr,
						uint32_t engine_clock,
				SMU72_Discrete_GraphicsLevel *graphic_level)
{
	int result;
	uint32_t mvdd;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info =
			    (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_table = NULL;

	result = tonga_calculate_sclk_params(hwmgr, engine_clock, graphic_level);

	if (hwmgr->od_enabled)
		vdd_dep_table = (phm_ppt_v1_clock_voltage_dependency_table *)&data->odn_dpm_table.vdd_dependency_on_sclk;
	else
		vdd_dep_table = pptable_info->vdd_dep_on_sclk;

	/* populate graphics levels*/
	result = tonga_get_dependency_volt_by_clk(hwmgr,
		vdd_dep_table, engine_clock,
		&graphic_level->MinVoltage, &mvdd);
	PP_ASSERT_WITH_CODE((!result),
		"can not find VDDC voltage value for VDDC "
		"engine clock dependency table", return result);

	/* SCLK frequency in units of 10KHz*/
	graphic_level->SclkFrequency = engine_clock;
	/* Indicates maximum activity level for this performance level. 50% for now*/
	graphic_level->ActivityLevel = data->current_profile_setting.sclk_activity;

	graphic_level->CcPwrDynRm = 0;
	graphic_level->CcPwrDynRm1 = 0;
	/* this level can be used if activity is high enough.*/
	graphic_level->EnabledForActivity = 0;
	/* this level can be used for throttling.*/
	graphic_level->EnabledForThrottle = 1;
	graphic_level->UpHyst = data->current_profile_setting.sclk_up_hyst;
	graphic_level->DownHyst = data->current_profile_setting.sclk_down_hyst;
	graphic_level->VoltageDownHyst = 0;
	graphic_level->PowerThrottle = 0;

	data->display_timing.min_clock_in_sr =
			hwmgr->display_config->min_core_set_clock_in_sr;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep))
		graphic_level->DeepSleepDivId =
				smu7_get_sleep_divider_id_from_clock(engine_clock,
						data->display_timing.min_clock_in_sr);

	/* Default to slow, highest DPM level will be set to PPSMC_DISPLAY_WATERMARK_LOW later.*/
	graphic_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	if (!result) {
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

static int tonga_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);
	struct phm_ppt_v1_information *pptable_info = (struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct smu7_dpm_table *dpm_table = &data->dpm_table;
	struct phm_ppt_v1_pcie_table *pcie_table = pptable_info->pcie_table;
	uint8_t pcie_entry_count = (uint8_t) data->dpm_table.pcie_speed_table.count;
	uint32_t level_array_address = smu_data->smu7_data.dpm_table_start +
				offsetof(SMU72_Discrete_DpmTable, GraphicsLevel);

	uint32_t level_array_size = sizeof(SMU72_Discrete_GraphicsLevel) *
						SMU72_MAX_LEVELS_GRAPHICS;

	SMU72_Discrete_GraphicsLevel *levels = smu_data->smc_state_table.GraphicsLevel;

	uint32_t i, max_entry;
	uint8_t highest_pcie_level_enabled = 0;
	uint8_t lowest_pcie_level_enabled = 0, mid_pcie_level_enabled = 0;
	uint8_t count = 0;
	int result = 0;

	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		result = tonga_populate_single_graphic_level(hwmgr,
					dpm_table->sclk_table.dpm_levels[i].value,
					&(smu_data->smc_state_table.GraphicsLevel[i]));
		if (result != 0)
			return result;

		/* Making sure only DPM level 0-1 have Deep Sleep Div ID populated. */
		if (i > 1)
			smu_data->smc_state_table.GraphicsLevel[i].DeepSleepDivId = 0;
	}

	/* Only enable level 0 for now. */
	smu_data->smc_state_table.GraphicsLevel[0].EnabledForActivity = 1;

	/* set highest level watermark to high */
	if (dpm_table->sclk_table.count > 1)
		smu_data->smc_state_table.GraphicsLevel[dpm_table->sclk_table.count-1].DisplayWatermark =
			PPSMC_DISPLAY_WATERMARK_HIGH;

	smu_data->smc_state_table.GraphicsDpmLevelCount =
		(uint8_t)dpm_table->sclk_table.count;
	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
		phm_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);

	if (pcie_table != NULL) {
		PP_ASSERT_WITH_CODE((pcie_entry_count >= 1),
			"There must be 1 or more PCIE levels defined in PPTable.",
			return -EINVAL);
		max_entry = pcie_entry_count - 1; /* for indexing, we need to decrement by 1.*/
		for (i = 0; i < dpm_table->sclk_table.count; i++) {
			smu_data->smc_state_table.GraphicsLevel[i].pcieDpmLevel =
				(uint8_t) ((i < max_entry) ? i : max_entry);
		}
	} else {
		if (0 == data->dpm_level_enable_mask.pcie_dpm_enable_mask)
			pr_err("Pcie Dpm Enablemask is 0 !");

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
		for (i = 2; i < dpm_table->sclk_table.count; i++)
			smu_data->smc_state_table.GraphicsLevel[i].pcieDpmLevel = highest_pcie_level_enabled;

		/* set pcieDpmLevel to lowest_pcie_level_enabled*/
		smu_data->smc_state_table.GraphicsLevel[0].pcieDpmLevel = lowest_pcie_level_enabled;

		/* set pcieDpmLevel to mid_pcie_level_enabled*/
		smu_data->smc_state_table.GraphicsLevel[1].pcieDpmLevel = mid_pcie_level_enabled;
	}
	/* level count will send to smc once at init smc table and never change*/
	result = smu7_copy_bytes_to_smc(hwmgr, level_array_address,
				(uint8_t *)levels, (uint32_t)level_array_size,
								SMC_RAM_END);

	return result;
}

static int tonga_calculate_mclk_params(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU72_Discrete_MemoryLevel *mclk,
		bool strobe_mode,
		bool dllStateOn
		)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	uint32_t dll_cntl = data->clock_registers.vDLL_CNTL;
	uint32_t mclk_pwrmgt_cntl = data->clock_registers.vMCLK_PWRMGT_CNTL;
	uint32_t mpll_ad_func_cntl = data->clock_registers.vMPLL_AD_FUNC_CNTL;
	uint32_t mpll_dq_func_cntl = data->clock_registers.vMPLL_DQ_FUNC_CNTL;
	uint32_t mpll_func_cntl = data->clock_registers.vMPLL_FUNC_CNTL;
	uint32_t mpll_func_cntl_1 = data->clock_registers.vMPLL_FUNC_CNTL_1;
	uint32_t mpll_func_cntl_2 = data->clock_registers.vMPLL_FUNC_CNTL_2;
	uint32_t mpll_ss1 = data->clock_registers.vMPLL_SS1;
	uint32_t mpll_ss2 = data->clock_registers.vMPLL_SS2;

	pp_atomctrl_memory_clock_param mpll_param;
	int result;

	result = atomctrl_get_memory_pll_dividers_si(hwmgr,
				memory_clock, &mpll_param, strobe_mode);
	PP_ASSERT_WITH_CODE(
			!result,
			"Error retrieving Memory Clock Parameters from VBIOS.",
			return result);

	/* MPLL_FUNC_CNTL setup*/
	mpll_func_cntl = PHM_SET_FIELD(mpll_func_cntl, MPLL_FUNC_CNTL, BWCTRL,
					mpll_param.bw_ctrl);

	/* MPLL_FUNC_CNTL_1 setup*/
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
					MPLL_FUNC_CNTL_1, CLKF,
					mpll_param.mpll_fb_divider.cl_kf);
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
					MPLL_FUNC_CNTL_1, CLKFRAC,
					mpll_param.mpll_fb_divider.clk_frac);
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
						MPLL_FUNC_CNTL_1, VCO_MODE,
						mpll_param.vco_mode);

	/* MPLL_AD_FUNC_CNTL setup*/
	mpll_ad_func_cntl = PHM_SET_FIELD(mpll_ad_func_cntl,
					MPLL_AD_FUNC_CNTL, YCLK_POST_DIV,
					mpll_param.mpll_post_divider);

	if (data->is_memory_gddr5) {
		/* MPLL_DQ_FUNC_CNTL setup*/
		mpll_dq_func_cntl  = PHM_SET_FIELD(mpll_dq_func_cntl,
						MPLL_DQ_FUNC_CNTL, YCLK_SEL,
						mpll_param.yclk_sel);
		mpll_dq_func_cntl  = PHM_SET_FIELD(mpll_dq_func_cntl,
						MPLL_DQ_FUNC_CNTL, YCLK_POST_DIV,
						mpll_param.mpll_post_divider);
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
		if (memory_clock < 12500)
			mc_para_index = 0x00;
		else if (memory_clock > 47500)
			mc_para_index = 0x0f;
		else
			mc_para_index = (uint8_t)((memory_clock - 10000) / 2500);
	} else {
		if (memory_clock < 65000)
			mc_para_index = 0x00;
		else if (memory_clock > 135000)
			mc_para_index = 0x0f;
		else
			mc_para_index = (uint8_t)((memory_clock - 60000) / 5000);
	}

	return mc_para_index;
}

static uint8_t tonga_get_ddr3_mclk_frequency_ratio(uint32_t memory_clock)
{
	uint8_t mc_para_index;

	if (memory_clock < 10000)
		mc_para_index = 0;
	else if (memory_clock >= 80000)
		mc_para_index = 0x0f;
	else
		mc_para_index = (uint8_t)((memory_clock - 10000) / 5000 + 1);

	return mc_para_index;
}


static int tonga_populate_single_memory_level(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU72_Discrete_MemoryLevel *memory_level
		)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info =
			  (struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t mclk_edc_wr_enable_threshold = 40000;
	uint32_t mclk_stutter_mode_threshold = 30000;
	uint32_t mclk_edc_enable_threshold = 40000;
	uint32_t mclk_strobe_mode_threshold = 40000;
	phm_ppt_v1_clock_voltage_dependency_table *vdd_dep_table = NULL;
	int result = 0;
	bool dll_state_on;
	uint32_t mvdd = 0;

	if (hwmgr->od_enabled)
		vdd_dep_table = (phm_ppt_v1_clock_voltage_dependency_table *)&data->odn_dpm_table.vdd_dependency_on_mclk;
	else
		vdd_dep_table = pptable_info->vdd_dep_on_mclk;

	if (NULL != vdd_dep_table) {
		result = tonga_get_dependency_volt_by_clk(hwmgr,
				vdd_dep_table,
				memory_clock,
				&memory_level->MinVoltage, &mvdd);
		PP_ASSERT_WITH_CODE(
			!result,
			"can not find MinVddc voltage value from memory VDDC "
			"voltage dependency table",
			return result);
	}

	if (data->mvdd_control == SMU7_VOLTAGE_CONTROL_NONE)
		memory_level->MinMvdd = data->vbios_boot_state.mvdd_bootup_value;
	else
		memory_level->MinMvdd = mvdd;

	memory_level->EnabledForThrottle = 1;
	memory_level->EnabledForActivity = 0;
	memory_level->UpHyst = data->current_profile_setting.mclk_up_hyst;
	memory_level->DownHyst = data->current_profile_setting.mclk_down_hyst;
	memory_level->VoltageDownHyst = 0;

	/* Indicates maximum activity level for this performance level.*/
	memory_level->ActivityLevel = data->current_profile_setting.mclk_activity;
	memory_level->StutterEnable = 0;
	memory_level->StrobeEnable = 0;
	memory_level->EdcReadEnable = 0;
	memory_level->EdcWriteEnable = 0;
	memory_level->RttEnable = 0;

	/* default set to low watermark. Highest level will be set to high later.*/
	memory_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	data->display_timing.num_existing_displays = hwmgr->display_config->num_display;
	data->display_timing.vrefresh = hwmgr->display_config->vrefresh;

	if ((mclk_stutter_mode_threshold != 0) &&
	    (memory_clock <= mclk_stutter_mode_threshold) &&
	    (!data->is_uvd_enabled)
	    && (PHM_READ_FIELD(hwmgr->device, DPG_PIPE_STUTTER_CONTROL, STUTTER_ENABLE) & 0x1)
	    && (data->display_timing.num_existing_displays <= 2)
	    && (data->display_timing.num_existing_displays != 0))
		memory_level->StutterEnable = 1;

	/* decide strobe mode*/
	memory_level->StrobeEnable = (mclk_strobe_mode_threshold != 0) &&
		(memory_clock <= mclk_strobe_mode_threshold);

	/* decide EDC mode and memory clock ratio*/
	if (data->is_memory_gddr5) {
		memory_level->StrobeRatio = tonga_get_mclk_frequency_ratio(memory_clock,
					memory_level->StrobeEnable);

		if ((mclk_edc_enable_threshold != 0) &&
				(memory_clock > mclk_edc_enable_threshold)) {
			memory_level->EdcReadEnable = 1;
		}

		if ((mclk_edc_wr_enable_threshold != 0) &&
				(memory_clock > mclk_edc_wr_enable_threshold)) {
			memory_level->EdcWriteEnable = 1;
		}

		if (memory_level->StrobeEnable) {
			if (tonga_get_mclk_frequency_ratio(memory_clock, 1) >=
					((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC7) >> 16) & 0xf)) {
				dll_state_on = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC5) >> 1) & 0x1) ? 1 : 0;
			} else {
				dll_state_on = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC6) >> 1) & 0x1) ? 1 : 0;
			}

		} else {
			dll_state_on = data->dll_default_on;
		}
	} else {
		memory_level->StrobeRatio =
			tonga_get_ddr3_mclk_frequency_ratio(memory_clock);
		dll_state_on = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC5) >> 1) & 0x1) ? 1 : 0;
	}

	result = tonga_calculate_mclk_params(hwmgr,
		memory_clock, memory_level, memory_level->StrobeEnable, dll_state_on);

	if (!result) {
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

static int tonga_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data =
			(struct tonga_smumgr *)(hwmgr->smu_backend);
	struct smu7_dpm_table *dpm_table = &data->dpm_table;
	int result;

	/* populate MCLK dpm table to SMU7 */
	uint32_t level_array_address =
				smu_data->smu7_data.dpm_table_start +
				offsetof(SMU72_Discrete_DpmTable, MemoryLevel);
	uint32_t level_array_size =
				sizeof(SMU72_Discrete_MemoryLevel) *
				SMU72_MAX_LEVELS_MEMORY;
	SMU72_Discrete_MemoryLevel *levels =
				smu_data->smc_state_table.MemoryLevel;
	uint32_t i;

	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		PP_ASSERT_WITH_CODE((0 != dpm_table->mclk_table.dpm_levels[i].value),
			"can not populate memory level as memory clock is zero",
			return -EINVAL);
		result = tonga_populate_single_memory_level(
				hwmgr,
				dpm_table->mclk_table.dpm_levels[i].value,
				&(smu_data->smc_state_table.MemoryLevel[i]));
		if (result)
			return result;
	}

	/* Only enable level 0 for now.*/
	smu_data->smc_state_table.MemoryLevel[0].EnabledForActivity = 1;

	/*
	* in order to prevent MC activity from stutter mode to push DPM up.
	* the UVD change complements this by putting the MCLK in a higher state
	* by default such that we are not effected by up threshold or and MCLK DPM latency.
	*/
	smu_data->smc_state_table.MemoryLevel[0].ActivityLevel = 0x1F;
	CONVERT_FROM_HOST_TO_SMC_US(smu_data->smc_state_table.MemoryLevel[0].ActivityLevel);

	smu_data->smc_state_table.MemoryDpmLevelCount = (uint8_t)dpm_table->mclk_table.count;
	data->dpm_level_enable_mask.mclk_dpm_enable_mask = phm_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);
	/* set highest level watermark to high*/
	smu_data->smc_state_table.MemoryLevel[dpm_table->mclk_table.count-1].DisplayWatermark = PPSMC_DISPLAY_WATERMARK_HIGH;

	/* level count will send to smc once at init smc table and never change*/
	result = smu7_copy_bytes_to_smc(hwmgr,
		level_array_address, (uint8_t *)levels, (uint32_t)level_array_size,
		SMC_RAM_END);

	return result;
}

static int tonga_populate_mvdd_value(struct pp_hwmgr *hwmgr,
				uint32_t mclk, SMIO_Pattern *smio_pattern)
{
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t i = 0;

	if (SMU7_VOLTAGE_CONTROL_NONE != data->mvdd_control) {
		/* find mvdd value which clock is more than request */
		for (i = 0; i < table_info->vdd_dep_on_mclk->count; i++) {
			if (mclk <= table_info->vdd_dep_on_mclk->entries[i].clk) {
				/* Always round to higher voltage. */
				smio_pattern->Voltage =
				      data->mvdd_voltage_table.entries[i].value;
				break;
			}
		}

		PP_ASSERT_WITH_CODE(i < table_info->vdd_dep_on_mclk->count,
			"MVDD Voltage is outside the supported range.",
			return -EINVAL);
	} else {
		return -EINVAL;
	}

	return 0;
}


static int tonga_populate_smc_acpi_level(struct pp_hwmgr *hwmgr,
	SMU72_Discrete_DpmTable *table)
{
	int result = 0;
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_clock_dividers_vi dividers;

	SMIO_Pattern voltage_level;
	uint32_t spll_func_cntl    = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_2  = data->clock_registers.vCG_SPLL_FUNC_CNTL_2;
	uint32_t dll_cntl          = data->clock_registers.vDLL_CNTL;
	uint32_t mclk_pwrmgt_cntl  = data->clock_registers.vMCLK_PWRMGT_CNTL;

	/* The ACPI state should not do DPM on DC (or ever).*/
	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	table->ACPILevel.MinVoltage =
			smu_data->smc_state_table.GraphicsLevel[0].MinVoltage;

	/* assign zero for now*/
	table->ACPILevel.SclkFrequency = atomctrl_get_reference_clock(hwmgr);

	/* get the engine clock dividers for this clock value*/
	result = atomctrl_get_engine_pll_dividers_vi(hwmgr,
		table->ACPILevel.SclkFrequency,  &dividers);

	PP_ASSERT_WITH_CODE(result == 0,
		"Error retrieving Engine Clock dividers from VBIOS.",
		return result);

	/* divider ID for required SCLK*/
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
	table->MemoryACPILevel.MinVoltage =
			    smu_data->smc_state_table.MemoryLevel[0].MinVoltage;

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
	table->MemoryACPILevel.ActivityLevel =
			PP_HOST_TO_SMC_US(data->current_profile_setting.mclk_activity);

	table->MemoryACPILevel.StutterEnable = 0;
	table->MemoryACPILevel.StrobeEnable = 0;
	table->MemoryACPILevel.EdcReadEnable = 0;
	table->MemoryACPILevel.EdcWriteEnable = 0;
	table->MemoryACPILevel.RttEnable = 0;

	return result;
}

static int tonga_populate_smc_uvd_level(struct pp_hwmgr *hwmgr,
					SMU72_Discrete_DpmTable *table)
{
	int result = 0;

	uint8_t count;
	pp_atomctrl_clock_dividers_vi dividers;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info =
				(struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
						pptable_info->mm_dep_table;

	table->UvdLevelCount = (uint8_t) (mm_table->count);
	table->UvdBootLevel = 0;

	for (count = 0; count < table->UvdLevelCount; count++) {
		table->UvdLevel[count].VclkFrequency = mm_table->entries[count].vclk;
		table->UvdLevel[count].DclkFrequency = mm_table->entries[count].dclk;
		table->UvdLevel[count].MinVoltage.Vddc =
			phm_get_voltage_index(pptable_info->vddc_lookup_table,
						mm_table->entries[count].vddc);
		table->UvdLevel[count].MinVoltage.VddGfx =
			(data->vdd_gfx_control == SMU7_VOLTAGE_CONTROL_BY_SVID2) ?
			phm_get_voltage_index(pptable_info->vddgfx_lookup_table,
						mm_table->entries[count].vddgfx) : 0;
		table->UvdLevel[count].MinVoltage.Vddci =
			phm_get_voltage_id(&data->vddci_voltage_table,
					     mm_table->entries[count].vddc - VDDC_VDDCI_DELTA);
		table->UvdLevel[count].MinVoltage.Phases = 1;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(
					hwmgr,
					table->UvdLevel[count].VclkFrequency,
					&dividers);

		PP_ASSERT_WITH_CODE((!result),
				    "can not find divide id for Vclk clock",
					return result);

		table->UvdLevel[count].VclkDivider = (uint8_t)dividers.pll_post_divider;

		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
							  table->UvdLevel[count].DclkFrequency, &dividers);
		PP_ASSERT_WITH_CODE((!result),
				    "can not find divide id for Dclk clock",
					return result);

		table->UvdLevel[count].DclkDivider =
					(uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].VclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_UL(table->UvdLevel[count].DclkFrequency);
	}

	return result;

}

static int tonga_populate_smc_vce_level(struct pp_hwmgr *hwmgr,
		SMU72_Discrete_DpmTable *table)
{
	int result = 0;

	uint8_t count;
	pp_atomctrl_clock_dividers_vi dividers;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info =
			      (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
						     pptable_info->mm_dep_table;

	table->VceLevelCount = (uint8_t) (mm_table->count);
	table->VceBootLevel = 0;

	for (count = 0; count < table->VceLevelCount; count++) {
		table->VceLevel[count].Frequency =
			mm_table->entries[count].eclk;
		table->VceLevel[count].MinVoltage.Vddc =
			phm_get_voltage_index(pptable_info->vddc_lookup_table,
				mm_table->entries[count].vddc);
		table->VceLevel[count].MinVoltage.VddGfx =
			(data->vdd_gfx_control == SMU7_VOLTAGE_CONTROL_BY_SVID2) ?
			phm_get_voltage_index(pptable_info->vddgfx_lookup_table,
				mm_table->entries[count].vddgfx) : 0;
		table->VceLevel[count].MinVoltage.Vddci =
			phm_get_voltage_id(&data->vddci_voltage_table,
				mm_table->entries[count].vddc - VDDC_VDDCI_DELTA);
		table->VceLevel[count].MinVoltage.Phases = 1;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
					table->VceLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((!result),
				"can not find divide id for VCE engine clock",
				return result);

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
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *pptable_info =
			     (struct phm_ppt_v1_information *)(hwmgr->pptable);
	phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
						    pptable_info->mm_dep_table;

	table->AcpLevelCount = (uint8_t) (mm_table->count);
	table->AcpBootLevel = 0;

	for (count = 0; count < table->AcpLevelCount; count++) {
		table->AcpLevel[count].Frequency =
			pptable_info->mm_dep_table->entries[count].aclk;
		table->AcpLevel[count].MinVoltage.Vddc =
			phm_get_voltage_index(pptable_info->vddc_lookup_table,
			mm_table->entries[count].vddc);
		table->AcpLevel[count].MinVoltage.VddGfx =
			(data->vdd_gfx_control == SMU7_VOLTAGE_CONTROL_BY_SVID2) ?
			phm_get_voltage_index(pptable_info->vddgfx_lookup_table,
				mm_table->entries[count].vddgfx) : 0;
		table->AcpLevel[count].MinVoltage.Vddci =
			phm_get_voltage_id(&data->vddci_voltage_table,
				mm_table->entries[count].vddc - VDDC_VDDCI_DELTA);
		table->AcpLevel[count].MinVoltage.Phases = 1;

		/* retrieve divider value for VBIOS */
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr,
			table->AcpLevel[count].Frequency, &dividers);
		PP_ASSERT_WITH_CODE((!result),
			"can not find divide id for engine clock", return result);

		table->AcpLevel[count].Divider = (uint8_t)dividers.pll_post_divider;

		CONVERT_FROM_HOST_TO_SMC_UL(table->AcpLevel[count].Frequency);
	}

	return result;
}

static int tonga_populate_memory_timing_parameters(
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

static int tonga_program_memory_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
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

			if (result)
				break;
		}
	}

	if (!result) {
		result = smu7_copy_bytes_to_smc(
				hwmgr,
				smu_data->smu7_data.arb_table_start,
				(uint8_t *)&arb_regs,
				sizeof(SMU72_Discrete_MCArbDramTimingTable),
				SMC_RAM_END
				);
	}

	return result;
}

static int tonga_populate_smc_boot_level(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	int result = 0;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	table->GraphicsBootLevel = 0;
	table->MemoryBootLevel = 0;

	/* find boot level from dpm table*/
	result = phm_find_boot_level(&(data->dpm_table.sclk_table),
	data->vbios_boot_state.sclk_bootup_value,
	(uint32_t *)&(smu_data->smc_state_table.GraphicsBootLevel));

	if (result != 0) {
		smu_data->smc_state_table.GraphicsBootLevel = 0;
		pr_err("[powerplay] VBIOS did not find boot engine "
				"clock value in dependency table. "
				"Using Graphics DPM level 0 !");
		result = 0;
	}

	result = phm_find_boot_level(&(data->dpm_table.mclk_table),
		data->vbios_boot_state.mclk_bootup_value,
		(uint32_t *)&(smu_data->smc_state_table.MemoryBootLevel));

	if (result != 0) {
		smu_data->smc_state_table.MemoryBootLevel = 0;
		pr_err("[powerplay] VBIOS did not find boot "
				"engine clock value in dependency table."
				"Using Memory DPM level 0 !");
		result = 0;
	}

	table->BootVoltage.Vddc =
		phm_get_voltage_id(&(data->vddc_voltage_table),
			data->vbios_boot_state.vddc_bootup_value);
	table->BootVoltage.VddGfx =
		phm_get_voltage_id(&(data->vddgfx_voltage_table),
			data->vbios_boot_state.vddgfx_bootup_value);
	table->BootVoltage.Vddci =
		phm_get_voltage_id(&(data->vddci_voltage_table),
			data->vbios_boot_state.vddci_bootup_value);
	table->BootMVdd = data->vbios_boot_state.mvdd_bootup_value;

	CONVERT_FROM_HOST_TO_SMC_US(table->BootMVdd);

	return result;
}

static int tonga_populate_clock_stretcher_data_table(struct pp_hwmgr *hwmgr)
{
	uint32_t ro, efuse, efuse2, clock_freq, volt_without_cks,
			volt_with_cks, value;
	uint16_t clock_freq_u16;
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	uint8_t type, i, j, cks_setting, stretch_amount, stretch_amount2,
			volt_offset = 0;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *sclk_table =
			table_info->vdd_dep_on_sclk;
	uint32_t hw_revision, dev_id;
	struct amdgpu_device *adev = hwmgr->adev;

	stretch_amount = (uint8_t)table_info->cac_dtp_table->usClockStretchAmount;

	hw_revision = adev->pdev->revision;
	dev_id = adev->pdev->device;

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
	smu_data->smc_state_table.ClockStretcherAmount = stretch_amount;


	/* Populate Sclk_CKS_masterEn0_7 and Sclk_voltageOffset */
	for (i = 0; i < sclk_table->count; i++) {
		smu_data->smc_state_table.Sclk_CKS_masterEn0_7 |=
				sclk_table->entries[i].cks_enable << i;
		if (ASICID_IS_TONGA_P(dev_id, hw_revision)) {
			volt_without_cks = (uint32_t)((7732 + 60 - ro - 20838 *
				(sclk_table->entries[i].clk/100) / 10000) * 1000 /
				(8730 - (5301 * (sclk_table->entries[i].clk/100) / 1000)));
			volt_with_cks = (uint32_t)((5250 + 51 - ro - 2404 *
				(sclk_table->entries[i].clk/100) / 100000) * 1000 /
				(6146 - (3193 * (sclk_table->entries[i].clk/100) / 1000)));
		} else {
			volt_without_cks = (uint32_t)((14041 *
				(sclk_table->entries[i].clk/100) / 10000 + 3571 + 75 - ro) * 1000 /
				(4026 - (13924 * (sclk_table->entries[i].clk/100) / 10000)));
			volt_with_cks = (uint32_t)((13946 *
				(sclk_table->entries[i].clk/100) / 10000 + 3320 + 45 - ro) * 1000 /
				(3664 - (11454 * (sclk_table->entries[i].clk/100) / 10000)));
		}
		if (volt_without_cks >= volt_with_cks)
			volt_offset = (uint8_t)(((volt_without_cks - volt_with_cks +
					sclk_table->entries[i].cks_voffset) * 100 / 625) + 1);
		smu_data->smc_state_table.Sclk_voltageOffset[i] = volt_offset;
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
				"Stretch Amount in PPTable not supported",
				return -EINVAL);
	}

	value = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixPWR_CKS_CNTL);
	value &= 0xFFC2FF87;
	smu_data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].minFreq =
			tonga_clock_stretcher_lookup_table[stretch_amount2][0];
	smu_data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].maxFreq =
			tonga_clock_stretcher_lookup_table[stretch_amount2][1];
	clock_freq_u16 = (uint16_t)(PP_SMC_TO_HOST_UL(smu_data->smc_state_table.
			GraphicsLevel[smu_data->smc_state_table.GraphicsDpmLevelCount - 1].
			SclkFrequency) / 100);
	if (tonga_clock_stretcher_lookup_table[stretch_amount2][0] <
			clock_freq_u16 &&
	    tonga_clock_stretcher_lookup_table[stretch_amount2][1] >
			clock_freq_u16) {
		/* Program PWR_CKS_CNTL. CKS_USE_FOR_LOW_FREQ */
		value |= (tonga_clock_stretcher_lookup_table[stretch_amount2][3]) << 16;
		/* Program PWR_CKS_CNTL. CKS_LDO_REFSEL */
		value |= (tonga_clock_stretcher_lookup_table[stretch_amount2][2]) << 18;
		/* Program PWR_CKS_CNTL. CKS_STRETCH_AMOUNT */
		value |= (tonga_clock_stretch_amount_conversion
				[tonga_clock_stretcher_lookup_table[stretch_amount2][3]]
				 [stretch_amount]) << 3;
	}
	CONVERT_FROM_HOST_TO_SMC_US(smu_data->smc_state_table.CKS_LOOKUPTable.
			CKS_LOOKUPTableEntry[0].minFreq);
	CONVERT_FROM_HOST_TO_SMC_US(smu_data->smc_state_table.CKS_LOOKUPTable.
			CKS_LOOKUPTableEntry[0].maxFreq);
	smu_data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].setting =
			tonga_clock_stretcher_lookup_table[stretch_amount2][2] & 0x7F;
	smu_data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].setting |=
			(tonga_clock_stretcher_lookup_table[stretch_amount2][3]) << 7;

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixPWR_CKS_CNTL, value);

	/* Populate DDT Lookup Table */
	for (i = 0; i < 4; i++) {
		/* Assign the minimum and maximum VID stored
		 * in the last row of Clock Stretcher Voltage Table.
		 */
		smu_data->smc_state_table.ClockStretcherDataTable.
		ClockStretcherDataTableEntry[i].minVID =
				(uint8_t) tonga_clock_stretcher_ddt_table[type][i][2];
		smu_data->smc_state_table.ClockStretcherDataTable.
		ClockStretcherDataTableEntry[i].maxVID =
				(uint8_t) tonga_clock_stretcher_ddt_table[type][i][3];
		/* Loop through each SCLK and check the frequency
		 * to see if it lies within the frequency for clock stretcher.
		 */
		for (j = 0; j < smu_data->smc_state_table.GraphicsDpmLevelCount; j++) {
			cks_setting = 0;
			clock_freq = PP_SMC_TO_HOST_UL(
					smu_data->smc_state_table.GraphicsLevel[j].SclkFrequency);
			/* Check the allowed frequency against the sclk level[j].
			 *  Sclk's endianness has already been converted,
			 *  and it's in 10Khz unit,
			 *  as opposed to Data table, which is in Mhz unit.
			 */
			if (clock_freq >= tonga_clock_stretcher_ddt_table[type][i][0] * 100) {
				cks_setting |= 0x2;
				if (clock_freq < tonga_clock_stretcher_ddt_table[type][i][1] * 100)
					cks_setting |= 0x1;
			}
			smu_data->smc_state_table.ClockStretcherDataTable.
			ClockStretcherDataTableEntry[i].setting |= cks_setting << (j * 2);
		}
		CONVERT_FROM_HOST_TO_SMC_US(smu_data->smc_state_table.
				ClockStretcherDataTable.
				ClockStretcherDataTableEntry[i].setting);
	}

	value = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixPWR_CKS_CNTL);
	value &= 0xFFFFFFFE;
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixPWR_CKS_CNTL, value);

	return 0;
}

static int tonga_populate_vr_config(struct pp_hwmgr *hwmgr,
			SMU72_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint16_t config;

	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_gfx_control) {
		/*  Splitted mode */
		config = VR_SVI2_PLANE_1;
		table->VRConfig |= (config<<VRCONF_VDDGFX_SHIFT);

		if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
			config = VR_SVI2_PLANE_2;
			table->VRConfig |= config;
		} else {
			pr_err("VDDC and VDDGFX should "
				"be both on SVI2 control in splitted mode !\n");
		}
	} else {
		/* Merged mode  */
		config = VR_MERGED_WITH_VDDC;
		table->VRConfig |= (config<<VRCONF_VDDGFX_SHIFT);

		/* Set Vddc Voltage Controller  */
		if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
			config = VR_SVI2_PLANE_1;
			table->VRConfig |= config;
		} else {
			pr_err("VDDC should be on "
					"SVI2 control in merged mode !\n");
		}
	}

	/* Set Vddci Voltage Controller  */
	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control) {
		config = VR_SVI2_PLANE_2;  /* only in merged mode */
		table->VRConfig |= (config<<VRCONF_VDDCI_SHIFT);
	} else if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		config = VR_SMIO_PATTERN_1;
		table->VRConfig |= (config<<VRCONF_VDDCI_SHIFT);
	}

	/* Set Mvdd Voltage Controller */
	if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		config = VR_SMIO_PATTERN_2;
		table->VRConfig |= (config<<VRCONF_MVDD_SHIFT);
	}

	return 0;
}

static int tonga_init_arb_table_index(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);
	uint32_t tmp;
	int result;

	/*
	* This is a read-modify-write on the first byte of the ARB table.
	* The first byte in the SMU72_Discrete_MCArbDramTimingTable structure
	* is the field 'current'.
	* This solution is ugly, but we never write the whole table only
	* individual fields in it.
	* In reality this field should not be in that structure
	* but in a soft register.
	*/
	result = smu7_read_smc_sram_dword(hwmgr,
				smu_data->smu7_data.arb_table_start, &tmp, SMC_RAM_END);

	if (result != 0)
		return result;

	tmp &= 0x00FFFFFF;
	tmp |= ((uint32_t)MC_CG_ARB_FREQ_F1) << 24;

	return smu7_write_smc_sram_dword(hwmgr,
			smu_data->smu7_data.arb_table_start, tmp, SMC_RAM_END);
}


static int tonga_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	const struct tonga_pt_defaults *defaults = smu_data->power_tune_defaults;
	SMU72_Discrete_DpmTable  *dpm_table = &(smu_data->smc_state_table);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_cac_tdp_table *cac_dtp_table = table_info->cac_dtp_table;
	int  i, j, k;
	const uint16_t *pdef1, *pdef2;

	dpm_table->DefaultTdp = PP_HOST_TO_SMC_US(
			(uint16_t)(cac_dtp_table->usTDP * 256));
	dpm_table->TargetTdp = PP_HOST_TO_SMC_US(
			(uint16_t)(cac_dtp_table->usConfigurableTDP * 256));

	PP_ASSERT_WITH_CODE(cac_dtp_table->usTargetOperatingTemp <= 255,
			"Target Operating Temp is out of Range !",
			);

	dpm_table->GpuTjMax = (uint8_t)(cac_dtp_table->usTargetOperatingTemp);
	dpm_table->GpuTjHyst = 8;

	dpm_table->DTEAmbientTempBase = defaults->dte_ambient_temp_base;

	dpm_table->BAPM_TEMP_GRADIENT =
				PP_HOST_TO_SMC_UL(defaults->bapm_temp_gradient);
	pdef1 = defaults->bapmti_r;
	pdef2 = defaults->bapmti_rc;

	for (i = 0; i < SMU72_DTE_ITERATIONS; i++) {
		for (j = 0; j < SMU72_DTE_SOURCES; j++) {
			for (k = 0; k < SMU72_DTE_SINKS; k++) {
				dpm_table->BAPMTI_R[i][j][k] =
						PP_HOST_TO_SMC_US(*pdef1);
				dpm_table->BAPMTI_RC[i][j][k] =
						PP_HOST_TO_SMC_US(*pdef2);
				pdef1++;
				pdef2++;
			}
		}
	}

	return 0;
}

static int tonga_populate_svi_load_line(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	const struct tonga_pt_defaults *defaults = smu_data->power_tune_defaults;

	smu_data->power_tune_table.SviLoadLineEn = defaults->svi_load_line_en;
	smu_data->power_tune_table.SviLoadLineVddC = defaults->svi_load_line_vddC;
	smu_data->power_tune_table.SviLoadLineTrimVddC = 3;
	smu_data->power_tune_table.SviLoadLineOffsetVddC = 0;

	return 0;
}

static int tonga_populate_tdc_limit(struct pp_hwmgr *hwmgr)
{
	uint16_t tdc_limit;
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	const struct tonga_pt_defaults *defaults = smu_data->power_tune_defaults;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	/* TDC number of fraction bits are changed from 8 to 7
	 * for Fiji as requested by SMC team
	 */
	tdc_limit = (uint16_t)(table_info->cac_dtp_table->usTDC * 256);
	smu_data->power_tune_table.TDC_VDDC_PkgLimit =
			CONVERT_FROM_HOST_TO_SMC_US(tdc_limit);
	smu_data->power_tune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
			defaults->tdc_vddc_throttle_release_limit_perc;
	smu_data->power_tune_table.TDC_MAWt = defaults->tdc_mawt;

	return 0;
}

static int tonga_populate_dw8(struct pp_hwmgr *hwmgr, uint32_t fuse_table_offset)
{
	struct tonga_smumgr *smu_data =
			(struct tonga_smumgr *)(hwmgr->smu_backend);
	const struct tonga_pt_defaults *defaults = smu_data->power_tune_defaults;
	uint32_t temp;

	if (smu7_read_smc_sram_dword(hwmgr,
			fuse_table_offset +
			offsetof(SMU72_Discrete_PmFuses, TdcWaterfallCtl),
			(uint32_t *)&temp, SMC_RAM_END))
		PP_ASSERT_WITH_CODE(false,
				"Attempt to read PmFuses.DW6 "
				"(SviLoadLineEn) from SMC Failed !",
				return -EINVAL);
	else
		smu_data->power_tune_table.TdcWaterfallCtl = defaults->tdc_waterfall_ctl;

	return 0;
}

static int tonga_populate_temperature_scaler(struct pp_hwmgr *hwmgr)
{
	int i;
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		smu_data->power_tune_table.LPMLTemperatureScaler[i] = 0;

	return 0;
}

static int tonga_populate_fuzzy_fan(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);

	if ((hwmgr->thermal_controller.advanceFanControlParameters.
			usFanOutputSensitivity & (1 << 15)) ||
		(hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity == 0))
		hwmgr->thermal_controller.advanceFanControlParameters.
		usFanOutputSensitivity = hwmgr->thermal_controller.
			advanceFanControlParameters.usDefaultFanOutputSensitivity;

	smu_data->power_tune_table.FuzzyFan_PwmSetDelta =
			PP_HOST_TO_SMC_US(hwmgr->thermal_controller.
					advanceFanControlParameters.usFanOutputSensitivity);
	return 0;
}

static int tonga_populate_gnb_lpml(struct pp_hwmgr *hwmgr)
{
	int i;
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		smu_data->power_tune_table.GnbLPML[i] = 0;

	return 0;
}

static int tonga_populate_bapm_vddc_base_leakage_sidd(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint16_t hi_sidd = smu_data->power_tune_table.BapmVddCBaseLeakageHiSidd;
	uint16_t lo_sidd = smu_data->power_tune_table.BapmVddCBaseLeakageLoSidd;
	struct phm_cac_tdp_table *cac_table = table_info->cac_dtp_table;

	hi_sidd = (uint16_t)(cac_table->usHighCACLeakage / 100 * 256);
	lo_sidd = (uint16_t)(cac_table->usLowCACLeakage / 100 * 256);

	smu_data->power_tune_table.BapmVddCBaseLeakageHiSidd =
			CONVERT_FROM_HOST_TO_SMC_US(hi_sidd);
	smu_data->power_tune_table.BapmVddCBaseLeakageLoSidd =
			CONVERT_FROM_HOST_TO_SMC_US(lo_sidd);

	return 0;
}

static int tonga_populate_pm_fuses(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	uint32_t pm_fuse_table_offset;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (smu7_read_smc_sram_dword(hwmgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, PmFuseTable),
				&pm_fuse_table_offset, SMC_RAM_END))
			PP_ASSERT_WITH_CODE(false,
				"Attempt to get pm_fuse_table_offset Failed !",
				return -EINVAL);

		/* DW6 */
		if (tonga_populate_svi_load_line(hwmgr))
			PP_ASSERT_WITH_CODE(false,
				"Attempt to populate SviLoadLine Failed !",
				return -EINVAL);
		/* DW7 */
		if (tonga_populate_tdc_limit(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TDCLimit Failed !",
					return -EINVAL);
		/* DW8 */
		if (tonga_populate_dw8(hwmgr, pm_fuse_table_offset))
			PP_ASSERT_WITH_CODE(false,
				"Attempt to populate TdcWaterfallCtl Failed !",
				return -EINVAL);

		/* DW9-DW12 */
		if (tonga_populate_temperature_scaler(hwmgr) != 0)
			PP_ASSERT_WITH_CODE(false,
				"Attempt to populate LPMLTemperatureScaler Failed !",
				return -EINVAL);

		/* DW13-DW14 */
		if (tonga_populate_fuzzy_fan(hwmgr))
			PP_ASSERT_WITH_CODE(false,
				"Attempt to populate Fuzzy Fan "
				"Control parameters Failed !",
				return -EINVAL);

		/* DW15-DW18 */
		if (tonga_populate_gnb_lpml(hwmgr))
			PP_ASSERT_WITH_CODE(false,
				"Attempt to populate GnbLPML Failed !",
				return -EINVAL);

		/* DW20 */
		if (tonga_populate_bapm_vddc_base_leakage_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(
				false,
				"Attempt to populate BapmVddCBaseLeakage "
				"Hi and Lo Sidd Failed !",
				return -EINVAL);

		if (smu7_copy_bytes_to_smc(hwmgr, pm_fuse_table_offset,
				(uint8_t *)&smu_data->power_tune_table,
				sizeof(struct SMU72_Discrete_PmFuses), SMC_RAM_END))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to download PmFuseTable Failed !",
					return -EINVAL);
	}
	return 0;
}

static int tonga_populate_mc_reg_address(struct pp_hwmgr *hwmgr,
				 SMU72_Discrete_MCRegisters *mc_reg_table)
{
	const struct tonga_smumgr *smu_data = (struct tonga_smumgr *)hwmgr->smu_backend;

	uint32_t i, j;

	for (i = 0, j = 0; j < smu_data->mc_reg_table.last; j++) {
		if (smu_data->mc_reg_table.validflag & 1<<j) {
			PP_ASSERT_WITH_CODE(
				i < SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE,
				"Index of mc_reg_table->address[] array "
				"out of boundary",
				return -EINVAL);
			mc_reg_table->address[i].s0 =
				PP_HOST_TO_SMC_US(smu_data->mc_reg_table.mc_reg_address[j].s0);
			mc_reg_table->address[i].s1 =
				PP_HOST_TO_SMC_US(smu_data->mc_reg_table.mc_reg_address[j].s1);
			i++;
		}
	}

	mc_reg_table->last = (uint8_t)i;

	return 0;
}

/*convert register values from driver to SMC format */
static void tonga_convert_mc_registers(
	const struct tonga_mc_reg_entry *entry,
	SMU72_Discrete_MCRegisterSet *data,
	uint32_t num_entries, uint32_t valid_flag)
{
	uint32_t i, j;

	for (i = 0, j = 0; j < num_entries; j++) {
		if (valid_flag & 1<<j) {
			data->value[i] = PP_HOST_TO_SMC_UL(entry->mc_data[j]);
			i++;
		}
	}
}

static int tonga_convert_mc_reg_table_entry_to_smc(
		struct pp_hwmgr *hwmgr,
		const uint32_t memory_clock,
		SMU72_Discrete_MCRegisterSet *mc_reg_table_data
		)
{
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);
	uint32_t i = 0;

	for (i = 0; i < smu_data->mc_reg_table.num_entries; i++) {
		if (memory_clock <=
			smu_data->mc_reg_table.mc_reg_table_entry[i].mclk_max) {
			break;
		}
	}

	if ((i == smu_data->mc_reg_table.num_entries) && (i > 0))
		--i;

	tonga_convert_mc_registers(&smu_data->mc_reg_table.mc_reg_table_entry[i],
				mc_reg_table_data, smu_data->mc_reg_table.last,
				smu_data->mc_reg_table.validflag);

	return 0;
}

static int tonga_convert_mc_reg_table_to_smc(struct pp_hwmgr *hwmgr,
		SMU72_Discrete_MCRegisters *mc_regs)
{
	int result = 0;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	int res;
	uint32_t i;

	for (i = 0; i < data->dpm_table.mclk_table.count; i++) {
		res = tonga_convert_mc_reg_table_entry_to_smc(
				hwmgr,
				data->dpm_table.mclk_table.dpm_levels[i].value,
				&mc_regs->data[i]
				);

		if (0 != res)
			result = res;
	}

	return result;
}

static int tonga_update_and_upload_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t address;
	int32_t result;

	if (0 == (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK))
		return 0;


	memset(&smu_data->mc_regs, 0, sizeof(SMU72_Discrete_MCRegisters));

	result = tonga_convert_mc_reg_table_to_smc(hwmgr, &(smu_data->mc_regs));

	if (result != 0)
		return result;


	address = smu_data->smu7_data.mc_reg_table_start +
			(uint32_t)offsetof(SMU72_Discrete_MCRegisters, data[0]);

	return  smu7_copy_bytes_to_smc(
			hwmgr, address,
			(uint8_t *)&smu_data->mc_regs.data[0],
			sizeof(SMU72_Discrete_MCRegisterSet) *
			data->dpm_table.mclk_table.count,
			SMC_RAM_END);
}

static int tonga_populate_initial_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);

	memset(&smu_data->mc_regs, 0x00, sizeof(SMU72_Discrete_MCRegisters));
	result = tonga_populate_mc_reg_address(hwmgr, &(smu_data->mc_regs));
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize MCRegTable for the MC register addresses !",
		return result;);

	result = tonga_convert_mc_reg_table_to_smc(hwmgr, &smu_data->mc_regs);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize MCRegTable for driver state !",
		return result;);

	return smu7_copy_bytes_to_smc(hwmgr, smu_data->smu7_data.mc_reg_table_start,
			(uint8_t *)&smu_data->mc_regs, sizeof(SMU72_Discrete_MCRegisters), SMC_RAM_END);
}

static void tonga_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);
	struct  phm_ppt_v1_information *table_info =
			(struct  phm_ppt_v1_information *)(hwmgr->pptable);

	if (table_info &&
			table_info->cac_dtp_table->usPowerTuneDataSetID <= POWERTUNE_DEFAULT_SET_MAX &&
			table_info->cac_dtp_table->usPowerTuneDataSetID)
		smu_data->power_tune_defaults =
				&tonga_power_tune_data_set_array
				[table_info->cac_dtp_table->usPowerTuneDataSetID - 1];
	else
		smu_data->power_tune_defaults = &tonga_power_tune_data_set_array[0];
}

static int tonga_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data =
			(struct tonga_smumgr *)(hwmgr->smu_backend);
	SMU72_Discrete_DpmTable *table = &(smu_data->smc_state_table);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	uint8_t i;
	pp_atomctrl_gpio_pin_assignment gpio_pin_assignment;


	memset(&(smu_data->smc_state_table), 0x00, sizeof(smu_data->smc_state_table));

	tonga_initialize_power_tune_defaults(hwmgr);

	if (SMU7_VOLTAGE_CONTROL_NONE != data->voltage_control)
		tonga_populate_smc_voltage_tables(hwmgr, table);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;


	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StepVddc))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (data->is_memory_gddr5)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GDDR5;

	i = PHM_READ_FIELD(hwmgr->device, CC_MC_MAX_CHANNEL, NOOFCHAN);

	if (i == 1 || i == 0)
		table->SystemFlags |= 0x40;

	if (data->ulv_supported && table_info->us_ulv_voltage_offset) {
		result = tonga_populate_ulv_state(hwmgr, table);
		PP_ASSERT_WITH_CODE(!result,
			"Failed to initialize ULV state !",
			return result;);

		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_ULV_PARAMETER, 0x40035);
	}

	result = tonga_populate_smc_link_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize Link Level !", return result);

	result = tonga_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize Graphics Level !", return result);

	result = tonga_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize Memory Level !", return result);

	result = tonga_populate_smc_acpi_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize ACPI Level !", return result);

	result = tonga_populate_smc_vce_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize VCE Level !", return result);

	result = tonga_populate_smc_acp_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize ACP Level !", return result);

	/* Since only the initial state is completely set up at this
	* point (the other states are just copies of the boot state) we only
	* need to populate the  ARB settings for the initial state.
	*/
	result = tonga_program_memory_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to Write ARB settings for the initial state.",
		return result;);

	result = tonga_populate_smc_uvd_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize UVD Level !", return result);

	result = tonga_populate_smc_boot_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to initialize Boot Level !", return result);

	tonga_populate_bapm_parameters_in_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to populate BAPM Parameters !", return result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ClockStretcher)) {
		result = tonga_populate_clock_stretcher_data_table(hwmgr);
		PP_ASSERT_WITH_CODE(!result,
			"Failed to populate Clock Stretcher Data Table !",
			return result;);
	}
	table->GraphicsVoltageChangeEnable  = 1;
	table->GraphicsThermThrottleEnable  = 1;
	table->GraphicsInterval = 1;
	table->VoltageInterval  = 1;
	table->ThermalInterval  = 1;
	table->TemperatureLimitHigh =
		table_info->cac_dtp_table->usTargetOperatingTemp *
		SMU7_Q88_FORMAT_CONVERSION_UNIT;
	table->TemperatureLimitLow =
		(table_info->cac_dtp_table->usTargetOperatingTemp - 1) *
		SMU7_Q88_FORMAT_CONVERSION_UNIT;
	table->MemoryVoltageChangeEnable  = 1;
	table->MemoryInterval  = 1;
	table->VoltageResponseTime  = 0;
	table->PhaseResponseTime  = 0;
	table->MemoryThermThrottleEnable  = 1;

	/*
	* Cail reads current link status and reports it as cap (we cannot
	* change this due to some previous issues we had)
	* SMC drops the link status to lowest level after enabling
	* DPM by PowerPlay. After pnp or toggling CF, driver gets reloaded again
	* but this time Cail reads current link status which was set to low by
	* SMC and reports it as cap to powerplay
	* To avoid it, we set PCIeBootLinkLevel to highest dpm level
	*/
	PP_ASSERT_WITH_CODE((1 <= data->dpm_table.pcie_speed_table.count),
			"There must be 1 or more PCIE levels defined in PPTable.",
			return -EINVAL);

	table->PCIeBootLinkLevel = (uint8_t) (data->dpm_table.pcie_speed_table.count);

	table->PCIeGenInterval  = 1;

	result = tonga_populate_vr_config(hwmgr, table);
	PP_ASSERT_WITH_CODE(!result,
		"Failed to populate VRConfig setting !", return result);
	data->vr_config = table->VRConfig;
	table->ThermGpio  = 17;
	table->SclkStepSize = 0x4000;

	if (atomctrl_get_pp_assign_pin(hwmgr, VDDC_VRHOT_GPIO_PINID,
						&gpio_pin_assignment)) {
		table->VRHotGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot);
	} else {
		table->VRHotGpio = SMU7_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot);
	}

	if (atomctrl_get_pp_assign_pin(hwmgr, PP_AC_DC_SWITCH_GPIO_PINID,
						&gpio_pin_assignment)) {
		table->AcDcGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition);
	} else {
		table->AcDcGpio = SMU7_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition);
	}

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_Falcon_QuickTransition);

	if (0) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition);
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_Falcon_QuickTransition);
	}

	if (atomctrl_get_pp_assign_pin(hwmgr,
			THERMAL_INT_OUTPUT_GPIO_PINID, &gpio_pin_assignment)) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalOutGPIO);

		table->ThermOutGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;

		table->ThermOutPolarity =
			(0 == (cgs_read_register(hwmgr->device, mmGPIOPAD_A) &
			(1 << gpio_pin_assignment.uc_gpio_pin_bit_shift))) ? 1 : 0;

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

	for (i = 0; i < SMU72_MAX_ENTRIES_SMIO; i++)
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
	result = smu7_copy_bytes_to_smc(
			hwmgr,
			smu_data->smu7_data.dpm_table_start + offsetof(SMU72_Discrete_DpmTable, SystemFlags),
			(uint8_t *)&(table->SystemFlags),
			sizeof(SMU72_Discrete_DpmTable) - 3 * sizeof(SMU72_PIDController),
			SMC_RAM_END);

	PP_ASSERT_WITH_CODE(!result,
		"Failed to upload dpm data to SMC memory !", return result;);

	result = tonga_init_arb_table_index(hwmgr);
	PP_ASSERT_WITH_CODE(!result,
			"Failed to upload arb data to SMC memory !", return result);

	tonga_populate_pm_fuses(hwmgr);
	PP_ASSERT_WITH_CODE((!result),
		"Failed to populate initialize pm fuses !", return result);

	result = tonga_populate_initial_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((!result),
		"Failed to populate initialize MC Reg table !", return result);

	return 0;
}

static int tonga_thermal_setup_fan_table(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data =
			(struct tonga_smumgr *)(hwmgr->smu_backend);
	SMU72_Discrete_FanTable fan_table = { FDO_MODE_HARDWARE };
	uint32_t duty100;
	uint32_t t_diff1, t_diff2, pwm_diff1, pwm_diff2;
	uint16_t fdo_min, slope1, slope2;
	uint32_t reference_clock;
	int res;
	uint64_t tmp64;

	if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_MicrocodeFanControl))
		return 0;

	if (hwmgr->thermal_controller.fanInfo.bNoFan) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	if (0 == smu_data->smu7_data.fan_table_start) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	duty100 = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device,
						CGS_IND_REG__SMC,
						CG_FDO_CTRL1, FMAX_DUTY100);

	if (0 == duty100) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	tmp64 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin * duty100;
	do_div(tmp64, 10000);
	fdo_min = (uint16_t)tmp64;

	t_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usTMed -
		   hwmgr->thermal_controller.advanceFanControlParameters.usTMin;
	t_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usTHigh -
		  hwmgr->thermal_controller.advanceFanControlParameters.usTMed;

	pwm_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed -
		    hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin;
	pwm_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh -
		    hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed;

	slope1 = (uint16_t)((50 + ((16 * duty100 * pwm_diff1) / t_diff1)) / 100);
	slope2 = (uint16_t)((50 + ((16 * duty100 * pwm_diff2) / t_diff2)) / 100);

	fan_table.TempMin = cpu_to_be16((50 + hwmgr->thermal_controller.advanceFanControlParameters.usTMin) / 100);
	fan_table.TempMed = cpu_to_be16((50 + hwmgr->thermal_controller.advanceFanControlParameters.usTMed) / 100);
	fan_table.TempMax = cpu_to_be16((50 + hwmgr->thermal_controller.advanceFanControlParameters.usTMax) / 100);

	fan_table.Slope1 = cpu_to_be16(slope1);
	fan_table.Slope2 = cpu_to_be16(slope2);

	fan_table.FdoMin = cpu_to_be16(fdo_min);

	fan_table.HystDown = cpu_to_be16(hwmgr->thermal_controller.advanceFanControlParameters.ucTHyst);

	fan_table.HystUp = cpu_to_be16(1);

	fan_table.HystSlope = cpu_to_be16(1);

	fan_table.TempRespLim = cpu_to_be16(5);

	reference_clock = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

	fan_table.RefreshPeriod = cpu_to_be32((hwmgr->thermal_controller.advanceFanControlParameters.ulCycleDelay * reference_clock) / 1600);

	fan_table.FdoMax = cpu_to_be16((uint16_t)duty100);

	fan_table.TempSrc = (uint8_t)PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_MULT_THERMAL_CTRL, TEMP_SEL);

	fan_table.FanControl_GL_Flag = 1;

	res = smu7_copy_bytes_to_smc(hwmgr,
					smu_data->smu7_data.fan_table_start,
					(uint8_t *)&fan_table,
					(uint32_t)sizeof(fan_table),
					SMC_RAM_END);

	return res;
}


static int tonga_program_mem_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	if (data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_OD_UPDATE_MCLK))
		return tonga_program_memory_timing_parameters(hwmgr);

	return 0;
}

static int tonga_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data =
			(struct tonga_smumgr *)(hwmgr->smu_backend);

	int result = 0;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification)
		&& (data->low_sclk_interrupt_threshold != 0)) {
		low_sclk_interrupt_threshold =
				data->low_sclk_interrupt_threshold;

		CONVERT_FROM_HOST_TO_SMC_UL(low_sclk_interrupt_threshold);

		result = smu7_copy_bytes_to_smc(
				hwmgr,
				smu_data->smu7_data.dpm_table_start +
				offsetof(SMU72_Discrete_DpmTable,
					LowSclkInterruptThreshold),
				(uint8_t *)&low_sclk_interrupt_threshold,
				sizeof(uint32_t),
				SMC_RAM_END);
	}

	result = tonga_update_and_upload_mc_reg_table(hwmgr);

	PP_ASSERT_WITH_CODE((!result),
				"Failed to upload MC reg table !",
				return result);

	result = tonga_program_mem_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((result == 0),
			"Failed to program memory timing parameters !",
			);

	return result;
}

static uint32_t tonga_get_offsetof(uint32_t type, uint32_t member)
{
	switch (type) {
	case SMU_SoftRegisters:
		switch (member) {
		case HandshakeDisables:
			return offsetof(SMU72_SoftRegisters, HandshakeDisables);
		case VoltageChangeTimeout:
			return offsetof(SMU72_SoftRegisters, VoltageChangeTimeout);
		case AverageGraphicsActivity:
			return offsetof(SMU72_SoftRegisters, AverageGraphicsActivity);
		case AverageMemoryActivity:
			return offsetof(SMU72_SoftRegisters, AverageMemoryActivity);
		case PreVBlankGap:
			return offsetof(SMU72_SoftRegisters, PreVBlankGap);
		case VBlankTimeout:
			return offsetof(SMU72_SoftRegisters, VBlankTimeout);
		case UcodeLoadStatus:
			return offsetof(SMU72_SoftRegisters, UcodeLoadStatus);
		case DRAM_LOG_ADDR_H:
			return offsetof(SMU72_SoftRegisters, DRAM_LOG_ADDR_H);
		case DRAM_LOG_ADDR_L:
			return offsetof(SMU72_SoftRegisters, DRAM_LOG_ADDR_L);
		case DRAM_LOG_PHY_ADDR_H:
			return offsetof(SMU72_SoftRegisters, DRAM_LOG_PHY_ADDR_H);
		case DRAM_LOG_PHY_ADDR_L:
			return offsetof(SMU72_SoftRegisters, DRAM_LOG_PHY_ADDR_L);
		case DRAM_LOG_BUFF_SIZE:
			return offsetof(SMU72_SoftRegisters, DRAM_LOG_BUFF_SIZE);
		}
		break;
	case SMU_Discrete_DpmTable:
		switch (member) {
		case UvdBootLevel:
			return offsetof(SMU72_Discrete_DpmTable, UvdBootLevel);
		case VceBootLevel:
			return offsetof(SMU72_Discrete_DpmTable, VceBootLevel);
		case LowSclkInterruptThreshold:
			return offsetof(SMU72_Discrete_DpmTable, LowSclkInterruptThreshold);
		}
		break;
	}
	pr_warn("can't get the offset of type %x member %x\n", type, member);
	return 0;
}

static uint32_t tonga_get_mac_definition(uint32_t value)
{
	switch (value) {
	case SMU_MAX_LEVELS_GRAPHICS:
		return SMU72_MAX_LEVELS_GRAPHICS;
	case SMU_MAX_LEVELS_MEMORY:
		return SMU72_MAX_LEVELS_MEMORY;
	case SMU_MAX_LEVELS_LINK:
		return SMU72_MAX_LEVELS_LINK;
	case SMU_MAX_ENTRIES_SMIO:
		return SMU72_MAX_ENTRIES_SMIO;
	case SMU_MAX_LEVELS_VDDC:
		return SMU72_MAX_LEVELS_VDDC;
	case SMU_MAX_LEVELS_VDDGFX:
		return SMU72_MAX_LEVELS_VDDGFX;
	case SMU_MAX_LEVELS_VDDCI:
		return SMU72_MAX_LEVELS_VDDCI;
	case SMU_MAX_LEVELS_MVDD:
		return SMU72_MAX_LEVELS_MVDD;
	}
	pr_warn("can't get the mac value %x\n", value);

	return 0;
}

static int tonga_update_uvd_smc_table(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	smu_data->smc_state_table.UvdBootLevel = 0;
	if (table_info->mm_dep_table->count > 0)
		smu_data->smc_state_table.UvdBootLevel =
				(uint8_t) (table_info->mm_dep_table->count - 1);
	mm_boot_level_offset = smu_data->smu7_data.dpm_table_start +
				offsetof(SMU72_Discrete_DpmTable, UvdBootLevel);
	mm_boot_level_offset /= 4;
	mm_boot_level_offset *= 4;
	mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, mm_boot_level_offset);
	mm_boot_level_value &= 0x00FFFFFF;
	mm_boot_level_value |= smu_data->smc_state_table.UvdBootLevel << 24;
	cgs_write_ind_register(hwmgr->device,
				CGS_IND_REG__SMC,
				mm_boot_level_offset, mm_boot_level_value);

	if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UVDDPM) ||
		phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StablePState))
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_UVDDPM_SetEnabledMask,
				(uint32_t)(1 << smu_data->smc_state_table.UvdBootLevel),
				NULL);
	return 0;
}

static int tonga_update_vce_smc_table(struct pp_hwmgr *hwmgr)
{
	struct tonga_smumgr *smu_data =
				(struct tonga_smumgr *)(hwmgr->smu_backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);


	smu_data->smc_state_table.VceBootLevel =
		(uint8_t) (table_info->mm_dep_table->count - 1);

	mm_boot_level_offset = smu_data->smu7_data.dpm_table_start +
					offsetof(SMU72_Discrete_DpmTable, VceBootLevel);
	mm_boot_level_offset /= 4;
	mm_boot_level_offset *= 4;
	mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, mm_boot_level_offset);
	mm_boot_level_value &= 0xFF00FFFF;
	mm_boot_level_value |= smu_data->smc_state_table.VceBootLevel << 16;
	cgs_write_ind_register(hwmgr->device,
			CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_StablePState))
		smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_VCEDPM_SetEnabledMask,
				(uint32_t)1 << smu_data->smc_state_table.VceBootLevel,
				NULL);
	return 0;
}

static int tonga_update_smc_table(struct pp_hwmgr *hwmgr, uint32_t type)
{
	switch (type) {
	case SMU_UVD_TABLE:
		tonga_update_uvd_smc_table(hwmgr);
		break;
	case SMU_VCE_TABLE:
		tonga_update_vce_smc_table(hwmgr);
		break;
	default:
		break;
	}
	return 0;
}

static int tonga_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);

	uint32_t tmp;
	int result;
	bool error = false;

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, DpmTable),
				&tmp, SMC_RAM_END);

	if (!result)
		smu_data->smu7_data.dpm_table_start = tmp;

	error |= (result != 0);

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, SoftRegisters),
				&tmp, SMC_RAM_END);

	if (!result) {
		data->soft_regs_start = tmp;
		smu_data->smu7_data.soft_regs_start = tmp;
	}

	error |= (result != 0);


	result = smu7_read_smc_sram_dword(hwmgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, mcRegisterTable),
				&tmp, SMC_RAM_END);

	if (!result)
		smu_data->smu7_data.mc_reg_table_start = tmp;

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, FanTable),
				&tmp, SMC_RAM_END);

	if (!result)
		smu_data->smu7_data.fan_table_start = tmp;

	error |= (result != 0);

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, mcArbDramTimingTable),
				&tmp, SMC_RAM_END);

	if (!result)
		smu_data->smu7_data.arb_table_start = tmp;

	error |= (result != 0);

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, Version),
				&tmp, SMC_RAM_END);

	if (!result)
		hwmgr->microcode_version_info.SMC = tmp;

	error |= (result != 0);

	return error ? 1 : 0;
}

/*---------------------------MC----------------------------*/

static uint8_t tonga_get_memory_modile_index(struct pp_hwmgr *hwmgr)
{
	return (uint8_t) (0xFF & (cgs_read_register(hwmgr->device, mmBIOS_SCRATCH_4) >> 16));
}

static bool tonga_check_s0_mc_reg_index(uint16_t in_reg, uint16_t *out_reg)
{
	bool result = true;

	switch (in_reg) {
	case  mmMC_SEQ_RAS_TIMING:
		*out_reg = mmMC_SEQ_RAS_TIMING_LP;
		break;

	case  mmMC_SEQ_DLL_STBY:
		*out_reg = mmMC_SEQ_DLL_STBY_LP;
		break;

	case  mmMC_SEQ_G5PDX_CMD0:
		*out_reg = mmMC_SEQ_G5PDX_CMD0_LP;
		break;

	case  mmMC_SEQ_G5PDX_CMD1:
		*out_reg = mmMC_SEQ_G5PDX_CMD1_LP;
		break;

	case  mmMC_SEQ_G5PDX_CTRL:
		*out_reg = mmMC_SEQ_G5PDX_CTRL_LP;
		break;

	case mmMC_SEQ_CAS_TIMING:
		*out_reg = mmMC_SEQ_CAS_TIMING_LP;
		break;

	case mmMC_SEQ_MISC_TIMING:
		*out_reg = mmMC_SEQ_MISC_TIMING_LP;
		break;

	case mmMC_SEQ_MISC_TIMING2:
		*out_reg = mmMC_SEQ_MISC_TIMING2_LP;
		break;

	case mmMC_SEQ_PMG_DVS_CMD:
		*out_reg = mmMC_SEQ_PMG_DVS_CMD_LP;
		break;

	case mmMC_SEQ_PMG_DVS_CTL:
		*out_reg = mmMC_SEQ_PMG_DVS_CTL_LP;
		break;

	case mmMC_SEQ_RD_CTL_D0:
		*out_reg = mmMC_SEQ_RD_CTL_D0_LP;
		break;

	case mmMC_SEQ_RD_CTL_D1:
		*out_reg = mmMC_SEQ_RD_CTL_D1_LP;
		break;

	case mmMC_SEQ_WR_CTL_D0:
		*out_reg = mmMC_SEQ_WR_CTL_D0_LP;
		break;

	case mmMC_SEQ_WR_CTL_D1:
		*out_reg = mmMC_SEQ_WR_CTL_D1_LP;
		break;

	case mmMC_PMG_CMD_EMRS:
		*out_reg = mmMC_SEQ_PMG_CMD_EMRS_LP;
		break;

	case mmMC_PMG_CMD_MRS:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS_LP;
		break;

	case mmMC_PMG_CMD_MRS1:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS1_LP;
		break;

	case mmMC_SEQ_PMG_TIMING:
		*out_reg = mmMC_SEQ_PMG_TIMING_LP;
		break;

	case mmMC_PMG_CMD_MRS2:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS2_LP;
		break;

	case mmMC_SEQ_WR_CTL_2:
		*out_reg = mmMC_SEQ_WR_CTL_2_LP;
		break;

	default:
		result = false;
		break;
	}

	return result;
}

static int tonga_set_s0_mc_reg_index(struct tonga_mc_reg_table *table)
{
	uint32_t i;
	uint16_t address;

	for (i = 0; i < table->last; i++) {
		table->mc_reg_address[i].s0 =
			tonga_check_s0_mc_reg_index(table->mc_reg_address[i].s1,
							&address) ?
							address :
						 table->mc_reg_address[i].s1;
	}
	return 0;
}

static int tonga_copy_vbios_smc_reg_table(const pp_atomctrl_mc_reg_table *table,
					struct tonga_mc_reg_table *ni_table)
{
	uint8_t i, j;

	PP_ASSERT_WITH_CODE((table->last <= SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
		"Invalid VramInfo table.", return -EINVAL);
	PP_ASSERT_WITH_CODE((table->num_entries <= MAX_AC_TIMING_ENTRIES),
		"Invalid VramInfo table.", return -EINVAL);

	for (i = 0; i < table->last; i++)
		ni_table->mc_reg_address[i].s1 = table->mc_reg_address[i].s1;

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

static int tonga_set_mc_special_registers(struct pp_hwmgr *hwmgr,
					struct tonga_mc_reg_table *table)
{
	uint8_t i, j, k;
	uint32_t temp_reg;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	for (i = 0, j = table->last; i < table->last; i++) {
		PP_ASSERT_WITH_CODE((j < SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
			"Invalid VramInfo table.", return -EINVAL);

		switch (table->mc_reg_address[i].s1) {

		case mmMC_SEQ_MISC1:
			temp_reg = cgs_read_register(hwmgr->device,
							mmMC_PMG_CMD_EMRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_EMRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_EMRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					((temp_reg & 0xffff0000)) |
					((table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16);
			}
			j++;

			PP_ASSERT_WITH_CODE((j < SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
				"Invalid VramInfo table.", return -EINVAL);
			temp_reg = cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_MRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_MRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);

				if (!data->is_memory_gddr5)
					table->mc_reg_table_entry[k].mc_data[j] |= 0x100;
			}
			j++;

			if (!data->is_memory_gddr5) {
				PP_ASSERT_WITH_CODE((j < SMU72_DISCRETE_MC_REGISTER_ARRAY_SIZE),
					"Invalid VramInfo table.", return -EINVAL);
				table->mc_reg_address[j].s1 = mmMC_PMG_AUTO_CMD;
				table->mc_reg_address[j].s0 = mmMC_PMG_AUTO_CMD;
				for (k = 0; k < table->num_entries; k++)
					table->mc_reg_table_entry[k].mc_data[j] =
						(table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16;
				j++;
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
			break;

		default:
			break;
		}

	}

	table->last = j;

	return 0;
}

static int tonga_set_valid_flag(struct tonga_mc_reg_table *table)
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

static int tonga_initialize_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)(hwmgr->smu_backend);
	pp_atomctrl_mc_reg_table *table;
	struct tonga_mc_reg_table *ni_table = &smu_data->mc_reg_table;
	uint8_t module_index = tonga_get_memory_modile_index(hwmgr);

	table = kzalloc(sizeof(pp_atomctrl_mc_reg_table), GFP_KERNEL);

	if (table == NULL)
		return -ENOMEM;

	/* Program additional LP registers that are no longer programmed by VBIOS */
	cgs_write_register(hwmgr->device, mmMC_SEQ_RAS_TIMING_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_RAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_CAS_TIMING_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_CAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_DLL_STBY_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_DLL_STBY));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD0_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD1_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CTRL_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CTRL));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CMD_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CMD));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CTL_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CTL));
	cgs_write_register(hwmgr->device, mmMC_SEQ_MISC_TIMING_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_MISC_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_EMRS_LP,
			cgs_read_register(hwmgr->device, mmMC_PMG_CMD_EMRS));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS_LP,
			cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS1_LP,
			cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_D0_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_D0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_TIMING_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS2_LP,
			cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS2));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_2_LP,
			cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_2));

	result = atomctrl_initialize_mc_reg_table(hwmgr, module_index, table);

	if (!result)
		result = tonga_copy_vbios_smc_reg_table(table, ni_table);

	if (!result) {
		tonga_set_s0_mc_reg_index(ni_table);
		result = tonga_set_mc_special_registers(hwmgr, ni_table);
	}

	if (!result)
		tonga_set_valid_flag(ni_table);

	kfree(table);

	return result;
}

static bool tonga_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	return (1 == PHM_READ_INDIRECT_FIELD(hwmgr->device,
			CGS_IND_REG__SMC, FEATURE_STATUS, VOLTAGE_CONTROLLER_ON))
			? true : false;
}

static int tonga_update_dpm_settings(struct pp_hwmgr *hwmgr,
				void *profile_setting)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct tonga_smumgr *smu_data = (struct tonga_smumgr *)
			(hwmgr->smu_backend);
	struct profile_mode_setting *setting;
	struct SMU72_Discrete_GraphicsLevel *levels =
			smu_data->smc_state_table.GraphicsLevel;
	uint32_t array = smu_data->smu7_data.dpm_table_start +
			offsetof(SMU72_Discrete_DpmTable, GraphicsLevel);

	uint32_t mclk_array = smu_data->smu7_data.dpm_table_start +
			offsetof(SMU72_Discrete_DpmTable, MemoryLevel);
	struct SMU72_Discrete_MemoryLevel *mclk_levels =
			smu_data->smc_state_table.MemoryLevel;
	uint32_t i;
	uint32_t offset, up_hyst_offset, down_hyst_offset, clk_activity_offset, tmp;

	if (profile_setting == NULL)
		return -EINVAL;

	setting = (struct profile_mode_setting *)profile_setting;

	if (setting->bupdate_sclk) {
		if (!data->sclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_SCLKDPM_FreezeLevel, NULL);
		for (i = 0; i < smu_data->smc_state_table.GraphicsDpmLevelCount; i++) {
			if (levels[i].ActivityLevel !=
				cpu_to_be16(setting->sclk_activity)) {
				levels[i].ActivityLevel = cpu_to_be16(setting->sclk_activity);

				clk_activity_offset = array + (sizeof(SMU72_Discrete_GraphicsLevel) * i)
						+ offsetof(SMU72_Discrete_GraphicsLevel, ActivityLevel);
				offset = clk_activity_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(clk_activity_offset, tmp, levels[i].ActivityLevel, sizeof(uint16_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));

			}
			if (levels[i].UpHyst != setting->sclk_up_hyst ||
				levels[i].DownHyst != setting->sclk_down_hyst) {
				levels[i].UpHyst = setting->sclk_up_hyst;
				levels[i].DownHyst = setting->sclk_down_hyst;
				up_hyst_offset = array + (sizeof(SMU72_Discrete_GraphicsLevel) * i)
						+ offsetof(SMU72_Discrete_GraphicsLevel, UpHyst);
				down_hyst_offset = array + (sizeof(SMU72_Discrete_GraphicsLevel) * i)
						+ offsetof(SMU72_Discrete_GraphicsLevel, DownHyst);
				offset = up_hyst_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(up_hyst_offset, tmp, levels[i].UpHyst, sizeof(uint8_t));
				tmp = phm_set_field_to_u32(down_hyst_offset, tmp, levels[i].DownHyst, sizeof(uint8_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));
			}
		}
		if (!data->sclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_SCLKDPM_UnfreezeLevel, NULL);
	}

	if (setting->bupdate_mclk) {
		if (!data->mclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_MCLKDPM_FreezeLevel, NULL);
		for (i = 0; i < smu_data->smc_state_table.MemoryDpmLevelCount; i++) {
			if (mclk_levels[i].ActivityLevel !=
				cpu_to_be16(setting->mclk_activity)) {
				mclk_levels[i].ActivityLevel = cpu_to_be16(setting->mclk_activity);

				clk_activity_offset = mclk_array + (sizeof(SMU72_Discrete_MemoryLevel) * i)
						+ offsetof(SMU72_Discrete_MemoryLevel, ActivityLevel);
				offset = clk_activity_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(clk_activity_offset, tmp, mclk_levels[i].ActivityLevel, sizeof(uint16_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));

			}
			if (mclk_levels[i].UpHyst != setting->mclk_up_hyst ||
				mclk_levels[i].DownHyst != setting->mclk_down_hyst) {
				mclk_levels[i].UpHyst = setting->mclk_up_hyst;
				mclk_levels[i].DownHyst = setting->mclk_down_hyst;
				up_hyst_offset = mclk_array + (sizeof(SMU72_Discrete_MemoryLevel) * i)
						+ offsetof(SMU72_Discrete_MemoryLevel, UpHyst);
				down_hyst_offset = mclk_array + (sizeof(SMU72_Discrete_MemoryLevel) * i)
						+ offsetof(SMU72_Discrete_MemoryLevel, DownHyst);
				offset = up_hyst_offset & ~0x3;
				tmp = PP_HOST_TO_SMC_UL(cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset));
				tmp = phm_set_field_to_u32(up_hyst_offset, tmp, mclk_levels[i].UpHyst, sizeof(uint8_t));
				tmp = phm_set_field_to_u32(down_hyst_offset, tmp, mclk_levels[i].DownHyst, sizeof(uint8_t));
				cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset, PP_HOST_TO_SMC_UL(tmp));
			}
		}
		if (!data->mclk_dpm_key_disabled)
			smum_send_msg_to_smc(hwmgr, PPSMC_MSG_MCLKDPM_UnfreezeLevel, NULL);
	}
	return 0;
}

const struct pp_smumgr_func tonga_smu_funcs = {
	.name = "tonga_smu",
	.smu_init = &tonga_smu_init,
	.smu_fini = &smu7_smu_fini,
	.start_smu = &tonga_start_smu,
	.check_fw_load_finish = &smu7_check_fw_load_finish,
	.request_smu_load_fw = &smu7_request_smu_load_fw,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &smu7_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu7_send_msg_to_smc_with_parameter,
	.get_argument = smu7_get_argument,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.update_smc_table = tonga_update_smc_table,
	.get_offsetof = tonga_get_offsetof,
	.process_firmware_header = tonga_process_firmware_header,
	.init_smc_table = tonga_init_smc_table,
	.update_sclk_threshold = tonga_update_sclk_threshold,
	.thermal_setup_fan_table = tonga_thermal_setup_fan_table,
	.populate_all_graphic_levels = tonga_populate_all_graphic_levels,
	.populate_all_memory_levels = tonga_populate_all_memory_levels,
	.get_mac_definition = tonga_get_mac_definition,
	.initialize_mc_reg_table = tonga_initialize_mc_reg_table,
	.is_dpm_running = tonga_is_dpm_running,
	.update_dpm_settings = tonga_update_dpm_settings,
};
