/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GGTT_TYPES_H_
#define _XE_GGTT_TYPES_H_

#include <linux/types.h>
#include <drm/drm_mm.h>

struct xe_ggtt;
struct xe_ggtt_node;

typedef void (*xe_ggtt_set_pte_fn)(struct xe_ggtt *ggtt, u64 addr, u64 pte);
typedef void (*xe_ggtt_transform_cb)(struct xe_ggtt *ggtt,
				     struct xe_ggtt_node *node,
				     u64 pte_flags,
				     xe_ggtt_set_pte_fn set_pte, void *arg);

#endif
