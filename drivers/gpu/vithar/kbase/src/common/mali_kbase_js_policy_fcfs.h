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
 * @file mali_kbase_js_policy_fcfs.h
 * First Come First Served Job Scheduler Policy structure definitions
 */

#ifndef _KBASE_JS_POLICY_FCFS_H_
#define _KBASE_JS_POLICY_FCFS_H_

#define KBASE_JS_POLICY_AVAILABLE_FCFS

/** @addtogroup base_api
 * @{ */
/** @addtogroup base_kbase_api
 * @{ */
/** @addtogroup kbase_js_policy
 * @{ */

/**
 * Internally, this policy keeps a few internal queues for different variants
 * of core requirements, which are used to decide how to schedule onto the
 * different job slots.
 *
 * Currently, one extra variant is supported: an NSS variant.
 *
 * Must be a power of 2 to keep the lookup math simple
 */
#define KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS_LOG2 2
#define KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS      (1u << KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS_LOG2 )

/** Bits needed in the lookup to support all slots */
#define KBASEP_JS_VARIANT_LOOKUP_BITS_NEEDED (BASE_JM_MAX_NR_SLOTS * KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS)
/** Number of u32s needed in the lookup array to support all slots */
#define KBASEP_JS_VARIANT_LOOKUP_WORDS_NEEDED ((KBASEP_JS_VARIANT_LOOKUP_BITS_NEEDED + 31) / 32)

typedef struct kbasep_js_policy_fcfs
{
	/** List of all contexts in the context queue. Hold
	 * kbasep_js_device_data::queue_mutex whilst accessing. */
	osk_dlist ctx_queue_head;

	/** List of scheduled contexts. Hold kbasep_jd_device_data::runpool_irq::lock
	 * whilst accessing, which is a spinlock */
	osk_dlist scheduled_ctxs_head;

	/** Number of valid elements in the core_req_variants member, and the
	 * kbasep_js_policy_fcfs_ctx::job_list_head array */
	u32 num_core_req_variants;

	/** Variants of the core requirements */
	base_jd_core_req core_req_variants[KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS];

	/* Lookups per job slot against which core_req_variants match it */
	u32 slot_to_variant_lookup_ss_state[KBASEP_JS_VARIANT_LOOKUP_WORDS_NEEDED];
	u32 slot_to_variant_lookup_nss_state[KBASEP_JS_VARIANT_LOOKUP_WORDS_NEEDED];

} kbasep_js_policy_fcfs;


/**
 * @addtogroup kbase_js_policy_ctx
 * @{
 */

/**
 * This policy contains a single linked list of all contexts. The list is unprioritized.
 */
typedef struct kbasep_js_policy_fcfs_ctx
{
	/** Link implementing the Policy's Queue, and Currently Scheduled list */
	osk_dlist_item list;

	/** Job lists for use when in the Run Pool - only using
	 * kbasep_js_policy_fcfs::num_unique_slots of them. We still need to track
	 * the jobs when we're not in the runpool, so this member is accessed from
	 * outside the policy queue (for the first job), inside the policy queue,
	 * and inside the runpool.
	 *
	 * If the context is in the runpool, then this must only be accessed with
	 * kbasep_js_device_data::runpool_irq::lock held
	 *
	 * Jobs are still added to this list even when the context is not in the
	 * runpool. In that case, the kbasep_js_kctx_info::ctx::jsctx_mutex must be
	 * held before accessing this. */
	osk_dlist job_list_head[KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS];

} kbasep_js_policy_fcfs_ctx;

/** @} */ /* end group kbase_js_policy_ctx */



/**
 * @addtogroup kbase_js_policy_job
 * @{
 */


/**
 * In this policy, each Job is part of at most one of the per_corereq lists
 */
typedef struct kbasep_js_policy_fcfs_job
{
	osk_dlist_item list;      /**< Link implementing the Run Pool list/Jobs owned by the ctx */
	u32 cached_variant_idx;   /**< Cached index of the list this should be entered into on re-queue */
} kbasep_js_policy_fcfs_job;

/** @} */ /* end group kbase_js_policy_job */

/** @} */ /* end group kbase_js_policy */
/** @} */ /* end group base_kbase_api */
/** @} */ /* end group base_api */


#endif /* _KBASE_JS_POLICY_FCFS_H_ */
