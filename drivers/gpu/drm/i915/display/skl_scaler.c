// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_fb.h"
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
#define MTL_MAX_SRC_W 4096
#define MTL_MAX_SRC_H 8192
#define MTL_MAX_DST_W 8192
#define MTL_MAX_DST_H 8192
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
	int pipe_src_w = drm_rect_width(&crtc_state->pipe_src);
	int pipe_src_h = drm_rect_height(&crtc_state->pipe_src);
	int min_src_w, min_src_h, min_dst_w, min_dst_h;
	int max_src_w, max_src_h, max_dst_w, max_dst_h;

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

	min_src_w = SKL_MIN_SRC_W;
	min_src_h = SKL_MIN_SRC_H;
	min_dst_w = SKL_MIN_DST_W;
	min_dst_h = SKL_MIN_DST_H;

	if (DISPLAY_VER(dev_priv) < 11) {
		max_src_w = SKL_MAX_SRC_W;
		max_src_h = SKL_MAX_SRC_H;
		max_dst_w = SKL_MAX_DST_W;
		max_dst_h = SKL_MAX_DST_H;
	} else if (DISPLAY_VER(dev_priv) < 14) {
		max_src_w = ICL_MAX_SRC_W;
		max_src_h = ICL_MAX_SRC_H;
		max_dst_w = ICL_MAX_DST_W;
		max_dst_h = ICL_MAX_DST_H;
	} else {
		max_src_w = MTL_MAX_SRC_W;
		max_src_h = MTL_MAX_SRC_H;
		max_dst_w = MTL_MAX_DST_W;
		max_dst_h = MTL_MAX_DST_H;
	}

	/* range checks */
	if (src_w < min_src_w || src_h < min_src_h ||
	    dst_w < min_dst_w || dst_h < min_dst_h ||
	    src_w > max_src_w || src_h > max_src_h ||
	    dst_w > max_dst_w || dst_h > max_dst_h) {
		drm_dbg_kms(&dev_priv->drm,
			    "scaler_user index %u.%u: src %ux%u dst %ux%u "
			    "size is out of scaler range\n",
			    crtc->pipe, scaler_user, src_w, src_h,
			    dst_w, dst_h);
		return -EINVAL;
	}

	/*
	 * The pipe scaler does not use all the bits of PIPESRC, at least
	 * on the earlier platforms. So even when we're scaling a plane
	 * the *pipe* source size must not be too large. For simplicity
	 * we assume the limits match the scaler source size limits. Might
	 * not be 100% accurate on all platforms, but good enough for now.
	 */
	if (pipe_src_w > max_src_w || pipe_src_h > max_src_h) {
		drm_dbg_kms(&dev_priv->drm,
			    "scaler_user index %u.%u: pipe src size %ux%u "
			    "is out of scaler range\n",
			    crtc->pipe, scaler_user, pipe_src_w, pipe_src_h);
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
				 drm_rect_width(&crtc_state->pipe_src),
				 drm_rect_height(&crtc_state->pipe_src),
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

static int intel_atomic_setup_scaler(struct intel_crtc_scaler_state *scaler_state,
				     int num_scalers_need, struct intel_crtc *intel_crtc,
				     const char *name, int idx,
				     struct intel_plane_state *plane_state,
				     int *scaler_id)
{
	struct drm_i915_private *dev_priv = to_i915(intel_crtc->base.dev);
	int j;
	u32 mode;

	if (*scaler_id < 0) {
		/* find a free scaler */
		for (j = 0; j < intel_crtc->num_scalers; j++) {
			if (scaler_state->scalers[j].in_use)
				continue;

			*scaler_id = j;
			scaler_state->scalers[*scaler_id].in_use = 1;
			break;
		}
	}

	if (drm_WARN(&dev_priv->drm, *scaler_id < 0,
		     "Cannot find scaler for %s:%d\n", name, idx))
		return -EINVAL;

	/* set scaler mode */
	if (plane_state && plane_state->hw.fb &&
	    plane_state->hw.fb->format->is_yuv &&
	    plane_state->hw.fb->format->num_planes > 1) {
		struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);

		if (DISPLAY_VER(dev_priv) == 9) {
			mode = SKL_PS_SCALER_MODE_NV12;
		} else if (icl_is_hdr_plane(dev_priv, plane->id)) {
			/*
			 * On gen11+'s HDR planes we only use the scaler for
			 * scaling. They have a dedicated chroma upsampler, so
			 * we don't need the scaler to upsample the UV plane.
			 */
			mode = PS_SCALER_MODE_NORMAL;
		} else {
			struct intel_plane *linked =
				plane_state->planar_linked_plane;

			mode = PS_SCALER_MODE_PLANAR;

			if (linked)
				mode |= PS_PLANE_Y_SEL(linked->id);
		}
	} else if (DISPLAY_VER(dev_priv) >= 10) {
		mode = PS_SCALER_MODE_NORMAL;
	} else if (num_scalers_need == 1 && intel_crtc->num_scalers > 1) {
		/*
		 * when only 1 scaler is in use on a pipe with 2 scalers
		 * scaler 0 operates in high quality (HQ) mode.
		 * In this case use scaler 0 to take advantage of HQ mode
		 */
		scaler_state->scalers[*scaler_id].in_use = 0;
		*scaler_id = 0;
		scaler_state->scalers[0].in_use = 1;
		mode = SKL_PS_SCALER_MODE_HQ;
	} else {
		mode = SKL_PS_SCALER_MODE_DYN;
	}

	/*
	 * FIXME: we should also check the scaler factors for pfit, so
	 * this shouldn't be tied directly to planes.
	 */
	if (plane_state && plane_state->hw.fb) {
		const struct drm_framebuffer *fb = plane_state->hw.fb;
		const struct drm_rect *src = &plane_state->uapi.src;
		const struct drm_rect *dst = &plane_state->uapi.dst;
		int hscale, vscale, max_vscale, max_hscale;

		/*
		 * FIXME: When two scalers are needed, but only one of
		 * them needs to downscale, we should make sure that
		 * the one that needs downscaling support is assigned
		 * as the first scaler, so we don't reject downscaling
		 * unnecessarily.
		 */

		if (DISPLAY_VER(dev_priv) >= 14) {
			/*
			 * On versions 14 and up, only the first
			 * scaler supports a vertical scaling factor
			 * of more than 1.0, while a horizontal
			 * scaling factor of 3.0 is supported.
			 */
			max_hscale = 0x30000 - 1;
			if (*scaler_id == 0)
				max_vscale = 0x30000 - 1;
			else
				max_vscale = 0x10000;

		} else if (DISPLAY_VER(dev_priv) >= 10 ||
			   !intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier)) {
			max_hscale = 0x30000 - 1;
			max_vscale = 0x30000 - 1;
		} else {
			max_hscale = 0x20000 - 1;
			max_vscale = 0x20000 - 1;
		}

		/*
		 * FIXME: We should change the if-else block above to
		 * support HQ vs dynamic scaler properly.
		 */

		/* Check if required scaling is within limits */
		hscale = drm_rect_calc_hscale(src, dst, 1, max_hscale);
		vscale = drm_rect_calc_vscale(src, dst, 1, max_vscale);

		if (hscale < 0 || vscale < 0) {
			drm_dbg_kms(&dev_priv->drm,
				    "Scaler %d doesn't support required plane scaling\n",
				    *scaler_id);
			drm_rect_debug_print("src: ", src, true);
			drm_rect_debug_print("dst: ", dst, false);

			return -EINVAL;
		}
	}

	drm_dbg_kms(&dev_priv->drm, "Attached scaler id %u.%u to %s:%d\n",
		    intel_crtc->pipe, *scaler_id, name, idx);
	scaler_state->scalers[*scaler_id].mode = mode;

	return 0;
}

/**
 * intel_atomic_setup_scalers() - setup scalers for crtc per staged requests
 * @dev_priv: i915 device
 * @intel_crtc: intel crtc
 * @crtc_state: incoming crtc_state to validate and setup scalers
 *
 * This function sets up scalers based on staged scaling requests for
 * a @crtc and its planes. It is called from crtc level check path. If request
 * is a supportable request, it attaches scalers to requested planes and crtc.
 *
 * This function takes into account the current scaler(s) in use by any planes
 * not being part of this atomic state
 *
 *  Returns:
 *         0 - scalers were setup successfully
 *         error code - otherwise
 */
int intel_atomic_setup_scalers(struct drm_i915_private *dev_priv,
			       struct intel_crtc *intel_crtc,
			       struct intel_crtc_state *crtc_state)
{
	struct drm_plane *plane = NULL;
	struct intel_plane *intel_plane;
	struct intel_crtc_scaler_state *scaler_state =
		&crtc_state->scaler_state;
	struct drm_atomic_state *drm_state = crtc_state->uapi.state;
	struct intel_atomic_state *intel_state = to_intel_atomic_state(drm_state);
	int num_scalers_need;
	int i;

	num_scalers_need = hweight32(scaler_state->scaler_users);

	/*
	 * High level flow:
	 * - staged scaler requests are already in scaler_state->scaler_users
	 * - check whether staged scaling requests can be supported
	 * - add planes using scalers that aren't in current transaction
	 * - assign scalers to requested users
	 * - as part of plane commit, scalers will be committed
	 *   (i.e., either attached or detached) to respective planes in hw
	 * - as part of crtc_commit, scaler will be either attached or detached
	 *   to crtc in hw
	 */

	/* fail if required scalers > available scalers */
	if (num_scalers_need > intel_crtc->num_scalers) {
		drm_dbg_kms(&dev_priv->drm,
			    "Too many scaling requests %d > %d\n",
			    num_scalers_need, intel_crtc->num_scalers);
		return -EINVAL;
	}

	/* walkthrough scaler_users bits and start assigning scalers */
	for (i = 0; i < sizeof(scaler_state->scaler_users) * 8; i++) {
		struct intel_plane_state *plane_state = NULL;
		int *scaler_id;
		const char *name;
		int idx, ret;

		/* skip if scaler not required */
		if (!(scaler_state->scaler_users & (1 << i)))
			continue;

		if (i == SKL_CRTC_INDEX) {
			name = "CRTC";
			idx = intel_crtc->base.base.id;

			/* panel fitter case: assign as a crtc scaler */
			scaler_id = &scaler_state->scaler_id;
		} else {
			name = "PLANE";

			/* plane scaler case: assign as a plane scaler */
			/* find the plane that set the bit as scaler_user */
			plane = drm_state->planes[i].ptr;

			/*
			 * to enable/disable hq mode, add planes that are using scaler
			 * into this transaction
			 */
			if (!plane) {
				struct drm_plane_state *state;

				/*
				 * GLK+ scalers don't have a HQ mode so it
				 * isn't necessary to change between HQ and dyn mode
				 * on those platforms.
				 */
				if (DISPLAY_VER(dev_priv) >= 10)
					continue;

				plane = drm_plane_from_index(&dev_priv->drm, i);
				state = drm_atomic_get_plane_state(drm_state, plane);
				if (IS_ERR(state)) {
					drm_dbg_kms(&dev_priv->drm,
						    "Failed to add [PLANE:%d] to drm_state\n",
						    plane->base.id);
					return PTR_ERR(state);
				}
			}

			intel_plane = to_intel_plane(plane);
			idx = plane->base.id;

			/* plane on different crtc cannot be a scaler user of this crtc */
			if (drm_WARN_ON(&dev_priv->drm,
					intel_plane->pipe != intel_crtc->pipe))
				continue;

			plane_state = intel_atomic_get_new_plane_state(intel_state,
								       intel_plane);
			scaler_id = &plane_state->scaler_id;
		}

		ret = intel_atomic_setup_scaler(scaler_state, num_scalers_need,
						intel_crtc, name, idx,
						plane_state, scaler_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int glk_coef_tap(int i)
{
	return i % 7;
}

static u16 glk_nearest_filter_coef(int t)
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

static void glk_program_nearest_filter_coefs(struct drm_i915_private *dev_priv,
					     enum pipe pipe, int id, int set)
{
	int i;

	intel_de_write_fw(dev_priv, GLK_PS_COEF_INDEX_SET(pipe, id, set),
			  PS_COEE_INDEX_AUTO_INC);

	for (i = 0; i < 17 * 7; i += 2) {
		u32 tmp;
		int t;

		t = glk_coef_tap(i);
		tmp = glk_nearest_filter_coef(t);

		t = glk_coef_tap(i + 1);
		tmp |= glk_nearest_filter_coef(t) << 16;

		intel_de_write_fw(dev_priv, GLK_PS_COEF_DATA_SET(pipe, id, set),
				  tmp);
	}

	intel_de_write_fw(dev_priv, GLK_PS_COEF_INDEX_SET(pipe, id, set), 0);
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
		glk_program_nearest_filter_coefs(dev_priv, pipe, id, set);
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
	const struct drm_rect *dst = &crtc_state->pch_pfit.dst;
	u16 uv_rgb_hphase, uv_rgb_vphase;
	enum pipe pipe = crtc->pipe;
	int width = drm_rect_width(dst);
	int height = drm_rect_height(dst);
	int x = dst->x1;
	int y = dst->y1;
	int hscale, vscale;
	struct drm_rect src;
	int id;
	u32 ps_ctrl;

	if (!crtc_state->pch_pfit.enabled)
		return;

	if (drm_WARN_ON(&dev_priv->drm,
			crtc_state->scaler_state.scaler_id < 0))
		return;

	drm_rect_init(&src, 0, 0,
		      drm_rect_width(&crtc_state->pipe_src) << 16,
		      drm_rect_height(&crtc_state->pipe_src) << 16);

	hscale = drm_rect_calc_hscale(&src, dst, 0, INT_MAX);
	vscale = drm_rect_calc_vscale(&src, dst, 0, INT_MAX);

	uv_rgb_hphase = skl_scaler_calc_phase(1, hscale, false);
	uv_rgb_vphase = skl_scaler_calc_phase(1, vscale, false);

	id = scaler_state->scaler_id;

	ps_ctrl = skl_scaler_get_filter_select(crtc_state->hw.scaling_filter, 0);
	ps_ctrl |=  PS_SCALER_EN | scaler_state->scalers[id].mode;

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

	intel_de_write_fw(dev_priv, SKL_PS_CTRL(crtc->pipe, id), 0);
	intel_de_write_fw(dev_priv, SKL_PS_WIN_POS(crtc->pipe, id), 0);
	intel_de_write_fw(dev_priv, SKL_PS_WIN_SZ(crtc->pipe, id), 0);
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
