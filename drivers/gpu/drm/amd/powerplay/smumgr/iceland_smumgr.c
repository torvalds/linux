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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include "smumgr.h"
#include "iceland_smumgr.h"
#include "pp_debug.h"
#include "smu_ucode_xfer_vi.h"
#include "ppsmc.h"
#include "smu/smu_7_1_1_d.h"
#include "smu/smu_7_1_1_sh_mask.h"
#include "cgs_common.h"

#define ICELAND_SMC_SIZE		0x20000
#define BUFFER_SIZE			80000
#define MAX_STRING_SIZE			15
#define BUFFER_SIZETWO              	131072 /*128 *1024*/

/**
 * Set the address for reading/writing the SMC SRAM space.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    smcAddress the address in the SMC RAM to access.
 */
static int iceland_set_smc_sram_address(struct pp_smumgr *smumgr,
				uint32_t smcAddress, uint32_t limit)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;
	PP_ASSERT_WITH_CODE((0 == (3 & smcAddress)),
		"SMC address must be 4 byte aligned.",
		return -1;);

	PP_ASSERT_WITH_CODE((limit > (smcAddress + 3)),
		"SMC address is beyond the SMC RAM area.",
		return -1;);

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_0, smcAddress);
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 0);

	return 0;
}

/**
 * Copy bytes from an array into the SMC RAM space.
 *
 * @param    smumgr  the address of the powerplay SMU manager.
 * @param    smcStartAddress the start address in the SMC RAM to copy bytes to.
 * @param    src the byte array to copy the bytes from.
 * @param    byteCount the number of bytes to copy.
 */
int iceland_copy_bytes_to_smc(struct pp_smumgr *smumgr,
		uint32_t smcStartAddress, const uint8_t *src,
		uint32_t byteCount, uint32_t limit)
{
	uint32_t addr;
	uint32_t data, orig_data;
	int result = 0;
	uint32_t extra_shift;

	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;
	PP_ASSERT_WITH_CODE((0 == (3 & smcStartAddress)),
		"SMC address must be 4 byte aligned.",
		return 0;);

	PP_ASSERT_WITH_CODE((limit > (smcStartAddress + byteCount)),
		"SMC address is beyond the SMC RAM area.",
		return 0;);

	addr = smcStartAddress;

	while (byteCount >= 4) {
		/*
		 * Bytes are written into the
		 * SMC address space with the MSB first
		 */
		data = (src[0] << 24) + (src[1] << 16) + (src[2] << 8) + src[3];

		result = iceland_set_smc_sram_address(smumgr, addr, limit);

		if (result)
			goto out;

		cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, data);

		src += 4;
		byteCount -= 4;
		addr += 4;
	}

	if (0 != byteCount) {
		/* Now write odd bytes left, do a read modify write cycle */
		data = 0;

		result = iceland_set_smc_sram_address(smumgr, addr, limit);
		if (result)
			goto out;

		orig_data = cgs_read_register(smumgr->device,
							mmSMC_IND_DATA_0);
		extra_shift = 8 * (4 - byteCount);

		while (byteCount > 0) {
			data = (data << 8) + *src++;
			byteCount--;
		}

		data <<= extra_shift;
		data |= (orig_data & ~((~0UL) << extra_shift));

		result = iceland_set_smc_sram_address(smumgr, addr, limit);
		if (result)
			goto out;

		cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, data);
	}

out:
	return result;
}

/**
 * Deassert the reset'pin' (set it to high).
 *
 * @param smumgr  the address of the powerplay hardware manager.
 */
static int iceland_start_smc(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	return 0;
}

static void iceland_pp_reset_smc(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_RESET_CNTL,
				  rst_reg, 1);
}

int iceland_program_jump_on_start(struct pp_smumgr *smumgr)
{
	static const unsigned char pData[] = { 0xE0, 0x00, 0x80, 0x40 };

	iceland_copy_bytes_to_smc(smumgr, 0x0, pData, 4, sizeof(pData)+1);

	return 0;
}

/**
 * Return if the SMC is currently running.
 *
 * @param    smumgr  the address of the powerplay hardware manager.
 */
bool iceland_is_smc_ram_running(struct pp_smumgr *smumgr)
{
	uint32_t val1, val2;

	val1 = SMUM_READ_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable);
	val2 = cgs_read_ind_register(smumgr->device, CGS_IND_REG__SMC,
				     ixSMC_PC_C);

	return ((0 == val1) && (0x20100 <= val2));
}

/**
 * Send a message to the SMC, and wait for its response.
 *
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    msg the message to send.
 * @return   The response that came from the SMC.
 */
static int iceland_send_msg_to_smc(struct pp_smumgr *smumgr, uint16_t msg)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	if (!iceland_is_smc_ram_running(smumgr))
		return -EINVAL;

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);
	PP_ASSERT_WITH_CODE(
		1 == SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP),
		"Failed to send Previous Message.",
		);

	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, msg);

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);
	PP_ASSERT_WITH_CODE(
		1 == SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP),
		"Failed to send Message.",
		);

	return 0;
}

/**
 * Send a message to the SMC with parameter
 *
 * @param    smumgr:  the address of the powerplay hardware manager.
 * @param    msg: the message to send.
 * @param    parameter: the parameter to send
 * @return   The response that came from the SMC.
 */
static int iceland_send_msg_to_smc_with_parameter(struct pp_smumgr *smumgr,
				uint16_t msg, uint32_t parameter)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, parameter);

	return iceland_send_msg_to_smc(smumgr, msg);
}

/*
 * Read a 32bit value from the SMC SRAM space.
 * ALL PARAMETERS ARE IN HOST BYTE ORDER.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    smcAddress the address in the SMC RAM to access.
 * @param    value and output parameter for the data read from the SMC SRAM.
 */
int iceland_read_smc_sram_dword(struct pp_smumgr *smumgr,
				uint32_t smcAddress, uint32_t *value,
				uint32_t limit)
{
	int result;

	result = iceland_set_smc_sram_address(smumgr, smcAddress, limit);

	if (0 != result)
		return result;

	*value = cgs_read_register(smumgr->device, mmSMC_IND_DATA_0);

	return 0;
}

/*
 * Write a 32bit value to the SMC SRAM space.
 * ALL PARAMETERS ARE IN HOST BYTE ORDER.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    smcAddress the address in the SMC RAM to access.
 * @param    value to write to the SMC SRAM.
 */
int iceland_write_smc_sram_dword(struct pp_smumgr *smumgr,
				 uint32_t smcAddress, uint32_t value,
				 uint32_t limit)
{
	int result;

	result = iceland_set_smc_sram_address(smumgr, smcAddress, limit);

	if (0 != result)
		return result;

	cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, value);

	return 0;
}

static int iceland_smu_fini(struct pp_smumgr *smumgr)
{
	struct iceland_smumgr *priv = (struct iceland_smumgr *)(smumgr->backend);

	smu_free_memory(smumgr->device, (void *)priv->header_buffer.handle);

	if (smumgr->backend != NULL) {
		kfree(smumgr->backend);
		smumgr->backend = NULL;
	}

	cgs_rel_firmware(smumgr->device, CGS_UCODE_ID_SMU);
	return 0;
}

static enum cgs_ucode_id iceland_convert_fw_type_to_cgs(uint32_t fw_type)
{
	enum cgs_ucode_id result = CGS_UCODE_ID_MAXIMUM;

	switch (fw_type) {
	case UCODE_ID_SMU:
		result = CGS_UCODE_ID_SMU;
		break;
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
	case UCODE_ID_CP_MEC:
		result = CGS_UCODE_ID_CP_MEC;
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

/**
 * Convert the PPIRI firmware type to SMU type mask.
 * For MEC, we need to check all MEC related type
 */
static uint16_t iceland_get_mask_for_firmware_type(uint16_t firmwareType)
{
	uint16_t result = 0;

	switch (firmwareType) {
	case UCODE_ID_SDMA0:
		result = UCODE_ID_SDMA0_MASK;
		break;
	case UCODE_ID_SDMA1:
		result = UCODE_ID_SDMA1_MASK;
		break;
	case UCODE_ID_CP_CE:
		result = UCODE_ID_CP_CE_MASK;
		break;
	case UCODE_ID_CP_PFP:
		result = UCODE_ID_CP_PFP_MASK;
		break;
	case UCODE_ID_CP_ME:
		result = UCODE_ID_CP_ME_MASK;
		break;
	case UCODE_ID_CP_MEC:
	case UCODE_ID_CP_MEC_JT1:
	case UCODE_ID_CP_MEC_JT2:
		result = UCODE_ID_CP_MEC_MASK;
		break;
	case UCODE_ID_RLC_G:
		result = UCODE_ID_RLC_G_MASK;
		break;
	default:
		break;
	}

	return result;
}

/**
 * Check if the FW has been loaded,
 * SMU will not return if loading has not finished.
*/
static int iceland_check_fw_load_finish(struct pp_smumgr *smumgr, uint32_t fwType)
{
	uint16_t fwMask = iceland_get_mask_for_firmware_type(fwType);

	if (0 != SMUM_WAIT_VFPF_INDIRECT_REGISTER(smumgr, SMC_IND,
				SOFT_REGISTERS_TABLE_27, fwMask, fwMask)) {
		pr_err("[ powerplay ] check firmware loading failed\n");
		return -EINVAL;
	}

	return 0;
}

/* Populate one firmware image to the data structure */
static int iceland_populate_single_firmware_entry(struct pp_smumgr *smumgr,
				uint16_t firmware_type,
				struct SMU_Entry *pentry)
{
	int result;
	struct cgs_firmware_info info = {0};

	result = cgs_get_firmware_info(
				smumgr->device,
				iceland_convert_fw_type_to_cgs(firmware_type),
				&info);

	if (result == 0) {
		pentry->version = 0;
		pentry->id = (uint16_t)firmware_type;
		pentry->image_addr_high = smu_upper_32_bits(info.mc_addr);
		pentry->image_addr_low = smu_lower_32_bits(info.mc_addr);
		pentry->meta_data_addr_high = 0;
		pentry->meta_data_addr_low = 0;
		pentry->data_size_byte = info.image_size;
		pentry->num_register_entries = 0;

		if (firmware_type == UCODE_ID_RLC_G)
			pentry->flags = 1;
		else
			pentry->flags = 0;
	} else {
		return result;
	}

	return result;
}

static void iceland_pp_stop_smc_clock(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_CLOCK_CNTL_0,
				  ck_disable, 1);
}

static void iceland_start_smc_clock(struct pp_smumgr *smumgr)
{
	SMUM_WRITE_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_CLOCK_CNTL_0,
				  ck_disable, 0);
}

int iceland_smu_start_smc(struct pp_smumgr *smumgr)
{
	/* set smc instruct start point at 0x0 */
	iceland_program_jump_on_start(smumgr);

	/* enable smc clock */
	iceland_start_smc_clock(smumgr);

	/* de-assert reset */
	iceland_start_smc(smumgr);

	SMUM_WAIT_INDIRECT_FIELD(smumgr, SMC_IND, FIRMWARE_FLAGS,
				 INTERRUPTS_ENABLED, 1);

	return 0;
}

/**
 * Upload the SMC firmware to the SMC microcontroller.
 *
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    pFirmware the data structure containing the various sections of the firmware.
 */
int iceland_smu_upload_firmware_image(struct pp_smumgr *smumgr)
{
	const uint8_t *src;
	uint32_t byte_count, val;
	uint32_t data;
	struct cgs_firmware_info info = {0};

	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	/* load SMC firmware */
	cgs_get_firmware_info(smumgr->device,
		iceland_convert_fw_type_to_cgs(UCODE_ID_SMU), &info);

	if (info.image_size & 3) {
		pr_err("[ powerplay ] SMC ucode is not 4 bytes aligned\n");
		return -EINVAL;
	}

	if (info.image_size > ICELAND_SMC_SIZE) {
		pr_err("[ powerplay ] SMC address is beyond the SMC RAM area\n");
		return -EINVAL;
	}

	/* wait for smc boot up */
	SMUM_WAIT_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
					 RCU_UC_EVENTS, boot_seq_done, 0);

	/* clear firmware interrupt enable flag */
	val = cgs_read_ind_register(smumgr->device, CGS_IND_REG__SMC,
				    ixSMC_SYSCON_MISC_CNTL);
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
			       ixSMC_SYSCON_MISC_CNTL, val | 1);

	/* stop smc clock */
	iceland_pp_stop_smc_clock(smumgr);

	/* reset smc */
	iceland_pp_reset_smc(smumgr);

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_0,
			   info.ucode_start_address);

	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL,
			 AUTO_INCREMENT_IND_0, 1);

	byte_count = info.image_size;
	src = (const uint8_t *)info.kptr;

	while (byte_count >= 4) {
		data = (src[0] << 24) + (src[1] << 16) + (src[2] << 8) + src[3];
		cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, data);
		src += 4;
		byte_count -= 4;
	}

	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL,
			 AUTO_INCREMENT_IND_0, 0);

	return 0;
}

static int iceland_request_smu_reload_fw(struct pp_smumgr *smumgr)
{
	struct iceland_smumgr *iceland_smu =
		(struct iceland_smumgr *)(smumgr->backend);
	uint16_t fw_to_load;
	int result = 0;
	struct SMU_DRAMData_TOC *toc;

	toc = (struct SMU_DRAMData_TOC *)iceland_smu->pHeader;
	toc->num_entries = 0;
	toc->structure_version = 1;

	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry(smumgr,
		UCODE_ID_RLC_G,
		&toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry(smumgr,
		UCODE_ID_CP_CE,
		&toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_PFP, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_ME, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_MEC, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_MEC_JT1, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_MEC_JT2, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry
		(smumgr, UCODE_ID_SDMA0, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == iceland_populate_single_firmware_entry
		(smumgr, UCODE_ID_SDMA1, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);

	if (!iceland_is_smc_ram_running(smumgr)) {
		result = iceland_smu_upload_firmware_image(smumgr);
		if (result)
			return result;

		result = iceland_smu_start_smc(smumgr);
		if (result)
			return result;
	}

	iceland_send_msg_to_smc_with_parameter(smumgr,
		PPSMC_MSG_DRV_DRAM_ADDR_HI,
		iceland_smu->header_buffer.mc_addr_high);

	iceland_send_msg_to_smc_with_parameter(smumgr,
		PPSMC_MSG_DRV_DRAM_ADDR_LO,
		iceland_smu->header_buffer.mc_addr_low);

	fw_to_load = UCODE_ID_RLC_G_MASK
			+ UCODE_ID_SDMA0_MASK
			+ UCODE_ID_SDMA1_MASK
			+ UCODE_ID_CP_CE_MASK
			+ UCODE_ID_CP_ME_MASK
			+ UCODE_ID_CP_PFP_MASK
			+ UCODE_ID_CP_MEC_MASK
			+ UCODE_ID_CP_MEC_JT1_MASK
			+ UCODE_ID_CP_MEC_JT2_MASK;

	PP_ASSERT_WITH_CODE(
		0 == iceland_send_msg_to_smc_with_parameter(
		smumgr, PPSMC_MSG_LoadUcodes, fw_to_load),
		"Fail to Request SMU Load uCode", return 0);

	return result;
}

static int iceland_request_smu_load_specific_fw(struct pp_smumgr *smumgr,
						uint32_t firmwareType)
{
	return 0;
}

static int iceland_start_smu(struct pp_smumgr *smumgr)
{
	int result;

	result = iceland_smu_upload_firmware_image(smumgr);
	if (result)
		return result;

	result = iceland_smu_start_smc(smumgr);
	if (result)
		return result;

	result = iceland_request_smu_reload_fw(smumgr);

	return result;
}

/**
 * Write a 32bit value to the SMC SRAM space.
 * ALL PARAMETERS ARE IN HOST BYTE ORDER.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    smcAddress the address in the SMC RAM to access.
 * @param    value to write to the SMC SRAM.
 */
static int iceland_smu_init(struct pp_smumgr *smumgr)
{
	struct iceland_smumgr *iceland_smu;
	uint64_t mc_addr = 0;

	/* Allocate memory for backend private data */
	iceland_smu = (struct iceland_smumgr *)(smumgr->backend);
	iceland_smu->header_buffer.data_size =
		((sizeof(struct SMU_DRAMData_TOC) / 4096) + 1) * 4096;

	smu_allocate_memory(smumgr->device,
		iceland_smu->header_buffer.data_size,
		CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB,
		PAGE_SIZE,
		&mc_addr,
		&iceland_smu->header_buffer.kaddr,
		&iceland_smu->header_buffer.handle);

	iceland_smu->pHeader = iceland_smu->header_buffer.kaddr;
	iceland_smu->header_buffer.mc_addr_high = smu_upper_32_bits(mc_addr);
	iceland_smu->header_buffer.mc_addr_low = smu_lower_32_bits(mc_addr);

	PP_ASSERT_WITH_CODE((NULL != iceland_smu->pHeader),
		"Out of memory.",
		kfree(smumgr->backend);
		cgs_free_gpu_mem(smumgr->device,
		(cgs_handle_t)iceland_smu->header_buffer.handle);
		return -1);

	return 0;
}

static const struct pp_smumgr_func iceland_smu_funcs = {
	.smu_init = &iceland_smu_init,
	.smu_fini = &iceland_smu_fini,
	.start_smu = &iceland_start_smu,
	.check_fw_load_finish = &iceland_check_fw_load_finish,
	.request_smu_load_fw = &iceland_request_smu_reload_fw,
	.request_smu_load_specific_fw = &iceland_request_smu_load_specific_fw,
	.send_msg_to_smc = &iceland_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &iceland_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
};

int iceland_smum_init(struct pp_smumgr *smumgr)
{
	struct iceland_smumgr *iceland_smu = NULL;

	iceland_smu = kzalloc(sizeof(struct iceland_smumgr), GFP_KERNEL);

	if (iceland_smu == NULL)
		return -ENOMEM;

	smumgr->backend = iceland_smu;
	smumgr->smumgr_funcs = &iceland_smu_funcs;

	return 0;
}
