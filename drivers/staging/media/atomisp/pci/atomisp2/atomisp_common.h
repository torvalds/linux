/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef	__ATOMISP_COMMON_H__
#define	__ATOMISP_COMMON_H__

#include "../../include/linux/atomisp.h"

#include <linux/v4l2-mediabus.h>

#include <media/videobuf-core.h>

#include "atomisp_compat.h"

#include "ia_css.h"

extern int dbg_level;
extern int dbg_func;
extern int mipicsi_flag;
extern int pad_w;
extern int pad_h;

#define CSS_DTRACE_VERBOSITY_LEVEL	5	/* Controls trace verbosity */
#define CSS_DTRACE_VERBOSITY_TIMEOUT	9	/* Verbosity on ISP timeout */
#define MRFLD_MAX_ZOOM_FACTOR	1024
#ifdef ISP2401
#define ATOMISP_CSS_ISP_PIPE_VERSION_2_2    0
#define ATOMISP_CSS_ISP_PIPE_VERSION_2_7    1
#endif

#define IS_ISP2401(isp)							\
	(((isp)->media_dev.hw_revision & ATOMISP_HW_REVISION_MASK)	\
	 >= (ATOMISP_HW_REVISION_ISP2401_LEGACY << ATOMISP_HW_REVISION_SHIFT))

struct atomisp_format_bridge {
	unsigned int pixelformat;
	unsigned int depth;
	u32 mbus_code;
	enum atomisp_css_frame_format sh_fmt;
	unsigned char description[32];	/* the same as struct v4l2_fmtdesc */
	bool planar;
};

struct atomisp_fmt {
	u32 pixelformat;
	u32 depth;
	u32 bytesperline;
	u32 framesize;
	u32 imagesize;
	u32 width;
	u32 height;
	u32 bayer_order;
};

struct atomisp_buffer {
	struct videobuf_buffer	vb;
};

#endif
