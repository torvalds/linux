/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTM_VRAM_MGR_TYPES_H_
#define _XE_TTM_VRAM_MGR_TYPES_H_

#include <linux/gpu_buddy.h>
#include <drm/ttm/ttm_device.h>

/**
 * struct xe_ttm_vram_mgr - Xe TTM VRAM manager
 *
 * Manages placement of TTM resource in VRAM.
 */
struct xe_ttm_vram_mgr {
	/** @manager: Base TTM resource manager */
	struct ttm_resource_manager manager;
	/** @mm: DRM buddy allocator which manages the VRAM */
	struct gpu_buddy mm;
	/** @offlined_pages: List of offlined pages */
	struct list_head offlined_pages;
	/** @n_offlined_pages: Number of offlined pages */
	u16 n_offlined_pages;
	/** @queued_pages: List of queued pages */
	struct list_head queued_pages;
	/** @n_queued_pages: Number of queued pages */
	u16 n_queued_pages;
	/** @visible_size: Proped size of the CPU visible portion */
	u64 visible_size;
	/** @visible_avail: CPU visible portion still unallocated */
	u64 visible_avail;
	/** @default_page_size: default page size */
	u64 default_page_size;
	/** @lock: protects allocations of VRAM */
	struct mutex lock;
	/** @mem_type: The TTM memory type */
	u32 mem_type;
	/** @max_pages: max pages that can be in offline queue retrieved from FW */
	u16 max_pages;
};

/**
 * struct xe_ttm_vram_mgr_resource - Xe TTM VRAM resource
 */
struct xe_ttm_vram_mgr_resource {
	/** @base: Base TTM resource */
	struct ttm_resource base;
	/** @blocks: list of DRM buddy blocks */
	struct list_head blocks;
	/** @used_visible_size: How many CPU visible bytes this resource is using */
	u64 used_visible_size;
	/** @flags: flags associated with the resource */
	unsigned long flags;
};

/**
 * enum xe_page_reserve_status - Buddy reservation status
 * @XE_PAGE_RESERVE_PENDING: reservation in progress
 * @XE_PAGE_RESERVE_FAIL: reservation failed
 */
enum xe_page_reserve_status {
	XE_PAGE_RESERVE_PENDING = 0,
	XE_PAGE_RESERVE_FAIL,
};

/**
 * struct xe_ttm_vram_offline_resource - Tracks a single offlined VRAM page
 */
struct xe_ttm_vram_offline_resource {
	/** @offlined_link: Link into mgr->offlined_pages */
	struct list_head offlined_link;
	/** @queued_link: Link into mgr->queued_pages */
	struct list_head queued_link;
	/** @blocks: Buddy blocks reserved for this page */
	struct list_head blocks;
	/** @used_visible_size: CPU-visible bytes consumed */
	u64 used_visible_size;
	/** @addr: Faulty DPA reported by HW */
	u64 addr;
	/** @status: buddy reservation status */
	enum xe_page_reserve_status status;
	/** @rcu: RCU head for deferred freeing */
	struct rcu_head rcu;
};

#endif
