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





/*
 * Job Scheduler Implementation
 */
#include <mali_kbase.h>
#include <mali_kbase_js.h>
#include <mali_kbase_js_affinity.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_hw.h>

#include "mali_kbase_jm.h"
#include <mali_kbase_defs.h>

/*
 * Private types
 */

/** Bitpattern indicating the result of releasing a context */
enum {
	/** The context was descheduled - caller should try scheduling in a new one
	 * to keep the runpool full */
	KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED = (1u << 0),
};

typedef u32 kbasep_js_release_result;

/*
 * Private function prototypes
 */
STATIC INLINE void kbasep_js_deref_permon_check_and_disable_cycle_counter(kbase_device *kbdev, kbase_jd_atom *katom);

STATIC INLINE void kbasep_js_ref_permon_check_and_enable_cycle_counter(kbase_device *kbdev, kbase_jd_atom *katom);

STATIC kbasep_js_release_result kbasep_js_runpool_release_ctx_internal(kbase_device *kbdev, kbase_context *kctx, kbasep_js_atom_retained_state *katom_retained_state);

/** Helper for trace subcodes */
#if KBASE_TRACE_ENABLE != 0
STATIC int kbasep_js_trace_get_refcnt(kbase_device *kbdev, kbase_context *kctx)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;
	int as_nr;
	int refcnt = 0;

	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	as_nr = kctx->as_nr;
	if (as_nr != KBASEP_AS_NR_INVALID) {
		kbasep_js_per_as_data *js_per_as_data;
		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		refcnt = js_per_as_data->as_busy_refcount;
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	return refcnt;
}
#else				/* KBASE_TRACE_ENABLE != 0 */
STATIC int kbasep_js_trace_get_refcnt(kbase_device *kbdev, kbase_context *kctx)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
	return 0;
}
#endif				/* KBASE_TRACE_ENABLE != 0 */

/*
 * Private types
 */
enum {
	JS_DEVDATA_INIT_NONE = 0,
	JS_DEVDATA_INIT_CONSTANTS = (1 << 0),
	JS_DEVDATA_INIT_POLICY = (1 << 1),
	JS_DEVDATA_INIT_ALL = ((1 << 2) - 1)
};

enum {
	JS_KCTX_INIT_NONE = 0,
	JS_KCTX_INIT_CONSTANTS = (1 << 0),
	JS_KCTX_INIT_POLICY = (1 << 1),
	JS_KCTX_INIT_ALL = ((1 << 2) - 1)
};

/*
 * Private functions
 */

/**
 * Check if the job had performance monitoring enabled and decrement the count.  If no jobs require
 * performance monitoring, then the cycle counters will be disabled in the GPU.
 *
 * No locks need to be held - locking is handled further down
 *
 * This function does not sleep.
 */

STATIC INLINE void kbasep_js_deref_permon_check_and_disable_cycle_counter(kbase_device *kbdev, kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	if (katom->core_req & BASE_JD_REQ_PERMON)
		kbase_pm_release_gpu_cycle_counter(kbdev);
}

/**
 * Check if the job has performance monitoring enabled and keep a count of it.  If at least one
 * job requires performance monitoring, then the cycle counters will be enabled in the GPU.
 *
 * No locks need to be held - locking is handled further down
 *
 * This function does not sleep.
 */

STATIC INLINE void kbasep_js_ref_permon_check_and_enable_cycle_counter(kbase_device *kbdev, kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	if (katom->core_req & BASE_JD_REQ_PERMON)
		kbase_pm_request_gpu_cycle_counter(kbdev);
}

/*
 * The following locking conditions are made on the caller:
 * - The caller must hold the kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - The caller must hold the kbasep_js_device_data::runpool_mutex
 */
STATIC INLINE void runpool_inc_context_count(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	BUG_ON(!mutex_is_locked(&js_kctx_info->ctx.jsctx_mutex));
	BUG_ON(!mutex_is_locked(&js_devdata->runpool_mutex));

	/* Track total contexts */
	KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running < S8_MAX);
	++(js_devdata->nr_all_contexts_running);

	if ((js_kctx_info->ctx.flags & KBASE_CTX_FLAG_SUBMIT_DISABLED) == 0) {
		/* Track contexts that can submit jobs */
		KBASE_DEBUG_ASSERT(js_devdata->nr_user_contexts_running < S8_MAX);
		++(js_devdata->nr_user_contexts_running);
	}
}

/*
 * The following locking conditions are made on the caller:
 * - The caller must hold the kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - The caller must hold the kbasep_js_device_data::runpool_mutex
 */
STATIC INLINE void runpool_dec_context_count(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	BUG_ON(!mutex_is_locked(&js_kctx_info->ctx.jsctx_mutex));
	BUG_ON(!mutex_is_locked(&js_devdata->runpool_mutex));

	/* Track total contexts */
	--(js_devdata->nr_all_contexts_running);
	KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running >= 0);

	if ((js_kctx_info->ctx.flags & KBASE_CTX_FLAG_SUBMIT_DISABLED) == 0) {
		/* Track contexts that can submit jobs */
		--(js_devdata->nr_user_contexts_running);
		KBASE_DEBUG_ASSERT(js_devdata->nr_user_contexts_running >= 0);
	}
}

/**
 * @brief check whether the runpool is full for a specified context
 *
 * If kctx == NULL, then this makes the least restrictive check on the
 * runpool. A specific context that is supplied immediately after could fail
 * the check, even under the same conditions.
 *
 * Therefore, once a context is obtained you \b must re-check it with this
 * function, since the return value could change to MALI_FALSE.
 *
 * The following locking conditions are made on the caller:
 * - In all cases, the caller must hold kbasep_js_device_data::runpool_mutex
 * - When kctx != NULL the caller must hold the kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - When kctx == NULL, then the caller need not hold any jsctx_mutex locks (but it doesn't do any harm to do so).
 */
STATIC mali_bool check_is_runpool_full(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_device_data *js_devdata;
	mali_bool is_runpool_full;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;
	BUG_ON(!mutex_is_locked(&js_devdata->runpool_mutex));

	/* Regardless of whether a context is submitting or not, can't have more than there
	 * are HW address spaces */
	is_runpool_full = (mali_bool) (js_devdata->nr_all_contexts_running >= kbdev->nr_hw_address_spaces);

	if (kctx != NULL && (kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_SUBMIT_DISABLED) == 0) {
		BUG_ON(!mutex_is_locked(&kctx->jctx.sched_info.ctx.jsctx_mutex));
		/* Contexts that submit might use less of the address spaces available, due to HW
		 * workarounds.  In which case, the runpool is also full when the number of
		 * submitting contexts exceeds the number of submittable address spaces.
		 *
		 * Both checks must be made: can have nr_user_address_spaces == nr_hw_address spaces,
		 * and at the same time can have nr_user_contexts_running < nr_all_contexts_running. */
		is_runpool_full |= (mali_bool) (js_devdata->nr_user_contexts_running >= kbdev->nr_user_address_spaces);
	}

	return is_runpool_full;
}

STATIC base_jd_core_req core_reqs_from_jsn_features(u16 features) /* JS<n>_FEATURE register value */
{
	base_jd_core_req core_req = 0u;

	if ((features & JSn_FEATURE_SET_VALUE_JOB) != 0)
		core_req |= BASE_JD_REQ_V;

	if ((features & JSn_FEATURE_CACHE_FLUSH_JOB) != 0)
		core_req |= BASE_JD_REQ_CF;

	if ((features & JSn_FEATURE_COMPUTE_JOB) != 0)
		core_req |= BASE_JD_REQ_CS;

	if ((features & JSn_FEATURE_TILER_JOB) != 0)
		core_req |= BASE_JD_REQ_T;

	if ((features & JSn_FEATURE_FRAGMENT_JOB) != 0)
		core_req |= BASE_JD_REQ_FS;

	return core_req;
}

/**
 * Picks and reserves an address space.
 *
 * When this function returns, the address space returned is reserved and
 * cannot be picked for another context until it is released.
 *
 * The caller must ensure there \b is a free address space before calling this.
 *
 * The following locking conditions are made on the caller:
 * - it must hold kbasep_js_device_data::runpool_mutex
 *
 * @return a non-NULL pointer to a kbase_as that is not in use by any other context
 */
STATIC kbase_as *pick_free_addr_space(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	kbase_as *current_as;
	long ffs_result;
	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&js_devdata->runpool_mutex);

	/* Find the free address space */
	ffs_result = ffs(js_devdata->as_free) - 1;

	/* ASSERT that we should've found a free one */
	KBASE_DEBUG_ASSERT(0 <= ffs_result && ffs_result < kbdev->nr_hw_address_spaces);
	/* Ensure no-one else picks this one */
	js_devdata->as_free &= ~((u16) (1u << ffs_result));

	current_as = &kbdev->as[ffs_result];

	return current_as;
}

/**
 * Release an address space, making it available for being picked again.
 *
 * The following locking conditions are made on the caller:
 * - it must hold kbasep_js_device_data::runpool_mutex
 */
STATIC INLINE void release_addr_space(kbase_device *kbdev, int kctx_as_nr)
{
	kbasep_js_device_data *js_devdata;
	u16 as_bit = (1u << kctx_as_nr);

	js_devdata = &kbdev->js_data;
	lockdep_assert_held(&js_devdata->runpool_mutex);

	/* The address space must not already be free */
	KBASE_DEBUG_ASSERT(!(js_devdata->as_free & as_bit));

	js_devdata->as_free |= as_bit;
}

/**
 * Assign an Address Space (AS) to a context, and add the context to the Policy.
 *
 * This includes:
 * - setting up the global runpool_irq structure and the context on the AS
 * - Activating the MMU on the AS
 * - Allowing jobs to be submitted on the AS
 *
 * Locking conditions:
 * - Caller must hold the kbasep_js_kctx_info::jsctx_mutex
 * - Caller must hold the kbasep_js_device_data::runpool_mutex
 * - Caller must hold AS transaction mutex
 * - Caller must hold Runpool IRQ lock
 */
STATIC void assign_and_activate_kctx_addr_space(kbase_device *kbdev, kbase_context *kctx, kbase_as *current_as)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_per_as_data *js_per_as_data;
	int as_nr;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(current_as != NULL);

	js_devdata = &kbdev->js_data;
	as_nr = current_as->number;

	lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&current_as->transaction_mutex);
	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

	/* Attribute handling */
	kbasep_js_ctx_attr_runpool_retain_ctx(kbdev, kctx);

	/* Assign addr space */
	kctx->as_nr = as_nr;
#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_mmu_as_in_use(kctx->as_nr);
#endif				/* CONFIG_MALI_GATOR_SUPPORT */
	/* Activate this address space on the MMU */
	kbase_mmu_update(kctx);

	/* Allow it to run jobs */
	kbasep_js_set_submit_allowed(js_devdata, kctx);

	/* Book-keeping */
	js_per_as_data->kctx = kctx;
	js_per_as_data->as_busy_refcount = 0;

	/* Lastly, add the context to the policy's runpool - this really allows it to run jobs */
	kbasep_js_policy_runpool_add_ctx(&js_devdata->policy, kctx);

}

void kbasep_js_try_run_next_job_nolock(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	int js;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	/* It's cheap and simple to retest this here - otherwise we burden the
	 * caller with it. In some cases, we do this higher up to optimize out the
	 * spinlock. */
	if (js_devdata->nr_user_contexts_running == 0)
		return; /* No contexts present - the GPU might be powered off, so just return */

	for (js = 0; js < kbdev->gpu_props.num_job_slots; ++js)
		kbasep_js_try_run_next_job_on_slot_nolock(kbdev, js);
}

/** Hold the kbasep_js_device_data::runpool_irq::lock for this */
mali_bool kbasep_js_runpool_retain_ctx_nolock(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_per_as_data *js_per_as_data;
	mali_bool result = MALI_FALSE;
	int as_nr;
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_devdata = &kbdev->js_data;

	as_nr = kctx->as_nr;
	if (as_nr != KBASEP_AS_NR_INVALID) {
		int new_refcnt;

		KBASE_DEBUG_ASSERT(as_nr >= 0);
		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		KBASE_DEBUG_ASSERT(js_per_as_data->kctx != NULL);

		new_refcnt = ++(js_per_as_data->as_busy_refcount);
		KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_RETAIN_CTX_NOLOCK, kctx, NULL, 0u, new_refcnt);
		result = MALI_TRUE;
	}

	return result;
}

/*
 * Functions private to KBase ('Protected' functions)
 */
void kbase_js_try_run_jobs(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;

	mutex_lock(&js_devdata->runpool_mutex);
	if (js_devdata->nr_user_contexts_running != 0) {
		/* Only try running jobs when we have contexts present, otherwise the GPU might be powered off.  */
		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

		kbasep_js_try_run_next_job_nolock(kbdev);

		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
	}
	mutex_unlock(&js_devdata->runpool_mutex);
}

void kbase_js_try_run_jobs_on_slot(kbase_device *kbdev, int js)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;

	mutex_lock(&js_devdata->runpool_mutex);
	if (js_devdata->nr_user_contexts_running != 0) {
		/* Only try running jobs when we have contexts present, otherwise the GPU might be powered off.  */
		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

		kbasep_js_try_run_next_job_on_slot_nolock(kbdev, js);

		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
	}
	mutex_unlock(&js_devdata->runpool_mutex);
}

mali_error kbasep_js_devdata_init(kbase_device * const kbdev)
{
	kbasep_js_device_data *js_devdata;
	mali_error err;
	int i;
	u16 as_present;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	KBASE_DEBUG_ASSERT(js_devdata->init_status == JS_DEVDATA_INIT_NONE);

	/* These two must be recalculated if nr_hw_address_spaces changes (e.g. for HW workarounds) */
	as_present = (1U << kbdev->nr_hw_address_spaces) - 1;
	kbdev->nr_user_address_spaces = kbdev->nr_hw_address_spaces;
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987)) {
		mali_bool use_workaround_for_security;
		use_workaround_for_security = (mali_bool) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_SECURE_BUT_LOSS_OF_PERFORMANCE);
		if (use_workaround_for_security != MALI_FALSE) {
			dev_dbg(kbdev->dev, "GPU has HW ISSUE 8987, and driver configured for security workaround: 1 address space only");
			kbdev->nr_user_address_spaces = 1;
		}
	}
#ifdef CONFIG_MALI_DEBUG
	/* Soft-stop will be disabled on a single context by default unless softstop_always is set */
	js_devdata->softstop_always = MALI_FALSE;
#endif				/* CONFIG_MALI_DEBUG */
	js_devdata->nr_all_contexts_running = 0;
	js_devdata->nr_user_contexts_running = 0;
	js_devdata->as_free = as_present;	/* All ASs initially free */
	js_devdata->runpool_irq.submit_allowed = 0u;	/* No ctx allowed to submit */
	memset(js_devdata->runpool_irq.ctx_attr_ref_count, 0, sizeof(js_devdata->runpool_irq.ctx_attr_ref_count));
	memset(js_devdata->runpool_irq.slot_affinities, 0, sizeof(js_devdata->runpool_irq.slot_affinities));
	js_devdata->runpool_irq.slots_blocked_on_affinity = 0u;
	memset(js_devdata->runpool_irq.slot_affinity_refcount, 0, sizeof(js_devdata->runpool_irq.slot_affinity_refcount));
	INIT_LIST_HEAD(&js_devdata->suspended_soft_jobs_list);

	/* Config attributes */
	js_devdata->scheduling_tick_ns = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_SCHEDULING_TICK_NS);
	js_devdata->soft_stop_ticks = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS);
	js_devdata->soft_stop_ticks_cl = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS_CL);
	js_devdata->hard_stop_ticks_ss = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS);
	js_devdata->hard_stop_ticks_cl = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_CL);
	js_devdata->hard_stop_ticks_nss = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS);
	js_devdata->gpu_reset_ticks_ss = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS);
	js_devdata->gpu_reset_ticks_cl = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TICKS_CL);
	js_devdata->gpu_reset_ticks_nss = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS);
	js_devdata->ctx_timeslice_ns = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_CTX_TIMESLICE_NS);
	js_devdata->cfs_ctx_runtime_init_slices = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_INIT_SLICES);
	js_devdata->cfs_ctx_runtime_min_slices = (u32) kbasep_get_config_value(kbdev, kbdev->config_attributes, KBASE_CONFIG_ATTR_JS_CFS_CTX_RUNTIME_MIN_SLICES);

	dev_dbg(kbdev->dev, "JS Config Attribs: ");
	dev_dbg(kbdev->dev, "\tscheduling_tick_ns:%u", js_devdata->scheduling_tick_ns);
	dev_dbg(kbdev->dev, "\tsoft_stop_ticks:%u", js_devdata->soft_stop_ticks);
	dev_dbg(kbdev->dev, "\tsoft_stop_ticks_cl:%u", js_devdata->soft_stop_ticks_cl);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_ss:%u", js_devdata->hard_stop_ticks_ss);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_cl:%u", js_devdata->hard_stop_ticks_cl);
	dev_dbg(kbdev->dev, "\thard_stop_ticks_nss:%u", js_devdata->hard_stop_ticks_nss);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_ss:%u", js_devdata->gpu_reset_ticks_ss);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_cl:%u", js_devdata->gpu_reset_ticks_cl);
	dev_dbg(kbdev->dev, "\tgpu_reset_ticks_nss:%u", js_devdata->gpu_reset_ticks_nss);
	dev_dbg(kbdev->dev, "\tctx_timeslice_ns:%u", js_devdata->ctx_timeslice_ns);
	dev_dbg(kbdev->dev, "\tcfs_ctx_runtime_init_slices:%u", js_devdata->cfs_ctx_runtime_init_slices);
	dev_dbg(kbdev->dev, "\tcfs_ctx_runtime_min_slices:%u", js_devdata->cfs_ctx_runtime_min_slices);

#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS != 0
	dev_dbg(kbdev->dev, "Job Scheduling Policy Soft-stops disabled, ignoring value for soft_stop_ticks==%u at %uns per tick. Other soft-stops may still occur.", js_devdata->soft_stop_ticks, js_devdata->scheduling_tick_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_HARD_STOPS != 0
	dev_dbg(kbdev->dev, "Job Scheduling Policy Hard-stops disabled, ignoring values for hard_stop_ticks_ss==%d and hard_stop_ticks_nss==%u at %uns per tick. Other hard-stops may still occur.", js_devdata->hard_stop_ticks_ss, js_devdata->hard_stop_ticks_nss, js_devdata->scheduling_tick_ns);
#endif
#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS != 0 && KBASE_DISABLE_SCHEDULING_HARD_STOPS != 0
	dev_dbg(kbdev->dev, "Note: The JS policy's tick timer (if coded) will still be run, but do nothing.");
#endif

	/* setup the number of irq throttle cycles base on given time */
	{
		int irq_throttle_time_us = kbdev->gpu_props.irq_throttle_time_us;
		int irq_throttle_cycles = kbasep_js_convert_us_to_gpu_ticks_max_freq(kbdev, irq_throttle_time_us);
		atomic_set(&kbdev->irq_throttle_cycles, irq_throttle_cycles);
	}

	/* Clear the AS data, including setting NULL pointers */
	memset(&js_devdata->runpool_irq.per_as_data[0], 0, sizeof(js_devdata->runpool_irq.per_as_data));

	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i)
		js_devdata->js_reqs[i] = core_reqs_from_jsn_features(kbdev->gpu_props.props.raw_props.js_features[i]);

	js_devdata->init_status |= JS_DEVDATA_INIT_CONSTANTS;

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above */

	mutex_init(&js_devdata->runpool_mutex);
	mutex_init(&js_devdata->queue_mutex);
	spin_lock_init(&js_devdata->runpool_irq.lock);

	err = kbasep_js_policy_init(kbdev);
	if (err == MALI_ERROR_NONE)
		js_devdata->init_status |= JS_DEVDATA_INIT_POLICY;

	/* On error, do no cleanup; this will be handled by the caller(s), since
	 * we've designed this resource to be safe to terminate on init-fail */
	if (js_devdata->init_status != JS_DEVDATA_INIT_ALL)
		return MALI_ERROR_FUNCTION_FAILED;

	return MALI_ERROR_NONE;
}

void kbasep_js_devdata_halt(kbase_device *kbdev)
{
	CSTD_UNUSED(kbdev);
}

void kbasep_js_devdata_term(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	if ((js_devdata->init_status & JS_DEVDATA_INIT_CONSTANTS)) {
		s8 zero_ctx_attr_ref_count[KBASEP_JS_CTX_ATTR_COUNT] = { 0, };
		/* The caller must de-register all contexts before calling this */
		KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running == 0);
		KBASE_DEBUG_ASSERT(memcmp(js_devdata->runpool_irq.ctx_attr_ref_count, zero_ctx_attr_ref_count, sizeof(js_devdata->runpool_irq.ctx_attr_ref_count)) == 0);
		CSTD_UNUSED(zero_ctx_attr_ref_count);
	}
	if ((js_devdata->init_status & JS_DEVDATA_INIT_POLICY))
		kbasep_js_policy_term(&js_devdata->policy);

	js_devdata->init_status = JS_DEVDATA_INIT_NONE;
}

mali_error kbasep_js_kctx_init(kbase_context * const kctx)
{
	kbase_device *kbdev;
	kbasep_js_kctx_info *js_kctx_info;
	mali_error err;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_kctx_info = &kctx->jctx.sched_info;
	KBASE_DEBUG_ASSERT(js_kctx_info->init_status == JS_KCTX_INIT_NONE);

	js_kctx_info->ctx.nr_jobs = 0;
	js_kctx_info->ctx.is_scheduled = MALI_FALSE;
	js_kctx_info->ctx.is_dying = MALI_FALSE;
	memset(js_kctx_info->ctx.ctx_attr_ref_count, 0, sizeof(js_kctx_info->ctx.ctx_attr_ref_count));

	/* Initially, the context is disabled from submission until the create flags are set */
	js_kctx_info->ctx.flags = KBASE_CTX_FLAG_SUBMIT_DISABLED;

	js_kctx_info->init_status |= JS_KCTX_INIT_CONSTANTS;

	/* On error, we could continue on: providing none of the below resources
	 * rely on the ones above */
	mutex_init(&js_kctx_info->ctx.jsctx_mutex);

	init_waitqueue_head(&js_kctx_info->ctx.is_scheduled_wait);

	err = kbasep_js_policy_init_ctx(kbdev, kctx);
	if (err == MALI_ERROR_NONE)
		js_kctx_info->init_status |= JS_KCTX_INIT_POLICY;

	/* On error, do no cleanup; this will be handled by the caller(s), since
	 * we've designed this resource to be safe to terminate on init-fail */
	if (js_kctx_info->init_status != JS_KCTX_INIT_ALL)
		return MALI_ERROR_FUNCTION_FAILED;

	return MALI_ERROR_NONE;
}

void kbasep_js_kctx_term(kbase_context *kctx)
{
	kbase_device *kbdev;
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_policy *js_policy;

	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	if ((js_kctx_info->init_status & JS_KCTX_INIT_CONSTANTS)) {
		/* The caller must de-register all jobs before calling this */
		KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled == MALI_FALSE);
		KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs == 0);
	}

	if ((js_kctx_info->init_status & JS_KCTX_INIT_POLICY))
		kbasep_js_policy_term_ctx(js_policy, kctx);

	js_kctx_info->init_status = JS_KCTX_INIT_NONE;
}

/* Evict jobs from the NEXT registers
 *
 * The caller must hold:
 * - kbasep_js_kctx_info::ctx::jsctx_mutex
 * - kbasep_js_device_data::runpool_mutex
 */
STATIC void kbasep_js_runpool_evict_next_jobs(kbase_device *kbdev, kbase_context *kctx)
{
	unsigned long flags;
	int js;
	kbasep_js_device_data *js_devdata;

	js_devdata = &kbdev->js_data;

	BUG_ON(!mutex_is_locked(&kctx->jctx.sched_info.ctx.jsctx_mutex));
	BUG_ON(!mutex_is_locked(&js_devdata->runpool_mutex));

	/* Prevent contexts in the runpool from submitting jobs */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	/* There's no need to prevent contexts in the runpool from submitting jobs,
	 * because we complete this operation by the time we release the
	 * runpool_irq.lock */

	/* Evict jobs from the NEXT registers */
	for (js = 0; js < kbdev->gpu_props.num_job_slots; js++) {
		kbase_jm_slot *slot;
		kbase_jd_atom *tail;

		if (!kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), NULL)) {
			/* No job in the NEXT register */
			continue;
		}

		slot = &kbdev->jm_slots[js];
		tail = kbasep_jm_peek_idx_submit_slot(slot, slot->submitted_nr - 1);

		KBASE_TIMELINE_TRY_SOFT_STOP(kctx, js, 1);
		/* Clearing job from next registers */
		kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_COMMAND_NEXT), JSn_COMMAND_NOP, NULL);

		/* Check to see if we did remove a job from the next registers */
		if (kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), NULL) != 0 || kbase_reg_read(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), NULL) != 0) {
			/* The job was successfully cleared from the next registers, requeue it */
			kbase_jd_atom *dequeued_katom = kbasep_jm_dequeue_tail_submit_slot(slot);
			KBASE_DEBUG_ASSERT(dequeued_katom == tail);

			/* Set the next registers to NULL */
			kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_LO), 0, NULL);
			kbase_reg_write(kbdev, JOB_SLOT_REG(js, JSn_HEAD_NEXT_HI), 0, NULL);

			KBASE_TRACE_ADD_SLOT(kbdev, JM_SLOT_EVICT, dequeued_katom->kctx, dequeued_katom, dequeued_katom->jc, js);

			/* Complete the job, indicate that it took no time, and don't start
			 * new atoms */
			kbase_jd_done(dequeued_katom, js, NULL, KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT);
		}
		KBASE_TIMELINE_TRY_SOFT_STOP(kctx, js, 0);
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
}

/**
 * Fast start a higher priority job
 * If the runpool is full, the lower priority contexts with no running jobs
 * will be evicted from the runpool
 *
 * If \a kctx_new is NULL, the first context with no running jobs will be evicted
 *
 * The following locking conditions are made on the caller:
 * - The caller must \b not hold \a kctx_new's
 * kbasep_js_kctx_info::ctx::jsctx_mutex, or that mutex of any ctx in the
 * runpool. This is because \a kctx_new's jsctx_mutex and one of the other
 * scheduled ctx's jsctx_mutex will be obtained internally.
 * - it must \em not hold kbasep_js_device_data::runpool_irq::lock (as this will be
 * obtained internally)
 * - it must \em not hold kbasep_js_device_data::runpool_mutex (as this will be
 * obtained internally)
 * - it must \em not hold kbasep_jd_device_data::queue_mutex (again, it's used
 * internally).
 */
STATIC void kbasep_js_runpool_attempt_fast_start_ctx(kbase_device *kbdev, kbase_context *kctx_new)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_new;
	kbasep_js_policy *js_policy;
	kbasep_js_per_as_data *js_per_as_data;
	int evict_as_nr;
	kbasep_js_atom_retained_state katom_retained_state;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;

	if (kctx_new != NULL) {
		js_kctx_new = &kctx_new->jctx.sched_info;
		mutex_lock(&js_kctx_new->ctx.jsctx_mutex);
	} else {
		js_kctx_new = NULL;
		CSTD_UNUSED(js_kctx_new);
	}

	/* Setup a dummy katom_retained_state */
	kbasep_js_atom_retained_state_init_invalid(&katom_retained_state);

	mutex_lock(&js_devdata->runpool_mutex);

	/* If the runpool is full and either there is no specified context or the specified context is not dying, then
	   attempt to fast start the specified context or evict the first context with no running jobs. */
	if (check_is_runpool_full(kbdev, kctx_new) && 
            (!js_kctx_new || (js_kctx_new && !js_kctx_new->ctx.is_dying))) {
		/* No free address spaces - attempt to evict non-running lower priority context */
		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
		for (evict_as_nr = 0; evict_as_nr < kbdev->nr_hw_address_spaces; evict_as_nr++) {
			kbase_context *kctx_evict;
			js_per_as_data = &js_devdata->runpool_irq.per_as_data[evict_as_nr];
			kctx_evict = js_per_as_data->kctx;

			/* Look for the AS which is not currently running */
			if (0 == js_per_as_data->as_busy_refcount && kctx_evict != NULL) {
				/* Now compare the scheduled priority we are considering evicting with the new ctx priority
				 * and take into consideration if the scheduled priority is a realtime policy or not.
				 * Note that the lower the number, the higher the priority
				 */
				if ((kctx_new == NULL) || kbasep_js_policy_ctx_has_priority(js_policy, kctx_evict, kctx_new)) {
					mali_bool retain_result;
					kbasep_js_release_result release_result;
					KBASE_TRACE_ADD(kbdev, JS_FAST_START_EVICTS_CTX, kctx_evict, NULL, 0u, (uintptr_t)kctx_new);

					/* Retain the ctx to work on it - this shouldn't be able to fail */
					retain_result = kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx_evict);
					KBASE_DEBUG_ASSERT(retain_result != MALI_FALSE);
					CSTD_UNUSED(retain_result);

					/* This will cause the context to be scheduled out on the next runpool_release_ctx(),
					 * and also stop its refcount increasing */
					kbasep_js_clear_submit_allowed(js_devdata, kctx_evict);

					spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
					mutex_unlock(&js_devdata->runpool_mutex);
					if (kctx_new != NULL)
						mutex_unlock(&js_kctx_new->ctx.jsctx_mutex);

					/* Stop working on the target context, start working on the kctx_evict context */

					mutex_lock(&kctx_evict->jctx.sched_info.ctx.jsctx_mutex);
					mutex_lock(&js_devdata->runpool_mutex);
					release_result = kbasep_js_runpool_release_ctx_internal(kbdev, kctx_evict, &katom_retained_state);
					mutex_unlock(&js_devdata->runpool_mutex);
					/* Only requeue if actually descheduled, which is more robust in case
					 * something else retains it (e.g. two high priority contexts racing
					 * to evict the same lower priority context) */
					if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u)
						kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx_evict, MALI_TRUE);

					mutex_unlock(&kctx_evict->jctx.sched_info.ctx.jsctx_mutex);

					/* release_result isn't propogated further:
					 * - the caller will be scheduling in a context anyway
					 * - which will also cause new jobs to run */

					/* ctx fast start has taken place */
					return;
				}
			}
		}
		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
	}

	/* ctx fast start has not taken place */
	mutex_unlock(&js_devdata->runpool_mutex);
	if (kctx_new != NULL)
		mutex_unlock(&js_kctx_new->ctx.jsctx_mutex);
}

mali_bool kbasep_js_add_job(kbase_context *kctx, kbase_jd_atom *atom)
{
	unsigned long flags;
	kbasep_js_kctx_info *js_kctx_info;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy *js_policy;

	mali_bool policy_queue_updated = MALI_FALSE;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);
	lockdep_assert_held(&kctx->jctx.lock);

	kbdev = kctx->kbdev;
	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TIMELINE_ATOM_READY(kctx, kbase_jd_atom_id(kctx, atom));

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	/* Policy-specific initialization of atoms (which cannot fail). Anything that
	 * could've failed must've been done at kbasep_jd_policy_init_job() time. */
	kbasep_js_policy_register_job(js_policy, kctx, atom);

	/*
	 * Begin Runpool transaction
	 */
	mutex_lock(&js_devdata->runpool_mutex);
	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_ADD_JOB, kctx, atom, atom->jc, kbasep_js_trace_get_refcnt(kbdev, kctx));

	/* Refcount ctx.nr_jobs */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs < U32_MAX);
	++(js_kctx_info->ctx.nr_jobs);

	/* Setup any scheduling information */
	kbasep_js_clear_job_retry_submit(atom);

	/* Lock for state available during IRQ */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	/* Context Attribute Refcounting */
	kbasep_js_ctx_attr_ctx_retain_atom(kbdev, kctx, atom);

	/* Enqueue the job in the policy, causing it to be scheduled if the
	 * parent context gets scheduled */
	kbasep_js_policy_enqueue_job(js_policy, atom);

	if (js_kctx_info->ctx.is_scheduled != MALI_FALSE) {
		/* Handle an already running context - try to run the new job, in case it
		 * matches requirements that aren't matched by any other job in the Run
		 * Pool */
		kbasep_js_try_run_next_job_nolock(kbdev);
	}
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
	mutex_unlock(&js_devdata->runpool_mutex);
	/* End runpool transaction */

	if (js_kctx_info->ctx.is_scheduled == MALI_FALSE) {
		if (js_kctx_info->ctx.is_dying) {
			/* A job got added while/after kbase_job_zap_context() was called
			 * on a non-scheduled context (e.g. KDS dependency resolved). Kill
			 * that job by killing the context. */
			kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, MALI_FALSE);
		} else if (js_kctx_info->ctx.nr_jobs == 1) {
			/* Handle Refcount going from 0 to 1: schedule the context on the Policy Queue */
			KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled == MALI_FALSE);
			dev_dbg(kbdev->dev, "JS: Enqueue Context %p", kctx);

			mutex_lock(&js_devdata->queue_mutex);
			kbasep_js_policy_enqueue_ctx(js_policy, kctx);
			mutex_unlock(&js_devdata->queue_mutex);

			/* Policy Queue was updated - caller must try to schedule the head context
			 * We also try to encourage a fast-start from here. */
			policy_queue_updated = MALI_TRUE;
		}
	}
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	/* If the runpool is full and this job has a higher priority than the
	 * non-running job in the runpool - evict it so this higher priority job
	 * starts faster.  Fast-starting requires the jsctx_mutex to be dropped,
	 * because it works on multiple ctxs
	 *
	 * Note: If the context is being killed with kbase_job_zap_context(), then
	 * kctx can't disappear after the jsctx_mutex was dropped. This is because
	 * the caller holds kctx->jctx.lock */
	if (policy_queue_updated)
		kbasep_js_runpool_attempt_fast_start_ctx(kbdev, kctx);

	return policy_queue_updated;
}

void kbasep_js_remove_job(kbase_device *kbdev, kbase_context *kctx, kbase_jd_atom *atom)
{
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy *js_policy;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;
	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_REMOVE_JOB, kctx, atom, atom->jc, kbasep_js_trace_get_refcnt(kbdev, kctx));

	/* De-refcount ctx.nr_jobs */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.nr_jobs > 0);
	--(js_kctx_info->ctx.nr_jobs);

	/* De-register the job from the system */
	kbasep_js_policy_deregister_job(js_policy, kctx, atom);
}

void kbasep_js_remove_cancelled_job(kbase_device *kbdev, kbase_context *kctx, kbase_jd_atom *katom)
{
	unsigned long flags;
	kbasep_js_atom_retained_state katom_retained_state;
	kbasep_js_device_data *js_devdata;
	mali_bool attr_state_changed;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	js_devdata = &kbdev->js_data;

	kbasep_js_atom_retained_state_copy(&katom_retained_state, katom);
	kbasep_js_remove_job(kbdev, kctx, katom);

	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	/* The atom has 'finished' (will not be re-run), so no need to call
	 * kbasep_js_has_atom_finished().
	 *
	 * This is because it returns MALI_FALSE for soft-stopped atoms, but we
	 * want to override that, because we're cancelling an atom regardless of
	 * whether it was soft-stopped or not */
	attr_state_changed = kbasep_js_ctx_attr_ctx_release_atom(kbdev, kctx, &katom_retained_state);

	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	if (attr_state_changed != MALI_FALSE) {
		/* A change in runpool ctx attributes might mean we can run more jobs
		 * than before. */
		kbase_js_try_run_jobs(kbdev);
	}
}

mali_bool kbasep_js_runpool_retain_ctx(kbase_device *kbdev, kbase_context *kctx)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;
	mali_bool result;
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;

	/* KBASE_TRACE_ADD_REFCOUNT( kbdev, JS_RETAIN_CTX, kctx, NULL, 0,
	   kbasep_js_trace_get_refcnt(kbdev, kctx)); */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	result = kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	return result;
}

kbase_context *kbasep_js_runpool_lookup_ctx(kbase_device *kbdev, int as_nr)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;
	kbase_context *found_kctx = NULL;
	kbasep_js_per_as_data *js_per_as_data;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(0 <= as_nr && as_nr < BASE_MAX_NR_AS);
	js_devdata = &kbdev->js_data;
	js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	found_kctx = js_per_as_data->kctx;

	if (found_kctx != NULL)
		++(js_per_as_data->as_busy_refcount);

	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	return found_kctx;
}

/**
 * @brief Try running more jobs after releasing a context and/or atom
 *
 * This collates a set of actions that must happen whilst
 * kbasep_js_device_data::runpool_irq::lock is held.
 *
 * This includes running more jobs when:
 * - The previously released kctx caused a ctx attribute change
 * - The released atom caused a ctx attribute change
 * - Slots were previously blocked due to affinity restrictions
 * - Submission during IRQ handling failed
 */
STATIC void kbasep_js_run_jobs_after_ctx_and_atom_release(kbase_device *kbdev, kbase_context *kctx, kbasep_js_atom_retained_state *katom_retained_state, mali_bool runpool_ctx_attr_change)
{
	kbasep_js_device_data *js_devdata;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(katom_retained_state != NULL);
	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	if (js_devdata->nr_user_contexts_running != 0) {
		mali_bool retry_submit;
		int retry_jobslot;

		retry_submit = kbasep_js_get_atom_retry_submit_slot(katom_retained_state, &retry_jobslot);

		if (runpool_ctx_attr_change != MALI_FALSE) {
			/* A change in runpool ctx attributes might mean we can run more jobs
			 * than before  */
			kbasep_js_try_run_next_job_nolock(kbdev);

			/* A retry submit on all slots has now happened, so don't need to do it again */
			retry_submit = MALI_FALSE;
		}

		/* Submit on any slots that might've had atoms blocked by the affinity of
		 * a completed atom.
		 *
		 * If no atom has recently completed, then this is harmelss */
		kbase_js_affinity_submit_to_blocked_slots(kbdev);

		/* If the IRQ handler failed to get a job from the policy, try again from
		 * outside the IRQ handler
		 * NOTE: We may've already cleared retry_submit from submitting above */
		if (retry_submit != MALI_FALSE) {
			KBASE_TRACE_ADD_SLOT(kbdev, JD_DONE_TRY_RUN_NEXT_JOB, kctx, NULL, 0u, retry_jobslot);
			kbasep_js_try_run_next_job_on_slot_nolock(kbdev, retry_jobslot);
		}
	}
}

/**
 * Internal function to release the reference on a ctx and an atom's "retained
 * state", only taking the runpool and as transaction mutexes
 *
 * This also starts more jobs running in the case of an ctx-attribute state change
 *
 * This does none of the followup actions for scheduling:
 * - It does not schedule in a new context
 * - It does not requeue or handle dying contexts
 *
 * For those tasks, just call kbasep_js_runpool_release_ctx() instead
 *
 * Requires:
 * - Context is scheduled in, and kctx->as_nr matches kctx_as_nr
 * - Context has a non-zero refcount
 * - Caller holds js_kctx_info->ctx.jsctx_mutex
 * - Caller holds js_devdata->runpool_mutex
 */
STATIC kbasep_js_release_result kbasep_js_runpool_release_ctx_internal(kbase_device *kbdev, kbase_context *kctx, kbasep_js_atom_retained_state *katom_retained_state)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_policy *js_policy;
	kbasep_js_per_as_data *js_per_as_data;

	kbasep_js_release_result release_result = 0u;
	mali_bool runpool_ctx_attr_change = MALI_FALSE;
	int kctx_as_nr;
	kbase_as *current_as;
	int new_ref_count;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;

	/* Ensure context really is scheduled in */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled != MALI_FALSE);

	/* kctx->as_nr and js_per_as_data are only read from here. The caller's
	 * js_ctx_mutex provides a barrier that ensures they are up-to-date.
	 *
	 * They will not change whilst we're reading them, because the refcount
	 * is non-zero (and we ASSERT on that last fact).
	 */
	kctx_as_nr = kctx->as_nr;
	KBASE_DEBUG_ASSERT(kctx_as_nr != KBASEP_AS_NR_INVALID);
	js_per_as_data = &js_devdata->runpool_irq.per_as_data[kctx_as_nr];
	KBASE_DEBUG_ASSERT(js_per_as_data->as_busy_refcount > 0);

	/*
	 * Transaction begins on AS and runpool_irq
	 *
	 * Assert about out calling contract
	 */
	current_as = &kbdev->as[kctx_as_nr];
	mutex_lock(&current_as->transaction_mutex);
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	KBASE_DEBUG_ASSERT(kctx_as_nr == kctx->as_nr);
	KBASE_DEBUG_ASSERT(js_per_as_data->as_busy_refcount > 0);

	/* Update refcount */
	new_ref_count = --(js_per_as_data->as_busy_refcount);

	/* Release the atom if it finished (i.e. wasn't soft-stopped) */
	if (kbasep_js_has_atom_finished(katom_retained_state) != MALI_FALSE)
		runpool_ctx_attr_change |= kbasep_js_ctx_attr_ctx_release_atom(kbdev, kctx, katom_retained_state);

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_RELEASE_CTX, kctx, NULL, 0u, new_ref_count);

	if (new_ref_count == 1 && kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_PRIVILEGED
		&& !kbase_pm_is_suspending(kbdev) ) {
		/* Context is kept scheduled into an address space even when there are no jobs, in this case we have
		 * to handle the situation where all jobs have been evicted from the GPU and submission is disabled.
		 *
		 * At this point we re-enable submission to allow further jobs to be executed
		 */
		kbasep_js_set_submit_allowed(js_devdata, kctx);
	}

	/* Make a set of checks to see if the context should be scheduled out */
	if (new_ref_count == 0 && (kctx->jctx.sched_info.ctx.nr_jobs == 0 || kbasep_js_is_submit_allowed(js_devdata, kctx) == MALI_FALSE)) {
		/* Last reference, and we've been told to remove this context from the Run Pool */
		dev_dbg(kbdev->dev, "JS: RunPool Remove Context %p because as_busy_refcount=%d, jobs=%d, allowed=%d", kctx, new_ref_count, js_kctx_info->ctx.nr_jobs, kbasep_js_is_submit_allowed(js_devdata, kctx));

		kbasep_js_policy_runpool_remove_ctx(js_policy, kctx);

		/* Stop any more refcounts occuring on the context */
		js_per_as_data->kctx = NULL;

		/* Ensure we prevent the context from submitting any new jobs
		 * e.g. from kbasep_js_try_run_next_job_on_slot_irq_nolock()  */
		kbasep_js_clear_submit_allowed(js_devdata, kctx);

		/* Disable the MMU on the affected address space, and indicate it's invalid */
		kbase_mmu_disable(kctx);

#ifdef CONFIG_MALI_GATOR_SUPPORT
		kbase_trace_mali_mmu_as_released(kctx->as_nr);
#endif				/* CONFIG_MALI_GATOR_SUPPORT */

		kctx->as_nr = KBASEP_AS_NR_INVALID;

		/* Ctx Attribute handling
		 *
		 * Releasing atoms attributes must either happen before this, or after
		 * 'is_scheduled' is changed, otherwise we double-decount the attributes*/
		runpool_ctx_attr_change |= kbasep_js_ctx_attr_runpool_release_ctx(kbdev, kctx);

		/* Early update of context count, to optimize the
		 * kbasep_js_run_jobs_after_ctx_and_atom_release() call */
		runpool_dec_context_count(kbdev, kctx);

		/* Releasing the context and katom retained state can allow more jobs to run */
		kbasep_js_run_jobs_after_ctx_and_atom_release(kbdev, kctx, katom_retained_state, runpool_ctx_attr_change);

		/*
		 * Transaction ends on AS and runpool_irq:
		 *
		 * By this point, the AS-related data is now clear and ready for re-use.
		 *
		 * Since releases only occur once for each previous successful retain, and no more
		 * retains are allowed on this context, no other thread will be operating in this
		 * code whilst we are
		 */
		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
		mutex_unlock(&current_as->transaction_mutex);

		/* Free up the address space */
		release_addr_space(kbdev, kctx_as_nr);
		/* Note: Don't reuse kctx_as_nr now */

		/* Synchronize with any policy timers */
		kbasep_js_policy_runpool_timers_sync(js_policy);

		/* update book-keeping info */
		js_kctx_info->ctx.is_scheduled = MALI_FALSE;
		/* Signal any waiter that the context is not scheduled, so is safe for
		 * termination - once the jsctx_mutex is also dropped, and jobs have
		 * finished. */
		wake_up(&js_kctx_info->ctx.is_scheduled_wait);

		/* Queue an action to occur after we've dropped the lock */
		release_result |= KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED;

	} else {
		kbasep_js_run_jobs_after_ctx_and_atom_release(kbdev, kctx, katom_retained_state, runpool_ctx_attr_change);

		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
		mutex_unlock(&current_as->transaction_mutex);
	}

	return release_result;
}

void kbasep_js_runpool_requeue_or_kill_ctx(kbase_device *kbdev, kbase_context *kctx, mali_bool has_pm_ref)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy *js_policy;
	kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_policy = &kbdev->js_data.policy;
	js_devdata = &kbdev->js_data;

	/* This is called if and only if you've you've detached the context from
	 * the Runpool or the Policy Queue, and not added it back to the Runpool */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled == MALI_FALSE);

	if (js_kctx_info->ctx.is_dying != MALI_FALSE) {
		/* Dying: don't requeue, but kill all jobs on the context. This happens
		 * asynchronously */
		dev_dbg(kbdev->dev, "JS: ** Killing Context %p on RunPool Remove **", kctx);
		kbasep_js_policy_foreach_ctx_job(js_policy, kctx, &kbase_jd_cancel, MALI_TRUE);
	} else if (js_kctx_info->ctx.nr_jobs > 0) {
		/* Not dying, has jobs: de-ref core counts from each job before addding
		 * back to the queue */
		kbasep_js_policy_foreach_ctx_job(js_policy, kctx, &kbasep_js_job_check_deref_cores, MALI_FALSE);

		dev_dbg(kbdev->dev, "JS: Requeue Context %p", kctx);
		mutex_lock(&js_devdata->queue_mutex);
		kbasep_js_policy_enqueue_ctx(js_policy, kctx);
		mutex_unlock(&js_devdata->queue_mutex);
	} else {
		/* Not dying, no jobs: don't add back to the queue */
		dev_dbg(kbdev->dev, "JS: Idling Context %p (not requeued)", kctx);
	}

	if (has_pm_ref) {
		/* In all cases where we had a pm active refcount, release it */
		kbase_pm_context_idle(kbdev);
	}
}

void kbasep_js_runpool_release_ctx_and_katom_retained_state(kbase_device *kbdev, kbase_context *kctx, kbasep_js_atom_retained_state *katom_retained_state)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_release_result release_result;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);
	release_result = kbasep_js_runpool_release_ctx_internal(kbdev, kctx, katom_retained_state);

	/* Drop the runpool mutex to allow requeing kctx */
	mutex_unlock(&js_devdata->runpool_mutex);
	if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u)
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, MALI_TRUE);

	/* Drop the jsctx_mutex to allow scheduling in a new context */
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u) {
		/* We've freed up an address space, so let's try to schedule in another
		 * context
		 *
		 * Note: if there's a context to schedule in, then it also tries to run
		 * another job, in case the new context has jobs satisfying requirements
		 * that no other context/job in the runpool does */
		kbasep_js_try_schedule_head_ctx(kbdev);
	}
}

void kbasep_js_runpool_release_ctx(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_atom_retained_state katom_retained_state;

	kbasep_js_atom_retained_state_init_invalid(&katom_retained_state);

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx, &katom_retained_state);
}

/** Variant of kbasep_js_runpool_release_ctx() that doesn't call into
 * kbasep_js_try_schedule_head_ctx() */
STATIC void kbasep_js_runpool_release_ctx_no_schedule(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_release_result release_result;
	kbasep_js_atom_retained_state katom_retained_state_struct;
	kbasep_js_atom_retained_state *katom_retained_state = &katom_retained_state_struct;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;
	kbasep_js_atom_retained_state_init_invalid(katom_retained_state);

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);
	release_result = kbasep_js_runpool_release_ctx_internal(kbdev, kctx, katom_retained_state);

	/* Drop the runpool mutex to allow requeing kctx */
	mutex_unlock(&js_devdata->runpool_mutex);
	if ((release_result & KBASEP_JS_RELEASE_RESULT_WAS_DESCHEDULED) != 0u)
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, kctx, MALI_TRUE);

	/* Drop the jsctx_mutex to allow scheduling in a new context */
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	/* NOTE: could return release_result if the caller would like to know
	 * whether it should schedule a new context, but currently no callers do */
}


/**
 * @brief Handle retaining cores for power management and affinity management,
 * ensuring that cores are powered up and won't violate affinity restrictions.
 *
 * This function enters at the following @ref kbase_atom_coreref_state states:
 *
 * - NO_CORES_REQUESTED,
 * - WAITING_FOR_REQUESTED_CORES,
 * - RECHECK_AFFINITY,
 *
 * The transitions are as folows:
 * - NO_CORES_REQUESTED -> WAITING_FOR_REQUESTED_CORES
 * - WAITING_FOR_REQUESTED_CORES -> ( WAITING_FOR_REQUESTED_CORES or RECHECK_AFFINITY )
 * - RECHECK_AFFINITY -> ( WAITING_FOR_REQUESTED_CORES or CHECK_AFFINITY_VIOLATIONS )
 * - CHECK_AFFINITY_VIOLATIONS -> ( RECHECK_AFFINITY or READY )
 *
 * The caller must hold:
 * - kbasep_js_device_data::runpool_irq::lock
 *
 * @return MALI_FALSE when the function makes a transition to the same or lower state, indicating
 * that the cores are not ready.
 * @return MALI_TRUE once READY state is reached, indicating that the cores are 'ready' and won't
 * violate affinity restrictions.
 *
 */
STATIC mali_bool kbasep_js_job_check_ref_cores(kbase_device *kbdev, int js, kbase_jd_atom *katom)
{
	/* The most recently checked affinity. Having this at this scope allows us
	 * to guarantee that we've checked the affinity in this function call. */
	u64 recently_chosen_affinity = 0;
	mali_bool chosen_affinity = MALI_FALSE;
	mali_bool retry;

	do {
		retry = MALI_FALSE;

		/* NOTE: The following uses a number of FALLTHROUGHs to optimize the
		 * calls to this function. Ending of the function is indicated by BREAK OUT */
		switch (katom->coreref_state) {
			/* State when job is first attempted to be run */
		case KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED:
			KBASE_DEBUG_ASSERT(katom->affinity == 0);

			/* Compute affinity */
			if (MALI_FALSE == kbase_js_choose_affinity(&recently_chosen_affinity, kbdev, katom, js)) {
				/* No cores are currently available */
				/* *** BREAK OUT: No state transition *** */
				break;
			}		

			chosen_affinity = MALI_TRUE;

			/* Request the cores */
			kbase_pm_request_cores(kbdev, katom->core_req & BASE_JD_REQ_T, recently_chosen_affinity);

			katom->affinity = recently_chosen_affinity;

			/* Proceed to next state */
			katom->coreref_state = KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES;

			/* ***FALLTHROUGH: TRANSITION TO HIGHER STATE*** */

		case KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES:
			{
				kbase_pm_cores_ready cores_ready;
				KBASE_DEBUG_ASSERT(katom->affinity != 0 || (katom->core_req & BASE_JD_REQ_T));

				cores_ready = kbase_pm_register_inuse_cores(kbdev, katom->core_req & BASE_JD_REQ_T, katom->affinity);
				if (cores_ready == KBASE_NEW_AFFINITY) {
					/* Affinity no longer valid - return to previous state */
					kbasep_js_job_check_deref_cores(kbdev, katom);
					KBASE_TRACE_ADD_SLOT_INFO(kbdev, JS_CORE_REF_REGISTER_INUSE_FAILED, katom->kctx, katom, katom->jc, js, (u32) katom->affinity);
					/* *** BREAK OUT: Return to previous state, retry *** */
					retry = MALI_TRUE;
					break;
				}
				if (cores_ready == KBASE_CORES_NOT_READY) {
					/* Stay in this state and return, to retry at this state later */
					KBASE_TRACE_ADD_SLOT_INFO(kbdev, JS_CORE_REF_REGISTER_INUSE_FAILED, katom->kctx, katom, katom->jc, js, (u32) katom->affinity);
					/* *** BREAK OUT: No state transition *** */
					break;
				}
				/* Proceed to next state */
				katom->coreref_state = KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY;
			}

			/* ***FALLTHROUGH: TRANSITION TO HIGHER STATE*** */

		case KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY:
			KBASE_DEBUG_ASSERT(katom->affinity != 0 || (katom->core_req & BASE_JD_REQ_T));

			/* Optimize out choosing the affinity twice in the same function call */
			if (chosen_affinity == MALI_FALSE) {
				/* See if the affinity changed since a previous call. */
				if (MALI_FALSE == kbase_js_choose_affinity(&recently_chosen_affinity, kbdev, katom, js)) {
					/* No cores are currently available */
					kbasep_js_job_check_deref_cores(kbdev, katom);
					KBASE_TRACE_ADD_SLOT_INFO(kbdev, JS_CORE_REF_REQUEST_ON_RECHECK_FAILED, katom->kctx, katom, katom->jc, js, (u32) recently_chosen_affinity);
					/* *** BREAK OUT: Transition to lower state *** */
					break;
				}		
				chosen_affinity = MALI_TRUE;
			}

			/* Now see if this requires a different set of cores */
			if (recently_chosen_affinity != katom->affinity) {
				kbase_pm_cores_ready cores_ready;

				kbase_pm_request_cores(kbdev, katom->core_req & BASE_JD_REQ_T, recently_chosen_affinity);

				/* Register new cores whilst we still hold the old ones, to minimize power transitions */
				cores_ready = kbase_pm_register_inuse_cores(kbdev, katom->core_req & BASE_JD_REQ_T, recently_chosen_affinity);
				kbasep_js_job_check_deref_cores(kbdev, katom);

				/* Fixup the state that was reduced by deref_cores: */
				katom->coreref_state = KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY;
				katom->affinity = recently_chosen_affinity;
				if (cores_ready == KBASE_NEW_AFFINITY) {
					/* Affinity no longer valid - return to previous state */
					katom->coreref_state = KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES;
					kbasep_js_job_check_deref_cores(kbdev, katom);
					KBASE_TRACE_ADD_SLOT_INFO(kbdev, JS_CORE_REF_REGISTER_INUSE_FAILED, katom->kctx, katom, katom->jc, js, (u32) katom->affinity);
					/* *** BREAK OUT: Return to previous state, retry *** */
					retry = MALI_TRUE;
					break;
				}
				/* Now might be waiting for powerup again, with a new affinity */
				if (cores_ready == KBASE_CORES_NOT_READY) {
					/* Return to previous state */
					katom->coreref_state = KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES;
					KBASE_TRACE_ADD_SLOT_INFO(kbdev, JS_CORE_REF_REGISTER_ON_RECHECK_FAILED, katom->kctx, katom, katom->jc, js, (u32) katom->affinity);
					/* *** BREAK OUT: Transition to lower state *** */
					break;
				}
			}
			/* Proceed to next state */
			katom->coreref_state = KBASE_ATOM_COREREF_STATE_CHECK_AFFINITY_VIOLATIONS;

			/* ***FALLTHROUGH: TRANSITION TO HIGHER STATE*** */
		case KBASE_ATOM_COREREF_STATE_CHECK_AFFINITY_VIOLATIONS:
			KBASE_DEBUG_ASSERT(katom->affinity != 0 || (katom->core_req & BASE_JD_REQ_T));
			KBASE_DEBUG_ASSERT(katom->affinity == recently_chosen_affinity);

			/* Note: this is where the caller must've taken the runpool_irq.lock */

			/* Check for affinity violations - if there are any, then we just ask
			 * the caller to requeue and try again later */
			if (kbase_js_affinity_would_violate(kbdev, js, katom->affinity) != MALI_FALSE) {
				/* Cause a re-attempt to submit from this slot on the next job complete */
				kbase_js_affinity_slot_blocked_an_atom(kbdev, js);
				/* Return to previous state */
				katom->coreref_state = KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY;
				/* *** BREAK OUT: Transition to lower state *** */
				KBASE_TRACE_ADD_SLOT_INFO(kbdev, JS_CORE_REF_AFFINITY_WOULD_VIOLATE, katom->kctx, katom, katom->jc, js, (u32) katom->affinity);
				break;
			}

			/* No affinity violations would result, so the cores are ready */
			katom->coreref_state = KBASE_ATOM_COREREF_STATE_READY;
			/* *** BREAK OUT: Cores Ready *** */
			break;

		default:
			KBASE_DEBUG_ASSERT_MSG(MALI_FALSE, "Unhandled kbase_atom_coreref_state %d", katom->coreref_state);
			break;
		}
	} while (retry != MALI_FALSE);

	return (katom->coreref_state == KBASE_ATOM_COREREF_STATE_READY);
}

void kbasep_js_job_check_deref_cores(kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);

	switch (katom->coreref_state) {
	case KBASE_ATOM_COREREF_STATE_READY:
		/* State where atom was submitted to the HW - just proceed to power-down */
		KBASE_DEBUG_ASSERT(katom->affinity != 0 || (katom->core_req & BASE_JD_REQ_T));

		/* *** FALLTHROUGH *** */

	case KBASE_ATOM_COREREF_STATE_RECHECK_AFFINITY:
		/* State where cores were registered */
		KBASE_DEBUG_ASSERT(katom->affinity != 0 || (katom->core_req & BASE_JD_REQ_T));
		kbase_pm_release_cores(kbdev, katom->core_req & BASE_JD_REQ_T, katom->affinity);

		/* Note: We do not clear the state for kbase_js_affinity_slot_blocked_an_atom().
		 * That is handled after finishing the job. This might be slightly
		 * suboptimal for some corner cases, but is otherwise not a problem
		 * (and resolves itself after the next job completes). */

		break;

	case KBASE_ATOM_COREREF_STATE_WAITING_FOR_REQUESTED_CORES:
		/* State where cores were requested, but not registered */
		KBASE_DEBUG_ASSERT(katom->affinity != 0 || (katom->core_req & BASE_JD_REQ_T));
		kbase_pm_unrequest_cores(kbdev, katom->core_req & BASE_JD_REQ_T, katom->affinity);
		break;

	case KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED:
		/* Initial state - nothing required */
		KBASE_DEBUG_ASSERT(katom->affinity == 0);
		break;

	default:
		KBASE_DEBUG_ASSERT_MSG(MALI_FALSE, "Unhandled coreref_state: %d", katom->coreref_state);
		break;
	}

	katom->affinity = 0;
	katom->coreref_state = KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED;
}

/*
 * Note: this function is quite similar to kbasep_js_try_run_next_job_on_slot()
 */
mali_bool kbasep_js_try_run_next_job_on_slot_irq_nolock(kbase_device *kbdev, int js, s8 *submit_count)
{
	kbasep_js_device_data *js_devdata;
	mali_bool cores_ready;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	/* The caller of this function may not be aware of Ctx Attribute state changes so we
	 * must recheck if the given slot is still valid. Otherwise do not try to run.
	 */
	if (kbase_js_can_run_job_on_slot_no_lock(kbdev, js)) {
		/* Keep submitting while there's space to run a job on this job-slot,
		 * and there are jobs to get that match its requirements (see 'break'
		 * statement below) */
		while (*submit_count < KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ && kbasep_jm_is_submit_slots_free(kbdev, js, NULL) != MALI_FALSE) {
			kbase_jd_atom *dequeued_atom;
			mali_bool has_job = MALI_FALSE;

			/* Dequeue a job that matches the requirements */
			has_job = kbasep_js_policy_dequeue_job(kbdev, js, &dequeued_atom);

			if (has_job != MALI_FALSE) {
				/* NOTE: since the runpool_irq lock is currently held and acts across
				 * all address spaces, any context whose busy refcount has reached
				 * zero won't yet be scheduled out whilst we're trying to run jobs
				 * from it */
				kbase_context *parent_ctx = dequeued_atom->kctx;
				mali_bool retain_success;

				/* Retain/power up the cores it needs, check if cores are ready */
				cores_ready = kbasep_js_job_check_ref_cores(kbdev, js, dequeued_atom);

				if (cores_ready != MALI_TRUE && dequeued_atom->event_code != BASE_JD_EVENT_PM_EVENT) {
					/* The job can't be submitted until the cores are ready, requeue the job */
					kbasep_js_policy_enqueue_job(&kbdev->js_data.policy, dequeued_atom);
					break;
				}

				/* ASSERT that the Policy picked a job from an allowed context */
				KBASE_DEBUG_ASSERT(kbasep_js_is_submit_allowed(js_devdata, parent_ctx));

				/* Retain the context to stop it from being scheduled out
				 * This is released when the job finishes */
				retain_success = kbasep_js_runpool_retain_ctx_nolock(kbdev, parent_ctx);
				KBASE_DEBUG_ASSERT(retain_success != MALI_FALSE);
				CSTD_UNUSED(retain_success);

				/* Retain the affinity on the slot */
				kbase_js_affinity_retain_slot_cores(kbdev, js, dequeued_atom->affinity);

				/* Check if this job needs the cycle counter enabled before submission */
				kbasep_js_ref_permon_check_and_enable_cycle_counter(kbdev, dequeued_atom);

				if (dequeued_atom->event_code == BASE_JD_EVENT_PM_EVENT) {
					dev_warn(kbdev->dev, "Rejecting atom due to BASE_JD_EVENT_PM_EVENT\n");
					/* The job has failed due to the specified core group being unavailable */
					kbase_jd_done(dequeued_atom, js, NULL, 0);
				} else {
					/* Submit the job */
					kbase_job_submit_nolock(kbdev, dequeued_atom, js);

					++(*submit_count);
				}
			} else {
				/* No more jobs - stop submitting for this slot */
				break;
			}
		}
	}

	/* Indicate whether a retry in submission should be tried on a different
	 * dequeue function. These are the reasons why it *must* happen:
	 * - the KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ threshold was reached
	 *   and new scheduling must be performed outside of IRQ mode.
	 *
	 * Failure to indicate this correctly could stop further jobs being processed.
	 *
	 * However, we do not _need_ to indicate a retry for the following:
	 * - kbasep_js_policy_dequeue_job() couldn't get a job. In which case,
	 *   there's no point re-trying outside of IRQ, because the result will be
	 *   the same until job dependencies are resolved, or user-space provides
	 *   more jobs. In both those cases, we try to run jobs anyway, so
	 *   processing does not stop.
	 * - kbasep_jm_is_submit_slots_free() was MALI_FALSE, indicating jobs were
	 *   already running. When those jobs complete, that will still cause events
	 *   that cause us to resume job submission.
	 * - kbase_js_can_run_job_on_slot_no_lock() was MALI_FALSE - this is for
	 *   Ctx Attribute handling. That _can_ change outside of IRQ context, but
	 *   is handled explicitly by kbasep_js_runpool_release_ctx_and_katom_retained_state().
	 */
	return (mali_bool) (*submit_count >= KBASE_JS_MAX_JOB_SUBMIT_PER_SLOT_PER_IRQ);
}

void kbasep_js_try_run_next_job_on_slot_nolock(kbase_device *kbdev, int js)
{
	kbasep_js_device_data *js_devdata;
	mali_bool has_job;
	mali_bool cores_ready;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	KBASE_DEBUG_ASSERT(js_devdata->nr_user_contexts_running > 0);

	/* Keep submitting while there's space to run a job on this job-slot,
	 * and there are jobs to get that match its requirements (see 'break'
	 * statement below) */
	if (kbasep_jm_is_submit_slots_free(kbdev, js, NULL) != MALI_FALSE) {
		/* The caller of this function may not be aware of Ctx Attribute state changes so we
		 * must recheck if the given slot is still valid. Otherwise do not try to run.
		 */
		if (kbase_js_can_run_job_on_slot_no_lock(kbdev, js)) {
			do {
				kbase_jd_atom *dequeued_atom;

				/* Dequeue a job that matches the requirements */
				has_job = kbasep_js_policy_dequeue_job(kbdev, js, &dequeued_atom);

				if (has_job != MALI_FALSE) {
					/* NOTE: since the runpool_irq lock is currently held and acts across
					 * all address spaces, any context whose busy refcount has reached
					 * zero won't yet be scheduled out whilst we're trying to run jobs
					 * from it */
					kbase_context *parent_ctx = dequeued_atom->kctx;
					mali_bool retain_success;

					/* Retain/power up the cores it needs, check if cores are ready */
					cores_ready = kbasep_js_job_check_ref_cores(kbdev, js, dequeued_atom);

					if (cores_ready != MALI_TRUE && dequeued_atom->event_code != BASE_JD_EVENT_PM_EVENT) {
						/* The job can't be submitted until the cores are ready, requeue the job */
						kbasep_js_policy_enqueue_job(&kbdev->js_data.policy, dequeued_atom);
						break;
					}
					/* ASSERT that the Policy picked a job from an allowed context */
					KBASE_DEBUG_ASSERT(kbasep_js_is_submit_allowed(js_devdata, parent_ctx));

					/* Retain the context to stop it from being scheduled out
					 * This is released when the job finishes */
					retain_success = kbasep_js_runpool_retain_ctx_nolock(kbdev, parent_ctx);
					KBASE_DEBUG_ASSERT(retain_success != MALI_FALSE);
					CSTD_UNUSED(retain_success);

					/* Retain the affinity on the slot */
					kbase_js_affinity_retain_slot_cores(kbdev, js, dequeued_atom->affinity);

					/* Check if this job needs the cycle counter enabled before submission */
					kbasep_js_ref_permon_check_and_enable_cycle_counter(kbdev, dequeued_atom);

					if (dequeued_atom->event_code == BASE_JD_EVENT_PM_EVENT) {
						dev_warn(kbdev->dev, "Rejecting atom due to BASE_JD_EVENT_PM_EVENT\n");
						/* The job has failed due to the specified core group being unavailable */
						kbase_jd_done(dequeued_atom, js, NULL, 0);
					} else {
						/* Submit the job */
						kbase_job_submit_nolock(kbdev, dequeued_atom, js);
					}
				}

			} while (kbasep_jm_is_submit_slots_free(kbdev, js, NULL) != MALI_FALSE && has_job != MALI_FALSE);
		}
	}
}

void kbasep_js_try_schedule_head_ctx(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	mali_bool has_kctx;
	kbase_context *head_kctx;
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool is_runpool_full;
	kbase_as *new_address_space;
	unsigned long flags;
	mali_bool head_kctx_suspended = MALI_FALSE;
	int pm_active_err;

	KBASE_DEBUG_ASSERT(kbdev != NULL);

	js_devdata = &kbdev->js_data;

	/* We *don't* make a speculative check on whether we can fit a context in the
	 * runpool, because most of our use-cases assume 2 or fewer contexts, and
	 * so we will usually have enough address spaces free.
	 *
	 * In any case, the check will be done later on once we have a context */

	/* Grab the context off head of queue - if there is one */
	mutex_lock(&js_devdata->queue_mutex);
	has_kctx = kbasep_js_policy_dequeue_head_ctx(&js_devdata->policy, &head_kctx);
	mutex_unlock(&js_devdata->queue_mutex);

	if (has_kctx == MALI_FALSE) {
		/* No ctxs to run - nothing to do */
		return;
	}
	js_kctx_info = &head_kctx->jctx.sched_info;

	dev_dbg(kbdev->dev, "JS: Dequeue Context %p", head_kctx);

	pm_active_err = kbase_pm_context_active_handle_suspend(kbdev, KBASE_PM_SUSPEND_HANDLER_DONT_INCREASE);

	/*
	 * Atomic transaction on the Context and Run Pool begins
	 */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	/* Check to see if we shouldn't add the context to run Run Pool:
	 * - it can't take the specified context, and so is 'full'. This may be
	 * 'full' even when there are addres spaces available, since some contexts
	 * are allowed in whereas others may not due to HW workarounds
	 * - A suspend is taking place
	 * - The context is dying due to kbase_job_zap_context() */
	is_runpool_full = check_is_runpool_full(kbdev, head_kctx);
	if (is_runpool_full || pm_active_err || js_kctx_info->ctx.is_dying) {
		/* Roll back the transaction so far and return */
		mutex_unlock(&js_devdata->runpool_mutex);

		/* Note: If a Power Management active reference was taken, it's released by
		 * this: */
		kbasep_js_runpool_requeue_or_kill_ctx(kbdev, head_kctx, !pm_active_err);

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		return;
	}

	/* From the point on, the Power Management active reference is released
	 * only if kbasep_js_runpool_release_ctx() causes the context to be removed
	 * from the runpool */

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_TRY_SCHEDULE_HEAD_CTX, head_kctx, NULL, 0u, kbasep_js_trace_get_refcnt(kbdev, head_kctx));

#if MALI_CUSTOMER_RELEASE == 0
	if (js_devdata->nr_user_contexts_running == 0) {
		/* Only when there are no other contexts submitting jobs:
		 * Latch in run-time job scheduler timeouts that were set through js_timeouts sysfs file */
		if (kbdev->js_soft_stop_ticks != 0)
			js_devdata->soft_stop_ticks = kbdev->js_soft_stop_ticks;

		if (kbdev->js_soft_stop_ticks_cl != 0)
			js_devdata->soft_stop_ticks_cl = kbdev->js_soft_stop_ticks_cl;

		if (kbdev->js_hard_stop_ticks_ss != 0)
			js_devdata->hard_stop_ticks_ss = kbdev->js_hard_stop_ticks_ss;

		if (kbdev->js_hard_stop_ticks_cl != 0)
			js_devdata->hard_stop_ticks_cl = kbdev->js_hard_stop_ticks_cl;

		if (kbdev->js_hard_stop_ticks_nss != 0)
			js_devdata->hard_stop_ticks_nss = kbdev->js_hard_stop_ticks_nss;

		if (kbdev->js_reset_ticks_ss != 0)
			js_devdata->gpu_reset_ticks_ss = kbdev->js_reset_ticks_ss;

		if (kbdev->js_reset_ticks_cl != 0)
			js_devdata->gpu_reset_ticks_cl = kbdev->js_reset_ticks_cl;

		if (kbdev->js_reset_ticks_nss != 0)
			js_devdata->gpu_reset_ticks_nss = kbdev->js_reset_ticks_nss;
	}
#endif

	runpool_inc_context_count(kbdev, head_kctx);
	/* Cause any future waiter-on-termination to wait until the context is
	 * descheduled */
	js_kctx_info->ctx.is_scheduled = MALI_TRUE;
	wake_up(&js_kctx_info->ctx.is_scheduled_wait);

	/* Pick the free address space (guaranteed free by check_is_runpool_full() ) */
	new_address_space = pick_free_addr_space(kbdev);

	/* Lock the address space whilst working on it */
	mutex_lock(&new_address_space->transaction_mutex);
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	/* Do all the necessaries to assign the address space (inc. update book-keeping info)
	 * Add the context to the Run Pool, and allow it to run jobs */
	assign_and_activate_kctx_addr_space(kbdev, head_kctx, new_address_space);

	/* NOTE: If Linux allows, then we can drop the new_address_space->transaction mutex here */

	if ((js_kctx_info->ctx.flags & KBASE_CTX_FLAG_PRIVILEGED) != 0) {
		/* We need to retain it to keep the corresponding address space */
		kbasep_js_runpool_retain_ctx_nolock(kbdev, head_kctx);
	}

	/* Re-check for suspending: a suspend could've occurred after we
	 * pm_context_active'd, and all the contexts could've been removed from the
	 * runpool before we took this lock. In this case, we don't want to allow
	 * this context to run jobs, we just want it out immediately.
	 *
	 * The DMB required to read the suspend flag was issued recently as part of
	 * the runpool_irq locking. If a suspend occurs *after* that lock was taken
	 * (i.e. this condition doesn't execute), then the kbasep_js_suspend() code
	 * will cleanup this context instead (by virtue of it being called strictly
	 * after the suspend flag is set, and will wait for this lock to drop) */
	if (kbase_pm_is_suspending(kbdev)) {
		/* Cause it to leave at some later point */
		mali_bool retained;
		retained = kbasep_js_runpool_retain_ctx_nolock(kbdev, head_kctx);
		KBASE_DEBUG_ASSERT(retained);
		kbasep_js_clear_submit_allowed(js_devdata, head_kctx);
		head_kctx_suspended = MALI_TRUE;
	}

	/* Try to run the next job, in case this context has jobs that match the
	 * job slot requirements, but none of the other currently running contexts
	 * do */
	kbasep_js_try_run_next_job_nolock(kbdev);

	/* Transaction complete */
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);
	mutex_unlock(&new_address_space->transaction_mutex);
	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	/* Note: after this point, the context could potentially get scheduled out immediately */

	if (head_kctx_suspended) {
		/* Finishing forcing out the context due to a suspend. Use a variant of
		 * kbasep_js_runpool_release_ctx() that doesn't schedule a new context,
		 * to prevent a risk of recursion back into this function */
		kbasep_js_runpool_release_ctx_no_schedule(kbdev, head_kctx);
	}
	return;
}

void kbasep_js_schedule_privileged_ctx(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_device_data *js_devdata;
	mali_bool is_scheduled;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	/* This must never be attempted whilst suspending - i.e. it should only
	 * happen in response to a syscall from a user-space thread */
	BUG_ON(kbase_pm_is_suspending(kbdev));

	kbase_pm_request_l2_caches(kbdev);

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	/* Mark the context as privileged */
	js_kctx_info->ctx.flags |= KBASE_CTX_FLAG_PRIVILEGED;

	is_scheduled = js_kctx_info->ctx.is_scheduled;
	if (is_scheduled == MALI_FALSE) {
		mali_bool is_runpool_full;

		/* Add the context to the runpool */
		mutex_lock(&js_devdata->queue_mutex);
		kbasep_js_policy_enqueue_ctx(&js_devdata->policy, kctx);
		mutex_unlock(&js_devdata->queue_mutex);

		mutex_lock(&js_devdata->runpool_mutex);
		{
			is_runpool_full = check_is_runpool_full(kbdev, kctx);
			if (is_runpool_full != MALI_FALSE) {
				/* Evict jobs from the NEXT registers to free an AS asap */
				kbasep_js_runpool_evict_next_jobs(kbdev, kctx);
			}
		}
		mutex_unlock(&js_devdata->runpool_mutex);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		/* Fast-starting requires the jsctx_mutex to be dropped, because it works on multiple ctxs */

		if (is_runpool_full != MALI_FALSE) {
			/* Evict non-running contexts from the runpool */
			kbasep_js_runpool_attempt_fast_start_ctx(kbdev, NULL);
		}
		/* Try to schedule the context in */
		kbasep_js_try_schedule_head_ctx(kbdev);

		/* Wait for the context to be scheduled in */
		wait_event(kctx->jctx.sched_info.ctx.is_scheduled_wait, kctx->jctx.sched_info.ctx.is_scheduled == MALI_TRUE);
	} else {
		/* Already scheduled in - We need to retain it to keep the corresponding address space */
		kbasep_js_runpool_retain_ctx(kbdev, kctx);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	}
}

void kbasep_js_release_privileged_ctx(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_kctx_info *js_kctx_info;
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;

	/* We don't need to use the address space anymore */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	js_kctx_info->ctx.flags &= (~KBASE_CTX_FLAG_PRIVILEGED);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	kbase_pm_release_l2_caches(kbdev);

	/* Release the context - it will be scheduled out if there is no pending job */
	kbasep_js_runpool_release_ctx(kbdev, kctx);
}

void kbasep_js_job_done_slot_irq(kbase_jd_atom *katom, int slot_nr,
                                 ktime_t *end_timestamp,
                                 kbasep_js_atom_done_code done_code)
{
	kbase_device *kbdev;
	kbasep_js_policy *js_policy;
	kbasep_js_device_data *js_devdata;
	mali_bool submit_retry_needed = MALI_TRUE;	/* If we don't start jobs here, start them from the workqueue */
	ktime_t tick_diff;
	u64 microseconds_spent = 0u;
	kbase_context *parent_ctx;

	KBASE_DEBUG_ASSERT(katom);
	parent_ctx = katom->kctx;
	KBASE_DEBUG_ASSERT(parent_ctx);
	kbdev = parent_ctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev);

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;

	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	/*
	 * Release resources before submitting new jobs (bounds the refcount of
	 * the resource to BASE_JM_SUBMIT_SLOTS)
	 */
#ifdef CONFIG_MALI_GATOR_SUPPORT
	kbase_trace_mali_job_slots_event(GATOR_MAKE_EVENT(GATOR_JOB_SLOT_STOP, slot_nr), NULL, 0);
#endif				/* CONFIG_MALI_GATOR_SUPPORT */

	/* Check if submitted jobs no longer require the cycle counter to be enabled */
	kbasep_js_deref_permon_check_and_disable_cycle_counter(kbdev, katom);

	/* Release the affinity from the slot - must happen before next submission to this slot */
	kbase_js_affinity_release_slot_cores(kbdev, slot_nr, katom->affinity);
	kbase_js_debug_log_current_affinities(kbdev);
	/* Calculate the job's time used */
	if (end_timestamp != NULL) {
		/* Only calculating it for jobs that really run on the HW (e.g. removed
		 * from next jobs never actually ran, so really did take zero time) */
		tick_diff = ktime_sub(*end_timestamp, katom->start_timestamp);

		microseconds_spent = ktime_to_ns(tick_diff);
		do_div(microseconds_spent, 1000);

		/* Round up time spent to the minimum timer resolution */
		if (microseconds_spent < KBASEP_JS_TICK_RESOLUTION_US)
			microseconds_spent = KBASEP_JS_TICK_RESOLUTION_US;
	}

	/* Log the result of the job (completion status, and time spent). */
	kbasep_js_policy_log_job_result(js_policy, katom, microseconds_spent);
	/* Determine whether the parent context's timeslice is up */
	if (kbasep_js_policy_should_remove_ctx(js_policy, parent_ctx) != MALI_FALSE)
		kbasep_js_clear_submit_allowed(js_devdata, parent_ctx);

	if (done_code & KBASE_JS_ATOM_DONE_START_NEW_ATOMS) {
		/* Submit a new job (if there is one) to help keep the GPU's HEAD and NEXT registers full */
		KBASE_TRACE_ADD_SLOT(kbdev, JS_JOB_DONE_TRY_RUN_NEXT_JOB, parent_ctx, katom, katom->jc, slot_nr);

		submit_retry_needed = kbasep_js_try_run_next_job_on_slot_irq_nolock(kbdev, slot_nr, &kbdev->slot_submit_count_irq[slot_nr]);
	}

	if (submit_retry_needed != MALI_FALSE || katom->event_code == BASE_JD_EVENT_STOPPED) {
		/* The extra condition on STOPPED jobs is needed because they may be
		 * the only job present, but they won't get re-run until the JD work
		 * queue activates. Crucially, work queues can run items out of order
		 * e.g. on different CPUs, so being able to submit from the IRQ handler
		 * is not a good indication that we don't need to run jobs; the
		 * submitted job could be processed on the work-queue *before* the
		 * stopped job, even though it was submitted after.
		 *
		 * Therefore, we must try to run it, otherwise it might not get run at
		 * all after this. */

		KBASE_TRACE_ADD_SLOT(kbdev, JS_JOB_DONE_RETRY_NEEDED, parent_ctx, katom, katom->jc, slot_nr);
		kbasep_js_set_job_retry_submit_slot(katom, slot_nr);
	}
}

void kbasep_js_suspend(kbase_device *kbdev)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;
	int i;
	u16 retained = 0u;
	int nr_privileged_ctx = 0;
	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(kbase_pm_is_suspending(kbdev));
	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	/* Prevent all contexts from submitting */
	js_devdata->runpool_irq.submit_allowed = 0;

	/* Retain each of the contexts, so we can cause it to leave even if it had
	 * no refcount to begin with */
	for (i = BASE_MAX_NR_AS - 1; i >= 0; --i) {
		kbasep_js_per_as_data *js_per_as_data = &js_devdata->runpool_irq.per_as_data[i];
		kbase_context *kctx = js_per_as_data->kctx;
		retained = retained << 1;

		if (kctx) {
			++(js_per_as_data->as_busy_refcount);
			retained |= 1u;
			/* We can only cope with up to 1 privileged context - the
			 * instrumented context. It'll be suspended by disabling
			 * instrumentation */
			if (kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_PRIVILEGED)
				KBASE_DEBUG_ASSERT(++nr_privileged_ctx == 1);
		}
	}
	CSTD_UNUSED(nr_privileged_ctx);
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	/* De-ref the previous retain to ensure each context gets pulled out
	 * sometime later. */
	for (i = 0;
		 i < BASE_MAX_NR_AS;
		 ++i, retained = retained >> 1) {
		kbasep_js_per_as_data *js_per_as_data = &js_devdata->runpool_irq.per_as_data[i];
		kbase_context *kctx = js_per_as_data->kctx;

		if (retained & 1u)
			kbasep_js_runpool_release_ctx(kbdev,kctx);
	}

	/* Caller must wait for all Power Manager active references to be dropped */
}

void kbasep_js_resume(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	int i;
	KBASE_DEBUG_ASSERT(kbdev);
	js_devdata = &kbdev->js_data;

	KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

	/* Schedule in as many contexts as address spaces. This also starts atoms. */
	for (i = 0 ; i < kbdev->nr_hw_address_spaces; ++i)
	{
		kbasep_js_try_schedule_head_ctx(kbdev);
	}
	/* JS Resume complete */
}
