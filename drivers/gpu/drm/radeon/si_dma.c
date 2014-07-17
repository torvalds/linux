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
 * Authors: Alex Deucher
 */
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "radeon_trace.h"
#include "sid.h"

u32 si_gpu_check_soft_reset(struct radeon_device *rdev);

/**
 * si_dma_is_lockup - Check if the DMA engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the async DMA engine is locked up.
 * Returns true if the engine appears to be locked up, false if not.
 */
bool si_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = si_gpu_check_soft_reset(rdev);
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
 * si_dma_vm_set_page - update the page tables using the DMA
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using the DMA (SI).
 */
void si_dma_vm_set_page(struct radeon_device *rdev,
			struct radeon_ib *ib,
			uint64_t pe,
			uint64_t addr, unsigned count,
			uint32_t incr, uint32_t flags)
{
	uint64_t value;
	unsigned ndw;

	trace_radeon_vm_set_page(pe, addr, count, incr, flags);

	/* XXX: How to distinguish between GART and other system memory pages? */
	if (flags & R600_PTE_SYSTEM) {
		uint64_t src = rdev->gart.table_addr + (addr >> 12) * 8;
		while (count) {
			unsigned bytes = count * 8;
			if (bytes > 0xFFFF8)
				bytes = 0xFFFF8;

			ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_COPY,
							      1, 0, 0, bytes);
			ib->ptr[ib->length_dw++] = lower_32_bits(pe);
			ib->ptr[ib->length_dw++] = lower_32_bits(src);
			ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
			ib->ptr[ib->length_dw++] = upper_32_bits(src) & 0xff;

			pe += bytes;
			src += bytes;
			count -= bytes / 8;
		}
	} else if (flags & R600_PTE_SYSTEM) {
		while (count) {
			ndw = count * 2;
			if (ndw > 0xFFFFE)
				ndw = 0xFFFFE;

			/* for non-physically contiguous pages (system) */
			ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 0, ndw);
			ib->ptr[ib->length_dw++] = pe;
			ib->ptr[ib->length_dw++] = upper_32_bits(pe) & 0xff;
			for (; ndw > 0; ndw -= 2, --count, pe += 8) {
				value = radeon_vm_map_gart(rdev, addr);
				value &= 0xFFFFFFFFFFFFF000ULL;
				addr += incr;
				value |= flags;
				ib->ptr[ib->length_dw++] = value;
				ib->ptr[ib->length_dw++] = upper_32_bits(value);
			}
		}
	} else {
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
	while (ib->length_dw & 0x7)
		ib->ptr[ib->length_dw++] = DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0, 0);
}

void si_dma_vm_flush(struct radeon_device *rdev, int ridx, struct radeon_vm *vm)
{
	struct radeon_ring *ring = &rdev->ring[ridx];

	if (vm == NULL)
		return;

	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	if (vm->id < 8) {
		radeon_ring_write(ring, (0xf << 16) | ((VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm->id << 2)) >> 2));
	} else {
		radeon_ring_write(ring, (0xf << 16) | ((VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm->id - 8) << 2)) >> 2));
	}
	radeon_ring_write(ring, vm->pd_gpu_addr >> 12);

	/* flush hdp cache */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	radeon_ring_write(ring, (0xf << 16) | (HDP_MEM_COHERENCY_FLUSH_CNTL >> 2));
	radeon_ring_write(ring, 1);

	/* bits 0-7 are the VM contexts0-7 */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SRBM_WRITE, 0, 0, 0, 0));
	radeon_ring_write(ring, (0xf << 16) | (VM_INVALIDATE_REQUEST >> 2));
	radeon_ring_write(ring, 1 << vm->id);
}

/**
 * si_copy_dma - copy pages using the DMA engine
 *
 * @rdev: radeon_device pointer
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @num_gpu_pages: number of GPU pages to xfer
 * @fence: radeon fence object
 *
 * Copy GPU paging using the DMA engine (SI).
 * Used by the radeon ttm implementation to move pages if
 * registered as the asic copy callback.
 */
int si_copy_dma(struct radeon_device *rdev,
		uint64_t src_offset, uint64_t dst_offset,
		unsigned num_gpu_pages,
		struct radeon_fence **fence)
{
	struct radeon_semaphore *sem = NULL;
	int ring_index = rdev->asic->copy.dma_ring_index;
	struct radeon_ring *ring = &rdev->ring[ring_index];
	u32 size_in_bytes, cur_size_in_bytes;
	int i, num_loops;
	int r = 0;

	r = radeon_semaphore_create(rdev, &sem);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		return r;
	}

	size_in_bytes = (num_gpu_pages << RADEON_GPU_PAGE_SHIFT);
	num_loops = DIV_ROUND_UP(size_in_bytes, 0xfffff);
	r = radeon_ring_lock(rdev, ring, num_loops * 5 + 11);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		radeon_semaphore_free(rdev, &sem, NULL);
		return r;
	}

	radeon_semaphore_sync_to(sem, *fence);
	radeon_semaphore_sync_rings(rdev, sem, ring->idx);

	for (i = 0; i < num_loops; i++) {
		cur_size_in_bytes = size_in_bytes;
		if (cur_size_in_bytes > 0xFFFFF)
			cur_size_in_bytes = 0xFFFFF;
		size_in_bytes -= cur_size_in_bytes;
		radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_COPY, 1, 0, 0, cur_size_in_bytes));
		radeon_ring_write(ring, lower_32_bits(dst_offset));
		radeon_ring_write(ring, lower_32_bits(src_offset));
		radeon_ring_write(ring, upper_32_bits(dst_offset) & 0xff);
		radeon_ring_write(ring, upper_32_bits(src_offset) & 0xff);
		src_offset += cur_size_in_bytes;
		dst_offset += cur_size_in_bytes;
	}

	r = radeon_fence_emit(rdev, fence, ring->idx);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		radeon_semaphore_free(rdev, &sem, NULL);
		return r;
	}

	radeon_ring_unlock_commit(rdev, ring);
	radeon_semaphore_free(rdev, &sem, *fence);

	return r;
}

