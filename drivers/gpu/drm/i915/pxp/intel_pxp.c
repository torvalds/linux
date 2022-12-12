// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */
#include <linux/workqueue.h>
#include "intel_pxp.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"
#include "gem/i915_gem_context.h"
#include "gt/intel_context.h"
#include "i915_drv.h"

/**
 * DOC: PXP
 *
 * PXP (Protected Xe Path) is a feature available in Gen12 and newer platforms.
 * It allows execution and flip to display of protected (i.e. encrypted)
 * objects. The SW support is enabled via the CONFIG_DRM_I915_PXP kconfig.
 *
 * Objects can opt-in to PXP encryption at creation time via the
 * I915_GEM_CREATE_EXT_PROTECTED_CONTENT create_ext flag. For objects to be
 * correctly protected they must be used in conjunction with a context created
 * with the I915_CONTEXT_PARAM_PROTECTED_CONTENT flag. See the documentation
 * of those two uapi flags for details and restrictions.
 *
 * Protected objects are tied to a pxp session; currently we only support one
 * session, which i915 manages and whose index is available in the uapi
 * (I915_PROTECTED_CONTENT_DEFAULT_SESSION) for use in instructions targeting
 * protected objects.
 * The session is invalidated by the HW when certain events occur (e.g.
 * suspend/resume). When this happens, all the objects that were used with the
 * session are marked as invalid and all contexts marked as using protected
 * content are banned. Any further attempt at using them in an execbuf call is
 * rejected, while flips are converted to black frames.
 *
 * Some of the PXP setup operations are performed by the Management Engine,
 * which is handled by the mei driver; communication between i915 and mei is
 * performed via the mei_pxp component module.
 */

struct intel_gt *pxp_to_gt(const struct intel_pxp *pxp)
{
	return container_of(pxp, struct intel_gt, pxp);
}

bool intel_pxp_is_enabled(const struct intel_pxp *pxp)
{
	return pxp->ce;
}

bool intel_pxp_is_active(const struct intel_pxp *pxp)
{
	return pxp->arb_is_valid;
}

/* KCR register definitions */
#define KCR_INIT _MMIO(0x320f0)
/* Setting KCR Init bit is required after system boot */
#define KCR_INIT_ALLOW_DISPLAY_ME_WRITES REG_BIT(14)

static void kcr_pxp_enable(struct intel_gt *gt)
{
	intel_uncore_write(gt->uncore, KCR_INIT,
			   _MASKED_BIT_ENABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES));
}

static void kcr_pxp_disable(struct intel_gt *gt)
{
	intel_uncore_write(gt->uncore, KCR_INIT,
			   _MASKED_BIT_DISABLE(KCR_INIT_ALLOW_DISPLAY_ME_WRITES));
}

static int create_vcs_context(struct intel_pxp *pxp)
{
	static struct lock_class_key pxp_lock;
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct intel_engine_cs *engine;
	struct intel_context *ce;
	int i;

	/*
	 * Find the first VCS engine present. We're guaranteed there is one
	 * if we're in this function due to the check in has_pxp
	 */
	for (i = 0, engine = NULL; !engine; i++)
		engine = gt->engine_class[VIDEO_DECODE_CLASS][i];

	GEM_BUG_ON(!engine || engine->class != VIDEO_DECODE_CLASS);

	ce = intel_engine_create_pinned_context(engine, engine->gt->vm, SZ_4K,
						I915_GEM_HWS_PXP_ADDR,
						&pxp_lock, "pxp_context");
	if (IS_ERR(ce)) {
		drm_err(&gt->i915->drm, "failed to create VCS ctx for PXP\n");
		return PTR_ERR(ce);
	}

	pxp->ce = ce;

	return 0;
}

static void destroy_vcs_context(struct intel_pxp *pxp)
{
	intel_engine_destroy_pinned_context(fetch_and_zero(&pxp->ce));
}

void intel_pxp_init(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp_to_gt(pxp);
	int ret;

	if (!HAS_PXP(gt->i915))
		return;

	mutex_init(&pxp->tee_mutex);

	/*
	 * we'll use the completion to check if there is a termination pending,
	 * so we start it as completed and we reinit it when a termination
	 * is triggered.
	 */
	init_completion(&pxp->termination);
	complete_all(&pxp->termination);

	mutex_init(&pxp->arb_mutex);
	INIT_WORK(&pxp->session_work, intel_pxp_session_work);

	ret = create_vcs_context(pxp);
	if (ret)
		return;

	ret = intel_pxp_tee_component_init(pxp);
	if (ret)
		goto out_context;

	drm_info(&gt->i915->drm, "Protected Xe Path (PXP) protected content support initialized\n");

	return;

out_context:
	destroy_vcs_context(pxp);
}

void intel_pxp_fini(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	pxp->arb_is_valid = false;

	intel_pxp_tee_component_fini(pxp);

	destroy_vcs_context(pxp);
}

void intel_pxp_mark_termination_in_progress(struct intel_pxp *pxp)
{
	pxp->arb_is_valid = false;
	reinit_completion(&pxp->termination);
}

static void pxp_queue_termination(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp_to_gt(pxp);

	/*
	 * We want to get the same effect as if we received a termination
	 * interrupt, so just pretend that we did.
	 */
	spin_lock_irq(gt->irq_lock);
	intel_pxp_mark_termination_in_progress(pxp);
	pxp->session_events |= PXP_TERMINATION_REQUEST;
	queue_work(system_unbound_wq, &pxp->session_work);
	spin_unlock_irq(gt->irq_lock);
}

static bool pxp_component_bound(struct intel_pxp *pxp)
{
	bool bound = false;

	mutex_lock(&pxp->tee_mutex);
	if (pxp->pxp_component)
		bound = true;
	mutex_unlock(&pxp->tee_mutex);

	return bound;
}

/*
 * the arb session is restarted from the irq work when we receive the
 * termination completion interrupt
 */
int intel_pxp_start(struct intel_pxp *pxp)
{
	int ret = 0;

	if (!intel_pxp_is_enabled(pxp))
		return -ENODEV;

	if (wait_for(pxp_component_bound(pxp), 250))
		return -ENXIO;

	mutex_lock(&pxp->arb_mutex);

	if (pxp->arb_is_valid)
		goto unlock;

	pxp_queue_termination(pxp);

	if (!wait_for_completion_timeout(&pxp->termination,
					msecs_to_jiffies(250))) {
		ret = -ETIMEDOUT;
		goto unlock;
	}

	/* make sure the compiler doesn't optimize the double access */
	barrier();

	if (!pxp->arb_is_valid)
		ret = -EIO;

unlock:
	mutex_unlock(&pxp->arb_mutex);
	return ret;
}

void intel_pxp_init_hw(struct intel_pxp *pxp)
{
	kcr_pxp_enable(pxp_to_gt(pxp));
	intel_pxp_irq_enable(pxp);
}

void intel_pxp_fini_hw(struct intel_pxp *pxp)
{
	kcr_pxp_disable(pxp_to_gt(pxp));

	intel_pxp_irq_disable(pxp);
}

int intel_pxp_key_check(struct intel_pxp *pxp,
			struct drm_i915_gem_object *obj,
			bool assign)
{
	if (!intel_pxp_is_active(pxp))
		return -ENODEV;

	if (!i915_gem_object_is_protected(obj))
		return -EINVAL;

	GEM_BUG_ON(!pxp->key_instance);

	/*
	 * If this is the first time we're using this object, it's not
	 * encrypted yet; it will be encrypted with the current key, so mark it
	 * as such. If the object is already encrypted, check instead if the
	 * used key is still valid.
	 */
	if (!obj->pxp_key_instance && assign)
		obj->pxp_key_instance = pxp->key_instance;

	if (obj->pxp_key_instance != pxp->key_instance)
		return -ENOEXEC;

	return 0;
}

void intel_pxp_invalidate(struct intel_pxp *pxp)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct i915_gem_context *ctx, *cn;

	/* ban all contexts marked as protected */
	spin_lock_irq(&i915->gem.contexts.lock);
	list_for_each_entry_safe(ctx, cn, &i915->gem.contexts.list, link) {
		struct i915_gem_engines_iter it;
		struct intel_context *ce;

		if (!kref_get_unless_zero(&ctx->ref))
			continue;

		if (likely(!i915_gem_context_uses_protected_content(ctx))) {
			i915_gem_context_put(ctx);
			continue;
		}

		spin_unlock_irq(&i915->gem.contexts.lock);

		/*
		 * By the time we get here we are either going to suspend with
		 * quiesced execution or the HW keys are already long gone and
		 * in this case it is worthless to attempt to close the context
		 * and wait for its execution. It will hang the GPU if it has
		 * not already. So, as a fast mitigation, we can ban the
		 * context as quick as we can. That might race with the
		 * execbuffer, but currently this is the best that can be done.
		 */
		for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it)
			intel_context_ban(ce, NULL);
		i915_gem_context_unlock_engines(ctx);

		/*
		 * The context has been banned, no need to keep the wakeref.
		 * This is safe from races because the only other place this
		 * is touched is context_release and we're holding a ctx ref
		 */
		if (ctx->pxp_wakeref) {
			intel_runtime_pm_put(&i915->runtime_pm,
					     ctx->pxp_wakeref);
			ctx->pxp_wakeref = 0;
		}

		spin_lock_irq(&i915->gem.contexts.lock);
		list_safe_reset_next(ctx, cn, link);
		i915_gem_context_put(ctx);
	}
	spin_unlock_irq(&i915->gem.contexts.lock);
}
