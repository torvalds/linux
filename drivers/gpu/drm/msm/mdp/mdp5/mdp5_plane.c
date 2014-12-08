/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdp5_kms.h"

struct mdp5_plane {
	struct drm_plane base;
	const char *name;

	enum mdp5_pipe pipe;

	spinlock_t pipe_lock;	/* protect REG_MDP5_PIPE_* registers */
	uint32_t reg_offset;

	uint32_t flush_mask;	/* used to commit pipe registers */

	uint32_t nformats;
	uint32_t formats[32];

	bool enabled;
};
#define to_mdp5_plane(x) container_of(x, struct mdp5_plane, base)

static int mdp5_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h);
static void set_scanout_locked(struct drm_plane *plane,
		struct drm_framebuffer *fb);

static struct mdp5_kms *get_kms(struct drm_plane *plane)
{
	struct msm_drm_private *priv = plane->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

static bool plane_enabled(struct drm_plane_state *state)
{
	return state->fb && state->crtc;
}

static int mdp5_plane_disable(struct drm_plane *plane)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	enum mdp5_pipe pipe = mdp5_plane->pipe;

	DBG("%s: disable", mdp5_plane->name);

	if (mdp5_kms) {
		/* Release the memory we requested earlier from the SMP: */
		mdp5_smp_release(mdp5_kms->smp, pipe);
	}

	return 0;
}

static void mdp5_plane_destroy(struct drm_plane *plane)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);

	drm_plane_helper_disable(plane);
	drm_plane_cleanup(plane);

	kfree(mdp5_plane);
}

/* helper to install properties which are common to planes and crtcs */
void mdp5_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj)
{
	// XXX
}

int mdp5_plane_set_property(struct drm_plane *plane,
		struct drm_property *property, uint64_t val)
{
	// XXX
	return -EINVAL;
}

static void mdp5_plane_reset(struct drm_plane *plane)
{
	struct mdp5_plane_state *mdp5_state;

	if (plane->state && plane->state->fb)
		drm_framebuffer_unreference(plane->state->fb);

	kfree(to_mdp5_plane_state(plane->state));
	mdp5_state = kzalloc(sizeof(*mdp5_state), GFP_KERNEL);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		mdp5_state->zpos = 0;
	} else {
		mdp5_state->zpos = 1 + drm_plane_index(plane);
	}
	mdp5_state->base.plane = plane;

	plane->state = &mdp5_state->base;
}

static struct drm_plane_state *
mdp5_plane_duplicate_state(struct drm_plane *plane)
{
	struct mdp5_plane_state *mdp5_state;

	if (WARN_ON(!plane->state))
		return NULL;

	mdp5_state = kmemdup(to_mdp5_plane_state(plane->state),
			sizeof(*mdp5_state), GFP_KERNEL);

	if (mdp5_state && mdp5_state->base.fb)
		drm_framebuffer_reference(mdp5_state->base.fb);

	mdp5_state->mode_changed = false;
	mdp5_state->pending = false;

	return &mdp5_state->base;
}

static void mdp5_plane_destroy_state(struct drm_plane *plane,
		struct drm_plane_state *state)
{
	if (state->fb)
		drm_framebuffer_unreference(state->fb);

	kfree(to_mdp5_plane_state(state));
}

static const struct drm_plane_funcs mdp5_plane_funcs = {
		.update_plane = drm_atomic_helper_update_plane,
		.disable_plane = drm_atomic_helper_disable_plane,
		.destroy = mdp5_plane_destroy,
		.set_property = mdp5_plane_set_property,
		.reset = mdp5_plane_reset,
		.atomic_duplicate_state = mdp5_plane_duplicate_state,
		.atomic_destroy_state = mdp5_plane_destroy_state,
};

static int mdp5_plane_prepare_fb(struct drm_plane *plane,
		struct drm_framebuffer *fb)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct mdp5_kms *mdp5_kms = get_kms(plane);

	DBG("%s: prepare: FB[%u]", mdp5_plane->name, fb->base.id);
	return msm_framebuffer_prepare(fb, mdp5_kms->id);
}

static void mdp5_plane_cleanup_fb(struct drm_plane *plane,
		struct drm_framebuffer *fb)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct mdp5_kms *mdp5_kms = get_kms(plane);

	DBG("%s: cleanup: FB[%u]", mdp5_plane->name, fb->base.id);
	msm_framebuffer_cleanup(fb, mdp5_kms->id);
}

static int mdp5_plane_atomic_check(struct drm_plane *plane,
		struct drm_plane_state *state)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct drm_plane_state *old_state = plane->state;

	DBG("%s: check (%d -> %d)", mdp5_plane->name,
			plane_enabled(old_state), plane_enabled(state));

	if (plane_enabled(state) && plane_enabled(old_state)) {
		/* we cannot change SMP block configuration during scanout: */
		bool full_modeset = false;
		if (state->fb->pixel_format != old_state->fb->pixel_format) {
			DBG("%s: pixel_format change!", mdp5_plane->name);
			full_modeset = true;
		}
		if (state->src_w != old_state->src_w) {
			DBG("%s: src_w change!", mdp5_plane->name);
			full_modeset = true;
		}
		if (to_mdp5_plane_state(old_state)->pending) {
			DBG("%s: still pending!", mdp5_plane->name);
			full_modeset = true;
		}
		if (full_modeset) {
			struct drm_crtc_state *crtc_state =
					drm_atomic_get_crtc_state(state->state, state->crtc);
			crtc_state->mode_changed = true;
			to_mdp5_plane_state(state)->mode_changed = true;
		}
	} else {
		to_mdp5_plane_state(state)->mode_changed = true;
	}

	return 0;
}

static void mdp5_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct drm_plane_state *state = plane->state;

	DBG("%s: update", mdp5_plane->name);

	if (!plane_enabled(state)) {
		to_mdp5_plane_state(state)->pending = true;
		mdp5_plane_disable(plane);
	} else if (to_mdp5_plane_state(state)->mode_changed) {
		int ret;
		to_mdp5_plane_state(state)->pending = true;
		ret = mdp5_plane_mode_set(plane,
				state->crtc, state->fb,
				state->crtc_x, state->crtc_y,
				state->crtc_w, state->crtc_h,
				state->src_x,  state->src_y,
				state->src_w, state->src_h);
		/* atomic_check should have ensured that this doesn't fail */
		WARN_ON(ret < 0);
	} else {
		unsigned long flags;
		spin_lock_irqsave(&mdp5_plane->pipe_lock, flags);
		set_scanout_locked(plane, state->fb);
		spin_unlock_irqrestore(&mdp5_plane->pipe_lock, flags);
	}
}

static const struct drm_plane_helper_funcs mdp5_plane_helper_funcs = {
		.prepare_fb = mdp5_plane_prepare_fb,
		.cleanup_fb = mdp5_plane_cleanup_fb,
		.atomic_check = mdp5_plane_atomic_check,
		.atomic_update = mdp5_plane_atomic_update,
};

static void set_scanout_locked(struct drm_plane *plane,
		struct drm_framebuffer *fb)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	enum mdp5_pipe pipe = mdp5_plane->pipe;

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_STRIDE_A(pipe),
			MDP5_PIPE_SRC_STRIDE_A_P0(fb->pitches[0]) |
			MDP5_PIPE_SRC_STRIDE_A_P1(fb->pitches[1]));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_STRIDE_B(pipe),
			MDP5_PIPE_SRC_STRIDE_B_P2(fb->pitches[2]) |
			MDP5_PIPE_SRC_STRIDE_B_P3(fb->pitches[3]));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC0_ADDR(pipe),
			msm_framebuffer_iova(fb, mdp5_kms->id, 0));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC1_ADDR(pipe),
			msm_framebuffer_iova(fb, mdp5_kms->id, 1));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC2_ADDR(pipe),
			msm_framebuffer_iova(fb, mdp5_kms->id, 2));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC3_ADDR(pipe),
			msm_framebuffer_iova(fb, mdp5_kms->id, 4));

	plane->fb = fb;
}

/* Note: mdp5_plane->pipe_lock must be locked */
static void csc_disable(struct mdp5_kms *mdp5_kms, enum mdp5_pipe pipe)
{
	uint32_t value = mdp5_read(mdp5_kms, REG_MDP5_PIPE_OP_MODE(pipe)) &
			 ~MDP5_PIPE_OP_MODE_CSC_1_EN;

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_OP_MODE(pipe), value);
}

/* Note: mdp5_plane->pipe_lock must be locked */
static void csc_enable(struct mdp5_kms *mdp5_kms, enum mdp5_pipe pipe,
		struct csc_cfg *csc)
{
	uint32_t  i, mode = 0; /* RGB, no CSC */
	uint32_t *matrix;

	if (unlikely(!csc))
		return;

	if ((csc->type == CSC_YUV2RGB) || (CSC_YUV2YUV == csc->type))
		mode |= MDP5_PIPE_OP_MODE_CSC_SRC_DATA_FORMAT(DATA_FORMAT_YUV);
	if ((csc->type == CSC_RGB2YUV) || (CSC_YUV2YUV == csc->type))
		mode |= MDP5_PIPE_OP_MODE_CSC_DST_DATA_FORMAT(DATA_FORMAT_YUV);
	mode |= MDP5_PIPE_OP_MODE_CSC_1_EN;
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_OP_MODE(pipe), mode);

	matrix = csc->matrix;
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_MATRIX_COEFF_0(pipe),
			MDP5_PIPE_CSC_1_MATRIX_COEFF_0_COEFF_11(matrix[0]) |
			MDP5_PIPE_CSC_1_MATRIX_COEFF_0_COEFF_12(matrix[1]));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_MATRIX_COEFF_1(pipe),
			MDP5_PIPE_CSC_1_MATRIX_COEFF_1_COEFF_13(matrix[2]) |
			MDP5_PIPE_CSC_1_MATRIX_COEFF_1_COEFF_21(matrix[3]));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_MATRIX_COEFF_2(pipe),
			MDP5_PIPE_CSC_1_MATRIX_COEFF_2_COEFF_22(matrix[4]) |
			MDP5_PIPE_CSC_1_MATRIX_COEFF_2_COEFF_23(matrix[5]));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_MATRIX_COEFF_3(pipe),
			MDP5_PIPE_CSC_1_MATRIX_COEFF_3_COEFF_31(matrix[6]) |
			MDP5_PIPE_CSC_1_MATRIX_COEFF_3_COEFF_32(matrix[7]));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_MATRIX_COEFF_4(pipe),
			MDP5_PIPE_CSC_1_MATRIX_COEFF_4_COEFF_33(matrix[8]));

	for (i = 0; i < ARRAY_SIZE(csc->pre_bias); i++) {
		uint32_t *pre_clamp = csc->pre_clamp;
		uint32_t *post_clamp = csc->post_clamp;

		mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_PRE_CLAMP(pipe, i),
			MDP5_PIPE_CSC_1_PRE_CLAMP_REG_HIGH(pre_clamp[2*i+1]) |
			MDP5_PIPE_CSC_1_PRE_CLAMP_REG_LOW(pre_clamp[2*i]));

		mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_POST_CLAMP(pipe, i),
			MDP5_PIPE_CSC_1_POST_CLAMP_REG_HIGH(post_clamp[2*i+1]) |
			MDP5_PIPE_CSC_1_POST_CLAMP_REG_LOW(post_clamp[2*i]));

		mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_PRE_BIAS(pipe, i),
			MDP5_PIPE_CSC_1_PRE_BIAS_REG_VALUE(csc->pre_bias[i]));

		mdp5_write(mdp5_kms, REG_MDP5_PIPE_CSC_1_POST_BIAS(pipe, i),
			MDP5_PIPE_CSC_1_POST_BIAS_REG_VALUE(csc->post_bias[i]));
	}
}

#define PHASE_STEP_SHIFT	21
#define DOWN_SCALE_RATIO_MAX	32	/* 2^(26-21) */

static int calc_phase_step(uint32_t src, uint32_t dst, uint32_t *out_phase)
{
	uint32_t unit;

	if (src == 0 || dst == 0)
		return -EINVAL;

	/*
	 * PHASE_STEP_X/Y is coded on 26 bits (25:0),
	 * where 2^21 represents the unity "1" in fixed-point hardware design.
	 * This leaves 5 bits for the integer part (downscale case):
	 *	-> maximum downscale ratio = 0b1_1111 = 31
	 */
	if (src > (dst * DOWN_SCALE_RATIO_MAX))
		return -EOVERFLOW;

	unit = 1 << PHASE_STEP_SHIFT;
	*out_phase = mult_frac(unit, src, dst);

	return 0;
}

static int calc_scalex_steps(uint32_t pixel_format, uint32_t src, uint32_t dest,
		uint32_t phasex_steps[2])
{
	uint32_t phasex_step;
	unsigned int hsub;
	int ret;

	ret = calc_phase_step(src, dest, &phasex_step);
	if (ret)
		return ret;

	hsub = drm_format_horz_chroma_subsampling(pixel_format);

	phasex_steps[0] = phasex_step;
	phasex_steps[1] = phasex_step / hsub;

	return 0;
}

static int calc_scaley_steps(uint32_t pixel_format, uint32_t src, uint32_t dest,
		uint32_t phasey_steps[2])
{
	uint32_t phasey_step;
	unsigned int vsub;
	int ret;

	ret = calc_phase_step(src, dest, &phasey_step);
	if (ret)
		return ret;

	vsub = drm_format_vert_chroma_subsampling(pixel_format);

	phasey_steps[0] = phasey_step;
	phasey_steps[1] = phasey_step / vsub;

	return 0;
}

static uint32_t get_scalex_config(uint32_t src, uint32_t dest)
{
	uint32_t filter;

	filter = (src <= dest) ? SCALE_FILTER_BIL : SCALE_FILTER_PCMN;

	return  MDP5_PIPE_SCALE_CONFIG_SCALEX_EN |
		MDP5_PIPE_SCALE_CONFIG_SCALEX_MIN_FILTER(filter) |
		MDP5_PIPE_SCALE_CONFIG_SCALEX_CR_FILTER(filter)  |
		MDP5_PIPE_SCALE_CONFIG_SCALEX_MAX_FILTER(filter);
}

static uint32_t get_scaley_config(uint32_t src, uint32_t dest)
{
	uint32_t filter;

	filter = (src <= dest) ? SCALE_FILTER_BIL : SCALE_FILTER_PCMN;

	return  MDP5_PIPE_SCALE_CONFIG_SCALEY_EN |
		MDP5_PIPE_SCALE_CONFIG_SCALEY_MIN_FILTER(filter) |
		MDP5_PIPE_SCALE_CONFIG_SCALEY_CR_FILTER(filter)  |
		MDP5_PIPE_SCALE_CONFIG_SCALEY_MAX_FILTER(filter);
}

static int mdp5_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	struct device *dev = mdp5_kms->dev->dev;
	enum mdp5_pipe pipe = mdp5_plane->pipe;
	const struct mdp_format *format;
	uint32_t nplanes, config = 0;
	/* below array -> index 0: comp 0/3 ; index 1: comp 1/2 */
	uint32_t phasex_step[2] = {0,}, phasey_step[2] = {0,};
	uint32_t hdecm = 0, vdecm = 0;
	uint32_t pix_format;
	unsigned long flags;
	int ret;

	nplanes = drm_format_num_planes(fb->pixel_format);

	/* bad formats should already be rejected: */
	if (WARN_ON(nplanes > pipe2nclients(pipe)))
		return -EINVAL;

	format = to_mdp_format(msm_framebuffer_format(fb));
	pix_format = format->base.pixel_format;

	/* src values are in Q16 fixed point, convert to integer: */
	src_x = src_x >> 16;
	src_y = src_y >> 16;
	src_w = src_w >> 16;
	src_h = src_h >> 16;

	DBG("%s: FB[%u] %u,%u,%u,%u -> CRTC[%u] %d,%d,%u,%u", mdp5_plane->name,
			fb->base.id, src_x, src_y, src_w, src_h,
			crtc->base.id, crtc_x, crtc_y, crtc_w, crtc_h);

	/* Request some memory from the SMP: */
	ret = mdp5_smp_request(mdp5_kms->smp,
			mdp5_plane->pipe, fb->pixel_format, src_w);
	if (ret)
		return ret;

	/*
	 * Currently we update the hw for allocations/requests immediately,
	 * but once atomic modeset/pageflip is in place, the allocation
	 * would move into atomic->check_plane_state(), while updating the
	 * hw would remain here:
	 */
	mdp5_smp_configure(mdp5_kms->smp, pipe);

	/* SCALE is used to both scale and up-sample chroma components */

	if ((src_w != crtc_w) || MDP_FORMAT_IS_YUV(format)) {
		/* TODO calc hdecm */
		ret = calc_scalex_steps(pix_format, src_w, crtc_w, phasex_step);
		if (ret) {
			dev_err(dev, "X scaling (%d -> %d) failed: %d\n",
					src_w, crtc_w, ret);
			return ret;
		}
		config |= get_scalex_config(src_w, crtc_w);
	}

	if ((src_h != crtc_h) || MDP_FORMAT_IS_YUV(format)) {
		/* TODO calc vdecm */
		ret = calc_scaley_steps(pix_format, src_h, crtc_h, phasey_step);
		if (ret) {
			dev_err(dev, "Y scaling (%d -> %d) failed: %d\n",
					src_h, crtc_h, ret);
			return ret;
		}
		config |= get_scaley_config(src_h, crtc_h);
	}

	spin_lock_irqsave(&mdp5_plane->pipe_lock, flags);

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_IMG_SIZE(pipe),
			MDP5_PIPE_SRC_IMG_SIZE_WIDTH(src_w) |
			MDP5_PIPE_SRC_IMG_SIZE_HEIGHT(src_h));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_SIZE(pipe),
			MDP5_PIPE_SRC_SIZE_WIDTH(src_w) |
			MDP5_PIPE_SRC_SIZE_HEIGHT(src_h));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_XY(pipe),
			MDP5_PIPE_SRC_XY_X(src_x) |
			MDP5_PIPE_SRC_XY_Y(src_y));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_OUT_SIZE(pipe),
			MDP5_PIPE_OUT_SIZE_WIDTH(crtc_w) |
			MDP5_PIPE_OUT_SIZE_HEIGHT(crtc_h));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_OUT_XY(pipe),
			MDP5_PIPE_OUT_XY_X(crtc_x) |
			MDP5_PIPE_OUT_XY_Y(crtc_y));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_FORMAT(pipe),
			MDP5_PIPE_SRC_FORMAT_A_BPC(format->bpc_a) |
			MDP5_PIPE_SRC_FORMAT_R_BPC(format->bpc_r) |
			MDP5_PIPE_SRC_FORMAT_G_BPC(format->bpc_g) |
			MDP5_PIPE_SRC_FORMAT_B_BPC(format->bpc_b) |
			COND(format->alpha_enable, MDP5_PIPE_SRC_FORMAT_ALPHA_ENABLE) |
			MDP5_PIPE_SRC_FORMAT_CPP(format->cpp - 1) |
			MDP5_PIPE_SRC_FORMAT_UNPACK_COUNT(format->unpack_count - 1) |
			COND(format->unpack_tight, MDP5_PIPE_SRC_FORMAT_UNPACK_TIGHT) |
			MDP5_PIPE_SRC_FORMAT_NUM_PLANES(format->fetch_type) |
			MDP5_PIPE_SRC_FORMAT_CHROMA_SAMP(format->chroma_sample));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_UNPACK(pipe),
			MDP5_PIPE_SRC_UNPACK_ELEM0(format->unpack[0]) |
			MDP5_PIPE_SRC_UNPACK_ELEM1(format->unpack[1]) |
			MDP5_PIPE_SRC_UNPACK_ELEM2(format->unpack[2]) |
			MDP5_PIPE_SRC_UNPACK_ELEM3(format->unpack[3]));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_OP_MODE(pipe),
			MDP5_PIPE_SRC_OP_MODE_BWC(BWC_LOSSLESS));

	/* not using secure mode: */
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_ADDR_SW_STATUS(pipe), 0);

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_PHASE_STEP_X(pipe),
			phasex_step[0]);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_PHASE_STEP_Y(pipe),
			phasey_step[0]);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_CR_PHASE_STEP_X(pipe),
			phasex_step[1]);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_CR_PHASE_STEP_Y(pipe),
			phasey_step[1]);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_DECIMATION(pipe),
			MDP5_PIPE_DECIMATION_VERT(vdecm) |
			MDP5_PIPE_DECIMATION_HORZ(hdecm));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_CONFIG(pipe), config);

	if (MDP_FORMAT_IS_YUV(format))
		csc_enable(mdp5_kms, pipe,
				mdp_get_default_csc_cfg(CSC_YUV2RGB));
	else
		csc_disable(mdp5_kms, pipe);

	set_scanout_locked(plane, fb);

	spin_unlock_irqrestore(&mdp5_plane->pipe_lock, flags);

	return ret;
}

void mdp5_plane_complete_flip(struct drm_plane *plane)
{
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	enum mdp5_pipe pipe = mdp5_plane->pipe;

	DBG("%s: complete flip", mdp5_plane->name);

	mdp5_smp_commit(mdp5_kms->smp, pipe);

	to_mdp5_plane_state(plane->state)->pending = false;
}

enum mdp5_pipe mdp5_plane_pipe(struct drm_plane *plane)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	return mdp5_plane->pipe;
}

uint32_t mdp5_plane_get_flush(struct drm_plane *plane)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);

	return mdp5_plane->flush_mask;
}

/* initialize plane */
struct drm_plane *mdp5_plane_init(struct drm_device *dev,
		enum mdp5_pipe pipe, bool private_plane, uint32_t reg_offset)
{
	struct drm_plane *plane = NULL;
	struct mdp5_plane *mdp5_plane;
	int ret;
	enum drm_plane_type type;

	mdp5_plane = kzalloc(sizeof(*mdp5_plane), GFP_KERNEL);
	if (!mdp5_plane) {
		ret = -ENOMEM;
		goto fail;
	}

	plane = &mdp5_plane->base;

	mdp5_plane->pipe = pipe;
	mdp5_plane->name = pipe2name(pipe);

	mdp5_plane->nformats = mdp5_get_formats(pipe, mdp5_plane->formats,
			ARRAY_SIZE(mdp5_plane->formats));

	mdp5_plane->flush_mask = mdp_ctl_flush_mask_pipe(pipe);
	mdp5_plane->reg_offset = reg_offset;
	spin_lock_init(&mdp5_plane->pipe_lock);

	type = private_plane ? DRM_PLANE_TYPE_PRIMARY : DRM_PLANE_TYPE_OVERLAY;
	ret = drm_universal_plane_init(dev, plane, 0xff, &mdp5_plane_funcs,
				 mdp5_plane->formats, mdp5_plane->nformats,
				 type);
	if (ret)
		goto fail;

	drm_plane_helper_add(plane, &mdp5_plane_helper_funcs);

	mdp5_plane_install_properties(plane, &plane->base);

	return plane;

fail:
	if (plane)
		mdp5_plane_destroy(plane);

	return ERR_PTR(ret);
}
