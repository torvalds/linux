/*
 * Copyright © 2012 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

#include <drm/drm_privacy_screen_consumer.h>
#include <drm/drm_scdc_helper.h>

#include "i915_drv.h"
#include "intel_audio.h"
#include "intel_backlight.h"
#include "intel_combo_phy.h"
#include "intel_combo_phy_regs.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_ddi_buf_trans.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dpio_phy.h"
#include "intel_drrs.h"
#include "intel_dsi.h"
#include "intel_fdi.h"
#include "intel_fifo_underrun.h"
#include "intel_gmbus.h"
#include "intel_hdcp.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_lspcon.h"
#include "intel_pps.h"
#include "intel_psr.h"
#include "intel_snps_phy.h"
#include "intel_sprite.h"
#include "intel_tc.h"
#include "intel_tc_phy_regs.h"
#include "intel_vdsc.h"
#include "intel_vrr.h"
#include "skl_scaler.h"
#include "skl_universal_plane.h"

static const u8 index_to_dp_signal_levels[] = {
	[0] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[1] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[2] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[3] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_3,
	[4] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[5] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[6] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[7] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[8] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[9] = DP_TRAIN_VOLTAGE_SWING_LEVEL_3 | DP_TRAIN_PRE_EMPH_LEVEL_0,
};

static int intel_ddi_hdmi_level(struct intel_encoder *encoder,
				const struct intel_ddi_buf_trans *trans)
{
	int level;

	level = intel_bios_hdmi_level_shift(encoder);
	if (level < 0)
		level = trans->hdmi_default_entry;

	return level;
}

static bool has_buf_trans_select(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) < 10 && !IS_BROXTON(i915);
}

static bool has_iboost(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) == 9 && !IS_BROXTON(i915);
}

/*
 * Starting with Haswell, DDI port buffers must be programmed with correct
 * values in advance. This function programs the correct values for
 * DP/eDP/FDI use cases.
 */
void hsw_prepare_dp_ddi_buffers(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 iboost_bit = 0;
	int i, n_entries;
	enum port port = encoder->port;
	const struct intel_ddi_buf_trans *trans;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&dev_priv->drm, !trans))
		return;

	/* If we're boosting the current, set bit 31 of trans1 */
	if (has_iboost(dev_priv) &&
	    intel_bios_encoder_dp_boost_level(encoder->devdata))
		iboost_bit = DDI_BUF_BALANCE_LEG_ENABLE;

	for (i = 0; i < n_entries; i++) {
		intel_de_write(dev_priv, DDI_BUF_TRANS_LO(port, i),
			       trans->entries[i].hsw.trans1 | iboost_bit);
		intel_de_write(dev_priv, DDI_BUF_TRANS_HI(port, i),
			       trans->entries[i].hsw.trans2);
	}
}

/*
 * Starting with Haswell, DDI port buffers must be programmed with correct
 * values in advance. This function programs the correct values for
 * HDMI/DVI use cases.
 */
static void hsw_prepare_hdmi_ddi_buffers(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int level = intel_ddi_level(encoder, crtc_state, 0);
	u32 iboost_bit = 0;
	int n_entries;
	enum port port = encoder->port;
	const struct intel_ddi_buf_trans *trans;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&dev_priv->drm, !trans))
		return;

	/* If we're boosting the current, set bit 31 of trans1 */
	if (has_iboost(dev_priv) &&
	    intel_bios_encoder_hdmi_boost_level(encoder->devdata))
		iboost_bit = DDI_BUF_BALANCE_LEG_ENABLE;

	/* Entry 9 is for HDMI: */
	intel_de_write(dev_priv, DDI_BUF_TRANS_LO(port, 9),
		       trans->entries[level].hsw.trans1 | iboost_bit);
	intel_de_write(dev_priv, DDI_BUF_TRANS_HI(port, 9),
		       trans->entries[level].hsw.trans2);
}

void intel_wait_ddi_buf_idle(struct drm_i915_private *dev_priv,
			     enum port port)
{
	if (IS_BROXTON(dev_priv)) {
		udelay(16);
		return;
	}

	if (wait_for_us((intel_de_read(dev_priv, DDI_BUF_CTL(port)) &
			 DDI_BUF_IS_IDLE), 8))
		drm_err(&dev_priv->drm, "Timeout waiting for DDI BUF %c to get idle\n",
			port_name(port));
}

static void intel_wait_ddi_buf_active(struct drm_i915_private *dev_priv,
				      enum port port)
{
	int ret;

	/* Wait > 518 usecs for DDI_BUF_CTL to be non idle */
	if (DISPLAY_VER(dev_priv) < 10) {
		usleep_range(518, 1000);
		return;
	}

	ret = _wait_for(!(intel_de_read(dev_priv, DDI_BUF_CTL(port)) &
			  DDI_BUF_IS_IDLE), IS_DG2(dev_priv) ? 1200 : 500, 10, 10);

	if (ret)
		drm_err(&dev_priv->drm, "Timeout waiting for DDI BUF %c to get active\n",
			port_name(port));
}

static u32 hsw_pll_to_ddi_pll_sel(const struct intel_shared_dpll *pll)
{
	switch (pll->info->id) {
	case DPLL_ID_WRPLL1:
		return PORT_CLK_SEL_WRPLL1;
	case DPLL_ID_WRPLL2:
		return PORT_CLK_SEL_WRPLL2;
	case DPLL_ID_SPLL:
		return PORT_CLK_SEL_SPLL;
	case DPLL_ID_LCPLL_810:
		return PORT_CLK_SEL_LCPLL_810;
	case DPLL_ID_LCPLL_1350:
		return PORT_CLK_SEL_LCPLL_1350;
	case DPLL_ID_LCPLL_2700:
		return PORT_CLK_SEL_LCPLL_2700;
	default:
		MISSING_CASE(pll->info->id);
		return PORT_CLK_SEL_NONE;
	}
}

static u32 icl_pll_to_ddi_clk_sel(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	int clock = crtc_state->port_clock;
	const enum intel_dpll_id id = pll->info->id;

	switch (id) {
	default:
		/*
		 * DPLL_ID_ICL_DPLL0 and DPLL_ID_ICL_DPLL1 should not be used
		 * here, so do warn if this get passed in
		 */
		MISSING_CASE(id);
		return DDI_CLK_SEL_NONE;
	case DPLL_ID_ICL_TBTPLL:
		switch (clock) {
		case 162000:
			return DDI_CLK_SEL_TBT_162;
		case 270000:
			return DDI_CLK_SEL_TBT_270;
		case 540000:
			return DDI_CLK_SEL_TBT_540;
		case 810000:
			return DDI_CLK_SEL_TBT_810;
		default:
			MISSING_CASE(clock);
			return DDI_CLK_SEL_NONE;
		}
	case DPLL_ID_ICL_MGPLL1:
	case DPLL_ID_ICL_MGPLL2:
	case DPLL_ID_ICL_MGPLL3:
	case DPLL_ID_ICL_MGPLL4:
	case DPLL_ID_TGL_MGPLL5:
	case DPLL_ID_TGL_MGPLL6:
		return DDI_CLK_SEL_MG;
	}
}

static u32 ddi_buf_phy_link_rate(int port_clock)
{
	switch (port_clock) {
	case 162000:
		return DDI_BUF_PHY_LINK_RATE(0);
	case 216000:
		return DDI_BUF_PHY_LINK_RATE(4);
	case 243000:
		return DDI_BUF_PHY_LINK_RATE(5);
	case 270000:
		return DDI_BUF_PHY_LINK_RATE(1);
	case 324000:
		return DDI_BUF_PHY_LINK_RATE(6);
	case 432000:
		return DDI_BUF_PHY_LINK_RATE(7);
	case 540000:
		return DDI_BUF_PHY_LINK_RATE(2);
	case 810000:
		return DDI_BUF_PHY_LINK_RATE(3);
	default:
		MISSING_CASE(port_clock);
		return DDI_BUF_PHY_LINK_RATE(0);
	}
}

static void intel_ddi_init_dp_buf_reg(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	/* DDI_BUF_CTL_ENABLE will be set by intel_ddi_prepare_link_retrain() later */
	intel_dp->DP = dig_port->saved_port_bits |
		DDI_PORT_WIDTH(crtc_state->lane_count) |
		DDI_BUF_TRANS_SELECT(0);

	if (IS_ALDERLAKE_P(i915) && intel_phy_is_tc(i915, phy)) {
		intel_dp->DP |= ddi_buf_phy_link_rate(crtc_state->port_clock);
		if (!intel_tc_port_in_tbt_alt_mode(dig_port))
			intel_dp->DP |= DDI_BUF_CTL_TC_PHY_OWNERSHIP;
	}
}

static int icl_calc_tbt_pll_link(struct drm_i915_private *dev_priv,
				 enum port port)
{
	u32 val = intel_de_read(dev_priv, DDI_CLK_SEL(port)) & DDI_CLK_SEL_MASK;

	switch (val) {
	case DDI_CLK_SEL_NONE:
		return 0;
	case DDI_CLK_SEL_TBT_162:
		return 162000;
	case DDI_CLK_SEL_TBT_270:
		return 270000;
	case DDI_CLK_SEL_TBT_540:
		return 540000;
	case DDI_CLK_SEL_TBT_810:
		return 810000;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static void ddi_dotclock_get(struct intel_crtc_state *pipe_config)
{
	int dotclock;

	/* CRT dotclock is determined via other means */
	if (pipe_config->has_pch_encoder)
		return;

	if (intel_crtc_has_dp_encoder(pipe_config))
		dotclock = intel_dotclock_calculate(pipe_config->port_clock,
						    &pipe_config->dp_m_n);
	else if (pipe_config->has_hdmi_sink && pipe_config->pipe_bpp > 24)
		dotclock = pipe_config->port_clock * 24 / pipe_config->pipe_bpp;
	else
		dotclock = pipe_config->port_clock;

	if (pipe_config->output_format == INTEL_OUTPUT_FORMAT_YCBCR420 &&
	    !intel_crtc_has_dp_encoder(pipe_config))
		dotclock *= 2;

	if (pipe_config->pixel_multiplier)
		dotclock /= pipe_config->pixel_multiplier;

	pipe_config->hw.adjusted_mode.crtc_clock = dotclock;
}

void intel_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state,
			  const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 temp;

	if (!intel_crtc_has_dp_encoder(crtc_state))
		return;

	drm_WARN_ON(&dev_priv->drm, transcoder_is_dsi(cpu_transcoder));

	temp = DP_MSA_MISC_SYNC_CLOCK;

	switch (crtc_state->pipe_bpp) {
	case 18:
		temp |= DP_MSA_MISC_6_BPC;
		break;
	case 24:
		temp |= DP_MSA_MISC_8_BPC;
		break;
	case 30:
		temp |= DP_MSA_MISC_10_BPC;
		break;
	case 36:
		temp |= DP_MSA_MISC_12_BPC;
		break;
	default:
		MISSING_CASE(crtc_state->pipe_bpp);
		break;
	}

	/* nonsense combination */
	drm_WARN_ON(&dev_priv->drm, crtc_state->limited_color_range &&
		    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB);

	if (crtc_state->limited_color_range)
		temp |= DP_MSA_MISC_COLOR_CEA_RGB;

	/*
	 * As per DP 1.2 spec section 2.3.4.3 while sending
	 * YCBCR 444 signals we should program MSA MISC1/0 fields with
	 * colorspace information.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		temp |= DP_MSA_MISC_COLOR_YCBCR_444_BT709;

	/*
	 * As per DP 1.4a spec section 2.2.4.3 [MSA Field for Indication
	 * of Color Encoding Format and Content Color Gamut] while sending
	 * YCBCR 420, HDR BT.2020 signals we should program MSA MISC1 fields
	 * which indicate VSC SDP for the Pixel Encoding/Colorimetry Format.
	 */
	if (intel_dp_needs_vsc_sdp(crtc_state, conn_state))
		temp |= DP_MSA_MISC_COLOR_VSC_SDP;

	intel_de_write(dev_priv, TRANS_MSA_MISC(cpu_transcoder), temp);
}

static u32 bdw_trans_port_sync_master_select(enum transcoder master_transcoder)
{
	if (master_transcoder == TRANSCODER_EDP)
		return 0;
	else
		return master_transcoder + 1;
}

static void
intel_ddi_config_transcoder_dp2(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val = 0;

	if (intel_dp_is_uhbr(crtc_state))
		val = TRANS_DP2_128B132B_CHANNEL_CODING;

	intel_de_write(i915, TRANS_DP2_CTL(cpu_transcoder), val);
}

/*
 * Returns the TRANS_DDI_FUNC_CTL value based on CRTC state.
 *
 * Only intended to be used by intel_ddi_enable_transcoder_func() and
 * intel_ddi_config_transcoder_func().
 */
static u32
intel_ddi_transcoder_func_reg_val_get(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum port port = encoder->port;
	u32 temp;

	/* Enable TRANS_DDI_FUNC_CTL for the pipe to work in HDMI mode */
	temp = TRANS_DDI_FUNC_ENABLE;
	if (DISPLAY_VER(dev_priv) >= 12)
		temp |= TGL_TRANS_DDI_SELECT_PORT(port);
	else
		temp |= TRANS_DDI_SELECT_PORT(port);

	switch (crtc_state->pipe_bpp) {
	case 18:
		temp |= TRANS_DDI_BPC_6;
		break;
	case 24:
		temp |= TRANS_DDI_BPC_8;
		break;
	case 30:
		temp |= TRANS_DDI_BPC_10;
		break;
	case 36:
		temp |= TRANS_DDI_BPC_12;
		break;
	default:
		BUG();
	}

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PVSYNC)
		temp |= TRANS_DDI_PVSYNC;
	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PHSYNC)
		temp |= TRANS_DDI_PHSYNC;

	if (cpu_transcoder == TRANSCODER_EDP) {
		switch (pipe) {
		case PIPE_A:
			/* On Haswell, can only use the always-on power well for
			 * eDP when not using the panel fitter, and when not
			 * using motion blur mitigation (which we don't
			 * support). */
			if (crtc_state->pch_pfit.force_thru)
				temp |= TRANS_DDI_EDP_INPUT_A_ONOFF;
			else
				temp |= TRANS_DDI_EDP_INPUT_A_ON;
			break;
		case PIPE_B:
			temp |= TRANS_DDI_EDP_INPUT_B_ONOFF;
			break;
		case PIPE_C:
			temp |= TRANS_DDI_EDP_INPUT_C_ONOFF;
			break;
		default:
			BUG();
			break;
		}
	}

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		if (crtc_state->has_hdmi_sink)
			temp |= TRANS_DDI_MODE_SELECT_HDMI;
		else
			temp |= TRANS_DDI_MODE_SELECT_DVI;

		if (crtc_state->hdmi_scrambling)
			temp |= TRANS_DDI_HDMI_SCRAMBLING;
		if (crtc_state->hdmi_high_tmds_clock_ratio)
			temp |= TRANS_DDI_HIGH_TMDS_CHAR_RATE;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG)) {
		temp |= TRANS_DDI_MODE_SELECT_FDI_OR_128B132B;
		temp |= (crtc_state->fdi_lanes - 1) << 1;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)) {
		if (intel_dp_is_uhbr(crtc_state))
			temp |= TRANS_DDI_MODE_SELECT_FDI_OR_128B132B;
		else
			temp |= TRANS_DDI_MODE_SELECT_DP_MST;
		temp |= DDI_PORT_WIDTH(crtc_state->lane_count);

		if (DISPLAY_VER(dev_priv) >= 12) {
			enum transcoder master;

			master = crtc_state->mst_master_transcoder;
			drm_WARN_ON(&dev_priv->drm,
				    master == INVALID_TRANSCODER);
			temp |= TRANS_DDI_MST_TRANSPORT_SELECT(master);
		}
	} else {
		temp |= TRANS_DDI_MODE_SELECT_DP_SST;
		temp |= DDI_PORT_WIDTH(crtc_state->lane_count);
	}

	if (IS_DISPLAY_VER(dev_priv, 8, 10) &&
	    crtc_state->master_transcoder != INVALID_TRANSCODER) {
		u8 master_select =
			bdw_trans_port_sync_master_select(crtc_state->master_transcoder);

		temp |= TRANS_DDI_PORT_SYNC_ENABLE |
			TRANS_DDI_PORT_SYNC_MASTER_SELECT(master_select);
	}

	return temp;
}

void intel_ddi_enable_transcoder_func(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (DISPLAY_VER(dev_priv) >= 11) {
		enum transcoder master_transcoder = crtc_state->master_transcoder;
		u32 ctl2 = 0;

		if (master_transcoder != INVALID_TRANSCODER) {
			u8 master_select =
				bdw_trans_port_sync_master_select(master_transcoder);

			ctl2 |= PORT_SYNC_MODE_ENABLE |
				PORT_SYNC_MODE_MASTER_SELECT(master_select);
		}

		intel_de_write(dev_priv,
			       TRANS_DDI_FUNC_CTL2(cpu_transcoder), ctl2);
	}

	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder),
		       intel_ddi_transcoder_func_reg_val_get(encoder,
							     crtc_state));
}

/*
 * Same as intel_ddi_enable_transcoder_func(), but it does not set the enable
 * bit.
 */
static void
intel_ddi_config_transcoder_func(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 ctl;

	ctl = intel_ddi_transcoder_func_reg_val_get(encoder, crtc_state);
	ctl &= ~TRANS_DDI_FUNC_ENABLE;
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), ctl);
}

void intel_ddi_disable_transcoder_func(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 ctl;

	if (DISPLAY_VER(dev_priv) >= 11)
		intel_de_write(dev_priv,
			       TRANS_DDI_FUNC_CTL2(cpu_transcoder), 0);

	ctl = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));

	drm_WARN_ON(crtc->base.dev, ctl & TRANS_DDI_HDCP_SIGNALLING);

	ctl &= ~TRANS_DDI_FUNC_ENABLE;

	if (IS_DISPLAY_VER(dev_priv, 8, 10))
		ctl &= ~(TRANS_DDI_PORT_SYNC_ENABLE |
			 TRANS_DDI_PORT_SYNC_MASTER_SELECT_MASK);

	if (DISPLAY_VER(dev_priv) >= 12) {
		if (!intel_dp_mst_is_master_trans(crtc_state)) {
			ctl &= ~(TGL_TRANS_DDI_PORT_MASK |
				 TRANS_DDI_MODE_SELECT_MASK);
		}
	} else {
		ctl &= ~(TRANS_DDI_PORT_MASK | TRANS_DDI_MODE_SELECT_MASK);
	}

	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), ctl);

	if (dev_priv->quirks & QUIRK_INCREASE_DDI_DISABLED_TIME &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Quirk Increase DDI disabled time\n");
		/* Quirk time at 100ms for reliable operation */
		msleep(100);
	}
}

int intel_ddi_toggle_hdcp_bits(struct intel_encoder *intel_encoder,
			       enum transcoder cpu_transcoder,
			       bool enable, u32 hdcp_mask)
{
	struct drm_device *dev = intel_encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	intel_wakeref_t wakeref;
	int ret = 0;
	u32 tmp;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     intel_encoder->power_domain);
	if (drm_WARN_ON(dev, !wakeref))
		return -ENXIO;

	tmp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (enable)
		tmp |= hdcp_mask;
	else
		tmp &= ~hdcp_mask;
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), tmp);
	intel_display_power_put(dev_priv, intel_encoder->power_domain, wakeref);
	return ret;
}

bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector)
{
	struct drm_device *dev = intel_connector->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_encoder *encoder = intel_attached_encoder(intel_connector);
	int type = intel_connector->base.connector_type;
	enum port port = encoder->port;
	enum transcoder cpu_transcoder;
	intel_wakeref_t wakeref;
	enum pipe pipe = 0;
	u32 tmp;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	if (!encoder->get_hw_state(encoder, &pipe)) {
		ret = false;
		goto out;
	}

	if (HAS_TRANSCODER(dev_priv, TRANSCODER_EDP) && port == PORT_A)
		cpu_transcoder = TRANSCODER_EDP;
	else
		cpu_transcoder = (enum transcoder) pipe;

	tmp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));

	switch (tmp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
	case TRANS_DDI_MODE_SELECT_DVI:
		ret = type == DRM_MODE_CONNECTOR_HDMIA;
		break;

	case TRANS_DDI_MODE_SELECT_DP_SST:
		ret = type == DRM_MODE_CONNECTOR_eDP ||
		      type == DRM_MODE_CONNECTOR_DisplayPort;
		break;

	case TRANS_DDI_MODE_SELECT_DP_MST:
		/* if the transcoder is in MST state then
		 * connector isn't connected */
		ret = false;
		break;

	case TRANS_DDI_MODE_SELECT_FDI_OR_128B132B:
		if (HAS_DP20(dev_priv))
			/* 128b/132b */
			ret = false;
		else
			/* FDI */
			ret = type == DRM_MODE_CONNECTOR_VGA;
		break;

	default:
		ret = false;
		break;
	}

out:
	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

static void intel_ddi_get_encoder_pipes(struct intel_encoder *encoder,
					u8 *pipe_mask, bool *is_dp_mst)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = encoder->port;
	intel_wakeref_t wakeref;
	enum pipe p;
	u32 tmp;
	u8 mst_pipe_mask;

	*pipe_mask = 0;
	*is_dp_mst = false;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return;

	tmp = intel_de_read(dev_priv, DDI_BUF_CTL(port));
	if (!(tmp & DDI_BUF_CTL_ENABLE))
		goto out;

	if (HAS_TRANSCODER(dev_priv, TRANSCODER_EDP) && port == PORT_A) {
		tmp = intel_de_read(dev_priv,
				    TRANS_DDI_FUNC_CTL(TRANSCODER_EDP));

		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		default:
			MISSING_CASE(tmp & TRANS_DDI_EDP_INPUT_MASK);
			fallthrough;
		case TRANS_DDI_EDP_INPUT_A_ON:
		case TRANS_DDI_EDP_INPUT_A_ONOFF:
			*pipe_mask = BIT(PIPE_A);
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			*pipe_mask = BIT(PIPE_B);
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			*pipe_mask = BIT(PIPE_C);
			break;
		}

		goto out;
	}

	mst_pipe_mask = 0;
	for_each_pipe(dev_priv, p) {
		enum transcoder cpu_transcoder = (enum transcoder)p;
		unsigned int port_mask, ddi_select;
		intel_wakeref_t trans_wakeref;

		trans_wakeref = intel_display_power_get_if_enabled(dev_priv,
								   POWER_DOMAIN_TRANSCODER(cpu_transcoder));
		if (!trans_wakeref)
			continue;

		if (DISPLAY_VER(dev_priv) >= 12) {
			port_mask = TGL_TRANS_DDI_PORT_MASK;
			ddi_select = TGL_TRANS_DDI_SELECT_PORT(port);
		} else {
			port_mask = TRANS_DDI_PORT_MASK;
			ddi_select = TRANS_DDI_SELECT_PORT(port);
		}

		tmp = intel_de_read(dev_priv,
				    TRANS_DDI_FUNC_CTL(cpu_transcoder));
		intel_display_power_put(dev_priv, POWER_DOMAIN_TRANSCODER(cpu_transcoder),
					trans_wakeref);

		if ((tmp & port_mask) != ddi_select)
			continue;

		if ((tmp & TRANS_DDI_MODE_SELECT_MASK) == TRANS_DDI_MODE_SELECT_DP_MST ||
		    (HAS_DP20(dev_priv) &&
		     (tmp & TRANS_DDI_MODE_SELECT_MASK) == TRANS_DDI_MODE_SELECT_FDI_OR_128B132B))
			mst_pipe_mask |= BIT(p);

		*pipe_mask |= BIT(p);
	}

	if (!*pipe_mask)
		drm_dbg_kms(&dev_priv->drm,
			    "No pipe for [ENCODER:%d:%s] found\n",
			    encoder->base.base.id, encoder->base.name);

	if (!mst_pipe_mask && hweight8(*pipe_mask) > 1) {
		drm_dbg_kms(&dev_priv->drm,
			    "Multiple pipes for [ENCODER:%d:%s] (pipe_mask %02x)\n",
			    encoder->base.base.id, encoder->base.name,
			    *pipe_mask);
		*pipe_mask = BIT(ffs(*pipe_mask) - 1);
	}

	if (mst_pipe_mask && mst_pipe_mask != *pipe_mask)
		drm_dbg_kms(&dev_priv->drm,
			    "Conflicting MST and non-MST state for [ENCODER:%d:%s] (pipe_mask %02x mst_pipe_mask %02x)\n",
			    encoder->base.base.id, encoder->base.name,
			    *pipe_mask, mst_pipe_mask);
	else
		*is_dp_mst = mst_pipe_mask;

out:
	if (*pipe_mask && (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))) {
		tmp = intel_de_read(dev_priv, BXT_PHY_CTL(port));
		if ((tmp & (BXT_PHY_CMNLANE_POWERDOWN_ACK |
			    BXT_PHY_LANE_POWERDOWN_ACK |
			    BXT_PHY_LANE_ENABLED)) != BXT_PHY_LANE_ENABLED)
			drm_err(&dev_priv->drm,
				"[ENCODER:%d:%s] enabled but PHY powered down? (PHY_CTL %08x)\n",
				encoder->base.base.id, encoder->base.name, tmp);
	}

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);
}

bool intel_ddi_get_hw_state(struct intel_encoder *encoder,
			    enum pipe *pipe)
{
	u8 pipe_mask;
	bool is_mst;

	intel_ddi_get_encoder_pipes(encoder, &pipe_mask, &is_mst);

	if (is_mst || !pipe_mask)
		return false;

	*pipe = ffs(pipe_mask) - 1;

	return true;
}

static enum intel_display_power_domain
intel_ddi_main_link_aux_domain(struct intel_digital_port *dig_port)
{
	/* ICL+ HW requires corresponding AUX IOs to be powered up for PSR with
	 * DC states enabled at the same time, while for driver initiated AUX
	 * transfers we need the same AUX IOs to be powered but with DC states
	 * disabled. Accordingly use the AUX power domain here which leaves DC
	 * states enabled.
	 * However, for non-A AUX ports the corresponding non-EDP transcoders
	 * would have already enabled power well 2 and DC_OFF. This means we can
	 * acquire a wider POWER_DOMAIN_AUX_{B,C,D,F} reference instead of a
	 * specific AUX_IO reference without powering up any extra wells.
	 * Note that PSR is enabled only on Port A even though this function
	 * returns the correct domain for other ports too.
	 */
	return dig_port->aux_ch == AUX_CH_A ? POWER_DOMAIN_AUX_IO_A :
					      intel_aux_power_domain(dig_port);
}

static void intel_ddi_get_power_domains(struct intel_encoder *encoder,
					struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);

	/*
	 * TODO: Add support for MST encoders. Atm, the following should never
	 * happen since fake-MST encoders don't set their get_power_domains()
	 * hook.
	 */
	if (drm_WARN_ON(&dev_priv->drm,
			intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)))
		return;

	dig_port = enc_to_dig_port(encoder);

	if (!intel_tc_port_in_tbt_alt_mode(dig_port)) {
		drm_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
		dig_port->ddi_io_wakeref = intel_display_power_get(dev_priv,
								   dig_port->ddi_io_power_domain);
	}

	/*
	 * AUX power is only needed for (e)DP mode, and for HDMI mode on TC
	 * ports.
	 */
	if (intel_crtc_has_dp_encoder(crtc_state) ||
	    intel_phy_is_tc(dev_priv, phy)) {
		drm_WARN_ON(&dev_priv->drm, dig_port->aux_wakeref);
		dig_port->aux_wakeref =
			intel_display_power_get(dev_priv,
						intel_ddi_main_link_aux_domain(dig_port));
	}
}

void intel_ddi_enable_pipe_clock(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	u32 val;

	if (cpu_transcoder != TRANSCODER_EDP) {
		if (DISPLAY_VER(dev_priv) >= 13)
			val = TGL_TRANS_CLK_SEL_PORT(phy);
		else if (DISPLAY_VER(dev_priv) >= 12)
			val = TGL_TRANS_CLK_SEL_PORT(encoder->port);
		else
			val = TRANS_CLK_SEL_PORT(encoder->port);

		intel_de_write(dev_priv, TRANS_CLK_SEL(cpu_transcoder), val);
	}
}

void intel_ddi_disable_pipe_clock(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (cpu_transcoder != TRANSCODER_EDP) {
		if (DISPLAY_VER(dev_priv) >= 12)
			intel_de_write(dev_priv,
				       TRANS_CLK_SEL(cpu_transcoder),
				       TGL_TRANS_CLK_SEL_DISABLED);
		else
			intel_de_write(dev_priv,
				       TRANS_CLK_SEL(cpu_transcoder),
				       TRANS_CLK_SEL_DISABLED);
	}
}

static void _skl_ddi_set_iboost(struct drm_i915_private *dev_priv,
				enum port port, u8 iboost)
{
	u32 tmp;

	tmp = intel_de_read(dev_priv, DISPIO_CR_TX_BMU_CR0);
	tmp &= ~(BALANCE_LEG_MASK(port) | BALANCE_LEG_DISABLE(port));
	if (iboost)
		tmp |= iboost << BALANCE_LEG_SHIFT(port);
	else
		tmp |= BALANCE_LEG_DISABLE(port);
	intel_de_write(dev_priv, DISPIO_CR_TX_BMU_CR0, tmp);
}

static void skl_ddi_set_iboost(struct intel_encoder *encoder,
			       const struct intel_crtc_state *crtc_state,
			       int level)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u8 iboost;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		iboost = intel_bios_encoder_hdmi_boost_level(encoder->devdata);
	else
		iboost = intel_bios_encoder_dp_boost_level(encoder->devdata);

	if (iboost == 0) {
		const struct intel_ddi_buf_trans *trans;
		int n_entries;

		trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
		if (drm_WARN_ON_ONCE(&dev_priv->drm, !trans))
			return;

		iboost = trans->entries[level].hsw.i_boost;
	}

	/* Make sure that the requested I_boost is valid */
	if (iboost && iboost != 0x1 && iboost != 0x3 && iboost != 0x7) {
		drm_err(&dev_priv->drm, "Invalid I_boost value %u\n", iboost);
		return;
	}

	_skl_ddi_set_iboost(dev_priv, encoder->port, iboost);

	if (encoder->port == PORT_A && dig_port->max_lanes == 4)
		_skl_ddi_set_iboost(dev_priv, PORT_E, iboost);
}

static u8 intel_ddi_dp_voltage_max(struct intel_dp *intel_dp,
				   const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int n_entries;

	encoder->get_buf_trans(encoder, crtc_state, &n_entries);

	if (drm_WARN_ON(&dev_priv->drm, n_entries < 1))
		n_entries = 1;
	if (drm_WARN_ON(&dev_priv->drm,
			n_entries > ARRAY_SIZE(index_to_dp_signal_levels)))
		n_entries = ARRAY_SIZE(index_to_dp_signal_levels);

	return index_to_dp_signal_levels[n_entries - 1] &
		DP_TRAIN_VOLTAGE_SWING_MASK;
}

/*
 * We assume that the full set of pre-emphasis values can be
 * used on all DDI platforms. Should that change we need to
 * rethink this code.
 */
static u8 intel_ddi_dp_preemph_max(struct intel_dp *intel_dp)
{
	return DP_TRAIN_PRE_EMPH_LEVEL_3;
}

static u32 icl_combo_phy_loadgen_select(const struct intel_crtc_state *crtc_state,
					int lane)
{
	if (crtc_state->port_clock > 600000)
		return 0;

	if (crtc_state->lane_count == 4)
		return lane >= 1 ? LOADGEN_SELECT : 0;
	else
		return lane == 1 || lane == 2 ? LOADGEN_SELECT : 0;
}

static void icl_ddi_combo_vswing_program(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct intel_ddi_buf_trans *trans;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	int n_entries, ln;
	u32 val;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&dev_priv->drm, !trans))
		return;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP)) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		val = EDP4K2K_MODE_OVRD_EN | EDP4K2K_MODE_OVRD_OPTIMIZED;
		intel_dp->hobl_active = is_hobl_buf_trans(trans);
		intel_de_rmw(dev_priv, ICL_PORT_CL_DW10(phy), val,
			     intel_dp->hobl_active ? val : 0);
	}

	/* Set PORT_TX_DW5 */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN(0, phy));
	val &= ~(SCALING_MODE_SEL_MASK | RTERM_SELECT_MASK |
		  TAP2_DISABLE | TAP3_DISABLE);
	val |= SCALING_MODE_SEL(0x2);
	val |= RTERM_SELECT(0x6);
	val |= TAP3_DISABLE;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);

	/* Program PORT_TX_DW2 */
	for (ln = 0; ln < 4; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);

		intel_de_rmw(dev_priv, ICL_PORT_TX_DW2_LN(ln, phy),
			     SWING_SEL_UPPER_MASK | SWING_SEL_LOWER_MASK | RCOMP_SCALAR_MASK,
			     SWING_SEL_UPPER(trans->entries[level].icl.dw2_swing_sel) |
			     SWING_SEL_LOWER(trans->entries[level].icl.dw2_swing_sel) |
			     RCOMP_SCALAR(0x98));
	}

	/* Program PORT_TX_DW4 */
	/* We cannot write to GRP. It would overwrite individual loadgen. */
	for (ln = 0; ln < 4; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);

		intel_de_rmw(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy),
			     POST_CURSOR_1_MASK | POST_CURSOR_2_MASK | CURSOR_COEFF_MASK,
			     POST_CURSOR_1(trans->entries[level].icl.dw4_post_cursor_1) |
			     POST_CURSOR_2(trans->entries[level].icl.dw4_post_cursor_2) |
			     CURSOR_COEFF(trans->entries[level].icl.dw4_cursor_coeff));
	}

	/* Program PORT_TX_DW7 */
	for (ln = 0; ln < 4; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);

		intel_de_rmw(dev_priv, ICL_PORT_TX_DW7_LN(ln, phy),
			     N_SCALAR_MASK,
			     N_SCALAR(trans->entries[level].icl.dw7_n_scalar));
	}
}

static void icl_combo_phy_set_signal_levels(struct intel_encoder *encoder,
					    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	u32 val;
	int ln;

	/*
	 * 1. If port type is eDP or DP,
	 * set PORT_PCS_DW1 cmnkeeper_enable to 1b,
	 * else clear to 0b.
	 */
	val = intel_de_read(dev_priv, ICL_PORT_PCS_DW1_LN(0, phy));
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		val &= ~COMMON_KEEPER_EN;
	else
		val |= COMMON_KEEPER_EN;
	intel_de_write(dev_priv, ICL_PORT_PCS_DW1_GRP(phy), val);

	/* 2. Program loadgen select */
	/*
	 * Program PORT_TX_DW4 depending on Bit rate and used lanes
	 * <= 6 GHz and 4 lanes (LN0=0, LN1=1, LN2=1, LN3=1)
	 * <= 6 GHz and 1,2 lanes (LN0=0, LN1=1, LN2=1, LN3=0)
	 * > 6 GHz (LN0=0, LN1=0, LN2=0, LN3=0)
	 */
	for (ln = 0; ln < 4; ln++) {
		intel_de_rmw(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy),
			     LOADGEN_SELECT,
			     icl_combo_phy_loadgen_select(crtc_state, ln));
	}

	/* 3. Set PORT_CL_DW5 SUS Clock Config to 11b */
	intel_de_rmw(dev_priv, ICL_PORT_CL_DW5(phy),
		     0, SUS_CLOCK_CONFIG);

	/* 4. Clear training enable to change swing values */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN(0, phy));
	val &= ~TX_TRAINING_EN;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);

	/* 5. Program swing and de-emphasis */
	icl_ddi_combo_vswing_program(encoder, crtc_state);

	/* 6. Set training enable to trigger update */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN(0, phy));
	val |= TX_TRAINING_EN;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);
}

static void icl_mg_phy_set_signal_levels(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, encoder->port);
	const struct intel_ddi_buf_trans *trans;
	int n_entries, ln;

	if (intel_tc_port_in_tbt_alt_mode(enc_to_dig_port(encoder)))
		return;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&dev_priv->drm, !trans))
		return;

	for (ln = 0; ln < 2; ln++) {
		intel_de_rmw(dev_priv, MG_TX1_LINK_PARAMS(ln, tc_port),
			     CRI_USE_FS32, 0);
		intel_de_rmw(dev_priv, MG_TX2_LINK_PARAMS(ln, tc_port),
			     CRI_USE_FS32, 0);
	}

	/* Program MG_TX_SWINGCTRL with values from vswing table */
	for (ln = 0; ln < 2; ln++) {
		int level;

		level = intel_ddi_level(encoder, crtc_state, 2*ln+0);

		intel_de_rmw(dev_priv, MG_TX1_SWINGCTRL(ln, tc_port),
			     CRI_TXDEEMPH_OVERRIDE_17_12_MASK,
			     CRI_TXDEEMPH_OVERRIDE_17_12(trans->entries[level].mg.cri_txdeemph_override_17_12));

		level = intel_ddi_level(encoder, crtc_state, 2*ln+1);

		intel_de_rmw(dev_priv, MG_TX2_SWINGCTRL(ln, tc_port),
			     CRI_TXDEEMPH_OVERRIDE_17_12_MASK,
			     CRI_TXDEEMPH_OVERRIDE_17_12(trans->entries[level].mg.cri_txdeemph_override_17_12));
	}

	/* Program MG_TX_DRVCTRL with values from vswing table */
	for (ln = 0; ln < 2; ln++) {
		int level;

		level = intel_ddi_level(encoder, crtc_state, 2*ln+0);

		intel_de_rmw(dev_priv, MG_TX1_DRVCTRL(ln, tc_port),
			     CRI_TXDEEMPH_OVERRIDE_11_6_MASK |
			     CRI_TXDEEMPH_OVERRIDE_5_0_MASK,
			     CRI_TXDEEMPH_OVERRIDE_11_6(trans->entries[level].mg.cri_txdeemph_override_11_6) |
			     CRI_TXDEEMPH_OVERRIDE_5_0(trans->entries[level].mg.cri_txdeemph_override_5_0) |
			     CRI_TXDEEMPH_OVERRIDE_EN);

		level = intel_ddi_level(encoder, crtc_state, 2*ln+1);

		intel_de_rmw(dev_priv, MG_TX2_DRVCTRL(ln, tc_port),
			     CRI_TXDEEMPH_OVERRIDE_11_6_MASK |
			     CRI_TXDEEMPH_OVERRIDE_5_0_MASK,
			     CRI_TXDEEMPH_OVERRIDE_11_6(trans->entries[level].mg.cri_txdeemph_override_11_6) |
			     CRI_TXDEEMPH_OVERRIDE_5_0(trans->entries[level].mg.cri_txdeemph_override_5_0) |
			     CRI_TXDEEMPH_OVERRIDE_EN);

		/* FIXME: Program CRI_LOADGEN_SEL after the spec is updated */
	}

	/*
	 * Program MG_CLKHUB<LN, port being used> with value from frequency table
	 * In case of Legacy mode on MG PHY, both TX1 and TX2 enabled so use the
	 * values from table for which TX1 and TX2 enabled.
	 */
	for (ln = 0; ln < 2; ln++) {
		intel_de_rmw(dev_priv, MG_CLKHUB(ln, tc_port),
			     CFG_LOW_RATE_LKREN_EN,
			     crtc_state->port_clock < 300000 ? CFG_LOW_RATE_LKREN_EN : 0);
	}

	/* Program the MG_TX_DCC<LN, port being used> based on the link frequency */
	for (ln = 0; ln < 2; ln++) {
		intel_de_rmw(dev_priv, MG_TX1_DCC(ln, tc_port),
			     CFG_AMI_CK_DIV_OVERRIDE_VAL_MASK |
			     CFG_AMI_CK_DIV_OVERRIDE_EN,
			     crtc_state->port_clock > 500000 ?
			     CFG_AMI_CK_DIV_OVERRIDE_VAL(1) |
			     CFG_AMI_CK_DIV_OVERRIDE_EN : 0);

		intel_de_rmw(dev_priv, MG_TX2_DCC(ln, tc_port),
			     CFG_AMI_CK_DIV_OVERRIDE_VAL_MASK |
			     CFG_AMI_CK_DIV_OVERRIDE_EN,
			     crtc_state->port_clock > 500000 ?
			     CFG_AMI_CK_DIV_OVERRIDE_VAL(1) |
			     CFG_AMI_CK_DIV_OVERRIDE_EN : 0);
	}

	/* Program MG_TX_PISO_READLOAD with values from vswing table */
	for (ln = 0; ln < 2; ln++) {
		intel_de_rmw(dev_priv, MG_TX1_PISO_READLOAD(ln, tc_port),
			     0, CRI_CALCINIT);
		intel_de_rmw(dev_priv, MG_TX2_PISO_READLOAD(ln, tc_port),
			     0, CRI_CALCINIT);
	}
}

static void tgl_dkl_phy_set_signal_levels(struct intel_encoder *encoder,
					  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, encoder->port);
	const struct intel_ddi_buf_trans *trans;
	int n_entries, ln;

	if (intel_tc_port_in_tbt_alt_mode(enc_to_dig_port(encoder)))
		return;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&dev_priv->drm, !trans))
		return;

	for (ln = 0; ln < 2; ln++) {
		int level;

		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, ln));

		intel_de_write(dev_priv, DKL_TX_PMD_LANE_SUS(tc_port), 0);

		level = intel_ddi_level(encoder, crtc_state, 2*ln+0);

		intel_de_rmw(dev_priv, DKL_TX_DPCNTL0(tc_port),
			     DKL_TX_PRESHOOT_COEFF_MASK |
			     DKL_TX_DE_EMPAHSIS_COEFF_MASK |
			     DKL_TX_VSWING_CONTROL_MASK,
			     DKL_TX_PRESHOOT_COEFF(trans->entries[level].dkl.preshoot) |
			     DKL_TX_DE_EMPHASIS_COEFF(trans->entries[level].dkl.de_emphasis) |
			     DKL_TX_VSWING_CONTROL(trans->entries[level].dkl.vswing));

		level = intel_ddi_level(encoder, crtc_state, 2*ln+1);

		intel_de_rmw(dev_priv, DKL_TX_DPCNTL1(tc_port),
			     DKL_TX_PRESHOOT_COEFF_MASK |
			     DKL_TX_DE_EMPAHSIS_COEFF_MASK |
			     DKL_TX_VSWING_CONTROL_MASK,
			     DKL_TX_PRESHOOT_COEFF(trans->entries[level].dkl.preshoot) |
			     DKL_TX_DE_EMPHASIS_COEFF(trans->entries[level].dkl.de_emphasis) |
			     DKL_TX_VSWING_CONTROL(trans->entries[level].dkl.vswing));

		intel_de_rmw(dev_priv, DKL_TX_DPCNTL2(tc_port),
			     DKL_TX_DP20BITMODE, 0);

		if (IS_ALDERLAKE_P(dev_priv)) {
			u32 val;

			if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
				if (ln == 0) {
					val = DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(0);
					val |= DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(2);
				} else {
					val = DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(3);
					val |= DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(3);
				}
			} else {
				val = DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1(0);
				val |= DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2(0);
			}

			intel_de_rmw(dev_priv, DKL_TX_DPCNTL2(tc_port),
				     DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1_MASK |
				     DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2_MASK,
				     val);
		}
	}
}

static int translate_signal_level(struct intel_dp *intel_dp,
				  u8 signal_levels)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int i;

	for (i = 0; i < ARRAY_SIZE(index_to_dp_signal_levels); i++) {
		if (index_to_dp_signal_levels[i] == signal_levels)
			return i;
	}

	drm_WARN(&i915->drm, 1,
		 "Unsupported voltage swing/pre-emphasis level: 0x%x\n",
		 signal_levels);

	return 0;
}

static int intel_ddi_dp_level(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state,
			      int lane)
{
	u8 train_set = intel_dp->train_set[lane];

	if (intel_dp_is_uhbr(crtc_state)) {
		return train_set & DP_TX_FFE_PRESET_VALUE_MASK;
	} else {
		u8 signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
						DP_TRAIN_PRE_EMPHASIS_MASK);

		return translate_signal_level(intel_dp, signal_levels);
	}
}

int intel_ddi_level(struct intel_encoder *encoder,
		    const struct intel_crtc_state *crtc_state,
		    int lane)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_ddi_buf_trans *trans;
	int level, n_entries;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&i915->drm, !trans))
		return 0;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		level = intel_ddi_hdmi_level(encoder, trans);
	else
		level = intel_ddi_dp_level(enc_to_intel_dp(encoder), crtc_state,
					   lane);

	if (drm_WARN_ON_ONCE(&i915->drm, level >= n_entries))
		level = n_entries - 1;

	return level;
}

static void
hsw_set_signal_levels(struct intel_encoder *encoder,
		      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	int level = intel_ddi_level(encoder, crtc_state, 0);
	enum port port = encoder->port;
	u32 signal_levels;

	if (has_iboost(dev_priv))
		skl_ddi_set_iboost(encoder, crtc_state, level);

	/* HDMI ignores the rest */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return;

	signal_levels = DDI_BUF_TRANS_SELECT(level);

	drm_dbg_kms(&dev_priv->drm, "Using signal levels %08x\n",
		    signal_levels);

	intel_dp->DP &= ~DDI_BUF_EMP_MASK;
	intel_dp->DP |= signal_levels;

	intel_de_write(dev_priv, DDI_BUF_CTL(port), intel_dp->DP);
	intel_de_posting_read(dev_priv, DDI_BUF_CTL(port));
}

static void _icl_ddi_enable_clock(struct drm_i915_private *i915, i915_reg_t reg,
				  u32 clk_sel_mask, u32 clk_sel, u32 clk_off)
{
	mutex_lock(&i915->dpll.lock);

	intel_de_rmw(i915, reg, clk_sel_mask, clk_sel);

	/*
	 * "This step and the step before must be
	 *  done with separate register writes."
	 */
	intel_de_rmw(i915, reg, clk_off, 0);

	mutex_unlock(&i915->dpll.lock);
}

static void _icl_ddi_disable_clock(struct drm_i915_private *i915, i915_reg_t reg,
				   u32 clk_off)
{
	mutex_lock(&i915->dpll.lock);

	intel_de_rmw(i915, reg, 0, clk_off);

	mutex_unlock(&i915->dpll.lock);
}

static bool _icl_ddi_is_clock_enabled(struct drm_i915_private *i915, i915_reg_t reg,
				      u32 clk_off)
{
	return !(intel_de_read(i915, reg) & clk_off);
}

static struct intel_shared_dpll *
_icl_ddi_get_pll(struct drm_i915_private *i915, i915_reg_t reg,
		 u32 clk_sel_mask, u32 clk_sel_shift)
{
	enum intel_dpll_id id;

	id = (intel_de_read(i915, reg) & clk_sel_mask) >> clk_sel_shift;

	return intel_get_shared_dpll_by_id(i915, id);
}

static void adls_ddi_enable_clock(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	_icl_ddi_enable_clock(i915, ADLS_DPCLKA_CFGCR(phy),
			      ADLS_DPCLKA_CFGCR_DDI_CLK_SEL_MASK(phy),
			      pll->info->id << ADLS_DPCLKA_CFGCR_DDI_SHIFT(phy),
			      ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static void adls_ddi_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	_icl_ddi_disable_clock(i915, ADLS_DPCLKA_CFGCR(phy),
			       ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static bool adls_ddi_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_is_clock_enabled(i915, ADLS_DPCLKA_CFGCR(phy),
					 ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static struct intel_shared_dpll *adls_ddi_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_get_pll(i915, ADLS_DPCLKA_CFGCR(phy),
				ADLS_DPCLKA_CFGCR_DDI_CLK_SEL_MASK(phy),
				ADLS_DPCLKA_CFGCR_DDI_SHIFT(phy));
}

static void rkl_ddi_enable_clock(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	_icl_ddi_enable_clock(i915, ICL_DPCLKA_CFGCR0,
			      RKL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy),
			      RKL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll->info->id, phy),
			      RKL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static void rkl_ddi_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	_icl_ddi_disable_clock(i915, ICL_DPCLKA_CFGCR0,
			       RKL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static bool rkl_ddi_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_is_clock_enabled(i915, ICL_DPCLKA_CFGCR0,
					 RKL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static struct intel_shared_dpll *rkl_ddi_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_get_pll(i915, ICL_DPCLKA_CFGCR0,
				RKL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy),
				RKL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy));
}

static void dg1_ddi_enable_clock(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	/*
	 * If we fail this, something went very wrong: first 2 PLLs should be
	 * used by first 2 phys and last 2 PLLs by last phys
	 */
	if (drm_WARN_ON(&i915->drm,
			(pll->info->id < DPLL_ID_DG1_DPLL2 && phy >= PHY_C) ||
			(pll->info->id >= DPLL_ID_DG1_DPLL2 && phy < PHY_C)))
		return;

	_icl_ddi_enable_clock(i915, DG1_DPCLKA_CFGCR0(phy),
			      DG1_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy),
			      DG1_DPCLKA_CFGCR0_DDI_CLK_SEL(pll->info->id, phy),
			      DG1_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static void dg1_ddi_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	_icl_ddi_disable_clock(i915, DG1_DPCLKA_CFGCR0(phy),
			       DG1_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static bool dg1_ddi_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_is_clock_enabled(i915, DG1_DPCLKA_CFGCR0(phy),
					 DG1_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static struct intel_shared_dpll *dg1_ddi_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);
	enum intel_dpll_id id;
	u32 val;

	val = intel_de_read(i915, DG1_DPCLKA_CFGCR0(phy));
	val &= DG1_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy);
	val >>= DG1_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy);
	id = val;

	/*
	 * _DG1_DPCLKA0_CFGCR0 maps between DPLL 0 and 1 with one bit for phy A
	 * and B while _DG1_DPCLKA1_CFGCR0 maps between DPLL 2 and 3 with one
	 * bit for phy C and D.
	 */
	if (phy >= PHY_C)
		id += DPLL_ID_DG1_DPLL2;

	return intel_get_shared_dpll_by_id(i915, id);
}

static void icl_ddi_combo_enable_clock(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	_icl_ddi_enable_clock(i915, ICL_DPCLKA_CFGCR0,
			      ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy),
			      ICL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll->info->id, phy),
			      ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static void icl_ddi_combo_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	_icl_ddi_disable_clock(i915, ICL_DPCLKA_CFGCR0,
			       ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static bool icl_ddi_combo_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_is_clock_enabled(i915, ICL_DPCLKA_CFGCR0,
					 ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

struct intel_shared_dpll *icl_ddi_combo_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_get_pll(i915, ICL_DPCLKA_CFGCR0,
				ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy),
				ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy));
}

static void jsl_ddi_tc_enable_clock(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum port port = encoder->port;

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	/*
	 * "For DDIC and DDID, program DDI_CLK_SEL to map the MG clock to the port.
	 *  MG does not exist, but the programming is required to ungate DDIC and DDID."
	 */
	intel_de_write(i915, DDI_CLK_SEL(port), DDI_CLK_SEL_MG);

	icl_ddi_combo_enable_clock(encoder, crtc_state);
}

static void jsl_ddi_tc_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;

	icl_ddi_combo_disable_clock(encoder);

	intel_de_write(i915, DDI_CLK_SEL(port), DDI_CLK_SEL_NONE);
}

static bool jsl_ddi_tc_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u32 tmp;

	tmp = intel_de_read(i915, DDI_CLK_SEL(port));

	if ((tmp & DDI_CLK_SEL_MASK) == DDI_CLK_SEL_NONE)
		return false;

	return icl_ddi_combo_is_clock_enabled(encoder);
}

static void icl_ddi_tc_enable_clock(struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum tc_port tc_port = intel_port_to_tc(i915, encoder->port);
	enum port port = encoder->port;

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	intel_de_write(i915, DDI_CLK_SEL(port),
		       icl_pll_to_ddi_clk_sel(encoder, crtc_state));

	mutex_lock(&i915->dpll.lock);

	intel_de_rmw(i915, ICL_DPCLKA_CFGCR0,
		     ICL_DPCLKA_CFGCR0_TC_CLK_OFF(tc_port), 0);

	mutex_unlock(&i915->dpll.lock);
}

static void icl_ddi_tc_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum tc_port tc_port = intel_port_to_tc(i915, encoder->port);
	enum port port = encoder->port;

	mutex_lock(&i915->dpll.lock);

	intel_de_rmw(i915, ICL_DPCLKA_CFGCR0,
		     0, ICL_DPCLKA_CFGCR0_TC_CLK_OFF(tc_port));

	mutex_unlock(&i915->dpll.lock);

	intel_de_write(i915, DDI_CLK_SEL(port), DDI_CLK_SEL_NONE);
}

static bool icl_ddi_tc_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum tc_port tc_port = intel_port_to_tc(i915, encoder->port);
	enum port port = encoder->port;
	u32 tmp;

	tmp = intel_de_read(i915, DDI_CLK_SEL(port));

	if ((tmp & DDI_CLK_SEL_MASK) == DDI_CLK_SEL_NONE)
		return false;

	tmp = intel_de_read(i915, ICL_DPCLKA_CFGCR0);

	return !(tmp & ICL_DPCLKA_CFGCR0_TC_CLK_OFF(tc_port));
}

static struct intel_shared_dpll *icl_ddi_tc_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum tc_port tc_port = intel_port_to_tc(i915, encoder->port);
	enum port port = encoder->port;
	enum intel_dpll_id id;
	u32 tmp;

	tmp = intel_de_read(i915, DDI_CLK_SEL(port));

	switch (tmp & DDI_CLK_SEL_MASK) {
	case DDI_CLK_SEL_TBT_162:
	case DDI_CLK_SEL_TBT_270:
	case DDI_CLK_SEL_TBT_540:
	case DDI_CLK_SEL_TBT_810:
		id = DPLL_ID_ICL_TBTPLL;
		break;
	case DDI_CLK_SEL_MG:
		id = icl_tc_port_to_pll_id(tc_port);
		break;
	default:
		MISSING_CASE(tmp);
		fallthrough;
	case DDI_CLK_SEL_NONE:
		return NULL;
	}

	return intel_get_shared_dpll_by_id(i915, id);
}

static struct intel_shared_dpll *bxt_ddi_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum intel_dpll_id id;

	switch (encoder->port) {
	case PORT_A:
		id = DPLL_ID_SKL_DPLL0;
		break;
	case PORT_B:
		id = DPLL_ID_SKL_DPLL1;
		break;
	case PORT_C:
		id = DPLL_ID_SKL_DPLL2;
		break;
	default:
		MISSING_CASE(encoder->port);
		return NULL;
	}

	return intel_get_shared_dpll_by_id(i915, id);
}

static void skl_ddi_enable_clock(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum port port = encoder->port;

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	mutex_lock(&i915->dpll.lock);

	intel_de_rmw(i915, DPLL_CTRL2,
		     DPLL_CTRL2_DDI_CLK_OFF(port) |
		     DPLL_CTRL2_DDI_CLK_SEL_MASK(port),
		     DPLL_CTRL2_DDI_CLK_SEL(pll->info->id, port) |
		     DPLL_CTRL2_DDI_SEL_OVERRIDE(port));

	mutex_unlock(&i915->dpll.lock);
}

static void skl_ddi_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;

	mutex_lock(&i915->dpll.lock);

	intel_de_rmw(i915, DPLL_CTRL2,
		     0, DPLL_CTRL2_DDI_CLK_OFF(port));

	mutex_unlock(&i915->dpll.lock);
}

static bool skl_ddi_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;

	/*
	 * FIXME Not sure if the override affects both
	 * the PLL selection and the CLK_OFF bit.
	 */
	return !(intel_de_read(i915, DPLL_CTRL2) & DPLL_CTRL2_DDI_CLK_OFF(port));
}

static struct intel_shared_dpll *skl_ddi_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum intel_dpll_id id;
	u32 tmp;

	tmp = intel_de_read(i915, DPLL_CTRL2);

	/*
	 * FIXME Not sure if the override affects both
	 * the PLL selection and the CLK_OFF bit.
	 */
	if ((tmp & DPLL_CTRL2_DDI_SEL_OVERRIDE(port)) == 0)
		return NULL;

	id = (tmp & DPLL_CTRL2_DDI_CLK_SEL_MASK(port)) >>
		DPLL_CTRL2_DDI_CLK_SEL_SHIFT(port);

	return intel_get_shared_dpll_by_id(i915, id);
}

void hsw_ddi_enable_clock(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum port port = encoder->port;

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	intel_de_write(i915, PORT_CLK_SEL(port), hsw_pll_to_ddi_pll_sel(pll));
}

void hsw_ddi_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;

	intel_de_write(i915, PORT_CLK_SEL(port), PORT_CLK_SEL_NONE);
}

bool hsw_ddi_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;

	return intel_de_read(i915, PORT_CLK_SEL(port)) != PORT_CLK_SEL_NONE;
}

static struct intel_shared_dpll *hsw_ddi_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	enum intel_dpll_id id;
	u32 tmp;

	tmp = intel_de_read(i915, PORT_CLK_SEL(port));

	switch (tmp & PORT_CLK_SEL_MASK) {
	case PORT_CLK_SEL_WRPLL1:
		id = DPLL_ID_WRPLL1;
		break;
	case PORT_CLK_SEL_WRPLL2:
		id = DPLL_ID_WRPLL2;
		break;
	case PORT_CLK_SEL_SPLL:
		id = DPLL_ID_SPLL;
		break;
	case PORT_CLK_SEL_LCPLL_810:
		id = DPLL_ID_LCPLL_810;
		break;
	case PORT_CLK_SEL_LCPLL_1350:
		id = DPLL_ID_LCPLL_1350;
		break;
	case PORT_CLK_SEL_LCPLL_2700:
		id = DPLL_ID_LCPLL_2700;
		break;
	default:
		MISSING_CASE(tmp);
		fallthrough;
	case PORT_CLK_SEL_NONE:
		return NULL;
	}

	return intel_get_shared_dpll_by_id(i915, id);
}

void intel_ddi_enable_clock(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	if (encoder->enable_clock)
		encoder->enable_clock(encoder, crtc_state);
}

void intel_ddi_disable_clock(struct intel_encoder *encoder)
{
	if (encoder->disable_clock)
		encoder->disable_clock(encoder);
}

void intel_ddi_sanitize_encoder_pll_mapping(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u32 port_mask;
	bool ddi_clk_needed;

	/*
	 * In case of DP MST, we sanitize the primary encoder only, not the
	 * virtual ones.
	 */
	if (encoder->type == INTEL_OUTPUT_DP_MST)
		return;

	if (!encoder->base.crtc && intel_encoder_is_dp(encoder)) {
		u8 pipe_mask;
		bool is_mst;

		intel_ddi_get_encoder_pipes(encoder, &pipe_mask, &is_mst);
		/*
		 * In the unlikely case that BIOS enables DP in MST mode, just
		 * warn since our MST HW readout is incomplete.
		 */
		if (drm_WARN_ON(&i915->drm, is_mst))
			return;
	}

	port_mask = BIT(encoder->port);
	ddi_clk_needed = encoder->base.crtc;

	if (encoder->type == INTEL_OUTPUT_DSI) {
		struct intel_encoder *other_encoder;

		port_mask = intel_dsi_encoder_ports(encoder);
		/*
		 * Sanity check that we haven't incorrectly registered another
		 * encoder using any of the ports of this DSI encoder.
		 */
		for_each_intel_encoder(&i915->drm, other_encoder) {
			if (other_encoder == encoder)
				continue;

			if (drm_WARN_ON(&i915->drm,
					port_mask & BIT(other_encoder->port)))
				return;
		}
		/*
		 * For DSI we keep the ddi clocks gated
		 * except during enable/disable sequence.
		 */
		ddi_clk_needed = false;
	}

	if (ddi_clk_needed || !encoder->is_clock_enabled ||
	    !encoder->is_clock_enabled(encoder))
		return;

	drm_notice(&i915->drm,
		   "[ENCODER:%d:%s] is disabled/in DSI mode with an ungated DDI clock, gate it\n",
		   encoder->base.base.id, encoder->base.name);

	encoder->disable_clock(encoder);
}

static void
icl_program_mg_dp_mode(struct intel_digital_port *dig_port,
		       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum tc_port tc_port = intel_port_to_tc(dev_priv, dig_port->base.port);
	enum phy phy = intel_port_to_phy(dev_priv, dig_port->base.port);
	u32 ln0, ln1, pin_assignment;
	u8 width;

	if (!intel_phy_is_tc(dev_priv, phy) ||
	    intel_tc_port_in_tbt_alt_mode(dig_port))
		return;

	if (DISPLAY_VER(dev_priv) >= 12) {
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x0));
		ln0 = intel_de_read(dev_priv, DKL_DP_MODE(tc_port));
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x1));
		ln1 = intel_de_read(dev_priv, DKL_DP_MODE(tc_port));
	} else {
		ln0 = intel_de_read(dev_priv, MG_DP_MODE(0, tc_port));
		ln1 = intel_de_read(dev_priv, MG_DP_MODE(1, tc_port));
	}

	ln0 &= ~(MG_DP_MODE_CFG_DP_X1_MODE | MG_DP_MODE_CFG_DP_X2_MODE);
	ln1 &= ~(MG_DP_MODE_CFG_DP_X1_MODE | MG_DP_MODE_CFG_DP_X2_MODE);

	/* DPPATC */
	pin_assignment = intel_tc_port_get_pin_assignment_mask(dig_port);
	width = crtc_state->lane_count;

	switch (pin_assignment) {
	case 0x0:
		drm_WARN_ON(&dev_priv->drm,
			    !intel_tc_port_in_legacy_mode(dig_port));
		if (width == 1) {
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x1:
		if (width == 4) {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x2:
		if (width == 2) {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x3:
	case 0x5:
		if (width == 1) {
			ln0 |= MG_DP_MODE_CFG_DP_X1_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	case 0x4:
	case 0x6:
		if (width == 1) {
			ln0 |= MG_DP_MODE_CFG_DP_X1_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X1_MODE;
		} else {
			ln0 |= MG_DP_MODE_CFG_DP_X2_MODE;
			ln1 |= MG_DP_MODE_CFG_DP_X2_MODE;
		}
		break;
	default:
		MISSING_CASE(pin_assignment);
	}

	if (DISPLAY_VER(dev_priv) >= 12) {
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x0));
		intel_de_write(dev_priv, DKL_DP_MODE(tc_port), ln0);
		intel_de_write(dev_priv, HIP_INDEX_REG(tc_port),
			       HIP_INDEX_VAL(tc_port, 0x1));
		intel_de_write(dev_priv, DKL_DP_MODE(tc_port), ln1);
	} else {
		intel_de_write(dev_priv, MG_DP_MODE(0, tc_port), ln0);
		intel_de_write(dev_priv, MG_DP_MODE(1, tc_port), ln1);
	}
}

static enum transcoder
tgl_dp_tp_transcoder(const struct intel_crtc_state *crtc_state)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
		return crtc_state->mst_master_transcoder;
	else
		return crtc_state->cpu_transcoder;
}

i915_reg_t dp_tp_ctl_reg(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) >= 12)
		return TGL_DP_TP_CTL(tgl_dp_tp_transcoder(crtc_state));
	else
		return DP_TP_CTL(encoder->port);
}

i915_reg_t dp_tp_status_reg(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) >= 12)
		return TGL_DP_TP_STATUS(tgl_dp_tp_transcoder(crtc_state));
	else
		return DP_TP_STATUS(encoder->port);
}

static void intel_dp_sink_set_msa_timing_par_ignore_state(struct intel_dp *intel_dp,
							  const struct intel_crtc_state *crtc_state,
							  bool enable)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!crtc_state->vrr.enable)
		return;

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_DOWNSPREAD_CTRL,
			       enable ? DP_MSA_TIMING_PAR_IGNORE_EN : 0) <= 0)
		drm_dbg_kms(&i915->drm,
			    "Failed to %s MSA_TIMING_PAR_IGNORE in the sink\n",
			    enabledisable(enable));
}

static void intel_dp_sink_set_fec_ready(struct intel_dp *intel_dp,
					const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!crtc_state->fec_enable)
		return;

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_FEC_CONFIGURATION, DP_FEC_READY) <= 0)
		drm_dbg_kms(&i915->drm,
			    "Failed to set FEC_READY in the sink\n");
}

static void intel_ddi_enable_fec(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp;
	u32 val;

	if (!crtc_state->fec_enable)
		return;

	intel_dp = enc_to_intel_dp(encoder);
	val = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));
	val |= DP_TP_CTL_FEC_ENABLE;
	intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), val);
}

static void intel_ddi_disable_fec_state(struct intel_encoder *encoder,
					const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp;
	u32 val;

	if (!crtc_state->fec_enable)
		return;

	intel_dp = enc_to_intel_dp(encoder);
	val = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));
	val &= ~DP_TP_CTL_FEC_ENABLE;
	intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), val);
	intel_de_posting_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));
}

static void intel_ddi_power_up_lanes(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (intel_phy_is_combo(i915, phy)) {
		bool lane_reversal =
			dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;

		intel_combo_phy_power_up_lanes(i915, phy, false,
					       crtc_state->lane_count,
					       lane_reversal);
	}
}

/* Splitter enable for eDP MSO is limited to certain pipes. */
static u8 intel_ddi_splitter_pipe_mask(struct drm_i915_private *i915)
{
	if (IS_ALDERLAKE_P(i915))
		return BIT(PIPE_A) | BIT(PIPE_B);
	else
		return BIT(PIPE_A);
}

static void intel_ddi_mso_get_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *pipe_config)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 dss1;

	if (!HAS_MSO(i915))
		return;

	dss1 = intel_de_read(i915, ICL_PIPE_DSS_CTL1(pipe));

	pipe_config->splitter.enable = dss1 & SPLITTER_ENABLE;
	if (!pipe_config->splitter.enable)
		return;

	if (drm_WARN_ON(&i915->drm, !(intel_ddi_splitter_pipe_mask(i915) & BIT(pipe)))) {
		pipe_config->splitter.enable = false;
		return;
	}

	switch (dss1 & SPLITTER_CONFIGURATION_MASK) {
	default:
		drm_WARN(&i915->drm, true,
			 "Invalid splitter configuration, dss1=0x%08x\n", dss1);
		fallthrough;
	case SPLITTER_CONFIGURATION_2_SEGMENT:
		pipe_config->splitter.link_count = 2;
		break;
	case SPLITTER_CONFIGURATION_4_SEGMENT:
		pipe_config->splitter.link_count = 4;
		break;
	}

	pipe_config->splitter.pixel_overlap = REG_FIELD_GET(OVERLAP_PIXELS_MASK, dss1);
}

static void intel_ddi_mso_configure(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 dss1 = 0;

	if (!HAS_MSO(i915))
		return;

	if (crtc_state->splitter.enable) {
		dss1 |= SPLITTER_ENABLE;
		dss1 |= OVERLAP_PIXELS(crtc_state->splitter.pixel_overlap);
		if (crtc_state->splitter.link_count == 2)
			dss1 |= SPLITTER_CONFIGURATION_2_SEGMENT;
		else
			dss1 |= SPLITTER_CONFIGURATION_4_SEGMENT;
	}

	intel_de_rmw(i915, ICL_PIPE_DSS_CTL1(pipe),
		     SPLITTER_ENABLE | SPLITTER_CONFIGURATION_MASK |
		     OVERLAP_PIXELS_MASK, dss1);
}

static void tgl_ddi_pre_enable_dp(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);

	intel_dp_set_link_params(intel_dp,
				 crtc_state->port_clock,
				 crtc_state->lane_count);

	/*
	 * We only configure what the register value will be here.  Actual
	 * enabling happens during link training farther down.
	 */
	intel_ddi_init_dp_buf_reg(encoder, crtc_state);

	/*
	 * 1. Enable Power Wells
	 *
	 * This was handled at the beginning of intel_atomic_commit_tail(),
	 * before we called down into this function.
	 */

	/* 2. Enable Panel Power if PPS is required */
	intel_pps_on(intel_dp);

	/*
	 * 3. For non-TBT Type-C ports, set FIA lane count
	 * (DFLEXDPSP.DPX4TXLATC)
	 *
	 * This was done before tgl_ddi_pre_enable_dp by
	 * hsw_crtc_enable()->intel_encoders_pre_pll_enable().
	 */

	/*
	 * 4. Enable the port PLL.
	 *
	 * The PLL enabling itself was already done before this function by
	 * hsw_crtc_enable()->intel_enable_shared_dpll().  We need only
	 * configure the PLL to port mapping here.
	 */
	intel_ddi_enable_clock(encoder, crtc_state);

	/* 5. If IO power is controlled through PWR_WELL_CTL, Enable IO Power */
	if (!intel_tc_port_in_tbt_alt_mode(dig_port)) {
		drm_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
		dig_port->ddi_io_wakeref = intel_display_power_get(dev_priv,
								   dig_port->ddi_io_power_domain);
	}

	/* 6. Program DP_MODE */
	icl_program_mg_dp_mode(dig_port, crtc_state);

	/*
	 * 7. The rest of the below are substeps under the bspec's "Enable and
	 * Train Display Port" step.  Note that steps that are specific to
	 * MST will be handled by intel_mst_pre_enable_dp() before/after it
	 * calls into this function.  Also intel_mst_pre_enable_dp() only calls
	 * us when active_mst_links==0, so any steps designated for "single
	 * stream or multi-stream master transcoder" can just be performed
	 * unconditionally here.
	 */

	/*
	 * 7.a Configure Transcoder Clock Select to direct the Port clock to the
	 * Transcoder.
	 */
	intel_ddi_enable_pipe_clock(encoder, crtc_state);

	if (HAS_DP20(dev_priv))
		intel_ddi_config_transcoder_dp2(encoder, crtc_state);

	/*
	 * 7.b Configure TRANS_DDI_FUNC_CTL DDI Select, DDI Mode Select & MST
	 * Transport Select
	 */
	intel_ddi_config_transcoder_func(encoder, crtc_state);

	/*
	 * 7.c Configure & enable DP_TP_CTL with link training pattern 1
	 * selected
	 *
	 * This will be handled by the intel_dp_start_link_train() farther
	 * down this function.
	 */

	/* 7.e Configure voltage swing and related IO settings */
	encoder->set_signal_levels(encoder, crtc_state);

	/*
	 * 7.f Combo PHY: Configure PORT_CL_DW10 Static Power Down to power up
	 * the used lanes of the DDI.
	 */
	intel_ddi_power_up_lanes(encoder, crtc_state);

	/*
	 * 7.g Program CoG/MSO configuration bits in DSS_CTL1 if selected.
	 */
	intel_ddi_mso_configure(crtc_state);

	if (!is_mst)
		intel_dp_set_power(intel_dp, DP_SET_POWER_D0);

	intel_dp_configure_protocol_converter(intel_dp, crtc_state);
	intel_dp_sink_set_decompression_state(intel_dp, crtc_state, true);
	/*
	 * DDI FEC: "anticipates enabling FEC encoding sets the FEC_READY bit
	 * in the FEC_CONFIGURATION register to 1 before initiating link
	 * training
	 */
	intel_dp_sink_set_fec_ready(intel_dp, crtc_state);

	intel_dp_check_frl_training(intel_dp);
	intel_dp_pcon_dsc_configure(intel_dp, crtc_state);

	/*
	 * 7.i Follow DisplayPort specification training sequence (see notes for
	 *     failure handling)
	 * 7.j If DisplayPort multi-stream - Set DP_TP_CTL link training to Idle
	 *     Pattern, wait for 5 idle patterns (DP_TP_STATUS Min_Idles_Sent)
	 *     (timeout after 800 us)
	 */
	intel_dp_start_link_train(intel_dp, crtc_state);

	/* 7.k Set DP_TP_CTL link training to Normal */
	if (!is_trans_port_sync_mode(crtc_state))
		intel_dp_stop_link_train(intel_dp, crtc_state);

	/* 7.l Configure and enable FEC if needed */
	intel_ddi_enable_fec(encoder, crtc_state);

	intel_dsc_dp_pps_write(encoder, crtc_state);
}

static void hsw_ddi_pre_enable_dp(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);

	if (DISPLAY_VER(dev_priv) < 11)
		drm_WARN_ON(&dev_priv->drm,
			    is_mst && (port == PORT_A || port == PORT_E));
	else
		drm_WARN_ON(&dev_priv->drm, is_mst && port == PORT_A);

	intel_dp_set_link_params(intel_dp,
				 crtc_state->port_clock,
				 crtc_state->lane_count);

	/*
	 * We only configure what the register value will be here.  Actual
	 * enabling happens during link training farther down.
	 */
	intel_ddi_init_dp_buf_reg(encoder, crtc_state);

	intel_pps_on(intel_dp);

	intel_ddi_enable_clock(encoder, crtc_state);

	if (!intel_tc_port_in_tbt_alt_mode(dig_port)) {
		drm_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
		dig_port->ddi_io_wakeref = intel_display_power_get(dev_priv,
								   dig_port->ddi_io_power_domain);
	}

	icl_program_mg_dp_mode(dig_port, crtc_state);

	if (has_buf_trans_select(dev_priv))
		hsw_prepare_dp_ddi_buffers(encoder, crtc_state);

	encoder->set_signal_levels(encoder, crtc_state);

	intel_ddi_power_up_lanes(encoder, crtc_state);

	if (!is_mst)
		intel_dp_set_power(intel_dp, DP_SET_POWER_D0);
	intel_dp_configure_protocol_converter(intel_dp, crtc_state);
	intel_dp_sink_set_decompression_state(intel_dp, crtc_state,
					      true);
	intel_dp_sink_set_fec_ready(intel_dp, crtc_state);
	intel_dp_start_link_train(intel_dp, crtc_state);
	if ((port != PORT_A || DISPLAY_VER(dev_priv) >= 9) &&
	    !is_trans_port_sync_mode(crtc_state))
		intel_dp_stop_link_train(intel_dp, crtc_state);

	intel_ddi_enable_fec(encoder, crtc_state);

	if (!is_mst)
		intel_ddi_enable_pipe_clock(encoder, crtc_state);

	intel_dsc_dp_pps_write(encoder, crtc_state);
}

static void intel_ddi_pre_enable_dp(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) >= 12)
		tgl_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);
	else
		hsw_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);

	/* MST will call a setting of MSA after an allocating of Virtual Channel
	 * from MST encoder pre_enable callback.
	 */
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
		intel_ddi_set_dp_msa(crtc_state, conn_state);
}

static void intel_ddi_pre_enable_hdmi(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state,
				      const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, true);
	intel_ddi_enable_clock(encoder, crtc_state);

	drm_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
	dig_port->ddi_io_wakeref = intel_display_power_get(dev_priv,
							   dig_port->ddi_io_power_domain);

	icl_program_mg_dp_mode(dig_port, crtc_state);

	intel_ddi_enable_pipe_clock(encoder, crtc_state);

	dig_port->set_infoframes(encoder,
				 crtc_state->has_infoframe,
				 crtc_state, conn_state);
}

static void intel_ddi_pre_enable(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/*
	 * When called from DP MST code:
	 * - conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - crtc_state will be the state of the first stream to
	 *   be activated on this port, and it may not be the same
	 *   stream that will be deactivated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	drm_WARN_ON(&dev_priv->drm, crtc_state->has_pch_encoder);

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		intel_ddi_pre_enable_hdmi(state, encoder, crtc_state,
					  conn_state);
	} else {
		struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

		intel_ddi_pre_enable_dp(state, encoder, crtc_state,
					conn_state);

		/* FIXME precompute everything properly */
		/* FIXME how do we turn infoframes off again? */
		if (dig_port->lspcon.active && dig_port->dp.has_hdmi_sink)
			dig_port->set_infoframes(encoder,
						 crtc_state->has_infoframe,
						 crtc_state, conn_state);
	}
}

static void intel_disable_ddi_buf(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	bool wait = false;
	u32 val;

	val = intel_de_read(dev_priv, DDI_BUF_CTL(port));
	if (val & DDI_BUF_CTL_ENABLE) {
		val &= ~DDI_BUF_CTL_ENABLE;
		intel_de_write(dev_priv, DDI_BUF_CTL(port), val);
		wait = true;
	}

	if (intel_crtc_has_dp_encoder(crtc_state)) {
		val = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));
		val &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		val |= DP_TP_CTL_LINK_TRAIN_PAT1;
		intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), val);
	}

	/* Disable FEC in DP Sink */
	intel_ddi_disable_fec_state(encoder, crtc_state);

	if (wait)
		intel_wait_ddi_buf_idle(dev_priv, port);
}

static void intel_ddi_post_disable_dp(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &dig_port->dp;
	bool is_mst = intel_crtc_has_type(old_crtc_state,
					  INTEL_OUTPUT_DP_MST);

	if (!is_mst)
		intel_dp_set_infoframes(encoder, false,
					old_crtc_state, old_conn_state);

	/*
	 * Power down sink before disabling the port, otherwise we end
	 * up getting interrupts from the sink on detecting link loss.
	 */
	intel_dp_set_power(intel_dp, DP_SET_POWER_D3);

	if (DISPLAY_VER(dev_priv) >= 12) {
		if (is_mst) {
			enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;
			u32 val;

			val = intel_de_read(dev_priv,
					    TRANS_DDI_FUNC_CTL(cpu_transcoder));
			val &= ~(TGL_TRANS_DDI_PORT_MASK |
				 TRANS_DDI_MODE_SELECT_MASK);
			intel_de_write(dev_priv,
				       TRANS_DDI_FUNC_CTL(cpu_transcoder),
				       val);
		}
	} else {
		if (!is_mst)
			intel_ddi_disable_pipe_clock(old_crtc_state);
	}

	intel_disable_ddi_buf(encoder, old_crtc_state);

	/*
	 * From TGL spec: "If single stream or multi-stream master transcoder:
	 * Configure Transcoder Clock select to direct no clock to the
	 * transcoder"
	 */
	if (DISPLAY_VER(dev_priv) >= 12)
		intel_ddi_disable_pipe_clock(old_crtc_state);

	intel_pps_vdd_on(intel_dp);
	intel_pps_off(intel_dp);

	if (!intel_tc_port_in_tbt_alt_mode(dig_port))
		intel_display_power_put(dev_priv,
					dig_port->ddi_io_power_domain,
					fetch_and_zero(&dig_port->ddi_io_wakeref));

	intel_ddi_disable_clock(encoder);
}

static void intel_ddi_post_disable_hdmi(struct intel_atomic_state *state,
					struct intel_encoder *encoder,
					const struct intel_crtc_state *old_crtc_state,
					const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;

	dig_port->set_infoframes(encoder, false,
				 old_crtc_state, old_conn_state);

	intel_ddi_disable_pipe_clock(old_crtc_state);

	intel_disable_ddi_buf(encoder, old_crtc_state);

	intel_display_power_put(dev_priv,
				dig_port->ddi_io_power_domain,
				fetch_and_zero(&dig_port->ddi_io_wakeref));

	intel_ddi_disable_clock(encoder);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, false);
}

static void intel_ddi_post_disable(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	bool is_tc_port = intel_phy_is_tc(dev_priv, phy);

	if (!intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DP_MST)) {
		intel_crtc_vblank_off(old_crtc_state);

		intel_disable_transcoder(old_crtc_state);

		intel_vrr_disable(old_crtc_state);

		intel_ddi_disable_transcoder_func(old_crtc_state);

		intel_dsc_disable(old_crtc_state);

		if (DISPLAY_VER(dev_priv) >= 9)
			skl_scaler_disable(old_crtc_state);
		else
			ilk_pfit_disable(old_crtc_state);
	}

	if (old_crtc_state->bigjoiner_linked_crtc) {
		struct intel_crtc *slave_crtc =
			old_crtc_state->bigjoiner_linked_crtc;
		const struct intel_crtc_state *old_slave_crtc_state =
			intel_atomic_get_old_crtc_state(state, slave_crtc);

		intel_crtc_vblank_off(old_slave_crtc_state);

		intel_dsc_disable(old_slave_crtc_state);
		skl_scaler_disable(old_slave_crtc_state);
	}

	/*
	 * When called from DP MST code:
	 * - old_conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - old_crtc_state will be the state of the last stream to
	 *   be deactivated on this port, and it may not be the same
	 *   stream that was activated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	if (intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI))
		intel_ddi_post_disable_hdmi(state, encoder, old_crtc_state,
					    old_conn_state);
	else
		intel_ddi_post_disable_dp(state, encoder, old_crtc_state,
					  old_conn_state);

	if (intel_crtc_has_dp_encoder(old_crtc_state) || is_tc_port)
		intel_display_power_put(dev_priv,
					intel_ddi_main_link_aux_domain(dig_port),
					fetch_and_zero(&dig_port->aux_wakeref));

	if (is_tc_port)
		intel_tc_port_put_link(dig_port);
}

static void trans_port_sync_stop_link_train(struct intel_atomic_state *state,
					    struct intel_encoder *encoder,
					    const struct intel_crtc_state *crtc_state)
{
	const struct drm_connector_state *conn_state;
	struct drm_connector *conn;
	int i;

	if (!crtc_state->sync_mode_slaves_mask)
		return;

	for_each_new_connector_in_state(&state->base, conn, conn_state, i) {
		struct intel_encoder *slave_encoder =
			to_intel_encoder(conn_state->best_encoder);
		struct intel_crtc *slave_crtc = to_intel_crtc(conn_state->crtc);
		const struct intel_crtc_state *slave_crtc_state;

		if (!slave_crtc)
			continue;

		slave_crtc_state =
			intel_atomic_get_new_crtc_state(state, slave_crtc);

		if (slave_crtc_state->master_transcoder !=
		    crtc_state->cpu_transcoder)
			continue;

		intel_dp_stop_link_train(enc_to_intel_dp(slave_encoder),
					 slave_crtc_state);
	}

	usleep_range(200, 400);

	intel_dp_stop_link_train(enc_to_intel_dp(encoder),
				 crtc_state);
}

static void intel_enable_ddi_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum port port = encoder->port;

	if (port == PORT_A && DISPLAY_VER(dev_priv) < 9)
		intel_dp_stop_link_train(intel_dp, crtc_state);

	drm_connector_update_privacy_screen(conn_state);
	intel_edp_backlight_on(crtc_state, conn_state);

	if (!dig_port->lspcon.active || dig_port->dp.has_hdmi_sink)
		intel_dp_set_infoframes(encoder, true, crtc_state, conn_state);

	intel_drrs_enable(intel_dp, crtc_state);

	if (crtc_state->has_audio)
		intel_audio_codec_enable(encoder, crtc_state, conn_state);

	trans_port_sync_stop_link_train(state, encoder, crtc_state);
}

static i915_reg_t
gen9_chicken_trans_reg_by_port(struct drm_i915_private *dev_priv,
			       enum port port)
{
	static const enum transcoder trans[] = {
		[PORT_A] = TRANSCODER_EDP,
		[PORT_B] = TRANSCODER_A,
		[PORT_C] = TRANSCODER_B,
		[PORT_D] = TRANSCODER_C,
		[PORT_E] = TRANSCODER_A,
	};

	drm_WARN_ON(&dev_priv->drm, DISPLAY_VER(dev_priv) < 9);

	if (drm_WARN_ON(&dev_priv->drm, port < PORT_A || port > PORT_E))
		port = PORT_A;

	return CHICKEN_TRANS(trans[port]);
}

static void intel_enable_ddi_hdmi(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_connector *connector = conn_state->connector;
	enum port port = encoder->port;

	if (!intel_hdmi_handle_sink_scrambling(encoder, connector,
					       crtc_state->hdmi_high_tmds_clock_ratio,
					       crtc_state->hdmi_scrambling))
		drm_dbg_kms(&dev_priv->drm,
			    "[CONNECTOR:%d:%s] Failed to configure sink scrambling/TMDS bit clock ratio\n",
			    connector->base.id, connector->name);

	if (has_buf_trans_select(dev_priv))
		hsw_prepare_hdmi_ddi_buffers(encoder, crtc_state);

	encoder->set_signal_levels(encoder, crtc_state);

	/* Display WA #1143: skl,kbl,cfl */
	if (DISPLAY_VER(dev_priv) == 9 && !IS_BROXTON(dev_priv)) {
		/*
		 * For some reason these chicken bits have been
		 * stuffed into a transcoder register, event though
		 * the bits affect a specific DDI port rather than
		 * a specific transcoder.
		 */
		i915_reg_t reg = gen9_chicken_trans_reg_by_port(dev_priv, port);
		u32 val;

		val = intel_de_read(dev_priv, reg);

		if (port == PORT_E)
			val |= DDIE_TRAINING_OVERRIDE_ENABLE |
				DDIE_TRAINING_OVERRIDE_VALUE;
		else
			val |= DDI_TRAINING_OVERRIDE_ENABLE |
				DDI_TRAINING_OVERRIDE_VALUE;

		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);

		udelay(1);

		if (port == PORT_E)
			val &= ~(DDIE_TRAINING_OVERRIDE_ENABLE |
				 DDIE_TRAINING_OVERRIDE_VALUE);
		else
			val &= ~(DDI_TRAINING_OVERRIDE_ENABLE |
				 DDI_TRAINING_OVERRIDE_VALUE);

		intel_de_write(dev_priv, reg, val);
	}

	intel_ddi_power_up_lanes(encoder, crtc_state);

	/* In HDMI/DVI mode, the port width, and swing/emphasis values
	 * are ignored so nothing special needs to be done besides
	 * enabling the port.
	 *
	 * On ADL_P the PHY link rate and lane count must be programmed but
	 * these are both 0 for HDMI.
	 */
	intel_de_write(dev_priv, DDI_BUF_CTL(port),
		       dig_port->saved_port_bits | DDI_BUF_CTL_ENABLE);

	if (crtc_state->has_audio)
		intel_audio_codec_enable(encoder, crtc_state, conn_state);
}

static void intel_enable_ddi(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	drm_WARN_ON(state->base.dev, crtc_state->has_pch_encoder);

	if (!intel_crtc_is_bigjoiner_slave(crtc_state))
		intel_ddi_enable_transcoder_func(encoder, crtc_state);

	intel_vrr_enable(encoder, crtc_state);

	intel_enable_transcoder(crtc_state);

	intel_crtc_vblank_on(crtc_state);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		intel_enable_ddi_hdmi(state, encoder, crtc_state, conn_state);
	else
		intel_enable_ddi_dp(state, encoder, crtc_state, conn_state);

	/* Enable hdcp if it's desired */
	if (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED)
		intel_hdcp_enable(to_intel_connector(conn_state->connector),
				  crtc_state,
				  (u8)conn_state->hdcp_content_type);
}

static void intel_disable_ddi_dp(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_dp->link_trained = false;

	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	intel_drrs_disable(intel_dp, old_crtc_state);
	intel_psr_disable(intel_dp, old_crtc_state);
	intel_edp_backlight_off(old_conn_state);
	/* Disable the decompression in DP Sink */
	intel_dp_sink_set_decompression_state(intel_dp, old_crtc_state,
					      false);
	/* Disable Ignore_MSA bit in DP Sink */
	intel_dp_sink_set_msa_timing_par_ignore_state(intel_dp, old_crtc_state,
						      false);
}

static void intel_disable_ddi_hdmi(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct drm_connector *connector = old_conn_state->connector;

	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	if (!intel_hdmi_handle_sink_scrambling(encoder, connector,
					       false, false))
		drm_dbg_kms(&i915->drm,
			    "[CONNECTOR:%d:%s] Failed to reset sink scrambling/TMDS bit clock ratio\n",
			    connector->base.id, connector->name);
}

static void intel_disable_ddi(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	intel_hdcp_disable(to_intel_connector(old_conn_state->connector));

	if (intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI))
		intel_disable_ddi_hdmi(state, encoder, old_crtc_state,
				       old_conn_state);
	else
		intel_disable_ddi_dp(state, encoder, old_crtc_state,
				     old_conn_state);
}

static void intel_ddi_update_pipe_dp(struct intel_atomic_state *state,
				     struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state,
				     const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_ddi_set_dp_msa(crtc_state, conn_state);

	intel_dp_set_infoframes(encoder, true, crtc_state, conn_state);
	intel_drrs_update(intel_dp, crtc_state);

	intel_backlight_update(state, encoder, crtc_state, conn_state);
	drm_connector_update_privacy_screen(conn_state);
}

void intel_ddi_update_pipe(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   const struct drm_connector_state *conn_state)
{

	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI) &&
	    !intel_encoder_is_mst(encoder))
		intel_ddi_update_pipe_dp(state, encoder, crtc_state,
					 conn_state);

	intel_hdcp_update_pipe(state, encoder, crtc_state, conn_state);
}

static void
intel_ddi_update_prepare(struct intel_atomic_state *state,
			 struct intel_encoder *encoder,
			 struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state =
		crtc ? intel_atomic_get_new_crtc_state(state, crtc) : NULL;
	int required_lanes = crtc_state ? crtc_state->lane_count : 1;

	drm_WARN_ON(state->base.dev, crtc && crtc->active);

	intel_tc_port_get_link(enc_to_dig_port(encoder),
		               required_lanes);
	if (crtc_state && crtc_state->hw.active) {
		struct intel_crtc *slave_crtc = crtc_state->bigjoiner_linked_crtc;

		intel_update_active_dpll(state, crtc, encoder);

		if (slave_crtc)
			intel_update_active_dpll(state, slave_crtc, encoder);
	}
}

static void
intel_ddi_update_complete(struct intel_atomic_state *state,
			  struct intel_encoder *encoder,
			  struct intel_crtc *crtc)
{
	intel_tc_port_put_link(enc_to_dig_port(encoder));
}

static void
intel_ddi_pre_pll_enable(struct intel_atomic_state *state,
			 struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	bool is_tc_port = intel_phy_is_tc(dev_priv, phy);

	if (is_tc_port)
		intel_tc_port_get_link(dig_port, crtc_state->lane_count);

	if (intel_crtc_has_dp_encoder(crtc_state) || is_tc_port) {
		drm_WARN_ON(&dev_priv->drm, dig_port->aux_wakeref);
		dig_port->aux_wakeref =
			intel_display_power_get(dev_priv,
						intel_ddi_main_link_aux_domain(dig_port));
	}

	if (is_tc_port && !intel_tc_port_in_tbt_alt_mode(dig_port))
		/*
		 * Program the lane count for static/dynamic connections on
		 * Type-C ports.  Skip this step for TBT.
		 */
		intel_tc_port_set_fia_lane_count(dig_port, crtc_state->lane_count);
	else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		bxt_ddi_phy_set_lane_optim_mask(encoder,
						crtc_state->lane_lat_optim_mask);
}

static void intel_ddi_prepare_link_retrain(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u32 dp_tp_ctl, ddi_buf_ctl;
	bool wait = false;

	dp_tp_ctl = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

	if (dp_tp_ctl & DP_TP_CTL_ENABLE) {
		ddi_buf_ctl = intel_de_read(dev_priv, DDI_BUF_CTL(port));
		if (ddi_buf_ctl & DDI_BUF_CTL_ENABLE) {
			intel_de_write(dev_priv, DDI_BUF_CTL(port),
				       ddi_buf_ctl & ~DDI_BUF_CTL_ENABLE);
			wait = true;
		}

		dp_tp_ctl &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		dp_tp_ctl |= DP_TP_CTL_LINK_TRAIN_PAT1;
		intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), dp_tp_ctl);
		intel_de_posting_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

		if (wait)
			intel_wait_ddi_buf_idle(dev_priv, port);
	}

	dp_tp_ctl = DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_PAT1;
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)) {
		dp_tp_ctl |= DP_TP_CTL_MODE_MST;
	} else {
		dp_tp_ctl |= DP_TP_CTL_MODE_SST;
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			dp_tp_ctl |= DP_TP_CTL_ENHANCED_FRAME_ENABLE;
	}
	intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), dp_tp_ctl);
	intel_de_posting_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

	intel_dp->DP |= DDI_BUF_CTL_ENABLE;
	intel_de_write(dev_priv, DDI_BUF_CTL(port), intel_dp->DP);
	intel_de_posting_read(dev_priv, DDI_BUF_CTL(port));

	intel_wait_ddi_buf_active(dev_priv, port);
}

static void intel_ddi_set_link_train(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state,
				     u8 dp_train_pat)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 temp;

	temp = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

	temp &= ~DP_TP_CTL_LINK_TRAIN_MASK;
	switch (intel_dp_training_pattern_symbol(dp_train_pat)) {
	case DP_TRAINING_PATTERN_DISABLE:
		temp |= DP_TP_CTL_LINK_TRAIN_NORMAL;
		break;
	case DP_TRAINING_PATTERN_1:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
		break;
	case DP_TRAINING_PATTERN_2:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT2;
		break;
	case DP_TRAINING_PATTERN_3:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT3;
		break;
	case DP_TRAINING_PATTERN_4:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT4;
		break;
	}

	intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), temp);
}

static void intel_ddi_set_idle_link_train(struct intel_dp *intel_dp,
					  const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u32 val;

	val = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));
	val &= ~DP_TP_CTL_LINK_TRAIN_MASK;
	val |= DP_TP_CTL_LINK_TRAIN_IDLE;
	intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), val);

	/*
	 * Until TGL on PORT_A we can have only eDP in SST mode. There the only
	 * reason we need to set idle transmission mode is to work around a HW
	 * issue where we enable the pipe while not in idle link-training mode.
	 * In this case there is requirement to wait for a minimum number of
	 * idle patterns to be sent.
	 */
	if (port == PORT_A && DISPLAY_VER(dev_priv) < 12)
		return;

	if (intel_de_wait_for_set(dev_priv,
				  dp_tp_status_reg(encoder, crtc_state),
				  DP_TP_STATUS_IDLE_DONE, 1))
		drm_err(&dev_priv->drm,
			"Timed out waiting for DP idle patterns\n");
}

static bool intel_ddi_is_audio_enabled(struct drm_i915_private *dev_priv,
				       enum transcoder cpu_transcoder)
{
	if (cpu_transcoder == TRANSCODER_EDP)
		return false;

	if (!intel_display_power_is_enabled(dev_priv, POWER_DOMAIN_AUDIO_MMIO))
		return false;

	return intel_de_read(dev_priv, HSW_AUD_PIN_ELD_CP_VLD) &
		AUDIO_OUTPUT_ENABLE(cpu_transcoder);
}

void intel_ddi_compute_min_voltage_level(struct drm_i915_private *dev_priv,
					 struct intel_crtc_state *crtc_state)
{
	if (DISPLAY_VER(dev_priv) >= 12 && crtc_state->port_clock > 594000)
		crtc_state->min_voltage_level = 2;
	else if (IS_JSL_EHL(dev_priv) && crtc_state->port_clock > 594000)
		crtc_state->min_voltage_level = 3;
	else if (DISPLAY_VER(dev_priv) >= 11 && crtc_state->port_clock > 594000)
		crtc_state->min_voltage_level = 1;
}

static enum transcoder bdw_transcoder_master_readout(struct drm_i915_private *dev_priv,
						     enum transcoder cpu_transcoder)
{
	u32 master_select;

	if (DISPLAY_VER(dev_priv) >= 11) {
		u32 ctl2 = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL2(cpu_transcoder));

		if ((ctl2 & PORT_SYNC_MODE_ENABLE) == 0)
			return INVALID_TRANSCODER;

		master_select = REG_FIELD_GET(PORT_SYNC_MODE_MASTER_SELECT_MASK, ctl2);
	} else {
		u32 ctl = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));

		if ((ctl & TRANS_DDI_PORT_SYNC_ENABLE) == 0)
			return INVALID_TRANSCODER;

		master_select = REG_FIELD_GET(TRANS_DDI_PORT_SYNC_MASTER_SELECT_MASK, ctl);
	}

	if (master_select == 0)
		return TRANSCODER_EDP;
	else
		return master_select - 1;
}

static void bdw_get_trans_port_sync_config(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	u32 transcoders = BIT(TRANSCODER_A) | BIT(TRANSCODER_B) |
		BIT(TRANSCODER_C) | BIT(TRANSCODER_D);
	enum transcoder cpu_transcoder;

	crtc_state->master_transcoder =
		bdw_transcoder_master_readout(dev_priv, crtc_state->cpu_transcoder);

	for_each_cpu_transcoder_masked(dev_priv, cpu_transcoder, transcoders) {
		enum intel_display_power_domain power_domain;
		intel_wakeref_t trans_wakeref;

		power_domain = POWER_DOMAIN_TRANSCODER(cpu_transcoder);
		trans_wakeref = intel_display_power_get_if_enabled(dev_priv,
								   power_domain);

		if (!trans_wakeref)
			continue;

		if (bdw_transcoder_master_readout(dev_priv, cpu_transcoder) ==
		    crtc_state->cpu_transcoder)
			crtc_state->sync_mode_slaves_mask |= BIT(cpu_transcoder);

		intel_display_power_put(dev_priv, power_domain, trans_wakeref);
	}

	drm_WARN_ON(&dev_priv->drm,
		    crtc_state->master_transcoder != INVALID_TRANSCODER &&
		    crtc_state->sync_mode_slaves_mask);
}

static void intel_ddi_read_func_ctl(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	u32 temp, flags = 0;

	temp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (temp & TRANS_DDI_PHSYNC)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (temp & TRANS_DDI_PVSYNC)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	pipe_config->hw.adjusted_mode.flags |= flags;

	switch (temp & TRANS_DDI_BPC_MASK) {
	case TRANS_DDI_BPC_6:
		pipe_config->pipe_bpp = 18;
		break;
	case TRANS_DDI_BPC_8:
		pipe_config->pipe_bpp = 24;
		break;
	case TRANS_DDI_BPC_10:
		pipe_config->pipe_bpp = 30;
		break;
	case TRANS_DDI_BPC_12:
		pipe_config->pipe_bpp = 36;
		break;
	default:
		break;
	}

	switch (temp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
		pipe_config->has_hdmi_sink = true;

		pipe_config->infoframes.enable |=
			intel_hdmi_infoframes_enabled(encoder, pipe_config);

		if (pipe_config->infoframes.enable)
			pipe_config->has_infoframe = true;

		if (temp & TRANS_DDI_HDMI_SCRAMBLING)
			pipe_config->hdmi_scrambling = true;
		if (temp & TRANS_DDI_HIGH_TMDS_CHAR_RATE)
			pipe_config->hdmi_high_tmds_clock_ratio = true;
		fallthrough;
	case TRANS_DDI_MODE_SELECT_DVI:
		pipe_config->output_types |= BIT(INTEL_OUTPUT_HDMI);
		pipe_config->lane_count = 4;
		break;
	case TRANS_DDI_MODE_SELECT_DP_SST:
		if (encoder->type == INTEL_OUTPUT_EDP)
			pipe_config->output_types |= BIT(INTEL_OUTPUT_EDP);
		else
			pipe_config->output_types |= BIT(INTEL_OUTPUT_DP);
		pipe_config->lane_count =
			((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;

		intel_cpu_transcoder_get_m1_n1(crtc, cpu_transcoder,
					       &pipe_config->dp_m_n);
		intel_cpu_transcoder_get_m2_n2(crtc, cpu_transcoder,
					       &pipe_config->dp_m2_n2);

		if (DISPLAY_VER(dev_priv) >= 11) {
			i915_reg_t dp_tp_ctl = dp_tp_ctl_reg(encoder, pipe_config);

			pipe_config->fec_enable =
				intel_de_read(dev_priv, dp_tp_ctl) & DP_TP_CTL_FEC_ENABLE;

			drm_dbg_kms(&dev_priv->drm,
				    "[ENCODER:%d:%s] Fec status: %u\n",
				    encoder->base.base.id, encoder->base.name,
				    pipe_config->fec_enable);
		}

		if (dig_port->lspcon.active && dig_port->dp.has_hdmi_sink)
			pipe_config->infoframes.enable |=
				intel_lspcon_infoframes_enabled(encoder, pipe_config);
		else
			pipe_config->infoframes.enable |=
				intel_hdmi_infoframes_enabled(encoder, pipe_config);
		break;
	case TRANS_DDI_MODE_SELECT_FDI_OR_128B132B:
		if (!HAS_DP20(dev_priv)) {
			/* FDI */
			pipe_config->output_types |= BIT(INTEL_OUTPUT_ANALOG);
			break;
		}
		fallthrough; /* 128b/132b */
	case TRANS_DDI_MODE_SELECT_DP_MST:
		pipe_config->output_types |= BIT(INTEL_OUTPUT_DP_MST);
		pipe_config->lane_count =
			((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;

		if (DISPLAY_VER(dev_priv) >= 12)
			pipe_config->mst_master_transcoder =
					REG_FIELD_GET(TRANS_DDI_MST_TRANSPORT_SELECT_MASK, temp);

		intel_cpu_transcoder_get_m1_n1(crtc, cpu_transcoder,
					       &pipe_config->dp_m_n);

		pipe_config->infoframes.enable |=
			intel_hdmi_infoframes_enabled(encoder, pipe_config);
		break;
	default:
		break;
	}
}

static void intel_ddi_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;

	/* XXX: DSI transcoder paranoia */
	if (drm_WARN_ON(&dev_priv->drm, transcoder_is_dsi(cpu_transcoder)))
		return;

	intel_ddi_read_func_ctl(encoder, pipe_config);

	intel_ddi_mso_get_config(encoder, pipe_config);

	pipe_config->has_audio =
		intel_ddi_is_audio_enabled(dev_priv, cpu_transcoder);

	if (encoder->type == INTEL_OUTPUT_EDP && dev_priv->vbt.edp.bpp &&
	    pipe_config->pipe_bpp > dev_priv->vbt.edp.bpp) {
		/*
		 * This is a big fat ugly hack.
		 *
		 * Some machines in UEFI boot mode provide us a VBT that has 18
		 * bpp and 1.62 GHz link bandwidth for eDP, which for reasons
		 * unknown we fail to light up. Yet the same BIOS boots up with
		 * 24 bpp and 2.7 GHz link. Use the same bpp as the BIOS uses as
		 * max, not what it tells us to use.
		 *
		 * Note: This will still be broken if the eDP panel is not lit
		 * up by the BIOS, and thus we can't get the mode at module
		 * load.
		 */
		drm_dbg_kms(&dev_priv->drm,
			    "pipe has %d bpp for eDP panel, overriding BIOS-provided max %d bpp\n",
			    pipe_config->pipe_bpp, dev_priv->vbt.edp.bpp);
		dev_priv->vbt.edp.bpp = pipe_config->pipe_bpp;
	}

	ddi_dotclock_get(pipe_config);

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_get_lane_lat_optim_mask(encoder);

	intel_ddi_compute_min_voltage_level(dev_priv, pipe_config);

	intel_hdmi_read_gcp_infoframe(encoder, pipe_config);

	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_AVI,
			     &pipe_config->infoframes.avi);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_SPD,
			     &pipe_config->infoframes.spd);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_VENDOR,
			     &pipe_config->infoframes.hdmi);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_DRM,
			     &pipe_config->infoframes.drm);

	if (DISPLAY_VER(dev_priv) >= 8)
		bdw_get_trans_port_sync_config(pipe_config);

	intel_read_dp_sdp(encoder, pipe_config, HDMI_PACKET_TYPE_GAMUT_METADATA);
	intel_read_dp_sdp(encoder, pipe_config, DP_SDP_VSC);

	intel_psr_get_config(encoder, pipe_config);
}

void intel_ddi_get_clock(struct intel_encoder *encoder,
			 struct intel_crtc_state *crtc_state,
			 struct intel_shared_dpll *pll)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum icl_port_dpll_id port_dpll_id = ICL_PORT_DPLL_DEFAULT;
	struct icl_port_dpll *port_dpll = &crtc_state->icl_port_dplls[port_dpll_id];
	bool pll_active;

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	port_dpll->pll = pll;
	pll_active = intel_dpll_get_hw_state(i915, pll, &port_dpll->hw_state);
	drm_WARN_ON(&i915->drm, !pll_active);

	icl_set_active_port_dpll(crtc_state, port_dpll_id);

	crtc_state->port_clock = intel_dpll_get_freq(i915, crtc_state->shared_dpll,
						     &crtc_state->dpll_hw_state);
}

static void dg2_ddi_get_config(struct intel_encoder *encoder,
				struct intel_crtc_state *crtc_state)
{
	intel_mpllb_readout_hw_state(encoder, &crtc_state->mpllb_state);
	crtc_state->port_clock = intel_mpllb_calc_port_clock(encoder, &crtc_state->mpllb_state);

	intel_ddi_get_config(encoder, crtc_state);
}

static void adls_ddi_get_config(struct intel_encoder *encoder,
				struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, adls_ddi_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void rkl_ddi_get_config(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, rkl_ddi_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void dg1_ddi_get_config(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, dg1_ddi_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void icl_ddi_combo_get_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, icl_ddi_combo_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void icl_ddi_tc_get_clock(struct intel_encoder *encoder,
				 struct intel_crtc_state *crtc_state,
				 struct intel_shared_dpll *pll)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum icl_port_dpll_id port_dpll_id;
	struct icl_port_dpll *port_dpll;
	bool pll_active;

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	if (intel_get_shared_dpll_id(i915, pll) == DPLL_ID_ICL_TBTPLL)
		port_dpll_id = ICL_PORT_DPLL_DEFAULT;
	else
		port_dpll_id = ICL_PORT_DPLL_MG_PHY;

	port_dpll = &crtc_state->icl_port_dplls[port_dpll_id];

	port_dpll->pll = pll;
	pll_active = intel_dpll_get_hw_state(i915, pll, &port_dpll->hw_state);
	drm_WARN_ON(&i915->drm, !pll_active);

	icl_set_active_port_dpll(crtc_state, port_dpll_id);

	if (intel_get_shared_dpll_id(i915, crtc_state->shared_dpll) == DPLL_ID_ICL_TBTPLL)
		crtc_state->port_clock = icl_calc_tbt_pll_link(i915, encoder->port);
	else
		crtc_state->port_clock = intel_dpll_get_freq(i915, crtc_state->shared_dpll,
							     &crtc_state->dpll_hw_state);
}

static void icl_ddi_tc_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state)
{
	icl_ddi_tc_get_clock(encoder, crtc_state, icl_ddi_tc_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void bxt_ddi_get_config(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, bxt_ddi_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void skl_ddi_get_config(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, skl_ddi_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

void hsw_ddi_get_config(struct intel_encoder *encoder,
			struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, hsw_ddi_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void intel_ddi_sync_state(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (intel_phy_is_tc(i915, phy))
		intel_tc_port_sanitize(enc_to_dig_port(encoder));

	if (crtc_state && intel_crtc_has_dp_encoder(crtc_state))
		intel_dp_sync_state(encoder, crtc_state);
}

static bool intel_ddi_initial_fastset_check(struct intel_encoder *encoder,
					    struct intel_crtc_state *crtc_state)
{
	if (intel_crtc_has_dp_encoder(crtc_state))
		return intel_dp_initial_fastset_check(encoder, crtc_state);

	return true;
}

static enum intel_output_type
intel_ddi_compute_output_type(struct intel_encoder *encoder,
			      struct intel_crtc_state *crtc_state,
			      struct drm_connector_state *conn_state)
{
	switch (conn_state->connector->connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		return INTEL_OUTPUT_HDMI;
	case DRM_MODE_CONNECTOR_eDP:
		return INTEL_OUTPUT_EDP;
	case DRM_MODE_CONNECTOR_DisplayPort:
		return INTEL_OUTPUT_DP;
	default:
		MISSING_CASE(conn_state->connector->connector_type);
		return INTEL_OUTPUT_UNUSED;
	}
}

static int intel_ddi_compute_config(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config,
				    struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	int ret;

	if (HAS_TRANSCODER(dev_priv, TRANSCODER_EDP) && port == PORT_A)
		pipe_config->cpu_transcoder = TRANSCODER_EDP;

	if (intel_crtc_has_type(pipe_config, INTEL_OUTPUT_HDMI)) {
		ret = intel_hdmi_compute_config(encoder, pipe_config, conn_state);
	} else {
		ret = intel_dp_compute_config(encoder, pipe_config, conn_state);
	}

	if (ret)
		return ret;

	if (IS_HASWELL(dev_priv) && crtc->pipe == PIPE_A &&
	    pipe_config->cpu_transcoder == TRANSCODER_EDP)
		pipe_config->pch_pfit.force_thru =
			pipe_config->pch_pfit.enabled ||
			pipe_config->crc_enabled;

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_calc_lane_lat_optim_mask(pipe_config->lane_count);

	intel_ddi_compute_min_voltage_level(dev_priv, pipe_config);

	return 0;
}

static bool mode_equal(const struct drm_display_mode *mode1,
		       const struct drm_display_mode *mode2)
{
	return drm_mode_match(mode1, mode2,
			      DRM_MODE_MATCH_TIMINGS |
			      DRM_MODE_MATCH_FLAGS |
			      DRM_MODE_MATCH_3D_FLAGS) &&
		mode1->clock == mode2->clock; /* we want an exact match */
}

static bool m_n_equal(const struct intel_link_m_n *m_n_1,
		      const struct intel_link_m_n *m_n_2)
{
	return m_n_1->tu == m_n_2->tu &&
		m_n_1->data_m == m_n_2->data_m &&
		m_n_1->data_n == m_n_2->data_n &&
		m_n_1->link_m == m_n_2->link_m &&
		m_n_1->link_n == m_n_2->link_n;
}

static bool crtcs_port_sync_compatible(const struct intel_crtc_state *crtc_state1,
				       const struct intel_crtc_state *crtc_state2)
{
	return crtc_state1->hw.active && crtc_state2->hw.active &&
		crtc_state1->output_types == crtc_state2->output_types &&
		crtc_state1->output_format == crtc_state2->output_format &&
		crtc_state1->lane_count == crtc_state2->lane_count &&
		crtc_state1->port_clock == crtc_state2->port_clock &&
		mode_equal(&crtc_state1->hw.adjusted_mode,
			   &crtc_state2->hw.adjusted_mode) &&
		m_n_equal(&crtc_state1->dp_m_n, &crtc_state2->dp_m_n);
}

static u8
intel_ddi_port_sync_transcoders(const struct intel_crtc_state *ref_crtc_state,
				int tile_group_id)
{
	struct drm_connector *connector;
	const struct drm_connector_state *conn_state;
	struct drm_i915_private *dev_priv = to_i915(ref_crtc_state->uapi.crtc->dev);
	struct intel_atomic_state *state =
		to_intel_atomic_state(ref_crtc_state->uapi.state);
	u8 transcoders = 0;
	int i;

	/*
	 * We don't enable port sync on BDW due to missing w/as and
	 * due to not having adjusted the modeset sequence appropriately.
	 */
	if (DISPLAY_VER(dev_priv) < 9)
		return 0;

	if (!intel_crtc_has_type(ref_crtc_state, INTEL_OUTPUT_DP))
		return 0;

	for_each_new_connector_in_state(&state->base, connector, conn_state, i) {
		struct intel_crtc *crtc = to_intel_crtc(conn_state->crtc);
		const struct intel_crtc_state *crtc_state;

		if (!crtc)
			continue;

		if (!connector->has_tile ||
		    connector->tile_group->id !=
		    tile_group_id)
			continue;
		crtc_state = intel_atomic_get_new_crtc_state(state,
							     crtc);
		if (!crtcs_port_sync_compatible(ref_crtc_state,
						crtc_state))
			continue;
		transcoders |= BIT(crtc_state->cpu_transcoder);
	}

	return transcoders;
}

static int intel_ddi_compute_config_late(struct intel_encoder *encoder,
					 struct intel_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct drm_connector *connector = conn_state->connector;
	u8 port_sync_transcoders = 0;

	drm_dbg_kms(&i915->drm, "[ENCODER:%d:%s] [CRTC:%d:%s]",
		    encoder->base.base.id, encoder->base.name,
		    crtc_state->uapi.crtc->base.id, crtc_state->uapi.crtc->name);

	if (connector->has_tile)
		port_sync_transcoders = intel_ddi_port_sync_transcoders(crtc_state,
									connector->tile_group->id);

	/*
	 * EDP Transcoders cannot be ensalved
	 * make them a master always when present
	 */
	if (port_sync_transcoders & BIT(TRANSCODER_EDP))
		crtc_state->master_transcoder = TRANSCODER_EDP;
	else
		crtc_state->master_transcoder = ffs(port_sync_transcoders) - 1;

	if (crtc_state->master_transcoder == crtc_state->cpu_transcoder) {
		crtc_state->master_transcoder = INVALID_TRANSCODER;
		crtc_state->sync_mode_slaves_mask =
			port_sync_transcoders & ~BIT(crtc_state->cpu_transcoder);
	}

	return 0;
}

static void intel_ddi_encoder_destroy(struct drm_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(to_intel_encoder(encoder));
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);

	intel_dp_encoder_flush_work(encoder);
	if (intel_phy_is_tc(i915, phy))
		intel_tc_port_flush_work(dig_port);
	intel_display_power_flush_work(i915);

	drm_encoder_cleanup(encoder);
	kfree(dig_port->hdcp_port_data.streams);
	kfree(dig_port);
}

static void intel_ddi_encoder_reset(struct drm_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(encoder));

	intel_dp->reset_link_params = true;

	intel_pps_encoder_reset(intel_dp);
}

static const struct drm_encoder_funcs intel_ddi_funcs = {
	.reset = intel_ddi_encoder_reset,
	.destroy = intel_ddi_encoder_destroy,
};

static struct intel_connector *
intel_ddi_init_dp_connector(struct intel_digital_port *dig_port)
{
	struct intel_connector *connector;
	enum port port = dig_port->base.port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	dig_port->dp.output_reg = DDI_BUF_CTL(port);
	dig_port->dp.prepare_link_retrain = intel_ddi_prepare_link_retrain;
	dig_port->dp.set_link_train = intel_ddi_set_link_train;
	dig_port->dp.set_idle_link_train = intel_ddi_set_idle_link_train;

	dig_port->dp.voltage_max = intel_ddi_dp_voltage_max;
	dig_port->dp.preemph_max = intel_ddi_dp_preemph_max;

	if (!intel_dp_init_connector(dig_port, connector)) {
		kfree(connector);
		return NULL;
	}

	if (dig_port->base.type == INTEL_OUTPUT_EDP) {
		struct drm_device *dev = dig_port->base.base.dev;
		struct drm_privacy_screen *privacy_screen;

		privacy_screen = drm_privacy_screen_get(dev->dev, NULL);
		if (!IS_ERR(privacy_screen)) {
			drm_connector_attach_privacy_screen_provider(&connector->base,
								     privacy_screen);
		} else if (PTR_ERR(privacy_screen) != -ENODEV) {
			drm_warn(dev, "Error getting privacy-screen\n");
		}
	}

	return connector;
}

static int modeset_pipe(struct drm_crtc *crtc,
			struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	int ret;

	state = drm_atomic_state_alloc(crtc->dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state)) {
		ret = PTR_ERR(crtc_state);
		goto out;
	}

	crtc_state->connectors_changed = true;

	ret = drm_atomic_commit(state);
out:
	drm_atomic_state_put(state);

	return ret;
}

static int intel_hdmi_reset_link(struct intel_encoder *encoder,
				 struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_hdmi *hdmi = enc_to_intel_hdmi(encoder);
	struct intel_connector *connector = hdmi->attached_connector;
	struct i2c_adapter *adapter =
		intel_gmbus_get_adapter(dev_priv, hdmi->ddc_bus);
	struct drm_connector_state *conn_state;
	struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	u8 config;
	int ret;

	if (!connector || connector->base.status != connector_status_connected)
		return 0;

	ret = drm_modeset_lock(&dev_priv->drm.mode_config.connection_mutex,
			       ctx);
	if (ret)
		return ret;

	conn_state = connector->base.state;

	crtc = to_intel_crtc(conn_state->crtc);
	if (!crtc)
		return 0;

	ret = drm_modeset_lock(&crtc->base.mutex, ctx);
	if (ret)
		return ret;

	crtc_state = to_intel_crtc_state(crtc->base.state);

	drm_WARN_ON(&dev_priv->drm,
		    !intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI));

	if (!crtc_state->hw.active)
		return 0;

	if (!crtc_state->hdmi_high_tmds_clock_ratio &&
	    !crtc_state->hdmi_scrambling)
		return 0;

	if (conn_state->commit &&
	    !try_wait_for_completion(&conn_state->commit->hw_done))
		return 0;

	ret = drm_scdc_readb(adapter, SCDC_TMDS_CONFIG, &config);
	if (ret < 0) {
		drm_err(&dev_priv->drm, "Failed to read TMDS config: %d\n",
			ret);
		return 0;
	}

	if (!!(config & SCDC_TMDS_BIT_CLOCK_RATIO_BY_40) ==
	    crtc_state->hdmi_high_tmds_clock_ratio &&
	    !!(config & SCDC_SCRAMBLING_ENABLE) ==
	    crtc_state->hdmi_scrambling)
		return 0;

	/*
	 * HDMI 2.0 says that one should not send scrambled data
	 * prior to configuring the sink scrambling, and that
	 * TMDS clock/data transmission should be suspended when
	 * changing the TMDS clock rate in the sink. So let's
	 * just do a full modeset here, even though some sinks
	 * would be perfectly happy if were to just reconfigure
	 * the SCDC settings on the fly.
	 */
	return modeset_pipe(&crtc->base, ctx);
}

static enum intel_hotplug_state
intel_ddi_hotplug(struct intel_encoder *encoder,
		  struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &dig_port->dp;
	enum phy phy = intel_port_to_phy(i915, encoder->port);
	bool is_tc = intel_phy_is_tc(i915, phy);
	struct drm_modeset_acquire_ctx ctx;
	enum intel_hotplug_state state;
	int ret;

	if (intel_dp->compliance.test_active &&
	    intel_dp->compliance.test_type == DP_TEST_LINK_PHY_TEST_PATTERN) {
		intel_dp_phy_test(encoder);
		/* just do the PHY test and nothing else */
		return INTEL_HOTPLUG_UNCHANGED;
	}

	state = intel_encoder_hotplug(encoder, connector);

	drm_modeset_acquire_init(&ctx, 0);

	for (;;) {
		if (connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIA)
			ret = intel_hdmi_reset_link(encoder, &ctx);
		else
			ret = intel_dp_retrain_link(encoder, &ctx);

		if (ret == -EDEADLK) {
			drm_modeset_backoff(&ctx);
			continue;
		}

		break;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	drm_WARN(encoder->base.dev, ret,
		 "Acquiring modeset locks failed with %i\n", ret);

	/*
	 * Unpowered type-c dongles can take some time to boot and be
	 * responsible, so here giving some time to those dongles to power up
	 * and then retrying the probe.
	 *
	 * On many platforms the HDMI live state signal is known to be
	 * unreliable, so we can't use it to detect if a sink is connected or
	 * not. Instead we detect if it's connected based on whether we can
	 * read the EDID or not. That in turn has a problem during disconnect,
	 * since the HPD interrupt may be raised before the DDC lines get
	 * disconnected (due to how the required length of DDC vs. HPD
	 * connector pins are specified) and so we'll still be able to get a
	 * valid EDID. To solve this schedule another detection cycle if this
	 * time around we didn't detect any change in the sink's connection
	 * status.
	 *
	 * Type-c connectors which get their HPD signal deasserted then
	 * reasserted, without unplugging/replugging the sink from the
	 * connector, introduce a delay until the AUX channel communication
	 * becomes functional. Retry the detection for 5 seconds on type-c
	 * connectors to account for this delay.
	 */
	if (state == INTEL_HOTPLUG_UNCHANGED &&
	    connector->hotplug_retries < (is_tc ? 5 : 1) &&
	    !dig_port->dp.is_mst)
		state = INTEL_HOTPLUG_RETRY;

	return state;
}

static bool lpt_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->hotplug.pch_hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, SDEISR) & bit;
}

static bool hsw_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->hotplug.hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, DEISR) & bit;
}

static bool bdw_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->hotplug.hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, GEN8_DE_PORT_ISR) & bit;
}

static struct intel_connector *
intel_ddi_init_hdmi_connector(struct intel_digital_port *dig_port)
{
	struct intel_connector *connector;
	enum port port = dig_port->base.port;

	connector = intel_connector_alloc();
	if (!connector)
		return NULL;

	dig_port->hdmi.hdmi_reg = DDI_BUF_CTL(port);
	intel_hdmi_init_connector(dig_port, connector);

	return connector;
}

static bool intel_ddi_a_force_4_lanes(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);

	if (dig_port->base.port != PORT_A)
		return false;

	if (dig_port->saved_port_bits & DDI_A_4_LANES)
		return false;

	/* Broxton/Geminilake: Bspec says that DDI_A_4_LANES is the only
	 *                     supported configuration
	 */
	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		return true;

	return false;
}

static int
intel_ddi_max_lanes(struct intel_digital_port *dig_port)
{
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	enum port port = dig_port->base.port;
	int max_lanes = 4;

	if (DISPLAY_VER(dev_priv) >= 11)
		return max_lanes;

	if (port == PORT_A || port == PORT_E) {
		if (intel_de_read(dev_priv, DDI_BUF_CTL(PORT_A)) & DDI_A_4_LANES)
			max_lanes = port == PORT_A ? 4 : 0;
		else
			/* Both A and E share 2 lanes */
			max_lanes = 2;
	}

	/*
	 * Some BIOS might fail to set this bit on port A if eDP
	 * wasn't lit up at boot.  Force this bit set when needed
	 * so we use the proper lane count for our calculations.
	 */
	if (intel_ddi_a_force_4_lanes(dig_port)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Forcing DDI_A_4_LANES for port A\n");
		dig_port->saved_port_bits |= DDI_A_4_LANES;
		max_lanes = 4;
	}

	return max_lanes;
}

static bool hti_uses_phy(struct drm_i915_private *i915, enum phy phy)
{
	return i915->hti_state & HDPORT_ENABLED &&
	       i915->hti_state & HDPORT_DDI_USED(phy);
}

static enum hpd_pin xelpd_hpd_pin(struct drm_i915_private *dev_priv,
				  enum port port)
{
	if (port >= PORT_D_XELPD)
		return HPD_PORT_D + port - PORT_D_XELPD;
	else if (port >= PORT_TC1)
		return HPD_PORT_TC1 + port - PORT_TC1;
	else
		return HPD_PORT_A + port - PORT_A;
}

static enum hpd_pin dg1_hpd_pin(struct drm_i915_private *dev_priv,
				enum port port)
{
	if (port >= PORT_TC1)
		return HPD_PORT_C + port - PORT_TC1;
	else
		return HPD_PORT_A + port - PORT_A;
}

static enum hpd_pin tgl_hpd_pin(struct drm_i915_private *dev_priv,
				enum port port)
{
	if (port >= PORT_TC1)
		return HPD_PORT_TC1 + port - PORT_TC1;
	else
		return HPD_PORT_A + port - PORT_A;
}

static enum hpd_pin rkl_hpd_pin(struct drm_i915_private *dev_priv,
				enum port port)
{
	if (HAS_PCH_TGP(dev_priv))
		return tgl_hpd_pin(dev_priv, port);

	if (port >= PORT_TC1)
		return HPD_PORT_C + port - PORT_TC1;
	else
		return HPD_PORT_A + port - PORT_A;
}

static enum hpd_pin icl_hpd_pin(struct drm_i915_private *dev_priv,
				enum port port)
{
	if (port >= PORT_C)
		return HPD_PORT_TC1 + port - PORT_C;
	else
		return HPD_PORT_A + port - PORT_A;
}

static enum hpd_pin ehl_hpd_pin(struct drm_i915_private *dev_priv,
				enum port port)
{
	if (port == PORT_D)
		return HPD_PORT_A;

	if (HAS_PCH_MCC(dev_priv))
		return icl_hpd_pin(dev_priv, port);

	return HPD_PORT_A + port - PORT_A;
}

static enum hpd_pin skl_hpd_pin(struct drm_i915_private *dev_priv, enum port port)
{
	if (HAS_PCH_TGP(dev_priv))
		return icl_hpd_pin(dev_priv, port);

	return HPD_PORT_A + port - PORT_A;
}

static bool intel_ddi_is_tc(struct drm_i915_private *i915, enum port port)
{
	if (DISPLAY_VER(i915) >= 12)
		return port >= PORT_TC1;
	else if (DISPLAY_VER(i915) >= 11)
		return port >= PORT_C;
	else
		return false;
}

static void intel_ddi_encoder_suspend(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	intel_dp_encoder_suspend(encoder);

	if (!intel_phy_is_tc(i915, phy))
		return;

	intel_tc_port_flush_work(dig_port);
}

static void intel_ddi_encoder_shutdown(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	intel_dp_encoder_shutdown(encoder);
	intel_hdmi_encoder_shutdown(encoder);

	if (!intel_phy_is_tc(i915, phy))
		return;

	intel_tc_port_flush_work(dig_port);
}

#define port_tc_name(port) ((port) - PORT_TC1 + '1')
#define tc_port_name(tc_port) ((tc_port) - TC_PORT_1 + '1')

void intel_ddi_init(struct drm_i915_private *dev_priv, enum port port)
{
	struct intel_digital_port *dig_port;
	struct intel_encoder *encoder;
	const struct intel_bios_encoder_data *devdata;
	bool init_hdmi, init_dp;
	enum phy phy = intel_port_to_phy(dev_priv, port);

	/*
	 * On platforms with HTI (aka HDPORT), if it's enabled at boot it may
	 * have taken over some of the PHYs and made them unavailable to the
	 * driver.  In that case we should skip initializing the corresponding
	 * outputs.
	 */
	if (hti_uses_phy(dev_priv, phy)) {
		drm_dbg_kms(&dev_priv->drm, "PORT %c / PHY %c reserved by HTI\n",
			    port_name(port), phy_name(phy));
		return;
	}

	devdata = intel_bios_encoder_data_lookup(dev_priv, port);
	if (!devdata) {
		drm_dbg_kms(&dev_priv->drm,
			    "VBT says port %c is not present\n",
			    port_name(port));
		return;
	}

	init_hdmi = intel_bios_encoder_supports_dvi(devdata) ||
		intel_bios_encoder_supports_hdmi(devdata);
	init_dp = intel_bios_encoder_supports_dp(devdata);

	if (intel_bios_is_lspcon_present(dev_priv, port)) {
		/*
		 * Lspcon device needs to be driven with DP connector
		 * with special detection sequence. So make sure DP
		 * is initialized before lspcon.
		 */
		init_dp = true;
		init_hdmi = false;
		drm_dbg_kms(&dev_priv->drm, "VBT says port %c has lspcon\n",
			    port_name(port));
	}

	if (!init_dp && !init_hdmi) {
		drm_dbg_kms(&dev_priv->drm,
			    "VBT says port %c is not DVI/HDMI/DP compatible, respect it\n",
			    port_name(port));
		return;
	}

	dig_port = kzalloc(sizeof(*dig_port), GFP_KERNEL);
	if (!dig_port)
		return;

	encoder = &dig_port->base;
	encoder->devdata = devdata;

	if (DISPLAY_VER(dev_priv) >= 13 && port >= PORT_D_XELPD) {
		drm_encoder_init(&dev_priv->drm, &encoder->base, &intel_ddi_funcs,
				 DRM_MODE_ENCODER_TMDS,
				 "DDI %c/PHY %c",
				 port_name(port - PORT_D_XELPD + PORT_D),
				 phy_name(phy));
	} else if (DISPLAY_VER(dev_priv) >= 12) {
		enum tc_port tc_port = intel_port_to_tc(dev_priv, port);

		drm_encoder_init(&dev_priv->drm, &encoder->base, &intel_ddi_funcs,
				 DRM_MODE_ENCODER_TMDS,
				 "DDI %s%c/PHY %s%c",
				 port >= PORT_TC1 ? "TC" : "",
				 port >= PORT_TC1 ? port_tc_name(port) : port_name(port),
				 tc_port != TC_PORT_NONE ? "TC" : "",
				 tc_port != TC_PORT_NONE ? tc_port_name(tc_port) : phy_name(phy));
	} else if (DISPLAY_VER(dev_priv) >= 11) {
		enum tc_port tc_port = intel_port_to_tc(dev_priv, port);

		drm_encoder_init(&dev_priv->drm, &encoder->base, &intel_ddi_funcs,
				 DRM_MODE_ENCODER_TMDS,
				 "DDI %c%s/PHY %s%c",
				 port_name(port),
				 port >= PORT_C ? " (TC)" : "",
				 tc_port != TC_PORT_NONE ? "TC" : "",
				 tc_port != TC_PORT_NONE ? tc_port_name(tc_port) : phy_name(phy));
	} else {
		drm_encoder_init(&dev_priv->drm, &encoder->base, &intel_ddi_funcs,
				 DRM_MODE_ENCODER_TMDS,
				 "DDI %c/PHY %c", port_name(port),  phy_name(phy));
	}

	mutex_init(&dig_port->hdcp_mutex);
	dig_port->num_hdcp_streams = 0;

	encoder->hotplug = intel_ddi_hotplug;
	encoder->compute_output_type = intel_ddi_compute_output_type;
	encoder->compute_config = intel_ddi_compute_config;
	encoder->compute_config_late = intel_ddi_compute_config_late;
	encoder->enable = intel_enable_ddi;
	encoder->pre_pll_enable = intel_ddi_pre_pll_enable;
	encoder->pre_enable = intel_ddi_pre_enable;
	encoder->disable = intel_disable_ddi;
	encoder->post_disable = intel_ddi_post_disable;
	encoder->update_pipe = intel_ddi_update_pipe;
	encoder->get_hw_state = intel_ddi_get_hw_state;
	encoder->sync_state = intel_ddi_sync_state;
	encoder->initial_fastset_check = intel_ddi_initial_fastset_check;
	encoder->suspend = intel_ddi_encoder_suspend;
	encoder->shutdown = intel_ddi_encoder_shutdown;
	encoder->get_power_domains = intel_ddi_get_power_domains;

	encoder->type = INTEL_OUTPUT_DDI;
	encoder->power_domain = intel_port_to_power_domain(port);
	encoder->port = port;
	encoder->cloneable = 0;
	encoder->pipe_mask = ~0;

	if (IS_DG2(dev_priv)) {
		encoder->enable_clock = intel_mpllb_enable;
		encoder->disable_clock = intel_mpllb_disable;
		encoder->get_config = dg2_ddi_get_config;
	} else if (IS_ALDERLAKE_S(dev_priv)) {
		encoder->enable_clock = adls_ddi_enable_clock;
		encoder->disable_clock = adls_ddi_disable_clock;
		encoder->is_clock_enabled = adls_ddi_is_clock_enabled;
		encoder->get_config = adls_ddi_get_config;
	} else if (IS_ROCKETLAKE(dev_priv)) {
		encoder->enable_clock = rkl_ddi_enable_clock;
		encoder->disable_clock = rkl_ddi_disable_clock;
		encoder->is_clock_enabled = rkl_ddi_is_clock_enabled;
		encoder->get_config = rkl_ddi_get_config;
	} else if (IS_DG1(dev_priv)) {
		encoder->enable_clock = dg1_ddi_enable_clock;
		encoder->disable_clock = dg1_ddi_disable_clock;
		encoder->is_clock_enabled = dg1_ddi_is_clock_enabled;
		encoder->get_config = dg1_ddi_get_config;
	} else if (IS_JSL_EHL(dev_priv)) {
		if (intel_ddi_is_tc(dev_priv, port)) {
			encoder->enable_clock = jsl_ddi_tc_enable_clock;
			encoder->disable_clock = jsl_ddi_tc_disable_clock;
			encoder->is_clock_enabled = jsl_ddi_tc_is_clock_enabled;
			encoder->get_config = icl_ddi_combo_get_config;
		} else {
			encoder->enable_clock = icl_ddi_combo_enable_clock;
			encoder->disable_clock = icl_ddi_combo_disable_clock;
			encoder->is_clock_enabled = icl_ddi_combo_is_clock_enabled;
			encoder->get_config = icl_ddi_combo_get_config;
		}
	} else if (DISPLAY_VER(dev_priv) >= 11) {
		if (intel_ddi_is_tc(dev_priv, port)) {
			encoder->enable_clock = icl_ddi_tc_enable_clock;
			encoder->disable_clock = icl_ddi_tc_disable_clock;
			encoder->is_clock_enabled = icl_ddi_tc_is_clock_enabled;
			encoder->get_config = icl_ddi_tc_get_config;
		} else {
			encoder->enable_clock = icl_ddi_combo_enable_clock;
			encoder->disable_clock = icl_ddi_combo_disable_clock;
			encoder->is_clock_enabled = icl_ddi_combo_is_clock_enabled;
			encoder->get_config = icl_ddi_combo_get_config;
		}
	} else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) {
		/* BXT/GLK have fixed PLL->port mapping */
		encoder->get_config = bxt_ddi_get_config;
	} else if (DISPLAY_VER(dev_priv) == 9) {
		encoder->enable_clock = skl_ddi_enable_clock;
		encoder->disable_clock = skl_ddi_disable_clock;
		encoder->is_clock_enabled = skl_ddi_is_clock_enabled;
		encoder->get_config = skl_ddi_get_config;
	} else if (IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv)) {
		encoder->enable_clock = hsw_ddi_enable_clock;
		encoder->disable_clock = hsw_ddi_disable_clock;
		encoder->is_clock_enabled = hsw_ddi_is_clock_enabled;
		encoder->get_config = hsw_ddi_get_config;
	}

	if (IS_DG2(dev_priv)) {
		encoder->set_signal_levels = intel_snps_phy_set_signal_levels;
	} else if (DISPLAY_VER(dev_priv) >= 12) {
		if (intel_phy_is_combo(dev_priv, phy))
			encoder->set_signal_levels = icl_combo_phy_set_signal_levels;
		else
			encoder->set_signal_levels = tgl_dkl_phy_set_signal_levels;
	} else if (DISPLAY_VER(dev_priv) >= 11) {
		if (intel_phy_is_combo(dev_priv, phy))
			encoder->set_signal_levels = icl_combo_phy_set_signal_levels;
		else
			encoder->set_signal_levels = icl_mg_phy_set_signal_levels;
	} else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv)) {
		encoder->set_signal_levels = bxt_ddi_phy_set_signal_levels;
	} else {
		encoder->set_signal_levels = hsw_set_signal_levels;
	}

	intel_ddi_buf_trans_init(encoder);

	if (DISPLAY_VER(dev_priv) >= 13)
		encoder->hpd_pin = xelpd_hpd_pin(dev_priv, port);
	else if (IS_DG1(dev_priv))
		encoder->hpd_pin = dg1_hpd_pin(dev_priv, port);
	else if (IS_ROCKETLAKE(dev_priv))
		encoder->hpd_pin = rkl_hpd_pin(dev_priv, port);
	else if (DISPLAY_VER(dev_priv) >= 12)
		encoder->hpd_pin = tgl_hpd_pin(dev_priv, port);
	else if (IS_JSL_EHL(dev_priv))
		encoder->hpd_pin = ehl_hpd_pin(dev_priv, port);
	else if (DISPLAY_VER(dev_priv) == 11)
		encoder->hpd_pin = icl_hpd_pin(dev_priv, port);
	else if (DISPLAY_VER(dev_priv) == 9 && !IS_BROXTON(dev_priv))
		encoder->hpd_pin = skl_hpd_pin(dev_priv, port);
	else
		encoder->hpd_pin = intel_hpd_pin_default(dev_priv, port);

	if (DISPLAY_VER(dev_priv) >= 11)
		dig_port->saved_port_bits =
			intel_de_read(dev_priv, DDI_BUF_CTL(port))
			& DDI_BUF_PORT_REVERSAL;
	else
		dig_port->saved_port_bits =
			intel_de_read(dev_priv, DDI_BUF_CTL(port))
			& (DDI_BUF_PORT_REVERSAL | DDI_A_4_LANES);

	if (intel_bios_is_lane_reversal_needed(dev_priv, port))
		dig_port->saved_port_bits |= DDI_BUF_PORT_REVERSAL;

	dig_port->dp.output_reg = INVALID_MMIO_REG;
	dig_port->max_lanes = intel_ddi_max_lanes(dig_port);
	dig_port->aux_ch = intel_bios_port_aux_ch(dev_priv, port);

	if (intel_phy_is_tc(dev_priv, phy)) {
		bool is_legacy =
			!intel_bios_encoder_supports_typec_usb(devdata) &&
			!intel_bios_encoder_supports_tbt(devdata);

		intel_tc_port_init(dig_port, is_legacy);

		encoder->update_prepare = intel_ddi_update_prepare;
		encoder->update_complete = intel_ddi_update_complete;
	}

	drm_WARN_ON(&dev_priv->drm, port > PORT_I);
	dig_port->ddi_io_power_domain = POWER_DOMAIN_PORT_DDI_A_IO +
					      port - PORT_A;

	if (init_dp) {
		if (!intel_ddi_init_dp_connector(dig_port))
			goto err;

		dig_port->hpd_pulse = intel_dp_hpd_pulse;

		if (dig_port->dp.mso_link_count)
			encoder->pipe_mask = intel_ddi_splitter_pipe_mask(dev_priv);
	}

	/* In theory we don't need the encoder->type check, but leave it just in
	 * case we have some really bad VBTs... */
	if (encoder->type != INTEL_OUTPUT_EDP && init_hdmi) {
		if (!intel_ddi_init_hdmi_connector(dig_port))
			goto err;
	}

	if (DISPLAY_VER(dev_priv) >= 11) {
		if (intel_phy_is_tc(dev_priv, phy))
			dig_port->connected = intel_tc_port_connected;
		else
			dig_port->connected = lpt_digital_port_connected;
	} else if (DISPLAY_VER(dev_priv) >= 8) {
		if (port == PORT_A || IS_GEMINILAKE(dev_priv) ||
		    IS_BROXTON(dev_priv))
			dig_port->connected = bdw_digital_port_connected;
		else
			dig_port->connected = lpt_digital_port_connected;
	} else {
		if (port == PORT_A)
			dig_port->connected = hsw_digital_port_connected;
		else
			dig_port->connected = lpt_digital_port_connected;
	}

	intel_infoframe_init(dig_port);

	return;

err:
	drm_encoder_cleanup(&encoder->base);
	kfree(dig_port);
}
