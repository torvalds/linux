/*
 * Copyright 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/delay.h>

#include <trace/events/dma_fence.h>

#include "qxl_drv.h"
#include "qxl_object.h"

/*
 * drawable cmd cache - allocate a bunch of VRAM pages, suballocate
 * into 256 byte chunks for now - gives 16 cmds per page.
 *
 * use an ida to index into the chunks?
 */
/* manage releaseables */
/* stack them 16 high for now -drawable object is 191 */
#define RELEASE_SIZE 256
#define RELEASES_PER_BO (PAGE_SIZE / RELEASE_SIZE)
/* put an alloc/dealloc surface cmd into one bo and round up to 128 */
#define SURFACE_RELEASE_SIZE 128
#define SURFACE_RELEASES_PER_BO (PAGE_SIZE / SURFACE_RELEASE_SIZE)

static const int release_size_per_bo[] = { RELEASE_SIZE, SURFACE_RELEASE_SIZE, RELEASE_SIZE };
static const int releases_per_bo[] = { RELEASES_PER_BO, SURFACE_RELEASES_PER_BO, RELEASES_PER_BO };

static const char *qxl_get_driver_name(struct dma_fence *fence)
{
	return "qxl";
}

static const char *qxl_get_timeline_name(struct dma_fence *fence)
{
	return "release";
}

static long qxl_fence_wait(struct dma_fence *fence, bool intr,
			   signed long timeout)
{
	struct qxl_device *qdev;
	unsigned long cur, end = jiffies + timeout;

	qdev = container_of(fence->lock, struct qxl_device, release_lock);

	if (!wait_event_timeout(qdev->release_event,
				(dma_fence_is_signaled(fence) ||
				 (qxl_io_notify_oom(qdev), 0)),
				timeout))
		return 0;

	cur = jiffies;
	if (time_after(cur, end))
		return 0;
	return end - cur;
}

static const struct dma_fence_ops qxl_fence_ops = {
	.get_driver_name = qxl_get_driver_name,
	.get_timeline_name = qxl_get_timeline_name,
	.wait = qxl_fence_wait,
};

static int
qxl_release_alloc(struct qxl_device *qdev, int type,
		  struct qxl_release **ret)
{
	struct qxl_release *release;
	int handle;
	size_t size = sizeof(*release);

	release = kmalloc(size, GFP_KERNEL);
	if (!release) {
		DRM_ERROR("Out of memory\n");
		return -ENOMEM;
	}
	release->base.ops = NULL;
	release->type = type;
	release->release_offset = 0;
	release->surface_release_id = 0;
	INIT_LIST_HEAD(&release->bos);

	idr_preload(GFP_KERNEL);
	spin_lock(&qdev->release_idr_lock);
	handle = idr_alloc(&qdev->release_idr, release, 1, 0, GFP_NOWAIT);
	release->base.seqno = ++qdev->release_seqno;
	spin_unlock(&qdev->release_idr_lock);
	idr_preload_end();
	if (handle < 0) {
		kfree(release);
		*ret = NULL;
		return handle;
	}
	*ret = release;
	DRM_DEBUG_DRIVER("allocated release %d\n", handle);
	release->id = handle;
	return handle;
}

static void
qxl_release_free_list(struct qxl_release *release)
{
	while (!list_empty(&release->bos)) {
		struct qxl_bo_list *entry;

		entry = container_of(release->bos.next,
				     struct qxl_bo_list, list);
		qxl_bo_unref(&entry->bo);
		list_del(&entry->list);
		kfree(entry);
	}
	release->release_bo = NULL;
}

void
qxl_release_free(struct qxl_device *qdev,
		 struct qxl_release *release)
{
	DRM_DEBUG_DRIVER("release %d, type %d\n", release->id, release->type);

	if (release->surface_release_id)
		qxl_surface_id_dealloc(qdev, release->surface_release_id);

	spin_lock(&qdev->release_idr_lock);
	idr_remove(&qdev->release_idr, release->id);
	spin_unlock(&qdev->release_idr_lock);

	if (release->base.ops) {
		WARN_ON(list_empty(&release->bos));
		qxl_release_free_list(release);

		dma_fence_signal(&release->base);
		dma_fence_put(&release->base);
	} else {
		qxl_release_free_list(release);
		kfree(release);
	}
	atomic_dec(&qdev->release_count);
}

static int qxl_release_bo_alloc(struct qxl_device *qdev,
				struct qxl_bo **bo,
				u32 priority)
{
	/* pin releases bo's they are too messy to evict */
	return qxl_bo_create(qdev, PAGE_SIZE, false, true,
			     QXL_GEM_DOMAIN_VRAM, priority, NULL, bo);
}

int qxl_release_list_add(struct qxl_release *release, struct qxl_bo *bo)
{
	struct qxl_bo_list *entry;

	list_for_each_entry(entry, &release->bos, list) {
		if (entry->bo == bo)
			return 0;
	}

	entry = kmalloc(sizeof(struct qxl_bo_list), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	qxl_bo_ref(bo);
	entry->bo = bo;
	list_add_tail(&entry->list, &release->bos);
	return 0;
}

static int qxl_release_validate_bo(struct qxl_bo *bo)
{
	struct ttm_operation_ctx ctx = { true, false };
	int ret;

	if (!bo->tbo.pin_count) {
		qxl_ttm_placement_from_domain(bo, bo->type);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
		if (ret)
			return ret;
	}

	ret = dma_resv_reserve_fences(bo->tbo.base.resv, 1);
	if (ret)
		return ret;

	/* allocate a surface for reserved + validated buffers */
	ret = qxl_bo_check_id(to_qxl(bo->tbo.base.dev), bo);
	if (ret)
		return ret;
	return 0;
}

int qxl_release_reserve_list(struct qxl_release *release, bool no_intr)
{
	int ret;
	struct qxl_bo_list *entry;

	/* if only one object on the release its the release itself
	   since these objects are pinned no need to reserve */
	if (list_is_singular(&release->bos))
		return 0;

	drm_exec_init(&release->exec, no_intr ? 0 :
		      DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
	drm_exec_until_all_locked(&release->exec) {
		list_for_each_entry(entry, &release->bos, list) {
			ret = drm_exec_prepare_obj(&release->exec,
						   &entry->bo->tbo.base,
						   1);
			drm_exec_retry_on_contention(&release->exec);
			if (ret)
				goto error;
		}
	}

	list_for_each_entry(entry, &release->bos, list) {
		ret = qxl_release_validate_bo(entry->bo);
		if (ret)
			goto error;
	}
	return 0;
error:
	drm_exec_fini(&release->exec);
	return ret;
}

void qxl_release_backoff_reserve_list(struct qxl_release *release)
{
	/* if only one object on the release its the release itself
	   since these objects are pinned no need to reserve */
	if (list_is_singular(&release->bos))
		return;

	drm_exec_fini(&release->exec);
}

int qxl_alloc_surface_release_reserved(struct qxl_device *qdev,
				       enum qxl_surface_cmd_type surface_cmd_type,
				       struct qxl_release *create_rel,
				       struct qxl_release **release)
{
	if (surface_cmd_type == QXL_SURFACE_CMD_DESTROY && create_rel) {
		int idr_ret;
		struct qxl_bo *bo;
		union qxl_release_info *info;

		/* stash the release after the create command */
		idr_ret = qxl_release_alloc(qdev, QXL_RELEASE_SURFACE_CMD, release);
		if (idr_ret < 0)
			return idr_ret;
		bo = create_rel->release_bo;

		(*release)->release_bo = bo;
		(*release)->release_offset = create_rel->release_offset + 64;

		qxl_release_list_add(*release, bo);

		info = qxl_release_map(qdev, *release);
		info->id = idr_ret;
		qxl_release_unmap(qdev, *release, info);
		return 0;
	}

	return qxl_alloc_release_reserved(qdev, sizeof(struct qxl_surface_cmd),
					 QXL_RELEASE_SURFACE_CMD, release, NULL);
}

int qxl_alloc_release_reserved(struct qxl_device *qdev, unsigned long size,
				       int type, struct qxl_release **release,
				       struct qxl_bo **rbo)
{
	struct qxl_bo *bo, *free_bo = NULL;
	int idr_ret;
	int ret = 0;
	union qxl_release_info *info;
	int cur_idx;
	u32 priority;

	if (type == QXL_RELEASE_DRAWABLE) {
		cur_idx = 0;
		priority = 0;
	} else if (type == QXL_RELEASE_SURFACE_CMD) {
		cur_idx = 1;
		priority = 1;
	} else if (type == QXL_RELEASE_CURSOR_CMD) {
		cur_idx = 2;
		priority = 1;
	}
	else {
		DRM_ERROR("got illegal type: %d\n", type);
		return -EINVAL;
	}

	idr_ret = qxl_release_alloc(qdev, type, release);
	if (idr_ret < 0) {
		if (rbo)
			*rbo = NULL;
		return idr_ret;
	}
	atomic_inc(&qdev->release_count);

	mutex_lock(&qdev->release_mutex);
	if (qdev->current_release_bo_offset[cur_idx] + 1 >= releases_per_bo[cur_idx]) {
		free_bo = qdev->current_release_bo[cur_idx];
		qdev->current_release_bo_offset[cur_idx] = 0;
		qdev->current_release_bo[cur_idx] = NULL;
	}
	if (!qdev->current_release_bo[cur_idx]) {
		ret = qxl_release_bo_alloc(qdev, &qdev->current_release_bo[cur_idx], priority);
		if (ret) {
			mutex_unlock(&qdev->release_mutex);
			if (free_bo) {
				qxl_bo_unpin(free_bo);
				qxl_bo_unref(&free_bo);
			}
			qxl_release_free(qdev, *release);
			return ret;
		}
	}

	bo = qxl_bo_ref(qdev->current_release_bo[cur_idx]);

	(*release)->release_bo = bo;
	(*release)->release_offset = qdev->current_release_bo_offset[cur_idx] * release_size_per_bo[cur_idx];
	qdev->current_release_bo_offset[cur_idx]++;

	if (rbo)
		*rbo = bo;

	mutex_unlock(&qdev->release_mutex);
	if (free_bo) {
		qxl_bo_unpin(free_bo);
		qxl_bo_unref(&free_bo);
	}

	ret = qxl_release_list_add(*release, bo);
	qxl_bo_unref(&bo);
	if (ret) {
		qxl_release_free(qdev, *release);
		return ret;
	}

	info = qxl_release_map(qdev, *release);
	info->id = idr_ret;
	qxl_release_unmap(qdev, *release, info);

	return ret;
}

struct qxl_release *qxl_release_from_id_locked(struct qxl_device *qdev,
						   uint64_t id)
{
	struct qxl_release *release;

	spin_lock(&qdev->release_idr_lock);
	release = idr_find(&qdev->release_idr, id);
	spin_unlock(&qdev->release_idr_lock);
	if (!release) {
		DRM_ERROR("failed to find id in release_idr\n");
		return NULL;
	}

	return release;
}

union qxl_release_info *qxl_release_map(struct qxl_device *qdev,
					struct qxl_release *release)
{
	void *ptr;
	union qxl_release_info *info;
	struct qxl_bo *bo = release->release_bo;

	ptr = qxl_bo_kmap_atomic_page(qdev, bo, release->release_offset & PAGE_MASK);
	if (!ptr)
		return NULL;
	info = ptr + (release->release_offset & ~PAGE_MASK);
	return info;
}

void qxl_release_unmap(struct qxl_device *qdev,
		       struct qxl_release *release,
		       union qxl_release_info *info)
{
	struct qxl_bo *bo = release->release_bo;
	void *ptr;

	ptr = ((void *)info) - (release->release_offset & ~PAGE_MASK);
	qxl_bo_kunmap_atomic_page(qdev, bo, ptr);
}

void qxl_release_fence_buffer_objects(struct qxl_release *release)
{
	struct ttm_device *bdev;
	struct qxl_bo_list *entry;
	struct qxl_device *qdev;
	struct qxl_bo *bo;

	/* if only one object on the release its the release itself
	   since these objects are pinned no need to reserve */
	if (list_is_singular(&release->bos) || list_empty(&release->bos))
		return;

	bo = list_first_entry(&release->bos, struct qxl_bo_list, list)->bo;
	bdev = bo->tbo.bdev;
	qdev = container_of(bdev, struct qxl_device, mman.bdev);

	/*
	 * Since we never really allocated a context and we don't want to conflict,
	 * set the highest bits. This will break if we really allow exporting of dma-bufs.
	 */
	dma_fence_init(&release->base, &qxl_fence_ops, &qdev->release_lock,
		       release->id | 0xf0000000, release->base.seqno);
	trace_dma_fence_emit(&release->base);

	list_for_each_entry(entry, &release->bos, list) {
		bo = entry->bo;

		dma_resv_add_fence(bo->tbo.base.resv, &release->base,
				   DMA_RESV_USAGE_READ);
		ttm_bo_move_to_lru_tail_unlocked(&bo->tbo);
	}
	drm_exec_fini(&release->exec);
}
