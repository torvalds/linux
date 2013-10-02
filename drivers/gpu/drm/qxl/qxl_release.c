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
#define RELEASES_PER_BO (4096 / RELEASE_SIZE)
/* put an alloc/dealloc surface cmd into one bo and round up to 128 */
#define SURFACE_RELEASE_SIZE 128
#define SURFACE_RELEASES_PER_BO (4096 / SURFACE_RELEASE_SIZE)

static const int release_size_per_bo[] = { RELEASE_SIZE, SURFACE_RELEASE_SIZE, RELEASE_SIZE };
static const int releases_per_bo[] = { RELEASES_PER_BO, SURFACE_RELEASES_PER_BO, RELEASES_PER_BO };

static uint64_t
qxl_release_alloc(struct qxl_device *qdev, int type,
		  struct qxl_release **ret)
{
	struct qxl_release *release;
	int handle;
	size_t size = sizeof(*release);
	int idr_ret;

	release = kmalloc(size, GFP_KERNEL);
	if (!release) {
		DRM_ERROR("Out of memory\n");
		return 0;
	}
	release->type = type;
	release->release_offset = 0;
	release->surface_release_id = 0;
	INIT_LIST_HEAD(&release->bos);

	idr_preload(GFP_KERNEL);
	spin_lock(&qdev->release_idr_lock);
	idr_ret = idr_alloc(&qdev->release_idr, release, 1, 0, GFP_NOWAIT);
	spin_unlock(&qdev->release_idr_lock);
	idr_preload_end();
	handle = idr_ret;
	if (idr_ret < 0)
		goto release_fail;
	*ret = release;
	QXL_INFO(qdev, "allocated release %lld\n", handle);
	release->id = handle;
release_fail:

	return handle;
}

void
qxl_release_free(struct qxl_device *qdev,
		 struct qxl_release *release)
{
	struct qxl_bo_list *entry, *tmp;
	QXL_INFO(qdev, "release %d, type %d\n", release->id,
		 release->type);

	if (release->surface_release_id)
		qxl_surface_id_dealloc(qdev, release->surface_release_id);

	list_for_each_entry_safe(entry, tmp, &release->bos, tv.head) {
		struct qxl_bo *bo = to_qxl_bo(entry->tv.bo);
		QXL_INFO(qdev, "release %llx\n",
			drm_vma_node_offset_addr(&entry->tv.bo->vma_node)
						- DRM_FILE_OFFSET);
		qxl_fence_remove_release(&bo->fence, release->id);
		qxl_bo_unref(&bo);
	}
	spin_lock(&qdev->release_idr_lock);
	idr_remove(&qdev->release_idr, release->id);
	spin_unlock(&qdev->release_idr_lock);
	kfree(release);
}

static int qxl_release_bo_alloc(struct qxl_device *qdev,
				struct qxl_bo **bo)
{
	int ret;
	/* pin releases bo's they are too messy to evict */
	ret = qxl_bo_create(qdev, PAGE_SIZE, false, true,
			    QXL_GEM_DOMAIN_VRAM, NULL,
			    bo);
	return ret;
}

int qxl_release_list_add(struct qxl_release *release, struct qxl_bo *bo)
{
	struct qxl_bo_list *entry;

	list_for_each_entry(entry, &release->bos, tv.head) {
		if (entry->tv.bo == &bo->tbo)
			return 0;
	}

	entry = kmalloc(sizeof(struct qxl_bo_list), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	qxl_bo_ref(bo);
	entry->tv.bo = &bo->tbo;
	list_add_tail(&entry->tv.head, &release->bos);
	return 0;
}

static int qxl_release_validate_bo(struct qxl_bo *bo)
{
	int ret;

	if (!bo->pin_count) {
		qxl_ttm_placement_from_domain(bo, bo->type, false);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement,
				      true, false);
		if (ret)
			return ret;
	}

	/* allocate a surface for reserved + validated buffers */
	ret = qxl_bo_check_id(bo->gem_base.dev->dev_private, bo);
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

	ret = ttm_eu_reserve_buffers(&release->ticket, &release->bos);
	if (ret)
		return ret;

	list_for_each_entry(entry, &release->bos, tv.head) {
		struct qxl_bo *bo = to_qxl_bo(entry->tv.bo);

		ret = qxl_release_validate_bo(bo);
		if (ret) {
			ttm_eu_backoff_reservation(&release->ticket, &release->bos);
			return ret;
		}
	}
	return 0;
}

void qxl_release_backoff_reserve_list(struct qxl_release *release)
{
	/* if only one object on the release its the release itself
	   since these objects are pinned no need to reserve */
	if (list_is_singular(&release->bos))
		return;

	ttm_eu_backoff_reservation(&release->ticket, &release->bos);
}


int qxl_alloc_surface_release_reserved(struct qxl_device *qdev,
				       enum qxl_surface_cmd_type surface_cmd_type,
				       struct qxl_release *create_rel,
				       struct qxl_release **release)
{
	if (surface_cmd_type == QXL_SURFACE_CMD_DESTROY && create_rel) {
		int idr_ret;
		struct qxl_bo_list *entry = list_first_entry(&create_rel->bos, struct qxl_bo_list, tv.head);
		struct qxl_bo *bo;
		union qxl_release_info *info;

		/* stash the release after the create command */
		idr_ret = qxl_release_alloc(qdev, QXL_RELEASE_SURFACE_CMD, release);
		bo = qxl_bo_ref(to_qxl_bo(entry->tv.bo));

		(*release)->release_offset = create_rel->release_offset + 64;

		qxl_release_list_add(*release, bo);

		info = qxl_release_map(qdev, *release);
		info->id = idr_ret;
		qxl_release_unmap(qdev, *release, info);

		qxl_bo_unref(&bo);
		return 0;
	}

	return qxl_alloc_release_reserved(qdev, sizeof(struct qxl_surface_cmd),
					 QXL_RELEASE_SURFACE_CMD, release, NULL);
}

int qxl_alloc_release_reserved(struct qxl_device *qdev, unsigned long size,
				       int type, struct qxl_release **release,
				       struct qxl_bo **rbo)
{
	struct qxl_bo *bo;
	int idr_ret;
	int ret = 0;
	union qxl_release_info *info;
	int cur_idx;

	if (type == QXL_RELEASE_DRAWABLE)
		cur_idx = 0;
	else if (type == QXL_RELEASE_SURFACE_CMD)
		cur_idx = 1;
	else if (type == QXL_RELEASE_CURSOR_CMD)
		cur_idx = 2;
	else {
		DRM_ERROR("got illegal type: %d\n", type);
		return -EINVAL;
	}

	idr_ret = qxl_release_alloc(qdev, type, release);

	mutex_lock(&qdev->release_mutex);
	if (qdev->current_release_bo_offset[cur_idx] + 1 >= releases_per_bo[cur_idx]) {
		qxl_bo_unref(&qdev->current_release_bo[cur_idx]);
		qdev->current_release_bo_offset[cur_idx] = 0;
		qdev->current_release_bo[cur_idx] = NULL;
	}
	if (!qdev->current_release_bo[cur_idx]) {
		ret = qxl_release_bo_alloc(qdev, &qdev->current_release_bo[cur_idx]);
		if (ret) {
			mutex_unlock(&qdev->release_mutex);
			return ret;
		}
	}

	bo = qxl_bo_ref(qdev->current_release_bo[cur_idx]);

	(*release)->release_offset = qdev->current_release_bo_offset[cur_idx] * release_size_per_bo[cur_idx];
	qdev->current_release_bo_offset[cur_idx]++;

	if (rbo)
		*rbo = bo;

	mutex_unlock(&qdev->release_mutex);

	qxl_release_list_add(*release, bo);

	info = qxl_release_map(qdev, *release);
	info->id = idr_ret;
	qxl_release_unmap(qdev, *release, info);

	qxl_bo_unref(&bo);
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
	struct qxl_bo_list *entry = list_first_entry(&release->bos, struct qxl_bo_list, tv.head);
	struct qxl_bo *bo = to_qxl_bo(entry->tv.bo);

	ptr = qxl_bo_kmap_atomic_page(qdev, bo, release->release_offset & PAGE_SIZE);
	if (!ptr)
		return NULL;
	info = ptr + (release->release_offset & ~PAGE_SIZE);
	return info;
}

void qxl_release_unmap(struct qxl_device *qdev,
		       struct qxl_release *release,
		       union qxl_release_info *info)
{
	struct qxl_bo_list *entry = list_first_entry(&release->bos, struct qxl_bo_list, tv.head);
	struct qxl_bo *bo = to_qxl_bo(entry->tv.bo);
	void *ptr;

	ptr = ((void *)info) - (release->release_offset & ~PAGE_SIZE);
	qxl_bo_kunmap_atomic_page(qdev, bo, ptr);
}

void qxl_release_fence_buffer_objects(struct qxl_release *release)
{
	struct ttm_validate_buffer *entry;
	struct ttm_buffer_object *bo;
	struct ttm_bo_global *glob;
	struct ttm_bo_device *bdev;
	struct ttm_bo_driver *driver;
	struct qxl_bo *qbo;

	/* if only one object on the release its the release itself
	   since these objects are pinned no need to reserve */
	if (list_is_singular(&release->bos))
		return;

	bo = list_first_entry(&release->bos, struct ttm_validate_buffer, head)->bo;
	bdev = bo->bdev;
	driver = bdev->driver;
	glob = bo->glob;

	spin_lock(&glob->lru_lock);
	spin_lock(&bdev->fence_lock);

	list_for_each_entry(entry, &release->bos, head) {
		bo = entry->bo;
		qbo = to_qxl_bo(bo);

		if (!entry->bo->sync_obj)
			entry->bo->sync_obj = &qbo->fence;

		qxl_fence_add_release_locked(&qbo->fence, release->id);

		ttm_bo_add_to_lru(bo);
		ww_mutex_unlock(&bo->resv->lock);
		entry->reserved = false;
	}
	spin_unlock(&bdev->fence_lock);
	spin_unlock(&glob->lru_lock);
	ww_acquire_fini(&release->ticket);
}

