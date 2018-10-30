/*
 * Copyright © 2018 Intel Corporation
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
 *   Madhav Chauhan <madhav.chauhan@intel.com>
 *   Jani Nikula <jani.nikula@intel.com>
 */

#include "intel_dsi.h"

static void gen11_dsi_program_esc_clk_div(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	u32 afe_clk_khz; /* 8X Clock */
	u32 esc_clk_div_m;

	afe_clk_khz = DIV_ROUND_CLOSEST(intel_dsi->pclk * bpp,
					intel_dsi->lane_count);

	esc_clk_div_m = DIV_ROUND_UP(afe_clk_khz, DSI_MAX_ESC_CLK);

	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(ICL_DSI_ESC_CLK_DIV(port),
			   esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		POSTING_READ(ICL_DSI_ESC_CLK_DIV(port));
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(ICL_DPHY_ESC_CLK_DIV(port),
			   esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		POSTING_READ(ICL_DPHY_ESC_CLK_DIV(port));
	}
}

static void gen11_dsi_enable_io_power(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_DSI_IO_MODECTL(port));
		tmp |= COMBO_PHY_MODE_DSI;
		I915_WRITE(ICL_DSI_IO_MODECTL(port), tmp);
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		intel_display_power_get(dev_priv, port == PORT_A ?
					POWER_DOMAIN_PORT_DDI_A_IO :
					POWER_DOMAIN_PORT_DDI_B_IO);
	}
}

static void gen11_dsi_power_up_lanes(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;
	u32 lane_mask;

	switch (intel_dsi->lane_count) {
	case 1:
		lane_mask = PWR_DOWN_LN_3_1_0;
		break;
	case 2:
		lane_mask = PWR_DOWN_LN_3_1;
		break;
	case 3:
		lane_mask = PWR_DOWN_LN_3;
		break;
	case 4:
	default:
		lane_mask = PWR_UP_ALL_LANES;
		break;
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_CL_DW10(port));
		tmp &= ~PWR_DOWN_LN_MASK;
		I915_WRITE(ICL_PORT_CL_DW10(port), tmp | lane_mask);
	}
}

static void gen11_dsi_enable_port_and_phy(struct intel_encoder *encoder)
{
	/* step 4a: power up all lanes of the DDI used by DSI */
	gen11_dsi_power_up_lanes(encoder);
}

static void __attribute__((unused))
gen11_dsi_pre_enable(struct intel_encoder *encoder,
		     const struct intel_crtc_state *pipe_config,
		     const struct drm_connector_state *conn_state)
{
	/* step2: enable IO power */
	gen11_dsi_enable_io_power(encoder);

	/* step3: enable DSI PLL */
	gen11_dsi_program_esc_clk_div(encoder);

	/* step4: enable DSI port and DPHY */
	gen11_dsi_enable_port_and_phy(encoder);
}
