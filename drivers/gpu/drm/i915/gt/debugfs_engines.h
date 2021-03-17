/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef DEBUGFS_ENGINES_H
#define DEBUGFS_ENGINES_H

struct intel_gt;
struct dentry;

void debugfs_engines_register(struct intel_gt *gt, struct dentry *root);

#endif /* DEBUGFS_ENGINES_H */
