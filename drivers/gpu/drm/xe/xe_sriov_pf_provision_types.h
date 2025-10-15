/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_PROVISION_TYPES_H_
#define _XE_SRIOV_PF_PROVISION_TYPES_H_

#include <linux/build_bug.h>

/**
 * enum xe_sriov_provisioning_mode - SR-IOV provisioning mode.
 *
 * @XE_SRIOV_PROVISIONING_MODE_AUTO: VFs are provisioned during VFs enabling.
 *                                   Any allocated resources to the VFs will be
 *                                   automatically released when disabling VFs.
 *                                   This is a default mode.
 * @XE_SRIOV_PROVISIONING_MODE_CUSTOM: Explicit VFs provisioning using uABI interfaces.
 *                                     VFs resources remains allocated regardless if
 *                                     VFs are enabled or not.
 */
enum xe_sriov_provisioning_mode {
	XE_SRIOV_PROVISIONING_MODE_AUTO,
	XE_SRIOV_PROVISIONING_MODE_CUSTOM,
};
static_assert(XE_SRIOV_PROVISIONING_MODE_AUTO == 0);

/**
 * struct xe_sriov_pf_provision - Data used by the PF provisioning.
 */
struct xe_sriov_pf_provision {
	/** @mode: selected provisioning mode. */
	enum xe_sriov_provisioning_mode mode;
};

#endif
