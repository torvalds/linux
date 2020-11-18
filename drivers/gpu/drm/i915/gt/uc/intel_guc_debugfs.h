/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef DEBUGFS_GUC_H
#define DEBUGFS_GUC_H

struct intel_guc;
struct dentry;

void intel_guc_debugfs_register(struct intel_guc *guc, struct dentry *root);

#endif /* DEBUGFS_GUC_H */
