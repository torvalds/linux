// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dccg.h"
#include "clk_mgr_internal.h"
#include "dcn60/dcn60_clk_mgr_smu_msg.h"
#include "hw_sequencer.h"
#include "dce100/dce_clk_mgr.h"
#include "dcn20/dcn20_clk_mgr.h"
#include "dcn401/dcn401_clk_mgr.h"
#include "dcn60/dcn60_clk_mgr.h"
#include "soc_and_ip_translator.h"
#include "bounding_boxes/utm_qos_model_types.h"
#include "bounding_boxes/utm_qos_model_dchub_v3.h"
#include "reg_helper.h"
#include "core_types.h"
#include "dm_helpers.h"
#include "link_service.h"
#include "dc_state_priv.h"
#include "atomfirmware.h"

#include "dcn401/dcn401_smu14_driver_if.h"

#include "dcn/dcn_6_0_0_offset.h"
#include "dcn/dcn_6_0_0_sh_mask.h"

#undef DC_LOGGER
#define DC_LOGGER \
	clk_mgr_base->ctx->logger

#define mmCLK08_CLK8_CLK0_CURRENT_CNT                      0x1B83F
#define mmCLK08_CLK8_CLK1_CURRENT_CNT                      0x1B840
#define mmCLK08_CLK8_CLK2_CURRENT_CNT                      0x1B841
#define mmCLK08_CLK8_CLK3_CURRENT_CNT                      0x1B842
#define mmCLK08_CLK8_CLK4_CURRENT_CNT                      0x1B843

#define mmCLK08_CLK8_CLK0_BYPASS_CNTL                      0x1B816
#define mmCLK08_CLK8_CLK1_BYPASS_CNTL                      0x1B81E
#define mmCLK08_CLK8_CLK2_BYPASS_CNTL                      0x1B826
#define mmCLK08_CLK8_CLK3_BYPASS_CNTL                      0x1B82E
#define mmCLK08_CLK8_CLK4_BYPASS_CNTL                      0x1B836

#define mmCLK08_CLK8_CLK0_DS_CNTL                          0x1B810
#define mmCLK08_CLK8_CLK1_DS_CNTL                          0x1B818
#define mmCLK08_CLK8_CLK2_DS_CNTL                          0x1B820
#define mmCLK08_CLK8_CLK3_DS_CNTL                          0x1B828
#define mmCLK08_CLK8_CLK4_DS_CNTL                          0x1B830

#define mmCLK08_CLK8_CLK_TICK_CNT_CONFIG_REG               0x1B83D

#define DCN_BASE__INST0_SEG1                       0x000000C0

#define CLK8_CLK_TICK_CNT_CONFIG_REG__TIMER_THRESHOLD__SHIFT        0x0
#define CLK8_CLK_TICK_CNT_CONFIG_REG__TIMER_THRESHOLD_MASK          0x00FFFFFFL
#define CLK8_CLK0_BYPASS_CNTL__CLK0_BYPASS_SEL__SHIFT               0x0
#define CLK8_CLK0_BYPASS_CNTL__CLK0_BYPASS_SEL_MASK                 0x00000007L
#define CLK8_CLK1_BYPASS_CNTL__CLK1_BYPASS_SEL__SHIFT               0x0
#define CLK8_CLK1_BYPASS_CNTL__CLK1_BYPASS_SEL_MASK                 0x00000007L
#define CLK8_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL__SHIFT               0x0
#define CLK8_CLK2_BYPASS_CNTL__CLK2_BYPASS_SEL_MASK                 0x00000007L
#define CLK8_CLK3_BYPASS_CNTL__CLK3_BYPASS_SEL__SHIFT               0x0
#define CLK8_CLK3_BYPASS_CNTL__CLK3_BYPASS_SEL_MASK                 0x00000007L
#define CLK8_CLK4_BYPASS_CNTL__CLK4_BYPASS_SEL__SHIFT               0x0
#define CLK8_CLK4_BYPASS_CNTL__CLK4_BYPASS_SEL_MASK                 0x00000007L
#define CLK8_CLK0_DS_CNTL__CLK0_DS_DIV_ID__SHIFT                    0x0
#define CLK8_CLK0_DS_CNTL__CLK0_ALLOW_DS__SHIFT                     0x4
#define CLK8_CLK0_DS_CNTL__CLK0_DS_DIV_ID_MASK                      0x0000000FL
#define CLK8_CLK0_DS_CNTL__CLK0_ALLOW_DS_MASK                       0x00000010L
#define CLK8_CLK1_DS_CNTL__CLK1_DS_DIV_ID__SHIFT                    0x0
#define CLK8_CLK1_DS_CNTL__CLK1_ALLOW_DS__SHIFT                     0x4
#define CLK8_CLK1_DS_CNTL__CLK1_DS_DIV_ID_MASK                      0x0000000FL
#define CLK8_CLK1_DS_CNTL__CLK1_ALLOW_DS_MASK                       0x00000010L
#define CLK8_CLK2_DS_CNTL__CLK2_DS_DIV_ID__SHIFT                    0x0
#define CLK8_CLK2_DS_CNTL__CLK2_ALLOW_DS__SHIFT                     0x4
#define CLK8_CLK2_DS_CNTL__CLK2_DS_DIV_ID_MASK                      0x0000000FL
#define CLK8_CLK2_DS_CNTL__CLK2_ALLOW_DS_MASK                       0x00000010L
#define CLK8_CLK3_DS_CNTL__CLK3_DS_DIV_ID__SHIFT                    0x0
#define CLK8_CLK3_DS_CNTL__CLK3_ALLOW_DS__SHIFT                     0x4
#define CLK8_CLK3_DS_CNTL__CLK3_DS_DIV_ID_MASK                      0x0000000FL
#define CLK8_CLK3_DS_CNTL__CLK3_ALLOW_DS_MASK                       0x00000010L
#define CLK8_CLK4_DS_CNTL__CLK4_DS_DIV_ID__SHIFT                    0x0
#define CLK8_CLK4_DS_CNTL__CLK4_ALLOW_DS__SHIFT                     0x4
#define CLK8_CLK4_DS_CNTL__CLK4_DS_DIV_ID_MASK                      0x0000000FL
#define CLK8_CLK4_DS_CNTL__CLK4_ALLOW_DS_MASK                       0x00000010L

#define DCN60_CLKIP_REFCLK_KHZ 48000 /* 48 MHz */

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

#define CLK_SR_DCN60(reg_name, block, inst)\
	.reg_name = mm ## block ## _ ## reg_name

static const struct clk_mgr_registers clk_mgr_regs_dcn60 = {
	CLK_REG_LIST_DCN60()
};

static const struct clk_mgr_shift clk_mgr_shift_dcn60 = {
	CLK_COMMON_MASK_SH_LIST_DCN60(__SHIFT)
};

static const struct clk_mgr_mask clk_mgr_mask_dcn60 = {
	CLK_COMMON_MASK_SH_LIST_DCN60(_MASK)
};

#define TO_DCN60_CLK_MGR(clk_mgr)\
	container_of(clk_mgr, struct dcn60_clk_mgr, base)

static bool dcn60_is_ppclk_dpm_enabled(struct clk_mgr_internal *clk_mgr, PPCLK_e clk)
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

static bool dcn60_check_native_scaling(struct pipe_ctx *pipe)
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

static void dcn60_update_clocks_update_dpp_dto(struct clk_mgr_internal *clk_mgr,
	struct dc_state *context, bool safe_to_lower, int ref_dppclk_khz)
{
	uint32_t i;

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

static int dcn60_set_hard_min_by_freq_optimized(struct clk_mgr_internal *clk_mgr, PPCLK_e clk, int requested_clk_khz)
{
	if (!clk_mgr->smu_present || !dcn60_is_ppclk_dpm_enabled(clk_mgr, clk))
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
	int actual_clk_khz = dcn60_smu_set_hard_min_by_freq(clk_mgr, clk,
		(uint16_t)khz_to_mhz_floor(requested_clk_khz));

	if (actual_clk_khz < requested_clk_khz)
		actual_clk_khz = dcn60_smu_set_hard_min_by_freq(clk_mgr, clk,
			(uint16_t)khz_to_mhz_ceil(requested_clk_khz));

	return actual_clk_khz;
}

static void dcn60_update_clocks_update_dtb_dto(struct clk_mgr_internal *clk_mgr,
	struct dc_state *context,
	int ref_dtbclk_khz)
{
	(void)ref_dtbclk_khz;
	int i;
	struct dccg *dccg = clk_mgr->dccg;
	struct pipe_ctx *otg_master;
	bool use_hpo_encoder;

	for (i = 0; i < context->stream_count; i++) {
		otg_master = resource_get_otg_master_for_stream(
			&context->res_ctx, context->streams[i]);

		ASSERT(otg_master);
		if (!otg_master)
			continue;

		ASSERT(otg_master->clock_source);
		ASSERT(otg_master->clock_source->funcs->program_pix_clk);
		ASSERT(otg_master->stream_res.pix_clk_params.controller_id >= CONTROLLER_ID_D0);

		use_hpo_encoder = dccg->ctx->dc->link_srv->dp_is_128b_132b_signal(otg_master);
		use_hpo_encoder |= dc_is_hdmi_frl_signal(otg_master->stream->signal);
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

static unsigned int dcn60_build_update_display_clocks_sequence(
	struct clk_mgr *clk_mgr_base,
	struct dc_state *context,
	struct dc_clocks *new_clocks,
	bool safe_to_lower,
	unsigned int num_steps_start)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn60_clk_mgr *clk_mgr60 = TO_DCN60_CLK_MGR(clk_mgr_internal);
	struct dc *dc = clk_mgr_base->ctx->dc;
	struct dmcu *dmcu = clk_mgr_base->ctx->dc->res_pool->dmcu;
	struct dcn60_clk_mgr_block_sequence *block_sequence = clk_mgr60->block_sequence;
	bool force_reset = false;
	bool update_dispclk = false;
	bool update_dppclk = false;
	bool dppclk_lowered = false;
	struct pipe_ctx *otg_master;
	bool frl_present = false;
	unsigned int i;

	unsigned int num_steps = num_steps_start;

	/* CLK_MGR60_READ_CLOCKS_FROM_DENTIST */
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
	if (!new_clocks->dtbclk_en && dcn60_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DTBCLK)) {
		new_clocks->ref_dtbclk_khz = clk_mgr_base->bw_params->clk_table.entries[0].dtbclk_mhz * 1000;
	}

	/* clock limits are received with MHz precision, divide by 1000 to prevent setting clocks at every call */
	if (!dc->debug.disable_dtb_ref_clk_switch &&
		should_set_clock(safe_to_lower, new_clocks->ref_dtbclk_khz / 1000, clk_mgr_base->clks.ref_dtbclk_khz / 1000) && //TODO these should be ceiled
		dcn60_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DTBCLK)) {
		/* DCCG requires KHz precision for DTBCLK */
		block_sequence[num_steps].params.update_hardmin_params.ppclk = PPCLK_DTBCLK;
		block_sequence[num_steps].params.update_hardmin_params.freq_mhz =
			(uint16_t)khz_to_mhz_ceil(new_clocks->ref_dtbclk_khz);
		for (i = 0; i < context->stream_count; i++) {
			otg_master = resource_get_otg_master_for_stream(
				&context->res_ctx, context->streams[i]);
			if (otg_master != NULL &&
				otg_master->stream != NULL &&
				dc_is_hdmi_frl_signal(otg_master->stream->signal)) {
				frl_present = true;
				break;
			}
		}
		if (frl_present)
			block_sequence[num_steps].params.update_hardmin_params.freq_mhz =
				(uint16_t)clk_mgr_base->bw_params->clk_table.entries[
					clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels - 1].dtbclk_mhz;
		block_sequence[num_steps].params.update_hardmin_params.response = &clk_mgr_base->clks.ref_dtbclk_khz;
		block_sequence[num_steps].func = CLK_MGR60_UPDATE_HARDMIN_PPCLK;
		num_steps++;

		/* Update DTO in DCCG */
		block_sequence[num_steps].params.update_dtbclk_dto_params.context = context;
		block_sequence[num_steps].params.update_dtbclk_dto_params.ref_dtbclk_khz = &clk_mgr_base->clks.ref_dtbclk_khz;
		block_sequence[num_steps].func = CLK_MGR60_UPDATE_DTBCLK_DTO;
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

		if (dcn60_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DISPCLK)) {
			block_sequence[num_steps].params.update_hardmin_optimized_params.ppclk = PPCLK_DISPCLK;
			block_sequence[num_steps].params.update_hardmin_optimized_params.freq_khz = clk_mgr_base->clks.dispclk_khz;
			block_sequence[num_steps].params.update_hardmin_optimized_params.response = &clk_mgr_base->clks.actual_dispclk_khz;
			block_sequence[num_steps].func = CLK_MGR60_UPDATE_HARDMIN_PPCLK_OPTIMIZED;
			num_steps++;
		}

		update_dispclk = true;
	}

	if (dc->config.forced_clocks == false || (force_reset && safe_to_lower)) {
		if (dppclk_lowered) {
			/* if clock is being lowered, increase DTO before lowering refclk */
			block_sequence[num_steps].params.update_dppclk_dto_params.context = context;
			block_sequence[num_steps].params.update_dppclk_dto_params.ref_dppclk_khz = &clk_mgr_base->clks.dppclk_khz;
			block_sequence[num_steps].params.update_dppclk_dto_params.safe_to_lower = safe_to_lower;
			block_sequence[num_steps].func = CLK_MGR60_UPDATE_DPPCLK_DTO;
			num_steps++;

			if (dcn60_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DPPCLK)) {
				block_sequence[num_steps].params.update_hardmin_optimized_params.ppclk = PPCLK_DPPCLK;
				block_sequence[num_steps].params.update_hardmin_optimized_params.freq_khz = clk_mgr_base->clks.dppclk_khz;
				block_sequence[num_steps].params.update_hardmin_optimized_params.response = &clk_mgr_base->clks.actual_dppclk_khz;
				block_sequence[num_steps].func = CLK_MGR60_UPDATE_HARDMIN_PPCLK_OPTIMIZED;
				num_steps++;

				block_sequence[num_steps].params.update_dppclk_dto_params.context = context;
				block_sequence[num_steps].params.update_dppclk_dto_params.ref_dppclk_khz = &clk_mgr_base->clks.actual_dppclk_khz;
				block_sequence[num_steps].params.update_dppclk_dto_params.safe_to_lower = safe_to_lower;
				block_sequence[num_steps].func = CLK_MGR60_UPDATE_DPPCLK_DTO;
				num_steps++;
			}
		} else {
			/* if clock is being raised, increase refclk before lowering DTO */
			if (update_dppclk && dcn60_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_DPPCLK)) {
				block_sequence[num_steps].params.update_hardmin_optimized_params.ppclk = PPCLK_DPPCLK;
				block_sequence[num_steps].params.update_hardmin_optimized_params.freq_khz = clk_mgr_base->clks.dppclk_khz;
				block_sequence[num_steps].params.update_hardmin_optimized_params.response = &clk_mgr_base->clks.actual_dppclk_khz;
				block_sequence[num_steps].func = CLK_MGR60_UPDATE_HARDMIN_PPCLK_OPTIMIZED;
				num_steps++;
			}

			block_sequence[num_steps].params.update_dppclk_dto_params.context = context;
			block_sequence[num_steps].params.update_dppclk_dto_params.ref_dppclk_khz = &clk_mgr_base->clks.actual_dppclk_khz;
			block_sequence[num_steps].params.update_dppclk_dto_params.safe_to_lower = safe_to_lower;
			block_sequence[num_steps].func = CLK_MGR60_UPDATE_DPPCLK_DTO;
			num_steps++;
		}
	}

	if (update_dispclk && dmcu && dmcu->funcs->is_dmcu_initialized(dmcu)) {
		/*update dmcu for wait_loop count*/
		block_sequence[num_steps].params.update_psr_wait_loop_params.dmcu = dmcu;
		block_sequence[num_steps].params.update_psr_wait_loop_params.wait = clk_mgr_base->clks.dispclk_khz / 1000 / 7;
		block_sequence[num_steps].func = CLK_MGR60_UPDATE_PSR_WAIT_LOOP;
		num_steps++;
	}

	return num_steps;
}

static uint32_t dcn60_get_vco_frequency_from_reg(struct clk_mgr_internal *clk_mgr)
{
	(void)clk_mgr;
	//TODO: Get VCO frequency
	return 0;
}

static bool dcn60_are_clock_states_equal(struct dc_clocks *a,
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

static bool dcn60_is_smu_present(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	return clk_mgr->smu_present;
}

static int dcn60_get_dtb_ref_freq_khz(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	int dtb_ref_clk_khz = 0;

	if (clk_mgr->smu_present && dcn60_is_ppclk_dpm_enabled(clk_mgr, PPCLK_DTBCLK)) {
		/* DPM enabled, use currently set value */
		dtb_ref_clk_khz = clk_mgr_base->clks.ref_dtbclk_khz;
	} else {
		/* DPM disabled, so use boot snapshot */
		dtb_ref_clk_khz = clk_mgr_base->boot_snapshot.dtbclk;
	}

	return dtb_ref_clk_khz;
}

static unsigned int dcn60_get_dc_mode_limit_mhz(const DpmClock_t *dpm_clk)
{
	if (dpm_clk->NumClocks
			&& dpm_clk->DcMaxClock == dpm_clk->Clocks[dpm_clk->NumClocks - 1])
		return 0;

	return dpm_clk->DcMaxClock;
}

/**
 * dcn60_populate_dc_mode_limit - Populate DC mode limits from DAL init table.
 * @dc_limit: output DC mode limit to populate
 * @init_table: DAL init table transferred from PMFW
 *
 * Sets the DC mode max frequency for each clock. If DcMaxClock equals the
 * highest DPM level, the limit is set to 0 (no DC-specific cap).
 */
static void dcn60_populate_dc_mode_limit(
		struct clk_limit_table_entry *dc_limit,
		const DalInitTable_t *init_table)
{
	dc_limit->dcfclk_mhz  = dcn60_get_dc_mode_limit_mhz(&init_table->PPClocks[PPCLK_DCFCLK]);
	dc_limit->socclk_mhz  = dcn60_get_dc_mode_limit_mhz(&init_table->PPClocks[PPCLK_SOCCLK]);
	dc_limit->dtbclk_mhz  = dcn60_get_dc_mode_limit_mhz(&init_table->PPClocks[PPCLK_DTBCLK]);
	dc_limit->dispclk_mhz = dcn60_get_dc_mode_limit_mhz(&init_table->PPClocks[PPCLK_DISPCLK]);
	dc_limit->memclk_mhz  = dcn60_get_dc_mode_limit_mhz(&init_table->PPClocks[PPCLK_UCLK]);
	dc_limit->fclk_mhz    = dcn60_get_dc_mode_limit_mhz(&init_table->PPClocks[PPCLK_FCLK]);
}

/**
 * dcn60_populate_clk_table - Populate clock table from DAL init table.
 * @clk_mgr: clock manager instance
 * @clk_table: output clock table to populate
 * @init_table: DAL init table transferred from PMFW
 *
 * Reads the DPM clock levels from the PPClocks array within the DalInitTable_t
 * populated by PMFW via TABLE_DAL_INIT transfer.
 */
static void dcn60_populate_clk_table(struct clk_mgr_internal *clk_mgr,
		struct clk_limit_table *clk_table,
		const DalInitTable_t *init_table)
{
	struct clk_limit_num_entries *num_entries = &clk_table->num_entries_per_clk;
	const DpmClock_t *dpm_clk;
	unsigned int i;

	/* DCFCLK */
	dpm_clk = &init_table->PPClocks[PPCLK_DCFCLK];
	num_entries->num_dcfclk_levels = dpm_clk->NumClocks;
	for (i = 0; i < dpm_clk->NumClocks && i < NUM_CLOCK_LEVELS; i++)
		clk_table->entries[i].dcfclk_mhz = dpm_clk->Clocks[i];

	/* SOCCLK */
	dpm_clk = &init_table->PPClocks[PPCLK_SOCCLK];
	num_entries->num_socclk_levels = dpm_clk->NumClocks;
	for (i = 0; i < dpm_clk->NumClocks && i < NUM_CLOCK_LEVELS; i++)
		clk_table->entries[i].socclk_mhz = dpm_clk->Clocks[i];

	/* DTBCLK */
	dpm_clk = &init_table->PPClocks[PPCLK_DTBCLK];
	num_entries->num_dtbclk_levels = dpm_clk->NumClocks;
	for (i = 0; i < dpm_clk->NumClocks && i < NUM_CLOCK_LEVELS; i++)
		clk_table->entries[i].dtbclk_mhz = dpm_clk->Clocks[i];

	/* DISPCLK */
	dpm_clk = &init_table->PPClocks[PPCLK_DISPCLK];
	num_entries->num_dispclk_levels = dpm_clk->NumClocks;
	for (i = 0; i < dpm_clk->NumClocks && i < NUM_CLOCK_LEVELS; i++)
		clk_table->entries[i].dispclk_mhz = dpm_clk->Clocks[i];

	/* DPPCLK */
	dpm_clk = &init_table->PPClocks[PPCLK_DPPCLK];
	num_entries->num_dppclk_levels = dpm_clk->NumClocks;
	for (i = 0; i < dpm_clk->NumClocks && i < NUM_CLOCK_LEVELS; i++)
		clk_table->entries[i].dppclk_mhz = dpm_clk->Clocks[i];

	/* UCLK */
	dpm_clk = &init_table->PPClocks[PPCLK_UCLK];
	num_entries->num_memclk_levels = dpm_clk->NumClocks;
	for (i = 0; i < dpm_clk->NumClocks && i < NUM_CLOCK_LEVELS; i++)
		clk_table->entries[i].memclk_mhz = dpm_clk->Clocks[i];
	if (num_entries->num_memclk_levels)
		clk_mgr->base.bw_params->max_memclk_mhz =
				clk_table->entries[num_entries->num_memclk_levels - 1].memclk_mhz;

	/* FCLK */
	dpm_clk = &init_table->PPClocks[PPCLK_FCLK];
	num_entries->num_fclk_levels = dpm_clk->NumClocks;
	for (i = 0; i < dpm_clk->NumClocks && i < NUM_CLOCK_LEVELS; i++)
		clk_table->entries[i].fclk_mhz = dpm_clk->Clocks[i];

	if (num_entries->num_memclk_levels >= num_entries->num_fclk_levels)
		clk_table->num_entries = num_entries->num_memclk_levels;
	else
		clk_table->num_entries = num_entries->num_fclk_levels;
	if (!clk_table->num_entries)
		clk_table->num_entries = 1;
}

static void dcn60_override_bw_params(struct clk_mgr_internal *clk_mgr,
		struct clk_bw_params *bw_params)
{
	struct clk_limit_table *clk_table = &bw_params->clk_table;
	unsigned int i;

	if (clk_mgr->base.ctx->dc->debug.min_disp_clk_khz) {
		for (i = 0; i < clk_table->num_entries_per_clk.num_dispclk_levels; i++)
			if (clk_table->entries[i].dispclk_mhz
					< (unsigned int)khz_to_mhz_ceil(clk_mgr->base.ctx->dc->debug.min_disp_clk_khz))
				clk_table->entries[i].dispclk_mhz
					= (unsigned int)khz_to_mhz_ceil(clk_mgr->base.ctx->dc->debug.min_disp_clk_khz);
	}

	if (clk_mgr->base.ctx->dc->debug.min_dpp_clk_khz) {
		for (i = 0; i < clk_table->num_entries_per_clk.num_dppclk_levels; i++)
			if (clk_table->entries[i].dppclk_mhz
					< (unsigned int)khz_to_mhz_ceil(clk_mgr->base.ctx->dc->debug.min_dpp_clk_khz))
				clk_table->entries[i].dppclk_mhz
					= (unsigned int)khz_to_mhz_ceil(clk_mgr->base.ctx->dc->debug.min_dpp_clk_khz);
	}

	if (clk_mgr->base.ctx->dc->debug.disable_dtb_ref_clk_switch)
		bw_params->dc_mode_limit.dtbclk_mhz = 0;

	bw_params->dc_mode_softmax_memclk = bw_params->dc_mode_limit.memclk_mhz;
}

/**
 * dcn60_fetch_dal_init_table - Transfer and apply the DAL init table from PMFW.
 * @clk_mgr: clock manager instance
 *
 * Issues TABLE_DAL_INIT transfer, populates clock table and bounding box.
 *
 * Return: true on success, false if transfer failed
 */
static void dcn60_populate_utm_qos_model(
		struct clk_mgr_internal *clk_mgr,
		const struct utm_qos_model **qos_model,
		const DalInitTable_t *init_table)
{
	struct dcn60_clk_mgr *dcn60_clk_mgr =
			container_of(clk_mgr, struct dcn60_clk_mgr, base);
	struct utm_qos_model *model = &dcn60_clk_mgr->utm_qos_model;
	struct utm_qos_model_dchub_v3 *dchub = &dcn60_clk_mgr->dchub_v3;
	const SocUtmTable_t *utm_table = &init_table->UtmTable;
	unsigned int ll, sop;

	memset(dchub, 0, sizeof(*dchub));
	dchub->load_level_count = (uint8_t)utm_table->Header.LoadLevelCount;
	dchub->sop_count = (uint8_t)utm_table->Header.SopCount;

	for (ll = 0; ll < dchub->load_level_count
			&& ll < UTM_QOS_MODEL_V3_MAX_LOAD_LEVEL_COUNT; ll++) {
		for (sop = 0; sop < dchub->sop_count
				&& sop < UTM_QOS_MODEL_V3_MAX_SOP_COUNT; sop++) {
			const SocUtmSopEntry_t *src = &utm_table->Sops[ll][sop];
			struct utm_qos_model_dchub_v3_sop_entry *dst =
					&dchub->sops[ll][sop];

			dst->urgent_ramp_ps = src->UrgentRampPs;
			dst->t_trip_ps = src->TripPs;
			dst->meta_trip_to_mem_ps = src->MetaTripToMemPs;
			dst->max_req_latency_urg_ps = src->MaxReqLatencyUrgPs;
			dst->avg_req_latency_urg_ps = src->AvgReqLatencyUrgPs;
			dst->max_req_latency_non_urg_ps = src->MaxReqLatencyNonUrgPs;
			dst->avg_req_latency_non_urg_ps = src->AvgReqLatencyNonUrgPs;
			dst->df_response_time_ps = src->DfResponseTimePs;
			dst->urgent_bandwidth_KBps = src->UrgentBandwidthKBps;
			dst->nominal_bandwidth_KBps = src->NominalBandwidthKBps;
			dst->lsdma_bandwidth_KBps = src->LsdmaBandwidthKBps;
		}
	}

	model->version = utm_qos_model_version_v3;
	model->dchub_v3 = dchub;
	*qos_model = model;
}

static bool dcn60_fetch_dal_init_table(struct clk_mgr_internal *clk_mgr)
{
	struct clk_bw_params *bw_params = clk_mgr->base.bw_params;
	const DalInitTable_t *init_table;

	if (!dcn60_smu_get_dal_init_table(clk_mgr, &init_table))
		return false;

	clk_mgr->smu_ver = init_table->Header.SmuVersion;

	dcn60_populate_clk_table(clk_mgr, &bw_params->clk_table, init_table);
	dcn60_populate_dc_mode_limit(&bw_params->dc_mode_limit, init_table);

	bw_params->num_channels = init_table->MemoryConfig.NumUmcChannels;
	bw_params->dram_channel_width_bytes =
			clk_mgr->base.ctx->dc_bios->vram_info.dram_channel_width_bytes;

	dcn60_populate_utm_qos_model(clk_mgr, &bw_params->utm_qos_model, init_table);

	dcn60_override_bw_params(clk_mgr, bw_params);

	return true;
}

void dcn60_init_clocks(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	uint32_t smu_header_ver = 0;

	memset(&(clk_mgr_base->clks), 0, sizeof(struct dc_clocks));
	clk_mgr_base->clks.p_state_change_support = true;
	clk_mgr_base->clks.fclk_p_state_change_support = false;
	clk_mgr_base->force_smu_not_present = true; // temporary until SMU ready
	clk_mgr->smu_present = !clk_mgr_base->force_smu_not_present /* not force-disabled */
			&& dcn60_smu_get_msg_header_version(clk_mgr, &smu_header_ver)
			&& smu_header_ver != 0;

	clk_mgr->dpm_present = clk_mgr->smu_present
			&& dcn60_fetch_dal_init_table(clk_mgr)
			&& clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dcfclk_levels
			&& clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dtbclk_levels
			&& clk_mgr_base->bw_params->clk_table.num_entries_per_clk.num_dispclk_levels;

	if (clk_mgr->dpm_present)
		clk_mgr_base->ctx->dc->res_pool->funcs->update_bw_bounding_box(
				clk_mgr_base->ctx->dc, clk_mgr_base->bw_params);
}

static inline uint32_t count_to_khz(uint32_t count, uint32_t timer_ths, uint32_t refclk_khz)
{
	if (timer_ths == 0)
		return 0;

	return (uint32_t)div_u64((uint64_t)count * (uint64_t)refclk_khz, timer_ths);
}

static void dcn60_dump_clk_registers_internal(struct dcn42_clk_internal *internal, struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	// read ths
	REG_GET(CLK8_CLK_TICK_CNT_CONFIG_REG, TIMER_THRESHOLD, &internal->CLK8_CLK_TICK_CNT__TIMER_THRESHOLD);

	// read dtbclk
	internal->CLK8_CLK4_CURRENT_CNT = REG_READ(CLK8_CLK4_CURRENT_CNT);
	internal->CLK8_CLK4_BYPASS_CNTL = REG_READ(CLK8_CLK4_BYPASS_CNTL);

	// read dcfclk
	internal->CLK8_CLK0_CURRENT_CNT = REG_READ(CLK8_CLK0_CURRENT_CNT);
	internal->CLK8_CLK0_BYPASS_CNTL = REG_READ(CLK8_CLK0_BYPASS_CNTL);

	// read dcf deep sleep divider
	internal->CLK8_CLK0_DS_CNTL = REG_READ(CLK8_CLK0_DS_CNTL);
	internal->CLK8_CLK3_DS_CNTL = REG_READ(CLK8_CLK3_DS_CNTL);

	// read dppclk
	internal->CLK8_CLK2_CURRENT_CNT = REG_READ(CLK8_CLK2_CURRENT_CNT);
	internal->CLK8_CLK2_BYPASS_CNTL = REG_READ(CLK8_CLK2_BYPASS_CNTL);

	// read dprefclk
	internal->CLK8_CLK3_CURRENT_CNT = REG_READ(CLK8_CLK3_CURRENT_CNT);
	internal->CLK8_CLK3_BYPASS_CNTL = REG_READ(CLK8_CLK3_BYPASS_CNTL);

	// read dispclk
	internal->CLK8_CLK1_CURRENT_CNT = REG_READ(CLK8_CLK1_CURRENT_CNT);
	internal->CLK8_CLK1_BYPASS_CNTL = REG_READ(CLK8_CLK1_BYPASS_CNTL);
}

static void dcn60_dump_clk_registers(struct clk_state_registers_and_bypass *regs_and_bypass,
		struct clk_mgr *clk_mgr_base, struct clk_log_info *log_info)
{
	(void)log_info;
	struct dcn42_clk_internal internal = {0};
	char *bypass_clks[5] = {"0x0 DFS", "0x1 REFCLK", "0x2 ERROR", "0x3 400 FCH", "0x4 600 FCH"};
	const uint32_t refclk_khz = DCN60_CLKIP_REFCLK_KHZ;
	uint32_t timer_ths;

	dcn60_dump_clk_registers_internal(&internal, clk_mgr_base);
	regs_and_bypass->timer_threshold = internal.CLK8_CLK_TICK_CNT__TIMER_THRESHOLD;
	timer_ths = internal.CLK8_CLK_TICK_CNT__TIMER_THRESHOLD;

	regs_and_bypass->dcfclk = count_to_khz(internal.CLK8_CLK0_CURRENT_CNT, timer_ths, refclk_khz);
	regs_and_bypass->dcf_deep_sleep_divider = internal.CLK8_CLK0_DS_CNTL / 10;
	regs_and_bypass->dcf_deep_sleep_allow = true;
	regs_and_bypass->dprefclk = count_to_khz(internal.CLK8_CLK3_CURRENT_CNT, timer_ths, refclk_khz);
	regs_and_bypass->dispclk = count_to_khz(internal.CLK8_CLK1_CURRENT_CNT, timer_ths, refclk_khz);
	regs_and_bypass->dppclk = count_to_khz(internal.CLK8_CLK2_CURRENT_CNT, timer_ths, refclk_khz);
	regs_and_bypass->dtbclk = count_to_khz(internal.CLK8_CLK4_CURRENT_CNT, timer_ths, refclk_khz);

	regs_and_bypass->dispclk_bypass = get_reg_field_value(internal.CLK8_CLK0_BYPASS_CNTL, CLK8_CLK0_BYPASS_CNTL, CLK0_BYPASS_SEL);
	regs_and_bypass->dppclk_bypass = get_reg_field_value(internal.CLK8_CLK1_BYPASS_CNTL, CLK8_CLK1_BYPASS_CNTL, CLK1_BYPASS_SEL);
	regs_and_bypass->dprefclk_bypass = get_reg_field_value(internal.CLK8_CLK2_BYPASS_CNTL, CLK8_CLK2_BYPASS_CNTL, CLK2_BYPASS_SEL);
	regs_and_bypass->dcfclk_bypass = get_reg_field_value(internal.CLK8_CLK3_BYPASS_CNTL, CLK8_CLK3_BYPASS_CNTL, CLK3_BYPASS_SEL);

	if (clk_mgr_base->ctx->dc->debug.pstate_enabled) {
		DC_LOG_SMU("clk_type,clk_value,deepsleep_cntl,deepsleep_allow,bypass\n");

		DC_LOG_SMU("dcfclk,%d,%d,%d,%s\n",
				   regs_and_bypass->dcfclk,
				   regs_and_bypass->dcf_deep_sleep_divider,
				   regs_and_bypass->dcf_deep_sleep_allow,
				   bypass_clks[(int) regs_and_bypass->dcfclk_bypass]);

		DC_LOG_SMU("dprefclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dprefclk,
			bypass_clks[(int) regs_and_bypass->dprefclk_bypass]);

		DC_LOG_SMU("dispclk,%d,N/A,N/A,%s\n",
			regs_and_bypass->dispclk,
			bypass_clks[(int) regs_and_bypass->dispclk_bypass]);

		//split
		DC_LOG_SMU("SPLIT\n");

		// REGISTER VALUES
		DC_LOG_SMU("reg_name,value,clk_type\n");
		DC_LOG_SMU("CLK1_CLK3_CURRENT_CNT,%d,dcfclk\n",
				internal.CLK8_CLK3_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK3_DS_CNTL,%d,dcf_deep_sleep_divider\n",
					internal.CLK8_CLK3_DS_CNTL);

		DC_LOG_SMU("CLK1_CLK3_ALLOW_DS,%d,dcf_deep_sleep_allow\n",
					(internal.CLK8_CLK3_DS_CNTL & 0x10));

		DC_LOG_SMU("CLK1_CLK2_CURRENT_CNT,%d,dprefclk\n",
					internal.CLK8_CLK2_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK0_CURRENT_CNT,%d,dispclk\n",
					internal.CLK8_CLK0_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK1_CURRENT_CNT,%d,dppclk\n",
					internal.CLK8_CLK1_CURRENT_CNT);

		DC_LOG_SMU("CLK1_CLK3_BYPASS_CNTL,%d,dcfclk_bypass\n",
					internal.CLK8_CLK3_BYPASS_CNTL);

		DC_LOG_SMU("CLK1_CLK2_BYPASS_CNTL,%d,dprefclk_bypass\n",
					internal.CLK8_CLK2_BYPASS_CNTL);

		DC_LOG_SMU("CLK1_CLK0_BYPASS_CNTL,%d,dispclk_bypass\n",
					internal.CLK8_CLK0_BYPASS_CNTL);

		DC_LOG_SMU("CLK1_CLK1_BYPASS_CNTL,%d,dppclk_bypass\n",
					internal.CLK8_CLK1_BYPASS_CNTL);
	}
}

static void dcn60_auto_dpm_test_log(
		struct dc_clocks *new_clocks,
		struct clk_mgr_internal *clk_mgr,
		struct dc_state *context)
{
	unsigned int mall_ss_size_bytes;
	int dramclk_khz_override, fclk_khz_override, num_fclk_levels;
	struct clk_mgr *clk_mgr_base = &clk_mgr->base;

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

	dcn60_dump_clk_registers(&clk_register_dump, clk_mgr_base, &log_info);

	// Overrides for these clocks in case there is no p_state change support
	dramclk_khz_override = new_clocks->dramclk_khz;
	fclk_khz_override = new_clocks->fclk_khz;

	num_fclk_levels = clk_mgr->base.bw_params->clk_table.num_entries_per_clk.num_fclk_levels - 1;

	if (!new_clocks->p_state_change_support)
		dramclk_khz_override = clk_mgr->base.bw_params->max_memclk_mhz * 1000;

	if (!new_clocks->fclk_p_state_change_support)
		fclk_khz_override = clk_mgr->base.bw_params->clk_table.entries[num_fclk_levels].fclk_mhz * 1000;

	////////////////////////////////////////////////////////////////////////////
	//	IMPORTANT:	When adding more clocks to these logs, do NOT put a newline
	//				anywhere other than at the very end of the string.
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
				is_scaled_list[i] = !(dcn60_check_native_scaling(curr_pipe_ctx));
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

static void dcn60_execute_block_sequence(struct clk_mgr *clk_mgr_base, unsigned int num_steps)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn60_clk_mgr *clk_mgr60 = TO_DCN60_CLK_MGR(clk_mgr_internal);

	unsigned int i;
	union dcn60_clk_mgr_block_sequence_params *params;

	/* execute sequence */
	for (i = 0; i < num_steps; i++) {
		params = &clk_mgr60->block_sequence[i].params;

		switch (clk_mgr60->block_sequence[i].func) {
		case CLK_MGR60_UPDATE_HARDMIN_PPCLK:
			if (params->update_hardmin_params.response)
				*params->update_hardmin_params.response = dcn60_smu_set_hard_min_by_freq(
						clk_mgr_internal,
						params->update_hardmin_params.ppclk,
						params->update_hardmin_params.freq_mhz);
			else
				dcn60_smu_set_hard_min_by_freq(clk_mgr_internal,
						params->update_hardmin_params.ppclk,
						params->update_hardmin_params.freq_mhz);
			break;
		case CLK_MGR60_UPDATE_HARDMIN_PPCLK_OPTIMIZED:
			if (params->update_hardmin_optimized_params.response)
				*params->update_hardmin_optimized_params.response =
					dcn60_set_hard_min_by_freq_optimized(
						clk_mgr_internal,
						params->update_hardmin_optimized_params.ppclk,
						params->update_hardmin_optimized_params.freq_khz);
			else
				dcn60_set_hard_min_by_freq_optimized(clk_mgr_internal,
						params->update_hardmin_optimized_params.ppclk,
						params->update_hardmin_optimized_params.freq_khz);
			break;
		case CLK_MGR60_UPDATE_DEEP_SLEEP_DCFCLK:
			dcn60_smu_set_min_deep_sleep_dcfclk(
					clk_mgr_internal,
					params->update_deep_sleep_dcfclk_params.freq_mhz);
			break;
		case CLK_MGR60_INDICATE_PSTATE_STATUS:
			dcn60_smu_indicate_pstate_status(
					clk_mgr_internal,
					params->indicate_pstate_status_params.allow_fclk,
					params->indicate_pstate_status_params.allow_uclk,
					params->indicate_pstate_status_params.wait_resp,
					params->indicate_pstate_status_params.drr_enable,
					params->indicate_pstate_status_params.alt_ch_enable);
			break;
		case CLK_MGR60_UPDATE_DPPCLK_DTO:
			dcn60_update_clocks_update_dpp_dto(
					clk_mgr_internal,
					params->update_dppclk_dto_params.context,
					params->update_dppclk_dto_params.safe_to_lower,
					*params->update_dppclk_dto_params.ref_dppclk_khz);
			break;
		case CLK_MGR60_UPDATE_DTBCLK_DTO:
			dcn60_update_clocks_update_dtb_dto(
					clk_mgr_internal,
					params->update_dtbclk_dto_params.context,
					*params->update_dtbclk_dto_params.ref_dtbclk_khz);
			break;
		case CLK_MGR60_UPDATE_PSR_WAIT_LOOP:
			params->update_psr_wait_loop_params.dmcu->funcs->set_psr_wait_loop(
					params->update_psr_wait_loop_params.dmcu,
					params->update_psr_wait_loop_params.wait);
			break;
		case CLK_MGR60_UPDATE_STUTTER_EFFICIENCY:
			dcn60_smu_set_stutter_efficiency(
					clk_mgr_internal,
					params->update_stutter_efficiency_params.base_efficiency,
					params->update_stutter_efficiency_params.low_power_efficiency);
			break;
		case CLK_MGR60_UPDATE_UTM_QOS_REQUEST:
			dcn60_smu_update_utm_qos_request(clk_mgr_internal,
					params->update_utm_qos_request_params.utm_latency_ub_index,
					params->update_utm_qos_request_params.utm_nominal_bandwidth_lb_KBps,
					params->update_utm_qos_request_params.utm_urgent_bandwidth_lb_KBps,
					params->update_utm_qos_request_params.utm_lsdma_bandwidth_lb_KBps);
			break;
		default:
			/* this should never happen */
			BREAK_TO_DEBUGGER();
			break;
		}
	}
}

/**
 * dcn60_make_bandwidth_clocks_update_action - Determine what clock and p-state
 * updates are needed by comparing new_clocks against current clk_mgr state.
 * @action: output action struct, zeroed and populated by this function
 * @clk_mgr_base: current clock manager state (read-only)
 * @context: dc state with active plane information
 * @new_clocks: requested clock values to compare against current state
 * @safe_to_lower: whether clocks are allowed to decrease
 */
static void dcn60_make_bandwidth_clocks_update_action(
		struct dcn60_bandwidth_clocks_update_action *action,
		struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		struct dc_clocks *new_clocks,
		bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	int total_plane_count = clk_mgr_helper_get_active_plane_cnt(
			clk_mgr_base->ctx->dc, context);
	bool dcfclk_dpm_enabled = dcn60_is_ppclk_dpm_enabled(clk_mgr_internal,
			PPCLK_DCFCLK);
	bool uclk_pstate_supported = new_clocks->p_state_change_support
			|| (total_plane_count == 0);

	bool fclk_pstate_supported = new_clocks->fclk_p_state_change_support
			|| (total_plane_count == 0);

	memset(action, 0, sizeof(*action));
	/* clock actions */
	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_khz,
			clk_mgr_base->clks.dcfclk_khz)) {
		action->dcfclk.update = true;
		action->dcfclk.send_message = dcfclk_dpm_enabled;
	}

	if (should_set_clock(safe_to_lower, new_clocks->dcfclk_deep_sleep_khz,
			clk_mgr_base->clks.dcfclk_deep_sleep_khz)) {
		action->deep_sleep_dcfclk.update = true;
		action->deep_sleep_dcfclk.send_message = dcfclk_dpm_enabled;
	}

	if (should_set_clock(safe_to_lower, new_clocks->socclk_khz,
			clk_mgr_base->clks.socclk_khz)) {
		action->socclk.update = true;
		/* state tracking only — no SMU hardmin message for socclk */
		action->socclk.send_message = false;
	}

	if (clk_mgr_base->clks.stutter_efficiency.base_efficiency
				!= new_clocks->stutter_efficiency.base_efficiency
			|| clk_mgr_base->clks.stutter_efficiency.low_power_efficiency
				!= new_clocks->stutter_efficiency.low_power_efficiency) {
		action->stutter.update = true;
		action->stutter.send_message = true;
	}

	if (new_clocks->utm_nominal_bandwidth_lb_KBps != clk_mgr_base->clks.utm_nominal_bandwidth_lb_KBps
			|| new_clocks->utm_urgent_bandwidth_lb_KBps != clk_mgr_base->clks.utm_urgent_bandwidth_lb_KBps
			|| new_clocks->utm_lsdma_bandwidth_lb_KBps != clk_mgr_base->clks.utm_lsdma_bandwidth_lb_KBps
			|| new_clocks->utm_latency_ub_index != clk_mgr_base->clks.utm_latency_ub_index) {
		action->utm_qos.update = true;
		action->utm_qos.send_message = true;
	}

	/* p-state actions */
	if (should_update_pstate_support(safe_to_lower,
			uclk_pstate_supported,
			clk_mgr_base->clks.p_state_change_support)) {
		action->uclk_pstate.enable = uclk_pstate_supported;
		action->uclk_pstate.disable = !uclk_pstate_supported;
		action->uclk_pstate.send_message =
				dcn60_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_UCLK);
	}

	if (should_update_pstate_support(safe_to_lower,
			fclk_pstate_supported,
			clk_mgr_base->clks.fclk_p_state_change_support)) {
		action->fclk_pstate.enable = fclk_pstate_supported;
		action->fclk_pstate.disable = !fclk_pstate_supported;
		action->fclk_pstate.send_message =
				dcn60_is_ppclk_dpm_enabled(clk_mgr_internal, PPCLK_FCLK);
	}

	if (new_clocks->fw_based_mclk_switching
			!= clk_mgr_base->clks.fw_based_mclk_switching) {
		action->fams.enable = new_clocks->fw_based_mclk_switching;
		action->fams.disable = !new_clocks->fw_based_mclk_switching
				&& safe_to_lower;
		action->fams.send_message = action->fams.enable
				|| action->fams.disable;
	}

	if (new_clocks->alt_ch_pstate_switch != clk_mgr_base->clks.alt_ch_pstate_switch) {
		action->alt_ch.enable = new_clocks->alt_ch_pstate_switch;
		action->alt_ch.disable = !new_clocks->alt_ch_pstate_switch
				&& safe_to_lower;
		action->alt_ch.send_message = action->alt_ch.enable
				|| action->alt_ch.disable;
	}
}

/**
 * dcn60_build_bandwidth_clocks_block_sequence_with_action - Build the SMU
 * message sequence from the action struct without mutating clk_mgr state.
 * @block_sequence: output array of block sequence steps
 * @action: action struct populated by dcn60_make_bandwidth_clocks_update_action
 * @clk_mgr_base: current clock manager state (read-only, used for original values)
 * @new_clocks: requested clock values for message parameters
 *
 * Return: number of steps written to block_sequence
 */
static unsigned int dcn60_build_bandwidth_clocks_block_sequence_with_action(
		struct dcn60_clk_mgr_block_sequence *block_sequence,
		struct dcn60_bandwidth_clocks_update_action *action,
		struct clk_mgr *clk_mgr_base,
		struct dc_clocks *new_clocks)
{
	unsigned int num_steps = 0;

	/* CLK_MGR60_INDICATE_PSTATE_STATUS — enable */
	if ((action->uclk_pstate.enable && action->uclk_pstate.send_message)
			|| (action->fclk_pstate.enable && action->fclk_pstate.send_message)
			|| (action->fams.enable && action->fams.send_message)
			|| (action->alt_ch.enable && action->alt_ch.send_message)) {
		block_sequence[num_steps].params.indicate_pstate_status_params.allow_uclk =
				action->uclk_pstate.enable
				|| clk_mgr_base->clks.p_state_change_support;
		block_sequence[num_steps].params.indicate_pstate_status_params.allow_fclk =
				action->fclk_pstate.enable
				|| clk_mgr_base->clks.fclk_p_state_change_support;
		block_sequence[num_steps].params.indicate_pstate_status_params.drr_enable =
				action->fams.enable
				|| clk_mgr_base->clks.fw_based_mclk_switching;
		block_sequence[num_steps].params.indicate_pstate_status_params.wait_resp =
				action->fams.enable;
		block_sequence[num_steps].params.indicate_pstate_status_params.alt_ch_enable =
				action->alt_ch.enable
				|| clk_mgr_base->clks.alt_ch_pstate_switch;
		block_sequence[num_steps].func = CLK_MGR60_INDICATE_PSTATE_STATUS;
		num_steps++;
	}

	/* CLK_MGR60_UPDATE_HARDMIN_PPCLK — DCFCLK */
	if (action->dcfclk.send_message) {
		block_sequence[num_steps].params.update_hardmin_params.ppclk = PPCLK_DCFCLK;
		block_sequence[num_steps].params.update_hardmin_params.freq_mhz =
			(uint16_t)khz_to_mhz_ceil(new_clocks->dcfclk_khz);
		block_sequence[num_steps].params.update_hardmin_params.response = NULL;
		block_sequence[num_steps].func = CLK_MGR60_UPDATE_HARDMIN_PPCLK;
		num_steps++;
	}

	/* CLK_MGR60_UPDATE_DEEP_SLEEP_DCFCLK */
	if (action->deep_sleep_dcfclk.send_message) {
		block_sequence[num_steps].params.update_deep_sleep_dcfclk_params.freq_mhz =
			(uint16_t)khz_to_mhz_ceil(new_clocks->dcfclk_deep_sleep_khz);
		block_sequence[num_steps].func = CLK_MGR60_UPDATE_DEEP_SLEEP_DCFCLK;
		num_steps++;
	}

	/* CLK_MGR60_UPDATE_UTM_QOS_REQUEST */
	if (action->utm_qos.send_message) {
		block_sequence[num_steps].params.update_utm_qos_request_params.utm_nominal_bandwidth_lb_KBps =
				new_clocks->utm_nominal_bandwidth_lb_KBps;
		block_sequence[num_steps].params.update_utm_qos_request_params.utm_urgent_bandwidth_lb_KBps =
				new_clocks->utm_urgent_bandwidth_lb_KBps;
		block_sequence[num_steps].params.update_utm_qos_request_params.utm_lsdma_bandwidth_lb_KBps =
				new_clocks->utm_lsdma_bandwidth_lb_KBps;
		block_sequence[num_steps].params.update_utm_qos_request_params.utm_latency_ub_index =
				new_clocks->utm_latency_ub_index;
		block_sequence[num_steps].func = CLK_MGR60_UPDATE_UTM_QOS_REQUEST;
		num_steps++;
	}

	/* CLK_MGR60_INDICATE_PSTATE_STATUS — disable */
	if ((action->uclk_pstate.disable && action->uclk_pstate.send_message)
			|| (action->fclk_pstate.disable && action->fclk_pstate.send_message)
			|| (action->fams.disable && action->fams.send_message)
			|| (action->alt_ch.disable && action->alt_ch.send_message)) {
		block_sequence[num_steps].params.indicate_pstate_status_params.allow_uclk =
				!action->uclk_pstate.disable
				&& (action->uclk_pstate.enable
						|| clk_mgr_base->clks.p_state_change_support);
		block_sequence[num_steps].params.indicate_pstate_status_params.allow_fclk =
				!action->fclk_pstate.disable
				&& (action->fclk_pstate.enable
						|| clk_mgr_base->clks.fclk_p_state_change_support);
		block_sequence[num_steps].params.indicate_pstate_status_params.drr_enable =
				!action->fams.disable
				&& (action->fams.enable
						|| clk_mgr_base->clks.fw_based_mclk_switching);
		block_sequence[num_steps].params.indicate_pstate_status_params.alt_ch_enable =
				!action->alt_ch.disable
				&& (action->alt_ch.enable
						|| clk_mgr_base->clks.alt_ch_pstate_switch);
		block_sequence[num_steps].params.indicate_pstate_status_params.wait_resp = false;
		block_sequence[num_steps].func = CLK_MGR60_INDICATE_PSTATE_STATUS;
		num_steps++;
	}

	/* CLK_MGR60_UPDATE_STUTTER_EFFICIENCY */
	if (action->stutter.send_message) {
		block_sequence[num_steps].params.update_stutter_efficiency_params.base_efficiency =
			new_clocks->stutter_efficiency.base_efficiency;
		block_sequence[num_steps].params.update_stutter_efficiency_params.low_power_efficiency =
			new_clocks->stutter_efficiency.low_power_efficiency;
		block_sequence[num_steps].func = CLK_MGR60_UPDATE_STUTTER_EFFICIENCY;
		num_steps++;
	}

	return num_steps;
}

/**
 * dcn60_update_bandwidth_clocks_state_with_action - Apply all pending clock
 * and p-state changes to clk_mgr state based on the action struct.
 * @clk_mgr_base: clock manager state to update
 * @action: action struct populated by dcn60_make_bandwidth_clocks_update_action
 * @new_clocks: new clock values to copy into clk_mgr state
 */
static void dcn60_update_bandwidth_clocks_state_with_action(
		struct clk_mgr *clk_mgr_base,
		struct dcn60_bandwidth_clocks_update_action *action,
		struct dc_clocks *new_clocks)
{
	if (action->uclk_pstate.enable)
		clk_mgr_base->clks.p_state_change_support = true;
	if (action->uclk_pstate.disable)
		clk_mgr_base->clks.p_state_change_support = false;
	if (action->fclk_pstate.enable)
		clk_mgr_base->clks.fclk_p_state_change_support = true;
	if (action->fclk_pstate.disable)
		clk_mgr_base->clks.fclk_p_state_change_support = false;
	if (action->fams.enable)
		clk_mgr_base->clks.fw_based_mclk_switching = true;
	if (action->fams.disable)
		clk_mgr_base->clks.fw_based_mclk_switching = false;
	if (action->alt_ch.enable)
		clk_mgr_base->clks.alt_ch_pstate_switch = true;
	if (action->alt_ch.disable)
		clk_mgr_base->clks.alt_ch_pstate_switch = false;
	if (action->dcfclk.update)
		clk_mgr_base->clks.dcfclk_khz = new_clocks->dcfclk_khz;
	if (action->deep_sleep_dcfclk.update)
		clk_mgr_base->clks.dcfclk_deep_sleep_khz = new_clocks->dcfclk_deep_sleep_khz;
	if (action->socclk.update)
		clk_mgr_base->clks.socclk_khz = new_clocks->socclk_khz;
	if (action->utm_qos.update) {
		clk_mgr_base->clks.utm_nominal_bandwidth_lb_KBps = new_clocks->utm_nominal_bandwidth_lb_KBps;
		clk_mgr_base->clks.utm_urgent_bandwidth_lb_KBps  = new_clocks->utm_urgent_bandwidth_lb_KBps;
		clk_mgr_base->clks.utm_lsdma_bandwidth_lb_KBps   = new_clocks->utm_lsdma_bandwidth_lb_KBps;
		clk_mgr_base->clks.utm_latency_ub_index          = new_clocks->utm_latency_ub_index;
	}
	if (action->stutter.update) {
		clk_mgr_base->clks.stutter_efficiency.base_efficiency =
				new_clocks->stutter_efficiency.base_efficiency;
		clk_mgr_base->clks.stutter_efficiency.low_power_efficiency =
				new_clocks->stutter_efficiency.low_power_efficiency;
	}
}

/**
 * dcn60_build_update_bandwidth_clocks_sequence - Build block sequence for
 * bandwidth clock updates and apply new clock values to clk_mgr state.
 * @clk_mgr_base: clock manager to update
 * @context: dc state with active plane information
 * @new_clocks: requested clock values from bandwidth calculation
 * @safe_to_lower: whether clocks are allowed to decrease
 *
 * Return: number of block sequence steps to execute
 */
static unsigned int dcn60_build_update_bandwidth_clocks_sequence(
		struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		struct dc_clocks *new_clocks,
		bool safe_to_lower)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn60_clk_mgr *clk_mgr60 = TO_DCN60_CLK_MGR(clk_mgr_internal);
	struct dcn60_bandwidth_clocks_update_action action = {0};
	unsigned int num_steps;

	dcn60_make_bandwidth_clocks_update_action(&action,
			clk_mgr_base, context, new_clocks, safe_to_lower);

	num_steps = dcn60_build_bandwidth_clocks_block_sequence_with_action(
			clk_mgr60->block_sequence, &action,
			clk_mgr_base, new_clocks);

	dcn60_update_bandwidth_clocks_state_with_action(clk_mgr_base, &action, new_clocks);

	return num_steps;
}

static void dcn60_update_clocks(struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		bool safe_to_lower)
{
	struct dc *dc = clk_mgr_base->ctx->dc;
	unsigned int num_steps = 0;

	/* TODO: mutates context bw clocks. This is a side effect */
	if (dc->debug.force_min_dcfclk_mhz > 0) {
		int force_min_dcfclk_khz =
				dc->debug.force_min_dcfclk_mhz * 1000;

		if (context->bw_ctx.bw.dcn.clk.dcfclk_khz < force_min_dcfclk_khz)
			context->bw_ctx.bw.dcn.clk.dcfclk_khz = force_min_dcfclk_khz;
	}

	/* build bandwidth related clocks update sequence */
	num_steps = dcn60_build_update_bandwidth_clocks_sequence(clk_mgr_base,
			context,
			&context->bw_ctx.bw.dcn.clk,
			safe_to_lower);

	/* execute sequence */
	dcn60_execute_block_sequence(clk_mgr_base,	num_steps);

	/* build display related clocks update sequence */
	num_steps = dcn60_build_update_display_clocks_sequence(clk_mgr_base,
			context,
			&context->bw_ctx.bw.dcn.clk,
			safe_to_lower,
			0);

	/* execute sequence */
	dcn60_execute_block_sequence(clk_mgr_base,	num_steps);

	if (dc->config.enable_auto_dpm_test_logs)
		dcn60_auto_dpm_test_log(&context->bw_ctx.bw.dcn.clk, TO_CLK_MGR_INTERNAL(clk_mgr_base), context);

}

static void dcn60_clock_read_ss_info(struct clk_mgr_internal *clk_mgr)
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

/* Set min memclk to minimum, either constrained by the current mode or DPM0 */
static void dcn60_set_hard_min_memclk(struct clk_mgr *clk_mgr_base, bool current_mode)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	const struct dc *dc = clk_mgr->base.ctx->dc;
	struct dc_state *context = dc->current_state;
	struct dc_clocks new_clocks;
	int num_steps;

	if (!clk_mgr->smu_present || !dcn60_is_ppclk_dpm_enabled(clk_mgr, PPCLK_UCLK))
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

	num_steps = dcn60_build_update_bandwidth_clocks_sequence(clk_mgr_base,
			context,
			&new_clocks,
			true);

	/* execute sequence */
	dcn60_execute_block_sequence(clk_mgr_base,	num_steps);
}

static void dcn60_get_memclk_states_from_smu(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	if (!dcn60_fetch_dal_init_table(clk_mgr))
		return;

	clk_mgr_base->ctx->dc->res_pool->funcs->update_bw_bounding_box(
			clk_mgr_base->ctx->dc, clk_mgr_base->bw_params);
}

static void dcn60_enable_pme_wa(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn60_smu_set_pme_workaround(clk_mgr);
}

static unsigned int dcn60_get_max_active_utm_bandwidth_kbps(
		struct clk_mgr *clk_mgr_base)
{
	const struct utm_qos_model *qos_model = clk_mgr_base->bw_params->utm_qos_model;
	const struct utm_qos_model_dchub_v3 *dchub;
	unsigned int highest;

	if (!qos_model
			|| qos_model->version != utm_qos_model_version_v3
			|| !qos_model->dchub_v3)
		return 0;

	dchub = qos_model->dchub_v3;
	highest = dchub->sop_count - 1;

	return dchub->sops[UTM_QOS_MODEL_V3_LOAD_LEVEL_ACTIVE_ALTERNATE_PSTATE]
			[highest].nominal_bandwidth_KBps;
}

static void dcn60_get_requested_memory_qos(
		struct clk_mgr *clk_mgr_base,
		struct dc_requested_memory_qos *qos)
{
	const struct dc_clocks *clks = &clk_mgr_base->clks;

	qos->bandwidth_lb_in_mbps = clks->utm_nominal_bandwidth_lb_KBps / 1000;
	qos->calculated_avg_bw_in_mbps = clks->required_avg_active_bandwidth_KBps / 1000;
	qos->max_latency_ub_in_ns = clks->utm_nominal_max_latency_ub_ns;
	qos->avg_latency_ub_in_ns = clks->utm_nominal_avg_latency_ub_ns;
	qos->max_bw_budget_in_mbps = dcn60_get_max_active_utm_bandwidth_kbps(clk_mgr_base) / 1000;
}

static unsigned int dcn60_override_memory_bandwidth_request(
		struct clk_mgr *clk_mgr_base,
		unsigned int bw_kbps)
{
	const struct dc *dc = clk_mgr_base->ctx->dc;
	struct dc_clocks new_clocks;
	unsigned int max_bw_kbps;
	unsigned int capped_bw_kbps;
	int num_steps;

	memcpy(&new_clocks, &clk_mgr_base->clks, sizeof(struct dc_clocks));

	if (bw_kbps == 0) {
		new_clocks.utm_nominal_bandwidth_lb_KBps = 0;
		new_clocks.utm_urgent_bandwidth_lb_KBps  = 0;
	} else {
		max_bw_kbps    = dcn60_get_max_active_utm_bandwidth_kbps(clk_mgr_base);
		capped_bw_kbps = (bw_kbps > max_bw_kbps) ? max_bw_kbps : bw_kbps;

		new_clocks.utm_nominal_bandwidth_lb_KBps = capped_bw_kbps;
		if (new_clocks.utm_urgent_bandwidth_lb_KBps < capped_bw_kbps)
			new_clocks.utm_urgent_bandwidth_lb_KBps = capped_bw_kbps;
	}

	num_steps = dcn60_build_update_bandwidth_clocks_sequence(clk_mgr_base,
			dc->current_state, &new_clocks, true);

	dcn60_execute_block_sequence(clk_mgr_base, num_steps);

	return new_clocks.utm_nominal_bandwidth_lb_KBps;
}

static void dcn60_set_idle_power_optimizations(struct clk_mgr *clk_mgr_base, bool enable)
{
	struct clk_mgr_internal *clk_mgr = TO_CLK_MGR_INTERNAL(clk_mgr_base);

	if (!clk_mgr->smu_present)
		return;

	dcn60_smu_set_display_idle_optimization(clk_mgr, enable);
}

/*
 * Build-for-BLS functions.
 * These build both bandwidth and display clock sequences into the clk_mgr's
 * internal block sequence array, then add a single CLK_MGR_UPDATE_CLOCKS step
 * to the HWSS block sequence whose executor will call
 * execute_clk_mgr_block_sequence to dispatch all accumulated steps.
 */
void dcn60_build_clock_update_for_bls(
		struct clk_mgr *clk_mgr_base,
		struct dc_state *context,
		bool safe_to_lower,
		struct block_sequence_state *seq_state)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn60_clk_mgr *clk_mgr60 = TO_DCN60_CLK_MGR(clk_mgr_internal);
	unsigned int num_bw_steps;
	unsigned int total_steps;

	/* Build bandwidth clocks sequence starting at index 0 */
	num_bw_steps = dcn60_build_update_bandwidth_clocks_sequence(clk_mgr_base,
			context,
			&context->bw_ctx.bw.dcn.clk,
			safe_to_lower);

	/* Build display clocks sequence appended after bandwidth steps */
	total_steps = dcn60_build_update_display_clocks_sequence(clk_mgr_base,
			context,
			&context->bw_ctx.bw.dcn.clk,
			safe_to_lower,
			num_bw_steps);

	/* Store total step count for the executor */
	clk_mgr60->num_block_sequence_steps = total_steps;

	/* Add single HWSS step that will execute all clk_mgr block sequence steps */
	hwss_add_clk_mgr_update_clocks(seq_state, clk_mgr_base);
}

static void dcn60_execute_clk_mgr_block_sequence_bls(struct clk_mgr *clk_mgr_base)
{
	struct clk_mgr_internal *clk_mgr_internal = TO_CLK_MGR_INTERNAL(clk_mgr_base);
	struct dcn60_clk_mgr *clk_mgr60 = TO_DCN60_CLK_MGR(clk_mgr_internal);

	dcn60_execute_block_sequence(clk_mgr_base, clk_mgr60->num_block_sequence_steps);
}

static struct clk_mgr_funcs dcn60_funcs = {
		.get_dp_ref_clk_frequency = dce12_get_dp_ref_freq_khz,
		.get_dtb_ref_clk_frequency = dcn60_get_dtb_ref_freq_khz,
		.update_clocks = dcn60_update_clocks,
		.dump_clk_registers = dcn60_dump_clk_registers,
		.init_clocks = dcn60_init_clocks,
		.set_hard_min_memclk = dcn60_set_hard_min_memclk,
		.get_memclk_states_from_smu = dcn60_get_memclk_states_from_smu,
		.are_clock_states_equal = dcn60_are_clock_states_equal,
		.enable_pme_wa = dcn60_enable_pme_wa,
		.is_smu_present = dcn60_is_smu_present,
		.get_dispclk_from_dentist = NULL,
		.get_max_clock_khz = dcn401_get_max_clock_khz,
		.override_memory_bandwidth_request = dcn60_override_memory_bandwidth_request,
		.get_requested_memory_qos = dcn60_get_requested_memory_qos,
		.set_idle_power_optimizations = dcn60_set_idle_power_optimizations,
		.build_clock_update_for_bls = dcn60_build_clock_update_for_bls,
		.execute_clk_mgr_block_sequence = dcn60_execute_clk_mgr_block_sequence_bls,
};

struct clk_mgr_internal *dcn60_clk_mgr_construct(
		struct dc_context *ctx,
		struct dccg *dccg)
{
	struct clk_log_info log_info = {0};
	struct dcn60_clk_mgr *clk_mgr60 = kzalloc_obj(struct dcn60_clk_mgr);
	struct clk_mgr_internal *clk_mgr;

	if (!clk_mgr60)
		return NULL;

	clk_mgr = &clk_mgr60->base;
	clk_mgr->base.ctx = ctx;
	clk_mgr->base.funcs = &dcn60_funcs;
	clk_mgr->regs = &clk_mgr_regs_dcn60;
	clk_mgr->clk_mgr_shift = &clk_mgr_shift_dcn60;
	clk_mgr->clk_mgr_mask = &clk_mgr_mask_dcn60;

	clk_mgr->dccg = dccg;
	clk_mgr->dfs_bypass_disp_clk = 0;

	clk_mgr->dprefclk_ss_percentage = 0;
	clk_mgr->dprefclk_ss_divider = 1000;
	clk_mgr->ss_on_dprefclk = false;
	clk_mgr->dfs_ref_freq_khz = 100000;

	/* Changed from DCN3.2_clock_frequency doc to match
	 * dcn60_dump_clk_registers from 4 * dentist_vco_freq_khz /
	 * dprefclk DID divider
	 */
	clk_mgr->base.dprefclk_khz = 720000;

		/* integer part is now VCO frequency in kHz */
		clk_mgr->base.dentist_vco_freq_khz = dcn60_get_vco_frequency_from_reg(clk_mgr);

		/* in case we don't get a value from the register, use default */
		if (clk_mgr->base.dentist_vco_freq_khz == 0)
			clk_mgr->base.dentist_vco_freq_khz = 4500000;

		dcn60_dump_clk_registers(&clk_mgr->base.boot_snapshot, &clk_mgr->base, &log_info);

		if (ctx->dc->debug.disable_dtb_ref_clk_switch &&
				clk_mgr->base.clks.ref_dtbclk_khz != clk_mgr->base.boot_snapshot.dtbclk) {
			clk_mgr->base.clks.ref_dtbclk_khz = clk_mgr->base.boot_snapshot.dtbclk;
		}

		if (clk_mgr->base.boot_snapshot.dprefclk != 0)
			clk_mgr->base.dprefclk_khz = clk_mgr->base.boot_snapshot.dprefclk;
		dcn60_clock_read_ss_info(clk_mgr);

	clk_mgr->dfs_bypass_enabled = false;

	clk_mgr->smu_present = false;

	clk_mgr->base.bw_params = kzalloc_obj(*clk_mgr->base.bw_params);
	if (!clk_mgr->base.bw_params)
		goto fail;

	clk_mgr->dal_init_table = dm_helpers_allocate_gpu_mem(clk_mgr->base.ctx,
			DC_MEM_ALLOC_TYPE_GART, sizeof(DalInitTable_t),
			&clk_mgr->dal_init_table_addr);
	if (!clk_mgr->dal_init_table)
		goto fail;

	return &clk_mgr60->base;

fail:
	BREAK_TO_DEBUGGER();
	dcn60_clk_mgr_destroy(clk_mgr);
	kfree(clk_mgr60);
	return NULL;
}

void dcn60_clk_mgr_destroy(struct clk_mgr_internal *clk_mgr)
{
	kfree(clk_mgr->base.bw_params);

	if (clk_mgr->dal_init_table)
		dm_helpers_free_gpu_mem(clk_mgr->base.ctx, DC_MEM_ALLOC_TYPE_GART,
				(void *)clk_mgr->dal_init_table);
}
