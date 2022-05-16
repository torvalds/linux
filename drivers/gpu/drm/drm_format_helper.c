// SPDX-License-Identifier: GPL-2.0 or MIT
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
#include <linux/io.h>

#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>

static unsigned int clip_offset(struct drm_rect *clip,
				unsigned int pitch, unsigned int cpp)
{
	return clip->y1 * pitch + clip->x1 * cpp;
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
	unsigned int cpp = fb->format->cpp[0];
	size_t len = (clip->x2 - clip->x1) * cpp;
	unsigned int y, lines = clip->y2 - clip->y1;

	vaddr += clip_offset(clip, fb->pitches[0], cpp);
	for (y = 0; y < lines; y++) {
		memcpy(dst, vaddr, len);
		vaddr += fb->pitches[0];
		dst += len;
	}
}
EXPORT_SYMBOL(drm_fb_memcpy);

/**
 * drm_fb_memcpy_dstclip - Copy clip buffer
 * @dst: Destination buffer (iomem)
 * @vaddr: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * This function applies clipping on dst, i.e. the destination is a
 * full (iomem) framebuffer but only the clip rect content is copied over.
 */
void drm_fb_memcpy_dstclip(void __iomem *dst, void *vaddr,
			   struct drm_framebuffer *fb,
			   struct drm_rect *clip)
{
	unsigned int cpp = fb->format->cpp[0];
	unsigned int offset = clip_offset(clip, fb->pitches[0], cpp);
	size_t len = (clip->x2 - clip->x1) * cpp;
	unsigned int y, lines = clip->y2 - clip->y1;

	vaddr += offset;
	dst += offset;
	for (y = 0; y < lines; y++) {
		memcpy_toio(dst, vaddr, len);
		vaddr += fb->pitches[0];
		dst += fb->pitches[0];
	}
}
EXPORT_SYMBOL(drm_fb_memcpy_dstclip);

/**
 * drm_fb_swab - Swap bytes into clip buffer
 * @dst: Destination buffer
 * @src: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @cached: Source buffer is mapped cached (eg. not write-combined)
 *
 * If @cached is false a temporary buffer is used to cache one pixel line at a
 * time to speed up slow uncached reads.
 *
 * This function does not apply clipping on dst, i.e. the destination
 * is a small buffer containing the clip rect only.
 */
void drm_fb_swab(void *dst, void *src, struct drm_framebuffer *fb,
		 struct drm_rect *clip, bool cached)
{
	u8 cpp = fb->format->cpp[0];
	size_t len = drm_rect_width(clip) * cpp;
	u16 *src16, *dst16 = dst;
	u32 *src32, *dst32 = dst;
	unsigned int x, y;
	void *buf = NULL;

	if (WARN_ON_ONCE(cpp != 2 && cpp != 4))
		return;

	if (!cached)
		buf = kmalloc(len, GFP_KERNEL);

	src += clip_offset(clip, fb->pitches[0], cpp);

	for (y = clip->y1; y < clip->y2; y++) {
		if (buf) {
			memcpy(buf, src, len);
			src16 = buf;
			src32 = buf;
		} else {
			src16 = src;
			src32 = src;
		}

		for (x = clip->x1; x < clip->x2; x++) {
			if (cpp == 4)
				*dst32++ = swab32(*src32++);
			else
				*dst16++ = swab16(*src16++);
		}

		src += fb->pitches[0];
	}

	kfree(buf);
}
EXPORT_SYMBOL(drm_fb_swab);

static void drm_fb_xrgb8888_to_rgb565_line(u16 *dbuf, u32 *sbuf,
					   unsigned int pixels,
					   bool swab)
{
	unsigned int x;
	u16 val16;

	for (x = 0; x < pixels; x++) {
		val16 = ((sbuf[x] & 0x00F80000) >> 8) |
			((sbuf[x] & 0x0000FC00) >> 5) |
			((sbuf[x] & 0x000000F8) >> 3);
		if (swab)
			dbuf[x] = swab16(val16);
		else
			dbuf[x] = val16;
	}
}

/**
 * drm_fb_xrgb8888_to_rgb565 - Convert XRGB8888 to RGB565 clip buffer
 * @dst: RGB565 destination buffer
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @swab: Swap bytes
 *
 * Drivers can use this function for RGB565 devices that don't natively
 * support XRGB8888.
 *
 * This function does not apply clipping on dst, i.e. the destination
 * is a small buffer containing the clip rect only.
 */
void drm_fb_xrgb8888_to_rgb565(void *dst, void *vaddr,
			       struct drm_framebuffer *fb,
			       struct drm_rect *clip, bool swab)
{
	size_t linepixels = clip->x2 - clip->x1;
	size_t src_len = linepixels * sizeof(u32);
	size_t dst_len = linepixels * sizeof(u16);
	unsigned y, lines = clip->y2 - clip->y1;
	void *sbuf;

	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 */
	sbuf = kmalloc(src_len, GFP_KERNEL);
	if (!sbuf)
		return;

	vaddr += clip_offset(clip, fb->pitches[0], sizeof(u32));
	for (y = 0; y < lines; y++) {
		memcpy(sbuf, vaddr, src_len);
		drm_fb_xrgb8888_to_rgb565_line(dst, sbuf, linepixels, swab);
		vaddr += fb->pitches[0];
		dst += dst_len;
	}

	kfree(sbuf);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565);

/**
 * drm_fb_xrgb8888_to_rgb565_dstclip - Convert XRGB8888 to RGB565 clip buffer
 * @dst: RGB565 destination buffer (iomem)
 * @dst_pitch: destination buffer pitch
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @swab: Swap bytes
 *
 * Drivers can use this function for RGB565 devices that don't natively
 * support XRGB8888.
 *
 * This function applies clipping on dst, i.e. the destination is a
 * full (iomem) framebuffer but only the clip rect content is copied over.
 */
void drm_fb_xrgb8888_to_rgb565_dstclip(void __iomem *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip, bool swab)
{
	size_t linepixels = clip->x2 - clip->x1;
	size_t dst_len = linepixels * sizeof(u16);
	unsigned y, lines = clip->y2 - clip->y1;
	void *dbuf;

	dbuf = kmalloc(dst_len, GFP_KERNEL);
	if (!dbuf)
		return;

	vaddr += clip_offset(clip, fb->pitches[0], sizeof(u32));
	dst += clip_offset(clip, dst_pitch, sizeof(u16));
	for (y = 0; y < lines; y++) {
		drm_fb_xrgb8888_to_rgb565_line(dbuf, vaddr, linepixels, swab);
		memcpy_toio(dst, dbuf, dst_len);
		vaddr += fb->pitches[0];
		dst += dst_len;
	}

	kfree(dbuf);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565_dstclip);

static void drm_fb_xrgb8888_to_rgb888_line(u8 *dbuf, u32 *sbuf,
					   unsigned int pixels)
{
	unsigned int x;

	for (x = 0; x < pixels; x++) {
		*dbuf++ = (sbuf[x] & 0x000000FF) >>  0;
		*dbuf++ = (sbuf[x] & 0x0000FF00) >>  8;
		*dbuf++ = (sbuf[x] & 0x00FF0000) >> 16;
	}
}

/**
 * drm_fb_xrgb8888_to_rgb888_dstclip - Convert XRGB8888 to RGB888 clip buffer
 * @dst: RGB565 destination buffer (iomem)
 * @dst_pitch: destination buffer pitch
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * Drivers can use this function for RGB888 devices that don't natively
 * support XRGB8888.
 *
 * This function applies clipping on dst, i.e. the destination is a
 * full (iomem) framebuffer but only the clip rect content is copied over.
 */
void drm_fb_xrgb8888_to_rgb888_dstclip(void __iomem *dst, unsigned int dst_pitch,
				       void *vaddr, struct drm_framebuffer *fb,
				       struct drm_rect *clip)
{
	size_t linepixels = clip->x2 - clip->x1;
	size_t dst_len = linepixels * 3;
	unsigned y, lines = clip->y2 - clip->y1;
	void *dbuf;

	dbuf = kmalloc(dst_len, GFP_KERNEL);
	if (!dbuf)
		return;

	vaddr += clip_offset(clip, fb->pitches[0], sizeof(u32));
	dst += clip_offset(clip, dst_pitch, sizeof(u16));
	for (y = 0; y < lines; y++) {
		drm_fb_xrgb8888_to_rgb888_line(dbuf, vaddr, linepixels);
		memcpy_toio(dst, dbuf, dst_len);
		vaddr += fb->pitches[0];
		dst += dst_len;
	}

	kfree(dbuf);
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

