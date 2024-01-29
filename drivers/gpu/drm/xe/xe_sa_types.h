/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */
#ifndef _XE_SA_TYPES_H_
#define _XE_SA_TYPES_H_

#include <drm/drm_suballoc.h>

struct xe_bo;

struct xe_sa_manager {
	struct drm_suballoc_manager base;
	struct xe_bo *bo;
	u64 gpu_addr;
	void *cpu_ptr;
};

#endif
