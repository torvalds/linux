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

#include <drm/drm_device.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>

static unsigned int clip_offset(const struct drm_rect *clip, unsigned int pitch, unsigned int cpp)
{
	return clip->y1 * pitch + clip->x1 * cpp;
}

/**
 * drm_fb_clip_offset - Returns the clipping rectangles byte-offset in a framebuffer
 * @pitch: Framebuffer line pitch in byte
 * @format: Framebuffer format
 * @clip: Clip rectangle
 *
 * Returns:
 * The byte offset of the clip rectangle's top-left corner within the framebuffer.
 */
unsigned int drm_fb_clip_offset(unsigned int pitch, const struct drm_format_info *format,
				const struct drm_rect *clip)
{
	return clip_offset(clip, pitch, format->cpp[0]);
}
EXPORT_SYMBOL(drm_fb_clip_offset);

/* TODO: Make this functon work with multi-plane formats. */
static int drm_fb_xfrm(void *dst, unsigned long dst_pitch, unsigned long dst_pixsize,
		       const void *vaddr, const struct drm_framebuffer *fb,
		       const struct drm_rect *clip, bool vaddr_cached_hint,
		       void (*xfrm_line)(void *dbuf, const void *sbuf, unsigned int npixels))
{
	unsigned long linepixels = drm_rect_width(clip);
	unsigned long lines = drm_rect_height(clip);
	size_t sbuf_len = linepixels * fb->format->cpp[0];
	void *stmp = NULL;
	unsigned long i;
	const void *sbuf;

	/*
	 * Some source buffers, such as CMA memory, use write-combine
	 * caching, so reads are uncached. Speed up access by fetching
	 * one line at a time.
	 */
	if (!vaddr_cached_hint) {
		stmp = kmalloc(sbuf_len, GFP_KERNEL);
		if (!stmp)
			return -ENOMEM;
	}

	if (!dst_pitch)
		dst_pitch = drm_rect_width(clip) * dst_pixsize;
	vaddr += clip_offset(clip, fb->pitches[0], fb->format->cpp[0]);

	for (i = 0; i < lines; ++i) {
		if (stmp)
			sbuf = memcpy(stmp, vaddr, sbuf_len);
		else
			sbuf = vaddr;
		xfrm_line(dst, sbuf, linepixels);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(stmp);

	return 0;
}

/* TODO: Make this functon work with multi-plane formats. */
static int drm_fb_xfrm_toio(void __iomem *dst, unsigned long dst_pitch, unsigned long dst_pixsize,
			    const void *vaddr, const struct drm_framebuffer *fb,
			    const struct drm_rect *clip, bool vaddr_cached_hint,
			    void (*xfrm_line)(void *dbuf, const void *sbuf, unsigned int npixels))
{
	unsigned long linepixels = drm_rect_width(clip);
	unsigned long lines = drm_rect_height(clip);
	size_t dbuf_len = linepixels * dst_pixsize;
	size_t stmp_off = round_up(dbuf_len, ARCH_KMALLOC_MINALIGN); /* for sbuf alignment */
	size_t sbuf_len = linepixels * fb->format->cpp[0];
	void *stmp = NULL;
	unsigned long i;
	const void *sbuf;
	void *dbuf;

	if (vaddr_cached_hint) {
		dbuf = kmalloc(dbuf_len, GFP_KERNEL);
	} else {
		dbuf = kmalloc(stmp_off + sbuf_len, GFP_KERNEL);
		stmp = dbuf + stmp_off;
	}
	if (!dbuf)
		return -ENOMEM;

	if (!dst_pitch)
		dst_pitch = linepixels * dst_pixsize;
	vaddr += clip_offset(clip, fb->pitches[0], fb->format->cpp[0]);

	for (i = 0; i < lines; ++i) {
		if (stmp)
			sbuf = memcpy(stmp, vaddr, sbuf_len);
		else
			sbuf = vaddr;
		xfrm_line(dbuf, sbuf, linepixels);
		memcpy_toio(dst, dbuf, dbuf_len);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(dbuf);

	return 0;
}

/**
 * drm_fb_memcpy - Copy clip buffer
 * @dst: Destination buffer
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @vaddr: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * This function does not apply clipping on dst, i.e. the destination
 * is at the top-left corner.
 */
void drm_fb_memcpy(void *dst, unsigned int dst_pitch, const void *vaddr,
		   const struct drm_framebuffer *fb, const struct drm_rect *clip)
{
	unsigned int cpp = fb->format->cpp[0];
	size_t len = (clip->x2 - clip->x1) * cpp;
	unsigned int y, lines = clip->y2 - clip->y1;

	if (!dst_pitch)
		dst_pitch = len;

	vaddr += clip_offset(clip, fb->pitches[0], cpp);
	for (y = 0; y < lines; y++) {
		memcpy(dst, vaddr, len);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}
}
EXPORT_SYMBOL(drm_fb_memcpy);

/**
 * drm_fb_memcpy_toio - Copy clip buffer
 * @dst: Destination buffer (iomem)
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @vaddr: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * This function does not apply clipping on dst, i.e. the destination
 * is at the top-left corner.
 */
void drm_fb_memcpy_toio(void __iomem *dst, unsigned int dst_pitch, const void *vaddr,
			const struct drm_framebuffer *fb, const struct drm_rect *clip)
{
	unsigned int cpp = fb->format->cpp[0];
	size_t len = (clip->x2 - clip->x1) * cpp;
	unsigned int y, lines = clip->y2 - clip->y1;

	if (!dst_pitch)
		dst_pitch = len;

	vaddr += clip_offset(clip, fb->pitches[0], cpp);
	for (y = 0; y < lines; y++) {
		memcpy_toio(dst, vaddr, len);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}
}
EXPORT_SYMBOL(drm_fb_memcpy_toio);

static void drm_fb_swab16_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u16 *dbuf16 = dbuf;
	const u16 *sbuf16 = sbuf;
	const u16 *send16 = sbuf16 + pixels;

	while (sbuf16 < send16)
		*dbuf16++ = swab16(*sbuf16++);
}

static void drm_fb_swab32_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u32 *dbuf32 = dbuf;
	const u32 *sbuf32 = sbuf;
	const u32 *send32 = sbuf32 + pixels;

	while (sbuf32 < send32)
		*dbuf32++ = swab32(*sbuf32++);
}

/**
 * drm_fb_swab - Swap bytes into clip buffer
 * @dst: Destination buffer
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @src: Source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @cached: Source buffer is mapped cached (eg. not write-combined)
 *
 * If @cached is false a temporary buffer is used to cache one pixel line at a
 * time to speed up slow uncached reads.
 *
 * This function does not apply clipping on dst, i.e. the destination
 * is at the top-left corner.
 */
void drm_fb_swab(void *dst, unsigned int dst_pitch, const void *src,
		 const struct drm_framebuffer *fb, const struct drm_rect *clip,
		 bool cached)
{
	u8 cpp = fb->format->cpp[0];

	switch (cpp) {
	case 4:
		drm_fb_xfrm(dst, dst_pitch, cpp, src, fb, clip, cached, drm_fb_swab32_line);
		break;
	case 2:
		drm_fb_xfrm(dst, dst_pitch, cpp, src, fb, clip, cached, drm_fb_swab16_line);
		break;
	default:
		drm_warn_once(fb->dev, "Format %p4cc has unsupported pixel size.\n",
			      &fb->format->format);
		break;
	}
}
EXPORT_SYMBOL(drm_fb_swab);

static void drm_fb_xrgb8888_to_rgb332_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		dbuf8[x] = ((pix & 0x00e00000) >> 16) |
			   ((pix & 0x0000e000) >> 11) |
			   ((pix & 0x000000c0) >> 6);
	}
}

/**
 * drm_fb_xrgb8888_to_rgb332 - Convert XRGB8888 to RGB332 clip buffer
 * @dst: RGB332 destination buffer
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @src: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * Drivers can use this function for RGB332 devices that don't natively support XRGB8888.
 */
void drm_fb_xrgb8888_to_rgb332(void *dst, unsigned int dst_pitch, const void *src,
			       const struct drm_framebuffer *fb, const struct drm_rect *clip)
{
	drm_fb_xfrm(dst, dst_pitch, 1, src, fb, clip, false, drm_fb_xrgb8888_to_rgb332_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb332);

static void drm_fb_xrgb8888_to_rgb565_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u16 *dbuf16 = dbuf;
	const u32 *sbuf32 = sbuf;
	unsigned int x;
	u16 val16;

	for (x = 0; x < pixels; x++) {
		val16 = ((sbuf32[x] & 0x00F80000) >> 8) |
			((sbuf32[x] & 0x0000FC00) >> 5) |
			((sbuf32[x] & 0x000000F8) >> 3);
		dbuf16[x] = val16;
	}
}

static void drm_fb_xrgb8888_to_rgb565_swab_line(void *dbuf, const void *sbuf,
						unsigned int pixels)
{
	u16 *dbuf16 = dbuf;
	const u32 *sbuf32 = sbuf;
	unsigned int x;
	u16 val16;

	for (x = 0; x < pixels; x++) {
		val16 = ((sbuf32[x] & 0x00F80000) >> 8) |
			((sbuf32[x] & 0x0000FC00) >> 5) |
			((sbuf32[x] & 0x000000F8) >> 3);
		dbuf16[x] = swab16(val16);
	}
}

/**
 * drm_fb_xrgb8888_to_rgb565 - Convert XRGB8888 to RGB565 clip buffer
 * @dst: RGB565 destination buffer
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @swab: Swap bytes
 *
 * Drivers can use this function for RGB565 devices that don't natively
 * support XRGB8888.
 */
void drm_fb_xrgb8888_to_rgb565(void *dst, unsigned int dst_pitch, const void *vaddr,
			       const struct drm_framebuffer *fb, const struct drm_rect *clip,
			       bool swab)
{
	if (swab)
		drm_fb_xfrm(dst, dst_pitch, 2, vaddr, fb, clip, false,
			    drm_fb_xrgb8888_to_rgb565_swab_line);
	else
		drm_fb_xfrm(dst, dst_pitch, 2, vaddr, fb, clip, false,
			    drm_fb_xrgb8888_to_rgb565_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565);

/**
 * drm_fb_xrgb8888_to_rgb565_toio - Convert XRGB8888 to RGB565 clip buffer
 * @dst: RGB565 destination buffer (iomem)
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @swab: Swap bytes
 *
 * Drivers can use this function for RGB565 devices that don't natively
 * support XRGB8888.
 */
void drm_fb_xrgb8888_to_rgb565_toio(void __iomem *dst, unsigned int dst_pitch,
				    const void *vaddr, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip, bool swab)
{
	if (swab)
		drm_fb_xfrm_toio(dst, dst_pitch, 2, vaddr, fb, clip, false,
				 drm_fb_xrgb8888_to_rgb565_swab_line);
	else
		drm_fb_xfrm_toio(dst, dst_pitch, 2, vaddr, fb, clip, false,
				 drm_fb_xrgb8888_to_rgb565_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565_toio);

static void drm_fb_xrgb8888_to_rgb888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const u32 *sbuf32 = sbuf;
	unsigned int x;

	for (x = 0; x < pixels; x++) {
		*dbuf8++ = (sbuf32[x] & 0x000000FF) >>  0;
		*dbuf8++ = (sbuf32[x] & 0x0000FF00) >>  8;
		*dbuf8++ = (sbuf32[x] & 0x00FF0000) >> 16;
	}
}

/**
 * drm_fb_xrgb8888_to_rgb888 - Convert XRGB8888 to RGB888 clip buffer
 * @dst: RGB888 destination buffer
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @src: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * Drivers can use this function for RGB888 devices that don't natively
 * support XRGB8888.
 */
void drm_fb_xrgb8888_to_rgb888(void *dst, unsigned int dst_pitch, const void *src,
			       const struct drm_framebuffer *fb, const struct drm_rect *clip)
{
	drm_fb_xfrm(dst, dst_pitch, 3, src, fb, clip, false, drm_fb_xrgb8888_to_rgb888_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb888);

/**
 * drm_fb_xrgb8888_to_rgb888_toio - Convert XRGB8888 to RGB888 clip buffer
 * @dst: RGB565 destination buffer (iomem)
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * Drivers can use this function for RGB888 devices that don't natively
 * support XRGB8888.
 */
void drm_fb_xrgb8888_to_rgb888_toio(void __iomem *dst, unsigned int dst_pitch,
				    const void *vaddr, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip)
{
	drm_fb_xfrm_toio(dst, dst_pitch, 3, vaddr, fb, clip, false,
			 drm_fb_xrgb8888_to_rgb888_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb888_toio);

static void drm_fb_rgb565_to_xrgb8888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u32 *dbuf32 = dbuf;
	const u16 *sbuf16 = sbuf;
	unsigned int x;

	for (x = 0; x < pixels; x++, ++sbuf16, ++dbuf32) {
		u32 val32 = ((*sbuf16 & 0xf800) << 8) |
			    ((*sbuf16 & 0x07e0) << 5) |
			    ((*sbuf16 & 0x001f) << 3);
		*dbuf32 = 0xff000000 | val32 |
			  ((val32 >> 3) & 0x00070007) |
			  ((val32 >> 2) & 0x00000300);
	}
}

static void drm_fb_rgb565_to_xrgb8888_toio(void __iomem *dst, unsigned int dst_pitch,
					   const void *vaddr, const struct drm_framebuffer *fb,
					   const struct drm_rect *clip)
{
	drm_fb_xfrm_toio(dst, dst_pitch, 4, vaddr, fb, clip, false,
			 drm_fb_rgb565_to_xrgb8888_line);
}

static void drm_fb_rgb888_to_xrgb8888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u32 *dbuf32 = dbuf;
	const u8 *sbuf8 = sbuf;
	unsigned int x;

	for (x = 0; x < pixels; x++) {
		u8 r = *sbuf8++;
		u8 g = *sbuf8++;
		u8 b = *sbuf8++;
		*dbuf32++ = 0xff000000 | (r << 16) | (g << 8) | b;
	}
}

static void drm_fb_rgb888_to_xrgb8888_toio(void __iomem *dst, unsigned int dst_pitch,
					   const void *vaddr, const struct drm_framebuffer *fb,
					   const struct drm_rect *clip)
{
	drm_fb_xfrm_toio(dst, dst_pitch, 4, vaddr, fb, clip, false,
			 drm_fb_rgb888_to_xrgb8888_line);
}

static void drm_fb_xrgb8888_to_xrgb2101010_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u32 *dbuf32 = dbuf;
	const u32 *sbuf32 = sbuf;
	unsigned int x;
	u32 val32;

	for (x = 0; x < pixels; x++) {
		val32 = ((sbuf32[x] & 0x000000FF) << 2) |
			((sbuf32[x] & 0x0000FF00) << 4) |
			((sbuf32[x] & 0x00FF0000) << 6);
		*dbuf32++ = val32 | ((val32 >> 8) & 0x00300C03);
	}
}

/**
 * drm_fb_xrgb8888_to_xrgb2101010_toio - Convert XRGB8888 to XRGB2101010 clip
 * buffer
 * @dst: XRGB2101010 destination buffer (iomem)
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * Drivers can use this function for XRGB2101010 devices that don't natively
 * support XRGB8888.
 */
void drm_fb_xrgb8888_to_xrgb2101010_toio(void __iomem *dst,
					 unsigned int dst_pitch, const void *vaddr,
					 const struct drm_framebuffer *fb,
					 const struct drm_rect *clip)
{
	drm_fb_xfrm_toio(dst, dst_pitch, 4, vaddr, fb, clip, false,
			 drm_fb_xrgb8888_to_xrgb2101010_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_xrgb2101010_toio);

static void drm_fb_xrgb8888_to_gray8_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const u32 *sbuf32 = sbuf;
	unsigned int x;

	for (x = 0; x < pixels; x++) {
		u8 r = (*sbuf32 & 0x00ff0000) >> 16;
		u8 g = (*sbuf32 & 0x0000ff00) >> 8;
		u8 b =  *sbuf32 & 0x000000ff;

		/* ITU BT.601: Y = 0.299 R + 0.587 G + 0.114 B */
		*dbuf8++ = (3 * r + 6 * g + b) / 10;
		sbuf32++;
	}
}

/**
 * drm_fb_xrgb8888_to_gray8 - Convert XRGB8888 to grayscale
 * @dst: 8-bit grayscale destination buffer
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
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
void drm_fb_xrgb8888_to_gray8(void *dst, unsigned int dst_pitch, const void *vaddr,
			      const struct drm_framebuffer *fb, const struct drm_rect *clip)
{
	drm_fb_xfrm(dst, dst_pitch, 1, vaddr, fb, clip, false, drm_fb_xrgb8888_to_gray8_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_gray8);

/**
 * drm_fb_blit_toio - Copy parts of a framebuffer to display memory
 * @dst:	The display memory to copy to
 * @dst_pitch:	Number of bytes between two consecutive scanlines within dst
 * @dst_format:	FOURCC code of the display's color format
 * @vmap:	The framebuffer memory to copy from
 * @fb:		The framebuffer to copy from
 * @clip:	Clip rectangle area to copy
 *
 * This function copies parts of a framebuffer to display memory. If the
 * formats of the display and the framebuffer mismatch, the blit function
 * will attempt to convert between them.
 *
 * Returns:
 * 0 on success, or
 * -EINVAL if the color-format conversion failed, or
 * a negative error code otherwise.
 */
int drm_fb_blit_toio(void __iomem *dst, unsigned int dst_pitch, uint32_t dst_format,
		     const void *vmap, const struct drm_framebuffer *fb,
		     const struct drm_rect *clip)
{
	uint32_t fb_format = fb->format->format;

	/* treat alpha channel like filler bits */
	if (fb_format == DRM_FORMAT_ARGB8888)
		fb_format = DRM_FORMAT_XRGB8888;
	if (dst_format == DRM_FORMAT_ARGB8888)
		dst_format = DRM_FORMAT_XRGB8888;
	if (fb_format == DRM_FORMAT_ARGB2101010)
		fb_format = DRM_FORMAT_XRGB2101010;
	if (dst_format == DRM_FORMAT_ARGB2101010)
		dst_format = DRM_FORMAT_XRGB2101010;

	if (dst_format == fb_format) {
		drm_fb_memcpy_toio(dst, dst_pitch, vmap, fb, clip);
		return 0;

	} else if (dst_format == DRM_FORMAT_RGB565) {
		if (fb_format == DRM_FORMAT_XRGB8888) {
			drm_fb_xrgb8888_to_rgb565_toio(dst, dst_pitch, vmap, fb, clip, false);
			return 0;
		}
	} else if (dst_format == DRM_FORMAT_RGB888) {
		if (fb_format == DRM_FORMAT_XRGB8888) {
			drm_fb_xrgb8888_to_rgb888_toio(dst, dst_pitch, vmap, fb, clip);
			return 0;
		}
	} else if (dst_format == DRM_FORMAT_XRGB8888) {
		if (fb_format == DRM_FORMAT_RGB888) {
			drm_fb_rgb888_to_xrgb8888_toio(dst, dst_pitch, vmap, fb, clip);
			return 0;
		} else if (fb_format == DRM_FORMAT_RGB565) {
			drm_fb_rgb565_to_xrgb8888_toio(dst, dst_pitch, vmap, fb, clip);
			return 0;
		}
	} else if (dst_format == DRM_FORMAT_XRGB2101010) {
		if (fb_format == DRM_FORMAT_XRGB8888) {
			drm_fb_xrgb8888_to_xrgb2101010_toio(dst, dst_pitch, vmap, fb, clip);
			return 0;
		}
	}

	drm_warn_once(fb->dev, "No conversion helper from %p4cc to %p4cc found.\n",
		      &fb_format, &dst_format);

	return -EINVAL;
}
EXPORT_SYMBOL(drm_fb_blit_toio);


static void drm_fb_gray8_to_mono_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const u8 *sbuf8 = sbuf;

	while (pixels) {
		unsigned int i, bits = min(pixels, 8U);
		u8 byte = 0;

		for (i = 0; i < bits; i++, pixels--) {
			if (*sbuf8++ >= 128)
				byte |= BIT(i);
		}
		*dbuf8++ = byte;
	}
}

/**
 * drm_fb_xrgb8888_to_mono - Convert XRGB8888 to monochrome
 * @dst: monochrome destination buffer (0=black, 1=white)
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @vaddr: XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * DRM doesn't have native monochrome support.
 * Such drivers can announce the commonly supported XR24 format to userspace
 * and use this function to convert to the native format.
 *
 * This function uses drm_fb_xrgb8888_to_gray8() to convert to grayscale and
 * then the result is converted from grayscale to monochrome.
 *
 * The first pixel (upper left corner of the clip rectangle) will be converted
 * and copied to the first bit (LSB) in the first byte of the monochrome
 * destination buffer.
 * If the caller requires that the first pixel in a byte must be located at an
 * x-coordinate that is a multiple of 8, then the caller must take care itself
 * of supplying a suitable clip rectangle.
 */
void drm_fb_xrgb8888_to_mono(void *dst, unsigned int dst_pitch, const void *vaddr,
			     const struct drm_framebuffer *fb, const struct drm_rect *clip)
{
	unsigned int linepixels = drm_rect_width(clip);
	unsigned int lines = drm_rect_height(clip);
	unsigned int cpp = fb->format->cpp[0];
	unsigned int len_src32 = linepixels * cpp;
	struct drm_device *dev = fb->dev;
	unsigned int y;
	u8 *mono = dst, *gray8;
	u32 *src32;

	if (drm_WARN_ON(dev, fb->format->format != DRM_FORMAT_XRGB8888))
		return;

	/*
	 * The mono destination buffer contains 1 bit per pixel
	 */
	if (!dst_pitch)
		dst_pitch = DIV_ROUND_UP(linepixels, 8);

	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 *
	 * Also, format conversion from XR24 to monochrome are done
	 * line-by-line but are converted to 8-bit grayscale as an
	 * intermediate step.
	 *
	 * Allocate a buffer to be used for both copying from the cma
	 * memory and to store the intermediate grayscale line pixels.
	 */
	src32 = kmalloc(len_src32 + linepixels, GFP_KERNEL);
	if (!src32)
		return;

	gray8 = (u8 *)src32 + len_src32;

	vaddr += clip_offset(clip, fb->pitches[0], cpp);
	for (y = 0; y < lines; y++) {
		src32 = memcpy(src32, vaddr, len_src32);
		drm_fb_xrgb8888_to_gray8_line(gray8, src32, linepixels);
		drm_fb_gray8_to_mono_line(mono, gray8, linepixels);
		vaddr += fb->pitches[0];
		mono += dst_pitch;
	}

	kfree(src32);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_mono);
