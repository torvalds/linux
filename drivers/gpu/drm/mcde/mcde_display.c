// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Linus Walleij <linus.walleij@linaro.org>
 * Parts of this file were based on the MCDE driver by Marcus Lorentzon
 * (C) ST-Ericsson SA 2013
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>

#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_simple_kms_helper.h>
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

enum mcde_dsi_formatter {
	MCDE_DSI_FORMATTER_0 = 0,
	MCDE_DSI_FORMATTER_1,
	MCDE_DSI_FORMATTER_2,
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
	if (mcde_dsi_irq(mcde->mdsi)) {
		u32 val;

		/*
		 * In oneshot mode we do not send continuous updates
		 * to the display, instead we only push out updates when
		 * the update function is called, then we disable the
		 * flow on the channel once we get the TE IRQ.
		 */
		if (mcde->oneshot_mode) {
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
	/*
	 * MCDE has inverse semantics from DRM on RBG/BGR which is why
	 * all the modes are inversed here.
	 */
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_ABGR8888:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_XRGB8888:
		val |= MCDE_EXTSRCXCONF_BPP_XRGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_XBGR8888:
		val |= MCDE_EXTSRCXCONF_BPP_XRGB8888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_RGB888:
		val |= MCDE_EXTSRCXCONF_BPP_RGB888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_BGR888:
		val |= MCDE_EXTSRCXCONF_BPP_RGB888 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_ARGB4444:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB4444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_ABGR4444:
		val |= MCDE_EXTSRCXCONF_BPP_ARGB4444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_XRGB4444:
		val |= MCDE_EXTSRCXCONF_BPP_RGB444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_XBGR4444:
		val |= MCDE_EXTSRCXCONF_BPP_RGB444 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_XRGB1555:
		val |= MCDE_EXTSRCXCONF_BPP_IRGB1555 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_XBGR1555:
		val |= MCDE_EXTSRCXCONF_BPP_IRGB1555 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		break;
	case DRM_FORMAT_RGB565:
		val |= MCDE_EXTSRCXCONF_BPP_RGB565 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
		val |= MCDE_EXTSRCXCONF_BGR;
		break;
	case DRM_FORMAT_BGR565:
		val |= MCDE_EXTSRCXCONF_BPP_RGB565 <<
			MCDE_EXTSRCXCONF_BPP_SHIFT;
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
				   u32 format)
{
	u32 val;
	u32 conf1;
	u32 conf2;
	u32 crop;
	u32 ljinc;
	u32 cr;
	u32 comp;

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
	/* The default watermark level for overlay 0 is 48 */
	val |= 48 << MCDE_OVLXCONF2_PIXELFETCHERWATERMARKLEVEL_SHIFT;
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
	if (mcde->video_mode || mcde->te_sync)
		val = MCDE_CHNLXSYNCHMOD_SRC_SYNCH_HARDWARE
			<< MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SHIFT;
	else
		val = MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SOFTWARE
			<< MCDE_CHNLXSYNCHMOD_SRC_SYNCH_SHIFT;

	if (mcde->te_sync)
		val |= MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_TE0
			<< MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_SHIFT;
	else
		val |= MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_FORMATTER
			<< MCDE_CHNLXSYNCHMOD_OUT_SYNCH_SRC_SHIFT;

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
}

static void mcde_configure_fifo(struct mcde *mcde, enum mcde_fifo fifo,
				enum mcde_dsi_formatter fmt,
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
	/* We only support DSI formatting for now */
	val |= MCDE_CTRLX_FORMTYPE_DSI <<
		MCDE_CTRLX_FORMTYPE_SHIFT;

	/* Select the formatter to use for this FIFO */
	val |= fmt << MCDE_CTRLX_FORMID_SHIFT;
	writel(val, mcde->regs + ctrl);

	/* Blend source with Alpha 0xff on FIFO */
	val = MCDE_CRX0_BLENDEN |
		0xff << MCDE_CRX0_ALPHABLEND_SHIFT;
	writel(val, mcde->regs + cr0);

	/* Set-up from mcde_fmtr_dsi.c, fmtr_dsi_enable_video() */

	/* Use the MCDE clock for this FIFO */
	val = MCDE_CRX1_CLKSEL_MCDECLK << MCDE_CRX1_CLKSEL_SHIFT;

	/* TODO: when adding DPI support add OUTBPP etc here */
	writel(val, mcde->regs + cr1);
};

static void mcde_configure_dsi_formatter(struct mcde *mcde,
					 enum mcde_dsi_formatter fmt,
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
		val |= MCDE_DSICONF0_PACKING_RGB666_PACKED <<
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
	u32 formatter_ppl = mode->hdisplay; /* pixels per line */
	u32 formatter_lpf = mode->vdisplay; /* lines per frame */
	int pkt_size, fifo_wtrmrk;
	int cpp = fb->format->cpp[0];
	int formatter_cpp;
	struct drm_format_name_buf tmp;
	u32 formatter_frame;
	u32 pkt_div;
	u32 val;

	dev_info(drm->dev, "enable MCDE, %d x %d format %s\n",
		 mode->hdisplay, mode->vdisplay,
		 drm_get_format_name(format, &tmp));
	if (!mcde->mdsi) {
		/* TODO: deal with this for non-DSI output */
		dev_err(drm->dev, "no DSI master attached!\n");
		return;
	}

	dev_info(drm->dev, "output in %s mode, format %dbpp\n",
		 (mcde->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO) ?
		 "VIDEO" : "CMD",
		 mipi_dsi_pixel_format_to_bpp(mcde->mdsi->format));
	formatter_cpp =
		mipi_dsi_pixel_format_to_bpp(mcde->mdsi->format) / 8;
	dev_info(drm->dev, "overlay CPP %d bytes, DSI CPP %d bytes\n",
		 cpp,
		 formatter_cpp);

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
	dev_dbg(drm->dev, "FIFO watermark after flooring: %d bytes\n",
		fifo_wtrmrk);
	dev_dbg(drm->dev, "Packet divisor: %d bytes\n", pkt_div);

	/* NOTE: pkt_div is 1 for video mode */
	pkt_size = (formatter_ppl * formatter_cpp) / pkt_div;
	/* Commands CMD8 need one extra byte */
	if (!(mcde->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO))
		pkt_size++;

	dev_dbg(drm->dev, "DSI packet size: %d * %d bytes per line\n",
		pkt_size, pkt_div);
	dev_dbg(drm->dev, "Overlay frame size: %u bytes\n",
		mode->hdisplay * mode->vdisplay * cpp);
	mcde->stride = mode->hdisplay * cpp;
	dev_dbg(drm->dev, "Overlay line stride: %u bytes\n",
		mcde->stride);
	/* NOTE: pkt_div is 1 for video mode */
	formatter_frame = pkt_size * pkt_div * formatter_lpf;
	dev_dbg(drm->dev, "Formatter frame size: %u bytes\n", formatter_frame);

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
			       MCDE_CHANNEL_0, mode, format);

	/*
	 * Configure pixel-per-line and line-per-frame for channel 0 and then
	 * route channel 0 to FIFO A
	 */
	mcde_configure_channel(mcde, MCDE_CHANNEL_0, MCDE_FIFO_A, mode);

	/* Configure FIFO A to use DSI formatter 0 */
	mcde_configure_fifo(mcde, MCDE_FIFO_A, MCDE_DSI_FORMATTER_0,
			    fifo_wtrmrk);

	/* Configure the DSI formatter 0 for the DSI panel output */
	mcde_configure_dsi_formatter(mcde, MCDE_DSI_FORMATTER_0,
				     formatter_frame, pkt_size);

	if (mcde->te_sync) {
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			val = MCDE_VSCRC_VSPOL;
		else
			val = 0;
		writel(val, mcde->regs + MCDE_VSCRC0);
		/* Enable VSYNC capture on TE0 */
		val = readl(mcde->regs + MCDE_CRC);
		val |= MCDE_CRC_SYCEN0;
		writel(val, mcde->regs + MCDE_CRC);
	}

	drm_crtc_vblank_on(crtc);

	if (mcde->video_mode)
		/*
		 * Keep FIFO permanently enabled in video mode,
		 * otherwise MCDE will stop feeding data to the panel.
		 */
		mcde_enable_fifo(mcde, MCDE_FIFO_A);

	dev_info(drm->dev, "MCDE display is enabled\n");
}

static void mcde_display_disable(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *drm = crtc->dev;
	struct mcde *mcde = to_mcde(drm);
	struct drm_pending_vblank_event *event;

	drm_crtc_vblank_off(crtc);

	/* Disable FIFO A flow */
	mcde_disable_fifo(mcde, MCDE_FIFO_A, true);

	event = crtc->state->event;
	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}

	dev_info(drm->dev, "MCDE display is disabled\n");
}

static void mcde_display_send_one_frame(struct mcde *mcde)
{
	/* Request a TE ACK */
	if (mcde->te_sync)
		mcde_dsi_te_request(mcde->mdsi);

	/* Enable FIFO A flow */
	mcde_enable_fifo(mcde, MCDE_FIFO_A);

	if (mcde->te_sync) {
		/*
		 * If oneshot mode is enabled, the flow will be disabled
		 * when the TE0 IRQ arrives in the interrupt handler. Otherwise
		 * updates are continuously streamed to the display after this
		 * point.
		 */
		dev_dbg(mcde->dev, "sent TE0 framebuffer update\n");
		return;
	}

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

	dev_dbg(mcde->dev, "sent SW framebuffer update\n");
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
		if (!mcde->video_mode) {
			/*
			 * Send a single frame using software sync if the flow
			 * is not active yet.
			 */
			if (mcde->flow_active == 0)
				mcde_display_send_one_frame(mcde);
		}
		dev_info_once(mcde->dev, "sent first display update\n");
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
