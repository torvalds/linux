// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on sources as follows:
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Copyright (C) 2007 Amos Lee <amos_lee@storlinksemi.com>
 * Copyright (C) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 * Copyright (C) 2017 Eric Anholt
 */

#include <linux/clk.h>
#include <linux/dma-buf.h>
#include <linux/of_graph.h>
#include <linux/delay.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "tve200_drm.h"

irqreturn_t tve200_irq(int irq, void *data)
{
	struct tve200_drm_dev_private *priv = data;
	u32 stat;
	u32 val;

	stat = readl(priv->regs + TVE200_INT_STAT);

	if (!stat)
		return IRQ_NONE;

	/*
	 * Vblank IRQ
	 *
	 * The hardware is a bit tilted: the line stays high after clearing
	 * the vblank IRQ, firing many more interrupts. We counter this
	 * by toggling the IRQ back and forth from firing at vblank and
	 * firing at start of active image, which works around the problem
	 * since those occur strictly in sequence, and we get two IRQs for each
	 * frame, one at start of Vblank (that we make call into the CRTC) and
	 * another one at the start of the image (that we discard).
	 */
	if (stat & TVE200_INT_V_STATUS) {
		val = readl(priv->regs + TVE200_CTRL);
		/* We have an actual start of vsync */
		if (!(val & TVE200_VSTSTYPE_BITS)) {
			drm_crtc_handle_vblank(&priv->crtc);
			/* Toggle trigger to start of active image */
			val |= TVE200_VSTSTYPE_VAI;
		} else {
			/* Toggle trigger back to start of vsync */
			val &= ~TVE200_VSTSTYPE_BITS;
		}
		writel(val, priv->regs + TVE200_CTRL);
	} else
		dev_err(priv->drm->dev, "stray IRQ %08x\n", stat);

	/* Clear the interrupt once done */
	writel(stat, priv->regs + TVE200_INT_CLR);

	return IRQ_HANDLED;
}

static int tve200_plane_helper_atomic_check(struct drm_plane *plane,
					    struct drm_atomic_commit *commit)
{
	struct drm_plane_state *pstate = drm_atomic_get_new_plane_state(commit, plane);
	struct drm_plane_state *old_pstate = drm_atomic_get_old_plane_state(commit, plane);
	struct drm_crtc_state *cstate = NULL;
	const struct drm_display_mode *mode;
	struct drm_framebuffer *old_fb = old_pstate->fb;
	struct drm_framebuffer *fb = pstate->fb;
	int ret;

	if (pstate->crtc) {
		cstate = drm_atomic_get_crtc_state(commit, pstate->crtc);
		if (IS_ERR(cstate))
			return PTR_ERR(cstate);
	}

	ret = drm_atomic_helper_check_plane_state(pstate, cstate,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return ret;

	if (!pstate->visible || !cstate)
		return 0;

	mode = &cstate->mode;

	/*
	 * We support these specific resolutions and nothing else.
	 */
	if (!(mode->hdisplay == 352 && mode->vdisplay == 240) && /* SIF(525) */
	    !(mode->hdisplay == 352 && mode->vdisplay == 288) && /* CIF(625) */
	    !(mode->hdisplay == 640 && mode->vdisplay == 480) && /* VGA */
	    !(mode->hdisplay == 720 && mode->vdisplay == 480) && /* D1 */
	    !(mode->hdisplay == 720 && mode->vdisplay == 576)) { /* D1 */
		DRM_DEBUG_KMS("unsupported display mode (%u x %u)\n",
			mode->hdisplay, mode->vdisplay);
		return -EINVAL;
	}

	if (fb) {
		u32 offset = drm_fb_dma_get_gem_addr(fb, pstate, 0);

		/* FB base address must be dword aligned. */
		if (offset & 3) {
			DRM_DEBUG_KMS("FB not 32-bit aligned\n");
			return -EINVAL;
		}

		/*
		 * There's no pitch register, the mode's hdisplay
		 * controls this.
		 */
		if (fb->pitches[0] != mode->hdisplay * fb->format->cpp[0]) {
			DRM_DEBUG_KMS("can't handle pitches\n");
			return -EINVAL;
		}

		/*
		 * We can't change the FB format in a flicker-free
		 * manner (and only update it during CRTC enable).
		 */
		if (old_fb && old_fb->format != fb->format)
			cstate->mode_changed = true;
	}

	return 0;
}

static void tve200_plane_helper_atomic_update(struct drm_plane *plane,
					      struct drm_atomic_commit *commit)
{
	struct tve200_drm_dev_private *priv = plane->dev->dev_private;
	struct drm_plane_state *pstate = drm_atomic_get_new_plane_state(commit, plane);
	struct drm_framebuffer *fb = pstate->fb;

	if (!fb)
		return;

	/* For RGB, the Y component is used as base address */
	writel(drm_fb_dma_get_gem_addr(fb, pstate, 0),
	       priv->regs + TVE200_Y_FRAME_BASE_ADDR);

	/* For three plane YUV we need two more addresses */
	if (fb->format->format == DRM_FORMAT_YUV420) {
		writel(drm_fb_dma_get_gem_addr(fb, pstate, 1),
		       priv->regs + TVE200_U_FRAME_BASE_ADDR);
		writel(drm_fb_dma_get_gem_addr(fb, pstate, 2),
		       priv->regs + TVE200_V_FRAME_BASE_ADDR);
	}
}

static void tve200_crtc_helper_atomic_flush(struct drm_crtc *crtc,
					    struct drm_atomic_commit *commit)
{
	struct drm_crtc_state *cstate = drm_atomic_get_new_crtc_state(commit, crtc);
	struct drm_pending_vblank_event *event = cstate->event;

	if (!event)
		return;

	cstate->event = NULL;

	spin_lock_irq(&crtc->dev->event_lock);
	if (cstate->active && drm_crtc_vblank_get(crtc) == 0)
		drm_crtc_arm_vblank_event(crtc, event);
	else
		drm_crtc_send_vblank_event(crtc, event);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static const struct drm_plane_helper_funcs tve200_plane_helper_funcs = {
	.prepare_fb	= drm_gem_plane_helper_prepare_fb,
	.atomic_check	= tve200_plane_helper_atomic_check,
	.atomic_update	= tve200_plane_helper_atomic_update,
};

static const struct drm_plane_funcs tve200_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.atomic_create_state	= drm_atomic_helper_plane_create_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static void tve200_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					     struct drm_atomic_commit *commit)
{
	struct drm_device *drm = crtc->dev;
	struct tve200_drm_dev_private *priv = drm->dev_private;
	struct drm_crtc_state *cstate = drm_atomic_get_new_crtc_state(commit, crtc);
	const struct drm_display_mode *mode = &cstate->mode;
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(commit, &priv->plane);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_connector *connector = priv->connector;
	u32 format = fb->format->format;
	u32 ctrl1 = 0;
	int retries;

	clk_prepare_enable(priv->clk);

	/* Reset the TVE200 and wait for it to come back online */
	writel(TVE200_CTRL_4_RESET, priv->regs + TVE200_CTRL_4);
	for (retries = 0; retries < 5; retries++) {
		usleep_range(30000, 50000);
		if (readl(priv->regs + TVE200_CTRL_4) & TVE200_CTRL_4_RESET)
			continue;
		else
			break;
	}
	if (retries == 5 &&
	    readl(priv->regs + TVE200_CTRL_4) & TVE200_CTRL_4_RESET) {
		dev_err(drm->dev, "can't get hardware out of reset\n");
		return;
	}

	/* Function 1 */
	ctrl1 |= TVE200_CTRL_CSMODE;
	/* Interlace mode for CCIR656: parameterize? */
	ctrl1 |= TVE200_CTRL_NONINTERLACE;
	/* 32 words per burst */
	ctrl1 |= TVE200_CTRL_BURST_32_WORDS;
	/* 16 retries */
	ctrl1 |= TVE200_CTRL_RETRYCNT_16;
	/* NTSC mode: parametrize? */
	ctrl1 |= TVE200_CTRL_NTSC;

	/* Vsync IRQ at start of Vsync at first */
	ctrl1 |= TVE200_VSTSTYPE_VSYNC;

	if (connector->display_info.bus_flags &
	    DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		ctrl1 |= TVE200_CTRL_TVCLKP;

	if ((mode->hdisplay == 352 && mode->vdisplay == 240) || /* SIF(525) */
	    (mode->hdisplay == 352 && mode->vdisplay == 288)) { /* CIF(625) */
		ctrl1 |= TVE200_CTRL_IPRESOL_CIF;
		dev_info(drm->dev, "CIF mode\n");
	} else if (mode->hdisplay == 640 && mode->vdisplay == 480) {
		ctrl1 |= TVE200_CTRL_IPRESOL_VGA;
		dev_info(drm->dev, "VGA mode\n");
	} else if ((mode->hdisplay == 720 && mode->vdisplay == 480) ||
		   (mode->hdisplay == 720 && mode->vdisplay == 576)) {
		ctrl1 |= TVE200_CTRL_IPRESOL_D1;
		dev_info(drm->dev, "D1 mode\n");
	}

	if (format & DRM_FORMAT_BIG_ENDIAN) {
		ctrl1 |= TVE200_CTRL_BBBP;
		format &= ~DRM_FORMAT_BIG_ENDIAN;
	}

	switch (format) {
	case DRM_FORMAT_XRGB8888:
		ctrl1 |= TVE200_IPDMOD_RGB888;
		break;
	case DRM_FORMAT_RGB565:
		ctrl1 |= TVE200_IPDMOD_RGB565;
		break;
	case DRM_FORMAT_XRGB1555:
		ctrl1 |= TVE200_IPDMOD_RGB555;
		break;
	case DRM_FORMAT_XBGR8888:
		ctrl1 |= TVE200_IPDMOD_RGB888 | TVE200_BGR;
		break;
	case DRM_FORMAT_BGR565:
		ctrl1 |= TVE200_IPDMOD_RGB565 | TVE200_BGR;
		break;
	case DRM_FORMAT_XBGR1555:
		ctrl1 |= TVE200_IPDMOD_RGB555 | TVE200_BGR;
		break;
	case DRM_FORMAT_YUYV:
		ctrl1 |= TVE200_IPDMOD_YUV422;
		ctrl1 |= TVE200_CTRL_YCBCRODR_CR0Y1CB0Y0;
		break;
	case DRM_FORMAT_YVYU:
		ctrl1 |= TVE200_IPDMOD_YUV422;
		ctrl1 |= TVE200_CTRL_YCBCRODR_CB0Y1CR0Y0;
		break;
	case DRM_FORMAT_UYVY:
		ctrl1 |= TVE200_IPDMOD_YUV422;
		ctrl1 |= TVE200_CTRL_YCBCRODR_Y1CR0Y0CB0;
		break;
	case DRM_FORMAT_VYUY:
		ctrl1 |= TVE200_IPDMOD_YUV422;
		ctrl1 |= TVE200_CTRL_YCBCRODR_Y1CB0Y0CR0;
		break;
	case DRM_FORMAT_YUV420:
		ctrl1 |= TVE200_CTRL_YUV420;
		ctrl1 |= TVE200_IPDMOD_YUV420;
		break;
	default:
		dev_err(drm->dev, "Unknown FB format 0x%08x\n",
			fb->format->format);
		break;
	}

	ctrl1 |= TVE200_TVEEN;

	/* Turn it on */
	writel(ctrl1, priv->regs + TVE200_CTRL);

	drm_crtc_vblank_on(crtc);
}

static void tve200_crtc_helper_atomic_disable(struct drm_crtc *crtc,
					      struct drm_atomic_commit *commit)
{
	struct drm_device *drm = crtc->dev;
	struct tve200_drm_dev_private *priv = drm->dev_private;

	drm_crtc_vblank_off(crtc);

	/* Disable put into reset and Power Down */
	writel(0, priv->regs + TVE200_CTRL);
	writel(TVE200_CTRL_4_RESET, priv->regs + TVE200_CTRL_4);

	clk_disable_unprepare(priv->clk);
}

static int tve200_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *drm = crtc->dev;
	struct tve200_drm_dev_private *priv = drm->dev_private;

	/* Clear any IRQs and enable */
	writel(0xFF, priv->regs + TVE200_INT_CLR);
	writel(TVE200_INT_V_STATUS, priv->regs + TVE200_INT_EN);
	return 0;
}

static void tve200_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *drm = crtc->dev;
	struct tve200_drm_dev_private *priv = drm->dev_private;

	writel(0, priv->regs + TVE200_INT_EN);
}

static int tve200_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_atomic_commit *commit)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(commit, crtc);
	int ret;

	if (crtc_state->enable) {
		ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
		if (ret)
			return ret;
	}

	return drm_atomic_add_affected_planes(commit, crtc);
}

static const struct drm_crtc_helper_funcs tve200_crtc_helper_funcs = {
	.atomic_check	= tve200_crtc_helper_atomic_check,
	.atomic_enable	= tve200_crtc_helper_atomic_enable,
	.atomic_disable	= tve200_crtc_helper_atomic_disable,
	.atomic_flush	= tve200_crtc_helper_atomic_flush,
};

static const struct drm_crtc_funcs tve200_crtc_funcs = {
	.atomic_create_state	= drm_atomic_helper_crtc_create_state,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= tve200_crtc_enable_vblank,
	.disable_vblank		= tve200_crtc_disable_vblank,
};

static const struct drm_encoder_funcs tve200_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int tve200_display_init(struct drm_device *drm)
{
	struct tve200_drm_dev_private *priv = drm->dev_private;
	int ret;
	static const u32 formats[] = {
		DRM_FORMAT_XRGB8888,
		DRM_FORMAT_XBGR8888,
		DRM_FORMAT_RGB565,
		DRM_FORMAT_BGR565,
		DRM_FORMAT_XRGB1555,
		DRM_FORMAT_XBGR1555,
		/*
		 * The controller actually supports any YCbCr ordering,
		 * for packed YCbCr. This just lists the orderings that
		 * DRM supports.
		 */
		DRM_FORMAT_YUYV,
		DRM_FORMAT_YVYU,
		DRM_FORMAT_UYVY,
		DRM_FORMAT_VYUY,
		/* This uses three planes */
		DRM_FORMAT_YUV420,
	};

	ret = drm_universal_plane_init(drm, &priv->plane, 0,
				       &tve200_plane_funcs,
				       formats, ARRAY_SIZE(formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(&priv->plane, &tve200_plane_helper_funcs);

	ret = drm_crtc_init_with_planes(drm, &priv->crtc,
					&priv->plane, NULL,
					&tve200_crtc_funcs, NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(&priv->crtc, &tve200_crtc_helper_funcs);

	ret = drm_encoder_init(drm, &priv->encoder,
			       &tve200_encoder_funcs,
			       DRM_MODE_ENCODER_NONE, NULL);
	if (ret)
		return ret;

	priv->encoder.possible_crtcs = drm_crtc_mask(&priv->crtc);

	return 0;
}
