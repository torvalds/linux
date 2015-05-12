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
#include "amdgpu.h"
#include "amdgpu_trace.h"

/**
 * amdgpu_sync_create - zero init sync object
 *
 * @sync: sync object to initialize
 *
 * Just clear the sync object for now.
 */
void amdgpu_sync_create(struct amdgpu_sync *sync)
{
	unsigned i;

	for (i = 0; i < AMDGPU_NUM_SYNCS; ++i)
		sync->semaphores[i] = NULL;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		sync->sync_to[i] = NULL;

	sync->last_vm_update = NULL;
}

/**
 * amdgpu_sync_fence - use the semaphore to sync to a fence
 *
 * @sync: sync object to add fence to
 * @fence: fence to sync to
 *
 * Sync to the fence using the semaphore objects
 */
void amdgpu_sync_fence(struct amdgpu_sync *sync,
		       struct amdgpu_fence *fence)
{
	struct amdgpu_fence *other;

	if (!fence)
		return;

	other = sync->sync_to[fence->ring->idx];
	sync->sync_to[fence->ring->idx] = amdgpu_fence_ref(
		amdgpu_fence_later(fence, other));
	amdgpu_fence_unref(&other);

	if (fence->owner == AMDGPU_FENCE_OWNER_VM) {
		other = sync->last_vm_update;
		sync->last_vm_update = amdgpu_fence_ref(
			amdgpu_fence_later(fence, other));
		amdgpu_fence_unref(&other);
	}
}

/**
 * amdgpu_sync_resv - use the semaphores to sync to a reservation object
 *
 * @sync: sync object to add fences from reservation object to
 * @resv: reservation object with embedded fence
 * @shared: true if we should only sync to the exclusive fence
 *
 * Sync to the fence using the semaphore objects
 */
int amdgpu_sync_resv(struct amdgpu_device *adev,
		     struct amdgpu_sync *sync,
		     struct reservation_object *resv,
		     void *owner)
{
	struct reservation_object_list *flist;
	struct fence *f;
	struct amdgpu_fence *fence;
	unsigned i;
	int r = 0;

	if (resv == NULL)
		return -EINVAL;

	/* always sync to the exclusive fence */
	f = reservation_object_get_excl(resv);
	fence = f ? to_amdgpu_fence(f) : NULL;
	if (fence && fence->ring->adev == adev)
		amdgpu_sync_fence(sync, fence);
	else if (f)
		r = fence_wait(f, true);

	flist = reservation_object_get_list(resv);
	if (!flist || r)
		return r;

	for (i = 0; i < flist->shared_count; ++i) {
		f = rcu_dereference_protected(flist->shared[i],
					      reservation_object_held(resv));
		fence = f ? to_amdgpu_fence(f) : NULL;
		if (fence && fence->ring->adev == adev) {
			if (fence->owner != owner ||
			    fence->owner == AMDGPU_FENCE_OWNER_UNDEFINED)
				amdgpu_sync_fence(sync, fence);
		} else if (f) {
			r = fence_wait(f, true);
			if (r)
				break;
		}
	}
	return r;
}

/**
 * amdgpu_sync_rings - sync ring to all registered fences
 *
 * @sync: sync object to use
 * @ring: ring that needs sync
 *
 * Ensure that all registered fences are signaled before letting
 * the ring continue. The caller must hold the ring lock.
 */
int amdgpu_sync_rings(struct amdgpu_sync *sync,
		      struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	unsigned count = 0;
	int i, r;

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i) {
		struct amdgpu_fence *fence = sync->sync_to[i];
		struct amdgpu_semaphore *semaphore;
		struct amdgpu_ring *other = adev->rings[i];

		/* check if we really need to sync */
		if (!amdgpu_fence_need_sync(fence, ring))
			continue;

		/* prevent GPU deadlocks */
		if (!other->ready) {
			dev_err(adev->dev, "Syncing to a disabled ring!");
			return -EINVAL;
		}

		if (count >= AMDGPU_NUM_SYNCS) {
			/* not enough room, wait manually */
			r = amdgpu_fence_wait(fence, false);
			if (r)
				return r;
			continue;
		}
		r = amdgpu_semaphore_create(adev, &semaphore);
		if (r)
			return r;

		sync->semaphores[count++] = semaphore;

		/* allocate enough space for sync command */
		r = amdgpu_ring_alloc(other, 16);
		if (r)
			return r;

		/* emit the signal semaphore */
		if (!amdgpu_semaphore_emit_signal(other, semaphore)) {
			/* signaling wasn't successful wait manually */
			amdgpu_ring_undo(other);
			r = amdgpu_fence_wait(fence, false);
			if (r)
				return r;
			continue;
		}

		/* we assume caller has already allocated space on waiters ring */
		if (!amdgpu_semaphore_emit_wait(ring, semaphore)) {
			/* waiting wasn't successful wait manually */
			amdgpu_ring_undo(other);
			r = amdgpu_fence_wait(fence, false);
			if (r)
				return r;
			continue;
		}

		amdgpu_ring_commit(other);
		amdgpu_fence_note_sync(fence, ring);
	}

	return 0;
}

/**
 * amdgpu_sync_free - free the sync object
 *
 * @adev: amdgpu_device pointer
 * @sync: sync object to use
 * @fence: fence to use for the free
 *
 * Free the sync object by freeing all semaphores in it.
 */
void amdgpu_sync_free(struct amdgpu_device *adev,
		      struct amdgpu_sync *sync,
		      struct amdgpu_fence *fence)
{
	unsigned i;

	for (i = 0; i < AMDGPU_NUM_SYNCS; ++i)
		amdgpu_semaphore_free(adev, &sync->semaphores[i], fence);

	for (i = 0; i < AMDGPU_MAX_RINGS; ++i)
		amdgpu_fence_unref(&sync->sync_to[i]);

	amdgpu_fence_unref(&sync->last_vm_update);
}
