// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/minmax.h>

#include <drm/drm_blend.h>
#include <drm/drm_rect.h>
#include <drm/drm_fixed.h>

#include <kunit/visibility.h>

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

/**
 * get_subsampling() - Get the subsampling divisor value on a specific direction
 *
 * @format: format to extarct the subsampling from
 * @direction: direction of the subsampling requested
 */
static int get_subsampling(const struct drm_format_info *format,
			   enum pixel_read_direction direction)
{
	switch (direction) {
	case READ_BOTTOM_TO_TOP:
	case READ_TOP_TO_BOTTOM:
		return format->vsub;
	case READ_RIGHT_TO_LEFT:
	case READ_LEFT_TO_RIGHT:
		return format->hsub;
	}
	WARN_ONCE(true, "Invalid direction for pixel reading: %d\n", direction);
	return 1;
}

/**
 * get_subsampling_offset() - An offset for keeping the chroma siting consistent regardless of
 * x_start and y_start values
 *
 * @direction: direction of the reading to properly compute this offset
 * @x_start: x coordinate of the starting point of the readed line
 * @y_start: y coordinate of the starting point of the readed line
 */
static int get_subsampling_offset(enum pixel_read_direction direction, int x_start, int y_start)
{
	switch (direction) {
	case READ_BOTTOM_TO_TOP:
		return -y_start - 1;
	case READ_TOP_TO_BOTTOM:
		return y_start;
	case READ_RIGHT_TO_LEFT:
		return -x_start - 1;
	case READ_LEFT_TO_RIGHT:
		return x_start;
	}
	WARN_ONCE(true, "Invalid direction for pixel reading: %d\n", direction);
	return 0;
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

static struct pixel_argb_u16 argb_u16_from_gray8(u8 gray)
{
	return argb_u16_from_u8888(255, gray, gray, gray);
}

static struct pixel_argb_u16 argb_u16_from_grayu16(u16 gray)
{
	return argb_u16_from_u16161616(0xFFFF, gray, gray, gray);
}

static struct pixel_argb_u16 argb_u16_from_BGR565(const __le16 *pixel)
{
	struct pixel_argb_u16 out_pixel;

	out_pixel = argb_u16_from_RGB565(pixel);
	swap(out_pixel.r, out_pixel.b);

	return out_pixel;
}

VISIBLE_IF_KUNIT
struct pixel_argb_u16 argb_u16_from_yuv161616(const struct conversion_matrix *matrix,
					      u16 y, u16 channel_1, u16 channel_2)
{
	u16 r, g, b;
	s64 fp_y, fp_channel_1, fp_channel_2;
	s64 fp_r, fp_g, fp_b;

	fp_y = drm_int2fixp((int)y - matrix->y_offset * 257);
	fp_channel_1 = drm_int2fixp((int)channel_1 - 128 * 257);
	fp_channel_2 = drm_int2fixp((int)channel_2 - 128 * 257);

	fp_r = drm_fixp_mul(matrix->matrix[0][0], fp_y) +
	       drm_fixp_mul(matrix->matrix[0][1], fp_channel_1) +
	       drm_fixp_mul(matrix->matrix[0][2], fp_channel_2);
	fp_g = drm_fixp_mul(matrix->matrix[1][0], fp_y) +
	       drm_fixp_mul(matrix->matrix[1][1], fp_channel_1) +
	       drm_fixp_mul(matrix->matrix[1][2], fp_channel_2);
	fp_b = drm_fixp_mul(matrix->matrix[2][0], fp_y) +
	       drm_fixp_mul(matrix->matrix[2][1], fp_channel_1) +
	       drm_fixp_mul(matrix->matrix[2][2], fp_channel_2);

	fp_r = drm_fixp2int_round(fp_r);
	fp_g = drm_fixp2int_round(fp_g);
	fp_b = drm_fixp2int_round(fp_b);

	r = clamp(fp_r, 0, 0xffff);
	g = clamp(fp_g, 0, 0xffff);
	b = clamp(fp_b, 0, 0xffff);

	return argb_u16_from_u16161616(0xffff, r, g, b);
}
EXPORT_SYMBOL_IF_KUNIT(argb_u16_from_yuv161616);

/**
 * READ_LINE() - Generic generator for a read_line function which can be used for format with one
 * plane and a block_h == block_w == 1.
 *
 * @function_name: Function name to generate
 * @pixel_name: Temporary pixel name used in the @__VA_ARGS__ parameters
 * @pixel_type: Used to specify the type you want to cast the pixel pointer
 * @callback: Callback to call for each pixels. This fonction should take @__VA_ARGS__ as parameter
 *            and return a pixel_argb_u16
 * __VA_ARGS__: Argument to pass inside the callback. You can use @pixel_name to access current
 *  pixel.
 */
#define READ_LINE(function_name, pixel_name, pixel_type, callback, ...)				\
static void function_name(const struct vkms_plane_state *plane, int x_start,			\
			      int y_start, enum pixel_read_direction direction, int count,	\
			      struct pixel_argb_u16 out_pixel[])				\
{												\
	struct pixel_argb_u16 *end = out_pixel + count;						\
	int step = get_block_step_bytes(plane->frame_info->fb, direction, 0);			\
	u8 *src_pixels;										\
												\
	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0, &src_pixels);		\
												\
	while (out_pixel < end) {								\
		pixel_type *(pixel_name) = (pixel_type *)src_pixels;				\
		*out_pixel = (callback)(__VA_ARGS__);						\
		out_pixel += 1;									\
		src_pixels += step;								\
	}											\
}

/**
 * READ_LINE_ARGB8888() - Generic generator for ARGB8888 formats.
 * The pixel type used is u8, so pixel_name[0]..pixel_name[n] are the n components of the pixel.
 *
 * @function_name: Function name to generate
 * @pixel_name: temporary pixel to use in @a, @r, @g and @b parameters
 * @a: alpha value
 * @r: red value
 * @g: green value
 * @b: blue value
 */
#define READ_LINE_ARGB8888(function_name, pixel_name, a, r, g, b) \
	READ_LINE(function_name, pixel_name, u8, argb_u16_from_u8888, a, r, g, b)
/**
 * READ_LINE_le16161616() - Generic generator for ARGB16161616 formats.
 * The pixel type used is u16, so pixel_name[0]..pixel_name[n] are the n components of the pixel.
 *
 * @function_name: Function name to generate
 * @pixel_name: temporary pixel to use in @a, @r, @g and @b parameters
 * @a: alpha value
 * @r: red value
 * @g: green value
 * @b: blue value
 */
#define READ_LINE_le16161616(function_name, pixel_name, a, r, g, b) \
	READ_LINE(function_name, pixel_name, __le16, argb_u16_from_le16161616, a, r, g, b)

/*
 * The following functions are read_line function for each pixel format supported by VKMS.
 *
 * They read a line starting at the point @x_start,@y_start following the @direction. The result
 * is stored in @out_pixel and in a 64 bits format, see struct pixel_argb_u16.
 *
 * These functions are very repetitive, but the innermost pixel loops must be kept inside these
 * functions for performance reasons. Some benchmarking was done in [1] where having the innermost
 * loop factored out of these functions showed a slowdown by a factor of three.
 *
 * [1]: https://lore.kernel.org/dri-devel/d258c8dc-78e9-4509-9037-a98f7f33b3a3@riseup.net/
 */

static void Rx_read_line(const struct vkms_plane_state *plane, int x_start,
			 int y_start, enum pixel_read_direction direction, int count,
			 struct pixel_argb_u16 out_pixel[])
{
	struct pixel_argb_u16 *end = out_pixel + count;
	int bits_per_pixel = drm_format_info_bpp(plane->frame_info->fb->format, 0);
	u8 *src_pixels;
	int rem_x, rem_y;

	WARN_ONCE(drm_format_info_block_height(plane->frame_info->fb->format, 0) != 1,
		  "%s() only support formats with block_h == 1", __func__);

	packed_pixels_addr(plane->frame_info, x_start, y_start, 0, &src_pixels, &rem_x, &rem_y);
	int bit_offset = (8 - bits_per_pixel) - rem_x * bits_per_pixel;
	int step = get_block_step_bytes(plane->frame_info->fb, direction, 0);
	int mask = (0x1 << bits_per_pixel) - 1;
	int lum_per_level = 0xFFFF / mask;

	if (direction == READ_LEFT_TO_RIGHT || direction == READ_RIGHT_TO_LEFT) {
		int restart_bit_offset;
		int step_bit_offset;

		if (direction == READ_LEFT_TO_RIGHT) {
			restart_bit_offset = 8 - bits_per_pixel;
			step_bit_offset = -bits_per_pixel;
		} else {
			restart_bit_offset = 0;
			step_bit_offset = bits_per_pixel;
		}

		while (out_pixel < end) {
			u8 val = ((*src_pixels) >> bit_offset) & mask;

			*out_pixel = argb_u16_from_grayu16((int)val * lum_per_level);

			bit_offset += step_bit_offset;
			if (bit_offset < 0 || 8 <= bit_offset) {
				bit_offset = restart_bit_offset;
				src_pixels += step;
			}
			out_pixel += 1;
		}
	} else if (direction == READ_TOP_TO_BOTTOM || direction == READ_BOTTOM_TO_TOP) {
		while (out_pixel < end) {
			u8 val = (*src_pixels >> bit_offset) & mask;
			*out_pixel = argb_u16_from_grayu16((int)val * lum_per_level);
			src_pixels += step;
			out_pixel += 1;
		}
	}
}

static void R1_read_line(const struct vkms_plane_state *plane, int x_start,
			 int y_start, enum pixel_read_direction direction, int count,
			 struct pixel_argb_u16 out_pixel[])
{
	Rx_read_line(plane, x_start, y_start, direction, count, out_pixel);
}

static void R2_read_line(const struct vkms_plane_state *plane, int x_start,
			 int y_start, enum pixel_read_direction direction, int count,
			 struct pixel_argb_u16 out_pixel[])
{
	Rx_read_line(plane, x_start, y_start, direction, count, out_pixel);
}

static void R4_read_line(const struct vkms_plane_state *plane, int x_start,
			 int y_start, enum pixel_read_direction direction, int count,
			 struct pixel_argb_u16 out_pixel[])
{
	Rx_read_line(plane, x_start, y_start, direction, count, out_pixel);
}


READ_LINE_ARGB8888(XRGB8888_read_line, px, 0xFF, px[2], px[1], px[0])
READ_LINE_ARGB8888(XBGR8888_read_line, px, 0xFF, px[0], px[1], px[2])

READ_LINE_ARGB8888(ARGB8888_read_line, px, px[3], px[2], px[1], px[0])
READ_LINE_ARGB8888(ABGR8888_read_line, px, px[3], px[0], px[1], px[2])
READ_LINE_ARGB8888(RGBA8888_read_line, px, px[0], px[3], px[2], px[1])
READ_LINE_ARGB8888(BGRA8888_read_line, px, px[0], px[1], px[2], px[3])

READ_LINE_ARGB8888(RGB888_read_line, px, 0xFF, px[2], px[1], px[0])
READ_LINE_ARGB8888(BGR888_read_line, px, 0xFF, px[0], px[1], px[2])

READ_LINE_le16161616(ARGB16161616_read_line, px, px[3], px[2], px[1], px[0])
READ_LINE_le16161616(ABGR16161616_read_line, px, px[3], px[0], px[1], px[2])
READ_LINE_le16161616(XRGB16161616_read_line, px, cpu_to_le16(0xFFFF), px[2], px[1], px[0])
READ_LINE_le16161616(XBGR16161616_read_line, px, cpu_to_le16(0xFFFF), px[0], px[1], px[2])

READ_LINE(RGB565_read_line, px, __le16, argb_u16_from_RGB565, px)
READ_LINE(BGR565_read_line, px, __le16, argb_u16_from_BGR565, px)

READ_LINE(R8_read_line, px, u8, argb_u16_from_gray8, *px)

/*
 * This callback can be used for YUV formats where U and V values are
 * stored in the same plane (often called semi-planar formats). It will
 * correctly handle subsampling as described in the drm_format_info of the plane.
 *
 * The conversion matrix stored in the @plane is used to:
 * - Apply the correct color range and encoding
 * - Convert YUV and YVU with the same function (a column swap is needed when setting up
 * plane->conversion_matrix)
 */

/**
 * READ_LINE_YUV_SEMIPLANAR() - Generic generator for a read_line function which can be used for yuv
 * formats with two planes and block_w == block_h == 1.
 *
 * @function_name: Function name to generate
 * @pixel_1_name: temporary pixel name for the first plane used in the @__VA_ARGS__ parameters
 * @pixel_2_name: temporary pixel name for the second plane used in the @__VA_ARGS__ parameters
 * @pixel_1_type: Used to specify the type you want to cast the pixel pointer on the plane 1
 * @pixel_2_type: Used to specify the type you want to cast the pixel pointer on the plane 2
 * @callback: Callback to call for each pixels. This function should take
 *            (struct conversion_matrix*, @__VA_ARGS__) as parameter and return a pixel_argb_u16
 * __VA_ARGS__: Argument to pass inside the callback. You can use @pixel_1_name and @pixel_2_name
 *               to access current pixel values
 */
#define READ_LINE_YUV_SEMIPLANAR(function_name, pixel_1_name, pixel_2_name, pixel_1_type,	\
				 pixel_2_type, callback, ...)					\
static void function_name(const struct vkms_plane_state *plane, int x_start,			\
		 int y_start, enum pixel_read_direction direction, int count,			\
		 struct pixel_argb_u16 out_pixel[])						\
{												\
	u8 *plane_1;										\
	u8 *plane_2;										\
												\
	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0,				\
			       &plane_1);							\
	packed_pixels_addr_1x1(plane->frame_info,						\
			       x_start / plane->frame_info->fb->format->hsub,			\
			       y_start / plane->frame_info->fb->format->vsub, 1,		\
			       &plane_2);							\
	int step_1 = get_block_step_bytes(plane->frame_info->fb, direction, 0);			\
	int step_2 = get_block_step_bytes(plane->frame_info->fb, direction, 1);			\
	int subsampling = get_subsampling(plane->frame_info->fb->format, direction);		\
	int subsampling_offset = get_subsampling_offset(direction, x_start, y_start);		\
	const struct conversion_matrix *conversion_matrix = &plane->conversion_matrix;		\
												\
	for (int i = 0; i < count; i++) {							\
		pixel_1_type *(pixel_1_name) = (pixel_1_type *)plane_1;				\
		pixel_2_type *(pixel_2_name) = (pixel_2_type *)plane_2;				\
		*out_pixel = (callback)(conversion_matrix, __VA_ARGS__);			\
		out_pixel += 1;									\
		plane_1 += step_1;								\
		if ((i + subsampling_offset + 1) % subsampling == 0)				\
			plane_2 += step_2;							\
	}											\
}

READ_LINE_YUV_SEMIPLANAR(YUV888_semiplanar_read_line, y, uv, u8, u8, argb_u16_from_yuv161616,
			 y[0] * 257, uv[0] * 257, uv[1] * 257)
READ_LINE_YUV_SEMIPLANAR(YUV161616_semiplanar_read_line, y, uv, u16, u16, argb_u16_from_yuv161616,
			 y[0], uv[0], uv[1])
/*
 * This callback can be used for YUV format where each color component is
 * stored in a different plane (often called planar formats). It will
 * correctly handle subsampling as described in the drm_format_info of the plane.
 *
 * The conversion matrix stored in the @plane is used to:
 * - Apply the correct color range and encoding
 * - Convert YUV and YVU with the same function (a column swap is needed when setting up
 * plane->conversion_matrix)
 */
static void planar_yuv_read_line(const struct vkms_plane_state *plane, int x_start,
				 int y_start, enum pixel_read_direction direction, int count,
				 struct pixel_argb_u16 out_pixel[])
{
	u8 *y_plane;
	u8 *channel_1_plane;
	u8 *channel_2_plane;

	packed_pixels_addr_1x1(plane->frame_info, x_start, y_start, 0,
			       &y_plane);
	packed_pixels_addr_1x1(plane->frame_info,
			       x_start / plane->frame_info->fb->format->hsub,
			       y_start / plane->frame_info->fb->format->vsub, 1,
			       &channel_1_plane);
	packed_pixels_addr_1x1(plane->frame_info,
			       x_start / plane->frame_info->fb->format->hsub,
			       y_start / plane->frame_info->fb->format->vsub, 2,
			       &channel_2_plane);
	int step_y = get_block_step_bytes(plane->frame_info->fb, direction, 0);
	int step_channel_1 = get_block_step_bytes(plane->frame_info->fb, direction, 1);
	int step_channel_2 = get_block_step_bytes(plane->frame_info->fb, direction, 2);
	int subsampling = get_subsampling(plane->frame_info->fb->format, direction);
	int subsampling_offset = get_subsampling_offset(direction, x_start, y_start);
	const struct conversion_matrix *conversion_matrix = &plane->conversion_matrix;

	for (int i = 0; i < count; i++) {
		*out_pixel = argb_u16_from_yuv161616(conversion_matrix,
						     *y_plane * 257, *channel_1_plane * 257,
						     *channel_2_plane * 257);
		out_pixel += 1;
		y_plane += step_y;
		if ((i + subsampling_offset + 1) % subsampling == 0) {
			channel_1_plane += step_channel_1;
			channel_2_plane += step_channel_2;
		}
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

static void argb_u16_to_ABGR8888(u8 *out_pixel, const struct pixel_argb_u16 *in_pixel)
{
	out_pixel[3] = DIV_ROUND_CLOSEST(in_pixel->a, 257);
	out_pixel[2] = DIV_ROUND_CLOSEST(in_pixel->b, 257);
	out_pixel[1] = DIV_ROUND_CLOSEST(in_pixel->g, 257);
	out_pixel[0] = DIV_ROUND_CLOSEST(in_pixel->r, 257);
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
	case DRM_FORMAT_ABGR8888:
		return &ABGR8888_read_line;
	case DRM_FORMAT_BGRA8888:
		return &BGRA8888_read_line;
	case DRM_FORMAT_RGBA8888:
		return &RGBA8888_read_line;
	case DRM_FORMAT_XRGB8888:
		return &XRGB8888_read_line;
	case DRM_FORMAT_XBGR8888:
		return &XBGR8888_read_line;
	case DRM_FORMAT_RGB888:
		return &RGB888_read_line;
	case DRM_FORMAT_BGR888:
		return &BGR888_read_line;
	case DRM_FORMAT_ARGB16161616:
		return &ARGB16161616_read_line;
	case DRM_FORMAT_ABGR16161616:
		return &ABGR16161616_read_line;
	case DRM_FORMAT_XRGB16161616:
		return &XRGB16161616_read_line;
	case DRM_FORMAT_XBGR16161616:
		return &XBGR16161616_read_line;
	case DRM_FORMAT_RGB565:
		return &RGB565_read_line;
	case DRM_FORMAT_BGR565:
		return &BGR565_read_line;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV42:
		return &YUV888_semiplanar_read_line;
	case DRM_FORMAT_P010:
	case DRM_FORMAT_P012:
	case DRM_FORMAT_P016:
		return &YUV161616_semiplanar_read_line;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YVU444:
		return &planar_yuv_read_line;
	case DRM_FORMAT_R1:
		return &R1_read_line;
	case DRM_FORMAT_R2:
		return &R2_read_line;
	case DRM_FORMAT_R4:
		return &R4_read_line;
	case DRM_FORMAT_R8:
		return &R8_read_line;
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

/*
 * Those matrices were generated using the colour python framework
 *
 * Below are the function calls used to generate each matrix, go to
 * https://colour.readthedocs.io/en/develop/generated/colour.matrix_YCbCr.html
 * for more info:
 *
 * numpy.around(colour.matrix_YCbCr(K=colour.WEIGHTS_YCBCR["ITU-R BT.601"],
 *                                  is_legal = False,
 *                                  bits = 8) * 2**32).astype(int)
 */
static const struct conversion_matrix no_operation = {
	.matrix = {
		{ 4294967296, 0,          0, },
		{ 0,          4294967296, 0, },
		{ 0,          0,          4294967296, },
	},
	.y_offset = 0,
};

static const struct conversion_matrix yuv_bt601_full = {
	.matrix = {
		{ 4294967296, 0,           6021544149 },
		{ 4294967296, -1478054095, -3067191994 },
		{ 4294967296, 7610682049,  0 },
	},
	.y_offset = 0,
};

/*
 * numpy.around(colour.matrix_YCbCr(K=colour.WEIGHTS_YCBCR["ITU-R BT.601"],
 *                                  is_legal = True,
 *                                  bits = 8) * 2**32).astype(int)
 */
static const struct conversion_matrix yuv_bt601_limited = {
	.matrix = {
		{ 5020601039, 0,           6881764740 },
		{ 5020601039, -1689204679, -3505362278 },
		{ 5020601039, 8697922339,  0 },
	},
	.y_offset = 16,
};

/*
 * numpy.around(colour.matrix_YCbCr(K=colour.WEIGHTS_YCBCR["ITU-R BT.709"],
 *                                  is_legal = False,
 *                                  bits = 8) * 2**32).astype(int)
 */
static const struct conversion_matrix yuv_bt709_full = {
	.matrix = {
		{ 4294967296, 0,          6763714498 },
		{ 4294967296, -804551626, -2010578443 },
		{ 4294967296, 7969741314, 0 },
	},
	.y_offset = 0,
};

/*
 * numpy.around(colour.matrix_YCbCr(K=colour.WEIGHTS_YCBCR["ITU-R BT.709"],
 *                                  is_legal = True,
 *                                  bits = 8) * 2**32).astype(int)
 */
static const struct conversion_matrix yuv_bt709_limited = {
	.matrix = {
		{ 5020601039, 0,          7729959424 },
		{ 5020601039, -919487572, -2297803934 },
		{ 5020601039, 9108275786, 0 },
	},
	.y_offset = 16,
};

/*
 * numpy.around(colour.matrix_YCbCr(K=colour.WEIGHTS_YCBCR["ITU-R BT.2020"],
 *                                  is_legal = False,
 *                                  bits = 8) * 2**32).astype(int)
 */
static const struct conversion_matrix yuv_bt2020_full = {
	.matrix = {
		{ 4294967296, 0,          6333358775 },
		{ 4294967296, -706750298, -2453942994 },
		{ 4294967296, 8080551471, 0 },
	},
	.y_offset = 0,
};

/*
 * numpy.around(colour.matrix_YCbCr(K=colour.WEIGHTS_YCBCR["ITU-R BT.2020"],
 *                                  is_legal = True,
 *                                  bits = 8) * 2**32).astype(int)
 */
static const struct conversion_matrix yuv_bt2020_limited = {
	.matrix = {
		{ 5020601039, 0,          7238124312 },
		{ 5020601039, -807714626, -2804506279 },
		{ 5020601039, 9234915964, 0 },
	},
	.y_offset = 16,
};

/**
 * swap_uv_columns() - Swap u and v column of a given matrix
 *
 * @matrix: Matrix in which column are swapped
 */
static void swap_uv_columns(struct conversion_matrix *matrix)
{
	swap(matrix->matrix[0][2], matrix->matrix[0][1]);
	swap(matrix->matrix[1][2], matrix->matrix[1][1]);
	swap(matrix->matrix[2][2], matrix->matrix[2][1]);
}

/**
 * get_conversion_matrix_to_argb_u16() - Retrieve the correct yuv to rgb conversion matrix for a
 * given encoding and range.
 *
 * @format: DRM_FORMAT_* value for which to obtain a conversion function (see [drm_fourcc.h])
 * @encoding: DRM_COLOR_* value for which to obtain a conversion matrix
 * @range: DRM_COLOR_*_RANGE value for which to obtain a conversion matrix
 * @matrix: Pointer to store the value into
 */
void get_conversion_matrix_to_argb_u16(u32 format,
				       enum drm_color_encoding encoding,
				       enum drm_color_range range,
				       struct conversion_matrix *matrix)
{
	const struct conversion_matrix *matrix_to_copy;
	bool limited_range;

	switch (range) {
	case DRM_COLOR_YCBCR_LIMITED_RANGE:
		limited_range = true;
		break;
	case DRM_COLOR_YCBCR_FULL_RANGE:
		limited_range = false;
		break;
	case DRM_COLOR_RANGE_MAX:
		limited_range = false;
		WARN_ONCE(true, "The requested range is not supported.");
		break;
	}

	switch (encoding) {
	case DRM_COLOR_YCBCR_BT601:
		matrix_to_copy = limited_range ? &yuv_bt601_limited :
						 &yuv_bt601_full;
		break;
	case DRM_COLOR_YCBCR_BT709:
		matrix_to_copy = limited_range ? &yuv_bt709_limited :
						 &yuv_bt709_full;
		break;
	case DRM_COLOR_YCBCR_BT2020:
		matrix_to_copy = limited_range ? &yuv_bt2020_limited :
						 &yuv_bt2020_full;
		break;
	case DRM_COLOR_ENCODING_MAX:
		matrix_to_copy = &no_operation;
		WARN_ONCE(true, "The requested encoding is not supported.");
		break;
	}

	memcpy(matrix, matrix_to_copy, sizeof(*matrix_to_copy));

	switch (format) {
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YVU444:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV42:
		swap_uv_columns(matrix);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(get_conversion_matrix_to_argb_u16);

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
	case DRM_FORMAT_ABGR8888:
		return &argb_u16_to_ABGR8888;
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
