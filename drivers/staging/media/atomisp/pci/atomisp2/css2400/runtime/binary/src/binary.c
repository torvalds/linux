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

#include <math_support.h>
#include <gdc_device.h>	/* HR_GDC_N */
#include "isp.h"	/* ISP_VEC_NELEMS */

#include "ia_css_binary.h"
#include "ia_css_debug.h"
#include "ia_css_util.h"
#include "ia_css_isp_param.h"
#include "sh_css_internal.h"
#include "sh_css_sp.h"
#include "sh_css_firmware.h"
#include "sh_css_defs.h"
#include "sh_css_legacy.h"

#include "vf/vf_1.0/ia_css_vf.host.h"
#ifdef ISP2401
#include "sc/sc_1.0/ia_css_sc.host.h"
#endif
#include "sdis/sdis_1.0/ia_css_sdis.host.h"
#ifdef ISP2401
#include "fixedbds/fixedbds_1.0/ia_css_fixedbds_param.h"	/* FRAC_ACC */
#endif

#include "camera/pipe/interface/ia_css_pipe_binarydesc.h"

#include "memory_access.h"

#include "assert_support.h"

#define IMPLIES(a, b)           (!(a) || (b))   /* A => B */

static struct ia_css_binary_xinfo *all_binaries; /* ISP binaries only (no SP) */
static struct ia_css_binary_xinfo
	*binary_infos[IA_CSS_BINARY_NUM_MODES] = { NULL, };

static void
ia_css_binary_dvs_env(const struct ia_css_binary_info *info,
		      const struct ia_css_resolution *dvs_env,
		      struct ia_css_resolution *binary_dvs_env)
{
	if (info->enable.dvs_envelope) {
		assert(dvs_env != NULL);
		binary_dvs_env->width  = max(dvs_env->width, SH_CSS_MIN_DVS_ENVELOPE);
		binary_dvs_env->height = max(dvs_env->height, SH_CSS_MIN_DVS_ENVELOPE);
	}
}

static void
ia_css_binary_internal_res(const struct ia_css_frame_info *in_info,
			   const struct ia_css_frame_info *bds_out_info,
			   const struct ia_css_frame_info *out_info,
			   const struct ia_css_resolution *dvs_env,
			   const struct ia_css_binary_info *info,
			   struct ia_css_resolution *internal_res)
{
	unsigned int isp_tmp_internal_width = 0,
		     isp_tmp_internal_height = 0;
	bool binary_supports_yuv_ds = info->enable.ds & 2;
	struct ia_css_resolution binary_dvs_env;

	binary_dvs_env.width = 0;
	binary_dvs_env.height = 0;
	ia_css_binary_dvs_env(info, dvs_env, &binary_dvs_env);

	if (binary_supports_yuv_ds) {
		if (in_info != NULL) {
			isp_tmp_internal_width = in_info->res.width
				+ info->pipeline.left_cropping + binary_dvs_env.width;
			isp_tmp_internal_height = in_info->res.height
				+ info->pipeline.top_cropping + binary_dvs_env.height;
		}
	} else if ((bds_out_info != NULL) && (out_info != NULL) &&
				/* TODO: hack to make video_us case work. this should be reverted after
				a nice solution in ISP */
				(bds_out_info->res.width >= out_info->res.width)) {
			isp_tmp_internal_width = bds_out_info->padded_width;
			isp_tmp_internal_height = bds_out_info->res.height;
	} else {
		if (out_info != NULL) {
			isp_tmp_internal_width = out_info->padded_width;
			isp_tmp_internal_height = out_info->res.height;
		}
	}

	/* We first calculate the resolutions used by the ISP. After that,
	 * we use those resolutions to compute sizes for tables etc. */
	internal_res->width = __ISP_INTERNAL_WIDTH(isp_tmp_internal_width,
		(int)binary_dvs_env.width,
		info->pipeline.left_cropping, info->pipeline.mode,
		info->pipeline.c_subsampling,
		info->output.num_chunks, info->pipeline.pipelining);
	internal_res->height = __ISP_INTERNAL_HEIGHT(isp_tmp_internal_height,
		info->pipeline.top_cropping,
		binary_dvs_env.height);
}

#ifndef ISP2401
/* Computation results of the origin coordinate of bayer on the shading table. */
struct sh_css_shading_table_bayer_origin_compute_results {
	uint32_t bayer_scale_hor_ratio_in;	/* Horizontal ratio (in) of bayer scaling. */
	uint32_t bayer_scale_hor_ratio_out;	/* Horizontal ratio (out) of bayer scaling. */
	uint32_t bayer_scale_ver_ratio_in;	/* Vertical ratio (in) of bayer scaling. */
	uint32_t bayer_scale_ver_ratio_out;	/* Vertical ratio (out) of bayer scaling. */
	uint32_t sc_bayer_origin_x_bqs_on_shading_table; /* X coordinate (in bqs) of bayer origin on shading table. */
	uint32_t sc_bayer_origin_y_bqs_on_shading_table; /* Y coordinate (in bqs) of bayer origin on shading table. */
#else
/* Requirements for the shading correction. */
struct sh_css_binary_sc_requirements {
	/* Bayer scaling factor, for the scaling which is applied before shading correction. */
	uint32_t bayer_scale_hor_ratio_in;  /* Horizontal ratio (in) of scaling applied BEFORE shading correction. */
	uint32_t bayer_scale_hor_ratio_out; /* Horizontal ratio (out) of scaling applied BEFORE shading correction. */
	uint32_t bayer_scale_ver_ratio_in;  /* Vertical ratio (in) of scaling applied BEFORE shading correction. */
	uint32_t bayer_scale_ver_ratio_out; /* Vertical ratio (out) of scaling applied BEFORE shading correction. */

	/* ISP internal frame is composed of the real sensor data and the padding data. */
	uint32_t sensor_data_origin_x_bqs_on_internal; /* X origin (in bqs) of sensor data on internal frame
								at shading correction. */
	uint32_t sensor_data_origin_y_bqs_on_internal; /* Y origin (in bqs) of sensor data on internal frame
								at shading correction. */
#endif
};

/* Get the requirements for the shading correction. */
static enum ia_css_err
#ifndef ISP2401
ia_css_binary_compute_shading_table_bayer_origin(
	const struct ia_css_binary *binary,				/* [in] */
	unsigned int required_bds_factor,				/* [in] */
	const struct ia_css_stream_config *stream_config,		/* [in] */
	struct sh_css_shading_table_bayer_origin_compute_results *res)	/* [out] */
#else
sh_css_binary_get_sc_requirements(
	const struct ia_css_binary *binary,			/* [in] */
	unsigned int required_bds_factor,			/* [in] */
	const struct ia_css_stream_config *stream_config,	/* [in] */
	struct sh_css_binary_sc_requirements *scr)		/* [out] */
#endif
{
	enum ia_css_err err;

#ifndef ISP2401
	/* Numerator and denominator of the fixed bayer downscaling factor.
	(numerator >= denominator) */
#else
	/* Numerator and denominator of the fixed bayer downscaling factor. (numerator >= denominator) */
#endif
	unsigned int bds_num, bds_den;

#ifndef ISP2401
	/* Horizontal/Vertical ratio of bayer scaling
	between input area and output area. */
	unsigned int bs_hor_ratio_in;
	unsigned int bs_hor_ratio_out;
	unsigned int bs_ver_ratio_in;
	unsigned int bs_ver_ratio_out;
#else
	/* Horizontal/Vertical ratio of bayer scaling between input area and output area. */
	unsigned int bs_hor_ratio_in, bs_hor_ratio_out, bs_ver_ratio_in, bs_ver_ratio_out;
#endif

	/* Left padding set by InputFormatter. */
#ifndef ISP2401
	unsigned int left_padding_bqs;			/* in bqs */
#else
	unsigned int left_padding_bqs;
#endif

#ifndef ISP2401
	/* Flag for the NEED_BDS_FACTOR_2_00 macro defined in isp kernels. */
	unsigned int need_bds_factor_2_00;

	/* Left padding adjusted inside the isp. */
	unsigned int left_padding_adjusted_bqs;		/* in bqs */

	/* Bad pixels caused by filters.
	NxN-filter (before/after bayer scaling) moves the image position
	to right/bottom directions by a few pixels.
	It causes bad pixels at left/top sides,
	and effective bayer size decreases. */
	unsigned int bad_bqs_on_left_before_bs;	/* in bqs */
	unsigned int bad_bqs_on_left_after_bs;	/* in bqs */
	unsigned int bad_bqs_on_top_before_bs;	/* in bqs */
	unsigned int bad_bqs_on_top_after_bs;	/* in bqs */

	/* Get the numerator and denominator of bayer downscaling factor. */
	err = sh_css_bds_factor_get_numerator_denominator
		(required_bds_factor, &bds_num, &bds_den);
	if (err != IA_CSS_SUCCESS)
#else
	/* Flags corresponding to NEED_BDS_FACTOR_2_00/NEED_BDS_FACTOR_1_50/NEED_BDS_FACTOR_1_25 macros
	 * defined in isp kernels. */
	unsigned int need_bds_factor_2_00, need_bds_factor_1_50, need_bds_factor_1_25;

	/* Left padding adjusted inside the isp kernels. */
	unsigned int left_padding_adjusted_bqs;

	/* Top padding padded inside the isp kernel for bayer downscaling binaries. */
	unsigned int top_padding_bqs;

	/* Bayer downscaling factor 1.0 by fixed-point. */
	int bds_frac_acc = FRAC_ACC;	/* FRAC_ACC is defined in ia_css_fixedbds_param.h. */

	/* Right/Down shift amount caused by filters applied BEFORE shading corrertion. */
	unsigned int right_shift_bqs_before_bs; /* right shift before bayer scaling */
	unsigned int right_shift_bqs_after_bs;  /* right shift after bayer scaling */
	unsigned int down_shift_bqs_before_bs;  /* down shift before bayer scaling */
	unsigned int down_shift_bqs_after_bs;   /* down shift after bayer scaling */

	/* Origin of the real sensor data area on the internal frame at shading correction. */
	unsigned int sensor_data_origin_x_bqs_on_internal;
	unsigned int sensor_data_origin_y_bqs_on_internal;

	IA_CSS_ENTER_PRIVATE("binary=%p, required_bds_factor=%d, stream_config=%p",
		binary, required_bds_factor, stream_config);

	/* Get the numerator and denominator of the required bayer downscaling factor. */
	err = sh_css_bds_factor_get_numerator_denominator(required_bds_factor, &bds_num, &bds_den);
	if (err != IA_CSS_SUCCESS) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
#endif
		return err;
#ifdef ISP2401
	}
#endif

#ifndef ISP2401
	/* Set the horizontal/vertical ratio of bayer scaling
	between input area and output area. */
#else
	IA_CSS_LOG("bds_num=%d, bds_den=%d", bds_num, bds_den);

	/* Set the horizontal/vertical ratio of bayer scaling between input area and output area. */
#endif
	bs_hor_ratio_in  = bds_num;
	bs_hor_ratio_out = bds_den;
	bs_ver_ratio_in  = bds_num;
	bs_ver_ratio_out = bds_den;

#ifndef ISP2401
	/* Set the left padding set by InputFormatter. (ifmtr.c) */
#else
	/* Set the left padding set by InputFormatter. (ia_css_ifmtr_configure() in ifmtr.c) */
#endif
	if (stream_config->left_padding == -1)
		left_padding_bqs = _ISP_BQS(binary->left_padding);
	else
#ifndef ISP2401
		left_padding_bqs = (unsigned int)((int)ISP_VEC_NELEMS
			- _ISP_BQS(stream_config->left_padding));
#else
		left_padding_bqs = (unsigned int)((int)ISP_VEC_NELEMS - _ISP_BQS(stream_config->left_padding));
#endif

#ifndef ISP2401
	/* Set the left padding adjusted inside the isp.
	When bds_factor 2.00 is needed, some padding is added to left_padding
	inside the isp, before bayer downscaling. (raw.isp.c)
	(Hopefully, left_crop/left_padding/top_crop should be defined in css
	appropriately, depending on bds_factor.)
	*/
#else
	IA_CSS_LOG("stream.left_padding=%d, binary.left_padding=%d, left_padding_bqs=%d",
		stream_config->left_padding, binary->left_padding, left_padding_bqs);

	/* Set the left padding adjusted inside the isp kernels.
	 * When the bds_factor isn't 1.00, the left padding size is adjusted inside the isp,
	 * before bayer downscaling. (scaled_hor_plane_index(), raw_compute_hphase() in raw.isp.c)
	 */
#endif
	need_bds_factor_2_00 = ((binary->info->sp.bds.supported_bds_factors &
		(PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_2_00) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_2_50) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_3_00) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_4_00) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_4_50) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_5_00) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_6_00) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_8_00))) != 0);

#ifndef ISP2401
	if (need_bds_factor_2_00 && binary->info->sp.pipeline.left_cropping > 0)
		left_padding_adjusted_bqs = left_padding_bqs + ISP_VEC_NELEMS;
	else
#else
	need_bds_factor_1_50 = ((binary->info->sp.bds.supported_bds_factors &
		(PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_1_50) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_2_25) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_3_00) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_4_50) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_6_00))) != 0);

	need_bds_factor_1_25 = ((binary->info->sp.bds.supported_bds_factors &
		(PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_1_25) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_2_50) |
		 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_5_00))) != 0);

	if (binary->info->sp.pipeline.left_cropping > 0 &&
	    (need_bds_factor_2_00 || need_bds_factor_1_50 || need_bds_factor_1_25)) {
		/*
		 * downscale 2.0  -> first_vec_adjusted_bqs = 128
		 * downscale 1.5  -> first_vec_adjusted_bqs = 96
		 * downscale 1.25 -> first_vec_adjusted_bqs = 80
		 */
		unsigned int first_vec_adjusted_bqs
			= ISP_VEC_NELEMS * bs_hor_ratio_in / bs_hor_ratio_out;
		left_padding_adjusted_bqs = first_vec_adjusted_bqs
			- _ISP_BQS(binary->info->sp.pipeline.left_cropping);
	} else
#endif
		left_padding_adjusted_bqs = left_padding_bqs;

#ifndef ISP2401
	/* Currently, the bad pixel caused by filters before bayer scaling
	is NOT considered, because the bad pixel is subtle.
	When some large filter is used in the future,
	we need to consider the bad pixel.

	Currently, when bds_factor isn't 1.00, 3x3 anti-alias filter is applied
	to each color plane(Gr/R/B/Gb) before bayer downscaling.
	This filter moves each color plane to right/bottom directions
	by 1 pixel at the most, depending on downscaling factor.
	*/
	bad_bqs_on_left_before_bs = 0;
	bad_bqs_on_top_before_bs = 0;
#else
	IA_CSS_LOG("supported_bds_factors=%d, need_bds_factor:2_00=%d, 1_50=%d, 1_25=%d",
		binary->info->sp.bds.supported_bds_factors,
		need_bds_factor_2_00, need_bds_factor_1_50, need_bds_factor_1_25);
	IA_CSS_LOG("left_cropping=%d, left_padding_adjusted_bqs=%d",
		binary->info->sp.pipeline.left_cropping, left_padding_adjusted_bqs);

	/* Set the top padding padded inside the isp kernel for bayer downscaling binaries.
	 * When the bds_factor isn't 1.00, the top padding is padded inside the isp
	 * before bayer downscaling, because the top cropping size (input margin) is not enough.
	 * (calculate_input_line(), raw_compute_vphase(), dma_read_raw() in raw.isp.c)
	 * NOTE: In dma_read_raw(), the factor passed to raw_compute_vphase() is got by get_bds_factor_for_dma_read().
	 *       This factor is BDS_FPVAL_100/BDS_FPVAL_125/BDS_FPVAL_150/BDS_FPVAL_200.
	 */
	top_padding_bqs = 0;
	if (binary->info->sp.pipeline.top_cropping > 0 &&
	    (required_bds_factor == SH_CSS_BDS_FACTOR_1_25 ||
	     required_bds_factor == SH_CSS_BDS_FACTOR_1_50 ||
	     required_bds_factor == SH_CSS_BDS_FACTOR_2_00)) {
		/* Calculation from calculate_input_line() and raw_compute_vphase() in raw.isp.c. */
		int top_cropping_bqs = _ISP_BQS(binary->info->sp.pipeline.top_cropping);
								/* top cropping (in bqs) */
		int factor = bds_num * bds_frac_acc / bds_den;	/* downscaling factor by fixed-point */
		int top_padding_bqsxfrac_acc = (top_cropping_bqs * factor - top_cropping_bqs * bds_frac_acc)
				+ (2 * bds_frac_acc - factor);	/* top padding by fixed-point (in bqs) */

		top_padding_bqs = (unsigned int)((top_padding_bqsxfrac_acc + bds_frac_acc/2 - 1) / bds_frac_acc);
	}

	IA_CSS_LOG("top_cropping=%d, top_padding_bqs=%d", binary->info->sp.pipeline.top_cropping, top_padding_bqs);

	/* Set the right/down shift amount caused by filters applied BEFORE bayer scaling,
	 * which scaling is applied BEFORE shading corrertion.
	 *
	 * When the bds_factor isn't 1.00, 3x3 anti-alias filter is applied to each color plane(Gr/R/B/Gb)
	 * before bayer downscaling.
	 * This filter shifts each color plane (Gr/R/B/Gb) to right/down directions by 1 pixel.
	 */
	right_shift_bqs_before_bs = 0;
	down_shift_bqs_before_bs = 0;
#endif

#ifndef ISP2401
	/* Currently, the bad pixel caused by filters after bayer scaling
	is NOT considered, because the bad pixel is subtle.
	When some large filter is used in the future,
	we need to consider the bad pixel.

	Currently, when DPC&BNR is processed between bayer scaling and
	shading correction, DPC&BNR moves each color plane to
	right/bottom directions by 1 pixel.
	*/
	bad_bqs_on_left_after_bs = 0;
	bad_bqs_on_top_after_bs = 0;
#else
	if (need_bds_factor_2_00 || need_bds_factor_1_50 || need_bds_factor_1_25) {
		right_shift_bqs_before_bs = 1;
		down_shift_bqs_before_bs = 1;
	}

	IA_CSS_LOG("right_shift_bqs_before_bs=%d, down_shift_bqs_before_bs=%d",
		right_shift_bqs_before_bs, down_shift_bqs_before_bs);

	/* Set the right/down shift amount caused by filters applied AFTER bayer scaling,
	 * which scaling is applied BEFORE shading corrertion.
	 *
	 * When DPC&BNR is processed between bayer scaling and shading correction,
	 * DPC&BNR moves each color plane (Gr/R/B/Gb) to right/down directions by 1 pixel.
	 */
	right_shift_bqs_after_bs = 0;
	down_shift_bqs_after_bs = 0;
#endif

#ifndef ISP2401
	/* Calculate the origin of bayer (real sensor data area)
	located on the shading table during the shading correction. */
	res->sc_bayer_origin_x_bqs_on_shading_table
		= ((left_padding_adjusted_bqs + bad_bqs_on_left_before_bs)
		* bs_hor_ratio_out + bs_hor_ratio_in/2) / bs_hor_ratio_in
		+ bad_bqs_on_left_after_bs;
			/* "+ bs_hor_ratio_in/2": rounding for division by bs_hor_ratio_in */
	res->sc_bayer_origin_y_bqs_on_shading_table
		= (bad_bqs_on_top_before_bs
		* bs_ver_ratio_out + bs_ver_ratio_in/2) / bs_ver_ratio_in
		+ bad_bqs_on_top_after_bs;
			/* "+ bs_ver_ratio_in/2": rounding for division by bs_ver_ratio_in */

	res->bayer_scale_hor_ratio_in  = (uint32_t)bs_hor_ratio_in;
	res->bayer_scale_hor_ratio_out = (uint32_t)bs_hor_ratio_out;
	res->bayer_scale_ver_ratio_in  = (uint32_t)bs_ver_ratio_in;
	res->bayer_scale_ver_ratio_out = (uint32_t)bs_ver_ratio_out;
#else
	if (binary->info->mem_offsets.offsets.param->dmem.dp.size != 0) { /* if DPC&BNR is enabled in the binary */
		right_shift_bqs_after_bs = 1;
		down_shift_bqs_after_bs = 1;
	}

	IA_CSS_LOG("right_shift_bqs_after_bs=%d, down_shift_bqs_after_bs=%d",
		right_shift_bqs_after_bs, down_shift_bqs_after_bs);

	/* Set the origin of the sensor data area on the internal frame at shading correction. */
	{
		unsigned int bs_frac = bds_frac_acc;	/* scaling factor 1.0 in fixed point */
		unsigned int bs_out, bs_in;		/* scaling ratio in fixed point */

		bs_out = bs_hor_ratio_out * bs_frac;
		bs_in = bs_hor_ratio_in * bs_frac;
		sensor_data_origin_x_bqs_on_internal
			= ((left_padding_adjusted_bqs + right_shift_bqs_before_bs) * bs_out + bs_in/2) / bs_in
				+ right_shift_bqs_after_bs;	/* "+ bs_in/2": rounding */

		bs_out = bs_ver_ratio_out * bs_frac;
		bs_in = bs_ver_ratio_in * bs_frac;
		sensor_data_origin_y_bqs_on_internal
			= ((top_padding_bqs + down_shift_bqs_before_bs) * bs_out + bs_in/2) / bs_in
				+ down_shift_bqs_after_bs;	/* "+ bs_in/2": rounding */
	}

	scr->bayer_scale_hor_ratio_in			= (uint32_t)bs_hor_ratio_in;
	scr->bayer_scale_hor_ratio_out			= (uint32_t)bs_hor_ratio_out;
	scr->bayer_scale_ver_ratio_in			= (uint32_t)bs_ver_ratio_in;
	scr->bayer_scale_ver_ratio_out			= (uint32_t)bs_ver_ratio_out;
	scr->sensor_data_origin_x_bqs_on_internal	= (uint32_t)sensor_data_origin_x_bqs_on_internal;
	scr->sensor_data_origin_y_bqs_on_internal	= (uint32_t)sensor_data_origin_y_bqs_on_internal;

	IA_CSS_LOG("sc_requirements: %d, %d, %d, %d, %d, %d",
		scr->bayer_scale_hor_ratio_in, scr->bayer_scale_hor_ratio_out,
		scr->bayer_scale_ver_ratio_in, scr->bayer_scale_ver_ratio_out,
		scr->sensor_data_origin_x_bqs_on_internal, scr->sensor_data_origin_y_bqs_on_internal);
#endif

#ifdef ISP2401
	IA_CSS_LEAVE_ERR_PRIVATE(err);
#endif
	return err;
}

/* Get the shading information of Shading Correction Type 1. */
static enum ia_css_err
ia_css_binary_get_shading_info_type_1(const struct ia_css_binary *binary,	/* [in] */
			unsigned int required_bds_factor,			/* [in] */
			const struct ia_css_stream_config *stream_config,	/* [in] */
#ifndef ISP2401
			struct ia_css_shading_info *info)			/* [out] */
#else
			struct ia_css_shading_info *shading_info,		/* [out] */
			struct ia_css_pipe_config *pipe_config)			/* [out] */
#endif
{
	enum ia_css_err err;
#ifndef ISP2401
	struct sh_css_shading_table_bayer_origin_compute_results res;
#else
	struct sh_css_binary_sc_requirements scr;
#endif

#ifndef ISP2401
	assert(binary != NULL);
	assert(info != NULL);
#else
	uint32_t in_width_bqs, in_height_bqs, internal_width_bqs, internal_height_bqs;
	uint32_t num_hor_grids, num_ver_grids, bqs_per_grid_cell, tbl_width_bqs, tbl_height_bqs;
	uint32_t sensor_org_x_bqs_on_internal, sensor_org_y_bqs_on_internal, sensor_width_bqs, sensor_height_bqs;
	uint32_t sensor_center_x_bqs_on_internal, sensor_center_y_bqs_on_internal;
	uint32_t left, right, upper, lower;
	uint32_t adjust_left, adjust_right, adjust_upper, adjust_lower, adjust_width_bqs, adjust_height_bqs;
	uint32_t internal_org_x_bqs_on_tbl, internal_org_y_bqs_on_tbl;
	uint32_t sensor_org_x_bqs_on_tbl, sensor_org_y_bqs_on_tbl;
#endif

#ifndef ISP2401
	info->type = IA_CSS_SHADING_CORRECTION_TYPE_1;
#else
	assert(binary != NULL);
	assert(stream_config != NULL);
	assert(shading_info != NULL);
	assert(pipe_config != NULL);
#endif

#ifndef ISP2401
	info->info.type_1.enable	    = binary->info->sp.enable.sc;
	info->info.type_1.num_hor_grids	    = binary->sctbl_width_per_color;
	info->info.type_1.num_ver_grids	    = binary->sctbl_height;
	info->info.type_1.bqs_per_grid_cell = (1 << binary->deci_factor_log2);
#else
	IA_CSS_ENTER_PRIVATE("binary=%p, required_bds_factor=%d, stream_config=%p",
		binary, required_bds_factor, stream_config);
#endif

	/* Initialize by default values. */
#ifndef ISP2401
	info->info.type_1.bayer_scale_hor_ratio_in	= 1;
	info->info.type_1.bayer_scale_hor_ratio_out	= 1;
	info->info.type_1.bayer_scale_ver_ratio_in	= 1;
	info->info.type_1.bayer_scale_ver_ratio_out	= 1;
	info->info.type_1.sc_bayer_origin_x_bqs_on_shading_table = 0;
	info->info.type_1.sc_bayer_origin_y_bqs_on_shading_table = 0;

	err = ia_css_binary_compute_shading_table_bayer_origin(
		binary,
		required_bds_factor,
		stream_config,
		&res);
	if (err != IA_CSS_SUCCESS)
#else
	*shading_info = DEFAULT_SHADING_INFO_TYPE_1;

	err = sh_css_binary_get_sc_requirements(binary, required_bds_factor, stream_config, &scr);
	if (err != IA_CSS_SUCCESS) {
		IA_CSS_LEAVE_ERR_PRIVATE(err);
#endif
		return err;
#ifdef ISP2401
	}

	IA_CSS_LOG("binary: id=%d, sctbl=%dx%d, deci=%d",
		binary->info->sp.id, binary->sctbl_width_per_color, binary->sctbl_height, binary->deci_factor_log2);
	IA_CSS_LOG("binary: in=%dx%d, in_padded_w=%d, int=%dx%d, int_padded_w=%d, out=%dx%d, out_padded_w=%d",
		binary->in_frame_info.res.width, binary->in_frame_info.res.height, binary->in_frame_info.padded_width,
		binary->internal_frame_info.res.width, binary->internal_frame_info.res.height,
		binary->internal_frame_info.padded_width,
		binary->out_frame_info[0].res.width, binary->out_frame_info[0].res.height,
		binary->out_frame_info[0].padded_width);

	/* Set the input size from sensor, which includes left/top crop size. */
	in_width_bqs	    = _ISP_BQS(binary->in_frame_info.res.width);
	in_height_bqs	    = _ISP_BQS(binary->in_frame_info.res.height);

	/* Frame size internally used in ISP, including sensor data and padding.
	 * This is the frame size, to which the shading correction is applied.
	 */
	internal_width_bqs  = _ISP_BQS(binary->internal_frame_info.res.width);
	internal_height_bqs = _ISP_BQS(binary->internal_frame_info.res.height);

	/* Shading table. */
	num_hor_grids = binary->sctbl_width_per_color;
	num_ver_grids = binary->sctbl_height;
	bqs_per_grid_cell = (1 << binary->deci_factor_log2);
	tbl_width_bqs  = (num_hor_grids - 1) * bqs_per_grid_cell;
	tbl_height_bqs = (num_ver_grids - 1) * bqs_per_grid_cell;
#endif

#ifndef ISP2401
	info->info.type_1.bayer_scale_hor_ratio_in	= res.bayer_scale_hor_ratio_in;
	info->info.type_1.bayer_scale_hor_ratio_out	= res.bayer_scale_hor_ratio_out;
	info->info.type_1.bayer_scale_ver_ratio_in	= res.bayer_scale_ver_ratio_in;
	info->info.type_1.bayer_scale_ver_ratio_out	= res.bayer_scale_ver_ratio_out;
	info->info.type_1.sc_bayer_origin_x_bqs_on_shading_table = res.sc_bayer_origin_x_bqs_on_shading_table;
	info->info.type_1.sc_bayer_origin_y_bqs_on_shading_table = res.sc_bayer_origin_y_bqs_on_shading_table;
#else
	IA_CSS_LOG("tbl_width_bqs=%d, tbl_height_bqs=%d", tbl_width_bqs, tbl_height_bqs);
#endif

#ifdef ISP2401
	/* Real sensor data area on the internal frame at shading correction.
	 * Filters and scaling are applied to the internal frame before shading correction, depending on the binary.
	 */
	sensor_org_x_bqs_on_internal = scr.sensor_data_origin_x_bqs_on_internal;
	sensor_org_y_bqs_on_internal = scr.sensor_data_origin_y_bqs_on_internal;
	{
		unsigned int bs_frac = 8;	/* scaling factor 1.0 in fixed point (8 == FRAC_ACC macro in ISP) */
		unsigned int bs_out, bs_in;	/* scaling ratio in fixed point */

		bs_out = scr.bayer_scale_hor_ratio_out * bs_frac;
		bs_in = scr.bayer_scale_hor_ratio_in * bs_frac;
		sensor_width_bqs  = (in_width_bqs * bs_out + bs_in/2) / bs_in; /* "+ bs_in/2": rounding */

		bs_out = scr.bayer_scale_ver_ratio_out * bs_frac;
		bs_in = scr.bayer_scale_ver_ratio_in * bs_frac;
		sensor_height_bqs = (in_height_bqs * bs_out + bs_in/2) / bs_in; /* "+ bs_in/2": rounding */
	}

	/* Center of the sensor data on the internal frame at shading correction. */
	sensor_center_x_bqs_on_internal = sensor_org_x_bqs_on_internal + sensor_width_bqs / 2;
	sensor_center_y_bqs_on_internal = sensor_org_y_bqs_on_internal + sensor_height_bqs / 2;

	/* Size of left/right/upper/lower sides of the sensor center on the internal frame. */
	left  = sensor_center_x_bqs_on_internal;
	right = internal_width_bqs - sensor_center_x_bqs_on_internal;
	upper = sensor_center_y_bqs_on_internal;
	lower = internal_height_bqs - sensor_center_y_bqs_on_internal;

	/* Align the size of left/right/upper/lower sides to a multiple of the grid cell size. */
	adjust_left  = CEIL_MUL(left,  bqs_per_grid_cell);
	adjust_right = CEIL_MUL(right, bqs_per_grid_cell);
	adjust_upper = CEIL_MUL(upper, bqs_per_grid_cell);
	adjust_lower = CEIL_MUL(lower, bqs_per_grid_cell);

	/* Shading table should cover the adjusted frame size. */
	adjust_width_bqs  = adjust_left + adjust_right;
	adjust_height_bqs = adjust_upper + adjust_lower;

	IA_CSS_LOG("adjust_width_bqs=%d, adjust_height_bqs=%d", adjust_width_bqs, adjust_height_bqs);

	if (adjust_width_bqs > tbl_width_bqs || adjust_height_bqs > tbl_height_bqs) {
		IA_CSS_LEAVE_ERR_PRIVATE(IA_CSS_ERR_INTERNAL_ERROR);
		return IA_CSS_ERR_INTERNAL_ERROR;
	}

	/* Origin of the internal frame on the shading table. */
	internal_org_x_bqs_on_tbl = adjust_left - left;
	internal_org_y_bqs_on_tbl = adjust_upper - upper;

	/* Origin of the real sensor data area on the shading table. */
	sensor_org_x_bqs_on_tbl = internal_org_x_bqs_on_tbl + sensor_org_x_bqs_on_internal;
	sensor_org_y_bqs_on_tbl = internal_org_y_bqs_on_tbl + sensor_org_y_bqs_on_internal;

	/* The shading information necessary as API is stored in the shading_info. */
	shading_info->info.type_1.num_hor_grids	    = num_hor_grids;
	shading_info->info.type_1.num_ver_grids	    = num_ver_grids;
	shading_info->info.type_1.bqs_per_grid_cell = bqs_per_grid_cell;

	shading_info->info.type_1.bayer_scale_hor_ratio_in  = scr.bayer_scale_hor_ratio_in;
	shading_info->info.type_1.bayer_scale_hor_ratio_out = scr.bayer_scale_hor_ratio_out;
	shading_info->info.type_1.bayer_scale_ver_ratio_in  = scr.bayer_scale_ver_ratio_in;
	shading_info->info.type_1.bayer_scale_ver_ratio_out = scr.bayer_scale_ver_ratio_out;

	shading_info->info.type_1.isp_input_sensor_data_res_bqs.width  = in_width_bqs;
	shading_info->info.type_1.isp_input_sensor_data_res_bqs.height = in_height_bqs;

	shading_info->info.type_1.sensor_data_res_bqs.width  = sensor_width_bqs;
	shading_info->info.type_1.sensor_data_res_bqs.height = sensor_height_bqs;

	shading_info->info.type_1.sensor_data_origin_bqs_on_sctbl.x = (int32_t)sensor_org_x_bqs_on_tbl;
	shading_info->info.type_1.sensor_data_origin_bqs_on_sctbl.y = (int32_t)sensor_org_y_bqs_on_tbl;

	/* The shading information related to ISP (but, not necessary as API) is stored in the pipe_config. */
	pipe_config->internal_frame_origin_bqs_on_sctbl.x = (int32_t)internal_org_x_bqs_on_tbl;
	pipe_config->internal_frame_origin_bqs_on_sctbl.y = (int32_t)internal_org_y_bqs_on_tbl;

	IA_CSS_LOG("shading_info: grids=%dx%d, cell=%d, scale=%d,%d,%d,%d, input=%dx%d, data=%dx%d, origin=(%d,%d)",
		shading_info->info.type_1.num_hor_grids,
		shading_info->info.type_1.num_ver_grids,
		shading_info->info.type_1.bqs_per_grid_cell,
		shading_info->info.type_1.bayer_scale_hor_ratio_in,
		shading_info->info.type_1.bayer_scale_hor_ratio_out,
		shading_info->info.type_1.bayer_scale_ver_ratio_in,
		shading_info->info.type_1.bayer_scale_ver_ratio_out,
		shading_info->info.type_1.isp_input_sensor_data_res_bqs.width,
		shading_info->info.type_1.isp_input_sensor_data_res_bqs.height,
		shading_info->info.type_1.sensor_data_res_bqs.width,
		shading_info->info.type_1.sensor_data_res_bqs.height,
		shading_info->info.type_1.sensor_data_origin_bqs_on_sctbl.x,
		shading_info->info.type_1.sensor_data_origin_bqs_on_sctbl.y);

	IA_CSS_LOG("pipe_config: origin=(%d,%d)",
		pipe_config->internal_frame_origin_bqs_on_sctbl.x,
		pipe_config->internal_frame_origin_bqs_on_sctbl.y);

	IA_CSS_LEAVE_ERR_PRIVATE(err);
#endif
	return err;
}

enum ia_css_err
ia_css_binary_get_shading_info(const struct ia_css_binary *binary,			/* [in] */
				enum ia_css_shading_correction_type type,		/* [in] */
				unsigned int required_bds_factor,			/* [in] */
				const struct ia_css_stream_config *stream_config,	/* [in] */
#ifndef ISP2401
				struct ia_css_shading_info *info)			/* [out] */
#else
				struct ia_css_shading_info *shading_info,		/* [out] */
				struct ia_css_pipe_config *pipe_config)			/* [out] */
#endif
{
	enum ia_css_err err;

	assert(binary != NULL);
#ifndef ISP2401
	assert(info != NULL);
#else
	assert(shading_info != NULL);

	IA_CSS_ENTER_PRIVATE("binary=%p, type=%d, required_bds_factor=%d, stream_config=%p",
		binary, type, required_bds_factor, stream_config);
#endif

	if (type == IA_CSS_SHADING_CORRECTION_TYPE_1)
#ifndef ISP2401
		err = ia_css_binary_get_shading_info_type_1(binary, required_bds_factor, stream_config, info);
#else
		err = ia_css_binary_get_shading_info_type_1(binary, required_bds_factor, stream_config,
								shading_info, pipe_config);
#endif

	/* Other function calls can be added here when other shading correction types will be added in the future. */

	else
		err = IA_CSS_ERR_NOT_SUPPORTED;

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static void sh_css_binary_common_grid_info(const struct ia_css_binary *binary,
				struct ia_css_grid_info *info)
{
	assert(binary != NULL);
	assert(info != NULL);

	info->isp_in_width = binary->internal_frame_info.res.width;
	info->isp_in_height = binary->internal_frame_info.res.height;

	info->vamem_type = IA_CSS_VAMEM_TYPE_2;
}

void
ia_css_binary_dvs_grid_info(const struct ia_css_binary *binary,
			    struct ia_css_grid_info *info,
			    struct ia_css_pipe *pipe)
{
	struct ia_css_dvs_grid_info *dvs_info;

	(void)pipe;
	assert(binary != NULL);
	assert(info != NULL);

	dvs_info = &info->dvs_grid.dvs_grid_info;

	/* for DIS, we use a division instead of a ceil_div. If this is smaller
	 * than the 3a grid size, it indicates that the outer values are not
	 * valid for DIS.
	 */
	dvs_info->enable            = binary->info->sp.enable.dis;
	dvs_info->width             = binary->dis.grid.dim.width;
	dvs_info->height            = binary->dis.grid.dim.height;
	dvs_info->aligned_width     = binary->dis.grid.pad.width;
	dvs_info->aligned_height    = binary->dis.grid.pad.height;
	dvs_info->bqs_per_grid_cell = 1 << binary->dis.deci_factor_log2;
	dvs_info->num_hor_coefs     = binary->dis.coef.dim.width;
	dvs_info->num_ver_coefs     = binary->dis.coef.dim.height;

	sh_css_binary_common_grid_info(binary, info);
}

void
ia_css_binary_dvs_stat_grid_info(
	const struct ia_css_binary *binary,
	struct ia_css_grid_info *info,
	struct ia_css_pipe *pipe)
{
	(void)pipe;
	sh_css_binary_common_grid_info(binary, info);
	return;
}

enum ia_css_err
ia_css_binary_3a_grid_info(const struct ia_css_binary *binary,
			   struct ia_css_grid_info *info,
			   struct ia_css_pipe *pipe)
{
	struct ia_css_3a_grid_info *s3a_info;
	enum ia_css_err err = IA_CSS_SUCCESS;

	IA_CSS_ENTER_PRIVATE("binary=%p, info=%p, pipe=%p",
			     binary, info, pipe);

	assert(binary != NULL);
	assert(info != NULL);
	s3a_info = &info->s3a_grid;


	/* 3A statistics grid */
	s3a_info->enable            = binary->info->sp.enable.s3a;
	s3a_info->width             = binary->s3atbl_width;
	s3a_info->height            = binary->s3atbl_height;
	s3a_info->aligned_width     = binary->s3atbl_isp_width;
	s3a_info->aligned_height    = binary->s3atbl_isp_height;
	s3a_info->bqs_per_grid_cell = (1 << binary->deci_factor_log2);
	s3a_info->deci_factor_log2  = binary->deci_factor_log2;
	s3a_info->elem_bit_depth    = SH_CSS_BAYER_BITS;
	s3a_info->use_dmem          = binary->info->sp.s3a.s3atbl_use_dmem;
#if defined(HAS_NO_HMEM)
	s3a_info->has_histogram     = 1;
#else
	s3a_info->has_histogram     = 0;
#endif
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static void
binary_init_pc_histogram(struct sh_css_pc_histogram *histo)
{
	assert(histo != NULL);

	histo->length = 0;
	histo->run = NULL;
	histo->stall = NULL;
}

static void
binary_init_metrics(struct sh_css_binary_metrics *metrics,
	     const struct ia_css_binary_info *info)
{
	assert(metrics != NULL);
	assert(info != NULL);

	metrics->mode = info->pipeline.mode;
	metrics->id   = info->id;
	metrics->next = NULL;
	binary_init_pc_histogram(&metrics->isp_histogram);
	binary_init_pc_histogram(&metrics->sp_histogram);
}

/* move to host part of output module */
static bool
binary_supports_output_format(const struct ia_css_binary_xinfo *info,
		       enum ia_css_frame_format format)
{
	int i;

	assert(info != NULL);

	for (i = 0; i < info->num_output_formats; i++) {
		if (info->output_formats[i] == format)
			return true;
	}
	return false;
}

#ifdef ISP2401
static bool
binary_supports_input_format(const struct ia_css_binary_xinfo *info,
			     enum ia_css_stream_format format)
{

	assert(info != NULL);
	(void)format;

	return true;
}
#endif

static bool
binary_supports_vf_format(const struct ia_css_binary_xinfo *info,
			  enum ia_css_frame_format format)
{
	int i;

	assert(info != NULL);

	for (i = 0; i < info->num_vf_formats; i++) {
		if (info->vf_formats[i] == format)
			return true;
	}
	return false;
}

/* move to host part of bds module */
static bool
supports_bds_factor(uint32_t supported_factors,
		       uint32_t bds_factor)
{
	return ((supported_factors & PACK_BDS_FACTOR(bds_factor)) != 0);
}

static enum ia_css_err
binary_init_info(struct ia_css_binary_xinfo *info, unsigned int i,
		 bool *binary_found)
{
	const unsigned char *blob = sh_css_blob_info[i].blob;
	unsigned size = sh_css_blob_info[i].header.blob.size;

	if ((info == NULL) || (binary_found == NULL))
		return IA_CSS_ERR_INVALID_ARGUMENTS;

	*info = sh_css_blob_info[i].header.info.isp;
	*binary_found = blob != NULL;
	info->blob_index = i;
	/* we don't have this binary, skip it */
	if (!size)
		return IA_CSS_SUCCESS;

	info->xmem_addr = sh_css_load_blob(blob, size);
	if (!info->xmem_addr)
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
	return IA_CSS_SUCCESS;
}

/* When binaries are put at the beginning, they will only
 * be selected if no other primary matches.
 */
enum ia_css_err
ia_css_binary_init_infos(void)
{
	unsigned int i;
	unsigned int num_of_isp_binaries = sh_css_num_binaries - NUM_OF_SPS - NUM_OF_BLS;

	if (num_of_isp_binaries == 0)
		return IA_CSS_SUCCESS;

	all_binaries = sh_css_malloc(num_of_isp_binaries *
						sizeof(*all_binaries));
	if (all_binaries == NULL)
		return IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;

	for (i = 0; i < num_of_isp_binaries; i++) {
		enum ia_css_err ret;
		struct ia_css_binary_xinfo *binary = &all_binaries[i];
		bool binary_found;

		ret = binary_init_info(binary, i, &binary_found);
		if (ret != IA_CSS_SUCCESS)
			return ret;
		if (!binary_found)
			continue;
		/* Prepend new binary information */
		binary->next = binary_infos[binary->sp.pipeline.mode];
		binary_infos[binary->sp.pipeline.mode] = binary;
		binary->blob = &sh_css_blob_info[i];
		binary->mem_offsets = sh_css_blob_info[i].mem_offsets;
	}
	return IA_CSS_SUCCESS;
}

enum ia_css_err
ia_css_binary_uninit(void)
{
	unsigned int i;
	struct ia_css_binary_xinfo *b;

	for (i = 0; i < IA_CSS_BINARY_NUM_MODES; i++) {
		for (b = binary_infos[i]; b; b = b->next) {
			if (b->xmem_addr)
				hmm_free(b->xmem_addr);
			b->xmem_addr = mmgr_NULL;
		}
		binary_infos[i] = NULL;
	}
	sh_css_free(all_binaries);
	return IA_CSS_SUCCESS;
}

/* @brief Compute decimation factor for 3A statistics and shading correction.
 *
 * @param[in]	width	Frame width in pixels.
 * @param[in]	height	Frame height in pixels.
 * @return	Log2 of decimation factor (= grid cell size) in bayer quads.
 */
static int
binary_grid_deci_factor_log2(int width, int height)
{
/* 3A/Shading decimation factor spcification (at August 2008)
 * ------------------------------------------------------------------
 * [Image Width (BQ)] [Decimation Factor (BQ)] [Resulting grid cells]
#ifndef ISP2401
 * 1280 ?c             32                       40 ?c
 *  640 ?c 1279        16                       40 ?c 80
 *      ?c  639         8                          ?c 80
#else
 * from 1280                   32                 from 40
 * from  640 to 1279           16                 from 40 to 80
 *           to  639            8                         to 80
#endif
 * ------------------------------------------------------------------
 */
/* Maximum and minimum decimation factor by the specification */
#define MAX_SPEC_DECI_FACT_LOG2		5
#define MIN_SPEC_DECI_FACT_LOG2		3
/* the smallest frame width in bayer quads when decimation factor (log2) is 5 or 4, by the specification */
#define DECI_FACT_LOG2_5_SMALLEST_FRAME_WIDTH_BQ	1280
#define DECI_FACT_LOG2_4_SMALLEST_FRAME_WIDTH_BQ	640

	int smallest_factor; /* the smallest factor (log2) where the number of cells does not exceed the limitation */
	int spec_factor;     /* the factor (log2) which satisfies the specification */

	/* Currently supported maximum width and height are 5120(=80*64) and 3840(=60*64). */
	assert(ISP_BQ_GRID_WIDTH(width, MAX_SPEC_DECI_FACT_LOG2) <= SH_CSS_MAX_BQ_GRID_WIDTH);
	assert(ISP_BQ_GRID_HEIGHT(height, MAX_SPEC_DECI_FACT_LOG2) <= SH_CSS_MAX_BQ_GRID_HEIGHT);

	/* Compute the smallest factor. */
	smallest_factor = MAX_SPEC_DECI_FACT_LOG2;
	while (ISP_BQ_GRID_WIDTH(width, smallest_factor - 1) <= SH_CSS_MAX_BQ_GRID_WIDTH &&
	       ISP_BQ_GRID_HEIGHT(height, smallest_factor - 1) <= SH_CSS_MAX_BQ_GRID_HEIGHT
	       && smallest_factor > MIN_SPEC_DECI_FACT_LOG2)
		smallest_factor--;

	/* Get the factor by the specification. */
	if (_ISP_BQS(width) >= DECI_FACT_LOG2_5_SMALLEST_FRAME_WIDTH_BQ)
		spec_factor = 5;
	else if (_ISP_BQS(width) >= DECI_FACT_LOG2_4_SMALLEST_FRAME_WIDTH_BQ)
		spec_factor = 4;
	else
		spec_factor = 3;

	/* If smallest_factor is smaller than or equal to spec_factor, choose spec_factor to follow the specification.
	   If smallest_factor is larger than spec_factor, choose smallest_factor.

		ex. width=2560, height=1920
			smallest_factor=4, spec_factor=5
			smallest_factor < spec_factor   ->   return spec_factor

		ex. width=300, height=3000
			smallest_factor=5, spec_factor=3
			smallest_factor > spec_factor   ->   return smallest_factor
	*/
	return max(smallest_factor, spec_factor);

#undef MAX_SPEC_DECI_FACT_LOG2
#undef MIN_SPEC_DECI_FACT_LOG2
#undef DECI_FACT_LOG2_5_SMALLEST_FRAME_WIDTH_BQ
#undef DECI_FACT_LOG2_4_SMALLEST_FRAME_WIDTH_BQ
}

static int
binary_in_frame_padded_width(int in_frame_width,
			     int isp_internal_width,
			     int dvs_env_width,
			     int stream_config_left_padding,
			     int left_cropping,
			     bool need_scaling)
{
	int rval;
	int nr_of_left_paddings;	/* number of paddings pixels on the left of an image line */

#if defined(USE_INPUT_SYSTEM_VERSION_2401)
	/* the output image line of Input System 2401 does not have the left paddings  */
	nr_of_left_paddings = 0;
#else
	/* in other cases, the left padding pixels are always 128 */
	nr_of_left_paddings = 2*ISP_VEC_NELEMS;
#endif
	if (need_scaling) {
		/* In SDV use-case, we need to match left-padding of
		 * primary and the video binary. */
		if (stream_config_left_padding != -1) {
			/* Different than before, we do left&right padding. */
			rval =
				CEIL_MUL(in_frame_width + nr_of_left_paddings,
					2*ISP_VEC_NELEMS);
		} else {
			/* Different than before, we do left&right padding. */
			in_frame_width += dvs_env_width;
			rval =
				CEIL_MUL(in_frame_width +
					(left_cropping ? nr_of_left_paddings : 0),
					2*ISP_VEC_NELEMS);
		}
	} else {
		rval = isp_internal_width;
	}

	return rval;
}


enum ia_css_err
ia_css_binary_fill_info(const struct ia_css_binary_xinfo *xinfo,
		 bool online,
		 bool two_ppc,
		 enum ia_css_stream_format stream_format,
		 const struct ia_css_frame_info *in_info, /* can be NULL */
		 const struct ia_css_frame_info *bds_out_info, /* can be NULL */
		 const struct ia_css_frame_info *out_info[], /* can be NULL */
		 const struct ia_css_frame_info *vf_info, /* can be NULL */
		 struct ia_css_binary *binary,
		 struct ia_css_resolution *dvs_env,
		 int stream_config_left_padding,
		 bool accelerator)
{
	const struct ia_css_binary_info *info = &xinfo->sp;
	unsigned int dvs_env_width = 0,
		     dvs_env_height = 0,
		     vf_log_ds = 0,
		     s3a_log_deci = 0,
		     bits_per_pixel = 0,
		     /* Resolution at SC/3A/DIS kernel. */
		     sc_3a_dis_width = 0,
		     /* Resolution at SC/3A/DIS kernel. */
		     sc_3a_dis_padded_width = 0,
		     /* Resolution at SC/3A/DIS kernel. */
		     sc_3a_dis_height = 0,
		     isp_internal_width = 0,
		     isp_internal_height = 0,
		     s3a_isp_width = 0;

	bool need_scaling = false;
	struct ia_css_resolution binary_dvs_env, internal_res;
	enum ia_css_err err;
	unsigned int i;
	const struct ia_css_frame_info *bin_out_info = NULL;

	assert(info != NULL);
	assert(binary != NULL);

	binary->info = xinfo;
	if (!accelerator) {
		/* binary->css_params has been filled by accelerator itself. */
		err = ia_css_isp_param_allocate_isp_parameters(
			&binary->mem_params, &binary->css_params,
			&info->mem_initializers);
		if (err != IA_CSS_SUCCESS) {
			return err;
		}
	}
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		if (out_info[i] && (out_info[i]->res.width != 0)) {
			bin_out_info = out_info[i];
			break;
		}
	}
	if (in_info != NULL && bin_out_info != NULL) {
		need_scaling = (in_info->res.width != bin_out_info->res.width) ||
			(in_info->res.height != bin_out_info->res.height);
	}


	/* binary_dvs_env has to be equal or larger than SH_CSS_MIN_DVS_ENVELOPE */
	binary_dvs_env.width = 0;
	binary_dvs_env.height = 0;
	ia_css_binary_dvs_env(info, dvs_env, &binary_dvs_env);
	dvs_env_width = binary_dvs_env.width;
	dvs_env_height = binary_dvs_env.height;
	binary->dvs_envelope.width  = dvs_env_width;
	binary->dvs_envelope.height = dvs_env_height;

	/* internal resolution calculation */
	internal_res.width = 0;
	internal_res.height = 0;
	ia_css_binary_internal_res(in_info, bds_out_info, bin_out_info, dvs_env,
				   info, &internal_res);
	isp_internal_width = internal_res.width;
	isp_internal_height = internal_res.height;

	/* internal frame info */
	if (bin_out_info != NULL) /* { */
		binary->internal_frame_info.format = bin_out_info->format;
	/* } */
	binary->internal_frame_info.res.width       = isp_internal_width;
	binary->internal_frame_info.padded_width    = CEIL_MUL(isp_internal_width, 2*ISP_VEC_NELEMS);
	binary->internal_frame_info.res.height      = isp_internal_height;
	binary->internal_frame_info.raw_bit_depth   = bits_per_pixel;

	if (in_info != NULL) {
		binary->effective_in_frame_res.width = in_info->res.width;
		binary->effective_in_frame_res.height = in_info->res.height;

		bits_per_pixel = in_info->raw_bit_depth;

		/* input info */
		binary->in_frame_info.res.width = in_info->res.width + info->pipeline.left_cropping;
		binary->in_frame_info.res.height = in_info->res.height + info->pipeline.top_cropping;

		binary->in_frame_info.res.width += dvs_env_width;
		binary->in_frame_info.res.height += dvs_env_height;

		binary->in_frame_info.padded_width =
			binary_in_frame_padded_width(in_info->res.width,
						     isp_internal_width,
						     dvs_env_width,
						     stream_config_left_padding,
						     info->pipeline.left_cropping,
						     need_scaling);

		binary->in_frame_info.format = in_info->format;
		binary->in_frame_info.raw_bayer_order = in_info->raw_bayer_order;
		binary->in_frame_info.crop_info = in_info->crop_info;
	}

	if (online) {
		bits_per_pixel = ia_css_util_input_format_bpp(
			stream_format, two_ppc);
	}
	binary->in_frame_info.raw_bit_depth = bits_per_pixel;

	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		if (out_info[i] != NULL) {
			binary->out_frame_info[i].res.width     = out_info[i]->res.width;
			binary->out_frame_info[i].res.height    = out_info[i]->res.height;
			binary->out_frame_info[i].padded_width  = out_info[i]->padded_width;
			if (info->pipeline.mode == IA_CSS_BINARY_MODE_COPY) {
				binary->out_frame_info[i].raw_bit_depth = bits_per_pixel;
			} else {
				/* Only relevant for RAW format.
				 * At the moment, all outputs are raw, 16 bit per pixel, except for copy.
				 * To do this cleanly, the binary should specify in its info
				 * the bit depth per output channel.
				 */
				binary->out_frame_info[i].raw_bit_depth = 16;
			}
			binary->out_frame_info[i].format        = out_info[i]->format;
		}
	}

	if (vf_info && (vf_info->res.width != 0)) {
		err = ia_css_vf_configure(binary, bin_out_info, (struct ia_css_frame_info *)vf_info, &vf_log_ds);
		if (err != IA_CSS_SUCCESS) {
			if (!accelerator) {
				ia_css_isp_param_destroy_isp_parameters(
					&binary->mem_params,
					&binary->css_params);
			}
			return err;
		}
	}
	binary->vf_downscale_log2 = vf_log_ds;

	binary->online            = online;
	binary->input_format      = stream_format;

	/* viewfinder output info */
	if ((vf_info != NULL) && (vf_info->res.width != 0)) {
		unsigned int vf_out_vecs, vf_out_width, vf_out_height;
		binary->vf_frame_info.format = vf_info->format;
		if (bin_out_info == NULL)
			return IA_CSS_ERR_INTERNAL_ERROR;
		vf_out_vecs = __ISP_VF_OUTPUT_WIDTH_VECS(bin_out_info->padded_width,
			vf_log_ds);
		vf_out_width = _ISP_VF_OUTPUT_WIDTH(vf_out_vecs);
		vf_out_height = _ISP_VF_OUTPUT_HEIGHT(bin_out_info->res.height,
			vf_log_ds);

		/* For preview mode, output pin is used instead of vf. */
		if (info->pipeline.mode == IA_CSS_BINARY_MODE_PREVIEW) {
			binary->out_frame_info[0].res.width =
				(bin_out_info->res.width >> vf_log_ds);
			binary->out_frame_info[0].padded_width = vf_out_width;
			binary->out_frame_info[0].res.height   = vf_out_height;

			binary->vf_frame_info.res.width    = 0;
			binary->vf_frame_info.padded_width = 0;
			binary->vf_frame_info.res.height   = 0;
		} else {
			/* we also store the raw downscaled width. This is
			 * used for digital zoom in preview to zoom only on
			 * the width that we actually want to keep, not on
			 * the aligned width. */
			binary->vf_frame_info.res.width =
				(bin_out_info->res.width >> vf_log_ds);
			binary->vf_frame_info.padded_width = vf_out_width;
			binary->vf_frame_info.res.height   = vf_out_height;
		}
	} else {
		binary->vf_frame_info.res.width    = 0;
		binary->vf_frame_info.padded_width = 0;
		binary->vf_frame_info.res.height   = 0;
	}

	if (info->enable.ca_gdc) {
		binary->morph_tbl_width =
			_ISP_MORPH_TABLE_WIDTH(isp_internal_width);
		binary->morph_tbl_aligned_width  =
			_ISP_MORPH_TABLE_ALIGNED_WIDTH(isp_internal_width);
		binary->morph_tbl_height =
			_ISP_MORPH_TABLE_HEIGHT(isp_internal_height);
	} else {
		binary->morph_tbl_width  = 0;
		binary->morph_tbl_aligned_width  = 0;
		binary->morph_tbl_height = 0;
	}

	sc_3a_dis_width = binary->in_frame_info.res.width;
	sc_3a_dis_padded_width = binary->in_frame_info.padded_width;
	sc_3a_dis_height = binary->in_frame_info.res.height;
	if (bds_out_info != NULL && in_info != NULL &&
			bds_out_info->res.width != in_info->res.width) {
		/* TODO: Next, "internal_frame_info" should be derived from
		 * bds_out. So this part will change once it is in place! */
		sc_3a_dis_width = bds_out_info->res.width + info->pipeline.left_cropping;
		sc_3a_dis_padded_width = isp_internal_width;
		sc_3a_dis_height = isp_internal_height;
	}


	s3a_isp_width = _ISP_S3A_ELEMS_ISP_WIDTH(sc_3a_dis_padded_width,
		info->pipeline.left_cropping);
	if (info->s3a.fixed_s3a_deci_log) {
		s3a_log_deci = info->s3a.fixed_s3a_deci_log;
	} else {
		s3a_log_deci = binary_grid_deci_factor_log2(s3a_isp_width,
							    sc_3a_dis_height);
	}
	binary->deci_factor_log2  = s3a_log_deci;

	if (info->enable.s3a) {
		binary->s3atbl_width  =
			_ISP_S3ATBL_WIDTH(sc_3a_dis_width,
				s3a_log_deci);
		binary->s3atbl_height =
			_ISP_S3ATBL_HEIGHT(sc_3a_dis_height,
				s3a_log_deci);
		binary->s3atbl_isp_width =
			_ISP_S3ATBL_ISP_WIDTH(s3a_isp_width,
					s3a_log_deci);
		binary->s3atbl_isp_height =
			_ISP_S3ATBL_ISP_HEIGHT(sc_3a_dis_height,
				s3a_log_deci);
	} else {
		binary->s3atbl_width  = 0;
		binary->s3atbl_height = 0;
		binary->s3atbl_isp_width  = 0;
		binary->s3atbl_isp_height = 0;
	}

	if (info->enable.sc) {
		binary->sctbl_width_per_color  =
#ifndef ISP2401
			_ISP_SCTBL_WIDTH_PER_COLOR(sc_3a_dis_padded_width,
				s3a_log_deci);
#else
			_ISP_SCTBL_WIDTH_PER_COLOR(isp_internal_width, s3a_log_deci);
#endif
		binary->sctbl_aligned_width_per_color =
			SH_CSS_MAX_SCTBL_ALIGNED_WIDTH_PER_COLOR;
		binary->sctbl_height =
#ifndef ISP2401
			_ISP_SCTBL_HEIGHT(sc_3a_dis_height, s3a_log_deci);
#else
			_ISP_SCTBL_HEIGHT(isp_internal_height, s3a_log_deci);
		binary->sctbl_legacy_width_per_color  =
			_ISP_SCTBL_LEGACY_WIDTH_PER_COLOR(sc_3a_dis_padded_width, s3a_log_deci);
		binary->sctbl_legacy_height =
			_ISP_SCTBL_LEGACY_HEIGHT(sc_3a_dis_height, s3a_log_deci);
#endif
	} else {
		binary->sctbl_width_per_color         = 0;
		binary->sctbl_aligned_width_per_color = 0;
		binary->sctbl_height                  = 0;
#ifdef ISP2401
		binary->sctbl_legacy_width_per_color  = 0;
		binary->sctbl_legacy_height	      = 0;
#endif
	}
	ia_css_sdis_init_info(&binary->dis,
				sc_3a_dis_width,
				sc_3a_dis_padded_width,
				sc_3a_dis_height,
				info->pipeline.isp_pipe_version,
				info->enable.dis);
	if (info->pipeline.left_cropping)
		binary->left_padding = 2 * ISP_VEC_NELEMS - info->pipeline.left_cropping;
	else
		binary->left_padding = 0;

	return IA_CSS_SUCCESS;
}

enum ia_css_err
ia_css_binary_find(struct ia_css_binary_descr *descr,
		   struct ia_css_binary *binary)
{
	int mode;
	bool online;
	bool two_ppc;
	enum ia_css_stream_format stream_format;
	const struct ia_css_frame_info *req_in_info,
				       *req_bds_out_info,
				       *req_out_info[IA_CSS_BINARY_MAX_OUTPUT_PORTS],
				       *req_bin_out_info = NULL,
				       *req_vf_info;

	struct ia_css_binary_xinfo *xcandidate;
#ifndef ISP2401
	bool need_ds, need_dz, need_dvs, need_xnr, need_dpc;
#else
	bool need_ds, need_dz, need_dvs, need_xnr, need_dpc, need_tnr;
#endif
	bool striped;
	bool enable_yuv_ds;
	bool enable_high_speed;
	bool enable_dvs_6axis;
	bool enable_reduced_pipe;
	bool enable_capture_pp_bli;
#ifdef ISP2401
	bool enable_luma_only;
#endif
	enum ia_css_err err = IA_CSS_ERR_INTERNAL_ERROR;
	bool continuous;
	unsigned int isp_pipe_version;
	struct ia_css_resolution dvs_env, internal_res;
	unsigned int i;

	assert(descr != NULL);
	/* MW: used after an error check, may accept NULL, but doubtfull */
	assert(binary != NULL);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_binary_find() enter: descr=%p, (mode=%d), binary=%p\n",
		descr, descr->mode,
		binary);

	mode = descr->mode;
	online = descr->online;
	two_ppc = descr->two_ppc;
	stream_format = descr->stream_format;
	req_in_info = descr->in_info;
	req_bds_out_info = descr->bds_out_info;
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++) {
		req_out_info[i] = descr->out_info[i];
		if (req_out_info[i] && (req_out_info[i]->res.width != 0))
			req_bin_out_info = req_out_info[i];
	}
	if (req_bin_out_info == NULL)
		return IA_CSS_ERR_INTERNAL_ERROR;
#ifndef ISP2401
	req_vf_info = descr->vf_info;
#else

	if ((descr->vf_info != NULL) && (descr->vf_info->res.width == 0))
		/* width==0 means that there is no vf pin (e.g. in SkyCam preview case) */
		req_vf_info = NULL;
	else
		req_vf_info = descr->vf_info;
#endif

	need_xnr = descr->enable_xnr;
	need_ds = descr->enable_fractional_ds;
	need_dz = false;
	need_dvs = false;
	need_dpc = descr->enable_dpc;
#ifdef ISP2401
	need_tnr = descr->enable_tnr;
#endif
	enable_yuv_ds = descr->enable_yuv_ds;
	enable_high_speed = descr->enable_high_speed;
	enable_dvs_6axis  = descr->enable_dvs_6axis;
	enable_reduced_pipe = descr->enable_reduced_pipe;
	enable_capture_pp_bli = descr->enable_capture_pp_bli;
#ifdef ISP2401
	enable_luma_only = descr->enable_luma_only;
#endif
	continuous = descr->continuous;
	striped = descr->striped;
	isp_pipe_version = descr->isp_pipe_version;

	dvs_env.width = 0;
	dvs_env.height = 0;
	internal_res.width = 0;
	internal_res.height = 0;


	if (mode == IA_CSS_BINARY_MODE_VIDEO) {
		dvs_env = descr->dvs_env;
		need_dz = descr->enable_dz;
		/* Video is the only mode that has a nodz variant. */
		need_dvs = dvs_env.width || dvs_env.height;
	}

	/* print a map of the binary file */
	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,	"BINARY INFO:\n");
	for (i = 0; i < IA_CSS_BINARY_NUM_MODES; i++) {
		xcandidate = binary_infos[i];
		if (xcandidate) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,	"%d:\n", i);
			while (xcandidate) {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE, " Name:%s Type:%d Cont:%d\n",
						xcandidate->blob->name, xcandidate->type,
						xcandidate->sp.enable.continuous);
				xcandidate = xcandidate->next;
			}
		}
	}

	/* printf("sh_css_binary_find: pipe version %d\n", isp_pipe_version); */
	for (xcandidate = binary_infos[mode]; xcandidate;
	     xcandidate = xcandidate->next) {
		struct ia_css_binary_info *candidate = &xcandidate->sp;
		/* printf("sh_css_binary_find: evaluating candidate:
		 * %d\n",candidate->id); */
		ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"ia_css_binary_find() candidate = %p, mode = %d ID = %d\n",
			candidate, candidate->pipeline.mode, candidate->id);

		/*
		 * MW: Only a limited set of jointly configured binaries can
		 * be used in a continuous preview/video mode unless it is
		 * the copy mode and runs on SP.
		*/
		if (!candidate->enable.continuous &&
		    continuous && (mode != IA_CSS_BINARY_MODE_COPY)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && %d && (%d != %d)\n",
					__LINE__, candidate->enable.continuous,
					continuous, mode,
					IA_CSS_BINARY_MODE_COPY);
			continue;
		}
		if (striped && candidate->iterator.num_stripes == 1) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: binary is not striped\n",
					__LINE__);
			continue;
		}

		if (candidate->pipeline.isp_pipe_version != isp_pipe_version &&
		    (mode != IA_CSS_BINARY_MODE_COPY) &&
		    (mode != IA_CSS_BINARY_MODE_CAPTURE_PP) &&
		    (mode != IA_CSS_BINARY_MODE_VF_PP)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: (%d != %d)\n",
				__LINE__,
				candidate->pipeline.isp_pipe_version, isp_pipe_version);
			continue;
		}
		if (!candidate->enable.reduced_pipe && enable_reduced_pipe) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__,
				candidate->enable.reduced_pipe,
				enable_reduced_pipe);
			continue;
		}
		if (!candidate->enable.dvs_6axis && enable_dvs_6axis) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__,
				candidate->enable.dvs_6axis,
				enable_dvs_6axis);
			continue;
		}
		if (candidate->enable.high_speed && !enable_high_speed) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: %d && !%d\n",
				__LINE__,
				candidate->enable.high_speed,
				enable_high_speed);
			continue;
		}
		if (!candidate->enable.xnr && need_xnr) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: %d && !%d\n",
				__LINE__,
				candidate->enable.xnr,
				need_xnr);
			continue;
		}
		if (!(candidate->enable.ds & 2) && enable_yuv_ds) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__,
				((candidate->enable.ds & 2) != 0),
				enable_yuv_ds);
			continue;
		}
		if ((candidate->enable.ds & 2) && !enable_yuv_ds) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: %d && !%d\n",
				__LINE__,
				((candidate->enable.ds & 2) != 0),
				enable_yuv_ds);
			continue;
		}

		if (mode == IA_CSS_BINARY_MODE_VIDEO &&
			candidate->enable.ds && need_ds)
			need_dz = false;

		/* when we require vf output, we need to have vf_veceven */
		if ((req_vf_info != NULL) && !(candidate->enable.vf_veceven ||
				/* or variable vf vec even */
				candidate->vf_dec.is_variable ||
				/* or more than one output pin. */
				xcandidate->num_output_pins > 1)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: (%p != NULL) && !(%d || %d || (%d >%d))\n",
				__LINE__, req_vf_info,
				candidate->enable.vf_veceven,
				candidate->vf_dec.is_variable,
				xcandidate->num_output_pins, 1);
			continue;
		}
		if (!candidate->enable.dvs_envelope && need_dvs) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__,
				candidate->enable.dvs_envelope, (int)need_dvs);
			continue;
		}
		/* internal_res check considers input, output, and dvs envelope sizes */
		ia_css_binary_internal_res(req_in_info, req_bds_out_info,
					   req_bin_out_info, &dvs_env, candidate, &internal_res);
		if (internal_res.width > candidate->internal.max_width) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"ia_css_binary_find() [%d] continue: (%d > %d)\n",
			__LINE__, internal_res.width,
			candidate->internal.max_width);
			continue;
		}
		if (internal_res.height > candidate->internal.max_height) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
			"ia_css_binary_find() [%d] continue: (%d > %d)\n",
			__LINE__, internal_res.height,
			candidate->internal.max_height);
			continue;
		}
		if (!candidate->enable.ds && need_ds && !(xcandidate->num_output_pins > 1)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__, candidate->enable.ds, (int)need_ds);
			continue;
		}
		if (!candidate->enable.uds && !candidate->enable.dvs_6axis && need_dz) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && !%d && %d\n",
				__LINE__, candidate->enable.uds,
				candidate->enable.dvs_6axis, (int)need_dz);
			continue;
		}
		if (online && candidate->input.source == IA_CSS_BINARY_INPUT_MEMORY) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: %d && (%d == %d)\n",
				__LINE__, online, candidate->input.source,
				IA_CSS_BINARY_INPUT_MEMORY);
			continue;
		}
		if (!online && candidate->input.source == IA_CSS_BINARY_INPUT_SENSOR) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && (%d == %d)\n",
				__LINE__, online, candidate->input.source,
				IA_CSS_BINARY_INPUT_SENSOR);
			continue;
		}
		if (req_bin_out_info->res.width < candidate->output.min_width ||
		    req_bin_out_info->res.width > candidate->output.max_width) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: (%d > %d) || (%d < %d)\n",
				__LINE__,
				req_bin_out_info->padded_width,
				candidate->output.min_width,
				req_bin_out_info->padded_width,
				candidate->output.max_width);
			continue;
		}
		if (xcandidate->num_output_pins > 1 && /* in case we have a second output pin, */
		     req_vf_info) { /* and we need vf output. */
			if (req_vf_info->res.width > candidate->output.max_width) {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					"ia_css_binary_find() [%d] continue: (%d < %d)\n",
					__LINE__,
					req_vf_info->res.width,
					candidate->output.max_width);
				continue;
			}
		}
		if (req_in_info->padded_width > candidate->input.max_width) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: (%d > %d)\n",
				__LINE__, req_in_info->padded_width,
				candidate->input.max_width);
			continue;
		}
		if (!binary_supports_output_format(xcandidate, req_bin_out_info->format)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d\n",
				__LINE__,
				binary_supports_output_format(xcandidate, req_bin_out_info->format));
			continue;
		}
#ifdef ISP2401
		if (!binary_supports_input_format(xcandidate, descr->stream_format)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					    "ia_css_binary_find() [%d] continue: !%d\n",
					    __LINE__,
					    binary_supports_input_format(xcandidate, req_in_info->format));
			continue;
		}
#endif
		if (xcandidate->num_output_pins > 1 && /* in case we have a second output pin, */
		    req_vf_info                   && /* and we need vf output. */
						      /* check if the required vf format
							 is supported. */
		    !binary_supports_output_format(xcandidate, req_vf_info->format)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: (%d > %d) && (%p != NULL) && !%d\n",
				__LINE__, xcandidate->num_output_pins, 1,
				req_vf_info,
				binary_supports_output_format(xcandidate, req_vf_info->format));
			continue;
		}

		/* Check if vf_veceven supports the requested vf format */
		if (xcandidate->num_output_pins == 1 &&
		    req_vf_info && candidate->enable.vf_veceven &&
		    !binary_supports_vf_format(xcandidate, req_vf_info->format)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: (%d == %d) && (%p != NULL) && %d && !%d\n",
				__LINE__, xcandidate->num_output_pins, 1,
				req_vf_info, candidate->enable.vf_veceven,
				binary_supports_vf_format(xcandidate, req_vf_info->format));
			continue;
		}

		/* Check if vf_veceven supports the requested vf width */
		if (xcandidate->num_output_pins == 1 &&
		    req_vf_info && candidate->enable.vf_veceven) { /* and we need vf output. */
			if (req_vf_info->res.width > candidate->output.max_width) {
				ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
					"ia_css_binary_find() [%d] continue: (%d < %d)\n",
					__LINE__,
					req_vf_info->res.width,
					candidate->output.max_width);
				continue;
			}
		}

		if (!supports_bds_factor(candidate->bds.supported_bds_factors,
		    descr->required_bds_factor)) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: 0x%x & 0x%x)\n",
				__LINE__, candidate->bds.supported_bds_factors,
				descr->required_bds_factor);
			continue;
		}

		if (!candidate->enable.dpc && need_dpc) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: 0x%x & 0x%x)\n",
				__LINE__, candidate->enable.dpc,
				descr->enable_dpc);
			continue;
		}

		if (candidate->uds.use_bci && enable_capture_pp_bli) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: 0x%x & 0x%x)\n",
				__LINE__, candidate->uds.use_bci,
				descr->enable_capture_pp_bli);
			continue;
		}

#ifdef ISP2401
		if (candidate->enable.luma_only != enable_luma_only) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: %d != %d\n",
				__LINE__, candidate->enable.luma_only,
				descr->enable_luma_only);
			continue;
		}

		if(!candidate->enable.tnr && need_tnr) {
			ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
				"ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__, candidate->enable.tnr,
				descr->enable_tnr);
			continue;
		}

#endif
		/* reconfigure any variable properties of the binary */
		err = ia_css_binary_fill_info(xcandidate, online, two_ppc,
				       stream_format, req_in_info,
				       req_bds_out_info,
				       req_out_info, req_vf_info,
				       binary, &dvs_env,
				       descr->stream_config_left_padding,
				       false);

		if (err != IA_CSS_SUCCESS)
			break;
		binary_init_metrics(&binary->metrics, &binary->info->sp);
		break;
	}

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_binary_find() selected = %p, mode = %d ID = %d\n",
		xcandidate, xcandidate ? xcandidate->sp.pipeline.mode : 0, xcandidate ? xcandidate->sp.id : 0);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE,
		"ia_css_binary_find() leave: return_err=%d\n", err);

	return err;
}

unsigned
ia_css_binary_max_vf_width(void)
{
	/* This is (should be) true for IPU1 and IPU2 */
	/* For IPU3 (SkyCam) this pointer is guarenteed to be NULL simply because such a binary does not exist  */
	if (binary_infos[IA_CSS_BINARY_MODE_VF_PP])
		return binary_infos[IA_CSS_BINARY_MODE_VF_PP]->sp.output.max_width;
	return 0;
}

void
ia_css_binary_destroy_isp_parameters(struct ia_css_binary *binary)
{
	if (binary) {
		ia_css_isp_param_destroy_isp_parameters(&binary->mem_params,
							&binary->css_params);
	}
}

void
ia_css_binary_get_isp_binaries(struct ia_css_binary_xinfo **binaries,
	uint32_t *num_isp_binaries)
{
	assert(binaries != NULL);

	if (num_isp_binaries)
		*num_isp_binaries = 0;

	*binaries = all_binaries;
	if (all_binaries && num_isp_binaries) {
		/* -1 to account for sp binary which is not stored in all_binaries */
		if (sh_css_num_binaries > 0)
			*num_isp_binaries = sh_css_num_binaries - 1;
	}
}
