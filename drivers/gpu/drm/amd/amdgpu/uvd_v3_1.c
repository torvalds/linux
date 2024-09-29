/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 * Authors: Sonny Jiang <sonny.jiang@amd.com>
 */

#include <linux/firmware.h>

#include "amdgpu.h"
#include "amdgpu_uvd.h"
#include "sid.h"

#include "uvd/uvd_3_1_d.h"
#include "uvd/uvd_3_1_sh_mask.h"

#include "oss/oss_1_0_d.h"
#include "oss/oss_1_0_sh_mask.h"

/**
 * uvd_v3_1_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t uvd_v3_1_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32(mmUVD_RBC_RB_RPTR);
}

/**
 * uvd_v3_1_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t uvd_v3_1_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32(mmUVD_RBC_RB_WPTR);
}

/**
 * uvd_v3_1_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void uvd_v3_1_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32(mmUVD_RBC_RB_WPTR, lower_32_bits(ring->wptr));
}

/**
 * uvd_v3_1_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @job: iob associated with the indirect buffer
 * @ib: indirect buffer to execute
 * @flags: flags associated with the indirect buffer
 *
 * Write ring commands to execute the indirect buffer
 */
static void uvd_v3_1_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_job *job,
				  struct amdgpu_ib *ib,
				  uint32_t flags)
{
	amdgpu_ring_write(ring, PACKET0(mmUVD_RBC_IB_BASE, 0));
	amdgpu_ring_write(ring, ib->gpu_addr);
	amdgpu_ring_write(ring, PACKET0(mmUVD_RBC_IB_SIZE, 0));
	amdgpu_ring_write(ring, ib->length_dw);
}

/**
 * uvd_v3_1_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @addr: address
 * @seq: sequence number
 * @flags: fence related flags
 *
 * Write a fence and a trap command to the ring.
 */
static void uvd_v3_1_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				 unsigned flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, PACKET0(mmUVD_CONTEXT_ID, 0));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, addr & 0xffffffff);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xff);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 2);
}

/**
 * uvd_v3_1_ring_test_ring - register write test
 *
 * @ring: amdgpu_ring pointer
 *
 * Test if we can successfully write to the context register
 */
static int uvd_v3_1_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	WREG32(mmUVD_CONTEXT_ID, 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);
	if (r)
		return r;

	amdgpu_ring_write(ring, PACKET0(mmUVD_CONTEXT_ID, 0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(mmUVD_CONTEXT_ID);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i >= adev->usec_timeout)
		r = -ETIMEDOUT;

	return r;
}

static void uvd_v3_1_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	int i;

	WARN_ON(ring->wptr % 2 || count % 2);

	for (i = 0; i < count / 2; i++) {
		amdgpu_ring_write(ring, PACKET0(mmUVD_NO_OP, 0));
		amdgpu_ring_write(ring, 0);
	}
}

static const struct amdgpu_ring_funcs uvd_v3_1_ring_funcs = {
	.type = AMDGPU_RING_TYPE_UVD,
	.align_mask = 0xf,
	.support_64bit_ptrs = false,
	.no_user_fence = true,
	.get_rptr = uvd_v3_1_ring_get_rptr,
	.get_wptr = uvd_v3_1_ring_get_wptr,
	.set_wptr = uvd_v3_1_ring_set_wptr,
	.parse_cs = amdgpu_uvd_ring_parse_cs,
	.emit_frame_size =
		14, /* uvd_v3_1_ring_emit_fence  x1 no user fence */
	.emit_ib_size = 4, /* uvd_v3_1_ring_emit_ib */
	.emit_ib = uvd_v3_1_ring_emit_ib,
	.emit_fence = uvd_v3_1_ring_emit_fence,
	.test_ring = uvd_v3_1_ring_test_ring,
	.test_ib = amdgpu_uvd_ring_test_ib,
	.insert_nop = uvd_v3_1_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_uvd_ring_begin_use,
	.end_use = amdgpu_uvd_ring_end_use,
};

static void uvd_v3_1_set_ring_funcs(struct amdgpu_device *adev)
{
	adev->uvd.inst->ring.funcs = &uvd_v3_1_ring_funcs;
}

static void uvd_v3_1_set_dcm(struct amdgpu_device *adev,
							 bool sw_mode)
{
	u32 tmp, tmp2;

	WREG32_FIELD(UVD_CGC_GATE, REGS, 0);

	tmp = RREG32(mmUVD_CGC_CTRL);
	tmp &= ~(UVD_CGC_CTRL__CLK_OFF_DELAY_MASK | UVD_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK);
	tmp |= UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK |
		(1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT) |
		(4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT);

	if (sw_mode) {
		tmp &= ~0x7ffff800;
		tmp2 = UVD_CGC_CTRL2__DYN_OCLK_RAMP_EN_MASK |
			UVD_CGC_CTRL2__DYN_RCLK_RAMP_EN_MASK |
			(7 << UVD_CGC_CTRL2__GATER_DIV_ID__SHIFT);
	} else {
		tmp |= 0x7ffff800;
		tmp2 = 0;
	}

	WREG32(mmUVD_CGC_CTRL, tmp);
	WREG32_UVD_CTX(ixUVD_CGC_CTRL2, tmp2);
}

/**
 * uvd_v3_1_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 *
 * Let the UVD memory controller know it's offsets
 */
static void uvd_v3_1_mc_resume(struct amdgpu_device *adev)
{
	uint64_t addr;
	uint32_t size;

	/* programm the VCPU memory controller bits 0-27 */
	addr = (adev->uvd.inst->gpu_addr + AMDGPU_UVD_FIRMWARE_OFFSET) >> 3;
	size = AMDGPU_UVD_FIRMWARE_SIZE(adev) >> 3;
	WREG32(mmUVD_VCPU_CACHE_OFFSET0, addr);
	WREG32(mmUVD_VCPU_CACHE_SIZE0, size);

	addr += size;
	size = AMDGPU_UVD_HEAP_SIZE >> 3;
	WREG32(mmUVD_VCPU_CACHE_OFFSET1, addr);
	WREG32(mmUVD_VCPU_CACHE_SIZE1, size);

	addr += size;
	size = (AMDGPU_UVD_STACK_SIZE +
		(AMDGPU_UVD_SESSION_SIZE * adev->uvd.max_handles)) >> 3;
	WREG32(mmUVD_VCPU_CACHE_OFFSET2, addr);
	WREG32(mmUVD_VCPU_CACHE_SIZE2, size);

	/* bits 28-31 */
	addr = (adev->uvd.inst->gpu_addr >> 28) & 0xF;
	WREG32(mmUVD_LMI_ADDR_EXT, (addr << 12) | (addr << 0));

	/* bits 32-39 */
	addr = (adev->uvd.inst->gpu_addr >> 32) & 0xFF;
	WREG32(mmUVD_LMI_EXT40_ADDR, addr | (0x9 << 16) | (0x1 << 31));

	WREG32(mmUVD_UDEC_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmUVD_UDEC_DB_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmUVD_UDEC_DBW_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
}

/**
 * uvd_v3_1_fw_validate - FW validation operation
 *
 * @adev: amdgpu_device pointer
 *
 * Initialate and check UVD validation.
 */
static int uvd_v3_1_fw_validate(struct amdgpu_device *adev)
{
	int i;
	uint32_t keysel = adev->uvd.keyselect;

	WREG32(mmUVD_FW_START, keysel);

	for (i = 0; i < 10; ++i) {
		mdelay(10);
		if (RREG32(mmUVD_FW_STATUS) & UVD_FW_STATUS__DONE_MASK)
			break;
	}

	if (i == 10)
		return -ETIMEDOUT;

	if (!(RREG32(mmUVD_FW_STATUS) & UVD_FW_STATUS__PASS_MASK))
		return -EINVAL;

	for (i = 0; i < 10; ++i) {
		mdelay(10);
		if (!(RREG32(mmUVD_FW_STATUS) & UVD_FW_STATUS__BUSY_MASK))
			break;
	}

	if (i == 10)
		return -ETIMEDOUT;

	return 0;
}

/**
 * uvd_v3_1_start - start UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the UVD block
 */
static int uvd_v3_1_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->uvd.inst->ring;
	uint32_t rb_bufsz;
	int i, j, r;
	u32 tmp;
	/* disable byte swapping */
	u32 lmi_swap_cntl = 0;
	u32 mp_swap_cntl = 0;

	/* set uvd busy */
	WREG32_P(mmUVD_STATUS, 1<<2, ~(1<<2));

	uvd_v3_1_set_dcm(adev, true);
	WREG32(mmUVD_CGC_GATE, 0);

	/* take UVD block out of reset */
	WREG32_P(mmSRBM_SOFT_RESET, 0, ~SRBM_SOFT_RESET__SOFT_RESET_UVD_MASK);
	mdelay(5);

	/* enable VCPU clock */
	WREG32(mmUVD_VCPU_CNTL,  1 << 9);

	/* disable interrupt */
	WREG32_P(mmUVD_MASTINT_EN, 0, ~(1 << 1));

#ifdef __BIG_ENDIAN
	/* swap (8 in 32) RB and IB */
	lmi_swap_cntl = 0xa;
	mp_swap_cntl = 0;
#endif
	WREG32(mmUVD_LMI_SWAP_CNTL, lmi_swap_cntl);
	WREG32(mmUVD_MP_SWAP_CNTL, mp_swap_cntl);

	/* initialize UVD memory controller */
	WREG32(mmUVD_LMI_CTRL, 0x40 | (1 << 8) | (1 << 13) |
		(1 << 21) | (1 << 9) | (1 << 20));

	tmp = RREG32(mmUVD_MPC_CNTL);
	WREG32(mmUVD_MPC_CNTL, tmp | 0x10);

	WREG32(mmUVD_MPC_SET_MUXA0, 0x40c2040);
	WREG32(mmUVD_MPC_SET_MUXA1, 0x0);
	WREG32(mmUVD_MPC_SET_MUXB0, 0x40c2040);
	WREG32(mmUVD_MPC_SET_MUXB1, 0x0);
	WREG32(mmUVD_MPC_SET_ALU, 0);
	WREG32(mmUVD_MPC_SET_MUX, 0x88);

	tmp = RREG32_UVD_CTX(ixUVD_LMI_CACHE_CTRL);
	WREG32_UVD_CTX(ixUVD_LMI_CACHE_CTRL, tmp & (~0x10));

	/* enable UMC */
	WREG32_P(mmUVD_LMI_CTRL2, 0, ~(1 << 8));

	WREG32_P(mmUVD_SOFT_RESET, 0, ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK);

	WREG32_P(mmUVD_SOFT_RESET, 0, ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK);

	WREG32_P(mmUVD_SOFT_RESET, 0, ~UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);

	mdelay(10);

	for (i = 0; i < 10; ++i) {
		uint32_t status;
		for (j = 0; j < 100; ++j) {
			status = RREG32(mmUVD_STATUS);
			if (status & 2)
				break;
			mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_ERROR("UVD not responding, trying to reset the VCPU!!!\n");
		WREG32_P(mmUVD_SOFT_RESET, UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK,
				 ~UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
		mdelay(10);
		WREG32_P(mmUVD_SOFT_RESET, 0, ~UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_ERROR("UVD not responding, giving up!!!\n");
		return r;
	}

	/* enable interrupt */
	WREG32_P(mmUVD_MASTINT_EN, 3<<1, ~(3 << 1));

	WREG32_P(mmUVD_STATUS, 0, ~(1<<2));

	/* force RBC into idle state */
	WREG32(mmUVD_RBC_RB_CNTL, 0x11010101);

	/* Set the write pointer delay */
	WREG32(mmUVD_RBC_RB_WPTR_CNTL, 0);

	/* programm the 4GB memory segment for rptr and ring buffer */
	WREG32(mmUVD_LMI_EXT40_ADDR, upper_32_bits(ring->gpu_addr) |
		   (0x7 << 16) | (0x1 << 31));

	/* Initialize the ring buffer's read and write pointers */
	WREG32(mmUVD_RBC_RB_RPTR, 0x0);

	ring->wptr = RREG32(mmUVD_RBC_RB_RPTR);
	WREG32(mmUVD_RBC_RB_WPTR, lower_32_bits(ring->wptr));

	/* set the ring address */
	WREG32(mmUVD_RBC_RB_BASE, ring->gpu_addr);

	/* Set ring buffer size */
	rb_bufsz = order_base_2(ring->ring_size);
	rb_bufsz = (0x1 << 8) | rb_bufsz;
	WREG32_P(mmUVD_RBC_RB_CNTL, rb_bufsz, ~0x11f1f);

	return 0;
}

/**
 * uvd_v3_1_stop - stop UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the UVD block
 */
static void uvd_v3_1_stop(struct amdgpu_device *adev)
{
	uint32_t i, j;
	uint32_t status;

	WREG32(mmUVD_RBC_RB_CNTL, 0x11010101);

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			status = RREG32(mmUVD_STATUS);
			if (status & 2)
				break;
			mdelay(1);
		}
		if (status & 2)
			break;
	}

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			status = RREG32(mmUVD_LMI_STATUS);
			if (status & 0xf)
				break;
			mdelay(1);
		}
		if (status & 0xf)
			break;
	}

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(mmUVD_LMI_CTRL2, 1 << 8, ~(1 << 8));

	for (i = 0; i < 10; ++i) {
		for (j = 0; j < 100; ++j) {
			status = RREG32(mmUVD_LMI_STATUS);
			if (status & 0x240)
				break;
			mdelay(1);
		}
		if (status & 0x240)
			break;
	}

	WREG32_P(0x3D49, 0, ~(1 << 2));

	WREG32_P(mmUVD_VCPU_CNTL, 0, ~(1 << 9));

	/* put LMI, VCPU, RBC etc... into reset */
	WREG32(mmUVD_SOFT_RESET, UVD_SOFT_RESET__LMI_SOFT_RESET_MASK |
		UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK |
		UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK);

	WREG32(mmUVD_STATUS, 0);

	uvd_v3_1_set_dcm(adev, false);
}

static int uvd_v3_1_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int uvd_v3_1_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: UVD TRAP\n");
	amdgpu_fence_process(&adev->uvd.inst->ring);
	return 0;
}


static const struct amdgpu_irq_src_funcs uvd_v3_1_irq_funcs = {
	.set = uvd_v3_1_set_interrupt_state,
	.process = uvd_v3_1_process_interrupt,
};

static void uvd_v3_1_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->uvd.inst->irq.num_types = 1;
	adev->uvd.inst->irq.funcs = &uvd_v3_1_irq_funcs;
}


static int uvd_v3_1_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	adev->uvd.num_uvd_inst = 1;

	uvd_v3_1_set_ring_funcs(adev);
	uvd_v3_1_set_irq_funcs(adev);

	return 0;
}

static int uvd_v3_1_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_ring *ring;
	struct amdgpu_device *adev = ip_block->adev;
	int r;
	void *ptr;
	uint32_t ucode_len;

	/* UVD TRAP */
	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, 124, &adev->uvd.inst->irq);
	if (r)
		return r;

	r = amdgpu_uvd_sw_init(adev);
	if (r)
		return r;

	ring = &adev->uvd.inst->ring;
	sprintf(ring->name, "uvd");
	r = amdgpu_ring_init(adev, ring, 512, &adev->uvd.inst->irq, 0,
			 AMDGPU_RING_PRIO_DEFAULT, NULL);
	if (r)
		return r;

	r = amdgpu_uvd_resume(adev);
	if (r)
		return r;

	/* Retrieval firmware validate key */
	ptr = adev->uvd.inst[0].cpu_addr;
	ptr += 192 + 16;
	memcpy(&ucode_len, ptr, 4);
	ptr += ucode_len;
	memcpy(&adev->uvd.keyselect, ptr, 4);

	return r;
}

static int uvd_v3_1_sw_fini(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

	r = amdgpu_uvd_suspend(adev);
	if (r)
		return r;

	return amdgpu_uvd_sw_fini(adev);
}

static void uvd_v3_1_enable_mgcg(struct amdgpu_device *adev,
				 bool enable)
{
	u32 orig, data;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_UVD_MGCG)) {
		data = RREG32_UVD_CTX(ixUVD_CGC_MEM_CTRL);
		data |= 0x3fff;
		WREG32_UVD_CTX(ixUVD_CGC_MEM_CTRL, data);

		orig = data = RREG32(mmUVD_CGC_CTRL);
		data |= UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK;
		if (orig != data)
			WREG32(mmUVD_CGC_CTRL, data);
	} else {
		data = RREG32_UVD_CTX(ixUVD_CGC_MEM_CTRL);
		data &= ~0x3fff;
		WREG32_UVD_CTX(ixUVD_CGC_MEM_CTRL, data);

		orig = data = RREG32(mmUVD_CGC_CTRL);
		data &= ~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK;
		if (orig != data)
			WREG32(mmUVD_CGC_CTRL, data);
	}
}

/**
 * uvd_v3_1_hw_init - start and test UVD block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int uvd_v3_1_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring = &adev->uvd.inst->ring;
	uint32_t tmp;
	int r;

	uvd_v3_1_mc_resume(adev);

	r = uvd_v3_1_fw_validate(adev);
	if (r) {
		DRM_ERROR("amdgpu: UVD Firmware validate fail (%d).\n", r);
		return r;
	}

	uvd_v3_1_enable_mgcg(adev, true);
	amdgpu_asic_set_uvd_clocks(adev, 53300, 40000);

	uvd_v3_1_start(adev);

	r = amdgpu_ring_test_helper(ring);
	if (r) {
		DRM_ERROR("amdgpu: UVD ring test fail (%d).\n", r);
		goto done;
	}

	r = amdgpu_ring_alloc(ring, 10);
	if (r) {
		DRM_ERROR("amdgpu: ring failed to lock UVD ring (%d).\n", r);
		goto done;
	}

	tmp = PACKET0(mmUVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL, 0);
	amdgpu_ring_write(ring, tmp);
	amdgpu_ring_write(ring, 0xFFFFF);

	tmp = PACKET0(mmUVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL, 0);
	amdgpu_ring_write(ring, tmp);
	amdgpu_ring_write(ring, 0xFFFFF);

	tmp = PACKET0(mmUVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL, 0);
	amdgpu_ring_write(ring, tmp);
	amdgpu_ring_write(ring, 0xFFFFF);

	/* Clear timeout status bits */
	amdgpu_ring_write(ring, PACKET0(mmUVD_SEMA_TIMEOUT_STATUS, 0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring, PACKET0(mmUVD_SEMA_CNTL, 0));
	amdgpu_ring_write(ring, 3);

	amdgpu_ring_commit(ring);

done:
	if (!r)
		DRM_INFO("UVD initialized successfully.\n");

	return r;
}

/**
 * uvd_v3_1_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the UVD block, mark ring as not ready any more
 */
static int uvd_v3_1_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	cancel_delayed_work_sync(&adev->uvd.idle_work);

	if (RREG32(mmUVD_STATUS) != 0)
		uvd_v3_1_stop(adev);

	return 0;
}

static int uvd_v3_1_prepare_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	return amdgpu_uvd_prepare_suspend(adev);
}

static int uvd_v3_1_suspend(struct amdgpu_ip_block *ip_block)
{
	int r;
	struct amdgpu_device *adev = ip_block->adev;

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
	cancel_delayed_work_sync(&adev->uvd.idle_work);

	if (adev->pm.dpm_enabled) {
		amdgpu_dpm_enable_uvd(adev, false);
	} else {
		amdgpu_asic_set_uvd_clocks(adev, 0, 0);
		/* shutdown the UVD block */
		amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
						       AMD_PG_STATE_GATE);
		amdgpu_device_ip_set_clockgating_state(adev, AMD_IP_BLOCK_TYPE_UVD,
						       AMD_CG_STATE_GATE);
	}

	r = uvd_v3_1_hw_fini(ip_block);
	if (r)
		return r;

	return amdgpu_uvd_suspend(adev);
}

static int uvd_v3_1_resume(struct amdgpu_ip_block *ip_block)
{
	int r;

	r = amdgpu_uvd_resume(ip_block->adev);
	if (r)
		return r;

	return uvd_v3_1_hw_init(ip_block);
}

static bool uvd_v3_1_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return !(RREG32(mmSRBM_STATUS) & SRBM_STATUS__UVD_BUSY_MASK);
}

static int uvd_v3_1_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	unsigned i;
	struct amdgpu_device *adev = ip_block->adev;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (!(RREG32(mmSRBM_STATUS) & SRBM_STATUS__UVD_BUSY_MASK))
			return 0;
	}
	return -ETIMEDOUT;
}

static int uvd_v3_1_soft_reset(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	uvd_v3_1_stop(adev);

	WREG32_P(mmSRBM_SOFT_RESET, SRBM_SOFT_RESET__SOFT_RESET_UVD_MASK,
			 ~SRBM_SOFT_RESET__SOFT_RESET_UVD_MASK);
	mdelay(5);

	return uvd_v3_1_start(adev);
}

static int uvd_v3_1_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int uvd_v3_1_set_powergating_state(struct amdgpu_ip_block *ip_block,
					  enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs uvd_v3_1_ip_funcs = {
	.name = "uvd_v3_1",
	.early_init = uvd_v3_1_early_init,
	.sw_init = uvd_v3_1_sw_init,
	.sw_fini = uvd_v3_1_sw_fini,
	.hw_init = uvd_v3_1_hw_init,
	.hw_fini = uvd_v3_1_hw_fini,
	.prepare_suspend = uvd_v3_1_prepare_suspend,
	.suspend = uvd_v3_1_suspend,
	.resume = uvd_v3_1_resume,
	.is_idle = uvd_v3_1_is_idle,
	.wait_for_idle = uvd_v3_1_wait_for_idle,
	.soft_reset = uvd_v3_1_soft_reset,
	.set_clockgating_state = uvd_v3_1_set_clockgating_state,
	.set_powergating_state = uvd_v3_1_set_powergating_state,
};

const struct amdgpu_ip_block_version uvd_v3_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_UVD,
	.major = 3,
	.minor = 1,
	.rev = 0,
	.funcs = &uvd_v3_1_ip_funcs,
};
