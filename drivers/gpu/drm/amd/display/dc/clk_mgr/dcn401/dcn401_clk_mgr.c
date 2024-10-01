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
#include "dc_state_priv.h"
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
#define mmCLK20_CLK2_CLK2_DFS_CNTL                      0x1B051

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

#define TO_DCN401_CLK_MGR(clk_mgr)\
	container_of(clk_mgr, struct dcn401_clk_mgr, base)

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

static bool dcn401_is_ppclk_idle_dpm_enabled(struct clk_mgr_internal *clk_mgr, PPCLK_e clk)
{
	bool ppclk_idle_dpm_enabled = false;

	switch (clk) {
	case PPCLK_UCLK:
	case PPCLK_FCLK:
		if (ASICREV_IS_GC_12_0_0_A0(clk_mgr->base.ctx->asic_id.hw_internal_rev) &&
				clk_mgr->smu_ver >= 0x681800) {
			ppclk_idle_dpm_enabled = true;
		} else if (ASICREV_IS_GC_12_0_1_A0(clk_mgr->base.ctx->asic_id.hw_internal_rev) &&
				clk_mgr->smu_ver >= 0x661300) {
			ppclk_idle_dpm_enabled = true;
		}
		break;
	default:
		ppclk_idle_dpm_enabled = false;
	}

	ppclk_idle_dpm_enabled &= clk_mgr->smu_present;

	return ppclk_idle_dpm_enabled;
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
	struct clk_limit_num_entries *num_entries_per_clk;
	unsigned int i;

	if (!clk_mgr_base->bw_params)
		return;

	num_entries_per_clk = &clk_mgr_base->bw_params->clk_table.num_entries_per_clk;

	memset(&(clk_mgr_base->clks), 0, sizeof(struct dc_clocks));
	clk_mgr_base->clks.p_state_change_support = true;
	clk_mgr_base->clks.prev_p_state_change_support = true;
	clk_mgr_base->clks.fclk_prev_p_state_change_support = true;
	clk_mgr->smu_present = false;
	clk_mgr->dpm_present = false;

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

static void dcn401_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
		struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
		uint32_t dprefclk_did = 0;
		uint32_t dcfclk_did = 0;
		uint32_t dtbclk_did = 0;
		uint32_t dispclk_did = 0;
		uint32_t dppclk_did = 0;
		uint32_t fclk_did = 0;
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
		/* DFS Slice _ is used for FCLK */
		fclk_did = REG_READ(CLK2_CLK2_DFS_CNTL);

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

		/* Convert DTBCLK DFS Slice DID to divider*/
		target_div = dentist_get_divider_from_did(fclk_did);
		//Get fclk in khz
		regs_and_bypass->fclk = (DENTIST_DIVIDER_RANGE_SCALE_FACTOR
				* clk_mgr->base.dentist_vco_freq_khz) / target_div;
}

static bool dcn401_check_native_scaling(struct pipe_ctx *pipe)
{
	bool is_native_scaling = false;
	int width = pipe->plane_state->src_rect.width;
	int height = pipe->plane_state->src_rect.height;

	if (pipe->stream->timing.h_addressable == width &&
			pipe->stream->timing.v_addressable == height &&
			pipe->plane_state->dst_rect.width == width &&
			pipe->plane_state->dst_rect.height == height)
		is_native_scaling = true;

	return is_native_scaling;
}

static void dcn401_auto_dpm_test_log(
		struct dc_clocks *new_clocks,
		struct clk_mgr_internal *clk_mgr,
		struct dc_state *context)
{
	unsigned int mall_ss_size_bytes;
	int dramclk_khz_override, fclk_khz_override, num_fclk_levels;

	struct pipe_ctx *pipe_ctx_list[MAX_PIPES];
	int active_pipe_count = 0;

	for (int i = 0; i < MAX_PIPES; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream && dc_state_get_pipe_subvp_type(context, pipe_ctx) != SUBVP_PHANTOM) {
			pipe_ctx_list[active_pipe_count] = pipe_ctx;
			active_pipe_count++;
		}
	}

	msleep(5);

	mall_ss_size_bytes = context->bw_ctx.bw.dcn.mall_ss_size_bytes;

	struct clk_log_info log_info = {0};
	struct clk_state_registers_and_bypass clk_register_dump;

	dcn401_dump_clk_registers(&clk_register_dump, &clk_mgr->base, &log_info);

	// Overrides for these clocks in case there is no p_state change support
	dramclk_khz_override = new_clocks->dramclk_khz;
	fclk_khz_override = new_clocks->fclk_khz;

	num_fclk_levels = clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_fclk_levels - 1;

	if (!new_clocks->p_state_change_support)
		dramclk_khz_override = clk_mgr->base.bw_params->max_memclk_mhz * 1000;

	if (!new_clocks->fclk_p_state_change_support)
		fclk_khz_override = clk_mgr->base.bw_params->clk_table.entries[num_fclk_levels].fclk_mhz * 1000;


	////////////////////////////////////////////////////////////////////////////
	//	IMPORTANT: 	When adding more clocks to these logs, do NOT put a newline
	//	 			anywhere other than at the very end of the string.
	//
	//	Formatting example (make sure to have " - " between each entry):
	//
	//				AutoDPMTest: clk1:%d - clk2:%d - clk3:%d - clk4:%d\n"
	////////////////////////////////////////////////////////////////////////////
	if (active_pipe_count > 0 &&
		new_clocks->dramclk_khz > 0 &&
		new_clocks->fclk_khz > 0 &&
		new_clocks->dcfclk_khz > 0 &&
		new_clocks->dppclk_khz > 0) {

		uint32_t pix_clk_list[MAX_PIPES] = {0};
		int p_state_list[MAX_PIPES] = {0};
		int disp_src_width_list[MAX_PIPES] = {0};
		int disp_src_height_list[MAX_PIPES] = {0};
		uint64_t disp_src_refresh_list[MAX_PIPES] = {0};
		bool is_scaled_list[MAX_PIPES] = {0};

		for (int i = 0; i < active_pipe_count; i++) {
			struct pipe_ctx *curr_pipe_ctx = pipe_ctx_list[i];
			uint64_t refresh_rate;

			pix_clk_list[i] = curr_pipe_ctx->stream->timing.pix_clk_100hz;
			p_state_list[i] = curr_pipe_ctx->p_state_type;

			refresh_rate = (curr_pipe_ctx->stream->timing.pix_clk_100hz * (uint64_t)100 +
				curr_pipe_ctx->stream->timing.v_total
				* (uint64_t) curr_pipe_ctx->stream->timing.h_total - (uint64_t)1);
			refresh_rate = div_u64(refresh_rate, curr_pipe_ctx->stream->timing.v_total);
			refresh_rate = div_u64(refresh_rate, curr_pipe_ctx->stream->timing.h_total);
			disp_src_refresh_list[i] = refresh_rate;

			if (curr_pipe_ctx->plane_state) {
				is_scaled_list[i] = !(dcn401_check_native_scaling(curr_pipe_ctx));
				disp_src_width_list[i] = curr_pipe_ctx->plane_state->src_rect.width;
				disp_src_height_list[i] = curr_pipe_ctx->plane_state->src_rect.height;
			}
		}

		DC_LOG_AUTO_DPM_TEST("AutoDPMTest: dramclk:%d - fclk:%d - "
			"dcfclk:%d - dppclk:%d - dispclk_hw:%d - "
			"dppclk_hw:%d - dprefclk_hw:%d - dcfclk_hw:%d - "
			"dtbclk_hw:%d - fclk_hw:%d - pix_clk_0:%d - pix_clk_1:%d - "
			"pix_clk_2:%d - pix_clk_3:%d - mall_ss_size:%d - p_state_type_0:%d - "
			"p_state_type_1:%d - p_state_type_2:%d - p_state_type_3:%d - "
			"pix_width_0:%d - pix_height_0:%d - refresh_rate_0:%lld - is_scaled_0:%d - "
			"pix_width_1:%d - pix_height_1:%d - refresh_rate_1:%lld - is_scaled_1:%d - "
			"pix_width_2:%d - pix_height_2:%d - refresh_rate_2:%lld - is_scaled_2:%d - "
			"pix_width_3:%d - pix_height_3:%d - refresh_rate_3:%lld - is_scaled_3:%d - LOG_END\n",
			dramclk_khz_override,
			fclk_khz_override,
			new_clocks->dcfclk_khz,
			new_clocks->dppclk_khz,
			clk_register_dump.dispclk,
			clk_register_dump.dppclk,
			clk_register_dump.dprefclk,
			clk_register_dump.dcfclk,
			clk_register_dump.dtbclk,
			clk_register_dump.fclk,
			pix_clk_list[0], pix_clk_list[1], pix_clk_list[3], pix_clk_list[2],
			mall_ss_size_bytes,
			p_state_list[0], p_state_list[1], p_state_list[2], p_state_list[3],
			disp_src_width_list[0], disp_src_height_list[0], disp_src_refresh_list[0], is_scaled_list[0],
			disp_src_width_list[1], disp_src_height_list[1], disp_src_refresh_list[1], is_scaled_list[1],
			disp_src_width_list[2], disp_src_height_list[2], disp_src_refresh_list[2], is_scaled_list[2],
			disp_src_width_list[3], disp_src_height_list[3], disp_src_refresh_list[3], is_scaled_list[3]);
	}
}

static void dcn401_update_clocks_update_dtb_dto(struct clk_mgr_internal *clk_mgr,
			struct dc_state *context,
			int ref_dtbclk_khz)
{
	int i;
	struct dccg *dccg = clk_mgr->dccg;
	struct pipe_ctx *otg_master;
	bool use_hpo_encoder;


	for (i = 0; i < context->stream_count; i++) {
		otg_master = resource_get_otg_master_for_stream(
				&context->res_ctx, context->streams[i]);
		ASSERT(otg_master);
		ASSERT(otg_master->clock_source);
		ASSERT(otg_master->clock_source->funcs->program_pix_clk);
		ASSERT(otg_master->stream_res.pix_clk_params.controller_id >= CONTROLLER_ID_D0);

		use_hpo_encoder = dccg->ctx->dc->link_srv->dp_is_128b_132b_signal(otg_master);
		if (!use_hpo_encoder)
			continue;

		if (otg_master->stream_res.pix_clk_params.controller_id > CONTROLLER_ID_UNDEFINED)
			otg_master->clock_source->funcs->program_pix_clk(
				otg_master->clock_source,
				&otg_master->stream_res.pix_clk_params,
				dccg->ctx->dc->link_srv->dp_get_encoding_format(
					&otg_master->link_config.dp_link_settings),
				&otg_master->pll_settings);
	}
}

static void dcn401_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr,
		struct dc_state *context, bool safe_to_lower, int ref_dppclk_khz)
{
	int i;

	clk_mgr->dccg->ref_dppclk = ref_dppclk_khz;
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
	uint32_t dentist_dispclk_wdivider_readback = 0;
	struct dc *dc = clk_mgr->base.ctx->dc;

	if (clk_mgr->base.clks.dispclk_khz == 0)
		return;

	new_disp_divider = DENTIST_DIVIDER_RANGE_SCALE_FACTOR
			* clk_mgr->base.dentist_vco_freq_khz / clk_mgr->base.clks.dispclk_khz;

	new_dispclk_wdivider = dentist_get_did_from_divider(new_disp_divider);

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

static void dcn401_update_clocks_legacy(struct clk_mgr *clk_mgr_base,
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
			dcn401_smu_set_num_of_displays(clk_mgr, display_count);

		clk_mgr_base->clks.fclk_prev_p_state_change_support = clk_mgr_base->clks.fclk_p_state_change_support;

		total_plane_count = clk_mgr_helper_get_active_plane_cnt(dc, context);
		fclk_p_state_change_support = new_clocks->fclk_p_state_change_support || (total_plane_count == 0);

		if (should_update_pstate_support(safe_to_lower, fclk_p_state_change_support, clk_mgr_base->clks.fclk_p_state_change_support)) {
			clk_mgr_base->clks.fclk_p_state_change_support = fclk_p_state_change_support;

			/* To enable FCLK P-state switching, send PSTATE_SUPPORTED message to PMFW */
			if (clk_mgr_base->clks.fclk_p_state_change_support) {
				/* Handle the code for sending a message to PMFW that FCLK P-state change is supported */
				dcn401_smu_send_fclk_pstate_message(clk_mgr, true);
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
				dcn401_smu_set_min_deep_sleep_dcef_clk(clk_mgr, khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_deep_sleep_khz));
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
		if (should_update_pstate_support(safe_to_lower, p_state_change_support, clk_mgr_base->clks.prev_p_state_change_support)) {
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
			dcn401_smu_send_fclk_pstate_message(clk_mgr, false);
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

		if (clk_mgr->smu_present && dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DISPCLK))
			clk_mgr_base->clks.actual_dispclk_khz = dcn401_set_hard_min_by_freq_optimized(clk_mgr, PPCLK_DISPCLK, clk_mgr_base->clks.dispclk_khz);

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

static void dcn401_execute_block_sequence(struct clk_mgr *clk_mgr_base, unsigned int num_steps)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn401_clk_mgr *clk_mgr401 = TO_DCN401_CLK_MGR(clk_mgr_internal);

	unsigned int i;
	union dcn401_clk_mgr_block_sequence_params *params;

	/* execute sequence */
	for (i = 0; i < num_steps; i++) {
		params = &clk_mgr401->block_sequence[i].params;

		switch (clk_mgr401->block_sequence[i].func) {
		case CLK_MGR401_READ_CLOCKS_FROM_DENTIST:
			dcn2_read_clocks_from_hw_dentist(clk_mgr_base);
			break;
		case CLK_MGR401_UPDATE_NUM_DISPLAYS:
			dcn401_smu_set_num_of_displays(clk_mgr_internal,
					params->update_num_displays_params.num_displays);
			break;
		case CLK_MGR401_UPDATE_HARDMIN_PPCLK:
			if (params->update_hardmin_params.response)
				*params->update_hardmin_params.response = dcn401_smu_set_hard_min_by_freq(
						clk_mgr_internal,
						params->update_hardmin_params.ppclk,
						params->update_hardmin_params.freq_mhz);
			else
				dcn401_smu_set_hard_min_by_freq(clk_mgr_internal,
						params->update_hardmin_params.ppclk,
						params->update_hardmin_params.freq_mhz);
			break;
		case CLK_MGR401_UPDATE_HARDMIN_PPCLK_OPTIMIZED:
			if (params->update_hardmin_optimized_params.response)
				*params->update_hardmin_optimized_params.response = dcn401_set_hard_min_by_freq_optimized(
						clk_mgr_internal,
						params->update_hardmin_optimized_params.ppclk,
						params->update_hardmin_optimized_params.freq_khz);
			else
				dcn401_set_hard_min_by_freq_optimized(clk_mgr_internal,
						params->update_hardmin_optimized_params.ppclk,
						params->update_hardmin_optimized_params.freq_khz);
			break;
		case CLK_MGR401_UPDATE_ACTIVE_HARDMINS:
			dcn401_smu_set_active_uclk_fclk_hardmin(
					clk_mgr_internal,
					params->update_idle_hardmin_params.uclk_mhz,
					params->update_idle_hardmin_params.fclk_mhz);
			break;
		case CLK_MGR401_UPDATE_IDLE_HARDMINS:
			dcn401_smu_set_idle_uclk_fclk_hardmin(
					clk_mgr_internal,
					params->update_idle_hardmin_params.uclk_mhz,
					params->update_idle_hardmin_params.fclk_mhz);
			break;
		case CLK_MGR401_UPDATE_DEEP_SLEEP_DCFCLK:
			dcn401_smu_set_min_deep_sleep_dcef_clk(
					clk_mgr_internal,
					params->update_deep_sleep_dcfclk_params.freq_mhz);
			break;
		case CLK_MGR401_UPDATE_FCLK_PSTATE_SUPPORT:
			dcn401_smu_send_fclk_pstate_message(
					clk_mgr_internal,
					params->update_pstate_support_params.support);
			break;
		case CLK_MGR401_UPDATE_UCLK_PSTATE_SUPPORT:
			dcn401_smu_send_uclk_pstate_message(
					clk_mgr_internal,
					params->update_pstate_support_params.support);
			break;
		case CLK_MGR401_UPDATE_CAB_FOR_UCLK:
			dcn401_smu_send_cab_for_uclk_message(
				clk_mgr_internal,
				params->update_cab_for_uclk_params.num_ways);
			break;
		case CLK_MGR401_UPDATE_WAIT_FOR_DMUB_ACK:
			dcn401_smu_wait_for_dmub_ack_mclk(
					clk_mgr_internal,
					params->update_wait_for_dmub_ack_params.enable);
			break;
		case CLK_MGR401_INDICATE_DRR_STATUS:
			dcn401_smu_indicate_drr_status(
					clk_mgr_internal,
					params->indicate_drr_status_params.mod_drr_for_pstate);
			break;
		case CLK_MGR401_UPDATE_DPPCLK_DTO:
			dcn401_update_clocks_update_dpp_dto(
					clk_mgr_internal,
					params->update_dppclk_dto_params.context,
					params->update_dppclk_dto_params.safe_to_lower,
					*params->update_dppclk_dto_params.ref_dppclk_khz);
			break;
		case CLK_MGR401_UPDATE_DTBCLK_DTO:
			dcn401_update_clocks_update_dtb_dto(
					clk_mgr_internal,
					params->update_dtbclk_dto_params.context,
					*params->update_dtbclk_dto_params.ref_dtbclk_khz);
			break;
		case CLK_MGR401_UPDATE_DENTIST:
			dcn401_update_clocks_update_dentist(
					clk_mgr_internal,
					params->update_dentist_params.context);
			break;
		case CLK_MGR401_UPDATE_PSR_WAIT_LOOP:
			params->update_psr_wait_loop_params.dmcu->funcs->set_psr_wait_loop(
					params->update_psr_wait_loop_params.dmcu,
					params->update_psr_wait_loop_params.wait);
			break;
		default:
			/* this should never happen */
			BREAK_TO_DEBUGGER();
			break;
		}
	}
}

static unsigned int dcn401_build_update_bandwidth_clocks_sequence(
		struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		struct dc_clocks *new_clocks,
		bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn401_clk_mgr *clk_mgr401 = TO_DCN401_CLK_MGR(clk_mgr_internal);
	struct dc *dc = clk_mgr_base->ctx->dc;
	struct dcn401_clk_mgr_block_sequence *block_sequence = clk_mgr401->block_sequence;
	bool enter_display_off = false;
	bool update_active_fclk = false;
	bool update_active_uclk = false;
	bool update_idle_fclk = false;
	bool update_idle_uclk = false;
	bool is_idle_dpm_enabled = dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_UCLK) &&
			dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_FCLK) &&
			dcn401_is_ppclk_idle_dpm_enabled(clk_mgr_internal, PPCLK_UCLK) &&
			dcn401_is_ppclk_idle_dpm_enabled(clk_mgr_internal, PPCLK_FCLK);
	int total_plane_count = clk_mgr_helper_get_active_plane_cnt(dc, context);
	int active_uclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz);
	int active_fclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.fclk_khz);
	int idle_uclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.idle_dramclk_khz);
	int idle_fclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.idle_fclk_khz);

	unsigned int num_steps = 0;

	int display_count;
	bool fclk_p_state_change_support, uclk_p_state_change_support;

	/* CLK_MGR401_UPDATE_NUM_DISPLAYS */
	if (clk_mgr_internal->smu_present) {
		display_count = clk_mgr_helper_get_active_display_cnt(dc, context);

		if (display_count == 0)
			enter_display_off = true;

		if (enter_display_off == safe_to_lower) {
			block_sequence[num_steps].params.update_num_displays_params.num_displays = display_count;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_NUM_DISPLAYS;
			num_steps++;
		}
	}

	/* CLK_MGR401_UPDATE_FCLK_PSTATE_SUPPORT */
	clk_mgr_base->clks.fclk_prev_p_state_change_support = clk_mgr_base->clks.fclk_p_state_change_support;
	fclk_p_state_change_support = new_clocks->fclk_p_state_change_support || (total_plane_count == 0);
	if (should_update_pstate_support(safe_to_lower, fclk_p_state_change_support, clk_mgr_base->clks.fclk_prev_p_state_change_support)) {
		clk_mgr_base->clks.fclk_p_state_change_support = fclk_p_state_change_support;
		update_active_fclk = true;
		update_idle_fclk = true;

		/* To enable FCLK P-state switching, send PSTATE_SUPPORTED message to PMFW */
		if (clk_mgr_base->clks.fclk_p_state_change_support) {
			/* Handle the code for sending a message to PMFW that FCLK P-state change is supported */
			if (dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_FCLK)) {
				block_sequence[num_steps].params.update_pstate_support_params.support = true;
				block_sequence[num_steps].func = CLK_MGR401_UPDATE_FCLK_PSTATE_SUPPORT;
				num_steps++;
			}
		}
	}

	if (!clk_mgr_base->clks.fclk_p_state_change_support && dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_FCLK)) {
		/* when P-State switching disabled, set UCLK min = max */
		idle_fclk_mhz =
				clk_mgr_base->bw_params->clk_table.entries[clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_fclk_levels - 1].fclk_mhz;
		active_fclk_mhz = idle_fclk_mhz;
	}

	/* UPDATE DCFCLK */
	if (dc->debug.force_min_dcfclk_mhz > 0)
		new_clocks->dcfclk_khz = (new_clocks->dcfclk_khz > (dc->debug.force_min_dcfclk_mhz * 1000)) ?
				new_clocks->dcfclk_khz : (dc->debug.force_min_dcfclk_mhz * 1000);

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz, clk_mgr_base->clks.dcfclk_khz)) {
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
		if (dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DCFCLK)) {
			block_sequence[num_steps].params.update_hardmin_params.ppclk = PPCLK_DCFCLK;
			block_sequence[num_steps].params.update_hardmin_params.freq_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_khz);
			block_sequence[num_steps].params.update_hardmin_params.response = NULL;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_HARDMIN_PPCLK;
			num_steps++;
		}
	}

	/* CLK_MGR401_UPDATE_DEEP_SLEEP_DCFCLK */
	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_deep_sleep_khz, clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
		if (dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DCFCLK)) {
			block_sequence[num_steps].params.update_deep_sleep_dcfclk_params.freq_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.dcfclk_deep_sleep_khz);
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_DEEP_SLEEP_DCFCLK;
			num_steps++;
		}
	}

	/* SOCCLK */
	if (should_set_clock(safe_to_lower, new_clocks->socclk_khz, clk_mgr_base->clks.socclk_khz))
		/* We don't actually care about socclk, don't notify SMU of hard min */
		clk_mgr_base->clks.socclk_khz = new_clocks->socclk_khz;

	/* UCLK */
	if (new_clocks->fw_based_mclk_switching != clk_mgr_base->clks.fw_based_mclk_switching &&
			new_clocks->fw_based_mclk_switching) {
		/* enable FAMS features */
		clk_mgr_base->clks.fw_based_mclk_switching = new_clocks->fw_based_mclk_switching;

		block_sequence[num_steps].params.update_wait_for_dmub_ack_params.enable = clk_mgr_base->clks.fw_based_mclk_switching;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_WAIT_FOR_DMUB_ACK;
		num_steps++;

		block_sequence[num_steps].params.indicate_drr_status_params.mod_drr_for_pstate = clk_mgr_base->clks.fw_based_mclk_switching;
		block_sequence[num_steps].func = CLK_MGR401_INDICATE_DRR_STATUS;
		num_steps++;
	}

	/* CLK_MGR401_UPDATE_CAB_FOR_UCLK */
	clk_mgr_base->clks.prev_num_ways = clk_mgr_base->clks.num_ways;
	if (clk_mgr_base->clks.num_ways != new_clocks->num_ways &&
			clk_mgr_base->clks.num_ways < new_clocks->num_ways) {
		/* increase num ways for subvp */
		clk_mgr_base->clks.num_ways = new_clocks->num_ways;
		if (dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_UCLK)) {
			block_sequence[num_steps].params.update_cab_for_uclk_params.num_ways = clk_mgr_base->clks.num_ways;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_CAB_FOR_UCLK;
			num_steps++;
		}
	}

	clk_mgr_base->clks.prev_p_state_change_support = clk_mgr_base->clks.p_state_change_support;
	uclk_p_state_change_support = new_clocks->p_state_change_support || (total_plane_count == 0);
	if (should_update_pstate_support(safe_to_lower, uclk_p_state_change_support, clk_mgr_base->clks.prev_p_state_change_support)) {
		clk_mgr_base->clks.p_state_change_support = uclk_p_state_change_support;
		update_active_uclk = true;
		update_idle_uclk = true;

		if (clk_mgr_base->clks.p_state_change_support) {
			/* enable UCLK switching  */
			if (dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_UCLK)) {
				block_sequence[num_steps].params.update_pstate_support_params.support = true;
				block_sequence[num_steps].func = CLK_MGR401_UPDATE_UCLK_PSTATE_SUPPORT;
				num_steps++;
			}
		}
	}

	if (!clk_mgr_base->clks.p_state_change_support && dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_UCLK)) {
		/* when P-State switching disabled, set UCLK min = max */
		if (dc->clk_mgr->dc_mode_softmax_enabled) {
			/* will never have the functional UCLK min above the softmax
			* since we calculate mode support based on softmax being the max UCLK
			* frequency.
			*/
			active_uclk_mhz = clk_mgr_base->bw_params->dc_mode_softmax_memclk;
		} else {
			active_uclk_mhz = clk_mgr_base->bw_params->max_memclk_mhz;
		}
		idle_uclk_mhz = active_uclk_mhz;
	}

	/* Always update saved value, even if new value not set due to P-State switching unsupported */
	if (should_set_clock(safe_to_lower, new_clocks->dramclk_khz, clk_mgr_base->clks.dramclk_khz)) {
		clk_mgr_base->clks.dramclk_khz = new_clocks->dramclk_khz;

		if (clk_mgr_base->clks.p_state_change_support) {
			update_active_uclk = true;
			active_uclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.dramclk_khz);
		}
	}

	if (should_set_clock(safe_to_lower, new_clocks->idle_dramclk_khz, clk_mgr_base->clks.idle_dramclk_khz)) {
		clk_mgr_base->clks.idle_dramclk_khz = new_clocks->idle_dramclk_khz;

		if (clk_mgr_base->clks.p_state_change_support) {
			update_idle_uclk = true;
			idle_uclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.idle_dramclk_khz);
		}
	}

	/* FCLK */
	/* Always update saved value, even if new value not set due to P-State switching unsupported */
	if (should_set_clock(safe_to_lower, new_clocks->fclk_khz, clk_mgr_base->clks.fclk_khz)) {
		clk_mgr_base->clks.fclk_khz = new_clocks->fclk_khz;

		if (clk_mgr_base->clks.fclk_p_state_change_support) {
			update_active_fclk = true;
			active_fclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.fclk_khz);
		}
	}

	if (should_set_clock(safe_to_lower, new_clocks->idle_fclk_khz, clk_mgr_base->clks.idle_fclk_khz)) {
		clk_mgr_base->clks.idle_fclk_khz = new_clocks->idle_fclk_khz;

		if (clk_mgr_base->clks.fclk_p_state_change_support) {
			update_idle_fclk = true;
			idle_fclk_mhz = khz_to_mhz_ceil(clk_mgr_base->clks.idle_fclk_khz);
		}
	}

	/* When idle DPM is enabled, need to send active and idle hardmins separately */
	/* CLK_MGR401_UPDATE_ACTIVE_HARDMINS */
	if ((update_active_uclk || update_active_fclk) && is_idle_dpm_enabled) {
		block_sequence[num_steps].params.update_idle_hardmin_params.uclk_mhz = active_uclk_mhz;
		block_sequence[num_steps].params.update_idle_hardmin_params.fclk_mhz = active_fclk_mhz;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_ACTIVE_HARDMINS;
		num_steps++;
	}

	/* CLK_MGR401_UPDATE_IDLE_HARDMINS */
	if ((update_idle_uclk || update_idle_fclk) && is_idle_dpm_enabled) {
		block_sequence[num_steps].params.update_idle_hardmin_params.uclk_mhz = idle_uclk_mhz;
		block_sequence[num_steps].params.update_idle_hardmin_params.fclk_mhz = idle_fclk_mhz;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_IDLE_HARDMINS;
		num_steps++;
	}

	/* set UCLK to requested value if P-State switching is supported, or to re-enable P-State switching */
	if (update_active_uclk || update_idle_uclk) {
		if (!is_idle_dpm_enabled) {
			block_sequence[num_steps].params.update_hardmin_params.ppclk = PPCLK_UCLK;
			block_sequence[num_steps].params.update_hardmin_params.freq_mhz = active_uclk_mhz;
			block_sequence[num_steps].params.update_hardmin_params.response = NULL;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_HARDMIN_PPCLK;
			num_steps++;
		}

		/* disable UCLK P-State support if needed */
		if (!uclk_p_state_change_support &&
				should_update_pstate_support(safe_to_lower, uclk_p_state_change_support, clk_mgr_base->clks.prev_p_state_change_support) &&
				dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_UCLK)) {
			block_sequence[num_steps].params.update_pstate_support_params.support = false;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_UCLK_PSTATE_SUPPORT;
			num_steps++;
		}
	}

	/* set FCLK to requested value if P-State switching is supported, or to re-enable P-State switching */
	if (update_active_fclk || update_idle_fclk) {
		/* No need to send active FCLK hardmin, automatically set based on DCFCLK */
		// if (!is_idle_dpm_enabled) {
		// 	block_sequence[*num_steps].update_hardmin_params.clk_mgr = clk_mgr;
		// 	block_sequence[*num_steps].update_hardmin_params.ppclk = PPCLK_FCLK;
		// 	block_sequence[*num_steps].update_hardmin_params.freq_mhz = active_fclk_mhz;
		// 	block_sequence[*num_steps].update_hardmin_params.response = NULL;
		// 	block_sequence[*num_steps].func = CLK_MGR401_UPDATE_HARDMIN_PPCLK;
		// 	(*num_steps)++;
		// }

		/* disable FCLK P-State support if needed */
		if (!fclk_p_state_change_support &&
				should_update_pstate_support(safe_to_lower, fclk_p_state_change_support, clk_mgr_base->clks.fclk_prev_p_state_change_support) &&
				dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_FCLK)) {
			block_sequence[num_steps].params.update_pstate_support_params.support = false;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_FCLK_PSTATE_SUPPORT;
			num_steps++;
		}
	}

	if (new_clocks->fw_based_mclk_switching != clk_mgr_base->clks.fw_based_mclk_switching &&
			safe_to_lower && !new_clocks->fw_based_mclk_switching) {
		/* disable FAMS features */
		clk_mgr_base->clks.fw_based_mclk_switching = new_clocks->fw_based_mclk_switching;

		block_sequence[num_steps].params.update_wait_for_dmub_ack_params.enable = clk_mgr_base->clks.fw_based_mclk_switching;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_WAIT_FOR_DMUB_ACK;
		num_steps++;

		block_sequence[num_steps].params.indicate_drr_status_params.mod_drr_for_pstate = clk_mgr_base->clks.fw_based_mclk_switching;
		block_sequence[num_steps].func = CLK_MGR401_INDICATE_DRR_STATUS;
		num_steps++;
	}

	/* CLK_MGR401_UPDATE_CAB_FOR_UCLK */
	if (clk_mgr_base->clks.num_ways != new_clocks->num_ways &&
			safe_to_lower && clk_mgr_base->clks.num_ways > new_clocks->num_ways) {
		/* decrease num ways for subvp */
		clk_mgr_base->clks.num_ways = new_clocks->num_ways;
		if (dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_UCLK)) {
			block_sequence[num_steps].params.update_cab_for_uclk_params.num_ways = clk_mgr_base->clks.num_ways;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_CAB_FOR_UCLK;
			num_steps++;
		}
	}

	return num_steps;
}

static unsigned int dcn401_build_update_display_clocks_sequence(
		struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		struct dc_clocks *new_clocks,
		bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn401_clk_mgr *clk_mgr401 = TO_DCN401_CLK_MGR(clk_mgr_internal);
	struct dc *dc = clk_mgr_base->ctx->dc;
	struct dmcu *dmcu = clk_mgr_base->ctx->dc->res_pool->dmcu;
	struct dcn401_clk_mgr_block_sequence *block_sequence = clk_mgr401->block_sequence;
	bool force_reset = false;
	bool update_dispclk = false;
	bool update_dppclk = false;
	bool dppclk_lowered = false;

	unsigned int num_steps = 0;

	/* CLK_MGR401_READ_CLOCKS_FROM_DENTIST */
	if (clk_mgr_base->clks.dispclk_khz == 0 ||
			(dc->debug.force_clock_mode & 0x1)) {
		/* This is from resume or boot up, if forced_clock cfg option used,
		 * we bypass program dispclk and DPPCLK, but need set them for S3.
		 * Force_clock_mode 0x1:  force reset the clock even it is the same clock
		 * as long as it is in Passive level.
		 */
		force_reset = true;

		clk_mgr_base->clks.dispclk_khz = clk_mgr_base->boot_snapshot.dispclk;
		clk_mgr_base->clks.actual_dispclk_khz = clk_mgr_base->clks.dispclk_khz;

		clk_mgr_base->clks.dppclk_khz = clk_mgr_base->boot_snapshot.dppclk;
		clk_mgr_base->clks.actual_dppclk_khz = clk_mgr_base->clks.dppclk_khz;
	}

	/* DTBCLK */
	if (!new_clocks->dtbclk_en && dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DTBCLK)) {
		new_clocks->ref_dtbclk_khz = clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz * 1000;
	}

	/* clock limits are received with MHz precision, divide by 1000 to prevent setting clocks at every call */
	if (!dc->debug.disable_dtb_ref_clk_switch &&
			should_set_clock(safe_to_lower, new_clocks->ref_dtbclk_khz / 1000, clk_mgr_base->clks.ref_dtbclk_khz / 1000) && //TODO these should be ceiled
			dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DTBCLK)) {
		/* DCCG requires KHz precision for DTBCLK */
		block_sequence[num_steps].params.update_hardmin_params.ppclk = PPCLK_DTBCLK;
		block_sequence[num_steps].params.update_hardmin_params.freq_mhz = khz_to_mhz_ceil(new_clocks->ref_dtbclk_khz);
		block_sequence[num_steps].params.update_hardmin_params.response = &clk_mgr_base->clks.ref_dtbclk_khz;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_HARDMIN_PPCLK;
		num_steps++;

		/* Update DTO in DCCG */
		block_sequence[num_steps].params.update_dtbclk_dto_params.context = context;
		block_sequence[num_steps].params.update_dtbclk_dto_params.ref_dtbclk_khz = &clk_mgr_base->clks.ref_dtbclk_khz;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_DTBCLK_DTO;
		num_steps++;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dppclk_khz, clk_mgr_base->clks.dppclk_khz)) {
		if (clk_mgr_base->clks.dppclk_khz > new_clocks->dppclk_khz)
			dppclk_lowered = true;

		clk_mgr_base->clks.dppclk_khz = new_clocks->dppclk_khz;
		clk_mgr_base->clks.actual_dppclk_khz = new_clocks->dppclk_khz;

		update_dppclk = true;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dispclk_khz, clk_mgr_base->clks.dispclk_khz)) {
		clk_mgr_base->clks.dispclk_khz = new_clocks->dispclk_khz;

		block_sequence[num_steps].params.update_hardmin_optimized_params.ppclk = PPCLK_DISPCLK;
		block_sequence[num_steps].params.update_hardmin_optimized_params.freq_khz = clk_mgr_base->clks.dispclk_khz;
		block_sequence[num_steps].params.update_hardmin_optimized_params.response = &clk_mgr_base->clks.actual_dispclk_khz;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_HARDMIN_PPCLK_OPTIMIZED;
		num_steps++;

		update_dispclk = true;
	}

	if (dc->config.forced_clocks == false || (force_reset && safe_to_lower)) {
		if (dppclk_lowered) {
			/* if clock is being lowered, increase DTO before lowering refclk */
			block_sequence[num_steps].params.update_dppclk_dto_params.context = context;
			block_sequence[num_steps].params.update_dppclk_dto_params.ref_dppclk_khz = &clk_mgr_base->clks.dppclk_khz;
			block_sequence[num_steps].params.update_dppclk_dto_params.safe_to_lower = safe_to_lower;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_DPPCLK_DTO;
			num_steps++;

			block_sequence[num_steps].params.update_dentist_params.context = context;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_DENTIST;
			num_steps++;

			if (dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DPPCLK)) {
				block_sequence[num_steps].params.update_hardmin_optimized_params.ppclk = PPCLK_DPPCLK;
				block_sequence[num_steps].params.update_hardmin_optimized_params.freq_khz = clk_mgr_base->clks.dppclk_khz;
				block_sequence[num_steps].params.update_hardmin_optimized_params.response = &clk_mgr_base->clks.actual_dppclk_khz;
				block_sequence[num_steps].func = CLK_MGR401_UPDATE_HARDMIN_PPCLK_OPTIMIZED;
				num_steps++;

				block_sequence[num_steps].params.update_dppclk_dto_params.context = context;
				block_sequence[num_steps].params.update_dppclk_dto_params.ref_dppclk_khz = &clk_mgr_base->clks.actual_dppclk_khz;
				block_sequence[num_steps].params.update_dppclk_dto_params.safe_to_lower = safe_to_lower;
				block_sequence[num_steps].func = CLK_MGR401_UPDATE_DPPCLK_DTO;
				num_steps++;
			}
		} else {
			/* if clock is being raised, increase refclk before lowering DTO */
			if (update_dppclk && dcn401_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DPPCLK)) {
				block_sequence[num_steps].params.update_hardmin_optimized_params.ppclk = PPCLK_DPPCLK;
				block_sequence[num_steps].params.update_hardmin_optimized_params.freq_khz = clk_mgr_base->clks.dppclk_khz;
				block_sequence[num_steps].params.update_hardmin_optimized_params.response = &clk_mgr_base->clks.actual_dppclk_khz;
				block_sequence[num_steps].func = CLK_MGR401_UPDATE_HARDMIN_PPCLK_OPTIMIZED;
				num_steps++;
			}

			if (update_dppclk || update_dispclk) {
				block_sequence[num_steps].params.update_dentist_params.context = context;
				block_sequence[num_steps].func = CLK_MGR401_UPDATE_DENTIST;
				num_steps++;
			}

			block_sequence[num_steps].params.update_dppclk_dto_params.context = context;
			block_sequence[num_steps].params.update_dppclk_dto_params.ref_dppclk_khz = &clk_mgr_base->clks.actual_dppclk_khz;
			block_sequence[num_steps].params.update_dppclk_dto_params.safe_to_lower = safe_to_lower;
			block_sequence[num_steps].func = CLK_MGR401_UPDATE_DPPCLK_DTO;
			num_steps++;
		}
	}

	if (update_dispclk && dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
		/*update dmcu for wait_loop count*/
		block_sequence[num_steps].params.update_psr_wait_loop_params.dmcu = dmcu;
		block_sequence[num_steps].params.update_psr_wait_loop_params.wait = clk_mgr_base->clks.dispclk_khz / 1000 / 7;
		block_sequence[num_steps].func = CLK_MGR401_UPDATE_PSR_WAIT_LOOP;
		num_steps++;
	}

	return num_steps;
}

static void dcn401_update_clocks(struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		bool safe_to_lower)
{
	struct dc *dc = clk_mgr_base->ctx->dc;

	unsigned int num_steps = 0;

	if (dc->debug.enable_legacy_clock_update) {
		dcn401_update_clocks_legacy(clk_mgr_base, context, safe_to_lower);
		return;
	}

	/* build bandwidth related clocks update sequence */
	num_steps = dcn401_build_update_bandwidth_clocks_sequence(clk_mgr_base,
			context,
			&context->bw_ctx.bw.dcn.clk,
			safe_to_lower);

	/* execute sequence */
	dcn401_execute_block_sequence(clk_mgr_base,	num_steps);

	/* build display related clocks update sequence */
	num_steps = dcn401_build_update_display_clocks_sequence(clk_mgr_base,
			context,
			&context->bw_ctx.bw.dcn.clk,
			safe_to_lower);

	/* execute sequence */
	dcn401_execute_block_sequence(clk_mgr_base,	num_steps);

	if (dc->config.enable_auto_dpm_test_logs)
		dcn401_auto_dpm_test_log(&context->bw_ctx.bw.dcn.clk, TO_CLK_MGR_INTERNAL(clk_mgr_base), context);

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
	const struct dc *dc = clk_mgr->base.ctx->dc;
	struct dc_state *context = dc->current_state;
	struct dc_clocks new_clocks;
	int num_steps;

	if (!clk_mgr->smu_present || !dcn401_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
		return;

	/* build clock update */
	memcpy(&new_clocks, &clk_mgr_base->clks, sizeof(struct dc_clocks));

	if (current_mode) {
		new_clocks.dramclk_khz = context->bw_ctx.bw.dcn.clk.dramclk_khz;
		new_clocks.idle_dramclk_khz = context->bw_ctx.bw.dcn.clk.idle_dramclk_khz;
		new_clocks.p_state_change_support = context->bw_ctx.bw.dcn.clk.p_state_change_support;
	} else {
		new_clocks.dramclk_khz = clk_mgr_base->bw_params->clk_table.entries[0].memclk_mhz * 1000;
		new_clocks.idle_dramclk_khz = new_clocks.dramclk_khz;
		new_clocks.p_state_change_support = true;
	}

	num_steps = dcn401_build_update_bandwidth_clocks_sequence(clk_mgr_base,
			context,
			&new_clocks,
			true);

	/* execute sequence */
	dcn401_execute_block_sequence(clk_mgr_base,	num_steps);
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

static int dcn401_get_dispclk_from_dentist(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t dispclk_wdivider;
	int disp_divider;

	REG_GET(DENTIST_DISPCLK_CNTL, DENTIST_DISPCLK_WDIVIDER, &dispclk_wdivider);
	disp_divider = dentist_get_divider_from_did(dispclk_wdivider);

	/* Return DISPCLK freq in Khz */
	if (disp_divider)
		return (DENTIST_DIVIDER_RANGE_SCALE_FACTOR * clk_mgr->base.dentist_vco_freq_khz) / disp_divider;

	return 0;
}

static struct clk_mgr_funcs dcn401_funcs = {
		.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
		.get_dtb_ref_clk_frequency = dcn401_get_dtb_ref_freq_khz,
		.update_clocks = dcn401_update_clocks,
		.dump_clk_registers = dcn401_dump_clk_registers,
		.init_clocks = dcn401_init_clocks,
		.notify_wm_ranges = dcn401_notify_wm_ranges,
		.set_hard_min_memclk = dcn401_set_hard_min_memclk,
		.get_memclk_states_from_smu = dcn401_get_memclk_states_from_smu,
		.are_clock_states_equal = dcn401_are_clock_states_equal,
		.enable_pme_wa = dcn401_enable_pme_wa,
		.is_smu_present = dcn401_is_smu_present,
		.get_dispclk_from_dentist = dcn401_get_dispclk_from_dentist,
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

