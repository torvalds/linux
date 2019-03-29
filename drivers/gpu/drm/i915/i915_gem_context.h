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

#ifndef __I915_GEM_CONTEXT_H__
#define __I915_GEM_CONTEXT_H__

#include "i915_gem_context_types.h"

#include "i915_gem.h"
#include "i915_scheduler.h"
#include "intel_context.h"
#include "intel_device_info.h"

struct drm_device;
struct drm_file;

static inline bool i915_gem_context_is_closed(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_CLOSED, &ctx->flags);
}

static inline void i915_gem_context_set_closed(struct i915_gem_context *ctx)
{
	GEM_BUG_ON(i915_gem_context_is_closed(ctx));
	set_bit(CONTEXT_CLOSED, &ctx->flags);
}

static inline bool i915_gem_context_no_error_capture(const struct i915_gem_context *ctx)
{
	return test_bit(UCONTEXT_NO_ERROR_CAPTURE, &ctx->user_flags);
}

static inline void i915_gem_context_set_no_error_capture(struct i915_gem_context *ctx)
{
	set_bit(UCONTEXT_NO_ERROR_CAPTURE, &ctx->user_flags);
}

static inline void i915_gem_context_clear_no_error_capture(struct i915_gem_context *ctx)
{
	clear_bit(UCONTEXT_NO_ERROR_CAPTURE, &ctx->user_flags);
}

static inline bool i915_gem_context_is_bannable(const struct i915_gem_context *ctx)
{
	return test_bit(UCONTEXT_BANNABLE, &ctx->user_flags);
}

static inline void i915_gem_context_set_bannable(struct i915_gem_context *ctx)
{
	set_bit(UCONTEXT_BANNABLE, &ctx->user_flags);
}

static inline void i915_gem_context_clear_bannable(struct i915_gem_context *ctx)
{
	clear_bit(UCONTEXT_BANNABLE, &ctx->user_flags);
}

static inline bool i915_gem_context_is_recoverable(const struct i915_gem_context *ctx)
{
	return test_bit(UCONTEXT_RECOVERABLE, &ctx->user_flags);
}

static inline void i915_gem_context_set_recoverable(struct i915_gem_context *ctx)
{
	set_bit(UCONTEXT_RECOVERABLE, &ctx->user_flags);
}

static inline void i915_gem_context_clear_recoverable(struct i915_gem_context *ctx)
{
	clear_bit(UCONTEXT_RECOVERABLE, &ctx->user_flags);
}

static inline bool i915_gem_context_is_banned(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_BANNED, &ctx->flags);
}

static inline void i915_gem_context_set_banned(struct i915_gem_context *ctx)
{
	set_bit(CONTEXT_BANNED, &ctx->flags);
}

static inline bool i915_gem_context_force_single_submission(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_FORCE_SINGLE_SUBMISSION, &ctx->flags);
}

static inline void i915_gem_context_set_force_single_submission(struct i915_gem_context *ctx)
{
	__set_bit(CONTEXT_FORCE_SINGLE_SUBMISSION, &ctx->flags);
}

int __i915_gem_context_pin_hw_id(struct i915_gem_context *ctx);
static inline int i915_gem_context_pin_hw_id(struct i915_gem_context *ctx)
{
	if (atomic_inc_not_zero(&ctx->hw_id_pin_count))
		return 0;

	return __i915_gem_context_pin_hw_id(ctx);
}

static inline void i915_gem_context_unpin_hw_id(struct i915_gem_context *ctx)
{
	GEM_BUG_ON(atomic_read(&ctx->hw_id_pin_count) == 0u);
	atomic_dec(&ctx->hw_id_pin_count);
}

static inline bool i915_gem_context_is_kernel(struct i915_gem_context *ctx)
{
	return !ctx->file_priv;
}

/* i915_gem_context.c */
int __must_check i915_gem_contexts_init(struct drm_i915_private *dev_priv);
void i915_gem_contexts_lost(struct drm_i915_private *dev_priv);
void i915_gem_contexts_fini(struct drm_i915_private *dev_priv);

int i915_gem_context_open(struct drm_i915_private *i915,
			  struct drm_file *file);
void i915_gem_context_close(struct drm_file *file);

int i915_switch_context(struct i915_request *rq);
int i915_gem_switch_to_kernel_context(struct drm_i915_private *i915,
				      unsigned long engine_mask);

void i915_gem_context_release(struct kref *ctx_ref);
struct i915_gem_context *
i915_gem_context_create_gvt(struct drm_device *dev);

int i915_gem_vm_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file);
int i915_gem_vm_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file);

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file);
int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file);
int i915_gem_context_getparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int i915_gem_context_setparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
int i915_gem_context_reset_stats_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file);

struct i915_gem_context *
i915_gem_context_create_kernel(struct drm_i915_private *i915, int prio);

static inline struct i915_gem_context *
i915_gem_context_get(struct i915_gem_context *ctx)
{
	kref_get(&ctx->ref);
	return ctx;
}

static inline void i915_gem_context_put(struct i915_gem_context *ctx)
{
	kref_put(&ctx->ref, i915_gem_context_release);
}

struct i915_lut_handle *i915_lut_handle_alloc(void);
void i915_lut_handle_free(struct i915_lut_handle *lut);

#endif /* !__I915_GEM_CONTEXT_H__ */
