/*
 * Copyright 2012-13 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/types.h>
#include <linux/version.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_edid.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "dm_services_types.h"

// We need to #undef FRAME_SIZE and DEPRECATED because they conflict
// with ptrace-abi.h's #define's of them.
#undef FRAME_SIZE
#undef DEPRECATED

#include "dc.h"

#include "amdgpu_dm_types.h"
#include "amdgpu_dm_mst_types.h"

#include "modules/inc/mod_freesync.h"

struct dm_connector_state {
	struct drm_connector_state base;

	enum amdgpu_rmx_type scaling;
	uint8_t underscan_vborder;
	uint8_t underscan_hborder;
	bool underscan_enable;
};

#define to_dm_connector_state(x)\
	container_of((x), struct dm_connector_state, base)


void amdgpu_dm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs amdgpu_dm_encoder_funcs = {
	.destroy = amdgpu_dm_encoder_destroy,
};

static void dm_set_cursor(
	struct amdgpu_crtc *amdgpu_crtc,
	uint64_t gpu_addr,
	uint32_t width,
	uint32_t height)
{
	struct dc_cursor_attributes attributes;
	amdgpu_crtc->cursor_width = width;
	amdgpu_crtc->cursor_height = height;

	attributes.address.high_part = upper_32_bits(gpu_addr);
	attributes.address.low_part  = lower_32_bits(gpu_addr);
	attributes.width             = width;
	attributes.height            = height;
	attributes.x_hot             = 0;
	attributes.y_hot             = 0;
	attributes.color_format      = CURSOR_MODE_COLOR_PRE_MULTIPLIED_ALPHA;
	attributes.rotation_angle    = 0;
	attributes.attribute_flags.value = 0;

	if (!dc_target_set_cursor_attributes(
				amdgpu_crtc->target,
				&attributes)) {
		DRM_ERROR("DC failed to set cursor attributes\n");
	}
}

static int dm_crtc_unpin_cursor_bo_old(
	struct amdgpu_crtc *amdgpu_crtc)
{
	struct amdgpu_bo *robj;
	int ret = 0;

	if (NULL != amdgpu_crtc && NULL != amdgpu_crtc->cursor_bo) {
		robj = gem_to_amdgpu_bo(amdgpu_crtc->cursor_bo);

		ret = amdgpu_bo_reserve(robj, false);

		if (likely(ret == 0)) {
			ret = amdgpu_bo_unpin(robj);

			if (unlikely(ret != 0)) {
				DRM_ERROR(
					"%s: unpin failed (ret=%d), bo %p\n",
					__func__,
					ret,
					amdgpu_crtc->cursor_bo);
			}

			amdgpu_bo_unreserve(robj);
		} else {
			DRM_ERROR(
				"%s: reserve failed (ret=%d), bo %p\n",
				__func__,
				ret,
				amdgpu_crtc->cursor_bo);
		}

		drm_gem_object_unreference_unlocked(amdgpu_crtc->cursor_bo);
		amdgpu_crtc->cursor_bo = NULL;
	}

	return ret;
}

static int dm_crtc_pin_cursor_bo_new(
	struct drm_crtc *crtc,
	struct drm_file *file_priv,
	uint32_t handle,
	struct amdgpu_bo **ret_obj)
{
	struct amdgpu_crtc *amdgpu_crtc;
	struct amdgpu_bo *robj;
	struct drm_gem_object *obj;
	int ret = -EINVAL;

	if (NULL != crtc) {
		struct drm_device *dev = crtc->dev;
		struct amdgpu_device *adev = dev->dev_private;
		uint64_t gpu_addr;

		amdgpu_crtc = to_amdgpu_crtc(crtc);

		obj = drm_gem_object_lookup(file_priv, handle);

		if (!obj) {
			DRM_ERROR(
				"Cannot find cursor object %x for crtc %d\n",
				handle,
				amdgpu_crtc->crtc_id);
			goto release;
		}
		robj = gem_to_amdgpu_bo(obj);

		ret  = amdgpu_bo_reserve(robj, false);

		if (unlikely(ret != 0)) {
			drm_gem_object_unreference_unlocked(obj);
		DRM_ERROR("dm_crtc_pin_cursor_bo_new ret %x, handle %x\n",
				 ret, handle);
			goto release;
		}

		ret = amdgpu_bo_pin_restricted(robj, AMDGPU_GEM_DOMAIN_VRAM, 0,
						adev->mc.visible_vram_size,
						&gpu_addr);

		if (ret == 0) {
			amdgpu_crtc->cursor_addr = gpu_addr;
			*ret_obj  = robj;
		}
		amdgpu_bo_unreserve(robj);
		if (ret)
			drm_gem_object_unreference_unlocked(obj);

	}
release:

	return ret;
}

static int dm_crtc_cursor_set(
	struct drm_crtc *crtc,
	struct drm_file *file_priv,
	uint32_t handle,
	uint32_t width,
	uint32_t height)
{
	struct amdgpu_bo *new_cursor_bo;
	struct dc_cursor_position position;

	int ret;

	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	ret		= EINVAL;
	new_cursor_bo	= NULL;

	DRM_DEBUG_KMS(
	"%s: crtc_id=%d with handle %d and size %d to %d, bo_object %p\n",
		__func__,
		amdgpu_crtc->crtc_id,
		handle,
		width,
		height,
		amdgpu_crtc->cursor_bo);

	if (!handle) {
		/* turn off cursor */
		position.enable = false;
		position.x = 0;
		position.y = 0;
		position.hot_spot_enable = false;

		if (amdgpu_crtc->target) {
			/*set cursor visible false*/
			dc_target_set_cursor_position(
				amdgpu_crtc->target,
				&position);
		}
		/*unpin old cursor buffer and update cache*/
		ret = dm_crtc_unpin_cursor_bo_old(amdgpu_crtc);
		goto release;

	}

	if ((width > amdgpu_crtc->max_cursor_width) ||
		(height > amdgpu_crtc->max_cursor_height)) {
		DRM_ERROR(
			"%s: bad cursor width or height %d x %d\n",
			__func__,
			width,
			height);
		goto release;
	}
	/*try to pin new cursor bo*/
	ret = dm_crtc_pin_cursor_bo_new(crtc, file_priv, handle, &new_cursor_bo);
	/*if map not successful then return an error*/
	if (ret)
		goto release;

	/*program new cursor bo to hardware*/
	dm_set_cursor(amdgpu_crtc, amdgpu_crtc->cursor_addr, width, height);

	/*un map old, not used anymore cursor bo ,
	 * return memory and mapping back */
	dm_crtc_unpin_cursor_bo_old(amdgpu_crtc);

	/*assign new cursor bo to our internal cache*/
	amdgpu_crtc->cursor_bo = &new_cursor_bo->gem_base;

release:
	return ret;

}

static int dm_crtc_cursor_move(struct drm_crtc *crtc,
				     int x, int y)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int xorigin = 0, yorigin = 0;
	struct dc_cursor_position position;

	/* avivo cursor are offset into the total surface */
	x += crtc->primary->state->src_x >> 16;
	y += crtc->primary->state->src_y >> 16;

	/*
	 * TODO: for cursor debugging unguard the following
	 */
#if 0
	DRM_DEBUG_KMS(
		"%s: x %d y %d c->x %d c->y %d\n",
		__func__,
		x,
		y,
		crtc->x,
		crtc->y);
#endif

	if (x < 0) {
		xorigin = min(-x, amdgpu_crtc->max_cursor_width - 1);
		x = 0;
	}
	if (y < 0) {
		yorigin = min(-y, amdgpu_crtc->max_cursor_height - 1);
		y = 0;
	}

	position.enable = true;
	position.x = x;
	position.y = y;

	position.hot_spot_enable = true;
	position.x_hotspot = xorigin;
	position.y_hotspot = yorigin;

	if (amdgpu_crtc->target) {
		if (!dc_target_set_cursor_position(
					amdgpu_crtc->target,
					&position)) {
			DRM_ERROR("DC failed to set cursor position\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void dm_crtc_cursor_reset(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	DRM_DEBUG_KMS(
		"%s: with cursor_bo %p\n",
		__func__,
		amdgpu_crtc->cursor_bo);

	if (amdgpu_crtc->cursor_bo && amdgpu_crtc->target) {
		dm_set_cursor(
		amdgpu_crtc,
		amdgpu_crtc->cursor_addr,
		amdgpu_crtc->cursor_width,
		amdgpu_crtc->cursor_height);
	}
}
static bool fill_rects_from_plane_state(
	const struct drm_plane_state *state,
	struct dc_surface *surface)
{
	surface->src_rect.x = state->src_x >> 16;
	surface->src_rect.y = state->src_y >> 16;
	/*we ignore for now mantissa and do not to deal with floating pixels :(*/
	surface->src_rect.width = state->src_w >> 16;

	if (surface->src_rect.width == 0)
		return false;

	surface->src_rect.height = state->src_h >> 16;
	if (surface->src_rect.height == 0)
		return false;

	surface->dst_rect.x = state->crtc_x;
	surface->dst_rect.y = state->crtc_y;

	if (state->crtc_w == 0)
		return false;

	surface->dst_rect.width = state->crtc_w;

	if (state->crtc_h == 0)
		return false;

	surface->dst_rect.height = state->crtc_h;

	surface->clip_rect = surface->dst_rect;

	switch (state->rotation & DRM_MODE_ROTATE_MASK) {
	case DRM_MODE_ROTATE_0:
		surface->rotation = ROTATION_ANGLE_0;
		break;
	case DRM_MODE_ROTATE_90:
		surface->rotation = ROTATION_ANGLE_90;
		break;
	case DRM_MODE_ROTATE_180:
		surface->rotation = ROTATION_ANGLE_180;
		break;
	case DRM_MODE_ROTATE_270:
		surface->rotation = ROTATION_ANGLE_270;
		break;
	default:
		surface->rotation = ROTATION_ANGLE_0;
		break;
	}

	return true;
}
static bool get_fb_info(
	const struct amdgpu_framebuffer *amdgpu_fb,
	uint64_t *tiling_flags,
	uint64_t *fb_location)
{
	struct amdgpu_bo *rbo = gem_to_amdgpu_bo(amdgpu_fb->obj);
	int r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0)){
		DRM_ERROR("Unable to reserve buffer\n");
		return false;
	}

	if (fb_location)
		*fb_location = amdgpu_bo_gpu_offset(rbo);

	if (tiling_flags)
		amdgpu_bo_get_tiling_flags(rbo, tiling_flags);

	amdgpu_bo_unreserve(rbo);

	return true;
}
static void fill_plane_attributes_from_fb(
	struct dc_surface *surface,
	const struct amdgpu_framebuffer *amdgpu_fb, bool addReq)
{
	uint64_t tiling_flags;
	uint64_t fb_location = 0;
	const struct drm_framebuffer *fb = &amdgpu_fb->base;
	struct drm_format_name_buf format_name;

	get_fb_info(
		amdgpu_fb,
		&tiling_flags,
		addReq == true ? &fb_location:NULL);

	surface->address.type                = PLN_ADDR_TYPE_GRAPHICS;
	surface->address.grph.addr.low_part  = lower_32_bits(fb_location);
	surface->address.grph.addr.high_part = upper_32_bits(fb_location);

	switch (fb->format->format) {
	case DRM_FORMAT_C8:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS;
		break;
	case DRM_FORMAT_RGB565:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_RGB565;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB8888;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_ARGB2101010:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010;
		break;
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_ABGR2101010:
		surface->format = SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010;
		break;
	default:
		DRM_ERROR("Unsupported screen format %s\n",
		          drm_get_format_name(fb->format->format, &format_name));
		return;
	}

	memset(&surface->tiling_info, 0, sizeof(surface->tiling_info));

	if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == DC_ARRAY_2D_TILED_THIN1)
	{
		unsigned bankw, bankh, mtaspect, tile_split, num_banks;

		bankw = AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
		bankh = AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
		mtaspect = AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
		tile_split = AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT);
		num_banks = AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);

		/* XXX fix me for VI */
		surface->tiling_info.gfx8.num_banks = num_banks;
		surface->tiling_info.gfx8.array_mode =
				DC_ARRAY_2D_TILED_THIN1;
		surface->tiling_info.gfx8.tile_split = tile_split;
		surface->tiling_info.gfx8.bank_width = bankw;
		surface->tiling_info.gfx8.bank_height = bankh;
		surface->tiling_info.gfx8.tile_aspect = mtaspect;
		surface->tiling_info.gfx8.tile_mode =
				DC_ADDR_SURF_MICRO_TILING_DISPLAY;
	} else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE)
			== DC_ARRAY_1D_TILED_THIN1) {
		surface->tiling_info.gfx8.array_mode = DC_ARRAY_1D_TILED_THIN1;
	}

	surface->tiling_info.gfx8.pipe_config =
			AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);

	surface->plane_size.grph.surface_size.x = 0;
	surface->plane_size.grph.surface_size.y = 0;
	surface->plane_size.grph.surface_size.width = fb->width;
	surface->plane_size.grph.surface_size.height = fb->height;
	surface->plane_size.grph.surface_pitch =
		fb->pitches[0] / fb->format->cpp[0];

	surface->visible = true;
	surface->scaling_quality.h_taps_c = 0;
	surface->scaling_quality.v_taps_c = 0;

	/* TODO: unhardcode */
	surface->color_space = COLOR_SPACE_SRGB;
	/* is this needed? is surface zeroed at allocation? */
	surface->scaling_quality.h_taps = 0;
	surface->scaling_quality.v_taps = 0;
	surface->stereo_format = PLANE_STEREO_FORMAT_NONE;

}

#define NUM_OF_RAW_GAMMA_RAMP_RGB_256 256

static void fill_gamma_from_crtc(
	const struct drm_crtc *crtc,
	struct dc_surface *dc_surface)
{
	int i;
	struct dc_gamma *gamma;
	struct drm_crtc_state *state = crtc->state;
	struct drm_color_lut *lut = (struct drm_color_lut *) state->gamma_lut->data;
	struct dc_transfer_func *input_tf;

	gamma = dc_create_gamma();

	if (gamma == NULL)
		return;

	for (i = 0; i < NUM_OF_RAW_GAMMA_RAMP_RGB_256; i++) {
		gamma->gamma_ramp_rgb256x3x16.red[i] = lut[i].red;
		gamma->gamma_ramp_rgb256x3x16.green[i] = lut[i].green;
		gamma->gamma_ramp_rgb256x3x16.blue[i] = lut[i].blue;
	}

	gamma->type = GAMMA_RAMP_RBG256X3X16;
	gamma->size = sizeof(gamma->gamma_ramp_rgb256x3x16);

	dc_surface->gamma_correction = gamma;

	input_tf = dc_create_transfer_func();

	if (input_tf == NULL)
		return;

	input_tf->type = TF_TYPE_PREDEFINED;
	input_tf->tf = TRANSFER_FUNCTION_SRGB;

	dc_surface->in_transfer_func = input_tf;
}

static void fill_plane_attributes(
			struct dc_surface *surface,
			struct drm_plane_state *state, bool addrReq)
{
	const struct amdgpu_framebuffer *amdgpu_fb =
		to_amdgpu_framebuffer(state->fb);
	const struct drm_crtc *crtc = state->crtc;

	fill_rects_from_plane_state(state, surface);
	fill_plane_attributes_from_fb(
		surface,
		amdgpu_fb,
		addrReq);

	/* In case of gamma set, update gamma value */
	if (state->crtc->state->gamma_lut) {
		fill_gamma_from_crtc(crtc, surface);
	}
}

/*****************************************************************************/

struct amdgpu_connector *aconnector_from_drm_crtc_id(
		const struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_connector *connector;
	struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct amdgpu_connector *aconnector;

	list_for_each_entry(connector,
			&dev->mode_config.connector_list, head)	{

		aconnector = to_amdgpu_connector(connector);

		if (aconnector->base.state->crtc != &acrtc->base)
			continue;

		/* Found the connector */
		return aconnector;
	}

	/* If we get here, not found. */
	return NULL;
}

static void update_stream_scaling_settings(
		const struct drm_display_mode *mode,
		const struct dm_connector_state *dm_state,
		const struct dc_stream *stream)
{
	struct amdgpu_device *adev = dm_state->base.crtc->dev->dev_private;
	enum amdgpu_rmx_type rmx_type;

	struct rect src = { 0 }; /* viewport in target space*/
	struct rect dst = { 0 }; /* stream addressable area */

	/* Full screen scaling by default */
	src.width = mode->hdisplay;
	src.height = mode->vdisplay;
	dst.width = stream->timing.h_addressable;
	dst.height = stream->timing.v_addressable;

	rmx_type = dm_state->scaling;
	if (rmx_type == RMX_ASPECT || rmx_type == RMX_OFF) {
		if (src.width * dst.height <
				src.height * dst.width) {
			/* height needs less upscaling/more downscaling */
			dst.width = src.width *
					dst.height / src.height;
		} else {
			/* width needs less upscaling/more downscaling */
			dst.height = src.height *
					dst.width / src.width;
		}
	} else if (rmx_type == RMX_CENTER) {
		dst = src;
	}

	dst.x = (stream->timing.h_addressable - dst.width) / 2;
	dst.y = (stream->timing.v_addressable - dst.height) / 2;

	if (dm_state->underscan_enable) {
		dst.x += dm_state->underscan_hborder / 2;
		dst.y += dm_state->underscan_vborder / 2;
		dst.width -= dm_state->underscan_hborder;
		dst.height -= dm_state->underscan_vborder;
	}

	adev->dm.dc->stream_funcs.stream_update_scaling(adev->dm.dc, stream, &src, &dst);

	DRM_DEBUG_KMS("Destination Rectangle x:%d  y:%d  width:%d  height:%d\n",
			dst.x, dst.y, dst.width, dst.height);

}

static void dm_dc_surface_commit(
		struct dc *dc,
		struct drm_crtc *crtc)
{
	struct dc_surface *dc_surface;
	const struct dc_surface *dc_surfaces[1];
	const struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
	struct dc_target *dc_target = acrtc->target;

	if (!dc_target) {
		dm_error(
			"%s: Failed to obtain target on crtc (%d)!\n",
			__func__,
			acrtc->crtc_id);
		goto fail;
	}

	dc_surface = dc_create_surface(dc);

	if (!dc_surface) {
		dm_error(
			"%s: Failed to create a surface!\n",
			__func__);
		goto fail;
	}

	/* Surface programming */
	fill_plane_attributes(dc_surface, crtc->primary->state, true);

	dc_surfaces[0] = dc_surface;

	if (false == dc_commit_surfaces_to_target(
			dc,
			dc_surfaces,
			1,
			dc_target)) {
		dm_error(
			"%s: Failed to attach surface!\n",
			__func__);
	}

	dc_surface_release(dc_surface);
fail:
	return;
}

static enum dc_color_depth convert_color_depth_from_display_info(
		const struct drm_connector *connector)
{
	uint32_t bpc = connector->display_info.bpc;

	/* Limited color depth to 8bit
	 * TODO: Still need to handle deep color*/
	if (bpc > 8)
		bpc = 8;

	switch (bpc) {
	case 0:
		/* Temporary Work around, DRM don't parse color depth for
		 * EDID revision before 1.4
		 * TODO: Fix edid parsing
		 */
		return COLOR_DEPTH_888;
	case 6:
		return COLOR_DEPTH_666;
	case 8:
		return COLOR_DEPTH_888;
	case 10:
		return COLOR_DEPTH_101010;
	case 12:
		return COLOR_DEPTH_121212;
	case 14:
		return COLOR_DEPTH_141414;
	case 16:
		return COLOR_DEPTH_161616;
	default:
		return COLOR_DEPTH_UNDEFINED;
	}
}

static enum dc_aspect_ratio get_aspect_ratio(
		const struct drm_display_mode *mode_in)
{
	int32_t width = mode_in->crtc_hdisplay * 9;
	int32_t height = mode_in->crtc_vdisplay * 16;
	if ((width - height) < 10 && (width - height) > -10)
		return ASPECT_RATIO_16_9;
	else
		return ASPECT_RATIO_4_3;
}

static enum dc_color_space get_output_color_space(
				const struct dc_crtc_timing *dc_crtc_timing)
{
	enum dc_color_space color_space = COLOR_SPACE_SRGB;

	switch (dc_crtc_timing->pixel_encoding)	{
	case PIXEL_ENCODING_YCBCR422:
	case PIXEL_ENCODING_YCBCR444:
	case PIXEL_ENCODING_YCBCR420:
	{
		/*
		 * 27030khz is the separation point between HDTV and SDTV
		 * according to HDMI spec, we use YCbCr709 and YCbCr601
		 * respectively
		 */
		if (dc_crtc_timing->pix_clk_khz > 27030) {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR709_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR709;
		} else {
			if (dc_crtc_timing->flags.Y_ONLY)
				color_space =
					COLOR_SPACE_YCBCR601_LIMITED;
			else
				color_space = COLOR_SPACE_YCBCR601;
		}

	}
	break;
	case PIXEL_ENCODING_RGB:
		color_space = COLOR_SPACE_SRGB;
		break;

	default:
		WARN_ON(1);
		break;
	}

	return color_space;
}

/*****************************************************************************/

static void fill_stream_properties_from_drm_display_mode(
	struct dc_stream *stream,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector)
{
	struct dc_crtc_timing *timing_out = &stream->timing;
	memset(timing_out, 0, sizeof(struct dc_crtc_timing));

	timing_out->h_border_left = 0;
	timing_out->h_border_right = 0;
	timing_out->v_border_top = 0;
	timing_out->v_border_bottom = 0;
	/* TODO: un-hardcode */

	if ((connector->display_info.color_formats & DRM_COLOR_FORMAT_YCRCB444)
			&& stream->sink->sink_signal == SIGNAL_TYPE_HDMI_TYPE_A)
		timing_out->pixel_encoding = PIXEL_ENCODING_YCBCR444;
	else
		timing_out->pixel_encoding = PIXEL_ENCODING_RGB;

	timing_out->timing_3d_format = TIMING_3D_FORMAT_NONE;
	timing_out->display_color_depth = convert_color_depth_from_display_info(
			connector);
	timing_out->scan_type = SCANNING_TYPE_NODATA;
	timing_out->hdmi_vic = 0;
	timing_out->vic = drm_match_cea_mode(mode_in);

	timing_out->h_addressable = mode_in->crtc_hdisplay;
	timing_out->h_total = mode_in->crtc_htotal;
	timing_out->h_sync_width =
		mode_in->crtc_hsync_end - mode_in->crtc_hsync_start;
	timing_out->h_front_porch =
		mode_in->crtc_hsync_start - mode_in->crtc_hdisplay;
	timing_out->v_total = mode_in->crtc_vtotal;
	timing_out->v_addressable = mode_in->crtc_vdisplay;
	timing_out->v_front_porch =
		mode_in->crtc_vsync_start - mode_in->crtc_vdisplay;
	timing_out->v_sync_width =
		mode_in->crtc_vsync_end - mode_in->crtc_vsync_start;
	timing_out->pix_clk_khz = mode_in->crtc_clock;
	timing_out->aspect_ratio = get_aspect_ratio(mode_in);
	if (mode_in->flags & DRM_MODE_FLAG_PHSYNC)
		timing_out->flags.HSYNC_POSITIVE_POLARITY = 1;
	if (mode_in->flags & DRM_MODE_FLAG_PVSYNC)
		timing_out->flags.VSYNC_POSITIVE_POLARITY = 1;

	stream->output_color_space = get_output_color_space(timing_out);

}

static void fill_audio_info(
	struct audio_info *audio_info,
	const struct drm_connector *drm_connector,
	const struct dc_sink *dc_sink)
{
	int i = 0;
	int cea_revision = 0;
	const struct dc_edid_caps *edid_caps = &dc_sink->edid_caps;

	audio_info->manufacture_id = edid_caps->manufacturer_id;
	audio_info->product_id = edid_caps->product_id;

	cea_revision = drm_connector->display_info.cea_rev;

	while (i < AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS &&
		edid_caps->display_name[i]) {
		audio_info->display_name[i] = edid_caps->display_name[i];
		i++;
	}

	if(cea_revision >= 3) {
		audio_info->mode_count = edid_caps->audio_mode_count;

		for (i = 0; i < audio_info->mode_count; ++i) {
			audio_info->modes[i].format_code =
					(enum audio_format_code)
					(edid_caps->audio_modes[i].format_code);
			audio_info->modes[i].channel_count =
					edid_caps->audio_modes[i].channel_count;
			audio_info->modes[i].sample_rates.all =
					edid_caps->audio_modes[i].sample_rate;
			audio_info->modes[i].sample_size =
					edid_caps->audio_modes[i].sample_size;
		}
	}

	audio_info->flags.all = edid_caps->speaker_flags;

	/* TODO: We only check for the progressive mode, check for interlace mode too */
	if(drm_connector->latency_present[0]) {
		audio_info->video_latency = drm_connector->video_latency[0];
		audio_info->audio_latency = drm_connector->audio_latency[0];
	}

	/* TODO: For DP, video and audio latency should be calculated from DPCD caps */

}

static void copy_crtc_timing_for_drm_display_mode(
		const struct drm_display_mode *src_mode,
		struct drm_display_mode *dst_mode)
{
	dst_mode->crtc_hdisplay = src_mode->crtc_hdisplay;
	dst_mode->crtc_vdisplay = src_mode->crtc_vdisplay;
	dst_mode->crtc_clock = src_mode->crtc_clock;
	dst_mode->crtc_hblank_start = src_mode->crtc_hblank_start;
	dst_mode->crtc_hblank_end = src_mode->crtc_hblank_end;
	dst_mode->crtc_hsync_start=  src_mode->crtc_hsync_start;
	dst_mode->crtc_hsync_end = src_mode->crtc_hsync_end;
	dst_mode->crtc_htotal = src_mode->crtc_htotal;
	dst_mode->crtc_hskew = src_mode->crtc_hskew;
	dst_mode->crtc_vblank_start = src_mode->crtc_vblank_start;;
	dst_mode->crtc_vblank_end = src_mode->crtc_vblank_end;;
	dst_mode->crtc_vsync_start = src_mode->crtc_vsync_start;;
	dst_mode->crtc_vsync_end = src_mode->crtc_vsync_end;;
	dst_mode->crtc_vtotal = src_mode->crtc_vtotal;;
}

static void decide_crtc_timing_for_drm_display_mode(
		struct drm_display_mode *drm_mode,
		const struct drm_display_mode *native_mode,
		bool scale_enabled)
{
	if (scale_enabled) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else if (native_mode->clock == drm_mode->clock &&
			native_mode->htotal == drm_mode->htotal &&
			native_mode->vtotal == drm_mode->vtotal) {
		copy_crtc_timing_for_drm_display_mode(native_mode, drm_mode);
	} else {
		/* no scaling nor amdgpu inserted, no need to patch */
	}
}

static struct dc_target *create_target_for_sink(
		const struct amdgpu_connector *aconnector,
		const struct drm_display_mode *drm_mode,
		const struct dm_connector_state *dm_state)
{
	struct drm_display_mode *preferred_mode = NULL;
	const struct drm_connector *drm_connector;
	struct dc_target *target = NULL;
	struct dc_stream *stream;
	struct drm_display_mode mode = *drm_mode;
	bool native_mode_found = false;

	if (NULL == aconnector) {
		DRM_ERROR("aconnector is NULL!\n");
		goto drm_connector_null;
	}

	if (NULL == dm_state) {
		DRM_ERROR("dm_state is NULL!\n");
		goto dm_state_null;
	}

	drm_connector = &aconnector->base;
	stream = dc_create_stream_for_sink(aconnector->dc_sink);

	if (NULL == stream) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto stream_create_fail;
	}

	list_for_each_entry(preferred_mode, &aconnector->base.modes, head) {
		/* Search for preferred mode */
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			native_mode_found = true;
			break;
		}
	}
	if (!native_mode_found)
		preferred_mode = list_first_entry_or_null(
				&aconnector->base.modes,
				struct drm_display_mode,
				head);

	if (NULL == preferred_mode) {
		/* This may not be an error, the use case is when we we have no
		 * usermode calls to reset and set mode upon hotplug. In this
		 * case, we call set mode ourselves to restore the previous mode
		 * and the modelist may not be filled in in time.
		 */
		DRM_INFO("No preferred mode found\n");
	} else {
		decide_crtc_timing_for_drm_display_mode(
				&mode, preferred_mode,
				dm_state->scaling != RMX_OFF);
	}

	fill_stream_properties_from_drm_display_mode(stream,
			&mode, &aconnector->base);
	update_stream_scaling_settings(&mode, dm_state, stream);

	fill_audio_info(
		&stream->audio_info,
		drm_connector,
		aconnector->dc_sink);

	target = dc_create_target_for_streams(&stream, 1);
	dc_stream_release(stream);

	if (NULL == target) {
		DRM_ERROR("Failed to create target with streams!\n");
		goto target_create_fail;
	}

dm_state_null:
drm_connector_null:
target_create_fail:
stream_create_fail:
	return target;
}

void amdgpu_dm_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

/* Implemented only the options currently availible for the driver */
static const struct drm_crtc_funcs amdgpu_dm_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.cursor_set = dm_crtc_cursor_set,
	.cursor_move = dm_crtc_cursor_move,
	.destroy = amdgpu_dm_crtc_destroy,
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static enum drm_connector_status
amdgpu_dm_connector_detect(struct drm_connector *connector, bool force)
{
	bool connected;
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);

	/* Notes:
	 * 1. This interface is NOT called in context of HPD irq.
	 * 2. This interface *is called* in context of user-mode ioctl. Which
	 * makes it a bad place for *any* MST-related activit. */

	if (aconnector->base.force == DRM_FORCE_UNSPECIFIED)
		connected = (aconnector->dc_sink != NULL);
	else
		connected = (aconnector->base.force == DRM_FORCE_ON);

	return (connected ? connector_status_connected :
			connector_status_disconnected);
}

int amdgpu_dm_connector_atomic_set_property(
	struct drm_connector *connector,
	struct drm_connector_state *connector_state,
	struct drm_property *property,
	uint64_t val)
{
	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct dm_connector_state *dm_old_state =
		to_dm_connector_state(connector->state);
	struct dm_connector_state *dm_new_state =
		to_dm_connector_state(connector_state);

	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;
	int ret = -EINVAL;

	if (property == dev->mode_config.scaling_mode_property) {
		enum amdgpu_rmx_type rmx_type;

		switch (val) {
		case DRM_MODE_SCALE_CENTER:
			rmx_type = RMX_CENTER;
			break;
		case DRM_MODE_SCALE_ASPECT:
			rmx_type = RMX_ASPECT;
			break;
		case DRM_MODE_SCALE_FULLSCREEN:
			rmx_type = RMX_FULL;
			break;
		case DRM_MODE_SCALE_NONE:
		default:
			rmx_type = RMX_OFF;
			break;
		}

		if (dm_old_state->scaling == rmx_type)
			return 0;

		dm_new_state->scaling = rmx_type;
		ret = 0;
	} else if (property == adev->mode_info.underscan_hborder_property) {
		dm_new_state->underscan_hborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_vborder_property) {
		dm_new_state->underscan_vborder = val;
		ret = 0;
	} else if (property == adev->mode_info.underscan_property) {
		dm_new_state->underscan_enable = val;
		ret = 0;
	}

	for_each_crtc_in_state(
		connector_state->state,
		crtc,
		new_crtc_state,
		i) {

		if (crtc == connector_state->crtc) {
			struct drm_plane_state *plane_state;

			/*
			 * Bit of magic done here. We need to ensure
			 * that planes get update after mode is set.
			 * So, we need to add primary plane to state,
			 * and this way atomic_update would be called
			 * for it
			 */
			plane_state =
				drm_atomic_get_plane_state(
					connector_state->state,
					crtc->primary);

			if (!plane_state)
				return -EINVAL;
		}
	}

	return ret;
}

void amdgpu_dm_connector_destroy(struct drm_connector *connector)
{
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
	const struct dc_link *link = aconnector->dc_link;
	struct amdgpu_device *adev = connector->dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	if (link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) {
		amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev) {
			backlight_device_unregister(dm->backlight_dev);
			dm->backlight_dev = NULL;
		}

	}
#endif
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	kfree(state);

	state = kzalloc(sizeof(*state), GFP_KERNEL);

	if (state) {
		state->scaling = RMX_OFF;
		state->underscan_enable = false;
		state->underscan_hborder = 0;
		state->underscan_vborder = 0;

		connector->state = &state->base;
		connector->state->connector = connector;
	}
}

struct drm_connector_state *amdgpu_dm_connector_atomic_duplicate_state(
	struct drm_connector *connector)
{
	struct dm_connector_state *state =
		to_dm_connector_state(connector->state);

	struct dm_connector_state *new_state =
			kmemdup(state, sizeof(*state), GFP_KERNEL);

	if (new_state) {
		__drm_atomic_helper_connector_duplicate_state(connector,
								      &new_state->base);
		return &new_state->base;
	}

	return NULL;
}

static const struct drm_connector_funcs amdgpu_dm_connector_funcs = {
	.reset = amdgpu_dm_connector_funcs_reset,
	.detect = amdgpu_dm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = amdgpu_dm_connector_destroy,
	.atomic_duplicate_state = amdgpu_dm_connector_atomic_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = amdgpu_dm_connector_atomic_set_property
};

static struct drm_encoder *best_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	DRM_DEBUG_KMS("Finding the best encoder\n");

	/* pick the encoder ids */
	if (enc_id) {
		obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
		if (!obj) {
			DRM_ERROR("Couldn't find a matching encoder for our connector\n");
			return NULL;
		}
		encoder = obj_to_encoder(obj);
		return encoder;
	}
	DRM_ERROR("No encoder id\n");
	return NULL;
}

static int get_modes(struct drm_connector *connector)
{
	return amdgpu_dm_connector_get_modes(connector);
}

static void create_eml_sink(struct amdgpu_connector *aconnector)
{
	struct dc_sink_init_data init_params = {
			.link = aconnector->dc_link,
			.sink_signal = SIGNAL_TYPE_VIRTUAL
	};
	struct edid *edid = (struct edid *) aconnector->base.edid_blob_ptr->data;

	if (!aconnector->base.edid_blob_ptr ||
		!aconnector->base.edid_blob_ptr->data) {
		DRM_ERROR("No EDID firmware found on connector: %s ,forcing to OFF!\n",
				aconnector->base.name);

		aconnector->base.force = DRM_FORCE_OFF;
		aconnector->base.override_edid = false;
		return;
	}

	aconnector->edid = edid;

	aconnector->dc_em_sink = dc_link_add_remote_sink(
		aconnector->dc_link,
		(uint8_t *)edid,
		(edid->extensions + 1) * EDID_LENGTH,
		&init_params);

	if (aconnector->base.force
					== DRM_FORCE_ON)
		aconnector->dc_sink = aconnector->dc_link->local_sink ?
		aconnector->dc_link->local_sink :
		aconnector->dc_em_sink;
}

static void handle_edid_mgmt(struct amdgpu_connector *aconnector)
{
	struct dc_link *link = (struct dc_link *)aconnector->dc_link;

	/* In case of headless boot with force on for DP managed connector
	 * Those settings have to be != 0 to get initial modeset
	 */
	if (link->connector_signal == SIGNAL_TYPE_DISPLAY_PORT) {
		link->verified_link_cap.lane_count = LANE_COUNT_FOUR;
		link->verified_link_cap.link_rate = LINK_RATE_HIGH2;
	}


	aconnector->base.override_edid = true;
	create_eml_sink(aconnector);
}

int amdgpu_dm_connector_mode_valid(
		struct drm_connector *connector,
		struct drm_display_mode *mode)
{
	int result = MODE_ERROR;
	const struct dc_sink *dc_sink;
	struct amdgpu_device *adev = connector->dev->dev_private;
	struct dc_validation_set val_set = { 0 };
	/* TODO: Unhardcode stream count */
	struct dc_stream *streams[1];
	struct dc_target *target;
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	/* Only run this the first time mode_valid is called to initilialize
	 * EDID mgmt
	 */
	if (aconnector->base.force != DRM_FORCE_UNSPECIFIED &&
		!aconnector->dc_em_sink)
		handle_edid_mgmt(aconnector);

	dc_sink = to_amdgpu_connector(connector)->dc_sink;

	if (NULL == dc_sink) {
		DRM_ERROR("dc_sink is NULL!\n");
		goto stream_create_fail;
	}

	streams[0] = dc_create_stream_for_sink(dc_sink);

	if (NULL == streams[0]) {
		DRM_ERROR("Failed to create stream for sink!\n");
		goto stream_create_fail;
	}

	drm_mode_set_crtcinfo(mode, 0);
	fill_stream_properties_from_drm_display_mode(streams[0], mode, connector);

	target = dc_create_target_for_streams(streams, 1);
	val_set.target = target;

	if (NULL == val_set.target) {
		DRM_ERROR("Failed to create target with stream!\n");
		goto target_create_fail;
	}

	val_set.surface_count = 0;
	streams[0]->src.width = mode->hdisplay;
	streams[0]->src.height = mode->vdisplay;
	streams[0]->dst = streams[0]->src;

	if (dc_validate_resources(adev->dm.dc, &val_set, 1))
		result = MODE_OK;

	dc_target_release(target);
target_create_fail:
	dc_stream_release(streams[0]);
stream_create_fail:
	/* TODO: error handling*/
	return result;
}

static const struct drm_connector_helper_funcs
amdgpu_dm_connector_helper_funcs = {
	/*
	* If hotplug a second bigger display in FB Con mode, bigger resolution
	* modes will be filtered by drm_mode_validate_size(), and those modes
	* is missing after user start lightdm. So we need to renew modes list.
	* in get_modes call back, not just return the modes count
	*/
	.get_modes = get_modes,
	.mode_valid = amdgpu_dm_connector_mode_valid,
	.best_encoder = best_encoder
};

static void dm_crtc_helper_disable(struct drm_crtc *crtc)
{
}

static int dm_crtc_helper_atomic_check(
	struct drm_crtc *crtc,
	struct drm_crtc_state *state)
{
	return 0;
}

static bool dm_crtc_helper_mode_fixup(
	struct drm_crtc *crtc,
	const struct drm_display_mode *mode,
	struct drm_display_mode *adjusted_mode)
{
	return true;
}

static const struct drm_crtc_helper_funcs amdgpu_dm_crtc_helper_funcs = {
	.disable = dm_crtc_helper_disable,
	.atomic_check = dm_crtc_helper_atomic_check,
	.mode_fixup = dm_crtc_helper_mode_fixup
};

static void dm_encoder_helper_disable(struct drm_encoder *encoder)
{

}

static int dm_encoder_helper_atomic_check(
	struct drm_encoder *encoder,
	struct drm_crtc_state *crtc_state,
	struct drm_connector_state *conn_state)
{
	return 0;
}

const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs = {
	.disable = dm_encoder_helper_disable,
	.atomic_check = dm_encoder_helper_atomic_check
};

static const struct drm_plane_funcs dm_plane_funcs = {
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state
};

static void clear_unrelated_fields(struct drm_plane_state *state)
{
	state->crtc = NULL;
	state->fb = NULL;
	state->state = NULL;
	state->fence = NULL;
}

static bool page_flip_needed(
	const struct drm_plane_state *new_state,
	const struct drm_plane_state *old_state,
	struct drm_pending_vblank_event *event,
	bool commit_surface_required)
{
	struct drm_plane_state old_state_tmp;
	struct drm_plane_state new_state_tmp;

	struct amdgpu_framebuffer *amdgpu_fb_old;
	struct amdgpu_framebuffer *amdgpu_fb_new;
	struct amdgpu_crtc *acrtc_new;

	uint64_t old_tiling_flags;
	uint64_t new_tiling_flags;

	bool page_flip_required;

	if (!old_state)
		return false;

	if (!old_state->fb)
		return false;

	if (!new_state)
		return false;

	if (!new_state->fb)
		return false;

	old_state_tmp = *old_state;
	new_state_tmp = *new_state;

	if (!event)
		return false;

	amdgpu_fb_old = to_amdgpu_framebuffer(old_state->fb);
	amdgpu_fb_new = to_amdgpu_framebuffer(new_state->fb);

	if (!get_fb_info(amdgpu_fb_old, &old_tiling_flags, NULL))
		return false;

	if (!get_fb_info(amdgpu_fb_new, &new_tiling_flags, NULL))
		return false;

	if (commit_surface_required == true &&
	    old_tiling_flags != new_tiling_flags)
		return false;

	clear_unrelated_fields(&old_state_tmp);
	clear_unrelated_fields(&new_state_tmp);

	page_flip_required = memcmp(&old_state_tmp,
				    &new_state_tmp,
				    sizeof(old_state_tmp)) == 0 ? true:false;
	if (new_state->crtc && page_flip_required == false) {
		acrtc_new = to_amdgpu_crtc(new_state->crtc);
		if (acrtc_new->flip_flags & DRM_MODE_PAGE_FLIP_ASYNC)
			page_flip_required = true;
	}
	return page_flip_required;
}

static int dm_plane_helper_prepare_fb(
	struct drm_plane *plane,
	struct drm_plane_state *new_state)
{
	struct amdgpu_framebuffer *afb;
	struct drm_gem_object *obj;
	struct amdgpu_bo *rbo;
	int r;

	if (!new_state->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	afb = to_amdgpu_framebuffer(new_state->fb);

	obj = afb->obj;
	rbo = gem_to_amdgpu_bo(obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r != 0))
		return r;

	r = amdgpu_bo_pin(rbo, AMDGPU_GEM_DOMAIN_VRAM, NULL);

	amdgpu_bo_unreserve(rbo);

	if (unlikely(r != 0)) {
		DRM_ERROR("Failed to pin framebuffer\n");
		return r;
	}

	return 0;
}

static void dm_plane_helper_cleanup_fb(
	struct drm_plane *plane,
	struct drm_plane_state *old_state)
{
	struct amdgpu_bo *rbo;
	struct amdgpu_framebuffer *afb;
	int r;

	if (!old_state->fb)
		return;

	afb = to_amdgpu_framebuffer(old_state->fb);
	rbo = gem_to_amdgpu_bo(afb->obj);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r)) {
		DRM_ERROR("failed to reserve rbo before unpin\n");
		return;
	} else {
		amdgpu_bo_unpin(rbo);
		amdgpu_bo_unreserve(rbo);
	}
}

int dm_create_validation_set_for_target(struct drm_connector *connector,
		struct drm_display_mode *mode, struct dc_validation_set *val_set)
{
	int result = MODE_ERROR;
	const struct dc_sink *dc_sink =
			to_amdgpu_connector(connector)->dc_sink;
	/* TODO: Unhardcode stream count */
	struct dc_stream *streams[1];
	struct dc_target *target;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) ||
			(mode->flags & DRM_MODE_FLAG_DBLSCAN))
		return result;

	if (NULL == dc_sink) {
		DRM_ERROR("dc_sink is NULL!\n");
		return result;
	}

	streams[0] = dc_create_stream_for_sink(dc_sink);

	if (NULL == streams[0]) {
		DRM_ERROR("Failed to create stream for sink!\n");
		return result;
	}

	drm_mode_set_crtcinfo(mode, 0);

	fill_stream_properties_from_drm_display_mode(streams[0], mode, connector);

	target = dc_create_target_for_streams(streams, 1);
	val_set->target = target;

	if (NULL == val_set->target) {
		DRM_ERROR("Failed to create target with stream!\n");
		goto fail;
	}

	streams[0]->src.width = mode->hdisplay;
	streams[0]->src.height = mode->vdisplay;
	streams[0]->dst = streams[0]->src;

	return MODE_OK;

fail:
	dc_stream_release(streams[0]);
	return result;

}

static const struct drm_plane_helper_funcs dm_plane_helper_funcs = {
	.prepare_fb = dm_plane_helper_prepare_fb,
	.cleanup_fb = dm_plane_helper_cleanup_fb,
};

/*
 * TODO: these are currently initialized to rgb formats only.
 * For future use cases we should either initialize them dynamically based on
 * plane capabilities, or initialize this array to all formats, so internal drm
 * check will succeed, and let DC to implement proper check
 */
static uint32_t rgb_formats[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
};

int amdgpu_dm_crtc_init(struct amdgpu_display_manager *dm,
			struct amdgpu_crtc *acrtc,
			uint32_t crtc_index)
{
	int res = -ENOMEM;

	struct drm_plane *primary_plane =
		kzalloc(sizeof(*primary_plane), GFP_KERNEL);

	if (!primary_plane)
		goto fail_plane;

	primary_plane->format_default = true;

	res = drm_universal_plane_init(
		dm->adev->ddev,
		primary_plane,
		0,
		&dm_plane_funcs,
		rgb_formats,
		ARRAY_SIZE(rgb_formats),
		NULL,
		DRM_PLANE_TYPE_PRIMARY, NULL);

	primary_plane->crtc = &acrtc->base;

	drm_plane_helper_add(primary_plane, &dm_plane_helper_funcs);

	res = drm_crtc_init_with_planes(
			dm->ddev,
			&acrtc->base,
			primary_plane,
			NULL,
			&amdgpu_dm_crtc_funcs, NULL);

	if (res)
		goto fail;

	drm_crtc_helper_add(&acrtc->base, &amdgpu_dm_crtc_helper_funcs);

	acrtc->max_cursor_width = 128;
	acrtc->max_cursor_height = 128;

	acrtc->crtc_id = crtc_index;
	acrtc->base.enabled = false;

	dm->adev->mode_info.crtcs[crtc_index] = acrtc;
	drm_mode_crtc_set_gamma_size(&acrtc->base, 256);

	return 0;
fail:
	kfree(primary_plane);
fail_plane:
	acrtc->crtc_id = -1;
	return res;
}

static int to_drm_connector_type(enum signal_type st)
{
	switch (st) {
	case SIGNAL_TYPE_HDMI_TYPE_A:
		return DRM_MODE_CONNECTOR_HDMIA;
	case SIGNAL_TYPE_EDP:
		return DRM_MODE_CONNECTOR_eDP;
	case SIGNAL_TYPE_RGB:
		return DRM_MODE_CONNECTOR_VGA;
	case SIGNAL_TYPE_DISPLAY_PORT:
	case SIGNAL_TYPE_DISPLAY_PORT_MST:
		return DRM_MODE_CONNECTOR_DisplayPort;
	case SIGNAL_TYPE_DVI_DUAL_LINK:
	case SIGNAL_TYPE_DVI_SINGLE_LINK:
		return DRM_MODE_CONNECTOR_DVID;
	case SIGNAL_TYPE_VIRTUAL:
		return DRM_MODE_CONNECTOR_VIRTUAL;

	default:
		return DRM_MODE_CONNECTOR_Unknown;
	}
}

static void amdgpu_dm_get_native_mode(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
		connector->helper_private;
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	encoder = helper->best_encoder(connector);

	if (encoder == NULL)
		return;

	amdgpu_encoder = to_amdgpu_encoder(encoder);

	amdgpu_encoder->native_mode.clock = 0;

	if (!list_empty(&connector->probed_modes)) {
		struct drm_display_mode *preferred_mode = NULL;
		list_for_each_entry(preferred_mode,
				&connector->probed_modes,
				head) {
		if (preferred_mode->type & DRM_MODE_TYPE_PREFERRED) {
			amdgpu_encoder->native_mode = *preferred_mode;
		}
			break;
		}

	}
}

static struct drm_display_mode *amdgpu_dm_create_common_mode(
		struct drm_encoder *encoder, char *name,
		int hdisplay, int vdisplay)
{
	struct drm_device *dev = encoder->dev;
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;

	mode = drm_mode_duplicate(dev, native_mode);

	if(mode == NULL)
		return NULL;

	mode->hdisplay = hdisplay;
	mode->vdisplay = vdisplay;
	mode->type &= ~DRM_MODE_TYPE_PREFERRED;
	strncpy(mode->name, name, DRM_DISPLAY_MODE_LEN);

	return mode;

}

static void amdgpu_dm_connector_add_common_modes(struct drm_encoder *encoder,
					struct drm_connector *connector)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);
	struct drm_display_mode *mode = NULL;
	struct drm_display_mode *native_mode = &amdgpu_encoder->native_mode;
	struct amdgpu_connector *amdgpu_connector =
				to_amdgpu_connector(connector);
	int i;
	int n;
	struct mode_size {
		char name[DRM_DISPLAY_MODE_LEN];
		int w;
		int h;
	}common_modes[] = {
		{  "640x480",  640,  480},
		{  "800x600",  800,  600},
		{ "1024x768", 1024,  768},
		{ "1280x720", 1280,  720},
		{ "1280x800", 1280,  800},
		{"1280x1024", 1280, 1024},
		{ "1440x900", 1440,  900},
		{"1680x1050", 1680, 1050},
		{"1600x1200", 1600, 1200},
		{"1920x1080", 1920, 1080},
		{"1920x1200", 1920, 1200}
	};

	n = sizeof(common_modes) / sizeof(common_modes[0]);

	for (i = 0; i < n; i++) {
		struct drm_display_mode *curmode = NULL;
		bool mode_existed = false;

		if (common_modes[i].w > native_mode->hdisplay ||
			common_modes[i].h > native_mode->vdisplay ||
			(common_modes[i].w == native_mode->hdisplay &&
			common_modes[i].h == native_mode->vdisplay))
				continue;

		list_for_each_entry(curmode, &connector->probed_modes, head) {
			if (common_modes[i].w == curmode->hdisplay &&
				common_modes[i].h == curmode->vdisplay) {
				mode_existed = true;
				break;
			}
		}

		if (mode_existed)
			continue;

		mode = amdgpu_dm_create_common_mode(encoder,
				common_modes[i].name, common_modes[i].w,
				common_modes[i].h);
		drm_mode_probed_add(connector, mode);
		amdgpu_connector->num_modes++;
	}
}

static void amdgpu_dm_connector_ddc_get_modes(
	struct drm_connector *connector,
	struct edid *edid)
{
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);

	if (edid) {
		/* empty probed_modes */
		INIT_LIST_HEAD(&connector->probed_modes);
		amdgpu_connector->num_modes =
				drm_add_edid_modes(connector, edid);

		drm_edid_to_eld(connector, edid);

		amdgpu_dm_get_native_mode(connector);
	} else
		amdgpu_connector->num_modes = 0;
}

int amdgpu_dm_connector_get_modes(struct drm_connector *connector)
{
	const struct drm_connector_helper_funcs *helper =
			connector->helper_private;
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);
	struct drm_encoder *encoder;
	struct edid *edid = amdgpu_connector->edid;

	encoder = helper->best_encoder(connector);

	amdgpu_dm_connector_ddc_get_modes(connector, edid);
	amdgpu_dm_connector_add_common_modes(encoder, connector);
	return amdgpu_connector->num_modes;
}

void amdgpu_dm_connector_init_helper(
	struct amdgpu_display_manager *dm,
	struct amdgpu_connector *aconnector,
	int connector_type,
	const struct dc_link *link,
	int link_index)
{
	struct amdgpu_device *adev = dm->ddev->dev_private;

	aconnector->connector_id = link_index;
	aconnector->dc_link = link;
	aconnector->base.interlace_allowed = true;
	aconnector->base.doublescan_allowed = true;
	aconnector->base.dpms = DRM_MODE_DPMS_OFF;
	aconnector->hpd.hpd = AMDGPU_HPD_NONE; /* not used */

	mutex_init(&aconnector->hpd_lock);

	/*configure suport HPD hot plug connector_>polled default value is 0
	 * which means HPD hot plug not supported*/
	switch (connector_type) {
	case DRM_MODE_CONNECTOR_HDMIA:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case DRM_MODE_CONNECTOR_DisplayPort:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	case DRM_MODE_CONNECTOR_DVID:
		aconnector->base.polled = DRM_CONNECTOR_POLL_HPD;
		break;
	default:
		break;
	}

	drm_object_attach_property(&aconnector->base.base,
				dm->ddev->mode_config.scaling_mode_property,
				DRM_MODE_SCALE_NONE);

	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_property,
				UNDERSCAN_OFF);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_hborder_property,
				0);
	drm_object_attach_property(&aconnector->base.base,
				adev->mode_info.underscan_vborder_property,
				0);

}

int amdgpu_dm_i2c_xfer(struct i2c_adapter *i2c_adap,
		      struct i2c_msg *msgs, int num)
{
	struct amdgpu_i2c_adapter *i2c = i2c_get_adapdata(i2c_adap);
	struct i2c_command cmd;
	int i;
	int result = -EIO;

	cmd.payloads = kzalloc(num * sizeof(struct i2c_payload), GFP_KERNEL);

	if (!cmd.payloads)
		return result;

	cmd.number_of_payloads = num;
	cmd.engine = I2C_COMMAND_ENGINE_DEFAULT;
	cmd.speed = 100;

	for (i = 0; i < num; i++) {
		cmd.payloads[i].write = (msgs[i].flags & I2C_M_RD);
		cmd.payloads[i].address = msgs[i].addr;
		cmd.payloads[i].length = msgs[i].len;
		cmd.payloads[i].data = msgs[i].buf;
	}

	if (dc_submit_i2c(i2c->dm->dc, i2c->link_index, &cmd))
		result = num;

	kfree(cmd.payloads);

	return result;
}

u32 amdgpu_dm_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm amdgpu_dm_i2c_algo = {
	.master_xfer = amdgpu_dm_i2c_xfer,
	.functionality = amdgpu_dm_i2c_func,
};

struct amdgpu_i2c_adapter *create_i2c(unsigned int link_index, struct amdgpu_display_manager *dm, int *res)
{
	struct amdgpu_i2c_adapter *i2c;

	i2c = kzalloc(sizeof (struct amdgpu_i2c_adapter), GFP_KERNEL);
	i2c->dm = dm;
	i2c->base.owner = THIS_MODULE;
	i2c->base.class = I2C_CLASS_DDC;
	i2c->base.dev.parent = &dm->adev->pdev->dev;
	i2c->base.algo = &amdgpu_dm_i2c_algo;
	snprintf(i2c->base.name, sizeof (i2c->base.name), "AMDGPU DM i2c hw bus %d", link_index);
	i2c->link_index = link_index;
	i2c_set_adapdata(&i2c->base, i2c);

	return i2c;
}

/* Note: this function assumes that dc_link_detect() was called for the
 * dc_link which will be represented by this aconnector. */
int amdgpu_dm_connector_init(
	struct amdgpu_display_manager *dm,
	struct amdgpu_connector *aconnector,
	uint32_t link_index,
	struct amdgpu_encoder *aencoder)
{
	int res = 0;
	int connector_type;
	struct dc *dc = dm->dc;
	const struct dc_link *link = dc_get_link_at_index(dc, link_index);
	struct amdgpu_i2c_adapter *i2c;

	DRM_DEBUG_KMS("%s()\n", __func__);

	i2c = create_i2c(link->link_index, dm, &res);
	aconnector->i2c = i2c;
	res = i2c_add_adapter(&i2c->base);

	if (res) {
		DRM_ERROR("Failed to register hw i2c %d\n", link->link_index);
		goto out_free;
	}

	connector_type = to_drm_connector_type(link->connector_signal);

	res = drm_connector_init(
			dm->ddev,
			&aconnector->base,
			&amdgpu_dm_connector_funcs,
			connector_type);

	if (res) {
		DRM_ERROR("connector_init failed\n");
		aconnector->connector_id = -1;
		goto out_free;
	}

	drm_connector_helper_add(
			&aconnector->base,
			&amdgpu_dm_connector_helper_funcs);

	amdgpu_dm_connector_init_helper(
		dm,
		aconnector,
		connector_type,
		link,
		link_index);

	drm_mode_connector_attach_encoder(
		&aconnector->base, &aencoder->base);

	drm_connector_register(&aconnector->base);

	if (connector_type == DRM_MODE_CONNECTOR_DisplayPort
		|| connector_type == DRM_MODE_CONNECTOR_eDP)
		amdgpu_dm_initialize_mst_connector(dm, aconnector);

#if defined(CONFIG_BACKLIGHT_CLASS_DEVICE) ||\
	defined(CONFIG_BACKLIGHT_CLASS_DEVICE_MODULE)

	/* NOTE: this currently will create backlight device even if a panel
	 * is not connected to the eDP/LVDS connector.
	 *
	 * This is less than ideal but we don't have sink information at this
	 * stage since detection happens after. We can't do detection earlier
	 * since MST detection needs connectors to be created first.
	 */
	if (link->connector_signal & (SIGNAL_TYPE_EDP | SIGNAL_TYPE_LVDS)) {
		/* Event if registration failed, we should continue with
		 * DM initialization because not having a backlight control
		 * is better then a black screen. */
		amdgpu_dm_register_backlight_device(dm);

		if (dm->backlight_dev)
			dm->backlight_link = link;
	}
#endif

out_free:
	if (res) {
		kfree(i2c);
		aconnector->i2c = NULL;
	}
	return res;
}

int amdgpu_dm_get_encoder_crtc_mask(struct amdgpu_device *adev)
{
	switch (adev->mode_info.num_crtc) {
	case 1:
		return 0x1;
	case 2:
		return 0x3;
	case 3:
		return 0x7;
	case 4:
		return 0xf;
	case 5:
		return 0x1f;
	case 6:
	default:
		return 0x3f;
	}
}

int amdgpu_dm_encoder_init(
	struct drm_device *dev,
	struct amdgpu_encoder *aencoder,
	uint32_t link_index)
{
	struct amdgpu_device *adev = dev->dev_private;

	int res = drm_encoder_init(dev,
				   &aencoder->base,
				   &amdgpu_dm_encoder_funcs,
				   DRM_MODE_ENCODER_TMDS,
				   NULL);

	aencoder->base.possible_crtcs = amdgpu_dm_get_encoder_crtc_mask(adev);

	if (!res)
		aencoder->encoder_id = link_index;
	else
		aencoder->encoder_id = -1;

	drm_encoder_helper_add(&aencoder->base, &amdgpu_dm_encoder_helper_funcs);

	return res;
}

enum dm_commit_action {
	DM_COMMIT_ACTION_NOTHING,
	DM_COMMIT_ACTION_RESET,
	DM_COMMIT_ACTION_DPMS_ON,
	DM_COMMIT_ACTION_DPMS_OFF,
	DM_COMMIT_ACTION_SET
};

static enum dm_commit_action get_dm_commit_action(struct drm_crtc_state *state)
{
	/* mode changed means either actually mode changed or enabled changed */
	/* active changed means dpms changed */

	DRM_DEBUG_KMS("crtc_state_flags: enable:%d, active:%d, planes_changed:%d, mode_changed:%d,active_changed:%d,connectors_changed:%d\n",
			state->enable,
			state->active,
			state->planes_changed,
			state->mode_changed,
			state->active_changed,
			state->connectors_changed);

	if (state->mode_changed) {
		/* if it is got disabled - call reset mode */
		if (!state->enable)
			return DM_COMMIT_ACTION_RESET;

		if (state->active)
			return DM_COMMIT_ACTION_SET;
		else
			return DM_COMMIT_ACTION_RESET;
	} else {
		/* ! mode_changed */

		/* if it is remain disable - skip it */
		if (!state->enable)
			return DM_COMMIT_ACTION_NOTHING;

		if (state->active && state->connectors_changed)
			return DM_COMMIT_ACTION_SET;

		if (state->active_changed) {
			if (state->active) {
				return DM_COMMIT_ACTION_DPMS_ON;
			} else {
				return DM_COMMIT_ACTION_DPMS_OFF;
			}
		} else {
			/* ! active_changed */
			return DM_COMMIT_ACTION_NOTHING;
		}
	}
}


typedef bool (*predicate)(struct amdgpu_crtc *acrtc);

static void wait_while_pflip_status(struct amdgpu_device *adev,
		struct amdgpu_crtc *acrtc, predicate f) {
	int count = 0;
	while (f(acrtc)) {
		/* Spin Wait*/
		msleep(1);
		count++;
		if (count == 1000) {
			DRM_ERROR("%s - crtc:%d[%p], pflip_stat:%d, probable hang!\n",
										__func__, acrtc->crtc_id,
										acrtc,
										acrtc->pflip_status);

			/* we do not expect to hit this case except on Polaris with PHY PLL
			 * 1. DP to HDMI passive dongle connected
			 * 2. unplug (headless)
			 * 3. plug in DP
			 * 3a. on plug in, DP will try verify link by training, and training
			 * would disable PHY PLL which HDMI rely on to drive TG
			 * 3b. this will cause flip interrupt cannot be generated, and we
			 * exit when timeout expired.  however we do not have code to clean
			 * up flip, flip clean up will happen when the address is written
			 * with the restore mode change
			 */
			WARN_ON(1);
			break;
		}
	}

	DRM_DEBUG_DRIVER("%s - Finished waiting for:%d msec, crtc:%d[%p], pflip_stat:%d \n",
											__func__,
											count,
											acrtc->crtc_id,
											acrtc,
											acrtc->pflip_status);
}

static bool pflip_in_progress_predicate(struct amdgpu_crtc *acrtc)
{
	return acrtc->pflip_status != AMDGPU_FLIP_NONE;
}

static void manage_dm_interrupts(
	struct amdgpu_device *adev,
	struct amdgpu_crtc *acrtc,
	bool enable)
{
	/*
	 * this is not correct translation but will work as soon as VBLANK
	 * constant is the same as PFLIP
	 */
	int irq_type =
		amdgpu_crtc_idx_to_irq_type(
			adev,
			acrtc->crtc_id);

	if (enable) {
		drm_crtc_vblank_on(&acrtc->base);
		amdgpu_irq_get(
			adev,
			&adev->pageflip_irq,
			irq_type);
	} else {
		wait_while_pflip_status(adev, acrtc,
				pflip_in_progress_predicate);

		amdgpu_irq_put(
			adev,
			&adev->pageflip_irq,
			irq_type);
		drm_crtc_vblank_off(&acrtc->base);
	}
}


static bool pflip_pending_predicate(struct amdgpu_crtc *acrtc)
{
	return acrtc->pflip_status == AMDGPU_FLIP_PENDING;
}

static bool is_scaling_state_different(
		const struct dm_connector_state *dm_state,
		const struct dm_connector_state *old_dm_state)
{
	if (dm_state->scaling != old_dm_state->scaling)
		return true;
	if (!dm_state->underscan_enable && old_dm_state->underscan_enable) {
		if (old_dm_state->underscan_hborder != 0 && old_dm_state->underscan_vborder != 0)
			return true;
	} else  if (dm_state->underscan_enable && !old_dm_state->underscan_enable) {
		if (dm_state->underscan_hborder != 0 && dm_state->underscan_vborder != 0)
			return true;
	} else if (dm_state->underscan_hborder != old_dm_state->underscan_hborder
				|| dm_state->underscan_vborder != old_dm_state->underscan_vborder)
			return true;
	return false;
}

static void remove_target(struct amdgpu_device *adev, struct amdgpu_crtc *acrtc)
{
	int i;

	/*
	 * we evade vblanks and pflips on crtc that
	 * should be changed
	 */
	manage_dm_interrupts(adev, acrtc, false);
	/* this is the update mode case */
	if (adev->dm.freesync_module)
		for (i = 0; i < acrtc->target->stream_count; i++)
			mod_freesync_remove_stream(
					adev->dm.freesync_module,
					acrtc->target->streams[i]);
	dc_target_release(acrtc->target);
	acrtc->target = NULL;
	acrtc->otg_inst = -1;
	acrtc->enabled = false;
}

int amdgpu_dm_atomic_commit(
	struct drm_device *dev,
	struct drm_atomic_state *state,
	bool async)
{
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_display_manager *dm = &adev->dm;
	struct drm_plane *plane;
	struct drm_plane_state *new_plane_state;
	struct drm_plane_state *old_plane_state;
	uint32_t i, j;
	int32_t ret = 0;
	uint32_t commit_targets_count = 0;
	uint32_t new_crtcs_count = 0;
	uint32_t flip_crtcs_count = 0;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;

	struct dc_target *commit_targets[MAX_TARGETS];
	struct amdgpu_crtc *new_crtcs[MAX_TARGETS];
	struct dc_target *new_target;
	struct drm_crtc *flip_crtcs[MAX_TARGETS];
	struct amdgpu_flip_work *work[MAX_TARGETS] = {0};
	struct amdgpu_bo *new_abo[MAX_TARGETS] = {0};

	/* In this step all new fb would be pinned */

	/*
	 * TODO: Revisit when we support true asynchronous commit.
	 * Right now we receive async commit only from pageflip, in which case
	 * we should not pin/unpin the fb here, it should be done in
	 * amdgpu_crtc_flip and from the vblank irq handler.
	 */
	if (!async) {
		ret = drm_atomic_helper_prepare_planes(dev, state);
		if (ret)
			return ret;
	}

	/* Page flip if needed */
	for_each_plane_in_state(state, plane, new_plane_state, i) {
		struct drm_plane_state *old_plane_state = plane->state;
		struct drm_crtc *crtc = new_plane_state->crtc;
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		struct drm_framebuffer *fb = new_plane_state->fb;
		struct drm_crtc_state *crtc_state;

		if (!fb || !crtc)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);

		if (!crtc_state->planes_changed || !crtc_state->active)
			continue;

		if (page_flip_needed(
				new_plane_state,
				old_plane_state,
				crtc_state->event,
				false)) {
			ret = amdgpu_crtc_prepare_flip(crtc,
							fb,
							crtc_state->event,
							acrtc->flip_flags,
							drm_crtc_vblank_count(crtc),
							&work[flip_crtcs_count],
							&new_abo[flip_crtcs_count]);

			if (ret) {
				/* According to atomic_commit hook API, EINVAL is not allowed */
				if (unlikely(ret == -EINVAL))
					ret = -ENOMEM;

				DRM_ERROR("Atomic commit: Flip for  crtc id %d: [%p], "
									"failed, errno = %d\n",
									acrtc->crtc_id,
									acrtc,
									ret);
				/* cleanup all flip configurations which
				 * succeeded in this commit
				 */
				for (i = 0; i < flip_crtcs_count; i++)
					amdgpu_crtc_cleanup_flip_ctx(
							work[i],
							new_abo[i]);

				return ret;
			}

			flip_crtcs[flip_crtcs_count] = crtc;
			flip_crtcs_count++;
		}
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(state, true);

	/*
	 * From this point state become old state really. New state is
	 * initialized to appropriate objects and could be accessed from there
	 */

	/*
	 * there is no fences usage yet in state. We can skip the following line
	 * wait_for_fences(dev, state);
	 */

	drm_atomic_helper_update_legacy_modeset_state(dev, state);

	/* update changed items */
	for_each_crtc_in_state(state, crtc, old_crtc_state, i) {
		struct amdgpu_crtc *acrtc;
		struct amdgpu_connector *aconnector = NULL;
		enum dm_commit_action action;
		struct drm_crtc_state *new_state = crtc->state;

		acrtc = to_amdgpu_crtc(crtc);

		aconnector =
			amdgpu_dm_find_first_crct_matching_connector(
				state,
				crtc,
				false);

		/* handles headless hotplug case, updating new_state and
		 * aconnector as needed
		 */

		action = get_dm_commit_action(new_state);

		switch (action) {
		case DM_COMMIT_ACTION_DPMS_ON:
		case DM_COMMIT_ACTION_SET: {
			struct dm_connector_state *dm_state = NULL;
			new_target = NULL;

			if (aconnector)
				dm_state = to_dm_connector_state(aconnector->base.state);

			new_target = create_target_for_sink(
					aconnector,
					&crtc->state->mode,
					dm_state);

			DRM_INFO("Atomic commit: SET crtc id %d: [%p]\n", acrtc->crtc_id, acrtc);

			if (!new_target) {
				/*
				 * this could happen because of issues with
				 * userspace notifications delivery.
				 * In this case userspace tries to set mode on
				 * display which is disconnect in fact.
				 * dc_sink in NULL in this case on aconnector.
				 * We expect reset mode will come soon.
				 *
				 * This can also happen when unplug is done
				 * during resume sequence ended
				 *
				 * In this case, we want to pretend we still
				 * have a sink to keep the pipe running so that
				 * hw state is consistent with the sw state
				 */
				DRM_DEBUG_KMS("%s: Failed to create new target for crtc %d\n",
						__func__, acrtc->base.base.id);
				break;
			}

			if (acrtc->target)
				remove_target(adev, acrtc);

			/*
			 * this loop saves set mode crtcs
			 * we needed to enable vblanks once all
			 * resources acquired in dc after dc_commit_targets
			 */
			new_crtcs[new_crtcs_count] = acrtc;
			new_crtcs_count++;

			acrtc->target = new_target;
			acrtc->enabled = true;
			acrtc->hw_mode = crtc->state->mode;
			crtc->hwmode = crtc->state->mode;

			break;
		}

		case DM_COMMIT_ACTION_NOTHING: {
			struct dm_connector_state *dm_state = NULL;

			if (!aconnector)
				break;

			dm_state = to_dm_connector_state(aconnector->base.state);

			/* Scaling update */
			update_stream_scaling_settings(
					&crtc->state->mode,
					dm_state,
					acrtc->target->streams[0]);

			break;
		}
		case DM_COMMIT_ACTION_DPMS_OFF:
		case DM_COMMIT_ACTION_RESET:
			DRM_INFO("Atomic commit: RESET. crtc id %d:[%p]\n", acrtc->crtc_id, acrtc);
			/* i.e. reset mode */
			if (acrtc->target)
				remove_target(adev, acrtc);
			break;
		} /* switch() */
	} /* for_each_crtc_in_state() */

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->target) {
			commit_targets[commit_targets_count] = acrtc->target;
			++commit_targets_count;
		}
	}

	/*
	 * Add streams after required streams from new and replaced targets
	 * are removed from freesync module
	 */
	if (adev->dm.freesync_module) {
		for (i = 0; i < new_crtcs_count; i++) {
			struct amdgpu_connector *aconnector = NULL;
			new_target = new_crtcs[i]->target;
			aconnector =
				amdgpu_dm_find_first_crct_matching_connector(
					state,
					&new_crtcs[i]->base,
					false);
			if (!aconnector) {
				DRM_INFO(
						"Atomic commit: Failed to find connector for acrtc id:%d "
						"skipping freesync init\n",
						new_crtcs[i]->crtc_id);
				continue;
			}

			for (j = 0; j < new_target->stream_count; j++)
				mod_freesync_add_stream(
						adev->dm.freesync_module,
						new_target->streams[j], &aconnector->caps);
		}
	}

	/* DC is optimized not to do anything if 'targets' didn't change. */
	dc_commit_targets(dm->dc, commit_targets, commit_targets_count);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->target != NULL)
			acrtc->otg_inst =
				dc_target_get_status(acrtc->target)->primary_otg_inst;
	}

	/* update planes when needed */
	for_each_plane_in_state(state, plane, old_plane_state, i) {
		struct drm_plane_state *plane_state = plane->state;
		struct drm_crtc *crtc = plane_state->crtc;
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		struct drm_framebuffer *fb = plane_state->fb;
		struct drm_connector *connector;
		struct dm_connector_state *dm_state = NULL;
		enum dm_commit_action action;

		if (!fb || !crtc || !crtc->state->active)
			continue;

		action = get_dm_commit_action(crtc->state);

		/* Surfaces are created under two scenarios:
		 * 1. This commit is not a page flip.
		 * 2. This commit is a page flip, and targets are created.
		 */
		if (!page_flip_needed(
				plane_state,
				old_plane_state,
				crtc->state->event, true) ||
				action == DM_COMMIT_ACTION_DPMS_ON ||
				action == DM_COMMIT_ACTION_SET) {
			list_for_each_entry(connector,
				&dev->mode_config.connector_list, head)	{
				if (connector->state->crtc == crtc) {
					dm_state = to_dm_connector_state(
						connector->state);
					break;
				}
			}

			/*
			 * This situation happens in the following case:
			 * we are about to get set mode for connector who's only
			 * possible crtc (in encoder crtc mask) is used by
			 * another connector, that is why it will try to
			 * re-assing crtcs in order to make configuration
			 * supported. For our implementation we need to make all
			 * encoders support all crtcs, then this issue will
			 * never arise again. But to guard code from this issue
			 * check is left.
			 *
			 * Also it should be needed when used with actual
			 * drm_atomic_commit ioctl in future
			 */
			if (!dm_state)
				continue;

			/*
			 * if flip is pending (ie, still waiting for fence to return
			 * before address is submitted) here, we cannot commit_surface
			 * as commit_surface will pre-maturely write out the future
			 * address. wait until flip is submitted before proceeding.
			 */
			wait_while_pflip_status(adev, acrtc, pflip_pending_predicate);

			dm_dc_surface_commit(dm->dc, crtc);
		}
	}

	for (i = 0; i < new_crtcs_count; i++) {
		/*
		 * loop to enable interrupts on newly arrived crtc
		 */
		struct amdgpu_crtc *acrtc = new_crtcs[i];

		if (adev->dm.freesync_module) {
			for (j = 0; j < acrtc->target->stream_count; j++)
				mod_freesync_notify_mode_change(
						adev->dm.freesync_module,
						acrtc->target->streams,
						acrtc->target->stream_count);
		}

		manage_dm_interrupts(adev, acrtc, true);
		dm_crtc_cursor_reset(&acrtc->base);

	}

	/* Do actual flip */
	flip_crtcs_count = 0;
	for_each_plane_in_state(state, plane, old_plane_state, i) {
		struct drm_plane_state *plane_state = plane->state;
		struct drm_crtc *crtc = plane_state->crtc;
		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);
		struct drm_framebuffer *fb = plane_state->fb;

		if (!fb || !crtc || !crtc->state->planes_changed ||
			!crtc->state->active)
			continue;

		if (page_flip_needed(
				plane_state,
				old_plane_state,
				crtc->state->event,
				false)) {
				amdgpu_crtc_submit_flip(
							crtc,
						    fb,
						    work[flip_crtcs_count],
						    new_abo[i]);
				 flip_crtcs_count++;
			/*clean up the flags for next usage*/
			acrtc->flip_flags = 0;
		}
	}

	/* In this state all old framebuffers would be unpinned */

	/* TODO: Revisit when we support true asynchronous commit.*/
	if (!async)
		drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_put(state);

	return ret;
}
/*
 * This functions handle all cases when set mode does not come upon hotplug.
 * This include when the same display is unplugged then plugged back into the
 * same port and when we are running without usermode desktop manager supprot
 */
void dm_restore_drm_connector_state(struct drm_device *dev, struct drm_connector *connector)
{
	struct drm_crtc *crtc;
	struct amdgpu_device *adev = dev->dev_private;
	struct dc *dc = adev->dm.dc;
	struct amdgpu_connector *aconnector = to_amdgpu_connector(connector);
	struct amdgpu_crtc *disconnected_acrtc;
	const struct dc_sink *sink;
	struct dc_target *commit_targets[6];
	struct dc_target *current_target;
	uint32_t commit_targets_count = 0;
	int i;

	if (!aconnector->dc_sink || !connector->state || !connector->encoder)
		return;

	disconnected_acrtc = to_amdgpu_crtc(connector->encoder->crtc);

	if (!disconnected_acrtc || !disconnected_acrtc->target)
		return;

	sink = disconnected_acrtc->target->streams[0]->sink;

	/*
	 * If the previous sink is not released and different from the current,
	 * we deduce we are in a state where we can not rely on usermode call
	 * to turn on the display, so we do it here
	 */
	if (sink != aconnector->dc_sink) {
		struct dm_connector_state *dm_state =
				to_dm_connector_state(aconnector->base.state);

		struct dc_target *new_target =
			create_target_for_sink(
				aconnector,
				&disconnected_acrtc->base.state->mode,
				dm_state);

		DRM_INFO("Headless hotplug, restoring connector state\n");
		/*
		 * we evade vblanks and pflips on crtc that
		 * should be changed
		 */
		manage_dm_interrupts(adev, disconnected_acrtc, false);
		/* this is the update mode case */

		current_target = disconnected_acrtc->target;

		disconnected_acrtc->target = new_target;
		disconnected_acrtc->enabled = true;
		disconnected_acrtc->hw_mode = disconnected_acrtc->base.state->mode;

		commit_targets_count = 0;

		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

			if (acrtc->target) {
				commit_targets[commit_targets_count] = acrtc->target;
				++commit_targets_count;
			}
		}

		/* DC is optimized not to do anything if 'targets' didn't change. */
		if (!dc_commit_targets(dc, commit_targets,
				commit_targets_count)) {
			DRM_INFO("Failed to restore connector state!\n");
			dc_target_release(disconnected_acrtc->target);
			disconnected_acrtc->target = current_target;
			manage_dm_interrupts(adev, disconnected_acrtc, true);
			return;
		}

		if (adev->dm.freesync_module) {

			for (i = 0; i < current_target->stream_count; i++)
				mod_freesync_remove_stream(
						adev->dm.freesync_module,
						current_target->streams[i]);

			for (i = 0; i < new_target->stream_count; i++)
				mod_freesync_add_stream(
						adev->dm.freesync_module,
						new_target->streams[i],
						&aconnector->caps);
		}
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

			if (acrtc->target != NULL) {
				acrtc->otg_inst =
					dc_target_get_status(acrtc->target)->primary_otg_inst;
			}
		}

		dc_target_release(current_target);

		dm_dc_surface_commit(dc, &disconnected_acrtc->base);

		manage_dm_interrupts(adev, disconnected_acrtc, true);
		dm_crtc_cursor_reset(&disconnected_acrtc->base);

	}
}

static uint32_t add_val_sets_surface(
	struct dc_validation_set *val_sets,
	uint32_t set_count,
	const struct dc_target *target,
	const struct dc_surface *surface)
{
	uint32_t i = 0;

	while (i < set_count) {
		if (val_sets[i].target == target)
			break;
		++i;
	}

	val_sets[i].surfaces[val_sets[i].surface_count] = surface;
	val_sets[i].surface_count++;

	return val_sets[i].surface_count;
}

static uint32_t update_in_val_sets_target(
	struct dc_validation_set *val_sets,
	struct drm_crtc **crtcs,
	uint32_t set_count,
	const struct dc_target *old_target,
	const struct dc_target *new_target,
	struct drm_crtc *crtc)
{
	uint32_t i = 0;

	while (i < set_count) {
		if (val_sets[i].target == old_target)
			break;
		++i;
	}

	val_sets[i].target = new_target;
	crtcs[i] = crtc;

	if (i == set_count) {
		/* nothing found. add new one to the end */
		return set_count + 1;
	}

	return set_count;
}

static uint32_t remove_from_val_sets(
	struct dc_validation_set *val_sets,
	uint32_t set_count,
	const struct dc_target *target)
{
	int i;

	for (i = 0; i < set_count; i++)
		if (val_sets[i].target == target)
			break;

	if (i == set_count) {
		/* nothing found */
		return set_count;
	}

	set_count--;

	for (; i < set_count; i++) {
		val_sets[i] = val_sets[i + 1];
	}

	return set_count;
}

int amdgpu_dm_atomic_check(struct drm_device *dev,
			struct drm_atomic_state *state)
{
	struct drm_crtc *crtc;
	struct drm_crtc_state *crtc_state;
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i, j;
	int ret;
	int set_count;
	int new_target_count;
	struct dc_validation_set set[MAX_TARGETS] = {{ 0 }};
	struct dc_target *new_targets[MAX_TARGETS] = { 0 };
	struct drm_crtc *crtc_set[MAX_TARGETS] = { 0 };
	struct amdgpu_device *adev = dev->dev_private;
	struct dc *dc = adev->dm.dc;
	bool need_to_validate = false;

	ret = drm_atomic_helper_check(dev, state);

	if (ret) {
		DRM_ERROR("Atomic state validation failed with error :%d !\n",
				ret);
		return ret;
	}

	ret = -EINVAL;

	/* copy existing configuration */
	new_target_count = 0;
	set_count = 0;
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {

		struct amdgpu_crtc *acrtc = to_amdgpu_crtc(crtc);

		if (acrtc->target) {
			set[set_count].target = acrtc->target;
			crtc_set[set_count] = crtc;
			++set_count;
		}
	}

	/* update changed items */
	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct amdgpu_crtc *acrtc = NULL;
		struct amdgpu_connector *aconnector = NULL;
		enum dm_commit_action action;

		acrtc = to_amdgpu_crtc(crtc);

		aconnector = amdgpu_dm_find_first_crct_matching_connector(state, crtc, true);

		action = get_dm_commit_action(crtc_state);

		switch (action) {
		case DM_COMMIT_ACTION_DPMS_ON:
		case DM_COMMIT_ACTION_SET: {
			struct dc_target *new_target = NULL;
			struct drm_connector_state *conn_state = NULL;
			struct dm_connector_state *dm_state = NULL;

			if (aconnector) {
				conn_state = drm_atomic_get_connector_state(state, &aconnector->base);
				if (IS_ERR(conn_state))
					return ret;
				dm_state = to_dm_connector_state(conn_state);
			}

			new_target = create_target_for_sink(aconnector, &crtc_state->mode, dm_state);

			/*
			 * we can have no target on ACTION_SET if a display
			 * was disconnected during S3, in this case it not and
			 * error, the OS will be updated after detection, and
			 * do the right thing on next atomic commit
			 */
			if (!new_target) {
				DRM_DEBUG_KMS("%s: Failed to create new target for crtc %d\n",
						__func__, acrtc->base.base.id);
				break;
			}

			new_targets[new_target_count] = new_target;
			set_count = update_in_val_sets_target(
					set,
					crtc_set,
					set_count,
					acrtc->target,
					new_target,
					crtc);

			new_target_count++;
			need_to_validate = true;
			break;
		}

		case DM_COMMIT_ACTION_NOTHING: {
			const struct drm_connector *drm_connector = NULL;
			struct drm_connector_state *conn_state = NULL;
			struct dm_connector_state *dm_state = NULL;
			struct dm_connector_state *old_dm_state = NULL;
			struct dc_target *new_target;

			if (!aconnector)
				break;

			for_each_connector_in_state(
				state, drm_connector, conn_state, j) {
				if (&aconnector->base == drm_connector)
					break;
			}

			old_dm_state = to_dm_connector_state(drm_connector->state);
			dm_state = to_dm_connector_state(conn_state);

			/* Support underscan adjustment*/
			if (!is_scaling_state_different(dm_state, old_dm_state))
				break;

			new_target = create_target_for_sink(aconnector, &crtc_state->mode, dm_state);

			if (!new_target) {
				DRM_ERROR("%s: Failed to create new target for crtc %d\n",
						__func__, acrtc->base.base.id);
				break;
			}

			new_targets[new_target_count] = new_target;
			set_count = update_in_val_sets_target(
					set,
					crtc_set,
					set_count,
					acrtc->target,
					new_target,
					crtc);

			new_target_count++;
			need_to_validate = true;

			break;
		}
		case DM_COMMIT_ACTION_DPMS_OFF:
		case DM_COMMIT_ACTION_RESET:
			/* i.e. reset mode */
			if (acrtc->target) {
				set_count = remove_from_val_sets(
						set,
						set_count,
						acrtc->target);
			}
			break;
		}

		/*
		 * TODO revisit when removing commit action
		 * and looking at atomic flags directly
		 */

		/* commit needs planes right now (for gamma, eg.) */
		/* TODO rework commit to chack crtc for gamma change */
		ret = drm_atomic_add_affected_planes(state, crtc);
		if (ret)
			return ret;
	}

	for (i = 0; i < set_count; i++) {
		for_each_plane_in_state(state, plane, plane_state, j) {
			struct drm_plane_state *old_plane_state = plane->state;
			struct drm_crtc *crtc = plane_state->crtc;
			struct drm_framebuffer *fb = plane_state->fb;
			struct drm_connector *connector;
			struct dm_connector_state *dm_state = NULL;
			enum dm_commit_action action;
			struct drm_crtc_state *crtc_state;


			if (!fb || !crtc || crtc_set[i] != crtc ||
				!crtc->state->planes_changed || !crtc->state->active)
				continue;

			action = get_dm_commit_action(crtc->state);

			/* Surfaces are created under two scenarios:
			 * 1. This commit is not a page flip.
			 * 2. This commit is a page flip, and targets are created.
			 */
			crtc_state = drm_atomic_get_crtc_state(state, crtc);
			if (!page_flip_needed(plane_state, old_plane_state,
					crtc_state->event, true) ||
					action == DM_COMMIT_ACTION_DPMS_ON ||
					action == DM_COMMIT_ACTION_SET) {
				struct dc_surface *surface;

				list_for_each_entry(connector,
					&dev->mode_config.connector_list, head)	{
					if (connector->state->crtc == crtc) {
						dm_state = to_dm_connector_state(
							connector->state);
						break;
					}
				}

				/*
				 * This situation happens in the following case:
				 * we are about to get set mode for connector who's only
				 * possible crtc (in encoder crtc mask) is used by
				 * another connector, that is why it will try to
				 * re-assing crtcs in order to make configuration
				 * supported. For our implementation we need to make all
				 * encoders support all crtcs, then this issue will
				 * never arise again. But to guard code from this issue
				 * check is left.
				 *
				 * Also it should be needed when used with actual
				 * drm_atomic_commit ioctl in future
				 */
				if (!dm_state)
					continue;

				surface = dc_create_surface(dc);
				fill_plane_attributes(
					surface,
					plane_state,
					false);

				add_val_sets_surface(
							set,
							set_count,
							set[i].target,
							surface);

				need_to_validate = true;
			}
		}
	}

	if (need_to_validate == false || set_count == 0 ||
		dc_validate_resources(dc, set, set_count))
		ret = 0;

	for (i = 0; i < set_count; i++) {
		for (j = 0; j < set[i].surface_count; j++) {
			dc_surface_release(set[i].surfaces[j]);
		}
	}
	for (i = 0; i < new_target_count; i++)
		dc_target_release(new_targets[i]);

	if (ret != 0)
		DRM_ERROR("Atomic check failed.\n");

	return ret;
}

static bool is_dp_capable_without_timing_msa(
		struct dc *dc,
		struct amdgpu_connector *amdgpu_connector)
{
	uint8_t dpcd_data;
	bool capable = false;
	if (amdgpu_connector->dc_link &&
	    dc_read_dpcd(dc, amdgpu_connector->dc_link->link_index,
			 DP_DOWN_STREAM_PORT_COUNT,
			 &dpcd_data, sizeof(dpcd_data)) )
		capable = dpcd_data & DP_MSA_TIMING_PAR_IGNORED? true:false;

	return capable;
}
void amdgpu_dm_add_sink_to_freesync_module(
		struct drm_connector *connector,
		struct edid *edid)
{
	int i;
	uint64_t val_capable;
	bool edid_check_required;
	struct detailed_timing *timing;
	struct detailed_non_pixel *data;
	struct detailed_data_monitor_range *range;
	struct amdgpu_connector *amdgpu_connector =
			to_amdgpu_connector(connector);

	struct drm_device *dev = connector->dev;
	struct amdgpu_device *adev = dev->dev_private;
	edid_check_required = false;
	if (!amdgpu_connector->dc_sink) {
		DRM_ERROR("dc_sink NULL, could not add free_sync module.\n");
		return;
	}
	if (!adev->dm.freesync_module)
		return;
	/*
	 * if edid non zero restrict freesync only for dp and edp
	 */
	if (edid) {
		if (amdgpu_connector->dc_sink->sink_signal == SIGNAL_TYPE_DISPLAY_PORT
			|| amdgpu_connector->dc_sink->sink_signal == SIGNAL_TYPE_EDP) {
			edid_check_required = is_dp_capable_without_timing_msa(
						adev->dm.dc,
						amdgpu_connector);
		}
	}
	val_capable = 0;
	if (edid_check_required == true && (edid->version > 1 ||
	   (edid->version == 1 && edid->revision > 1))) {
		for (i = 0; i < 4; i++) {

			timing	= &edid->detailed_timings[i];
			data	= &timing->data.other_data;
			range	= &data->data.range;
			/*
			 * Check if monitor has continuous frequency mode
			 */
			if (data->type != EDID_DETAIL_MONITOR_RANGE)
				continue;
			/*
			 * Check for flag range limits only. If flag == 1 then
			 * no additional timing information provided.
			 * Default GTF, GTF Secondary curve and CVT are not
			 * supported
			 */
			if (range->flags != 1)
				continue;

			amdgpu_connector->min_vfreq = range->min_vfreq;
			amdgpu_connector->max_vfreq = range->max_vfreq;
			amdgpu_connector->pixel_clock_mhz =
				range->pixel_clock_mhz * 10;
			break;
		}

		if (amdgpu_connector->max_vfreq -
				amdgpu_connector->min_vfreq > 10) {
			amdgpu_connector->caps.supported = true;
			amdgpu_connector->caps.min_refresh_in_micro_hz =
					amdgpu_connector->min_vfreq * 1000000;
			amdgpu_connector->caps.max_refresh_in_micro_hz =
					amdgpu_connector->max_vfreq * 1000000;
				val_capable = 1;
		}
	}

	/*
	 * TODO figure out how to notify user-mode or DRM of freesync caps
	 * once we figure out how to deal with freesync in an upstreamable
	 * fashion
	 */

}

void amdgpu_dm_remove_sink_from_freesync_module(
		struct drm_connector *connector)
{
	/*
	 * TODO fill in once we figure out how to deal with freesync in
	 * an upstreamable fashion
	 */
}
