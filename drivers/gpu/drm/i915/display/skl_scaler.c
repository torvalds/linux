// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include "intel_de.h"
#include "intel_display_types.h"
#include "skl_scaler.h"
#include "skl_universal_plane.h"

/*
 * The hardware phase 0.0 refers to the center of the pixel.
 * We want to start from the top/left edge which is phase
 * -0.5. That matches how the hardware calculates the scaling
 * factors (from top-left of the first pixel to bottom-right
 * of the last pixel, as opposed to the pixel centers).
 *
 * For 4:2:0 subsampled chroma planes we obviously have to
 * adjust that so that the chroma sample position lands in
 * the right spot.
 *
 * Note that for packed YCbCr 4:2:2 formats there is no way to
 * control chroma siting. The hardware simply replicates the
 * chroma samples for both of the luma samples, and thus we don't
 * actually get the expected MPEG2 chroma siting convention :(
 * The same behaviour is observed on pre-SKL platforms as well.
 *
 * Theory behind the formula (note that we ignore sub-pixel
 * source coordinates):
 * s = source sample position
 * d = destination sample position
 *
 * Downscaling 4:1:
 * -0.5
 * | 0.0
 * | |     1.5 (initial phase)
 * | |     |
 * v v     v
 * | s | s | s | s |
 * |       d       |
 *
 * Upscaling 1:4:
 * -0.5
 * | -0.375 (initial phase)
 * | |     0.0
 * | |     |
 * v v     v
 * |       s       |
 * | d | d | d | d |
 */
static u16 skl_scaler_calc_phase(int sub, int scale, bool chroma_cosited)
{
	int phase = -0x8000;
	u16 trip = 0;

	if (chroma_cosited)
		phase += (sub - 1) * 0x8000 / sub;

	phase += scale / (2 * sub);

	/*
	 * Hardware initial phase limited to [-0.5:1.5].
	 * Since the max hardware scale factor is 3.0, we
	 * should never actually excdeed 1.0 here.
	 */
	WARN_ON(phase < -0x8000 || phase > 0x18000);

	if (phase < 0)
		phase = 0x10000 + phase;
	else
		trip = PS_PHASE_TRIP;

	return ((phase >> 2) & PS_PHASE_MASK) | trip;
}

#define SKL_MIN_SRC_W 8
#define SKL_MAX_SRC_W 4096
#define SKL_MIN_SRC_H 8
#define SKL_MAX_SRC_H 4096
#define SKL_MIN_DST_W 8
#define SKL_MAX_DST_W 4096
#define SKL_MIN_DST_H 8
#define SKL_MAX_DST_H 4096
#define ICL_MAX_SRC_W 5120
#define ICL_MAX_SRC_H 4096
#define ICL_MAX_DST_W 5120
#define ICL_MAX_DST_H 4096
#define SKL_MIN_YUV_420_SRC_W 16
#define SKL_MIN_YUV_420_SRC_H 16

static int
skl_update_scaler(struct intel_crtc_state *crtc_state, bool force_detach,
		  unsigned int scaler_user, int *scaler_id,
		  int src_w, int src_h, int dst_w, int dst_h,
		  const struct drm_format_info *format,
		  u64 modifier, bool need_scaler)
{
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_display_mode *adjusted_mode =
		&crtc_state->hw.adjusted_mode;

	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	if (src_w != dst_w || src_h != dst_h)
		need_scaler = true;

	/*
	 * Scaling/fitting not supported in IF-ID mode in GEN9+
	 * TODO: Interlace fetch mode doesn't support YUV420 planar formats.
	 * Once NV12 is enabled, handle it here while allocating scaler
	 * for NV12.
	 */
	if (DISPLAY_VER(dev_priv) >= 9 && crtc_state->hw.enable &&
	    need_scaler && adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE) {
		drm_dbg_kms(&dev_priv->drm,
			    "Pipe/Plane scaling not supported with IF-ID mode\n");
		return -EINVAL;
	}

	/*
	 * if plane is being disabled or scaler is no more required or force detach
	 *  - free scaler binded to this plane/crtc
	 *  - in order to do this, update crtc->scaler_usage
	 *
	 * Here scaler state in crtc_state is set free so that
	 * scaler can be assigned to other user. Actual register
	 * update to free the scaler is done in plane/panel-fit programming.
	 * For this purpose crtc/plane_state->scaler_id isn't reset here.
	 */
	if (force_detach || !need_scaler) {
		if (*scaler_id >= 0) {
			scaler_state->scaler_users &= ~(1 << scaler_user);
			scaler_state->scalers[*scaler_id].in_use = 0;

			drm_dbg_kms(&dev_priv->drm,
				    "scaler_user index %u.%u: "
				    "Staged freeing scaler id %d scaler_users = 0x%x\n",
				    crtc->pipe, scaler_user, *scaler_id,
				    scaler_state->scaler_users);
			*scaler_id = -1;
		}
		return 0;
	}

	if (format && intel_format_info_is_yuv_semiplanar(format, modifier) &&
	    (src_h < SKL_MIN_YUV_420_SRC_H || src_w < SKL_MIN_YUV_420_SRC_W)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Planar YUV: src dimensions not met\n");
		return -EINVAL;
	}

	/* range checks */
	if (src_w < SKL_MIN_SRC_W || src_h < SKL_MIN_SRC_H ||
	    dst_w < SKL_MIN_DST_W || dst_h < SKL_MIN_DST_H ||
	    (DISPLAY_VER(dev_priv) >= 11 &&
	     (src_w > ICL_MAX_SRC_W || src_h > ICL_MAX_SRC_H ||
	      dst_w > ICL_MAX_DST_W || dst_h > ICL_MAX_DST_H)) ||
	    (DISPLAY_VER(dev_priv) < 11 &&
	     (src_w > SKL_MAX_SRC_W || src_h > SKL_MAX_SRC_H ||
	      dst_w > SKL_MAX_DST_W || dst_h > SKL_MAX_DST_H)))	{
		drm_dbg_kms(&dev_priv->drm,
			    "scaler_user index %u.%u: src %ux%u dst %ux%u "
			    "size is out of scaler range\n",
			    crtc->pipe, scaler_user, src_w, src_h,
			    dst_w, dst_h);
		return -EINVAL;
	}

	/* mark this plane as a scaler user in crtc_state */
	scaler_state->scaler_users |= (1 << scaler_user);
	drm_dbg_kms(&dev_priv->drm, "scaler_user index %u.%u: "
		    "staged scaling request for %ux%u->%ux%u scaler_users = 0x%x\n",
		    crtc->pipe, scaler_user, src_w, src_h, dst_w, dst_h,
		    scaler_state->scaler_users);

	return 0;
}

int skl_update_scaler_crtc(struct intel_crtc_state *crtc_state)
{
	const struct drm_display_mode *pipe_mode = &crtc_state->hw.pipe_mode;
	int width, height;

	if (crtc_state->pch_pfit.enabled) {
		width = drm_rect_width(&crtc_state->pch_pfit.dst);
		height = drm_rect_height(&crtc_state->pch_pfit.dst);
	} else {
		width = pipe_mode->crtc_hdisplay;
		height = pipe_mode->crtc_vdisplay;
	}
	return skl_update_scaler(crtc_state, !crtc_state->hw.active,
				 SKL_CRTC_INDEX,
				 &crtc_state->scaler_state.scaler_id,
				 crtc_state->pipe_src_w, crtc_state->pipe_src_h,
				 width, height, NULL, 0,
				 crtc_state->pch_pfit.enabled);
}

/**
 * skl_update_scaler_plane - Stages update to scaler state for a given plane.
 * @crtc_state: crtc's scaler state
 * @plane_state: atomic plane state to update
 *
 * Return
 *     0 - scaler_usage updated successfully
 *    error - requested scaling cannot be supported or other error condition
 */
int skl_update_scaler_plane(struct intel_crtc_state *crtc_state,
			    struct intel_plane_state *plane_state)
{
	struct intel_plane *intel_plane =
		to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(intel_plane->base.dev);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	int ret;
	bool force_detach = !fb || !plane_state->uapi.visible;
	bool need_scaler = false;

	/* Pre-gen11 and SDR planes always need a scaler for planar formats. */
	if (!icl_is_hdr_plane(dev_priv, intel_plane->id) &&
	    fb && intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier))
		need_scaler = true;

	ret = skl_update_scaler(crtc_state, force_detach,
				drm_plane_index(&intel_plane->base),
				&plane_state->scaler_id,
				drm_rect_width(&plane_state->uapi.src) >> 16,
				drm_rect_height(&plane_state->uapi.src) >> 16,
				drm_rect_width(&plane_state->uapi.dst),
				drm_rect_height(&plane_state->uapi.dst),
				fb ? fb->format : NULL,
				fb ? fb->modifier : 0,
				need_scaler);

	if (ret || plane_state->scaler_id < 0)
		return ret;

	/* check colorkey */
	if (plane_state->ckey.flags) {
		drm_dbg_kms(&dev_priv->drm,
			    "[PLANE:%d:%s] scaling with color key not allowed",
			    intel_plane->base.base.id,
			    intel_plane->base.name);
		return -EINVAL;
	}

	/* Check src format */
	switch (fb->format->format) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_XYUV8888:
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
	case DRM_FORMAT_Y210:
	case DRM_FORMAT_Y212:
	case DRM_FORMAT_Y216:
	case DRM_FORMAT_XVYU2101010:
	case DRM_FORMAT_XVYU12_16161616:
	case DRM_FORMAT_XVYU16161616:
		break;
	case DRM_FORMAT_XBGR16161616F:
	case DRM_FORMAT_ABGR16161616F:
	case DRM_FORMAT_XRGB16161616F:
	case DRM_FORMAT_ARGB16161616F:
		if (DISPLAY_VER(dev_priv) >= 11)
			break;
		fallthrough;
	default:
		drm_dbg_kms(&dev_priv->drm,
			    "[PLANE:%d:%s] FB:%d unsupported scaling format 0x%x\n",
			    intel_plane->base.base.id, intel_plane->base.name,
			    fb->base.id, fb->format->format);
		return -EINVAL;
	}

	return 0;
}

static int cnl_coef_tap(int i)
{
	return i % 7;
}

static u16 cnl_nearest_filter_coef(int t)
{
	return t == 3 ? 0x0800 : 0x3000;
}

/*
 *  Theory behind setting nearest-neighbor integer scaling:
 *
 *  17 phase of 7 taps requires 119 coefficients in 60 dwords per set.
 *  The letter represents the filter tap (D is the center tap) and the number
 *  represents the coefficient set for a phase (0-16).
 *
 *         +------------+------------------------+------------------------+
 *         |Index value | Data value coeffient 1 | Data value coeffient 2 |
 *         +------------+------------------------+------------------------+
 *         |   00h      |          B0            |          A0            |
 *         +------------+------------------------+------------------------+
 *         |   01h      |          D0            |          C0            |
 *         +------------+------------------------+------------------------+
 *         |   02h      |          F0            |          E0            |
 *         +------------+------------------------+------------------------+
 *         |   03h      |          A1            |          G0            |
 *         +------------+------------------------+------------------------+
 *         |   04h      |          C1            |          B1            |
 *         +------------+------------------------+------------------------+
 *         |   ...      |          ...           |          ...           |
 *         +------------+------------------------+------------------------+
 *         |   38h      |          B16           |          A16           |
 *         +------------+------------------------+------------------------+
 *         |   39h      |          D16           |          C16           |
 *         +------------+------------------------+------------------------+
 *         |   3Ah      |          F16           |          C16           |
 *         +------------+------------------------+------------------------+
 *         |   3Bh      |        Reserved        |          G16           |
 *         +------------+------------------------+------------------------+
 *
 *  To enable nearest-neighbor scaling:  program scaler coefficents with
 *  the center tap (Dxx) values set to 1 and all other values set to 0 as per
 *  SCALER_COEFFICIENT_FORMAT
 *
 */

static void cnl_program_nearest_filter_coefs(struct drm_i915_private *dev_priv,
					     enum pipe pipe, int id, int set)
{
	int i;

	intel_de_write_fw(dev_priv, CNL_PS_COEF_INDEX_SET(pipe, id, set),
			  PS_COEE_INDEX_AUTO_INC);

	for (i = 0; i < 17 * 7; i += 2) {
		u32 tmp;
		int t;

		t = cnl_coef_tap(i);
		tmp = cnl_nearest_filter_coef(t);

		t = cnl_coef_tap(i + 1);
		tmp |= cnl_nearest_filter_coef(t) << 16;

		intel_de_write_fw(dev_priv, CNL_PS_COEF_DATA_SET(pipe, id, set),
				  tmp);
	}

	intel_de_write_fw(dev_priv, CNL_PS_COEF_INDEX_SET(pipe, id, set), 0);
}

static u32 skl_scaler_get_filter_select(enum drm_scaling_filter filter, int set)
{
	if (filter == DRM_SCALING_FILTER_NEAREST_NEIGHBOR) {
		return (PS_FILTER_PROGRAMMED |
			PS_Y_VERT_FILTER_SELECT(set) |
			PS_Y_HORZ_FILTER_SELECT(set) |
			PS_UV_VERT_FILTER_SELECT(set) |
			PS_UV_HORZ_FILTER_SELECT(set));
	}

	return PS_FILTER_MEDIUM;
}

static void skl_scaler_setup_filter(struct drm_i915_private *dev_priv, enum pipe pipe,
				    int id, int set, enum drm_scaling_filter filter)
{
	switch (filter) {
	case DRM_SCALING_FILTER_DEFAULT:
		break;
	case DRM_SCALING_FILTER_NEAREST_NEIGHBOR:
		cnl_program_nearest_filter_coefs(dev_priv, pipe, id, set);
		break;
	default:
		MISSING_CASE(filter);
	}
}

void skl_pfit_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct drm_rect src = {
		.x2 = crtc_state->pipe_src_w << 16,
		.y2 = crtc_state->pipe_src_h << 16,
	};
	const struct drm_rect *dst = &crtc_state->pch_pfit.dst;
	u16 uv_rgb_hphase, uv_rgb_vphase;
	enum pipe pipe = crtc->pipe;
	int width = drm_rect_width(dst);
	int height = drm_rect_height(dst);
	int x = dst->x1;
	int y = dst->y1;
	int hscale, vscale;
	unsigned long irqflags;
	int id;
	u32 ps_ctrl;

	if (!crtc_state->pch_pfit.enabled)
		return;

	if (drm_WARN_ON(&dev_priv->drm,
			crtc_state->scaler_state.scaler_id < 0))
		return;

	hscale = drm_rect_calc_hscale(&src, dst, 0, INT_MAX);
	vscale = drm_rect_calc_vscale(&src, dst, 0, INT_MAX);

	uv_rgb_hphase = skl_scaler_calc_phase(1, hscale, false);
	uv_rgb_vphase = skl_scaler_calc_phase(1, vscale, false);

	id = scaler_state->scaler_id;

	ps_ctrl = skl_scaler_get_filter_select(crtc_state->hw.scaling_filter, 0);
	ps_ctrl |=  PS_SCALER_EN | scaler_state->scalers[id].mode;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	skl_scaler_setup_filter(dev_priv, pipe, id, 0,
				crtc_state->hw.scaling_filter);

	intel_de_write_fw(dev_priv, SKL_PS_CTRL(pipe, id), ps_ctrl);

	intel_de_write_fw(dev_priv, SKL_PS_VPHASE(pipe, id),
			  PS_Y_PHASE(0) | PS_UV_RGB_PHASE(uv_rgb_vphase));
	intel_de_write_fw(dev_priv, SKL_PS_HPHASE(pipe, id),
			  PS_Y_PHASE(0) | PS_UV_RGB_PHASE(uv_rgb_hphase));
	intel_de_write_fw(dev_priv, SKL_PS_WIN_POS(pipe, id),
			  x << 16 | y);
	intel_de_write_fw(dev_priv, SKL_PS_WIN_SZ(pipe, id),
			  width << 16 | height);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

void
skl_program_plane_scaler(struct intel_plane *plane,
			 const struct intel_crtc_state *crtc_state,
			 const struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	enum pipe pipe = plane->pipe;
	int scaler_id = plane_state->scaler_id;
	const struct intel_scaler *scaler =
		&crtc_state->scaler_state.scalers[scaler_id];
	int crtc_x = plane_state->uapi.dst.x1;
	int crtc_y = plane_state->uapi.dst.y1;
	u32 crtc_w = drm_rect_width(&plane_state->uapi.dst);
	u32 crtc_h = drm_rect_height(&plane_state->uapi.dst);
	u16 y_hphase, uv_rgb_hphase;
	u16 y_vphase, uv_rgb_vphase;
	int hscale, vscale;
	u32 ps_ctrl;

	hscale = drm_rect_calc_hscale(&plane_state->uapi.src,
				      &plane_state->uapi.dst,
				      0, INT_MAX);
	vscale = drm_rect_calc_vscale(&plane_state->uapi.src,
				      &plane_state->uapi.dst,
				      0, INT_MAX);

	/* TODO: handle sub-pixel coordinates */
	if (intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier) &&
	    !icl_is_hdr_plane(dev_priv, plane->id)) {
		y_hphase = skl_scaler_calc_phase(1, hscale, false);
		y_vphase = skl_scaler_calc_phase(1, vscale, false);

		/* MPEG2 chroma siting convention */
		uv_rgb_hphase = skl_scaler_calc_phase(2, hscale, true);
		uv_rgb_vphase = skl_scaler_calc_phase(2, vscale, false);
	} else {
		/* not used */
		y_hphase = 0;
		y_vphase = 0;

		uv_rgb_hphase = skl_scaler_calc_phase(1, hscale, false);
		uv_rgb_vphase = skl_scaler_calc_phase(1, vscale, false);
	}

	ps_ctrl = skl_scaler_get_filter_select(plane_state->hw.scaling_filter, 0);
	ps_ctrl |= PS_SCALER_EN | PS_PLANE_SEL(plane->id) | scaler->mode;

	skl_scaler_setup_filter(dev_priv, pipe, scaler_id, 0,
				plane_state->hw.scaling_filter);

	intel_de_write_fw(dev_priv, SKL_PS_CTRL(pipe, scaler_id), ps_ctrl);
	intel_de_write_fw(dev_priv, SKL_PS_VPHASE(pipe, scaler_id),
			  PS_Y_PHASE(y_vphase) | PS_UV_RGB_PHASE(uv_rgb_vphase));
	intel_de_write_fw(dev_priv, SKL_PS_HPHASE(pipe, scaler_id),
			  PS_Y_PHASE(y_hphase) | PS_UV_RGB_PHASE(uv_rgb_hphase));
	intel_de_write_fw(dev_priv, SKL_PS_WIN_POS(pipe, scaler_id),
			  (crtc_x << 16) | crtc_y);
	intel_de_write_fw(dev_priv, SKL_PS_WIN_SZ(pipe, scaler_id),
			  (crtc_w << 16) | crtc_h);
}

static void skl_detach_scaler(struct intel_crtc *crtc, int id)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	intel_de_write_fw(dev_priv, SKL_PS_CTRL(crtc->pipe, id), 0);
	intel_de_write_fw(dev_priv, SKL_PS_WIN_POS(crtc->pipe, id), 0);
	intel_de_write_fw(dev_priv, SKL_PS_WIN_SZ(crtc->pipe, id), 0);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

/*
 * This function detaches (aka. unbinds) unused scalers in hardware
 */
void skl_detach_scalers(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	const struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	int i;

	/* loop through and disable scalers that aren't in use */
	for (i = 0; i < crtc->num_scalers; i++) {
		if (!scaler_state->scalers[i].in_use)
			skl_detach_scaler(crtc, i);
	}
}

void skl_scaler_disable(const struct intel_crtc_state *old_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(old_crtc_state->uapi.crtc);
	int i;

	for (i = 0; i < crtc->num_scalers; i++)
		skl_detach_scaler(crtc, i);
}
