/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __I915_DRM_CLIENT_H__
#define __I915_DRM_CLIENT_H__

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>

#include <uapi/drm/i915_drm.h>

#define I915_LAST_UABI_ENGINE_CLASS I915_ENGINE_CLASS_COMPUTE

struct drm_i915_private;

struct i915_drm_clients {
	struct drm_i915_private *i915;

	struct xarray xarray;
	u32 next_id;
};

struct i915_drm_client {
	struct kref kref;

	unsigned int id;

	spinlock_t ctx_lock; /* For add/remove from ctx_list. */
	struct list_head ctx_list; /* List of contexts belonging to client. */

	struct i915_drm_clients *clients;

	/**
	 * @past_runtime: Accumulation of pphwsp runtimes from closed contexts.
	 */
	atomic64_t past_runtime[I915_LAST_UABI_ENGINE_CLASS + 1];
};

void i915_drm_clients_init(struct i915_drm_clients *clients,
			   struct drm_i915_private *i915);

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

struct i915_drm_client *i915_drm_client_add(struct i915_drm_clients *clients);

#ifdef CONFIG_PROC_FS
void i915_drm_client_fdinfo(struct seq_file *m, struct file *f);
#endif

void i915_drm_clients_fini(struct i915_drm_clients *clients);

#endif /* !__I915_DRM_CLIENT_H__ */
