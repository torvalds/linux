// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 *
 * This code is based on drivers/gpu/drm/mxsfb/mxsfb*
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/media-bus-format.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "lcdif_drv.h"
#include "lcdif_regs.h"

/* -----------------------------------------------------------------------------
 * CRTC
 */
static void lcdif_set_formats(struct lcdif_drm_private *lcdif,
			      const u32 bus_format)
{
	struct drm_device *drm = lcdif->drm;
	const u32 format = lcdif->crtc.primary->state->fb->format->format;

	writel(CSC0_CTRL_BYPASS, lcdif->base + LCDC_V8_CSC0_CTRL);

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		writel(DISP_PARA_LINE_PATTERN_RGB565,
		       lcdif->base + LCDC_V8_DISP_PARA);
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		writel(DISP_PARA_LINE_PATTERN_RGB888,
		       lcdif->base + LCDC_V8_DISP_PARA);
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		writel(DISP_PARA_LINE_PATTERN_UYVY_H,
		       lcdif->base + LCDC_V8_DISP_PARA);

		/* CSC: BT.601 Full Range RGB to YCbCr coefficients. */
		writel(CSC0_COEF0_A2(0x096) | CSC0_COEF0_A1(0x04c),
		       lcdif->base + LCDC_V8_CSC0_COEF0);
		writel(CSC0_COEF1_B1(0x7d5) | CSC0_COEF1_A3(0x01d),
		       lcdif->base + LCDC_V8_CSC0_COEF1);
		writel(CSC0_COEF2_B3(0x080) | CSC0_COEF2_B2(0x7ac),
		       lcdif->base + LCDC_V8_CSC0_COEF2);
		writel(CSC0_COEF3_C2(0x795) | CSC0_COEF3_C1(0x080),
		       lcdif->base + LCDC_V8_CSC0_COEF3);
		writel(CSC0_COEF4_D1(0x000) | CSC0_COEF4_C3(0x7ec),
		       lcdif->base + LCDC_V8_CSC0_COEF4);
		writel(CSC0_COEF5_D3(0x080) | CSC0_COEF5_D2(0x080),
		       lcdif->base + LCDC_V8_CSC0_COEF5);

		writel(CSC0_CTRL_CSC_MODE_RGB2YCbCr,
		       lcdif->base + LCDC_V8_CSC0_CTRL);

		break;
	default:
		dev_err(drm->dev, "Unknown media bus format 0x%x\n", bus_format);
		break;
	}

	switch (format) {
	case DRM_FORMAT_RGB565:
		writel(CTRLDESCL0_5_BPP_16_RGB565,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		break;
	case DRM_FORMAT_RGB888:
		writel(CTRLDESCL0_5_BPP_24_RGB888,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		break;
	case DRM_FORMAT_XRGB1555:
		writel(CTRLDESCL0_5_BPP_16_ARGB1555,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		break;
	case DRM_FORMAT_XRGB4444:
		writel(CTRLDESCL0_5_BPP_16_ARGB4444,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		break;
	case DRM_FORMAT_XBGR8888:
		writel(CTRLDESCL0_5_BPP_32_ABGR8888,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		break;
	case DRM_FORMAT_XRGB8888:
		writel(CTRLDESCL0_5_BPP_32_ARGB8888,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		break;
	default:
		dev_err(drm->dev, "Unknown pixel format 0x%x\n", format);
		break;
	}
}

static void lcdif_set_mode(struct lcdif_drm_private *lcdif, u32 bus_flags)
{
	struct drm_display_mode *m = &lcdif->crtc.state->adjusted_mode;
	u32 ctrl = 0;

	if (m->flags & DRM_MODE_FLAG_NHSYNC)
		ctrl |= CTRL_INV_HS;
	if (m->flags & DRM_MODE_FLAG_NVSYNC)
		ctrl |= CTRL_INV_VS;
	if (bus_flags & DRM_BUS_FLAG_DE_LOW)
		ctrl |= CTRL_INV_DE;
	if (bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		ctrl |= CTRL_INV_PXCK;

	writel(ctrl, lcdif->base + LCDC_V8_CTRL);

	writel(DISP_SIZE_DELTA_Y(m->crtc_vdisplay) |
	       DISP_SIZE_DELTA_X(m->crtc_hdisplay),
	       lcdif->base + LCDC_V8_DISP_SIZE);

	writel(HSYN_PARA_BP_H(m->htotal - m->hsync_end) |
	       HSYN_PARA_FP_H(m->hsync_start - m->hdisplay),
	       lcdif->base + LCDC_V8_HSYN_PARA);

	writel(VSYN_PARA_BP_V(m->vtotal - m->vsync_end) |
	       VSYN_PARA_FP_V(m->vsync_start - m->vdisplay),
	       lcdif->base + LCDC_V8_VSYN_PARA);

	writel(VSYN_HSYN_WIDTH_PW_V(m->vsync_end - m->vsync_start) |
	       VSYN_HSYN_WIDTH_PW_H(m->hsync_end - m->hsync_start),
	       lcdif->base + LCDC_V8_VSYN_HSYN_WIDTH);

	writel(CTRLDESCL0_1_HEIGHT(m->crtc_vdisplay) |
	       CTRLDESCL0_1_WIDTH(m->crtc_hdisplay),
	       lcdif->base + LCDC_V8_CTRLDESCL0_1);

	writel(CTRLDESCL0_3_PITCH(lcdif->crtc.primary->state->fb->pitches[0]),
	       lcdif->base + LCDC_V8_CTRLDESCL0_3);
}

static void lcdif_enable_controller(struct lcdif_drm_private *lcdif)
{
	u32 reg;

	reg = readl(lcdif->base + LCDC_V8_DISP_PARA);
	reg |= DISP_PARA_DISP_ON;
	writel(reg, lcdif->base + LCDC_V8_DISP_PARA);

	reg = readl(lcdif->base + LCDC_V8_CTRLDESCL0_5);
	reg |= CTRLDESCL0_5_EN;
	writel(reg, lcdif->base + LCDC_V8_CTRLDESCL0_5);
}

static void lcdif_disable_controller(struct lcdif_drm_private *lcdif)
{
	u32 reg;
	int ret;

	reg = readl(lcdif->base + LCDC_V8_CTRLDESCL0_5);
	reg &= ~CTRLDESCL0_5_EN;
	writel(reg, lcdif->base + LCDC_V8_CTRLDESCL0_5);

	ret = readl_poll_timeout(lcdif->base + LCDC_V8_CTRLDESCL0_5,
				 reg, !(reg & CTRLDESCL0_5_EN),
				 0, 36000);	/* Wait ~2 frame times max */
	if (ret)
		drm_err(lcdif->drm, "Failed to disable controller!\n");

	reg = readl(lcdif->base + LCDC_V8_DISP_PARA);
	reg &= ~DISP_PARA_DISP_ON;
	writel(reg, lcdif->base + LCDC_V8_DISP_PARA);
}

static void lcdif_reset_block(struct lcdif_drm_private *lcdif)
{
	writel(CTRL_SW_RESET, lcdif->base + LCDC_V8_CTRL + REG_SET);
	readl(lcdif->base + LCDC_V8_CTRL);
	writel(CTRL_SW_RESET, lcdif->base + LCDC_V8_CTRL + REG_CLR);
	readl(lcdif->base + LCDC_V8_CTRL);
}

static void lcdif_crtc_mode_set_nofb(struct lcdif_drm_private *lcdif,
				     struct drm_bridge_state *bridge_state,
				     const u32 bus_format)
{
	struct drm_device *drm = lcdif->crtc.dev;
	struct drm_display_mode *m = &lcdif->crtc.state->adjusted_mode;
	u32 bus_flags = 0;

	if (lcdif->bridge && lcdif->bridge->timings)
		bus_flags = lcdif->bridge->timings->input_bus_flags;
	else if (bridge_state)
		bus_flags = bridge_state->input_bus_cfg.flags;

	DRM_DEV_DEBUG_DRIVER(drm->dev, "Pixel clock: %dkHz (actual: %dkHz)\n",
			     m->crtc_clock,
			     (int)(clk_get_rate(lcdif->clk) / 1000));
	DRM_DEV_DEBUG_DRIVER(drm->dev, "Connector bus_flags: 0x%08X\n",
			     bus_flags);
	DRM_DEV_DEBUG_DRIVER(drm->dev, "Mode flags: 0x%08X\n", m->flags);

	/* Mandatory eLCDIF reset as per the Reference Manual */
	lcdif_reset_block(lcdif);

	lcdif_set_formats(lcdif, bus_format);

	lcdif_set_mode(lcdif, bus_flags);
}

static int lcdif_crtc_atomic_check(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	bool has_primary = crtc_state->plane_mask &
			   drm_plane_mask(crtc->primary);

	/* The primary plane has to be enabled when the CRTC is active. */
	if (crtc_state->active && !has_primary)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state, crtc);
}

static void lcdif_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(crtc->dev);
	struct drm_pending_vblank_event *event;
	u32 reg;

	reg = readl(lcdif->base + LCDC_V8_CTRLDESCL0_5);
	reg |= CTRLDESCL0_5_SHADOW_LOAD_EN;
	writel(reg, lcdif->base + LCDC_V8_CTRLDESCL0_5);

	event = crtc->state->event;
	crtc->state->event = NULL;

	if (!event)
		return;

	spin_lock_irq(&crtc->dev->event_lock);
	if (drm_crtc_vblank_get(crtc) == 0)
		drm_crtc_arm_vblank_event(crtc, event);
	else
		drm_crtc_send_vblank_event(crtc, event);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void lcdif_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(crtc->dev);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state,
									    crtc->primary);
	struct drm_display_mode *m = &lcdif->crtc.state->adjusted_mode;
	struct drm_bridge_state *bridge_state = NULL;
	struct drm_device *drm = lcdif->drm;
	u32 bus_format = 0;
	dma_addr_t paddr;

	/* If there is a bridge attached to the LCDIF, use its bus format */
	if (lcdif->bridge) {
		bridge_state =
			drm_atomic_get_new_bridge_state(state,
							lcdif->bridge);
		if (!bridge_state)
			bus_format = MEDIA_BUS_FMT_FIXED;
		else
			bus_format = bridge_state->input_bus_cfg.format;

		if (bus_format == MEDIA_BUS_FMT_FIXED) {
			dev_warn_once(drm->dev,
				      "Bridge does not provide bus format, assuming MEDIA_BUS_FMT_RGB888_1X24.\n"
				      "Please fix bridge driver by handling atomic_get_input_bus_fmts.\n");
			bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		}
	}

	/* If all else fails, default to RGB888_1X24 */
	if (!bus_format)
		bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	clk_set_rate(lcdif->clk, m->crtc_clock * 1000);

	pm_runtime_get_sync(drm->dev);

	lcdif_crtc_mode_set_nofb(lcdif, bridge_state, bus_format);

	/* Write cur_buf as well to avoid an initial corrupt frame */
	paddr = drm_fb_cma_get_gem_addr(new_pstate->fb, new_pstate, 0);
	if (paddr) {
		writel(lower_32_bits(paddr),
		       lcdif->base + LCDC_V8_CTRLDESCL_LOW0_4);
		writel(CTRLDESCL_HIGH0_4_ADDR_HIGH(upper_32_bits(paddr)),
		       lcdif->base + LCDC_V8_CTRLDESCL_HIGH0_4);
	}
	lcdif_enable_controller(lcdif);

	drm_crtc_vblank_on(crtc);
}

static void lcdif_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_atomic_state *state)
{
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(crtc->dev);
	struct drm_device *drm = lcdif->drm;
	struct drm_pending_vblank_event *event;

	drm_crtc_vblank_off(crtc);

	lcdif_disable_controller(lcdif);

	spin_lock_irq(&drm->event_lock);
	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;
		drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irq(&drm->event_lock);

	pm_runtime_put_sync(drm->dev);
}

static int lcdif_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(crtc->dev);

	/* Clear and enable VBLANK IRQ */
	writel(INT_STATUS_D0_VS_BLANK, lcdif->base + LCDC_V8_INT_STATUS_D0);
	writel(INT_ENABLE_D0_VS_BLANK_EN, lcdif->base + LCDC_V8_INT_ENABLE_D0);

	return 0;
}

static void lcdif_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(crtc->dev);

	/* Disable and clear VBLANK IRQ */
	writel(0, lcdif->base + LCDC_V8_INT_ENABLE_D0);
	writel(INT_STATUS_D0_VS_BLANK, lcdif->base + LCDC_V8_INT_STATUS_D0);
}

static const struct drm_crtc_helper_funcs lcdif_crtc_helper_funcs = {
	.atomic_check = lcdif_crtc_atomic_check,
	.atomic_flush = lcdif_crtc_atomic_flush,
	.atomic_enable = lcdif_crtc_atomic_enable,
	.atomic_disable = lcdif_crtc_atomic_disable,
};

static const struct drm_crtc_funcs lcdif_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = lcdif_crtc_enable_vblank,
	.disable_vblank = lcdif_crtc_disable_vblank,
};

/* -----------------------------------------------------------------------------
 * Encoder
 */

static const struct drm_encoder_funcs lcdif_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

/* -----------------------------------------------------------------------------
 * Planes
 */

static int lcdif_plane_atomic_check(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state,
									     plane);
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(plane->dev);
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(state,
						   &lcdif->crtc);

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   false, true);
}

static void lcdif_plane_primary_atomic_update(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(plane->dev);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state,
									    plane);
	dma_addr_t paddr;

	paddr = drm_fb_cma_get_gem_addr(new_pstate->fb, new_pstate, 0);
	if (paddr) {
		writel(lower_32_bits(paddr),
		       lcdif->base + LCDC_V8_CTRLDESCL_LOW0_4);
		writel(CTRLDESCL_HIGH0_4_ADDR_HIGH(upper_32_bits(paddr)),
		       lcdif->base + LCDC_V8_CTRLDESCL_HIGH0_4);
	}
}

static bool lcdif_format_mod_supported(struct drm_plane *plane,
				       uint32_t format,
				       uint64_t modifier)
{
	return modifier == DRM_FORMAT_MOD_LINEAR;
}

static const struct drm_plane_helper_funcs lcdif_plane_primary_helper_funcs = {
	.atomic_check = lcdif_plane_atomic_check,
	.atomic_update = lcdif_plane_primary_atomic_update,
};

static const struct drm_plane_funcs lcdif_plane_funcs = {
	.format_mod_supported	= lcdif_format_mod_supported,
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static const u32 lcdif_primary_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XRGB8888,
};

static const u64 lcdif_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

/* -----------------------------------------------------------------------------
 * Initialization
 */

int lcdif_kms_init(struct lcdif_drm_private *lcdif)
{
	struct drm_encoder *encoder = &lcdif->encoder;
	struct drm_crtc *crtc = &lcdif->crtc;
	int ret;

	drm_plane_helper_add(&lcdif->planes.primary,
			     &lcdif_plane_primary_helper_funcs);
	ret = drm_universal_plane_init(lcdif->drm, &lcdif->planes.primary, 1,
				       &lcdif_plane_funcs,
				       lcdif_primary_plane_formats,
				       ARRAY_SIZE(lcdif_primary_plane_formats),
				       lcdif_modifiers, DRM_PLANE_TYPE_PRIMARY,
				       NULL);
	if (ret)
		return ret;

	drm_crtc_helper_add(crtc, &lcdif_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(lcdif->drm, crtc,
					&lcdif->planes.primary, NULL,
					&lcdif_crtc_funcs, NULL);
	if (ret)
		return ret;

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	return drm_encoder_init(lcdif->drm, encoder, &lcdif_encoder_funcs,
				DRM_MODE_ENCODER_NONE, NULL);
}
