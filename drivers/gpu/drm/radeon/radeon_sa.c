/*
 * Copyright 2011 Red Hat Inc.
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
 *    Jerome Glisse <glisse@freedesktop.org>
 */
#include "drmP.h"
#include "drm.h"
#include "radeon.h"

int radeon_sa_bo_manager_init(struct radeon_device *rdev,
			      struct radeon_sa_manager *sa_manager,
			      unsigned size, u32 domain)
{
	int r;

	spin_lock_init(&sa_manager->lock);
	sa_manager->bo = NULL;
	sa_manager->size = size;
	sa_manager->domain = domain;
	INIT_LIST_HEAD(&sa_manager->sa_bo);

	r = radeon_bo_create(rdev, size, RADEON_GPU_PAGE_SIZE, true,
			     RADEON_GEM_DOMAIN_CPU, &sa_manager->bo);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to allocate bo for manager\n", r);
		return r;
	}

	return r;
}

void radeon_sa_bo_manager_fini(struct radeon_device *rdev,
			       struct radeon_sa_manager *sa_manager)
{
	struct radeon_sa_bo *sa_bo, *tmp;

	if (!list_empty(&sa_manager->sa_bo)) {
		dev_err(rdev->dev, "sa_manager is not empty, clearing anyway\n");
	}
	list_for_each_entry_safe(sa_bo, tmp, &sa_manager->sa_bo, list) {
		list_del_init(&sa_bo->list);
	}
	radeon_bo_unref(&sa_manager->bo);
	sa_manager->size = 0;
}

int radeon_sa_bo_manager_start(struct radeon_device *rdev,
			       struct radeon_sa_manager *sa_manager)
{
	int r;

	if (sa_manager->bo == NULL) {
		dev_err(rdev->dev, "no bo for sa manager\n");
		return -EINVAL;
	}

	/* map the buffer */
	r = radeon_bo_reserve(sa_manager->bo, false);
	if (r) {
		dev_err(rdev->dev, "(%d) failed to reserve manager bo\n", r);
		return r;
	}
	r = radeon_bo_pin(sa_manager->bo, sa_manager->domain, &sa_manager->gpu_addr);
	if (r) {
		radeon_bo_unreserve(sa_manager->bo);
		dev_err(rdev->dev, "(%d) failed to pin manager bo\n", r);
		return r;
	}
	r = radeon_bo_kmap(sa_manager->bo, &sa_manager->cpu_ptr);
	radeon_bo_unreserve(sa_manager->bo);
	return r;
}

int radeon_sa_bo_manager_suspend(struct radeon_device *rdev,
				 struct radeon_sa_manager *sa_manager)
{
	int r;

	if (sa_manager->bo == NULL) {
		dev_err(rdev->dev, "no bo for sa manager\n");
		return -EINVAL;
	}

	r = radeon_bo_reserve(sa_manager->bo, false);
	if (!r) {
		radeon_bo_kunmap(sa_manager->bo);
		radeon_bo_unpin(sa_manager->bo);
		radeon_bo_unreserve(sa_manager->bo);
	}
	return r;
}

/*
 * Principe is simple, we keep a list of sub allocation in offset
 * order (first entry has offset == 0, last entry has the highest
 * offset).
 *
 * When allocating new object we first check if there is room at
 * the end total_size - (last_object_offset + last_object_size) >=
 * alloc_size. If so we allocate new object there.
 *
 * When there is not enough room at the end, we start waiting for
 * each sub object until we reach object_offset+object_size >=
 * alloc_size, this object then become the sub object we return.
 *
 * Alignment can't be bigger than page size
 */
int radeon_sa_bo_new(struct radeon_device *rdev,
		     struct radeon_sa_manager *sa_manager,
		     struct radeon_sa_bo *sa_bo,
		     unsigned size, unsigned align)
{
	struct radeon_sa_bo *tmp;
	struct list_head *head;
	unsigned offset = 0, wasted = 0;

	BUG_ON(align > RADEON_GPU_PAGE_SIZE);
	BUG_ON(size > sa_manager->size);
	spin_lock(&sa_manager->lock);

	/* no one ? */
	head = sa_manager->sa_bo.prev;
	if (list_empty(&sa_manager->sa_bo)) {
		goto out;
	}

	/* look for a hole big enough */
	offset = 0;
	list_for_each_entry(tmp, &sa_manager->sa_bo, list) {
		/* room before this object ? */
		if (offset < tmp->offset && (tmp->offset - offset) >= size) {
			head = tmp->list.prev;
			goto out;
		}
		offset = tmp->offset + tmp->size;
		wasted = offset % align;
		if (wasted) {
			wasted = align - wasted;
		}
		offset += wasted;
	}
	/* room at the end ? */
	head = sa_manager->sa_bo.prev;
	tmp = list_entry(head, struct radeon_sa_bo, list);
	offset = tmp->offset + tmp->size;
	wasted = offset % align;
	if (wasted) {
		wasted = align - wasted;
	}
	offset += wasted;
	if ((sa_manager->size - offset) < size) {
		/* failed to find somethings big enough */
		spin_unlock(&sa_manager->lock);
		return -ENOMEM;
	}

out:
	sa_bo->manager = sa_manager;
	sa_bo->offset = offset;
	sa_bo->size = size;
	list_add(&sa_bo->list, head);
	spin_unlock(&sa_manager->lock);
	return 0;
}

void radeon_sa_bo_free(struct radeon_device *rdev, struct radeon_sa_bo *sa_bo)
{
	spin_lock(&sa_bo->manager->lock);
	list_del_init(&sa_bo->list);
	spin_unlock(&sa_bo->manager->lock);
}
