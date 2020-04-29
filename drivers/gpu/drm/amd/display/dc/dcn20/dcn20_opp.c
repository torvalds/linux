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
#include "dcn20_opp.h"
#include "reg_helper.h"

#define REG(reg) \
	(oppn20->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	oppn20->opp_shift->field_name, oppn20->opp_mask->field_name

#define CTX \
	oppn20->base.ctx


void opp2_set_disp_pattern_generator(
		struct output_pixel_processor *opp,
		enum controller_dp_test_pattern test_pattern,
		enum controller_dp_color_space color_space,
		enum dc_color_depth color_depth,
		const struct tg_color *solid_color,
		int width,
		int height,
		int offset)
{
	struct dcn20_opp *oppn20 = TO_DCN20_OPP(opp);
	enum test_pattern_color_format bit_depth;
	enum test_pattern_dyn_range dyn_range;
	enum test_pattern_mode mode;

	/* color ramp generator mixes 16-bits color */
	uint32_t src_bpc = 16;
	/* requested bpc */
	uint32_t dst_bpc;
	uint32_t index;
	/* RGB values of the color bars.
	 * Produce two RGB colors: RGB0 - white (all Fs)
	 * and RGB1 - black (all 0s)
	 * (three RGB components for two colors)
	 */
	uint16_t src_color[6] = {0xFFFF, 0xFFFF, 0xFFFF, 0x0000,
						0x0000, 0x0000};
	/* dest color (converted to the specified color format) */
	uint16_t dst_color[6];
	uint32_t inc_base;

	/* translate to bit depth */
	switch (color_depth) {
	case COLOR_DEPTH_666:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_6;
	break;
	case COLOR_DEPTH_888:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_8;
	break;
	case COLOR_DEPTH_101010:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_10;
	break;
	case COLOR_DEPTH_121212:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_12;
	break;
	default:
		bit_depth = TEST_PATTERN_COLOR_FORMAT_BPC_8;
	break;
	}

	/* set DPG dimentions */
	REG_SET_2(DPG_DIMENSIONS, 0,
		DPG_ACTIVE_WIDTH, width,
		DPG_ACTIVE_HEIGHT, height);

	/* set DPG offset */
	REG_SET_2(DPG_OFFSET_SEGMENT, 0,
		DPG_X_OFFSET, offset,
		DPG_SEGMENT_WIDTH, 0);

	switch (test_pattern) {
	case CONTROLLER_DP_TEST_PATTERN_COLORSQUARES:
	case CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA:
	{
		dyn_range = (test_pattern ==
				CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA ?
				TEST_PATTERN_DYN_RANGE_CEA :
				TEST_PATTERN_DYN_RANGE_VESA);

		switch (color_space) {
		case CONTROLLER_DP_COLOR_SPACE_YCBCR601:
			mode = TEST_PATTERN_MODE_COLORSQUARES_YCBCR601;
		break;
		case CONTROLLER_DP_COLOR_SPACE_YCBCR709:
			mode = TEST_PATTERN_MODE_COLORSQUARES_YCBCR709;
		break;
		case CONTROLLER_DP_COLOR_SPACE_RGB:
		default:
			mode = TEST_PATTERN_MODE_COLORSQUARES_RGB;
		break;
		}

		REG_UPDATE_6(DPG_CONTROL,
			DPG_EN, 1,
			DPG_MODE, mode,
			DPG_DYNAMIC_RANGE, dyn_range,
			DPG_BIT_DEPTH, bit_depth,
			DPG_VRES, 6,
			DPG_HRES, 6);
	}
	break;

	case CONTROLLER_DP_TEST_PATTERN_VERTICALBARS:
	case CONTROLLER_DP_TEST_PATTERN_HORIZONTALBARS:
	{
		mode = (test_pattern ==
			CONTROLLER_DP_TEST_PATTERN_VERTICALBARS ?
			TEST_PATTERN_MODE_VERTICALBARS :
			TEST_PATTERN_MODE_HORIZONTALBARS);

		switch (bit_depth) {
		case TEST_PATTERN_COLOR_FORMAT_BPC_6:
			dst_bpc = 6;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
			dst_bpc = 8;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
			dst_bpc = 10;
		break;
		default:
			dst_bpc = 8;
		break;
		}

		/* adjust color to the required colorFormat */
		for (index = 0; index < 6; index++) {
			/* dst = 2^dstBpc * src / 2^srcBpc = src >>
			 * (srcBpc - dstBpc);
			 */
			dst_color[index] =
				src_color[index] >> (src_bpc - dst_bpc);
		/* DPG_COLOUR registers are 16-bit MSB aligned value with bits 3:0 hardwired to ZERO.
		 * XXXXXXXXXX000000 for 10 bit,
		 * XXXXXXXX00000000 for 8 bit,
		 * XXXXXX0000000000 for 6 bits
		 */
			dst_color[index] <<= (16 - dst_bpc);
		}

		REG_SET_2(DPG_COLOUR_R_CR, 0,
				DPG_COLOUR1_R_CR, dst_color[0],
				DPG_COLOUR0_R_CR, dst_color[3]);
		REG_SET_2(DPG_COLOUR_G_Y, 0,
				DPG_COLOUR1_G_Y, dst_color[1],
				DPG_COLOUR0_G_Y, dst_color[4]);
		REG_SET_2(DPG_COLOUR_B_CB, 0,
				DPG_COLOUR1_B_CB, dst_color[2],
				DPG_COLOUR0_B_CB, dst_color[5]);

		/* enable test pattern */
		REG_UPDATE_6(DPG_CONTROL,
			DPG_EN, 1,
			DPG_MODE, mode,
			DPG_DYNAMIC_RANGE, 0,
			DPG_BIT_DEPTH, bit_depth,
			DPG_VRES, 0,
			DPG_HRES, 0);
	}
	break;

	case CONTROLLER_DP_TEST_PATTERN_COLORRAMP:
	{
		mode = (bit_depth ==
			TEST_PATTERN_COLOR_FORMAT_BPC_10 ?
			TEST_PATTERN_MODE_DUALRAMP_RGB :
			TEST_PATTERN_MODE_SINGLERAMP_RGB);

		switch (bit_depth) {
		case TEST_PATTERN_COLOR_FORMAT_BPC_6:
			dst_bpc = 6;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
			dst_bpc = 8;
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
			dst_bpc = 10;
		break;
		default:
			dst_bpc = 8;
		break;
		}

		/* increment for the first ramp for one color gradation
		 * 1 gradation for 6-bit color is 2^10
		 * gradations in 16-bit color
		 */
		inc_base = (src_bpc - dst_bpc);

		switch (bit_depth) {
		case TEST_PATTERN_COLOR_FORMAT_BPC_6:
		{
			REG_SET_3(DPG_RAMP_CONTROL, 0,
				DPG_RAMP0_OFFSET, 0,
				DPG_INC0, inc_base,
				DPG_INC1, 0);
			REG_UPDATE_2(DPG_CONTROL,
				DPG_VRES, 6,
				DPG_HRES, 6);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_8:
		{
			REG_SET_3(DPG_RAMP_CONTROL, 0,
				DPG_RAMP0_OFFSET, 0,
				DPG_INC0, inc_base,
				DPG_INC1, 0);
			REG_UPDATE_2(DPG_CONTROL,
				DPG_VRES, 6,
				DPG_HRES, 8);
		}
		break;
		case TEST_PATTERN_COLOR_FORMAT_BPC_10:
		{
			REG_SET_3(DPG_RAMP_CONTROL, 0,
				DPG_RAMP0_OFFSET, 384 << 6,
				DPG_INC0, inc_base,
				DPG_INC1, inc_base + 2);
			REG_UPDATE_2(DPG_CONTROL,
				DPG_VRES, 5,
				DPG_HRES, 8);
		}
		break;
		default:
		break;
		}

		/* enable test pattern */
		REG_UPDATE_4(DPG_CONTROL,
			DPG_EN, 1,
			DPG_MODE, mode,
			DPG_DYNAMIC_RANGE, 0,
			DPG_BIT_DEPTH, bit_depth);
	}
	break;
	case CONTROLLER_DP_TEST_PATTERN_VIDEOMODE:
	{
		REG_WRITE(DPG_CONTROL, 0);
		REG_WRITE(DPG_COLOUR_R_CR, 0);
		REG_WRITE(DPG_COLOUR_G_Y, 0);
		REG_WRITE(DPG_COLOUR_B_CB, 0);
		REG_WRITE(DPG_RAMP_CONTROL, 0);
	}
	break;
	case CONTROLLER_DP_TEST_PATTERN_SOLID_COLOR:
	{
		opp2_dpg_set_blank_color(opp, solid_color);
		REG_UPDATE_2(DPG_CONTROL,
				DPG_EN, 1,
				DPG_MODE, TEST_PATTERN_MODE_HORIZONTALBARS);

		REG_SET_2(DPG_DIMENSIONS, 0,
				DPG_ACTIVE_WIDTH, width,
				DPG_ACTIVE_HEIGHT, height);
	}
	break;
	default:
		break;

	}
}

void opp2_dpg_set_blank_color(
		struct output_pixel_processor *opp,
		const struct tg_color *color)
{
	struct dcn20_opp *oppn20 = TO_DCN20_OPP(opp);

	/* 16-bit MSB aligned value. Bits 3:0 of this field are hardwired to ZERO */
	ASSERT(color);
	REG_SET_2(DPG_COLOUR_B_CB, 0,
			DPG_COLOUR1_B_CB, color->color_b_cb << 6,
			DPG_COLOUR0_B_CB, color->color_b_cb << 6);
	REG_SET_2(DPG_COLOUR_G_Y, 0,
			DPG_COLOUR1_G_Y, color->color_g_y << 6,
			DPG_COLOUR0_G_Y, color->color_g_y << 6);
	REG_SET_2(DPG_COLOUR_R_CR, 0,
			DPG_COLOUR1_R_CR, color->color_r_cr << 6,
			DPG_COLOUR0_R_CR, color->color_r_cr << 6);
}

bool opp2_dpg_is_blanked(struct output_pixel_processor *opp)
{
	struct dcn20_opp *oppn20 = TO_DCN20_OPP(opp);
	uint32_t dpg_en, dpg_mode;
	uint32_t double_buffer_pending;

	REG_GET_2(DPG_CONTROL,
			DPG_EN, &dpg_en,
			DPG_MODE, &dpg_mode);

	REG_GET(DPG_STATUS,
			DPG_DOUBLE_BUFFER_PENDING, &double_buffer_pending);

	return (dpg_en == 1) &&
		(double_buffer_pending == 0);
}

void opp2_program_left_edge_extra_pixel (
		struct output_pixel_processor *opp,
		bool count)
{
	struct dcn20_opp *oppn20 = TO_DCN20_OPP(opp);

	/* Specifies the number of extra left edge pixels that are supplied to
	 * the 422 horizontal chroma sub-sample filter.
	 * Note that when left edge pixel is not "0", fmt pixel encoding can be in either 420 or 422 mode
	 * */
	REG_UPDATE(FMT_422_CONTROL, FMT_LEFT_EDGE_EXTRA_PIXEL_COUNT, count);
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

static struct opp_funcs dcn20_opp_funcs = {
		.opp_set_dyn_expansion = opp1_set_dyn_expansion,
		.opp_program_fmt = opp1_program_fmt,
		.opp_program_bit_depth_reduction = opp1_program_bit_depth_reduction,
		.opp_program_stereo = opp1_program_stereo,
		.opp_pipe_clock_control = opp1_pipe_clock_control,
		.opp_set_disp_pattern_generator = opp2_set_disp_pattern_generator,
		.dpg_is_blanked = opp2_dpg_is_blanked,
		.opp_dpg_set_blank_color = opp2_dpg_set_blank_color,
		.opp_destroy = opp1_destroy,
		.opp_program_left_edge_extra_pixel = opp2_program_left_edge_extra_pixel,
};

void dcn20_opp_construct(struct dcn20_opp *oppn20,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn20_opp_registers *regs,
	const struct dcn20_opp_shift *opp_shift,
	const struct dcn20_opp_mask *opp_mask)
{
	oppn20->base.ctx = ctx;
	oppn20->base.inst = inst;
	oppn20->base.funcs = &dcn20_opp_funcs;

	oppn20->regs = regs;
	oppn20->opp_shift = opp_shift;
	oppn20->opp_mask = opp_mask;
}

