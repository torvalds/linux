/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2008-2018 Intel Corporation
 */

#ifndef I915_RESET_H
#define I915_RESET_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/srcu.h>

#include "intel_engine_types.h"
#include "intel_reset_types.h"

struct i915_request;
struct intel_engine_cs;
struct intel_gt;
struct intel_guc;

void intel_gt_init_reset(struct intel_gt *gt);
void intel_gt_fini_reset(struct intel_gt *gt);

__printf(4, 5)
void intel_gt_handle_error(struct intel_gt *gt,
			   intel_engine_mask_t engine_mask,
			   unsigned long flags,
			   const char *fmt, ...);
#define I915_ERROR_CAPTURE BIT(0)

bool intel_gt_gpu_reset_clobbers_display(struct intel_gt *gt);

void intel_gt_reset(struct intel_gt *gt,
		    intel_engine_mask_t stalled_mask,
		    const char *reason);
int intel_engine_reset(struct intel_engine_cs *engine,
		       const char *reason);
int __intel_engine_reset_bh(struct intel_engine_cs *engine,
			    const char *reason);

void __i915_request_reset(struct i915_request *rq, bool guilty);

int __must_check intel_gt_reset_trylock(struct intel_gt *gt, int *srcu);
int __must_check intel_gt_reset_lock_interruptible(struct intel_gt *gt, int *srcu);
void intel_gt_reset_unlock(struct intel_gt *gt, int tag);

void intel_gt_set_wedged(struct intel_gt *gt);
bool intel_gt_unset_wedged(struct intel_gt *gt);
int intel_gt_terminally_wedged(struct intel_gt *gt);

/*
 * There's no unset_wedged_on_init paired with this one.
 * Once we're wedged on init, there's no going back.
 * Same thing for unset_wedged_on_fini.
 */
void intel_gt_set_wedged_on_init(struct intel_gt *gt);
void intel_gt_set_wedged_on_fini(struct intel_gt *gt);

int intel_gt_reset_engine(struct intel_engine_cs *engine);
int intel_gt_reset_all_engines(struct intel_gt *gt);

int intel_reset_guc(struct intel_gt *gt);

struct intel_wedge_me {
	struct delayed_work work;
	struct intel_gt *gt;
	const char *name;
};

void __intel_init_wedge(struct intel_wedge_me *w,
			struct intel_gt *gt,
			long timeout,
			const char *name);
void __intel_fini_wedge(struct intel_wedge_me *w);

#define intel_wedge_on_timeout(W, GT, TIMEOUT)				\
	for (__intel_init_wedge((W), (GT), (TIMEOUT), __func__);	\
	     (W)->gt;							\
	     __intel_fini_wedge((W)))

bool intel_has_gpu_reset(const struct intel_gt *gt);
bool intel_has_reset_engine(const struct intel_gt *gt);

bool intel_engine_reset_needs_wa_22011802037(struct intel_gt *gt);

#endif /* I915_RESET_H */
