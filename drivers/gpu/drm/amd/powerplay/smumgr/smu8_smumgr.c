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

#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "cgs_common.h"
#include "smu/smu_8_0_d.h"
#include "smu/smu_8_0_sh_mask.h"
#include "smu8.h"
#include "smu8_fusion.h"
#include "smu8_smumgr.h"
#include "cz_ppsmc.h"
#include "smu_ucode_xfer_cz.h"
#include "gca/gfx_8_0_d.h"
#include "gca/gfx_8_0_sh_mask.h"
#include "smumgr.h"

#define SIZE_ALIGN_32(x)    (((x) + 31) / 32 * 32)

static const enum smu8_scratch_entry firmware_list[] = {
	SMU8_SCRATCH_ENTRY_UCODE_ID_SDMA0,
	SMU8_SCRATCH_ENTRY_UCODE_ID_SDMA1,
	SMU8_SCRATCH_ENTRY_UCODE_ID_CP_CE,
	SMU8_SCRATCH_ENTRY_UCODE_ID_CP_PFP,
	SMU8_SCRATCH_ENTRY_UCODE_ID_CP_ME,
	SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1,
	SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2,
	SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_G,
};

static uint32_t smu8_get_argument(struct pp_hwmgr *hwmgr)
{
	if (hwmgr == NULL || hwmgr->device == NULL)
		return 0;

	return cgs_read_register(hwmgr->device,
					mmSMU_MP1_SRBM2P_ARG_0);
}

/* Send a message to the SMC, and wait for its response.*/
static int smu8_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
					    uint16_t msg, uint32_t parameter)
{
	int result = 0;
	ktime_t t_start;
	s64 elapsed_us;

	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	result = PHM_WAIT_FIELD_UNEQUAL(hwmgr,
					SMU_MP1_SRBM2P_RESP_0, CONTENT, 0);
	if (result != 0) {
		/* Read the last message to SMU, to report actual cause */
		uint32_t val = cgs_read_register(hwmgr->device,
						 mmSMU_MP1_SRBM2P_MSG_0);
		pr_err("%s(0x%04x) aborted; SMU still servicing msg (0x%04x)\n",
			__func__, msg, val);
		return result;
	}
	t_start = ktime_get();

	cgs_write_register(hwmgr->device, mmSMU_MP1_SRBM2P_ARG_0, parameter);

	cgs_write_register(hwmgr->device, mmSMU_MP1_SRBM2P_RESP_0, 0);
	cgs_write_register(hwmgr->device, mmSMU_MP1_SRBM2P_MSG_0, msg);

	result = PHM_WAIT_FIELD_UNEQUAL(hwmgr,
					SMU_MP1_SRBM2P_RESP_0, CONTENT, 0);

	elapsed_us = ktime_us_delta(ktime_get(), t_start);

	WARN(result, "%s(0x%04x, %#x) timed out after %lld us\n",
			__func__, msg, parameter, elapsed_us);

	return result;
}

static int smu8_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	return smu8_send_msg_to_smc_with_parameter(hwmgr, msg, 0);
}

static int smu8_set_smc_sram_address(struct pp_hwmgr *hwmgr,
				     uint32_t smc_address, uint32_t limit)
{
	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	if (0 != (3 & smc_address)) {
		pr_err("SMC address must be 4 byte aligned\n");
		return -EINVAL;
	}

	if (limit <= (smc_address + 3)) {
		pr_err("SMC address beyond the SMC RAM area\n");
		return -EINVAL;
	}

	cgs_write_register(hwmgr->device, mmMP0PUB_IND_INDEX_0,
				SMN_MP1_SRAM_START_ADDR + smc_address);

	return 0;
}

static int smu8_write_smc_sram_dword(struct pp_hwmgr *hwmgr,
		uint32_t smc_address, uint32_t value, uint32_t limit)
{
	int result;

	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	result = smu8_set_smc_sram_address(hwmgr, smc_address, limit);
	if (!result)
		cgs_write_register(hwmgr->device, mmMP0PUB_IND_DATA_0, value);

	return result;
}

static int smu8_check_fw_load_finish(struct pp_hwmgr *hwmgr,
				   uint32_t firmware)
{
	int i;
	uint32_t index = SMN_MP1_SRAM_START_ADDR +
			 SMU8_FIRMWARE_HEADER_LOCATION +
			 offsetof(struct SMU8_Firmware_Header, UcodeLoadStatus);

	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	cgs_write_register(hwmgr->device, mmMP0PUB_IND_INDEX, index);

	for (i = 0; i < hwmgr->usec_timeout; i++) {
		if (firmware ==
			(cgs_read_register(hwmgr->device, mmMP0PUB_IND_DATA) & firmware))
			break;
		udelay(1);
	}

	if (i >= hwmgr->usec_timeout) {
		pr_err("SMU check loaded firmware failed.\n");
		return -EINVAL;
	}

	return 0;
}

static int smu8_load_mec_firmware(struct pp_hwmgr *hwmgr)
{
	uint32_t reg_data;
	uint32_t tmp;
	int ret = 0;
	struct cgs_firmware_info info = {0};
	struct smu8_smumgr *smu8_smu;

	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	smu8_smu = hwmgr->smu_backend;
	ret = cgs_get_firmware_info(hwmgr->device,
						CGS_UCODE_ID_CP_MEC, &info);

	if (ret)
		return -EINVAL;

	/* Disable MEC parsing/prefetching */
	tmp = cgs_read_register(hwmgr->device,
					mmCP_MEC_CNTL);
	tmp = PHM_SET_FIELD(tmp, CP_MEC_CNTL, MEC_ME1_HALT, 1);
	tmp = PHM_SET_FIELD(tmp, CP_MEC_CNTL, MEC_ME2_HALT, 1);
	cgs_write_register(hwmgr->device, mmCP_MEC_CNTL, tmp);

	tmp = cgs_read_register(hwmgr->device,
					mmCP_CPC_IC_BASE_CNTL);

	tmp = PHM_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, VMID, 0);
	tmp = PHM_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, ATC, 0);
	tmp = PHM_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, CACHE_POLICY, 0);
	tmp = PHM_SET_FIELD(tmp, CP_CPC_IC_BASE_CNTL, MTYPE, 1);
	cgs_write_register(hwmgr->device, mmCP_CPC_IC_BASE_CNTL, tmp);

	reg_data = lower_32_bits(info.mc_addr) &
			PHM_FIELD_MASK(CP_CPC_IC_BASE_LO, IC_BASE_LO);
	cgs_write_register(hwmgr->device, mmCP_CPC_IC_BASE_LO, reg_data);

	reg_data = upper_32_bits(info.mc_addr) &
			PHM_FIELD_MASK(CP_CPC_IC_BASE_HI, IC_BASE_HI);
	cgs_write_register(hwmgr->device, mmCP_CPC_IC_BASE_HI, reg_data);

	return 0;
}

static uint8_t smu8_translate_firmware_enum_to_arg(struct pp_hwmgr *hwmgr,
			enum smu8_scratch_entry firmware_enum)
{
	uint8_t ret = 0;

	switch (firmware_enum) {
	case SMU8_SCRATCH_ENTRY_UCODE_ID_SDMA0:
		ret = UCODE_ID_SDMA0;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_SDMA1:
		if (hwmgr->chip_id == CHIP_STONEY)
			ret = UCODE_ID_SDMA0;
		else
			ret = UCODE_ID_SDMA1;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_CP_CE:
		ret = UCODE_ID_CP_CE;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_CP_PFP:
		ret = UCODE_ID_CP_PFP;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_CP_ME:
		ret = UCODE_ID_CP_ME;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1:
		ret = UCODE_ID_CP_MEC_JT1;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2:
		if (hwmgr->chip_id == CHIP_STONEY)
			ret = UCODE_ID_CP_MEC_JT1;
		else
			ret = UCODE_ID_CP_MEC_JT2;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_GMCON_RENG:
		ret = UCODE_ID_GMCON_RENG;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_G:
		ret = UCODE_ID_RLC_G;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH:
		ret = UCODE_ID_RLC_SCRATCH;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM:
		ret = UCODE_ID_RLC_SRM_ARAM;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM:
		ret = UCODE_ID_RLC_SRM_DRAM;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_DMCU_ERAM:
		ret = UCODE_ID_DMCU_ERAM;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_DMCU_IRAM:
		ret = UCODE_ID_DMCU_IRAM;
		break;
	case SMU8_SCRATCH_ENTRY_UCODE_ID_POWER_PROFILING:
		ret = TASK_ARG_INIT_MM_PWR_LOG;
		break;
	case SMU8_SCRATCH_ENTRY_DATA_ID_SDMA_HALT:
	case SMU8_SCRATCH_ENTRY_DATA_ID_SYS_CLOCKGATING:
	case SMU8_SCRATCH_ENTRY_DATA_ID_SDMA_RING_REGS:
	case SMU8_SCRATCH_ENTRY_DATA_ID_NONGFX_REINIT:
	case SMU8_SCRATCH_ENTRY_DATA_ID_SDMA_START:
	case SMU8_SCRATCH_ENTRY_DATA_ID_IH_REGISTERS:
		ret = TASK_ARG_REG_MMIO;
		break;
	case SMU8_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE:
		ret = TASK_ARG_INIT_CLK_TABLE;
		break;
	}

	return ret;
}

static enum cgs_ucode_id smu8_convert_fw_type_to_cgs(uint32_t fw_type)
{
	enum cgs_ucode_id result = CGS_UCODE_ID_MAXIMUM;

	switch (fw_type) {
	case UCODE_ID_SDMA0:
		result = CGS_UCODE_ID_SDMA0;
		break;
	case UCODE_ID_SDMA1:
		result = CGS_UCODE_ID_SDMA1;
		break;
	case UCODE_ID_CP_CE:
		result = CGS_UCODE_ID_CP_CE;
		break;
	case UCODE_ID_CP_PFP:
		result = CGS_UCODE_ID_CP_PFP;
		break;
	case UCODE_ID_CP_ME:
		result = CGS_UCODE_ID_CP_ME;
		break;
	case UCODE_ID_CP_MEC_JT1:
		result = CGS_UCODE_ID_CP_MEC_JT1;
		break;
	case UCODE_ID_CP_MEC_JT2:
		result = CGS_UCODE_ID_CP_MEC_JT2;
		break;
	case UCODE_ID_RLC_G:
		result = CGS_UCODE_ID_RLC_G;
		break;
	default:
		break;
	}

	return result;
}

static int smu8_smu_populate_single_scratch_task(
			struct pp_hwmgr *hwmgr,
			enum smu8_scratch_entry fw_enum,
			uint8_t type, bool is_last)
{
	uint8_t i;
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	struct TOC *toc = (struct TOC *)smu8_smu->toc_buffer.kaddr;
	struct SMU_Task *task = &toc->tasks[smu8_smu->toc_entry_used_count++];

	task->type = type;
	task->arg = smu8_translate_firmware_enum_to_arg(hwmgr, fw_enum);
	task->next = is_last ? END_OF_TASK_LIST : smu8_smu->toc_entry_used_count;

	for (i = 0; i < smu8_smu->scratch_buffer_length; i++)
		if (smu8_smu->scratch_buffer[i].firmware_ID == fw_enum)
			break;

	if (i >= smu8_smu->scratch_buffer_length) {
		pr_err("Invalid Firmware Type\n");
		return -EINVAL;
	}

	task->addr.low = lower_32_bits(smu8_smu->scratch_buffer[i].mc_addr);
	task->addr.high = upper_32_bits(smu8_smu->scratch_buffer[i].mc_addr);
	task->size_bytes = smu8_smu->scratch_buffer[i].data_size;

	if (SMU8_SCRATCH_ENTRY_DATA_ID_IH_REGISTERS == fw_enum) {
		struct smu8_ih_meta_data *pIHReg_restore =
		     (struct smu8_ih_meta_data *)smu8_smu->scratch_buffer[i].kaddr;
		pIHReg_restore->command =
			METADATA_CMD_MODE0 | METADATA_PERFORM_ON_LOAD;
	}

	return 0;
}

static int smu8_smu_populate_single_ucode_load_task(
					struct pp_hwmgr *hwmgr,
					enum smu8_scratch_entry fw_enum,
					bool is_last)
{
	uint8_t i;
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	struct TOC *toc = (struct TOC *)smu8_smu->toc_buffer.kaddr;
	struct SMU_Task *task = &toc->tasks[smu8_smu->toc_entry_used_count++];

	task->type = TASK_TYPE_UCODE_LOAD;
	task->arg = smu8_translate_firmware_enum_to_arg(hwmgr, fw_enum);
	task->next = is_last ? END_OF_TASK_LIST : smu8_smu->toc_entry_used_count;

	for (i = 0; i < smu8_smu->driver_buffer_length; i++)
		if (smu8_smu->driver_buffer[i].firmware_ID == fw_enum)
			break;

	if (i >= smu8_smu->driver_buffer_length) {
		pr_err("Invalid Firmware Type\n");
		return -EINVAL;
	}

	task->addr.low = lower_32_bits(smu8_smu->driver_buffer[i].mc_addr);
	task->addr.high = upper_32_bits(smu8_smu->driver_buffer[i].mc_addr);
	task->size_bytes = smu8_smu->driver_buffer[i].data_size;

	return 0;
}

static int smu8_smu_construct_toc_for_rlc_aram_save(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;

	smu8_smu->toc_entry_aram = smu8_smu->toc_entry_used_count;
	smu8_smu_populate_single_scratch_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM,
				TASK_TYPE_UCODE_SAVE, true);

	return 0;
}

static int smu8_smu_initialize_toc_empty_job_list(struct pp_hwmgr *hwmgr)
{
	int i;
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	struct TOC *toc = (struct TOC *)smu8_smu->toc_buffer.kaddr;

	for (i = 0; i < NUM_JOBLIST_ENTRIES; i++)
		toc->JobList[i] = (uint8_t)IGNORE_JOB;

	return 0;
}

static int smu8_smu_construct_toc_for_vddgfx_enter(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	struct TOC *toc = (struct TOC *)smu8_smu->toc_buffer.kaddr;

	toc->JobList[JOB_GFX_SAVE] = (uint8_t)smu8_smu->toc_entry_used_count;
	smu8_smu_populate_single_scratch_task(hwmgr,
				    SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH,
				    TASK_TYPE_UCODE_SAVE, false);

	smu8_smu_populate_single_scratch_task(hwmgr,
				    SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM,
				    TASK_TYPE_UCODE_SAVE, true);

	return 0;
}


static int smu8_smu_construct_toc_for_vddgfx_exit(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	struct TOC *toc = (struct TOC *)smu8_smu->toc_buffer.kaddr;

	toc->JobList[JOB_GFX_RESTORE] = (uint8_t)smu8_smu->toc_entry_used_count;

	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_CE, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_PFP, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_ME, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1, false);

	if (hwmgr->chip_id == CHIP_STONEY)
		smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1, false);
	else
		smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2, false);

	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_G, false);

	/* populate scratch */
	smu8_smu_populate_single_scratch_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH,
				TASK_TYPE_UCODE_LOAD, false);

	smu8_smu_populate_single_scratch_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM,
				TASK_TYPE_UCODE_LOAD, false);

	smu8_smu_populate_single_scratch_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM,
				TASK_TYPE_UCODE_LOAD, true);

	return 0;
}

static int smu8_smu_construct_toc_for_power_profiling(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;

	smu8_smu->toc_entry_power_profiling_index = smu8_smu->toc_entry_used_count;

	smu8_smu_populate_single_scratch_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_POWER_PROFILING,
				TASK_TYPE_INITIALIZE, true);
	return 0;
}

static int smu8_smu_construct_toc_for_bootup(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;

	smu8_smu->toc_entry_initialize_index = smu8_smu->toc_entry_used_count;

	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_SDMA0, false);
	if (hwmgr->chip_id != CHIP_STONEY)
		smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_SDMA1, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_CE, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_PFP, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_ME, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT1, false);
	if (hwmgr->chip_id != CHIP_STONEY)
		smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_CP_MEC_JT2, false);
	smu8_smu_populate_single_ucode_load_task(hwmgr,
				SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_G, true);

	return 0;
}

static int smu8_smu_construct_toc_for_clock_table(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;

	smu8_smu->toc_entry_clock_table = smu8_smu->toc_entry_used_count;

	smu8_smu_populate_single_scratch_task(hwmgr,
				SMU8_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE,
				TASK_TYPE_INITIALIZE, true);

	return 0;
}

static int smu8_smu_construct_toc(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;

	smu8_smu->toc_entry_used_count = 0;
	smu8_smu_initialize_toc_empty_job_list(hwmgr);
	smu8_smu_construct_toc_for_rlc_aram_save(hwmgr);
	smu8_smu_construct_toc_for_vddgfx_enter(hwmgr);
	smu8_smu_construct_toc_for_vddgfx_exit(hwmgr);
	smu8_smu_construct_toc_for_power_profiling(hwmgr);
	smu8_smu_construct_toc_for_bootup(hwmgr);
	smu8_smu_construct_toc_for_clock_table(hwmgr);

	return 0;
}

static int smu8_smu_populate_firmware_entries(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	uint32_t firmware_type;
	uint32_t i;
	int ret;
	enum cgs_ucode_id ucode_id;
	struct cgs_firmware_info info = {0};

	smu8_smu->driver_buffer_length = 0;

	for (i = 0; i < ARRAY_SIZE(firmware_list); i++) {

		firmware_type = smu8_translate_firmware_enum_to_arg(hwmgr,
					firmware_list[i]);

		ucode_id = smu8_convert_fw_type_to_cgs(firmware_type);

		ret = cgs_get_firmware_info(hwmgr->device,
							ucode_id, &info);

		if (ret == 0) {
			smu8_smu->driver_buffer[i].mc_addr = info.mc_addr;

			smu8_smu->driver_buffer[i].data_size = info.image_size;

			smu8_smu->driver_buffer[i].firmware_ID = firmware_list[i];
			smu8_smu->driver_buffer_length++;
		}
	}

	return 0;
}

static int smu8_smu_populate_single_scratch_entry(
				struct pp_hwmgr *hwmgr,
				enum smu8_scratch_entry scratch_type,
				uint32_t ulsize_byte,
				struct smu8_buffer_entry *entry)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	uint32_t ulsize_aligned = SIZE_ALIGN_32(ulsize_byte);

	entry->data_size = ulsize_byte;
	entry->kaddr = (char *) smu8_smu->smu_buffer.kaddr +
				smu8_smu->smu_buffer_used_bytes;
	entry->mc_addr = smu8_smu->smu_buffer.mc_addr + smu8_smu->smu_buffer_used_bytes;
	entry->firmware_ID = scratch_type;

	smu8_smu->smu_buffer_used_bytes += ulsize_aligned;

	return 0;
}

static int smu8_download_pptable_settings(struct pp_hwmgr *hwmgr, void **table)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	unsigned long i;

	for (i = 0; i < smu8_smu->scratch_buffer_length; i++) {
		if (smu8_smu->scratch_buffer[i].firmware_ID
			== SMU8_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE)
			break;
	}

	*table = (struct SMU8_Fusion_ClkTable *)smu8_smu->scratch_buffer[i].kaddr;

	smu8_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetClkTableAddrHi,
				upper_32_bits(smu8_smu->scratch_buffer[i].mc_addr));

	smu8_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetClkTableAddrLo,
				lower_32_bits(smu8_smu->scratch_buffer[i].mc_addr));

	smu8_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_ExecuteJob,
				smu8_smu->toc_entry_clock_table);

	smu8_send_msg_to_smc(hwmgr, PPSMC_MSG_ClkTableXferToDram);

	return 0;
}

static int smu8_upload_pptable_settings(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	unsigned long i;

	for (i = 0; i < smu8_smu->scratch_buffer_length; i++) {
		if (smu8_smu->scratch_buffer[i].firmware_ID
				== SMU8_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE)
			break;
	}

	smu8_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetClkTableAddrHi,
				upper_32_bits(smu8_smu->scratch_buffer[i].mc_addr));

	smu8_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetClkTableAddrLo,
				lower_32_bits(smu8_smu->scratch_buffer[i].mc_addr));

	smu8_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_ExecuteJob,
				smu8_smu->toc_entry_clock_table);

	smu8_send_msg_to_smc(hwmgr, PPSMC_MSG_ClkTableXferToSmu);

	return 0;
}

static int smu8_request_smu_load_fw(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu = hwmgr->smu_backend;
	uint32_t smc_address;
	uint32_t fw_to_check = 0;
	int ret;

	amdgpu_ucode_init_bo(hwmgr->adev);

	smu8_smu_populate_firmware_entries(hwmgr);

	smu8_smu_construct_toc(hwmgr);

	smc_address = SMU8_FIRMWARE_HEADER_LOCATION +
		offsetof(struct SMU8_Firmware_Header, UcodeLoadStatus);

	smu8_write_smc_sram_dword(hwmgr, smc_address, 0, smc_address+4);

	smu8_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DriverDramAddrHi,
					upper_32_bits(smu8_smu->toc_buffer.mc_addr));

	smu8_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_DriverDramAddrLo,
					lower_32_bits(smu8_smu->toc_buffer.mc_addr));

	smu8_send_msg_to_smc(hwmgr, PPSMC_MSG_InitJobs);

	smu8_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_ExecuteJob,
					smu8_smu->toc_entry_aram);
	smu8_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_ExecuteJob,
				smu8_smu->toc_entry_power_profiling_index);

	smu8_send_msg_to_smc_with_parameter(hwmgr,
					PPSMC_MSG_ExecuteJob,
					smu8_smu->toc_entry_initialize_index);

	fw_to_check = UCODE_ID_RLC_G_MASK |
			UCODE_ID_SDMA0_MASK |
			UCODE_ID_SDMA1_MASK |
			UCODE_ID_CP_CE_MASK |
			UCODE_ID_CP_ME_MASK |
			UCODE_ID_CP_PFP_MASK |
			UCODE_ID_CP_MEC_JT1_MASK |
			UCODE_ID_CP_MEC_JT2_MASK;

	if (hwmgr->chip_id == CHIP_STONEY)
		fw_to_check &= ~(UCODE_ID_SDMA1_MASK | UCODE_ID_CP_MEC_JT2_MASK);

	ret = smu8_check_fw_load_finish(hwmgr, fw_to_check);
	if (ret) {
		pr_err("SMU firmware load failed\n");
		return ret;
	}

	ret = smu8_load_mec_firmware(hwmgr);
	if (ret) {
		pr_err("Mec Firmware load failed\n");
		return ret;
	}

	return 0;
}

static int smu8_start_smu(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	uint32_t index = SMN_MP1_SRAM_START_ADDR +
			 SMU8_FIRMWARE_HEADER_LOCATION +
			 offsetof(struct SMU8_Firmware_Header, Version);


	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	cgs_write_register(hwmgr->device, mmMP0PUB_IND_INDEX, index);
	hwmgr->smu_version = cgs_read_register(hwmgr->device, mmMP0PUB_IND_DATA);
	adev->pm.fw_version = hwmgr->smu_version >> 8;

	return smu8_request_smu_load_fw(hwmgr);
}

static int smu8_smu_init(struct pp_hwmgr *hwmgr)
{
	int ret = 0;
	struct smu8_smumgr *smu8_smu;

	smu8_smu = kzalloc(sizeof(struct smu8_smumgr), GFP_KERNEL);
	if (smu8_smu == NULL)
		return -ENOMEM;

	hwmgr->smu_backend = smu8_smu;

	smu8_smu->toc_buffer.data_size = 4096;
	smu8_smu->smu_buffer.data_size =
		ALIGN(UCODE_ID_RLC_SCRATCH_SIZE_BYTE, 32) +
		ALIGN(UCODE_ID_RLC_SRM_ARAM_SIZE_BYTE, 32) +
		ALIGN(UCODE_ID_RLC_SRM_DRAM_SIZE_BYTE, 32) +
		ALIGN(sizeof(struct SMU8_MultimediaPowerLogData), 32) +
		ALIGN(sizeof(struct SMU8_Fusion_ClkTable), 32);

	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				smu8_smu->toc_buffer.data_size,
				PAGE_SIZE,
				AMDGPU_GEM_DOMAIN_VRAM,
				&smu8_smu->toc_buffer.handle,
				&smu8_smu->toc_buffer.mc_addr,
				&smu8_smu->toc_buffer.kaddr);
	if (ret)
		goto err2;

	ret = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
				smu8_smu->smu_buffer.data_size,
				PAGE_SIZE,
				AMDGPU_GEM_DOMAIN_VRAM,
				&smu8_smu->smu_buffer.handle,
				&smu8_smu->smu_buffer.mc_addr,
				&smu8_smu->smu_buffer.kaddr);
	if (ret)
		goto err1;

	if (0 != smu8_smu_populate_single_scratch_entry(hwmgr,
		SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SCRATCH,
		UCODE_ID_RLC_SCRATCH_SIZE_BYTE,
		&smu8_smu->scratch_buffer[smu8_smu->scratch_buffer_length++])) {
		pr_err("Error when Populate Firmware Entry.\n");
		goto err0;
	}

	if (0 != smu8_smu_populate_single_scratch_entry(hwmgr,
		SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_ARAM,
		UCODE_ID_RLC_SRM_ARAM_SIZE_BYTE,
		&smu8_smu->scratch_buffer[smu8_smu->scratch_buffer_length++])) {
		pr_err("Error when Populate Firmware Entry.\n");
		goto err0;
	}
	if (0 != smu8_smu_populate_single_scratch_entry(hwmgr,
		SMU8_SCRATCH_ENTRY_UCODE_ID_RLC_SRM_DRAM,
		UCODE_ID_RLC_SRM_DRAM_SIZE_BYTE,
		&smu8_smu->scratch_buffer[smu8_smu->scratch_buffer_length++])) {
		pr_err("Error when Populate Firmware Entry.\n");
		goto err0;
	}

	if (0 != smu8_smu_populate_single_scratch_entry(hwmgr,
		SMU8_SCRATCH_ENTRY_UCODE_ID_POWER_PROFILING,
		sizeof(struct SMU8_MultimediaPowerLogData),
		&smu8_smu->scratch_buffer[smu8_smu->scratch_buffer_length++])) {
		pr_err("Error when Populate Firmware Entry.\n");
		goto err0;
	}

	if (0 != smu8_smu_populate_single_scratch_entry(hwmgr,
		SMU8_SCRATCH_ENTRY_SMU8_FUSION_CLKTABLE,
		sizeof(struct SMU8_Fusion_ClkTable),
		&smu8_smu->scratch_buffer[smu8_smu->scratch_buffer_length++])) {
		pr_err("Error when Populate Firmware Entry.\n");
		goto err0;
	}

	return 0;

err0:
	amdgpu_bo_free_kernel(&smu8_smu->smu_buffer.handle,
				&smu8_smu->smu_buffer.mc_addr,
				&smu8_smu->smu_buffer.kaddr);
err1:
	amdgpu_bo_free_kernel(&smu8_smu->toc_buffer.handle,
				&smu8_smu->toc_buffer.mc_addr,
				&smu8_smu->toc_buffer.kaddr);
err2:
	kfree(smu8_smu);
	return -EINVAL;
}

static int smu8_smu_fini(struct pp_hwmgr *hwmgr)
{
	struct smu8_smumgr *smu8_smu;

	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	smu8_smu = hwmgr->smu_backend;
	if (smu8_smu) {
		amdgpu_bo_free_kernel(&smu8_smu->toc_buffer.handle,
					&smu8_smu->toc_buffer.mc_addr,
					&smu8_smu->toc_buffer.kaddr);
		amdgpu_bo_free_kernel(&smu8_smu->smu_buffer.handle,
					&smu8_smu->smu_buffer.mc_addr,
					&smu8_smu->smu_buffer.kaddr);
		kfree(smu8_smu);
	}

	return 0;
}

static bool smu8_dpm_check_smu_features(struct pp_hwmgr *hwmgr,
				unsigned long check_feature)
{
	int result;
	unsigned long features;

	result = smu8_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_GetFeatureStatus, 0);
	if (result == 0) {
		features = smum_get_argument(hwmgr);
		if (features & check_feature)
			return true;
	}

	return false;
}

static bool smu8_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	if (smu8_dpm_check_smu_features(hwmgr, SMU_EnabledFeatureScoreboard_SclkDpmOn))
		return true;
	return false;
}

const struct pp_smumgr_func smu8_smu_funcs = {
	.smu_init = smu8_smu_init,
	.smu_fini = smu8_smu_fini,
	.start_smu = smu8_start_smu,
	.check_fw_load_finish = smu8_check_fw_load_finish,
	.request_smu_load_fw = NULL,
	.request_smu_load_specific_fw = NULL,
	.get_argument = smu8_get_argument,
	.send_msg_to_smc = smu8_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = smu8_send_msg_to_smc_with_parameter,
	.download_pptable_settings = smu8_download_pptable_settings,
	.upload_pptable_settings = smu8_upload_pptable_settings,
	.is_dpm_running = smu8_is_dpm_running,
};

