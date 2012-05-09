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

static int radeon_semaphore_add_bo(struct radeon_device *rdev)
{
	struct radeon_semaphore_bo *bo;
	unsigned long irq_flags;
	uint64_t gpu_addr;
	uint32_t *cpu_ptr;
	int r, i;

	bo = kmalloc(sizeof(struct radeon_semaphore_bo), GFP_KERNEL);
	if (bo == NULL) {
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&bo->free);
	INIT_LIST_HEAD(&bo->list);
	bo->nused = 0;

	r = radeon_ib_get(rdev, 0, &bo->ib, RADEON_SEMAPHORE_BO_SIZE);
	if (r) {
		dev_err(rdev->dev, "failed to get a bo after 5 retry\n");
		kfree(bo);
		return r;
	}
	gpu_addr = rdev->ib_pool.sa_manager.gpu_addr;
	gpu_addr += bo->ib->sa_bo.offset;
	cpu_ptr = rdev->ib_pool.sa_manager.cpu_ptr;
	cpu_ptr += (bo->ib->sa_bo.offset >> 2);
	for (i = 0; i < (RADEON_SEMAPHORE_BO_SIZE/8); i++) {
		bo->semaphores[i].gpu_addr = gpu_addr;
		bo->semaphores[i].cpu_ptr = cpu_ptr;
		bo->semaphores[i].bo = bo;
		list_add_tail(&bo->semaphores[i].list, &bo->free);
		gpu_addr += 8;
		cpu_ptr += 2;
	}
	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	list_add_tail(&bo->list, &rdev->semaphore_drv.bo);
	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);
	return 0;
}

static void radeon_semaphore_del_bo_locked(struct radeon_device *rdev,
					   struct radeon_semaphore_bo *bo)
{
	radeon_sa_bo_free(rdev, &bo->ib->sa_bo);
	radeon_fence_unref(&bo->ib->fence);
	list_del(&bo->list);
	kfree(bo);
}

void radeon_semaphore_shrink_locked(struct radeon_device *rdev)
{
	struct radeon_semaphore_bo *bo, *n;

	if (list_empty(&rdev->semaphore_drv.bo)) {
		return;
	}
	/* only shrink if first bo has free semaphore */
	bo = list_first_entry(&rdev->semaphore_drv.bo, struct radeon_semaphore_bo, list);
	if (list_empty(&bo->free)) {
		return;
	}
	list_for_each_entry_safe_continue(bo, n, &rdev->semaphore_drv.bo, list) {
		if (bo->nused)
			continue;
		radeon_semaphore_del_bo_locked(rdev, bo);
	}
}

int radeon_semaphore_create(struct radeon_device *rdev,
			    struct radeon_semaphore **semaphore)
{
	struct radeon_semaphore_bo *bo;
	unsigned long irq_flags;
	bool do_retry = true;
	int r;

retry:
	*semaphore = NULL;
	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	list_for_each_entry(bo, &rdev->semaphore_drv.bo, list) {
		if (list_empty(&bo->free))
			continue;
		*semaphore = list_first_entry(&bo->free, struct radeon_semaphore, list);
		(*semaphore)->cpu_ptr[0] = 0;
		(*semaphore)->cpu_ptr[1] = 0;
		list_del(&(*semaphore)->list);
		bo->nused++;
		break;
	}
	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);

	if (*semaphore == NULL) {
		if (do_retry) {
			do_retry = false;
			r = radeon_semaphore_add_bo(rdev);
			if (r)
				return r;
			goto retry;
		}
		return -ENOMEM;
	}

	return 0;
}

void radeon_semaphore_emit_signal(struct radeon_device *rdev, int ring,
			          struct radeon_semaphore *semaphore)
{
	radeon_semaphore_ring_emit(rdev, ring, &rdev->ring[ring], semaphore, false);
}

void radeon_semaphore_emit_wait(struct radeon_device *rdev, int ring,
			        struct radeon_semaphore *semaphore)
{
	radeon_semaphore_ring_emit(rdev, ring, &rdev->ring[ring], semaphore, true);
}

int radeon_semaphore_sync_rings(struct radeon_device *rdev,
				struct radeon_semaphore *semaphore,
				bool sync_to[RADEON_NUM_RINGS],
				int dst_ring)
{
	int i = 0, r;

	mutex_lock(&rdev->ring_lock);
	r = radeon_ring_alloc(rdev, &rdev->ring[dst_ring], RADEON_NUM_RINGS * 8);
	if (r) {
		goto error;
	}

	for (i = 0; i < RADEON_NUM_RINGS; ++i) {
		/* no need to sync to our own or unused rings */
		if (!sync_to[i] || i == dst_ring)
			continue;

		/* prevent GPU deadlocks */
		if (!rdev->ring[i].ready) {
			dev_err(rdev->dev, "Trying to sync to a disabled ring!");
			r = -EINVAL;
			goto error;
		}

		r = radeon_ring_alloc(rdev, &rdev->ring[i], 8);
		if (r) {
			goto error;
		}

		radeon_semaphore_emit_signal(rdev, i, semaphore);
		radeon_semaphore_emit_wait(rdev, dst_ring, semaphore);

		radeon_ring_commit(rdev, &rdev->ring[i]);
	}

	radeon_ring_commit(rdev, &rdev->ring[dst_ring]);
	mutex_unlock(&rdev->ring_lock);

	return 0;

error:
	/* unlock all locks taken so far */
	for (--i; i >= 0; --i) {
		if (sync_to[i] || i == dst_ring) {
			radeon_ring_undo(&rdev->ring[i]);
		}
	}
	radeon_ring_undo(&rdev->ring[dst_ring]);
	mutex_unlock(&rdev->ring_lock);
	return r;
}

void radeon_semaphore_free(struct radeon_device *rdev,
			   struct radeon_semaphore *semaphore)
{
	unsigned long irq_flags;

	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	semaphore->bo->nused--;
	list_add_tail(&semaphore->list, &semaphore->bo->free);
	radeon_semaphore_shrink_locked(rdev);
	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);
}

void radeon_semaphore_driver_fini(struct radeon_device *rdev)
{
	struct radeon_semaphore_bo *bo, *n;
	unsigned long irq_flags;

	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	/* we force to free everything */
	list_for_each_entry_safe(bo, n, &rdev->semaphore_drv.bo, list) {
		if (!list_empty(&bo->free)) {
			dev_err(rdev->dev, "still in use semaphore\n");
		}
		radeon_semaphore_del_bo_locked(rdev, bo);
	}
	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);
}
