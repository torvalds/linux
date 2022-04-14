// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "g4x_dp.h"
#include "intel_crt.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_fdi.h"
#include "intel_lvds.h"
#include "intel_pch_display.h"
#include "intel_pch_refclk.h"
#include "intel_pps.h"
#include "intel_sdvo.h"

bool intel_has_pch_trancoder(struct drm_i915_private *i915,
			     enum pipe pch_transcoder)
{
	return HAS_PCH_IBX(i915) || HAS_PCH_CPT(i915) ||
		(HAS_PCH_LPT_H(i915) && pch_transcoder == PIPE_A);
}

enum pipe intel_crtc_pch_transcoder(struct intel_crtc *crtc)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (HAS_PCH_LPT(i915))
		return PIPE_A;
	else
		return crtc->pipe;
}

static void assert_pch_dp_disabled(struct drm_i915_private *dev_priv,
				   enum pipe pipe, enum port port,
				   i915_reg_t dp_reg)
{
	enum pipe port_pipe;
	bool state;

	state = g4x_dp_port_enabled(dev_priv, dp_reg, port, &port_pipe);

	I915_STATE_WARN(state && port_pipe == pipe,
			"PCH DP %c enabled on transcoder %c, should be disabled\n",
			port_name(port), pipe_name(pipe));

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv) && !state && port_pipe == PIPE_B,
			"IBX PCH DP %c still using transcoder B\n",
			port_name(port));
}

static void assert_pch_hdmi_disabled(struct drm_i915_private *dev_priv,
				     enum pipe pipe, enum port port,
				     i915_reg_t hdmi_reg)
{
	enum pipe port_pipe;
	bool state;

	state = intel_sdvo_port_enabled(dev_priv, hdmi_reg, &port_pipe);

	I915_STATE_WARN(state && port_pipe == pipe,
			"PCH HDMI %c enabled on transcoder %c, should be disabled\n",
			port_name(port), pipe_name(pipe));

	I915_STATE_WARN(HAS_PCH_IBX(dev_priv) && !state && port_pipe == PIPE_B,
			"IBX PCH HDMI %c still using transcoder B\n",
			port_name(port));
}

static void assert_pch_ports_disabled(struct drm_i915_private *dev_priv,
				      enum pipe pipe)
{
	enum pipe port_pipe;

	assert_pch_dp_disabled(dev_priv, pipe, PORT_B, PCH_DP_B);
	assert_pch_dp_disabled(dev_priv, pipe, PORT_C, PCH_DP_C);
	assert_pch_dp_disabled(dev_priv, pipe, PORT_D, PCH_DP_D);

	I915_STATE_WARN(intel_crt_port_enabled(dev_priv, PCH_ADPA, &port_pipe) &&
			port_pipe == pipe,
			"PCH VGA enabled on transcoder %c, should be disabled\n",
			pipe_name(pipe));

	I915_STATE_WARN(intel_lvds_port_enabled(dev_priv, PCH_LVDS, &port_pipe) &&
			port_pipe == pipe,
			"PCH LVDS enabled on transcoder %c, should be disabled\n",
			pipe_name(pipe));

	/* PCH SDVOB multiplex with HDMIB */
	assert_pch_hdmi_disabled(dev_priv, pipe, PORT_B, PCH_HDMIB);
	assert_pch_hdmi_disabled(dev_priv, pipe, PORT_C, PCH_HDMIC);
	assert_pch_hdmi_disabled(dev_priv, pipe, PORT_D, PCH_HDMID);
}

static void assert_pch_transcoder_disabled(struct drm_i915_private *dev_priv,
					   enum pipe pipe)
{
	u32 val;
	bool enabled;

	val = intel_de_read(dev_priv, PCH_TRANSCONF(pipe));
	enabled = !!(val & TRANS_ENABLE);
	I915_STATE_WARN(enabled,
			"transcoder assertion failed, should be off on pipe %c but is still active\n",
			pipe_name(pipe));
}

static void ibx_sanitize_pch_hdmi_port(struct drm_i915_private *dev_priv,
				       enum port port, i915_reg_t hdmi_reg)
{
	u32 val = intel_de_read(dev_priv, hdmi_reg);

	if (val & SDVO_ENABLE ||
	    (val & SDVO_PIPE_SEL_MASK) == SDVO_PIPE_SEL(PIPE_A))
		return;

	drm_dbg_kms(&dev_priv->drm,
		    "Sanitizing transcoder select for HDMI %c\n",
		    port_name(port));

	val &= ~SDVO_PIPE_SEL_MASK;
	val |= SDVO_PIPE_SEL(PIPE_A);

	intel_de_write(dev_priv, hdmi_reg, val);
}

static void ibx_sanitize_pch_dp_port(struct drm_i915_private *dev_priv,
				     enum port port, i915_reg_t dp_reg)
{
	u32 val = intel_de_read(dev_priv, dp_reg);

	if (val & DP_PORT_EN ||
	    (val & DP_PIPE_SEL_MASK) == DP_PIPE_SEL(PIPE_A))
		return;

	drm_dbg_kms(&dev_priv->drm,
		    "Sanitizing transcoder select for DP %c\n",
		    port_name(port));

	val &= ~DP_PIPE_SEL_MASK;
	val |= DP_PIPE_SEL(PIPE_A);

	intel_de_write(dev_priv, dp_reg, val);
}

static void ibx_sanitize_pch_ports(struct drm_i915_private *dev_priv)
{
	/*
	 * The BIOS may select transcoder B on some of the PCH
	 * ports even it doesn't enable the port. This would trip
	 * assert_pch_dp_disabled() and assert_pch_hdmi_disabled().
	 * Sanitize the transcoder select bits to prevent that. We
	 * assume that the BIOS never actually enabled the port,
	 * because if it did we'd actually have to toggle the port
	 * on and back off to make the transcoder A select stick
	 * (see. intel_dp_link_down(), intel_disable_hdmi(),
	 * intel_disable_sdvo()).
	 */
	ibx_sanitize_pch_dp_port(dev_priv, PORT_B, PCH_DP_B);
	ibx_sanitize_pch_dp_port(dev_priv, PORT_C, PCH_DP_C);
	ibx_sanitize_pch_dp_port(dev_priv, PORT_D, PCH_DP_D);

	/* PCH SDVOB multiplex with HDMIB */
	ibx_sanitize_pch_hdmi_port(dev_priv, PORT_B, PCH_HDMIB);
	ibx_sanitize_pch_hdmi_port(dev_priv, PORT_C, PCH_HDMIC);
	ibx_sanitize_pch_hdmi_port(dev_priv, PORT_D, PCH_HDMID);
}

static void intel_pch_transcoder_set_m1_n1(struct intel_crtc *crtc,
					   const struct intel_link_m_n *m_n)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_set_m_n(dev_priv, m_n,
		      PCH_TRANS_DATA_M1(pipe), PCH_TRANS_DATA_N1(pipe),
		      PCH_TRANS_LINK_M1(pipe), PCH_TRANS_LINK_N1(pipe));
}

static void intel_pch_transcoder_set_m2_n2(struct intel_crtc *crtc,
					   const struct intel_link_m_n *m_n)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_set_m_n(dev_priv, m_n,
		      PCH_TRANS_DATA_M2(pipe), PCH_TRANS_DATA_N2(pipe),
		      PCH_TRANS_LINK_M2(pipe), PCH_TRANS_LINK_N2(pipe));
}

void intel_pch_transcoder_get_m1_n1(struct intel_crtc *crtc,
				    struct intel_link_m_n *m_n)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_get_m_n(dev_priv, m_n,
		      PCH_TRANS_DATA_M1(pipe), PCH_TRANS_DATA_N1(pipe),
		      PCH_TRANS_LINK_M1(pipe), PCH_TRANS_LINK_N1(pipe));
}

void intel_pch_transcoder_get_m2_n2(struct intel_crtc *crtc,
				    struct intel_link_m_n *m_n)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	intel_get_m_n(dev_priv, m_n,
		      PCH_TRANS_DATA_M2(pipe), PCH_TRANS_DATA_N2(pipe),
		      PCH_TRANS_LINK_M2(pipe), PCH_TRANS_LINK_N2(pipe));
}

static void ilk_pch_transcoder_set_timings(const struct intel_crtc_state *crtc_state,
					   enum pipe pch_transcoder)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	intel_de_write(dev_priv, PCH_TRANS_HTOTAL(pch_transcoder),
		       intel_de_read(dev_priv, HTOTAL(cpu_transcoder)));
	intel_de_write(dev_priv, PCH_TRANS_HBLANK(pch_transcoder),
		       intel_de_read(dev_priv, HBLANK(cpu_transcoder)));
	intel_de_write(dev_priv, PCH_TRANS_HSYNC(pch_transcoder),
		       intel_de_read(dev_priv, HSYNC(cpu_transcoder)));

	intel_de_write(dev_priv, PCH_TRANS_VTOTAL(pch_transcoder),
		       intel_de_read(dev_priv, VTOTAL(cpu_transcoder)));
	intel_de_write(dev_priv, PCH_TRANS_VBLANK(pch_transcoder),
		       intel_de_read(dev_priv, VBLANK(cpu_transcoder)));
	intel_de_write(dev_priv, PCH_TRANS_VSYNC(pch_transcoder),
		       intel_de_read(dev_priv, VSYNC(cpu_transcoder)));
	intel_de_write(dev_priv, PCH_TRANS_VSYNCSHIFT(pch_transcoder),
		       intel_de_read(dev_priv, VSYNCSHIFT(cpu_transcoder)));
}

static void ilk_enable_pch_transcoder(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 val, pipeconf_val;

	/* Make sure PCH DPLL is enabled */
	assert_shared_dpll_enabled(dev_priv, crtc_state->shared_dpll);

	/* FDI must be feeding us bits for PCH ports */
	assert_fdi_tx_enabled(dev_priv, pipe);
	assert_fdi_rx_enabled(dev_priv, pipe);

	if (HAS_PCH_CPT(dev_priv)) {
		reg = TRANS_CHICKEN2(pipe);
		val = intel_de_read(dev_priv, reg);
		/*
		 * Workaround: Set the timing override bit
		 * before enabling the pch transcoder.
		 */
		val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
		/* Configure frame start delay to match the CPU */
		val &= ~TRANS_CHICKEN2_FRAME_START_DELAY_MASK;
		val |= TRANS_CHICKEN2_FRAME_START_DELAY(crtc_state->framestart_delay - 1);
		intel_de_write(dev_priv, reg, val);
	}

	reg = PCH_TRANSCONF(pipe);
	val = intel_de_read(dev_priv, reg);
	pipeconf_val = intel_de_read(dev_priv, PIPECONF(pipe));

	if (HAS_PCH_IBX(dev_priv)) {
		/* Configure frame start delay to match the CPU */
		val &= ~TRANS_FRAME_START_DELAY_MASK;
		val |= TRANS_FRAME_START_DELAY(crtc_state->framestart_delay - 1);

		/*
		 * Make the BPC in transcoder be consistent with
		 * that in pipeconf reg. For HDMI we must use 8bpc
		 * here for both 8bpc and 12bpc.
		 */
		val &= ~PIPECONF_BPC_MASK;
		if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
			val |= PIPECONF_BPC_8;
		else
			val |= pipeconf_val & PIPECONF_BPC_MASK;
	}

	val &= ~TRANS_INTERLACE_MASK;
	if ((pipeconf_val & PIPECONF_INTERLACE_MASK_ILK) == PIPECONF_INTERLACE_IF_ID_ILK) {
		if (HAS_PCH_IBX(dev_priv) &&
		    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_SDVO))
			val |= TRANS_INTERLACE_LEGACY_VSYNC_IBX;
		else
			val |= TRANS_INTERLACE_INTERLACED;
	} else {
		val |= TRANS_INTERLACE_PROGRESSIVE;
	}

	intel_de_write(dev_priv, reg, val | TRANS_ENABLE);
	if (intel_de_wait_for_set(dev_priv, reg, TRANS_STATE_ENABLE, 100))
		drm_err(&dev_priv->drm, "failed to enable transcoder %c\n",
			pipe_name(pipe));
}

static void ilk_disable_pch_transcoder(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 val;

	/* FDI relies on the transcoder */
	assert_fdi_tx_disabled(dev_priv, pipe);
	assert_fdi_rx_disabled(dev_priv, pipe);

	/* Ports must be off as well */
	assert_pch_ports_disabled(dev_priv, pipe);

	reg = PCH_TRANSCONF(pipe);
	val = intel_de_read(dev_priv, reg);
	val &= ~TRANS_ENABLE;
	intel_de_write(dev_priv, reg, val);
	/* wait for PCH transcoder off, transcoder state */
	if (intel_de_wait_for_clear(dev_priv, reg, TRANS_STATE_ENABLE, 50))
		drm_err(&dev_priv->drm, "failed to disable transcoder %c\n",
			pipe_name(pipe));

	if (HAS_PCH_CPT(dev_priv)) {
		/* Workaround: Clear the timing override chicken bit again. */
		reg = TRANS_CHICKEN2(pipe);
		val = intel_de_read(dev_priv, reg);
		val &= ~TRANS_CHICKEN2_TIMING_OVERRIDE;
		intel_de_write(dev_priv, reg, val);
	}
}

void ilk_pch_pre_enable(struct intel_atomic_state *state,
			struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	/*
	 * Note: FDI PLL enabling _must_ be done before we enable the
	 * cpu pipes, hence this is separate from all the other fdi/pch
	 * enabling.
	 */
	ilk_fdi_pll_enable(crtc_state);
}

/*
 * Enable PCH resources required for PCH ports:
 *   - PCH PLLs
 *   - FDI training & RX/TX
 *   - update transcoder timings
 *   - DP transcoding bits
 *   - transcoder
 */
void ilk_pch_enable(struct intel_atomic_state *state,
		    struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;
	u32 temp;

	assert_pch_transcoder_disabled(dev_priv, pipe);

	/* For PCH output, training FDI link */
	intel_fdi_link_train(crtc, crtc_state);

	/*
	 * We need to program the right clock selection
	 * before writing the pixel multiplier into the DPLL.
	 */
	if (HAS_PCH_CPT(dev_priv)) {
		u32 sel;

		temp = intel_de_read(dev_priv, PCH_DPLL_SEL);
		temp |= TRANS_DPLL_ENABLE(pipe);
		sel = TRANS_DPLLB_SEL(pipe);
		if (crtc_state->shared_dpll ==
		    intel_get_shared_dpll_by_id(dev_priv, DPLL_ID_PCH_PLL_B))
			temp |= sel;
		else
			temp &= ~sel;
		intel_de_write(dev_priv, PCH_DPLL_SEL, temp);
	}

	/*
	 * XXX: pch pll's can be enabled any time before we enable the PCH
	 * transcoder, and we actually should do this to not upset any PCH
	 * transcoder that already use the clock when we share it.
	 *
	 * Note that enable_shared_dpll tries to do the right thing, but
	 * get_shared_dpll unconditionally resets the pll - we need that
	 * to have the right LVDS enable sequence.
	 */
	intel_enable_shared_dpll(crtc_state);

	/* set transcoder timing, panel must allow it */
	assert_pps_unlocked(dev_priv, pipe);
	if (intel_crtc_has_dp_encoder(crtc_state)) {
		intel_pch_transcoder_set_m1_n1(crtc, &crtc_state->dp_m_n);
		intel_pch_transcoder_set_m2_n2(crtc, &crtc_state->dp_m2_n2);
	}
	ilk_pch_transcoder_set_timings(crtc_state, pipe);

	intel_fdi_normal_train(crtc);

	/* For PCH DP, enable TRANS_DP_CTL */
	if (HAS_PCH_CPT(dev_priv) &&
	    intel_crtc_has_dp_encoder(crtc_state)) {
		const struct drm_display_mode *adjusted_mode =
			&crtc_state->hw.adjusted_mode;
		u32 bpc = (intel_de_read(dev_priv, PIPECONF(pipe)) & PIPECONF_BPC_MASK) >> 5;
		i915_reg_t reg = TRANS_DP_CTL(pipe);
		enum port port;

		temp = intel_de_read(dev_priv, reg);
		temp &= ~(TRANS_DP_PORT_SEL_MASK |
			  TRANS_DP_VSYNC_ACTIVE_HIGH |
			  TRANS_DP_HSYNC_ACTIVE_HIGH |
			  TRANS_DP_BPC_MASK);
		temp |= TRANS_DP_OUTPUT_ENABLE;
		temp |= bpc << 9; /* same format but at 11:9 */

		if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
			temp |= TRANS_DP_HSYNC_ACTIVE_HIGH;
		if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
			temp |= TRANS_DP_VSYNC_ACTIVE_HIGH;

		port = intel_get_crtc_new_encoder(state, crtc_state)->port;
		drm_WARN_ON(&dev_priv->drm, port < PORT_B || port > PORT_D);
		temp |= TRANS_DP_PORT_SEL(port);

		intel_de_write(dev_priv, reg, temp);
	}

	ilk_enable_pch_transcoder(crtc_state);
}

void ilk_pch_disable(struct intel_atomic_state *state,
		     struct intel_crtc *crtc)
{
	ilk_fdi_disable(crtc);
}

void ilk_pch_post_disable(struct intel_atomic_state *state,
			  struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	ilk_disable_pch_transcoder(crtc);

	if (HAS_PCH_CPT(dev_priv)) {
		i915_reg_t reg;
		u32 temp;

		/* disable TRANS_DP_CTL */
		reg = TRANS_DP_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~(TRANS_DP_OUTPUT_ENABLE |
			  TRANS_DP_PORT_SEL_MASK);
		temp |= TRANS_DP_PORT_SEL_NONE;
		intel_de_write(dev_priv, reg, temp);

		/* disable DPLL_SEL */
		temp = intel_de_read(dev_priv, PCH_DPLL_SEL);
		temp &= ~(TRANS_DPLL_ENABLE(pipe) | TRANS_DPLLB_SEL(pipe));
		intel_de_write(dev_priv, PCH_DPLL_SEL, temp);
	}

	ilk_fdi_pll_disable(crtc);
}

static void ilk_pch_clock_get(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	/* read out port_clock from the DPLL */
	i9xx_crtc_clock_get(crtc, crtc_state);

	/*
	 * In case there is an active pipe without active ports,
	 * we may need some idea for the dotclock anyway.
	 * Calculate one based on the FDI configuration.
	 */
	crtc_state->hw.adjusted_mode.crtc_clock =
		intel_dotclock_calculate(intel_fdi_link_freq(dev_priv, crtc_state),
					 &crtc_state->fdi_m_n);
}

void ilk_pch_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll;
	enum pipe pipe = crtc->pipe;
	enum intel_dpll_id pll_id;
	bool pll_active;
	u32 tmp;

	if ((intel_de_read(dev_priv, PCH_TRANSCONF(pipe)) & TRANS_ENABLE) == 0)
		return;

	crtc_state->has_pch_encoder = true;

	tmp = intel_de_read(dev_priv, FDI_RX_CTL(pipe));
	crtc_state->fdi_lanes = ((FDI_DP_PORT_WIDTH_MASK & tmp) >>
				 FDI_DP_PORT_WIDTH_SHIFT) + 1;

	intel_cpu_transcoder_get_m1_n1(crtc, crtc_state->cpu_transcoder,
				       &crtc_state->fdi_m_n);

	if (HAS_PCH_IBX(dev_priv)) {
		/*
		 * The pipe->pch transcoder and pch transcoder->pll
		 * mapping is fixed.
		 */
		pll_id = (enum intel_dpll_id) pipe;
	} else {
		tmp = intel_de_read(dev_priv, PCH_DPLL_SEL);
		if (tmp & TRANS_DPLLB_SEL(pipe))
			pll_id = DPLL_ID_PCH_PLL_B;
		else
			pll_id = DPLL_ID_PCH_PLL_A;
	}

	crtc_state->shared_dpll = intel_get_shared_dpll_by_id(dev_priv, pll_id);
	pll = crtc_state->shared_dpll;

	pll_active = intel_dpll_get_hw_state(dev_priv, pll,
					     &crtc_state->dpll_hw_state);
	drm_WARN_ON(&dev_priv->drm, !pll_active);

	tmp = crtc_state->dpll_hw_state.dpll;
	crtc_state->pixel_multiplier =
		((tmp & PLL_REF_SDVO_HDMI_MULTIPLIER_MASK)
		 >> PLL_REF_SDVO_HDMI_MULTIPLIER_SHIFT) + 1;

	ilk_pch_clock_get(crtc_state);
}

static void lpt_enable_pch_transcoder(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val, pipeconf_val;

	/* FDI must be feeding us bits for PCH ports */
	assert_fdi_tx_enabled(dev_priv, (enum pipe) cpu_transcoder);
	assert_fdi_rx_enabled(dev_priv, PIPE_A);

	val = intel_de_read(dev_priv, TRANS_CHICKEN2(PIPE_A));
	/* Workaround: set timing override bit. */
	val |= TRANS_CHICKEN2_TIMING_OVERRIDE;
	/* Configure frame start delay to match the CPU */
	val &= ~TRANS_CHICKEN2_FRAME_START_DELAY_MASK;
	val |= TRANS_CHICKEN2_FRAME_START_DELAY(crtc_state->framestart_delay - 1);
	intel_de_write(dev_priv, TRANS_CHICKEN2(PIPE_A), val);

	val = TRANS_ENABLE;
	pipeconf_val = intel_de_read(dev_priv, PIPECONF(cpu_transcoder));

	if ((pipeconf_val & PIPECONF_INTERLACE_MASK_HSW) == PIPECONF_INTERLACE_IF_ID_ILK)
		val |= TRANS_INTERLACE_INTERLACED;
	else
		val |= TRANS_INTERLACE_PROGRESSIVE;

	intel_de_write(dev_priv, LPT_TRANSCONF, val);
	if (intel_de_wait_for_set(dev_priv, LPT_TRANSCONF,
				  TRANS_STATE_ENABLE, 100))
		drm_err(&dev_priv->drm, "Failed to enable PCH transcoder\n");
}

static void lpt_disable_pch_transcoder(struct drm_i915_private *dev_priv)
{
	u32 val;

	val = intel_de_read(dev_priv, LPT_TRANSCONF);
	val &= ~TRANS_ENABLE;
	intel_de_write(dev_priv, LPT_TRANSCONF, val);
	/* wait for PCH transcoder off, transcoder state */
	if (intel_de_wait_for_clear(dev_priv, LPT_TRANSCONF,
				    TRANS_STATE_ENABLE, 50))
		drm_err(&dev_priv->drm, "Failed to disable PCH transcoder\n");

	/* Workaround: clear timing override bit. */
	val = intel_de_read(dev_priv, TRANS_CHICKEN2(PIPE_A));
	val &= ~TRANS_CHICKEN2_TIMING_OVERRIDE;
	intel_de_write(dev_priv, TRANS_CHICKEN2(PIPE_A), val);
}

void lpt_pch_enable(struct intel_atomic_state *state,
		    struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	assert_pch_transcoder_disabled(dev_priv, PIPE_A);

	lpt_program_iclkip(crtc_state);

	/* Set transcoder timing. */
	ilk_pch_transcoder_set_timings(crtc_state, PIPE_A);

	lpt_enable_pch_transcoder(crtc_state);
}

void lpt_pch_disable(struct intel_atomic_state *state,
		     struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	lpt_disable_pch_transcoder(dev_priv);

	lpt_disable_iclkip(dev_priv);
}

void lpt_pch_get_config(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 tmp;

	if ((intel_de_read(dev_priv, LPT_TRANSCONF) & TRANS_ENABLE) == 0)
		return;

	crtc_state->has_pch_encoder = true;

	tmp = intel_de_read(dev_priv, FDI_RX_CTL(PIPE_A));
	crtc_state->fdi_lanes = ((FDI_DP_PORT_WIDTH_MASK & tmp) >>
				 FDI_DP_PORT_WIDTH_SHIFT) + 1;

	intel_cpu_transcoder_get_m1_n1(crtc, crtc_state->cpu_transcoder,
				       &crtc_state->fdi_m_n);

	crtc_state->hw.adjusted_mode.crtc_clock = lpt_get_iclkip(dev_priv);
}

void intel_pch_sanitize(struct drm_i915_private *i915)
{
	if (HAS_PCH_IBX(i915))
		ibx_sanitize_pch_ports(i915);
}
