// SPDX-License-Identifier: GPL-2.0-or-later
/* exyyess_drm_crtc.c
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

#include "exyyess_drm_crtc.h"
#include "exyyess_drm_drv.h"
#include "exyyess_drm_plane.h"

static void exyyess_drm_crtc_atomic_enable(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_state)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->enable)
		exyyess_crtc->ops->enable(exyyess_crtc);

	drm_crtc_vblank_on(crtc);
}

static void exyyess_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					   struct drm_crtc_state *old_state)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	drm_crtc_vblank_off(crtc);

	if (exyyess_crtc->ops->disable)
		exyyess_crtc->ops->disable(exyyess_crtc);

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static int exyyess_crtc_atomic_check(struct drm_crtc *crtc,
				     struct drm_crtc_state *state)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (!state->enable)
		return 0;

	if (exyyess_crtc->ops->atomic_check)
		return exyyess_crtc->ops->atomic_check(exyyess_crtc, state);

	return 0;
}

static void exyyess_crtc_atomic_begin(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_crtc_state)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->atomic_begin)
		exyyess_crtc->ops->atomic_begin(exyyess_crtc);
}

static void exyyess_crtc_atomic_flush(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_crtc_state)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->atomic_flush)
		exyyess_crtc->ops->atomic_flush(exyyess_crtc);
}

static enum drm_mode_status exyyess_crtc_mode_valid(struct drm_crtc *crtc,
	const struct drm_display_mode *mode)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->mode_valid)
		return exyyess_crtc->ops->mode_valid(exyyess_crtc, mode);

	return MODE_OK;
}

static bool exyyess_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->mode_fixup)
		return exyyess_crtc->ops->mode_fixup(exyyess_crtc, mode,
				adjusted_mode);

	return true;
}


static const struct drm_crtc_helper_funcs exyyess_crtc_helper_funcs = {
	.mode_valid	= exyyess_crtc_mode_valid,
	.mode_fixup	= exyyess_crtc_mode_fixup,
	.atomic_check	= exyyess_crtc_atomic_check,
	.atomic_begin	= exyyess_crtc_atomic_begin,
	.atomic_flush	= exyyess_crtc_atomic_flush,
	.atomic_enable	= exyyess_drm_crtc_atomic_enable,
	.atomic_disable	= exyyess_drm_crtc_atomic_disable,
};

void exyyess_crtc_handle_event(struct exyyess_drm_crtc *exyyess_crtc)
{
	struct drm_crtc *crtc = &exyyess_crtc->base;
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

static void exyyess_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(exyyess_crtc);
}

static int exyyess_drm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->enable_vblank)
		return exyyess_crtc->ops->enable_vblank(exyyess_crtc);

	return 0;
}

static void exyyess_drm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->disable_vblank)
		exyyess_crtc->ops->disable_vblank(exyyess_crtc);
}

static const struct drm_crtc_funcs exyyess_crtc_funcs = {
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.destroy	= exyyess_drm_crtc_destroy,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = exyyess_drm_crtc_enable_vblank,
	.disable_vblank = exyyess_drm_crtc_disable_vblank,
};

struct exyyess_drm_crtc *exyyess_drm_crtc_create(struct drm_device *drm_dev,
					struct drm_plane *plane,
					enum exyyess_drm_output_type type,
					const struct exyyess_drm_crtc_ops *ops,
					void *ctx)
{
	struct exyyess_drm_crtc *exyyess_crtc;
	struct drm_crtc *crtc;
	int ret;

	exyyess_crtc = kzalloc(sizeof(*exyyess_crtc), GFP_KERNEL);
	if (!exyyess_crtc)
		return ERR_PTR(-ENOMEM);

	exyyess_crtc->type = type;
	exyyess_crtc->ops = ops;
	exyyess_crtc->ctx = ctx;

	crtc = &exyyess_crtc->base;

	ret = drm_crtc_init_with_planes(drm_dev, crtc, plane, NULL,
					&exyyess_crtc_funcs, NULL);
	if (ret < 0)
		goto err_crtc;

	drm_crtc_helper_add(crtc, &exyyess_crtc_helper_funcs);

	return exyyess_crtc;

err_crtc:
	plane->funcs->destroy(plane);
	kfree(exyyess_crtc);
	return ERR_PTR(ret);
}

struct exyyess_drm_crtc *exyyess_drm_crtc_get_by_type(struct drm_device *drm_dev,
				       enum exyyess_drm_output_type out_type)
{
	struct drm_crtc *crtc;

	drm_for_each_crtc(crtc, drm_dev)
		if (to_exyyess_crtc(crtc)->type == out_type)
			return to_exyyess_crtc(crtc);

	return ERR_PTR(-ENODEV);
}

int exyyess_drm_set_possible_crtcs(struct drm_encoder *encoder,
		enum exyyess_drm_output_type out_type)
{
	struct exyyess_drm_crtc *crtc = exyyess_drm_crtc_get_by_type(encoder->dev,
						out_type);

	if (IS_ERR(crtc))
		return PTR_ERR(crtc);

	encoder->possible_crtcs = drm_crtc_mask(&crtc->base);

	return 0;
}

void exyyess_drm_crtc_te_handler(struct drm_crtc *crtc)
{
	struct exyyess_drm_crtc *exyyess_crtc = to_exyyess_crtc(crtc);

	if (exyyess_crtc->ops->te_handler)
		exyyess_crtc->ops->te_handler(exyyess_crtc);
}
