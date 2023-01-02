/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __IA_CSS_FRAME_PUBLIC_H
#define __IA_CSS_FRAME_PUBLIC_H

/* @file
 * This file contains structs to describe various frame-formats supported by the ISP.
 */

#include <media/videobuf2-v4l2.h>
#include <type_support.h>
#include "ia_css_err.h"
#include "ia_css_types.h"
#include "ia_css_frame_format.h"
#include "ia_css_buffer.h"

/* For RAW input, the bayer order needs to be specified separately. There
 *  are 4 possible orders. The name is constructed by taking the first two
 *  colors on the first line and the first two colors from the second line.
 */
enum ia_css_bayer_order {
	IA_CSS_BAYER_ORDER_GRBG, /** GRGRGRGRGR .. BGBGBGBGBG */
	IA_CSS_BAYER_ORDER_RGGB, /** RGRGRGRGRG .. GBGBGBGBGB */
	IA_CSS_BAYER_ORDER_BGGR, /** BGBGBGBGBG .. GRGRGRGRGR */
	IA_CSS_BAYER_ORDER_GBRG, /** GBGBGBGBGB .. RGRGRGRGRG */
};

#define IA_CSS_BAYER_ORDER_NUM (IA_CSS_BAYER_ORDER_GBRG + 1)

/* Frame plane structure. This describes one plane in an image
 *  frame buffer.
 */
struct ia_css_frame_plane {
	unsigned int height; /** height of a plane in lines */
	unsigned int width;  /** width of a line, in DMA elements, note that
				  for RGB565 the three subpixels are stored in
				  one element. For all other formats this is
				  the number of subpixels per line. */
	unsigned int stride; /** stride of a line in bytes */
	unsigned int offset; /** offset in bytes to start of frame data.
				  offset is wrt data field in ia_css_frame */
};

/* Binary "plane". This is used to story binary streams such as jpeg
 *  images. This is not actually a real plane.
 */
struct ia_css_frame_binary_plane {
	unsigned int		  size; /** number of bytes in the stream */
	struct ia_css_frame_plane data; /** plane */
};

/* Container for planar YUV frames. This contains 3 planes.
 */
struct ia_css_frame_yuv_planes {
	struct ia_css_frame_plane y; /** Y plane */
	struct ia_css_frame_plane u; /** U plane */
	struct ia_css_frame_plane v; /** V plane */
};

/* Container for semi-planar YUV frames.
  */
struct ia_css_frame_nv_planes {
	struct ia_css_frame_plane y;  /** Y plane */
	struct ia_css_frame_plane uv; /** UV plane */
};

/* Container for planar RGB frames. Each color has its own plane.
 */
struct ia_css_frame_rgb_planes {
	struct ia_css_frame_plane r; /** Red plane */
	struct ia_css_frame_plane g; /** Green plane */
	struct ia_css_frame_plane b; /** Blue plane */
};

/* Container for 6-plane frames. These frames are used internally
 *  in the advanced ISP only.
 */
struct ia_css_frame_plane6_planes {
	struct ia_css_frame_plane r;	  /** Red plane */
	struct ia_css_frame_plane r_at_b; /** Red at blue plane */
	struct ia_css_frame_plane gr;	  /** Red-green plane */
	struct ia_css_frame_plane gb;	  /** Blue-green plane */
	struct ia_css_frame_plane b;	  /** Blue plane */
	struct ia_css_frame_plane b_at_r; /** Blue at red plane */
};

/* Crop info struct - stores the lines to be cropped in isp */
struct ia_css_crop_info {
	/* the final start column and start line
	 * sum of lines to be cropped + bayer offset
	 */
	unsigned int start_column;
	unsigned int start_line;
};

/* Frame info struct. This describes the contents of an image frame buffer.
  */
struct ia_css_frame_info {
	struct ia_css_resolution res; /** Frame resolution (valid data) */
	unsigned int padded_width; /** stride of line in memory (in pixels) */
	enum ia_css_frame_format format; /** format of the frame data */
	unsigned int raw_bit_depth; /** number of valid bits per pixel,
					 only valid for RAW bayer frames */
	enum ia_css_bayer_order raw_bayer_order; /** bayer order, only valid
						      for RAW bayer frames */
	/* the params below are computed based on bayer_order
	 * we can remove the raw_bayer_order if it is redundant
	 * keeping it for now as bxt and fpn code seem to use it
	 */
	struct ia_css_crop_info crop_info;
};

#define IA_CSS_BINARY_DEFAULT_FRAME_INFO { \
	.format			= IA_CSS_FRAME_FORMAT_NUM,  \
	.raw_bayer_order	= IA_CSS_BAYER_ORDER_NUM, \
}

/**
 *  Specifies the DVS loop delay in "frame periods"
 */
enum ia_css_frame_delay {
	IA_CSS_FRAME_DELAY_0, /** Frame delay = 0 */
	IA_CSS_FRAME_DELAY_1, /** Frame delay = 1 */
	IA_CSS_FRAME_DELAY_2  /** Frame delay = 2 */
};

enum ia_css_frame_flash_state {
	IA_CSS_FRAME_FLASH_STATE_NONE,
	IA_CSS_FRAME_FLASH_STATE_PARTIAL,
	IA_CSS_FRAME_FLASH_STATE_FULL
};

/* Frame structure. This structure describes an image buffer or frame.
 *  This is the main structure used for all input and output images.
 */
struct ia_css_frame {
	/*
	 * The videobuf2 core will allocate buffers including room for private
	 * data (the rest of struct ia_css_frame). The vb2_v4l2_buffer must
	 * be the first member for this to work!
	 * Note the atomisp code also uses ia_css_frame-s which are not used
	 * as v4l2-buffers in some places. In this case the vb2 member will
	 * be unused.
	 */
	struct vb2_v4l2_buffer vb;
	/* List-head for linking into the activeq or buffers_waiting_for_param list */
	struct list_head queue;
	struct ia_css_frame_info frame_info; /** info struct describing the frame */
	ia_css_ptr   data;	       /** pointer to start of image data */
	unsigned int data_bytes;       /** size of image data in bytes */
	/* LA: move this to ia_css_buffer */
	/*
	 * -1 if data address is static during life time of pipeline
	 * >=0 if data address can change per pipeline/frame iteration
	 *     index to dynamic data: ia_css_frame_in, ia_css_frame_out
	 *                            ia_css_frame_out_vf
	 *     index to host-sp queue id: queue_0, queue_1 etc.
	 */
	int dynamic_queue_id;
	/*
	 * if it is dynamic frame, buf_type indicates which buffer type it
	 * should use for event generation. we have this because in vf_pp
	 * binary, we use output port, but we expect VF_OUTPUT_DONE event
	 */
	enum ia_css_buffer_type buf_type;
	enum ia_css_frame_flash_state flash_state;
	unsigned int exp_id;
	/** exposure id, see ia_css_event_public.h for more detail */
	u32 isp_config_id; /** Unique ID to track which config was actually applied to a particular frame */
	bool valid; /** First video output frame is not valid */
	union {
		unsigned int	_initialisation_dummy;
		struct ia_css_frame_plane raw;
		struct ia_css_frame_plane rgb;
		struct ia_css_frame_rgb_planes planar_rgb;
		struct ia_css_frame_plane yuyv;
		struct ia_css_frame_yuv_planes yuv;
		struct ia_css_frame_nv_planes nv;
		struct ia_css_frame_plane6_planes plane6;
		struct ia_css_frame_binary_plane binary;
	} planes; /** frame planes, select the right one based on
		       info.format */
};

#define vb_to_frame(vb2) \
	container_of(to_vb2_v4l2_buffer(vb2), struct ia_css_frame, vb)

#define DEFAULT_FRAME { \
	.frame_info		= IA_CSS_BINARY_DEFAULT_FRAME_INFO, \
	.dynamic_queue_id	= SH_CSS_INVALID_QUEUE_ID, \
	.buf_type		= IA_CSS_BUFFER_TYPE_INVALID, \
	.flash_state		= IA_CSS_FRAME_FLASH_STATE_NONE, \
}

/* @brief Allocate a CSS frame structure
 *
 * @param	frame		The allocated frame.
 * @param	width		The width (in pixels) of the frame.
 * @param	height		The height (in lines) of the frame.
 * @param	format		The frame format.
 * @param	stride		The padded stride, in pixels.
 * @param	raw_bit_depth	The raw bit depth, in bits.
 * @return			The error code.
 *
 * Allocate a CSS frame structure. The memory for the frame data will be
 * allocated in the CSS address space.
 */
int
ia_css_frame_allocate(struct ia_css_frame **frame,
		      unsigned int width,
		      unsigned int height,
		      enum ia_css_frame_format format,
		      unsigned int stride,
		      unsigned int raw_bit_depth);

/* @brief Initialize a CSS frame structure using a frame info structure.
 *
 * @param	frame	The allocated frame.
 * @param[in]	info	The frame info structure.
 * @return		The error code.
 *
 * Initialize a frame using the resolution and format from a frame info struct.
 */
int ia_css_frame_init_from_info(struct ia_css_frame *frame,
				const struct ia_css_frame_info *info);

/* @brief Allocate a CSS frame structure using a frame info structure.
 *
 * @param	frame	The allocated frame.
 * @param[in]	info	The frame info structure.
 * @return		The error code.
 *
 * Allocate a frame using the resolution and format from a frame info struct.
 * This is a convenience function, implemented on top of
 * ia_css_frame_allocate().
 */
int
ia_css_frame_allocate_from_info(struct ia_css_frame **frame,
				const struct ia_css_frame_info *info);
/* @brief Free a CSS frame structure.
 *
 * @param[in]	frame	Pointer to the frame.
 * @return	None
 *
 * Free a CSS frame structure. This will free both the frame structure
 * and the pixel data pointer contained within the frame structure.
 */
void
ia_css_frame_free(struct ia_css_frame *frame);

static inline const struct ia_css_frame_info *
ia_css_frame_get_info(const struct ia_css_frame *frame)
{
	return frame ? &frame->frame_info : NULL;
}

#endif /* __IA_CSS_FRAME_PUBLIC_H */
