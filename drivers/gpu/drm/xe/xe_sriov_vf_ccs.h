/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_VF_CCS_H_
#define _XE_SRIOV_VF_CCS_H_

#include "xe_device_types.h"
#include "xe_sriov.h"
#include "xe_sriov_vf_ccs_types.h"

struct drm_printer;
struct xe_device;
struct xe_bo;

int xe_sriov_vf_ccs_init(struct xe_device *xe);
int xe_sriov_vf_ccs_attach_bo(struct xe_bo *bo);
int xe_sriov_vf_ccs_detach_bo(struct xe_bo *bo);
int xe_sriov_vf_ccs_register_context(struct xe_device *xe);
void xe_sriov_vf_ccs_print(struct xe_device *xe, struct drm_printer *p);

static inline bool xe_sriov_vf_ccs_ready(struct xe_device *xe)
{
	xe_assert(xe, IS_SRIOV_VF(xe));
	return xe->sriov.vf.ccs.initialized;
}

#define IS_VF_CCS_READY(xe) ({ \
	struct xe_device *xe__ = (xe); \
	IS_SRIOV_VF(xe__) && xe_sriov_vf_ccs_ready(xe__); \
	})

#endif
