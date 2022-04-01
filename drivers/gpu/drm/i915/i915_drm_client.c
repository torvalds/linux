// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "i915_drm_client.h"
#include "i915_gem.h"
#include "i915_utils.h"

void i915_drm_clients_init(struct i915_drm_clients *clients,
			   struct drm_i915_private *i915)
{
	clients->i915 = i915;
	clients->next_id = 0;

	xa_init_flags(&clients->xarray, XA_FLAGS_ALLOC | XA_FLAGS_LOCK_IRQ);
}

struct i915_drm_client *i915_drm_client_add(struct i915_drm_clients *clients)
{
	struct i915_drm_client *client;
	struct xarray *xa = &clients->xarray;
	int ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	xa_lock_irq(xa);
	ret = __xa_alloc_cyclic(xa, &client->id, client, xa_limit_32b,
				&clients->next_id, GFP_KERNEL);
	xa_unlock_irq(xa);
	if (ret < 0)
		goto err;

	kref_init(&client->kref);
	spin_lock_init(&client->ctx_lock);
	INIT_LIST_HEAD(&client->ctx_list);
	client->clients = clients;

	return client;

err:
	kfree(client);

	return ERR_PTR(ret);
}

void __i915_drm_client_free(struct kref *kref)
{
	struct i915_drm_client *client =
		container_of(kref, typeof(*client), kref);
	struct xarray *xa = &client->clients->xarray;
	unsigned long flags;

	xa_lock_irqsave(xa, flags);
	__xa_erase(xa, client->id);
	xa_unlock_irqrestore(xa, flags);
	kfree(client);
}

void i915_drm_clients_fini(struct i915_drm_clients *clients)
{
	GEM_BUG_ON(!xa_empty(&clients->xarray));
	xa_destroy(&clients->xarray);
}
