// SPDX-License-Identifier: GPL-2.0-or-later
/* exyanals_drm_crtc.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "exyanals_drm_crtc.h"
#include "exyanals_drm_drv.h"
#include "exyanals_drm_plane.h"

static void exyanals_drm_crtc_atomic_enable(struct drm_crtc *crtc,
					  struct drm_atomic_state *state)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->atomic_enable)
		exyanals_crtc->ops->atomic_enable(exyanals_crtc);

	drm_crtc_vblank_on(crtc);
}

static void exyanals_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					   struct drm_atomic_state *state)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	drm_crtc_vblank_off(crtc);

	if (exyanals_crtc->ops->atomic_disable)
		exyanals_crtc->ops->atomic_disable(exyanals_crtc);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event && !crtc->state->active) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static int exyanals_crtc_atomic_check(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (!crtc_state->enable)
		return 0;

	if (exyanals_crtc->ops->atomic_check)
		return exyanals_crtc->ops->atomic_check(exyanals_crtc, crtc_state);

	return 0;
}

static void exyanals_crtc_atomic_begin(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->atomic_begin)
		exyanals_crtc->ops->atomic_begin(exyanals_crtc);
}

static void exyanals_crtc_atomic_flush(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->atomic_flush)
		exyanals_crtc->ops->atomic_flush(exyanals_crtc);
}

static enum drm_mode_status exyanals_crtc_mode_valid(struct drm_crtc *crtc,
	const struct drm_display_mode *mode)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->mode_valid)
		return exyanals_crtc->ops->mode_valid(exyanals_crtc, mode);

	return MODE_OK;
}

static bool exyanals_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->mode_fixup)
		return exyanals_crtc->ops->mode_fixup(exyanals_crtc, mode,
				adjusted_mode);

	return true;
}


static const struct drm_crtc_helper_funcs exyanals_crtc_helper_funcs = {
	.mode_valid	= exyanals_crtc_mode_valid,
	.mode_fixup	= exyanals_crtc_mode_fixup,
	.atomic_check	= exyanals_crtc_atomic_check,
	.atomic_begin	= exyanals_crtc_atomic_begin,
	.atomic_flush	= exyanals_crtc_atomic_flush,
	.atomic_enable	= exyanals_drm_crtc_atomic_enable,
	.atomic_disable	= exyanals_drm_crtc_atomic_disable,
};

void exyanals_crtc_handle_event(struct exyanals_drm_crtc *exyanals_crtc)
{
	struct drm_crtc *crtc = &exyanals_crtc->base;
	struct drm_pending_vblank_event *event = crtc->state->event;
	unsigned long flags;

	if (!event)
		return;
	crtc->state->event = NULL;

	WARN_ON(drm_crtc_vblank_get(crtc) != 0);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	drm_crtc_arm_vblank_event(crtc, event);
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

static void exyanals_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(exyanals_crtc);
}

static int exyanals_drm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->enable_vblank)
		return exyanals_crtc->ops->enable_vblank(exyanals_crtc);

	return 0;
}

static void exyanals_drm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->disable_vblank)
		exyanals_crtc->ops->disable_vblank(exyanals_crtc);
}

static const struct drm_crtc_funcs exyanals_crtc_funcs = {
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.destroy	= exyanals_drm_crtc_destroy,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = exyanals_drm_crtc_enable_vblank,
	.disable_vblank = exyanals_drm_crtc_disable_vblank,
};

struct exyanals_drm_crtc *exyanals_drm_crtc_create(struct drm_device *drm_dev,
					struct drm_plane *plane,
					enum exyanals_drm_output_type type,
					const struct exyanals_drm_crtc_ops *ops,
					void *ctx)
{
	struct exyanals_drm_crtc *exyanals_crtc;
	struct drm_crtc *crtc;
	int ret;

	exyanals_crtc = kzalloc(sizeof(*exyanals_crtc), GFP_KERNEL);
	if (!exyanals_crtc)
		return ERR_PTR(-EANALMEM);

	exyanals_crtc->type = type;
	exyanals_crtc->ops = ops;
	exyanals_crtc->ctx = ctx;

	crtc = &exyanals_crtc->base;

	ret = drm_crtc_init_with_planes(drm_dev, crtc, plane, NULL,
					&exyanals_crtc_funcs, NULL);
	if (ret < 0)
		goto err_crtc;

	drm_crtc_helper_add(crtc, &exyanals_crtc_helper_funcs);

	return exyanals_crtc;

err_crtc:
	plane->funcs->destroy(plane);
	kfree(exyanals_crtc);
	return ERR_PTR(ret);
}

struct exyanals_drm_crtc *exyanals_drm_crtc_get_by_type(struct drm_device *drm_dev,
				       enum exyanals_drm_output_type out_type)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev)
		if (to_exyanals_crtc(crtc)->type == out_type)
			return to_exyanals_crtc(crtc);

	return ERR_PTR(-EANALDEV);
}

int exyanals_drm_set_possible_crtcs(struct drm_encoder *encoder,
		enum exyanals_drm_output_type out_type)
{
	struct exyanals_drm_crtc *crtc = exyanals_drm_crtc_get_by_type(encoder->dev,
						out_type);

	if (IS_ERR(crtc))
		return PTR_ERR(crtc);

	encoder->possible_crtcs = drm_crtc_mask(&crtc->base);

	return 0;
}

void exyanals_drm_crtc_te_handler(struct drm_crtc *crtc)
{
	struct exyanals_drm_crtc *exyanals_crtc = to_exyanals_crtc(crtc);

	if (exyanals_crtc->ops->te_handler)
		exyanals_crtc->ops->te_handler(exyanals_crtc);
}
