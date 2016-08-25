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
 * Author: Huang Rui <ray.huang@amd.com>
 *
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include "linux/delay.h"
#include "pp_acpi.h"
#include "hwmgr.h"
#include <atombios.h>
#include "iceland_hwmgr.h"
#include "pptable.h"
#include "processpptables.h"
#include "pp_debug.h"
#include "ppsmc.h"
#include "cgs_common.h"
#include "pppcielanes.h"
#include "iceland_dyn_defaults.h"
#include "smumgr.h"
#include "iceland_smumgr.h"
#include "iceland_clockpowergating.h"
#include "iceland_thermal.h"
#include "iceland_powertune.h"

#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#include "smu/smu_7_1_1_d.h"
#include "smu/smu_7_1_1_sh_mask.h"

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

#define SMC_RAM_END		    0x40000
#define SMC_CG_IND_START            0xc0030000
#define SMC_CG_IND_END              0xc0040000  /* First byte after SMC_CG_IND*/

#define VOLTAGE_SCALE               4
#define VOLTAGE_VID_OFFSET_SCALE1   625
#define VOLTAGE_VID_OFFSET_SCALE2   100

const uint32_t iceland_magic = (uint32_t)(PHM_VIslands_Magic);

#define MC_SEQ_MISC0_GDDR5_SHIFT 28
#define MC_SEQ_MISC0_GDDR5_MASK  0xf0000000
#define MC_SEQ_MISC0_GDDR5_VALUE 5

/** Values for the CG_THERMAL_CTRL::DPM_EVENT_SRC field. */
enum DPM_EVENT_SRC {
    DPM_EVENT_SRC_ANALOG = 0,               /* Internal analog trip point */
    DPM_EVENT_SRC_EXTERNAL = 1,             /* External (GPIO 17) signal */
    DPM_EVENT_SRC_DIGITAL = 2,              /* Internal digital trip point (DIG_THERM_DPM) */
    DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,   /* Internal analog or external */
    DPM_EVENT_SRC_DIGITAL_OR_EXTERNAL = 4   /* Internal digital or external */
};

static int iceland_read_clock_registers(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

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
int iceland_get_memory_type(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	uint32_t temp;

	temp = cgs_read_register(hwmgr->device, mmMC_SEQ_MISC0);

	data->is_memory_GDDR5 = (MC_SEQ_MISC0_GDDR5_VALUE ==
			((temp & MC_SEQ_MISC0_GDDR5_MASK) >>
			 MC_SEQ_MISC0_GDDR5_SHIFT));

	return 0;
}

int iceland_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	/* iceland does not have MM hardware blocks */
	return 0;
}

/**
 * Enables Dynamic Power Management by SMC
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int iceland_enable_acpi_power_management(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT, STATIC_PM_EN, 1);

	return 0;
}

/**
 * Find the MC microcode version and store it in the HwMgr struct
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int iceland_get_mc_microcode_version(struct pp_hwmgr *hwmgr)
{
	cgs_write_register(hwmgr->device, mmMC_SEQ_IO_DEBUG_INDEX, 0x9F);

	hwmgr->microcode_version_info.MC = cgs_read_register(hwmgr->device, mmMC_SEQ_IO_DEBUG_DATA);

	return 0;
}

static int iceland_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	data->low_sclk_interrupt_threshold = 0;

	return 0;
}


static int iceland_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	tmp_result = iceland_read_clock_registers(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to read clock registers!", result = tmp_result);

	tmp_result = iceland_get_memory_type(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to get memory type!", result = tmp_result);

	tmp_result = iceland_enable_acpi_power_management(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable ACPI power management!", result = tmp_result);

	tmp_result = iceland_get_mc_microcode_version(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to get MC microcode version!", result = tmp_result);

	tmp_result = iceland_init_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to init sclk threshold!", result = tmp_result);

	return result;
}

static bool cf_iceland_voltage_control(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	return ICELAND_VOLTAGE_CONTROL_NONE != data->voltage_control;
}

/*
 * -------------- Voltage Tables ----------------------
 * If the voltage table would be bigger than what will fit into the
 * state table on the SMC keep only the higher entries.
 */

static void iceland_trim_voltage_table_to_fit_state_table(
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
 * Enable voltage control
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int iceland_enable_voltage_control(struct pp_hwmgr *hwmgr)
{
	/* enable voltage control */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT, VOLT_PWRMGT_EN, 1);

	return 0;
}

static int iceland_get_svi2_voltage_table(struct pp_hwmgr *hwmgr,
		struct phm_clock_voltage_dependency_table *voltage_dependency_table,
		pp_atomctrl_voltage_table *voltage_table)
{
	uint32_t i;

	PP_ASSERT_WITH_CODE((NULL != voltage_table),
			"Voltage Dependency Table empty.", return -EINVAL;);

	voltage_table->mask_low = 0;
	voltage_table->phase_delay = 0;
	voltage_table->count = voltage_dependency_table->count;

	for (i = 0; i < voltage_dependency_table->count; i++) {
		voltage_table->entries[i].value =
			voltage_dependency_table->entries[i].v;
		voltage_table->entries[i].smio_low = 0;
	}

	return 0;
}

/**
 * Create Voltage Tables.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int iceland_construct_voltage_tables(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	int result;

	/* GPIO voltage */
	if (ICELAND_VOLTAGE_CONTROL_BY_GPIO == data->voltage_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
					VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_GPIO_LUT,
					&data->vddc_voltage_table);
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve VDDC table.", return result;);
	} else if (ICELAND_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		/* SVI2 VDDC voltage */
		result = iceland_get_svi2_voltage_table(hwmgr,
					hwmgr->dyn_state.vddc_dependency_on_mclk,
					&data->vddc_voltage_table);
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve SVI2 VDDC table from dependancy table.", return result;);
	}

	PP_ASSERT_WITH_CODE(
			(data->vddc_voltage_table.count <= (SMU71_MAX_LEVELS_VDDC)),
			"Too many voltage values for VDDC. Trimming to fit state table.",
			iceland_trim_voltage_table_to_fit_state_table(hwmgr,
			SMU71_MAX_LEVELS_VDDC, &(data->vddc_voltage_table));
			);

	/* GPIO */
	if (ICELAND_VOLTAGE_CONTROL_BY_GPIO == data->vdd_ci_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
					VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT, &(data->vddci_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve VDDCI table.", return result;);
	}

	/* SVI2 VDDCI voltage */
	if (ICELAND_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_ci_control) {
		result = iceland_get_svi2_voltage_table(hwmgr,
					hwmgr->dyn_state.vddci_dependency_on_mclk,
					&data->vddci_voltage_table);
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve SVI2 VDDCI table from dependancy table.", return result;);
	}

	PP_ASSERT_WITH_CODE(
			(data->vddci_voltage_table.count <= (SMU71_MAX_LEVELS_VDDCI)),
			"Too many voltage values for VDDCI. Trimming to fit state table.",
			iceland_trim_voltage_table_to_fit_state_table(hwmgr,
			SMU71_MAX_LEVELS_VDDCI, &(data->vddci_voltage_table));
			);


	/* GPIO */
	if (ICELAND_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
					VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT, &(data->mvdd_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve table.", return result;);
	}

	/* SVI2 voltage control */
	if (ICELAND_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control) {
		result = iceland_get_svi2_voltage_table(hwmgr,
					hwmgr->dyn_state.mvdd_dependency_on_mclk,
					&data->mvdd_voltage_table);
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to retrieve SVI2 MVDD table from dependancy table.", return result;);
	}

	PP_ASSERT_WITH_CODE(
			(data->mvdd_voltage_table.count <= (SMU71_MAX_LEVELS_MVDD)),
			"Too many voltage values for MVDD. Trimming to fit state table.",
			iceland_trim_voltage_table_to_fit_state_table(hwmgr,
			SMU71_MAX_LEVELS_MVDD, &(data->mvdd_voltage_table));
			);

	return 0;
}

/*---------------------------MC----------------------------*/

uint8_t iceland_get_memory_module_index(struct pp_hwmgr *hwmgr)
{
	return (uint8_t) (0xFF & (cgs_read_register(hwmgr->device, mmBIOS_SCRATCH_4) >> 16));
}

bool iceland_check_s0_mc_reg_index(uint16_t inReg, uint16_t *outReg)
{
	bool result = true;

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
		result = false;
		break;
	}

	return result;
}

int iceland_set_s0_mc_reg_index(phw_iceland_mc_reg_table *table)
{
	uint32_t i;
	uint16_t address;

	for (i = 0; i < table->last; i++) {
		table->mc_reg_address[i].s0 =
			iceland_check_s0_mc_reg_index(table->mc_reg_address[i].s1, &address)
			? address : table->mc_reg_address[i].s1;
	}
	return 0;
}

int iceland_copy_vbios_smc_reg_table(const pp_atomctrl_mc_reg_table *table, phw_iceland_mc_reg_table *ni_table)
{
	uint8_t i, j;

	PP_ASSERT_WITH_CODE((table->last <= SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
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
static int iceland_set_mc_special_registers(struct pp_hwmgr *hwmgr, phw_iceland_mc_reg_table *table)
{
	uint8_t i, j, k;
	uint32_t temp_reg;
	const iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	for (i = 0, j = table->last; i < table->last; i++) {
		PP_ASSERT_WITH_CODE((j < SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
			"Invalid VramInfo table.", return -1);
		switch (table->mc_reg_address[i].s1) {
		/*
		 * mmMC_SEQ_MISC1, bit[31:16] EMRS1, need to be write
		 * to mmMC_PMG_CMD_EMRS/_LP[15:0]. Bit[15:0] MRS, need
		 * to be update mmMC_PMG_CMD_MRS/_LP[15:0]
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
			PP_ASSERT_WITH_CODE((j < SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
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
			PP_ASSERT_WITH_CODE((j <= SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
				"Invalid VramInfo table.", return -1);

			if (!data->is_memory_GDDR5) {
				table->mc_reg_address[j].s1 = mmMC_PMG_AUTO_CMD;
				table->mc_reg_address[j].s0 = mmMC_PMG_AUTO_CMD;
				for (k = 0; k < table->num_entries; k++) {
					table->mc_reg_table_entry[k].mc_data[j] =
						(table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16;
				}
				j++;
				PP_ASSERT_WITH_CODE((j <= SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
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
			PP_ASSERT_WITH_CODE((j <= SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
				"Invalid VramInfo table.", return -1);
			break;

		default:
			break;
		}

	}

	table->last = j;

	return 0;
}


static int iceland_set_valid_flag(phw_iceland_mc_reg_table *table)
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

static int iceland_initialize_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	pp_atomctrl_mc_reg_table *table;
	phw_iceland_mc_reg_table *ni_table = &data->iceland_mc_reg_table;
	uint8_t module_index = iceland_get_memory_module_index(hwmgr);

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
		result = iceland_copy_vbios_smc_reg_table(table, ni_table);

	if (0 == result) {
		iceland_set_s0_mc_reg_index(ni_table);
		result = iceland_set_mc_special_registers(hwmgr, ni_table);
	}

	if (0 == result)
		iceland_set_valid_flag(ni_table);

	kfree(table);
	return result;
}

/**
 * Programs static screed detection parameters
 *
 * @param   hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int iceland_program_static_screen_threshold_parameters(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

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
int iceland_enable_display_gap(struct pp_hwmgr *hwmgr)
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
int iceland_program_voting_clients(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

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

static int iceland_upload_firmware(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	if (!iceland_is_smc_ram_running(hwmgr->smumgr))
		ret = iceland_smu_upload_firmware_image(hwmgr->smumgr);

	return ret;
}

/**
 * Get the location of various tables inside the FW image.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int iceland_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	uint32_t tmp;
	int result;
	bool error = 0;

	result = iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, DpmTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->dpm_table_start = tmp;
	}

	error |= (0 != result);

	result = iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, SoftRegisters),
				&tmp, data->sram_end);

	if (0 == result) {
		data->soft_regs_start = tmp;
	}

	error |= (0 != result);


	result = iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, mcRegisterTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->mc_reg_table_start = tmp;
	}

	result = iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, FanTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->fan_table_start = tmp;
	}

	error |= (0 != result);

	result = iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, mcArbDramTimingTable),
				&tmp, data->sram_end);

	if (0 == result) {
		data->arb_table_start = tmp;
	}

	error |= (0 != result);


	result = iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, Version),
				&tmp, data->sram_end);

	if (0 == result) {
		hwmgr->microcode_version_info.SMC = tmp;
	}

	error |= (0 != result);

	result = iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, UlvSettings),
				&tmp, data->sram_end);

	if (0 == result) {
		data->ulv_settings_start = tmp;
	}

	error |= (0 != result);

	return error ? 1 : 0;
}

/*
* Copy one arb setting to another and then switch the active set.
* arbFreqSrc and arbFreqDest is one of the MC_CG_ARB_FREQ_Fx constants.
*/
int iceland_copy_and_switch_arb_sets(struct pp_hwmgr *hwmgr,
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
int iceland_initial_switch_from_arb_f0_to_f1(struct pp_hwmgr *hwmgr)
{
	return iceland_copy_and_switch_arb_sets(hwmgr, MC_CG_ARB_FREQ_F0, MC_CG_ARB_FREQ_F1);
}

/* ---------------------------------------- ULV related functions ----------------------------------------------------*/


static int iceland_reset_single_dpm_table(
	struct pp_hwmgr *hwmgr,
	struct iceland_single_dpm_table *dpm_table,
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

static void iceland_setup_pcie_table_entry(
	struct iceland_single_dpm_table *dpm_table,
	uint32_t index, uint32_t pcie_gen,
	uint32_t pcie_lanes)
{
	dpm_table->dpm_levels[index].value = pcie_gen;
	dpm_table->dpm_levels[index].param1 = pcie_lanes;
	dpm_table->dpm_levels[index].enabled = 1;
}

/*
 * Set up the PCIe DPM table as follows:
 *
 * A  = Performance State, Max, Gen Speed
 * C  = Performance State, Min, Gen Speed
 * 1  = Performance State, Max, Lane #
 * 3  = Performance State, Min, Lane #
 *
 * B  = Power Saving State, Max, Gen Speed
 * D  = Power Saving State, Min, Gen Speed
 * 2  = Power Saving State, Max, Lane #
 * 4  = Power Saving State, Min, Lane #
 *
 *
 * DPM Index   Gen Speed   Lane #
 * 5           A           1
 * 4           B           2
 * 3           C           1
 * 2           D           2
 * 1           C           3
 * 0           D           4
 *
 */
static int iceland_setup_default_pcie_tables(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	PP_ASSERT_WITH_CODE((data->use_pcie_performance_levels ||
				data->use_pcie_power_saving_levels),
			"No pcie performance levels!", return -EINVAL);

	if (data->use_pcie_performance_levels && !data->use_pcie_power_saving_levels) {
		data->pcie_gen_power_saving = data->pcie_gen_performance;
		data->pcie_lane_power_saving = data->pcie_lane_performance;
	} else if (!data->use_pcie_performance_levels && data->use_pcie_power_saving_levels) {
		data->pcie_gen_performance = data->pcie_gen_power_saving;
		data->pcie_lane_performance = data->pcie_lane_power_saving;
	}

	iceland_reset_single_dpm_table(hwmgr, &data->dpm_table.pcie_speed_table, SMU71_MAX_LEVELS_LINK);

	/* Hardcode Pcie Table */
	iceland_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 0,
		get_pcie_gen_support(data->pcie_gen_cap, PP_Min_PCIEGen),
		get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
	iceland_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 1,
		get_pcie_gen_support(data->pcie_gen_cap, PP_Min_PCIEGen),
		get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
	iceland_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 2,
		get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
		get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
	iceland_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 3,
		get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
		get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
	iceland_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 4,
		get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
		get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
	iceland_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 5,
		get_pcie_gen_support(data->pcie_gen_cap, PP_Max_PCIEGen),
		get_pcie_lane_support(data->pcie_lane_cap, PP_Max_PCIELane));
	data->dpm_table.pcie_speed_table.count = 6;

	return 0;

}


/*
 * This function is to initalize all DPM state tables for SMU7 based on the dependency table.
 * Dynamic state patching function will then trim these state tables to the allowed range based
 * on the power policy or external client requests, such as UVD request, etc.
 */
static int iceland_setup_default_dpm_tables(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	uint32_t i;

	struct phm_clock_voltage_dependency_table *allowed_vdd_sclk_table =
		hwmgr->dyn_state.vddc_dependency_on_sclk;
	struct phm_clock_voltage_dependency_table *allowed_vdd_mclk_table =
		hwmgr->dyn_state.vddc_dependency_on_mclk;
	struct phm_cac_leakage_table *std_voltage_table =
		hwmgr->dyn_state.cac_leakage_table;

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
	iceland_reset_single_dpm_table(hwmgr, &data->dpm_table.sclk_table, SMU71_MAX_LEVELS_GRAPHICS);
	iceland_reset_single_dpm_table(hwmgr, &data->dpm_table.mclk_table, SMU71_MAX_LEVELS_MEMORY);
	iceland_reset_single_dpm_table(hwmgr, &data->dpm_table.vddc_table, SMU71_MAX_LEVELS_VDDC);
	iceland_reset_single_dpm_table(hwmgr, &data->dpm_table.vdd_ci_table, SMU71_MAX_LEVELS_VDDCI);
	iceland_reset_single_dpm_table(hwmgr, &data->dpm_table.mvdd_table, SMU71_MAX_LEVELS_MVDD);

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
		data->dpm_table.vddc_table.dpm_levels[i].value = allowed_vdd_mclk_table->entries[i].v;
		data->dpm_table.vddc_table.dpm_levels[i].param1 = std_voltage_table->entries[i].Leakage;
		/* param1 is for corresponding std voltage */
		data->dpm_table.vddc_table.dpm_levels[i].enabled = 1;
	}

	data->dpm_table.vddc_table.count = allowed_vdd_sclk_table->count;
	allowed_vdd_mclk_table = hwmgr->dyn_state.vddci_dependency_on_mclk;

	if (NULL != allowed_vdd_mclk_table) {
		/* Initialize Vddci DPM table based on allow Mclk values */
		for (i = 0; i < allowed_vdd_mclk_table->count; i++) {
			data->dpm_table.vdd_ci_table.dpm_levels[i].value = allowed_vdd_mclk_table->entries[i].v;
			data->dpm_table.vdd_ci_table.dpm_levels[i].enabled = 1;
		}
		data->dpm_table.vdd_ci_table.count = allowed_vdd_mclk_table->count;
	}

	allowed_vdd_mclk_table = hwmgr->dyn_state.mvdd_dependency_on_mclk;

	if (NULL != allowed_vdd_mclk_table) {
		/*
		 * Initialize MVDD DPM table based on allow Mclk
		 * values
		 */
		for (i = 0; i < allowed_vdd_mclk_table->count; i++) {
			data->dpm_table.mvdd_table.dpm_levels[i].value = allowed_vdd_mclk_table->entries[i].v;
			data->dpm_table.mvdd_table.dpm_levels[i].enabled = 1;
		}
		data->dpm_table.mvdd_table.count = allowed_vdd_mclk_table->count;
	}

	/* setup PCIE gen speed levels*/
	iceland_setup_default_pcie_tables(hwmgr);

	/* save a copy of the default DPM table*/
	memcpy(&(data->golden_dpm_table), &(data->dpm_table), sizeof(struct iceland_dpm_table));

	return 0;
}

/**
 * @brief PhwIceland_GetVoltageOrder
 *  Returns index of requested voltage record in lookup(table)
 * @param hwmgr - pointer to hardware manager
 * @param lookutab - lookup list to search in
 * @param voltage - voltage to look for
 * @return 0 on success
 */
uint8_t iceland_get_voltage_index(phm_ppt_v1_voltage_lookup_table *look_up_table,
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


static int iceland_get_std_voltage_value_sidd(struct pp_hwmgr *hwmgr,
		pp_atomctrl_voltage_table_entry *tab, uint16_t *hi,
		uint16_t *lo)
{
	uint16_t v_index;
	bool vol_found = false;
	*hi = tab->value * VOLTAGE_SCALE;
	*lo = tab->value * VOLTAGE_SCALE;

	/* SCLK/VDDC Dependency Table has to exist. */
	PP_ASSERT_WITH_CODE(NULL != hwmgr->dyn_state.vddc_dependency_on_sclk,
	                    "The SCLK/VDDC Dependency Table does not exist.\n",
	                    return -EINVAL);

	if (NULL == hwmgr->dyn_state.cac_leakage_table) {
		pr_warning("CAC Leakage Table does not exist, using vddc.\n");
		return 0;
	}

	/*
	 * Since voltage in the sclk/vddc dependency table is not
	 * necessarily in ascending order because of ELB voltage
	 * patching, loop through entire list to find exact voltage.
	 */
	for (v_index = 0; (uint32_t)v_index < hwmgr->dyn_state.vddc_dependency_on_sclk->count; v_index++) {
		if (tab->value == hwmgr->dyn_state.vddc_dependency_on_sclk->entries[v_index].v) {
			vol_found = true;
			if ((uint32_t)v_index < hwmgr->dyn_state.cac_leakage_table->count) {
				*lo = hwmgr->dyn_state.cac_leakage_table->entries[v_index].Vddc * VOLTAGE_SCALE;
				*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[v_index].Leakage * VOLTAGE_SCALE);
			} else {
				pr_warning("Index from SCLK/VDDC Dependency Table exceeds the CAC Leakage Table index, using maximum index from CAC table.\n");
				*lo = hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Vddc * VOLTAGE_SCALE;
				*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Leakage * VOLTAGE_SCALE);
			}
			break;
		}
	}

	/*
	 * If voltage is not found in the first pass, loop again to
	 * find the best match, equal or higher value.
	 */
	if (!vol_found) {
		for (v_index = 0; (uint32_t)v_index < hwmgr->dyn_state.vddc_dependency_on_sclk->count; v_index++) {
			if (tab->value <= hwmgr->dyn_state.vddc_dependency_on_sclk->entries[v_index].v) {
				vol_found = true;
				if ((uint32_t)v_index < hwmgr->dyn_state.cac_leakage_table->count) {
					*lo = hwmgr->dyn_state.cac_leakage_table->entries[v_index].Vddc * VOLTAGE_SCALE;
					*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[v_index].Leakage) * VOLTAGE_SCALE;
				} else {
					pr_warning("Index from SCLK/VDDC Dependency Table exceeds the CAC Leakage Table index in second look up, using maximum index from CAC table.");
					*lo = hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Vddc * VOLTAGE_SCALE;
					*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Leakage * VOLTAGE_SCALE);
				}
				break;
			}
		}

		if (!vol_found)
			pr_warning("Unable to get std_vddc from SCLK/VDDC Dependency Table, using vddc.\n");
	}

	return 0;
}

static int iceland_populate_smc_voltage_table(struct pp_hwmgr *hwmgr,
		pp_atomctrl_voltage_table_entry *tab,
		SMU71_Discrete_VoltageLevel *smc_voltage_tab) {
	int result;


	result = iceland_get_std_voltage_value_sidd(hwmgr, tab,
			&smc_voltage_tab->StdVoltageHiSidd,
			&smc_voltage_tab->StdVoltageLoSidd);
	if (0 != result) {
		smc_voltage_tab->StdVoltageHiSidd = tab->value * VOLTAGE_SCALE;
		smc_voltage_tab->StdVoltageLoSidd = tab->value * VOLTAGE_SCALE;
	}

	smc_voltage_tab->Voltage = PP_HOST_TO_SMC_US(tab->value * VOLTAGE_SCALE);
	CONVERT_FROM_HOST_TO_SMC_US(smc_voltage_tab->StdVoltageHiSidd);
	CONVERT_FROM_HOST_TO_SMC_US(smc_voltage_tab->StdVoltageHiSidd);

	return 0;
}

/**
 * Vddc table preparation for SMC.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    table     the SMC DPM table structure to be populated
 * @return   always 0
 */
static int iceland_populate_smc_vddc_table(struct pp_hwmgr *hwmgr,
			SMU71_Discrete_DpmTable *table)
{
	unsigned int count;
	int result;

	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	table->VddcLevelCount = data->vddc_voltage_table.count;
	for (count = 0; count < table->VddcLevelCount; count++) {
		result = iceland_populate_smc_voltage_table(hwmgr,
				&data->vddc_voltage_table.entries[count],
				&table->VddcLevel[count]);
		PP_ASSERT_WITH_CODE(0 == result, "do not populate SMC VDDC voltage table", return -EINVAL);

		/* GPIO voltage control */
		if (ICELAND_VOLTAGE_CONTROL_BY_GPIO == data->voltage_control)
			table->VddcLevel[count].Smio |= data->vddc_voltage_table.entries[count].smio_low;
		else if (ICELAND_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control)
			table->VddcLevel[count].Smio = 0;
	}

	CONVERT_FROM_HOST_TO_SMC_UL(table->VddcLevelCount);

	return 0;
}

/**
 * Vddci table preparation for SMC.
 *
 * @param    *hwmgr The address of the hardware manager.
 * @param    *table The SMC DPM table structure to be populated.
 * @return   0
 */
static int iceland_populate_smc_vdd_ci_table(struct pp_hwmgr *hwmgr,
			SMU71_Discrete_DpmTable *table)
{
	int result;
	uint32_t count;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	table->VddciLevelCount = data->vddci_voltage_table.count;
	for (count = 0; count < table->VddciLevelCount; count++) {
		result = iceland_populate_smc_voltage_table(hwmgr,
				&data->vddci_voltage_table.entries[count],
				&table->VddciLevel[count]);
		PP_ASSERT_WITH_CODE(0 == result, "do not populate SMC VDDCI voltage table", return -EINVAL);

		/* GPIO voltage control */
		if (ICELAND_VOLTAGE_CONTROL_BY_GPIO == data->vdd_ci_control)
			table->VddciLevel[count].Smio |= data->vddci_voltage_table.entries[count].smio_low;
		else
			table->VddciLevel[count].Smio = 0;
	}

	CONVERT_FROM_HOST_TO_SMC_UL(table->VddcLevelCount);

	return 0;
}

/**
 * Mvdd table preparation for SMC.
 *
 * @param    *hwmgr The address of the hardware manager.
 * @param    *table The SMC DPM table structure to be populated.
 * @return   0
 */
static int iceland_populate_smc_mvdd_table(struct pp_hwmgr *hwmgr,
			SMU71_Discrete_DpmTable *table)
{
	int result;
	uint32_t count;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	table->MvddLevelCount = data->mvdd_voltage_table.count;
	for (count = 0; count < table->MvddLevelCount; count++) {
		result = iceland_populate_smc_voltage_table(hwmgr,
				&data->mvdd_voltage_table.entries[count],
				&table->MvddLevel[count]);
		PP_ASSERT_WITH_CODE(0 == result, "do not populate SMC VDDCI voltage table", return -EINVAL);

		/* GPIO voltage control */
		if (ICELAND_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control)
			table->MvddLevel[count].Smio |= data->mvdd_voltage_table.entries[count].smio_low;
		else
			table->MvddLevel[count].Smio = 0;
	}

	CONVERT_FROM_HOST_TO_SMC_UL(table->MvddLevelCount);

	return 0;
}

/**
 * Convert a voltage value in mv unit to VID number required by SMU firmware
 */
static uint8_t convert_to_vid(uint16_t vddc)
{
	return (uint8_t) ((6200 - (vddc * VOLTAGE_SCALE)) / 25);
}

int iceland_populate_bapm_vddc_vid_sidd(struct pp_hwmgr *hwmgr)
{
	int i;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint8_t * hi_vid = data->power_tune_table.BapmVddCVidHiSidd;
	uint8_t * lo_vid = data->power_tune_table.BapmVddCVidLoSidd;

	PP_ASSERT_WITH_CODE(NULL != hwmgr->dyn_state.cac_leakage_table,
			    "The CAC Leakage table does not exist!", return -EINVAL);
	PP_ASSERT_WITH_CODE(hwmgr->dyn_state.cac_leakage_table->count <= 8,
			    "There should never be more than 8 entries for BapmVddcVid!!!", return -EINVAL);
	PP_ASSERT_WITH_CODE(hwmgr->dyn_state.cac_leakage_table->count == hwmgr->dyn_state.vddc_dependency_on_sclk->count,
			    "CACLeakageTable->count and VddcDependencyOnSCLk->count not equal", return -EINVAL);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_EVV)) {
		for (i = 0; (uint32_t) i < hwmgr->dyn_state.cac_leakage_table->count; i++) {
			lo_vid[i] = convert_to_vid(hwmgr->dyn_state.cac_leakage_table->entries[i].Vddc1);
			hi_vid[i] = convert_to_vid(hwmgr->dyn_state.cac_leakage_table->entries[i].Vddc2);
		}
	} else {
		PP_ASSERT_WITH_CODE(false, "Iceland should always support EVV", return -EINVAL);
	}

	return 0;
}

int iceland_populate_vddc_vid(struct pp_hwmgr *hwmgr)
{
	int i;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint8_t *vid = data->power_tune_table.VddCVid;

	PP_ASSERT_WITH_CODE(data->vddc_voltage_table.count <= 8,
		"There should never be more than 8 entries for VddcVid!!!",
		return -EINVAL);

	for (i = 0; i < (int)data->vddc_voltage_table.count; i++) {
		vid[i] = convert_to_vid(data->vddc_voltage_table.entries[i].value);
	}

	return 0;
}

/**
 * Preparation of voltage tables for SMC.
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    table     the SMC DPM table structure to be populated
 * @return   always 0
 */

int iceland_populate_smc_voltage_tables(struct pp_hwmgr *hwmgr,
	SMU71_Discrete_DpmTable *table)
{
	int result;

	result = iceland_populate_smc_vddc_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate VDDC voltage table to SMC", return -1);

	result = iceland_populate_smc_vdd_ci_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate VDDCI voltage table to SMC", return -1);

	result = iceland_populate_smc_mvdd_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate MVDD voltage table to SMC", return -1);

	return 0;
}


/**
 * Re-generate the DPM level mask value
 * @param    hwmgr      the address of the hardware manager
 */
static uint32_t iceland_get_dpm_level_enable_mask_value(
			struct iceland_single_dpm_table * dpm_table)
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

int iceland_populate_memory_timing_parameters(
		struct pp_hwmgr *hwmgr,
		uint32_t engine_clock,
		uint32_t memory_clock,
		struct SMU71_Discrete_MCArbDramTimingTableEntry *arb_regs
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
int iceland_program_memory_timing_parameters(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	int result = 0;
	SMU71_Discrete_MCArbDramTimingTable  arb_regs;
	uint32_t i, j;

	memset(&arb_regs, 0x00, sizeof(SMU71_Discrete_MCArbDramTimingTable));

	for (i = 0; i < data->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < data->dpm_table.mclk_table.count; j++) {
			result = iceland_populate_memory_timing_parameters
				(hwmgr, data->dpm_table.sclk_table.dpm_levels[i].value,
				 data->dpm_table.mclk_table.dpm_levels[j].value,
				 &arb_regs.entries[i][j]);

			if (0 != result) {
				break;
			}
		}
	}

	if (0 == result) {
		result = iceland_copy_bytes_to_smc(
				hwmgr->smumgr,
				data->arb_table_start,
				(uint8_t *)&arb_regs,
				sizeof(SMU71_Discrete_MCArbDramTimingTable),
				data->sram_end
				);
	}

	return result;
}

static int iceland_populate_smc_link_level(struct pp_hwmgr *hwmgr, SMU71_Discrete_DpmTable *table)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	struct iceland_dpm_table *dpm_table = &data->dpm_table;
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
		iceland_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);

	return 0;
}

static int iceland_populate_smc_uvd_level(struct pp_hwmgr *hwmgr,
					SMU71_Discrete_DpmTable *table)
{
	return 0;
}

uint8_t iceland_get_voltage_id(pp_atomctrl_voltage_table *voltage_table,
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

static int iceland_populate_smc_vce_level(struct pp_hwmgr *hwmgr,
					  SMU71_Discrete_DpmTable *table)
{
	return 0;
}

static int iceland_populate_smc_acp_level(struct pp_hwmgr *hwmgr,
					  SMU71_Discrete_DpmTable *table)
{
	return 0;
}

static int iceland_populate_smc_samu_level(struct pp_hwmgr *hwmgr,
					   SMU71_Discrete_DpmTable *table)
{
	return 0;
}


static int iceland_populate_smc_svi2_config(struct pp_hwmgr *hwmgr,
					    SMU71_Discrete_DpmTable *tab)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	if(ICELAND_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control)
		tab->SVI2Enable |= VDDC_ON_SVI2;

	if(ICELAND_VOLTAGE_CONTROL_BY_SVID2 == data->vdd_ci_control)
		tab->SVI2Enable |= VDDCI_ON_SVI2;
	else
		tab->MergedVddci = 1;

	if(ICELAND_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control)
		tab->SVI2Enable |= MVDD_ON_SVI2;

	PP_ASSERT_WITH_CODE( tab->SVI2Enable != (VDDC_ON_SVI2 | VDDCI_ON_SVI2 | MVDD_ON_SVI2) &&
	        (tab->SVI2Enable & VDDC_ON_SVI2), "SVI2 domain configuration is incorrect!", return -EINVAL);

	return 0;
}

static int iceland_get_dependecy_volt_by_clk(struct pp_hwmgr *hwmgr,
	struct phm_clock_voltage_dependency_table *allowed_clock_voltage_table,
	uint32_t clock, uint32_t *vol)
{
	uint32_t i = 0;

	/* clock - voltage dependency table is empty table */
	if (allowed_clock_voltage_table->count == 0)
		return -EINVAL;

	for (i = 0; i < allowed_clock_voltage_table->count; i++) {
		/* find first sclk bigger than request */
		if (allowed_clock_voltage_table->entries[i].clk >= clock) {
			*vol = allowed_clock_voltage_table->entries[i].v;
			return 0;
		}
	}

	/* sclk is bigger than max sclk in the dependence table */
	*vol = allowed_clock_voltage_table->entries[i - 1].v;

	return 0;
}

static uint8_t iceland_get_mclk_frequency_ratio(uint32_t memory_clock,
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

static uint8_t iceland_get_ddr3_mclk_frequency_ratio(uint32_t memory_clock)
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

static int iceland_populate_phase_value_based_on_sclk(struct pp_hwmgr *hwmgr, const struct phm_phase_shedding_limits_table *pl,
					uint32_t sclk, uint32_t *p_shed)
{
	unsigned int i;

	/* use the minimum phase shedding */
	*p_shed = 1;

	/*
	 * PPGen ensures the phase shedding limits table is sorted
	 * from lowest voltage/sclk/mclk to highest voltage/sclk/mclk.
	 * VBIOS ensures the phase shedding masks table is sorted from
	 * least phases enabled (phase shedding on) to most phases
	 * enabled (phase shedding off).
	 */
	for (i = 0; i < pl->count; i++) {
	    if (sclk < pl->entries[i].Sclk) {
	        /* Enable phase shedding */
	        *p_shed = i;
	        break;
	    }
	}

	return 0;
}

static int iceland_populate_phase_value_based_on_mclk(struct pp_hwmgr *hwmgr, const struct phm_phase_shedding_limits_table *pl,
					uint32_t memory_clock, uint32_t *p_shed)
{
	unsigned int i;

	/* use the minimum phase shedding */
	*p_shed = 1;

	/*
	 * PPGen ensures the phase shedding limits table is sorted
	 * from lowest voltage/sclk/mclk to highest voltage/sclk/mclk.
	 * VBIOS ensures the phase shedding masks table is sorted from
	 * least phases enabled (phase shedding on) to most phases
	 * enabled (phase shedding off).
	 */
	for (i = 0; i < pl->count; i++) {
	    if (memory_clock < pl->entries[i].Mclk) {
	        /* Enable phase shedding */
	        *p_shed = i;
	        break;
	    }
	}

	return 0;
}

/**
 * Populates the SMC MCLK structure using the provided memory clock
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    memory_clock the memory clock to use to populate the structure
 * @param    sclk        the SMC SCLK structure to be populated
 */
static int iceland_calculate_mclk_params(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU71_Discrete_MemoryLevel *mclk,
		bool strobe_mode,
		bool dllStateOn
		)
{
	const iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
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

static int iceland_populate_single_memory_level(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU71_Discrete_MemoryLevel *memory_level
		)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	int result = 0;
	bool dllStateOn;
	struct cgs_display_info info = {0};


	if (NULL != hwmgr->dyn_state.vddc_dependency_on_mclk) {
		result = iceland_get_dependecy_volt_by_clk(hwmgr,
			hwmgr->dyn_state.vddc_dependency_on_mclk, memory_clock, &memory_level->MinVddc);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find MinVddc voltage value from memory VDDC voltage dependency table", return result);
	}

	if (data->vdd_ci_control == ICELAND_VOLTAGE_CONTROL_NONE) {
		memory_level->MinVddci = memory_level->MinVddc;
	} else if (NULL != hwmgr->dyn_state.vddci_dependency_on_mclk) {
		result = iceland_get_dependecy_volt_by_clk(hwmgr,
				hwmgr->dyn_state.vddci_dependency_on_mclk,
				memory_clock,
				&memory_level->MinVddci);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find MinVddci voltage value from memory VDDCI voltage dependency table", return result);
	}

	if (NULL != hwmgr->dyn_state.mvdd_dependency_on_mclk) {
		result = iceland_get_dependecy_volt_by_clk(hwmgr,
			hwmgr->dyn_state.mvdd_dependency_on_mclk, memory_clock, &memory_level->MinMvdd);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find MinMVDD voltage value from memory MVDD voltage dependency table", return result);
	}

	memory_level->MinVddcPhases = 1;

	if (data->vddc_phase_shed_control) {
		iceland_populate_phase_value_based_on_mclk(hwmgr, hwmgr->dyn_state.vddc_phase_shed_limits_table,
				memory_clock, &memory_level->MinVddcPhases);
	}

	memory_level->EnabledForThrottle = 1;
	memory_level->EnabledForActivity = 1;
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

	//if ((data->mclk_stutter_mode_threshold != 0) &&
	//    (memory_clock <= data->mclk_stutter_mode_threshold) &&
	//    (data->is_uvd_enabled == 0)
	//    && (PHM_READ_FIELD(hwmgr->device, DPG_PIPE_STUTTER_CONTROL, STUTTER_ENABLE) & 0x1)
	//    && (data->display_timing.num_existing_displays <= 2)
	//    && (data->display_timing.num_existing_displays != 0))
	//	memory_level->StutterEnable = 1;

	/* decide strobe mode*/
	memory_level->StrobeEnable = (data->mclk_strobe_mode_threshold != 0) &&
		(memory_clock <= data->mclk_strobe_mode_threshold);

	/* decide EDC mode and memory clock ratio*/
	if (data->is_memory_GDDR5) {
		memory_level->StrobeRatio = iceland_get_mclk_frequency_ratio(memory_clock,
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
			if (iceland_get_mclk_frequency_ratio(memory_clock, 1) >=
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
			iceland_get_ddr3_mclk_frequency_ratio(memory_clock);
		dllStateOn = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC5) >> 1) & 0x1) ? 1 : 0;
	}

	result = iceland_calculate_mclk_params(hwmgr,
		memory_clock, memory_level, memory_level->StrobeEnable, dllStateOn);

	if (0 == result) {
		memory_level->MinVddc = PP_HOST_TO_SMC_UL(memory_level->MinVddc * VOLTAGE_SCALE);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MinVddcPhases);
		memory_level->MinVddci = PP_HOST_TO_SMC_UL(memory_level->MinVddci * VOLTAGE_SCALE);
		memory_level->MinMvdd = PP_HOST_TO_SMC_UL(memory_level->MinMvdd * VOLTAGE_SCALE);
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
int iceland_populate_mvdd_value(struct pp_hwmgr *hwmgr, uint32_t mclk, SMU71_Discrete_VoltageLevel *voltage)
{
	const iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	uint32_t i = 0;

	if (ICELAND_VOLTAGE_CONTROL_NONE != data->mvdd_control) {
		/* find mvdd value which clock is more than request */
		for (i = 0; i < hwmgr->dyn_state.mvdd_dependency_on_mclk->count; i++) {
			if (mclk <= hwmgr->dyn_state.mvdd_dependency_on_mclk->entries[i].clk) {
				/* Always round to higher voltage. */
				voltage->Voltage = data->mvdd_voltage_table.entries[i].value;
				break;
			}
		}

		PP_ASSERT_WITH_CODE(i < hwmgr->dyn_state.mvdd_dependency_on_mclk->count,
			"MVDD Voltage is outside the supported range.", return -1);

	} else {
		return -1;
	}

	return 0;
}


static int iceland_populate_smc_acpi_level(struct pp_hwmgr *hwmgr,
	SMU71_Discrete_DpmTable *table)
{
	int result = 0;
	const iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	pp_atomctrl_clock_dividers_vi dividers;
	SMU71_Discrete_VoltageLevel voltage_level;
	uint32_t spll_func_cntl    = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_2  = data->clock_registers.vCG_SPLL_FUNC_CNTL_2;
	uint32_t dll_cntl          = data->clock_registers.vDLL_CNTL;
	uint32_t mclk_pwrmgt_cntl  = data->clock_registers.vMCLK_PWRMGT_CNTL;

	/* The ACPI state should not do DPM on DC (or ever).*/
	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (data->acpi_vddc)
		table->ACPILevel.MinVddc = PP_HOST_TO_SMC_UL(data->acpi_vddc * VOLTAGE_SCALE);
	else
		table->ACPILevel.MinVddc = PP_HOST_TO_SMC_UL(data->min_vddc_in_pp_table * VOLTAGE_SCALE);

	table->ACPILevel.MinVddcPhases = (data->vddc_phase_shed_control) ? 0 : 1;

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

	table->MemoryACPILevel.MinVddc = table->ACPILevel.MinVddc;
	table->MemoryACPILevel.MinVddcPhases = table->ACPILevel.MinVddcPhases;

	/*  CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MinVoltage);*/

	if (0 == iceland_populate_mvdd_value(hwmgr, 0, &voltage_level))
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

static int iceland_find_boot_level(struct iceland_single_dpm_table *table, uint32_t value, uint32_t *boot_level)
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

/**
 * Calculates the SCLK dividers using the provided engine clock
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    engine_clock the engine clock to use to populate the structure
 * @param    sclk        the SMC SCLK structure to be populated
 */
int iceland_calculate_sclk_params(struct pp_hwmgr *hwmgr,
		uint32_t engine_clock, SMU71_Discrete_GraphicsLevel *sclk)
{
	const iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
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

static uint8_t iceland_get_sleep_divider_id_from_clock(struct pp_hwmgr *hwmgr,
		uint32_t engine_clock, uint32_t min_engine_clock_in_sr)
{
	uint32_t i, temp;
	uint32_t min = (min_engine_clock_in_sr > ICELAND_MINIMUM_ENGINE_CLOCK) ?
			min_engine_clock_in_sr : ICELAND_MINIMUM_ENGINE_CLOCK;

	PP_ASSERT_WITH_CODE((engine_clock >= min),
			"Engine clock can't satisfy stutter requirement!", return 0);

	for (i = ICELAND_MAX_DEEPSLEEP_DIVIDER_ID;; i--) {
		temp = engine_clock / (1 << i);

		if(temp >= min || i == 0)
			break;
	}
	return (uint8_t)i;
}

/**
 * Populates single SMC SCLK structure using the provided engine clock
 *
 * @param    hwmgr      the address of the hardware manager
 * @param    engine_clock the engine clock to use to populate the structure
 * @param    sclk        the SMC SCLK structure to be populated
 */
static int iceland_populate_single_graphic_level(struct pp_hwmgr *hwmgr,
		uint32_t engine_clock, uint16_t	sclk_activity_level_threshold,
		SMU71_Discrete_GraphicsLevel *graphic_level)
{
	int result;
	uint32_t threshold;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	result = iceland_calculate_sclk_params(hwmgr, engine_clock, graphic_level);


	/* populate graphics levels*/
	result = iceland_get_dependecy_volt_by_clk(hwmgr,
			hwmgr->dyn_state.vddc_dependency_on_sclk, engine_clock, &graphic_level->MinVddc);
	PP_ASSERT_WITH_CODE((0 == result),
		"can not find VDDC voltage value for VDDC engine clock dependency table", return result);

	/* SCLK frequency in units of 10KHz*/
	graphic_level->SclkFrequency = engine_clock;

	/*
	 * Minimum VDDC phases required to support this level, it
	 * should get from dependence table.
	 */
	graphic_level->MinVddcPhases = 1;

	if (data->vddc_phase_shed_control) {
		iceland_populate_phase_value_based_on_sclk(hwmgr,
				hwmgr->dyn_state.vddc_phase_shed_limits_table,
				engine_clock,
				&graphic_level->MinVddcPhases);
	}

	/* Indicates maximum activity level for this performance level. 50% for now*/
	graphic_level->ActivityLevel = sclk_activity_level_threshold;

	graphic_level->CcPwrDynRm = 0;
	graphic_level->CcPwrDynRm1 = 0;
	/* this level can be used if activity is high enough.*/
	graphic_level->EnabledForActivity = 1;
	/* this level can be used for throttling.*/
	graphic_level->EnabledForThrottle = 1;
	graphic_level->UpHyst = 0;
	graphic_level->DownHyst = 100;
	graphic_level->VoltageDownHyst = 0;
	graphic_level->PowerThrottle = 0;

	threshold = engine_clock * data->fast_watermark_threshold / 100;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep)) {
		graphic_level->DeepSleepDivId =
				iceland_get_sleep_divider_id_from_clock(hwmgr, engine_clock,
						data->display_timing.min_clock_insr);
	}

	/* Default to slow, highest DPM level will be set to PPSMC_DISPLAY_WATERMARK_LOW later.*/
	graphic_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	if (0 == result) {
		graphic_level->MinVddc = PP_HOST_TO_SMC_UL(graphic_level->MinVddc * VOLTAGE_SCALE);
		/* CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->MinVoltage);*/
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->MinVddcPhases);
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
static int iceland_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	struct iceland_dpm_table *dpm_table = &data->dpm_table;
	int result = 0;
	uint32_t level_array_adress = data->dpm_table_start +
		offsetof(SMU71_Discrete_DpmTable, GraphicsLevel);

	uint32_t level_array_size = sizeof(SMU71_Discrete_GraphicsLevel) * SMU71_MAX_LEVELS_GRAPHICS;
	SMU71_Discrete_GraphicsLevel *levels = data->smc_state_table.GraphicsLevel;
	uint32_t i;
	uint8_t highest_pcie_level_enabled = 0, lowest_pcie_level_enabled = 0, mid_pcie_level_enabled = 0, count = 0;
	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		result = iceland_populate_single_graphic_level(hwmgr,
					dpm_table->sclk_table.dpm_levels[i].value,
					(uint16_t)data->activity_target[i],
					&(data->smc_state_table.GraphicsLevel[i]));
		if (0 != result)
			return result;

		/* Making sure only DPM level 0-1 have Deep Sleep Div ID populated. */
		if (i > 1)
			data->smc_state_table.GraphicsLevel[i].DeepSleepDivId = 0;
	}

	/* set highest level watermark to high */
	if (dpm_table->sclk_table.count > 1)
		data->smc_state_table.GraphicsLevel[dpm_table->sclk_table.count-1].DisplayWatermark =
			PPSMC_DISPLAY_WATERMARK_HIGH;

	data->smc_state_table.GraphicsDpmLevelCount =
		(uint8_t)dpm_table->sclk_table.count;
	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
		iceland_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);

	while ((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
				(1 << (highest_pcie_level_enabled + 1))) != 0) {
		highest_pcie_level_enabled++;
	}

	while ((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
	       (1 << lowest_pcie_level_enabled)) == 0) {
		lowest_pcie_level_enabled++;
	}

	while ((count < highest_pcie_level_enabled) &&
			((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
				(1 << (lowest_pcie_level_enabled + 1 + count))) == 0)) {
		count++;
	}

	mid_pcie_level_enabled = (lowest_pcie_level_enabled+1+count) < highest_pcie_level_enabled ?
		(lowest_pcie_level_enabled + 1 + count) : highest_pcie_level_enabled;

	/* set pcieDpmLevel to highest_pcie_level_enabled*/
	for (i = 2; i < dpm_table->sclk_table.count; i++) {
		data->smc_state_table.GraphicsLevel[i].pcieDpmLevel = highest_pcie_level_enabled;
	}

	/* set pcieDpmLevel to lowest_pcie_level_enabled*/
	data->smc_state_table.GraphicsLevel[0].pcieDpmLevel = lowest_pcie_level_enabled;

	/* set pcieDpmLevel to mid_pcie_level_enabled*/
	data->smc_state_table.GraphicsLevel[1].pcieDpmLevel = mid_pcie_level_enabled;

	/* level count will send to smc once at init smc table and never change*/
	result = iceland_copy_bytes_to_smc(hwmgr->smumgr, level_array_adress, (uint8_t *)levels, (uint32_t)level_array_size, data->sram_end);

	if (0 != result)
		return result;

	return 0;
}

/**
 * Populates all SMC MCLK levels' structure based on the trimmed allowed dpm memory clock states
 *
 * @param    hwmgr      the address of the hardware manager
 */

static int iceland_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	struct iceland_dpm_table *dpm_table = &data->dpm_table;
	int result;
	/* populate MCLK dpm table to SMU7 */
	uint32_t level_array_adress = data->dpm_table_start + offsetof(SMU71_Discrete_DpmTable, MemoryLevel);
	uint32_t level_array_size = sizeof(SMU71_Discrete_MemoryLevel) * SMU71_MAX_LEVELS_MEMORY;
	SMU71_Discrete_MemoryLevel *levels = data->smc_state_table.MemoryLevel;
	uint32_t i;

	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		PP_ASSERT_WITH_CODE((0 != dpm_table->mclk_table.dpm_levels[i].value),
			"can not populate memory level as memory clock is zero", return -1);
		result = iceland_populate_single_memory_level(hwmgr, dpm_table->mclk_table.dpm_levels[i].value,
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
	data->dpm_level_enable_mask.mclk_dpm_enable_mask = iceland_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);
	/* set highest level watermark to high*/
	data->smc_state_table.MemoryLevel[dpm_table->mclk_table.count-1].DisplayWatermark = PPSMC_DISPLAY_WATERMARK_HIGH;

	/* level count will send to smc once at init smc table and never change*/
	result = iceland_copy_bytes_to_smc(hwmgr->smumgr,
		level_array_adress, (uint8_t *)levels, (uint32_t)level_array_size, data->sram_end);

	if (0 != result) {
		return result;
	}

	return 0;
}

struct ICELAND_DLL_SPEED_SETTING
{
	uint16_t        Min;           /* Minimum Data Rate*/
	uint16_t        Max;           /* Maximum Data Rate*/
	uint32_t	dll_speed;     /* The desired DLL_SPEED setting*/
};

static int iceland_populate_ulv_level(struct pp_hwmgr *hwmgr, SMU71_Discrete_Ulv *pstate)
{
	int result = 0;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	uint32_t voltage_response_time, ulv_voltage;

	pstate->CcPwrDynRm = 0;
	pstate->CcPwrDynRm1 = 0;

	//backbiasResponseTime is use for ULV state voltage value.
	result = pp_tables_get_response_times(hwmgr, &voltage_response_time, &ulv_voltage);
	PP_ASSERT_WITH_CODE((0 == result), "can not get ULV voltage value", return result;);

	if(!ulv_voltage) {
		data->ulv.ulv_supported = false;
		return 0;
	}

	if (ICELAND_VOLTAGE_CONTROL_BY_SVID2 != data->voltage_control) {
		/* use minimum voltage if ulv voltage in pptable is bigger than minimum voltage */
		if (ulv_voltage > hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v) {
			pstate->VddcOffset = 0;
		}
		else {
			/* used in SMIO Mode. not implemented for now. this is backup only for CI. */
			pstate->VddcOffset = (uint16_t)(hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v - ulv_voltage);
		}
	} else {
		/* use minimum voltage if ulv voltage in pptable is bigger than minimum voltage */
		if(ulv_voltage > hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v) {
			pstate->VddcOffsetVid = 0;
		} else {
			/* used in SVI2 Mode */
			pstate->VddcOffsetVid = (uint8_t)((hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v - ulv_voltage) * VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1);
		}
	}

	/* used in SVI2 Mode to shed phase */
	pstate->VddcPhase = (data->vddc_phase_shed_control) ? 0 : 1;

	if (0 == result) {
		CONVERT_FROM_HOST_TO_SMC_UL(pstate->CcPwrDynRm);
		CONVERT_FROM_HOST_TO_SMC_UL(pstate->CcPwrDynRm1);
		CONVERT_FROM_HOST_TO_SMC_US(pstate->VddcOffset);
	}

	return result;
}

static int iceland_populate_ulv_state(struct pp_hwmgr *hwmgr, SMU71_Discrete_Ulv *ulv)
{
	return iceland_populate_ulv_level(hwmgr, ulv);
}

static int iceland_populate_smc_initial_state(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	uint8_t count, level;

	count = (uint8_t)(hwmgr->dyn_state.vddc_dependency_on_sclk->count);

	for (level = 0; level < count; level++) {
		if (hwmgr->dyn_state.vddc_dependency_on_sclk->entries[level].clk
			 >= data->vbios_boot_state.sclk_bootup_value) {
			data->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	count = (uint8_t)(hwmgr->dyn_state.vddc_dependency_on_mclk->count);

	for (level = 0; level < count; level++) {
		if (hwmgr->dyn_state.vddc_dependency_on_mclk->entries[level].clk
			>= data->vbios_boot_state.mclk_bootup_value) {
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
int iceland_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	SMU71_Discrete_DpmTable  *table = &(data->smc_state_table);
	const struct phw_iceland_ulv_parm *ulv = &(data->ulv);

	result = iceland_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to setup default DPM tables!", return result;);
	memset(&(data->smc_state_table), 0x00, sizeof(data->smc_state_table));

	if (ICELAND_VOLTAGE_CONTROL_NONE != data->voltage_control) {
		iceland_populate_smc_voltage_tables(hwmgr, table);
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

	if (ulv->ulv_supported) {
		result = iceland_populate_ulv_state(hwmgr, &data->ulv_setting);
		PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize ULV state!", return result;);

		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_ULV_PARAMETER, ulv->ch_ulv_parameter);
	}

	result = iceland_populate_smc_link_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Link Level!", return result;);

	result = iceland_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Graphics Level!", return result;);

	result = iceland_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Memory Level!", return result;);

	result = iceland_populate_smc_acpi_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize ACPI Level!", return result;);

	result = iceland_populate_smc_vce_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize VCE Level!", return result;);

	result = iceland_populate_smc_acp_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize ACP Level!", return result;);

	result = iceland_populate_smc_samu_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize SAMU Level!", return result;);

	/*
	 * Since only the initial state is completely set up at this
	 * point (the other states are just copies of the boot state)
	 * we only need to populate the  ARB settings for the initial
	 * state.
	 */
	result = iceland_program_memory_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to Write ARB settings for the initial state.", return result;);

	result = iceland_populate_smc_uvd_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize UVD Level!", return result;);

	table->GraphicsBootLevel = 0;
	table->MemoryBootLevel = 0;

	/* find boot level from dpm table */
	result = iceland_find_boot_level(&(data->dpm_table.sclk_table),
			data->vbios_boot_state.sclk_bootup_value,
			(uint32_t *)&(data->smc_state_table.GraphicsBootLevel));

	if (result)
		pr_warning("VBIOS did not find boot engine clock value in dependency table.\n");

	result = iceland_find_boot_level(&(data->dpm_table.mclk_table),
				data->vbios_boot_state.mclk_bootup_value,
				(uint32_t *)&(data->smc_state_table.MemoryBootLevel));

	if (result)
		pr_warning("VBIOS did not find boot memory clock value in dependency table.\n");

	table->BootVddc = data->vbios_boot_state.vddc_bootup_value;
	if (ICELAND_VOLTAGE_CONTROL_NONE == data->vdd_ci_control) {
		table->BootVddci = table->BootVddc;
	}
	else {
		table->BootVddci = data->vbios_boot_state.vddci_bootup_value;
	}
	table->BootMVdd = data->vbios_boot_state.mvdd_bootup_value;

	result = iceland_populate_smc_initial_state(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result, "Failed to initialize Boot State!", return result);

	result = iceland_populate_bapm_parameters_in_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result, "Failed to populate BAPM Parameters!", return result);

	table->GraphicsVoltageChangeEnable  = 1;
	table->GraphicsThermThrottleEnable  = 1;
	table->GraphicsInterval = 1;
	table->VoltageInterval  = 1;
	table->ThermalInterval  = 1;
	table->TemperatureLimitHigh =
		(data->thermal_temp_setting.temperature_high *
		 ICELAND_Q88_FORMAT_CONVERSION_UNIT) / PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	table->TemperatureLimitLow =
		(data->thermal_temp_setting.temperature_low *
		ICELAND_Q88_FORMAT_CONVERSION_UNIT) / PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	table->MemoryVoltageChangeEnable  = 1;
	table->MemoryInterval  = 1;
	table->VoltageResponseTime  = 0;
	table->PhaseResponseTime  = 0;
	table->MemoryThermThrottleEnable  = 1;
	table->PCIeBootLinkLevel = 0;
	table->PCIeGenInterval = 1;

	result = iceland_populate_smc_svi2_config(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to populate SVI2 setting!", return result);

	table->ThermGpio  = 17;
	table->SclkStepSize = 0x4000;

	CONVERT_FROM_HOST_TO_SMC_UL(table->SystemFlags);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskVddcVid);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskVddcPhase);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskVddciVid);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskMvddVid);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SclkStepSize);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitHigh);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitLow);
	CONVERT_FROM_HOST_TO_SMC_US(table->VoltageResponseTime);
	CONVERT_FROM_HOST_TO_SMC_US(table->PhaseResponseTime);

	table->BootVddc = PP_HOST_TO_SMC_US(table->BootVddc * VOLTAGE_SCALE);
	table->BootVddci = PP_HOST_TO_SMC_US(table->BootVddci * VOLTAGE_SCALE);
	table->BootMVdd = PP_HOST_TO_SMC_US(table->BootMVdd * VOLTAGE_SCALE);

	/* Upload all dpm data to SMC memory.(dpm level, dpm level count etc) */
	result = iceland_copy_bytes_to_smc(hwmgr->smumgr, data->dpm_table_start +
				offsetof(SMU71_Discrete_DpmTable, SystemFlags),
				(uint8_t *)&(table->SystemFlags),
				sizeof(SMU71_Discrete_DpmTable) - 3 * sizeof(SMU71_PIDController),
				data->sram_end);

	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to upload dpm data to SMC memory!", return result);

	/* Upload all ulv setting to SMC memory.(dpm level, dpm level count etc) */
	result = iceland_copy_bytes_to_smc(hwmgr->smumgr,
			data->ulv_settings_start,
			(uint8_t *)&(data->ulv_setting),
			sizeof(SMU71_Discrete_Ulv),
			data->sram_end);

#if 0
	/* Notify SMC to follow new GPIO scheme */
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition)) {
		if (0 == iceland_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_UseNewGPIOScheme))
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_SMCtoPPLIBAcdcGpioScheme);
	}
#endif

	return result;
}

int iceland_populate_mc_reg_address(struct pp_hwmgr *hwmgr, SMU71_Discrete_MCRegisters *mc_reg_table)
{
	const struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	uint32_t i, j;

	for (i = 0, j = 0; j < data->iceland_mc_reg_table.last; j++) {
		if (data->iceland_mc_reg_table.validflag & 1<<j) {
			PP_ASSERT_WITH_CODE(i < SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE,
				"Index of mc_reg_table->address[] array out of boundary", return -1);
			mc_reg_table->address[i].s0 =
				PP_HOST_TO_SMC_US(data->iceland_mc_reg_table.mc_reg_address[j].s0);
			mc_reg_table->address[i].s1 =
				PP_HOST_TO_SMC_US(data->iceland_mc_reg_table.mc_reg_address[j].s1);
			i++;
		}
	}

	mc_reg_table->last = (uint8_t)i;

	return 0;
}

/* convert register values from driver to SMC format */
void iceland_convert_mc_registers(
	const phw_iceland_mc_reg_entry * pEntry,
	SMU71_Discrete_MCRegisterSet *pData,
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

/* find the entry in the memory range table, then populate the value to SMC's iceland_mc_reg_table */
int iceland_convert_mc_reg_table_entry_to_smc(
		struct pp_hwmgr *hwmgr,
		const uint32_t memory_clock,
		SMU71_Discrete_MCRegisterSet *mc_reg_table_data
		)
{
	const iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint32_t i = 0;

	for (i = 0; i < data->iceland_mc_reg_table.num_entries; i++) {
		if (memory_clock <=
			data->iceland_mc_reg_table.mc_reg_table_entry[i].mclk_max) {
			break;
		}
	}

	if ((i == data->iceland_mc_reg_table.num_entries) && (i > 0))
		--i;

	iceland_convert_mc_registers(&data->iceland_mc_reg_table.mc_reg_table_entry[i],
		mc_reg_table_data, data->iceland_mc_reg_table.last, data->iceland_mc_reg_table.validflag);

	return 0;
}

int iceland_convert_mc_reg_table_to_smc(struct pp_hwmgr *hwmgr,
		SMU71_Discrete_MCRegisters *mc_reg_table)
{
	int result = 0;
	iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	int res;
	uint32_t i;

	for (i = 0; i < data->dpm_table.mclk_table.count; i++) {
		res = iceland_convert_mc_reg_table_entry_to_smc(
				hwmgr,
				data->dpm_table.mclk_table.dpm_levels[i].value,
				&mc_reg_table->data[i]
				);

		if (0 != res)
			result = res;
	}

	return result;
}

int iceland_populate_initial_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	memset(&data->mc_reg_table, 0x00, sizeof(SMU71_Discrete_MCRegisters));
	result = iceland_populate_mc_reg_address(hwmgr, &(data->mc_reg_table));
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize MCRegTable for the MC register addresses!", return result;);

	result = iceland_convert_mc_reg_table_to_smc(hwmgr, &data->mc_reg_table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize MCRegTable for driver state!", return result;);

	return iceland_copy_bytes_to_smc(hwmgr->smumgr, data->mc_reg_table_start,
			(uint8_t *)&data->mc_reg_table, sizeof(SMU71_Discrete_MCRegisters), data->sram_end);
}

int iceland_notify_smc_display_change(struct pp_hwmgr *hwmgr, bool has_display)
{
	PPSMC_Msg msg = has_display? (PPSMC_Msg)PPSMC_HasDisplay : (PPSMC_Msg)PPSMC_NoDisplay;

	return (smum_send_msg_to_smc(hwmgr->smumgr, msg) == 0) ?  0 : -1;
}

int iceland_enable_sclk_control(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL, SCLK_PWRMGT_OFF, 0);

	return 0;
}

int iceland_enable_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

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

int iceland_start_dpm(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	/* enable general power management */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT, GLOBAL_PWRMGT_EN, 1);
	/* enable sclk deep sleep */
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL, DYNAMIC_PM_EN, 1);

	/* prepare for PCIE DPM */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SOFT_REGISTERS_TABLE_12, VoltageChangeTimeout, 0x1000);

	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__PCIE, SWRST_COMMAND_1, RESETLC, 0x0);

	PP_ASSERT_WITH_CODE(
			(0 == smum_send_msg_to_smc(hwmgr->smumgr,
					PPSMC_MSG_Voltage_Cntl_Enable)),
			"Failed to enable voltage DPM during DPM Start Function!",
			return -1);

	if (0 != iceland_enable_sclk_mclk_dpm(hwmgr)) {
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

static void iceland_set_dpm_event_sources(struct pp_hwmgr *hwmgr,
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

static int iceland_enable_auto_throttle_source(struct pp_hwmgr *hwmgr,
		PHM_AutoThrottleSource source)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	if (!(data->active_auto_throttle_sources & (1 << source))) {
		data->active_auto_throttle_sources |= 1 << source;
		iceland_set_dpm_event_sources(hwmgr, data->active_auto_throttle_sources);
	}
	return 0;
}

static int iceland_enable_thermal_auto_throttle(struct pp_hwmgr *hwmgr)
{
	return iceland_enable_auto_throttle_source(hwmgr, PHM_AutoThrottleSource_Thermal);
}

static int iceland_tf_start_smc(struct pp_hwmgr *hwmgr)
{
	int ret = 0;

	if (!iceland_is_smc_ram_running(hwmgr->smumgr))
		ret = iceland_smu_start_smc(hwmgr->smumgr);

	return ret;
}

/**
* Programs the Deep Sleep registers
*
* @param    pHwMgr  the address of the powerplay hardware manager.
* @param    pInput the pointer to input data (PhwEvergreen_DisplayConfiguration)
* @param    pOutput the pointer to output data (unused)
* @param    pStorage the pointer to temporary storage (unused)
* @param    Result the last failure code (unused)
* @return   always 0
*/
static int iceland_enable_deep_sleep_master_switch(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_SclkDeepSleep)) {
		if (smum_send_msg_to_smc(hwmgr->smumgr,
					 PPSMC_MSG_MASTER_DeepSleep_ON) != 0)
			PP_ASSERT_WITH_CODE(false,
					    "Attempt to enable Master Deep Sleep switch failed!",
					    return -EINVAL);
	} else {
		if (smum_send_msg_to_smc(hwmgr->smumgr,
					 PPSMC_MSG_MASTER_DeepSleep_OFF) != 0)
			PP_ASSERT_WITH_CODE(false,
					    "Attempt to disable Master Deep Sleep switch failed!",
					    return -EINVAL);
	}

	return 0;
}

static int iceland_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	if (cf_iceland_voltage_control(hwmgr)) {
		tmp_result = iceland_enable_voltage_control(hwmgr);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable voltage control!", return tmp_result);

		tmp_result = iceland_construct_voltage_tables(hwmgr);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to contruct voltage tables!", return tmp_result);
	}

	tmp_result = iceland_initialize_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to initialize MC reg table!", return tmp_result);

	tmp_result = iceland_program_static_screen_threshold_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to program static screen threshold parameters!", return tmp_result);

	tmp_result = iceland_enable_display_gap(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable display gap!", return tmp_result);

	tmp_result = iceland_program_voting_clients(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to program voting clients!", return tmp_result);

	tmp_result = iceland_upload_firmware(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to upload firmware header!", return tmp_result);

	tmp_result = iceland_process_firmware_header(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to process firmware header!", return tmp_result);

	tmp_result = iceland_initial_switch_from_arb_f0_to_f1(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to initialize switch from ArbF0 to F1!", return tmp_result);

	tmp_result = iceland_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to initialize SMC table!", return tmp_result);

	tmp_result = iceland_populate_initial_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to populate initialize MC Reg table!", return tmp_result);

	tmp_result = iceland_populate_pm_fuses(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to populate PM fuses!", return tmp_result);

	/* start SMC */
	tmp_result = iceland_tf_start_smc(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to start SMC!", return tmp_result);

	/* enable SCLK control */
	tmp_result = iceland_enable_sclk_control(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable SCLK control!", return tmp_result);

	tmp_result = iceland_enable_deep_sleep_master_switch(hwmgr);
	PP_ASSERT_WITH_CODE((tmp_result == 0),
		"Failed to enable deep sleep!", return tmp_result);

	/* enable DPM */
	tmp_result = iceland_start_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to start DPM!", return tmp_result);

	tmp_result = iceland_enable_smc_cac(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable SMC CAC!", return tmp_result);

	tmp_result = iceland_enable_power_containment(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to enable power containment!", return tmp_result);

	tmp_result = iceland_power_control_set_level(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
		"Failed to power control set level!", result = tmp_result);

	tmp_result = iceland_enable_thermal_auto_throttle(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable thermal auto throttle!", result = tmp_result);

	return result;
}

static int iceland_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	return phm_hwmgr_backend_fini(hwmgr);
}

static void iceland_initialize_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct phw_iceland_ulv_parm *ulv;

	ulv = &data->ulv;
	ulv->ch_ulv_parameter = PPICELAND_CGULVPARAMETER_DFLT;
	data->voting_rights_clients0 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT0;
	data->voting_rights_clients1 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT1;
	data->voting_rights_clients2 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT2;
	data->voting_rights_clients3 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT3;
	data->voting_rights_clients4 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT4;
	data->voting_rights_clients5 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT5;
	data->voting_rights_clients6 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT6;
	data->voting_rights_clients7 = PPICELAND_VOTINGRIGHTSCLIENTS_DFLT7;

	data->static_screen_threshold_unit = PPICELAND_STATICSCREENTHRESHOLDUNIT_DFLT;
	data->static_screen_threshold = PPICELAND_STATICSCREENTHRESHOLD_DFLT;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_ABM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_NonABMSupportInPPLib);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_DynamicACTiming);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_DisableMemoryTransition);

	iceland_initialize_power_tune_defaults(hwmgr);

	data->mclk_strobe_mode_threshold = 40000;
	data->mclk_stutter_mode_threshold = 30000;
	data->mclk_edc_enable_threshold = 40000;
	data->mclk_edc_wr_enable_threshold = 40000;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_DisableMCLS);

	data->pcie_gen_performance.max = PP_PCIEGen1;
	data->pcie_gen_performance.min = PP_PCIEGen3;
	data->pcie_gen_power_saving.max = PP_PCIEGen1;
	data->pcie_gen_power_saving.min = PP_PCIEGen3;

	data->pcie_lane_performance.max = 0;
	data->pcie_lane_performance.min = 16;
	data->pcie_lane_power_saving.max = 0;
	data->pcie_lane_power_saving.min = 16;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_SclkThrottleLowNotification);
}

static int iceland_get_evv_voltage(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);
	uint16_t    virtual_voltage_id;
	uint16_t    vddc = 0;
	uint16_t    i;

	/* the count indicates actual number of entries */
	data->vddc_leakage.count = 0;
	data->vddci_leakage.count = 0;

	if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_EVV)) {
		pr_err("Iceland should always support EVV\n");
		return -EINVAL;
	}

	/* retrieve voltage for leakage ID (0xff01 + i) */
	for (i = 0; i < ICELAND_MAX_LEAKAGE_COUNT; i++) {
		virtual_voltage_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;

		PP_ASSERT_WITH_CODE((0 == atomctrl_get_voltage_evv(hwmgr, virtual_voltage_id, &vddc)),
				    "Error retrieving EVV voltage value!\n", continue);

		if (vddc >= 2000)
			pr_warning("Invalid VDDC value!\n");

		if (vddc != 0 && vddc != virtual_voltage_id) {
			data->vddc_leakage.actual_voltage[data->vddc_leakage.count] = vddc;
			data->vddc_leakage.leakage_id[data->vddc_leakage.count] = virtual_voltage_id;
			data->vddc_leakage.count++;
		}
	}

	return 0;
}

static void iceland_patch_with_vddc_leakage(struct pp_hwmgr *hwmgr,
					    uint32_t *vddc)
{
	iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint32_t leakage_index;
	struct phw_iceland_leakage_voltage *leakage_table = &data->vddc_leakage;

	/* search for leakage voltage ID 0xff01 ~ 0xff08 */
	for (leakage_index = 0; leakage_index < leakage_table->count; leakage_index++) {
		/*
		 * If this voltage matches a leakage voltage ID, patch
		 * with actual leakage voltage.
		 */
		if (leakage_table->leakage_id[leakage_index] == *vddc) {
			/*
			 * Need to make sure vddc is less than 2v or
			 * else, it could burn the ASIC.
			 */
			if (leakage_table->actual_voltage[leakage_index] >= 2000)
				pr_warning("Invalid VDDC value!\n");
			*vddc = leakage_table->actual_voltage[leakage_index];
			/* we found leakage voltage */
			break;
		}
	}

	if (*vddc >= ATOM_VIRTUAL_VOLTAGE_ID0)
		pr_warning("Voltage value looks like a Leakage ID but it's not patched\n");
}

static void iceland_patch_with_vddci_leakage(struct pp_hwmgr *hwmgr,
					     uint32_t *vddci)
{
	iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint32_t leakage_index;
	struct phw_iceland_leakage_voltage *leakage_table = &data->vddci_leakage;

	/* search for leakage voltage ID 0xff01 ~ 0xff08 */
	for (leakage_index = 0; leakage_index < leakage_table->count; leakage_index++) {
		/*
		 * If this voltage matches a leakage voltage ID, patch
		 * with actual leakage voltage.
		 */
		if (leakage_table->leakage_id[leakage_index] == *vddci) {
			*vddci = leakage_table->actual_voltage[leakage_index];
			/* we found leakage voltage */
			break;
		}
	}

	if (*vddci >= ATOM_VIRTUAL_VOLTAGE_ID0)
		pr_warning("Voltage value looks like a Leakage ID but it's not patched\n");
}

static int iceland_patch_vddc(struct pp_hwmgr *hwmgr,
			      struct phm_clock_voltage_dependency_table *tab)
{
	uint16_t i;

	if (tab)
		for (i = 0; i < tab->count; i++)
			iceland_patch_with_vddc_leakage(hwmgr, &tab->entries[i].v);

	return 0;
}

static int iceland_patch_vddci(struct pp_hwmgr *hwmgr,
			       struct phm_clock_voltage_dependency_table *tab)
{
	uint16_t i;

	if (tab)
		for (i = 0; i < tab->count; i++)
			iceland_patch_with_vddci_leakage(hwmgr, &tab->entries[i].v);

	return 0;
}

static int iceland_patch_vce_vddc(struct pp_hwmgr *hwmgr,
				  struct phm_vce_clock_voltage_dependency_table *tab)
{
	uint16_t i;

	if (tab)
		for (i = 0; i < tab->count; i++)
			iceland_patch_with_vddc_leakage(hwmgr, &tab->entries[i].v);

	return 0;
}


static int iceland_patch_uvd_vddc(struct pp_hwmgr *hwmgr,
				  struct phm_uvd_clock_voltage_dependency_table *tab)
{
	uint16_t i;

	if (tab)
		for (i = 0; i < tab->count; i++)
			iceland_patch_with_vddc_leakage(hwmgr, &tab->entries[i].v);

	return 0;
}

static int iceland_patch_vddc_shed_limit(struct pp_hwmgr *hwmgr,
					 struct phm_phase_shedding_limits_table *tab)
{
	uint16_t i;

	if (tab)
		for (i = 0; i < tab->count; i++)
			iceland_patch_with_vddc_leakage(hwmgr, &tab->entries[i].Voltage);

	return 0;
}

static int iceland_patch_samu_vddc(struct pp_hwmgr *hwmgr,
				   struct phm_samu_clock_voltage_dependency_table *tab)
{
	uint16_t i;

	if (tab)
		for (i = 0; i < tab->count; i++)
			iceland_patch_with_vddc_leakage(hwmgr, &tab->entries[i].v);

	return 0;
}

static int iceland_patch_acp_vddc(struct pp_hwmgr *hwmgr,
				  struct phm_acp_clock_voltage_dependency_table *tab)
{
	uint16_t i;

	if (tab)
		for (i = 0; i < tab->count; i++)
			iceland_patch_with_vddc_leakage(hwmgr, &tab->entries[i].v);

	return 0;
}

static int iceland_patch_limits_vddc(struct pp_hwmgr *hwmgr,
				     struct phm_clock_and_voltage_limits *tab)
{
	if (tab) {
		iceland_patch_with_vddc_leakage(hwmgr, (uint32_t *)&tab->vddc);
		iceland_patch_with_vddci_leakage(hwmgr, (uint32_t *)&tab->vddci);
	}

	return 0;
}

static int iceland_patch_cac_vddc(struct pp_hwmgr *hwmgr, struct phm_cac_leakage_table *tab)
{
	uint32_t i;
	uint32_t vddc;

	if (tab) {
		for (i = 0; i < tab->count; i++) {
			vddc = (uint32_t)(tab->entries[i].Vddc);
			iceland_patch_with_vddc_leakage(hwmgr, &vddc);
			tab->entries[i].Vddc = (uint16_t)vddc;
		}
	}

	return 0;
}

static int iceland_patch_dependency_tables_with_leakage(struct pp_hwmgr *hwmgr)
{
	int tmp;

	tmp = iceland_patch_vddc(hwmgr, hwmgr->dyn_state.vddc_dependency_on_sclk);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_vddc(hwmgr, hwmgr->dyn_state.vddc_dependency_on_mclk);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_vddc(hwmgr, hwmgr->dyn_state.vddc_dep_on_dal_pwrl);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_vddci(hwmgr, hwmgr->dyn_state.vddci_dependency_on_mclk);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_vce_vddc(hwmgr, hwmgr->dyn_state.vce_clock_voltage_dependency_table);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_uvd_vddc(hwmgr, hwmgr->dyn_state.uvd_clock_voltage_dependency_table);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_samu_vddc(hwmgr, hwmgr->dyn_state.samu_clock_voltage_dependency_table);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_acp_vddc(hwmgr, hwmgr->dyn_state.acp_clock_voltage_dependency_table);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_vddc_shed_limit(hwmgr, hwmgr->dyn_state.vddc_phase_shed_limits_table);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_limits_vddc(hwmgr, &hwmgr->dyn_state.max_clock_voltage_on_ac);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_limits_vddc(hwmgr, &hwmgr->dyn_state.max_clock_voltage_on_dc);
	if(tmp)
		return -EINVAL;

	tmp = iceland_patch_cac_vddc(hwmgr, hwmgr->dyn_state.cac_leakage_table);
	if(tmp)
		return -EINVAL;

	return 0;
}

static int iceland_set_private_var_based_on_pptale(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	struct phm_clock_voltage_dependency_table *allowed_sclk_vddc_table = hwmgr->dyn_state.vddc_dependency_on_sclk;
	struct phm_clock_voltage_dependency_table *allowed_mclk_vddc_table = hwmgr->dyn_state.vddc_dependency_on_mclk;
	struct phm_clock_voltage_dependency_table *allowed_mclk_vddci_table = hwmgr->dyn_state.vddci_dependency_on_mclk;

	PP_ASSERT_WITH_CODE(allowed_sclk_vddc_table != NULL,
		"VDDC dependency on SCLK table is missing. This table is mandatory\n", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_sclk_vddc_table->count >= 1,
		"VDDC dependency on SCLK table has to have is missing. This table is mandatory\n", return -EINVAL);

	PP_ASSERT_WITH_CODE(allowed_mclk_vddc_table != NULL,
		"VDDC dependency on MCLK table is missing. This table is mandatory\n", return -EINVAL);
	PP_ASSERT_WITH_CODE(allowed_mclk_vddc_table->count >= 1,
		"VDD dependency on MCLK table has to have is missing. This table is mandatory\n", return -EINVAL);

	data->min_vddc_in_pp_table = (uint16_t)allowed_sclk_vddc_table->entries[0].v;
	data->max_vddc_in_pp_table = (uint16_t)allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].v;

	hwmgr->dyn_state.max_clock_voltage_on_ac.sclk =
		allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].clk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.mclk =
		allowed_mclk_vddc_table->entries[allowed_mclk_vddc_table->count - 1].clk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddc =
		allowed_sclk_vddc_table->entries[allowed_sclk_vddc_table->count - 1].v;

	if (allowed_mclk_vddci_table != NULL && allowed_mclk_vddci_table->count >= 1) {
		data->min_vddci_in_pp_table = (uint16_t)allowed_mclk_vddci_table->entries[0].v;
		data->max_vddci_in_pp_table = (uint16_t)allowed_mclk_vddci_table->entries[allowed_mclk_vddci_table->count - 1].v;
	}

	if (hwmgr->dyn_state.vddci_dependency_on_mclk != NULL && hwmgr->dyn_state.vddci_dependency_on_mclk->count > 1)
		hwmgr->dyn_state.max_clock_voltage_on_ac.vddci = hwmgr->dyn_state.vddci_dependency_on_mclk->entries[hwmgr->dyn_state.vddci_dependency_on_mclk->count - 1].v;

	return 0;
}

static int iceland_initializa_dynamic_state_adjustment_rule_settings(struct pp_hwmgr *hwmgr)
{
	uint32_t table_size;
	struct phm_clock_voltage_dependency_table *table_clk_vlt;

	hwmgr->dyn_state.mclk_sclk_ratio = 4;
	hwmgr->dyn_state.sclk_mclk_delta = 15000;      /* 150 MHz */
	hwmgr->dyn_state.vddc_vddci_delta = 200;       /* 200mV */

	/* initialize vddc_dep_on_dal_pwrl table */
	table_size = sizeof(uint32_t) + 4 * sizeof(struct phm_clock_voltage_dependency_record);
	table_clk_vlt = (struct phm_clock_voltage_dependency_table *)kzalloc(table_size, GFP_KERNEL);

	if (NULL == table_clk_vlt) {
		pr_err("[ powerplay ] Can not allocate space for vddc_dep_on_dal_pwrl! \n");
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
		hwmgr->dyn_state.vddc_dep_on_dal_pwrl = table_clk_vlt;
	}

	return 0;
}

/**
 * Initializes the Volcanic Islands Hardware Manager
 *
 * @param   hwmgr the address of the powerplay hardware manager.
 * @return   1 if success; otherwise appropriate error code.
 */
static int iceland_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	SMU71_Discrete_DpmTable *table = NULL;
	iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	pp_atomctrl_gpio_pin_assignment gpio_pin_assignment;
	bool stay_in_boot;
	struct phw_iceland_ulv_parm *ulv;
	struct cgs_system_info sys_info = {0};

	PP_ASSERT_WITH_CODE((NULL != hwmgr),
		"Invalid Parameter!", return -EINVAL;);

	data->dll_defaule_on = 0;
	data->sram_end = SMC_RAM_END;

	data->activity_target[0] = PPICELAND_TARGETACTIVITY_DFLT;
	data->activity_target[1] = PPICELAND_TARGETACTIVITY_DFLT;
	data->activity_target[2] = PPICELAND_TARGETACTIVITY_DFLT;
	data->activity_target[3] = PPICELAND_TARGETACTIVITY_DFLT;
	data->activity_target[4] = PPICELAND_TARGETACTIVITY_DFLT;
	data->activity_target[5] = PPICELAND_TARGETACTIVITY_DFLT;
	data->activity_target[6] = PPICELAND_TARGETACTIVITY_DFLT;
	data->activity_target[7] = PPICELAND_TARGETACTIVITY_DFLT;

	data->mclk_activity_target = PPICELAND_MCLK_TARGETACTIVITY_DFLT;

	data->sclk_dpm_key_disabled = 0;
	data->mclk_dpm_key_disabled = 0;
	data->pcie_dpm_key_disabled = 0;
	data->pcc_monitor_enabled = 0;

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_UnTabledHardwareInterface);

	data->gpio_debug = 0;
	data->engine_clock_data = 0;
	data->memory_clock_data = 0;

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
		      PHM_PlatformCaps_SclkDeepSleepAboveLow);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_DynamicPatchPowerState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_TablelessHardwareInterface);

	/* Initializes DPM default values. */
	iceland_initialize_dpm_defaults(hwmgr);

	/* Enable Platform EVV support. */
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_EVV);

	/* Get leakage voltage based on leakage ID. */
	result = iceland_get_evv_voltage(hwmgr);
	if (result)
		goto failed;

	/**
	 * Patch our voltage dependency table with actual leakage
	 * voltage. We need to perform leakage translation before it's
	 * used by other functions such as
	 * iceland_set_hwmgr_variables_based_on_pptable.
	 */
	result = iceland_patch_dependency_tables_with_leakage(hwmgr);
	if (result)
		goto failed;

	/* Parse pptable data read from VBIOS. */
	result = iceland_set_private_var_based_on_pptale(hwmgr);
	if (result)
		goto failed;

	/* ULV support */
	ulv = &(data->ulv);
	ulv->ulv_supported = 1;

	/* Initalize Dynamic State Adjustment Rule Settings*/
	result = iceland_initializa_dynamic_state_adjustment_rule_settings(hwmgr);
	if (result) {
		pr_err("[ powerplay ] iceland_initializa_dynamic_state_adjustment_rule_settings failed!\n");
		goto failed;
	}

	data->voltage_control = ICELAND_VOLTAGE_CONTROL_NONE;
	data->vdd_ci_control = ICELAND_VOLTAGE_CONTROL_NONE;
	data->mvdd_control = ICELAND_VOLTAGE_CONTROL_NONE;

	/*
	 * Hardcode thermal temperature settings for now, these will
	 * be overwritten if a custom policy exists.
	 */
	data->thermal_temp_setting.temperature_low = 99500;
	data->thermal_temp_setting.temperature_high = 100000;
	data->thermal_temp_setting.temperature_shutdown = 104000;
	data->uvd_enabled = false;

	table = &data->smc_state_table;

	if (atomctrl_get_pp_assign_pin(hwmgr, VDDC_VRHOT_GPIO_PINID,
				       &gpio_pin_assignment)) {
		table->VRHotGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_RegulatorHot);
	} else {
		table->VRHotGpio = ICELAND_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			      PHM_PlatformCaps_RegulatorHot);
	}

	if (atomctrl_get_pp_assign_pin(hwmgr, PP_AC_DC_SWITCH_GPIO_PINID,
				       &gpio_pin_assignment)) {
		table->AcDcGpio = gpio_pin_assignment.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_AutomaticDCTransition);
	} else {
		table->AcDcGpio = ICELAND_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			      PHM_PlatformCaps_AutomaticDCTransition);
	}

	/*
	 * If ucGPIO_ID=VDDC_PCC_GPIO_PINID in GPIO_LUTable, Peak.
	 * Current Control feature is enabled and we should program
	 * PCC HW register
	 */
	if (atomctrl_get_pp_assign_pin(hwmgr, VDDC_PCC_GPIO_PINID,
				       &gpio_pin_assignment)) {
		uint32_t temp_reg = cgs_read_ind_register(hwmgr->device,
							  CGS_IND_REG__SMC,
							  ixCNB_PWRMGT_CNTL);

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
			pr_warning("[ powerplay ] Failed to setup PCC HW register! Wrong GPIO assigned for VDDC_PCC_GPIO_PINID!\n");
			break;
		}
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				       ixCNB_PWRMGT_CNTL, temp_reg);
	}

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_EnableSMU7ThermalManagement);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		    PHM_PlatformCaps_SMU7);

	if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
						     VOLTAGE_TYPE_VDDC,
						     VOLTAGE_OBJ_GPIO_LUT))
		data->voltage_control = ICELAND_VOLTAGE_CONTROL_BY_GPIO;
	else if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
							  VOLTAGE_TYPE_VDDC,
							  VOLTAGE_OBJ_SVID2))
		data->voltage_control = ICELAND_VOLTAGE_CONTROL_BY_SVID2;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_ControlVDDCI)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
							     VOLTAGE_TYPE_VDDCI,
							     VOLTAGE_OBJ_GPIO_LUT))
			data->vdd_ci_control = ICELAND_VOLTAGE_CONTROL_BY_GPIO;
		else if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
								  VOLTAGE_TYPE_VDDCI,
								  VOLTAGE_OBJ_SVID2))
			data->vdd_ci_control = ICELAND_VOLTAGE_CONTROL_BY_SVID2;
	}

	if (data->vdd_ci_control == ICELAND_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			      PHM_PlatformCaps_ControlVDDCI);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			    PHM_PlatformCaps_EnableMVDDControl)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
							     VOLTAGE_TYPE_MVDDC,
							     VOLTAGE_OBJ_GPIO_LUT))
			data->mvdd_control = ICELAND_VOLTAGE_CONTROL_BY_GPIO;
		else if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
								  VOLTAGE_TYPE_MVDDC,
								  VOLTAGE_OBJ_SVID2))
			data->mvdd_control = ICELAND_VOLTAGE_CONTROL_BY_SVID2;
	}

	if (data->mvdd_control == ICELAND_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			      PHM_PlatformCaps_EnableMVDDControl);

	data->vddc_phase_shed_control = false;

	stay_in_boot = phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				       PHM_PlatformCaps_StayInBootState);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPowerManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ActivityReporting);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_GFXClockGatingSupport);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MemorySpreadSpectrumSupport);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EngineSpreadSpectrumSupport);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPCIEGen2Support);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SMC);

	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DisablePowerGating);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_BACO);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalAutoThrottling);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DisableLSClockGating);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SamuDPM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AcpDPM);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_OD6inACSupport);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnablePlatformPowerManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PauseMMSessions);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_OD6PlusinACSupport);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PauseMMSessions);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_GFXClockGatingManagedInCAIL);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_IcelandULPSSWWorkAround);


	/* iceland doesn't support UVD and VCE */
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

		data->is_tlu_enabled = false;
		hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
			ICELAND_MAX_HARDWARE_POWERLEVELS;
		hwmgr->platform_descriptor.hardwarePerformanceLevels = 2;
		hwmgr->platform_descriptor.minimumClocksReductionPercentage  = 50;

		sys_info.size = sizeof(struct cgs_system_info);
		sys_info.info_id = CGS_SYSTEM_INFO_PCIE_GEN_INFO;
		result = cgs_query_system_info(hwmgr->device, &sys_info);
		if (result)
			data->pcie_gen_cap = AMDGPU_DEFAULT_PCIE_GEN_MASK;
		else
			data->pcie_gen_cap = (uint32_t)sys_info.value;
		if (data->pcie_gen_cap & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
			data->pcie_spc_cap = 20;
		sys_info.size = sizeof(struct cgs_system_info);
		sys_info.info_id = CGS_SYSTEM_INFO_PCIE_MLW;
		result = cgs_query_system_info(hwmgr->device, &sys_info);
		if (result)
			data->pcie_lane_cap = AMDGPU_DEFAULT_PCIE_MLW_MASK;
		else
			data->pcie_lane_cap = (uint32_t)sys_info.value;
	} else {
		/* Ignore return value in here, we are cleaning up a mess. */
		iceland_hwmgr_backend_fini(hwmgr);
	}

	return 0;
failed:
	return result;
}

static int iceland_get_num_of_entries(struct pp_hwmgr *hwmgr)
{
	int result;
	unsigned long ret = 0;

	result = pp_tables_get_num_of_entries(hwmgr, &ret);

	return result ? 0 : ret;
}

static const unsigned long PhwIceland_Magic = (unsigned long)(PHM_VIslands_Magic);

struct iceland_power_state *cast_phw_iceland_power_state(
				  struct pp_hw_power_state *hw_ps)
{
	if (hw_ps == NULL)
		return NULL;

	PP_ASSERT_WITH_CODE((PhwIceland_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL);

	return (struct iceland_power_state *)hw_ps;
}

static int iceland_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state  *prequest_ps,
			const struct pp_power_state *pcurrent_ps)
{
	struct iceland_power_state *iceland_ps =
				cast_phw_iceland_power_state(&prequest_ps->hardware);

	uint32_t sclk;
	uint32_t mclk;
	struct PP_Clocks minimum_clocks = {0};
	bool disable_mclk_switching;
	bool disable_mclk_switching_for_frame_lock;
	struct cgs_display_info info = {0};
	const struct phm_clock_and_voltage_limits *max_limits;
	uint32_t i;
	iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	int32_t count;
	int32_t stable_pstate_sclk = 0, stable_pstate_mclk = 0;

	data->battery_state = (PP_StateUILabel_Battery == prequest_ps->classification.ui_label);

	PP_ASSERT_WITH_CODE(iceland_ps->performance_level_count == 2,
				 "VI should always have 2 performance levels",
				 );

	max_limits = (PP_PowerSource_AC == hwmgr->power_source) ?
			&(hwmgr->dyn_state.max_clock_voltage_on_ac) :
			&(hwmgr->dyn_state.max_clock_voltage_on_dc);

	if (PP_PowerSource_DC == hwmgr->power_source) {
		for (i = 0; i < iceland_ps->performance_level_count; i++) {
			if (iceland_ps->performance_levels[i].memory_clock > max_limits->mclk)
				iceland_ps->performance_levels[i].memory_clock = max_limits->mclk;
			if (iceland_ps->performance_levels[i].engine_clock > max_limits->sclk)
				iceland_ps->performance_levels[i].engine_clock = max_limits->sclk;
		}
	}

	iceland_ps->vce_clocks.EVCLK = hwmgr->vce_arbiter.evclk;
	iceland_ps->vce_clocks.ECCLK = hwmgr->vce_arbiter.ecclk;

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState)) {

		max_limits = &(hwmgr->dyn_state.max_clock_voltage_on_ac);
		stable_pstate_sclk = (max_limits->sclk * 75) / 100;

		for (count = hwmgr->dyn_state.vddc_dependency_on_sclk->count-1; count >= 0; count--) {
			if (stable_pstate_sclk >= hwmgr->dyn_state.vddc_dependency_on_sclk->entries[count].clk) {
				stable_pstate_sclk = hwmgr->dyn_state.vddc_dependency_on_sclk->entries[count].clk;
				break;
			}
		}

		if (count < 0)
			stable_pstate_sclk = hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].clk;

		stable_pstate_mclk = max_limits->mclk;

		minimum_clocks.engineClock = stable_pstate_sclk;
		minimum_clocks.memoryClock = stable_pstate_mclk;
	}

	if (minimum_clocks.engineClock < hwmgr->gfx_arbiter.sclk)
		minimum_clocks.engineClock = hwmgr->gfx_arbiter.sclk;

	if (minimum_clocks.memoryClock < hwmgr->gfx_arbiter.mclk)
		minimum_clocks.memoryClock = hwmgr->gfx_arbiter.mclk;

	iceland_ps->sclk_threshold = hwmgr->gfx_arbiter.sclk_threshold;

	if (0 != hwmgr->gfx_arbiter.sclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.sclk_over_drive <= hwmgr->platform_descriptor.overdriveLimit.engineClock),
					"Overdrive sclk exceeds limit",
					hwmgr->gfx_arbiter.sclk_over_drive = hwmgr->platform_descriptor.overdriveLimit.engineClock);

		if (hwmgr->gfx_arbiter.sclk_over_drive >= hwmgr->gfx_arbiter.sclk)
			iceland_ps->performance_levels[1].engine_clock = hwmgr->gfx_arbiter.sclk_over_drive;
	}

	if (0 != hwmgr->gfx_arbiter.mclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.mclk_over_drive <= hwmgr->platform_descriptor.overdriveLimit.memoryClock),
			"Overdrive mclk exceeds limit",
			hwmgr->gfx_arbiter.mclk_over_drive = hwmgr->platform_descriptor.overdriveLimit.memoryClock);

		if (hwmgr->gfx_arbiter.mclk_over_drive >= hwmgr->gfx_arbiter.mclk)
			iceland_ps->performance_levels[1].memory_clock = hwmgr->gfx_arbiter.mclk_over_drive;
	}

	disable_mclk_switching_for_frame_lock = phm_cap_enabled(
				    hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_DisableMclkSwitchingForFrameLock);

	disable_mclk_switching = (1 < info.display_count) ||
				    disable_mclk_switching_for_frame_lock;

	sclk  = iceland_ps->performance_levels[0].engine_clock;
	mclk  = iceland_ps->performance_levels[0].memory_clock;

	if (disable_mclk_switching)
		mclk  = iceland_ps->performance_levels[iceland_ps->performance_level_count - 1].memory_clock;

	if (sclk < minimum_clocks.engineClock)
		sclk = (minimum_clocks.engineClock > max_limits->sclk) ? max_limits->sclk : minimum_clocks.engineClock;

	if (mclk < minimum_clocks.memoryClock)
		mclk = (minimum_clocks.memoryClock > max_limits->mclk) ? max_limits->mclk : minimum_clocks.memoryClock;

	iceland_ps->performance_levels[0].engine_clock = sclk;
	iceland_ps->performance_levels[0].memory_clock = mclk;

	iceland_ps->performance_levels[1].engine_clock =
		(iceland_ps->performance_levels[1].engine_clock >= iceland_ps->performance_levels[0].engine_clock) ?
			      iceland_ps->performance_levels[1].engine_clock :
			      iceland_ps->performance_levels[0].engine_clock;

	if (disable_mclk_switching) {
		if (mclk < iceland_ps->performance_levels[1].memory_clock)
			mclk = iceland_ps->performance_levels[1].memory_clock;

		iceland_ps->performance_levels[0].memory_clock = mclk;
		iceland_ps->performance_levels[1].memory_clock = mclk;
	} else {
		if (iceland_ps->performance_levels[1].memory_clock < iceland_ps->performance_levels[0].memory_clock)
			iceland_ps->performance_levels[1].memory_clock = iceland_ps->performance_levels[0].memory_clock;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState)) {
		for (i=0; i < iceland_ps->performance_level_count; i++) {
			iceland_ps->performance_levels[i].engine_clock = stable_pstate_sclk;
			iceland_ps->performance_levels[i].memory_clock = stable_pstate_mclk;
			iceland_ps->performance_levels[i].pcie_gen = data->pcie_gen_performance.max;
			iceland_ps->performance_levels[i].pcie_lane = data->pcie_gen_performance.max;
		}
	}

	return 0;
}

static bool iceland_is_dpm_running(struct pp_hwmgr *hwmgr)
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
 * force DPM power State
 *
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    n     :  DPM level
 * @return   The response that came from the SMC.
 */
int iceland_dpm_force_state(struct pp_hwmgr *hwmgr, uint32_t n)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	/* Checking if DPM is running.  If we discover hang because of this, we should skip this message. */
	PP_ASSERT_WITH_CODE(0 == iceland_is_dpm_running(hwmgr),
			"Trying to force SCLK when DPM is disabled", return -1;);
	if (0 == data->sclk_dpm_key_disabled)
		return (0 == smum_send_msg_to_smc_with_parameter(
							     hwmgr->smumgr,
							     PPSMC_MSG_DPM_ForceState,
							     n) ? 0 : 1);

	return 0;
}

/**
 * force DPM power State
 *
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    n     :  DPM level
 * @return   The response that came from the SMC.
 */
int iceland_dpm_force_state_mclk(struct pp_hwmgr *hwmgr, uint32_t n)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	/* Checking if DPM is running.  If we discover hang because of this, we should skip this message. */
	PP_ASSERT_WITH_CODE(0 == iceland_is_dpm_running(hwmgr),
			"Trying to Force MCLK when DPM is disabled", return -1;);
	if (0 == data->mclk_dpm_key_disabled)
		return (0 == smum_send_msg_to_smc_with_parameter(
								hwmgr->smumgr,
								PPSMC_MSG_MCLKDPM_ForceState,
								n) ? 0 : 1);

	return 0;
}

/**
 * force DPM power State
 *
 * @param    hwmgr:  the address of the powerplay hardware manager.
 * @param    n     :  DPM level
 * @return   The response that came from the SMC.
 */
int iceland_dpm_force_state_pcie(struct pp_hwmgr *hwmgr, uint32_t n)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
	PP_ASSERT_WITH_CODE(0 == iceland_is_dpm_running(hwmgr),
			"Trying to Force PCIE level when DPM is disabled", return -1;);
	if (0 == data->pcie_dpm_key_disabled)
		return (0 == smum_send_msg_to_smc_with_parameter(
							     hwmgr->smumgr,
							     PPSMC_MSG_PCIeDPM_ForceLevel,
							     n) ? 0 : 1);

	return 0;
}

static int iceland_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	uint32_t level, tmp;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	if (0 == data->sclk_dpm_key_disabled) {
		/* SCLK */
		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask != 0) {
			level = 0;
			tmp = data->dpm_level_enable_mask.sclk_dpm_enable_mask;
			while (tmp >>= 1)
				level++ ;

			if (0 != level) {
				PP_ASSERT_WITH_CODE((0 == iceland_dpm_force_state(hwmgr, level)),
					"force highest sclk dpm state failed!", return -1);
				PHM_WAIT_INDIRECT_FIELD(hwmgr->device,
					SMC_IND, TARGET_AND_CURRENT_PROFILE_INDEX, CURR_SCLK_INDEX, level);
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
				PP_ASSERT_WITH_CODE((0 == iceland_dpm_force_state_mclk(hwmgr, level)),
					"force highest mclk dpm state failed!", return -1);
				PHM_WAIT_INDIRECT_FIELD(hwmgr->device, SMC_IND,
					TARGET_AND_CURRENT_PROFILE_INDEX, CURR_MCLK_INDEX, level);
			}
		}
	}

	if (0 == data->pcie_dpm_key_disabled) {
		/* PCIE */
		if (data->dpm_level_enable_mask.pcie_dpm_enable_mask != 0) {
			level = 0;
			tmp = data->dpm_level_enable_mask.pcie_dpm_enable_mask;
			while (tmp >>= 1)
				level++ ;

			if (0 != level) {
				PP_ASSERT_WITH_CODE((0 == iceland_dpm_force_state_pcie(hwmgr, level)),
					"force highest pcie dpm state failed!", return -1);
			}
		}
	}

	return 0;
}

static uint32_t iceland_get_lowest_enable_level(struct pp_hwmgr *hwmgr,
						uint32_t level_mask)
{
	uint32_t level = 0;

	while (0 == (level_mask & (1 << level)))
		level++;

	return level;
}

static int iceland_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	uint32_t level;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	/* for now force only sclk */
	if (0 != data->dpm_level_enable_mask.sclk_dpm_enable_mask) {
		level = iceland_get_lowest_enable_level(hwmgr,
						      data->dpm_level_enable_mask.sclk_dpm_enable_mask);

		PP_ASSERT_WITH_CODE((0 == iceland_dpm_force_state(hwmgr, level)),
				    "force sclk dpm state failed!", return -1);

		PHM_WAIT_INDIRECT_FIELD(hwmgr->device, SMC_IND,
					TARGET_AND_CURRENT_PROFILE_INDEX,
					CURR_SCLK_INDEX,
					level);
	}

	return 0;
}

int iceland_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	PP_ASSERT_WITH_CODE (0 == iceland_is_dpm_running(hwmgr),
		"Trying to Unforce DPM when DPM is disabled. Returning without sending SMC message.",
		return -1);

	if (0 == data->sclk_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE((0 == smum_send_msg_to_smc(
							     hwmgr->smumgr,
					PPSMC_MSG_NoForcedLevel)),
					   "unforce sclk dpm state failed!",
								return -1);
	}

	if (0 == data->mclk_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE((0 == smum_send_msg_to_smc(
							     hwmgr->smumgr,
					PPSMC_MSG_MCLKDPM_NoForcedLevel)),
					   "unforce mclk dpm state failed!",
								return -1);
	}

	if (0 == data->pcie_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE((0 == smum_send_msg_to_smc(
							     hwmgr->smumgr,
					PPSMC_MSG_PCIeDPM_UnForceLevel)),
					   "unforce pcie level failed!",
								return -1);
	}

	return 0;
}

static int iceland_force_dpm_level(struct pp_hwmgr *hwmgr,
		enum amd_dpm_forced_level level)
{
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = iceland_force_dpm_highest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = iceland_force_dpm_lowest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = iceland_unforce_dpm_levels(hwmgr);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	hwmgr->dpm_level = level;
	return ret;
}

const struct iceland_power_state *cast_const_phw_iceland_power_state(
				 const struct pp_hw_power_state *hw_ps)
{
	if (hw_ps == NULL)
		return NULL;

	PP_ASSERT_WITH_CODE((PhwIceland_Magic == hw_ps->magic),
			    "Invalid Powerstate Type!",
			    return NULL);

	return (const struct iceland_power_state *)hw_ps;
}

static int iceland_find_dpm_states_clocks_in_dpm_table(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	const struct iceland_power_state *iceland_ps = cast_const_phw_iceland_power_state(states->pnew_state);
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_single_dpm_table *psclk_table = &(data->dpm_table.sclk_table);
	uint32_t sclk = iceland_ps->performance_levels[iceland_ps->performance_level_count-1].engine_clock;
	struct iceland_single_dpm_table *pmclk_table = &(data->dpm_table.mclk_table);
	uint32_t mclk = iceland_ps->performance_levels[iceland_ps->performance_level_count-1].memory_clock;
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
		/*
		 * TODO: Check SCLK in DAL's minimum clocks in case DeepSleep
		 * divider update is required.
		 */
		if(data->display_timing.min_clock_insr != min_clocks.engineClockInSR)
			data->need_update_smu7_dpm_table |= DPMTABLE_UPDATE_SCLK;
	}

	for (i = 0; i < pmclk_table->count; i++) {
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

static uint16_t iceland_get_maximum_link_speed(struct pp_hwmgr *hwmgr, const struct iceland_power_state *hw_ps)
{
	uint32_t i;
	uint32_t pcie_speed, max_speed = 0;

	for (i = 0; i < hw_ps->performance_level_count; i++) {
		pcie_speed = hw_ps->performance_levels[i].pcie_gen;
		if (max_speed < pcie_speed)
			max_speed = pcie_speed;
	}

	return max_speed;
}

static uint16_t iceland_get_current_pcie_speed(struct pp_hwmgr *hwmgr)
{
	uint32_t speed_cntl = 0;

	speed_cntl = cgs_read_ind_register(hwmgr->device,
					   CGS_IND_REG__PCIE,
					   ixPCIE_LC_SPEED_CNTL);
	return((uint16_t)PHM_GET_FIELD(speed_cntl,
			PCIE_LC_SPEED_CNTL, LC_CURRENT_DATA_RATE));
}


static int iceland_request_link_speed_change_before_state_change(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	const struct iceland_power_state *iceland_nps = cast_const_phw_iceland_power_state(states->pnew_state);
	const struct iceland_power_state *iceland_cps = cast_const_phw_iceland_power_state(states->pcurrent_state);

	uint16_t target_link_speed = iceland_get_maximum_link_speed(hwmgr, iceland_nps);
	uint16_t current_link_speed;

	if (data->force_pcie_gen == PP_PCIEGenInvalid)
		current_link_speed = iceland_get_maximum_link_speed(hwmgr, iceland_cps);
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
			data->force_pcie_gen = iceland_get_current_pcie_speed(hwmgr);
			break;
		}
	} else {
		if (target_link_speed < current_link_speed)
			data->pspp_notify_required = true;
	}

	return 0;
}

static int iceland_freeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {
		PP_ASSERT_WITH_CODE(
			0 == iceland_is_dpm_running(hwmgr),
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
		PP_ASSERT_WITH_CODE(0 == iceland_is_dpm_running(hwmgr),
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

static int iceland_populate_and_upload_sclk_mclk_dpm_levels(struct pp_hwmgr *hwmgr, const void *input)
{
	int result = 0;

	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	const struct iceland_power_state *iceland_ps = cast_const_phw_iceland_power_state(states->pnew_state);
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint32_t sclk = iceland_ps->performance_levels[iceland_ps->performance_level_count-1].engine_clock;
	uint32_t mclk = iceland_ps->performance_levels[iceland_ps->performance_level_count-1].memory_clock;
	struct iceland_dpm_table *pdpm_table = &data->dpm_table;

	struct iceland_dpm_table *pgolden_dpm_table = &data->golden_dpm_table;
	uint32_t dpm_count, clock_percent;
	uint32_t i;

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_SCLK) {
		pdpm_table->sclk_table.dpm_levels[pdpm_table->sclk_table.count-1].value = sclk;

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinACSupport) ||
		    phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinDCSupport)) {
			/*
			 * Need to do calculation based on the golden DPM table
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
		result = iceland_populate_all_graphic_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
			"Failed to populate SCLK during PopulateNewDPMClocksStates Function!",
			return result);
	}

	if (data->need_update_smu7_dpm_table & (DPMTABLE_OD_UPDATE_MCLK + DPMTABLE_UPDATE_MCLK)) {
		/*populate MCLK dpm table to SMU7 */
		result = iceland_populate_all_memory_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate MCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	return result;
}

static int iceland_trim_single_dpm_states(struct pp_hwmgr *hwmgr,
			  struct iceland_single_dpm_table *pdpm_table,
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

static int iceland_trim_dpm_states(struct pp_hwmgr *hwmgr, const struct iceland_power_state *hw_state)
{
	int result = 0;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint32_t high_limit_count;

	PP_ASSERT_WITH_CODE((hw_state->performance_level_count >= 1),
				"power state did not have any performance level",
				 return -1);

	high_limit_count = (1 == hw_state->performance_level_count) ? 0: 1;

	iceland_trim_single_dpm_states(hwmgr, &(data->dpm_table.sclk_table),
					hw_state->performance_levels[0].engine_clock,
					hw_state->performance_levels[high_limit_count].engine_clock);

	iceland_trim_single_dpm_states(hwmgr, &(data->dpm_table.mclk_table),
					hw_state->performance_levels[0].memory_clock,
					hw_state->performance_levels[high_limit_count].memory_clock);

	return result;
}

static int iceland_generate_dpm_level_enable_mask(struct pp_hwmgr *hwmgr, const void *input)
{
	int result;
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	const struct iceland_power_state *iceland_ps = cast_const_phw_iceland_power_state(states->pnew_state);

	result = iceland_trim_dpm_states(hwmgr, iceland_ps);
	if (0 != result)
		return result;

	data->dpm_level_enable_mask.sclk_dpm_enable_mask = iceland_get_dpm_level_enable_mask_value(&data->dpm_table.sclk_table);
	data->dpm_level_enable_mask.mclk_dpm_enable_mask = iceland_get_dpm_level_enable_mask_value(&data->dpm_table.mclk_table);
	data->last_mclk_dpm_enable_mask = data->dpm_level_enable_mask.mclk_dpm_enable_mask;
	if (data->uvd_enabled && (data->dpm_level_enable_mask.mclk_dpm_enable_mask & 1))
		data->dpm_level_enable_mask.mclk_dpm_enable_mask &= 0xFFFFFFFE;

	data->dpm_level_enable_mask.pcie_dpm_enable_mask = iceland_get_dpm_level_enable_mask_value(&data->dpm_table.pcie_speed_table);

	return 0;
}

static int iceland_update_vce_dpm(struct pp_hwmgr *hwmgr, const void *input)
{
	return 0;
}

int iceland_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	int result = 0;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification)
		&& (hwmgr->gfx_arbiter.sclk_threshold != data->low_sclk_interrupt_threshold)) {
		data->low_sclk_interrupt_threshold = hwmgr->gfx_arbiter.sclk_threshold;
		low_sclk_interrupt_threshold = data->low_sclk_interrupt_threshold;

		CONVERT_FROM_HOST_TO_SMC_UL(low_sclk_interrupt_threshold);

		result = iceland_copy_bytes_to_smc(
				hwmgr->smumgr,
				data->dpm_table_start + offsetof(SMU71_Discrete_DpmTable,
				LowSclkInterruptThreshold),
				(uint8_t *)&low_sclk_interrupt_threshold,
				sizeof(uint32_t),
				data->sram_end
				);
	}

	return result;
}

static int iceland_update_and_upload_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	uint32_t address;
	int32_t result;

	if (0 == (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK))
		return 0;


	memset(&data->mc_reg_table, 0, sizeof(SMU71_Discrete_MCRegisters));

	result = iceland_convert_mc_reg_table_to_smc(hwmgr, &(data->mc_reg_table));

	if(result != 0)
		return result;


	address = data->mc_reg_table_start + (uint32_t)offsetof(SMU71_Discrete_MCRegisters, data[0]);

	return  iceland_copy_bytes_to_smc(hwmgr->smumgr, address,
				 (uint8_t *)&data->mc_reg_table.data[0],
				sizeof(SMU71_Discrete_MCRegisterSet) * data->dpm_table.mclk_table.count,
				data->sram_end);
}

static int iceland_program_memory_timing_parameters_conditionally(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	if (data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_OD_UPDATE_MCLK))
		return iceland_program_memory_timing_parameters(hwmgr);

	return 0;
}

static int iceland_unfreeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {

		PP_ASSERT_WITH_CODE(0 == iceland_is_dpm_running(hwmgr),
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
				0 == iceland_is_dpm_running(hwmgr),
				"Trying to Unfreeze MCLK DPM when DPM is disabled",
				);
		PP_ASSERT_WITH_CODE(
			 0 == smum_send_msg_to_smc(hwmgr->smumgr,
					 PPSMC_MSG_MCLKDPM_UnfreezeLevel),
		    "Failed to unfreeze MCLK DPM during UnFreezeSclkMclkDPM Function!",
		    return -1);
	}

	data->need_update_smu7_dpm_table = 0;

	return 0;
}

static int iceland_notify_link_speed_change_after_state_change(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states = (const struct phm_set_power_state_input *)input;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	const struct iceland_power_state *iceland_ps = cast_const_phw_iceland_power_state(states->pnew_state);
	uint16_t target_link_speed = iceland_get_maximum_link_speed(hwmgr, iceland_ps);
	uint8_t  request;

	if (data->pspp_notify_required  ||
	    data->pcie_performance_request) {
		if (target_link_speed == PP_PCIEGen3)
			request = PCIE_PERF_REQ_GEN3;
		else if (target_link_speed == PP_PCIEGen2)
			request = PCIE_PERF_REQ_GEN2;
		else
			request = PCIE_PERF_REQ_GEN1;

		if(request == PCIE_PERF_REQ_GEN1 && iceland_get_current_pcie_speed(hwmgr) > 0) {
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

int iceland_upload_dpm_level_enable_mask(struct pp_hwmgr *hwmgr)
{
	PPSMC_Result result;
	iceland_hwmgr *data = (iceland_hwmgr *)(hwmgr->backend);

	if (0 == data->sclk_dpm_key_disabled) {
		/* Checking if DPM is running.  If we discover hang because of this, we should skip this message.*/
		if (0 != iceland_is_dpm_running(hwmgr))
			printk(KERN_ERR "[ powerplay ] Trying to set Enable Sclk Mask when DPM is disabled \n");

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
		if (0 != iceland_is_dpm_running(hwmgr))
			printk(KERN_ERR "[ powerplay ] Trying to set Enable Mclk Mask when DPM is disabled \n");

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

static int iceland_set_power_state_tasks(struct pp_hwmgr *hwmgr, const void *input)
{
	int tmp_result, result = 0;

	tmp_result = iceland_find_dpm_states_clocks_in_dpm_table(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to find DPM states clocks in DPM table!", result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result = iceland_request_link_speed_change_before_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to request link speed change before state change!", result = tmp_result);
	}

	tmp_result = iceland_freeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to freeze SCLK MCLK DPM!", result = tmp_result);

	tmp_result = iceland_populate_and_upload_sclk_mclk_dpm_levels(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to populate and upload SCLK MCLK DPM levels!", result = tmp_result);

	tmp_result = iceland_generate_dpm_level_enable_mask(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to generate DPM level enabled mask!", result = tmp_result);

	tmp_result = iceland_update_vce_dpm(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to update VCE DPM!", result = tmp_result);

	tmp_result = iceland_update_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to update SCLK threshold!", result = tmp_result);

	tmp_result = iceland_update_and_upload_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to upload MC reg table!", result = tmp_result);

	tmp_result = iceland_program_memory_timing_parameters_conditionally(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to program memory timing parameters!", result = tmp_result);

	tmp_result = iceland_unfreeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to unfreeze SCLK MCLK DPM!", result = tmp_result);

	tmp_result = iceland_upload_dpm_level_enable_mask(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to upload DPM level enabled mask!", result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result = iceland_notify_link_speed_change_after_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result), "Failed to notify link speed change after state change!", result = tmp_result);
	}

	return result;
}

static int iceland_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct iceland_power_state);
}

static int iceland_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct iceland_power_state  *iceland_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	iceland_ps = cast_phw_iceland_power_state(&ps->hardware);

	if (low)
		return iceland_ps->performance_levels[0].memory_clock;
	else
		return iceland_ps->performance_levels[iceland_ps->performance_level_count-1].memory_clock;
}

static int iceland_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct iceland_power_state  *iceland_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	iceland_ps = cast_phw_iceland_power_state(&ps->hardware);

	if (low)
		return iceland_ps->performance_levels[0].engine_clock;
	else
		return iceland_ps->performance_levels[iceland_ps->performance_level_count-1].engine_clock;
}

static int iceland_get_current_pcie_lane_number(
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

static int iceland_dpm_patch_boot_state(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *hw_ps)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_power_state *ps = (struct iceland_power_state *)hw_ps;
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
	data->vbios_boot_state.pcie_gen_bootup_value = iceland_get_current_pcie_speed(hwmgr);
	data->vbios_boot_state.pcie_lane_bootup_value =
			(uint16_t)iceland_get_current_pcie_lane_number(hwmgr);

	/* set boot power state */
	ps->performance_levels[0].memory_clock = data->vbios_boot_state.mclk_bootup_value;
	ps->performance_levels[0].engine_clock = data->vbios_boot_state.sclk_bootup_value;
	ps->performance_levels[0].pcie_gen = data->vbios_boot_state.pcie_gen_bootup_value;
	ps->performance_levels[0].pcie_lane = data->vbios_boot_state.pcie_lane_bootup_value;

	return 0;
}

static int iceland_get_pp_table_entry_callback_func(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *power_state,
					unsigned int index, const void *clock_info)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_power_state  *iceland_power_state = cast_phw_iceland_power_state(power_state);
	const ATOM_PPLIB_CI_CLOCK_INFO *visland_clk_info = clock_info;
	struct iceland_performance_level *performance_level;
	uint32_t engine_clock, memory_clock;
	uint16_t pcie_gen_from_bios;

	engine_clock = visland_clk_info->ucEngineClockHigh << 16 | visland_clk_info->usEngineClockLow;
	memory_clock = visland_clk_info->ucMemoryClockHigh << 16 | visland_clk_info->usMemoryClockLow;

	if (!(data->mc_micro_code_feature & DISABLE_MC_LOADMICROCODE) && memory_clock > data->highest_mclk)
		data->highest_mclk = memory_clock;

	performance_level = &(iceland_power_state->performance_levels
			[iceland_power_state->performance_level_count++]);

	PP_ASSERT_WITH_CODE(
			(iceland_power_state->performance_level_count < SMU71_MAX_LEVELS_GRAPHICS),
			"Performance levels exceeds SMC limit!",
			return -1);

	PP_ASSERT_WITH_CODE(
			(iceland_power_state->performance_level_count <=
					hwmgr->platform_descriptor.hardwareActivityPerformanceLevels),
			"Performance levels exceeds Driver limit!",
			return -1);

	/* Performance levels are arranged from low to high. */
	performance_level->memory_clock = memory_clock;
	performance_level->engine_clock = engine_clock;

	pcie_gen_from_bios = visland_clk_info->ucPCIEGen;

	performance_level->pcie_gen = get_pcie_gen_support(data->pcie_gen_cap, pcie_gen_from_bios);
	performance_level->pcie_lane = get_pcie_lane_support(data->pcie_lane_cap, visland_clk_info->usPCIELane);

	return 0;
}

static int iceland_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		unsigned long entry_index, struct pp_power_state *state)
{
	int result;
	struct iceland_power_state *ps;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct phm_clock_voltage_dependency_table *dep_mclk_table =
			hwmgr->dyn_state.vddci_dependency_on_mclk;

	memset(&state->hardware, 0x00, sizeof(struct pp_hw_power_state));

	state->hardware.magic = PHM_VIslands_Magic;

	ps = (struct iceland_power_state *)(&state->hardware);

	result = pp_tables_get_entry(hwmgr, entry_index, state,
			iceland_get_pp_table_entry_callback_func);

	/*
	 * This is the earliest time we have all the dependency table
	 * and the VBIOS boot state as
	 * PP_Tables_GetPowerPlayTableEntry retrieves the VBIOS boot
	 * state if there is only one VDDCI/MCLK level, check if it's
	 * the same as VBIOS boot state
	 */
	if (dep_mclk_table != NULL && dep_mclk_table->count == 1) {
		if (dep_mclk_table->entries[0].clk !=
				data->vbios_boot_state.mclk_bootup_value)
			printk(KERN_ERR "Single MCLK entry VDDCI/MCLK dependency table "
					"does not match VBIOS boot MCLK level");
		if (dep_mclk_table->entries[0].v !=
				data->vbios_boot_state.vddci_bootup_value)
			printk(KERN_ERR "Single VDDCI entry VDDCI/MCLK dependency table "
					"does not match VBIOS boot VDDCI level");
	}

	/* set DC compatible flag if this state supports DC */
	if (!state->validation.disallowOnDC)
		ps->dc_compatible = true;

	if (state->classification.flags & PP_StateClassificationFlag_ACPI)
		data->acpi_pcie_gen = ps->performance_levels[0].pcie_gen;
	else if (0 != (state->classification.flags & PP_StateClassificationFlag_Boot)) {
		if (data->bacos.best_match == 0xffff) {
			/* For C.I. use boot state as base BACO state */
			data->bacos.best_match = PP_StateClassificationFlag_Boot;
			data->bacos.performance_level = ps->performance_levels[0];
		}
	}


	ps->uvd_clocks.VCLK = state->uvd_clocks.VCLK;
	ps->uvd_clocks.DCLK = state->uvd_clocks.DCLK;

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

static void
iceland_print_current_perforce_level(struct pp_hwmgr *hwmgr, struct seq_file *m)
{
	uint32_t sclk, mclk, activity_percent;
	uint32_t offset;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	smum_send_msg_to_smc(hwmgr->smumgr, (PPSMC_Msg)(PPSMC_MSG_API_GetSclkFrequency));

	sclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);

	smum_send_msg_to_smc(hwmgr->smumgr, (PPSMC_Msg)(PPSMC_MSG_API_GetMclkFrequency));

	mclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);
	seq_printf(m, "\n [  mclk  ]: %u MHz\n\n [  sclk  ]: %u MHz\n", mclk/100, sclk/100);

	offset = data->soft_regs_start + offsetof(SMU71_SoftRegisters, AverageGraphicsActivity);
	activity_percent = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset);
	activity_percent += 0x80;
	activity_percent >>= 8;

	seq_printf(m, "\n [GPU load]: %u%%\n\n", activity_percent > 100 ? 100 : activity_percent);

	seq_printf(m, "uvd    %sabled\n", data->uvd_power_gated ? "dis" : "en");

	seq_printf(m, "vce    %sabled\n", data->vce_power_gated ? "dis" : "en");
}

int iceland_notify_smc_display_config_after_ps_adjustment(struct pp_hwmgr *hwmgr)
{
	uint32_t num_active_displays = 0;
	struct cgs_display_info info = {0};
	info.mode_info = NULL;

	cgs_get_active_displays_info(hwmgr->device, &info);

	num_active_displays = info.display_count;

	if (num_active_displays > 1)  /* to do && (pHwMgr->pPECI->displayConfiguration.bMultiMonitorInSync != TRUE)) */
		iceland_notify_smc_display_change(hwmgr, false);
	else
		iceland_notify_smc_display_change(hwmgr, true);

	return 0;
}

/**
* Programs the display gap
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always OK
*/
int iceland_program_display_gap(struct pp_hwmgr *hwmgr)
{
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

	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SOFT_REGISTERS_TABLE_4, PreVBlankGap, 0x64);

	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SOFT_REGISTERS_TABLE_5, VBlankTimeout, (frame_time_in_us - pre_vbi_time_in_us));

	if (num_active_displays == 1)
		iceland_notify_smc_display_change(hwmgr, true);

	return 0;
}

int iceland_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	iceland_program_display_gap(hwmgr);

	return 0;
}

/**
*  Set maximum target operating fan output PWM
*
* @param    pHwMgr:  the address of the powerplay hardware manager.
* @param    usMaxFanPwm:  max operating fan PWM in percents
* @return   The response that came from the SMC.
*/
static int iceland_set_max_fan_pwm_output(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_pwm)
{
	hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanPWM = us_max_fan_pwm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return (0 == smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, PPSMC_MSG_SetFanPwmMax, us_max_fan_pwm) ? 0 : -1);
}

/**
*  Set maximum target operating fan output RPM
*
* @param    pHwMgr:  the address of the powerplay hardware manager.
* @param    usMaxFanRpm:  max operating fan RPM value.
* @return   The response that came from the SMC.
*/
static int iceland_set_max_fan_rpm_output(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_pwm)
{
	hwmgr->thermal_controller.advanceFanControlParameters.usMaxFanRPM = us_max_fan_pwm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return (0 == smum_send_msg_to_smc_with_parameter(hwmgr->smumgr, PPSMC_MSG_SetFanRpmMax, us_max_fan_pwm) ? 0 : -1);
}

static int iceland_dpm_set_interrupt_state(void *private_data,
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

static int iceland_register_internal_thermal_interrupt(struct pp_hwmgr *hwmgr,
					const void *thermal_interrupt_info)
{
	int result;
	const struct pp_interrupt_registration_info *info =
			(const struct pp_interrupt_registration_info *)thermal_interrupt_info;

	if (info == NULL)
		return -EINVAL;

	result = cgs_add_irq_source(hwmgr->device, 230, AMD_THERMAL_IRQ_LAST,
				iceland_dpm_set_interrupt_state,
				info->call_back, info->context);

	if (result)
		return -EINVAL;

	result = cgs_add_irq_source(hwmgr->device, 231, AMD_THERMAL_IRQ_LAST,
				iceland_dpm_set_interrupt_state,
				info->call_back, info->context);

	if (result)
		return -EINVAL;

	return 0;
}


static bool iceland_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
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


static inline bool iceland_are_power_levels_equal(const struct iceland_performance_level *pl1,
							   const struct iceland_performance_level *pl2)
{
	return ((pl1->memory_clock == pl2->memory_clock) &&
		  (pl1->engine_clock == pl2->engine_clock) &&
		  (pl1->pcie_gen == pl2->pcie_gen) &&
		  (pl1->pcie_lane == pl2->pcie_lane));
}

int iceland_check_states_equal(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *pstate1,
		const struct pp_hw_power_state *pstate2, bool *equal)
{
	const struct iceland_power_state *psa = cast_const_phw_iceland_power_state(pstate1);
	const struct iceland_power_state *psb = cast_const_phw_iceland_power_state(pstate2);
	int i;

	if (equal == NULL || psa == NULL || psb == NULL)
		return -EINVAL;

	/* If the two states don't even have the same number of performance levels they cannot be the same state. */
	if (psa->performance_level_count != psb->performance_level_count) {
		*equal = false;
		return 0;
	}

	for (i = 0; i < psa->performance_level_count; i++) {
		if (!iceland_are_power_levels_equal(&(psa->performance_levels[i]), &(psb->performance_levels[i]))) {
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

static int iceland_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	if (mode) {
		/* stop auto-manage */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl))
			iceland_fan_ctrl_stop_smc_fan_control(hwmgr);
		iceland_fan_ctrl_set_static_mode(hwmgr, mode);
	} else
		/* restart auto-manage */
		iceland_fan_ctrl_reset_fan_speed_to_default(hwmgr);

	return 0;
}

static int iceland_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->fan_ctrl_is_in_default_mode)
		return hwmgr->fan_ctrl_default_mode;
	else
		return PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_FDO_CTRL2, FDO_PWM_MODE);
}

static int iceland_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

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

static int iceland_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_single_dpm_table *sclk_table = &(data->dpm_table.sclk_table);
	struct iceland_single_dpm_table *mclk_table = &(data->dpm_table.mclk_table);
	struct iceland_single_dpm_table *pcie_table = &(data->dpm_table.pcie_speed_table);
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
		pcie_speed = iceland_get_current_pcie_speed(hwmgr);
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

static int iceland_get_sclk_od(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_single_dpm_table *sclk_table = &(data->dpm_table.sclk_table);
	struct iceland_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.sclk_table);
	int value;

	value = (sclk_table->dpm_levels[sclk_table->count - 1].value -
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value) *
			100 /
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value;

	return value;
}

static int iceland_set_sclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_single_dpm_table *golden_sclk_table =
			&(data->golden_dpm_table.sclk_table);
	struct pp_power_state  *ps;
	struct iceland_power_state  *iceland_ps;

	if (value > 20)
		value = 20;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	iceland_ps = cast_phw_iceland_power_state(&ps->hardware);

	iceland_ps->performance_levels[iceland_ps->performance_level_count - 1].engine_clock =
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value *
			value / 100 +
			golden_sclk_table->dpm_levels[golden_sclk_table->count - 1].value;

	return 0;
}

static int iceland_get_mclk_od(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_single_dpm_table *mclk_table = &(data->dpm_table.mclk_table);
	struct iceland_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mclk_table);
	int value;

	value = (mclk_table->dpm_levels[mclk_table->count - 1].value -
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value) *
			100 /
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value;

	return value;
}

uint32_t iceland_get_xclk(struct pp_hwmgr *hwmgr)
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

	reference_clock = le16_to_cpu(fw_info->usReferenceClock);

	divide = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_CLKPIN_CNTL, XTALIN_DIVIDE);

	if (0 != divide)
		return reference_clock / 4;

	return reference_clock;
}

static int iceland_set_mclk_od(struct pp_hwmgr *hwmgr, uint32_t value)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_single_dpm_table *golden_mclk_table =
			&(data->golden_dpm_table.mclk_table);
	struct pp_power_state  *ps;
	struct iceland_power_state  *iceland_ps;

	if (value > 20)
		value = 20;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	iceland_ps = cast_phw_iceland_power_state(&ps->hardware);

	iceland_ps->performance_levels[iceland_ps->performance_level_count - 1].memory_clock =
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value *
			value / 100 +
			golden_mclk_table->dpm_levels[golden_mclk_table->count - 1].value;

	return 0;
}

static const struct pp_hwmgr_func iceland_hwmgr_funcs = {
	.backend_init = &iceland_hwmgr_backend_init,
	.backend_fini = &iceland_hwmgr_backend_fini,
	.asic_setup = &iceland_setup_asic_task,
	.dynamic_state_management_enable = &iceland_enable_dpm_tasks,
	.apply_state_adjust_rules = iceland_apply_state_adjust_rules,
	.force_dpm_level = &iceland_force_dpm_level,
	.power_state_set = iceland_set_power_state_tasks,
	.get_power_state_size = iceland_get_power_state_size,
	.get_mclk = iceland_dpm_get_mclk,
	.get_sclk = iceland_dpm_get_sclk,
	.patch_boot_state = iceland_dpm_patch_boot_state,
	.get_pp_table_entry = iceland_get_pp_table_entry,
	.get_num_of_pp_table_entries = iceland_get_num_of_entries,
	.print_current_perforce_level = iceland_print_current_perforce_level,
	.powerdown_uvd = iceland_phm_powerdown_uvd,
	.powergate_uvd = iceland_phm_powergate_uvd,
	.powergate_vce = iceland_phm_powergate_vce,
	.disable_clock_power_gating = iceland_phm_disable_clock_power_gating,
	.update_clock_gatings = iceland_phm_update_clock_gatings,
	.notify_smc_display_config_after_ps_adjustment = iceland_notify_smc_display_config_after_ps_adjustment,
	.display_config_changed = iceland_display_configuration_changed_task,
	.set_max_fan_pwm_output = iceland_set_max_fan_pwm_output,
	.set_max_fan_rpm_output = iceland_set_max_fan_rpm_output,
	.get_temperature = iceland_thermal_get_temperature,
	.stop_thermal_controller = iceland_thermal_stop_thermal_controller,
	.get_fan_speed_info = iceland_fan_ctrl_get_fan_speed_info,
	.get_fan_speed_percent = iceland_fan_ctrl_get_fan_speed_percent,
	.set_fan_speed_percent = iceland_fan_ctrl_set_fan_speed_percent,
	.reset_fan_speed_to_default = iceland_fan_ctrl_reset_fan_speed_to_default,
	.get_fan_speed_rpm = iceland_fan_ctrl_get_fan_speed_rpm,
	.set_fan_speed_rpm = iceland_fan_ctrl_set_fan_speed_rpm,
	.uninitialize_thermal_controller = iceland_thermal_ctrl_uninitialize_thermal_controller,
	.register_internal_thermal_interrupt = iceland_register_internal_thermal_interrupt,
	.check_smc_update_required_for_display_configuration = iceland_check_smc_update_required_for_display_configuration,
	.check_states_equal = iceland_check_states_equal,
	.set_fan_control_mode = iceland_set_fan_control_mode,
	.get_fan_control_mode = iceland_get_fan_control_mode,
	.force_clock_level = iceland_force_clock_level,
	.print_clock_levels = iceland_print_clock_levels,
	.get_sclk_od = iceland_get_sclk_od,
	.set_sclk_od = iceland_set_sclk_od,
	.get_mclk_od = iceland_get_mclk_od,
	.set_mclk_od = iceland_set_mclk_od,
};

int iceland_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	iceland_hwmgr  *data;

	data = kzalloc (sizeof(iceland_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	memset(data, 0x00, sizeof(iceland_hwmgr));

	hwmgr->backend = data;
	hwmgr->hwmgr_func = &iceland_hwmgr_funcs;
	hwmgr->pptable_func = &pptable_funcs;

	/* thermal */
	pp_iceland_thermal_initialize(hwmgr);
	return 0;
}
