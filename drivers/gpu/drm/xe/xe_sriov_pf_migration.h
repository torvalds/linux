/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_MIGRATION_H_
#define _XE_SRIOV_PF_MIGRATION_H_

#include <linux/types.h>

struct xe_device;

int xe_sriov_pf_migration_init(struct xe_device *xe);
bool xe_sriov_pf_migration_supported(struct xe_device *xe);

#endif
