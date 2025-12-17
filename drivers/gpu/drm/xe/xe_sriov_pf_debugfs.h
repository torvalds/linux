/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_DEBUGFS_H_
#define _XE_SRIOV_PF_DEBUGFS_H_

struct dentry;
struct xe_device;

#ifdef CONFIG_PCI_IOV
void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root);
#else
static inline void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root) { }
#endif

#endif
