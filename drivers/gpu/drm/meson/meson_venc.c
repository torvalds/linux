/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "meson_drv.h"
#include "meson_venc.h"
#include "meson_vpp.h"
#include "meson_vclk.h"
#include "meson_registers.h"

/*
 * VENC Handle the pixels encoding to the output formats.
 * We handle the following encodings :
 * - CVBS Encoding via the ENCI encoder and VDAC digital to analog converter
 *
 * What is missing :
 * - TMDS/HDMI Encoding via ENCI_DIV and ENCP
 * - Setup of more clock rates for HDMI modes
 * - LCD Panel encoding via ENCL
 * - TV Panel encoding via ENCT
 */

/* HHI Registers */
#define HHI_VDAC_CNTL0		0x2F4 /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL1		0x2F8 /* 0xbe offset in data sheet */
#define HHI_HDMI_PHY_CNTL0	0x3a0 /* 0xe8 offset in data sheet */

struct meson_cvbs_enci_mode meson_cvbs_enci_pal = {
	.mode_tag = MESON_VENC_MODE_CVBS_PAL,
	.hso_begin = 3,
	.hso_end = 129,
	.vso_even = 3,
	.vso_odd = 260,
	.macv_max_amp = 7,
	.video_prog_mode = 0xff,
	.video_mode = 0x13,
	.sch_adjust = 0x28,
	.yc_delay = 0x343,
	.pixel_start = 251,
	.pixel_end = 1691,
	.top_field_line_start = 22,
	.top_field_line_end = 310,
	.bottom_field_line_start = 23,
	.bottom_field_line_end = 311,
	.video_saturation = 9,
	.video_contrast = 0,
	.video_brightness = 0,
	.video_hue = 0,
	.analog_sync_adj = 0x8080,
};

struct meson_cvbs_enci_mode meson_cvbs_enci_ntsc = {
	.mode_tag = MESON_VENC_MODE_CVBS_NTSC,
	.hso_begin = 5,
	.hso_end = 129,
	.vso_even = 3,
	.vso_odd = 260,
	.macv_max_amp = 0xb,
	.video_prog_mode = 0xf0,
	.video_mode = 0x8,
	.sch_adjust = 0x20,
	.yc_delay = 0x333,
	.pixel_start = 227,
	.pixel_end = 1667,
	.top_field_line_start = 18,
	.top_field_line_end = 258,
	.bottom_field_line_start = 19,
	.bottom_field_line_end = 259,
	.video_saturation = 18,
	.video_contrast = 3,
	.video_brightness = 0,
	.video_hue = 0,
	.analog_sync_adj = 0x9c00,
};

void meson_venci_cvbs_mode_set(struct meson_drm *priv,
			       struct meson_cvbs_enci_mode *mode)
{
	if (mode->mode_tag == priv->venc.current_mode)
		return;

	/* CVBS Filter settings */
	writel_relaxed(0x12, priv->io_base + _REG(ENCI_CFILT_CTRL));
	writel_relaxed(0x12, priv->io_base + _REG(ENCI_CFILT_CTRL2));

	/* Digital Video Select : Interlace, clk27 clk, external */
	writel_relaxed(0, priv->io_base + _REG(VENC_DVI_SETTING));

	/* Reset Video Mode */
	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_MODE));
	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_MODE_ADV));

	/* Horizontal sync signal output */
	writel_relaxed(mode->hso_begin,
			priv->io_base + _REG(ENCI_SYNC_HSO_BEGIN));
	writel_relaxed(mode->hso_end,
			priv->io_base + _REG(ENCI_SYNC_HSO_END));

	/* Vertical Sync lines */
	writel_relaxed(mode->vso_even,
			priv->io_base + _REG(ENCI_SYNC_VSO_EVNLN));
	writel_relaxed(mode->vso_odd,
			priv->io_base + _REG(ENCI_SYNC_VSO_ODDLN));

	/* Macrovision max amplitude change */
	writel_relaxed(0x8100 + mode->macv_max_amp,
			priv->io_base + _REG(ENCI_MACV_MAX_AMP));

	/* Video mode */
	writel_relaxed(mode->video_prog_mode,
			priv->io_base + _REG(VENC_VIDEO_PROG_MODE));
	writel_relaxed(mode->video_mode,
			priv->io_base + _REG(ENCI_VIDEO_MODE));

	/* Advanced Video Mode :
	 * Demux shifting 0x2
	 * Blank line end at line17/22
	 * High bandwidth Luma Filter
	 * Low bandwidth Chroma Filter
	 * Bypass luma low pass filter
	 * No macrovision on CSYNC
	 */
	writel_relaxed(0x26, priv->io_base + _REG(ENCI_VIDEO_MODE_ADV));

	writel(mode->sch_adjust, priv->io_base + _REG(ENCI_VIDEO_SCH));

	/* Sync mode : MASTER Master mode, free run, send HSO/VSO out */
	writel_relaxed(0x07, priv->io_base + _REG(ENCI_SYNC_MODE));

	/* 0x3 Y, C, and Component Y delay */
	writel_relaxed(mode->yc_delay, priv->io_base + _REG(ENCI_YC_DELAY));

	/* Timings */
	writel_relaxed(mode->pixel_start,
			priv->io_base + _REG(ENCI_VFIFO2VD_PIXEL_START));
	writel_relaxed(mode->pixel_end,
			priv->io_base + _REG(ENCI_VFIFO2VD_PIXEL_END));

	writel_relaxed(mode->top_field_line_start,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_TOP_START));
	writel_relaxed(mode->top_field_line_end,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_TOP_END));

	writel_relaxed(mode->bottom_field_line_start,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_BOT_START));
	writel_relaxed(mode->bottom_field_line_end,
			priv->io_base + _REG(ENCI_VFIFO2VD_LINE_BOT_END));

	/* Internal Venc, Internal VIU Sync, Internal Vencoder */
	writel_relaxed(0, priv->io_base + _REG(VENC_SYNC_ROUTE));

	/* UNreset Interlaced TV Encoder */
	writel_relaxed(0, priv->io_base + _REG(ENCI_DBG_PX_RST));

	/* Enable Vfifo2vd, Y_Cb_Y_Cr select */
	writel_relaxed(0x4e01, priv->io_base + _REG(ENCI_VFIFO2VD_CTL));

	/* Power UP Dacs */
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_SETTING));

	/* Video Upsampling */
	writel_relaxed(0x0061, priv->io_base + _REG(VENC_UPSAMPLE_CTRL0));
	writel_relaxed(0x4061, priv->io_base + _REG(VENC_UPSAMPLE_CTRL1));
	writel_relaxed(0x5061, priv->io_base + _REG(VENC_UPSAMPLE_CTRL2));

	/* Select Interlace Y DACs */
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL0));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL1));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL2));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL3));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL4));
	writel_relaxed(0, priv->io_base + _REG(VENC_VDAC_DACSEL5));

	/* Select ENCI for VIU */
	meson_vpp_setup_mux(priv, MESON_VIU_VPP_MUX_ENCI);

	/* Enable ENCI FIFO */
	writel_relaxed(0x2000, priv->io_base + _REG(VENC_VDAC_FIFO_CTRL));

	/* Select ENCI DACs 0, 1, 4, and 5 */
	writel_relaxed(0x11, priv->io_base + _REG(ENCI_DACSEL_0));
	writel_relaxed(0x11, priv->io_base + _REG(ENCI_DACSEL_1));

	/* Interlace video enable */
	writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));

	/* Configure Video Saturation / Contrast / Brightness / Hue */
	writel_relaxed(mode->video_saturation,
			priv->io_base + _REG(ENCI_VIDEO_SAT));
	writel_relaxed(mode->video_contrast,
			priv->io_base + _REG(ENCI_VIDEO_CONT));
	writel_relaxed(mode->video_brightness,
			priv->io_base + _REG(ENCI_VIDEO_BRIGHT));
	writel_relaxed(mode->video_hue,
			priv->io_base + _REG(ENCI_VIDEO_HUE));

	/* Enable DAC0 Filter */
	writel_relaxed(0x1, priv->io_base + _REG(VENC_VDAC_DAC0_FILT_CTRL0));
	writel_relaxed(0xfc48, priv->io_base + _REG(VENC_VDAC_DAC0_FILT_CTRL1));

	/* 0 in Macrovision register 0 */
	writel_relaxed(0, priv->io_base + _REG(ENCI_MACV_N0));

	/* Analog Synchronization and color burst value adjust */
	writel_relaxed(mode->analog_sync_adj,
			priv->io_base + _REG(ENCI_SYNC_ADJ));

	/* Setup 27MHz vclk2 for ENCI and VDAC */
	meson_vclk_setup(priv, MESON_VCLK_TARGET_CVBS, MESON_VCLK_CVBS);

	priv->venc.current_mode = mode->mode_tag;
}

/* Returns the current ENCI field polarity */
unsigned int meson_venci_get_field(struct meson_drm *priv)
{
	return readl_relaxed(priv->io_base + _REG(ENCI_INFO_READ)) & BIT(29);
}

void meson_venc_enable_vsync(struct meson_drm *priv)
{
	writel_relaxed(2, priv->io_base + _REG(VENC_INTCTRL));
}

void meson_venc_disable_vsync(struct meson_drm *priv)
{
	writel_relaxed(0, priv->io_base + _REG(VENC_INTCTRL));
}

void meson_venc_init(struct meson_drm *priv)
{
	/* Disable CVBS VDAC */
	regmap_write(priv->hhi, HHI_VDAC_CNTL0, 0);
	regmap_write(priv->hhi, HHI_VDAC_CNTL1, 8);

	/* Power Down Dacs */
	writel_relaxed(0xff, priv->io_base + _REG(VENC_VDAC_SETTING));

	/* Disable HDMI PHY */
	regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0);

	/* Disable HDMI */
	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));

	/* Disable all encoders */
	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCL_VIDEO_EN));

	/* Disable VSync IRQ */
	meson_venc_disable_vsync(priv);

	priv->venc.current_mode = MESON_VENC_MODE_NONE;
}
