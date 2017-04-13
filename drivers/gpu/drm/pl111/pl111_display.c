/*
 * (C) COPYRIGHT 2012-2013 ARM Limited. All rights reserved.
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms of
 * such GNU licence.
 *
 */

#include <linux/amba/clcd-regs.h>
#include <linux/clk.h>
#include <linux/version.h>
#include <linux/dma-buf.h>
#include <linux/of_graph.h>

#include <drm/drmP.h>
#include <drm/drm_panel.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

#include "pl111_drm.h"

irqreturn_t pl111_irq(int irq, void *data)
{
	struct pl111_drm_dev_private *priv = data;
	u32 irq_stat;
	irqreturn_t status = IRQ_NONE;

	irq_stat = readl(priv->regs + CLCD_PL111_MIS);

	if (!irq_stat)
		return IRQ_NONE;

	if (irq_stat & CLCD_IRQ_NEXTBASE_UPDATE) {
		drm_crtc_handle_vblank(&priv->pipe.crtc);

		status = IRQ_HANDLED;
	}

	/* Clear the interrupt once done */
	writel(irq_stat, priv->regs + CLCD_PL111_ICR);

	return status;
}

static u32 pl111_get_fb_offset(struct drm_plane_state *pstate)
{
	struct drm_framebuffer *fb = pstate->fb;
	struct drm_gem_cma_object *obj = drm_fb_cma_get_gem_obj(fb, 0);

	return (obj->paddr +
		fb->offsets[0] +
		fb->format->cpp[0] * pstate->src_x +
		fb->pitches[0] * pstate->src_y);
}

static int pl111_display_check(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *pstate,
			       struct drm_crtc_state *cstate)
{
	const struct drm_display_mode *mode = &cstate->mode;
	struct drm_framebuffer *old_fb = pipe->plane.state->fb;
	struct drm_framebuffer *fb = pstate->fb;

	if (mode->hdisplay % 16)
		return -EINVAL;

	if (fb) {
		u32 offset = pl111_get_fb_offset(pstate);

		/* FB base address must be dword aligned. */
		if (offset & 3)
			return -EINVAL;

		/* There's no pitch register -- the mode's hdisplay
		 * controls it.
		 */
		if (fb->pitches[0] != mode->hdisplay * fb->format->cpp[0])
			return -EINVAL;

		/* We can't change the FB format in a flicker-free
		 * manner (and only update it during CRTC enable).
		 */
		if (old_fb && old_fb->format != fb->format)
			cstate->mode_changed = true;
	}

	return 0;
}

static void pl111_display_enable(struct drm_simple_display_pipe *pipe,
				 struct drm_crtc_state *cstate)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_plane *plane = &pipe->plane;
	struct drm_device *drm = crtc->dev;
	struct pl111_drm_dev_private *priv = drm->dev_private;
	const struct drm_display_mode *mode = &cstate->mode;
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_connector *connector = &priv->connector.connector;
	u32 cntl;
	u32 ppl, hsw, hfp, hbp;
	u32 lpp, vsw, vfp, vbp;
	u32 cpl;
	int ret;

	ret = clk_set_rate(priv->clk, mode->clock * 1000);
	if (ret) {
		dev_err(drm->dev,
			"Failed to set pixel clock rate to %d: %d\n",
			mode->clock * 1000, ret);
	}

	clk_prepare_enable(priv->clk);

	ppl = (mode->hdisplay / 16) - 1;
	hsw = mode->hsync_end - mode->hsync_start - 1;
	hfp = mode->hsync_start - mode->hdisplay - 1;
	hbp = mode->htotal - mode->hsync_end - 1;

	lpp = mode->vdisplay - 1;
	vsw = mode->vsync_end - mode->vsync_start - 1;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;

	cpl = mode->hdisplay - 1;

	writel((ppl << 2) |
	       (hsw << 8) |
	       (hfp << 16) |
	       (hbp << 24),
	       priv->regs + CLCD_TIM0);
	writel(lpp |
	       (vsw << 10) |
	       (vfp << 16) |
	       (vbp << 24),
	       priv->regs + CLCD_TIM1);
	/* XXX: We currently always use CLCDCLK with no divisor.  We
	 * could probably reduce power consumption by using HCLK
	 * (apb_pclk) with a divisor when it gets us near our target
	 * pixel clock.
	 */
	writel(((mode->flags & DRM_MODE_FLAG_NHSYNC) ? TIM2_IHS : 0) |
	       ((mode->flags & DRM_MODE_FLAG_NVSYNC) ? TIM2_IVS : 0) |
	       ((connector->display_info.bus_flags &
		 DRM_BUS_FLAG_DE_LOW) ? TIM2_IOE : 0) |
	       ((connector->display_info.bus_flags &
		 DRM_BUS_FLAG_PIXDATA_NEGEDGE) ? TIM2_IPC : 0) |
	       TIM2_BCD |
	       (cpl << 16),
	       priv->regs + CLCD_TIM2);
	writel(0, priv->regs + CLCD_TIM3);

	drm_panel_prepare(priv->connector.panel);

	/* Enable and Power Up */
	cntl = CNTL_LCDEN | CNTL_LCDTFT | CNTL_LCDPWR | CNTL_LCDVCOMP(1);

	/* Note that the the hardware's format reader takes 'r' from
	 * the low bit, while DRM formats list channels from high bit
	 * to low bit as you read left to right.
	 */
	switch (fb->format->format) {
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_XBGR8888:
		cntl |= CNTL_LCDBPP24;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		cntl |= CNTL_LCDBPP24 | CNTL_BGR;
		break;
	case DRM_FORMAT_BGR565:
		cntl |= CNTL_LCDBPP16_565;
		break;
	case DRM_FORMAT_RGB565:
		cntl |= CNTL_LCDBPP16_565 | CNTL_BGR;
		break;
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_XBGR1555:
		cntl |= CNTL_LCDBPP16;
		break;
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_XRGB1555:
		cntl |= CNTL_LCDBPP16 | CNTL_BGR;
		break;
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_XBGR4444:
		cntl |= CNTL_LCDBPP16_444;
		break;
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_XRGB4444:
		cntl |= CNTL_LCDBPP16_444 | CNTL_BGR;
		break;
	default:
		WARN_ONCE(true, "Unknown FB format 0x%08x\n",
			  fb->format->format);
		break;
	}

	writel(cntl, priv->regs + CLCD_PL111_CNTL);

	drm_panel_enable(priv->connector.panel);

	drm_crtc_vblank_on(crtc);
}

void pl111_display_disable(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct pl111_drm_dev_private *priv = drm->dev_private;

	drm_crtc_vblank_off(crtc);

	drm_panel_disable(priv->connector.panel);

	/* Disable and Power Down */
	writel(0, priv->regs + CLCD_PL111_CNTL);

	drm_panel_unprepare(priv->connector.panel);

	clk_disable_unprepare(priv->clk);
}

static void pl111_display_update(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *old_pstate)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct pl111_drm_dev_private *priv = drm->dev_private;
	struct drm_pending_vblank_event *event = crtc->state->event;
	struct drm_plane *plane = &pipe->plane;
	struct drm_plane_state *pstate = plane->state;
	struct drm_framebuffer *fb = pstate->fb;

	if (fb) {
		u32 addr = pl111_get_fb_offset(pstate);

		writel(addr, priv->regs + CLCD_UBAS);
	}

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (crtc->state->active && drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

int pl111_enable_vblank(struct drm_device *drm, unsigned int crtc)
{
	struct pl111_drm_dev_private *priv = drm->dev_private;

	writel(CLCD_IRQ_NEXTBASE_UPDATE, priv->regs + CLCD_PL111_IENB);

	return 0;
}

void pl111_disable_vblank(struct drm_device *drm, unsigned int crtc)
{
	struct pl111_drm_dev_private *priv = drm->dev_private;

	writel(0, priv->regs + CLCD_PL111_IENB);
}

static int pl111_display_prepare_fb(struct drm_simple_display_pipe *pipe,
				    struct drm_plane_state *plane_state)
{
	return drm_fb_cma_prepare_fb(&pipe->plane, plane_state);
}

const struct drm_simple_display_pipe_funcs pl111_display_funcs = {
	.check = pl111_display_check,
	.enable = pl111_display_enable,
	.disable = pl111_display_disable,
	.update = pl111_display_update,
	.prepare_fb = pl111_display_prepare_fb,
};

int pl111_display_init(struct drm_device *drm)
{
	struct pl111_drm_dev_private *priv = drm->dev_private;
	struct device *dev = drm->dev;
	struct device_node *endpoint;
	u32 tft_r0b0g0[3];
	int ret;
	static const u32 formats[] = {
		DRM_FORMAT_ABGR8888,
		DRM_FORMAT_XBGR8888,
		DRM_FORMAT_ARGB8888,
		DRM_FORMAT_XRGB8888,
		DRM_FORMAT_BGR565,
		DRM_FORMAT_RGB565,
		DRM_FORMAT_ABGR1555,
		DRM_FORMAT_XBGR1555,
		DRM_FORMAT_ARGB1555,
		DRM_FORMAT_XRGB1555,
		DRM_FORMAT_ABGR4444,
		DRM_FORMAT_XBGR4444,
		DRM_FORMAT_ARGB4444,
		DRM_FORMAT_XRGB4444,
	};

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	if (of_property_read_u32_array(endpoint,
				       "arm,pl11x,tft-r0g0b0-pads",
				       tft_r0b0g0,
				       ARRAY_SIZE(tft_r0b0g0)) != 0) {
		dev_err(dev, "arm,pl11x,tft-r0g0b0-pads should be 3 ints\n");
		of_node_put(endpoint);
		return -ENOENT;
	}
	of_node_put(endpoint);

	if (tft_r0b0g0[0] != 0 ||
	    tft_r0b0g0[1] != 8 ||
	    tft_r0b0g0[2] != 16) {
		dev_err(dev, "arm,pl11x,tft-r0g0b0-pads != [0,8,16] not yet supported\n");
		return -EINVAL;
	}

	ret = drm_simple_display_pipe_init(drm, &priv->pipe,
					   &pl111_display_funcs,
					   formats, ARRAY_SIZE(formats),
					   &priv->connector.connector);
	if (ret)
		return ret;

	return 0;
}
