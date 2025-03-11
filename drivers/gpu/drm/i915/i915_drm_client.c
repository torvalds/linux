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
#ifdef CONFIG_PROC_FS
	spin_lock_init(&client->objects_lock);
	INIT_LIST_HEAD(&client->objects_list);
#endif

	return client;
}

void __i915_drm_client_free(struct kref *kref)
{
	struct i915_drm_client *client =
		container_of(kref, typeof(*client), kref);

	kfree(client);
}

#ifdef CONFIG_PROC_FS
static void
obj_meminfo(struct drm_i915_gem_object *obj,
	    struct drm_memory_stats stats[INTEL_REGION_UNKNOWN])
{
	const enum intel_region_id id = obj->mm.region ?
					obj->mm.region->id : INTEL_REGION_SMEM;
	const u64 sz = obj->base.size;

	if (drm_gem_object_is_shared_for_memory_stats(&obj->base))
		stats[id].shared += sz;
	else
		stats[id].private += sz;

	if (i915_gem_object_has_pages(obj)) {
		stats[id].resident += sz;

		if (!dma_resv_test_signaled(obj->base.resv,
					    DMA_RESV_USAGE_BOOKKEEP))
			stats[id].active += sz;
		else if (i915_gem_object_is_shrinkable(obj) &&
			 obj->mm.madv == I915_MADV_DONTNEED)
			stats[id].purgeable += sz;
	}
}

static void show_meminfo(struct drm_printer *p, struct drm_file *file)
{
	struct drm_memory_stats stats[INTEL_REGION_UNKNOWN] = {};
	struct drm_i915_file_private *fpriv = file->driver_priv;
	struct i915_drm_client *client = fpriv->client;
	struct drm_i915_private *i915 = fpriv->i915;
	struct drm_i915_gem_object *obj;
	struct intel_memory_region *mr;
	struct list_head __rcu *pos;
	unsigned int id;

	/* Public objects. */
	spin_lock(&file->table_lock);
	idr_for_each_entry(&file->object_idr, obj, id)
		obj_meminfo(obj, stats);
	spin_unlock(&file->table_lock);

	/* Internal objects. */
	rcu_read_lock();
	list_for_each_rcu(pos, &client->objects_list) {
		obj = i915_gem_object_get_rcu(list_entry(pos, typeof(*obj),
							 client_link));
		if (!obj)
			continue;
		obj_meminfo(obj, stats);
		i915_gem_object_put(obj);
	}
	rcu_read_unlock();

	for_each_memory_region(mr, i915, id)
		drm_print_memory_stats(p,
				       &stats[id],
				       DRM_GEM_OBJECT_ACTIVE |
				       DRM_GEM_OBJECT_RESIDENT |
				       DRM_GEM_OBJECT_PURGEABLE,
				       mr->uabi_name);
}

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

	show_meminfo(p, file);

	if (GRAPHICS_VER(i915) < 8)
		return;

	for (i = 0; i < ARRAY_SIZE(uabi_class_names); i++)
		show_client_class(p, i915, file_priv->client, i);
}

void i915_drm_client_add_object(struct i915_drm_client *client,
				struct drm_i915_gem_object *obj)
{
	unsigned long flags;

	GEM_WARN_ON(obj->client);
	GEM_WARN_ON(!list_empty(&obj->client_link));

	spin_lock_irqsave(&client->objects_lock, flags);
	obj->client = i915_drm_client_get(client);
	list_add_tail_rcu(&obj->client_link, &client->objects_list);
	spin_unlock_irqrestore(&client->objects_lock, flags);
}

void i915_drm_client_remove_object(struct drm_i915_gem_object *obj)
{
	struct i915_drm_client *client = fetch_and_zero(&obj->client);
	unsigned long flags;

	/* Object may not be associated with a client. */
	if (!client)
		return;

	spin_lock_irqsave(&client->objects_lock, flags);
	list_del_rcu(&obj->client_link);
	spin_unlock_irqrestore(&client->objects_lock, flags);

	i915_drm_client_put(client);
}

void i915_drm_client_add_context_objects(struct i915_drm_client *client,
					 struct intel_context *ce)
{
	if (ce->state)
		i915_drm_client_add_object(client, ce->state->obj);

	if (ce->ring != ce->engine->legacy.ring && ce->ring->vma)
		i915_drm_client_add_object(client, ce->ring->vma->obj);
}
#endif
