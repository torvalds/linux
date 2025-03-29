/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#include "../../i915/i915_gtt_view_types.h"

/* Partial view not supported in xe, fail build if used. */
#define I915_GTT_VIEW_PARTIAL
