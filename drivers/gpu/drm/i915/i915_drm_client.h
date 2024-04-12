/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __I915_DRM_CLIENT_H__
#define __I915_DRM_CLIENT_H__

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <uapi/drm/i915_drm.h>

#include "i915_file_private.h"
#include "gem/i915_gem_object_types.h"
#include "gt/intel_context_types.h"

#define I915_LAST_UABI_ENGINE_CLASS I915_ENGINE_CLASS_COMPUTE

struct drm_file;
struct drm_printer;

struct i915_drm_client {
	struct kref kref;

	spinlock_t ctx_lock; /* For add/remove from ctx_list. */
	struct list_head ctx_list; /* List of contexts belonging to client. */

#ifdef CONFIG_PROC_FS
	/**
	 * @objects_lock: lock protecting @objects_list
	 */
	spinlock_t objects_lock;

	/**
	 * @objects_list: list of objects created by this client
	 *
	 * Protected by @objects_lock.
	 */
	struct list_head objects_list;
#endif

	/**
	 * @past_runtime: Accumulation of pphwsp runtimes from closed contexts.
	 */
	atomic64_t past_runtime[I915_LAST_UABI_ENGINE_CLASS + 1];
};

static inline struct i915_drm_client *
i915_drm_client_get(struct i915_drm_client *client)
{
	kref_get(&client->kref);
	return client;
}

void __i915_drm_client_free(struct kref *kref);

static inline void i915_drm_client_put(struct i915_drm_client *client)
{
	kref_put(&client->kref, __i915_drm_client_free);
}

struct i915_drm_client *i915_drm_client_alloc(void);

void i915_drm_client_fdinfo(struct drm_printer *p, struct drm_file *file);

#ifdef CONFIG_PROC_FS
void i915_drm_client_add_object(struct i915_drm_client *client,
				struct drm_i915_gem_object *obj);
void i915_drm_client_remove_object(struct drm_i915_gem_object *obj);
void i915_drm_client_add_context_objects(struct i915_drm_client *client,
					 struct intel_context *ce);
#else
static inline void i915_drm_client_add_object(struct i915_drm_client *client,
					      struct drm_i915_gem_object *obj)
{
}

static inline void
i915_drm_client_remove_object(struct drm_i915_gem_object *obj)
{
}

static inline void
i915_drm_client_add_context_objects(struct i915_drm_client *client,
				    struct intel_context *ce)
{
}
#endif

#endif /* !__I915_DRM_CLIENT_H__ */
