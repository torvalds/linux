/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 */

#include <linux/firmware.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "sid.h"

#define VCE_V1_0_FW_SIZE	(256 * 1024)
#define VCE_V1_0_STACK_SIZE	(64 * 1024)
#define VCE_V1_0_DATA_SIZE	(7808 * (RADEON_MAX_VCE_HANDLES + 1))

struct vce_v1_0_fw_signature
{
	int32_t off;
	uint32_t len;
	int32_t num;
	struct {
		uint32_t chip_id;
		uint32_t keyselect;
		uint32_t nonce[4];
		uint32_t sigval[4];
	} val[8];
};

/**
 * vce_v1_0_get_rptr - get read pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Returns the current hardware read pointer
 */
uint32_t vce_v1_0_get_rptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	if (ring->idx == TN_RING_TYPE_VCE1_INDEX)
		return RREG32(VCE_RB_RPTR);
	else
		return RREG32(VCE_RB_RPTR2);
}

/**
 * vce_v1_0_get_wptr - get write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Returns the current hardware write pointer
 */
uint32_t vce_v1_0_get_wptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	if (ring->idx == TN_RING_TYPE_VCE1_INDEX)
		return RREG32(VCE_RB_WPTR);
	else
		return RREG32(VCE_RB_WPTR2);
}

/**
 * vce_v1_0_set_wptr - set write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Commits the write pointer to the hardware
 */
void vce_v1_0_set_wptr(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	if (ring->idx == TN_RING_TYPE_VCE1_INDEX)
		WREG32(VCE_RB_WPTR, ring->wptr);
	else
		WREG32(VCE_RB_WPTR2, ring->wptr);
}

void vce_v1_0_enable_mgcg(struct radeon_device *rdev, bool enable)
{
	u32 tmp;

	if (enable && (rdev->cg_flags & RADEON_CG_SUPPORT_VCE_MGCG)) {
		tmp = RREG32(VCE_CLOCK_GATING_A);
		tmp |= CGC_DYN_CLOCK_MODE;
		WREG32(VCE_CLOCK_GATING_A, tmp);

		tmp = RREG32(VCE_UENC_CLOCK_GATING);
		tmp &= ~0x1ff000;
		tmp |= 0xff800000;
		WREG32(VCE_UENC_CLOCK_GATING, tmp);

		tmp = RREG32(VCE_UENC_REG_CLOCK_GATING);
		tmp &= ~0x3ff;
		WREG32(VCE_UENC_REG_CLOCK_GATING, tmp);
	} else {
		tmp = RREG32(VCE_CLOCK_GATING_A);
		tmp &= ~CGC_DYN_CLOCK_MODE;
		WREG32(VCE_CLOCK_GATING_A, tmp);

		tmp = RREG32(VCE_UENC_CLOCK_GATING);
		tmp |= 0x1ff000;
		tmp &= ~0xff800000;
		WREG32(VCE_UENC_CLOCK_GATING, tmp);

		tmp = RREG32(VCE_UENC_REG_CLOCK_GATING);
		tmp |= 0x3ff;
		WREG32(VCE_UENC_REG_CLOCK_GATING, tmp);
	}
}

static void vce_v1_0_init_cg(struct radeon_device *rdev)
{
	u32 tmp;

	tmp = RREG32(VCE_CLOCK_GATING_A);
	tmp |= CGC_DYN_CLOCK_MODE;
	WREG32(VCE_CLOCK_GATING_A, tmp);

	tmp = RREG32(VCE_CLOCK_GATING_B);
	tmp |= 0x1e;
	tmp &= ~0xe100e1;
	WREG32(VCE_CLOCK_GATING_B, tmp);

	tmp = RREG32(VCE_UENC_CLOCK_GATING);
	tmp &= ~0xff9ff000;
	WREG32(VCE_UENC_CLOCK_GATING, tmp);

	tmp = RREG32(VCE_UENC_REG_CLOCK_GATING);
	tmp &= ~0x3ff;
	WREG32(VCE_UENC_REG_CLOCK_GATING, tmp);
}

int vce_v1_0_load_fw(struct radeon_device *rdev, uint32_t *data)
{
	struct vce_v1_0_fw_signature *sign = (void*)rdev->vce_fw->data;
	uint32_t chip_id;
	int i;

	switch (rdev->family) {
	case CHIP_TAHITI:
		chip_id = 0x01000014;
		break;
	case CHIP_VERDE:
		chip_id = 0x01000015;
		break;
	case CHIP_PITCAIRN:
	case CHIP_OLAND:
		chip_id = 0x01000016;
		break;
	case CHIP_ARUBA:
		chip_id = 0x01000017;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < sign->num; ++i) {
		if (sign->val[i].chip_id == chip_id)
			break;
	}

	if (i == sign->num)
		return -EINVAL;

	data += (256 - 64) / 4;
	data[0] = sign->val[i].nonce[0];
	data[1] = sign->val[i].nonce[1];
	data[2] = sign->val[i].nonce[2];
	data[3] = sign->val[i].nonce[3];
	data[4] = sign->len + 64;

	memset(&data[5], 0, 44);
	memcpy(&data[16], &sign[1], rdev->vce_fw->size - sizeof(*sign));

	data += data[4] / 4;
	data[0] = sign->val[i].sigval[0];
	data[1] = sign->val[i].sigval[1];
	data[2] = sign->val[i].sigval[2];
	data[3] = sign->val[i].sigval[3];

	rdev->vce.keyselect = sign->val[i].keyselect;

	return 0;
}

unsigned vce_v1_0_bo_size(struct radeon_device *rdev)
{
	WARN_ON(VCE_V1_0_FW_SIZE < rdev->vce_fw->size);
	return VCE_V1_0_FW_SIZE + VCE_V1_0_STACK_SIZE + VCE_V1_0_DATA_SIZE;
}

int vce_v1_0_resume(struct radeon_device *rdev)
{
	uint64_t addr = rdev->vce.gpu_addr;
	uint32_t size;
	int i;

	WREG32_P(VCE_CLOCK_GATING_A, 0, ~(1 << 16));
	WREG32_P(VCE_UENC_CLOCK_GATING, 0x1FF000, ~0xFF9FF000);
	WREG32_P(VCE_UENC_REG_CLOCK_GATING, 0x3F, ~0x3F);
	WREG32(VCE_CLOCK_GATING_B, 0);

	WREG32_P(VCE_LMI_FW_PERIODIC_CTRL, 0x4, ~0x4);

	WREG32(VCE_LMI_CTRL, 0x00398000);
	WREG32_P(VCE_LMI_CACHE_CTRL, 0x0, ~0x1);
	WREG32(VCE_LMI_SWAP_CNTL, 0);
	WREG32(VCE_LMI_SWAP_CNTL1, 0);
	WREG32(VCE_LMI_VM_CTRL, 0);

	WREG32(VCE_VCPU_SCRATCH7, RADEON_MAX_VCE_HANDLES);

	addr += 256;
	size = VCE_V1_0_FW_SIZE;
	WREG32(VCE_VCPU_CACHE_OFFSET0, addr & 0x7fffffff);
	WREG32(VCE_VCPU_CACHE_SIZE0, size);

	addr += size;
	size = VCE_V1_0_STACK_SIZE;
	WREG32(VCE_VCPU_CACHE_OFFSET1, addr & 0x7fffffff);
	WREG32(VCE_VCPU_CACHE_SIZE1, size);

	addr += size;
	size = VCE_V1_0_DATA_SIZE;
	WREG32(VCE_VCPU_CACHE_OFFSET2, addr & 0x7fffffff);
	WREG32(VCE_VCPU_CACHE_SIZE2, size);

	WREG32_P(VCE_LMI_CTRL2, 0x0, ~0x100);

	WREG32(VCE_LMI_FW_START_KEYSEL, rdev->vce.keyselect);

	for (i = 0; i < 10; ++i) {
		mdelay(10);
		if (RREG32(VCE_FW_REG_STATUS) & VCE_FW_REG_STATUS_DONE)
			break;
	}

	if (i == 10)
		return -ETIMEDOUT;

	if (!(RREG32(VCE_FW_REG_STATUS) & VCE_FW_REG_STATUS_PASS))
		return -EINVAL;

	for (i = 0; i < 10; ++i) {
		mdelay(10);
		if (!(RREG32(VCE_FW_REG_STATUS) & VCE_FW_REG_STATUS_BUSY))
			break;
	}

	if (i == 10)
		return -ETIMEDOUT;

	vce_v1_0_init_cg(rdev);

	return 0;
}

/**
 * vce_v1_0_start - start VCE block
 *
 * @rdev: radeon_device pointer
 *
 * Setup and start the VCE block
 */
int vce_v1_0_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int i, j, r;

	/* set BUSY flag */
	WREG32_P(VCE_STATUS, 1, ~1);

	ring = &rdev->ring[TN_RING_TYPE_VCE1_INDEX];
	WREG32(VCE_RB_RPTR, ring->wptr);
	WREG32(VCE_RB_WPTR, ring->wptr);
	WREG32(VCE_RB_BASE_LO, ring->gpu_addr);
	WREG32(VCE_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32(VCE_RB_SIZE, ring->ring_size / 4);

	ring = &rdev->ring[TN_RING_TYPE_VCE2_INDEX];
	WREG32(VCE_RB_RPTR2, ring->wptr);
	WREG32(VCE_RB_WPTR2, ring->wptr);
	WREG32(VCE_RB_BASE_LO2, ring->gpu_addr);
	WREG32(VCE_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
	WREG32(VCE_RB_SIZE2, ring->ring_size / 4);

	WREG32_P(VCE_VCPU_CNTL, VCE_CLK_EN, ~VCE_CLK_EN);

	WREG32_P(VCE_SOFT_RESET,
		 VCE_ECPU_SOFT_RESET |
		 VCE_FME_SOFT_RESET, ~(
		 VCE_ECPU_SOFT_RESET |
		 VCE_FME_SOFT_RESET));

	mdelay(100);

	WREG32_P(VCE_SOFT_RESET, 0, ~(
		 VCE_ECPU_SOFT_RESET |
		 VCE_FME_SOFT_RESET));

	for (i = 0; i < 10; ++i) {
		uint32_t status;
		for (j = 0; j < 100; ++j) {
			status = RREG32(VCE_STATUS);
			if (status & 2)
				break;
			mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_ERROR("VCE not responding, trying to reset the ECPU!!!\n");
		WREG32_P(VCE_SOFT_RESET, VCE_ECPU_SOFT_RESET, ~VCE_ECPU_SOFT_RESET);
		mdelay(10);
		WREG32_P(VCE_SOFT_RESET, 0, ~VCE_ECPU_SOFT_RESET);
		mdelay(10);
		r = -1;
	}

	/* clear BUSY flag */
	WREG32_P(VCE_STATUS, 0, ~1);

	if (r) {
		DRM_ERROR("VCE not responding, giving up!!!\n");
		return r;
	}

	return 0;
}

int vce_v1_0_init(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	int r;

	r = vce_v1_0_start(rdev);
	if (r)
		return r;

	ring = &rdev->ring[TN_RING_TYPE_VCE1_INDEX];
	ring->ready = true;
	r = radeon_ring_test(rdev, TN_RING_TYPE_VCE1_INDEX, ring);
	if (r) {
		ring->ready = false;
		return r;
	}

	ring = &rdev->ring[TN_RING_TYPE_VCE2_INDEX];
	ring->ready = true;
	r = radeon_ring_test(rdev, TN_RING_TYPE_VCE2_INDEX, ring);
	if (r) {
		ring->ready = false;
		return r;
	}

	DRM_INFO("VCE initialized successfully.\n");

	return 0;
}
