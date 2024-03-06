/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef __DC_HUBP_DCN35_H__
#define __DC_HUBP_DCN35_H__

#include "dcn31/dcn31_hubp.h"
#include "dcn32/dcn32_hubp.h"
#define HUBP_MASK_SH_LIST_DCN35(mask_sh)\
	HUBP_MASK_SH_LIST_DCN32(mask_sh),\
	HUBP_SF(HUBP0_HUBP_CLK_CNTL, HUBP_FGCG_REP_DIS, mask_sh)

#define DCN35_HUBP_REG_FIELD_VARIABLE_LIST(type)          \
	struct {                                          \
		DCN32_HUBP_REG_FIELD_VARIABLE_LIST(type); \
		type HUBP_FGCG_REP_DIS;                   \
	}

struct dcn35_hubp2_shift {
	DCN35_HUBP_REG_FIELD_VARIABLE_LIST(uint8_t);
};

struct dcn35_hubp2_mask {
	DCN35_HUBP_REG_FIELD_VARIABLE_LIST(uint32_t);
};


bool hubp35_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn35_hubp2_shift *hubp_shift,
	const struct dcn35_hubp2_mask *hubp_mask);

void hubp35_set_fgcg(struct hubp *hubp, bool enable);

void hubp35_program_pixel_format(
	struct hubp *hubp,
	enum surface_pixel_format format);

void hubp35_program_surface_config(
	struct hubp *hubp,
	enum surface_pixel_format format,
	union dc_tiling_info *tiling_info,
	struct plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror,
	unsigned int compat_level);

#endif /* __DC_HUBP_DCN35_H__ */
