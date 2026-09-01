// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dcn401_mmhubbub.h"
#include "reg_helper.h"

#define REG(reg)                                                             \
	((const struct dcn35_mmhubbub_registers *)(mcif_wb30->mcif_wb_regs)) \
		->reg

#define CTX mcif_wb30->base.ctx

#undef FN
#define FN(reg_name, field_name)                                                \
	((const struct dcn401_mmhubbub_shift *)(mcif_wb30->mcif_wb_shift))       \
		->field_name,                                                   \
		((const struct dcn401_mmhubbub_mask *)(mcif_wb30->mcif_wb_mask)) \
			->field_name

static void mmhubbub401_config_mcif_arb(struct mcif_wb *mcif_wb,
		struct mcif_arb_params *params)
{
	struct dcn30_mmhubbub *mcif_wb30 = TO_DCN30_MMHUBBUB(mcif_wb);

	/* Programmed by the video driver based on the CRTC timing (for DWB) */
	REG_UPDATE(MCIF_WB_ARBITRATION_CONTROL,
			MCIF_WB_TIME_PER_PIXEL, params->dcn4x.inst_regs.time_per_pixel);

	/*
	 * Programming DWB watermark.
	 * Watermark to generate urgent in MCIF_WB_CLI, value is determined by MCIF_WB_CLI_WATERMARK_MASK.
	 * Program in ns. A formula will be provided in the pseudo code to calculate the value.
	 */
	/* Program urgent_watermarkA */
	REG_UPDATE_2(MCIF_WB_WATERMARK,
			MCIF_WB_CLI_WATERMARK_MASK, 0x0,
			MCIF_WB_CLI_WATERMARK, params->dcn4x.global_regs.wm_regs[0].urgent);
	/* Program urgent_watermarkB */
	REG_UPDATE_2(MCIF_WB_WATERMARK,
			MCIF_WB_CLI_WATERMARK_MASK, 0x1,
			MCIF_WB_CLI_WATERMARK, params->dcn4x.global_regs.wm_regs[0].urgent);
	/* Program urgent_watermarkC */
	REG_UPDATE_2(MCIF_WB_WATERMARK,
			MCIF_WB_CLI_WATERMARK_MASK, 0x2,
			MCIF_WB_CLI_WATERMARK, params->dcn4x.global_regs.wm_regs[0].urgent);
	/* Program urgent_watermarkD */
	REG_UPDATE_2(MCIF_WB_WATERMARK,
			MCIF_WB_CLI_WATERMARK_MASK, 0x3,
			MCIF_WB_CLI_WATERMARK, params->dcn4x.global_regs.wm_regs[0].urgent);

	/* Programming UCLK P-State watermark */
	/* Program nbp_state_change_watermarkA */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x0,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x0,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].uclk_pstate);
	/* Program nbp_state_change_watermarkB */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x1,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x0,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].uclk_pstate);
	/* Program nbp_state_change_watermarkC */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x2,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x0,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].uclk_pstate);
	/* Program nbp_state_change_watermarkD */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x3,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x0,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].uclk_pstate);

	/* Programming FCLK P-State watermark */
	/* Program nbp_state_change_watermarkA */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x0,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x1,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].temp_read_or_ppt);
	/* Program nbp_state_change_watermarkB */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x1,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x1,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].temp_read_or_ppt);
	/* Program nbp_state_change_watermarkC */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x2,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x1,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].fclk_pstate);
	/* Program nbp_state_change_watermarkD */
	REG_UPDATE_3(MCIF_WB_NB_PSTATE_LATENCY_WATERMARK,
		NB_PSTATE_CHANGE_WATERMARK_MASK, 0x3,
		NB_PSTATE_CHANGE_WATERMARK_TYPE, 0x1,
		NB_PSTATE_CHANGE_REFRESH_WATERMARK, params->dcn4x.global_regs.wm_regs[0].fclk_pstate);

	/* Program max_scaled_time */
	REG_UPDATE(MULTI_LEVEL_QOS_CTRL,
			MAX_SCALED_TIME_TO_URGENT, params->dcn4x.inst_regs.max_scaled_time_ns);

	/* Program slice_lines */
	REG_UPDATE(MCIF_WB_BUFMGR_VCE_CONTROL,
			MCIF_WB_BUFMGR_SLICE_SIZE, params->dcn4x.inst_regs.slice_lines);

	/* Set arbitration unit for Luma/Chroma */
	/* arb_unit=2 should be chosen for more efficiency */
	/* Arbitration size, 0: 2048 bytes 1: 4096 bytes 2: 8192 Bytes */
	REG_UPDATE(MCIF_WB_ARBITRATION_CONTROL,
			MCIF_WB_CLIENT_ARBITRATION_SLICE, params->dcn4x.inst_regs.arbitration_slice);
}

static const struct mcif_wb_funcs dcn401_mmhubbub_funcs = {
	.warmup_mcif		= mmhubbub32_warmup_mcif,
	.enable_mcif		= mmhubbub2_enable_mcif,
	.disable_mcif		= mmhubbub2_disable_mcif,
	.config_mcif_buf	= mmhubbub32_config_mcif_buf,
	.config_mcif_arb	= mmhubbub401_config_mcif_arb,
	.config_mcif_irq	= mmhubbub2_config_mcif_irq,
	.dump_frame			= mcifwb2_dump_frame,
};

void dcn401_mmhubbub_construct(struct dcn30_mmhubbub *mcif_wb30,
		struct dc_context *ctx,
		const struct dcn35_mmhubbub_registers *mcif_wb_regs,
		const struct dcn401_mmhubbub_shift *mcif_wb_shift,
		const struct dcn401_mmhubbub_mask *mcif_wb_mask,
		int inst)
{
	mcif_wb30->base.ctx = ctx;

	mcif_wb30->base.inst = inst;
	mcif_wb30->base.funcs = &dcn401_mmhubbub_funcs;
	mcif_wb30->mcif_wb_regs = (const struct dcn30_mmhubbub_registers *)mcif_wb_regs;
	mcif_wb30->mcif_wb_shift = (const struct dcn30_mmhubbub_shift *)mcif_wb_shift;
	mcif_wb30->mcif_wb_mask = (const struct dcn30_mmhubbub_mask *)mcif_wb_mask;
}
