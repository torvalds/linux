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

#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"
#include "intel_sideband.h"

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
int vlv_dsi_pll_compute(struct intel_encoder *encoder,
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

void vlv_dsi_pll_enable(struct intel_encoder *encoder,
			const struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	DRM_DEBUG_KMS("\n");

	vlv_cck_get(dev_priv);

	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, 0);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_DIVIDER, config->dsi_pll.div);
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL,
		      config->dsi_pll.ctrl & ~DSI_PLL_VCO_EN);

	/* wait at least 0.5 us after ungating before enabling VCO,
	 * allow hrtimer subsystem optimization by relaxing timing
	 */
	usleep_range(10, 50);

	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, config->dsi_pll.ctrl);

	if (wait_for(vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL) &
						DSI_PLL_LOCK, 20)) {

		vlv_cck_put(dev_priv);
		DRM_ERROR("DSI PLL lock failed\n");
		return;
	}
	vlv_cck_put(dev_priv);

	DRM_DEBUG_KMS("DSI PLL locked\n");
}

void vlv_dsi_pll_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 tmp;

	DRM_DEBUG_KMS("\n");

	vlv_cck_get(dev_priv);

	tmp = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	tmp &= ~DSI_PLL_VCO_EN;
	tmp |= DSI_PLL_LDO_GATE;
	vlv_cck_write(dev_priv, CCK_REG_DSI_PLL_CONTROL, tmp);

	vlv_cck_put(dev_priv);
}

bool bxt_dsi_pll_is_enabled(struct drm_i915_private *dev_priv)
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
	 * Dividers must be programmed with valid values. As per BSEPC, for
	 * GEMINLAKE only PORT A divider values are checked while for BXT
	 * both divider values are validated. Check this here for
	 * paranoia, since BIOS is known to misconfigure PLLs in this way at
	 * times, and since accessing DSI registers with invalid dividers
	 * causes a system hang.
	 */
	val = I915_READ(BXT_DSI_PLL_CTL);
	if (IS_GEMINILAKE(dev_priv)) {
		if (!(val & BXT_DSIA_16X_MASK)) {
			DRM_DEBUG_DRIVER("Invalid PLL divider (%08x)\n", val);
			enabled = false;
		}
	} else {
		if (!(val & BXT_DSIA_16X_MASK) || !(val & BXT_DSIC_16X_MASK)) {
			DRM_DEBUG_DRIVER("Invalid PLL divider (%08x)\n", val);
			enabled = false;
		}
	}

	return enabled;
}

void bxt_dsi_pll_disable(struct intel_encoder *encoder)
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
	if (intel_wait_for_register(&dev_priv->uncore,
				    BXT_DSI_PLL_ENABLE,
				    BXT_DSI_PLL_LOCKED,
				    0,
				    1))
		DRM_ERROR("Timeout waiting for PLL lock deassertion\n");
}

u32 vlv_dsi_get_pclk(struct intel_encoder *encoder,
		     struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	u32 dsi_clock, pclk;
	u32 pll_ctl, pll_div;
	u32 m = 0, p = 0, n;
	int refclk = IS_CHERRYVIEW(dev_priv) ? 100000 : 25000;
	int i;

	DRM_DEBUG_KMS("\n");

	vlv_cck_get(dev_priv);
	pll_ctl = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_CONTROL);
	pll_div = vlv_cck_read(dev_priv, CCK_REG_DSI_PLL_DIVIDER);
	vlv_cck_put(dev_priv);

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

	pclk = DIV_ROUND_CLOSEST(dsi_clock * intel_dsi->lane_count, bpp);

	return pclk;
}

u32 bxt_dsi_get_pclk(struct intel_encoder *encoder,
		     struct intel_crtc_state *config)
{
	u32 pclk;
	u32 dsi_clk;
	u32 dsi_ratio;
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);

	config->dsi_pll.ctrl = I915_READ(BXT_DSI_PLL_CTL);

	dsi_ratio = config->dsi_pll.ctrl & BXT_DSI_PLL_RATIO_MASK;

	dsi_clk = (dsi_ratio * BXT_REF_CLOCK_KHZ) / 2;

	pclk = DIV_ROUND_CLOSEST(dsi_clk * intel_dsi->lane_count, bpp);

	DRM_DEBUG_DRIVER("Calculated pclk=%u\n", pclk);
	return pclk;
}

void vlv_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
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

static void glk_dsi_program_esc_clock(struct drm_device *dev,
				   const struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	u32 dsi_rate = 0;
	u32 pll_ratio = 0;
	u32 ddr_clk = 0;
	u32 div1_value = 0;
	u32 div2_value = 0;
	u32 txesc1_div = 0;
	u32 txesc2_div = 0;

	pll_ratio = config->dsi_pll.ctrl & BXT_DSI_PLL_RATIO_MASK;

	dsi_rate = (BXT_REF_CLOCK_KHZ * pll_ratio) / 2;

	ddr_clk = dsi_rate / 2;

	/* Variable divider value */
	div1_value = DIV_ROUND_CLOSEST(ddr_clk, 20000);

	/* Calculate TXESC1 divider */
	if (div1_value <= 10)
		txesc1_div = div1_value;
	else if ((div1_value > 10) && (div1_value <= 20))
		txesc1_div = DIV_ROUND_UP(div1_value, 2);
	else if ((div1_value > 20) && (div1_value <= 30))
		txesc1_div = DIV_ROUND_UP(div1_value, 4);
	else if ((div1_value > 30) && (div1_value <= 40))
		txesc1_div = DIV_ROUND_UP(div1_value, 6);
	else if ((div1_value > 40) && (div1_value <= 50))
		txesc1_div = DIV_ROUND_UP(div1_value, 8);
	else
		txesc1_div = 10;

	/* Calculate TXESC2 divider */
	div2_value = DIV_ROUND_UP(div1_value, txesc1_div);

	if (div2_value < 10)
		txesc2_div = div2_value;
	else
		txesc2_div = 10;

	I915_WRITE(MIPIO_TXESC_CLK_DIV1, txesc1_div & GLK_TX_ESC_CLK_DIV1_MASK);
	I915_WRITE(MIPIO_TXESC_CLK_DIV2, txesc2_div & GLK_TX_ESC_CLK_DIV2_MASK);
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

	mipi_8by3_divider = 0x2;

	tmp |= BXT_MIPI_8X_BY3_DIVIDER(port, mipi_8by3_divider);
	tmp |= BXT_MIPI_TX_ESCLK_DIVIDER(port, tx_div);
	tmp |= BXT_MIPI_RX_ESCLK_LOWER_DIVIDER(port, rx_div_lower);
	tmp |= BXT_MIPI_RX_ESCLK_UPPER_DIVIDER(port, rx_div_upper);

	I915_WRITE(BXT_MIPI_CLOCK_CTL, tmp);
}

int bxt_dsi_pll_compute(struct intel_encoder *encoder,
			struct intel_crtc_state *config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u8 dsi_ratio, dsi_ratio_min, dsi_ratio_max;
	u32 dsi_clk;

	dsi_clk = dsi_clk_from_pclk(intel_dsi->pclk, intel_dsi->pixel_format,
				    intel_dsi->lane_count);

	/*
	 * From clock diagram, to get PLL ratio divider, divide double of DSI
	 * link rate (i.e., 2*8x=16x frequency value) by ref clock. Make sure to
	 * round 'up' the result
	 */
	dsi_ratio = DIV_ROUND_UP(dsi_clk * 2, BXT_REF_CLOCK_KHZ);

	if (IS_BROXTON(dev_priv)) {
		dsi_ratio_min = BXT_DSI_PLL_RATIO_MIN;
		dsi_ratio_max = BXT_DSI_PLL_RATIO_MAX;
	} else {
		dsi_ratio_min = GLK_DSI_PLL_RATIO_MIN;
		dsi_ratio_max = GLK_DSI_PLL_RATIO_MAX;
	}

	if (dsi_ratio < dsi_ratio_min || dsi_ratio > dsi_ratio_max) {
		DRM_ERROR("Cant get a suitable ratio from DSI PLL ratios\n");
		return -ECHRNG;
	} else
		DRM_DEBUG_KMS("DSI PLL calculation is Done!!\n");

	/*
	 * Program DSI ratio and Select MIPIC and MIPIA PLL output as 8x
	 * Spec says both have to be programmed, even if one is not getting
	 * used. Configure MIPI_CLOCK_CTL dividers in modeset
	 */
	config->dsi_pll.ctrl = dsi_ratio | BXT_DSIA_16X_BY2 | BXT_DSIC_16X_BY2;

	/* As per recommendation from hardware team,
	 * Prog PVD ratio =1 if dsi ratio <= 50
	 */
	if (IS_BROXTON(dev_priv) && dsi_ratio <= 50)
		config->dsi_pll.ctrl |= BXT_DSI_PLL_PVD_RATIO_1;

	return 0;
}

void bxt_dsi_pll_enable(struct intel_encoder *encoder,
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
	if (IS_BROXTON(dev_priv)) {
		for_each_dsi_port(port, intel_dsi->ports)
			bxt_dsi_program_clocks(encoder->base.dev, port, config);
	} else {
		glk_dsi_program_esc_clock(encoder->base.dev, config);
	}

	/* Enable DSI PLL */
	val = I915_READ(BXT_DSI_PLL_ENABLE);
	val |= BXT_DSI_PLL_DO_ENABLE;
	I915_WRITE(BXT_DSI_PLL_ENABLE, val);

	/* Timeout and fail if PLL not locked */
	if (intel_wait_for_register(&dev_priv->uncore,
				    BXT_DSI_PLL_ENABLE,
				    BXT_DSI_PLL_LOCKED,
				    BXT_DSI_PLL_LOCKED,
				    1)) {
		DRM_ERROR("Timed out waiting for DSI PLL to lock\n");
		return;
	}

	DRM_DEBUG_KMS("DSI PLL locked\n");
}

void bxt_dsi_reset_clocks(struct intel_encoder *encoder, enum port port)
{
	u32 tmp;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);

	/* Clear old configurations */
	if (IS_BROXTON(dev_priv)) {
		tmp = I915_READ(BXT_MIPI_CLOCK_CTL);
		tmp &= ~(BXT_MIPI_TX_ESCLK_FIXDIV_MASK(port));
		tmp &= ~(BXT_MIPI_RX_ESCLK_UPPER_FIXDIV_MASK(port));
		tmp &= ~(BXT_MIPI_8X_BY3_DIVIDER_MASK(port));
		tmp &= ~(BXT_MIPI_RX_ESCLK_LOWER_FIXDIV_MASK(port));
		I915_WRITE(BXT_MIPI_CLOCK_CTL, tmp);
	} else {
		tmp = I915_READ(MIPIO_TXESC_CLK_DIV1);
		tmp &= ~GLK_TX_ESC_CLK_DIV1_MASK;
		I915_WRITE(MIPIO_TXESC_CLK_DIV1, tmp);

		tmp = I915_READ(MIPIO_TXESC_CLK_DIV2);
		tmp &= ~GLK_TX_ESC_CLK_DIV2_MASK;
		I915_WRITE(MIPIO_TXESC_CLK_DIV2, tmp);
	}
	I915_WRITE(MIPI_EOT_DISABLE(port), CLOCKSTOP);
}
