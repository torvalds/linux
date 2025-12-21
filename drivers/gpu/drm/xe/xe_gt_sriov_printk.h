/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PRINTK_H_
#define _XE_GT_SRIOV_PRINTK_H_

#include "xe_gt_printk.h"
#include "xe_tile_sriov_printk.h"

#define __XE_GT_SRIOV_PRINTK_FMT(_gt, _fmt, ...) \
	__XE_TILE_SRIOV_PRINTK_FMT((_gt)->tile, __XE_GT_PRINTK_FMT((_gt), _fmt, ##__VA_ARGS__))

#define __xe_gt_sriov_printk(gt, _level, fmt, ...) \
	xe_sriov_##_level(gt_to_xe(gt), __XE_GT_SRIOV_PRINTK_FMT((gt), fmt, ##__VA_ARGS__))

#define xe_gt_sriov_err(_gt, _fmt, ...) \
	__xe_gt_sriov_printk(_gt, err, _fmt, ##__VA_ARGS__)

#define xe_gt_sriov_notice(_gt, _fmt, ...) \
	__xe_gt_sriov_printk(_gt, notice, _fmt, ##__VA_ARGS__)

#define xe_gt_sriov_info(_gt, _fmt, ...) \
	__xe_gt_sriov_printk(_gt, info, _fmt, ##__VA_ARGS__)

#define xe_gt_sriov_dbg(_gt, _fmt, ...) \
	__xe_gt_sriov_printk(_gt, dbg, _fmt, ##__VA_ARGS__)

/* for low level noisy debug messages */
#ifdef CONFIG_DRM_XE_DEBUG_SRIOV
#define xe_gt_sriov_dbg_verbose(_gt, _fmt, ...) xe_gt_sriov_dbg(_gt, _fmt, ##__VA_ARGS__)
#else
#define xe_gt_sriov_dbg_verbose(_gt, _fmt, ...) typecheck(struct xe_gt *, (_gt))
#endif

#endif
