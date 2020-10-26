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
#include <linux/delay.h>
#include "atom.h"
#include "ppatomctrl.h"
#include "atombios.h"
#include "cgs_common.h"
#include "ppevvmath.h"

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
 * @return   0
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
		smu_atom_get_data_table(hwmgr->adev,
				GetIndexIntoMasterTable(DATA, VRAM_Info), &size, &frev, &crev);

	if (module_index >= vram_info->ucNumOfVRAMModule) {
		pr_err("Invalid VramInfo table.");
		result = -1;
	} else if (vram_info->sHeader.ucTableFormatRevision < 2) {
		pr_err("Invalid VramInfo table.");
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
	struct amdgpu_device *adev = hwmgr->adev;

	SET_ENGINE_CLOCK_PS_ALLOCATION engine_clock_parameters;

	/* They are both in 10KHz Units. */
	engine_clock_parameters.ulTargetEngineClock =
		cpu_to_le32((engine_clock & SET_CLOCK_FREQ_MASK) |
			    ((COMPUTE_ENGINE_PLL_PARAM << 24)));

	/* in 10 khz units.*/
	engine_clock_parameters.sReserved.ulClock =
		cpu_to_le32(memory_clock & SET_CLOCK_FREQ_MASK);

	return amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, DynamicMemorySettings),
			(uint32_t *)&engine_clock_parameters);
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
		smu_atom_get_data_table(device, index,
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
	struct amdgpu_device *adev = hwmgr->adev;
	COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_1 mpll_parameters;
	int result;

	mpll_parameters.ulClock = cpu_to_le32(clock_value);
	mpll_parameters.ucInputFlag = (uint8_t)((strobe_mode) ? 1 : 0);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryClockParam),
		(uint32_t *)&mpll_parameters);

	if (0 == result) {
		mpll_param->mpll_fb_divider.clk_frac =
			le16_to_cpu(mpll_parameters.ulFbDiv.usFbDivFrac);
		mpll_param->mpll_fb_divider.cl_kf =
			le16_to_cpu(mpll_parameters.ulFbDiv.usFbDiv);
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

/** atomctrl_get_memory_pll_dividers_vi().
 *
 * @param hwmgr                 input parameter: pointer to HwMgr
 * @param clock_value             input parameter: memory clock
 * @param dividers               output parameter: memory PLL dividers
 */
int atomctrl_get_memory_pll_dividers_vi(struct pp_hwmgr *hwmgr,
		uint32_t clock_value, pp_atomctrl_memory_clock_param *mpll_param)
{
	struct amdgpu_device *adev = hwmgr->adev;
	COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_2 mpll_parameters;
	int result;

	mpll_parameters.ulClock.ulClock = cpu_to_le32(clock_value);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ComputeMemoryClockParam),
			(uint32_t *)&mpll_parameters);

	if (!result)
		mpll_param->mpll_post_divider =
				(uint32_t)mpll_parameters.ulClock.ucPostDiv;

	return result;
}

int atomctrl_get_memory_pll_dividers_ai(struct pp_hwmgr *hwmgr,
					uint32_t clock_value,
					pp_atomctrl_memory_clock_param_ai *mpll_param)
{
	struct amdgpu_device *adev = hwmgr->adev;
	COMPUTE_MEMORY_CLOCK_PARAM_PARAMETERS_V2_3 mpll_parameters = {{0}, 0, 0};
	int result;

	mpll_parameters.ulClock.ulClock = cpu_to_le32(clock_value);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ComputeMemoryClockParam),
			(uint32_t *)&mpll_parameters);

	/* VEGAM's mpll takes sometime to finish computing */
	udelay(10);

	if (!result) {
		mpll_param->ulMclk_fcw_int =
			le16_to_cpu(mpll_parameters.usMclk_fcw_int);
		mpll_param->ulMclk_fcw_frac =
			le16_to_cpu(mpll_parameters.usMclk_fcw_frac);
		mpll_param->ulClock =
			le32_to_cpu(mpll_parameters.ulClock.ulClock);
		mpll_param->ulPostDiv = mpll_parameters.ulClock.ucPostDiv;
	}

	return result;
}

int atomctrl_get_engine_pll_dividers_kong(struct pp_hwmgr *hwmgr,
					  uint32_t clock_value,
					  pp_atomctrl_clock_dividers_kong *dividers)
{
	struct amdgpu_device *adev = hwmgr->adev;
	COMPUTE_MEMORY_ENGINE_PLL_PARAMETERS_V4 pll_parameters;
	int result;

	pll_parameters.ulClock = cpu_to_le32(clock_value);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryEnginePLL),
		(uint32_t *)&pll_parameters);

	if (0 == result) {
		dividers->pll_post_divider = pll_parameters.ucPostDiv;
		dividers->real_clock = le32_to_cpu(pll_parameters.ulClock);
	}

	return result;
}

int atomctrl_get_engine_pll_dividers_vi(
		struct pp_hwmgr *hwmgr,
		uint32_t clock_value,
		pp_atomctrl_clock_dividers_vi *dividers)
{
	struct amdgpu_device *adev = hwmgr->adev;
	COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_6 pll_patameters;
	int result;

	pll_patameters.ulClock.ulClock = cpu_to_le32(clock_value);
	pll_patameters.ulClock.ucPostDiv = COMPUTE_GPUCLK_INPUT_FLAG_SCLK;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryEnginePLL),
		(uint32_t *)&pll_patameters);

	if (0 == result) {
		dividers->pll_post_divider =
			pll_patameters.ulClock.ucPostDiv;
		dividers->real_clock =
			le32_to_cpu(pll_patameters.ulClock.ulClock);

		dividers->ul_fb_div.ul_fb_div_frac =
			le16_to_cpu(pll_patameters.ulFbDiv.usFbDivFrac);
		dividers->ul_fb_div.ul_fb_div =
			le16_to_cpu(pll_patameters.ulFbDiv.usFbDiv);

		dividers->uc_pll_ref_div =
			pll_patameters.ucPllRefDiv;
		dividers->uc_pll_post_div =
			pll_patameters.ucPllPostDiv;
		dividers->uc_pll_cntl_flag =
			pll_patameters.ucPllCntlFlag;
	}

	return result;
}

int atomctrl_get_engine_pll_dividers_ai(struct pp_hwmgr *hwmgr,
		uint32_t clock_value,
		pp_atomctrl_clock_dividers_ai *dividers)
{
	struct amdgpu_device *adev = hwmgr->adev;
	COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_7 pll_patameters;
	int result;

	pll_patameters.ulClock.ulClock = cpu_to_le32(clock_value);
	pll_patameters.ulClock.ucPostDiv = COMPUTE_GPUCLK_INPUT_FLAG_SCLK;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryEnginePLL),
		(uint32_t *)&pll_patameters);

	if (0 == result) {
		dividers->usSclk_fcw_frac     = le16_to_cpu(pll_patameters.usSclk_fcw_frac);
		dividers->usSclk_fcw_int      = le16_to_cpu(pll_patameters.usSclk_fcw_int);
		dividers->ucSclkPostDiv       = pll_patameters.ucSclkPostDiv;
		dividers->ucSclkVcoMode       = pll_patameters.ucSclkVcoMode;
		dividers->ucSclkPllRange      = pll_patameters.ucSclkPllRange;
		dividers->ucSscEnable         = pll_patameters.ucSscEnable;
		dividers->usSsc_fcw1_frac     = le16_to_cpu(pll_patameters.usSsc_fcw1_frac);
		dividers->usSsc_fcw1_int      = le16_to_cpu(pll_patameters.usSsc_fcw1_int);
		dividers->usPcc_fcw_int       = le16_to_cpu(pll_patameters.usPcc_fcw_int);
		dividers->usSsc_fcw_slew_frac = le16_to_cpu(pll_patameters.usSsc_fcw_slew_frac);
		dividers->usPcc_fcw_slew_frac = le16_to_cpu(pll_patameters.usPcc_fcw_slew_frac);
	}
	return result;
}

int atomctrl_get_dfs_pll_dividers_vi(
		struct pp_hwmgr *hwmgr,
		uint32_t clock_value,
		pp_atomctrl_clock_dividers_vi *dividers)
{
	struct amdgpu_device *adev = hwmgr->adev;
	COMPUTE_GPU_CLOCK_OUTPUT_PARAMETERS_V1_6 pll_patameters;
	int result;

	pll_patameters.ulClock.ulClock = cpu_to_le32(clock_value);
	pll_patameters.ulClock.ucPostDiv =
		COMPUTE_GPUCLK_INPUT_FLAG_DEFAULT_GPUCLK;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
		 GetIndexIntoMasterTable(COMMAND, ComputeMemoryEnginePLL),
		(uint32_t *)&pll_patameters);

	if (0 == result) {
		dividers->pll_post_divider =
			pll_patameters.ulClock.ucPostDiv;
		dividers->real_clock =
			le32_to_cpu(pll_patameters.ulClock.ulClock);

		dividers->ul_fb_div.ul_fb_div_frac =
			le16_to_cpu(pll_patameters.ulFbDiv.usFbDivFrac);
		dividers->ul_fb_div.ul_fb_div =
			le16_to_cpu(pll_patameters.ulFbDiv.usFbDiv);

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
		smu_atom_get_data_table(hwmgr->adev,
			GetIndexIntoMasterTable(DATA, FirmwareInfo),
			&size, &frev, &crev);

	if (fw_info == NULL)
		clock = 2700;
	else
		clock = (uint32_t)(le16_to_cpu(fw_info->usReferenceClock));

	return clock;
}

/**
 * Returns true if the given voltage type is controlled by GPIO pins.
 * voltage_type is one of SET_VOLTAGE_TYPE_ASIC_VDDC,
 * SET_VOLTAGE_TYPE_ASIC_MVDDC, SET_VOLTAGE_TYPE_ASIC_MVDDQ.
 * voltage_mode is one of ATOM_SET_VOLTAGE, ATOM_SET_VOLTAGE_PHASE
 */
bool atomctrl_is_voltage_controlled_by_gpio_v3(
		struct pp_hwmgr *hwmgr,
		uint8_t voltage_type,
		uint8_t voltage_mode)
{
	ATOM_VOLTAGE_OBJECT_INFO_V3_1 *voltage_info =
		(ATOM_VOLTAGE_OBJECT_INFO_V3_1 *)get_voltage_info_table(hwmgr->adev);
	bool ret;

	PP_ASSERT_WITH_CODE((NULL != voltage_info),
			"Could not find Voltage Table in BIOS.", return false;);

	ret = (NULL != atomctrl_lookup_voltage_type_v3
			(voltage_info, voltage_type, voltage_mode)) ? true : false;

	return ret;
}

int atomctrl_get_voltage_table_v3(
		struct pp_hwmgr *hwmgr,
		uint8_t voltage_type,
		uint8_t voltage_mode,
		pp_atomctrl_voltage_table *voltage_table)
{
	ATOM_VOLTAGE_OBJECT_INFO_V3_1 *voltage_info =
		(ATOM_VOLTAGE_OBJECT_INFO_V3_1 *)get_voltage_info_table(hwmgr->adev);
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
			le16_to_cpu(voltage_object->asGpioVoltageObj.asVolGpioLut[i].usVoltageValue);
		voltage_table->entries[i].smio_low =
			le32_to_cpu(voltage_object->asGpioVoltageObj.asVolGpioLut[i].ulVoltageId);
	}

	voltage_table->mask_low    =
		le32_to_cpu(voltage_object->asGpioVoltageObj.ulGpioMaskVal);
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
			return true;
		}

		offset += offsetof(ATOM_GPIO_PIN_ASSIGNMENT, ucGPIO_ID) + 1;
	}

	return false;
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
		smu_atom_get_data_table(device,
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
	bool bRet = false;
	ATOM_GPIO_PIN_LUT *gpio_lookup_table =
		get_gpio_lookup_table(hwmgr->adev);

	PP_ASSERT_WITH_CODE((NULL != gpio_lookup_table),
			"Could not find GPIO lookup Table in BIOS.", return false);

	bRet = atomctrl_lookup_gpio_pin(gpio_lookup_table, pinId,
		gpio_pin_assignment);

	return bRet;
}

int atomctrl_calculate_voltage_evv_on_sclk(
		struct pp_hwmgr *hwmgr,
		uint8_t voltage_type,
		uint32_t sclk,
		uint16_t virtual_voltage_Id,
		uint16_t *voltage,
		uint16_t dpm_level,
		bool debug)
{
	ATOM_ASIC_PROFILING_INFO_V3_4 *getASICProfilingInfo;
	struct amdgpu_device *adev = hwmgr->adev;
	EFUSE_LINEAR_FUNC_PARAM sRO_fuse;
	EFUSE_LINEAR_FUNC_PARAM sCACm_fuse;
	EFUSE_LINEAR_FUNC_PARAM sCACb_fuse;
	EFUSE_LOGISTIC_FUNC_PARAM sKt_Beta_fuse;
	EFUSE_LOGISTIC_FUNC_PARAM sKv_m_fuse;
	EFUSE_LOGISTIC_FUNC_PARAM sKv_b_fuse;
	EFUSE_INPUT_PARAMETER sInput_FuseValues;
	READ_EFUSE_VALUE_PARAMETER sOutput_FuseValues;

	uint32_t ul_RO_fused, ul_CACb_fused, ul_CACm_fused, ul_Kt_Beta_fused, ul_Kv_m_fused, ul_Kv_b_fused;
	fInt fSM_A0, fSM_A1, fSM_A2, fSM_A3, fSM_A4, fSM_A5, fSM_A6, fSM_A7;
	fInt fMargin_RO_a, fMargin_RO_b, fMargin_RO_c, fMargin_fixed, fMargin_FMAX_mean, fMargin_Plat_mean, fMargin_FMAX_sigma, fMargin_Plat_sigma, fMargin_DC_sigma;
	fInt fLkg_FT, repeat;
	fInt fMicro_FMAX, fMicro_CR, fSigma_FMAX, fSigma_CR, fSigma_DC, fDC_SCLK, fSquared_Sigma_DC, fSquared_Sigma_CR, fSquared_Sigma_FMAX;
	fInt fRLL_LoadLine, fPowerDPMx, fDerateTDP, fVDDC_base, fA_Term, fC_Term, fB_Term, fRO_DC_margin;
	fInt fRO_fused, fCACm_fused, fCACb_fused, fKv_m_fused, fKv_b_fused, fKt_Beta_fused, fFT_Lkg_V0NORM;
	fInt fSclk_margin, fSclk, fEVV_V;
	fInt fV_min, fV_max, fT_prod, fLKG_Factor, fT_FT, fV_FT, fV_x, fTDP_Power, fTDP_Power_right, fTDP_Power_left, fTDP_Current, fV_NL;
	uint32_t ul_FT_Lkg_V0NORM;
	fInt fLn_MaxDivMin, fMin, fAverage, fRange;
	fInt fRoots[2];
	fInt fStepSize = GetScaledFraction(625, 100000);

	int result;

	getASICProfilingInfo = (ATOM_ASIC_PROFILING_INFO_V3_4 *)
			smu_atom_get_data_table(hwmgr->adev,
					GetIndexIntoMasterTable(DATA, ASIC_ProfilingInfo),
					NULL, NULL, NULL);

	if (!getASICProfilingInfo)
		return -1;

	if (getASICProfilingInfo->asHeader.ucTableFormatRevision < 3 ||
	    (getASICProfilingInfo->asHeader.ucTableFormatRevision == 3 &&
	     getASICProfilingInfo->asHeader.ucTableContentRevision < 4))
		return -1;

	/*-----------------------------------------------------------
	 *GETTING MULTI-STEP PARAMETERS RELATED TO CURRENT DPM LEVEL
	 *-----------------------------------------------------------
	 */
	fRLL_LoadLine = Divide(getASICProfilingInfo->ulLoadLineSlop, 1000);

	switch (dpm_level) {
	case 1:
		fPowerDPMx = Convert_ULONG_ToFraction(le16_to_cpu(getASICProfilingInfo->usPowerDpm1));
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM1), 1000);
		break;
	case 2:
		fPowerDPMx = Convert_ULONG_ToFraction(le16_to_cpu(getASICProfilingInfo->usPowerDpm2));
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM2), 1000);
		break;
	case 3:
		fPowerDPMx = Convert_ULONG_ToFraction(le16_to_cpu(getASICProfilingInfo->usPowerDpm3));
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM3), 1000);
		break;
	case 4:
		fPowerDPMx = Convert_ULONG_ToFraction(le16_to_cpu(getASICProfilingInfo->usPowerDpm4));
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM4), 1000);
		break;
	case 5:
		fPowerDPMx = Convert_ULONG_ToFraction(le16_to_cpu(getASICProfilingInfo->usPowerDpm5));
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM5), 1000);
		break;
	case 6:
		fPowerDPMx = Convert_ULONG_ToFraction(le16_to_cpu(getASICProfilingInfo->usPowerDpm6));
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM6), 1000);
		break;
	case 7:
		fPowerDPMx = Convert_ULONG_ToFraction(le16_to_cpu(getASICProfilingInfo->usPowerDpm7));
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM7), 1000);
		break;
	default:
		pr_err("DPM Level not supported\n");
		fPowerDPMx = Convert_ULONG_ToFraction(1);
		fDerateTDP = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulTdpDerateDPM0), 1000);
	}

	/*-------------------------
	 * DECODING FUSE VALUES
	 * ------------------------
	 */
	/*Decode RO_Fused*/
	sRO_fuse = getASICProfilingInfo->sRoFuse;

	sInput_FuseValues.usEfuseIndex = sRO_fuse.usEfuseIndex;
	sInput_FuseValues.ucBitShift = sRO_fuse.ucEfuseBitLSB;
	sInput_FuseValues.ucBitLength = sRO_fuse.ucEfuseLength;

	sOutput_FuseValues.sEfuse = sInput_FuseValues;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&sOutput_FuseValues);

	if (result)
		return result;

	/* Finally, the actual fuse value */
	ul_RO_fused = le32_to_cpu(sOutput_FuseValues.ulEfuseValue);
	fMin = GetScaledFraction(le32_to_cpu(sRO_fuse.ulEfuseMin), 1);
	fRange = GetScaledFraction(le32_to_cpu(sRO_fuse.ulEfuseEncodeRange), 1);
	fRO_fused = fDecodeLinearFuse(ul_RO_fused, fMin, fRange, sRO_fuse.ucEfuseLength);

	sCACm_fuse = getASICProfilingInfo->sCACm;

	sInput_FuseValues.usEfuseIndex = sCACm_fuse.usEfuseIndex;
	sInput_FuseValues.ucBitShift = sCACm_fuse.ucEfuseBitLSB;
	sInput_FuseValues.ucBitLength = sCACm_fuse.ucEfuseLength;

	sOutput_FuseValues.sEfuse = sInput_FuseValues;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&sOutput_FuseValues);

	if (result)
		return result;

	ul_CACm_fused = le32_to_cpu(sOutput_FuseValues.ulEfuseValue);
	fMin = GetScaledFraction(le32_to_cpu(sCACm_fuse.ulEfuseMin), 1000);
	fRange = GetScaledFraction(le32_to_cpu(sCACm_fuse.ulEfuseEncodeRange), 1000);

	fCACm_fused = fDecodeLinearFuse(ul_CACm_fused, fMin, fRange, sCACm_fuse.ucEfuseLength);

	sCACb_fuse = getASICProfilingInfo->sCACb;

	sInput_FuseValues.usEfuseIndex = sCACb_fuse.usEfuseIndex;
	sInput_FuseValues.ucBitShift = sCACb_fuse.ucEfuseBitLSB;
	sInput_FuseValues.ucBitLength = sCACb_fuse.ucEfuseLength;
	sOutput_FuseValues.sEfuse = sInput_FuseValues;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&sOutput_FuseValues);

	if (result)
		return result;

	ul_CACb_fused = le32_to_cpu(sOutput_FuseValues.ulEfuseValue);
	fMin = GetScaledFraction(le32_to_cpu(sCACb_fuse.ulEfuseMin), 1000);
	fRange = GetScaledFraction(le32_to_cpu(sCACb_fuse.ulEfuseEncodeRange), 1000);

	fCACb_fused = fDecodeLinearFuse(ul_CACb_fused, fMin, fRange, sCACb_fuse.ucEfuseLength);

	sKt_Beta_fuse = getASICProfilingInfo->sKt_b;

	sInput_FuseValues.usEfuseIndex = sKt_Beta_fuse.usEfuseIndex;
	sInput_FuseValues.ucBitShift = sKt_Beta_fuse.ucEfuseBitLSB;
	sInput_FuseValues.ucBitLength = sKt_Beta_fuse.ucEfuseLength;

	sOutput_FuseValues.sEfuse = sInput_FuseValues;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&sOutput_FuseValues);

	if (result)
		return result;

	ul_Kt_Beta_fused = le32_to_cpu(sOutput_FuseValues.ulEfuseValue);
	fAverage = GetScaledFraction(le32_to_cpu(sKt_Beta_fuse.ulEfuseEncodeAverage), 1000);
	fRange = GetScaledFraction(le32_to_cpu(sKt_Beta_fuse.ulEfuseEncodeRange), 1000);

	fKt_Beta_fused = fDecodeLogisticFuse(ul_Kt_Beta_fused,
			fAverage, fRange, sKt_Beta_fuse.ucEfuseLength);

	sKv_m_fuse = getASICProfilingInfo->sKv_m;

	sInput_FuseValues.usEfuseIndex = sKv_m_fuse.usEfuseIndex;
	sInput_FuseValues.ucBitShift = sKv_m_fuse.ucEfuseBitLSB;
	sInput_FuseValues.ucBitLength = sKv_m_fuse.ucEfuseLength;

	sOutput_FuseValues.sEfuse = sInput_FuseValues;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&sOutput_FuseValues);
	if (result)
		return result;

	ul_Kv_m_fused = le32_to_cpu(sOutput_FuseValues.ulEfuseValue);
	fAverage = GetScaledFraction(le32_to_cpu(sKv_m_fuse.ulEfuseEncodeAverage), 1000);
	fRange = GetScaledFraction((le32_to_cpu(sKv_m_fuse.ulEfuseEncodeRange) & 0x7fffffff), 1000);
	fRange = fMultiply(fRange, ConvertToFraction(-1));

	fKv_m_fused = fDecodeLogisticFuse(ul_Kv_m_fused,
			fAverage, fRange, sKv_m_fuse.ucEfuseLength);

	sKv_b_fuse = getASICProfilingInfo->sKv_b;

	sInput_FuseValues.usEfuseIndex = sKv_b_fuse.usEfuseIndex;
	sInput_FuseValues.ucBitShift = sKv_b_fuse.ucEfuseBitLSB;
	sInput_FuseValues.ucBitLength = sKv_b_fuse.ucEfuseLength;
	sOutput_FuseValues.sEfuse = sInput_FuseValues;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&sOutput_FuseValues);

	if (result)
		return result;

	ul_Kv_b_fused = le32_to_cpu(sOutput_FuseValues.ulEfuseValue);
	fAverage = GetScaledFraction(le32_to_cpu(sKv_b_fuse.ulEfuseEncodeAverage), 1000);
	fRange = GetScaledFraction(le32_to_cpu(sKv_b_fuse.ulEfuseEncodeRange), 1000);

	fKv_b_fused = fDecodeLogisticFuse(ul_Kv_b_fused,
			fAverage, fRange, sKv_b_fuse.ucEfuseLength);

	/* Decoding the Leakage - No special struct container */
	/*
	 * usLkgEuseIndex=56
	 * ucLkgEfuseBitLSB=6
	 * ucLkgEfuseLength=10
	 * ulLkgEncodeLn_MaxDivMin=69077
	 * ulLkgEncodeMax=1000000
	 * ulLkgEncodeMin=1000
	 * ulEfuseLogisticAlpha=13
	 */

	sInput_FuseValues.usEfuseIndex = getASICProfilingInfo->usLkgEuseIndex;
	sInput_FuseValues.ucBitShift = getASICProfilingInfo->ucLkgEfuseBitLSB;
	sInput_FuseValues.ucBitLength = getASICProfilingInfo->ucLkgEfuseLength;

	sOutput_FuseValues.sEfuse = sInput_FuseValues;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&sOutput_FuseValues);

	if (result)
		return result;

	ul_FT_Lkg_V0NORM = le32_to_cpu(sOutput_FuseValues.ulEfuseValue);
	fLn_MaxDivMin = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulLkgEncodeLn_MaxDivMin), 10000);
	fMin = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulLkgEncodeMin), 10000);

	fFT_Lkg_V0NORM = fDecodeLeakageID(ul_FT_Lkg_V0NORM,
			fLn_MaxDivMin, fMin, getASICProfilingInfo->ucLkgEfuseLength);
	fLkg_FT = fFT_Lkg_V0NORM;

	/*-------------------------------------------
	 * PART 2 - Grabbing all required values
	 *-------------------------------------------
	 */
	fSM_A0 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A0), 1000000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A0_sign)));
	fSM_A1 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A1), 1000000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A1_sign)));
	fSM_A2 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A2), 100000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A2_sign)));
	fSM_A3 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A3), 1000000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A3_sign)));
	fSM_A4 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A4), 1000000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A4_sign)));
	fSM_A5 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A5), 1000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A5_sign)));
	fSM_A6 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A6), 1000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A6_sign)));
	fSM_A7 = fMultiply(GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulSM_A7), 1000),
			ConvertToFraction(uPow(-1, getASICProfilingInfo->ucSM_A7_sign)));

	fMargin_RO_a = ConvertToFraction(le32_to_cpu(getASICProfilingInfo->ulMargin_RO_a));
	fMargin_RO_b = ConvertToFraction(le32_to_cpu(getASICProfilingInfo->ulMargin_RO_b));
	fMargin_RO_c = ConvertToFraction(le32_to_cpu(getASICProfilingInfo->ulMargin_RO_c));

	fMargin_fixed = ConvertToFraction(le32_to_cpu(getASICProfilingInfo->ulMargin_fixed));

	fMargin_FMAX_mean = GetScaledFraction(
		le32_to_cpu(getASICProfilingInfo->ulMargin_Fmax_mean), 10000);
	fMargin_Plat_mean = GetScaledFraction(
		le32_to_cpu(getASICProfilingInfo->ulMargin_plat_mean), 10000);
	fMargin_FMAX_sigma = GetScaledFraction(
		le32_to_cpu(getASICProfilingInfo->ulMargin_Fmax_sigma), 10000);
	fMargin_Plat_sigma = GetScaledFraction(
		le32_to_cpu(getASICProfilingInfo->ulMargin_plat_sigma), 10000);

	fMargin_DC_sigma = GetScaledFraction(
		le32_to_cpu(getASICProfilingInfo->ulMargin_DC_sigma), 100);
	fMargin_DC_sigma = fDivide(fMargin_DC_sigma, ConvertToFraction(1000));

	fCACm_fused = fDivide(fCACm_fused, ConvertToFraction(100));
	fCACb_fused = fDivide(fCACb_fused, ConvertToFraction(100));
	fKt_Beta_fused = fDivide(fKt_Beta_fused, ConvertToFraction(100));
	fKv_m_fused =  fNegate(fDivide(fKv_m_fused, ConvertToFraction(100)));
	fKv_b_fused = fDivide(fKv_b_fused, ConvertToFraction(10));

	fSclk = GetScaledFraction(sclk, 100);

	fV_max = fDivide(GetScaledFraction(
				 le32_to_cpu(getASICProfilingInfo->ulMaxVddc), 1000), ConvertToFraction(4));
	fT_prod = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulBoardCoreTemp), 10);
	fLKG_Factor = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulEvvLkgFactor), 100);
	fT_FT = GetScaledFraction(le32_to_cpu(getASICProfilingInfo->ulLeakageTemp), 10);
	fV_FT = fDivide(GetScaledFraction(
				le32_to_cpu(getASICProfilingInfo->ulLeakageVoltage), 1000), ConvertToFraction(4));
	fV_min = fDivide(GetScaledFraction(
				 le32_to_cpu(getASICProfilingInfo->ulMinVddc), 1000), ConvertToFraction(4));

	/*-----------------------
	 * PART 3
	 *-----------------------
	 */

	fA_Term = fAdd(fMargin_RO_a, fAdd(fMultiply(fSM_A4, fSclk), fSM_A5));
	fB_Term = fAdd(fAdd(fMultiply(fSM_A2, fSclk), fSM_A6), fMargin_RO_b);
	fC_Term = fAdd(fMargin_RO_c,
			fAdd(fMultiply(fSM_A0, fLkg_FT),
			fAdd(fMultiply(fSM_A1, fMultiply(fLkg_FT, fSclk)),
			fAdd(fMultiply(fSM_A3, fSclk),
			fSubtract(fSM_A7, fRO_fused)))));

	fVDDC_base = fSubtract(fRO_fused,
			fSubtract(fMargin_RO_c,
					fSubtract(fSM_A3, fMultiply(fSM_A1, fSclk))));
	fVDDC_base = fDivide(fVDDC_base, fAdd(fMultiply(fSM_A0, fSclk), fSM_A2));

	repeat = fSubtract(fVDDC_base,
			fDivide(fMargin_DC_sigma, ConvertToFraction(1000)));

	fRO_DC_margin = fAdd(fMultiply(fMargin_RO_a,
			fGetSquare(repeat)),
			fAdd(fMultiply(fMargin_RO_b, repeat),
			fMargin_RO_c));

	fDC_SCLK = fSubtract(fRO_fused,
			fSubtract(fRO_DC_margin,
			fSubtract(fSM_A3,
			fMultiply(fSM_A2, repeat))));
	fDC_SCLK = fDivide(fDC_SCLK, fAdd(fMultiply(fSM_A0, repeat), fSM_A1));

	fSigma_DC = fSubtract(fSclk, fDC_SCLK);

	fMicro_FMAX = fMultiply(fSclk, fMargin_FMAX_mean);
	fMicro_CR = fMultiply(fSclk, fMargin_Plat_mean);
	fSigma_FMAX = fMultiply(fSclk, fMargin_FMAX_sigma);
	fSigma_CR = fMultiply(fSclk, fMargin_Plat_sigma);

	fSquared_Sigma_DC = fGetSquare(fSigma_DC);
	fSquared_Sigma_CR = fGetSquare(fSigma_CR);
	fSquared_Sigma_FMAX = fGetSquare(fSigma_FMAX);

	fSclk_margin = fAdd(fMicro_FMAX,
			fAdd(fMicro_CR,
			fAdd(fMargin_fixed,
			fSqrt(fAdd(fSquared_Sigma_FMAX,
			fAdd(fSquared_Sigma_DC, fSquared_Sigma_CR))))));
	/*
	 fA_Term = fSM_A4 * (fSclk + fSclk_margin) + fSM_A5;
	 fB_Term = fSM_A2 * (fSclk + fSclk_margin) + fSM_A6;
	 fC_Term = fRO_DC_margin + fSM_A0 * fLkg_FT + fSM_A1 * fLkg_FT * (fSclk + fSclk_margin) + fSM_A3 * (fSclk + fSclk_margin) + fSM_A7 - fRO_fused;
	 */

	fA_Term = fAdd(fMultiply(fSM_A4, fAdd(fSclk, fSclk_margin)), fSM_A5);
	fB_Term = fAdd(fMultiply(fSM_A2, fAdd(fSclk, fSclk_margin)), fSM_A6);
	fC_Term = fAdd(fRO_DC_margin,
			fAdd(fMultiply(fSM_A0, fLkg_FT),
			fAdd(fMultiply(fMultiply(fSM_A1, fLkg_FT),
			fAdd(fSclk, fSclk_margin)),
			fAdd(fMultiply(fSM_A3,
			fAdd(fSclk, fSclk_margin)),
			fSubtract(fSM_A7, fRO_fused)))));

	SolveQuadracticEqn(fA_Term, fB_Term, fC_Term, fRoots);

	if (GreaterThan(fRoots[0], fRoots[1]))
		fEVV_V = fRoots[1];
	else
		fEVV_V = fRoots[0];

	if (GreaterThan(fV_min, fEVV_V))
		fEVV_V = fV_min;
	else if (GreaterThan(fEVV_V, fV_max))
		fEVV_V = fSubtract(fV_max, fStepSize);

	fEVV_V = fRoundUpByStepSize(fEVV_V, fStepSize, 0);

	/*-----------------
	 * PART 4
	 *-----------------
	 */

	fV_x = fV_min;

	while (GreaterThan(fAdd(fV_max, fStepSize), fV_x)) {
		fTDP_Power_left = fMultiply(fMultiply(fMultiply(fAdd(
				fMultiply(fCACm_fused, fV_x), fCACb_fused), fSclk),
				fGetSquare(fV_x)), fDerateTDP);

		fTDP_Power_right = fMultiply(fFT_Lkg_V0NORM, fMultiply(fLKG_Factor,
				fMultiply(fExponential(fMultiply(fAdd(fMultiply(fKv_m_fused,
				fT_prod), fKv_b_fused), fV_x)), fV_x)));
		fTDP_Power_right = fMultiply(fTDP_Power_right, fExponential(fMultiply(
				fKt_Beta_fused, fT_prod)));
		fTDP_Power_right = fDivide(fTDP_Power_right, fExponential(fMultiply(
				fAdd(fMultiply(fKv_m_fused, fT_prod), fKv_b_fused), fV_FT)));
		fTDP_Power_right = fDivide(fTDP_Power_right, fExponential(fMultiply(
				fKt_Beta_fused, fT_FT)));

		fTDP_Power = fAdd(fTDP_Power_left, fTDP_Power_right);

		fTDP_Current = fDivide(fTDP_Power, fV_x);

		fV_NL = fAdd(fV_x, fDivide(fMultiply(fTDP_Current, fRLL_LoadLine),
				ConvertToFraction(10)));

		fV_NL = fRoundUpByStepSize(fV_NL, fStepSize, 0);

		if (GreaterThan(fV_max, fV_NL) &&
			(GreaterThan(fV_NL, fEVV_V) ||
			Equal(fV_NL, fEVV_V))) {
			fV_NL = fMultiply(fV_NL, ConvertToFraction(1000));

			*voltage = (uint16_t)fV_NL.partial.real;
			break;
		} else
			fV_x = fAdd(fV_x, fStepSize);
	}

	return result;
}

/** atomctrl_get_voltage_evv_on_sclk gets voltage via call to ATOM COMMAND table.
 * @param hwmgr	input: pointer to hwManager
 * @param voltage_type            input: type of EVV voltage VDDC or VDDGFX
 * @param sclk                        input: in 10Khz unit. DPM state SCLK frequency
 *		which is define in PPTable SCLK/VDDC dependence
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
	struct amdgpu_device *adev = hwmgr->adev;
	GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_2 get_voltage_info_param_space;
	int result;

	get_voltage_info_param_space.ucVoltageType   =
		voltage_type;
	get_voltage_info_param_space.ucVoltageMode   =
		ATOM_GET_VOLTAGE_EVV_VOLTAGE;
	get_voltage_info_param_space.usVoltageLevel  =
		cpu_to_le16(virtual_voltage_Id);
	get_voltage_info_param_space.ulSCLKFreq      =
		cpu_to_le32(sclk);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, GetVoltageInfo),
			(uint32_t *)&get_voltage_info_param_space);

	*voltage = result ? 0 :
			le16_to_cpu(((GET_EVV_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_2 *)
				(&get_voltage_info_param_space))->usVoltageLevel);

	return result;
}

/**
 * atomctrl_get_voltage_evv gets voltage via call to ATOM COMMAND table.
 * @param hwmgr	input: pointer to hwManager
 * @param virtual_voltage_id      input: voltage id which match per voltage DPM state: 0xff01, 0xff02.. 0xff08
 * @param voltage		       output: real voltage level in unit of mv
 */
int atomctrl_get_voltage_evv(struct pp_hwmgr *hwmgr,
			     uint16_t virtual_voltage_id,
			     uint16_t *voltage)
{
	struct amdgpu_device *adev = hwmgr->adev;
	GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_2 get_voltage_info_param_space;
	int result;
	int entry_id;

	/* search for leakage voltage ID 0xff01 ~ 0xff08 and sckl */
	for (entry_id = 0; entry_id < hwmgr->dyn_state.vddc_dependency_on_sclk->count; entry_id++) {
		if (hwmgr->dyn_state.vddc_dependency_on_sclk->entries[entry_id].v == virtual_voltage_id) {
			/* found */
			break;
		}
	}

	if (entry_id >= hwmgr->dyn_state.vddc_dependency_on_sclk->count) {
	        pr_debug("Can't find requested voltage id in vddc_dependency_on_sclk table!\n");
	        return -EINVAL;
	}

	get_voltage_info_param_space.ucVoltageType = VOLTAGE_TYPE_VDDC;
	get_voltage_info_param_space.ucVoltageMode = ATOM_GET_VOLTAGE_EVV_VOLTAGE;
	get_voltage_info_param_space.usVoltageLevel = virtual_voltage_id;
	get_voltage_info_param_space.ulSCLKFreq =
		cpu_to_le32(hwmgr->dyn_state.vddc_dependency_on_sclk->entries[entry_id].clk);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, GetVoltageInfo),
			(uint32_t *)&get_voltage_info_param_space);

	if (0 != result)
		return result;

	*voltage = le16_to_cpu(((GET_EVV_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_2 *)
				(&get_voltage_info_param_space))->usVoltageLevel);

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
		smu_atom_get_data_table(hwmgr->adev,
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
		smu_atom_get_data_table(device,
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

	table = asic_internal_ss_get_ss_table(hwmgr->adev);

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
			le16_to_cpu(ssInfo->usSpreadSpectrumPercentage);
		ssEntry->speed_spectrum_rate = le16_to_cpu(ssInfo->usSpreadRateInKhz);

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

int atomctrl_read_efuse(struct pp_hwmgr *hwmgr, uint16_t start_index,
		uint16_t end_index, uint32_t mask, uint32_t *efuse)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int result;
	READ_EFUSE_VALUE_PARAMETER efuse_param;

	efuse_param.sEfuse.usEfuseIndex = cpu_to_le16((start_index / 32) * 4);
	efuse_param.sEfuse.ucBitShift = (uint8_t)
			(start_index - ((start_index / 32) * 32));
	efuse_param.sEfuse.ucBitLength  = (uint8_t)
			((end_index - start_index) + 1);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, ReadEfuseValue),
			(uint32_t *)&efuse_param);
	*efuse = result ? 0 : le32_to_cpu(efuse_param.ulEfuseValue) & mask;

	return result;
}

int atomctrl_set_ac_timing_ai(struct pp_hwmgr *hwmgr, uint32_t memory_clock,
			      uint8_t level)
{
	struct amdgpu_device *adev = hwmgr->adev;
	DYNAMICE_MEMORY_SETTINGS_PARAMETER_V2_1 memory_clock_parameters;
	int result;

	memory_clock_parameters.asDPMMCReg.ulClock.ulClockFreq =
		memory_clock & SET_CLOCK_FREQ_MASK;
	memory_clock_parameters.asDPMMCReg.ulClock.ulComputeClockFlag =
		ADJUST_MC_SETTING_PARAM;
	memory_clock_parameters.asDPMMCReg.ucMclkDPMState = level;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
		 GetIndexIntoMasterTable(COMMAND, DynamicMemorySettings),
		(uint32_t *)&memory_clock_parameters);

	return result;
}

int atomctrl_get_voltage_evv_on_sclk_ai(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
				uint32_t sclk, uint16_t virtual_voltage_Id, uint32_t *voltage)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int result;
	GET_VOLTAGE_INFO_INPUT_PARAMETER_V1_3 get_voltage_info_param_space;

	get_voltage_info_param_space.ucVoltageType = voltage_type;
	get_voltage_info_param_space.ucVoltageMode = ATOM_GET_VOLTAGE_EVV_VOLTAGE;
	get_voltage_info_param_space.usVoltageLevel = cpu_to_le16(virtual_voltage_Id);
	get_voltage_info_param_space.ulSCLKFreq = cpu_to_le32(sclk);

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, GetVoltageInfo),
			(uint32_t *)&get_voltage_info_param_space);

	*voltage = result ? 0 :
		le32_to_cpu(((GET_EVV_VOLTAGE_INFO_OUTPUT_PARAMETER_V1_3 *)(&get_voltage_info_param_space))->ulVoltageLevel);

	return result;
}

int atomctrl_get_smc_sclk_range_table(struct pp_hwmgr *hwmgr, struct pp_atom_ctrl_sclk_range_table *table)
{

	int i;
	u8 frev, crev;
	u16 size;

	ATOM_SMU_INFO_V2_1 *psmu_info =
		(ATOM_SMU_INFO_V2_1 *)smu_atom_get_data_table(hwmgr->adev,
			GetIndexIntoMasterTable(DATA, SMU_Info),
			&size, &frev, &crev);


	for (i = 0; i < psmu_info->ucSclkEntryNum; i++) {
		table->entry[i].ucVco_setting = psmu_info->asSclkFcwRangeEntry[i].ucVco_setting;
		table->entry[i].ucPostdiv = psmu_info->asSclkFcwRangeEntry[i].ucPostdiv;
		table->entry[i].usFcw_pcc =
			le16_to_cpu(psmu_info->asSclkFcwRangeEntry[i].ucFcw_pcc);
		table->entry[i].usFcw_trans_upper =
			le16_to_cpu(psmu_info->asSclkFcwRangeEntry[i].ucFcw_trans_upper);
		table->entry[i].usRcw_trans_lower =
			le16_to_cpu(psmu_info->asSclkFcwRangeEntry[i].ucRcw_trans_lower);
	}

	return 0;
}

int atomctrl_get_avfs_information(struct pp_hwmgr *hwmgr,
				  struct pp_atom_ctrl__avfs_parameters *param)
{
	ATOM_ASIC_PROFILING_INFO_V3_6 *profile = NULL;

	if (param == NULL)
		return -EINVAL;

	profile = (ATOM_ASIC_PROFILING_INFO_V3_6 *)
			smu_atom_get_data_table(hwmgr->adev,
					GetIndexIntoMasterTable(DATA, ASIC_ProfilingInfo),
					NULL, NULL, NULL);
	if (!profile)
		return -1;

	param->ulAVFS_meanNsigma_Acontant0 = le32_to_cpu(profile->ulAVFS_meanNsigma_Acontant0);
	param->ulAVFS_meanNsigma_Acontant1 = le32_to_cpu(profile->ulAVFS_meanNsigma_Acontant1);
	param->ulAVFS_meanNsigma_Acontant2 = le32_to_cpu(profile->ulAVFS_meanNsigma_Acontant2);
	param->usAVFS_meanNsigma_DC_tol_sigma = le16_to_cpu(profile->usAVFS_meanNsigma_DC_tol_sigma);
	param->usAVFS_meanNsigma_Platform_mean = le16_to_cpu(profile->usAVFS_meanNsigma_Platform_mean);
	param->usAVFS_meanNsigma_Platform_sigma = le16_to_cpu(profile->usAVFS_meanNsigma_Platform_sigma);
	param->ulGB_VDROOP_TABLE_CKSOFF_a0 = le32_to_cpu(profile->ulGB_VDROOP_TABLE_CKSOFF_a0);
	param->ulGB_VDROOP_TABLE_CKSOFF_a1 = le32_to_cpu(profile->ulGB_VDROOP_TABLE_CKSOFF_a1);
	param->ulGB_VDROOP_TABLE_CKSOFF_a2 = le32_to_cpu(profile->ulGB_VDROOP_TABLE_CKSOFF_a2);
	param->ulGB_VDROOP_TABLE_CKSON_a0 = le32_to_cpu(profile->ulGB_VDROOP_TABLE_CKSON_a0);
	param->ulGB_VDROOP_TABLE_CKSON_a1 = le32_to_cpu(profile->ulGB_VDROOP_TABLE_CKSON_a1);
	param->ulGB_VDROOP_TABLE_CKSON_a2 = le32_to_cpu(profile->ulGB_VDROOP_TABLE_CKSON_a2);
	param->ulAVFSGB_FUSE_TABLE_CKSOFF_m1 = le32_to_cpu(profile->ulAVFSGB_FUSE_TABLE_CKSOFF_m1);
	param->usAVFSGB_FUSE_TABLE_CKSOFF_m2 = le16_to_cpu(profile->usAVFSGB_FUSE_TABLE_CKSOFF_m2);
	param->ulAVFSGB_FUSE_TABLE_CKSOFF_b = le32_to_cpu(profile->ulAVFSGB_FUSE_TABLE_CKSOFF_b);
	param->ulAVFSGB_FUSE_TABLE_CKSON_m1 = le32_to_cpu(profile->ulAVFSGB_FUSE_TABLE_CKSON_m1);
	param->usAVFSGB_FUSE_TABLE_CKSON_m2 = le16_to_cpu(profile->usAVFSGB_FUSE_TABLE_CKSON_m2);
	param->ulAVFSGB_FUSE_TABLE_CKSON_b = le32_to_cpu(profile->ulAVFSGB_FUSE_TABLE_CKSON_b);
	param->usMaxVoltage_0_25mv = le16_to_cpu(profile->usMaxVoltage_0_25mv);
	param->ucEnableGB_VDROOP_TABLE_CKSOFF = profile->ucEnableGB_VDROOP_TABLE_CKSOFF;
	param->ucEnableGB_VDROOP_TABLE_CKSON = profile->ucEnableGB_VDROOP_TABLE_CKSON;
	param->ucEnableGB_FUSE_TABLE_CKSOFF = profile->ucEnableGB_FUSE_TABLE_CKSOFF;
	param->ucEnableGB_FUSE_TABLE_CKSON = profile->ucEnableGB_FUSE_TABLE_CKSON;
	param->usPSM_Age_ComFactor = le16_to_cpu(profile->usPSM_Age_ComFactor);
	param->ucEnableApplyAVFS_CKS_OFF_Voltage = profile->ucEnableApplyAVFS_CKS_OFF_Voltage;

	return 0;
}

int  atomctrl_get_svi2_info(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
				uint8_t *svd_gpio_id, uint8_t *svc_gpio_id,
				uint16_t *load_line)
{
	ATOM_VOLTAGE_OBJECT_INFO_V3_1 *voltage_info =
		(ATOM_VOLTAGE_OBJECT_INFO_V3_1 *)get_voltage_info_table(hwmgr->adev);

	const ATOM_VOLTAGE_OBJECT_V3 *voltage_object;

	PP_ASSERT_WITH_CODE((NULL != voltage_info),
			"Could not find Voltage Table in BIOS.", return -EINVAL);

	voltage_object = atomctrl_lookup_voltage_type_v3
		(voltage_info, voltage_type,  VOLTAGE_OBJ_SVID2);

	*svd_gpio_id = voltage_object->asSVID2Obj.ucSVDGpioId;
	*svc_gpio_id = voltage_object->asSVID2Obj.ucSVCGpioId;
	*load_line = voltage_object->asSVID2Obj.usLoadLine_PSI;

	return 0;
}

int atomctrl_get_leakage_id_from_efuse(struct pp_hwmgr *hwmgr, uint16_t *virtual_voltage_id)
{
	struct amdgpu_device *adev = hwmgr->adev;
	SET_VOLTAGE_PS_ALLOCATION allocation;
	SET_VOLTAGE_PARAMETERS_V1_3 *voltage_parameters =
			(SET_VOLTAGE_PARAMETERS_V1_3 *)&allocation.sASICSetVoltage;
	int result;

	voltage_parameters->ucVoltageMode = ATOM_GET_LEAKAGE_ID;

	result = amdgpu_atom_execute_table(adev->mode_info.atom_context,
			GetIndexIntoMasterTable(COMMAND, SetVoltage),
			(uint32_t *)voltage_parameters);

	*virtual_voltage_id = voltage_parameters->usVoltageLevel;

	return result;
}

int atomctrl_get_leakage_vddc_base_on_leakage(struct pp_hwmgr *hwmgr,
					uint16_t *vddc, uint16_t *vddci,
					uint16_t virtual_voltage_id,
					uint16_t efuse_voltage_id)
{
	int i, j;
	int ix;
	u16 *leakage_bin, *vddc_id_buf, *vddc_buf, *vddci_id_buf, *vddci_buf;
	ATOM_ASIC_PROFILING_INFO_V2_1 *profile;

	*vddc = 0;
	*vddci = 0;

	ix = GetIndexIntoMasterTable(DATA, ASIC_ProfilingInfo);

	profile = (ATOM_ASIC_PROFILING_INFO_V2_1 *)
			smu_atom_get_data_table(hwmgr->adev,
					ix,
					NULL, NULL, NULL);
	if (!profile)
		return -EINVAL;

	if ((profile->asHeader.ucTableFormatRevision >= 2) &&
		(profile->asHeader.ucTableContentRevision >= 1) &&
		(profile->asHeader.usStructureSize >= sizeof(ATOM_ASIC_PROFILING_INFO_V2_1))) {
		leakage_bin = (u16 *)((char *)profile + profile->usLeakageBinArrayOffset);
		vddc_id_buf = (u16 *)((char *)profile + profile->usElbVDDC_IdArrayOffset);
		vddc_buf = (u16 *)((char *)profile + profile->usElbVDDC_LevelArrayOffset);
		if (profile->ucElbVDDC_Num > 0) {
			for (i = 0; i < profile->ucElbVDDC_Num; i++) {
				if (vddc_id_buf[i] == virtual_voltage_id) {
					for (j = 0; j < profile->ucLeakageBinNum; j++) {
						if (efuse_voltage_id <= leakage_bin[j]) {
							*vddc = vddc_buf[j * profile->ucElbVDDC_Num + i];
							break;
						}
					}
					break;
				}
			}
		}

		vddci_id_buf = (u16 *)((char *)profile + profile->usElbVDDCI_IdArrayOffset);
		vddci_buf   = (u16 *)((char *)profile + profile->usElbVDDCI_LevelArrayOffset);
		if (profile->ucElbVDDCI_Num > 0) {
			for (i = 0; i < profile->ucElbVDDCI_Num; i++) {
				if (vddci_id_buf[i] == virtual_voltage_id) {
					for (j = 0; j < profile->ucLeakageBinNum; j++) {
						if (efuse_voltage_id <= leakage_bin[j]) {
							*vddci = vddci_buf[j * profile->ucElbVDDCI_Num + i];
							break;
						}
					}
					break;
				}
			}
		}
	}

	return 0;
}

void atomctrl_get_voltage_range(struct pp_hwmgr *hwmgr, uint32_t *max_vddc,
							uint32_t *min_vddc)
{
	void *profile;

	profile = smu_atom_get_data_table(hwmgr->adev,
					GetIndexIntoMasterTable(DATA, ASIC_ProfilingInfo),
					NULL, NULL, NULL);

	if (profile) {
		switch (hwmgr->chip_id) {
		case CHIP_TONGA:
		case CHIP_FIJI:
			*max_vddc = le32_to_cpu(((ATOM_ASIC_PROFILING_INFO_V3_3 *)profile)->ulMaxVddc) / 4;
			*min_vddc = le32_to_cpu(((ATOM_ASIC_PROFILING_INFO_V3_3 *)profile)->ulMinVddc) / 4;
			return;
		case CHIP_POLARIS11:
		case CHIP_POLARIS10:
		case CHIP_POLARIS12:
			*max_vddc = le32_to_cpu(((ATOM_ASIC_PROFILING_INFO_V3_6 *)profile)->ulMaxVddc) / 100;
			*min_vddc = le32_to_cpu(((ATOM_ASIC_PROFILING_INFO_V3_6 *)profile)->ulMinVddc) / 100;
			return;
		default:
			break;
		}
	}
	*max_vddc = 0;
	*min_vddc = 0;
}
