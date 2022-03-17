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
	size_t len = drm_rect_width(clip) * cpp;
	const u16 *src16;
	const u32 *src32;
	u16 *dst16;
	u32 *dst32;
	unsigned int x, y;
	void *buf = NULL;

	if (WARN_ON_ONCE(cpp != 2 && cpp != 4))
		return;

	if (!dst_pitch)
		dst_pitch = len;

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

		dst16 = dst;
		dst32 = dst;

		for (x = clip->x1; x < clip->x2; x++) {
			if (cpp == 4)
				*dst32++ = swab32(*src32++);
			else
				*dst16++ = swab16(*src16++);
		}

		src += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(buf);
}
EXPORT_SYMBOL(drm_fb_swab);

static void drm_fb_xrgb8888_to_rgb332_line(u8 *dbuf, const __le32 *sbuf, unsigned int pixels)
{
	unsigned int x;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf[x]);
		dbuf[x] = ((pix & 0x00e00000) >> 16) |
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
	size_t width = drm_rect_width(clip);
	size_t src_len = width * sizeof(u32);
	unsigned int y;
	void *sbuf;

	if (!dst_pitch)
		dst_pitch = width;

	/* Use a buffer to speed up access on buffers with uncached read mapping (i.e. WC) */
	sbuf = kmalloc(src_len, GFP_KERNEL);
	if (!sbuf)
		return;

	src += clip_offset(clip, fb->pitches[0], sizeof(u32));
	for (y = 0; y < drm_rect_height(clip); y++) {
		memcpy(sbuf, src, src_len);
		drm_fb_xrgb8888_to_rgb332_line(dst, sbuf, width);
		src += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(sbuf);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb332);

static void drm_fb_xrgb8888_to_rgb565_line(u16 *dbuf, const u32 *sbuf,
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
	size_t linepixels = clip->x2 - clip->x1;
	size_t src_len = linepixels * sizeof(u32);
	size_t dst_len = linepixels * sizeof(u16);
	unsigned y, lines = clip->y2 - clip->y1;
	void *sbuf;

	if (!dst_pitch)
		dst_pitch = dst_len;

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
		dst += dst_pitch;
	}

	kfree(sbuf);
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
	size_t linepixels = clip->x2 - clip->x1;
	size_t dst_len = linepixels * sizeof(u16);
	unsigned y, lines = clip->y2 - clip->y1;
	void *dbuf;

	if (!dst_pitch)
		dst_pitch = dst_len;

	dbuf = kmalloc(dst_len, GFP_KERNEL);
	if (!dbuf)
		return;

	vaddr += clip_offset(clip, fb->pitches[0], sizeof(u32));
	for (y = 0; y < lines; y++) {
		drm_fb_xrgb8888_to_rgb565_line(dbuf, vaddr, linepixels, swab);
		memcpy_toio(dst, dbuf, dst_len);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(dbuf);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565_toio);

static void drm_fb_xrgb8888_to_rgb888_line(u8 *dbuf, const u32 *sbuf,
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
	size_t width = drm_rect_width(clip);
	size_t src_len = width * sizeof(u32);
	unsigned int y;
	void *sbuf;

	if (!dst_pitch)
		dst_pitch = width * 3;

	/* Use a buffer to speed up access on buffers with uncached read mapping (i.e. WC) */
	sbuf = kmalloc(src_len, GFP_KERNEL);
	if (!sbuf)
		return;

	src += clip_offset(clip, fb->pitches[0], sizeof(u32));
	for (y = 0; y < drm_rect_height(clip); y++) {
		memcpy(sbuf, src, src_len);
		drm_fb_xrgb8888_to_rgb888_line(dst, sbuf, width);
		src += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(sbuf);
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
	size_t linepixels = clip->x2 - clip->x1;
	size_t dst_len = linepixels * 3;
	unsigned y, lines = clip->y2 - clip->y1;
	void *dbuf;

	if (!dst_pitch)
		dst_pitch = dst_len;

	dbuf = kmalloc(dst_len, GFP_KERNEL);
	if (!dbuf)
		return;

	vaddr += clip_offset(clip, fb->pitches[0], sizeof(u32));
	for (y = 0; y < lines; y++) {
		drm_fb_xrgb8888_to_rgb888_line(dbuf, vaddr, linepixels);
		memcpy_toio(dst, dbuf, dst_len);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(dbuf);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb888_toio);

static void drm_fb_xrgb8888_to_xrgb2101010_line(u32 *dbuf, const u32 *sbuf,
						unsigned int pixels)
{
	unsigned int x;
	u32 val32;

	for (x = 0; x < pixels; x++) {
		val32 = ((sbuf[x] & 0x000000FF) << 2) |
			((sbuf[x] & 0x0000FF00) << 4) |
			((sbuf[x] & 0x00FF0000) << 6);
		*dbuf++ = val32 | ((val32 >> 8) & 0x00300C03);
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
	size_t linepixels = clip->x2 - clip->x1;
	size_t dst_len = linepixels * sizeof(u32);
	unsigned int y, lines = clip->y2 - clip->y1;
	void *dbuf;

	if (!dst_pitch)
		dst_pitch = dst_len;

	dbuf = kmalloc(dst_len, GFP_KERNEL);
	if (!dbuf)
		return;

	vaddr += clip_offset(clip, fb->pitches[0], sizeof(u32));
	for (y = 0; y < lines; y++) {
		drm_fb_xrgb8888_to_xrgb2101010_line(dbuf, vaddr, linepixels);
		memcpy_toio(dst, dbuf, dst_len);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(dbuf);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_xrgb2101010_toio);

static void drm_fb_xrgb8888_to_gray8_line(u8 *dst, const u32 *src, unsigned int pixels)
{
	unsigned int x;

	for (x = 0; x < pixels; x++) {
		u8 r = (*src & 0x00ff0000) >> 16;
		u8 g = (*src & 0x0000ff00) >> 8;
		u8 b =  *src & 0x000000ff;

		/* ITU BT.601: Y = 0.299 R + 0.587 G + 0.114 B */
		*dst++ = (3 * r + 6 * g + b) / 10;
		src++;
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
	unsigned int linepixels = clip->x2 - clip->x1;
	unsigned int len = linepixels * sizeof(u32);
	unsigned int y;
	void *buf;
	u8 *dst8;
	u32 *src32;

	if (WARN_ON(fb->format->format != DRM_FORMAT_XRGB8888))
		return;

	if (!dst_pitch)
		dst_pitch = drm_rect_width(clip);

	/*
	 * The cma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return;

	vaddr += clip_offset(clip, fb->pitches[0], sizeof(u32));
	for (y = clip->y1; y < clip->y2; y++) {
		dst8 = dst;
		src32 = memcpy(buf, vaddr, len);
		drm_fb_xrgb8888_to_gray8_line(dst8, src32, linepixels);
		vaddr += fb->pitches[0];
		dst += dst_pitch;
	}

	kfree(buf);
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
	} else if (dst_format == DRM_FORMAT_XRGB2101010) {
		if (fb_format == DRM_FORMAT_XRGB8888) {
			drm_fb_xrgb8888_to_xrgb2101010_toio(dst, dst_pitch, vmap, fb, clip);
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL(drm_fb_blit_toio);


static void drm_fb_gray8_to_mono_line(u8 *dst, const u8 *src, unsigned int pixels)
{
	while (pixels) {
		unsigned int i, bits = min(pixels, 8U);
		u8 byte = 0;

		for (i = 0; i < bits; i++, pixels--) {
			if (*src++ >= 128)
				byte |= BIT(i);
		}
		*dst++ = byte;
	}
}

/**
 * drm_fb_xrgb8888_to_mono - Convert XRGB8888 to monochrome
 * @dst: monochrome destination buffer (0=black, 1=white)
 * @dst_pitch: Number of bytes between two consecutive scanlines within dst
 * @src: XRGB8888 source buffer
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
