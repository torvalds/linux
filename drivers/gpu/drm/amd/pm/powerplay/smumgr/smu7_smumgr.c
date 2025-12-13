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
#include "smumgr.h"
#include "smu_ucode_xfer_vi.h"
#include "ppatomctrl.h"
#include "cgs_common.h"
#include "smu7_ppsmc.h"
#include "smu7_smumgr.h"
#include "smu7_common.h"

#include "polaris10_pwrvirus.h"

#define SMU7_SMC_SIZE 0x20000

static int smu7_set_smc_sram_address(struct pp_hwmgr *hwmgr, uint32_t smc_addr, uint32_t limit)
{
	PP_ASSERT_WITH_CODE((0 == (3 & smc_addr)), "SMC address must be 4 byte aligned.", return -EINVAL);
	PP_ASSERT_WITH_CODE((limit > (smc_addr + 3)), "SMC addr is beyond the SMC RAM area.", return -EINVAL);

	cgs_write_register(hwmgr->device, mmSMC_IND_INDEX_11, smc_addr);
	PHM_WRITE_FIELD(hwmgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 0); /* on ci, SMC_IND_ACCESS_CNTL is different */
	return 0;
}


int smu7_copy_bytes_to_smc(struct pp_hwmgr *hwmgr, uint32_t smc_start_address,
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

		result = smu7_set_smc_sram_address(hwmgr, addr, limit);

		if (0 != result)
			return result;

		cgs_write_register(hwmgr->device, mmSMC_IND_DATA_11, data);

		src += 4;
		byte_count -= 4;
		addr += 4;
	}

	if (0 != byte_count) {

		data = 0;

		result = smu7_set_smc_sram_address(hwmgr, addr, limit);

		if (0 != result)
			return result;


		original_data = cgs_read_register(hwmgr->device, mmSMC_IND_DATA_11);

		extra_shift = 8 * (4 - byte_count);

		while (byte_count > 0) {
			/* Bytes are written into the SMC addres space with the MSB first. */
			data = (0x100 * data) + *src++;
			byte_count--;
		}

		data <<= extra_shift;

		data |= (original_data & ~((~0UL) << extra_shift));

		result = smu7_set_smc_sram_address(hwmgr, addr, limit);

		if (0 != result)
			return result;

		cgs_write_register(hwmgr->device, mmSMC_IND_DATA_11, data);
	}

	return 0;
}


int smu7_program_jump_on_start(struct pp_hwmgr *hwmgr)
{
	static const unsigned char data[4] = { 0xE0, 0x00, 0x80, 0x40 };

	smu7_copy_bytes_to_smc(hwmgr, 0x0, data, 4, sizeof(data)+1);

	return 0;
}

bool smu7_is_smc_ram_running(struct pp_hwmgr *hwmgr)
{
	return ((0 == PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, SMC_SYSCON_CLOCK_CNTL_0, ck_disable))
	&& (0x20100 <= cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC, ixSMC_PC_C)));
}

int smu7_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int ret;

	PHM_WAIT_FIELD_UNEQUAL(hwmgr, SMC_RESP_0, SMC_RESP, 0);

	ret = PHM_READ_FIELD(hwmgr->device, SMC_RESP_0, SMC_RESP);

	if (ret == 0xFE)
		dev_dbg(adev->dev, "last message was not supported\n");
	else if (ret != 1)
		dev_info(adev->dev,
			"\nlast message was failed ret is %d\n", ret);

	cgs_write_register(hwmgr->device, mmSMC_RESP_0, 0);
	cgs_write_register(hwmgr->device, mmSMC_MESSAGE_0, msg);

	PHM_WAIT_FIELD_UNEQUAL(hwmgr, SMC_RESP_0, SMC_RESP, 0);

	ret = PHM_READ_FIELD(hwmgr->device, SMC_RESP_0, SMC_RESP);

	if (ret == 0xFE)
		dev_dbg(adev->dev, "message %x was not supported\n", msg);
	else if (ret != 1)
		dev_dbg(adev->dev,
			"failed to send message %x ret is %d \n",  msg, ret);

	return 0;
}

int smu7_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr, uint16_t msg, uint32_t parameter)
{
	PHM_WAIT_FIELD_UNEQUAL(hwmgr, SMC_RESP_0, SMC_RESP, 0);

	cgs_write_register(hwmgr->device, mmSMC_MSG_ARG_0, parameter);

	return smu7_send_msg_to_smc(hwmgr, msg);
}

uint32_t smu7_get_argument(struct pp_hwmgr *hwmgr)
{
	return cgs_read_register(hwmgr->device, mmSMC_MSG_ARG_0);
}

int smu7_send_msg_to_smc_offset(struct pp_hwmgr *hwmgr)
{
	return smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_Test, 0x20000, NULL);
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


int smu7_read_smc_sram_dword(struct pp_hwmgr *hwmgr, uint32_t smc_addr, uint32_t *value, uint32_t limit)
{
	int result;

	result = smu7_set_smc_sram_address(hwmgr, smc_addr, limit);

	*value = result ? 0 : cgs_read_register(hwmgr->device, mmSMC_IND_DATA_11);

	return result;
}

int smu7_write_smc_sram_dword(struct pp_hwmgr *hwmgr, uint32_t smc_addr, uint32_t value, uint32_t limit)
{
	int result;

	result = smu7_set_smc_sram_address(hwmgr, smc_addr, limit);

	if (result)
		return result;

	cgs_write_register(hwmgr->device, mmSMC_IND_DATA_11, value);

	return 0;
}

static int smu7_populate_single_firmware_entry(struct pp_hwmgr *hwmgr,
						uint32_t fw_type,
						struct SMU_Entry *entry)
{
	int result = 0;
	struct cgs_firmware_info info = {0};

	result = cgs_get_firmware_info(hwmgr->device,
				smu7_convert_fw_type_to_cgs(fw_type),
				&info);

	if (!result) {
		entry->version = info.fw_version;
		entry->id = (uint16_t)fw_type;
		entry->image_addr_high = upper_32_bits(info.mc_addr);
		entry->image_addr_low = lower_32_bits(info.mc_addr);
		entry->meta_data_addr_high = 0;
		entry->meta_data_addr_low = 0;

		/* digest need be excluded out */
		if (!hwmgr->not_vf)
			info.image_size -= 20;
		entry->data_size_byte = info.image_size;
		entry->num_register_entries = 0;
	}

	if ((fw_type == UCODE_ID_RLC_G)
		|| (fw_type == UCODE_ID_CP_MEC))
		entry->flags = 1;
	else
		entry->flags = 0;

	return 0;
}

int smu7_request_smu_load_fw(struct pp_hwmgr *hwmgr)
{
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(hwmgr->smu_backend);
	uint32_t fw_to_load;
	int r = 0;

	amdgpu_ucode_init_bo(hwmgr->adev);

	if (smu_data->soft_regs_start)
		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
					smu_data->soft_regs_start + smum_get_offsetof(hwmgr,
					SMU_SoftRegisters, UcodeLoadStatus),
					0x0);

	if (hwmgr->chip_id > CHIP_TOPAZ) { /* add support for Topaz */
		if (hwmgr->not_vf) {
			smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SMU_DRAM_ADDR_HI,
						upper_32_bits(smu_data->smu_buffer.mc_addr),
						NULL);
			smum_send_msg_to_smc_with_parameter(hwmgr,
						PPSMC_MSG_SMU_DRAM_ADDR_LO,
						lower_32_bits(smu_data->smu_buffer.mc_addr),
						NULL);
		}
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

	if (!smu_data->toc) {
		struct SMU_DRAMData_TOC *toc;

		smu_data->toc = kzalloc(sizeof(struct SMU_DRAMData_TOC), GFP_KERNEL);
		if (!smu_data->toc)
			return -ENOMEM;
		toc = smu_data->toc;
		toc->num_entries = 0;
		toc->structure_version = 1;

		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_RLC_G, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_CP_CE, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_CP_PFP, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_CP_ME, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_CP_MEC, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_CP_MEC_JT1, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_CP_MEC_JT2, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_SDMA0, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_SDMA1, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
		if (!hwmgr->not_vf)
			PP_ASSERT_WITH_CODE(0 == smu7_populate_single_firmware_entry(hwmgr,
				UCODE_ID_MEC_STORAGE, &toc->entry[toc->num_entries++]),
				"Failed to Get Firmware Entry.", r = -EINVAL; goto failed);
	}
	memcpy_toio(smu_data->header_buffer.kaddr, smu_data->toc,
		    sizeof(struct SMU_DRAMData_TOC));
	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_DRV_DRAM_ADDR_HI,
			upper_32_bits(smu_data->header_buffer.mc_addr),
			NULL);
	smum_send_msg_to_smc_with_parameter(hwmgr,
			PPSMC_MSG_DRV_DRAM_ADDR_LO,
			lower_32_bits(smu_data->header_buffer.mc_addr),
			NULL);

	smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_MSG_LoadUcodes, fw_to_load, NULL);

	r = smu7_check_fw_load_finish(hwmgr, fw_to_load);
	if (!r)
		return 0;

	pr_err("SMU load firmware failed\n");

failed:
	kfree(smu_data->toc);
	smu_data->toc = NULL;
	return r;
}

/* Check if the FW has been loaded, SMU will not return if loading has not finished. */
int smu7_check_fw_load_finish(struct pp_hwmgr *hwmgr, uint32_t fw_type)
{
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(hwmgr->smu_backend);
	int ret;

	ret = phm_wait_on_indirect_register(hwmgr, mmSMC_IND_INDEX_11,
					smu_data->soft_regs_start + smum_get_offsetof(hwmgr,
					SMU_SoftRegisters, UcodeLoadStatus),
					fw_type, fw_type);
	return ret;
}

int smu7_reload_firmware(struct pp_hwmgr *hwmgr)
{
	return hwmgr->smumgr_funcs->start_smu(hwmgr);
}

static int smu7_upload_smc_firmware_data(struct pp_hwmgr *hwmgr, uint32_t length, uint32_t *src, uint32_t limit)
{
	uint32_t byte_count = length;

	PP_ASSERT_WITH_CODE((limit >= byte_count), "SMC address is beyond the SMC RAM area.", return -EINVAL);

	cgs_write_register(hwmgr->device, mmSMC_IND_INDEX_11, 0x20000);
	PHM_WRITE_FIELD(hwmgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 1);

	for (; byte_count >= 4; byte_count -= 4)
		cgs_write_register(hwmgr->device, mmSMC_IND_DATA_11, *src++);

	PHM_WRITE_FIELD(hwmgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_11, 0);

	PP_ASSERT_WITH_CODE((0 == byte_count), "SMC size must be divisible by 4.", return -EINVAL);

	return 0;
}


int smu7_upload_smu_firmware_image(struct pp_hwmgr *hwmgr)
{
	int result = 0;
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(hwmgr->smu_backend);

	struct cgs_firmware_info info = {0};

	if (smu_data->security_hard_key == 1)
		cgs_get_firmware_info(hwmgr->device,
			smu7_convert_fw_type_to_cgs(UCODE_ID_SMU), &info);
	else
		cgs_get_firmware_info(hwmgr->device,
			smu7_convert_fw_type_to_cgs(UCODE_ID_SMU_SK), &info);

	hwmgr->is_kicker = info.is_kicker;
	hwmgr->smu_version = info.version;
	result = smu7_upload_smc_firmware_data(hwmgr, info.image_size, (uint32_t *)info.kptr, SMU7_SMC_SIZE);

	return result;
}

static void execute_pwr_table(struct pp_hwmgr *hwmgr, const PWR_Command_Table *pvirus, int size)
{
	int i;
	uint32_t reg, data;

	for (i = 0; i < size; i++) {
		reg  = pvirus->reg;
		data = pvirus->data;
		if (reg != 0xffffffff)
			cgs_write_register(hwmgr->device, reg, data);
		else
			break;
		pvirus++;
	}
}

static void execute_pwr_dfy_table(struct pp_hwmgr *hwmgr, const PWR_DFY_Section *section)
{
	int i;

	cgs_write_register(hwmgr->device, mmCP_DFY_CNTL, section->dfy_cntl);
	cgs_write_register(hwmgr->device, mmCP_DFY_ADDR_HI, section->dfy_addr_hi);
	cgs_write_register(hwmgr->device, mmCP_DFY_ADDR_LO, section->dfy_addr_lo);
	for (i = 0; i < section->dfy_size; i++)
		cgs_write_register(hwmgr->device, mmCP_DFY_DATA_0, section->dfy_data[i]);
}

int smu7_setup_pwr_virus(struct pp_hwmgr *hwmgr)
{
	execute_pwr_table(hwmgr, pwr_virus_table_pre, ARRAY_SIZE(pwr_virus_table_pre));
	execute_pwr_dfy_table(hwmgr, &pwr_virus_section1);
	execute_pwr_dfy_table(hwmgr, &pwr_virus_section2);
	execute_pwr_dfy_table(hwmgr, &pwr_virus_section3);
	execute_pwr_dfy_table(hwmgr, &pwr_virus_section4);
	execute_pwr_dfy_table(hwmgr, &pwr_virus_section5);
	execute_pwr_dfy_table(hwmgr, &pwr_virus_section6);
	execute_pwr_table(hwmgr, pwr_virus_table_post, ARRAY_SIZE(pwr_virus_table_post));

	return 0;
}

int smu7_init(struct pp_hwmgr *hwmgr)
{
	struct smu7_smumgr *smu_data;
	int r;
	/* Allocate memory for backend private data */
	smu_data = (struct smu7_smumgr *)(hwmgr->smu_backend);
	smu_data->header_buffer.data_size =
			((sizeof(struct SMU_DRAMData_TOC) / 4096) + 1) * 4096;

/* Allocate FW image data structure and header buffer and
 * send the header buffer address to SMU */
	r = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
		smu_data->header_buffer.data_size,
		PAGE_SIZE,
		AMDGPU_GEM_DOMAIN_VRAM,
		&smu_data->header_buffer.handle,
		&smu_data->header_buffer.mc_addr,
		&smu_data->header_buffer.kaddr);

	if (r)
		return -EINVAL;

	if (!hwmgr->not_vf)
		return 0;

	smu_data->smu_buffer.data_size = 200*4096;
	r = amdgpu_bo_create_kernel((struct amdgpu_device *)hwmgr->adev,
		smu_data->smu_buffer.data_size,
		PAGE_SIZE,
		AMDGPU_GEM_DOMAIN_VRAM,
		&smu_data->smu_buffer.handle,
		&smu_data->smu_buffer.mc_addr,
		&smu_data->smu_buffer.kaddr);

	if (r) {
		amdgpu_bo_free_kernel(&smu_data->header_buffer.handle,
					&smu_data->header_buffer.mc_addr,
					&smu_data->header_buffer.kaddr);
		return -EINVAL;
	}

	if (smum_is_hw_avfs_present(hwmgr) &&
	    (hwmgr->feature_mask & PP_AVFS_MASK))
		hwmgr->avfs_supported = true;

	return 0;
}


int smu7_smu_fini(struct pp_hwmgr *hwmgr)
{
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(hwmgr->smu_backend);

	amdgpu_bo_free_kernel(&smu_data->header_buffer.handle,
					&smu_data->header_buffer.mc_addr,
					&smu_data->header_buffer.kaddr);

	if (hwmgr->not_vf)
		amdgpu_bo_free_kernel(&smu_data->smu_buffer.handle,
					&smu_data->smu_buffer.mc_addr,
					&smu_data->smu_buffer.kaddr);


	kfree(smu_data->toc);
	smu_data->toc = NULL;
	kfree(hwmgr->smu_backend);
	hwmgr->smu_backend = NULL;
	return 0;
}
