// SPDX-License-Identifier: GPL-2.0
/*
 *
 * (C) COPYRIGHT 2012-2016, 2018, 2020-2021 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>
#include <mali_kbase_config.h>

/*
 * Private functions follow
 */

/**
 * Check whether a ctx has a certain attribute, and if so, retain that
 * attribute on the runpool.
 * @kbdev: Device pointer
 * @kctx:  KBase context
 * @attribute: Atribute to check/retain
 *
 * Requires:
 * - jsctx mutex
 * - runpool_irq spinlock
 * - ctx is scheduled on the runpool
 *
 * @return true indicates a change in ctx attributes state of the runpool.
 * In this state, the scheduler might be able to submit more jobs than
 * previously, and so the caller should ensure kbasep_js_try_run_next_job_nolock()
 * or similar is called sometime later.
 * @return false indicates no change in ctx attributes state of the runpool.
 */
static bool kbasep_js_ctx_attr_runpool_retain_attr(struct kbase_device *kbdev, struct kbase_context *kctx, enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, attribute) != false) {
		KBASE_DEBUG_ASSERT(js_devdata->runpool_irq.ctx_attr_ref_count[attribute] < S8_MAX);
		++(js_devdata->runpool_irq.ctx_attr_ref_count[attribute]);

		if (js_devdata->runpool_irq.ctx_attr_ref_count[attribute] == 1) {
			/* First refcount indicates a state change */
			runpool_state_changed = true;
			KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_ON_RUNPOOL, kctx, NULL, 0u, attribute);
		}
	}

	return runpool_state_changed;
}

/**
 * Check whether a ctx has a certain attribute, and if so, release that
 * attribute on the runpool.
 * @kbdev: Device pointer
 * @kctx:  KBase context
 * @attribute: Atribute to release
 *
 * Requires:
 * - jsctx mutex
 * - runpool_irq spinlock
 * - ctx is scheduled on the runpool
 *
 * @return true indicates a change in ctx attributes state of the runpool.
 * In this state, the scheduler might be able to submit more jobs than
 * previously, and so the caller should ensure kbasep_js_try_run_next_job_nolock()
 * or similar is called sometime later.
 * @return false indicates no change in ctx attributes state of the runpool.
 */
static bool kbasep_js_ctx_attr_runpool_release_attr(struct kbase_device *kbdev, struct kbase_context *kctx, enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, attribute) != false) {
		KBASE_DEBUG_ASSERT(js_devdata->runpool_irq.ctx_attr_ref_count[attribute] > 0);
		--(js_devdata->runpool_irq.ctx_attr_ref_count[attribute]);

		if (js_devdata->runpool_irq.ctx_attr_ref_count[attribute] == 0) {
			/* Last de-refcount indicates a state change */
			runpool_state_changed = true;
			KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_OFF_RUNPOOL, kctx, NULL, 0u, attribute);
		}
	}

	return runpool_state_changed;
}

/**
 * Retain a certain attribute on a ctx, also retaining it on the runpool
 * if the context is scheduled.
 * @kbdev: Device pointer
 * @kctx:  KBase context
 * @attribute: Atribute to retain
 *
 * Requires:
 * - jsctx mutex
 * - If the context is scheduled, then runpool_irq spinlock must also be held
 *
 * @return true indicates a change in ctx attributes state of the runpool.
 * This may allow the scheduler to submit more jobs than previously.
 * @return false indicates no change in ctx attributes state of the runpool.
 */
static bool kbasep_js_ctx_attr_ctx_retain_attr(struct kbase_device *kbdev, struct kbase_context *kctx, enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.ctx_attr_ref_count[attribute] < U32_MAX);

	++(js_kctx_info->ctx.ctx_attr_ref_count[attribute]);

	if (kbase_ctx_flag(kctx, KCTX_SCHEDULED) && js_kctx_info->ctx.ctx_attr_ref_count[attribute] == 1) {
		/* Only ref-count the attribute on the runpool for the first time this contexts sees this attribute */
		KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_ON_CTX, kctx, NULL, 0u, attribute);
		runpool_state_changed = kbasep_js_ctx_attr_runpool_retain_attr(kbdev, kctx, attribute);
	}

	return runpool_state_changed;
}

/**
 * Release a certain attribute on a ctx, also releasing it from the runpool
 * if the context is scheduled.
 * @kbdev: Device pointer
 * @kctx:  KBase context
 * @attribute: Atribute to release
 *
 * Requires:
 * - jsctx mutex
 * - If the context is scheduled, then runpool_irq spinlock must also be held
 *
 * @return true indicates a change in ctx attributes state of the runpool.
 * This may allow the scheduler to submit more jobs than previously.
 * @return false indicates no change in ctx attributes state of the runpool.
 */
static bool kbasep_js_ctx_attr_ctx_release_attr(struct kbase_device *kbdev, struct kbase_context *kctx, enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_kctx_info *js_kctx_info;
	bool runpool_state_changed = false;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.ctx_attr_ref_count[attribute] > 0);

	if (kbase_ctx_flag(kctx, KCTX_SCHEDULED) && js_kctx_info->ctx.ctx_attr_ref_count[attribute] == 1) {
		lockdep_assert_held(&kbdev->hwaccess_lock);
		/* Only de-ref-count the attribute on the runpool when this is the last ctx-reference to it */
		runpool_state_changed = kbasep_js_ctx_attr_runpool_release_attr(kbdev, kctx, attribute);
		KBASE_KTRACE_ADD_JM(kbdev, JS_CTX_ATTR_NOW_OFF_CTX, kctx, NULL, 0u, attribute);
	}

	/* De-ref must happen afterwards, because kbasep_js_ctx_attr_runpool_release() needs to check it too */
	--(js_kctx_info->ctx.ctx_attr_ref_count[attribute]);

	return runpool_state_changed;
}

/*
 * More commonly used public functions
 */

void kbasep_js_ctx_attr_runpool_retain_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	bool runpool_state_changed;
	int i;

	/* Retain any existing attributes */
	for (i = 0; i < KBASEP_JS_CTX_ATTR_COUNT; ++i) {
		if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, (enum kbasep_js_ctx_attr) i) != false) {
			/* The context is being scheduled in, so update the runpool with the new attributes */
			runpool_state_changed = kbasep_js_ctx_attr_runpool_retain_attr(kbdev, kctx, (enum kbasep_js_ctx_attr) i);

			/* We don't need to know about state changed, because retaining a
			 * context occurs on scheduling it, and that itself will also try
			 * to run new atoms
			 */
			CSTD_UNUSED(runpool_state_changed);
		}
	}
}

bool kbasep_js_ctx_attr_runpool_release_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	bool runpool_state_changed = false;
	int i;

	/* Release any existing attributes */
	for (i = 0; i < KBASEP_JS_CTX_ATTR_COUNT; ++i) {
		if (kbasep_js_ctx_attr_is_attr_on_ctx(kctx, (enum kbasep_js_ctx_attr) i) != false) {
			/* The context is being scheduled out, so update the runpool on the removed attributes */
			runpool_state_changed |= kbasep_js_ctx_attr_runpool_release_attr(kbdev, kctx, (enum kbasep_js_ctx_attr) i);
		}
	}

	return runpool_state_changed;
}

void kbasep_js_ctx_attr_ctx_retain_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom)
{
	bool runpool_state_changed = false;
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

	/* We don't need to know about state changed, because retaining an atom
	 * occurs on adding it, and that itself will also try to run new atoms
	 */
	CSTD_UNUSED(runpool_state_changed);
}

bool kbasep_js_ctx_attr_ctx_release_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbasep_js_atom_retained_state *katom_retained_state)
{
	bool runpool_state_changed = false;
	base_jd_core_req core_req;

	KBASE_DEBUG_ASSERT(katom_retained_state);
	core_req = katom_retained_state->core_req;

	/* No-op for invalid atoms */
	if (kbasep_js_atom_retained_state_is_valid(katom_retained_state) == false)
		return false;

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
