/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */
#ifndef _XE_DEFAULTS_H_
#define _XE_DEFAULTS_H_

#include "xe_device_types.h"

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define XE_DEFAULT_GUC_LOG_LEVEL		3
#else
#define XE_DEFAULT_GUC_LOG_LEVEL		1
#endif

#define XE_DEFAULT_PROBE_DISPLAY		IS_ENABLED(CONFIG_DRM_XE_DISPLAY)
#define XE_DEFAULT_VRAM_BAR_SIZE		0
#define XE_DEFAULT_FORCE_PROBE			CONFIG_DRM_XE_FORCE_PROBE
#define XE_DEFAULT_MAX_VFS			~0
#define XE_DEFAULT_MAX_VFS_STR			"unlimited"
#define XE_DEFAULT_ADMIN_ONLY_PF		false
#define XE_DEFAULT_WEDGED_MODE			XE_WEDGED_MODE_UPON_CRITICAL_ERROR
#define XE_DEFAULT_WEDGED_MODE_STR		"upon-critical-error"
#define XE_DEFAULT_SVM_NOTIFIER_SIZE		512

#endif
