/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_GT_PRINT__
#define __INTEL_GT_PRINT__

#include <drm/drm_print.h>
#include "intel_gt_types.h"
#include "i915_utils.h"

#define gt_err(_gt, _fmt, ...) \
	drm_err(&(_gt)->i915->drm, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_warn(_gt, _fmt, ...) \
	drm_warn(&(_gt)->i915->drm, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_notice(_gt, _fmt, ...) \
	drm_notice(&(_gt)->i915->drm, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_info(_gt, _fmt, ...) \
	drm_info(&(_gt)->i915->drm, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_dbg(_gt, _fmt, ...) \
	drm_dbg(&(_gt)->i915->drm, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_err_ratelimited(_gt, _fmt, ...) \
	drm_err_ratelimited(&(_gt)->i915->drm, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_probe_error(_gt, _fmt, ...) \
	do { \
		if (i915_error_injected()) \
			gt_dbg(_gt, _fmt, ##__VA_ARGS__); \
		else \
			gt_err(_gt, _fmt, ##__VA_ARGS__); \
	} while (0)

#define gt_WARN(_gt, _condition, _fmt, ...) \
	drm_WARN(&(_gt)->i915->drm, _condition, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_WARN_ONCE(_gt, _condition, _fmt, ...) \
	drm_WARN_ONCE(&(_gt)->i915->drm, _condition, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define gt_WARN_ON(_gt, _condition) \
	gt_WARN(_gt, _condition, "%s", "gt_WARN_ON(" __stringify(_condition) ")")

#define gt_WARN_ON_ONCE(_gt, _condition) \
	gt_WARN_ONCE(_gt, _condition, "%s", "gt_WARN_ONCE(" __stringify(_condition) ")")

#endif /* __INTEL_GT_PRINT_H__ */
