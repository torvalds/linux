/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "ttm/ttm_execbuf_util.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/module.h>

void ttm_eu_backoff_reservation(struct list_head *list)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;
		if (!entry->reserved)
			continue;

		entry->reserved = false;
		ttm_bo_unreserve(bo);
	}
}
EXPORT_SYMBOL(ttm_eu_backoff_reservation);

/*
 * Reserve buffers for validation.
 *
 * If a buffer in the list is marked for CPU access, we back off and
 * wait for that buffer to become free for GPU access.
 *
 * If a buffer is reserved for another validation, the validator with
 * the highest validation sequence backs off and waits for that buffer
 * to become unreserved. This prevents deadlocks when validating multiple
 * buffers in different orders.
 */

int ttm_eu_reserve_buffers(struct list_head *list, uint32_t val_seq)
{
	struct ttm_validate_buffer *entry;
	int ret;

retry:
	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;

		entry->reserved = false;
		ret = ttm_bo_reserve(bo, true, false, true, val_seq);
		if (ret != 0) {
			ttm_eu_backoff_reservation(list);
			if (ret == -EAGAIN) {
				ret = ttm_bo_wait_unreserved(bo, true);
				if (unlikely(ret != 0))
					return ret;
				goto retry;
			} else
				return ret;
		}

		entry->reserved = true;
		if (unlikely(atomic_read(&bo->cpu_writers) > 0)) {
			ttm_eu_backoff_reservation(list);
			ret = ttm_bo_wait_cpu(bo, false);
			if (ret)
				return ret;
			goto retry;
		}
	}
	return 0;
}
EXPORT_SYMBOL(ttm_eu_reserve_buffers);

void ttm_eu_fence_buffer_objects(struct list_head *list, void *sync_obj)
{
	struct ttm_validate_buffer *entry;

	list_for_each_entry(entry, list, head) {
		struct ttm_buffer_object *bo = entry->bo;
		struct ttm_bo_driver *driver = bo->bdev->driver;
		void *old_sync_obj;

		spin_lock(&bo->lock);
		old_sync_obj = bo->sync_obj;
		bo->sync_obj = driver->sync_obj_ref(sync_obj);
		bo->sync_obj_arg = entry->new_sync_obj_arg;
		spin_unlock(&bo->lock);
		ttm_bo_unreserve(bo);
		entry->reserved = false;
		if (old_sync_obj)
			driver->sync_obj_unref(&old_sync_obj);
	}
}
EXPORT_SYMBOL(ttm_eu_fence_buffer_objects);
