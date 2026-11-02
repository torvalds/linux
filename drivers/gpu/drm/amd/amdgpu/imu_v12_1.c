/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "amdgpu_imu.h"
#include "amdgpu_dpm.h"

#include "imu_v12_1.h"

#include "gc/gc_12_1_0_offset.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "mmhub/mmhub_4_2_0_offset.h"

#define TRANSFER_RAM_MASK	0x001c0000

static int imu_v12_1_init_microcode(struct amdgpu_device *adev)
{
	char ucode_prefix[15];
	int err;
	const struct imu_firmware_header_v1_0 *imu_hdr;
	struct amdgpu_firmware_info *info = NULL;

	DRM_DEBUG("\n");

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix, sizeof(ucode_prefix));
	err = amdgpu_ucode_request(adev, &adev->gfx.imu_fw, AMDGPU_UCODE_REQUIRED,
				   "amdgpu/%s_imu.bin", ucode_prefix);
	if (err)
		goto out;

	imu_hdr = (const struct imu_firmware_header_v1_0 *)adev->gfx.imu_fw->data;
	adev->gfx.imu_fw_version = le32_to_cpu(imu_hdr->header.ucode_version);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_IMU_I];
		info->ucode_id = AMDGPU_UCODE_ID_IMU_I;
		info->fw = adev->gfx.imu_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(imu_hdr->imu_iram_ucode_size_bytes), PAGE_SIZE);
		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_IMU_D];
		info->ucode_id = AMDGPU_UCODE_ID_IMU_D;
		info->fw = adev->gfx.imu_fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(imu_hdr->imu_dram_ucode_size_bytes), PAGE_SIZE);
	}

out:
	if (err) {
		dev_err(adev->dev,
			"gfx12: Failed to load firmware \"%s_imu.bin\"\n",
			ucode_prefix);
		amdgpu_ucode_release(&adev->gfx.imu_fw);
	}

	return err;
}

static void imu_v12_1_xcc_load_microcode(struct amdgpu_device *adev,
					 int xcc_id)
{
	const struct imu_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	hdr = (const struct imu_firmware_header_v1_0 *)adev->gfx.imu_fw->data;
	fw_data = (const __le32 *)(adev->gfx.imu_fw->data +
			le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(hdr->imu_iram_ucode_size_bytes) / 4;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regGFX_IMU_I_RAM_ADDR, 0);

	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regGFX_IMU_I_RAM_DATA,
			     le32_to_cpup(fw_data++));

	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regGFX_IMU_I_RAM_ADDR,
		     adev->gfx.imu_fw_version);

	fw_data = (const __le32 *)(adev->gfx.imu_fw->data +
			le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
			le32_to_cpu(hdr->imu_iram_ucode_size_bytes));
	fw_size = le32_to_cpu(hdr->imu_dram_ucode_size_bytes) / 4;

	WREG32_SOC15(GC, GET_INST(GC, xcc_id), regGFX_IMU_D_RAM_ADDR, 0);

	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, GET_INST(GC, xcc_id),
			     regGFX_IMU_D_RAM_DATA,
			     le32_to_cpup(fw_data++));

	WREG32_SOC15(GC, GET_INST(GC, xcc_id),
		     regGFX_IMU_D_RAM_ADDR,
		     adev->gfx.imu_fw_version);
}

static int imu_v12_1_load_microcode(struct amdgpu_device *adev)
{
	int i, num_xcc;

	if (!adev->gfx.imu_fw)
		return -EINVAL;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < num_xcc; i++) {
		imu_v12_1_xcc_load_microcode(adev, i);
	}

	return 0;
}

#define regGFX_IMU_PARTITION_SWITCH		0x5f8c
#define regGFX_IMU_PARTITION_SWITCH_BASE_IDX	1
#define GFX_IMU_PARTITION_SWITCH__PARTITION_MODE__SHIFT		0x0
#define GFX_IMU_PARTITION_SWITCH__TOTAL_XCCS_IN_XCP__SHIFT	0x2
#define GFX_IMU_PARTITION_SWITCH__VIRTUAL_XCC_ID__SHIFT		0x9
#define GFX_IMU_PARTITION_SWITCH__PHYSICAL_XCC_MASK__SHIFT	0x10
#define GFX_IMU_PARTITION_SWITCH__PARTITION_MODE_MASK		0x00000003L
#define GFX_IMU_PARTITION_SWITCH__TOTAL_XCCS_IN_XCP_MASK	0x0000003CL
#define GFX_IMU_PARTITION_SWITCH__VIRTUAL_XCC_ID_MASK		0x00000E00L
#define GFX_IMU_PARTITION_SWITCH__PHYSICAL_XCC_MASK_MASK	0x00FF0000L

static int imu_v12_1_switch_compute_partition(struct amdgpu_device *adev,
					      int num_xccs_per_xcp,
					      int compute_partition_mode)
{
	int ret, i, num_xcc;
	u32 tmp = 0, physical_xcc_mask = 0;

	if (adev->psp.funcs) {
		ret = psp_spatial_partition(&adev->psp, compute_partition_mode);
		if (ret)
			return ret;
	} else {
		num_xcc = NUM_XCC(adev->gfx.xcc_mask);
		/* 00 - SPX; 01 - DPX; 10 - QPX; 11 - CPX */
		switch (compute_partition_mode) {
		case AMDGPU_SPX_PARTITION_MODE:
			compute_partition_mode = 0;
			break;
		case AMDGPU_DPX_PARTITION_MODE:
			compute_partition_mode = 1;
			break;
		case AMDGPU_QPX_PARTITION_MODE:
			compute_partition_mode = 2;
			break;
		case AMDGPU_CPX_PARTITION_MODE:
			compute_partition_mode = 3;
			break;
		default:
			dev_err(adev->dev, "Invalid compute partition mode\n");
			return -EINVAL;
		}

		num_xcc = NUM_XCC(adev->gfx.xcc_mask);
		for(i = 0; i < num_xcc; i++) {
			if (i % num_xccs_per_xcp == 0) {
				physical_xcc_mask =
					adev->gfx.xcc_mask &
					(GENMASK(GET_INST(GC, i + num_xccs_per_xcp - 1),
						 GET_INST(GC, i)));
			}
			tmp = REG_SET_FIELD(tmp, GFX_IMU_PARTITION_SWITCH,
					    PARTITION_MODE, compute_partition_mode);
			tmp = REG_SET_FIELD(tmp, GFX_IMU_PARTITION_SWITCH,
					    TOTAL_XCCS_IN_XCP, num_xccs_per_xcp);
			tmp = REG_SET_FIELD(tmp, GFX_IMU_PARTITION_SWITCH,
					    VIRTUAL_XCC_ID, i % num_xccs_per_xcp);
			tmp = REG_SET_FIELD(tmp, GFX_IMU_PARTITION_SWITCH,
					    PHYSICAL_XCC_MASK, physical_xcc_mask);
			WREG32_SOC15(GC, GET_INST(GC, i), regGFX_IMU_PARTITION_SWITCH, tmp);
		}
		ret = 0;
	}

	adev->gfx.num_xcc_per_xcp = num_xccs_per_xcp;
	return 0;
}

#define regGFX_IMU_MCM_ADDR_LUT_PF		0x5f8d
#define regGFX_IMU_MCM_ADDR_LUT_PF_BASE_IDX	1

static void imu_v12_1_init_mcm_addr_lut(struct amdgpu_device *adev)
{
	/* Encoding mcm_addr_lut in SPX mode for now
	 * I've noticed variations in encoding
	 * Need to revisit the configuration later
	 * XCD0:: Bits-[3:0 ] AID-01 XCD-00
	 * XCD1:: Bits-[7:4 ] AID-01 XCD-01
	 * XCD2:: Bits-[11:8 ] AID-01 XCD-10
	 * XCD3:: Bits-[15:12] AID-01 XCD-11
	 * XCD4:: Bits-[19:16] AID-10 XCD-00
	 * XCD5:: Bits-[23:20] AID-10 XCD-01
	 * XCD6:: Bits-[27:24] AID-10 XCD-10
	 * XCD7:: Bits-[31:28] AID-10 XCD-11
	 */
	int data, encode_data;
	int i, j, num_xcc;

	num_xcc = NUM_XCC(adev->gfx.xcc_mask);
	for (i = 0; i < (num_xcc / adev->gfx.num_xcc_per_xcp); i++) {
		encode_data = 0;
		for (j = 0; j < adev->gfx.num_xcc_per_xcp; j++) {
			data = ((GET_INST(GC, i * adev->gfx.num_xcc_per_xcp + j) / 4 + 1) << 2) |
			        (GET_INST(GC, i * adev->gfx.num_xcc_per_xcp + j) % 4);
			encode_data |= data << (j * 4);
		}
		for (j = 0; j < adev->gfx.num_xcc_per_xcp; j++)
			WREG32_SOC15(GC, GET_INST(GC, i * adev->gfx.num_xcc_per_xcp + j),
					regGFX_IMU_MCM_ADDR_LUT_PF, encode_data);
	}
}

const struct amdgpu_imu_funcs gfx_v12_1_imu_funcs = {
	.init_microcode = imu_v12_1_init_microcode,
	.load_microcode = imu_v12_1_load_microcode,
	.switch_compute_partition = imu_v12_1_switch_compute_partition,
	.init_mcm_addr_lut = imu_v12_1_init_mcm_addr_lut,
};
