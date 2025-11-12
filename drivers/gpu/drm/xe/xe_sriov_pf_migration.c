// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_sriov.h"
#include "xe_sriov_pf_migration.h"

/**
 * xe_sriov_pf_migration_supported() - Check if SR-IOV VF migration is supported by the device
 * @xe: the &xe_device
 *
 * Return: true if migration is supported, false otherwise
 */
bool xe_sriov_pf_migration_supported(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	return xe->sriov.pf.migration.supported;
}

static bool pf_check_migration_support(struct xe_device *xe)
{
	/* XXX: for now this is for feature enabling only */
	return IS_ENABLED(CONFIG_DRM_XE_DEBUG);
}

/**
 * xe_sriov_pf_migration_init() - Initialize support for SR-IOV VF migration.
 * @xe: the &xe_device
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_sriov_pf_migration_init(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));

	xe->sriov.pf.migration.supported = pf_check_migration_support(xe);

	return 0;
}
