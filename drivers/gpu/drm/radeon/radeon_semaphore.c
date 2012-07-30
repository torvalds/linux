/*
 * Copyright 2011 Christian König.
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
 *    Christian König <deathsimple@vodafone.de>
 */
#include "drmP.h"
#include "drm.h"
#include "radeon.h"


int radeon_semaphore_create(struct radeon_device *rdev,
			    struct radeon_semaphore **semaphore)
{
	int r;

	*semaphore = kmalloc(sizeof(struct radeon_semaphore), GFP_KERNEL);
	if (*semaphore == NULL) {
		return -ENOMEM;
	}
	r = radeon_sa_bo_new(rdev, &rdev->ring_tmp_bo,
			     &(*semaphore)->sa_bo, 8, 8, true);
	if (r) {
		kfree(*semaphore);
		*semaphore = NULL;
		return r;
	}
	(*semaphore)->waiters = 0;
	(*semaphore)->gpu_addr = radeon_sa_bo_gpu_addr((*semaphore)->sa_bo);
	*((uint64_t*)radeon_sa_bo_cpu_addr((*semaphore)->sa_bo)) = 0;
	return 0;
}

void radeon_semaphore_emit_signal(struct radeon_device *rdev, int ring,
			          struct radeon_semaphore *semaphore)
{
	--semaphore->waiters;
	radeon_semaphore_ring_emit(rdev, ring, &rdev->ring[ring], semaphore, false);
}

void radeon_semaphore_emit_wait(struct radeon_device *rdev, int ring,
			        struct radeon_semaphore *semaphore)
{
	++semaphore->waiters;
	radeon_semaphore_ring_emit(rdev, ring, &rdev->ring[ring], semaphore, true);
}

/* caller must hold ring lock */
int radeon_semaphore_sync_rings(struct radeon_device *rdev,
				struct radeon_semaphore *semaphore,
				int signaler, int waiter)
{
	int r;

	/* no need to signal and wait on the same ring */
	if (signaler == waiter) {
		return 0;
	}

	/* prevent GPU deadlocks */
	if (!rdev->ring[signaler].ready) {
		dev_err(rdev->dev, "Trying to sync to a disabled ring!");
		return -EINVAL;
	}

	r = radeon_ring_alloc(rdev, &rdev->ring[signaler], 8);
	if (r) {
		return r;
	}
	radeon_semaphore_emit_signal(rdev, signaler, semaphore);
	radeon_ring_commit(rdev, &rdev->ring[signaler]);

	/* we assume caller has already allocated space on waiters ring */
	radeon_semaphore_emit_wait(rdev, waiter, semaphore);

	return 0;
}

void radeon_semaphore_free(struct radeon_device *rdev,
			   struct radeon_semaphore **semaphore,
			   struct radeon_fence *fence)
{
	if (semaphore == NULL || *semaphore == NULL) {
		return;
	}
	if ((*semaphore)->waiters > 0) {
		dev_err(rdev->dev, "semaphore %p has more waiters than signalers,"
			" hardware lockup imminent!\n", *semaphore);
	}
	radeon_sa_bo_free(rdev, &(*semaphore)->sa_bo, fence);
	kfree(*semaphore);
	*semaphore = NULL;
}
