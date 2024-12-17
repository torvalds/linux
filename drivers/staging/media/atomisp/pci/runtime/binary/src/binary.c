// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include <linux/math.h>

#include <math_support.h>
#include <gdc_device.h>	/* HR_GDC_N */

#include "hmm.h"

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

#include "atomisp_internal.h"

#include "vf/vf_1.0/ia_css_vf.host.h"
#include "sc/sc_1.0/ia_css_sc.host.h"
#include "sdis/sdis_1.0/ia_css_sdis.host.h"
#include "fixedbds/fixedbds_1.0/ia_css_fixedbds_param.h"	/* FRAC_ACC */

#include "camera/pipe/interface/ia_css_pipe_binarydesc.h"

#include "assert_support.h"

static struct ia_css_binary_xinfo *all_binaries; /* ISP binaries only (no SP) */
static struct ia_css_binary_xinfo
	*binary_infos[IA_CSS_BINARY_NUM_MODES] = { NULL, };

static void
ia_css_binary_dvs_env(const struct ia_css_binary_info *info,
		      const struct ia_css_resolution *dvs_env,
		      struct ia_css_resolution *binary_dvs_env)
{
	if (info->enable.dvs_envelope) {
		assert(dvs_env);
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
		if (in_info) {
			isp_tmp_internal_width = in_info->res.width
						 + info->pipeline.left_cropping + binary_dvs_env.width;
			isp_tmp_internal_height = in_info->res.height
						  + info->pipeline.top_cropping + binary_dvs_env.height;
		}
	} else if ((bds_out_info) && (out_info) &&
		   /* TODO: hack to make video_us case work. this should be reverted after
		   a nice solution in ISP */
		   (bds_out_info->res.width >= out_info->res.width)) {
		isp_tmp_internal_width = bds_out_info->padded_width;
		isp_tmp_internal_height = bds_out_info->res.height;
	} else {
		if (out_info) {
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

/* Computation results of the origin coordinate of bayer on the shading table. */
struct sh_css_shading_table_bayer_origin_compute_results {
	u32 bayer_scale_hor_ratio_in;	/* Horizontal ratio (in) of bayer scaling. */
	u32 bayer_scale_hor_ratio_out;	/* Horizontal ratio (out) of bayer scaling. */
	u32 bayer_scale_ver_ratio_in;	/* Vertical ratio (in) of bayer scaling. */
	u32 bayer_scale_ver_ratio_out;	/* Vertical ratio (out) of bayer scaling. */
	u32 sc_bayer_origin_x_bqs_on_shading_table; /* X coordinate (in bqs) of bayer origin on shading table. */
	u32 sc_bayer_origin_y_bqs_on_shading_table; /* Y coordinate (in bqs) of bayer origin on shading table. */
};

/* Get the requirements for the shading correction. */
static int
ia_css_binary_compute_shading_table_bayer_origin(
    const struct ia_css_binary *binary,				/* [in] */
    unsigned int required_bds_factor,				/* [in] */
    const struct ia_css_stream_config *stream_config,		/* [in] */
    struct sh_css_shading_table_bayer_origin_compute_results *res)	/* [out] */
{
	int err;

	/* Rational fraction of the fixed bayer downscaling factor. */
	struct u32_fract bds;

	/* Left padding set by InputFormatter. */
	unsigned int left_padding_bqs;			/* in bqs */

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

	/* Get the rational fraction of bayer downscaling factor. */
	err = sh_css_bds_factor_get_fract(required_bds_factor, &bds);
	if (err)
		return err;

	/* Set the left padding set by InputFormatter. (ifmtr.c) */
	if (stream_config->left_padding == -1)
		left_padding_bqs = _ISP_BQS(binary->left_padding);
	else
		left_padding_bqs = (unsigned int)((int)ISP_VEC_NELEMS
				   - _ISP_BQS(stream_config->left_padding));

	/* Set the left padding adjusted inside the isp.
	When bds_factor 2.00 is needed, some padding is added to left_padding
	inside the isp, before bayer downscaling. (raw.isp.c)
	(Hopefully, left_crop/left_padding/top_crop should be defined in css
	appropriately, depending on bds_factor.)
	*/
	need_bds_factor_2_00 = ((binary->info->sp.bds.supported_bds_factors &
				(PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_2_00) |
				 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_2_50) |
				 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_3_00) |
				 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_4_00) |
				 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_4_50) |
				 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_5_00) |
				 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_6_00) |
				 PACK_BDS_FACTOR(SH_CSS_BDS_FACTOR_8_00))) != 0);

	if (need_bds_factor_2_00 && binary->info->sp.pipeline.left_cropping > 0)
		left_padding_adjusted_bqs = left_padding_bqs + ISP_VEC_NELEMS;
	else
		left_padding_adjusted_bqs = left_padding_bqs;

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

	/* Calculate the origin of bayer (real sensor data area)
	located on the shading table during the shading correction. */
	res->sc_bayer_origin_x_bqs_on_shading_table =
		((left_padding_adjusted_bqs + bad_bqs_on_left_before_bs)
		* bds.denominator + bds.numerator / 2) / bds.numerator
		+ bad_bqs_on_left_after_bs;
	/* "+ bds.numerator / 2": rounding for division by bds.numerator */
	res->sc_bayer_origin_y_bqs_on_shading_table =
		(bad_bqs_on_top_before_bs * bds.denominator + bds.numerator / 2) / bds.numerator
		+ bad_bqs_on_top_after_bs;
	/* "+ bds.numerator / 2": rounding for division by bds.numerator */

	res->bayer_scale_hor_ratio_in  = bds.numerator;
	res->bayer_scale_hor_ratio_out = bds.denominator;
	res->bayer_scale_ver_ratio_in  = bds.numerator;
	res->bayer_scale_ver_ratio_out = bds.denominator;

	return err;
}

/* Get the shading information of Shading Correction Type 1. */
static int
binary_get_shading_info_type_1(const struct ia_css_binary *binary,	/* [in] */
			       unsigned int required_bds_factor,			/* [in] */
			       const struct ia_css_stream_config *stream_config,	/* [in] */
			       struct ia_css_shading_info *info)			/* [out] */
{
	int err;
	struct sh_css_shading_table_bayer_origin_compute_results res;

	assert(binary);
	assert(info);

	info->type = IA_CSS_SHADING_CORRECTION_TYPE_1;

	info->info.type_1.enable	    = binary->info->sp.enable.sc;
	info->info.type_1.num_hor_grids	    = binary->sctbl_width_per_color;
	info->info.type_1.num_ver_grids	    = binary->sctbl_height;
	info->info.type_1.bqs_per_grid_cell = (1 << binary->deci_factor_log2);

	/* Initialize by default values. */
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
	if (err)
		return err;

	info->info.type_1.bayer_scale_hor_ratio_in	= res.bayer_scale_hor_ratio_in;
	info->info.type_1.bayer_scale_hor_ratio_out	= res.bayer_scale_hor_ratio_out;
	info->info.type_1.bayer_scale_ver_ratio_in	= res.bayer_scale_ver_ratio_in;
	info->info.type_1.bayer_scale_ver_ratio_out	= res.bayer_scale_ver_ratio_out;
	info->info.type_1.sc_bayer_origin_x_bqs_on_shading_table = res.sc_bayer_origin_x_bqs_on_shading_table;
	info->info.type_1.sc_bayer_origin_y_bqs_on_shading_table = res.sc_bayer_origin_y_bqs_on_shading_table;

	return err;
}


int
ia_css_binary_get_shading_info(const struct ia_css_binary *binary,			/* [in] */
			       enum ia_css_shading_correction_type type,		/* [in] */
			       unsigned int required_bds_factor,			/* [in] */
			       const struct ia_css_stream_config *stream_config,	/* [in] */
			       struct ia_css_shading_info *shading_info,		/* [out] */
			       struct ia_css_pipe_config *pipe_config)			/* [out] */
{
	int err;

	assert(binary);
	assert(shading_info);

	IA_CSS_ENTER_PRIVATE("binary=%p, type=%d, required_bds_factor=%d, stream_config=%p",
			     binary, type, required_bds_factor, stream_config);

	if (type == IA_CSS_SHADING_CORRECTION_TYPE_1)
		err = binary_get_shading_info_type_1(binary,
						     required_bds_factor,
						     stream_config,
						     shading_info);
	else
		err = -ENOTSUPP;

	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static void sh_css_binary_common_grid_info(const struct ia_css_binary *binary,
	struct ia_css_grid_info *info)
{
	assert(binary);
	assert(info);

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
	assert(binary);
	assert(info);

	dvs_info = &info->dvs_grid.dvs_grid_info;

	/*
	 * For DIS, we use a division instead of a DIV_ROUND_UP(). If this is smaller
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

int
ia_css_binary_3a_grid_info(const struct ia_css_binary *binary,
			   struct ia_css_grid_info *info,
			   struct ia_css_pipe *pipe) {
	struct ia_css_3a_grid_info *s3a_info;
	int err = 0;

	IA_CSS_ENTER_PRIVATE("binary=%p, info=%p, pipe=%p",
			     binary, info, pipe);

	assert(binary);
	assert(info);
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
	s3a_info->has_histogram     = 0;
	IA_CSS_LEAVE_ERR_PRIVATE(err);
	return err;
}

static void
binary_init_pc_histogram(struct sh_css_pc_histogram *histo)
{
	assert(histo);

	histo->length = 0;
	histo->run = NULL;
	histo->stall = NULL;
}

static void
binary_init_metrics(struct sh_css_binary_metrics *metrics,
		    const struct ia_css_binary_info *info)
{
	assert(metrics);
	assert(info);

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

	assert(info);

	for (i = 0; i < info->num_output_formats; i++) {
		if (info->output_formats[i] == format)
			return true;
	}
	return false;
}

static bool
binary_supports_vf_format(const struct ia_css_binary_xinfo *info,
			  enum ia_css_frame_format format)
{
	int i;

	assert(info);

	for (i = 0; i < info->num_vf_formats; i++) {
		if (info->vf_formats[i] == format)
			return true;
	}
	return false;
}

/* move to host part of bds module */
static bool
supports_bds_factor(u32 supported_factors,
		    uint32_t bds_factor)
{
	return ((supported_factors & PACK_BDS_FACTOR(bds_factor)) != 0);
}

static int
binary_init_info(struct ia_css_binary_xinfo *info, unsigned int i,
		 bool *binary_found) {
	const unsigned char *blob = sh_css_blob_info[i].blob;
	unsigned int size = sh_css_blob_info[i].header.blob.size;

	if ((!info) || (!binary_found))
		return -EINVAL;

	*info = sh_css_blob_info[i].header.info.isp;
	*binary_found = blob;
	info->blob_index = i;
	/* we don't have this binary, skip it */
	if (!size)
		return 0;

	info->xmem_addr = sh_css_load_blob(blob, size);
	if (!info->xmem_addr)
		return -ENOMEM;
	return 0;
}

/* When binaries are put at the beginning, they will only
 * be selected if no other primary matches.
 */
int
ia_css_binary_init_infos(void) {
	unsigned int i;
	unsigned int num_of_isp_binaries = sh_css_num_binaries - NUM_OF_SPS - NUM_OF_BLS;

	if (num_of_isp_binaries == 0)
		return 0;

	all_binaries = kvmalloc(num_of_isp_binaries * sizeof(*all_binaries),
				GFP_KERNEL);
	if (!all_binaries)
		return -ENOMEM;

	for (i = 0; i < num_of_isp_binaries; i++)
	{
		int ret;
		struct ia_css_binary_xinfo *binary = &all_binaries[i];
		bool binary_found;

		ret = binary_init_info(binary, i, &binary_found);
		if (ret)
			return ret;
		if (!binary_found)
			continue;
		/* Prepend new binary information */
		binary->next = binary_infos[binary->sp.pipeline.mode];
		binary_infos[binary->sp.pipeline.mode] = binary;
		binary->blob = &sh_css_blob_info[i];
		binary->mem_offsets = sh_css_blob_info[i].mem_offsets;
	}
	return 0;
}

int
ia_css_binary_uninit(void) {
	unsigned int i;
	struct ia_css_binary_xinfo *b;

	for (i = 0; i < IA_CSS_BINARY_NUM_MODES; i++)
	{
		for (b = binary_infos[i]; b; b = b->next) {
			if (b->xmem_addr)
				hmm_free(b->xmem_addr);
			b->xmem_addr = mmgr_NULL;
		}
		binary_infos[i] = NULL;
	}
	kvfree(all_binaries);
	return 0;
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
	/* 3A/Shading decimation factor specification (at August 2008)
	 * ------------------------------------------------------------------
	 * [Image Width (BQ)] [Decimation Factor (BQ)] [Resulting grid cells]
	 * 1280 ?c             32                       40 ?c
	 *  640 ?c 1279        16                       40 ?c 80
	 *      ?c  639         8                          ?c 80
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
	assert(ISP_BQ_GRID_WIDTH(width,
				 MAX_SPEC_DECI_FACT_LOG2) <= SH_CSS_MAX_BQ_GRID_WIDTH);
	assert(ISP_BQ_GRID_HEIGHT(height,
				  MAX_SPEC_DECI_FACT_LOG2) <= SH_CSS_MAX_BQ_GRID_HEIGHT);

	/* Compute the smallest factor. */
	smallest_factor = MAX_SPEC_DECI_FACT_LOG2;
	while (ISP_BQ_GRID_WIDTH(width,
				 smallest_factor - 1) <= SH_CSS_MAX_BQ_GRID_WIDTH &&
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

	if (IS_ISP2401) {
		/* the output image line of Input System 2401 does not have the left paddings  */
		nr_of_left_paddings = 0;
	} else {
		/* in other cases, the left padding pixels are always 128 */
		nr_of_left_paddings = 2 * ISP_VEC_NELEMS;
	}

	if (need_scaling) {
		/* In SDV use-case, we need to match left-padding of
		 * primary and the video binary. */
		if (stream_config_left_padding != -1) {
			/* Different than before, we do left&right padding. */
			rval =
			    CEIL_MUL(in_frame_width + nr_of_left_paddings,
				     2 * ISP_VEC_NELEMS);
		} else {
			/* Different than before, we do left&right padding. */
			in_frame_width += dvs_env_width;
			rval =
			    CEIL_MUL(in_frame_width +
				     (left_cropping ? nr_of_left_paddings : 0),
				     2 * ISP_VEC_NELEMS);
		}
	} else {
		rval = isp_internal_width;
	}

	return rval;
}

int
ia_css_binary_fill_info(const struct ia_css_binary_xinfo *xinfo,
			bool online,
			bool two_ppc,
			enum atomisp_input_format stream_format,
			const struct ia_css_frame_info *in_info, /* can be NULL */
			const struct ia_css_frame_info *bds_out_info, /* can be NULL */
			const struct ia_css_frame_info *out_info[], /* can be NULL */
			const struct ia_css_frame_info *vf_info, /* can be NULL */
			struct ia_css_binary *binary,
			struct ia_css_resolution *dvs_env,
			int stream_config_left_padding,
			bool accelerator) {
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
	int err;
	unsigned int i;
	const struct ia_css_frame_info *bin_out_info = NULL;

	assert(info);
	assert(binary);

	binary->info = xinfo;
	if (!accelerator)
	{
		/* binary->css_params has been filled by accelerator itself. */
		err = ia_css_isp_param_allocate_isp_parameters(
		    &binary->mem_params, &binary->css_params,
		    &info->mem_initializers);
		if (err) {
			return err;
		}
	}
	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
	{
		if (out_info[i] && (out_info[i]->res.width != 0)) {
			bin_out_info = out_info[i];
			break;
		}
	}
	if (in_info && bin_out_info)
	{
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
	if (bin_out_info) /* { */
		binary->internal_frame_info.format = bin_out_info->format;
	/* } */
	binary->internal_frame_info.res.width       = isp_internal_width;
	binary->internal_frame_info.padded_width    = CEIL_MUL(isp_internal_width, 2 * ISP_VEC_NELEMS);
	binary->internal_frame_info.res.height      = isp_internal_height;
	binary->internal_frame_info.raw_bit_depth   = bits_per_pixel;

	if (in_info)
	{
		binary->effective_in_frame_res.width = in_info->res.width;
		binary->effective_in_frame_res.height = in_info->res.height;

		bits_per_pixel = in_info->raw_bit_depth;

		/* input info */
		binary->in_frame_info.res.width = in_info->res.width +
						  info->pipeline.left_cropping;
		binary->in_frame_info.res.height = in_info->res.height +
						   info->pipeline.top_cropping;

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

	if (online)
	{
		bits_per_pixel = ia_css_util_input_format_bpp(
				     stream_format, two_ppc);
	}
	binary->in_frame_info.raw_bit_depth = bits_per_pixel;

	for (i = 0; i < IA_CSS_BINARY_MAX_OUTPUT_PORTS; i++)
	{
		if (out_info[i]) {
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

	if (vf_info && (vf_info->res.width != 0))
	{
		err = ia_css_vf_configure(binary, bin_out_info,
					  (struct ia_css_frame_info *)vf_info, &vf_log_ds);
		if (err) {
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
	if ((vf_info) && (vf_info->res.width != 0))
	{
		unsigned int vf_out_vecs, vf_out_width, vf_out_height;

		binary->vf_frame_info.format = vf_info->format;
		if (!bin_out_info)
			return -EINVAL;
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
	} else
	{
		binary->vf_frame_info.res.width    = 0;
		binary->vf_frame_info.padded_width = 0;
		binary->vf_frame_info.res.height   = 0;
	}

	if (info->enable.ca_gdc)
	{
		binary->morph_tbl_width =
		    _ISP_MORPH_TABLE_WIDTH(isp_internal_width);
		binary->morph_tbl_aligned_width  =
		    _ISP_MORPH_TABLE_ALIGNED_WIDTH(isp_internal_width);
		binary->morph_tbl_height =
		    _ISP_MORPH_TABLE_HEIGHT(isp_internal_height);
	} else
	{
		binary->morph_tbl_width  = 0;
		binary->morph_tbl_aligned_width  = 0;
		binary->morph_tbl_height = 0;
	}

	sc_3a_dis_width = binary->in_frame_info.res.width;
	sc_3a_dis_padded_width = binary->in_frame_info.padded_width;
	sc_3a_dis_height = binary->in_frame_info.res.height;
	if (bds_out_info && in_info &&
	    bds_out_info->res.width != in_info->res.width)
	{
		/* TODO: Next, "internal_frame_info" should be derived from
		 * bds_out. So this part will change once it is in place! */
		sc_3a_dis_width = bds_out_info->res.width + info->pipeline.left_cropping;
		sc_3a_dis_padded_width = isp_internal_width;
		sc_3a_dis_height = isp_internal_height;
	}

	s3a_isp_width = _ISP_S3A_ELEMS_ISP_WIDTH(sc_3a_dis_padded_width,
			info->pipeline.left_cropping);
	if (info->s3a.fixed_s3a_deci_log)
	{
		s3a_log_deci = info->s3a.fixed_s3a_deci_log;
	} else
	{
		s3a_log_deci = binary_grid_deci_factor_log2(s3a_isp_width,
			       sc_3a_dis_height);
	}
	binary->deci_factor_log2  = s3a_log_deci;

	if (info->enable.s3a)
	{
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
	} else
	{
		binary->s3atbl_width  = 0;
		binary->s3atbl_height = 0;
		binary->s3atbl_isp_width  = 0;
		binary->s3atbl_isp_height = 0;
	}

	if (info->enable.sc)
	{
		binary->sctbl_width_per_color = _ISP_SCTBL_WIDTH_PER_COLOR(sc_3a_dis_padded_width, s3a_log_deci);
		binary->sctbl_aligned_width_per_color = SH_CSS_MAX_SCTBL_ALIGNED_WIDTH_PER_COLOR;
		binary->sctbl_height = _ISP_SCTBL_HEIGHT(sc_3a_dis_height, s3a_log_deci);
	} else
	{
		binary->sctbl_width_per_color         = 0;
		binary->sctbl_aligned_width_per_color = 0;
		binary->sctbl_height                  = 0;
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

	return 0;
}

int ia_css_binary_find(struct ia_css_binary_descr *descr, struct ia_css_binary *binary)
{
	int mode;
	bool online;
	bool two_ppc;
	enum atomisp_input_format stream_format;
	const struct ia_css_frame_info *req_in_info,
		*req_bds_out_info,
		*req_out_info[IA_CSS_BINARY_MAX_OUTPUT_PORTS],
		*req_bin_out_info = NULL,
		*req_vf_info;

	struct ia_css_binary_xinfo *xcandidate;
	bool need_ds, need_dz, need_dvs, need_xnr, need_dpc;
	bool striped;
	bool enable_yuv_ds;
	bool enable_high_speed;
	bool enable_dvs_6axis;
	bool enable_reduced_pipe;
	bool enable_capture_pp_bli;
	int err = -EINVAL;
	bool continuous;
	unsigned int isp_pipe_version;
	struct ia_css_resolution dvs_env, internal_res;
	unsigned int i;

	assert(descr);
	/* MW: used after an error check, may accept NULL, but doubtful */
	assert(binary);

	dev_dbg(atomisp_dev, "ia_css_binary_find() enter: descr=%p, (mode=%d), binary=%p\n",
		descr, descr->mode, binary);

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
	if (!req_bin_out_info)
		return -EINVAL;
	req_vf_info = descr->vf_info;

	need_xnr = descr->enable_xnr;
	need_ds = descr->enable_fractional_ds;
	need_dz = false;
	need_dvs = false;
	need_dpc = descr->enable_dpc;

	enable_yuv_ds = descr->enable_yuv_ds;
	enable_high_speed = descr->enable_high_speed;
	enable_dvs_6axis  = descr->enable_dvs_6axis;
	enable_reduced_pipe = descr->enable_reduced_pipe;
	enable_capture_pp_bli = descr->enable_capture_pp_bli;
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
	dev_dbg(atomisp_dev, "BINARY INFO:\n");
	for (i = 0; i < IA_CSS_BINARY_NUM_MODES; i++) {
		xcandidate = binary_infos[i];
		if (xcandidate) {
			dev_dbg(atomisp_dev, "%d:\n", i);
			while (xcandidate) {
				dev_dbg(atomisp_dev, " Name:%s Type:%d Cont:%d\n",
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
		dev_dbg(atomisp_dev,
			"ia_css_binary_find() candidate = %p, mode = %d ID = %d\n",
			candidate, candidate->pipeline.mode, candidate->id);

		/*
		 * MW: Only a limited set of jointly configured binaries can
		 * be used in a continuous preview/video mode unless it is
		 * the copy mode and runs on SP.
		*/
		if (!candidate->enable.continuous &&
		    continuous && (mode != IA_CSS_BINARY_MODE_COPY)) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: !%d && %d && (%d != %d)\n",
				__LINE__, candidate->enable.continuous,
				continuous, mode, IA_CSS_BINARY_MODE_COPY);
			continue;
		}
		if (striped && candidate->iterator.num_stripes == 1) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: binary is not striped\n",
				__LINE__);
			continue;
		}

		if (candidate->pipeline.isp_pipe_version != isp_pipe_version &&
		    (mode != IA_CSS_BINARY_MODE_COPY) &&
		    (mode != IA_CSS_BINARY_MODE_CAPTURE_PP) &&
		    (mode != IA_CSS_BINARY_MODE_VF_PP)) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: (%d != %d)\n",
				__LINE__, candidate->pipeline.isp_pipe_version, isp_pipe_version);
			continue;
		}
		if (!candidate->enable.reduced_pipe && enable_reduced_pipe) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__, candidate->enable.reduced_pipe, enable_reduced_pipe);
			continue;
		}
		if (!candidate->enable.dvs_6axis && enable_dvs_6axis) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__, candidate->enable.dvs_6axis, enable_dvs_6axis);
			continue;
		}
		if (candidate->enable.high_speed && !enable_high_speed) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: %d && !%d\n",
				__LINE__, candidate->enable.high_speed, enable_high_speed);
			continue;
		}
		if (!candidate->enable.xnr && need_xnr) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: %d && !%d\n",
				__LINE__, candidate->enable.xnr, need_xnr);
			continue;
		}
		if (!(candidate->enable.ds & 2) && enable_yuv_ds) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__, ((candidate->enable.ds & 2) != 0), enable_yuv_ds);
			continue;
		}
		if ((candidate->enable.ds & 2) && !enable_yuv_ds) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: %d && !%d\n",
				__LINE__, ((candidate->enable.ds & 2) != 0), enable_yuv_ds);
			continue;
		}

		if (mode == IA_CSS_BINARY_MODE_VIDEO &&
		    candidate->enable.ds && need_ds)
			need_dz = false;

		/* when we require vf output, we need to have vf_veceven */
		if ((req_vf_info) && !(candidate->enable.vf_veceven ||
				       /* or variable vf vec even */
				       candidate->vf_dec.is_variable ||
				       /* or more than one output pin. */
				       xcandidate->num_output_pins > 1)) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: (%p != NULL) && !(%d || %d || (%d >%d))\n",
				__LINE__, req_vf_info, candidate->enable.vf_veceven,
				candidate->vf_dec.is_variable, xcandidate->num_output_pins, 1);
			continue;
		}
		if (!candidate->enable.dvs_envelope && need_dvs) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__, candidate->enable.dvs_envelope, (int)need_dvs);
			continue;
		}
		/* internal_res check considers input, output, and dvs envelope sizes */
		ia_css_binary_internal_res(req_in_info, req_bds_out_info,
					   req_bin_out_info, &dvs_env, candidate, &internal_res);
		if (internal_res.width > candidate->internal.max_width) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: (%d > %d)\n",
				__LINE__, internal_res.width, candidate->internal.max_width);
			continue;
		}
		if (internal_res.height > candidate->internal.max_height) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: (%d > %d)\n",
				__LINE__, internal_res.height, candidate->internal.max_height);
			continue;
		}
		if (!candidate->enable.ds && need_ds && !(xcandidate->num_output_pins > 1)) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: !%d && %d\n",
				__LINE__, candidate->enable.ds, (int)need_ds);
			continue;
		}
		if (!candidate->enable.uds && !candidate->enable.dvs_6axis && need_dz) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: !%d && !%d && %d\n",
				__LINE__, candidate->enable.uds, candidate->enable.dvs_6axis,
				(int)need_dz);
			continue;
		}
		if (online && candidate->input.source == IA_CSS_BINARY_INPUT_MEMORY) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: %d && (%d == %d)\n",
				__LINE__, online, candidate->input.source,
				IA_CSS_BINARY_INPUT_MEMORY);
			continue;
		}
		if (!online && candidate->input.source == IA_CSS_BINARY_INPUT_SENSOR) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: !%d && (%d == %d)\n",
				__LINE__, online, candidate->input.source,
				IA_CSS_BINARY_INPUT_SENSOR);
			continue;
		}
		if (req_bin_out_info->res.width < candidate->output.min_width ||
		    req_bin_out_info->res.width > candidate->output.max_width) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: (%d > %d) || (%d < %d)\n",
				__LINE__, req_bin_out_info->padded_width,
				candidate->output.min_width, req_bin_out_info->padded_width,
				candidate->output.max_width);
			continue;
		}
		if (xcandidate->num_output_pins > 1 &&
		    /* in case we have a second output pin, */
		    req_vf_info) { /* and we need vf output. */
			if (req_vf_info->res.width > candidate->output.max_width) {
				dev_dbg(atomisp_dev,
					"ia_css_binary_find() [%d] continue: (%d < %d)\n",
					__LINE__, req_vf_info->res.width,
					candidate->output.max_width);
				continue;
			}
		}
		if (req_in_info->padded_width > candidate->input.max_width) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: (%d > %d)\n",
				__LINE__, req_in_info->padded_width, candidate->input.max_width);
			continue;
		}
		if (!binary_supports_output_format(xcandidate, req_bin_out_info->format)) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: !%d\n",
				__LINE__, binary_supports_output_format(xcandidate,
									req_bin_out_info->format));
			continue;
		}
		if (xcandidate->num_output_pins > 1 &&
		    /* in case we have a second output pin, */
		    req_vf_info                   && /* and we need vf output. */
		    /* check if the required vf format
		    is supported. */
		    !binary_supports_output_format(xcandidate, req_vf_info->format)) {
			dev_dbg(atomisp_dev,
				"ia_css_binary_find() [%d] continue: (%d > %d) && (%p != NULL) && !%d\n",
				__LINE__, xcandidate->num_output_pins, 1, req_vf_info,
				binary_supports_output_format(xcandidate, req_vf_info->format));
			continue;
		}

		/* Check if vf_veceven supports the requested vf format */
		if (xcandidate->num_output_pins == 1 &&
		    req_vf_info && candidate->enable.vf_veceven &&
		    !binary_supports_vf_format(xcandidate, req_vf_info->format)) {
			dev_dbg(atomisp_dev,
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
				dev_dbg(atomisp_dev,
					"ia_css_binary_find() [%d] continue: (%d < %d)\n",
					__LINE__, req_vf_info->res.width,
					candidate->output.max_width);
				continue;
			}
		}

		if (!supports_bds_factor(candidate->bds.supported_bds_factors,
					 descr->required_bds_factor)) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: 0x%x & 0x%x)\n",
				__LINE__, candidate->bds.supported_bds_factors,
				descr->required_bds_factor);
			continue;
		}

		if (!candidate->enable.dpc && need_dpc) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: 0x%x & 0x%x)\n",
				__LINE__, candidate->enable.dpc, descr->enable_dpc);
			continue;
		}

		if (candidate->uds.use_bci && enable_capture_pp_bli) {
			dev_dbg(atomisp_dev, "ia_css_binary_find() [%d] continue: 0x%x & 0x%x)\n",
				__LINE__, candidate->uds.use_bci, descr->enable_capture_pp_bli);
			continue;
		}

		/* reconfigure any variable properties of the binary */
		err = ia_css_binary_fill_info(xcandidate, online, two_ppc,
					      stream_format, req_in_info,
					      req_bds_out_info,
					      req_out_info, req_vf_info,
					      binary, &dvs_env,
					      descr->stream_config_left_padding,
					      false);

		if (err)
			break;
		binary_init_metrics(&binary->metrics, &binary->info->sp);
		break;
	}

	if (!err && xcandidate)
		dev_dbg(atomisp_dev, "Using binary %s (id %d), type %d, mode %d, continuous %s\n",
			xcandidate->blob->name, xcandidate->sp.id, xcandidate->type,
			xcandidate->sp.pipeline.mode,
			xcandidate->sp.enable.continuous ? "true" : "false");

	if (err)
		dev_err(atomisp_dev, "Failed to find a firmware binary matching the pipeline parameters\n");

	return err;
}

unsigned
ia_css_binary_max_vf_width(void)
{
	/* This is (should be) true for IPU1 and IPU2 */
	/* For IPU3 (SkyCam) this pointer is guaranteed to be NULL simply because such a binary does not exist  */
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
	assert(binaries);

	if (num_isp_binaries)
		*num_isp_binaries = 0;

	*binaries = all_binaries;
	if (all_binaries && num_isp_binaries) {
		/* -1 to account for sp binary which is not stored in all_binaries */
		if (sh_css_num_binaries > 0)
			*num_isp_binaries = sh_css_num_binaries - 1;
	}
}
