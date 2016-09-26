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
#include "smu_ucode_xfer_vi.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "ppatomctrl.h"
#include "pp_debug.h"
#include "cgs_common.h"
#include "smu7_ppsmc.h"
#include "smu7_smumgr.h"

#define SMU7_SMC_SIZE 0x20000

static int smu7_set_smc_sram_address(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t limit)
{
	PP_ASSERT_WITH_CODE((0 == (3 & smc_addr)), "SMC address must be 4 byte aligned.", return -EINVAL);
	PP_ASSERT_WITH_CODE((limit > (smc_addr + 3)), "SMC addr is beyond the SMC RAM area.", return -EINVAL);

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_11, smc_addr);
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 0); /* on ci, SMC_IND_ACCESS_CNTL is different */
	return 0;
}


int smu7_copy_bytes_from_smc(struct pp_smumgr *smumgr, uint32_t smc_start_address, uint32_t *dest, uint32_t byte_count, uint32_t limit)
{
	uint32_t data;
	uint32_t addr;
	uint8_t *dest_byte;
	uint8_t i, data_byte[4] = {0};
	uint32_t *pdata = (uint32_t *)&data_byte;

	PP_ASSERT_WITH_CODE((0 == (3 & smc_start_address)), "SMC address must be 4 byte aligned.", return -EINVAL);
	PP_ASSERT_WITH_CODE((limit > (smc_start_address + byte_count)), "SMC address is beyond the SMC RAM area.", return -EINVAL);

	addr = smc_start_address;

	while (byte_count >= 4) {
		smu7_read_smc_sram_dword(smumgr, addr, &data, limit);

		*dest = PP_SMC_TO_HOST_UL(data);

		dest += 1;
		byte_count -= 4;
		addr += 4;
	}

	if (byte_count) {
		smu7_read_smc_sram_dword(smumgr, addr, &data, limit);
		*pdata = PP_SMC_TO_HOST_UL(data);
	/* Cast dest into byte type in dest_byte.  This way, we don't overflow if the allocated memory is not 4-byte aligned. */
		dest_byte = (uint8_t *)dest;
		for (i = 0; i < byte_count; i++)
			dest_byte[i] = data_byte[i];
	}

	return 0;
}


int smu7_copy_bytes_to_smc(struct pp_smumgr *smumgr, uint32_t smc_start_address,
				const uint8_t *src, uint32_t byte_count, uint32_t limit)
{
	int result;
	uint32_t data = 0;
	uint32_t original_data;
	uint32_t addr = 0;
	uint32_t extra_shift;

	PP_ASSERT_WITH_CODE((0 == (3 & smc_start_address)), "SMC address must be 4 byte aligned.", return -EINVAL);
	PP_ASSERT_WITH_CODE((limit > (smc_start_address + byte_count)), "SMC address is beyond the SMC RAM area.", return -EINVAL);

	addr = smc_start_address;

	while (byte_count >= 4) {
	/* Bytes are written into the SMC addres space with the MSB first. */
		data = src[0] * 0x1000000 + src[1] * 0x10000 + src[2] * 0x100 + src[3];

		result = smu7_set_smc_sram_address(smumgr, addr, limit);

		if (0 != result)
			return result;

		cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, data);

		src += 4;
		byte_count -= 4;
		addr += 4;
	}

	if (0 != byte_count) {

		data = 0;

		result = smu7_set_smc_sram_address(smumgr, addr, limit);

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

		result = smu7_set_smc_sram_address(smumgr, addr, limit);

		if (0 != result)
			return result;

		cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, data);
	}

	return 0;
}


int smu7_program_jump_on_start(struct pp_smumgr *smumgr)
{
	static const unsigned char data[4] = { 0xE0, 0x00, 0x80, 0x40 };

	smu7_copy_bytes_to_smc(smumgr, 0x0, data, 4, sizeof(data)+1);

	return 0;
}

bool smu7_is_smc_ram_running(struct pp_smumgr *smumgr)
{
	return ((0 == SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC, SMC_SYSCON_CLOCK_CNTL_0, ck_disable))
	&& (0x20100 <= cgs_read_ind_register(smumgr->device, CGS_IND_REG__SMC, ixSMC_PC_C)));
}

int smu7_send_msg_to_smc(struct pp_smumgr *smumgr, uint16_t msg)
{
	int ret;

	if (!smu7_is_smc_ram_running(smumgr))
		return -EINVAL;


	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	ret = SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP);

	if (ret != 1)
		printk("\n failed to send pre message %x ret is %d \n",  msg, ret);

	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, msg);

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	ret = SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP);

	if (ret != 1)
		printk("\n failed to send message %x ret is %d \n",  msg, ret);

	return 0;
}

int smu7_send_msg_to_smc_without_waiting(struct pp_smumgr *smumgr, uint16_t msg)
{
	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, msg);

	return 0;
}

int smu7_send_msg_to_smc_with_parameter(struct pp_smumgr *smumgr, uint16_t msg, uint32_t parameter)
{
	if (!smu7_is_smc_ram_running(smumgr)) {
		return -EINVAL;
	}

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, parameter);

	return smu7_send_msg_to_smc(smumgr, msg);
}

int smu7_send_msg_to_smc_with_parameter_without_waiting(struct pp_smumgr *smumgr, uint16_t msg, uint32_t parameter)
{
	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, parameter);

	return smu7_send_msg_to_smc_without_waiting(smumgr, msg);
}

int smu7_send_msg_to_smc_offset(struct pp_smumgr *smumgr)
{
	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, 0x20000);

	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, PPSMC_MSG_Test);

	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	if (1 != SMUM_READ_FIELD(smumgr->device, SMC_RESP_0, SMC_RESP))
		printk("Failed to send Message.\n");

	return 0;
}

int smu7_wait_for_smc_inactive(struct pp_smumgr *smumgr)
{
	if (!smu7_is_smc_ram_running(smumgr))
		return -EINVAL;

	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND, SMC_SYSCON_CLOCK_CNTL_0, cken, 0);
	return 0;
}


enum cgs_ucode_id smu7_convert_fw_type_to_cgs(uint32_t fw_type)
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
	case UCODE_ID_MEC_STORAGE:
		result = CGS_UCODE_ID_STORAGE;
		break;
	default:
		break;
	}

	return result;
}


int smu7_read_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t *value, uint32_t limit)
{
	int result;

	result = smu7_set_smc_sram_address(smumgr, smc_addr, limit);

	if (result)
		return result;

	*value = cgs_read_register(smumgr->device, mmSMC_IND_DATA_11);
	return 0;
}

int smu7_write_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t value, uint32_t limit)
{
	int result;

	result = smu7_set_smc_sram_address(smumgr, smc_addr, limit);

	if (result)
		return result;

	cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, value);

	return 0;
}

/* Convert the firmware type to SMU type mask. For MEC, we need to check all MEC related type */

static uint32_t smu7_get_mask_for_firmware_type(uint32_t fw_type)
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
	case UCODE_ID_CP_MEC:
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

static int smu7_populate_single_firmware_entry(struct pp_smumgr *smumgr,
						uint32_t fw_type,
						struct SMU_Entry *entry)
{
	int result = 0;
	struct cgs_firmware_info info = {0};

	result = cgs_get_firmware_info(smumgr->device,
				smu7_convert_fw_type_to_cgs(fw_type),
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

int smu7_request_smu_load_fw(struct pp_smumgr *smumgr)
{
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(smumgr->backend);
	uint32_t fw_to_load;
	int result = 0;
	struct SMU_DRAMData_TOC *toc;

	if (!smumgr->reload_fw) {
		printk(KERN_INFO "[ powerplay ] skip reloading...\n");
		return 0;
	}

	if (smu_data->soft_regs_start)
		cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
					smu_data->soft_regs_start + smum_get_offsetof(smumgr,
					SMU_SoftRegisters, UcodeLoadStatus),
					0x0);

	if (smumgr->chip_id > CHIP_TOPAZ) { /* add support for Topaz */
		smu7_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_SMU_DRAM_ADDR_HI, smu_data->smu_buffer.mc_addr_high);
		smu7_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_SMU_DRAM_ADDR_LO, smu_data->smu_buffer.mc_addr_low);
		fw_to_load = UCODE_ID_RLC_G_MASK
			   + UCODE_ID_SDMA0_MASK
			   + UCODE_ID_SDMA1_MASK
			   + UCODE_ID_CP_CE_MASK
			   + UCODE_ID_CP_ME_MASK
			   + UCODE_ID_CP_PFP_MASK
			   + UCODE_ID_CP_MEC_MASK;
	} else {
		fw_to_load = UCODE_ID_RLC_G_MASK
			   + UCODE_ID_SDMA0_MASK
			   + UCODE_ID_SDMA1_MASK
			   + UCODE_ID_CP_CE_MASK
			   + UCODE_ID_CP_ME_MASK
			   + UCODE_ID_CP_PFP_MASK
			   + UCODE_ID_CP_MEC_MASK
			   + UCODE_ID_CP_MEC_JT1_MASK
			   + UCODE_ID_CP_MEC_JT2_MASK;
	}

	toc = (struct SMU_DRAMData_TOC *)smu_data->header;
	toc->num_entries = 0;
	toc->structure_version = 1;

	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_RLC_G, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_CP_CE, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_CP_PFP, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_CP_ME, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_CP_MEC, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_CP_MEC_JT1, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_CP_MEC_JT2, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_SDMA0, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_SDMA1, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);
	if (cgs_is_virtualization_enabled(smumgr->device))
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(smumgr,
				UCODE_ID_MEC_STORAGE, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", return -EINVAL);

	smu7_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_DRV_DRAM_ADDR_HI, smu_data->header_buffer.mc_addr_high);
	smu7_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_DRV_DRAM_ADDR_LO, smu_data->header_buffer.mc_addr_low);

	if (smu7_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_LoadUcodes, fw_to_load))
		printk(KERN_ERR "Fail to Request SMU Load uCode");

	return result;
}

/* Check if the FW has been loaded, SMU will not return if loading has not finished. */
int smu7_check_fw_load_finish(struct pp_smumgr *smumgr, uint32_t fw_type)
{
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(smumgr->backend);
	uint32_t fw_mask = smu7_get_mask_for_firmware_type(fw_type);
	uint32_t ret;

	ret = smum_wait_on_indirect_register(smumgr, mmSMC_IND_INDEX_11,
					smu_data->soft_regs_start + smum_get_offsetof(smumgr,
					SMU_SoftRegisters, UcodeLoadStatus),
					fw_mask, fw_mask);

	return ret;
}

int smu7_reload_firmware(struct pp_smumgr *smumgr)
{
	return smumgr->smumgr_funcs->start_smu(smumgr);
}

static int smu7_upload_smc_firmware_data(struct pp_smumgr *smumgr, uint32_t length, uint32_t *src, uint32_t limit)
{
	uint32_t byte_count = length;

	PP_ASSERT_WITH_CODE((limit >= byte_count), "SMC address is beyond the SMC RAM area.", return -EINVAL);

	cgs_write_register(smumgr->device, mmSMC_IND_INDEX_11, 0x20000);
	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 1);

	for (; byte_count >= 4; byte_count -= 4)
		cgs_write_register(smumgr->device, mmSMC_IND_DATA_11, *src++);

	SMUM_WRITE_FIELD(smumgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 0);

	PP_ASSERT_WITH_CODE((0 == byte_count), "SMC size must be dividable by 4.", return -EINVAL);

	return 0;
}


int smu7_upload_smu_firmware_image(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(smumgr->backend);

	struct cgs_firmware_info info = {0};

	if (smu_data->security_hard_key == 1)
		cgs_get_firmware_info(smumgr->device,
			smu7_convert_fw_type_to_cgs(UCODE_ID_SMU), &info);
	else
		cgs_get_firmware_info(smumgr->device,
			smu7_convert_fw_type_to_cgs(UCODE_ID_SMU_SK), &info);

	result = smu7_upload_smc_firmware_data(smumgr, info.image_size, (uint32_t *)info.kptr, SMU7_SMC_SIZE);

	return result;
}


int smu7_init(struct pp_smumgr *smumgr)
{
	struct smu7_smumgr *smu_data;
	uint8_t *internal_buf;
	uint64_t mc_addr = 0;

	/* Allocate memory for backend private data */
	smu_data = (struct smu7_smumgr *)(smumgr->backend);
	smu_data->header_buffer.data_size =
			((sizeof(struct SMU_DRAMData_TOC) / 4096) + 1) * 4096;
	smu_data->smu_buffer.data_size = 200*4096;

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
		return -EINVAL);

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
		return -EINVAL);

	return 0;
}


int smu7_smu_fini(struct pp_smumgr *smumgr)
{
	if (smumgr->backend) {
		kfree(smumgr->backend);
		smumgr->backend = NULL;
	}
	cgs_rel_firmware(smumgr->device, CGS_UCODE_ID_SMU);
	return 0;
}
