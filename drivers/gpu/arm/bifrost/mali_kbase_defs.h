/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2011-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/**
 * DOC: Defintions (types, defines, etcs) common to Kbase. They are placed here
 * to allow the hierarchy of header files to work.
 */

#ifndef _KBASE_DEFS_H_
#define _KBASE_DEFS_H_

#include <mali_kbase_config.h>
#include <mali_base_hwconfig_features.h>
#include <mali_base_hwconfig_issues.h>
#include <mali_kbase_mem_lowlevel.h>
#include <mmu/mali_kbase_mmu_hw.h>
#include <backend/gpu/mali_kbase_instr_defs.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_gpuprops_types.h>
#include <hwcnt/mali_kbase_hwcnt_watchdog_if.h>

#if MALI_USE_CSF
#include <hwcnt/backend/mali_kbase_hwcnt_backend_csf.h>
#else
#include <hwcnt/backend/mali_kbase_hwcnt_backend_jm.h>
#include <hwcnt/backend/mali_kbase_hwcnt_backend_jm_watchdog.h>
#endif

#include <protected_mode_switcher.h>

#include <linux/atomic.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/sizes.h>


#include "mali_kbase_fence_defs.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif /* CONFIG_DEBUG_FS */

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
#include <linux/devfreq.h>
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */

#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif

#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include <arbiter/mali_kbase_arbiter_defs.h>
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/memory_group_manager.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "debug/mali_kbase_debug_ktrace_defs.h"

/** Number of milliseconds before we time out on a GPU soft/hard reset */
#define RESET_TIMEOUT           500

/**
 * BASE_JM_MAX_NR_SLOTS - The maximum number of Job Slots to support in the Hardware.
 *
 * You can optimize this down if your target devices will only ever support a
 * small number of job slots.
 */
#define BASE_JM_MAX_NR_SLOTS        3

/**
 * BASE_MAX_NR_AS - The maximum number of Address Spaces to support in the Hardware.
 *
 * You can optimize this down if your target devices will only ever support a
 * small number of Address Spaces
 */
#define BASE_MAX_NR_AS              16

/* mmu */
#define MIDGARD_MMU_LEVEL(x) (x)

#define MIDGARD_MMU_TOPLEVEL    MIDGARD_MMU_LEVEL(0)

#define MIDGARD_MMU_BOTTOMLEVEL MIDGARD_MMU_LEVEL(3)

#define GROWABLE_FLAGS_REQUIRED (KBASE_REG_PF_GROW | KBASE_REG_GPU_WR)

/** setting in kbase_context::as_nr that indicates it's invalid */
#define KBASEP_AS_NR_INVALID     (-1)

/**
 * KBASE_LOCK_REGION_MAX_SIZE_LOG2 - Maximum size in bytes of a MMU lock region,
 *                                   as a logarithm
 */
#define KBASE_LOCK_REGION_MAX_SIZE_LOG2 (48) /*  256 TB */

/**
 * KBASE_REG_ZONE_MAX - Maximum number of GPU memory region zones
 */
#if MALI_USE_CSF
#define KBASE_REG_ZONE_MAX 6ul
#else
#define KBASE_REG_ZONE_MAX 4ul
#endif

#include "mali_kbase_hwaccess_defs.h"

/* Maximum number of pages of memory that require a permanent mapping, per
 * kbase_context
 */
#define KBASE_PERMANENTLY_MAPPED_MEM_LIMIT_PAGES ((64 * 1024ul * 1024ul) >> PAGE_SHIFT)
/* Minimum threshold period for hwcnt dumps between different hwcnt virtualizer
 * clients, to reduce undesired system load.
 * If a virtualizer client requests a dump within this threshold period after
 * some other client has performed a dump, a new dump won't be performed and
 * the accumulated counter values for that client will be returned instead.
 */
#define KBASE_HWCNT_GPU_VIRTUALIZER_DUMP_THRESHOLD_NS (200 * NSEC_PER_USEC)

#if MALI_USE_CSF
/* The buffer count of CSF hwcnt backend ring buffer, which is used when CSF
 * hwcnt backend allocate the ring buffer to communicate with CSF firmware for
 * HWC dump samples.
 * To meet the hardware requirement, this number MUST be power of 2, otherwise,
 * CSF hwcnt backend creation will be failed.
 */
#define KBASE_HWCNT_BACKEND_CSF_RING_BUFFER_COUNT (128)
#endif

/* Maximum number of clock/regulator pairs that may be referenced by
 * the device node.
 * This is dependent on support for of_property_read_u64_array() in the
 * kernel.
 * While, the number of clocks could be more than regulators,
 * as mentioned in power_control_init().
 */
#define BASE_MAX_NR_CLOCKS_REGULATORS (4)

/* Forward declarations */
struct kbase_context;
struct kbase_device;
struct kbase_as;
struct kbase_mmu_setup;
struct kbase_kinstr_jm;

/**
 * struct kbase_io_access - holds information about 1 register access
 *
 * @addr: first bit indicates r/w (r=0, w=1)
 * @value: value written or read
 */
struct kbase_io_access {
	uintptr_t addr;
	u32 value;
};

/**
 * struct kbase_io_history - keeps track of all recent register accesses
 *
 * @enabled: true if register accesses are recorded, false otherwise
 * @lock: spinlock protecting kbase_io_access array
 * @count: number of registers read/written
 * @size: number of elements in kbase_io_access array
 * @buf: array of kbase_io_access
 */
struct kbase_io_history {
	bool enabled;

	spinlock_t lock;
	size_t count;
	u16 size;
	struct kbase_io_access *buf;
};

/**
 * struct kbase_debug_copy_buffer - information about the buffer to be copied.
 *
 * @size:	size of the buffer in bytes
 * @pages:	pointer to an array of pointers to the pages which contain
 *		the buffer
 * @is_vmalloc: true if @pages was allocated with vzalloc. false if @pages was
 *              allocated with kcalloc
 * @nr_pages:	number of pages
 * @offset:	offset into the pages
 * @gpu_alloc:	pointer to physical memory allocated by the GPU
 * @extres_pages: array of pointers to the pages containing external resources
 *		for this buffer
 * @nr_extres_pages: number of pages in @extres_pages
 */
struct kbase_debug_copy_buffer {
	size_t size;
	struct page **pages;
	bool is_vmalloc;
	int nr_pages;
	size_t offset;
	struct kbase_mem_phy_alloc *gpu_alloc;

	struct page **extres_pages;
	int nr_extres_pages;
};

struct kbase_device_info {
	u32 features;
};

struct kbase_mmu_setup {
	u64	transtab;
	u64	memattr;
	u64	transcfg;
};

/**
 * struct kbase_fault - object containing data relating to a page or bus fault.
 * @addr:           Records the faulting address.
 * @extra_addr:     Records the secondary fault address.
 * @status:         Records the fault status as reported by Hw.
 * @protected_mode: Flag indicating whether the fault occurred in protected mode
 *                  or not.
 */
struct kbase_fault {
	u64 addr;
	u64 extra_addr;
	u32 status;
	bool protected_mode;
};

/**
 * struct kbase_mmu_table  - object representing a set of GPU page tables
 * @mmu_teardown_pages:   Array containing pointers to 3 separate pages, used
 *                        to cache the entries of top (L0) & intermediate level
 *                        page tables (L1 & L2) to avoid repeated calls to
 *                        kmap_atomic() during the MMU teardown.
 * @mmu_lock:             Lock to serialize the accesses made to multi level GPU
 *                        page tables
 * @pgd:                  Physical address of the page allocated for the top
 *                        level page table of the context, this is used for
 *                        MMU HW programming as the address translation will
 *                        start from the top level page table.
 * @group_id:             A memory group ID to be passed to a platform-specific
 *                        memory group manager.
 *                        Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @kctx:                 If this set of MMU tables belongs to a context then
 *                        this is a back-reference to the context, otherwise
 *                        it is NULL
 */
struct kbase_mmu_table {
	u64 *mmu_teardown_pages[MIDGARD_MMU_BOTTOMLEVEL];
	struct mutex mmu_lock;
	phys_addr_t pgd;
	u8 group_id;
	struct kbase_context *kctx;
};

/**
 * struct kbase_reg_zone - Information about GPU memory region zones
 * @base_pfn: Page Frame Number in GPU virtual address space for the start of
 *            the Zone
 * @va_size_pages: Size of the Zone in pages
 *
 * Track information about a zone KBASE_REG_ZONE() and related macros.
 * In future, this could also store the &rb_root that are currently in
 * &kbase_context and &kbase_csf_device.
 */
struct kbase_reg_zone {
	u64 base_pfn;
	u64 va_size_pages;
};

#if MALI_USE_CSF
#include "csf/mali_kbase_csf_defs.h"
#else
#include "jm/mali_kbase_jm_defs.h"
#endif

static inline int kbase_as_has_bus_fault(struct kbase_as *as,
	struct kbase_fault *fault)
{
	return (fault == &as->bf_data);
}

static inline int kbase_as_has_page_fault(struct kbase_as *as,
	struct kbase_fault *fault)
{
	return (fault == &as->pf_data);
}

/**
 * struct kbasep_mem_device - Data stored per device for memory allocation
 *
 * @used_pages:   Tracks usage of OS shared memory. Updated when OS memory is
 *                allocated/freed.
 * @ir_threshold: Fraction of the maximum size of an allocation that grows
 *                on GPU page fault that can be used before the driver
 *                switches to incremental rendering, in 1/256ths.
 *                0 means disabled.
 */
struct kbasep_mem_device {
	atomic_t used_pages;
	atomic_t ir_threshold;
};

struct kbase_clk_rate_listener;

/**
 * typedef kbase_clk_rate_listener_on_change_t() - Frequency change callback
 *
 * @listener:     Clock frequency change listener.
 * @clk_index:    Index of the clock for which the change has occurred.
 * @clk_rate_hz:  Clock frequency(Hz).
 *
 * A callback to call when clock rate changes. The function must not
 * sleep. No clock rate manager functions must be called from here, as
 * its lock is taken.
 */
typedef void
kbase_clk_rate_listener_on_change_t(struct kbase_clk_rate_listener *listener,
				    u32 clk_index, u32 clk_rate_hz);

/**
 * struct kbase_clk_rate_listener - Clock frequency listener
 *
 * @node:        List node.
 * @notify:    Callback to be called when GPU frequency changes.
 */
struct kbase_clk_rate_listener {
	struct list_head node;
	kbase_clk_rate_listener_on_change_t *notify;
};

/**
 * struct kbase_clk_rate_trace_manager - Data stored per device for GPU clock
 *                                       rate trace manager.
 *
 * @gpu_idle:           Tracks the idle state of GPU.
 * @clks:               Array of pointer to structures storing data for every
 *                      enumerated GPU clock.
 * @clk_rate_trace_ops: Pointer to the platform specific GPU clock rate trace
 *                      operations.
 * @listeners:          List of listener attached.
 * @lock:               Lock to serialize the actions of GPU clock rate trace
 *                      manager.
 */
struct kbase_clk_rate_trace_manager {
	bool gpu_idle;
	struct kbase_clk_data *clks[BASE_MAX_NR_CLOCKS_REGULATORS];
	struct kbase_clk_rate_trace_op_conf *clk_rate_trace_ops;
	struct list_head listeners;
	spinlock_t lock;
};

/**
 * struct kbase_pm_device_data - Data stored per device for power management.
 * @lock: The lock protecting Power Management structures accessed
 *        outside of IRQ.
 *        This lock must also be held whenever the GPU is being
 *        powered on or off.
 * @active_count: The reference count of active contexts on this device.
 *                Note that some code paths keep shaders/the tiler
 *                powered whilst this is 0.
 *                Use kbase_pm_is_active() instead to check for such cases.
 * @suspending: Flag indicating suspending/suspended
 * @runtime_active: Flag to track if the GPU is in runtime suspended or active
 *                  state. This ensures that runtime_put and runtime_get
 *                  functions are called in pairs. For example if runtime_get
 *                  has already been called from the power_on callback, then
 *                  the call to it from runtime_gpu_active callback can be
 *                  skipped.
 * @gpu_lost: Flag indicating gpu lost
 *            This structure contains data for the power management framework.
 *            There is one instance of this structure per device in the system.
 * @zero_active_count_wait: Wait queue set when active_count == 0
 * @resume_wait: system resume of GPU device.
 * @debug_core_mask: Bit masks identifying the available shader cores that are
 *                   specified via sysfs. One mask per job slot.
 * @debug_core_mask_all: Bit masks identifying the available shader cores that
 *                       are specified via sysfs.
 * @callback_power_runtime_init: Callback for initializing the runtime power
 *                               management. Return 0 on success, else error code
 * @callback_power_runtime_term: Callback for terminating the runtime power
 *                               management.
 * @dvfs_period: Time in milliseconds between each dvfs sample
 * @backend: KBase PM backend data
 * @arb_vm_state: The state of the arbiter VM machine
 * @gpu_users_waiting: Used by virtualization to notify the arbiter that there
 *                     are users waiting for the GPU so that it can request
 *                     and resume the driver.
 * @clk_rtm: The state of the GPU clock rate trace manager
 */
struct kbase_pm_device_data {
	struct mutex lock;
	int active_count;
	bool suspending;
#if MALI_USE_CSF
	bool runtime_active;
#endif
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	atomic_t gpu_lost;
#endif /* CONFIG_MALI_ARBITER_SUPPORT */
	wait_queue_head_t zero_active_count_wait;
	wait_queue_head_t resume_wait;

#if MALI_USE_CSF
	u64 debug_core_mask;
#else
	/* One mask per job slot. */
	u64 debug_core_mask[BASE_JM_MAX_NR_SLOTS];
	u64 debug_core_mask_all;
#endif /* MALI_USE_CSF */

	int (*callback_power_runtime_init)(struct kbase_device *kbdev);
	void (*callback_power_runtime_term)(struct kbase_device *kbdev);
	u32 dvfs_period;
	struct kbase_pm_backend_data backend;
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	struct kbase_arbiter_vm_state *arb_vm_state;
	atomic_t gpu_users_waiting;
#endif /* CONFIG_MALI_ARBITER_SUPPORT */
	struct kbase_clk_rate_trace_manager clk_rtm;
};

/**
 * struct kbase_mem_pool - Page based memory pool for kctx/kbdev
 * @kbdev:                     Kbase device where memory is used
 * @cur_size:                  Number of free pages currently in the pool (may exceed
 *                             @max_size in some corner cases)
 * @max_size:                  Maximum number of free pages in the pool
 * @order:                     order = 0 refers to a pool of 4 KB pages
 *                             order = 9 refers to a pool of 2 MB pages (2^9 * 4KB = 2 MB)
 * @group_id:                  A memory group ID to be passed to a platform-specific
 *                             memory group manager, if present. Immutable.
 *                             Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @pool_lock:                 Lock protecting the pool - must be held when modifying
 *                             @cur_size and @page_list
 * @page_list:                 List of free pages in the pool
 * @reclaim:                   Shrinker for kernel reclaim of free pages
 * @isolation_in_progress_cnt: Number of pages in pool undergoing page isolation.
 *                             This is used to avoid race condition between pool termination
 *                             and page isolation for page migration.
 * @next_pool:                 Pointer to next pool where pages can be allocated when this
 *                             pool is empty. Pages will spill over to the next pool when
 *                             this pool is full. Can be NULL if there is no next pool.
 * @dying:                     true if the pool is being terminated, and any ongoing
 *                             operations should be abandoned
 * @dont_reclaim:              true if the shrinker is forbidden from reclaiming memory from
 *                             this pool, eg during a grow operation
 */
struct kbase_mem_pool {
	struct kbase_device *kbdev;
	size_t cur_size;
	size_t max_size;
	u8 order;
	u8 group_id;
	spinlock_t pool_lock;
	struct list_head page_list;
	struct shrinker reclaim;
	atomic_t isolation_in_progress_cnt;

	struct kbase_mem_pool *next_pool;

	bool dying;
	bool dont_reclaim;
};

/**
 * struct kbase_mem_pool_group - a complete set of physical memory pools.
 *
 * @small: Array of objects containing the state for pools of 4 KiB size
 *         physical pages.
 * @large: Array of objects containing the state for pools of 2 MiB size
 *         physical pages.
 *
 * Memory pools are used to allow efficient reallocation of previously-freed
 * physical pages. A pair of memory pools is initialized for each physical
 * memory group: one for 4 KiB pages and one for 2 MiB pages. These arrays
 * should be indexed by physical memory group ID, the meaning of which is
 * defined by the systems integrator.
 */
struct kbase_mem_pool_group {
	struct kbase_mem_pool small[MEMORY_GROUP_MANAGER_NR_GROUPS];
	struct kbase_mem_pool large[MEMORY_GROUP_MANAGER_NR_GROUPS];
};

/**
 * struct kbase_mem_pool_config - Initial configuration for a physical memory
 *                                pool
 *
 * @max_size: Maximum number of free pages that the pool can hold.
 */
struct kbase_mem_pool_config {
	size_t max_size;
};

/**
 * struct kbase_mem_pool_group_config - Initial configuration for a complete
 *                                      set of physical memory pools
 *
 * @small: Array of initial configuration for pools of 4 KiB pages.
 * @large: Array of initial configuration for pools of 2 MiB pages.
 *
 * This array should be indexed by physical memory group ID, the meaning
 * of which is defined by the systems integrator.
 */
struct kbase_mem_pool_group_config {
	struct kbase_mem_pool_config small[MEMORY_GROUP_MANAGER_NR_GROUPS];
	struct kbase_mem_pool_config large[MEMORY_GROUP_MANAGER_NR_GROUPS];
};

/**
 * struct kbase_devfreq_opp - Lookup table for converting between nominal OPP
 *                            frequency, real frequencies and core mask
 * @real_freqs: Real GPU frequencies.
 * @opp_volts: OPP voltages.
 * @opp_freq:  Nominal OPP frequency
 * @core_mask: Shader core mask
 */
struct kbase_devfreq_opp {
	u64 opp_freq;
	u64 core_mask;
	u64 real_freqs[BASE_MAX_NR_CLOCKS_REGULATORS];
	u32 opp_volts[BASE_MAX_NR_CLOCKS_REGULATORS];
};

/* MMU mode flags */
#define KBASE_MMU_MODE_HAS_NON_CACHEABLE (1ul << 0) /* Has NON_CACHEABLE MEMATTR */

/**
 * struct kbase_mmu_mode - object containing pointer to methods invoked for
 *                         programming the MMU, as per the MMU mode supported
 *                         by Hw.
 * @update:           enable & setup/configure one of the GPU address space.
 * @get_as_setup:     retrieve the configuration of one of the GPU address space.
 * @disable_as:       disable one of the GPU address space.
 * @pte_to_phy_addr:  retrieve the physical address encoded in the page table entry.
 * @ate_is_valid:     check if the pte is a valid address translation entry
 *                    encoding the physical address of the actual mapped page.
 * @pte_is_valid:     check if the pte is a valid entry encoding the physical
 *                    address of the next lower level page table.
 * @entry_set_ate:    program the pte to be a valid address translation entry to
 *                    encode the physical address of the actual page being mapped.
 * @entry_set_pte:    program the pte to be a valid entry to encode the physical
 *                    address of the next lower level page table and also update
 *                    the number of valid entries.
 * @entries_invalidate: clear out or invalidate a range of ptes.
 * @get_num_valid_entries: returns the number of valid entries for a specific pgd.
 * @set_num_valid_entries: sets the number of valid entries for a specific pgd
 * @flags:            bitmask of MMU mode flags. Refer to KBASE_MMU_MODE_ constants.
 */
struct kbase_mmu_mode {
	void (*update)(struct kbase_device *kbdev,
			struct kbase_mmu_table *mmut,
			int as_nr);
	void (*get_as_setup)(struct kbase_mmu_table *mmut,
			struct kbase_mmu_setup * const setup);
	void (*disable_as)(struct kbase_device *kbdev, int as_nr);
	phys_addr_t (*pte_to_phy_addr)(u64 entry);
	int (*ate_is_valid)(u64 ate, int level);
	int (*pte_is_valid)(u64 pte, int level);
	void (*entry_set_ate)(u64 *entry, struct tagged_addr phy,
			unsigned long flags, int level);
	void (*entry_set_pte)(u64 *entry, phys_addr_t phy);
	void (*entries_invalidate)(u64 *entry, u32 count);
	unsigned int (*get_num_valid_entries)(u64 *pgd);
	void (*set_num_valid_entries)(u64 *pgd,
				      unsigned int num_of_valid_entries);
	unsigned long flags;
};

struct kbase_mmu_mode const *kbase_mmu_mode_get_aarch64(void);

#define DEVNAME_SIZE	16

/**
 * enum kbase_devfreq_work_type - The type of work to perform in the devfreq
 *                                suspend/resume worker.
 * @DEVFREQ_WORK_NONE:    Initilisation state.
 * @DEVFREQ_WORK_SUSPEND: Call devfreq_suspend_device().
 * @DEVFREQ_WORK_RESUME:  Call devfreq_resume_device().
 */
enum kbase_devfreq_work_type {
	DEVFREQ_WORK_NONE,
	DEVFREQ_WORK_SUSPEND,
	DEVFREQ_WORK_RESUME
};

/**
 * struct kbase_devfreq_queue_info - Object representing an instance for managing
 *                                   the queued devfreq suspend/resume works.
 * @workq:                 Workqueue for devfreq suspend/resume requests
 * @work:                  Work item for devfreq suspend & resume
 * @req_type:              Requested work type to be performed by the devfreq
 *                         suspend/resume worker
 * @acted_type:            Work type has been acted on by the worker, i.e. the
 *                         internal recorded state of the suspend/resume
 */
struct kbase_devfreq_queue_info {
	struct workqueue_struct *workq;
	struct work_struct work;
	enum kbase_devfreq_work_type req_type;
	enum kbase_devfreq_work_type acted_type;
};

/**
 * struct kbase_process - Representing an object of a kbase process instantiated
 *                        when the first kbase context is created under it.
 * @tgid:               Thread group ID.
 * @total_gpu_pages:    Total gpu pages allocated across all the contexts
 *                      of this process, it accounts for both native allocations
 *                      and dma_buf imported allocations.
 * @kctx_list:          List of kbase contexts created for the process.
 * @kprcs_node:         Node to a rb_tree, kbase_device will maintain a rb_tree
 *                      based on key tgid, kprcs_node is the node link to
 *                      &struct_kbase_device.process_root.
 * @dma_buf_root:       RB tree of the dma-buf imported allocations, imported
 *                      across all the contexts created for this process.
 *                      Used to ensure that pages of allocation are accounted
 *                      only once for the process, even if the allocation gets
 *                      imported multiple times for the process.
 */
struct kbase_process {
	pid_t tgid;
	size_t total_gpu_pages;
	struct list_head kctx_list;

	struct rb_node kprcs_node;
	struct rb_root dma_buf_root;
};

/**
 * struct kbase_mem_migrate - Object representing an instance for managing
 *                            page migration.
 *
 * @mapping:          Pointer to address space struct used for page migration.
 * @free_pages_list:  List of deferred pages to free. Mostly used when page migration
 *                    is enabled. Pages in memory pool that require migrating
 *                    will be freed instead. However page cannot be freed
 *                    right away as Linux will need to release the page lock.
 *                    Therefore page will be added to this list and freed later.
 * @free_pages_lock:  This lock should be held when adding or removing pages
 *                    from @free_pages_list.
 * @free_pages_workq: Work queue to process the work items queued to free
 *                    pages in @free_pages_list.
 * @free_pages_work:  Work item to free pages in @free_pages_list.
 */
struct kbase_mem_migrate {
	struct address_space *mapping;
	struct list_head free_pages_list;
	spinlock_t free_pages_lock;
	struct workqueue_struct *free_pages_workq;
	struct work_struct free_pages_work;
};

/**
 * struct kbase_device   - Object representing an instance of GPU platform device,
 *                         allocated from the probe method of mali driver.
 * @hw_quirks_sc:          Configuration to be used for the shader cores as per
 *                         the HW issues present in the GPU.
 * @hw_quirks_tiler:       Configuration to be used for the Tiler as per the HW
 *                         issues present in the GPU.
 * @hw_quirks_mmu:         Configuration to be used for the MMU as per the HW
 *                         issues present in the GPU.
 * @hw_quirks_gpu:         Configuration to be used for the Job Manager or CSF/MCU
 *                         subsystems as per the HW issues present in the GPU.
 * @entry:                 Links the device instance to the global list of GPU
 *                         devices. The list would have as many entries as there
 *                         are GPU device instances.
 * @dev:                   Pointer to the kernel's generic/base representation
 *                         of the GPU platform device.
 * @mdev:                  Pointer to the miscellaneous device registered to
 *                         provide Userspace access to kernel driver through the
 *                         device file /dev/malixx.
 * @reg_start:             Base address of the region in physical address space
 *                         where GPU registers have been mapped.
 * @reg_size:              Size of the region containing GPU registers
 * @reg:                   Kernel virtual address of the region containing GPU
 *                         registers, using which Driver will access the registers.
 * @irqs:                  Array containing IRQ resource info for 3 types of
 *                         interrupts : Job scheduling, MMU & GPU events (like
 *                         power management, cache etc.)
 * @irqs.irq:              irq number
 * @irqs.flags:            irq flags
 * @clocks:                Pointer to the input clock resources referenced by
 *                         the GPU device node.
 * @scmi_clk:              Pointer to the input scmi clock resources
 * @nr_clocks:             Number of clocks set in the clocks array.
 * @regulators:            Pointer to the structs corresponding to the
 *                         regulators referenced by the GPU device node.
 * @nr_regulators:         Number of regulators set in the regulators array.
 * @opp_table:             Pointer to the device OPP structure maintaining the
 *                         link to OPPs attached to a device. This is obtained
 *                         after setting regulator names for the device.
 * @devname:               string containing the name used for GPU device instance,
 *                         miscellaneous device is registered using the same name.
 * @id:                    Unique identifier for the device, indicates the number of
 *                         devices which have been created so far.
 * @model:                 Pointer, valid only when Driver is compiled to not access
 *                         the real GPU Hw, to the dummy model which tries to mimic
 *                         to some extent the state & behavior of GPU Hw in response
 *                         to the register accesses made by the Driver.
 * @irq_slab:              slab cache for allocating the work items queued when
 *                         model mimics raising of IRQ to cause an interrupt on CPU.
 * @irq_workq:             workqueue for processing the irq work items.
 * @serving_job_irq:       function to execute work items queued when model mimics
 *                         the raising of JS irq, mimics the interrupt handler
 *                         processing JS interrupts.
 * @serving_gpu_irq:       function to execute work items queued when model mimics
 *                         the raising of GPU irq, mimics the interrupt handler
 *                         processing GPU interrupts.
 * @serving_mmu_irq:       function to execute work items queued when model mimics
 *                         the raising of MMU irq, mimics the interrupt handler
 *                         processing MMU interrupts.
 * @reg_op_lock:           lock used by model to serialize the handling of register
 *                         accesses made by the driver.
 * @pm:                    Per device object for storing data for power management
 *                         framework.
 * @fw_load_lock:          Mutex to protect firmware loading in @ref kbase_open.
 * @csf:                   CSF object for the GPU device.
 * @js_data:               Per device object encapsulating the current context of
 *                         Job Scheduler, which is global to the device and is not
 *                         tied to any particular struct kbase_context running on
 *                         the device
 * @mem_pools:             Global pools of free physical memory pages which can
 *                         be used by all the contexts.
 * @memdev:                keeps track of the in use physical pages allocated by
 *                         the Driver.
 * @mmu_mode:              Pointer to the object containing methods for programming
 *                         the MMU, depending on the type of MMU supported by Hw.
 * @mgm_dev:               Pointer to the memory group manager device attached
 *                         to the GPU device. This points to an internal memory
 *                         group manager if no platform-specific memory group
 *                         manager was retrieved through device tree.
 * @as:                    Array of objects representing address spaces of GPU.
 * @as_free:               Bitpattern of free/available GPU address spaces.
 * @as_to_kctx:            Array of pointers to struct kbase_context, having
 *                         GPU adrress spaces assigned to them.
 * @mmu_mask_change:       Lock to serialize the access to MMU interrupt mask
 *                         register used in the handling of Bus & Page faults.
 * @gpu_props:             Object containing complete information about the
 *                         configuration/properties of GPU HW device in use.
 * @hw_issues_mask:        List of SW workarounds for HW issues
 * @hw_features_mask:      List of available HW features.
 * @disjoint_event:        struct for keeping track of the disjoint information,
 *                         that whether the GPU is in a disjoint state and the
 *                         number of disjoint events that have occurred on GPU.
 * @disjoint_event.count:  disjoint event count
 * @disjoint_event.state:  disjoint event state
 * @nr_hw_address_spaces:  Number of address spaces actually available in the
 *                         GPU, remains constant after driver initialisation.
 * @nr_user_address_spaces: Number of address spaces available to user contexts
 * @hwcnt_backend_csf_if_fw: Firmware interface to access CSF GPU performance
 *                         counters.
 * @hwcnt:                  Structure used for instrumentation and HW counters
 *                         dumping
 * @hwcnt.lock:            The lock should be used when accessing any of the
 *                         following members
 * @hwcnt.kctx:            kbase context
 * @hwcnt.addr:            HW counter address
 * @hwcnt.addr_bytes:      HW counter size in bytes
 * @hwcnt.backend:         Kbase instrumentation backend
 * @hwcnt_gpu_jm_backend:  Job manager GPU backend interface, used as superclass reference
 *                         pointer by hwcnt_gpu_iface, which wraps this implementation in
 *                         order to extend it with periodic dumping functionality.
 * @hwcnt_gpu_iface:       Backend interface for GPU hardware counter access.
 * @hwcnt_watchdog_timer:  Watchdog interface, used by the GPU backend hwcnt_gpu_iface to
 *                         perform periodic dumps in order to prevent hardware counter value
 *                         overflow or saturation.
 * @hwcnt_gpu_ctx:         Context for GPU hardware counter access.
 *                         @hwaccess_lock must be held when calling
 *                         kbase_hwcnt_context_enable() with @hwcnt_gpu_ctx.
 * @hwcnt_gpu_virt:        Virtualizer for GPU hardware counters.
 * @vinstr_ctx:            vinstr context created per device.
 * @kinstr_prfcnt_ctx:     kinstr_prfcnt context created per device.
 * @timeline_flags:        Bitmask defining which sets of timeline tracepoints
 *                         are enabled. If zero, there is no timeline client and
 *                         therefore timeline is disabled.
 * @timeline:              Timeline context created per device.
 * @ktrace:                kbase device's ktrace
 * @reset_timeout_ms:      Number of milliseconds to wait for the soft stop to
 *                         complete for the GPU jobs before proceeding with the
 *                         GPU reset.
 * @lowest_gpu_freq_khz:   Lowest frequency in KHz that the GPU can run at. Used
 *                         to calculate suitable timeouts for wait operations.
 * @cache_clean_in_progress: Set when a cache clean has been started, and
 *                         cleared when it has finished. This prevents multiple
 *                         cache cleans being done simultaneously.
 * @cache_clean_queued:    Pended cache clean operations invoked while another is
 *                         in progress. If this is not 0, another cache clean needs
 *                         to be triggered immediately after completion of the
 *                         current one.
 * @cache_clean_wait:      Signalled when a cache clean has finished.
 * @platform_context:      Platform specific private data to be accessed by
 *                         platform specific config files only.
 * @kctx_list:             List of kbase_contexts created for the device,
 *                         including any contexts that might be created for
 *                         hardware counters.
 * @kctx_list_lock:        Lock protecting concurrent accesses to @kctx_list.
 * @devfreq_profile:       Describes devfreq profile for the Mali GPU device, passed
 *                         to devfreq_add_device() to add devfreq feature to Mali
 *                         GPU device.
 * @devfreq:               Pointer to devfreq structure for Mali GPU device,
 *                         returned on the call to devfreq_add_device().
 * @current_freqs:         The real frequencies, corresponding to
 *                         @current_nominal_freq, at which the Mali GPU device
 *                         is currently operating, as retrieved from
 *                         @devfreq_table in the target callback of
 *                         @devfreq_profile.
 * @current_nominal_freq:  The nominal frequency currently used for the Mali GPU
 *                         device as retrieved through devfreq_recommended_opp()
 *                         using the freq value passed as an argument to target
 *                         callback of @devfreq_profile
 * @current_voltages:      The voltages corresponding to @current_nominal_freq,
 *                         as retrieved from @devfreq_table in the target
 *                         callback of @devfreq_profile.
 * @current_core_mask:     bitmask of shader cores that are currently desired &
 *                         enabled, corresponding to @current_nominal_freq as
 *                         retrieved from @devfreq_table in the target callback
 *                         of @devfreq_profile.
 * @devfreq_table:         Pointer to the lookup table for converting between
 *                         nominal OPP (operating performance point) frequency,
 *                         and real frequency and core mask. This table is
 *                         constructed according to operating-points-v2-mali
 *                         table in devicetree.
 * @num_opps:              Number of operating performance points available for the Mali
 *                         GPU device.
 * @last_devfreq_metrics:  last PM metrics
 * @devfreq_queue:         Per device object for storing data that manages devfreq
 *                         suspend & resume request queue and the related items.
 * @devfreq_cooling:       Pointer returned on registering devfreq cooling device
 *                         corresponding to @devfreq.
 * @ipa_protection_mode_switched: is set to TRUE when GPU is put into protected
 *                         mode. It is a sticky flag which is cleared by IPA
 *                         once it has made use of information that GPU had
 *                         previously entered protected mode.
 * @ipa:                   Top level structure for IPA, containing pointers to both
 *                         configured & fallback models.
 * @ipa.lock:              Access to this struct must be with ipa.lock held
 * @ipa.configured_model:  ipa model to use
 * @ipa.fallback_model:    ipa fallback model
 * @ipa.last_metrics:      Values of the PM utilization metrics from last time
 *                         the power model was invoked. The utilization is
 *                         calculated as the difference between last_metrics
 *                         and the current values.
 * @ipa.force_fallback_model: true if use of fallback model has been forced by
 *                            the User
 * @ipa.last_sample_time:  Records the time when counters, used for dynamic
 *                         energy estimation, were last sampled.
 * @previous_frequency:    Previous frequency of GPU clock used for
 *                         BASE_HW_ISSUE_GPU2017_1336 workaround, This clock is
 *                         restored when L2 is powered on.
 * @job_fault_debug:       Flag to control the dumping of debug data for job faults,
 *                         set when the 'job_fault' debugfs file is opened.
 * @mali_debugfs_directory: Root directory for the debugfs files created by the driver
 * @debugfs_ctx_directory: Directory inside the @mali_debugfs_directory containing
 *                         a sub-directory for every context.
 * @debugfs_instr_directory: Instrumentation debugfs directory
 * @debugfs_as_read_bitmap: bitmap of address spaces for which the bus or page fault
 *                         has occurred.
 * @job_fault_wq:          Waitqueue to block the job fault dumping daemon till the
 *                         occurrence of a job fault.
 * @job_fault_resume_wq:   Waitqueue on which every context with a faulty job wait
 *                         for the job fault dumping to complete before they can
 *                         do bottom half of job done for the atoms which followed
 *                         the faulty atom.
 * @job_fault_resume_workq: workqueue to process the work items queued for the faulty
 *                         atoms, whereby the work item function waits for the dumping
 *                         to get completed.
 * @job_fault_event_list:  List of atoms, each belonging to a different context, which
 *                         generated a job fault.
 * @job_fault_event_lock:  Lock to protect concurrent accesses to @job_fault_event_list
 * @regs_dump_debugfs_data: Contains the offset of register to be read through debugfs
 *                         file "read_register".
 * @regs_dump_debugfs_data.reg_offset: Contains the offset of register to be
 *                         read through debugfs file "read_register".
 * @ctx_num:               Total number of contexts created for the device.
 * @io_history:            Pointer to an object keeping a track of all recent
 *                         register accesses. The history of register accesses
 *                         can be read through "regs_history" debugfs file.
 * @hwaccess:              Contains a pointer to active kbase context and GPU
 *                         backend specific data for HW access layer.
 * @faults_pending:        Count of page/bus faults waiting for bottom half processing
 *                         via workqueues.
 * @mmu_hw_operation_in_progress: Set before sending the MMU command and is
 *                         cleared after the command is complete. Whilst this
 *                         flag is set, the write to L2_PWROFF register will be
 *                         skipped which is needed to workaround the HW issue
 *                         GPU2019-3878. PM state machine is invoked after
 *                         clearing this flag and @hwaccess_lock is used to
 *                         serialize the access.
 * @poweroff_pending:      Set when power off operation for GPU is started, reset when
 *                         power on for GPU is started.
 * @infinite_cache_active_default: Set to enable using infinite cache for all the
 *                         allocations of a new context.
 * @mem_pool_defaults:     Default configuration for the group of memory pools
 *                         created for a new context.
 * @current_gpu_coherency_mode: coherency mode in use, which can be different
 *                         from @system_coherency, when using protected mode.
 * @system_coherency:      coherency mode as retrieved from the device tree.
 * @cci_snoop_enabled:     Flag to track when CCI snoops have been enabled.
 * @snoop_enable_smc:      SMC function ID to call into Trusted firmware to
 *                         enable cache snooping. Value of 0 indicates that it
 *                         is not used.
 * @snoop_disable_smc:     SMC function ID to call disable cache snooping.
 * @protected_ops:         Pointer to the methods for switching in or out of the
 *                         protected mode, as per the @protected_dev being used.
 * @protected_dev:         Pointer to the protected mode switcher device attached
 *                         to the GPU device retrieved through device tree if
 *                         GPU do not support protected mode switching natively.
 * @protected_mode:        set to TRUE when GPU is put into protected mode
 * @protected_mode_transition: set to TRUE when GPU is transitioning into or
 *                         out of protected mode.
 * @protected_mode_hwcnt_desired: True if we want GPU hardware counters to be
 *                         enabled. Counters must be disabled before transition
 *                         into protected mode.
 * @protected_mode_hwcnt_disabled: True if GPU hardware counters are not
 *                         enabled.
 * @protected_mode_hwcnt_disable_work: Work item to disable GPU hardware
 *                         counters, used if atomic disable is not possible.
 * @irq_reset_flush:        Flag to indicate that GPU reset is in-flight and flush of
 *                          IRQ + bottom half is being done, to prevent the writes
 *                          to MMU_IRQ_CLEAR & MMU_IRQ_MASK registers.
 * @inited_subsys:          Bitmap of inited sub systems at the time of device probe.
 *                          Used during device remove or for handling error in probe.
 * @hwaccess_lock:          Lock, which can be taken from IRQ context, to serialize
 *                          the updates made to Job dispatcher + scheduler states.
 * @mmu_hw_mutex:           Protects access to MMU operations and address space
 *                          related state.
 * @serialize_jobs:         Currently used mode for serialization of jobs, both
 *                          intra & inter slots serialization is supported.
 * @backup_serialize_jobs:  Copy of the original value of @serialize_jobs taken
 *                          when GWT is enabled. Used to restore the original value
 *                          on disabling of GWT.
 * @js_ctx_scheduling_mode: Context scheduling mode currently being used by
 *                          Job Scheduler
 * @l2_size_override:       Used to set L2 cache size via device tree blob
 * @l2_hash_override:       Used to set L2 cache hash via device tree blob
 * @l2_hash_values_override: true if @l2_hash_values is valid.
 * @l2_hash_values:         Used to set L2 asn_hash via device tree blob
 * @sysc_alloc:             Array containing values to be programmed into
 *                          SYSC_ALLOC[0..7] GPU registers on L2 cache
 *                          power down. These come from either DTB or
 *                          via DebugFS (if it is available in kernel).
 * @process_root:           rb_tree root node for maintaining a rb_tree of
 *                          kbase_process based on key tgid(thread group ID).
 * @dma_buf_root:           rb_tree root node for maintaining a rb_tree of
 *                          &struct kbase_dma_buf based on key dma_buf.
 *                          We maintain a rb_tree of dma_buf mappings under
 *                          kbase_device and kbase_process, one indicates a
 *                          mapping and gpu memory usage at device level and
 *                          other one at process level.
 * @total_gpu_pages:        Total GPU pages used for the complete GPU device.
 * @dma_buf_lock:           This mutex should be held while accounting for
 *                          @total_gpu_pages from imported dma buffers.
 * @gpu_mem_usage_lock:     This spinlock should be held while accounting
 *                          @total_gpu_pages for both native and dma-buf imported
 *                          allocations.
 * @dummy_job_wa:           struct for dummy job execution workaround for the
 *                          GPU hang issue
 * @dummy_job_wa.ctx:       dummy job workaround context
 * @dummy_job_wa.jc:        dummy job workaround job
 * @dummy_job_wa.slot:      dummy job workaround slot
 * @dummy_job_wa.flags:     dummy job workaround flags
 * @dummy_job_wa_loaded:    Flag for indicating that the workaround blob has
 *                          been loaded. Protected by @fw_load_lock.
 * @arb:                    Pointer to the arbiter device
 * @pcm_dev:                The priority control manager device.
 * @oom_notifier_block:     notifier_block containing kernel-registered out-of-
 *                          memory handler.
 * @mem_migrate:            Per device object for managing page migration.
 */
struct kbase_device {
	u32 hw_quirks_sc;
	u32 hw_quirks_tiler;
	u32 hw_quirks_mmu;
	u32 hw_quirks_gpu;

	struct list_head entry;
	struct device *dev;
	struct miscdevice mdev;
	u64 reg_start;
	size_t reg_size;
	void __iomem *reg;

	struct {
		int irq;
		int flags;
	} irqs[3];

	struct clk *clocks[BASE_MAX_NR_CLOCKS_REGULATORS];
	unsigned int nr_clocks;
#if IS_ENABLED(CONFIG_REGULATOR)
	struct regulator *regulators[BASE_MAX_NR_CLOCKS_REGULATORS];
	unsigned int nr_regulators;
#if (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
	struct opp_table *opp_table;
#endif /* (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE */
#endif /* CONFIG_REGULATOR */
	char devname[DEVNAME_SIZE];
	u32  id;

#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
	void *model;
	struct kmem_cache *irq_slab;
	struct workqueue_struct *irq_workq;
	atomic_t serving_job_irq;
	atomic_t serving_gpu_irq;
	atomic_t serving_mmu_irq;
	spinlock_t reg_op_lock;
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
	struct kbase_pm_device_data pm;

	struct kbase_mem_pool_group mem_pools;
	struct kbasep_mem_device memdev;
	struct kbase_mmu_mode const *mmu_mode;

	struct memory_group_manager_device *mgm_dev;

	struct kbase_as as[BASE_MAX_NR_AS];
	u16 as_free;
	struct kbase_context *as_to_kctx[BASE_MAX_NR_AS];

	spinlock_t mmu_mask_change;

	struct kbase_gpu_props gpu_props;

	unsigned long hw_issues_mask[(BASE_HW_ISSUE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];
	unsigned long hw_features_mask[(BASE_HW_FEATURE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];

	struct {
		atomic_t count;
		atomic_t state;
	} disjoint_event;

	s8 nr_hw_address_spaces;
	s8 nr_user_address_spaces;

	/**
	 * @pbha_propagate_bits:   Record of Page-Based Hardware Attribute Propagate bits to
	 *                         restore to L2_CONFIG upon GPU reset.
	 */
	u8 pbha_propagate_bits;

#if MALI_USE_CSF
	struct kbase_hwcnt_backend_csf_if hwcnt_backend_csf_if_fw;
#else
	struct kbase_hwcnt {
		spinlock_t lock;

		struct kbase_context *kctx;
		u64 addr;
		u64 addr_bytes;

		struct kbase_instr_backend backend;
	} hwcnt;

	struct kbase_hwcnt_backend_interface hwcnt_gpu_jm_backend;
#endif

	struct kbase_hwcnt_backend_interface hwcnt_gpu_iface;
	struct kbase_hwcnt_watchdog_interface hwcnt_watchdog_timer;

	struct kbase_hwcnt_context *hwcnt_gpu_ctx;
	struct kbase_hwcnt_virtualizer *hwcnt_gpu_virt;
	struct kbase_vinstr_context *vinstr_ctx;
	struct kbase_kinstr_prfcnt_context *kinstr_prfcnt_ctx;

	atomic_t               timeline_flags;
	struct kbase_timeline *timeline;

#if KBASE_KTRACE_TARGET_RBUF
	struct kbase_ktrace ktrace;
#endif
	u32 reset_timeout_ms;

	u64 lowest_gpu_freq_khz;

	bool cache_clean_in_progress;
	u32 cache_clean_queued;
	wait_queue_head_t cache_clean_wait;

	void *platform_context;

	struct list_head        kctx_list;
	struct mutex            kctx_list_lock;

	struct rockchip_opp_info opp_info;
	bool is_runtime_resumed;
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	struct devfreq_dev_profile devfreq_profile;
	struct devfreq *devfreq;
	unsigned long current_freqs[BASE_MAX_NR_CLOCKS_REGULATORS];
	unsigned long current_nominal_freq;
	unsigned long current_voltages[BASE_MAX_NR_CLOCKS_REGULATORS];
	u64 current_core_mask;
	struct kbase_devfreq_opp *devfreq_table;
	int num_opps;
	struct kbasep_pm_metrics last_devfreq_metrics;
	struct monitor_dev_info *mdev_info;
	struct ipa_power_model_data *model_data;
	struct kbase_devfreq_queue_info devfreq_queue;

#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	struct devfreq_cooling_power dfc_power;
	struct thermal_cooling_device *devfreq_cooling;
	bool ipa_protection_mode_switched;
	struct {
		/* Access to this struct must be with ipa.lock held */
		struct mutex lock;
		struct kbase_ipa_model *configured_model;
		struct kbase_ipa_model *fallback_model;

		/* Values of the PM utilization metrics from last time the
		 * power model was invoked. The utilization is calculated as
		 * the difference between last_metrics and the current values.
		 */
		struct kbasep_pm_metrics last_metrics;

		/* true if use of fallback model has been forced by the User */
		bool force_fallback_model;
		/* Records the time when counters, used for dynamic energy
		 * estimation, were last sampled.
		 */
		ktime_t last_sample_time;
	} ipa;
#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */
	unsigned long previous_frequency;

#if !MALI_USE_CSF
	atomic_t job_fault_debug;
#endif /* !MALI_USE_CSF */

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *mali_debugfs_directory;
	struct dentry *debugfs_ctx_directory;
	struct dentry *debugfs_instr_directory;

#ifdef CONFIG_MALI_BIFROST_DEBUG
	u64 debugfs_as_read_bitmap;
#endif /* CONFIG_MALI_BIFROST_DEBUG */

#if !MALI_USE_CSF
	wait_queue_head_t job_fault_wq;
	wait_queue_head_t job_fault_resume_wq;
	struct workqueue_struct *job_fault_resume_workq;
	struct list_head job_fault_event_list;
	spinlock_t job_fault_event_lock;
#endif /* !MALI_USE_CSF */

#if !MALI_CUSTOMER_RELEASE
	struct {
		u32 reg_offset;
	} regs_dump_debugfs_data;
#endif /* !MALI_CUSTOMER_RELEASE */
#endif /* CONFIG_DEBUG_FS */

	atomic_t ctx_num;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct kbase_io_history io_history;
#endif /* CONFIG_DEBUG_FS */

	struct kbase_hwaccess_data hwaccess;

	atomic_t faults_pending;

#if MALI_USE_CSF
	bool mmu_hw_operation_in_progress;
#endif
	bool poweroff_pending;

	bool infinite_cache_active_default;

	struct kbase_mem_pool_group_config mem_pool_defaults;

	u32 current_gpu_coherency_mode;
	u32 system_coherency;

	bool cci_snoop_enabled;

	u32 snoop_enable_smc;
	u32 snoop_disable_smc;

	const struct protected_mode_ops *protected_ops;

	struct protected_mode_device *protected_dev;

	bool protected_mode;

	bool protected_mode_transition;

	bool protected_mode_hwcnt_desired;

	bool protected_mode_hwcnt_disabled;

	struct work_struct protected_mode_hwcnt_disable_work;


	bool irq_reset_flush;

	u32 inited_subsys;

	spinlock_t hwaccess_lock;

	struct mutex mmu_hw_mutex;

	u8 l2_size_override;
	u8 l2_hash_override;
	bool l2_hash_values_override;
	u32 l2_hash_values[ASN_HASH_COUNT];

	u32 sysc_alloc[SYSC_ALLOC_COUNT];

	struct mutex fw_load_lock;
#if MALI_USE_CSF
	/* CSF object for the GPU device. */
	struct kbase_csf_device csf;
#else
	struct kbasep_js_device_data js_data;

	/* See KBASE_JS_*_PRIORITY_MODE for details. */
	u32 js_ctx_scheduling_mode;

	/* See KBASE_SERIALIZE_* for details */
	u8 serialize_jobs;

#ifdef CONFIG_MALI_CINSTR_GWT
	u8 backup_serialize_jobs;
#endif /* CONFIG_MALI_CINSTR_GWT */

#endif /* MALI_USE_CSF */

	struct rb_root process_root;
	struct rb_root dma_buf_root;

	size_t total_gpu_pages;
	struct mutex dma_buf_lock;
	spinlock_t gpu_mem_usage_lock;

	struct {
		struct kbase_context *ctx;
		u64 jc;
		int slot;
		u64 flags;
	} dummy_job_wa;
	bool dummy_job_wa_loaded;

#ifdef CONFIG_MALI_ARBITER_SUPPORT
		struct kbase_arbiter_device arb;
#endif
	/* Priority Control Manager device */
	struct priority_control_manager_device *pcm_dev;

	struct notifier_block oom_notifier_block;

#if !MALI_USE_CSF
	spinlock_t quick_reset_lock;
	bool quick_reset_enabled;
	/*
	 * 进入 quck_reset_mode 后 (quick_reset_enabled 为 true),
	 * 对已经进入 KBASE_JD_ATOM_STATE_HW_COMPLETED 状态的 atom 的计数.
	 *
	 * 若 num_of_atoms_hw_completed 达到一定值, 将退出 quck_reset_mode.
	 * 见 kbase_js_complete_atom() 对 num_of_atoms_hw_completed 的引用.
	 */
	u32 num_of_atoms_hw_completed;
#endif

	struct kbase_mem_migrate mem_migrate;
};

/**
 * enum kbase_file_state - Initialization state of a file opened by @kbase_open
 *
 * @KBASE_FILE_NEED_VSN:        Initial state, awaiting API version.
 * @KBASE_FILE_VSN_IN_PROGRESS: Indicates if setting an API version is in
 *                              progress and other setup calls shall be
 *                              rejected.
 * @KBASE_FILE_NEED_CTX:        Indicates if the API version handshake has
 *                              completed, awaiting context creation flags.
 * @KBASE_FILE_CTX_IN_PROGRESS: Indicates if the context's setup is in progress
 *                              and other setup calls shall be rejected.
 * @KBASE_FILE_COMPLETE:        Indicates if the setup for context has
 *                              completed, i.e. flags have been set for the
 *                              context.
 *
 * The driver allows only limited interaction with user-space until setup
 * is complete.
 */
enum kbase_file_state {
	KBASE_FILE_NEED_VSN,
	KBASE_FILE_VSN_IN_PROGRESS,
	KBASE_FILE_NEED_CTX,
	KBASE_FILE_CTX_IN_PROGRESS,
	KBASE_FILE_COMPLETE
};

/**
 * struct kbase_file - Object representing a file opened by @kbase_open
 *
 * @kbdev:               Object representing an instance of GPU platform device,
 *                       allocated from the probe method of the Mali driver.
 * @filp:                Pointer to the struct file corresponding to device file
 *                       /dev/malixx instance, passed to the file's open method.
 * @kctx:                Object representing an entity, among which GPU is
 *                       scheduled and which gets its own GPU address space.
 *                       Invalid until @setup_state is KBASE_FILE_COMPLETE.
 * @api_version:         Contains the version number for User/kernel interface,
 *                       used for compatibility check. Invalid until
 *                       @setup_state is KBASE_FILE_NEED_CTX.
 * @setup_state:         Initialization state of the file. Values come from
 *                       the kbase_file_state enumeration.
 */
struct kbase_file {
	struct kbase_device  *kbdev;
	struct file          *filp;
	struct kbase_context *kctx;
	unsigned long         api_version;
	atomic_t              setup_state;
};
#if MALI_JIT_PRESSURE_LIMIT_BASE
/**
 * enum kbase_context_flags - Flags for kbase contexts
 *
 * @KCTX_COMPAT: Set when the context process is a compat process, 32-bit
 * process on a 64-bit kernel.
 *
 * @KCTX_RUNNABLE_REF: Set when context is counted in
 * kbdev->js_data.nr_contexts_runnable. Must hold queue_mutex when accessing.
 *
 * @KCTX_ACTIVE: Set when the context is active.
 *
 * @KCTX_PULLED: Set when last kick() caused atoms to be pulled from this
 * context.
 *
 * @KCTX_MEM_PROFILE_INITIALIZED: Set when the context's memory profile has been
 * initialized.
 *
 * @KCTX_INFINITE_CACHE: Set when infinite cache is to be enabled for new
 * allocations. Existing allocations will not change.
 *
 * @KCTX_SUBMIT_DISABLED: Set to prevent context from submitting any jobs.
 *
 * @KCTX_PRIVILEGED:Set if the context uses an address space and should be kept
 * scheduled in.
 *
 * @KCTX_SCHEDULED: Set when the context is scheduled on the Run Pool.
 * This is only ever updated whilst the jsctx_mutex is held.
 *
 * @KCTX_DYING: Set when the context process is in the process of being evicted.
 *
 * @KCTX_FORCE_SAME_VA: Set when BASE_MEM_SAME_VA should be forced on memory
 * allocations. For 64-bit clients it is enabled by default, and disabled by
 * default on 32-bit clients. Being able to clear this flag is only used for
 * testing purposes of the custom zone allocation on 64-bit user-space builds,
 * where we also require more control than is available through e.g. the JIT
 * allocation mechanism. However, the 64-bit user-space client must still
 * reserve a JIT region using KBASE_IOCTL_MEM_JIT_INIT
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS0: Set when the context has had an atom pulled
 * from it for job slot 0. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS1: Set when the context has had an atom pulled
 * from it for job slot 1. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS2: Set when the context has had an atom pulled
 * from it for job slot 2. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_AS_DISABLED_ON_FAULT: Set when the GPU address space is disabled for
 * the context due to unhandled page(or bus) fault. It is cleared when the
 * refcount for the context drops to 0 or on when the address spaces are
 * re-enabled on GPU reset or power cycle.
 *
 * @KCTX_JPL_ENABLED: Set when JIT physical page limit is less than JIT virtual
 * address page limit, so we must take care to not exceed the physical limit
 *
 * All members need to be separate bits. This enum is intended for use in a
 * bitmask where multiple values get OR-ed together.
 */
enum kbase_context_flags {
	KCTX_COMPAT = 1U << 0,
	KCTX_RUNNABLE_REF = 1U << 1,
	KCTX_ACTIVE = 1U << 2,
	KCTX_PULLED = 1U << 3,
	KCTX_MEM_PROFILE_INITIALIZED = 1U << 4,
	KCTX_INFINITE_CACHE = 1U << 5,
	KCTX_SUBMIT_DISABLED = 1U << 6,
	KCTX_PRIVILEGED = 1U << 7,
	KCTX_SCHEDULED = 1U << 8,
	KCTX_DYING = 1U << 9,
	KCTX_FORCE_SAME_VA = 1U << 11,
	KCTX_PULLED_SINCE_ACTIVE_JS0 = 1U << 12,
	KCTX_PULLED_SINCE_ACTIVE_JS1 = 1U << 13,
	KCTX_PULLED_SINCE_ACTIVE_JS2 = 1U << 14,
	KCTX_AS_DISABLED_ON_FAULT = 1U << 15,
	KCTX_JPL_ENABLED = 1U << 16,
};
#else
/**
 * enum kbase_context_flags - Flags for kbase contexts
 *
 * @KCTX_COMPAT: Set when the context process is a compat process, 32-bit
 * process on a 64-bit kernel.
 *
 * @KCTX_RUNNABLE_REF: Set when context is counted in
 * kbdev->js_data.nr_contexts_runnable. Must hold queue_mutex when accessing.
 *
 * @KCTX_ACTIVE: Set when the context is active.
 *
 * @KCTX_PULLED: Set when last kick() caused atoms to be pulled from this
 * context.
 *
 * @KCTX_MEM_PROFILE_INITIALIZED: Set when the context's memory profile has been
 * initialized.
 *
 * @KCTX_INFINITE_CACHE: Set when infinite cache is to be enabled for new
 * allocations. Existing allocations will not change.
 *
 * @KCTX_SUBMIT_DISABLED: Set to prevent context from submitting any jobs.
 *
 * @KCTX_PRIVILEGED:Set if the context uses an address space and should be kept
 * scheduled in.
 *
 * @KCTX_SCHEDULED: Set when the context is scheduled on the Run Pool.
 * This is only ever updated whilst the jsctx_mutex is held.
 *
 * @KCTX_DYING: Set when the context process is in the process of being evicted.
 *
 *
 * @KCTX_FORCE_SAME_VA: Set when BASE_MEM_SAME_VA should be forced on memory
 * allocations. For 64-bit clients it is enabled by default, and disabled by
 * default on 32-bit clients. Being able to clear this flag is only used for
 * testing purposes of the custom zone allocation on 64-bit user-space builds,
 * where we also require more control than is available through e.g. the JIT
 * allocation mechanism. However, the 64-bit user-space client must still
 * reserve a JIT region using KBASE_IOCTL_MEM_JIT_INIT
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS0: Set when the context has had an atom pulled
 * from it for job slot 0. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS1: Set when the context has had an atom pulled
 * from it for job slot 1. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_PULLED_SINCE_ACTIVE_JS2: Set when the context has had an atom pulled
 * from it for job slot 2. This is reset when the context first goes active or
 * is re-activated on that slot.
 *
 * @KCTX_AS_DISABLED_ON_FAULT: Set when the GPU address space is disabled for
 * the context due to unhandled page(or bus) fault. It is cleared when the
 * refcount for the context drops to 0 or on when the address spaces are
 * re-enabled on GPU reset or power cycle.
 *
 * All members need to be separate bits. This enum is intended for use in a
 * bitmask where multiple values get OR-ed together.
 */
enum kbase_context_flags {
	KCTX_COMPAT = 1U << 0,
	KCTX_RUNNABLE_REF = 1U << 1,
	KCTX_ACTIVE = 1U << 2,
	KCTX_PULLED = 1U << 3,
	KCTX_MEM_PROFILE_INITIALIZED = 1U << 4,
	KCTX_INFINITE_CACHE = 1U << 5,
	KCTX_SUBMIT_DISABLED = 1U << 6,
	KCTX_PRIVILEGED = 1U << 7,
	KCTX_SCHEDULED = 1U << 8,
	KCTX_DYING = 1U << 9,
	KCTX_FORCE_SAME_VA = 1U << 11,
	KCTX_PULLED_SINCE_ACTIVE_JS0 = 1U << 12,
	KCTX_PULLED_SINCE_ACTIVE_JS1 = 1U << 13,
	KCTX_PULLED_SINCE_ACTIVE_JS2 = 1U << 14,
	KCTX_AS_DISABLED_ON_FAULT = 1U << 15,
};
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */

struct kbase_sub_alloc {
	struct list_head link;
	struct page *page;
	DECLARE_BITMAP(sub_pages, SZ_2M / SZ_4K);
};

/**
 * struct kbase_context - Kernel base context
 *
 * @filp:                 Pointer to the struct file corresponding to device file
 *                        /dev/malixx instance, passed to the file's open method.
 * @kbdev:                Pointer to the Kbase device for which the context is created.
 * @kctx_list_link:       Node into Kbase device list of contexts.
 * @mmu:                  Structure holding details of the MMU tables for this
 *                        context
 * @id:                   Unique identifier for the context, indicates the number of
 *                        contexts which have been created for the device so far.
 * @api_version:          contains the version number for User/kernel interface,
 *                        used for compatibility check.
 * @event_list:           list of posted events about completed atoms, to be sent to
 *                        event handling thread of Userpsace.
 * @event_coalesce_list:  list containing events corresponding to successive atoms
 *                        which have requested deferred delivery of the completion
 *                        events to Userspace.
 * @event_mutex:          Lock to protect the concurrent access to @event_list &
 *                        @event_mutex.
 * @event_closed:         Flag set through POST_TERM ioctl, indicates that Driver
 *                        should stop posting events and also inform event handling
 *                        thread that context termination is in progress.
 * @event_workq:          Workqueue for processing work items corresponding to atoms
 *                        that do not return an event to userspace.
 * @event_count:          Count of the posted events to be consumed by Userspace.
 * @event_coalesce_count: Count of the events present in @event_coalesce_list.
 * @flags:                bitmap of enums from kbase_context_flags, indicating the
 *                        state & attributes for the context.
 * @aliasing_sink_page:   Special page used for KBASE_MEM_TYPE_ALIAS allocations,
 *                        which can alias number of memory regions. The page is
 *                        represent a region where it is mapped with a write-alloc
 *                        cache setup, typically used when the write result of the
 *                        GPU isn't needed, but the GPU must write anyway.
 * @mem_partials_lock:    Lock for protecting the operations done on the elements
 *                        added to @mem_partials list.
 * @mem_partials:         List head for the list of large pages, 2MB in size, which
 *                        have been split into 4 KB pages and are used partially
 *                        for the allocations >= 2 MB in size.
 * @reg_lock:             Lock used for GPU virtual address space management operations,
 *                        like adding/freeing a memory region in the address space.
 *                        Can be converted to a rwlock ?.
 * @reg_rbtree_same:      RB tree of the memory regions allocated from the SAME_VA
 *                        zone of the GPU virtual address space. Used for allocations
 *                        having the same value for GPU & CPU virtual address.
 * @reg_rbtree_custom:    RB tree of the memory regions allocated from the CUSTOM_VA
 *                        zone of the GPU virtual address space.
 * @reg_rbtree_exec:      RB tree of the memory regions allocated from the EXEC_VA
 *                        zone of the GPU virtual address space. Used for GPU-executable
 *                        allocations which don't need the SAME_VA property.
 * @reg_rbtree_exec_fixed: RB tree of the memory regions allocated from the
 *                         EXEC_FIXED_VA zone of the GPU virtual address space. Used for
 *                        GPU-executable allocations with FIXED/FIXABLE GPU virtual
 *                        addresses.
 * @reg_rbtree_fixed:     RB tree of the memory regions allocated from the FIXED_VA zone
 *                        of the GPU virtual address space. Used for allocations with
 *                        FIXED/FIXABLE GPU virtual addresses.
 * @num_fixable_allocs:   A count for the number of memory allocations with the
 *                        BASE_MEM_FIXABLE property.
 * @num_fixed_allocs:     A count for the number of memory allocations with the
 *                        BASE_MEM_FIXED property.
 * @reg_zone:             Zone information for the reg_rbtree_<...> members.
 * @cookies:              Bitmask containing of BITS_PER_LONG bits, used mainly for
 *                        SAME_VA allocations to defer the reservation of memory region
 *                        (from the GPU virtual address space) from base_mem_alloc
 *                        ioctl to mmap system call. This helps returning unique
 *                        handles, disguised as GPU VA, to Userspace from base_mem_alloc
 *                        and later retrieving the pointer to memory region structure
 *                        in the mmap handler.
 * @pending_regions:      Array containing pointers to memory region structures,
 *                        used in conjunction with @cookies bitmask mainly for
 *                        providing a mechansim to have the same value for CPU &
 *                        GPU virtual address.
 * @event_queue:          Wait queue used for blocking the thread, which consumes
 *                        the base_jd_event corresponding to an atom, when there
 *                        are no more posted events.
 * @tgid:                 Thread group ID of the process whose thread created
 *                        the context (by calling KBASE_IOCTL_VERSION_CHECK or
 *                        KBASE_IOCTL_SET_FLAGS, depending on the @api_version).
 *                        This is usually, but not necessarily, the same as the
 *                        process whose thread opened the device file
 *                        /dev/malixx instance.
 * @pid:                  ID of the thread, corresponding to process @tgid,
 *                        which actually created the context. This is usually,
 *                        but not necessarily, the same as the thread which
 *                        opened the device file /dev/malixx instance.
 * @csf:                  kbase csf context
 * @jctx:                 object encapsulating all the Job dispatcher related state,
 *                        including the array of atoms.
 * @used_pages:           Keeps a track of the number of 4KB physical pages in use
 *                        for the context.
 * @nonmapped_pages:      Updated in the same way as @used_pages, except for the case
 *                        when special tracking page is freed by userspace where it
 *                        is reset to 0.
 * @permanent_mapped_pages: Usage count of permanently mapped memory
 * @mem_pools:            Context-specific pools of free physical memory pages.
 * @reclaim:              Shrinker object registered with the kernel containing
 *                        the pointer to callback function which is invoked under
 *                        low memory conditions. In the callback function Driver
 *                        frees up the memory for allocations marked as
 *                        evictable/reclaimable.
 * @evict_list:           List head for the list containing the allocations which
 *                        can be evicted or freed up in the shrinker callback.
 * @evict_nents:          Total number of pages allocated by the allocations within
 *                        @evict_list (atomic).
 * @waiting_soft_jobs:    List head for the list containing softjob atoms, which
 *                        are either waiting for the event set operation, or waiting
 *                        for the signaling of input fence or waiting for the GPU
 *                        device to powered on so as to dump the CPU/GPU timestamps.
 * @waiting_soft_jobs_lock: Lock to protect @waiting_soft_jobs list from concurrent
 *                        accesses.
 * @dma_fence:            Object containing list head for the list of dma-buf fence
 *                        waiting atoms and the waitqueue to process the work item
 *                        queued for the atoms blocked on the signaling of dma-buf
 *                        fences.
 * @dma_fence.waiting_resource: list head for the list of dma-buf fence
 * @dma_fence.wq:         waitqueue to process the work item queued
 * @as_nr:                id of the address space being used for the scheduled in
 *                        context. This is effectively part of the Run Pool, because
 *                        it only has a valid setting (!=KBASEP_AS_NR_INVALID) whilst
 *                        the context is scheduled in. The hwaccess_lock must be held
 *                        whilst accessing this.
 *                        If the context relating to this value of as_nr is required,
 *                        then the context must be retained to ensure that it doesn't
 *                        disappear whilst it is being used. Alternatively, hwaccess_lock
 *                        can be held to ensure the context doesn't disappear (but this
 *                        has restrictions on what other locks can be taken simutaneously).
 * @refcount:             Keeps track of the number of users of this context. A user
 *                        can be a job that is available for execution, instrumentation
 *                        needing to 'pin' a context for counter collection, etc.
 *                        If the refcount reaches 0 then this context is considered
 *                        inactive and the previously programmed AS might be cleared
 *                        at any point.
 *                        Generally the reference count is incremented when the context
 *                        is scheduled in and an atom is pulled from the context's per
 *                        slot runnable tree in JM GPU or GPU command queue
 *                        group is programmed on CSG slot in CSF GPU.
 * @mm_update_lock:       lock used for handling of special tracking page.
 * @process_mm:           Pointer to the memory descriptor of the process which
 *                        created the context. Used for accounting the physical
 *                        pages used for GPU allocations, done for the context,
 *                        to the memory consumed by the process.
 * @gpu_va_end:           End address of the GPU va space (in 4KB page units)
 * @running_total_tiler_heap_nr_chunks: Running total of number of chunks in all
 *                        tiler heaps of the kbase context.
 * @running_total_tiler_heap_memory: Running total of the tiler heap memory in the
 *                        kbase context.
 * @peak_total_tiler_heap_memory: Peak value of the total tiler heap memory in the
 *                        kbase context.
 * @jit_va:               Indicates if a JIT_VA zone has been created.
 * @mem_profile_data:     Buffer containing the profiling information provided by
 *                        Userspace, can be read through the mem_profile debugfs file.
 * @mem_profile_size:     Size of the @mem_profile_data.
 * @mem_profile_lock:     Lock to serialize the operations related to mem_profile
 *                        debugfs file.
 * @kctx_dentry:          Pointer to the debugfs directory created for every context,
 *                        inside kbase_device::debugfs_ctx_directory, containing
 *                        context specific files.
 * @reg_dump:             Buffer containing a register offset & value pair, used
 *                        for dumping job fault debug info.
 * @job_fault_count:      Indicates that a job fault occurred for the context and
 *                        dumping of its debug info is in progress.
 * @job_fault_resume_event_list: List containing atoms completed after the faulty
 *                        atom but before the debug data for faulty atom was dumped.
 * @mem_view_column_width: Controls the number of bytes shown in every column of the
 *                         output of "mem_view" debugfs file.
 * @jsctx_queue:          Per slot & priority arrays of object containing the root
 *                        of RB-tree holding currently runnable atoms on the job slot
 *                        and the head item of the linked list of atoms blocked on
 *                        cross-slot dependencies.
 * @slot_tracking:        Tracking and control of this context's use of all job
 *                        slots
 * @atoms_pulled_all_slots: Total number of atoms currently pulled from the
 *                        context, across all slots.
 * @slots_pullable:       Bitmask of slots, indicating the slots for which the
 *                        context has pullable atoms in the runnable tree.
 * @work:                 Work structure used for deferred ASID assignment.
 * @completed_jobs:       List containing completed atoms for which base_jd_event is
 *                        to be posted.
 * @work_count:           Number of work items, corresponding to atoms, currently
 *                        pending on job_done workqueue of @jctx.
 * @soft_job_timeout:     Timer object used for failing/cancelling the waiting
 *                        soft-jobs which have been blocked for more than the
 *                        timeout value used for the soft-jobs
 * @jit_alloc:            Array of 256 pointers to GPU memory regions, used for
 *                        just-in-time memory allocations.
 * @jit_max_allocations:             Maximum allowed number of in-flight
 *                                   just-in-time memory allocations.
 * @jit_current_allocations:         Current number of in-flight just-in-time
 *                                   memory allocations.
 * @jit_current_allocations_per_bin: Current number of in-flight just-in-time
 *                                   memory allocations per bin.
 * @jit_group_id:         A memory group ID to be passed to a platform-specific
 *                        memory group manager.
 *                        Valid range is 0..(MEMORY_GROUP_MANAGER_NR_GROUPS-1).
 * @jit_phys_pages_limit:      Limit of physical pages to apply across all
 *                             just-in-time memory allocations, applied to
 *                             @jit_current_phys_pressure.
 * @jit_current_phys_pressure: Current 'pressure' on physical pages, which is
 *                             the sum of the worst case estimate of pages that
 *                             could be used (i.e. the
 *                             &struct_kbase_va_region.nr_pages for all in-use
 *                             just-in-time memory regions that have not yet had
 *                             a usage report) and the actual number of pages
 *                             that were used (i.e. the
 *                             &struct_kbase_va_region.used_pages for regions
 *                             that have had a usage report).
 * @jit_phys_pages_to_be_allocated: Count of the physical pages that are being
 *                                  now allocated for just-in-time memory
 *                                  allocations of a context (across all the
 *                                  threads). This is supposed to be updated
 *                                  with @reg_lock held before allocating
 *                                  the backing pages. This helps ensure that
 *                                  total physical memory usage for just in
 *                                  time memory allocation remains within the
 *                                  @jit_phys_pages_limit in multi-threaded
 *                                  scenarios.
 * @jit_active_head:      List containing the just-in-time memory allocations
 *                        which are in use.
 * @jit_pool_head:        List containing the just-in-time memory allocations
 *                        which have been freed up by userspace and so not being
 *                        used by them.
 *                        Driver caches them to quickly fulfill requests for new
 *                        JIT allocations. They are released in case of memory
 *                        pressure as they are put on the @evict_list when they
 *                        are freed up by userspace.
 * @jit_destroy_head:     List containing the just-in-time memory allocations
 *                        which were moved to it from @jit_pool_head, in the
 *                        shrinker callback, after freeing their backing
 *                        physical pages.
 * @jit_evict_lock:       Lock used for operations done on just-in-time memory
 *                        allocations and also for accessing @evict_list.
 * @jit_work:             Work item queued to defer the freeing of a memory
 *                        region when a just-in-time memory allocation is moved
 *                        to @jit_destroy_head.
 * @ext_res_meta_head:    A list of sticky external resources which were requested to
 *                        be mapped on GPU side, through a softjob atom of type
 *                        EXT_RES_MAP or STICKY_RESOURCE_MAP ioctl.
 * @age_count:            Counter incremented on every call to jd_submit_atom,
 *                        atom is assigned the snapshot of this counter, which
 *                        is used to determine the atom's age when it is added to
 *                        the runnable RB-tree.
 * @trim_level:           Level of JIT allocation trimming to perform on free (0-100%)
 * @kprcs:                Reference to @struct kbase_process that the current
 *                        kbase_context belongs to.
 * @kprcs_link:           List link for the list of kbase context maintained
 *                        under kbase_process.
 * @gwt_enabled:          Indicates if tracking of GPU writes is enabled, protected by
 *                        kbase_context.reg_lock.
 * @gwt_was_enabled:      Simple sticky bit flag to know if GWT was ever enabled.
 * @gwt_current_list:     A list of addresses for which GPU has generated write faults,
 *                        after the last snapshot of it was sent to userspace.
 * @gwt_snapshot_list:    Snapshot of the @gwt_current_list for sending to user space.
 * @priority:             Indicates the context priority. Used along with @atoms_count
 *                        for context scheduling, protected by hwaccess_lock.
 * @atoms_count:          Number of GPU atoms currently in use, per priority
 * @create_flags:         Flags used in context creation.
 * @kinstr_jm:            Kernel job manager instrumentation context handle
 * @tl_kctx_list_node:    List item into the device timeline's list of
 *                        contexts, for timeline summarization.
 * @limited_core_mask:    The mask that is applied to the affinity in case of atoms
 *                        marked with BASE_JD_REQ_LIMITED_CORE_MASK.
 * @platform_data:        Pointer to platform specific per-context data.
 *
 * A kernel base context is an entity among which the GPU is scheduled.
 * Each context has its own GPU address space.
 * Up to one context can be created for each client that opens the device file
 * /dev/malixx. Context creation is deferred until a special ioctl() system call
 * is made on the device file.
 */
struct kbase_context {
	struct file *filp;
	struct kbase_device *kbdev;
	struct list_head kctx_list_link;
	struct kbase_mmu_table mmu;

	u32 id;
	unsigned long api_version;
	struct list_head event_list;
	struct list_head event_coalesce_list;
	struct mutex event_mutex;
#if !MALI_USE_CSF
	atomic_t event_closed;
#endif
	struct workqueue_struct *event_workq;
	atomic_t event_count;
	int event_coalesce_count;

	atomic_t flags;

	struct tagged_addr aliasing_sink_page;

	spinlock_t              mem_partials_lock;
	struct list_head        mem_partials;

	struct mutex            reg_lock;

	struct rb_root reg_rbtree_same;
	struct rb_root reg_rbtree_custom;
	struct rb_root reg_rbtree_exec;
#if MALI_USE_CSF
	struct rb_root reg_rbtree_exec_fixed;
	struct rb_root reg_rbtree_fixed;
	atomic64_t num_fixable_allocs;
	atomic64_t num_fixed_allocs;
#endif
	struct kbase_reg_zone reg_zone[KBASE_REG_ZONE_MAX];

#if MALI_USE_CSF
	struct kbase_csf_context csf;
#else
	struct kbase_jd_context jctx;
	struct jsctx_queue jsctx_queue
		[KBASE_JS_ATOM_SCHED_PRIO_COUNT][BASE_JM_MAX_NR_SLOTS];
	struct kbase_jsctx_slot_tracking slot_tracking[BASE_JM_MAX_NR_SLOTS];
	atomic_t atoms_pulled_all_slots;

	struct list_head completed_jobs;
	atomic_t work_count;
	struct timer_list soft_job_timeout;

	int priority;
	s16 atoms_count[KBASE_JS_ATOM_SCHED_PRIO_COUNT];
	u32 slots_pullable;
	u32 age_count;
#endif /* MALI_USE_CSF */

	DECLARE_BITMAP(cookies, BITS_PER_LONG);
	struct kbase_va_region *pending_regions[BITS_PER_LONG];

	wait_queue_head_t event_queue;
	pid_t tgid;
	pid_t pid;
	atomic_t used_pages;
	atomic_t nonmapped_pages;
	atomic_t permanent_mapped_pages;

	struct kbase_mem_pool_group mem_pools;

	struct shrinker         reclaim;
	struct list_head        evict_list;
	atomic_t evict_nents;

	struct list_head waiting_soft_jobs;
	spinlock_t waiting_soft_jobs_lock;

	int as_nr;

	atomic_t refcount;

	spinlock_t         mm_update_lock;
	struct mm_struct __rcu *process_mm;
	u64 gpu_va_end;
#if MALI_USE_CSF
	u32 running_total_tiler_heap_nr_chunks;
	u64 running_total_tiler_heap_memory;
	u64 peak_total_tiler_heap_memory;
#endif
	bool jit_va;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	char *mem_profile_data;
	size_t mem_profile_size;
	struct mutex mem_profile_lock;
	struct dentry *kctx_dentry;

	unsigned int *reg_dump;
	atomic_t job_fault_count;
	struct list_head job_fault_resume_event_list;
	unsigned int mem_view_column_width;

#endif /* CONFIG_DEBUG_FS */
	struct kbase_va_region *jit_alloc[1 + BASE_JIT_ALLOC_COUNT];
	u8 jit_max_allocations;
	u8 jit_current_allocations;
	u8 jit_current_allocations_per_bin[256];
	u8 jit_group_id;
#if MALI_JIT_PRESSURE_LIMIT_BASE
	u64 jit_phys_pages_limit;
	u64 jit_current_phys_pressure;
	u64 jit_phys_pages_to_be_allocated;
#endif /* MALI_JIT_PRESSURE_LIMIT_BASE */
	struct list_head jit_active_head;
	struct list_head jit_pool_head;
	struct list_head jit_destroy_head;
	struct mutex jit_evict_lock;
	struct work_struct jit_work;

	struct list_head ext_res_meta_head;

	u8 trim_level;

	struct kbase_process *kprcs;
	struct list_head kprcs_link;

#ifdef CONFIG_MALI_CINSTR_GWT
	bool gwt_enabled;
	bool gwt_was_enabled;
	struct list_head gwt_current_list;
	struct list_head gwt_snapshot_list;
#endif

	base_context_create_flags create_flags;

#if !MALI_USE_CSF
	struct kbase_kinstr_jm *kinstr_jm;
#endif
	struct list_head tl_kctx_list_node;

	u64 limited_core_mask;

#if !MALI_USE_CSF
	void *platform_data;
#endif
};

#ifdef CONFIG_MALI_CINSTR_GWT
/**
 * struct kbasep_gwt_list_element - Structure used to collect GPU
 *                                  write faults.
 * @link:                           List head for adding write faults.
 * @region:                         Details of the region where we have the
 *                                  faulting page address.
 * @page_addr:                      Page address where GPU write fault occurred.
 * @num_pages:                      The number of pages modified.
 *
 * Using this structure all GPU write faults are stored in a list.
 */
struct kbasep_gwt_list_element {
	struct list_head link;
	struct kbase_va_region *region;
	u64 page_addr;
	u64 num_pages;
};

#endif

/**
 * struct kbase_ctx_ext_res_meta - Structure which binds an external resource
 *                                 to a @kbase_context.
 * @ext_res_node:                  List head for adding the metadata to a
 *                                 @kbase_context.
 * @reg:                           External resource information, containing
 *                                 the corresponding VA region
 * @ref:                           Reference count.
 *
 * External resources can be mapped into multiple contexts as well as the same
 * context multiple times.
 * As kbase_va_region is refcounted, we guarantee that it will be available
 * for the duration of the external resource, meaning it is sufficient to use
 * it to rederive any additional data, like the GPU address.
 * This metadata structure binds a single external resource to a single
 * context, ensuring that per context mapping is tracked separately so it can
 * be overridden when needed and abuses by the application (freeing the resource
 * multiple times) don't effect the refcount of the physical allocation.
 */
struct kbase_ctx_ext_res_meta {
	struct list_head ext_res_node;
	struct kbase_va_region *reg;
	u32 ref;
};

enum kbase_reg_access_type {
	REG_READ,
	REG_WRITE
};

enum kbase_share_attr_bits {
	/* (1ULL << 8) bit is reserved */
	SHARE_BOTH_BITS = (2ULL << 8),	/* inner and outer shareable coherency */
	SHARE_INNER_BITS = (3ULL << 8)	/* inner shareable coherency */
};

/**
 * kbase_device_is_cpu_coherent - Returns if the device is CPU coherent.
 * @kbdev: kbase device
 *
 * Return: true if the device access are coherent, false if not.
 */
static inline bool kbase_device_is_cpu_coherent(struct kbase_device *kbdev)
{
	if ((kbdev->system_coherency == COHERENCY_ACE_LITE) ||
			(kbdev->system_coherency == COHERENCY_ACE))
		return true;

	return false;
}

/**
 * kbase_get_lock_region_min_size_log2 - Returns the minimum size of the MMU lock
 * region, as a logarithm
 *
 * @gpu_props:   GPU properties
 *
 * Return: the minimum size of the MMU lock region as dictated by the corresponding
 * arch spec.
 */
static inline u64 kbase_get_lock_region_min_size_log2(struct kbase_gpu_props const *gpu_props)
{
	if (GPU_ID2_MODEL_MATCH_VALUE(gpu_props->props.core_props.product_id) >=
	    GPU_ID2_MODEL_MAKE(12, 0))
		return 12; /* 4 kB */

	return 15; /* 32 kB */
}

/* Conversion helpers for setting up high resolution timers */
#define HR_TIMER_DELAY_MSEC(x) (ns_to_ktime(((u64)(x))*1000000U))
#define HR_TIMER_DELAY_NSEC(x) (ns_to_ktime(x))

/* Maximum number of loops polling the GPU for a cache flush before we assume it must have completed */
#define KBASE_CLEAN_CACHE_MAX_LOOPS     100000
/* Maximum number of loops polling the GPU for an AS command to complete before we assume the GPU has hung */
#define KBASE_AS_INACTIVE_MAX_LOOPS     100000000
/* Maximum number of loops polling the GPU PRFCNT_ACTIVE bit before we assume the GPU has hung */
#define KBASE_PRFCNT_ACTIVE_MAX_LOOPS   100000000

#endif /* _KBASE_DEFS_H_ */
