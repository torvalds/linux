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

#include "ia_css_iterator.host.h"
#include "ia_css_frame_public.h"
#include "ia_css_binary.h"
#include "ia_css_err.h"
#define IA_CSS_INCLUDE_CONFIGURATIONS
#include "ia_css_isp_configs.h"

static const struct ia_css_iterator_configuration default_config = {
	.input_info = (struct ia_css_frame_info *)NULL,
};

void
ia_css_iterator_config(
	struct sh_css_isp_iterator_isp_config *to,
	const struct ia_css_iterator_configuration *from,
	unsigned size)
{
	(void)size;
	ia_css_frame_info_to_frame_sp_info(&to->input_info,    from->input_info);
	ia_css_frame_info_to_frame_sp_info(&to->internal_info, from->internal_info);
	ia_css_frame_info_to_frame_sp_info(&to->output_info,   from->output_info);
	ia_css_frame_info_to_frame_sp_info(&to->vf_info,       from->vf_info);
	ia_css_resolution_to_sp_resolution(&to->dvs_envelope,  from->dvs_envelope);
}

enum ia_css_err
ia_css_iterator_configure(
	const struct ia_css_binary *binary,
	const struct ia_css_frame_info *in_info)
{
	struct ia_css_frame_info my_info = IA_CSS_BINARY_DEFAULT_FRAME_INFO;
	struct ia_css_iterator_configuration config = default_config;

	config.input_info    = &binary->in_frame_info;
	config.internal_info = &binary->internal_frame_info;
	config.output_info   = &binary->out_frame_info[0];
	config.vf_info       = &binary->vf_frame_info;
	config.dvs_envelope  = &binary->dvs_envelope;

	/* Use in_info iso binary->in_frame_info.
	 * They can differ in padded width in case of scaling, e.g. for capture_pp.
	 * Find out why.
	*/
	if (in_info)
		config.input_info = in_info;
	if (binary->out_frame_info[0].res.width == 0)
		config.output_info = &binary->out_frame_info[1];
	my_info = *config.output_info;
	config.output_info = &my_info;
	/* we do this only for preview pipe because in fill_binary_info function
	 * we assign vf_out res to out res, but for ISP internal processing, we need
	 * the original out res. for video pipe, it has two output pins --- out and
	 * vf_out, so it can keep these two resolutions already. */
	if (binary->info->sp.pipeline.mode == IA_CSS_BINARY_MODE_PREVIEW &&
	    binary->vf_downscale_log2 > 0) {
		/* TODO: Remove this after preview output decimation is fixed
		 * by configuring out&vf info files properly */
		my_info.padded_width <<= binary->vf_downscale_log2;
		my_info.res.width    <<= binary->vf_downscale_log2;
		my_info.res.height   <<= binary->vf_downscale_log2;
	}

	ia_css_configure_iterator(binary, &config);

	return IA_CSS_SUCCESS;
}
