/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_ih.h"
#include "amdgpu_amdkfd.h"

/**
 * amdgpu_ih_ring_alloc - allocate memory for the IH ring
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate a ring buffer for the interrupt controller.
 * Returns 0 for success, errors for failure.
 */
static int amdgpu_ih_ring_alloc(struct amdgpu_device *adev)
{
	int r;

	/* Allocate ring buffer */
	if (adev->irq.ih.ring_obj == NULL) {
		r = amdgpu_bo_create_kernel(adev, adev->irq.ih.ring_size,
					    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
					    &adev->irq.ih.ring_obj,
					    &adev->irq.ih.gpu_addr,
					    (void **)&adev->irq.ih.ring);
		if (r) {
			DRM_ERROR("amdgpu: failed to create ih ring buffer (%d).\n", r);
			return r;
		}
	}
	return 0;
}

/**
 * amdgpu_ih_ring_init - initialize the IH state
 *
 * @adev: amdgpu_device pointer
 *
 * Initializes the IH state and allocates a buffer
 * for the IH ring buffer.
 * Returns 0 for success, errors for failure.
 */
int amdgpu_ih_ring_init(struct amdgpu_device *adev, unsigned ring_size,
			bool use_bus_addr)
{
	u32 rb_bufsz;
	int r;

	/* Align ring size */
	rb_bufsz = order_base_2(ring_size / 4);
	ring_size = (1 << rb_bufsz) * 4;
	adev->irq.ih.ring_size = ring_size;
	adev->irq.ih.ptr_mask = adev->irq.ih.ring_size - 1;
	adev->irq.ih.rptr = 0;
	adev->irq.ih.use_bus_addr = use_bus_addr;

	if (adev->irq.ih.use_bus_addr) {
		if (!adev->irq.ih.ring) {
			/* add 8 bytes for the rptr/wptr shadows and
			 * add them to the end of the ring allocation.
			 */
			adev->irq.ih.ring = pci_alloc_consistent(adev->pdev,
								 adev->irq.ih.ring_size + 8,
								 &adev->irq.ih.rb_dma_addr);
			if (adev->irq.ih.ring == NULL)
				return -ENOMEM;
			memset((void *)adev->irq.ih.ring, 0, adev->irq.ih.ring_size + 8);
			adev->irq.ih.wptr_offs = (adev->irq.ih.ring_size / 4) + 0;
			adev->irq.ih.rptr_offs = (adev->irq.ih.ring_size / 4) + 1;
		}
		return 0;
	} else {
		r = amdgpu_device_wb_get(adev, &adev->irq.ih.wptr_offs);
		if (r) {
			dev_err(adev->dev, "(%d) ih wptr_offs wb alloc failed\n", r);
			return r;
		}

		r = amdgpu_device_wb_get(adev, &adev->irq.ih.rptr_offs);
		if (r) {
			amdgpu_device_wb_free(adev, adev->irq.ih.wptr_offs);
			dev_err(adev->dev, "(%d) ih rptr_offs wb alloc failed\n", r);
			return r;
		}

		return amdgpu_ih_ring_alloc(adev);
	}
}

/**
 * amdgpu_ih_ring_fini - tear down the IH state
 *
 * @adev: amdgpu_device pointer
 *
 * Tears down the IH state and frees buffer
 * used for the IH ring buffer.
 */
void amdgpu_ih_ring_fini(struct amdgpu_device *adev)
{
	if (adev->irq.ih.use_bus_addr) {
		if (adev->irq.ih.ring) {
			/* add 8 bytes for the rptr/wptr shadows and
			 * add them to the end of the ring allocation.
			 */
			pci_free_consistent(adev->pdev, adev->irq.ih.ring_size + 8,
					    (void *)adev->irq.ih.ring,
					    adev->irq.ih.rb_dma_addr);
			adev->irq.ih.ring = NULL;
		}
	} else {
		amdgpu_bo_free_kernel(&adev->irq.ih.ring_obj,
				      &adev->irq.ih.gpu_addr,
				      (void **)&adev->irq.ih.ring);
		amdgpu_device_wb_free(adev, adev->irq.ih.wptr_offs);
		amdgpu_device_wb_free(adev, adev->irq.ih.rptr_offs);
	}
}

/**
 * amdgpu_ih_process - interrupt handler
 *
 * @adev: amdgpu_device pointer
 *
 * Interrupt hander (VI), walk the IH ring.
 * Returns irq process return code.
 */
int amdgpu_ih_process(struct amdgpu_device *adev)
{
	struct amdgpu_iv_entry entry;
	u32 wptr;

	if (!adev->irq.ih.enabled || adev->shutdown)
		return IRQ_NONE;

	wptr = amdgpu_ih_get_wptr(adev);

restart_ih:
	/* is somebody else already processing irqs? */
	if (atomic_xchg(&adev->irq.ih.lock, 1))
		return IRQ_NONE;

	DRM_DEBUG("%s: rptr %d, wptr %d\n", __func__, adev->irq.ih.rptr, wptr);

	/* Order reading of wptr vs. reading of IH ring data */
	rmb();

	while (adev->irq.ih.rptr != wptr) {
		u32 ring_index = adev->irq.ih.rptr >> 2;

		/* Prescreening of high-frequency interrupts */
		if (!amdgpu_ih_prescreen_iv(adev)) {
			adev->irq.ih.rptr &= adev->irq.ih.ptr_mask;
			continue;
		}

		/* Before dispatching irq to IP blocks, send it to amdkfd */
		amdgpu_amdkfd_interrupt(adev,
				(const void *) &adev->irq.ih.ring[ring_index]);

		entry.iv_entry = (const uint32_t *)
			&adev->irq.ih.ring[ring_index];
		amdgpu_ih_decode_iv(adev, &entry);
		adev->irq.ih.rptr &= adev->irq.ih.ptr_mask;

		amdgpu_irq_dispatch(adev, &entry);
	}
	amdgpu_ih_set_rptr(adev);
	atomic_set(&adev->irq.ih.lock, 0);

	/* make sure wptr hasn't changed while processing */
	wptr = amdgpu_ih_get_wptr(adev);
	if (wptr != adev->irq.ih.rptr)
		goto restart_ih;

	return IRQ_HANDLED;
}

/**
 * amdgpu_ih_add_fault - Add a page fault record
 *
 * @adev: amdgpu device pointer
 * @key: 64-bit encoding of PASID and address
 *
 * This should be called when a retry page fault interrupt is
 * received. If this is a new page fault, it will be added to a hash
 * table. The return value indicates whether this is a new fault, or
 * a fault that was already known and is already being handled.
 *
 * If there are too many pending page faults, this will fail. Retry
 * interrupts should be ignored in this case until there is enough
 * free space.
 *
 * Returns 0 if the fault was added, 1 if the fault was already known,
 * -ENOSPC if there are too many pending faults.
 */
int amdgpu_ih_add_fault(struct amdgpu_device *adev, u64 key)
{
	unsigned long flags;
	int r = -ENOSPC;

	if (WARN_ON_ONCE(!adev->irq.ih.faults))
		/* Should be allocated in <IP>_ih_sw_init on GPUs that
		 * support retry faults and require retry filtering.
		 */
		return r;

	spin_lock_irqsave(&adev->irq.ih.faults->lock, flags);

	/* Only let the hash table fill up to 50% for best performance */
	if (adev->irq.ih.faults->count >= (1 << (AMDGPU_PAGEFAULT_HASH_BITS-1)))
		goto unlock_out;

	r = chash_table_copy_in(&adev->irq.ih.faults->hash, key, NULL);
	if (!r)
		adev->irq.ih.faults->count++;

	/* chash_table_copy_in should never fail unless we're losing count */
	WARN_ON_ONCE(r < 0);

unlock_out:
	spin_unlock_irqrestore(&adev->irq.ih.faults->lock, flags);
	return r;
}

/**
 * amdgpu_ih_clear_fault - Remove a page fault record
 *
 * @adev: amdgpu device pointer
 * @key: 64-bit encoding of PASID and address
 *
 * This should be called when a page fault has been handled. Any
 * future interrupt with this key will be processed as a new
 * page fault.
 */
void amdgpu_ih_clear_fault(struct amdgpu_device *adev, u64 key)
{
	unsigned long flags;
	int r;

	if (!adev->irq.ih.faults)
		return;

	spin_lock_irqsave(&adev->irq.ih.faults->lock, flags);

	r = chash_table_remove(&adev->irq.ih.faults->hash, key, NULL);
	if (!WARN_ON_ONCE(r < 0)) {
		adev->irq.ih.faults->count--;
		WARN_ON_ONCE(adev->irq.ih.faults->count < 0);
	}

	spin_unlock_irqrestore(&adev->irq.ih.faults->lock, flags);
}
