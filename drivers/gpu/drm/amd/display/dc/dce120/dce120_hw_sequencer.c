/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
#include "dc.h"
#include "core_dc.h"
#include "core_types.h"
#include "dce120_hw_sequencer.h"

#include "dce110/dce110_hw_sequencer.h"

/* include DCE12.0 register header files */
#include "vega10/DC/dce_12_0_offset.h"
#include "vega10/DC/dce_12_0_sh_mask.h"
#include "vega10/soc15ip.h"
#include "reg_helper.h"

struct dce120_hw_seq_reg_offsets {
	uint32_t crtc;
};

static const struct dce120_hw_seq_reg_offsets reg_offsets[] = {
{
	.crtc = (mmCRTC0_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC1_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC2_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC3_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC4_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC5_CRTC_GSL_CONTROL - mmCRTC0_CRTC_GSL_CONTROL),
}
};

#define HW_REG_CRTC(reg, id)\
	(reg + reg_offsets[id].crtc)

#define CNTL_ID(controller_id)\
	controller_id
/*******************************************************************************
 * Private definitions
 ******************************************************************************/
#if 0
static void dce120_init_pte(struct dc_context *ctx, uint8_t controller_id)
{
	uint32_t addr;
	uint32_t value = 0;
	uint32_t chunk_int = 0;
	uint32_t chunk_mul = 0;
/*
	addr = mmDCP0_DVMM_PTE_CONTROL + controller_id *
			(mmDCP1_DVMM_PTE_CONTROL- mmDCP0_DVMM_PTE_CONTROL);

	value = dm_read_reg(ctx, addr);

	set_reg_field_value(
			value, 0, DCP, controller_id,
			DVMM_PTE_CONTROL,
			DVMM_USE_SINGLE_PTE);

	set_reg_field_value_soc15(
			value, 1, DCP, controller_id,
			DVMM_PTE_CONTROL,
			DVMM_PTE_BUFFER_MODE0);

	set_reg_field_value_soc15(
			value, 1, DCP, controller_id,
			DVMM_PTE_CONTROL,
			DVMM_PTE_BUFFER_MODE1);

	dm_write_reg(ctx, addr, value);*/

	addr = mmDVMM_PTE_REQ;
	value = dm_read_reg(ctx, addr);

	chunk_int = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_INT);

	chunk_mul = get_reg_field_value(
		value,
		DVMM_PTE_REQ,
		HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

	if (chunk_int != 0x4 || chunk_mul != 0x4) {

		set_reg_field_value(
			value,
			255,
			DVMM_PTE_REQ,
			MAX_PTEREQ_TO_ISSUE);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_INT);

		set_reg_field_value(
			value,
			4,
			DVMM_PTE_REQ,
			HFLIP_PTEREQ_PER_CHUNK_MULTIPLIER);

		dm_write_reg(ctx, addr, value);
	}
}
#endif

static bool dce120_enable_display_power_gating(
	struct core_dc *dc,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating)
{
	/* disable for bringup */
#if 0
	enum bp_result bp_result = BP_RESULT_OK;
	enum bp_pipe_control_action cntl;
	struct dc_context *ctx = dc->ctx;

	if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment))
		return true;

	if (power_gating == PIPE_GATING_CONTROL_INIT)
		cntl = ASIC_PIPE_INIT;
	else if (power_gating == PIPE_GATING_CONTROL_ENABLE)
		cntl = ASIC_PIPE_ENABLE;
	else
		cntl = ASIC_PIPE_DISABLE;

	if (power_gating != PIPE_GATING_CONTROL_INIT || controller_id == 0) {

		bp_result = dcb->funcs->enable_disp_power_gating(
						dcb, controller_id + 1, cntl);

		/* Revert MASTER_UPDATE_MODE to 0 because bios sets it 2
		 * by default when command table is called
		 */
		dm_write_reg(ctx,
			HW_REG_CRTC(mmCRTC0_CRTC_MASTER_UPDATE_MODE, controller_id),
			0);
	}

	if (power_gating != PIPE_GATING_CONTROL_ENABLE)
		dce120_init_pte(ctx, controller_id);

	if (bp_result == BP_RESULT_OK)
		return true;
	else
		return false;
#endif
	return false;
}

bool dce120_hw_sequencer_construct(struct core_dc *dc)
{
	/* All registers used by dce11.2 match those in dce11 in offset and
	 * structure
	 */
	dce110_hw_sequencer_construct(dc);
	dc->hwss.enable_display_power_gating = dce120_enable_display_power_gating;

	return true;
}

