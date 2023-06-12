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

struct i915_drm_client *i915_drm_client_alloc(void)
{
	struct i915_drm_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return NULL;

	kref_init(&client->kref);
	spin_lock_init(&client->ctx_lock);
	INIT_LIST_HEAD(&client->ctx_list);

	return client;
}

void __i915_drm_client_free(struct kref *kref)
{
	struct i915_drm_client *client =
		container_of(kref, typeof(*client), kref);

	kfree(client);
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
show_client_class(struct drm_printer *p,
		  struct drm_i915_private *i915,
		  struct i915_drm_client *client,
		  unsigned int class)
{
	const unsigned int capacity = i915->engine_uabi_class_count[class];
	u64 total = atomic64_read(&client->past_runtime[class]);
	struct i915_gem_context *ctx;

	rcu_read_lock();
	list_for_each_entry_rcu(ctx, &client->ctx_list, client_link)
		total += busy_add(ctx, class);
	rcu_read_unlock();

	if (capacity)
		drm_printf(p, "drm-engine-%s:\t%llu ns\n",
			   uabi_class_names[class], total);

	if (capacity > 1)
		drm_printf(p, "drm-engine-capacity-%s:\t%u\n",
			   uabi_class_names[class],
			   capacity);
}

void i915_drm_client_fdinfo(struct drm_printer *p, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_private *i915 = file_priv->i915;
	unsigned int i;

	/*
	 * ******************************************************************
	 * For text output format description please see drm-usage-stats.rst!
	 * ******************************************************************
	 */

	if (GRAPHICS_VER(i915) < 8)
		return;

	for (i = 0; i < ARRAY_SIZE(uabi_class_names); i++)
		show_client_class(p, i915, file_priv->client, i);
}
#endif
