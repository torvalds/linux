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

#include "dce/dce_8_0_d.h"
#include "dce/dce_8_0_sh_mask.h"
/* TODO: this needs to be looked at, used by Stella's workaround*/
#include "gmc/gmc_7_1_d.h"
#include "gmc/gmc_7_1_sh_mask.h"

#include "include/logger_interface.h"
#include "inc/dce_calcs.h"

#include "../dce110/dce110_mem_input.h"
#include "dce80_mem_input.h"

#define MAX_WATERMARK 0xFFFF
#define SAFE_NBP_MARK 0x7FFF

#define DCP_REG(reg) (reg + mem_input80->offsets.dcp)
#define DMIF_REG(reg) (reg + mem_input80->offsets.dmif)
#define PIPE_REG(reg) (reg + mem_input80->offsets.pipe)

static struct mem_input_funcs dce80_mem_input_funcs = {
	.mem_input_program_display_marks =
			dce110_mem_input_program_display_marks,
	.allocate_mem_input = dce_mem_input_allocate_dmif,
	.free_mem_input = dce_mem_input_free_dmif,
	.mem_input_program_surface_flip_and_addr =
			dce110_mem_input_program_surface_flip_and_addr,
	.mem_input_program_surface_config =
			dce_mem_input_program_surface_config,
	.mem_input_is_flip_pending =
			dce110_mem_input_is_flip_pending,
	.mem_input_update_dchub = NULL
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce80_mem_input_construct(
	struct dce110_mem_input *mem_input80,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offsets)
{
	/* supported stutter method
	 * STUTTER_MODE_ENHANCED
	 * STUTTER_MODE_QUAD_DMIF_BUFFER
	 */
	mem_input80->base.funcs = &dce80_mem_input_funcs;
	mem_input80->base.ctx = ctx;

	mem_input80->base.inst = inst;

	mem_input80->offsets = *offsets;

	return true;
}

