/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef DEBUGFS_UC_H
#define DEBUGFS_UC_H

struct intel_uc;
struct dentry;

void intel_uc_debugfs_register(struct intel_uc *uc, struct dentry *gt_root);

#endif /* DEBUGFS_UC_H */
