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

static const u16 lfsr_converts[] = {
	426, 469, 234, 373, 442, 221, 110, 311, 411,		/* 62 - 70 */
	461, 486, 243, 377, 188, 350, 175, 343, 427, 213,	/* 71 - 80 */
	106, 53, 282, 397, 454, 227, 113, 56, 284, 142,		/* 81 - 90 */
	71, 35, 273, 136, 324, 418, 465, 488, 500, 506		/* 91 - 100 */
};

/* Get DSI clock from pixel clock */
static u32 dsi_clk_from_pclk(u32 pclk, enum mipi_dsi_pixel_format fmt,
			     int lane_count)
{
	u32 dsi_clk_khz;
	u32 bpp = mipi_dsi_pixel_format_to_bpp(fmt);

	/* DSI data rate = pixel clock * bits per pixel / lane count
	   pixel clock is converted from KHz to Hz */
	dsi_clk_khz = DIV_ROUND_CLOSEST(pclk * bpp, lane_count);

	return dsi_clk_khz;
}

static int dsi_calc_mnp(struct drm_i915_private *dev_priv,
			struct intel_crtc_state *config,
			int target_dsi_clk)
{
	unsigned int m_min, m_max, p_min = 2, p_max = 6;
	unsigned int m, n, p;
	unsigned int calc_m, calc_p;
	int delta, ref_clk;

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

	calc_p = p_min;
	calc_m = m_min;
	delta = abs(target_dsi_clk - (m_min * ref_clk) / (p_min * n));

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
	config->dsi_pll.ctrl = 1 << (DSI_PLL_P1_POST_DIV_SHIFT + calc_p - 2);
	config->dsi_pll.div =
		(ffs(n) - 1) << DSI_PLL_N1_DIV_SHIFT |
		(u32)lfsr_converts[calc_m - 62] << DSI_PLL_M1_DIV_SHIFT;

	return 0;
}

/*
 * XXX: The muxing and gating is hard coded for now. Need to add support for
 * sharing PLLs with two DSI outputs.
 */
static int vlv_compute_dsi_pll(struct intel_encoder *encoder,
			       struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	int ret;
	u32 dsi_clk;

	dsi_clk = dsi_clk_from_pclk(intel_dsi->pclk, intel_dsi->pixel_format,
				    intel_dsi->lane_count);

	ret = dsi_calc_mnp(dev_priv, config, dsi_clk);
	if (ret) {
		DRM_DEBUG_KMS("dsi_calc_mnp failed\n");
		return ret;
	}

	if (intel_dsi->ports & (1 << PORT_A))
		config->dsi_pll.ctrl |= DSI_PLL_CLK_GATE_DSI0_DSIPLL;

	if (intel_dsi->ports & (1 << PORT_C))
		config->dsi_pll.ctrl |= DSI_PLL_CLK_GATE_DSI1_DSIPLL;

	config->dsi_pll.ctrl |= DSI_PLL_VCO_EN;

	DRM_DEBUG_KMS("dsi pll div %08x, ctrl %08x\n",
		      config->dsi_pll.div, config->dsi_pll.ctrl);

	return 0;
}

static void vlv_enable_dsi_pll(struct intel_encoder *encoder,
			       const struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->sb_lock);

	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, 0);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_DIVIDER, config->dsi_pll.div);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL,
		      config->dsi_pll.ctrl & ~DSI_PLL_VCO_EN);

	/* wait at least 0.5 us after ungating before enabling VCO */
	usleep_range(1, 10);

	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, config->dsi_pll.ctrl);

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
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->sb_lock);

	tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	tmp &= ~DSI_PLL_VCO_EN;
	tmp |= DSI_PLL_LDO_GATE;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	mutex_unlock(&dev_priv->sb_lock);
}

static bool bxt_dsi_pll_is_enabled(struct drm_i915_private *dev_priv)
{
	bool enabled;
	u32 val;
	u32 mask;

	mask = BXT_DSI_PLL_DO_ENABLE | BXT_DSI_PLL_LOCKED;
	val = I915_READ(BXT_DSI_PLL_ENABLE);
	enabled = (val & mask) == mask;

	if (!enabled)
		return false;

	/*
	 * Both dividers must be programmed with valid values even if only one
	 * of the PLL is used, see BSpec/Broxton Clocks. Check this here for
	 * paranoia, since BIOS is known to misconfigure PLLs in this way at
	 * times, and since accessing DSI registers with invalid dividers
	 * causes a system hang.
	 */
	val = I915_READ(BXT_DSI_PLL_CTL);
	if (!(val & BXT_DSIA_16X_MASK) || !(val & BXT_DSIC_16X_MASK)) {
		DRM_DEBUG_DRIVER("PLL is enabled with invalid divider settings (%08x)\n",
				 val);
		enabled = false;
	}

	return enabled;
}

static void bxt_disable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 val;

	DRM_DEBUG_KMS("\n");

	val = I915_READ(BXT_DSI_PLL_ENABLE);
	val &= ~BXT_DSI_PLL_DO_ENABLE;
	I915_WRITE(BXT_DSI_PLL_ENABLE, val);

	/*
	 * PLL lock should deassert within 200us.
	 * Wait up to 1ms before timing out.
	 */
	if (intel_wait_for_register(dev_priv,
				    BXT_DSI_PLL_ENABLE,
				    BXT_DSI_PLL_LOCKED,
				    0,
				    1))
		DRM_ERROR("Timeout waiting for PLL lock deassertion\n");
}

static void assert_bpp_mismatch(enum mipi_dsi_pixel_format fmt, int pipe_bpp)
{
	int bpp = mipi_dsi_pixel_format_to_bpp(fmt);

	WARN(bpp != pipe_bpp,
	     "bpp match assertion failure (expected %d, current %d)\n",
	     bpp, pipe_bpp);
}

static u32 vlv_dsi_get_pclk(struct intel_encoder *encoder, int pipe_bpp,
			    struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 dsi_clock, pclk;
	u32 pll_ctl, pll_div;
	u32 m = 0, p = 0, n;
	int refclk = IS_CHERRYVIEW(dev_priv) ? 100000 : 25000;
	int i;

	DRM_DEBUG_KMS("\n");

	mutex_lock(&dev_priv->sb_lock);
	pll_ctl = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	pll_div = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_DIVIDER);
	mutex_unlock(&dev_priv->sb_lock);

	config->dsi_pll.ctrl = pll_ctl & ~DSI_PLL_LOCK;
	config->dsi_pll.div = pll_div;

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

static u32 bxt_dsi_get_pclk(struct intel_encoder *encoder, int pipe_bpp,
			    struct intel_crtc_state *config)
{
	u32 pclk;
	u32 dsi_clk;
	u32 dsi_ratio;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	/* Divide by zero */
	if (!pipe_bpp) {
		DRM_ERROR("Invalid BPP(0)\n");
		return 0;
	}

	config->dsi_pll.ctrl = I915_READ(BXT_DSI_PLL_CTL);

	dsi_ratio = config->dsi_pll.ctrl & BXT_DSI_PLL_RATIO_MASK;

	dsi_clk = (dsi_ratio * BXT_REF_CLOCK_KHZ) / 2;

	/* pixel_format and pipe_bpp should agree */
	assert_bpp_mismatch(intel_dsi->pixel_format, pipe_bpp);

	pclk = DIV_ROUND_CLOSEST(dsi_clk * intel_dsi->lane_count, pipe_bpp);

	DRM_DEBUG_DRIVER("Calculated pclk=%u\n", pclk);
	return pclk;
}

u32 intel_dsi_get_pclk(struct intel_encoder *encoder, int pipe_bpp,
		       struct intel_crtc_state *config)
{
	if (IS_BROXTON(to_i915(encoder->base.dev)))
		return bxt_dsi_get_pclk(encoder, pipe_bpp, config);
	else
		return vlv_dsi_get_pclk(encoder, pipe_bpp, config);
}

static void vlv_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
{
	u32 temp;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);

	temp = I915_READ(MIPI_CTRL(port));
	temp &= ~ESCAPE_CLOCK_DIVIDER_MASK;
	I915_WRITE(MIPI_CTRL(port), temp |
			intel_dsi->escape_clk_div <<
			ESCAPE_CLOCK_DIVIDER_SHIFT);
}

/* Program BXT Mipi clocks and dividers */
static void bxt_dsi_program_clocks(struct drm_device *dev, enum port port,
				   const struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 tmp;
	u32 dsi_rate = 0;
	u32 pll_ratio = 0;
	u32 rx_div;
	u32 tx_div;
	u32 rx_div_upper;
	u32 rx_div_lower;
	u32 mipi_8by3_divider;

	/* Clear old configurations */
	tmp = I915_READ(BXT_MIPI_CLOCK_CTL);
	tmp &= ~(BXT_MIPI_TX_ESCLK_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_RX_ESCLK_UPPER_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_8X_BY3_DIVIDER_MASK(port));
	tmp &= ~(BXT_MIPI_RX_ESCLK_LOWER_FIXDIV_MASK(port));

	/* Get the current DSI rate(actual) */
	pll_ratio = config->dsi_pll.ctrl & BXT_DSI_PLL_RATIO_MASK;
	dsi_rate = (BXT_REF_CLOCK_KHZ * pll_ratio) / 2;

	/*
	 * tx clock should be <= 20MHz and the div value must be
	 * subtracted by 1 as per bspec
	 */
	tx_div = DIV_ROUND_UP(dsi_rate, 20000) - 1;
	/*
	 * rx clock should be <= 150MHz and the div value must be
	 * subtracted by 1 as per bspec
	 */
	rx_div = DIV_ROUND_UP(dsi_rate, 150000) - 1;

	/*
	 * rx divider value needs to be updated in the
	 * two differnt bit fields in the register hence splitting the
	 * rx divider value accordingly
	 */
	rx_div_lower = rx_div & RX_DIVIDER_BIT_1_2;
	rx_div_upper = (rx_div & RX_DIVIDER_BIT_3_4) >> 2;

	/* As per bpsec program the 8/3X clock divider to the below value */
	if (dev_priv->vbt.dsi.config->is_cmd_mode)
		mipi_8by3_divider = 0x2;
	else
		mipi_8by3_divider = 0x3;

	tmp |= BXT_MIPI_8X_BY3_DIVIDER(port, mipi_8by3_divider);
	tmp |= BXT_MIPI_TX_ESCLK_DIVIDER(port, tx_div);
	tmp |= BXT_MIPI_RX_ESCLK_LOWER_DIVIDER(port, rx_div_lower);
	tmp |= BXT_MIPI_RX_ESCLK_UPPER_DIVIDER(port, rx_div_upper);

	I915_WRITE(BXT_MIPI_CLOCK_CTL, tmp);
}

static int bxt_compute_dsi_pll(struct intel_encoder *encoder,
			       struct intel_crtc_state *config)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u8 dsi_ratio;
	u32 dsi_clk;

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
		return -ECHRNG;
	}

	/*
	 * Program DSI ratio and Select MIPIC and MIPIA PLL output as 8x
	 * Spec says both have to be programmed, even if one is not getting
	 * used. Configure MIPI_CLOCK_CTL dividers in modeset
	 */
	config->dsi_pll.ctrl = dsi_ratio | BXT_DSIA_16X_BY2 | BXT_DSIC_16X_BY2;

	/* As per recommendation from hardware team,
	 * Prog PVD ratio =1 if dsi ratio <= 50
	 */
	if (dsi_ratio <= 50)
		config->dsi_pll.ctrl |= BXT_DSI_PLL_PVD_RATIO_1;

	return 0;
}

static void bxt_enable_dsi_pll(struct intel_encoder *encoder,
			       const struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 val;

	DRM_DEBUG_KMS("\n");

	/* Configure PLL vales */
	I915_WRITE(BXT_DSI_PLL_CTL, config->dsi_pll.ctrl);
	POSTING_READ(BXT_DSI_PLL_CTL);

	/* Program TX, RX, Dphy clocks */
	for_each_dsi_port(port, intel_dsi->ports)
		bxt_dsi_program_clocks(encoder->base.dev, port, config);

	/* Enable DSI PLL */
	val = I915_READ(BXT_DSI_PLL_ENABLE);
	val |= BXT_DSI_PLL_DO_ENABLE;
	I915_WRITE(BXT_DSI_PLL_ENABLE, val);

	/* Timeout and fail if PLL not locked */
	if (intel_wait_for_register(dev_priv,
				    BXT_DSI_PLL_ENABLE,
				    BXT_DSI_PLL_LOCKED,
				    BXT_DSI_PLL_LOCKED,
				    1)) {
		DRM_ERROR("Timed out waiting for DSI PLL to lock\n");
		return;
	}

	DRM_DEBUG_KMS("DSI PLL locked\n");
}

bool intel_dsi_pll_is_enabled(struct drm_i915_private *dev_priv)
{
	if (IS_BROXTON(dev_priv))
		return bxt_dsi_pll_is_enabled(dev_priv);

	MISSING_CASE(INTEL_DEVID(dev_priv));

	return false;
}

int intel_compute_dsi_pll(struct intel_encoder *encoder,
			  struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return vlv_compute_dsi_pll(encoder, config);
	else if (IS_BROXTON(dev_priv))
		return bxt_compute_dsi_pll(encoder, config);

	return -ENODEV;
}

void intel_enable_dsi_pll(struct intel_encoder *encoder,
			  const struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		vlv_enable_dsi_pll(encoder, config);
	else if (IS_BROXTON(dev_priv))
		bxt_enable_dsi_pll(encoder, config);
}

void intel_disable_dsi_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		vlv_disable_dsi_pll(encoder);
	else if (IS_BROXTON(dev_priv))
		bxt_disable_dsi_pll(encoder);
}

static void bxt_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
{
	u32 tmp;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	/* Clear old configurations */
	tmp = I915_READ(BXT_MIPI_CLOCK_CTL);
	tmp &= ~(BXT_MIPI_TX_ESCLK_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_RX_ESCLK_UPPER_FIXDIV_MASK(port));
	tmp &= ~(BXT_MIPI_8X_BY3_DIVIDER_MASK(port));
	tmp &= ~(BXT_MIPI_RX_ESCLK_LOWER_FIXDIV_MASK(port));
	I915_WRITE(BXT_MIPI_CLOCK_CTL, tmp);
	I915_WRITE(MIPI_EOT_DISABLE(port), CLOCKSTOP);
}

void intel_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (IS_BROXTON(dev_priv))
		bxt_dsi_reset_clocks(encoder, port);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		vlv_dsi_reset_clocks(encoder, port);
}
