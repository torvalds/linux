/*
 * Copyright 2010 Advanced Micro Devices, Inc.
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

#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_trace.h"
#include "nid.h"

u32 cayman_gpu_check_soft_reset(struct radeon_device *rdev);

/*
 * DMA
 * Starting with R600, the GPU has an asynchronous
 * DMA engine.  The programming model is very similar
 * to the 3D engine (ring buffer, IBs, etc.), but the
 * DMA controller has it's own packet format that is
 * different form the PM4 format used by the 3D engine.
 * It supports copying data, writing embedded data,
 * solid fills, and a number of other things.  It also
 * has support for tiling/detiling of buffers.
 * Cayman and newer support two asynchronous DMA engines.
 */

/**
 * cayman_dma_get_rptr - get the current read pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Get the current rptr from the hardware (cayman+).
 */
uint32_t cayman_dma_get_rptr(struct radeon_device *rdev,
			     struct radeon_ring *ring)
{
	u32 rptr, reg;

	if (rdev->wb.enabled) {
		rptr = rdev->wb.wb[ring->rptr_offs/4];
	} else {
		if (ring->idx == R600_RING_TYPE_DMA_INDEX)
			reg = DMA_RB_RPTR + DMA0_REGISTER_OFFSET;
		else
			reg = DMA_RB_RPTR + DMA1_REGISTER_OFFSET;

		rptr = RREG32(reg);
	}

	return (rptr & 0x3fffc) >> 2;
}

/**
 * cayman_dma_get_wptr - get the current write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Get the current wptr from the hardware (cayman+).
 */
uint32_t cayman_dma_get_wptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	u32 reg;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		reg = DMA_RB_WPTR + DMA0_REGISTER_OFFSET;
	else
		reg = DMA_RB_WPTR + DMA1_REGISTER_OFFSET;

	return (RREG32(reg) & 0x3fffc) >> 2;
}

/**
 * cayman_dma_set_wptr - commit the write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Write the wptr back to the hardware (cayman+).
 */
void cayman_dma_set_wptr(struct radeon_device *rdev,
			 struct radeon_ring *ring)
{
	u32 reg;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		reg = DMA_RB_WPTR + DMA0_REGISTER_OFFSET;
	else
		reg = DMA_RB_WPTR + DMA1_REGISTER_OFFSET;

	WREG32(reg, (ring->wptr << 2) & 0x3fffc);
}

/**
 * cayman_dma_ring_ib_execute - Schedule an IB on the DMA engine
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to schedule
 *
 * Schedule an IB in the DMA ring (cayman-SI).
 */
void cayman_dma_ring_ib_execute(struct radeon_device *rdev,
				struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	unsigned vm_id = ib->vm ? ib->vm->ids[ib->ring].id : 0;

	if (rdev->wb.enabled) {
		u32 next_rptr = ring->wptr + 4;
		while ((next_rptr & 7) != 5)
			next_rptr++;
		next_rptr += 3;
		radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 1));
		radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
		radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xff);
		radeon_ring_write(ring, next_rptr);
	}

	/* The indirect buffer packet must end on an 8 DW boundary in the DMA ring.
	 * Pad as necessary with NOPs.
	 */
	while ((ring->wptr & 7) != 5)
		radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0));
	radeon_ring_write(ring, DMA_IB_PACKET(DMA_PACKET_INDIRECT_BUFFER, vm_id, 0));
	radeon_ring_write(ring, (ib->gpu_addr & 0xFFFFFFE0));
	radeon_ring_write(ring, (ib->length_dw << 12) | (upper_32_bits(ib->gpu_addr) & 0xFF));

}

/**
 * cayman_dma_stop - stop the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the async dma engines (cayman-SI).
 */
void cayman_dma_stop(struct radeon_device *rdev)
{
	u32 rb_cntl;

	if ((rdev->asic->copy.copy_ring_index == R600_RING_TYPE_DMA_INDEX) ||
	    (rdev->asic->copy.copy_ring_index == CAYMAN_RING_TYPE_DMA1_INDEX))
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);

	/* dma0 */
	rb_cntl = RREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET);
	rb_cntl &= ~DMA_RB_ENABLE;
	WREG32(DMA_RB_CNTL + DMA0_REGISTER_OFFSET, rb_cntl);

	/* dma1 */
	rb_cntl = RREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET);
	rb_cntl &= ~DMA_RB_ENABLE;
	WREG32(DMA_RB_CNTL + DMA1_REGISTER_OFFSET, rb_cntl);

	rdev->ring[R600_RING_TYPE_DMA_INDEX].ready = false;
	rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX].ready = false;
}

/**
 * cayman_dma_resume - setup and start the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Set up the DMA ring buffers and enable them. (cayman-SI).
 * Returns 0 for success, error for failure.
 */
int cayman_dma_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 rb_cntl, dma_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 reg_offset, wb_offset;
	int i, r;

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
			reg_offset = DMA0_REGISTER_OFFSET;
			wb_offset = R600_WB_DMA_RPTR_OFFSET;
		} else {
			ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
			reg_offset = DMA1_REGISTER_OFFSET;
			wb_offset = CAYMAN_WB_DMA1_RPTR_OFFSET;
		}

		WREG32(DMA_SEM_INCOMPLETE_TIMER_CNTL + reg_offset, 0);
		WREG32(DMA_SEM_WAIT_FAIL_TIMER_CNTL + reg_offset, 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = order_base_2(ring->ring_size / 4);
		rb_cntl = rb_bufsz << 1;
#ifdef __BIG_ENDIAN
		rb_cntl |= DMA_RB_SWAP_ENABLE | DMA_RPTR_WRITEBACK_SWAP_ENABLE;
#endif
		WREG32(DMA_RB_CNTL + reg_offset, rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(DMA_RB_RPTR + reg_offset, 0);
		WREG32(DMA_RB_WPTR + reg_offset, 0);

		/* set the wb address whether it's enabled or not */
		WREG32(DMA_RB_RPTR_ADDR_HI + reg_offset,
		       upper_32_bits(rdev->wb.gpu_addr + wb_offset) & 0xFF);
		WREG32(DMA_RB_RPTR_ADDR_LO + reg_offset,
		       ((rdev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC));

		if (rdev->wb.enabled)
			rb_cntl |= DMA_RPTR_WRITEBACK_ENABLE;

		WREG32(DMA_RB_BASE + reg_offset, ring->gpu_addr >> 8);

		/* enable DMA IBs */
		ib_cntl = DMA_IB_ENABLE | CMD_VMID_FORCE;
#ifdef __BIG_ENDIAN
		ib_cntl |= DMA_IB_SWAP_ENABLE;
#endif
		WREG32(DMA_IB_CNTL + reg_offset, ib_cntl);

		dma_cntl = RREG32(DMA_CNTL + reg_offset);
		dma_cntl &= ~CTXEMPTY_INT_ENABLE;
		WREG32(DMA_CNTL + reg_offset, dma_cntl);

		ring->wptr = 0;
		WREG32(DMA_RB_WPTR + reg_offset, ring->wptr << 2);

		WREG32(DMA_RB_CNTL + reg_offset, rb_cntl | DMA_RB_ENABLE);

		ring->ready = true;

		r = radeon_ring_test(rdev, ring->idx, ring);
		if (r) {
			ring->ready = false;
			return r;
		}
	}

	if ((rdev->asic->copy.copy_ring_index == R600_RING_TYPE_DMA_INDEX) ||
	    (rdev->asic->copy.copy_ring_index == CAYMAN_RING_TYPE_DMA1_INDEX))
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	return 0;
}

/**
 * cayman_dma_fini - tear down the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the async dma engines and free the rings (cayman-SI).
 */
void cayman_dma_fini(struct radeon_device *rdev)
{
	cayman_dma_stop(rdev);
	radeon_ring_fini(rdev, &rdev->ring[R600_RING_TYPE_DMA_INDEX]);
	radeon_ring_fini(rdev, &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX]);
}

/**
 * cayman_dma_is_lockup - Check if the DMA engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the async DMA engine is locked up.
 * Returns true if the engine appears to be locked up, false if not.
 */
bool cayman_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = cayman_gpu_check_soft_reset(rdev);
	u32 mask;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		mask = RADEON_RESET_DMA;
	else
		mask = RADEON_RESET_DMA1;

	if (!(reset_mask & mask)) {
		radeon_ring_lockup_update(rdev, ring);
		return false;
	}
	return radeon_ring_test_lockup(rdev, ring);
}

/**
 * cayman_dma_vm_copy_pages - update PTEs by copying them from the GART
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr where to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using the DMA (cayman/TN).
 */
void cayman_dma_vm_copy_pages(struct radeon_device *rdev,
			      struct radeon_ib *ib,
			      uint64_t pe, uint64_t src,
			      unsigned count)
{
	unsigned ndw;

	while (count) {
		ndw = count * 2;
		if (ndw > 0xFFFFE)
			ndw = 0xFFFFE;

		ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_COPY,
						      0, 0, ndw);
		ib->ptr[ib->length_dw++] = lower_32_bits(pe);
		ib->ptr[ib->length_dw++] = lower_32_bits(src);
		ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
		ib->ptr[ib->length_dw++] = upper_32_bits(src) & 0xff;

		pe += ndw * 4;
		src += ndw * 4;
		count -= ndw / 2;
	}
}

/**
 * cayman_dma_vm_write_pages - update PTEs by writing them manually
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: hw access flags
 *
 * Update PTEs by writing them manually using the DMA (cayman/TN).
 */
void cayman_dma_vm_write_pages(struct radeon_device *rdev,
			       struct radeon_ib *ib,
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

		/* for non-physically contiguous pages (system) */
		ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_WRITE,
						      0, 0, ndw);
		ib->ptr[ib->length_dw++] = pe;
		ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
		for (; ndw > 0; ndw -= 2, --count, pe += 8) {
			if (flags & R600_PTE_SYSTEM) {
				value = radeon_vm_map_gart(rdev, addr);
			} else if (flags & R600_PTE_VALID) {
				value = addr;
			} else {
				value = 0;
			}
			addr += incr;
			value |= flags;
			ib->ptr[ib->length_dw++] = value;
			ib->ptr[ib->length_dw++] = upper_32_bits(value);
		}
	}
}

/**
 * cayman_dma_vm_set_pages - update the page tables using the DMA
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: hw access flags
 *
 * Update the page tables using the DMA (cayman/TN).
 */
void cayman_dma_vm_set_pages(struct radeon_device *rdev,
			     struct radeon_ib *ib,
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

		if (flags & R600_PTE_VALID)
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
 * cayman_dma_vm_pad_ib - pad the IB to the required number of dw
 *
 * @ib: indirect buffer to fill with padding
 *
 */
void cayman_dma_vm_pad_ib(struct radeon_ib *ib)
{
	while (ib->length_dw & 0x7)
		ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0);
}

void cayman_dma_vm_flush(struct radeon_device *rdev, struct radeon_ring *ring,
			 unsigned vm_id, uint64_t pd_addr)
{
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0));
	radeon_ring_write(ring, (0xf << 16) | ((VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm_id << 2)) >> 2));
	radeon_ring_write(ring, pd_addr >> 12);

	/* flush hdp cache */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0));
	radeon_ring_write(ring, (0xf << 16) | (HDP_MEM_COHERENCY_FLUSH_CNTL >> 2));
	radeon_ring_write(ring, 1);

	/* bits 0-7 are the VM contexts0-7 */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0));
	radeon_ring_write(ring, (0xf << 16) | (VM_INVALIDATE_REQUEST >> 2));
	radeon_ring_write(ring, 1 << vm_id);

	/* wait for invalidate to complete */
	radeon_ring_write(ring, DMA_SRBM_READ_PACKET);
	radeon_ring_write(ring, (0xff << 20) | (VM_INVALIDATE_REQUEST >> 2));
	radeon_ring_write(ring, 0); /* mask */
	radeon_ring_write(ring, 0); /* value */
}

