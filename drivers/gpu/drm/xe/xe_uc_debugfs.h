/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_UC_DEBUGFS_H_
#define _XE_UC_DEBUGFS_H_

struct dentry;
struct xe_uc;

void xe_uc_debugfs_register(struct xe_uc *uc, struct dentry *parent);

#endif
