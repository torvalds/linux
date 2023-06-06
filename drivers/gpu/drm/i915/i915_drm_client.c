// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <uapi/drm/i915_drm.h>

#include <drm/drm_print.h>

#include "gem/i915_gem_context.h"
#include "i915_drm_client.h"
#include "i915_file_private.h"
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

#ifdef CONFIG_PROC_FS
static const char * const uabi_class_names[] = {
	[I915_ENGINE_CLASS_RENDER] = "render",
	[I915_ENGINE_CLASS_COPY] = "copy",
	[I915_ENGINE_CLASS_VIDEO] = "video",
	[I915_ENGINE_CLASS_VIDEO_ENHANCE] = "video-enhance",
	[I915_ENGINE_CLASS_COMPUTE] = "compute",
};

static u64 busy_add(struct i915_gem_context *ctx, unsigned int class)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;
	u64 total = 0;

	for_each_gem_engine(ce, rcu_dereference(ctx->engines), it) {
		if (ce->engine->uabi_class != class)
			continue;

		total += intel_context_get_total_runtime_ns(ce);
	}

	return total;
}

static void
show_client_class(struct seq_file *m,
		  struct i915_drm_client *client,
		  unsigned int class)
{
	const struct list_head *list = &client->ctx_list;
	u64 total = atomic64_read(&client->past_runtime[class]);
	const unsigned int capacity =
		client->clients->i915->engine_uabi_class_count[class];
	struct i915_gem_context *ctx;

	rcu_read_lock();
	list_for_each_entry_rcu(ctx, list, client_link)
		total += busy_add(ctx, class);
	rcu_read_unlock();

	if (capacity)
		seq_printf(m, "drm-engine-%s:\t%llu ns\n",
			   uabi_class_names[class], total);

	if (capacity > 1)
		seq_printf(m, "drm-engine-capacity-%s:\t%u\n",
			   uabi_class_names[class],
			   capacity);
}

void i915_drm_client_fdinfo(struct seq_file *m, struct file *f)
{
	struct drm_file *file = f->private_data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_private *i915 = file_priv->i915;
	struct i915_drm_client *client = file_priv->client;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	unsigned int i;

	/*
	 * ******************************************************************
	 * For text output format description please see drm-usage-stats.rst!
	 * ******************************************************************
	 */

	seq_printf(m, "drm-driver:\t%s\n", i915->drm.driver->name);
	seq_printf(m, "drm-pdev:\t%04x:%02x:%02x.%d\n",
		   pci_domain_nr(pdev->bus), pdev->bus->number,
		   PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
	seq_printf(m, "drm-client-id:\t%u\n", client->id);

	/*
	 * Temporarily skip showing client engine information with GuC submission till
	 * fetching engine busyness is implemented in the GuC submission backend
	 */
	if (GRAPHICS_VER(i915) < 8 || intel_uc_uses_guc_submission(&i915->gt0.uc))
		return;

	for (i = 0; i < ARRAY_SIZE(uabi_class_names); i++)
		show_client_class(m, client, i);
}
#endif
