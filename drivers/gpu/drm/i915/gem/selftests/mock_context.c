/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#include "mock_context.h"
#include "selftests/mock_drm.h"
#include "selftests/mock_gtt.h"

struct i915_gem_context *
mock_context(struct drm_i915_private *i915,
	     const char *name)
{
	struct i915_gem_context *ctx;
	struct i915_gem_engines *e;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	kref_init(&ctx->ref);
	INIT_LIST_HEAD(&ctx->link);
	ctx->i915 = i915;

	spin_lock_init(&ctx->stale.lock);
	INIT_LIST_HEAD(&ctx->stale.engines);

	i915_gem_context_set_persistence(ctx);

	mutex_init(&ctx->engines_mutex);
	e = default_engines(ctx);
	if (IS_ERR(e))
		goto err_free;
	RCU_INIT_POINTER(ctx->engines, e);

	INIT_RADIX_TREE(&ctx->handles_vma, GFP_KERNEL);
	mutex_init(&ctx->mutex);

	if (name) {
		struct i915_ppgtt *ppgtt;

		strncpy(ctx->name, name, sizeof(ctx->name) - 1);

		ppgtt = mock_ppgtt(i915, name);
		if (!ppgtt)
			goto err_put;

		mutex_lock(&ctx->mutex);
		__set_ppgtt(ctx, &ppgtt->vm);
		mutex_unlock(&ctx->mutex);

		i915_vm_put(&ppgtt->vm);
	}

	return ctx;

err_free:
	kfree(ctx);
	return NULL;

err_put:
	i915_gem_context_set_closed(ctx);
	i915_gem_context_put(ctx);
	return NULL;
}

void mock_context_close(struct i915_gem_context *ctx)
{
	context_close(ctx);
}

void mock_init_contexts(struct drm_i915_private *i915)
{
	init_contexts(&i915->gem.contexts);
}

struct i915_gem_context *
live_context(struct drm_i915_private *i915, struct file *file)
{
	struct i915_gem_context *ctx;
	int err;
	u32 id;

	ctx = i915_gem_create_context(i915, 0);
	if (IS_ERR(ctx))
		return ctx;

	i915_gem_context_set_no_error_capture(ctx);

	err = gem_context_register(ctx, to_drm_file(file)->driver_priv, &id);
	if (err < 0)
		goto err_ctx;

	return ctx;

err_ctx:
	context_close(ctx);
	return ERR_PTR(err);
}

struct i915_gem_context *
kernel_context(struct drm_i915_private *i915)
{
	struct i915_gem_context *ctx;

	ctx = i915_gem_create_context(i915, 0);
	if (IS_ERR(ctx))
		return ctx;

	i915_gem_context_clear_bannable(ctx);
	i915_gem_context_set_persistence(ctx);
	i915_gem_context_set_no_error_capture(ctx);

	return ctx;
}

void kernel_context_close(struct i915_gem_context *ctx)
{
	context_close(ctx);
}
