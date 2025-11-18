// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright (C) 2016 Noralf Trønnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/export.h>
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

#include "drm_format_internal.h"

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

#define ALIGN_DOWN_PIXELS(end, n, a) \
	((end) - ((n) & ((a) - 1)))

static __always_inline void drm_fb_xfrm_line_32to8(void *dbuf, const void *sbuf,
						   unsigned int pixels,
						   u32 (*xfrm_pixel)(u32))
{
	__le32 *dbuf32 = dbuf;
	u8 *dbuf8;
	const __le32 *sbuf32 = sbuf;
	const __le32 *send32 = sbuf32 + pixels;

	/* write 4 pixels at once */
	while (sbuf32 < ALIGN_DOWN_PIXELS(send32, pixels, 4)) {
		u32 pix[4] = {
			le32_to_cpup(sbuf32),
			le32_to_cpup(sbuf32 + 1),
			le32_to_cpup(sbuf32 + 2),
			le32_to_cpup(sbuf32 + 3),
		};
		/* write output bytes in reverse order for little endianness */
		u32 val32 = xfrm_pixel(pix[0]) |
			   (xfrm_pixel(pix[1]) << 8) |
			   (xfrm_pixel(pix[2]) << 16) |
			   (xfrm_pixel(pix[3]) << 24);
		*dbuf32++ = cpu_to_le32(val32);
		sbuf32 += ARRAY_SIZE(pix);
	}

	/* write trailing pixels */
	dbuf8 = (u8 __force *)dbuf32;
	while (sbuf32 < send32)
		*dbuf8++ = xfrm_pixel(le32_to_cpup(sbuf32++));
}

static __always_inline void drm_fb_xfrm_line_32to16(void *dbuf, const void *sbuf,
						    unsigned int pixels,
						    u32 (*xfrm_pixel)(u32))
{
	__le64 *dbuf64 = dbuf;
	__le32 *dbuf32;
	__le16 *dbuf16;
	const __le32 *sbuf32 = sbuf;
	const __le32 *send32 = sbuf32 + pixels;

#if defined(CONFIG_64BIT)
	/* write 4 pixels at once */
	while (sbuf32 < ALIGN_DOWN_PIXELS(send32, pixels, 4)) {
		u32 pix[4] = {
			le32_to_cpup(sbuf32),
			le32_to_cpup(sbuf32 + 1),
			le32_to_cpup(sbuf32 + 2),
			le32_to_cpup(sbuf32 + 3),
		};
		/* write output bytes in reverse order for little endianness */
		u64 val64 = ((u64)xfrm_pixel(pix[0])) |
			    ((u64)xfrm_pixel(pix[1]) << 16) |
			    ((u64)xfrm_pixel(pix[2]) << 32) |
			    ((u64)xfrm_pixel(pix[3]) << 48);
		*dbuf64++ = cpu_to_le64(val64);
		sbuf32 += ARRAY_SIZE(pix);
	}
#endif

	/* write 2 pixels at once */
	dbuf32 = (__le32 __force *)dbuf64;
	while (sbuf32 < ALIGN_DOWN_PIXELS(send32, pixels, 2)) {
		u32 pix[2] = {
			le32_to_cpup(sbuf32),
			le32_to_cpup(sbuf32 + 1),
		};
		/* write output bytes in reverse order for little endianness */
		u32 val32 = xfrm_pixel(pix[0]) |
			   (xfrm_pixel(pix[1]) << 16);
		*dbuf32++ = cpu_to_le32(val32);
		sbuf32 += ARRAY_SIZE(pix);
	}

	/* write trailing pixel */
	dbuf16 = (__le16 __force *)dbuf32;
	while (sbuf32 < send32)
		*dbuf16++ = cpu_to_le16(xfrm_pixel(le32_to_cpup(sbuf32++)));
}

static __always_inline void drm_fb_xfrm_line_32to24(void *dbuf, const void *sbuf,
						    unsigned int pixels,
						    u32 (*xfrm_pixel)(u32))
{
	__le32 *dbuf32 = dbuf;
	u8 *dbuf8;
	const __le32 *sbuf32 = sbuf;
	const __le32 *send32 = sbuf32 + pixels;

	/* write pixels in chunks of 4 */
	while (sbuf32 < ALIGN_DOWN_PIXELS(send32, pixels, 4)) {
		u32 val24[4] = {
			xfrm_pixel(le32_to_cpup(sbuf32)),
			xfrm_pixel(le32_to_cpup(sbuf32 + 1)),
			xfrm_pixel(le32_to_cpup(sbuf32 + 2)),
			xfrm_pixel(le32_to_cpup(sbuf32 + 3)),
		};
		u32 out32[3] = {
			/* write output bytes in reverse order for little endianness */
			((val24[0] & 0x000000ff)) |
			((val24[0] & 0x0000ff00)) |
			((val24[0] & 0x00ff0000)) |
			((val24[1] & 0x000000ff) << 24),
			((val24[1] & 0x0000ff00) >> 8) |
			((val24[1] & 0x00ff0000) >> 8) |
			((val24[2] & 0x000000ff) << 16) |
			((val24[2] & 0x0000ff00) << 16),
			((val24[2] & 0x00ff0000) >> 16) |
			((val24[3] & 0x000000ff) << 8) |
			((val24[3] & 0x0000ff00) << 8) |
			((val24[3] & 0x00ff0000) << 8),
		};

		*dbuf32++ = cpu_to_le32(out32[0]);
		*dbuf32++ = cpu_to_le32(out32[1]);
		*dbuf32++ = cpu_to_le32(out32[2]);
		sbuf32 += ARRAY_SIZE(val24);
	}

	/* write trailing pixel */
	dbuf8 = (u8 __force *)dbuf32;
	while (sbuf32 < send32) {
		u32 val24 = xfrm_pixel(le32_to_cpup(sbuf32++));
		/* write output in reverse order for little endianness */
		*dbuf8++ = (val24 & 0x000000ff);
		*dbuf8++ = (val24 & 0x0000ff00) >>  8;
		*dbuf8++ = (val24 & 0x00ff0000) >> 16;
	}
}

static __always_inline void drm_fb_xfrm_line_32to32(void *dbuf, const void *sbuf,
						    unsigned int pixels,
						    u32 (*xfrm_pixel)(u32))
{
	__le32 *dbuf32 = dbuf;
	const __le32 *sbuf32 = sbuf;
	const __le32 *send32 = sbuf32 + pixels;

	while (sbuf32 < send32)
		*dbuf32++ = cpu_to_le32(xfrm_pixel(le32_to_cpup(sbuf32++)));
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
	drm_fb_xfrm_line_32to8(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_rgb332);
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
	drm_fb_xfrm_line_32to16(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_rgb565);
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
			       const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		2,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_rgb565_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565);

static void drm_fb_xrgb8888_to_rgb565be_line(void *dbuf, const void *sbuf,
					     unsigned int pixels)
{
	drm_fb_xfrm_line_32to16(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_rgb565be);
}

/**
 * drm_fb_xrgb8888_to_rgb565be - Convert XRGB8888 to RGB565|DRM_FORMAT_BIG_ENDIAN clip buffer
 * @dst: Array of RGB565BE destination buffers
 * @dst_pitch: Array of numbers of bytes between the start of two consecutive scanlines
 *             within @dst; can be NULL if scanlines are stored next to each other.
 * @src: Array of XRGB8888 source buffer
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
 * Drivers can use this function for RGB565BE devices that don't support XRGB8888 natively.
 */
void drm_fb_xrgb8888_to_rgb565be(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src, const struct drm_framebuffer *fb,
				 const struct drm_rect *clip, struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		2,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_rgb565be_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_rgb565be);

static void drm_fb_xrgb8888_to_xrgb1555_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	drm_fb_xfrm_line_32to16(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_xrgb1555);
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
	drm_fb_xfrm_line_32to16(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_argb1555);
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
	drm_fb_xfrm_line_32to16(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_rgba5551);
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
	drm_fb_xfrm_line_32to24(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_rgb888);
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
	drm_fb_xfrm_line_32to24(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_bgr888);
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
	drm_fb_xfrm_line_32to32(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_argb8888);
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
	drm_fb_xfrm_line_32to32(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_abgr8888);
}

/**
 * drm_fb_xrgb8888_to_abgr8888 - Convert XRGB8888 to ABGR8888 clip buffer
 * @dst: Array of ABGR8888 destination buffers
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
 * Drivers can use this function for ABGR8888 devices that don't support XRGB8888
 * natively. It sets an opaque alpha channel as part of the conversion.
 */
void drm_fb_xrgb8888_to_abgr8888(struct iosys_map *dst, const unsigned int *dst_pitch,
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
EXPORT_SYMBOL(drm_fb_xrgb8888_to_abgr8888);

static void drm_fb_xrgb8888_to_xbgr8888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	drm_fb_xfrm_line_32to32(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_xbgr8888);
}

/**
 * drm_fb_xrgb8888_to_xbgr8888 - Convert XRGB8888 to XBGR8888 clip buffer
 * @dst: Array of XBGR8888 destination buffers
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
 * Drivers can use this function for XBGR8888 devices that don't support XRGB8888
 * natively.
 */
void drm_fb_xrgb8888_to_xbgr8888(struct iosys_map *dst, const unsigned int *dst_pitch,
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
EXPORT_SYMBOL(drm_fb_xrgb8888_to_xbgr8888);

static void drm_fb_xrgb8888_to_bgrx8888_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	drm_fb_xfrm_line_32to32(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_bgrx8888);
}

/**
 * drm_fb_xrgb8888_to_bgrx8888 - Convert XRGB8888 to BGRX8888 clip buffer
 * @dst: Array of BGRX8888 destination buffers
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
 * Drivers can use this function for BGRX8888 devices that don't support XRGB8888
 * natively.
 */
void drm_fb_xrgb8888_to_bgrx8888(struct iosys_map *dst, const unsigned int *dst_pitch,
				 const struct iosys_map *src,
				 const struct drm_framebuffer *fb,
				 const struct drm_rect *clip,
				 struct drm_format_conv_state *state)
{
	static const u8 dst_pixsize[DRM_FORMAT_MAX_PLANES] = {
		4,
	};

	drm_fb_xfrm(dst, dst_pitch, dst_pixsize, src, fb, clip, false, state,
		    drm_fb_xrgb8888_to_bgrx8888_line);
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_bgrx8888);

static void drm_fb_xrgb8888_to_xrgb2101010_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	drm_fb_xfrm_line_32to32(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_xrgb2101010);
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
	drm_fb_xfrm_line_32to32(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_argb2101010);
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
	drm_fb_xfrm_line_32to8(dbuf, sbuf, pixels, drm_pixel_xrgb8888_to_r8_bt601);
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
	drm_fb_xfrm_line_32to16(dbuf, sbuf, pixels, drm_pixel_argb8888_to_argb4444);
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
			drm_fb_xrgb8888_to_rgb565(dst, dst_pitch, src, fb, clip, state);
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
		} else if (dst_format == DRM_FORMAT_RGB332) {
			drm_fb_xrgb8888_to_rgb332(dst, dst_pitch, src, fb, clip, state);
			return 0;
		}
	}

	drm_warn_once(fb->dev, "No conversion helper from %p4cc to %p4cc found.\n",
		      &fb_format, &dst_format);

	return -EINVAL;
}
EXPORT_SYMBOL(drm_fb_blit);

static void drm_fb_gray8_to_gray2_line(void *dbuf, const void *sbuf, unsigned int pixels)
{
	u8 *dbuf8 = dbuf;
	const u8 *sbuf8 = sbuf;
	u8 px;

	while (pixels) {
		unsigned int i, bits = min(pixels, 4U);
		u8 byte = 0;

		for (i = 0; i < bits; i++, pixels--) {
			byte >>= 2;
			px = (*sbuf8++ * 3 + 127) / 255;
			byte |= (px &= 0x03) << 6;
		}
		*dbuf8++ = byte;
	}
}

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

/**
 * drm_fb_xrgb8888_to_gray2 - Convert XRGB8888 to gray2
 * @dst: Array of gray2 destination buffer
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
 * be converted and copied to the two first bits (LSB) in the first byte of the gray2
 * destination buffer. If the caller requires that the first pixel in a byte must
 * be located at an x-coordinate that is a multiple of 8, then the caller must take
 * care itself of supplying a suitable clip rectangle.
 *
 * DRM doesn't have native gray2 support. Drivers can use this function for
 * gray2 devices that don't support XRGB8888 natively. Such drivers can
 * announce the commonly supported XR24 format to userspace and use this function
 * to convert to the native format.
 *
 */
void drm_fb_xrgb8888_to_gray2(struct iosys_map *dst, const unsigned int *dst_pitch,
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
	u8 *gray2 = dst[0].vaddr, *gray8;
	u32 *src32;

	if (drm_WARN_ON(dev, fb->format->format != DRM_FORMAT_XRGB8888))
		return;

	if (!dst_pitch)
		dst_pitch = default_dst_pitch;
	dst_pitch_0 = dst_pitch[0];

	/*
	 * The gray2 destination buffer contains 2 bit per pixel
	 */
	if (!dst_pitch_0)
		dst_pitch_0 = DIV_ROUND_UP(linepixels, 4);

	/*
	 * The dma memory is write-combined so reads are uncached.
	 * Speed up by fetching one line at a time.
	 *
	 * Also, format conversion from XR24 to gray2 are done
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
		drm_fb_gray8_to_gray2_line(gray2, gray8, linepixels);
		vaddr += fb->pitches[0];
		gray2 += dst_pitch_0;
	}
}
EXPORT_SYMBOL(drm_fb_xrgb8888_to_gray2);

