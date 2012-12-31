/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/*
 * Job Scheduler: First Come, First Served Policy Implementation
 */
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_js.h>
#include <kbase/src/common/mali_kbase_js_policy_fcfs.h>

#define LOOKUP_VARIANT_MASK ((1u<<KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS) - 1u)

/* core_req variants are ordered by least restrictive first, so that our
 * algorithm in cached_variant_idx_init picks the least restrictive variant for
 * each job . Note that coherent_group requirement is added to all CS works as the
 * selection of JS does not depend on the coherency requirement. */
static const base_jd_core_req core_req_variants[] ={

	(BASE_JD_REQ_FS | BASE_JD_REQ_CF | BASE_JD_REQ_V | BASE_JD_REQ_PERMON ),
	(BASE_JD_REQ_CS | BASE_JD_REQ_CF | BASE_JD_REQ_V | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_PERMON ),
	(BASE_JD_REQ_CS | BASE_JD_REQ_T  | BASE_JD_REQ_CF | BASE_JD_REQ_V   | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_PERMON ),
	(BASE_JD_REQ_CS | BASE_JD_REQ_CF | BASE_JD_REQ_V  | BASE_JD_REQ_NSS | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_PERMON ),
};

#define NUM_CORE_REQ_VARIANTS NELEMS(core_req_variants)

static const u32 variants_supported_ss_state[] =
{
	(1u << 0),             /* js[0] uses variant 0 (FS list)*/
	(1u << 2) | (1u << 1), /* js[1] uses variants 1 and 2 (CS and CS+T lists)*/
	(1u << 1)              /* js[2] uses variant 1 (CS list). NOTE: could set to 0 */
};

static const u32 variants_supported_nss_state[] =
{
	(1u << 0),             /* js[0] uses variant 0 (FS list)*/
	(1u << 2) | (1u << 1), /* js[1] uses variants 1 and 2 (CS and CS+T lists)*/
	(1u << 3)              /* js[2] uses variant 3 (NSS list) */
};

/* Defines for easy asserts 'is scheduled'/'is queued'/'is neither queued norscheduled' */
#define KBASEP_JS_CHECKFLAG_QUEUED       (1u << 0) /**< Check the queued state */
#define KBASEP_JS_CHECKFLAG_SCHEDULED     (1u << 1) /**< Check the scheduled state */
#define KBASEP_JS_CHECKFLAG_IS_QUEUED    (1u << 2) /**< Expect queued state to be set */
#define KBASEP_JS_CHECKFLAG_IS_SCHEDULED (1u << 3) /**< Expect scheduled state to be set */

enum
{
	KBASEP_JS_CHECK_NOTQUEUED     = KBASEP_JS_CHECKFLAG_QUEUED,
	KBASEP_JS_CHECK_NOTSCHEDULED  = KBASEP_JS_CHECKFLAG_SCHEDULED,
	KBASEP_JS_CHECK_QUEUED        = KBASEP_JS_CHECKFLAG_QUEUED | KBASEP_JS_CHECKFLAG_IS_QUEUED,
	KBASEP_JS_CHECK_SCHEDULED     = KBASEP_JS_CHECKFLAG_SCHEDULED | KBASEP_JS_CHECKFLAG_IS_SCHEDULED
};

typedef u32 kbasep_js_check;

/*
 * Private Functions
 */

STATIC void kbasep_js_debug_check( kbasep_js_policy_fcfs *policy_info, kbase_context *kctx, kbasep_js_check check_flag )
{
	/* This function uses the ternary operator and non-explicit comparisons,
	 * because it makes for much shorter, easier to read code */

	if ( check_flag & KBASEP_JS_CHECKFLAG_QUEUED )
	{
		mali_bool is_queued;
		mali_bool expect_queued;
		is_queued = ( OSK_DLIST_MEMBER_OF( &policy_info->ctx_queue_head,
										   kctx,
										   jctx.sched_info.runpool.policy_ctx.fcfs.list ) )? MALI_TRUE: MALI_FALSE;

		expect_queued = ( check_flag & KBASEP_JS_CHECKFLAG_IS_QUEUED ) ? MALI_TRUE : MALI_FALSE;

		OSK_ASSERT_MSG( expect_queued == is_queued,
						"Expected context %p to be %s but it was %s\n",
						kctx,
						(expect_queued)   ?"queued":"not queued",
						(is_queued)       ?"queued":"not queued" );

	}

	if ( check_flag & KBASEP_JS_CHECKFLAG_SCHEDULED )
	{
		mali_bool is_scheduled;
		mali_bool expect_scheduled;
		is_scheduled = ( OSK_DLIST_MEMBER_OF( &policy_info->scheduled_ctxs_head,
											  kctx,
											  jctx.sched_info.runpool.policy_ctx.fcfs.list ) )? MALI_TRUE: MALI_FALSE;

		expect_scheduled = ( check_flag & KBASEP_JS_CHECKFLAG_IS_SCHEDULED ) ? MALI_TRUE : MALI_FALSE;
		OSK_ASSERT_MSG( expect_scheduled == is_scheduled,
						"Expected context %p to be %s but it was %s\n",
						kctx,
						(expect_scheduled)?"scheduled":"not scheduled",
						(is_scheduled)    ?"scheduled":"not scheduled" );

	}

}

STATIC INLINE void set_slot_to_variant_lookup( u32 *bit_array, u32 slot_idx, u32 variants_supported )
{
	u32 overall_bit_idx = slot_idx * KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS;
	u32 word_idx = overall_bit_idx / 32;
	u32 bit_idx = overall_bit_idx % 32;

	OSK_ASSERT( slot_idx < BASE_JM_MAX_NR_SLOTS );
	OSK_ASSERT( (variants_supported & ~LOOKUP_VARIANT_MASK) == 0 );

	bit_array[word_idx] |= variants_supported << bit_idx;
}


STATIC INLINE u32 get_slot_to_variant_lookup( u32 *bit_array, u32 slot_idx )
{
	u32 overall_bit_idx = slot_idx * KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS;
	u32 word_idx = overall_bit_idx / 32;
	u32 bit_idx = overall_bit_idx % 32;

	u32 res;

	OSK_ASSERT( slot_idx < BASE_JM_MAX_NR_SLOTS );

	res = bit_array[word_idx] >> bit_idx;
	res &= LOOKUP_VARIANT_MASK;

	return res;
}

#if MALI_DEBUG
/* Check the core_req_variants: make sure that every job slot is satisifed by
 * one of the variants. This checks that cached_variant_idx_init will produce a
 * valid result for jobs that make maximum use of the job slots. */
STATIC void debug_check_core_req_variants( kbase_device *kbdev, kbasep_js_policy_fcfs *policy_info )
{
	kbasep_js_device_data *js_devdata;
	int i;
	int j;

	js_devdata = &kbdev->js_data;

	for ( j = 0 ; j < kbdev->nr_job_slots ; ++j )
	{
		base_jd_core_req job_core_req;
		mali_bool found = MALI_FALSE;

		job_core_req =  js_devdata->js_reqs[j];
		for ( i = 0; i < policy_info->num_core_req_variants ; ++i )
		{
			base_jd_core_req var_core_req;
			var_core_req = policy_info->core_req_variants[i];

			if ( (var_core_req & job_core_req) == job_core_req )
			{
				found = MALI_TRUE;
				break;
			}
		}

		/* Early-out on any failure */
		OSK_ASSERT_MSG( found != MALI_FALSE,
						"Job slot %d features 0x%x not matched by core_req_variants. "
						"Rework core_req_variants and vairants_supported_<...>_state[] to match\n",
						j,
						job_core_req );
	}
}
#endif

STATIC void build_core_req_variants( kbase_device *kbdev, kbasep_js_policy_fcfs *policy_info )
{
	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( policy_info != NULL );

	/* Assume a static set of variants */
	OSK_MEMCPY( policy_info->core_req_variants, core_req_variants, sizeof(core_req_variants) );

	policy_info->num_core_req_variants = NUM_CORE_REQ_VARIANTS;

	OSK_DEBUG_CODE( debug_check_core_req_variants( kbdev, policy_info ) );
}


STATIC void build_slot_lookups( kbase_device *kbdev, kbasep_js_policy_fcfs *policy_info )
{
	int i;

	/* Given the static set of variants, provide a static set of lookups */
	for ( i = 0; i < kbdev->nr_job_slots; ++i )
	{
		set_slot_to_variant_lookup( policy_info->slot_to_variant_lookup_ss_state,
									i,
									variants_supported_ss_state[i] );

		set_slot_to_variant_lookup( policy_info->slot_to_variant_lookup_nss_state,
									i,
									variants_supported_nss_state[i] );
	}

}

STATIC mali_error cached_variant_idx_init( kbasep_js_policy_fcfs *policy_info, kbase_jd_atom *atom )
{
	kbasep_js_policy_fcfs_job *job_info;
	u32 i;
	base_jd_core_req job_core_req;

	OSK_ASSERT( policy_info != NULL );
	OSK_ASSERT( atom != NULL );

	job_info = &atom->sched_info.fcfs;
	job_core_req = atom->user_atom->core_req;

	/* Pick a core_req variant that matches us. Since they're ordered by least
	 * restrictive first, it picks the least restrictive variant */
	for ( i = 0; i < policy_info->num_core_req_variants ; ++i )
	{
		base_jd_core_req var_core_req;
		var_core_req = policy_info->core_req_variants[i];
		
		if ( (var_core_req & job_core_req) == job_core_req )
		{
			job_info->cached_variant_idx = i;
			return MALI_ERROR_NONE;
		}
	}

	/* Could not find a matching requirement, this should only be caused by an
	 * attempt to attack the driver. */
	return MALI_ERROR_FUNCTION_FAILED;
}


/*
 * Non-private functions
 */

mali_error kbasep_js_policy_init( kbase_device *kbdev )
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy_fcfs *policy_info;

	OSK_ASSERT( kbdev != NULL );
	js_devdata = &kbdev->js_data;
	policy_info = &js_devdata->policy.fcfs;

	OSK_DLIST_INIT( &policy_info->ctx_queue_head );
	OSK_DLIST_INIT( &policy_info->scheduled_ctxs_head );

	/* Build up the core_req variants */
	build_core_req_variants( kbdev, policy_info );
	/* Build the slot to variant lookups */
	build_slot_lookups(kbdev, policy_info );

	return MALI_ERROR_NONE;
}

void kbasep_js_policy_term( kbasep_js_policy *js_policy )
{
	kbasep_js_policy_fcfs     *policy_info;

	OSK_ASSERT( js_policy != NULL );
	policy_info = &js_policy->fcfs;

	/* ASSERT that there are no contexts queued */
	OSK_ASSERT( OSK_DLIST_IS_EMPTY( &policy_info->ctx_queue_head ) != MALI_FALSE );
	/* ASSERT that there are no contexts scheduled */
	OSK_ASSERT( OSK_DLIST_IS_EMPTY( &policy_info->scheduled_ctxs_head ) != MALI_FALSE );

	/* This policy is simple enough that there's nothing to do */
}

mali_error kbasep_js_policy_init_ctx( kbase_device *kbdev, kbase_context *kctx )
{
	kbasep_js_policy_fcfs_ctx *ctx_info;
	kbasep_js_policy_fcfs     *policy_info;
	int i;

	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( kctx != NULL );

	policy_info = &kbdev->js_data.policy.fcfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.fcfs;

	for ( i = 0 ; i < policy_info->num_core_req_variants ; ++i )
	{
		OSK_DLIST_INIT( &ctx_info->job_list_head[i] );
	}

	return MALI_ERROR_NONE;
}

void kbasep_js_policy_term_ctx( kbasep_js_policy *js_policy, kbase_context *kctx )
{
	kbasep_js_policy_fcfs_ctx *ctx_info;
	kbasep_js_policy_fcfs     *policy_info;
	int i;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( kctx != NULL );

	policy_info = &js_policy->fcfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.fcfs;

	/* ASSERT that no jobs are present */
	for ( i = 0 ; i < policy_info->num_core_req_variants ; ++i )
	{
		OSK_ASSERT( OSK_DLIST_IS_EMPTY( &ctx_info->job_list_head[i] ) != MALI_FALSE );
	}

	/* No work to do */
}


/*
 * Context Management
 */

void kbasep_js_policy_enqueue_ctx( kbasep_js_policy *js_policy, kbase_context *kctx )
{
	kbasep_js_policy_fcfs     *policy_info;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( kctx != NULL );

	policy_info = &js_policy->fcfs;

	/* ASSERT about scheduled-ness/queued-ness */
	kbasep_js_debug_check( policy_info, kctx, KBASEP_JS_CHECK_NOTQUEUED );

	/* All enqueued contexts go to the back of the queue */
	OSK_DLIST_PUSH_BACK( &policy_info->ctx_queue_head,
						 kctx,
						 kbase_context,
						 jctx.sched_info.runpool.policy_ctx.fcfs.list );
}

mali_bool kbasep_js_policy_dequeue_head_ctx( kbasep_js_policy *js_policy, kbase_context **kctx_ptr )
{
	kbasep_js_policy_fcfs     *policy_info;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( kctx_ptr != NULL );

	policy_info = &js_policy->fcfs;

	if ( OSK_DLIST_IS_EMPTY( &policy_info->ctx_queue_head ) != MALI_FALSE )
	{
		/* Nothing to dequeue */
		return MALI_FALSE;
	}

	/* Contexts are dequeued from the front of the queue */
	*kctx_ptr = OSK_DLIST_POP_FRONT( &policy_info->ctx_queue_head,
									 kbase_context,
									 jctx.sched_info.runpool.policy_ctx.fcfs.list );


	return MALI_TRUE;
}

mali_bool kbasep_js_policy_try_evict_ctx( kbasep_js_policy *js_policy, kbase_context *kctx )
{
	kbasep_js_policy_fcfs     *policy_info;
	mali_bool is_present;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( kctx != NULL );

	policy_info = &js_policy->fcfs;

	/* Check the queue to see if it's found */
	is_present = OSK_DLIST_MEMBER_OF( &policy_info->ctx_queue_head,
									   kctx,
									  jctx.sched_info.runpool.policy_ctx.fcfs.list );

	if ( is_present != MALI_FALSE )
	{
		OSK_DLIST_REMOVE( &policy_info->ctx_queue_head,
						  kctx,
						  jctx.sched_info.runpool.policy_ctx.fcfs.list );
	}

	return is_present;
}

void kbasep_js_policy_kill_all_ctx_jobs( kbasep_js_policy *js_policy, kbase_context *kctx )
{
	kbasep_js_policy_fcfs *policy_info;
	kbasep_js_policy_fcfs_ctx *ctx_info;
	u32 i;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( kctx != NULL );

	policy_info = &js_policy->fcfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.fcfs;

	/* Kill jobs on each variant in turn */
	for ( i = 0; i < policy_info->num_core_req_variants; ++i )
	{
		osk_dlist *job_list;
		job_list = &ctx_info->job_list_head[i];

		/* Call kbase_jd_cancel() on all kbase_jd_atoms in this list, whilst removing them from the list */
		OSK_DLIST_EMPTY_LIST( job_list, kbase_jd_atom, sched_info.fcfs.list, kbase_jd_cancel );
	}

}

void kbasep_js_policy_runpool_add_ctx( kbasep_js_policy *js_policy, kbase_context *kctx )
{
	kbasep_js_policy_fcfs     *policy_info;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( kctx != NULL );

	policy_info = &js_policy->fcfs;

	/* ASSERT about scheduled-ness/queued-ness */
	kbasep_js_debug_check( policy_info, kctx, KBASEP_JS_CHECK_NOTSCHEDULED );

	/* All enqueued contexts go to the back of the runpool */
	OSK_DLIST_PUSH_BACK( &policy_info->scheduled_ctxs_head,
						 kctx,
						 kbase_context,
						 jctx.sched_info.runpool.policy_ctx.fcfs.list );

}

void kbasep_js_policy_runpool_remove_ctx( kbasep_js_policy *js_policy, kbase_context *kctx )
{
	kbasep_js_policy_fcfs     *policy_info;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( kctx != NULL );

	policy_info = &js_policy->fcfs;

	/* ASSERT about scheduled-ness/queued-ness */
	kbasep_js_debug_check( policy_info, kctx, KBASEP_JS_CHECK_SCHEDULED );

	/* No searching or significant list maintenance required to remove this context */
	OSK_DLIST_REMOVE( &policy_info->scheduled_ctxs_head,
					  kctx,
					  jctx.sched_info.runpool.policy_ctx.fcfs.list );
}

mali_bool kbasep_js_policy_should_remove_ctx( kbasep_js_policy *js_policy, kbase_context *kctx )
{
	/* Under this policy, all contexts run until they're finished.
	 * Modifying this will change it from FCFS to some form of round-robin */
	return MALI_FALSE;
}


/*
 * Job Chain Management
 */

mali_error kbasep_js_policy_init_job( kbasep_js_policy *js_policy, kbase_jd_atom *atom )
{
 	kbasep_js_policy_fcfs *policy_info;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( atom != NULL );

	policy_info = &js_policy->fcfs;

	/* Determine the job's index into the job list head, will return error if the
	 * atom is malformed and so is reported. */
	return cached_variant_idx_init( policy_info, atom );
}

void kbasep_js_policy_term_job( kbasep_js_policy *js_policy, kbase_jd_atom *atom )
{
	kbasep_js_policy_fcfs_job *job_info;
	kbasep_js_policy_fcfs_ctx *ctx_info;
	kbase_context *parent_ctx;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( atom != NULL );
	parent_ctx = atom->kctx;
	OSK_ASSERT( parent_ctx != NULL );

	job_info = &atom->sched_info.fcfs;
	ctx_info = &parent_ctx->jctx.sched_info.runpool.policy_ctx.fcfs;

	/* This policy is simple enough that nothing is required */

	/* In any case, we'll ASSERT that this job was correctly removed from the relevant lists */
	OSK_ASSERT( OSK_DLIST_MEMBER_OF( &ctx_info->job_list_head[job_info->cached_variant_idx],
									 atom,
									 sched_info.fcfs.list ) == MALI_FALSE );
}


mali_bool kbasep_js_policy_dequeue_job( kbase_device *kbdev,
										int job_slot_idx,
										kbase_jd_atom **katom_ptr )
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy_fcfs *policy_info;
	kbase_context *kctx;
	u32 variants_supported;

	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( katom_ptr != NULL );
	OSK_ASSERT( job_slot_idx < BASE_JM_MAX_NR_SLOTS );

	js_devdata = &kbdev->js_data;
	policy_info = &js_devdata->policy.fcfs;

	/* Get the variants for this slot */
	if ( js_devdata->runpool_irq.nr_nss_ctxs_running == 0 )
	{
		/* SS-state */
		variants_supported = get_slot_to_variant_lookup( policy_info->slot_to_variant_lookup_ss_state, job_slot_idx );
	}
	else
	{
		/* NSS-state */
		variants_supported = get_slot_to_variant_lookup( policy_info->slot_to_variant_lookup_nss_state, job_slot_idx );
	}

	/* Choose the first context that has a job ready matching the requirements */
	OSK_DLIST_FOREACH( &policy_info->scheduled_ctxs_head,
					   kbase_context,
					   jctx.sched_info.runpool.policy_ctx.fcfs.list,
					   kctx )
	{
		u32 test_variants = variants_supported;
		kbasep_js_policy_fcfs_ctx *ctx_info;
		ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.fcfs;

		/* Only submit jobs from contexts that are allowed*/
		if ( kbasep_js_is_submit_allowed( js_devdata, kctx ) != MALI_FALSE )
		{
			/* Check each variant in turn */
			while ( test_variants != 0 )
			{
				long variant_idx;
				osk_dlist *job_list;
				variant_idx = osk_find_first_set_bit( test_variants );
				job_list = &ctx_info->job_list_head[variant_idx];

				if ( OSK_DLIST_IS_EMPTY( job_list ) == MALI_FALSE )
				{
					/* Found a context with a matching job */
					*katom_ptr = OSK_DLIST_POP_FRONT( job_list, kbase_jd_atom, sched_info.fcfs.list );

					return MALI_TRUE;
				}

				test_variants &= ~(1u << variant_idx);
			}
			/* All variants checked by here */
		}

	}

	/* By this point, no contexts had a matching job */

	return MALI_FALSE;
}

mali_bool kbasep_js_policy_dequeue_job_irq( kbase_device *kbdev,
											int job_slot_idx,
											kbase_jd_atom **katom_ptr )
{
	/* IRQ and non-IRQ variants of this are the same (though, the IRQ variant could be made faster) */
	return kbasep_js_policy_dequeue_job( kbdev, job_slot_idx, katom_ptr );
}


void kbasep_js_policy_enqueue_job( kbasep_js_policy *js_policy, kbase_jd_atom *katom )
{
	kbasep_js_policy_fcfs_job *job_info;
	kbasep_js_policy_fcfs_ctx *ctx_info;
	kbase_context *parent_ctx;

	OSK_ASSERT( js_policy != NULL );
	OSK_ASSERT( katom != NULL );
	parent_ctx = katom->kctx;
	OSK_ASSERT( parent_ctx != NULL );

	job_info = &katom->sched_info.fcfs;
	ctx_info = &parent_ctx->jctx.sched_info.runpool.policy_ctx.fcfs;

	OSK_DLIST_PUSH_BACK( &ctx_info->job_list_head[job_info->cached_variant_idx],
						 katom,
						 kbase_jd_atom,
						 sched_info.fcfs.list );
}

void kbasep_js_policy_log_job_result( kbasep_js_policy *js_policy, kbase_jd_atom *katom, u32 time_spent_us )
{
	/* We ignore the job result */
	return;
}

mali_bool kbasep_js_policy_ctx_has_priority( kbasep_js_policy *js_policy, kbase_context *current_ctx, kbase_context *new_ctx )
{
	/* A new context never has priority over the current one */
	return MALI_FALSE;
}

