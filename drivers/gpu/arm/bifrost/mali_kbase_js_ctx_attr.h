/*
 *
 * (C) COPYRIGHT 2012-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/**
 * @file mali_kbase_js_ctx_attr.h
 * Job Scheduler Context Attribute APIs
 */

#ifndef _KBASE_JS_CTX_ATTR_H_
#define _KBASE_JS_CTX_ATTR_H_

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

/**
 * Set the initial attributes of a context (when context create flags are set)
 *
 * Requires:
 * - Hold the jsctx_mutex
 */
void kbasep_js_ctx_attr_set_initial_attrs(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * Retain all attributes of a context
 *
 * This occurs on scheduling in the context on the runpool (but after
 * is_scheduled is set)
 *
 * Requires:
 * - jsctx mutex
 * - runpool_irq spinlock
 * - ctx->is_scheduled is true
 */
void kbasep_js_ctx_attr_runpool_retain_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * Release all attributes of a context
 *
 * This occurs on scheduling out the context from the runpool (but before
 * is_scheduled is cleared)
 *
 * Requires:
 * - jsctx mutex
 * - runpool_irq spinlock
 * - ctx->is_scheduled is true
 *
 * @return true indicates a change in ctx attributes state of the runpool.
 * In this state, the scheduler might be able to submit more jobs than
 * previously, and so the caller should ensure kbasep_js_try_run_next_job_nolock()
 * or similar is called sometime later.
 * @return false indicates no change in ctx attributes state of the runpool.
 */
bool kbasep_js_ctx_attr_runpool_release_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * Retain all attributes of an atom
 *
 * This occurs on adding an atom to a context
 *
 * Requires:
 * - jsctx mutex
 * - If the context is scheduled, then runpool_irq spinlock must also be held
 */
void kbasep_js_ctx_attr_ctx_retain_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *katom);

/**
 * Release all attributes of an atom, given its retained state.
 *
 * This occurs after (permanently) removing an atom from a context
 *
 * Requires:
 * - jsctx mutex
 * - If the context is scheduled, then runpool_irq spinlock must also be held
 *
 * This is a no-op when \a katom_retained_state is invalid.
 *
 * @return true indicates a change in ctx attributes state of the runpool.
 * In this state, the scheduler might be able to submit more jobs than
 * previously, and so the caller should ensure kbasep_js_try_run_next_job_nolock()
 * or similar is called sometime later.
 * @return false indicates no change in ctx attributes state of the runpool.
 */
bool kbasep_js_ctx_attr_ctx_release_atom(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbasep_js_atom_retained_state *katom_retained_state);

/**
 * Requires:
 * - runpool_irq spinlock
 */
static inline s8 kbasep_js_ctx_attr_count_on_runpool(struct kbase_device *kbdev, enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_device_data *js_devdata;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_devdata = &kbdev->js_data;

	return js_devdata->runpool_irq.ctx_attr_ref_count[attribute];
}

/**
 * Requires:
 * - runpool_irq spinlock
 */
static inline bool kbasep_js_ctx_attr_is_attr_on_runpool(struct kbase_device *kbdev, enum kbasep_js_ctx_attr attribute)
{
	/* In general, attributes are 'on' when they have a non-zero refcount (note: the refcount will never be < 0) */
	return (bool) kbasep_js_ctx_attr_count_on_runpool(kbdev, attribute);
}

/**
 * Requires:
 * - jsctx mutex
 */
static inline bool kbasep_js_ctx_attr_is_attr_on_ctx(struct kbase_context *kctx, enum kbasep_js_ctx_attr attribute)
{
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(attribute < KBASEP_JS_CTX_ATTR_COUNT);
	js_kctx_info = &kctx->jctx.sched_info;

	/* In general, attributes are 'on' when they have a refcount (which should never be < 0) */
	return (bool) (js_kctx_info->ctx.ctx_attr_ref_count[attribute]);
}

	  /** @} *//* end group kbase_js */
	  /** @} *//* end group base_kbase_api */
	  /** @} *//* end group base_api */

#endif				/* _KBASE_JS_DEFS_H_ */
