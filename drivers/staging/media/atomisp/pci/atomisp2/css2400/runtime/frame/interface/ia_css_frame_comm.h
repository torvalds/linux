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

#ifndef __IA_CSS_FRAME_COMM_H__
#define __IA_CSS_FRAME_COMM_H__

#include "type_support.h"
#include "platform_support.h"
#include "runtime/bufq/interface/ia_css_bufq_comm.h"
#include <system_types.h>	 /* hrt_vaddress */

/*
 * These structs are derived from structs defined in ia_css_types.h
 * (just take out the "_sp" from the struct name to get the "original")
 * All the fields that are not needed by the SP are removed.
 */
struct ia_css_frame_sp_plane {
	unsigned int offset;	/* offset in bytes to start of frame data */
				/* offset is wrt data in sh_css_sp_sp_frame */
};

struct ia_css_frame_sp_binary_plane {
	unsigned int size;
	struct ia_css_frame_sp_plane data;
};

struct ia_css_frame_sp_yuv_planes {
	struct ia_css_frame_sp_plane y;
	struct ia_css_frame_sp_plane u;
	struct ia_css_frame_sp_plane v;
};

struct ia_css_frame_sp_nv_planes {
	struct ia_css_frame_sp_plane y;
	struct ia_css_frame_sp_plane uv;
};

struct ia_css_frame_sp_rgb_planes {
	struct ia_css_frame_sp_plane r;
	struct ia_css_frame_sp_plane g;
	struct ia_css_frame_sp_plane b;
};

struct ia_css_frame_sp_plane6 {
	struct ia_css_frame_sp_plane r;
	struct ia_css_frame_sp_plane r_at_b;
	struct ia_css_frame_sp_plane gr;
	struct ia_css_frame_sp_plane gb;
	struct ia_css_frame_sp_plane b;
	struct ia_css_frame_sp_plane b_at_r;
};

struct ia_css_sp_resolution {
	uint16_t width;		/* width of valid data in pixels */
	uint16_t height;	/* Height of valid data in lines */
};

/*
 * Frame info struct. This describes the contents of an image frame buffer.
 */
struct ia_css_frame_sp_info {
	struct ia_css_sp_resolution res;
	uint16_t padded_width;		/* stride of line in memory
					(in pixels) */
	unsigned char format;		/* format of the frame data */
	unsigned char raw_bit_depth;	/* number of valid bits per pixel,
					only valid for RAW bayer frames */
	unsigned char raw_bayer_order;	/* bayer order, only valid
					for RAW bayer frames */
	unsigned char padding[3];	/* Extend to 32 bit multiple */
};

struct ia_css_buffer_sp {
	union {
		hrt_vaddress xmem_addr;
		enum sh_css_queue_id queue_id;
	} buf_src;
	enum ia_css_buffer_type buf_type;
};

struct ia_css_frame_sp {
	struct ia_css_frame_sp_info info;
	struct ia_css_buffer_sp buf_attr;
	union {
		struct ia_css_frame_sp_plane raw;
		struct ia_css_frame_sp_plane rgb;
		struct ia_css_frame_sp_rgb_planes planar_rgb;
		struct ia_css_frame_sp_plane yuyv;
		struct ia_css_frame_sp_yuv_planes yuv;
		struct ia_css_frame_sp_nv_planes nv;
		struct ia_css_frame_sp_plane6 plane6;
		struct ia_css_frame_sp_binary_plane binary;
	} planes;
};

void ia_css_frame_info_to_frame_sp_info(
	struct ia_css_frame_sp_info *sp_info,
	const struct ia_css_frame_info *info);

void ia_css_resolution_to_sp_resolution(
	struct ia_css_sp_resolution *sp_info,
	const struct ia_css_resolution *info);

#endif /*__IA_CSS_FRAME_COMM_H__*/

