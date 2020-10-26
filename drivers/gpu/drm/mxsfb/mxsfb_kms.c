// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Marek Vasut <marex@denx.de>
 *
 * This code is based on drivers/video/fbdev/mxsfb.c :
 * Copyright (C) 2010 Juergen Beisert, Pengutronix
 * Copyright (C) 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "mxsfb_drv.h"
#include "mxsfb_regs.h"

/* 1 second delay should be plenty of time for block reset */
#define RESET_TIMEOUT		1000000

/* -----------------------------------------------------------------------------
 * CRTC
 */

static u32 set_hsync_pulse_width(struct mxsfb_drm_private *mxsfb, u32 val)
{
	return (val & mxsfb->devdata->hs_wdth_mask) <<
		mxsfb->devdata->hs_wdth_shift;
}

/*
 * Setup the MXSFB registers for decoding the pixels out of the framebuffer and
 * outputting them on the bus.
 */
static void mxsfb_set_formats(struct mxsfb_drm_private *mxsfb)
{
	struct drm_device *drm = mxsfb->drm;
	const u32 format = mxsfb->crtc.primary->state->fb->format->format;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	u32 ctrl, ctrl1;

	if (mxsfb->connector->display_info.num_bus_formats)
		bus_format = mxsfb->connector->display_info.bus_formats[0];

	DRM_DEV_DEBUG_DRIVER(drm->dev, "Using bus_format: 0x%08X\n",
			     bus_format);

	ctrl = CTRL_BYPASS_COUNT | CTRL_MASTER;

	/* CTRL1 contains IRQ config and status bits, preserve those. */
	ctrl1 = readl(mxsfb->base + LCDC_CTRL1);
	ctrl1 &= CTRL1_CUR_FRAME_DONE_IRQ_EN | CTRL1_CUR_FRAME_DONE_IRQ;

	switch (format) {
	case DRM_FORMAT_RGB565:
		dev_dbg(drm->dev, "Setting up RGB565 mode\n");
		ctrl |= CTRL_WORD_LENGTH_16;
		ctrl1 |= CTRL1_SET_BYTE_PACKAGING(0xf);
		break;
	case DRM_FORMAT_XRGB8888:
		dev_dbg(drm->dev, "Setting up XRGB8888 mode\n");
		ctrl |= CTRL_WORD_LENGTH_24;
		/* Do not use packed pixels = one pixel per word instead. */
		ctrl1 |= CTRL1_SET_BYTE_PACKAGING(0x7);
		break;
	}

	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		ctrl |= CTRL_BUS_WIDTH_16;
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
		ctrl |= CTRL_BUS_WIDTH_18;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		ctrl |= CTRL_BUS_WIDTH_24;
		break;
	default:
		dev_err(drm->dev, "Unknown media bus format %d\n", bus_format);
		break;
	}

	writel(ctrl1, mxsfb->base + LCDC_CTRL1);
	writel(ctrl, mxsfb->base + LCDC_CTRL);
}

static void mxsfb_enable_controller(struct mxsfb_drm_private *mxsfb)
{
	u32 reg;

	if (mxsfb->clk_disp_axi)
		clk_prepare_enable(mxsfb->clk_disp_axi);
	clk_prepare_enable(mxsfb->clk);

	/* If it was disabled, re-enable the mode again */
	writel(CTRL_DOTCLK_MODE, mxsfb->base + LCDC_CTRL + REG_SET);

	/* Enable the SYNC signals first, then the DMA engine */
	reg = readl(mxsfb->base + LCDC_VDCTRL4);
	reg |= VDCTRL4_SYNC_SIGNALS_ON;
	writel(reg, mxsfb->base + LCDC_VDCTRL4);

	writel(CTRL_RUN, mxsfb->base + LCDC_CTRL + REG_SET);
}

static void mxsfb_disable_controller(struct mxsfb_drm_private *mxsfb)
{
	u32 reg;

	/*
	 * Even if we disable the controller here, it will still continue
	 * until its FIFOs are running out of data
	 */
	writel(CTRL_DOTCLK_MODE, mxsfb->base + LCDC_CTRL + REG_CLR);

	readl_poll_timeout(mxsfb->base + LCDC_CTRL, reg, !(reg & CTRL_RUN),
			   0, 1000);

	reg = readl(mxsfb->base + LCDC_VDCTRL4);
	reg &= ~VDCTRL4_SYNC_SIGNALS_ON;
	writel(reg, mxsfb->base + LCDC_VDCTRL4);

	clk_disable_unprepare(mxsfb->clk);
	if (mxsfb->clk_disp_axi)
		clk_disable_unprepare(mxsfb->clk_disp_axi);
}

/*
 * Clear the bit and poll it cleared.  This is usually called with
 * a reset address and mask being either SFTRST(bit 31) or CLKGATE
 * (bit 30).
 */
static int clear_poll_bit(void __iomem *addr, u32 mask)
{
	u32 reg;

	writel(mask, addr + REG_CLR);
	return readl_poll_timeout(addr, reg, !(reg & mask), 0, RESET_TIMEOUT);
}

static int mxsfb_reset_block(struct mxsfb_drm_private *mxsfb)
{
	int ret;

	ret = clear_poll_bit(mxsfb->base + LCDC_CTRL, CTRL_SFTRST);
	if (ret)
		return ret;

	writel(CTRL_CLKGATE, mxsfb->base + LCDC_CTRL + REG_CLR);

	ret = clear_poll_bit(mxsfb->base + LCDC_CTRL, CTRL_SFTRST);
	if (ret)
		return ret;

	return clear_poll_bit(mxsfb->base + LCDC_CTRL, CTRL_CLKGATE);
}

static dma_addr_t mxsfb_get_fb_paddr(struct drm_plane *plane)
{
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_gem_cma_object *gem;

	if (!fb)
		return 0;

	gem = drm_fb_cma_get_gem_obj(fb, 0);
	if (!gem)
		return 0;

	return gem->paddr;
}

static void mxsfb_crtc_mode_set_nofb(struct mxsfb_drm_private *mxsfb)
{
	struct drm_device *drm = mxsfb->crtc.dev;
	struct drm_display_mode *m = &mxsfb->crtc.state->adjusted_mode;
	u32 bus_flags = mxsfb->connector->display_info.bus_flags;
	u32 vdctrl0, vsync_pulse_len, hsync_pulse_len;
	int err;

	/*
	 * It seems, you can't re-program the controller if it is still
	 * running. This may lead to shifted pictures (FIFO issue?), so
	 * first stop the controller and drain its FIFOs.
	 */

	/* Mandatory eLCDIF reset as per the Reference Manual */
	err = mxsfb_reset_block(mxsfb);
	if (err)
		return;

	/* Clear the FIFOs */
	writel(CTRL1_FIFO_CLEAR, mxsfb->base + LCDC_CTRL1 + REG_SET);

	if (mxsfb->devdata->has_overlay)
		writel(0, mxsfb->base + LCDC_AS_CTRL);

	mxsfb_set_formats(mxsfb);

	clk_set_rate(mxsfb->clk, m->crtc_clock * 1000);

	if (mxsfb->bridge && mxsfb->bridge->timings)
		bus_flags = mxsfb->bridge->timings->input_bus_flags;

	DRM_DEV_DEBUG_DRIVER(drm->dev, "Pixel clock: %dkHz (actual: %dkHz)\n",
			     m->crtc_clock,
			     (int)(clk_get_rate(mxsfb->clk) / 1000));
	DRM_DEV_DEBUG_DRIVER(drm->dev, "Connector bus_flags: 0x%08X\n",
			     bus_flags);
	DRM_DEV_DEBUG_DRIVER(drm->dev, "Mode flags: 0x%08X\n", m->flags);

	writel(TRANSFER_COUNT_SET_VCOUNT(m->crtc_vdisplay) |
	       TRANSFER_COUNT_SET_HCOUNT(m->crtc_hdisplay),
	       mxsfb->base + mxsfb->devdata->transfer_count);

	vsync_pulse_len = m->crtc_vsync_end - m->crtc_vsync_start;

	vdctrl0 = VDCTRL0_ENABLE_PRESENT |	/* Always in DOTCLOCK mode */
		  VDCTRL0_VSYNC_PERIOD_UNIT |
		  VDCTRL0_VSYNC_PULSE_WIDTH_UNIT |
		  VDCTRL0_SET_VSYNC_PULSE_WIDTH(vsync_pulse_len);
	if (m->flags & DRM_MODE_FLAG_PHSYNC)
		vdctrl0 |= VDCTRL0_HSYNC_ACT_HIGH;
	if (m->flags & DRM_MODE_FLAG_PVSYNC)
		vdctrl0 |= VDCTRL0_VSYNC_ACT_HIGH;
	/* Make sure Data Enable is high active by default */
	if (!(bus_flags & DRM_BUS_FLAG_DE_LOW))
		vdctrl0 |= VDCTRL0_ENABLE_ACT_HIGH;
	/*
	 * DRM_BUS_FLAG_PIXDATA_DRIVE_ defines are controller centric,
	 * controllers VDCTRL0_DOTCLK is display centric.
	 * Drive on positive edge       -> display samples on falling edge
	 * DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE -> VDCTRL0_DOTCLK_ACT_FALLING
	 */
	if (bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE)
		vdctrl0 |= VDCTRL0_DOTCLK_ACT_FALLING;

	writel(vdctrl0, mxsfb->base + LCDC_VDCTRL0);

	/* Frame length in lines. */
	writel(m->crtc_vtotal, mxsfb->base + LCDC_VDCTRL1);

	/* Line length in units of clocks or pixels. */
	hsync_pulse_len = m->crtc_hsync_end - m->crtc_hsync_start;
	writel(set_hsync_pulse_width(mxsfb, hsync_pulse_len) |
	       VDCTRL2_SET_HSYNC_PERIOD(m->crtc_htotal),
	       mxsfb->base + LCDC_VDCTRL2);

	writel(SET_HOR_WAIT_CNT(m->crtc_htotal - m->crtc_hsync_start) |
	       SET_VERT_WAIT_CNT(m->crtc_vtotal - m->crtc_vsync_start),
	       mxsfb->base + LCDC_VDCTRL3);

	writel(SET_DOTCLK_H_VALID_DATA_CNT(m->hdisplay),
	       mxsfb->base + LCDC_VDCTRL4);
}

static int mxsfb_crtc_atomic_check(struct drm_crtc *crtc,
				   struct drm_crtc_state *state)
{
	bool has_primary = state->plane_mask &
			   drm_plane_mask(crtc->primary);

	/* The primary plane has to be enabled when the CRTC is active. */
	if (state->active && !has_primary)
		return -EINVAL;

	/* TODO: Is this needed ? */
	return drm_atomic_add_affected_planes(state->state, crtc);
}

static void mxsfb_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct drm_pending_vblank_event *event;

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

static void mxsfb_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct mxsfb_drm_private *mxsfb = to_mxsfb_drm_private(crtc->dev);
	struct drm_device *drm = mxsfb->drm;
	dma_addr_t paddr;

	pm_runtime_get_sync(drm->dev);
	mxsfb_enable_axi_clk(mxsfb);

	drm_crtc_vblank_on(crtc);

	mxsfb_crtc_mode_set_nofb(mxsfb);

	/* Write cur_buf as well to avoid an initial corrupt frame */
	paddr = mxsfb_get_fb_paddr(crtc->primary);
	if (paddr) {
		writel(paddr, mxsfb->base + mxsfb->devdata->cur_buf);
		writel(paddr, mxsfb->base + mxsfb->devdata->next_buf);
	}

	mxsfb_enable_controller(mxsfb);
}

static void mxsfb_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)
{
	struct mxsfb_drm_private *mxsfb = to_mxsfb_drm_private(crtc->dev);
	struct drm_device *drm = mxsfb->drm;
	struct drm_pending_vblank_event *event;

	mxsfb_disable_controller(mxsfb);

	spin_lock_irq(&drm->event_lock);
	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;
		drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irq(&drm->event_lock);

	drm_crtc_vblank_off(crtc);

	mxsfb_disable_axi_clk(mxsfb);
	pm_runtime_put_sync(drm->dev);
}

static int mxsfb_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct mxsfb_drm_private *mxsfb = to_mxsfb_drm_private(crtc->dev);

	/* Clear and enable VBLANK IRQ */
	writel(CTRL1_CUR_FRAME_DONE_IRQ, mxsfb->base + LCDC_CTRL1 + REG_CLR);
	writel(CTRL1_CUR_FRAME_DONE_IRQ_EN, mxsfb->base + LCDC_CTRL1 + REG_SET);

	return 0;
}

static void mxsfb_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct mxsfb_drm_private *mxsfb = to_mxsfb_drm_private(crtc->dev);

	/* Disable and clear VBLANK IRQ */
	writel(CTRL1_CUR_FRAME_DONE_IRQ_EN, mxsfb->base + LCDC_CTRL1 + REG_CLR);
	writel(CTRL1_CUR_FRAME_DONE_IRQ, mxsfb->base + LCDC_CTRL1 + REG_CLR);
}

static const struct drm_crtc_helper_funcs mxsfb_crtc_helper_funcs = {
	.atomic_check = mxsfb_crtc_atomic_check,
	.atomic_flush = mxsfb_crtc_atomic_flush,
	.atomic_enable = mxsfb_crtc_atomic_enable,
	.atomic_disable = mxsfb_crtc_atomic_disable,
};

static const struct drm_crtc_funcs mxsfb_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = mxsfb_crtc_enable_vblank,
	.disable_vblank = mxsfb_crtc_disable_vblank,
};

/* -----------------------------------------------------------------------------
 * Encoder
 */

static const struct drm_encoder_funcs mxsfb_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

/* -----------------------------------------------------------------------------
 * Planes
 */

static int mxsfb_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *plane_state)
{
	struct mxsfb_drm_private *mxsfb = to_mxsfb_drm_private(plane->dev);
	struct drm_crtc_state *crtc_state;

	crtc_state = drm_atomic_get_new_crtc_state(plane_state->state,
						   &mxsfb->crtc);

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   false, true);
}

static void mxsfb_plane_primary_atomic_update(struct drm_plane *plane,
					      struct drm_plane_state *old_pstate)
{
	struct mxsfb_drm_private *mxsfb = to_mxsfb_drm_private(plane->dev);
	dma_addr_t paddr;

	paddr = mxsfb_get_fb_paddr(plane);
	if (paddr)
		writel(paddr, mxsfb->base + mxsfb->devdata->next_buf);
}

static void mxsfb_plane_overlay_atomic_update(struct drm_plane *plane,
					      struct drm_plane_state *old_pstate)
{
	struct mxsfb_drm_private *mxsfb = to_mxsfb_drm_private(plane->dev);
	struct drm_plane_state *state = plane->state;
	dma_addr_t paddr;
	u32 ctrl;

	paddr = mxsfb_get_fb_paddr(plane);
	if (!paddr) {
		writel(0, mxsfb->base + LCDC_AS_CTRL);
		return;
	}

	/*
	 * HACK: The hardware seems to output 64 bytes of data of unknown
	 * origin, and then to proceed with the framebuffer. Until the reason
	 * is understood, live with the 16 initial invalid pixels on the first
	 * line and start 64 bytes within the framebuffer.
	 */
	paddr += 64;

	writel(paddr, mxsfb->base + LCDC_AS_NEXT_BUF);

	/*
	 * If the plane was previously disabled, write LCDC_AS_BUF as well to
	 * provide the first buffer.
	 */
	if (!old_pstate->fb)
		writel(paddr, mxsfb->base + LCDC_AS_BUF);

	ctrl = AS_CTRL_AS_ENABLE | AS_CTRL_ALPHA(255);

	switch (state->fb->format->format) {
	case DRM_FORMAT_XRGB4444:
		ctrl |= AS_CTRL_FORMAT_RGB444 | AS_CTRL_ALPHA_CTRL_OVERRIDE;
		break;
	case DRM_FORMAT_ARGB4444:
		ctrl |= AS_CTRL_FORMAT_ARGB4444 | AS_CTRL_ALPHA_CTRL_EMBEDDED;
		break;
	case DRM_FORMAT_XRGB1555:
		ctrl |= AS_CTRL_FORMAT_RGB555 | AS_CTRL_ALPHA_CTRL_OVERRIDE;
		break;
	case DRM_FORMAT_ARGB1555:
		ctrl |= AS_CTRL_FORMAT_ARGB1555 | AS_CTRL_ALPHA_CTRL_EMBEDDED;
		break;
	case DRM_FORMAT_RGB565:
		ctrl |= AS_CTRL_FORMAT_RGB565 | AS_CTRL_ALPHA_CTRL_OVERRIDE;
		break;
	case DRM_FORMAT_XRGB8888:
		ctrl |= AS_CTRL_FORMAT_RGB888 | AS_CTRL_ALPHA_CTRL_OVERRIDE;
		break;
	case DRM_FORMAT_ARGB8888:
		ctrl |= AS_CTRL_FORMAT_ARGB8888 | AS_CTRL_ALPHA_CTRL_EMBEDDED;
		break;
	}

	writel(ctrl, mxsfb->base + LCDC_AS_CTRL);
}

static const struct drm_plane_helper_funcs mxsfb_plane_primary_helper_funcs = {
	.atomic_check = mxsfb_plane_atomic_check,
	.atomic_update = mxsfb_plane_primary_atomic_update,
};

static const struct drm_plane_helper_funcs mxsfb_plane_overlay_helper_funcs = {
	.atomic_check = mxsfb_plane_atomic_check,
	.atomic_update = mxsfb_plane_overlay_atomic_update,
};

static const struct drm_plane_funcs mxsfb_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static const uint32_t mxsfb_primary_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
};

static const uint32_t mxsfb_overlay_plane_formats[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const uint64_t mxsfb_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

/* -----------------------------------------------------------------------------
 * Initialization
 */

int mxsfb_kms_init(struct mxsfb_drm_private *mxsfb)
{
	struct drm_encoder *encoder = &mxsfb->encoder;
	struct drm_crtc *crtc = &mxsfb->crtc;
	int ret;

	drm_plane_helper_add(&mxsfb->planes.primary,
			     &mxsfb_plane_primary_helper_funcs);
	ret = drm_universal_plane_init(mxsfb->drm, &mxsfb->planes.primary, 1,
				       &mxsfb_plane_funcs,
				       mxsfb_primary_plane_formats,
				       ARRAY_SIZE(mxsfb_primary_plane_formats),
				       mxsfb_modifiers, DRM_PLANE_TYPE_PRIMARY,
				       NULL);
	if (ret)
		return ret;

	if (mxsfb->devdata->has_overlay) {
		drm_plane_helper_add(&mxsfb->planes.overlay,
				     &mxsfb_plane_overlay_helper_funcs);
		ret = drm_universal_plane_init(mxsfb->drm,
					       &mxsfb->planes.overlay, 1,
					       &mxsfb_plane_funcs,
					       mxsfb_overlay_plane_formats,
					       ARRAY_SIZE(mxsfb_overlay_plane_formats),
					       mxsfb_modifiers, DRM_PLANE_TYPE_OVERLAY,
					       NULL);
		if (ret)
			return ret;
	}

	drm_crtc_helper_add(crtc, &mxsfb_crtc_helper_funcs);
	ret = drm_crtc_init_with_planes(mxsfb->drm, crtc,
					&mxsfb->planes.primary, NULL,
					&mxsfb_crtc_funcs, NULL);
	if (ret)
		return ret;

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	return drm_encoder_init(mxsfb->drm, encoder, &mxsfb_encoder_funcs,
				DRM_MODE_ENCODER_NONE, NULL);
}
