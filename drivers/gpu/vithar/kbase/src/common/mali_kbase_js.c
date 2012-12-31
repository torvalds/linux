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
 * Job Scheduler Implementation
 */
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_js.h>
#include <kbase/src/common/mali_kbase_js_affinity.h>

#include "mali_kbase_jm.h"
#include <kbase/src/common/mali_kbase_defs.h>

/*
 * Private function prototypes
 */

STATIC INLINE void kbasep_js_check_and_ref_nss_running_ctx( kbasep_js_device_data *js_devdata, kbase_context *kctx );
STATIC INLINE mali_bool kbasep_js_check_and_deref_nss_running_ctx( kbasep_js_device_data *js_devdata, kbase_context *kctx );

STATIC void kbasep_js_check_and_ref_nss_job( kbasep_js_device_data *js_devdata,
											 kbase_context *kctx,
											 kbase_jd_atom *atom );
STATIC mali_bool kbasep_js_check_and_deref_nss_job( kbasep_js_device_data *js_devdata,
											   kbase_context *kctx,
											   kbase_jd_atom *atom );

STATIC INLINE void kbasep_js_deref_permon_check_and_disable_cycle_counter( kbase_device *kbdev,
											kbase_jd_atom * katom );

STATIC INLINE void kbasep_js_ref_permon_check_and_enable_cycle_counter( kbase_device *kbdev,
											kbase_jd_atom * katom );

/** Helper for trace subcodes */
#if KBASE_TRACE_ENABLE != 0
STATIC int kbasep_js_trace_get_refcnt( kbase_device *kbdev, kbase_context *kctx )
{
	kbasep_js_device_data *js_devdata;
	int as_nr;
	int refcnt = 0;

	js_devdata = &kbdev->js_data;

	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
	as_nr = kctx->as_nr;
	if ( as_nr != KBASEP_AS_NR_INVALID )
	{
		kbasep_js_per_as_data *js_per_as_data;
		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		refcnt = js_per_as_data->as_busy_refcount;
	}
	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );

	return refcnt;
}
#else /* KBASE_TRACE_ENABLE != 0 */
STATIC int kbasep_js_trace_get_refcnt( kbase_device *kbdev, kbase_context *kctx )
{
	CSTD_UNUSED( kbdev );
	CSTD_UNUSED( kctx );
	return 0;
}
#endif /* KBASE_TRACE_ENABLE != 0 */



/*
 * Private types
 */
enum
{
	JS_DEVDATA_INIT_NONE            =0,
	JS_DEVDATA_INIT_CONSTANTS       =(1 << 0),
	JS_DEVDATA_INIT_RUNPOOL_MUTEX   =(1 << 1),
	JS_DEVDATA_INIT_QUEUE_MUTEX     =(1 << 2),
	JS_DEVDATA_INIT_RUNPOOL_IRQ_LOCK=(1 << 3),
	JS_DEVDATA_INIT_POLICY          =(1 << 4),
	JS_DEVDATA_INIT_ALL             =((1 << 5)-1)
};

enum
{
	JS_KCTX_INIT_NONE        =0,
	JS_KCTX_INIT_CONSTANTS   =(1 << 0),
	JS_KCTX_INIT_JSCTX_MUTEX =(1 << 1),
	JS_KCTX_INIT_POLICY      =(1 << 2),
	JS_KCTX_INIT_JSCTX_WAITQ =(1 << 3),
	JS_KCTX_INIT_ALL         =((1 << 4)-1)
};

/*
 * Private functions
 */

/**
 * Check if the job had performance monitoring enabled and decrement the count.  If no jobs require
 * performance monitoring, then the cycle counters will be disabled in the GPU.
 *
 * Caller must hold the runpool_irq lock (a spinlock)
 */

STATIC INLINE void kbasep_js_deref_permon_check_and_disable_cycle_counter( kbase_device *kbdev, kbase_jd_atom * katom )
{
	kbasep_js_device_data *js_devdata;

	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( katom != NULL );

	js_devdata = &kbdev->js_data;

	if ( katom->core_req & BASE_JD_REQ_PERMON )
	{
		OSK_ASSERT( js_devdata->runpool_irq.nr_permon_jobs_submitted > 0 );

		--kbdev->js_data.runpool_irq.nr_permon_jobs_submitted;

		if ( 0 == js_devdata->runpool_irq.nr_permon_jobs_submitted )
		{
#if BASE_HW_ISSUE_6367 == 0 /* Workaround for issue 6367 requires cycle counter to remain on */
			kbase_reg_write( kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CYCLE_COUNT_STOP, NULL );
#endif
		}
	}
}

/**
 * Check if the job has performance monitoring enabled and keep a count of it.  If at least one
 * job requires performance monitoring, then the cycle counters will be enabled in the GPU.
 *
 * Caller must hold the runpool_irq lock (a spinlock)
 */

STATIC INLINE void kbasep_js_ref_permon_check_and_enable_cycle_counter( kbase_device *kbdev, kbase_jd_atom * katom )
{
	kbasep_js_device_data *js_devdata;

	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( katom != NULL );

	js_devdata = &kbdev->js_data;

	if ( katom->core_req & BASE_JD_REQ_PERMON )
	{
		OSK_ASSERT( js_devdata->runpool_irq.nr_permon_jobs_submitted < S8_MAX );

		++js_devdata->runpool_irq.nr_permon_jobs_submitted;

		if ( 1 == js_devdata->runpool_irq.nr_permon_jobs_submitted )
		{
			kbase_reg_write( kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CYCLE_COUNT_START, NULL );
		}
	}
}


/**
 * Caller must hold the runpool_irq lock (a spinlock)
 */
STATIC INLINE void kbasep_js_check_and_ref_nss_running_ctx( kbasep_js_device_data *js_devdata, kbase_context *kctx )
{
	kbasep_js_kctx_info *js_kctx_info;

	OSK_ASSERT( kctx != NULL );
	js_kctx_info = &kctx->jctx.sched_info;

	OSK_ASSERT( js_kctx_info->ctx.is_scheduled != MALI_FALSE );

	if ( js_kctx_info->ctx.nr_nss_jobs > 0 )
	{
		OSK_ASSERT( js_devdata->runpool_irq.nr_nss_ctxs_running < S8_MAX );
		++(js_devdata->runpool_irq.nr_nss_ctxs_running);

		if ( js_devdata->runpool_irq.nr_nss_ctxs_running == 1 )
		{
			OSK_PRINT_INFO(OSK_BASE_JM, "JS: First NSS Context %p scheduled (switched to NSS state)", kctx );
		}
	}
}

/**
 * Caller must hold the runpool_irq lock (a spinlock)
 */
STATIC INLINE mali_bool kbasep_js_check_and_deref_nss_running_ctx( kbasep_js_device_data *js_devdata, kbase_context *kctx )
{
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool nss_state_changed = MALI_FALSE;

	OSK_ASSERT( kctx != NULL );
	js_kctx_info = &kctx->jctx.sched_info;

	OSK_ASSERT( js_kctx_info->ctx.is_scheduled != MALI_FALSE );

	if ( js_kctx_info->ctx.nr_nss_jobs > 0 )
	{
		OSK_ASSERT( js_devdata->runpool_irq.nr_nss_ctxs_running > 0 );
		--(js_devdata->runpool_irq.nr_nss_ctxs_running);

		if ( js_devdata->runpool_irq.nr_nss_ctxs_running == 0 )
		{
			OSK_PRINT_INFO(OSK_BASE_JM, "JS: Last NSS Context %p descheduled (switched to SS state)", kctx );
			nss_state_changed = MALI_TRUE;
		}
	}

	return nss_state_changed;
}

/**
 * When the context is scheduled, the caller must hold the runpool_irq lock (a spinlock).
 */
STATIC void kbasep_js_check_and_ref_nss_job( kbasep_js_device_data *js_devdata,
											 kbase_context *kctx,
											 kbase_jd_atom *atom )
{
	kbasep_js_kctx_info *js_kctx_info;

	OSK_ASSERT( kctx != NULL );
	js_kctx_info = &kctx->jctx.sched_info;

	if ( atom->core_req & BASE_JD_REQ_NSS )
	{
		OSK_ASSERT( js_kctx_info->ctx.nr_nss_jobs < U32_MAX );
		++(js_kctx_info->ctx.nr_nss_jobs);

		if ( js_kctx_info->ctx.is_scheduled != MALI_FALSE
			 && js_kctx_info->ctx.nr_nss_jobs == 1 )
		{
			/* Only NSS ref-count a running ctx on the first nss job */
			kbasep_js_check_and_ref_nss_running_ctx( js_devdata, kctx );
		}
	}
}


/**
 * When the context is scheduled, the caller must hold the runpool_irq lock (a spinlock).
 */
STATIC mali_bool kbasep_js_check_and_deref_nss_job( kbasep_js_device_data *js_devdata,
											   kbase_context *kctx,
											   kbase_jd_atom *atom )
{
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool nss_state_changed = MALI_FALSE;

	OSK_ASSERT( kctx != NULL );
	js_kctx_info = &kctx->jctx.sched_info;

	if ( atom->core_req & BASE_JD_REQ_NSS )
	{
		OSK_ASSERT( js_kctx_info->ctx.nr_nss_jobs > 0 );

		if ( js_kctx_info->ctx.is_scheduled != MALI_FALSE
			 && js_kctx_info->ctx.nr_nss_jobs == 1 )
		{
			/* Only NSS deref-count a running ctx on the last nss job */
			nss_state_changed = kbasep_js_check_and_deref_nss_running_ctx( js_devdata, kctx );
		}

		--(js_kctx_info->ctx.nr_nss_jobs);
	}

	return nss_state_changed;
}



STATIC base_jd_core_req core_reqs_from_jsn_features( kbasep_jsn_feature features )
{
	base_jd_core_req core_req = 0u;

	if ( (features & KBASE_JSn_FEATURE_SET_VALUE_JOB) != 0 )
	{
		core_req |= BASE_JD_REQ_V;
	}
	if ( (features & KBASE_JSn_FEATURE_CACHE_FLUSH_JOB) != 0 )
	{
		core_req |= BASE_JD_REQ_CF;
	}
	if ( (features & KBASE_JSn_FEATURE_COMPUTE_JOB) != 0 )
	{
		core_req |= BASE_JD_REQ_CS;
	}
	if ( (features & KBASE_JSn_FEATURE_TILER_JOB) != 0 )
	{
		core_req |= BASE_JD_REQ_T;
	}
	if ( (features & KBASE_JSn_FEATURE_FRAGMENT_JOB) != 0 )
	{
		core_req |= BASE_JD_REQ_FS;
	}
	return core_req;
}

/**
 * Picks a free address space and add the context to the Policy. Then perform a
 * transaction on this AS and RunPool IRQ to:
 * - setup the runpool_irq structure and the context on that AS
 * - Activate the MMU on that AS
 * - Allow jobs to be submitted on that AS
 *
 * Locking conditions:
 * - Caller must hold the kbasep_js_kctx_info::jsctx_mutex
 * - Caller must hold the kbase_js_device_data::runpool_mutex
 * - AS transaction mutex will be obtained
 * - Runpool IRQ lock will be obtained
 */
STATIC void assign_and_activate_kctx_addr_space( kbase_device *kbdev, kbase_context *kctx )
{
	kbasep_js_device_data *js_devdata;
	kbase_as *current_as;
	kbasep_js_per_as_data *js_per_as_data;
	long ffs_result;

	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( kctx != NULL );

	js_devdata = &kbdev->js_data;

	/* Find the free address space */
	ffs_result = osk_find_first_set_bit( js_devdata->as_free );
	/* ASSERT that we should've found a free one */
	OSK_ASSERT( 0 <= ffs_result && ffs_result < kbdev->nr_address_spaces );
	js_devdata->as_free &= ~((u16)(1u << ffs_result));

	/*
	 * Transaction on the AS and runpool_irq
	 */
	current_as = &kbdev->as[ffs_result];
	js_per_as_data = &js_devdata->runpool_irq.per_as_data[ffs_result];
	osk_mutex_lock( &current_as->transaction_mutex );
	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );

	/* NSS Handling */
	kbasep_js_check_and_ref_nss_running_ctx( js_devdata, kctx );

	/* Assign addr space */
	kctx->as_nr = (int)ffs_result;

	/* Activate this address space on the MMU */
	kbase_mmu_update( kctx );

	/* Allow it to run jobs */
	kbasep_js_set_submit_allowed( js_devdata, kctx );

	/* Book-keeping */
	js_per_as_data->kctx = kctx;
	js_per_as_data->as_busy_refcount = 0;

	/* Lastly, add the context to the policy's runpool - this really allows it to run jobs */
	kbasep_js_policy_runpool_add_ctx( &js_devdata->policy, kctx );
	/*
	 * Transaction complete
	 */
	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );
	osk_mutex_unlock( &current_as->transaction_mutex );

}

void kbasep_js_try_run_next_job( kbase_device *kbdev )
{
	int js;

	OSK_ASSERT( kbdev != NULL );

	for ( js = 0; js < kbdev->nr_job_slots ; ++js )
	{
		kbasep_js_try_run_next_job_on_slot( kbdev, js );
	}
}

/** Hold the kbasep_js_device_data::runpool_irq::lock for this */
mali_bool kbasep_js_runpool_retain_ctx_nolock( kbase_device *kbdev, kbase_context *kctx )
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_per_as_data *js_per_as_data;
	mali_bool result = MALI_FALSE;
	int as_nr;
	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( kctx != NULL );
	js_devdata = &kbdev->js_data;

	as_nr = kctx->as_nr;
	if ( as_nr != KBASEP_AS_NR_INVALID )
	{
		int new_refcnt;

		OSK_ASSERT( as_nr >= 0 );
		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		OSK_ASSERT( js_per_as_data->kctx != NULL );

		new_refcnt = ++(js_per_as_data->as_busy_refcount);
		KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_RETAIN_CTX_NOLOCK, kctx, NULL, 0u,
								  new_refcnt );
		result = MALI_TRUE;
	}

	return result;
}

/*
 * Functions private to KBase ('Protected' functions)
 */
void kbase_js_try_run_jobs( kbase_device *kbdev )
{
	kbasep_js_device_data *js_devdata;

	OSK_ASSERT( kbdev != NULL );
	js_devdata = &kbdev->js_data;

	osk_mutex_lock( &js_devdata->runpool_mutex );

	kbasep_js_try_run_next_job( kbdev );

	osk_mutex_unlock( &js_devdata->runpool_mutex );
}

mali_error kbasep_js_devdata_init( kbase_device *kbdev )
{
	kbasep_js_device_data *js_devdata;
	mali_error err;
	int i;
	u16 as_present;
	osk_error osk_err;

	OSK_ASSERT( kbdev != NULL );

	js_devdata = &kbdev->js_data;
	as_present = (1U << kbdev->nr_address_spaces) - 1;

	OSK_ASSERT( js_devdata->init_status == JS_DEVDATA_INIT_NONE );

	js_devdata->nr_contexts_running = 0;
	js_devdata->as_free = as_present; /* All ASs initially free */
	js_devdata->runpool_irq.nr_nss_ctxs_running = 0;
	js_devdata->runpool_irq.nr_permon_jobs_submitted = 0;
	js_devdata->runpool_irq.submit_allowed = 0u; /* No ctx allowed to submit */

	/* Config attributes */
	js_devdata->scheduling_tick_ns = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS );
	js_devdata->soft_stop_ticks = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS );
	js_devdata->hard_stop_ticks_ss = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS );
	js_devdata->hard_stop_ticks_nss = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS );
	js_devdata->gpu_reset_ticks_ss = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS );
	js_devdata->gpu_reset_ticks_nss = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS );
	js_devdata->ctx_timeslice_ns = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS );
	js_devdata->cfs_ctx_runtime_init_slices = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES );
	js_devdata->cfs_ctx_runtime_min_slices = (u32)kbasep_get_config_value( kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES );

	OSK_PRINT_INFO( OSK_BASE_JM, "JS Config Attribs: " );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->scheduling_tick_ns:%d", js_devdata->scheduling_tick_ns );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->soft_stop_ticks:%d", js_devdata->soft_stop_ticks );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->hard_stop_ticks_ss:%d", js_devdata->hard_stop_ticks_ss );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->hard_stop_ticks_nss:%d", js_devdata->hard_stop_ticks_nss );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->gpu_reset_ticks_ss:%d", js_devdata->gpu_reset_ticks_ss );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->gpu_reset_ticks_nss:%d", js_devdata->gpu_reset_ticks_nss );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->ctx_timeslice_ns:%d", js_devdata->ctx_timeslice_ns );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->cfs_ctx_runtime_init_slices:%d", js_devdata->cfs_ctx_runtime_init_slices );
	OSK_PRINT_INFO( OSK_BASE_JM, "\tjs_devdata->cfs_ctx_runtime_min_slices:%d", js_devdata->cfs_ctx_runtime_min_slices );

#if MALI_BACKEND_KERNEL /* Only output on real kernel modules, otherwise it fills up multictx testing output */
#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS != 0
	OSK_PRINT( OSK_BASE_JM,
			   "Job Scheduling Policy Soft-stops disabled, ignoring value for soft_stop_ticks==%d at %dns per tick. Other soft-stops may still occur.",
			   js_devdata->soft_stop_ticks,
			   js_devdata->scheduling_tick_ns );
#endif
#if KBASE_DISABLE_SCHEDULING_HARD_STOPS != 0
	OSK_PRINT( OSK_BASE_JM,
			   "Job Scheduling Policy Hard-stops disabled, ignoring values for hard_stop_ticks_ss==%d and hard_stop_ticks_nss==%d at %dns per tick. Other hard-stops may still occur.",
			   js_devdata->hard_stop_ticks_ss,
			   js_devdata->hard_stop_ticks_nss,
			   js_devdata->scheduling_tick_ns );
#endif
#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS != 0 && KBASE_DISABLE_SCHEDULING_HARD_STOPS != 0
	OSK_PRINT( OSK_BASE_JM, "Note: The JS policy's tick timer (if coded) will still be run, but do nothing." );
#endif
#endif /* MALI_BACKEND_KERNEL */

	/* setup the number of irq throttle cycles base on given time */
	{
		u32 irq_throttle_time_us = kbdev->gpu_props.irq_throttle_time_us;
		u32 irq_throttle_cycles = kbasep_js_convert_us_to_gpu_ticks_max_freq(kbdev, irq_throttle_time_us);
		osk_atomic_set( &kbdev->irq_throttle_cycles, irq_throttle_cycles);
	}

	/* Clear the AS data, including setting NULL pointers */
	OSK_MEMSET( &js_devdata->runpool_irq.per_as_data[0], 0, sizeof(js_devdata->runpool_irq.per_as_data) );

	for ( i = 0; i < kbdev->nr_job_slots; ++i )
	{
		js_devdata->js_reqs[i] = core_reqs_from_jsn_features( kbdev->job_slot_features[i] );
	}
	js_devdata->init_status |= JS_DEVDATA_INIT_CONSTANTS;

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above */

	osk_err = osk_mutex_init( &js_devdata->runpool_mutex, OSK_LOCK_ORDER_JS_RUNPOOL );
	if ( osk_err == OSK_ERR_NONE )
	{
		js_devdata->init_status |= JS_DEVDATA_INIT_RUNPOOL_MUTEX;
	}

	osk_err = osk_mutex_init( &js_devdata->queue_mutex, OSK_LOCK_ORDER_JS_QUEUE );
	if ( osk_err == OSK_ERR_NONE )
	{
		js_devdata->init_status |= JS_DEVDATA_INIT_QUEUE_MUTEX;
	}

	osk_err = osk_spinlock_irq_init( &js_devdata->runpool_irq.lock, OSK_LOCK_ORDER_JS_RUNPOOL_IRQ );
	if ( osk_err == OSK_ERR_NONE )
	{
		js_devdata->init_status |= JS_DEVDATA_INIT_RUNPOOL_IRQ_LOCK;
	}

	err = kbasep_js_policy_init( kbdev );
	if ( err == MALI_ERROR_NONE)
	{
		js_devdata->init_status |= JS_DEVDATA_INIT_POLICY;
	}

	/* On error, do no cleanup; this will be handled by the caller(s), since
	 * we've designed this resource to be safe to terminate on init-fail */
	if ( js_devdata->init_status != JS_DEVDATA_INIT_ALL)
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	return MALI_ERROR_NONE;
}

void kbasep_js_devdata_halt( kbase_device *kbdev )
{
	CSTD_UNUSED(kbdev);
}

void kbasep_js_devdata_term( kbase_device *kbdev )
{
	kbasep_js_device_data *js_devdata;

	OSK_ASSERT( kbdev != NULL );

	js_devdata = &kbdev->js_data;

	if ( (js_devdata->init_status & JS_DEVDATA_INIT_CONSTANTS) )
	{
		/* The caller must de-register all contexts before calling this */
		OSK_ASSERT( js_devdata->nr_contexts_running == 0 );
		OSK_ASSERT( js_devdata->runpool_irq.nr_nss_ctxs_running == 0 );
	}
	if ( (js_devdata->init_status & JS_DEVDATA_INIT_POLICY) )
	{
		kbasep_js_policy_term( &js_devdata->policy );
	}
	if ( (js_devdata->init_status & JS_DEVDATA_INIT_RUNPOOL_IRQ_LOCK) )
	{
		osk_spinlock_irq_term( &js_devdata->runpool_irq.lock );
	}
	if ( (js_devdata->init_status & JS_DEVDATA_INIT_QUEUE_MUTEX) )
	{
		osk_mutex_term( &js_devdata->queue_mutex );
	}
	if ( (js_devdata->init_status & JS_DEVDATA_INIT_RUNPOOL_MUTEX) )
	{
		osk_mutex_term( &js_devdata->runpool_mutex );
	}

	js_devdata->init_status = JS_DEVDATA_INIT_NONE;
}


mali_error kbasep_js_kctx_init( kbase_context *kctx )
{
	kbase_device *kbdev;
	kbasep_js_kctx_info *js_kctx_info;
	mali_error err;
	osk_error osk_err;

	OSK_ASSERT( kctx != NULL );

	kbdev = kctx->kbdev;
	OSK_ASSERT( kbdev != NULL );

	js_kctx_info = &kctx->jctx.sched_info;
	OSK_ASSERT( js_kctx_info->init_status == JS_KCTX_INIT_NONE );

	js_kctx_info->ctx.nr_jobs = 0;
	js_kctx_info->ctx.nr_nss_jobs = 0;

	js_kctx_info->ctx.is_scheduled = MALI_FALSE;
	js_kctx_info->ctx.is_dying = MALI_FALSE;

	js_kctx_info->init_status |= JS_KCTX_INIT_CONSTANTS;

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above */
	osk_err = osk_mutex_init( &js_kctx_info->ctx.jsctx_mutex, OSK_LOCK_ORDER_JS_CTX );
	if ( osk_err == OSK_ERR_NONE )
	{
		js_kctx_info->init_status |= JS_KCTX_INIT_JSCTX_MUTEX;
	}

	osk_err = osk_waitq_init( &js_kctx_info->ctx.not_scheduled_waitq );
	if ( osk_err == OSK_ERR_NONE )
	{
		js_kctx_info->init_status |= JS_KCTX_INIT_JSCTX_WAITQ;
	}

	err = kbasep_js_policy_init_ctx( kbdev, kctx );
	if ( err == MALI_ERROR_NONE )
	{
		js_kctx_info->init_status |= JS_KCTX_INIT_POLICY;
	}

	/* On error, do no cleanup; this will be handled by the caller(s), since
	 * we've designed this resource to be safe to terminate on init-fail */
	if ( js_kctx_info->init_status != JS_KCTX_INIT_ALL)
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	/* Initially, the context is not scheduled */
	osk_waitq_set( &js_kctx_info->ctx.not_scheduled_waitq );

	return MALI_ERROR_NONE;
}

void kbasep_js_kctx_term( kbase_context *kctx )
{
	kbase_device *kbdev;
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_policy    *js_policy;

	OSK_ASSERT( kctx != NULL );

	kbdev = kctx->kbdev;
	OSK_ASSERT( kbdev != NULL );

	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	if ( (js_kctx_info->init_status & JS_KCTX_INIT_CONSTANTS) )
	{
		/* The caller must de-register all jobs before calling this */
		OSK_ASSERT( js_kctx_info->ctx.is_scheduled == MALI_FALSE );
		OSK_ASSERT( js_kctx_info->ctx.nr_jobs == 0 );
		OSK_ASSERT( js_kctx_info->ctx.nr_nss_jobs == 0 );
	}

	if ( (js_kctx_info->init_status & JS_KCTX_INIT_JSCTX_WAITQ) )
	{
		osk_waitq_term( &js_kctx_info->ctx.not_scheduled_waitq );
	}

	if ( (js_kctx_info->init_status & JS_KCTX_INIT_JSCTX_MUTEX) )
	{
		osk_mutex_term( &js_kctx_info->ctx.jsctx_mutex );
	}

	if ( (js_kctx_info->init_status & JS_KCTX_INIT_POLICY) )
	{
		kbasep_js_policy_term_ctx( js_policy, kctx );
	}

	js_kctx_info->init_status = JS_KCTX_INIT_NONE;
}

/**
 * Fast start a higher priority job when the runpool is full and contains a
 * non-running lower priority job.
 * The evicted job will be returned to the policy queue.
 *
 * The following locking conditions are made on the caller:
 * - The caller must hold the kbasep_js_kctx_info::ctx::jsctx_mutex.
 */
STATIC void kbasep_js_runpool_attempt_fast_start_ctx( kbase_device *kbdev, kbase_context *kctx )
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy      *js_policy;
	kbasep_js_per_as_data *js_per_as_data;
	int evict_as_nr;
	kbase_as *current_as;
	mali_bool nss_state_changed = MALI_FALSE;
	mali_bool is_runpool_full;

	OSK_ASSERT(kbdev != NULL);
	OSK_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	osk_mutex_lock(&js_devdata->runpool_mutex);

	/* If the runpool is full, attempt to fast start our context */
	is_runpool_full = (mali_bool)(js_devdata->nr_contexts_running >= kbdev->nr_address_spaces);

	if(is_runpool_full != MALI_FALSE)
	{
		/* No free address spaces - attempt to evict non-running lower priority context */
		osk_spinlock_irq_lock(&js_devdata->runpool_irq.lock);
		for(evict_as_nr = 0; evict_as_nr < kbdev->nr_address_spaces; evict_as_nr++)
		{
			current_as = &kbdev->as[evict_as_nr];
			js_per_as_data = &js_devdata->runpool_irq.per_as_data[evict_as_nr];

			/* Look for the AS which is not currently running */
			if(0 == js_per_as_data->as_busy_refcount)
			{
				kbase_context *kctx_evict = js_per_as_data->kctx;

				osk_spinlock_irq_unlock(&js_devdata->runpool_irq.lock);

				/* Now compare the scheduled priority we are considering evicting with the new ctx priority
				 * and take into consideration if the scheduled priority is a realtime policy or not.
				 * Note that the lower the number, the higher the priority
				 */
				if(kbasep_js_policy_ctx_has_priority(js_policy, kctx_evict, kctx))
				{
					/* Evict idle job in the runpool as priority is lower than new job */
					osk_mutex_lock(&current_as->transaction_mutex);
					osk_spinlock_irq_lock(&js_devdata->runpool_irq.lock);
					/* Remove the context from the runpool policy list (policy_info->scheduled_ctxs_head) */
					kbasep_js_policy_runpool_remove_ctx(js_policy, kctx_evict);
					/* Stop any more refcounts occuring on the context */
					js_per_as_data->kctx = NULL;

					/* Prevent a context from submitting more jobs on this policy */
					kbasep_js_clear_submit_allowed(js_devdata, kctx_evict);

					/* Disable the MMU on the affected address space, and indicate it's invalid */
					kbase_mmu_disable(kctx_evict);
					kctx_evict->as_nr = KBASEP_AS_NR_INVALID;

					/* NSS handling */
					nss_state_changed = kbasep_js_check_and_deref_nss_running_ctx(js_devdata, kctx_evict);
					CSTD_UNUSED(nss_state_changed);

					osk_spinlock_irq_unlock(&js_devdata->runpool_irq.lock);
					osk_mutex_unlock(&current_as->transaction_mutex);

					/* Free up the address space */
					js_devdata->as_free |= ((u16)(1u << evict_as_nr));

					/* update book-keeping info */
					--(js_devdata->nr_contexts_running);
					kctx_evict->jctx.sched_info.ctx.is_scheduled = MALI_FALSE;
					/* Signal any waiter that the context is not scheduled */
					osk_waitq_set(&kctx_evict->jctx.sched_info.ctx.not_scheduled_waitq);

					osk_mutex_unlock(&js_devdata->runpool_mutex);

					/* Requeue onto the policy queue */
					OSK_PRINT_INFO(OSK_BASE_JM, "JS: Requeue Context %p", kctx_evict);
					osk_mutex_lock(&js_devdata->queue_mutex);
					kbasep_js_policy_enqueue_ctx(js_policy, kctx_evict);
					osk_mutex_unlock(&js_devdata->queue_mutex);
					/* ctx fast start has taken place */
					return;
				}
				osk_spinlock_irq_lock(&js_devdata->runpool_irq.lock);
			}
		}
		osk_spinlock_irq_unlock(&js_devdata->runpool_irq.lock);
	}

	/* ctx fast start has not taken place */
	osk_mutex_unlock(&js_devdata->runpool_mutex);
}

mali_bool kbasep_js_add_job( kbase_context *kctx, kbase_jd_atom *atom )
{
	kbasep_js_kctx_info *js_kctx_info;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy    *js_policy;

	mali_bool policy_queue_updated = MALI_FALSE;

	OSK_ASSERT( kctx != NULL );
	OSK_ASSERT( atom != NULL );

	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	osk_mutex_lock( &js_devdata->runpool_mutex );
	{
		KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_ADD_JOB, kctx, atom->user_atom, atom->jc,
								  kbasep_js_trace_get_refcnt(kbdev, kctx));
	}

	/* Refcount ctx.nr_jobs */
	OSK_ASSERT( js_kctx_info->ctx.nr_jobs < U32_MAX );
	++(js_kctx_info->ctx.nr_jobs);

	/* Setup any scheduling information */
	kbasep_js_clear_job_retry_submit( atom );

	/*
	 * Begin Runpool_irq transaction
	 */
	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
	{
		/* NSS refcounting */
		kbasep_js_check_and_ref_nss_job( js_devdata, kctx, atom );

		/* Enqueue the job in the policy, causing it to be scheduled if the
		 * parent context gets scheduled */
		kbasep_js_policy_enqueue_job( js_policy, atom );
	}
	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );
	/* End runpool_irq transaction */

	if ( js_kctx_info->ctx.is_scheduled != MALI_FALSE )
	{
		/* Handle an already running context - try to run the new job, in case it
		 * matches requirements that aren't matched by any other job in the Run
		 * Pool */
		kbasep_js_try_run_next_job( kbdev );
	}
	osk_mutex_unlock( &js_devdata->runpool_mutex );

	if ( js_kctx_info->ctx.is_scheduled == MALI_FALSE && js_kctx_info->ctx.nr_jobs == 1 )
	{
		/* Handle Refcount going from 0 to 1: schedule the context on the Policy Queue */
		OSK_ASSERT( js_kctx_info->ctx.is_scheduled == MALI_FALSE );

		OSK_PRINT_INFO(OSK_BASE_JM, "JS: Enqueue Context %p", kctx );

		osk_mutex_lock( &js_devdata->queue_mutex );
		kbasep_js_policy_enqueue_ctx( js_policy, kctx );
		osk_mutex_unlock( &js_devdata->queue_mutex );
		/* If the runpool is full and this job has a higher priority than the non-running
		 * job in the runpool - evict it so this higher priority job starts faster */
		kbasep_js_runpool_attempt_fast_start_ctx( kbdev, kctx );

		/* This context is becoming active */
		kbase_pm_context_active(kctx->kbdev);

		/* NOTE: Potentially, we can make the scheduling of the head context
		 * happen in a work-queue if we need to wait for the PM to power
		 * up. Also need logic to submit nothing until PM really has completed
		 * powering up. */

		/* Policy Queue was updated - caller must try to schedule the head context */
		policy_queue_updated = MALI_TRUE;
	}

	return policy_queue_updated;
}

void kbasep_js_remove_job( kbase_context *kctx, kbase_jd_atom *atom )
{
	kbasep_js_kctx_info *js_kctx_info;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy    *js_policy;
	mali_bool nss_state_changed;

	OSK_ASSERT( kctx != NULL );
	OSK_ASSERT( atom != NULL );

	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	{
		KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_REMOVE_JOB, kctx, atom->user_atom, atom->jc,
								  kbasep_js_trace_get_refcnt(kbdev, kctx));
	}

	/* De-refcount ctx.nr_jobs */
	OSK_ASSERT( js_kctx_info->ctx.nr_jobs > 0 );
	--(js_kctx_info->ctx.nr_jobs);

	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
	nss_state_changed = kbasep_js_check_and_deref_nss_job( js_devdata, kctx, atom );
	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );

	/* De-register the job from the system */
	kbasep_js_policy_term_job( js_policy, atom );

	/* A change in NSS state means we might be able to run on slots that were
	 * previously empty, but could now run jobs on them */
	if ( nss_state_changed != MALI_FALSE )
	{
		osk_mutex_lock( &js_devdata->runpool_mutex );
		kbasep_js_try_run_next_job( kbdev );
		osk_mutex_unlock( &js_devdata->runpool_mutex );
	}

}


mali_bool kbasep_js_runpool_retain_ctx( kbase_device *kbdev, kbase_context *kctx )
{
	kbasep_js_device_data *js_devdata;
	mali_bool result;
	OSK_ASSERT( kbdev != NULL );
	js_devdata = &kbdev->js_data;

	/* KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_RETAIN_CTX, kctx, NULL, 0,
	                             kbasep_js_trace_get_refcnt(kbdev, kctx)); */
	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
	result = kbasep_js_runpool_retain_ctx_nolock( kbdev, kctx );
	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );

	return result;
}


kbase_context* kbasep_js_runpool_lookup_ctx( kbase_device *kbdev, int as_nr )
{
	kbasep_js_device_data *js_devdata;
	kbase_context *found_kctx = NULL;
	kbasep_js_per_as_data *js_per_as_data;

	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( 0 <= as_nr && as_nr < BASE_MAX_NR_AS );
	js_devdata = &kbdev->js_data;
	js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );

	found_kctx = js_per_as_data->kctx;

	if ( found_kctx != NULL )
	{
		++(js_per_as_data->as_busy_refcount);
	}

	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );

	return found_kctx;
}


void kbasep_js_runpool_release_ctx( kbase_device *kbdev, kbase_context *kctx )
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info   *js_kctx_info;
	kbasep_js_policy      *js_policy;
	kbasep_js_per_as_data *js_per_as_data;
	
	mali_bool was_descheduled = MALI_FALSE;
	int saved_as_nr;
	kbase_as *current_as;
	int new_ref_count;
	mali_bool nss_state_changed = MALI_FALSE;

	OSK_ASSERT( kbdev != NULL );
	OSK_ASSERT( kctx != NULL );
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;

	osk_mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
	osk_mutex_lock( &js_devdata->runpool_mutex );

	/* Ensure context really is scheduled in */
	OSK_ASSERT( js_kctx_info->ctx.is_scheduled != MALI_FALSE );

	/* The saved_as_nr must be accessed under lock, but we also need to take a
	 * sleeping mutex. Since the ctx is known to be busy-refcounted, we can
	 * just take the runpool lock briefly, then taken it again later (the as_nr
	 * won't be reassigned due to being busy).
	 *
	 * We ASSERT on this fact */
	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
	{
		saved_as_nr = kctx->as_nr;
		OSK_ASSERT( saved_as_nr != KBASEP_AS_NR_INVALID );
		js_per_as_data = &js_devdata->runpool_irq.per_as_data[saved_as_nr];
		OSK_ASSERT( js_per_as_data->as_busy_refcount > 0 );
	}
	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );

	/* 
	 * Transaction begins on AS and runpool_irq
	 *
	 * Doubly-assert that our previous facts are still true
	 */
	current_as = &kbdev->as[saved_as_nr];
	osk_mutex_lock( &current_as->transaction_mutex );
	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );
	OSK_ASSERT( saved_as_nr == kctx->as_nr );
	OSK_ASSERT( js_per_as_data->as_busy_refcount > 0 );

	/* Update refcount */
	new_ref_count = --(js_per_as_data->as_busy_refcount);

	KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_RELEASE_CTX, kctx, NULL, 0u,
							  new_ref_count);

	/* Make a set of checks to see if the context should be scheduled out */
	if ( new_ref_count == 0
		 && ( kctx->jctx.sched_info.ctx.nr_jobs == 0
			  || kbasep_js_is_submit_allowed( js_devdata, kctx ) == MALI_FALSE ) )
	{
		/* Last reference, and we've been told to remove this context from the Run Pool */
		OSK_PRINT_INFO(OSK_BASE_JM, "JS: RunPool Remove Context %p because as_busy_refcount=%d, jobs=%d, allowed=%d",
					   kctx,
					   new_ref_count,
					   js_kctx_info->ctx.nr_jobs,
					   kbasep_js_is_submit_allowed( js_devdata, kctx ) );

		kbasep_js_policy_runpool_remove_ctx( js_policy, kctx );

		/* Stop any more refcounts occuring on the context */
		js_per_as_data->kctx = NULL;

		/* Ensure we prevent the context from submitting any new jobs
		 * e.g. from kbasep_js_try_run_next_job_on_slot_irq_nolock()  */
		kbasep_js_clear_submit_allowed( js_devdata, kctx );

		/* Disable the MMU on the affected address space, and indicate it's invalid */
		kbase_mmu_disable( kctx );
		kctx->as_nr = KBASEP_AS_NR_INVALID;

		/* NSS handling */
		nss_state_changed = kbasep_js_check_and_deref_nss_running_ctx( js_devdata, kctx );

		/*
		 * Transaction ends on AS and runpool_irq:
		 *
		 * By this point, the AS-related data is now clear and ready for re-use.
		 *
		 * Since releases only occur once for each previous successful retain, and no more
		 * retains are allowed on this context, no other thread will be operating in this
		 * code whilst we are
		 */
		osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );
		osk_mutex_unlock( &current_as->transaction_mutex );

		/* Free up the address space */
		js_devdata->as_free |= ((u16)(1u << saved_as_nr));
		/* Note: Don't reuse saved_as_nr now */

		/* update book-keeping info */
		--(js_devdata->nr_contexts_running);
		js_kctx_info->ctx.is_scheduled = MALI_FALSE;
		/* Signal any waiter that the context is not scheduled, so is safe for
		 * termination - once the jsctx_mutex is also dropped, and jobs have
		 * finished. */
		osk_waitq_set( &js_kctx_info->ctx.not_scheduled_waitq );

		/* Handle dying contexts */
		if ( js_kctx_info->ctx.is_dying != MALI_FALSE )
		{
			/* This happens asynchronously */
			OSK_PRINT_INFO(OSK_BASE_JM, "JS: ** Killing Context %p on RunPool Remove **", kctx );
			kbasep_js_policy_kill_all_ctx_jobs( js_policy, kctx );
		}

		/* Queue an action to occur after we've dropped the lock */
		was_descheduled = MALI_TRUE;

	}
	else
	{
		osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );
		osk_mutex_unlock( &current_as->transaction_mutex );
	}
	osk_mutex_unlock( &js_devdata->runpool_mutex );

	/* Do we have an action queued whilst the lock was held? */
	if ( was_descheduled != MALI_FALSE )
	{
		/* Determine whether this context should be requeued on the policy queue */
		if ( js_kctx_info->ctx.nr_jobs > 0 && js_kctx_info->ctx.is_dying == MALI_FALSE )
		{
			OSK_PRINT_INFO(OSK_BASE_JM, "JS: Requeue Context %p", kctx );
			osk_mutex_lock( &js_devdata->queue_mutex );
			kbasep_js_policy_enqueue_ctx( js_policy, kctx );
			osk_mutex_unlock( &js_devdata->queue_mutex );
		}
		else
		{
			OSK_PRINT_INFO(OSK_BASE_JM, "JS: Idling Context %p (not requeued)", kctx );
			/* Notify PM that a context has gone idle */
			kbase_pm_context_idle(kctx->kbdev);
		}
	}
	/* We've finished with this context for now, so drop the lock for it. */
	osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );

	if ( was_descheduled != MALI_FALSE )
	{
		/* We've freed up an address space, so let's try to schedule in another
		 * context
		 *
		 * Note: if there's a context to schedule in, then it also tries to run
		 * another job, in case the new context has jobs satisfying requirements
		 * that no other context/job in the runpool does */
		kbasep_js_try_schedule_head_ctx( kbdev );
	}

	if ( nss_state_changed != MALI_FALSE )
	{
		osk_mutex_lock( &js_devdata->runpool_mutex );
		kbasep_js_try_run_next_job( kbdev );
		osk_mutex_unlock( &js_devdata->runpool_mutex );
	}

}

STATIC mali_bool kbasep_js_job_check_ref_cores(kbase_device *kbdev, int js, kbase_jd_atom *katom)
{
	mali_bool cores_ready = MALI_TRUE;
	u64 tiler_affinity = 0;

	if (katom->core_req & BASE_JD_REQ_T)
	{
		tiler_affinity = kbdev->tiler_present_bitmap;
	}

	/* This is done once when the job is first attempted to be run */
	if ( katom->affinity == 0 )
	{
		/* Compute affinity */
		kbase_js_choose_affinity( &katom->affinity, kbdev, katom, js );

		/* Request the cores */
		if (MALI_ERROR_NONE != kbase_pm_request_cores( kbdev, katom->affinity, tiler_affinity ))
		{
			/* Failed to request cores, unset the affinity so we try again and return */
			katom->affinity = 0;
			return MALI_FALSE;
		}
	}

	cores_ready = kbase_pm_register_inuse_cores( kbdev, katom->affinity, tiler_affinity );

	return cores_ready;
}

/*
 * Note: this function is quite similar to kbasep_js_try_run_next_job_on_slot()
 */
mali_bool kbasep_js_try_run_next_job_on_slot_irq_nolock( kbase_device *kbdev, int js, s8 *submit_count )
{
	kbasep_js_device_data *js_devdata;
	mali_bool tried_to_dequeue_jobs_but_failed = MALI_FALSE;
	mali_bool cores_ready;

	OSK_ASSERT( kbdev != NULL );

	js_devdata = &kbdev->js_data;

#if BASE_HW_ISSUE_7347
	for(js = 0; js < kbdev->nr_job_slots; js++)
	{
#endif

	/* The caller of this function may not be aware of NSS status changes so we
	 * must recheck if the given slot is still valid. Otherwise do not try to run.
	 */
	if (kbase_js_can_run_job_on_slot_no_lock( js_devdata, js))
	{
		/* Keep submitting while there's space to run a job on this job-slot,
		 * and there are jobs to get that match its requirements (see 'break'
		 * statement below) */
		while ( *submit_count < KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ
				&& kbasep_jm_is_submit_slots_free( kbdev, js, NULL ) != MALI_FALSE )
		{
			kbase_jd_atom *dequeued_atom;
			mali_bool has_job = MALI_FALSE;
			
			/* Dequeue a job that matches the requirements */
			has_job = kbasep_js_policy_dequeue_job_irq( kbdev, js, &dequeued_atom );
			
			if ( has_job != MALI_FALSE )
			{
				/* NOTE: since the runpool_irq lock is currently held and acts across
				 * all address spaces, any context whose busy refcount has reached
				 * zero won't yet be scheduled out whilst we're trying to run jobs
				 * from it */
				kbase_context *parent_ctx = dequeued_atom->kctx;
				mali_bool retain_success;

				/* Retain/power up the cores it needs, check if cores are ready */
				cores_ready = kbasep_js_job_check_ref_cores( kbdev, js, dequeued_atom );

				if ( cores_ready != MALI_TRUE )
				{
					/* The job can't be submitted until the cores are ready, requeue the job */
					kbasep_js_policy_enqueue_job( &kbdev->js_data.policy, dequeued_atom );
					break;
				}

				/* ASSERT that the Policy picked a job from an allowed context */
				OSK_ASSERT( kbasep_js_is_submit_allowed( js_devdata, parent_ctx) );

				/* Retain the context to stop it from being scheduled out
				 * This is released when the job finishes */
				retain_success = kbasep_js_runpool_retain_ctx_nolock( kbdev, parent_ctx );
				OSK_ASSERT( retain_success != MALI_FALSE );
				CSTD_UNUSED( retain_success );

				/* Check if this job needs the cycle counter enabled before submission */
				kbasep_js_ref_permon_check_and_enable_cycle_counter( kbdev, dequeued_atom );

				/* Submit the job */
				kbase_job_submit_nolock( kbdev, dequeued_atom, js );

				++(*submit_count);
			}
			else
			{
				tried_to_dequeue_jobs_but_failed = MALI_TRUE;
				/* No more jobs - stop submitting for this slot */
				break;
			}
		}
	}

#if BASE_HW_ISSUE_7347
	}
#endif

	/* Indicate whether a retry in submission should be tried on a different
	 * dequeue function. These are the reasons why it *must* happen:
	 *
	 * - kbasep_js_policy_dequeue_job_irq() couldn't get any jobs. In this case,
	 *   kbasep_js_policy_dequeue_job() might be able to get jobs (must be done
	 *   outside of IRQ)
	 * - kbasep_js_policy_dequeue_job_irq() got some jobs, but failed to get a
	 *   job in the last call to it. Again, kbasep_js_policy_dequeue_job()
	 *   might be able to get jobs.
	 * - the KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ threshold was reached
	 *   and new scheduling must be performed outside of IRQ mode.
	 *
	 * Failure to indicate this correctly could stop further jobs being processed.
	 *
	 * However, we do not _need_ to indicate a retry for the following:
	 * - kbasep_jm_is_submit_slots_free() was MALI_FALSE, indicating jobs were
	 *   already running. When those jobs complete, that will still cause events
	 *   that cause us to resume job submission.
	 * - kbase_js_can_run_job_on_slot_no_lock() was MALI_FALSE - this is for
	 *   NSS handling. That _can_ change outside of IRQ context, but is handled
	 *   explicitly by kbasep_js_remove_job() and kbasep_js_runpool_release_ctx().
	 */
	return (mali_bool)(tried_to_dequeue_jobs_but_failed || *submit_count >= KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ);
}

void kbasep_js_try_run_next_job_on_slot( kbase_device *kbdev, int js )
{
	kbasep_js_device_data *js_devdata;
	mali_bool has_job;
	mali_bool cores_ready;

	OSK_ASSERT( kbdev != NULL );

	js_devdata = &kbdev->js_data;

#if BASE_HW_ISSUE_7347
	for(js = 0; js < kbdev->nr_job_slots; js++)
	{
#endif

	kbase_job_slot_lock(kbdev, js);

	/* Keep submitting while there's space to run a job on this job-slot,
	 * and there are jobs to get that match its requirements (see 'break'
	 * statement below) */
	if (  kbasep_jm_is_submit_slots_free( kbdev, js, NULL ) != MALI_FALSE )
	{
		/* Only lock the Run Pool whilst there's work worth doing */
		osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );

		/* The caller of this function may not be aware of NSS status changes so we
		 * must recheck if the given slot is still valid. Otherwise do not try to run.
		 */
		if (kbase_js_can_run_job_on_slot_no_lock( js_devdata, js))
		{
			do {
				kbase_jd_atom *dequeued_atom;

				/* Dequeue a job that matches the requirements */
				has_job = kbasep_js_policy_dequeue_job( kbdev, js, &dequeued_atom );

				if ( has_job != MALI_FALSE )
				{
					/* NOTE: since the runpool_irq lock is currently held and acts across
					 * all address spaces, any context whose busy refcount has reached
					 * zero won't yet be scheduled out whilst we're trying to run jobs
					 * from it */
					kbase_context *parent_ctx = dequeued_atom->kctx;
					mali_bool retain_success;

					/* Retain/power up the cores it needs, check if cores are ready */
					cores_ready = kbasep_js_job_check_ref_cores( kbdev, js, dequeued_atom );

					if ( cores_ready != MALI_TRUE )
					{
						/* The job can't be submitted until the cores are ready, requeue the job */
						kbasep_js_policy_enqueue_job( &kbdev->js_data.policy, dequeued_atom );
						break;
					}
					/* ASSERT that the Policy picked a job from an allowed context */
					OSK_ASSERT( kbasep_js_is_submit_allowed( js_devdata, parent_ctx) );

					/* Retain the context to stop it from being scheduled out
					 * This is released when the job finishes */
					retain_success = kbasep_js_runpool_retain_ctx_nolock( kbdev, parent_ctx );
					OSK_ASSERT( retain_success != MALI_FALSE );
					CSTD_UNUSED( retain_success );

					/* Check if this job needs the cycle counter enabled before submission */
					kbasep_js_ref_permon_check_and_enable_cycle_counter( kbdev, dequeued_atom );

					/* Submit the job */
					kbase_job_submit_nolock( kbdev, dequeued_atom, js );
				}

			} while ( kbasep_jm_is_submit_slots_free( kbdev, js, NULL ) != MALI_FALSE
				      && has_job != MALI_FALSE );
		}

		osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );
	}
	kbase_job_slot_unlock(kbdev, js);
#if BASE_HW_ISSUE_7347
	}
#endif
}

void kbasep_js_try_schedule_head_ctx( kbase_device *kbdev )
{
	kbasep_js_device_data *js_devdata;
	mali_bool has_kctx;
	kbase_context *head_kctx;
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool is_runpool_full;

	OSK_ASSERT( kbdev != NULL );

	js_devdata = &kbdev->js_data;

	/* Make a speculative check on the Run Pool - this MUST be repeated once
	 * we've obtained a context from the queue and reobtained the Run Pool
	 * lock */
	osk_mutex_lock( &js_devdata->runpool_mutex );
	is_runpool_full = (mali_bool)( js_devdata->nr_contexts_running >= kbdev->nr_address_spaces );
	osk_mutex_unlock( &js_devdata->runpool_mutex );

	if ( is_runpool_full != MALI_FALSE )
	{
		/* No free address spaces - nothing to do */
		return;
	}

	/* Grab the context off head of queue - if there is one */
	osk_mutex_lock( &js_devdata->queue_mutex );
	has_kctx = kbasep_js_policy_dequeue_head_ctx( &js_devdata->policy, &head_kctx );
	osk_mutex_unlock( &js_devdata->queue_mutex );

	if ( has_kctx == MALI_FALSE )
	{
		/* No ctxs to run - nothing to do */
		return;
	}
	js_kctx_info = &head_kctx->jctx.sched_info;

	OSK_PRINT_INFO(OSK_BASE_JM, "JS: Dequeue Context %p", head_kctx );

	/*
	 * Atomic transaction on the Context and Run Pool begins
	 */
	osk_mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
	osk_mutex_lock( &js_devdata->runpool_mutex );

	/* Re-check to see if the Run Pool is full */
	is_runpool_full = (mali_bool)( js_devdata->nr_contexts_running >= kbdev->nr_address_spaces );
	if ( is_runpool_full != MALI_FALSE )
	{
		/* No free address spaces - roll back the transaction so far and return */
		osk_mutex_unlock( &js_devdata->runpool_mutex );

		/* Only requeue if not dying - which might occur through zapping-whilst-scheduling */
		if ( js_kctx_info->ctx.is_dying == MALI_FALSE )
		{
			OSK_PRINT_INFO(OSK_BASE_JM, "JS: Transaction rollback: Requeue Context %p", head_kctx );

			osk_mutex_lock( &js_devdata->queue_mutex );
			kbasep_js_policy_enqueue_ctx( &js_devdata->policy, head_kctx );
			osk_mutex_unlock( &js_devdata->queue_mutex );
		}
		else
		{
			OSK_PRINT_INFO(OSK_BASE_JM, "JS: Transaction rollback: Context %p is dying. Kill remaining jobs and pm-idle ctx", head_kctx );
			OSK_ASSERT( js_kctx_info->ctx.nr_jobs > 0 );
			/* Notify PM that a context has gone idle */
			kbase_pm_context_idle(kbdev);

			/* Kill all the jobs present (call kbase_jd_cancel on all jobs) */
			kbasep_js_policy_kill_all_ctx_jobs( &js_devdata->policy, head_kctx );

			/* Nothing more to be done to kill the context here, kbase_jd_zap_context
			 * waits for all jobs to be cancelled */
		}

		osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
		return;
	}

	KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_TRY_SCHEDULE_HEAD_CTX, head_kctx, NULL, 0u,
							  kbasep_js_trace_get_refcnt(kbdev, head_kctx));


	/* update book-keeping info */
	js_kctx_info->ctx.is_scheduled = MALI_TRUE;

#if MALI_CUSTOMER_RELEASE == 0
	/* Latch in run-time job scheduler timeouts that were set through js_timeouts sysfs file */
	if (kbdev->js_soft_stop_ticks != 0)
	{
		js_devdata->soft_stop_ticks = kbdev->js_soft_stop_ticks;
	}
	if (kbdev->js_hard_stop_ticks_ss != 0)
	{
		js_devdata->hard_stop_ticks_ss = kbdev->js_hard_stop_ticks_ss;
	}
	if (kbdev->js_hard_stop_ticks_nss != 0)
	{
		js_devdata->hard_stop_ticks_nss = kbdev->js_hard_stop_ticks_nss;
	}
	if (kbdev->js_reset_ticks_ss != 0)
	{
		js_devdata->gpu_reset_ticks_ss = kbdev->js_reset_ticks_ss;
	}
	if (kbdev->js_reset_ticks_nss != 0)
	{
		js_devdata->gpu_reset_ticks_nss = kbdev->js_reset_ticks_nss;
	}
#endif

	++(js_devdata->nr_contexts_running);
	/* Cause any future waiter-on-termination to wait until the context is
	 * descheduled */
	osk_waitq_clear( &js_kctx_info->ctx.not_scheduled_waitq );


	/* Do all the necessaries to pick the address space (inc. update book-keeping info)
	 * Add the context to the Run Pool, and allow it to run jobs */
	assign_and_activate_kctx_addr_space( kbdev, head_kctx );

	/* Check and setup HW counters dumping */
	osk_spinlock_lock(&kbdev->hwcnt_lock);
	osk_spinlock_irq_lock(&js_devdata->runpool_irq.lock);
	if (head_kctx == kbdev->hwcnt_context &&
		kbdev->hwcnt_is_setup == MALI_FALSE)
	{
		/* Setup the base address */
#if BASE_HW_ISSUE_8186
		u32 val;
		/* Save and clear PRFCNT_TILER_EN */
		val = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), head_kctx);
		if(0 != val)
		{
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), 0, head_kctx);
		}
		/* Update PRFCNT_CONFIG with TILER_EN = 0 */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (head_kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, head_kctx);
		/* Restore PRFCNT_TILER_EN */
		if(0 != val)
		{
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN),  val, head_kctx);
		}
#else
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (head_kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, head_kctx);
#endif
		/* Prevent the context to be scheduled out */
		kbasep_js_runpool_retain_ctx_nolock(kbdev, head_kctx);

		kbdev->hwcnt_is_setup = MALI_TRUE;
	}
	osk_spinlock_irq_unlock(&js_devdata->runpool_irq.lock);
	osk_spinlock_unlock(&kbdev->hwcnt_lock);

	/* Try to run the next job, in case this context has jobs that match the
	 * job slot requirements, but none of the other currently running contexts
	 * do */
	kbasep_js_try_run_next_job( kbdev );

	/* Transaction complete */
	osk_mutex_unlock( &js_devdata->runpool_mutex );
	osk_mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
	/* Note: after this point, the context could potentially get scheduled out immediately */
}


void kbasep_js_job_done_slot_irq( kbase_device *kbdev, int s, kbase_jd_atom *katom, kbasep_js_tick *end_timestamp )
{
	kbasep_js_policy *js_policy;
	kbasep_js_device_data *js_devdata;
	mali_bool submit_retry_needed;
	kbasep_js_tick tick_diff;
	u32 microseconds_spent;
	kbase_context *parent_ctx;

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	parent_ctx = katom->kctx;

	/* Calculate the job's time used */
	tick_diff = *end_timestamp - katom->start_timestamp;
	microseconds_spent = kbasep_js_convert_js_ticks_to_us( tick_diff );
	/* Round up time spent to the minimum timer resolution */
	if (microseconds_spent < KBASEP_JS_TICK_RESOLUTION_US)
	{
		microseconds_spent = KBASEP_JS_TICK_RESOLUTION_US;
	}

	/* Lock the runpool_irq for modifying the runpool_irq data */
	osk_spinlock_irq_lock( &js_devdata->runpool_irq.lock );

	/* Check if submitted jobs no longer require the cycle counter to be enabled */
	kbasep_js_deref_permon_check_and_disable_cycle_counter( kbdev, katom );

	/* Log the result of the job (completion status, and time spent). */
	kbasep_js_policy_log_job_result( js_policy, katom, microseconds_spent );
	/* Determine whether the parent context's timeslice is up */
	if ( kbasep_js_policy_should_remove_ctx( js_policy, parent_ctx ) != MALI_FALSE )
	{
		kbasep_js_clear_submit_allowed( js_devdata, parent_ctx );
	}

	/* Submit a new job (if there is one) to help keep the GPU's HEAD and NEXT registers full */
	KBASE_TRACE_ADD_SLOT( kbdev, JS_JOB_DONE_TRY_RUN_NEXT_JOB, parent_ctx, katom->user_atom, katom->jc, s);

	submit_retry_needed = kbasep_js_try_run_next_job_on_slot_irq_nolock(
		kbdev,
		s,
		&kbdev->slot_submit_count_irq[s] );

	osk_spinlock_irq_unlock( &js_devdata->runpool_irq.lock );
	/* We've finished modifying runpool_irq data, so the lock is dropped */

	if ( submit_retry_needed != MALI_FALSE )
	{
		KBASE_TRACE_ADD_SLOT( kbdev, JS_JOB_DONE_RETRY_NEEDED, parent_ctx, katom->user_atom, katom->jc, s);
		kbasep_js_set_job_retry_submit_slot( katom, s );
	}

}
