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
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"

int amdgpu_semaphore_create(struct amdgpu_device *adev,
			    struct amdgpu_semaphore **semaphore)
{
	int r;

	*semaphore = kmalloc(sizeof(struct amdgpu_semaphore), GFP_KERNEL);
	if (*semaphore == NULL) {
		return -ENOMEM;
	}
	r = amdgpu_sa_bo_new(adev, &adev->ring_tmp_bo,
			     &(*semaphore)->sa_bo, 8, 8);
	if (r) {
		kfree(*semaphore);
		*semaphore = NULL;
		return r;
	}
	(*semaphore)->waiters = 0;
	(*semaphore)->gpu_addr = amdgpu_sa_bo_gpu_addr((*semaphore)->sa_bo);

	*((uint64_t *)amdgpu_sa_bo_cpu_addr((*semaphore)->sa_bo)) = 0;

	return 0;
}

bool amdgpu_semaphore_emit_signal(struct amdgpu_ring *ring,
				  struct amdgpu_semaphore *semaphore)
{
	trace_amdgpu_semaphore_signale(ring->idx, semaphore);

	if (amdgpu_ring_emit_semaphore(ring, semaphore, false)) {
		--semaphore->waiters;

		/* for debugging lockup only, used by sysfs debug files */
		ring->last_semaphore_signal_addr = semaphore->gpu_addr;
		return true;
	}
	return false;
}

bool amdgpu_semaphore_emit_wait(struct amdgpu_ring *ring,
				struct amdgpu_semaphore *semaphore)
{
	trace_amdgpu_semaphore_wait(ring->idx, semaphore);

	if (amdgpu_ring_emit_semaphore(ring, semaphore, true)) {
		++semaphore->waiters;

		/* for debugging lockup only, used by sysfs debug files */
		ring->last_semaphore_wait_addr = semaphore->gpu_addr;
		return true;
	}
	return false;
}

void amdgpu_semaphore_free(struct amdgpu_device *adev,
			   struct amdgpu_semaphore **semaphore,
			   struct amdgpu_fence *fence)
{
	if (semaphore == NULL || *semaphore == NULL) {
		return;
	}
	if ((*semaphore)->waiters > 0) {
		dev_err(adev->dev, "semaphore %p has more waiters than signalers,"
			" hardware lockup imminent!\n", *semaphore);
	}
	amdgpu_sa_bo_free(adev, &(*semaphore)->sa_bo, fence);
	kfree(*semaphore);
	*semaphore = NULL;
}
