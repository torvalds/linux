/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __I915_GEM_GTT_H__
#define __I915_GEM_GTT_H__

#include <linux/io-mapping.h>
#include <linux/types.h>

#include <drm/drm_mm.h>

#include "gt/intel_gtt.h"
#include "i915_scatterlist.h"

struct drm_i915_gem_object;
struct i915_address_space;

int __must_check i915_gem_gtt_prepare_pages(struct drm_i915_gem_object *obj,
					    struct sg_table *pages);
void i915_gem_gtt_finish_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *pages);

int i915_gem_gtt_reserve(struct i915_address_space *vm,
			 struct drm_mm_node *node,
			 u64 size, u64 offset, unsigned long color,
			 unsigned int flags);

int i915_gem_gtt_insert(struct i915_address_space *vm,
			struct drm_mm_node *node,
			u64 size, u64 alignment, unsigned long color,
			u64 start, u64 end, unsigned int flags);

/* Flags used by pin/bind&friends. */
#define PIN_NOEVICT		BIT_ULL(0)
#define PIN_NOSEARCH		BIT_ULL(1)
#define PIN_NONBLOCK		BIT_ULL(2)
#define PIN_MAPPABLE		BIT_ULL(3)
#define PIN_ZONE_4G		BIT_ULL(4)
#define PIN_HIGH		BIT_ULL(5)
#define PIN_OFFSET_BIAS		BIT_ULL(6)
#define PIN_OFFSET_FIXED	BIT_ULL(7)

#define PIN_UPDATE		BIT_ULL(9)
#define PIN_GLOBAL		BIT_ULL(10) /* I915_VMA_GLOBAL_BIND */
#define PIN_USER		BIT_ULL(11) /* I915_VMA_LOCAL_BIND */

#define PIN_OFFSET_MASK		I915_GTT_PAGE_MASK

#endif
