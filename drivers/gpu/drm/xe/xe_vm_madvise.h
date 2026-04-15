/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_VM_MADVISE_H_
#define _XE_VM_MADVISE_H_

struct drm_device;
struct drm_file;
struct xe_bo;

int xe_vm_madvise_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file);

void xe_bo_recompute_purgeable_state(struct xe_bo *bo);

#endif
