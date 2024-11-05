/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
#include <linux/delay.h>

#include "dm_services.h"
#include "basics/dc_common.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dcn20/dcn20_resource.h"
#include "dcn20_hwseq.h"
#include "dce/dce_hwseq.h"
#include "dcn20/dcn20_dsc.h"
#include "dcn20/dcn20_optc.h"
#include "abm.h"
#include "clk_mgr.h"
#include "dmcu.h"
#include "hubp.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "mcif_wb.h"
#include "dchubbub.h"
#include "reg_helper.h"
#include "dcn10/dcn10_cm_common.h"
#include "vm_helper.h"
#include "dccg.h"
#include "dc_dmub_srv.h"
#include "dce/dmub_hw_lock_mgr.h"
#include "hw_sequencer.h"
#include "dpcd_defs.h"
#include "inc/link_enc_cfg.h"
#include "link_hwss.h"
#include "link.h"
#include "dc_state_priv.h"

#define DC_LOGGER \
	dc_logger
#define DC_LOGGER_INIT(logger) \
	struct dal_logger *dc_logger = logger

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg

#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

void dcn20_log_color_state(struct dc *dc,
			   struct dc_log_buffer_ctx *log_ctx)
{
	struct dc_context *dc_ctx = dc->ctx;
	struct resource_pool *pool = dc->res_pool;
	int i;

	DTN_INFO("DPP:  DGAM mode  SHAPER mode  3DLUT mode  3DLUT bit depth"
		 "  3DLUT size  RGAM mode  GAMUT adjust  "
		 "C11        C12        C13        C14        "
		 "C21        C22        C23        C24        "
		 "C31        C32        C33        C34        \n");

	for (i = 0; i < pool->pipe_count; i++) {
		struct dpp *dpp = pool->dpps[i];
		struct dcn_dpp_state s = {0};

		dpp->funcs->dpp_read_state(dpp, &s);
		dpp->funcs->dpp_get_gamut_remap(dpp, &s.gamut_remap);

		if (!s.is_enabled)
			continue;

		DTN_INFO("[%2d]:  %8s  %11s  %10s  %15s  %10s  %9s  %12s  "
			 "%010lld %010lld %010lld %010lld "
			 "%010lld %010lld %010lld %010lld "
			 "%010lld %010lld %010lld %010lld",
			dpp->inst,
			(s.dgam_lut_mode == 0) ? "Bypass" :
			 ((s.dgam_lut_mode == 1) ? "sRGB" :
			 ((s.dgam_lut_mode == 2) ? "Ycc" :
			 ((s.dgam_lut_mode == 3) ? "RAM" :
			 ((s.dgam_lut_mode == 4) ? "RAM" :
						   "Unknown")))),
			(s.shaper_lut_mode == 1) ? "RAM A" :
			 ((s.shaper_lut_mode == 2) ? "RAM B" :
						     "Bypass"),
			(s.lut3d_mode == 1) ? "RAM A" :
			 ((s.lut3d_mode == 2) ? "RAM B" :
						"Bypass"),
			(s.lut3d_bit_depth <= 0) ? "12-bit" : "10-bit",
			(s.lut3d_size == 0) ? "17x17x17" : "9x9x9",
			(s.rgam_lut_mode == 1) ? "RAM A" :
			 ((s.rgam_lut_mode == 1) ? "RAM B" : "Bypass"),
			(s.gamut_remap.gamut_adjust_type == 0) ? "Bypass" :
			 ((s.gamut_remap.gamut_adjust_type == 1) ? "HW" :
								   "SW"),
			s.gamut_remap.temperature_matrix[0].value,
			s.gamut_remap.temperature_matrix[1].value,
			s.gamut_remap.temperature_matrix[2].value,
			s.gamut_remap.temperature_matrix[3].value,
			s.gamut_remap.temperature_matrix[4].value,
			s.gamut_remap.temperature_matrix[5].value,
			s.gamut_remap.temperature_matrix[6].value,
			s.gamut_remap.temperature_matrix[7].value,
			s.gamut_remap.temperature_matrix[8].value,
			s.gamut_remap.temperature_matrix[9].value,
			s.gamut_remap.temperature_matrix[10].value,
			s.gamut_remap.temperature_matrix[11].value);
		DTN_INFO("\n");
	}
	DTN_INFO("\n");
	DTN_INFO("DPP Color Caps: input_lut_shared:%d  icsc:%d"
		 "  dgam_ram:%d  dgam_rom: srgb:%d,bt2020:%d,gamma2_2:%d,pq:%d,hlg:%d"
		 "  post_csc:%d  gamcor:%d  dgam_rom_for_yuv:%d  3d_lut:%d"
		 "  blnd_lut:%d  oscs:%d\n\n",
		 dc->caps.color.dpp.input_lut_shared,
		 dc->caps.color.dpp.icsc,
		 dc->caps.color.dpp.dgam_ram,
		 dc->caps.color.dpp.dgam_rom_caps.srgb,
		 dc->caps.color.dpp.dgam_rom_caps.bt2020,
		 dc->caps.color.dpp.dgam_rom_caps.gamma2_2,
		 dc->caps.color.dpp.dgam_rom_caps.pq,
		 dc->caps.color.dpp.dgam_rom_caps.hlg,
		 dc->caps.color.dpp.post_csc,
		 dc->caps.color.dpp.gamma_corr,
		 dc->caps.color.dpp.dgam_rom_for_yuv,
		 dc->caps.color.dpp.hw_3d_lut,
		 dc->caps.color.dpp.ogam_ram,
		 dc->caps.color.dpp.ocsc);

	DTN_INFO("MPCC:  OPP  DPP  MPCCBOT  MODE  ALPHA_MODE  PREMULT  OVERLAP_ONLY  IDLE"
		 "  OGAM mode\n");

	for (i = 0; i < pool->mpcc_count; i++) {
		struct mpcc_state s = {0};

		pool->mpc->funcs->read_mpcc_state(pool->mpc, i, &s);
		if (s.opp_id != 0xf)
			DTN_INFO("[%2d]:  %2xh  %2xh  %6xh  %4d  %10d  %7d  %12d  %4d  %9s\n",
				i, s.opp_id, s.dpp_id, s.bot_mpcc_id,
				s.mode, s.alpha_mode, s.pre_multiplied_alpha, s.overlap_only,
				s.idle,
				(s.rgam_mode == 1) ? "RAM A" :
				 ((s.rgam_mode == 2) ? "RAM B" :
						       "Bypass"));
	}
	DTN_INFO("\n");
	DTN_INFO("MPC Color Caps: gamut_remap:%d, 3dlut:%d, ogam_ram:%d, ocsc:%d\n\n",
		 dc->caps.color.mpc.gamut_remap,
		 dc->caps.color.mpc.num_3dluts,
		 dc->caps.color.mpc.ogam_ram,
		 dc->caps.color.mpc.ocsc);
}


static int find_free_gsl_group(const struct dc *dc)
{
	if (dc->res_pool->gsl_groups.gsl_0 == 0)
		return 1;
	if (dc->res_pool->gsl_groups.gsl_1 == 0)
		return 2;
	if (dc->res_pool->gsl_groups.gsl_2 == 0)
		return 3;

	return 0;
}

/* NOTE: This is not a generic setup_gsl function (hence the suffix as_lock)
 * This is only used to lock pipes in pipe splitting case with immediate flip
 * Ordinary MPC/OTG locks suppress VUPDATE which doesn't help with immediate,
 * so we get tearing with freesync since we cannot flip multiple pipes
 * atomically.
 * We use GSL for this:
 * - immediate flip: find first available GSL group if not already assigned
 *                   program gsl with that group, set current OTG as master
 *                   and always us 0x4 = AND of flip_ready from all pipes
 * - vsync flip: disable GSL if used
 *
 * Groups in stream_res are stored as +1 from HW registers, i.e.
 * gsl_0 <=> pipe_ctx->stream_res.gsl_group == 1
 * Using a magic value like -1 would require tracking all inits/resets
 */
void dcn20_setup_gsl_group_as_lock(
		const struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool enable)
{
	struct gsl_params gsl;
	int group_idx;

	memset(&gsl, 0, sizeof(struct gsl_params));

	if (enable) {
		/* return if group already assigned since GSL was set up
		 * for vsync flip, we would unassign so it can't be "left over"
		 */
		if (pipe_ctx->stream_res.gsl_group > 0)
			return;

		group_idx = find_free_gsl_group(dc);
		ASSERT(group_idx != 0);
		pipe_ctx->stream_res.gsl_group = group_idx;

		/* set gsl group reg field and mark resource used */
		switch (group_idx) {
		case 1:
			gsl.gsl0_en = 1;
			dc->res_pool->gsl_groups.gsl_0 = 1;
			break;
		case 2:
			gsl.gsl1_en = 1;
			dc->res_pool->gsl_groups.gsl_1 = 1;
			break;
		case 3:
			gsl.gsl2_en = 1;
			dc->res_pool->gsl_groups.gsl_2 = 1;
			break;
		default:
			BREAK_TO_DEBUGGER();
			return; // invalid case
		}
		gsl.gsl_master_en = 1;
	} else {
		group_idx = pipe_ctx->stream_res.gsl_group;
		if (group_idx == 0)
			return; // if not in use, just return

		pipe_ctx->stream_res.gsl_group = 0;

		/* unset gsl group reg field and mark resource free */
		switch (group_idx) {
		case 1:
			gsl.gsl0_en = 0;
			dc->res_pool->gsl_groups.gsl_0 = 0;
			break;
		case 2:
			gsl.gsl1_en = 0;
			dc->res_pool->gsl_groups.gsl_1 = 0;
			break;
		case 3:
			gsl.gsl2_en = 0;
			dc->res_pool->gsl_groups.gsl_2 = 0;
			break;
		default:
			BREAK_TO_DEBUGGER();
			return;
		}
		gsl.gsl_master_en = 0;
	}

	/* at this point we want to program whether it's to enable or disable */
	if (pipe_ctx->stream_res.tg->funcs->set_gsl != NULL &&
		pipe_ctx->stream_res.tg->funcs->set_gsl_source_select != NULL) {
		pipe_ctx->stream_res.tg->funcs->set_gsl(
			pipe_ctx->stream_res.tg,
			&gsl);

		pipe_ctx->stream_res.tg->funcs->set_gsl_source_select(
			pipe_ctx->stream_res.tg, group_idx,	enable ? 4 : 0);
	} else
		BREAK_TO_DEBUGGER();
}

void dcn20_set_flip_control_gsl(
		struct pipe_ctx *pipe_ctx,
		bool flip_immediate)
{
	if (pipe_ctx && pipe_ctx->plane_res.hubp->funcs->hubp_set_flip_control_surface_gsl)
		pipe_ctx->plane_res.hubp->funcs->hubp_set_flip_control_surface_gsl(
				pipe_ctx->plane_res.hubp, flip_immediate);

}

void dcn20_enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable)
{
	bool force_on = true; /* disable power gating */
	uint32_t org_ip_request_cntl = 0;

	if (enable)
		force_on = false;

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	/* DCHUBP0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN0_PG_CONFIG, DOMAIN0_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN2_PG_CONFIG, DOMAIN2_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN4_PG_CONFIG, DOMAIN4_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN6_PG_CONFIG, DOMAIN6_POWER_FORCEON, force_on);
	if (REG(DOMAIN8_PG_CONFIG))
		REG_UPDATE(DOMAIN8_PG_CONFIG, DOMAIN8_POWER_FORCEON, force_on);
	if (REG(DOMAIN10_PG_CONFIG))
		REG_UPDATE(DOMAIN10_PG_CONFIG, DOMAIN8_POWER_FORCEON, force_on);

	/* DPP0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN1_PG_CONFIG, DOMAIN1_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN3_PG_CONFIG, DOMAIN3_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN5_PG_CONFIG, DOMAIN5_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN7_PG_CONFIG, DOMAIN7_POWER_FORCEON, force_on);
	if (REG(DOMAIN9_PG_CONFIG))
		REG_UPDATE(DOMAIN9_PG_CONFIG, DOMAIN9_POWER_FORCEON, force_on);
	if (REG(DOMAIN11_PG_CONFIG))
		REG_UPDATE(DOMAIN11_PG_CONFIG, DOMAIN9_POWER_FORCEON, force_on);

	/* DCS0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN16_PG_CONFIG, DOMAIN16_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN17_PG_CONFIG, DOMAIN17_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN18_PG_CONFIG, DOMAIN18_POWER_FORCEON, force_on);
	if (REG(DOMAIN19_PG_CONFIG))
		REG_UPDATE(DOMAIN19_PG_CONFIG, DOMAIN19_POWER_FORCEON, force_on);
	if (REG(DOMAIN20_PG_CONFIG))
		REG_UPDATE(DOMAIN20_PG_CONFIG, DOMAIN20_POWER_FORCEON, force_on);
	if (REG(DOMAIN21_PG_CONFIG))
		REG_UPDATE(DOMAIN21_PG_CONFIG, DOMAIN21_POWER_FORCEON, force_on);

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);

}

void dcn20_dccg_init(struct dce_hwseq *hws)
{
	/*
	 * set MICROSECOND_TIME_BASE_DIV
	 * 100Mhz refclk -> 0x120264
	 * 27Mhz refclk -> 0x12021b
	 * 48Mhz refclk -> 0x120230
	 *
	 */
	REG_WRITE(MICROSECOND_TIME_BASE_DIV, 0x120264);

	/*
	 * set MILLISECOND_TIME_BASE_DIV
	 * 100Mhz refclk -> 0x1186a0
	 * 27Mhz refclk -> 0x106978
	 * 48Mhz refclk -> 0x10bb80
	 *
	 */
	REG_WRITE(MILLISECOND_TIME_BASE_DIV, 0x1186a0);

	/* This value is dependent on the hardware pipeline delay so set once per SOC */
	REG_WRITE(DISPCLK_FREQ_CHANGE_CNTL, 0xe01003c);
}

void dcn20_disable_vga(
	struct dce_hwseq *hws)
{
	REG_WRITE(D1VGA_CONTROL, 0);
	REG_WRITE(D2VGA_CONTROL, 0);
	REG_WRITE(D3VGA_CONTROL, 0);
	REG_WRITE(D4VGA_CONTROL, 0);
	REG_WRITE(D5VGA_CONTROL, 0);
	REG_WRITE(D6VGA_CONTROL, 0);
}

void dcn20_program_triple_buffer(
	const struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	bool enable_triple_buffer)
{
	if (pipe_ctx->plane_res.hubp && pipe_ctx->plane_res.hubp->funcs) {
		pipe_ctx->plane_res.hubp->funcs->hubp_enable_tripleBuffer(
			pipe_ctx->plane_res.hubp,
			enable_triple_buffer);
	}
}

/* Blank pixel data during initialization */
void dcn20_init_blank(
		struct dc *dc,
		struct timing_generator *tg)
{
	struct dce_hwseq *hws = dc->hwseq;
	enum dc_color_space color_space;
	struct tg_color black_color = {0};
	struct output_pixel_processor *opp = NULL;
	struct output_pixel_processor *bottom_opp = NULL;
	uint32_t num_opps, opp_id_src0, opp_id_src1;
	uint32_t otg_active_width = 0, otg_active_height = 0;

	/* program opp dpg blank color */
	color_space = COLOR_SPACE_SRGB;
	color_space_to_black_color(dc, color_space, &black_color);

	/* get the OTG active size */
	tg->funcs->get_otg_active_size(tg,
			&otg_active_width,
			&otg_active_height);

	/* get the OPTC source */
	tg->funcs->get_optc_source(tg, &num_opps, &opp_id_src0, &opp_id_src1);

	if (opp_id_src0 >= dc->res_pool->res_cap->num_opp) {
		ASSERT(false);
		return;
	}
	opp = dc->res_pool->opps[opp_id_src0];

	/* don't override the blank pattern if already enabled with the correct one. */
	if (opp->funcs->dpg_is_blanked && opp->funcs->dpg_is_blanked(opp))
		return;

	if (num_opps == 2) {
		otg_active_width = otg_active_width / 2;

		if (opp_id_src1 >= dc->res_pool->res_cap->num_opp) {
			ASSERT(false);
			return;
		}
		bottom_opp = dc->res_pool->opps[opp_id_src1];
	}

	opp->funcs->opp_set_disp_pattern_generator(
			opp,
			CONTROLLER_DP_TEST_PATTERN_SOLID_COLOR,
			CONTROLLER_DP_COLOR_SPACE_UDEFINED,
			COLOR_DEPTH_UNDEFINED,
			&black_color,
			otg_active_width,
			otg_active_height,
			0);

	if (num_opps == 2) {
		bottom_opp->funcs->opp_set_disp_pattern_generator(
				bottom_opp,
				CONTROLLER_DP_TEST_PATTERN_SOLID_COLOR,
				CONTROLLER_DP_COLOR_SPACE_UDEFINED,
				COLOR_DEPTH_UNDEFINED,
				&black_color,
				otg_active_width,
				otg_active_height,
				0);
	}

	hws->funcs.wait_for_blank_complete(opp);
}

void dcn20_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl = 0;

	if (hws->ctx->dc->debug.disable_dsc_power_gate)
		return;

	if (REG(DOMAIN16_PG_CONFIG) == 0)
		return;

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_UPDATE(DOMAIN16_PG_CONFIG,
				DOMAIN16_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN16_PG_STATUS,
				DOMAIN16_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DSC1 */
		REG_UPDATE(DOMAIN17_PG_CONFIG,
				DOMAIN17_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN17_PG_STATUS,
				DOMAIN17_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DSC2 */
		REG_UPDATE(DOMAIN18_PG_CONFIG,
				DOMAIN18_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN18_PG_STATUS,
				DOMAIN18_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DSC3 */
		REG_UPDATE(DOMAIN19_PG_CONFIG,
				DOMAIN19_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN19_PG_STATUS,
				DOMAIN19_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 4: /* DSC4 */
		REG_UPDATE(DOMAIN20_PG_CONFIG,
				DOMAIN20_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN20_PG_STATUS,
				DOMAIN20_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 5: /* DSC5 */
		REG_UPDATE(DOMAIN21_PG_CONFIG,
				DOMAIN21_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN21_PG_STATUS,
				DOMAIN21_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}

void dcn20_dpp_pg_control(
		struct dce_hwseq *hws,
		unsigned int dpp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_dpp_power_gate)
		return;
	if (REG(DOMAIN1_PG_CONFIG) == 0)
		return;

	switch (dpp_inst) {
	case 0: /* DPP0 */
		REG_UPDATE(DOMAIN1_PG_CONFIG,
				DOMAIN1_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN1_PG_STATUS,
				DOMAIN1_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DPP1 */
		REG_UPDATE(DOMAIN3_PG_CONFIG,
				DOMAIN3_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN3_PG_STATUS,
				DOMAIN3_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DPP2 */
		REG_UPDATE(DOMAIN5_PG_CONFIG,
				DOMAIN5_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN5_PG_STATUS,
				DOMAIN5_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DPP3 */
		REG_UPDATE(DOMAIN7_PG_CONFIG,
				DOMAIN7_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN7_PG_STATUS,
				DOMAIN7_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 4: /* DPP4 */
		REG_UPDATE(DOMAIN9_PG_CONFIG,
				DOMAIN9_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN9_PG_STATUS,
				DOMAIN9_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 5: /* DPP5 */
		/*
		 * Do not power gate DPP5, should be left at HW default, power on permanently.
		 * PG on Pipe5 is De-featured, attempting to put it to PG state may result in hard
		 * reset.
		 * REG_UPDATE(DOMAIN11_PG_CONFIG,
		 *		DOMAIN11_POWER_GATE, power_gate);
		 *
		 * REG_WAIT(DOMAIN11_PG_STATUS,
		 *		DOMAIN11_PGFSM_PWR_STATUS, pwr_status,
		 * 		1, 1000);
		 */
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}


void dcn20_hubp_pg_control(
		struct dce_hwseq *hws,
		unsigned int hubp_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;

	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;
	if (REG(DOMAIN0_PG_CONFIG) == 0)
		return;

	switch (hubp_inst) {
	case 0: /* DCHUBP0 */
		REG_UPDATE(DOMAIN0_PG_CONFIG,
				DOMAIN0_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN0_PG_STATUS,
				DOMAIN0_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DCHUBP1 */
		REG_UPDATE(DOMAIN2_PG_CONFIG,
				DOMAIN2_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN2_PG_STATUS,
				DOMAIN2_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DCHUBP2 */
		REG_UPDATE(DOMAIN4_PG_CONFIG,
				DOMAIN4_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN4_PG_STATUS,
				DOMAIN4_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 3: /* DCHUBP3 */
		REG_UPDATE(DOMAIN6_PG_CONFIG,
				DOMAIN6_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN6_PG_STATUS,
				DOMAIN6_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 4: /* DCHUBP4 */
		REG_UPDATE(DOMAIN8_PG_CONFIG,
				DOMAIN8_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN8_PG_STATUS,
				DOMAIN8_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 5: /* DCHUBP5 */
		/*
		 * Do not power gate DCHUB5, should be left at HW default, power on permanently.
		 * PG on Pipe5 is De-featured, attempting to put it to PG state may result in hard
		 * reset.
		 * REG_UPDATE(DOMAIN10_PG_CONFIG,
		 *		DOMAIN10_POWER_GATE, power_gate);
		 *
		 * REG_WAIT(DOMAIN10_PG_STATUS,
		 *		DOMAIN10_PGFSM_PWR_STATUS, pwr_status,
		 *		1, 1000);
		 */
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
}


/* disable HW used by plane.
 * note:  cannot disable until disconnect is complete
 */
void dcn20_plane_atomic_disable(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;

	dc->hwss.wait_for_mpcc_disconnect(dc, dc->res_pool, pipe_ctx);

	/* In flip immediate with pipe splitting case GSL is used for
	 * synchronization so we must disable it when the plane is disabled.
	 */
	if (pipe_ctx->stream_res.gsl_group != 0)
		dcn20_setup_gsl_group_as_lock(dc, pipe_ctx, false);

	if (hubp->funcs->hubp_update_mall_sel)
		hubp->funcs->hubp_update_mall_sel(hubp, 0, false);

	dc->hwss.set_flip_control_gsl(pipe_ctx, false);

	hubp->funcs->hubp_clk_cntl(hubp, false);

	dpp->funcs->dpp_dppclk_control(dpp, false, false);

	hubp->power_gated = true;

	hws->funcs.plane_atomic_power_down(dc,
			pipe_ctx->plane_res.dpp,
			pipe_ctx->plane_res.hubp);

	pipe_ctx->stream = NULL;
	memset(&pipe_ctx->stream_res, 0, sizeof(pipe_ctx->stream_res));
	memset(&pipe_ctx->plane_res, 0, sizeof(pipe_ctx->plane_res));
	pipe_ctx->top_pipe = NULL;
	pipe_ctx->bottom_pipe = NULL;
	pipe_ctx->prev_odm_pipe = NULL;
	pipe_ctx->next_odm_pipe = NULL;
	pipe_ctx->plane_state = NULL;
}


void dcn20_disable_plane(struct dc *dc, struct dc_state *state, struct pipe_ctx *pipe_ctx)
{
	bool is_phantom = dc_state_get_pipe_subvp_type(state, pipe_ctx) == SUBVP_PHANTOM;
	struct timing_generator *tg = is_phantom ? pipe_ctx->stream_res.tg : NULL;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (!pipe_ctx->plane_res.hubp || pipe_ctx->plane_res.hubp->power_gated)
		return;

	dcn20_plane_atomic_disable(dc, pipe_ctx);

	/* Turn back off the phantom OTG after the phantom plane is fully disabled
	 */
	if (is_phantom)
		if (tg && tg->funcs->disable_phantom_crtc)
			tg->funcs->disable_phantom_crtc(tg);

	DC_LOG_DC("Power down front end %d\n",
					pipe_ctx->pipe_idx);
}

void dcn20_disable_pixel_data(struct dc *dc, struct pipe_ctx *pipe_ctx, bool blank)
{
	dcn20_blank_pixel_data(dc, pipe_ctx, blank);
}

static int calc_mpc_flow_ctrl_cnt(const struct dc_stream_state *stream,
		int opp_cnt, bool is_two_pixels_per_container)
{
	bool hblank_halved = is_two_pixels_per_container;
	int flow_ctrl_cnt;

	if (opp_cnt >= 2)
		hblank_halved = true;

	flow_ctrl_cnt = stream->timing.h_total - stream->timing.h_addressable -
			stream->timing.h_border_left -
			stream->timing.h_border_right;

	if (hblank_halved)
		flow_ctrl_cnt /= 2;

	/* ODM combine 4:1 case */
	if (opp_cnt == 4)
		flow_ctrl_cnt /= 2;

	return flow_ctrl_cnt;
}

static enum phyd32clk_clock_source get_phyd32clk_src(struct dc_link *link)
{
	switch (link->link_enc->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		return PHYD32CLKA;
	case TRANSMITTER_UNIPHY_B:
		return PHYD32CLKB;
	case TRANSMITTER_UNIPHY_C:
		return PHYD32CLKC;
	case TRANSMITTER_UNIPHY_D:
		return PHYD32CLKD;
	case TRANSMITTER_UNIPHY_E:
		return PHYD32CLKE;
	default:
		return PHYD32CLKA;
	}
}

static int get_odm_segment_count(struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *odm_pipe = pipe_ctx->next_odm_pipe;
	int count = 1;

	while (odm_pipe != NULL) {
		count++;
		odm_pipe = odm_pipe->next_odm_pipe;
	}

	return count;
}

enum dc_status dcn20_enable_stream_timing(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct drr_params params = {0};
	unsigned int event_triggers = 0;
	int opp_cnt = 1;
	int opp_inst[MAX_PIPES] = {0};
	bool interlace = stream->timing.flags.INTERLACE;
	int i;
	struct mpc_dwb_flow_control flow_control;
	struct mpc *mpc = dc->res_pool->mpc;
	bool is_two_pixels_per_container =
			pipe_ctx->stream_res.tg->funcs->is_two_pixels_per_container(&stream->timing);
	bool rate_control_2x_pclk = (interlace || is_two_pixels_per_container);
	int odm_slice_width;
	int last_odm_slice_width;
	struct pipe_ctx *opp_heads[MAX_PIPES];

	if (dc->res_pool->dccg->funcs->set_pixel_rate_div)
		dc->res_pool->dccg->funcs->set_pixel_rate_div(
			dc->res_pool->dccg,
			pipe_ctx->stream_res.tg->inst,
			pipe_ctx->pixel_rate_divider.div_factor1,
			pipe_ctx->pixel_rate_divider.div_factor2);

	/* by upper caller loop, pipe0 is parent pipe and be called first.
	 * back end is set up by for pipe0. Other children pipe share back end
	 * with pipe 0. No program is needed.
	 */
	if (pipe_ctx->top_pipe != NULL)
		return DC_OK;

	/* TODO check if timing_changed, disable stream if timing changed */

	opp_cnt = resource_get_opp_heads_for_otg_master(pipe_ctx, &context->res_ctx, opp_heads);
	for (i = 0; i < opp_cnt; i++)
		opp_inst[i] = opp_heads[i]->stream_res.opp->inst;

	odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, false);
	last_odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, true);
	if (opp_cnt > 1)
		pipe_ctx->stream_res.tg->funcs->set_odm_combine(
				pipe_ctx->stream_res.tg,
				opp_inst, opp_cnt, odm_slice_width,
				last_odm_slice_width);

	/* HW program guide assume display already disable
	 * by unplug sequence. OTG assume stop.
	 */
	pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, true);

	if (false == pipe_ctx->clock_source->funcs->program_pix_clk(
			pipe_ctx->clock_source,
			&pipe_ctx->stream_res.pix_clk_params,
			dc->link_srv->dp_get_encoding_format(&pipe_ctx->link_config.dp_link_settings),
			&pipe_ctx->pll_settings)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}

	if (dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		struct dccg *dccg = dc->res_pool->dccg;
		struct timing_generator *tg = pipe_ctx->stream_res.tg;
		struct dtbclk_dto_params dto_params = {0};

		if (dccg->funcs->set_dtbclk_p_src)
			dccg->funcs->set_dtbclk_p_src(dccg, DTBCLK0, tg->inst);

		dto_params.otg_inst = tg->inst;
		dto_params.pixclk_khz = pipe_ctx->stream->timing.pix_clk_100hz / 10;
		dto_params.num_odm_segments = get_odm_segment_count(pipe_ctx);
		dto_params.timing = &pipe_ctx->stream->timing;
		dto_params.ref_dtbclk_khz = dc->clk_mgr->funcs->get_dtb_ref_clk_frequency(dc->clk_mgr);
		dccg->funcs->set_dtbclk_dto(dccg, &dto_params);
	}

	if (dc_is_hdmi_tmds_signal(stream->signal)) {
		stream->link->phy_state.symclk_ref_cnts.otg = 1;
		if (stream->link->phy_state.symclk_state == SYMCLK_OFF_TX_OFF)
			stream->link->phy_state.symclk_state = SYMCLK_ON_TX_OFF;
		else
			stream->link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
	}

	if (dc->hwseq->funcs.PLAT_58856_wa && (!dc_is_dp_signal(stream->signal)))
		dc->hwseq->funcs.PLAT_58856_wa(context, pipe_ctx);

	pipe_ctx->stream_res.tg->funcs->program_timing(
			pipe_ctx->stream_res.tg,
			&stream->timing,
			pipe_ctx->pipe_dlg_param.vready_offset,
			pipe_ctx->pipe_dlg_param.vstartup_start,
			pipe_ctx->pipe_dlg_param.vupdate_offset,
			pipe_ctx->pipe_dlg_param.vupdate_width,
			pipe_ctx->pipe_dlg_param.pstate_keepout,
			pipe_ctx->stream->signal,
			true);

	rate_control_2x_pclk = rate_control_2x_pclk || opp_cnt > 1;
	flow_control.flow_ctrl_mode = 0;
	flow_control.flow_ctrl_cnt0 = 0x80;
	flow_control.flow_ctrl_cnt1 = calc_mpc_flow_ctrl_cnt(stream, opp_cnt,
			is_two_pixels_per_container);
	if (mpc->funcs->set_out_rate_control) {
		for (i = 0; i < opp_cnt; ++i) {
			mpc->funcs->set_out_rate_control(
					mpc, opp_inst[i],
					true,
					rate_control_2x_pclk,
					&flow_control);
		}
	}

	for (i = 0; i < opp_cnt; i++) {
		opp_heads[i]->stream_res.opp->funcs->opp_pipe_clock_control(
				opp_heads[i]->stream_res.opp,
				true);
		opp_heads[i]->stream_res.opp->funcs->opp_program_left_edge_extra_pixel(
				opp_heads[i]->stream_res.opp,
				stream->timing.pixel_encoding,
				resource_is_pipe_type(opp_heads[i], OTG_MASTER));
	}

	hws->funcs.blank_pixel_data(dc, pipe_ctx, true);

	/* VTG is  within DCHUB command block. DCFCLK is always on */
	if (false == pipe_ctx->stream_res.tg->funcs->enable_crtc(pipe_ctx->stream_res.tg)) {
		BREAK_TO_DEBUGGER();
		return DC_ERROR_UNEXPECTED;
	}

	hws->funcs.wait_for_blank_complete(pipe_ctx->stream_res.opp);

	params.vertical_total_min = stream->adjust.v_total_min;
	params.vertical_total_max = stream->adjust.v_total_max;
	params.vertical_total_mid = stream->adjust.v_total_mid;
	params.vertical_total_mid_frame_num = stream->adjust.v_total_mid_frame_num;
	if (pipe_ctx->stream_res.tg->funcs->set_drr)
		pipe_ctx->stream_res.tg->funcs->set_drr(
			pipe_ctx->stream_res.tg, &params);

	// DRR should set trigger event to monitor surface update event
	if (stream->adjust.v_total_min != 0 && stream->adjust.v_total_max != 0)
		event_triggers = 0x80;
	/* Event triggers and num frames initialized for DRR, but can be
	 * later updated for PSR use. Note DRR trigger events are generated
	 * regardless of whether num frames met.
	 */
	if (pipe_ctx->stream_res.tg->funcs->set_static_screen_control)
		pipe_ctx->stream_res.tg->funcs->set_static_screen_control(
				pipe_ctx->stream_res.tg, event_triggers, 2);

	/* TODO program crtc source select for non-virtual signal*/
	/* TODO program FMT */
	/* TODO setup link_enc */
	/* TODO set stream attributes */
	/* TODO program audio */
	/* TODO enable stream if timing changed */
	/* TODO unblank stream if DP */

	if (dc_state_get_pipe_subvp_type(context, pipe_ctx) == SUBVP_PHANTOM) {
		if (pipe_ctx->stream_res.tg->funcs->phantom_crtc_post_enable)
			pipe_ctx->stream_res.tg->funcs->phantom_crtc_post_enable(pipe_ctx->stream_res.tg);
	}

	return DC_OK;
}

void dcn20_program_output_csc(struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum dc_color_space colorspace,
		uint16_t *matrix,
		int opp_id)
{
	struct mpc *mpc = dc->res_pool->mpc;
	enum mpc_output_csc_mode ocsc_mode = MPC_OUTPUT_CSC_COEF_A;
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;

	if (mpc->funcs->power_on_mpc_mem_pwr)
		mpc->funcs->power_on_mpc_mem_pwr(mpc, mpcc_id, true);

	if (pipe_ctx->stream->csc_color_matrix.enable_adjustment == true) {
		if (mpc->funcs->set_output_csc != NULL)
			mpc->funcs->set_output_csc(mpc,
					opp_id,
					matrix,
					ocsc_mode);
	} else {
		if (mpc->funcs->set_ocsc_default != NULL)
			mpc->funcs->set_ocsc_default(mpc,
					opp_id,
					colorspace,
					ocsc_mode);
	}
}

bool dcn20_set_output_transfer_func(struct dc *dc, struct pipe_ctx *pipe_ctx,
				const struct dc_stream_state *stream)
{
	int mpcc_id = pipe_ctx->plane_res.hubp->inst;
	struct mpc *mpc = pipe_ctx->stream_res.opp->ctx->dc->res_pool->mpc;
	const struct pwl_params *params = NULL;
	/*
	 * program OGAM only for the top pipe
	 * if there is a pipe split then fix diagnostic is required:
	 * how to pass OGAM parameter for stream.
	 * if programming for all pipes is required then remove condition
	 * pipe_ctx->top_pipe == NULL ,but then fix the diagnostic.
	 */
	if (mpc->funcs->power_on_mpc_mem_pwr)
		mpc->funcs->power_on_mpc_mem_pwr(mpc, mpcc_id, true);
	if (pipe_ctx->top_pipe == NULL
			&& mpc->funcs->set_output_gamma) {
		if (stream->out_transfer_func.type == TF_TYPE_HWPWL)
			params = &stream->out_transfer_func.pwl;
		else if (pipe_ctx->stream->out_transfer_func.type ==
			TF_TYPE_DISTRIBUTED_POINTS &&
			cm_helper_translate_curve_to_hw_format(dc->ctx,
			&stream->out_transfer_func,
			&mpc->blender_params, false))
			params = &mpc->blender_params;
		/*
		 * there is no ROM
		 */
		if (stream->out_transfer_func.type == TF_TYPE_PREDEFINED)
			BREAK_TO_DEBUGGER();
	}
	/*
	 * if above if is not executed then 'params' equal to 0 and set in bypass
	 */
	if (mpc->funcs->set_output_gamma)
		mpc->funcs->set_output_gamma(mpc, mpcc_id, params);

	return true;
}

bool dcn20_set_blend_lut(
	struct pipe_ctx *pipe_ctx, const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	bool result = true;
	const struct pwl_params *blend_lut = NULL;

	if (plane_state->blend_tf.type == TF_TYPE_HWPWL)
		blend_lut = &plane_state->blend_tf.pwl;
	else if (plane_state->blend_tf.type == TF_TYPE_DISTRIBUTED_POINTS) {
		cm_helper_translate_curve_to_hw_format(plane_state->ctx,
				&plane_state->blend_tf,
				&dpp_base->regamma_params, false);
		blend_lut = &dpp_base->regamma_params;
	}
	result = dpp_base->funcs->dpp_program_blnd_lut(dpp_base, blend_lut);

	return result;
}

bool dcn20_set_shaper_3dlut(
	struct pipe_ctx *pipe_ctx, const struct dc_plane_state *plane_state)
{
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	bool result = true;
	const struct pwl_params *shaper_lut = NULL;

	if (plane_state->in_shaper_func.type == TF_TYPE_HWPWL)
		shaper_lut = &plane_state->in_shaper_func.pwl;
	else if (plane_state->in_shaper_func.type == TF_TYPE_DISTRIBUTED_POINTS) {
		cm_helper_translate_curve_to_hw_format(plane_state->ctx,
				&plane_state->in_shaper_func,
				&dpp_base->shaper_params, true);
		shaper_lut = &dpp_base->shaper_params;
	}

	result = dpp_base->funcs->dpp_program_shaper_lut(dpp_base, shaper_lut);
	if (plane_state->lut3d_func.state.bits.initialized == 1)
		result = dpp_base->funcs->dpp_program_3dlut(dpp_base,
								&plane_state->lut3d_func.lut_3d);
	else
		result = dpp_base->funcs->dpp_program_3dlut(dpp_base, NULL);

	return result;
}

bool dcn20_set_input_transfer_func(struct dc *dc,
				struct pipe_ctx *pipe_ctx,
				const struct dc_plane_state *plane_state)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dpp *dpp_base = pipe_ctx->plane_res.dpp;
	const struct dc_transfer_func *tf = NULL;
	bool result = true;
	bool use_degamma_ram = false;

	if (dpp_base == NULL || plane_state == NULL)
		return false;

	hws->funcs.set_shaper_3dlut(pipe_ctx, plane_state);
	hws->funcs.set_blend_lut(pipe_ctx, plane_state);

	tf = &plane_state->in_transfer_func;

	if (tf->type == TF_TYPE_HWPWL || tf->type == TF_TYPE_DISTRIBUTED_POINTS)
		use_degamma_ram = true;

	if (use_degamma_ram == true) {
		if (tf->type == TF_TYPE_HWPWL)
			dpp_base->funcs->dpp_program_degamma_pwl(dpp_base,
					&tf->pwl);
		else if (tf->type == TF_TYPE_DISTRIBUTED_POINTS) {
			cm_helper_translate_curve_to_degamma_hw_format(tf,
					&dpp_base->degamma_params);
			dpp_base->funcs->dpp_program_degamma_pwl(dpp_base,
				&dpp_base->degamma_params);
		}
		return true;
	}
	/* handle here the optimized cases when de-gamma ROM could be used.
	 *
	 */
	if (tf->type == TF_TYPE_PREDEFINED) {
		switch (tf->tf) {
		case TRANSFER_FUNCTION_SRGB:
			dpp_base->funcs->dpp_set_degamma(dpp_base,
					IPP_DEGAMMA_MODE_HW_sRGB);
			break;
		case TRANSFER_FUNCTION_BT709:
			dpp_base->funcs->dpp_set_degamma(dpp_base,
					IPP_DEGAMMA_MODE_HW_xvYCC);
			break;
		case TRANSFER_FUNCTION_LINEAR:
			dpp_base->funcs->dpp_set_degamma(dpp_base,
					IPP_DEGAMMA_MODE_BYPASS);
			break;
		case TRANSFER_FUNCTION_PQ:
			dpp_base->funcs->dpp_set_degamma(dpp_base, IPP_DEGAMMA_MODE_USER_PWL);
			cm_helper_translate_curve_to_degamma_hw_format(tf, &dpp_base->degamma_params);
			dpp_base->funcs->dpp_program_degamma_pwl(dpp_base, &dpp_base->degamma_params);
			result = true;
			break;
		default:
			result = false;
			break;
		}
	} else if (tf->type == TF_TYPE_BYPASS)
		dpp_base->funcs->dpp_set_degamma(dpp_base,
				IPP_DEGAMMA_MODE_BYPASS);
	else {
		/*
		 * if we are here, we did not handle correctly.
		 * fix is required for this use case
		 */
		BREAK_TO_DEBUGGER();
		dpp_base->funcs->dpp_set_degamma(dpp_base,
				IPP_DEGAMMA_MODE_BYPASS);
	}

	return result;
}

void dcn20_update_odm(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;
	int opp_inst[MAX_PIPES] = { pipe_ctx->stream_res.opp->inst };
	int odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, false);
	int last_odm_slice_width = resource_get_odm_slice_dst_width(pipe_ctx, true);

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		opp_inst[opp_cnt] = odm_pipe->stream_res.opp->inst;
		opp_cnt++;
	}

	if (opp_cnt > 1)
		pipe_ctx->stream_res.tg->funcs->set_odm_combine(
				pipe_ctx->stream_res.tg,
				opp_inst, opp_cnt,
				odm_slice_width, last_odm_slice_width);
	else
		pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);
}

void dcn20_blank_pixel_data(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		bool blank)
{
	struct tg_color black_color = {0};
	struct stream_resource *stream_res = &pipe_ctx->stream_res;
	struct dc_stream_state *stream = pipe_ctx->stream;
	enum dc_color_space color_space = stream->output_color_space;
	enum controller_dp_test_pattern test_pattern = CONTROLLER_DP_TEST_PATTERN_SOLID_COLOR;
	enum controller_dp_color_space test_pattern_color_space = CONTROLLER_DP_COLOR_SPACE_UDEFINED;
	struct pipe_ctx *odm_pipe;
	struct rect odm_slice_src;

	if (stream->link->test_pattern_enabled)
		return;

	/* get opp dpg blank color */
	color_space_to_black_color(dc, color_space, &black_color);

	if (blank) {
		dc->hwss.set_abm_immediate_disable(pipe_ctx);

		if (dc->debug.visual_confirm != VISUAL_CONFIRM_DISABLE) {
			test_pattern = CONTROLLER_DP_TEST_PATTERN_COLORSQUARES;
			test_pattern_color_space = CONTROLLER_DP_COLOR_SPACE_RGB;
		}
	} else {
		test_pattern = CONTROLLER_DP_TEST_PATTERN_VIDEOMODE;
	}

	odm_pipe = pipe_ctx;

	while (odm_pipe->next_odm_pipe) {
		odm_slice_src = resource_get_odm_slice_src_rect(odm_pipe);
		dc->hwss.set_disp_pattern_generator(dc,
				odm_pipe,
				test_pattern,
				test_pattern_color_space,
				stream->timing.display_color_depth,
				&black_color,
				odm_slice_src.width,
				odm_slice_src.height,
				odm_slice_src.x);
		odm_pipe = odm_pipe->next_odm_pipe;
	}

	odm_slice_src = resource_get_odm_slice_src_rect(odm_pipe);
	dc->hwss.set_disp_pattern_generator(dc,
			odm_pipe,
			test_pattern,
			test_pattern_color_space,
			stream->timing.display_color_depth,
			&black_color,
			odm_slice_src.width,
			odm_slice_src.height,
			odm_slice_src.x);

	if (!blank)
		if (stream_res->abm) {
			dc->hwss.set_pipe(pipe_ctx);
			stream_res->abm->funcs->set_abm_level(stream_res->abm, stream->abm_level);
		}
}


static void dcn20_power_on_plane_resources(
	struct dce_hwseq *hws,
	struct pipe_ctx *pipe_ctx)
{
	DC_LOGGER_INIT(hws->ctx->logger);

	if (hws->funcs.dpp_root_clock_control)
		hws->funcs.dpp_root_clock_control(hws, pipe_ctx->plane_res.dpp->inst, true);

	if (REG(DC_IP_REQUEST_CNTL)) {
		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 1);

		if (hws->funcs.dpp_pg_control)
			hws->funcs.dpp_pg_control(hws, pipe_ctx->plane_res.dpp->inst, true);

		if (hws->funcs.hubp_pg_control)
			hws->funcs.hubp_pg_control(hws, pipe_ctx->plane_res.hubp->inst, true);

		REG_SET(DC_IP_REQUEST_CNTL, 0,
				IP_REQUEST_EN, 0);
		DC_LOG_DEBUG(
				"Un-gated front end for pipe %d\n", pipe_ctx->plane_res.hubp->inst);
	}
}

static void dcn20_enable_plane(struct dc *dc, struct pipe_ctx *pipe_ctx,
			       struct dc_state *context)
{
	//if (dc->debug.sanity_checks) {
	//	dcn10_verify_allow_pstate_change_high(dc);
	//}
	dcn20_power_on_plane_resources(dc->hwseq, pipe_ctx);

	/* enable DCFCLK current DCHUB */
	pipe_ctx->plane_res.hubp->funcs->hubp_clk_cntl(pipe_ctx->plane_res.hubp, true);

	/* initialize HUBP on power up */
	pipe_ctx->plane_res.hubp->funcs->hubp_init(pipe_ctx->plane_res.hubp);

	/* make sure OPP_PIPE_CLOCK_EN = 1 */
	pipe_ctx->stream_res.opp->funcs->opp_pipe_clock_control(
			pipe_ctx->stream_res.opp,
			true);

/* TODO: enable/disable in dm as per update type.
	if (plane_state) {
		DC_LOG_DC(dc->ctx->logger,
				"Pipe:%d 0x%x: addr hi:0x%x, "
				"addr low:0x%x, "
				"src: %d, %d, %d,"
				" %d; dst: %d, %d, %d, %d;\n",
				pipe_ctx->pipe_idx,
				plane_state,
				plane_state->address.grph.addr.high_part,
				plane_state->address.grph.addr.low_part,
				plane_state->src_rect.x,
				plane_state->src_rect.y,
				plane_state->src_rect.width,
				plane_state->src_rect.height,
				plane_state->dst_rect.x,
				plane_state->dst_rect.y,
				plane_state->dst_rect.width,
				plane_state->dst_rect.height);

		DC_LOG_DC(dc->ctx->logger,
				"Pipe %d: width, height, x, y         format:%d\n"
				"viewport:%d, %d, %d, %d\n"
				"recout:  %d, %d, %d, %d\n",
				pipe_ctx->pipe_idx,
				plane_state->format,
				pipe_ctx->plane_res.scl_data.viewport.width,
				pipe_ctx->plane_res.scl_data.viewport.height,
				pipe_ctx->plane_res.scl_data.viewport.x,
				pipe_ctx->plane_res.scl_data.viewport.y,
				pipe_ctx->plane_res.scl_data.recout.width,
				pipe_ctx->plane_res.scl_data.recout.height,
				pipe_ctx->plane_res.scl_data.recout.x,
				pipe_ctx->plane_res.scl_data.recout.y);
		print_rq_dlg_ttu(dc, pipe_ctx);
	}
*/
	if (dc->vm_pa_config.valid) {
		struct vm_system_aperture_param apt;

		apt.sys_default.quad_part = 0;

		apt.sys_low.quad_part = dc->vm_pa_config.system_aperture.start_addr;
		apt.sys_high.quad_part = dc->vm_pa_config.system_aperture.end_addr;

		// Program system aperture settings
		pipe_ctx->plane_res.hubp->funcs->hubp_set_vm_system_aperture_settings(pipe_ctx->plane_res.hubp, &apt);
	}

	if (!pipe_ctx->top_pipe
		&& pipe_ctx->plane_state
		&& pipe_ctx->plane_state->flip_int_enabled
		&& pipe_ctx->plane_res.hubp->funcs->hubp_set_flip_int)
			pipe_ctx->plane_res.hubp->funcs->hubp_set_flip_int(pipe_ctx->plane_res.hubp);

//	if (dc->debug.sanity_checks) {
//		dcn10_verify_allow_pstate_change_high(dc);
//	}
}

void dcn20_pipe_control_lock(
	struct dc *dc,
	struct pipe_ctx *pipe,
	bool lock)
{
	struct pipe_ctx *temp_pipe;
	bool flip_immediate = false;

	/* use TG master update lock to lock everything on the TG
	 * therefore only top pipe need to lock
	 */
	if (!pipe || pipe->top_pipe)
		return;

	if (pipe->plane_state != NULL)
		flip_immediate = pipe->plane_state->flip_immediate;

	if  (pipe->stream_res.gsl_group > 0) {
	    temp_pipe = pipe->bottom_pipe;
	    while (!flip_immediate && temp_pipe) {
		    if (temp_pipe->plane_state != NULL)
			    flip_immediate = temp_pipe->plane_state->flip_immediate;
		    temp_pipe = temp_pipe->bottom_pipe;
	    }
	}

	if (flip_immediate && lock) {
		const int TIMEOUT_FOR_FLIP_PENDING_US = 100000;
		unsigned int polling_interval_us = 1;
		int i;

		temp_pipe = pipe;
		while (temp_pipe) {
			if (temp_pipe->plane_state && temp_pipe->plane_state->flip_immediate) {
				for (i = 0; i < TIMEOUT_FOR_FLIP_PENDING_US / polling_interval_us; ++i) {
					if (!temp_pipe->plane_res.hubp->funcs->hubp_is_flip_pending(temp_pipe->plane_res.hubp))
						break;
					udelay(polling_interval_us);
				}

				/* no reason it should take this long for immediate flips */
				ASSERT(i != TIMEOUT_FOR_FLIP_PENDING_US);
			}
			temp_pipe = temp_pipe->bottom_pipe;
		}
	}

	/* In flip immediate and pipe splitting case, we need to use GSL
	 * for synchronization. Only do setup on locking and on flip type change.
	 */
	if (lock && (pipe->bottom_pipe != NULL || !flip_immediate))
		if ((flip_immediate && pipe->stream_res.gsl_group == 0) ||
		    (!flip_immediate && pipe->stream_res.gsl_group > 0))
			dcn20_setup_gsl_group_as_lock(dc, pipe, flip_immediate);

	if (pipe->plane_state != NULL)
		flip_immediate = pipe->plane_state->flip_immediate;

	temp_pipe = pipe->bottom_pipe;
	while (flip_immediate && temp_pipe) {
	    if (temp_pipe->plane_state != NULL)
		flip_immediate = temp_pipe->plane_state->flip_immediate;
	    temp_pipe = temp_pipe->bottom_pipe;
	}

	if (!lock && pipe->stream_res.gsl_group > 0 && pipe->plane_state &&
		!flip_immediate)
	    dcn20_setup_gsl_group_as_lock(dc, pipe, false);

	if (pipe->stream && should_use_dmub_lock(pipe->stream->link)) {
		union dmub_hw_lock_flags hw_locks = { 0 };
		struct dmub_hw_lock_inst_flags inst_flags = { 0 };

		hw_locks.bits.lock_pipe = 1;
		inst_flags.otg_inst =  pipe->stream_res.tg->inst;

		if (pipe->plane_state != NULL)
			hw_locks.bits.triple_buffer_lock = pipe->plane_state->triplebuffer_flips;

		dmub_hw_lock_mgr_cmd(dc->ctx->dmub_srv,
					lock,
					&hw_locks,
					&inst_flags);
	} else if (pipe->plane_state != NULL && pipe->plane_state->triplebuffer_flips) {
		if (lock)
			pipe->stream_res.tg->funcs->triplebuffer_lock(pipe->stream_res.tg);
		else
			pipe->stream_res.tg->funcs->triplebuffer_unlock(pipe->stream_res.tg);
	} else {
		if (lock)
			pipe->stream_res.tg->funcs->lock(pipe->stream_res.tg);
		else
			pipe->stream_res.tg->funcs->unlock(pipe->stream_res.tg);
	}
}

static void dcn20_detect_pipe_changes(struct dc_state *old_state,
		struct dc_state *new_state,
		struct pipe_ctx *old_pipe,
		struct pipe_ctx *new_pipe)
{
	bool old_is_phantom = dc_state_get_pipe_subvp_type(old_state, old_pipe) == SUBVP_PHANTOM;
	bool new_is_phantom = dc_state_get_pipe_subvp_type(new_state, new_pipe) == SUBVP_PHANTOM;

	new_pipe->update_flags.raw = 0;

	/* If non-phantom pipe is being transitioned to a phantom pipe,
	 * set disable and return immediately. This is because the pipe
	 * that was previously in use must be fully disabled before we
	 * can "enable" it as a phantom pipe (since the OTG will certainly
	 * be different). The post_unlock sequence will set the correct
	 * update flags to enable the phantom pipe.
	 */
	if (old_pipe->plane_state && !old_is_phantom &&
			new_pipe->plane_state && new_is_phantom) {
		new_pipe->update_flags.bits.disable = 1;
		return;
	}

	if (resource_is_pipe_type(new_pipe, OTG_MASTER) &&
			resource_is_odm_topology_changed(new_pipe, old_pipe))
		/* Detect odm changes */
		new_pipe->update_flags.bits.odm = 1;

	/* Exit on unchanged, unused pipe */
	if (!old_pipe->plane_state && !new_pipe->plane_state)
		return;
	/* Detect pipe enable/disable */
	if (!old_pipe->plane_state && new_pipe->plane_state) {
		new_pipe->update_flags.bits.enable = 1;
		new_pipe->update_flags.bits.mpcc = 1;
		new_pipe->update_flags.bits.dppclk = 1;
		new_pipe->update_flags.bits.hubp_interdependent = 1;
		new_pipe->update_flags.bits.hubp_rq_dlg_ttu = 1;
		new_pipe->update_flags.bits.unbounded_req = 1;
		new_pipe->update_flags.bits.gamut_remap = 1;
		new_pipe->update_flags.bits.scaler = 1;
		new_pipe->update_flags.bits.viewport = 1;
		new_pipe->update_flags.bits.det_size = 1;
		if (new_pipe->stream->test_pattern.type != DP_TEST_PATTERN_VIDEO_MODE &&
				new_pipe->stream_res.test_pattern_params.width != 0 &&
				new_pipe->stream_res.test_pattern_params.height != 0)
			new_pipe->update_flags.bits.test_pattern_changed = 1;
		if (!new_pipe->top_pipe && !new_pipe->prev_odm_pipe) {
			new_pipe->update_flags.bits.odm = 1;
			new_pipe->update_flags.bits.global_sync = 1;
		}
		return;
	}

	/* For SubVP we need to unconditionally enable because any phantom pipes are
	 * always removed then newly added for every full updates whenever SubVP is in use.
	 * The remove-add sequence of the phantom pipe always results in the pipe
	 * being blanked in enable_stream_timing (DPG).
	 */
	if (new_pipe->stream && dc_state_get_pipe_subvp_type(new_state, new_pipe) == SUBVP_PHANTOM)
		new_pipe->update_flags.bits.enable = 1;

	/* Phantom pipes are effectively disabled, if the pipe was previously phantom
	 * we have to enable
	 */
	if (old_pipe->plane_state && old_is_phantom &&
			new_pipe->plane_state && !new_is_phantom)
		new_pipe->update_flags.bits.enable = 1;

	if (old_pipe->plane_state && !new_pipe->plane_state) {
		new_pipe->update_flags.bits.disable = 1;
		return;
	}

	/* Detect plane change */
	if (old_pipe->plane_state != new_pipe->plane_state) {
		new_pipe->update_flags.bits.plane_changed = true;
	}

	/* Detect top pipe only changes */
	if (resource_is_pipe_type(new_pipe, OTG_MASTER)) {
		/* Detect global sync changes */
		if (old_pipe->pipe_dlg_param.vready_offset != new_pipe->pipe_dlg_param.vready_offset
				|| old_pipe->pipe_dlg_param.vstartup_start != new_pipe->pipe_dlg_param.vstartup_start
				|| old_pipe->pipe_dlg_param.vupdate_offset != new_pipe->pipe_dlg_param.vupdate_offset
				|| old_pipe->pipe_dlg_param.vupdate_width != new_pipe->pipe_dlg_param.vupdate_width)
			new_pipe->update_flags.bits.global_sync = 1;
	}

	if (old_pipe->det_buffer_size_kb != new_pipe->det_buffer_size_kb)
		new_pipe->update_flags.bits.det_size = 1;

	/*
	 * Detect opp / tg change, only set on change, not on enable
	 * Assume mpcc inst = pipe index, if not this code needs to be updated
	 * since mpcc is what is affected by these. In fact all of our sequence
	 * makes this assumption at the moment with how hubp reset is matched to
	 * same index mpcc reset.
	 */
	if (old_pipe->stream_res.opp != new_pipe->stream_res.opp)
		new_pipe->update_flags.bits.opp_changed = 1;
	if (old_pipe->stream_res.tg != new_pipe->stream_res.tg)
		new_pipe->update_flags.bits.tg_changed = 1;

	/*
	 * Detect mpcc blending changes, only dpp inst and opp matter here,
	 * mpccs getting removed/inserted update connected ones during their own
	 * programming
	 */
	if (old_pipe->plane_res.dpp != new_pipe->plane_res.dpp
			|| old_pipe->stream_res.opp != new_pipe->stream_res.opp)
		new_pipe->update_flags.bits.mpcc = 1;

	/* Detect dppclk change */
	if (old_pipe->plane_res.bw.dppclk_khz != new_pipe->plane_res.bw.dppclk_khz)
		new_pipe->update_flags.bits.dppclk = 1;

	/* Check for scl update */
	if (memcmp(&old_pipe->plane_res.scl_data, &new_pipe->plane_res.scl_data, sizeof(struct scaler_data)))
			new_pipe->update_flags.bits.scaler = 1;
	/* Check for vp update */
	if (memcmp(&old_pipe->plane_res.scl_data.viewport, &new_pipe->plane_res.scl_data.viewport, sizeof(struct rect))
			|| memcmp(&old_pipe->plane_res.scl_data.viewport_c,
				&new_pipe->plane_res.scl_data.viewport_c, sizeof(struct rect)))
		new_pipe->update_flags.bits.viewport = 1;

	/* Detect dlg/ttu/rq updates */
	{
		struct _vcs_dpi_display_dlg_regs_st old_dlg_attr = old_pipe->dlg_regs;
		struct _vcs_dpi_display_ttu_regs_st old_ttu_attr = old_pipe->ttu_regs;
		struct _vcs_dpi_display_dlg_regs_st *new_dlg_attr = &new_pipe->dlg_regs;
		struct _vcs_dpi_display_ttu_regs_st *new_ttu_attr = &new_pipe->ttu_regs;

		/* Detect pipe interdependent updates */
		if (old_dlg_attr.dst_y_prefetch != new_dlg_attr->dst_y_prefetch ||
				old_dlg_attr.vratio_prefetch != new_dlg_attr->vratio_prefetch ||
				old_dlg_attr.vratio_prefetch_c != new_dlg_attr->vratio_prefetch_c ||
				old_dlg_attr.dst_y_per_vm_vblank != new_dlg_attr->dst_y_per_vm_vblank ||
				old_dlg_attr.dst_y_per_row_vblank != new_dlg_attr->dst_y_per_row_vblank ||
				old_dlg_attr.dst_y_per_vm_flip != new_dlg_attr->dst_y_per_vm_flip ||
				old_dlg_attr.dst_y_per_row_flip != new_dlg_attr->dst_y_per_row_flip ||
				old_dlg_attr.refcyc_per_meta_chunk_vblank_l != new_dlg_attr->refcyc_per_meta_chunk_vblank_l ||
				old_dlg_attr.refcyc_per_meta_chunk_vblank_c != new_dlg_attr->refcyc_per_meta_chunk_vblank_c ||
				old_dlg_attr.refcyc_per_meta_chunk_flip_l != new_dlg_attr->refcyc_per_meta_chunk_flip_l ||
				old_dlg_attr.refcyc_per_line_delivery_pre_l != new_dlg_attr->refcyc_per_line_delivery_pre_l ||
				old_dlg_attr.refcyc_per_line_delivery_pre_c != new_dlg_attr->refcyc_per_line_delivery_pre_c ||
				old_ttu_attr.refcyc_per_req_delivery_pre_l != new_ttu_attr->refcyc_per_req_delivery_pre_l ||
				old_ttu_attr.refcyc_per_req_delivery_pre_c != new_ttu_attr->refcyc_per_req_delivery_pre_c ||
				old_ttu_attr.refcyc_per_req_delivery_pre_cur0 != new_ttu_attr->refcyc_per_req_delivery_pre_cur0 ||
				old_ttu_attr.refcyc_per_req_delivery_pre_cur1 != new_ttu_attr->refcyc_per_req_delivery_pre_cur1 ||
				old_ttu_attr.min_ttu_vblank != new_ttu_attr->min_ttu_vblank ||
				old_ttu_attr.qos_level_flip != new_ttu_attr->qos_level_flip) {
			old_dlg_attr.dst_y_prefetch = new_dlg_attr->dst_y_prefetch;
			old_dlg_attr.vratio_prefetch = new_dlg_attr->vratio_prefetch;
			old_dlg_attr.vratio_prefetch_c = new_dlg_attr->vratio_prefetch_c;
			old_dlg_attr.dst_y_per_vm_vblank = new_dlg_attr->dst_y_per_vm_vblank;
			old_dlg_attr.dst_y_per_row_vblank = new_dlg_attr->dst_y_per_row_vblank;
			old_dlg_attr.dst_y_per_vm_flip = new_dlg_attr->dst_y_per_vm_flip;
			old_dlg_attr.dst_y_per_row_flip = new_dlg_attr->dst_y_per_row_flip;
			old_dlg_attr.refcyc_per_meta_chunk_vblank_l = new_dlg_attr->refcyc_per_meta_chunk_vblank_l;
			old_dlg_attr.refcyc_per_meta_chunk_vblank_c = new_dlg_attr->refcyc_per_meta_chunk_vblank_c;
			old_dlg_attr.refcyc_per_meta_chunk_flip_l = new_dlg_attr->refcyc_per_meta_chunk_flip_l;
			old_dlg_attr.refcyc_per_line_delivery_pre_l = new_dlg_attr->refcyc_per_line_delivery_pre_l;
			old_dlg_attr.refcyc_per_line_delivery_pre_c = new_dlg_attr->refcyc_per_line_delivery_pre_c;
			old_ttu_attr.refcyc_per_req_delivery_pre_l = new_ttu_attr->refcyc_per_req_delivery_pre_l;
			old_ttu_attr.refcyc_per_req_delivery_pre_c = new_ttu_attr->refcyc_per_req_delivery_pre_c;
			old_ttu_attr.refcyc_per_req_delivery_pre_cur0 = new_ttu_attr->refcyc_per_req_delivery_pre_cur0;
			old_ttu_attr.refcyc_per_req_delivery_pre_cur1 = new_ttu_attr->refcyc_per_req_delivery_pre_cur1;
			old_ttu_attr.min_ttu_vblank = new_ttu_attr->min_ttu_vblank;
			old_ttu_attr.qos_level_flip = new_ttu_attr->qos_level_flip;
			new_pipe->update_flags.bits.hubp_interdependent = 1;
		}
		/* Detect any other updates to ttu/rq/dlg */
		if (memcmp(&old_dlg_attr, &new_pipe->dlg_regs, sizeof(old_dlg_attr)) ||
				memcmp(&old_ttu_attr, &new_pipe->ttu_regs, sizeof(old_ttu_attr)) ||
				memcmp(&old_pipe->rq_regs, &new_pipe->rq_regs, sizeof(old_pipe->rq_regs)))
			new_pipe->update_flags.bits.hubp_rq_dlg_ttu = 1;
	}

	if (old_pipe->unbounded_req != new_pipe->unbounded_req)
		new_pipe->update_flags.bits.unbounded_req = 1;

	if (memcmp(&old_pipe->stream_res.test_pattern_params,
				&new_pipe->stream_res.test_pattern_params, sizeof(struct test_pattern_params))) {
		new_pipe->update_flags.bits.test_pattern_changed = 1;
	}
}

static void dcn20_update_dchubp_dpp(
	struct dc *dc,
	struct pipe_ctx *pipe_ctx,
	struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct dpp *dpp = pipe_ctx->plane_res.dpp;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dccg *dccg = dc->res_pool->dccg;
	bool viewport_changed = false;
	enum mall_stream_type pipe_mall_type = dc_state_get_pipe_subvp_type(context, pipe_ctx);

	if (pipe_ctx->update_flags.bits.dppclk)
		dpp->funcs->dpp_dppclk_control(dpp, false, true);

	if (pipe_ctx->update_flags.bits.enable)
		dccg->funcs->update_dpp_dto(dccg, dpp->inst, pipe_ctx->plane_res.bw.dppclk_khz);

	/* TODO: Need input parameter to tell current DCHUB pipe tie to which OTG
	 * VTG is within DCHUBBUB which is commond block share by each pipe HUBP.
	 * VTG is 1:1 mapping with OTG. Each pipe HUBP will select which VTG
	 */
	if (pipe_ctx->update_flags.bits.hubp_rq_dlg_ttu) {
		hubp->funcs->hubp_vtg_sel(hubp, pipe_ctx->stream_res.tg->inst);

		hubp->funcs->hubp_setup(
			hubp,
			&pipe_ctx->dlg_regs,
			&pipe_ctx->ttu_regs,
			&pipe_ctx->rq_regs,
			&pipe_ctx->pipe_dlg_param);
	}

	if (pipe_ctx->update_flags.bits.unbounded_req && hubp->funcs->set_unbounded_requesting)
		hubp->funcs->set_unbounded_requesting(hubp, pipe_ctx->unbounded_req);

	if (pipe_ctx->update_flags.bits.hubp_interdependent)
		hubp->funcs->hubp_setup_interdependent(
			hubp,
			&pipe_ctx->dlg_regs,
			&pipe_ctx->ttu_regs);

	if (pipe_ctx->update_flags.bits.enable ||
			pipe_ctx->update_flags.bits.plane_changed ||
			plane_state->update_flags.bits.bpp_change ||
			plane_state->update_flags.bits.input_csc_change ||
			plane_state->update_flags.bits.color_space_change ||
			plane_state->update_flags.bits.coeff_reduction_change) {
		struct dc_bias_and_scale bns_params = plane_state->bias_and_scale;

		// program the input csc
		dpp->funcs->dpp_setup(dpp,
				plane_state->format,
				EXPANSION_MODE_ZERO,
				plane_state->input_csc_color_matrix,
				plane_state->color_space,
				NULL);

		if (dpp->funcs->set_cursor_matrix) {
			dpp->funcs->set_cursor_matrix(dpp,
				plane_state->color_space,
				plane_state->cursor_csc_color_matrix);
		}
		if (dpp->funcs->dpp_program_bias_and_scale) {
			//TODO :for CNVC set scale and bias registers if necessary
			dpp->funcs->dpp_program_bias_and_scale(dpp, &bns_params);
		}
	}

	if (pipe_ctx->update_flags.bits.mpcc
			|| pipe_ctx->update_flags.bits.plane_changed
			|| plane_state->update_flags.bits.global_alpha_change
			|| plane_state->update_flags.bits.per_pixel_alpha_change) {
		// MPCC inst is equal to pipe index in practice
		hws->funcs.update_mpcc(dc, pipe_ctx);
	}

	if (pipe_ctx->update_flags.bits.scaler ||
			plane_state->update_flags.bits.scaling_change ||
			plane_state->update_flags.bits.position_change ||
			plane_state->update_flags.bits.clip_size_change ||
			plane_state->update_flags.bits.per_pixel_alpha_change ||
			pipe_ctx->stream->update_flags.bits.scaling) {
		pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->plane_state->per_pixel_alpha;
		ASSERT(pipe_ctx->plane_res.scl_data.lb_params.depth == LB_PIXEL_DEPTH_36BPP);
		/* scaler configuration */
		pipe_ctx->plane_res.dpp->funcs->dpp_set_scaler(
				pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data);
	}

	if (pipe_ctx->update_flags.bits.viewport ||
			(context == dc->current_state && plane_state->update_flags.bits.position_change) ||
			(context == dc->current_state && plane_state->update_flags.bits.scaling_change) ||
			(context == dc->current_state && plane_state->update_flags.bits.clip_size_change) ||
			(context == dc->current_state && pipe_ctx->stream->update_flags.bits.scaling)) {

		hubp->funcs->mem_program_viewport(
			hubp,
			&pipe_ctx->plane_res.scl_data.viewport,
			&pipe_ctx->plane_res.scl_data.viewport_c);
		viewport_changed = true;
	}
		if (hubp->funcs->hubp_program_mcache_id_and_split_coordinate)
			hubp->funcs->hubp_program_mcache_id_and_split_coordinate(
				hubp,
				&pipe_ctx->mcache_regs);

	/* Any updates are handled in dc interface, just need to apply existing for plane enable */
	if ((pipe_ctx->update_flags.bits.enable || pipe_ctx->update_flags.bits.opp_changed ||
			pipe_ctx->update_flags.bits.scaler || viewport_changed == true) &&
			pipe_ctx->stream->cursor_attributes.address.quad_part != 0) {
		dc->hwss.set_cursor_attribute(pipe_ctx);
		dc->hwss.set_cursor_position(pipe_ctx);

		if (dc->hwss.set_cursor_sdr_white_level)
			dc->hwss.set_cursor_sdr_white_level(pipe_ctx);
	}

	/* Any updates are handled in dc interface, just need
	 * to apply existing for plane enable / opp change */
	if (pipe_ctx->update_flags.bits.enable || pipe_ctx->update_flags.bits.opp_changed
			|| pipe_ctx->update_flags.bits.plane_changed
			|| pipe_ctx->stream->update_flags.bits.gamut_remap
			|| plane_state->update_flags.bits.gamut_remap_change
			|| pipe_ctx->stream->update_flags.bits.out_csc) {
		/* dpp/cm gamut remap*/
		dc->hwss.program_gamut_remap(pipe_ctx);

		/*call the dcn2 method which uses mpc csc*/
		dc->hwss.program_output_csc(dc,
				pipe_ctx,
				pipe_ctx->stream->output_color_space,
				pipe_ctx->stream->csc_color_matrix.matrix,
				hubp->opp_id);
	}

	if (pipe_ctx->update_flags.bits.enable ||
			pipe_ctx->update_flags.bits.plane_changed ||
			pipe_ctx->update_flags.bits.opp_changed ||
			plane_state->update_flags.bits.pixel_format_change ||
			plane_state->update_flags.bits.horizontal_mirror_change ||
			plane_state->update_flags.bits.rotation_change ||
			plane_state->update_flags.bits.swizzle_change ||
			plane_state->update_flags.bits.dcc_change ||
			plane_state->update_flags.bits.bpp_change ||
			plane_state->update_flags.bits.scaling_change ||
			plane_state->update_flags.bits.plane_size_change) {
		struct plane_size size = plane_state->plane_size;

		size.surface_size = pipe_ctx->plane_res.scl_data.viewport;
		hubp->funcs->hubp_program_surface_config(
			hubp,
			plane_state->format,
			&plane_state->tiling_info,
			&size,
			plane_state->rotation,
			&plane_state->dcc,
			plane_state->horizontal_mirror,
			0);
		hubp->power_gated = false;
	}

	if (pipe_ctx->update_flags.bits.enable ||
		pipe_ctx->update_flags.bits.plane_changed ||
		plane_state->update_flags.bits.addr_update) {
		if (resource_is_pipe_type(pipe_ctx, OTG_MASTER) &&
				pipe_mall_type == SUBVP_MAIN) {
			union block_sequence_params params;

			params.subvp_save_surf_addr.dc_dmub_srv = dc->ctx->dmub_srv;
			params.subvp_save_surf_addr.addr = &pipe_ctx->plane_state->address;
			params.subvp_save_surf_addr.subvp_index = pipe_ctx->subvp_index;
			hwss_subvp_save_surf_addr(&params);
		}
		dc->hwss.update_plane_addr(dc, pipe_ctx);
	}

	if (pipe_ctx->update_flags.bits.enable)
		hubp->funcs->set_blank(hubp, false);
	/* If the stream paired with this plane is phantom, the plane is also phantom */
	if (pipe_mall_type == SUBVP_PHANTOM && hubp->funcs->phantom_hubp_post_enable)
		hubp->funcs->phantom_hubp_post_enable(hubp);
}

static int calculate_vready_offset_for_group(struct pipe_ctx *pipe)
{
	struct pipe_ctx *other_pipe;
	int vready_offset = pipe->pipe_dlg_param.vready_offset;

	/* Always use the largest vready_offset of all connected pipes */
	for (other_pipe = pipe->bottom_pipe; other_pipe != NULL; other_pipe = other_pipe->bottom_pipe) {
		if (other_pipe->pipe_dlg_param.vready_offset > vready_offset)
			vready_offset = other_pipe->pipe_dlg_param.vready_offset;
	}
	for (other_pipe = pipe->top_pipe; other_pipe != NULL; other_pipe = other_pipe->top_pipe) {
		if (other_pipe->pipe_dlg_param.vready_offset > vready_offset)
			vready_offset = other_pipe->pipe_dlg_param.vready_offset;
	}
	for (other_pipe = pipe->next_odm_pipe; other_pipe != NULL; other_pipe = other_pipe->next_odm_pipe) {
		if (other_pipe->pipe_dlg_param.vready_offset > vready_offset)
			vready_offset = other_pipe->pipe_dlg_param.vready_offset;
	}
	for (other_pipe = pipe->prev_odm_pipe; other_pipe != NULL; other_pipe = other_pipe->prev_odm_pipe) {
		if (other_pipe->pipe_dlg_param.vready_offset > vready_offset)
			vready_offset = other_pipe->pipe_dlg_param.vready_offset;
	}

	return vready_offset;
}

static void dcn20_program_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;

	/* Only need to unblank on top pipe */
	if (resource_is_pipe_type(pipe_ctx, OTG_MASTER)) {
		if (pipe_ctx->update_flags.bits.enable ||
				pipe_ctx->update_flags.bits.odm ||
				pipe_ctx->stream->update_flags.bits.abm_level)
			hws->funcs.blank_pixel_data(dc, pipe_ctx,
					!pipe_ctx->plane_state ||
					!pipe_ctx->plane_state->visible);
	}

	/* Only update TG on top pipe */
	if (pipe_ctx->update_flags.bits.global_sync && !pipe_ctx->top_pipe
			&& !pipe_ctx->prev_odm_pipe) {
		pipe_ctx->stream_res.tg->funcs->program_global_sync(
				pipe_ctx->stream_res.tg,
				calculate_vready_offset_for_group(pipe_ctx),
				pipe_ctx->pipe_dlg_param.vstartup_start,
				pipe_ctx->pipe_dlg_param.vupdate_offset,
				pipe_ctx->pipe_dlg_param.vupdate_width,
				pipe_ctx->pipe_dlg_param.pstate_keepout);

		if (dc_state_get_pipe_subvp_type(context, pipe_ctx) != SUBVP_PHANTOM)
			pipe_ctx->stream_res.tg->funcs->wait_for_state(pipe_ctx->stream_res.tg, CRTC_STATE_VACTIVE);

		pipe_ctx->stream_res.tg->funcs->set_vtg_params(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing, true);

		if (hws->funcs.setup_vupdate_interrupt)
			hws->funcs.setup_vupdate_interrupt(dc, pipe_ctx);
	}

	if (pipe_ctx->update_flags.bits.odm)
		hws->funcs.update_odm(dc, context, pipe_ctx);

	if (pipe_ctx->update_flags.bits.enable) {
		if (hws->funcs.enable_plane)
			hws->funcs.enable_plane(dc, pipe_ctx, context);
		else
			dcn20_enable_plane(dc, pipe_ctx, context);

		if (dc->res_pool->hubbub->funcs->force_wm_propagate_to_pipes)
			dc->res_pool->hubbub->funcs->force_wm_propagate_to_pipes(dc->res_pool->hubbub);
	}

	if (pipe_ctx->update_flags.bits.det_size) {
		if (dc->res_pool->hubbub->funcs->program_det_size)
			dc->res_pool->hubbub->funcs->program_det_size(
				dc->res_pool->hubbub, pipe_ctx->plane_res.hubp->inst, pipe_ctx->det_buffer_size_kb);

		if (dc->res_pool->hubbub->funcs->program_det_segments)
			dc->res_pool->hubbub->funcs->program_det_segments(
				dc->res_pool->hubbub, pipe_ctx->plane_res.hubp->inst, pipe_ctx->hubp_regs.det_size);
	}

	if (pipe_ctx->plane_state && (pipe_ctx->update_flags.raw ||
	    pipe_ctx->plane_state->update_flags.raw ||
	    pipe_ctx->stream->update_flags.raw))
		dcn20_update_dchubp_dpp(dc, pipe_ctx, context);

	if (pipe_ctx->plane_state && (pipe_ctx->update_flags.bits.enable ||
	    pipe_ctx->plane_state->update_flags.bits.hdr_mult))
		hws->funcs.set_hdr_multiplier(pipe_ctx);

	if (hws->funcs.populate_mcm_luts) {
		if (pipe_ctx->plane_state) {
			hws->funcs.populate_mcm_luts(dc, pipe_ctx, pipe_ctx->plane_state->mcm_luts,
						     pipe_ctx->plane_state->lut_bank_a);
			pipe_ctx->plane_state->lut_bank_a = !pipe_ctx->plane_state->lut_bank_a;
		}
	}

	if (pipe_ctx->plane_state &&
	    (pipe_ctx->plane_state->update_flags.bits.in_transfer_func_change ||
	    pipe_ctx->plane_state->update_flags.bits.gamma_change ||
	    pipe_ctx->plane_state->update_flags.bits.lut_3d ||
	    pipe_ctx->update_flags.bits.enable))
		hws->funcs.set_input_transfer_func(dc, pipe_ctx, pipe_ctx->plane_state);

	/* dcn10_translate_regamma_to_hw_format takes 750us to finish
	 * only do gamma programming for powering on, internal memcmp to avoid
	 * updating on slave planes
	 */
	if (pipe_ctx->update_flags.bits.enable ||
			pipe_ctx->update_flags.bits.plane_changed ||
			pipe_ctx->stream->update_flags.bits.out_tf ||
			(pipe_ctx->plane_state &&
			 pipe_ctx->plane_state->update_flags.bits.output_tf_change))
		hws->funcs.set_output_transfer_func(dc, pipe_ctx, pipe_ctx->stream);

	/* If the pipe has been enabled or has a different opp, we
	 * should reprogram the fmt. This deals with cases where
	 * interation between mpc and odm combine on different streams
	 * causes a different pipe to be chosen to odm combine with.
	 */
	if (pipe_ctx->update_flags.bits.enable
	    || pipe_ctx->update_flags.bits.opp_changed) {

		pipe_ctx->stream_res.opp->funcs->opp_set_dyn_expansion(
			pipe_ctx->stream_res.opp,
			COLOR_SPACE_YCBCR601,
			pipe_ctx->stream->timing.display_color_depth,
			pipe_ctx->stream->signal);

		pipe_ctx->stream_res.opp->funcs->opp_program_fmt(
			pipe_ctx->stream_res.opp,
			&pipe_ctx->stream->bit_depth_params,
			&pipe_ctx->stream->clamping);
	}

	/* Set ABM pipe after other pipe configurations done */
	if ((pipe_ctx->plane_state && pipe_ctx->plane_state->visible)) {
		if (pipe_ctx->stream_res.abm) {
			dc->hwss.set_pipe(pipe_ctx);
			pipe_ctx->stream_res.abm->funcs->set_abm_level(pipe_ctx->stream_res.abm,
				pipe_ctx->stream->abm_level);
		}
	}

	if (pipe_ctx->update_flags.bits.test_pattern_changed) {
		struct output_pixel_processor *odm_opp = pipe_ctx->stream_res.opp;
		struct bit_depth_reduction_params params;

		memset(&params, 0, sizeof(params));
		odm_opp->funcs->opp_program_bit_depth_reduction(odm_opp, &params);
		dc->hwss.set_disp_pattern_generator(dc,
				pipe_ctx,
				pipe_ctx->stream_res.test_pattern_params.test_pattern,
				pipe_ctx->stream_res.test_pattern_params.color_space,
				pipe_ctx->stream_res.test_pattern_params.color_depth,
				NULL,
				pipe_ctx->stream_res.test_pattern_params.width,
				pipe_ctx->stream_res.test_pattern_params.height,
				pipe_ctx->stream_res.test_pattern_params.offset);
	}
}

void dcn20_program_front_end_for_ctx(
		struct dc *dc,
		struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;
	DC_LOGGER_INIT(dc->ctx->logger);
	unsigned int prev_hubp_count = 0;
	unsigned int hubp_count = 0;
	struct pipe_ctx *pipe;

	if (resource_is_pipe_topology_changed(dc->current_state, context))
		resource_log_pipe_topology_update(dc, context);

	if (dc->hwss.program_triplebuffer != NULL && dc->debug.enable_tri_buf) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			pipe = &context->res_ctx.pipe_ctx[i];

			if (!pipe->top_pipe && !pipe->prev_odm_pipe && pipe->plane_state) {
				ASSERT(!pipe->plane_state->triplebuffer_flips);
				/*turn off triple buffer for full update*/
				dc->hwss.program_triplebuffer(
						dc, pipe, pipe->plane_state->triplebuffer_flips);
			}
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (dc->current_state->res_ctx.pipe_ctx[i].plane_state)
			prev_hubp_count++;
		if (context->res_ctx.pipe_ctx[i].plane_state)
			hubp_count++;
	}

	if (prev_hubp_count == 0 && hubp_count > 0) {
		if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
			dc->res_pool->hubbub->funcs->force_pstate_change_control(
					dc->res_pool->hubbub, true, false);
		udelay(500);
	}

	/* Set pipe update flags and lock pipes */
	for (i = 0; i < dc->res_pool->pipe_count; i++)
		dcn20_detect_pipe_changes(dc->current_state, context, &dc->current_state->res_ctx.pipe_ctx[i],
				&context->res_ctx.pipe_ctx[i]);

	/* When disabling phantom pipes, turn on phantom OTG first (so we can get double
	 * buffer updates properly)
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_stream_state *stream = dc->current_state->res_ctx.pipe_ctx[i].stream;

		if (context->res_ctx.pipe_ctx[i].update_flags.bits.disable && stream &&
				dc_state_get_pipe_subvp_type(dc->current_state, &dc->current_state->res_ctx.pipe_ctx[i]) == SUBVP_PHANTOM) {
			struct timing_generator *tg = dc->current_state->res_ctx.pipe_ctx[i].stream_res.tg;

			if (tg->funcs->enable_crtc) {
				if (dc->hwss.blank_phantom) {
					int main_pipe_width = 0, main_pipe_height = 0;
					struct dc_stream_state *phantom_stream = dc_state_get_paired_subvp_stream(dc->current_state, dc->current_state->res_ctx.pipe_ctx[i].stream);

					if (phantom_stream) {
						main_pipe_width = phantom_stream->dst.width;
						main_pipe_height = phantom_stream->dst.height;
					}

					dc->hwss.blank_phantom(dc, tg, main_pipe_width, main_pipe_height);
				}
				tg->funcs->enable_crtc(tg);
			}
		}
	}
	/* OTG blank before disabling all front ends */
	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (context->res_ctx.pipe_ctx[i].update_flags.bits.disable
				&& !context->res_ctx.pipe_ctx[i].top_pipe
				&& !context->res_ctx.pipe_ctx[i].prev_odm_pipe
				&& context->res_ctx.pipe_ctx[i].stream)
			hws->funcs.blank_pixel_data(dc, &context->res_ctx.pipe_ctx[i], true);

	/* Disconnect mpcc */
	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (context->res_ctx.pipe_ctx[i].update_flags.bits.disable
				|| context->res_ctx.pipe_ctx[i].update_flags.bits.opp_changed) {
			struct hubbub *hubbub = dc->res_pool->hubbub;

			/* Phantom pipe DET should be 0, but if a pipe in use is being transitioned to phantom
			 * then we want to do the programming here (effectively it's being disabled). If we do
			 * the programming later the DET won't be updated until the OTG for the phantom pipe is
			 * turned on (i.e. in an MCLK switch) which can come in too late and cause issues with
			 * DET allocation.
			 */
			if ((context->res_ctx.pipe_ctx[i].update_flags.bits.disable ||
					(context->res_ctx.pipe_ctx[i].plane_state && dc_state_get_pipe_subvp_type(context, &context->res_ctx.pipe_ctx[i]) == SUBVP_PHANTOM))) {
				if (hubbub->funcs->program_det_size)
					hubbub->funcs->program_det_size(hubbub, dc->current_state->res_ctx.pipe_ctx[i].plane_res.hubp->inst, 0);
				if (dc->res_pool->hubbub->funcs->program_det_segments)
					dc->res_pool->hubbub->funcs->program_det_segments(hubbub, dc->current_state->res_ctx.pipe_ctx[i].plane_res.hubp->inst, 0);
			}
			hws->funcs.plane_atomic_disconnect(dc, dc->current_state, &dc->current_state->res_ctx.pipe_ctx[i]);
			DC_LOG_DC("Reset mpcc for pipe %d\n", dc->current_state->res_ctx.pipe_ctx[i].pipe_idx);
		}

	/* update ODM for blanked OTG master pipes */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];
		if (resource_is_pipe_type(pipe, OTG_MASTER) &&
				!resource_is_pipe_type(pipe, DPP_PIPE) &&
				pipe->update_flags.bits.odm &&
				hws->funcs.update_odm)
			hws->funcs.update_odm(dc, context, pipe);
	}

	/*
	 * Program all updated pipes, order matters for mpcc setup. Start with
	 * top pipe and program all pipes that follow in order
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state && !pipe->top_pipe) {
			while (pipe) {
				if (hws->funcs.program_pipe)
					hws->funcs.program_pipe(dc, pipe, context);
				else {
					/* Don't program phantom pipes in the regular front end programming sequence.
					 * There is an MPO transition case where a pipe being used by a video plane is
					 * transitioned directly to be a phantom pipe when closing the MPO video. However
					 * the phantom pipe will program a new HUBP_VTG_SEL (update takes place right away),
					 * but the MPO still exists until the double buffered update of the main pipe so we
					 * will get a frame of underflow if the phantom pipe is programmed here.
					 */
					if (pipe->stream && dc_state_get_pipe_subvp_type(context, pipe) != SUBVP_PHANTOM)
						dcn20_program_pipe(dc, pipe, context);
				}

				pipe = pipe->bottom_pipe;
			}
		}
		/* Program secondary blending tree and writeback pipes */
		pipe = &context->res_ctx.pipe_ctx[i];
		if (!pipe->top_pipe && !pipe->prev_odm_pipe
				&& pipe->stream && pipe->stream->num_wb_info > 0
				&& (pipe->update_flags.raw || (pipe->plane_state && pipe->plane_state->update_flags.raw)
					|| pipe->stream->update_flags.raw)
				&& hws->funcs.program_all_writeback_pipes_in_tree)
			hws->funcs.program_all_writeback_pipes_in_tree(dc, pipe->stream, context);

		/* Avoid underflow by check of pipe line read when adding 2nd plane. */
		if (hws->wa.wait_hubpret_read_start_during_mpo_transition &&
			!pipe->top_pipe &&
			pipe->stream &&
			pipe->plane_res.hubp->funcs->hubp_wait_pipe_read_start &&
			dc->current_state->stream_status[0].plane_count == 1 &&
			context->stream_status[0].plane_count > 1) {
			pipe->plane_res.hubp->funcs->hubp_wait_pipe_read_start(pipe->plane_res.hubp);
		}
	}
}

/* post_unlock_reset_opp - the function wait for corresponding double
 * buffered pending status clear and reset opp head pipe's none double buffered
 * registers to their initial state.
 */
static void post_unlock_reset_opp(struct dc *dc,
		struct pipe_ctx *opp_head)
{
	struct display_stream_compressor *dsc = opp_head->stream_res.dsc;
	struct dccg *dccg = dc->res_pool->dccg;

	/*
	 * wait for all DPP pipes in current mpc blending tree completes double
	 * buffered disconnection before resetting OPP
	 */
	dc->hwss.wait_for_mpcc_disconnect(dc, dc->res_pool, opp_head);

	if (dsc) {
		bool is_dsc_ungated = false;

		if (dc->hwseq->funcs.dsc_pg_status)
			is_dsc_ungated = dc->hwseq->funcs.dsc_pg_status(dc->hwseq, dsc->inst);

		if (is_dsc_ungated) {
			/*
			 * seamless update specific where we will postpone non
			 * double buffered DSCCLK disable logic in post unlock
			 * sequence after DSC is disconnected from OPP but not
			 * yet power gated.
			 */
			dsc->funcs->dsc_wait_disconnect_pending_clear(dsc);
			dsc->funcs->dsc_disable(dsc);
			if (dccg->funcs->set_ref_dscclk)
				dccg->funcs->set_ref_dscclk(dccg, dsc->inst);
		}
	}
}

void dcn20_post_unlock_program_front_end(
		struct dc *dc,
		struct dc_state *context)
{
	int i;
	const unsigned int TIMEOUT_FOR_PIPE_ENABLE_US = 100000;
	unsigned int polling_interval_us = 1;
	struct dce_hwseq *hwseq = dc->hwseq;

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (resource_is_pipe_type(&dc->current_state->res_ctx.pipe_ctx[i], OPP_HEAD) &&
				!resource_is_pipe_type(&context->res_ctx.pipe_ctx[i], OPP_HEAD))
			post_unlock_reset_opp(dc,
					&dc->current_state->res_ctx.pipe_ctx[i]);

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (context->res_ctx.pipe_ctx[i].update_flags.bits.disable)
			dc->hwss.disable_plane(dc, dc->current_state, &dc->current_state->res_ctx.pipe_ctx[i]);

	/*
	 * If we are enabling a pipe, we need to wait for pending clear as this is a critical
	 * part of the enable operation otherwise, DM may request an immediate flip which
	 * will cause HW to perform an "immediate enable" (as opposed to "vsync enable") which
	 * is unsupported on DCN.
	 */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		// Don't check flip pending on phantom pipes
		if (pipe->plane_state && !pipe->top_pipe && pipe->update_flags.bits.enable &&
				dc_state_get_pipe_subvp_type(context, pipe) != SUBVP_PHANTOM) {
			struct hubp *hubp = pipe->plane_res.hubp;
			int j = 0;
			for (j = 0; j < TIMEOUT_FOR_PIPE_ENABLE_US / polling_interval_us
					&& hubp->funcs->hubp_is_flip_pending(hubp); j++)
				udelay(polling_interval_us);
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];
		struct pipe_ctx *old_pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		/* When going from a smaller ODM slice count to larger, we must ensure double
		 * buffer update completes before we return to ensure we don't reduce DISPCLK
		 * before we've transitioned to 2:1 or 4:1
		 */
		if (resource_is_pipe_type(old_pipe, OTG_MASTER) && resource_is_pipe_type(pipe, OTG_MASTER) &&
				resource_get_odm_slice_count(old_pipe) < resource_get_odm_slice_count(pipe) &&
				dc_state_get_pipe_subvp_type(context, pipe) != SUBVP_PHANTOM) {
			int j = 0;
			struct timing_generator *tg = pipe->stream_res.tg;


			if (tg->funcs->get_double_buffer_pending) {
				for (j = 0; j < TIMEOUT_FOR_PIPE_ENABLE_US / polling_interval_us
				&& tg->funcs->get_double_buffer_pending(tg); j++)
					udelay(polling_interval_us);
			}
		}
	}

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		if (pipe->plane_state && !pipe->top_pipe) {
			/* Program phantom pipe here to prevent a frame of underflow in the MPO transition
			 * case (if a pipe being used for a video plane transitions to a phantom pipe, it
			 * can underflow due to HUBP_VTG_SEL programming if done in the regular front end
			 * programming sequence).
			 */
			while (pipe) {
				if (pipe->stream && dc_state_get_pipe_subvp_type(context, pipe) == SUBVP_PHANTOM) {
					/* When turning on the phantom pipe we want to run through the
					 * entire enable sequence, so apply all the "enable" flags.
					 */
					if (dc->hwss.apply_update_flags_for_phantom)
						dc->hwss.apply_update_flags_for_phantom(pipe);
					if (dc->hwss.update_phantom_vp_position)
						dc->hwss.update_phantom_vp_position(dc, context, pipe);
					dcn20_program_pipe(dc, pipe, context);
				}
				pipe = pipe->bottom_pipe;
			}
		}
	}

	if (!hwseq)
		return;

	/* P-State support transitions:
	 * Natural -> FPO: 		P-State disabled in prepare, force disallow anytime is safe
	 * FPO -> Natural: 		Unforce anytime after FW disable is safe (P-State will assert naturally)
	 * Unsupported -> FPO:	P-State enabled in optimize, force disallow anytime is safe
	 * FPO -> Unsupported:	P-State disabled in prepare, unforce disallow anytime is safe
	 * FPO <-> SubVP:		Force disallow is maintained on the FPO / SubVP pipes
	 */
	if (hwseq->funcs.update_force_pstate)
		dc->hwseq->funcs.update_force_pstate(dc, context);

	/* Only program the MALL registers after all the main and phantom pipes
	 * are done programming.
	 */
	if (hwseq->funcs.program_mall_pipe_config)
		hwseq->funcs.program_mall_pipe_config(dc, context);

	/* WA to apply WM setting*/
	if (hwseq->wa.DEGVIDCN21)
		dc->res_pool->hubbub->funcs->apply_DEDCN21_147_wa(dc->res_pool->hubbub);


	/* WA for stutter underflow during MPO transitions when adding 2nd plane */
	if (hwseq->wa.disallow_self_refresh_during_multi_plane_transition) {

		if (dc->current_state->stream_status[0].plane_count == 1 &&
				context->stream_status[0].plane_count > 1) {

			struct timing_generator *tg = dc->res_pool->timing_generators[0];

			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub, false);

			hwseq->wa_state.disallow_self_refresh_during_multi_plane_transition_applied = true;
			hwseq->wa_state.disallow_self_refresh_during_multi_plane_transition_applied_on_frame = tg->funcs->get_frame_count(tg);
		}
	}
}

void dcn20_prepare_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct hubbub *hubbub = dc->res_pool->hubbub;
	unsigned int compbuf_size_kb = 0;
	unsigned int cache_wm_a = context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns;
	unsigned int i;

	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			context,
			false);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// At optimize don't restore the original watermark value
		if (pipe->stream && dc_state_get_pipe_subvp_type(context, pipe) != SUBVP_NONE) {
			context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = 4U * 1000U * 1000U * 1000U;
			break;
		}
	}

	/* program dchubbub watermarks:
	 * For assigning wm_optimized_required, use |= operator since we don't want
	 * to clear the value if the optimize has not happened yet
	 */
	dc->wm_optimized_required |= hubbub->funcs->program_watermarks(hubbub,
					&context->bw_ctx.bw.dcn.watermarks,
					dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000,
					false);

	// Restore the real watermark so we can commit the value to DMCUB
	// DMCUB uses the "original" watermark value in SubVP MCLK switch
	context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = cache_wm_a;

	/* decrease compbuf size */
	if (hubbub->funcs->program_compbuf_size) {
		if (context->bw_ctx.dml.ip.min_comp_buffer_size_kbytes) {
			compbuf_size_kb = context->bw_ctx.dml.ip.min_comp_buffer_size_kbytes;
			dc->wm_optimized_required |= (compbuf_size_kb != dc->current_state->bw_ctx.dml.ip.min_comp_buffer_size_kbytes);
		} else {
			compbuf_size_kb = context->bw_ctx.bw.dcn.compbuf_size_kb;
			dc->wm_optimized_required |= (compbuf_size_kb != dc->current_state->bw_ctx.bw.dcn.compbuf_size_kb);
		}

		hubbub->funcs->program_compbuf_size(hubbub, compbuf_size_kb, false);
	}
}

void dcn20_optimize_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	struct hubbub *hubbub = dc->res_pool->hubbub;
	int i;

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe = &context->res_ctx.pipe_ctx[i];

		// At optimize don't need  to restore the original watermark value
		if (pipe->stream && dc_state_get_pipe_subvp_type(context, pipe) != SUBVP_NONE) {
			context->bw_ctx.bw.dcn.watermarks.a.cstate_pstate.pstate_change_ns = 4U * 1000U * 1000U * 1000U;
			break;
		}
	}

	/* program dchubbub watermarks */
	hubbub->funcs->program_watermarks(hubbub,
					&context->bw_ctx.bw.dcn.watermarks,
					dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000,
					true);

	if (dc->clk_mgr->dc_mode_softmax_enabled)
		if (dc->clk_mgr->clks.dramclk_khz > dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000 &&
				context->bw_ctx.bw.dcn.clk.dramclk_khz <= dc->clk_mgr->bw_params->dc_mode_softmax_memclk * 1000)
			dc->clk_mgr->funcs->set_max_memclk(dc->clk_mgr, dc->clk_mgr->bw_params->dc_mode_softmax_memclk);

	/* increase compbuf size */
	if (hubbub->funcs->program_compbuf_size)
		hubbub->funcs->program_compbuf_size(hubbub, context->bw_ctx.bw.dcn.compbuf_size_kb, true);

	if (context->bw_ctx.bw.dcn.clk.fw_based_mclk_switching) {
		dc_dmub_srv_p_state_delegate(dc,
			true, context);
		context->bw_ctx.bw.dcn.clk.p_state_change_support = true;
		dc->clk_mgr->clks.fw_based_mclk_switching = true;
	} else {
		dc->clk_mgr->clks.fw_based_mclk_switching = false;
	}

	dc->clk_mgr->funcs->update_clocks(
			dc->clk_mgr,
			context,
			true);
	if (context->bw_ctx.bw.dcn.clk.zstate_support == DCN_ZSTATE_SUPPORT_ALLOW &&
		!dc->debug.disable_extblankadj) {
		for (i = 0; i < dc->res_pool->pipe_count; ++i) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->stream && pipe_ctx->plane_res.hubp->funcs->program_extended_blank
				&& pipe_ctx->stream->adjust.v_total_min == pipe_ctx->stream->adjust.v_total_max
				&& pipe_ctx->stream->adjust.v_total_max > pipe_ctx->stream->timing.v_total)
					pipe_ctx->plane_res.hubp->funcs->program_extended_blank(pipe_ctx->plane_res.hubp,
						pipe_ctx->dlg_regs.min_dst_y_next_start);
		}
	}
}

bool dcn20_update_bandwidth(
		struct dc *dc,
		struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;

	/* recalculate DML parameters */
	if (!dc->res_pool->funcs->validate_bandwidth(dc, context, false))
		return false;

	/* apply updated bandwidth parameters */
	dc->hwss.prepare_bandwidth(dc, context);

	/* update hubp configs for all pipes */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->plane_state == NULL)
			continue;

		if (pipe_ctx->top_pipe == NULL) {
			bool blank = !is_pipe_tree_visible(pipe_ctx);

			pipe_ctx->stream_res.tg->funcs->program_global_sync(
					pipe_ctx->stream_res.tg,
					calculate_vready_offset_for_group(pipe_ctx),
					pipe_ctx->pipe_dlg_param.vstartup_start,
					pipe_ctx->pipe_dlg_param.vupdate_offset,
					pipe_ctx->pipe_dlg_param.vupdate_width,
					pipe_ctx->pipe_dlg_param.pstate_keepout);

			pipe_ctx->stream_res.tg->funcs->set_vtg_params(
					pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing, false);

			if (pipe_ctx->prev_odm_pipe == NULL)
				hws->funcs.blank_pixel_data(dc, pipe_ctx, blank);

			if (hws->funcs.setup_vupdate_interrupt)
				hws->funcs.setup_vupdate_interrupt(dc, pipe_ctx);
		}

		pipe_ctx->plane_res.hubp->funcs->hubp_setup(
				pipe_ctx->plane_res.hubp,
					&pipe_ctx->dlg_regs,
					&pipe_ctx->ttu_regs,
					&pipe_ctx->rq_regs,
					&pipe_ctx->pipe_dlg_param);
	}

	return true;
}

void dcn20_enable_writeback(
		struct dc *dc,
		struct dc_writeback_info *wb_info,
		struct dc_state *context)
{
	struct dwbc *dwb;
	struct mcif_wb *mcif_wb;
	struct timing_generator *optc;

	ASSERT(wb_info->dwb_pipe_inst < MAX_DWB_PIPES);
	ASSERT(wb_info->wb_enabled);
	dwb = dc->res_pool->dwbc[wb_info->dwb_pipe_inst];
	mcif_wb = dc->res_pool->mcif_wb[wb_info->dwb_pipe_inst];

	/* set the OPTC source mux */
	optc = dc->res_pool->timing_generators[dwb->otg_inst];
	optc->funcs->set_dwb_source(optc, wb_info->dwb_pipe_inst);
	/* set MCIF_WB buffer and arbitration configuration */
	mcif_wb->funcs->config_mcif_buf(mcif_wb, &wb_info->mcif_buf_params, wb_info->dwb_params.dest_height);
	mcif_wb->funcs->config_mcif_arb(mcif_wb, &context->bw_ctx.bw.dcn.bw_writeback.mcif_wb_arb[wb_info->dwb_pipe_inst]);
	/* Enable MCIF_WB */
	mcif_wb->funcs->enable_mcif(mcif_wb);
	/* Enable DWB */
	dwb->funcs->enable(dwb, &wb_info->dwb_params);
	/* TODO: add sequence to enable/disable warmup */
}

void dcn20_disable_writeback(
		struct dc *dc,
		unsigned int dwb_pipe_inst)
{
	struct dwbc *dwb;
	struct mcif_wb *mcif_wb;

	ASSERT(dwb_pipe_inst < MAX_DWB_PIPES);
	dwb = dc->res_pool->dwbc[dwb_pipe_inst];
	mcif_wb = dc->res_pool->mcif_wb[dwb_pipe_inst];

	dwb->funcs->disable(dwb);
	mcif_wb->funcs->disable_mcif(mcif_wb);
}

bool dcn20_wait_for_blank_complete(
		struct output_pixel_processor *opp)
{
	int counter;

	if (!opp)
		return false;

	for (counter = 0; counter < 1000; counter++) {
		if (!opp->funcs->dpg_is_pending(opp))
			break;

		udelay(100);
	}

	if (counter == 1000) {
		dm_error("DC: failed to blank crtc!\n");
		return false;
	}

	return opp->funcs->dpg_is_blanked(opp);
}

bool dcn20_dmdata_status_done(struct pipe_ctx *pipe_ctx)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;

	if (!hubp)
		return false;
	return hubp->funcs->dmdata_status_done(hubp);
}

void dcn20_disable_stream_gating(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;

	if (pipe_ctx->stream_res.dsc) {
		struct pipe_ctx *odm_pipe = pipe_ctx->next_odm_pipe;

		hws->funcs.dsc_pg_control(hws, pipe_ctx->stream_res.dsc->inst, true);
		while (odm_pipe) {
			hws->funcs.dsc_pg_control(hws, odm_pipe->stream_res.dsc->inst, true);
			odm_pipe = odm_pipe->next_odm_pipe;
		}
	}
}

void dcn20_enable_stream_gating(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct dce_hwseq *hws = dc->hwseq;

	if (pipe_ctx->stream_res.dsc) {
		struct pipe_ctx *odm_pipe = pipe_ctx->next_odm_pipe;

		hws->funcs.dsc_pg_control(hws, pipe_ctx->stream_res.dsc->inst, false);
		while (odm_pipe) {
			hws->funcs.dsc_pg_control(hws, odm_pipe->stream_res.dsc->inst, false);
			odm_pipe = odm_pipe->next_odm_pipe;
		}
	}
}

void dcn20_set_dmdata_attributes(struct pipe_ctx *pipe_ctx)
{
	struct dc_dmdata_attributes attr = { 0 };
	struct hubp *hubp = pipe_ctx->plane_res.hubp;

	attr.dmdata_mode = DMDATA_HW_MODE;
	attr.dmdata_size =
		dc_is_hdmi_signal(pipe_ctx->stream->signal) ? 32 : 36;
	attr.address.quad_part =
			pipe_ctx->stream->dmdata_address.quad_part;
	attr.dmdata_dl_delta = 0;
	attr.dmdata_qos_mode = 0;
	attr.dmdata_qos_level = 0;
	attr.dmdata_repeat = 1; /* always repeat */
	attr.dmdata_updated = 1;
	attr.dmdata_sw_data = NULL;

	hubp->funcs->dmdata_set_attributes(hubp, &attr);
}

void dcn20_init_vm_ctx(
		struct dce_hwseq *hws,
		struct dc *dc,
		struct dc_virtual_addr_space_config *va_config,
		int vmid)
{
	struct dcn_hubbub_virt_addr_config config;

	if (vmid == 0) {
		ASSERT(0); /* VMID cannot be 0 for vm context */
		return;
	}

	config.page_table_start_addr = va_config->page_table_start_addr;
	config.page_table_end_addr = va_config->page_table_end_addr;
	config.page_table_block_size = va_config->page_table_block_size_in_bytes;
	config.page_table_depth = va_config->page_table_depth;
	config.page_table_base_addr = va_config->page_table_base_addr;

	dc->res_pool->hubbub->funcs->init_vm_ctx(dc->res_pool->hubbub, &config, vmid);
}

int dcn20_init_sys_ctx(struct dce_hwseq *hws, struct dc *dc, struct dc_phy_addr_space_config *pa_config)
{
	struct dcn_hubbub_phys_addr_config config;

	config.system_aperture.fb_top = pa_config->system_aperture.fb_top;
	config.system_aperture.fb_offset = pa_config->system_aperture.fb_offset;
	config.system_aperture.fb_base = pa_config->system_aperture.fb_base;
	config.system_aperture.agp_top = pa_config->system_aperture.agp_top;
	config.system_aperture.agp_bot = pa_config->system_aperture.agp_bot;
	config.system_aperture.agp_base = pa_config->system_aperture.agp_base;
	config.gart_config.page_table_start_addr = pa_config->gart_config.page_table_start_addr;
	config.gart_config.page_table_end_addr = pa_config->gart_config.page_table_end_addr;
	config.gart_config.page_table_base_addr = pa_config->gart_config.page_table_base_addr;
	config.page_table_default_page_addr = pa_config->page_table_default_page_addr;

	return dc->res_pool->hubbub->funcs->init_dchub_sys_ctx(dc->res_pool->hubbub, &config);
}

static bool patch_address_for_sbs_tb_stereo(
		struct pipe_ctx *pipe_ctx, PHYSICAL_ADDRESS_LOC *addr)
{
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	bool sec_split = pipe_ctx->top_pipe &&
			pipe_ctx->top_pipe->plane_state == pipe_ctx->plane_state;
	if (sec_split && plane_state->address.type == PLN_ADDR_TYPE_GRPH_STEREO &&
			(pipe_ctx->stream->timing.timing_3d_format ==
			TIMING_3D_FORMAT_SIDE_BY_SIDE ||
			pipe_ctx->stream->timing.timing_3d_format ==
			TIMING_3D_FORMAT_TOP_AND_BOTTOM)) {
		*addr = plane_state->address.grph_stereo.left_addr;
		plane_state->address.grph_stereo.left_addr =
				plane_state->address.grph_stereo.right_addr;
		return true;
	}

	if (pipe_ctx->stream->view_format != VIEW_3D_FORMAT_NONE &&
			plane_state->address.type != PLN_ADDR_TYPE_GRPH_STEREO) {
		plane_state->address.type = PLN_ADDR_TYPE_GRPH_STEREO;
		plane_state->address.grph_stereo.right_addr =
				plane_state->address.grph_stereo.left_addr;
		plane_state->address.grph_stereo.right_meta_addr =
				plane_state->address.grph_stereo.left_meta_addr;
	}
	return false;
}

void dcn20_update_plane_addr(const struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	bool addr_patched = false;
	PHYSICAL_ADDRESS_LOC addr;
	struct dc_plane_state *plane_state = pipe_ctx->plane_state;

	if (plane_state == NULL)
		return;

	addr_patched = patch_address_for_sbs_tb_stereo(pipe_ctx, &addr);

	// Call Helper to track VMID use
	vm_helper_mark_vmid_used(dc->vm_helper, plane_state->address.vmid, pipe_ctx->plane_res.hubp->inst);

	pipe_ctx->plane_res.hubp->funcs->hubp_program_surface_flip_and_addr(
			pipe_ctx->plane_res.hubp,
			&plane_state->address,
			plane_state->flip_immediate);

	plane_state->status.requested_address = plane_state->address;

	if (plane_state->flip_immediate)
		plane_state->status.current_address = plane_state->address;

	if (addr_patched)
		pipe_ctx->plane_state->address.grph_stereo.left_addr = addr;
}

void dcn20_unblank_stream(struct pipe_ctx *pipe_ctx,
		struct dc_link_settings *link_settings)
{
	struct encoder_unblank_param params = {0};
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct dce_hwseq *hws = link->dc->hwseq;
	struct pipe_ctx *odm_pipe;
	bool is_two_pixels_per_container =
			pipe_ctx->stream_res.tg->funcs->is_two_pixels_per_container(&stream->timing);

	params.opp_cnt = 1;

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		params.opp_cnt++;
	}
	/* only 3 items below are used by unblank */
	params.timing = pipe_ctx->stream->timing;

	params.link_settings.link_rate = link_settings->link_rate;

	if (link->dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		/* TODO - DP2.0 HW: Set ODM mode in dp hpo encoder here */
		pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->dp_unblank(
				pipe_ctx->stream_res.hpo_dp_stream_enc,
				pipe_ctx->stream_res.tg->inst);
	} else if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		if (is_two_pixels_per_container || params.opp_cnt > 1)
			params.timing.pix_clk_100hz /= 2;
		pipe_ctx->stream_res.stream_enc->funcs->dp_set_odm_combine(
				pipe_ctx->stream_res.stream_enc, params.opp_cnt > 1);
		pipe_ctx->stream_res.stream_enc->funcs->dp_unblank(link, pipe_ctx->stream_res.stream_enc, &params);
	}

	if (link->local_sink && link->local_sink->sink_signal == SIGNAL_TYPE_EDP) {
		hws->funcs.edp_backlight_control(link, true);
	}
}

void dcn20_setup_vupdate_interrupt(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	int start_line = dc->hwss.get_vupdate_offset_from_vsync(pipe_ctx);

	if (start_line < 0)
		start_line = 0;

	if (tg->funcs->setup_vertical_interrupt2)
		tg->funcs->setup_vertical_interrupt2(tg, start_line);
}

void dcn20_reset_back_end_for_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	int i;
	struct dc_link *link = pipe_ctx->stream->link;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);

	DC_LOGGER_INIT(dc->ctx->logger);
	if (pipe_ctx->stream_res.stream_enc == NULL) {
		pipe_ctx->stream = NULL;
		return;
	}

	/* DPMS may already disable or */
	/* dpms_off status is incorrect due to fastboot
	 * feature. When system resume from S4 with second
	 * screen only, the dpms_off would be true but
	 * VBIOS lit up eDP, so check link status too.
	 */
	if (!pipe_ctx->stream->dpms_off || link->link_status.link_active)
		dc->link_srv->set_dpms_off(pipe_ctx);
	else if (pipe_ctx->stream_res.audio)
		dc->hwss.disable_audio_stream(pipe_ctx);

	/* free acquired resources */
	if (pipe_ctx->stream_res.audio) {
		/*disable az_endpoint*/
		pipe_ctx->stream_res.audio->funcs->az_disable(pipe_ctx->stream_res.audio);

		/*free audio*/
		if (dc->caps.dynamic_audio == true) {
			/*we have to dynamic arbitrate the audio endpoints*/
			/*we free the resource, need reset is_audio_acquired*/
			update_audio_usage(&dc->current_state->res_ctx, dc->res_pool,
					pipe_ctx->stream_res.audio, false);
			pipe_ctx->stream_res.audio = NULL;
		}
	}

	/* by upper caller loop, parent pipe: pipe0, will be reset last.
	 * back end share by all pipes and will be disable only when disable
	 * parent pipe.
	 */
	if (pipe_ctx->top_pipe == NULL) {

		dc->hwss.set_abm_immediate_disable(pipe_ctx);

		pipe_ctx->stream_res.tg->funcs->disable_crtc(pipe_ctx->stream_res.tg);

		pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, false);
		if (pipe_ctx->stream_res.tg->funcs->set_odm_bypass)
			pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
					pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);

		if (pipe_ctx->stream_res.tg->funcs->set_drr)
			pipe_ctx->stream_res.tg->funcs->set_drr(
					pipe_ctx->stream_res.tg, NULL);
		/* TODO - convert symclk_ref_cnts for otg to a bit map to solve
		 * the case where the same symclk is shared across multiple otg
		 * instances
		 */
		if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal))
			link->phy_state.symclk_ref_cnts.otg = 0;
		if (link->phy_state.symclk_state == SYMCLK_ON_TX_OFF) {
			link_hwss->disable_link_output(link,
					&pipe_ctx->link_res, pipe_ctx->stream->signal);
			link->phy_state.symclk_state = SYMCLK_OFF_TX_OFF;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++)
		if (&dc->current_state->res_ctx.pipe_ctx[i] == pipe_ctx)
			break;

	if (i == dc->res_pool->pipe_count)
		return;

/*
 * In case of a dangling plane, setting this to NULL unconditionally
 * causes failures during reset hw ctx where, if stream is NULL,
 * it is expected that the pipe_ctx pointers to pipes and plane are NULL.
 */
	pipe_ctx->stream = NULL;
	DC_LOG_DEBUG("Reset back end for pipe %d, tg:%d\n",
					pipe_ctx->pipe_idx, pipe_ctx->stream_res.tg->inst);
}

void dcn20_reset_hw_ctx_wrap(
		struct dc *dc,
		struct dc_state *context)
{
	int i;
	struct dce_hwseq *hws = dc->hwseq;

	/* Reset Back End*/
	for (i = dc->res_pool->pipe_count - 1; i >= 0 ; i--) {
		struct pipe_ctx *pipe_ctx_old =
			&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!pipe_ctx_old->stream)
			continue;

		if (pipe_ctx_old->top_pipe || pipe_ctx_old->prev_odm_pipe)
			continue;

		if (!pipe_ctx->stream ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx)) {
			struct clock_source *old_clk = pipe_ctx_old->clock_source;

			dcn20_reset_back_end_for_pipe(dc, pipe_ctx_old, dc->current_state);
			if (hws->funcs.enable_stream_gating)
				hws->funcs.enable_stream_gating(dc, pipe_ctx_old);
			if (old_clk)
				old_clk->funcs->cs_power_down(old_clk);
		}
	}
}

void dcn20_update_mpcc(struct dc *dc, struct pipe_ctx *pipe_ctx)
{
	struct hubp *hubp = pipe_ctx->plane_res.hubp;
	struct mpcc_blnd_cfg blnd_cfg = {0};
	bool per_pixel_alpha = pipe_ctx->plane_state->per_pixel_alpha;
	int mpcc_id;
	struct mpcc *new_mpcc;
	struct mpc *mpc = dc->res_pool->mpc;
	struct mpc_tree *mpc_tree_params = &(pipe_ctx->stream_res.opp->mpc_tree_params);

	blnd_cfg.overlap_only = false;
	blnd_cfg.global_gain = 0xff;

	if (per_pixel_alpha) {
		blnd_cfg.pre_multiplied_alpha = pipe_ctx->plane_state->pre_multiplied_alpha;
		if (pipe_ctx->plane_state->global_alpha) {
			blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN;
			blnd_cfg.global_gain = pipe_ctx->plane_state->global_alpha_value;
		} else {
			blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA;
		}
	} else {
		blnd_cfg.pre_multiplied_alpha = false;
		blnd_cfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
	}

	if (pipe_ctx->plane_state->global_alpha)
		blnd_cfg.global_alpha = pipe_ctx->plane_state->global_alpha_value;
	else
		blnd_cfg.global_alpha = 0xff;

	blnd_cfg.background_color_bpc = 4;
	blnd_cfg.bottom_gain_mode = 0;
	blnd_cfg.top_gain = 0x1f000;
	blnd_cfg.bottom_inside_gain = 0x1f000;
	blnd_cfg.bottom_outside_gain = 0x1f000;

	if (pipe_ctx->plane_state->format
			== SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA)
		blnd_cfg.pre_multiplied_alpha = false;

	/*
	 * TODO: remove hack
	 * Note: currently there is a bug in init_hw such that
	 * on resume from hibernate, BIOS sets up MPCC0, and
	 * we do mpcc_remove but the mpcc cannot go to idle
	 * after remove. This cause us to pick mpcc1 here,
	 * which causes a pstate hang for yet unknown reason.
	 */
	mpcc_id = hubp->inst;

	/* If there is no full update, don't need to touch MPC tree*/
	if (!pipe_ctx->plane_state->update_flags.bits.full_update &&
		!pipe_ctx->update_flags.bits.mpcc) {
		mpc->funcs->update_blending(mpc, &blnd_cfg, mpcc_id);
		dc->hwss.update_visual_confirm_color(dc, pipe_ctx, mpcc_id);
		return;
	}

	/* check if this MPCC is already being used */
	new_mpcc = mpc->funcs->get_mpcc_for_dpp(mpc_tree_params, mpcc_id);
	/* remove MPCC if being used */
	if (new_mpcc != NULL)
		mpc->funcs->remove_mpcc(mpc, mpc_tree_params, new_mpcc);
	else
		if (dc->debug.sanity_checks)
			mpc->funcs->assert_mpcc_idle_before_connect(
					dc->res_pool->mpc, mpcc_id);

	/* Call MPC to insert new plane */
	new_mpcc = mpc->funcs->insert_plane(dc->res_pool->mpc,
			mpc_tree_params,
			&blnd_cfg,
			NULL,
			NULL,
			hubp->inst,
			mpcc_id);
	dc->hwss.update_visual_confirm_color(dc, pipe_ctx, mpcc_id);

	ASSERT(new_mpcc != NULL);
	hubp->opp_id = pipe_ctx->stream_res.opp->inst;
	hubp->mpcc_id = mpcc_id;
}

void dcn20_enable_stream(struct pipe_ctx *pipe_ctx)
{
	enum dc_lane_count lane_count =
		pipe_ctx->stream->link->cur_link_settings.lane_count;

	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;
	struct dc_link *link = pipe_ctx->stream->link;

	uint32_t active_total_with_borders;
	uint32_t early_control = 0;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	const struct link_hwss *link_hwss = get_link_hwss(link, &pipe_ctx->link_res);
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct dtbclk_dto_params dto_params = {0};
	struct dccg *dccg = dc->res_pool->dccg;
	enum phyd32clk_clock_source phyd32clk;
	int dp_hpo_inst;

	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);
	struct stream_encoder *stream_enc = pipe_ctx->stream_res.stream_enc;

	if (dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		dto_params.otg_inst = tg->inst;
		dto_params.pixclk_khz = pipe_ctx->stream->timing.pix_clk_100hz / 10;
		dto_params.num_odm_segments = get_odm_segment_count(pipe_ctx);
		dto_params.timing = &pipe_ctx->stream->timing;
		dto_params.ref_dtbclk_khz = dc->clk_mgr->funcs->get_dtb_ref_clk_frequency(dc->clk_mgr);
		dccg->funcs->set_dtbclk_dto(dccg, &dto_params);
		dp_hpo_inst = pipe_ctx->stream_res.hpo_dp_stream_enc->inst;
		dccg->funcs->set_dpstreamclk(dccg, DTBCLK0, tg->inst, dp_hpo_inst);

		phyd32clk = get_phyd32clk_src(link);
		dccg->funcs->enable_symclk32_se(dccg, dp_hpo_inst, phyd32clk);
	} else {
		if (dccg->funcs->enable_symclk_se)
			dccg->funcs->enable_symclk_se(dccg, stream_enc->stream_enc_inst,
						      link_enc->transmitter - TRANSMITTER_UNIPHY_A);
	}

	if (dc->res_pool->dccg->funcs->set_pixel_rate_div)
		dc->res_pool->dccg->funcs->set_pixel_rate_div(
			dc->res_pool->dccg,
			pipe_ctx->stream_res.tg->inst,
			pipe_ctx->pixel_rate_divider.div_factor1,
			pipe_ctx->pixel_rate_divider.div_factor2);

	link_hwss->setup_stream_encoder(pipe_ctx);

	if (pipe_ctx->plane_state && pipe_ctx->plane_state->flip_immediate != 1) {
		if (dc->hwss.program_dmdata_engine)
			dc->hwss.program_dmdata_engine(pipe_ctx);
	}

	dc->hwss.update_info_frame(pipe_ctx);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_UPDATE_INFO_FRAME);

	/* enable early control to avoid corruption on DP monitor*/
	active_total_with_borders =
			timing->h_addressable
				+ timing->h_border_left
				+ timing->h_border_right;

	if (lane_count != 0)
		early_control = active_total_with_borders % lane_count;

	if (early_control == 0)
		early_control = lane_count;

	tg->funcs->set_early_control(tg, early_control);
}

void dcn20_program_dmdata_engine(struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state    *stream     = pipe_ctx->stream;
	struct hubp               *hubp       = pipe_ctx->plane_res.hubp;
	bool                       enable     = false;
	struct stream_encoder     *stream_enc = pipe_ctx->stream_res.stream_enc;
	enum dynamic_metadata_mode mode       = dc_is_dp_signal(stream->signal)
							? dmdata_dp
							: dmdata_hdmi;

	/* if using dynamic meta, don't set up generic infopackets */
	if (pipe_ctx->stream->dmdata_address.quad_part != 0) {
		pipe_ctx->stream_res.encoder_info_frame.hdrsmd.valid = false;
		enable = true;
	}

	if (!hubp)
		return;

	if (!stream_enc || !stream_enc->funcs->set_dynamic_metadata)
		return;

	stream_enc->funcs->set_dynamic_metadata(stream_enc, enable,
						hubp->inst, mode);
}

void dcn20_fpga_init_hw(struct dc *dc)
{
	int i, j;
	struct dce_hwseq *hws = dc->hwseq;
	struct resource_pool *res_pool = dc->res_pool;
	struct dc_state  *context = dc->current_state;

	if (dc->clk_mgr && dc->clk_mgr->funcs->init_clocks)
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	//Enable ability to power gate / don't force power on permanently
	hws->funcs.enable_power_gating_plane(hws, true);

	// Specific to FPGA dccg and registers
	REG_WRITE(RBBMIF_TIMEOUT_DIS, 0xFFFFFFFF);
	REG_WRITE(RBBMIF_TIMEOUT_DIS_2, 0xFFFFFFFF);

	hws->funcs.dccg_init(hws);

	REG_UPDATE(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, 2);
	REG_UPDATE(DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_ENABLE, 1);
	if (REG(REFCLK_CNTL))
		REG_WRITE(REFCLK_CNTL, 0);
	//


	/* Blank pixel data with OPP DPG */
	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];

		if (tg->funcs->is_tg_enabled(tg))
			dcn20_init_blank(dc, tg);
	}

	for (i = 0; i < res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];

		if (tg->funcs->is_tg_enabled(tg))
			tg->funcs->lock(tg);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct dpp *dpp = res_pool->dpps[i];

		dpp->funcs->dpp_reset(dpp);
	}

	/* Reset all MPCC muxes */
	res_pool->mpc->funcs->mpc_init(res_pool->mpc);

	/* initialize OPP mpc_tree parameter */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		res_pool->opps[i]->mpc_tree_params.opp_id = res_pool->opps[i]->inst;
		res_pool->opps[i]->mpc_tree_params.opp_list = NULL;
		for (j = 0; j < MAX_PIPES; j++)
			res_pool->opps[i]->mpcc_disconnect_pending[j] = false;
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];
		struct hubp *hubp = dc->res_pool->hubps[i];
		struct dpp *dpp = dc->res_pool->dpps[i];

		pipe_ctx->stream_res.tg = tg;
		pipe_ctx->pipe_idx = i;

		pipe_ctx->plane_res.hubp = hubp;
		pipe_ctx->plane_res.dpp = dpp;
		pipe_ctx->plane_res.mpcc_inst = dpp->inst;
		hubp->mpcc_id = dpp->inst;
		hubp->opp_id = OPP_ID_INVALID;
		hubp->power_gated = false;
		pipe_ctx->stream_res.opp = NULL;

		hubp->funcs->hubp_init(hubp);

		//dc->res_pool->opps[i]->mpc_tree_params.opp_id = dc->res_pool->opps[i]->inst;
		//dc->res_pool->opps[i]->mpc_tree_params.opp_list = NULL;
		dc->res_pool->opps[i]->mpcc_disconnect_pending[pipe_ctx->plane_res.mpcc_inst] = true;
		pipe_ctx->stream_res.opp = dc->res_pool->opps[i];
		/*to do*/
		hws->funcs.plane_atomic_disconnect(dc, context, pipe_ctx);
	}

	/* initialize DWB pointer to MCIF_WB */
	for (i = 0; i < res_pool->res_cap->num_dwb; i++)
		res_pool->dwbc[i]->mcif = res_pool->mcif_wb[i];

	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];

		if (tg->funcs->is_tg_enabled(tg))
			tg->funcs->unlock(tg);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		dc->hwss.disable_plane(dc, context, pipe_ctx);

		pipe_ctx->stream_res.tg = NULL;
		pipe_ctx->plane_res.hubp = NULL;
	}

	for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
		struct timing_generator *tg = dc->res_pool->timing_generators[i];

		tg->funcs->tg_init(tg);
	}

	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);
}

void dcn20_set_disp_pattern_generator(const struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		enum controller_dp_test_pattern test_pattern,
		enum controller_dp_color_space color_space,
		enum dc_color_depth color_depth,
		const struct tg_color *solid_color,
		int width, int height, int offset)
{
	pipe_ctx->stream_res.opp->funcs->opp_set_disp_pattern_generator(pipe_ctx->stream_res.opp, test_pattern,
			color_space, color_depth, solid_color, width, height, offset);
}
