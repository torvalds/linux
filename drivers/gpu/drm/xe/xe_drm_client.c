// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_print.h>
#include <drm/xe_drm.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "xe_bo.h"
#include "xe_bo_types.h"
#include "xe_device_types.h"
#include "xe_drm_client.h"
#include "xe_trace.h"

/**
 * xe_drm_client_alloc() - Allocate drm client
 * @void: No arg
 *
 * Allocate drm client struct to track client memory against
 * same till client life. Call this API whenever new client
 * has opened xe device.
 *
 * Return: pointer to client struct or NULL if can't allocate
 */
struct xe_drm_client *xe_drm_client_alloc(void)
{
	struct xe_drm_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	kref_init(&client->kref);

#ifdef CONFIG_PROC_FS
	spin_lock_init(&client->bos_lock);
	INIT_LIST_HEAD(&client->bos_list);
#endif
	return client;
}

/**
 * __xe_drm_client_free() - Free client struct
 * @kref: The reference
 *
 * This frees client struct. Call this API when xe device is closed
 * by drm client.
 *
 * Return: void
 */
void __xe_drm_client_free(struct kref *kref)
{
	struct xe_drm_client *client =
		container_of(kref, typeof(*client), kref);

	kfree(client);
}

#ifdef CONFIG_PROC_FS
/**
 * xe_drm_client_add_bo() - Add BO for tracking client mem usage
 * @client: The drm client ptr
 * @bo: The xe BO ptr
 *
 * Add all BO created by individual drm client by calling this function.
 * This helps in tracking client memory usage.
 *
 * Return: void
 */
void xe_drm_client_add_bo(struct xe_drm_client *client,
			  struct xe_bo *bo)
{
	XE_WARN_ON(bo->client);
	XE_WARN_ON(!list_empty(&bo->client_link));

	spin_lock(&client->bos_lock);
	bo->client = xe_drm_client_get(client);
	list_add_tail_rcu(&bo->client_link, &client->bos_list);
	spin_unlock(&client->bos_lock);
}

/**
 * xe_drm_client_remove_bo() - Remove BO for tracking client mem usage
 * @bo: The xe BO ptr
 *
 * Remove all BO removed by individual drm client by calling this function.
 * This helps in tracking client memory usage.
 *
 * Return: void
 */
void xe_drm_client_remove_bo(struct xe_bo *bo)
{
	struct xe_drm_client *client = bo->client;

	spin_lock(&client->bos_lock);
	list_del_rcu(&bo->client_link);
	spin_unlock(&client->bos_lock);

	xe_drm_client_put(client);
}

static void bo_meminfo(struct xe_bo *bo,
		       struct drm_memory_stats stats[TTM_NUM_MEM_TYPES])
{
	u64 sz = bo->size;
	u32 mem_type;

	if (bo->placement.placement)
		mem_type = bo->placement.placement->mem_type;
	else
		mem_type = XE_PL_TT;

	if (bo->ttm.base.handle_count > 1)
		stats[mem_type].shared += sz;
	else
		stats[mem_type].private += sz;

	if (xe_bo_has_pages(bo)) {
		stats[mem_type].resident += sz;

		if (!dma_resv_test_signaled(bo->ttm.base.resv,
					    DMA_RESV_USAGE_BOOKKEEP))
			stats[mem_type].active += sz;
		else if (mem_type == XE_PL_SYSTEM)
			stats[mem_type].purgeable += sz;
	}
}

static void show_meminfo(struct drm_printer *p, struct drm_file *file)
{
	struct drm_memory_stats stats[TTM_NUM_MEM_TYPES] = {};
	struct xe_file *xef = file->driver_priv;
	struct ttm_device *bdev = &xef->xe->ttm;
	struct ttm_resource_manager *man;
	struct xe_drm_client *client;
	struct drm_gem_object *obj;
	struct xe_bo *bo;
	unsigned int id;
	u32 mem_type;

	client = xef->client;

	/* Public objects. */
	spin_lock(&file->table_lock);
	idr_for_each_entry(&file->object_idr, obj, id) {
		struct xe_bo *bo = gem_to_xe_bo(obj);

		bo_meminfo(bo, stats);
	}
	spin_unlock(&file->table_lock);

	/* Internal objects. */
	spin_lock(&client->bos_lock);
	list_for_each_entry_rcu(bo, &client->bos_list, client_link) {
		if (!bo || !kref_get_unless_zero(&bo->ttm.base.refcount))
			continue;
		bo_meminfo(bo, stats);
		xe_bo_put(bo);
	}
	spin_unlock(&client->bos_lock);

	for (mem_type = XE_PL_SYSTEM; mem_type < TTM_NUM_MEM_TYPES; ++mem_type) {
		if (!xe_mem_type_to_name[mem_type])
			continue;

		man = ttm_manager_type(bdev, mem_type);

		if (man) {
			drm_print_memory_stats(p,
					       &stats[mem_type],
					       DRM_GEM_OBJECT_RESIDENT |
					       (mem_type != XE_PL_SYSTEM ? 0 :
					       DRM_GEM_OBJECT_PURGEABLE),
					       xe_mem_type_to_name[mem_type]);
		}
	}
}

/**
 * xe_drm_client_fdinfo() - Callback for fdinfo interface
 * @p: The drm_printer ptr
 * @file: The drm_file ptr
 *
 * This is callabck for drm fdinfo interface. Register this callback
 * in drm driver ops for show_fdinfo.
 *
 * Return: void
 */
void xe_drm_client_fdinfo(struct drm_printer *p, struct drm_file *file)
{
	show_meminfo(p, file);
}
#endif
