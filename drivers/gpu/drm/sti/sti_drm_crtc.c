/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>

#include "sti_compositor.h"
#include "sti_drm_drv.h"
#include "sti_drm_crtc.h"
#include "sti_vtg.h"

static void sti_drm_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	DRM_DEBUG_KMS("\n");
}

static void sti_drm_crtc_prepare(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct device *dev = mixer->dev;
	struct sti_compositor *compo = dev_get_drvdata(dev);

	mixer->enabled = true;

	/* Prepare and enable the compo IP clock */
	if (mixer->id == STI_MIXER_MAIN) {
		if (clk_prepare_enable(compo->clk_compo_main))
			DRM_INFO("Failed to prepare/enable compo_main clk\n");
	} else {
		if (clk_prepare_enable(compo->clk_compo_aux))
			DRM_INFO("Failed to prepare/enable compo_aux clk\n");
	}

	sti_mixer_clear_all_layers(mixer);
}

static void sti_drm_crtc_commit(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct device *dev = mixer->dev;
	struct sti_compositor *compo = dev_get_drvdata(dev);
	struct sti_layer *layer;

	if ((!mixer || !compo)) {
		DRM_ERROR("Can not find mixer or compositor)\n");
		return;
	}

	/* get GDP which is reserved to the CRTC FB */
	layer = to_sti_layer(crtc->primary);
	if (layer)
		sti_layer_commit(layer);
	else
		DRM_ERROR("Can not find CRTC dedicated plane (GDP0)\n");

	/* Enable layer on mixer */
	if (sti_mixer_set_layer_status(mixer, layer, true))
		DRM_ERROR("Can not enable layer at mixer\n");

	drm_crtc_vblank_on(crtc);
}

static bool sti_drm_crtc_mode_fixup(struct drm_crtc *crtc,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	/* accept the provided drm_display_mode, do not fix it up */
	return true;
}

static int
sti_drm_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct device *dev = mixer->dev;
	struct sti_compositor *compo = dev_get_drvdata(dev);
	struct clk *clk;
	int rate = mode->clock * 1000;
	int res;

	DRM_DEBUG_KMS("CRTC:%d (%s) mode:%d (%s)\n",
		      crtc->base.id, sti_mixer_to_str(mixer),
		      mode->base.id, mode->name);

	DRM_DEBUG_KMS("%d %d %d %d %d %d %d %d %d %d 0x%x 0x%x\n",
		      mode->vrefresh, mode->clock,
		      mode->hdisplay,
		      mode->hsync_start, mode->hsync_end,
		      mode->htotal,
		      mode->vdisplay,
		      mode->vsync_start, mode->vsync_end,
		      mode->vtotal, mode->type, mode->flags);

	/* Set rate and prepare/enable pixel clock */
	if (mixer->id == STI_MIXER_MAIN)
		clk = compo->clk_pix_main;
	else
		clk = compo->clk_pix_aux;

	res = clk_set_rate(clk, rate);
	if (res < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for pix clk\n", rate);
		return -EINVAL;
	}
	if (clk_prepare_enable(clk)) {
		DRM_ERROR("Failed to prepare/enable pix clk\n");
		return -EINVAL;
	}

	sti_vtg_set_config(mixer->id == STI_MIXER_MAIN ?
			compo->vtg_main : compo->vtg_aux, &crtc->mode);

	res = sti_mixer_active_video_area(mixer, &crtc->mode);
	if (res) {
		DRM_ERROR("Can not set active video area\n");
		return -EINVAL;
	}

	return res;
}

static void sti_drm_crtc_disable(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct device *dev = mixer->dev;
	struct sti_compositor *compo = dev_get_drvdata(dev);

	if (!mixer->enabled)
		return;

	DRM_DEBUG_KMS("CRTC:%d (%s)\n", crtc->base.id, sti_mixer_to_str(mixer));

	/* Disable Background */
	sti_mixer_set_background_status(mixer, false);

	drm_crtc_vblank_off(crtc);

	/* Disable pixel clock and compo IP clocks */
	if (mixer->id == STI_MIXER_MAIN) {
		clk_disable_unprepare(compo->clk_pix_main);
		clk_disable_unprepare(compo->clk_compo_main);
	} else {
		clk_disable_unprepare(compo->clk_pix_aux);
		clk_disable_unprepare(compo->clk_compo_aux);
	}

	mixer->enabled = false;
}

static void
sti_drm_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	sti_drm_crtc_prepare(crtc);
	sti_drm_crtc_mode_set(crtc, &crtc->state->adjusted_mode);
}

static void sti_drm_atomic_begin(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);

	if (crtc->state->event) {
		crtc->state->event->pipe = drm_crtc_index(crtc);

		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		mixer->pending_event = crtc->state->event;
		crtc->state->event = NULL;
	}
}

static void sti_drm_atomic_flush(struct drm_crtc *crtc)
{
}

static struct drm_crtc_helper_funcs sti_crtc_helper_funcs = {
	.dpms = sti_drm_crtc_dpms,
	.prepare = sti_drm_crtc_prepare,
	.commit = sti_drm_crtc_commit,
	.mode_fixup = sti_drm_crtc_mode_fixup,
	.mode_set = drm_helper_crtc_mode_set,
	.mode_set_nofb = sti_drm_crtc_mode_set_nofb,
	.mode_set_base = drm_helper_crtc_mode_set_base,
	.disable = sti_drm_crtc_disable,
	.atomic_begin = sti_drm_atomic_begin,
	.atomic_flush = sti_drm_atomic_flush,
};

static void sti_drm_crtc_destroy(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("\n");
	drm_crtc_cleanup(crtc);
}

static int sti_drm_crtc_set_property(struct drm_crtc *crtc,
				     struct drm_property *property,
				     uint64_t val)
{
	DRM_DEBUG_KMS("\n");
	return 0;
}

int sti_drm_crtc_vblank_cb(struct notifier_block *nb,
			   unsigned long event, void *data)
{
	struct drm_device *drm_dev;
	struct sti_compositor *compo =
		container_of(nb, struct sti_compositor, vtg_vblank_nb);
	int *crtc = data;
	unsigned long flags;
	struct sti_drm_private *priv;

	drm_dev = compo->mixer[*crtc]->drm_crtc.dev;
	priv = drm_dev->dev_private;

	if ((event != VTG_TOP_FIELD_EVENT) &&
	    (event != VTG_BOTTOM_FIELD_EVENT)) {
		DRM_ERROR("unknown event: %lu\n", event);
		return -EINVAL;
	}

	drm_handle_vblank(drm_dev, *crtc);

	spin_lock_irqsave(&drm_dev->event_lock, flags);
	if (compo->mixer[*crtc]->pending_event) {
		drm_send_vblank_event(drm_dev, -1,
				compo->mixer[*crtc]->pending_event);
		drm_vblank_put(drm_dev, *crtc);
		compo->mixer[*crtc]->pending_event = NULL;
	}
	spin_unlock_irqrestore(&drm_dev->event_lock, flags);

	return 0;
}

int sti_drm_crtc_enable_vblank(struct drm_device *dev, int crtc)
{
	struct sti_drm_private *dev_priv = dev->dev_private;
	struct sti_compositor *compo = dev_priv->compo;
	struct notifier_block *vtg_vblank_nb = &compo->vtg_vblank_nb;

	if (sti_vtg_register_client(crtc == STI_MIXER_MAIN ?
			compo->vtg_main : compo->vtg_aux,
			vtg_vblank_nb, crtc)) {
		DRM_ERROR("Cannot register VTG notifier\n");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(sti_drm_crtc_enable_vblank);

void sti_drm_crtc_disable_vblank(struct drm_device *dev, int crtc)
{
	struct sti_drm_private *priv = dev->dev_private;
	struct sti_compositor *compo = priv->compo;
	struct notifier_block *vtg_vblank_nb = &compo->vtg_vblank_nb;

	DRM_DEBUG_DRIVER("\n");

	if (sti_vtg_unregister_client(crtc == STI_MIXER_MAIN ?
			compo->vtg_main : compo->vtg_aux, vtg_vblank_nb))
		DRM_DEBUG_DRIVER("Warning: cannot unregister VTG notifier\n");

	/* free the resources of the pending requests */
	if (compo->mixer[crtc]->pending_event) {
		drm_vblank_put(dev, crtc);
		compo->mixer[crtc]->pending_event = NULL;
	}
}
EXPORT_SYMBOL(sti_drm_crtc_disable_vblank);

static struct drm_crtc_funcs sti_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = sti_drm_crtc_destroy,
	.set_property = sti_drm_crtc_set_property,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

bool sti_drm_crtc_is_main(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);

	if (mixer->id == STI_MIXER_MAIN)
		return true;

	return false;
}
EXPORT_SYMBOL(sti_drm_crtc_is_main);

int sti_drm_crtc_init(struct drm_device *drm_dev, struct sti_mixer *mixer,
		struct drm_plane *primary, struct drm_plane *cursor)
{
	struct drm_crtc *crtc = &mixer->drm_crtc;
	int res;

	res = drm_crtc_init_with_planes(drm_dev, crtc, primary, cursor,
			&sti_crtc_funcs);
	if (res) {
		DRM_ERROR("Can not initialze CRTC\n");
		return -EINVAL;
	}

	drm_crtc_helper_add(crtc, &sti_crtc_helper_funcs);

	DRM_DEBUG_DRIVER("drm CRTC:%d mapped to %s\n",
			 crtc->base.id, sti_mixer_to_str(mixer));

	return 0;
}
