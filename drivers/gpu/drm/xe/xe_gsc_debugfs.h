/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GSC_DEBUGFS_H_
#define _XE_GSC_DEBUGFS_H_

struct dentry;
struct xe_gsc;

void xe_gsc_debugfs_register(struct xe_gsc *gsc, struct dentry *parent);

#endif
