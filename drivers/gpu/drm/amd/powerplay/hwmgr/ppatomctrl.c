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

#include "ppatomctrl.h"
#include "atombios.h"
#include "cgs_common.h"
#include "pp_debug.h"
#define MEM_ID_MASK           0xff000000
#define MEM_ID_SHIFT          24
#define CLOCK_RANGE_MASK      0x00ffffff
#define CLOCK_RANGE_SHIFT     0
#define LOW_NIBBLE_MASK       0xf
#define DATA_EQU_PREV         0
#define DATA_FROM_TABLE       4

union voltage_object_info {
	struct _ATOM_VOLTAGE_OBJECT_INFO v1;
	struct _ATOM_VOLTAGE_OBJECT_INFO_V2 v2;
	struct _ATOM_VOLTAGE_OBJECT_INFO_V3_1 v3;
};

static int atomctrl_retrieve_ac_timing(
		uint8_t index,
		ATOM_INIT_REG_BLOCK *reg_block,
		pp_atomctrl_mc_reg_table *table)
{
	uint32_t i, j;
	uint8_t tmem_id;
	ATOM_MEMORY_SETTING_DATA_BLOCK *reg_data = (ATOM_MEMORY_SETTING_DATA_BLOCK *)
		((uint8_t *)reg_block + (2 * sizeof(uint16_t)) + le16_to_cpu(reg_block->usRegIndexTblSize));

	uint8_t num_ranges = 0;

	while (*(uint32_t *)reg_data != END_OF_REG_DATA_BLOCK &&
			num_ranges < VBIOS_MAX_AC_TIMING_ENTRIES) {
		tmem_id = (uint8_t)((*(uint32_t *)reg_data & MEM_ID_MASK) >> MEM_ID_SHIFT);

		if (index == tmem_id) {
			table->mc_reg_table_entry[num_ranges].mclk_max =
				(uint32_t)((*(uint32_t *)reg_data & CLOCK_RANGE_MASK) >>
						CLOCK_RANGE_SHIFT);

			for (i = 0, j = 1; i < table->last; i++) {
				if ((table->mc_reg_address[i].uc_pre_reg_data &
							LOW_NIBBLE_MASK) == DATA_FROM_TABLE) {
					table->mc_reg_table_entry[num_ranges].mc_data[i] =
						(uint32_t)*((uint32_t *)reg_data + j);
					j++;
				} else if ((table->mc_reg_address[i].uc_pre_reg_data &
							LOW_NIBBLE_MASK) == DATA_EQU_PREV) {
					table->mc_reg_table_entry[num_ranges].mc_data[i] =
						table->mc_reg_table_entry[num_ranges].mc_data[i-1];
				}
			}
			num_ranges++;
		}

		reg_data = (ATOM_MEMORY_SETTING_DATA_BLOCK *)
			((uint8_t *)reg_data + le16_to_cpu(reg_block->usRegDataBlkSize)) ;
	}

	PP_ASSERT_WITH_CODE((*(uint32_t *)reg_data == END_OF_REG_DATA_BLOCK),
			"Invalid VramInfo table.", return -1);
	table->num_entries = num_ranges;

	return 0;
}

/**
 * Get memory clock AC timing registers index from VBIOS table
 * VBIOS set end of memory clock AC timing registers by ucPreRegDataLength bit6 = 1
 * @param    reg_block the address ATOM_INIT_REG_BLOCK
 * @param    table the address of MCRegTable
 * @return   PP_Result_OK
 */
static int atomctrl_set_mc_reg_address_table(
		ATOM_INIT_REG_BLOCK *reg_block,
		pp_atomctrl_mc_reg_table *table)
{
	uint8_t i = 0;
	uint8_t num_entries = (uint8_t)((le16_to_cpu(reg_block->usRegIndexTblSize))
			/ sizeof(ATOM_INIT_REG_INDEX_FORMAT));
	ATOM_INIT_REG_INDEX_FORMAT *format = &reg_block->asRegIndexBuf[0];

	num_entries--;        /* subtract 1 data end mark entry */

	PP_ASSERT_WITH_CODE((num_entries <= VBIOS_MC_REGISTER_ARRAY_SIZE),
			"Invalid VramInfo table.", return -1);

	/* ucPreRegDataLength bit6 = 1 is the end of memory clock AC timing registers */
	while ((!(format->ucPreRegDataLength & ACCESS_PLACEHOLDER)) &&
			(i < num_entries)) {
		table->mc_reg_address[i].s1 =
			(uint16_t)(le16_to_cpu(format->usRegIndex));
		table->mc_reg_address[i].uc_pre_reg_data =
			format->ucPreRegDataLength;

		i++;
		format = (ATOM_INIT_REG_INDEX_FORMAT *)
			((uint8_t *)format + sizeof(ATOM_INIT_REG_INDEX_FORMAT));
	}

	table->last = i;
	return 0;
}


int atomctrl_initialize_mc_reg_table(
		struct pp_hwmgr *hwmgr,
		uint8_t module_index,
		pp_atomctrl_mc_reg_table *table)
{
	ATOM_VRAM_INFO_HEADER_V2_1 *vram_info;
	ATOM_INIT_REG_BLOCK *reg_block;
	int result = 0;
	u8 frev, crev;
	u16 size;

	vram_info = (ATOM_VRAM_INFO_HEADER_V2_1 *)
		cgs_atom_get_data_table(hwmgr->device,
				GetIndexIntoMasterTable(DATA, VRAM_Info), &size, &frev, &crev);

	if (module_index >= vram_info->ucNumOfVRAMModule) {
		printk(KERN_ERR "[ powerplay ] Invalid VramInfo table.");
		result = -1;
	} else if (vram_info->sHeader.ucTableFormatRevision < 2) {
		printk(KERN_ERR "[ powerplay ] Invalid VramInfo table.");
		result = -1;
	}

	if (0 == result) {
		reg_block = (ATOM_INIT_REG_BLOCK *)
			((uint8_t *)vram_info + le16_to_cpu(vram_info->usMemClkPatchTblOffset));
		result = atomctrl_set_mc_reg_address_table(reg_block, table);
	}

	if (0 == result) {
		result = atomctrl_retrieve_ac_timing(module_index,
					reg_block, table);
	}

	return result;
}

/**
 * Set DRAM timings based on engine clock and memory clock.
 */
int atomctrl_set_engine_dram_timings_rv770(
		struct pp_hwmgr *hwmgr,
		uint32_t engine_clock,
		uint32_t memory_clock)
{
	SET_ENGINE_CLOCK_PS_ALLOCATION engine_clock_parameters;

	/* They are both in 10KHz Units. */
	engine_clock_parameters.ulTargetEngineClock =
		(uint32_t) engine_clock & SET_CLOCK_FREQ_MASK;
	engine_clock_parameters.ulTargetEngineClock |=
		(COMPUTE_ENGINE_PLL_PARAM << 24);

	/* in 10 khz units.*/
	engine_clock_parameters.sReserved.ulClock =
		(uint32_t) memory_clock & SET_CLOCK_FREQ_MASK;
	return cgs_atom_exec_cmd_table(hwmgr->device,
			GetIndexIntoMasterTable(COMMAND, DynamicMemorySettings),
			&engine_clock_parameters);
}

/**
 * Private Function to get the PowerPlay Table Address.
 * WARNING: The tabled returned by this function is in
 * dynamically allocated memory.
 * The caller has to release if by calling kfree.
 */
static ATOM_VOLTAGE_OBJECT_INFO *get_voltage_info_table(void *device)
{
	int index = GetIndexIntoMasterTable(DATA, VoltageObjectInfo);
	u8 frev, crev;
	u16 size;
	union voltage_object_info *voltage_info;

	voltage_info = (union voltage_object_info *)
		cgs_atom_get_data_table(device, index,
			&size, &frev, &crev);

	if (voltage_info != NULL)
		return (ATOM_VOLTAGE_OBJECT_INFO *) &(voltage_info->v3);
	else
		return NULL;
}

static const ATOM_VOLTAGE_OBJECT_V3 *atomctrl_lookup_voltage_type_v3(
		const ATOM_VOLTAGE_OBJECT_INFO_V3_1 * voltage_object_info_table,
		uint8_t voltage_type, uint8_t voltage_mode)
{
	unsigned int size = le16_to_cpu(voltage_object_info_table->sHeader.usStructureSize);
	unsigned int offset = offsetof(ATOM_VOLTAGE_OBJECT_INFO_V3_1, asVoltageObj[0]);
	uint8_t *start = (uint8_t *)voltage_object_info_table;

	while (offset < size) {
		const ATOM_VOLTAGE_OBJECT_V3 *voltage_object =
			(const ATOM_VOLTAGE_OBJECT_V3 *)(start + offset);

		if (voltage_type == voltage_object->asGpioVoltageObj.sHeader.ucVoltageType &&
			voltage_mode == voltage_object->asGpioVoltageObj.sHeader.ucVoltageMode)
			return voltage_object;

		offset += le16_to_cpu(voltage_object->asGpioVoltageObj.sHeader.usSize);
	}

	return NULL;
}

/** atomctrl_get_memory_pll_dividers_si().
 *
 * @param hwmgr                 input parameter: pointer to HwMgr
 * @param clock_value             input parameter: memory clock
 * @param dividers                 output parameter: memory PLL dividers
 * @param strobe_mode            input parameter: 1 for strobe mode,  0 for performance mode
 */
int atomctrl_get_memory_pll_dividers_si(
		struct pp_hwmgr *hwmgr,
		uint32_t clock_value,
		pp_atomctrl_memory_clock_param *mpll_param,
		bool strobe_mode)
{
	COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_1 mpll_parameters;
	int result;

	mpll_parameters.ulClock = (uint32_t) clock_value;
	mpll_parameters.ucInputFlag = (uint8_t)((strobe_mode) ? 1 : 0);

	result = cgs_atom_exec_cmd_table
		(hwmgr->device,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryClockParam),
		 &mpll_parameters);

	if (0 == result) {
		mpll_param->mpll_fb_divider.clk_frac =
			mpll_parameters.ulFbDiv.usFbDivFrac;
		mpll_param->mpll_fb_divider.cl_kf =
			mpll_parameters.ulFbDiv.usFbDiv;
		mpll_param->mpll_post_divider =
			(uint32_t)mpll_parameters.ucPostDiv;
		mpll_param->vco_mode =
			(uint32_t)(mpll_parameters.ucPllCntlFlag &
					MPLL_CNTL_FLAG_VCO_MODE_MASK);
		mpll_param->yclk_sel =
			(uint32_t)((mpll_parameters.ucPllCntlFlag &
						MPLL_CNTL_FLAG_BYPASS_DQ_PLL) ? 1 : 0);
		mpll_param->qdr =
			(uint32_t)((mpll_parameters.ucPllCntlFlag &
						MPLL_CNTL_FLAG_QDR_ENABLE) ? 1 : 0);
		mpll_param->half_rate =
			(uint32_t)((mpll_parameters.ucPllCntlFlag &
						MPLL_CNTL_FLAG_AD_HALF_RATE) ? 1 : 0);
		mpll_param->dll_speed =
			(uint32_t)(mpll_parameters.ucDllSpeed);
		mpll_param->bw_ctrl =
			(uint32_t)(mpll_parameters.ucBWCntl);
	}

	return result;
}

int atomctrl_get_engine_pll_dividers_vi(
		struct pp_hwmgr *hwmgr,
		uint32_t clock_value,
		pp_atomctrl_clock_dividers_vi *dividers)
{
	COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_6 pll_patameters;
	int result;

	pll_patameters.ulClock.ulClock = clock_value;
	pll_patameters.ulClock.ucPostDiv = COMPUTE_GPUCLK_INPUT_FLAG_SCLK;

	result = cgs_atom_exec_cmd_table
		(hwmgr->device,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryEnginePLL),
		 &pll_patameters);

	if (0 == result) {
		dividers->pll_post_divider =
			pll_patameters.ulClock.ucPostDiv;
		dividers->real_clock =
			pll_patameters.ulClock.ulClock;

		dividers->ul_fb_div.ul_fb_div_frac =
			pll_patameters.ulFbDiv.usFbDivFrac;
		dividers->ul_fb_div.ul_fb_div =
			pll_patameters.ulFbDiv.usFbDiv;

		dividers->uc_pll_ref_div =
			pll_patameters.ucPllRefDiv;
		dividers->uc_pll_post_div =
			pll_patameters.ucPllPostDiv;
		dividers->uc_pll_cntl_flag =
			pll_patameters.ucPllCntlFlag;
	}

	return result;
}

int atomctrl_get_dfs_pll_dividers_vi(
		struct pp_hwmgr *hwmgr,
		uint32_t clock_value,
		pp_atomctrl_clock_dividers_vi *dividers)
{
	COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_6 pll_patameters;
	int result;

	pll_patameters.ulClock.ulClock = clock_value;
	pll_patameters.ulClock.ucPostDiv =
		COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK;

	result = cgs_atom_exec_cmd_table
		(hwmgr->device,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryEnginePLL),
		 &pll_patameters);

	if (0 == result) {
		dividers->pll_post_divider =
			pll_patameters.ulClock.ucPostDiv;
		dividers->real_clock =
			pll_patameters.ulClock.ulClock;

		dividers->ul_fb_div.ul_fb_div_frac =
			pll_patameters.ulFbDiv.usFbDivFrac;
		dividers->ul_fb_div.ul_fb_div =
			pll_patameters.ulFbDiv.usFbDiv;

		dividers->uc_pll_ref_div =
			pll_patameters.ucPllRefDiv;
		dividers->uc_pll_post_div =
			pll_patameters.ucPllPostDiv;
		dividers->uc_pll_cntl_flag =
			pll_patameters.ucPllCntlFlag;
	}

	return result;
}

/**
 * Get the reference clock in 10KHz
 */
uint32_t atomctrl_get_reference_clock(struct pp_hwmgr *hwmgr)
{
	ATOM_FIRMWARE_INFO *fw_info;
	u8 frev, crev;
	u16 size;
	uint32_t clock;

	fw_info = (ATOM_FIRMWARE_INFO *)
		cgs_atom_get_data_table(hwmgr->device,
			GetIndexIntoMasterTable(DATA, FirmwareInfo),
			&size, &frev, &crev);

	if (fw_info == NULL)
		clock = 2700;
	else
		clock = (uint32_t)(le16_to_cpu(fw_info->usReferenceClock));

	return clock;
}

/**
 * Returns 0 if the given voltage type is controlled by GPIO pins.
 * voltage_type is one of SET_VOLTAGE_TYPE_ASIC_VDDC,
 * SET_VOLTAGE_TYPE_ASIC_MVDDC, SET_VOLTAGE_TYPE_ASIC_MVDDQ.
 * voltage_mode is one of ATOM_SET_VOLTAGE, ATOM_SET_VOLTAGE_PHASE
 */
bool atomctrl_is_voltage_controled_by_gpio_v3(
		struct pp_hwmgr *hwmgr,
		uint8_t voltage_type,
		uint8_t voltage_mode)
{
	ATOM_VOLTAGE_OBJECT_INFO_V3_1 *voltage_info =
		(ATOM_VOLTAGE_OBJECT_INFO_V3_1 *)get_voltage_info_table(hwmgr->device);
	bool ret;

	PP_ASSERT_WITH_CODE((NULL != voltage_info),
			"Could not find Voltage Table in BIOS.", return -1;);

	ret = (NULL != atomctrl_lookup_voltage_type_v3
			(voltage_info, voltage_type, voltage_mode)) ? 0 : 1;

	return ret;
}

int atomctrl_get_voltage_table_v3(
		struct pp_hwmgr *hwmgr,
		uint8_t voltage_type,
		uint8_t voltage_mode,
		pp_atomctrl_voltage_table *voltage_table)
{
	ATOM_VOLTAGE_OBJECT_INFO_V3_1 *voltage_info =
		(ATOM_VOLTAGE_OBJECT_INFO_V3_1 *)get_voltage_info_table(hwmgr->device);
	const ATOM_VOLTAGE_OBJECT_V3 *voltage_object;
	unsigned int i;

	PP_ASSERT_WITH_CODE((NULL != voltage_info),
			"Could not find Voltage Table in BIOS.", return -1;);

	voltage_object = atomctrl_lookup_voltage_type_v3
		(voltage_info, voltage_type, voltage_mode);

	if (voltage_object == NULL)
		return -1;

	PP_ASSERT_WITH_CODE(
			(voltage_object->asGpioVoltageObj.ucGpioEntryNum <=
			PP_ATOMCTRL_MAX_VOLTAGE_ENTRIES),
			"Too many voltage entries!",
			return -1;
			);

	for (i = 0; i < voltage_object->asGpioVoltageObj.ucGpioEntryNum; i++) {
		voltage_table->entries[i].value =
			voltage_object->asGpioVoltageObj.asVolGpioLut[i].usVoltageValue;
		voltage_table->entries[i].smio_low =
			voltage_object->asGpioVoltageObj.asVolGpioLut[i].ulVoltageId;
	}

	voltage_table->mask_low    =
		voltage_object->asGpioVoltageObj.ulGpioMaskVal;
	voltage_table->count      =
		voltage_object->asGpioVoltageObj.ucGpioEntryNum;
	voltage_table->phase_delay =
		voltage_object->asGpioVoltageObj.ucPhaseDelay;

	return 0;
}

static bool atomctrl_lookup_gpio_pin(
		ATOM_GPIO_PIN_LUT * gpio_lookup_table,
		const uint32_t pinId,
		pp_atomctrl_gpio_pin_assignment *gpio_pin_assignment)
{
	unsigned int size = le16_to_cpu(gpio_lookup_table->sHeader.usStructureSize);
	unsigned int offset = offsetof(ATOM_GPIO_PIN_LUT, asGPIO_Pin[0]);
	uint8_t *start = (uint8_t *)gpio_lookup_table;

	while (offset < size) {
		const ATOM_GPIO_PIN_ASSIGNMENT *pin_assignment =
			(const ATOM_GPIO_PIN_ASSIGNMENT *)(start + offset);

		if (pinId == pin_assignment->ucGPIO_ID) {
			gpio_pin_assignment->uc_gpio_pin_bit_shift =
				pin_assignment->ucGpioPinBitShift;
			gpio_pin_assignment->us_gpio_pin_aindex =
				le16_to_cpu(pin_assignment->usGpioPin_AIndex);
			return 0;
		}

		offset += offsetof(ATOM_GPIO_PIN_ASSIGNMENT, ucGPIO_ID) + 1;
	}

	return 1;
}

/**
 * Private Function to get the PowerPlay Table Address.
 * WARNING: The tabled returned by this function is in
 * dynamically allocated memory.
 * The caller has to release if by calling kfree.
 */
static ATOM_GPIO_PIN_LUT *get_gpio_lookup_table(void *device)
{
	u8 frev, crev;
	u16 size;
	void *table_address;

	table_address = (ATOM_GPIO_PIN_LUT *)
		cgs_atom_get_data_table(device,
				GetIndexIntoMasterTable(DATA, GPIO_Pin_LUT),
				&size, &frev, &crev);

	PP_ASSERT_WITH_CODE((NULL != table_address),
			"Error retrieving BIOS Table Address!", return NULL;);

	return (ATOM_GPIO_PIN_LUT *)table_address;
}

/**
 * Returns 1 if the given pin id find in lookup table.
 */
bool atomctrl_get_pp_assign_pin(
		struct pp_hwmgr *hwmgr,
		const uint32_t pinId,
		pp_atomctrl_gpio_pin_assignment *gpio_pin_assignment)
{
	bool bRet = 0;
	ATOM_GPIO_PIN_LUT *gpio_lookup_table =
		get_gpio_lookup_table(hwmgr->device);

	PP_ASSERT_WITH_CODE((NULL != gpio_lookup_table),
			"Could not find GPIO lookup Table in BIOS.", return -1);

	bRet = atomctrl_lookup_gpio_pin(gpio_lookup_table, pinId,
		gpio_pin_assignment);

	return bRet;
}

/** atomctrl_get_voltage_evv_on_sclk gets voltage via call to ATOM COMMAND table.
 * @param hwmgr               	input: pointer to hwManager
 * @param voltage_type            input: type of EVV voltage VDDC or VDDGFX
 * @param sclk                        input: in 10Khz unit. DPM state SCLK frequency
 *                                   		which is define in PPTable SCLK/VDDC dependence
 *				table associated with this virtual_voltage_Id
 * @param virtual_voltage_Id      input: voltage id which match per voltage DPM state: 0xff01, 0xff02.. 0xff08
 * @param voltage		       output: real voltage level in unit of mv
 */
int atomctrl_get_voltage_evv_on_sclk(
		struct pp_hwmgr *hwmgr,
		uint8_t voltage_type,
		uint32_t sclk, uint16_t virtual_voltage_Id,
		uint16_t *voltage)
{
	int result;
	GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_2 get_voltage_info_param_space;

	get_voltage_info_param_space.ucVoltageType   =
		voltage_type;
	get_voltage_info_param_space.ucVoltageMode   =
		ATOM_GET_VOLTAGE_EVV_VOLTAGE;
	get_voltage_info_param_space.usVoltageLevel  =
		virtual_voltage_Id;
	get_voltage_info_param_space.ulSCLKFreq      =
		sclk;

	result = cgs_atom_exec_cmd_table(hwmgr->device,
			GetIndexIntoMasterTable(COMMAND, GetVoltageInfo),
			&get_voltage_info_param_space);

	if (0 != result)
		return result;

	*voltage = ((GET_EVV_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_2 *)
			(&get_voltage_info_param_space))->usVoltageLevel;

	return result;
}

/**
 * Get the mpll reference clock in 10KHz
 */
uint32_t atomctrl_get_mpll_reference_clock(struct pp_hwmgr *hwmgr)
{
	ATOM_COMMON_TABLE_HEADER *fw_info;
	uint32_t clock;
	u8 frev, crev;
	u16 size;

	fw_info = (ATOM_COMMON_TABLE_HEADER *)
		cgs_atom_get_data_table(hwmgr->device,
				GetIndexIntoMasterTable(DATA, FirmwareInfo),
				&size, &frev, &crev);

	if (fw_info == NULL)
		clock = 2700;
	else {
		if ((fw_info->ucTableFormatRevision == 2) &&
			(le16_to_cpu(fw_info->usStructureSize) >= sizeof(ATOM_FIRMWARE_INFO_V2_1))) {
			ATOM_FIRMWARE_INFO_V2_1 *fwInfo_2_1 =
				(ATOM_FIRMWARE_INFO_V2_1 *)fw_info;
			clock = (uint32_t)(le16_to_cpu(fwInfo_2_1->usMemoryReferenceClock));
		} else {
			ATOM_FIRMWARE_INFO *fwInfo_0_0 =
				(ATOM_FIRMWARE_INFO *)fw_info;
			clock = (uint32_t)(le16_to_cpu(fwInfo_0_0->usReferenceClock));
		}
	}

	return clock;
}

/**
 * Get the asic internal spread spectrum table
 */
static ATOM_ASIC_INTERNAL_SS_INFO *asic_internal_ss_get_ss_table(void *device)
{
	ATOM_ASIC_INTERNAL_SS_INFO *table = NULL;
	u8 frev, crev;
	u16 size;

	table = (ATOM_ASIC_INTERNAL_SS_INFO *)
		cgs_atom_get_data_table(device,
			GetIndexIntoMasterTable(DATA, ASIC_InternalSS_Info),
			&size, &frev, &crev);

	return table;
}

/**
 * Get the asic internal spread spectrum assignment
 */
static int asic_internal_ss_get_ss_asignment(struct pp_hwmgr *hwmgr,
		const uint8_t clockSource,
		const uint32_t clockSpeed,
		pp_atomctrl_internal_ss_info *ssEntry)
{
	ATOM_ASIC_INTERNAL_SS_INFO *table;
	ATOM_ASIC_SS_ASSIGNMENT *ssInfo;
	int entry_found = 0;

	memset(ssEntry, 0x00, sizeof(pp_atomctrl_internal_ss_info));

	table = asic_internal_ss_get_ss_table(hwmgr->device);

	if (NULL == table)
		return -1;

	ssInfo = &table->asSpreadSpectrum[0];

	while (((uint8_t *)ssInfo - (uint8_t *)table) <
		le16_to_cpu(table->sHeader.usStructureSize)) {
		if ((clockSource == ssInfo->ucClockIndication) &&
			((uint32_t)clockSpeed <= le32_to_cpu(ssInfo->ulTargetClockRange))) {
			entry_found = 1;
			break;
		}

		ssInfo = (ATOM_ASIC_SS_ASSIGNMENT *)((uint8_t *)ssInfo +
				sizeof(ATOM_ASIC_SS_ASSIGNMENT));
	}

	if (entry_found) {
		ssEntry->speed_spectrum_percentage =
			ssInfo->usSpreadSpectrumPercentage;
		ssEntry->speed_spectrum_rate = ssInfo->usSpreadRateInKhz;

		if (((GET_DATA_TABLE_MAJOR_REVISION(table) == 2) &&
			(GET_DATA_TABLE_MINOR_REVISION(table) >= 2)) ||
			(GET_DATA_TABLE_MAJOR_REVISION(table) == 3)) {
			ssEntry->speed_spectrum_rate /= 100;
		}

		switch (ssInfo->ucSpreadSpectrumMode) {
		case 0:
			ssEntry->speed_spectrum_mode =
				pp_atomctrl_spread_spectrum_mode_down;
			break;
		case 1:
			ssEntry->speed_spectrum_mode =
				pp_atomctrl_spread_spectrum_mode_center;
			break;
		default:
			ssEntry->speed_spectrum_mode =
				pp_atomctrl_spread_spectrum_mode_down;
			break;
		}
	}

	return entry_found ? 0 : 1;
}

/**
 * Get the memory clock spread spectrum info
 */
int atomctrl_get_memory_clock_spread_spectrum(
		struct pp_hwmgr *hwmgr,
		const uint32_t memory_clock,
		pp_atomctrl_internal_ss_info *ssInfo)
{
	return asic_internal_ss_get_ss_asignment(hwmgr,
			ASIC_INTERNAL_MEMORY_SS, memory_clock, ssInfo);
}
/**
 * Get the engine clock spread spectrum info
 */
int atomctrl_get_engine_clock_spread_spectrum(
		struct pp_hwmgr *hwmgr,
		const uint32_t engine_clock,
		pp_atomctrl_internal_ss_info *ssInfo)
{
	return asic_internal_ss_get_ss_asignment(hwmgr,
			ASIC_INTERNAL_ENGINE_SS, engine_clock, ssInfo);
}


