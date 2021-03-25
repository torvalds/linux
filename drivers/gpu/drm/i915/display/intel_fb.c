// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_framebuffer.h>

#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_fb.h"

bool is_ccs_plane(const struct drm_framebuffer *fb, int plane)
{
	if (!is_ccs_modifier(fb->modifier))
		return false;

	return plane >= fb->format->num_planes / 2;
}

bool is_gen12_ccs_plane(const struct drm_framebuffer *fb, int plane)
{
	return is_gen12_ccs_modifier(fb->modifier) && is_ccs_plane(fb, plane);
}

bool is_gen12_ccs_cc_plane(const struct drm_framebuffer *fb, int plane)
{
	return fb->modifier == I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC &&
	       plane == 2;
}

bool is_aux_plane(const struct drm_framebuffer *fb, int plane)
{
	if (is_ccs_modifier(fb->modifier))
		return is_ccs_plane(fb, plane);

	return plane == 1;
}

bool is_semiplanar_uv_plane(const struct drm_framebuffer *fb, int color_plane)
{
	return intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier) &&
		color_plane == 1;
}

bool is_surface_linear(const struct drm_framebuffer *fb, int color_plane)
{
	return fb->modifier == DRM_FORMAT_MOD_LINEAR ||
	       is_gen12_ccs_plane(fb, color_plane);
}

int main_to_ccs_plane(const struct drm_framebuffer *fb, int main_plane)
{
	drm_WARN_ON(fb->dev, !is_ccs_modifier(fb->modifier) ||
		    (main_plane && main_plane >= fb->format->num_planes / 2));

	return fb->format->num_planes / 2 + main_plane;
}

int skl_ccs_to_main_plane(const struct drm_framebuffer *fb, int ccs_plane)
{
	drm_WARN_ON(fb->dev, !is_ccs_modifier(fb->modifier) ||
		    ccs_plane < fb->format->num_planes / 2);

	if (is_gen12_ccs_cc_plane(fb, ccs_plane))
		return 0;

	return ccs_plane - fb->format->num_planes / 2;
}

int skl_main_to_aux_plane(const struct drm_framebuffer *fb, int main_plane)
{
	struct drm_i915_private *i915 = to_i915(fb->dev);

	if (is_ccs_modifier(fb->modifier))
		return main_to_ccs_plane(fb, main_plane);
	else if (DISPLAY_VER(i915) < 11 &&
		 intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier))
		return 1;
	else
		return 0;
}

unsigned int intel_tile_size(const struct drm_i915_private *dev_priv)
{
	return IS_DISPLAY_VER(dev_priv, 2) ? 2048 : 4096;
}

unsigned int intel_tile_height(const struct drm_framebuffer *fb, int color_plane)
{
	if (is_gen12_ccs_plane(fb, color_plane))
		return 1;

	return intel_tile_size(to_i915(fb->dev)) /
		intel_tile_width_bytes(fb, color_plane);
}

/* Return the tile dimensions in pixel units */
static void intel_tile_dims(const struct drm_framebuffer *fb, int color_plane,
			    unsigned int *tile_width,
			    unsigned int *tile_height)
{
	unsigned int tile_width_bytes = intel_tile_width_bytes(fb, color_plane);
	unsigned int cpp = fb->format->cpp[color_plane];

	*tile_width = tile_width_bytes / cpp;
	*tile_height = intel_tile_height(fb, color_plane);
}

unsigned int intel_tile_row_size(const struct drm_framebuffer *fb, int color_plane)
{
	unsigned int tile_width, tile_height;

	intel_tile_dims(fb, color_plane, &tile_width, &tile_height);

	return fb->pitches[color_plane] * tile_height;
}

unsigned int intel_cursor_alignment(const struct drm_i915_private *dev_priv)
{
	if (IS_I830(dev_priv))
		return 16 * 1024;
	else if (IS_I85X(dev_priv))
		return 256;
	else if (IS_I845G(dev_priv) || IS_I865G(dev_priv))
		return 32;
	else
		return 4 * 1024;
}

void intel_fb_plane_get_subsampling(int *hsub, int *vsub,
				    const struct drm_framebuffer *fb,
				    int color_plane)
{
	int main_plane;

	if (color_plane == 0) {
		*hsub = 1;
		*vsub = 1;

		return;
	}

	/*
	 * TODO: Deduct the subsampling from the char block for all CCS
	 * formats and planes.
	 */
	if (!is_gen12_ccs_plane(fb, color_plane)) {
		*hsub = fb->format->hsub;
		*vsub = fb->format->vsub;

		return;
	}

	main_plane = skl_ccs_to_main_plane(fb, color_plane);
	*hsub = drm_format_info_block_width(fb->format, color_plane) /
		drm_format_info_block_width(fb->format, main_plane);

	/*
	 * The min stride check in the core framebuffer_check() function
	 * assumes that format->hsub applies to every plane except for the
	 * first plane. That's incorrect for the CCS AUX plane of the first
	 * plane, but for the above check to pass we must define the block
	 * width with that subsampling applied to it. Adjust the width here
	 * accordingly, so we can calculate the actual subsampling factor.
	 */
	if (main_plane == 0)
		*hsub *= fb->format->hsub;

	*vsub = 32;
}

static void intel_fb_plane_dims(int *w, int *h, struct drm_framebuffer *fb, int color_plane)
{
	int main_plane = is_ccs_plane(fb, color_plane) ?
			 skl_ccs_to_main_plane(fb, color_plane) : 0;
	int main_hsub, main_vsub;
	int hsub, vsub;

	intel_fb_plane_get_subsampling(&main_hsub, &main_vsub, fb, main_plane);
	intel_fb_plane_get_subsampling(&hsub, &vsub, fb, color_plane);
	*w = fb->width / main_hsub / hsub;
	*h = fb->height / main_vsub / vsub;
}

static u32 intel_adjust_tile_offset(int *x, int *y,
				    unsigned int tile_width,
				    unsigned int tile_height,
				    unsigned int tile_size,
				    unsigned int pitch_tiles,
				    u32 old_offset,
				    u32 new_offset)
{
	unsigned int pitch_pixels = pitch_tiles * tile_width;
	unsigned int tiles;

	WARN_ON(old_offset & (tile_size - 1));
	WARN_ON(new_offset & (tile_size - 1));
	WARN_ON(new_offset > old_offset);

	tiles = (old_offset - new_offset) / tile_size;

	*y += tiles / pitch_tiles * tile_height;
	*x += tiles % pitch_tiles * tile_width;

	/* minimize x in case it got needlessly big */
	*y += *x / pitch_pixels * tile_height;
	*x %= pitch_pixels;

	return new_offset;
}

static u32 intel_adjust_aligned_offset(int *x, int *y,
				       const struct drm_framebuffer *fb,
				       int color_plane,
				       unsigned int rotation,
				       unsigned int pitch,
				       u32 old_offset, u32 new_offset)
{
	struct drm_i915_private *dev_priv = to_i915(fb->dev);
	unsigned int cpp = fb->format->cpp[color_plane];

	drm_WARN_ON(&dev_priv->drm, new_offset > old_offset);

	if (!is_surface_linear(fb, color_plane)) {
		unsigned int tile_size, tile_width, tile_height;
		unsigned int pitch_tiles;

		tile_size = intel_tile_size(dev_priv);
		intel_tile_dims(fb, color_plane, &tile_width, &tile_height);

		if (drm_rotation_90_or_270(rotation)) {
			pitch_tiles = pitch / tile_height;
			swap(tile_width, tile_height);
		} else {
			pitch_tiles = pitch / (tile_width * cpp);
		}

		intel_adjust_tile_offset(x, y, tile_width, tile_height,
					 tile_size, pitch_tiles,
					 old_offset, new_offset);
	} else {
		old_offset += *y * pitch + *x * cpp;

		*y = (old_offset - new_offset) / pitch;
		*x = ((old_offset - new_offset) - *y * pitch) / cpp;
	}

	return new_offset;
}

/*
 * Adjust the tile offset by moving the difference into
 * the x/y offsets.
 */
u32 intel_plane_adjust_aligned_offset(int *x, int *y,
				      const struct intel_plane_state *state,
				      int color_plane,
				      u32 old_offset, u32 new_offset)
{
	return intel_adjust_aligned_offset(x, y, state->hw.fb, color_plane,
					   state->hw.rotation,
					   state->color_plane[color_plane].stride,
					   old_offset, new_offset);
}

/*
 * Computes the aligned offset to the base tile and adjusts
 * x, y. bytes per pixel is assumed to be a power-of-two.
 *
 * In the 90/270 rotated case, x and y are assumed
 * to be already rotated to match the rotated GTT view, and
 * pitch is the tile_height aligned framebuffer height.
 *
 * This function is used when computing the derived information
 * under intel_framebuffer, so using any of that information
 * here is not allowed. Anything under drm_framebuffer can be
 * used. This is why the user has to pass in the pitch since it
 * is specified in the rotated orientation.
 */
static u32 intel_compute_aligned_offset(struct drm_i915_private *dev_priv,
					int *x, int *y,
					const struct drm_framebuffer *fb,
					int color_plane,
					unsigned int pitch,
					unsigned int rotation,
					u32 alignment)
{
	unsigned int cpp = fb->format->cpp[color_plane];
	u32 offset, offset_aligned;

	if (!is_surface_linear(fb, color_plane)) {
		unsigned int tile_size, tile_width, tile_height;
		unsigned int tile_rows, tiles, pitch_tiles;

		tile_size = intel_tile_size(dev_priv);
		intel_tile_dims(fb, color_plane, &tile_width, &tile_height);

		if (drm_rotation_90_or_270(rotation)) {
			pitch_tiles = pitch / tile_height;
			swap(tile_width, tile_height);
		} else {
			pitch_tiles = pitch / (tile_width * cpp);
		}

		tile_rows = *y / tile_height;
		*y %= tile_height;

		tiles = *x / tile_width;
		*x %= tile_width;

		offset = (tile_rows * pitch_tiles + tiles) * tile_size;

		offset_aligned = offset;
		if (alignment)
			offset_aligned = rounddown(offset_aligned, alignment);

		intel_adjust_tile_offset(x, y, tile_width, tile_height,
					 tile_size, pitch_tiles,
					 offset, offset_aligned);
	} else {
		offset = *y * pitch + *x * cpp;
		offset_aligned = offset;
		if (alignment) {
			offset_aligned = rounddown(offset_aligned, alignment);
			*y = (offset % alignment) / pitch;
			*x = ((offset % alignment) - *y * pitch) / cpp;
		} else {
			*y = *x = 0;
		}
	}

	return offset_aligned;
}

u32 intel_plane_compute_aligned_offset(int *x, int *y,
				       const struct intel_plane_state *state,
				       int color_plane)
{
	struct intel_plane *intel_plane = to_intel_plane(state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(intel_plane->base.dev);
	const struct drm_framebuffer *fb = state->hw.fb;
	unsigned int rotation = state->hw.rotation;
	int pitch = state->color_plane[color_plane].stride;
	u32 alignment;

	if (intel_plane->id == PLANE_CURSOR)
		alignment = intel_cursor_alignment(dev_priv);
	else
		alignment = intel_surf_alignment(fb, color_plane);

	return intel_compute_aligned_offset(dev_priv, x, y, fb, color_plane,
					    pitch, rotation, alignment);
}

/* Convert the fb->offset[] into x/y offsets */
static int intel_fb_offset_to_xy(int *x, int *y,
				 const struct drm_framebuffer *fb,
				 int color_plane)
{
	struct drm_i915_private *dev_priv = to_i915(fb->dev);
	unsigned int height;
	u32 alignment;

	if (DISPLAY_VER(dev_priv) >= 12 &&
	    is_semiplanar_uv_plane(fb, color_plane))
		alignment = intel_tile_row_size(fb, color_plane);
	else if (fb->modifier != DRM_FORMAT_MOD_LINEAR)
		alignment = intel_tile_size(dev_priv);
	else
		alignment = 0;

	if (alignment != 0 && fb->offsets[color_plane] % alignment) {
		drm_dbg_kms(&dev_priv->drm,
			    "Misaligned offset 0x%08x for color plane %d\n",
			    fb->offsets[color_plane], color_plane);
		return -EINVAL;
	}

	height = drm_framebuffer_plane_height(fb->height, fb, color_plane);
	height = ALIGN(height, intel_tile_height(fb, color_plane));

	/* Catch potential overflows early */
	if (add_overflows_t(u32, mul_u32_u32(height, fb->pitches[color_plane]),
			    fb->offsets[color_plane])) {
		drm_dbg_kms(&dev_priv->drm,
			    "Bad offset 0x%08x or pitch %d for color plane %d\n",
			    fb->offsets[color_plane], fb->pitches[color_plane],
			    color_plane);
		return -ERANGE;
	}

	*x = 0;
	*y = 0;

	intel_adjust_aligned_offset(x, y,
				    fb, color_plane, DRM_MODE_ROTATE_0,
				    fb->pitches[color_plane],
				    fb->offsets[color_plane], 0);

	return 0;
}

static int intel_fb_check_ccs_xy(struct drm_framebuffer *fb, int ccs_plane, int x, int y)
{
	struct drm_i915_private *i915 = to_i915(fb->dev);
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	int main_plane;
	int hsub, vsub;
	int tile_width, tile_height;
	int ccs_x, ccs_y;
	int main_x, main_y;

	if (!is_ccs_plane(fb, ccs_plane) || is_gen12_ccs_cc_plane(fb, ccs_plane))
		return 0;

	intel_tile_dims(fb, ccs_plane, &tile_width, &tile_height);
	intel_fb_plane_get_subsampling(&hsub, &vsub, fb, ccs_plane);

	tile_width *= hsub;
	tile_height *= vsub;

	ccs_x = (x * hsub) % tile_width;
	ccs_y = (y * vsub) % tile_height;

	main_plane = skl_ccs_to_main_plane(fb, ccs_plane);
	main_x = intel_fb->normal[main_plane].x % tile_width;
	main_y = intel_fb->normal[main_plane].y % tile_height;

	/*
	 * CCS doesn't have its own x/y offset register, so the intra CCS tile
	 * x/y offsets must match between CCS and the main surface.
	 */
	if (main_x != ccs_x || main_y != ccs_y) {
		drm_dbg_kms(&i915->drm,
			      "Bad CCS x/y (main %d,%d ccs %d,%d) full (main %d,%d ccs %d,%d)\n",
			      main_x, main_y,
			      ccs_x, ccs_y,
			      intel_fb->normal[main_plane].x,
			      intel_fb->normal[main_plane].y,
			      x, y);
		return -EINVAL;
	}

	return 0;
}

static bool intel_plane_can_remap(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *dev_priv = to_i915(plane->base.dev);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int i;

	/* We don't want to deal with remapping with cursors */
	if (plane->id == PLANE_CURSOR)
		return false;

	/*
	 * The display engine limits already match/exceed the
	 * render engine limits, so not much point in remapping.
	 * Would also need to deal with the fence POT alignment
	 * and gen2 2KiB GTT tile size.
	 */
	if (DISPLAY_VER(dev_priv) < 4)
		return false;

	/*
	 * The new CCS hash mode isn't compatible with remapping as
	 * the virtual address of the pages affects the compressed data.
	 */
	if (is_ccs_modifier(fb->modifier))
		return false;

	/* Linear needs a page aligned stride for remapping */
	if (fb->modifier == DRM_FORMAT_MOD_LINEAR) {
		unsigned int alignment = intel_tile_size(dev_priv) - 1;

		for (i = 0; i < fb->format->num_planes; i++) {
			if (fb->pitches[i] & alignment)
				return false;
		}
	}

	return true;
}

int intel_fb_pitch(const struct drm_framebuffer *fb, int color_plane, unsigned int rotation)
{
	if (drm_rotation_90_or_270(rotation))
		return to_intel_framebuffer(fb)->rotated[color_plane].pitch;
	else
		return fb->pitches[color_plane];
}

static bool intel_plane_needs_remap(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	u32 stride, max_stride;

	/*
	 * No remapping for invisible planes since we don't have
	 * an actual source viewport to remap.
	 */
	if (!plane_state->uapi.visible)
		return false;

	if (!intel_plane_can_remap(plane_state))
		return false;

	/*
	 * FIXME: aux plane limits on gen9+ are
	 * unclear in Bspec, for now no checking.
	 */
	stride = intel_fb_pitch(fb, 0, rotation);
	max_stride = plane->max_stride(plane, fb->format->format,
				       fb->modifier, rotation);

	return stride > max_stride;
}

/*
 * Setup the rotated view for an FB plane and return the size the GTT mapping
 * requires for this view.
 */
static u32 setup_fb_rotation(int plane, const struct intel_remapped_plane_info *plane_info,
			     u32 gtt_offset_rotated, int x, int y,
			     unsigned int width, unsigned int height,
			     unsigned int tile_size,
			     unsigned int tile_width, unsigned int tile_height,
			     struct drm_framebuffer *fb)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct intel_rotation_info *rot_info = &intel_fb->rot_info;
	unsigned int pitch_tiles;
	struct drm_rect r;

	/* Y or Yf modifiers required for 90/270 rotation */
	if (fb->modifier != I915_FORMAT_MOD_Y_TILED &&
	    fb->modifier != I915_FORMAT_MOD_Yf_TILED)
		return 0;

	if (drm_WARN_ON(fb->dev, plane >= ARRAY_SIZE(rot_info->plane)))
		return 0;

	rot_info->plane[plane] = *plane_info;

	intel_fb->rotated[plane].pitch = plane_info->height * tile_height;

	/* rotate the x/y offsets to match the GTT view */
	drm_rect_init(&r, x, y, width, height);
	drm_rect_rotate(&r,
			plane_info->width * tile_width,
			plane_info->height * tile_height,
			DRM_MODE_ROTATE_270);
	x = r.x1;
	y = r.y1;

	/* rotate the tile dimensions to match the GTT view */
	pitch_tiles = intel_fb->rotated[plane].pitch / tile_height;
	swap(tile_width, tile_height);

	/*
	 * We only keep the x/y offsets, so push all of the
	 * gtt offset into the x/y offsets.
	 */
	intel_adjust_tile_offset(&x, &y,
				 tile_width, tile_height,
				 tile_size, pitch_tiles,
				 gtt_offset_rotated * tile_size, 0);

	/*
	 * First pixel of the framebuffer from
	 * the start of the rotated gtt mapping.
	 */
	intel_fb->rotated[plane].x = x;
	intel_fb->rotated[plane].y = y;

	return plane_info->width * plane_info->height;
}

int intel_fill_fb_info(struct drm_i915_private *dev_priv, struct drm_framebuffer *fb)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	u32 gtt_offset_rotated = 0;
	unsigned int max_size = 0;
	int i, num_planes = fb->format->num_planes;
	unsigned int tile_size = intel_tile_size(dev_priv);

	for (i = 0; i < num_planes; i++) {
		unsigned int width, height;
		unsigned int cpp, size;
		u32 offset;
		int x, y;
		int ret;

		/*
		 * Plane 2 of Render Compression with Clear Color fb modifier
		 * is consumed by the driver and not passed to DE. Skip the
		 * arithmetic related to alignment and offset calculation.
		 */
		if (is_gen12_ccs_cc_plane(fb, i)) {
			if (IS_ALIGNED(fb->offsets[i], PAGE_SIZE))
				continue;
			else
				return -EINVAL;
		}

		cpp = fb->format->cpp[i];
		intel_fb_plane_dims(&width, &height, fb, i);

		ret = intel_fb_offset_to_xy(&x, &y, fb, i);
		if (ret) {
			drm_dbg_kms(&dev_priv->drm,
				    "bad fb plane %d offset: 0x%x\n",
				    i, fb->offsets[i]);
			return ret;
		}

		ret = intel_fb_check_ccs_xy(fb, i, x, y);
		if (ret)
			return ret;

		/*
		 * The fence (if used) is aligned to the start of the object
		 * so having the framebuffer wrap around across the edge of the
		 * fenced region doesn't really work. We have no API to configure
		 * the fence start offset within the object (nor could we probably
		 * on gen2/3). So it's just easier if we just require that the
		 * fb layout agrees with the fence layout. We already check that the
		 * fb stride matches the fence stride elsewhere.
		 */
		if (i == 0 && i915_gem_object_is_tiled(obj) &&
		    (x + width) * cpp > fb->pitches[i]) {
			drm_dbg_kms(&dev_priv->drm,
				    "bad fb plane %d offset: 0x%x\n",
				     i, fb->offsets[i]);
			return -EINVAL;
		}

		/*
		 * First pixel of the framebuffer from
		 * the start of the normal gtt mapping.
		 */
		intel_fb->normal[i].x = x;
		intel_fb->normal[i].y = y;

		offset = intel_compute_aligned_offset(dev_priv, &x, &y, fb, i,
						      fb->pitches[i],
						      DRM_MODE_ROTATE_0,
						      tile_size);
		offset /= tile_size;

		if (!is_surface_linear(fb, i)) {
			struct intel_remapped_plane_info plane_info;
			unsigned int tile_width, tile_height;

			intel_tile_dims(fb, i, &tile_width, &tile_height);

			plane_info.offset = offset;
			plane_info.stride = DIV_ROUND_UP(fb->pitches[i],
							 tile_width * cpp);
			plane_info.width = DIV_ROUND_UP(x + width, tile_width);
			plane_info.height = DIV_ROUND_UP(y + height,
							 tile_height);

			/* how many tiles does this plane need */
			size = plane_info.stride * plane_info.height;
			/*
			 * If the plane isn't horizontally tile aligned,
			 * we need one more tile.
			 */
			if (x != 0)
				size++;

			gtt_offset_rotated +=
				setup_fb_rotation(i, &plane_info,
						  gtt_offset_rotated,
						  x, y, width, height,
						  tile_size,
						  tile_width, tile_height,
						  fb);
		} else {
			size = DIV_ROUND_UP((y + height) * fb->pitches[i] +
					    x * cpp, tile_size);
		}

		/* how many tiles in total needed in the bo */
		max_size = max(max_size, offset + size);
	}

	if (mul_u32_u32(max_size, tile_size) > obj->base.size) {
		drm_dbg_kms(&dev_priv->drm,
			    "fb too big for bo (need %llu bytes, have %zu bytes)\n",
			    mul_u32_u32(max_size, tile_size), obj->base.size);
		return -EINVAL;
	}

	return 0;
}

static void intel_plane_remap_gtt(struct intel_plane_state *plane_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(plane_state->uapi.plane->dev);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	struct intel_rotation_info *info = &plane_state->view.rotated;
	unsigned int rotation = plane_state->hw.rotation;
	int i, num_planes = fb->format->num_planes;
	unsigned int tile_size = intel_tile_size(dev_priv);
	unsigned int src_x, src_y;
	unsigned int src_w, src_h;
	u32 gtt_offset = 0;

	memset(&plane_state->view, 0, sizeof(plane_state->view));
	plane_state->view.type = drm_rotation_90_or_270(rotation) ?
		I915_GGTT_VIEW_ROTATED : I915_GGTT_VIEW_REMAPPED;

	src_x = plane_state->uapi.src.x1 >> 16;
	src_y = plane_state->uapi.src.y1 >> 16;
	src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	src_h = drm_rect_height(&plane_state->uapi.src) >> 16;

	drm_WARN_ON(&dev_priv->drm, is_ccs_modifier(fb->modifier));

	/* Make src coordinates relative to the viewport */
	drm_rect_translate(&plane_state->uapi.src,
			   -(src_x << 16), -(src_y << 16));

	/* Rotate src coordinates to match rotated GTT view */
	if (drm_rotation_90_or_270(rotation))
		drm_rect_rotate(&plane_state->uapi.src,
				src_w << 16, src_h << 16,
				DRM_MODE_ROTATE_270);

	for (i = 0; i < num_planes; i++) {
		unsigned int hsub = i ? fb->format->hsub : 1;
		unsigned int vsub = i ? fb->format->vsub : 1;
		unsigned int cpp = fb->format->cpp[i];
		unsigned int tile_width, tile_height;
		unsigned int width, height;
		unsigned int pitch_tiles;
		unsigned int x, y;
		u32 offset;

		intel_tile_dims(fb, i, &tile_width, &tile_height);

		x = src_x / hsub;
		y = src_y / vsub;
		width = src_w / hsub;
		height = src_h / vsub;

		/*
		 * First pixel of the src viewport from the
		 * start of the normal gtt mapping.
		 */
		x += intel_fb->normal[i].x;
		y += intel_fb->normal[i].y;

		offset = intel_compute_aligned_offset(dev_priv, &x, &y,
						      fb, i, fb->pitches[i],
						      DRM_MODE_ROTATE_0, tile_size);
		offset /= tile_size;

		drm_WARN_ON(&dev_priv->drm, i >= ARRAY_SIZE(info->plane));
		info->plane[i].offset = offset;
		info->plane[i].stride = DIV_ROUND_UP(fb->pitches[i],
						     tile_width * cpp);
		info->plane[i].width = DIV_ROUND_UP(x + width, tile_width);
		info->plane[i].height = DIV_ROUND_UP(y + height, tile_height);

		if (drm_rotation_90_or_270(rotation)) {
			struct drm_rect r;

			/* rotate the x/y offsets to match the GTT view */
			drm_rect_init(&r, x, y, width, height);
			drm_rect_rotate(&r,
					info->plane[i].width * tile_width,
					info->plane[i].height * tile_height,
					DRM_MODE_ROTATE_270);
			x = r.x1;
			y = r.y1;

			pitch_tiles = info->plane[i].height;
			plane_state->color_plane[i].stride = pitch_tiles * tile_height;

			/* rotate the tile dimensions to match the GTT view */
			swap(tile_width, tile_height);
		} else {
			pitch_tiles = info->plane[i].width;
			plane_state->color_plane[i].stride = pitch_tiles * tile_width * cpp;
		}

		/*
		 * We only keep the x/y offsets, so push all of the
		 * gtt offset into the x/y offsets.
		 */
		intel_adjust_tile_offset(&x, &y,
					 tile_width, tile_height,
					 tile_size, pitch_tiles,
					 gtt_offset * tile_size, 0);

		gtt_offset += info->plane[i].width * info->plane[i].height;

		plane_state->color_plane[i].offset = 0;
		plane_state->color_plane[i].x = x;
		plane_state->color_plane[i].y = y;
	}
}

void intel_fill_fb_ggtt_view(struct i915_ggtt_view *view,
			     const struct drm_framebuffer *fb,
			     unsigned int rotation)
{
	memset(view, 0, sizeof(*view));

	view->type = I915_GGTT_VIEW_NORMAL;
	if (drm_rotation_90_or_270(rotation)) {
		view->type = I915_GGTT_VIEW_ROTATED;
		view->rotated = to_intel_framebuffer(fb)->rot_info;
	}
}

int intel_plane_check_stride(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	unsigned int rotation = plane_state->hw.rotation;
	u32 stride, max_stride;

	/*
	 * We ignore stride for all invisible planes that
	 * can be remapped. Otherwise we could end up
	 * with a false positive when the remapping didn't
	 * kick in due the plane being invisible.
	 */
	if (intel_plane_can_remap(plane_state) &&
	    !plane_state->uapi.visible)
		return 0;

	/* FIXME other color planes? */
	stride = plane_state->color_plane[0].stride;
	max_stride = plane->max_stride(plane, fb->format->format,
				       fb->modifier, rotation);

	if (stride > max_stride) {
		DRM_DEBUG_KMS("[FB:%d] stride (%d) exceeds [PLANE:%d:%s] max stride (%d)\n",
			      fb->base.id, stride,
			      plane->base.base.id, plane->base.name, max_stride);
		return -EINVAL;
	}

	return 0;
}

int intel_plane_compute_gtt(struct intel_plane_state *plane_state)
{
	const struct intel_framebuffer *fb =
		to_intel_framebuffer(plane_state->hw.fb);
	unsigned int rotation = plane_state->hw.rotation;
	int i, num_planes;

	if (!fb)
		return 0;

	num_planes = fb->base.format->num_planes;

	if (intel_plane_needs_remap(plane_state)) {
		intel_plane_remap_gtt(plane_state);

		/*
		 * Sometimes even remapping can't overcome
		 * the stride limitations :( Can happen with
		 * big plane sizes and suitably misaligned
		 * offsets.
		 */
		return intel_plane_check_stride(plane_state);
	}

	intel_fill_fb_ggtt_view(&plane_state->view, &fb->base, rotation);

	for (i = 0; i < num_planes; i++) {
		plane_state->color_plane[i].stride = intel_fb_pitch(&fb->base, i, rotation);
		plane_state->color_plane[i].offset = 0;

		if (drm_rotation_90_or_270(rotation)) {
			plane_state->color_plane[i].x = fb->rotated[i].x;
			plane_state->color_plane[i].y = fb->rotated[i].y;
		} else {
			plane_state->color_plane[i].x = fb->normal[i].x;
			plane_state->color_plane[i].y = fb->normal[i].y;
		}
	}

	/* Rotate src coordinates to match rotated GTT view */
	if (drm_rotation_90_or_270(rotation))
		drm_rect_rotate(&plane_state->uapi.src,
				fb->base.width << 16, fb->base.height << 16,
				DRM_MODE_ROTATE_270);

	return intel_plane_check_stride(plane_state);
}
