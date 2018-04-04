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

#ifndef __DC_OPP_DCN10_H__
#define __DC_OPP_DCN10_H__

#include "opp.h"

#define TO_DCN10_OPP(opp)\
	container_of(opp, struct dcn10_opp, base)

#define OPP_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define OPP_REG_LIST_DCN(id) \
	SRI(FMT_BIT_DEPTH_CONTROL, FMT, id), \
	SRI(FMT_CONTROL, FMT, id), \
	SRI(FMT_DITHER_RAND_R_SEED, FMT, id), \
	SRI(FMT_DITHER_RAND_G_SEED, FMT, id), \
	SRI(FMT_DITHER_RAND_B_SEED, FMT, id), \
	SRI(FMT_CLAMP_CNTL, FMT, id), \
	SRI(FMT_DYNAMIC_EXP_CNTL, FMT, id), \
	SRI(FMT_MAP420_MEMORY_CONTROL, FMT, id), \
	SRI(OPPBUF_CONTROL, OPPBUF, id),\
	SRI(OPPBUF_3D_PARAMETERS_0, OPPBUF, id), \
	SRI(OPPBUF_3D_PARAMETERS_1, OPPBUF, id)

#define OPP_REG_LIST_DCN10(id) \
	OPP_REG_LIST_DCN(id)

#define OPP_COMMON_REG_VARIABLE_LIST \
	uint32_t FMT_BIT_DEPTH_CONTROL; \
	uint32_t FMT_CONTROL; \
	uint32_t FMT_DITHER_RAND_R_SEED; \
	uint32_t FMT_DITHER_RAND_G_SEED; \
	uint32_t FMT_DITHER_RAND_B_SEED; \
	uint32_t FMT_CLAMP_CNTL; \
	uint32_t FMT_DYNAMIC_EXP_CNTL; \
	uint32_t FMT_MAP420_MEMORY_CONTROL; \
	uint32_t OPPBUF_CONTROL; \
	uint32_t OPPBUF_CONTROL1; \
	uint32_t OPPBUF_3D_PARAMETERS_0; \
	uint32_t OPPBUF_3D_PARAMETERS_1

#define OPP_MASK_SH_LIST_DCN(mask_sh) \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_EN, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_DEPTH, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_TRUNCATE_MODE, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_EN, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_MODE, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_SPATIAL_DITHER_DEPTH, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_TEMPORAL_DITHER_EN, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_HIGHPASS_RANDOM_ENABLE, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_FRAME_RANDOM_ENABLE, mask_sh), \
	OPP_SF(FMT0_FMT_BIT_DEPTH_CONTROL, FMT_RGB_RANDOM_ENABLE, mask_sh), \
	OPP_SF(FMT0_FMT_CONTROL, FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX, mask_sh), \
	OPP_SF(FMT0_FMT_CONTROL, FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP, mask_sh), \
	OPP_SF(FMT0_FMT_CONTROL, FMT_PIXEL_ENCODING, mask_sh), \
	OPP_SF(FMT0_FMT_CONTROL, FMT_STEREOSYNC_OVERRIDE, mask_sh), \
	OPP_SF(FMT0_FMT_DITHER_RAND_R_SEED, FMT_RAND_R_SEED, mask_sh), \
	OPP_SF(FMT0_FMT_DITHER_RAND_G_SEED, FMT_RAND_G_SEED, mask_sh), \
	OPP_SF(FMT0_FMT_DITHER_RAND_B_SEED, FMT_RAND_B_SEED, mask_sh), \
	OPP_SF(FMT0_FMT_CLAMP_CNTL, FMT_CLAMP_DATA_EN, mask_sh), \
	OPP_SF(FMT0_FMT_CLAMP_CNTL, FMT_CLAMP_COLOR_FORMAT, mask_sh), \
	OPP_SF(FMT0_FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_EN, mask_sh), \
	OPP_SF(FMT0_FMT_DYNAMIC_EXP_CNTL, FMT_DYNAMIC_EXP_MODE, mask_sh), \
	OPP_SF(FMT0_FMT_MAP420_MEMORY_CONTROL, FMT_MAP420MEM_PWR_FORCE, mask_sh), \
	OPP_SF(OPPBUF0_OPPBUF_CONTROL, OPPBUF_ACTIVE_WIDTH, mask_sh),\
	OPP_SF(OPPBUF0_OPPBUF_CONTROL, OPPBUF_PIXEL_REPETITION, mask_sh),\
	OPP_SF(OPPBUF0_OPPBUF_3D_PARAMETERS_0, OPPBUF_3D_VACT_SPACE1_SIZE, mask_sh), \
	OPP_SF(OPPBUF0_OPPBUF_3D_PARAMETERS_0, OPPBUF_3D_VACT_SPACE2_SIZE, mask_sh)

#define OPP_MASK_SH_LIST_DCN10(mask_sh) \
	OPP_MASK_SH_LIST_DCN(mask_sh), \
	OPP_SF(OPPBUF0_OPPBUF_CONTROL, OPPBUF_DISPLAY_SEGMENTATION, mask_sh),\
	OPP_SF(OPPBUF0_OPPBUF_CONTROL, OPPBUF_OVERLAP_PIXEL_NUM, mask_sh)

#define OPP_DCN10_REG_FIELD_LIST(type) \
	type FMT_TRUNCATE_EN; \
	type FMT_TRUNCATE_DEPTH; \
	type FMT_TRUNCATE_MODE; \
	type FMT_SPATIAL_DITHER_EN; \
	type FMT_SPATIAL_DITHER_MODE; \
	type FMT_SPATIAL_DITHER_DEPTH; \
	type FMT_TEMPORAL_DITHER_EN; \
	type FMT_HIGHPASS_RANDOM_ENABLE; \
	type FMT_FRAME_RANDOM_ENABLE; \
	type FMT_RGB_RANDOM_ENABLE; \
	type FMT_SPATIAL_DITHER_FRAME_COUNTER_MAX; \
	type FMT_SPATIAL_DITHER_FRAME_COUNTER_BIT_SWAP; \
	type FMT_RAND_R_SEED; \
	type FMT_RAND_G_SEED; \
	type FMT_RAND_B_SEED; \
	type FMT_PIXEL_ENCODING; \
	type FMT_CLAMP_DATA_EN; \
	type FMT_CLAMP_COLOR_FORMAT; \
	type FMT_DYNAMIC_EXP_EN; \
	type FMT_DYNAMIC_EXP_MODE; \
	type FMT_MAP420MEM_PWR_FORCE; \
	type FMT_STEREOSYNC_OVERRIDE; \
	type OPPBUF_ACTIVE_WIDTH;\
	type OPPBUF_PIXEL_REPETITION;\
	type OPPBUF_DISPLAY_SEGMENTATION;\
	type OPPBUF_OVERLAP_PIXEL_NUM;\
	type OPPBUF_NUM_SEGMENT_PADDED_PIXELS; \
	type OPPBUF_3D_VACT_SPACE1_SIZE; \
	type OPPBUF_3D_VACT_SPACE2_SIZE

struct dcn10_opp_registers {
	OPP_COMMON_REG_VARIABLE_LIST;
};

struct dcn10_opp_shift {
	OPP_DCN10_REG_FIELD_LIST(uint8_t);
};

struct dcn10_opp_mask {
	OPP_DCN10_REG_FIELD_LIST(uint32_t);
};

struct dcn10_opp {
	struct output_pixel_processor base;

	const struct dcn10_opp_registers *regs;
	const struct dcn10_opp_shift *opp_shift;
	const struct dcn10_opp_mask *opp_mask;

	bool is_write_to_ram_a_safe;
};

void dcn10_opp_construct(struct dcn10_opp *oppn10,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn10_opp_registers *regs,
	const struct dcn10_opp_shift *opp_shift,
	const struct dcn10_opp_mask *opp_mask);

void opp1_set_dyn_expansion(
	struct output_pixel_processor *opp,
	enum dc_color_space color_sp,
	enum dc_color_depth color_dpth,
	enum signal_type signal);

void opp1_program_fmt(
	struct output_pixel_processor *opp,
	struct bit_depth_reduction_params *fmt_bit_depth,
	struct clamping_and_pixel_encoding_params *clamping);

void opp1_program_bit_depth_reduction(
	struct output_pixel_processor *opp,
	const struct bit_depth_reduction_params *params);

void opp1_program_stereo(
	struct output_pixel_processor *opp,
	bool enable,
	const struct dc_crtc_timing *timing);

void opp1_destroy(struct output_pixel_processor **opp);

#endif
