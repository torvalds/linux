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
#include "dce120_mem_input.h"


#include "vega10/DC/dce_12_0_offset.h"
#include "vega10/DC/dce_12_0_sh_mask.h"
#include "vega10/soc15ip.h"

#define GENERAL_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_soc15(mem_input110->base.ctx, 0, reg_name, n, __VA_ARGS__)

#define GENERAL_REG_UPDATE(reg, field, val)	\
		GENERAL_REG_UPDATE_N(reg, 1, FD(reg##__##field), val)

#define GENERAL_REG_UPDATE_2(reg, field1, val1, field2, val2)	\
		GENERAL_REG_UPDATE_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)



#define DCP_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_soc15(mem_input110->base.ctx, mem_input110->offsets.dcp, reg_name, n, __VA_ARGS__)

#define DCP_REG_SET_N(reg_name, n, ...)	\
		generic_reg_set_soc15(mem_input110->base.ctx, mem_input110->offsets.dcp, reg_name, n, __VA_ARGS__)

#define DCP_REG_UPDATE(reg, field, val)	\
		DCP_REG_UPDATE_N(reg, 1, FD(reg##__##field), val)

#define DCP_REG_UPDATE_2(reg, field1, val1, field2, val2)	\
		DCP_REG_UPDATE_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define DCP_REG_UPDATE_3(reg, field1, val1, field2, val2, field3, val3)	\
		DCP_REG_UPDATE_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)

#define DCP_REG_SET(reg, field, val)	\
		DCP_REG_SET_N(reg, 1, FD(reg##__##field), val)

#define DCP_REG_SET_2(reg, field1, val1, field2, val2)	\
		DCP_REG_SET_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define DCP_REG_SET_3(reg, field1, val1, field2, val2, field3, val3)	\
		DCP_REG_SET_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)



#define DMIF_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_soc15(mem_input110->base.ctx, mem_input110->offsets.dmif, reg_name, n, __VA_ARGS__)

#define DMIF_REG_SET_N(reg_name, n, ...)	\
		generic_reg_set_soc15(mem_input110->base.ctx, mem_input110->offsets.dmif, reg_name, n, __VA_ARGS__)

#define DMIF_REG_UPDATE(reg, field, val)	\
		DMIF_REG_UPDATE_N(reg, 1, FD(reg##__##field), val)

#define DMIF_REG_UPDATE_2(reg, field1, val1, field2, val2)	\
		DMIF_REG_UPDATE_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define DMIF_REG_UPDATE_3(reg, field1, val1, field2, val2, field3, val3)	\
		DMIF_REG_UPDATE_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)

#define DMIF_REG_SET(reg, field, val)	\
		DMIF_REG_SET_N(reg, 1, FD(reg##__##field), val)

#define DMIF_REG_SET_2(reg, field1, val1, field2, val2)	\
		DMIF_REG_SET_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define DMIF_REG_SET_3(reg, field1, val1, field2, val2, field3, val3)	\
		DMIF_REG_SET_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)



#define PIPE_REG_UPDATE_N(reg_name, n, ...)	\
		generic_reg_update_soc15(mem_input110->base.ctx, mem_input110->offsets.pipe, reg_name, n, __VA_ARGS__)

#define PIPE_REG_SET_N(reg_name, n, ...)	\
		generic_reg_set_soc15(mem_input110->base.ctx, mem_input110->offsets.pipe, reg_name, n, __VA_ARGS__)

#define PIPE_REG_UPDATE(reg, field, val)	\
		PIPE_REG_UPDATE_N(reg, 1, FD(reg##__##field), val)

#define PIPE_REG_UPDATE_2(reg, field1, val1, field2, val2)	\
		PIPE_REG_UPDATE_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define PIPE_REG_UPDATE_3(reg, field1, val1, field2, val2, field3, val3)	\
		PIPE_REG_UPDATE_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)

#define PIPE_REG_SET(reg, field, val)	\
		PIPE_REG_SET_N(reg, 1, FD(reg##__##field), val)

#define PIPE_REG_SET_2(reg, field1, val1, field2, val2)	\
		PIPE_REG_SET_N(reg, 2, FD(reg##__##field1), val1, FD(reg##__##field2), val2)

#define PIPE_REG_SET_3(reg, field1, val1, field2, val2, field3, val3)	\
		PIPE_REG_SET_N(reg, 3, FD(reg##__##field1), val1, FD(reg##__##field2), val2, FD(reg##__##field3), val3)



static void program_sec_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t temp;

	/*high register MUST be programmed first*/
	temp = address.high_part &
		DCP0_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH__GRPH_SECONDARY_SURFACE_ADDRESS_HIGH_MASK;

	DCP_REG_SET(
		DCP0_GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
		GRPH_SECONDARY_SURFACE_ADDRESS_HIGH,
		temp);

	temp = address.low_part >>
		DCP0_GRPH_SECONDARY_SURFACE_ADDRESS__GRPH_SECONDARY_SURFACE_ADDRESS__SHIFT;

	DCP_REG_SET_2(
		DCP0_GRPH_SECONDARY_SURFACE_ADDRESS,
		GRPH_SECONDARY_SURFACE_ADDRESS, temp,
		GRPH_SECONDARY_DFQ_ENABLE, 0);
}

static void program_pri_addr(
	struct dce110_mem_input *mem_input110,
	PHYSICAL_ADDRESS_LOC address)
{
	uint32_t temp;

	/*high register MUST be programmed first*/
	temp = address.high_part &
		DCP0_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH__GRPH_PRIMARY_SURFACE_ADDRESS_HIGH_MASK;

	DCP_REG_SET(
		DCP0_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
		GRPH_PRIMARY_SURFACE_ADDRESS_HIGH,
		temp);

	temp = address.low_part >>
		DCP0_GRPH_PRIMARY_SURFACE_ADDRESS__GRPH_PRIMARY_SURFACE_ADDRESS__SHIFT;

	DCP_REG_SET(
		DCP0_GRPH_PRIMARY_SURFACE_ADDRESS,
		GRPH_PRIMARY_SURFACE_ADDRESS,
		temp);
}


static bool mem_input_is_flip_pending(struct mem_input *mem_input)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);
	uint32_t value;

	value = dm_read_reg_soc15(mem_input110->base.ctx,
			mmDCP0_GRPH_UPDATE, mem_input110->offsets.dcp);

	if (get_reg_field_value(value, DCP0_GRPH_UPDATE,
			GRPH_SURFACE_UPDATE_PENDING))
		return true;

	mem_input->current_address = mem_input->request_address;
	return false;
}

static bool mem_input_program_surface_flip_and_addr(
	struct mem_input *mem_input,
	const struct dc_plane_address *address,
	bool flip_immediate)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mem_input);

	/* TODO: Figure out if two modes are needed:
	 * non-XDMA Mode: GRPH_SURFACE_UPDATE_IMMEDIATE_EN = 1
	 * XDMA Mode: GRPH_SURFACE_UPDATE_H_RETRACE_EN = 1
	 */
	DCP_REG_UPDATE(DCP0_GRPH_UPDATE,
			GRPH_UPDATE_LOCK, 1);

	if (flip_immediate) {
		DCP_REG_UPDATE_2(
			DCP0_GRPH_FLIP_CONTROL,
			GRPH_SURFACE_UPDATE_IMMEDIATE_EN, 0,
			GRPH_SURFACE_UPDATE_H_RETRACE_EN, 1);
	} else {
		DCP_REG_UPDATE_2(
			DCP0_GRPH_FLIP_CONTROL,
			GRPH_SURFACE_UPDATE_IMMEDIATE_EN, 0,
			GRPH_SURFACE_UPDATE_H_RETRACE_EN, 0);
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

	DCP_REG_UPDATE(DCP0_GRPH_UPDATE,
			GRPH_UPDATE_LOCK, 0);

	return true;
}

static void mem_input_update_dchub(struct mem_input *mi,
		struct dchub_init_data *dh_data)
{
	struct dce110_mem_input *mem_input110 = TO_DCE110_MEM_INPUT(mi);
	/* TODO: port code from dal2 */
	switch (dh_data->fb_mode) {
	case FRAME_BUFFER_MODE_ZFB_ONLY:
		/*For ZFB case need to put DCHUB FB BASE and TOP upside down to indicate ZFB mode*/
		GENERAL_REG_UPDATE_2(
				DCHUB_FB_LOCATION,
				FB_TOP, 0,
				FB_BASE, 0x0FFFF);

		GENERAL_REG_UPDATE(
				DCHUB_AGP_BASE,
				AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		GENERAL_REG_UPDATE(
				DCHUB_AGP_BOT,
				AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		GENERAL_REG_UPDATE(
				DCHUB_AGP_TOP,
				AGP_TOP, (dh_data->zfb_mc_base_addr + dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_MIXED_ZFB_AND_LOCAL:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/
		GENERAL_REG_UPDATE(
				DCHUB_AGP_BASE,
				AGP_BASE, dh_data->zfb_phys_addr_base >> 22);

		GENERAL_REG_UPDATE(
				DCHUB_AGP_BOT,
				AGP_BOT, dh_data->zfb_mc_base_addr >> 22);

		GENERAL_REG_UPDATE(
				DCHUB_AGP_TOP,
				AGP_TOP, (dh_data->zfb_mc_base_addr + dh_data->zfb_size_in_byte - 1) >> 22);
		break;
	case FRAME_BUFFER_MODE_LOCAL_ONLY:
		/*Should not touch FB LOCATION (done by VBIOS on AsicInit table)*/
		GENERAL_REG_UPDATE(
				DCHUB_AGP_BASE,
				AGP_BASE, 0);

		GENERAL_REG_UPDATE(
				DCHUB_AGP_BOT,
				AGP_BOT, 0x03FFFF);

		GENERAL_REG_UPDATE(
				DCHUB_AGP_TOP,
				AGP_TOP, 0);
		break;
	default:
		break;
	}

	dh_data->dchub_initialzied = true;
	dh_data->dchub_info_valid = false;
}

static struct mem_input_funcs dce120_mem_input_funcs = {
	.mem_input_program_display_marks = dce_mem_input_program_display_marks,
	.allocate_mem_input = dce_mem_input_allocate_dmif,
	.free_mem_input = dce_mem_input_free_dmif,
	.mem_input_program_surface_flip_and_addr =
			mem_input_program_surface_flip_and_addr,
	.mem_input_program_pte_vm = dce_mem_input_program_pte_vm,
	.mem_input_program_surface_config =
			dce_mem_input_program_surface_config,
	.mem_input_is_flip_pending = mem_input_is_flip_pending,
	.mem_input_update_dchub = mem_input_update_dchub
};

/*****************************************/
/* Constructor, Destructor               */
/*****************************************/

bool dce120_mem_input_construct(
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

	if (!dce110_mem_input_construct(mem_input110, ctx, inst, offsets))
		return false;

	mem_input110->base.funcs = &dce120_mem_input_funcs;
	mem_input110->offsets = *offsets;

	return true;
}
