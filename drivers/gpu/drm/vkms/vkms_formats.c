// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/minmax.h>

#include <drm/drm_blend.h>
#include <drm/drm_rect.h>
#include <drm/drm_fixed.h>

#include "vkms_formats.h"

/**
 * packed_pixels_offset() - Get the offset of the block containing the pixel at coordinates x/y
 *
 * @frame_info: Buffer metadata
 * @x: The x coordinate of the wanted pixel in the buffer
 * @y: The y coordinate of the wanted pixel in the buffer
 * @plane_index: The index of the plane to use
 * @offset: The returned offset inside the buffer of the block
 * @rem_x: The returned X coordinate of the requested pixel in the block
 * @rem_y: The returned Y coordinate of the requested pixel in the block
 *
 * As some pixel formats store multiple pixels in a block (DRM_FORMAT_R* for example), some
 * pixels are not individually addressable. This function return 3 values: the offset of the
 * whole block, and the coordinate of the requested pixel inside this block.
 * For example, if the format is DRM_FORMAT_R1 and the requested coordinate is 13,5, the offset
 * will point to the byte 5*pitches + 13/8 (second byte of the 5th line), and the rem_x/rem_y
 * coordinates will be (13 % 8, 5 % 1) = (5, 0)
 *
 * With this function, the caller just have to extract the correct pixel from the block.
 */
static void packed_pixels_offset(const struct vkms_frame_info *frame_info, int x, int y,
				 int plane_index, int *offset, int *rem_x, int *rem_y)
{
	struct drm_framebuffer *fb = frame_info->fb;
	const struct drm_format_info *format = frame_info->fb->format;
	/* Directly using x and y to multiply pitches and format->ccp is not sufficient because
	 * in some formats a block can represent multiple pixels.
	 *
	 * Dividing x and y by the block size allows to extract the correct offset of the block
	 * containing the pixel.
	 */

	int block_x = x / drm_format_info_block_width(format, plane_index);
	int block_y = y / drm_format_info_block_height(format, plane_index);
	int block_pitch = fb->pitches[plane_index] * drm_format_info_block_height(format,
										  plane_index);
	*rem_x = x % drm_format_info_block_width(format, plane_index);
	*rem_y = y % drm_format_info_block_height(format, plane_index);
	*offset = fb->offsets[plane_index] +
		  block_y * block_pitch +
		  block_x * format->char_per_block[plane_index];
}

/**
 * packed_pixels_addr() - Get the pointer to the block containing the pixel at the given
 * coordinates
 *
 * @frame_info: Buffer metadata
 * @x: The x (width) coordinate inside the plane
 * @y: The y (height) coordinate inside the plane
 * @plane_index: The index of the plane
 * @addr: The returned pointer
 * @rem_x: The returned X coordinate of the requested pixel in the block
 * @rem_y: The returned Y coordinate of the requested pixel in the block
 *
 * Takes the information stored in the frame_info, a pair of coordinates, and returns the address
 * of the block containing this pixel and the pixel position inside this block.
 *
 * See @packed_pixels_offset for details about rem_x/rem_y behavior.
 */
static void packed_pixels_addr(const struct vkms_frame_info *frame_info,
			       int x, int y, int plane_index, u8 **addr, int *rem_x,
			       int *rem_y)
{
	int offset;

	packed_pixels_offset(frame_info, x, y, plane_index, &offset, rem_x, rem_y);
	*addr = (u8 *)frame_info->map[0].vaddr + offset;
}

/**
 * get_block_step_bytes() - Common helper to compute the correct step value between each pixel block
 * to read in a certain direction.
 *
 * @fb: Framebuffer to iter on
 * @direction: Direction of the reading
 * @plane_index: Plane to get the step from
 *
 * As the returned count is the number of bytes between two consecutive blocks in a direction,
 * the caller may have to read multiple pixels before using the next one (for example, to read from
 * left to right in a DRM_FORMAT_R1 plane, each block contains 8 pixels, so the step must be used
 * only every 8 pixels).
 */
static int get_block_step_bytes(struct drm_framebuffer *fb, enum pixel_read_direction direction,
				int plane_index)
{
	switch (direction) {
	case READ_LEFT_TO_RIGHT:
		return fb->format->char_per_block[plane_index];
	case READ_RIGHT_TO_LEFT:
		return -fb->format->char_per_block[plane_index];
	case READ_TOP_TO_BOTTOM:
		return (int)fb->pitches[plane_index] * drm_format_info_block_width(fb->format,
										   plane_index);
	case READ_BOTTOM_TO_TOP:
		return -(int)fb->pitches[plane_index] * drm_format_info_block_width(fb->format,
										    plane_index);
	}

	return 0;
}

/**
 * packed_pixels_addr_1x1() - Get the pointer to the block containing the pixel at the given
 * coordinates
 *
 * @frame_info: Buffer metadata
 * @x: The x (width) coordinate inside the plane
 * @y: The y (height) coordinate inside the plane
 * @plane_index: The index of the plane
 * @addr: The returned pointer
 *
 * This function can only be used with format where block_h == block_w == 1.
 */
static void packed_pixels_addr_1x1(const struct vkms_frame_info *frame_info,
				   int x, int y, int plane_index, u8 **addr)
{
	int offset, rem_x, rem_y;

	WARN_ONCE(drm_format_info_block_width(frame_info->fb->format,
					      plane_index) != 1,
		"%s() only support formats with block_w == 1", __func__);
	WARN_ONCE(drm_format_info_block_height(frame_info->fb->format,
					       plane_index) != 1,
		"%s() only support formats with block_h == 1", __func__);

	packed_pixels_offset(frame_info, x, y, plane_index, &offset, &rem_x,
			     &rem_y);
	*addr = (u8 *)frame_info->map[0].vaddr + offset;
}

/*
 * The following functions take pixel data (a, r, g, b, pixel, ...) and convert them to
 * &struct pixel_argb_u16
 *
 * They are used in the `read_line`s functions to avoid duplicate work for some pixel formats.
 */

static struct pixel_argb_u16 argb_u16_from_u8888(u8 a, u8 r, u8 g, u8 b)
{
	struct pixel_argb_u16 out_pixel;
	/*
	 * The 257 is the "conversion ratio". This number is obtained by the
	 * (2^16 - 1) / (2^8 - 1) division. Which, in this case, tries to get
	 * the best color value in a pixel format with more possibilities.
	 * A similar idea applies to others RGB color conversions.
	 */
	out_pixel.a = (u16)a * 257;
	out_pixel.r = (u16)r * 257;
	out_pixel.g = (u16)g * 257;
	out_pixel.b = (u16)b * 257;

	return out_pixel;
}

static struct pixel_argb_u16 argb_u16_from_u16161616(u16 a, u16 r, u16 g, u16 b)
{
	struct pixel_argb_u16 out_pixel;

	out_pixel.a = a;
	out_pixel.r = r;
	out_pixel.g = g;
	out_pixel.b = b;

	return out_pixel;
}

static struct pixel_argb_u16 argb_u16_from_le16161616(__le16 a, __le16 r, __le16 g, __le16 b)
{
	return argb_u16_from_u16161616(le16_to_cpu(a), le16_to_cpu(r), le16_to_cpu(g),
				       le16_to_cpu(b));
}

static struct pixel_argb_u16 argb_u16_from_RGB565(const __le16 *pixel)
{
	struct pixel_argb_u16 out_pixel;

	s64 fp_rb_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(31));
	s64 fp_g_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(63));

	u16 rgb_565 = le16_to_cpu(*pixel);
	s64 fp_r = drm_int2fixp((rgb_565 >> 11) & 0x1f);
	s64 fp_g = drm_int2fixp((rgb_565 >> 5) & 0x3f);
	s64 fp_b = drm_int2fixp(rgb_565 & 0x1f);

	out_pixel.a = (u16)0xffff;
	out_pixel.r = drm_fixp2int_round(drm_fixp_mul(fp_r, fp_rb_ratio));
	out_pixel.g = drm_fixp2int_round(drm_fixp_mul(fp_g, fp_g_ratio));
	out_pixel.b = drm_fixp2int_round(drm_fixp_mul(fp_b, fp_rb_ratio));

	return out_pixel;
}

/*
 * The following functions are read_line function for each pixel format supported by VKMS.
 *
 * They read a line starting at the point @x_start,@y_start following the @direction. The result
 * is stored in @out_pixel and in the format ARGB16161616.
 *
 * These functions are very repetitive, but the innermost pixel loops must be kept inside these
 * functions for performance reasons. Some benchmarking was done in [1] where having the innermost
 * loop factored out of these functions showed a slowdown by a factor of three.
 *
 * [1]: https://lore.kernel.org/dri-devel/d258c8dc-78e9-4509-9037-a98f7f33b3a3@riseup.net/
 */

static void ARGB8888_read_line(const struct vkms_plane_state *plane, int x_start, int y_start,
			       enum pixel_read_direction direction, int count,
			       struct pixel_argb_u16 out_pixel[])
{
	struct pixel_argb_u16 *end = out_pixel + count;
	u8 *src_pixels;

	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0, &src_pixels);

	int step = get_block_step_bytes(plane->frame_info->fb, direction, 0);

	while (out_pixel < end) {
		u8 *px = (u8 *)src_pixels;
		*out_pixel = argb_u16_from_u8888(px[3], px[2], px[1], px[0]);
		out_pixel += 1;
		src_pixels += step;
	}
}

static void XRGB8888_read_line(const struct vkms_plane_state *plane, int x_start, int y_start,
			       enum pixel_read_direction direction, int count,
			       struct pixel_argb_u16 out_pixel[])
{
	struct pixel_argb_u16 *end = out_pixel + count;
	u8 *src_pixels;

	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0, &src_pixels);

	int step = get_block_step_bytes(plane->frame_info->fb, direction, 0);

	while (out_pixel < end) {
		u8 *px = (u8 *)src_pixels;
		*out_pixel = argb_u16_from_u8888(255, px[2], px[1], px[0]);
		out_pixel += 1;
		src_pixels += step;
	}
}

static void ARGB16161616_read_line(const struct vkms_plane_state *plane, int x_start,
				   int y_start, enum pixel_read_direction direction, int count,
				   struct pixel_argb_u16 out_pixel[])
{
	struct pixel_argb_u16 *end = out_pixel + count;
	u8 *src_pixels;

	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0, &src_pixels);

	int step = get_block_step_bytes(plane->frame_info->fb, direction, 0);

	while (out_pixel < end) {
		u16 *px = (u16 *)src_pixels;
		*out_pixel = argb_u16_from_u16161616(px[3], px[2], px[1], px[0]);
		out_pixel += 1;
		src_pixels += step;
	}
}

static void XRGB16161616_read_line(const struct vkms_plane_state *plane, int x_start,
				   int y_start, enum pixel_read_direction direction, int count,
				   struct pixel_argb_u16 out_pixel[])
{
	struct pixel_argb_u16 *end = out_pixel + count;
	u8 *src_pixels;

	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0, &src_pixels);

	int step = get_block_step_bytes(plane->frame_info->fb, direction, 0);

	while (out_pixel < end) {
		__le16 *px = (__le16 *)src_pixels;
		*out_pixel = argb_u16_from_le16161616(cpu_to_le16(0xFFFF), px[2], px[1], px[0]);
		out_pixel += 1;
		src_pixels += step;
	}
}

static void RGB565_read_line(const struct vkms_plane_state *plane, int x_start,
			     int y_start, enum pixel_read_direction direction, int count,
			     struct pixel_argb_u16 out_pixel[])
{
	struct pixel_argb_u16 *end = out_pixel + count;
	u8 *src_pixels;

	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0, &src_pixels);

	int step = get_block_step_bytes(plane->frame_info->fb, direction, 0);

	while (out_pixel < end) {
		__le16 *px = (__le16 *)src_pixels;

		*out_pixel = argb_u16_from_RGB565(px);
		out_pixel += 1;
		src_pixels += step;
	}
}

/*
 * The following functions take one &struct pixel_argb_u16 and convert it to a specific format.
 * The result is stored in @out_pixel.
 *
 * They are used in vkms_writeback_row() to convert and store a pixel from the src_buffer to
 * the writeback buffer.
 */
static void argb_u16_to_ARGB8888(u8 *out_pixel, const struct pixel_argb_u16 *in_pixel)
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
	out_pixel[3] = DIV_ROUND_CLOSEST(in_pixel->a, 257);
	out_pixel[2] = DIV_ROUND_CLOSEST(in_pixel->r, 257);
	out_pixel[1] = DIV_ROUND_CLOSEST(in_pixel->g, 257);
	out_pixel[0] = DIV_ROUND_CLOSEST(in_pixel->b, 257);
}

static void argb_u16_to_XRGB8888(u8 *out_pixel, const struct pixel_argb_u16 *in_pixel)
{
	out_pixel[3] = 0xff;
	out_pixel[2] = DIV_ROUND_CLOSEST(in_pixel->r, 257);
	out_pixel[1] = DIV_ROUND_CLOSEST(in_pixel->g, 257);
	out_pixel[0] = DIV_ROUND_CLOSEST(in_pixel->b, 257);
}

static void argb_u16_to_ARGB16161616(u8 *out_pixel, const struct pixel_argb_u16 *in_pixel)
{
	__le16 *pixel = (__le16 *)out_pixel;

	pixel[3] = cpu_to_le16(in_pixel->a);
	pixel[2] = cpu_to_le16(in_pixel->r);
	pixel[1] = cpu_to_le16(in_pixel->g);
	pixel[0] = cpu_to_le16(in_pixel->b);
}

static void argb_u16_to_XRGB16161616(u8 *out_pixel, const struct pixel_argb_u16 *in_pixel)
{
	__le16 *pixel = (__le16 *)out_pixel;

	pixel[3] = cpu_to_le16(0xffff);
	pixel[2] = cpu_to_le16(in_pixel->r);
	pixel[1] = cpu_to_le16(in_pixel->g);
	pixel[0] = cpu_to_le16(in_pixel->b);
}

static void argb_u16_to_RGB565(u8 *out_pixel, const struct pixel_argb_u16 *in_pixel)
{
	__le16 *pixel = (__le16 *)out_pixel;

	s64 fp_rb_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(31));
	s64 fp_g_ratio = drm_fixp_div(drm_int2fixp(65535), drm_int2fixp(63));

	s64 fp_r = drm_int2fixp(in_pixel->r);
	s64 fp_g = drm_int2fixp(in_pixel->g);
	s64 fp_b = drm_int2fixp(in_pixel->b);

	u16 r = drm_fixp2int(drm_fixp_div(fp_r, fp_rb_ratio));
	u16 g = drm_fixp2int(drm_fixp_div(fp_g, fp_g_ratio));
	u16 b = drm_fixp2int(drm_fixp_div(fp_b, fp_rb_ratio));

	*pixel = cpu_to_le16(r << 11 | g << 5 | b);
}

/**
 * vkms_writeback_row() - Generic loop for all supported writeback format. It is executed just
 * after the blending to write a line in the writeback buffer.
 *
 * @wb: Job where to insert the final image
 * @src_buffer: Line to write
 * @y: Row to write in the writeback buffer
 */
void vkms_writeback_row(struct vkms_writeback_job *wb,
			const struct line_buffer *src_buffer, int y)
{
	struct vkms_frame_info *frame_info = &wb->wb_frame_info;
	int x_dst = frame_info->dst.x1;
	u8 *dst_pixels;
	int rem_x, rem_y;

	packed_pixels_addr(frame_info, x_dst, y, 0, &dst_pixels, &rem_x, &rem_y);
	struct pixel_argb_u16 *in_pixels = src_buffer->pixels;
	int x_limit = min_t(size_t, drm_rect_width(&frame_info->dst), src_buffer->n_pixels);

	for (size_t x = 0; x < x_limit; x++, dst_pixels += frame_info->fb->format->cpp[0])
		wb->pixel_write(dst_pixels, &in_pixels[x]);
}

/**
 * get_pixel_read_line_function() - Retrieve the correct read_line function for a specific
 * format. The returned pointer is NULL for unsupported pixel formats. The caller must ensure that
 * the pointer is valid before using it in a vkms_plane_state.
 *
 * @format: DRM_FORMAT_* value for which to obtain a conversion function (see [drm_fourcc.h])
 */
pixel_read_line_t get_pixel_read_line_function(u32 format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
		return &ARGB8888_read_line;
	case DRM_FORMAT_XRGB8888:
		return &XRGB8888_read_line;
	case DRM_FORMAT_ARGB16161616:
		return &ARGB16161616_read_line;
	case DRM_FORMAT_XRGB16161616:
		return &XRGB16161616_read_line;
	case DRM_FORMAT_RGB565:
		return &RGB565_read_line;
	default:
		/*
		 * This is a bug in vkms_plane_atomic_check(). All the supported
		 * format must:
		 * - Be listed in vkms_formats in vkms_plane.c
		 * - Have a pixel_read callback defined here
		 */
		pr_err("Pixel format %p4cc is not supported by VKMS planes. This is a kernel bug, atomic check must forbid this configuration.\n",
		       &format);
		BUG();
	}
}

/**
 * get_pixel_write_function() - Retrieve the correct write_pixel function for a specific format.
 * The returned pointer is NULL for unsupported pixel formats. The caller must ensure that the
 * pointer is valid before using it in a vkms_writeback_job.
 *
 * @format: DRM_FORMAT_* value for which to obtain a conversion function (see [drm_fourcc.h])
 */
pixel_write_t get_pixel_write_function(u32 format)
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
		/*
		 * This is a bug in vkms_writeback_atomic_check. All the supported
		 * format must:
		 * - Be listed in vkms_wb_formats in vkms_writeback.c
		 * - Have a pixel_write callback defined here
		 */
		pr_err("Pixel format %p4cc is not supported by VKMS writeback. This is a kernel bug, atomic check must forbid this configuration.\n",
		       &format);
		BUG();
	}
}
