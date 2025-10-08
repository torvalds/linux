/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_SERVICE_TYPES_H_
#define _XE_SRIOV_PF_SERVICE_TYPES_H_

#include <linux/types.h>

/**
 * struct xe_sriov_pf_service_version - VF/PF ABI Version.
 * @major: the major version of the VF/PF ABI
 * @minor: the minor version of the VF/PF ABI
 *
 * See `GuC Relay Communication`_.
 */
struct xe_sriov_pf_service_version {
	u16 major;
	u16 minor;
};

/**
 * struct xe_sriov_pf_service - Data used by the PF service.
 * @version: information about VF/PF ABI versions for current platform.
 * @version.base: lowest VF/PF ABI version that could be negotiated with VF.
 * @version.latest: latest VF/PF ABI version supported by the PF driver.
 */
struct xe_sriov_pf_service {
	struct {
		struct xe_sriov_pf_service_version base;
		struct xe_sriov_pf_service_version latest;
	} version;
};

#endif
