// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/string_helpers.h>

#include <drm/drm_fixed.h>

#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_dp.h"
#include "intel_display_types.h"
#include "intel_fdi.h"
#include "intel_fdi_regs.h"
#include "intel_link_bw.h"

struct intel_fdi_funcs {
	void (*fdi_link_train)(struct intel_crtc *crtc,
			       const struct intel_crtc_state *crtc_state);
};

static void assert_fdi_tx(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	struct intel_display *display = &dev_priv->display;
	bool cur_state;

	if (HAS_DDI(display)) {
		/*
		 * DDI does not have a specific FDI_TX register.
		 *
		 * FDI is never fed from EDP transcoder
		 * so pipe->transcoder cast is fine here.
		 */
		enum transcoder cpu_transcoder = (enum transcoder)pipe;
		cur_state = intel_de_read(display,
					  TRANS_DDI_FUNC_CTL(display, cpu_transcoder)) & TRANS_DDI_FUNC_ENABLE;
	} else {
		cur_state = intel_de_read(display, FDI_TX_CTL(pipe)) & FDI_TX_ENABLE;
	}
	INTEL_DISPLAY_STATE_WARN(display, cur_state != state,
				 "FDI TX state assertion failure (expected %s, current %s)\n",
				 str_on_off(state), str_on_off(cur_state));
}

void assert_fdi_tx_enabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_fdi_tx(i915, pipe, true);
}

void assert_fdi_tx_disabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_fdi_tx(i915, pipe, false);
}

static void assert_fdi_rx(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	struct intel_display *display = &dev_priv->display;
	bool cur_state;

	cur_state = intel_de_read(display, FDI_RX_CTL(pipe)) & FDI_RX_ENABLE;
	INTEL_DISPLAY_STATE_WARN(display, cur_state != state,
				 "FDI RX state assertion failure (expected %s, current %s)\n",
				 str_on_off(state), str_on_off(cur_state));
}

void assert_fdi_rx_enabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_fdi_rx(i915, pipe, true);
}

void assert_fdi_rx_disabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_fdi_rx(i915, pipe, false);
}

void assert_fdi_tx_pll_enabled(struct drm_i915_private *i915,
			       enum pipe pipe)
{
	struct intel_display *display = &i915->display;
	bool cur_state;

	/* ILK FDI PLL is always enabled */
	if (IS_IRONLAKE(i915))
		return;

	/* On Haswell, DDI ports are responsible for the FDI PLL setup */
	if (HAS_DDI(display))
		return;

	cur_state = intel_de_read(display, FDI_TX_CTL(pipe)) & FDI_TX_PLL_ENABLE;
	INTEL_DISPLAY_STATE_WARN(display, !cur_state,
				 "FDI TX PLL assertion failure, should be active but is disabled\n");
}

static void assert_fdi_rx_pll(struct drm_i915_private *i915,
			      enum pipe pipe, bool state)
{
	struct intel_display *display = &i915->display;
	bool cur_state;

	cur_state = intel_de_read(display, FDI_RX_CTL(pipe)) & FDI_RX_PLL_ENABLE;
	INTEL_DISPLAY_STATE_WARN(display, cur_state != state,
				 "FDI RX PLL assertion failure (expected %s, current %s)\n",
				 str_on_off(state), str_on_off(cur_state));
}

void assert_fdi_rx_pll_enabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_fdi_rx_pll(i915, pipe, true);
}

void assert_fdi_rx_pll_disabled(struct drm_i915_private *i915, enum pipe pipe)
{
	assert_fdi_rx_pll(i915, pipe, false);
}

void intel_fdi_link_train(struct intel_crtc *crtc,
			  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	dev_priv->display.funcs.fdi->fdi_link_train(crtc, crtc_state);
}

/**
 * intel_fdi_add_affected_crtcs - add CRTCs on FDI affected by other modeset CRTCs
 * @state: intel atomic state
 *
 * Add a CRTC using FDI to @state if changing another CRTC's FDI BW usage is
 * known to affect the available FDI BW for the former CRTC. In practice this
 * means adding CRTC B on IVYBRIDGE if its use of FDI lanes is limited (by
 * CRTC C) and CRTC C is getting disabled.
 *
 * Returns 0 in case of success, or a negative error code otherwise.
 */
int intel_fdi_add_affected_crtcs(struct intel_atomic_state *state)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	const struct intel_crtc_state *old_crtc_state;
	const struct intel_crtc_state *new_crtc_state;
	struct intel_crtc *crtc;

	if (!IS_IVYBRIDGE(i915) || INTEL_NUM_PIPES(i915) != 3)
		return 0;

	crtc = intel_crtc_for_pipe(display, PIPE_C);
	new_crtc_state = intel_atomic_get_new_crtc_state(state, crtc);
	if (!new_crtc_state)
		return 0;

	if (!intel_crtc_needs_modeset(new_crtc_state))
		return 0;

	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);
	if (!old_crtc_state->fdi_lanes)
		return 0;

	crtc = intel_crtc_for_pipe(display, PIPE_B);
	new_crtc_state = intel_atomic_get_crtc_state(&state->base, crtc);
	if (IS_ERR(new_crtc_state))
		return PTR_ERR(new_crtc_state);

	old_crtc_state = intel_atomic_get_old_crtc_state(state, crtc);
	if (!old_crtc_state->fdi_lanes)
		return 0;

	return intel_modeset_pipes_in_mask_early(state,
						 "FDI link BW decrease on pipe C",
						 BIT(PIPE_B));
}

/* units of 100MHz */
static int pipe_required_fdi_lanes(struct intel_crtc_state *crtc_state)
{
	if (crtc_state->hw.enable && crtc_state->has_pch_encoder)
		return crtc_state->fdi_lanes;

	return 0;
}

static int ilk_check_fdi_lanes(struct drm_device *dev, enum pipe pipe,
			       struct intel_crtc_state *pipe_config,
			       enum pipe *pipe_to_reduce)
{
	struct intel_display *display = to_intel_display(dev);
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_atomic_state *state = pipe_config->uapi.state;
	struct intel_crtc *other_crtc;
	struct intel_crtc_state *other_crtc_state;

	*pipe_to_reduce = pipe;

	drm_dbg_kms(&dev_priv->drm,
		    "checking fdi config on pipe %c, lanes %i\n",
		    pipe_name(pipe), pipe_config->fdi_lanes);
	if (pipe_config->fdi_lanes > 4) {
		drm_dbg_kms(&dev_priv->drm,
			    "invalid fdi lane config on pipe %c: %i lanes\n",
			    pipe_name(pipe), pipe_config->fdi_lanes);
		return -EINVAL;
	}

	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		if (pipe_config->fdi_lanes > 2) {
			drm_dbg_kms(&dev_priv->drm,
				    "only 2 lanes on haswell, required: %i lanes\n",
				    pipe_config->fdi_lanes);
			return -EINVAL;
		} else {
			return 0;
		}
	}

	if (INTEL_NUM_PIPES(dev_priv) == 2)
		return 0;

	/* Ivybridge 3 pipe is really complicated */
	switch (pipe) {
	case PIPE_A:
		return 0;
	case PIPE_B:
		if (pipe_config->fdi_lanes <= 2)
			return 0;

		other_crtc = intel_crtc_for_pipe(display, PIPE_C);
		other_crtc_state =
			intel_atomic_get_crtc_state(state, other_crtc);
		if (IS_ERR(other_crtc_state))
			return PTR_ERR(other_crtc_state);

		if (pipe_required_fdi_lanes(other_crtc_state) > 0) {
			drm_dbg_kms(&dev_priv->drm,
				    "invalid shared fdi lane config on pipe %c: %i lanes\n",
				    pipe_name(pipe), pipe_config->fdi_lanes);
			return -EINVAL;
		}
		return 0;
	case PIPE_C:
		if (pipe_config->fdi_lanes > 2) {
			drm_dbg_kms(&dev_priv->drm,
				    "only 2 lanes on pipe %c: required %i lanes\n",
				    pipe_name(pipe), pipe_config->fdi_lanes);
			return -EINVAL;
		}

		other_crtc = intel_crtc_for_pipe(display, PIPE_B);
		other_crtc_state =
			intel_atomic_get_crtc_state(state, other_crtc);
		if (IS_ERR(other_crtc_state))
			return PTR_ERR(other_crtc_state);

		if (pipe_required_fdi_lanes(other_crtc_state) > 2) {
			drm_dbg_kms(&dev_priv->drm,
				    "fdi link B uses too many lanes to enable link C\n");

			*pipe_to_reduce = PIPE_B;

			return -EINVAL;
		}
		return 0;
	default:
		MISSING_CASE(pipe);
		return 0;
	}
}

void intel_fdi_pll_freq_update(struct drm_i915_private *i915)
{
	if (IS_IRONLAKE(i915)) {
		u32 fdi_pll_clk =
			intel_de_read(i915, FDI_PLL_BIOS_0) & FDI_PLL_FB_CLOCK_MASK;

		i915->display.fdi.pll_freq = (fdi_pll_clk + 2) * 10000;
	} else if (IS_SANDYBRIDGE(i915) || IS_IVYBRIDGE(i915)) {
		i915->display.fdi.pll_freq = 270000;
	} else {
		return;
	}

	drm_dbg(&i915->drm, "FDI PLL freq=%d\n", i915->display.fdi.pll_freq);
}

int intel_fdi_link_freq(struct drm_i915_private *i915,
			const struct intel_crtc_state *pipe_config)
{
	if (HAS_DDI(i915))
		return pipe_config->port_clock; /* SPLL */
	else
		return i915->display.fdi.pll_freq;
}

/**
 * intel_fdi_compute_pipe_bpp - compute pipe bpp limited by max link bpp
 * @crtc_state: the crtc state
 *
 * Compute the pipe bpp limited by the CRTC's maximum link bpp. Encoders can
 * call this function during state computation in the simple case where the
 * link bpp will always match the pipe bpp. This is the case for all non-DP
 * encoders, while DP encoders will use a link bpp lower than pipe bpp in case
 * of DSC compression.
 *
 * Returns %true in case of success, %false if pipe bpp would need to be
 * reduced below its valid range.
 */
bool intel_fdi_compute_pipe_bpp(struct intel_crtc_state *crtc_state)
{
	int pipe_bpp = min(crtc_state->pipe_bpp,
			   fxp_q4_to_int(crtc_state->max_link_bpp_x16));

	pipe_bpp = rounddown(pipe_bpp, 2 * 3);

	if (pipe_bpp < 6 * 3)
		return false;

	crtc_state->pipe_bpp = pipe_bpp;

	return true;
}

int ilk_fdi_compute_config(struct intel_crtc *crtc,
			   struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	int lane, link_bw, fdi_dotclock;

	/* FDI is a binary signal running at ~2.7GHz, encoding
	 * each output octet as 10 bits. The actual frequency
	 * is stored as a divider into a 100MHz clock, and the
	 * mode pixel clock is stored in units of 1KHz.
	 * Hence the bw of each lane in terms of the mode signal
	 * is:
	 */
	link_bw = intel_fdi_link_freq(i915, pipe_config);

	fdi_dotclock = adjusted_mode->crtc_clock;

	lane = ilk_get_lanes_required(fdi_dotclock, link_bw,
				      pipe_config->pipe_bpp);

	pipe_config->fdi_lanes = lane;

	intel_link_compute_m_n(fxp_q4_from_int(pipe_config->pipe_bpp),
			       lane, fdi_dotclock,
			       link_bw,
			       intel_dp_bw_fec_overhead(false),
			       &pipe_config->fdi_m_n);

	return 0;
}

static int intel_fdi_atomic_check_bw(struct intel_atomic_state *state,
				     struct intel_crtc *crtc,
				     struct intel_crtc_state *pipe_config,
				     struct intel_link_bw_limits *limits)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe_to_reduce;
	int ret;

	ret = ilk_check_fdi_lanes(&i915->drm, crtc->pipe, pipe_config,
				  &pipe_to_reduce);
	if (ret != -EINVAL)
		return ret;

	ret = intel_link_bw_reduce_bpp(state, limits,
				       BIT(pipe_to_reduce),
				       "FDI link BW");

	return ret ? : -EAGAIN;
}

/**
 * intel_fdi_atomic_check_link - check all modeset FDI link configuration
 * @state: intel atomic state
 * @limits: link BW limits
 *
 * Check the link configuration for all modeset FDI outputs. If the
 * configuration is invalid @limits will be updated if possible to
 * reduce the total BW, after which the configuration for all CRTCs in
 * @state must be recomputed with the updated @limits.
 *
 * Returns:
 *   - 0 if the confugration is valid
 *   - %-EAGAIN, if the configuration is invalid and @limits got updated
 *     with fallback values with which the configuration of all CRTCs
 *     in @state must be recomputed
 *   - Other negative error, if the configuration is invalid without a
 *     fallback possibility, or the check failed for another reason
 */
int intel_fdi_atomic_check_link(struct intel_atomic_state *state,
				struct intel_link_bw_limits *limits)
{
	struct intel_crtc *crtc;
	struct intel_crtc_state *crtc_state;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		int ret;

		if (!crtc_state->has_pch_encoder ||
		    !intel_crtc_needs_modeset(crtc_state) ||
		    !crtc_state->hw.enable)
			continue;

		ret = intel_fdi_atomic_check_bw(state, crtc, crtc_state, limits);
		if (ret)
			return ret;
	}

	return 0;
}

static void cpt_set_fdi_bc_bifurcation(struct drm_i915_private *dev_priv, bool enable)
{
	u32 temp;

	temp = intel_de_read(dev_priv, SOUTH_CHICKEN1);
	if (!!(temp & FDI_BC_BIFURCATION_SELECT) == enable)
		return;

	drm_WARN_ON(&dev_priv->drm,
		    intel_de_read(dev_priv, FDI_RX_CTL(PIPE_B)) &
		    FDI_RX_ENABLE);
	drm_WARN_ON(&dev_priv->drm,
		    intel_de_read(dev_priv, FDI_RX_CTL(PIPE_C)) &
		    FDI_RX_ENABLE);

	temp &= ~FDI_BC_BIFURCATION_SELECT;
	if (enable)
		temp |= FDI_BC_BIFURCATION_SELECT;

	drm_dbg_kms(&dev_priv->drm, "%sabling fdi C rx\n",
		    enable ? "en" : "dis");
	intel_de_write(dev_priv, SOUTH_CHICKEN1, temp);
	intel_de_posting_read(dev_priv, SOUTH_CHICKEN1);
}

static void ivb_update_fdi_bc_bifurcation(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	switch (crtc->pipe) {
	case PIPE_A:
		break;
	case PIPE_B:
		if (crtc_state->fdi_lanes > 2)
			cpt_set_fdi_bc_bifurcation(dev_priv, false);
		else
			cpt_set_fdi_bc_bifurcation(dev_priv, true);

		break;
	case PIPE_C:
		cpt_set_fdi_bc_bifurcation(dev_priv, true);

		break;
	default:
		MISSING_CASE(crtc->pipe);
	}
}

void intel_fdi_normal_train(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* enable normal train */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	if (IS_IVYBRIDGE(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_NONE_IVB;
		temp |= FDI_LINK_TRAIN_NONE_IVB | FDI_TX_ENHANCE_FRAME_ENABLE;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_NONE | FDI_TX_ENHANCE_FRAME_ENABLE;
	}
	intel_de_write(dev_priv, reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_NORMAL_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_NONE;
	}
	intel_de_write(dev_priv, reg, temp | FDI_RX_ENHANCE_FRAME_ENABLE);

	/* wait one idle pattern time */
	intel_de_posting_read(dev_priv, reg);
	udelay(1000);

	/* IVB wants error correction enabled */
	if (IS_IVYBRIDGE(dev_priv))
		intel_de_rmw(dev_priv, reg, 0, FDI_FS_ERRC_ENABLE | FDI_FE_ERRC_ENABLE);
}

/* The FDI link training functions for ILK/Ibexpeak. */
static void ilk_fdi_link_train(struct intel_crtc *crtc,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 temp, tries;

	/*
	 * Write the TU size bits before fdi link training, so that error
	 * detection works.
	 */
	intel_de_write(dev_priv, FDI_RX_TUSIZE1(pipe),
		       intel_de_read(dev_priv, PIPE_DATA_M1(dev_priv, pipe)) & TU_SIZE_MASK);

	/* FDI needs bits from pipe first */
	assert_transcoder_enabled(dev_priv, crtc_state->cpu_transcoder);

	/* Train 1: umask FDI RX Interrupt symbol_lock and bit_lock bit
	   for train result */
	reg = FDI_RX_IMR(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_RX_SYMBOL_LOCK;
	temp &= ~FDI_RX_BIT_LOCK;
	intel_de_write(dev_priv, reg, temp);
	intel_de_read(dev_priv, reg);
	udelay(150);

	/* enable CPU FDI TX and PCH FDI RX */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_DP_PORT_WIDTH_MASK;
	temp |= FDI_DP_PORT_WIDTH(crtc_state->fdi_lanes);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	intel_de_write(dev_priv, reg, temp | FDI_TX_ENABLE);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	intel_de_write(dev_priv, reg, temp | FDI_RX_ENABLE);

	intel_de_posting_read(dev_priv, reg);
	udelay(150);

	/* Ironlake workaround, enable clock pointer after FDI enable*/
	intel_de_write(dev_priv, FDI_RX_CHICKEN(pipe),
		       FDI_RX_PHASE_SYNC_POINTER_OVR);
	intel_de_write(dev_priv, FDI_RX_CHICKEN(pipe),
		       FDI_RX_PHASE_SYNC_POINTER_OVR | FDI_RX_PHASE_SYNC_POINTER_EN);

	reg = FDI_RX_IIR(pipe);
	for (tries = 0; tries < 5; tries++) {
		temp = intel_de_read(dev_priv, reg);
		drm_dbg_kms(&dev_priv->drm, "FDI_RX_IIR 0x%x\n", temp);

		if ((temp & FDI_RX_BIT_LOCK)) {
			drm_dbg_kms(&dev_priv->drm, "FDI train 1 done.\n");
			intel_de_write(dev_priv, reg, temp | FDI_RX_BIT_LOCK);
			break;
		}
	}
	if (tries == 5)
		drm_err(&dev_priv->drm, "FDI train 1 fail!\n");

	/* Train 2 */
	intel_de_rmw(dev_priv, FDI_TX_CTL(pipe),
		     FDI_LINK_TRAIN_NONE, FDI_LINK_TRAIN_PATTERN_2);
	intel_de_rmw(dev_priv, FDI_RX_CTL(pipe),
		     FDI_LINK_TRAIN_NONE, FDI_LINK_TRAIN_PATTERN_2);
	intel_de_posting_read(dev_priv, FDI_RX_CTL(pipe));
	udelay(150);

	reg = FDI_RX_IIR(pipe);
	for (tries = 0; tries < 5; tries++) {
		temp = intel_de_read(dev_priv, reg);
		drm_dbg_kms(&dev_priv->drm, "FDI_RX_IIR 0x%x\n", temp);

		if (temp & FDI_RX_SYMBOL_LOCK) {
			intel_de_write(dev_priv, reg,
				       temp | FDI_RX_SYMBOL_LOCK);
			drm_dbg_kms(&dev_priv->drm, "FDI train 2 done.\n");
			break;
		}
	}
	if (tries == 5)
		drm_err(&dev_priv->drm, "FDI train 2 fail!\n");

	drm_dbg_kms(&dev_priv->drm, "FDI train done\n");

}

static const int snb_b_fdi_train_param[] = {
	FDI_LINK_TRAIN_400MV_0DB_SNB_B,
	FDI_LINK_TRAIN_400MV_6DB_SNB_B,
	FDI_LINK_TRAIN_600MV_3_5DB_SNB_B,
	FDI_LINK_TRAIN_800MV_0DB_SNB_B,
};

/* The FDI link training functions for SNB/Cougarpoint. */
static void gen6_fdi_link_train(struct intel_crtc *crtc,
				const struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 temp, i, retry;

	/*
	 * Write the TU size bits before fdi link training, so that error
	 * detection works.
	 */
	intel_de_write(dev_priv, FDI_RX_TUSIZE1(pipe),
		       intel_de_read(dev_priv, PIPE_DATA_M1(dev_priv, pipe)) & TU_SIZE_MASK);

	/* Train 1: umask FDI RX Interrupt symbol_lock and bit_lock bit
	   for train result */
	reg = FDI_RX_IMR(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_RX_SYMBOL_LOCK;
	temp &= ~FDI_RX_BIT_LOCK;
	intel_de_write(dev_priv, reg, temp);

	intel_de_posting_read(dev_priv, reg);
	udelay(150);

	/* enable CPU FDI TX and PCH FDI RX */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_DP_PORT_WIDTH_MASK;
	temp |= FDI_DP_PORT_WIDTH(crtc_state->fdi_lanes);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
	/* SNB-B */
	temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	intel_de_write(dev_priv, reg, temp | FDI_TX_ENABLE);

	intel_de_write(dev_priv, FDI_RX_MISC(pipe),
		       FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
	}
	intel_de_write(dev_priv, reg, temp | FDI_RX_ENABLE);

	intel_de_posting_read(dev_priv, reg);
	udelay(150);

	for (i = 0; i < 4; i++) {
		intel_de_rmw(dev_priv, FDI_TX_CTL(pipe),
			     FDI_LINK_TRAIN_VOL_EMP_MASK, snb_b_fdi_train_param[i]);
		intel_de_posting_read(dev_priv, FDI_TX_CTL(pipe));
		udelay(500);

		for (retry = 0; retry < 5; retry++) {
			reg = FDI_RX_IIR(pipe);
			temp = intel_de_read(dev_priv, reg);
			drm_dbg_kms(&dev_priv->drm, "FDI_RX_IIR 0x%x\n", temp);
			if (temp & FDI_RX_BIT_LOCK) {
				intel_de_write(dev_priv, reg,
					       temp | FDI_RX_BIT_LOCK);
				drm_dbg_kms(&dev_priv->drm,
					    "FDI train 1 done.\n");
				break;
			}
			udelay(50);
		}
		if (retry < 5)
			break;
	}
	if (i == 4)
		drm_err(&dev_priv->drm, "FDI train 1 fail!\n");

	/* Train 2 */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_2;
	if (IS_SANDYBRIDGE(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		/* SNB-B */
		temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	}
	intel_de_write(dev_priv, reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_2_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_2;
	}
	intel_de_write(dev_priv, reg, temp);

	intel_de_posting_read(dev_priv, reg);
	udelay(150);

	for (i = 0; i < 4; i++) {
		intel_de_rmw(dev_priv, FDI_TX_CTL(pipe),
			     FDI_LINK_TRAIN_VOL_EMP_MASK, snb_b_fdi_train_param[i]);
		intel_de_posting_read(dev_priv, FDI_TX_CTL(pipe));
		udelay(500);

		for (retry = 0; retry < 5; retry++) {
			reg = FDI_RX_IIR(pipe);
			temp = intel_de_read(dev_priv, reg);
			drm_dbg_kms(&dev_priv->drm, "FDI_RX_IIR 0x%x\n", temp);
			if (temp & FDI_RX_SYMBOL_LOCK) {
				intel_de_write(dev_priv, reg,
					       temp | FDI_RX_SYMBOL_LOCK);
				drm_dbg_kms(&dev_priv->drm,
					    "FDI train 2 done.\n");
				break;
			}
			udelay(50);
		}
		if (retry < 5)
			break;
	}
	if (i == 4)
		drm_err(&dev_priv->drm, "FDI train 2 fail!\n");

	drm_dbg_kms(&dev_priv->drm, "FDI train done.\n");
}

/* Manual link training for Ivy Bridge A0 parts */
static void ivb_manual_fdi_link_train(struct intel_crtc *crtc,
				      const struct intel_crtc_state *crtc_state)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 temp, i, j;

	ivb_update_fdi_bc_bifurcation(crtc_state);

	/*
	 * Write the TU size bits before fdi link training, so that error
	 * detection works.
	 */
	intel_de_write(dev_priv, FDI_RX_TUSIZE1(pipe),
		       intel_de_read(dev_priv, PIPE_DATA_M1(dev_priv, pipe)) & TU_SIZE_MASK);

	/* Train 1: umask FDI RX Interrupt symbol_lock and bit_lock bit
	   for train result */
	reg = FDI_RX_IMR(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_RX_SYMBOL_LOCK;
	temp &= ~FDI_RX_BIT_LOCK;
	intel_de_write(dev_priv, reg, temp);

	intel_de_posting_read(dev_priv, reg);
	udelay(150);

	drm_dbg_kms(&dev_priv->drm, "FDI_RX_IIR before link train 0x%x\n",
		    intel_de_read(dev_priv, FDI_RX_IIR(pipe)));

	/* Try each vswing and preemphasis setting twice before moving on */
	for (j = 0; j < ARRAY_SIZE(snb_b_fdi_train_param) * 2; j++) {
		/* disable first in case we need to retry */
		reg = FDI_TX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~(FDI_LINK_TRAIN_AUTO | FDI_LINK_TRAIN_NONE_IVB);
		temp &= ~FDI_TX_ENABLE;
		intel_de_write(dev_priv, reg, temp);

		reg = FDI_RX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~FDI_LINK_TRAIN_AUTO;
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp &= ~FDI_RX_ENABLE;
		intel_de_write(dev_priv, reg, temp);

		/* enable CPU FDI TX and PCH FDI RX */
		reg = FDI_TX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~FDI_DP_PORT_WIDTH_MASK;
		temp |= FDI_DP_PORT_WIDTH(crtc_state->fdi_lanes);
		temp |= FDI_LINK_TRAIN_PATTERN_1_IVB;
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		temp |= snb_b_fdi_train_param[j/2];
		temp |= FDI_COMPOSITE_SYNC;
		intel_de_write(dev_priv, reg, temp | FDI_TX_ENABLE);

		intel_de_write(dev_priv, FDI_RX_MISC(pipe),
			       FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

		reg = FDI_RX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
		temp |= FDI_COMPOSITE_SYNC;
		intel_de_write(dev_priv, reg, temp | FDI_RX_ENABLE);

		intel_de_posting_read(dev_priv, reg);
		udelay(1); /* should be 0.5us */

		for (i = 0; i < 4; i++) {
			reg = FDI_RX_IIR(pipe);
			temp = intel_de_read(dev_priv, reg);
			drm_dbg_kms(&dev_priv->drm, "FDI_RX_IIR 0x%x\n", temp);

			if (temp & FDI_RX_BIT_LOCK ||
			    (intel_de_read(dev_priv, reg) & FDI_RX_BIT_LOCK)) {
				intel_de_write(dev_priv, reg,
					       temp | FDI_RX_BIT_LOCK);
				drm_dbg_kms(&dev_priv->drm,
					    "FDI train 1 done, level %i.\n",
					    i);
				break;
			}
			udelay(1); /* should be 0.5us */
		}
		if (i == 4) {
			drm_dbg_kms(&dev_priv->drm,
				    "FDI train 1 fail on vswing %d\n", j / 2);
			continue;
		}

		/* Train 2 */
		intel_de_rmw(dev_priv, FDI_TX_CTL(pipe),
			     FDI_LINK_TRAIN_NONE_IVB,
			     FDI_LINK_TRAIN_PATTERN_2_IVB);
		intel_de_rmw(dev_priv, FDI_RX_CTL(pipe),
			     FDI_LINK_TRAIN_PATTERN_MASK_CPT,
			     FDI_LINK_TRAIN_PATTERN_2_CPT);
		intel_de_posting_read(dev_priv, FDI_RX_CTL(pipe));
		udelay(2); /* should be 1.5us */

		for (i = 0; i < 4; i++) {
			reg = FDI_RX_IIR(pipe);
			temp = intel_de_read(dev_priv, reg);
			drm_dbg_kms(&dev_priv->drm, "FDI_RX_IIR 0x%x\n", temp);

			if (temp & FDI_RX_SYMBOL_LOCK ||
			    (intel_de_read(dev_priv, reg) & FDI_RX_SYMBOL_LOCK)) {
				intel_de_write(dev_priv, reg,
					       temp | FDI_RX_SYMBOL_LOCK);
				drm_dbg_kms(&dev_priv->drm,
					    "FDI train 2 done, level %i.\n",
					    i);
				goto train_done;
			}
			udelay(2); /* should be 1.5us */
		}
		if (i == 4)
			drm_dbg_kms(&dev_priv->drm,
				    "FDI train 2 fail on vswing %d\n", j / 2);
	}

train_done:
	drm_dbg_kms(&dev_priv->drm, "FDI train done.\n");
}

/* Starting with Haswell, different DDI ports can work in FDI mode for
 * connection to the PCH-located connectors. For this, it is necessary to train
 * both the DDI port and PCH receiver for the desired DDI buffer settings.
 *
 * The recommended port to work in FDI mode is DDI E, which we use here. Also,
 * please note that when FDI mode is active on DDI E, it shares 2 lines with
 * DDI A (which is used for eDP)
 */
void hsw_fdi_link_train(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 temp, i, rx_ctl_val;
	int n_entries;

	encoder->get_buf_trans(encoder, crtc_state, &n_entries);

	hsw_prepare_dp_ddi_buffers(encoder, crtc_state);

	/* Set the FDI_RX_MISC pwrdn lanes and the 2 workarounds listed at the
	 * mode set "sequence for CRT port" document:
	 * - TP1 to TP2 time with the default value
	 * - FDI delay to 90h
	 *
	 * WaFDIAutoLinkSetTimingOverrride:hsw
	 */
	intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A),
		       FDI_RX_PWRDN_LANE1_VAL(2) |
		       FDI_RX_PWRDN_LANE0_VAL(2) |
		       FDI_RX_TP1_TO_TP2_48 |
		       FDI_RX_FDI_DELAY_90);

	/* Enable the PCH Receiver FDI PLL */
	rx_ctl_val = dev_priv->display.fdi.rx_config | FDI_RX_ENHANCE_FRAME_ENABLE |
		     FDI_RX_PLL_ENABLE |
		     FDI_DP_PORT_WIDTH(crtc_state->fdi_lanes);
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);
	intel_de_posting_read(dev_priv, FDI_RX_CTL(PIPE_A));
	udelay(220);

	/* Switch from Rawclk to PCDclk */
	rx_ctl_val |= FDI_PCDCLK;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);

	/* Configure Port Clock Select */
	drm_WARN_ON(&dev_priv->drm, crtc_state->shared_dpll->info->id != DPLL_ID_SPLL);
	intel_ddi_enable_clock(encoder, crtc_state);

	/* Start the training iterating through available voltages and emphasis,
	 * testing each value twice. */
	for (i = 0; i < n_entries * 2; i++) {
		/* Configure DP_TP_CTL with auto-training */
		intel_de_write(dev_priv, DP_TP_CTL(PORT_E),
			       DP_TP_CTL_FDI_AUTOTRAIN |
			       DP_TP_CTL_ENHANCED_FRAME_ENABLE |
			       DP_TP_CTL_LINK_TRAIN_PAT1 |
			       DP_TP_CTL_ENABLE);

		/* Configure and enable DDI_BUF_CTL for DDI E with next voltage.
		 * DDI E does not support port reversal, the functionality is
		 * achieved on the PCH side in FDI_RX_CTL, so no need to set the
		 * port reversal bit */
		intel_de_write(dev_priv, DDI_BUF_CTL(PORT_E),
			       DDI_BUF_CTL_ENABLE |
			       ((crtc_state->fdi_lanes - 1) << 1) |
			       DDI_BUF_TRANS_SELECT(i / 2));
		intel_de_posting_read(dev_priv, DDI_BUF_CTL(PORT_E));

		udelay(600);

		/* Program PCH FDI Receiver TU */
		intel_de_write(dev_priv, FDI_RX_TUSIZE1(PIPE_A), TU_SIZE(64));

		/* Enable PCH FDI Receiver with auto-training */
		rx_ctl_val |= FDI_RX_ENABLE | FDI_LINK_TRAIN_AUTO;
		intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);
		intel_de_posting_read(dev_priv, FDI_RX_CTL(PIPE_A));

		/* Wait for FDI receiver lane calibration */
		udelay(30);

		/* Unset FDI_RX_MISC pwrdn lanes */
		intel_de_rmw(dev_priv, FDI_RX_MISC(PIPE_A),
			     FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK, 0);
		intel_de_posting_read(dev_priv, FDI_RX_MISC(PIPE_A));

		/* Wait for FDI auto training time */
		udelay(5);

		temp = intel_de_read(dev_priv, DP_TP_STATUS(PORT_E));
		if (temp & DP_TP_STATUS_AUTOTRAIN_DONE) {
			drm_dbg_kms(&dev_priv->drm,
				    "FDI link training done on step %d\n", i);
			break;
		}

		/*
		 * Leave things enabled even if we failed to train FDI.
		 * Results in less fireworks from the state checker.
		 */
		if (i == n_entries * 2 - 1) {
			drm_err(&dev_priv->drm, "FDI link training failed!\n");
			break;
		}

		rx_ctl_val &= ~FDI_RX_ENABLE;
		intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), rx_ctl_val);
		intel_de_posting_read(dev_priv, FDI_RX_CTL(PIPE_A));

		intel_de_rmw(dev_priv, DDI_BUF_CTL(PORT_E), DDI_BUF_CTL_ENABLE, 0);
		intel_de_posting_read(dev_priv, DDI_BUF_CTL(PORT_E));

		/* Disable DP_TP_CTL and FDI_RX_CTL and retry */
		intel_de_rmw(dev_priv, DP_TP_CTL(PORT_E), DP_TP_CTL_ENABLE, 0);
		intel_de_posting_read(dev_priv, DP_TP_CTL(PORT_E));

		intel_wait_ddi_buf_idle(dev_priv, PORT_E);

		/* Reset FDI_RX_MISC pwrdn lanes */
		intel_de_rmw(dev_priv, FDI_RX_MISC(PIPE_A),
			     FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK,
			     FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2));
		intel_de_posting_read(dev_priv, FDI_RX_MISC(PIPE_A));
	}

	/* Enable normal pixel sending for FDI */
	intel_de_write(dev_priv, DP_TP_CTL(PORT_E),
		       DP_TP_CTL_FDI_AUTOTRAIN |
		       DP_TP_CTL_LINK_TRAIN_NORMAL |
		       DP_TP_CTL_ENHANCED_FRAME_ENABLE |
		       DP_TP_CTL_ENABLE);
}

void hsw_fdi_disable(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	/*
	 * Bspec lists this as both step 13 (before DDI_BUF_CTL disable)
	 * and step 18 (after clearing PORT_CLK_SEL). Based on a BUN,
	 * step 13 is the correct place for it. Step 18 is where it was
	 * originally before the BUN.
	 */
	intel_de_rmw(dev_priv, FDI_RX_CTL(PIPE_A), FDI_RX_ENABLE, 0);
	intel_de_rmw(dev_priv, DDI_BUF_CTL(PORT_E), DDI_BUF_CTL_ENABLE, 0);
	intel_wait_ddi_buf_idle(dev_priv, PORT_E);
	intel_ddi_disable_clock(encoder);
	intel_de_rmw(dev_priv, FDI_RX_MISC(PIPE_A),
		     FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK,
		     FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2));
	intel_de_rmw(dev_priv, FDI_RX_CTL(PIPE_A), FDI_PCDCLK, 0);
	intel_de_rmw(dev_priv, FDI_RX_CTL(PIPE_A), FDI_RX_PLL_ENABLE, 0);
}

void ilk_fdi_pll_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* enable PCH FDI RX PLL, wait warmup plus DMI latency */
	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~(FDI_DP_PORT_WIDTH_MASK | (0x7 << 16));
	temp |= FDI_DP_PORT_WIDTH(crtc_state->fdi_lanes);
	temp |= (intel_de_read(dev_priv, TRANSCONF(dev_priv, pipe)) & TRANSCONF_BPC_MASK) << 11;
	intel_de_write(dev_priv, reg, temp | FDI_RX_PLL_ENABLE);

	intel_de_posting_read(dev_priv, reg);
	udelay(200);

	/* Switch from Rawclk to PCDclk */
	intel_de_rmw(dev_priv, reg, 0, FDI_PCDCLK);
	intel_de_posting_read(dev_priv, reg);
	udelay(200);

	/* Enable CPU FDI TX PLL, always on for Ironlake */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	if ((temp & FDI_TX_PLL_ENABLE) == 0) {
		intel_de_write(dev_priv, reg, temp | FDI_TX_PLL_ENABLE);

		intel_de_posting_read(dev_priv, reg);
		udelay(100);
	}
}

void ilk_fdi_pll_disable(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;

	/* Switch from PCDclk to Rawclk */
	intel_de_rmw(dev_priv, FDI_RX_CTL(pipe), FDI_PCDCLK, 0);

	/* Disable CPU FDI TX PLL */
	intel_de_rmw(dev_priv, FDI_TX_CTL(pipe), FDI_TX_PLL_ENABLE, 0);
	intel_de_posting_read(dev_priv, FDI_TX_CTL(pipe));
	udelay(100);

	/* Wait for the clocks to turn off. */
	intel_de_rmw(dev_priv, FDI_RX_CTL(pipe), FDI_RX_PLL_ENABLE, 0);
	intel_de_posting_read(dev_priv, FDI_RX_CTL(pipe));
	udelay(100);
}

void ilk_fdi_disable(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* disable CPU FDI tx and PCH FDI rx */
	intel_de_rmw(dev_priv, FDI_TX_CTL(pipe), FDI_TX_ENABLE, 0);
	intel_de_posting_read(dev_priv, FDI_TX_CTL(pipe));

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~(0x7 << 16);
	temp |= (intel_de_read(dev_priv, TRANSCONF(dev_priv, pipe)) & TRANSCONF_BPC_MASK) << 11;
	intel_de_write(dev_priv, reg, temp & ~FDI_RX_ENABLE);

	intel_de_posting_read(dev_priv, reg);
	udelay(100);

	/* Ironlake workaround, disable clock pointer after downing FDI */
	if (HAS_PCH_IBX(dev_priv))
		intel_de_write(dev_priv, FDI_RX_CHICKEN(pipe),
			       FDI_RX_PHASE_SYNC_POINTER_OVR);

	/* still set train pattern 1 */
	intel_de_rmw(dev_priv, FDI_TX_CTL(pipe),
		     FDI_LINK_TRAIN_NONE, FDI_LINK_TRAIN_PATTERN_1);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
	}
	/* BPC in FDI rx is consistent with that in TRANSCONF */
	temp &= ~(0x07 << 16);
	temp |= (intel_de_read(dev_priv, TRANSCONF(dev_priv, pipe)) & TRANSCONF_BPC_MASK) << 11;
	intel_de_write(dev_priv, reg, temp);

	intel_de_posting_read(dev_priv, reg);
	udelay(100);
}

static const struct intel_fdi_funcs ilk_funcs = {
	.fdi_link_train = ilk_fdi_link_train,
};

static const struct intel_fdi_funcs gen6_funcs = {
	.fdi_link_train = gen6_fdi_link_train,
};

static const struct intel_fdi_funcs ivb_funcs = {
	.fdi_link_train = ivb_manual_fdi_link_train,
};

void
intel_fdi_init_hook(struct drm_i915_private *dev_priv)
{
	if (IS_IRONLAKE(dev_priv)) {
		dev_priv->display.funcs.fdi = &ilk_funcs;
	} else if (IS_SANDYBRIDGE(dev_priv)) {
		dev_priv->display.funcs.fdi = &gen6_funcs;
	} else if (IS_IVYBRIDGE(dev_priv)) {
		/* FIXME: detect B0+ stepping and use auto training */
		dev_priv->display.funcs.fdi = &ivb_funcs;
	}
}
