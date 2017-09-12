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
#include "dce110_opp.h"
#include "basics/conversion.h"

/* include DCE11 register header files */
#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"

#define DCP_REG(reg)\
	(reg + opp110->offsets.dcp_offset)

enum {
	OUTPUT_CSC_MATRIX_SIZE = 12
};

static const struct out_csc_color_matrix global_color_matrix[] = {
{ COLOR_SPACE_SRGB,
	{ 0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0} },
{ COLOR_SPACE_SRGB_LIMITED,
	{ 0x1B60, 0, 0, 0x200, 0, 0x1B60, 0, 0x200, 0, 0, 0x1B60, 0x200} },
{ COLOR_SPACE_YCBCR601,
	{ 0xE00, 0xF447, 0xFDB9, 0x1000, 0x82F, 0x1012, 0x31F, 0x200, 0xFB47,
		0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x5D2, 0x1394, 0x1FA,
	0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} },
/* TODO: correct values below */
{ COLOR_SPACE_YCBCR601_LIMITED, { 0xE00, 0xF447, 0xFDB9, 0x1000, 0x991,
	0x12C9, 0x3A6, 0x200, 0xFB47, 0xF6B9, 0xE00, 0x1000} },
{ COLOR_SPACE_YCBCR709_LIMITED, { 0xE00, 0xF349, 0xFEB7, 0x1000, 0x6CE, 0x16E3,
	0x24F, 0x200, 0xFCCB, 0xF535, 0xE00, 0x1000} }
};

enum csc_color_mode {
	/* 00 - BITS2:0 Bypass */
	CSC_COLOR_MODE_GRAPHICS_BYPASS,
	/* 01 - hard coded coefficient TV RGB */
	CSC_COLOR_MODE_GRAPHICS_PREDEFINED,
	/* 04 - programmable OUTPUT CSC coefficient */
	CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC,
};

static void program_color_matrix(
	struct dce110_opp *opp110,
	const struct out_csc_color_matrix *tbl_entry,
	enum grph_color_adjust_option options)
{
	struct dc_context *ctx = opp110->base.ctx;
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C11_C12);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[0],
			OUTPUT_CSC_C11_C12,
			OUTPUT_CSC_C11);

		set_reg_field_value(
			value,
			tbl_entry->regval[1],
			OUTPUT_CSC_C11_C12,
			OUTPUT_CSC_C12);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C13_C14);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[2],
			OUTPUT_CSC_C13_C14,
			OUTPUT_CSC_C13);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[3],
			OUTPUT_CSC_C13_C14,
			OUTPUT_CSC_C14);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C21_C22);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[4],
			OUTPUT_CSC_C21_C22,
			OUTPUT_CSC_C21);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[5],
			OUTPUT_CSC_C21_C22,
			OUTPUT_CSC_C22);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C23_C24);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[6],
			OUTPUT_CSC_C23_C24,
			OUTPUT_CSC_C23);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[7],
			OUTPUT_CSC_C23_C24,
			OUTPUT_CSC_C24);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C31_C32);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[8],
			OUTPUT_CSC_C31_C32,
			OUTPUT_CSC_C31);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[9],
			OUTPUT_CSC_C31_C32,
			OUTPUT_CSC_C32);

		dm_write_reg(ctx, addr, value);
	}
	{
		uint32_t value = 0;
		uint32_t addr = DCP_REG(mmOUTPUT_CSC_C33_C34);
		/* fixed S2.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[10],
			OUTPUT_CSC_C33_C34,
			OUTPUT_CSC_C33);
		/* fixed S0.13 format */
		set_reg_field_value(
			value,
			tbl_entry->regval[11],
			OUTPUT_CSC_C33_C34,
			OUTPUT_CSC_C34);

		dm_write_reg(ctx, addr, value);
	}
}

static bool configure_graphics_mode(
	struct dce110_opp *opp110,
	enum csc_color_mode config,
	enum graphics_csc_adjust_type csc_adjust_type,
	enum dc_color_space color_space)
{
	struct dc_context *ctx = opp110->base.ctx;
	uint32_t addr = DCP_REG(mmOUTPUT_CSC_CONTROL);
	uint32_t value = dm_read_reg(ctx, addr);

	set_reg_field_value(
		value,
		0,
		OUTPUT_CSC_CONTROL,
		OUTPUT_CSC_GRPH_MODE);

	if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_SW) {
		if (config == CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC) {
			set_reg_field_value(
				value,
				4,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
		} else {

			switch (color_space) {
			case COLOR_SPACE_SRGB:
				/* by pass */
				set_reg_field_value(
					value,
					0,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_SRGB_LIMITED:
				/* TV RGB */
				set_reg_field_value(
					value,
					1,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_YCBCR601:
			case COLOR_SPACE_YPBPR601:
			case COLOR_SPACE_YCBCR601_LIMITED:
				/* YCbCr601 */
				set_reg_field_value(
					value,
					2,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			case COLOR_SPACE_YCBCR709:
			case COLOR_SPACE_YPBPR709:
			case COLOR_SPACE_YCBCR709_LIMITED:
				/* YCbCr709 */
				set_reg_field_value(
					value,
					3,
					OUTPUT_CSC_CONTROL,
					OUTPUT_CSC_GRPH_MODE);
				break;
			default:
				return false;
			}
		}
	} else if (csc_adjust_type == GRAPHICS_CSC_ADJUST_TYPE_HW) {
		switch (color_space) {
		case COLOR_SPACE_SRGB:
			/* by pass */
			set_reg_field_value(
				value,
				0,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_SRGB_LIMITED:
			/* TV RGB */
			set_reg_field_value(
				value,
				1,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_YCBCR601:
		case COLOR_SPACE_YPBPR601:
		case COLOR_SPACE_YCBCR601_LIMITED:
			/* YCbCr601 */
			set_reg_field_value(
				value,
				2,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		case COLOR_SPACE_YCBCR709:
		case COLOR_SPACE_YPBPR709:
		case COLOR_SPACE_YCBCR709_LIMITED:
			 /* YCbCr709 */
			set_reg_field_value(
				value,
				3,
				OUTPUT_CSC_CONTROL,
				OUTPUT_CSC_GRPH_MODE);
			break;
		default:
			return false;
		}

	} else
		/* by pass */
		set_reg_field_value(
			value,
			0,
			OUTPUT_CSC_CONTROL,
			OUTPUT_CSC_GRPH_MODE);

	addr = DCP_REG(mmOUTPUT_CSC_CONTROL);
	dm_write_reg(ctx, addr, value);

	return true;
}

void dce110_opp_set_csc_adjustment(
	struct output_pixel_processor *opp,
	const struct out_csc_color_matrix *tbl_entry)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);
	enum csc_color_mode config =
			CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;

	program_color_matrix(
			opp110, tbl_entry, GRAPHICS_CSC_ADJUST_TYPE_SW);

	/*  We did everything ,now program DxOUTPUT_CSC_CONTROL */
	configure_graphics_mode(opp110, config, GRAPHICS_CSC_ADJUST_TYPE_SW,
			tbl_entry->color_space);
}

void dce110_opp_set_csc_default(
	struct output_pixel_processor *opp,
	const struct default_adjustment *default_adjust)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);
	enum csc_color_mode config =
			CSC_COLOR_MODE_GRAPHICS_PREDEFINED;

	if (default_adjust->force_hw_default == false) {
		const struct out_csc_color_matrix *elm;
		/* currently parameter not in use */
		enum grph_color_adjust_option option =
			GRPH_COLOR_MATRIX_HW_DEFAULT;
		uint32_t i;
		/*
		 * HW default false we program locally defined matrix
		 * HW default true  we use predefined hw matrix and we
		 * do not need to program matrix
		 * OEM wants the HW default via runtime parameter.
		 */
		option = GRPH_COLOR_MATRIX_SW;

		for (i = 0; i < ARRAY_SIZE(global_color_matrix); ++i) {
			elm = &global_color_matrix[i];
			if (elm->color_space != default_adjust->out_color_space)
				continue;
			/* program the matrix with default values from this
			 * file */
			program_color_matrix(opp110, elm, option);
			config = CSC_COLOR_MODE_GRAPHICS_OUTPUT_CSC;
			break;
		}
	}

	/* configure the what we programmed :
	 * 1. Default values from this file
	 * 2. Use hardware default from ROM_A and we do not need to program
	 * matrix */

	configure_graphics_mode(opp110, config,
		default_adjust->csc_adjust_type,
		default_adjust->out_color_space);
}
