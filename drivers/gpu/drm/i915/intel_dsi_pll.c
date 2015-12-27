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

static int dsi_pixel_format_bpp(int pixel_format)
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

	return bpp;
}

struct dsi_mnp {
	u32 dsi_pll_ctrl;
	u32 dsi_pll_div;
};

static const u32 lfsr_converts[] = {
	426, 469, 234, 373, 442, 221, 110, 311, 411,		/* 62 - 70 */
	461, 486, 243, 377, 188, 350, 175, 343, 427, 213,	/* 71 - 80 */
	106, 53, 282, 397, 454, 227, 113, 56, 284, 142,		/* 81 - 90 */
	71, 35, 273, 136, 324, 418, 465, 488, 500, 506		/* 91 - 100 */
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

	bpp = dsi_pixel_format_bpp(pixel_format);

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
	u32 bpp = dsi_pixel_format_bpp(pixel_format);

	/* DSI data rate = pixel clock * bits per pixel / lane count
	   pixel clock is converted from KHz to Hz */
	dsi_clk_khz = DIV_ROUND_CLOSEST(pclk * bpp, lane_count);

	return dsi_clk_khz;
}

#endif

static int dsi_calc_mnp(struct drm_i915_private *dev_priv,
			struct dsi_mnp *dsi_mnp, int target_dsi_clk)
{
	unsigned int calc_m = 0, calc_p = 0;
	unsigned int m_min, m_max, p_min = 2, p_max = 6;
	unsigned int m, n, p;
	int ref_clk;
	int delta = target_dsi_clk;
	u32 m_seed;

	/* target_dsi_clk is expected in kHz */
	if (target_dsi_clk < 300000 || target_dsi_clk > 1150000) {
		DRM_ERROR("DSI CLK Out of Range\n");
		return -ECHRNG;
	}

	if (IS_CHERRYVIEW(dev_priv)) {
		ref_clk = 100000;
		n = 4;
		m_min = 70;
		m_max = 96;
	} else {
		ref_clk = 25000;
		n = 1;
		m_min = 62;
		m_max = 92;
	}

	for (m = m_min; m <= m_max && delta; m++) {
		for (p = p_min; p <= p_max && delta; p++) {
			/*
			 * Find the optimal m and p divisors with minimal delta
			 * +/- the required clock
			 */
			int calc_dsi_clk = (m * ref_clk) / (p * n);
			int d = abs(target_dsi_clk - calc_dsi_clk);
			if (d < delta) {
				delta = d;
				calc_m = m;
				calc_p = p;
			}
		}
	}

	/* register has log2(N1), this works fine for powers of two */
	n = ffs(n) - 1;
	m_seed = lfsr_converts[calc_m - 62];
	dsi_mnp->dsi_pll_ctrl = 1 << (DSI_PLL_P1_POST_DIV_SHIFT + calc_p - 2);
	dsi_mnp->dsi_pll_div = n << DSI_PLL_N1_DIV_SHIFT |
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

	ret = dsi_calc_mnp(dev_priv, &dsi_mnp, dsi_clk);
	if (ret) {
		DRM_DEBUG_KMS("dsi_calc_mnp failed\n");
		return;
	}

	if (intel_dsi->ports & (1 << PORT_A))
		dsi_mnp.dsi_pll_ctrl |= DSI_PLL_CLK_GATE_DSI0_DSIPLL;

	if (intel_dsi->ports & (1 << PORT_C))
		dsi_mnp.dsi_pll_ctrl |= DSI_PLL_CLK_GATE_DSI1_DSIPLL;

	DRM_DEBUG_KMS("dsi pll div %08x, ctrl %08x\n",
		      dsi_mnp.dsi_pll_div, dsi_mnp.dsi_pll_ctrl);

	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, 0);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_DIVIDER, dsi_mnp.dsi_pll_div);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, dsi_mnp.dsi_pll_ctrl);
}

static void vlv_enable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->sb_lock);

	vlv_configure_dsi_pll(encoder);

	/* wait at least 0.5 us after ungating before enabling VCO */
	usleep_range(1, 10);

	tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	tmp |= DSI_PLL_VCO_EN;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	if (wait_for(vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL) &
						DSI_PLL_LOCK, 20)) {

		mutex_unlock(&dev_priv->sb_lock);
		DRM_ERROR("DSI PLL lock failed\n");
		return;
	}
	mutex_unlock(&dev_priv->sb_lock);

	DRM_DEBUG_KMS("DSI PLL locked\n");
}

static void vlv_disable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->sb_lock);

	tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	tmp &= ~DSI_PLL_VCO_EN;
	tmp |= DSI_PLL_LDO_GATE;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	mutex_unlock(&dev_priv->sb_lock);
}

static void bxt_disable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	u32 val;

	DRM_DEBUG_KMS("\n");

	val = I915_READ(BXT_DSI_PLL_ENABLE);
	val &= ~BXT_DSI_PLL_DO_ENABLE;
	I915_WRITE(BXT_DSI_PLL_ENABLE, val);

	/*
	 * PLL lock should deassert within 200us.
	 * Wait up to 1ms before timing out.
	 */
	if (wait_for((I915_READ(BXT_DSI_PLL_ENABLE)
					& BXT_DSI_PLL_LOCKED) == 0, 1))
		DRM_ERROR("Timeout waiting for PLL lock deassertion\n");
}

static void assert_bpp_mismatch(int pixel_format, int pipe_bpp)
{
	int bpp = dsi_pixel_format_bpp(pixel_format);

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
	u32 m = 0, p = 0, n;
	int refclk = 25000;
	int i;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->sb_lock);
	pll_ctl = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	pll_div = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_DIVIDER);
	mutex_unlock(&dev_priv->sb_lock);

	/* mask out other bits and extract the P1 divisor */
	pll_ctl &= DSI_PLL_P1_POST_DIV_MASK;
	pll_ctl = pll_ctl >> (DSI_PLL_P1_POST_DIV_SHIFT - 2);

	/* N1 divisor */
	n = (pll_div & DSI_PLL_N1_DIV_MASK) >> DSI_PLL_N1_DIV_SHIFT;
	n = 1 << n; /* register has log2(N1) */

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

	dsi_clock = (m * refclk) / (p * n);

	/* pixel_format and pipe_bpp should agree */
	assert_bpp_mismatch(intel_dsi->pixel_format, pipe_bpp);

	pclk = DIV_ROUND_CLOSEST(dsi_clock * intel_dsi->lane_count, pipe_bpp);

	return pclk;
}

u32 bxt_get_dsi_pclk(struct intel_encoder *encoder, int pipe_bpp)
{
	u32 pclk;
	u32 dsi_clk;
	u32 dsi_ratio;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;

	/* Divide by zero */
	if (!pipe_bpp) {
		DRM_ERROR("Invalid BPP(0)\n");
		return 0;
	}

	dsi_ratio = I915_READ(BXT_DSI_PLL_CTL) &
				BXT_DSI_PLL_RATIO_MASK;

	/* Invalid DSI ratio ? */
	if (dsi_ratio < BXT_DSI_PLL_RATIO_MIN ||
			dsi_ratio > BXT_DSI_PLL_RATIO_MAX) {
		DRM_ERROR("Invalid DSI pll ratio(%u) programmed\n", dsi_ratio);
		return 0;
	}

	dsi_clk = (dsi_ratio * BXT_REF_CLOCK_KHZ) / 2;

	/* pixel_format and pipe_bpp should agree */
	assert_bpp_mismatch(intel_dsi->pixel_format, pipe_bpp);

	pclk = DIV_ROUND_CLOSEST(dsi_clk * intel_dsi->lane_count, pipe_bpp);

	DRM_DEBUG_DRIVER("Calculated pclk=%u\n", pclk);
	return pclk;
}

static void vlv_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
{
	u32 temp;
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);

	temp = I915_READ(MIPI_CTRL(port));
	temp &= ~ESCAPE_CLOCK_DIVIDER_MASK;
	I915_WRITE(MIPI_CTRL(port), temp |
			intel_dsi->escape_clk_div <<
			ESCAPE_CLOCK_DIVIDER_SHIFT);
}

/* Program BXT Mipi clocks and dividers */
static void bxt_dsi_program_clocks(struct drm_device *dev, enum port port)
{
	u32 tmp;
	u32 divider;
	u32 dsi_rate;
	u32 pll_ratio;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Clear old configurations */
	tmp = I915_READ(BXT_MIPI_CLOCK_CTL);
	tmp &= ~(BXT_MIPI_TX_ESCLK_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_RX_ESCLK_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_ESCLK_VAR_DIV_MASK(port));
	tmp &= ~(BXT_MIPI_DPHY_DIVIDER_MASK(port));

	/* Get the current DSI rate(actual) */
	pll_ratio = I915_READ(BXT_DSI_PLL_CTL) &
				BXT_DSI_PLL_RATIO_MASK;
	dsi_rate = (BXT_REF_CLOCK_KHZ * pll_ratio) / 2;

	/* Max possible output of clock is 39.5 MHz, program value -1 */
	divider = (dsi_rate / BXT_MAX_VAR_OUTPUT_KHZ) - 1;
	tmp |= BXT_MIPI_ESCLK_VAR_DIV(port, divider);

	/*
	 * Tx escape clock must be as close to 20MHz possible, but should
	 * not exceed it. Hence select divide by 2
	 */
	tmp |= BXT_MIPI_TX_ESCLK_8XDIV_BY2(port);

	tmp |= BXT_MIPI_RX_ESCLK_8X_BY3(port);

	I915_WRITE(BXT_MIPI_CLOCK_CTL, tmp);
}

static bool bxt_configure_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u8 dsi_ratio;
	u32 dsi_clk;
	u32 val;

	dsi_clk = dsi_clk_from_pclk(intel_dsi->pclk, intel_dsi->pixel_format,
			intel_dsi->lane_count);

	/*
	 * From clock diagram, to get PLL ratio divider, divide double of DSI
	 * link rate (i.e., 2*8x=16x frequency value) by ref clock. Make sure to
	 * round 'up' the result
	 */
	dsi_ratio = DIV_ROUND_UP(dsi_clk * 2, BXT_REF_CLOCK_KHZ);
	if (dsi_ratio < BXT_DSI_PLL_RATIO_MIN ||
			dsi_ratio > BXT_DSI_PLL_RATIO_MAX) {
		DRM_ERROR("Cant get a suitable ratio from DSI PLL ratios\n");
		return false;
	}

	/*
	 * Program DSI ratio and Select MIPIC and MIPIA PLL output as 8x
	 * Spec says both have to be programmed, even if one is not getting
	 * used. Configure MIPI_CLOCK_CTL dividers in modeset
	 */
	val = I915_READ(BXT_DSI_PLL_CTL);
	val &= ~BXT_DSI_PLL_PVD_RATIO_MASK;
	val &= ~BXT_DSI_FREQ_SEL_MASK;
	val &= ~BXT_DSI_PLL_RATIO_MASK;
	val |= (dsi_ratio | BXT_DSIA_16X_BY2 | BXT_DSIC_16X_BY2);

	/* As per recommendation from hardware team,
	 * Prog PVD ratio =1 if dsi ratio <= 50
	 */
	if (dsi_ratio <= 50) {
		val &= ~BXT_DSI_PLL_PVD_RATIO_MASK;
		val |= BXT_DSI_PLL_PVD_RATIO_1;
	}

	I915_WRITE(BXT_DSI_PLL_CTL, val);
	POSTING_READ(BXT_DSI_PLL_CTL);

	return true;
}

static void bxt_enable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = encoder->base.dev->dev_private;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 val;

	DRM_DEBUG_KMS("\n");

	val = I915_READ(BXT_DSI_PLL_ENABLE);

	if (val & BXT_DSI_PLL_DO_ENABLE) {
		WARN(1, "DSI PLL already enabled. Disabling it.\n");
		val &= ~BXT_DSI_PLL_DO_ENABLE;
		I915_WRITE(BXT_DSI_PLL_ENABLE, val);
	}

	/* Configure PLL vales */
	if (!bxt_configure_dsi_pll(encoder)) {
		DRM_ERROR("Configure DSI PLL failed, abort PLL enable\n");
		return;
	}

	/* Program TX, RX, Dphy clocks */
	for_each_dsi_port(port, intel_dsi->ports)
		bxt_dsi_program_clocks(encoder->base.dev, port);

	/* Enable DSI PLL */
	val = I915_READ(BXT_DSI_PLL_ENABLE);
	val |= BXT_DSI_PLL_DO_ENABLE;
	I915_WRITE(BXT_DSI_PLL_ENABLE, val);

	/* Timeout and fail if PLL not locked */
	if (wait_for(I915_READ(BXT_DSI_PLL_ENABLE) & BXT_DSI_PLL_LOCKED, 1)) {
		DRM_ERROR("Timed out waiting for DSI PLL to lock\n");
		return;
	}

	DRM_DEBUG_KMS("DSI PLL locked\n");
}

void intel_enable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;

	if (IS_VALLEYVIEW(dev))
		vlv_enable_dsi_pll(encoder);
	else if (IS_BROXTON(dev))
		bxt_enable_dsi_pll(encoder);
}

void intel_disable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_device *dev = encoder->base.dev;

	if (IS_VALLEYVIEW(dev))
		vlv_disable_dsi_pll(encoder);
	else if (IS_BROXTON(dev))
		bxt_disable_dsi_pll(encoder);
}

static void bxt_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
{
	u32 tmp;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Clear old configurations */
	tmp = I915_READ(BXT_MIPI_CLOCK_CTL);
	tmp &= ~(BXT_MIPI_TX_ESCLK_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_RX_ESCLK_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_ESCLK_VAR_DIV_MASK(port));
	tmp &= ~(BXT_MIPI_DPHY_DIVIDER_MASK(port));
	I915_WRITE(BXT_MIPI_CLOCK_CTL, tmp);
	I915_WRITE(MIPI_EOT_DISABLE(port), CLOCKSTOP);
}

void intel_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
{
	struct drm_device *dev = encoder->base.dev;

	if (IS_BROXTON(dev))
		bxt_dsi_reset_clocks(encoder, port);
	else if (IS_VALLEYVIEW(dev))
		vlv_dsi_reset_clocks(encoder, port);
}
