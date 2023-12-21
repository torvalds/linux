/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_BB_TYPES_H_
#define _XE_BB_TYPES_H_

#include <linux/types.h>

struct drm_suballoc;

struct xe_bb {
	struct drm_suballoc *bo;

	u32 *cs;
	u32 len; /* in dwords */
};

#endif
