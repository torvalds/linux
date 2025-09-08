/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_GGTT_H_
#define _XE_GGTT_H_

#include "xe_ggtt_types.h"

struct drm_printer;
struct xe_tile;
struct drm_exec;

struct xe_ggtt *xe_ggtt_alloc(struct xe_tile *tile);
int xe_ggtt_init_early(struct xe_ggtt *ggtt);
int xe_ggtt_init_kunit(struct xe_ggtt *ggtt, u32 reserved, u32 size);
int xe_ggtt_init(struct xe_ggtt *ggtt);

struct xe_ggtt_node *xe_ggtt_node_init(struct xe_ggtt *ggtt);
void xe_ggtt_node_fini(struct xe_ggtt_node *node);
int xe_ggtt_node_insert_balloon_locked(struct xe_ggtt_node *node,
				       u64 start, u64 size);
void xe_ggtt_node_remove_balloon_locked(struct xe_ggtt_node *node);
void xe_ggtt_shift_nodes_locked(struct xe_ggtt *ggtt, s64 shift);

int xe_ggtt_node_insert(struct xe_ggtt_node *node, u32 size, u32 align);
int xe_ggtt_node_insert_locked(struct xe_ggtt_node *node,
			       u32 size, u32 align, u32 mm_flags);
void xe_ggtt_node_remove(struct xe_ggtt_node *node, bool invalidate);
bool xe_ggtt_node_allocated(const struct xe_ggtt_node *node);
void xe_ggtt_map_bo(struct xe_ggtt *ggtt, struct xe_ggtt_node *node,
		    struct xe_bo *bo, u16 pat_index);
void xe_ggtt_map_bo_unlocked(struct xe_ggtt *ggtt, struct xe_bo *bo);
int xe_ggtt_insert_bo(struct xe_ggtt *ggtt, struct xe_bo *bo, struct drm_exec *exec);
int xe_ggtt_insert_bo_at(struct xe_ggtt *ggtt, struct xe_bo *bo,
			 u64 start, u64 end, struct drm_exec *exec);
void xe_ggtt_remove_bo(struct xe_ggtt *ggtt, struct xe_bo *bo);
u64 xe_ggtt_largest_hole(struct xe_ggtt *ggtt, u64 alignment, u64 *spare);

int xe_ggtt_dump(struct xe_ggtt *ggtt, struct drm_printer *p);
u64 xe_ggtt_print_holes(struct xe_ggtt *ggtt, u64 alignment, struct drm_printer *p);

#ifdef CONFIG_PCI_IOV
void xe_ggtt_assign(const struct xe_ggtt_node *node, u16 vfid);
#endif

#ifndef CONFIG_LOCKDEP
static inline void xe_ggtt_might_lock(struct xe_ggtt *ggtt)
{ }
#else
void xe_ggtt_might_lock(struct xe_ggtt *ggtt);
#endif

u64 xe_ggtt_encode_pte_flags(struct xe_ggtt *ggtt, struct xe_bo *bo, u16 pat_index);
u64 xe_ggtt_read_pte(struct xe_ggtt *ggtt, u64 offset);

#endif
