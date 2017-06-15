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

#include "dce/dce_11_0_d.h"
#include "dce/dce_11_0_sh_mask.h"
/* TODO: this needs to be looked at, used by Stella's workaround*/
#include "gmc/gmc_8_2_d.h"
#include "gmc/gmc_8_2_sh_mask.h"

#include "include/logger_interface.h"

#include "dce110_mem_input.h"

#define DCP_REG(reg) (reg + mem_input110->offsets.dcp)
#define DMIF_REG(reg) (reg + mem_input110->offsets.dmif)
#define PIPE_REG(reg) (reg + mem_input110->offsets.pipe)

static void program_sec_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp;

	/*high register MUST be programmed first*/
	temp = address.high_part &
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH__GRPH_SECONDARY_SURFACE_ADDRESS_HIGH_MASK;
	set_reg_field_value(value, temp,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_SECONDARY_SURFACE_ADDRESS_HIGH), value);

	value = 0;
	temp = address.low_part >>
		GRPH_SECONDARY_SURFACE_ADDRESS__GRPH_SECONDARY_SURFACE_ADDRESS__SHIFT;
	set_reg_field_value(value, temp,
		GRPH_SECONDARY_SURFACE_ADDRESS,
		GRPH_SECONDARY_SURFACE_ADDRESS);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_SECONDARY_SURFACE_ADDRESS), value);
}

static void program_pri_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t value = 0;
	uint32_t temp;

	/*high register MUST be programmed first*/
	temp = address.high_part &
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_MASK;
	set_reg_field_value(value, temp,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_PRIMARY_SURFACE_ADDRESS_HIGH), value);

	value = 0;
	temp = address.low_part >>
		GRPH_PRIMARY_SURFACE_ADDRESS__GRPH_PRIMARY_SURFACE_ADDRESS__SHIFT;
	set_reg_field_value(value, temp,
		GRPH_PRIMARY_SURFACE_ADDRESS,
		GRPH_PRIMARY_SURFACE_ADDRESS);
	dm_write_reg(mem_input110->base.ctx,
				 DCP_REG(mmGRPH_PRIMARY_SURFACE_ADDRESS), value);
}

bool dce110_mem_input_is_flip_pending(struct mem_input *mem_input)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);
	uint32_t value;

	value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_UPDATE));

	if (get_reg_field_value(value, GRPH_UPDATE,
			GRPH_SURFACE_UPDATE_PENDING))
		return true;

	mem_input->current_address = mem_input->request_address;
	return false;
}

bool dce110_mem_input_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);

	uint32_t value = 0;
	uint32_t value_old = 0;
	uint32_t lock_value = 0;

	lock_value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_UPDATE));
	set_reg_field_value(lock_value, 1, GRPH_UPDATE, GRPH_UPDATE_LOCK);
	dm_write_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_UPDATE), lock_value);

	value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_FLIP_CONTROL));
	value_old = value;
	if (flip_immediate) {
		set_reg_field_value(value, 0, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_IMMEDIATE_EN);
		set_reg_field_value(value, 1, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_H_RETRACE_EN);
	} else {
		set_reg_field_value(value, 0, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_IMMEDIATE_EN);
		set_reg_field_value(value, 0, GRPH_FLIP_CONTROL, GRPH_SURFACE_UPDATE_H_RETRACE_EN);
	}
	if (value != value_old) {
		dm_write_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_FLIP_CONTROL), value);
	}

	switch (address->type) {
	case PLN_ADDR_TYPE_GRAPHICS:
		if (address->grph.addr.quad_part == 0)
			break;
		program_pri_addr(mem_input110, address->grph.addr);
		break;
	case PLN_ADDR_TYPE_GRPH_STEREO:
		if (address->grph_stereo.left_addr.quad_part == 0
			|| address->grph_stereo.right_addr.quad_part == 0)
			break;
		program_pri_addr(mem_input110, address->grph_stereo.left_addr);
		program_sec_addr(mem_input110, address->grph_stereo.right_addr);
		break;
	default:
		/* not supported */
		BREAK_TO_DEBUGGER();
		break;
	}

	mem_input->request_address = *address;
	if (flip_immediate)
		mem_input->current_address = *address;

	lock_value = dm_read_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_UPDATE));
	set_reg_field_value(lock_value, 0, GRPH_UPDATE, GRPH_UPDATE_LOCK);
	dm_write_reg(mem_input110->base.ctx, DCP_REG(mmGRPH_UPDATE), lock_value);

	return true;
}

static void program_urgency_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks_low,
	uint32_t total_dest_line_time_ns)
{
	/* register value */
	uint32_t urgency_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t urgency_addr = offset + mmDPG_PIPE_URGENCY_CONTROL;
	uint32_t wm_addr = offset + mmDPG_WATERMARK_MASK_CONTROL;

	/*Write mask to enable reading/writing of watermark set A*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			1,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(
		urgency_cntl,
		marks_low.d_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(
		urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
			2,
			DPG_WATERMARK_MASK_CONTROL,
			URGENCY_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	urgency_cntl = dm_read_reg(ctx, urgency_addr);

	set_reg_field_value(urgency_cntl,
		marks_low.a_mark,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_LOW_WATERMARK);

	set_reg_field_value(urgency_cntl,
		total_dest_line_time_ns,
		DPG_PIPE_URGENCY_CONTROL,
		URGENCY_HIGH_WATERMARK);
	dm_write_reg(ctx, urgency_addr, urgency_cntl);
}

static void program_stutter_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks)
{
	/* register value */
	uint32_t stutter_cntl = 0;
	uint32_t wm_mask_cntl = 0;

	uint32_t stutter_addr = offset + mmDPG_PIPE_STUTTER_CONTROL;
	uint32_t wm_addr = offset + mmDPG_WATERMARK_MASK_CONTROL;

	/*Write mask to enable reading/writing of watermark set A*/

	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);

	if (ctx->dc->debug.disable_stutter) {
		set_reg_field_value(stutter_cntl,
			0,
			DPG_PIPE_STUTTER_CONTROL,
			STUTTER_ENABLE);
	} else {
		set_reg_field_value(stutter_cntl,
			1,
			DPG_PIPE_STUTTER_CONTROL,
			STUTTER_ENABLE);
	}

	set_reg_field_value(stutter_cntl,
		1,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_IGNORE_FBC);

	/*Write watermark set A*/
	set_reg_field_value(stutter_cntl,
		marks.d_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);

	/*Write mask to enable reading/writing of watermark set B*/
	wm_mask_cntl = dm_read_reg(ctx, wm_addr);
	set_reg_field_value(wm_mask_cntl,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK_MASK);
	dm_write_reg(ctx, wm_addr, wm_mask_cntl);

	stutter_cntl = dm_read_reg(ctx, stutter_addr);

	/*Write watermark set B*/
	set_reg_field_value(stutter_cntl,
		marks.a_mark,
		DPG_PIPE_STUTTER_CONTROL,
		STUTTER_EXIT_SELF_REFRESH_WATERMARK);
	dm_write_reg(ctx, stutter_addr, stutter_cntl);
}

static void program_nbp_watermark(
	const struct dc_context *ctx,
	const uint32_t offset,
	struct bw_watermarks marks)
{
	uint32_t value;
	uint32_t addr;
	/* Write mask to enable reading/writing of watermark set A */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set A */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.d_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);

	/* Write mask to enable reading/writing of watermark set B */
	addr = offset + mmDPG_WATERMARK_MASK_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		2,
		DPG_WATERMARK_MASK_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK_MASK);
	dm_write_reg(ctx, addr, value);

	addr = offset + mmDPG_PIPE_NB_PSTATE_CHANGE_CONTROL;
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_ENABLE);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_URGENT_DURING_REQUEST);
	set_reg_field_value(
		value,
		1,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_NOT_SELF_REFRESH_DURING_REQUEST);
	dm_write_reg(ctx, addr, value);

	/* Write watermark set B */
	value = dm_read_reg(ctx, addr);
	set_reg_field_value(
		value,
		marks.a_mark,
		DPG_PIPE_NB_PSTATE_CHANGE_CONTROL,
		NB_PSTATE_CHANGE_WATERMARK);
	dm_write_reg(ctx, addr, value);
}

void dce110_mem_input_program_display_marks(
	struct mem_input *mem_input,
	struct bw_watermarks nbp,
	struct bw_watermarks stutter,
	struct bw_watermarks urgent,
	uint32_t total_dest_line_time_ns)
{
	struct dce110_mem_input *bm_dce110 = TO_DCE110_MEM_INPUT(mem_input);

	program_urgency_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		urgent,
		total_dest_line_time_ns);

	program_nbp_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		nbp);

	program_stutter_watermark(
		mem_input->ctx,
		bm_dce110->offsets.dmif,
		stutter);
}

static struct mem_input_funcs dce110_mem_input_funcs = {
	.mem_input_program_display_marks =
			dce110_mem_input_program_display_marks,
	.allocate_mem_input = dce_mem_input_allocate_dmif,
	.free_mem_input = dce_mem_input_free_dmif,
	.mem_input_program_surface_flip_and_addr =
			dce110_mem_input_program_surface_flip_and_addr,
	.mem_input_program_pte_vm =
			dce_mem_input_program_pte_vm,
	.mem_input_program_surface_config =
			dce_mem_input_program_surface_config,
	.mem_input_is_flip_pending =
			dce110_mem_input_is_flip_pending,
	.mem_input_update_dchub = NULL
};
/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce110_mem_input_construct(
	struct dce110_mem_input *mem_input110,
	struct dc_context *ctx,
	uint32_t inst,
	const struct dce110_mem_input_reg_offsets *offsets)
{
	/* supported stutter method
	 * STUTTER_MODE_ENHANCED
	 * STUTTER_MODE_QUAD_DMIF_BUFFER
	 * STUTTER_MODE_WATERMARK_NBP_STATE
	 */
	mem_input110->base.funcs = &dce110_mem_input_funcs;
	mem_input110->base.ctx = ctx;

	mem_input110->base.inst = inst;

	mem_input110->offsets = *offsets;

	return true;
}
