/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_TYPES_H_
#define _XE_SRIOV_PF_TYPES_H_

#include <linux/mutex.h>
#include <linux/types.h>

#include "xe_sriov_pf_service_types.h"

/**
 * struct xe_sriov_metadata - per-VF device level metadata
 */
struct xe_sriov_metadata {
	/** @version: negotiated VF/PF ABI version */
	struct xe_sriov_pf_service_version version;
};

/**
 * struct xe_device_pf - Xe PF related data
 *
 * The data in this structure is valid only if driver is running in the
 * @XE_SRIOV_MODE_PF mode.
 */
struct xe_device_pf {
	/** @device_total_vfs: Maximum number of VFs supported by the device. */
	u16 device_total_vfs;

	/** @driver_max_vfs: Maximum number of VFs supported by the driver. */
	u16 driver_max_vfs;

	/** @master_lock: protects all VFs configurations across GTs */
	struct mutex master_lock;

	/** @service: device level service data. */
	struct xe_sriov_pf_service service;

	/** @vfs: metadata for all VFs. */
	struct xe_sriov_metadata *vfs;
};

#endif
