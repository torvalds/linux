// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/minmax.h>

#include <drm/drm_blend.h>
#include <drm/drm_rect.h>
#include <drm/drm_fixed.h>

#include "vkms_formats.h"

static size_t pixel_offset(const struct vkms_frame_info *frame_info, int x, int y)
{
	return frame_info->offset + (y * frame_info->pitch)
				  + (x * frame_info->cpp);
}

/*
 * packed_pixels_addr - Get the pointer to pixel of a given pair of coordinates
 *
 * @frame_info: Buffer metadata
 * @x: The x(width) coordinate of the 2D buffer
 * @y: The y(Heigth) coordinate of the 2D buffer
 *
 * Takes the information stored in the frame_info, a pair of coordinates, and
 * returns the address of the first color channel.
 * This function assumes the channels are packed together, i.e. a color channel
 * comes immediately after another in the memory. And therefore, this function
 * doesn't work for YUV with chroma subsampling (e.g. YUV420 and NV21).
 */
static void *packed_pixels_addr(const struct vkms_frame_info *frame_info,
				int x, int y)
{
	size_t offset = pixel_offset(frame_info, x, y);

	return (u8 *)frame_info->map[0].vaddr + offset;
}

static void *get_packed_src_addr(const struct vkms_frame_info *frame_info, int y)
{
	int x_src = frame_info->src.x1 >> 16;
	int y_src = y - frame_info->rotated.y1 + (frame_info->src.y1 >> 16);

	return packed_pixels_addr(frame_info, x_src, y_src);
}

static int get_x_position(const struct vkms_frame_info *frame_info, int limit, int x)
{
	if (frame_info->rotation & (DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_270))
		return limit - x - 1;
	return x;
}

static void ARGB8888_to_argb_u16(u8 *src_pixels, struct pixel_argb_u16 *out_pixel)
{
	/*
	 * The 257 is the "conversion ratio". This number is obtained by the
	 * (2^16 - 1) / (2^8 - 1) division. Which, in this case, tries to get
	 * the best color value in a pixel format with more possibilities.
	 * A similar idea applies to others RGB color conversions.
	 */
	out_pixel->a = (u16)src_pixels[3] * 257;
	out_pixel->r = (u16)src_pixels[2] * 257;
	out_pixel->g = (u16)src_pixels[1] * 257;
	out_pixel->b = (u16)src_pixels[0] * 257;
}

static void XRGB8888_to_argb_u16(u8 *src_pixels, struct pixel_argb_u16 *out_pixel)
{
	out_pixel->a = (u16)0xffff;
	out_pixel->r = (u16)src_pixels[2] * 257;
	out_pixel->g = (u16)src_pixels[1] * 257;
	out_pixel->b = (u16)src_pixels[0] * 257;
}

static void ARGB16161616_to_argb_u16(u8 *src_pixels, struct pixel_argb_u16 *out_pixel)
{
	u16 *pixels = (u16 *)src_pixels;

	out_pixel->a = le16_to_cpu(pixels[3]);
	out_pixel->r = le16_to_cpu(pixels[2]);
	out_pixel->g = le16_to_cpu(pixels[1]);
	out_pixel->b = le16_to_cpu(pixels[0]);
}

static void XRGB16161616_to_argb_u16(u8 *src_pixels, struct pixel_argb_u16 *out_pixel)
{
	u16 *pixels = (u16 *)src_pixels;

	out_pixel->a = (u16)0xffff;
	out_pixel->r = le16_to_cpu(pixels[2]);
	out_pixel->g = le16_to_cpu(pixels[1]);
	out_pixel->b = le16_to_cpu(pixels[0]);
}

static void RGB565_to_argb_u16(u8 *src_pixels, struct pixel_argb_u16 *out_pixel)
{
	u16 *pixels = (u16 *)src_pixels;

	s64 fp_rb_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(31));
	s64 fp_g_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(63));

	u16 rgb_565 = le16_to_cpu(*pixels);
	s64 fp_r = drm_int2fixp((rgb_565 >> 11) & 0x1f);
	s64 fp_g = drm_int2fixp((rgb_565 >> 5) & 0x3f);
	s64 fp_b = drm_int2fixp(rgb_565 & 0x1f);

	out_pixel->a = (u16)0xffff;
	out_pixel->r = drm_fixp2int_round(drm_fixp_mul(fp_r, fp_rb_ratio));
	out_pixel->g = drm_fixp2int_round(drm_fixp_mul(fp_g, fp_g_ratio));
	out_pixel->b = drm_fixp2int_round(drm_fixp_mul(fp_b, fp_rb_ratio));
}

/**
 * vkms_compose_row - compose a single row of a plane
 * @stage_buffer: output line with the composed pixels
 * @plane: state of the plane that is being composed
 * @y: y coordinate of the row
 *
 * This function composes a single row of a plane. It gets the source pixels
 * through the y coordinate (see get_packed_src_addr()) and goes linearly
 * through the source pixel, reading the pixels and converting it to
 * ARGB16161616 (see the pixel_read() callback). For rotate-90 and rotate-270,
 * the source pixels are not traversed linearly. The source pixels are queried
 * on each iteration in order to traverse the pixels vertically.
 */
void vkms_compose_row(struct line_buffer *stage_buffer, struct vkms_plane_state *plane, int y)
{
	struct pixel_argb_u16 *out_pixels = stage_buffer->pixels;
	struct vkms_frame_info *frame_info = plane->frame_info;
	u8 *src_pixels = get_packed_src_addr(frame_info, y);
	int limit = min_t(size_t, drm_rect_width(&frame_info->dst), stage_buffer->n_pixels);

	for (size_t x = 0; x < limit; x++, src_pixels += frame_info->cpp) {
		int x_pos = get_x_position(frame_info, limit, x);

		if (drm_rotation_90_or_270(frame_info->rotation))
			src_pixels = get_packed_src_addr(frame_info, x + frame_info->rotated.y1)
				+ frame_info->cpp * y;

		plane->pixel_read(src_pixels, &out_pixels[x_pos]);
	}
}

/*
 * The following  functions take an line of argb_u16 pixels from the
 * src_buffer, convert them to a specific format, and store them in the
 * destination.
 *
 * They are used in the `compose_active_planes` to convert and store a line
 * from the src_buffer to the writeback buffer.
 */
static void argb_u16_to_ARGB8888(u8 *dst_pixels, struct pixel_argb_u16 *in_pixel)
{
	/*
	 * This sequence below is important because the format's byte order is
	 * in little-endian. In the case of the ARGB8888 the memory is
	 * organized this way:
	 *
	 * | Addr     | = blue channel
	 * | Addr + 1 | = green channel
	 * | Addr + 2 | = Red channel
	 * | Addr + 3 | = Alpha channel
	 */
	dst_pixels[3] = DIV_ROUND_CLOSEST(in_pixel->a, 257);
	dst_pixels[2] = DIV_ROUND_CLOSEST(in_pixel->r, 257);
	dst_pixels[1] = DIV_ROUND_CLOSEST(in_pixel->g, 257);
	dst_pixels[0] = DIV_ROUND_CLOSEST(in_pixel->b, 257);
}

static void argb_u16_to_XRGB8888(u8 *dst_pixels, struct pixel_argb_u16 *in_pixel)
{
	dst_pixels[3] = 0xff;
	dst_pixels[2] = DIV_ROUND_CLOSEST(in_pixel->r, 257);
	dst_pixels[1] = DIV_ROUND_CLOSEST(in_pixel->g, 257);
	dst_pixels[0] = DIV_ROUND_CLOSEST(in_pixel->b, 257);
}

static void argb_u16_to_ARGB16161616(u8 *dst_pixels, struct pixel_argb_u16 *in_pixel)
{
	u16 *pixels = (u16 *)dst_pixels;

	pixels[3] = cpu_to_le16(in_pixel->a);
	pixels[2] = cpu_to_le16(in_pixel->r);
	pixels[1] = cpu_to_le16(in_pixel->g);
	pixels[0] = cpu_to_le16(in_pixel->b);
}

static void argb_u16_to_XRGB16161616(u8 *dst_pixels, struct pixel_argb_u16 *in_pixel)
{
	u16 *pixels = (u16 *)dst_pixels;

	pixels[3] = 0xffff;
	pixels[2] = cpu_to_le16(in_pixel->r);
	pixels[1] = cpu_to_le16(in_pixel->g);
	pixels[0] = cpu_to_le16(in_pixel->b);
}

static void argb_u16_to_RGB565(u8 *dst_pixels, struct pixel_argb_u16 *in_pixel)
{
	u16 *pixels = (u16 *)dst_pixels;

	s64 fp_rb_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(31));
	s64 fp_g_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(63));

	s64 fp_r = drm_int2fixp(in_pixel->r);
	s64 fp_g = drm_int2fixp(in_pixel->g);
	s64 fp_b = drm_int2fixp(in_pixel->b);

	u16 r = drm_fixp2int(drm_fixp_div(fp_r, fp_rb_ratio));
	u16 g = drm_fixp2int(drm_fixp_div(fp_g, fp_g_ratio));
	u16 b = drm_fixp2int(drm_fixp_div(fp_b, fp_rb_ratio));

	*pixels = cpu_to_le16(r << 11 | g << 5 | b);
}

void vkms_writeback_row(struct vkms_writeback_job *wb,
			const struct line_buffer *src_buffer, int y)
{
	struct vkms_frame_info *frame_info = &wb->wb_frame_info;
	int x_dst = frame_info->dst.x1;
	u8 *dst_pixels = packed_pixels_addr(frame_info, x_dst, y);
	struct pixel_argb_u16 *in_pixels = src_buffer->pixels;
	int x_limit = min_t(size_t, drm_rect_width(&frame_info->dst), src_buffer->n_pixels);

	for (size_t x = 0; x < x_limit; x++, dst_pixels += frame_info->cpp)
		wb->pixel_write(dst_pixels, &in_pixels[x]);
}

void *get_pixel_conversion_function(u32 format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return &ARGB8888_to_argb_u16;
	case DRM_FORMAT_XRGB8888:
		return &XRGB8888_to_argb_u16;
	case DRM_FORMAT_ARGB16161616:
		return &ARGB16161616_to_argb_u16;
	case DRM_FORMAT_XRGB16161616:
		return &XRGB16161616_to_argb_u16;
	case DRM_FORMAT_RGB565:
		return &RGB565_to_argb_u16;
	default:
		return NULL;
	}
}

void *get_pixel_write_function(u32 format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return &argb_u16_to_ARGB8888;
	case DRM_FORMAT_XRGB8888:
		return &argb_u16_to_XRGB8888;
	case DRM_FORMAT_ARGB16161616:
		return &argb_u16_to_ARGB16161616;
	case DRM_FORMAT_XRGB16161616:
		return &argb_u16_to_XRGB16161616;
	case DRM_FORMAT_RGB565:
		return &argb_u16_to_RGB565;
	default:
		return NULL;
	}
}
