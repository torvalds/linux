/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2026 Intel Corporation
 */

#ifndef _XE_TILE_TYPES_H_
#define _XE_TILE_TYPES_H_

#include <linux/mutex_types.h>
#include <linux/workqueue_types.h>

#include "xe_lmtt_types.h"
#include "xe_memirq_types.h"
#include "xe_mert.h"
#include "xe_mmio_types.h"
#include "xe_tile_sriov_vf_types.h"

#define tile_to_xe(tile__)								\
	_Generic(tile__,								\
		 const struct xe_tile * : (const struct xe_device *)((tile__)->xe),	\
		 struct xe_tile * : (tile__)->xe)

/**
 * struct xe_tile - hardware tile structure
 *
 * From a driver perspective, a "tile" is effectively a complete GPU, containing
 * an SGunit, 1-2 GTs, and (for discrete platforms) VRAM.
 *
 * Multi-tile platforms effectively bundle multiple GPUs behind a single PCI
 * device and designate one "root" tile as being responsible for external PCI
 * communication.  PCI BAR0 exposes the GGTT and MMIO register space for each
 * tile in a stacked layout, and PCI BAR2 exposes the local memory associated
 * with each tile similarly.  Device-wide interrupts can be enabled/disabled
 * at the root tile, and the MSTR_TILE_INTR register will report which tiles
 * have interrupts that need servicing.
 */
struct xe_tile {
	/** @xe: Backpointer to tile's PCI device */
	struct xe_device *xe;

	/** @id: ID of the tile */
	u8 id;

	/**
	 * @primary_gt: Primary GT
	 */
	struct xe_gt *primary_gt;

	/**
	 * @media_gt: Media GT
	 *
	 * Only present on devices with media version >= 13.
	 */
	struct xe_gt *media_gt;

	/**
	 * @mmio: MMIO info for a tile.
	 *
	 * Each tile has its own 16MB space in BAR0, laid out as:
	 * * 0-4MB: registers
	 * * 4MB-8MB: reserved
	 * * 8MB-16MB: global GTT
	 */
	struct xe_mmio mmio;

	/** @mem: memory management info for tile */
	struct {
		/**
		 * @mem.kernel_vram: kernel-dedicated VRAM info for tile.
		 *
		 * Although VRAM is associated with a specific tile, it can
		 * still be accessed by all tiles' GTs.
		 */
		struct xe_vram_region *kernel_vram;

		/**
		 * @mem.vram: general purpose VRAM info for tile.
		 *
		 * Although VRAM is associated with a specific tile, it can
		 * still be accessed by all tiles' GTs.
		 */
		struct xe_vram_region *vram;

		/** @mem.ggtt: Global graphics translation table */
		struct xe_ggtt *ggtt;

		/**
		 * @mem.kernel_bb_pool: Pool from which batchbuffers are allocated.
		 *
		 * Media GT shares a pool with its primary GT.
		 */
		struct xe_sa_manager *kernel_bb_pool;

		/**
		 * @mem.reclaim_pool: Pool for PRLs allocated.
		 *
		 * Only main GT has page reclaim list allocations.
		 */
		struct xe_sa_manager *reclaim_pool;
	} mem;

	/** @sriov: tile level virtualization data */
	union {
		struct {
			/** @sriov.pf.lmtt: Local Memory Translation Table. */
			struct xe_lmtt lmtt;
		} pf;
		struct {
			/** @sriov.vf.ggtt_balloon: GGTT regions excluded from use. */
			struct xe_ggtt_node *ggtt_balloon[2];
			/** @sriov.vf.self_config: VF configuration data */
			struct xe_tile_sriov_vf_selfconfig self_config;
		} vf;
	} sriov;

	/** @memirq: Memory Based Interrupts. */
	struct xe_memirq memirq;

	/** @csc_hw_error_work: worker to report CSC HW errors */
	struct work_struct csc_hw_error_work;

	/** @pcode: tile's PCODE */
	struct {
		/** @pcode.lock: protecting tile's PCODE mailbox data */
		struct mutex lock;
	} pcode;

	/** @migrate: Migration helper for vram blits and clearing */
	struct xe_migrate *migrate;

	/** @sysfs: sysfs' kobj used by xe_tile_sysfs */
	struct kobject *sysfs;

	/** @debugfs: debugfs directory associated with this tile */
	struct dentry *debugfs;

	/** @mert: MERT-related data */
	struct xe_mert mert;
};

#endif
