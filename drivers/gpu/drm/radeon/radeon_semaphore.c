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

static int allocate_semaphores(struct radeon_device *rdev)
{
	const unsigned long bo_size = PAGE_SIZE * 4;

	struct radeon_bo *bo;
	struct list_head new_entrys;
	unsigned long irq_flags;
	uint64_t gpu_addr;
	void *map;
	int i, r;

	r = radeon_bo_create(rdev, bo_size, RADEON_GPU_PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_GTT, &bo);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to allocate semaphore bo\n", r);
		return r;
	}

	r = radeon_bo_reserve(bo, false);
	if (r) {
		radeon_bo_unref(&bo);
		dev_err(rdev->dev, "(%d) failed to reserve semaphore bo\n", r);
		return r;
	}

	r = radeon_bo_kmap(bo, &map);
	if (r) {
		radeon_bo_unreserve(bo);
		radeon_bo_unref(&bo);
		dev_err(rdev->dev, "(%d) semaphore map failed\n", r);
		return r;
	}
	memset(map, 0, bo_size);
	radeon_bo_kunmap(bo);

	r = radeon_bo_pin(bo, RADEON_GEM_DOMAIN_VRAM, &gpu_addr);
	if (r) {
		radeon_bo_unreserve(bo);
		radeon_bo_unref(&bo);
		dev_err(rdev->dev, "(%d) semaphore pin failed\n", r);
		return r;
	}

	INIT_LIST_HEAD(&new_entrys);
	for (i = 0; i < bo_size/8; ++i) {
		struct radeon_semaphore *sem = kmalloc(sizeof(struct radeon_semaphore), GFP_KERNEL);
		ttm_bo_reference(&bo->tbo);
		sem->robj = bo;
		sem->gpu_addr = gpu_addr;
		gpu_addr += 8;
		list_add_tail(&sem->list, &new_entrys);
	}

	radeon_bo_unreserve(bo);
	radeon_bo_unref(&bo);

	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	list_splice_tail(&new_entrys, &rdev->semaphore_drv.free);
	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);

	DRM_INFO("%d new semaphores allocated\n", (int)(bo_size/8));

	return 0;
}

int radeon_semaphore_create(struct radeon_device *rdev,
			    struct radeon_semaphore **semaphore)
{
	unsigned long irq_flags;

	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	if (list_empty(&rdev->semaphore_drv.free)) {
		int r;
		write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);
		r = allocate_semaphores(rdev);
		if (r)
			return r;
		write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	}

	*semaphore = list_first_entry(&rdev->semaphore_drv.free, struct radeon_semaphore, list);
	list_del(&(*semaphore)->list);

	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);
	return 0;
}

void radeon_semaphore_emit_signal(struct radeon_device *rdev, int ring,
			          struct radeon_semaphore *semaphore)
{
	radeon_semaphore_ring_emit(rdev, semaphore, ring, false);
}

void radeon_semaphore_emit_wait(struct radeon_device *rdev, int ring,
			        struct radeon_semaphore *semaphore)
{
	radeon_semaphore_ring_emit(rdev, semaphore, ring, true);
}

void radeon_semaphore_free(struct radeon_device *rdev,
			  struct radeon_semaphore *semaphore)
{
	unsigned long irq_flags;

	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	list_add_tail(&semaphore->list, &rdev->semaphore_drv.free);
	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);
}

void radeon_semaphore_driver_fini(struct radeon_device *rdev)
{
	struct radeon_semaphore *i, *n;
	struct list_head entrys;
	unsigned long irq_flags;

	INIT_LIST_HEAD(&entrys);
	write_lock_irqsave(&rdev->semaphore_drv.lock, irq_flags);
	if (!list_empty(&rdev->semaphore_drv.free)) {
		list_splice(&rdev->semaphore_drv.free, &entrys);
	}
	INIT_LIST_HEAD(&rdev->semaphore_drv.free);
	write_unlock_irqrestore(&rdev->semaphore_drv.lock, irq_flags);

	list_for_each_entry_safe(i, n, &entrys, list) {
		radeon_bo_unref(&i->robj);
		kfree(i);
	}
}
