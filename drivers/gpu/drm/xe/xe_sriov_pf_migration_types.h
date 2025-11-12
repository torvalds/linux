/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_MIGRATION_TYPES_H_
#define _XE_SRIOV_PF_MIGRATION_TYPES_H_

#include <linux/types.h>

/**
 * struct xe_sriov_pf_migration - Xe device level VF migration data
 */
struct xe_sriov_pf_migration {
	/** @supported: indicates whether VF migration feature is supported */
	bool supported;
};

#endif
