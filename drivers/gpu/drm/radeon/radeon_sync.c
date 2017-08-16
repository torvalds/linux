/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */
/*
 * Authors:
 *    Christian König <christian.koenig@amd.com>
 */

#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_trace.h"

/**
 * radeon_sync_create - zero init sync object
 *
 * @sync: sync object to initialize
 *
 * Just clear the sync object for now.
 */
void radeon_sync_create(struct radeon_sync *sync)
{
	unsigned i;

	for (i = 0; i < RADEON_NUM_SYNCS; ++i)
		sync->semaphores[i] = NULL;

	for (i = 0; i < RADEON_NUM_RINGS; ++i)
		sync->sync_to[i] = NULL;

	sync->last_vm_update = NULL;
}

/**
 * radeon_sync_fence - use the semaphore to sync to a fence
 *
 * @sync: sync object to add fence to
 * @fence: fence to sync to
 *
 * Sync to the fence using the semaphore objects
 */
void radeon_sync_fence(struct radeon_sync *sync,
		       struct radeon_fence *fence)
{
	struct radeon_fence *other;

	if (!fence)
		return;

	other = sync->sync_to[fence->ring];
	sync->sync_to[fence->ring] = radeon_fence_later(fence, other);

	if (fence->is_vm_update) {
		other = sync->last_vm_update;
		sync->last_vm_update = radeon_fence_later(fence, other);
	}
}

/**
 * radeon_sync_resv - use the semaphores to sync to a reservation object
 *
 * @sync: sync object to add fences from reservation object to
 * @resv: reservation object with embedded fence
 * @shared: true if we should only sync to the exclusive fence
 *
 * Sync to the fence using the semaphore objects
 */
int radeon_sync_resv(struct radeon_device *rdev,
		     struct radeon_sync *sync,
		     struct reservation_object *resv,
		     bool shared)
{
	struct reservation_object_list *flist;
	struct dma_fence *f;
	struct radeon_fence *fence;
	unsigned i;
	int r = 0;

	/* always sync to the exclusive fence */
	f = reservation_object_get_excl(resv);
	fence = f ? to_radeon_fence(f) : NULL;
	if (fence && fence->rdev == rdev)
		radeon_sync_fence(sync, fence);
	else if (f)
		r = dma_fence_wait(f, true);

	flist = reservation_object_get_list(resv);
	if (shared || !flist || r)
		return r;

	for (i = 0; i < flist->shared_count; ++i) {
		f = rcu_dereference_protected(flist->shared[i],
					      reservation_object_held(resv));
		fence = to_radeon_fence(f);
		if (fence && fence->rdev == rdev)
			radeon_sync_fence(sync, fence);
		else
			r = dma_fence_wait(f, true);

		if (r)
			break;
	}
	return r;
}

/**
 * radeon_sync_rings - sync ring to all registered fences
 *
 * @rdev: radeon_device pointer
 * @sync: sync object to use
 * @ring: ring that needs sync
 *
 * Ensure that all registered fences are signaled before letting
 * the ring continue. The caller must hold the ring lock.
 */
int radeon_sync_rings(struct radeon_device *rdev,
		      struct radeon_sync *sync,
		      int ring)
{
	unsigned count = 0;
	int i, r;

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		struct radeon_fence *fence = sync->sync_to[i];
		struct radeon_semaphore *semaphore;

		/* check if we really need to sync */
		if (!radeon_fence_need_sync(fence, ring))
			continue;

		/* prevent GPU deadlocks */
		if (!rdev->ring[i].ready) {
			dev_err(rdev->dev, "Syncing to a disabled ring!");
			return -EINVAL;
		}

		if (count >= RADEON_NUM_SYNCS) {
			/* not enough room, wait manually */
			r = radeon_fence_wait(fence, false);
			if (r)
				return r;
			continue;
		}
		r = radeon_semaphore_create(rdev, &semaphore);
		if (r)
			return r;

		sync->semaphores[count++] = semaphore;

		/* allocate enough space for sync command */
		r = radeon_ring_alloc(rdev, &rdev->ring[i], 16);
		if (r)
			return r;

		/* emit the signal semaphore */
		if (!radeon_semaphore_emit_signal(rdev, i, semaphore)) {
			/* signaling wasn't successful wait manually */
			radeon_ring_undo(&rdev->ring[i]);
			r = radeon_fence_wait(fence, false);
			if (r)
				return r;
			continue;
		}

		/* we assume caller has already allocated space on waiters ring */
		if (!radeon_semaphore_emit_wait(rdev, ring, semaphore)) {
			/* waiting wasn't successful wait manually */
			radeon_ring_undo(&rdev->ring[i]);
			r = radeon_fence_wait(fence, false);
			if (r)
				return r;
			continue;
		}

		radeon_ring_commit(rdev, &rdev->ring[i], false);
		radeon_fence_note_sync(fence, ring);
	}

	return 0;
}

/**
 * radeon_sync_free - free the sync object
 *
 * @rdev: radeon_device pointer
 * @sync: sync object to use
 * @fence: fence to use for the free
 *
 * Free the sync object by freeing all semaphores in it.
 */
void radeon_sync_free(struct radeon_device *rdev,
		      struct radeon_sync *sync,
		      struct radeon_fence *fence)
{
	unsigned i;

	for (i = 0; i < RADEON_NUM_SYNCS; ++i)
		radeon_semaphore_free(rdev, &sync->semaphores[i], fence);
}
