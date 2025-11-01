/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_PRINTK_H_
#define _XE_PRINTK_H_

#include <drm/drm_print.h>

#include "xe_device_types.h"

#define __XE_PRINTK_FMT(_xe, _fmt, _args...)	_fmt, ##_args

#define xe_printk(_xe, _level, _fmt, ...) \
	drm_##_level(&(_xe)->drm, __XE_PRINTK_FMT((_xe), _fmt, ## __VA_ARGS__))

#define xe_err(_xe, _fmt, ...) \
	xe_printk((_xe), err, _fmt, ##__VA_ARGS__)

#define xe_err_once(_xe, _fmt, ...) \
	xe_printk((_xe), err_once, _fmt, ##__VA_ARGS__)

#define xe_err_ratelimited(_xe, _fmt, ...) \
	xe_printk((_xe), err_ratelimited, _fmt, ##__VA_ARGS__)

#define xe_warn(_xe, _fmt, ...) \
	xe_printk((_xe), warn, _fmt, ##__VA_ARGS__)

#define xe_notice(_xe, _fmt, ...) \
	xe_printk((_xe), notice, _fmt, ##__VA_ARGS__)

#define xe_info(_xe, _fmt, ...) \
	xe_printk((_xe), info, _fmt, ##__VA_ARGS__)

#define xe_dbg(_xe, _fmt, ...) \
	xe_printk((_xe), dbg, _fmt, ##__VA_ARGS__)

#define xe_WARN_type(_xe, _type, _condition, _fmt, ...) \
	drm_WARN##_type(&(_xe)->drm, _condition, _fmt, ## __VA_ARGS__)

#define xe_WARN(_xe, _condition, _fmt, ...) \
	xe_WARN_type((_xe),, _condition, __XE_PRINTK_FMT((_xe), _fmt, ## __VA_ARGS__))

#define xe_WARN_ONCE(_xe, _condition, _fmt, ...) \
	xe_WARN_type((_xe), _ONCE, _condition, __XE_PRINTK_FMT((_xe), _fmt, ## __VA_ARGS__))

#define xe_WARN_ON(_xe, _condition) \
	xe_WARN((_xe), _condition, "%s(%s)", "WARN_ON", __stringify(_condition))

#define xe_WARN_ON_ONCE(_xe, _condition) \
	xe_WARN_ONCE((_xe), _condition, "%s(%s)", "WARN_ON_ONCE", __stringify(_condition))

static inline void __xe_printfn_err(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_device *xe = p->arg;

	xe_err(xe, "%pV", vaf);
}

static inline void __xe_printfn_info(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_device *xe = p->arg;

	xe_info(xe, "%pV", vaf);
}

static inline void __xe_printfn_dbg(struct drm_printer *p, struct va_format *vaf)
{
	struct xe_device *xe = p->arg;
	struct drm_printer ddp;

	/*
	 * The original xe_dbg() callsite annotations are useless here,
	 * redirect to the tweaked drm_dbg_printer() instead.
	 */
	ddp = drm_dbg_printer(&xe->drm, DRM_UT_DRIVER, NULL);
	ddp.origin = p->origin;

	drm_printf(&ddp, __XE_PRINTK_FMT(xe, "%pV", vaf));
}

/**
 * xe_err_printer - Construct a &drm_printer that outputs to xe_err()
 * @xe: the &xe_device pointer to use in xe_err()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_err_printer(struct xe_device *xe)
{
	struct drm_printer p = {
		.printfn = __xe_printfn_err,
		.arg = xe,
	};
	return p;
}

/**
 * xe_info_printer - Construct a &drm_printer that outputs to xe_info()
 * @xe: the &xe_device pointer to use in xe_info()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_info_printer(struct xe_device *xe)
{
	struct drm_printer p = {
		.printfn = __xe_printfn_info,
		.arg = xe,
	};
	return p;
}

/**
 * xe_dbg_printer - Construct a &drm_printer that outputs like xe_dbg()
 * @xe: the &xe_device pointer to use in xe_dbg()
 *
 * Return: The &drm_printer object.
 */
static inline struct drm_printer xe_dbg_printer(struct xe_device *xe)
{
	struct drm_printer p = {
		.printfn = __xe_printfn_dbg,
		.arg = xe,
		.origin = (const void *)_THIS_IP_,
	};
	return p;
}

#endif
