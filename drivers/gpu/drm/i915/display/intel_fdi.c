// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/string_helpers.h>

#include "i915_reg.h"
#include "intel_atomic.h"
#include "intel_crtc.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_fdi.h"

struct intel_fdi_funcs {
	void (*fdi_link_train)(struct intel_crtc *crtc,
			       const struct intel_crtc_state *crtc_state);
};

static void assert_fdi_tx(struct drm_i915_private *dev_priv,
			  enum pipe pipe, bool state)
{
	bool cur_state;

	if (HAS_DDI(dev_priv)) {
		/*
		 * DDI does not have a specific FDI_TX register.
		 *
		 * FDI is never fed from EDP transcoder
		 * so pipe->transcoder cast is fine here.
		 */
		enum transcoder cpu_transcoder = (enum transcoder)pipe;
		cur_state = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder)) & TRANS_DDI_FUNC_ENABLE;
	} else {
		cur_state = intel_de_read(dev_priv, FDI_TX_CTL(pipe)) & FDI_TX_ENABLE;
	}
	I915_STATE_WARN(cur_state != state,
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
	bool cur_state;

	cur_state = intel_de_read(dev_priv, FDI_RX_CTL(pipe)) & FDI_RX_ENABLE;
	I915_STATE_WARN(cur_state != state,
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
	bool cur_state;

	/* ILK FDI PLL is always enabled */
	if (IS_IRONLAKE(i915))
		return;

	/* On Haswell, DDI ports are responsible for the FDI PLL setup */
	if (HAS_DDI(i915))
		return;

	cur_state = intel_de_read(i915, FDI_TX_CTL(pipe)) & FDI_TX_PLL_ENABLE;
	I915_STATE_WARN(!cur_state, "FDI TX PLL assertion failure, should be active but is disabled\n");
}

static void assert_fdi_rx_pll(struct drm_i915_private *i915,
			      enum pipe pipe, bool state)
{
	bool cur_state;

	cur_state = intel_de_read(i915, FDI_RX_CTL(pipe)) & FDI_RX_PLL_ENABLE;
	I915_STATE_WARN(cur_state != state,
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

/* units of 100MHz */
static int pipe_required_fdi_lanes(struct intel_crtc_state *crtc_state)
{
	if (crtc_state->hw.enable && crtc_state->has_pch_encoder)
		return crtc_state->fdi_lanes;

	return 0;
}

static int ilk_check_fdi_lanes(struct drm_device *dev, enum pipe pipe,
			       struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_atomic_state *state = pipe_config->uapi.state;
	struct intel_crtc *other_crtc;
	struct intel_crtc_state *other_crtc_state;

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

		other_crtc = intel_crtc_for_pipe(dev_priv, PIPE_C);
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

		other_crtc = intel_crtc_for_pipe(dev_priv, PIPE_B);
		other_crtc_state =
			intel_atomic_get_crtc_state(state, other_crtc);
		if (IS_ERR(other_crtc_state))
			return PTR_ERR(other_crtc_state);

		if (pipe_required_fdi_lanes(other_crtc_state) > 2) {
			drm_dbg_kms(&dev_priv->drm,
				    "fdi link B uses too many lanes to enable link C\n");
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

int ilk_fdi_compute_config(struct intel_crtc *crtc,
			   struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	const struct drm_display_mode *adjusted_mode = &pipe_config->hw.adjusted_mode;
	int lane, link_bw, fdi_dotclock, ret;
	bool needs_recompute = false;

retry:
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

	intel_link_compute_m_n(pipe_config->pipe_bpp, lane, fdi_dotclock,
			       link_bw, &pipe_config->fdi_m_n, false);

	ret = ilk_check_fdi_lanes(dev, crtc->pipe, pipe_config);
	if (ret == -EDEADLK)
		return ret;

	if (ret == -EINVAL && pipe_config->pipe_bpp > 6*3) {
		pipe_config->pipe_bpp -= 2*3;
		drm_dbg_kms(&i915->drm,
			    "fdi link bw constraint, reducing pipe bpp to %i\n",
			    pipe_config->pipe_bpp);
		needs_recompute = true;
		pipe_config->bw_constrained = true;

		goto retry;
	}

	if (needs_recompute)
		return -EAGAIN;

	return ret;
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
		intel_de_write(dev_priv, reg,
			       intel_de_read(dev_priv, reg) | FDI_FS_ERRC_ENABLE | FDI_FE_ERRC_ENABLE);
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
		       intel_de_read(dev_priv, PIPE_DATA_M1(pipe)) & TU_SIZE_MASK);

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
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_2;
	intel_de_write(dev_priv, reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_2;
	intel_de_write(dev_priv, reg, temp);

	intel_de_posting_read(dev_priv, reg);
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
		       intel_de_read(dev_priv, PIPE_DATA_M1(pipe)) & TU_SIZE_MASK);

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
		reg = FDI_TX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		temp |= snb_b_fdi_train_param[i];
		intel_de_write(dev_priv, reg, temp);

		intel_de_posting_read(dev_priv, reg);
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
		reg = FDI_TX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		temp |= snb_b_fdi_train_param[i];
		intel_de_write(dev_priv, reg, temp);

		intel_de_posting_read(dev_priv, reg);
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
		       intel_de_read(dev_priv, PIPE_DATA_M1(pipe)) & TU_SIZE_MASK);

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
		reg = FDI_TX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~FDI_LINK_TRAIN_NONE_IVB;
		temp |= FDI_LINK_TRAIN_PATTERN_2_IVB;
		intel_de_write(dev_priv, reg, temp);

		reg = FDI_RX_CTL(pipe);
		temp = intel_de_read(dev_priv, reg);
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_2_CPT;
		intel_de_write(dev_priv, reg, temp);

		intel_de_posting_read(dev_priv, reg);
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
		       FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2) | FDI_RX_TP1_TO_TP2_48 | FDI_RX_FDI_DELAY_90);

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
			       DDI_BUF_CTL_ENABLE | ((crtc_state->fdi_lanes - 1) << 1) | DDI_BUF_TRANS_SELECT(i / 2));
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
		temp = intel_de_read(dev_priv, FDI_RX_MISC(PIPE_A));
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A), temp);
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

		temp = intel_de_read(dev_priv, DDI_BUF_CTL(PORT_E));
		temp &= ~DDI_BUF_CTL_ENABLE;
		intel_de_write(dev_priv, DDI_BUF_CTL(PORT_E), temp);
		intel_de_posting_read(dev_priv, DDI_BUF_CTL(PORT_E));

		/* Disable DP_TP_CTL and FDI_RX_CTL and retry */
		temp = intel_de_read(dev_priv, DP_TP_CTL(PORT_E));
		temp &= ~(DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_MASK);
		temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
		intel_de_write(dev_priv, DP_TP_CTL(PORT_E), temp);
		intel_de_posting_read(dev_priv, DP_TP_CTL(PORT_E));

		intel_wait_ddi_buf_idle(dev_priv, PORT_E);

		/* Reset FDI_RX_MISC pwrdn lanes */
		temp = intel_de_read(dev_priv, FDI_RX_MISC(PIPE_A));
		temp &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
		temp |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
		intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A), temp);
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
	u32 val;

	/*
	 * Bspec lists this as both step 13 (before DDI_BUF_CTL disable)
	 * and step 18 (after clearing PORT_CLK_SEL). Based on a BUN,
	 * step 13 is the correct place for it. Step 18 is where it was
	 * originally before the BUN.
	 */
	val = intel_de_read(dev_priv, FDI_RX_CTL(PIPE_A));
	val &= ~FDI_RX_ENABLE;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), val);

	val = intel_de_read(dev_priv, DDI_BUF_CTL(PORT_E));
	val &= ~DDI_BUF_CTL_ENABLE;
	intel_de_write(dev_priv, DDI_BUF_CTL(PORT_E), val);

	intel_wait_ddi_buf_idle(dev_priv, PORT_E);

	intel_ddi_disable_clock(encoder);

	val = intel_de_read(dev_priv, FDI_RX_MISC(PIPE_A));
	val &= ~(FDI_RX_PWRDN_LANE1_MASK | FDI_RX_PWRDN_LANE0_MASK);
	val |= FDI_RX_PWRDN_LANE1_VAL(2) | FDI_RX_PWRDN_LANE0_VAL(2);
	intel_de_write(dev_priv, FDI_RX_MISC(PIPE_A), val);

	val = intel_de_read(dev_priv, FDI_RX_CTL(PIPE_A));
	val &= ~FDI_PCDCLK;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), val);

	val = intel_de_read(dev_priv, FDI_RX_CTL(PIPE_A));
	val &= ~FDI_RX_PLL_ENABLE;
	intel_de_write(dev_priv, FDI_RX_CTL(PIPE_A), val);
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
	temp |= (intel_de_read(dev_priv, PIPECONF(pipe)) & PIPECONF_BPC_MASK) << 11;
	intel_de_write(dev_priv, reg, temp | FDI_RX_PLL_ENABLE);

	intel_de_posting_read(dev_priv, reg);
	udelay(200);

	/* Switch from Rawclk to PCDclk */
	temp = intel_de_read(dev_priv, reg);
	intel_de_write(dev_priv, reg, temp | FDI_PCDCLK);

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
	i915_reg_t reg;
	u32 temp;

	/* Switch from PCDclk to Rawclk */
	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	intel_de_write(dev_priv, reg, temp & ~FDI_PCDCLK);

	/* Disable CPU FDI TX PLL */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	intel_de_write(dev_priv, reg, temp & ~FDI_TX_PLL_ENABLE);

	intel_de_posting_read(dev_priv, reg);
	udelay(100);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	intel_de_write(dev_priv, reg, temp & ~FDI_RX_PLL_ENABLE);

	/* Wait for the clocks to turn off. */
	intel_de_posting_read(dev_priv, reg);
	udelay(100);
}

void ilk_fdi_disable(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	i915_reg_t reg;
	u32 temp;

	/* disable CPU FDI tx and PCH FDI rx */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	intel_de_write(dev_priv, reg, temp & ~FDI_TX_ENABLE);
	intel_de_posting_read(dev_priv, reg);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~(0x7 << 16);
	temp |= (intel_de_read(dev_priv, PIPECONF(pipe)) & PIPECONF_BPC_MASK) << 11;
	intel_de_write(dev_priv, reg, temp & ~FDI_RX_ENABLE);

	intel_de_posting_read(dev_priv, reg);
	udelay(100);

	/* Ironlake workaround, disable clock pointer after downing FDI */
	if (HAS_PCH_IBX(dev_priv))
		intel_de_write(dev_priv, FDI_RX_CHICKEN(pipe),
			       FDI_RX_PHASE_SYNC_POINTER_OVR);

	/* still set train pattern 1 */
	reg = FDI_TX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	intel_de_write(dev_priv, reg, temp);

	reg = FDI_RX_CTL(pipe);
	temp = intel_de_read(dev_priv, reg);
	if (HAS_PCH_CPT(dev_priv)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
	}
	/* BPC in FDI rx is consistent with that in PIPECONF */
	temp &= ~(0x07 << 16);
	temp |= (intel_de_read(dev_priv, PIPECONF(pipe)) & PIPECONF_BPC_MASK) << 11;
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
