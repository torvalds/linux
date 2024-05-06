// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_blend.h>
#include <drm/drm_modeset_helper.h>

#include <linux/dma-fence.h>
#include <linux/dma-resv.h>

#include "i915_drv.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_dpt.h"
#include "intel_fb.h"
#include "intel_fb_bo.h"
#include "intel_frontbuffer.h"

#define check_array_bounds(i915, a, i) drm_WARN_ON(&(i915)->drm, (i) >= ARRAY_SIZE(a))

/*
 * From the Sky Lake PRM:
 * "The Color Control Surface (CCS) contains the compression status of
 *  the cache-line pairs. The compression state of the cache-line pair
 *  is specified by 2 bits in the CCS. Each CCS cache-line represents
 *  an area on the main surface of 16 x16 sets of 128 byte Y-tiled
 *  cache-line-pairs. CCS is always Y tiled."
 *
 * Since cache line pairs refers to horizontally adjacent cache lines,
 * each cache line in the CCS corresponds to an area of 32x16 cache
 * lines on the main surface. Since each pixel is 4 bytes, this gives
 * us a ratio of one byte in the CCS for each 8x16 pixels in the
 * main surface.
 */
static const struct drm_format_info skl_ccs_formats[] = {
	{ .format = DRM_FORMAT_XRGB8888, .depth = 24, .num_planes = 2,
	  .cpp = { 4, 1, }, .hsub = 8, .vsub = 16, },
	{ .format = DRM_FORMAT_XBGR8888, .depth = 24, .num_planes = 2,
	  .cpp = { 4, 1, }, .hsub = 8, .vsub = 16, },
	{ .format = DRM_FORMAT_ARGB8888, .depth = 32, .num_planes = 2,
	  .cpp = { 4, 1, }, .hsub = 8, .vsub = 16, .has_alpha = true, },
	{ .format = DRM_FORMAT_ABGR8888, .depth = 32, .num_planes = 2,
	  .cpp = { 4, 1, }, .hsub = 8, .vsub = 16, .has_alpha = true, },
};

/*
 * Gen-12 compression uses 4 bits of CCS data for each cache line pair in the
 * main surface. And each 64B CCS cache line represents an area of 4x1 Y-tiles
 * in the main surface. With 4 byte pixels and each Y-tile having dimensions of
 * 32x32 pixels, the ratio turns out to 1B in the CCS for every 2x32 pixels in
 * the main surface.
 */
static const struct drm_format_info gen12_ccs_formats[] = {
	{ .format = DRM_FORMAT_XRGB8888, .depth = 24, .num_planes = 2,
	  .char_per_block = { 4, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, },
	{ .format = DRM_FORMAT_XBGR8888, .depth = 24, .num_planes = 2,
	  .char_per_block = { 4, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, },
	{ .format = DRM_FORMAT_ARGB8888, .depth = 32, .num_planes = 2,
	  .char_per_block = { 4, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, .has_alpha = true },
	{ .format = DRM_FORMAT_ABGR8888, .depth = 32, .num_planes = 2,
	  .char_per_block = { 4, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, .has_alpha = true },
	{ .format = DRM_FORMAT_YUYV, .num_planes = 2,
	  .char_per_block = { 2, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 2, .vsub = 1, .is_yuv = true },
	{ .format = DRM_FORMAT_YVYU, .num_planes = 2,
	  .char_per_block = { 2, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 2, .vsub = 1, .is_yuv = true },
	{ .format = DRM_FORMAT_UYVY, .num_planes = 2,
	  .char_per_block = { 2, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 2, .vsub = 1, .is_yuv = true },
	{ .format = DRM_FORMAT_VYUY, .num_planes = 2,
	  .char_per_block = { 2, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 2, .vsub = 1, .is_yuv = true },
	{ .format = DRM_FORMAT_XYUV8888, .num_planes = 2,
	  .char_per_block = { 4, 1 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, .is_yuv = true },
	{ .format = DRM_FORMAT_NV12, .num_planes = 4,
	  .char_per_block = { 1, 2, 1, 1 }, .block_w = { 1, 1, 4, 4 }, .block_h = { 1, 1, 1, 1 },
	  .hsub = 2, .vsub = 2, .is_yuv = true },
	{ .format = DRM_FORMAT_P010, .num_planes = 4,
	  .char_per_block = { 2, 4, 1, 1 }, .block_w = { 1, 1, 2, 2 }, .block_h = { 1, 1, 1, 1 },
	  .hsub = 2, .vsub = 2, .is_yuv = true },
	{ .format = DRM_FORMAT_P012, .num_planes = 4,
	  .char_per_block = { 2, 4, 1, 1 }, .block_w = { 1, 1, 2, 2 }, .block_h = { 1, 1, 1, 1 },
	  .hsub = 2, .vsub = 2, .is_yuv = true },
	{ .format = DRM_FORMAT_P016, .num_planes = 4,
	  .char_per_block = { 2, 4, 1, 1 }, .block_w = { 1, 1, 2, 2 }, .block_h = { 1, 1, 1, 1 },
	  .hsub = 2, .vsub = 2, .is_yuv = true },
};

/*
 * Same as gen12_ccs_formats[] above, but with additional surface used
 * to pass Clear Color information in plane 2 with 64 bits of data.
 */
static const struct drm_format_info gen12_ccs_cc_formats[] = {
	{ .format = DRM_FORMAT_XRGB8888, .depth = 24, .num_planes = 3,
	  .char_per_block = { 4, 1, 0 }, .block_w = { 1, 2, 2 }, .block_h = { 1, 1, 1 },
	  .hsub = 1, .vsub = 1, },
	{ .format = DRM_FORMAT_XBGR8888, .depth = 24, .num_planes = 3,
	  .char_per_block = { 4, 1, 0 }, .block_w = { 1, 2, 2 }, .block_h = { 1, 1, 1 },
	  .hsub = 1, .vsub = 1, },
	{ .format = DRM_FORMAT_ARGB8888, .depth = 32, .num_planes = 3,
	  .char_per_block = { 4, 1, 0 }, .block_w = { 1, 2, 2 }, .block_h = { 1, 1, 1 },
	  .hsub = 1, .vsub = 1, .has_alpha = true },
	{ .format = DRM_FORMAT_ABGR8888, .depth = 32, .num_planes = 3,
	  .char_per_block = { 4, 1, 0 }, .block_w = { 1, 2, 2 }, .block_h = { 1, 1, 1 },
	  .hsub = 1, .vsub = 1, .has_alpha = true },
};

static const struct drm_format_info gen12_flat_ccs_cc_formats[] = {
	{ .format = DRM_FORMAT_XRGB8888, .depth = 24, .num_planes = 2,
	  .char_per_block = { 4, 0 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, },
	{ .format = DRM_FORMAT_XBGR8888, .depth = 24, .num_planes = 2,
	  .char_per_block = { 4, 0 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, },
	{ .format = DRM_FORMAT_ARGB8888, .depth = 32, .num_planes = 2,
	  .char_per_block = { 4, 0 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, .has_alpha = true },
	{ .format = DRM_FORMAT_ABGR8888, .depth = 32, .num_planes = 2,
	  .char_per_block = { 4, 0 }, .block_w = { 1, 2 }, .block_h = { 1, 1 },
	  .hsub = 1, .vsub = 1, .has_alpha = true },
};

struct intel_modifier_desc {
	u64 modifier;
	struct {
		u8 from;
		u8 until;
	} display_ver;
#define DISPLAY_VER_ALL		{ 0, -1 }

	const struct drm_format_info *formats;
	int format_count;
#define FORMAT_OVERRIDE(format_list) \
	.formats = format_list, \
	.format_count = ARRAY_SIZE(format_list)

	u8 plane_caps;

	struct {
		u8 cc_planes:3;
		u8 packed_aux_planes:4;
		u8 planar_aux_planes:4;
	} ccs;
};

#define INTEL_PLANE_CAP_CCS_MASK	(INTEL_PLANE_CAP_CCS_RC | \
					 INTEL_PLANE_CAP_CCS_RC_CC | \
					 INTEL_PLANE_CAP_CCS_MC)
#define INTEL_PLANE_CAP_TILING_MASK	(INTEL_PLANE_CAP_TILING_X | \
					 INTEL_PLANE_CAP_TILING_Y | \
					 INTEL_PLANE_CAP_TILING_Yf | \
					 INTEL_PLANE_CAP_TILING_4)
#define INTEL_PLANE_CAP_TILING_NONE	0

static const struct intel_modifier_desc intel_modifiers[] = {
	{
		.modifier = I915_FORMAT_MOD_4_TILED_MTL_MC_CCS,
		.display_ver = { 14, 14 },
		.plane_caps = INTEL_PLANE_CAP_TILING_4 | INTEL_PLANE_CAP_CCS_MC,

		.ccs.packed_aux_planes = BIT(1),
		.ccs.planar_aux_planes = BIT(2) | BIT(3),

		FORMAT_OVERRIDE(gen12_ccs_formats),
	}, {
		.modifier = I915_FORMAT_MOD_4_TILED_MTL_RC_CCS,
		.display_ver = { 14, 14 },
		.plane_caps = INTEL_PLANE_CAP_TILING_4 | INTEL_PLANE_CAP_CCS_RC,

		.ccs.packed_aux_planes = BIT(1),

		FORMAT_OVERRIDE(gen12_ccs_formats),
	}, {
		.modifier = I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC,
		.display_ver = { 14, 14 },
		.plane_caps = INTEL_PLANE_CAP_TILING_4 | INTEL_PLANE_CAP_CCS_RC_CC,

		.ccs.cc_planes = BIT(2),
		.ccs.packed_aux_planes = BIT(1),

		FORMAT_OVERRIDE(gen12_ccs_cc_formats),
	}, {
		.modifier = I915_FORMAT_MOD_4_TILED_DG2_MC_CCS,
		.display_ver = { 13, 13 },
		.plane_caps = INTEL_PLANE_CAP_TILING_4 | INTEL_PLANE_CAP_CCS_MC,
	}, {
		.modifier = I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC,
		.display_ver = { 13, 13 },
		.plane_caps = INTEL_PLANE_CAP_TILING_4 | INTEL_PLANE_CAP_CCS_RC_CC,

		.ccs.cc_planes = BIT(1),

		FORMAT_OVERRIDE(gen12_flat_ccs_cc_formats),
	}, {
		.modifier = I915_FORMAT_MOD_4_TILED_DG2_RC_CCS,
		.display_ver = { 13, 13 },
		.plane_caps = INTEL_PLANE_CAP_TILING_4 | INTEL_PLANE_CAP_CCS_RC,
	}, {
		.modifier = I915_FORMAT_MOD_4_TILED,
		.display_ver = { 13, -1 },
		.plane_caps = INTEL_PLANE_CAP_TILING_4,
	}, {
		.modifier = I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS,
		.display_ver = { 12, 13 },
		.plane_caps = INTEL_PLANE_CAP_TILING_Y | INTEL_PLANE_CAP_CCS_MC,

		.ccs.packed_aux_planes = BIT(1),
		.ccs.planar_aux_planes = BIT(2) | BIT(3),

		FORMAT_OVERRIDE(gen12_ccs_formats),
	}, {
		.modifier = I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS,
		.display_ver = { 12, 13 },
		.plane_caps = INTEL_PLANE_CAP_TILING_Y | INTEL_PLANE_CAP_CCS_RC,

		.ccs.packed_aux_planes = BIT(1),

		FORMAT_OVERRIDE(gen12_ccs_formats),
	}, {
		.modifier = I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC,
		.display_ver = { 12, 13 },
		.plane_caps = INTEL_PLANE_CAP_TILING_Y | INTEL_PLANE_CAP_CCS_RC_CC,

		.ccs.cc_planes = BIT(2),
		.ccs.packed_aux_planes = BIT(1),

		FORMAT_OVERRIDE(gen12_ccs_cc_formats),
	}, {
		.modifier = I915_FORMAT_MOD_Yf_TILED_CCS,
		.display_ver = { 9, 11 },
		.plane_caps = INTEL_PLANE_CAP_TILING_Yf | INTEL_PLANE_CAP_CCS_RC,

		.ccs.packed_aux_planes = BIT(1),

		FORMAT_OVERRIDE(skl_ccs_formats),
	}, {
		.modifier = I915_FORMAT_MOD_Y_TILED_CCS,
		.display_ver = { 9, 11 },
		.plane_caps = INTEL_PLANE_CAP_TILING_Y | INTEL_PLANE_CAP_CCS_RC,

		.ccs.packed_aux_planes = BIT(1),

		FORMAT_OVERRIDE(skl_ccs_formats),
	}, {
		.modifier = I915_FORMAT_MOD_Yf_TILED,
		.display_ver = { 9, 11 },
		.plane_caps = INTEL_PLANE_CAP_TILING_Yf,
	}, {
		.modifier = I915_FORMAT_MOD_Y_TILED,
		.display_ver = { 9, 13 },
		.plane_caps = INTEL_PLANE_CAP_TILING_Y,
	}, {
		.modifier = I915_FORMAT_MOD_X_TILED,
		.display_ver = DISPLAY_VER_ALL,
		.plane_caps = INTEL_PLANE_CAP_TILING_X,
	}, {
		.modifier = DRM_FORMAT_MOD_LINEAR,
		.display_ver = DISPLAY_VER_ALL,
	},
};

static const struct intel_modifier_desc *lookup_modifier_or_null(u64 modifier)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_modifiers); i++)
		if (intel_modifiers[i].modifier == modifier)
			return &intel_modifiers[i];

	return NULL;
}

static const struct intel_modifier_desc *lookup_modifier(u64 modifier)
{
	const struct intel_modifier_desc *md = lookup_modifier_or_null(modifier);

	if (WARN_ON(!md))
		return &intel_modifiers[0];

	return md;
}

static const struct drm_format_info *
lookup_format_info(const struct drm_format_info formats[],
		   int num_formats, u32 format)
{
	int i;

	for (i = 0; i < num_formats; i++) {
		if (formats[i].format == format)
			return &formats[i];
	}

	return NULL;
}

unsigned int intel_fb_modifier_to_tiling(u64 fb_modifier)
{
	const struct intel_modifier_desc *md;
	u8 tiling_caps;

	md = lookup_modifier_or_null(fb_modifier);
	if (!md)
		return I915_TILING_NONE;

	tiling_caps = lookup_modifier_or_null(fb_modifier)->plane_caps &
			 INTEL_PLANE_CAP_TILING_MASK;

	switch (tiling_caps) {
	case INTEL_PLANE_CAP_TILING_Y:
		return I915_TILING_Y;
	case INTEL_PLANE_CAP_TILING_X:
		return I915_TILING_X;
	case INTEL_PLANE_CAP_TILING_4:
	case INTEL_PLANE_CAP_TILING_Yf:
	case INTEL_PLANE_CAP_TILING_NONE:
		return I915_TILING_NONE;
	default:
		MISSING_CASE(tiling_caps);
		return I915_TILING_NONE;
	}
}

/**
 * intel_fb_get_format_info: Get a modifier specific format information
 * @cmd: FB add command structure
 *
 * Returns:
 * Returns the format information for @cmd->pixel_format specific to @cmd->modifier[0],
 * or %NULL if the modifier doesn't override the format.
 */
const struct drm_format_info *
intel_fb_get_format_info(const struct drm_mode_fb_cmd2 *cmd)
{
	const struct intel_modifier_desc *md = lookup_modifier_or_null(cmd->modifier[0]);

	if (!md || !md->formats)
		return NULL;

	return lookup_format_info(md->formats, md->format_count, cmd->pixel_format);
}

static bool plane_caps_contain_any(u8 caps, u8 mask)
{
	return caps & mask;
}

static bool plane_caps_contain_all(u8 caps, u8 mask)
{
	return (caps & mask) == mask;
}

/**
 * intel_fb_is_tiled_modifier: Check if a modifier is a tiled modifier type
 * @modifier: Modifier to check
 *
 * Returns:
 * Returns %true if @modifier is a tiled modifier.
 */
bool intel_fb_is_tiled_modifier(u64 modifier)
{
	return plane_caps_contain_any(lookup_modifier(modifier)->plane_caps,
				      INTEL_PLANE_CAP_TILING_MASK);
}

/**
 * intel_fb_is_ccs_modifier: Check if a modifier is a CCS modifier type
 * @modifier: Modifier to check
 *
 * Returns:
 * Returns %true if @modifier is a render, render with color clear or
 * media compression modifier.
 */
bool intel_fb_is_ccs_modifier(u64 modifier)
{
	return plane_caps_contain_any(lookup_modifier(modifier)->plane_caps,
				      INTEL_PLANE_CAP_CCS_MASK);
}

/**
 * intel_fb_is_rc_ccs_cc_modifier: Check if a modifier is an RC CCS CC modifier type
 * @modifier: Modifier to check
 *
 * Returns:
 * Returns %true if @modifier is a render with color clear modifier.
 */
bool intel_fb_is_rc_ccs_cc_modifier(u64 modifier)
{
	return plane_caps_contain_any(lookup_modifier(modifier)->plane_caps,
				      INTEL_PLANE_CAP_CCS_RC_CC);
}

/**
 * intel_fb_is_mc_ccs_modifier: Check if a modifier is an MC CCS modifier type
 * @modifier: Modifier to check
 *
 * Returns:
 * Returns %true if @modifier is a media compression modifier.
 */
bool intel_fb_is_mc_ccs_modifier(u64 modifier)
{
	return plane_caps_contain_any(lookup_modifier(modifier)->plane_caps,
				      INTEL_PLANE_CAP_CCS_MC);
}

static bool check_modifier_display_ver_range(const struct intel_modifier_desc *md,
					     u8 display_ver_from, u8 display_ver_until)
{
	return md->display_ver.from <= display_ver_until &&
		display_ver_from <= md->display_ver.until;
}

static bool plane_has_modifier(struct drm_i915_private *i915,
			       u8 plane_caps,
			       const struct intel_modifier_desc *md)
{
	if (!IS_DISPLAY_VER(i915, md->display_ver.from, md->display_ver.until))
		return false;

	if (!plane_caps_contain_all(plane_caps, md->plane_caps))
		return false;

	/*
	 * Separate AuxCCS and Flat CCS modifiers to be run only on platforms
	 * where supported.
	 */
	if (intel_fb_is_ccs_modifier(md->modifier) &&
	    HAS_FLAT_CCS(i915) != !md->ccs.packed_aux_planes)
		return false;

	return true;
}

/**
 * intel_fb_plane_get_modifiers: Get the modifiers for the given platform and plane capabilities
 * @i915: i915 device instance
 * @plane_caps: capabilities for the plane the modifiers are queried for
 *
 * Returns:
 * Returns the list of modifiers allowed by the @i915 platform and @plane_caps.
 * The caller must free the returned buffer.
 */
u64 *intel_fb_plane_get_modifiers(struct drm_i915_private *i915,
				  u8 plane_caps)
{
	u64 *list, *p;
	int count = 1;		/* +1 for invalid modifier terminator */
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_modifiers); i++) {
		if (plane_has_modifier(i915, plane_caps, &intel_modifiers[i]))
			count++;
	}

	list = kmalloc_array(count, sizeof(*list), GFP_KERNEL);
	if (drm_WARN_ON(&i915->drm, !list))
		return NULL;

	p = list;
	for (i = 0; i < ARRAY_SIZE(intel_modifiers); i++) {
		if (plane_has_modifier(i915, plane_caps, &intel_modifiers[i]))
			*p++ = intel_modifiers[i].modifier;
	}
	*p++ = DRM_FORMAT_MOD_INVALID;

	return list;
}

/**
 * intel_fb_plane_supports_modifier: Determine if a modifier is supported by the given plane
 * @plane: Plane to check the modifier support for
 * @modifier: The modifier to check the support for
 *
 * Returns:
 * %true if the @modifier is supported on @plane.
 */
bool intel_fb_plane_supports_modifier(struct intel_plane *plane, u64 modifier)
{
	int i;

	for (i = 0; i < plane->base.modifier_count; i++)
		if (plane->base.modifiers[i] == modifier)
			return true;

	return false;
}

static bool format_is_yuv_semiplanar(const struct intel_modifier_desc *md,
				     const struct drm_format_info *info)
{
	if (!info->is_yuv)
		return false;

	if (hweight8(md->ccs.planar_aux_planes) == 2)
		return info->num_planes == 4;
	else
		return info->num_planes == 2;
}

/**
 * intel_format_info_is_yuv_semiplanar: Check if the given format is YUV semiplanar
 * @info: format to check
 * @modifier: modifier used with the format
 *
 * Returns:
 * %true if @info / @modifier is YUV semiplanar.
 */
bool intel_format_info_is_yuv_semiplanar(const struct drm_format_info *info,
					 u64 modifier)
{
	return format_is_yuv_semiplanar(lookup_modifier(modifier), info);
}

static u8 ccs_aux_plane_mask(const struct intel_modifier_desc *md,
			     const struct drm_format_info *format)
{
	if (format_is_yuv_semiplanar(md, format))
		return md->ccs.planar_aux_planes;
	else
		return md->ccs.packed_aux_planes;
}

/**
 * intel_fb_is_ccs_aux_plane: Check if a framebuffer color plane is a CCS AUX plane
 * @fb: Framebuffer
 * @color_plane: color plane index to check
 *
 * Returns:
 * Returns %true if @fb's color plane at index @color_plane is a CCS AUX plane.
 */
bool intel_fb_is_ccs_aux_plane(const struct drm_framebuffer *fb, int color_plane)
{
	const struct intel_modifier_desc *md = lookup_modifier(fb->modifier);

	return ccs_aux_plane_mask(md, fb->format) & BIT(color_plane);
}

/**
 * intel_fb_is_gen12_ccs_aux_plane: Check if a framebuffer color plane is a GEN12 CCS AUX plane
 * @fb: Framebuffer
 * @color_plane: color plane index to check
 *
 * Returns:
 * Returns %true if @fb's color plane at index @color_plane is a GEN12 CCS AUX plane.
 */
static bool intel_fb_is_gen12_ccs_aux_plane(const struct drm_framebuffer *fb, int color_plane)
{
	const struct intel_modifier_desc *md = lookup_modifier(fb->modifier);

	return check_modifier_display_ver_range(md, 12, 14) &&
	       ccs_aux_plane_mask(md, fb->format) & BIT(color_plane);
}

/**
 * intel_fb_rc_ccs_cc_plane: Get the CCS CC color plane index for a framebuffer
 * @fb: Framebuffer
 *
 * Returns:
 * Returns the index of the color clear plane for @fb, or -1 if @fb is not a
 * framebuffer using a render compression/color clear modifier.
 */
int intel_fb_rc_ccs_cc_plane(const struct drm_framebuffer *fb)
{
	const struct intel_modifier_desc *md = lookup_modifier(fb->modifier);

	if (!md->ccs.cc_planes)
		return -1;

	drm_WARN_ON_ONCE(fb->dev, hweight8(md->ccs.cc_planes) > 1);

	return ilog2((int)md->ccs.cc_planes);
}

static bool is_gen12_ccs_cc_plane(const struct drm_framebuffer *fb, int color_plane)
{
	return intel_fb_rc_ccs_cc_plane(fb) == color_plane;
}

static bool is_semiplanar_uv_plane(const struct drm_framebuffer *fb, int color_plane)
{
	return intel_format_info_is_yuv_semiplanar(fb->format, fb->modifier) &&
		color_plane == 1;
}

bool is_surface_linear(const struct drm_framebuffer *fb, int color_plane)
{
	return fb->modifier == DRM_FORMAT_MOD_LINEAR ||
	       intel_fb_is_gen12_ccs_aux_plane(fb, color_plane) ||
	       is_gen12_ccs_cc_plane(fb, color_plane);
}

int main_to_ccs_plane(const struct drm_framebuffer *fb, int main_plane)
{
	drm_WARN_ON(fb->dev, !intel_fb_is_ccs_modifier(fb->modifier) ||
		    (main_plane && main_plane >= fb->format->num_planes / 2));

	return fb->format->num_planes / 2 + main_plane;
}

int skl_ccs_to_main_plane(const struct drm_framebuffer *fb, int ccs_plane)
{
	drm_WARN_ON(fb->dev, !intel_fb_is_ccs_modifier(fb->modifier) ||
		    ccs_plane < fb->format->num_planes / 2);

	if (is_gen12_ccs_cc_plane(fb, ccs_plane))
		return 0;

	return ccs_plane - fb->format->num_planes / 2;
}

static unsigned int gen12_ccs_aux_stride(struct intel_framebuffer *fb, int ccs_plane)
{
	int main_plane = skl_ccs_to_main_plane(&fb->base, ccs_plane);
	unsigned int main_stride = fb->base.pitches[main_plane];
	unsigned int main_tile_width = intel_tile_width_bytes(&fb->base, main_plane);

	return DIV_ROUND_UP(main_stride, 4 * main_tile_width) * 64;
}

int skl_main_to_aux_plane(const struct drm_framebuffer *fb, int main_plane)
{
	const struct intel_modifier_desc *md = lookup_modifier(fb->modifier);
	struct drm_i915_private *i915 = to_i915(fb->dev);

	if (md->ccs.packed_aux_planes | md->ccs.planar_aux_planes)
		return main_to_ccs_plane(fb, main_plane);
	else if (DISPLAY_VER(i915) < 11 &&
		 format_is_yuv_semiplanar(md, fb->format))
		return 1;
	else
		return 0;
}

unsigned int intel_tile_size(const struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) == 2 ? 2048 : 4096;
}

unsigned int
intel_tile_width_bytes(const struct drm_framebuffer *fb, int color_plane)
{
	struct drm_i915_private *dev_priv = to_i915(fb->dev);
	unsigned int cpp = fb->format->cpp[color_plane];

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		return intel_tile_size(dev_priv);
	case I915_FORMAT_MOD_X_TILED:
		if (DISPLAY_VER(dev_priv) == 2)
			return 128;
		else
			return 512;
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
	case I915_FORMAT_MOD_4_TILED:
		/*
		 * Each 4K tile consists of 64B(8*8) subtiles, with
		 * same shape as Y Tile(i.e 4*16B OWords)
		 */
		return 128;
	case I915_FORMAT_MOD_Y_TILED_CCS:
		if (intel_fb_is_ccs_aux_plane(fb, color_plane))
			return 128;
		fallthrough;
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_MTL_MC_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
		if (intel_fb_is_ccs_aux_plane(fb, color_plane) ||
		    is_gen12_ccs_cc_plane(fb, color_plane))
			return 64;
		fallthrough;
	case I915_FORMAT_MOD_Y_TILED:
		if (DISPLAY_VER(dev_priv) == 2 || HAS_128_BYTE_Y_TILING(dev_priv))
			return 128;
		else
			return 512;
	case I915_FORMAT_MOD_Yf_TILED_CCS:
		if (intel_fb_is_ccs_aux_plane(fb, color_plane))
			return 128;
		fallthrough;
	case I915_FORMAT_MOD_Yf_TILED:
		switch (cpp) {
		case 1:
			return 64;
		case 2:
		case 4:
			return 128;
		case 8:
		case 16:
			return 256;
		default:
			MISSING_CASE(cpp);
			return cpp;
		}
		break;
	default:
		MISSING_CASE(fb->modifier);
		return cpp;
	}
}

unsigned int intel_tile_height(const struct drm_framebuffer *fb, int color_plane)
{
	return intel_tile_size(to_i915(fb->dev)) /
		intel_tile_width_bytes(fb, color_plane);
}

/*
 * Return the tile dimensions in pixel units, based on the (2 or 4 kbyte) GTT
 * page tile size.
 */
static void intel_tile_dims(const struct drm_framebuffer *fb, int color_plane,
			    unsigned int *tile_width,
			    unsigned int *tile_height)
{
	unsigned int tile_width_bytes = intel_tile_width_bytes(fb, color_plane);
	unsigned int cpp = fb->format->cpp[color_plane];

	*tile_width = tile_width_bytes / cpp;
	*tile_height = intel_tile_height(fb, color_plane);
}

/*
 * Return the tile dimensions in pixel units, based on the tile block size.
 * The block covers the full GTT page sized tile on all tiled surfaces and
 * it's a 64 byte portion of the tile on TGL+ CCS surfaces.
 */
static void intel_tile_block_dims(const struct drm_framebuffer *fb, int color_plane,
				  unsigned int *tile_width,
				  unsigned int *tile_height)
{
	intel_tile_dims(fb, color_plane, tile_width, tile_height);

	if (intel_fb_is_gen12_ccs_aux_plane(fb, color_plane))
		*tile_height = 1;
}

unsigned int intel_tile_row_size(const struct drm_framebuffer *fb, int color_plane)
{
	unsigned int tile_width, tile_height;

	intel_tile_dims(fb, color_plane, &tile_width, &tile_height);

	return fb->pitches[color_plane] * tile_height;
}

unsigned int
intel_fb_align_height(const struct drm_framebuffer *fb,
		      int color_plane, unsigned int height)
{
	unsigned int tile_height = intel_tile_height(fb, color_plane);

	return ALIGN(height, tile_height);
}

bool intel_fb_modifier_uses_dpt(struct drm_i915_private *i915, u64 modifier)
{
	return HAS_DPT(i915) && modifier != DRM_FORMAT_MOD_LINEAR;
}

bool intel_fb_uses_dpt(const struct drm_framebuffer *fb)
{
	return to_i915(fb->dev)->display.params.enable_dpt &&
		intel_fb_modifier_uses_dpt(to_i915(fb->dev), fb->modifier);
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

static unsigned int intel_linear_alignment(const struct drm_i915_private *dev_priv)
{
	if (DISPLAY_VER(dev_priv) >= 9)
		return 256 * 1024;
	else if (IS_I965G(dev_priv) || IS_I965GM(dev_priv) ||
		 IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return 128 * 1024;
	else if (DISPLAY_VER(dev_priv) >= 4)
		return 4 * 1024;
	else
		return 0;
}

unsigned int intel_surf_alignment(const struct drm_framebuffer *fb,
				  int color_plane)
{
	struct drm_i915_private *dev_priv = to_i915(fb->dev);

	if (intel_fb_uses_dpt(fb)) {
		/* AUX_DIST needs only 4K alignment */
		if (intel_fb_is_ccs_aux_plane(fb, color_plane))
			return 512 * 4096;

		/*
		 * FIXME ADL sees GGTT/DMAR faults with async
		 * flips unless we align to 16k at least.
		 * Figure out what's going on here...
		 */
		if (IS_ALDERLAKE_P(dev_priv) &&
		    !intel_fb_is_ccs_modifier(fb->modifier) &&
		    HAS_ASYNC_FLIPS(dev_priv))
			return 512 * 16 * 1024;

		return 512 * 4096;
	}

	/* AUX_DIST needs only 4K alignment */
	if (intel_fb_is_ccs_aux_plane(fb, color_plane))
		return 4096;

	if (is_semiplanar_uv_plane(fb, color_plane)) {
		/*
		 * TODO: cross-check wrt. the bspec stride in bytes * 64 bytes
		 * alignment for linear UV planes on all platforms.
		 */
		if (DISPLAY_VER(dev_priv) >= 12) {
			if (fb->modifier == DRM_FORMAT_MOD_LINEAR)
				return intel_linear_alignment(dev_priv);

			return intel_tile_row_size(fb, color_plane);
		}

		return 4096;
	}

	drm_WARN_ON(&dev_priv->drm, color_plane != 0);

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		return intel_linear_alignment(dev_priv);
	case I915_FORMAT_MOD_X_TILED:
		if (HAS_ASYNC_FLIPS(dev_priv))
			return 256 * 1024;
		return 0;
	case I915_FORMAT_MOD_Y_TILED_GEN12_MC_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS:
	case I915_FORMAT_MOD_Y_TILED_GEN12_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_MTL_MC_CCS:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_MTL_RC_CCS_CC:
		return 16 * 1024;
	case I915_FORMAT_MOD_Y_TILED_CCS:
	case I915_FORMAT_MOD_Yf_TILED_CCS:
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_4_TILED:
	case I915_FORMAT_MOD_Yf_TILED:
		return 1 * 1024 * 1024;
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS:
	case I915_FORMAT_MOD_4_TILED_DG2_RC_CCS_CC:
	case I915_FORMAT_MOD_4_TILED_DG2_MC_CCS:
		return 16 * 1024;
	default:
		MISSING_CASE(fb->modifier);
		return 0;
	}
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
	if (!intel_fb_is_gen12_ccs_aux_plane(fb, color_plane)) {
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
	int main_plane = intel_fb_is_ccs_aux_plane(&fb->base, color_plane) ?
			 skl_ccs_to_main_plane(&fb->base, color_plane) : 0;
	unsigned int main_width = fb->base.width;
	unsigned int main_height = fb->base.height;
	int main_hsub, main_vsub;
	int hsub, vsub;

	intel_fb_plane_get_subsampling(&main_hsub, &main_vsub, &fb->base, main_plane);
	intel_fb_plane_get_subsampling(&hsub, &vsub, &fb->base, color_plane);

	*w = DIV_ROUND_UP(main_width, main_hsub * hsub);
	*h = DIV_ROUND_UP(main_height, main_vsub * vsub);
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

static u32 intel_adjust_linear_offset(int *x, int *y,
				      unsigned int cpp,
				      unsigned int pitch,
				      u32 old_offset,
				      u32 new_offset)
{
	old_offset += *y * pitch + *x * cpp;

	*y = (old_offset - new_offset) / pitch;
	*x = ((old_offset - new_offset) - *y * pitch) / cpp;

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
		intel_adjust_linear_offset(x, y, cpp, pitch,
					   old_offset, new_offset);
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
					   state->view.color_plane[color_plane].mapping_stride,
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
					unsigned int alignment)
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
	unsigned int pitch = state->view.color_plane[color_plane].mapping_stride;
	unsigned int alignment;

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
	unsigned int height, alignment, unused;

	if (DISPLAY_VER(i915) >= 12 &&
	    !intel_fb_needs_pot_stride_remap(to_intel_framebuffer(fb)) &&
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

	height = drm_format_info_plane_height(fb->format, fb->height, color_plane);
	height = ALIGN(height, intel_tile_height(fb, color_plane));

	/* Catch potential overflows early */
	if (check_add_overflow(mul_u32_u32(height, fb->pitches[color_plane]),
			       fb->offsets[color_plane], &unused)) {
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

	if (!intel_fb_is_ccs_aux_plane(fb, ccs_plane))
		return 0;

	/*
	 * While all the tile dimensions are based on a 2k or 4k GTT page size
	 * here the main and CCS coordinates must match only within a (64 byte
	 * on TGL+) block inside the tile.
	 */
	intel_tile_block_dims(fb, ccs_plane, &tile_width, &tile_height);
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
	if (intel_fb_is_ccs_modifier(fb->modifier))
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

	return (IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14) &&
		intel_fb_uses_dpt(&fb->base);
}

static int intel_fb_pitch(const struct intel_framebuffer *fb, int color_plane, unsigned int rotation)
{
	if (drm_rotation_90_or_270(rotation))
		return fb->rotated_view.color_plane[color_plane].mapping_stride;
	else if (intel_fb_needs_pot_stride_remap(fb))
		return fb->remapped_view.color_plane[color_plane].mapping_stride;
	else
		return fb->normal_view.color_plane[color_plane].mapping_stride;
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
	if (intel_fb_needs_pot_stride_remap(fb)) {
		/*
		 * ADL_P, the only platform needing a POT stride has a minimum
		 * of 8 main surface tiles.
		 */
		return roundup_pow_of_two(max(pitch_tiles, 8u));
	} else {
		return pitch_tiles;
	}
}

static unsigned int
plane_view_scanout_stride(const struct intel_framebuffer *fb, int color_plane,
			  unsigned int tile_width,
			  unsigned int src_stride_tiles, unsigned int dst_stride_tiles)
{
	struct drm_i915_private *i915 = to_i915(fb->base.dev);
	unsigned int stride_tiles;

	if ((IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14) &&
	    src_stride_tiles < dst_stride_tiles)
		stride_tiles = src_stride_tiles;
	else
		stride_tiles = dst_stride_tiles;

	return stride_tiles * tile_width * fb->base.format->cpp[color_plane];
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

static unsigned int
plane_view_linear_tiles(const struct intel_framebuffer *fb, int color_plane,
			const struct fb_plane_view_dims *dims,
			int x, int y)
{
	struct drm_i915_private *i915 = to_i915(fb->base.dev);
	unsigned int size;

	size = (y + dims->height) * fb->base.pitches[color_plane] +
		x * fb->base.format->cpp[color_plane];

	return DIV_ROUND_UP(size, intel_tile_size(i915));
}

#define assign_chk_ovf(i915, var, val) ({ \
	drm_WARN_ON(&(i915)->drm, overflows_type(val, var)); \
	(var) = (val); \
})

#define assign_bfld_chk_ovf(i915, var, val) ({ \
	(var) = (val); \
	drm_WARN_ON(&(i915)->drm, (var) != (val)); \
	(var); \
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
	u32 size = 0;

	assign_bfld_chk_ovf(i915, remap_info->offset, obj_offset);

	if (intel_fb_is_gen12_ccs_aux_plane(&fb->base, color_plane)) {
		remap_info->linear = 1;

		assign_chk_ovf(i915, remap_info->size,
			       plane_view_linear_tiles(fb, color_plane, dims, x, y));
	} else {
		remap_info->linear = 0;

		assign_chk_ovf(i915, remap_info->src_stride,
			       plane_view_src_stride_tiles(fb, color_plane, dims));
		assign_chk_ovf(i915, remap_info->width,
			       plane_view_width_tiles(fb, color_plane, dims, x));
		assign_chk_ovf(i915, remap_info->height,
			       plane_view_height_tiles(fb, color_plane, dims, y));
	}

	if (view->gtt.type == I915_GTT_VIEW_ROTATED) {
		drm_WARN_ON(&i915->drm, remap_info->linear);
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

		color_plane_info->mapping_stride = remap_info->dst_stride * tile_height;
		color_plane_info->scanout_stride = color_plane_info->mapping_stride;

		size += remap_info->dst_stride * remap_info->width;

		/* rotate the tile dimensions to match the GTT view */
		swap(tile_width, tile_height);
	} else {
		drm_WARN_ON(&i915->drm, view->gtt.type != I915_GTT_VIEW_REMAPPED);

		check_array_bounds(i915, view->gtt.remapped.plane, color_plane);

		if (view->gtt.remapped.plane_alignment) {
			u32 aligned_offset = ALIGN(gtt_offset,
						   view->gtt.remapped.plane_alignment);

			size += aligned_offset - gtt_offset;
			gtt_offset = aligned_offset;
		}

		color_plane_info->x = x;
		color_plane_info->y = y;

		if (remap_info->linear) {
			color_plane_info->mapping_stride = fb->base.pitches[color_plane];
			color_plane_info->scanout_stride = color_plane_info->mapping_stride;

			size += remap_info->size;
		} else {
			unsigned int dst_stride;

			/*
			 * The hardware automagically calculates the CCS AUX surface
			 * stride from the main surface stride so can't really remap a
			 * smaller subset (unless we'd remap in whole AUX page units).
			 */
			if (intel_fb_needs_pot_stride_remap(fb) &&
			    intel_fb_is_ccs_modifier(fb->base.modifier))
				dst_stride = remap_info->src_stride;
			else
				dst_stride = remap_info->width;

			dst_stride = plane_view_dst_stride_tiles(fb, color_plane, dst_stride);

			assign_chk_ovf(i915, remap_info->dst_stride, dst_stride);
			color_plane_info->mapping_stride = dst_stride *
							   tile_width *
							   fb->base.format->cpp[color_plane];
			color_plane_info->scanout_stride =
				plane_view_scanout_stride(fb, color_plane, tile_width,
							  remap_info->src_stride,
							  dst_stride);

			size += dst_stride * remap_info->height;
		}
	}

	/*
	 * We only keep the x/y offsets, so push all of the gtt offset into
	 * the x/y offsets.  x,y will hold the first pixel of the framebuffer
	 * plane from the start of the remapped/rotated gtt mapping.
	 */
	if (remap_info->linear)
		intel_adjust_linear_offset(&color_plane_info->x, &color_plane_info->y,
					   fb->base.format->cpp[color_plane],
					   color_plane_info->mapping_stride,
					   gtt_offset * tile_size, 0);
	else
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
	unsigned int tiles;

	if (is_surface_linear(&fb->base, color_plane)) {
		tiles = plane_view_linear_tiles(fb, color_plane, dims, x, y);
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

static void intel_fb_view_init(struct drm_i915_private *i915, struct intel_fb_view *view,
			       enum i915_gtt_view_type view_type)
{
	memset(view, 0, sizeof(*view));
	view->gtt.type = view_type;

	if (view_type == I915_GTT_VIEW_REMAPPED &&
	    (IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14))
		view->gtt.remapped.plane_alignment = SZ_2M / PAGE_SIZE;
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

	intel_fb_view_init(i915, &fb->normal_view, I915_GTT_VIEW_NORMAL);

	drm_WARN_ON(&i915->drm,
		    intel_fb_supports_90_270_rotation(fb) &&
		    intel_fb_needs_pot_stride_remap(fb));

	if (intel_fb_supports_90_270_rotation(fb))
		intel_fb_view_init(i915, &fb->rotated_view, I915_GTT_VIEW_ROTATED);
	if (intel_fb_needs_pot_stride_remap(fb))
		intel_fb_view_init(i915, &fb->remapped_view, I915_GTT_VIEW_REMAPPED);

	for (i = 0; i < num_planes; i++) {
		struct fb_plane_view_dims view_dims;
		unsigned int width, height;
		unsigned int size;
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
		fb->normal_view.color_plane[i].mapping_stride = fb->base.pitches[i];
		fb->normal_view.color_plane[i].scanout_stride =
			fb->normal_view.color_plane[i].mapping_stride;

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

	if (mul_u32_u32(max_size, tile_size) > intel_bo_to_drm_bo(obj)->size) {
		drm_dbg_kms(&i915->drm,
			    "fb too big for bo (need %llu bytes, have %zu bytes)\n",
			    mul_u32_u32(max_size, tile_size), intel_bo_to_drm_bo(obj)->size);
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

	intel_fb_view_init(i915, &plane_state->view,
			   drm_rotation_90_or_270(rotation) ? I915_GTT_VIEW_ROTATED :
							      I915_GTT_VIEW_REMAPPED);

	src_x = plane_state->uapi.src.x1 >> 16;
	src_y = plane_state->uapi.src.y1 >> 16;
	src_w = drm_rect_width(&plane_state->uapi.src) >> 16;
	src_h = drm_rect_height(&plane_state->uapi.src) >> 16;

	drm_WARN_ON(&i915->drm, intel_fb_is_ccs_modifier(fb->modifier));

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

static
u32 intel_fb_max_stride(struct drm_i915_private *dev_priv,
			u32 pixel_format, u64 modifier)
{
	/*
	 * Arbitrary limit for gen4+ chosen to match the
	 * render engine max stride.
	 *
	 * The new CCS hash mode makes remapping impossible
	 */
	if (DISPLAY_VER(dev_priv) < 4 || intel_fb_is_ccs_modifier(modifier) ||
	    intel_fb_modifier_uses_dpt(dev_priv, modifier))
		return intel_plane_fb_max_stride(dev_priv, pixel_format, modifier);
	else if (DISPLAY_VER(dev_priv) >= 7)
		return 256 * 1024;
	else
		return 128 * 1024;
}

static unsigned int
intel_fb_stride_alignment(const struct drm_framebuffer *fb, int color_plane)
{
	struct drm_i915_private *dev_priv = to_i915(fb->dev);
	unsigned int tile_width;

	if (is_surface_linear(fb, color_plane)) {
		unsigned int max_stride = intel_plane_fb_max_stride(dev_priv,
								    fb->format->format,
								    fb->modifier);

		/*
		 * To make remapping with linear generally feasible
		 * we need the stride to be page aligned.
		 */
		if (fb->pitches[color_plane] > max_stride &&
		    !intel_fb_is_ccs_modifier(fb->modifier))
			return intel_tile_size(dev_priv);
		else
			return 64;
	}

	tile_width = intel_tile_width_bytes(fb, color_plane);
	if (intel_fb_is_ccs_modifier(fb->modifier)) {
		/*
		 * On TGL the surface stride must be 4 tile aligned, mapped by
		 * one 64 byte cacheline on the CCS AUX surface.
		 */
		if (DISPLAY_VER(dev_priv) >= 12)
			tile_width *= 4;
		/*
		 * Display WA #0531: skl,bxt,kbl,glk
		 *
		 * Render decompression and plane width > 3840
		 * combined with horizontal panning requires the
		 * plane stride to be a multiple of 4. We'll just
		 * require the entire fb to accommodate that to avoid
		 * potential runtime errors at plane configuration time.
		 */
		else if ((DISPLAY_VER(dev_priv) == 9 || IS_GEMINILAKE(dev_priv)) &&
			 color_plane == 0 && fb->width > 3840)
			tile_width *= 4;
	}
	return tile_width;
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
	stride = plane_state->view.color_plane[0].mapping_stride;
	max_stride = plane->max_stride(plane, fb->format->format,
				       fb->modifier, rotation);

	if (stride > max_stride) {
		drm_dbg_kms(plane->base.dev,
			    "[FB:%d] stride (%d) exceeds [PLANE:%d:%s] max stride (%d)\n",
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

static void intel_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct intel_framebuffer *intel_fb = to_intel_framebuffer(fb);

	drm_framebuffer_cleanup(fb);

	if (intel_fb_uses_dpt(fb))
		intel_dpt_destroy(intel_fb->dpt_vm);

	intel_frontbuffer_put(intel_fb->frontbuffer);

	intel_fb_bo_framebuffer_fini(intel_fb_obj(fb));

	kfree(intel_fb);
}

static int intel_user_framebuffer_create_handle(struct drm_framebuffer *fb,
						struct drm_file *file,
						unsigned int *handle)
{
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct drm_i915_private *i915 = to_i915(intel_bo_to_drm_bo(obj)->dev);

	if (i915_gem_object_is_userptr(obj)) {
		drm_dbg(&i915->drm,
			"attempting to use a userptr for a framebuffer, denied\n");
		return -EINVAL;
	}

	return drm_gem_handle_create(file, intel_bo_to_drm_bo(obj), handle);
}

struct frontbuffer_fence_cb {
	struct dma_fence_cb base;
	struct intel_frontbuffer *front;
};

static void intel_user_framebuffer_fence_wake(struct dma_fence *dma,
					      struct dma_fence_cb *data)
{
	struct frontbuffer_fence_cb *cb = container_of(data, typeof(*cb), base);

	intel_frontbuffer_queue_flush(cb->front);
	kfree(cb);
	dma_fence_put(dma);
}

static int intel_user_framebuffer_dirty(struct drm_framebuffer *fb,
					struct drm_file *file,
					unsigned int flags, unsigned int color,
					struct drm_clip_rect *clips,
					unsigned int num_clips)
{
	struct drm_i915_gem_object *obj = intel_fb_obj(fb);
	struct intel_frontbuffer *front = to_intel_frontbuffer(fb);
	struct dma_fence *fence;
	struct frontbuffer_fence_cb *cb;
	int ret = 0;

	if (!atomic_read(&front->bits))
		return 0;

	if (dma_resv_test_signaled(intel_bo_to_drm_bo(obj)->resv, dma_resv_usage_rw(false)))
		goto flush;

	ret = dma_resv_get_singleton(intel_bo_to_drm_bo(obj)->resv, dma_resv_usage_rw(false),
				     &fence);
	if (ret || !fence)
		goto flush;

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb) {
		dma_fence_put(fence);
		ret = -ENOMEM;
		goto flush;
	}

	cb->front = front;

	intel_frontbuffer_invalidate(front, ORIGIN_DIRTYFB);

	ret = dma_fence_add_callback(fence, &cb->base,
				     intel_user_framebuffer_fence_wake);
	if (ret) {
		intel_user_framebuffer_fence_wake(fence, &cb->base);
		if (ret == -ENOENT)
			ret = 0;
	}

	return ret;

flush:
	i915_gem_object_flush_if_display(obj);
	intel_frontbuffer_flush(front, ORIGIN_DIRTYFB);
	return ret;
}

static const struct drm_framebuffer_funcs intel_fb_funcs = {
	.destroy = intel_user_framebuffer_destroy,
	.create_handle = intel_user_framebuffer_create_handle,
	.dirty = intel_user_framebuffer_dirty,
};

int intel_framebuffer_init(struct intel_framebuffer *intel_fb,
			   struct drm_i915_gem_object *obj,
			   struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_i915_private *dev_priv = to_i915(intel_bo_to_drm_bo(obj)->dev);
	struct drm_framebuffer *fb = &intel_fb->base;
	u32 max_stride;
	int ret = -EINVAL;
	int i;

	ret = intel_fb_bo_framebuffer_init(intel_fb, obj, mode_cmd);
	if (ret)
		return ret;

	intel_fb->frontbuffer = intel_frontbuffer_get(obj);
	if (!intel_fb->frontbuffer) {
		ret = -ENOMEM;
		goto err;
	}

	ret = -EINVAL;
	if (!drm_any_plane_has_format(&dev_priv->drm,
				      mode_cmd->pixel_format,
				      mode_cmd->modifier[0])) {
		drm_dbg_kms(&dev_priv->drm,
			    "unsupported pixel format %p4cc / modifier 0x%llx\n",
			    &mode_cmd->pixel_format, mode_cmd->modifier[0]);
		goto err_frontbuffer_put;
	}

	max_stride = intel_fb_max_stride(dev_priv, mode_cmd->pixel_format,
					 mode_cmd->modifier[0]);
	if (mode_cmd->pitches[0] > max_stride) {
		drm_dbg_kms(&dev_priv->drm,
			    "%s pitch (%u) must be at most %d\n",
			    mode_cmd->modifier[0] != DRM_FORMAT_MOD_LINEAR ?
			    "tiled" : "linear",
			    mode_cmd->pitches[0], max_stride);
		goto err_frontbuffer_put;
	}

	/* FIXME need to adjust LINOFF/TILEOFF accordingly. */
	if (mode_cmd->offsets[0] != 0) {
		drm_dbg_kms(&dev_priv->drm,
			    "plane 0 offset (0x%08x) must be 0\n",
			    mode_cmd->offsets[0]);
		goto err_frontbuffer_put;
	}

	drm_helper_mode_fill_fb_struct(&dev_priv->drm, fb, mode_cmd);

	for (i = 0; i < fb->format->num_planes; i++) {
		unsigned int stride_alignment;

		if (mode_cmd->handles[i] != mode_cmd->handles[0]) {
			drm_dbg_kms(&dev_priv->drm, "bad plane %d handle\n",
				    i);
			goto err_frontbuffer_put;
		}

		stride_alignment = intel_fb_stride_alignment(fb, i);
		if (fb->pitches[i] & (stride_alignment - 1)) {
			drm_dbg_kms(&dev_priv->drm,
				    "plane %d pitch (%d) must be at least %u byte aligned\n",
				    i, fb->pitches[i], stride_alignment);
			goto err_frontbuffer_put;
		}

		if (intel_fb_is_gen12_ccs_aux_plane(fb, i)) {
			unsigned int ccs_aux_stride = gen12_ccs_aux_stride(intel_fb, i);

			if (fb->pitches[i] != ccs_aux_stride) {
				drm_dbg_kms(&dev_priv->drm,
					    "ccs aux plane %d pitch (%d) must be %d\n",
					    i,
					    fb->pitches[i], ccs_aux_stride);
				goto err_frontbuffer_put;
			}
		}

		fb->obj[i] = intel_bo_to_drm_bo(obj);
	}

	ret = intel_fill_fb_info(dev_priv, intel_fb);
	if (ret)
		goto err_frontbuffer_put;

	if (intel_fb_uses_dpt(fb)) {
		struct i915_address_space *vm;

		vm = intel_dpt_create(intel_fb);
		if (IS_ERR(vm)) {
			drm_dbg_kms(&dev_priv->drm, "failed to create DPT\n");
			ret = PTR_ERR(vm);
			goto err_frontbuffer_put;
		}

		intel_fb->dpt_vm = vm;
	}

	ret = drm_framebuffer_init(&dev_priv->drm, fb, &intel_fb_funcs);
	if (ret) {
		drm_err(&dev_priv->drm, "framebuffer init failed %d\n", ret);
		goto err_free_dpt;
	}

	return 0;

err_free_dpt:
	if (intel_fb_uses_dpt(fb))
		intel_dpt_destroy(intel_fb->dpt_vm);
err_frontbuffer_put:
	intel_frontbuffer_put(intel_fb->frontbuffer);
err:
	intel_fb_bo_framebuffer_fini(obj);
	return ret;
}

struct drm_framebuffer *
intel_user_framebuffer_create(struct drm_device *dev,
			      struct drm_file *filp,
			      const struct drm_mode_fb_cmd2 *user_mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_i915_gem_object *obj;
	struct drm_mode_fb_cmd2 mode_cmd = *user_mode_cmd;
	struct drm_i915_private *i915 = to_i915(dev);

	obj = intel_fb_bo_lookup_valid_bo(i915, filp, &mode_cmd);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	fb = intel_framebuffer_create(obj, &mode_cmd);
	drm_gem_object_put(intel_bo_to_drm_bo(obj));

	return fb;
}

struct drm_framebuffer *
intel_framebuffer_create(struct drm_i915_gem_object *obj,
			 struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct intel_framebuffer *intel_fb;
	int ret;

	intel_fb = kzalloc(sizeof(*intel_fb), GFP_KERNEL);
	if (!intel_fb)
		return ERR_PTR(-ENOMEM);

	ret = intel_framebuffer_init(intel_fb, obj, mode_cmd);
	if (ret)
		goto err;

	return &intel_fb->base;

err:
	kfree(intel_fb);
	return ERR_PTR(ret);
}
