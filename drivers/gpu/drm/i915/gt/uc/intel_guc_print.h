/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_GUC_PRINT__
#define __INTEL_GUC_PRINT__

#include "gt/intel_gt.h"
#include "gt/intel_gt_print.h"

#define guc_printk(_guc, _level, _fmt, ...) \
	gt_##_level(guc_to_gt(_guc), "GUC: " _fmt, ##__VA_ARGS__)

#define guc_err(_guc, _fmt, ...) \
	guc_printk((_guc), err, _fmt, ##__VA_ARGS__)

#define guc_warn(_guc, _fmt, ...) \
	guc_printk((_guc), warn, _fmt, ##__VA_ARGS__)

#define guc_notice(_guc, _fmt, ...) \
	guc_printk((_guc), notice, _fmt, ##__VA_ARGS__)

#define guc_info(_guc, _fmt, ...) \
	guc_printk((_guc), info, _fmt, ##__VA_ARGS__)

#define guc_dbg(_guc, _fmt, ...) \
	guc_printk((_guc), dbg, _fmt, ##__VA_ARGS__)

#define guc_err_ratelimited(_guc, _fmt, ...) \
	guc_printk((_guc), err_ratelimited, _fmt, ##__VA_ARGS__)

#define guc_probe_error(_guc, _fmt, ...) \
	guc_printk((_guc), probe_error, _fmt, ##__VA_ARGS__)

#define guc_WARN(_guc, _cond, _fmt, ...) \
	gt_WARN(guc_to_gt(_guc), _cond, "GUC: " _fmt, ##__VA_ARGS__)

#define guc_WARN_ONCE(_guc, _cond, _fmt, ...) \
	gt_WARN_ONCE(guc_to_gt(_guc), _cond, "GUC: " _fmt, ##__VA_ARGS__)

#define guc_WARN_ON(_guc, _cond) \
	gt_WARN(guc_to_gt(_guc), _cond, "%s(%s)", "guc_WARN_ON", __stringify(_cond))

#define guc_WARN_ON_ONCE(_guc, _cond) \
	gt_WARN_ONCE(guc_to_gt(_guc), _cond, "%s(%s)", "guc_WARN_ON_ONCE", __stringify(_cond))

#endif /* __INTEL_GUC_PRINT__ */
