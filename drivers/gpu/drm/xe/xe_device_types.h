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
#include "xe_late_bind_fw_types.h"
#include "xe_lmtt_types.h"
#include "xe_memirq_types.h"
#include "xe_mert.h"
#include "xe_oa_types.h"
#include "xe_pagefault_types.h"
#include "xe_platform_types.h"
#include "xe_pmu_types.h"
#include "xe_pt_types.h"
#include "xe_sriov_pf_types.h"
#include "xe_sriov_types.h"
#include "xe_sriov_vf_types.h"
#include "xe_sriov_vf_ccs_types.h"
#include "xe_step_types.h"
#include "xe_survivability_mode_types.h"
#include "xe_tile_sriov_vf_types.h"
#include "xe_validation.h"

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG)
#define TEST_VM_OPS_ERROR
#endif

struct dram_info;
struct drm_pagemap_shrinker;
struct intel_display;
struct intel_dg_nvm_dev;
struct xe_ggtt;
struct xe_i2c;
struct xe_pat_ops;
struct xe_pxp;
struct xe_vram_region;

/**
 * enum xe_wedged_mode - possible wedged modes
 * @XE_WEDGED_MODE_NEVER: Device will never be declared wedged.
 * @XE_WEDGED_MODE_UPON_CRITICAL_ERROR: Device will be declared wedged only
 *	when critical error occurs like GT reset failure or firmware failure.
 *	This is the default mode.
 * @XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET: Device will be declared wedged on
 *	any hang. In this mode, engine resets are disabled to avoid automatic
 *	recovery attempts. This mode is primarily intended for debugging hangs.
 */
enum xe_wedged_mode {
	XE_WEDGED_MODE_NEVER = 0,
	XE_WEDGED_MODE_UPON_CRITICAL_ERROR = 1,
	XE_WEDGED_MODE_UPON_ANY_HANG_NO_RESET = 2,
};

#define XE_WEDGED_MODE_DEFAULT		XE_WEDGED_MODE_UPON_CRITICAL_ERROR
#define XE_WEDGED_MODE_DEFAULT_STR	"upon-critical-error"

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
		 const struct xe_tile * : (const struct xe_device *)((tile__)->xe),	\
		 struct xe_tile * : (tile__)->xe)

/**
 * struct xe_mmio - register mmio structure
 *
 * Represents an MMIO region that the CPU may use to access registers.  A
 * region may share its IO map with other regions (e.g., all GTs within a
 * tile share the same map with their parent tile, but represent different
 * subregions of the overall IO space).
 */
struct xe_mmio {
	/** @tile: Backpointer to tile, used for tracing */
	struct xe_tile *tile;

	/** @regs: Map used to access registers. */
	void __iomem *regs;

	/**
	 * @sriov_vf_gt: Backpointer to GT.
	 *
	 * This pointer is only set for GT MMIO regions and only when running
	 * as an SRIOV VF structure
	 */
	struct xe_gt *sriov_vf_gt;

	/**
	 * @regs_size: Length of the register region within the map.
	 *
	 * The size of the iomap set in *regs is generally larger than the
	 * register mmio space since it includes unused regions and/or
	 * non-register regions such as the GGTT PTEs.
	 */
	size_t regs_size;

	/** @adj_limit: adjust MMIO address if address is below this value */
	u32 adj_limit;

	/** @adj_offset: offset to add to MMIO address when adjusting */
	u32 adj_offset;
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

/**
 * struct xe_device - Top level struct of Xe device
 */
struct xe_device {
	/** @drm: drm device */
	struct drm_device drm;

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)
	/** @display: display device data, must be placed after drm device member */
	struct intel_display *display;
#endif

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
		/** @info.graphics_verx100: graphics IP version */
		u32 graphics_verx100;
		/** @info.media_verx100: media IP version */
		u32 media_verx100;
		/** @info.mem_region_mask: mask of valid memory regions */
		u32 mem_region_mask;
		/** @info.platform: Xe platform enum */
		enum xe_platform platform;
		/** @info.subplatform: Xe subplatform enum */
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
		/** @info.max_gt_per_tile: Number of GT IDs allocated to each tile */
		u8 max_gt_per_tile;
		/** @info.gt_count: Total number of GTs for entire device */
		u8 gt_count;
		/** @info.vm_max_level: Max VM level */
		u8 vm_max_level;
		/** @info.va_bits: Maximum bits of a virtual address */
		u8 va_bits;

		/*
		 * Keep all flags below alphabetically sorted
		 */

		/** @info.force_execlist: Forced execlist submission */
		u8 force_execlist:1;
		/** @info.has_asid: Has address space ID */
		u8 has_asid:1;
		/** @info.has_atomic_enable_pte_bit: Device has atomic enable PTE bit */
		u8 has_atomic_enable_pte_bit:1;
		/** @info.has_cached_pt: Supports caching pagetable */
		u8 has_cached_pt:1;
		/** @info.has_device_atomics_on_smem: Supports device atomics on SMEM */
		u8 has_device_atomics_on_smem:1;
		/** @info.has_fan_control: Device supports fan control */
		u8 has_fan_control:1;
		/** @info.has_flat_ccs: Whether flat CCS metadata is used */
		u8 has_flat_ccs:1;
		/** @info.has_gsc_nvm: Device has gsc non-volatile memory */
		u8 has_gsc_nvm:1;
		/** @info.has_heci_cscfi: device has heci cscfi */
		u8 has_heci_cscfi:1;
		/** @info.has_heci_gscfi: device has heci gscfi */
		u8 has_heci_gscfi:1;
		/** @info.has_i2c: Device has I2C controller */
		u8 has_i2c:1;
		/** @info.has_late_bind: Device has firmware late binding support */
		u8 has_late_bind:1;
		/** @info.has_llc: Device has a shared CPU+GPU last level cache */
		u8 has_llc:1;
		/** @info.has_mbx_power_limits: Device has support to manage power limits using
		 * pcode mailbox commands.
		 */
		u8 has_mbx_power_limits:1;
		/** @info.has_mbx_thermal_info: Device supports thermal mailbox commands */
		u8 has_mbx_thermal_info:1;
		/** @info.has_mem_copy_instr: Device supports MEM_COPY instruction */
		u8 has_mem_copy_instr:1;
		/** @info.has_mert: Device has standalone MERT */
		u8 has_mert:1;
		/** @info.has_page_reclaim_hw_assist: Device supports page reclamation feature */
		u8 has_page_reclaim_hw_assist:1;
		/** @info.has_pre_prod_wa: Pre-production workarounds still present in driver */
		u8 has_pre_prod_wa:1;
		/** @info.has_pxp: Device has PXP support */
		u8 has_pxp:1;
		/** @info.has_range_tlb_inval: Has range based TLB invalidations */
		u8 has_range_tlb_inval:1;
		/** @info.has_soc_remapper_sysctrl: Has SoC remapper system controller */
		u8 has_soc_remapper_sysctrl:1;
		/** @info.has_soc_remapper_telem: Has SoC remapper telemetry support */
		u8 has_soc_remapper_telem:1;
		/** @info.has_sriov: Supports SR-IOV */
		u8 has_sriov:1;
		/** @info.has_usm: Device has unified shared memory support */
		u8 has_usm:1;
		/** @info.has_64bit_timestamp: Device supports 64-bit timestamps */
		u8 has_64bit_timestamp:1;
		/** @info.is_dgfx: is discrete device */
		u8 is_dgfx:1;
		/** @info.needs_scratch: needs scratch page for oob prefetch to work */
		u8 needs_scratch:1;
		/**
		 * @info.probe_display: Probe display hardware.  If set to
		 * false, the driver will behave as if there is no display
		 * hardware present and will not try to read/write to it in any
		 * way.  The display hardware, if it exists, will not be
		 * exposed to userspace and will be left untouched in whatever
		 * state the firmware or bootloader left it in.
		 */
		u8 probe_display:1;
		/** @info.skip_guc_pc: Skip GuC based PM feature init */
		u8 skip_guc_pc:1;
		/** @info.skip_mtcfg: skip Multi-Tile configuration from MTCFG register */
		u8 skip_mtcfg:1;
		/** @info.skip_pcode: skip access to PCODE uC */
		u8 skip_pcode:1;
		/** @info.needs_shared_vf_gt_wq: needs shared GT WQ on VF */
		u8 needs_shared_vf_gt_wq:1;
	} info;

	/** @wa_active: keep track of active workarounds */
	struct {
		/** @wa_active.oob: bitmap with active OOB workarounds */
		unsigned long *oob;

		/**
		 * @wa_active.oob_initialized: Mark oob as initialized to help detecting misuse
		 * of XE_DEVICE_WA() - it can only be called on initialization after
		 * Device OOB WAs have been processed.
		 */
		bool oob_initialized;
	} wa_active;

	/** @survivability: survivability information for device */
	struct xe_survivability survivability;

	/** @irq: device interrupt state */
	struct {
		/** @irq.lock: lock for processing irq's on this device */
		spinlock_t lock;

		/** @irq.enabled: interrupts enabled on this device */
		atomic_t enabled;

		/** @irq.msix: irq info for platforms that support MSI-X */
		struct {
			/** @irq.msix.nvec: number of MSI-X interrupts */
			u16 nvec;
			/** @irq.msix.indexes: used to allocate MSI-X indexes */
			struct xarray indexes;
		} msix;
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
		struct xe_vram_region *vram;
		/** @mem.sys_mgr: system TTM manager */
		struct ttm_resource_manager sys_mgr;
		/** @mem.sys_mgr: system memory shrinker. */
		struct xe_shrinker *shrinker;
	} mem;

	/** @sriov: device level virtualization data */
	struct {
		/** @sriov.__mode: SR-IOV mode (Don't access directly!) */
		enum xe_sriov_mode __mode;

		union {
			/** @sriov.pf: PF specific data */
			struct xe_device_pf pf;
			/** @sriov.vf: VF specific data */
			struct xe_device_vf vf;
		};

		/** @sriov.wq: workqueue used by the virtualization workers */
		struct workqueue_struct *wq;
	} sriov;

	/** @usm: unified memory state */
	struct {
		/** @usm.asid: convert a ASID to VM */
		struct xarray asid_to_vm;
		/** @usm.next_asid: next ASID, used to cyclical alloc asids */
		u32 next_asid;
		/** @usm.lock: protects UM state */
		struct rw_semaphore lock;
		/** @usm.pf_wq: page fault work queue, unbound, high priority */
		struct workqueue_struct *pf_wq;
		/*
		 * We pick 4 here because, in the current implementation, it
		 * yields the best bandwidth utilization of the kernel paging
		 * engine.
		 */
#define XE_PAGEFAULT_QUEUE_COUNT	4
		/** @usm.pf_queue: Page fault queues */
		struct xe_pagefault_queue pf_queue[XE_PAGEFAULT_QUEUE_COUNT];
#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)
		/** @usm.pagemap_shrinker: Shrinker for unused pagemaps */
		struct drm_pagemap_shrinker *dpagemap_shrinker;
#endif
	} usm;

	/** @pinned: pinned BO state */
	struct {
		/** @pinned.lock: protected pinned BO list state */
		spinlock_t lock;
		/** @pinned.early: early pinned lists */
		struct {
			/** @pinned.early.kernel_bo_present: pinned kernel BO that are present */
			struct list_head kernel_bo_present;
			/** @pinned.early.evicted: pinned BO that have been evicted */
			struct list_head evicted;
		} early;
		/** @pinned.late: late pinned lists */
		struct {
			/** @pinned.late.kernel_bo_present: pinned kernel BO that are present */
			struct list_head kernel_bo_present;
			/** @pinned.late.evicted: pinned BO that have been evicted */
			struct list_head evicted;
			/** @pinned.external: pinned external and dma-buf. */
			struct list_head external;
		} late;
	} pinned;

	/** @ufence_wq: user fence wait queue */
	wait_queue_head_t ufence_wq;

	/** @preempt_fence_wq: used to serialize preempt fences */
	struct workqueue_struct *preempt_fence_wq;

	/** @ordered_wq: used to serialize compute mode resume */
	struct workqueue_struct *ordered_wq;

	/** @unordered_wq: used to serialize unordered work */
	struct workqueue_struct *unordered_wq;

	/** @destroy_wq: used to serialize user destroy work, like queue */
	struct workqueue_struct *destroy_wq;

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
		/** @pat.ats_entry: PAT entry for PCIe ATS responses */
		const struct xe_pat_table_entry *pat_ats;
		/** @pat.pta_entry: PAT entry for page table accesses */
		const struct xe_pat_table_entry *pat_pta;
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

	/** @pm_notifier: Our PM notifier to perform actions in response to various PM events. */
	struct notifier_block pm_notifier;
	/** @pm_block: Completion to block validating tasks on suspend / hibernate prepare */
	struct completion pm_block;
	/** @rebind_resume_list: List of wq items to kick on resume. */
	struct list_head rebind_resume_list;
	/** @rebind_resume_lock: Lock to protect the rebind_resume_list */
	struct mutex rebind_resume_lock;

	/** @pmt: Support the PMT driver callback interface */
	struct {
		/** @pmt.lock: protect access for telemetry data */
		struct mutex lock;
	} pmt;

	/** @soc_remapper: SoC remapper object */
	struct {
		/** @soc_remapper.lock: Serialize access to SoC Remapper's index registers */
		spinlock_t lock;

		/** @soc_remapper.set_telem_region: Set telemetry index */
		void (*set_telem_region)(struct xe_device *xe, u32 index);

		/** @soc_remapper.set_sysctrl_region: Set system controller index */
		void (*set_sysctrl_region)(struct xe_device *xe, u32 index);
	} soc_remapper;

	/**
	 * @pm_callback_task: Track the active task that is running in either
	 * the runtime_suspend or runtime_resume callbacks.
	 */
	struct task_struct *pm_callback_task;

	/** @hwmon: hwmon subsystem integration */
	struct xe_hwmon *hwmon;

	/** @heci_gsc: graphics security controller */
	struct xe_heci_gsc heci_gsc;

	/** @nvm: discrete graphics non-volatile memory */
	struct intel_dg_nvm_dev *nvm;

	/** @late_bind: xe mei late bind interface */
	struct xe_late_bind late_bind;

	/** @oa: oa observation subsystem */
	struct xe_oa oa;

	/** @pxp: Encapsulate Protected Xe Path support */
	struct xe_pxp *pxp;

	/** @needs_flr_on_fini: requests function-reset on fini */
	bool needs_flr_on_fini;

	/** @wedged: Struct to control Wedged States and mode */
	struct {
		/** @wedged.flag: Xe device faced a critical error and is now blocked. */
		atomic_t flag;
		/** @wedged.mode: Mode controlled by kernel parameter and debugfs */
		enum xe_wedged_mode mode;
		/** @wedged.method: Recovery method to be sent in the drm device wedged uevent */
		unsigned long method;
		/** @wedged.inconsistent_reset: Inconsistent reset policy state between GTs */
		bool inconsistent_reset;
	} wedged;

	/** @bo_device: Struct to control async free of BOs */
	struct xe_bo_dev {
		/** @bo_device.async_free: Free worker */
		struct work_struct async_free;
		/** @bo_device.async_list: List of BOs to be freed */
		struct llist_head async_list;
	} bo_device;

	/** @pmu: performance monitoring unit */
	struct xe_pmu pmu;

	/** @i2c: I2C host controller */
	struct xe_i2c *i2c;

	/** @atomic_svm_timeslice_ms: Atomic SVM fault timeslice MS */
	u32 atomic_svm_timeslice_ms;

	/** @min_run_period_lr_ms: LR VM (preempt fence mode) timeslice */
	u32 min_run_period_lr_ms;

	/** @min_run_period_pf_ms: LR VM (page fault mode) timeslice */
	u32 min_run_period_pf_ms;

#ifdef TEST_VM_OPS_ERROR
	/**
	 * @vm_inject_error_position: inject errors at different places in VM
	 * bind IOCTL based on this value
	 */
	u8 vm_inject_error_position;
#endif

#if IS_ENABLED(CONFIG_TRACE_GPU_MEM)
	/**
	 * @global_total_pages: global GPU page usage tracked for gpu_mem
	 * tracepoints
	 */
	atomic64_t global_total_pages;
#endif
	/** @val: The domain for exhaustive eviction, which is currently per device. */
	struct xe_validation_device val;

	/** @psmi: GPU debugging via additional validation HW */
	struct {
		/** @psmi.capture_obj: PSMI buffer for VRAM */
		struct xe_bo *capture_obj[XE_MAX_TILES_PER_DEVICE + 1];
		/** @psmi.region_mask: Mask of valid memory regions */
		u8 region_mask;
	} psmi;

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
	/** @g2g_test_array: for testing G2G communications */
	u32 *g2g_test_array;
	/** @g2g_test_count: for testing G2G communications */
	atomic_t g2g_test_count;
#endif

	/* private: */

#if IS_ENABLED(CONFIG_DRM_XE_DISPLAY)
	/*
	 * Any fields below this point are the ones used by display.
	 * They are temporarily added here so xe_device can be desguised as
	 * drm_i915_private during build. After cleanup these should go away,
	 * migrating to the right sub-structs
	 */

	struct intel_uncore {
		spinlock_t lock;
	} uncore;
#endif
};

/**
 * struct xe_file - file handle for Xe driver
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
		/**
		 * @vm.lock: Protects VM lookup + reference and removal from
		 * file xarray. Not an intended to be an outer lock which does
		 * thing while being held.
		 */
		struct mutex lock;
	} vm;

	/** @exec_queue: Submission exec queue state for file */
	struct {
		/** @exec_queue.xa: xarray to store exece queues */
		struct xarray xa;
		/**
		 * @exec_queue.lock: Protects exec queue lookup + reference and
		 * removal from file xarray. Not intended to be an outer lock
		 * which does things while being held.
		 */
		struct mutex lock;
		/**
		 * @exec_queue.pending_removal: items pending to be removed to
		 * synchronize GPU state update with ongoing query.
		 */
		atomic_t pending_removal;
	} exec_queue;

	/** @run_ticks: hw engine class run time in ticks for this drm client */
	u64 run_ticks[XE_ENGINE_CLASS_MAX];

	/** @client: drm client */
	struct xe_drm_client *client;

	/**
	 * @process_name: process name for file handle, used to safely output
	 * during error situations where xe file can outlive process
	 */
	char *process_name;

	/**
	 * @pid: pid for file handle, used to safely output uring error
	 * situations where xe file can outlive process
	 */
	pid_t pid;

	/** @refcount: ref count of this xe file */
	struct kref refcount;
};

#endif
