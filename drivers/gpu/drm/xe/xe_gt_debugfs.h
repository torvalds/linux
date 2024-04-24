/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GT_DEBUGFS_H_
#define _XE_GT_DEBUGFS_H_

struct seq_file;
struct xe_gt;

void xe_gt_debugfs_register(struct xe_gt *gt);
int xe_gt_debugfs_simple_show(struct seq_file *m, void *data);

#endif
