/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * Authors: Christian König <christian.koenig@amd.com>
 */

#include <linux/firmware.h>
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "rv770d.h"

/**
 * uvd_v2_2_fence_emit - emit an fence & trap command
 *
 * @rdev: radeon_device pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
void uvd_v2_2_fence_emit(struct radeon_device *rdev,
			 struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	uint64_t addr = rdev->fence_drv[fence->ring].gpu_addr;

	radeon_ring_write(ring, PACKET0(UVD_CONTEXT_ID, 0));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA0, 0));
	radeon_ring_write(ring, lower_32_bits(addr));
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA1, 0));
	radeon_ring_write(ring, upper_32_bits(addr) & 0xff);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_CMD, 0));
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA0, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA1, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_CMD, 0));
	radeon_ring_write(ring, 2);
}

/**
 * uvd_v2_2_resume - memory controller programming
 *
 * @rdev: radeon_device pointer
 *
 * Let the UVD memory controller know it's offsets
 */
int uvd_v2_2_resume(struct radeon_device *rdev)
{
	uint64_t addr;
	uint32_t chip_id, size;
	int r;

	r = radeon_uvd_resume(rdev);
	if (r)
		return r;

	/* programm the VCPU memory controller bits 0-27 */
	addr = rdev->uvd.gpu_addr >> 3;
	size = RADEON_GPU_PAGE_ALIGN(rdev->uvd_fw->size + 4) >> 3;
	WREG32(UVD_VCPU_CACHE_OFFSET0, addr);
	WREG32(UVD_VCPU_CACHE_SIZE0, size);

	addr += size;
	size = RADEON_UVD_STACK_SIZE >> 3;
	WREG32(UVD_VCPU_CACHE_OFFSET1, addr);
	WREG32(UVD_VCPU_CACHE_SIZE1, size);

	addr += size;
	size = RADEON_UVD_HEAP_SIZE >> 3;
	WREG32(UVD_VCPU_CACHE_OFFSET2, addr);
	WREG32(UVD_VCPU_CACHE_SIZE2, size);

	/* bits 28-31 */
	addr = (rdev->uvd.gpu_addr >> 28) & 0xF;
	WREG32(UVD_LMI_ADDR_EXT, (addr << 12) | (addr << 0));

	/* bits 32-39 */
	addr = (rdev->uvd.gpu_addr >> 32) & 0xFF;
	WREG32(UVD_LMI_EXT40_ADDR, addr | (0x9 << 16) | (0x1 << 31));

	/* tell firmware which hardware it is running on */
	switch (rdev->family) {
	default:
		return -EINVAL;
	case CHIP_RV710:
		chip_id = 0x01000005;
		break;
	case CHIP_RV730:
		chip_id = 0x01000006;
		break;
	case CHIP_RV740:
		chip_id = 0x01000007;
		break;
	case CHIP_CYPRESS:
	case CHIP_HEMLOCK:
		chip_id = 0x01000008;
		break;
	case CHIP_JUNIPER:
		chip_id = 0x01000009;
		break;
	case CHIP_REDWOOD:
		chip_id = 0x0100000a;
		break;
	case CHIP_CEDAR:
		chip_id = 0x0100000b;
		break;
	case CHIP_SUMO:
	case CHIP_SUMO2:
		chip_id = 0x0100000c;
		break;
	case CHIP_PALM:
		chip_id = 0x0100000e;
		break;
	case CHIP_CAYMAN:
		chip_id = 0x0100000f;
		break;
	case CHIP_BARTS:
		chip_id = 0x01000010;
		break;
	case CHIP_TURKS:
		chip_id = 0x01000011;
		break;
	case CHIP_CAICOS:
		chip_id = 0x01000012;
		break;
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
	}
	WREG32(UVD_VCPU_CHIP_ID, chip_id);

	return 0;
}
