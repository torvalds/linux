/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2024 Intel Corporation
 */

#ifndef __INTEL_WAKELOCK_H__
#define __INTEL_WAKELOCK_H__

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/refcount.h>

#include "i915_reg_defs.h"

struct drm_i915_private;

struct intel_dmc_wl {
	spinlock_t lock; /* protects enabled, taken  and refcount */
	bool enabled;
	bool taken;
	refcount_t refcount;
	struct delayed_work work;
};

void intel_dmc_wl_init(struct drm_i915_private *i915);
void intel_dmc_wl_enable(struct drm_i915_private *i915);
void intel_dmc_wl_disable(struct drm_i915_private *i915);
void intel_dmc_wl_get(struct drm_i915_private *i915, i915_reg_t reg);
void intel_dmc_wl_put(struct drm_i915_private *i915, i915_reg_t reg);

#endif /* __INTEL_WAKELOCK_H__ */
