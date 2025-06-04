/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include "imu_v12_0.h"

#include "gc/gc_12_0_0_offset.h"
#include "gc/gc_12_0_0_sh_mask.h"
#include "mmhub/mmhub_4_1_0_offset.h"

MODULE_FIRMWARE("amdgpu/gc_12_0_0_imu.bin");
MODULE_FIRMWARE("amdgpu/gc_12_0_1_imu.bin");
MODULE_FIRMWARE("amdgpu/gc_12_0_1_imu_kicker.bin");

#define TRANSFER_RAM_MASK	0x001c0000

static int imu_v12_0_init_microcode(struct amdgpu_device *adev)
{
	char ucode_prefix[30];
	int err;
	const struct imu_firmware_header_v1_0 *imu_hdr;
	struct amdgpu_firmware_info *info = NULL;

	DRM_DEBUG("\n");

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix, sizeof(ucode_prefix));
	if (amdgpu_is_kicker_fw(adev))
		err = amdgpu_ucode_request(adev, &adev->gfx.imu_fw, AMDGPU_UCODE_REQUIRED,
					   "amdgpu/%s_imu_kicker.bin", ucode_prefix);
	else
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

static int imu_v12_0_load_microcode(struct amdgpu_device *adev)
{
	const struct imu_firmware_header_v1_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.imu_fw)
		return -EINVAL;

	hdr = (const struct imu_firmware_header_v1_0 *)adev->gfx.imu_fw->data;

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

static int imu_v12_0_wait_for_reset_status(struct amdgpu_device *adev)
{
	u32 imu_reg_val = 0;
	int i;

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

static void imu_v12_0_setup(struct amdgpu_device *adev)
{
	u32 imu_reg_val;

	WREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_ACCESS_CTRL0, 0xffffff);
	WREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_ACCESS_CTRL1, 0xffff);

	if (adev->gfx.imu.mode == DEBUG_MODE) {
		imu_reg_val = RREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_16);
		imu_reg_val |= 0x1;
		WREG32_SOC15(GC, 0, regGFX_IMU_C2PMSG_16, imu_reg_val);

		imu_reg_val = RREG32_SOC15(GC, 0, regGFX_IMU_SCRATCH_10);
		imu_reg_val |= 0x20010007;
		WREG32_SOC15(GC, 0, regGFX_IMU_SCRATCH_10, imu_reg_val);

	}
}

static int imu_v12_0_start(struct amdgpu_device *adev)
{
	u32 imu_reg_val;

	imu_reg_val = RREG32_SOC15(GC, 0, regGFX_IMU_CORE_CTRL);
	imu_reg_val &= 0xfffffffe;
	WREG32_SOC15(GC, 0, regGFX_IMU_CORE_CTRL, imu_reg_val);

	if (adev->flags & AMD_IS_APU)
		amdgpu_dpm_set_gfx_power_up_by_imu(adev);

	return imu_v12_0_wait_for_reset_status(adev);
}

static const struct imu_rlc_ram_golden imu_rlc_ram_golden_12_0_1[] = {
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCH_PIPE_STEER, 0x1e4, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL1X_PIPE_STEER, 0x1e4, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL1_PIPE_STEER, 0x1e4, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_0, 0x13571357, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_1, 0x64206420, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_2, 0x2460246, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2_PIPE_STEER_3, 0x75317531, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGL2C_CTRL3, 0xc0d41183, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regSDMA0_CHICKEN_BITS, 0x507d1c0, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regSDMA1_CHICKEN_BITS, 0x507d1c0, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCP_RB_WPTR_POLL_CNTL, 0x600100, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_CREDITS, 0x3f7fff, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_CREDITS, 0x3f7ebf, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_TAG_RESERVE0, 0x2e00000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_TAG_RESERVE1, 0x1a078, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_TAG_RESERVE2, 0x0, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_TAG_RESERVE0, 0x0, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_TAG_RESERVE1, 0x12030, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_TAG_RESERVE2, 0x0, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_VCC_RESERVE0, 0x19041000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_VCC_RESERVE1, 0x80000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_VCC_RESERVE0, 0x1e080000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_VCC_RESERVE1, 0x80000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_PRIORITY, 0x880, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_PRIORITY, 0x8880, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_ARB_FINAL, 0x17, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_ARB_FINAL, 0x77, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_SDP_ENABLE, 0x00000001, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_SDP_ENABLE, 0x00000001, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_PROTECTION_FAULT_CNTL2, 0x20000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_APT_CNTL, 0x0c, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_CACHEABLE_DRAM_ADDRESS_END, 0xfffff, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_CPWD_MISC, 0x0091, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGC_EA_SE_MISC, 0x0091, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGRBM_GFX_INDEX, 0xe0000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCR_GENERAL_CNTL, 0x00008500, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regPA_CL_ENHANCE, 0x00880007, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regTD_CNTL, 0x00000001, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGRBM_GFX_INDEX, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regRMI_GENERAL_CNTL, 0x01e00000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGRBM_GFX_INDEX, 0x00000001, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regRMI_GENERAL_CNTL, 0x01e00000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGRBM_GFX_INDEX, 0x00000100, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regRMI_GENERAL_CNTL, 0x01e00000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGRBM_GFX_INDEX, 0x00000101, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regRMI_GENERAL_CNTL, 0x01e00000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGRBM_GFX_INDEX, 0xe0000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGB_ADDR_CONFIG, 0x08200545, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGRBMH_CP_PERFMON_CNTL, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCB_PERFCOUNTER0_SELECT1, 0x000fffff, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCP_DEBUG_2, 0x00020000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regCP_CPC_DEBUG, 0x00500010, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_MX_L1_TLB_CNTL, 0x00000500, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR, 0x00000001, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_START, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_END, 0x0000000f, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_FB_LOCATION_BASE, 0x00006000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_FB_LOCATION_TOP, 0x0000600f, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT0_CNTL, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_CONTEXT1_CNTL, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_TOP_OF_DRAM_SLOT1, 0xff800000, 0xe0000000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_LOWER_TOP_OF_DRAM2, 0x00000001, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_NB_UPPER_TOP_OF_DRAM2, 0x0000ffff, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_BASE, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_BOT, 0x00000002, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_AGP_TOP, 0x00000000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_PROTECTION_FAULT_CNTL, 0x00001ffc, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_MX_L1_TLB_CNTL, 0x00000551, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL, 0x00080603, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL2, 0x00000003, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL3, 0x00100003, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCVM_L2_CNTL5, 0x00003fe0, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR, 0x0003d000, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR, 0x0003d7ff, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB, 0, 0x1c0000),
	IMU_RLC_RAM_GOLDEN_VALUE(GC, 0, regGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB, 0, 0x1c0000)
};

static void program_imu_rlc_ram_old(struct amdgpu_device *adev,
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
}

static u32 imu_v12_0_grbm_gfx_index_remap(struct amdgpu_device *adev,
					  u32 data, bool high)
{
	u32 val, inst_index;

	inst_index = REG_GET_FIELD(data, GRBM_GFX_INDEX, INSTANCE_INDEX);

	if (high)
		val = inst_index >> 5;
	else
		val = REG_GET_FIELD(data, GRBM_GFX_INDEX, SE_BROADCAST_WRITES) << 18 |
		      REG_GET_FIELD(data, GRBM_GFX_INDEX, SA_BROADCAST_WRITES) << 19 |
		      REG_GET_FIELD(data, GRBM_GFX_INDEX, INSTANCE_BROADCAST_WRITES) << 20 |
		      REG_GET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX) << 21 |
		      REG_GET_FIELD(data, GRBM_GFX_INDEX, SA_INDEX) << 25 |
		      (inst_index & 0x1f);

	return val;
}

static u32 imu_v12_init_gfxhub_settings(struct amdgpu_device *adev,
					u32 reg, u32 data)
{
	if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_FB_LOCATION_BASE))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_FB_LOCATION_BASE);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_FB_LOCATION_TOP))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_FB_LOCATION_TOP);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_FB_OFFSET))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_FB_OFFSET);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_AGP_BASE))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_AGP_BASE);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_AGP_BOT))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_AGP_BOT);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_AGP_TOP))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_AGP_TOP);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_MX_L1_TLB_CNTL))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_MX_L1_TLB_CNTL);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_SYSTEM_APERTURE_LOW_ADDR))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_SYSTEM_APERTURE_LOW_ADDR);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_SYSTEM_APERTURE_HIGH_ADDR))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_SYSTEM_APERTURE_HIGH_ADDR);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_START))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_LOCAL_FB_ADDRESS_START);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_LOCAL_FB_ADDRESS_END))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_LOCAL_FB_ADDRESS_END);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_LOCAL_SYSMEM_ADDRESS_START))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_LOCAL_SYSMEM_ADDRESS_START);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_LOCAL_SYSMEM_ADDRESS_END))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_LOCAL_SYSMEM_ADDRESS_END);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_LSB);
	else if (reg == SOC15_REG_OFFSET(GC, 0, regGCMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB))
		return RREG32_SOC15(MMHUB, 0, regMMMC_VM_SYSTEM_APERTURE_DEFAULT_ADDR_MSB);
	else
		return data;
}

static void program_imu_rlc_ram(struct amdgpu_device *adev,
				const u32 *regs,
				const u32 array_size)
{
	u32 reg, data, val_h = 0, val_l = TRANSFER_RAM_MASK;
	int i;

	if (array_size % 3)
		return;

	for (i = 0; i < array_size; i += 3) {
		reg = regs[i + 0];
		data = regs[i + 2];
		data = imu_v12_init_gfxhub_settings(adev, reg, data);
		if (reg == SOC15_REG_OFFSET(GC, 0, regGRBM_GFX_INDEX)) {
			val_l = imu_v12_0_grbm_gfx_index_remap(adev, data, false);
			val_h = imu_v12_0_grbm_gfx_index_remap(adev, data, true);
		} else {
			WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_HIGH, val_h);
			WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_LOW, reg | val_l);
			WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_DATA, data);
		}
	}
}

static void imu_v12_0_program_rlc_ram(struct amdgpu_device *adev)
{
	u32 reg_data, size = 0;
	const u32 *data = NULL;
	int r = -EINVAL;

	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_INDEX, 0x2);

	switch (amdgpu_ip_version(adev, GC_HWIP, 0)) {
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		if (!r)
			program_imu_rlc_ram(adev, data, (const u32)size);
		else
			program_imu_rlc_ram_old(adev, imu_rlc_ram_golden_12_0_1,
				(const u32)ARRAY_SIZE(imu_rlc_ram_golden_12_0_1));
		break;
	default:
		BUG();
		break;
	}

	//Indicate the latest entry
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_HIGH, 0);
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_ADDR_LOW, 0);
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_DATA, 0);

	reg_data = RREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_INDEX);
	reg_data |= GFX_IMU_RLC_RAM_INDEX__RAM_VALID_MASK;
	WREG32_SOC15(GC, 0, regGFX_IMU_RLC_RAM_INDEX, reg_data);
}

const struct amdgpu_imu_funcs gfx_v12_0_imu_funcs = {
	.init_microcode = imu_v12_0_init_microcode,
	.load_microcode = imu_v12_0_load_microcode,
	.setup_imu = imu_v12_0_setup,
	.start_imu = imu_v12_0_start,
	.program_rlc_ram = imu_v12_0_program_rlc_ram,
	.wait_for_reset_status = imu_v12_0_wait_for_reset_status,
};
