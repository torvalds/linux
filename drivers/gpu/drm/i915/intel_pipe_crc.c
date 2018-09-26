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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Author: Damien Lespiau <damien.lespiau@intel.com>
 *
 */

#include <linux/seq_file.h>
#include <linux/circ_buf.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include "intel_drv.h"

static const char * const pipe_crc_sources[] = {
	"none",
	"plane1",
	"plane2",
	"pf",
	"pipe",
	"TV",
	"DP-B",
	"DP-C",
	"DP-D",
	"auto",
};

static int i8xx_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				 uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_INCLUDE_BORDER_I8XX;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int i9xx_pipe_crc_auto_source(struct drm_i915_private *dev_priv,
				     enum pipe pipe,
				     enum intel_pipe_crc_source *source)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	struct intel_digital_port *dig_port;
	int ret = 0;

	*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	drm_modeset_lock_all(dev);
	for_each_intel_encoder(dev, encoder) {
		if (!encoder->base.crtc)
			continue;

		crtc = to_intel_crtc(encoder->base.crtc);

		if (crtc->pipe != pipe)
			continue;

		switch (encoder->type) {
		case INTEL_OUTPUT_TVOUT:
			*source = INTEL_PIPE_CRC_SOURCE_TV;
			break;
		case INTEL_OUTPUT_DP:
		case INTEL_OUTPUT_EDP:
			dig_port = enc_to_dig_port(&encoder->base);
			switch (dig_port->base.port) {
			case PORT_B:
				*source = INTEL_PIPE_CRC_SOURCE_DP_B;
				break;
			case PORT_C:
				*source = INTEL_PIPE_CRC_SOURCE_DP_C;
				break;
			case PORT_D:
				*source = INTEL_PIPE_CRC_SOURCE_DP_D;
				break;
			default:
				WARN(1, "nonexisting DP port %c\n",
				     port_name(dig_port->base.port));
				break;
			}
			break;
		default:
			break;
		}
	}
	drm_modeset_unlock_all(dev);

	return ret;
}

static int vlv_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	bool need_stable_symbols = false;

	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO) {
		int ret = i9xx_pipe_crc_auto_source(dev_priv, pipe, source);
		if (ret)
			return ret;
	}

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_VLV;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_B:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_B_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_C:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_C_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_D:
		if (!IS_CHERRYVIEW(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_D_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * When the pipe CRC tap point is after the transcoders we need
	 * to tweak symbol-level features to produce a deterministic series of
	 * symbols for a given frame. We need to reset those features only once
	 * a frame (instead of every nth symbol):
	 *   - DC-balance: used to ensure a better clock recovery from the data
	 *     link (SDVO)
	 *   - DisplayPort scrambling: used for EMI reduction
	 */
	if (need_stable_symbols) {
		uint32_t tmp = I915_READ(PORT_DFT2_G4X);

		tmp |= DC_BALANCE_RESET_VLV;
		switch (pipe) {
		case PIPE_A:
			tmp |= PIPE_A_SCRAMBLE_RESET;
			break;
		case PIPE_B:
			tmp |= PIPE_B_SCRAMBLE_RESET;
			break;
		case PIPE_C:
			tmp |= PIPE_C_SCRAMBLE_RESET;
			break;
		default:
			return -EINVAL;
		}
		I915_WRITE(PORT_DFT2_G4X, tmp);
	}

	return 0;
}

static int i9xx_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				 enum pipe pipe,
				 enum intel_pipe_crc_source *source,
				 uint32_t *val)
{
	bool need_stable_symbols = false;

	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO) {
		int ret = i9xx_pipe_crc_auto_source(dev_priv, pipe, source);
		if (ret)
			return ret;
	}

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_I9XX;
		break;
	case INTEL_PIPE_CRC_SOURCE_TV:
		if (!SUPPORTS_TV(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_TV_PRE;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_B:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_B_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_C:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_C_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_D:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_D_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * When the pipe CRC tap point is after the transcoders we need
	 * to tweak symbol-level features to produce a deterministic series of
	 * symbols for a given frame. We need to reset those features only once
	 * a frame (instead of every nth symbol):
	 *   - DC-balance: used to ensure a better clock recovery from the data
	 *     link (SDVO)
	 *   - DisplayPort scrambling: used for EMI reduction
	 */
	if (need_stable_symbols) {
		uint32_t tmp = I915_READ(PORT_DFT2_G4X);

		WARN_ON(!IS_G4X(dev_priv));

		I915_WRITE(PORT_DFT_I9XX,
			   I915_READ(PORT_DFT_I9XX) | DC_BALANCE_RESET);

		if (pipe == PIPE_A)
			tmp |= PIPE_A_SCRAMBLE_RESET;
		else
			tmp |= PIPE_B_SCRAMBLE_RESET;

		I915_WRITE(PORT_DFT2_G4X, tmp);
	}

	return 0;
}

static void vlv_undo_pipe_scramble_reset(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	uint32_t tmp = I915_READ(PORT_DFT2_G4X);

	switch (pipe) {
	case PIPE_A:
		tmp &= ~PIPE_A_SCRAMBLE_RESET;
		break;
	case PIPE_B:
		tmp &= ~PIPE_B_SCRAMBLE_RESET;
		break;
	case PIPE_C:
		tmp &= ~PIPE_C_SCRAMBLE_RESET;
		break;
	default:
		return;
	}
	if (!(tmp & PIPE_SCRAMBLE_RESET_MASK))
		tmp &= ~DC_BALANCE_RESET_VLV;
	I915_WRITE(PORT_DFT2_G4X, tmp);

}

static void g4x_undo_pipe_scramble_reset(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	uint32_t tmp = I915_READ(PORT_DFT2_G4X);

	if (pipe == PIPE_A)
		tmp &= ~PIPE_A_SCRAMBLE_RESET;
	else
		tmp &= ~PIPE_B_SCRAMBLE_RESET;
	I915_WRITE(PORT_DFT2_G4X, tmp);

	if (!(tmp & PIPE_SCRAMBLE_RESET_MASK)) {
		I915_WRITE(PORT_DFT_I9XX,
			   I915_READ(PORT_DFT_I9XX) & ~DC_BALANCE_RESET);
	}
}

static int ilk_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PRIMARY_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_SPRITE_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void hsw_pipe_A_crc_wa(struct drm_i915_private *dev_priv,
			      bool enable)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc = intel_get_crtc_for_pipe(dev_priv, PIPE_A);
	struct intel_crtc_state *pipe_config;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	int ret = 0;

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto unlock;
	}

	state->acquire_ctx = &ctx;

retry:
	pipe_config = intel_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(pipe_config)) {
		ret = PTR_ERR(pipe_config);
		goto put_state;
	}

	if (HAS_IPS(dev_priv)) {
		/*
		 * When IPS gets enabled, the pipe CRC changes. Since IPS gets
		 * enabled and disabled dynamically based on package C states,
		 * user space can't make reliable use of the CRCs, so let's just
		 * completely disable it.
		 */
		pipe_config->ips_force_disable = enable;
	}

	if (IS_HASWELL(dev_priv)) {
		pipe_config->pch_pfit.force_thru = enable;
		if (pipe_config->cpu_transcoder == TRANSCODER_EDP &&
		    pipe_config->pch_pfit.enabled != enable)
			pipe_config->base.connectors_changed = true;
	}

	ret = drm_atomic_commit(state);

put_state:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_atomic_state_put(state);
unlock:
	WARN(ret, "Toggling workaround to %i returns %i\n", enable, ret);
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

static int ivb_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				uint32_t *val,
				bool set_wa)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PF;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PRIMARY_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_SPRITE_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PF:
		if (set_wa && (IS_HASWELL(dev_priv) ||
		     IS_BROADWELL(dev_priv)) && pipe == PIPE_A)
			hsw_pipe_A_crc_wa(dev_priv, true);

		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PF_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int get_new_crc_ctl_reg(struct drm_i915_private *dev_priv,
			       enum pipe pipe,
			       enum intel_pipe_crc_source *source, u32 *val,
			       bool set_wa)
{
	if (IS_GEN2(dev_priv))
		return i8xx_pipe_crc_ctl_reg(source, val);
	else if (INTEL_GEN(dev_priv) < 5)
		return i9xx_pipe_crc_ctl_reg(dev_priv, pipe, source, val);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return vlv_pipe_crc_ctl_reg(dev_priv, pipe, source, val);
	else if (IS_GEN5(dev_priv) || IS_GEN6(dev_priv))
		return ilk_pipe_crc_ctl_reg(source, val);
	else
		return ivb_pipe_crc_ctl_reg(dev_priv, pipe, source, val, set_wa);
}

static int
display_crc_ctl_parse_source(const char *buf, enum intel_pipe_crc_source *s)
{
	int i;

	if (!buf) {
		*s = INTEL_PIPE_CRC_SOURCE_NONE;
		return 0;
	}

	i = match_string(pipe_crc_sources, ARRAY_SIZE(pipe_crc_sources), buf);
	if (i < 0)
		return i;

	*s = i;
	return 0;
}

void intel_display_crc_init(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[pipe];

		spin_lock_init(&pipe_crc->lock);
	}
}

int intel_crtc_set_crc_source(struct drm_crtc *crtc, const char *source_name,
			      size_t *values_cnt)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[crtc->index];
	enum intel_display_power_domain power_domain;
	enum intel_pipe_crc_source source;
	u32 val = 0; /* shut up gcc */
	int ret = 0;

	if (display_crc_ctl_parse_source(source_name, &source) < 0) {
		DRM_DEBUG_DRIVER("unknown source %s\n", source_name);
		return -EINVAL;
	}

	power_domain = POWER_DOMAIN_PIPE(crtc->index);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain)) {
		DRM_DEBUG_KMS("Trying to capture CRC while pipe is off\n");
		return -EIO;
	}

	ret = get_new_crc_ctl_reg(dev_priv, crtc->index, &source, &val, true);
	if (ret != 0)
		goto out;

	pipe_crc->source = source;
	I915_WRITE(PIPE_CRC_CTL(crtc->index), val);
	POSTING_READ(PIPE_CRC_CTL(crtc->index));

	if (!source) {
		if (IS_G4X(dev_priv))
			g4x_undo_pipe_scramble_reset(dev_priv, crtc->index);
		else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			vlv_undo_pipe_scramble_reset(dev_priv, crtc->index);
		else if ((IS_HASWELL(dev_priv) ||
			  IS_BROADWELL(dev_priv)) && crtc->index == PIPE_A)
			hsw_pipe_A_crc_wa(dev_priv, false);
	}

	pipe_crc->skipped = 0;
	*values_cnt = 5;

out:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

void intel_crtc_enable_pipe_crc(struct intel_crtc *intel_crtc)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[crtc->index];
	u32 val = 0;

	if (!crtc->crc.opened)
		return;

	if (get_new_crc_ctl_reg(dev_priv, crtc->index, &pipe_crc->source, &val, false) < 0)
		return;

	/* Don't need pipe_crc->lock here, IRQs are not generated. */
	pipe_crc->skipped = 0;

	I915_WRITE(PIPE_CRC_CTL(crtc->index), val);
	POSTING_READ(PIPE_CRC_CTL(crtc->index));
}

void intel_crtc_disable_pipe_crc(struct intel_crtc *intel_crtc)
{
	struct drm_crtc *crtc = &intel_crtc->base;
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[crtc->index];

	/* Swallow crc's until we stop generating them. */
	spin_lock_irq(&pipe_crc->lock);
	pipe_crc->skipped = INT_MIN;
	spin_unlock_irq(&pipe_crc->lock);

	I915_WRITE(PIPE_CRC_CTL(crtc->index), 0);
	POSTING_READ(PIPE_CRC_CTL(crtc->index));
	synchronize_irq(dev_priv->drm.irq);
}
