/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/**
 * @file mali_kbase_defs.h
 *
 * Defintions (types, defines, etcs) common to Kbase. They are placed here to
 * allow the hierarchy of header files to work.
 */

#ifndef _KBASE_DEFS_H_
#define _KBASE_DEFS_H_

#include <mali_kbase_config.h>
#include <mali_base_hwconfig.h>
#include <mali_kbase_mem_lowlevel.h>
#include <mali_kbase_mem_alloc.h>
#include <mali_kbase_mmu_hw.h>


#include <linux/atomic.h>
#include <linux/mempool.h>
#include <linux/slab.h>

#ifdef CONFIG_KDS
#include <linux/kds.h>
#endif				/* CONFIG_KDS */

#ifdef CONFIG_SYNC
#include "sync.h"
#endif				/* CONFIG_SYNC */

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif				/* CONFIG_DEBUG_FS */

#ifdef CONFIG_PM_DEVFREQ
#include <linux/devfreq.h>
#endif /* CONFIG_DEVFREQ */

/** Enable SW tracing when set */
#ifdef CONFIG_MALI_MIDGARD_ENABLE_TRACE
#define KBASE_TRACE_ENABLE 1
#endif

#ifndef KBASE_TRACE_ENABLE
#ifdef CONFIG_MALI_DEBUG
#define KBASE_TRACE_ENABLE 1
#else
#define KBASE_TRACE_ENABLE 0
#endif				/* CONFIG_MALI_DEBUG */
#endif				/* KBASE_TRACE_ENABLE */

/** Dump Job slot trace on error (only active if KBASE_TRACE_ENABLE != 0) */
#define KBASE_TRACE_DUMP_ON_JOB_SLOT_ERROR 1

/**
 * Number of milliseconds before resetting the GPU when a job cannot be "zapped" from the hardware.
 * Note that the time is actually ZAP_TIMEOUT+SOFT_STOP_RESET_TIMEOUT between the context zap starting and the GPU
 * actually being reset to give other contexts time for their jobs to be soft-stopped and removed from the hardware
 * before resetting.
 */
#define ZAP_TIMEOUT             1000

/** Number of milliseconds before we time out on a GPU soft/hard reset */
#define RESET_TIMEOUT           500

/**
 * Prevent soft-stops from occuring in scheduling situations
 *
 * This is not due to HW issues, but when scheduling is desired to be more predictable.
 *
 * Therefore, soft stop may still be disabled due to HW issues.
 *
 * @note Soft stop will still be used for non-scheduling purposes e.g. when terminating a context.
 *
 * @note if not in use, define this value to 0 instead of \#undef'ing it
 */
#define KBASE_DISABLE_SCHEDULING_SOFT_STOPS 0
/**
 * Prevent hard-stops from occuring in scheduling situations
 *
 * This is not due to HW issues, but when scheduling is desired to be more predictable.
 *
 * @note Hard stop will still be used for non-scheduling purposes e.g. when terminating a context.
 *
 * @note if not in use, define this value to 0 instead of \#undef'ing it
 */
#define KBASE_DISABLE_SCHEDULING_HARD_STOPS 0

/**
 * The maximum number of Job Slots to support in the Hardware.
 *
 * You can optimize this down if your target devices will only ever support a
 * small number of job slots.
 */
#define BASE_JM_MAX_NR_SLOTS        16

/**
 * The maximum number of Address Spaces to support in the Hardware.
 *
 * You can optimize this down if your target devices will only ever support a
 * small number of Address Spaces
 */
#define BASE_MAX_NR_AS              16

/* mmu */
#define ENTRY_IS_ATE        1ULL
#define ENTRY_IS_INVAL      2ULL
#define ENTRY_IS_PTE        3ULL

#define MIDGARD_MMU_VA_BITS 48

#define ENTRY_ATTR_BITS (7ULL << 2)	/* bits 4:2 */
#define ENTRY_RD_BIT (1ULL << 6)
#define ENTRY_WR_BIT (1ULL << 7)
#define ENTRY_SHARE_BITS (3ULL << 8)	/* bits 9:8 */
#define ENTRY_ACCESS_BIT (1ULL << 10)
#define ENTRY_NX_BIT (1ULL << 54)

#define ENTRY_FLAGS_MASK (ENTRY_ATTR_BITS | ENTRY_RD_BIT | ENTRY_WR_BIT | ENTRY_SHARE_BITS | ENTRY_ACCESS_BIT | ENTRY_NX_BIT)

#if MIDGARD_MMU_VA_BITS > 39
#define MIDGARD_MMU_TOPLEVEL    0
#else
#define MIDGARD_MMU_TOPLEVEL    1
#endif

#define GROWABLE_FLAGS_REQUIRED (KBASE_REG_PF_GROW)

/** setting in kbase_context::as_nr that indicates it's invalid */
#define KBASEP_AS_NR_INVALID     (-1)

#define KBASE_LOCK_REGION_MAX_SIZE (63)
#define KBASE_LOCK_REGION_MIN_SIZE (11)

#define KBASE_TRACE_SIZE_LOG2 8	/* 256 entries */
#define KBASE_TRACE_SIZE (1 << KBASE_TRACE_SIZE_LOG2)
#define KBASE_TRACE_MASK ((1 << KBASE_TRACE_SIZE_LOG2)-1)

#include "mali_kbase_js_defs.h"

#define KBASEP_FORCE_REPLAY_DISABLED 0

/* Maximum force replay limit when randomization is enabled */
#define KBASEP_FORCE_REPLAY_RANDOM_LIMIT 16

/** Atom has been previously soft-stoppped */
#define KBASE_KATOM_FLAG_BEEN_SOFT_STOPPPED (1<<1)
/** Atom has been previously retried to execute */
#define KBASE_KATOM_FLAGS_RERUN (1<<2)
#define KBASE_KATOM_FLAGS_JOBCHAIN (1<<3)
/** Atom has been previously hard-stopped. */
#define KBASE_KATOM_FLAG_BEEN_HARD_STOPPED (1<<4)
/** Atom has caused us to enter disjoint state */
#define KBASE_KATOM_FLAG_IN_DISJOINT (1<<5)

/* SW related flags about types of JS_COMMAND action
 * NOTE: These must be masked off by JS_COMMAND_MASK */

/** This command causes a disjoint event */
#define JS_COMMAND_SW_CAUSES_DISJOINT 0x100

/** Bitmask of all SW related flags */
#define JS_COMMAND_SW_BITS  (JS_COMMAND_SW_CAUSES_DISJOINT)

#if (JS_COMMAND_SW_BITS & JS_COMMAND_MASK)
#error JS_COMMAND_SW_BITS not masked off by JS_COMMAND_MASK. Must update JS_COMMAND_SW_<..> bitmasks
#endif

/** Soft-stop command that causes a Disjoint event. This of course isn't
 *  entirely masked off by JS_COMMAND_MASK */
#define JS_COMMAND_SOFT_STOP_WITH_SW_DISJOINT \
		(JS_COMMAND_SW_CAUSES_DISJOINT | JS_COMMAND_SOFT_STOP)


struct kbase_jd_atom_dependency
{
	struct kbase_jd_atom *atom;
	u8 dep_type;
};

/**
 * @brief The function retrieves a read-only reference to the atom field from 
 * the  kbase_jd_atom_dependency structure
 *
 * @param[in] dep kbase jd atom dependency.
 *
 * @return readonly reference to dependent ATOM.
 */
static INLINE const struct kbase_jd_atom* const kbase_jd_katom_dep_atom(const struct kbase_jd_atom_dependency* dep)
{
	LOCAL_ASSERT(dep != NULL);
	
	return (const struct kbase_jd_atom* const )(dep->atom);
}
 
/**
 * @brief The function retrieves a read-only reference to the dependency type field from 
 * the  kbase_jd_atom_dependency structure
 *
 * @param[in] dep kbase jd atom dependency.
 *
 * @return A dependency type value.
 */
static INLINE const u8 kbase_jd_katom_dep_type(const struct kbase_jd_atom_dependency* dep)
{
	LOCAL_ASSERT(dep != NULL);

	return dep->dep_type;
}

/**
 * @brief Setter macro for dep_atom array entry in kbase_jd_atom
 *
 * @param[in] dep    The kbase jd atom dependency.
 * @param[in] a      The ATOM to be set as a dependency.
 * @param     type   The ATOM dependency type to be set.
 *
 */
static INLINE void kbase_jd_katom_dep_set(const struct kbase_jd_atom_dependency* const_dep, 
	struct kbase_jd_atom * a,
	u8 type)
{
	struct kbase_jd_atom_dependency* dep;
	
	LOCAL_ASSERT(const_dep != NULL);

	dep = (REINTERPRET_CAST(struct kbase_jd_atom_dependency* )const_dep);

	dep->atom = a;
	dep->dep_type = type; 
}

/**
 * @brief Setter macro for dep_atom array entry in kbase_jd_atom
 *
 * @param[in] dep    The kbase jd atom dependency to be cleared.
 *
 */
static INLINE void kbase_jd_katom_dep_clear(const struct kbase_jd_atom_dependency* const_dep)
{
	struct kbase_jd_atom_dependency* dep;

	LOCAL_ASSERT(const_dep != NULL);

	dep = (REINTERPRET_CAST(struct kbase_jd_atom_dependency* )const_dep);

	dep->atom = NULL;
	dep->dep_type = BASE_JD_DEP_TYPE_INVALID; 
}

struct kbase_ext_res
{
	mali_addr64 gpu_address;
	struct kbase_mem_phy_alloc * alloc;
};

struct kbase_jd_atom {
	struct work_struct work;
	ktime_t start_timestamp;
	u64 time_spent_us; /**< Total time spent on the GPU in microseconds */

	struct base_jd_udata udata;
	struct kbase_context *kctx;

	struct list_head dep_head[2];
	struct list_head dep_item[2];
	const struct kbase_jd_atom_dependency dep[2];

	u16 nr_extres;
	struct kbase_ext_res * extres;

	u32 device_nr;
	u64 affinity;
	u64 jc;
	enum kbase_atom_coreref_state coreref_state;
#ifdef CONFIG_KDS
	struct list_head node;
	struct kds_resource_set *kds_rset;
	mali_bool kds_dep_satisfied;
#endif				/* CONFIG_KDS */
#ifdef CONFIG_SYNC
	struct sync_fence *fence;
	struct sync_fence_waiter sync_waiter;
#endif				/* CONFIG_SYNC */

	/* Note: refer to kbasep_js_atom_retained_state, which will take a copy of some of the following members */
	enum base_jd_event_code event_code;
	base_jd_core_req core_req;	    /**< core requirements */
	/** Job Slot to retry submitting to if submission from IRQ handler failed
	 *
	 * NOTE: see if this can be unified into the another member e.g. the event */
	int retry_submit_on_slot;

	union kbasep_js_policy_job_info sched_info;
	/* atom priority scaled to nice range with +20 offset 0..39 */
	int nice_prio;

	int poking;		/* BASE_HW_ISSUE_8316 */

	wait_queue_head_t completed;
	enum kbase_jd_atom_state status;
#ifdef CONFIG_GPU_TRACEPOINTS
	int work_id;
#endif
	/* Assigned after atom is completed. Used to check whether PRLAM-10676 workaround should be applied */
	int slot_nr;

	u32 atom_flags;

	/* Number of times this atom has been retried. Used by replay soft job.
	 */
	int retry_count;
};

/*
 * Theory of operations:
 *
 * Atom objects are statically allocated within the context structure.
 *
 * Each atom is the head of two lists, one for the "left" set of dependencies, one for the "right" set.
 */

#define KBASE_JD_DEP_QUEUE_SIZE 256

struct kbase_jd_context {
	struct mutex lock;
	struct kbasep_js_kctx_info sched_info;
	struct kbase_jd_atom atoms[BASE_JD_ATOM_COUNT];

	/** Tracks all job-dispatch jobs.  This includes those not tracked by
	 * the scheduler: 'not ready to run' and 'dependency-only' jobs. */
	u32 job_nr;

	/** Waitq that reflects whether there are no jobs (including SW-only
	 * dependency jobs). This is set when no jobs are present on the ctx,
	 * and clear when there are jobs.
	 *
	 * @note: Job Dispatcher knows about more jobs than the Job Scheduler:
	 * the Job Scheduler is unaware of jobs that are blocked on dependencies,
	 * and SW-only dependency jobs.
	 *
	 * This waitq can be waited upon to find out when the context jobs are all
	 * done/cancelled (including those that might've been blocked on
	 * dependencies) - and so, whether it can be terminated. However, it should
	 * only be terminated once it is neither present in the policy-queue (see
	 * kbasep_js_policy_try_evict_ctx() ) nor the run-pool (see
	 * kbasep_js_kctx_info::ctx::is_scheduled).
	 *
	 * Since the waitq is only set under kbase_jd_context::lock,
	 * the waiter should also briefly obtain and drop kbase_jd_context::lock to
	 * guarentee that the setter has completed its work on the kbase_context
	 *
	 * This must be updated atomically with:
	 * - kbase_jd_context::job_nr */
	wait_queue_head_t zero_jobs_wait;

	/** Job Done workqueue. */
	struct workqueue_struct *job_done_wq;

	spinlock_t tb_lock;
	u32 *tb;
	size_t tb_wrap_offset;

#ifdef CONFIG_KDS
	struct kds_callback kds_cb;
#endif				/* CONFIG_KDS */
#ifdef CONFIG_GPU_TRACEPOINTS
	atomic_t work_id;
#endif
};

struct kbase_jm_slot {
	/* The number of slots must be a power of two */
#define BASE_JM_SUBMIT_SLOTS        16
#define BASE_JM_SUBMIT_SLOTS_MASK   (BASE_JM_SUBMIT_SLOTS - 1)

	struct kbase_jd_atom *submitted[BASE_JM_SUBMIT_SLOTS];

	struct kbase_context *last_context;

	u8 submitted_head;
	u8 submitted_nr;
	u8 job_chain_flag;

};

struct kbase_device_info {
	u32 features;
};

/** Poking state for BASE_HW_ISSUE_8316  */
enum {
	KBASE_AS_POKE_STATE_IN_FLIGHT     = 1<<0,
	KBASE_AS_POKE_STATE_KILLING_POKE  = 1<<1
};

/** Poking state for BASE_HW_ISSUE_8316  */
typedef u32 kbase_as_poke_state;

struct kbase_mmu_setup {
	u64	transtab;
	u64	memattr;
};

/**
 * Important: Our code makes assumptions that a struct kbase_as structure is always at
 * kbase_device->as[number]. This is used to recover the containing
 * struct kbase_device from a struct kbase_as structure.
 *
 * Therefore, struct kbase_as structures must not be allocated anywhere else.
 */
struct kbase_as {
	int number;

	struct workqueue_struct *pf_wq;
	struct work_struct work_pagefault;
	struct work_struct work_busfault;
	enum kbase_mmu_fault_type fault_type;
	u32 fault_status;
	mali_addr64 fault_addr;
	struct mutex transaction_mutex;

	struct kbase_mmu_setup current_setup;

	/* BASE_HW_ISSUE_8316  */
	struct workqueue_struct *poke_wq;
	struct work_struct poke_work;
	/** Protected by kbasep_js_device_data::runpool_irq::lock */
	int poke_refcount;
	/** Protected by kbasep_js_device_data::runpool_irq::lock */
	kbase_as_poke_state poke_state;
	struct hrtimer poke_timer;
};

static inline int kbase_as_has_bus_fault(struct kbase_as *as)
{
	return as->fault_type == KBASE_MMU_FAULT_TYPE_BUS;
}

static inline int kbase_as_has_page_fault(struct kbase_as *as)
{
	return as->fault_type == KBASE_MMU_FAULT_TYPE_PAGE;
}

/**
 * Instrumentation State Machine States
 */
enum kbase_instr_state {
	/** State where instrumentation is not active */
	KBASE_INSTR_STATE_DISABLED = 0,
	/** State machine is active and ready for a command. */
	KBASE_INSTR_STATE_IDLE,
	/** Hardware is currently dumping a frame. */
	KBASE_INSTR_STATE_DUMPING,
	/** We've requested a clean to occur on a workqueue */
	KBASE_INSTR_STATE_REQUEST_CLEAN,
	/** Hardware is currently cleaning and invalidating caches. */
	KBASE_INSTR_STATE_CLEANING,
	/** Cache clean completed, and either a) a dump is complete, or
	 * b) instrumentation can now be setup. */
	KBASE_INSTR_STATE_CLEANED,
	/** kbasep_reset_timeout_worker() has started (but not compelted) a
	 * reset. This generally indicates the current action should be aborted, and
	 * kbasep_reset_timeout_worker() will handle the cleanup */
	KBASE_INSTR_STATE_RESETTING,
	/** An error has occured during DUMPING (page fault). */
	KBASE_INSTR_STATE_FAULT
};

void kbasep_reset_timeout_worker(struct work_struct *data);
enum hrtimer_restart kbasep_reset_timer_callback(struct hrtimer *data);

struct kbasep_mem_device {
	atomic_t used_pages;   /* Tracks usage of OS shared memory. Updated
				   when OS memory is allocated/freed. */

};



#define KBASE_TRACE_CODE(X) KBASE_TRACE_CODE_ ## X

enum kbase_trace_code {
	/* IMPORTANT: USE OF SPECIAL #INCLUDE OF NON-STANDARD HEADER FILE
	 * THIS MUST BE USED AT THE START OF THE ENUM */
#define KBASE_TRACE_CODE_MAKE_CODE(X) KBASE_TRACE_CODE(X)
#include "mali_kbase_trace_defs.h"
#undef  KBASE_TRACE_CODE_MAKE_CODE
	/* Comma on its own, to extend the list */
	,
	/* Must be the last in the enum */
	KBASE_TRACE_CODE_COUNT
};

#define KBASE_TRACE_FLAG_REFCOUNT (((u8)1) << 0)
#define KBASE_TRACE_FLAG_JOBSLOT  (((u8)1) << 1)

struct kbase_trace {
	struct timespec timestamp;
	u32 thread_id;
	u32 cpu;
	void *ctx;
	mali_bool katom;
	int atom_number;
	u64 atom_udata[2];
	u64 gpu_addr;
	unsigned long info_val;
	u8 code;
	u8 jobslot;
	u8 refcount;
	u8 flags;
};

/** Event IDs for the power management framework.
 *
 * Any of these events might be missed, so they should not be relied upon to
 * find the precise state of the GPU at a particular time in the
 * trace. Overall, we should get a high percentage of these events for
 * statisical purposes, and so a few missing should not be a problem */
enum kbase_timeline_pm_event {
	/* helper for tests */
	KBASEP_TIMELINE_PM_EVENT_FIRST,

	/** Event reserved for backwards compatibility with 'init' events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_0 = KBASEP_TIMELINE_PM_EVENT_FIRST,

	/** The power state of the device has changed.
	 *
	 * Specifically, the device has reached a desired or available state.
	 */
	KBASE_TIMELINE_PM_EVENT_GPU_STATE_CHANGED,

	/** The GPU is becoming active.
	 *
	 * This event is sent when the first context is about to use the GPU.
	 */
	KBASE_TIMELINE_PM_EVENT_GPU_ACTIVE,

	/** The GPU is becoming idle.
	 *
	 * This event is sent when the last context has finished using the GPU.
	 */
	KBASE_TIMELINE_PM_EVENT_GPU_IDLE,

	/** Event reserved for backwards compatibility with 'policy_change'
	 * events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_4,

	/** Event reserved for backwards compatibility with 'system_suspend'
	 * events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_5,

	/** Event reserved for backwards compatibility with 'system_resume'
	 * events */
	KBASE_TIMELINE_PM_EVENT_RESERVED_6,

	/** The job scheduler is requesting to power up/down cores.
	 *
	 * This event is sent when:
	 * - powered down cores are needed to complete a job
	 * - powered up cores are not needed anymore
	 */
	KBASE_TIMELINE_PM_EVENT_CHANGE_GPU_STATE,

	KBASEP_TIMELINE_PM_EVENT_LAST = KBASE_TIMELINE_PM_EVENT_CHANGE_GPU_STATE,
};

#ifdef CONFIG_MALI_TRACE_TIMELINE
struct kbase_trace_kctx_timeline {
	atomic_t jd_atoms_in_flight;
	u32 owner_tgid;
};

struct kbase_trace_kbdev_timeline {
	/** DebugFS entry */
	struct dentry *dentry;

	/* Note: strictly speaking, not needed, because it's in sync with
	 * kbase_device::jm_slots[]::submitted_nr
	 *
	 * But it's kept as an example of how to add global timeline tracking
	 * information
	 *
	 * The caller must hold kbasep_js_device_data::runpool_irq::lock when
	 * accessing this */
	u8 slot_atoms_submitted[BASE_JM_SUBMIT_SLOTS];

	/* Last UID for each PM event */
	atomic_t pm_event_uid[KBASEP_TIMELINE_PM_EVENT_LAST+1];
	/* Counter for generating PM event UIDs */
	atomic_t pm_event_uid_counter;
	/*
	 * L2 transition state - MALI_TRUE indicates that the transition is ongoing
	 * Expected to be protected by pm.power_change_lock */
	mali_bool l2_transitioning;
};
#endif /* CONFIG_MALI_TRACE_TIMELINE */


struct kbasep_kctx_list_element {
	struct list_head link;
	struct kbase_context    *kctx;
};

#define DEVNAME_SIZE	16

struct kbase_device {
	/** jm_slots is protected by kbasep_js_device_data::runpool_irq::lock */
	struct kbase_jm_slot jm_slots[BASE_JM_MAX_NR_SLOTS];
	s8 slot_submit_count_irq[BASE_JM_MAX_NR_SLOTS];

	struct list_head entry;
	struct device *dev;
	unsigned int kbase_group_error;
	struct miscdevice mdev;
	u64 reg_start;
	size_t reg_size;
	void __iomem *reg;
	struct resource *reg_res;
	struct {
		int irq;
		int flags;
	} irqs[3];
#ifdef CONFIG_HAVE_CLK
	struct clk *clock;
#endif
	char devname[DEVNAME_SIZE];

#ifdef CONFIG_MALI_NO_MALI
	void *model;
	struct kmem_cache *irq_slab;
	struct workqueue_struct *irq_workq;
	atomic_t serving_job_irq;
	atomic_t serving_gpu_irq;
	atomic_t serving_mmu_irq;
	spinlock_t reg_op_lock;
#endif				/* CONFIG_MALI_NO_MALI */

	struct kbase_pm_device_data pm;
	struct kbasep_js_device_data js_data;
	struct kbasep_mem_device memdev;
	struct kbase_as as[BASE_MAX_NR_AS];

	spinlock_t              mmu_mask_change;

	kbase_gpu_props gpu_props;

	/** List of SW workarounds for HW issues */
	unsigned long hw_issues_mask[(BASE_HW_ISSUE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];
	/** List of features available */
	unsigned long hw_features_mask[(BASE_HW_FEATURE_END + BITS_PER_LONG - 1) / BITS_PER_LONG];

	/* Cached present bitmaps - these are the same as the corresponding hardware registers */
	u64 shader_present_bitmap;
	u64 tiler_present_bitmap;
	u64 l2_present_bitmap;
	u64 l3_present_bitmap;

	/* Bitmaps of cores that are currently in use (running jobs).
	 * These should be kept up to date by the job scheduler.
	 *
	 * pm.power_change_lock should be held when accessing these members.
	 *
	 * kbase_pm_check_transitions_nolock() should be called when bits are
	 * cleared to update the power management system and allow transitions to
	 * occur. */
	u64 shader_inuse_bitmap;

	/* Refcount for cores in use */
	u32 shader_inuse_cnt[64];

	/* Bitmaps of cores the JS needs for jobs ready to run */
	u64 shader_needed_bitmap;

	/* Refcount for cores needed */
	u32 shader_needed_cnt[64];

	u32 tiler_inuse_cnt;

	u32 tiler_needed_cnt;

	/* struct for keeping track of the disjoint information
	 *
	 * The state  is > 0 if the GPU is in a disjoint state. Otherwise 0
	 * The count is the number of disjoint events that have occurred on the GPU
	 */
	struct {
		atomic_t count;
		atomic_t state;
	} disjoint_event;

	/* Refcount for tracking users of the l2 cache, e.g. when using hardware counter instrumentation. */
	u32 l2_users_count;

	/* Bitmaps of cores that are currently available (powered up and the power policy is happy for jobs to be
	 * submitted to these cores. These are updated by the power management code. The job scheduler should avoid
	 * submitting new jobs to any cores that are not marked as available.
	 *
	 * pm.power_change_lock should be held when accessing these members.
	 */
	u64 shader_available_bitmap;
	u64 tiler_available_bitmap;
	u64 l2_available_bitmap;

	u64 shader_ready_bitmap;
	u64 shader_transitioning_bitmap;

	s8 nr_hw_address_spaces;			  /**< Number of address spaces in the GPU (constant after driver initialisation) */
	s8 nr_user_address_spaces;			  /**< Number of address spaces available to user contexts */

	/* Structure used for instrumentation and HW counters dumping */
	struct {
		/* The lock should be used when accessing any of the following members */
		spinlock_t lock;

		struct kbase_context *kctx;
		u64 addr;
		wait_queue_head_t wait;
		int triggered;
		enum kbase_instr_state state;
		wait_queue_head_t   cache_clean_wait;
		struct workqueue_struct *cache_clean_wq;
		struct work_struct  cache_clean_work;

		struct kbase_context *suspended_kctx;
		struct kbase_uk_hwcnt_setup suspended_state;
	} hwcnt;

	/* Set when we're about to reset the GPU */
	atomic_t reset_gpu;
#define KBASE_RESET_GPU_NOT_PENDING     0	/* The GPU reset isn't pending */
#define KBASE_RESET_GPU_PREPARED        1	/* kbase_prepare_to_reset_gpu has been called */
#define KBASE_RESET_GPU_COMMITTED       2	/* kbase_reset_gpu has been called - the reset will now definitely happen
						 * within the timeout period */
#define KBASE_RESET_GPU_HAPPENING       3	/* The GPU reset process is currently occuring (timeout has expired or
						 * kbasep_try_reset_gpu_early was called) */

	/* Work queue and work item for performing the reset in */
	struct workqueue_struct *reset_workq;
	struct work_struct reset_work;
	wait_queue_head_t reset_wait;
	struct hrtimer reset_timer;

	/*value to be written to the irq_throttle register each time an irq is served */
	atomic_t irq_throttle_cycles;

	const struct kbase_attribute *config_attributes;

#if KBASE_TRACE_ENABLE
	spinlock_t              trace_lock;
	u16                     trace_first_out;
	u16                     trace_next_in;
	struct kbase_trace            *trace_rbuf;
#endif

#if !MALI_CUSTOMER_RELEASE
	/* This is used to override the current job scheduler values for
	 * KBASE_CONFIG_ATTR_JS_STOP_STOP_TICKS_SS
	 * KBASE_CONFIG_ATTR_JS_STOP_STOP_TICKS_CL
	 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS
	 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_CL
	 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS
	 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS
	 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_CL
	 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS.
	 *
	 * These values are set via the js_timeouts sysfs file.
	 */
	u32 js_soft_stop_ticks;
	u32 js_soft_stop_ticks_cl;
	u32 js_hard_stop_ticks_ss;
	u32 js_hard_stop_ticks_cl;
	u32 js_hard_stop_ticks_nss;
	u32 js_reset_ticks_ss;
	u32 js_reset_ticks_cl;
	u32 js_reset_ticks_nss;
#endif

	struct mutex cacheclean_lock;

	/* Platform specific private data to be accessed by mali_kbase_config_xxx.c only */
	void *platform_context;

	/** Count of contexts keeping the GPU powered */
	atomic_t keep_gpu_powered_count;

	/* List of kbase_contexts created */
	struct list_head        kctx_list;
	struct mutex            kctx_list_lock;

#ifdef CONFIG_MALI_MIDGARD_RT_PM
	struct delayed_work runtime_pm_workqueue;
#endif

#ifdef CONFIG_PM_DEVFREQ
	struct devfreq_dev_profile devfreq_profile;
	struct devfreq *devfreq;
	unsigned long freq;
#ifdef CONFIG_DEVFREQ_THERMAL
	struct devfreq_cooling_device *devfreq_cooling;
#ifdef CONFIG_MALI_POWER_ACTOR
	struct power_actor *power_actor;
#endif
#endif
#endif

#ifdef CONFIG_MALI_TRACE_TIMELINE
	struct kbase_trace_kbdev_timeline timeline;
#endif

#ifdef CONFIG_DEBUG_FS
	/* directory for debugfs entries */
	struct dentry *mali_debugfs_directory;
	/* debugfs entry for gpu_memory */
	struct dentry *gpu_memory_dentry;
	/* debugfs entry for trace */
	struct dentry *trace_dentry;
	/* directory for per-ctx memory profiling data */
	struct dentry *memory_profile_directory;
	/* Root directory for job dispatcher data */
	struct dentry *jd_directory;
#endif /* CONFIG_DEBUG_FS */

	/* fbdump profiling controls set by gator */
	u32 kbase_profiling_controls[FBDUMP_CONTROL_MAX];


#if MALI_CUSTOMER_RELEASE == 0
	/* Number of jobs that are run before a job is forced to fail and
	 * replay. May be KBASEP_FORCE_REPLAY_DISABLED, to disable forced
	 * failures. */
	int force_replay_limit;
	/* Count of jobs between forced failures. Incremented on each job. A
	 * job is forced to fail once this is greater than or equal to
	 * force_replay_limit. */
	int force_replay_count;
	/* Core requirement for jobs to be failed and replayed. May be zero. */
	base_jd_core_req force_replay_core_req;
	/* MALI_TRUE if force_replay_limit should be randomized. The random
	 * value will be in the range of 1 - KBASEP_FORCE_REPLAY_RANDOM_LIMIT.
	 */
	mali_bool force_replay_random;
#endif

	/* Total number of created contexts */
	atomic_t ctx_num;
};

struct kbase_context {
	struct kbase_device *kbdev;
	int id; /* System wide unique id */
	phys_addr_t pgd;
	struct list_head event_list;
	struct mutex event_mutex;
	mali_bool event_closed;
	struct workqueue_struct *event_workq;

	u64 mem_attrs;
	bool is_compat;

	atomic_t                setup_complete;
	atomic_t                setup_in_progress;

	mali_bool keep_gpu_powered;

	u64 *mmu_teardown_pages;

	phys_addr_t aliasing_sink_page;

	struct mutex            reg_lock; /* To be converted to a rwlock? */
	struct rb_root          reg_rbtree; /* Red-Black tree of GPU regions (live regions) */

	unsigned long    cookies;
	struct kbase_va_region *pending_regions[BITS_PER_LONG];
	
	wait_queue_head_t event_queue;
	pid_t tgid;
	pid_t pid;

	struct kbase_jd_context jctx;
	atomic_t used_pages;
	atomic_t         nonmapped_pages;

	struct kbase_mem_allocator osalloc;
	struct kbase_mem_allocator * pgd_allocator;

	struct list_head waiting_soft_jobs;
#ifdef CONFIG_KDS
	struct list_head waiting_kds_resource;
#endif
	/** This is effectively part of the Run Pool, because it only has a valid
	 * setting (!=KBASEP_AS_NR_INVALID) whilst the context is scheduled in
	 *
	 * The kbasep_js_device_data::runpool_irq::lock must be held whilst accessing
	 * this.
	 *
	 * If the context relating to this as_nr is required, you must use
	 * kbasep_js_runpool_retain_ctx() to ensure that the context doesn't disappear
	 * whilst you're using it. Alternatively, just hold the kbasep_js_device_data::runpool_irq::lock
	 * to ensure the context doesn't disappear (but this has restrictions on what other locks
	 * you can take whilst doing this) */
	int as_nr;

	/* NOTE:
	 *
	 * Flags are in jctx.sched_info.ctx.flags
	 * Mutable flags *must* be accessed under jctx.sched_info.ctx.jsctx_mutex
	 *
	 * All other flags must be added there */
	spinlock_t         mm_update_lock;
	struct mm_struct * process_mm;

#ifdef CONFIG_MALI_TRACE_TIMELINE
	struct kbase_trace_kctx_timeline timeline;
#endif
#ifdef CONFIG_DEBUG_FS
	/* debugfs entry for memory profile */
	struct dentry *mem_dentry;
	/* Content of mem_profile file */
	char *mem_profile_data;
	/* Size of @c mem_profile_data */
	size_t mem_profile_size;
	/* Spinlock guarding data */
	spinlock_t mem_profile_lock;
	/* Per-context directory for JD data */
	struct dentry *jd_ctx_dir;
#endif /* CONFIG_DEBUG_FS */
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

/* Conversion helpers for setting up high resolution timers */
#define HR_TIMER_DELAY_MSEC(x) (ns_to_ktime((x)*1000000U))
#define HR_TIMER_DELAY_NSEC(x) (ns_to_ktime(x))

/* Maximum number of loops polling the GPU for a cache flush before we assume it must have completed */
#define KBASE_CLEAN_CACHE_MAX_LOOPS     100000
/* Maximum number of loops polling the GPU for an AS command to complete before we assume the GPU has hung */
#define KBASE_AS_INACTIVE_MAX_LOOPS     100000

/* Maximum number of times a job can be replayed */
#define BASEP_JD_REPLAY_LIMIT 15

#endif				/* _KBASE_DEFS_H_ */
