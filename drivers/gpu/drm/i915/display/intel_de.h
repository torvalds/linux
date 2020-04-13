/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DE_H__
#define __INTEL_DE_H__

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_uncore.h"

static inline u32
intel_de_read(struct drm_i915_private *i915, i915_reg_t reg)
{
	return intel_uncore_read(&i915->uncore, reg);
}

static inline void
intel_de_posting_read(struct drm_i915_private *i915, i915_reg_t reg)
{
	intel_uncore_posting_read(&i915->uncore, reg);
}

/* Note: read the warnings for intel_uncore_*_fw() functions! */
static inline u32
intel_de_read_fw(struct drm_i915_private *i915, i915_reg_t reg)
{
	return intel_uncore_read_fw(&i915->uncore, reg);
}

static inline void
intel_de_write(struct drm_i915_private *i915, i915_reg_t reg, u32 val)
{
	intel_uncore_write(&i915->uncore, reg, val);
}

/* Note: read the warnings for intel_uncore_*_fw() functions! */
static inline void
intel_de_write_fw(struct drm_i915_private *i915, i915_reg_t reg, u32 val)
{
	intel_uncore_write_fw(&i915->uncore, reg, val);
}

static inline void
intel_de_rmw(struct drm_i915_private *i915, i915_reg_t reg, u32 clear, u32 set)
{
	intel_uncore_rmw(&i915->uncore, reg, clear, set);
}

static inline int
intel_de_wait_for_register(struct drm_i915_private *i915, i915_reg_t reg,
			   u32 mask, u32 value, unsigned int timeout)
{
	return intel_wait_for_register(&i915->uncore, reg, mask, value, timeout);
}

static inline int
intel_de_wait_for_set(struct drm_i915_private *i915, i915_reg_t reg,
		      u32 mask, unsigned int timeout)
{
	return intel_de_wait_for_register(i915, reg, mask, mask, timeout);
}

static inline int
intel_de_wait_for_clear(struct drm_i915_private *i915, i915_reg_t reg,
			u32 mask, unsigned int timeout)
{
	return intel_de_wait_for_register(i915, reg, mask, 0, timeout);
}

#endif /* __INTEL_DE_H__ */
