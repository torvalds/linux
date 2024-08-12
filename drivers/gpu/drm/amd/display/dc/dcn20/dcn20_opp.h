/* Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_OPP_DCN20_H__
#define __DC_OPP_DCN20_H__

#include "dcn10/dcn10_opp.h"

#define TO_DCN20_OPP(opp)\
	container_of(opp, struct dcn20_opp, base)

#define OPP_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define OPP_DPG_REG_LIST(id) \
	SRI(DPG_CONTROL, DPG, id), \
	SRI(DPG_DIMENSIONS, DPG, id), \
	SRI(DPG_OFFSET_SEGMENT, DPG, id), \
	SRI(DPG_COLOUR_B_CB, DPG, id), \
	SRI(DPG_COLOUR_G_Y, DPG, id), \
	SRI(DPG_COLOUR_R_CR, DPG, id), \
	SRI(DPG_RAMP_CONTROL, DPG, id), \
	SRI(DPG_STATUS, DPG, id)

#define OPP_REG_LIST_DCN20(id) \
	OPP_REG_LIST_DCN10(id), \
	OPP_DPG_REG_LIST(id), \
	SRI(FMT_422_CONTROL, FMT, id), \
	SRI(OPPBUF_CONTROL1, OPPBUF, id)

#define OPP_REG_VARIABLE_LIST_DCN2_0 \
	OPP_COMMON_REG_VARIABLE_LIST; \
	uint32_t FMT_422_CONTROL; \
	uint32_t DPG_CONTROL; \
	uint32_t DPG_DIMENSIONS; \
	uint32_t DPG_OFFSET_SEGMENT; \
	uint32_t DPG_COLOUR_B_CB; \
	uint32_t DPG_COLOUR_G_Y; \
	uint32_t DPG_COLOUR_R_CR; \
	uint32_t DPG_RAMP_CONTROL; \
	uint32_t DPG_STATUS

#define OPP_DPG_MASK_SH_LIST(mask_sh) \
	OPP_SF(DPG0_DPG_CONTROL, DPG_EN, mask_sh), \
	OPP_SF(DPG0_DPG_CONTROL, DPG_MODE, mask_sh), \
	OPP_SF(DPG0_DPG_CONTROL, DPG_DYNAMIC_RANGE, mask_sh), \
	OPP_SF(DPG0_DPG_CONTROL, DPG_BIT_DEPTH, mask_sh), \
	OPP_SF(DPG0_DPG_CONTROL, DPG_VRES, mask_sh), \
	OPP_SF(DPG0_DPG_CONTROL, DPG_HRES, mask_sh), \
	OPP_SF(DPG0_DPG_DIMENSIONS, DPG_ACTIVE_WIDTH, mask_sh), \
	OPP_SF(DPG0_DPG_DIMENSIONS, DPG_ACTIVE_HEIGHT, mask_sh), \
	OPP_SF(DPG0_DPG_OFFSET_SEGMENT, DPG_X_OFFSET, mask_sh), \
	OPP_SF(DPG0_DPG_OFFSET_SEGMENT, DPG_SEGMENT_WIDTH, mask_sh), \
	OPP_SF(DPG0_DPG_COLOUR_R_CR, DPG_COLOUR0_R_CR, mask_sh), \
	OPP_SF(DPG0_DPG_COLOUR_R_CR, DPG_COLOUR1_R_CR, mask_sh), \
	OPP_SF(DPG0_DPG_COLOUR_B_CB, DPG_COLOUR0_B_CB, mask_sh), \
	OPP_SF(DPG0_DPG_COLOUR_B_CB, DPG_COLOUR1_B_CB, mask_sh), \
	OPP_SF(DPG0_DPG_COLOUR_G_Y, DPG_COLOUR0_G_Y, mask_sh), \
	OPP_SF(DPG0_DPG_COLOUR_G_Y, DPG_COLOUR1_G_Y, mask_sh), \
	OPP_SF(DPG0_DPG_RAMP_CONTROL, DPG_RAMP0_OFFSET, mask_sh), \
	OPP_SF(DPG0_DPG_RAMP_CONTROL, DPG_INC0, mask_sh), \
	OPP_SF(DPG0_DPG_RAMP_CONTROL, DPG_INC1, mask_sh), \
	OPP_SF(DPG0_DPG_STATUS, DPG_DOUBLE_BUFFER_PENDING, mask_sh)

#define OPP_MASK_SH_LIST_DCN20(mask_sh) \
	OPP_MASK_SH_LIST_DCN(mask_sh), \
	OPP_DPG_MASK_SH_LIST(mask_sh), \
	OPP_SF(OPPBUF0_OPPBUF_CONTROL, OPPBUF_DISPLAY_SEGMENTATION, mask_sh),\
	OPP_SF(OPPBUF0_OPPBUF_CONTROL, OPPBUF_OVERLAP_PIXEL_NUM, mask_sh), \
	OPP_SF(FMT0_FMT_422_CONTROL, FMT_LEFT_EDGE_EXTRA_PIXEL_COUNT, mask_sh)

#define OPP_DCN20_REG_FIELD_LIST(type) \
	OPP_DCN10_REG_FIELD_LIST(type); \
	type FMT_LEFT_EDGE_EXTRA_PIXEL_COUNT; \
	type DPG_EN; \
	type DPG_MODE; \
	type DPG_DYNAMIC_RANGE; \
	type DPG_BIT_DEPTH; \
	type DPG_VRES; \
	type DPG_HRES; \
	type DPG_ACTIVE_WIDTH; \
	type DPG_ACTIVE_HEIGHT; \
	type DPG_X_OFFSET; \
	type DPG_SEGMENT_WIDTH; \
	type DPG_COLOUR0_R_CR; \
	type DPG_COLOUR1_R_CR; \
	type DPG_COLOUR0_B_CB; \
	type DPG_COLOUR1_B_CB; \
	type DPG_COLOUR0_G_Y; \
	type DPG_COLOUR1_G_Y; \
	type DPG_RAMP0_OFFSET; \
	type DPG_INC0; \
	type DPG_INC1; \
	type DPG_DOUBLE_BUFFER_PENDING

struct dcn20_opp_registers {
	OPP_REG_VARIABLE_LIST_DCN2_0;
};

struct dcn20_opp_shift {
	OPP_DCN20_REG_FIELD_LIST(uint8_t);
};

struct dcn20_opp_mask {
	OPP_DCN20_REG_FIELD_LIST(uint32_t);
};

struct dcn20_opp {
	struct output_pixel_processor base;

	const struct dcn20_opp_registers *regs;
	const struct dcn20_opp_shift *opp_shift;
	const struct dcn20_opp_mask *opp_mask;

	bool is_write_to_ram_a_safe;
};

void dcn20_opp_construct(struct dcn20_opp *oppn20,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn20_opp_registers *regs,
	const struct dcn20_opp_shift *opp_shift,
	const struct dcn20_opp_mask *opp_mask);

void opp2_set_disp_pattern_generator(
	struct output_pixel_processor *opp,
	enum controller_dp_test_pattern test_pattern,
	enum controller_dp_color_space color_space,
	enum dc_color_depth color_depth,
	const struct tg_color *solid_color,
	int width,
	int height,
	int offset);

void opp2_program_dpg_dimensions(
		struct output_pixel_processor *opp,
		int width, int height);

bool opp2_dpg_is_blanked(struct output_pixel_processor *opp);

bool opp2_dpg_is_pending(struct output_pixel_processor *opp);

void opp2_dpg_set_blank_color(
		struct output_pixel_processor *opp,
		const struct tg_color *color);

void opp2_program_left_edge_extra_pixel (
		struct output_pixel_processor *opp,
		enum dc_pixel_encoding pixel_encoding, bool is_primary);

uint32_t opp2_get_left_edge_extra_pixel_count(struct output_pixel_processor *opp,
		enum dc_pixel_encoding pixel_encoding, bool is_primary);
#endif
