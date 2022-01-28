// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 * DisplayPort support for G4x,ILK,SNB,IVB,VLV,CHV (HSW+ handled by the DDI code).
 */

#include "g4x_dp.h"
#include "intel_audio.h"
#include "intel_backlight.h"
#include "intel_connector.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dpio_phy.h"
#include "intel_fifo_underrun.h"
#include "intel_hdmi.h"
#include "intel_hotplug.h"
#include "intel_pps.h"
#include "vlv_sideband.h"

struct dp_link_dpll {
	int clock;
	struct dpll dpll;
};

static const struct dp_link_dpll g4x_dpll[] = {
	{ 162000,
		{ .p1 = 2, .p2 = 10, .n = 2, .m1 = 23, .m2 = 8 } },
	{ 270000,
		{ .p1 = 1, .p2 = 10, .n = 1, .m1 = 14, .m2 = 2 } }
};

static const struct dp_link_dpll pch_dpll[] = {
	{ 162000,
		{ .p1 = 2, .p2 = 10, .n = 1, .m1 = 12, .m2 = 9 } },
	{ 270000,
		{ .p1 = 1, .p2 = 10, .n = 2, .m1 = 14, .m2 = 8 } }
};

static const struct dp_link_dpll vlv_dpll[] = {
	{ 162000,
		{ .p1 = 3, .p2 = 2, .n = 5, .m1 = 3, .m2 = 81 } },
	{ 270000,
		{ .p1 = 2, .p2 = 2, .n = 1, .m1 = 2, .m2 = 27 } }
};

/*
 * CHV supports eDP 1.4 that have  more link rates.
 * Below only provides the fixed rate but exclude variable rate.
 */
static const struct dp_link_dpll chv_dpll[] = {
	/*
	 * CHV requires to program fractional division for m2.
	 * m2 is stored in fixed point format using formula below
	 * (m2_int << 22) | m2_fraction
	 */
	{ 162000,	/* m2_int = 32, m2_fraction = 1677722 */
		{ .p1 = 4, .p2 = 2, .n = 1, .m1 = 2, .m2 = 0x819999a } },
	{ 270000,	/* m2_int = 27, m2_fraction = 0 */
		{ .p1 = 4, .p2 = 1, .n = 1, .m1 = 2, .m2 = 0x6c00000 } },
};

const struct dpll *vlv_get_dpll(struct drm_i915_private *i915)
{
	return IS_CHERRYVIEW(i915) ? &chv_dpll[0].dpll : &vlv_dpll[0].dpll;
}

void g4x_dp_set_clock(struct intel_encoder *encoder,
		      struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct dp_link_dpll *divisor = NULL;
	int i, count = 0;

	if (IS_G4X(dev_priv)) {
		divisor = g4x_dpll;
		count = ARRAY_SIZE(g4x_dpll);
	} else if (HAS_PCH_SPLIT(dev_priv)) {
		divisor = pch_dpll;
		count = ARRAY_SIZE(pch_dpll);
	} else if (IS_CHERRYVIEW(dev_priv)) {
		divisor = chv_dpll;
		count = ARRAY_SIZE(chv_dpll);
	} else if (IS_VALLEYVIEW(dev_priv)) {
		divisor = vlv_dpll;
		count = ARRAY_SIZE(vlv_dpll);
	}

	if (divisor && count) {
		for (i = 0; i < count; i++) {
			if (pipe_config->port_clock == divisor[i].clock) {
				pipe_config->dpll = divisor[i].dpll;
				pipe_config->clock_set = true;
				break;
			}
		}
	}
}

static void intel_dp_prepare(struct intel_encoder *encoder,
			     const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;

	intel_dp_set_link_params(intel_dp,
				 pipe_config->port_clock,
				 pipe_config->lane_count);

	/*
	 * There are four kinds of DP registers:
	 * IBX PCH
	 * SNB CPU
	 * IVB CPU
	 * CPT PCH
	 *
	 * IBX PCH and CPU are the same for almost everything,
	 * except that the CPU DP PLL is configured in this
	 * register
	 *
	 * CPT PCH is quite different, having many bits moved
	 * to the TRANS_DP_CTL register instead. That
	 * configuration happens (oddly) in ilk_pch_enable
	 */

	/* Preserve the BIOS-computed detected bit. This is
	 * supposed to be read-only.
	 */
	intel_dp->DP = intel_de_read(dev_priv, intel_dp->output_reg) & DP_DETECTED;

	/* Handle DP bits in common between all three register formats */
	intel_dp->DP |= DP_VOLTAGE_0_4 | DP_PRE_EMPHASIS_0;
	intel_dp->DP |= DP_PORT_WIDTH(pipe_config->lane_count);

	/* Split out the IBX/CPU vs CPT settings */

	if (IS_IVYBRIDGE(dev_priv) && port == PORT_A) {
		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;

		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		intel_dp->DP |= DP_PIPE_SEL_IVB(crtc->pipe);
	} else if (HAS_PCH_CPT(dev_priv) && port != PORT_A) {
		u32 trans_dp;

		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;

		trans_dp = intel_de_read(dev_priv, TRANS_DP_CTL(crtc->pipe));
		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			trans_dp |= TRANS_DP_ENH_FRAMING;
		else
			trans_dp &= ~TRANS_DP_ENH_FRAMING;
		intel_de_write(dev_priv, TRANS_DP_CTL(crtc->pipe), trans_dp);
	} else {
		if (IS_G4X(dev_priv) && pipe_config->limited_color_range)
			intel_dp->DP |= DP_COLOR_RANGE_16_235;

		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			intel_dp->DP |= DP_SYNC_HS_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			intel_dp->DP |= DP_SYNC_VS_HIGH;
		intel_dp->DP |= DP_LINK_TRAIN_OFF;

		if (drm_dp_enhanced_frame_cap(intel_dp->dpcd))
			intel_dp->DP |= DP_ENHANCED_FRAMING;

		if (IS_CHERRYVIEW(dev_priv))
			intel_dp->DP |= DP_PIPE_SEL_CHV(crtc->pipe);
		else
			intel_dp->DP |= DP_PIPE_SEL(crtc->pipe);
	}
}

static void assert_dp_port(struct intel_dp *intel_dp, bool state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct drm_i915_private *dev_priv = to_i915(dig_port->base.base.dev);
	bool cur_state = intel_de_read(dev_priv, intel_dp->output_reg) & DP_PORT_EN;

	I915_STATE_WARN(cur_state != state,
			"[ENCODER:%d:%s] state assertion failure (expected %s, current %s)\n",
			dig_port->base.base.base.id, dig_port->base.base.name,
			onoff(state), onoff(cur_state));
}
#define assert_dp_port_disabled(d) assert_dp_port((d), false)

static void assert_edp_pll(struct drm_i915_private *dev_priv, bool state)
{
	bool cur_state = intel_de_read(dev_priv, DP_A) & DP_PLL_ENABLE;

	I915_STATE_WARN(cur_state != state,
			"eDP PLL state assertion failure (expected %s, current %s)\n",
			onoff(state), onoff(cur_state));
}
#define assert_edp_pll_enabled(d) assert_edp_pll((d), true)
#define assert_edp_pll_disabled(d) assert_edp_pll((d), false)

static void ilk_edp_pll_on(struct intel_dp *intel_dp,
			   const struct intel_crtc_state *pipe_config)
{
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	assert_transcoder_disabled(dev_priv, pipe_config->cpu_transcoder);
	assert_dp_port_disabled(intel_dp);
	assert_edp_pll_disabled(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "enabling eDP PLL for clock %d\n",
		    pipe_config->port_clock);

	intel_dp->DP &= ~DP_PLL_FREQ_MASK;

	if (pipe_config->port_clock == 162000)
		intel_dp->DP |= DP_PLL_FREQ_162MHZ;
	else
		intel_dp->DP |= DP_PLL_FREQ_270MHZ;

	intel_de_write(dev_priv, DP_A, intel_dp->DP);
	intel_de_posting_read(dev_priv, DP_A);
	udelay(500);

	/*
	 * [DevILK] Work around required when enabling DP PLL
	 * while a pipe is enabled going to FDI:
	 * 1. Wait for the start of vertical blank on the enabled pipe going to FDI
	 * 2. Program DP PLL enable
	 */
	if (IS_IRONLAKE(dev_priv))
		intel_wait_for_vblank_if_active(dev_priv, !crtc->pipe);

	intel_dp->DP |= DP_PLL_ENABLE;

	intel_de_write(dev_priv, DP_A, intel_dp->DP);
	intel_de_posting_read(dev_priv, DP_A);
	udelay(200);
}

static void ilk_edp_pll_off(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	assert_transcoder_disabled(dev_priv, old_crtc_state->cpu_transcoder);
	assert_dp_port_disabled(intel_dp);
	assert_edp_pll_enabled(dev_priv);

	drm_dbg_kms(&dev_priv->drm, "disabling eDP PLL\n");

	intel_dp->DP &= ~DP_PLL_ENABLE;

	intel_de_write(dev_priv, DP_A, intel_dp->DP);
	intel_de_posting_read(dev_priv, DP_A);
	udelay(200);
}

static bool cpt_dp_port_selected(struct drm_i915_private *dev_priv,
				 enum port port, enum pipe *pipe)
{
	enum pipe p;

	for_each_pipe(dev_priv, p) {
		u32 val = intel_de_read(dev_priv, TRANS_DP_CTL(p));

		if ((val & TRANS_DP_PORT_SEL_MASK) == TRANS_DP_PORT_SEL(port)) {
			*pipe = p;
			return true;
		}
	}

	drm_dbg_kms(&dev_priv->drm, "No pipe for DP port %c found\n",
		    port_name(port));

	/* must initialize pipe to something for the asserts */
	*pipe = PIPE_A;

	return false;
}

bool g4x_dp_port_enabled(struct drm_i915_private *dev_priv,
			 i915_reg_t dp_reg, enum port port,
			 enum pipe *pipe)
{
	bool ret;
	u32 val;

	val = intel_de_read(dev_priv, dp_reg);

	ret = val & DP_PORT_EN;

	/* asserts want to know the pipe even if the port is disabled */
	if (IS_IVYBRIDGE(dev_priv) && port == PORT_A)
		*pipe = (val & DP_PIPE_SEL_MASK_IVB) >> DP_PIPE_SEL_SHIFT_IVB;
	else if (HAS_PCH_CPT(dev_priv) && port != PORT_A)
		ret &= cpt_dp_port_selected(dev_priv, port, pipe);
	else if (IS_CHERRYVIEW(dev_priv))
		*pipe = (val & DP_PIPE_SEL_MASK_CHV) >> DP_PIPE_SEL_SHIFT_CHV;
	else
		*pipe = (val & DP_PIPE_SEL_MASK) >> DP_PIPE_SEL_SHIFT;

	return ret;
}

static bool intel_dp_get_hw_state(struct intel_encoder *encoder,
				  enum pipe *pipe)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	intel_wakeref_t wakeref;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	ret = g4x_dp_port_enabled(dev_priv, intel_dp->output_reg,
				  encoder->port, pipe);

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

static void g4x_dp_get_m_n(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	if (crtc_state->has_pch_encoder)
		intel_pch_transcoder_get_m_n(crtc, &crtc_state->dp_m_n);
	else
		intel_cpu_transcoder_get_m_n(crtc, crtc_state->cpu_transcoder,
					     &crtc_state->dp_m_n,
					     &crtc_state->dp_m2_n2);
}

static void intel_dp_get_config(struct intel_encoder *encoder,
				struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	u32 tmp, flags = 0;
	enum port port = encoder->port;
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);

	if (encoder->type == INTEL_OUTPUT_EDP)
		pipe_config->output_types |= BIT(INTEL_OUTPUT_EDP);
	else
		pipe_config->output_types |= BIT(INTEL_OUTPUT_DP);

	tmp = intel_de_read(dev_priv, intel_dp->output_reg);

	pipe_config->has_audio = tmp & DP_AUDIO_OUTPUT_ENABLE && port != PORT_A;

	if (HAS_PCH_CPT(dev_priv) && port != PORT_A) {
		u32 trans_dp = intel_de_read(dev_priv,
					     TRANS_DP_CTL(crtc->pipe));

		if (trans_dp & TRANS_DP_HSYNC_ACTIVE_HIGH)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;

		if (trans_dp & TRANS_DP_VSYNC_ACTIVE_HIGH)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
	} else {
		if (tmp & DP_SYNC_HS_HIGH)
			flags |= DRM_MODE_FLAG_PHSYNC;
		else
			flags |= DRM_MODE_FLAG_NHSYNC;

		if (tmp & DP_SYNC_VS_HIGH)
			flags |= DRM_MODE_FLAG_PVSYNC;
		else
			flags |= DRM_MODE_FLAG_NVSYNC;
	}

	pipe_config->hw.adjusted_mode.flags |= flags;

	if (IS_G4X(dev_priv) && tmp & DP_COLOR_RANGE_16_235)
		pipe_config->limited_color_range = true;

	pipe_config->lane_count =
		((tmp & DP_PORT_WIDTH_MASK) >> DP_PORT_WIDTH_SHIFT) + 1;

	g4x_dp_get_m_n(pipe_config);

	if (port == PORT_A) {
		if ((intel_de_read(dev_priv, DP_A) & DP_PLL_FREQ_MASK) == DP_PLL_FREQ_162MHZ)
			pipe_config->port_clock = 162000;
		else
			pipe_config->port_clock = 270000;
	}

	pipe_config->hw.adjusted_mode.crtc_clock =
		intel_dotclock_calculate(pipe_config->port_clock,
					 &pipe_config->dp_m_n);

	if (intel_dp_is_edp(intel_dp) && dev_priv->vbt.edp.bpp &&
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
}

static void
intel_dp_link_down(struct intel_encoder *encoder,
		   const struct intel_crtc_state *old_crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	enum port port = encoder->port;

	if (drm_WARN_ON(&dev_priv->drm,
			(intel_de_read(dev_priv, intel_dp->output_reg) &
			 DP_PORT_EN) == 0))
		return;

	drm_dbg_kms(&dev_priv->drm, "\n");

	if ((IS_IVYBRIDGE(dev_priv) && port == PORT_A) ||
	    (HAS_PCH_CPT(dev_priv) && port != PORT_A)) {
		intel_dp->DP &= ~DP_LINK_TRAIN_MASK_CPT;
		intel_dp->DP |= DP_LINK_TRAIN_PAT_IDLE_CPT;
	} else {
		intel_dp->DP &= ~DP_LINK_TRAIN_MASK;
		intel_dp->DP |= DP_LINK_TRAIN_PAT_IDLE;
	}
	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	intel_dp->DP &= ~(DP_PORT_EN | DP_AUDIO_OUTPUT_ENABLE);
	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);

	/*
	 * HW workaround for IBX, we need to move the port
	 * to transcoder A after disabling it to allow the
	 * matching HDMI port to be enabled on transcoder A.
	 */
	if (HAS_PCH_IBX(dev_priv) && crtc->pipe == PIPE_B && port != PORT_A) {
		/*
		 * We get CPU/PCH FIFO underruns on the other pipe when
		 * doing the workaround. Sweep them under the rug.
		 */
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, false);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, false);

		/* always enable with pattern 1 (as per spec) */
		intel_dp->DP &= ~(DP_PIPE_SEL_MASK | DP_LINK_TRAIN_MASK);
		intel_dp->DP |= DP_PORT_EN | DP_PIPE_SEL(PIPE_A) |
			DP_LINK_TRAIN_PAT_1;
		intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
		intel_de_posting_read(dev_priv, intel_dp->output_reg);

		intel_dp->DP &= ~DP_PORT_EN;
		intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
		intel_de_posting_read(dev_priv, intel_dp->output_reg);

		intel_wait_for_vblank_if_active(dev_priv, PIPE_A);
		intel_set_cpu_fifo_underrun_reporting(dev_priv, PIPE_A, true);
		intel_set_pch_fifo_underrun_reporting(dev_priv, PIPE_A, true);
	}

	msleep(intel_dp->pps.panel_power_down_delay);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		intel_wakeref_t wakeref;

		with_intel_pps_lock(intel_dp, wakeref)
			intel_dp->pps.active_pipe = INVALID_PIPE;
	}
}

static void intel_disable_dp(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *old_crtc_state,
			     const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	intel_dp->link_trained = false;

	if (old_crtc_state->has_audio)
		intel_audio_codec_disable(encoder,
					  old_crtc_state, old_conn_state);

	/*
	 * Make sure the panel is off before trying to change the mode.
	 * But also ensure that we have vdd while we switch off the panel.
	 */
	intel_pps_vdd_on(intel_dp);
	intel_edp_backlight_off(old_conn_state);
	intel_dp_set_power(intel_dp, DP_SET_POWER_D3);
	intel_pps_off(intel_dp);
}

static void g4x_disable_dp(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *old_crtc_state,
			   const struct drm_connector_state *old_conn_state)
{
	intel_disable_dp(state, encoder, old_crtc_state, old_conn_state);
}

static void vlv_disable_dp(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *old_crtc_state,
			   const struct drm_connector_state *old_conn_state)
{
	intel_disable_dp(state, encoder, old_crtc_state, old_conn_state);
}

static void g4x_post_disable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;

	/*
	 * Bspec does not list a specific disable sequence for g4x DP.
	 * Follow the ilk+ sequence (disable pipe before the port) for
	 * g4x DP as it does not suffer from underruns like the normal
	 * g4x modeset sequence (disable pipe after the port).
	 */
	intel_dp_link_down(encoder, old_crtc_state);

	/* Only ilk+ has port A */
	if (port == PORT_A)
		ilk_edp_pll_off(intel_dp, old_crtc_state);
}

static void vlv_post_disable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	intel_dp_link_down(encoder, old_crtc_state);
}

static void chv_post_disable_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	intel_dp_link_down(encoder, old_crtc_state);

	vlv_dpio_get(dev_priv);

	/* Assert data lane reset */
	chv_data_lane_soft_reset(encoder, old_crtc_state, true);

	vlv_dpio_put(dev_priv);
}

static void
cpt_set_link_train(struct intel_dp *intel_dp,
		   const struct intel_crtc_state *crtc_state,
		   u8 dp_train_pat)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	intel_dp->DP &= ~DP_LINK_TRAIN_MASK_CPT;

	switch (intel_dp_training_pattern_symbol(dp_train_pat)) {
	case DP_TRAINING_PATTERN_DISABLE:
		intel_dp->DP |= DP_LINK_TRAIN_OFF_CPT;
		break;
	case DP_TRAINING_PATTERN_1:
		intel_dp->DP |= DP_LINK_TRAIN_PAT_1_CPT;
		break;
	case DP_TRAINING_PATTERN_2:
		intel_dp->DP |= DP_LINK_TRAIN_PAT_2_CPT;
		break;
	default:
		MISSING_CASE(intel_dp_training_pattern_symbol(dp_train_pat));
		return;
	}

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

static void
g4x_set_link_train(struct intel_dp *intel_dp,
		   const struct intel_crtc_state *crtc_state,
		   u8 dp_train_pat)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	intel_dp->DP &= ~DP_LINK_TRAIN_MASK;

	switch (intel_dp_training_pattern_symbol(dp_train_pat)) {
	case DP_TRAINING_PATTERN_DISABLE:
		intel_dp->DP |= DP_LINK_TRAIN_OFF;
		break;
	case DP_TRAINING_PATTERN_1:
		intel_dp->DP |= DP_LINK_TRAIN_PAT_1;
		break;
	case DP_TRAINING_PATTERN_2:
		intel_dp->DP |= DP_LINK_TRAIN_PAT_2;
		break;
	default:
		MISSING_CASE(intel_dp_training_pattern_symbol(dp_train_pat));
		return;
	}

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

static void intel_dp_enable_port(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);

	/* enable with pattern 1 (as per spec) */

	intel_dp_program_link_training_pattern(intel_dp, crtc_state,
					       DP_PHY_DPRX, DP_TRAINING_PATTERN_1);

	/*
	 * Magic for VLV/CHV. We _must_ first set up the register
	 * without actually enabling the port, and then do another
	 * write to enable the port. Otherwise link training will
	 * fail when the power sequencer is freshly used for this port.
	 */
	intel_dp->DP |= DP_PORT_EN;
	if (crtc_state->has_audio)
		intel_dp->DP |= DP_AUDIO_OUTPUT_ENABLE;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

static void intel_enable_dp(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *pipe_config,
			    const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	u32 dp_reg = intel_de_read(dev_priv, intel_dp->output_reg);
	enum pipe pipe = crtc->pipe;
	intel_wakeref_t wakeref;

	if (drm_WARN_ON(&dev_priv->drm, dp_reg & DP_PORT_EN))
		return;

	with_intel_pps_lock(intel_dp, wakeref) {
		if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			vlv_pps_init(encoder, pipe_config);

		intel_dp_enable_port(intel_dp, pipe_config);

		intel_pps_vdd_on_unlocked(intel_dp);
		intel_pps_on_unlocked(intel_dp);
		intel_pps_vdd_off_unlocked(intel_dp, true);
	}

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		unsigned int lane_mask = 0x0;

		if (IS_CHERRYVIEW(dev_priv))
			lane_mask = intel_dp_unused_lane_mask(pipe_config->lane_count);

		vlv_wait_port_ready(dev_priv, dp_to_dig_port(intel_dp),
				    lane_mask);
	}

	intel_dp_set_power(intel_dp, DP_SET_POWER_D0);
	intel_dp_configure_protocol_converter(intel_dp, pipe_config);
	intel_dp_check_frl_training(intel_dp);
	intel_dp_pcon_dsc_configure(intel_dp, pipe_config);
	intel_dp_start_link_train(intel_dp, pipe_config);
	intel_dp_stop_link_train(intel_dp, pipe_config);

	if (pipe_config->has_audio) {
		drm_dbg(&dev_priv->drm, "Enabling DP audio on pipe %c\n",
			pipe_name(pipe));
		intel_audio_codec_enable(encoder, pipe_config, conn_state);
	}
}

static void g4x_enable_dp(struct intel_atomic_state *state,
			  struct intel_encoder *encoder,
			  const struct intel_crtc_state *pipe_config,
			  const struct drm_connector_state *conn_state)
{
	intel_enable_dp(state, encoder, pipe_config, conn_state);
	intel_edp_backlight_on(pipe_config, conn_state);
}

static void vlv_enable_dp(struct intel_atomic_state *state,
			  struct intel_encoder *encoder,
			  const struct intel_crtc_state *pipe_config,
			  const struct drm_connector_state *conn_state)
{
	intel_edp_backlight_on(pipe_config, conn_state);
}

static void g4x_pre_enable_dp(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	enum port port = encoder->port;

	intel_dp_prepare(encoder, pipe_config);

	/* Only ilk+ has port A */
	if (port == PORT_A)
		ilk_edp_pll_on(intel_dp, pipe_config);
}

static void vlv_pre_enable_dp(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	vlv_phy_pre_encoder_enable(encoder, pipe_config);

	intel_enable_dp(state, encoder, pipe_config, conn_state);
}

static void vlv_dp_pre_pll_enable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	intel_dp_prepare(encoder, pipe_config);

	vlv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_pre_enable_dp(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	chv_phy_pre_encoder_enable(encoder, pipe_config);

	intel_enable_dp(state, encoder, pipe_config, conn_state);

	/* Second common lane will stay alive on its own now */
	chv_phy_release_cl2_override(encoder);
}

static void chv_dp_pre_pll_enable(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *pipe_config,
				  const struct drm_connector_state *conn_state)
{
	intel_dp_prepare(encoder, pipe_config);

	chv_phy_pre_pll_enable(encoder, pipe_config);
}

static void chv_dp_post_pll_disable(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *old_crtc_state,
				    const struct drm_connector_state *old_conn_state)
{
	chv_phy_post_pll_disable(encoder, old_crtc_state);
}

static u8 intel_dp_voltage_max_2(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
{
	return DP_TRAIN_VOLTAGE_SWING_LEVEL_2;
}

static u8 intel_dp_voltage_max_3(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state)
{
	return DP_TRAIN_VOLTAGE_SWING_LEVEL_3;
}

static u8 intel_dp_preemph_max_2(struct intel_dp *intel_dp)
{
	return DP_TRAIN_PRE_EMPH_LEVEL_2;
}

static u8 intel_dp_preemph_max_3(struct intel_dp *intel_dp)
{
	return DP_TRAIN_PRE_EMPH_LEVEL_3;
}

static void vlv_set_signal_levels(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	unsigned long demph_reg_value, preemph_reg_value,
		uniqtranscale_reg_value;
	u8 train_set = intel_dp->train_set[0];

	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
		preemph_reg_value = 0x0004000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x2B405555;
			uniqtranscale_reg_value = 0x552AB83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x5548B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			demph_reg_value = 0x2B245555;
			uniqtranscale_reg_value = 0x5560B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
			demph_reg_value = 0x2B405555;
			uniqtranscale_reg_value = 0x5598DA3A;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		preemph_reg_value = 0x0002000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x5552B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			demph_reg_value = 0x2B404848;
			uniqtranscale_reg_value = 0x5580B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			demph_reg_value = 0x2B404040;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		preemph_reg_value = 0x0000000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x2B305555;
			uniqtranscale_reg_value = 0x5570B83A;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			demph_reg_value = 0x2B2B4040;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
		preemph_reg_value = 0x0006000;
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			demph_reg_value = 0x1B405555;
			uniqtranscale_reg_value = 0x55ADDA3A;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}

	vlv_set_phy_signal_level(encoder, crtc_state,
				 demph_reg_value, preemph_reg_value,
				 uniqtranscale_reg_value, 0);
}

static void chv_set_signal_levels(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	u32 deemph_reg_value, margin_reg_value;
	bool uniq_trans_scale = false;
	u8 train_set = intel_dp->train_set[0];

	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 128;
			margin_reg_value = 52;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			deemph_reg_value = 128;
			margin_reg_value = 77;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			deemph_reg_value = 128;
			margin_reg_value = 102;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
			deemph_reg_value = 128;
			margin_reg_value = 154;
			uniq_trans_scale = true;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 85;
			margin_reg_value = 78;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			deemph_reg_value = 85;
			margin_reg_value = 116;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
			deemph_reg_value = 85;
			margin_reg_value = 154;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 64;
			margin_reg_value = 104;
			break;
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
			deemph_reg_value = 64;
			margin_reg_value = 154;
			break;
		default:
			return;
		}
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
		switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
		case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
			deemph_reg_value = 43;
			margin_reg_value = 154;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}

	chv_set_phy_signal_level(encoder, crtc_state,
				 deemph_reg_value, margin_reg_value,
				 uniq_trans_scale);
}

static u32 g4x_signal_levels(u8 train_set)
{
	u32 signal_levels = 0;

	switch (train_set & DP_TRAIN_VOLTAGE_SWING_MASK) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0:
	default:
		signal_levels |= DP_VOLTAGE_0_4;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1:
		signal_levels |= DP_VOLTAGE_0_6;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2:
		signal_levels |= DP_VOLTAGE_0_8;
		break;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_3:
		signal_levels |= DP_VOLTAGE_1_2;
		break;
	}
	switch (train_set & DP_TRAIN_PRE_EMPHASIS_MASK) {
	case DP_TRAIN_PRE_EMPH_LEVEL_0:
	default:
		signal_levels |= DP_PRE_EMPHASIS_0;
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_1:
		signal_levels |= DP_PRE_EMPHASIS_3_5;
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_2:
		signal_levels |= DP_PRE_EMPHASIS_6;
		break;
	case DP_TRAIN_PRE_EMPH_LEVEL_3:
		signal_levels |= DP_PRE_EMPHASIS_9_5;
		break;
	}
	return signal_levels;
}

static void
g4x_set_signal_levels(struct intel_encoder *encoder,
		      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	u8 train_set = intel_dp->train_set[0];
	u32 signal_levels;

	signal_levels = g4x_signal_levels(train_set);

	drm_dbg_kms(&dev_priv->drm, "Using signal levels %08x\n",
		    signal_levels);

	intel_dp->DP &= ~(DP_VOLTAGE_MASK | DP_PRE_EMPHASIS_MASK);
	intel_dp->DP |= signal_levels;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

/* SNB CPU eDP voltage swing and pre-emphasis control */
static u32 snb_cpu_edp_signal_levels(u8 train_set)
{
	u8 signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					DP_TRAIN_PRE_EMPHASIS_MASK);

	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_400_600MV_0DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_400MV_3_5DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2:
		return EDP_LINK_TRAIN_400_600MV_6DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_600_800MV_3_5DB_SNB_B;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_3 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_800_1200MV_0DB_SNB_B;
	default:
		MISSING_CASE(signal_levels);
		return EDP_LINK_TRAIN_400_600MV_0DB_SNB_B;
	}
}

static void
snb_cpu_edp_set_signal_levels(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	u8 train_set = intel_dp->train_set[0];
	u32 signal_levels;

	signal_levels = snb_cpu_edp_signal_levels(train_set);

	drm_dbg_kms(&dev_priv->drm, "Using signal levels %08x\n",
		    signal_levels);

	intel_dp->DP &= ~EDP_LINK_TRAIN_VOL_EMP_MASK_SNB;
	intel_dp->DP |= signal_levels;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

/* IVB CPU eDP voltage swing and pre-emphasis control */
static u32 ivb_cpu_edp_signal_levels(u8 train_set)
{
	u8 signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
					DP_TRAIN_PRE_EMPHASIS_MASK);

	switch (signal_levels) {
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_400MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_400MV_3_5DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2:
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2:
		return EDP_LINK_TRAIN_400MV_6DB_IVB;

	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_600MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_600MV_3_5DB_IVB;

	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0:
		return EDP_LINK_TRAIN_800MV_0DB_IVB;
	case DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1:
		return EDP_LINK_TRAIN_800MV_3_5DB_IVB;

	default:
		MISSING_CASE(signal_levels);
		return EDP_LINK_TRAIN_500MV_0DB_IVB;
	}
}

static void
ivb_cpu_edp_set_signal_levels(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	u8 train_set = intel_dp->train_set[0];
	u32 signal_levels;

	signal_levels = ivb_cpu_edp_signal_levels(train_set);

	drm_dbg_kms(&dev_priv->drm, "Using signal levels %08x\n",
		    signal_levels);

	intel_dp->DP &= ~EDP_LINK_TRAIN_VOL_EMP_MASK_IVB;
	intel_dp->DP |= signal_levels;

	intel_de_write(dev_priv, intel_dp->output_reg, intel_dp->DP);
	intel_de_posting_read(dev_priv, intel_dp->output_reg);
}

/*
 * If display is now connected check links status,
 * there has been known issues of link loss triggering
 * long pulse.
 *
 * Some sinks (eg. ASUS PB287Q) seem to perform some
 * weird HPD ping pong during modesets. So we can apparently
 * end up with HPD going low during a modeset, and then
 * going back up soon after. And once that happens we must
 * retrain the link to get a picture. That's in case no
 * userspace component reacted to intermittent HPD dip.
 */
static enum intel_hotplug_state
intel_dp_hotplug(struct intel_encoder *encoder,
		 struct intel_connector *connector)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
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
	 * Keeping it consistent with intel_ddi_hotplug() and
	 * intel_hdmi_hotplug().
	 */
	if (state == INTEL_HOTPLUG_UNCHANGED && !connector->hotplug_retries)
		state = INTEL_HOTPLUG_RETRY;

	return state;
}

static bool ibx_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->hotplug.pch_hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, SDEISR) & bit;
}

static bool g4x_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_B:
		bit = PORTB_HOTPLUG_LIVE_STATUS_G4X;
		break;
	case HPD_PORT_C:
		bit = PORTC_HOTPLUG_LIVE_STATUS_G4X;
		break;
	case HPD_PORT_D:
		bit = PORTD_HOTPLUG_LIVE_STATUS_G4X;
		break;
	default:
		MISSING_CASE(encoder->hpd_pin);
		return false;
	}

	return intel_de_read(dev_priv, PORT_HOTPLUG_STAT) & bit;
}

static bool gm45_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit;

	switch (encoder->hpd_pin) {
	case HPD_PORT_B:
		bit = PORTB_HOTPLUG_LIVE_STATUS_GM45;
		break;
	case HPD_PORT_C:
		bit = PORTC_HOTPLUG_LIVE_STATUS_GM45;
		break;
	case HPD_PORT_D:
		bit = PORTD_HOTPLUG_LIVE_STATUS_GM45;
		break;
	default:
		MISSING_CASE(encoder->hpd_pin);
		return false;
	}

	return intel_de_read(dev_priv, PORT_HOTPLUG_STAT) & bit;
}

static bool ilk_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->hotplug.hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, DEISR) & bit;
}

static void intel_dp_encoder_destroy(struct drm_encoder *encoder)
{
	intel_dp_encoder_flush_work(encoder);

	drm_encoder_cleanup(encoder);
	kfree(enc_to_dig_port(to_intel_encoder(encoder)));
}

enum pipe vlv_active_pipe(struct intel_dp *intel_dp)
{
	struct drm_i915_private *dev_priv = dp_to_i915(intel_dp);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	enum pipe pipe;

	if (g4x_dp_port_enabled(dev_priv, intel_dp->output_reg,
				encoder->port, &pipe))
		return pipe;

	return INVALID_PIPE;
}

static void intel_dp_encoder_reset(struct drm_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(to_intel_encoder(encoder));

	intel_dp->DP = intel_de_read(dev_priv, intel_dp->output_reg);

	intel_dp->reset_link_params = true;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		intel_wakeref_t wakeref;

		with_intel_pps_lock(intel_dp, wakeref)
			intel_dp->pps.active_pipe = vlv_active_pipe(intel_dp);
	}

	intel_pps_encoder_reset(intel_dp);
}

static const struct drm_encoder_funcs intel_dp_enc_funcs = {
	.reset = intel_dp_encoder_reset,
	.destroy = intel_dp_encoder_destroy,
};

bool g4x_dp_init(struct drm_i915_private *dev_priv,
		 i915_reg_t output_reg, enum port port)
{
	struct intel_digital_port *dig_port;
	struct intel_encoder *intel_encoder;
	struct drm_encoder *encoder;
	struct intel_connector *intel_connector;

	dig_port = kzalloc(sizeof(*dig_port), GFP_KERNEL);
	if (!dig_port)
		return false;

	intel_connector = intel_connector_alloc();
	if (!intel_connector)
		goto err_connector_alloc;

	intel_encoder = &dig_port->base;
	encoder = &intel_encoder->base;

	mutex_init(&dig_port->hdcp_mutex);

	if (drm_encoder_init(&dev_priv->drm, &intel_encoder->base,
			     &intel_dp_enc_funcs, DRM_MODE_ENCODER_TMDS,
			     "DP %c", port_name(port)))
		goto err_encoder_init;

	intel_encoder->hotplug = intel_dp_hotplug;
	intel_encoder->compute_config = intel_dp_compute_config;
	intel_encoder->get_hw_state = intel_dp_get_hw_state;
	intel_encoder->get_config = intel_dp_get_config;
	intel_encoder->sync_state = intel_dp_sync_state;
	intel_encoder->initial_fastset_check = intel_dp_initial_fastset_check;
	intel_encoder->update_pipe = intel_backlight_update;
	intel_encoder->suspend = intel_dp_encoder_suspend;
	intel_encoder->shutdown = intel_dp_encoder_shutdown;
	if (IS_CHERRYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = chv_dp_pre_pll_enable;
		intel_encoder->pre_enable = chv_pre_enable_dp;
		intel_encoder->enable = vlv_enable_dp;
		intel_encoder->disable = vlv_disable_dp;
		intel_encoder->post_disable = chv_post_disable_dp;
		intel_encoder->post_pll_disable = chv_dp_post_pll_disable;
	} else if (IS_VALLEYVIEW(dev_priv)) {
		intel_encoder->pre_pll_enable = vlv_dp_pre_pll_enable;
		intel_encoder->pre_enable = vlv_pre_enable_dp;
		intel_encoder->enable = vlv_enable_dp;
		intel_encoder->disable = vlv_disable_dp;
		intel_encoder->post_disable = vlv_post_disable_dp;
	} else {
		intel_encoder->pre_enable = g4x_pre_enable_dp;
		intel_encoder->enable = g4x_enable_dp;
		intel_encoder->disable = g4x_disable_dp;
		intel_encoder->post_disable = g4x_post_disable_dp;
	}

	if ((IS_IVYBRIDGE(dev_priv) && port == PORT_A) ||
	    (HAS_PCH_CPT(dev_priv) && port != PORT_A))
		dig_port->dp.set_link_train = cpt_set_link_train;
	else
		dig_port->dp.set_link_train = g4x_set_link_train;

	if (IS_CHERRYVIEW(dev_priv))
		intel_encoder->set_signal_levels = chv_set_signal_levels;
	else if (IS_VALLEYVIEW(dev_priv))
		intel_encoder->set_signal_levels = vlv_set_signal_levels;
	else if (IS_IVYBRIDGE(dev_priv) && port == PORT_A)
		intel_encoder->set_signal_levels = ivb_cpu_edp_set_signal_levels;
	else if (IS_SANDYBRIDGE(dev_priv) && port == PORT_A)
		intel_encoder->set_signal_levels = snb_cpu_edp_set_signal_levels;
	else
		intel_encoder->set_signal_levels = g4x_set_signal_levels;

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv) ||
	    (HAS_PCH_SPLIT(dev_priv) && port != PORT_A)) {
		dig_port->dp.preemph_max = intel_dp_preemph_max_3;
		dig_port->dp.voltage_max = intel_dp_voltage_max_3;
	} else {
		dig_port->dp.preemph_max = intel_dp_preemph_max_2;
		dig_port->dp.voltage_max = intel_dp_voltage_max_2;
	}

	dig_port->dp.output_reg = output_reg;
	dig_port->max_lanes = 4;

	intel_encoder->type = INTEL_OUTPUT_DP;
	intel_encoder->power_domain = intel_port_to_power_domain(port);
	if (IS_CHERRYVIEW(dev_priv)) {
		if (port == PORT_D)
			intel_encoder->pipe_mask = BIT(PIPE_C);
		else
			intel_encoder->pipe_mask = BIT(PIPE_A) | BIT(PIPE_B);
	} else {
		intel_encoder->pipe_mask = ~0;
	}
	intel_encoder->cloneable = 0;
	intel_encoder->port = port;
	intel_encoder->hpd_pin = intel_hpd_pin_default(dev_priv, port);

	dig_port->hpd_pulse = intel_dp_hpd_pulse;

	if (HAS_GMCH(dev_priv)) {
		if (IS_GM45(dev_priv))
			dig_port->connected = gm45_digital_port_connected;
		else
			dig_port->connected = g4x_digital_port_connected;
	} else {
		if (port == PORT_A)
			dig_port->connected = ilk_digital_port_connected;
		else
			dig_port->connected = ibx_digital_port_connected;
	}

	if (port != PORT_A)
		intel_infoframe_init(dig_port);

	dig_port->aux_ch = intel_bios_port_aux_ch(dev_priv, port);
	if (!intel_dp_init_connector(dig_port, intel_connector))
		goto err_init_connector;

	return true;

err_init_connector:
	drm_encoder_cleanup(encoder);
err_encoder_init:
	kfree(intel_connector);
err_connector_alloc:
	kfree(dig_port);
	return false;
}
