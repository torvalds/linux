// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_framebuffer.h>

#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_fb.h"

#define check_array_bounds(i915, a, i) drm_WARN_ON(&(i915)->drm, (i) >= ARRAY_SIZE(a))

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

unsigned int intel_tile_size(const struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) == 2 ? 2048 : 4096;
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

unsigned int intel_cursor_alignment(const struct drm_i915_private *i915)
{
	if (IS_I830(i915))
		return 16 * 1024;
	else if (IS_I85X(i915))
		return 256;
	else if (IS_I845G(i915) || IS_I865G(i915))
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

static void intel_fb_plane_dims(const struct intel_framebuffer *fb, int color_plane, int *w, int *h)
{
	int main_plane = is_ccs_plane(&fb->base, color_plane) ?
			 skl_ccs_to_main_plane(&fb->base, color_plane) : 0;
	int main_hsub, main_vsub;
	int hsub, vsub;

	intel_fb_plane_get_subsampling(&main_hsub, &main_vsub, &fb->base, main_plane);
	intel_fb_plane_get_subsampling(&hsub, &vsub, &fb->base, color_plane);
	*w = fb->base.width / main_hsub / hsub;
	*h = fb->base.height / main_vsub / vsub;
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
	struct drm_i915_private *i915 = to_i915(fb->dev);
	unsigned int cpp = fb->format->cpp[color_plane];

	drm_WARN_ON(&i915->drm, new_offset > old_offset);

	if (!is_surface_linear(fb, color_plane)) {
		unsigned int tile_size, tile_width, tile_height;
		unsigned int pitch_tiles;

		tile_size = intel_tile_size(i915);
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
					   state->view.color_plane[color_plane].stride,
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
static u32 intel_compute_aligned_offset(struct drm_i915_private *i915,
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

		tile_size = intel_tile_size(i915);
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
	struct drm_i915_private *i915 = to_i915(intel_plane->base.dev);
	const struct drm_framebuffer *fb = state->hw.fb;
	unsigned int rotation = state->hw.rotation;
	int pitch = state->view.color_plane[color_plane].stride;
	u32 alignment;

	if (intel_plane->id == PLANE_CURSOR)
		alignment = intel_cursor_alignment(i915);
	else
		alignment = intel_surf_alignment(fb, color_plane);

	return intel_compute_aligned_offset(i915, x, y, fb, color_plane,
					    pitch, rotation, alignment);
}

/* Convert the fb->offset[] into x/y offsets */
static int intel_fb_offset_to_xy(int *x, int *y,
				 const struct drm_framebuffer *fb,
				 int color_plane)
{
	struct drm_i915_private *i915 = to_i915(fb->dev);
	unsigned int height;
	u32 alignment;

	/*
	 * All DPT color planes must be 512*4k aligned (the amount mapped by a
	 * single DPT page). For ADL_P CCS FBs this only works by requiring
	 * the allocated offsets to be 2MB aligned.  Once supoort to remap
	 * such FBs is added we can remove this requirement, as then all the
	 * planes can be remapped to an aligned offset.
	 */
	if (IS_ALDERLAKE_P(i915) && is_ccs_modifier(fb->modifier))
		alignment = 512 * 4096;
	else if (DISPLAY_VER(i915) >= 12 &&
		 is_semiplanar_uv_plane(fb, color_plane))
		alignment = intel_tile_row_size(fb, color_plane);
	else if (fb->modifier != DRM_FORMAT_MOD_LINEAR)
		alignment = intel_tile_size(i915);
	else
		alignment = 0;

	if (alignment != 0 && fb->offsets[color_plane] % alignment) {
		drm_dbg_kms(&i915->drm,
			    "Misaligned offset 0x%08x for color plane %d\n",
			    fb->offsets[color_plane], color_plane);
		return -EINVAL;
	}

	height = drm_framebuffer_plane_height(fb->height, fb, color_plane);
	height = ALIGN(height, intel_tile_height(fb, color_plane));

	/* Catch potential overflows early */
	if (add_overflows_t(u32, mul_u32_u32(height, fb->pitches[color_plane]),
			    fb->offsets[color_plane])) {
		drm_dbg_kms(&i915->drm,
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

static int intel_fb_check_ccs_xy(const struct drm_framebuffer *fb, int ccs_plane, int x, int y)
{
	struct drm_i915_private *i915 = to_i915(fb->dev);
	const struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
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
	main_x = intel_fb->normal_view.color_plane[main_plane].x % tile_width;
	main_y = intel_fb->normal_view.color_plane[main_plane].y % tile_height;

	/*
	 * CCS doesn't have its own x/y offset register, so the intra CCS tile
	 * x/y offsets must match between CCS and the main surface.
	 */
	if (main_x != ccs_x || main_y != ccs_y) {
		drm_dbg_kms(&i915->drm,
			      "Bad CCS x/y (main %d,%d ccs %d,%d) full (main %d,%d ccs %d,%d)\n",
			      main_x, main_y,
			      ccs_x, ccs_y,
			      intel_fb->normal_view.color_plane[main_plane].x,
			      intel_fb->normal_view.color_plane[main_plane].y,
			      x, y);
		return -EINVAL;
	}

	return 0;
}

static bool intel_plane_can_remap(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	struct drm_i915_private *i915 = to_i915(plane->base.dev);
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
	if (DISPLAY_VER(i915) < 4)
		return false;

	/*
	 * The new CCS hash mode isn't compatible with remapping as
	 * the virtual address of the pages affects the compressed data.
	 */
	if (is_ccs_modifier(fb->modifier))
		return false;

	/* Linear needs a page aligned stride for remapping */
	if (fb->modifier == DRM_FORMAT_MOD_LINEAR) {
		unsigned int alignment = intel_tile_size(i915) - 1;

		for (i = 0; i < fb->format->num_planes; i++) {
			if (fb->pitches[i] & alignment)
				return false;
		}
	}

	return true;
}

bool intel_fb_needs_pot_stride_remap(const struct intel_framebuffer *fb)
{
	struct drm_i915_private *i915 = to_i915(fb->base.dev);

	return IS_ALDERLAKE_P(i915) && fb->base.modifier != DRM_FORMAT_MOD_LINEAR &&
	       !is_ccs_modifier(fb->base.modifier);
}

static int intel_fb_pitch(const struct intel_framebuffer *fb, int color_plane, unsigned int rotation)
{
	if (drm_rotation_90_or_270(rotation))
		return fb->rotated_view.color_plane[color_plane].stride;
	else if (intel_fb_needs_pot_stride_remap(fb))
		return fb->remapped_view.color_plane[color_plane].stride;
	else
		return fb->normal_view.color_plane[color_plane].stride;
}

static bool intel_plane_needs_remap(const struct intel_plane_state *plane_state)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	const struct intel_framebuffer *fb = to_intel_framebuffer(plane_state->hw.fb);
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
	max_stride = plane->max_stride(plane, fb->base.format->format,
				       fb->base.modifier, rotation);

	return stride > max_stride;
}

static int convert_plane_offset_to_xy(const struct intel_framebuffer *fb, int color_plane,
				      int plane_width, int *x, int *y)
{
	struct drm_i915_gem_object *obj = intel_fb_obj(&fb->base);
	int ret;

	ret = intel_fb_offset_to_xy(x, y, &fb->base, color_plane);
	if (ret) {
		drm_dbg_kms(fb->base.dev,
			    "bad fb plane %d offset: 0x%x\n",
			    color_plane, fb->base.offsets[color_plane]);
		return ret;
	}

	ret = intel_fb_check_ccs_xy(&fb->base, color_plane, *x, *y);
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
	if (color_plane == 0 && i915_gem_object_is_tiled(obj) &&
	    (*x + plane_width) * fb->base.format->cpp[color_plane] > fb->base.pitches[color_plane]) {
		drm_dbg_kms(fb->base.dev,
			    "bad fb plane %d offset: 0x%x\n",
			    color_plane, fb->base.offsets[color_plane]);
		return -EINVAL;
	}

	return 0;
}

static u32 calc_plane_aligned_offset(const struct intel_framebuffer *fb, int color_plane, int *x, int *y)
{
	struct drm_i915_private *i915 = to_i915(fb->base.dev);
	unsigned int tile_size = intel_tile_size(i915);
	u32 offset;

	offset = intel_compute_aligned_offset(i915, x, y, &fb->base, color_plane,
					      fb->base.pitches[color_plane],
					      DRM_MODE_ROTATE_0,
					      tile_size);

	return offset / tile_size;
}

struct fb_plane_view_dims {
	unsigned int width, height;
	unsigned int tile_width, tile_height;
};

static void init_plane_view_dims(const struct intel_framebuffer *fb, int color_plane,
				 unsigned int width, unsigned int height,
				 struct fb_plane_view_dims *dims)
{
	dims->width = width;
	dims->height = height;

	intel_tile_dims(&fb->base, color_plane, &dims->tile_width, &dims->tile_height);
}

static unsigned int
plane_view_src_stride_tiles(const struct intel_framebuffer *fb, int color_plane,
			    const struct fb_plane_view_dims *dims)
{
	return DIV_ROUND_UP(fb->base.pitches[color_plane],
			    dims->tile_width * fb->base.format->cpp[color_plane]);
}

static unsigned int
plane_view_dst_stride_tiles(const struct intel_framebuffer *fb, int color_plane,
			    unsigned int pitch_tiles)
{
	if (intel_fb_needs_pot_stride_remap(fb))
		/*
		 * ADL_P, the only platform needing a POT stride has a minimum
		 * of 8 stride tiles.
		 */
		return roundup_pow_of_two(max(pitch_tiles, 8u));
	else
		return pitch_tiles;
}

static unsigned int
plane_view_width_tiles(const struct intel_framebuffer *fb, int color_plane,
		       const struct fb_plane_view_dims *dims,
		       int x)
{
	return DIV_ROUND_UP(x + dims->width, dims->tile_width);
}

static unsigned int
plane_view_height_tiles(const struct intel_framebuffer *fb, int color_plane,
			const struct fb_plane_view_dims *dims,
			int y)
{
	return DIV_ROUND_UP(y + dims->height, dims->tile_height);
}

#define assign_chk_ovf(i915, var, val) ({ \
	drm_WARN_ON(&(i915)->drm, overflows_type(val, var)); \
	(var) = (val); \
})

static u32 calc_plane_remap_info(const struct intel_framebuffer *fb, int color_plane,
				 const struct fb_plane_view_dims *dims,
				 u32 obj_offset, u32 gtt_offset, int x, int y,
				 struct intel_fb_view *view)
{
	struct drm_i915_private *i915 = to_i915(fb->base.dev);
	struct intel_remapped_plane_info *remap_info = &view->gtt.remapped.plane[color_plane];
	struct i915_color_plane_view *color_plane_info = &view->color_plane[color_plane];
	unsigned int tile_width = dims->tile_width;
	unsigned int tile_height = dims->tile_height;
	unsigned int tile_size = intel_tile_size(i915);
	struct drm_rect r;
	u32 size;

	assign_chk_ovf(i915, remap_info->offset, obj_offset);
	assign_chk_ovf(i915, remap_info->src_stride, plane_view_src_stride_tiles(fb, color_plane, dims));
	assign_chk_ovf(i915, remap_info->width, plane_view_width_tiles(fb, color_plane, dims, x));
	assign_chk_ovf(i915, remap_info->height, plane_view_height_tiles(fb, color_plane, dims, y));

	if (view->gtt.type == I915_GGTT_VIEW_ROTATED) {
		check_array_bounds(i915, view->gtt.rotated.plane, color_plane);

		assign_chk_ovf(i915, remap_info->dst_stride,
			       plane_view_dst_stride_tiles(fb, color_plane, remap_info->height));

		/* rotate the x/y offsets to match the GTT view */
		drm_rect_init(&r, x, y, dims->width, dims->height);
		drm_rect_rotate(&r,
				remap_info->width * tile_width,
				remap_info->height * tile_height,
				DRM_MODE_ROTATE_270);

		color_plane_info->x = r.x1;
		color_plane_info->y = r.y1;

		color_plane_info->stride = remap_info->dst_stride * tile_height;

		size = remap_info->dst_stride * remap_info->width;

		/* rotate the tile dimensions to match the GTT view */
		swap(tile_width, tile_height);
	} else {
		drm_WARN_ON(&i915->drm, view->gtt.type != I915_GGTT_VIEW_REMAPPED);

		check_array_bounds(i915, view->gtt.remapped.plane, color_plane);

		assign_chk_ovf(i915, remap_info->dst_stride,
			       plane_view_dst_stride_tiles(fb, color_plane, remap_info->width));

		color_plane_info->x = x;
		color_plane_info->y = y;

		color_plane_info->stride = remap_info->dst_stride * tile_width *
					   fb->base.format->cpp[color_plane];

		size = remap_info->dst_stride * remap_info->height;
	}

	/*
	 * We only keep the x/y offsets, so push all of the gtt offset into
	 * the x/y offsets.  x,y will hold the first pixel of the framebuffer
	 * plane from the start of the remapped/rotated gtt mapping.
	 */
	intel_adjust_tile_offset(&color_plane_info->x, &color_plane_info->y,
				 tile_width, tile_height,
				 tile_size, remap_info->dst_stride,
				 gtt_offset * tile_size, 0);

	return size;
}

#undef assign_chk_ovf

/* Return number of tiles @color_plane needs. */
static unsigned int
calc_plane_normal_size(const struct intel_framebuffer *fb, int color_plane,
		       const struct fb_plane_view_dims *dims,
		       int x, int y)
{
	struct drm_i915_private *i915 = to_i915(fb->base.dev);
	unsigned int tiles;

	if (is_surface_linear(&fb->base, color_plane)) {
		unsigned int size;

		size = (y + dims->height) * fb->base.pitches[color_plane] +
		       x * fb->base.format->cpp[color_plane];
		tiles = DIV_ROUND_UP(size, intel_tile_size(i915));
	} else {
		tiles = plane_view_src_stride_tiles(fb, color_plane, dims) *
			plane_view_height_tiles(fb, color_plane, dims, y);
		/*
		 * If the plane isn't horizontally tile aligned,
		 * we need one more tile.
		 */
		if (x != 0)
			tiles++;
	}

	return tiles;
}

static void intel_fb_view_init(struct intel_fb_view *view, enum i915_ggtt_view_type view_type)
{
	memset(view, 0, sizeof(*view));
	view->gtt.type = view_type;
}

bool intel_fb_supports_90_270_rotation(const struct intel_framebuffer *fb)
{
	if (DISPLAY_VER(to_i915(fb->base.dev)) >= 13)
		return false;

	return fb->base.modifier == I915_FORMAT_MOD_Y_TILED ||
	       fb->base.modifier == I915_FORMAT_MOD_Yf_TILED;
}

int intel_fill_fb_info(struct drm_i915_private *i915, struct intel_framebuffer *fb)
{
	struct drm_i915_gem_object *obj = intel_fb_obj(&fb->base);
	u32 gtt_offset_rotated = 0;
	u32 gtt_offset_remapped = 0;
	unsigned int max_size = 0;
	int i, num_planes = fb->base.format->num_planes;
	unsigned int tile_size = intel_tile_size(i915);

	intel_fb_view_init(&fb->normal_view, I915_GGTT_VIEW_NORMAL);

	drm_WARN_ON(&i915->drm,
		    intel_fb_supports_90_270_rotation(fb) &&
		    intel_fb_needs_pot_stride_remap(fb));

	if (intel_fb_supports_90_270_rotation(fb))
		intel_fb_view_init(&fb->rotated_view, I915_GGTT_VIEW_ROTATED);
	if (intel_fb_needs_pot_stride_remap(fb))
		intel_fb_view_init(&fb->remapped_view, I915_GGTT_VIEW_REMAPPED);

	for (i = 0; i < num_planes; i++) {
		struct fb_plane_view_dims view_dims;
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
		if (is_gen12_ccs_cc_plane(&fb->base, i)) {
			if (IS_ALIGNED(fb->base.offsets[i], PAGE_SIZE))
				continue;
			else
				return -EINVAL;
		}

		cpp = fb->base.format->cpp[i];
		intel_fb_plane_dims(fb, i, &width, &height);

		ret = convert_plane_offset_to_xy(fb, i, width, &x, &y);
		if (ret)
			return ret;

		init_plane_view_dims(fb, i, width, height, &view_dims);

		/*
		 * First pixel of the framebuffer from
		 * the start of the normal gtt mapping.
		 */
		fb->normal_view.color_plane[i].x = x;
		fb->normal_view.color_plane[i].y = y;
		fb->normal_view.color_plane[i].stride = fb->base.pitches[i];

		offset = calc_plane_aligned_offset(fb, i, &x, &y);

		if (intel_fb_supports_90_270_rotation(fb))
			gtt_offset_rotated += calc_plane_remap_info(fb, i, &view_dims,
								    offset, gtt_offset_rotated, x, y,
								    &fb->rotated_view);

		if (intel_fb_needs_pot_stride_remap(fb))
			gtt_offset_remapped += calc_plane_remap_info(fb, i, &view_dims,
								     offset, gtt_offset_remapped, x, y,
								     &fb->remapped_view);

		size = calc_plane_normal_size(fb, i, &view_dims, x, y);
		/* how many tiles in total needed in the bo */
		max_size = max(max_size, offset + size);
	}

	if (mul_u32_u32(max_size, tile_size) > obj->base.size) {
		drm_dbg_kms(&i915->drm,
			    "fb too big for bo (need %llu bytes, have %zu bytes)\n",
			    mul_u32_u32(max_size, tile_size), obj->base.size);
		return -EINVAL;
	}

	return 0;
}

static void intel_plane_remap_gtt(struct intel_plane_state *plane_state)
{
	struct drm_i915_private *i915 =
		to_i915(plane_state->uapi.plane->dev);
	struct drm_framebuffer *fb = plane_state->hw.fb;
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);
	unsigned int rotation = plane_state->hw.rotation;
	int i, num_planes = fb->format->num_planes;
	unsigned int src_x, src_y;
	unsigned int src_w, src_h;
	u32 gtt_offset = 0;

	intel_fb_view_init(&plane_state->view,
			   drm_rotation_90_or_270(rotation) ? I915_GGTT_VIEW_ROTATED :
							      I915_GGTT_VIEW_REMAPPED);

	src_x = plane_state->uapi.src.x1 >> 16;
	src_y = plane_state->uapi.src.y1 >> 16;
	src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	src_h = drm_rect_height(&plane_state->uapi.src) >> 16;

	drm_WARN_ON(&i915->drm, is_ccs_modifier(fb->modifier));

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
		struct fb_plane_view_dims view_dims;
		unsigned int width, height;
		unsigned int x, y;
		u32 offset;

		x = src_x / hsub;
		y = src_y / vsub;
		width = src_w / hsub;
		height = src_h / vsub;

		init_plane_view_dims(intel_fb, i, width, height, &view_dims);

		/*
		 * First pixel of the src viewport from the
		 * start of the normal gtt mapping.
		 */
		x += intel_fb->normal_view.color_plane[i].x;
		y += intel_fb->normal_view.color_plane[i].y;

		offset = calc_plane_aligned_offset(intel_fb, i, &x, &y);

		gtt_offset += calc_plane_remap_info(intel_fb, i, &view_dims,
						    offset, gtt_offset, x, y,
						    &plane_state->view);
	}
}

void intel_fb_fill_view(const struct intel_framebuffer *fb, unsigned int rotation,
			struct intel_fb_view *view)
{
	if (drm_rotation_90_or_270(rotation))
		*view = fb->rotated_view;
	else if (intel_fb_needs_pot_stride_remap(fb))
		*view = fb->remapped_view;
	else
		*view = fb->normal_view;
}

static int intel_plane_check_stride(const struct intel_plane_state *plane_state)
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
	stride = plane_state->view.color_plane[0].stride;
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

	if (!fb)
		return 0;

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

	intel_fb_fill_view(fb, rotation, &plane_state->view);

	/* Rotate src coordinates to match rotated GTT view */
	if (drm_rotation_90_or_270(rotation))
		drm_rect_rotate(&plane_state->uapi.src,
				fb->base.width << 16, fb->base.height << 16,
				DRM_MODE_ROTATE_270);

	return intel_plane_check_stride(plane_state);
}
