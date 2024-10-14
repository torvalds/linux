/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_GGTT_H_
#define _XE_GGTT_H_

#include "xe_ggtt_types.h"

struct drm_printer;

int xe_ggtt_init_early(struct xe_ggtt *ggtt);
int xe_ggtt_init(struct xe_ggtt *ggtt);

struct xe_ggtt_node *xe_ggtt_node_init(struct xe_ggtt *ggtt);
void xe_ggtt_node_fini(struct xe_ggtt_node *node);
int xe_ggtt_node_insert_balloon(struct xe_ggtt_node *node,
				u64 start, u64 size);
void xe_ggtt_node_remove_balloon(struct xe_ggtt_node *node);

int xe_ggtt_node_insert(struct xe_ggtt_node *node, u32 size, u32 align);
int xe_ggtt_node_insert_locked(struct xe_ggtt_node *node,
			       u32 size, u32 align, u32 mm_flags);
void xe_ggtt_node_remove(struct xe_ggtt_node *node, bool invalidate);
bool xe_ggtt_node_allocated(const struct xe_ggtt_node *node);
void xe_ggtt_map_bo(struct xe_ggtt *ggtt, struct xe_bo *bo);
int xe_ggtt_insert_bo(struct xe_ggtt *ggtt, struct xe_bo *bo);
int xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
			 u64 start, u64 end);
void xe_ggtt_remove_bo(struct xe_ggtt *ggtt, struct xe_bo *bo);
u64 xe_ggtt_largest_hole(struct xe_ggtt *ggtt, u64 alignment, u64 *spare);

int xe_ggtt_dump(struct xe_ggtt *ggtt, struct drm_printer *p);
u64 xe_ggtt_print_holes(struct xe_ggtt *ggtt, u64 alignment, struct drm_printer *p);

#ifdef CONFIG_PCI_IOV
void xe_ggtt_assign(const struct xe_ggtt_node *node, u16 vfid);
#endif

#endif
