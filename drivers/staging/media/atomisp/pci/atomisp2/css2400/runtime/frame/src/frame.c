#ifndef ISP2401
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#include "ia_css_frame.h"
#include <math_support.h>
#include "assert_support.h"
#include "ia_css_debug.h"
#include "isp.h"
#include "sh_css_internal.h"
#include "memory_access.h"


#define NV12_TILEY_TILE_WIDTH  128
#define NV12_TILEY_TILE_HEIGHT  32

/**************************************************************************
**	Static functions declarations
**************************************************************************/
static void frame_init_plane(struct ia_css_frame_plane *plane,
	unsigned int width,
	unsigned int stride,
	unsigned int height,
	unsigned int offset);

static void frame_init_single_plane(struct ia_css_frame *frame,
	struct ia_css_frame_plane *plane,
	unsigned int height,
	unsigned int subpixels_per_line,
	unsigned int bytes_per_pixel);

static void frame_init_raw_single_plane(
	struct ia_css_frame *frame,
	struct ia_css_frame_plane *plane,
	unsigned int height,
	unsigned int subpixels_per_line,
	unsigned int bits_per_pixel);

static void frame_init_mipi_plane(struct ia_css_frame *frame,
	struct ia_css_frame_plane *plane,
	unsigned int height,
	unsigned int subpixels_per_line,
	unsigned int bytes_per_pixel);

static void frame_init_nv_planes(struct ia_css_frame *frame,
				 unsigned int horizontal_decimation,
				 unsigned int vertical_decimation,
				 unsigned int bytes_per_element);

static void frame_init_yuv_planes(struct ia_css_frame *frame,
	unsigned int horizontal_decimation,
	unsigned int vertical_decimation,
	bool swap_uv,
	unsigned int bytes_per_element);

static void frame_init_rgb_planes(struct ia_css_frame *frame,
	unsigned int bytes_per_element);

static void frame_init_qplane6_planes(struct ia_css_frame *frame);

static enum ia_css_err frame_allocate_buffer_data(struct ia_css_frame *frame);

static enum ia_css_err frame_allocate_with_data(struct ia_css_frame **frame,
	unsigned int width,
	unsigned int height,
	enum ia_css_frame_format format,
	unsigned int padded_width,
	unsigned int raw_bit_depth,
	bool contiguous);

static struct ia_css_frame *frame_create(unsigned int width,
	unsigned int height,
	enum ia_css_frame_format format,
	unsigned int padded_width,
	unsigned int raw_bit_depth,
	bool contiguous,
	bool valid);

static unsigned
ia_css_elems_bytes_from_info(
	const struct ia_css_frame_info *info);

/**************************************************************************
**	CSS API functions, exposed by ia_css.h
**************************************************************************/

void ia_css_frame_zero(struct ia_css_frame *frame)
{
	assert(frame != NULL);
	mmgr_clear(frame->data, frame->data_bytes);
}

enum ia_css_err ia_css_frame_allocate_from_info(struct ia_css_frame **frame,
	const struct ia_css_frame_info *info)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
	if (frame == NULL || info == NULL)
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		      "ia_css_frame_allocate_from_info() enter:\n");
	err =
	    ia_css_frame_allocate(frame, info->res.width, info->res.height,
				  info->format, info->padded_width,
				  info->raw_bit_depth);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		      "ia_css_frame_allocate_from_info() leave:\n");
	return err;
}

enum ia_css_err ia_css_frame_allocate(struct ia_css_frame **frame,
	unsigned int width,
	unsigned int height,
	enum ia_css_frame_format format,
	unsigned int padded_width,
	unsigned int raw_bit_depth)
{
	enum ia_css_err err = IA_CSS_SUCCESS;

	if (frame == NULL || width == 0 || height == 0)
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
#ifndef ISP2401
	  "ia_css_frame_allocate() enter: width=%d, height=%d, format=%d\n",
	  width, height, format);
#else
	  "ia_css_frame_allocate() enter: width=%d, height=%d, format=%d, padded_width=%d, raw_bit_depth=%d\n",
	  width, height, format, padded_width, raw_bit_depth);
#endif

	err = frame_allocate_with_data(frame, width, height, format,
				       padded_width, raw_bit_depth, false);

#ifndef ISP2401
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		      "ia_css_frame_allocate() leave: frame=%p\n", *frame);
#else
	if ((*frame != NULL) && err == IA_CSS_SUCCESS)
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		      "ia_css_frame_allocate() leave: frame=%p, data(DDR address)=0x%x\n", *frame, (*frame)->data);
	else
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		      "ia_css_frame_allocate() leave: frame=%p, data(DDR address)=0x%x\n",
		      (void *)-1, (unsigned int)-1);
#endif

	return err;
}

enum ia_css_err ia_css_frame_map(struct ia_css_frame **frame,
	const struct ia_css_frame_info *info,
	const void *data,
	uint16_t attribute,
	void *context)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
	struct ia_css_frame *me;
	assert(frame != NULL);

	/* Create the frame structure */
	err = ia_css_frame_create_from_info(&me, info);

	if (err != IA_CSS_SUCCESS)
		return err;

	if (err == IA_CSS_SUCCESS) {
		/* use mmgr_mmap to map */
		me->data = (ia_css_ptr) mmgr_mmap(data,
						  me->data_bytes,
						  attribute, context);
		if (me->data == mmgr_NULL)
			err = IA_CSS_ERR_INVALID_ARGUMENTS;
	};

	if (err != IA_CSS_SUCCESS) {
		sh_css_free(me);
#ifndef ISP2401
		return err;
#else
		me = NULL;
#endif
	}

	*frame = me;

	return err;
}

enum ia_css_err ia_css_frame_create_from_info(struct ia_css_frame **frame,
	const struct ia_css_frame_info *info)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
	struct ia_css_frame *me;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_frame_create_from_info() enter:\n");
	if (frame == NULL || info == NULL) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"ia_css_frame_create_from_info() leave:"
			" invalid arguments\n");
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}

	me = frame_create(info->res.width,
		info->res.height,
		info->format,
		info->padded_width,
		info->raw_bit_depth,
		false,
		false);
	if (me == NULL) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"ia_css_frame_create_from_info() leave:"
			" frame create failed\n");
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
	}

	err = ia_css_frame_init_planes(me);

#ifndef ISP2401
	if (err == IA_CSS_SUCCESS)
		*frame = me;
	else
#else
	if (err != IA_CSS_SUCCESS) {
#endif
		sh_css_free(me);
#ifdef ISP2401
		me = NULL;
	}

	*frame = me;
#endif

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_frame_create_from_info() leave:\n");

	return err;
}

enum ia_css_err ia_css_frame_set_data(struct ia_css_frame *frame,
	const ia_css_ptr mapped_data,
	size_t data_bytes)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_frame_set_data() enter:\n");
	if (frame == NULL) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"ia_css_frame_set_data() leave: NULL frame\n");
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}

	/* If we are setting a valid data.
	 * Make sure that there is enough
	 * room for the expected frame format
	 */
	if ((mapped_data != mmgr_NULL) && (frame->data_bytes > data_bytes)) {
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"ia_css_frame_set_data() leave: invalid arguments\n");
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}

	frame->data = mapped_data;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, "ia_css_frame_set_data() leave:\n");

	return err;
}

enum ia_css_err ia_css_frame_allocate_contiguous(struct ia_css_frame **frame,
	unsigned int width,
	unsigned int height,
	enum ia_css_frame_format format,
	unsigned int padded_width,
	unsigned int raw_bit_depth)
{
	enum ia_css_err err = IA_CSS_SUCCESS;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_frame_allocate_contiguous() "
#ifndef ISP2401
		"enter: width=%d, height=%d, format=%d\n",
		width, height, format);
#else
		"enter: width=%d, height=%d, format=%d, padded_width=%d, raw_bit_depth=%d\n",
		width, height, format, padded_width, raw_bit_depth);
#endif

	err = frame_allocate_with_data(frame, width, height, format,
			padded_width, raw_bit_depth, true);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_frame_allocate_contiguous() leave: frame=%p\n",
		frame ? *frame : (void *)-1);

	return err;
}

enum ia_css_err ia_css_frame_allocate_contiguous_from_info(
	struct ia_css_frame **frame,
	const struct ia_css_frame_info *info)
{
	enum ia_css_err err = IA_CSS_SUCCESS;
	assert(frame != NULL);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_frame_allocate_contiguous_from_info() enter:\n");
	err = ia_css_frame_allocate_contiguous(frame,
						info->res.width,
						info->res.height,
						info->format,
						info->padded_width,
						info->raw_bit_depth);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_frame_allocate_contiguous_from_info() leave:\n");
	return err;
}

void ia_css_frame_free(struct ia_css_frame *frame)
{
	IA_CSS_ENTER_PRIVATE("frame = %p", frame);

	if (frame != NULL) {
		hmm_free(frame->data);
		sh_css_free(frame);
	}

	IA_CSS_LEAVE_PRIVATE("void");
}

/**************************************************************************
**	Module public functions
**************************************************************************/

enum ia_css_err ia_css_frame_check_info(const struct ia_css_frame_info *info)
{
	assert(info != NULL);
	if (info->res.width == 0 || info->res.height == 0)
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	return IA_CSS_SUCCESS;
}

enum ia_css_err ia_css_frame_init_planes(struct ia_css_frame *frame)
{
	assert(frame != NULL);

	switch (frame->info.format) {
	case IA_CSS_FRAME_FORMAT_MIPI:
		frame_init_mipi_plane(frame, &frame->planes.raw,
			frame->info.res.height,
			frame->info.padded_width,
			frame->info.raw_bit_depth <= 8 ? 1 : 2);
		break;
	case IA_CSS_FRAME_FORMAT_RAW_PACKED:
		frame_init_raw_single_plane(frame, &frame->planes.raw,
			frame->info.res.height,
			frame->info.padded_width,
			frame->info.raw_bit_depth);
		break;
	case IA_CSS_FRAME_FORMAT_RAW:
		frame_init_single_plane(frame, &frame->planes.raw,
			frame->info.res.height,
			frame->info.padded_width,
			frame->info.raw_bit_depth <= 8 ? 1 : 2);
		break;
	case IA_CSS_FRAME_FORMAT_RGB565:
		frame_init_single_plane(frame, &frame->planes.rgb,
			frame->info.res.height,
			frame->info.padded_width, 2);
		break;
	case IA_CSS_FRAME_FORMAT_RGBA888:
		frame_init_single_plane(frame, &frame->planes.rgb,
			frame->info.res.height,
			frame->info.padded_width * 4, 1);
		break;
	case IA_CSS_FRAME_FORMAT_PLANAR_RGB888:
		frame_init_rgb_planes(frame, 1);
		break;
		/* yuyv and uyvu have the same frame layout, only the data
		 * positioning differs.
		 */
	case IA_CSS_FRAME_FORMAT_YUYV:
	case IA_CSS_FRAME_FORMAT_UYVY:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_YUV420_8:
	case IA_CSS_FRAME_FORMAT_CSI_MIPI_LEGACY_YUV420_8:
		frame_init_single_plane(frame, &frame->planes.yuyv,
			frame->info.res.height,
			frame->info.padded_width * 2, 1);
		break;
	case IA_CSS_FRAME_FORMAT_YUV_LINE:
		/* Needs 3 extra lines to allow vf_pp prefetching */
		frame_init_single_plane(frame, &frame->planes.yuyv,
			frame->info.res.height * 3 / 2 + 3,
			frame->info.padded_width, 1);
		break;
	case IA_CSS_FRAME_FORMAT_NV11:
	  frame_init_nv_planes(frame, 4, 1, 1);
		break;
		/* nv12 and nv21 have the same frame layout, only the data
		 * positioning differs.
		 */
	case IA_CSS_FRAME_FORMAT_NV12:
	case IA_CSS_FRAME_FORMAT_NV21:
	case IA_CSS_FRAME_FORMAT_NV12_TILEY:
		frame_init_nv_planes(frame, 2, 2, 1);
		break;
	case IA_CSS_FRAME_FORMAT_NV12_16:
		frame_init_nv_planes(frame, 2, 2, 2);
		break;
		/* nv16 and nv61 have the same frame layout, only the data
		 * positioning differs.
		 */
	case IA_CSS_FRAME_FORMAT_NV16:
	case IA_CSS_FRAME_FORMAT_NV61:
		frame_init_nv_planes(frame, 2, 1, 1);
		break;
	case IA_CSS_FRAME_FORMAT_YUV420:
		frame_init_yuv_planes(frame, 2, 2, false, 1);
		break;
	case IA_CSS_FRAME_FORMAT_YUV422:
		frame_init_yuv_planes(frame, 2, 1, false, 1);
		break;
	case IA_CSS_FRAME_FORMAT_YUV444:
		frame_init_yuv_planes(frame, 1, 1, false, 1);
		break;
	case IA_CSS_FRAME_FORMAT_YUV420_16:
		frame_init_yuv_planes(frame, 2, 2, false, 2);
		break;
	case IA_CSS_FRAME_FORMAT_YUV422_16:
		frame_init_yuv_planes(frame, 2, 1, false, 2);
		break;
	case IA_CSS_FRAME_FORMAT_YV12:
		frame_init_yuv_planes(frame, 2, 2, true, 1);
		break;
	case IA_CSS_FRAME_FORMAT_YV16:
		frame_init_yuv_planes(frame, 2, 1, true, 1);
		break;
	case IA_CSS_FRAME_FORMAT_QPLANE6:
		frame_init_qplane6_planes(frame);
		break;
	case IA_CSS_FRAME_FORMAT_BINARY_8:
		frame_init_single_plane(frame, &frame->planes.binary.data,
			frame->info.res.height,
			frame->info.padded_width, 1);
		frame->planes.binary.size = 0;
		break;
	default:
		return IA_CSS_ERR_INVALID_ARGUMENTS;
	}
	return IA_CSS_SUCCESS;
}

void ia_css_frame_info_set_width(struct ia_css_frame_info *info,
	unsigned int width,
	unsigned int min_padded_width)
{
	unsigned int align;

	IA_CSS_ENTER_PRIVATE("info = %p,width = %d, minimum padded width = %d",
			     info, width, min_padded_width);
	if (info == NULL) {
		IA_CSS_ERROR("NULL input parameter");
		IA_CSS_LEAVE_PRIVATE("");
		return;
	}
	if (min_padded_width > width)
		align = min_padded_width;
	else
		align = width;

	info->res.width = width;
	/* frames with a U and V plane of 8 bits per pixel need to have
	   all planes aligned, this means double the alignment for the
	   Y plane if the horizontal decimation is 2. */
	if (info->format == IA_CSS_FRAME_FORMAT_YUV420 ||
	    info->format == IA_CSS_FRAME_FORMAT_YV12 ||
	    info->format == IA_CSS_FRAME_FORMAT_NV12 ||
	    info->format == IA_CSS_FRAME_FORMAT_NV21 ||
	    info->format == IA_CSS_FRAME_FORMAT_BINARY_8 ||
	    info->format == IA_CSS_FRAME_FORMAT_YUV_LINE)
		info->padded_width =
		    CEIL_MUL(align, 2 * HIVE_ISP_DDR_WORD_BYTES);
	else if (info->format == IA_CSS_FRAME_FORMAT_NV12_TILEY)
		info->padded_width = CEIL_MUL(align, NV12_TILEY_TILE_WIDTH);
	else if (info->format == IA_CSS_FRAME_FORMAT_RAW ||
		 info->format == IA_CSS_FRAME_FORMAT_RAW_PACKED)
		info->padded_width = CEIL_MUL(align, 2 * ISP_VEC_NELEMS);
	else {
		info->padded_width = CEIL_MUL(align, HIVE_ISP_DDR_WORD_BYTES);
	}
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_frame_info_set_format(struct ia_css_frame_info *info,
	enum ia_css_frame_format format)
{
	assert(info != NULL);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_frame_info_set_format() enter:\n");
	info->format = format;
}

void ia_css_frame_info_init(struct ia_css_frame_info *info,
	unsigned int width,
	unsigned int height,
	enum ia_css_frame_format format,
	unsigned int aligned)
{
	IA_CSS_ENTER_PRIVATE("info = %p, width = %d, height = %d, format = %d, aligned = %d",
			     info, width, height, format, aligned);
	if (info == NULL) {
		IA_CSS_ERROR("NULL input parameter");
		IA_CSS_LEAVE_PRIVATE("");
		return;
	}
	info->res.height = height;
	info->format     = format;
	ia_css_frame_info_set_width(info, width, aligned);
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_frame_free_multiple(unsigned int num_frames,
	struct ia_css_frame **frames_array)
{
	unsigned int i;
	for (i = 0; i < num_frames; i++) {
		if (frames_array[i]) {
			ia_css_frame_free(frames_array[i]);
			frames_array[i] = NULL;
		}
	}
}

enum ia_css_err ia_css_frame_allocate_with_buffer_size(
	struct ia_css_frame **frame,
	const unsigned int buffer_size_bytes,
	const bool contiguous)
{
	/* AM: Body coppied from frame_allocate_with_data(). */
	enum ia_css_err err;
	struct ia_css_frame *me = frame_create(0, 0,
		IA_CSS_FRAME_FORMAT_NUM,/* Not valid format yet */
		0, 0, contiguous, false);

	if (me == NULL)
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;

	/* Get the data size */
	me->data_bytes = buffer_size_bytes;

	err = frame_allocate_buffer_data(me);

	if (err != IA_CSS_SUCCESS) {
		sh_css_free(me);
#ifndef ISP2401
		return err;
#else
		me = NULL;
#endif
	}

	*frame = me;

	return err;
}

bool ia_css_frame_info_is_same_resolution(
	const struct ia_css_frame_info *info_a,
	const struct ia_css_frame_info *info_b)
{
	if (!info_a || !info_b)
		return false;
	return (info_a->res.width == info_b->res.width) &&
	    (info_a->res.height == info_b->res.height);
}

bool ia_css_frame_is_same_type(const struct ia_css_frame *frame_a,
	const struct ia_css_frame *frame_b)
{
	bool is_equal = false;
	const struct ia_css_frame_info *info_a = &frame_a->info,
	    *info_b = &frame_b->info;

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		      "ia_css_frame_is_same_type() enter:\n");

	if (!info_a || !info_b)
		return false;
	if (info_a->format != info_b->format)
		return false;
	if (info_a->padded_width != info_b->padded_width)
		return false;
	is_equal = ia_css_frame_info_is_same_resolution(info_a, info_b);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		      "ia_css_frame_is_same_type() leave:\n");

	return is_equal;
}

void
ia_css_dma_configure_from_info(
	struct dma_port_config *config,
	const struct ia_css_frame_info *info)
{
	unsigned is_raw_packed = info->format == IA_CSS_FRAME_FORMAT_RAW_PACKED;
	unsigned bits_per_pixel = is_raw_packed ? info->raw_bit_depth : ia_css_elems_bytes_from_info(info)*8;
	unsigned pix_per_ddrword = HIVE_ISP_DDR_WORD_BITS / bits_per_pixel;
	unsigned words_per_line = CEIL_DIV(info->padded_width, pix_per_ddrword);
	unsigned elems_b = pix_per_ddrword;

	config->stride = HIVE_ISP_DDR_WORD_BYTES * words_per_line;
	config->elems  = (uint8_t)elems_b;
	config->width  = (uint16_t)info->res.width;
	config->crop   = 0;
	assert(config->width <= info->padded_width);
}

/**************************************************************************
**	Static functions
**************************************************************************/

static void frame_init_plane(struct ia_css_frame_plane *plane,
	unsigned int width,
	unsigned int stride,
	unsigned int height,
	unsigned int offset)
{
	plane->height = height;
	plane->width = width;
	plane->stride = stride;
	plane->offset = offset;
}

static void frame_init_single_plane(struct ia_css_frame *frame,
	struct ia_css_frame_plane *plane,
	unsigned int height,
	unsigned int subpixels_per_line,
	unsigned int bytes_per_pixel)
{
	unsigned int stride;

	stride = subpixels_per_line * bytes_per_pixel;
	/* Frame height needs to be even number - needed by hw ISYS2401
	   In case of odd number, round up to even.
	   Images won't be impacted by this round up,
	   only needed by jpeg/embedded data.
	   As long as buffer allocation and release are using data_bytes,
	   there won't be memory leak. */
	frame->data_bytes = stride * CEIL_MUL2(height, 2);
	frame_init_plane(plane, subpixels_per_line, stride, height, 0);
	return;
}

static void frame_init_raw_single_plane(
	struct ia_css_frame *frame,
	struct ia_css_frame_plane *plane,
	unsigned int height,
	unsigned int subpixels_per_line,
	unsigned int bits_per_pixel)
{
	unsigned int stride;
	assert(frame != NULL);

	stride = HIVE_ISP_DDR_WORD_BYTES *
			CEIL_DIV(subpixels_per_line,
				HIVE_ISP_DDR_WORD_BITS / bits_per_pixel);
	frame->data_bytes = stride * height;
	frame_init_plane(plane, subpixels_per_line, stride, height, 0);
	return;
}

static void frame_init_mipi_plane(struct ia_css_frame *frame,
	struct ia_css_frame_plane *plane,
	unsigned int height,
	unsigned int subpixels_per_line,
	unsigned int bytes_per_pixel)
{
	unsigned int stride;

	stride = subpixels_per_line * bytes_per_pixel;
	frame->data_bytes = 8388608; /* 8*1024*1024 */
	frame->valid = false;
	frame->contiguous = true;
	frame_init_plane(plane, subpixels_per_line, stride, height, 0);
	return;
}

static void frame_init_nv_planes(struct ia_css_frame *frame,
				 unsigned int horizontal_decimation,
				 unsigned int vertical_decimation,
				 unsigned int bytes_per_element)
{
	unsigned int y_width = frame->info.padded_width;
	unsigned int y_height = frame->info.res.height;
	unsigned int uv_width;
	unsigned int uv_height;
	unsigned int y_bytes;
	unsigned int uv_bytes;
	unsigned int y_stride;
	unsigned int uv_stride;

	assert(horizontal_decimation != 0 && vertical_decimation != 0);

	uv_width = 2 * (y_width / horizontal_decimation);
	uv_height = y_height / vertical_decimation;

	if (IA_CSS_FRAME_FORMAT_NV12_TILEY == frame->info.format) {
		y_width   = CEIL_MUL(y_width,   NV12_TILEY_TILE_WIDTH);
		uv_width  = CEIL_MUL(uv_width,  NV12_TILEY_TILE_WIDTH);
		y_height  = CEIL_MUL(y_height,  NV12_TILEY_TILE_HEIGHT);
		uv_height = CEIL_MUL(uv_height, NV12_TILEY_TILE_HEIGHT);
	}

	y_stride = y_width * bytes_per_element;
	uv_stride = uv_width * bytes_per_element;
	y_bytes = y_stride * y_height;
	uv_bytes = uv_stride * uv_height;

	frame->data_bytes = y_bytes + uv_bytes;
	frame_init_plane(&frame->planes.nv.y, y_width, y_stride, y_height, 0);
	frame_init_plane(&frame->planes.nv.uv, uv_width,
			 uv_stride, uv_height, y_bytes);
	return;
}

static void frame_init_yuv_planes(struct ia_css_frame *frame,
	unsigned int horizontal_decimation,
	unsigned int vertical_decimation,
	bool swap_uv,
	unsigned int bytes_per_element)
{
	unsigned int y_width = frame->info.padded_width,
	    y_height = frame->info.res.height,
	    uv_width = y_width / horizontal_decimation,
	    uv_height = y_height / vertical_decimation,
	    y_stride, y_bytes, uv_bytes, uv_stride;

	y_stride = y_width * bytes_per_element;
	uv_stride = uv_width * bytes_per_element;
	y_bytes = y_stride * y_height;
	uv_bytes = uv_stride * uv_height;

	frame->data_bytes = y_bytes + 2 * uv_bytes;
	frame_init_plane(&frame->planes.yuv.y, y_width, y_stride, y_height, 0);
	if (swap_uv) {
		frame_init_plane(&frame->planes.yuv.v, uv_width, uv_stride,
				 uv_height, y_bytes);
		frame_init_plane(&frame->planes.yuv.u, uv_width, uv_stride,
				 uv_height, y_bytes + uv_bytes);
	} else {
		frame_init_plane(&frame->planes.yuv.u, uv_width, uv_stride,
				 uv_height, y_bytes);
		frame_init_plane(&frame->planes.yuv.v, uv_width, uv_stride,
				 uv_height, y_bytes + uv_bytes);
	}
	return;
}

static void frame_init_rgb_planes(struct ia_css_frame *frame,
	unsigned int bytes_per_element)
{
	unsigned int width = frame->info.res.width,
	    height = frame->info.res.height, stride, bytes;

	stride = width * bytes_per_element;
	bytes = stride * height;
	frame->data_bytes = 3 * bytes;
	frame_init_plane(&frame->planes.planar_rgb.r, width, stride, height, 0);
	frame_init_plane(&frame->planes.planar_rgb.g,
			 width, stride, height, 1 * bytes);
	frame_init_plane(&frame->planes.planar_rgb.b,
			 width, stride, height, 2 * bytes);
	return;
}

static void frame_init_qplane6_planes(struct ia_css_frame *frame)
{
	unsigned int width = frame->info.padded_width / 2,
	    height = frame->info.res.height / 2, bytes, stride;

	stride = width * 2;
	bytes = stride * height;

	frame->data_bytes = 6 * bytes;
	frame_init_plane(&frame->planes.plane6.r,
			 width, stride, height, 0 * bytes);
	frame_init_plane(&frame->planes.plane6.r_at_b,
			 width, stride, height, 1 * bytes);
	frame_init_plane(&frame->planes.plane6.gr,
			 width, stride, height, 2 * bytes);
	frame_init_plane(&frame->planes.plane6.gb,
			 width, stride, height, 3 * bytes);
	frame_init_plane(&frame->planes.plane6.b,
			 width, stride, height, 4 * bytes);
	frame_init_plane(&frame->planes.plane6.b_at_r,
			 width, stride, height, 5 * bytes);
	return;
}

static enum ia_css_err frame_allocate_buffer_data(struct ia_css_frame *frame)
{
#ifdef ISP2401
	IA_CSS_ENTER_LEAVE_PRIVATE("frame->data_bytes=%d\n", frame->data_bytes);
#endif
	frame->data = mmgr_alloc_attr(frame->data_bytes,
				      frame->contiguous ?
				      MMGR_ATTRIBUTE_CONTIGUOUS :
				      MMGR_ATTRIBUTE_DEFAULT);

	if (frame->data == mmgr_NULL)
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
	return IA_CSS_SUCCESS;
}

static enum ia_css_err frame_allocate_with_data(struct ia_css_frame **frame,
	unsigned int width,
	unsigned int height,
	enum ia_css_frame_format format,
	unsigned int padded_width,
	unsigned int raw_bit_depth,
	bool contiguous)
{
	enum ia_css_err err;
	struct ia_css_frame *me = frame_create(width,
		height,
		format,
		padded_width,
		raw_bit_depth,
		contiguous,
		true);

	if (me == NULL)
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;

	err = ia_css_frame_init_planes(me);

	if (err == IA_CSS_SUCCESS)
		err = frame_allocate_buffer_data(me);

	if (err != IA_CSS_SUCCESS) {
		sh_css_free(me);
#ifndef ISP2401
		return err;
#else
		me = NULL;
#endif
	}

	*frame = me;

	return err;
}

static struct ia_css_frame *frame_create(unsigned int width,
	unsigned int height,
	enum ia_css_frame_format format,
	unsigned int padded_width,
	unsigned int raw_bit_depth,
	bool contiguous,
	bool valid)
{
	struct ia_css_frame *me = sh_css_malloc(sizeof(*me));

	if (me == NULL)
		return NULL;

	memset(me, 0, sizeof(*me));
	me->info.res.width = width;
	me->info.res.height = height;
	me->info.format = format;
	me->info.padded_width = padded_width;
	me->info.raw_bit_depth = raw_bit_depth;
	me->contiguous = contiguous;
	me->valid = valid;
	me->data_bytes = 0;
	me->data = mmgr_NULL;
	/* To indicate it is not valid frame. */
	me->dynamic_queue_id = (int)SH_CSS_INVALID_QUEUE_ID;
	me->buf_type = IA_CSS_BUFFER_TYPE_INVALID;

	return me;
}

static unsigned
ia_css_elems_bytes_from_info(const struct ia_css_frame_info *info)
{
	if (info->format == IA_CSS_FRAME_FORMAT_RGB565)
		return 2; /* bytes per pixel */
	if (info->format == IA_CSS_FRAME_FORMAT_YUV420_16)
		return 2; /* bytes per pixel */
	if (info->format == IA_CSS_FRAME_FORMAT_YUV422_16)
		return 2; /* bytes per pixel */
	/* Note: Essentially NV12_16 is a 2 bytes per pixel format, this return value is used
	 * to configure DMA for the output buffer,
	 * At least in SKC this data is overwriten by isp_output_init.sp.c except for elements(elems),
	 * which is configured from this return value,
	 * NV12_16 is implemented by a double buffer of 8 bit elements hence elems should be configured as 8 */
	if (info->format == IA_CSS_FRAME_FORMAT_NV12_16)
		return 1; /* bytes per pixel */

	if (info->format == IA_CSS_FRAME_FORMAT_RAW
		|| (info->format == IA_CSS_FRAME_FORMAT_RAW_PACKED)) {
		if (info->raw_bit_depth)
			return CEIL_DIV(info->raw_bit_depth,8);
		else
			return 2; /* bytes per pixel */
	}
	if (info->format == IA_CSS_FRAME_FORMAT_PLANAR_RGB888)
		return 3; /* bytes per pixel */
	if (info->format == IA_CSS_FRAME_FORMAT_RGBA888)
		return 4; /* bytes per pixel */
	if (info->format == IA_CSS_FRAME_FORMAT_QPLANE6)
		return 2; /* bytes per pixel */
	return 1; /* Default is 1 byte per pixel */
}

void ia_css_frame_info_to_frame_sp_info(
	struct ia_css_frame_sp_info *to,
	const struct ia_css_frame_info *from)
{
	ia_css_resolution_to_sp_resolution(&to->res, &from->res);
	to->padded_width = (uint16_t)from->padded_width;
	to->format = (uint8_t)from->format;
	to->raw_bit_depth = (uint8_t)from->raw_bit_depth;
	to->raw_bayer_order = from->raw_bayer_order;
}

void ia_css_resolution_to_sp_resolution(
	struct ia_css_sp_resolution *to,
	const struct ia_css_resolution *from)
{
	to->width  = (uint16_t)from->width;
	to->height = (uint16_t)from->height;
}
#ifdef ISP2401

enum ia_css_err
ia_css_frame_find_crop_resolution(const struct ia_css_resolution *in_res,
	const struct ia_css_resolution *out_res,
	struct ia_css_resolution *crop_res)
{
	uint32_t wd_even_ceil, ht_even_ceil;
	uint32_t in_ratio, out_ratio;

	if ((in_res == NULL) || (out_res == NULL) || (crop_res == NULL))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	IA_CSS_ENTER_PRIVATE("in(%ux%u) -> out(%ux%u)", in_res->width,
		in_res->height, out_res->width, out_res->height);

	if ((in_res->width == 0)
		|| (in_res->height == 0)
		|| (out_res->width == 0)
		|| (out_res->height == 0))
			return IA_CSS_ERR_INVALID_ARGUMENTS;

	if ((out_res->width > in_res->width) ||
		 (out_res->height > in_res->height))
			return IA_CSS_ERR_INVALID_ARGUMENTS;

	/* If aspect ratio (width/height) of out_res is higher than the aspect
	 * ratio of the in_res, then we crop vertically, otherwise we crop
	 * horizontally.
	 */
	in_ratio = in_res->width * out_res->height;
	out_ratio = out_res->width * in_res->height;

	if (in_ratio == out_ratio) {
		crop_res->width = in_res->width;
		crop_res->height = in_res->height;
	} else if (out_ratio > in_ratio) {
		crop_res->width = in_res->width;
		crop_res->height = ROUND_DIV(out_res->height * crop_res->width,
			out_res->width);
	} else {
		crop_res->height = in_res->height;
		crop_res->width = ROUND_DIV(out_res->width * crop_res->height,
			out_res->height);
	}

	/* Round new (cropped) width and height to an even number.
	 * binarydesc_calculate_bds_factor is such that we should consider as
	 * much of the input as possible. This is different only when we end up
	 * with an odd number in the last step. So, we take the next even number
	 * if it falls within the input, otherwise take the previous even no.
	 */
	wd_even_ceil = EVEN_CEIL(crop_res->width);
	ht_even_ceil = EVEN_CEIL(crop_res->height);
	if ((wd_even_ceil > in_res->width) || (ht_even_ceil > in_res->height)) {
		crop_res->width = EVEN_FLOOR(crop_res->width);
		crop_res->height = EVEN_FLOOR(crop_res->height);
	} else {
		crop_res->width = wd_even_ceil;
		crop_res->height = ht_even_ceil;
	}

	IA_CSS_LEAVE_PRIVATE("in(%ux%u) -> out(%ux%u)", crop_res->width,
		crop_res->height, out_res->width, out_res->height);
	return IA_CSS_SUCCESS;
}
#endif
