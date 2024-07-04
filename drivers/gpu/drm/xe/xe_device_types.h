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
#include "xe_heci_gsc.h"
#include "xe_gt_types.h"
#include "xe_lmtt_types.h"
#include "xe_memirq_types.h"
#include "xe_oa.h"
#include "xe_platform_types.h"
#include "xe_pt_types.h"
#include "xe_sriov_types.h"
#include "xe_step_types.h"

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define TEST_VM_OPS_ERROR
#endif

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)
#include "soc/intel_pch.h"
#include "intel_display_core.h"
#include "intel_display_device.h"
#endif

struct xe_ggtt;
struct xe_pat_ops;

#define XE_BO_INVALID_OFFSET	LONG_MAX

#define GRAPHICS_VER(xe) ((xe)->info.graphics_verx100 / 100)
#define MEDIA_VER(xe) ((xe)->info.media_verx100 / 100)
#define GRAPHICS_VERx100(xe) ((xe)->info.graphics_verx100)
#define MEDIA_VERx100(xe) ((xe)->info.media_verx100)
#define IS_DGFX(xe) ((xe)->info.is_dgfx)
#define HAS_HECI_GSCFI(xe) ((xe)->info.has_heci_gscfi)

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
		 const struct xe_tile * : (const struct xe_device *)((tile__)->xe),	\
		 struct xe_tile * : (tile__)->xe)

/**
 * struct xe_mem_region - memory region structure
 * This is used to describe a memory region in xe
 * device, such as HBM memory or CXL extension memory.
 */
struct xe_mem_region {
	/** @io_start: IO start address of this VRAM instance */
	resource_size_t io_start;
	/**
	 * @io_size: IO size of this VRAM instance
	 *
	 * This represents how much of this VRAM we can access
	 * via the CPU through the VRAM BAR. This can be smaller
	 * than @usable_size, in which case only part of VRAM is CPU
	 * accessible (typically the first 256M). This
	 * configuration is known as small-bar.
	 */
	resource_size_t io_size;
	/** @dpa_base: This memory regions's DPA (device physical address) base */
	resource_size_t dpa_base;
	/**
	 * @usable_size: usable size of VRAM
	 *
	 * Usable size of VRAM excluding reserved portions
	 * (e.g stolen mem)
	 */
	resource_size_t usable_size;
	/**
	 * @actual_physical_size: Actual VRAM size
	 *
	 * Actual VRAM size including reserved portions
	 * (e.g stolen mem)
	 */
	resource_size_t actual_physical_size;
	/** @mapping: pointer to VRAM mappable space */
	void __iomem *mapping;
};

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
	struct {
		/** @mmio.size: size of tile's MMIO space */
		size_t size;

		/** @mmio.regs: pointer to tile's MMIO space (starting with registers) */
		void __iomem *regs;
	} mmio;

	/**
	 * @mmio_ext: MMIO-extension info for a tile.
	 *
	 * Each tile has its own additional 256MB (28-bit) MMIO-extension space.
	 */
	struct {
		/** @mmio_ext.size: size of tile's additional MMIO-extension space */
		size_t size;

		/** @mmio_ext.regs: pointer to tile's additional MMIO-extension space */
		void __iomem *regs;
	} mmio_ext;

	/** @mem: memory management info for tile */
	struct {
		/**
		 * @mem.vram: VRAM info for tile.
		 *
		 * Although VRAM is associated with a specific tile, it can
		 * still be accessed by all tiles' GTs.
		 */
		struct xe_mem_region vram;

		/** @mem.vram_mgr: VRAM TTM manager */
		struct xe_ttm_vram_mgr *vram_mgr;

		/** @mem.ggtt: Global graphics translation table */
		struct xe_ggtt *ggtt;

		/**
		 * @mem.kernel_bb_pool: Pool from which batchbuffers are allocated.
		 *
		 * Media GT shares a pool with its primary GT.
		 */
		struct xe_sa_manager *kernel_bb_pool;
	} mem;

	/** @sriov: tile level virtualization data */
	union {
		struct {
			/** @sriov.pf.lmtt: Local Memory Translation Table. */
			struct xe_lmtt lmtt;
		} pf;
		struct {
			/** @sriov.vf.memirq: Memory Based Interrupts. */
			struct xe_memirq memirq;

			/** @sriov.vf.ggtt_balloon: GGTT regions excluded from use. */
			struct drm_mm_node ggtt_balloon[2];
		} vf;
	} sriov;

	/** @migrate: Migration helper for vram blits and clearing */
	struct xe_migrate *migrate;

	/** @sysfs: sysfs' kobj used by xe_tile_sysfs */
	struct kobject *sysfs;
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
		/** @info.platform_name: platform name */
		const char *platform_name;
		/** @info.graphics_name: graphics IP name */
		const char *graphics_name;
		/** @info.media_name: media IP name */
		const char *media_name;
		/** @info.tile_mmio_ext_size: size of MMIO extension space, per-tile */
		u32 tile_mmio_ext_size;
		/** @info.graphics_verx100: graphics IP version */
		u32 graphics_verx100;
		/** @info.media_verx100: media IP version */
		u32 media_verx100;
		/** @info.mem_region_mask: mask of valid memory regions */
		u32 mem_region_mask;
		/** @info.platform: XE platform enum */
		enum xe_platform platform;
		/** @info.subplatform: XE subplatform enum */
		enum xe_subplatform subplatform;
		/** @info.devid: device ID */
		u16 devid;
		/** @info.revid: device revision */
		u8 revid;
		/** @info.step: stepping information for each IP */
		struct xe_step_info step;
		/** @info.dma_mask_size: DMA address bits */
		u8 dma_mask_size;
		/** @info.vram_flags: Vram flags */
		u8 vram_flags;
		/** @info.tile_count: Number of tiles */
		u8 tile_count;
		/** @info.gt_count: Total number of GTs for entire device */
		u8 gt_count;
		/** @info.vm_max_level: Max VM level */
		u8 vm_max_level;
		/** @info.va_bits: Maximum bits of a virtual address */
		u8 va_bits;

		/** @info.is_dgfx: is discrete device */
		u8 is_dgfx:1;
		/** @info.has_asid: Has address space ID */
		u8 has_asid:1;
		/** @info.force_execlist: Forced execlist submission */
		u8 force_execlist:1;
		/** @info.has_flat_ccs: Whether flat CCS metadata is used */
		u8 has_flat_ccs:1;
		/** @info.has_llc: Device has a shared CPU+GPU last level cache */
		u8 has_llc:1;
		/** @info.has_mmio_ext: Device has extra MMIO address range */
		u8 has_mmio_ext:1;
		/** @info.has_range_tlb_invalidation: Has range based TLB invalidations */
		u8 has_range_tlb_invalidation:1;
		/** @info.has_sriov: Supports SR-IOV */
		u8 has_sriov:1;
		/** @info.has_usm: Device has unified shared memory support */
		u8 has_usm:1;
		/** @info.enable_display: display enabled */
		u8 enable_display:1;
		/** @info.skip_mtcfg: skip Multi-Tile configuration from MTCFG register */
		u8 skip_mtcfg:1;
		/** @info.skip_pcode: skip access to PCODE uC */
		u8 skip_pcode:1;
		/** @info.has_heci_gscfi: device has heci gscfi */
		u8 has_heci_gscfi:1;
		/** @info.skip_guc_pc: Skip GuC based PM feature init */
		u8 skip_guc_pc:1;
		/** @info.has_atomic_enable_pte_bit: Device has atomic enable PTE bit */
		u8 has_atomic_enable_pte_bit:1;
		/** @info.has_device_atomics_on_smem: Supports device atomics on SMEM */
		u8 has_device_atomics_on_smem:1;

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)
		struct {
			u32 rawclk_freq;
		} i915_runtime;
#endif
	} info;

	/** @irq: device interrupt state */
	struct {
		/** @irq.lock: lock for processing irq's on this device */
		spinlock_t lock;

		/** @irq.enabled: interrupts enabled on this device */
		bool enabled;
	} irq;

	/** @ttm: ttm device */
	struct ttm_device ttm;

	/** @mmio: mmio info for device */
	struct {
		/** @mmio.size: size of MMIO space for device */
		size_t size;
		/** @mmio.regs: pointer to MMIO space for device */
		void __iomem *regs;
	} mmio;

	/** @mem: memory info for device */
	struct {
		/** @mem.vram: VRAM info for device */
		struct xe_mem_region vram;
		/** @mem.sys_mgr: system TTM manager */
		struct ttm_resource_manager sys_mgr;
	} mem;

	/** @sriov: device level virtualization data */
	struct {
		/** @sriov.__mode: SR-IOV mode (Don't access directly!) */
		enum xe_sriov_mode __mode;

		/** @sriov.pf: PF specific data */
		struct xe_device_pf pf;

		/** @sriov.wq: workqueue used by the virtualization workers */
		struct workqueue_struct *wq;
	} sriov;

	/** @clients: drm clients info */
	struct {
		/** @clients.lock: Protects drm clients info */
		spinlock_t lock;

		/** @clients.count: number of drm clients */
		u64 count;
	} clients;

	/** @usm: unified memory state */
	struct {
		/** @usm.asid: convert a ASID to VM */
		struct xarray asid_to_vm;
		/** @usm.next_asid: next ASID, used to cyclical alloc asids */
		u32 next_asid;
		/** @usm.num_vm_in_fault_mode: number of VM in fault mode */
		u32 num_vm_in_fault_mode;
		/** @usm.num_vm_in_non_fault_mode: number of VM in non-fault mode */
		u32 num_vm_in_non_fault_mode;
		/** @usm.lock: protects UM state */
		struct mutex lock;
	} usm;

	/** @pinned: pinned BO state */
	struct {
		/** @pinned.lock: protected pinned BO list state */
		spinlock_t lock;
		/** @pinned.kernel_bo_present: pinned kernel BO that are present */
		struct list_head kernel_bo_present;
		/** @pinned.evicted: pinned BO that have been evicted */
		struct list_head evicted;
		/** @pinned.external_vram: pinned external BO in vram*/
		struct list_head external_vram;
	} pinned;

	/** @ufence_wq: user fence wait queue */
	wait_queue_head_t ufence_wq;

	/** @preempt_fence_wq: used to serialize preempt fences */
	struct workqueue_struct *preempt_fence_wq;

	/** @ordered_wq: used to serialize compute mode resume */
	struct workqueue_struct *ordered_wq;

	/** @unordered_wq: used to serialize unordered work, mostly display */
	struct workqueue_struct *unordered_wq;

	/** @tiles: device tiles */
	struct xe_tile tiles[XE_MAX_TILES_PER_DEVICE];

	/**
	 * @mem_access: keep track of memory access in the device, possibly
	 * triggering additional actions when they occur.
	 */
	struct {
		/**
		 * @mem_access.vram_userfault: Encapsulate vram_userfault
		 * related stuff
		 */
		struct {
			/**
			 * @mem_access.vram_userfault.lock: Protects access to
			 * @vram_usefault.list Using mutex instead of spinlock
			 * as lock is applied to entire list operation which
			 * may sleep
			 */
			struct mutex lock;

			/**
			 * @mem_access.vram_userfault.list: Keep list of userfaulted
			 * vram bo, which require to release their mmap mappings
			 * at runtime suspend path
			 */
			struct list_head list;
		} vram_userfault;
	} mem_access;

	/**
	 * @pat: Encapsulate PAT related stuff
	 */
	struct {
		/** @pat.ops: Internal operations to abstract platforms */
		const struct xe_pat_ops *ops;
		/** @pat.table: PAT table to program in the HW */
		const struct xe_pat_table_entry *table;
		/** @pat.n_entries: Number of PAT entries */
		int n_entries;
		u32 idx[__XE_CACHE_LEVEL_COUNT];
	} pat;

	/** @d3cold: Encapsulate d3cold related stuff */
	struct {
		/** @d3cold.capable: Indicates if root port is d3cold capable */
		bool capable;

		/** @d3cold.allowed: Indicates if d3cold is a valid device state */
		bool allowed;

		/**
		 * @d3cold.vram_threshold:
		 *
		 * This represents the permissible threshold(in megabytes)
		 * for vram save/restore. d3cold will be disallowed,
		 * when vram_usages is above or equals the threshold value
		 * to avoid the vram save/restore latency.
		 * Default threshold value is 300mb.
		 */
		u32 vram_threshold;
		/** @d3cold.lock: protect vram_threshold */
		struct mutex lock;
	} d3cold;

	/**
	 * @pm_callback_task: Track the active task that is running in either
	 * the runtime_suspend or runtime_resume callbacks.
	 */
	struct task_struct *pm_callback_task;

	/** @hwmon: hwmon subsystem integration */
	struct xe_hwmon *hwmon;

	/** @heci_gsc: graphics security controller */
	struct xe_heci_gsc heci_gsc;

	/** @oa: oa observation subsystem */
	struct xe_oa oa;

	/** @needs_flr_on_fini: requests function-reset on fini */
	bool needs_flr_on_fini;

	/** @wedged: Struct to control Wedged States and mode */
	struct {
		/** @wedged.flag: Xe device faced a critical error and is now blocked. */
		atomic_t flag;
		/** @wedged.mode: Mode controlled by kernel parameter and debugfs */
		int mode;
	} wedged;

#ifdef TEST_VM_OPS_ERROR
	/**
	 * @vm_inject_error_position: inject errors at different places in VM
	 * bind IOCTL based on this value
	 */
	u8 vm_inject_error_position;
#endif

	/* private: */

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)
	/*
	 * Any fields below this point are the ones used by display.
	 * They are temporarily added here so xe_device can be desguised as
	 * drm_i915_private during build. After cleanup these should go away,
	 * migrating to the right sub-structs
	 */
	struct intel_display display;
	enum intel_pch pch_type;
	u16 pch_id;

	struct dram_info {
		bool wm_lv_0_adjust_needed;
		u8 num_channels;
		bool symmetric_memory;
		enum intel_dram_type {
			INTEL_DRAM_UNKNOWN,
			INTEL_DRAM_DDR3,
			INTEL_DRAM_DDR4,
			INTEL_DRAM_LPDDR3,
			INTEL_DRAM_LPDDR4,
			INTEL_DRAM_DDR5,
			INTEL_DRAM_LPDDR5,
			INTEL_DRAM_GDDR,
		} type;
		u8 num_qgv_points;
		u8 num_psf_gv_points;
	} dram_info;

	/*
	 * edram size in MB.
	 * Cannot be determined by PCIID. You must always read a register.
	 */
	u32 edram_size_mb;

	/* To shut up runtime pm macros.. */
	struct xe_runtime_pm {} runtime_pm;

	/* only to allow build, not used functionally */
	u32 irq_mask;

	struct intel_uncore {
		spinlock_t lock;
	} uncore;

	/* only to allow build, not used functionally */
	struct {
		unsigned int hpll_freq;
		unsigned int czclk_freq;
		unsigned int fsb_freq, mem_freq, is_ddr3;
	};

	void *pxp;
#endif
};

/**
 * struct xe_file - file handle for XE driver
 */
struct xe_file {
	/** @xe: xe DEVICE **/
	struct xe_device *xe;

	/** @drm: base DRM file */
	struct drm_file *drm;

	/** @vm: VM state for file */
	struct {
		/** @vm.xe: xarray to store VMs */
		struct xarray xa;
		/** @vm.lock: protects file VM state */
		struct mutex lock;
	} vm;

	/** @exec_queue: Submission exec queue state for file */
	struct {
		/** @exec_queue.xe: xarray to store engines */
		struct xarray xa;
		/** @exec_queue.lock: protects file engine state */
		struct mutex lock;
	} exec_queue;

	/** @run_ticks: hw engine class run time in ticks for this drm client */
	u64 run_ticks[XE_ENGINE_CLASS_MAX];

	/** @client: drm client */
	struct xe_drm_client *client;
};

#endif
