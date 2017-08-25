/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include "dcn10_ipp.h"
#include "reg_helper.h"

#define REG(reg) \
	(ippn10->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	ippn10->ipp_shift->field_name, ippn10->ipp_mask->field_name

#define CTX \
	ippn10->base.ctx

static bool ippn10_cursor_program_control(
		struct dcn10_ipp *ippn10,
		bool pixel_data_invert,
		enum dc_cursor_color_format color_format)
{
	if (REG(CURSOR_SETTINS))
		REG_SET_2(CURSOR_SETTINS, 0,
				/* no shift of the cursor HDL schedule */
				CURSOR0_DST_Y_OFFSET, 0,
				 /* used to shift the cursor chunk request deadline */
				CURSOR0_CHUNK_HDL_ADJUST, 3);
	else
		REG_SET_2(CURSOR_SETTINGS, 0,
				/* no shift of the cursor HDL schedule */
				CURSOR0_DST_Y_OFFSET, 0,
				 /* used to shift the cursor chunk request deadline */
				CURSOR0_CHUNK_HDL_ADJUST, 3);

	REG_UPDATE_2(CURSOR0_CONTROL,
			CUR0_MODE, color_format,
			CUR0_EXPANSION_MODE, 0);

	if (color_format == CURSOR_MODE_MONO) {
		/* todo: clarify what to program these to */
		REG_UPDATE(CURSOR0_COLOR0,
				CUR0_COLOR0, 0x00000000);
		REG_UPDATE(CURSOR0_COLOR1,
				CUR0_COLOR1, 0xFFFFFFFF);
	}

	/* TODO: Fixed vs float */

	REG_UPDATE_3(FORMAT_CONTROL,
				CNVC_BYPASS, 0,
				ALPHA_EN, 1,
				FORMAT_EXPANSION_MODE, 0);

	return true;
}

enum cursor_pitch {
	CURSOR_PITCH_64_PIXELS = 0,
	CURSOR_PITCH_128_PIXELS,
	CURSOR_PITCH_256_PIXELS
};

enum cursor_lines_per_chunk {
	CURSOR_LINE_PER_CHUNK_2 = 1,
	CURSOR_LINE_PER_CHUNK_4,
	CURSOR_LINE_PER_CHUNK_8,
	CURSOR_LINE_PER_CHUNK_16
};

static enum cursor_pitch ippn10_get_cursor_pitch(
		unsigned int pitch)
{
	enum cursor_pitch hw_pitch;

	switch (pitch) {
	case 64:
		hw_pitch = CURSOR_PITCH_64_PIXELS;
		break;
	case 128:
		hw_pitch = CURSOR_PITCH_128_PIXELS;
		break;
	case 256:
		hw_pitch = CURSOR_PITCH_256_PIXELS;
		break;
	default:
		DC_ERR("Invalid cursor pitch of %d. "
				"Only 64/128/256 is supported on DCN.\n", pitch);
		hw_pitch = CURSOR_PITCH_64_PIXELS;
		break;
	}
	return hw_pitch;
}

static enum cursor_lines_per_chunk ippn10_get_lines_per_chunk(
		unsigned int cur_width,
		enum dc_cursor_color_format format)
{
	enum cursor_lines_per_chunk line_per_chunk;

	if (format == CURSOR_MODE_MONO)
		/* impl B. expansion in CUR Buffer reader */
		line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
	else if (cur_width <= 32)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_16;
	else if (cur_width <= 64)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_8;
	else if (cur_width <= 128)
		line_per_chunk = CURSOR_LINE_PER_CHUNK_4;
	else
		line_per_chunk = CURSOR_LINE_PER_CHUNK_2;

	return line_per_chunk;
}

static void ippn10_cursor_set_attributes(
		struct input_pixel_processor *ipp,
		const struct dc_cursor_attributes *attr)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
	enum cursor_pitch hw_pitch = ippn10_get_cursor_pitch(attr->pitch);
	enum cursor_lines_per_chunk lpc = ippn10_get_lines_per_chunk(
			attr->width, attr->color_format);

	ippn10->curs_attr = *attr;

	REG_UPDATE(CURSOR_SURFACE_ADDRESS_HIGH,
			CURSOR_SURFACE_ADDRESS_HIGH, attr->address.high_part);
	REG_UPDATE(CURSOR_SURFACE_ADDRESS,
			CURSOR_SURFACE_ADDRESS, attr->address.low_part);

	REG_UPDATE_2(CURSOR_SIZE,
			CURSOR_WIDTH, attr->width,
			CURSOR_HEIGHT, attr->height);

	REG_UPDATE_3(CURSOR_CONTROL,
			CURSOR_MODE, attr->color_format,
			CURSOR_PITCH, hw_pitch,
			CURSOR_LINES_PER_CHUNK, lpc);

	ippn10_cursor_program_control(ippn10,
			attr->attribute_flags.bits.INVERT_PIXEL_DATA,
			attr->color_format);
}

static void ippn10_cursor_set_position(
		struct input_pixel_processor *ipp,
		const struct dc_cursor_position *pos,
		const struct dc_cursor_mi_param *param)
{
	struct dcn10_ipp *ippn10 = TO_DCN10_IPP(ipp);
	int src_x_offset = pos->x - pos->x_hotspot - param->viewport_x_start;
	uint32_t cur_en = pos->enable ? 1 : 0;
	uint32_t dst_x_offset = (src_x_offset >= 0) ? src_x_offset : 0;

	/*
	 * Guard aganst cursor_set_position() from being called with invalid
	 * attributes
	 *
	 * TODO: Look at combining cursor_set_position() and
	 * cursor_set_attributes() into cursor_update()
	 */
	if (ippn10->curs_attr.address.quad_part == 0)
		return;

	dst_x_offset *= param->ref_clk_khz;
	dst_x_offset /= param->pixel_clk_khz;

	ASSERT(param->h_scale_ratio.value);

	if (param->h_scale_ratio.value)
		dst_x_offset = dal_fixed31_32_floor(dal_fixed31_32_div(
				dal_fixed31_32_from_int(dst_x_offset),
				param->h_scale_ratio));

	if (src_x_offset >= (int)param->viewport_width)
		cur_en = 0;  /* not visible beyond right edge*/

	if (src_x_offset + (int)ippn10->curs_attr.width < 0)
		cur_en = 0;  /* not visible beyond left edge*/

	if (cur_en && REG_READ(CURSOR_SURFACE_ADDRESS) == 0)
		ippn10_cursor_set_attributes(ipp, &ippn10->curs_attr);
	REG_UPDATE(CURSOR_CONTROL,
			CURSOR_ENABLE, cur_en);
	REG_UPDATE(CURSOR0_CONTROL,
			CUR0_ENABLE, cur_en);

	REG_SET_2(CURSOR_POSITION, 0,
			CURSOR_X_POSITION, pos->x,
			CURSOR_Y_POSITION, pos->y);

	REG_SET_2(CURSOR_HOT_SPOT, 0,
			CURSOR_HOT_SPOT_X, pos->x_hotspot,
			CURSOR_HOT_SPOT_Y, pos->y_hotspot);

	REG_SET(CURSOR_DST_OFFSET, 0,
			CURSOR_DST_X_OFFSET, dst_x_offset);
	/* TODO Handle surface pixel formats other than 4:4:4 */
}

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

static void dcn10_ipp_destroy(struct input_pixel_processor **ipp)
{
	dm_free(TO_DCN10_IPP(*ipp));
	*ipp = NULL;
}

static const struct ipp_funcs dcn10_ipp_funcs = {
	.ipp_cursor_set_attributes	= ippn10_cursor_set_attributes,
	.ipp_cursor_set_position	= ippn10_cursor_set_position,
	.ipp_set_degamma		= NULL,
	.ipp_program_input_lut		= NULL,
	.ipp_full_bypass		= NULL,
	.ipp_setup			= NULL,
	.ipp_program_degamma_pwl	= NULL,
	.ipp_destroy			= dcn10_ipp_destroy
};

void dcn10_ipp_construct(
	struct dcn10_ipp *ippn10,
	struct dc_context *ctx,
	int inst,
	const struct dcn10_ipp_registers *regs,
	const struct dcn10_ipp_shift *ipp_shift,
	const struct dcn10_ipp_mask *ipp_mask)
{
	ippn10->base.ctx = ctx;
	ippn10->base.inst = inst;
	ippn10->base.funcs = &dcn10_ipp_funcs;

	ippn10->regs = regs;
	ippn10->ipp_shift = ipp_shift;
	ippn10->ipp_mask = ipp_mask;
}

