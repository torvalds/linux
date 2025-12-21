// SPDX-License-Identifier: MIT
/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 * Copyright 2025 Valve Corporation
 * Copyright 2025 Alexandre Demers
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * Authors: Christian König <christian.koenig@amd.com>
 *          Timur Kristóf <timur.kristof@gmail.com>
 *          Alexandre Demers <alexandre.f.demers@gmail.com>
 */

#include <linux/firmware.h>

#include "amdgpu.h"
#include "amdgpu_vce.h"
#include "amdgpu_gart.h"
#include "sid.h"
#include "vce_v1_0.h"
#include "vce/vce_1_0_d.h"
#include "vce/vce_1_0_sh_mask.h"
#include "oss/oss_1_0_d.h"
#include "oss/oss_1_0_sh_mask.h"

#define VCE_V1_0_FW_SIZE	(256 * 1024)
#define VCE_V1_0_STACK_SIZE	(64 * 1024)
#define VCE_V1_0_DATA_SIZE	(7808 * (AMDGPU_MAX_VCE_HANDLES + 1))
#define VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK	0x02

#define VCE_V1_0_GART_PAGE_START \
	(AMDGPU_GTT_MAX_TRANSFER_SIZE * AMDGPU_GTT_NUM_TRANSFER_WINDOWS)
#define VCE_V1_0_GART_ADDR_START \
	(VCE_V1_0_GART_PAGE_START * AMDGPU_GPU_PAGE_SIZE)

static void vce_v1_0_set_ring_funcs(struct amdgpu_device *adev);
static void vce_v1_0_set_irq_funcs(struct amdgpu_device *adev);

struct vce_v1_0_fw_signature {
	int32_t offset;
	uint32_t length;
	int32_t number;
	struct {
		uint32_t chip_id;
		uint32_t keyselect;
		uint32_t nonce[4];
		uint32_t sigval[4];
	} val[8];
};

/**
 * vce_v1_0_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vce_v1_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->me == 0)
		return RREG32(mmVCE_RB_RPTR);
	else
		return RREG32(mmVCE_RB_RPTR2);
}

/**
 * vce_v1_0_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vce_v1_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->me == 0)
		return RREG32(mmVCE_RB_WPTR);
	else
		return RREG32(mmVCE_RB_WPTR2);
}

/**
 * vce_v1_0_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vce_v1_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->me == 0)
		WREG32(mmVCE_RB_WPTR, lower_32_bits(ring->wptr));
	else
		WREG32(mmVCE_RB_WPTR2, lower_32_bits(ring->wptr));
}

static int vce_v1_0_lmi_clean(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			if (RREG32(mmVCE_LMI_STATUS) & 0x337f)
				return 0;

			mdelay(10);
		}
	}

	return -ETIMEDOUT;
}

static int vce_v1_0_firmware_loaded(struct amdgpu_device *adev)
{
	int i, j;

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			if (RREG32(mmVCE_STATUS) & VCE_STATUS_VCPU_REPORT_FW_LOADED_MASK)
				return 0;
			mdelay(10);
		}

		dev_err(adev->dev, "VCE not responding, trying to reset the ECPU\n");

		WREG32_P(mmVCE_SOFT_RESET,
			VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK,
			~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
		mdelay(10);
		WREG32_P(mmVCE_SOFT_RESET, 0,
			~VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK);
		mdelay(10);
	}

	return -ETIMEDOUT;
}

static void vce_v1_0_init_cg(struct amdgpu_device *adev)
{
	u32 tmp;

	tmp = RREG32(mmVCE_CLOCK_GATING_A);
	tmp |= VCE_CLOCK_GATING_A__CGC_DYN_CLOCK_MODE_MASK;
	WREG32(mmVCE_CLOCK_GATING_A, tmp);

	tmp = RREG32(mmVCE_CLOCK_GATING_B);
	tmp |= 0x1e;
	tmp &= ~0xe100e1;
	WREG32(mmVCE_CLOCK_GATING_B, tmp);

	tmp = RREG32(mmVCE_UENC_CLOCK_GATING);
	tmp &= ~0xff9ff000;
	WREG32(mmVCE_UENC_CLOCK_GATING, tmp);

	tmp = RREG32(mmVCE_UENC_REG_CLOCK_GATING);
	tmp &= ~0x3ff;
	WREG32(mmVCE_UENC_REG_CLOCK_GATING, tmp);
}

/**
 * vce_v1_0_load_fw_signature - load firmware signature into VCPU BO
 *
 * @adev: amdgpu_device pointer
 *
 * The VCE1 firmware validation mechanism needs a firmware signature.
 * This function finds the signature appropriate for the current
 * ASIC and writes that into the VCPU BO.
 */
static int vce_v1_0_load_fw_signature(struct amdgpu_device *adev)
{
	const struct common_firmware_header *hdr;
	struct vce_v1_0_fw_signature *sign;
	unsigned int ucode_offset;
	uint32_t chip_id;
	u32 *cpu_addr;
	int i;

	hdr = (const struct common_firmware_header *)adev->vce.fw->data;
	ucode_offset = le32_to_cpu(hdr->ucode_array_offset_bytes);
	cpu_addr = adev->vce.cpu_addr;

	sign = (void *)adev->vce.fw->data + ucode_offset;

	switch (adev->asic_type) {
	case CHIP_TAHITI:
		chip_id = 0x01000014;
		break;
	case CHIP_VERDE:
		chip_id = 0x01000015;
		break;
	case CHIP_PITCAIRN:
		chip_id = 0x01000016;
		break;
	default:
		dev_err(adev->dev, "asic_type %#010x was not found!", adev->asic_type);
		return -EINVAL;
	}

	for (i = 0; i < le32_to_cpu(sign->number); ++i) {
		if (le32_to_cpu(sign->val[i].chip_id) == chip_id)
			break;
	}

	if (i == le32_to_cpu(sign->number)) {
		dev_err(adev->dev, "chip_id 0x%x for %s was not found in VCE firmware",
			chip_id, amdgpu_asic_name[adev->asic_type]);
		return -EINVAL;
	}

	cpu_addr += (256 - 64) / 4;
	memcpy_toio(&cpu_addr[0], &sign->val[i].nonce[0], 16);
	cpu_addr[4] = cpu_to_le32(le32_to_cpu(sign->length) + 64);

	memset_io(&cpu_addr[5], 0, 44);
	memcpy_toio(&cpu_addr[16], &sign[1], hdr->ucode_size_bytes - sizeof(*sign));

	cpu_addr += (le32_to_cpu(sign->length) + 64) / 4;
	memcpy_toio(&cpu_addr[0], &sign->val[i].sigval[0], 16);

	adev->vce.keyselect = le32_to_cpu(sign->val[i].keyselect);

	return 0;
}

static int vce_v1_0_wait_for_fw_validation(struct amdgpu_device *adev)
{
	int i;

	dev_dbg(adev->dev, "VCE keyselect: %d", adev->vce.keyselect);
	WREG32(mmVCE_LMI_FW_START_KEYSEL, adev->vce.keyselect);

	for (i = 0; i < 10; ++i) {
		mdelay(10);
		if (RREG32(mmVCE_FW_REG_STATUS) & VCE_FW_REG_STATUS__DONE_MASK)
			break;
	}

	if (!(RREG32(mmVCE_FW_REG_STATUS) & VCE_FW_REG_STATUS__DONE_MASK)) {
		dev_err(adev->dev, "VCE FW validation timeout\n");
		return -ETIMEDOUT;
	}

	if (!(RREG32(mmVCE_FW_REG_STATUS) & VCE_FW_REG_STATUS__PASS_MASK)) {
		dev_err(adev->dev, "VCE FW validation failed\n");
		return -EINVAL;
	}

	for (i = 0; i < 10; ++i) {
		mdelay(10);
		if (!(RREG32(mmVCE_FW_REG_STATUS) & VCE_FW_REG_STATUS__BUSY_MASK))
			break;
	}

	if (RREG32(mmVCE_FW_REG_STATUS) & VCE_FW_REG_STATUS__BUSY_MASK) {
		dev_err(adev->dev, "VCE FW busy timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int vce_v1_0_mc_resume(struct amdgpu_device *adev)
{
	uint32_t offset;
	uint32_t size;

	/*
	 * When the keyselect is already set, don't perturb VCE FW.
	 * Validation seems to always fail the second time.
	 */
	if (RREG32(mmVCE_LMI_FW_START_KEYSEL)) {
		dev_dbg(adev->dev, "keyselect already set: 0x%x (on CPU: 0x%x)\n",
			RREG32(mmVCE_LMI_FW_START_KEYSEL), adev->vce.keyselect);

		WREG32_P(mmVCE_LMI_CTRL2, 0x0, ~0x100);
		return 0;
	}

	WREG32_P(mmVCE_CLOCK_GATING_A, 0, ~(1 << 16));
	WREG32_P(mmVCE_UENC_CLOCK_GATING, 0x1FF000, ~0xFF9FF000);
	WREG32_P(mmVCE_UENC_REG_CLOCK_GATING, 0x3F, ~0x3F);
	WREG32(mmVCE_CLOCK_GATING_B, 0);

	WREG32_P(mmVCE_LMI_FW_PERIODIC_CTRL, 0x4, ~0x4);

	WREG32(mmVCE_LMI_CTRL, 0x00398000);

	WREG32_P(mmVCE_LMI_CACHE_CTRL, 0x0, ~0x1);
	WREG32(mmVCE_LMI_SWAP_CNTL, 0);
	WREG32(mmVCE_LMI_SWAP_CNTL1, 0);
	WREG32(mmVCE_LMI_VM_CTRL, 0);

	WREG32(mmVCE_VCPU_SCRATCH7, AMDGPU_MAX_VCE_HANDLES);

	offset =  adev->vce.gpu_addr + AMDGPU_VCE_FIRMWARE_OFFSET;
	size = VCE_V1_0_FW_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET0, offset & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE0, size);

	offset += size;
	size = VCE_V1_0_STACK_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET1, offset & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE1, size);

	offset += size;
	size = VCE_V1_0_DATA_SIZE;
	WREG32(mmVCE_VCPU_CACHE_OFFSET2, offset & 0x7fffffff);
	WREG32(mmVCE_VCPU_CACHE_SIZE2, size);

	WREG32_P(mmVCE_LMI_CTRL2, 0x0, ~0x100);

	return vce_v1_0_wait_for_fw_validation(adev);
}

/**
 * vce_v1_0_is_idle() - Check idle status of VCE1 IP block
 *
 * @ip_block: amdgpu_ip_block pointer
 *
 * Check whether VCE is busy according to VCE_STATUS.
 * Also check whether the SRBM thinks VCE is busy, although
 * SRBM_STATUS.VCE_BUSY seems to be bogus because it
 * appears to mirror the VCE_STATUS.VCPU_REPORT_FW_LOADED bit.
 */
static bool vce_v1_0_is_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool busy =
		(RREG32(mmVCE_STATUS) & (VCE_STATUS__JOB_BUSY_MASK | VCE_STATUS__UENC_BUSY_MASK)) ||
		(RREG32(mmSRBM_STATUS2) & SRBM_STATUS2__VCE_BUSY_MASK);

	return !busy;
}

static int vce_v1_0_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	unsigned int i;

	for (i = 0; i < adev->usec_timeout; i++) {
		udelay(1);
		if (vce_v1_0_is_idle(ip_block))
			return 0;
	}
	return -ETIMEDOUT;
}

/**
 * vce_v1_0_start - start VCE block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the VCE block
 */
static int vce_v1_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	int r;

	WREG32_P(mmVCE_STATUS, 1, ~1);

	r = vce_v1_0_mc_resume(adev);
	if (r)
		return r;

	ring = &adev->vce.ring[0];
	WREG32(mmVCE_RB_RPTR, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_WPTR, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_BASE_LO, lower_32_bits(ring->gpu_addr));
	WREG32(mmVCE_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32(mmVCE_RB_SIZE, ring->ring_size / 4);

	ring = &adev->vce.ring[1];
	WREG32(mmVCE_RB_RPTR2, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_WPTR2, lower_32_bits(ring->wptr));
	WREG32(mmVCE_RB_BASE_LO2, lower_32_bits(ring->gpu_addr));
	WREG32(mmVCE_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
	WREG32(mmVCE_RB_SIZE2, ring->ring_size / 4);

	WREG32_P(mmVCE_VCPU_CNTL, VCE_VCPU_CNTL__CLK_EN_MASK,
		 ~VCE_VCPU_CNTL__CLK_EN_MASK);

	WREG32_P(mmVCE_SOFT_RESET,
		VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK |
		VCE_SOFT_RESET__FME_SOFT_RESET_MASK,
		~(VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK |
		  VCE_SOFT_RESET__FME_SOFT_RESET_MASK));

	mdelay(100);

	WREG32_P(mmVCE_SOFT_RESET, 0,
		~(VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK |
		  VCE_SOFT_RESET__FME_SOFT_RESET_MASK));

	r = vce_v1_0_firmware_loaded(adev);

	/* Clear VCE_STATUS, otherwise SRBM thinks VCE1 is busy. */
	WREG32(mmVCE_STATUS, 0);

	if (r) {
		dev_err(adev->dev, "VCE not responding, giving up\n");
		return r;
	}

	return 0;
}

static int vce_v1_0_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ip_block *ip_block;
	int status;
	int i;

	ip_block = amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_VCE);
	if (!ip_block)
		return -EINVAL;

	if (vce_v1_0_lmi_clean(adev))
		dev_warn(adev->dev, "VCE not idle\n");

	if (vce_v1_0_wait_for_idle(ip_block))
		dev_warn(adev->dev, "VCE busy: VCE_STATUS=0x%x, SRBM_STATUS2=0x%x\n",
			RREG32(mmVCE_STATUS), RREG32(mmSRBM_STATUS2));

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(mmVCE_LMI_CTRL2, 1 << 8, ~(1 << 8));

	for (i = 0; i < 100; ++i) {
		status = RREG32(mmVCE_LMI_STATUS);
		if (status & 0x240)
			break;
		mdelay(1);
	}

	WREG32_P(mmVCE_VCPU_CNTL, 0, ~VCE_VCPU_CNTL__CLK_EN_MASK);

	WREG32_P(mmVCE_SOFT_RESET,
		VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK |
		VCE_SOFT_RESET__FME_SOFT_RESET_MASK,
		~(VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK |
		  VCE_SOFT_RESET__FME_SOFT_RESET_MASK));

	WREG32(mmVCE_STATUS, 0);

	return 0;
}

static void vce_v1_0_enable_mgcg(struct amdgpu_device *adev, bool enable)
{
	u32 tmp;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_VCE_MGCG)) {
		tmp = RREG32(mmVCE_CLOCK_GATING_A);
		tmp |= VCE_CLOCK_GATING_A__CGC_DYN_CLOCK_MODE_MASK;
		WREG32(mmVCE_CLOCK_GATING_A, tmp);

		tmp = RREG32(mmVCE_UENC_CLOCK_GATING);
		tmp &= ~0x1ff000;
		tmp |= 0xff800000;
		WREG32(mmVCE_UENC_CLOCK_GATING, tmp);

		tmp = RREG32(mmVCE_UENC_REG_CLOCK_GATING);
		tmp &= ~0x3ff;
		WREG32(mmVCE_UENC_REG_CLOCK_GATING, tmp);
	} else {
		tmp = RREG32(mmVCE_CLOCK_GATING_A);
		tmp &= ~VCE_CLOCK_GATING_A__CGC_DYN_CLOCK_MODE_MASK;
		WREG32(mmVCE_CLOCK_GATING_A, tmp);

		tmp = RREG32(mmVCE_UENC_CLOCK_GATING);
		tmp |= 0x1ff000;
		tmp &= ~0xff800000;
		WREG32(mmVCE_UENC_CLOCK_GATING, tmp);

		tmp = RREG32(mmVCE_UENC_REG_CLOCK_GATING);
		tmp |= 0x3ff;
		WREG32(mmVCE_UENC_REG_CLOCK_GATING, tmp);
	}
}

static int vce_v1_0_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_vce_early_init(adev);
	if (r)
		return r;

	adev->vce.num_rings = 2;

	vce_v1_0_set_ring_funcs(adev);
	vce_v1_0_set_irq_funcs(adev);

	return 0;
}

/**
 * vce_v1_0_ensure_vcpu_bo_32bit_addr() - ensure the VCPU BO has a 32-bit address
 *
 * @adev: amdgpu_device pointer
 *
 * Due to various hardware limitations, the VCE1 requires
 * the VCPU BO to be in the low 32 bit address range.
 * Ensure that the VCPU BO has a 32-bit GPU address,
 * or return an error code when that isn't possible.
 *
 * To accomodate that, we put GART to the LOW address range
 * and reserve some GART pages where we map the VCPU BO,
 * so that it gets a 32-bit address.
 */
static int vce_v1_0_ensure_vcpu_bo_32bit_addr(struct amdgpu_device *adev)
{
	u64 gpu_addr = amdgpu_bo_gpu_offset(adev->vce.vcpu_bo);
	u64 bo_size = amdgpu_bo_size(adev->vce.vcpu_bo);
	u64 max_vcpu_bo_addr = 0xffffffff - bo_size;
	u64 num_pages = ALIGN(bo_size, AMDGPU_GPU_PAGE_SIZE) / AMDGPU_GPU_PAGE_SIZE;
	u64 pa = amdgpu_gmc_vram_pa(adev, adev->vce.vcpu_bo);
	u64 flags = AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE | AMDGPU_PTE_VALID;

	/*
	 * Check if the VCPU BO already has a 32-bit address.
	 * Eg. if MC is configured to put VRAM in the low address range.
	 */
	if (gpu_addr <= max_vcpu_bo_addr)
		return 0;

	/* Check if we can map the VCPU BO in GART to a 32-bit address. */
	if (adev->gmc.gart_start + VCE_V1_0_GART_ADDR_START > max_vcpu_bo_addr)
		return -EINVAL;

	amdgpu_gart_map_vram_range(adev, pa, VCE_V1_0_GART_PAGE_START,
				   num_pages, flags, adev->gart.ptr);
	adev->vce.gpu_addr = adev->gmc.gart_start + VCE_V1_0_GART_ADDR_START;
	if (adev->vce.gpu_addr > max_vcpu_bo_addr)
		return -EINVAL;

	return 0;
}

static int vce_v1_0_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int r, i;

	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 167, &adev->vce.irq);
	if (r)
		return r;

	r = amdgpu_vce_sw_init(adev, VCE_V1_0_FW_SIZE +
		VCE_V1_0_STACK_SIZE + VCE_V1_0_DATA_SIZE);
	if (r)
		return r;

	r = amdgpu_vce_resume(adev);
	if (r)
		return r;
	r = vce_v1_0_load_fw_signature(adev);
	if (r)
		return r;
	r = vce_v1_0_ensure_vcpu_bo_32bit_addr(adev);
	if (r)
		return r;

	for (i = 0; i < adev->vce.num_rings; i++) {
		enum amdgpu_ring_priority_level hw_prio = amdgpu_vce_get_ring_prio(i);

		ring = &adev->vce.ring[i];
		sprintf(ring->name, "vce%d", i);
		r = amdgpu_ring_init(adev, ring, 512, &adev->vce.irq, 0,
				     hw_prio, NULL);
		if (r)
			return r;
	}

	return r;
}

static int vce_v1_0_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_vce_suspend(adev);
	if (r)
		return r;

	return amdgpu_vce_sw_fini(adev);
}

/**
 * vce_v1_0_hw_init - start and test VCE block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vce_v1_0_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_vce(adev, true);
	else
		amdgpu_asic_set_vce_clocks(adev, 10000, 10000);

	for (i = 0; i < adev->vce.num_rings; i++) {
		r = amdgpu_ring_test_helper(&adev->vce.ring[i]);
		if (r)
			return r;
	}

	dev_info(adev->dev, "VCE initialized successfully.\n");

	return 0;
}

static int vce_v1_0_hw_fini(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = vce_v1_0_stop(ip_block->adev);
	if (r)
		return r;

	cancel_delayed_work_sync(&ip_block->adev->vce.idle_work);
	return 0;
}

static int vce_v1_0_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	/*
	 * Proper cleanups before halting the HW engine:
	 *   - cancel the delayed idle work
	 *   - enable powergating
	 *   - enable clockgating
	 *   - disable dpm
	 *
	 * TODO: to align with the VCN implementation, move the
	 * jobs for clockgating/powergating/dpm setting to
	 * ->set_powergating_state().
	 */
	cancel_delayed_work_sync(&adev->vce.idle_work);

	if (adev->pm.dpm_enabled) {
		amdgpu_dpm_enable_vce(adev, false);
	} else {
		amdgpu_asic_set_vce_clocks(adev, 0, 0);
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_PG_STATE_GATE);
		amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_VCE,
						       AMD_CG_STATE_GATE);
	}

	r = vce_v1_0_hw_fini(ip_block);
	if (r) {
		dev_err(adev->dev, "vce_v1_0_hw_fini() failed with error %i", r);
		return r;
	}

	return amdgpu_vce_suspend(adev);
}

static int vce_v1_0_resume(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_vce_resume(adev);
	if (r)
		return r;
	r = vce_v1_0_load_fw_signature(adev);
	if (r)
		return r;
	r = vce_v1_0_ensure_vcpu_bo_32bit_addr(adev);
	if (r)
		return r;

	return vce_v1_0_hw_init(ip_block);
}

static int vce_v1_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int type,
					enum amdgpu_interrupt_state state)
{
	uint32_t val = 0;

	if (state == AMDGPU_IRQ_STATE_ENABLE)
		val |= VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK;

	WREG32_P(mmVCE_SYS_INT_EN, val,
		 ~VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK);
	return 0;
}

static int vce_v1_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	dev_dbg(adev->dev, "IH: VCE\n");
	switch (entry->src_data[0]) {
	case 0:
	case 1:
		amdgpu_fence_process(&adev->vce.ring[entry->src_data[0]]);
		break;
	default:
		dev_err(adev->dev, "Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static int vce_v1_0_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;

	vce_v1_0_init_cg(adev);
	vce_v1_0_enable_mgcg(adev, state == AMD_CG_STATE_GATE);

	return 0;
}

static int vce_v1_0_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;

	/*
	 * This doesn't actually powergate the VCE block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	if (state == AMD_PG_STATE_GATE)
		return vce_v1_0_stop(adev);
	else
		return vce_v1_0_start(adev);
}

static const struct amd_ip_funcs vce_v1_0_ip_funcs = {
	.name = "vce_v1_0",
	.early_init = vce_v1_0_early_init,
	.sw_init = vce_v1_0_sw_init,
	.sw_fini = vce_v1_0_sw_fini,
	.hw_init = vce_v1_0_hw_init,
	.hw_fini = vce_v1_0_hw_fini,
	.suspend = vce_v1_0_suspend,
	.resume = vce_v1_0_resume,
	.is_idle = vce_v1_0_is_idle,
	.wait_for_idle = vce_v1_0_wait_for_idle,
	.set_clockgating_state = vce_v1_0_set_clockgating_state,
	.set_powergating_state = vce_v1_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs vce_v1_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_VCE,
	.align_mask = 0xf,
	.nop = VCE_CMD_NO_OP,
	.support_64bit_ptrs = false,
	.no_user_fence = true,
	.get_rptr = vce_v1_0_ring_get_rptr,
	.get_wptr = vce_v1_0_ring_get_wptr,
	.set_wptr = vce_v1_0_ring_set_wptr,
	.parse_cs = amdgpu_vce_ring_parse_cs,
	.emit_frame_size = 6, /* amdgpu_vce_ring_emit_fence  x1 no user fence */
	.emit_ib_size = 4, /* amdgpu_vce_ring_emit_ib */
	.emit_ib = amdgpu_vce_ring_emit_ib,
	.emit_fence = amdgpu_vce_ring_emit_fence,
	.test_ring = amdgpu_vce_ring_test_ring,
	.test_ib = amdgpu_vce_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vce_ring_begin_use,
	.end_use = amdgpu_vce_ring_end_use,
};

static void vce_v1_0_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vce.num_rings; i++) {
		adev->vce.ring[i].funcs = &vce_v1_0_ring_funcs;
		adev->vce.ring[i].me = i;
	}
};

static const struct amdgpu_irq_src_funcs vce_v1_0_irq_funcs = {
	.set = vce_v1_0_set_interrupt_state,
	.process = vce_v1_0_process_interrupt,
};

static void vce_v1_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->vce.irq.num_types = 1;
	adev->vce.irq.funcs = &vce_v1_0_irq_funcs;
};

const struct amdgpu_ip_block_version vce_v1_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_VCE,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &vce_v1_0_ip_funcs,
};
