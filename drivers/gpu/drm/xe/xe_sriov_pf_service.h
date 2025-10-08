/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_SERVICE_H_
#define _XE_SRIOV_PF_SERVICE_H_

#include <linux/types.h>

struct drm_printer;
struct xe_device;

void xe_sriov_pf_service_init(struct xe_device *xe);
void xe_sriov_pf_service_print_versions(struct xe_device *xe, struct drm_printer *p);

int xe_sriov_pf_service_handshake_vf(struct xe_device *xe, u32 vfid,
				     u32 wanted_major, u32 wanted_minor,
				     u32 *major, u32 *minor);
bool xe_sriov_pf_service_is_negotiated(struct xe_device *xe, u32 vfid, u32 major, u32 minor);
void xe_sriov_pf_service_reset_vf(struct xe_device *xe, unsigned int vfid);

#endif
