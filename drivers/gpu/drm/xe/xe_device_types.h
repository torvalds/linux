/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_DEVICE_TYPES_H_
#define _XE_DEVICE_TYPES_H_

#include <linux/pci.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/ttm/ttm_device.h>

#include "xe_gt_types.h"
#include "xe_platform_types.h"
#include "xe_step_types.h"

#define XE_BO_INVALID_OFFSET	LONG_MAX

#define GRAPHICS_VER(xe) ((xe)->info.graphics_verx100 / 100)
#define MEDIA_VER(xe) ((xe)->info.media_verx100 / 100)
#define GRAPHICS_VERx100(xe) ((xe)->info.graphics_verx100)
#define MEDIA_VERx100(xe) ((xe)->info.media_verx100)
#define IS_DGFX(xe) ((xe)->info.is_dgfx)

#define XE_VRAM_FLAGS_NEED64K		BIT(0)

#define XE_GT0		0
#define XE_GT1		1
#define XE_MAX_GT	(XE_GT1 + 1)

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

/**
 * struct xe_device - Top level struct of XE device
 */
struct xe_device {
	/** @drm: drm device */
	struct drm_device drm;

	/** @info: device info */
	struct intel_device_info {
		/** @graphics_verx100: graphics IP version */
		u32 graphics_verx100;
		/** @media_verx100: media IP version */
		u32 media_verx100;
		/** @mem_region_mask: mask of valid memory regions */
		u32 mem_region_mask;
		/** @is_dgfx: is discrete device */
		bool is_dgfx;
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
		/** @media_ver: Media version */
		u8 media_ver;
		/** @supports_usm: Supports unified shared memory */
		bool supports_usm;
		/** @enable_guc: GuC submission enabled */
		bool enable_guc;
		/** @has_flat_ccs: Whether flat CCS metadata is used */
		bool has_flat_ccs;
		/** @has_4tile: Whether tile-4 tiling is supported */
		bool has_4tile;
		/** @has_range_tlb_invalidation: Has range based TLB invalidations */
		bool has_range_tlb_invalidation;
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
			/** @io_start: start address of VRAM */
			resource_size_t io_start;
			/** @size: size of VRAM */
			resource_size_t size;
			/** @mapping: pointer to VRAM mappable space */
			void *__iomem mapping;
		} vram;
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

	/** @persitent_engines: engines that are closed but still running */
	struct {
		/** @lock: protects persitent engines */
		struct mutex lock;
		/** @list: list of persitent engines */
		struct list_head list;
	} persitent_engines;

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

	/** @gt: graphics tile */
	struct xe_gt gt[XE_MAX_GT];

	/**
	 * @mem_access: keep track of memory access in the device, possibly
	 * triggering additional actions when they occur.
	 */
	struct {
		/** @lock: protect the ref count */
		struct mutex lock;
		/** @ref: ref count of memory accesses */
		s32 ref;
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
