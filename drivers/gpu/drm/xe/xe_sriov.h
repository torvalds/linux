/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_SRIOV_H_
#define _XE_SRIOV_H_

#include "xe_assert.h"
#include "xe_device_types.h"
#include "xe_sriov_types.h"

struct drm_printer;

const char *xe_sriov_mode_to_string(enum xe_sriov_mode mode);
const char *xe_sriov_function_name(unsigned int n, char *buf, size_t len);

void xe_sriov_probe_early(struct xe_device *xe);
void xe_sriov_print_info(struct xe_device *xe, struct drm_printer *p);
int xe_sriov_init(struct xe_device *xe);
int xe_sriov_init_late(struct xe_device *xe);

static inline enum xe_sriov_mode xe_device_sriov_mode(const struct xe_device *xe)
{
	xe_assert(xe, xe->sriov.__mode);
	return xe->sriov.__mode;
}

static inline bool xe_device_is_sriov_pf(const struct xe_device *xe)
{
	return xe_device_sriov_mode(xe) == XE_SRIOV_MODE_PF;
}

static inline bool xe_device_is_sriov_vf(const struct xe_device *xe)
{
	return xe_device_sriov_mode(xe) == XE_SRIOV_MODE_VF;
}

#ifdef CONFIG_PCI_IOV
#define IS_SRIOV_PF(xe) xe_device_is_sriov_pf(xe)
#else
#define IS_SRIOV_PF(xe) (typecheck(struct xe_device *, (xe)) && false)
#endif
#define IS_SRIOV_VF(xe) xe_device_is_sriov_vf(xe)

#define IS_SRIOV(xe) (IS_SRIOV_PF(xe) || IS_SRIOV_VF(xe))

#endif
