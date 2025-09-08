/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_H_
#define _XE_SRIOV_PF_H_

#include <linux/types.h>

struct dentry;
struct drm_printer;
struct xe_device;

#ifdef CONFIG_PCI_IOV
bool xe_sriov_pf_readiness(struct xe_device *xe);
int xe_sriov_pf_init_early(struct xe_device *xe);
int xe_sriov_pf_wait_ready(struct xe_device *xe);
void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root);
void xe_sriov_pf_print_vfs_summary(struct xe_device *xe, struct drm_printer *p);
#else
static inline bool xe_sriov_pf_readiness(struct xe_device *xe)
{
	return false;
}

static inline int xe_sriov_pf_init_early(struct xe_device *xe)
{
	return 0;
}

static inline void xe_sriov_pf_debugfs_register(struct xe_device *xe, struct dentry *root)
{
}
#endif

#endif
