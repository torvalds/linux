/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_BO_TYPES_H_
#define _XE_BO_TYPES_H_

#include <linux/iosys-map.h>

#include <drm/drm_mm.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_placement.h>

struct xe_device;
struct xe_vm;

#define XE_BO_MAX_PLACEMENTS	3

/* TODO: To be selected with VM_MADVISE */
#define	XE_BO_PRIORITY_NORMAL	1

/** @xe_bo: XE buffer object */
struct xe_bo {
	/** @ttm: TTM base buffer object */
	struct ttm_buffer_object ttm;
	/** @size: Size of this buffer object */
	size_t size;
	/** @flags: flags for this buffer object */
	u32 flags;
	/** @vm: VM this BO is attached to, for extobj this will be NULL */
	struct xe_vm *vm;
	/** @tile: Tile this BO is attached to (kernel BO only) */
	struct xe_tile *tile;
	/** @placements: valid placements for this BO */
	struct ttm_place placements[XE_BO_MAX_PLACEMENTS];
	/** @placement: current placement for this BO */
	struct ttm_placement placement;
	/** @ggtt_node: GGTT node if this BO is mapped in the GGTT */
	struct drm_mm_node ggtt_node;
	/** @vmap: iosys map of this buffer */
	struct iosys_map vmap;
	/** @ttm_kmap: TTM bo kmap object for internal use only. Keep off. */
	struct ttm_bo_kmap_obj kmap;
	/** @pinned_link: link to present / evicted list of pinned BO */
	struct list_head pinned_link;
#ifdef CONFIG_PROC_FS
	/**
	 * @client: @xe_drm_client which created the bo
	 */
	struct xe_drm_client *client;
	/**
	 * @client_link: Link into @xe_drm_client.objects_list
	 */
	struct list_head client_link;
#endif
	/** @freed: List node for delayed put. */
	struct llist_node freed;
	/** @created: Whether the bo has passed initial creation */
	bool created;

	/** @ccs_cleared */
	bool ccs_cleared;

	/**
	 * @cpu_caching: CPU caching mode. Currently only used for userspace
	 * objects.
	 */
	u16 cpu_caching;

	/** @vram_userfault_link: Link into @mem_access.vram_userfault.list */
		struct list_head vram_userfault_link;
};

#define intel_bo_to_drm_bo(bo) (&(bo)->ttm.base)
#define intel_bo_to_i915(bo) to_i915(intel_bo_to_drm_bo(bo)->dev)

#endif
