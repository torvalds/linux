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

#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "i915_drv.h"
#include "i915_irq.h"
#include "intel_atomic.h"
#include "intel_de.h"
#include "intel_display_irq.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_pipe_crc.h"
#include "intel_pipe_crc_regs.h"

static const char * const pipe_crc_sources[] = {
	[INTEL_PIPE_CRC_SOURCE_NONE] = "none",
	[INTEL_PIPE_CRC_SOURCE_PLANE1] = "plane1",
	[INTEL_PIPE_CRC_SOURCE_PLANE2] = "plane2",
	[INTEL_PIPE_CRC_SOURCE_PLANE3] = "plane3",
	[INTEL_PIPE_CRC_SOURCE_PLANE4] = "plane4",
	[INTEL_PIPE_CRC_SOURCE_PLANE5] = "plane5",
	[INTEL_PIPE_CRC_SOURCE_PLANE6] = "plane6",
	[INTEL_PIPE_CRC_SOURCE_PLANE7] = "plane7",
	[INTEL_PIPE_CRC_SOURCE_PIPE] = "pipe",
	[INTEL_PIPE_CRC_SOURCE_TV] = "TV",
	[INTEL_PIPE_CRC_SOURCE_DP_B] = "DP-B",
	[INTEL_PIPE_CRC_SOURCE_DP_C] = "DP-C",
	[INTEL_PIPE_CRC_SOURCE_DP_D] = "DP-D",
	[INTEL_PIPE_CRC_SOURCE_AUTO] = "auto",
};

static int i8xx_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				 u32 *val)
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

static void i9xx_pipe_crc_auto_source(struct intel_display *display,
				      enum pipe pipe,
				      enum intel_pipe_crc_source *source)
{
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	struct intel_digital_port *dig_port;

	*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	drm_modeset_lock_all(display->drm);
	for_each_intel_encoder(display->drm, encoder) {
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
			dig_port = enc_to_dig_port(encoder);
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
				drm_WARN(display->drm, 1, "nonexisting DP port %c\n",
					 port_name(dig_port->base.port));
				break;
			}
			break;
		default:
			break;
		}
	}
	drm_modeset_unlock_all(display->drm);
}

static int vlv_pipe_crc_ctl_reg(struct intel_display *display,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				u32 *val)
{
	bool need_stable_symbols = false;

	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		i9xx_pipe_crc_auto_source(display, pipe, source);

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
		if (!display->platform.cherryview)
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
		u32 tmp = intel_de_read(display, PORT_DFT2_G4X(display));

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
		intel_de_write(display, PORT_DFT2_G4X(display), tmp);
	}

	return 0;
}

static int i9xx_pipe_crc_ctl_reg(struct intel_display *display,
				 enum pipe pipe,
				 enum intel_pipe_crc_source *source,
				 u32 *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		i9xx_pipe_crc_auto_source(display, pipe, source);

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_I9XX;
		break;
	case INTEL_PIPE_CRC_SOURCE_TV:
		if (!SUPPORTS_TV(display))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_TV_PRE;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		/*
		 * The DP CRC source doesn't work on g4x.
		 * It can be made to work to some degree by selecting
		 * the correct CRC source before the port is enabled,
		 * and not touching the CRC source bits again until
		 * the port is disabled. But even then the bits
		 * eventually get stuck and a reboot is needed to get
		 * working CRCs on the pipe again. Let's simply
		 * refuse to use DP CRCs on g4x.
		 */
		return -EINVAL;
	}

	return 0;
}

static void vlv_undo_pipe_scramble_reset(struct intel_display *display,
					 enum pipe pipe)
{
	u32 tmp = intel_de_read(display, PORT_DFT2_G4X(display));

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
	intel_de_write(display, PORT_DFT2_G4X(display), tmp);
}

static int ilk_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				u32 *val)
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

static void
intel_crtc_crc_setup_workarounds(struct intel_crtc *crtc, bool enable)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_crtc_state *pipe_config;
	struct drm_atomic_state *state;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	if (display->platform.i945gm || display->platform.i915gm)
		i915gm_irq_cstate_wa(display, enable);

	drm_modeset_acquire_init(&ctx, 0);

	state = drm_atomic_state_alloc(display->drm);
	if (!state) {
		ret = -ENOMEM;
		goto unlock;
	}

	state->acquire_ctx = &ctx;
	to_intel_atomic_state(state)->internal = true;

retry:
	pipe_config = intel_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(pipe_config)) {
		ret = PTR_ERR(pipe_config);
		goto put_state;
	}

	pipe_config->uapi.mode_changed = pipe_config->has_psr;
	pipe_config->crc_enabled = enable;

	if (display->platform.haswell &&
	    pipe_config->hw.active && crtc->pipe == PIPE_A &&
	    pipe_config->cpu_transcoder == TRANSCODER_EDP)
		pipe_config->uapi.mode_changed = true;

	ret = drm_atomic_commit(state);

put_state:
	if (ret == -EDEADLK) {
		drm_atomic_state_clear(state);
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_atomic_state_put(state);
unlock:
	drm_WARN(display->drm, ret,
		 "Toggling workaround to %i returns %i\n", enable, ret);
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
}

static int ivb_pipe_crc_ctl_reg(struct intel_display *display,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				u32 *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PRIMARY_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_SPRITE_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PIPE:
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

static int skl_pipe_crc_ctl_reg(struct intel_display *display,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				u32 *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PLANE_1_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PLANE_2_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE3:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PLANE_3_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE4:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PLANE_4_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE5:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PLANE_5_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE6:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PLANE_6_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE7:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PLANE_7_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DMUX_SKL;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int get_new_crc_ctl_reg(struct intel_display *display,
			       enum pipe pipe,
			       enum intel_pipe_crc_source *source, u32 *val)
{
	if (DISPLAY_VER(display) == 2)
		return i8xx_pipe_crc_ctl_reg(source, val);
	else if (DISPLAY_VER(display) < 5)
		return i9xx_pipe_crc_ctl_reg(display, pipe, source, val);
	else if (display->platform.valleyview || display->platform.cherryview)
		return vlv_pipe_crc_ctl_reg(display, pipe, source, val);
	else if (display->platform.ironlake || display->platform.sandybridge)
		return ilk_pipe_crc_ctl_reg(source, val);
	else if (DISPLAY_VER(display) < 9)
		return ivb_pipe_crc_ctl_reg(display, pipe, source, val);
	else
		return skl_pipe_crc_ctl_reg(display, pipe, source, val);
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

void intel_crtc_crc_init(struct intel_crtc *crtc)
{
	struct intel_pipe_crc *pipe_crc = &crtc->pipe_crc;

	spin_lock_init(&pipe_crc->lock);
}

static int i8xx_crc_source_valid(struct intel_display *display,
				 const enum intel_pipe_crc_source source)
{
	switch (source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
	case INTEL_PIPE_CRC_SOURCE_NONE:
		return 0;
	default:
		return -EINVAL;
	}
}

static int i9xx_crc_source_valid(struct intel_display *display,
				 const enum intel_pipe_crc_source source)
{
	switch (source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
	case INTEL_PIPE_CRC_SOURCE_TV:
	case INTEL_PIPE_CRC_SOURCE_NONE:
		return 0;
	default:
		return -EINVAL;
	}
}

static int vlv_crc_source_valid(struct intel_display *display,
				const enum intel_pipe_crc_source source)
{
	switch (source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
	case INTEL_PIPE_CRC_SOURCE_DP_B:
	case INTEL_PIPE_CRC_SOURCE_DP_C:
	case INTEL_PIPE_CRC_SOURCE_DP_D:
	case INTEL_PIPE_CRC_SOURCE_NONE:
		return 0;
	default:
		return -EINVAL;
	}
}

static int ilk_crc_source_valid(struct intel_display *display,
				const enum intel_pipe_crc_source source)
{
	switch (source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
	case INTEL_PIPE_CRC_SOURCE_NONE:
		return 0;
	default:
		return -EINVAL;
	}
}

static int ivb_crc_source_valid(struct intel_display *display,
				const enum intel_pipe_crc_source source)
{
	switch (source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
	case INTEL_PIPE_CRC_SOURCE_NONE:
		return 0;
	default:
		return -EINVAL;
	}
}

static int skl_crc_source_valid(struct intel_display *display,
				const enum intel_pipe_crc_source source)
{
	switch (source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
	case INTEL_PIPE_CRC_SOURCE_PLANE3:
	case INTEL_PIPE_CRC_SOURCE_PLANE4:
	case INTEL_PIPE_CRC_SOURCE_PLANE5:
	case INTEL_PIPE_CRC_SOURCE_PLANE6:
	case INTEL_PIPE_CRC_SOURCE_PLANE7:
	case INTEL_PIPE_CRC_SOURCE_NONE:
		return 0;
	default:
		return -EINVAL;
	}
}

static int
intel_is_valid_crc_source(struct intel_display *display,
			  const enum intel_pipe_crc_source source)
{
	if (DISPLAY_VER(display) == 2)
		return i8xx_crc_source_valid(display, source);
	else if (DISPLAY_VER(display) < 5)
		return i9xx_crc_source_valid(display, source);
	else if (display->platform.valleyview || display->platform.cherryview)
		return vlv_crc_source_valid(display, source);
	else if (display->platform.ironlake || display->platform.sandybridge)
		return ilk_crc_source_valid(display, source);
	else if (DISPLAY_VER(display) < 9)
		return ivb_crc_source_valid(display, source);
	else
		return skl_crc_source_valid(display, source);
}

const char *const *intel_crtc_get_crc_sources(struct drm_crtc *crtc,
					      size_t *count)
{
	*count = ARRAY_SIZE(pipe_crc_sources);
	return pipe_crc_sources;
}

int intel_crtc_verify_crc_source(struct drm_crtc *crtc, const char *source_name,
				 size_t *values_cnt)
{
	struct intel_display *display = to_intel_display(crtc->dev);
	enum intel_pipe_crc_source source;

	if (display_crc_ctl_parse_source(source_name, &source) < 0) {
		drm_dbg_kms(display->drm, "unknown source %s\n", source_name);
		return -EINVAL;
	}

	if (source == INTEL_PIPE_CRC_SOURCE_AUTO ||
	    intel_is_valid_crc_source(display, source) == 0) {
		*values_cnt = 5;
		return 0;
	}

	return -EINVAL;
}

int intel_crtc_set_crc_source(struct drm_crtc *_crtc, const char *source_name)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct intel_display *display = to_intel_display(crtc);
	struct intel_pipe_crc *pipe_crc = &crtc->pipe_crc;
	enum intel_display_power_domain power_domain;
	enum intel_pipe_crc_source source;
	enum pipe pipe = crtc->pipe;
	intel_wakeref_t wakeref;
	u32 val = 0; /* shut up gcc */
	int ret = 0;
	bool enable;

	if (display_crc_ctl_parse_source(source_name, &source) < 0) {
		drm_dbg_kms(display->drm, "unknown source %s\n", source_name);
		return -EINVAL;
	}

	power_domain = POWER_DOMAIN_PIPE(pipe);
	wakeref = intel_display_power_get_if_enabled(display, power_domain);
	if (!wakeref) {
		drm_dbg_kms(display->drm,
			    "Trying to capture CRC while pipe is off\n");
		return -EIO;
	}

	enable = source != INTEL_PIPE_CRC_SOURCE_NONE;
	if (enable)
		intel_crtc_crc_setup_workarounds(crtc, true);

	ret = get_new_crc_ctl_reg(display, pipe, &source, &val);
	if (ret != 0)
		goto out;

	pipe_crc->source = source;
	intel_de_write(display, PIPE_CRC_CTL(display, pipe), val);
	intel_de_posting_read(display, PIPE_CRC_CTL(display, pipe));

	if (!source) {
		if (display->platform.valleyview || display->platform.cherryview)
			vlv_undo_pipe_scramble_reset(display, pipe);
	}

	pipe_crc->skipped = 0;

out:
	if (!enable)
		intel_crtc_crc_setup_workarounds(crtc, false);

	intel_display_power_put(display, power_domain, wakeref);

	return ret;
}

void intel_crtc_enable_pipe_crc(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_pipe_crc *pipe_crc = &crtc->pipe_crc;
	enum pipe pipe = crtc->pipe;
	u32 val = 0;

	if (!crtc->base.crc.opened)
		return;

	if (get_new_crc_ctl_reg(display, pipe, &pipe_crc->source, &val) < 0)
		return;

	/* Don't need pipe_crc->lock here, IRQs are not generated. */
	pipe_crc->skipped = 0;

	intel_de_write(display, PIPE_CRC_CTL(display, pipe), val);
	intel_de_posting_read(display, PIPE_CRC_CTL(display, pipe));
}

void intel_crtc_disable_pipe_crc(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	struct drm_i915_private *dev_priv = to_i915(display->drm);
	struct intel_pipe_crc *pipe_crc = &crtc->pipe_crc;
	enum pipe pipe = crtc->pipe;

	/* Swallow crc's until we stop generating them. */
	spin_lock_irq(&pipe_crc->lock);
	pipe_crc->skipped = INT_MIN;
	spin_unlock_irq(&pipe_crc->lock);

	intel_de_write(display, PIPE_CRC_CTL(display, pipe), 0);
	intel_de_posting_read(display, PIPE_CRC_CTL(display, pipe));
	intel_synchronize_irq(dev_priv);
}
