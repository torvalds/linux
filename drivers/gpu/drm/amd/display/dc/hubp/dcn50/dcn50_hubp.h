/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __DC_HUBP_DCN50_H__
#define __DC_HUBP_DCN50_H__

#include "dcn20/dcn20_hubp.h"
#include "dcn21/dcn21_hubp.h"
#include "dcn30/dcn30_hubp.h"
#include "dcn31/dcn31_hubp.h"
#include "dcn32/dcn32_hubp.h"
#include "dcn401/dcn401_hubp.h"
#include "dml2_0/dml21/inc/dml_top_dchub_registers.h"

#define HUBP_MASK_SH_LIST_DCN50(mask_sh)\
	HUBP_MASK_SH_LIST_DCN401(mask_sh)

bool hubp50_program_surface_flip_and_addr(
	struct hubp *hubp,
	const struct dc_plane_address *address,
	bool flip_immediate);

void hubp50_program_surface_config(
	struct hubp *hubp,
	enum surface_pixel_format format,
	struct dc_tiling_info *tiling_info,
	struct plane_size *plane_size,
	enum dc_rotation_angle rotation,
	struct dc_plane_dcc_param *dcc,
	bool horizontal_mirror,
	unsigned int compat_level);


void hubp50_read_state(struct hubp *hubp);


bool hubp50_construct(
	struct dcn20_hubp *hubp2,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dcn_hubp2_registers *hubp_regs,
	const struct dcn_hubp2_shift *hubp_shift,
	const struct dcn_hubp2_mask *hubp_mask);

#endif /* __DC_HUBP_DCN50_H__ */
