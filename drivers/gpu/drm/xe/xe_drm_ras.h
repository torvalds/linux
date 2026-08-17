/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */
#ifndef _XE_DRM_RAS_H_
#define _XE_DRM_RAS_H_

struct xe_device;

#define for_each_error_severity(i)	\
	for (i = 0; i < DRM_XE_RAS_ERR_SEV_MAX; i++)

int xe_drm_ras_init(struct xe_device *xe);

#endif
