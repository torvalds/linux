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

struct intel_display;

struct intel_dmc_wl {
	spinlock_t lock; /* protects enabled, taken  and refcount */
	bool enabled;
	bool taken;
	refcount_t refcount;
	struct delayed_work work;
};

void intel_dmc_wl_init(struct intel_display *display);
void intel_dmc_wl_enable(struct intel_display *display);
void intel_dmc_wl_disable(struct intel_display *display);
void intel_dmc_wl_get(struct intel_display *display, i915_reg_t reg);
void intel_dmc_wl_put(struct intel_display *display, i915_reg_t reg);

#endif /* __INTEL_WAKELOCK_H__ */
