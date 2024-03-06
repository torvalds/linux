/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_GGTT_H_
#define _XE_GGTT_H_

#include "xe_ggtt_types.h"

struct drm_printer;

void xe_ggtt_set_pte(struct xe_ggtt *ggtt, u64 addr, u64 pte);
void xe_ggtt_invalidate(struct xe_ggtt *ggtt);
int xe_ggtt_init_early(struct xe_ggtt *ggtt);
int xe_ggtt_init(struct xe_ggtt *ggtt);
void xe_ggtt_printk(struct xe_ggtt *ggtt, const char *prefix);

int xe_ggtt_insert_special_node(struct xe_ggtt *ggtt, struct drm_mm_node *node,
				u32 size, u32 align);
int xe_ggtt_insert_special_node_locked(struct xe_ggtt *ggtt,
				       struct drm_mm_node *node,
				       u32 size, u32 align, u32 mm_flags);
void xe_ggtt_remove_node(struct xe_ggtt *ggtt, struct drm_mm_node *node);
void xe_ggtt_map_bo(struct xe_ggtt *ggtt, struct xe_bo *bo);
int xe_ggtt_insert_bo(struct xe_ggtt *ggtt, struct xe_bo *bo);
int xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
			 u64 start, u64 end);
void xe_ggtt_remove_bo(struct xe_ggtt *ggtt, struct xe_bo *bo);

int xe_ggtt_dump(struct xe_ggtt *ggtt, struct drm_printer *p);

#endif
