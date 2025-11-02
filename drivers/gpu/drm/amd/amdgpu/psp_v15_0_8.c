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
#include <drm/drm_drv.h>
#include <linux/vmalloc.h>
#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "amdgpu_ucode.h"
#include "soc15_common.h"
#include "psp_v15_0_8.h"

#include "mp/mp_15_0_8_offset.h"
#include "mp/mp_15_0_8_sh_mask.h"

MODULE_FIRMWARE("amdgpu/psp_15_0_8_toc.bin");

static int psp_v15_0_8_init_microcode(struct psp_context *psp)
{
	struct amdgpu_device *adev = psp->adev;
	char ucode_prefix[30];
	int err = 0;

	amdgpu_ucode_ip_version_decode(adev, MP0_HWIP, ucode_prefix, sizeof(ucode_prefix));

	err = psp_init_toc_microcode(psp, ucode_prefix);
	if (err)
		return err;

	return 0;
}

static int psp_v15_0_8_ring_stop(struct psp_context *psp,
			       enum psp_ring_type ring_type)
{
	int ret = 0;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		/* Write the ring destroy command*/
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_101,
			     GFX_CTRL_CMD_ID_DESTROY_GPCOM_RING);
		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);
		/* Wait for response flag (bit 31) */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_SMN_C2PMSG_101),
				   0x80000000, 0x80000000, false);
	} else {
		/* Write the ring destroy command*/
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_64,
			     GFX_CTRL_CMD_ID_DESTROY_RINGS);
		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);
		/* Wait for response flag (bit 31) */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_SMN_C2PMSG_64),
				   0x80000000, 0x80000000, false);
	}

	return ret;
}

static int psp_v15_0_8_ring_create(struct psp_context *psp,
				 enum psp_ring_type ring_type)
{
	int ret = 0;
	unsigned int psp_ring_reg = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		ret = psp_v15_0_8_ring_stop(psp, ring_type);
		if (ret) {
			DRM_ERROR("psp_v14_0_ring_stop_sriov failed!\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_102 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_102, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_103 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_103, psp_ring_reg);

		/* Write the ring initialization command to C2PMSG_101 */
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_101,
			     GFX_CTRL_CMD_ID_INIT_GPCOM_RING);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_101 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_SMN_C2PMSG_101),
				   0x80000000, 0x8000FFFF, false);

	} else {
		/* Wait for sOS ready for ring creation */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_SMN_C2PMSG_64),
				   0x80000000, 0x80000000, false);
		if (ret) {
			DRM_ERROR("Failed to wait for trust OS ready for ring creation\n");
			return ret;
		}

		/* Write low address of the ring to C2PMSG_69 */
		psp_ring_reg = lower_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_69, psp_ring_reg);
		/* Write high address of the ring to C2PMSG_70 */
		psp_ring_reg = upper_32_bits(ring->ring_mem_mc_addr);
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_70, psp_ring_reg);
		/* Write size of ring to C2PMSG_71 */
		psp_ring_reg = ring->ring_size;
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_71, psp_ring_reg);
		/* Write the ring initialization command to C2PMSG_64 */
		psp_ring_reg = ring_type;
		psp_ring_reg = psp_ring_reg << 16;
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_64, psp_ring_reg);

		/* there might be handshake issue with hardware which needs delay */
		mdelay(20);

		/* Wait for response flag (bit 31) in C2PMSG_64 */
		ret = psp_wait_for(psp, SOC15_REG_OFFSET(MP0, 0, regMPASP_SMN_C2PMSG_64),
				   0x80000000, 0x8000FFFF, false);
	}

	return ret;
}

static int psp_v15_0_8_ring_destroy(struct psp_context *psp,
				  enum psp_ring_type ring_type)
{
	int ret = 0;
	struct psp_ring *ring = &psp->km_ring;
	struct amdgpu_device *adev = psp->adev;

	ret = psp_v15_0_8_ring_stop(psp, ring_type);
	if (ret)
		DRM_ERROR("Fail to stop psp ring\n");

	amdgpu_bo_free_kernel(&adev->firmware.rbuf,
			      &ring->ring_mem_mc_addr,
			      (void **)&ring->ring_mem);

	return ret;
}

static uint32_t psp_v15_0_8_ring_get_wptr(struct psp_context *psp)
{
	uint32_t data;
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev))
		data = RREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_102);
	else
		data = RREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_67);

	return data;
}

static void psp_v15_0_8_ring_set_wptr(struct psp_context *psp, uint32_t value)
{
	struct amdgpu_device *adev = psp->adev;

	if (amdgpu_sriov_vf(adev)) {
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_102, value);
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_101,
			     GFX_CTRL_CMD_ID_CONSUME_CMD);
	} else
		WREG32_SOC15(MP0, 0, regMPASP_SMN_C2PMSG_67, value);
}

static int psp_v15_0_8_get_fw_type(struct amdgpu_firmware_info *ucode,
				   enum psp_gfx_fw_type *type)
{
	switch (ucode->ucode_id) {
	case AMDGPU_UCODE_ID_CAP:
		*type = GFX_FW_TYPE_CAP;
		break;
	case AMDGPU_UCODE_ID_SDMA0:
		*type = GFX_FW_TYPE_SDMA0;
		break;
	case AMDGPU_UCODE_ID_SDMA1:
		*type = GFX_FW_TYPE_SDMA1;
		break;
	case AMDGPU_UCODE_ID_SDMA2:
		*type = GFX_FW_TYPE_SDMA2;
		break;
	case AMDGPU_UCODE_ID_SDMA3:
		*type = GFX_FW_TYPE_SDMA3;
		break;
	case AMDGPU_UCODE_ID_SDMA4:
		*type = GFX_FW_TYPE_SDMA4;
		break;
	case AMDGPU_UCODE_ID_SDMA5:
		*type = GFX_FW_TYPE_SDMA5;
		break;
	case AMDGPU_UCODE_ID_SDMA6:
		*type = GFX_FW_TYPE_SDMA6;
		break;
	case AMDGPU_UCODE_ID_SDMA7:
		*type = GFX_FW_TYPE_SDMA7;
		break;
	case AMDGPU_UCODE_ID_CP_MES:
		*type = GFX_FW_TYPE_RS64_MES;
		break;
	case AMDGPU_UCODE_ID_CP_MES_DATA:
		*type = GFX_FW_TYPE_RS64_MES_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_MES1:
		*type = GFX_FW_TYPE_RS64_KIQ;
		break;
	case AMDGPU_UCODE_ID_CP_MES1_DATA:
		*type = GFX_FW_TYPE_RS64_KIQ_STACK;
		break;
	case AMDGPU_UCODE_ID_RLC_P:
		*type = GFX_FW_TYPE_RLC_P;
		break;
	case AMDGPU_UCODE_ID_RLC_V:
		*type = GFX_FW_TYPE_RLC_V;
		break;
	case AMDGPU_UCODE_ID_RLC_G:
		*type = GFX_FW_TYPE_RLC_G;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_CNTL;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_GPM_MEM;
		break;
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
		*type = GFX_FW_TYPE_RLC_RESTORE_LIST_SRM_MEM;
		break;
	case AMDGPU_UCODE_ID_RLC_IRAM:
		*type = GFX_FW_TYPE_RLC_IRAM;
		break;
	case AMDGPU_UCODE_ID_RLC_DRAM:
		*type = GFX_FW_TYPE_RLC_DRAM_BOOT;
		break;
	case AMDGPU_UCODE_ID_RLC_IRAM_1:
		*type = GFX_FW_TYPE_RLX6_UCODE_CORE1;
		break;
	case AMDGPU_UCODE_ID_RLC_DRAM_1:
		*type = GFX_FW_TYPE_RLX6_DRAM_BOOT_CORE1;
		break;
	case AMDGPU_UCODE_ID_SMC:
		*type = GFX_FW_TYPE_SMU;
		break;
	case AMDGPU_UCODE_ID_PPTABLE:
		*type = GFX_FW_TYPE_PPTABLE;
		break;
	case AMDGPU_UCODE_ID_VCN:
		*type = GFX_FW_TYPE_VCN;
		break;
	case AMDGPU_UCODE_ID_VCN1:
		*type = GFX_FW_TYPE_VCN1;
		break;
	case AMDGPU_UCODE_ID_VCN0_RAM:
		*type = GFX_FW_TYPE_VCN0_RAM;
		break;
	case AMDGPU_UCODE_ID_VCN1_RAM:
		*type = GFX_FW_TYPE_VCN1_RAM;
		break;
	case AMDGPU_UCODE_ID_SDMA_UCODE_TH0:
	case AMDGPU_UCODE_ID_SDMA_RS64:
		*type = GFX_FW_TYPE_SDMA0;
		break;
	case AMDGPU_UCODE_ID_SDMA_UCODE_TH1:
		*type = GFX_FW_TYPE_SDMA_UCODE_TH1;
		break;
	case AMDGPU_UCODE_ID_IMU_I:
		*type = GFX_FW_TYPE_IMU_I;
		break;
	case AMDGPU_UCODE_ID_IMU_D:
		*type = GFX_FW_TYPE_IMU_D;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC:
		*type = GFX_FW_TYPE_RS64_MEC;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P0_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P1_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P2_STACK;
		break;
	case AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK:
		*type = GFX_FW_TYPE_RS64_MEC_P3_STACK;
		break;
	case AMDGPU_UCODE_ID_UMSCH_MM_UCODE:
		*type = GFX_FW_TYPE_UMSCH_UCODE;
		break;
	case AMDGPU_UCODE_ID_UMSCH_MM_DATA:
		*type = GFX_FW_TYPE_UMSCH_DATA;
		break;
	case AMDGPU_UCODE_ID_UMSCH_MM_CMD_BUFFER:
		*type = GFX_FW_TYPE_UMSCH_CMD_BUFFER;
		break;
	case AMDGPU_UCODE_ID_P2S_TABLE:
		*type = GFX_FW_TYPE_P2S_TABLE;
		break;
	case AMDGPU_UCODE_ID_MAXIMUM:
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct psp_funcs psp_v15_0_8_funcs = {
	.init_microcode = psp_v15_0_8_init_microcode,
	.ring_create = psp_v15_0_8_ring_create,
	.ring_stop = psp_v15_0_8_ring_stop,
	.ring_destroy = psp_v15_0_8_ring_destroy,
	.ring_get_wptr = psp_v15_0_8_ring_get_wptr,
	.ring_set_wptr = psp_v15_0_8_ring_set_wptr,
	.get_fw_type = psp_v15_0_8_get_fw_type,
};

void psp_v15_0_8_set_psp_funcs(struct psp_context *psp)
{
	psp->funcs = &psp_v15_0_8_funcs;
}
