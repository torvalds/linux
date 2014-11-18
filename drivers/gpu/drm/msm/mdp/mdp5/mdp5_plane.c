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

#define MAX_PLANE	4

struct mdp5_plane {
	struct drm_plane base;
	const char *name;

	enum mdp5_pipe pipe;

	spinlock_t pipe_lock;	/* protect REG_MDP5_PIPE_* registers */
	uint32_t reg_offset;

	uint32_t flush_mask;	/* used to commit pipe registers */

	struct mdp5_overlay_info overlay_info;

	uint32_t nformats;
	uint32_t formats[32];

	bool enabled;
};
#define to_mdp5_plane(x) container_of(x, struct mdp5_plane, base)

static struct mdp5_kms *get_kms(struct drm_plane *plane)
{
	struct msm_drm_private *priv = plane->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

static int mdp5_plane_update(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);

	mdp5_plane->enabled = true;

	if (plane->fb)
		drm_framebuffer_unreference(plane->fb);

	drm_framebuffer_reference(fb);

	return mdp5_plane_mode_set(plane, crtc, fb,
			crtc_x, crtc_y, crtc_w, crtc_h,
			src_x, src_y, src_w, src_h);
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

	/* TODO detaching now will cause us not to get the last
	 * vblank and mdp5_smp_commit().. so other planes will
	 * still see smp blocks previously allocated to us as
	 * in-use..
	 */
	if (plane->crtc)
		mdp5_crtc_detach(plane->crtc, plane);

	return 0;
}

static void mdp5_plane_destroy(struct drm_plane *plane)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct msm_drm_private *priv = plane->dev->dev_private;

	if (priv->kms)
		mdp5_plane_disable(plane);

	drm_plane_cleanup(plane);

	kfree(mdp5_plane);
}

void mdp5_plane_set_overlay_info(struct drm_plane *plane,
		const struct mdp5_overlay_info *overlay_info)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);

	memcpy(&mdp5_plane->overlay_info, overlay_info, sizeof(*overlay_info));
}

struct mdp5_overlay_info *mdp5_plane_get_overlay_info(
		struct drm_plane *plane)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);

	return &mdp5_plane->overlay_info;
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

static const struct drm_plane_funcs mdp5_plane_funcs = {
		.update_plane = mdp5_plane_update,
		.disable_plane = mdp5_plane_disable,
		.destroy = mdp5_plane_destroy,
		.set_property = mdp5_plane_set_property,
};

static int get_fb_addr(struct drm_plane *plane, struct drm_framebuffer *fb,
		uint32_t iova[MAX_PLANE])
{
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	uint32_t nplanes = drm_format_num_planes(fb->pixel_format);
	int i;

	for (i = 0; i < nplanes; i++) {
		struct drm_gem_object *bo = msm_framebuffer_bo(fb, i);
		msm_gem_get_iova(bo, mdp5_kms->id, &iova[i]);
	}
	for (; i < MAX_PLANE; i++)
		iova[i] = 0;

	return 0;
}

static void set_scanout_locked(struct drm_plane *plane,
		uint32_t pitches[MAX_PLANE], uint32_t src_addr[MAX_PLANE])
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	enum mdp5_pipe pipe = mdp5_plane->pipe;

	WARN_ON(!spin_is_locked(&mdp5_plane->pipe_lock));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_STRIDE_A(pipe),
			MDP5_PIPE_SRC_STRIDE_A_P0(pitches[0]) |
			MDP5_PIPE_SRC_STRIDE_A_P1(pitches[1]));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_STRIDE_B(pipe),
			MDP5_PIPE_SRC_STRIDE_B_P2(pitches[2]) |
			MDP5_PIPE_SRC_STRIDE_B_P3(pitches[3]));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC0_ADDR(pipe), src_addr[0]);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC1_ADDR(pipe), src_addr[1]);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC2_ADDR(pipe), src_addr[2]);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC3_ADDR(pipe), src_addr[3]);
}

void mdp5_plane_set_scanout(struct drm_plane *plane,
		struct drm_framebuffer *fb)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	uint32_t src_addr[MAX_PLANE];
	unsigned long flags;

	get_fb_addr(plane, fb, src_addr);

	spin_lock_irqsave(&mdp5_plane->pipe_lock, flags);
	set_scanout_locked(plane, fb->pitches, src_addr);
	spin_unlock_irqrestore(&mdp5_plane->pipe_lock, flags);

	plane->fb = fb;
}

int mdp5_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	struct mdp5_plane *mdp5_plane = to_mdp5_plane(plane);
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	enum mdp5_pipe pipe = mdp5_plane->pipe;
	const struct mdp_format *format;
	uint32_t nplanes, config = 0;
	uint32_t phasex_step = 0, phasey_step = 0;
	uint32_t hdecm = 0, vdecm = 0;
	uint32_t src_addr[MAX_PLANE];
	unsigned long flags;
	int ret;

	nplanes = drm_format_num_planes(fb->pixel_format);

	/* bad formats should already be rejected: */
	if (WARN_ON(nplanes > pipe2nclients(pipe)))
		return -EINVAL;

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

	if (src_w != crtc_w) {
		config |= MDP5_PIPE_SCALE_CONFIG_SCALEX_EN;
		/* TODO calc phasex_step, hdecm */
	}

	if (src_h != crtc_h) {
		config |= MDP5_PIPE_SCALE_CONFIG_SCALEY_EN;
		/* TODO calc phasey_step, vdecm */
	}

	ret = get_fb_addr(plane, fb, src_addr);
	if (ret)
		return ret;

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

	format = to_mdp_format(msm_framebuffer_format(fb));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_FORMAT(pipe),
			MDP5_PIPE_SRC_FORMAT_A_BPC(format->bpc_a) |
			MDP5_PIPE_SRC_FORMAT_R_BPC(format->bpc_r) |
			MDP5_PIPE_SRC_FORMAT_G_BPC(format->bpc_g) |
			MDP5_PIPE_SRC_FORMAT_B_BPC(format->bpc_b) |
			COND(format->alpha_enable, MDP5_PIPE_SRC_FORMAT_ALPHA_ENABLE) |
			MDP5_PIPE_SRC_FORMAT_CPP(format->cpp - 1) |
			MDP5_PIPE_SRC_FORMAT_UNPACK_COUNT(format->unpack_count - 1) |
			COND(format->unpack_tight, MDP5_PIPE_SRC_FORMAT_UNPACK_TIGHT) |
			MDP5_PIPE_SRC_FORMAT_NUM_PLANES(nplanes - 1) |
			MDP5_PIPE_SRC_FORMAT_CHROMA_SAMP(CHROMA_RGB));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_UNPACK(pipe),
			MDP5_PIPE_SRC_UNPACK_ELEM0(format->unpack[0]) |
			MDP5_PIPE_SRC_UNPACK_ELEM1(format->unpack[1]) |
			MDP5_PIPE_SRC_UNPACK_ELEM2(format->unpack[2]) |
			MDP5_PIPE_SRC_UNPACK_ELEM3(format->unpack[3]));

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_OP_MODE(pipe),
			MDP5_PIPE_SRC_OP_MODE_BWC(BWC_LOSSLESS));

	/* not using secure mode: */
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SRC_ADDR_SW_STATUS(pipe), 0);

	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_PHASE_STEP_X(pipe), phasex_step);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_PHASE_STEP_Y(pipe), phasey_step);
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_DECIMATION(pipe),
			MDP5_PIPE_DECIMATION_VERT(vdecm) |
			MDP5_PIPE_DECIMATION_HORZ(hdecm));
	mdp5_write(mdp5_kms, REG_MDP5_PIPE_SCALE_CONFIG(pipe),
			MDP5_PIPE_SCALE_CONFIG_SCALEX_MIN_FILTER(SCALE_FILTER_NEAREST) |
			MDP5_PIPE_SCALE_CONFIG_SCALEY_MIN_FILTER(SCALE_FILTER_NEAREST) |
			MDP5_PIPE_SCALE_CONFIG_SCALEX_CR_FILTER(SCALE_FILTER_NEAREST) |
			MDP5_PIPE_SCALE_CONFIG_SCALEY_CR_FILTER(SCALE_FILTER_NEAREST) |
			MDP5_PIPE_SCALE_CONFIG_SCALEX_MAX_FILTER(SCALE_FILTER_NEAREST) |
			MDP5_PIPE_SCALE_CONFIG_SCALEY_MAX_FILTER(SCALE_FILTER_NEAREST));

	set_scanout_locked(plane, fb->pitches, src_addr);

	spin_unlock_irqrestore(&mdp5_plane->pipe_lock, flags);

	/* TODO detach from old crtc (if we had more than one) */
	ret = mdp5_crtc_attach(crtc, plane);

	return ret;
}

void mdp5_plane_complete_flip(struct drm_plane *plane)
{
	struct mdp5_kms *mdp5_kms = get_kms(plane);
	enum mdp5_pipe pipe = to_mdp5_plane(plane)->pipe;

	mdp5_smp_commit(mdp5_kms->smp, pipe);
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
	drm_universal_plane_init(dev, plane, 0xff, &mdp5_plane_funcs,
				 mdp5_plane->formats, mdp5_plane->nformats,
				 type);

	mdp5_plane_install_properties(plane, &plane->base);

	return plane;

fail:
	if (plane)
		mdp5_plane_destroy(plane);

	return ERR_PTR(ret);
}
