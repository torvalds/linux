/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"

#include "dce80_opp.h"

#define FMT_REG(reg)\
	(reg + opp80->offsets.fmt_offset)

/**
 *	set_truncation
 *	1) set truncation depth: 0 for 18 bpp or 1 for 24 bpp
 *	2) enable truncation
 *	3) HW remove 12bit FMT support for DCE8 power saving reason.
 */
static void set_truncation(
		struct dce80_opp *opp80,
		const struct bit_depth_reduction_params *params)
{
	uint32_t value = 0;
	uint32_t addr = FMT_REG(mmFMT_BIT_DEPTH_CONTROL);

	/*Disable truncation*/
	value = dm_read_reg(opp80->base.ctx, addr);
	set_reg_field_value(value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_EN);
	set_reg_field_value(value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_DEPTH);
	set_reg_field_value(value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_MODE);

	dm_write_reg(opp80->base.ctx, addr, value);

	/* no 10bpc trunc on DCE8*/
	if (params->flags.TRUNCATE_ENABLED == 0 ||
		params->flags.TRUNCATE_DEPTH == 2)
		return;

	/*Set truncation depth and Enable truncation*/
	set_reg_field_value(value, 1,
		FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_EN);
	set_reg_field_value(value, params->flags.TRUNCATE_MODE,
		FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_MODE);
	set_reg_field_value(value, params->flags.TRUNCATE_DEPTH,
		FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_DEPTH);

	dm_write_reg(opp80->base.ctx, addr, value);

}

/**
 *	set_spatial_dither
 *	1) set spatial dithering mode: pattern of seed
 *	2) set spatical dithering depth: 0 for 18bpp or 1 for 24bpp
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
	struct dce80_opp *opp80,
	const struct bit_depth_reduction_params *params)
{
	uint32_t addr = FMT_REG(mmFMT_BIT_DEPTH_CONTROL);
	uint32_t depth_cntl_value = 0;
	uint32_t dither_r_value = 0;
	uint32_t dither_g_value = 0;
	uint32_t dither_b_value = 0;

	/*Disable spatial (random) dithering*/
	depth_cntl_value = dm_read_reg(opp80->base.ctx, addr);
	set_reg_field_value(depth_cntl_value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_EN);
	set_reg_field_value(depth_cntl_value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_MODE);
	set_reg_field_value(depth_cntl_value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_DEPTH);
	set_reg_field_value(depth_cntl_value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_TEMPORAL_DITHER_EN);
	set_reg_field_value(depth_cntl_value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_HIGHPASS_RANDOM_ENABLE);
	set_reg_field_value(depth_cntl_value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_FRAME_RANDOM_ENABLE);
	set_reg_field_value(depth_cntl_value, 0,
		FMT_BIT_DEPTH_CONTROL, FMT_RGB_RANDOM_ENABLE);

	dm_write_reg(opp80->base.ctx, addr, depth_cntl_value);

	/* no 10bpc on DCE8*/
	if (params->flags.SPATIAL_DITHER_ENABLED == 0 ||
		params->flags.SPATIAL_DITHER_DEPTH == 2)
		return;

	/*Set seed for random values for
	 * spatial dithering for R,G,B channels*/
	addr = FMT_REG(mmFMT_DITHER_RAND_R_SEED);
	set_reg_field_value(dither_r_value, params->r_seed_value,
		FMT_DITHER_RAND_R_SEED,
		FMT_RAND_R_SEED);
	dm_write_reg(opp80->base.ctx, addr, dither_r_value);

	addr = FMT_REG(mmFMT_DITHER_RAND_G_SEED);
	set_reg_field_value(dither_g_value,
		params->g_seed_value,
		FMT_DITHER_RAND_G_SEED,
		FMT_RAND_G_SEED);
	dm_write_reg(opp80->base.ctx, addr, dither_g_value);

	addr = FMT_REG(mmFMT_DITHER_RAND_B_SEED);
	set_reg_field_value(dither_b_value, params->b_seed_value,
		FMT_DITHER_RAND_B_SEED,
		FMT_RAND_B_SEED);
	dm_write_reg(opp80->base.ctx, addr, dither_b_value);

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

	/*Set spatial dithering bit depth*/
	set_reg_field_value(depth_cntl_value,
		params->flags.SPATIAL_DITHER_DEPTH,
		FMT_BIT_DEPTH_CONTROL,
		FMT_SPATIAL_DITHER_DEPTH);

	/* Set spatial dithering mode
	 * (default is Seed patterrn AAAA...)
	 */
	set_reg_field_value(depth_cntl_value,
		params->flags.SPATIAL_DITHER_MODE,
		FMT_BIT_DEPTH_CONTROL,
		FMT_SPATIAL_DITHER_MODE);

	/*Reset only at startup*/
	set_reg_field_value(depth_cntl_value,
		params->flags.FRAME_RANDOM,
		FMT_BIT_DEPTH_CONTROL,
		FMT_FRAME_RANDOM_ENABLE);

	/*Set RGB data dithered with x^28+x^3+1*/
	set_reg_field_value(depth_cntl_value,
		params->flags.RGB_RANDOM,
		FMT_BIT_DEPTH_CONTROL,
		FMT_RGB_RANDOM_ENABLE);

	/*Disable High pass filter*/
	set_reg_field_value(depth_cntl_value,
		params->flags.HIGHPASS_RANDOM,
		FMT_BIT_DEPTH_CONTROL,
		FMT_HIGHPASS_RANDOM_ENABLE);

	/*Enable spatial dithering*/
	set_reg_field_value(depth_cntl_value,
		1,
		FMT_BIT_DEPTH_CONTROL,
		FMT_SPATIAL_DITHER_EN);

	addr = FMT_REG(mmFMT_BIT_DEPTH_CONTROL);
	dm_write_reg(opp80->base.ctx, addr, depth_cntl_value);

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
	struct dce80_opp *opp80,
	const struct bit_depth_reduction_params *params)
{
	uint32_t addr = FMT_REG(mmFMT_BIT_DEPTH_CONTROL);
	uint32_t value;

	/*Disable temporal (frame modulation) dithering first*/
	value = dm_read_reg(opp80->base.ctx, addr);

	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_EN);

	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_RESET);
	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_OFFSET);
	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_DEPTH);
	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_LEVEL);
	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_25FRC_SEL);

	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_50FRC_SEL);

	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_75FRC_SEL);

	dm_write_reg(opp80->base.ctx, addr, value);

	/* no 10bpc dither on DCE8*/
	if (params->flags.FRAME_MODULATION_ENABLED == 0 ||
		params->flags.FRAME_MODULATION_DEPTH == 2)
		return;

	/* Set temporal dithering depth*/
	set_reg_field_value(value,
		params->flags.FRAME_MODULATION_DEPTH,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_DEPTH);

	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_RESET);

	set_reg_field_value(value,
		0,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_OFFSET);

	/*Select legacy pattern based on FRC and Temporal level*/
	addr = FMT_REG(mmFMT_TEMPORAL_DITHER_PATTERN_CONTROL);
	dm_write_reg(opp80->base.ctx, addr, 0);
	/*Set s matrix*/
	addr = FMT_REG(
		mmFMT_TEMPORAL_DITHER_PROGRAMMABLE_PATTERN_S_MATRIX);
	dm_write_reg(opp80->base.ctx, addr, 0);
	/*Set t matrix*/
	addr = FMT_REG(
		mmFMT_TEMPORAL_DITHER_PROGRAMMABLE_PATTERN_T_MATRIX);
	dm_write_reg(opp80->base.ctx, addr, 0);

	/*Select patterns for 0.25, 0.5 and 0.75 grey level*/
	set_reg_field_value(value,
		params->flags.TEMPORAL_LEVEL,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_LEVEL);

	set_reg_field_value(value,
		params->flags.FRC25,
		FMT_BIT_DEPTH_CONTROL,
		FMT_25FRC_SEL);

	set_reg_field_value(value,
		params->flags.FRC50,
		FMT_BIT_DEPTH_CONTROL,
		FMT_50FRC_SEL);

	set_reg_field_value(value,
		params->flags.FRC75,
		FMT_BIT_DEPTH_CONTROL,
		FMT_75FRC_SEL);

	/*Enable bit reduction by temporal (frame modulation) dithering*/
	set_reg_field_value(value,
		1,
		FMT_BIT_DEPTH_CONTROL,
		FMT_TEMPORAL_DITHER_EN);

	addr = FMT_REG(mmFMT_BIT_DEPTH_CONTROL);
	dm_write_reg(opp80->base.ctx, addr, value);

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
static void set_clamping(
	struct dce80_opp *opp80,
	const struct clamping_and_pixel_encoding_params *params)
{
	uint32_t clamp_cntl_value = 0;
	uint32_t red_clamp_value = 0;
	uint32_t green_clamp_value = 0;
	uint32_t blue_clamp_value = 0;
	uint32_t addr = FMT_REG(mmFMT_CLAMP_CNTL);

	clamp_cntl_value = dm_read_reg(opp80->base.ctx, addr);

	set_reg_field_value(clamp_cntl_value,
		0,
		FMT_CLAMP_CNTL,
		FMT_CLAMP_DATA_EN);

	set_reg_field_value(clamp_cntl_value,
		0,
		FMT_CLAMP_CNTL,
		FMT_CLAMP_COLOR_FORMAT);

	switch (params->clamping_level) {
	case CLAMPING_FULL_RANGE:
		break;

	case CLAMPING_LIMITED_RANGE_8BPC:
		set_reg_field_value(clamp_cntl_value,
			1,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_DATA_EN);

		set_reg_field_value(clamp_cntl_value,
			1,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_COLOR_FORMAT);

		break;

	case CLAMPING_LIMITED_RANGE_10BPC:
		set_reg_field_value(clamp_cntl_value,
			1,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_DATA_EN);

		set_reg_field_value(clamp_cntl_value,
			2,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_COLOR_FORMAT);

		break;
	case CLAMPING_LIMITED_RANGE_12BPC:
		set_reg_field_value(clamp_cntl_value,
			1,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_DATA_EN);

		set_reg_field_value(clamp_cntl_value,
			3,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_COLOR_FORMAT);

		break;
	case CLAMPING_LIMITED_RANGE_PROGRAMMABLE:
		set_reg_field_value(clamp_cntl_value,
			1,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_DATA_EN);

		set_reg_field_value(clamp_cntl_value,
			7,
			FMT_CLAMP_CNTL,
			FMT_CLAMP_COLOR_FORMAT);

		/*set the defaults*/
		set_reg_field_value(red_clamp_value,
			0x10,
			FMT_CLAMP_COMPONENT_R,
			FMT_CLAMP_LOWER_R);

		set_reg_field_value(red_clamp_value,
			0xFEF,
			FMT_CLAMP_COMPONENT_R,
			FMT_CLAMP_UPPER_R);

		addr = FMT_REG(mmFMT_CLAMP_COMPONENT_R);
		dm_write_reg(opp80->base.ctx, addr, red_clamp_value);

		set_reg_field_value(green_clamp_value,
			0x10,
			FMT_CLAMP_COMPONENT_G,
			FMT_CLAMP_LOWER_G);

		set_reg_field_value(green_clamp_value,
			0xFEF,
			FMT_CLAMP_COMPONENT_G,
			FMT_CLAMP_UPPER_G);

		addr = FMT_REG(mmFMT_CLAMP_COMPONENT_G);
		dm_write_reg(opp80->base.ctx, addr, green_clamp_value);

		set_reg_field_value(blue_clamp_value,
			0x10,
			FMT_CLAMP_COMPONENT_B,
			FMT_CLAMP_LOWER_B);

		set_reg_field_value(blue_clamp_value,
			0xFEF,
			FMT_CLAMP_COMPONENT_B,
			FMT_CLAMP_UPPER_B);

		addr = FMT_REG(mmFMT_CLAMP_COMPONENT_B);
		dm_write_reg(opp80->base.ctx, addr, blue_clamp_value);

		break;

	default:
		break;
	}

	addr = FMT_REG(mmFMT_CLAMP_CNTL);
	/*Set clamp control*/
	dm_write_reg(opp80->base.ctx, addr, clamp_cntl_value);

}

/**
 *	set_pixel_encoding
 *
 *	Set Pixel Encoding
 *		0: RGB 4:4:4 or YCbCr 4:4:4 or YOnly
 *		1: YCbCr 4:2:2
 */
static void set_pixel_encoding(
	struct dce80_opp *opp80,
	const struct clamping_and_pixel_encoding_params *params)
{
	uint32_t fmt_cntl_value;
	uint32_t addr = FMT_REG(mmFMT_CONTROL);

	/*RGB 4:4:4 or YCbCr 4:4:4 - 0; YCbCr 4:2:2 -1.*/
	fmt_cntl_value = dm_read_reg(opp80->base.ctx, addr);

	set_reg_field_value(fmt_cntl_value,
		0,
		FMT_CONTROL,
		FMT_PIXEL_ENCODING);

	if (params->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		set_reg_field_value(fmt_cntl_value,
			1,
			FMT_CONTROL,
			FMT_PIXEL_ENCODING);

		/*00 - Pixels drop mode ,01 - Pixels average mode*/
		set_reg_field_value(fmt_cntl_value,
			0,
			FMT_CONTROL,
			FMT_SUBSAMPLING_MODE);

		/*00 - Cb before Cr ,01 - Cr before Cb*/
		set_reg_field_value(fmt_cntl_value,
			0,
			FMT_CONTROL,
			FMT_SUBSAMPLING_ORDER);
	}
	dm_write_reg(opp80->base.ctx, addr, fmt_cntl_value);

}

void dce80_opp_program_bit_depth_reduction(
	struct output_pixel_processor *opp,
	const struct bit_depth_reduction_params *params)
{
	struct dce80_opp *opp80 = TO_DCE80_OPP(opp);

	set_truncation(opp80, params);
	set_spatial_dither(opp80, params);
	set_temporal_dither(opp80, params);
}

void dce80_opp_program_clamping_and_pixel_encoding(
	struct output_pixel_processor *opp,
	const struct clamping_and_pixel_encoding_params *params)
{
	struct dce80_opp *opp80 = TO_DCE80_OPP(opp);

	set_clamping(opp80, params);
	set_pixel_encoding(opp80, params);
}

void dce80_opp_set_dyn_expansion(
	struct output_pixel_processor *opp,
	enum dc_color_space color_sp,
	enum dc_color_depth color_dpth,
	enum signal_type signal)
{
	struct dce80_opp *opp80 = TO_DCE80_OPP(opp);
	uint32_t value;
	bool enable_dyn_exp = false;
	uint32_t addr = FMT_REG(mmFMT_DYNAMIC_EXP_CNTL);

	value = dm_read_reg(opp->ctx, addr);

	set_reg_field_value(value, 0,
		FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_EN);
	set_reg_field_value(value, 0,
		FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_MODE);

	/* From HW programming guide:
		FMT_DYNAMIC_EXP_EN = 0 for limited RGB or YCbCr output
		FMT_DYNAMIC_EXP_EN = 1 for RGB full range only*/
	if (color_sp == COLOR_SPACE_SRGB)
		enable_dyn_exp = true;

	/*00 - 10-bit -> 12-bit dynamic expansion*/
	/*01 - 8-bit  -> 12-bit dynamic expansion*/
	if (signal == SIGNAL_TYPE_HDMI_TYPE_A) {
		switch (color_dpth) {
		case COLOR_DEPTH_888:
			set_reg_field_value(value, enable_dyn_exp ? 1:0,
				FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_EN);
			set_reg_field_value(value, 1,
				FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_MODE);
			break;
		case COLOR_DEPTH_101010:
			set_reg_field_value(value, enable_dyn_exp ? 1:0,
				FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_EN);
			set_reg_field_value(value, 0,
				FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_MODE);
			break;
		case COLOR_DEPTH_121212:
			break;
		default:
			break;
		}
	}

	dm_write_reg(opp->ctx, addr, value);
}
