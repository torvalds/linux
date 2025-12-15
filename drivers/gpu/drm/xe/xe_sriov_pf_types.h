/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_TYPES_H_
#define _XE_SRIOV_PF_TYPES_H_

#include <linux/mutex.h>
#include <linux/types.h>

#include "xe_guard.h"
#include "xe_sriov_pf_migration_types.h"
#include "xe_sriov_pf_provision_types.h"
#include "xe_sriov_pf_service_types.h"

struct kobject;

/**
 * struct xe_sriov_metadata - per-VF device level metadata
 */
struct xe_sriov_metadata {
	/** @kobj: kobject representing VF in PF's SR-IOV sysfs tree. */
	struct kobject *kobj;

	/** @version: negotiated VF/PF ABI version */
	struct xe_sriov_pf_service_version version;
	/** @migration: migration state */
	struct xe_sriov_migration_state migration;
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

	/** @guard_vfs_enabling: guards VFs enabling */
	struct xe_guard guard_vfs_enabling;

	/** @master_lock: protects all VFs configurations across GTs */
	struct mutex master_lock;

	/** @provision: device level provisioning data. */
	struct xe_sriov_pf_provision provision;

	/** @migration: device level migration data. */
	struct xe_sriov_pf_migration migration;

	/** @service: device level service data. */
	struct xe_sriov_pf_service service;

	/** @sysfs: device level sysfs data. */
	struct {
		/** @sysfs.root: the root kobject for all SR-IOV entries in sysfs. */
		struct kobject *root;
	} sysfs;

	/** @vfs: metadata for all VFs. */
	struct xe_sriov_metadata *vfs;
};

#endif
