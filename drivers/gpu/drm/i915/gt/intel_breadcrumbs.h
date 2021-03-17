/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_BREADCRUMBS__
#define __INTEL_BREADCRUMBS__

#include <linux/irq_work.h>

#include "intel_engine_types.h"

struct drm_printer;
struct i915_request;
struct intel_breadcrumbs;

struct intel_breadcrumbs *
intel_breadcrumbs_create(struct intel_engine_cs *irq_engine);
void intel_breadcrumbs_free(struct intel_breadcrumbs *b);

void intel_breadcrumbs_reset(struct intel_breadcrumbs *b);
void intel_breadcrumbs_park(struct intel_breadcrumbs *b);

static inline void
intel_engine_signal_breadcrumbs(struct intel_engine_cs *engine)
{
	irq_work_queue(&engine->breadcrumbs->irq_work);
}

void intel_engine_print_breadcrumbs(struct intel_engine_cs *engine,
				    struct drm_printer *p);

bool i915_request_enable_breadcrumb(struct i915_request *request);
void i915_request_cancel_breadcrumb(struct i915_request *request);

#endif /* __INTEL_BREADCRUMBS__ */
