/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_PM_DEBUGFS_H
#define INTEL_GT_PM_DEBUGFS_H

struct intel_gt;
struct dentry;
struct drm_printer;

void intel_gt_pm_debugfs_register(struct intel_gt *gt, struct dentry *root);
void intel_gt_pm_frequency_dump(struct intel_gt *gt, struct drm_printer *m);

#endif /* INTEL_GT_PM_DEBUGFS_H */
