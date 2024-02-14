/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_SRIOV_TYPES_H_
#define _XE_SRIOV_TYPES_H_

#include <linux/build_bug.h>

/**
 * enum xe_sriov_mode - SR-IOV mode
 * @XE_SRIOV_MODE_NONE: bare-metal mode (non-virtualized)
 * @XE_SRIOV_MODE_PF: SR-IOV Physical Function (PF) mode
 * @XE_SRIOV_MODE_VF: SR-IOV Virtual Function (VF) mode
 */
enum xe_sriov_mode {
	/*
	 * Note: We don't use default enum value 0 to allow catch any too early
	 * attempt of checking the SR-IOV mode prior to the actual mode probe.
	 */
	XE_SRIOV_MODE_NONE = 1,
	XE_SRIOV_MODE_PF,
	XE_SRIOV_MODE_VF,
};
static_assert(XE_SRIOV_MODE_NONE);

#endif
