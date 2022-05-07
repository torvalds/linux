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

#include <linux/slab.h>

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


/************* FORMATTER ************/

/**
 *	set_truncation
 *	1) set truncation depth: 0 for 18 bpp or 1 for 24 bpp
 *	2) enable truncation
 *	3) HW remove 12bit FMT support for DCE11 power saving reason.
 */
static void opp1_set_truncation(
		struct dcn10_opp *oppn10,
		const struct bit_depth_reduction_params *params)
{
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_TRUNCATE_EN, params->flags.TRUNCATE_ENABLED,
		FMT_TRUNCATE_DEPTH, params->flags.TRUNCATE_DEPTH,
		FMT_TRUNCATE_MODE, params->flags.TRUNCATE_MODE);
}

static void opp1_set_spatial_dither(
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

void opp1_program_bit_depth_reduction(
	struct output_pixel_processor *opp,
	const struct bit_depth_reduction_params *params)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	opp1_set_truncation(oppn10, params);
	opp1_set_spatial_dither(oppn10, params);
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
static void opp1_set_pixel_encoding(
	struct dcn10_opp *oppn10,
	const struct clamping_and_pixel_encoding_params *params)
{
	switch (params->pixel_encoding)	{

	case PIXEL_ENCODING_RGB:
	case PIXEL_ENCODING_YCBCR444:
		REG_UPDATE(FMT_CONTROL, FMT_PIXEL_ENCODING, 0);
		break;
	case PIXEL_ENCODING_YCBCR422:
		REG_UPDATE_3(FMT_CONTROL,
				FMT_PIXEL_ENCODING, 1,
				FMT_SUBSAMPLING_MODE, 2,
				FMT_CBCR_BIT_REDUCTION_BYPASS, 0);
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
static void opp1_set_clamping(
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

void opp1_set_dyn_expansion(
	struct output_pixel_processor *opp,
	enum dc_color_space color_sp,
	enum dc_color_depth color_dpth,
	enum signal_type signal)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	REG_UPDATE_2(FMT_DYNAMIC_EXP_CNTL,
			FMT_DYNAMIC_EXP_EN, 0,
			FMT_DYNAMIC_EXP_MODE, 0);

	if (opp->dyn_expansion == DYN_EXPANSION_DISABLE)
		return;

	/*00 - 10-bit -> 12-bit dynamic expansion*/
	/*01 - 8-bit  -> 12-bit dynamic expansion*/
	if (signal == SIGNAL_TYPE_HDMI_TYPE_A ||
		signal == SIGNAL_TYPE_DISPLAY_PORT ||
		signal == SIGNAL_TYPE_DISPLAY_PORT_MST ||
		signal == SIGNAL_TYPE_VIRTUAL) {
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

static void opp1_program_clamping_and_pixel_encoding(
	struct output_pixel_processor *opp,
	const struct clamping_and_pixel_encoding_params *params)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	opp1_set_clamping(oppn10, params);
	opp1_set_pixel_encoding(oppn10, params);
}

void opp1_program_fmt(
	struct output_pixel_processor *opp,
	struct bit_depth_reduction_params *fmt_bit_depth,
	struct clamping_and_pixel_encoding_params *clamping)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	if (clamping->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		REG_UPDATE(FMT_MAP420_MEMORY_CONTROL, FMT_MAP420MEM_PWR_FORCE, 0);

	/* dithering is affected by <CrtcSourceSelect>, hence should be
	 * programmed afterwards */
	opp1_program_bit_depth_reduction(
		opp,
		fmt_bit_depth);

	opp1_program_clamping_and_pixel_encoding(
		opp,
		clamping);

	return;
}

void opp1_program_stereo(
	struct output_pixel_processor *opp,
	bool enable,
	const struct dc_crtc_timing *timing)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	uint32_t active_width = timing->h_addressable - timing->h_border_right - timing->h_border_right;
	uint32_t space1_size = timing->v_total - timing->v_addressable;
	/* TODO: confirm computation of space2_size */
	uint32_t space2_size = timing->v_total - timing->v_addressable;

	if (!enable) {
		active_width = 0;
		space1_size = 0;
		space2_size = 0;
	}

	/* TODO: for which cases should FMT_STEREOSYNC_OVERRIDE be set? */
	REG_UPDATE(FMT_CONTROL, FMT_STEREOSYNC_OVERRIDE, 0);

	REG_UPDATE(OPPBUF_CONTROL, OPPBUF_ACTIVE_WIDTH, active_width);

	/* Program OPPBUF_3D_VACT_SPACE1_SIZE and OPPBUF_VACT_SPACE2_SIZE registers
	 * In 3D progressive frames, Vactive space happens only in between the 2 frames,
	 * so only need to program OPPBUF_3D_VACT_SPACE1_SIZE
	 * In 3D alternative frames, left and right frames, top and bottom field.
	 */
	if (timing->timing_3d_format == TIMING_3D_FORMAT_FRAME_ALTERNATE)
		REG_UPDATE(OPPBUF_3D_PARAMETERS_0, OPPBUF_3D_VACT_SPACE2_SIZE, space2_size);
	else
		REG_UPDATE(OPPBUF_3D_PARAMETERS_0, OPPBUF_3D_VACT_SPACE1_SIZE, space1_size);

	/* TODO: Is programming of OPPBUF_DUMMY_DATA_R/G/B needed? */
	/*
	REG_UPDATE(OPPBUF_3D_PARAMETERS_0,
			OPPBUF_DUMMY_DATA_R, data_r);
	REG_UPDATE(OPPBUF_3D_PARAMETERS_1,
			OPPBUF_DUMMY_DATA_G, data_g);
	REG_UPDATE(OPPBUF_3D_PARAMETERS_1,
			OPPBUF_DUMMY_DATA_B, _data_b);
	*/
}

void opp1_program_oppbuf(
	struct output_pixel_processor *opp,
	struct oppbuf_params *oppbuf)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);

	/* Program the oppbuf active width to be the frame width from mpc */
	REG_UPDATE(OPPBUF_CONTROL, OPPBUF_ACTIVE_WIDTH, oppbuf->active_width);

	/* Specifies the number of segments in multi-segment mode (DP-MSO operation)
	 * description  "In 1/2/4 segment mode, specifies the horizontal active width in pixels of the display panel.
	 * In 4 segment split left/right mode, specifies the horizontal 1/2 active width in pixels of the display panel.
	 * Used to determine segment boundaries in multi-segment mode. Used to determine the width of the vertical active space in 3D frame packed modes.
	 * OPPBUF_ACTIVE_WIDTH must be integer divisible by the total number of segments."
	 */
	REG_UPDATE(OPPBUF_CONTROL, OPPBUF_DISPLAY_SEGMENTATION, oppbuf->mso_segmentation);

	/* description  "Specifies the number of overlap pixels (1-8 overlapping pixels supported), used in multi-segment mode (DP-MSO operation)" */
	REG_UPDATE(OPPBUF_CONTROL, OPPBUF_OVERLAP_PIXEL_NUM, oppbuf->mso_overlap_pixel_num);

	/* description  "Specifies the number of times a pixel is replicated (0-15 pixel replications supported).
	 * A value of 0 disables replication. The total number of times a pixel is output is OPPBUF_PIXEL_REPETITION + 1."
	 */
	REG_UPDATE(OPPBUF_CONTROL, OPPBUF_PIXEL_REPETITION, oppbuf->pixel_repetition);

	/* Controls the number of padded pixels at the end of a segment */
	if (REG(OPPBUF_CONTROL1))
		REG_UPDATE(OPPBUF_CONTROL1, OPPBUF_NUM_SEGMENT_PADDED_PIXELS, oppbuf->num_segment_padded_pixels);
}

void opp1_pipe_clock_control(struct output_pixel_processor *opp, bool enable)
{
	struct dcn10_opp *oppn10 = TO_DCN10_OPP(opp);
	uint32_t regval = enable ? 1 : 0;

	REG_UPDATE(OPP_PIPE_CONTROL, OPP_PIPE_CLOCK_EN, regval);
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

void opp1_destroy(struct output_pixel_processor **opp)
{
	kfree(TO_DCN10_OPP(*opp));
	*opp = NULL;
}

static const struct opp_funcs dcn10_opp_funcs = {
		.opp_set_dyn_expansion = opp1_set_dyn_expansion,
		.opp_program_fmt = opp1_program_fmt,
		.opp_program_bit_depth_reduction = opp1_program_bit_depth_reduction,
		.opp_program_stereo = opp1_program_stereo,
		.opp_pipe_clock_control = opp1_pipe_clock_control,
		.opp_set_disp_pattern_generator = NULL,
		.dpg_is_blanked = NULL,
		.opp_destroy = opp1_destroy
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
