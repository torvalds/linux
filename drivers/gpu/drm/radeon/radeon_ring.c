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
 */
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon_reg.h"
#include "radeon.h"
#include "atom.h"

int radeon_debugfs_ib_init(struct radeon_device *rdev);

void radeon_ib_bogus_cleanup(struct radeon_device *rdev)
{
	struct radeon_ib *ib, *n;

	list_for_each_entry_safe(ib, n, &rdev->ib_pool.bogus_ib, list) {
		list_del(&ib->list);
		vfree(ib->ptr);
		kfree(ib);
	}
}

void radeon_ib_bogus_add(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ib *bib;

	bib = kmalloc(sizeof(*bib), GFP_KERNEL);
	if (bib == NULL)
		return;
	bib->ptr = vmalloc(ib->length_dw * 4);
	if (bib->ptr == NULL) {
		kfree(bib);
		return;
	}
	memcpy(bib->ptr, ib->ptr, ib->length_dw * 4);
	bib->length_dw = ib->length_dw;
	mutex_lock(&rdev->ib_pool.mutex);
	list_add_tail(&bib->list, &rdev->ib_pool.bogus_ib);
	mutex_unlock(&rdev->ib_pool.mutex);
}

/*
 * IB.
 */
int radeon_ib_get(struct radeon_device *rdev, struct radeon_ib **ib)
{
	struct radeon_fence *fence;
	struct radeon_ib *nib;
	int r = 0, i, c;

	*ib = NULL;
	r = radeon_fence_create(rdev, &fence);
	if (r) {
		dev_err(rdev->dev, "failed to create fence for new IB\n");
		return r;
	}
	mutex_lock(&rdev->ib_pool.mutex);
	for (i = rdev->ib_pool.head_id, c = 0, nib = NULL; c < RADEON_IB_POOL_SIZE; c++, i++) {
		i &= (RADEON_IB_POOL_SIZE - 1);
		if (rdev->ib_pool.ibs[i].free) {
			nib = &rdev->ib_pool.ibs[i];
			break;
		}
	}
	if (nib == NULL) {
		/* This should never happen, it means we allocated all
		 * IB and haven't scheduled one yet, return EBUSY to
		 * userspace hoping that on ioctl recall we get better
		 * luck
		 */
		dev_err(rdev->dev, "no free indirect buffer !\n");
		mutex_unlock(&rdev->ib_pool.mutex);
		radeon_fence_unref(&fence);
		return -EBUSY;
	}
	rdev->ib_pool.head_id = (nib->idx + 1) & (RADEON_IB_POOL_SIZE - 1);
	nib->free = false;
	if (nib->fence) {
		mutex_unlock(&rdev->ib_pool.mutex);
		r = radeon_fence_wait(nib->fence, false);
		if (r) {
			dev_err(rdev->dev, "error waiting fence of IB(%u:0x%016lX:%u)\n",
				nib->idx, (unsigned long)nib->gpu_addr, nib->length_dw);
			mutex_lock(&rdev->ib_pool.mutex);
			nib->free = true;
			mutex_unlock(&rdev->ib_pool.mutex);
			radeon_fence_unref(&fence);
			return r;
		}
		mutex_lock(&rdev->ib_pool.mutex);
	}
	radeon_fence_unref(&nib->fence);
	nib->fence = fence;
	nib->length_dw = 0;
	mutex_unlock(&rdev->ib_pool.mutex);
	*ib = nib;
	return 0;
}

void radeon_ib_free(struct radeon_device *rdev, struct radeon_ib **ib)
{
	struct radeon_ib *tmp = *ib;

	*ib = NULL;
	if (tmp == NULL) {
		return;
	}
	if (!tmp->fence->emited)
		radeon_fence_unref(&tmp->fence);
	mutex_lock(&rdev->ib_pool.mutex);
	tmp->free = true;
	mutex_unlock(&rdev->ib_pool.mutex);
}

int radeon_ib_schedule(struct radeon_device *rdev, struct radeon_ib *ib)
{
	int r = 0;

	if (!ib->length_dw || !rdev->cp.ready) {
		/* TODO: Nothings in the ib we should report. */
		DRM_ERROR("radeon: couldn't schedule IB(%u).\n", ib->idx);
		return -EINVAL;
	}

	/* 64 dwords should be enough for fence too */
	r = radeon_ring_lock(rdev, 64);
	if (r) {
		DRM_ERROR("radeon: scheduling IB failled (%d).\n", r);
		return r;
	}
	radeon_ring_ib_execute(rdev, ib);
	radeon_fence_emit(rdev, ib->fence);
	mutex_lock(&rdev->ib_pool.mutex);
	/* once scheduled IB is considered free and protected by the fence */
	ib->free = true;
	mutex_unlock(&rdev->ib_pool.mutex);
	radeon_ring_unlock_commit(rdev);
	return 0;
}

int radeon_ib_pool_init(struct radeon_device *rdev)
{
	void *ptr;
	uint64_t gpu_addr;
	int i;
	int r = 0;

	if (rdev->ib_pool.robj)
		return 0;
	INIT_LIST_HEAD(&rdev->ib_pool.bogus_ib);
	/* Allocate 1M object buffer */
	r = radeon_bo_create(rdev, NULL,  RADEON_IB_POOL_SIZE*64*1024,
				true, RADEON_GEM_DOMAIN_GTT,
				&rdev->ib_pool.robj);
	if (r) {
		DRM_ERROR("radeon: failed to ib pool (%d).\n", r);
		return r;
	}
	r = radeon_bo_reserve(rdev->ib_pool.robj, false);
	if (unlikely(r != 0))
		return r;
	r = radeon_bo_pin(rdev->ib_pool.robj, RADEON_GEM_DOMAIN_GTT, &gpu_addr);
	if (r) {
		radeon_bo_unreserve(rdev->ib_pool.robj);
		DRM_ERROR("radeon: failed to pin ib pool (%d).\n", r);
		return r;
	}
	r = radeon_bo_kmap(rdev->ib_pool.robj, &ptr);
	radeon_bo_unreserve(rdev->ib_pool.robj);
	if (r) {
		DRM_ERROR("radeon: failed to map ib poll (%d).\n", r);
		return r;
	}
	for (i = 0; i < RADEON_IB_POOL_SIZE; i++) {
		unsigned offset;

		offset = i * 64 * 1024;
		rdev->ib_pool.ibs[i].gpu_addr = gpu_addr + offset;
		rdev->ib_pool.ibs[i].ptr = ptr + offset;
		rdev->ib_pool.ibs[i].idx = i;
		rdev->ib_pool.ibs[i].length_dw = 0;
		rdev->ib_pool.ibs[i].free = true;
	}
	rdev->ib_pool.head_id = 0;
	rdev->ib_pool.ready = true;
	DRM_INFO("radeon: ib pool ready.\n");
	if (radeon_debugfs_ib_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for IB !\n");
	}
	return r;
}

void radeon_ib_pool_fini(struct radeon_device *rdev)
{
	int r;

	if (!rdev->ib_pool.ready) {
		return;
	}
	mutex_lock(&rdev->ib_pool.mutex);
	radeon_ib_bogus_cleanup(rdev);

	if (rdev->ib_pool.robj) {
		r = radeon_bo_reserve(rdev->ib_pool.robj, false);
		if (likely(r == 0)) {
			radeon_bo_kunmap(rdev->ib_pool.robj);
			radeon_bo_unpin(rdev->ib_pool.robj);
			radeon_bo_unreserve(rdev->ib_pool.robj);
		}
		radeon_bo_unref(&rdev->ib_pool.robj);
		rdev->ib_pool.robj = NULL;
	}
	mutex_unlock(&rdev->ib_pool.mutex);
}


/*
 * Ring.
 */
void radeon_ring_free_size(struct radeon_device *rdev)
{
	if (rdev->family >= CHIP_R600)
		rdev->cp.rptr = RREG32(R600_CP_RB_RPTR);
	else
		rdev->cp.rptr = RREG32(RADEON_CP_RB_RPTR);
	/* This works because ring_size is a power of 2 */
	rdev->cp.ring_free_dw = (rdev->cp.rptr + (rdev->cp.ring_size / 4));
	rdev->cp.ring_free_dw -= rdev->cp.wptr;
	rdev->cp.ring_free_dw &= rdev->cp.ptr_mask;
	if (!rdev->cp.ring_free_dw) {
		rdev->cp.ring_free_dw = rdev->cp.ring_size / 4;
	}
}

int radeon_ring_lock(struct radeon_device *rdev, unsigned ndw)
{
	int r;

	/* Align requested size with padding so unlock_commit can
	 * pad safely */
	ndw = (ndw + rdev->cp.align_mask) & ~rdev->cp.align_mask;
	mutex_lock(&rdev->cp.mutex);
	while (ndw > (rdev->cp.ring_free_dw - 1)) {
		radeon_ring_free_size(rdev);
		if (ndw < rdev->cp.ring_free_dw) {
			break;
		}
		r = radeon_fence_wait_next(rdev);
		if (r) {
			mutex_unlock(&rdev->cp.mutex);
			return r;
		}
	}
	rdev->cp.count_dw = ndw;
	rdev->cp.wptr_old = rdev->cp.wptr;
	return 0;
}

void radeon_ring_unlock_commit(struct radeon_device *rdev)
{
	unsigned count_dw_pad;
	unsigned i;

	/* We pad to match fetch size */
	count_dw_pad = (rdev->cp.align_mask + 1) -
		       (rdev->cp.wptr & rdev->cp.align_mask);
	for (i = 0; i < count_dw_pad; i++) {
		radeon_ring_write(rdev, 2 << 30);
	}
	DRM_MEMORYBARRIER();
	radeon_cp_commit(rdev);
	mutex_unlock(&rdev->cp.mutex);
}

void radeon_ring_unlock_undo(struct radeon_device *rdev)
{
	rdev->cp.wptr = rdev->cp.wptr_old;
	mutex_unlock(&rdev->cp.mutex);
}

int radeon_ring_init(struct radeon_device *rdev, unsigned ring_size)
{
	int r;

	rdev->cp.ring_size = ring_size;
	/* Allocate ring buffer */
	if (rdev->cp.ring_obj == NULL) {
		r = radeon_bo_create(rdev, NULL, rdev->cp.ring_size, true,
					RADEON_GEM_DOMAIN_GTT,
					&rdev->cp.ring_obj);
		if (r) {
			dev_err(rdev->dev, "(%d) ring create failed\n", r);
			return r;
		}
		r = radeon_bo_reserve(rdev->cp.ring_obj, false);
		if (unlikely(r != 0))
			return r;
		r = radeon_bo_pin(rdev->cp.ring_obj, RADEON_GEM_DOMAIN_GTT,
					&rdev->cp.gpu_addr);
		if (r) {
			radeon_bo_unreserve(rdev->cp.ring_obj);
			dev_err(rdev->dev, "(%d) ring pin failed\n", r);
			return r;
		}
		r = radeon_bo_kmap(rdev->cp.ring_obj,
				       (void **)&rdev->cp.ring);
		radeon_bo_unreserve(rdev->cp.ring_obj);
		if (r) {
			dev_err(rdev->dev, "(%d) ring map failed\n", r);
			return r;
		}
	}
	rdev->cp.ptr_mask = (rdev->cp.ring_size / 4) - 1;
	rdev->cp.ring_free_dw = rdev->cp.ring_size / 4;
	return 0;
}

void radeon_ring_fini(struct radeon_device *rdev)
{
	int r;

	mutex_lock(&rdev->cp.mutex);
	if (rdev->cp.ring_obj) {
		r = radeon_bo_reserve(rdev->cp.ring_obj, false);
		if (likely(r == 0)) {
			radeon_bo_kunmap(rdev->cp.ring_obj);
			radeon_bo_unpin(rdev->cp.ring_obj);
			radeon_bo_unreserve(rdev->cp.ring_obj);
		}
		radeon_bo_unref(&rdev->cp.ring_obj);
		rdev->cp.ring = NULL;
		rdev->cp.ring_obj = NULL;
	}
	mutex_unlock(&rdev->cp.mutex);
}


/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)
static int radeon_debugfs_ib_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct radeon_ib *ib = node->info_ent->data;
	unsigned i;

	if (ib == NULL) {
		return 0;
	}
	seq_printf(m, "IB %04u\n", ib->idx);
	seq_printf(m, "IB fence %p\n", ib->fence);
	seq_printf(m, "IB size %05u dwords\n", ib->length_dw);
	for (i = 0; i < ib->length_dw; i++) {
		seq_printf(m, "[%05u]=0x%08X\n", i, ib->ptr[i]);
	}
	return 0;
}

static int radeon_debugfs_ib_bogus_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct radeon_device *rdev = node->info_ent->data;
	struct radeon_ib *ib;
	unsigned i;

	mutex_lock(&rdev->ib_pool.mutex);
	if (list_empty(&rdev->ib_pool.bogus_ib)) {
		mutex_unlock(&rdev->ib_pool.mutex);
		seq_printf(m, "no bogus IB recorded\n");
		return 0;
	}
	ib = list_first_entry(&rdev->ib_pool.bogus_ib, struct radeon_ib, list);
	list_del_init(&ib->list);
	mutex_unlock(&rdev->ib_pool.mutex);
	seq_printf(m, "IB size %05u dwords\n", ib->length_dw);
	for (i = 0; i < ib->length_dw; i++) {
		seq_printf(m, "[%05u]=0x%08X\n", i, ib->ptr[i]);
	}
	vfree(ib->ptr);
	kfree(ib);
	return 0;
}

static struct drm_info_list radeon_debugfs_ib_list[RADEON_IB_POOL_SIZE];
static char radeon_debugfs_ib_names[RADEON_IB_POOL_SIZE][32];

static struct drm_info_list radeon_debugfs_ib_bogus_info_list[] = {
	{"radeon_ib_bogus", radeon_debugfs_ib_bogus_info, 0, NULL},
};
#endif

int radeon_debugfs_ib_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	unsigned i;
	int r;

	radeon_debugfs_ib_bogus_info_list[0].data = rdev;
	r = radeon_debugfs_add_files(rdev, radeon_debugfs_ib_bogus_info_list, 1);
	if (r)
		return r;
	for (i = 0; i < RADEON_IB_POOL_SIZE; i++) {
		sprintf(radeon_debugfs_ib_names[i], "radeon_ib_%04u", i);
		radeon_debugfs_ib_list[i].name = radeon_debugfs_ib_names[i];
		radeon_debugfs_ib_list[i].show = &radeon_debugfs_ib_info;
		radeon_debugfs_ib_list[i].driver_features = 0;
		radeon_debugfs_ib_list[i].data = &rdev->ib_pool.ibs[i];
	}
	return radeon_debugfs_add_files(rdev, radeon_debugfs_ib_list,
					RADEON_IB_POOL_SIZE);
#else
	return 0;
#endif
}
