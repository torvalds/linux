// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022 Marek Vasut <marex@denx.de>
 *
 * This code is based on drivers/gpu/drm/mxsfb/mxsfb*
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/media-bus-format.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>

#include "lcdif_drv.h"
#include "lcdif_regs.h"

struct lcdif_crtc_state {
	struct drm_crtc_state	base;	/* always be the first member */
	u32			bus_format;
	u32			bus_flags;
};

static inline struct lcdif_crtc_state *
to_lcdif_crtc_state(struct drm_crtc_state *s)
{
	return container_of(s, struct lcdif_crtc_state, base);
}

/* -----------------------------------------------------------------------------
 * CRTC
 */

/*
 * For conversion from YCbCr to RGB, the CSC operates as follows:
 *
 * |R|   |A1 A2 A3|   |Y  + D1|
 * |G| = |B1 B2 B3| * |Cb + D2|
 * |B|   |C1 C2 C3|   |Cr + D3|
 *
 * The A, B and C coefficients are expressed as Q2.8 fixed point values, and
 * the D coefficients as Q0.8. Despite the reference manual stating the
 * opposite, the D1, D2 and D3 offset values are added to Y, Cb and Cr, not
 * subtracted. They must thus be programmed with negative values.
 */
static const u32 lcdif_yuv2rgb_coeffs[3][2][6] = {
	[DRM_COLOR_YCBCR_BT601] = {
		[DRM_COLOR_YCBCR_LIMITED_RANGE] = {
			/*
			 * BT.601 limited range:
			 *
			 * |R|   |1.1644  0.0000  1.5960|   |Y  - 16 |
			 * |G| = |1.1644 -0.3917 -0.8129| * |Cb - 128|
			 * |B|   |1.1644  2.0172  0.0000|   |Cr - 128|
			 */
			CSC0_COEF0_A1(0x12a) | CSC0_COEF0_A2(0x000),
			CSC0_COEF1_A3(0x199) | CSC0_COEF1_B1(0x12a),
			CSC0_COEF2_B2(0x79c) | CSC0_COEF2_B3(0x730),
			CSC0_COEF3_C1(0x12a) | CSC0_COEF3_C2(0x204),
			CSC0_COEF4_C3(0x000) | CSC0_COEF4_D1(0x1f0),
			CSC0_COEF5_D2(0x180) | CSC0_COEF5_D3(0x180),
		},
		[DRM_COLOR_YCBCR_FULL_RANGE] = {
			/*
			 * BT.601 full range:
			 *
			 * |R|   |1.0000  0.0000  1.4020|   |Y  - 0  |
			 * |G| = |1.0000 -0.3441 -0.7141| * |Cb - 128|
			 * |B|   |1.0000  1.7720  0.0000|   |Cr - 128|
			 */
			CSC0_COEF0_A1(0x100) | CSC0_COEF0_A2(0x000),
			CSC0_COEF1_A3(0x167) | CSC0_COEF1_B1(0x100),
			CSC0_COEF2_B2(0x7a8) | CSC0_COEF2_B3(0x749),
			CSC0_COEF3_C1(0x100) | CSC0_COEF3_C2(0x1c6),
			CSC0_COEF4_C3(0x000) | CSC0_COEF4_D1(0x000),
			CSC0_COEF5_D2(0x180) | CSC0_COEF5_D3(0x180),
		},
	},
	[DRM_COLOR_YCBCR_BT709] = {
		[DRM_COLOR_YCBCR_LIMITED_RANGE] = {
			/*
			 * Rec.709 limited range:
			 *
			 * |R|   |1.1644  0.0000  1.7927|   |Y  - 16 |
			 * |G| = |1.1644 -0.2132 -0.5329| * |Cb - 128|
			 * |B|   |1.1644  2.1124  0.0000|   |Cr - 128|
			 */
			CSC0_COEF0_A1(0x12a) | CSC0_COEF0_A2(0x000),
			CSC0_COEF1_A3(0x1cb) | CSC0_COEF1_B1(0x12a),
			CSC0_COEF2_B2(0x7c9) | CSC0_COEF2_B3(0x778),
			CSC0_COEF3_C1(0x12a) | CSC0_COEF3_C2(0x21d),
			CSC0_COEF4_C3(0x000) | CSC0_COEF4_D1(0x1f0),
			CSC0_COEF5_D2(0x180) | CSC0_COEF5_D3(0x180),
		},
		[DRM_COLOR_YCBCR_FULL_RANGE] = {
			/*
			 * Rec.709 full range:
			 *
			 * |R|   |1.0000  0.0000  1.5748|   |Y  - 0  |
			 * |G| = |1.0000 -0.1873 -0.4681| * |Cb - 128|
			 * |B|   |1.0000  1.8556  0.0000|   |Cr - 128|
			 */
			CSC0_COEF0_A1(0x100) | CSC0_COEF0_A2(0x000),
			CSC0_COEF1_A3(0x193) | CSC0_COEF1_B1(0x100),
			CSC0_COEF2_B2(0x7d0) | CSC0_COEF2_B3(0x788),
			CSC0_COEF3_C1(0x100) | CSC0_COEF3_C2(0x1db),
			CSC0_COEF4_C3(0x000) | CSC0_COEF4_D1(0x000),
			CSC0_COEF5_D2(0x180) | CSC0_COEF5_D3(0x180),
		},
	},
	[DRM_COLOR_YCBCR_BT2020] = {
		[DRM_COLOR_YCBCR_LIMITED_RANGE] = {
			/*
			 * BT.2020 limited range:
			 *
			 * |R|   |1.1644  0.0000  1.6787|   |Y  - 16 |
			 * |G| = |1.1644 -0.1874 -0.6505| * |Cb - 128|
			 * |B|   |1.1644  2.1418  0.0000|   |Cr - 128|
			 */
			CSC0_COEF0_A1(0x12a) | CSC0_COEF0_A2(0x000),
			CSC0_COEF1_A3(0x1ae) | CSC0_COEF1_B1(0x12a),
			CSC0_COEF2_B2(0x7d0) | CSC0_COEF2_B3(0x759),
			CSC0_COEF3_C1(0x12a) | CSC0_COEF3_C2(0x224),
			CSC0_COEF4_C3(0x000) | CSC0_COEF4_D1(0x1f0),
			CSC0_COEF5_D2(0x180) | CSC0_COEF5_D3(0x180),
		},
		[DRM_COLOR_YCBCR_FULL_RANGE] = {
			/*
			 * BT.2020 full range:
			 *
			 * |R|   |1.0000  0.0000  1.4746|   |Y  - 0  |
			 * |G| = |1.0000 -0.1646 -0.5714| * |Cb - 128|
			 * |B|   |1.0000  1.8814  0.0000|   |Cr - 128|
			 */
			CSC0_COEF0_A1(0x100) | CSC0_COEF0_A2(0x000),
			CSC0_COEF1_A3(0x179) | CSC0_COEF1_B1(0x100),
			CSC0_COEF2_B2(0x7d6) | CSC0_COEF2_B3(0x76e),
			CSC0_COEF3_C1(0x100) | CSC0_COEF3_C2(0x1e2),
			CSC0_COEF4_C3(0x000) | CSC0_COEF4_D1(0x000),
			CSC0_COEF5_D2(0x180) | CSC0_COEF5_D3(0x180),
		},
	},
};

static void lcdif_set_formats(struct lcdif_drm_private *lcdif,
			      struct drm_plane_state *plane_state,
			      const u32 bus_format)
{
	struct drm_device *drm = lcdif->drm;
	const u32 format = plane_state->fb->format->format;
	bool in_yuv = false;
	bool out_yuv = false;

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
		out_yuv = true;
		break;
	default:
		dev_err(drm->dev, "Unknown media bus format 0x%x\n", bus_format);
		break;
	}

	switch (format) {
	/* RGB Formats */
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

	/* YUV Formats */
	case DRM_FORMAT_YUYV:
		writel(CTRLDESCL0_5_BPP_YCbCr422 | CTRLDESCL0_5_YUV_FORMAT_VY2UY1,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		in_yuv = true;
		break;
	case DRM_FORMAT_YVYU:
		writel(CTRLDESCL0_5_BPP_YCbCr422 | CTRLDESCL0_5_YUV_FORMAT_UY2VY1,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		in_yuv = true;
		break;
	case DRM_FORMAT_UYVY:
		writel(CTRLDESCL0_5_BPP_YCbCr422 | CTRLDESCL0_5_YUV_FORMAT_Y2VY1U,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		in_yuv = true;
		break;
	case DRM_FORMAT_VYUY:
		writel(CTRLDESCL0_5_BPP_YCbCr422 | CTRLDESCL0_5_YUV_FORMAT_Y2UY1V,
		       lcdif->base + LCDC_V8_CTRLDESCL0_5);
		in_yuv = true;
		break;

	default:
		dev_err(drm->dev, "Unknown pixel format 0x%x\n", format);
		break;
	}

	/*
	 * The CSC differentiates between "YCbCr" and "YUV", but the reference
	 * manual doesn't detail how they differ. Experiments showed that the
	 * luminance value is unaffected, only the calculations involving chroma
	 * values differ. The YCbCr mode behaves as expected, with chroma values
	 * being offset by 128. The YUV mode isn't fully understood.
	 */
	if (!in_yuv && out_yuv) {
		/* RGB -> YCbCr */
		writel(CSC0_CTRL_CSC_MODE_RGB2YCbCr,
		       lcdif->base + LCDC_V8_CSC0_CTRL);

		/*
		 * CSC: BT.601 Limited Range RGB to YCbCr coefficients.
		 *
		 * |Y |   | 0.2568  0.5041  0.0979|   |R|   |16 |
		 * |Cb| = |-0.1482 -0.2910  0.4392| * |G| + |128|
		 * |Cr|   | 0.4392  0.4392 -0.3678|   |B|   |128|
		 */
		writel(CSC0_COEF0_A2(0x081) | CSC0_COEF0_A1(0x041),
		       lcdif->base + LCDC_V8_CSC0_COEF0);
		writel(CSC0_COEF1_B1(0x7db) | CSC0_COEF1_A3(0x019),
		       lcdif->base + LCDC_V8_CSC0_COEF1);
		writel(CSC0_COEF2_B3(0x070) | CSC0_COEF2_B2(0x7b6),
		       lcdif->base + LCDC_V8_CSC0_COEF2);
		writel(CSC0_COEF3_C2(0x7a2) | CSC0_COEF3_C1(0x070),
		       lcdif->base + LCDC_V8_CSC0_COEF3);
		writel(CSC0_COEF4_D1(0x010) | CSC0_COEF4_C3(0x7ee),
		       lcdif->base + LCDC_V8_CSC0_COEF4);
		writel(CSC0_COEF5_D3(0x080) | CSC0_COEF5_D2(0x080),
		       lcdif->base + LCDC_V8_CSC0_COEF5);
	} else if (in_yuv && !out_yuv) {
		/* YCbCr -> RGB */
		const u32 *coeffs =
			lcdif_yuv2rgb_coeffs[plane_state->color_encoding]
					    [plane_state->color_range];

		writel(CSC0_CTRL_CSC_MODE_YCbCr2RGB,
		       lcdif->base + LCDC_V8_CSC0_CTRL);

		writel(coeffs[0], lcdif->base + LCDC_V8_CSC0_COEF0);
		writel(coeffs[1], lcdif->base + LCDC_V8_CSC0_COEF1);
		writel(coeffs[2], lcdif->base + LCDC_V8_CSC0_COEF2);
		writel(coeffs[3], lcdif->base + LCDC_V8_CSC0_COEF3);
		writel(coeffs[4], lcdif->base + LCDC_V8_CSC0_COEF4);
		writel(coeffs[5], lcdif->base + LCDC_V8_CSC0_COEF5);
	} else {
		/* RGB -> RGB, YCbCr -> YCbCr: bypass colorspace converter. */
		writel(CSC0_CTRL_BYPASS, lcdif->base + LCDC_V8_CSC0_CTRL);
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

	writel(DISP_SIZE_DELTA_Y(m->vdisplay) |
	       DISP_SIZE_DELTA_X(m->hdisplay),
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

	writel(CTRLDESCL0_1_HEIGHT(m->vdisplay) |
	       CTRLDESCL0_1_WIDTH(m->hdisplay),
	       lcdif->base + LCDC_V8_CTRLDESCL0_1);

	/*
	 * Undocumented P_SIZE and T_SIZE register but those written in the
	 * downstream kernel those registers control the AXI burst size. As of
	 * now there are two known values:
	 *  1 - 128Byte
	 *  2 - 256Byte
	 * Downstream set it to 256B burst size to improve the memory
	 * efficiency so set it here too.
	 */
	ctrl = CTRLDESCL0_3_P_SIZE(2) | CTRLDESCL0_3_T_SIZE(2) |
	       CTRLDESCL0_3_PITCH(lcdif->crtc.primary->state->fb->pitches[0]);
	writel(ctrl, lcdif->base + LCDC_V8_CTRLDESCL0_3);
}

static void lcdif_enable_controller(struct lcdif_drm_private *lcdif)
{
	u32 reg;

	/* Set FIFO Panic watermarks, low 1/3, high 2/3 . */
	writel(FIELD_PREP(PANIC0_THRES_LOW_MASK, 1 * PANIC0_THRES_MAX / 3) |
	       FIELD_PREP(PANIC0_THRES_HIGH_MASK, 2 * PANIC0_THRES_MAX / 3),
	       lcdif->base + LCDC_V8_PANIC0_THRES);

	/*
	 * Enable FIFO Panic, this does not generate interrupt, but
	 * boosts NoC priority based on FIFO Panic watermarks.
	 */
	writel(INT_ENABLE_D1_PLANE_PANIC_EN,
	       lcdif->base + LCDC_V8_INT_ENABLE_D1);

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

	/* Disable FIFO Panic NoC priority booster. */
	writel(0, lcdif->base + LCDC_V8_INT_ENABLE_D1);
}

static void lcdif_reset_block(struct lcdif_drm_private *lcdif)
{
	writel(CTRL_SW_RESET, lcdif->base + LCDC_V8_CTRL + REG_SET);
	readl(lcdif->base + LCDC_V8_CTRL);
	writel(CTRL_SW_RESET, lcdif->base + LCDC_V8_CTRL + REG_CLR);
	readl(lcdif->base + LCDC_V8_CTRL);
}

static void lcdif_crtc_mode_set_nofb(struct drm_crtc_state *crtc_state,
				     struct drm_plane_state *plane_state)
{
	struct lcdif_crtc_state *lcdif_crtc_state = to_lcdif_crtc_state(crtc_state);
	struct drm_device *drm = crtc_state->crtc->dev;
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(drm);
	struct drm_display_mode *m = &crtc_state->adjusted_mode;

	DRM_DEV_DEBUG_DRIVER(drm->dev, "Pixel clock: %dkHz (actual: %dkHz)\n",
			     m->clock, (int)(clk_get_rate(lcdif->clk) / 1000));
	DRM_DEV_DEBUG_DRIVER(drm->dev, "Bridge bus_flags: 0x%08X\n",
			     lcdif_crtc_state->bus_flags);
	DRM_DEV_DEBUG_DRIVER(drm->dev, "Mode flags: 0x%08X\n", m->flags);

	/* Mandatory eLCDIF reset as per the Reference Manual */
	lcdif_reset_block(lcdif);

	lcdif_set_formats(lcdif, plane_state, lcdif_crtc_state->bus_format);

	lcdif_set_mode(lcdif, lcdif_crtc_state->bus_flags);
}

static int lcdif_crtc_atomic_check(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct drm_device *drm = crtc->dev;
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct lcdif_crtc_state *lcdif_crtc_state = to_lcdif_crtc_state(crtc_state);
	bool has_primary = crtc_state->plane_mask &
			   drm_plane_mask(crtc->primary);
	struct drm_connector_state *connector_state;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_bridge_state *bridge_state;
	u32 bus_format, bus_flags;
	bool format_set = false, flags_set = false;
	int ret, i;

	/* The primary plane has to be enabled when the CRTC is active. */
	if (crtc_state->active && !has_primary)
		return -EINVAL;

	ret = drm_atomic_add_affected_planes(state, crtc);
	if (ret)
		return ret;

	/* Try to find consistent bus format and flags across first bridges. */
	for_each_new_connector_in_state(state, connector, connector_state, i) {
		if (!connector_state->crtc)
			continue;

		encoder = connector_state->best_encoder;

		struct drm_bridge *bridge __free(drm_bridge_put) =
			drm_bridge_chain_get_first_bridge(encoder);
		if (!bridge)
			continue;

		bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
		if (!bridge_state)
			bus_format = MEDIA_BUS_FMT_FIXED;
		else
			bus_format = bridge_state->input_bus_cfg.format;

		if (bus_format == MEDIA_BUS_FMT_FIXED) {
			dev_warn(drm->dev,
				 "[ENCODER:%d:%s]'s bridge does not provide bus format, assuming MEDIA_BUS_FMT_RGB888_1X24.\n"
				 "Please fix bridge driver by handling atomic_get_input_bus_fmts.\n",
				 encoder->base.id, encoder->name);
			bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		} else if (!bus_format) {
			/* If all else fails, default to RGB888_1X24 */
			bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		}

		if (!format_set) {
			lcdif_crtc_state->bus_format = bus_format;
			format_set = true;
		} else if (lcdif_crtc_state->bus_format != bus_format) {
			DRM_DEV_DEBUG_DRIVER(drm->dev, "inconsistent bus format\n");
			return -EINVAL;
		}

		if (bridge->timings)
			bus_flags = bridge->timings->input_bus_flags;
		else if (bridge_state)
			bus_flags = bridge_state->input_bus_cfg.flags;
		else
			bus_flags = 0;

		if (!flags_set) {
			lcdif_crtc_state->bus_flags = bus_flags;
			flags_set = true;
		} else if (lcdif_crtc_state->bus_flags != bus_flags) {
			DRM_DEV_DEBUG_DRIVER(drm->dev, "inconsistent bus flags\n");
			return -EINVAL;
		}
	}

	return 0;
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
	struct drm_crtc_state *new_cstate = drm_atomic_get_new_crtc_state(state, crtc);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state,
									    crtc->primary);
	struct drm_display_mode *m = &lcdif->crtc.state->adjusted_mode;
	struct drm_device *drm = lcdif->drm;
	dma_addr_t paddr;

	clk_set_rate(lcdif->clk, m->clock * 1000);

	pm_runtime_get_sync(drm->dev);

	lcdif_crtc_mode_set_nofb(new_cstate, new_pstate);

	/* Write cur_buf as well to avoid an initial corrupt frame */
	paddr = drm_fb_dma_get_gem_addr(new_pstate->fb, new_pstate, 0);
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

static void lcdif_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					    struct drm_crtc_state *state)
{
	__drm_atomic_helper_crtc_destroy_state(state);
	kfree(to_lcdif_crtc_state(state));
}

static void lcdif_crtc_reset(struct drm_crtc *crtc)
{
	struct lcdif_crtc_state *state;

	if (crtc->state)
		lcdif_crtc_atomic_destroy_state(crtc, crtc->state);

	crtc->state = NULL;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_crtc_reset(crtc, &state->base);
}

static struct drm_crtc_state *
lcdif_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct lcdif_crtc_state *old = to_lcdif_crtc_state(crtc->state);
	struct lcdif_crtc_state *new;

	if (WARN_ON(!crtc->state))
		return NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &new->base);

	new->bus_format = old->bus_format;
	new->bus_flags = old->bus_flags;

	return &new->base;
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
	.reset = lcdif_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = lcdif_crtc_atomic_duplicate_state,
	.atomic_destroy_state = lcdif_crtc_atomic_destroy_state,
	.enable_vblank = lcdif_crtc_enable_vblank,
	.disable_vblank = lcdif_crtc_disable_vblank,
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
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   false, true);
}

static void lcdif_plane_primary_atomic_update(struct drm_plane *plane,
					      struct drm_atomic_state *state)
{
	struct lcdif_drm_private *lcdif = to_lcdif_drm_private(plane->dev);
	struct drm_plane_state *new_pstate = drm_atomic_get_new_plane_state(state,
									    plane);
	dma_addr_t paddr;

	paddr = drm_fb_dma_get_gem_addr(new_pstate->fb, new_pstate, 0);
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
	/* RGB */
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XRGB8888,

	/* Packed YCbCr */
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
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
	const u32 supported_encodings = BIT(DRM_COLOR_YCBCR_BT601) |
					BIT(DRM_COLOR_YCBCR_BT709) |
					BIT(DRM_COLOR_YCBCR_BT2020);
	const u32 supported_ranges = BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
				     BIT(DRM_COLOR_YCBCR_FULL_RANGE);
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

	ret = drm_plane_create_color_properties(&lcdif->planes.primary,
						supported_encodings,
						supported_ranges,
						DRM_COLOR_YCBCR_BT601,
						DRM_COLOR_YCBCR_LIMITED_RANGE);
	if (ret)
		return ret;

	drm_crtc_helper_add(crtc, &lcdif_crtc_helper_funcs);
	return drm_crtc_init_with_planes(lcdif->drm, crtc,
					 &lcdif->planes.primary, NULL,
					 &lcdif_crtc_funcs, NULL);
}
