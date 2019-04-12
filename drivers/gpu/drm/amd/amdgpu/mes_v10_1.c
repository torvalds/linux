/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include "amdgpu.h"
#include "soc15_common.h"

MODULE_FIRMWARE("amdgpu/navi10_mes.bin");

static int mes_v10_1_add_hw_queue(struct amdgpu_mes *mes,
				  struct mes_add_queue_input *input)
{
	return 0;
}

static int mes_v10_1_remove_hw_queue(struct amdgpu_mes *mes,
				     struct mes_remove_queue_input *input)
{
	return 0;
}

static int mes_v10_1_suspend_gang(struct amdgpu_mes *mes,
				  struct mes_suspend_gang_input *input)
{
	return 0;
}

static int mes_v10_1_resume_gang(struct amdgpu_mes *mes,
				 struct mes_resume_gang_input *input)
{
	return 0;
}

static const struct amdgpu_mes_funcs mes_v10_1_funcs = {
	.add_hw_queue = mes_v10_1_add_hw_queue,
	.remove_hw_queue = mes_v10_1_remove_hw_queue,
	.suspend_gang = mes_v10_1_suspend_gang,
	.resume_gang = mes_v10_1_resume_gang,
};

static int mes_v10_1_init_microcode(struct amdgpu_device *adev)
{
	const char *chip_name;
	char fw_name[30];
	int err;
	const struct mes_firmware_header_v1_0 *mes_hdr;

	switch (adev->asic_type) {
	case CHIP_NAVI10:
		chip_name = "navi10";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes.bin", chip_name);
	err = request_firmware(&adev->mes.fw, fw_name, adev->dev);
	if (err)
		return err;

	err = amdgpu_ucode_validate(adev->mes.fw);
	if (err) {
		release_firmware(adev->mes.fw);
		adev->mes.fw = NULL;
		return err;
	}

	mes_hdr = (const struct mes_firmware_header_v1_0 *)adev->mes.fw->data;
	adev->mes.ucode_fw_version = le32_to_cpu(mes_hdr->mes_ucode_version);
	adev->mes.ucode_fw_version =
		le32_to_cpu(mes_hdr->mes_ucode_data_version);
	adev->mes.uc_start_addr =
		le32_to_cpu(mes_hdr->mes_uc_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_uc_start_addr_hi)) << 32);
	adev->mes.data_start_addr =
		le32_to_cpu(mes_hdr->mes_data_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_data_start_addr_hi)) << 32);

	return 0;
}

static void mes_v10_1_free_microcode(struct amdgpu_device *adev)
{
	release_firmware(adev->mes.fw);
	adev->mes.fw = NULL;
}

static int mes_v10_1_allocate_ucode_buffer(struct amdgpu_device *adev)
{
	int r;
	const struct mes_firmware_header_v1_0 *mes_hdr;
	const __le32 *fw_data;
	unsigned fw_size;

	mes_hdr = (const struct mes_firmware_header_v1_0 *)
		adev->mes.fw->data;

	fw_data = (const __le32 *)(adev->mes.fw->data +
		   le32_to_cpu(mes_hdr->mes_ucode_offset_bytes));
	fw_size = le32_to_cpu(mes_hdr->mes_ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, fw_size,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				      &adev->mes.ucode_fw_obj,
				      &adev->mes.ucode_fw_gpu_addr,
				      (void **)&adev->mes.ucode_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create mes fw bo\n", r);
		return r;
	}

	memcpy(adev->mes.ucode_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->mes.ucode_fw_obj);
	amdgpu_bo_unreserve(adev->mes.ucode_fw_obj);

	return 0;
}

static int mes_v10_1_sw_init(void *handle)
{
	return 0;
}

static int mes_v10_1_sw_fini(void *handle)
{
	return 0;
}

static int mes_v10_1_hw_init(void *handle)
{
	return 0;
}

static int mes_v10_1_hw_fini(void *handle)
{
	return 0;
}

static int mes_v10_1_suspend(void *handle)
{
	return 0;
}

static int mes_v10_1_resume(void *handle)
{
	return 0;
}

static const struct amd_ip_funcs mes_v10_1_ip_funcs = {
	.name = "mes_v10_1",
	.sw_init = mes_v10_1_sw_init,
	.sw_fini = mes_v10_1_sw_fini,
	.hw_init = mes_v10_1_hw_init,
	.hw_fini = mes_v10_1_hw_fini,
	.suspend = mes_v10_1_suspend,
	.resume = mes_v10_1_resume,
};

const struct amdgpu_ip_block_version mes_v10_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_MES,
	.major = 10,
	.minor = 1,
	.rev = 0,
	.funcs = &mes_v10_1_ip_funcs,
};
