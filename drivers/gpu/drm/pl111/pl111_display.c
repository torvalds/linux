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
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
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
		u32 offset = drm_fb_cma_get_gem_addr(fb, pstate, 0);

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
	struct drm_connector *connector = priv->connector;
	u32 cntl;
	u32 ppl, hsw, hfp, hbp;
	u32 lpp, vsw, vfp, vbp;
	u32 cpl, tim2;
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

	spin_lock(&priv->tim2_lock);

	tim2 = readl(priv->regs + CLCD_TIM2);
	tim2 &= (TIM2_BCD | TIM2_PCD_LO_MASK | TIM2_PCD_HI_MASK);

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		tim2 |= TIM2_IHS;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		tim2 |= TIM2_IVS;

	if (connector->display_info.bus_flags & DRM_BUS_FLAG_DE_LOW)
		tim2 |= TIM2_IOE;

	if (connector->display_info.bus_flags & DRM_BUS_FLAG_PIXDATA_NEGEDGE)
		tim2 |= TIM2_IPC;

	tim2 |= cpl << 16;
	writel(tim2, priv->regs + CLCD_TIM2);
	spin_unlock(&priv->tim2_lock);

	writel(0, priv->regs + CLCD_TIM3);

	/* Hard-code TFT panel */
	cntl = CNTL_LCDEN | CNTL_LCDTFT | CNTL_LCDVCOMP(1);

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

	/* Power sequence: first enable and chill */
	writel(cntl, priv->regs + priv->ctrl);

	/*
	 * We expect this delay to stabilize the contrast
	 * voltage Vee as stipulated by the manual
	 */
	msleep(20);

	/* Power Up */
	cntl |= CNTL_LCDPWR;
	writel(cntl, priv->regs + priv->ctrl);

	drm_crtc_vblank_on(crtc);
}

void pl111_display_disable(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct pl111_drm_dev_private *priv = drm->dev_private;
	u32 cntl;

	drm_crtc_vblank_off(crtc);

	/* Power Down */
	cntl = readl(priv->regs + priv->ctrl);
	if (cntl & CNTL_LCDPWR) {
		cntl &= ~CNTL_LCDPWR;
		writel(cntl, priv->regs + priv->ctrl);
	}

	/*
	 * We expect this delay to stabilize the contrast voltage Vee as
	 * stipulated by the manual
	 */
	msleep(20);

	/* Disable */
	writel(0, priv->regs + priv->ctrl);

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
		u32 addr = drm_fb_cma_get_gem_addr(fb, pstate, 0);

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

	writel(CLCD_IRQ_NEXTBASE_UPDATE, priv->regs + priv->ienb);

	return 0;
}

void pl111_disable_vblank(struct drm_device *drm, unsigned int crtc)
{
	struct pl111_drm_dev_private *priv = drm->dev_private;

	writel(0, priv->regs + priv->ienb);
}

static int pl111_display_prepare_fb(struct drm_simple_display_pipe *pipe,
				    struct drm_plane_state *plane_state)
{
	return drm_gem_fb_prepare_fb(&pipe->plane, plane_state);
}

static const struct drm_simple_display_pipe_funcs pl111_display_funcs = {
	.check = pl111_display_check,
	.enable = pl111_display_enable,
	.disable = pl111_display_disable,
	.update = pl111_display_update,
	.prepare_fb = pl111_display_prepare_fb,
};

static int pl111_clk_div_choose_div(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate, bool set_parent)
{
	int best_div = 1, div;
	struct clk_hw *parent = clk_hw_get_parent(hw);
	unsigned long best_prate = 0;
	unsigned long best_diff = ~0ul;
	int max_div = (1 << (TIM2_PCD_LO_BITS + TIM2_PCD_HI_BITS)) - 1;

	for (div = 1; div < max_div; div++) {
		unsigned long this_prate, div_rate, diff;

		if (set_parent)
			this_prate = clk_hw_round_rate(parent, rate * div);
		else
			this_prate = *prate;
		div_rate = DIV_ROUND_UP_ULL(this_prate, div);
		diff = abs(rate - div_rate);

		if (diff < best_diff) {
			best_div = div;
			best_diff = diff;
			best_prate = this_prate;
		}
	}

	*prate = best_prate;
	return best_div;
}

static long pl111_clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	int div = pl111_clk_div_choose_div(hw, rate, prate, true);

	return DIV_ROUND_UP_ULL(*prate, div);
}

static unsigned long pl111_clk_div_recalc_rate(struct clk_hw *hw,
					       unsigned long prate)
{
	struct pl111_drm_dev_private *priv =
		container_of(hw, struct pl111_drm_dev_private, clk_div);
	u32 tim2 = readl(priv->regs + CLCD_TIM2);
	int div;

	if (tim2 & TIM2_BCD)
		return prate;

	div = tim2 & TIM2_PCD_LO_MASK;
	div |= (tim2 & TIM2_PCD_HI_MASK) >>
		(TIM2_PCD_HI_SHIFT - TIM2_PCD_LO_BITS);
	div += 2;

	return DIV_ROUND_UP_ULL(prate, div);
}

static int pl111_clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long prate)
{
	struct pl111_drm_dev_private *priv =
		container_of(hw, struct pl111_drm_dev_private, clk_div);
	int div = pl111_clk_div_choose_div(hw, rate, &prate, false);
	u32 tim2;

	spin_lock(&priv->tim2_lock);
	tim2 = readl(priv->regs + CLCD_TIM2);
	tim2 &= ~(TIM2_BCD | TIM2_PCD_LO_MASK | TIM2_PCD_HI_MASK);

	if (div == 1) {
		tim2 |= TIM2_BCD;
	} else {
		div -= 2;
		tim2 |= div & TIM2_PCD_LO_MASK;
		tim2 |= (div >> TIM2_PCD_LO_BITS) << TIM2_PCD_HI_SHIFT;
	}

	writel(tim2, priv->regs + CLCD_TIM2);
	spin_unlock(&priv->tim2_lock);

	return 0;
}

static const struct clk_ops pl111_clk_div_ops = {
	.recalc_rate = pl111_clk_div_recalc_rate,
	.round_rate = pl111_clk_div_round_rate,
	.set_rate = pl111_clk_div_set_rate,
};

static int
pl111_init_clock_divider(struct drm_device *drm)
{
	struct pl111_drm_dev_private *priv = drm->dev_private;
	struct clk *parent = devm_clk_get(drm->dev, "clcdclk");
	struct clk_hw *div = &priv->clk_div;
	const char *parent_name;
	struct clk_init_data init = {
		.name = "pl111_div",
		.ops = &pl111_clk_div_ops,
		.parent_names = &parent_name,
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	};
	int ret;

	if (IS_ERR(parent)) {
		dev_err(drm->dev, "CLCD: unable to get clcdclk.\n");
		return PTR_ERR(parent);
	}
	parent_name = __clk_get_name(parent);

	spin_lock_init(&priv->tim2_lock);
	div->init = &init;

	ret = devm_clk_hw_register(drm->dev, div);

	priv->clk = div->clk;
	return ret;
}

int pl111_display_init(struct drm_device *drm)
{
	struct pl111_drm_dev_private *priv = drm->dev_private;
	struct device *dev = drm->dev;
	struct device_node *endpoint;
	u32 tft_r0b0g0[3];
	int ret;

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

	ret = pl111_init_clock_divider(drm);
	if (ret)
		return ret;

	ret = drm_simple_display_pipe_init(drm, &priv->pipe,
					   &pl111_display_funcs,
					   priv->variant->formats,
					   priv->variant->nformats,
					   NULL,
					   priv->connector);
	if (ret)
		return ret;

	return 0;
}
