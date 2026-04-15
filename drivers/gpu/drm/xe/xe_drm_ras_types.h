/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_DRM_RAS_TYPES_H_
#define _XE_DRM_RAS_TYPES_H_

#include <linux/atomic.h>
#include <drm/xe_drm.h>

struct drm_ras_node;

/* Error categories reported by hardware */
enum hardware_error {
	HARDWARE_ERROR_CORRECTABLE = 0,
	HARDWARE_ERROR_NONFATAL,
	HARDWARE_ERROR_FATAL,
	HARDWARE_ERROR_MAX
};

/**
 * struct xe_drm_ras_counter - XE RAS counter
 *
 * This structure contains error component and counter information
 */
struct xe_drm_ras_counter {
	/** @name: error component name */
	const char *name;

	/** @counter: count of error */
	atomic_t counter;
};

/**
 * struct xe_drm_ras - XE DRM RAS structure
 *
 * This structure has details of error counters
 */
struct xe_drm_ras {
	/** @node: DRM RAS node */
	struct drm_ras_node *node;

	/** @info: info array for all types of errors */
	struct xe_drm_ras_counter *info[DRM_XE_RAS_ERR_SEV_MAX];
};

#endif
