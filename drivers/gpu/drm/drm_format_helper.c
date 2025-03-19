// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/iosys-map.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>

/**
 * drm_format_conv_state_init - Initialize format-conversion state
 * @state: The state to initialize
 *
 * Clears all fields in struct drm_format_conv_state. The state will
 * be empty with no preallocated resources.
 */
void drm_format_conv_state_init(struct drm_format_conv_state *state)
{
	state->tmp.mem = NULL;
	state->tmp.size = 0;
	state->tmp.preallocated = false;
}
EXPORT_SYMBOL(drm_format_conv_state_init);

/**
 * drm_format_conv_state_copy - Copy format-conversion state
 * @state: Destination state
 * @old_state: Source state
 *
 * Copies format-conversion state from @old_state to @state; except for
 * temporary storage.
 */
void drm_format_conv_state_copy(struct drm_format_conv_state *state,
				const struct drm_format_conv_state *old_state)
{
	/*
	 * So far, there's only temporary storage here, which we don't
	 * duplicate. Just clear the fields.
	 */
	state->tmp.mem = NULL;
	state->tmp.size = 0;
	state->tmp.preallocated = false;
}
EXPORT_SYMBOL(drm_format_conv_state_copy);

/**
 * drm_format_conv_state_reserve - Allocates storage for format conversion
 * @state: The format-conversion state
 * @new_size: The minimum allocation size
 * @flags: Flags for kmalloc()
 *
 * Allocates at least @new_size bytes and returns a pointer to the memory
 * range. After calling this function, previously returned memory blocks
 * are invalid. It's best to collect all memory requirements of a format
 * conversion and call this function once to allocate the range.
 *
 * Returns:
 * A pointer to the allocated memory range, or NULL otherwise.
 */
void *drm_format_conv_state_reserve(struct drm_format_conv_state *state,
				    size_t new_size, gfp_t flags)
{
	void *mem;

	if (new_size <= state->tmp.size)
		goto out;
	else if (state->tmp.preallocated)
		return NULL;

	mem = krealloc(state->tmp.mem, new_size, flags);
	if (!mem)
		return NULL;

	state->tmp.mem = mem;
	state->tmp.size = new_size;

out:
	return state->tmp.mem;
}
EXPORT_SYMBOL(drm_format_conv_state_reserve);

/**
 * drm_format_conv_state_release - Releases an format-conversion storage
 * @state: The format-conversion state
 *
 * Releases the memory range references by the format-conversion state.
 * After this call, all pointers to the memory are invalid. Prefer
 * drm_format_conv_state_init() for cleaning up and unloading a driver.
 */
void drm_format_conv_state_release(struct drm_format_conv_state *state)
{
	if (state->tmp.preallocated)
		return;

	kfree(state->tmp.mem);
	state->tmp.mem = NULL;
	state->tmp.size = 0;
}
EXPORT_SYMBOL(drm_format_conv_state_release);

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

/* TODO: Make this function work with multi-plane formats. */
static int __drm_fb_xfrm(void *dst, unsigned long dst_pitch, unsigned long dst_pixsize,
			 const void *vaddr, const struct drm_framebuffer *fb,
			 const struct drm_rect *clip, bool vaddr_cached_hint,
			 struct drm_format_conv_state *state,
			 void (*xfrm_line)(void *dbuf, const void *sbuf, unsigned int npixels))
{
	unsigned long linepixels = drm_rect_width(clip);
	unsigned long lines = drm_rect_height(clip);
	size_t sbuf_len = linepixels * fb->format->cpp[0];
	void *stmp = NULL;
	unsigned long i;
	const void *sbuf;

	/*
	 * Some source buffers, such as DMA memory, use write-combine
	 * caching, so reads are uncached. Speed up access by fetching
	 * one line at a time.
	 */
	if (!vaddr_cached_hint) {
		stmp = drm_format_conv_state_reserve(state, sbuf_len, GFP_KERNEL);
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

	return 0;
}

/* TODO: Make this function work with multi-plane formats. */
static int __drm_fb_xfrm_toio(void __iomem *dst, unsigned long dst_pitch, unsigned long dst_pixsize,
			      const void *vaddr, const struct drm_framebuffer *fb,
			      const struct drm_rect *clip, bool vaddr_cached_hint,
			      struct drm_format_conv_state *state,
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
		dbuf = drm_format_conv_state_reserve(state, dbuf_len, GFP_KERNEL);
	} else {
		dbuf = drm_format_conv_state_reserve(state, stmp_off + sbuf_len, GFP_KERNEL);
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

	return 0;
}

/* TODO: Make this function work with multi-plane formats. */
static int drm_fb_xfrm(struct iosys_map *dst,
		       const unsigned int *dst_pitch, const u8 *dst_pixsize,
		       const struct iosys_map *src, const struct drm_framebuffer *fb,
		       const struct drm_rect *clip, bool vaddr_cached_hint,
		       struct drm_format_conv_state *state,
		       void (*xfrm_line)(void *dbuf, const void *sbuf, unsigned int npixels))
{
	static const unsigned int default_dst_pitch[DRM_FORMAT_MAX_PLANES] = {
		0, 0, 0, 0
	};

	if (!dst_pitch)
		dst_pitch = default_dst_pitch;

	/* TODO: handle src in I/O memory here */
	if (dst[0].is_iomem)
		return __drm_fb_xfrm_toio(dst[0].vaddr_iomem, dst_pitch[0], dst_pixsize[0],
					  src[0].vaddr, fb, clip, vaddr_cached_hint, state,
					  xfrm_line);
	else
		return __drm_fb_xfrm(dst[0].vaddr, dst_pitch[0], dst_pixsize[0],
				     src[0].vaddr, fb, clip, vaddr_cached_hint, state,
				     xfrm_line);
}

/**
 * drm_fb_memcpy - Copy clip buffer
 * @dst: Array of destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 *
 * This function copies parts of a framebuffer to display memory. Destination and
 * framebuffer formats must match. No conversion takes place. The parameters @dst,
 * @dst_pitch and @src refer to arrays. Each array must have at least as many entries
 * as there are planes in @fb's format. Each entry stores the value for the format's
 * respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 */
void drm_fb_memcpy(struct iosys_map *dst, const unsigned int *dst_pitch,
		   const struct iosys_map *src, const struct drm_framebuffer *fb,
		   const struct drm_rect *clip)
{
	static const unsigned int default_dst_pitch[DRM_FORMAT_MAX_PLANES] = {
		0, 0, 0, 0
	};

	const struct drm_format_info *format = fb->format;
	unsigned int i, y, lines = drm_rect_height(clip);

	if (!dst_pitch)
		dst_pitch = default_dst_pitch;

	for (i = 0; i < format->num_planes; ++i) {
		unsigned int bpp_i = drm_format_info_bpp(format, i);
		unsigned int cpp_i = DIV_ROUND_UP(bpp_i, 8);
		size_t len_i = DIV_ROUND_UP(drm_rect_width(clip) * bpp_i, 8);
		unsigned int dst_pitch_i = dst_pitch[i];
		struct iosys_map dst_i = dst[i];
		struct iosys_map src_i = src[i];

		if (!dst_pitch_i)
			dst_pitch_i = len_i;

		iosys_map_incr(&src_i, clip_offset(clip, fb->pitches[i], cpp_i));
		for (y = 0; y < lines; y++) {
			/* TODO: handle src_i in I/O memory here */
			iosys_map_memcpy_to(&dst_i, 0, src_i.vaddr, len_i);
			iosys_map_incr(&src_i, fb->pitches[i]);
			iosys_map_incr(&dst_i, dst_pitch_i);
		}
	}
}
EXPORT_SYMBOL(drm_fb_memcpy);

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
 * @dst: Array of destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @cached: Source buffer is mapped cached (eg. not write-combined)
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and swaps per-pixel
 * bytes during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index. If @cached is
 * false a temporary buffer is used to cache one pixel line at a time to speed up
 * slow uncached reads.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 */
void drm_fb_swab(struct iosys_map *dst, const unsigned int *dst_pitch,
		 const struct iosys_map *src, const struct drm_framebuffer *fb,
		 const struct drm_rect *clip, bool cached,
		 struct drm_format_conv_state *state)
{
	const struct drm_format_info *format = fb->format;
	u8 cpp = DIV_ROUND_UP(drm_format_info_bpp(format, 0), 8);
	void (*swab_line)(void *dbuf, const void *sbuf, unsigned int npixels);

	switch (cpp) {
	case 4:
		swab_line = drm_fb_swab32_line;
		break;
	case 2:
		swab_line = drm_fb_swab16_line;
		break;
	default:
		drm_warn_once(fb->dev, "Format %p4cc has unsupported pixel size.\n",
			      &format->format);
		return;
	}

	drm_fb_xfrm(dst, dst_pitch, &cpp, src, fb, clip, cached, state, swab_line);
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
 * @dst: Array of RGB332 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for RGB332 devices that don't support XRGB8888 natively.
 */
void drm_fb_xrgb8888_to_rgb332(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		1,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_rgb332_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb332);

static void drm_fb_xrgb8888_to_rgb565_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le16 *dbuf16 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u16 val16;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		val16 = ((pix & 0x00F80000) >> 8) |
			((pix & 0x0000FC00) >> 5) |
			((pix & 0x000000F8) >> 3);
		dbuf16[x] = cpu_to_le16(val16);
	}
}

/* TODO: implement this helper as conversion to RGB565|BIG_ENDIAN */
static void drm_fb_xrgb8888_to_rgb565_swab_line(void *dbuf, const void *sbuf,
						unsigned int pixels)
{
	__le16 *dbuf16 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u16 val16;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		val16 = ((pix & 0x00F80000) >> 8) |
			((pix & 0x0000FC00) >> 5) |
			((pix & 0x000000F8) >> 3);
		dbuf16[x] = cpu_to_le16(swab16(val16));
	}
}

/**
 * drm_fb_xrgb8888_to_rgb565 - Convert XRGB8888 to RGB565 clip buffer
 * @dst: Array of RGB565 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 * @swab: Swap bytes
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for RGB565 devices that don't support XRGB8888 natively.
 */
void drm_fb_xrgb8888_to_rgb565(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state,
			       bool swab)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		2,
	};

	void (*xfrm_line)(void *dbuf, const void *sbuf, unsigned int npixels);

	if (swab)
		xfrm_line = drm_fb_xrgb8888_to_rgb565_swab_line;
	else
		xfrm_line = drm_fb_xrgb8888_to_rgb565_line;

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state, xfrm_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565);

static void drm_fb_xrgb8888_to_xrgb1555_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le16 *dbuf16 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u16 val16;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		val16 = ((pix & 0x00f80000) >> 9) |
			((pix & 0x0000f800) >> 6) |
			((pix & 0x000000f8) >> 3);
		dbuf16[x] = cpu_to_le16(val16);
	}
}

/**
 * drm_fb_xrgb8888_to_xrgb1555 - Convert XRGB8888 to XRGB1555 clip buffer
 * @dst: Array of XRGB1555 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts
 * the color format during the process. The parameters @dst, @dst_pitch and
 * @src refer to arrays. Each array must have at least as many entries as
 * there are planes in @fb's format. Each entry stores the value for the
 * format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for XRGB1555 devices that don't support
 * XRGB8888 natively.
 */
void drm_fb_xrgb8888_to_xrgb1555(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		2,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_xrgb1555_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_xrgb1555);

static void drm_fb_xrgb8888_to_argb1555_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le16 *dbuf16 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u16 val16;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		val16 = BIT(15) | /* set alpha bit */
			((pix & 0x00f80000) >> 9) |
			((pix & 0x0000f800) >> 6) |
			((pix & 0x000000f8) >> 3);
		dbuf16[x] = cpu_to_le16(val16);
	}
}

/**
 * drm_fb_xrgb8888_to_argb1555 - Convert XRGB8888 to ARGB1555 clip buffer
 * @dst: Array of ARGB1555 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts
 * the color format during the process. The parameters @dst, @dst_pitch and
 * @src refer to arrays. Each array must have at least as many entries as
 * there are planes in @fb's format. Each entry stores the value for the
 * format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for ARGB1555 devices that don't support
 * XRGB8888 natively. It sets an opaque alpha channel as part of the conversion.
 */
void drm_fb_xrgb8888_to_argb1555(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		2,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_argb1555_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_argb1555);

static void drm_fb_xrgb8888_to_rgba5551_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le16 *dbuf16 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u16 val16;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		val16 = ((pix & 0x00f80000) >> 8) |
			((pix & 0x0000f800) >> 5) |
			((pix & 0x000000f8) >> 2) |
			BIT(0); /* set alpha bit */
		dbuf16[x] = cpu_to_le16(val16);
	}
}

/**
 * drm_fb_xrgb8888_to_rgba5551 - Convert XRGB8888 to RGBA5551 clip buffer
 * @dst: Array of RGBA5551 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts
 * the color format during the process. The parameters @dst, @dst_pitch and
 * @src refer to arrays. Each array must have at least as many entries as
 * there are planes in @fb's format. Each entry stores the value for the
 * format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for RGBA5551 devices that don't support
 * XRGB8888 natively. It sets an opaque alpha channel as part of the conversion.
 */
void drm_fb_xrgb8888_to_rgba5551(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		2,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_rgba5551_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgba5551);

static void drm_fb_xrgb8888_to_rgb888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		/* write blue-green-red to output in little endianness */
		*dbuf8++ = (pix & 0x000000FF) >>  0;
		*dbuf8++ = (pix & 0x0000FF00) >>  8;
		*dbuf8++ = (pix & 0x00FF0000) >> 16;
	}
}

/**
 * drm_fb_xrgb8888_to_rgb888 - Convert XRGB8888 to RGB888 clip buffer
 * @dst: Array of RGB888 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for RGB888 devices that don't natively
 * support XRGB8888.
 */
void drm_fb_xrgb8888_to_rgb888(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		3,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_rgb888_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb888);

static void drm_fb_xrgb8888_to_bgr888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		/* write red-green-blue to output in little endianness */
		*dbuf8++ = (pix & 0x00ff0000) >> 16;
		*dbuf8++ = (pix & 0x0000ff00) >> 8;
		*dbuf8++ = (pix & 0x000000ff) >> 0;
	}
}

/**
 * drm_fb_xrgb8888_to_bgr888 - Convert XRGB8888 to BGR888 clip buffer
 * @dst: Array of BGR888 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for BGR888 devices that don't natively
 * support XRGB8888.
 */
void drm_fb_xrgb8888_to_bgr888(struct iosys_map *dst, const unsigned int *dst_pitch,
			       const struct iosys_map *src, const struct drm_framebuffer *fb,
			       const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		3,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_bgr888_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_bgr888);

static void drm_fb_xrgb8888_to_argb8888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le32 *dbuf32 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		pix |= GENMASK(31, 24); /* fill alpha bits */
		dbuf32[x] = cpu_to_le32(pix);
	}
}

/**
 * drm_fb_xrgb8888_to_argb8888 - Convert XRGB8888 to ARGB8888 clip buffer
 * @dst: Array of ARGB8888 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. The parameters @dst, @dst_pitch and @src refer
 * to arrays. Each array must have at least as many entries as there are planes in
 * @fb's format. Each entry stores the value for the format's respective color plane
 * at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for ARGB8888 devices that don't support XRGB8888
 * natively. It sets an opaque alpha channel as part of the conversion.
 */
void drm_fb_xrgb8888_to_argb8888(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		4,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_argb8888_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_argb8888);

static void drm_fb_xrgb8888_to_abgr8888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le32 *dbuf32 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		pix = ((pix & 0x00ff0000) >> 16) <<  0 |
		      ((pix & 0x0000ff00) >>  8) <<  8 |
		      ((pix & 0x000000ff) >>  0) << 16 |
		      GENMASK(31, 24); /* fill alpha bits */
		*dbuf32++ = cpu_to_le32(pix);
	}
}

static void drm_fb_xrgb8888_to_abgr8888(struct iosys_map *dst, const unsigned int *dst_pitch,
					const struct iosys_map *src,
					const struct drm_framebuffer *fb,
					const struct drm_rect *clip,
					struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		4,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_abgr8888_line);
}

static void drm_fb_xrgb8888_to_xbgr8888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le32 *dbuf32 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		pix = ((pix & 0x00ff0000) >> 16) <<  0 |
		      ((pix & 0x0000ff00) >>  8) <<  8 |
		      ((pix & 0x000000ff) >>  0) << 16 |
		      ((pix & 0xff000000) >> 24) << 24;
		*dbuf32++ = cpu_to_le32(pix);
	}
}

static void drm_fb_xrgb8888_to_xbgr8888(struct iosys_map *dst, const unsigned int *dst_pitch,
					const struct iosys_map *src,
					const struct drm_framebuffer *fb,
					const struct drm_rect *clip,
					struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		4,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_xbgr8888_line);
}

static void drm_fb_xrgb8888_to_xrgb2101010_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le32 *dbuf32 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 val32;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		val32 = ((pix & 0x000000FF) << 2) |
			((pix & 0x0000FF00) << 4) |
			((pix & 0x00FF0000) << 6);
		pix = val32 | ((val32 >> 8) & 0x00300C03);
		*dbuf32++ = cpu_to_le32(pix);
	}
}

/**
 * drm_fb_xrgb8888_to_xrgb2101010 - Convert XRGB8888 to XRGB2101010 clip buffer
 * @dst: Array of XRGB2101010 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for XRGB2101010 devices that don't support XRGB8888
 * natively.
 */
void drm_fb_xrgb8888_to_xrgb2101010(struct iosys_map *dst, const unsigned int *dst_pitch,
				    const struct iosys_map *src, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip,
				    struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		4,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_xrgb2101010_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_xrgb2101010);

static void drm_fb_xrgb8888_to_argb2101010_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	__le32 *dbuf32 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 val32;
	u32 pix;

	for (x = 0; x < pixels; x++) {
		pix = le32_to_cpu(sbuf32[x]);
		val32 = ((pix & 0x000000ff) << 2) |
			((pix & 0x0000ff00) << 4) |
			((pix & 0x00ff0000) << 6);
		pix = GENMASK(31, 30) | /* set alpha bits */
		      val32 | ((val32 >> 8) & 0x00300c03);
		*dbuf32++ = cpu_to_le32(pix);
	}
}

/**
 * drm_fb_xrgb8888_to_argb2101010 - Convert XRGB8888 to ARGB2101010 clip buffer
 * @dst: Array of ARGB2101010 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts
 * the color format during the process. The parameters @dst, @dst_pitch and
 * @src refer to arrays. Each array must have at least as many entries as
 * there are planes in @fb's format. Each entry stores the value for the
 * format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for ARGB2101010 devices that don't support XRGB8888
 * natively.
 */
void drm_fb_xrgb8888_to_argb2101010(struct iosys_map *dst, const unsigned int *dst_pitch,
				    const struct iosys_map *src, const struct drm_framebuffer *fb,
				    const struct drm_rect *clip,
				    struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		4,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_argb2101010_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_argb2101010);

static void drm_fb_xrgb8888_to_gray8_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const __le32 *sbuf32 = sbuf;
	unsigned int x;

	for (x = 0; x < pixels; x++) {
		u32 pix = le32_to_cpu(sbuf32[x]);
		u8 r = (pix & 0x00ff0000) >> 16;
		u8 g = (pix & 0x0000ff00) >> 8;
		u8 b =  pix & 0x000000ff;

		/* ITU BT.601: Y = 0.299 R + 0.587 G + 0.114 B */
		*dbuf8++ = (3 * r + 6 * g + b) / 10;
	}
}

/**
 * drm_fb_xrgb8888_to_gray8 - Convert XRGB8888 to grayscale
 * @dst: Array of 8-bit grayscale destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * DRM doesn't have native monochrome or grayscale support. Drivers can use this
 * function for grayscale devices that don't support XRGB8888 natively.Such
 * drivers can announce the commonly supported XR24 format to userspace and use
 * this function to convert to the native format. Monochrome drivers will use the
 * most significant bit, where 1 means foreground color and 0 background color.
 * ITU BT.601 is being used for the RGB -> luma (brightness) conversion.
 */
void drm_fb_xrgb8888_to_gray8(struct iosys_map *dst, const unsigned int *dst_pitch,
			      const struct iosys_map *src, const struct drm_framebuffer *fb,
			      const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		1,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_gray8_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_gray8);

static void drm_fb_argb8888_to_argb4444_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	unsigned int pixels2 = pixels & ~GENMASK_ULL(0, 0);
	__le32 *dbuf32 = dbuf;
	__le16 *dbuf16 = dbuf + pixels2 * sizeof(*dbuf16);
	const __le32 *sbuf32 = sbuf;
	unsigned int x;
	u32 val32;
	u16 val16;
	u32 pix[2];

	for (x = 0; x < pixels2; x += 2, ++dbuf32) {
		pix[0] = le32_to_cpu(sbuf32[x]);
		pix[1] = le32_to_cpu(sbuf32[x + 1]);
		val32 = ((pix[0] & 0xf0000000) >> 16) |
			((pix[0] & 0x00f00000) >> 12) |
			((pix[0] & 0x0000f000) >> 8) |
			((pix[0] & 0x000000f0) >> 4) |
			((pix[1] & 0xf0000000) >> 0) |
			((pix[1] & 0x00f00000) << 4) |
			((pix[1] & 0x0000f000) << 8) |
			((pix[1] & 0x000000f0) << 12);
		*dbuf32 = cpu_to_le32(val32);
	}
	for (; x < pixels; x++) {
		pix[0] = le32_to_cpu(sbuf32[x]);
		val16 = ((pix[0] & 0xf0000000) >> 16) |
			((pix[0] & 0x00f00000) >> 12) |
			((pix[0] & 0x0000f000) >> 8) |
			((pix[0] & 0x000000f0) >> 4);
		dbuf16[x] = cpu_to_le16(val16);
	}
}

/**
 * drm_fb_argb8888_to_argb4444 - Convert ARGB8888 to ARGB4444 clip buffer
 * @dst: Array of ARGB4444 destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of ARGB8888 source buffer
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts
 * the color format during the process. The parameters @dst, @dst_pitch and
 * @src refer to arrays. Each array must have at least as many entries as
 * there are planes in @fb's format. Each entry stores the value for the
 * format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Drivers can use this function for ARGB4444 devices that don't support
 * ARGB8888 natively.
 */
void drm_fb_argb8888_to_argb4444(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		2,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_argb8888_to_argb4444_line);
}
EXPORT_SYMBOL(drm_fb_argb8888_to_argb4444);

/**
 * drm_fb_blit - Copy parts of a framebuffer to display memory
 * @dst:	Array of display-memory addresses to copy to
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @dst_format:	FOURCC code of the display's color format
 * @src:	The framebuffer memory to copy from
 * @fb:		The framebuffer to copy from
 * @clip:	Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory. If the
 * formats of the display and the framebuffer mismatch, the blit function
 * will attempt to convert between them during the process. The parameters @dst,
 * @dst_pitch and @src refer to arrays. Each array must have at least as many
 * entries as there are planes in @dst_format's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner).
 *
 * Returns:
 * 0 on success, or
 * -EINVAL if the color-format conversion failed, or
 * a negative error code otherwise.
 */
int drm_fb_blit(struct iosys_map *dst, const unsigned int *dst_pitch, uint32_t dst_format,
		const struct iosys_map *src, const struct drm_framebuffer *fb,
		const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	uint32_t fb_format = fb->format->format;

	if (fb_format == dst_format) {
		drm_fb_memcpy(dst, dst_pitch, src, fb, clip);
		return 0;
	} else if (fb_format == (dst_format | DRM_FORMAT_BIG_ENDIAN)) {
		drm_fb_swab(dst, dst_pitch, src, fb, clip, false, state);
		return 0;
	} else if (fb_format == (dst_format & ~DRM_FORMAT_BIG_ENDIAN)) {
		drm_fb_swab(dst, dst_pitch, src, fb, clip, false, state);
		return 0;
	} else if (fb_format == DRM_FORMAT_XRGB8888) {
		if (dst_format == DRM_FORMAT_RGB565) {
			drm_fb_xrgb8888_to_rgb565(dst, dst_pitch, src, fb, clip, state, false);
			return 0;
		} else if (dst_format == DRM_FORMAT_XRGB1555) {
			drm_fb_xrgb8888_to_xrgb1555(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_ARGB1555) {
			drm_fb_xrgb8888_to_argb1555(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_RGBA5551) {
			drm_fb_xrgb8888_to_rgba5551(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_RGB888) {
			drm_fb_xrgb8888_to_rgb888(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_BGR888) {
			drm_fb_xrgb8888_to_bgr888(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_ARGB8888) {
			drm_fb_xrgb8888_to_argb8888(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_XBGR8888) {
			drm_fb_xrgb8888_to_xbgr8888(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_ABGR8888) {
			drm_fb_xrgb8888_to_abgr8888(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_XRGB2101010) {
			drm_fb_xrgb8888_to_xrgb2101010(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_ARGB2101010) {
			drm_fb_xrgb8888_to_argb2101010(dst, dst_pitch, src, fb, clip, state);
			return 0;
		} else if (dst_format == DRM_FORMAT_BGRX8888) {
			drm_fb_swab(dst, dst_pitch, src, fb, clip, false, state);
			return 0;
		}
	}

	drm_warn_once(fb->dev, "No conversion helper from %p4cc to %p4cc found.\n",
		      &fb_format, &dst_format);

	return -EINVAL;
}
EXPORT_SYMBOL(drm_fb_blit);

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
 * @dst: Array of monochrome destination buffers (0=black, 1=white)
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffers
 * @fb: DRM framebuffer
 * @clip: Clip rectangle area to copy
 * @state: Transform and conversion state
 *
 * This function copies parts of a framebuffer to display memory and converts the
 * color format during the process. Destination and framebuffer formats must match. The
 * parameters @dst, @dst_pitch and @src refer to arrays. Each array must have at
 * least as many entries as there are planes in @fb's format. Each entry stores the
 * value for the format's respective color plane at the same index.
 *
 * This function does not apply clipping on @dst (i.e. the destination is at the
 * top-left corner). The first pixel (upper left corner of the clip rectangle) will
 * be converted and copied to the first bit (LSB) in the first byte of the monochrome
 * destination buffer. If the caller requires that the first pixel in a byte must
 * be located at an x-coordinate that is a multiple of 8, then the caller must take
 * care itself of supplying a suitable clip rectangle.
 *
 * DRM doesn't have native monochrome support. Drivers can use this function for
 * monochrome devices that don't support XRGB8888 natively. Such drivers can
 * announce the commonly supported XR24 format to userspace and use this function
 * to convert to the native format.
 *
 * This function uses drm_fb_xrgb8888_to_gray8() to convert to grayscale and
 * then the result is converted from grayscale to monochrome.
 */
void drm_fb_xrgb8888_to_mono(struct iosys_map *dst, const unsigned int *dst_pitch,
			     const struct iosys_map *src, const struct drm_framebuffer *fb,
			     const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const unsigned int default_dst_pitch[DRM_FORMAT_MAX_PLANES] = {
		0, 0, 0, 0
	};
	unsigned int linepixels = drm_rect_width(clip);
	unsigned int lines = drm_rect_height(clip);
	unsigned int cpp = fb->format->cpp[0];
	unsigned int len_src32 = linepixels * cpp;
	struct drm_device *dev = fb->dev;
	void *vaddr = src[0].vaddr;
	unsigned int dst_pitch_0;
	unsigned int y;
	u8 *mono = dst[0].vaddr, *gray8;
	u32 *src32;

	if (drm_WARN_ON(dev, fb->format->format != DRM_FORMAT_XRGB8888))
		return;

	if (!dst_pitch)
		dst_pitch = default_dst_pitch;
	dst_pitch_0 = dst_pitch[0];

	/*
	 * The mono destination buffer contains 1 bit per pixel
	 */
	if (!dst_pitch_0)
		dst_pitch_0 = DIV_ROUND_UP(linepixels, 8);

	/*
	 * The dma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 *
	 * Also, format conversion from XR24 to monochrome are done
	 * line-by-line but are converted to 8-bit grayscale as an
	 * intermediate step.
	 *
	 * Allocate a buffer to be used for both copying from the cma
	 * memory and to store the intermediate grayscale line pixels.
	 */
	src32 = drm_format_conv_state_reserve(state, len_src32 + linepixels, GFP_KERNEL);
	if (!src32)
		return;

	gray8 = (u8 *)src32 + len_src32;

	vaddr += clip_offset(clip, fb->pitches[0], cpp);
	for (y = 0; y < lines; y++) {
		src32 = memcpy(src32, vaddr, len_src32);
		drm_fb_xrgb8888_to_gray8_line(gray8, src32, linepixels);
		drm_fb_gray8_to_mono_line(mono, gray8, linepixels);
		vaddr += fb->pitches[0];
		mono += dst_pitch_0;
	}
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_mono);

static uint32_t drm_fb_nonalpha_fourcc(uint32_t fourcc)
{
	/* only handle formats with depth != 0 and alpha channel */
	switch (fourcc) {
	case DRM_FORMAT_ARGB1555:
		return DRM_FORMAT_XRGB1555;
	case DRM_FORMAT_ABGR1555:
		return DRM_FORMAT_XBGR1555;
	case DRM_FORMAT_RGBA5551:
		return DRM_FORMAT_RGBX5551;
	case DRM_FORMAT_BGRA5551:
		return DRM_FORMAT_BGRX5551;
	case DRM_FORMAT_ARGB8888:
		return DRM_FORMAT_XRGB8888;
	case DRM_FORMAT_ABGR8888:
		return DRM_FORMAT_XBGR8888;
	case DRM_FORMAT_RGBA8888:
		return DRM_FORMAT_RGBX8888;
	case DRM_FORMAT_BGRA8888:
		return DRM_FORMAT_BGRX8888;
	case DRM_FORMAT_ARGB2101010:
		return DRM_FORMAT_XRGB2101010;
	case DRM_FORMAT_ABGR2101010:
		return DRM_FORMAT_XBGR2101010;
	case DRM_FORMAT_RGBA1010102:
		return DRM_FORMAT_RGBX1010102;
	case DRM_FORMAT_BGRA1010102:
		return DRM_FORMAT_BGRX1010102;
	}

	return fourcc;
}

static bool is_listed_fourcc(const uint32_t *fourccs, size_t nfourccs, uint32_t fourcc)
{
	const uint32_t *fourccs_end = fourccs + nfourccs;

	while (fourccs < fourccs_end) {
		if (*fourccs == fourcc)
			return true;
		++fourccs;
	}
	return false;
}

/**
 * drm_fb_build_fourcc_list - Filters a list of supported color formats against
 *                            the device's native formats
 * @dev: DRM device
 * @native_fourccs: 4CC codes of natively supported color formats
 * @native_nfourccs: The number of entries in @native_fourccs
 * @fourccs_out: Returns 4CC codes of supported color formats
 * @nfourccs_out: The number of available entries in @fourccs_out
 *
 * This function create a list of supported color format from natively
 * supported formats and additional emulated formats.
 * At a minimum, most userspace programs expect at least support for
 * XRGB8888 on the primary plane. Devices that have to emulate the
 * format, and possibly others, can use drm_fb_build_fourcc_list() to
 * create a list of supported color formats. The returned list can
 * be handed over to drm_universal_plane_init() et al. Native formats
 * will go before emulated formats. Native formats with alpha channel
 * will be replaced by such without, as primary planes usually don't
 * support alpha. Other heuristics might be applied
 * to optimize the order. Formats near the beginning of the list are
 * usually preferred over formats near the end of the list.
 *
 * Returns:
 * The number of color-formats 4CC codes returned in @fourccs_out.
 */
size_t drm_fb_build_fourcc_list(struct drm_device *dev,
				const u32 *native_fourccs, size_t native_nfourccs,
				u32 *fourccs_out, size_t nfourccs_out)
{
	/*
	 * XRGB8888 is the default fallback format for most of userspace
	 * and it's currently the only format that should be emulated for
	 * the primary plane. Only if there's ever another default fallback,
	 * it should be added here.
	 */
	static const uint32_t extra_fourccs[] = {
		DRM_FORMAT_XRGB8888,
	};
	static const size_t extra_nfourccs = ARRAY_SIZE(extra_fourccs);

	u32 *fourccs = fourccs_out;
	const u32 *fourccs_end = fourccs_out + nfourccs_out;
	size_t i;

	/*
	 * The device's native formats go first.
	 */

	for (i = 0; i < native_nfourccs; ++i) {
		/*
		 * Several DTs, boot loaders and firmware report native
		 * alpha formats that are non-alpha formats instead. So
		 * replace alpha formats by non-alpha formats.
		 */
		u32 fourcc = drm_fb_nonalpha_fourcc(native_fourccs[i]);

		if (is_listed_fourcc(fourccs_out, fourccs - fourccs_out, fourcc)) {
			continue; /* skip duplicate entries */
		} else if (fourccs == fourccs_end) {
			drm_warn(dev, "Ignoring native format %p4cc\n", &fourcc);
			continue; /* end of available output buffer */
		}

		drm_dbg_kms(dev, "adding native format %p4cc\n", &fourcc);

		*fourccs = fourcc;
		++fourccs;
	}

	/*
	 * The extra formats, emulated by the driver, go second.
	 */

	for (i = 0; (i < extra_nfourccs) && (fourccs < fourccs_end); ++i) {
		u32 fourcc = extra_fourccs[i];

		if (is_listed_fourcc(fourccs_out, fourccs - fourccs_out, fourcc)) {
			continue; /* skip duplicate and native entries */
		} else if (fourccs == fourccs_end) {
			drm_warn(dev, "Ignoring emulated format %p4cc\n", &fourcc);
			continue; /* end of available output buffer */
		}

		drm_dbg_kms(dev, "adding emulated format %p4cc\n", &fourcc);

		*fourccs = fourcc;
		++fourccs;
	}

	return fourccs - fourccs_out;
}
EXPORT_SYMBOL(drm_fb_build_fourcc_list);
