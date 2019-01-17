// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX IPUv3 DP Overlay Planes
 *
 * Copyright (C) 2013 Philipp Zabel, Pengutronix
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>

#include "video/imx-ipu-v3.h"
#include "imx-drm.h"
#include "ipuv3-plane.h"

struct ipu_plane_state {
	struct drm_plane_state base;
	bool use_pre;
};

static inline struct ipu_plane_state *
to_ipu_plane_state(struct drm_plane_state *p)
{
	return container_of(p, struct ipu_plane_state, base);
}

static inline struct ipu_plane *to_ipu_plane(struct drm_plane *p)
{
	return container_of(p, struct ipu_plane, base);
}

static const uint32_t ipu_plane_formats[] = {
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YVU444,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB565_A8,
	DRM_FORMAT_BGR565_A8,
	DRM_FORMAT_RGB888_A8,
	DRM_FORMAT_BGR888_A8,
	DRM_FORMAT_RGBX8888_A8,
	DRM_FORMAT_BGRX8888_A8,
};

static const uint64_t ipu_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static const uint64_t pre_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_VIVANTE_TILED,
	DRM_FORMAT_MOD_VIVANTE_SUPER_TILED,
	DRM_FORMAT_MOD_INVALID
};

int ipu_plane_irq(struct ipu_plane *ipu_plane)
{
	return ipu_idmac_channel_irq(ipu_plane->ipu, ipu_plane->ipu_ch,
				     IPU_IRQ_EOF);
}

static inline unsigned long
drm_plane_state_to_eba(struct drm_plane_state *state, int plane)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *cma_obj;
	int x = state->src.x1 >> 16;
	int y = state->src.y1 >> 16;

	cma_obj = drm_fb_cma_get_gem_obj(fb, plane);
	BUG_ON(!cma_obj);

	return cma_obj->paddr + fb->offsets[plane] + fb->pitches[plane] * y +
	       fb->format->cpp[plane] * x;
}

static inline unsigned long
drm_plane_state_to_ubo(struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *cma_obj;
	unsigned long eba = drm_plane_state_to_eba(state, 0);
	int x = state->src.x1 >> 16;
	int y = state->src.y1 >> 16;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 1);
	BUG_ON(!cma_obj);

	x /= drm_format_horz_chroma_subsampling(fb->format->format);
	y /= drm_format_vert_chroma_subsampling(fb->format->format);

	return cma_obj->paddr + fb->offsets[1] + fb->pitches[1] * y +
	       fb->format->cpp[1] * x - eba;
}

static inline unsigned long
drm_plane_state_to_vbo(struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *cma_obj;
	unsigned long eba = drm_plane_state_to_eba(state, 0);
	int x = state->src.x1 >> 16;
	int y = state->src.y1 >> 16;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 2);
	BUG_ON(!cma_obj);

	x /= drm_format_horz_chroma_subsampling(fb->format->format);
	y /= drm_format_vert_chroma_subsampling(fb->format->format);

	return cma_obj->paddr + fb->offsets[2] + fb->pitches[2] * y +
	       fb->format->cpp[2] * x - eba;
}

void ipu_plane_put_resources(struct ipu_plane *ipu_plane)
{
	if (!IS_ERR_OR_NULL(ipu_plane->dp))
		ipu_dp_put(ipu_plane->dp);
	if (!IS_ERR_OR_NULL(ipu_plane->dmfc))
		ipu_dmfc_put(ipu_plane->dmfc);
	if (!IS_ERR_OR_NULL(ipu_plane->ipu_ch))
		ipu_idmac_put(ipu_plane->ipu_ch);
	if (!IS_ERR_OR_NULL(ipu_plane->alpha_ch))
		ipu_idmac_put(ipu_plane->alpha_ch);
}

int ipu_plane_get_resources(struct ipu_plane *ipu_plane)
{
	int ret;
	int alpha_ch;

	ipu_plane->ipu_ch = ipu_idmac_get(ipu_plane->ipu, ipu_plane->dma);
	if (IS_ERR(ipu_plane->ipu_ch)) {
		ret = PTR_ERR(ipu_plane->ipu_ch);
		DRM_ERROR("failed to get idmac channel: %d\n", ret);
		return ret;
	}

	alpha_ch = ipu_channel_alpha_channel(ipu_plane->dma);
	if (alpha_ch >= 0) {
		ipu_plane->alpha_ch = ipu_idmac_get(ipu_plane->ipu, alpha_ch);
		if (IS_ERR(ipu_plane->alpha_ch)) {
			ret = PTR_ERR(ipu_plane->alpha_ch);
			DRM_ERROR("failed to get alpha idmac channel %d: %d\n",
				  alpha_ch, ret);
			return ret;
		}
	}

	ipu_plane->dmfc = ipu_dmfc_get(ipu_plane->ipu, ipu_plane->dma);
	if (IS_ERR(ipu_plane->dmfc)) {
		ret = PTR_ERR(ipu_plane->dmfc);
		DRM_ERROR("failed to get dmfc: ret %d\n", ret);
		goto err_out;
	}

	if (ipu_plane->dp_flow >= 0) {
		ipu_plane->dp = ipu_dp_get(ipu_plane->ipu, ipu_plane->dp_flow);
		if (IS_ERR(ipu_plane->dp)) {
			ret = PTR_ERR(ipu_plane->dp);
			DRM_ERROR("failed to get dp flow: %d\n", ret);
			goto err_out;
		}
	}

	return 0;
err_out:
	ipu_plane_put_resources(ipu_plane);

	return ret;
}

static bool ipu_plane_separate_alpha(struct ipu_plane *ipu_plane)
{
	switch (ipu_plane->base.state->fb->format->format) {
	case DRM_FORMAT_RGB565_A8:
	case DRM_FORMAT_BGR565_A8:
	case DRM_FORMAT_RGB888_A8:
	case DRM_FORMAT_BGR888_A8:
	case DRM_FORMAT_RGBX8888_A8:
	case DRM_FORMAT_BGRX8888_A8:
		return true;
	default:
		return false;
	}
}

static void ipu_plane_enable(struct ipu_plane *ipu_plane)
{
	if (ipu_plane->dp)
		ipu_dp_enable(ipu_plane->ipu);
	ipu_dmfc_enable_channel(ipu_plane->dmfc);
	ipu_idmac_enable_channel(ipu_plane->ipu_ch);
	if (ipu_plane_separate_alpha(ipu_plane))
		ipu_idmac_enable_channel(ipu_plane->alpha_ch);
	if (ipu_plane->dp)
		ipu_dp_enable_channel(ipu_plane->dp);
}

void ipu_plane_disable(struct ipu_plane *ipu_plane, bool disable_dp_channel)
{
	int ret;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	ret = ipu_idmac_wait_busy(ipu_plane->ipu_ch, 50);
	if (ret == -ETIMEDOUT) {
		DRM_ERROR("[PLANE:%d] IDMAC timeout\n",
			  ipu_plane->base.base.id);
	}

	if (ipu_plane->dp && disable_dp_channel)
		ipu_dp_disable_channel(ipu_plane->dp, false);
	ipu_idmac_disable_channel(ipu_plane->ipu_ch);
	if (ipu_plane->alpha_ch)
		ipu_idmac_disable_channel(ipu_plane->alpha_ch);
	ipu_dmfc_disable_channel(ipu_plane->dmfc);
	if (ipu_plane->dp)
		ipu_dp_disable(ipu_plane->ipu);
	if (ipu_prg_present(ipu_plane->ipu))
		ipu_prg_channel_disable(ipu_plane->ipu_ch);
}

void ipu_plane_disable_deferred(struct drm_plane *plane)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);

	if (ipu_plane->disabling) {
		ipu_plane->disabling = false;
		ipu_plane_disable(ipu_plane, false);
	}
}
EXPORT_SYMBOL_GPL(ipu_plane_disable_deferred);

static void ipu_plane_destroy(struct drm_plane *plane)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	drm_plane_cleanup(plane);
	kfree(ipu_plane);
}

static void ipu_plane_state_reset(struct drm_plane *plane)
{
	unsigned int zpos = (plane->type == DRM_PLANE_TYPE_PRIMARY) ? 0 : 1;
	struct ipu_plane_state *ipu_state;

	if (plane->state) {
		ipu_state = to_ipu_plane_state(plane->state);
		__drm_atomic_helper_plane_destroy_state(plane->state);
		kfree(ipu_state);
		plane->state = NULL;
	}

	ipu_state = kzalloc(sizeof(*ipu_state), GFP_KERNEL);

	if (ipu_state) {
		__drm_atomic_helper_plane_reset(plane, &ipu_state->base);
		ipu_state->base.zpos = zpos;
		ipu_state->base.normalized_zpos = zpos;
	}
}

static struct drm_plane_state *
ipu_plane_duplicate_state(struct drm_plane *plane)
{
	struct ipu_plane_state *state;

	if (WARN_ON(!plane->state))
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state)
		__drm_atomic_helper_plane_duplicate_state(plane, &state->base);

	return &state->base;
}

static void ipu_plane_destroy_state(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct ipu_plane_state *ipu_state = to_ipu_plane_state(state);

	__drm_atomic_helper_plane_destroy_state(state);
	kfree(ipu_state);
}

static bool ipu_plane_format_mod_supported(struct drm_plane *plane,
					   uint32_t format, uint64_t modifier)
{
	struct ipu_soc *ipu = to_ipu_plane(plane)->ipu;

	/* linear is supported for all planes and formats */
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	/* without a PRG there are no supported modifiers */
	if (!ipu_prg_present(ipu))
		return false;

	return ipu_prg_format_supported(ipu, format, modifier);
}

static const struct drm_plane_funcs ipu_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy	= ipu_plane_destroy,
	.reset		= ipu_plane_state_reset,
	.atomic_duplicate_state	= ipu_plane_duplicate_state,
	.atomic_destroy_state	= ipu_plane_destroy_state,
	.format_mod_supported = ipu_plane_format_mod_supported,
};

static int ipu_plane_atomic_check(struct drm_plane *plane,
				  struct drm_plane_state *state)
{
	struct drm_plane_state *old_state = plane->state;
	struct drm_crtc_state *crtc_state;
	struct device *dev = plane->dev->dev;
	struct drm_framebuffer *fb = state->fb;
	struct drm_framebuffer *old_fb = old_state->fb;
	unsigned long eba, ubo, vbo, old_ubo, old_vbo, alpha_eba;
	bool can_position = (plane->type == DRM_PLANE_TYPE_OVERLAY);
	int hsub, vsub;
	int ret;

	/* Ok to disable */
	if (!fb)
		return 0;

	if (!state->crtc)
		return -EINVAL;

	crtc_state =
		drm_atomic_get_existing_crtc_state(state->state, state->crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	ret = drm_atomic_helper_check_plane_state(state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  can_position, true);
	if (ret)
		return ret;

	/* nothing to check when disabling or disabled */
	if (!crtc_state->enable)
		return 0;

	switch (plane->type) {
	case DRM_PLANE_TYPE_PRIMARY:
		/* full plane minimum width is 13 pixels */
		if (drm_rect_width(&state->dst) < 13)
			return -EINVAL;
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		break;
	default:
		dev_warn(dev, "Unsupported plane type %d\n", plane->type);
		return -EINVAL;
	}

	if (drm_rect_height(&state->dst) < 2)
		return -EINVAL;

	/*
	 * We support resizing active plane or changing its format by
	 * forcing CRTC mode change in plane's ->atomic_check callback
	 * and disabling all affected active planes in CRTC's ->atomic_disable
	 * callback.  The planes will be reenabled in plane's ->atomic_update
	 * callback.
	 */
	if (old_fb &&
	    (drm_rect_width(&state->dst) != drm_rect_width(&old_state->dst) ||
	     drm_rect_height(&state->dst) != drm_rect_height(&old_state->dst) ||
	     fb->format != old_fb->format))
		crtc_state->mode_changed = true;

	eba = drm_plane_state_to_eba(state, 0);

	if (eba & 0x7)
		return -EINVAL;

	if (fb->pitches[0] < 1 || fb->pitches[0] > 16384)
		return -EINVAL;

	if (old_fb && fb->pitches[0] != old_fb->pitches[0])
		crtc_state->mode_changed = true;

	switch (fb->format->format) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		/*
		 * Multiplanar formats have to meet the following restrictions:
		 * - The (up to) three plane addresses are EBA, EBA+UBO, EBA+VBO
		 * - EBA, UBO and VBO are a multiple of 8
		 * - UBO and VBO are unsigned and not larger than 0xfffff8
		 * - Only EBA may be changed while scanout is active
		 * - The strides of U and V planes must be identical.
		 */
		vbo = drm_plane_state_to_vbo(state);

		if (vbo & 0x7 || vbo > 0xfffff8)
			return -EINVAL;

		if (old_fb && (fb->format == old_fb->format)) {
			old_vbo = drm_plane_state_to_vbo(old_state);
			if (vbo != old_vbo)
				crtc_state->mode_changed = true;
		}

		if (fb->pitches[1] != fb->pitches[2])
			return -EINVAL;

		/* fall-through */
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
		ubo = drm_plane_state_to_ubo(state);

		if (ubo & 0x7 || ubo > 0xfffff8)
			return -EINVAL;

		if (old_fb && (fb->format == old_fb->format)) {
			old_ubo = drm_plane_state_to_ubo(old_state);
			if (ubo != old_ubo)
				crtc_state->mode_changed = true;
		}

		if (fb->pitches[1] < 1 || fb->pitches[1] > 16384)
			return -EINVAL;

		if (old_fb && old_fb->pitches[1] != fb->pitches[1])
			crtc_state->mode_changed = true;

		/*
		 * The x/y offsets must be even in case of horizontal/vertical
		 * chroma subsampling.
		 */
		hsub = drm_format_horz_chroma_subsampling(fb->format->format);
		vsub = drm_format_vert_chroma_subsampling(fb->format->format);
		if (((state->src.x1 >> 16) & (hsub - 1)) ||
		    ((state->src.y1 >> 16) & (vsub - 1)))
			return -EINVAL;
		break;
	case DRM_FORMAT_RGB565_A8:
	case DRM_FORMAT_BGR565_A8:
	case DRM_FORMAT_RGB888_A8:
	case DRM_FORMAT_BGR888_A8:
	case DRM_FORMAT_RGBX8888_A8:
	case DRM_FORMAT_BGRX8888_A8:
		alpha_eba = drm_plane_state_to_eba(state, 1);
		if (alpha_eba & 0x7)
			return -EINVAL;

		if (fb->pitches[1] < 1 || fb->pitches[1] > 16384)
			return -EINVAL;

		if (old_fb && old_fb->pitches[1] != fb->pitches[1])
			crtc_state->mode_changed = true;
		break;
	}

	return 0;
}

static void ipu_plane_atomic_disable(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);

	if (ipu_plane->dp)
		ipu_dp_disable_channel(ipu_plane->dp, true);
	ipu_plane->disabling = true;
}

static int ipu_chan_assign_axi_id(int ipu_chan)
{
	switch (ipu_chan) {
	case IPUV3_CHANNEL_MEM_BG_SYNC:
		return 1;
	case IPUV3_CHANNEL_MEM_FG_SYNC:
		return 2;
	case IPUV3_CHANNEL_MEM_DC_SYNC:
		return 3;
	default:
		return 0;
	}
}

static void ipu_calculate_bursts(u32 width, u32 cpp, u32 stride,
				 u8 *burstsize, u8 *num_bursts)
{
	const unsigned int width_bytes = width * cpp;
	unsigned int npb, bursts;

	/* Maximum number of pixels per burst without overshooting stride */
	for (npb = 64 / cpp; npb > 0; --npb) {
		if (round_up(width_bytes, npb * cpp) <= stride)
			break;
	}
	*burstsize = npb;

	/* Maximum number of consecutive bursts without overshooting stride */
	for (bursts = 8; bursts > 1; bursts /= 2) {
		if (round_up(width_bytes, npb * cpp * bursts) <= stride)
			break;
	}
	*num_bursts = bursts;
}

static void ipu_plane_atomic_update(struct drm_plane *plane,
				    struct drm_plane_state *old_state)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct ipu_plane_state *ipu_state = to_ipu_plane_state(state);
	struct drm_crtc_state *crtc_state = state->crtc->state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect *dst = &state->dst;
	unsigned long eba, ubo, vbo;
	unsigned long alpha_eba = 0;
	enum ipu_color_space ics;
	unsigned int axi_id = 0;
	const struct drm_format_info *info;
	u8 burstsize, num_bursts;
	u32 width, height;
	int active;

	if (ipu_plane->dp_flow == IPU_DP_FLOW_SYNC_FG)
		ipu_dp_set_window_pos(ipu_plane->dp, dst->x1, dst->y1);

	switch (ipu_plane->dp_flow) {
	case IPU_DP_FLOW_SYNC_BG:
		if (state->normalized_zpos == 1) {
			ipu_dp_set_global_alpha(ipu_plane->dp,
						!fb->format->has_alpha, 0xff,
						true);
		} else {
			ipu_dp_set_global_alpha(ipu_plane->dp, true, 0, true);
		}
		break;
	case IPU_DP_FLOW_SYNC_FG:
		if (state->normalized_zpos == 1) {
			ipu_dp_set_global_alpha(ipu_plane->dp,
						!fb->format->has_alpha, 0xff,
						false);
		}
		break;
	}

	eba = drm_plane_state_to_eba(state, 0);

	/*
	 * Configure PRG channel and attached PRE, this changes the EBA to an
	 * internal SRAM location.
	 */
	if (ipu_state->use_pre) {
		axi_id = ipu_chan_assign_axi_id(ipu_plane->dma);
		ipu_prg_channel_configure(ipu_plane->ipu_ch, axi_id,
					  drm_rect_width(&state->src) >> 16,
					  drm_rect_height(&state->src) >> 16,
					  fb->pitches[0], fb->format->format,
					  fb->modifier, &eba);
	}

	if (old_state->fb && !drm_atomic_crtc_needs_modeset(crtc_state)) {
		/* nothing to do if PRE is used */
		if (ipu_state->use_pre)
			return;
		active = ipu_idmac_get_current_buffer(ipu_plane->ipu_ch);
		ipu_cpmem_set_buffer(ipu_plane->ipu_ch, !active, eba);
		ipu_idmac_select_buffer(ipu_plane->ipu_ch, !active);
		if (ipu_plane_separate_alpha(ipu_plane)) {
			active = ipu_idmac_get_current_buffer(ipu_plane->alpha_ch);
			ipu_cpmem_set_buffer(ipu_plane->alpha_ch, !active,
					     alpha_eba);
			ipu_idmac_select_buffer(ipu_plane->alpha_ch, !active);
		}
		return;
	}

	ics = ipu_drm_fourcc_to_colorspace(fb->format->format);
	switch (ipu_plane->dp_flow) {
	case IPU_DP_FLOW_SYNC_BG:
		ipu_dp_setup_channel(ipu_plane->dp, ics, IPUV3_COLORSPACE_RGB);
		break;
	case IPU_DP_FLOW_SYNC_FG:
		ipu_dp_setup_channel(ipu_plane->dp, ics,
					IPUV3_COLORSPACE_UNKNOWN);
		break;
	}

	ipu_dmfc_config_wait4eot(ipu_plane->dmfc, drm_rect_width(dst));

	width = drm_rect_width(&state->src) >> 16;
	height = drm_rect_height(&state->src) >> 16;
	info = drm_format_info(fb->format->format);
	ipu_calculate_bursts(width, info->cpp[0], fb->pitches[0],
			     &burstsize, &num_bursts);

	ipu_cpmem_zero(ipu_plane->ipu_ch);
	ipu_cpmem_set_resolution(ipu_plane->ipu_ch, width, height);
	ipu_cpmem_set_fmt(ipu_plane->ipu_ch, fb->format->format);
	ipu_cpmem_set_burstsize(ipu_plane->ipu_ch, burstsize);
	ipu_cpmem_set_high_priority(ipu_plane->ipu_ch);
	ipu_idmac_enable_watermark(ipu_plane->ipu_ch, true);
	ipu_idmac_set_double_buffer(ipu_plane->ipu_ch, 1);
	ipu_cpmem_set_stride(ipu_plane->ipu_ch, fb->pitches[0]);
	ipu_cpmem_set_axi_id(ipu_plane->ipu_ch, axi_id);

	switch (fb->format->format) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		ubo = drm_plane_state_to_ubo(state);
		vbo = drm_plane_state_to_vbo(state);
		if (fb->format->format == DRM_FORMAT_YVU420 ||
		    fb->format->format == DRM_FORMAT_YVU422 ||
		    fb->format->format == DRM_FORMAT_YVU444)
			swap(ubo, vbo);

		ipu_cpmem_set_yuv_planar_full(ipu_plane->ipu_ch,
					      fb->pitches[1], ubo, vbo);

		dev_dbg(ipu_plane->base.dev->dev,
			"phy = %lu %lu %lu, x = %d, y = %d", eba, ubo, vbo,
			state->src.x1 >> 16, state->src.y1 >> 16);
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
		ubo = drm_plane_state_to_ubo(state);

		ipu_cpmem_set_yuv_planar_full(ipu_plane->ipu_ch,
					      fb->pitches[1], ubo, ubo);

		dev_dbg(ipu_plane->base.dev->dev,
			"phy = %lu %lu, x = %d, y = %d", eba, ubo,
			state->src.x1 >> 16, state->src.y1 >> 16);
		break;
	case DRM_FORMAT_RGB565_A8:
	case DRM_FORMAT_BGR565_A8:
	case DRM_FORMAT_RGB888_A8:
	case DRM_FORMAT_BGR888_A8:
	case DRM_FORMAT_RGBX8888_A8:
	case DRM_FORMAT_BGRX8888_A8:
		alpha_eba = drm_plane_state_to_eba(state, 1);
		num_bursts = 0;

		dev_dbg(ipu_plane->base.dev->dev, "phys = %lu %lu, x = %d, y = %d",
			eba, alpha_eba, state->src.x1 >> 16, state->src.y1 >> 16);

		ipu_cpmem_set_burstsize(ipu_plane->ipu_ch, 16);

		ipu_cpmem_zero(ipu_plane->alpha_ch);
		ipu_cpmem_set_resolution(ipu_plane->alpha_ch,
					 drm_rect_width(&state->src) >> 16,
					 drm_rect_height(&state->src) >> 16);
		ipu_cpmem_set_format_passthrough(ipu_plane->alpha_ch, 8);
		ipu_cpmem_set_high_priority(ipu_plane->alpha_ch);
		ipu_idmac_set_double_buffer(ipu_plane->alpha_ch, 1);
		ipu_cpmem_set_stride(ipu_plane->alpha_ch, fb->pitches[1]);
		ipu_cpmem_set_burstsize(ipu_plane->alpha_ch, 16);
		ipu_cpmem_set_buffer(ipu_plane->alpha_ch, 0, alpha_eba);
		ipu_cpmem_set_buffer(ipu_plane->alpha_ch, 1, alpha_eba);
		break;
	default:
		dev_dbg(ipu_plane->base.dev->dev, "phys = %lu, x = %d, y = %d",
			eba, state->src.x1 >> 16, state->src.y1 >> 16);
		break;
	}
	ipu_cpmem_set_buffer(ipu_plane->ipu_ch, 0, eba);
	ipu_cpmem_set_buffer(ipu_plane->ipu_ch, 1, eba);
	ipu_idmac_lock_enable(ipu_plane->ipu_ch, num_bursts);
	ipu_plane_enable(ipu_plane);
}

static const struct drm_plane_helper_funcs ipu_plane_helper_funcs = {
	.prepare_fb = drm_gem_fb_prepare_fb,
	.atomic_check = ipu_plane_atomic_check,
	.atomic_disable = ipu_plane_atomic_disable,
	.atomic_update = ipu_plane_atomic_update,
};

bool ipu_plane_atomic_update_pending(struct drm_plane *plane)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct ipu_plane_state *ipu_state = to_ipu_plane_state(state);

	/* disabled crtcs must not block the update */
	if (!state->crtc)
		return false;

	if (ipu_state->use_pre)
		return ipu_prg_channel_configure_pending(ipu_plane->ipu_ch);

	/*
	 * Pretend no update is pending in the non-PRE/PRG case. For this to
	 * happen, an atomic update would have to be deferred until after the
	 * start of the next frame and simultaneously interrupt latency would
	 * have to be high enough to let the atomic update finish and issue an
	 * event before the previous end of frame interrupt handler can be
	 * executed.
	 */
	return false;
}
int ipu_planes_assign_pre(struct drm_device *dev,
			  struct drm_atomic_state *state)
{
	struct drm_crtc_state *old_crtc_state, *crtc_state;
	struct drm_plane_state *plane_state;
	struct ipu_plane_state *ipu_state;
	struct ipu_plane *ipu_plane;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int available_pres = ipu_prg_max_active_channels();
	int ret, i;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, crtc_state, i) {
		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret)
			return ret;
	}

	/*
	 * We are going over the planes in 2 passes: first we assign PREs to
	 * planes with a tiling modifier, which need the PREs to resolve into
	 * linear. Any failure to assign a PRE there is fatal. In the second
	 * pass we try to assign PREs to linear FBs, to improve memory access
	 * patterns for them. Failure at this point is non-fatal, as we can
	 * scan out linear FBs without a PRE.
	 */
	for_each_new_plane_in_state(state, plane, plane_state, i) {
		ipu_state = to_ipu_plane_state(plane_state);
		ipu_plane = to_ipu_plane(plane);

		if (!plane_state->fb) {
			ipu_state->use_pre = false;
			continue;
		}

		if (!(plane_state->fb->flags & DRM_MODE_FB_MODIFIERS) ||
		    plane_state->fb->modifier == DRM_FORMAT_MOD_LINEAR)
			continue;

		if (!ipu_prg_present(ipu_plane->ipu) || !available_pres)
			return -EINVAL;

		if (!ipu_prg_format_supported(ipu_plane->ipu,
					      plane_state->fb->format->format,
					      plane_state->fb->modifier))
			return -EINVAL;

		ipu_state->use_pre = true;
		available_pres--;
	}

	for_each_new_plane_in_state(state, plane, plane_state, i) {
		ipu_state = to_ipu_plane_state(plane_state);
		ipu_plane = to_ipu_plane(plane);

		if (!plane_state->fb) {
			ipu_state->use_pre = false;
			continue;
		}

		if ((plane_state->fb->flags & DRM_MODE_FB_MODIFIERS) &&
		    plane_state->fb->modifier != DRM_FORMAT_MOD_LINEAR)
			continue;

		/* make sure that modifier is initialized */
		plane_state->fb->modifier = DRM_FORMAT_MOD_LINEAR;

		if (ipu_prg_present(ipu_plane->ipu) && available_pres &&
		    ipu_prg_format_supported(ipu_plane->ipu,
					     plane_state->fb->format->format,
					     plane_state->fb->modifier)) {
			ipu_state->use_pre = true;
			available_pres--;
		} else {
			ipu_state->use_pre = false;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ipu_planes_assign_pre);

struct ipu_plane *ipu_plane_init(struct drm_device *dev, struct ipu_soc *ipu,
				 int dma, int dp, unsigned int possible_crtcs,
				 enum drm_plane_type type)
{
	struct ipu_plane *ipu_plane;
	const uint64_t *modifiers = ipu_format_modifiers;
	unsigned int zpos = (type == DRM_PLANE_TYPE_PRIMARY) ? 0 : 1;
	int ret;

	DRM_DEBUG_KMS("channel %d, dp flow %d, possible_crtcs=0x%x\n",
		      dma, dp, possible_crtcs);

	ipu_plane = kzalloc(sizeof(*ipu_plane), GFP_KERNEL);
	if (!ipu_plane) {
		DRM_ERROR("failed to allocate plane\n");
		return ERR_PTR(-ENOMEM);
	}

	ipu_plane->ipu = ipu;
	ipu_plane->dma = dma;
	ipu_plane->dp_flow = dp;

	if (ipu_prg_present(ipu))
		modifiers = pre_format_modifiers;

	ret = drm_universal_plane_init(dev, &ipu_plane->base, possible_crtcs,
				       &ipu_plane_funcs, ipu_plane_formats,
				       ARRAY_SIZE(ipu_plane_formats),
				       modifiers, type, NULL);
	if (ret) {
		DRM_ERROR("failed to initialize plane\n");
		kfree(ipu_plane);
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(&ipu_plane->base, &ipu_plane_helper_funcs);

	if (dp == IPU_DP_FLOW_SYNC_BG || dp == IPU_DP_FLOW_SYNC_FG)
		drm_plane_create_zpos_property(&ipu_plane->base, zpos, 0, 1);
	else
		drm_plane_create_zpos_immutable_property(&ipu_plane->base, 0);

	return ipu_plane;
}
