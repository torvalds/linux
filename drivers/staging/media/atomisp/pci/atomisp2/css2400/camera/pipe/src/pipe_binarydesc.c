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

#include "ia_css_pipe_binarydesc.h"
#include "ia_css_frame_format.h"
#include "ia_css_pipe.h"
#include "ia_css_pipe_util.h"
#include "ia_css_util.h"
#include "ia_css_debug.h"
#include "sh_css_params.h"
#include <assert_support.h>
/* HRT_GDC_N */
#include "gdc_device.h"
#include <linux/kernel.h>

/* This module provides a binary descriptions to used to find a binary. Since,
 * every stage is associated with a binary, it implicity helps stage
 * description. Apart from providing a binary description, this module also
 * populates the frame info's when required.*/

/* Generic descriptor for offline binaries. Internal function. */
static void pipe_binarydesc_get_offline(
	struct ia_css_pipe const * const pipe,
	const int mode,
	struct ia_css_binary_descr *descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info[],
	struct ia_css_frame_info *vf_info)
{
	unsigned int i;
	/* in_info, out_info, vf_info can be NULL */
	assert(pipe != NULL);
	assert(descr != NULL);
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE,
			    "pipe_binarydesc_get_offline() enter:\n");

	descr->mode = mode;
	descr->online = false;
	descr->continuous = pipe->stream->config.continuous;
	descr->striped = false;
	descr->two_ppc = false;
	descr->enable_yuv_ds = false;
	descr->enable_high_speed = false;
	descr->enable_dvs_6axis = false;
	descr->enable_reduced_pipe = false;
	descr->enable_dz = true;
	descr->enable_xnr = false;
	descr->enable_dpc = false;
#ifdef ISP2401
	descr->enable_luma_only = false;
	descr->enable_tnr = false;
#endif
	descr->enable_capture_pp_bli = false;
	descr->enable_fractional_ds = false;
	descr->dvs_env.width = 0;
	descr->dvs_env.height = 0;
	descr->stream_format = pipe->stream->config.input_config.format;
	descr->in_info = in_info;
	descr->bds_out_info = NULL;
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		descr->out_info[i] = out_info[i];
	descr->vf_info = vf_info;
	descr->isp_pipe_version = pipe->config.isp_pipe_version;
	descr->required_bds_factor = SH_CSS_BDS_FACTOR_1_00;
	descr->stream_config_left_padding = -1;
}

void ia_css_pipe_get_copy_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *copy_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info)
{
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	unsigned int i;
	/* out_info can be NULL */
	assert(pipe != NULL);
	assert(in_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;
	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_COPY,
				    copy_descr, in_info, out_infos, vf_info);
	copy_descr->online = true;
	copy_descr->continuous = false;
	copy_descr->two_ppc = (pipe->stream->config.pixels_per_clock == 2);
	copy_descr->enable_dz = false;
	copy_descr->isp_pipe_version = IA_CSS_PIPE_VERSION_1;
	IA_CSS_LEAVE_PRIVATE("");
}
void ia_css_pipe_get_vfpp_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *vf_pp_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info)
{
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	unsigned int i;
	/* out_info can be NULL ??? */
	assert(pipe != NULL);
	assert(in_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	in_info->raw_bit_depth = 0;
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_VF_PP,
			       vf_pp_descr, in_info, out_infos, NULL);
	vf_pp_descr->enable_fractional_ds = true;
	IA_CSS_LEAVE_PRIVATE("");
}

static struct sh_css_bds_factor bds_factors_list[] = {
	{1, 1, SH_CSS_BDS_FACTOR_1_00},
	{5, 4, SH_CSS_BDS_FACTOR_1_25},
	{3, 2, SH_CSS_BDS_FACTOR_1_50},
	{2, 1, SH_CSS_BDS_FACTOR_2_00},
	{9, 4, SH_CSS_BDS_FACTOR_2_25},
	{5, 2, SH_CSS_BDS_FACTOR_2_50},
	{3, 1, SH_CSS_BDS_FACTOR_3_00},
	{4, 1, SH_CSS_BDS_FACTOR_4_00},
	{9, 2, SH_CSS_BDS_FACTOR_4_50},
	{5, 1, SH_CSS_BDS_FACTOR_5_00},
	{6, 1, SH_CSS_BDS_FACTOR_6_00},
	{8, 1, SH_CSS_BDS_FACTOR_8_00}
};

enum ia_css_err sh_css_bds_factor_get_numerator_denominator(
	unsigned int bds_factor,
	unsigned int *bds_factor_numerator,
	unsigned int *bds_factor_denominator)
{
	unsigned int i;

	/* Loop over all bds factors until a match is found */
	for (i = 0; i < ARRAY_SIZE(bds_factors_list); i++) {
		if (bds_factors_list[i].bds_factor == bds_factor) {
			*bds_factor_numerator = bds_factors_list[i].numerator;
			*bds_factor_denominator = bds_factors_list[i].denominator;
			return IA_CSS_SUCCESS;
		}
	}

	/* Throw an error since bds_factor cannot be found
	in bds_factors_list */
	return IA_CSS_ERR_INVALID_ARGUMENTS;
}

enum ia_css_err binarydesc_calculate_bds_factor(
	struct ia_css_resolution input_res,
	struct ia_css_resolution output_res,
	unsigned int *bds_factor)
{
	unsigned int i;
	unsigned int in_w = input_res.width,
	    in_h = input_res.height,
	    out_w = output_res.width, out_h = output_res.height;

	unsigned int max_bds_factor = 8;
	unsigned int max_rounding_margin = 2;
	/* delta in pixels to account for rounding margin in the calculation */
	unsigned int delta = max_bds_factor * max_rounding_margin;

	/* Assert if the resolutions are not set */
	assert(in_w != 0 && in_h != 0);
	assert(out_w != 0 && out_h != 0);

	/* Loop over all bds factors until a match is found */
	for (i = 0; i < ARRAY_SIZE(bds_factors_list); i++) {
		unsigned num = bds_factors_list[i].numerator;
		unsigned den = bds_factors_list[i].denominator;

		/* See width-wise and height-wise if this bds_factor
		 * satisfies the condition */
		bool cond = (out_w * num / den + delta > in_w) &&
		    (out_w * num / den <= in_w) &&
		    (out_h * num / den + delta > in_h) &&
		    (out_h * num / den <= in_h);

		if (cond) {
			*bds_factor = bds_factors_list[i].bds_factor;
			return IA_CSS_SUCCESS;
		}
	}

	/* Throw an error since a suitable bds_factor cannot be found */
	return IA_CSS_ERR_INVALID_ARGUMENTS;
}

enum ia_css_err ia_css_pipe_get_preview_binarydesc(
	struct ia_css_pipe * const pipe,
	struct ia_css_binary_descr *preview_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *bds_out_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info)
{
	enum ia_css_err err;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	int mode = IA_CSS_BINARY_MODE_PREVIEW;
	unsigned int i;

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	assert(vf_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	/*
	 * Set up the info of the input frame with
	 * the ISP required resolution
	 */
	in_info->res = pipe->config.input_effective_res;
	in_info->padded_width = in_info->res.width;
	in_info->raw_bit_depth = ia_css_pipe_util_pipe_input_format_bpp(pipe);

	if (ia_css_util_is_input_format_yuv(pipe->stream->config.input_config.format))
		mode = IA_CSS_BINARY_MODE_COPY;
	else
		in_info->format = IA_CSS_FRAME_FORMAT_RAW;

	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, mode,
			       preview_descr, in_info, out_infos, vf_info);
	if (pipe->stream->config.online) {
		preview_descr->online = pipe->stream->config.online;
		preview_descr->two_ppc =
		    (pipe->stream->config.pixels_per_clock == 2);
	}
	preview_descr->stream_format = pipe->stream->config.input_config.format;

	/* TODO: Remove this when bds_out_info is available! */
	*bds_out_info = *in_info;

	if (pipe->extra_config.enable_raw_binning) {
		if (pipe->config.bayer_ds_out_res.width != 0 &&
		    pipe->config.bayer_ds_out_res.height != 0) {
			bds_out_info->res.width =
			    pipe->config.bayer_ds_out_res.width;
			bds_out_info->res.height =
			    pipe->config.bayer_ds_out_res.height;
			bds_out_info->padded_width =
			    pipe->config.bayer_ds_out_res.width;
			err =
			    binarydesc_calculate_bds_factor(in_info->res,
				    bds_out_info->res,
				    &preview_descr->required_bds_factor);
			if (err != IA_CSS_SUCCESS)
				return err;
		} else {
			bds_out_info->res.width = in_info->res.width / 2;
			bds_out_info->res.height = in_info->res.height / 2;
			bds_out_info->padded_width = in_info->padded_width / 2;
			preview_descr->required_bds_factor =
			    SH_CSS_BDS_FACTOR_2_00;
		}
	} else {
		/* TODO: Remove this when bds_out_info->is available! */
		bds_out_info->res.width = in_info->res.width;
		bds_out_info->res.height = in_info->res.height;
		bds_out_info->padded_width = in_info->padded_width;
		preview_descr->required_bds_factor = SH_CSS_BDS_FACTOR_1_00;
	}
	pipe->required_bds_factor = preview_descr->required_bds_factor;

	/* bayer ds and fractional ds cannot be enabled at the same time,
	so we disable bds_out_info when fractional ds is used */
	if (!pipe->extra_config.enable_fractional_ds)
		preview_descr->bds_out_info = bds_out_info;
	else
		preview_descr->bds_out_info = NULL;
	/*
	   ----Preview binary-----
	   --in-->|--out->|vf_veceven|--|--->vf
	   -----------------------
	   * Preview binary normally doesn't have a vf_port but
	   * instead it has an output port. However, the output is
	   * generated by vf_veceven module in which we might have
	   * a downscaling (by 1x, 2x, or 4x). Because the resolution
	   * might change, we need two different info, namely out_info
	   * & vf_info. In fill_binary_info we use out&vf info to
	   * calculate vf decimation factor.
	 */
	*out_info = *vf_info;

	/* In case of preview_ds binary, we can do any fractional amount
	 * of downscale, so there is no DS needed in vf_veceven. Therefore,
	 * out and vf infos will be the same. Otherwise, we set out resolution
	 * equal to in resolution. */
	if (!pipe->extra_config.enable_fractional_ds) {
		/* TODO: Change this when bds_out_info is available! */
		out_info->res.width = bds_out_info->res.width;
		out_info->res.height = bds_out_info->res.height;
		out_info->padded_width = bds_out_info->padded_width;
	}
	preview_descr->enable_fractional_ds =
	    pipe->extra_config.enable_fractional_ds;

	preview_descr->enable_dpc = pipe->config.enable_dpc;

	preview_descr->isp_pipe_version = pipe->config.isp_pipe_version;
	IA_CSS_LEAVE_ERR_PRIVATE(IA_CSS_SUCCESS);
	return IA_CSS_SUCCESS;
}

enum ia_css_err ia_css_pipe_get_video_binarydesc(
	struct ia_css_pipe * const pipe,
	struct ia_css_binary_descr *video_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *bds_out_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info,
	int stream_config_left_padding)
{
	int mode = IA_CSS_BINARY_MODE_VIDEO;
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	enum ia_css_err err = IA_CSS_SUCCESS;
	bool stream_dz_config = false;

	/* vf_info can be NULL */
	assert(pipe != NULL);
	assert(in_info != NULL);
	/* assert(vf_info != NULL); */
	IA_CSS_ENTER_PRIVATE("");

	/* The solution below is not optimal; we should move to using ia_css_pipe_get_copy_binarydesc()
	 * But for now this fixes things; this code used to be there but was removed
	 * with gerrit 8908 as this was wrong for Skycam; however 240x still needs this
	 */
	if (ia_css_util_is_input_format_yuv(pipe->stream->config.input_config.format))
		mode = IA_CSS_BINARY_MODE_COPY;

	in_info->res = pipe->config.input_effective_res;
	in_info->padded_width = in_info->res.width;
	in_info->format = IA_CSS_FRAME_FORMAT_RAW;
	in_info->raw_bit_depth = ia_css_pipe_util_pipe_input_format_bpp(pipe);
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, mode,
	       video_descr, in_info, out_infos, vf_info);

	if (pipe->stream->config.online) {
		video_descr->online = pipe->stream->config.online;
		video_descr->two_ppc =
		    (pipe->stream->config.pixels_per_clock == 2);
	}

	if (mode == IA_CSS_BINARY_MODE_VIDEO) {
		stream_dz_config =
		    ((pipe->stream->isp_params_configs->dz_config.dx !=
		      HRT_GDC_N)
		     || (pipe->stream->isp_params_configs->dz_config.dy !=
			 HRT_GDC_N));

		video_descr->enable_dz = pipe->config.enable_dz
		    || stream_dz_config;
		video_descr->dvs_env = pipe->config.dvs_envelope;
		video_descr->enable_yuv_ds = pipe->extra_config.enable_yuv_ds;
		video_descr->enable_high_speed =
		    pipe->extra_config.enable_high_speed;
		video_descr->enable_dvs_6axis =
		    pipe->extra_config.enable_dvs_6axis;
		video_descr->enable_reduced_pipe =
		    pipe->extra_config.enable_reduced_pipe;
		video_descr->isp_pipe_version = pipe->config.isp_pipe_version;
		video_descr->enable_fractional_ds =
		    pipe->extra_config.enable_fractional_ds;
		video_descr->enable_dpc =
			pipe->config.enable_dpc;
#ifdef ISP2401
		video_descr->enable_luma_only =
			pipe->config.enable_luma_only;
		video_descr->enable_tnr =
			pipe->config.enable_tnr;
#endif

		if (pipe->extra_config.enable_raw_binning) {
			if (pipe->config.bayer_ds_out_res.width != 0 &&
			    pipe->config.bayer_ds_out_res.height != 0) {
				bds_out_info->res.width =
				    pipe->config.bayer_ds_out_res.width;
				bds_out_info->res.height =
				    pipe->config.bayer_ds_out_res.height;
				bds_out_info->padded_width =
				    pipe->config.bayer_ds_out_res.width;
				err =
				binarydesc_calculate_bds_factor(
					in_info->res, bds_out_info->res,
					&video_descr->required_bds_factor);
				if (err != IA_CSS_SUCCESS)
					return err;
			} else {
				bds_out_info->res.width =
				    in_info->res.width / 2;
				bds_out_info->res.height =
				    in_info->res.height / 2;
				bds_out_info->padded_width =
				    in_info->padded_width / 2;
				video_descr->required_bds_factor =
				    SH_CSS_BDS_FACTOR_2_00;
			}
		} else {
			bds_out_info->res.width = in_info->res.width;
			bds_out_info->res.height = in_info->res.height;
			bds_out_info->padded_width = in_info->padded_width;
			video_descr->required_bds_factor =
			    SH_CSS_BDS_FACTOR_1_00;
		}

		pipe->required_bds_factor = video_descr->required_bds_factor;

		/* bayer ds and fractional ds cannot be enabled
		at the same time, so we disable bds_out_info when
		fractional ds is used */
		if (!pipe->extra_config.enable_fractional_ds)
			video_descr->bds_out_info = bds_out_info;
		else
			video_descr->bds_out_info = NULL;

		video_descr->enable_fractional_ds =
		    pipe->extra_config.enable_fractional_ds;
		video_descr->stream_config_left_padding = stream_config_left_padding;
	}
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

void ia_css_pipe_get_yuvscaler_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *yuv_scaler_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *internal_out_info,
	struct ia_css_frame_info *vf_info)
{
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];
	struct ia_css_frame_info *this_vf_info = NULL;

	assert(pipe != NULL);
	assert(in_info != NULL);
	/* Note: if the following assert fails, the number of ports has been
	 * changed; in that case an additional initializer must be added
	 * a few lines below after which this assert can be updated.
	 */
	assert(IA_CSS_BINARY_MAX_OUTPUT_PORTS == 2);
	IA_CSS_ENTER_PRIVATE("");

	in_info->padded_width = in_info->res.width;
	in_info->raw_bit_depth = 0;
	ia_css_frame_info_set_width(in_info, in_info->res.width, 0);
	out_infos[0] = out_info;
	out_infos[1] = internal_out_info;
	/* add initializers here if
	 * assert(IA_CSS_BINARY_MAX_OUTPUT_PORTS == ...);
	 * fails
	 */

	if (vf_info) {
		this_vf_info = (vf_info->res.width == 0 &&
			vf_info->res.height == 0) ? NULL : vf_info;
	}

	pipe_binarydesc_get_offline(pipe,
			       IA_CSS_BINARY_MODE_CAPTURE_PP,
			       yuv_scaler_descr,
			       in_info, out_infos, this_vf_info);

	yuv_scaler_descr->enable_fractional_ds = true;
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_capturepp_binarydesc(
	struct ia_css_pipe * const pipe,
	struct ia_css_binary_descr *capture_pp_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(vf_info != NULL);
	IA_CSS_ENTER_PRIVATE("");


	/* the in_info is only used for resolution to enable
	   bayer down scaling. */
	if (pipe->out_yuv_ds_input_info.res.width)
		*in_info = pipe->out_yuv_ds_input_info;
	else
		*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_YUV420;
	in_info->raw_bit_depth = 0;
	ia_css_frame_info_set_width(in_info, in_info->res.width, 0);

	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe,
			       IA_CSS_BINARY_MODE_CAPTURE_PP,
			       capture_pp_descr,
			       in_info, out_infos, vf_info);

	capture_pp_descr->enable_capture_pp_bli =
		pipe->config.default_capture_config.enable_capture_pp_bli;
	capture_pp_descr->enable_fractional_ds = true;
	capture_pp_descr->enable_xnr =
		pipe->config.default_capture_config.enable_xnr != 0;
	IA_CSS_LEAVE_PRIVATE("");
}

/* lookup table for high quality primary binaries */
static unsigned int primary_hq_binary_modes[NUM_PRIMARY_HQ_STAGES] =
{
	IA_CSS_BINARY_MODE_PRIMARY_HQ_STAGE0,
	IA_CSS_BINARY_MODE_PRIMARY_HQ_STAGE1,
	IA_CSS_BINARY_MODE_PRIMARY_HQ_STAGE2,
	IA_CSS_BINARY_MODE_PRIMARY_HQ_STAGE3,
	IA_CSS_BINARY_MODE_PRIMARY_HQ_STAGE4,
	IA_CSS_BINARY_MODE_PRIMARY_HQ_STAGE5
};

void ia_css_pipe_get_primary_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *prim_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info,
	unsigned int stage_idx)
{
	enum ia_css_pipe_version pipe_version = pipe->config.isp_pipe_version;
	int mode;
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	assert(stage_idx < NUM_PRIMARY_HQ_STAGES);
	/* vf_info can be NULL - example video_binarydescr */
	/*assert(vf_info != NULL);*/
	IA_CSS_ENTER_PRIVATE("");

	if (pipe_version == IA_CSS_PIPE_VERSION_2_6_1)
		mode = primary_hq_binary_modes[stage_idx];
	else
		mode = IA_CSS_BINARY_MODE_PRIMARY;

	if (ia_css_util_is_input_format_yuv(pipe->stream->config.input_config.format))
		mode = IA_CSS_BINARY_MODE_COPY;

	in_info->res = pipe->config.input_effective_res;
	in_info->padded_width = in_info->res.width;

#if !defined(HAS_NO_PACKED_RAW_PIXELS)
	if (pipe->stream->config.pack_raw_pixels)
		in_info->format = IA_CSS_FRAME_FORMAT_RAW_PACKED;
	else
#endif
		in_info->format = IA_CSS_FRAME_FORMAT_RAW;

	in_info->raw_bit_depth = ia_css_pipe_util_pipe_input_format_bpp(pipe);
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, mode,
			       prim_descr, in_info, out_infos, vf_info);

	if (pipe->stream->config.online &&
	    pipe->stream->config.mode != IA_CSS_INPUT_MODE_MEMORY) {
		prim_descr->online = true;
		prim_descr->two_ppc =
		    (pipe->stream->config.pixels_per_clock == 2);
		prim_descr->stream_format = pipe->stream->config.input_config.format;
	}
	if (mode == IA_CSS_BINARY_MODE_PRIMARY) {
		prim_descr->isp_pipe_version = pipe->config.isp_pipe_version;
		prim_descr->enable_fractional_ds =
		    pipe->extra_config.enable_fractional_ds;
#ifdef ISP2401
		prim_descr->enable_luma_only =
			pipe->config.enable_luma_only;
#endif
		/* We have both striped and non-striped primary binaries,
		 * if continuous viewfinder is required, then we must select
		 * a striped one. Otherwise we prefer to use a non-striped
		 * since it has better performance. */
		if (pipe_version == IA_CSS_PIPE_VERSION_2_6_1)
			prim_descr->striped = false;
		else
#ifndef ISP2401
			prim_descr->striped = prim_descr->continuous && (!pipe->stream->stop_copy_preview || !pipe->stream->disable_cont_vf);
#else
			prim_descr->striped = prim_descr->continuous && !pipe->stream->disable_cont_vf;

		if ((pipe->config.default_capture_config.enable_xnr != 0) &&
			(pipe->extra_config.enable_dvs_6axis == true))
				prim_descr->enable_xnr = true;
#endif
	}
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_pre_gdc_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *pre_gdc_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_RAW;
	in_info->raw_bit_depth = ia_css_pipe_util_pipe_input_format_bpp(pipe);
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_PRE_ISP,
			       pre_gdc_descr, in_info, out_infos, NULL);
	pre_gdc_descr->isp_pipe_version = pipe->config.isp_pipe_version;
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_gdc_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *gdc_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_QPLANE6;
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_GDC,
			       gdc_descr, in_info, out_infos, NULL);
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_post_gdc_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *post_gdc_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	assert(vf_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_YUV420_16;
	in_info->raw_bit_depth = 16;
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_POST_ISP,
			       post_gdc_descr, in_info, out_infos, vf_info);

	post_gdc_descr->isp_pipe_version = pipe->config.isp_pipe_version;
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_pre_de_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *pre_de_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_RAW;
	in_info->raw_bit_depth = ia_css_pipe_util_pipe_input_format_bpp(pipe);
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	if (pipe->config.isp_pipe_version == IA_CSS_PIPE_VERSION_1)
		pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_PRE_ISP,
				       pre_de_descr, in_info, out_infos, NULL);
	else if (pipe->config.isp_pipe_version == IA_CSS_PIPE_VERSION_2_2) {
		pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_PRE_DE,
				       pre_de_descr, in_info, out_infos, NULL);
	}

	if (pipe->stream->config.online) {
		pre_de_descr->online = true;
		pre_de_descr->two_ppc =
		    (pipe->stream->config.pixels_per_clock == 2);
		pre_de_descr->stream_format = pipe->stream->config.input_config.format;
	}
	pre_de_descr->isp_pipe_version = pipe->config.isp_pipe_version;
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_pre_anr_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *pre_anr_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_RAW;
	in_info->raw_bit_depth = ia_css_pipe_util_pipe_input_format_bpp(pipe);
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_PRE_ISP,
			       pre_anr_descr, in_info, out_infos, NULL);

	if (pipe->stream->config.online) {
		pre_anr_descr->online = true;
		pre_anr_descr->two_ppc =
		    (pipe->stream->config.pixels_per_clock == 2);
		pre_anr_descr->stream_format = pipe->stream->config.input_config.format;
	}
	pre_anr_descr->isp_pipe_version = pipe->config.isp_pipe_version;
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_anr_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *anr_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_RAW;
	in_info->raw_bit_depth = ANR_ELEMENT_BITS;
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_ANR,
			       anr_descr, in_info, out_infos, NULL);

	anr_descr->isp_pipe_version = pipe->config.isp_pipe_version;
	IA_CSS_LEAVE_PRIVATE("");
}


void ia_css_pipe_get_post_anr_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *post_anr_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info,
	struct ia_css_frame_info *vf_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	assert(vf_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

	*in_info = *out_info;
	in_info->format = IA_CSS_FRAME_FORMAT_RAW;
	in_info->raw_bit_depth = ANR_ELEMENT_BITS;
	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_POST_ISP,
			       post_anr_descr, in_info, out_infos, vf_info);

	post_anr_descr->isp_pipe_version = pipe->config.isp_pipe_version;
	IA_CSS_LEAVE_PRIVATE("");
}

void ia_css_pipe_get_ldc_binarydesc(
	struct ia_css_pipe const * const pipe,
	struct ia_css_binary_descr *ldc_descr,
	struct ia_css_frame_info *in_info,
	struct ia_css_frame_info *out_info)
{
	unsigned int i;
	struct ia_css_frame_info *out_infos[IA_CSS_BINARY_MAX_OUTPUT_PORTS];

	assert(pipe != NULL);
	assert(in_info != NULL);
	assert(out_info != NULL);
	IA_CSS_ENTER_PRIVATE("");

#ifndef ISP2401
	*in_info = *out_info;
#else
	if (pipe->out_yuv_ds_input_info.res.width)
		*in_info = pipe->out_yuv_ds_input_info;
	else
		*in_info = *out_info;
#endif
	in_info->format = IA_CSS_FRAME_FORMAT_YUV420;
	in_info->raw_bit_depth = 0;
	ia_css_frame_info_set_width(in_info, in_info->res.width, 0);

	out_infos[0] = out_info;
	for (i = 1; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
		out_infos[i] = NULL;

	pipe_binarydesc_get_offline(pipe, IA_CSS_BINARY_MODE_CAPTURE_PP,
			       ldc_descr, in_info, out_infos, NULL);
	ldc_descr->enable_dvs_6axis =
		    pipe->extra_config.enable_dvs_6axis;
	IA_CSS_LEAVE_PRIVATE("");
}
