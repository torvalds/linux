/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_HELPERS_H_
#define _XE_SRIOV_PF_HELPERS_H_

#include "xe_assert.h"
#include "xe_device_types.h"
#include "xe_sriov.h"
#include "xe_sriov_types.h"

/**
 * xe_sriov_pf_assert_vfid() - warn if &id is not a supported VF number when debugging.
 * @xe: the PF &xe_device to assert on
 * @vfid: the VF number to assert
 *
 * Assert that &xe represents the Physical Function (PF) device and provided &vfid
 * is within a range of supported VF numbers (up to maximum number of VFs that
 * driver can support, including VF0 that represents the PF itself).
 *
 * Note: Effective only on debug builds. See `Xe ASSERTs`_ for more information.
 */
#define xe_sriov_pf_assert_vfid(xe, vfid) \
	xe_assert((xe), (vfid) <= xe_sriov_pf_get_totalvfs(xe))

/**
 * xe_sriov_pf_get_totalvfs() - Get maximum number of VFs that driver can support.
 * @xe: the &xe_device to query (shall be PF)
 *
 * Return: Maximum number of VFs that this PF driver supports.
 */
static inline int xe_sriov_pf_get_totalvfs(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	return xe->sriov.pf.driver_max_vfs;
}

static inline struct mutex *xe_sriov_pf_master_mutex(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_PF(xe));
	return &xe->sriov.pf.master_lock;
}

#endif
