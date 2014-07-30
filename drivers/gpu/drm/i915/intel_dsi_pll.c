/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Shobhit Kumar <shobhit.kumar@intel.com>
 *	Yogesh Mohan Marimuthu <yogesh.mohan.marimuthu@intel.com>
 */

#include <linux/kernel.h>
#include "intel_drv.h"
#include "i915_drv.h"
#include "intel_dsi.h"

#define DSI_HSS_PACKET_SIZE		4
#define DSI_HSE_PACKET_SIZE		4
#define DSI_HSA_PACKET_EXTRA_SIZE	6
#define DSI_HBP_PACKET_EXTRA_SIZE	6
#define DSI_HACTIVE_PACKET_EXTRA_SIZE	6
#define DSI_HFP_PACKET_EXTRA_SIZE	6
#define DSI_EOTP_PACKET_SIZE		4

struct dsi_mnp {
	u32 dsi_pll_ctrl;
	u32 dsi_pll_div;
};

static const u32 lfsr_converts[] = {
	426, 469, 234, 373, 442, 221, 110, 311, 411,		/* 62 - 70 */
	461, 486, 243, 377, 188, 350, 175, 343, 427, 213,	/* 71 - 80 */
	106, 53, 282, 397, 354, 227, 113, 56, 284, 142,		/* 81 - 90 */
	71, 35							/* 91 - 92 */
};

#ifdef DSI_CLK_FROM_RR

static u32 dsi_rr_formula(const struct drm_display_mode *mode,
			  int pixel_format, int video_mode_format,
			  int lane_count, bool eotp)
{
	u32 bpp;
	u32 hactive, vactive, hfp, hsync, hbp, vfp, vsync, vbp;
	u32 hsync_bytes, hbp_bytes, hactive_bytes, hfp_bytes;
	u32 bytes_per_line, bytes_per_frame;
	u32 num_frames;
	u32 bytes_per_x_frames, bytes_per_x_frames_x_lanes;
	u32 dsi_bit_clock_hz;
	u32 dsi_clk;

	switch (pixel_format) {
	default:
	case VID_MODE_FORMAT_RGB888:
	case VID_MODE_FORMAT_RGB666_LOOSE:
		bpp = 24;
		break;
	case VID_MODE_FORMAT_RGB666:
		bpp = 18;
		break;
	case VID_MODE_FORMAT_RGB565:
		bpp = 16;
		break;
	}

	hactive = mode->hdisplay;
	vactive = mode->vdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hsync = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;

	vfp = mode->vsync_start - mode->vdisplay;
	vsync = mode->vsync_end - mode->vsync_start;
	vbp = mode->vtotal - mode->vsync_end;

	hsync_bytes = DIV_ROUND_UP(hsync * bpp, 8);
	hbp_bytes = DIV_ROUND_UP(hbp * bpp, 8);
	hactive_bytes = DIV_ROUND_UP(hactive * bpp, 8);
	hfp_bytes = DIV_ROUND_UP(hfp * bpp, 8);

	bytes_per_line = DSI_HSS_PACKET_SIZE + hsync_bytes +
		DSI_HSA_PACKET_EXTRA_SIZE + DSI_HSE_PACKET_SIZE +
		hbp_bytes + DSI_HBP_PACKET_EXTRA_SIZE +
		hactive_bytes + DSI_HACTIVE_PACKET_EXTRA_SIZE +
		hfp_bytes + DSI_HFP_PACKET_EXTRA_SIZE;

	/*
	 * XXX: Need to accurately calculate LP to HS transition timeout and add
	 * it to bytes_per_line/bytes_per_frame.
	 */

	if (eotp && video_mode_format == VIDEO_MODE_BURST)
		bytes_per_line += DSI_EOTP_PACKET_SIZE;

	bytes_per_frame = vsync * bytes_per_line + vbp * bytes_per_line +
		vactive * bytes_per_line + vfp * bytes_per_line;

	if (eotp &&
	    (video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE ||
	     video_mode_format == VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS))
		bytes_per_frame += DSI_EOTP_PACKET_SIZE;

	num_frames = drm_mode_vrefresh(mode);
	bytes_per_x_frames = num_frames * bytes_per_frame;

	bytes_per_x_frames_x_lanes = bytes_per_x_frames / lane_count;

	/* the dsi clock is divided by 2 in the hardware to get dsi ddr clock */
	dsi_bit_clock_hz = bytes_per_x_frames_x_lanes * 8;
	dsi_clk = dsi_bit_clock_hz / 1000;

	if (eotp && video_mode_format == VIDEO_MODE_BURST)
		dsi_clk *= 2;

	return dsi_clk;
}

#else

/* Get DSI clock from pixel clock */
static u32 dsi_clk_from_pclk(u32 pclk, int pixel_format, int lane_count)
{
	u32 dsi_clk_khz;
	u32 bpp;

	switch (pixel_format) {
	default:
	case VID_MODE_FORMAT_RGB888:
	case VID_MODE_FORMAT_RGB666_LOOSE:
		bpp = 24;
		break;
	case VID_MODE_FORMAT_RGB666:
		bpp = 18;
		break;
	case VID_MODE_FORMAT_RGB565:
		bpp = 16;
		break;
	}

	/* DSI data rate = pixel clock * bits per pixel / lane count
	   pixel clock is converted from KHz to Hz */
	dsi_clk_khz = DIV_ROUND_CLOSEST(pclk * bpp, lane_count);

	return dsi_clk_khz;
}

#endif

static int dsi_calc_mnp(u32 dsi_clk, struct dsi_mnp *dsi_mnp)
{
	u32 m, n, p;
	u32 ref_clk;
	u32 error;
	u32 tmp_error;
	int target_dsi_clk;
	int calc_dsi_clk;
	u32 calc_m;
	u32 calc_p;
	u32 m_seed;

	/* dsi_clk is expected in KHZ */
	if (dsi_clk < 300000 || dsi_clk > 1150000) {
		DRM_ERROR("DSI CLK Out of Range\n");
		return -ECHRNG;
	}

	ref_clk = 25000;
	target_dsi_clk = dsi_clk;
	error = 0xFFFFFFFF;
	tmp_error = 0xFFFFFFFF;
	calc_m = 0;
	calc_p = 0;

	for (m = 62; m <= 92; m++) {
		for (p = 2; p <= 6; p++) {
			/* Find the optimal m and p divisors
			with minimal error +/- the required clock */
			calc_dsi_clk = (m * ref_clk) / p;
			if (calc_dsi_clk == target_dsi_clk) {
				calc_m = m;
				calc_p = p;
				error = 0;
				break;
			} else
				tmp_error = abs(target_dsi_clk - calc_dsi_clk);

			if (tmp_error < error) {
				error = tmp_error;
				calc_m = m;
				calc_p = p;
			}
		}

		if (error == 0)
			break;
	}

	m_seed = lfsr_converts[calc_m - 62];
	n = 1;
	dsi_mnp->dsi_pll_ctrl = 1 << (DSI_PLL_P1_POST_DIV_SHIFT + calc_p - 2);
	dsi_mnp->dsi_pll_div = (n - 1) << DSI_PLL_N1_DIV_SHIFT |
		m_seed << DSI_PLL_M1_DIV_SHIFT;

	return 0;
}

/*
 * XXX: The muxing and gating is hard coded for now. Need to add support for
 * sharing PLLs with two DSI outputs.
 */
static void vlv_configure_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	int ret;
	struct dsi_mnp dsi_mnp;
	u32 dsi_clk;

	dsi_clk = dsi_clk_from_pclk(intel_dsi->pclk, intel_dsi->pixel_format,
						intel_dsi->lane_count);

	ret = dsi_calc_mnp(dsi_clk, &dsi_mnp);
	if (ret) {
		DRM_DEBUG_KMS("dsi_calc_mnp failed\n");
		return;
	}

	dsi_mnp.dsi_pll_ctrl |= DSI_PLL_CLK_GATE_DSI0_DSIPLL;

	DRM_DEBUG_KMS("dsi pll div %08x, ctrl %08x\n",
		      dsi_mnp.dsi_pll_div, dsi_mnp.dsi_pll_ctrl);

	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, 0);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_DIVIDER, dsi_mnp.dsi_pll_div);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, dsi_mnp.dsi_pll_ctrl);
}

void vlv_enable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->dpio_lock);

	vlv_configure_dsi_pll(encoder);

	/* wait at least 0.5 us after ungating before enabling VCO */
	usleep_range(1, 10);

	tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	tmp |= DSI_PLL_VCO_EN;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	mutex_unlock(&dev_priv->dpio_lock);

	if (wait_for(I915_READ(PIPECONF(PIPE_A)) & PIPECONF_DSI_PLL_LOCKED, 20)) {
		DRM_ERROR("DSI PLL lock failed\n");
		return;
	}

	DRM_DEBUG_KMS("DSI PLL locked\n");
}

void vlv_disable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->dpio_lock);

	tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	tmp &= ~DSI_PLL_VCO_EN;
	tmp |= DSI_PLL_LDO_GATE;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	mutex_unlock(&dev_priv->dpio_lock);
}

static void assert_bpp_mismatch(int pixel_format, int pipe_bpp)
{
	int bpp;

	switch (pixel_format) {
	default:
	case VID_MODE_FORMAT_RGB888:
	case VID_MODE_FORMAT_RGB666_LOOSE:
		bpp = 24;
		break;
	case VID_MODE_FORMAT_RGB666:
		bpp = 18;
		break;
	case VID_MODE_FORMAT_RGB565:
		bpp = 16;
		break;
	}

	WARN(bpp != pipe_bpp,
		"bpp match assertion failure (expected %d, current %d)\n",
		bpp, pipe_bpp);
}

u32 vlv_get_dsi_pclk(struct intel_encoder *encoder, int pipe_bpp)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 dsi_clock, pclk;
	u32 pll_ctl, pll_div;
	u32 m = 0, p = 0;
	int refclk = 25000;
	int i;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->dpio_lock);
	pll_ctl = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	pll_div = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_DIVIDER);
	mutex_unlock(&dev_priv->dpio_lock);

	/* mask out other bits and extract the P1 divisor */
	pll_ctl &= DSI_PLL_P1_POST_DIV_MASK;
	pll_ctl = pll_ctl >> (DSI_PLL_P1_POST_DIV_SHIFT - 2);

	/* mask out the other bits and extract the M1 divisor */
	pll_div &= DSI_PLL_M1_DIV_MASK;
	pll_div = pll_div >> DSI_PLL_M1_DIV_SHIFT;

	while (pll_ctl) {
		pll_ctl = pll_ctl >> 1;
		p++;
	}
	p--;

	if (!p) {
		DRM_ERROR("wrong P1 divisor\n");
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(lfsr_converts); i++) {
		if (lfsr_converts[i] == pll_div)
			break;
	}

	if (i == ARRAY_SIZE(lfsr_converts)) {
		DRM_ERROR("wrong m_seed programmed\n");
		return 0;
	}

	m = i + 62;

	dsi_clock = (m * refclk) / p;

	/* pixel_format and pipe_bpp should agree */
	assert_bpp_mismatch(intel_dsi->pixel_format, pipe_bpp);

	pclk = DIV_ROUND_CLOSEST(dsi_clock * intel_dsi->lane_count, pipe_bpp);

	return pclk;
}
