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

#include "smumgr.h"
#include "smu74.h"
#include "smu_ucode_xfer_vi.h"
#include "polaris10_smumgr.h"
#include "smu74_discrete.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gca/gfx_8_0_d.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "polaris10_pwrvirus.h"
#include "ppatomctrl.h"
#include "pp_debug.h"
#include "cgs_common.h"

#define POLARIS10_SMC_SIZE 0x20000
#define VOLTAGE_SCALE 4

/* Microcode file is stored in this buffer */
#define BUFFER_SIZE                 80000
#define MAX_STRING_SIZE             15
#define BUFFER_SIZETWO              131072  /* 128 *1024 */

#define SMC_RAM_END 0x40000

static const SMU74_Discrete_GraphicsLevel avfs_graphics_level_polaris10[8] = {
	/*  Min      pcie   DeepSleep Activity  CgSpll      CgSpll    CcPwr  CcPwr  Sclk         Enabled      Enabled                       Voltage    Power */
	/* Voltage, DpmLevel, DivId,  Level,  FuncCntl3,  FuncCntl4,  DynRm, DynRm1 Did, Padding,ForActivity, ForThrottle, UpHyst, DownHyst, DownHyst, Throttle */
	{ 0x3c0fd047, 0x00, 0x03, 0x1e00, 0x00200410, 0x87020000, 0, 0, 0x16, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0x30750000, 0, 0, 0, 0, 0, 0, 0 } },
	{ 0xa00fd047, 0x01, 0x04, 0x1e00, 0x00800510, 0x87020000, 0, 0, 0x16, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0x409c0000, 0, 0, 0, 0, 0, 0, 0 } },
	{ 0x0410d047, 0x01, 0x00, 0x1e00, 0x00600410, 0x87020000, 0, 0, 0x0e, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0x50c30000, 0, 0, 0, 0, 0, 0, 0 } },
	{ 0x6810d047, 0x01, 0x00, 0x1e00, 0x00800410, 0x87020000, 0, 0, 0x0c, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0x60ea0000, 0, 0, 0, 0, 0, 0, 0 } },
	{ 0xcc10d047, 0x01, 0x00, 0x1e00, 0x00e00410, 0x87020000, 0, 0, 0x0c, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0xe8fd0000, 0, 0, 0, 0, 0, 0, 0 } },
	{ 0x3011d047, 0x01, 0x00, 0x1e00, 0x00400510, 0x87020000, 0, 0, 0x0c, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0x70110100, 0, 0, 0, 0, 0, 0, 0 } },
	{ 0x9411d047, 0x01, 0x00, 0x1e00, 0x00a00510, 0x87020000, 0, 0, 0x0c, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0xf8240100, 0, 0, 0, 0, 0, 0, 0 } },
	{ 0xf811d047, 0x01, 0x00, 0x1e00, 0x00000610, 0x87020000, 0, 0, 0x0c, 0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, { 0x80380100, 0, 0, 0, 0, 0, 0, 0 } }
};

static const SMU74_Discrete_MemoryLevel avfs_memory_level_polaris10 =
	{0x50140000, 0x50140000, 0x00320000, 0x00, 0x00,
	 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x0000, 0x00, 0x00};

/**
* Set the address for reading/writing the SMC SRAM space.
* @param    smumgr  the address of the powerplay hardware manager.
* @param    smcAddress the address in the SMC RAM to access.
*/
static int polaris10_set_smc_sram_address(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t limit)
{
	PP_ASSERT_WITH_CODE((0 == (3 & smc_addr)), "SMC address must be 4 byte aligned.", return -EINVAL);
	PP_ASSERT_WITH_CODE((limit > (smc_addr + 3)), "SMC addr is beyond the SMC RAM area.", return -EINVAL);

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_11, smc_addr);
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 0);

	return 0;
}

/**
* Copy bytes from SMC RAM space into driver memory.
*
* @param    smumgr  the address of the powerplay SMU manager.
* @param    smc_start_address the start address in the SMC RAM to copy bytes from
* @param    src the byte array to copy the bytes to.
* @param    byte_count the number of bytes to copy.
*/
int polaris10_copy_bytes_from_smc(struct pp_smumgr *smumgr, uint32_t smc_start_address, uint32_t *dest, uint32_t byte_count, uint32_t limit)
{
	uint32_t data;
	uint32_t addr;
	uint8_t *dest_byte;
	uint8_t i, data_byte[4] = {0};
	uint32_t *pdata = (uint32_t *)&data_byte;

	PP_ASSERT_WITH_CODE((0 == (3 & smc_start_address)), "SMC address must be 4 byte aligned.", return -1;);
	PP_ASSERT_WITH_CODE((limit > (smc_start_address + byte_count)), "SMC address is beyond the SMC RAM area.", return -1);

	addr = smc_start_address;

	while (byte_count >= 4) {
		polaris10_read_smc_sram_dword(smumgr, addr, &data, limit);

		*dest = PP_SMC_TO_HOST_UL(data);

		dest += 1;
		byte_count -= 4;
		addr += 4;
	}

	if (byte_count) {
		polaris10_read_smc_sram_dword(smumgr, addr, &data, limit);
		*pdata = PP_SMC_TO_HOST_UL(data);
	/* Cast dest into byte type in dest_byte.  This way, we don't overflow if the allocated memory is not 4-byte aligned. */
		dest_byte = (uint8_t *)dest;
		for (i = 0; i < byte_count; i++)
			dest_byte[i] = data_byte[i];
	}

	return 0;
}

/**
* Copy bytes from an array into the SMC RAM space.
*
* @param    pSmuMgr  the address of the powerplay SMU manager.
* @param    smc_start_address the start address in the SMC RAM to copy bytes to.
* @param    src the byte array to copy the bytes from.
* @param    byte_count the number of bytes to copy.
*/
int polaris10_copy_bytes_to_smc(struct pp_smumgr *smumgr, uint32_t smc_start_address,
				const uint8_t *src, uint32_t byte_count, uint32_t limit)
{
	int result;
	uint32_t data = 0;
	uint32_t original_data;
	uint32_t addr = 0;
	uint32_t extra_shift;

	PP_ASSERT_WITH_CODE((0 == (3 & smc_start_address)), "SMC address must be 4 byte aligned.", return -1);
	PP_ASSERT_WITH_CODE((limit > (smc_start_address + byte_count)), "SMC address is beyond the SMC RAM area.", return -1);

	addr = smc_start_address;

	while (byte_count >= 4) {
	/* Bytes are written into the SMC addres space with the MSB first. */
		data = src[0] * 0x1000000 + src[1] * 0x10000 + src[2] * 0x100 + src[3];

		result = polaris10_set_smc_sram_address(smumgr, addr, limit);

		if (0 != result)
			return result;

		cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, data);

		src += 4;
		byte_count -= 4;
		addr += 4;
	}

	if (0 != byte_count) {

		data = 0;

		result = polaris10_set_smc_sram_address(smumgr, addr, limit);

		if (0 != result)
			return result;


		original_data = cgs_read_register(smumgr->device, mmSMC_IND_DATA_11);

		extra_shift = 8 * (4 - byte_count);

		while (byte_count > 0) {
			/* Bytes are written into the SMC addres space with the MSB first. */
			data = (0x100 * data) + *src++;
			byte_count--;
		}

		data <<= extra_shift;

		data |= (original_data & ~((~0UL) << extra_shift));

		result = polaris10_set_smc_sram_address(smumgr, addr, limit);

		if (0 != result)
			return result;

		cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, data);
	}

	return 0;
}


static int polaris10_program_jump_on_start(struct pp_smumgr *smumgr)
{
	static const unsigned char data[4] = { 0xE0, 0x00, 0x80, 0x40 };

	polaris10_copy_bytes_to_smc(smumgr, 0x0, data, 4, sizeof(data)+1);

	return 0;
}

/**
* Return if the SMC is currently running.
*
* @param    smumgr  the address of the powerplay hardware manager.
*/
bool polaris10_is_smc_ram_running(struct pp_smumgr *smumgr)
{
	return ((0 == SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC, SMC_SYSCON_CLOCK_CNTL_0, ck_disable))
	&& (0x20100 <= cgs_read_ind_register(smumgr->device, CGS_IND_REG__SMC, ixSMC_PC_C)));
}

/**
* Send a message to the SMC, and wait for its response.
*
* @param    smumgr  the address of the powerplay hardware manager.
* @param    msg the message to send.
* @return   The response that came from the SMC.
*/
int polaris10_send_msg_to_smc(struct pp_smumgr *smumgr, uint16_t msg)
{
	if (!polaris10_is_smc_ram_running(smumgr))
		return -1;

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	if (1 != SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP))
		printk("Failed to send Previous Message.\n");


	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, msg);

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	if (1 != SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP))
		printk("Failed to send Message.\n");

	return 0;
}


/**
* Send a message to the SMC, and do not wait for its response.
*
* @param    smumgr  the address of the powerplay hardware manager.
* @param    msg the message to send.
* @return   Always return 0.
*/
int polaris10_send_msg_to_smc_without_waiting(struct pp_smumgr *smumgr, uint16_t msg)
{
	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, msg);

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
int polaris10_send_msg_to_smc_with_parameter(struct pp_smumgr *smumgr, uint16_t msg, uint32_t parameter)
{
	if (!polaris10_is_smc_ram_running(smumgr)) {
		return -1;
	}

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, parameter);

	return polaris10_send_msg_to_smc(smumgr, msg);
}


/**
* Send a message to the SMC with parameter, do not wait for response
*
* @param    smumgr:  the address of the powerplay hardware manager.
* @param    msg: the message to send.
* @param    parameter: the parameter to send
* @return   The response that came from the SMC.
*/
int polaris10_send_msg_to_smc_with_parameter_without_waiting(struct pp_smumgr *smumgr, uint16_t msg, uint32_t parameter)
{
	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, parameter);

	return polaris10_send_msg_to_smc_without_waiting(smumgr, msg);
}

int polaris10_send_msg_to_smc_offset(struct pp_smumgr *smumgr)
{
	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, 0x20000);

	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, PPSMC_MSG_Test);

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	if (1 != SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP))
		printk("Failed to send Message.\n");

	return 0;
}

/**
* Wait until the SMC is doing nithing. Doing nothing means that the SMC is either turned off or it is sitting on the STOP instruction.
*
* @param    smumgr  the address of the powerplay hardware manager.
* @param    msg the message to send.
* @return   The response that came from the SMC.
*/
int polaris10_wait_for_smc_inactive(struct pp_smumgr *smumgr)
{
	/* If the SMC is not even on it qualifies as inactive. */
	if (!polaris10_is_smc_ram_running(smumgr))
		return -1;

	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND, SMC_SYSCON_CLOCK_CNTL_0, cken, 0);
	return 0;
}


/**
* Upload the SMC firmware to the SMC microcontroller.
*
* @param    smumgr  the address of the powerplay hardware manager.
* @param    pFirmware the data structure containing the various sections of the firmware.
*/
static int polaris10_upload_smc_firmware_data(struct pp_smumgr *smumgr, uint32_t length, uint32_t *src, uint32_t limit)
{
	uint32_t byte_count = length;

	PP_ASSERT_WITH_CODE((limit >= byte_count), "SMC address is beyond the SMC RAM area.", return -1);

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_11, 0x20000);
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 1);

	for (; byte_count >= 4; byte_count -= 4)
		cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, *src++);

	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 0);

	PP_ASSERT_WITH_CODE((0 == byte_count), "SMC size must be dividable by 4.", return -1);

	return 0;
}

static enum cgs_ucode_id polaris10_convert_fw_type_to_cgs(uint32_t fw_type)
{
	enum cgs_ucode_id result = CGS_UCODE_ID_MAXIMUM;

	switch (fw_type) {
	case UCODE_ID_SMU:
		result = CGS_UCODE_ID_SMU;
		break;
	case UCODE_ID_SMU_SK:
		result = CGS_UCODE_ID_SMU_SK;
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

static int polaris10_upload_smu_firmware_image(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);

	struct cgs_firmware_info info = {0};

	if (smu_data->security_hard_key == 1)
		cgs_get_firmware_info(smumgr->device,
			polaris10_convert_fw_type_to_cgs(UCODE_ID_SMU), &info);
	else
		cgs_get_firmware_info(smumgr->device,
			polaris10_convert_fw_type_to_cgs(UCODE_ID_SMU_SK), &info);

	/* TO DO cgs_init_samu_load_smu(smumgr->device, (uint32_t *)info.kptr, info.image_size, smu_data->post_initial_boot);*/
	result = polaris10_upload_smc_firmware_data(smumgr, info.image_size, (uint32_t *)info.kptr, POLARIS10_SMC_SIZE);

	return result;
}

/**
* Read a 32bit value from the SMC SRAM space.
* ALL PARAMETERS ARE IN HOST BYTE ORDER.
* @param    smumgr  the address of the powerplay hardware manager.
* @param    smcAddress the address in the SMC RAM to access.
* @param    value and output parameter for the data read from the SMC SRAM.
*/
int polaris10_read_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t *value, uint32_t limit)
{
	int result;

	result = polaris10_set_smc_sram_address(smumgr, smc_addr, limit);

	if (result)
		return result;

	*value = cgs_read_register(smumgr->device, mmSMC_IND_DATA_11);
	return 0;
}

/**
* Write a 32bit value to the SMC SRAM space.
* ALL PARAMETERS ARE IN HOST BYTE ORDER.
* @param    smumgr  the address of the powerplay hardware manager.
* @param    smc_addr the address in the SMC RAM to access.
* @param    value to write to the SMC SRAM.
*/
int polaris10_write_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t value, uint32_t limit)
{
	int result;

	result = polaris10_set_smc_sram_address(smumgr, smc_addr, limit);

	if (result)
		return result;

	cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, value);

	return 0;
}


int polaris10_smu_fini(struct pp_smumgr *smumgr)
{
	if (smumgr->backend) {
		kfree(smumgr->backend);
		smumgr->backend = NULL;
	}
	return 0;
}

/* Convert the firmware type to SMU type mask. For MEC, we need to check all MEC related type */
static uint32_t polaris10_get_mask_for_firmware_type(uint32_t fw_type)
{
	uint32_t result = 0;

	switch (fw_type) {
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
	case UCODE_ID_CP_MEC_JT1:
	case UCODE_ID_CP_MEC_JT2:
		result = UCODE_ID_CP_MEC_MASK;
		break;
	case UCODE_ID_RLC_G:
		result = UCODE_ID_RLC_G_MASK;
		break;
	default:
		printk("UCode type is out of range! \n");
		result = 0;
	}

	return result;
}

/* Populate one firmware image to the data structure */

static int polaris10_populate_single_firmware_entry(struct pp_smumgr *smumgr,
						uint32_t fw_type,
						struct SMU_Entry *entry)
{
	int result = 0;
	struct cgs_firmware_info info = {0};

	result = cgs_get_firmware_info(smumgr->device,
				polaris10_convert_fw_type_to_cgs(fw_type),
				&info);

	if (!result) {
		entry->version = info.version;
		entry->id = (uint16_t)fw_type;
		entry->image_addr_high = smu_upper_32_bits(info.mc_addr);
		entry->image_addr_low = smu_lower_32_bits(info.mc_addr);
		entry->meta_data_addr_high = 0;
		entry->meta_data_addr_low = 0;
		entry->data_size_byte = info.image_size;
		entry->num_register_entries = 0;
	}

	if (fw_type == UCODE_ID_RLC_G)
		entry->flags = 1;
	else
		entry->flags = 0;

	return 0;
}

static int polaris10_request_smu_load_fw(struct pp_smumgr *smumgr)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);
	uint32_t fw_to_load;

	int result = 0;
	struct SMU_DRAMData_TOC *toc;

	if (!smumgr->reload_fw) {
		printk(KERN_INFO "[ powerplay ] skip reloading...\n");
		return 0;
	}

	if (smu_data->soft_regs_start)
		cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
					smu_data->soft_regs_start + offsetof(SMU74_SoftRegisters, UcodeLoadStatus),
					0x0);

	polaris10_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_SMU_DRAM_ADDR_HI, smu_data->smu_buffer.mc_addr_high);
	polaris10_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_SMU_DRAM_ADDR_LO, smu_data->smu_buffer.mc_addr_low);

	toc = (struct SMU_DRAMData_TOC *)smu_data->header;
	toc->num_entries = 0;
	toc->structure_version = 1;

	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_RLC_G, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_CP_CE, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_CP_PFP, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_CP_ME, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_CP_MEC, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_CP_MEC_JT1, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_CP_MEC_JT2, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_SDMA0, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);
	PP_ASSERT_WITH_CODE(0 == polaris10_populate_single_firmware_entry(smumgr, UCODE_ID_SDMA1, &toc->entry[toc->num_entries++]), "Failed to Get Firmware Entry.", return -1);

	polaris10_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_DRV_DRAM_ADDR_HI, smu_data->header_buffer.mc_addr_high);
	polaris10_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_DRV_DRAM_ADDR_LO, smu_data->header_buffer.mc_addr_low);

	fw_to_load = UCODE_ID_RLC_G_MASK
		   + UCODE_ID_SDMA0_MASK
		   + UCODE_ID_SDMA1_MASK
		   + UCODE_ID_CP_CE_MASK
		   + UCODE_ID_CP_ME_MASK
		   + UCODE_ID_CP_PFP_MASK
		   + UCODE_ID_CP_MEC_MASK;

	if (polaris10_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_LoadUcodes, fw_to_load))
		printk(KERN_ERR "Fail to Request SMU Load uCode");

	return result;
}

/* Check if the FW has been loaded, SMU will not return if loading has not finished. */
static int polaris10_check_fw_load_finish(struct pp_smumgr *smumgr, uint32_t fw_type)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);
	uint32_t fw_mask = polaris10_get_mask_for_firmware_type(fw_type);
	uint32_t ret;
	/* Check SOFT_REGISTERS_TABLE_28.UcodeLoadStatus */
	ret = smum_wait_on_indirect_register(smumgr, mmSMC_IND_INDEX_11,
					smu_data->soft_regs_start + offsetof(SMU74_SoftRegisters, UcodeLoadStatus),
					fw_mask, fw_mask);

	return ret;
}

static int polaris10_reload_firmware(struct pp_smumgr *smumgr)
{
	return smumgr->smumgr_funcs->start_smu(smumgr);
}

static int polaris10_setup_pwr_virus(struct pp_smumgr *smumgr)
{
	int i;
	int result = -1;
	uint32_t reg, data;

	const PWR_Command_Table *pvirus = pwr_virus_table;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);


	for (i = 0; i < PWR_VIRUS_TABLE_SIZE; i++) {
		switch (pvirus->command) {
		case PwrCmdWrite:
			reg  = pvirus->reg;
			data = pvirus->data;
			cgs_write_register(smumgr->device, reg, data);
			break;

		case PwrCmdEnd:
			result = 0;
			break;

		default:
			printk("Table Exit with Invalid Command!");
			smu_data->avfs.avfs_btc_status = AVFS_BTC_VIRUS_FAIL;
			result = -1;
			break;
		}
		pvirus++;
	}

	return result;
}

static int polaris10_perform_btc(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);

	if (0 != smu_data->avfs.avfs_btc_param) {
		if (0 != polaris10_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_PerformBtc, smu_data->avfs.avfs_btc_param)) {
			printk("[AVFS][SmuPolaris10_PerformBtc] PerformBTC SMU msg failed");
			result = -1;
		}
	}
	if (smu_data->avfs.avfs_btc_param > 1) {
		/* Soft-Reset to reset the engine before loading uCode */
		/* halt */
		cgs_write_register(smumgr->device, mmCP_MEC_CNTL, 0x50000000);
		/* reset everything */
		cgs_write_register(smumgr->device, mmGRBM_SOFT_RESET, 0xffffffff);
		cgs_write_register(smumgr->device, mmGRBM_SOFT_RESET, 0);
	}
	return result;
}


int polaris10_setup_graphics_level_structure(struct pp_smumgr *smumgr)
{
	uint32_t vr_config;
	uint32_t dpm_table_start;

	uint16_t u16_boot_mvdd;
	uint32_t graphics_level_address, vr_config_address, graphics_level_size;

	graphics_level_size = sizeof(avfs_graphics_level_polaris10);
	u16_boot_mvdd = PP_HOST_TO_SMC_US(1300 * VOLTAGE_SCALE);

	PP_ASSERT_WITH_CODE(0 == polaris10_read_smc_sram_dword(smumgr,
				SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, DpmTable),
				&dpm_table_start, 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] SMU could not communicate starting address of DPM table",
			return -1);

	/*  Default value for VRConfig = VR_MERGED_WITH_VDDC + VR_STATIC_VOLTAGE(VDDCI) */
	vr_config = 0x01000500; /* Real value:0x50001 */

	vr_config_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, VRConfig);

	PP_ASSERT_WITH_CODE(0 == polaris10_copy_bytes_to_smc(smumgr, vr_config_address,
				(uint8_t *)&vr_config, sizeof(uint32_t), 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Problems copying VRConfig value over to SMC",
			return -1);

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, GraphicsLevel);

	PP_ASSERT_WITH_CODE(0 == polaris10_copy_bytes_to_smc(smumgr, graphics_level_address,
				(uint8_t *)(&avfs_graphics_level_polaris10),
				graphics_level_size, 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of SCLK DPM table failed!",
			return -1);

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, MemoryLevel);

	PP_ASSERT_WITH_CODE(0 == polaris10_copy_bytes_to_smc(smumgr, graphics_level_address,
				(uint8_t *)(&avfs_memory_level_polaris10), sizeof(avfs_memory_level_polaris10), 0x40000),
				"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of MCLK DPM table failed!",
			return -1);

	/* MVDD Boot value - neccessary for getting rid of the hang that occurs during Mclk DPM enablement */

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, BootMVdd);

	PP_ASSERT_WITH_CODE(0 == polaris10_copy_bytes_to_smc(smumgr, graphics_level_address,
			(uint8_t *)(&u16_boot_mvdd), sizeof(u16_boot_mvdd), 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of DPM table failed!",
			return -1);

	return 0;
}

int polaris10_avfs_event_mgr(struct pp_smumgr *smumgr, bool SMU_VFT_INTACT)
{
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);

	switch (smu_data->avfs.avfs_btc_status) {
	case AVFS_BTC_COMPLETED_PREVIOUSLY:
		break;

	case AVFS_BTC_BOOT: /* Cold Boot State - Post SMU Start */

		smu_data->avfs.avfs_btc_status = AVFS_BTC_DPMTABLESETUP_FAILED;
		PP_ASSERT_WITH_CODE(0 == polaris10_setup_graphics_level_structure(smumgr),
		"[AVFS][Polaris10_AVFSEventMgr] Could not Copy Graphics Level table over to SMU",
		return -1);

		if (smu_data->avfs.avfs_btc_param > 1) {
			printk("[AVFS][Polaris10_AVFSEventMgr] AC BTC has not been successfully verified on Fiji. There may be in this setting.");
			smu_data->avfs.avfs_btc_status = AVFS_BTC_VIRUS_FAIL;
			PP_ASSERT_WITH_CODE(-1 == polaris10_setup_pwr_virus(smumgr),
			"[AVFS][Polaris10_AVFSEventMgr] Could not setup Pwr Virus for AVFS ",
			return -1);
		}

		smu_data->avfs.avfs_btc_status = AVFS_BTC_FAILED;
		PP_ASSERT_WITH_CODE(0 == polaris10_perform_btc(smumgr),
					"[AVFS][Polaris10_AVFSEventMgr] Failure at SmuPolaris10_PerformBTC. AVFS Disabled",
				 return -1);

		break;

	case AVFS_BTC_DISABLED:
	case AVFS_BTC_NOTSUPPORTED:
		break;

	default:
		printk("[AVFS] Something is broken. See log!");
		break;
	}

	return 0;
}

static int polaris10_start_smu_in_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* Wait for smc boot up */
	/* SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND, RCU_UC_EVENTS, boot_seq_done, 0) */

	/* Assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = polaris10_upload_smu_firmware_image(smumgr);
	if (result != 0)
		return result;

	/* Clear status */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC, ixSMU_STATUS, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);


	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND, RCU_UC_EVENTS, INTERRUPTS_ENABLED, 1);


	/* Call Test SMU message with 0x20000 offset to trigger SMU start */
	polaris10_send_msg_to_smc_offset(smumgr);

	/* Wait done bit to be set */
	/* Check pass/failed indicator */

	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND, SMU_STATUS, SMU_DONE, 0);

	if (1 != SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
						SMU_STATUS, SMU_PASS))
		PP_ASSERT_WITH_CODE(false, "SMU Firmware start failed!", return -1);

	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC, ixFIRMWARE_FLAGS, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND, FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int polaris10_start_smu_in_non_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* wait for smc boot up */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND, RCU_UC_EVENTS, boot_seq_done, 0);

	/* Clear firmware interrupt enable flag */
	/* SMUM_WRITE_VFPF_INDIRECT_FIELD(pSmuMgr, SMC_IND, SMC_SYSCON_MISC_CNTL, pre_fetcher_en, 1); */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
				ixFIRMWARE_FLAGS, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL,
					rst_reg, 1);

	result = polaris10_upload_smu_firmware_image(smumgr);
	if (result != 0)
		return result;

	/* Set smc instruct start point at 0x0 */
	polaris10_program_jump_on_start(smumgr);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */

	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
					FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int polaris10_start_smu(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);
	bool SMU_VFT_INTACT;

	/* Only start SMC if SMC RAM is not running */
	if (!polaris10_is_smc_ram_running(smumgr)) {
		SMU_VFT_INTACT = false;
		smu_data->protected_mode = (uint8_t) (SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC, SMU_FIRMWARE, SMU_MODE));
		smu_data->security_hard_key = (uint8_t) (SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC, SMU_FIRMWARE, SMU_SEL));

		/* Check if SMU is running in protected mode */
		if (smu_data->protected_mode == 0) {
			result = polaris10_start_smu_in_non_protection_mode(smumgr);
		} else {
			result = polaris10_start_smu_in_protection_mode(smumgr);

			/* If failed, try with different security Key. */
			if (result != 0) {
				smu_data->security_hard_key ^= 1;
				result = polaris10_start_smu_in_protection_mode(smumgr);
			}
		}

		if (result != 0)
			PP_ASSERT_WITH_CODE(0, "Failed to load SMU ucode.", return result);

		polaris10_avfs_event_mgr(smumgr, true);
	} else
		SMU_VFT_INTACT = true; /*Driver went offline but SMU was still alive and contains the VFT table */

	smu_data->post_initial_boot = true;
	polaris10_avfs_event_mgr(smumgr, SMU_VFT_INTACT);
	/* Setup SoftRegsStart here for register lookup in case DummyBackEnd is used and ProcessFirmwareHeader is not executed */
	polaris10_read_smc_sram_dword(smumgr, SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, SoftRegisters),
					&(smu_data->soft_regs_start), 0x40000);

	result = polaris10_request_smu_load_fw(smumgr);

	return result;
}

static int polaris10_smu_init(struct pp_smumgr *smumgr)
{
	struct polaris10_smumgr *smu_data;
	uint8_t *internal_buf;
	uint64_t mc_addr = 0;
	/* Allocate memory for backend private data */
	smu_data = (struct polaris10_smumgr *)(smumgr->backend);
	smu_data->header_buffer.data_size =
		((sizeof(struct SMU_DRAMData_TOC) / 4096) + 1) * 4096;
	smu_data->smu_buffer.data_size = 200*4096;
	smu_data->avfs.avfs_btc_status = AVFS_BTC_NOTSUPPORTED;
/* Allocate FW image data structure and header buffer and
 * send the header buffer address to SMU */
	smu_allocate_memory(smumgr->device,
		smu_data->header_buffer.data_size,
		CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB,
		PAGE_SIZE,
		&mc_addr,
		&smu_data->header_buffer.kaddr,
		&smu_data->header_buffer.handle);

	smu_data->header = smu_data->header_buffer.kaddr;
	smu_data->header_buffer.mc_addr_high = smu_upper_32_bits(mc_addr);
	smu_data->header_buffer.mc_addr_low = smu_lower_32_bits(mc_addr);

	PP_ASSERT_WITH_CODE((NULL != smu_data->header),
		"Out of memory.",
		kfree(smumgr->backend);
		cgs_free_gpu_mem(smumgr->device,
		(cgs_handle_t)smu_data->header_buffer.handle);
		return -1);

/* Allocate buffer for SMU internal buffer and send the address to SMU.
 * Iceland SMU does not need internal buffer.*/
	smu_allocate_memory(smumgr->device,
		smu_data->smu_buffer.data_size,
		CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB,
		PAGE_SIZE,
		&mc_addr,
		&smu_data->smu_buffer.kaddr,
		&smu_data->smu_buffer.handle);

	internal_buf = smu_data->smu_buffer.kaddr;
	smu_data->smu_buffer.mc_addr_high = smu_upper_32_bits(mc_addr);
	smu_data->smu_buffer.mc_addr_low = smu_lower_32_bits(mc_addr);

	PP_ASSERT_WITH_CODE((NULL != internal_buf),
		"Out of memory.",
		kfree(smumgr->backend);
		cgs_free_gpu_mem(smumgr->device,
		(cgs_handle_t)smu_data->smu_buffer.handle);
		return -1;);

	return 0;
}

static const struct pp_smumgr_func ellsemere_smu_funcs = {
	.smu_init = polaris10_smu_init,
	.smu_fini = polaris10_smu_fini,
	.start_smu = polaris10_start_smu,
	.check_fw_load_finish = polaris10_check_fw_load_finish,
	.request_smu_load_fw = polaris10_reload_firmware,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = polaris10_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = polaris10_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
};

int polaris10_smum_init(struct pp_smumgr *smumgr)
{
	struct polaris10_smumgr *polaris10_smu = NULL;

	polaris10_smu = kzalloc(sizeof(struct polaris10_smumgr), GFP_KERNEL);

	if (polaris10_smu == NULL)
		return -1;

	smumgr->backend = polaris10_smu;
	smumgr->smumgr_funcs = &ellsemere_smu_funcs;

	return 0;
}
