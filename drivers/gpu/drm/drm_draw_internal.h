/* SPDX-License-Identifier: GPL-2.0 or MIT */
/*
 * Copyright (c) 2023 Red Hat.
 * Author: Jocelyn Falempe <jfalempe@redhat.com>
 */

#ifndef __DRM_DRAW_INTERNAL_H__
#define __DRM_DRAW_INTERNAL_H__

#include <linux/font.h>
#include <linux/types.h>

struct iosys_map;

/* check if the pixel at coord x,y is 1 (foreground) or 0 (background) */
static inline bool drm_draw_is_pixel_fg(const u8 *sbuf8, unsigned int spitch, int x, int y)
{
	return (sbuf8[(y * spitch) + x / 8] & (0x80 >> (x % 8))) != 0;
}

static inline const u8 *drm_draw_get_char_bitmap(const struct font_desc *font,
						 char c, size_t font_pitch)
{
	return font->data + (c * font->height) * font_pitch;
}

u32 drm_draw_color_from_xrgb8888(u32 color, u32 format);

void drm_draw_blit16(struct iosys_map *dmap, unsigned int dpitch,
		     const u8 *sbuf8, unsigned int spitch,
		     unsigned int height, unsigned int width,
		     unsigned int scale, u16 fg16);

void drm_draw_blit24(struct iosys_map *dmap, unsigned int dpitch,
		     const u8 *sbuf8, unsigned int spitch,
		     unsigned int height, unsigned int width,
		     unsigned int scale, u32 fg32);

void drm_draw_blit32(struct iosys_map *dmap, unsigned int dpitch,
		     const u8 *sbuf8, unsigned int spitch,
		     unsigned int height, unsigned int width,
		     unsigned int scale, u32 fg32);

void drm_draw_fill16(struct iosys_map *dmap, unsigned int dpitch,
		     unsigned int height, unsigned int width,
		     u16 color);

void drm_draw_fill24(struct iosys_map *dmap, unsigned int dpitch,
		     unsigned int height, unsigned int width,
		     u16 color);

void drm_draw_fill32(struct iosys_map *dmap, unsigned int dpitch,
		     unsigned int height, unsigned int width,
		     u32 color);

#endif /* __DRM_DRAW_INTERNAL_H__ */
