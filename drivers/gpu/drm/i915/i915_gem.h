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

#ifndef __I915_GEM_H__
#define __I915_GEM_H__

#include <linux/bug.h>
#include <linux/types.h>

#include <drm/drm_drv.h>

#include "i915_utils.h"

struct drm_file;
struct drm_i915_gem_object;
struct drm_i915_private;
struct i915_gem_ww_ctx;
struct i915_gtt_view;
struct i915_vma;

void i915_gem_init_early(struct drm_i915_private *i915);
void i915_gem_cleanup_early(struct drm_i915_private *i915);

void i915_gem_drain_freed_objects(struct drm_i915_private *i915);
void i915_gem_drain_workqueue(struct drm_i915_private *i915);

struct i915_vma * __must_check
i915_gem_object_ggtt_pin_ww(struct drm_i915_gem_object *obj,
			    struct i915_gem_ww_ctx *ww,
			    const struct i915_gtt_view *view,
			    u64 size, u64 alignment, u64 flags);

struct i915_vma * __must_check
i915_gem_object_ggtt_pin(struct drm_i915_gem_object *obj,
			 const struct i915_gtt_view *view,
			 u64 size, u64 alignment, u64 flags);

int i915_gem_object_unbind(struct drm_i915_gem_object *obj,
			   unsigned long flags);
#define I915_GEM_OBJECT_UNBIND_ACTIVE BIT(0)
#define I915_GEM_OBJECT_UNBIND_BARRIER BIT(1)
#define I915_GEM_OBJECT_UNBIND_TEST BIT(2)
#define I915_GEM_OBJECT_UNBIND_VM_TRYLOCK BIT(3)
#define I915_GEM_OBJECT_UNBIND_ASYNC BIT(4)

void i915_gem_runtime_suspend(struct drm_i915_private *i915);

int __must_check i915_gem_init(struct drm_i915_private *i915);
void i915_gem_driver_register(struct drm_i915_private *i915);
void i915_gem_driver_unregister(struct drm_i915_private *i915);
void i915_gem_driver_remove(struct drm_i915_private *i915);
void i915_gem_driver_release(struct drm_i915_private *i915);

int i915_gem_open(struct drm_i915_private *i915, struct drm_file *file);

/* FIXME: All of the below belong somewhere else. */

#ifdef CONFIG_DRM_I915_DEBUG_GEM

#define GEM_SHOW_DEBUG() drm_debug_enabled(DRM_UT_DRIVER)

#ifdef CONFIG_DRM_I915_DEBUG_GEM_ONCE
#define __GEM_BUG(cond) BUG()
#else
#define __GEM_BUG(cond) \
	WARN(1, "%s:%d GEM_BUG_ON(%s)\n", __func__, __LINE__, __stringify(cond))
#endif

#define GEM_BUG_ON(condition) do { if (unlikely((condition))) {	\
		GEM_TRACE_ERR("%s:%d GEM_BUG_ON(%s)\n", \
			      __func__, __LINE__, __stringify(condition)); \
		GEM_TRACE_DUMP(); \
		__GEM_BUG(condition); \
		} \
	} while(0)
#define GEM_WARN_ON(expr) WARN_ON(expr)

#define GEM_DEBUG_WARN_ON(expr) GEM_WARN_ON(expr)

#else

#define GEM_SHOW_DEBUG() (0)

#define GEM_BUG_ON(expr) BUILD_BUG_ON_INVALID(expr)
#define GEM_WARN_ON(expr) ({ unlikely(!!(expr)); })

#define GEM_DEBUG_WARN_ON(expr) ({ BUILD_BUG_ON_INVALID(expr); 0; })
#endif

#if IS_ENABLED(CONFIG_DRM_I915_TRACE_GEM)
#define GEM_TRACE(...) trace_printk(__VA_ARGS__)
#define GEM_TRACE_ERR(...) do {						\
	pr_err(__VA_ARGS__);						\
	trace_printk(__VA_ARGS__);					\
} while (0)
#define GEM_TRACE_DUMP() \
	do { ftrace_dump(DUMP_ALL); __add_taint_for_CI(TAINT_WARN); } while (0)
#define GEM_TRACE_DUMP_ON(expr) \
	do { if (expr) GEM_TRACE_DUMP(); } while (0)
#else
#define GEM_TRACE(...) do { } while (0)
#define GEM_TRACE_ERR(...) do { } while (0)
#define GEM_TRACE_DUMP() do { } while (0)
#define GEM_TRACE_DUMP_ON(expr) BUILD_BUG_ON_INVALID(expr)
#endif

#define I915_GEM_IDLE_TIMEOUT (HZ / 5)

#endif /* __I915_GEM_H__ */
