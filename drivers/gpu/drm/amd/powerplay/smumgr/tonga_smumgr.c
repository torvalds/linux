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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include "smumgr.h"
#include "tonga_smumgr.h"
#include "pp_debug.h"
#include "smu_ucode_xfer_vi.h"
#include "tonga_ppsmc.h"
#include "smu/smu_7_1_2_d.h"
#include "smu/smu_7_1_2_sh_mask.h"
#include "cgs_common.h"

#define TONGA_SMC_SIZE			0x20000
#define BUFFER_SIZE			80000
#define MAX_STRING_SIZE			15
#define BUFFER_SIZETWO              131072 /*128 *1024*/

/**
* Set the address for reading/writing the SMC SRAM space.
* @param    smumgr  the address of the powerplay hardware manager.
* @param    smcAddress the address in the SMC RAM to access.
*/
static int tonga_set_smc_sram_address(struct pp_smumgr *smumgr,
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
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 0);

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
int tonga_copy_bytes_to_smc(struct pp_smumgr *smumgr,
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

		result = tonga_set_smc_sram_address(smumgr, addr, limit);

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

		result = tonga_set_smc_sram_address(smumgr, addr, limit);
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

		result = tonga_set_smc_sram_address(smumgr, addr, limit);
		if (result)
			goto out;

		cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, data);
	}

out:
	return result;
}


int tonga_program_jump_on_start(struct pp_smumgr *smumgr)
{
	static const unsigned char pData[] = { 0xE0, 0x00, 0x80, 0x40 };

	tonga_copy_bytes_to_smc(smumgr, 0x0, pData, 4, sizeof(pData)+1);

	return 0;
}

/**
* Return if the SMC is currently running.
*
* @param    smumgr  the address of the powerplay hardware manager.
*/
static int tonga_is_smc_ram_running(struct pp_smumgr *smumgr)
{
	return ((0 == SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable))
			&& (0x20100 <= cgs_read_ind_register(smumgr->device,
					CGS_IND_REG__SMC, ixSMC_PC_C)));
}

static int tonga_send_msg_to_smc_offset(struct pp_smumgr *smumgr)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, 0x20000);
	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, PPSMC_MSG_Test);

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	return 0;
}

/**
* Send a message to the SMC, and wait for its response.
*
* @param    smumgr  the address of the powerplay hardware manager.
* @param    msg the message to send.
* @return   The response that came from the SMC.
*/
static int tonga_send_msg_to_smc(struct pp_smumgr *smumgr, uint16_t msg)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	if (!tonga_is_smc_ram_running(smumgr))
		return -1;

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

/*
* Send a message to the SMC, and do not wait for its response.
*
* @param    smumgr  the address of the powerplay hardware manager.
* @param    msg the message to send.
* @return   The response that came from the SMC.
*/
static int tonga_send_msg_to_smc_without_waiting
				(struct pp_smumgr *smumgr, uint16_t msg)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);
	PP_ASSERT_WITH_CODE(
		1 == SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP),
		"Failed to send Previous Message.",
		);
	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, msg);

	return 0;
}

/*
* Send a message to the SMC with parameter
*
* @param    smumgr:  the address of the powerplay hardware manager.
* @param    msg: the message to send.
* @param    parameter: the parameter to send
* @return   The response that came from the SMC.
*/
static int tonga_send_msg_to_smc_with_parameter(struct pp_smumgr *smumgr,
				uint16_t msg, uint32_t parameter)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	if (!tonga_is_smc_ram_running(smumgr))
		return PPSMC_Result_Failed;

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);
	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, parameter);

	return tonga_send_msg_to_smc(smumgr, msg);
}

/*
* Send a message to the SMC with parameter, do not wait for response
*
* @param    smumgr:  the address of the powerplay hardware manager.
* @param    msg: the message to send.
* @param    parameter: the parameter to send
* @return   The response that came from the SMC.
*/
static int tonga_send_msg_to_smc_with_parameter_without_waiting(
			struct pp_smumgr *smumgr,
			uint16_t msg, uint32_t parameter)
{
	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, parameter);

	return tonga_send_msg_to_smc_without_waiting(smumgr, msg);
}

/*
 * Read a 32bit value from the SMC SRAM space.
 * ALL PARAMETERS ARE IN HOST BYTE ORDER.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    smcAddress the address in the SMC RAM to access.
 * @param    value and output parameter for the data read from the SMC SRAM.
 */
int tonga_read_smc_sram_dword(struct pp_smumgr *smumgr,
					uint32_t smcAddress, uint32_t *value,
					uint32_t limit)
{
	int result;

	result = tonga_set_smc_sram_address(smumgr, smcAddress, limit);

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
int tonga_write_smc_sram_dword(struct pp_smumgr *smumgr,
					uint32_t smcAddress, uint32_t value,
					uint32_t limit)
{
	int result;

	result = tonga_set_smc_sram_address(smumgr, smcAddress, limit);

	if (0 != result)
		return result;

	cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, value);

	return 0;
}

static int tonga_smu_fini(struct pp_smumgr *smumgr)
{
	struct tonga_smumgr *priv = (struct tonga_smumgr *)(smumgr->backend);

	smu_free_memory(smumgr->device, (void *)priv->smu_buffer.handle);
	smu_free_memory(smumgr->device, (void *)priv->header_buffer.handle);

	if (smumgr->backend != NULL) {
		kfree(smumgr->backend);
		smumgr->backend = NULL;
	}

	cgs_rel_firmware(smumgr->device, CGS_UCODE_ID_SMU);
	return 0;
}

static enum cgs_ucode_id tonga_convert_fw_type_to_cgs(uint32_t fw_type)
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
static uint16_t tonga_get_mask_for_firmware_type(uint16_t firmwareType)
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
static int tonga_check_fw_load_finish(struct pp_smumgr *smumgr, uint32_t fwType)
{
	uint16_t fwMask = tonga_get_mask_for_firmware_type(fwType);

	if (0 != SMUM_WAIT_VFPF_INDIRECT_REGISTER(smumgr, SMC_IND,
				SOFT_REGISTERS_TABLE_28, fwMask, fwMask)) {
		printk(KERN_ERR "[ powerplay ] check firmware loading failed\n");
		return -EINVAL;
	}

	return 0;
}

/* Populate one firmware image to the data structure */
static int tonga_populate_single_firmware_entry(struct pp_smumgr *smumgr,
				uint16_t firmware_type,
				struct SMU_Entry *pentry)
{
	int result;
	struct cgs_firmware_info info = {0};

	result = cgs_get_firmware_info(
				smumgr->device,
				tonga_convert_fw_type_to_cgs(firmware_type),
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

static int tonga_request_smu_reload_fw(struct pp_smumgr *smumgr)
{
	struct tonga_smumgr *tonga_smu =
		(struct tonga_smumgr *)(smumgr->backend);
	uint16_t fw_to_load;
	int result = 0;
	struct SMU_DRAMData_TOC *toc;
	/**
	 * First time this gets called during SmuMgr init,
	 * we haven't processed SMU header file yet,
	 * so Soft Register Start offset is unknown.
	 * However, for this case, UcodeLoadStatus is already 0,
	 * so we can skip this if the Soft Registers Start offset is 0.
	 */
	cgs_write_ind_register(smumgr->device,
		CGS_IND_REG__SMC, ixSOFT_REGISTERS_TABLE_28, 0);

	tonga_send_msg_to_smc_with_parameter(smumgr,
		PPSMC_MSG_SMU_DRAM_ADDR_HI,
		tonga_smu->smu_buffer.mc_addr_high);
	tonga_send_msg_to_smc_with_parameter(smumgr,
		PPSMC_MSG_SMU_DRAM_ADDR_LO,
		tonga_smu->smu_buffer.mc_addr_low);

	toc = (struct SMU_DRAMData_TOC *)tonga_smu->pHeader;
	toc->num_entries = 0;
	toc->structure_version = 1;

	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry(smumgr,
		UCODE_ID_RLC_G,
		&toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry(smumgr,
		UCODE_ID_CP_CE,
		&toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n",
		return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_PFP, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_ME, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_MEC, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_MEC_JT1, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry
		(smumgr, UCODE_ID_CP_MEC_JT2, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry
		(smumgr, UCODE_ID_SDMA0, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);
	PP_ASSERT_WITH_CODE(
		0 == tonga_populate_single_firmware_entry
		(smumgr, UCODE_ID_SDMA1, &toc->entry[toc->num_entries++]),
		"Failed to Get Firmware Entry.\n", return -1);

	tonga_send_msg_to_smc_with_parameter(smumgr,
		PPSMC_MSG_DRV_DRAM_ADDR_HI,
		tonga_smu->header_buffer.mc_addr_high);
	tonga_send_msg_to_smc_with_parameter(smumgr,
		PPSMC_MSG_DRV_DRAM_ADDR_LO,
		tonga_smu->header_buffer.mc_addr_low);

	fw_to_load = UCODE_ID_RLC_G_MASK
			+ UCODE_ID_SDMA0_MASK
			+ UCODE_ID_SDMA1_MASK
			+ UCODE_ID_CP_CE_MASK
			+ UCODE_ID_CP_ME_MASK
			+ UCODE_ID_CP_PFP_MASK
			+ UCODE_ID_CP_MEC_MASK;

	PP_ASSERT_WITH_CODE(
		0 == tonga_send_msg_to_smc_with_parameter_without_waiting(
		smumgr, PPSMC_MSG_LoadUcodes, fw_to_load),
		"Fail to Request SMU Load uCode", return 0);

	return result;
}

static int tonga_request_smu_load_specific_fw(struct pp_smumgr *smumgr,
				uint32_t firmwareType)
{
	return 0;
}

/**
 * Upload the SMC firmware to the SMC microcontroller.
 *
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    pFirmware the data structure containing the various sections of the firmware.
 */
static int tonga_smu_upload_firmware_image(struct pp_smumgr *smumgr)
{
	const uint8_t *src;
	uint32_t byte_count;
	uint32_t *data;
	struct cgs_firmware_info info = {0};

	if (smumgr == NULL || smumgr->device == NULL)
		return -EINVAL;

	cgs_get_firmware_info(smumgr->device,
		tonga_convert_fw_type_to_cgs(UCODE_ID_SMU), &info);

	if (info.image_size & 3) {
		printk(KERN_ERR "[ powerplay ] SMC ucode is not 4 bytes aligned\n");
		return -EINVAL;
	}

	if (info.image_size > TONGA_SMC_SIZE) {
		printk(KERN_ERR "[ powerplay ] SMC address is beyond the SMC RAM area\n");
		return -EINVAL;
	}

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_0, 0x20000);
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 1);

	byte_count = info.image_size;
	src = (const uint8_t *)info.kptr;

	data = (uint32_t *)src;
	for (; byte_count >= 4; data++, byte_count -= 4)
		cgs_write_register(smumgr->device, mmSMC_IND_DATA_0, data[0]);

	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 0);

	return 0;
}

static int tonga_start_in_protection_mode(struct pp_smumgr *smumgr)
{
	int result;

	/* Assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = tonga_smu_upload_firmware_image(smumgr);
	if (result)
		return result;

	/* Clear status */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
		ixSMU_STATUS, 0);

	/* Enable clock */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Set SMU Auto Start */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMU_INPUT_DATA, AUTO_START, 1);

	/* Clear firmware interrupt enable flag */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
		ixFIRMWARE_FLAGS, 0);

	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
		RCU_UC_EVENTS, INTERRUPTS_ENABLED, 1);

	/**
	 * Call Test SMU message with 0x20000 offset to trigger SMU start
	 */
	tonga_send_msg_to_smc_offset(smumgr);

	/* Wait for done bit to be set */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
		SMU_STATUS, SMU_DONE, 0);

	/* Check pass/failed indicator */
	if (1 != SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device,
				CGS_IND_REG__SMC, SMU_STATUS, SMU_PASS)) {
		printk(KERN_ERR "[ powerplay ] SMU Firmware start failed\n");
		return -EINVAL;
	}

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
		FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return 0;
}


static int tonga_start_in_non_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* wait for smc boot up */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
		RCU_UC_EVENTS, boot_seq_done, 0);

	/*Clear firmware interrupt enable flag*/
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
		ixFIRMWARE_FLAGS, 0);


	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = tonga_smu_upload_firmware_image(smumgr);

	if (result != 0)
		return result;

	/* Set smc instruct start point at 0x0 */
	tonga_program_jump_on_start(smumgr);


	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/*De-assert reset*/
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
		SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
		FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int tonga_start_smu(struct pp_smumgr *smumgr)
{
	int result;

	/* Only start SMC if SMC RAM is not running */
	if (!tonga_is_smc_ram_running(smumgr)) {
		/*Check if SMU is running in protected mode*/
		if (0 == SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMU_FIRMWARE, SMU_MODE)) {
			result = tonga_start_in_non_protection_mode(smumgr);
			if (result)
				return result;
		} else {
			result = tonga_start_in_protection_mode(smumgr);
			if (result)
				return result;
		}
	}

	result = tonga_request_smu_reload_fw(smumgr);

	return result;
}

/**
 * Write a 32bit value to the SMC SRAM space.
 * ALL PARAMETERS ARE IN HOST BYTE ORDER.
 * @param    smumgr  the address of the powerplay hardware manager.
 * @param    smcAddress the address in the SMC RAM to access.
 * @param    value to write to the SMC SRAM.
 */
static int tonga_smu_init(struct pp_smumgr *smumgr)
{
	struct tonga_smumgr *tonga_smu;
	uint8_t *internal_buf;
	uint64_t mc_addr = 0;
	/* Allocate memory for backend private data */
	tonga_smu = (struct tonga_smumgr *)(smumgr->backend);
	tonga_smu->header_buffer.data_size =
		((sizeof(struct SMU_DRAMData_TOC) / 4096) + 1) * 4096;
	tonga_smu->smu_buffer.data_size = 200*4096;

	smu_allocate_memory(smumgr->device,
		tonga_smu->header_buffer.data_size,
		CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB,
		PAGE_SIZE,
		&mc_addr,
		&tonga_smu->header_buffer.kaddr,
		&tonga_smu->header_buffer.handle);

	tonga_smu->pHeader = tonga_smu->header_buffer.kaddr;
	tonga_smu->header_buffer.mc_addr_high = smu_upper_32_bits(mc_addr);
	tonga_smu->header_buffer.mc_addr_low = smu_lower_32_bits(mc_addr);

	PP_ASSERT_WITH_CODE((NULL != tonga_smu->pHeader),
		"Out of memory.",
		kfree(smumgr->backend);
		cgs_free_gpu_mem(smumgr->device,
		(cgs_handle_t)tonga_smu->header_buffer.handle);
		return -1);

	smu_allocate_memory(smumgr->device,
		tonga_smu->smu_buffer.data_size,
		CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB,
		PAGE_SIZE,
		&mc_addr,
		&tonga_smu->smu_buffer.kaddr,
		&tonga_smu->smu_buffer.handle);

	internal_buf = tonga_smu->smu_buffer.kaddr;
	tonga_smu->smu_buffer.mc_addr_high = smu_upper_32_bits(mc_addr);
	tonga_smu->smu_buffer.mc_addr_low = smu_lower_32_bits(mc_addr);

	PP_ASSERT_WITH_CODE((NULL != internal_buf),
		"Out of memory.",
		kfree(smumgr->backend);
		cgs_free_gpu_mem(smumgr->device,
		(cgs_handle_t)tonga_smu->smu_buffer.handle);
		return -1;);

	return 0;
}

static const struct pp_smumgr_func tonga_smu_funcs = {
	.smu_init = &tonga_smu_init,
	.smu_fini = &tonga_smu_fini,
	.start_smu = &tonga_start_smu,
	.check_fw_load_finish = &tonga_check_fw_load_finish,
	.request_smu_load_fw = &tonga_request_smu_reload_fw,
	.request_smu_load_specific_fw = &tonga_request_smu_load_specific_fw,
	.send_msg_to_smc = &tonga_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &tonga_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
};

int tonga_smum_init(struct pp_smumgr *smumgr)
{
	struct tonga_smumgr *tonga_smu = NULL;

	tonga_smu = kzalloc(sizeof(struct tonga_smumgr), GFP_KERNEL);

	if (tonga_smu == NULL)
		return -ENOMEM;

	smumgr->backend = tonga_smu;
	smumgr->smumgr_funcs = &tonga_smu_funcs;

	return 0;
}
