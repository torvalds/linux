/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>

#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>

static void drm_fb_memcpy_lines(void *dst, unsigned int dst_pitch,
				void *src, unsigned int src_pitch,
				unsigned int linelength, unsigned int lines)
{
	int line;

	for (line = 0; line < lines; line++) {
		memcpy(dst, src, linelength);
		src += src_pitch;
		dst += dst_pitch;
	}
}

/**
 * drm_fb_memcpy - Copy clip buffer
 * @dst: Destination buffer
 * @vaddr: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * This function does not apply clipping on dst, i.e. the destination
 * is a small buffer containing the clip rect only.
 */
void drm_fb_memcpy(void *dst, void *vaddr, struct drm_framebuffer *fb,
		   struct drm_rect *clip)
{
	unsigned int cpp = drm_format_plane_cpp(fb->format->format, 0);
	unsigned int offset = (clip->y1 * fb->pitches[0]) + (clip->x1 * cpp);
	size_t len = (clip->x2 - clip->x1) * cpp;

	drm_fb_memcpy_lines(dst, len,
			    vaddr + offset, fb->pitches[0],
			    len, clip->y2 - clip->y1);
}
EXPORT_SYMBOL(drm_fb_memcpy);

/**
 * drm_fb_memcpy_dstclip - Copy clip buffer
 * @dst: Destination buffer
 * @vaddr: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * This function applies clipping on dst, i.e. the destination is a
 * full framebuffer but only the clip rect content is copied over.
 */
void drm_fb_memcpy_dstclip(void *dst, void *vaddr, struct drm_framebuffer *fb,
			   struct drm_rect *clip)
{
	unsigned int cpp = drm_format_plane_cpp(fb->format->format, 0);
	unsigned int offset = (clip->y1 * fb->pitches[0]) + (clip->x1 * cpp);
	size_t len = (clip->x2 - clip->x1) * cpp;

	drm_fb_memcpy_lines(dst + offset, fb->pitches[0],
			    vaddr + offset, fb->pitches[0],
			    len, clip->y2 - clip->y1);
}
EXPORT_SYMBOL(drm_fb_memcpy_dstclip);

/**
 * drm_fb_swab16 - Swap bytes into clip buffer
 * @dst: RGB565 destination buffer
 * @vaddr: RGB565 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 */
void drm_fb_swab16(u16 *dst, void *vaddr, struct drm_framebuffer *fb,
		   struct drm_rect *clip)
{
	size_t len = (clip->x2 - clip->x1) * sizeof(u16);
	unsigned int x, y;
	u16 *src, *buf;

	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	for (y = clip->y1; y < clip->y2; y++) {
		src = vaddr + (y * fb->pitches[0]);
		src += clip->x1;
		memcpy(buf, src, len);
		src = buf;
		for (x = clip->x1; x < clip->x2; x++)
			*dst++ = swab16(*src++);
	}

	kfree(buf);
}
EXPORT_SYMBOL(drm_fb_swab16);

static void drm_fb_xrgb8888_to_rgb565_lines(void *dst, unsigned int dst_pitch,
					    void *src, unsigned int src_pitch,
					    unsigned int src_linelength,
					    unsigned int lines,
					    bool swap)
{
	unsigned int linepixels = src_linelength / sizeof(u32);
	unsigned int x, y;
	u32 *sbuf;
	u16 *dbuf, val16;

	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 */
	sbuf = kmalloc(src_linelength, GFP_KERNEL);
	if (!sbuf)
		return;

	for (y = 0; y < lines; y++) {
		memcpy(sbuf, src, src_linelength);
		dbuf = dst;
		for (x = 0; x < linepixels; x++) {
			val16 = ((sbuf[x] & 0x00F80000) >> 8) |
				((sbuf[x] & 0x0000FC00) >> 5) |
				((sbuf[x] & 0x000000F8) >> 3);
			if (swap)
				*dbuf++ = swab16(val16);
			else
				*dbuf++ = val16;
		}
		src += src_pitch;
		dst += dst_pitch;
	}

	kfree(sbuf);
}

/**
 * drm_fb_xrgb8888_to_rgb565 - Convert XRGB8888 to RGB565 clip buffer
 * @dst: RGB565 destination buffer
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @swap: Swap bytes
 *
 * Drivers can use this function for RGB565 devices that don't natively
 * support XRGB8888.
 *
 * This function does not apply clipping on dst, i.e. the destination
 * is a small buffer containing the clip rect only.
 */
void drm_fb_xrgb8888_to_rgb565(void *dst, void *vaddr,
			       struct drm_framebuffer *fb,
			       struct drm_rect *clip, bool swap)
{
	unsigned int src_offset = (clip->y1 * fb->pitches[0])
		+ (clip->x1 * sizeof(u32));
	size_t src_len = (clip->x2 - clip->x1) * sizeof(u32);
	size_t dst_len = (clip->x2 - clip->x1) * sizeof(u16);

	drm_fb_xrgb8888_to_rgb565_lines(dst, dst_len,
					vaddr + src_offset, fb->pitches[0],
					src_len, clip->y2 - clip->y1,
					swap);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565);

/**
 * drm_fb_xrgb8888_to_rgb565_dstclip - Convert XRGB8888 to RGB565 clip buffer
 * @dst: RGB565 destination buffer
 * @dst_pitch: destination buffer pitch
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @swap: Swap bytes
 *
 * Drivers can use this function for RGB565 devices that don't natively
 * support XRGB8888.
 *
 * This function applies clipping on dst, i.e. the destination is a
 * full framebuffer but only the clip rect content is copied over.
 */
void drm_fb_xrgb8888_to_rgb565_dstclip(void *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip, bool swap)
{
	unsigned int src_offset = (clip->y1 * fb->pitches[0])
		+ (clip->x1 * sizeof(u32));
	unsigned int dst_offset = (clip->y1 * dst_pitch)
		+ (clip->x1 * sizeof(u16));
	size_t src_len = (clip->x2 - clip->x1) * sizeof(u32);

	drm_fb_xrgb8888_to_rgb565_lines(dst + dst_offset, dst_pitch,
					vaddr + src_offset, fb->pitches[0],
					src_len, clip->y2 - clip->y1,
					swap);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565_dstclip);

static void drm_fb_xrgb8888_to_rgb888_lines(void *dst, unsigned int dst_pitch,
					    void *src, unsigned int src_pitch,
					    unsigned int src_linelength,
					    unsigned int lines)
{
	unsigned int linepixels = src_linelength / 3;
	unsigned int x, y;
	u32 *sbuf;
	u8 *dbuf;

	sbuf = kmalloc(src_linelength, GFP_KERNEL);
	if (!sbuf)
		return;

	for (y = 0; y < lines; y++) {
		memcpy(sbuf, src, src_linelength);
		dbuf = dst;
		for (x = 0; x < linepixels; x++) {
			*dbuf++ = (sbuf[x] & 0x000000FF) >>  0;
			*dbuf++ = (sbuf[x] & 0x0000FF00) >>  8;
			*dbuf++ = (sbuf[x] & 0x00FF0000) >> 16;
		}
		src += src_pitch;
		dst += dst_pitch;
	}

	kfree(sbuf);
}

/**
 * drm_fb_xrgb8888_to_rgb888_dstclip - Convert XRGB8888 to RGB888 clip buffer
 * @dst: RGB565 destination buffer
 * @dst_pitch: destination buffer pitch
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @dstclip: Clip destination too.
 *
 * Drivers can use this function for RGB888 devices that don't natively
 * support XRGB8888.
 *
 * This function applies clipping on dst, i.e. the destination is a
 * full framebuffer but only the clip rect content is copied over.
 */
void drm_fb_xrgb8888_to_rgb888_dstclip(void *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip)
{
	unsigned int src_offset = (clip->y1 * fb->pitches[0])
		+ (clip->x1 * sizeof(u32));
	unsigned int dst_offset = (clip->y1 * dst_pitch)
		+ (clip->x1 * 3);
	size_t src_len = (clip->x2 - clip->x1) * sizeof(u32);

	drm_fb_xrgb8888_to_rgb888_lines(dst + dst_offset, dst_pitch,
					vaddr + src_offset, fb->pitches[0],
					src_len, clip->y2 - clip->y1);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb888_dstclip);

/**
 * drm_fb_xrgb8888_to_gray8 - Convert XRGB8888 to grayscale
 * @dst: 8-bit grayscale destination buffer
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * Drm doesn't have native monochrome or grayscale support.
 * Such drivers can announce the commonly supported XR24 format to userspace
 * and use this function to convert to the native format.
 *
 * Monochrome drivers will use the most significant bit,
 * where 1 means foreground color and 0 background color.
 *
 * ITU BT.601 is used for the RGB -> luma (brightness) conversion.
 */
void drm_fb_xrgb8888_to_gray8(u8 *dst, void *vaddr, struct drm_framebuffer *fb,
			       struct drm_rect *clip)
{
	unsigned int len = (clip->x2 - clip->x1) * sizeof(u32);
	unsigned int x, y;
	void *buf;
	u32 *src;

	if (WARN_ON(fb->format->format != DRM_FORMAT_XRGB8888))
		return;
	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	for (y = clip->y1; y < clip->y2; y++) {
		src = vaddr + (y * fb->pitches[0]);
		src += clip->x1;
		memcpy(buf, src, len);
		src = buf;
		for (x = clip->x1; x < clip->x2; x++) {
			u8 r = (*src & 0x00ff0000) >> 16;
			u8 g = (*src & 0x0000ff00) >> 8;
			u8 b =  *src & 0x000000ff;

			/* ITU BT.601: Y = 0.299 R + 0.587 G + 0.114 B */
			*dst++ = (3 * r + 6 * g + b) / 10;
			src++;
		}
	}

	kfree(buf);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_gray8);

