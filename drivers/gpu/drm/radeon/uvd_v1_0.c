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
#include "r600d.h"

/**
 * uvd_v1_0_get_rptr - get read pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Returns the current hardware read pointer
 */
uint32_t uvd_v1_0_get_rptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	return RREG32(UVD_RBC_RB_RPTR);
}

/**
 * uvd_v1_0_get_wptr - get write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Returns the current hardware write pointer
 */
uint32_t uvd_v1_0_get_wptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	return RREG32(UVD_RBC_RB_WPTR);
}

/**
 * uvd_v1_0_set_wptr - set write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Commits the write pointer to the hardware
 */
void uvd_v1_0_set_wptr(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	WREG32(UVD_RBC_RB_WPTR, ring->wptr);
}

/**
 * uvd_v1_0_fence_emit - emit an fence & trap command
 *
 * @rdev: radeon_device pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
void uvd_v1_0_fence_emit(struct radeon_device *rdev,
			 struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	uint64_t addr = rdev->fence_drv[fence->ring].gpu_addr;

	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA0, 0));
	radeon_ring_write(ring, addr & 0xffffffff);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA1, 0));
	radeon_ring_write(ring, fence->seq);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_CMD, 0));
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA0, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_DATA1, 0));
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, PACKET0(UVD_GPCOM_VCPU_CMD, 0));
	radeon_ring_write(ring, 2);
	return;
}

/**
 * uvd_v1_0_resume - memory controller programming
 *
 * @rdev: radeon_device pointer
 *
 * Let the UVD memory controller know it's offsets
 */
int uvd_v1_0_resume(struct radeon_device *rdev)
{
	uint64_t addr;
	uint32_t size;
	int r;

	r = radeon_uvd_resume(rdev);
	if (r)
		return r;

	/* programm the VCPU memory controller bits 0-27 */
	addr = (rdev->uvd.gpu_addr >> 3) + 16;
	size = RADEON_GPU_PAGE_ALIGN(rdev->uvd_fw->size) >> 3;
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

	WREG32(UVD_FW_START, *((uint32_t*)rdev->uvd.cpu_addr));

	return 0;
}

/**
 * uvd_v1_0_init - start and test UVD block
 *
 * @rdev: radeon_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
int uvd_v1_0_init(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
	uint32_t tmp;
	int r;

	/* raise clocks while booting up the VCPU */
	if (rdev->family < CHIP_RV740)
		radeon_set_uvd_clocks(rdev, 10000, 10000);
	else
		radeon_set_uvd_clocks(rdev, 53300, 40000);

	r = uvd_v1_0_start(rdev);
	if (r)
		goto done;

	ring->ready = true;
	r = radeon_ring_test(rdev, R600_RING_TYPE_UVD_INDEX, ring);
	if (r) {
		ring->ready = false;
		goto done;
	}

	r = radeon_ring_lock(rdev, ring, 10);
	if (r) {
		DRM_ERROR("radeon: ring failed to lock UVD ring (%d).\n", r);
		goto done;
	}

	tmp = PACKET0(UVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL, 0);
	radeon_ring_write(ring, tmp);
	radeon_ring_write(ring, 0xFFFFF);

	tmp = PACKET0(UVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL, 0);
	radeon_ring_write(ring, tmp);
	radeon_ring_write(ring, 0xFFFFF);

	tmp = PACKET0(UVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL, 0);
	radeon_ring_write(ring, tmp);
	radeon_ring_write(ring, 0xFFFFF);

	/* Clear timeout status bits */
	radeon_ring_write(ring, PACKET0(UVD_SEMA_TIMEOUT_STATUS, 0));
	radeon_ring_write(ring, 0x8);

	radeon_ring_write(ring, PACKET0(UVD_SEMA_CNTL, 0));
	radeon_ring_write(ring, 3);

	radeon_ring_unlock_commit(rdev, ring, false);

done:
	/* lower clocks again */
	radeon_set_uvd_clocks(rdev, 0, 0);

	if (!r) {
		switch (rdev->family) {
		case CHIP_RV610:
		case CHIP_RV630:
		case CHIP_RV620:
			/* 64byte granularity workaround */
			WREG32(MC_CONFIG, 0);
			WREG32(MC_CONFIG, 1 << 4);
			WREG32(RS_DQ_RD_RET_CONF, 0x3f);
			WREG32(MC_CONFIG, 0x1f);

			/* fall through */
		case CHIP_RV670:
		case CHIP_RV635:

			/* write clean workaround */
			WREG32_P(UVD_VCPU_CNTL, 0x10, ~0x10);
			break;

		default:
			/* TODO: Do we need more? */
			break;
		}

		DRM_INFO("UVD initialized successfully.\n");
	}

	return r;
}

/**
 * uvd_v1_0_fini - stop the hardware block
 *
 * @rdev: radeon_device pointer
 *
 * Stop the UVD block, mark ring as not ready any more
 */
void uvd_v1_0_fini(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];

	uvd_v1_0_stop(rdev);
	ring->ready = false;
}

/**
 * uvd_v1_0_start - start UVD block
 *
 * @rdev: radeon_device pointer
 *
 * Setup and start the UVD block
 */
int uvd_v1_0_start(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[R600_RING_TYPE_UVD_INDEX];
	uint32_t rb_bufsz;
	int i, j, r;

	/* disable byte swapping */
	u32 lmi_swap_cntl = 0;
	u32 mp_swap_cntl = 0;

	/* disable clock gating */
	WREG32(UVD_CGC_GATE, 0);

	/* disable interupt */
	WREG32_P(UVD_MASTINT_EN, 0, ~(1 << 1));

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(UVD_LMI_CTRL2, 1 << 8, ~(1 << 8));
	WREG32_P(UVD_RB_ARB_CTRL, 1 << 3, ~(1 << 3));
	mdelay(1);

	/* put LMI, VCPU, RBC etc... into reset */
	WREG32(UVD_SOFT_RESET, LMI_SOFT_RESET | VCPU_SOFT_RESET |
	       LBSI_SOFT_RESET | RBC_SOFT_RESET | CSM_SOFT_RESET |
	       CXW_SOFT_RESET | TAP_SOFT_RESET | LMI_UMC_SOFT_RESET);
	mdelay(5);

	/* take UVD block out of reset */
	WREG32_P(SRBM_SOFT_RESET, 0, ~SOFT_RESET_UVD);
	mdelay(5);

	/* initialize UVD memory controller */
	WREG32(UVD_LMI_CTRL, 0x40 | (1 << 8) | (1 << 13) |
			     (1 << 21) | (1 << 9) | (1 << 20));

#ifdef __BIG_ENDIAN
	/* swap (8 in 32) RB and IB */
	lmi_swap_cntl = 0xa;
	mp_swap_cntl = 0;
#endif
	WREG32(UVD_LMI_SWAP_CNTL, lmi_swap_cntl);
	WREG32(UVD_MP_SWAP_CNTL, mp_swap_cntl);

	WREG32(UVD_MPC_SET_MUXA0, 0x40c2040);
	WREG32(UVD_MPC_SET_MUXA1, 0x0);
	WREG32(UVD_MPC_SET_MUXB0, 0x40c2040);
	WREG32(UVD_MPC_SET_MUXB1, 0x0);
	WREG32(UVD_MPC_SET_ALU, 0);
	WREG32(UVD_MPC_SET_MUX, 0x88);

	/* take all subblocks out of reset, except VCPU */
	WREG32(UVD_SOFT_RESET, VCPU_SOFT_RESET);
	mdelay(5);

	/* enable VCPU clock */
	WREG32(UVD_VCPU_CNTL,  1 << 9);

	/* enable UMC */
	WREG32_P(UVD_LMI_CTRL2, 0, ~(1 << 8));

	WREG32_P(UVD_RB_ARB_CTRL, 0, ~(1 << 3));

	/* boot up the VCPU */
	WREG32(UVD_SOFT_RESET, 0);
	mdelay(10);

	for (i = 0; i < 10; ++i) {
		uint32_t status;
		for (j = 0; j < 100; ++j) {
			status = RREG32(UVD_STATUS);
			if (status & 2)
				break;
			mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_ERROR("UVD not responding, trying to reset the VCPU!!!\n");
		WREG32_P(UVD_SOFT_RESET, VCPU_SOFT_RESET, ~VCPU_SOFT_RESET);
		mdelay(10);
		WREG32_P(UVD_SOFT_RESET, 0, ~VCPU_SOFT_RESET);
		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_ERROR("UVD not responding, giving up!!!\n");
		return r;
	}

	/* enable interupt */
	WREG32_P(UVD_MASTINT_EN, 3<<1, ~(3 << 1));

	/* force RBC into idle state */
	WREG32(UVD_RBC_RB_CNTL, 0x11010101);

	/* Set the write pointer delay */
	WREG32(UVD_RBC_RB_WPTR_CNTL, 0);

	/* programm the 4GB memory segment for rptr and ring buffer */
	WREG32(UVD_LMI_EXT40_ADDR, upper_32_bits(ring->gpu_addr) |
				   (0x7 << 16) | (0x1 << 31));

	/* Initialize the ring buffer's read and write pointers */
	WREG32(UVD_RBC_RB_RPTR, 0x0);

	ring->wptr = RREG32(UVD_RBC_RB_RPTR);
	WREG32(UVD_RBC_RB_WPTR, ring->wptr);

	/* set the ring address */
	WREG32(UVD_RBC_RB_BASE, ring->gpu_addr);

	/* Set ring buffer size */
	rb_bufsz = order_base_2(ring->ring_size);
	rb_bufsz = (0x1 << 8) | rb_bufsz;
	WREG32_P(UVD_RBC_RB_CNTL, rb_bufsz, ~0x11f1f);

	return 0;
}

/**
 * uvd_v1_0_stop - stop UVD block
 *
 * @rdev: radeon_device pointer
 *
 * stop the UVD block
 */
void uvd_v1_0_stop(struct radeon_device *rdev)
{
	/* force RBC into idle state */
	WREG32(UVD_RBC_RB_CNTL, 0x11010101);

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(UVD_LMI_CTRL2, 1 << 8, ~(1 << 8));
	WREG32_P(UVD_RB_ARB_CTRL, 1 << 3, ~(1 << 3));
	mdelay(1);

	/* put VCPU into reset */
	WREG32(UVD_SOFT_RESET, VCPU_SOFT_RESET);
	mdelay(5);

	/* disable VCPU clock */
	WREG32(UVD_VCPU_CNTL, 0x0);

	/* Unstall UMC and register bus */
	WREG32_P(UVD_LMI_CTRL2, 0, ~(1 << 8));
	WREG32_P(UVD_RB_ARB_CTRL, 0, ~(1 << 3));
}

/**
 * uvd_v1_0_ring_test - register write test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Test if we can successfully write to the context register
 */
int uvd_v1_0_ring_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	uint32_t tmp = 0;
	unsigned i;
	int r;

	WREG32(UVD_CONTEXT_ID, 0xCAFEDEAD);
	r = radeon_ring_lock(rdev, ring, 3);
	if (r) {
		DRM_ERROR("radeon: cp failed to lock ring %d (%d).\n",
			  ring->idx, r);
		return r;
	}
	radeon_ring_write(ring, PACKET0(UVD_CONTEXT_ID, 0));
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring, false);
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = RREG32(UVD_CONTEXT_ID);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n",
			 ring->idx, i);
	} else {
		DRM_ERROR("radeon: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	return r;
}

/**
 * uvd_v1_0_semaphore_emit - emit semaphore command
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 * @semaphore: semaphore to emit commands for
 * @emit_wait: true if we should emit a wait command
 *
 * Emit a semaphore command (either wait or signal) to the UVD ring.
 */
bool uvd_v1_0_semaphore_emit(struct radeon_device *rdev,
			     struct radeon_ring *ring,
			     struct radeon_semaphore *semaphore,
			     bool emit_wait)
{
	/* disable semaphores for UVD V1 hardware */
	return false;
}

/**
 * uvd_v1_0_ib_execute - execute indirect buffer
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to execute
 *
 * Write ring commands to execute the indirect buffer
 */
void uvd_v1_0_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];

	radeon_ring_write(ring, PACKET0(UVD_RBC_IB_BASE, 0));
	radeon_ring_write(ring, ib->gpu_addr);
	radeon_ring_write(ring, PACKET0(UVD_RBC_IB_SIZE, 0));
	radeon_ring_write(ring, ib->length_dw);
}

/**
 * uvd_v1_0_ib_test - test ib execution
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring pointer
 *
 * Test if we can successfully execute an IB
 */
int uvd_v1_0_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_fence *fence = NULL;
	int r;

	if (rdev->family < CHIP_RV740)
		r = radeon_set_uvd_clocks(rdev, 10000, 10000);
	else
		r = radeon_set_uvd_clocks(rdev, 53300, 40000);
	if (r) {
		DRM_ERROR("radeon: failed to raise UVD clocks (%d).\n", r);
		return r;
	}

	r = radeon_uvd_get_create_msg(rdev, ring->idx, 1, NULL);
	if (r) {
		DRM_ERROR("radeon: failed to get create msg (%d).\n", r);
		goto error;
	}

	r = radeon_uvd_get_destroy_msg(rdev, ring->idx, 1, &fence);
	if (r) {
		DRM_ERROR("radeon: failed to get destroy ib (%d).\n", r);
		goto error;
	}

	r = radeon_fence_wait(fence, false);
	if (r) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
		goto error;
	}
	DRM_INFO("ib test on ring %d succeeded\n",  ring->idx);
error:
	radeon_fence_unref(&fence);
	radeon_set_uvd_clocks(rdev, 0, 0);
	return r;
}
