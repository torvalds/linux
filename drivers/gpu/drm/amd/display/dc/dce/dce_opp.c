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
#include "basics/conversion.h"

#include "dce_opp.h"

#include "reg_helper.h"

#define REG(reg)\
	(opp110->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	opp110->opp_shift->field_name, opp110->opp_mask->field_name

#define CTX \
	opp110->base.ctx

enum {
	MAX_PWL_ENTRY = 128,
	MAX_REGIONS_NUMBER = 16
};

enum {
	MAX_LUT_ENTRY = 256,
	MAX_NUMBER_OF_ENTRIES = 256
};


enum {
	OUTPUT_CSC_MATRIX_SIZE = 12
};






















/*
 *****************************************************************************
 *  Function: regamma_config_regions_and_segments
 *
 *     build regamma curve by using predefined hw points
 *     uses interface parameters ,like EDID coeff.
 *
 * @param   : parameters   interface parameters
 *  @return void
 *
 *  @note
 *
 *  @see
 *
 *****************************************************************************
 */



/**
 *	set_truncation
 *	1) set truncation depth: 0 for 18 bpp or 1 for 24 bpp
 *	2) enable truncation
 *	3) HW remove 12bit FMT support for DCE11 power saving reason.
 */
static void set_truncation(
		struct dce110_opp *opp110,
		const struct bit_depth_reduction_params *params)
{
	/*Disable truncation*/
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
			FMT_TRUNCATE_EN, 0,
			FMT_TRUNCATE_DEPTH, 0,
			FMT_TRUNCATE_MODE, 0);


	if (params->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		/*  8bpc trunc on YCbCr422*/
		if (params->flags.TRUNCATE_DEPTH == 1)
			REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
					FMT_TRUNCATE_EN, 1,
					FMT_TRUNCATE_DEPTH, 1,
					FMT_TRUNCATE_MODE, 0);
		else if (params->flags.TRUNCATE_DEPTH == 2)
			/*  10bpc trunc on YCbCr422*/
			REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
					FMT_TRUNCATE_EN, 1,
					FMT_TRUNCATE_DEPTH, 2,
					FMT_TRUNCATE_MODE, 0);
		return;
	}
	/* on other format-to do */
	if (params->flags.TRUNCATE_ENABLED == 0)
		return;
	/*Set truncation depth and Enable truncation*/
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
				FMT_TRUNCATE_EN, 1,
				FMT_TRUNCATE_DEPTH,
				params->flags.TRUNCATE_DEPTH,
				FMT_TRUNCATE_MODE,
				params->flags.TRUNCATE_MODE);
}


/**
 *	set_spatial_dither
 *	1) set spatial dithering mode: pattern of seed
 *	2) set spatial dithering depth: 0 for 18bpp or 1 for 24bpp
 *	3) set random seed
 *	4) set random mode
 *		lfsr is reset every frame or not reset
 *		RGB dithering method
 *		0: RGB data are all dithered with x^28+x^3+1
 *		1: R data is dithered with x^28+x^3+1
 *		G data is dithered with x^28+X^9+1
 *		B data is dithered with x^28+x^13+1
 *		enable high pass filter or not
 *	5) enable spatical dithering
 */
static void set_spatial_dither(
	struct dce110_opp *opp110,
	const struct bit_depth_reduction_params *params)
{
	/*Disable spatial (random) dithering*/
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_SPATIAL_DITHER_EN, 0,
		FMT_SPATIAL_DITHER_DEPTH, 0,
		FMT_SPATIAL_DITHER_MODE, 0);

	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_HIGHPASS_RANDOM_ENABLE, 0,
		FMT_FRAME_RANDOM_ENABLE, 0,
		FMT_RGB_RANDOM_ENABLE, 0);

	REG_UPDATE(FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_EN, 0);

	/* no 10bpc on DCE11*/
	if (params->flags.SPATIAL_DITHER_ENABLED == 0 ||
		params->flags.SPATIAL_DITHER_DEPTH == 2)
		return;

	/* only use FRAME_COUNTER_MAX if frameRandom == 1*/

	if (opp110->opp_mask->FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX &&
			opp110->opp_mask->FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP) {
		if (params->flags.FRAME_RANDOM == 1) {
			if (params->flags.SPATIAL_DITHER_DEPTH == 0 ||
			params->flags.SPATIAL_DITHER_DEPTH == 1) {
				REG_UPDATE_2(FMT_CONTROL,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, 15,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, 2);
			} else if (params->flags.SPATIAL_DITHER_DEPTH == 2) {
				REG_UPDATE_2(FMT_CONTROL,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, 3,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, 1);
			} else
				return;
		} else {
			REG_UPDATE_2(FMT_CONTROL,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, 0,
					FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, 0);
		}
	}
	/* Set seed for random values for
	 * spatial dithering for R,G,B channels
	 */
	REG_UPDATE(FMT_DITHER_RAND_R_SEED,
			FMT_RAND_R_SEED, params->r_seed_value);

	REG_UPDATE(FMT_DITHER_RAND_G_SEED,
			FMT_RAND_G_SEED, params->g_seed_value);

	REG_UPDATE(FMT_DITHER_RAND_B_SEED,
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

	/* Disable High pass filter
	 * Reset only at startup
	 * Set RGB data dithered with x^28+x^3+1
	 */
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_HIGHPASS_RANDOM_ENABLE, params->flags.HIGHPASS_RANDOM,
		FMT_FRAME_RANDOM_ENABLE, params->flags.FRAME_RANDOM,
		FMT_RGB_RANDOM_ENABLE, params->flags.RGB_RANDOM);

	/* Set spatial dithering bit depth
	 * Set spatial dithering mode
	 * (default is Seed patterrn AAAA...)
	 * Enable spatial dithering
	 */
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_SPATIAL_DITHER_DEPTH, params->flags.SPATIAL_DITHER_DEPTH,
		FMT_SPATIAL_DITHER_MODE, params->flags.SPATIAL_DITHER_MODE,
		FMT_SPATIAL_DITHER_EN, 1);
}

/**
 *	SetTemporalDither (Frame Modulation)
 *	1) set temporal dither depth
 *	2) select pattern: from hard-coded pattern or programmable pattern
 *	3) select optimized strips for BGR or RGB LCD sub-pixel
 *	4) set s matrix
 *	5) set t matrix
 *	6) set grey level for 0.25, 0.5, 0.75
 *	7) enable temporal dithering
 */

static void set_temporal_dither(
	struct dce110_opp *opp110,
	const struct bit_depth_reduction_params *params)
{
	/*Disable temporal (frame modulation) dithering first*/
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_EN, 0,
		FMT_TEMPORAL_DITHER_RESET, 0,
		FMT_TEMPORAL_DITHER_OFFSET, 0);

	REG_UPDATE_2(FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_DEPTH, 0,
		FMT_TEMPORAL_LEVEL, 0);

	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_25FRC_SEL, 0,
		FMT_50FRC_SEL, 0,
		FMT_75FRC_SEL, 0);

	/* no 10bpc dither on DCE11*/
	if (params->flags.FRAME_MODULATION_ENABLED == 0 ||
		params->flags.FRAME_MODULATION_DEPTH == 2)
		return;

	/* Set temporal dithering depth*/
	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_DEPTH, params->flags.FRAME_MODULATION_DEPTH,
		FMT_TEMPORAL_DITHER_RESET, 0,
		FMT_TEMPORAL_DITHER_OFFSET, 0);

	/*Select legacy pattern based on FRC and Temporal level*/
	if (REG(FMT_TEMPORAL_DITHER_PATTERN_CONTROL)) {
		REG_WRITE(FMT_TEMPORAL_DITHER_PATTERN_CONTROL, 0);
		/*Set s matrix*/
		REG_WRITE(FMT_TEMPORAL_DITHER_PROGRAMMABLE_PATTERN_S_MATRIX, 0);
		/*Set t matrix*/
		REG_WRITE(FMT_TEMPORAL_DITHER_PROGRAMMABLE_PATTERN_T_MATRIX, 0);
	}

	/*Select patterns for 0.25, 0.5 and 0.75 grey level*/
	REG_UPDATE(FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_LEVEL, params->flags.TEMPORAL_LEVEL);

	REG_UPDATE_3(FMT_BIT_DEPTH_CONTROL,
		FMT_25FRC_SEL, params->flags.FRC25,
		FMT_50FRC_SEL, params->flags.FRC50,
		FMT_75FRC_SEL, params->flags.FRC75);

	/*Enable bit reduction by temporal (frame modulation) dithering*/
	REG_UPDATE(FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_EN, 1);
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
void dce110_opp_set_clamping(
	struct dce110_opp *opp110,
	const struct clamping_and_pixel_encoding_params *params)
{
	REG_SET_2(FMT_CLAMP_CNTL, 0,
		FMT_CLAMP_DATA_EN, 0,
		FMT_CLAMP_COLOR_FORMAT, 0);

	switch (params->clamping_level) {
	case CLAMPING_FULL_RANGE:
		break;
	case CLAMPING_LIMITED_RANGE_8BPC:
		REG_SET_2(FMT_CLAMP_CNTL, 0,
			FMT_CLAMP_DATA_EN, 1,
			FMT_CLAMP_COLOR_FORMAT, 1);
		break;
	case CLAMPING_LIMITED_RANGE_10BPC:
		REG_SET_2(FMT_CLAMP_CNTL, 0,
			FMT_CLAMP_DATA_EN, 1,
			FMT_CLAMP_COLOR_FORMAT, 2);
		break;
	case CLAMPING_LIMITED_RANGE_12BPC:
		REG_SET_2(FMT_CLAMP_CNTL, 0,
			FMT_CLAMP_DATA_EN, 1,
			FMT_CLAMP_COLOR_FORMAT, 3);
		break;
	case CLAMPING_LIMITED_RANGE_PROGRAMMABLE:
		/*Set clamp control*/
		REG_SET_2(FMT_CLAMP_CNTL, 0,
			FMT_CLAMP_DATA_EN, 1,
			FMT_CLAMP_COLOR_FORMAT, 7);

		/*set the defaults*/
		REG_SET_2(FMT_CLAMP_COMPONENT_R, 0,
			FMT_CLAMP_LOWER_R, 0x10,
			FMT_CLAMP_UPPER_R, 0xFEF);

		REG_SET_2(FMT_CLAMP_COMPONENT_G, 0,
			FMT_CLAMP_LOWER_G, 0x10,
			FMT_CLAMP_UPPER_G, 0xFEF);

		REG_SET_2(FMT_CLAMP_COMPONENT_B, 0,
			FMT_CLAMP_LOWER_B, 0x10,
			FMT_CLAMP_UPPER_B, 0xFEF);
		break;
	default:
		break;
	}
}

/**
 *	set_pixel_encoding
 *
 *	Set Pixel Encoding
 *		0: RGB 4:4:4 or YCbCr 4:4:4 or YOnly
 *		1: YCbCr 4:2:2
 */
static void set_pixel_encoding(
	struct dce110_opp *opp110,
	const struct clamping_and_pixel_encoding_params *params)
{
	if (opp110->opp_mask->FMT_CBCR_BIT_REDUCTION_BYPASS)
		REG_UPDATE_3(FMT_CONTROL,
				FMT_PIXEL_ENCODING, 0,
				FMT_SUBSAMPLING_MODE, 0,
				FMT_CBCR_BIT_REDUCTION_BYPASS, 0);
	else
		REG_UPDATE_2(FMT_CONTROL,
				FMT_PIXEL_ENCODING, 0,
				FMT_SUBSAMPLING_MODE, 0);

	if (params->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		REG_UPDATE_2(FMT_CONTROL,
				FMT_PIXEL_ENCODING, 1,
				FMT_SUBSAMPLING_ORDER, 0);
	}
	if (params->pixel_encoding == PIXEL_ENCODING_YCBCR420) {
		REG_UPDATE_3(FMT_CONTROL,
				FMT_PIXEL_ENCODING, 2,
				FMT_SUBSAMPLING_MODE, 2,
				FMT_CBCR_BIT_REDUCTION_BYPASS, 1);
	}

}

void dce110_opp_program_bit_depth_reduction(
	struct output_pixel_processor *opp,
	const struct bit_depth_reduction_params *params)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	set_truncation(opp110, params);
	set_spatial_dither(opp110, params);
	set_temporal_dither(opp110, params);
}

void dce110_opp_program_clamping_and_pixel_encoding(
	struct output_pixel_processor *opp,
	const struct clamping_and_pixel_encoding_params *params)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	dce110_opp_set_clamping(opp110, params);
	set_pixel_encoding(opp110, params);
}

static void program_formatter_420_memory(struct output_pixel_processor *opp)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);
	uint32_t fmt_mem_cntl_value;

	/* Program source select*/
	/* Use HW default source select for FMT_MEMORYx_CONTROL */
	/* Use that value for FMT_SRC_SELECT as well*/
	REG_GET(CONTROL,
			FMT420_MEM0_SOURCE_SEL, &fmt_mem_cntl_value);

	REG_UPDATE(FMT_CONTROL,
			FMT_SRC_SELECT, fmt_mem_cntl_value);

	/* Turn on the memory */
	REG_UPDATE(CONTROL,
			FMT420_MEM0_PWR_FORCE, 0);
}

void dce110_opp_set_dyn_expansion(
	struct output_pixel_processor *opp,
	enum dc_color_space color_sp,
	enum dc_color_depth color_dpth,
	enum signal_type signal)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

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
			REG_UPDATE_2(
				FMT_DYNAMIC_EXP_CNTL,
				FMT_DYNAMIC_EXP_EN, 1,/*otherwise last two bits are zero*/
				FMT_DYNAMIC_EXP_MODE, 0);
			break;
		default:
			break;
		}
	}
}

static void program_formatter_reset_dig_resync_fifo(struct output_pixel_processor *opp)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);

	/* clear previous phase lock status*/
	REG_UPDATE(FMT_CONTROL,
			FMT_420_PIXEL_PHASE_LOCKED_CLEAR, 1);

	/* poll until FMT_420_PIXEL_PHASE_LOCKED become 1*/
	REG_WAIT(FMT_CONTROL, FMT_420_PIXEL_PHASE_LOCKED, 1, 10, 10);

}

void dce110_opp_program_fmt(
	struct output_pixel_processor *opp,
	struct bit_depth_reduction_params *fmt_bit_depth,
	struct clamping_and_pixel_encoding_params *clamping)
{
	/* dithering is affected by <CrtcSourceSelect>, hence should be
	 * programmed afterwards */

	if (clamping->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		program_formatter_420_memory(opp);

	dce110_opp_program_bit_depth_reduction(
		opp,
		fmt_bit_depth);

	dce110_opp_program_clamping_and_pixel_encoding(
		opp,
		clamping);

	if (clamping->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		program_formatter_reset_dig_resync_fifo(opp);

	return;
}





/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

static const struct opp_funcs funcs = {
	.opp_set_dyn_expansion = dce110_opp_set_dyn_expansion,
	.opp_destroy = dce110_opp_destroy,
	.opp_program_fmt = dce110_opp_program_fmt,
	.opp_program_bit_depth_reduction = dce110_opp_program_bit_depth_reduction
};

void dce110_opp_construct(struct dce110_opp *opp110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce_opp_registers *regs,
	const struct dce_opp_shift *opp_shift,
	const struct dce_opp_mask *opp_mask)
{
	opp110->base.funcs = &funcs;

	opp110->base.ctx = ctx;

	opp110->base.inst = inst;

	opp110->regs = regs;
	opp110->opp_shift = opp_shift;
	opp110->opp_mask = opp_mask;
}

void dce110_opp_destroy(struct output_pixel_processor **opp)
{
	if (*opp)
		kfree(FROM_DCE11_OPP(*opp));
	*opp = NULL;
}

