/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_DEBUGFS_H_
#define _XE_DEBUGFS_H_

#include <linux/types.h>

struct xe_device;

#ifdef CONFIG_DEBUG_FS
bool xe_fault_gt_reset(void);
bool xe_fault_csc_hw_error(void);
void xe_debugfs_register(struct xe_device *xe);
#else
static inline bool xe_fault_gt_reset(void) { return false; }
static inline bool xe_fault_csc_hw_error(void) { return false; }
static inline void xe_debugfs_register(struct xe_device *xe) { }
#endif

#endif
