/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "imu_v11_0_3.h"

#include "gc/gc_11_0_0_offset.h"
#include "gc/gc_11_0_0_sh_mask.h"

MODULE_FIRMWARE("amdgpu/gc_11_0_0_imu.bin");
MODULE_FIRMWARE("amdgpu/gc_11_0_1_imu.bin");
MODULE_FIRMWARE("amdgpu/gc_11_0_2_imu.bin");
MODULE_FIRMWARE("amdgpu/gc_11_0_3_imu.bin");
MODULE_FIRMWARE("amdgpu/gc_11_0_4_imu.bin");

static int imu_v11_0_init_microcode(struct amdgpu_device *adev)
{
	char fw_name[40];
	char ucode_prefix[30];
	int err;
	const struct imu_firmware_header_v1_0 *imu_hdr;
	struct amdgpu_firmware_info *info = NULL;

	DRM_DEBUG("\n");

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix, sizeof(ucode_prefix));

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_imu.bin", ucode_prefix);
	err = amdgpu_ucode_request(adev, &adev->gfx.imu_fw, fw_name);
	if (err)
		goto out;
	imu_hdr = (const struct imu_firmware_header_v1_0 *)adev->gfx.imu_fw->data;
	adev->gfx.imu_fw_version = le32_to_cpu(imu_hdr->header.ucode_version);
	//adev->gfx.imu_feature_version = le32_to_cpu(imu_hdr->ucode_feature_version);
	
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
			"gfx11: Failed to load firmware \"%s\"\n",
			fw_name);
		amdgpu_ucode_release(&adev->gfx.imu_fw);
	}

	return err;
}

static int imu_v11_0_load_microcode(struct amdgpu_device *adev)
{
	const struct imu_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.imu_fw)
		return -EINVAL;

	hdr = (const struct imu_firmware_header_v1_0 *)adev->gfx.imu_fw->data;
	//amdgpu_ucode_print_rlc_hdr(&hdr->header);

	fw_data = (const __le32 *)(adev->gfx.imu_fw->data +
			le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(hdr->imu_iram_ucode_size_bytes) / 4;

	WREG32_SOC15(GC, 0, regGFX_IMU_I_RAM_ADDR, 0);

	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, 0, regGFX_IMU_I_RAM_DATA, le32_to_cpup(fw_data++));

	WREG32_SOC15(GC, 0, regGFX_IMU_I_RAM_ADDR, adev->gfx.imu_fw_version);

	fw_data = (const __le32 *)(adev->gfx.imu_fw->data +
			le32_to_cpu(hdr->header.ucode_array_offset_bytes) +
			le32_to_cpu(hdr->imu_iram_ucode_size_bytes));
	fw_size = le32_to_cpu(hdr->imu_dram_ucode_size_bytes) / 4;

	WREG32_SOC15(GC, 0, regGFX_IMU_D_RAM_ADDR, 0);

	for (i = 0; i < fw_size; i++)
		WREG32_SOC15(GC, 0, regGFX_IMU_D_RAM_DATA, le32_to_cpup(fw_data++));

	WREG32_SOC15(GC, 0, regGFX_IMU_D_RAM_ADDR, adev->gfx.imu_fw_version);

	return 0;
}

static int imu_v11_0_wait_for_reset_status(struct amdgpu_device *adev)
{
	int i, imu_reg_val = 0;

	for (i = 0; i < adev->usec_timeout; i++) {
		imu_reg_val = RREG32_SOC15(GC, 0, regGFX_IMU_GFX_RESET_CTRL);
		if ((imu_reg_val & 0x1f) == 0x1f)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout) {
		dev_err(adev->dev, "init imu: IMU start timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void imu_v11_0_setup(struct amdgpu_device *adev)
{
	int imu_reg_val;

	//enable IMU debug mode
	WREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_ACCESS_CTRL0, 0xffffff);
	WREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_ACCESS_CTRL1, 0xffff);

	if (adev->gfx.imu.mode == DEBUG_MODE) {
		imu_reg_val = RREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_16);
		imu_reg_val |= 0x1;
		WREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_16, imu_reg_val);
	}

	//disble imu Rtavfs, SmsRepair, DfllBTC, and ClkB
	imu_reg_val = RREG32_SOC15(GC, 0, regGFX_IMU_SCRATCH_10);
	imu_reg_val |= 0x10007;
	WREG32_SOC15(GC, 0, regGFX_IMU_SCRATCH_10, imu_reg_val);
}

static int imu_v11_0_start(struct amdgpu_device *adev)
{
	int imu_reg_val;

	//Start IMU by set GFX_IMU_CORE_CTRL.CRESET = 0
	imu_reg_val = RREG32_SOC15(GC, 0, regGFX_IMU_CORE_CTRL);
	imu_reg_val &= 0xfffffffe;
	WREG32_SOC15(GC, 0, regGFX_IMU_CORE_CTRL, imu_reg_val);

	if (adev->flags & AMD_IS_APU)
		amdgpu_dpm_set_gfx_power_up_by_imu(adev);

	return imu_v11_0_wait_for_reset_status(adev);
}

static const struct imu_rlc_ram_golden imu_rlc_ram_golden_11[] =
{
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_IO_RD_COMBINE_FLUSH, 0x00055555, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_IO_WR_COMBINE_FLUSH, 0x00055555, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_DRAM_COMBINE_FLUSH, 0x00555555, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_MISC2, 0x00001ffe, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_CREDITS , 0x003f3fff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_TAG_RESERVE1, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCC_RESERVE0, 0x00041000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCC_RESERVE1, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCD_RESERVE0, 0x00040000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCD_RESERVE1, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_MISC, 0x00000017, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_ENABLE, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_CREDITS , 0x003f3fbf, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_TAG_RESERVE0, 0x10201000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_TAG_RESERVE1, 0x00000080, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_VCC_RESERVE0, 0x1d041040, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_VCC_RESERVE1, 0x80000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_IO_PRIORITY, 0x88888888, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_MAM_CTRL, 0x0000d800, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_ARB_FINAL, 0x000003f7, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_ENABLE, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_PROTECTION_FAULT_CNTL2, 0x00020000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_APT_CNTL, 0x0000000c, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_CACHEABLE_DRAM_ADDRESS_END, 0x000fffff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_MISC, 0x0c48bff0, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_GC_SA_UNIT_DISABLE, 0x00fffc01, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_GC_PRIM_CONFIG, 0x000fffe1, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_RB_BACKEND_DISABLE, 0x0fffff01, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_GC_SHADER_ARRAY_CONFIG, 0xfffe0001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_MX_L1_TLB_CNTL, 0x00000500, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_START, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_END, 0x000fffff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT0_CNTL, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT1_CNTL, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_TOP_OF_DRAM_SLOT1, 0xff800000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_LOWER_TOP_OF_DRAM2, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_UPPER_TOP_OF_DRAM2, 0x00000fff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_PROTECTION_FAULT_CNTL, 0x00001ffc, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_MX_L1_TLB_CNTL, 0x00000501, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL, 0x00080603, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL2, 0x00000003, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL3, 0x00100003, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL5, 0x00003fe0, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT0_CNTL, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES, 0x00000c00, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT1_CNTL, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES, 0x00000c00, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGB_ADDR_CONFIG, 0x00000545, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_0, 0x13455431, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_1, 0x13455431, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_2, 0x76027602, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_3, 0x76207620, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGB_ADDR_CONFIG, 0x00000345, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCUTCL2_HARVEST_BYPASS_GROUPS, 0x0000003e, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_FB_LOCATION_BASE, 0x00006000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_FB_LOCATION_TOP, 0x000061ff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_APT_CNTL, 0x0000000c, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_BASE, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_BOT, 0x00000002, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_TOP, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_PROTECTION_FAULT_CNTL2, 0x00020000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regSDMA0_UCODE_SELFLOAD_CONTROL, 0x00000210, 0), 
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regSDMA1_UCODE_SELFLOAD_CONTROL, 0x00000210, 0), 
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCPC_PSP_DEBUG, CPC_PSP_DEBUG__GPA_OVERRIDE_MASK, 0), 
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCPG_PSP_DEBUG, CPG_PSP_DEBUG__GPA_OVERRIDE_MASK, 0)
};

static const struct imu_rlc_ram_golden imu_rlc_ram_golden_11_0_2[] =
{
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_MISC, 0x0c48bff0, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_CREDITS, 0x003f3fbf, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_TAG_RESERVE0, 0x10200800, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_TAG_RESERVE1, 0x00000088, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_VCC_RESERVE0, 0x1d041040, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_VCC_RESERVE1, 0x80000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_IO_PRIORITY, 0x88888888, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_MAM_CTRL, 0x0000d800, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_ARB_FINAL, 0x000007ef, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_DRAM_PAGE_BURST, 0x20080200, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCEA_SDP_ENABLE, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_APT_CNTL, 0x0000000c, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_CACHEABLE_DRAM_ADDRESS_END, 0x000fffff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_IO_RD_COMBINE_FLUSH, 0x00055555, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_IO_WR_COMBINE_FLUSH, 0x00055555, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_DRAM_COMBINE_FLUSH, 0x00555555, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_MISC2, 0x00001ffe, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_CREDITS, 0x003f3fff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_TAG_RESERVE1, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCC_RESERVE0, 0x00041000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCC_RESERVE1, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCD_RESERVE0, 0x00040000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_VCD_RESERVE1, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_MISC, 0x00000017, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGUS_SDP_ENABLE, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_GC_SA_UNIT_DISABLE, 0x00fffc01, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_GC_PRIM_CONFIG, 0x000fffe1, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_RB_BACKEND_DISABLE, 0x00000f01, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCC_GC_SHADER_ARRAY_CONFIG, 0xfffe0001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL1_PIPE_STEER, 0x000000e4, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCH_PIPE_STEER, 0x000000e4, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_0, 0x01231023, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGB_ADDR_CONFIG, 0x00000243, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCUTCL2_HARVEST_BYPASS_GROUPS, 0x00000002, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_MX_L1_TLB_CNTL, 0x00000500, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_START, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_END, 0x000001ff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_FB_LOCATION_BASE, 0x00006000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_FB_LOCATION_TOP, 0x000061ff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT0_CNTL, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT1_CNTL, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_APT_CNTL, 0x0000000c, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_TOP_OF_DRAM_SLOT1, 0xff800000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_LOWER_TOP_OF_DRAM2, 0x00000001, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_UPPER_TOP_OF_DRAM2, 0x00000fff, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_BASE, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_BOT, 0x00000002, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_TOP, 0x00000000, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_PROTECTION_FAULT_CNTL, 0x00001ffc, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_PROTECTION_FAULT_CNTL2, 0x00002825, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_MX_L1_TLB_CNTL, 0x00000501, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL, 0x00080603, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL2, 0x00000003, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL3, 0x00100003, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL5, 0x00003fe0, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT0_CNTL, 0x00000001, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CONTEXT0_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES, 0x00000c00, 0xe0000000),
        IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT1_CNTL, 0x00000001, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CONTEXT1_PER_PFVF_PTE_CACHE_FRAGMENT_SIZES, 0x00000c00, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regSDMA0_UCODE_SELFLOAD_CONTROL, 0x00000210, 0),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regSDMA1_UCODE_SELFLOAD_CONTROL, 0x00000210, 0),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCPC_PSP_DEBUG, CPC_PSP_DEBUG__GPA_OVERRIDE_MASK, 0),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCPG_PSP_DEBUG, CPG_PSP_DEBUG__GPA_OVERRIDE_MASK, 0)
};

static void program_imu_rlc_ram(struct amdgpu_device *adev,
				const struct imu_rlc_ram_golden *regs,
				const u32 array_size)
{
	const struct imu_rlc_ram_golden *entry;
	u32 reg, data;
	int i;

	for (i = 0; i < array_size; ++i) {
		entry = &regs[i];
		reg =  adev->reg_offset[entry->hwip][entry->instance][entry->segment] + entry->reg;
		reg |= entry->addr_mask;

		data = entry->data;
		if (entry->reg == regGCMC_VM_AGP_BASE)
			data = 0x00ffffff;
		else if (entry->reg == regGCMC_VM_AGP_TOP)
			data = 0x0;
		else if (entry->reg == regGCMC_VM_FB_LOCATION_BASE)
			data = adev->gmc.vram_start >> 24;
		else if (entry->reg == regGCMC_VM_FB_LOCATION_TOP)
			data = adev->gmc.vram_end >> 24;

		WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_HIGH, 0);
		WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_LOW, reg);
		WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_DATA, data);
	}
	//Indicate the latest entry
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_HIGH, 0);
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_LOW, 0);
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_DATA, 0);
}

static void imu_v11_0_program_rlc_ram(struct amdgpu_device *adev)
{
	u32 reg_data;

	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_INDEX, 0x2);

	switch (adev->ip_versions[GC_HWIP][0]) {
	case IP_VERSION(11, 0, 0):
		program_imu_rlc_ram(adev, imu_rlc_ram_golden_11,
				(const u32)ARRAY_SIZE(imu_rlc_ram_golden_11));
		break;
	case IP_VERSION(11, 0, 2):
		program_imu_rlc_ram(adev, imu_rlc_ram_golden_11_0_2,
				(const u32)ARRAY_SIZE(imu_rlc_ram_golden_11_0_2));
		break;
	case IP_VERSION(11, 0, 3):
		imu_v11_0_3_program_rlc_ram(adev);
		break;
	default:
		BUG();
		break;
	}

	//Indicate the contents of the RAM are valid
	reg_data = RREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_INDEX);
	reg_data |= GFX_IMU_RLC_RAM_INDEX__RAM_VALID_MASK;
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_INDEX, reg_data);
}

const struct amdgpu_imu_funcs gfx_v11_0_imu_funcs = {
	.init_microcode = imu_v11_0_init_microcode,
	.load_microcode = imu_v11_0_load_microcode,
	.setup_imu = imu_v11_0_setup,
	.start_imu = imu_v11_0_start,
	.program_rlc_ram = imu_v11_0_program_rlc_ram,
	.wait_for_reset_status = imu_v11_0_wait_for_reset_status,
};
