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
#include <asm/div64.h>
#include "linux/delay.h"
#include "pp_acpi.h"
#include "hwmgr.h"
#include "polaris10_hwmgr.h"
#include "polaris10_powertune.h"
#include "polaris10_dyn_defaults.h"
#include "polaris10_smumgr.h"
#include "pp_debug.h"
#include "ppatomctrl.h"
#include "atombios.h"
#include "tonga_pptable.h"
#include "pppcielanes.h"
#include "amd_pcie_helpers.h"
#include "hardwaremanager.h"
#include "tonga_processpptables.h"
#include "cgs_common.h"
#include "smu74.h"
#include "smu_ucode_xfer_vi.h"
#include "smu74_discrete.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gca/gfx_8_0_d.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

#include "polaris10_thermal.h"
#include "polaris10_clockpowergating.h"

#define MC_CG_ARB_FREQ_F0           0x0a
#define MC_CG_ARB_FREQ_F1           0x0b
#define MC_CG_ARB_FREQ_F2           0x0c
#define MC_CG_ARB_FREQ_F3           0x0d

#define MC_CG_SEQ_DRAMCONF_S0       0x05
#define MC_CG_SEQ_DRAMCONF_S1       0x06
#define MC_CG_SEQ_YCLK_SUSPEND      0x04
#define MC_CG_SEQ_YCLK_RESUME       0x0a


#define SMC_RAM_END 0x40000

#define SMC_CG_IND_START            0xc0030000
#define SMC_CG_IND_END              0xc0040000

#define VOLTAGE_SCALE               4
#define VOLTAGE_VID_OFFSET_SCALE1   625
#define VOLTAGE_VID_OFFSET_SCALE2   100

#define VDDC_VDDCI_DELTA            200

#define MEM_FREQ_LOW_LATENCY        25000
#define MEM_FREQ_HIGH_LATENCY       80000

#define MEM_LATENCY_HIGH            45
#define MEM_LATENCY_LOW             35
#define MEM_LATENCY_ERR             0xFFFF

#define MC_SEQ_MISC0_GDDR5_SHIFT 28
#define MC_SEQ_MISC0_GDDR5_MASK  0xf0000000
#define MC_SEQ_MISC0_GDDR5_VALUE 5


#define PCIE_BUS_CLK                10000
#define TCLK                        (PCIE_BUS_CLK / 10)


static const uint16_t polaris10_clock_stretcher_lookup_table[2][4] =
{ {600, 1050, 3, 0}, {600, 1050, 6, 1} };

/*  [FF, SS] type, [] 4 voltage ranges, and [Floor Freq, Boundary Freq, VID min , VID max] */
static const uint32_t polaris10_clock_stretcher_ddt_table[2][4][4] =
{ { {265, 529, 120, 128}, {325, 650, 96, 119}, {430, 860, 32, 95}, {0, 0, 0, 31} },
  { {275, 550, 104, 112}, {319, 638, 96, 103}, {360, 720, 64, 95}, {384, 768, 32, 63} } };

/*  [Use_For_Low_freq] value, [0%, 5%, 10%, 7.14%, 14.28%, 20%] (coming from PWR_CKS_CNTL.stretch_amount reg spec) */
static const uint8_t polaris10_clock_stretch_amount_conversion[2][6] =
{ {0, 1, 3, 2, 4, 5}, {0, 2, 4, 5, 6, 5} };

/** Values for the CG_THERMAL_CTRL::DPM_EVENT_SRC field. */
enum DPM_EVENT_SRC {
	DPM_EVENT_SRC_ANALOG = 0,
	DPM_EVENT_SRC_EXTERNAL = 1,
	DPM_EVENT_SRC_DIGITAL = 2,
	DPM_EVENT_SRC_ANALOG_OR_EXTERNAL = 3,
	DPM_EVENT_SRC_DIGITAL_OR_EXTERNAL = 4
};

static const unsigned long PhwPolaris10_Magic = (unsigned long)(PHM_VIslands_Magic);

struct polaris10_power_state *cast_phw_polaris10_power_state(
				  struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwPolaris10_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL);

	return (struct polaris10_power_state *)hw_ps;
}

const struct polaris10_power_state *cast_const_phw_polaris10_power_state(
				 const struct pp_hw_power_state *hw_ps)
{
	PP_ASSERT_WITH_CODE((PhwPolaris10_Magic == hw_ps->magic),
				"Invalid Powerstate Type!",
				 return NULL);

	return (const struct polaris10_power_state *)hw_ps;
}

static bool polaris10_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	return (1 == PHM_READ_INDIRECT_FIELD(hwmgr->device,
			CGS_IND_REG__SMC, FEATURE_STATUS, VOLTAGE_CONTROLLER_ON))
			? true : false;
}

/**
 * Find the MC microcode version and store it in the HwMgr struct
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
int phm_get_mc_microcode_version (struct pp_hwmgr *hwmgr)
{
	cgs_write_register(hwmgr->device, mmMC_SEQ_IO_DEBUG_INDEX, 0x9F);

	hwmgr->microcode_version_info.MC = cgs_read_register(hwmgr->device, mmMC_SEQ_IO_DEBUG_DATA);

	return 0;
}

uint16_t phm_get_current_pcie_speed(struct pp_hwmgr *hwmgr)
{
	uint32_t speedCntl = 0;

	/* mmPCIE_PORT_INDEX rename as mmPCIE_INDEX */
	speedCntl = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__PCIE,
			ixPCIE_LC_SPEED_CNTL);
	return((uint16_t)PHM_GET_FIELD(speedCntl,
			PCIE_LC_SPEED_CNTL, LC_CURRENT_DATA_RATE));
}

int phm_get_current_pcie_lane_number(struct pp_hwmgr *hwmgr)
{
	uint32_t link_width;

	/* mmPCIE_PORT_INDEX rename as mmPCIE_INDEX */
	link_width = PHM_READ_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__PCIE,
			PCIE_LC_LINK_WIDTH_CNTL, LC_LINK_WIDTH_RD);

	PP_ASSERT_WITH_CODE((7 >= link_width),
			"Invalid PCIe lane width!", return 0);

	return decode_pcie_lane_width(link_width);
}

void phm_apply_dal_min_voltage_request(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)hwmgr->pptable;
	struct phm_clock_voltage_dependency_table *table =
				table_info->vddc_dep_on_dal_pwrl;
	struct phm_ppt_v1_clock_voltage_dependency_table *vddc_table;
	enum PP_DAL_POWERLEVEL dal_power_level = hwmgr->dal_power_level;
	uint32_t req_vddc = 0, req_volt, i;

	if (!table && !(dal_power_level >= PP_DAL_POWERLEVEL_ULTRALOW &&
			dal_power_level <= PP_DAL_POWERLEVEL_PERFORMANCE))
		return;

	for (i = 0; i < table->count; i++) {
		if (dal_power_level == table->entries[i].clk) {
			req_vddc = table->entries[i].v;
			break;
		}
	}

	vddc_table = table_info->vdd_dep_on_sclk;
	for (i = 0; i < vddc_table->count; i++) {
		if (req_vddc <= vddc_table->entries[i].vddc) {
			req_volt = (((uint32_t)vddc_table->entries[i].vddc) * VOLTAGE_SCALE)
					<< VDDC_SHIFT;
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_VddC_Request, req_volt);
			return;
		}
	}
	printk(KERN_ERR "DAL requested level can not"
			" found a available voltage in VDDC DPM Table \n");
}

/**
* Enable voltage control
*
* @param    pHwMgr  the address of the powerplay hardware manager.
* @return   always PP_Result_OK
*/
int polaris10_enable_smc_voltage_controller(struct pp_hwmgr *hwmgr)
{
	PP_ASSERT_WITH_CODE(
		(hwmgr->smumgr->smumgr_funcs->send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_Voltage_Cntl_Enable) == 0),
		"Failed to enable voltage DPM during DPM Start Function!",
		return 1;
	);

	return 0;
}

/**
* Checks if we want to support voltage control
*
* @param    hwmgr  the address of the powerplay hardware manager.
*/
static bool polaris10_voltage_control(const struct pp_hwmgr *hwmgr)
{
	const struct polaris10_hwmgr *data =
			(const struct polaris10_hwmgr *)(hwmgr->backend);

	return (POLARIS10_VOLTAGE_CONTROL_NONE != data->voltage_control);
}

/**
* Enable voltage control
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int polaris10_enable_voltage_control(struct pp_hwmgr *hwmgr)
{
	/* enable voltage control */
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			GENERAL_PWRMGT, VOLT_PWRMGT_EN, 1);

	return 0;
}

/**
* Create Voltage Tables.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int polaris10_construct_voltage_tables(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)hwmgr->pptable;
	int result;

	if (POLARIS10_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
				VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT,
				&(data->mvdd_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve MVDD table.",
				return result);
	} else if (POLARIS10_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control) {
		result = phm_get_svi2_mvdd_voltage_table(&(data->mvdd_voltage_table),
				table_info->vdd_dep_on_mclk);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve SVI2 MVDD table from dependancy table.",
				return result;);
	}

	if (POLARIS10_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		result = atomctrl_get_voltage_table_v3(hwmgr,
				VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT,
				&(data->vddci_voltage_table));
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve VDDCI table.",
				return result);
	} else if (POLARIS10_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control) {
		result = phm_get_svi2_vddci_voltage_table(&(data->vddci_voltage_table),
				table_info->vdd_dep_on_mclk);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve SVI2 VDDCI table from dependancy table.",
				return result);
	}

	if (POLARIS10_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		result = phm_get_svi2_vdd_voltage_table(&(data->vddc_voltage_table),
				table_info->vddc_lookup_table);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to retrieve SVI2 VDDC table from lookup table.",
				return result);
	}

	PP_ASSERT_WITH_CODE(
			(data->vddc_voltage_table.count <= (SMU74_MAX_LEVELS_VDDC)),
			"Too many voltage values for VDDC. Trimming to fit state table.",
			phm_trim_voltage_table_to_fit_state_table(SMU74_MAX_LEVELS_VDDC,
								&(data->vddc_voltage_table)));

	PP_ASSERT_WITH_CODE(
			(data->vddci_voltage_table.count <= (SMU74_MAX_LEVELS_VDDCI)),
			"Too many voltage values for VDDCI. Trimming to fit state table.",
			phm_trim_voltage_table_to_fit_state_table(SMU74_MAX_LEVELS_VDDCI,
					&(data->vddci_voltage_table)));

	PP_ASSERT_WITH_CODE(
			(data->mvdd_voltage_table.count <= (SMU74_MAX_LEVELS_MVDD)),
			"Too many voltage values for MVDD. Trimming to fit state table.",
			phm_trim_voltage_table_to_fit_state_table(SMU74_MAX_LEVELS_MVDD,
							   &(data->mvdd_voltage_table)));

	return 0;
}

/**
* Programs static screed detection parameters
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int polaris10_program_static_screen_threshold_parameters(
							struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

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
static int polaris10_enable_display_gap(struct pp_hwmgr *hwmgr)
{
	uint32_t display_gap =
			cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					ixCG_DISPLAY_GAP_CNTL);

	display_gap = PHM_SET_FIELD(display_gap, CG_DISPLAY_GAP_CNTL,
			DISP_GAP, DISPLAY_GAP_IGNORE);

	display_gap = PHM_SET_FIELD(display_gap, CG_DISPLAY_GAP_CNTL,
			DISP_GAP_MCHG, DISPLAY_GAP_VBLANK);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_DISPLAY_GAP_CNTL, display_gap);

	return 0;
}

/**
* Programs activity state transition voting clients
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always  0
*/
static int polaris10_program_voting_clients(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

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
static int polaris10_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(hwmgr->smumgr->backend);
	uint32_t tmp;
	int result;
	bool error = false;

	result = polaris10_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, DpmTable),
			&tmp, data->sram_end);

	if (0 == result)
		data->dpm_table_start = tmp;

	error |= (0 != result);

	result = polaris10_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, SoftRegisters),
			&tmp, data->sram_end);

	if (!result) {
		data->soft_regs_start = tmp;
		smu_data->soft_regs_start = tmp;
	}

	error |= (0 != result);

	result = polaris10_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, mcRegisterTable),
			&tmp, data->sram_end);

	if (!result)
		data->mc_reg_table_start = tmp;

	result = polaris10_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, FanTable),
			&tmp, data->sram_end);

	if (!result)
		data->fan_table_start = tmp;

	error |= (0 != result);

	result = polaris10_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, mcArbDramTimingTable),
			&tmp, data->sram_end);

	if (!result)
		data->arb_table_start = tmp;

	error |= (0 != result);

	result = polaris10_read_smc_sram_dword(hwmgr->smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU74_Firmware_Header, Version),
			&tmp, data->sram_end);

	if (!result)
		hwmgr->microcode_version_info.SMC = tmp;

	error |= (0 != result);

	return error ? -1 : 0;
}

/* Copy one arb setting to another and then switch the active set.
 * arb_src and arb_dest is one of the MC_CG_ARB_FREQ_Fx constants.
 */
static int polaris10_copy_and_switch_arb_sets(struct pp_hwmgr *hwmgr,
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
static int polaris10_initial_switch_from_arbf0_to_f1(struct pp_hwmgr *hwmgr)
{
	return polaris10_copy_and_switch_arb_sets(hwmgr,
			MC_CG_ARB_FREQ_F0, MC_CG_ARB_FREQ_F1);
}

static int polaris10_setup_default_pcie_table(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
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

	phm_reset_single_dpm_table(&data->dpm_table.pcie_speed_table,
					SMU74_MAX_LEVELS_LINK,
					MAX_REGULAR_DPM_NUMBER);

	if (pcie_table != NULL) {
		/* max_entry is used to make sure we reserve one PCIE level
		 * for boot level (fix for A+A PSPP issue).
		 * If PCIE table from PPTable have ULV entry + 8 entries,
		 * then ignore the last entry.*/
		max_entry = (SMU74_MAX_LEVELS_LINK < pcie_table->count) ?
				SMU74_MAX_LEVELS_LINK : pcie_table->count;
		for (i = 1; i < max_entry; i++) {
			phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, i - 1,
					get_pcie_gen_support(data->pcie_gen_cap,
							pcie_table->entries[i].gen_speed),
					get_pcie_lane_support(data->pcie_lane_cap,
							pcie_table->entries[i].lane_width));
		}
		data->dpm_table.pcie_speed_table.count = max_entry - 1;

		/* Setup BIF_SCLK levels */
		for (i = 0; i < max_entry; i++)
			data->bif_sclk_table[i] = pcie_table->entries[i].pcie_sclk;
	} else {
		/* Hardcode Pcie Table */
		phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 0,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Min_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 1,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Min_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 2,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 3,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 4,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));
		phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table, 5,
				get_pcie_gen_support(data->pcie_gen_cap,
						PP_Max_PCIEGen),
				get_pcie_lane_support(data->pcie_lane_cap,
						PP_Max_PCIELane));

		data->dpm_table.pcie_speed_table.count = 6;
	}
	/* Populate last level for boot PCIE level, but do not increment count. */
	phm_setup_pcie_table_entry(&data->dpm_table.pcie_speed_table,
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
int polaris10_setup_default_dpm_tables(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
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
			"SCLK dependency table has to have is missing."
			"This table is mandatory",
			return -EINVAL);

	PP_ASSERT_WITH_CODE(dep_mclk_table != NULL,
			"MCLK dependency table is missing. This table is mandatory",
			return -EINVAL);
	PP_ASSERT_WITH_CODE(dep_mclk_table->count >= 1,
			"MCLK dependency table has to have is missing."
			"This table is mandatory",
			return -EINVAL);

	/* clear the state table to reset everything to default */
	phm_reset_single_dpm_table(
			&data->dpm_table.sclk_table, SMU74_MAX_LEVELS_GRAPHICS, MAX_REGULAR_DPM_NUMBER);
	phm_reset_single_dpm_table(
			&data->dpm_table.mclk_table, SMU74_MAX_LEVELS_MEMORY, MAX_REGULAR_DPM_NUMBER);


	/* Initialize Sclk DPM table based on allow Sclk values */
	data->dpm_table.sclk_table.count = 0;
	for (i = 0; i < dep_sclk_table->count; i++) {
		if (i == 0 || data->dpm_table.sclk_table.dpm_levels[data->dpm_table.sclk_table.count - 1].value !=
						dep_sclk_table->entries[i].clk) {

			data->dpm_table.sclk_table.dpm_levels[data->dpm_table.sclk_table.count].value =
					dep_sclk_table->entries[i].clk;

			data->dpm_table.sclk_table.dpm_levels[data->dpm_table.sclk_table.count].enabled =
					(i == 0) ? true : false;
			data->dpm_table.sclk_table.count++;
		}
	}

	/* Initialize Mclk DPM table based on allow Mclk values */
	data->dpm_table.mclk_table.count = 0;
	for (i = 0; i < dep_mclk_table->count; i++) {
		if (i == 0 || data->dpm_table.mclk_table.dpm_levels
				[data->dpm_table.mclk_table.count - 1].value !=
						dep_mclk_table->entries[i].clk) {
			data->dpm_table.mclk_table.dpm_levels[data->dpm_table.mclk_table.count].value =
							dep_mclk_table->entries[i].clk;
			data->dpm_table.mclk_table.dpm_levels[data->dpm_table.mclk_table.count].enabled =
							(i == 0) ? true : false;
			data->dpm_table.mclk_table.count++;
		}
	}

	/* setup PCIE gen speed levels */
	polaris10_setup_default_pcie_table(hwmgr);

	/* save a copy of the default DPM table */
	memcpy(&(data->golden_dpm_table), &(data->dpm_table),
			sizeof(struct polaris10_dpm_table));

	return 0;
}

uint8_t convert_to_vid(uint16_t vddc)
{
	return (uint8_t) ((6200 - (vddc * VOLTAGE_SCALE)) / 25);
}

/**
 * Mvdd table preparation for SMC.
 *
 * @param    *hwmgr The address of the hardware manager.
 * @param    *table The SMC DPM table structure to be populated.
 * @return   0
 */
static int polaris10_populate_smc_mvdd_table(struct pp_hwmgr *hwmgr,
			SMU74_Discrete_DpmTable *table)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t count, level;

	if (POLARIS10_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
		count = data->mvdd_voltage_table.count;
		if (count > SMU_MAX_SMIO_LEVELS)
			count = SMU_MAX_SMIO_LEVELS;
		for (level = 0; level < count; level++) {
			table->SmioTable2.Pattern[level].Voltage =
				PP_HOST_TO_SMC_US(data->mvdd_voltage_table.entries[count].value * VOLTAGE_SCALE);
			/* Index into DpmTable.Smio. Drive bits from Smio entry to get this voltage level.*/
			table->SmioTable2.Pattern[level].Smio =
				(uint8_t) level;
			table->Smio[level] |=
				data->mvdd_voltage_table.entries[level].smio_low;
		}
		table->SmioMask2 = data->vddci_voltage_table.mask_low;

		table->MvddLevelCount = (uint32_t) PP_HOST_TO_SMC_UL(count);
	}

	return 0;
}

static int polaris10_populate_smc_vddci_table(struct pp_hwmgr *hwmgr,
					struct SMU74_Discrete_DpmTable *table)
{
	uint32_t count, level;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	count = data->vddci_voltage_table.count;

	if (POLARIS10_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		if (count > SMU_MAX_SMIO_LEVELS)
			count = SMU_MAX_SMIO_LEVELS;
		for (level = 0; level < count; ++level) {
			table->SmioTable1.Pattern[level].Voltage =
				PP_HOST_TO_SMC_US(data->vddci_voltage_table.entries[level].value * VOLTAGE_SCALE);
			table->SmioTable1.Pattern[level].Smio = (uint8_t) level;

			table->Smio[level] |= data->vddci_voltage_table.entries[level].smio_low;
		}
	}

	table->SmioMask1 = data->vddci_voltage_table.mask_low;

	return 0;
}

/**
* Preparation of vddc and vddgfx CAC tables for SMC.
*
* @param    hwmgr  the address of the hardware manager
* @param    table  the SMC DPM table structure to be populated
* @return   always 0
*/
static int polaris10_populate_cac_table(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	uint32_t count;
	uint8_t index;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_voltage_lookup_table *lookup_table =
			table_info->vddc_lookup_table;
	/* tables is already swapped, so in order to use the value from it,
	 * we need to swap it back.
	 * We are populating vddc CAC data to BapmVddc table
	 * in split and merged mode
	 */
	for (count = 0; count < lookup_table->count; count++) {
		index = phm_get_voltage_index(lookup_table,
				data->vddc_voltage_table.entries[count].value);
		table->BapmVddcVidLoSidd[count] = convert_to_vid(lookup_table->entries[index].us_cac_low);
		table->BapmVddcVidHiSidd[count] = convert_to_vid(lookup_table->entries[index].us_cac_mid);
		table->BapmVddcVidHiSidd2[count] = convert_to_vid(lookup_table->entries[index].us_cac_high);
	}

	return 0;
}

/**
* Preparation of voltage tables for SMC.
*
* @param    hwmgr   the address of the hardware manager
* @param    table   the SMC DPM table structure to be populated
* @return   always  0
*/

int polaris10_populate_smc_voltage_tables(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	polaris10_populate_smc_vddci_table(hwmgr, table);
	polaris10_populate_smc_mvdd_table(hwmgr, table);
	polaris10_populate_cac_table(hwmgr, table);

	return 0;
}

static int polaris10_populate_ulv_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_Ulv *state)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	state->CcPwrDynRm = 0;
	state->CcPwrDynRm1 = 0;

	state->VddcOffset = (uint16_t) table_info->us_ulv_voltage_offset;
	state->VddcOffsetVid = (uint8_t)(table_info->us_ulv_voltage_offset *
			VOLTAGE_VID_OFFSET_SCALE2 / VOLTAGE_VID_OFFSET_SCALE1);

	state->VddcPhase = (data->vddc_phase_shed_control) ? 0 : 1;

	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm1);
	CONVERT_FROM_HOST_TO_SMC_US(state->VddcOffset);

	return 0;
}

static int polaris10_populate_ulv_state(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	return polaris10_populate_ulv_level(hwmgr, &table->Ulv);
}

static int polaris10_populate_smc_link_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_dpm_table *dpm_table = &data->dpm_table;
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
			phm_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);

	return 0;
}

static uint32_t polaris10_get_xclk(struct pp_hwmgr *hwmgr)
{
	uint32_t reference_clock, tmp;
	struct cgs_display_info info = {0};
	struct cgs_mode_info mode_info;

	info.mode_info = &mode_info;

	tmp = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_CLKPIN_CNTL_2, MUX_TCLK_TO_XCLK);

	if (tmp)
		return TCLK;

	cgs_get_active_displays_info(hwmgr->device, &info);
	reference_clock = mode_info.ref_clock;

	tmp = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_CLKPIN_CNTL, XTALIN_DIVIDE);

	if (0 != tmp)
		return reference_clock / 4;

	return reference_clock;
}

/**
* Calculates the SCLK dividers using the provided engine clock
*
* @param    hwmgr  the address of the hardware manager
* @param    clock  the engine clock to use to populate the structure
* @param    sclk   the SMC SCLK structure to be populated
*/
static int polaris10_calculate_sclk_params(struct pp_hwmgr *hwmgr,
		uint32_t clock, SMU_SclkSetting *sclk_setting)
{
	const struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const SMU74_Discrete_DpmTable *table = &(data->smc_state_table);
	struct pp_atomctrl_clock_dividers_ai dividers;

	uint32_t ref_clock;
	uint32_t pcc_target_percent, pcc_target_freq, ss_target_percent, ss_target_freq;
	uint8_t i;
	int result;
	uint64_t temp;

	sclk_setting->SclkFrequency = clock;
	/* get the engine clock dividers for this clock value */
	result = atomctrl_get_engine_pll_dividers_ai(hwmgr, clock,  &dividers);
	if (result == 0) {
		sclk_setting->Fcw_int = dividers.usSclk_fcw_int;
		sclk_setting->Fcw_frac = dividers.usSclk_fcw_frac;
		sclk_setting->Pcc_fcw_int = dividers.usPcc_fcw_int;
		sclk_setting->PllRange = dividers.ucSclkPllRange;
		sclk_setting->Sclk_slew_rate = 0x400;
		sclk_setting->Pcc_up_slew_rate = dividers.usPcc_fcw_slew_frac;
		sclk_setting->Pcc_down_slew_rate = 0xffff;
		sclk_setting->SSc_En = dividers.ucSscEnable;
		sclk_setting->Fcw1_int = dividers.usSsc_fcw1_int;
		sclk_setting->Fcw1_frac = dividers.usSsc_fcw1_frac;
		sclk_setting->Sclk_ss_slew_rate = dividers.usSsc_fcw_slew_frac;
		return result;
	}

	ref_clock = polaris10_get_xclk(hwmgr);

	for (i = 0; i < NUM_SCLK_RANGE; i++) {
		if (clock > data->range_table[i].trans_lower_frequency
		&& clock <= data->range_table[i].trans_upper_frequency) {
			sclk_setting->PllRange = i;
			break;
		}
	}

	sclk_setting->Fcw_int = (uint16_t)((clock << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv) / ref_clock);
	temp = clock << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv;
	temp <<= 0x10;
	do_div(temp, ref_clock);
	sclk_setting->Fcw_frac = temp & 0xffff;

	pcc_target_percent = 10; /*  Hardcode 10% for now. */
	pcc_target_freq = clock - (clock * pcc_target_percent / 100);
	sclk_setting->Pcc_fcw_int = (uint16_t)((pcc_target_freq << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv) / ref_clock);

	ss_target_percent = 2; /*  Hardcode 2% for now. */
	sclk_setting->SSc_En = 0;
	if (ss_target_percent) {
		sclk_setting->SSc_En = 1;
		ss_target_freq = clock - (clock * ss_target_percent / 100);
		sclk_setting->Fcw1_int = (uint16_t)((ss_target_freq << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv) / ref_clock);
		temp = ss_target_freq << table->SclkFcwRangeTable[sclk_setting->PllRange].postdiv;
		temp <<= 0x10;
		do_div(temp, ref_clock);
		sclk_setting->Fcw1_frac = temp & 0xffff;
	}

	return 0;
}

static int polaris10_get_dependency_volt_by_clk(struct pp_hwmgr *hwmgr,
		struct phm_ppt_v1_clock_voltage_dependency_table *dep_table,
		uint32_t clock, SMU_VoltageLevel *voltage, uint32_t *mvdd)
{
	uint32_t i;
	uint16_t vddci;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	*voltage = *mvdd = 0;

	/* clock - voltage dependency table is empty table */
	if (dep_table->count == 0)
		return -EINVAL;

	for (i = 0; i < dep_table->count; i++) {
		/* find first sclk bigger than request */
		if (dep_table->entries[i].clk >= clock) {
			*voltage |= (dep_table->entries[i].vddc *
					VOLTAGE_SCALE) << VDDC_SHIFT;
			if (POLARIS10_VOLTAGE_CONTROL_NONE == data->vddci_control)
				*voltage |= (data->vbios_boot_state.vddci_bootup_value *
						VOLTAGE_SCALE) << VDDCI_SHIFT;
			else if (dep_table->entries[i].vddci)
				*voltage |= (dep_table->entries[i].vddci *
						VOLTAGE_SCALE) << VDDCI_SHIFT;
			else {
				vddci = phm_find_closest_vddci(&(data->vddci_voltage_table),
						(dep_table->entries[i].vddc -
								(uint16_t)data->vddc_vddci_delta));
				*voltage |= (vddci * VOLTAGE_SCALE) <<	VDDCI_SHIFT;
			}

			if (POLARIS10_VOLTAGE_CONTROL_NONE == data->mvdd_control)
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

	if (POLARIS10_VOLTAGE_CONTROL_NONE == data->vddci_control)
		*voltage |= (data->vbios_boot_state.vddci_bootup_value *
				VOLTAGE_SCALE) << VDDCI_SHIFT;
	else if (dep_table->entries[i-1].vddci) {
		vddci = phm_find_closest_vddci(&(data->vddci_voltage_table),
				(dep_table->entries[i].vddc -
						(uint16_t)data->vddc_vddci_delta));
		*voltage |= (vddci * VOLTAGE_SCALE) << VDDCI_SHIFT;
	}

	if (POLARIS10_VOLTAGE_CONTROL_NONE == data->mvdd_control)
		*mvdd = data->vbios_boot_state.mvdd_bootup_value * VOLTAGE_SCALE;
	else if (dep_table->entries[i].mvdd)
		*mvdd = (uint32_t) dep_table->entries[i - 1].mvdd * VOLTAGE_SCALE;

	return 0;
}

static const sclkFcwRange_t Range_Table[NUM_SCLK_RANGE] =
{ {VCO_2_4, POSTDIV_DIV_BY_16,  75, 160, 112},
  {VCO_3_6, POSTDIV_DIV_BY_16, 112, 224, 160},
  {VCO_2_4, POSTDIV_DIV_BY_8,   75, 160, 112},
  {VCO_3_6, POSTDIV_DIV_BY_8,  112, 224, 160},
  {VCO_2_4, POSTDIV_DIV_BY_4,   75, 160, 112},
  {VCO_3_6, POSTDIV_DIV_BY_4,  112, 216, 160},
  {VCO_2_4, POSTDIV_DIV_BY_2,   75, 160, 108},
  {VCO_3_6, POSTDIV_DIV_BY_2,  112, 216, 160} };

static void polaris10_get_sclk_range_table(struct pp_hwmgr *hwmgr)
{
	uint32_t i, ref_clk;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	SMU74_Discrete_DpmTable  *table = &(data->smc_state_table);
	struct pp_atom_ctrl_sclk_range_table range_table_from_vbios = { { {0} } };

	ref_clk = polaris10_get_xclk(hwmgr);

	if (0 == atomctrl_get_smc_sclk_range_table(hwmgr, &range_table_from_vbios)) {
		for (i = 0; i < NUM_SCLK_RANGE; i++) {
			table->SclkFcwRangeTable[i].vco_setting = range_table_from_vbios.entry[i].ucVco_setting;
			table->SclkFcwRangeTable[i].postdiv = range_table_from_vbios.entry[i].ucPostdiv;
			table->SclkFcwRangeTable[i].fcw_pcc = range_table_from_vbios.entry[i].usFcw_pcc;

			table->SclkFcwRangeTable[i].fcw_trans_upper = range_table_from_vbios.entry[i].usFcw_trans_upper;
			table->SclkFcwRangeTable[i].fcw_trans_lower = range_table_from_vbios.entry[i].usRcw_trans_lower;

			CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_pcc);
			CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_upper);
			CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_lower);
		}
		return;
	}

	for (i = 0; i < NUM_SCLK_RANGE; i++) {

		data->range_table[i].trans_lower_frequency = (ref_clk * Range_Table[i].fcw_trans_lower) >> Range_Table[i].postdiv;
		data->range_table[i].trans_upper_frequency = (ref_clk * Range_Table[i].fcw_trans_upper) >> Range_Table[i].postdiv;

		table->SclkFcwRangeTable[i].vco_setting = Range_Table[i].vco_setting;
		table->SclkFcwRangeTable[i].postdiv = Range_Table[i].postdiv;
		table->SclkFcwRangeTable[i].fcw_pcc = Range_Table[i].fcw_pcc;

		table->SclkFcwRangeTable[i].fcw_trans_upper = Range_Table[i].fcw_trans_upper;
		table->SclkFcwRangeTable[i].fcw_trans_lower = Range_Table[i].fcw_trans_lower;

		CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_pcc);
		CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_upper);
		CONVERT_FROM_HOST_TO_SMC_US(table->SclkFcwRangeTable[i].fcw_trans_lower);
	}
}

/**
* Populates single SMC SCLK structure using the provided engine clock
*
* @param    hwmgr      the address of the hardware manager
* @param    clock the engine clock to use to populate the structure
* @param    sclk        the SMC SCLK structure to be populated
*/

static int polaris10_populate_single_graphic_level(struct pp_hwmgr *hwmgr,
		uint32_t clock, uint16_t sclk_al_threshold,
		struct SMU74_Discrete_GraphicsLevel *level)
{
	int result, i, temp;
	/* PP_Clocks minClocks; */
	uint32_t mvdd;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	SMU_SclkSetting curr_sclk_setting = { 0 };

	result = polaris10_calculate_sclk_params(hwmgr, clock, &curr_sclk_setting);

	/* populate graphics levels */
	result = polaris10_get_dependency_volt_by_clk(hwmgr,
			table_info->vdd_dep_on_sclk, clock,
			&level->MinVoltage, &mvdd);

	PP_ASSERT_WITH_CODE((0 == result),
			"can not find VDDC voltage value for "
			"VDDC engine clock dependency table",
			return result);
	level->ActivityLevel = sclk_al_threshold;

	level->CcPwrDynRm = 0;
	level->CcPwrDynRm1 = 0;
	level->EnabledForActivity = 0;
	level->EnabledForThrottle = 1;
	level->UpHyst = 10;
	level->DownHyst = 0;
	level->VoltageDownHyst = 0;
	level->PowerThrottle = 0;

	/*
	* TODO: get minimum clocks from dal configaration
	* PECI_GetMinClockSettings(hwmgr->pPECI, &minClocks);
	*/
	/* data->DisplayTiming.minClockInSR = minClocks.engineClockInSR; */

	/* get level->DeepSleepDivId
	if (phm_cap_enabled(hwmgr->platformDescriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep))
		level->DeepSleepDivId = PhwFiji_GetSleepDividerIdFromClock(hwmgr, clock, minClocks.engineClockInSR);
	*/
	PP_ASSERT_WITH_CODE((clock >= POLARIS10_MINIMUM_ENGINE_CLOCK), "Engine clock can't satisfy stutter requirement!", return 0);
	for (i = POLARIS10_MAX_DEEPSLEEP_DIVIDER_ID;  ; i--) {
		temp = clock >> i;

		if (temp >= POLARIS10_MINIMUM_ENGINE_CLOCK || i == 0)
			break;
	}

	level->DeepSleepDivId = i;

	/* Default to slow, highest DPM level will be
	 * set to PPSMC_DISPLAY_WATERMARK_LOW later.
	 */
	if (data->update_up_hyst)
		level->UpHyst = (uint8_t)data->up_hyst;
	if (data->update_down_hyst)
		level->DownHyst = (uint8_t)data->down_hyst;

	level->SclkSetting = curr_sclk_setting;

	CONVERT_FROM_HOST_TO_SMC_UL(level->MinVoltage);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(level->CcPwrDynRm1);
	CONVERT_FROM_HOST_TO_SMC_US(level->ActivityLevel);
	CONVERT_FROM_HOST_TO_SMC_UL(level->SclkSetting.SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw_frac);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Pcc_fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Sclk_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Pcc_up_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Pcc_down_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw1_int);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Fcw1_frac);
	CONVERT_FROM_HOST_TO_SMC_US(level->SclkSetting.Sclk_ss_slew_rate);
	return 0;
}

/**
* Populates all SMC SCLK levels' structure based on the trimmed allowed dpm engine clock states
*
* @param    hwmgr      the address of the hardware manager
*/
static int polaris10_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_dpm_table *dpm_table = &data->dpm_table;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_pcie_table *pcie_table = table_info->pcie_table;
	uint8_t pcie_entry_cnt = (uint8_t) data->dpm_table.pcie_speed_table.count;
	int result = 0;
	uint32_t array = data->dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, GraphicsLevel);
	uint32_t array_size = sizeof(struct SMU74_Discrete_GraphicsLevel) *
			SMU74_MAX_LEVELS_GRAPHICS;
	struct SMU74_Discrete_GraphicsLevel *levels =
			data->smc_state_table.GraphicsLevel;
	uint32_t i, max_entry;
	uint8_t hightest_pcie_level_enabled = 0,
		lowest_pcie_level_enabled = 0,
		mid_pcie_level_enabled = 0,
		count = 0;

	polaris10_get_sclk_range_table(hwmgr);

	for (i = 0; i < dpm_table->sclk_table.count; i++) {

		result = polaris10_populate_single_graphic_level(hwmgr,
				dpm_table->sclk_table.dpm_levels[i].value,
				(uint16_t)data->activity_target[i],
				&(data->smc_state_table.GraphicsLevel[i]));
		if (result)
			return result;

		/* Making sure only DPM level 0-1 have Deep Sleep Div ID populated. */
		if (i > 1)
			levels[i].DeepSleepDivId = 0;
	}
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SPLLShutdownSupport))
		data->smc_state_table.GraphicsLevel[0].SclkSetting.SSc_En = 0;

	data->smc_state_table.GraphicsLevel[0].EnabledForActivity = 1;
	data->smc_state_table.GraphicsDpmLevelCount =
			(uint8_t)dpm_table->sclk_table.count;
	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
			phm_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);


	if (pcie_table != NULL) {
		PP_ASSERT_WITH_CODE((1 <= pcie_entry_cnt),
				"There must be 1 or more PCIE levels defined in PPTable.",
				return -EINVAL);
		max_entry = pcie_entry_cnt - 1;
		for (i = 0; i < dpm_table->sclk_table.count; i++)
			levels[i].pcieDpmLevel =
					(uint8_t) ((i < max_entry) ? i : max_entry);
	} else {
		while (data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << (hightest_pcie_level_enabled + 1))) != 0))
			hightest_pcie_level_enabled++;

		while (data->dpm_level_enable_mask.pcie_dpm_enable_mask &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << lowest_pcie_level_enabled)) == 0))
			lowest_pcie_level_enabled++;

		while ((count < hightest_pcie_level_enabled) &&
				((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
						(1 << (lowest_pcie_level_enabled + 1 + count))) == 0))
			count++;

		mid_pcie_level_enabled = (lowest_pcie_level_enabled + 1 + count) <
				hightest_pcie_level_enabled ?
						(lowest_pcie_level_enabled + 1 + count) :
						hightest_pcie_level_enabled;

		/* set pcieDpmLevel to hightest_pcie_level_enabled */
		for (i = 2; i < dpm_table->sclk_table.count; i++)
			levels[i].pcieDpmLevel = hightest_pcie_level_enabled;

		/* set pcieDpmLevel to lowest_pcie_level_enabled */
		levels[0].pcieDpmLevel = lowest_pcie_level_enabled;

		/* set pcieDpmLevel to mid_pcie_level_enabled */
		levels[1].pcieDpmLevel = mid_pcie_level_enabled;
	}
	/* level count will send to smc once at init smc table and never change */
	result = polaris10_copy_bytes_to_smc(hwmgr->smumgr, array, (uint8_t *)levels,
			(uint32_t)array_size, data->sram_end);

	return result;
}

static int polaris10_populate_single_memory_level(struct pp_hwmgr *hwmgr,
		uint32_t clock, struct SMU74_Discrete_MemoryLevel *mem_level)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	int result = 0;
	struct cgs_display_info info = {0, 0, NULL};

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (table_info->vdd_dep_on_mclk) {
		result = polaris10_get_dependency_volt_by_clk(hwmgr,
				table_info->vdd_dep_on_mclk, clock,
				&mem_level->MinVoltage, &mem_level->MinMvdd);
		PP_ASSERT_WITH_CODE((0 == result),
				"can not find MinVddc voltage value from memory "
				"VDDC voltage dependency table", return result);
	}

	mem_level->MclkFrequency = clock;
	mem_level->StutterEnable = 0;
	mem_level->EnabledForThrottle = 1;
	mem_level->EnabledForActivity = 0;
	mem_level->UpHyst = 0;
	mem_level->DownHyst = 100;
	mem_level->VoltageDownHyst = 0;
	mem_level->ActivityLevel = (uint16_t)data->mclk_activity_target;
	mem_level->StutterEnable = false;

	mem_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	data->display_timing.num_existing_displays = info.display_count;

	if ((data->mclk_stutter_mode_threshold) &&
		(clock <= data->mclk_stutter_mode_threshold) &&
		(PHM_READ_FIELD(hwmgr->device, DPG_PIPE_STUTTER_CONTROL,
				STUTTER_ENABLE) & 0x1))
		mem_level->StutterEnable = true;

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
static int polaris10_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_dpm_table *dpm_table = &data->dpm_table;
	int result;
	/* populate MCLK dpm table to SMU7 */
	uint32_t array = data->dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, MemoryLevel);
	uint32_t array_size = sizeof(SMU74_Discrete_MemoryLevel) *
			SMU74_MAX_LEVELS_MEMORY;
	struct SMU74_Discrete_MemoryLevel *levels =
			data->smc_state_table.MemoryLevel;
	uint32_t i;

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		PP_ASSERT_WITH_CODE((0 != dpm_table->mclk_table.dpm_levels[i].value),
				"can not populate memory level as memory clock is zero",
				return -EINVAL);
		result = polaris10_populate_single_memory_level(hwmgr,
				dpm_table->mclk_table.dpm_levels[i].value,
				&levels[i]);
		if (i == dpm_table->mclk_table.count - 1) {
			levels[i].DisplayWatermark = PPSMC_DISPLAY_WATERMARK_HIGH;
			levels[i].EnabledForActivity = 1;
		}
		if (result)
			return result;
	}

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
			phm_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);

	/* level count will send to smc once at init smc table and never change */
	result = polaris10_copy_bytes_to_smc(hwmgr->smumgr, array, (uint8_t *)levels,
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
int polaris10_populate_mvdd_value(struct pp_hwmgr *hwmgr,
		uint32_t mclk, SMIO_Pattern *smio_pat)
{
	const struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t i = 0;

	if (POLARIS10_VOLTAGE_CONTROL_NONE != data->mvdd_control) {
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

static int polaris10_populate_smc_acpi_level(struct pp_hwmgr *hwmgr,
		SMU74_Discrete_DpmTable *table)
{
	int result = 0;
	uint32_t sclk_frequency;
	const struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	SMIO_Pattern vol_level;
	uint32_t mvdd;
	uint16_t us_mvdd;

	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (!data->sclk_dpm_key_disabled) {
		/* Get MinVoltage and Frequency from DPM0,
		 * already converted to SMC_UL */
		sclk_frequency = data->dpm_table.sclk_table.dpm_levels[0].value;
		result = polaris10_get_dependency_volt_by_clk(hwmgr,
				table_info->vdd_dep_on_sclk,
				table->ACPILevel.SclkFrequency,
				&table->ACPILevel.MinVoltage, &mvdd);
		PP_ASSERT_WITH_CODE((0 == result),
				"Cannot find ACPI VDDC voltage value "
				"in Clock Dependency Table", );
	} else {
		sclk_frequency = data->vbios_boot_state.sclk_bootup_value;
		table->ACPILevel.MinVoltage =
				data->vbios_boot_state.vddc_bootup_value * VOLTAGE_SCALE;
	}

	result = polaris10_calculate_sclk_params(hwmgr, sclk_frequency,  &(table->ACPILevel.SclkSetting));
	PP_ASSERT_WITH_CODE(result == 0, "Error retrieving Engine Clock dividers from VBIOS.", return result);

	table->ACPILevel.DeepSleepDivId = 0;
	table->ACPILevel.CcPwrDynRm = 0;
	table->ACPILevel.CcPwrDynRm1 = 0;

	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.Flags);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.MinVoltage);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm1);

	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SclkSetting.SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw_frac);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Pcc_fcw_int);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Sclk_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Pcc_up_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Pcc_down_slew_rate);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw1_int);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Fcw1_frac);
	CONVERT_FROM_HOST_TO_SMC_US(table->ACPILevel.SclkSetting.Sclk_ss_slew_rate);

	if (!data->mclk_dpm_key_disabled) {
		/* Get MinVoltage and Frequency from DPM0, already converted to SMC_UL */
		table->MemoryACPILevel.MclkFrequency =
				data->dpm_table.mclk_table.dpm_levels[0].value;
		result = polaris10_get_dependency_volt_by_clk(hwmgr,
				table_info->vdd_dep_on_mclk,
				table->MemoryACPILevel.MclkFrequency,
				&table->MemoryACPILevel.MinVoltage, &mvdd);
		PP_ASSERT_WITH_CODE((0 == result),
				"Cannot find ACPI VDDCI voltage value "
				"in Clock Dependency Table",
				);
	} else {
		table->MemoryACPILevel.MclkFrequency =
				data->vbios_boot_state.mclk_bootup_value;
		table->MemoryACPILevel.MinVoltage =
				data->vbios_boot_state.vddci_bootup_value * VOLTAGE_SCALE;
	}

	us_mvdd = 0;
	if ((POLARIS10_VOLTAGE_CONTROL_NONE == data->mvdd_control) ||
			(data->mclk_dpm_key_disabled))
		us_mvdd = data->vbios_boot_state.mvdd_bootup_value;
	else {
		if (!polaris10_populate_mvdd_value(hwmgr,
				data->dpm_table.mclk_table.dpm_levels[0].value,
				&vol_level))
			us_mvdd = vol_level.Voltage;
	}

	if (0 == polaris10_populate_mvdd_value(hwmgr, 0, &vol_level))
		table->MemoryACPILevel.MinMvdd = PP_HOST_TO_SMC_UL(vol_level.Voltage);
	else
		table->MemoryACPILevel.MinMvdd = 0;

	table->MemoryACPILevel.StutterEnable = false;

	table->MemoryACPILevel.EnabledForThrottle = 0;
	table->MemoryACPILevel.EnabledForActivity = 0;
	table->MemoryACPILevel.UpHyst = 0;
	table->MemoryACPILevel.DownHyst = 100;
	table->MemoryACPILevel.VoltageDownHyst = 0;
	table->MemoryACPILevel.ActivityLevel =
			PP_HOST_TO_SMC_US((uint16_t)data->mclk_activity_target);

	CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_UL(table->MemoryACPILevel.MinVoltage);

	return result;
}

static int polaris10_populate_smc_vce_level(struct pp_hwmgr *hwmgr,
		SMU74_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	table->VceLevelCount = (uint8_t)(mm_table->count);
	table->VceBootLevel = 0;

	for (count = 0; count < table->VceLevelCount; count++) {
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

static int polaris10_populate_smc_samu_level(struct pp_hwmgr *hwmgr,
		SMU74_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

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

static int polaris10_populate_memory_timing_parameters(struct pp_hwmgr *hwmgr,
		int32_t eng_clock, int32_t mem_clock,
		SMU74_Discrete_MCArbDramTimingTableEntry *arb_regs)
{
	uint32_t dram_timing;
	uint32_t dram_timing2;
	uint32_t burst_time;
	int result;

	result = atomctrl_set_engine_dram_timings_rv770(hwmgr,
			eng_clock, mem_clock);
	PP_ASSERT_WITH_CODE(result == 0,
			"Error calling VBIOS to set DRAM_TIMING.", return result);

	dram_timing = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING);
	dram_timing2 = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2);
	burst_time = PHM_READ_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE0);


	arb_regs->McArbDramTiming  = PP_HOST_TO_SMC_UL(dram_timing);
	arb_regs->McArbDramTiming2 = PP_HOST_TO_SMC_UL(dram_timing2);
	arb_regs->McArbBurstTime   = (uint8_t)burst_time;

	return 0;
}

static int polaris10_program_memory_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct SMU74_Discrete_MCArbDramTimingTable arb_regs;
	uint32_t i, j;
	int result = 0;

	for (i = 0; i < data->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < data->dpm_table.mclk_table.count; j++) {
			result = polaris10_populate_memory_timing_parameters(hwmgr,
					data->dpm_table.sclk_table.dpm_levels[i].value,
					data->dpm_table.mclk_table.dpm_levels[j].value,
					&arb_regs.entries[i][j]);
			if (result == 0)
				result = atomctrl_set_ac_timing_ai(hwmgr, data->dpm_table.mclk_table.dpm_levels[j].value, j);
			if (result != 0)
				return result;
		}
	}

	result = polaris10_copy_bytes_to_smc(
			hwmgr->smumgr,
			data->arb_table_start,
			(uint8_t *)&arb_regs,
			sizeof(SMU74_Discrete_MCArbDramTimingTable),
			data->sram_end);
	return result;
}

static int polaris10_populate_smc_uvd_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	int result = -EINVAL;
	uint8_t count;
	struct pp_atomctrl_clock_dividers_vi dividers;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_mm_clock_voltage_dependency_table *mm_table =
			table_info->mm_dep_table;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

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

static int polaris10_populate_smc_boot_level(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	int result = 0;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	table->GraphicsBootLevel = 0;
	table->MemoryBootLevel = 0;

	/* find boot level from dpm table */
	result = phm_find_boot_level(&(data->dpm_table.sclk_table),
			data->vbios_boot_state.sclk_bootup_value,
			(uint32_t *)&(table->GraphicsBootLevel));

	result = phm_find_boot_level(&(data->dpm_table.mclk_table),
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


static int polaris10_populate_smc_initailial_state(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint8_t count, level;

	count = (uint8_t)(table_info->vdd_dep_on_sclk->count);

	for (level = 0; level < count; level++) {
		if (table_info->vdd_dep_on_sclk->entries[level].clk >=
				data->vbios_boot_state.sclk_bootup_value) {
			data->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	count = (uint8_t)(table_info->vdd_dep_on_mclk->count);
	for (level = 0; level < count; level++) {
		if (table_info->vdd_dep_on_mclk->entries[level].clk >=
				data->vbios_boot_state.mclk_bootup_value) {
			data->smc_state_table.MemoryBootLevel = level;
			break;
		}
	}

	return 0;
}

static int polaris10_populate_clock_stretcher_data_table(struct pp_hwmgr *hwmgr)
{
	uint32_t ro, efuse, efuse2, clock_freq, volt_without_cks,
			volt_with_cks, value;
	uint16_t clock_freq_u16;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
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
	/* PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, PWR_CKS_ENABLE, staticEnable, 0x1); */
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
			polaris10_clock_stretcher_lookup_table[stretch_amount2][0];
	data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].maxFreq =
			polaris10_clock_stretcher_lookup_table[stretch_amount2][1];
	clock_freq_u16 = (uint16_t)(PP_SMC_TO_HOST_UL(data->smc_state_table.
			GraphicsLevel[data->smc_state_table.GraphicsDpmLevelCount - 1].SclkSetting.SclkFrequency) / 100);
	if (polaris10_clock_stretcher_lookup_table[stretch_amount2][0] < clock_freq_u16
	&& polaris10_clock_stretcher_lookup_table[stretch_amount2][1] > clock_freq_u16) {
		/* Program PWR_CKS_CNTL. CKS_USE_FOR_LOW_FREQ */
		value |= (polaris10_clock_stretcher_lookup_table[stretch_amount2][3]) << 16;
		/* Program PWR_CKS_CNTL. CKS_LDO_REFSEL */
		value |= (polaris10_clock_stretcher_lookup_table[stretch_amount2][2]) << 18;
		/* Program PWR_CKS_CNTL. CKS_STRETCH_AMOUNT */
		value |= (polaris10_clock_stretch_amount_conversion
				[polaris10_clock_stretcher_lookup_table[stretch_amount2][3]]
				 [stretch_amount]) << 3;
	}
	CONVERT_FROM_HOST_TO_SMC_US(data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].minFreq);
	CONVERT_FROM_HOST_TO_SMC_US(data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].maxFreq);
	data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].setting =
			polaris10_clock_stretcher_lookup_table[stretch_amount2][2] & 0x7F;
	data->smc_state_table.CKS_LOOKUPTable.CKS_LOOKUPTableEntry[0].setting |=
			(polaris10_clock_stretcher_lookup_table[stretch_amount2][3]) << 7;

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixPWR_CKS_CNTL, value);

	/* Populate DDT Lookup Table */
	for (i = 0; i < 4; i++) {
		/* Assign the minimum and maximum VID stored
		 * in the last row of Clock Stretcher Voltage Table.
		 */
		data->smc_state_table.ClockStretcherDataTable.ClockStretcherDataTableEntry[i].minVID =
				(uint8_t) polaris10_clock_stretcher_ddt_table[type][i][2];
		data->smc_state_table.ClockStretcherDataTable.ClockStretcherDataTableEntry[i].maxVID =
				(uint8_t) polaris10_clock_stretcher_ddt_table[type][i][3];
		/* Loop through each SCLK and check the frequency
		 * to see if it lies within the frequency for clock stretcher.
		 */
		for (j = 0; j < data->smc_state_table.GraphicsDpmLevelCount; j++) {
			cks_setting = 0;
			clock_freq = PP_SMC_TO_HOST_UL(
					data->smc_state_table.GraphicsLevel[j].SclkSetting.SclkFrequency);
			/* Check the allowed frequency against the sclk level[j].
			 *  Sclk's endianness has already been converted,
			 *  and it's in 10Khz unit,
			 *  as opposed to Data table, which is in Mhz unit.
			 */
			if (clock_freq >= (polaris10_clock_stretcher_ddt_table[type][i][0]) * 100) {
				cks_setting |= 0x2;
				if (clock_freq < (polaris10_clock_stretcher_ddt_table[type][i][1]) * 100)
					cks_setting |= 0x1;
			}
			data->smc_state_table.ClockStretcherDataTable.ClockStretcherDataTableEntry[i].setting
							|= cks_setting << (j * 2);
		}
		CONVERT_FROM_HOST_TO_SMC_US(
			data->smc_state_table.ClockStretcherDataTable.ClockStretcherDataTableEntry[i].setting);
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
static int polaris10_populate_vr_config(struct pp_hwmgr *hwmgr,
		struct SMU74_Discrete_DpmTable *table)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint16_t config;

	config = VR_MERGED_WITH_VDDC;
	table->VRConfig |= (config << VRCONF_VDDGFX_SHIFT);

	/* Set Vddc Voltage Controller */
	if (POLARIS10_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control) {
		config = VR_SVI2_PLANE_1;
		table->VRConfig |= config;
	} else {
		PP_ASSERT_WITH_CODE(false,
				"VDDC should be on SVI2 control in merged mode!",
				);
	}
	/* Set Vddci Voltage Controller */
	if (POLARIS10_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control) {
		config = VR_SVI2_PLANE_2;  /* only in merged mode */
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	} else if (POLARIS10_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control) {
		config = VR_SMIO_PATTERN_1;
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	} else {
		config = VR_STATIC_VOLTAGE;
		table->VRConfig |= (config << VRCONF_VDDCI_SHIFT);
	}
	/* Set Mvdd Voltage Controller */
	if (POLARIS10_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control) {
		config = VR_SVI2_PLANE_2;
		table->VRConfig |= (config << VRCONF_MVDD_SHIFT);
	} else if (POLARIS10_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control) {
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
* @return   always 0
*/
static int polaris10_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct SMU74_Discrete_DpmTable *table = &(data->smc_state_table);
	const struct polaris10_ulv_parm *ulv = &(data->ulv);
	uint8_t i;
	struct pp_atomctrl_gpio_pin_assignment gpio_pin;
	pp_atomctrl_clock_dividers_vi dividers;

	result = polaris10_setup_default_dpm_tables(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to setup default DPM tables!", return result);

	if (POLARIS10_VOLTAGE_CONTROL_NONE != data->voltage_control)
		polaris10_populate_smc_voltage_tables(hwmgr, table);

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
		result = polaris10_populate_ulv_state(hwmgr, table);
		PP_ASSERT_WITH_CODE(0 == result,
				"Failed to initialize ULV state!", return result);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				ixCG_ULV_PARAMETER, PPPOLARIS10_CGULVPARAMETER_DFLT);
	}

	result = polaris10_populate_smc_link_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Link Level!", return result);

	result = polaris10_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Graphics Level!", return result);

	result = polaris10_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Memory Level!", return result);

	result = polaris10_populate_smc_acpi_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize ACPI Level!", return result);

	result = polaris10_populate_smc_vce_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize VCE Level!", return result);

	result = polaris10_populate_smc_samu_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize SAMU Level!", return result);

	/* Since only the initial state is completely set up at this point
	 * (the other states are just copies of the boot state) we only
	 * need to populate the  ARB settings for the initial state.
	 */
	result = polaris10_program_memory_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to Write ARB settings for the initial state.", return result);

	result = polaris10_populate_smc_uvd_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize UVD Level!", return result);

	result = polaris10_populate_smc_boot_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Boot Level!", return result);

	result = polaris10_populate_smc_initailial_state(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize Boot State!", return result);

	result = polaris10_populate_bapm_parameters_in_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to populate BAPM Parameters!", return result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ClockStretcher)) {
		result = polaris10_populate_clock_stretcher_data_table(hwmgr);
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
			POLARIS10_Q88_FORMAT_CONVERSION_UNIT;
	table->TemperatureLimitLow  =
			(table_info->cac_dtp_table->usTargetOperatingTemp - 1) *
			POLARIS10_Q88_FORMAT_CONVERSION_UNIT;
	table->MemoryVoltageChangeEnable = 1;
	table->MemoryInterval = 1;
	table->VoltageResponseTime = 0;
	table->PhaseResponseTime = 0;
	table->MemoryThermThrottleEnable = 1;
	table->PCIeBootLinkLevel = 0;
	table->PCIeGenInterval = 1;
	table->VRConfig = 0;

	result = polaris10_populate_vr_config(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to populate VRConfig setting!", return result);

	table->ThermGpio = 17;
	table->SclkStepSize = 0x4000;

	if (atomctrl_get_pp_assign_pin(hwmgr, VDDC_VRHOT_GPIO_PINID, &gpio_pin)) {
		table->VRHotGpio = gpio_pin.uc_gpio_pin_bit_shift;
	} else {
		table->VRHotGpio = POLARIS10_UNUSED_GPIO_PIN;
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_RegulatorHot);
	}

	if (atomctrl_get_pp_assign_pin(hwmgr, PP_AC_DC_SWITCH_GPIO_PINID,
			&gpio_pin)) {
		table->AcDcGpio = gpio_pin.uc_gpio_pin_bit_shift;
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_AutomaticDCTransition);
	} else {
		table->AcDcGpio = POLARIS10_UNUSED_GPIO_PIN;
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
		table->ThermOutPolarity = (0 == (cgs_read_register(hwmgr->device, mmGPIOPAD_A)
					& (1 << gpio_pin.uc_gpio_pin_bit_shift))) ? 1:0;
		table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_ONLY;

		/* if required, combine VRHot/PCC with thermal out GPIO */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_RegulatorHot)
		&& phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_CombinePCCWithThermalSignal))
			table->ThermOutMode = SMU7_THERM_OUT_MODE_THERM_VRHOT;
	} else {
		table->ThermOutGpio = 17;
		table->ThermOutPolarity = 1;
		table->ThermOutMode = SMU7_THERM_OUT_MODE_DISABLE;
	}

	/* Populate BIF_SCLK levels into SMC DPM table */
	for (i = 0; i <= data->dpm_table.pcie_speed_table.count; i++) {
		result = atomctrl_get_dfs_pll_dividers_vi(hwmgr, data->bif_sclk_table[i], &dividers);
		PP_ASSERT_WITH_CODE((result == 0), "Can not find DFS divide id for Sclk", return result);

		if (i == 0)
			table->Ulv.BifSclkDfs = PP_HOST_TO_SMC_US((USHORT)(dividers.pll_post_divider));
		else
			table->LinkLevel[i-1].BifSclkDfs = PP_HOST_TO_SMC_US((USHORT)(dividers.pll_post_divider));
	}

	for (i = 0; i < SMU74_MAX_ENTRIES_SMIO; i++)
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
	result = polaris10_copy_bytes_to_smc(hwmgr->smumgr,
			data->dpm_table_start +
			offsetof(SMU74_Discrete_DpmTable, SystemFlags),
			(uint8_t *)&(table->SystemFlags),
			sizeof(SMU74_Discrete_DpmTable) - 3 * sizeof(SMU74_PIDController),
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
static int polaris10_init_arb_table_index(struct pp_hwmgr *hwmgr)
{
	const struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
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
	result = polaris10_read_smc_sram_dword(hwmgr->smumgr,
			data->arb_table_start, &tmp, data->sram_end);

	if (result)
		return result;

	tmp &= 0x00FFFFFF;
	tmp |= ((uint32_t)MC_CG_ARB_FREQ_F1) << 24;

	return polaris10_write_smc_sram_dword(hwmgr->smumgr,
			data->arb_table_start, tmp, data->sram_end);
}

static int polaris10_enable_vrhot_gpio_interrupt(struct pp_hwmgr *hwmgr)
{
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_RegulatorHot))
		return smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_EnableVRHotGPIOInterrupt);

	return 0;
}

static int polaris10_enable_sclk_control(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL,
			SCLK_PWRMGT_OFF, 0);
	return 0;
}

static int polaris10_enable_ulv(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_ulv_parm *ulv = &(data->ulv);

	if (ulv->ulv_supported)
		return smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_EnableULV);

	return 0;
}

static int polaris10_enable_deep_sleep_master_switch(struct pp_hwmgr *hwmgr)
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

static int polaris10_enable_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	/* enable SCLK dpm */
	if (!data->sclk_dpm_key_disabled)
		PP_ASSERT_WITH_CODE(
		(0 == smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_DPM_Enable)),
		"Failed to enable SCLK DPM during DPM Start Function!",
		return -1);

	/* enable MCLK dpm */
	if (0 == data->mclk_dpm_key_disabled) {

		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_MCLKDPM_Enable)),
				"Failed to enable MCLK DPM during DPM Start Function!",
				return -1);


		PHM_WRITE_FIELD(hwmgr->device, MC_SEQ_CNTL_3, CAC_EN, 0x1);

		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixLCAC_MC0_CNTL, 0x5);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixLCAC_MC1_CNTL, 0x5);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixLCAC_CPL_CNTL, 0x100005);
		udelay(10);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixLCAC_MC0_CNTL, 0x400005);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixLCAC_MC1_CNTL, 0x400005);
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixLCAC_CPL_CNTL, 0x500005);
	}

	return 0;
}

static int polaris10_start_dpm(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	/*enable general power management */

	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, GENERAL_PWRMGT,
			GLOBAL_PWRMGT_EN, 1);

	/* enable sclk deep sleep */

	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SCLK_PWRMGT_CNTL,
			DYNAMIC_PM_EN, 1);

	/* prepare for PCIE DPM */

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			data->soft_regs_start + offsetof(SMU74_SoftRegisters,
					VoltageChangeTimeout), 0x1000);
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__PCIE,
			SWRST_COMMAND_1, RESETLC, 0x0);
/*
	PP_ASSERT_WITH_CODE(
			(0 == smum_send_msg_to_smc(hwmgr->smumgr,
					PPSMC_MSG_Voltage_Cntl_Enable)),
			"Failed to enable voltage DPM during DPM Start Function!",
			return -1);
*/

	if (polaris10_enable_sclk_mclk_dpm(hwmgr)) {
		printk(KERN_ERR "Failed to enable Sclk DPM and Mclk DPM!");
		return -1;
	}

	/* enable PCIE dpm */
	if (0 == data->pcie_dpm_key_disabled) {
		PP_ASSERT_WITH_CODE(
				(0 == smum_send_msg_to_smc(hwmgr->smumgr,
						PPSMC_MSG_PCIeDPM_Enable)),
				"Failed to enable pcie DPM during DPM Start Function!",
				return -1);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_Falcon_QuickTransition)) {
		PP_ASSERT_WITH_CODE((0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_EnableACDCGPIOInterrupt)),
				"Failed to enable AC DC GPIO Interrupt!",
				);
	}

	return 0;
}

static void polaris10_set_dpm_event_sources(struct pp_hwmgr *hwmgr, uint32_t sources)
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

static int polaris10_enable_auto_throttle_source(struct pp_hwmgr *hwmgr,
		PHM_AutoThrottleSource source)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (!(data->active_auto_throttle_sources & (1 << source))) {
		data->active_auto_throttle_sources |= 1 << source;
		polaris10_set_dpm_event_sources(hwmgr, data->active_auto_throttle_sources);
	}
	return 0;
}

static int polaris10_enable_thermal_auto_throttle(struct pp_hwmgr *hwmgr)
{
	return polaris10_enable_auto_throttle_source(hwmgr, PHM_AutoThrottleSource_Thermal);
}

int polaris10_pcie_performance_request(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	data->pcie_performance_request = true;

	return 0;
}

int polaris10_enable_dpm_tasks(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;
	tmp_result = (!polaris10_is_dpm_running(hwmgr)) ? 0 : -1;
	PP_ASSERT_WITH_CODE(result == 0,
			"DPM is already running right now, no need to enable DPM!",
			return 0);

	if (polaris10_voltage_control(hwmgr)) {
		tmp_result = polaris10_enable_voltage_control(hwmgr);
		PP_ASSERT_WITH_CODE(tmp_result == 0,
				"Failed to enable voltage control!",
				result = tmp_result);

		tmp_result = polaris10_construct_voltage_tables(hwmgr);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
				"Failed to contruct voltage tables!",
				result = tmp_result);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EngineSpreadSpectrumSupport))
		PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				GENERAL_PWRMGT, DYN_SPREAD_SPECTRUM_EN, 1);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ThermalController))
		PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				GENERAL_PWRMGT, THERMAL_PROTECTION_DIS, 0);

	tmp_result = polaris10_program_static_screen_threshold_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to program static screen threshold parameters!",
			result = tmp_result);

	tmp_result = polaris10_enable_display_gap(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable display gap!", result = tmp_result);

	tmp_result = polaris10_program_voting_clients(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to program voting clients!", result = tmp_result);

	tmp_result = polaris10_process_firmware_header(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to process firmware header!", result = tmp_result);

	tmp_result = polaris10_initial_switch_from_arbf0_to_f1(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to initialize switch from ArbF0 to F1!",
			result = tmp_result);

	tmp_result = polaris10_init_smc_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to initialize SMC table!", result = tmp_result);

	tmp_result = polaris10_init_arb_table_index(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to initialize ARB table index!", result = tmp_result);

	tmp_result = polaris10_populate_pm_fuses(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to populate PM fuses!", result = tmp_result);

	tmp_result = polaris10_enable_vrhot_gpio_interrupt(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable VR hot GPIO interrupt!", result = tmp_result);

	tmp_result = polaris10_enable_sclk_control(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable SCLK control!", result = tmp_result);

	tmp_result = polaris10_enable_smc_voltage_controller(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable voltage control!", result = tmp_result);

	tmp_result = polaris10_enable_ulv(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable ULV!", result = tmp_result);

	tmp_result = polaris10_enable_deep_sleep_master_switch(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable deep sleep master switch!", result = tmp_result);

	tmp_result = polaris10_start_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to start DPM!", result = tmp_result);

	tmp_result = polaris10_enable_smc_cac(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable SMC CAC!", result = tmp_result);

	tmp_result = polaris10_enable_power_containment(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable power containment!", result = tmp_result);

	tmp_result = polaris10_power_control_set_level(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to power control set level!", result = tmp_result);

	tmp_result = polaris10_enable_thermal_auto_throttle(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable thermal auto throttle!", result = tmp_result);

	tmp_result = polaris10_pcie_performance_request(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"pcie performance request failed!", result = tmp_result);

	return result;
}

int polaris10_disable_dpm_tasks(struct pp_hwmgr *hwmgr)
{

	return 0;
}

int polaris10_reset_asic_tasks(struct pp_hwmgr *hwmgr)
{

	return 0;
}

int polaris10_hwmgr_backend_fini(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (data->soft_pp_table) {
		kfree(data->soft_pp_table);
		data->soft_pp_table = NULL;
	}

	return phm_hwmgr_backend_fini(hwmgr);
}

int polaris10_set_features_platform_caps(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
		PHM_PlatformCaps_DynamicPatchPowerState);

	if (data->mvdd_control == POLARIS10_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_EnableMVDDControl);

	if (data->vddci_control == POLARIS10_VOLTAGE_CONTROL_NONE)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_ControlVDDCI);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			 PHM_PlatformCaps_TablelessHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableSMU7ThermalManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DynamicPowerManagement);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_UnTabledHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TablelessHardwareInterface);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SMC);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_NonABMSupportInPPLib);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_DynamicUVDState);

	/* power tune caps Assume disabled */
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_TCPRamping);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_PowerContainment);
	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
							PHM_PlatformCaps_CAC);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_RegulatorHot);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_AutomaticDCTransition);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_ODFuzzyFanControlSupport);

	phm_cap_set(hwmgr->platform_descriptor.platformCaps,
						PHM_PlatformCaps_FanSpeedInTableIsRPM);
	if (hwmgr->chip_id == CHIP_POLARIS11)
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_SPLLShutdownSupport);
	return 0;
}

static void polaris10_init_dpm_defaults(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	polaris10_initialize_power_tune_defaults(hwmgr);

	data->pcie_gen_performance.max = PP_PCIEGen1;
	data->pcie_gen_performance.min = PP_PCIEGen3;
	data->pcie_gen_power_saving.max = PP_PCIEGen1;
	data->pcie_gen_power_saving.min = PP_PCIEGen3;
	data->pcie_lane_performance.max = 0;
	data->pcie_lane_performance.min = 16;
	data->pcie_lane_power_saving.max = 0;
	data->pcie_lane_power_saving.min = 16;
}

/**
* Get Leakage VDDC based on leakage ID.
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always 0
*/
static int polaris10_get_evv_voltages(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint16_t vv_id;
	uint16_t vddc = 0;
	uint16_t i, j;
	uint32_t sclk = 0;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)hwmgr->pptable;
	struct phm_ppt_v1_clock_voltage_dependency_table *sclk_table =
			table_info->vdd_dep_on_sclk;
	int result;

	for (i = 0; i < POLARIS10_MAX_LEAKAGE_COUNT; i++) {
		vv_id = ATOM_VIRTUAL_VOLTAGE_ID0 + i;
		if (!phm_get_sclk_for_voltage_evv(hwmgr,
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


			PP_ASSERT_WITH_CODE(0 == atomctrl_get_voltage_evv_on_sclk_ai(hwmgr,
							VOLTAGE_TYPE_VDDC, sclk, vv_id, &vddc),
						"Error retrieving EVV voltage value!",
						continue);


			/* need to make sure vddc is less than 2v or else, it could burn the ASIC. */
			PP_ASSERT_WITH_CODE((vddc < 2000 && vddc != 0),
					"Invalid VDDC value", result = -EINVAL;);

			/* the voltage should not be zero nor equal to leakage ID */
			if (vddc != 0 && vddc != vv_id) {
				data->vddc_leakage.actual_voltage[data->vddc_leakage.count] = (uint16_t)(vddc/100);
				data->vddc_leakage.leakage_id[data->vddc_leakage.count] = vv_id;
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
static void polaris10_patch_with_vdd_leakage(struct pp_hwmgr *hwmgr,
		uint16_t *voltage, struct polaris10_leakage_voltage *leakage_table)
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
static int polaris10_patch_lookup_table_with_leakage(struct pp_hwmgr *hwmgr,
		phm_ppt_v1_voltage_lookup_table *lookup_table,
		struct polaris10_leakage_voltage *leakage_table)
{
	uint32_t i;

	for (i = 0; i < lookup_table->count; i++)
		polaris10_patch_with_vdd_leakage(hwmgr,
				&lookup_table->entries[i].us_vdd, leakage_table);

	return 0;
}

static int polaris10_patch_clock_voltage_limits_with_vddc_leakage(
		struct pp_hwmgr *hwmgr, struct polaris10_leakage_voltage *leakage_table,
		uint16_t *vddc)
{
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	polaris10_patch_with_vdd_leakage(hwmgr, (uint16_t *)vddc, leakage_table);
	hwmgr->dyn_state.max_clock_voltage_on_dc.vddc =
			table_info->max_clock_voltage_on_dc.vddc;
	return 0;
}

static int polaris10_patch_voltage_dependency_tables_with_lookup_table(
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

static int polaris10_calc_voltage_dependency_tables(struct pp_hwmgr *hwmgr)
{
	/* Need to determine if we need calculated voltage. */
	return 0;
}

static int polaris10_calc_mm_voltage_dependency_table(struct pp_hwmgr *hwmgr)
{
	/* Need to determine if we need calculated voltage from mm table. */
	return 0;
}

static int polaris10_sort_lookup_table(struct pp_hwmgr *hwmgr,
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

static int polaris10_complete_dependency_tables(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	int tmp_result;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	tmp_result = polaris10_patch_lookup_table_with_leakage(hwmgr,
			table_info->vddc_lookup_table, &(data->vddc_leakage));
	if (tmp_result)
		result = tmp_result;

	tmp_result = polaris10_patch_clock_voltage_limits_with_vddc_leakage(hwmgr,
			&(data->vddc_leakage), &table_info->max_clock_voltage_on_dc.vddc);
	if (tmp_result)
		result = tmp_result;

	tmp_result = polaris10_patch_voltage_dependency_tables_with_lookup_table(hwmgr);
	if (tmp_result)
		result = tmp_result;

	tmp_result = polaris10_calc_voltage_dependency_tables(hwmgr);
	if (tmp_result)
		result = tmp_result;

	tmp_result = polaris10_calc_mm_voltage_dependency_table(hwmgr);
	if (tmp_result)
		result = tmp_result;

	tmp_result = polaris10_sort_lookup_table(hwmgr, table_info->vddc_lookup_table);
	if (tmp_result)
		result = tmp_result;

	return result;
}

static int polaris10_set_private_data_based_on_pptable(struct pp_hwmgr *hwmgr)
{
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

	table_info->max_clock_voltage_on_ac.sclk =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].clk;
	table_info->max_clock_voltage_on_ac.mclk =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].clk;
	table_info->max_clock_voltage_on_ac.vddc =
		allowed_sclk_vdd_table->entries[allowed_sclk_vdd_table->count - 1].vddc;
	table_info->max_clock_voltage_on_ac.vddci =
		allowed_mclk_vdd_table->entries[allowed_mclk_vdd_table->count - 1].vddci;

	hwmgr->dyn_state.max_clock_voltage_on_ac.sclk = table_info->max_clock_voltage_on_ac.sclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.mclk = table_info->max_clock_voltage_on_ac.mclk;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddc = table_info->max_clock_voltage_on_ac.vddc;
	hwmgr->dyn_state.max_clock_voltage_on_ac.vddci =table_info->max_clock_voltage_on_ac.vddci;

	return 0;
}

int polaris10_hwmgr_backend_init(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_gpio_pin_assignment gpio_pin_assignment;
	uint32_t temp_reg;
	int result;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	data->dll_default_on = false;
	data->sram_end = SMC_RAM_END;
	data->mclk_dpm0_activity_target = 0xa;
	data->disable_dpm_mask = 0xFF;
	data->static_screen_threshold = PPPOLARIS10_STATICSCREENTHRESHOLD_DFLT;
	data->static_screen_threshold_unit = PPPOLARIS10_STATICSCREENTHRESHOLD_DFLT;
	data->activity_target[0] = PPPOLARIS10_TARGETACTIVITY_DFLT;
	data->activity_target[1] = PPPOLARIS10_TARGETACTIVITY_DFLT;
	data->activity_target[2] = PPPOLARIS10_TARGETACTIVITY_DFLT;
	data->activity_target[3] = PPPOLARIS10_TARGETACTIVITY_DFLT;
	data->activity_target[4] = PPPOLARIS10_TARGETACTIVITY_DFLT;
	data->activity_target[5] = PPPOLARIS10_TARGETACTIVITY_DFLT;
	data->activity_target[6] = PPPOLARIS10_TARGETACTIVITY_DFLT;
	data->activity_target[7] = PPPOLARIS10_TARGETACTIVITY_DFLT;

	data->voting_rights_clients0 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT0;
	data->voting_rights_clients1 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT1;
	data->voting_rights_clients2 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT2;
	data->voting_rights_clients3 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT3;
	data->voting_rights_clients4 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT4;
	data->voting_rights_clients5 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT5;
	data->voting_rights_clients6 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT6;
	data->voting_rights_clients7 = PPPOLARIS10_VOTINGRIGHTSCLIENTS_DFLT7;

	data->vddc_vddci_delta = VDDC_VDDCI_DELTA;

	data->mclk_activity_target = PPPOLARIS10_MCLK_TARGETACTIVITY_DFLT;

	/* need to set voltage control types before EVV patching */
	data->voltage_control = POLARIS10_VOLTAGE_CONTROL_NONE;
	data->vddci_control = POLARIS10_VOLTAGE_CONTROL_NONE;
	data->mvdd_control = POLARIS10_VOLTAGE_CONTROL_NONE;

	if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
			VOLTAGE_TYPE_VDDC, VOLTAGE_OBJ_SVID2))
		data->voltage_control = POLARIS10_VOLTAGE_CONTROL_BY_SVID2;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EnableMVDDControl)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_GPIO_LUT))
			data->mvdd_control = POLARIS10_VOLTAGE_CONTROL_BY_GPIO;
		else if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_MVDDC, VOLTAGE_OBJ_SVID2))
			data->mvdd_control = POLARIS10_VOLTAGE_CONTROL_BY_SVID2;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ControlVDDCI)) {
		if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_GPIO_LUT))
			data->vddci_control = POLARIS10_VOLTAGE_CONTROL_BY_GPIO;
		else if (atomctrl_is_voltage_controled_by_gpio_v3(hwmgr,
				VOLTAGE_TYPE_VDDCI, VOLTAGE_OBJ_SVID2))
			data->vddci_control = POLARIS10_VOLTAGE_CONTROL_BY_SVID2;
	}

	polaris10_set_features_platform_caps(hwmgr);

	polaris10_init_dpm_defaults(hwmgr);

	/* Get leakage voltage based on leakage ID. */
	result = polaris10_get_evv_voltages(hwmgr);

	if (result) {
		printk("Get EVV Voltage Failed.  Abort Driver loading!\n");
		return -1;
	}

	polaris10_complete_dependency_tables(hwmgr);
	polaris10_set_private_data_based_on_pptable(hwmgr);

	/* Initalize Dynamic State Adjustment Rule Settings */
	result = phm_initializa_dynamic_state_adjustment_rule_settings(hwmgr);

	if (0 == result) {
		struct cgs_system_info sys_info = {0};

		data->is_tlu_enabled = 0;

		hwmgr->platform_descriptor.hardwareActivityPerformanceLevels =
							POLARIS10_MAX_HARDWARE_POWERLEVELS;
		hwmgr->platform_descriptor.hardwarePerformanceLevels = 2;
		hwmgr->platform_descriptor.minimumClocksReductionPercentage = 50;


		if (atomctrl_get_pp_assign_pin(hwmgr, VDDC_PCC_GPIO_PINID, &gpio_pin_assignment)) {
			temp_reg = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCNB_PWRMGT_CNTL);
			switch (gpio_pin_assignment.uc_gpio_pin_bit_shift) {
			case 0:
				temp_reg = PHM_SET_FIELD(temp_reg, CNB_PWRMGT_CNTL, GNB_SLOW_MODE, 0x1);
				break;
			case 1:
				temp_reg = PHM_SET_FIELD(temp_reg, CNB_PWRMGT_CNTL, GNB_SLOW_MODE, 0x2);
				break;
			case 2:
				temp_reg = PHM_SET_FIELD(temp_reg, CNB_PWRMGT_CNTL, GNB_SLOW, 0x1);
				break;
			case 3:
				temp_reg = PHM_SET_FIELD(temp_reg, CNB_PWRMGT_CNTL, FORCE_NB_PS1, 0x1);
				break;
			case 4:
				temp_reg = PHM_SET_FIELD(temp_reg, CNB_PWRMGT_CNTL, DPM_ENABLED, 0x1);
				break;
			default:
				PP_ASSERT_WITH_CODE(0,
				"Failed to setup PCC HW register! Wrong GPIO assigned for VDDC_PCC_GPIO_PINID!",
				);
				break;
			}
			cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCNB_PWRMGT_CNTL, temp_reg);
		}

		if (table_info->cac_dtp_table->usDefaultTargetOperatingTemp != 0 &&
			hwmgr->thermal_controller.advanceFanControlParameters.ucFanControlMode) {
			hwmgr->thermal_controller.advanceFanControlParameters.usFanPWMMinLimit =
				(uint16_t)hwmgr->thermal_controller.advanceFanControlParameters.ucMinimumPWMLimit;

			hwmgr->thermal_controller.advanceFanControlParameters.usFanPWMMaxLimit =
				(uint16_t)hwmgr->thermal_controller.advanceFanControlParameters.usDefaultMaxFanPWM;

			hwmgr->thermal_controller.advanceFanControlParameters.usFanPWMStep = 1;

			hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMMaxLimit = 100;

			hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMMinLimit =
				(uint16_t)hwmgr->thermal_controller.advanceFanControlParameters.ucMinimumPWMLimit;

			hwmgr->thermal_controller.advanceFanControlParameters.usFanRPMStep = 1;

			table_info->cac_dtp_table->usDefaultTargetOperatingTemp = (table_info->cac_dtp_table->usDefaultTargetOperatingTemp >= 50) ?
									(table_info->cac_dtp_table->usDefaultTargetOperatingTemp -50) : 0;

			table_info->cac_dtp_table->usOperatingTempMaxLimit = table_info->cac_dtp_table->usDefaultTargetOperatingTemp;
			table_info->cac_dtp_table->usOperatingTempStep = 1;
			table_info->cac_dtp_table->usOperatingTempHyst = 1;

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

		hwmgr->platform_descriptor.vbiosInterruptId = 0x20000400; /* IRQ_SOURCE1_SW_INT */
/* The true clock step depends on the frequency, typically 4.5 or 9 MHz. Here we use 5. */
		hwmgr->platform_descriptor.clockStep.engineClock = 500;
		hwmgr->platform_descriptor.clockStep.memoryClock = 500;
	} else {
		/* Ignore return value in here, we are cleaning up a mess. */
		polaris10_hwmgr_backend_fini(hwmgr);
	}

	return 0;
}

static int polaris10_force_dpm_highest(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t level, tmp;

	if (!data->pcie_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			level = 0;
			tmp = data->dpm_level_enable_mask.pcie_dpm_enable_mask;
			while (tmp >>= 1)
				level++;

			if (level)
				smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
						PPSMC_MSG_PCIeDPM_ForceLevel, level);
		}
	}

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

	return 0;
}

static int polaris10_upload_dpm_level_enable_mask(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	phm_apply_dal_min_voltage_request(hwmgr);

	if (!data->sclk_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_SCLKDPM_SetEnabledMask,
					data->dpm_level_enable_mask.sclk_dpm_enable_mask);
	}

	if (!data->mclk_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask)
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_MCLKDPM_SetEnabledMask,
					data->dpm_level_enable_mask.mclk_dpm_enable_mask);
	}

	return 0;
}

static int polaris10_unforce_dpm_levels(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (!polaris10_is_dpm_running(hwmgr))
		return -EINVAL;

	if (!data->pcie_dpm_key_disabled) {
		smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_PCIeDPM_UnForceLevel);
	}

	return polaris10_upload_dpm_level_enable_mask(hwmgr);
}

static int polaris10_force_dpm_lowest(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data =
			(struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t level;

	if (!data->sclk_dpm_key_disabled)
		if (data->dpm_level_enable_mask.sclk_dpm_enable_mask) {
			level = phm_get_lowest_enabled_level(hwmgr,
							      data->dpm_level_enable_mask.sclk_dpm_enable_mask);
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
							    PPSMC_MSG_SCLKDPM_SetEnabledMask,
							    (1 << level));

	}

	if (!data->mclk_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.mclk_dpm_enable_mask) {
			level = phm_get_lowest_enabled_level(hwmgr,
							      data->dpm_level_enable_mask.mclk_dpm_enable_mask);
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
							    PPSMC_MSG_MCLKDPM_SetEnabledMask,
							    (1 << level));
		}
	}

	if (!data->pcie_dpm_key_disabled) {
		if (data->dpm_level_enable_mask.pcie_dpm_enable_mask) {
			level = phm_get_lowest_enabled_level(hwmgr,
							      data->dpm_level_enable_mask.pcie_dpm_enable_mask);
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
							    PPSMC_MSG_PCIeDPM_ForceLevel,
							    (level));
		}
	}

	return 0;

}
static int polaris10_force_dpm_level(struct pp_hwmgr *hwmgr,
				enum amd_dpm_forced_level level)
{
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = polaris10_force_dpm_highest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = polaris10_force_dpm_lowest(hwmgr);
		if (ret)
			return ret;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		ret = polaris10_unforce_dpm_levels(hwmgr);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	hwmgr->dpm_level = level;

	return ret;
}

static int polaris10_get_power_state_size(struct pp_hwmgr *hwmgr)
{
	return sizeof(struct polaris10_power_state);
}


static int polaris10_apply_state_adjust_rules(struct pp_hwmgr *hwmgr,
				struct pp_power_state *request_ps,
			const struct pp_power_state *current_ps)
{

	struct polaris10_power_state *polaris10_ps =
				cast_phw_polaris10_power_state(&request_ps->hardware);
	uint32_t sclk;
	uint32_t mclk;
	struct PP_Clocks minimum_clocks = {0};
	bool disable_mclk_switching;
	bool disable_mclk_switching_for_frame_lock;
	struct cgs_display_info info = {0};
	const struct phm_clock_and_voltage_limits *max_limits;
	uint32_t i;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	int32_t count;
	int32_t stable_pstate_sclk = 0, stable_pstate_mclk = 0;

	data->battery_state = (PP_StateUILabel_Battery ==
			request_ps->classification.ui_label);

	PP_ASSERT_WITH_CODE(polaris10_ps->performance_level_count == 2,
				 "VI should always have 2 performance levels",
				);

	max_limits = (PP_PowerSource_AC == hwmgr->power_source) ?
			&(hwmgr->dyn_state.max_clock_voltage_on_ac) :
			&(hwmgr->dyn_state.max_clock_voltage_on_dc);

	/* Cap clock DPM tables at DC MAX if it is in DC. */
	if (PP_PowerSource_DC == hwmgr->power_source) {
		for (i = 0; i < polaris10_ps->performance_level_count; i++) {
			if (polaris10_ps->performance_levels[i].memory_clock > max_limits->mclk)
				polaris10_ps->performance_levels[i].memory_clock = max_limits->mclk;
			if (polaris10_ps->performance_levels[i].engine_clock > max_limits->sclk)
				polaris10_ps->performance_levels[i].engine_clock = max_limits->sclk;
		}
	}

	polaris10_ps->vce_clks.evclk = hwmgr->vce_arbiter.evclk;
	polaris10_ps->vce_clks.ecclk = hwmgr->vce_arbiter.ecclk;

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

	polaris10_ps->sclk_threshold = hwmgr->gfx_arbiter.sclk_threshold;

	if (0 != hwmgr->gfx_arbiter.sclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.sclk_over_drive <=
				hwmgr->platform_descriptor.overdriveLimit.engineClock),
				"Overdrive sclk exceeds limit",
				hwmgr->gfx_arbiter.sclk_over_drive =
						hwmgr->platform_descriptor.overdriveLimit.engineClock);

		if (hwmgr->gfx_arbiter.sclk_over_drive >= hwmgr->gfx_arbiter.sclk)
			polaris10_ps->performance_levels[1].engine_clock =
					hwmgr->gfx_arbiter.sclk_over_drive;
	}

	if (0 != hwmgr->gfx_arbiter.mclk_over_drive) {
		PP_ASSERT_WITH_CODE((hwmgr->gfx_arbiter.mclk_over_drive <=
				hwmgr->platform_descriptor.overdriveLimit.memoryClock),
				"Overdrive mclk exceeds limit",
				hwmgr->gfx_arbiter.mclk_over_drive =
						hwmgr->platform_descriptor.overdriveLimit.memoryClock);

		if (hwmgr->gfx_arbiter.mclk_over_drive >= hwmgr->gfx_arbiter.mclk)
			polaris10_ps->performance_levels[1].memory_clock =
					hwmgr->gfx_arbiter.mclk_over_drive;
	}

	disable_mclk_switching_for_frame_lock = phm_cap_enabled(
				    hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_DisableMclkSwitchingForFrameLock);

	disable_mclk_switching = (1 < info.display_count) ||
				    disable_mclk_switching_for_frame_lock;

	sclk = polaris10_ps->performance_levels[0].engine_clock;
	mclk = polaris10_ps->performance_levels[0].memory_clock;

	if (disable_mclk_switching)
		mclk = polaris10_ps->performance_levels
		[polaris10_ps->performance_level_count - 1].memory_clock;

	if (sclk < minimum_clocks.engineClock)
		sclk = (minimum_clocks.engineClock > max_limits->sclk) ?
				max_limits->sclk : minimum_clocks.engineClock;

	if (mclk < minimum_clocks.memoryClock)
		mclk = (minimum_clocks.memoryClock > max_limits->mclk) ?
				max_limits->mclk : minimum_clocks.memoryClock;

	polaris10_ps->performance_levels[0].engine_clock = sclk;
	polaris10_ps->performance_levels[0].memory_clock = mclk;

	polaris10_ps->performance_levels[1].engine_clock =
		(polaris10_ps->performance_levels[1].engine_clock >=
				polaris10_ps->performance_levels[0].engine_clock) ?
						polaris10_ps->performance_levels[1].engine_clock :
						polaris10_ps->performance_levels[0].engine_clock;

	if (disable_mclk_switching) {
		if (mclk < polaris10_ps->performance_levels[1].memory_clock)
			mclk = polaris10_ps->performance_levels[1].memory_clock;

		polaris10_ps->performance_levels[0].memory_clock = mclk;
		polaris10_ps->performance_levels[1].memory_clock = mclk;
	} else {
		if (polaris10_ps->performance_levels[1].memory_clock <
				polaris10_ps->performance_levels[0].memory_clock)
			polaris10_ps->performance_levels[1].memory_clock =
					polaris10_ps->performance_levels[0].memory_clock;
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StablePState)) {
		for (i = 0; i < polaris10_ps->performance_level_count; i++) {
			polaris10_ps->performance_levels[i].engine_clock = stable_pstate_sclk;
			polaris10_ps->performance_levels[i].memory_clock = stable_pstate_mclk;
			polaris10_ps->performance_levels[i].pcie_gen = data->pcie_gen_performance.max;
			polaris10_ps->performance_levels[i].pcie_lane = data->pcie_gen_performance.max;
		}
	}
	return 0;
}


static int polaris10_dpm_get_mclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct polaris10_power_state  *polaris10_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	polaris10_ps = cast_phw_polaris10_power_state(&ps->hardware);

	if (low)
		return polaris10_ps->performance_levels[0].memory_clock;
	else
		return polaris10_ps->performance_levels
				[polaris10_ps->performance_level_count-1].memory_clock;
}

static int polaris10_dpm_get_sclk(struct pp_hwmgr *hwmgr, bool low)
{
	struct pp_power_state  *ps;
	struct polaris10_power_state  *polaris10_ps;

	if (hwmgr == NULL)
		return -EINVAL;

	ps = hwmgr->request_ps;

	if (ps == NULL)
		return -EINVAL;

	polaris10_ps = cast_phw_polaris10_power_state(&ps->hardware);

	if (low)
		return polaris10_ps->performance_levels[0].engine_clock;
	else
		return polaris10_ps->performance_levels
				[polaris10_ps->performance_level_count-1].engine_clock;
}

static int polaris10_dpm_patch_boot_state(struct pp_hwmgr *hwmgr,
					struct pp_hw_power_state *hw_ps)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_power_state *ps = (struct polaris10_power_state *)hw_ps;
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
			phm_get_current_pcie_speed(hwmgr);

	data->vbios_boot_state.pcie_lane_bootup_value =
			(uint16_t)phm_get_current_pcie_lane_number(hwmgr);

	/* set boot power state */
	ps->performance_levels[0].memory_clock = data->vbios_boot_state.mclk_bootup_value;
	ps->performance_levels[0].engine_clock = data->vbios_boot_state.sclk_bootup_value;
	ps->performance_levels[0].pcie_gen = data->vbios_boot_state.pcie_gen_bootup_value;
	ps->performance_levels[0].pcie_lane = data->vbios_boot_state.pcie_lane_bootup_value;

	return 0;
}

static int polaris10_get_pp_table_entry_callback_func(struct pp_hwmgr *hwmgr,
		void *state, struct pp_power_state *power_state,
		void *pp_table, uint32_t classification_flag)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_power_state  *polaris10_power_state =
			(struct polaris10_power_state *)(&(power_state->hardware));
	struct polaris10_performance_level *performance_level;
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

	performance_level = &(polaris10_power_state->performance_levels
			[polaris10_power_state->performance_level_count++]);

	PP_ASSERT_WITH_CODE(
			(polaris10_power_state->performance_level_count < SMU74_MAX_LEVELS_GRAPHICS),
			"Performance levels exceeds SMC limit!",
			return -1);

	PP_ASSERT_WITH_CODE(
			(polaris10_power_state->performance_level_count <=
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

	performance_level = &(polaris10_power_state->performance_levels
			[polaris10_power_state->performance_level_count++]);
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

static int polaris10_get_pp_table_entry(struct pp_hwmgr *hwmgr,
		unsigned long entry_index, struct pp_power_state *state)
{
	int result;
	struct polaris10_power_state *ps;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_ppt_v1_clock_voltage_dependency_table *dep_mclk_table =
			table_info->vdd_dep_on_mclk;

	state->hardware.magic = PHM_VIslands_Magic;

	ps = (struct polaris10_power_state *)(&state->hardware);

	result = tonga_get_powerplay_table_entry(hwmgr, entry_index, state,
			polaris10_get_pp_table_entry_callback_func);

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

static void
polaris10_print_current_perforce_level(struct pp_hwmgr *hwmgr, struct seq_file *m)
{
	uint32_t sclk, mclk, activity_percent;
	uint32_t offset;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_API_GetSclkFrequency);

	sclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);

	smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_API_GetMclkFrequency);

	mclk = cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);
	seq_printf(m, "\n [  mclk  ]: %u MHz\n\n [  sclk  ]: %u MHz\n",
			mclk / 100, sclk / 100);

	offset = data->soft_regs_start + offsetof(SMU74_SoftRegisters, AverageGraphicsActivity);
	activity_percent = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, offset);
	activity_percent += 0x80;
	activity_percent >>= 8;

	seq_printf(m, "\n [GPU load]: %u%%\n\n", activity_percent > 100 ? 100 : activity_percent);

	seq_printf(m, "uvd    %sabled\n", data->uvd_power_gated ? "dis" : "en");

	seq_printf(m, "vce    %sabled\n", data->vce_power_gated ? "dis" : "en");
}

static int polaris10_find_dpm_states_clocks_in_dpm_table(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct polaris10_power_state *polaris10_ps =
			cast_const_phw_polaris10_power_state(states->pnew_state);
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_single_dpm_table *sclk_table = &(data->dpm_table.sclk_table);
	uint32_t sclk = polaris10_ps->performance_levels
			[polaris10_ps->performance_level_count - 1].engine_clock;
	struct polaris10_single_dpm_table *mclk_table = &(data->dpm_table.mclk_table);
	uint32_t mclk = polaris10_ps->performance_levels
			[polaris10_ps->performance_level_count - 1].memory_clock;
	struct PP_Clocks min_clocks = {0};
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
	/* TODO: Check SCLK in DAL's minimum clocks
	 * in case DeepSleep divider update is required.
	 */
		if (data->display_timing.min_clock_in_sr != min_clocks.engineClockInSR &&
			(min_clocks.engineClockInSR >= POLARIS10_MINIMUM_ENGINE_CLOCK ||
				data->display_timing.min_clock_in_sr >= POLARIS10_MINIMUM_ENGINE_CLOCK))
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

static uint16_t polaris10_get_maximum_link_speed(struct pp_hwmgr *hwmgr,
		const struct polaris10_power_state *polaris10_ps)
{
	uint32_t i;
	uint32_t sclk, max_sclk = 0;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_dpm_table *dpm_table = &data->dpm_table;

	for (i = 0; i < polaris10_ps->performance_level_count; i++) {
		sclk = polaris10_ps->performance_levels[i].engine_clock;
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

static int polaris10_request_link_speed_change_before_state_change(
		struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const struct polaris10_power_state *polaris10_nps =
			cast_const_phw_polaris10_power_state(states->pnew_state);
	const struct polaris10_power_state *polaris10_cps =
			cast_const_phw_polaris10_power_state(states->pcurrent_state);

	uint16_t target_link_speed = polaris10_get_maximum_link_speed(hwmgr, polaris10_nps);
	uint16_t current_link_speed;

	if (data->force_pcie_gen == PP_PCIEGenInvalid)
		current_link_speed = polaris10_get_maximum_link_speed(hwmgr, polaris10_cps);
	else
		current_link_speed = data->force_pcie_gen;

	data->force_pcie_gen = PP_PCIEGenInvalid;
	data->pspp_notify_required = false;

	if (target_link_speed > current_link_speed) {
		switch (target_link_speed) {
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
			data->force_pcie_gen = phm_get_current_pcie_speed(hwmgr);
			break;
		}
	} else {
		if (target_link_speed < current_link_speed)
			data->pspp_notify_required = true;
	}

	return 0;
}

static int polaris10_freeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
			(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {
		PP_ASSERT_WITH_CODE(true == polaris10_is_dpm_running(hwmgr),
				"Trying to freeze SCLK DPM when DPM is disabled",
				);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SCLKDPM_FreezeLevel),
				"Failed to freeze SCLK DPM during FreezeSclkMclkDPM Function!",
				return -1);
	}

	if ((0 == data->mclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		 DPMTABLE_OD_UPDATE_MCLK)) {
		PP_ASSERT_WITH_CODE(true == polaris10_is_dpm_running(hwmgr),
				"Trying to freeze MCLK DPM when DPM is disabled",
				);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_MCLKDPM_FreezeLevel),
				"Failed to freeze MCLK DPM during FreezeSclkMclkDPM Function!",
				return -1);
	}

	return 0;
}

static int polaris10_populate_and_upload_sclk_mclk_dpm_levels(
		struct pp_hwmgr *hwmgr, const void *input)
{
	int result = 0;
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	const struct polaris10_power_state *polaris10_ps =
			cast_const_phw_polaris10_power_state(states->pnew_state);
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t sclk = polaris10_ps->performance_levels
			[polaris10_ps->performance_level_count - 1].engine_clock;
	uint32_t mclk = polaris10_ps->performance_levels
			[polaris10_ps->performance_level_count - 1].memory_clock;
	struct polaris10_dpm_table *dpm_table = &data->dpm_table;

	struct polaris10_dpm_table *golden_dpm_table = &data->golden_dpm_table;
	uint32_t dpm_count, clock_percent;
	uint32_t i;

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_SCLK) {
		dpm_table->sclk_table.dpm_levels
		[dpm_table->sclk_table.count - 1].value = sclk;

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinACSupport) ||
		    phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinDCSupport)) {
		/* Need to do calculation based on the golden DPM table
		 * as the Heatmap GPU Clock axis is also based on the default values
		 */
			PP_ASSERT_WITH_CODE(
				(golden_dpm_table->sclk_table.dpm_levels
						[golden_dpm_table->sclk_table.count - 1].value != 0),
				"Divide by 0!",
				return -1);
			dpm_count = dpm_table->sclk_table.count < 2 ? 0 : dpm_table->sclk_table.count - 2;

			for (i = dpm_count; i > 1; i--) {
				if (sclk > golden_dpm_table->sclk_table.dpm_levels[golden_dpm_table->sclk_table.count-1].value) {
					clock_percent =
					      ((sclk
						- golden_dpm_table->sclk_table.dpm_levels[golden_dpm_table->sclk_table.count-1].value
						) * 100)
						/ golden_dpm_table->sclk_table.dpm_levels[golden_dpm_table->sclk_table.count-1].value;

					dpm_table->sclk_table.dpm_levels[i].value =
							golden_dpm_table->sclk_table.dpm_levels[i].value +
							(golden_dpm_table->sclk_table.dpm_levels[i].value *
								clock_percent)/100;

				} else if (golden_dpm_table->sclk_table.dpm_levels[dpm_table->sclk_table.count-1].value > sclk) {
					clock_percent =
						((golden_dpm_table->sclk_table.dpm_levels[golden_dpm_table->sclk_table.count - 1].value
						- sclk) * 100)
						/ golden_dpm_table->sclk_table.dpm_levels[golden_dpm_table->sclk_table.count-1].value;

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

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinACSupport) ||
		    phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_OD6PlusinDCSupport)) {

			PP_ASSERT_WITH_CODE(
					(golden_dpm_table->mclk_table.dpm_levels
						[golden_dpm_table->mclk_table.count-1].value != 0),
					"Divide by 0!",
					return -1);
			dpm_count = dpm_table->mclk_table.count < 2 ? 0 : dpm_table->mclk_table.count - 2;
			for (i = dpm_count; i > 1; i--) {
				if (golden_dpm_table->mclk_table.dpm_levels[golden_dpm_table->mclk_table.count-1].value < mclk) {
					clock_percent = ((mclk -
					golden_dpm_table->mclk_table.dpm_levels[golden_dpm_table->mclk_table.count-1].value) * 100)
					/ golden_dpm_table->mclk_table.dpm_levels[golden_dpm_table->mclk_table.count-1].value;

					dpm_table->mclk_table.dpm_levels[i].value =
							golden_dpm_table->mclk_table.dpm_levels[i].value +
							(golden_dpm_table->mclk_table.dpm_levels[i].value *
							clock_percent) / 100;

				} else if (golden_dpm_table->mclk_table.dpm_levels[dpm_table->mclk_table.count-1].value > mclk) {
					clock_percent = (
					 (golden_dpm_table->mclk_table.dpm_levels[golden_dpm_table->mclk_table.count-1].value - mclk)
					* 100)
					/ golden_dpm_table->mclk_table.dpm_levels[golden_dpm_table->mclk_table.count-1].value;

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
		result = polaris10_populate_all_graphic_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate SCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	if (data->need_update_smu7_dpm_table &
			(DPMTABLE_OD_UPDATE_MCLK + DPMTABLE_UPDATE_MCLK)) {
		/*populate MCLK dpm table to SMU7 */
		result = polaris10_populate_all_memory_levels(hwmgr);
		PP_ASSERT_WITH_CODE((0 == result),
				"Failed to populate MCLK during PopulateNewDPMClocksStates Function!",
				return result);
	}

	return result;
}

static int polaris10_trim_single_dpm_states(struct pp_hwmgr *hwmgr,
			  struct polaris10_single_dpm_table *dpm_table,
			uint32_t low_limit, uint32_t high_limit)
{
	uint32_t i;

	for (i = 0; i < dpm_table->count; i++) {
		if ((dpm_table->dpm_levels[i].value < low_limit)
		|| (dpm_table->dpm_levels[i].value > high_limit))
			dpm_table->dpm_levels[i].enabled = false;
		else
			dpm_table->dpm_levels[i].enabled = true;
	}

	return 0;
}

static int polaris10_trim_dpm_states(struct pp_hwmgr *hwmgr,
		const struct polaris10_power_state *polaris10_ps)
{
	int result = 0;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t high_limit_count;

	PP_ASSERT_WITH_CODE((polaris10_ps->performance_level_count >= 1),
			"power state did not have any performance level",
			return -1);

	high_limit_count = (1 == polaris10_ps->performance_level_count) ? 0 : 1;

	polaris10_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.sclk_table),
			polaris10_ps->performance_levels[0].engine_clock,
			polaris10_ps->performance_levels[high_limit_count].engine_clock);

	polaris10_trim_single_dpm_states(hwmgr,
			&(data->dpm_table.mclk_table),
			polaris10_ps->performance_levels[0].memory_clock,
			polaris10_ps->performance_levels[high_limit_count].memory_clock);

	return result;
}

static int polaris10_generate_dpm_level_enable_mask(
		struct pp_hwmgr *hwmgr, const void *input)
{
	int result;
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const struct polaris10_power_state *polaris10_ps =
			cast_const_phw_polaris10_power_state(states->pnew_state);

	result = polaris10_trim_dpm_states(hwmgr, polaris10_ps);
	if (result)
		return result;

	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
			phm_get_dpm_level_enable_mask_value(&data->dpm_table.sclk_table);
	data->dpm_level_enable_mask.mclk_dpm_enable_mask =
			phm_get_dpm_level_enable_mask_value(&data->dpm_table.mclk_table);
	data->dpm_level_enable_mask.pcie_dpm_enable_mask =
			phm_get_dpm_level_enable_mask_value(&data->dpm_table.pcie_speed_table);

	return 0;
}

int polaris10_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable ?
			PPSMC_MSG_UVDDPM_Enable :
			PPSMC_MSG_UVDDPM_Disable);
}

int polaris10_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable?
			PPSMC_MSG_VCEDPM_Enable :
			PPSMC_MSG_VCEDPM_Disable);
}

int polaris10_enable_disable_samu_dpm(struct pp_hwmgr *hwmgr, bool enable)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, enable?
			PPSMC_MSG_SAMUDPM_Enable :
			PPSMC_MSG_SAMUDPM_Disable);
}

int polaris10_update_uvd_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (!bgate) {
		data->smc_state_table.UvdBootLevel = 0;
		if (table_info->mm_dep_table->count > 0)
			data->smc_state_table.UvdBootLevel =
					(uint8_t) (table_info->mm_dep_table->count - 1);
		mm_boot_level_offset = data->dpm_table_start +
				offsetof(SMU74_Discrete_DpmTable, UvdBootLevel);
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

	return polaris10_enable_disable_uvd_dpm(hwmgr, !bgate);
}

static int polaris10_update_vce_dpm(struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const struct polaris10_power_state *polaris10_nps =
			cast_const_phw_polaris10_power_state(states->pnew_state);
	const struct polaris10_power_state *polaris10_cps =
			cast_const_phw_polaris10_power_state(states->pcurrent_state);

	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (polaris10_nps->vce_clks.evclk > 0 &&
	(polaris10_cps == NULL || polaris10_cps->vce_clks.evclk == 0)) {

		data->smc_state_table.VceBootLevel =
				(uint8_t) (table_info->mm_dep_table->count - 1);

		mm_boot_level_offset = data->dpm_table_start +
				offsetof(SMU74_Discrete_DpmTable, VceBootLevel);
		mm_boot_level_offset /= 4;
		mm_boot_level_offset *= 4;
		mm_boot_level_value = cgs_read_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset);
		mm_boot_level_value &= 0xFF00FFFF;
		mm_boot_level_value |= data->smc_state_table.VceBootLevel << 16;
		cgs_write_ind_register(hwmgr->device,
				CGS_IND_REG__SMC, mm_boot_level_offset, mm_boot_level_value);

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_StablePState)) {
			smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
					PPSMC_MSG_VCEDPM_SetEnabledMask,
					(uint32_t)1 << data->smc_state_table.VceBootLevel);

			polaris10_enable_disable_vce_dpm(hwmgr, true);
		} else if (polaris10_nps->vce_clks.evclk == 0 &&
				polaris10_cps != NULL &&
				polaris10_cps->vce_clks.evclk > 0)
			polaris10_enable_disable_vce_dpm(hwmgr, false);
	}

	return 0;
}

int polaris10_update_samu_dpm(struct pp_hwmgr *hwmgr, bool bgate)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	uint32_t mm_boot_level_offset, mm_boot_level_value;
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);

	if (!bgate) {
		data->smc_state_table.SamuBootLevel =
				(uint8_t) (table_info->mm_dep_table->count - 1);
		mm_boot_level_offset = data->dpm_table_start +
				offsetof(SMU74_Discrete_DpmTable, SamuBootLevel);
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

	return polaris10_enable_disable_samu_dpm(hwmgr, !bgate);
}

static int polaris10_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

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

		result = polaris10_copy_bytes_to_smc(
				hwmgr->smumgr,
				data->dpm_table_start +
				offsetof(SMU74_Discrete_DpmTable,
					LowSclkInterruptThreshold),
				(uint8_t *)&low_sclk_interrupt_threshold,
				sizeof(uint32_t),
				data->sram_end);
	}

	return result;
}

static int polaris10_program_mem_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_OD_UPDATE_MCLK))
		return polaris10_program_memory_timing_parameters(hwmgr);

	return 0;
}

static int polaris10_unfreeze_sclk_mclk_dpm(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (0 == data->need_update_smu7_dpm_table)
		return 0;

	if ((0 == data->sclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK + DPMTABLE_UPDATE_SCLK))) {

		PP_ASSERT_WITH_CODE(true == polaris10_is_dpm_running(hwmgr),
				"Trying to Unfreeze SCLK DPM when DPM is disabled",
				);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SCLKDPM_UnfreezeLevel),
			"Failed to unfreeze SCLK DPM during UnFreezeSclkMclkDPM Function!",
			return -1);
	}

	if ((0 == data->mclk_dpm_key_disabled) &&
		(data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK)) {

		PP_ASSERT_WITH_CODE(true == polaris10_is_dpm_running(hwmgr),
				"Trying to Unfreeze MCLK DPM when DPM is disabled",
				);
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(hwmgr->smumgr,
				PPSMC_MSG_SCLKDPM_UnfreezeLevel),
		    "Failed to unfreeze MCLK DPM during UnFreezeSclkMclkDPM Function!",
		    return -1);
	}

	data->need_update_smu7_dpm_table = 0;

	return 0;
}

static int polaris10_notify_link_speed_change_after_state_change(
		struct pp_hwmgr *hwmgr, const void *input)
{
	const struct phm_set_power_state_input *states =
			(const struct phm_set_power_state_input *)input;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	const struct polaris10_power_state *polaris10_ps =
			cast_const_phw_polaris10_power_state(states->pnew_state);
	uint16_t target_link_speed = polaris10_get_maximum_link_speed(hwmgr, polaris10_ps);
	uint8_t  request;

	if (data->pspp_notify_required) {
		if (target_link_speed == PP_PCIEGen3)
			request = PCIE_PERF_REQ_GEN3;
		else if (target_link_speed == PP_PCIEGen2)
			request = PCIE_PERF_REQ_GEN2;
		else
			request = PCIE_PERF_REQ_GEN1;

		if (request == PCIE_PERF_REQ_GEN1 &&
				phm_get_current_pcie_speed(hwmgr) > 0)
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

static int polaris10_set_power_state_tasks(struct pp_hwmgr *hwmgr, const void *input)
{
	int tmp_result, result = 0;
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	tmp_result = polaris10_find_dpm_states_clocks_in_dpm_table(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to find DPM states clocks in DPM table!",
			result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result =
			polaris10_request_link_speed_change_before_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
				"Failed to request link speed change before state change!",
				result = tmp_result);
	}

	tmp_result = polaris10_freeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to freeze SCLK MCLK DPM!", result = tmp_result);

	tmp_result = polaris10_populate_and_upload_sclk_mclk_dpm_levels(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to populate and upload SCLK MCLK DPM levels!",
			result = tmp_result);

	tmp_result = polaris10_generate_dpm_level_enable_mask(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to generate DPM level enabled mask!",
			result = tmp_result);

	tmp_result = polaris10_update_vce_dpm(hwmgr, input);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to update VCE DPM!",
			result = tmp_result);

	tmp_result = polaris10_update_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to update SCLK threshold!",
			result = tmp_result);

	tmp_result = polaris10_program_mem_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to program memory timing parameters!",
			result = tmp_result);

	tmp_result = polaris10_unfreeze_sclk_mclk_dpm(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to unfreeze SCLK MCLK DPM!",
			result = tmp_result);

	tmp_result = polaris10_upload_dpm_level_enable_mask(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to upload DPM level enabled mask!",
			result = tmp_result);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PCIEPerformanceRequest)) {
		tmp_result =
			polaris10_notify_link_speed_change_after_state_change(hwmgr, input);
		PP_ASSERT_WITH_CODE((0 == tmp_result),
				"Failed to notify link speed change after state change!",
				result = tmp_result);
	}
	data->apply_optimized_settings = false;
	return result;
}

static int polaris10_set_max_fan_pwm_output(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_pwm)
{
	hwmgr->thermal_controller.
	advanceFanControlParameters.usMaxFanPWM = us_max_fan_pwm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_SetFanPwmMax, us_max_fan_pwm);
}

int polaris10_notify_smc_display_change(struct pp_hwmgr *hwmgr, bool has_display)
{
	PPSMC_Msg msg = has_display ? (PPSMC_Msg)PPSMC_HasDisplay : (PPSMC_Msg)PPSMC_NoDisplay;

	return (smum_send_msg_to_smc(hwmgr->smumgr, msg) == 0) ?  0 : -1;
}

int polaris10_notify_smc_display_config_after_ps_adjustment(struct pp_hwmgr *hwmgr)
{
	uint32_t num_active_displays = 0;
	struct cgs_display_info info = {0};
	info.mode_info = NULL;

	cgs_get_active_displays_info(hwmgr->device, &info);

	num_active_displays = info.display_count;

	if (num_active_displays > 1)  /* to do && (pHwMgr->pPECI->displayConfiguration.bMultiMonitorInSync != TRUE)) */
		polaris10_notify_smc_display_change(hwmgr, false);
	else
		polaris10_notify_smc_display_change(hwmgr, true);

	return 0;
}

/**
* Programs the display gap
*
* @param    hwmgr  the address of the powerplay hardware manager.
* @return   always OK
*/
int polaris10_program_display_gap(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
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

	display_gap = PHM_SET_FIELD(display_gap, CG_DISPLAY_GAP_CNTL, DISP_GAP, (num_active_displays > 0) ? DISPLAY_GAP_VBLANK_OR_WM : DISPLAY_GAP_IGNORE);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_DISPLAY_GAP_CNTL, display_gap);

	ref_clock = mode_info.ref_clock;
	refresh_rate = mode_info.refresh_rate;

	if (0 == refresh_rate)
		refresh_rate = 60;

	frame_time_in_us = 1000000 / refresh_rate;

	pre_vbi_time_in_us = frame_time_in_us - 200 - mode_info.vblank_time_us;
	display_gap2 = pre_vbi_time_in_us * (ref_clock / 100);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixCG_DISPLAY_GAP_CNTL2, display_gap2);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, data->soft_regs_start + offsetof(SMU74_SoftRegisters, PreVBlankGap), 0x64);

	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC, data->soft_regs_start + offsetof(SMU74_SoftRegisters, VBlankTimeout), (frame_time_in_us - pre_vbi_time_in_us));

	polaris10_notify_smc_display_change(hwmgr, num_active_displays != 0);

	return 0;
}


int polaris10_display_configuration_changed_task(struct pp_hwmgr *hwmgr)
{
	return polaris10_program_display_gap(hwmgr);
}

/**
*  Set maximum target operating fan output RPM
*
* @param    hwmgr:  the address of the powerplay hardware manager.
* @param    usMaxFanRpm:  max operating fan RPM value.
* @return   The response that came from the SMC.
*/
static int polaris10_set_max_fan_rpm_output(struct pp_hwmgr *hwmgr, uint16_t us_max_fan_rpm)
{
	hwmgr->thermal_controller.
	advanceFanControlParameters.usMaxFanRPM = us_max_fan_rpm;

	if (phm_is_hw_access_blocked(hwmgr))
		return 0;

	return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_SetFanRpmMax, us_max_fan_rpm);
}

int polaris10_register_internal_thermal_interrupt(struct pp_hwmgr *hwmgr,
					const void *thermal_interrupt_info)
{
	return 0;
}

bool polaris10_check_smc_update_required_for_display_configuration(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	bool is_update_required = false;
	struct cgs_display_info info = {0, 0, NULL};

	cgs_get_active_displays_info(hwmgr->device, &info);

	if (data->display_timing.num_existing_displays != info.display_count)
		is_update_required = true;
/* TO DO NEED TO GET DEEP SLEEP CLOCK FROM DAL
	if (phm_cap_enabled(hwmgr->hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_SclkDeepSleep)) {
		cgs_get_min_clock_settings(hwmgr->device, &min_clocks);
		if (min_clocks.engineClockInSR != data->display_timing.minClockInSR &&
			(min_clocks.engineClockInSR >= POLARIS10_MINIMUM_ENGINE_CLOCK ||
				data->display_timing.minClockInSR >= POLARIS10_MINIMUM_ENGINE_CLOCK))
			is_update_required = true;
*/
	return is_update_required;
}

static inline bool polaris10_are_power_levels_equal(const struct polaris10_performance_level *pl1,
							   const struct polaris10_performance_level *pl2)
{
	return ((pl1->memory_clock == pl2->memory_clock) &&
		  (pl1->engine_clock == pl2->engine_clock) &&
		  (pl1->pcie_gen == pl2->pcie_gen) &&
		  (pl1->pcie_lane == pl2->pcie_lane));
}

int polaris10_check_states_equal(struct pp_hwmgr *hwmgr, const struct pp_hw_power_state *pstate1, const struct pp_hw_power_state *pstate2, bool *equal)
{
	const struct polaris10_power_state *psa = cast_const_phw_polaris10_power_state(pstate1);
	const struct polaris10_power_state *psb = cast_const_phw_polaris10_power_state(pstate2);
	int i;

	if (pstate1 == NULL || pstate2 == NULL || equal == NULL)
		return -EINVAL;

	/* If the two states don't even have the same number of performance levels they cannot be the same state. */
	if (psa->performance_level_count != psb->performance_level_count) {
		*equal = false;
		return 0;
	}

	for (i = 0; i < psa->performance_level_count; i++) {
		if (!polaris10_are_power_levels_equal(&(psa->performance_levels[i]), &(psb->performance_levels[i]))) {
			/* If we have found even one performance level pair that is different the states are different. */
			*equal = false;
			return 0;
		}
	}

	/* If all performance levels are the same try to use the UVD clocks to break the tie.*/
	*equal = ((psa->uvd_clks.vclk == psb->uvd_clks.vclk) && (psa->uvd_clks.dclk == psb->uvd_clks.dclk));
	*equal &= ((psa->vce_clks.evclk == psb->vce_clks.evclk) && (psa->vce_clks.ecclk == psb->vce_clks.ecclk));
	*equal &= (psa->sclk_threshold == psb->sclk_threshold);

	return 0;
}

int polaris10_upload_mc_firmware(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	uint32_t vbios_version;

	/*  Read MC indirect register offset 0x9F bits [3:0] to see if VBIOS has already loaded a full version of MC ucode or not.*/

	phm_get_mc_microcode_version(hwmgr);
	vbios_version = hwmgr->microcode_version_info.MC & 0xf;
	/*  Full version of MC ucode has already been loaded. */
	if (vbios_version == 0) {
		data->need_long_memory_training = false;
		return 0;
	}

	data->need_long_memory_training = true;

/*
 *	PPMCME_FirmwareDescriptorEntry *pfd = NULL;
	pfd = &tonga_mcmeFirmware;
	if (0 == PHM_READ_FIELD(hwmgr->device, MC_SEQ_SUP_CNTL, RUN))
		polaris10_load_mc_microcode(hwmgr, pfd->dpmThreshold,
					pfd->cfgArray, pfd->cfgSize, pfd->ioDebugArray,
					pfd->ioDebugSize, pfd->ucodeArray, pfd->ucodeSize);
*/
	return 0;
}

/**
 * Read clock related registers.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
static int polaris10_read_clock_registers(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	data->clock_registers.vCG_SPLL_FUNC_CNTL = cgs_read_ind_register(hwmgr->device,
						CGS_IND_REG__SMC, ixCG_SPLL_FUNC_CNTL)
						& CG_SPLL_FUNC_CNTL__SPLL_BYPASS_EN_MASK;

	data->clock_registers.vCG_SPLL_FUNC_CNTL_2 = cgs_read_ind_register(hwmgr->device,
						CGS_IND_REG__SMC, ixCG_SPLL_FUNC_CNTL_2)
						& CG_SPLL_FUNC_CNTL_2__SCLK_MUX_SEL_MASK;

	data->clock_registers.vCG_SPLL_FUNC_CNTL_4 = cgs_read_ind_register(hwmgr->device,
						CGS_IND_REG__SMC, ixCG_SPLL_FUNC_CNTL_4)
						& CG_SPLL_FUNC_CNTL_4__SPLL_SPARE_MASK;

	return 0;
}

/**
 * Find out if memory is GDDR5.
 *
 * @param    hwmgr  the address of the powerplay hardware manager.
 * @return   always 0
 */
static int polaris10_get_memory_type(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
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
static int polaris10_enable_acpi_power_management(struct pp_hwmgr *hwmgr)
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
static int polaris10_init_power_gate_state(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	data->uvd_power_gated = false;
	data->vce_power_gated = false;
	data->samu_power_gated = false;

	return 0;
}

static int polaris10_init_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	data->low_sclk_interrupt_threshold = 0;

	return 0;
}

int polaris10_setup_asic_task(struct pp_hwmgr *hwmgr)
{
	int tmp_result, result = 0;

	polaris10_upload_mc_firmware(hwmgr);

	tmp_result = polaris10_read_clock_registers(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to read clock registers!", result = tmp_result);

	tmp_result = polaris10_get_memory_type(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to get memory type!", result = tmp_result);

	tmp_result = polaris10_enable_acpi_power_management(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to enable ACPI power management!", result = tmp_result);

	tmp_result = polaris10_init_power_gate_state(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to init power gate state!", result = tmp_result);

	tmp_result = phm_get_mc_microcode_version(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to get MC microcode version!", result = tmp_result);

	tmp_result = polaris10_init_sclk_threshold(hwmgr);
	PP_ASSERT_WITH_CODE((0 == tmp_result),
			"Failed to init sclk threshold!", result = tmp_result);

	return result;
}

static int polaris10_get_pp_table(struct pp_hwmgr *hwmgr, char **table)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

	if (!data->soft_pp_table) {
		data->soft_pp_table = kzalloc(hwmgr->soft_pp_table_size, GFP_KERNEL);
		if (!data->soft_pp_table)
			return -ENOMEM;
		memcpy(data->soft_pp_table, hwmgr->soft_pp_table,
				hwmgr->soft_pp_table_size);
	}

	*table = (char *)&data->soft_pp_table;

	return hwmgr->soft_pp_table_size;
}

static int polaris10_set_pp_table(struct pp_hwmgr *hwmgr, const char *buf, size_t size)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

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

static int polaris10_force_clock_level(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, uint32_t mask)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);

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

static uint16_t polaris10_get_current_pcie_speed(struct pp_hwmgr *hwmgr)
{
	uint32_t speedCntl = 0;

	/* mmPCIE_PORT_INDEX rename as mmPCIE_INDEX */
	speedCntl = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__PCIE,
			ixPCIE_LC_SPEED_CNTL);
	return((uint16_t)PHM_GET_FIELD(speedCntl,
			PCIE_LC_SPEED_CNTL, LC_CURRENT_DATA_RATE));
}

static int polaris10_print_clock_levels(struct pp_hwmgr *hwmgr,
		enum pp_clock_type type, char *buf)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	struct polaris10_single_dpm_table *sclk_table = &(data->dpm_table.sclk_table);
	struct polaris10_single_dpm_table *mclk_table = &(data->dpm_table.mclk_table);
	struct polaris10_single_dpm_table *pcie_table = &(data->dpm_table.pcie_speed_table);
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
		pcie_speed = polaris10_get_current_pcie_speed(hwmgr);
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

static int polaris10_set_fan_control_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	if (mode) {
		/* stop auto-manage */
		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl))
			polaris10_fan_ctrl_stop_smc_fan_control(hwmgr);
		polaris10_fan_ctrl_set_static_mode(hwmgr, mode);
	} else
		/* restart auto-manage */
		polaris10_fan_ctrl_reset_fan_speed_to_default(hwmgr);

	return 0;
}

static int polaris10_get_fan_control_mode(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->fan_ctrl_is_in_default_mode)
		return hwmgr->fan_ctrl_default_mode;
	else
		return PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_FDO_CTRL2, FDO_PWM_MODE);
}

static const struct pp_hwmgr_func polaris10_hwmgr_funcs = {
	.backend_init = &polaris10_hwmgr_backend_init,
	.backend_fini = &polaris10_hwmgr_backend_fini,
	.asic_setup = &polaris10_setup_asic_task,
	.dynamic_state_management_enable = &polaris10_enable_dpm_tasks,
	.apply_state_adjust_rules = polaris10_apply_state_adjust_rules,
	.force_dpm_level = &polaris10_force_dpm_level,
	.power_state_set = polaris10_set_power_state_tasks,
	.get_power_state_size = polaris10_get_power_state_size,
	.get_mclk = polaris10_dpm_get_mclk,
	.get_sclk = polaris10_dpm_get_sclk,
	.patch_boot_state = polaris10_dpm_patch_boot_state,
	.get_pp_table_entry = polaris10_get_pp_table_entry,
	.get_num_of_pp_table_entries = tonga_get_number_of_powerplay_table_entries,
	.print_current_perforce_level = polaris10_print_current_perforce_level,
	.powerdown_uvd = polaris10_phm_powerdown_uvd,
	.powergate_uvd = polaris10_phm_powergate_uvd,
	.powergate_vce = polaris10_phm_powergate_vce,
	.disable_clock_power_gating = polaris10_phm_disable_clock_power_gating,
	.update_clock_gatings = polaris10_phm_update_clock_gatings,
	.notify_smc_display_config_after_ps_adjustment = polaris10_notify_smc_display_config_after_ps_adjustment,
	.display_config_changed = polaris10_display_configuration_changed_task,
	.set_max_fan_pwm_output = polaris10_set_max_fan_pwm_output,
	.set_max_fan_rpm_output = polaris10_set_max_fan_rpm_output,
	.get_temperature = polaris10_thermal_get_temperature,
	.stop_thermal_controller = polaris10_thermal_stop_thermal_controller,
	.get_fan_speed_info = polaris10_fan_ctrl_get_fan_speed_info,
	.get_fan_speed_percent = polaris10_fan_ctrl_get_fan_speed_percent,
	.set_fan_speed_percent = polaris10_fan_ctrl_set_fan_speed_percent,
	.reset_fan_speed_to_default = polaris10_fan_ctrl_reset_fan_speed_to_default,
	.get_fan_speed_rpm = polaris10_fan_ctrl_get_fan_speed_rpm,
	.set_fan_speed_rpm = polaris10_fan_ctrl_set_fan_speed_rpm,
	.uninitialize_thermal_controller = polaris10_thermal_ctrl_uninitialize_thermal_controller,
	.register_internal_thermal_interrupt = polaris10_register_internal_thermal_interrupt,
	.check_smc_update_required_for_display_configuration = polaris10_check_smc_update_required_for_display_configuration,
	.check_states_equal = polaris10_check_states_equal,
	.set_fan_control_mode = polaris10_set_fan_control_mode,
	.get_fan_control_mode = polaris10_get_fan_control_mode,
	.get_pp_table = polaris10_get_pp_table,
	.set_pp_table = polaris10_set_pp_table,
	.force_clock_level = polaris10_force_clock_level,
	.print_clock_levels = polaris10_print_clock_levels,
	.enable_per_cu_power_gating = polaris10_phm_enable_per_cu_power_gating,
};

int polaris10_hwmgr_init(struct pp_hwmgr *hwmgr)
{
	struct polaris10_hwmgr  *data;

	data = kzalloc (sizeof(struct polaris10_hwmgr), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	hwmgr->backend = data;
	hwmgr->hwmgr_func = &polaris10_hwmgr_funcs;
	hwmgr->pptable_func = &tonga_pptable_funcs;
	pp_polaris10_thermal_initialize(hwmgr);

	return 0;
}
