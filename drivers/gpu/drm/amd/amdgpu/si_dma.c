/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "si/sid.h"

const u32 sdma_offsets[SDMA_MAX_INSTANCE] =
{
	DMA0_REGISTER_OFFSET,
	DMA1_REGISTER_OFFSET
};

static void si_dma_set_ring_funcs(struct amdgpu_device *adev);
static void si_dma_set_buffer_funcs(struct amdgpu_device *adev);
static void si_dma_set_vm_pte_funcs(struct amdgpu_device *adev);
static void si_dma_set_irq_funcs(struct amdgpu_device *adev);

static uint32_t si_dma_ring_get_rptr(struct amdgpu_ring *ring)
{
	return ring->adev->wb.wb[ring->rptr_offs>>2];
}

static uint32_t si_dma_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 me = (ring == &adev->sdma.instance[0].ring) ? 0 : 1;

	return (RREG32(DMA_RB_WPTR + sdma_offsets[me]) & 0x3fffc) >> 2;
}

static void si_dma_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	u32 me = (ring == &adev->sdma.instance[0].ring) ? 0 : 1;

	WREG32(DMA_RB_WPTR + sdma_offsets[me], (ring->wptr << 2) & 0x3fffc);
}

static void si_dma_ring_emit_ib(struct amdgpu_ring *ring,
				struct amdgpu_ib *ib,
				unsigned vm_id, bool ctx_switch)
{
	/* The indirect buffer packet must end on an 8 DW boundary in the DMA ring.
	 * Pad as necessary with NOPs.
	 */
	while ((ring->wptr & 7) != 5)
		amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0));
	amdgpu_ring_write(ring, DMA_IB_PACKET(DMA_PACKET_INDIRECT_BUFFER, vm_id, 0));
	amdgpu_ring_write(ring, (ib->gpu_addr & 0xFFFFFFE0));
	amdgpu_ring_write(ring, (ib->length_dw << 12) | (upper_32_bits(ib->gpu_addr) & 0xFF));

}

static void si_dma_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	amdgpu_ring_write(ring, (0xf << 16) | (HDP_MEM_COHERENCY_FLUSH_CNTL));
	amdgpu_ring_write(ring, 1);
}

static void si_dma_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	amdgpu_ring_write(ring, (0xf << 16) | (HDP_DEBUG0));
	amdgpu_ring_write(ring, 1);
}

/**
 * si_dma_ring_emit_fence - emit a fence on the DMA ring
 *
 * @ring: amdgpu ring pointer
 * @fence: amdgpu fence object
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (VI).
 */
static void si_dma_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				      unsigned flags)
{

	bool write64bit = flags & AMDGPU_FENCE_FLAG_64BIT;
	/* write the fence */
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_FENCE, 0, 0, 0, 0));
	amdgpu_ring_write(ring, addr & 0xfffffffc);
	amdgpu_ring_write(ring, (upper_32_bits(addr) & 0xff));
	amdgpu_ring_write(ring, seq);
	/* optionally write high bits as well */
	if (write64bit) {
		addr += 4;
		amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_FENCE, 0, 0, 0, 0));
		amdgpu_ring_write(ring, addr & 0xfffffffc);
		amdgpu_ring_write(ring, (upper_32_bits(addr) & 0xff));
		amdgpu_ring_write(ring, upper_32_bits(seq));
	}
	/* generate an interrupt */
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_TRAP, 0, 0, 0, 0));
}

static void si_dma_stop(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl;
	unsigned i;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		/* dma0 */
		rb_cntl = RREG32(DMA_RB_CNTL + sdma_offsets[i]);
		rb_cntl &= ~DMA_RB_ENABLE;
		WREG32(DMA_RB_CNTL + sdma_offsets[i], rb_cntl);

		if (adev->mman.buffer_funcs_ring == ring)
			amdgpu_ttm_set_active_vram_size(adev, adev->mc.visible_vram_size);
		ring->ready = false;
	}
}

static int si_dma_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	u32 rb_cntl, dma_cntl, ib_cntl, rb_bufsz;
	int i, r;
	uint64_t rptr_addr;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;

		WREG32(DMA_SEM_INCOMPLETE_TIMER_CNTL + sdma_offsets[i], 0);
		WREG32(DMA_SEM_WAIT_FAIL_TIMER_CNTL + sdma_offsets[i], 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = order_base_2(ring->ring_size / 4);
		rb_cntl = rb_bufsz << 1;
#ifdef __BIG_ENDIAN
		rb_cntl |= DMA_RB_SWAP_ENABLE | DMA_RPTR_WRITEBACK_SWAP_ENABLE;
#endif
		WREG32(DMA_RB_CNTL + sdma_offsets[i], rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(DMA_RB_RPTR + sdma_offsets[i], 0);
		WREG32(DMA_RB_WPTR + sdma_offsets[i], 0);

		rptr_addr = adev->wb.gpu_addr + (ring->rptr_offs * 4);

		WREG32(DMA_RB_RPTR_ADDR_LO + sdma_offsets[i], lower_32_bits(rptr_addr));
		WREG32(DMA_RB_RPTR_ADDR_HI + sdma_offsets[i], upper_32_bits(rptr_addr) & 0xFF);

		rb_cntl |= DMA_RPTR_WRITEBACK_ENABLE;

		WREG32(DMA_RB_BASE + sdma_offsets[i], ring->gpu_addr >> 8);

		/* enable DMA IBs */
		ib_cntl = DMA_IB_ENABLE | CMD_VMID_FORCE;
#ifdef __BIG_ENDIAN
		ib_cntl |= DMA_IB_SWAP_ENABLE;
#endif
		WREG32(DMA_IB_CNTL + sdma_offsets[i], ib_cntl);

		dma_cntl = RREG32(DMA_CNTL + sdma_offsets[i]);
		dma_cntl &= ~CTXEMPTY_INT_ENABLE;
		WREG32(DMA_CNTL + sdma_offsets[i], dma_cntl);

		ring->wptr = 0;
		WREG32(DMA_RB_WPTR + sdma_offsets[i], ring->wptr << 2);
		WREG32(DMA_RB_CNTL + sdma_offsets[i], rb_cntl | DMA_RB_ENABLE);

		ring->ready = true;

		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->ready = false;
			return r;
		}

		if (adev->mman.buffer_funcs_ring == ring)
			amdgpu_ttm_set_active_vram_size(adev, adev->mc.real_vram_size);
	}

	return 0;
}

/**
 * si_dma_ring_test_ring - simple async dma engine test
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (VI).
 * Returns 0 for success, error for failure.
 */
static int si_dma_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp;
	u64 gpu_addr;

	r = amdgpu_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);

	r = amdgpu_ring_alloc(ring, 4);
	if (r) {
		DRM_ERROR("amdgpu: dma failed to lock ring %d (%d).\n", ring->idx, r);
		amdgpu_wb_free(adev, index);
		return r;
	}

	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 0, 1));
	amdgpu_ring_write(ring, lower_32_bits(gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(gpu_addr) & 0xff);
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = le32_to_cpu(adev->wb.wb[index]);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	amdgpu_wb_free(adev, index);

	return r;
}

/**
 * si_dma_ring_test_ib - test an IB on the DMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 *
 * Test a simple IB in the DMA ring (VI).
 * Returns 0 on success, error on failure.
 */
static int si_dma_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_ib ib;
	struct fence *f = NULL;
	unsigned index;
	u32 tmp = 0;
	u64 gpu_addr;
	long r;

	r = amdgpu_wb_get(adev, &index);
	if (r) {
		dev_err(adev->dev, "(%ld) failed to allocate wb slot\n", r);
		return r;
	}

	gpu_addr = adev->wb.gpu_addr + (index * 4);
	tmp = 0xCAFEDEAD;
	adev->wb.wb[index] = cpu_to_le32(tmp);
	memset(&ib, 0, sizeof(ib));
	r = amdgpu_ib_get(adev, NULL, 256, &ib);
	if (r) {
		DRM_ERROR("amdgpu: failed to get ib (%ld).\n", r);
		goto err0;
	}

	ib.ptr[0] = DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 0, 1);
	ib.ptr[1] = lower_32_bits(gpu_addr);
	ib.ptr[2] = upper_32_bits(gpu_addr) & 0xff;
	ib.ptr[3] = 0xDEADBEEF;
	ib.length_dw = 4;
	r = amdgpu_ib_schedule(ring, 1, &ib, NULL, NULL, &f);
	if (r)
		goto err1;

	r = fence_wait_timeout(f, false, timeout);
	if (r == 0) {
		DRM_ERROR("amdgpu: IB test timed out\n");
		r = -ETIMEDOUT;
		goto err1;
	} else if (r < 0) {
		DRM_ERROR("amdgpu: fence wait failed (%ld).\n", r);
		goto err1;
	}
	tmp = le32_to_cpu(adev->wb.wb[index]);
	if (tmp == 0xDEADBEEF) {
		DRM_INFO("ib test on ring %d succeeded\n", ring->idx);
		r = 0;
	} else {
		DRM_ERROR("amdgpu: ib test failed (0x%08X)\n", tmp);
		r = -EINVAL;
	}

err1:
	amdgpu_ib_free(adev, &ib, NULL);
	fence_put(f);
err0:
	amdgpu_wb_free(adev, index);
	return r;
}

/**
 * cik_dma_vm_copy_pte - update PTEs by copying them from the GART
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using DMA (SI).
 */
static void si_dma_vm_copy_pte(struct amdgpu_ib *ib,
			       uint64_t pe, uint64_t src,
			       unsigned count)
{
	unsigned bytes = count * 8;

	ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_COPY,
					      1, 0, 0, bytes);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = lower_32_bits(src);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
	ib->ptr[ib->length_dw++] = upper_32_bits(src) & 0xff;
}

/**
 * si_dma_vm_write_pte - update PTEs by writing them manually
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @value: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 *
 * Update PTEs by writing them manually using DMA (SI).
 */
static void si_dma_vm_write_pte(struct amdgpu_ib *ib, uint64_t pe,
				uint64_t value, unsigned count,
				uint32_t incr)
{
	unsigned ndw = count * 2;

	ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 0, ndw);
	ib->ptr[ib->length_dw++] = lower_32_bits(pe);
	ib->ptr[ib->length_dw++] = upper_32_bits(pe);
	for (; ndw > 0; ndw -= 2) {
		ib->ptr[ib->length_dw++] = lower_32_bits(value);
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		value += incr;
	}
}

/**
 * si_dma_vm_set_pte_pde - update the page tables using sDMA
 *
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using sDMA (CIK).
 */
static void si_dma_vm_set_pte_pde(struct amdgpu_ib *ib,
				     uint64_t pe,
				     uint64_t addr, unsigned count,
				     uint32_t incr, uint32_t flags)
{
	uint64_t value;
	unsigned ndw;

	while (count) {
		ndw = count * 2;
		if (ndw > 0xFFFFE)
			ndw = 0xFFFFE;

		if (flags & AMDGPU_PTE_VALID)
			value = addr;
		else
			value = 0;

		/* for physically contiguous pages (vram) */
		ib->ptr[ib->length_dw++] = DMA_PTE_PDE_PACKET(ndw);
		ib->ptr[ib->length_dw++] = pe; /* dst addr */
		ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
		ib->ptr[ib->length_dw++] = flags; /* mask */
		ib->ptr[ib->length_dw++] = 0;
		ib->ptr[ib->length_dw++] = value; /* value */
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		ib->ptr[ib->length_dw++] = incr; /* increment size */
		ib->ptr[ib->length_dw++] = 0;
		pe += ndw * 4;
		addr += (ndw / 2) * incr;
		count -= ndw / 2;
	}
}

/**
 * si_dma_pad_ib - pad the IB to the required number of dw
 *
 * @ib: indirect buffer to fill with padding
 *
 */
static void si_dma_ring_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib)
{
	while (ib->length_dw & 0x7)
		ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0);
}

/**
 * cik_sdma_ring_emit_pipeline_sync - sync the pipeline
 *
 * @ring: amdgpu_ring pointer
 *
 * Make sure all previous operations are completed (CIK).
 */
static void si_dma_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	/* wait for idle */
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_POLL_REG_MEM, 0, 0, 0, 0) |
			  (1 << 27)); /* Poll memory */
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, (0xff << 16) | upper_32_bits(addr)); /* retry, addr_hi */
	amdgpu_ring_write(ring, 0xffffffff); /* mask */
	amdgpu_ring_write(ring, seq); /* value */
	amdgpu_ring_write(ring, (3 << 28) | 0x20); /* func(equal) | poll interval */
}

/**
 * si_dma_ring_emit_vm_flush - cik vm flush using sDMA
 *
 * @ring: amdgpu_ring pointer
 * @vm: amdgpu_vm pointer
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (VI).
 */
static void si_dma_ring_emit_vm_flush(struct amdgpu_ring *ring,
				      unsigned vm_id, uint64_t pd_addr)
{
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	if (vm_id < 8)
		amdgpu_ring_write(ring, (0xf << 16) | (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + vm_id));
	else
		amdgpu_ring_write(ring, (0xf << 16) | (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + (vm_id - 8)));
	amdgpu_ring_write(ring, pd_addr >> 12);

	/* bits 0-7 are the VM contexts0-7 */
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	amdgpu_ring_write(ring, (0xf << 16) | (VM_INVALIDATE_REQUEST));
	amdgpu_ring_write(ring, 1 << vm_id);

	/* wait for invalidate to complete */
	amdgpu_ring_write(ring, DMA_PACKET(DMA_PACKET_POLL_REG_MEM, 0, 0, 0, 0));
	amdgpu_ring_write(ring, VM_INVALIDATE_REQUEST);
	amdgpu_ring_write(ring, 0xff << 16); /* retry */
	amdgpu_ring_write(ring, 1 << vm_id); /* mask */
	amdgpu_ring_write(ring, 0); /* value */
	amdgpu_ring_write(ring, (0 << 28) | 0x20); /* func(always) | poll interval */
}

static unsigned si_dma_ring_get_emit_ib_size(struct amdgpu_ring *ring)
{
	return
		7 + 3; /* si_dma_ring_emit_ib */
}

static unsigned si_dma_ring_get_dma_frame_size(struct amdgpu_ring *ring)
{
	return
		3 + /* si_dma_ring_emit_hdp_flush */
		3 + /* si_dma_ring_emit_hdp_invalidate */
		6 + /* si_dma_ring_emit_pipeline_sync */
		12 + /* si_dma_ring_emit_vm_flush */
		9 + 9 + 9; /* si_dma_ring_emit_fence x3 for user fence, vm fence */
}

static int si_dma_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->sdma.num_instances = 2;

	si_dma_set_ring_funcs(adev);
	si_dma_set_buffer_funcs(adev);
	si_dma_set_vm_pte_funcs(adev);
	si_dma_set_irq_funcs(adev);

	return 0;
}

static int si_dma_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* DMA0 trap event */
	r = amdgpu_irq_add_id(adev, 224, &adev->sdma.trap_irq);
	if (r)
		return r;

	/* DMA1 trap event */
	r = amdgpu_irq_add_id(adev, 244, &adev->sdma.trap_irq_1);
	if (r)
		return r;

	for (i = 0; i < adev->sdma.num_instances; i++) {
		ring = &adev->sdma.instance[i].ring;
		ring->ring_obj = NULL;
		ring->use_doorbell = false;
		sprintf(ring->name, "sdma%d", i);
		r = amdgpu_ring_init(adev, ring, 1024,
				     DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0), 0xf,
				     &adev->sdma.trap_irq,
				     (i == 0) ?
				     AMDGPU_SDMA_IRQ_TRAP0 : AMDGPU_SDMA_IRQ_TRAP1,
				     AMDGPU_RING_TYPE_SDMA);
		if (r)
			return r;
	}

	return r;
}

static int si_dma_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		amdgpu_ring_fini(&adev->sdma.instance[i].ring);

	return 0;
}

static int si_dma_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return si_dma_start(adev);
}

static int si_dma_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	si_dma_stop(adev);

	return 0;
}

static int si_dma_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return si_dma_hw_fini(adev);
}

static int si_dma_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return si_dma_hw_init(adev);
}

static bool si_dma_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 tmp = RREG32(SRBM_STATUS2);

	if (tmp & (DMA_BUSY_MASK | DMA1_BUSY_MASK))
	    return false;

	return true;
}

static int si_dma_wait_for_idle(void *handle)
{
	unsigned i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (si_dma_is_idle(handle))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int si_dma_soft_reset(void *handle)
{
	DRM_INFO("si_dma_soft_reset --- not implemented !!!!!!!\n");
	return 0;
}

static int si_dma_set_trap_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *src,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	u32 sdma_cntl;

	switch (type) {
	case AMDGPU_SDMA_IRQ_TRAP0:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			sdma_cntl = RREG32(DMA_CNTL + DMA0_REGISTER_OFFSET);
			sdma_cntl &= ~TRAP_ENABLE;
			WREG32(DMA_CNTL + DMA0_REGISTER_OFFSET, sdma_cntl);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			sdma_cntl = RREG32(DMA_CNTL + DMA0_REGISTER_OFFSET);
			sdma_cntl |= TRAP_ENABLE;
			WREG32(DMA_CNTL + DMA0_REGISTER_OFFSET, sdma_cntl);
			break;
		default:
			break;
		}
		break;
	case AMDGPU_SDMA_IRQ_TRAP1:
		switch (state) {
		case AMDGPU_IRQ_STATE_DISABLE:
			sdma_cntl = RREG32(DMA_CNTL + DMA1_REGISTER_OFFSET);
			sdma_cntl &= ~TRAP_ENABLE;
			WREG32(DMA_CNTL + DMA1_REGISTER_OFFSET, sdma_cntl);
			break;
		case AMDGPU_IRQ_STATE_ENABLE:
			sdma_cntl = RREG32(DMA_CNTL + DMA1_REGISTER_OFFSET);
			sdma_cntl |= TRAP_ENABLE;
			WREG32(DMA_CNTL + DMA1_REGISTER_OFFSET, sdma_cntl);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int si_dma_process_trap_irq(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	amdgpu_fence_process(&adev->sdma.instance[0].ring);

	return 0;
}

static int si_dma_process_trap_irq_1(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	amdgpu_fence_process(&adev->sdma.instance[1].ring);

	return 0;
}

static int si_dma_process_illegal_inst_irq(struct amdgpu_device *adev,
					      struct amdgpu_irq_src *source,
					      struct amdgpu_iv_entry *entry)
{
	DRM_ERROR("Illegal instruction in SDMA command stream\n");
	schedule_work(&adev->reset_work);
	return 0;
}

static int si_dma_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	u32 orig, data, offset;
	int i;
	bool enable;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	enable = (state == AMD_CG_STATE_GATE) ? true : false;

	if (enable && (adev->cg_flags & AMD_CG_SUPPORT_SDMA_MGCG)) {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			if (i == 0)
				offset = DMA0_REGISTER_OFFSET;
			else
				offset = DMA1_REGISTER_OFFSET;
			orig = data = RREG32(DMA_POWER_CNTL + offset);
			data &= ~MEM_POWER_OVERRIDE;
			if (data != orig)
				WREG32(DMA_POWER_CNTL + offset, data);
			WREG32(DMA_CLK_CTRL + offset, 0x00000100);
		}
	} else {
		for (i = 0; i < adev->sdma.num_instances; i++) {
			if (i == 0)
				offset = DMA0_REGISTER_OFFSET;
			else
				offset = DMA1_REGISTER_OFFSET;
			orig = data = RREG32(DMA_POWER_CNTL + offset);
			data |= MEM_POWER_OVERRIDE;
			if (data != orig)
				WREG32(DMA_POWER_CNTL + offset, data);

			orig = data = RREG32(DMA_CLK_CTRL + offset);
			data = 0xff000000;
			if (data != orig)
				WREG32(DMA_CLK_CTRL + offset, data);
		}
	}

	return 0;
}

static int si_dma_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	u32 tmp;

	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	WREG32(DMA_PGFSM_WRITE,  0x00002000);
	WREG32(DMA_PGFSM_CONFIG, 0x100010ff);

	for (tmp = 0; tmp < 5; tmp++)
		WREG32(DMA_PGFSM_WRITE, 0);

	return 0;
}

const struct amd_ip_funcs si_dma_ip_funcs = {
	.name = "si_dma",
	.early_init = si_dma_early_init,
	.late_init = NULL,
	.sw_init = si_dma_sw_init,
	.sw_fini = si_dma_sw_fini,
	.hw_init = si_dma_hw_init,
	.hw_fini = si_dma_hw_fini,
	.suspend = si_dma_suspend,
	.resume = si_dma_resume,
	.is_idle = si_dma_is_idle,
	.wait_for_idle = si_dma_wait_for_idle,
	.soft_reset = si_dma_soft_reset,
	.set_clockgating_state = si_dma_set_clockgating_state,
	.set_powergating_state = si_dma_set_powergating_state,
};

static const struct amdgpu_ring_funcs si_dma_ring_funcs = {
	.get_rptr = si_dma_ring_get_rptr,
	.get_wptr = si_dma_ring_get_wptr,
	.set_wptr = si_dma_ring_set_wptr,
	.emit_ib = si_dma_ring_emit_ib,
	.emit_fence = si_dma_ring_emit_fence,
	.emit_pipeline_sync = si_dma_ring_emit_pipeline_sync,
	.emit_vm_flush = si_dma_ring_emit_vm_flush,
	.emit_hdp_flush = si_dma_ring_emit_hdp_flush,
	.emit_hdp_invalidate = si_dma_ring_emit_hdp_invalidate,
	.test_ring = si_dma_ring_test_ring,
	.test_ib = si_dma_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = si_dma_ring_pad_ib,
	.get_emit_ib_size = si_dma_ring_get_emit_ib_size,
	.get_dma_frame_size = si_dma_ring_get_dma_frame_size,
};

static void si_dma_set_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->sdma.num_instances; i++)
		adev->sdma.instance[i].ring.funcs = &si_dma_ring_funcs;
}

static const struct amdgpu_irq_src_funcs si_dma_trap_irq_funcs = {
	.set = si_dma_set_trap_irq_state,
	.process = si_dma_process_trap_irq,
};

static const struct amdgpu_irq_src_funcs si_dma_trap_irq_funcs_1 = {
	.set = si_dma_set_trap_irq_state,
	.process = si_dma_process_trap_irq_1,
};

static const struct amdgpu_irq_src_funcs si_dma_illegal_inst_irq_funcs = {
	.process = si_dma_process_illegal_inst_irq,
};

static void si_dma_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->sdma.trap_irq.num_types = AMDGPU_SDMA_IRQ_LAST;
	adev->sdma.trap_irq.funcs = &si_dma_trap_irq_funcs;
	adev->sdma.trap_irq_1.funcs = &si_dma_trap_irq_funcs_1;
	adev->sdma.illegal_inst_irq.funcs = &si_dma_illegal_inst_irq_funcs;
}

/**
 * si_dma_emit_copy_buffer - copy buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Copy GPU buffers using the DMA engine (VI).
 * Used by the amdgpu ttm implementation to move pages if
 * registered as the asic copy callback.
 */
static void si_dma_emit_copy_buffer(struct amdgpu_ib *ib,
				       uint64_t src_offset,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_COPY,
					      1, 0, 0, byte_count);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = lower_32_bits(src_offset);
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset) & 0xff;
	ib->ptr[ib->length_dw++] = upper_32_bits(src_offset) & 0xff;
}

/**
 * si_dma_emit_fill_buffer - fill buffer using the sDMA engine
 *
 * @ring: amdgpu_ring structure holding ring information
 * @src_data: value to write to buffer
 * @dst_offset: dst GPU address
 * @byte_count: number of bytes to xfer
 *
 * Fill GPU buffers using the DMA engine (VI).
 */
static void si_dma_emit_fill_buffer(struct amdgpu_ib *ib,
				       uint32_t src_data,
				       uint64_t dst_offset,
				       uint32_t byte_count)
{
	ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_CONSTANT_FILL,
					      0, 0, 0, byte_count / 4);
	ib->ptr[ib->length_dw++] = lower_32_bits(dst_offset);
	ib->ptr[ib->length_dw++] = src_data;
	ib->ptr[ib->length_dw++] = upper_32_bits(dst_offset) << 16;
}


static const struct amdgpu_buffer_funcs si_dma_buffer_funcs = {
	.copy_max_bytes = 0xffff8,
	.copy_num_dw = 5,
	.emit_copy_buffer = si_dma_emit_copy_buffer,

	.fill_max_bytes = 0xffff8,
	.fill_num_dw = 4,
	.emit_fill_buffer = si_dma_emit_fill_buffer,
};

static void si_dma_set_buffer_funcs(struct amdgpu_device *adev)
{
	if (adev->mman.buffer_funcs == NULL) {
		adev->mman.buffer_funcs = &si_dma_buffer_funcs;
		adev->mman.buffer_funcs_ring = &adev->sdma.instance[0].ring;
	}
}

static const struct amdgpu_vm_pte_funcs si_dma_vm_pte_funcs = {
	.copy_pte = si_dma_vm_copy_pte,
	.write_pte = si_dma_vm_write_pte,
	.set_pte_pde = si_dma_vm_set_pte_pde,
};

static void si_dma_set_vm_pte_funcs(struct amdgpu_device *adev)
{
	unsigned i;

	if (adev->vm_manager.vm_pte_funcs == NULL) {
		adev->vm_manager.vm_pte_funcs = &si_dma_vm_pte_funcs;
		for (i = 0; i < adev->sdma.num_instances; i++)
			adev->vm_manager.vm_pte_rings[i] =
				&adev->sdma.instance[i].ring;

		adev->vm_manager.vm_pte_num_rings = adev->sdma.num_instances;
	}
}
