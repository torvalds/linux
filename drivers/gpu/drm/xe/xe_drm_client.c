// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_print.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "xe_device_types.h"
#include "xe_drm_client.h"

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
