/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 *          Christian König
 */
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon.h"
#include "atom.h"

/*
 * IB
 * IBs (Indirect Buffers) and areas of GPU accessible memory where
 * commands are stored.  You can put a pointer to the IB in the
 * command ring and the hw will fetch the commands from the IB
 * and execute them.  Generally userspace acceleration drivers
 * produce command buffers which are send to the kernel and
 * put in IBs for execution by the requested ring.
 */
static int radeon_debugfs_sa_init(struct radeon_device *rdev);

/**
 * radeon_ib_get - request an IB (Indirect Buffer)
 *
 * @rdev: radeon_device pointer
 * @ring: ring index the IB is associated with
 * @ib: IB object returned
 * @size: requested IB size
 *
 * Request an IB (all asics).  IBs are allocated using the
 * suballocator.
 * Returns 0 on success, error on failure.
 */
int radeon_ib_get(struct radeon_device *rdev, int ring,
		  struct radeon_ib *ib, struct radeon_vm *vm,
		  unsigned size)
{
	int i, r;

	r = radeon_sa_bo_new(rdev, &rdev->ring_tmp_bo, &ib->sa_bo, size, 256, true);
	if (r) {
		dev_err(rdev->dev, "failed to get a new IB (%d)\n", r);
		return r;
	}

	r = radeon_semaphore_create(rdev, &ib->semaphore);
	if (r) {
		return r;
	}

	ib->ring = ring;
	ib->fence = NULL;
	ib->ptr = radeon_sa_bo_cpu_addr(ib->sa_bo);
	ib->vm = vm;
	if (vm) {
		/* ib pool is bound at RADEON_VA_IB_OFFSET in virtual address
		 * space and soffset is the offset inside the pool bo
		 */
		ib->gpu_addr = ib->sa_bo->soffset + RADEON_VA_IB_OFFSET;
	} else {
		ib->gpu_addr = radeon_sa_bo_gpu_addr(ib->sa_bo);
	}
	ib->is_const_ib = false;
	for (i = 0; i < RADEON_NUM_RINGS; ++i)
		ib->sync_to[i] = NULL;

	return 0;
}

/**
 * radeon_ib_free - free an IB (Indirect Buffer)
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to free
 *
 * Free an IB (all asics).
 */
void radeon_ib_free(struct radeon_device *rdev, struct radeon_ib *ib)
{
	radeon_semaphore_free(rdev, &ib->semaphore, ib->fence);
	radeon_sa_bo_free(rdev, &ib->sa_bo, ib->fence);
	radeon_fence_unref(&ib->fence);
}

/**
 * radeon_ib_sync_to - sync to fence before executing the IB
 *
 * @ib: IB object to add fence to
 * @fence: fence to sync to
 *
 * Sync to the fence before executing the IB
 */
void radeon_ib_sync_to(struct radeon_ib *ib, struct radeon_fence *fence)
{
	struct radeon_fence *other;

	if (!fence)
		return;

	other = ib->sync_to[fence->ring];
	ib->sync_to[fence->ring] = radeon_fence_later(fence, other);
}

/**
 * radeon_ib_schedule - schedule an IB (Indirect Buffer) on the ring
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to schedule
 * @const_ib: Const IB to schedule (SI only)
 *
 * Schedule an IB on the associated ring (all asics).
 * Returns 0 on success, error on failure.
 *
 * On SI, there are two parallel engines fed from the primary ring,
 * the CE (Constant Engine) and the DE (Drawing Engine).  Since
 * resource descriptors have moved to memory, the CE allows you to
 * prime the caches while the DE is updating register state so that
 * the resource descriptors will be already in cache when the draw is
 * processed.  To accomplish this, the userspace driver submits two
 * IBs, one for the CE and one for the DE.  If there is a CE IB (called
 * a CONST_IB), it will be put on the ring prior to the DE IB.  Prior
 * to SI there was just a DE IB.
 */
int radeon_ib_schedule(struct radeon_device *rdev, struct radeon_ib *ib,
		       struct radeon_ib *const_ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	bool need_sync = false;
	int i, r = 0;

	if (!ib->length_dw || !ring->ready) {
		/* TODO: Nothings in the ib we should report. */
		dev_err(rdev->dev, "couldn't schedule ib\n");
		return -EINVAL;
	}

	/* 64 dwords should be enough for fence too */
	r = radeon_ring_lock(rdev, ring, 64 + RADEON_NUM_RINGS * 8);
	if (r) {
		dev_err(rdev->dev, "scheduling IB failed (%d).\n", r);
		return r;
	}
	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		struct radeon_fence *fence = ib->sync_to[i];
		if (radeon_fence_need_sync(fence, ib->ring)) {
			need_sync = true;
			radeon_semaphore_sync_rings(rdev, ib->semaphore,
						    fence->ring, ib->ring);
			radeon_fence_note_sync(fence, ib->ring);
		}
	}
	/* immediately free semaphore when we don't need to sync */
	if (!need_sync) {
		radeon_semaphore_free(rdev, &ib->semaphore, NULL);
	}
	/* if we can't remember our last VM flush then flush now! */
	/* XXX figure out why we have to flush for every IB */
	if (ib->vm /*&& !ib->vm->last_flush*/) {
		radeon_ring_vm_flush(rdev, ib->ring, ib->vm);
	}
	if (const_ib) {
		radeon_ring_ib_execute(rdev, const_ib->ring, const_ib);
		radeon_semaphore_free(rdev, &const_ib->semaphore, NULL);
	}
	radeon_ring_ib_execute(rdev, ib->ring, ib);
	r = radeon_fence_emit(rdev, &ib->fence, ib->ring);
	if (r) {
		dev_err(rdev->dev, "failed to emit fence for new IB (%d)\n", r);
		radeon_ring_unlock_undo(rdev, ring);
		return r;
	}
	if (const_ib) {
		const_ib->fence = radeon_fence_ref(ib->fence);
	}
	/* we just flushed the VM, remember that */
	if (ib->vm && !ib->vm->last_flush) {
		ib->vm->last_flush = radeon_fence_ref(ib->fence);
	}
	radeon_ring_unlock_commit(rdev, ring);
	return 0;
}

/**
 * radeon_ib_pool_init - Init the IB (Indirect Buffer) pool
 *
 * @rdev: radeon_device pointer
 *
 * Initialize the suballocator to manage a pool of memory
 * for use as IBs (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ib_pool_init(struct radeon_device *rdev)
{
	int r;

	if (rdev->ib_pool_ready) {
		return 0;
	}
	r = radeon_sa_bo_manager_init(rdev, &rdev->ring_tmp_bo,
				      RADEON_IB_POOL_SIZE*64*1024,
				      RADEON_GPU_PAGE_SIZE,
				      RADEON_GEM_DOMAIN_GTT);
	if (r) {
		return r;
	}

	r = radeon_sa_bo_manager_start(rdev, &rdev->ring_tmp_bo);
	if (r) {
		return r;
	}

	rdev->ib_pool_ready = true;
	if (radeon_debugfs_sa_init(rdev)) {
		dev_err(rdev->dev, "failed to register debugfs file for SA\n");
	}
	return 0;
}

/**
 * radeon_ib_pool_fini - Free the IB (Indirect Buffer) pool
 *
 * @rdev: radeon_device pointer
 *
 * Tear down the suballocator managing the pool of memory
 * for use as IBs (all asics).
 */
void radeon_ib_pool_fini(struct radeon_device *rdev)
{
	if (rdev->ib_pool_ready) {
		radeon_sa_bo_manager_suspend(rdev, &rdev->ring_tmp_bo);
		radeon_sa_bo_manager_fini(rdev, &rdev->ring_tmp_bo);
		rdev->ib_pool_ready = false;
	}
}

/**
 * radeon_ib_ring_tests - test IBs on the rings
 *
 * @rdev: radeon_device pointer
 *
 * Test an IB (Indirect Buffer) on each ring.
 * If the test fails, disable the ring.
 * Returns 0 on success, error if the primary GFX ring
 * IB test fails.
 */
int radeon_ib_ring_tests(struct radeon_device *rdev)
{
	unsigned i;
	int r;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		struct radeon_ring *ring = &rdev->ring[i];

		if (!ring->ready)
			continue;

		r = radeon_ib_test(rdev, i, ring);
		if (r) {
			ring->ready = false;

			if (i == RADEON_RING_TYPE_GFX_INDEX) {
				/* oh, oh, that's really bad */
				DRM_ERROR("radeon: failed testing IB on GFX ring (%d).\n", r);
		                rdev->accel_working = false;
				return r;

			} else {
				/* still not good, but we can live with it */
				DRM_ERROR("radeon: failed testing IB on ring %d (%d).\n", i, r);
			}
		}
	}
	return 0;
}

/*
 * Rings
 * Most engines on the GPU are fed via ring buffers.  Ring
 * buffers are areas of GPU accessible memory that the host
 * writes commands into and the GPU reads commands out of.
 * There is a rptr (read pointer) that determines where the
 * GPU is currently reading, and a wptr (write pointer)
 * which determines where the host has written.  When the
 * pointers are equal, the ring is idle.  When the host
 * writes commands to the ring buffer, it increments the
 * wptr.  The GPU then starts fetching commands and executes
 * them until the pointers are equal again.
 */
static int radeon_debugfs_ring_init(struct radeon_device *rdev, struct radeon_ring *ring);

/**
 * radeon_ring_write - write a value to the ring
 *
 * @ring: radeon_ring structure holding ring information
 * @v: dword (dw) value to write
 *
 * Write a value to the requested ring buffer (all asics).
 */
void radeon_ring_write(struct radeon_ring *ring, uint32_t v)
{
#if DRM_DEBUG_CODE
	if (ring->count_dw <= 0) {
		DRM_ERROR("radeon: writing more dwords to the ring than expected!\n");
	}
#endif
	ring->ring[ring->wptr++] = v;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw--;
	ring->ring_free_dw--;
}

/**
 * radeon_ring_supports_scratch_reg - check if the ring supports
 * writing to scratch registers
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if a specific ring supports writing to scratch registers (all asics).
 * Returns true if the ring supports writing to scratch regs, false if not.
 */
bool radeon_ring_supports_scratch_reg(struct radeon_device *rdev,
				      struct radeon_ring *ring)
{
	switch (ring->idx) {
	case RADEON_RING_TYPE_GFX_INDEX:
	case CAYMAN_RING_TYPE_CP1_INDEX:
	case CAYMAN_RING_TYPE_CP2_INDEX:
		return true;
	default:
		return false;
	}
}

/**
 * radeon_ring_free_size - update the free size
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Update the free dw slots in the ring buffer (all asics).
 */
void radeon_ring_free_size(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 rptr;

	if (rdev->wb.enabled && ring != &rdev->ring[R600_RING_TYPE_UVD_INDEX])
		rptr = le32_to_cpu(rdev->wb.wb[ring->rptr_offs/4]);
	else
		rptr = RREG32(ring->rptr_reg);
	ring->rptr = (rptr & ring->ptr_reg_mask) >> ring->ptr_reg_shift;
	/* This works because ring_size is a power of 2 */
	ring->ring_free_dw = (ring->rptr + (ring->ring_size / 4));
	ring->ring_free_dw -= ring->wptr;
	ring->ring_free_dw &= ring->ptr_mask;
	if (!ring->ring_free_dw) {
		ring->ring_free_dw = ring->ring_size / 4;
	}
}

/**
 * radeon_ring_alloc - allocate space on the ring buffer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @ndw: number of dwords to allocate in the ring buffer
 *
 * Allocate @ndw dwords in the ring buffer (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ring_alloc(struct radeon_device *rdev, struct radeon_ring *ring, unsigned ndw)
{
	int r;

	/* make sure we aren't trying to allocate more space than there is on the ring */
	if (ndw > (ring->ring_size / 4))
		return -ENOMEM;
	/* Align requested size with padding so unlock_commit can
	 * pad safely */
	radeon_ring_free_size(rdev, ring);
	if (ring->ring_free_dw == (ring->ring_size / 4)) {
		/* This is an empty ring update lockup info to avoid
		 * false positive.
		 */
		radeon_ring_lockup_update(ring);
	}
	ndw = (ndw + ring->align_mask) & ~ring->align_mask;
	while (ndw > (ring->ring_free_dw - 1)) {
		radeon_ring_free_size(rdev, ring);
		if (ndw < ring->ring_free_dw) {
			break;
		}
		r = radeon_fence_wait_next_locked(rdev, ring->idx);
		if (r)
			return r;
	}
	ring->count_dw = ndw;
	ring->wptr_old = ring->wptr;
	return 0;
}

/**
 * radeon_ring_lock - lock the ring and allocate space on it
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @ndw: number of dwords to allocate in the ring buffer
 *
 * Lock the ring and allocate @ndw dwords in the ring buffer
 * (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ring_lock(struct radeon_device *rdev, struct radeon_ring *ring, unsigned ndw)
{
	int r;

	mutex_lock(&rdev->ring_lock);
	r = radeon_ring_alloc(rdev, ring, ndw);
	if (r) {
		mutex_unlock(&rdev->ring_lock);
		return r;
	}
	return 0;
}

/**
 * radeon_ring_commit - tell the GPU to execute the new
 * commands on the ring buffer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Update the wptr (write pointer) to tell the GPU to
 * execute new commands on the ring buffer (all asics).
 */
void radeon_ring_commit(struct radeon_device *rdev, struct radeon_ring *ring)
{
	/* We pad to match fetch size */
	while (ring->wptr & ring->align_mask) {
		radeon_ring_write(ring, ring->nop);
	}
	DRM_MEMORYBARRIER();
	WREG32(ring->wptr_reg, (ring->wptr << ring->ptr_reg_shift) & ring->ptr_reg_mask);
	(void)RREG32(ring->wptr_reg);
}

/**
 * radeon_ring_unlock_commit - tell the GPU to execute the new
 * commands on the ring buffer and unlock it
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Call radeon_ring_commit() then unlock the ring (all asics).
 */
void radeon_ring_unlock_commit(struct radeon_device *rdev, struct radeon_ring *ring)
{
	radeon_ring_commit(rdev, ring);
	mutex_unlock(&rdev->ring_lock);
}

/**
 * radeon_ring_undo - reset the wptr
 *
 * @ring: radeon_ring structure holding ring information
 *
 * Reset the driver's copy of the wptr (all asics).
 */
void radeon_ring_undo(struct radeon_ring *ring)
{
	ring->wptr = ring->wptr_old;
}

/**
 * radeon_ring_unlock_undo - reset the wptr and unlock the ring
 *
 * @ring: radeon_ring structure holding ring information
 *
 * Call radeon_ring_undo() then unlock the ring (all asics).
 */
void radeon_ring_unlock_undo(struct radeon_device *rdev, struct radeon_ring *ring)
{
	radeon_ring_undo(ring);
	mutex_unlock(&rdev->ring_lock);
}

/**
 * radeon_ring_force_activity - add some nop packets to the ring
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Add some nop packets to the ring to force activity (all asics).
 * Used for lockup detection to see if the rptr is advancing.
 */
void radeon_ring_force_activity(struct radeon_device *rdev, struct radeon_ring *ring)
{
	int r;

	radeon_ring_free_size(rdev, ring);
	if (ring->rptr == ring->wptr) {
		r = radeon_ring_alloc(rdev, ring, 1);
		if (!r) {
			radeon_ring_write(ring, ring->nop);
			radeon_ring_commit(rdev, ring);
		}
	}
}

/**
 * radeon_ring_lockup_update - update lockup variables
 *
 * @ring: radeon_ring structure holding ring information
 *
 * Update the last rptr value and timestamp (all asics).
 */
void radeon_ring_lockup_update(struct radeon_ring *ring)
{
	ring->last_rptr = ring->rptr;
	ring->last_activity = jiffies;
}

/**
 * radeon_ring_test_lockup() - check if ring is lockedup by recording information
 * @rdev:       radeon device structure
 * @ring:       radeon_ring structure holding ring information
 *
 * We don't need to initialize the lockup tracking information as we will either
 * have CP rptr to a different value of jiffies wrap around which will force
 * initialization of the lockup tracking informations.
 *
 * A possible false positivie is if we get call after while and last_cp_rptr ==
 * the current CP rptr, even if it's unlikely it might happen. To avoid this
 * if the elapsed time since last call is bigger than 2 second than we return
 * false and update the tracking information. Due to this the caller must call
 * radeon_ring_test_lockup several time in less than 2sec for lockup to be reported
 * the fencing code should be cautious about that.
 *
 * Caller should write to the ring to force CP to do something so we don't get
 * false positive when CP is just gived nothing to do.
 *
 **/
bool radeon_ring_test_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	unsigned long cjiffies, elapsed;
	uint32_t rptr;

	cjiffies = jiffies;
	if (!time_after(cjiffies, ring->last_activity)) {
		/* likely a wrap around */
		radeon_ring_lockup_update(ring);
		return false;
	}
	rptr = RREG32(ring->rptr_reg);
	ring->rptr = (rptr & ring->ptr_reg_mask) >> ring->ptr_reg_shift;
	if (ring->rptr != ring->last_rptr) {
		/* CP is still working no lockup */
		radeon_ring_lockup_update(ring);
		return false;
	}
	elapsed = jiffies_to_msecs(cjiffies - ring->last_activity);
	if (radeon_lockup_timeout && elapsed >= radeon_lockup_timeout) {
		dev_err(rdev->dev, "GPU lockup CP stall for more than %lumsec\n", elapsed);
		return true;
	}
	/* give a chance to the GPU ... */
	return false;
}

/**
 * radeon_ring_backup - Back up the content of a ring
 *
 * @rdev: radeon_device pointer
 * @ring: the ring we want to back up
 *
 * Saves all unprocessed commits from a ring, returns the number of dwords saved.
 */
unsigned radeon_ring_backup(struct radeon_device *rdev, struct radeon_ring *ring,
			    uint32_t **data)
{
	unsigned size, ptr, i;

	/* just in case lock the ring */
	mutex_lock(&rdev->ring_lock);
	*data = NULL;

	if (ring->ring_obj == NULL) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	/* it doesn't make sense to save anything if all fences are signaled */
	if (!radeon_fence_count_emitted(rdev, ring->idx)) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	/* calculate the number of dw on the ring */
	if (ring->rptr_save_reg)
		ptr = RREG32(ring->rptr_save_reg);
	else if (rdev->wb.enabled)
		ptr = le32_to_cpu(*ring->next_rptr_cpu_addr);
	else {
		/* no way to read back the next rptr */
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	size = ring->wptr + (ring->ring_size / 4);
	size -= ptr;
	size &= ring->ptr_mask;
	if (size == 0) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}

	/* and then save the content of the ring */
	*data = kmalloc_array(size, sizeof(uint32_t), GFP_KERNEL);
	if (!*data) {
		mutex_unlock(&rdev->ring_lock);
		return 0;
	}
	for (i = 0; i < size; ++i) {
		(*data)[i] = ring->ring[ptr++];
		ptr &= ring->ptr_mask;
	}

	mutex_unlock(&rdev->ring_lock);
	return size;
}

/**
 * radeon_ring_restore - append saved commands to the ring again
 *
 * @rdev: radeon_device pointer
 * @ring: ring to append commands to
 * @size: number of dwords we want to write
 * @data: saved commands
 *
 * Allocates space on the ring and restore the previously saved commands.
 */
int radeon_ring_restore(struct radeon_device *rdev, struct radeon_ring *ring,
			unsigned size, uint32_t *data)
{
	int i, r;

	if (!size || !data)
		return 0;

	/* restore the saved ring content */
	r = radeon_ring_lock(rdev, ring, size);
	if (r)
		return r;

	for (i = 0; i < size; ++i) {
		radeon_ring_write(ring, data[i]);
	}

	radeon_ring_unlock_commit(rdev, ring);
	kfree(data);
	return 0;
}

/**
 * radeon_ring_init - init driver ring struct.
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @ring_size: size of the ring
 * @rptr_offs: offset of the rptr writeback location in the WB buffer
 * @rptr_reg: MMIO offset of the rptr register
 * @wptr_reg: MMIO offset of the wptr register
 * @ptr_reg_shift: bit offset of the rptr/wptr values
 * @ptr_reg_mask: bit mask of the rptr/wptr values
 * @nop: nop packet for this ring
 *
 * Initialize the driver information for the selected ring (all asics).
 * Returns 0 on success, error on failure.
 */
int radeon_ring_init(struct radeon_device *rdev, struct radeon_ring *ring, unsigned ring_size,
		     unsigned rptr_offs, unsigned rptr_reg, unsigned wptr_reg,
		     u32 ptr_reg_shift, u32 ptr_reg_mask, u32 nop)
{
	int r;

	ring->ring_size = ring_size;
	ring->rptr_offs = rptr_offs;
	ring->rptr_reg = rptr_reg;
	ring->wptr_reg = wptr_reg;
	ring->ptr_reg_shift = ptr_reg_shift;
	ring->ptr_reg_mask = ptr_reg_mask;
	ring->nop = nop;
	/* Allocate ring buffer */
	if (ring->ring_obj == NULL) {
		r = radeon_bo_create(rdev, ring->ring_size, PAGE_SIZE, true,
				     RADEON_GEM_DOMAIN_GTT,
				     NULL, &ring->ring_obj);
		if (r) {
			dev_err(rdev->dev, "(%d) ring create failed\n", r);
			return r;
		}
		r = radeon_bo_reserve(ring->ring_obj, false);
		if (unlikely(r != 0))
			return r;
		r = radeon_bo_pin(ring->ring_obj, RADEON_GEM_DOMAIN_GTT,
					&ring->gpu_addr);
		if (r) {
			radeon_bo_unreserve(ring->ring_obj);
			dev_err(rdev->dev, "(%d) ring pin failed\n", r);
			return r;
		}
		r = radeon_bo_kmap(ring->ring_obj,
				       (void **)&ring->ring);
		radeon_bo_unreserve(ring->ring_obj);
		if (r) {
			dev_err(rdev->dev, "(%d) ring map failed\n", r);
			return r;
		}
	}
	ring->ptr_mask = (ring->ring_size / 4) - 1;
	ring->ring_free_dw = ring->ring_size / 4;
	if (rdev->wb.enabled) {
		u32 index = RADEON_WB_RING0_NEXT_RPTR + (ring->idx * 4);
		ring->next_rptr_gpu_addr = rdev->wb.gpu_addr + index;
		ring->next_rptr_cpu_addr = &rdev->wb.wb[index/4];
	}
	if (radeon_debugfs_ring_init(rdev, ring)) {
		DRM_ERROR("Failed to register debugfs file for rings !\n");
	}
	radeon_ring_lockup_update(ring);
	return 0;
}

/**
 * radeon_ring_fini - tear down the driver ring struct.
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Tear down the driver information for the selected ring (all asics).
 */
void radeon_ring_fini(struct radeon_device *rdev, struct radeon_ring *ring)
{
	int r;
	struct radeon_bo *ring_obj;

	mutex_lock(&rdev->ring_lock);
	ring_obj = ring->ring_obj;
	ring->ready = false;
	ring->ring = NULL;
	ring->ring_obj = NULL;
	mutex_unlock(&rdev->ring_lock);

	if (ring_obj) {
		r = radeon_bo_reserve(ring_obj, false);
		if (likely(r == 0)) {
			radeon_bo_kunmap(ring_obj);
			radeon_bo_unpin(ring_obj);
			radeon_bo_unreserve(ring_obj);
		}
		radeon_bo_unref(&ring_obj);
	}
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int radeon_debugfs_ring_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;
	int ridx = *(int*)node->info_ent->data;
	struct radeon_ring *ring = &rdev->ring[ridx];
	unsigned count, i, j;
	u32 tmp;

	radeon_ring_free_size(rdev, ring);
	count = (ring->ring_size / 4) - ring->ring_free_dw;
	tmp = RREG32(ring->wptr_reg) >> ring->ptr_reg_shift;
	seq_printf(m, "wptr(0x%04x): 0x%08x [%5d]\n", ring->wptr_reg, tmp, tmp);
	tmp = RREG32(ring->rptr_reg) >> ring->ptr_reg_shift;
	seq_printf(m, "rptr(0x%04x): 0x%08x [%5d]\n", ring->rptr_reg, tmp, tmp);
	if (ring->rptr_save_reg) {
		seq_printf(m, "rptr next(0x%04x): 0x%08x\n", ring->rptr_save_reg,
			   RREG32(ring->rptr_save_reg));
	}
	seq_printf(m, "driver's copy of the wptr: 0x%08x [%5d]\n", ring->wptr, ring->wptr);
	seq_printf(m, "driver's copy of the rptr: 0x%08x [%5d]\n", ring->rptr, ring->rptr);
	seq_printf(m, "last semaphore signal addr : 0x%016llx\n", ring->last_semaphore_signal_addr);
	seq_printf(m, "last semaphore wait addr   : 0x%016llx\n", ring->last_semaphore_wait_addr);
	seq_printf(m, "%u free dwords in ring\n", ring->ring_free_dw);
	seq_printf(m, "%u dwords in ring\n", count);
	/* print 8 dw before current rptr as often it's the last executed
	 * packet that is the root issue
	 */
	i = (ring->rptr + ring->ptr_mask + 1 - 32) & ring->ptr_mask;
	for (j = 0; j <= (count + 32); j++) {
		seq_printf(m, "r[%5d]=0x%08x\n", i, ring->ring[i]);
		i = (i + 1) & ring->ptr_mask;
	}
	return 0;
}

static int radeon_gfx_index = RADEON_RING_TYPE_GFX_INDEX;
static int cayman_cp1_index = CAYMAN_RING_TYPE_CP1_INDEX;
static int cayman_cp2_index = CAYMAN_RING_TYPE_CP2_INDEX;
static int radeon_dma1_index = R600_RING_TYPE_DMA_INDEX;
static int radeon_dma2_index = CAYMAN_RING_TYPE_DMA1_INDEX;
static int r600_uvd_index = R600_RING_TYPE_UVD_INDEX;

static struct drm_info_list radeon_debugfs_ring_info_list[] = {
	{"radeon_ring_gfx", radeon_debugfs_ring_info, 0, &radeon_gfx_index},
	{"radeon_ring_cp1", radeon_debugfs_ring_info, 0, &cayman_cp1_index},
	{"radeon_ring_cp2", radeon_debugfs_ring_info, 0, &cayman_cp2_index},
	{"radeon_ring_dma1", radeon_debugfs_ring_info, 0, &radeon_dma1_index},
	{"radeon_ring_dma2", radeon_debugfs_ring_info, 0, &radeon_dma2_index},
	{"radeon_ring_uvd", radeon_debugfs_ring_info, 0, &r600_uvd_index},
};

static int radeon_debugfs_sa_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;

	radeon_sa_bo_dump_debug_info(&rdev->ring_tmp_bo, m);

	return 0;

}

static struct drm_info_list radeon_debugfs_sa_list[] = {
        {"radeon_sa_info", &radeon_debugfs_sa_info, 0, NULL},
};

#endif

static int radeon_debugfs_ring_init(struct radeon_device *rdev, struct radeon_ring *ring)
{
#if defined(CONFIG_DEBUG_FS)
	unsigned i;
	for (i = 0; i < ARRAY_SIZE(radeon_debugfs_ring_info_list); ++i) {
		struct drm_info_list *info = &radeon_debugfs_ring_info_list[i];
		int ridx = *(int*)radeon_debugfs_ring_info_list[i].data;
		unsigned r;

		if (&rdev->ring[ridx] != ring)
			continue;

		r = radeon_debugfs_add_files(rdev, info, 1);
		if (r)
			return r;
	}
#endif
	return 0;
}

static int radeon_debugfs_sa_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, radeon_debugfs_sa_list, 1);
#else
	return 0;
#endif
}
