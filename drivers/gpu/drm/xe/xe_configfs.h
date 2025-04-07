/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */
#ifndef _XE_CONFIGFS_H_
#define _XE_CONFIGFS_H_

#if IS_ENABLED(CONFIG_CONFIGFS_FS)
int xe_configfs_init(void);
void xe_configfs_exit(void);
#else
static inline int xe_configfs_init(void) { return 0; };
static inline void xe_configfs_exit(void) {};
#endif

#endif
