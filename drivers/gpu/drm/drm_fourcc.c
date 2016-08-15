/*
 * Copyright (c) 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * DRM core format related functions
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <linux/bug.h>
#include <linux/ctype.h>
#include <linux/export.h>
#include <linux/kernel.h>

#include <drm/drmP.h>
#include <drm/drm_fourcc.h>

static char printable_char(int c)
{
	return isascii(c) && isprint(c) ? c : '?';
}

/**
 * drm_get_format_name - return a string for drm fourcc format
 * @format: format to compute name of
 *
 * Note that the buffer returned by this function is owned by the caller
 * and will need to be freed using kfree().
 */
const char *drm_get_format_name(uint32_t format)
{
	char *buf = kmalloc(32, GFP_KERNEL);

	snprintf(buf, 32,
		 "%c%c%c%c %s-endian (0x%08x)",
		 printable_char(format & 0xff),
		 printable_char((format >> 8) & 0xff),
		 printable_char((format >> 16) & 0xff),
		 printable_char((format >> 24) & 0x7f),
		 format & DRM_FORMAT_BIG_ENDIAN ? "big" : "little",
		 format);

	return buf;
}
EXPORT_SYMBOL(drm_get_format_name);

/**
 * drm_fb_get_bpp_depth - get the bpp/depth values for format
 * @format: pixel format (DRM_FORMAT_*)
 * @depth: storage for the depth value
 * @bpp: storage for the bpp value
 *
 * This only supports RGB formats here for compat with code that doesn't use
 * pixel formats directly yet.
 */
void drm_fb_get_bpp_depth(uint32_t format, unsigned int *depth,
			  int *bpp)
{
	const char *format_name;

	switch (format) {
	case DRM_FORMAT_C8:
	case DRM_FORMAT_RGB332:
	case DRM_FORMAT_BGR233:
		*depth = 8;
		*bpp = 8;
		break;
	case DRM_FORMAT_XRGB1555:
	case DRM_FORMAT_XBGR1555:
	case DRM_FORMAT_RGBX5551:
	case DRM_FORMAT_BGRX5551:
	case DRM_FORMAT_ARGB1555:
	case DRM_FORMAT_ABGR1555:
	case DRM_FORMAT_RGBA5551:
	case DRM_FORMAT_BGRA5551:
		*depth = 15;
		*bpp = 16;
		break;
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_BGR565:
		*depth = 16;
		*bpp = 16;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_BGR888:
		*depth = 24;
		*bpp = 24;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_BGRX8888:
		*depth = 24;
		*bpp = 32;
		break;
	case DRM_FORMAT_XRGB2101010:
	case DRM_FORMAT_XBGR2101010:
	case DRM_FORMAT_RGBX1010102:
	case DRM_FORMAT_BGRX1010102:
	case DRM_FORMAT_ARGB2101010:
	case DRM_FORMAT_ABGR2101010:
	case DRM_FORMAT_RGBA1010102:
	case DRM_FORMAT_BGRA1010102:
		*depth = 30;
		*bpp = 32;
		break;
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_RGBA8888:
	case DRM_FORMAT_BGRA8888:
		*depth = 32;
		*bpp = 32;
		break;
	default:
		format_name = drm_get_format_name(format);
		DRM_DEBUG_KMS("unsupported pixel format %s\n", format_name);
		kfree(format_name);
		*depth = 0;
		*bpp = 0;
		break;
	}
}
EXPORT_SYMBOL(drm_fb_get_bpp_depth);

/**
 * drm_format_num_planes - get the number of planes for format
 * @format: pixel format (DRM_FORMAT_*)
 *
 * Returns:
 * The number of planes used by the specified pixel format.
 */
int drm_format_num_planes(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_YUV410:
	case DRM_FORMAT_YVU410:
	case DRM_FORMAT_YUV411:
	case DRM_FORMAT_YVU411:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		return 3;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		return 2;
	default:
		return 1;
	}
}
EXPORT_SYMBOL(drm_format_num_planes);

/**
 * drm_format_plane_cpp - determine the bytes per pixel value
 * @format: pixel format (DRM_FORMAT_*)
 * @plane: plane index
 *
 * Returns:
 * The bytes per pixel value for the specified plane.
 */
int drm_format_plane_cpp(uint32_t format, int plane)
{
	unsigned int depth;
	int bpp;

	if (plane >= drm_format_num_planes(format))
		return 0;

	switch (format) {
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		return 2;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		return plane ? 2 : 1;
	case DRM_FORMAT_YUV410:
	case DRM_FORMAT_YVU410:
	case DRM_FORMAT_YUV411:
	case DRM_FORMAT_YVU411:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV444:
	case DRM_FORMAT_YVU444:
		return 1;
	default:
		drm_fb_get_bpp_depth(format, &depth, &bpp);
		return bpp >> 3;
	}
}
EXPORT_SYMBOL(drm_format_plane_cpp);

/**
 * drm_format_horz_chroma_subsampling - get the horizontal chroma subsampling factor
 * @format: pixel format (DRM_FORMAT_*)
 *
 * Returns:
 * The horizontal chroma subsampling factor for the
 * specified pixel format.
 */
int drm_format_horz_chroma_subsampling(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_YUV411:
	case DRM_FORMAT_YVU411:
	case DRM_FORMAT_YUV410:
	case DRM_FORMAT_YVU410:
		return 4;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		return 2;
	default:
		return 1;
	}
}
EXPORT_SYMBOL(drm_format_horz_chroma_subsampling);

/**
 * drm_format_vert_chroma_subsampling - get the vertical chroma subsampling factor
 * @format: pixel format (DRM_FORMAT_*)
 *
 * Returns:
 * The vertical chroma subsampling factor for the
 * specified pixel format.
 */
int drm_format_vert_chroma_subsampling(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_YUV410:
	case DRM_FORMAT_YVU410:
		return 4;
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		return 2;
	default:
		return 1;
	}
}
EXPORT_SYMBOL(drm_format_vert_chroma_subsampling);

/**
 * drm_format_plane_width - width of the plane given the first plane
 * @width: width of the first plane
 * @format: pixel format
 * @plane: plane index
 *
 * Returns:
 * The width of @plane, given that the width of the first plane is @width.
 */
int drm_format_plane_width(int width, uint32_t format, int plane)
{
	if (plane >= drm_format_num_planes(format))
		return 0;

	if (plane == 0)
		return width;

	return width / drm_format_horz_chroma_subsampling(format);
}
EXPORT_SYMBOL(drm_format_plane_width);

/**
 * drm_format_plane_height - height of the plane given the first plane
 * @height: height of the first plane
 * @format: pixel format
 * @plane: plane index
 *
 * Returns:
 * The height of @plane, given that the height of the first plane is @height.
 */
int drm_format_plane_height(int height, uint32_t format, int plane)
{
	if (plane >= drm_format_num_planes(format))
		return 0;

	if (plane == 0)
		return height;

	return height / drm_format_vert_chroma_subsampling(format);
}
EXPORT_SYMBOL(drm_format_plane_height);
