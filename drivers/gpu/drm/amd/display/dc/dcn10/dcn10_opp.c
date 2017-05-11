/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "dcn10_opp.h"
#include "reg_helper.h"

#define REG(reg) \
	(oppn10->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	oppn10->opp_shift->field_name, oppn10->opp_mask->field_name

#define CTX \
	oppn10->base.ctx

static void opp_set_regamma_mode(
	struct output_pixel_processor *opp,
	enum opp_regamma mode)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);
	uint32_t re_mode = 0;
	uint32_t obuf_bypass = 0; /* need for pipe split */
	uint32_t obuf_hupscale = 0;

	switch (mode) {
	case OPP_REGAMMA_BYPASS:
		re_mode = 0;
		break;
	case OPP_REGAMMA_SRGB:
		re_mode = 1;
		break;
	case OPP_REGAMMA_3_6:
		re_mode = 2;
		break;
	case OPP_REGAMMA_USER:
		re_mode = oppn10->is_write_to_ram_a_safe ? 3 : 4;
		oppn10->is_write_to_ram_a_safe = !oppn10->is_write_to_ram_a_safe;
		break;
	default:
		break;
	}

	REG_SET(CM_RGAM_CONTROL, 0, CM_RGAM_LUT_MODE, re_mode);
	REG_UPDATE_2(OBUF_CONTROL,
			OBUF_BYPASS, obuf_bypass,
			OBUF_H_2X_UPSCALE_EN, obuf_hupscale);
}

/************* FORMATTER ************/

/**
 *	set_truncation
 *	1) set truncation depth: 0 for 18 bpp or 1 for 24 bpp
 *	2) enable truncation
 *	3) HW remove 12bit FMT support for DCE11 power saving reason.
 */
static void set_truncation(
		struct dcn10_opp *oppn10,
		const struct bit_depth_reduction_params *params)
{
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_TRUNCATE_EN, params->flags.TRUNCATE_ENABLED,
		FMT_TRUNCATE_DEPTH, params->flags.TRUNCATE_DEPTH,
		FMT_TRUNCATE_MODE, params->flags.TRUNCATE_MODE);
}

static void set_spatial_dither(
	struct dcn10_opp *oppn10,
	const struct bit_depth_reduction_params *params)
{
	/*Disable spatial (random) dithering*/
	REG_UPDATE_7(FMT_BIT_DEPTH_CONTROL,
			FMT_SPATIAL_DITHER_EN, 0,
			FMT_SPATIAL_DITHER_MODE, 0,
			FMT_SPATIAL_DITHER_DEPTH, 0,
			FMT_TEMPORAL_DITHER_EN, 0,
			FMT_HIGHPASS_RANDOM_ENABLE, 0,
			FMT_FRAME_RANDOM_ENABLE, 0,
			FMT_RGB_RANDOM_ENABLE, 0);


	/* only use FRAME_COUNTER_MAX if frameRandom == 1*/
	if (params->flags.FRAME_RANDOM == 1) {
		if (params->flags.SPATIAL_DITHER_DEPTH == 0 || params->flags.SPATIAL_DITHER_DEPTH == 1) {
			REG_UPDATE_2(FMT_CONTROL,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, 15,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, 2);
		} else if (params->flags.SPATIAL_DITHER_DEPTH == 2) {
			REG_UPDATE_2(FMT_CONTROL,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, 3,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, 1);
		} else {
			return;
		}
	} else {
		REG_UPDATE_2(FMT_CONTROL,
				FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, 0,
				FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, 0);
	}

	/*Set seed for random values for
	 * spatial dithering for R,G,B channels*/

	REG_SET(FMT_DITHER_RAND_R_SEED, 0,
			FMT_RAND_R_SEED, params->r_seed_value);

	REG_SET(FMT_DITHER_RAND_G_SEED, 0,
			FMT_RAND_G_SEED, params->g_seed_value);

	REG_SET(FMT_DITHER_RAND_B_SEED, 0,
			FMT_RAND_B_SEED, params->b_seed_value);

	/* FMT_OFFSET_R_Cr  31:16 0x0 Setting the zero
	 * offset for the R/Cr channel, lower 4LSB
	 * is forced to zeros. Typically set to 0
	 * RGB and 0x80000 YCbCr.
	 */
	/* FMT_OFFSET_G_Y   31:16 0x0 Setting the zero
	 * offset for the G/Y  channel, lower 4LSB is
	 * forced to zeros. Typically set to 0 RGB
	 * and 0x80000 YCbCr.
	 */
	/* FMT_OFFSET_B_Cb  31:16 0x0 Setting the zero
	 * offset for the B/Cb channel, lower 4LSB is
	 * forced to zeros. Typically set to 0 RGB and
	 * 0x80000 YCbCr.
	 */

	REG_UPDATE_6(FMT_BIT_DEPTH_CONTROL,
			/*Enable spatial dithering*/
			FMT_SPATIAL_DITHER_EN, params->flags.SPATIAL_DITHER_ENABLED,
			/* Set spatial dithering mode
			 * (default is Seed patterrn AAAA...)
			 */
			FMT_SPATIAL_DITHER_MODE, params->flags.SPATIAL_DITHER_MODE,
			/*Set spatial dithering bit depth*/
			FMT_SPATIAL_DITHER_DEPTH, params->flags.SPATIAL_DITHER_DEPTH,
			/*Disable High pass filter*/
			FMT_HIGHPASS_RANDOM_ENABLE, params->flags.HIGHPASS_RANDOM,
			/*Reset only at startup*/
			FMT_FRAME_RANDOM_ENABLE, params->flags.FRAME_RANDOM,
			/*Set RGB data dithered with x^28+x^3+1*/
			FMT_RGB_RANDOM_ENABLE, params->flags.RGB_RANDOM);
}

static void opp_program_bit_depth_reduction(
	struct output_pixel_processor *opp,
	const struct bit_depth_reduction_params *params)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	set_truncation(oppn10, params);
	set_spatial_dither(oppn10, params);
	/* TODO
	 * set_temporal_dither(oppn10, params);
	 */
}

/**
 *	set_pixel_encoding
 *
 *	Set Pixel Encoding
 *		0: RGB 4:4:4 or YCbCr 4:4:4 or YOnly
 *		1: YCbCr 4:2:2
 */
static void set_pixel_encoding(
	struct dcn10_opp *oppn10,
	const struct clamping_and_pixel_encoding_params *params)
{
	switch (params->pixel_encoding)	{

	case PIXEL_ENCODING_RGB:
	case PIXEL_ENCODING_YCBCR444:
		REG_UPDATE(FMT_CONTROL, FMT_PIXEL_ENCODING, 0);
		break;
	case PIXEL_ENCODING_YCBCR422:
		REG_UPDATE(FMT_CONTROL, FMT_PIXEL_ENCODING, 1);
		break;
	case PIXEL_ENCODING_YCBCR420:
		REG_UPDATE(FMT_CONTROL, FMT_PIXEL_ENCODING, 2);
		break;
	default:
		break;
	}
}

/**
 *	Set Clamping
 *	1) Set clamping format based on bpc - 0 for 6bpc (No clamping)
 *		1 for 8 bpc
 *		2 for 10 bpc
 *		3 for 12 bpc
 *		7 for programable
 *	2) Enable clamp if Limited range requested
 */
static void opp_set_clamping(
	struct dcn10_opp *oppn10,
	const struct clamping_and_pixel_encoding_params *params)
{
	REG_UPDATE_2(FMT_CLAMP_CNTL,
			FMT_CLAMP_DATA_EN, 0,
			FMT_CLAMP_COLOR_FORMAT, 0);

	switch (params->clamping_level) {
	case CLAMPING_FULL_RANGE:
		REG_UPDATE_2(FMT_CLAMP_CNTL,
				FMT_CLAMP_DATA_EN, 1,
				FMT_CLAMP_COLOR_FORMAT, 0);
		break;
	case CLAMPING_LIMITED_RANGE_8BPC:
		REG_UPDATE_2(FMT_CLAMP_CNTL,
				FMT_CLAMP_DATA_EN, 1,
				FMT_CLAMP_COLOR_FORMAT, 1);
		break;
	case CLAMPING_LIMITED_RANGE_10BPC:
		REG_UPDATE_2(FMT_CLAMP_CNTL,
				FMT_CLAMP_DATA_EN, 1,
				FMT_CLAMP_COLOR_FORMAT, 2);

		break;
	case CLAMPING_LIMITED_RANGE_12BPC:
		REG_UPDATE_2(FMT_CLAMP_CNTL,
				FMT_CLAMP_DATA_EN, 1,
				FMT_CLAMP_COLOR_FORMAT, 3);
		break;
	case CLAMPING_LIMITED_RANGE_PROGRAMMABLE:
		/* TODO */
	default:
		break;
	}

}

static void opp_set_dyn_expansion(
	struct output_pixel_processor *opp,
	enum dc_color_space color_sp,
	enum dc_color_depth color_dpth,
	enum signal_type signal)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	REG_UPDATE_2(FMT_DYNAMIC_EXP_CNTL,
			FMT_DYNAMIC_EXP_EN, 0,
			FMT_DYNAMIC_EXP_MODE, 0);

	/*00 - 10-bit -> 12-bit dynamic expansion*/
	/*01 - 8-bit  -> 12-bit dynamic expansion*/
	if (signal == SIGNAL_TYPE_HDMI_TYPE_A ||
		signal == SIGNAL_TYPE_DISPLAY_PORT ||
		signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		switch (color_dpth) {
		case COLOR_DEPTH_888:
			REG_UPDATE_2(FMT_DYNAMIC_EXP_CNTL,
				FMT_DYNAMIC_EXP_EN, 1,
				FMT_DYNAMIC_EXP_MODE, 1);
			break;
		case COLOR_DEPTH_101010:
			REG_UPDATE_2(FMT_DYNAMIC_EXP_CNTL,
				FMT_DYNAMIC_EXP_EN, 1,
				FMT_DYNAMIC_EXP_MODE, 0);
			break;
		case COLOR_DEPTH_121212:
			REG_UPDATE_2(FMT_DYNAMIC_EXP_CNTL,
				FMT_DYNAMIC_EXP_EN, 1,/*otherwise last two bits are zero*/
				FMT_DYNAMIC_EXP_MODE, 0);
			break;
		default:
			break;
		}
	}
}

static void opp_program_clamping_and_pixel_encoding(
	struct output_pixel_processor *opp,
	const struct clamping_and_pixel_encoding_params *params)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	opp_set_clamping(oppn10, params);
	set_pixel_encoding(oppn10, params);
}

static void opp_program_fmt(
	struct output_pixel_processor *opp,
	struct bit_depth_reduction_params *fmt_bit_depth,
	struct clamping_and_pixel_encoding_params *clamping)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	if (clamping->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		REG_UPDATE(FMT_MAP420_MEMORY_CONTROL, FMT_MAP420MEM_PWR_FORCE, 0);

	/* dithering is affected by <CrtcSourceSelect>, hence should be
	 * programmed afterwards */
	opp_program_bit_depth_reduction(
		opp,
		fmt_bit_depth);

	opp_program_clamping_and_pixel_encoding(
		opp,
		clamping);

	return;
}

static void opp_set_output_csc_default(
		struct output_pixel_processor *opp,
		const struct default_adjustment *default_adjust)
{

	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);
	uint32_t ocsc_mode = 0;

	if (default_adjust != NULL) {
		switch (default_adjust->out_color_space) {
		case COLOR_SPACE_SRGB:
		case COLOR_SPACE_2020_RGB_FULLRANGE:
			ocsc_mode = 0;
			break;
		case COLOR_SPACE_SRGB_LIMITED:
		case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
			ocsc_mode = 1;
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YCBCR601_LIMITED:
			ocsc_mode = 2;
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YCBCR709_LIMITED:
		case COLOR_SPACE_2020_YCBCR:
			ocsc_mode = 3;
			break;
		case COLOR_SPACE_UNKNOWN:
		default:
			break;
		}
	}

	REG_SET(CM_OCSC_CONTROL, 0, CM_OCSC_MODE, ocsc_mode);

}
/*program re gamma RAM B*/
static void opp_program_regamma_lutb_settings(
		struct output_pixel_processor *opp,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	REG_SET_2(CM_RGAM_RAMB_START_CNTL_B, 0,
		CM_RGAM_RAMB_EXP_REGION_START_B, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_RGAM_RAMB_START_CNTL_G, 0,
		CM_RGAM_RAMB_EXP_REGION_START_G, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_RGAM_RAMB_START_CNTL_R, 0,
		CM_RGAM_RAMB_EXP_REGION_START_R, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMB_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET(CM_RGAM_RAMB_SLOPE_CNTL_B, 0,
		CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_B, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMB_SLOPE_CNTL_G, 0,
		CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_G, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMB_SLOPE_CNTL_R, 0,
		CM_RGAM_RAMB_EXP_REGION_LINEAR_SLOPE_R, params->arr_points[0].custom_float_slope);

	REG_SET(CM_RGAM_RAMB_END_CNTL1_B, 0,
		CM_RGAM_RAMB_EXP_REGION_END_B, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMB_END_CNTL2_B, 0,
		CM_RGAM_RAMB_EXP_REGION_END_SLOPE_B, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMB_EXP_REGION_END_BASE_B, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMB_END_CNTL1_G, 0,
		CM_RGAM_RAMB_EXP_REGION_END_G, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMB_END_CNTL2_G, 0,
		CM_RGAM_RAMB_EXP_REGION_END_SLOPE_G, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMB_EXP_REGION_END_BASE_G, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMB_END_CNTL1_R, 0,
		CM_RGAM_RAMB_EXP_REGION_END_R, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMB_END_CNTL2_R, 0,
		CM_RGAM_RAMB_EXP_REGION_END_SLOPE_R, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMB_EXP_REGION_END_BASE_R, params->arr_points[1].custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_RGAM_RAMB_REGION_0_1, 0,
		CM_RGAM_RAMB_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_2_3, 0,
		CM_RGAM_RAMB_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_4_5, 0,
		CM_RGAM_RAMB_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_6_7, 0,
		CM_RGAM_RAMB_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_8_9, 0,
		CM_RGAM_RAMB_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_10_11, 0,
		CM_RGAM_RAMB_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_12_13, 0,
		CM_RGAM_RAMB_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_14_15, 0,
		CM_RGAM_RAMB_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_16_17, 0,
		CM_RGAM_RAMB_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_18_19, 0,
		CM_RGAM_RAMB_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_20_21, 0,
		CM_RGAM_RAMB_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_22_23, 0,
		CM_RGAM_RAMB_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_24_25, 0,
		CM_RGAM_RAMB_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_26_27, 0,
		CM_RGAM_RAMB_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_28_29, 0,
		CM_RGAM_RAMB_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_30_31, 0,
		CM_RGAM_RAMB_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMB_REGION_32_33, 0,
		CM_RGAM_RAMB_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMB_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMB_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMB_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);

}

/*program re gamma RAM A*/
static void opp_program_regamma_luta_settings(
		struct output_pixel_processor *opp,
		const struct pwl_params *params)
{
	const struct gamma_curve *curve;
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	REG_SET_2(CM_RGAM_RAMA_START_CNTL_B, 0,
		CM_RGAM_RAMA_EXP_REGION_START_B, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMA_EXP_REGION_START_SEGMENT_B, 0);
	REG_SET_2(CM_RGAM_RAMA_START_CNTL_G, 0,
		CM_RGAM_RAMA_EXP_REGION_START_G, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMA_EXP_REGION_START_SEGMENT_G, 0);
	REG_SET_2(CM_RGAM_RAMA_START_CNTL_R, 0,
		CM_RGAM_RAMA_EXP_REGION_START_R, params->arr_points[0].custom_float_x,
		CM_RGAM_RAMA_EXP_REGION_START_SEGMENT_R, 0);

	REG_SET(CM_RGAM_RAMA_SLOPE_CNTL_B, 0,
		CM_RGAM_RAMA_EXP_REGION_LINEAR_SLOPE_B, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMA_SLOPE_CNTL_G, 0,
		CM_RGAM_RAMA_EXP_REGION_LINEAR_SLOPE_G, params->arr_points[0].custom_float_slope);
	REG_SET(CM_RGAM_RAMA_SLOPE_CNTL_R, 0,
		CM_RGAM_RAMA_EXP_REGION_LINEAR_SLOPE_R, params->arr_points[0].custom_float_slope);

	REG_SET(CM_RGAM_RAMA_END_CNTL1_B, 0,
		CM_RGAM_RAMA_EXP_REGION_END_B, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMA_END_CNTL2_B, 0,
		CM_RGAM_RAMA_EXP_REGION_END_SLOPE_B, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMA_EXP_REGION_END_BASE_B, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMA_END_CNTL1_G, 0,
		CM_RGAM_RAMA_EXP_REGION_END_G, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMA_END_CNTL2_G, 0,
		CM_RGAM_RAMA_EXP_REGION_END_SLOPE_G, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMA_EXP_REGION_END_BASE_G, params->arr_points[1].custom_float_y);

	REG_SET(CM_RGAM_RAMA_END_CNTL1_R, 0,
		CM_RGAM_RAMA_EXP_REGION_END_R, params->arr_points[1].custom_float_x);
	REG_SET_2(CM_RGAM_RAMA_END_CNTL2_R, 0,
		CM_RGAM_RAMA_EXP_REGION_END_SLOPE_R, params->arr_points[1].custom_float_slope,
		CM_RGAM_RAMA_EXP_REGION_END_BASE_R, params->arr_points[1].custom_float_y);

	curve = params->arr_curve_points;
	REG_SET_4(CM_RGAM_RAMA_REGION_0_1, 0,
		CM_RGAM_RAMA_EXP_REGION0_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION0_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION1_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION1_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_2_3, 0,
		CM_RGAM_RAMA_EXP_REGION2_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION2_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION3_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION3_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_4_5, 0,
		CM_RGAM_RAMA_EXP_REGION4_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION4_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION5_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION5_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_6_7, 0,
		CM_RGAM_RAMA_EXP_REGION6_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION6_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION7_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION7_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_8_9, 0,
		CM_RGAM_RAMA_EXP_REGION8_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION8_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION9_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION9_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_10_11, 0,
		CM_RGAM_RAMA_EXP_REGION10_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION10_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION11_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION11_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_12_13, 0,
		CM_RGAM_RAMA_EXP_REGION12_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION12_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION13_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION13_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_14_15, 0,
		CM_RGAM_RAMA_EXP_REGION14_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION14_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION15_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION15_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_16_17, 0,
		CM_RGAM_RAMA_EXP_REGION16_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION16_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION17_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION17_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_18_19, 0,
		CM_RGAM_RAMA_EXP_REGION18_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION18_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION19_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION19_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_20_21, 0,
		CM_RGAM_RAMA_EXP_REGION20_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION20_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION21_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION21_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_22_23, 0,
		CM_RGAM_RAMA_EXP_REGION22_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION22_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION23_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION23_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_24_25, 0,
		CM_RGAM_RAMA_EXP_REGION24_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION24_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION25_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION25_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_26_27, 0,
		CM_RGAM_RAMA_EXP_REGION26_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION26_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION27_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION27_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_28_29, 0,
		CM_RGAM_RAMA_EXP_REGION28_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION28_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION29_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION29_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_30_31, 0,
		CM_RGAM_RAMA_EXP_REGION30_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION30_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION31_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION31_NUM_SEGMENTS, curve[1].segments_num);

	curve += 2;
	REG_SET_4(CM_RGAM_RAMA_REGION_32_33, 0,
		CM_RGAM_RAMA_EXP_REGION32_LUT_OFFSET, curve[0].offset,
		CM_RGAM_RAMA_EXP_REGION32_NUM_SEGMENTS, curve[0].segments_num,
		CM_RGAM_RAMA_EXP_REGION33_LUT_OFFSET, curve[1].offset,
		CM_RGAM_RAMA_EXP_REGION33_NUM_SEGMENTS, curve[1].segments_num);
}

static void opp_configure_regamma_lut(
		struct output_pixel_processor *opp,
		bool is_ram_a)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	REG_UPDATE(CM_RGAM_LUT_WRITE_EN_MASK,
			CM_RGAM_LUT_WRITE_EN_MASK, 7);
	REG_UPDATE(CM_RGAM_LUT_WRITE_EN_MASK,
			CM_RGAM_LUT_WRITE_SEL, is_ram_a == true ? 0:1);
	REG_SET(CM_RGAM_LUT_INDEX, 0, CM_RGAM_LUT_INDEX, 0);
}

static void opp_power_on_regamma_lut(
	struct output_pixel_processor *opp,
	bool power_on)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);
	REG_SET(CM_MEM_PWR_CTRL, 0,
			RGAM_MEM_PWR_FORCE, power_on == true ? 0:1);

}

static void opp_program_regamma_lut(
		struct output_pixel_processor *opp,
		const struct pwl_result_data *rgb,
		uint32_t num)
{
	uint32_t i;
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);
	for (i = 0 ; i < num; i++) {
		REG_SET(CM_RGAM_LUT_DATA, 0, CM_RGAM_LUT_DATA, rgb[i].red_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0, CM_RGAM_LUT_DATA, rgb[i].green_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0, CM_RGAM_LUT_DATA, rgb[i].blue_reg);

		REG_SET(CM_RGAM_LUT_DATA, 0,
				CM_RGAM_LUT_DATA, rgb[i].delta_red_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0,
				CM_RGAM_LUT_DATA, rgb[i].delta_green_reg);
		REG_SET(CM_RGAM_LUT_DATA, 0,
				CM_RGAM_LUT_DATA, rgb[i].delta_blue_reg);

	}

}

static bool opp_set_regamma_pwl(
	struct output_pixel_processor *opp, const struct pwl_params *params)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	opp_power_on_regamma_lut(opp, true);
	opp_configure_regamma_lut(opp, oppn10->is_write_to_ram_a_safe);

	if (oppn10->is_write_to_ram_a_safe)
		 opp_program_regamma_luta_settings(opp, params);
	else
		 opp_program_regamma_lutb_settings(opp, params);

	opp_program_regamma_lut(
		opp, params->rgb_resulted, params->hw_points_num);

	return true;
}

static void opp_set_stereo_polarity(
		struct output_pixel_processor *opp,
		bool enable, bool rightEyePolarity)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	REG_UPDATE(FMT_CONTROL, FMT_STEREOSYNC_OVERRIDE, enable);
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

static void dcn10_opp_destroy(struct output_pixel_processor **opp)
{
	dm_free(TO_DCN10_OPP(*opp));
	*opp = NULL;
}

static struct opp_funcs dcn10_opp_funcs = {
		.opp_power_on_regamma_lut = opp_power_on_regamma_lut,
		.opp_set_csc_adjustment = NULL,
		.opp_set_csc_default = opp_set_output_csc_default,
		.opp_set_dyn_expansion = opp_set_dyn_expansion,
		.opp_program_regamma_pwl = opp_set_regamma_pwl,
		.opp_set_regamma_mode = opp_set_regamma_mode,
		.opp_program_fmt = opp_program_fmt,
		.opp_program_bit_depth_reduction = opp_program_bit_depth_reduction,
		.opp_set_stereo_polarity = opp_set_stereo_polarity,
		.opp_destroy = dcn10_opp_destroy
};

void dcn10_opp_construct(struct dcn10_opp *oppn10,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn10_opp_registers *regs,
	const struct dcn10_opp_shift *opp_shift,
	const struct dcn10_opp_mask *opp_mask)
{
	oppn10->base.ctx = ctx;
	oppn10->base.inst = inst;
	oppn10->base.funcs = &dcn10_opp_funcs;

	oppn10->regs = regs;
	oppn10->opp_shift = opp_shift;
	oppn10->opp_mask = opp_mask;
}
