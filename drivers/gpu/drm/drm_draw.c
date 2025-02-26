// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright (c) 2023 Red Hat.
 * Author: Jocelyn Falempe <jfalempe@redhat.com>
 */

#include <linux/bits.h>
#include <linux/iosys-map.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>

#include "drm_draw_internal.h"

/*
 * Conversions from xrgb8888
 */

static u16 convert_xrgb8888_to_rgb565(u32 pix)
{
	return ((pix & 0x00F80000) >> 8) |
	       ((pix & 0x0000FC00) >> 5) |
	       ((pix & 0x000000F8) >> 3);
}

static u16 convert_xrgb8888_to_rgba5551(u32 pix)
{
	return ((pix & 0x00f80000) >> 8) |
	       ((pix & 0x0000f800) >> 5) |
	       ((pix & 0x000000f8) >> 2) |
	       BIT(0); /* set alpha bit */
}

static u16 convert_xrgb8888_to_xrgb1555(u32 pix)
{
	return ((pix & 0x00f80000) >> 9) |
	       ((pix & 0x0000f800) >> 6) |
	       ((pix & 0x000000f8) >> 3);
}

static u16 convert_xrgb8888_to_argb1555(u32 pix)
{
	return BIT(15) | /* set alpha bit */
	       ((pix & 0x00f80000) >> 9) |
	       ((pix & 0x0000f800) >> 6) |
	       ((pix & 0x000000f8) >> 3);
}

static u32 convert_xrgb8888_to_argb8888(u32 pix)
{
	return pix | GENMASK(31, 24); /* fill alpha bits */
}

static u32 convert_xrgb8888_to_xbgr8888(u32 pix)
{
	return ((pix & 0x00ff0000) >> 16) <<  0 |
	       ((pix & 0x0000ff00) >>  8) <<  8 |
	       ((pix & 0x000000ff) >>  0) << 16 |
	       ((pix & 0xff000000) >> 24) << 24;
}

static u32 convert_xrgb8888_to_abgr8888(u32 pix)
{
	return ((pix & 0x00ff0000) >> 16) <<  0 |
	       ((pix & 0x0000ff00) >>  8) <<  8 |
	       ((pix & 0x000000ff) >>  0) << 16 |
	       GENMASK(31, 24); /* fill alpha bits */
}

static u32 convert_xrgb8888_to_xrgb2101010(u32 pix)
{
	pix = ((pix & 0x000000FF) << 2) |
	      ((pix & 0x0000FF00) << 4) |
	      ((pix & 0x00FF0000) << 6);
	return pix | ((pix >> 8) & 0x00300C03);
}

static u32 convert_xrgb8888_to_argb2101010(u32 pix)
{
	pix = ((pix & 0x000000FF) << 2) |
	      ((pix & 0x0000FF00) << 4) |
	      ((pix & 0x00FF0000) << 6);
	return GENMASK(31, 30) /* set alpha bits */ | pix | ((pix >> 8) & 0x00300C03);
}

static u32 convert_xrgb8888_to_abgr2101010(u32 pix)
{
	pix = ((pix & 0x00FF0000) >> 14) |
	      ((pix & 0x0000FF00) << 4) |
	      ((pix & 0x000000FF) << 22);
	return GENMASK(31, 30) /* set alpha bits */ | pix | ((pix >> 8) & 0x00300C03);
}

/**
 * drm_draw_color_from_xrgb8888 - convert one pixel from xrgb8888 to the desired format
 * @color: input color, in xrgb8888 format
 * @format: output format
 *
 * Returns:
 * Color in the format specified, casted to u32.
 * Or 0 if the format is not supported.
 */
u32 drm_draw_color_from_xrgb8888(u32 color, u32 format)
{
	switch (format) {
	case DRM_FORMAT_RGB565:
		return convert_xrgb8888_to_rgb565(color);
	case DRM_FORMAT_RGBA5551:
		return convert_xrgb8888_to_rgba5551(color);
	case DRM_FORMAT_XRGB1555:
		return convert_xrgb8888_to_xrgb1555(color);
	case DRM_FORMAT_ARGB1555:
		return convert_xrgb8888_to_argb1555(color);
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		return color;
	case DRM_FORMAT_ARGB8888:
		return convert_xrgb8888_to_argb8888(color);
	case DRM_FORMAT_XBGR8888:
		return convert_xrgb8888_to_xbgr8888(color);
	case DRM_FORMAT_ABGR8888:
		return convert_xrgb8888_to_abgr8888(color);
	case DRM_FORMAT_XRGB2101010:
		return convert_xrgb8888_to_xrgb2101010(color);
	case DRM_FORMAT_ARGB2101010:
		return convert_xrgb8888_to_argb2101010(color);
	case DRM_FORMAT_ABGR2101010:
		return convert_xrgb8888_to_abgr2101010(color);
	default:
		WARN_ONCE(1, "Can't convert to %p4cc\n", &format);
		return 0;
	}
}
EXPORT_SYMBOL(drm_draw_color_from_xrgb8888);

/*
 * Blit functions
 */
void drm_draw_blit16(struct iosys_map *dmap, unsigned int dpitch,
		     const u8 *sbuf8, unsigned int spitch,
		     unsigned int height, unsigned int width,
		     unsigned int scale, u16 fg16)
{
	unsigned int y, x;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			if (drm_draw_is_pixel_fg(sbuf8, spitch, x / scale, y / scale))
				iosys_map_wr(dmap, y * dpitch + x * sizeof(u16), u16, fg16);
}
EXPORT_SYMBOL(drm_draw_blit16);

void drm_draw_blit24(struct iosys_map *dmap, unsigned int dpitch,
		     const u8 *sbuf8, unsigned int spitch,
		     unsigned int height, unsigned int width,
		     unsigned int scale, u32 fg32)
{
	unsigned int y, x;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			u32 off = y * dpitch + x * 3;

			if (drm_draw_is_pixel_fg(sbuf8, spitch, x / scale, y / scale)) {
				/* write blue-green-red to output in little endianness */
				iosys_map_wr(dmap, off, u8, (fg32 & 0x000000FF) >> 0);
				iosys_map_wr(dmap, off + 1, u8, (fg32 & 0x0000FF00) >> 8);
				iosys_map_wr(dmap, off + 2, u8, (fg32 & 0x00FF0000) >> 16);
			}
		}
	}
}
EXPORT_SYMBOL(drm_draw_blit24);

void drm_draw_blit32(struct iosys_map *dmap, unsigned int dpitch,
		     const u8 *sbuf8, unsigned int spitch,
		     unsigned int height, unsigned int width,
		     unsigned int scale, u32 fg32)
{
	unsigned int y, x;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			if (drm_draw_is_pixel_fg(sbuf8, spitch, x / scale, y / scale))
				iosys_map_wr(dmap, y * dpitch + x * sizeof(u32), u32, fg32);
}
EXPORT_SYMBOL(drm_draw_blit32);

/*
 * Fill functions
 */
void drm_draw_fill16(struct iosys_map *dmap, unsigned int dpitch,
		     unsigned int height, unsigned int width,
		     u16 color)
{
	unsigned int y, x;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			iosys_map_wr(dmap, y * dpitch + x * sizeof(u16), u16, color);
}
EXPORT_SYMBOL(drm_draw_fill16);

void drm_draw_fill24(struct iosys_map *dmap, unsigned int dpitch,
		     unsigned int height, unsigned int width,
		     u16 color)
{
	unsigned int y, x;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			unsigned int off = y * dpitch + x * 3;

			/* write blue-green-red to output in little endianness */
			iosys_map_wr(dmap, off, u8, (color & 0x000000FF) >> 0);
			iosys_map_wr(dmap, off + 1, u8, (color & 0x0000FF00) >> 8);
			iosys_map_wr(dmap, off + 2, u8, (color & 0x00FF0000) >> 16);
		}
	}
}
EXPORT_SYMBOL(drm_draw_fill24);

void drm_draw_fill32(struct iosys_map *dmap, unsigned int dpitch,
		     unsigned int height, unsigned int width,
		     u32 color)
{
	unsigned int y, x;

	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
			iosys_map_wr(dmap, y * dpitch + x * sizeof(u32), u32, color);
}
EXPORT_SYMBOL(drm_draw_fill32);
