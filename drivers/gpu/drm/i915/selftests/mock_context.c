/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "mock_context.h"
#include "mock_gtt.h"

struct i915_gem_context *
mock_context(struct drm_i915_private *i915,
	     const char *name)
{
	struct i915_gem_context *ctx;
	unsigned int n;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	kref_init(&ctx->ref);
	INIT_LIST_HEAD(&ctx->link);
	ctx->i915 = i915;

	INIT_RADIX_TREE(&ctx->handles_vma, GFP_KERNEL);
	INIT_LIST_HEAD(&ctx->handles_list);
	INIT_LIST_HEAD(&ctx->hw_id_link);

	for (n = 0; n < ARRAY_SIZE(ctx->__engine); n++)
		intel_context_init(&ctx->__engine[n], ctx, i915->engine[n]);

	ret = i915_gem_context_pin_hw_id(ctx);
	if (ret < 0)
		goto err_handles;

	if (name) {
		ctx->name = kstrdup(name, GFP_KERNEL);
		if (!ctx->name)
			goto err_put;

		ctx->ppgtt = mock_ppgtt(i915, name);
		if (!ctx->ppgtt)
			goto err_put;
	}

	return ctx;

err_handles:
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
	init_contexts(i915);
}

struct i915_gem_context *
live_context(struct drm_i915_private *i915, struct drm_file *file)
{
	lockdep_assert_held(&i915->drm.struct_mutex);

	return i915_gem_create_context(i915, file->driver_priv);
}

struct i915_gem_context *
kernel_context(struct drm_i915_private *i915)
{
	return i915_gem_context_create_kernel(i915, I915_PRIORITY_NORMAL);
}

void kernel_context_close(struct i915_gem_context *ctx)
{
	context_close(ctx);
}
