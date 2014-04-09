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




#include <mali_kbase.h>

/*
 * Private functions follow
 */

/**
 * @brief Check whether a ctx has a certain attribute, and if so, retain that
 * attribute on the runpool.
 *
 * Requires:
 * - jsctx mutex
 * - runpool_irq spinlock
 * - ctx is scheduled on the runpool
 *
 * @return MALI_TRUE indicates a change in ctx attributes state of the runpool.
 * In this state, the scheduler might be able to submit more jobs than
 * previously, and so the caller should ensure kbasep_js_try_run_next_job_nolock()
 * or similar is called sometime later.
 * @return MALI_FALSE indicates no change in ctx attributes state of the runpool.
 */
STATIC mali_bool kbasep_js_ctx_attr_runpool_retain_attr(kbase_device *kbdev, kbase_context *kctx, kbasep_js_ctx_attr attribute)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool runpool_state_changed = MALI_FALSE;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	BUG_ON(!mutex_is_locked(&js_kctx_info->ctx.jsctx_mutex));
	lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);

	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled != MALI_FALSE);

	if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, attribute) != MALI_FALSE) {
		KBASE_DEBUG_ASSERT(js_devdata->runpool_irq.ctx_attr_ref_count[attribute] < S8_MAX);
		++(js_devdata->runpool_irq.ctx_attr_ref_count[attribute]);

		if (js_devdata->runpool_irq.ctx_attr_ref_count[attribute] == 1) {
			/* First refcount indicates a state change */
			runpool_state_changed = MALI_TRUE;
			KBASE_TRACE_ADD(kbdev, JS_CTX_ATTR_NOW_ON_RUNPOOL, kctx, NULL, 0u, attribute);
		}
	}

	return runpool_state_changed;
}

/**
 * @brief Check whether a ctx has a certain attribute, and if so, release that
 * attribute on the runpool.
 *
 * Requires:
 * - jsctx mutex
 * - runpool_irq spinlock
 * - ctx is scheduled on the runpool
 *
 * @return MALI_TRUE indicates a change in ctx attributes state of the runpool.
 * In this state, the scheduler might be able to submit more jobs than
 * previously, and so the caller should ensure kbasep_js_try_run_next_job_nolock()
 * or similar is called sometime later.
 * @return MALI_FALSE indicates no change in ctx attributes state of the runpool.
 */
STATIC mali_bool kbasep_js_ctx_attr_runpool_release_attr(kbase_device *kbdev, kbase_context *kctx, kbasep_js_ctx_attr attribute)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool runpool_state_changed = MALI_FALSE;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	BUG_ON(!mutex_is_locked(&js_kctx_info->ctx.jsctx_mutex));
	lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled != MALI_FALSE);

	if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, attribute) != MALI_FALSE) {
		KBASE_DEBUG_ASSERT(js_devdata->runpool_irq.ctx_attr_ref_count[attribute] > 0);
		--(js_devdata->runpool_irq.ctx_attr_ref_count[attribute]);

		if (js_devdata->runpool_irq.ctx_attr_ref_count[attribute] == 0) {
			/* Last de-refcount indicates a state change */
			runpool_state_changed = MALI_TRUE;
			KBASE_TRACE_ADD(kbdev, JS_CTX_ATTR_NOW_OFF_RUNPOOL, kctx, NULL, 0u, attribute);
		}
	}

	return runpool_state_changed;
}

/**
 * @brief Retain a certain attribute on a ctx, also retaining it on the runpool
 * if the context is scheduled.
 *
 * Requires:
 * - jsctx mutex
 * - If the context is scheduled, then runpool_irq spinlock must also be held
 *
 * @return MALI_TRUE indicates a change in ctx attributes state of the runpool.
 * This may allow the scheduler to submit more jobs than previously.
 * @return MALI_FALSE indicates no change in ctx attributes state of the runpool.
 */
STATIC mali_bool kbasep_js_ctx_attr_ctx_retain_attr(kbase_device *kbdev, kbase_context *kctx, kbasep_js_ctx_attr attribute)
{
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool runpool_state_changed = MALI_FALSE;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_kctx_info = &kctx->jctx.sched_info;

	BUG_ON(!mutex_is_locked(&js_kctx_info->ctx.jsctx_mutex));
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.ctx_attr_ref_count[attribute] < U32_MAX);

	++(js_kctx_info->ctx.ctx_attr_ref_count[attribute]);

	if (js_kctx_info->ctx.is_scheduled != MALI_FALSE && js_kctx_info->ctx.ctx_attr_ref_count[attribute] == 1) {
		lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);
		/* Only ref-count the attribute on the runpool for the first time this contexts sees this attribute */
		KBASE_TRACE_ADD(kbdev, JS_CTX_ATTR_NOW_ON_CTX, kctx, NULL, 0u, attribute);
		runpool_state_changed = kbasep_js_ctx_attr_runpool_retain_attr(kbdev, kctx, attribute);
	}

	return runpool_state_changed;
}

/**
 * @brief Release a certain attribute on a ctx, also releasign it from the runpool
 * if the context is scheduled.
 *
 * Requires:
 * - jsctx mutex
 * - If the context is scheduled, then runpool_irq spinlock must also be held
 *
 * @return MALI_TRUE indicates a change in ctx attributes state of the runpool.
 * This may allow the scheduler to submit more jobs than previously.
 * @return MALI_FALSE indicates no change in ctx attributes state of the runpool.
 */
STATIC mali_bool kbasep_js_ctx_attr_ctx_release_attr(kbase_device *kbdev, kbase_context *kctx, kbasep_js_ctx_attr attribute)
{
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool runpool_state_changed = MALI_FALSE;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_kctx_info = &kctx->jctx.sched_info;

	BUG_ON(!mutex_is_locked(&js_kctx_info->ctx.jsctx_mutex));
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.ctx_attr_ref_count[attribute] > 0);

	if (js_kctx_info->ctx.is_scheduled != MALI_FALSE && js_kctx_info->ctx.ctx_attr_ref_count[attribute] == 1) {
		lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);
		/* Only de-ref-count the attribute on the runpool when this is the last ctx-reference to it */
		runpool_state_changed = kbasep_js_ctx_attr_runpool_release_attr(kbdev, kctx, attribute);
		KBASE_TRACE_ADD(kbdev, JS_CTX_ATTR_NOW_OFF_CTX, kctx, NULL, 0u, attribute);
	}

	/* De-ref must happen afterwards, because kbasep_js_ctx_attr_runpool_release() needs to check it too */
	--(js_kctx_info->ctx.ctx_attr_ref_count[attribute]);

	return runpool_state_changed;
}

/*
 * More commonly used public functions
 */

void kbasep_js_ctx_attr_set_initial_attrs(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool runpool_state_changed = MALI_FALSE;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	js_kctx_info = &kctx->jctx.sched_info;

	if ((js_kctx_info->ctx.flags & KBASE_CTX_FLAG_SUBMIT_DISABLED) != MALI_FALSE) {
		/* This context never submits, so don't track any scheduling attributes */
		return;
	}

	/* Transfer attributes held in the context flags for contexts that have submit enabled */

	if ((js_kctx_info->ctx.flags & KBASE_CTX_FLAG_HINT_ONLY_COMPUTE) != MALI_FALSE) {
		/* Compute context */
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_retain_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE);
	}
	/* NOTE: Whether this is a non-compute context depends on the jobs being
	 * run, e.g. it might be submitting jobs with BASE_JD_REQ_ONLY_COMPUTE */

	/* ... More attributes can be added here ... */

	/* The context should not have been scheduled yet, so ASSERT if this caused
	 * runpool state changes (note that other threads *can't* affect the value
	 * of runpool_state_changed, due to how it's calculated) */
	KBASE_DEBUG_ASSERT(runpool_state_changed == MALI_FALSE);
	CSTD_UNUSED(runpool_state_changed);
}

void kbasep_js_ctx_attr_runpool_retain_ctx(kbase_device *kbdev, kbase_context *kctx)
{
	mali_bool runpool_state_changed;
	int i;

	/* Retain any existing attributes */
	for (i = 0; i < KBASEP_JS_CTX_ATTR_COUNT; ++i) {
		if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, (kbasep_js_ctx_attr) i) != MALI_FALSE) {
			/* The context is being scheduled in, so update the runpool with the new attributes */
			runpool_state_changed = kbasep_js_ctx_attr_runpool_retain_attr(kbdev, kctx, (kbasep_js_ctx_attr) i);

			/* We don't need to know about state changed, because retaining a
			 * context occurs on scheduling it, and that itself will also try
			 * to run new atoms */
			CSTD_UNUSED(runpool_state_changed);
		}
	}
}

mali_bool kbasep_js_ctx_attr_runpool_release_ctx(kbase_device *kbdev, kbase_context *kctx)
{
	mali_bool runpool_state_changed = MALI_FALSE;
	int i;

	/* Release any existing attributes */
	for (i = 0; i < KBASEP_JS_CTX_ATTR_COUNT; ++i) {
		if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, (kbasep_js_ctx_attr) i) != MALI_FALSE) {
			/* The context is being scheduled out, so update the runpool on the removed attributes */
			runpool_state_changed |= kbasep_js_ctx_attr_runpool_release_attr(kbdev, kctx, (kbasep_js_ctx_attr) i);
		}
	}

	return runpool_state_changed;
}

void kbasep_js_ctx_attr_ctx_retain_atom(kbase_device *kbdev, kbase_context *kctx, kbase_jd_atom *katom)
{
	mali_bool runpool_state_changed = MALI_FALSE;
	base_jd_core_req core_req;

	KBASE_DEBUG_ASSERT(katom);
	core_req = katom->core_req;

	if (core_req & BASE_JD_REQ_ONLY_COMPUTE)
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_retain_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE);
	else
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_retain_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_NON_COMPUTE);

	if ((core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE | BASE_JD_REQ_T)) != 0 && (core_req & (BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)) == 0) {
		/* Atom that can run on slot1 or slot2, and can use all cores */
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_retain_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES);
	}

	/* We don't need to know about state changed, because retaining an
	 * atom occurs on adding it, and that itself will also try to run
	 * new atoms */
	CSTD_UNUSED(runpool_state_changed);
}

mali_bool kbasep_js_ctx_attr_ctx_release_atom(kbase_device *kbdev, kbase_context *kctx, kbasep_js_atom_retained_state *katom_retained_state)
{
	mali_bool runpool_state_changed = MALI_FALSE;
	base_jd_core_req core_req;

	KBASE_DEBUG_ASSERT(katom_retained_state);
	core_req = katom_retained_state->core_req;

	/* No-op for invalid atoms */
	if (kbasep_js_atom_retained_state_is_valid(katom_retained_state) == MALI_FALSE)
		return MALI_FALSE;

	if (core_req & BASE_JD_REQ_ONLY_COMPUTE)
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_release_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE);
	else
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_release_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_NON_COMPUTE);

	if ((core_req & (BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE | BASE_JD_REQ_T)) != 0 && (core_req & (BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP)) == 0) {
		/* Atom that can run on slot1 or slot2, and can use all cores */
		runpool_state_changed |= kbasep_js_ctx_attr_ctx_release_attr(kbdev, kctx, KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES);
	}

	return runpool_state_changed;
}
