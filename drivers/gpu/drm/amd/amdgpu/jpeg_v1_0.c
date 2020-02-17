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

#include "amdgpu.h"
#include "amdgpu_jpeg.h"
#include "soc15.h"
#include "soc15d.h"
#include "vcn_v1_0.h"

#include "vcn/vcn_1_0_offset.h"
#include "vcn/vcn_1_0_sh_mask.h"

static void jpeg_v1_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void jpeg_v1_0_set_irq_funcs(struct amdgpu_device *adev);

static void jpeg_v1_0_decode_ring_patch_wreg(struct amdgpu_ring *ring, uint32_t *ptr, uint32_t reg_offset, uint32_t val)
{
	struct amdgpu_device *adev = ring->adev;
	ring->ring[(*ptr)++] = PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0);
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
		((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		ring->ring[(*ptr)++] = 0;
		ring->ring[(*ptr)++] = PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE0);
	} else {
		ring->ring[(*ptr)++] = reg_offset;
		ring->ring[(*ptr)++] = PACKETJ(0, 0, 0, PACKETJ_TYPE0);
	}
	ring->ring[(*ptr)++] = val;
}

static void jpeg_v1_0_decode_ring_set_patch_ring(struct amdgpu_ring *ring, uint32_t ptr)
{
	struct amdgpu_device *adev = ring->adev;

	uint32_t reg, reg_offset, val, mask, i;

	// 1st: program mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW
	reg = SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW);
	reg_offset = (reg << 2);
	val = lower_32_bits(ring->gpu_addr);
	jpeg_v1_0_decode_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 2nd: program mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH
	reg = SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH);
	reg_offset = (reg << 2);
	val = upper_32_bits(ring->gpu_addr);
	jpeg_v1_0_decode_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 3rd to 5th: issue MEM_READ commands
	for (i = 0; i <= 2; i++) {
		ring->ring[ptr++] = PACKETJ(0, 0, 0, PACKETJ_TYPE2);
		ring->ring[ptr++] = 0;
	}

	// 6th: program mmUVD_JRBC_RB_CNTL register to enable NO_FETCH and RPTR write ability
	reg = SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_CNTL);
	reg_offset = (reg << 2);
	val = 0x13;
	jpeg_v1_0_decode_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 7th: program mmUVD_JRBC_RB_REF_DATA
	reg = SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_REF_DATA);
	reg_offset = (reg << 2);
	val = 0x1;
	jpeg_v1_0_decode_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 8th: issue conditional register read mmUVD_JRBC_RB_CNTL
	reg = SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_CNTL);
	reg_offset = (reg << 2);
	val = 0x1;
	mask = 0x1;

	ring->ring[ptr++] = PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0);
	ring->ring[ptr++] = 0x01400200;
	ring->ring[ptr++] = PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0);
	ring->ring[ptr++] = val;
	ring->ring[ptr++] = PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0);
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
		((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		ring->ring[ptr++] = 0;
		ring->ring[ptr++] = PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE3);
	} else {
		ring->ring[ptr++] = reg_offset;
		ring->ring[ptr++] = PACKETJ(0, 0, 0, PACKETJ_TYPE3);
	}
	ring->ring[ptr++] = mask;

	//9th to 21st: insert no-op
	for (i = 0; i <= 12; i++) {
		ring->ring[ptr++] = PACKETJ(0, 0, 0, PACKETJ_TYPE6);
		ring->ring[ptr++] = 0;
	}

	//22nd: reset mmUVD_JRBC_RB_RPTR
	reg = SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_RPTR);
	reg_offset = (reg << 2);
	val = 0;
	jpeg_v1_0_decode_ring_patch_wreg(ring, &ptr, reg_offset, val);

	//23rd: program mmUVD_JRBC_RB_CNTL to disable no_fetch
	reg = SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_CNTL);
	reg_offset = (reg << 2);
	val = 0x12;
	jpeg_v1_0_decode_ring_patch_wreg(ring, &ptr, reg_offset, val);
}

/**
 * jpeg_v1_0_decode_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t jpeg_v1_0_decode_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_RPTR);
}

/**
 * jpeg_v1_0_decode_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t jpeg_v1_0_decode_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR);
}

/**
 * jpeg_v1_0_decode_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void jpeg_v1_0_decode_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR, lower_32_bits(ring->wptr));
}

/**
 * jpeg_v1_0_decode_ring_insert_start - insert a start command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a start command to the ring.
 */
static void jpeg_v1_0_decode_ring_insert_start(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x68e04);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x80010000);
}

/**
 * jpeg_v1_0_decode_ring_insert_end - insert a end command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a end command to the ring.
 */
static void jpeg_v1_0_decode_ring_insert_end(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x68e04);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x00010000);
}

/**
 * jpeg_v1_0_decode_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
static void jpeg_v1_0_decode_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				     unsigned flags)
{
	struct amdgpu_device *adev = ring->adev;

	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_GPCOM_DATA0), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_GPCOM_DATA1), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_GPCOM_CMD), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_GPCOM_CMD), 0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE4));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(0, 0, PACKETJ_CONDITION_CHECK3, PACKETJ_TYPE2));
	amdgpu_ring_write(ring, 0xffffffff);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x3fbc);

	amdgpu_ring_write(ring,
		PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x1);

	/* emit trap */
	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE7));
	amdgpu_ring_write(ring, 0);
}

/**
 * jpeg_v1_0_decode_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write ring commands to execute the indirect buffer.
 */
static void jpeg_v1_0_decode_ring_emit_ib(struct amdgpu_ring *ring,
					struct amdgpu_job *job,
					struct amdgpu_ib *ib,
					uint32_t flags)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned vmid = AMDGPU_JOB_GET_VMID(job);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_IB_VMID), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, (vmid | (vmid << 4)));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JPEG_VMID), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, (vmid | (vmid << 4)));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_IB_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_IB_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_IB_SIZE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, ib->length_dw);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(0, 0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE2));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x2);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_STATUS), 0, PACKETJ_CONDITION_CHECK3, PACKETJ_TYPE3));
	amdgpu_ring_write(ring, 0x2);
}

static void jpeg_v1_0_decode_ring_emit_reg_wait(struct amdgpu_ring *ring,
					    uint32_t reg, uint32_t val,
					    uint32_t mask)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg_offset = (reg << 2);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, val);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
		((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring,
			PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE3));
	} else {
		amdgpu_ring_write(ring, reg_offset);
		amdgpu_ring_write(ring,
			PACKETJ(0, 0, 0, PACKETJ_TYPE3));
	}
	amdgpu_ring_write(ring, mask);
}

static void jpeg_v1_0_decode_ring_emit_vm_flush(struct amdgpu_ring *ring,
		unsigned vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t data0, data1, mask;

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for register write */
	data0 = hub->ctx0_ptb_addr_lo32 + vmid * 2;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	jpeg_v1_0_decode_ring_emit_reg_wait(ring, data0, data1, mask);
}

static void jpeg_v1_0_decode_ring_emit_wreg(struct amdgpu_ring *ring,
					uint32_t reg, uint32_t val)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg_offset = (reg << 2);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(JPEG, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
			((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring,
			PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE0));
	} else {
		amdgpu_ring_write(ring, reg_offset);
		amdgpu_ring_write(ring,
			PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	}
	amdgpu_ring_write(ring, val);
}

static void jpeg_v1_0_decode_ring_nop(struct amdgpu_ring *ring, uint32_t count)
{
	int i;

	WARN_ON(ring->wptr % 2 || count % 2);

	for (i = 0; i < count / 2; i++) {
		amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE6));
		amdgpu_ring_write(ring, 0);
	}
}

static int jpeg_v1_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int jpeg_v1_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: JPEG decode TRAP\n");

	switch (entry->src_id) {
	case 126:
		amdgpu_fence_process(&adev->jpeg.inst->ring_dec);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

/**
 * jpeg_v1_0_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
int jpeg_v1_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->jpeg.num_jpeg_inst = 1;

	jpeg_v1_0_set_dec_ring_funcs(adev);
	jpeg_v1_0_set_irq_funcs(adev);

	return 0;
}

/**
 * jpeg_v1_0_sw_init - sw init for JPEG block
 *
 * @handle: amdgpu_device pointer
 *
 */
int jpeg_v1_0_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring;
	int r;

	/* JPEG TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN, 126, &adev->jpeg.inst->irq);
	if (r)
		return r;

	ring = &adev->jpeg.inst->ring_dec;
	sprintf(ring->name, "jpeg_dec");
	r = amdgpu_ring_init(adev, ring, 512, &adev->jpeg.inst->irq, 0);
	if (r)
		return r;

	adev->jpeg.internal.jpeg_pitch = adev->jpeg.inst->external.jpeg_pitch =
		SOC15_REG_OFFSET(JPEG, 0, mmUVD_JPEG_PITCH);

	return 0;
}

/**
 * jpeg_v1_0_sw_fini - sw fini for JPEG block
 *
 * @handle: amdgpu_device pointer
 *
 * JPEG free up sw allocation
 */
void jpeg_v1_0_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_ring_fini(&adev->jpeg.inst[0].ring_dec);
}

/**
 * jpeg_v1_0_start - start JPEG block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the JPEG block
 */
void jpeg_v1_0_start(struct amdgpu_device *adev, int mode)
{
	struct amdgpu_ring *ring = &adev->jpeg.inst->ring_dec;

	if (mode == 0) {
		WREG32_SOC15(JPEG, 0, mmUVD_LMI_JRBC_RB_VMID, 0);
		WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_CNTL, UVD_JRBC_RB_CNTL__RB_NO_FETCH_MASK |
				UVD_JRBC_RB_CNTL__RB_RPTR_WR_EN_MASK);
		WREG32_SOC15(JPEG, 0, mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW, lower_32_bits(ring->gpu_addr));
		WREG32_SOC15(JPEG, 0, mmUVD_LMI_JRBC_RB_64BIT_BAR_HIGH, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_RPTR, 0);
		WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR, 0);
		WREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_CNTL, UVD_JRBC_RB_CNTL__RB_RPTR_WR_EN_MASK);
	}

	/* initialize wptr */
	ring->wptr = RREG32_SOC15(JPEG, 0, mmUVD_JRBC_RB_WPTR);

	/* copy patch commands to the jpeg ring */
	jpeg_v1_0_decode_ring_set_patch_ring(ring,
		(ring->wptr + ring->max_dw * amdgpu_sched_hw_submission));
}

static const struct amdgpu_ring_funcs jpeg_v1_0_decode_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.nop = PACKET0(0x81ff, 0),
	.support_64bit_ptrs = false,
	.no_user_fence = true,
	.vmhub = AMDGPU_MMHUB_0,
	.extra_dw = 64,
	.get_rptr = jpeg_v1_0_decode_ring_get_rptr,
	.get_wptr = jpeg_v1_0_decode_ring_get_wptr,
	.set_wptr = jpeg_v1_0_decode_ring_set_wptr,
	.emit_frame_size =
		6 + 6 + /* hdp invalidate / flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* jpeg_v1_0_decode_ring_emit_vm_flush */
		26 + 26 + /* jpeg_v1_0_decode_ring_emit_fence x2 vm fence */
		6,
	.emit_ib_size = 22, /* jpeg_v1_0_decode_ring_emit_ib */
	.emit_ib = jpeg_v1_0_decode_ring_emit_ib,
	.emit_fence = jpeg_v1_0_decode_ring_emit_fence,
	.emit_vm_flush = jpeg_v1_0_decode_ring_emit_vm_flush,
	.test_ring = amdgpu_jpeg_dec_ring_test_ring,
	.test_ib = amdgpu_jpeg_dec_ring_test_ib,
	.insert_nop = jpeg_v1_0_decode_ring_nop,
	.insert_start = jpeg_v1_0_decode_ring_insert_start,
	.insert_end = jpeg_v1_0_decode_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = vcn_v1_0_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = jpeg_v1_0_decode_ring_emit_wreg,
	.emit_reg_wait = jpeg_v1_0_decode_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void jpeg_v1_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->ring_dec.funcs = &jpeg_v1_0_decode_ring_vm_funcs;
	DRM_INFO("JPEG decode is enabled in VM mode\n");
}

static const struct amdgpu_irq_src_funcs jpeg_v1_0_irq_funcs = {
	.set = jpeg_v1_0_set_interrupt_state,
	.process = jpeg_v1_0_process_interrupt,
};

static void jpeg_v1_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->jpeg.inst->irq.funcs = &jpeg_v1_0_irq_funcs;
}
