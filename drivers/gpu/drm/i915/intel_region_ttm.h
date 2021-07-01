/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef _INTEL_REGION_TTM_H_
#define _INTEL_REGION_TTM_H_

#include <linux/types.h>

#include "i915_selftest.h"

struct drm_i915_private;
struct intel_memory_region;
struct ttm_resource;

int intel_region_ttm_device_init(struct drm_i915_private *dev_priv);

void intel_region_ttm_device_fini(struct drm_i915_private *dev_priv);

int intel_region_ttm_init(struct intel_memory_region *mem);

void intel_region_ttm_fini(struct intel_memory_region *mem);

struct sg_table *intel_region_ttm_node_to_st(struct intel_memory_region *mem,
					     struct ttm_resource *res);

struct ttm_resource *
intel_region_ttm_node_alloc(struct intel_memory_region *mem,
			    resource_size_t size,
			    unsigned int flags);

void intel_region_ttm_node_free(struct intel_memory_region *mem,
				struct ttm_resource *node);
#endif /* _INTEL_REGION_TTM_H_ */
