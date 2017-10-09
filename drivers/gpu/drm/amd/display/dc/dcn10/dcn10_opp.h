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
	SRI(FMT_MAP420_MEMORY_CONTROL, FMT, id)

#define OPP_REG_LIST_DCN10(id) \
	OPP_REG_LIST_DCN(id)

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
	OPP_SF(FMT0_FMT_MAP420_MEMORY_CONTROL, FMT_MAP420MEM_PWR_FORCE, mask_sh)

#define OPP_MASK_SH_LIST_DCN10(mask_sh) \
	OPP_MASK_SH_LIST_DCN(mask_sh)

#define OPP_DCN10_REG_FIELD_LIST(type) \
	type DPG_EN; \
	type DPG_MODE; \
	type DPG_VRES; \
	type DPG_HRES; \
	type DPG_COLOUR0_R_CR; \
	type DPG_COLOUR1_R_CR; \
	type DPG_COLOUR0_B_CB; \
	type DPG_COLOUR1_B_CB; \
	type DPG_COLOUR0_G_Y; \
	type DPG_COLOUR1_G_Y; \
	type CM_OCSC_C11; \
	type CM_OCSC_C12; \
	type CM_OCSC_C13; \
	type CM_OCSC_C14; \
	type CM_OCSC_C21; \
	type CM_OCSC_C22; \
	type CM_OCSC_C23; \
	type CM_OCSC_C24; \
	type CM_OCSC_C31; \
	type CM_OCSC_C32; \
	type CM_OCSC_C33; \
	type CM_OCSC_C34; \
	type CM_COMB_C11; \
	type CM_COMB_C12; \
	type CM_COMB_C13; \
	type CM_COMB_C14; \
	type CM_COMB_C21; \
	type CM_COMB_C22; \
	type CM_COMB_C23; \
	type CM_COMB_C24; \
	type CM_COMB_C31; \
	type CM_COMB_C32; \
	type CM_COMB_C33; \
	type CM_COMB_C34; \
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
	type FMT_STEREOSYNC_OVERRIDE

struct dcn10_opp_shift {
	OPP_DCN10_REG_FIELD_LIST(uint8_t);
};

struct dcn10_opp_mask {
	OPP_DCN10_REG_FIELD_LIST(uint32_t);
};

struct dcn10_opp_registers {
	uint32_t DPG_CONTROL;
	uint32_t DPG_COLOUR_B_CB;
	uint32_t DPG_COLOUR_G_Y;
	uint32_t DPG_COLOUR_R_CR;
	uint32_t CM_OCSC_C11_C12;
	uint32_t CM_OCSC_C13_C14;
	uint32_t CM_OCSC_C21_C22;
	uint32_t CM_OCSC_C23_C24;
	uint32_t CM_OCSC_C31_C32;
	uint32_t CM_OCSC_C33_C34;
	uint32_t CM_COMB_C11_C12;
	uint32_t CM_COMB_C13_C14;
	uint32_t CM_COMB_C21_C22;
	uint32_t CM_COMB_C23_C24;
	uint32_t CM_COMB_C31_C32;
	uint32_t CM_COMB_C33_C34;
	uint32_t FMT_BIT_DEPTH_CONTROL;
	uint32_t FMT_CONTROL;
	uint32_t FMT_DITHER_RAND_R_SEED;
	uint32_t FMT_DITHER_RAND_G_SEED;
	uint32_t FMT_DITHER_RAND_B_SEED;
	uint32_t FMT_CLAMP_CNTL;
	uint32_t FMT_DYNAMIC_EXP_CNTL;
	uint32_t FMT_MAP420_MEMORY_CONTROL;
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

#endif
