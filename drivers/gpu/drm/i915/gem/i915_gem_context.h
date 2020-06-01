/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016 Intel Corporation
 */

#ifndef __I915_GEM_CONTEXT_H__
#define __I915_GEM_CONTEXT_H__

#include "i915_gem_context_types.h"

#include "gt/intel_context.h"

#include "i915_drv.h"
#include "i915_gem.h"
#include "i915_scheduler.h"
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

static inline bool i915_gem_context_is_persistent(const struct i915_gem_context *ctx)
{
	return test_bit(UCONTEXT_PERSISTENCE, &ctx->user_flags);
}

static inline void i915_gem_context_set_persistence(struct i915_gem_context *ctx)
{
	set_bit(UCONTEXT_PERSISTENCE, &ctx->user_flags);
}

static inline void i915_gem_context_clear_persistence(struct i915_gem_context *ctx)
{
	clear_bit(UCONTEXT_PERSISTENCE, &ctx->user_flags);
}

static inline bool
i915_gem_context_user_engines(const struct i915_gem_context *ctx)
{
	return test_bit(CONTEXT_USER_ENGINES, &ctx->flags);
}

static inline void
i915_gem_context_set_user_engines(struct i915_gem_context *ctx)
{
	set_bit(CONTEXT_USER_ENGINES, &ctx->flags);
}

static inline void
i915_gem_context_clear_user_engines(struct i915_gem_context *ctx)
{
	clear_bit(CONTEXT_USER_ENGINES, &ctx->flags);
}

/* i915_gem_context.c */
void i915_gem_init__contexts(struct drm_i915_private *i915);
void i915_gem_driver_release__contexts(struct drm_i915_private *i915);

int i915_gem_context_open(struct drm_i915_private *i915,
			  struct drm_file *file);
void i915_gem_context_close(struct drm_file *file);

void i915_gem_context_release(struct kref *ctx_ref);

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

static inline struct i915_address_space *
i915_gem_context_vm(struct i915_gem_context *ctx)
{
	return rcu_dereference_protected(ctx->vm, lockdep_is_held(&ctx->mutex));
}

static inline struct i915_address_space *
i915_gem_context_get_vm_rcu(struct i915_gem_context *ctx)
{
	struct i915_address_space *vm;

	rcu_read_lock();
	vm = rcu_dereference(ctx->vm);
	if (!vm)
		vm = &ctx->i915->ggtt.vm;
	vm = i915_vm_get(vm);
	rcu_read_unlock();

	return vm;
}

static inline struct i915_gem_engines *
i915_gem_context_engines(struct i915_gem_context *ctx)
{
	return rcu_dereference_protected(ctx->engines,
					 lockdep_is_held(&ctx->engines_mutex));
}

static inline struct i915_gem_engines *
i915_gem_context_lock_engines(struct i915_gem_context *ctx)
	__acquires(&ctx->engines_mutex)
{
	mutex_lock(&ctx->engines_mutex);
	return i915_gem_context_engines(ctx);
}

static inline void
i915_gem_context_unlock_engines(struct i915_gem_context *ctx)
	__releases(&ctx->engines_mutex)
{
	mutex_unlock(&ctx->engines_mutex);
}

static inline struct intel_context *
i915_gem_context_get_engine(struct i915_gem_context *ctx, unsigned int idx)
{
	struct intel_context *ce;

	rcu_read_lock(); {
		struct i915_gem_engines *e = rcu_dereference(ctx->engines);
		if (unlikely(!e)) /* context was closed! */
			ce = ERR_PTR(-ENOENT);
		else if (likely(idx < e->num_engines && e->engines[idx]))
			ce = intel_context_get(e->engines[idx]);
		else
			ce = ERR_PTR(-EINVAL);
	} rcu_read_unlock();

	return ce;
}

static inline void
i915_gem_engines_iter_init(struct i915_gem_engines_iter *it,
			   struct i915_gem_engines *engines)
{
	it->engines = engines;
	it->idx = 0;
}

struct intel_context *
i915_gem_engines_iter_next(struct i915_gem_engines_iter *it);

#define for_each_gem_engine(ce, engines, it) \
	for (i915_gem_engines_iter_init(&(it), (engines)); \
	     ((ce) = i915_gem_engines_iter_next(&(it)));)

struct i915_lut_handle *i915_lut_handle_alloc(void);
void i915_lut_handle_free(struct i915_lut_handle *lut);

#endif /* !__I915_GEM_CONTEXT_H__ */
