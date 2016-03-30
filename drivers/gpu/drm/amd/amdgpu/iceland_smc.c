/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include "drmP.h"
#include "amdgpu.h"
#include "ppsmc.h"
#include "iceland_smumgr.h"
#include "smu_ucode_xfer_vi.h"
#include "amdgpu_ucode.h"

#include "smu/smu_7_1_1_d.h"
#include "smu/smu_7_1_1_sh_mask.h"

#define ICELAND_SMC_SIZE 0x20000

static int iceland_set_smc_sram_address(struct amdgpu_device *adev,
					uint32_t smc_address, uint32_t limit)
{
	uint32_t val;

	if (smc_address & 3)
		return -EINVAL;

	if ((smc_address + 3) > limit)
		return -EINVAL;

	WREG32(mmSMC_IND_INDEX_0, smc_address);

	val = RREG32(mmSMC_IND_ACCESS_CNTL);
	val = REG_SET_FIELD(val, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 0);
	WREG32(mmSMC_IND_ACCESS_CNTL, val);

	return 0;
}

static int iceland_copy_bytes_to_smc(struct amdgpu_device *adev,
				     uint32_t smc_start_address,
				     const uint8_t *src,
				     uint32_t byte_count, uint32_t limit)
{
	uint32_t addr;
	uint32_t data, orig_data;
	int result = 0;
	uint32_t extra_shift;
	unsigned long flags;

	if (smc_start_address & 3)
		return -EINVAL;

	if ((smc_start_address + byte_count) > limit)
		return -EINVAL;

	addr = smc_start_address;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	while (byte_count >= 4) {
		/* Bytes are written into the SMC addres space with the MSB first */
		data = (src[0] << 24) + (src[1] << 16) + (src[2] << 8) + src[3];

		result = iceland_set_smc_sram_address(adev, addr, limit);

		if (result)
			goto out;

		WREG32(mmSMC_IND_DATA_0, data);

		src += 4;
		byte_count -= 4;
		addr += 4;
	}

	if (0 != byte_count) {
		/* Now write odd bytes left, do a read modify write cycle */
		data = 0;

		result = iceland_set_smc_sram_address(adev, addr, limit);
		if (result)
			goto out;

		orig_data = RREG32(mmSMC_IND_DATA_0);
		extra_shift = 8 * (4 - byte_count);

		while (byte_count > 0) {
			data = (data << 8) + *src++;
			byte_count--;
		}

		data <<= extra_shift;
		data |= (orig_data & ~((~0UL) << extra_shift));

		result = iceland_set_smc_sram_address(adev, addr, limit);
		if (result)
			goto out;

		WREG32(mmSMC_IND_DATA_0, data);
	}

out:
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
	return result;
}

void iceland_start_smc(struct amdgpu_device *adev)
{
	uint32_t val = RREG32_SMC(ixSMC_SYSCON_RESET_CNTL);

	val = REG_SET_FIELD(val, SMC_SYSCON_RESET_CNTL, rst_reg, 0);
	WREG32_SMC(ixSMC_SYSCON_RESET_CNTL, val);
}

void iceland_reset_smc(struct amdgpu_device *adev)
{
	uint32_t val = RREG32_SMC(ixSMC_SYSCON_RESET_CNTL);

	val = REG_SET_FIELD(val, SMC_SYSCON_RESET_CNTL, rst_reg, 1);
	WREG32_SMC(ixSMC_SYSCON_RESET_CNTL, val);
}

static int iceland_program_jump_on_start(struct amdgpu_device *adev)
{
	static unsigned char data[] = {0xE0, 0x00, 0x80, 0x40};
	iceland_copy_bytes_to_smc(adev, 0x0, data, 4, sizeof(data)+1);

	return 0;
}

void iceland_stop_smc_clock(struct amdgpu_device *adev)
{
	uint32_t val = RREG32_SMC(ixSMC_SYSCON_CLOCK_CNTL_0);

	val = REG_SET_FIELD(val, SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 1);
	WREG32_SMC(ixSMC_SYSCON_CLOCK_CNTL_0, val);
}

void iceland_start_smc_clock(struct amdgpu_device *adev)
{
	uint32_t val = RREG32_SMC(ixSMC_SYSCON_CLOCK_CNTL_0);

	val = REG_SET_FIELD(val, SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);
	WREG32_SMC(ixSMC_SYSCON_CLOCK_CNTL_0, val);
}

static bool iceland_is_smc_ram_running(struct amdgpu_device *adev)
{
	uint32_t val = RREG32_SMC(ixSMC_SYSCON_CLOCK_CNTL_0);
	val = REG_GET_FIELD(val, SMC_SYSCON_CLOCK_CNTL_0, ck_disable);

	return ((0 == val) && (0x20100 <= RREG32_SMC(ixSMC_PC_C)));
}

static int wait_smu_response(struct amdgpu_device *adev)
{
	int i;
	uint32_t val;

	for (i = 0; i < adev->usec_timeout; i++) {
		val = RREG32(mmSMC_RESP_0);
		if (REG_GET_FIELD(val, SMC_RESP_0, SMC_RESP))
			break;
		udelay(1);
	}

	if (i == adev->usec_timeout)
		return -EINVAL;

	return 0;
}

static int iceland_send_msg_to_smc(struct amdgpu_device *adev, PPSMC_Msg msg)
{
	if (!iceland_is_smc_ram_running(adev))
		return -EINVAL;

	if (wait_smu_response(adev)) {
		DRM_ERROR("Failed to send previous message\n");
		return -EINVAL;
	}

	WREG32(mmSMC_MESSAGE_0, msg);

	if (wait_smu_response(adev)) {
		DRM_ERROR("Failed to send message\n");
		return -EINVAL;
	}

	return 0;
}

static int iceland_send_msg_to_smc_without_waiting(struct amdgpu_device *adev,
						   PPSMC_Msg msg)
{
	if (!iceland_is_smc_ram_running(adev))
		return -EINVAL;;

	if (wait_smu_response(adev)) {
		DRM_ERROR("Failed to send previous message\n");
		return -EINVAL;
	}

	WREG32(mmSMC_MESSAGE_0, msg);

	return 0;
}

static int iceland_send_msg_to_smc_with_parameter(struct amdgpu_device *adev,
						  PPSMC_Msg msg,
						  uint32_t parameter)
{
	WREG32(mmSMC_MSG_ARG_0, parameter);

	return iceland_send_msg_to_smc(adev, msg);
}

static int iceland_send_msg_to_smc_with_parameter_without_waiting(
					struct amdgpu_device *adev,
					PPSMC_Msg msg, uint32_t parameter)
{
	WREG32(mmSMC_MSG_ARG_0, parameter);

	return iceland_send_msg_to_smc_without_waiting(adev, msg);
}

#if 0 /* not used yet */
static int iceland_wait_for_smc_inactive(struct amdgpu_device *adev)
{
	int i;
	uint32_t val;

	if (!iceland_is_smc_ram_running(adev))
		return -EINVAL;

	for (i = 0; i < adev->usec_timeout; i++) {
		val = RREG32_SMC(ixSMC_SYSCON_CLOCK_CNTL_0);
		if (REG_GET_FIELD(val, SMC_SYSCON_CLOCK_CNTL_0, cken) == 0)
			break;
		udelay(1);
	}

	if (i == adev->usec_timeout)
		return -EINVAL;

	return 0;
}
#endif

static int iceland_smu_upload_firmware_image(struct amdgpu_device *adev)
{
	const struct smc_firmware_header_v1_0 *hdr;
	uint32_t ucode_size;
	uint32_t ucode_start_address;
	const uint8_t *src;
	uint32_t val;
	uint32_t byte_count;
	uint32_t data;
	unsigned long flags;
	int i;

	if (!adev->pm.fw)
		return -EINVAL;

	/* Skip SMC ucode loading on SR-IOV capable boards.
	 * vbios does this for us in asic_init in that case.
	 */
	if (adev->virtualization.supports_sr_iov)
		return 0;

	hdr = (const struct smc_firmware_header_v1_0 *)adev->pm.fw->data;
	amdgpu_ucode_print_smc_hdr(&hdr->header);

	adev->pm.fw_version = le32_to_cpu(hdr->header.ucode_version);
	ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes);
	ucode_start_address = le32_to_cpu(hdr->ucode_start_addr);
	src = (const uint8_t *)
		(adev->pm.fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));

	if (ucode_size & 3) {
		DRM_ERROR("SMC ucode is not 4 bytes aligned\n");
		return -EINVAL;
	}

	if (ucode_size > ICELAND_SMC_SIZE) {
		DRM_ERROR("SMC address is beyond the SMC RAM area\n");
		return -EINVAL;
	}

	for (i = 0; i < adev->usec_timeout; i++) {
		val = RREG32_SMC(ixRCU_UC_EVENTS);
		if (REG_GET_FIELD(val, RCU_UC_EVENTS, boot_seq_done) == 0)
			break;
		udelay(1);
	}
	val = RREG32_SMC(ixSMC_SYSCON_MISC_CNTL);
	WREG32_SMC(ixSMC_SYSCON_MISC_CNTL, val | 1);

	iceland_stop_smc_clock(adev);
	iceland_reset_smc(adev);

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	WREG32(mmSMC_IND_INDEX_0, ucode_start_address);

	val = RREG32(mmSMC_IND_ACCESS_CNTL);
	val = REG_SET_FIELD(val, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 1);
	WREG32(mmSMC_IND_ACCESS_CNTL, val);

	byte_count = ucode_size;
	while (byte_count >= 4) {
		data = (src[0] << 24) + (src[1] << 16) + (src[2] << 8) + src[3];
		WREG32(mmSMC_IND_DATA_0, data);
		src += 4;
		byte_count -= 4;
	}
	val = RREG32(mmSMC_IND_ACCESS_CNTL);
	val = REG_SET_FIELD(val, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 0);
	WREG32(mmSMC_IND_ACCESS_CNTL, val);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);

	return 0;
}

#if 0 /* not used yet */
static int iceland_read_smc_sram_dword(struct amdgpu_device *adev,
				       uint32_t smc_address,
				       uint32_t *value,
				       uint32_t limit)
{
	int result;
	unsigned long flags;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	result = iceland_set_smc_sram_address(adev, smc_address, limit);
	if (result == 0)
		*value = RREG32(mmSMC_IND_DATA_0);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
	return result;
}

static int iceland_write_smc_sram_dword(struct amdgpu_device *adev,
					uint32_t smc_address,
					uint32_t value,
					uint32_t limit)
{
	int result;
	unsigned long flags;

	spin_lock_irqsave(&adev->smc_idx_lock, flags);
	result = iceland_set_smc_sram_address(adev, smc_address, limit);
	if (result == 0)
		WREG32(mmSMC_IND_DATA_0, value);
	spin_unlock_irqrestore(&adev->smc_idx_lock, flags);
	return result;
}

static int iceland_smu_stop_smc(struct amdgpu_device *adev)
{
	iceland_reset_smc(adev);
	iceland_stop_smc_clock(adev);

	return 0;
}
#endif

static int iceland_smu_start_smc(struct amdgpu_device *adev)
{
	int i;
	uint32_t val;

	iceland_program_jump_on_start(adev);
	iceland_start_smc_clock(adev);
	iceland_start_smc(adev);

	for (i = 0; i < adev->usec_timeout; i++) {
		val = RREG32_SMC(ixFIRMWARE_FLAGS);
		if (REG_GET_FIELD(val, FIRMWARE_FLAGS, INTERRUPTS_ENABLED) == 1)
			break;
		udelay(1);
	}
	return 0;
}

static enum AMDGPU_UCODE_ID iceland_convert_fw_type(uint32_t fw_type)
{
	switch (fw_type) {
		case UCODE_ID_SDMA0:
			return AMDGPU_UCODE_ID_SDMA0;
		case UCODE_ID_SDMA1:
			return AMDGPU_UCODE_ID_SDMA1;
		case UCODE_ID_CP_CE:
			return AMDGPU_UCODE_ID_CP_CE;
		case UCODE_ID_CP_PFP:
			return AMDGPU_UCODE_ID_CP_PFP;
		case UCODE_ID_CP_ME:
			return AMDGPU_UCODE_ID_CP_ME;
		case UCODE_ID_CP_MEC:
		case UCODE_ID_CP_MEC_JT1:
			return AMDGPU_UCODE_ID_CP_MEC1;
		case UCODE_ID_CP_MEC_JT2:
			return AMDGPU_UCODE_ID_CP_MEC2;
		case UCODE_ID_RLC_G:
			return AMDGPU_UCODE_ID_RLC_G;
		default:
			DRM_ERROR("ucode type is out of range!\n");
			return AMDGPU_UCODE_ID_MAXIMUM;
	}
}

static uint32_t iceland_smu_get_mask_for_fw_type(uint32_t fw_type)
{
	switch (fw_type) {
		case AMDGPU_UCODE_ID_SDMA0:
			return UCODE_ID_SDMA0_MASK;
		case AMDGPU_UCODE_ID_SDMA1:
			return UCODE_ID_SDMA1_MASK;
		case AMDGPU_UCODE_ID_CP_CE:
			return UCODE_ID_CP_CE_MASK;
		case AMDGPU_UCODE_ID_CP_PFP:
			return UCODE_ID_CP_PFP_MASK;
		case AMDGPU_UCODE_ID_CP_ME:
			return UCODE_ID_CP_ME_MASK;
		case AMDGPU_UCODE_ID_CP_MEC1:
			return UCODE_ID_CP_MEC_MASK | UCODE_ID_CP_MEC_JT1_MASK;
		case AMDGPU_UCODE_ID_CP_MEC2:
			return UCODE_ID_CP_MEC_MASK;
		case AMDGPU_UCODE_ID_RLC_G:
			return UCODE_ID_RLC_G_MASK;
		default:
			DRM_ERROR("ucode type is out of range!\n");
			return 0;
	}
}

static int iceland_smu_populate_single_firmware_entry(struct amdgpu_device *adev,
						      uint32_t fw_type,
						      struct SMU_Entry *entry)
{
	enum AMDGPU_UCODE_ID id = iceland_convert_fw_type(fw_type);
	struct amdgpu_firmware_info *ucode = &adev->firmware.ucode[id];
	const struct gfx_firmware_header_v1_0 *header = NULL;
	uint64_t gpu_addr;
	uint32_t data_size;

	if (ucode->fw == NULL)
		return -EINVAL;

	gpu_addr  = ucode->mc_addr;
	header = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
	data_size = le32_to_cpu(header->header.ucode_size_bytes);

	entry->version = (uint16_t)le32_to_cpu(header->header.ucode_version);
	entry->id = (uint16_t)fw_type;
	entry->image_addr_high = upper_32_bits(gpu_addr);
	entry->image_addr_low = lower_32_bits(gpu_addr);
	entry->meta_data_addr_high = 0;
	entry->meta_data_addr_low = 0;
	entry->data_size_byte = data_size;
	entry->num_register_entries = 0;
	entry->flags = 0;

	return 0;
}

static int iceland_smu_request_load_fw(struct amdgpu_device *adev)
{
	struct iceland_smu_private_data *private = (struct iceland_smu_private_data *)adev->smu.priv;
	struct SMU_DRAMData_TOC *toc;
	uint32_t fw_to_load;

	toc = (struct SMU_DRAMData_TOC *)private->header;
	toc->num_entries = 0;
	toc->structure_version = 1;

	if (!adev->firmware.smu_load)
		return 0;

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_RLC_G,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for RLC\n");
		return -EINVAL;
	}

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_CP_CE,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for CE\n");
		return -EINVAL;
	}

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_CP_PFP,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for PFP\n");
		return -EINVAL;
	}

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_CP_ME,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for ME\n");
		return -EINVAL;
	}

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_CP_MEC,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for MEC\n");
		return -EINVAL;
	}

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_CP_MEC_JT1,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for MEC_JT1\n");
		return -EINVAL;
	}

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_SDMA0,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for SDMA0\n");
		return -EINVAL;
	}

	if (iceland_smu_populate_single_firmware_entry(adev, UCODE_ID_SDMA1,
			&toc->entry[toc->num_entries++])) {
		DRM_ERROR("Failed to get firmware entry for SDMA1\n");
		return -EINVAL;
	}

	iceland_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_DRV_DRAM_ADDR_HI, private->header_addr_high);
	iceland_send_msg_to_smc_with_parameter(adev, PPSMC_MSG_DRV_DRAM_ADDR_LO, private->header_addr_low);

	fw_to_load = UCODE_ID_RLC_G_MASK |
			UCODE_ID_SDMA0_MASK |
			UCODE_ID_SDMA1_MASK |
			UCODE_ID_CP_CE_MASK |
			UCODE_ID_CP_ME_MASK |
			UCODE_ID_CP_PFP_MASK |
			UCODE_ID_CP_MEC_MASK |
			UCODE_ID_CP_MEC_JT1_MASK;


	if (iceland_send_msg_to_smc_with_parameter_without_waiting(adev, PPSMC_MSG_LoadUcodes, fw_to_load)) {
		DRM_ERROR("Fail to request SMU load ucode\n");
		return -EINVAL;
	}

	return 0;
}

static int iceland_smu_check_fw_load_finish(struct amdgpu_device *adev,
					    uint32_t fw_type)
{
	uint32_t fw_mask = iceland_smu_get_mask_for_fw_type(fw_type);
	int i;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (fw_mask == (RREG32_SMC(ixSOFT_REGISTERS_TABLE_27) & fw_mask))
			break;
		udelay(1);
	}

	if (i == adev->usec_timeout) {
		DRM_ERROR("check firmware loading failed\n");
		return -EINVAL;
	}

	return 0;
}

int iceland_smu_start(struct amdgpu_device *adev)
{
	int result;

	result = iceland_smu_upload_firmware_image(adev);
	if (result)
		return result;
	result = iceland_smu_start_smc(adev);
	if (result)
		return result;

	return iceland_smu_request_load_fw(adev);
}

static const struct amdgpu_smumgr_funcs iceland_smumgr_funcs = {
	.check_fw_load_finish = iceland_smu_check_fw_load_finish,
	.request_smu_load_fw = NULL,
	.request_smu_specific_fw = NULL,
};

int iceland_smu_init(struct amdgpu_device *adev)
{
	struct iceland_smu_private_data *private;
	uint32_t image_size = ((sizeof(struct SMU_DRAMData_TOC) / 4096) + 1) * 4096;
	struct amdgpu_bo **toc_buf = &adev->smu.toc_buf;
	uint64_t mc_addr;
	void *toc_buf_ptr;
	int ret;

	private = kzalloc(sizeof(struct iceland_smu_private_data), GFP_KERNEL);
	if (NULL == private)
		return -ENOMEM;

	/* allocate firmware buffers */
	if (adev->firmware.smu_load)
		amdgpu_ucode_init_bo(adev);

	adev->smu.priv = private;
	adev->smu.fw_flags = 0;

	/* Allocate FW image data structure and header buffer */
	ret = amdgpu_bo_create(adev, image_size, PAGE_SIZE,
			       true, AMDGPU_GEM_DOMAIN_VRAM,
			       AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
			       NULL, NULL, toc_buf);
	if (ret) {
		DRM_ERROR("Failed to allocate memory for TOC buffer\n");
		return -ENOMEM;
	}

	/* Retrieve GPU address for header buffer and internal buffer */
	ret = amdgpu_bo_reserve(adev->smu.toc_buf, false);
	if (ret) {
		amdgpu_bo_unref(&adev->smu.toc_buf);
		DRM_ERROR("Failed to reserve the TOC buffer\n");
		return -EINVAL;
	}

	ret = amdgpu_bo_pin(adev->smu.toc_buf, AMDGPU_GEM_DOMAIN_VRAM, &mc_addr);
	if (ret) {
		amdgpu_bo_unreserve(adev->smu.toc_buf);
		amdgpu_bo_unref(&adev->smu.toc_buf);
		DRM_ERROR("Failed to pin the TOC buffer\n");
		return -EINVAL;
	}

	ret = amdgpu_bo_kmap(*toc_buf, &toc_buf_ptr);
	if (ret) {
		amdgpu_bo_unreserve(adev->smu.toc_buf);
		amdgpu_bo_unref(&adev->smu.toc_buf);
		DRM_ERROR("Failed to map the TOC buffer\n");
		return -EINVAL;
	}

	amdgpu_bo_unreserve(adev->smu.toc_buf);
	private->header_addr_low = lower_32_bits(mc_addr);
	private->header_addr_high = upper_32_bits(mc_addr);
	private->header = toc_buf_ptr;

	adev->smu.smumgr_funcs = &iceland_smumgr_funcs;

	return 0;
}

int iceland_smu_fini(struct amdgpu_device *adev)
{
	amdgpu_bo_unref(&adev->smu.toc_buf);
	kfree(adev->smu.priv);
	adev->smu.priv = NULL;
	if (adev->firmware.fw_buf)
		amdgpu_ucode_fini_bo(adev);

	return 0;
}
