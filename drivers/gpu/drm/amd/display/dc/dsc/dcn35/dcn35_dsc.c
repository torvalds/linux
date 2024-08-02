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

#include "dcn35_dsc.h"
#include "reg_helper.h"

static void dsc35_enable(struct display_stream_compressor *dsc, int opp_pipe);

static const struct dsc_funcs dcn35_dsc_funcs = {
	.dsc_get_enc_caps = dsc2_get_enc_caps,
	.dsc_read_state = dsc2_read_state,
	.dsc_validate_stream = dsc2_validate_stream,
	.dsc_set_config = dsc2_set_config,
	.dsc_get_packed_pps = dsc2_get_packed_pps,
	.dsc_enable = dsc35_enable,
	.dsc_disable = dsc2_disable,
	.dsc_disconnect = dsc2_disconnect,
	.dsc_wait_disconnect_pending_clear = dsc2_wait_disconnect_pending_clear,
};

/* Macro definitios for REG_SET macros*/
#define CTX \
	dsc20->base.ctx

#define REG(reg)\
	dsc20->dsc_regs->reg

#undef FN
#define FN(reg_name, field_name)                                          \
	((const struct dcn35_dsc_shift *)(dsc20->dsc_shift))->field_name, \
		((const struct dcn35_dsc_mask *)(dsc20->dsc_mask))->field_name

#define DC_LOGGER \
	dsc->ctx->logger

void dsc35_construct(struct dcn20_dsc *dsc,
		struct dc_context *ctx,
		int inst,
		const struct dcn20_dsc_registers *dsc_regs,
		const struct dcn35_dsc_shift *dsc_shift,
		const struct dcn35_dsc_mask *dsc_mask)
{
	dsc->base.ctx = ctx;
	dsc->base.inst = inst;
	dsc->base.funcs = &dcn35_dsc_funcs;

	dsc->dsc_regs = dsc_regs;
	dsc->dsc_shift = (const struct dcn20_dsc_shift *)(dsc_shift);
	dsc->dsc_mask = (const struct dcn20_dsc_mask *)(dsc_mask);

	dsc->max_image_width = 5184;
}

static void dsc35_enable(struct display_stream_compressor *dsc, int opp_pipe)
{
	struct dcn20_dsc *dsc20 = TO_DCN20_DSC(dsc);
	int dsc_clock_en;
	int dsc_fw_config;
	int enabled_opp_pipe;

	DC_LOG_DSC("enable DSC %d at opp pipe %d", dsc->inst, opp_pipe);

	// TODO: After an idle exit, the HW default values for power control
	// are changed intermittently due to unknown reasons. There are cases
	// when dscc memory are still in shutdown state during enablement.
	// Reset power control to hw default values.
	REG_UPDATE_2(DSCC_MEM_POWER_CONTROL,
		DSCC_MEM_PWR_FORCE, 0,
		DSCC_MEM_PWR_DIS, 0);

	REG_GET(DSC_TOP_CONTROL, DSC_CLOCK_EN, &dsc_clock_en);
	REG_GET_2(DSCRM_DSC_FORWARD_CONFIG, DSCRM_DSC_FORWARD_EN, &dsc_fw_config, DSCRM_DSC_OPP_PIPE_SOURCE, &enabled_opp_pipe);
	if ((dsc_clock_en || dsc_fw_config) && enabled_opp_pipe != opp_pipe) {
		DC_LOG_DSC("ERROR: DSC %d at opp pipe %d already enabled!", dsc->inst, enabled_opp_pipe);
		ASSERT(0);
	}

	REG_UPDATE(DSC_TOP_CONTROL,
		DSC_CLOCK_EN, 1);

	REG_UPDATE_2(DSCRM_DSC_FORWARD_CONFIG,
		DSCRM_DSC_FORWARD_EN, 1,
		DSCRM_DSC_OPP_PIPE_SOURCE, opp_pipe);
}

void dsc35_set_fgcg(struct dcn20_dsc *dsc20, bool enable)
{
	REG_UPDATE(DSC_TOP_CONTROL, DSC_FGCG_REP_DIS, !enable);
}
