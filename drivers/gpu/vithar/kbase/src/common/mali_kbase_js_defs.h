/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_js.h
 * Job Scheduler Type Definitions
 */


#ifndef _KBASE_JS_DEFS_H_
#define _KBASE_JS_DEFS_H_

#include "mali_kbase_js_policy_fcfs.h"
#include "mali_kbase_js_policy_cfs.h"

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_kbase_api
 * @{
 */

/**
 * @addtogroup kbase_js
 * @{
 */

/* Wrapper Interface - doxygen is elsewhere */
typedef union kbasep_js_policy
{
#ifdef KBASE_JS_POLICY_AVAILABLE_FCFS
	kbasep_js_policy_fcfs   fcfs;
#endif
#ifdef KBASE_JS_POLICY_AVAILABLE_CFS
	kbasep_js_policy_cfs    cfs;
#endif
} kbasep_js_policy;

/* Wrapper Interface - doxygen is elsewhere */
typedef union kbasep_js_policy_ctx_info
{
#ifdef KBASE_JS_POLICY_AVAILABLE_FCFS
	kbasep_js_policy_fcfs_ctx   fcfs;
#endif
#ifdef KBASE_JS_POLICY_AVAILABLE_CFS
	kbasep_js_policy_cfs_ctx    cfs;
#endif
} kbasep_js_policy_ctx_info;

/* Wrapper Interface - doxygen is elsewhere */
typedef union kbasep_js_policy_job_info
{
#ifdef KBASE_JS_POLICY_AVAILABLE_FCFS
	kbasep_js_policy_fcfs_job fcfs;
#endif
#ifdef KBASE_JS_POLICY_AVAILABLE_CFS
	kbasep_js_policy_cfs_job  cfs;
#endif
} kbasep_js_policy_job_info;

/**
 * @brief Maximum number of jobs that can be submitted to a job slot whilst
 * inside the IRQ handler.
 *
 * This is important because GPU NULL jobs can complete whilst the IRQ handler
 * is running. Otherwise, it potentially allows an unlimited number of GPU NULL
 * jobs to be submitted inside the IRQ handler, which increases IRQ latency.
 */
#define KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ 2

/**
 * @brief the IRQ_THROTTLE time in microseconds
 *
 * This will be converted via the GPU's clock frequency into a cycle-count.
 *
 * @note we can make an estimate of the GPU's frequency by periodically
 * sampling its CYCLE_COUNT register
 */
#define KBASE_JS_IRQ_THROTTLE_TIME_US 20


/**
 * Data used by the scheduler that is unique for each Address Space.
 *
 * This is used in IRQ context and kbasep_js_device_data::runpoool_irq::lock
 * must be held whilst accessing this data (inculding reads and atomic
 * decisions based on the read).
 */
typedef struct kbasep_js_per_as_data
{
	/**
	 * Ref count of whether this AS is busy, and must not be scheduled out
	 *
	 * When jobs are running this is always positive. However, it can still be
	 * positive when no jobs are running. If all you need is a heuristic to
	 * tell you whether jobs might be running, this should be sufficient.
	 */
	int as_busy_refcount;

	/** Pointer to the current context on this address space, or NULL for no context */
	kbase_context *kctx;
} kbasep_js_per_as_data;

/**
 * @brief KBase Device Data Job Scheduler sub-structure
 *
 * This encapsulates the current context of the Job Scheduler on a particular
 * device. This context is global to the device, and is not tied to any
 * particular kbase_context running on the device.
 *
 * nr_contexts_running, nr_nss_ctxs_running, nr_permon_jobs_submitted and as_free are
 * optimized for packing together (by making them smaller types than u32). The
 * operations on them should rarely involve masking. The use of signed types for
 * arithmetic indicates to the compiler that the value will not rollover (which
 * would be undefined behavior), and so under the Total License model, it is free
 * to make optimizations based on that (i.e. to remove masking).
 */
typedef struct kbasep_js_device_data
{
	/** Sub-structure to collect together Job Scheduling data used in IRQ context */
	struct
	{
		/**
		 * Lock for accessing Job Scheduling data used in IRQ context
		 *
		 * This lock must be held whenever this data is accessed (read, or
		 * write). Even for read-only access, memory barriers would be needed.
		 * In any case, it is likely that decisions based on only reading must
		 * also be atomic with respect to data held here and elsewhere in the
		 * Job Scheduler.
		 *
		 * This lock must also be held for accessing:
		 * - kbase_context::as_nr
		 * - Parts of the kbasep_js_policy, dependent on the policy (refer to
		 * the policy in question for more information)
		 * - Parts of kbasep_js_policy_ctx_info, dependent on the policy (refer to
		 * the policy in question for more information)
		 *
		 * If accessing a job slot at the same time, the slot's IRQ lock must
		 * be obtained first to respect lock ordering.
		 */
		osk_spinlock_irq lock;

		/** Bitvector indicating whether a currently scheduled context is allowed to submit jobs.
		 * When bit 'N' is set in this, it indicates whether the context bound to address space
		 * 'N' (per_as_data[N].kctx) is allowed to submit jobs.
		 *
		 * It is placed here because it's much more memory efficient than having a mali_bool8 in
		 * kbasep_js_per_as_data to store this flag  */
		u16 submit_allowed;

		s8 nr_nss_ctxs_running;                 /**< Number of NSS contexts */
		s8 nr_permon_jobs_submitted;			/**< Number of PERMON jobs submitted to hw */
		/** Data that is unique for each AS */
		kbasep_js_per_as_data per_as_data[BASE_MAX_NR_AS];
	} runpool_irq;

	/**
	 * Run Pool mutex, for managing contexts within the runpool.
	 * You must hold this lock whilst accessing any members that follow
	 *
	 * In addition, this is used to access:
	 * - the kbasep_js_kctx_info::runpool substructure
	 */
	osk_mutex runpool_mutex;

	/**
	 * Queue Lock, used to access the Policy's queue of contexts independently
	 * of the Run Pool.
	 *
	 * Of course, you don't need the Run Pool lock to access this.
	 */
	osk_mutex queue_mutex;

	u16 as_free;                            /**< Bitpattern of free Address Spaces */

	s8 nr_contexts_running;                 /**< Number of currently scheduled contexts */

	/**
	 * Policy-specific information.
	 *
	 * Refer to the structure defined by the current policy to determine which
	 * locks must be held when accessing this.
	 */
	kbasep_js_policy policy;

	/** Core Requirements to match up with base_js_atom's core_req memeber
	 * @note This is a write-once member, and so no locking is required to read */
	base_jd_core_req js_reqs[BASE_JM_MAX_NR_SLOTS];

	u32 scheduling_tick_ns;          /**< Value for KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS */
	u32 soft_stop_ticks;             /**< Value for KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS */
	u32 hard_stop_ticks_ss;          /**< Value for KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS */
	u32 hard_stop_ticks_nss;         /**< Value for KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS */
	u32 gpu_reset_ticks_ss;          /**< Value for KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS */
	u32 gpu_reset_ticks_nss;         /**< Value for KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS */
	u32 ctx_timeslice_ns;            /**< Value for KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS */
	u32 cfs_ctx_runtime_init_slices; /**< Value for KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES */
	u32 cfs_ctx_runtime_min_slices;  /**< Value for  KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES */

	/** The initalized-flag is placed at the end, to avoid cache-pollution (we should
	 * only be using this during init/term paths).
	 * @note This is a write-once member, and so no locking is required to read */
	int init_status;
} kbasep_js_device_data;

/**
 * @brief KBase Context Job Scheduling information structure
 *
 * This is a substructure in the kbase_context that encapsulates all the
 * scheduling information.
 */

typedef struct kbasep_js_kctx_info
{
	/**
	 * Runpool substructure. This must only be accessed whilst the Run Pool
	 * mutex ( kbasep_js_device_data::runpool_mutex ) is held.
	 *
	 * In addition, the kbasep_js_device_data::runpool_irq::lock may need to be
	 * held for certain sub-members.
	 *
	 * @note some of the members could be moved into kbasep_js_device_data for
	 * improved d-cache/tlb efficiency.
	 */
	struct
	{
		kbasep_js_policy_ctx_info policy_ctx;   /**< Policy-specific context */
	} runpool;

	/**
	 * Job Scheduler Context information sub-structure. These members are
	 * accessed regardless of whether the context is:
	 * - In the Policy's Run Pool
	 * - In the Policy's Queue
	 * - Not queued nor in the Run Pool.
	 *
	 * You must obtain the jsctx_mutex before accessing any other members of
	 * this substructure.
	 *
	 * You may not access any of these members from IRQ context.
	 */
	struct
	{
		osk_mutex jsctx_mutex;                   /**< Job Scheduler Context lock */

		/** Number of jobs <b>ready to run</b> - does \em not include the jobs waiting in
		 * the dispatcher, and dependency-only jobs. See kbase_jd_context::job_nr
		 * for such jobs*/
		u32 nr_jobs;

		u32 nr_nss_jobs;                        /**< Number of NSS jobs (BASE_JD_REQ_NSS bit set) */

		/**
		 * Waitq that reflects whether the context is not scheduled on the run-pool.
		 * This is clear when is_scheduled is true, and set when is_scheduled
		 * is false.
		 *
		 * This waitq can be waited upon to find out when a context is no
		 * longer in the run-pool, and is used in combination with
		 * kbasep_js_policy_try_evict_ctx() to determine when it can be
		 * terminated. However, it should only be terminated once all its jobs
		 * are also terminated (see kbase_jd_context::zero_jobs_waitq).
		 *
		 * Since the waitq is only set under jsctx_mutex, the waiter should
		 * also briefly obtain and drop jsctx_mutex to guarentee that the
		 * setter has completed its work on the kbase_context.
		 */
		osk_waitq not_scheduled_waitq;

		/* NOTE: Unify the flags together */
		/**
		 * Is the context scheduled on the Run Pool?
		 *
		 * This is only ever updated whilst the jsctx_mutex is held.
		 */
		mali_bool is_scheduled;
		mali_bool is_dying;                     /**< Is the context in the process of being evicted? */
	} ctx;

	/* The initalized-flag is placed at the end, to avoid cache-pollution (we should
	 * only be using this during init/term paths) */
	int init_status;
} kbasep_js_kctx_info;


/**
 * @brief The JS timer resolution, in microseconds
 *
 * Any non-zero difference in time will be at least this size.
 */
#define KBASEP_JS_TICK_RESOLUTION_US (1000000u/osk_time_mstoticks(1000))

/**
 * @note MIDBASE-769: OSK to add high resolution timer
 *
 * The underlying tick is an unsigned integral type
 */
typedef osk_ticks kbasep_js_tick;

/**
 * GPU clock ticks.
 */
typedef osk_ticks kbasep_js_gpu_tick;


#endif /* _KBASE_JS_DEFS_H_ */


/** @} */ /* end group kbase_js */
/** @} */ /* end group base_kbase_api */
/** @} */ /* end group base_api */
