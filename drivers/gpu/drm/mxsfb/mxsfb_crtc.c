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
#include <linux/iopoll.h>
#include <linux/of_graph.h>
#include <linux/platform_data/simplefb.h>

#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "mxsfb_drv.h"
#include "mxsfb_regs.h"

#define MXS_SET_ADDR		0x4
#define MXS_CLR_ADDR		0x8
#define MODULE_CLKGATE		BIT(30)
#define MODULE_SFTRST		BIT(31)
/* 1 second delay should be plenty of time for block reset */
#define RESET_TIMEOUT		1000000

static u32 set_hsync_pulse_width(struct mxsfb_drm_private *mxsfb, u32 val)
{
	return (val & mxsfb->devdata->hs_wdth_mask) <<
		mxsfb->devdata->hs_wdth_shift;
}

/* Setup the MXSFB registers for decoding the pixels out of the framebuffer */
static int mxsfb_set_pixel_fmt(struct mxsfb_drm_private *mxsfb)
{
	struct drm_crtc *crtc = &mxsfb->pipe.crtc;
	struct drm_device *drm = crtc->dev;
	const u32 format = crtc->primary->state->fb->format->format;
	u32 ctrl, ctrl1;

	ctrl = CTRL_BYPASS_COUNT | CTRL_MASTER;

	/*
	 * WARNING: The bus width, CTRL_SET_BUS_WIDTH(), is configured to
	 * match the selected mode here. This differs from the original
	 * MXSFB driver, which had the option to configure the bus width
	 * to arbitrary value. This limitation should not pose an issue.
	 */

	/* CTRL1 contains IRQ config and status bits, preserve those. */
	ctrl1 = readl(mxsfb->base + LCDC_CTRL1);
	ctrl1 &= CTRL1_CUR_FRAME_DONE_IRQ_EN | CTRL1_CUR_FRAME_DONE_IRQ;

	switch (format) {
	case DRM_FORMAT_RGB565:
		dev_dbg(drm->dev, "Setting up RGB565 mode\n");
		ctrl |= CTRL_SET_WORD_LENGTH(0);
		ctrl1 |= CTRL1_SET_BYTE_PACKAGING(0xf);
		break;
	case DRM_FORMAT_XRGB8888:
		dev_dbg(drm->dev, "Setting up XRGB8888 mode\n");
		ctrl |= CTRL_SET_WORD_LENGTH(3);
		/* Do not use packed pixels = one pixel per word instead. */
		ctrl1 |= CTRL1_SET_BYTE_PACKAGING(0x7);
		break;
	default:
		dev_err(drm->dev, "Unhandled pixel format %08x\n", format);
		return -EINVAL;
	}

	writel(ctrl1, mxsfb->base + LCDC_CTRL1);
	writel(ctrl, mxsfb->base + LCDC_CTRL);

	return 0;
}

static void mxsfb_set_bus_fmt(struct mxsfb_drm_private *mxsfb)
{
	struct drm_crtc *crtc = &mxsfb->pipe.crtc;
	struct drm_device *drm = crtc->dev;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	u32 reg;

	reg = readl(mxsfb->base + LCDC_CTRL);

	if (mxsfb->connector.display_info.num_bus_formats)
		bus_format = mxsfb->connector.display_info.bus_formats[0];

	reg &= ~CTRL_BUS_WIDTH_MASK;
	switch (bus_format) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		reg |= CTRL_SET_BUS_WIDTH(STMLCDIF_16BIT);
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
		reg |= CTRL_SET_BUS_WIDTH(STMLCDIF_18BIT);
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		reg |= CTRL_SET_BUS_WIDTH(STMLCDIF_24BIT);
		break;
	default:
		dev_err(drm->dev, "Unknown media bus format %d\n", bus_format);
		break;
	}
	writel(reg, mxsfb->base + LCDC_CTRL);
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

	writel(mask, addr + MXS_CLR_ADDR);
	return readl_poll_timeout(addr, reg, !(reg & mask), 0, RESET_TIMEOUT);
}

static int mxsfb_reset_block(void __iomem *reset_addr)
{
	int ret;

	ret = clear_poll_bit(reset_addr, MODULE_SFTRST);
	if (ret)
		return ret;

	writel(MODULE_CLKGATE, reset_addr + MXS_CLR_ADDR);

	ret = clear_poll_bit(reset_addr, MODULE_SFTRST);
	if (ret)
		return ret;

	return clear_poll_bit(reset_addr, MODULE_CLKGATE);
}

static dma_addr_t mxsfb_get_fb_paddr(struct mxsfb_drm_private *mxsfb)
{
	struct drm_framebuffer *fb = mxsfb->pipe.plane.state->fb;
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
	struct drm_display_mode *m = &mxsfb->pipe.crtc.state->adjusted_mode;
	const u32 bus_flags = mxsfb->connector.display_info.bus_flags;
	u32 vdctrl0, vsync_pulse_len, hsync_pulse_len;
	int err;

	/*
	 * It seems, you can't re-program the controller if it is still
	 * running. This may lead to shifted pictures (FIFO issue?), so
	 * first stop the controller and drain its FIFOs.
	 */

	/* Mandatory eLCDIF reset as per the Reference Manual */
	err = mxsfb_reset_block(mxsfb->base);
	if (err)
		return;

	/* Clear the FIFOs */
	writel(CTRL1_FIFO_CLEAR, mxsfb->base + LCDC_CTRL1 + REG_SET);

	err = mxsfb_set_pixel_fmt(mxsfb);
	if (err)
		return;

	clk_set_rate(mxsfb->clk, m->crtc_clock * 1000);

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

	mxsfb_set_bus_fmt(mxsfb);

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

void mxsfb_crtc_enable(struct mxsfb_drm_private *mxsfb)
{
	dma_addr_t paddr;

	mxsfb_enable_axi_clk(mxsfb);
	mxsfb_crtc_mode_set_nofb(mxsfb);

	/* Write cur_buf as well to avoid an initial corrupt frame */
	paddr = mxsfb_get_fb_paddr(mxsfb);
	if (paddr) {
		writel(paddr, mxsfb->base + mxsfb->devdata->cur_buf);
		writel(paddr, mxsfb->base + mxsfb->devdata->next_buf);
	}

	mxsfb_enable_controller(mxsfb);
}

void mxsfb_crtc_disable(struct mxsfb_drm_private *mxsfb)
{
	mxsfb_disable_controller(mxsfb);
	mxsfb_disable_axi_clk(mxsfb);
}

void mxsfb_plane_atomic_update(struct mxsfb_drm_private *mxsfb,
			       struct drm_plane_state *state)
{
	struct drm_simple_display_pipe *pipe = &mxsfb->pipe;
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_pending_vblank_event *event;
	dma_addr_t paddr;

	spin_lock_irq(&crtc->dev->event_lock);
	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;

		if (drm_crtc_vblank_get(crtc) == 0) {
			drm_crtc_arm_vblank_event(crtc, event);
		} else {
			drm_crtc_send_vblank_event(crtc, event);
		}
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	paddr = mxsfb_get_fb_paddr(mxsfb);
	if (paddr) {
		mxsfb_enable_axi_clk(mxsfb);
		writel(paddr, mxsfb->base + mxsfb->devdata->next_buf);
		mxsfb_disable_axi_clk(mxsfb);
	}
}
