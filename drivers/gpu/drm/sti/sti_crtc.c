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
#include "sti_crtc.h"
#include "sti_drv.h"
#include "sti_vid.h"
#include "sti_vtg.h"

static void sti_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_state)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);

	DRM_DEBUG_DRIVER("\n");

	mixer->status = STI_MIXER_READY;

	drm_crtc_vblank_on(crtc);
}

static void sti_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);

	DRM_DEBUG_DRIVER("\n");

	mixer->status = STI_MIXER_DISABLING;
}

static int
sti_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct device *dev = mixer->dev;
	struct sti_compositor *compo = dev_get_drvdata(dev);
	struct clk *compo_clk, *pix_clk;
	int rate = mode->clock * 1000;

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

	if (mixer->id == STI_MIXER_MAIN) {
		compo_clk = compo->clk_compo_main;
		pix_clk = compo->clk_pix_main;
	} else {
		compo_clk = compo->clk_compo_aux;
		pix_clk = compo->clk_pix_aux;
	}

	/* Prepare and enable the compo IP clock */
	if (clk_prepare_enable(compo_clk)) {
		DRM_INFO("Failed to prepare/enable compositor clk\n");
		goto compo_error;
	}

	/* Set rate and prepare/enable pixel clock */
	if (clk_set_rate(pix_clk, rate) < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for pix clk\n", rate);
		goto pix_error;
	}
	if (clk_prepare_enable(pix_clk)) {
		DRM_ERROR("Failed to prepare/enable pix clk\n");
		goto pix_error;
	}

	sti_vtg_set_config(compo->vtg[mixer->id], &crtc->mode);

	if (sti_mixer_active_video_area(mixer, &crtc->mode)) {
		DRM_ERROR("Can't set active video area\n");
		goto mixer_error;
	}

	return 0;

mixer_error:
	clk_disable_unprepare(pix_clk);
pix_error:
	clk_disable_unprepare(compo_clk);
compo_error:
	return -EINVAL;
}

static void sti_crtc_disable(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct device *dev = mixer->dev;
	struct sti_compositor *compo = dev_get_drvdata(dev);

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

	mixer->status = STI_MIXER_DISABLED;
}

static void
sti_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	sti_crtc_mode_set(crtc, &crtc->state->adjusted_mode);
}

static void sti_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_crtc_state)
{
	struct drm_device *drm_dev = crtc->dev;
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct sti_compositor *compo = dev_get_drvdata(mixer->dev);
	struct drm_plane *p;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	DRM_DEBUG_DRIVER("\n");

	/* perform plane actions */
	list_for_each_entry(p, &drm_dev->mode_config.plane_list, head) {
		struct sti_plane *plane = to_sti_plane(p);

		switch (plane->status) {
		case STI_PLANE_UPDATED:
			/* ignore update for other CRTC */
			if (p->state->crtc != crtc)
				continue;

			/* update planes tag as updated */
			DRM_DEBUG_DRIVER("update plane %s\n",
					 sti_plane_to_str(plane));

			if (sti_mixer_set_plane_depth(mixer, plane)) {
				DRM_ERROR("Cannot set plane %s depth\n",
					  sti_plane_to_str(plane));
				break;
			}

			if (sti_mixer_set_plane_status(mixer, plane, true)) {
				DRM_ERROR("Cannot enable plane %s at mixer\n",
					  sti_plane_to_str(plane));
				break;
			}

			/* if plane is HQVDP_0 then commit the vid[0] */
			if (plane->desc == STI_HQVDP_0)
				sti_vid_commit(compo->vid[0], p->state);

			plane->status = STI_PLANE_READY;

			break;
		case STI_PLANE_DISABLING:
			/* disabling sequence for planes tag as disabling */
			DRM_DEBUG_DRIVER("disable plane %s from mixer\n",
					 sti_plane_to_str(plane));

			if (sti_mixer_set_plane_status(mixer, plane, false)) {
				DRM_ERROR("Cannot disable plane %s at mixer\n",
					  sti_plane_to_str(plane));
				continue;
			}

			if (plane->desc == STI_CURSOR)
				/* tag plane status for disabled */
				plane->status = STI_PLANE_DISABLED;
			else
				/* tag plane status for flushing */
				plane->status = STI_PLANE_FLUSHING;

			/* if plane is HQVDP_0 then disable the vid[0] */
			if (plane->desc == STI_HQVDP_0)
				sti_vid_disable(compo->vid[0]);

			break;
		default:
			/* Other status case are not handled */
			break;
		}
	}

	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;

		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	}
}

static const struct drm_crtc_helper_funcs sti_crtc_helper_funcs = {
	.mode_set_nofb = sti_crtc_mode_set_nofb,
	.atomic_flush = sti_crtc_atomic_flush,
	.atomic_enable = sti_crtc_atomic_enable,
	.atomic_disable = sti_crtc_atomic_disable,
};

static void sti_crtc_destroy(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("\n");
	drm_crtc_cleanup(crtc);
}

static int sti_crtc_set_property(struct drm_crtc *crtc,
				 struct drm_property *property,
				 uint64_t val)
{
	DRM_DEBUG_KMS("\n");
	return 0;
}

int sti_crtc_vblank_cb(struct notifier_block *nb,
		       unsigned long event, void *data)
{
	struct sti_compositor *compo;
	struct drm_crtc *crtc = data;
	struct sti_mixer *mixer;
	struct sti_private *priv;
	unsigned int pipe;

	priv = crtc->dev->dev_private;
	pipe = drm_crtc_index(crtc);
	compo = container_of(nb, struct sti_compositor, vtg_vblank_nb[pipe]);
	mixer = compo->mixer[pipe];

	if ((event != VTG_TOP_FIELD_EVENT) &&
	    (event != VTG_BOTTOM_FIELD_EVENT)) {
		DRM_ERROR("unknown event: %lu\n", event);
		return -EINVAL;
	}

	drm_crtc_handle_vblank(crtc);

	if (mixer->status == STI_MIXER_DISABLING) {
		struct drm_plane *p;

		/* Disable mixer only if all overlay planes (GDP and VDP)
		 * are disabled */
		list_for_each_entry(p, &crtc->dev->mode_config.plane_list,
				    head) {
			struct sti_plane *plane = to_sti_plane(p);

			if ((plane->desc & STI_PLANE_TYPE_MASK) <= STI_VDP)
				if (plane->status != STI_PLANE_DISABLED)
					return 0;
		}
		sti_crtc_disable(crtc);
	}

	return 0;
}

int sti_crtc_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	struct sti_private *dev_priv = dev->dev_private;
	struct sti_compositor *compo = dev_priv->compo;
	struct notifier_block *vtg_vblank_nb = &compo->vtg_vblank_nb[pipe];
	struct drm_crtc *crtc = &compo->mixer[pipe]->drm_crtc;
	struct sti_vtg *vtg = compo->vtg[pipe];

	DRM_DEBUG_DRIVER("\n");

	if (sti_vtg_register_client(vtg, vtg_vblank_nb, crtc)) {
		DRM_ERROR("Cannot register VTG notifier\n");
		return -EINVAL;
	}

	return 0;
}

void sti_crtc_disable_vblank(struct drm_device *drm_dev, unsigned int pipe)
{
	struct sti_private *priv = drm_dev->dev_private;
	struct sti_compositor *compo = priv->compo;
	struct notifier_block *vtg_vblank_nb = &compo->vtg_vblank_nb[pipe];
	struct sti_vtg *vtg = compo->vtg[pipe];

	DRM_DEBUG_DRIVER("\n");

	if (sti_vtg_unregister_client(vtg, vtg_vblank_nb))
		DRM_DEBUG_DRIVER("Warning: cannot unregister VTG notifier\n");
}

static int sti_crtc_late_register(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct sti_compositor *compo = dev_get_drvdata(mixer->dev);

	if (drm_crtc_index(crtc) == 0)
		return sti_compositor_debugfs_init(compo, crtc->dev->primary);

	return 0;
}

static const struct drm_crtc_funcs sti_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.destroy = sti_crtc_destroy,
	.set_property = sti_crtc_set_property,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.late_register = sti_crtc_late_register,
};

bool sti_crtc_is_main(struct drm_crtc *crtc)
{
	struct sti_mixer *mixer = to_sti_mixer(crtc);

	if (mixer->id == STI_MIXER_MAIN)
		return true;

	return false;
}

int sti_crtc_init(struct drm_device *drm_dev, struct sti_mixer *mixer,
		  struct drm_plane *primary, struct drm_plane *cursor)
{
	struct drm_crtc *crtc = &mixer->drm_crtc;
	int res;

	res = drm_crtc_init_with_planes(drm_dev, crtc, primary, cursor,
					&sti_crtc_funcs, NULL);
	if (res) {
		DRM_ERROR("Can't initialze CRTC\n");
		return -EINVAL;
	}

	drm_crtc_helper_add(crtc, &sti_crtc_helper_funcs);

	DRM_DEBUG_DRIVER("drm CRTC:%d mapped to %s\n",
			 crtc->base.id, sti_mixer_to_str(mixer));

	return 0;
}
