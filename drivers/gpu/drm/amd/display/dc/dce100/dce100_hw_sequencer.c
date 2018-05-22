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
#include "core_types.h"
#include "hw_sequencer.h"
#include "dce100_hw_sequencer.h"
#include "resource.h"

#include "dce110/dce110_hw_sequencer.h"

/* include DCE10 register header files */
#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"

struct dce100_hw_seq_reg_offsets {
	uint32_t blnd;
	uint32_t crtc;
};

static const struct dce100_hw_seq_reg_offsets reg_offsets[] = {
{
	.crtc = (mmCRTC0_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC1_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC2_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC3_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC4_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
},
{
	.crtc = (mmCRTC5_CRTC_GSL_CONTROL - mmCRTC_GSL_CONTROL),
}
};

#define HW_REG_CRTC(reg, id)\
	(reg + reg_offsets[id].crtc)

/*******************************************************************************
 * Private definitions
 ******************************************************************************/
/***************************PIPE_CONTROL***********************************/

static bool dce100_enable_display_power_gating(
	struct dc *dc,
	uint8_t controller_id,
	struct dc_bios *dcb,
	enum pipe_gating_control power_gating)
{
	enum bp_result bp_result = BP_RESULT_OK;
	enum bp_pipe_control_action cntl;
	struct dc_context *ctx = dc->ctx;

	if (power_gating == PIPE_GATING_CONTROL_INIT)
		cntl = ASIC_PIPE_INIT;
	else if (power_gating == PIPE_GATING_CONTROL_ENABLE)
		cntl = ASIC_PIPE_ENABLE;
	else
		cntl = ASIC_PIPE_DISABLE;

	if (!(power_gating == PIPE_GATING_CONTROL_INIT && controller_id != 0)){

		bp_result = dcb->funcs->enable_disp_power_gating(
						dcb, controller_id + 1, cntl);

		/* Revert MASTER_UPDATE_MODE to 0 because bios sets it 2
		 * by default when command table is called
		 */
		dm_write_reg(ctx,
			HW_REG_CRTC(mmMASTER_UPDATE_MODE, controller_id),
			0);
	}

	if (bp_result == BP_RESULT_OK)
		return true;
	else
		return false;
}

static void dce100_pplib_apply_display_requirements(
	struct dc *dc,
	struct dc_state *context)
{
	struct dm_pp_display_configuration *pp_display_cfg = &context->pp_display_cfg;

	pp_display_cfg->avail_mclk_switch_time_us =
						dce110_get_min_vblank_time_us(context);
	/*pp_display_cfg->min_memory_clock_khz = context->bw.dce.yclk_khz
		/ MEMORY_TYPE_MULTIPLIER;*/

	dce110_fill_display_configs(context, pp_display_cfg);

	if (memcmp(&dc->prev_display_config, pp_display_cfg, sizeof(
			struct dm_pp_display_configuration)) !=  0)
		dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);

	dc->prev_display_config = *pp_display_cfg;
}

void dce100_set_bandwidth(
		struct dc *dc,
		struct dc_state *context,
		bool decrease_allowed)
{
	if (decrease_allowed || context->bw.dce.dispclk_khz > dc->current_state->bw.dce.dispclk_khz) {
		dc->res_pool->display_clock->funcs->set_clock(
				dc->res_pool->display_clock,
				context->bw.dce.dispclk_khz * 115 / 100);
		dc->current_state->bw.dce.dispclk_khz = context->bw.dce.dispclk_khz;
	}
	dce100_pplib_apply_display_requirements(dc, context);
}


/**************************************************************************/

void dce100_hw_sequencer_construct(struct dc *dc)
{
	dce110_hw_sequencer_construct(dc);

	dc->hwss.enable_display_power_gating = dce100_enable_display_power_gating;
	dc->hwss.set_bandwidth = dce100_set_bandwidth;
	dc->hwss.pplib_apply_display_requirements =
			dce100_pplib_apply_display_requirements;
}

