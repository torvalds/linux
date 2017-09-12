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

#include "dce/dce_11_2_d.h"
#include "dce/dce_11_2_sh_mask.h"

#include "dce112_opp.h"

#define FMT_REG(reg)\
	(reg + opp110->offsets.fmt_offset)
#define FMT_MEM_REG(reg)\
	(reg + opp110->offsets.fmt_mem_offset)

/**
 *	Set Clamping
 *	1) Set clamping format based on bpc - 0 for 6bpc (No clamping)
 *		1 for 8 bpc
 *		2 for 10 bpc
 *		3 for 12 bpc
 *		7 for programable
 *	2) Enable clamp if Limited range requested
 */

/**
 *	set_pixel_encoding
 *
 *	Set Pixel Encoding
 *		0: RGB 4:4:4 or YCbCr 4:4:4 or YOnly
 *		1: YCbCr 4:2:2
 *		2: YCbCr 4:2:0
 */
static void set_pixel_encoding(
	struct dce110_opp *opp110,
	const struct clamping_and_pixel_encoding_params *params)
{
	uint32_t fmt_cntl_value;
	uint32_t addr = FMT_REG(mmFMT_CONTROL);

	/*RGB 4:4:4 or YCbCr 4:4:4 - 0; YCbCr 4:2:2 -1.*/
	fmt_cntl_value = dm_read_reg(opp110->base.ctx, addr);

	set_reg_field_value(fmt_cntl_value,
		0,
		FMT_CONTROL,
		FMT_PIXEL_ENCODING);

	/*00 - Pixels drop mode HW default*/
	set_reg_field_value(fmt_cntl_value,
		0,
		FMT_CONTROL,
		FMT_SUBSAMPLING_MODE);

	/* By default no bypass*/
	set_reg_field_value(fmt_cntl_value,
		0,
		FMT_CONTROL,
		FMT_CBCR_BIT_REDUCTION_BYPASS);

	if (params->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		set_reg_field_value(fmt_cntl_value,
			1,
			FMT_CONTROL,
			FMT_PIXEL_ENCODING);

		/*00 - Cb before Cr ,01 - Cr before Cb*/
		set_reg_field_value(fmt_cntl_value,
			0,
			FMT_CONTROL,
			FMT_SUBSAMPLING_ORDER);
	}

	if (params->pixel_encoding == PIXEL_ENCODING_YCBCR420) {
		set_reg_field_value(fmt_cntl_value,
			2,
			FMT_CONTROL,
			FMT_PIXEL_ENCODING);

		/* 02 - Subsampling mode, 3 taps*/
		set_reg_field_value(fmt_cntl_value,
			2,
			FMT_CONTROL,
			FMT_SUBSAMPLING_MODE);

		/* 00 - Enable CbCr bit reduction bypass to preserve precision*/
		set_reg_field_value(fmt_cntl_value,
			1,
			FMT_CONTROL,
			FMT_CBCR_BIT_REDUCTION_BYPASS);
	}
	dm_write_reg(opp110->base.ctx, addr, fmt_cntl_value);

}

void dce112_opp_program_clamping_and_pixel_encoding(
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
	uint32_t fmt_cntl_value;
	uint32_t fmt_mem_cntl_value;
	uint32_t fmt_cntl_addr = FMT_REG(mmFMT_CONTROL);
	uint32_t fmt_mem_cntl_addr = FMT_MEM_REG(mmFMT_MEMORY0_CONTROL);

	fmt_mem_cntl_value = dm_read_reg(opp110->base.ctx, fmt_mem_cntl_addr);
	fmt_cntl_value = dm_read_reg(opp110->base.ctx, fmt_cntl_addr);
	/* Program source select*/
	/* Use HW default source select for FMT_MEMORYx_CONTROL */
	/* Use that value for FMT_SRC_SELECT as well*/
	set_reg_field_value(fmt_cntl_value,
		get_reg_field_value(fmt_mem_cntl_value, FMT_MEMORY0_CONTROL, FMT420_MEM0_SOURCE_SEL),
		FMT_CONTROL,
		FMT_SRC_SELECT);
	dm_write_reg(opp110->base.ctx, fmt_cntl_addr, fmt_cntl_value);

	/* Turn on the memory */
	set_reg_field_value(fmt_mem_cntl_value,
		0,
		FMT_MEMORY0_CONTROL,
		FMT420_MEM0_PWR_FORCE);
	dm_write_reg(opp110->base.ctx, fmt_mem_cntl_addr, fmt_mem_cntl_value);
}

static void program_formatter_reset_dig_resync_fifo(struct output_pixel_processor *opp)
{
	struct dce110_opp *opp110 = TO_DCE110_OPP(opp);
	uint32_t value;
	uint32_t addr = FMT_REG(mmFMT_CONTROL);
	uint8_t counter = 10;


	value = dm_read_reg(opp110->base.ctx, addr);

	/* clear previous phase lock status*/
	set_reg_field_value(value,
		1,
		FMT_CONTROL,
		FMT_420_PIXEL_PHASE_LOCKED_CLEAR);
	dm_write_reg(opp110->base.ctx, addr, value);

	/* poll until FMT_420_PIXEL_PHASE_LOCKED become 1*/
	while (counter > 0) {
		value = dm_read_reg(opp110->base.ctx, addr);

		if (get_reg_field_value(
			value,
			FMT_CONTROL,
			FMT_420_PIXEL_PHASE_LOCKED) == 1)
			break;

		msleep(10);
		counter--;
	}

	if (counter == 0)
		dm_logger_write(opp->ctx->logger, LOG_ERROR,
				"%s:opp program formattter reset dig resync info time out.\n",
				__func__);
}

void dce112_opp_program_fmt(
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

	dce112_opp_program_clamping_and_pixel_encoding(
		opp,
		clamping);

	if (clamping->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		program_formatter_reset_dig_resync_fifo(opp);

	return;
}
