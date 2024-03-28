// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dccg.h"
#include "clk_mgr_internal.h"
#include "dcn401/dcn401_clk_mgr_smu_msg.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dce100/dce_clk_mgr.h"
#include "dcn31/dcn31_clk_mgr.h"
#include "dcn32/dcn32_clk_mgr.h"
#include "dcn401/dcn401_clk_mgr.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dm_helpers.h"
#include "link.h"
#include "atomfirmware.h"

#include "dcn401_smu14_driver_if.h"

#include "dcn/dcn_4_1_0_offset.h"
#include "dcn/dcn_4_1_0_sh_mask.h"

#include "dml/dcn401/dcn401_fpu.h"

#define mmCLK01_CLK0_CLK_PLL_REQ                        0x16E37
#define mmCLK01_CLK0_CLK0_DFS_CNTL                      0x16E69
#define mmCLK01_CLK0_CLK1_DFS_CNTL                      0x16E6C
#define mmCLK01_CLK0_CLK2_DFS_CNTL                      0x16E6F
#define mmCLK01_CLK0_CLK3_DFS_CNTL                      0x16E72
#define mmCLK01_CLK0_CLK4_DFS_CNTL                      0x16E75

#define CLK0_CLK_PLL_REQ__FbMult_int_MASK                  0x000001ffUL
#define CLK0_CLK_PLL_REQ__PllSpineDiv_MASK                 0x0000f000UL
#define CLK0_CLK_PLL_REQ__FbMult_frac_MASK                 0xffff0000UL
#define CLK0_CLK_PLL_REQ__FbMult_int__SHIFT                0x00000000
#define CLK0_CLK_PLL_REQ__PllSpineDiv__SHIFT               0x0000000c
#define CLK0_CLK_PLL_REQ__FbMult_frac__SHIFT               0x00000010

#undef FN
#define FN(reg_name, field_name) \
	clk_mgr->clk_mgr_shift->field_name, clk_mgr->clk_mgr_mask->field_name

#define REG(reg) \
	(clk_mgr->regs->reg)

#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
					reg ## reg_name

#define CLK_SR_DCN401(reg_name, block, inst)\
	.reg_name = mm ## block ## _ ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn401 = {
	CLK_REG_LIST_DCN401()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn401 = {
	CLK_COMMON_MASK_SH_LIST_DCN401(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn401 = {
	CLK_COMMON_MASK_SH_LIST_DCN401(_MASK)
};

static bool dcn401_is_ppclk_dpm_enabled(struct clk_mgr_internal *clk_mgr, PPCLK_e clk)
{
	bool ppclk_dpm_enabled = false;

	switch (clk) {
	case PPCLK_SOCCLK:
		ppclk_dpm_enabled =
				clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_socclk_levels > 1;
		break;
	case PPCLK_UCLK:
		ppclk_dpm_enabled =
				clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_memclk_levels > 1;
		break;
	case PPCLK_FCLK:
		ppclk_dpm_enabled =
				clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_fclk_levels > 1;
		break;
	case PPCLK_DISPCLK:
		ppclk_dpm_enabled =
				clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_dispclk_levels > 1;
		break;
	case PPCLK_DPPCLK:
		ppclk_dpm_enabled =
				clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_dppclk_levels > 1;
		break;
	case PPCLK_DPREFCLK:
		ppclk_dpm_enabled = false;
		break;
	case PPCLK_DCFCLK:
		ppclk_dpm_enabled =
				clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels > 1;
		break;
	case PPCLK_DTBCLK:
		ppclk_dpm_enabled =
				clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels > 1;
		break;
	default:
		ppclk_dpm_enabled = false;
	}

	ppclk_dpm_enabled &= clk_mgr->smu_present;

	return ppclk_dpm_enabled;
}

/* Query SMU for all clock states for a particular clock */
static void dcn401_init_single_clock(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, unsigned int *entry_0,
		unsigned int *num_levels)
{
	unsigned int i;
	char *entry_i = (char *)entry_0;

	uint32_t ret = dcn30_smu_get_dpm_freq_by_index(clk_mgr, clk, 0xFF);

	if (ret & (1 << 31))
		/* fine-grained, only min and max */
		*num_levels = 2;
	else
		/* discrete, a number of fixed states */
		/* will set num_levels to 0 on failure */
		*num_levels = ret & 0xFF;

	/* if the initial message failed, num_levels will be 0 */
	for (i = 0; i < *num_levels && i < ARRAY_SIZE(clk_mgr->base.bw_params->clk_table.entries); i++) {
		*((unsigned int *)entry_i) = (dcn30_smu_get_dpm_freq_by_index(clk_mgr, clk, i) & 0xFFFF);
		entry_i += sizeof(clk_mgr->base.bw_params->clk_table.entries[0]);
	}
}

static void dcn401_build_wm_range_table(struct clk_mgr *clk_mgr)
{
	/* legacy */
	DC_FP_START();
	dcn401_build_wm_range_table_fpu(clk_mgr);
	DC_FP_END();

	if (clk_mgr->ctx->dc->debug.using_dml21) {
		/* For min clocks use as reported by PM FW and report those as min */
		uint16_t min_uclk_mhz = clk_mgr->bw_params->clk_table.entries[0].memclk_mhz;
		uint16_t min_dcfclk_mhz	= clk_mgr->bw_params->clk_table.entries[0].dcfclk_mhz;

		/* Set A - Normal - default values */
		clk_mgr->bw_params->wm_table.nv_entries[WM_A].valid = true;
		clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.wm_type = WATERMARKS_CLOCK_RANGE;
		clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
		clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_dcfclk = 0xFFFF;
		clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.min_uclk = min_uclk_mhz;
		clk_mgr->bw_params->wm_table.nv_entries[WM_A].pmfw_breakdown.max_uclk = 0xFFFF;

		/* Set B - Unused on dcn4 */
		clk_mgr->bw_params->wm_table.nv_entries[WM_B].valid = false;

		/* Set 1A - Dummy P-State - P-State latency set to "dummy p-state" value */
		/* 'DalDummyClockChangeLatencyNs' registry key option set to 0x7FFFFFFF can be used to disable Set C for dummy p-state */
		if (clk_mgr->ctx->dc->bb_overrides.dummy_clock_change_latency_ns != 0x7FFFFFFF) {
			clk_mgr->bw_params->wm_table.nv_entries[WM_1A].valid = true;
			clk_mgr->bw_params->wm_table.nv_entries[WM_1A].pmfw_breakdown.wm_type = WATERMARKS_DUMMY_PSTATE;
			clk_mgr->bw_params->wm_table.nv_entries[WM_1A].pmfw_breakdown.min_dcfclk = min_dcfclk_mhz;
			clk_mgr->bw_params->wm_table.nv_entries[WM_1A].pmfw_breakdown.max_dcfclk = 0xFFFF;
			clk_mgr->bw_params->wm_table.nv_entries[WM_1A].pmfw_breakdown.min_uclk = min_uclk_mhz;
			clk_mgr->bw_params->wm_table.nv_entries[WM_1A].pmfw_breakdown.max_uclk = 0xFFFF;
		} else {
			clk_mgr->bw_params->wm_table.nv_entries[WM_1A].valid = false;
		}

		/* Set 1B - Unused on dcn4 */
		clk_mgr->bw_params->wm_table.nv_entries[WM_1B].valid = false;
	}
}

void dcn401_init_clocks(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_limit_num_entries *num_entries_per_clk = &clk_mgr_base->bw_params->clk_table.num_entries_per_clk;
	unsigned int i;

	memset(&(clk_mgr_base->clks), 0, sizeof(struct dc_clocks));
	clk_mgr_base->clks.p_state_change_support = true;
	clk_mgr_base->clks.prev_p_state_change_support = true;
	clk_mgr_base->clks.fclk_prev_p_state_change_support = true;
	clk_mgr->smu_present = false;
	clk_mgr->dpm_present = false;

	if (!clk_mgr_base->bw_params)
		return;

	if (!clk_mgr_base->force_smu_not_present && dcn30_smu_get_smu_version(clk_mgr, &clk_mgr->smu_ver))
		clk_mgr->smu_present = true;

	if (!clk_mgr->smu_present)
		return;

	dcn30_smu_check_driver_if_version(clk_mgr);
	dcn30_smu_check_msg_header_version(clk_mgr);

	/* DCFCLK */
	dcn401_init_single_clock(clk_mgr, PPCLK_DCFCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dcfclk_mhz,
			&num_entries_per_clk->num_dcfclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.dcfclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_DCFCLK);
	if (num_entries_per_clk->num_dcfclk_levels && clk_mgr_base->bw_params->dc_mode_limit.dcfclk_mhz ==
			clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_dcfclk_levels - 1].dcfclk_mhz)
		clk_mgr_base->bw_params->dc_mode_limit.dcfclk_mhz = 0;

	/* SOCCLK */
	dcn401_init_single_clock(clk_mgr, PPCLK_SOCCLK,
					&clk_mgr_base->bw_params->clk_table.entries[0].socclk_mhz,
					&num_entries_per_clk->num_socclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.socclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_SOCCLK);
	if (num_entries_per_clk->num_socclk_levels && clk_mgr_base->bw_params->dc_mode_limit.socclk_mhz ==
			clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_socclk_levels - 1].socclk_mhz)
		clk_mgr_base->bw_params->dc_mode_limit.socclk_mhz = 0;

	/* DTBCLK */
	if (!clk_mgr->base.ctx->dc->debug.disable_dtb_ref_clk_switch) {
		dcn401_init_single_clock(clk_mgr, PPCLK_DTBCLK,
				&clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz,
				&num_entries_per_clk->num_dtbclk_levels);
		clk_mgr_base->bw_params->dc_mode_limit.dtbclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_DTBCLK);
		if (num_entries_per_clk->num_dtbclk_levels && clk_mgr_base->bw_params->dc_mode_limit.dtbclk_mhz ==
				clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_dtbclk_levels - 1].dtbclk_mhz)
			clk_mgr_base->bw_params->dc_mode_limit.dtbclk_mhz = 0;
	}

	/* DISPCLK */
	dcn401_init_single_clock(clk_mgr, PPCLK_DISPCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dispclk_mhz,
			&num_entries_per_clk->num_dispclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.dispclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_DISPCLK);
	if (num_entries_per_clk->num_dispclk_levels && clk_mgr_base->bw_params->dc_mode_limit.dispclk_mhz ==
			clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_dispclk_levels - 1].dispclk_mhz)
		clk_mgr_base->bw_params->dc_mode_limit.dispclk_mhz = 0;

	/* DPPCLK */
	dcn401_init_single_clock(clk_mgr, PPCLK_DPPCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].dppclk_mhz,
			&num_entries_per_clk->num_dppclk_levels);

	if (num_entries_per_clk->num_dcfclk_levels &&
			num_entries_per_clk->num_dtbclk_levels &&
			num_entries_per_clk->num_dispclk_levels)
		clk_mgr->dpm_present = true;

	if (clk_mgr_base->ctx->dc->debug.min_disp_clk_khz) {
		for (i = 0; i < num_entries_per_clk->num_dispclk_levels; i++)
			if (clk_mgr_base->bw_params->clk_table.entries[i].dispclk_mhz
					< khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_disp_clk_khz))
				clk_mgr_base->bw_params->clk_table.entries[i].dispclk_mhz
					= khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_disp_clk_khz);
	}

	if (clk_mgr_base->ctx->dc->debug.min_dpp_clk_khz) {
		for (i = 0; i < num_entries_per_clk->num_dppclk_levels; i++)
			if (clk_mgr_base->bw_params->clk_table.entries[i].dppclk_mhz
					< khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_dpp_clk_khz))
				clk_mgr_base->bw_params->clk_table.entries[i].dppclk_mhz
					= khz_to_mhz_ceil(clk_mgr_base->ctx->dc->debug.min_dpp_clk_khz);
	}

	/* Get UCLK, update bounding box */
	clk_mgr_base->funcs->get_memclk_states_from_smu(clk_mgr_base);

	/* WM range table */
	dcn401_build_wm_range_table(clk_mgr_base);
}

static void dcn401_update_clocks_update_dtb_dto(struct clk_mgr_internal *clk_mgr,
			struct dc_state *context,
			int ref_dtbclk_khz)
{
	struct dccg *dccg = clk_mgr->dccg;
	uint32_t tg_mask = 0;
	int i;

	for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		/* use mask to program DTO once per tg */
		if (pipe_ctx->stream_res.tg &&
				!(tg_mask & (1 << pipe_ctx->stream_res.tg->inst))) {
			tg_mask |= (1 << pipe_ctx->stream_res.tg->inst);

			if (dccg->ctx->dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
				pipe_ctx->clock_source->funcs->program_pix_clk(
						pipe_ctx->clock_source,
						&pipe_ctx->stream_res.pix_clk_params,
						dccg->ctx->dc->link_srv->dp_get_encoding_format(&pipe_ctx->link_config.dp_link_settings),
						&pipe_ctx->pll_settings);
			}

		}
	}
}

void dcn401_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr,
		struct dc_state *context, bool safe_to_lower, int dppclk_khz)
{
	int i;

	clk_mgr->dccg->ref_dppclk = dppclk_khz;
	for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
		int dpp_inst = 0, dppclk_khz, prev_dppclk_khz;

		dppclk_khz = context->res_ctx.pipe_ctx[i].plane_res.bw.dppclk_khz;

		if (context->res_ctx.pipe_ctx[i].plane_res.dpp)
			dpp_inst = context->res_ctx.pipe_ctx[i].plane_res.dpp->inst;
		else if (!context->res_ctx.pipe_ctx[i].plane_res.dpp && dppclk_khz == 0) {
			/* dpp == NULL && dppclk_khz == 0 is valid because of pipe harvesting.
			 * In this case just continue in loop
			 */
			continue;
		} else if (!context->res_ctx.pipe_ctx[i].plane_res.dpp && dppclk_khz > 0) {
			/* The software state is not valid if dpp resource is NULL and
			 * dppclk_khz > 0.
			 */
			ASSERT(false);
			continue;
		}

		prev_dppclk_khz = clk_mgr->dccg->pipe_dppclk_khz[i];

		if (safe_to_lower || prev_dppclk_khz < dppclk_khz)
			clk_mgr->dccg->funcs->update_dpp_dto(
							clk_mgr->dccg, dpp_inst, dppclk_khz);
	}
}

static int dcn401_set_hard_min_by_freq_optimized(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, int requested_clk_khz)
{
	if (!clk_mgr->smu_present || !dcn401_is_ppclk_dpm_enabled(clk_mgr, clk))
		return 0;

	/*
	 * SMU set hard min interface takes requested clock in mhz and return
	 * actual clock configured in khz. If we floor requested clk to mhz,
	 * there is a chance that the actual clock configured in khz is less
	 * than requested. If we ceil it to mhz, there is a chance that it
	 * unnecessarily dumps up to a higher dpm level, which burns more power.
	 * The solution is to set by flooring it to mhz first. If the actual
	 * clock returned is less than requested, then we will ceil the
	 * requested value to mhz and call it again.
	 */
	int actual_clk_khz = dcn401_smu_set_hard_min_by_freq(clk_mgr, clk, khz_to_mhz_floor(requested_clk_khz));

	if (actual_clk_khz < requested_clk_khz)
		actual_clk_khz = dcn401_smu_set_hard_min_by_freq(clk_mgr, clk, khz_to_mhz_ceil(requested_clk_khz));

	return actual_clk_khz;
}

static void dcn401_update_clocks_update_dentist(
		struct clk_mgr_internal *clk_mgr,
		struct dc_state *context)
{
	uint32_t new_disp_divider = 0;
	uint32_t new_dispclk_wdivider = 0;
	uint32_t old_dispclk_wdivider = 0;
	uint32_t i;
	uint32_t dentist_dispclk_wdivider_readback = 0;
	struct dc *dc = clk_mgr->base.ctx->dc;

	if (clk_mgr->base.clks.dispclk_khz == 0)
		return;

	new_disp_divider = DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz / clk_mgr->base.clks.dispclk_khz;

	new_dispclk_wdivider = dentist_get_did_from_divider(new_disp_divider);
	REG_GET(DENTIST_DISPCLK_CNTL,
			DENTIST_DISPCLK_WDIVIDER, &old_dispclk_wdivider);

	/* When changing divider to or from 127, some extra programming is required to prevent corruption */
	if (old_dispclk_wdivider == 127 && new_dispclk_wdivider != 127) {
		for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
			uint32_t fifo_level;
			struct dccg *dccg = clk_mgr->base.ctx->dc->res_pool->dccg;
			struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;
			int32_t N;
			int32_t j;

			if (!resource_is_pipe_type(pipe_ctx, OTG_MASTER))
				continue;
			/* Virtual encoders don't have this function */
			if (!stream_enc->funcs->get_fifo_cal_average_level)
				continue;
			fifo_level = stream_enc->funcs->get_fifo_cal_average_level(
					stream_enc);
			N = fifo_level / 4;
			dccg->funcs->set_fifo_errdet_ovr_en(
					dccg,
					true);
			for (j = 0; j < N - 4; j++)
				dccg->funcs->otg_drop_pixel(
						dccg,
						pipe_ctx->stream_res.tg->inst);
			dccg->funcs->set_fifo_errdet_ovr_en(
					dccg,
					false);
		}
	} else if (new_dispclk_wdivider == 127 && old_dispclk_wdivider != 127) {
		/* request clock with 126 divider first */
		uint32_t temp_disp_divider = dentist_get_divider_from_did(126);
		uint32_t temp_dispclk_khz = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR * clk_mgr->base.dentist_vco_freq_khz) / temp_disp_divider;

		if (clk_mgr->smu_present && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DISPCLK))
			dcn401_set_hard_min_by_freq_optimized(clk_mgr, PPCLK_DISPCLK,
					temp_dispclk_khz);

		if (dc->debug.override_dispclk_programming) {
			REG_GET(DENTIST_DISPCLK_CNTL,
					DENTIST_DISPCLK_WDIVIDER, &dentist_dispclk_wdivider_readback);

			if (dentist_dispclk_wdivider_readback != 126) {
				REG_UPDATE(DENTIST_DISPCLK_CNTL,
						DENTIST_DISPCLK_WDIVIDER, 126);
				REG_WAIT(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, 1, 50, 2000);
			}
		}

		for (i = 0; i < clk_mgr->base.ctx->dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
			struct dccg *dccg = clk_mgr->base.ctx->dc->res_pool->dccg;
			struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;
			uint32_t fifo_level;
			int32_t N;
			int32_t j;

			if (!resource_is_pipe_type(pipe_ctx, OTG_MASTER))
				continue;
			/* Virtual encoders don't have this function */
			if (!stream_enc->funcs->get_fifo_cal_average_level)
				continue;
			fifo_level = stream_enc->funcs->get_fifo_cal_average_level(
					stream_enc);
			N = fifo_level / 4;
			dccg->funcs->set_fifo_errdet_ovr_en(dccg, true);
			for (j = 0; j < 12 - N; j++)
				dccg->funcs->otg_add_pixel(dccg,
						pipe_ctx->stream_res.tg->inst);
			dccg->funcs->set_fifo_errdet_ovr_en(dccg, false);
		}
	}

	/* do requested DISPCLK updates*/
	if (clk_mgr->smu_present && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DISPCLK))
		dcn401_set_hard_min_by_freq_optimized(clk_mgr, PPCLK_DISPCLK,
				clk_mgr->base.clks.dispclk_khz);

	if (dc->debug.override_dispclk_programming) {
		REG_GET(DENTIST_DISPCLK_CNTL,
				DENTIST_DISPCLK_WDIVIDER, &dentist_dispclk_wdivider_readback);

		if (dentist_dispclk_wdivider_readback > new_dispclk_wdivider) {
			REG_UPDATE(DENTIST_DISPCLK_CNTL,
					DENTIST_DISPCLK_WDIVIDER, new_dispclk_wdivider);
			REG_WAIT(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_CHG_DONE, 1, 50, 2000);
		}
	}

}

static void dcn401_update_clocks(struct clk_mgr *clk_mgr_base,
			struct dc_state *context,
			bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dc_clocks *new_clocks = &context->bw_ctx.bw.dcn.clk;
	struct dc *dc = clk_mgr_base->ctx->dc;
	int display_count;
	bool update_dppclk = false;
	bool update_dispclk = false;
	bool enter_display_off = false;
	bool dpp_clock_lowered = false;
	struct dmcu *dmcu = clk_mgr_base->ctx->dc->res_pool->dmcu;
	bool force_reset = false;
	bool update_uclk = false, update_fclk = false;
	bool p_state_change_support;
	bool fclk_p_state_change_support;
	int total_plane_count;

	if (dc->work_arounds.skip_clock_update)
		return;

	if (clk_mgr_base->clks.dispclk_khz == 0 ||
			(dc->debug.force_clock_mode & 0x1)) {
		/* This is from resume or boot up, if forced_clock cfg option used,
		 * we bypass program dispclk and DPPCLK, but need set them for S3.
		 */
		force_reset = true;

		dcn2_read_clocks_from_hw_dentist(clk_mgr_base);

		/* Force_clock_mode 0x1:  force reset the clock even it is the same clock
		 * as long as it is in Passive level.
		 */
	}
	display_count = clk_mgr_helper_get_active_display_cnt(dc, context);

	if (display_count == 0)
		enter_display_off = true;

	if (clk_mgr->smu_present) {
		if (enter_display_off == safe_to_lower)
			dcn30_smu_set_num_of_displays(clk_mgr, display_count);

		clk_mgr_base->clks.fclk_prev_p_state_change_support = clk_mgr_base->clks.fclk_p_state_change_support;

		total_plane_count = clk_mgr_helper_get_active_plane_cnt(dc, context);
		fclk_p_state_change_support = new_clocks->fclk_p_state_change_support || (total_plane_count == 0);

		if (should_update_pstate_support(safe_to_lower, fclk_p_state_change_support, clk_mgr_base->clks.fclk_p_state_change_support)) {
			clk_mgr_base->clks.fclk_p_state_change_support = fclk_p_state_change_support;

			/* To enable FCLK P-state switching, send FCLK_PSTATE_SUPPORTED message to PMFW */
			if (clk_mgr_base->clks.fclk_p_state_change_support) {
				/* Handle the code for sending a message to PMFW that FCLK P-state change is supported */
				dcn401_smu_send_fclk_pstate_message(clk_mgr, FCLK_PSTATE_SUPPORTED);
			}
		}

		if (dc->debug.force_min_dcfclk_mhz > 0)
			new_clocks->dcfclk_khz = (new_clocks->dcfclk_khz > (dc->debug.force_min_dcfclk_mhz * 1000)) ?
					new_clocks->dcfclk_khz : (dc->debug.force_min_dcfclk_mhz * 1000);

		if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
			clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
			if (dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DCFCLK))
				dcn401_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DCFCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_khz));
		}

		if (should_set_clock(safe_to_lower, new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
			clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
			if (dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DCFCLK))
				dcn30_smu_set_min_deep_sleep_dcef_clk(clk_mgr, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_deep_sleep_khz));
		}

		if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr_base->clks.socclk_khz))
			/* We don't actually care about socclk, don't notify SMU of hard min */
			clk_mgr_base->clks.socclk_khz = new_clocks->socclk_khz;

		clk_mgr_base->clks.prev_p_state_change_support = clk_mgr_base->clks.p_state_change_support;
		clk_mgr_base->clks.prev_num_ways = clk_mgr_base->clks.num_ways;

		if (clk_mgr_base->clks.num_ways != new_clocks->num_ways &&
				clk_mgr_base->clks.num_ways < new_clocks->num_ways) {
			clk_mgr_base->clks.num_ways = new_clocks->num_ways;
			if (dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
				dcn401_smu_send_cab_for_uclk_message(clk_mgr, clk_mgr_base->clks.num_ways);
		}


		p_state_change_support = new_clocks->p_state_change_support || (total_plane_count == 0);
		if (should_update_pstate_support(safe_to_lower, p_state_change_support, clk_mgr_base->clks.p_state_change_support)) {
			clk_mgr_base->clks.p_state_change_support = p_state_change_support;
			clk_mgr_base->clks.fw_based_mclk_switching = p_state_change_support && new_clocks->fw_based_mclk_switching;

			/* to disable P-State switching, set UCLK min = max */
			if (!clk_mgr_base->clks.p_state_change_support && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
				dcn401_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
						clk_mgr_base->bw_params->clk_table.entries[clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_memclk_levels - 1].memclk_mhz);
		}

		/* Always update saved value, even if new value not set due to P-State switching unsupported. Also check safe_to_lower for FCLK */
		if (safe_to_lower && (clk_mgr_base->clks.fclk_p_state_change_support != clk_mgr_base->clks.fclk_prev_p_state_change_support)) {
			update_fclk = true;
		}

		if (!clk_mgr_base->clks.fclk_p_state_change_support &&
				update_fclk &&
				dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_FCLK)) {
			/* Handle code for sending a message to PMFW that FCLK P-state change is not supported */
			dcn401_smu_send_fclk_pstate_message(clk_mgr, FCLK_PSTATE_NOTSUPPORTED);
		}

		/* Always update saved value, even if new value not set due to P-State switching unsupported */
		if (should_set_clock(safe_to_lower, new_clocks->dramclk_khz, clk_mgr_base->clks.dramclk_khz)) {
			clk_mgr_base->clks.dramclk_khz = new_clocks->dramclk_khz;
			update_uclk = true;
		}

		/* set UCLK to requested value if P-State switching is supported, or to re-enable P-State switching */
		if (clk_mgr_base->clks.p_state_change_support &&
				(update_uclk || !clk_mgr_base->clks.prev_p_state_change_support) &&
				dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
			dcn401_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK, khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz));

		if (clk_mgr_base->clks.num_ways != new_clocks->num_ways &&
				clk_mgr_base->clks.num_ways > new_clocks->num_ways) {
			clk_mgr_base->clks.num_ways = new_clocks->num_ways;
			if (dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
				dcn401_smu_send_cab_for_uclk_message(clk_mgr, clk_mgr_base->clks.num_ways);
		}
	}

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr_base->clks.dppclk_khz)) {
		if (clk_mgr_base->clks.dppclk_khz > new_clocks->dppclk_khz)
			dpp_clock_lowered = true;

		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		clk_mgr_base->clks.actual_dppclk_khz = new_clocks->dppclk_khz;

		if (clk_mgr->smu_present && !dpp_clock_lowered && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DPPCLK))
			clk_mgr_base->clks.actual_dppclk_khz = dcn401_set_hard_min_by_freq_optimized(clk_mgr, PPCLK_DPPCLK, clk_mgr_base->clks.dppclk_khz);
		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;

		update_dispclk = true;
	}

	if (!new_clocks->dtbclk_en && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DTBCLK)) {
		new_clocks->ref_dtbclk_khz = clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz * 1000;
	}

	/* clock limits are received with MHz precision, divide by 1000 to prevent setting clocks at every call */
	if (!dc->debug.disable_dtb_ref_clk_switch &&
			should_set_clock(safe_to_lower, new_clocks->ref_dtbclk_khz / 1000, clk_mgr_base->clks.ref_dtbclk_khz / 1000) &&
			dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DTBCLK)) {
		/* DCCG requires KHz precision for DTBCLK */
		clk_mgr_base->clks.ref_dtbclk_khz =
				dcn401_smu_set_hard_min_by_freq(clk_mgr, PPCLK_DTBCLK, khz_to_mhz_ceil(new_clocks->ref_dtbclk_khz));

		dcn401_update_clocks_update_dtb_dto(clk_mgr, context, clk_mgr_base->clks.ref_dtbclk_khz);
	}

	if (dc->config.forced_clocks == false || (force_reset && safe_to_lower)) {
		if (dpp_clock_lowered) {
			/* if clock is being lowered, increase DTO before lowering refclk */
			dcn401_update_clocks_update_dpp_dto(clk_mgr, context,
					safe_to_lower, clk_mgr_base->clks.dppclk_khz);
			dcn401_update_clocks_update_dentist(clk_mgr, context);
			if (clk_mgr->smu_present && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DPPCLK)) {
				clk_mgr_base->clks.actual_dppclk_khz = dcn401_set_hard_min_by_freq_optimized(clk_mgr, PPCLK_DPPCLK,
						clk_mgr_base->clks.dppclk_khz);
				dcn401_update_clocks_update_dpp_dto(clk_mgr, context, safe_to_lower,
						clk_mgr_base->clks.actual_dppclk_khz);
			}

		} else {
			/* if clock is being raised, increase refclk before lowering DTO */
			if (update_dppclk || update_dispclk)
				dcn401_update_clocks_update_dentist(clk_mgr, context);
			/* There is a check inside dcn20_update_clocks_update_dpp_dto which ensures
			 * that we do not lower dto when it is not safe to lower. We do not need to
			 * compare the current and new dppclk before calling this function.
			 */
			dcn401_update_clocks_update_dpp_dto(clk_mgr, context,
					safe_to_lower, clk_mgr_base->clks.actual_dppclk_khz);
		}
	}

	if (update_dispclk && dmcu && dmcu->funcs->is_dmcu_initialized(dmcu))
		/*update dmcu for wait_loop count*/
		dmcu->funcs->set_psr_wait_loop(dmcu,
				clk_mgr_base->clks.dispclk_khz / 1000 / 7);
}

static uint32_t dcn401_get_vco_frequency_from_reg(struct clk_mgr_internal *clk_mgr)
{
		struct fixed31_32 pll_req;
		uint32_t pll_req_reg = 0;

		/* get FbMult value */
		pll_req_reg = REG_READ(CLK0_CLK_PLL_REQ);

		/* set up a fixed-point number
		 * this works because the int part is on the right edge of the register
		 * and the frac part is on the left edge
		 */
		pll_req = dc_fixpt_from_int(pll_req_reg & clk_mgr->clk_mgr_mask->FbMult_int);
		pll_req.value |= pll_req_reg & clk_mgr->clk_mgr_mask->FbMult_frac;

		/* multiply by REFCLK period */
		pll_req = dc_fixpt_mul_int(pll_req, clk_mgr->dfs_ref_freq_khz);

		return dc_fixpt_floor(pll_req);
}

static void dcn401_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t dprefclk_did = 0;
	uint32_t dcfclk_did = 0;
	uint32_t dtbclk_did = 0;
	uint32_t dispclk_did = 0;
	uint32_t dppclk_did = 0;
	uint32_t target_div = 0;

	/* DFS Slice 0 is used for DISPCLK */
	dispclk_did = REG_READ(CLK0_CLK0_DFS_CNTL);
	/* DFS Slice 1 is used for DPPCLK */
	dppclk_did = REG_READ(CLK0_CLK1_DFS_CNTL);
	/* DFS Slice 2 is used for DPREFCLK */
	dprefclk_did = REG_READ(CLK0_CLK2_DFS_CNTL);
	/* DFS Slice 3 is used for DCFCLK */
	dcfclk_did = REG_READ(CLK0_CLK3_DFS_CNTL);
	/* DFS Slice 4 is used for DTBCLK */
	dtbclk_did = REG_READ(CLK0_CLK4_DFS_CNTL);

	/* Convert DISPCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dispclk_did);
	//Get dispclk in khz
	regs_and_bypass->dispclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DISPCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dppclk_did);
	//Get dppclk in khz
	regs_and_bypass->dppclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DPREFCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dprefclk_did);
	//Get dprefclk in khz
	regs_and_bypass->dprefclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DCFCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dcfclk_did);
	//Get dcfclk in khz
	regs_and_bypass->dcfclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;

	/* Convert DTBCLK DFS Slice DID to divider*/
	target_div = dentist_get_divider_from_did(dtbclk_did);
	//Get dtbclk in khz
	regs_and_bypass->dtbclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz) / target_div;
}

static void dcn401_clock_read_ss_info(struct clk_mgr_internal *clk_mgr)
{
	struct dc_bios *bp = clk_mgr->base.ctx->dc_bios;
	int ss_info_num = bp->funcs->get_ss_entry_number(
			bp, AS_SIGNAL_TYPE_GPU_PLL);

	if (ss_info_num) {
		struct spread_spectrum_info info = { { 0 } };
		enum bp_result result = bp->funcs->get_spread_spectrum_info(
				bp, AS_SIGNAL_TYPE_GPU_PLL, 0, &info);

		/* SSInfo.spreadSpectrumPercentage !=0 would be sign
		 * that SS is enabled
		 */
		if (result == BP_RESULT_OK &&
				info.spread_spectrum_percentage != 0) {
			clk_mgr->ss_on_dprefclk = true;
			clk_mgr->dprefclk_ss_divider = info.spread_percentage_divider;

			if (info.type.CENTER_MODE == 0) {
				/* Currently for DP Reference clock we
				 * need only SS percentage for
				 * downspread
				 */
				clk_mgr->dprefclk_ss_percentage =
						info.spread_spectrum_percentage;
			}
		}
	}
}
static void dcn401_notify_wm_ranges(struct clk_mgr *clk_mgr_base)
{
	unsigned int i;
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	WatermarksExternal_t *table = (WatermarksExternal_t *) clk_mgr->wm_range_table;

	if (!clk_mgr->smu_present)
		return;

	if (!table)
		return;

	memset(table, 0, sizeof(*table));

	/* collect valid ranges, place in pmfw table */
	for (i = 0; i < WM_SET_COUNT; i++)
		if (clk_mgr->base.bw_params->wm_table.nv_entries[i].valid) {
			table->Watermarks.WatermarkRow[i].WmSetting = i;
			table->Watermarks.WatermarkRow[i].Flags = clk_mgr->base.bw_params->wm_table.nv_entries[i].pmfw_breakdown.wm_type;
		}
	dcn30_smu_set_dram_addr_high(clk_mgr, clk_mgr->wm_range_table_addr >> 32);
	dcn30_smu_set_dram_addr_low(clk_mgr, clk_mgr->wm_range_table_addr & 0xFFFFFFFF);
	dcn401_smu_transfer_wm_table_dram_2_smu(clk_mgr);
}

/* Set min memclk to minimum, either constrained by the current mode or DPM0 */
static void dcn401_set_hard_min_memclk(struct clk_mgr *clk_mgr_base, bool current_mode)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present || !dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
		return;

	if (current_mode) {
		if (clk_mgr_base->clks.p_state_change_support)
			dcn401_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz));
		else
			dcn401_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
					clk_mgr_base->bw_params->max_memclk_mhz);
	} else {
		dcn401_smu_set_hard_min_by_freq(clk_mgr, PPCLK_UCLK,
				clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz);
	}
}

/* Set max memclk to highest DPM value */
static void dcn401_set_hard_max_memclk(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present || !dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
		return;

	dcn30_smu_set_hard_max_by_freq(clk_mgr, PPCLK_UCLK,
			clk_mgr_base->bw_params->max_memclk_mhz);
}

/* Get current memclk states, update bounding box */
static void dcn401_get_memclk_states_from_smu(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct clk_limit_num_entries *num_entries_per_clk = &clk_mgr_base->bw_params->clk_table.num_entries_per_clk;
	unsigned int num_levels;

	if (!clk_mgr->smu_present)
		return;

	/* Refresh memclk and fclk states */
	dcn401_init_single_clock(clk_mgr, PPCLK_UCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz,
			&num_entries_per_clk->num_memclk_levels);
	if (num_entries_per_clk->num_memclk_levels) {
		clk_mgr_base->bw_params->max_memclk_mhz =
				clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_memclk_levels - 1].memclk_mhz;
	}

	clk_mgr_base->bw_params->dc_mode_limit.memclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_UCLK);
	if (num_entries_per_clk->num_memclk_levels && clk_mgr_base->bw_params->dc_mode_limit.memclk_mhz ==
			clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_memclk_levels - 1].memclk_mhz)
		clk_mgr_base->bw_params->dc_mode_limit.memclk_mhz = 0;
	clk_mgr_base->bw_params->dc_mode_softmax_memclk = clk_mgr_base->bw_params->dc_mode_limit.memclk_mhz;

	dcn401_init_single_clock(clk_mgr, PPCLK_FCLK,
			&clk_mgr_base->bw_params->clk_table.entries[0].fclk_mhz,
			&num_entries_per_clk->num_fclk_levels);
	clk_mgr_base->bw_params->dc_mode_limit.fclk_mhz = dcn30_smu_get_dc_mode_max_dpm_freq(clk_mgr, PPCLK_FCLK);
	if (num_entries_per_clk->num_fclk_levels && clk_mgr_base->bw_params->dc_mode_limit.fclk_mhz ==
			clk_mgr_base->bw_params->clk_table.entries[num_entries_per_clk->num_fclk_levels - 1].fclk_mhz)
		clk_mgr_base->bw_params->dc_mode_limit.fclk_mhz = 0;

	if (num_entries_per_clk->num_memclk_levels >= num_entries_per_clk->num_fclk_levels) {
		num_levels = num_entries_per_clk->num_memclk_levels;
	} else {
		num_levels = num_entries_per_clk->num_fclk_levels;
	}

	clk_mgr_base->bw_params->clk_table.num_entries = num_levels ? num_levels : 1;

	if (clk_mgr->dpm_present && !num_levels)
		clk_mgr->dpm_present = false;

	/* Refresh bounding box */
	clk_mgr_base->ctx->dc->res_pool->funcs->update_bw_bounding_box(
			clk_mgr->base.ctx->dc, clk_mgr_base->bw_params);
}

static bool dcn401_are_clock_states_equal(struct dc_clocks *a,
					struct dc_clocks *b)
{
	if (a->dispclk_khz != b->dispclk_khz)
		return false;
	else if (a->dppclk_khz != b->dppclk_khz)
		return false;
	else if (a->dcfclk_khz != b->dcfclk_khz)
		return false;
	else if (a->dcfclk_deep_sleep_khz != b->dcfclk_deep_sleep_khz)
		return false;
	else if (a->dramclk_khz != b->dramclk_khz)
		return false;
	else if (a->p_state_change_support != b->p_state_change_support)
		return false;
	else if (a->fclk_p_state_change_support != b->fclk_p_state_change_support)
		return false;

	return true;
}

static void dcn401_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn401_smu_set_pme_workaround(clk_mgr);
}

static bool dcn401_is_smu_present(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	return clk_mgr->smu_present;
}


static int dcn401_get_dtb_ref_freq_khz(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	int dtb_ref_clk_khz = 0;

	if (clk_mgr->smu_present && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DTBCLK)) {
		/* DPM enabled, use currently set value */
		dtb_ref_clk_khz = clk_mgr_base->clks.ref_dtbclk_khz;
	} else {
		/* DPM disabled, so use boot snapshot */
		dtb_ref_clk_khz = clk_mgr_base->boot_snapshot.dtbclk;
	}

	return dtb_ref_clk_khz;
}

static struct clk_mgr_funcs dcn401_funcs = {
		.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
		.get_dtb_ref_clk_frequency = dcn401_get_dtb_ref_freq_khz,
		.update_clocks = dcn401_update_clocks,
		.dump_clk_registers = dcn401_dump_clk_registers,
		.init_clocks = dcn401_init_clocks,
		.notify_wm_ranges = dcn401_notify_wm_ranges,
		.set_hard_min_memclk = dcn401_set_hard_min_memclk,
		.set_hard_max_memclk = dcn401_set_hard_max_memclk,
		.get_memclk_states_from_smu = dcn401_get_memclk_states_from_smu,
		.are_clock_states_equal = dcn401_are_clock_states_equal,
		.enable_pme_wa = dcn401_enable_pme_wa,
		.is_smu_present = dcn401_is_smu_present,
};

struct clk_mgr_internal *dcn401_clk_mgr_construct(
		struct dc_context *ctx,
		struct dccg *dccg)
{
	struct clk_log_info log_info = {0};
	struct dcn401_clk_mgr *clk_mgr401 = kzalloc(sizeof(struct dcn401_clk_mgr), GFP_KERNEL);
	struct clk_mgr_internal *clk_mgr;

	if (!clk_mgr401)
		return NULL;

	clk_mgr = &clk_mgr401->base;
	clk_mgr->base.ctx = ctx;
	clk_mgr->base.funcs = &dcn401_funcs;
	clk_mgr->regs = &clk_mgr_regs_dcn401;
	clk_mgr->clk_mgr_shift = &clk_mgr_shift_dcn401;
	clk_mgr->clk_mgr_mask = &clk_mgr_mask_dcn401;

	clk_mgr->dccg = dccg;
	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;
	clk_mgr->dfs_ref_freq_khz = 100000;

	/* Changed from DCN3.2_clock_frequency doc to match
	 * dcn401_dump_clk_registers from 4 * dentist_vco_freq_khz /
	 * dprefclk DID divider
	 */
	clk_mgr->base.dprefclk_khz = 720000; //TODO update from VBIOS

	/* integer part is now VCO frequency in kHz */
	clk_mgr->base.dentist_vco_freq_khz = dcn401_get_vco_frequency_from_reg(clk_mgr);

	/* in case we don't get a value from the register, use default */
	if (clk_mgr->base.dentist_vco_freq_khz == 0)
		clk_mgr->base.dentist_vco_freq_khz = 4500000; //TODO Update from VBIOS

	dcn401_dump_clk_registers(&clk_mgr->base.boot_snapshot, &clk_mgr->base, &log_info);

	if (ctx->dc->debug.disable_dtb_ref_clk_switch &&
			clk_mgr->base.clks.ref_dtbclk_khz != clk_mgr->base.boot_snapshot.dtbclk) {
		clk_mgr->base.clks.ref_dtbclk_khz = clk_mgr->base.boot_snapshot.dtbclk;
	}

	if (clk_mgr->base.boot_snapshot.dprefclk != 0) {
		clk_mgr->base.dprefclk_khz = clk_mgr->base.boot_snapshot.dprefclk;
	}
	dcn401_clock_read_ss_info(clk_mgr);

	clk_mgr->dfs_bypass_enabled = false;

	clk_mgr->smu_present = false;

	clk_mgr->base.bw_params = kzalloc(sizeof(*clk_mgr->base.bw_params), GFP_KERNEL);
	if (!clk_mgr->base.bw_params) {
		BREAK_TO_DEBUGGER();
		kfree(clk_mgr);
		return NULL;
	}

	/* need physical address of table to give to PMFW */
	clk_mgr->wm_range_table = dm_helpers_allocate_gpu_mem(clk_mgr->base.ctx,
			DC_MEM_ALLOC_TYPE_GART, sizeof(WatermarksExternal_t),
			&clk_mgr->wm_range_table_addr);
	if (!clk_mgr->wm_range_table) {
		BREAK_TO_DEBUGGER();
		kfree(clk_mgr->base.bw_params);
		return NULL;
	}

	return &clk_mgr401->base;

}

void dcn401_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr)
{
	kfree(clk_mgr->base.bw_params);

	if (clk_mgr->wm_range_table)
		dm_helpers_free_gpu_mem(clk_mgr->base.ctx, DC_MEM_ALLOC_TYPE_GART,
				clk_mgr->wm_range_table);
}

