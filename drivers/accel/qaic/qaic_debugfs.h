/* SPDX-License-Identifier: GPL-2.0-only */

/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#ifndef __QAIC_DEBUGFS_H__
#define __QAIC_DEBUGFS_H__

#include <drm/drm_file.h>

#ifdef CONFIG_DEBUG_FS
int qaic_bootlog_register(void);
void qaic_bootlog_unregister(void);
void qaic_debugfs_init(struct qaic_drm_device *qddev);
#else
static inline int qaic_bootlog_register(void) { return 0; }
static inline void qaic_bootlog_unregister(void) {}
static inline void qaic_debugfs_init(struct qaic_drm_device *qddev) {}
#endif /* CONFIG_DEBUG_FS */
#endif /* __QAIC_DEBUGFS_H__ */
