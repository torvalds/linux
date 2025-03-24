// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_vblank.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "dcss-dev.h"
#include "dcss-kms.h"

static int dcss_enable_vblank(struct drm_crtc *crtc)
{
	struct dcss_crtc *dcss_crtc = container_of(crtc, struct dcss_crtc,
						   base);
	struct dcss_dev *dcss = crtc->dev->dev_private;

	dcss_dtg_vblank_irq_enable(dcss->dtg, true);

	dcss_dtg_ctxld_kick_irq_enable(dcss->dtg, true);

	enable_irq(dcss_crtc->irq);

	return 0;
}

static void dcss_disable_vblank(struct drm_crtc *crtc)
{
	struct dcss_crtc *dcss_crtc = container_of(crtc, struct dcss_crtc,
						   base);
	struct dcss_dev *dcss = dcss_crtc->base.dev->dev_private;

	disable_irq_nosync(dcss_crtc->irq);

	dcss_dtg_vblank_irq_enable(dcss->dtg, false);

	if (dcss_crtc->disable_ctxld_kick_irq)
		dcss_dtg_ctxld_kick_irq_enable(dcss->dtg, false);
}

static const struct drm_crtc_funcs dcss_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = dcss_enable_vblank,
	.disable_vblank = dcss_disable_vblank,
};

static void dcss_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static void dcss_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct dcss_crtc *dcss_crtc = container_of(crtc, struct dcss_crtc,
						   base);
	struct dcss_dev *dcss = dcss_crtc->base.dev->dev_private;

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc));
		drm_crtc_arm_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	if (dcss_dtg_is_enabled(dcss->dtg))
		dcss_ctxld_enable(dcss->ctxld);
}

static void dcss_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state,
									      crtc);
	struct dcss_crtc *dcss_crtc = container_of(crtc, struct dcss_crtc,
						   base);
	struct dcss_dev *dcss = dcss_crtc->base.dev->dev_private;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_display_mode *old_mode = &old_crtc_state->adjusted_mode;
	struct videomode vm;

	drm_display_mode_to_videomode(mode, &vm);

	pm_runtime_get_sync(dcss->dev);

	vm.pixelclock = mode->crtc_clock * 1000;

	dcss_ss_subsam_set(dcss->ss);
	dcss_dtg_css_set(dcss->dtg);

	if (!drm_mode_equal(mode, old_mode) || !old_crtc_state->active) {
		dcss_dtg_sync_set(dcss->dtg, &vm);
		dcss_ss_sync_set(dcss->ss, &vm,
				 mode->flags & DRM_MODE_FLAG_PHSYNC,
				 mode->flags & DRM_MODE_FLAG_PVSYNC);
	}

	dcss_enable_dtg_and_ss(dcss);

	dcss_ctxld_enable(dcss->ctxld);

	/* Allow CTXLD kick interrupt to be disabled when VBLANK is disabled. */
	dcss_crtc->disable_ctxld_kick_irq = true;
}

static void dcss_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state = drm_atomic_get_old_crtc_state(state,
									      crtc);
	struct dcss_crtc *dcss_crtc = container_of(crtc, struct dcss_crtc,
						   base);
	struct dcss_dev *dcss = dcss_crtc->base.dev->dev_private;
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_display_mode *old_mode = &old_crtc_state->adjusted_mode;

	drm_atomic_helper_disable_planes_on_crtc(old_crtc_state, false);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	dcss_dtg_ctxld_kick_irq_enable(dcss->dtg, true);

	reinit_completion(&dcss->disable_completion);

	dcss_disable_dtg_and_ss(dcss);

	dcss_ctxld_enable(dcss->ctxld);

	if (!drm_mode_equal(mode, old_mode) || !crtc->state->active)
		if (!wait_for_completion_timeout(&dcss->disable_completion,
						 msecs_to_jiffies(100)))
			dev_err(dcss->dev, "Shutting off DTG timed out.\n");

	/*
	 * Do not shut off CTXLD kick interrupt when shutting VBLANK off. It
	 * will be needed to commit the last changes, before going to suspend.
	 */
	dcss_crtc->disable_ctxld_kick_irq = false;

	drm_crtc_vblank_off(crtc);

	pm_runtime_mark_last_busy(dcss->dev);
	pm_runtime_put_autosuspend(dcss->dev);
}

static const struct drm_crtc_helper_funcs dcss_helper_funcs = {
	.atomic_begin = dcss_crtc_atomic_begin,
	.atomic_flush = dcss_crtc_atomic_flush,
	.atomic_enable = dcss_crtc_atomic_enable,
	.atomic_disable = dcss_crtc_atomic_disable,
};

static irqreturn_t dcss_crtc_irq_handler(int irq, void *dev_id)
{
	struct dcss_crtc *dcss_crtc = dev_id;
	struct dcss_dev *dcss = dcss_crtc->base.dev->dev_private;

	if (!dcss_dtg_vblank_irq_valid(dcss->dtg))
		return IRQ_NONE;

	if (dcss_ctxld_is_flushed(dcss->ctxld))
		drm_crtc_handle_vblank(&dcss_crtc->base);

	dcss_dtg_vblank_irq_clear(dcss->dtg);

	return IRQ_HANDLED;
}

int dcss_crtc_init(struct dcss_crtc *crtc, struct drm_device *drm)
{
	struct dcss_dev *dcss = drm->dev_private;
	struct platform_device *pdev = to_platform_device(dcss->dev);
	int ret;

	crtc->plane[0] = dcss_plane_init(drm, drm_crtc_mask(&crtc->base),
					 DRM_PLANE_TYPE_PRIMARY, 0);
	if (IS_ERR(crtc->plane[0]))
		return PTR_ERR(crtc->plane[0]);

	crtc->base.port = dcss->of_port;

	drm_crtc_helper_add(&crtc->base, &dcss_helper_funcs);
	ret = drm_crtc_init_with_planes(drm, &crtc->base, &crtc->plane[0]->base,
					NULL, &dcss_crtc_funcs, NULL);
	if (ret) {
		dev_err(dcss->dev, "failed to init crtc\n");
		return ret;
	}

	crtc->irq = platform_get_irq_byname(pdev, "vblank");
	if (crtc->irq < 0)
		return crtc->irq;

	ret = request_irq(crtc->irq, dcss_crtc_irq_handler, IRQF_NO_AUTOEN,
			  "dcss_drm", crtc);
	if (ret) {
		dev_err(dcss->dev, "irq request failed with %d.\n", ret);
		return ret;
	}

	return 0;
}

void dcss_crtc_deinit(struct dcss_crtc *crtc, struct drm_device *drm)
{
	free_irq(crtc->irq, crtc);
}
