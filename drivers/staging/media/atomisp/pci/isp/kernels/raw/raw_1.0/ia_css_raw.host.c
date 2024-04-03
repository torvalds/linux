// SPDX-License-Identifier: GPL-2.0
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

#include "ia_css_frame.h"
#include "ia_css_types.h"
#include "sh_css_defs.h"
#include "ia_css_debug.h"
#include "assert_support.h"
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"
#include "isp.h"
#include "isp/modes/interface/isp_types.h"

#include "ia_css_raw.host.h"

static const struct ia_css_raw_configuration default_config = {
	.pipe = (struct sh_css_sp_pipeline *)NULL,
};

/* MW: These areMIPI / ISYS properties, not camera function properties */
static enum sh_stream_format
css2isp_stream_format(enum atomisp_input_format from) {
	switch (from)
	{
	case ATOMISP_INPUT_FORMAT_YUV420_8_LEGACY:
				return sh_stream_format_yuv420_legacy;
	case ATOMISP_INPUT_FORMAT_YUV420_8:
	case ATOMISP_INPUT_FORMAT_YUV420_10:
	case ATOMISP_INPUT_FORMAT_YUV420_16:
		return sh_stream_format_yuv420;
	case ATOMISP_INPUT_FORMAT_YUV422_8:
	case ATOMISP_INPUT_FORMAT_YUV422_10:
	case ATOMISP_INPUT_FORMAT_YUV422_16:
		return sh_stream_format_yuv422;
	case ATOMISP_INPUT_FORMAT_RGB_444:
	case ATOMISP_INPUT_FORMAT_RGB_555:
	case ATOMISP_INPUT_FORMAT_RGB_565:
	case ATOMISP_INPUT_FORMAT_RGB_666:
	case ATOMISP_INPUT_FORMAT_RGB_888:
		return sh_stream_format_rgb;
	case ATOMISP_INPUT_FORMAT_RAW_6:
	case ATOMISP_INPUT_FORMAT_RAW_7:
	case ATOMISP_INPUT_FORMAT_RAW_8:
	case ATOMISP_INPUT_FORMAT_RAW_10:
	case ATOMISP_INPUT_FORMAT_RAW_12:
	case ATOMISP_INPUT_FORMAT_RAW_14:
	case ATOMISP_INPUT_FORMAT_RAW_16:
		return sh_stream_format_raw;
	case ATOMISP_INPUT_FORMAT_BINARY_8:
	default:
		return sh_stream_format_raw;
	}
}

int ia_css_raw_config(struct sh_css_isp_raw_isp_config *to,
		      const struct ia_css_raw_configuration  *from,
		      unsigned int size)
{
	unsigned int elems_a = ISP_VEC_NELEMS;
	const struct ia_css_frame_info *in_info = from->in_info;
	const struct ia_css_frame_info *internal_info = from->internal_info;
	int ret;

	if (!IS_ISP2401 || !in_info)
		in_info = internal_info;

	ret = ia_css_dma_configure_from_info(&to->port_b, in_info);
	if (ret)
		return ret;

	/* Assume divisiblity here, may need to generalize to fixed point. */
	assert((in_info->format == IA_CSS_FRAME_FORMAT_RAW_PACKED) ||
	       (elems_a % to->port_b.elems == 0));

	to->width_a_over_b      = elems_a / to->port_b.elems;
	to->inout_port_config   = from->pipe->inout_port_config;
	to->format              = in_info->format;
	to->required_bds_factor = from->pipe->required_bds_factor;
	to->two_ppc             = from->two_ppc;
	to->stream_format       = css2isp_stream_format(from->stream_format);
	to->deinterleaved       = from->deinterleaved;

	if (IS_ISP2401) {
		to->start_column        = in_info->crop_info.start_column;
		to->start_line          = in_info->crop_info.start_line;
		to->enable_left_padding = from->enable_left_padding;
	}

	return 0;
}

int ia_css_raw_configure(const struct sh_css_sp_pipeline *pipe,
			 const struct ia_css_binary      *binary,
			 const struct ia_css_frame_info  *in_info,
			 const struct ia_css_frame_info  *internal_info,
			 bool two_ppc,
			 bool deinterleaved)
{
	u8 enable_left_padding = (uint8_t)((binary->left_padding) ? 1 : 0);
	struct ia_css_raw_configuration config = default_config;

	config.pipe                = pipe;
	config.in_info             = in_info;
	config.internal_info       = internal_info;
	config.two_ppc             = two_ppc;
	config.stream_format       = binary->input_format;
	config.deinterleaved       = deinterleaved;
	config.enable_left_padding = enable_left_padding;

	return ia_css_configure_raw(binary, &config);
}
