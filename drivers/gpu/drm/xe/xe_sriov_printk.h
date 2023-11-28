/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_SRIOV_PRINTK_H_
#define _XE_SRIOV_PRINTK_H_

#include <drm/drm_print.h>

#include "xe_device_types.h"
#include "xe_sriov_types.h"

#define xe_sriov_printk_prefix(xe) \
	((xe)->sriov.__mode == XE_SRIOV_MODE_PF ? "PF: " : \
	 (xe)->sriov.__mode == XE_SRIOV_MODE_VF ? "VF: " : "")

#define xe_sriov_printk(xe, _level, fmt, ...) \
	drm_##_level(&(xe)->drm, "%s" fmt, xe_sriov_printk_prefix(xe), ##__VA_ARGS__)

#define xe_sriov_err(xe, fmt, ...) \
	xe_sriov_printk((xe), err, fmt, ##__VA_ARGS__)

#define xe_sriov_err_ratelimited(xe, fmt, ...) \
	xe_sriov_printk((xe), err_ratelimited, fmt, ##__VA_ARGS__)

#define xe_sriov_warn(xe, fmt, ...) \
	xe_sriov_printk((xe), warn, fmt, ##__VA_ARGS__)

#define xe_sriov_notice(xe, fmt, ...) \
	xe_sriov_printk((xe), notice, fmt, ##__VA_ARGS__)

#define xe_sriov_info(xe, fmt, ...) \
	xe_sriov_printk((xe), info, fmt, ##__VA_ARGS__)

#define xe_sriov_dbg(xe, fmt, ...) \
	xe_sriov_printk((xe), dbg, fmt, ##__VA_ARGS__)

/* for low level noisy debug messages */
#ifdef CONFIG_DRM_XE_DEBUG_SRIOV
#define xe_sriov_dbg_verbose(xe, fmt, ...) xe_sriov_dbg(xe, fmt, ##__VA_ARGS__)
#else
#define xe_sriov_dbg_verbose(xe, fmt, ...) typecheck(struct xe_device *, (xe))
#endif

#endif
