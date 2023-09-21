// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_print.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

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
	/* show_meminfo() will be developed here */
}
#endif
