// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include "intel_atomic.h"
#include "intel_display_types.h"
#include "intel_fdi.h"

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

		other_crtc = intel_get_crtc_for_pipe(dev_priv, PIPE_C);
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

		other_crtc = intel_get_crtc_for_pipe(dev_priv, PIPE_B);
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
		BUG();
	}
}

int ilk_fdi_compute_config(struct intel_crtc *intel_crtc,
				  struct intel_crtc_state *pipe_config)
{
	struct drm_device *dev = intel_crtc->base.dev;
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
			       link_bw, &pipe_config->fdi_m_n, false, false);

	ret = ilk_check_fdi_lanes(dev, intel_crtc->pipe, pipe_config);
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
		return I915_DISPLAY_CONFIG_RETRY;

	return ret;
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

	/* FDI needs bits from pipe first */
	assert_pipe_enabled(dev_priv, crtc_state->cpu_transcoder);

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
	if (IS_GEN(dev_priv, 6)) {
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

void ilk_fdi_pll_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	enum pipe pipe = intel_crtc->pipe;
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

void ilk_fdi_pll_disable(struct intel_crtc *intel_crtc)
{
	struct drm_device *dev = intel_crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = intel_crtc->pipe;
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

void
intel_fdi_init_hook(struct drm_i915_private *dev_priv)
{
	if (IS_GEN(dev_priv, 5)) {
		dev_priv->display.fdi_link_train = ilk_fdi_link_train;
	} else if (IS_GEN(dev_priv, 6)) {
		dev_priv->display.fdi_link_train = gen6_fdi_link_train;
	} else if (IS_IVYBRIDGE(dev_priv)) {
		/* FIXME: detect B0+ stepping and use auto training */
		dev_priv->display.fdi_link_train = ivb_manual_fdi_link_train;
	}
}
