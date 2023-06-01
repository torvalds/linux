/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#ifndef _XE_DEVICE_TYPES_H_
#define _XE_DEVICE_TYPES_H_

#include <linux/pci.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/ttm/ttm_device.h>

#include "xe_devcoredump_types.h"
#include "xe_gt_types.h"
#include "xe_platform_types.h"
#include "xe_step_types.h"

struct xe_ggtt;

#define XE_BO_INVALID_OFFSET	LONG_MAX

#define GRAPHICS_VER(xe) ((xe)->info.graphics_verx100 / 100)
#define MEDIA_VER(xe) ((xe)->info.media_verx100 / 100)
#define GRAPHICS_VERx100(xe) ((xe)->info.graphics_verx100)
#define MEDIA_VERx100(xe) ((xe)->info.media_verx100)
#define IS_DGFX(xe) ((xe)->info.is_dgfx)

#define XE_VRAM_FLAGS_NEED64K		BIT(0)

#define XE_GT0		0
#define XE_GT1		1
#define XE_MAX_TILES_PER_DEVICE	(XE_GT1 + 1)

#define XE_MAX_ASID	(BIT(20))

#define IS_PLATFORM_STEP(_xe, _platform, min_step, max_step)	\
	((_xe)->info.platform == (_platform) &&			\
	 (_xe)->info.step.graphics >= (min_step) &&		\
	 (_xe)->info.step.graphics < (max_step))
#define IS_SUBPLATFORM_STEP(_xe, _platform, sub, min_step, max_step)	\
	((_xe)->info.platform == (_platform) &&				\
	 (_xe)->info.subplatform == (sub) &&				\
	 (_xe)->info.step.graphics >= (min_step) &&			\
	 (_xe)->info.step.graphics < (max_step))

#define tile_to_xe(tile__)								\
	_Generic(tile__,								\
		 const struct xe_tile *: (const struct xe_device *)((tile__)->xe),	\
		 struct xe_tile *: (tile__)->xe)

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
	struct xe_gt primary_gt;

	/* TODO: Add media GT here */

	/**
	 * @mmio: MMIO info for a tile.
	 *
	 * Each tile has its own 16MB space in BAR0, laid out as:
	 * * 0-4MB: registers
	 * * 4MB-8MB: reserved
	 * * 8MB-16MB: global GTT
	 */
	struct {
		/** @size: size of tile's MMIO space */
		size_t size;

		/** @regs: pointer to tile's MMIO space (starting with registers) */
		void *regs;
	} mmio;

	/** @mem: memory management info for tile */
	struct {
		/**
		 * @vram: VRAM info for tile.
		 *
		 * Although VRAM is associated with a specific tile, it can
		 * still be accessed by all tiles' GTs.
		 */
		struct {
			/** @io_start: IO start address of this VRAM instance */
			resource_size_t io_start;
			/**
			 * @io_size: IO size of this VRAM instance
			 *
			 * This represents how much of this VRAM we can access
			 * via the CPU through the VRAM BAR. This can be smaller
			 * than @size, in which case only part of VRAM is CPU
			 * accessible (typically the first 256M). This
			 * configuration is known as small-bar.
			 */
			resource_size_t io_size;
			/** @base: offset of VRAM starting base */
			resource_size_t base;
			/** @size: size of VRAM. */
			resource_size_t size;
			/** @mapping: pointer to VRAM mappable space */
			void *__iomem mapping;
		} vram;

		/** @vram_mgr: VRAM TTM manager */
		struct xe_ttm_vram_mgr *vram_mgr;

		/** @ggtt: Global graphics translation table */
		struct xe_ggtt *ggtt;
	} mem;
};

/**
 * struct xe_device - Top level struct of XE device
 */
struct xe_device {
	/** @drm: drm device */
	struct drm_device drm;

	/** @devcoredump: device coredump */
	struct xe_devcoredump devcoredump;

	/** @info: device info */
	struct intel_device_info {
		/** @graphics_name: graphics IP name */
		const char *graphics_name;
		/** @media_name: media IP name */
		const char *media_name;
		/** @graphics_verx100: graphics IP version */
		u32 graphics_verx100;
		/** @media_verx100: media IP version */
		u32 media_verx100;
		/** @mem_region_mask: mask of valid memory regions */
		u32 mem_region_mask;
		/** @platform: XE platform enum */
		enum xe_platform platform;
		/** @subplatform: XE subplatform enum */
		enum xe_subplatform subplatform;
		/** @devid: device ID */
		u16 devid;
		/** @revid: device revision */
		u8 revid;
		/** @step: stepping information for each IP */
		struct xe_step_info step;
		/** @dma_mask_size: DMA address bits */
		u8 dma_mask_size;
		/** @vram_flags: Vram flags */
		u8 vram_flags;
		/** @tile_count: Number of tiles */
		u8 tile_count;
		/** @vm_max_level: Max VM level */
		u8 vm_max_level;

		/** @is_dgfx: is discrete device */
		u8 is_dgfx:1;
		/** @supports_usm: Supports unified shared memory */
		u8 supports_usm:1;
		/** @has_asid: Has address space ID */
		u8 has_asid:1;
		/** @enable_guc: GuC submission enabled */
		u8 enable_guc:1;
		/** @has_flat_ccs: Whether flat CCS metadata is used */
		u8 has_flat_ccs:1;
		/** @has_4tile: Whether tile-4 tiling is supported */
		u8 has_4tile:1;
		/** @has_llc: Device has a shared CPU+GPU last level cache */
		u8 has_llc:1;
		/** @has_range_tlb_invalidation: Has range based TLB invalidations */
		u8 has_range_tlb_invalidation:1;
		/** @has_link_copy_engines: Whether the platform has link copy engines */
		u8 has_link_copy_engine:1;
	} info;

	/** @irq: device interrupt state */
	struct {
		/** @lock: lock for processing irq's on this device */
		spinlock_t lock;

		/** @enabled: interrupts enabled on this device */
		bool enabled;
	} irq;

	/** @ttm: ttm device */
	struct ttm_device ttm;

	/** @mmio: mmio info for device */
	struct {
		/** @size: size of MMIO space for device */
		size_t size;
		/** @regs: pointer to MMIO space for device */
		void *regs;
	} mmio;

	/** @mem: memory info for device */
	struct {
		/** @vram: VRAM info for device */
		struct {
			/** @io_start: IO start address of VRAM */
			resource_size_t io_start;
			/**
			 * @io_size: IO size of VRAM.
			 *
			 * This represents how much of VRAM the CPU can access
			 * via the VRAM BAR.
			 * On systems that do not support large BAR IO space,
			 * this can be smaller than the actual memory size, in
			 * which case only part of VRAM is CPU accessible
			 * (typically the first 256M).  This configuration is
			 * known as small-bar.
			 */
			resource_size_t io_size;
			/** @size: Total size of VRAM */
			resource_size_t size;
			/** @base: Offset to apply for Device Physical Address control */
			resource_size_t base;
			/** @mapping: pointer to VRAM mappable space */
			void *__iomem mapping;
		} vram;
		/** @sys_mgr: system TTM manager */
		struct ttm_resource_manager sys_mgr;
	} mem;

	/** @usm: unified memory state */
	struct {
		/** @asid: convert a ASID to VM */
		struct xarray asid_to_vm;
		/** @next_asid: next ASID, used to cyclical alloc asids */
		u32 next_asid;
		/** @num_vm_in_fault_mode: number of VM in fault mode */
		u32 num_vm_in_fault_mode;
		/** @num_vm_in_non_fault_mode: number of VM in non-fault mode */
		u32 num_vm_in_non_fault_mode;
		/** @lock: protects UM state */
		struct mutex lock;
	} usm;

	/** @persistent_engines: engines that are closed but still running */
	struct {
		/** @lock: protects persistent engines */
		struct mutex lock;
		/** @list: list of persistent engines */
		struct list_head list;
	} persistent_engines;

	/** @pinned: pinned BO state */
	struct {
		/** @lock: protected pinned BO list state */
		spinlock_t lock;
		/** @evicted: pinned kernel BO that are present */
		struct list_head kernel_bo_present;
		/** @evicted: pinned BO that have been evicted */
		struct list_head evicted;
		/** @external_vram: pinned external BO in vram*/
		struct list_head external_vram;
	} pinned;

	/** @ufence_wq: user fence wait queue */
	wait_queue_head_t ufence_wq;

	/** @ordered_wq: used to serialize compute mode resume */
	struct workqueue_struct *ordered_wq;

	/** @tiles: device tiles */
	struct xe_tile tiles[XE_MAX_TILES_PER_DEVICE];

	/**
	 * @mem_access: keep track of memory access in the device, possibly
	 * triggering additional actions when they occur.
	 */
	struct {
		/** @ref: ref count of memory accesses */
		atomic_t ref;
		/** @hold_rpm: need to put rpm ref back at the end */
		bool hold_rpm;
	} mem_access;

	/** @d3cold_allowed: Indicates if d3cold is a valid device state */
	bool d3cold_allowed;

	/* For pcode */
	struct mutex sb_lock;

	u32 enabled_irq_mask;
};

/**
 * struct xe_file - file handle for XE driver
 */
struct xe_file {
	/** @drm: base DRM file */
	struct drm_file *drm;

	/** @vm: VM state for file */
	struct {
		/** @xe: xarray to store VMs */
		struct xarray xa;
		/** @lock: protects file VM state */
		struct mutex lock;
	} vm;

	/** @engine: Submission engine state for file */
	struct {
		/** @xe: xarray to store engines */
		struct xarray xa;
		/** @lock: protects file engine state */
		struct mutex lock;
	} engine;
};

#endif
