// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on the MCDE driver by Marcus Lorentzon
 * (C) ST-Ericsson SA 2013
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/regulator/consumer.h>
#include <linux/media-bus-format.h>

#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_vblank.h>
#include <video/mipi_display.h>

#include "mcde_drm.h"
#include "mcde_display_regs.h"

enum mcde_fifo {
	MCDE_FIFO_A,
	MCDE_FIFO_B,
	/* TODO: implement FIFO C0 and FIFO C1 */
};

enum mcde_channel {
	MCDE_CHANNEL_0 = 0,
	MCDE_CHANNEL_1,
	MCDE_CHANNEL_2,
	MCDE_CHANNEL_3,
};

enum mcde_extsrc {
	MCDE_EXTSRC_0 = 0,
	MCDE_EXTSRC_1,
	MCDE_EXTSRC_2,
	MCDE_EXTSRC_3,
	MCDE_EXTSRC_4,
	MCDE_EXTSRC_5,
	MCDE_EXTSRC_6,
	MCDE_EXTSRC_7,
	MCDE_EXTSRC_8,
	MCDE_EXTSRC_9,
};

enum mcde_overlay {
	MCDE_OVERLAY_0 = 0,
	MCDE_OVERLAY_1,
	MCDE_OVERLAY_2,
	MCDE_OVERLAY_3,
	MCDE_OVERLAY_4,
	MCDE_OVERLAY_5,
};

enum mcde_formatter {
	MCDE_DSI_FORMATTER_0 = 0,
	MCDE_DSI_FORMATTER_1,
	MCDE_DSI_FORMATTER_2,
	MCDE_DSI_FORMATTER_3,
	MCDE_DSI_FORMATTER_4,
	MCDE_DSI_FORMATTER_5,
	MCDE_DPI_FORMATTER_0,
	MCDE_DPI_FORMATTER_1,
};

void mcde_display_irq(struct mcde *mcde)
{
	u32 mispp, misovl, mischnl;
	bool vblank = false;

	/* Handle display IRQs */
	mispp = readl(mcde->regs + MCDE_MISPP);
	misovl = readl(mcde->regs + MCDE_MISOVL);
	mischnl = readl(mcde->regs + MCDE_MISCHNL);

	/*
	 * Handle IRQs from the DSI link. All IRQs from the DSI links
	 * are just latched onto the MCDE IRQ line, so we need to traverse
	 * any active DSI masters and check if an IRQ is originating from
	 * them.
	 *
	 * TODO: Currently only one DSI link is supported.
	 */
	if (!mcde->dpi_output && mcde_dsi_irq(mcde->mdsi)) {
		u32 val;

		/*
		 * In oneshot mode we do not send continuous updates
		 * to the display, instead we only push out updates when
		 * the update function is called, then we disable the
		 * flow on the channel once we get the TE IRQ.
		 */
		if (mcde->flow_mode == MCDE_COMMAND_ONESHOT_FLOW) {
			spin_lock(&mcde->flow_lock);
			if (--mcde->flow_active == 0) {
				dev_dbg(mcde->dev, "TE0 IRQ\n");
				/* Disable FIFO A flow */
				val = readl(mcde->regs + MCDE_CRA0);
				val &= ~MCDE_CRX0_FLOEN;
				writel(val, mcde->regs + MCDE_CRA0);
			}
			spin_unlock(&mcde->flow_lock);
		}
	}

	/* Vblank from one of the channels */
	if (mispp & MCDE_PP_VCMPA) {
		dev_dbg(mcde->dev, "chnl A vblank IRQ\n");
		vblank = true;
	}
	if (mispp & MCDE_PP_VCMPB) {
		dev_dbg(mcde->dev, "chnl B vblank IRQ\n");
		vblank = true;
	}
	if (mispp & MCDE_PP_VCMPC0)
		dev_dbg(mcde->dev, "chnl C0 vblank IRQ\n");
	if (mispp & MCDE_PP_VCMPC1)
		dev_dbg(mcde->dev, "chnl C1 vblank IRQ\n");
	if (mispp & MCDE_PP_VSCC0)
		dev_dbg(mcde->dev, "chnl C0 TE IRQ\n");
	if (mispp & MCDE_PP_VSCC1)
		dev_dbg(mcde->dev, "chnl C1 TE IRQ\n");
	writel(mispp, mcde->regs + MCDE_RISPP);

	if (vblank)
		drm_crtc_handle_vblank(&mcde->pipe.crtc);

	if (misovl)
		dev_info(mcde->dev, "some stray overlay IRQ %08x\n", misovl);
	writel(misovl, mcde->regs + MCDE_RISOVL);

	if (mischnl)
		dev_info(mcde->dev, "some stray channel error IRQ %08x\n",
			 mischnl);
	writel(mischnl, mcde->regs + MCDE_RISCHNL);
}

void mcde_display_disable_irqs(struct mcde *mcde)
{
	/* Disable all IRQs */
	writel(0, mcde->regs + MCDE_IMSCPP);
	writel(0, mcde->regs + MCDE_IMSCOVL);
	writel(0, mcde->regs + MCDE_IMSCCHNL);

	/* Clear any pending IRQs */
	writel(0xFFFFFFFF, mcde->regs + MCDE_RISPP);
	writel(0xFFFFFFFF, mcde->regs + MCDE_RISOVL);
	writel(0xFFFFFFFF, mcde->regs + MCDE_RISCHNL);
}

static int mcde_display_check(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *pstate,
			      struct drm_crtc_state *cstate)
{
	const struct drm_display_mode *mode = &cstate->mode;
	struct drm_framebuffer *old_fb = pipe->plane.state->fb;
	struct drm_framebuffer *fb = pstate->fb;

	if (fb) {
		u32 offset = drm_fb_cma_get_gem_addr(fb, pstate, 0);

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

static int mcde_configure_extsrc(struct mcde *mcde, enum mcde_extsrc src,
				 u32 format)
{
	u32 val;
	u32 conf;
	u32 cr;

	switch (src) {
	case MCDE_EXTSRC_0:
		conf = MCDE_EXTSRC0CONF;
		cr = MCDE_EXTSRC0CR;
		break;
	case MCDE_EXTSRC_1:
		conf = MCDE_EXTSRC1CONF;
		cr = MCDE_EXTSRC1CR;
		break;
	case MCDE_EXTSRC_2:
		conf = MCDE_EXTSRC2CONF;
		cr = MCDE_EXTSRC2CR;
		break;
	case MCDE_EXTSRC_3:
		conf = MCDE_EXTSRC3CONF;
		cr = MCDE_EXTSRC3CR;
		break;
	case MCDE_EXTSRC_4:
		conf = MCDE_EXTSRC4CONF;
		cr = MCDE_EXTSRC4CR;
		break;
	case MCDE_EXTSRC_5:
		conf = MCDE_EXTSRC5CONF;
		cr = MCDE_EXTSRC5CR;
		break;
	case MCDE_EXTSRC_6:
		conf = MCDE_EXTSRC6CONF;
		cr = MCDE_EXTSRC6CR;
		break;
	case MCDE_EXTSRC_7:
		conf = MCDE_EXTSRC7CONF;
		cr = MCDE_EXTSRC7CR;
		break;
	case MCDE_EXTSRC_8:
		conf = MCDE_EXTSRC8CONF;
		cr = MCDE_EXTSRC8CR;
		break;
	case MCDE_EXTSRC_9:
		conf = MCDE_EXTSRC9CONF;
		cr = MCDE_EXTSRC9CR;
		break;
	}

	/*
	 * Configure external source 0 one buffer (buffer 0)
	 * primary overlay ID 0.
	 * From mcde_hw.c ovly_update_registers() in the vendor tree
	 */
	val = 0 << MCDE_EXTSRCXCONF_BUF_ID_SHIFT;
	val |= 1 << MCDE_EXTSRCXCONF_BUF_NB_SHIFT;
	val |= 0 << MCDE_EXTSRCXCONF_PRI_OVLID_SHIFT;

	switch (format) {
	case DRM_FORMAT_ARGB8888:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_ABGR8888:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_XRGB8888:
		val |= MCDE_EXTSRCXCONF_BPP_XRGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_XBGR8888:
		val |= MCDE_EXTSRCXCONF_BPP_XRGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_RGB888:
		val |= MCDE_EXTSRCXCONF_BPP_RGB888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_BGR888:
		val |= MCDE_EXTSRCXCONF_BPP_RGB888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_ARGB4444:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB4444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_ABGR4444:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB4444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_XRGB4444:
		val |= MCDE_EXTSRCXCONF_BPP_RGB444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_XBGR4444:
		val |= MCDE_EXTSRCXCONF_BPP_RGB444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_XRGB1555:
		val |= MCDE_EXTSRCXCONF_BPP_IRGB1555 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_XBGR1555:
		val |= MCDE_EXTSRCXCONF_BPP_IRGB1555 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_RGB565:
		val |= MCDE_EXTSRCXCONF_BPP_RGB565 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_BGR565:
		val |= MCDE_EXTSRCXCONF_BPP_RGB565 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_YUV422:
		val |= MCDE_EXTSRCXCONF_BPP_YCBCR422 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	default:
		dev_err(mcde->dev, "Unknown pixel format 0x%08x\n",
			format);
		return -EINVAL;
	}
	writel(val, mcde->regs + conf);

	/* Software select, primary */
	val = MCDE_EXTSRCXCR_SEL_MOD_SOFTWARE_SEL;
	val |= MCDE_EXTSRCXCR_MULTIOVL_CTRL_PRIMARY;
	writel(val, mcde->regs + cr);

	return 0;
}

static void mcde_configure_overlay(struct mcde *mcde, enum mcde_overlay ovl,
				   enum mcde_extsrc src,
				   enum mcde_channel ch,
				   const struct drm_display_mode *mode,
				   u32 format, int cpp)
{
	u32 val;
	u32 conf1;
	u32 conf2;
	u32 crop;
	u32 ljinc;
	u32 cr;
	u32 comp;
	u32 pixel_fetcher_watermark;

	switch (ovl) {
	case MCDE_OVERLAY_0:
		conf1 = MCDE_OVL0CONF;
		conf2 = MCDE_OVL0CONF2;
		crop = MCDE_OVL0CROP;
		ljinc = MCDE_OVL0LJINC;
		cr = MCDE_OVL0CR;
		comp = MCDE_OVL0COMP;
		break;
	case MCDE_OVERLAY_1:
		conf1 = MCDE_OVL1CONF;
		conf2 = MCDE_OVL1CONF2;
		crop = MCDE_OVL1CROP;
		ljinc = MCDE_OVL1LJINC;
		cr = MCDE_OVL1CR;
		comp = MCDE_OVL1COMP;
		break;
	case MCDE_OVERLAY_2:
		conf1 = MCDE_OVL2CONF;
		conf2 = MCDE_OVL2CONF2;
		crop = MCDE_OVL2CROP;
		ljinc = MCDE_OVL2LJINC;
		cr = MCDE_OVL2CR;
		comp = MCDE_OVL2COMP;
		break;
	case MCDE_OVERLAY_3:
		conf1 = MCDE_OVL3CONF;
		conf2 = MCDE_OVL3CONF2;
		crop = MCDE_OVL3CROP;
		ljinc = MCDE_OVL3LJINC;
		cr = MCDE_OVL3CR;
		comp = MCDE_OVL3COMP;
		break;
	case MCDE_OVERLAY_4:
		conf1 = MCDE_OVL4CONF;
		conf2 = MCDE_OVL4CONF2;
		crop = MCDE_OVL4CROP;
		ljinc = MCDE_OVL4LJINC;
		cr = MCDE_OVL4CR;
		comp = MCDE_OVL4COMP;
		break;
	case MCDE_OVERLAY_5:
		conf1 = MCDE_OVL5CONF;
		conf2 = MCDE_OVL5CONF2;
		crop = MCDE_OVL5CROP;
		ljinc = MCDE_OVL5LJINC;
		cr = MCDE_OVL5CR;
		comp = MCDE_OVL5COMP;
		break;
	}

	val = mode->hdisplay << MCDE_OVLXCONF_PPL_SHIFT;
	val |= mode->vdisplay << MCDE_OVLXCONF_LPF_SHIFT;
	/* Use external source 0 that we just configured */
	val |= src << MCDE_OVLXCONF_EXTSRC_ID_SHIFT;
	writel(val, mcde->regs + conf1);

	val = MCDE_OVLXCONF2_BP_PER_PIXEL_ALPHA;
	val |= 0xff << MCDE_OVLXCONF2_ALPHAVALUE_SHIFT;
	/* OPQ: overlay is opaque */
	switch (format) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB4444:
	case DRM_FORMAT_ABGR4444:
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XBGR1555:
		/* No OPQ */
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
	case DRM_FORMAT_YUV422:
		val |= MCDE_OVLXCONF2_OPQ;
		break;
	default:
		dev_err(mcde->dev, "Unknown pixel format 0x%08x\n",
			format);
		break;
	}

	/*
	 * Pixel fetch watermark level is max 0x1FFF pixels.
	 * Two basic rules should be followed:
	 * 1. The value should be at least 256 bits.
	 * 2. The sum of all active overlays pixelfetch watermark level
	 *    multiplied with bits per pixel, should be lower than the
	 *    size of input_fifo_size in bits.
	 * 3. The value should be a multiple of a line (256 bits).
	 */
	switch (cpp) {
	case 2:
		pixel_fetcher_watermark = 128;
		break;
	case 3:
		pixel_fetcher_watermark = 96;
		break;
	case 4:
		pixel_fetcher_watermark = 48;
		break;
	default:
		pixel_fetcher_watermark = 48;
		break;
	}
	dev_dbg(mcde->dev, "pixel fetcher watermark level %d pixels\n",
		pixel_fetcher_watermark);
	val |= pixel_fetcher_watermark << MCDE_OVLXCONF2_PIXELFETCHERWATERMARKLEVEL_SHIFT;
	writel(val, mcde->regs + conf2);

	/* Number of bytes to fetch per line */
	writel(mcde->stride, mcde->regs + ljinc);
	/* No cropping */
	writel(0, mcde->regs + crop);

	/* Set up overlay control register */
	val = MCDE_OVLXCR_OVLEN;
	val |= MCDE_OVLXCR_COLCCTRL_DISABLED;
	val |= MCDE_OVLXCR_BURSTSIZE_8W <<
		MCDE_OVLXCR_BURSTSIZE_SHIFT;
	val |= MCDE_OVLXCR_MAXOUTSTANDING_8_REQ <<
		MCDE_OVLXCR_MAXOUTSTANDING_SHIFT;
	/* Not using rotation but set it up anyways */
	val |= MCDE_OVLXCR_ROTBURSTSIZE_8W <<
		MCDE_OVLXCR_ROTBURSTSIZE_SHIFT;
	writel(val, mcde->regs + cr);

	/*
	 * Set up the overlay compositor to route the overlay out to
	 * the desired channel
	 */
	val = ch << MCDE_OVLXCOMP_CH_ID_SHIFT;
	writel(val, mcde->regs + comp);
}

static void mcde_configure_channel(struct mcde *mcde, enum mcde_channel ch,
				   enum mcde_fifo fifo,
				   const struct drm_display_mode *mode)
{
	u32 val;
	u32 conf;
	u32 sync;
	u32 stat;
	u32 bgcol;
	u32 mux;

	switch (ch) {
	case MCDE_CHANNEL_0:
		conf = MCDE_CHNL0CONF;
		sync = MCDE_CHNL0SYNCHMOD;
		stat = MCDE_CHNL0STAT;
		bgcol = MCDE_CHNL0BCKGNDCOL;
		mux = MCDE_CHNL0MUXING;
		break;
	case MCDE_CHANNEL_1:
		conf = MCDE_CHNL1CONF;
		sync = MCDE_CHNL1SYNCHMOD;
		stat = MCDE_CHNL1STAT;
		bgcol = MCDE_CHNL1BCKGNDCOL;
		mux = MCDE_CHNL1MUXING;
		break;
	case MCDE_CHANNEL_2:
		conf = MCDE_CHNL2CONF;
		sync = MCDE_CHNL2SYNCHMOD;
		stat = MCDE_CHNL2STAT;
		bgcol = MCDE_CHNL2BCKGNDCOL;
		mux = MCDE_CHNL2MUXING;
		break;
	case MCDE_CHANNEL_3:
		conf = MCDE_CHNL3CONF;
		sync = MCDE_CHNL3SYNCHMOD;
		stat = MCDE_CHNL3STAT;
		bgcol = MCDE_CHNL3BCKGNDCOL;
		mux = MCDE_CHNL3MUXING;
		return;
	}

	/* Set up channel 0 sync (based on chnl_update_registers()) */
	switch (mcde->flow_mode) {
	case MCDE_COMMAND_ONESHOT_FLOW:
		/* Oneshot is achieved with software sync */
		val = MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SOFTWARE
			<< MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SHIFT;
		break;
	case MCDE_COMMAND_TE_FLOW:
		val = MCDE_CHNLXSYNCHMOD_SRC_SYNCH_HARDWARE
			<< MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SHIFT;
		val |= MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_TE0
			<< MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_SHIFT;
		break;
	case MCDE_COMMAND_BTA_TE_FLOW:
		val = MCDE_CHNLXSYNCHMOD_SRC_SYNCH_HARDWARE
			<< MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SHIFT;
		/*
		 * TODO:
		 * The vendor driver uses the formatter as sync source
		 * for BTA TE mode. Test to use TE if you have a panel
		 * that uses this mode.
		 */
		val |= MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_FORMATTER
			<< MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_SHIFT;
		break;
	case MCDE_VIDEO_TE_FLOW:
		val = MCDE_CHNLXSYNCHMOD_SRC_SYNCH_HARDWARE
			<< MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SHIFT;
		val |= MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_TE0
			<< MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_SHIFT;
		break;
	case MCDE_VIDEO_FORMATTER_FLOW:
	case MCDE_DPI_FORMATTER_FLOW:
		val = MCDE_CHNLXSYNCHMOD_SRC_SYNCH_HARDWARE
			<< MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SHIFT;
		val |= MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_FORMATTER
			<< MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_SHIFT;
		break;
	default:
		dev_err(mcde->dev, "unknown flow mode %d\n",
			mcde->flow_mode);
		return;
	}

	writel(val, mcde->regs + sync);

	/* Set up pixels per line and lines per frame */
	val = (mode->hdisplay - 1) << MCDE_CHNLXCONF_PPL_SHIFT;
	val |= (mode->vdisplay - 1) << MCDE_CHNLXCONF_LPF_SHIFT;
	writel(val, mcde->regs + conf);

	/*
	 * Normalize color conversion:
	 * black background, OLED conversion disable on channel
	 */
	val = MCDE_CHNLXSTAT_CHNLBLBCKGND_EN |
		MCDE_CHNLXSTAT_CHNLRD;
	writel(val, mcde->regs + stat);
	writel(0, mcde->regs + bgcol);

	/* Set up muxing: connect the channel to the desired FIFO */
	switch (fifo) {
	case MCDE_FIFO_A:
		writel(MCDE_CHNLXMUXING_FIFO_ID_FIFO_A,
		       mcde->regs + mux);
		break;
	case MCDE_FIFO_B:
		writel(MCDE_CHNLXMUXING_FIFO_ID_FIFO_B,
		       mcde->regs + mux);
		break;
	}

	/*
	 * If using DPI configure the sync event.
	 * TODO: this is for LCD only, it does not cover TV out.
	 */
	if (mcde->dpi_output) {
		u32 stripwidth;

		stripwidth = 0xF000 / (mode->vdisplay * 4);
		dev_info(mcde->dev, "stripwidth: %d\n", stripwidth);

		val = MCDE_SYNCHCONF_HWREQVEVENT_ACTIVE_VIDEO |
			(mode->hdisplay - 1 - stripwidth) << MCDE_SYNCHCONF_HWREQVCNT_SHIFT |
			MCDE_SYNCHCONF_SWINTVEVENT_ACTIVE_VIDEO |
			(mode->hdisplay - 1 - stripwidth) << MCDE_SYNCHCONF_SWINTVCNT_SHIFT;

		switch (fifo) {
		case MCDE_FIFO_A:
			writel(val, mcde->regs + MCDE_SYNCHCONFA);
			break;
		case MCDE_FIFO_B:
			writel(val, mcde->regs + MCDE_SYNCHCONFB);
			break;
		}
	}
}

static void mcde_configure_fifo(struct mcde *mcde, enum mcde_fifo fifo,
				enum mcde_formatter fmt,
				int fifo_wtrmrk)
{
	u32 val;
	u32 ctrl;
	u32 cr0, cr1;

	switch (fifo) {
	case MCDE_FIFO_A:
		ctrl = MCDE_CTRLA;
		cr0 = MCDE_CRA0;
		cr1 = MCDE_CRA1;
		break;
	case MCDE_FIFO_B:
		ctrl = MCDE_CTRLB;
		cr0 = MCDE_CRB0;
		cr1 = MCDE_CRB1;
		break;
	}

	val = fifo_wtrmrk << MCDE_CTRLX_FIFOWTRMRK_SHIFT;

	/*
	 * Select the formatter to use for this FIFO
	 *
	 * The register definitions imply that different IDs should be used
	 * by the DSI formatters depending on if they are in VID or CMD
	 * mode, and the manual says they are dedicated but identical.
	 * The vendor code uses them as it seems fit.
	 */
	switch (fmt) {
	case MCDE_DSI_FORMATTER_0:
		val |= MCDE_CTRLX_FORMTYPE_DSI << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DSI0VID << MCDE_CTRLX_FORMID_SHIFT;
		break;
	case MCDE_DSI_FORMATTER_1:
		val |= MCDE_CTRLX_FORMTYPE_DSI << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DSI0CMD << MCDE_CTRLX_FORMID_SHIFT;
		break;
	case MCDE_DSI_FORMATTER_2:
		val |= MCDE_CTRLX_FORMTYPE_DSI << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DSI1VID << MCDE_CTRLX_FORMID_SHIFT;
		break;
	case MCDE_DSI_FORMATTER_3:
		val |= MCDE_CTRLX_FORMTYPE_DSI << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DSI1CMD << MCDE_CTRLX_FORMID_SHIFT;
		break;
	case MCDE_DSI_FORMATTER_4:
		val |= MCDE_CTRLX_FORMTYPE_DSI << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DSI2VID << MCDE_CTRLX_FORMID_SHIFT;
		break;
	case MCDE_DSI_FORMATTER_5:
		val |= MCDE_CTRLX_FORMTYPE_DSI << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DSI2CMD << MCDE_CTRLX_FORMID_SHIFT;
		break;
	case MCDE_DPI_FORMATTER_0:
		val |= MCDE_CTRLX_FORMTYPE_DPITV << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DPIA << MCDE_CTRLX_FORMID_SHIFT;
		break;
	case MCDE_DPI_FORMATTER_1:
		val |= MCDE_CTRLX_FORMTYPE_DPITV << MCDE_CTRLX_FORMTYPE_SHIFT;
		val |= MCDE_CTRLX_FORMID_DPIB << MCDE_CTRLX_FORMID_SHIFT;
		break;
	}
	writel(val, mcde->regs + ctrl);

	/* Blend source with Alpha 0xff on FIFO */
	val = MCDE_CRX0_BLENDEN |
		0xff << MCDE_CRX0_ALPHABLEND_SHIFT;
	writel(val, mcde->regs + cr0);

	spin_lock(&mcde->fifo_crx1_lock);
	val = readl(mcde->regs + cr1);
	/*
	 * Set-up from mcde_fmtr_dsi.c, fmtr_dsi_enable_video()
	 * FIXME: a different clock needs to be selected for TV out.
	 */
	if (mcde->dpi_output) {
		struct drm_connector *connector = drm_panel_bridge_connector(mcde->bridge);
		u32 bus_format;

		/* Assume RGB888 24 bit if we have no further info */
		if (!connector->display_info.num_bus_formats) {
			dev_info(mcde->dev, "panel does not specify bus format, assume RGB888\n");
			bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		} else {
			bus_format = connector->display_info.bus_formats[0];
		}

		/*
		 * Set up the CDWIN and OUTBPP for the LCD
		 *
		 * FIXME: fill this in if you know the correspondance between the MIPI
		 * DPI specification and the media bus formats.
		 */
		val &= ~MCDE_CRX1_CDWIN_MASK;
		val &= ~MCDE_CRX1_OUTBPP_MASK;
		switch (bus_format) {
		case MEDIA_BUS_FMT_RGB888_1X24:
			val |= MCDE_CRX1_CDWIN_24BPP << MCDE_CRX1_CDWIN_SHIFT;
			val |= MCDE_CRX1_OUTBPP_24BPP << MCDE_CRX1_OUTBPP_SHIFT;
			break;
		default:
			dev_err(mcde->dev, "unknown bus format, assume RGB888\n");
			val |= MCDE_CRX1_CDWIN_24BPP << MCDE_CRX1_CDWIN_SHIFT;
			val |= MCDE_CRX1_OUTBPP_24BPP << MCDE_CRX1_OUTBPP_SHIFT;
			break;
		}
	} else {
		/* Use the MCDE clock for DSI */
		val &= ~MCDE_CRX1_CLKSEL_MASK;
		val = MCDE_CRX1_CLKSEL_MCDECLK << MCDE_CRX1_CLKSEL_SHIFT;
	}
	writel(val, mcde->regs + cr1);
	spin_unlock(&mcde->fifo_crx1_lock);
};

static void mcde_configure_dsi_formatter(struct mcde *mcde,
					 enum mcde_formatter fmt,
					 u32 formatter_frame,
					 int pkt_size)
{
	u32 val;
	u32 conf0;
	u32 frame;
	u32 pkt;
	u32 sync;
	u32 cmdw;
	u32 delay0, delay1;

	switch (fmt) {
	case MCDE_DSI_FORMATTER_0:
		conf0 = MCDE_DSIVID0CONF0;
		frame = MCDE_DSIVID0FRAME;
		pkt = MCDE_DSIVID0PKT;
		sync = MCDE_DSIVID0SYNC;
		cmdw = MCDE_DSIVID0CMDW;
		delay0 = MCDE_DSIVID0DELAY0;
		delay1 = MCDE_DSIVID0DELAY1;
		break;
	case MCDE_DSI_FORMATTER_1:
		conf0 = MCDE_DSIVID1CONF0;
		frame = MCDE_DSIVID1FRAME;
		pkt = MCDE_DSIVID1PKT;
		sync = MCDE_DSIVID1SYNC;
		cmdw = MCDE_DSIVID1CMDW;
		delay0 = MCDE_DSIVID1DELAY0;
		delay1 = MCDE_DSIVID1DELAY1;
		break;
	case MCDE_DSI_FORMATTER_2:
		conf0 = MCDE_DSIVID2CONF0;
		frame = MCDE_DSIVID2FRAME;
		pkt = MCDE_DSIVID2PKT;
		sync = MCDE_DSIVID2SYNC;
		cmdw = MCDE_DSIVID2CMDW;
		delay0 = MCDE_DSIVID2DELAY0;
		delay1 = MCDE_DSIVID2DELAY1;
		break;
	default:
		dev_err(mcde->dev, "tried to configure a non-DSI formatter as DSI\n");
		return;
	}

	/*
	 * Enable formatter
	 * 8 bit commands and DCS commands (notgen = not generic)
	 */
	val = MCDE_DSICONF0_CMD8 | MCDE_DSICONF0_DCSVID_NOTGEN;
	if (mcde->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO)
		val |= MCDE_DSICONF0_VID_MODE_VID;
	switch (mcde->mdsi->format) {
	case MIPI_DSI_FMT_RGB888:
		val |= MCDE_DSICONF0_PACKING_RGB888 <<
			MCDE_DSICONF0_PACKING_SHIFT;
		break;
	case MIPI_DSI_FMT_RGB666:
		val |= MCDE_DSICONF0_PACKING_RGB666 <<
			MCDE_DSICONF0_PACKING_SHIFT;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		dev_err(mcde->dev,
			"we cannot handle the packed RGB666 format\n");
		val |= MCDE_DSICONF0_PACKING_RGB666 <<
			MCDE_DSICONF0_PACKING_SHIFT;
		break;
	case MIPI_DSI_FMT_RGB565:
		val |= MCDE_DSICONF0_PACKING_RGB565 <<
			MCDE_DSICONF0_PACKING_SHIFT;
		break;
	default:
		dev_err(mcde->dev, "unknown DSI format\n");
		return;
	}
	writel(val, mcde->regs + conf0);

	writel(formatter_frame, mcde->regs + frame);
	writel(pkt_size, mcde->regs + pkt);
	writel(0, mcde->regs + sync);
	/* Define the MIPI command: we want to write into display memory */
	val = MIPI_DCS_WRITE_MEMORY_CONTINUE <<
		MCDE_DSIVIDXCMDW_CMDW_CONTINUE_SHIFT;
	val |= MIPI_DCS_WRITE_MEMORY_START <<
		MCDE_DSIVIDXCMDW_CMDW_START_SHIFT;
	writel(val, mcde->regs + cmdw);

	/*
	 * FIXME: the vendor driver has some hack around this value in
	 * CMD mode with autotrig.
	 */
	writel(0, mcde->regs + delay0);
	writel(0, mcde->regs + delay1);
}

static void mcde_enable_fifo(struct mcde *mcde, enum mcde_fifo fifo)
{
	u32 val;
	u32 cr;

	switch (fifo) {
	case MCDE_FIFO_A:
		cr = MCDE_CRA0;
		break;
	case MCDE_FIFO_B:
		cr = MCDE_CRB0;
		break;
	default:
		dev_err(mcde->dev, "cannot enable FIFO %c\n",
			'A' + fifo);
		return;
	}

	spin_lock(&mcde->flow_lock);
	val = readl(mcde->regs + cr);
	val |= MCDE_CRX0_FLOEN;
	writel(val, mcde->regs + cr);
	mcde->flow_active++;
	spin_unlock(&mcde->flow_lock);
}

static void mcde_disable_fifo(struct mcde *mcde, enum mcde_fifo fifo,
			      bool wait_for_drain)
{
	int timeout = 100;
	u32 val;
	u32 cr;

	switch (fifo) {
	case MCDE_FIFO_A:
		cr = MCDE_CRA0;
		break;
	case MCDE_FIFO_B:
		cr = MCDE_CRB0;
		break;
	default:
		dev_err(mcde->dev, "cannot disable FIFO %c\n",
			'A' + fifo);
		return;
	}

	spin_lock(&mcde->flow_lock);
	val = readl(mcde->regs + cr);
	val &= ~MCDE_CRX0_FLOEN;
	writel(val, mcde->regs + cr);
	mcde->flow_active = 0;
	spin_unlock(&mcde->flow_lock);

	if (!wait_for_drain)
		return;

	/* Check that we really drained and stopped the flow */
	while (readl(mcde->regs + cr) & MCDE_CRX0_FLOEN) {
		usleep_range(1000, 1500);
		if (!--timeout) {
			dev_err(mcde->dev,
				"FIFO timeout while clearing FIFO %c\n",
				'A' + fifo);
			return;
		}
	}
}

/*
 * This drains a pipe i.e. a FIFO connected to a certain channel
 */
static void mcde_drain_pipe(struct mcde *mcde, enum mcde_fifo fifo,
			    enum mcde_channel ch)
{
	u32 val;
	u32 ctrl;
	u32 synsw;

	switch (fifo) {
	case MCDE_FIFO_A:
		ctrl = MCDE_CTRLA;
		break;
	case MCDE_FIFO_B:
		ctrl = MCDE_CTRLB;
		break;
	}

	switch (ch) {
	case MCDE_CHANNEL_0:
		synsw = MCDE_CHNL0SYNCHSW;
		break;
	case MCDE_CHANNEL_1:
		synsw = MCDE_CHNL1SYNCHSW;
		break;
	case MCDE_CHANNEL_2:
		synsw = MCDE_CHNL2SYNCHSW;
		break;
	case MCDE_CHANNEL_3:
		synsw = MCDE_CHNL3SYNCHSW;
		return;
	}

	val = readl(mcde->regs + ctrl);
	if (!(val & MCDE_CTRLX_FIFOEMPTY)) {
		dev_err(mcde->dev, "Channel A FIFO not empty (handover)\n");
		/* Attempt to clear the FIFO */
		mcde_enable_fifo(mcde, fifo);
		/* Trigger a software sync out on respective channel (0-3) */
		writel(MCDE_CHNLXSYNCHSW_SW_TRIG, mcde->regs + synsw);
		/* Disable FIFO A flow again */
		mcde_disable_fifo(mcde, fifo, true);
	}
}

static int mcde_dsi_get_pkt_div(int ppl, int fifo_size)
{
	/*
	 * DSI command mode line packets should be split into an even number of
	 * packets smaller than or equal to the fifo size.
	 */
	int div;
	const int max_div = DIV_ROUND_UP(MCDE_MAX_WIDTH, fifo_size);

	for (div = 1; div < max_div; div++)
		if (ppl % div == 0 && ppl / div <= fifo_size)
			return div;
	return 1;
}

static void mcde_setup_dpi(struct mcde *mcde, const struct drm_display_mode *mode,
			   int *fifo_wtrmrk_lvl)
{
	struct drm_connector *connector = drm_panel_bridge_connector(mcde->bridge);
	u32 hsw, hfp, hbp;
	u32 vsw, vfp, vbp;
	u32 val;

	/* FIXME: we only support LCD, implement TV out */
	hsw = mode->hsync_end - mode->hsync_start;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	vsw = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;

	dev_info(mcde->dev, "output on DPI LCD from channel A\n");
	/* Display actual values */
	dev_info(mcde->dev, "HSW: %d, HFP: %d, HBP: %d, VSW: %d, VFP: %d, VBP: %d\n",
		 hsw, hfp, hbp, vsw, vfp, vbp);

	/*
	 * The pixel fetcher is 128 64-bit words deep = 1024 bytes.
	 * One overlay of 32bpp (4 cpp) assumed, fetch 160 pixels.
	 * 160 * 4 = 640 bytes.
	 */
	*fifo_wtrmrk_lvl = 640;

	/* Set up the main control, watermark level at 7 */
	val = 7 << MCDE_CONF0_IFIFOCTRLWTRMRKLVL_SHIFT;

	/*
	 * This sets up the internal silicon muxing of the DPI
	 * lines. This is how the silicon connects out to the
	 * external pins, then the pins need to be further
	 * configured into "alternate functions" using pin control
	 * to actually get the signals out.
	 *
	 * FIXME: this is hardcoded to the only setting found in
	 * the wild. If we need to use different settings for
	 * different DPI displays, make this parameterizable from
	 * the device tree.
	 */
	/* 24 bits DPI: connect Ch A LSB to D[0:7] */
	val |= 0 << MCDE_CONF0_OUTMUX0_SHIFT;
	/* 24 bits DPI: connect Ch A MID to D[8:15] */
	val |= 1 << MCDE_CONF0_OUTMUX1_SHIFT;
	/* Don't care about this muxing */
	val |= 0 << MCDE_CONF0_OUTMUX2_SHIFT;
	/* Don't care about this muxing */
	val |= 0 << MCDE_CONF0_OUTMUX3_SHIFT;
	/* 24 bits DPI: connect Ch A MSB to D[32:39] */
	val |= 2 << MCDE_CONF0_OUTMUX4_SHIFT;
	/* Syncmux bits zero: DPI channel A */
	writel(val, mcde->regs + MCDE_CONF0);

	/* This hammers us into LCD mode */
	writel(0, mcde->regs + MCDE_TVCRA);

	/* Front porch and sync width */
	val = (vsw << MCDE_TVBL1_BEL1_SHIFT);
	val |= (vfp << MCDE_TVBL1_BSL1_SHIFT);
	writel(val, mcde->regs + MCDE_TVBL1A);
	/* The vendor driver sets the same value into TVBL2A */
	writel(val, mcde->regs + MCDE_TVBL2A);

	/* Vertical back porch */
	val = (vbp << MCDE_TVDVO_DVO1_SHIFT);
	/* The vendor drivers sets the same value into TVDVOA */
	val |= (vbp << MCDE_TVDVO_DVO2_SHIFT);
	writel(val, mcde->regs + MCDE_TVDVOA);

	/* Horizontal back porch, as 0 = 1 cycle we need to subtract 1 */
	writel((hbp - 1), mcde->regs + MCDE_TVTIM1A);

	/* Horizongal sync width and horizonal front porch, 0 = 1 cycle */
	val = ((hsw - 1) << MCDE_TVLBALW_LBW_SHIFT);
	val |= ((hfp - 1) << MCDE_TVLBALW_ALW_SHIFT);
	writel(val, mcde->regs + MCDE_TVLBALWA);

	/* Blank some TV registers we don't use */
	writel(0, mcde->regs + MCDE_TVISLA);
	writel(0, mcde->regs + MCDE_TVBLUA);

	/* Set up sync inversion etc */
	val = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= MCDE_LCDTIM1B_IHS;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= MCDE_LCDTIM1B_IVS;
	if (connector->display_info.bus_flags & DRM_BUS_FLAG_DE_LOW)
		val |= MCDE_LCDTIM1B_IOE;
	if (connector->display_info.bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		val |= MCDE_LCDTIM1B_IPC;
	writel(val, mcde->regs + MCDE_LCDTIM1A);
}

static void mcde_setup_dsi(struct mcde *mcde, const struct drm_display_mode *mode,
			   int cpp, int *fifo_wtrmrk_lvl, int *dsi_formatter_frame,
			   int *dsi_pkt_size)
{
	u32 formatter_ppl = mode->hdisplay; /* pixels per line */
	u32 formatter_lpf = mode->vdisplay; /* lines per frame */
	int formatter_frame;
	int formatter_cpp;
	int fifo_wtrmrk;
	u32 pkt_div;
	int pkt_size;
	u32 val;

	dev_info(mcde->dev, "output in %s mode, format %dbpp\n",
		 (mcde->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO) ?
		 "VIDEO" : "CMD",
		 mipi_dsi_pixel_format_to_bpp(mcde->mdsi->format));
	formatter_cpp =
		mipi_dsi_pixel_format_to_bpp(mcde->mdsi->format) / 8;
	dev_info(mcde->dev, "Overlay CPP: %d bytes, DSI formatter CPP %d bytes\n",
		 cpp, formatter_cpp);

	/* Set up the main control, watermark level at 7 */
	val = 7 << MCDE_CONF0_IFIFOCTRLWTRMRKLVL_SHIFT;

	/*
	 * This is the internal silicon muxing of the DPI
	 * (parallell display) lines. Since we are not using
	 * this at all (we are using DSI) these are just
	 * dummy values from the vendor tree.
	 */
	val |= 3 << MCDE_CONF0_OUTMUX0_SHIFT;
	val |= 3 << MCDE_CONF0_OUTMUX1_SHIFT;
	val |= 0 << MCDE_CONF0_OUTMUX2_SHIFT;
	val |= 4 << MCDE_CONF0_OUTMUX3_SHIFT;
	val |= 5 << MCDE_CONF0_OUTMUX4_SHIFT;
	writel(val, mcde->regs + MCDE_CONF0);

	/* Calculations from mcde_fmtr_dsi.c, fmtr_dsi_enable_video() */

	/*
	 * Set up FIFO A watermark level:
	 * 128 for LCD 32bpp video mode
	 * 48  for LCD 32bpp command mode
	 * 128 for LCD 16bpp video mode
	 * 64  for LCD 16bpp command mode
	 * 128 for HDMI 32bpp
	 * 192 for HDMI 16bpp
	 */
	fifo_wtrmrk = mode->hdisplay;
	if (mcde->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		fifo_wtrmrk = min(fifo_wtrmrk, 128);
		pkt_div = 1;
	} else {
		fifo_wtrmrk = min(fifo_wtrmrk, 48);
		/* The FIFO is 640 entries deep on this v3 hardware */
		pkt_div = mcde_dsi_get_pkt_div(mode->hdisplay, 640);
	}
	dev_dbg(mcde->dev, "FIFO watermark after flooring: %d bytes\n",
		fifo_wtrmrk);
	dev_dbg(mcde->dev, "Packet divisor: %d bytes\n", pkt_div);

	/* NOTE: pkt_div is 1 for video mode */
	pkt_size = (formatter_ppl * formatter_cpp) / pkt_div;
	/* Commands CMD8 need one extra byte */
	if (!(mcde->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO))
		pkt_size++;

	dev_dbg(mcde->dev, "DSI packet size: %d * %d bytes per line\n",
		pkt_size, pkt_div);
	dev_dbg(mcde->dev, "Overlay frame size: %u bytes\n",
		mode->hdisplay * mode->vdisplay * cpp);
	/* NOTE: pkt_div is 1 for video mode */
	formatter_frame = pkt_size * pkt_div * formatter_lpf;
	dev_dbg(mcde->dev, "Formatter frame size: %u bytes\n", formatter_frame);

	*fifo_wtrmrk_lvl = fifo_wtrmrk;
	*dsi_pkt_size = pkt_size;
	*dsi_formatter_frame = formatter_frame;
}

static void mcde_display_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *cstate,
				struct drm_plane_state *plane_state)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_plane *plane = &pipe->plane;
	struct drm_device *drm = crtc->dev;
	struct mcde *mcde = to_mcde(drm);
	const struct drm_display_mode *mode = &cstate->mode;
	struct drm_framebuffer *fb = plane->state->fb;
	u32 format = fb->format->format;
	int dsi_pkt_size;
	int fifo_wtrmrk;
	int cpp = fb->format->cpp[0];
	struct drm_format_name_buf tmp;
	u32 dsi_formatter_frame;
	u32 val;
	int ret;

	/* This powers up the entire MCDE block and the DSI hardware */
	ret = regulator_enable(mcde->epod);
	if (ret) {
		dev_err(drm->dev, "can't re-enable EPOD regulator\n");
		return;
	}

	dev_info(drm->dev, "enable MCDE, %d x %d format %s\n",
		 mode->hdisplay, mode->vdisplay,
		 drm_get_format_name(format, &tmp));


	/* Clear any pending interrupts */
	mcde_display_disable_irqs(mcde);
	writel(0, mcde->regs + MCDE_IMSCERR);
	writel(0xFFFFFFFF, mcde->regs + MCDE_RISERR);

	if (mcde->dpi_output)
		mcde_setup_dpi(mcde, mode, &fifo_wtrmrk);
	else
		mcde_setup_dsi(mcde, mode, cpp, &fifo_wtrmrk,
			       &dsi_formatter_frame, &dsi_pkt_size);

	mcde->stride = mode->hdisplay * cpp;
	dev_dbg(drm->dev, "Overlay line stride: %u bytes\n",
		 mcde->stride);

	/* Drain the FIFO A + channel 0 pipe so we have a clean slate */
	mcde_drain_pipe(mcde, MCDE_FIFO_A, MCDE_CHANNEL_0);

	/*
	 * We set up our display pipeline:
	 * EXTSRC 0 -> OVERLAY 0 -> CHANNEL 0 -> FIFO A -> DSI FORMATTER 0
	 *
	 * First configure the external source (memory) on external source 0
	 * using the desired bitstream/bitmap format
	 */
	mcde_configure_extsrc(mcde, MCDE_EXTSRC_0, format);

	/*
	 * Configure overlay 0 according to format and mode and take input
	 * from external source 0 and route the output of this overlay to
	 * channel 0
	 */
	mcde_configure_overlay(mcde, MCDE_OVERLAY_0, MCDE_EXTSRC_0,
			       MCDE_CHANNEL_0, mode, format, cpp);

	/*
	 * Configure pixel-per-line and line-per-frame for channel 0 and then
	 * route channel 0 to FIFO A
	 */
	mcde_configure_channel(mcde, MCDE_CHANNEL_0, MCDE_FIFO_A, mode);

	if (mcde->dpi_output) {
		unsigned long lcd_freq;

		/* Configure FIFO A to use DPI formatter 0 */
		mcde_configure_fifo(mcde, MCDE_FIFO_A, MCDE_DPI_FORMATTER_0,
				    fifo_wtrmrk);

		/* Set up and enable the LCD clock */
		lcd_freq = clk_round_rate(mcde->fifoa_clk, mode->clock * 1000);
		ret = clk_set_rate(mcde->fifoa_clk, lcd_freq);
		if (ret)
			dev_err(mcde->dev, "failed to set LCD clock rate %lu Hz\n",
				lcd_freq);
		ret = clk_prepare_enable(mcde->fifoa_clk);
		if (ret) {
			dev_err(mcde->dev, "failed to enable FIFO A DPI clock\n");
			return;
		}
		dev_info(mcde->dev, "LCD FIFO A clk rate %lu Hz\n",
			 clk_get_rate(mcde->fifoa_clk));
	} else {
		/* Configure FIFO A to use DSI formatter 0 */
		mcde_configure_fifo(mcde, MCDE_FIFO_A, MCDE_DSI_FORMATTER_0,
				    fifo_wtrmrk);

		/*
		 * This brings up the DSI bridge which is tightly connected
		 * to the MCDE DSI formatter.
		 */
		mcde_dsi_enable(mcde->bridge);

		/* Configure the DSI formatter 0 for the DSI panel output */
		mcde_configure_dsi_formatter(mcde, MCDE_DSI_FORMATTER_0,
					     dsi_formatter_frame, dsi_pkt_size);
	}

	switch (mcde->flow_mode) {
	case MCDE_COMMAND_TE_FLOW:
	case MCDE_COMMAND_BTA_TE_FLOW:
	case MCDE_VIDEO_TE_FLOW:
		/* We are using TE in some combination */
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			val = MCDE_VSCRC_VSPOL;
		else
			val = 0;
		writel(val, mcde->regs + MCDE_VSCRC0);
		/* Enable VSYNC capture on TE0 */
		val = readl(mcde->regs + MCDE_CRC);
		val |= MCDE_CRC_SYCEN0;
		writel(val, mcde->regs + MCDE_CRC);
		break;
	default:
		/* No TE capture */
		break;
	}

	drm_crtc_vblank_on(crtc);

	/*
	 * If we're using oneshot mode we don't start the flow
	 * until each time the display is given an update, and
	 * then we disable it immediately after. For all other
	 * modes (command or video) we start the FIFO flow
	 * right here. This is necessary for the hardware to
	 * behave right.
	 */
	if (mcde->flow_mode != MCDE_COMMAND_ONESHOT_FLOW) {
		mcde_enable_fifo(mcde, MCDE_FIFO_A);
		dev_dbg(mcde->dev, "started MCDE video FIFO flow\n");
	}

	/* Enable MCDE with automatic clock gating */
	val = readl(mcde->regs + MCDE_CR);
	val |= MCDE_CR_MCDEEN | MCDE_CR_AUTOCLKG_EN;
	writel(val, mcde->regs + MCDE_CR);

	dev_info(drm->dev, "MCDE display is enabled\n");
}

static void mcde_display_disable(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct mcde *mcde = to_mcde(drm);
	struct drm_pending_vblank_event *event;
	int ret;

	drm_crtc_vblank_off(crtc);

	/* Disable FIFO A flow */
	mcde_disable_fifo(mcde, MCDE_FIFO_A, true);

	if (mcde->dpi_output) {
		clk_disable_unprepare(mcde->fifoa_clk);
	} else {
		/* This disables the DSI bridge */
		mcde_dsi_disable(mcde->bridge);
	}

	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}

	ret = regulator_disable(mcde->epod);
	if (ret)
		dev_err(drm->dev, "can't disable EPOD regulator\n");
	/* Make sure we are powered down (before we may power up again) */
	usleep_range(50000, 70000);

	dev_info(drm->dev, "MCDE display is disabled\n");
}

static void mcde_start_flow(struct mcde *mcde)
{
	/* Request a TE ACK only in TE+BTA mode */
	if (mcde->flow_mode == MCDE_COMMAND_BTA_TE_FLOW)
		mcde_dsi_te_request(mcde->mdsi);

	/* Enable FIFO A flow */
	mcde_enable_fifo(mcde, MCDE_FIFO_A);

	/*
	 * If oneshot mode is enabled, the flow will be disabled
	 * when the TE0 IRQ arrives in the interrupt handler. Otherwise
	 * updates are continuously streamed to the display after this
	 * point.
	 */

	if (mcde->flow_mode == MCDE_COMMAND_ONESHOT_FLOW) {
		/* Trigger a software sync out on channel 0 */
		writel(MCDE_CHNLXSYNCHSW_SW_TRIG,
		       mcde->regs + MCDE_CHNL0SYNCHSW);

		/*
		 * Disable FIFO A flow again: since we are using TE sync we
		 * need to wait for the FIFO to drain before we continue
		 * so repeated calls to this function will not cause a mess
		 * in the hardware by pushing updates will updates are going
		 * on already.
		 */
		mcde_disable_fifo(mcde, MCDE_FIFO_A, true);
	}

	dev_dbg(mcde->dev, "started MCDE FIFO flow\n");
}

static void mcde_set_extsrc(struct mcde *mcde, u32 buffer_address)
{
	/* Write bitmap base address to register */
	writel(buffer_address, mcde->regs + MCDE_EXTSRCXA0);
	/*
	 * Base address for next line this is probably only used
	 * in interlace modes.
	 */
	writel(buffer_address + mcde->stride, mcde->regs + MCDE_EXTSRCXA1);
}

static void mcde_display_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_pstate)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct mcde *mcde = to_mcde(drm);
	struct drm_pending_vblank_event *event = crtc->state->event;
	struct drm_plane *plane = &pipe->plane;
	struct drm_plane_state *pstate = plane->state;
	struct drm_framebuffer *fb = pstate->fb;

	/*
	 * Handle any pending event first, we need to arm the vblank
	 * interrupt before sending any update to the display so we don't
	 * miss the interrupt.
	 */
	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		/*
		 * Hardware must be on before we can arm any vblank event,
		 * this is not a scanout controller where there is always
		 * some periodic update going on, it is completely frozen
		 * until we get an update. If MCDE output isn't yet enabled,
		 * we just send a vblank dummy event back.
		 */
		if (crtc->state->active && drm_crtc_vblank_get(crtc) == 0) {
			dev_dbg(mcde->dev, "arm vblank event\n");
			drm_crtc_arm_vblank_event(crtc, event);
		} else {
			dev_dbg(mcde->dev, "insert fake vblank event\n");
			drm_crtc_send_vblank_event(crtc, event);
		}

		spin_unlock_irq(&crtc->dev->event_lock);
	}

	/*
	 * We do not start sending framebuffer updates before the
	 * display is enabled. Update events will however be dispatched
	 * from the DRM core before the display is enabled.
	 */
	if (fb) {
		mcde_set_extsrc(mcde, drm_fb_cma_get_gem_addr(fb, pstate, 0));
		dev_info_once(mcde->dev, "first update of display contents\n");
		/*
		 * Usually the flow is already active, unless we are in
		 * oneshot mode, then we need to kick the flow right here.
		 */
		if (mcde->flow_active == 0)
			mcde_start_flow(mcde);
	} else {
		/*
		 * If an update is receieved before the MCDE is enabled
		 * (before mcde_display_enable() is called) we can't really
		 * do much with that buffer.
		 */
		dev_info(mcde->dev, "ignored a display update\n");
	}
}

static int mcde_display_enable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct mcde *mcde = to_mcde(drm);
	u32 val;

	/* Enable all VBLANK IRQs */
	val = MCDE_PP_VCMPA |
		MCDE_PP_VCMPB |
		MCDE_PP_VSCC0 |
		MCDE_PP_VSCC1 |
		MCDE_PP_VCMPC0 |
		MCDE_PP_VCMPC1;
	writel(val, mcde->regs + MCDE_IMSCPP);

	return 0;
}

static void mcde_display_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct mcde *mcde = to_mcde(drm);

	/* Disable all VBLANK IRQs */
	writel(0, mcde->regs + MCDE_IMSCPP);
	/* Clear any pending IRQs */
	writel(0xFFFFFFFF, mcde->regs + MCDE_RISPP);
}

static struct drm_simple_display_pipe_funcs mcde_display_funcs = {
	.check = mcde_display_check,
	.enable = mcde_display_enable,
	.disable = mcde_display_disable,
	.update = mcde_display_update,
	.enable_vblank = mcde_display_enable_vblank,
	.disable_vblank = mcde_display_disable_vblank,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

int mcde_display_init(struct drm_device *drm)
{
	struct mcde *mcde = to_mcde(drm);
	int ret;
	static const u32 formats[] = {
		DRM_FORMAT_ARGB8888,
		DRM_FORMAT_ABGR8888,
		DRM_FORMAT_XRGB8888,
		DRM_FORMAT_XBGR8888,
		DRM_FORMAT_RGB888,
		DRM_FORMAT_BGR888,
		DRM_FORMAT_ARGB4444,
		DRM_FORMAT_ABGR4444,
		DRM_FORMAT_XRGB4444,
		DRM_FORMAT_XBGR4444,
		/* These are actually IRGB1555 so intensity bit is lost */
		DRM_FORMAT_XRGB1555,
		DRM_FORMAT_XBGR1555,
		DRM_FORMAT_RGB565,
		DRM_FORMAT_BGR565,
		DRM_FORMAT_YUV422,
	};

	ret = mcde_init_clock_divider(mcde);
	if (ret)
		return ret;

	ret = drm_simple_display_pipe_init(drm, &mcde->pipe,
					   &mcde_display_funcs,
					   formats, ARRAY_SIZE(formats),
					   NULL,
					   mcde->connector);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(mcde_display_init);
