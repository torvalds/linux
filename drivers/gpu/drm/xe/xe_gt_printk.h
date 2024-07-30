/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_PRINTK_H_
#define _XE_GT_PRINTK_H_

#include <drm/drm_print.h>

#include "xe_device_types.h"

#define xe_gt_printk(_gt, _level, _fmt, ...) \
	drm_##_level(&gt_to_xe(_gt)->drm, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define xe_gt_err_once(_gt, _fmt, ...) \
	xe_gt_printk((_gt), err_once, _fmt, ##__VA_ARGS__)

#define xe_gt_err(_gt, _fmt, ...) \
	xe_gt_printk((_gt), err, _fmt, ##__VA_ARGS__)

#define xe_gt_warn(_gt, _fmt, ...) \
	xe_gt_printk((_gt), warn, _fmt, ##__VA_ARGS__)

#define xe_gt_notice(_gt, _fmt, ...) \
	xe_gt_printk((_gt), notice, _fmt, ##__VA_ARGS__)

#define xe_gt_info(_gt, _fmt, ...) \
	xe_gt_printk((_gt), info, _fmt, ##__VA_ARGS__)

#define xe_gt_dbg(_gt, _fmt, ...) \
	xe_gt_printk((_gt), dbg, _fmt, ##__VA_ARGS__)

#define xe_gt_err_ratelimited(_gt, _fmt, ...) \
	xe_gt_printk((_gt), err_ratelimited, _fmt, ##__VA_ARGS__)

#define xe_gt_WARN(_gt, _condition, _fmt, ...) \
	drm_WARN(&gt_to_xe(_gt)->drm, _condition, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define xe_gt_WARN_ONCE(_gt, _condition, _fmt, ...) \
	drm_WARN_ONCE(&gt_to_xe(_gt)->drm, _condition, "GT%u: " _fmt, (_gt)->info.id, ##__VA_ARGS__)

#define xe_gt_WARN_ON(_gt, _condition) \
	xe_gt_WARN((_gt), _condition, "%s(%s)", "gt_WARN_ON", __stringify(_condition))

#define xe_gt_WARN_ON_ONCE(_gt, _condition) \
	xe_gt_WARN_ONCE((_gt), _condition, "%s(%s)", "gt_WARN_ON_ONCE", __stringify(_condition))

static inline void __xe_gt_printfn_err(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_gt *gt = p->arg;

	xe_gt_err(gt, "%pV", vaf);
}

static inline void __xe_gt_printfn_info(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_gt *gt = p->arg;

	xe_gt_info(gt, "%pV", vaf);
}

/**
 * xe_gt_err_printer - Construct a &drm_printer that outputs to xe_gt_err()
 * @gt: the &xe_gt pointer to use in xe_gt_err()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_gt_err_printer(struct xe_gt *gt)
{
	struct drm_printer p = {
		.printfn = __xe_gt_printfn_err,
		.arg = gt,
	};
	return p;
}

/**
 * xe_gt_info_printer - Construct a &drm_printer that outputs to xe_gt_info()
 * @gt: the &xe_gt pointer to use in xe_gt_info()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_gt_info_printer(struct xe_gt *gt)
{
	struct drm_printer p = {
		.printfn = __xe_gt_printfn_info,
		.arg = gt,
	};
	return p;
}

#endif
