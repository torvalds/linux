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


#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dccg.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "reg_helper.h"
#include "abm.h"
#include "hubp.h"
#include "dchubbub.h"
#include "timing_generator.h"
#include "opp.h"
#include "ipp.h"
#include "mpc.h"
#include "mcif_wb.h"
#include "dc_dmub_srv.h"
#include "dcn31_hwseq.h"
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "dce/dmub_outbox.h"
#include "link.h"
#include "dcn10/dcn10_hwseq.h"
#include "inc/link_enc_cfg.h"
#include "dcn30/dcn30_vpg.h"
#include "dce/dce_i2c_hw.h"

#define DC_LOGGER_INIT(logger)

#define CTX \
	hws->ctx
#define REG(reg)\
	hws->regs->reg
#define DC_LOGGER \
		dc->ctx->logger


#undef FN
#define FN(reg_name, field_name) \
	hws->shifts->field_name, hws->masks->field_name

static void enable_memory_low_power(struct dc *dc)
{
	struct dce_hwseq *hws = dc->hwseq;
	int i;

	if (dc->debug.enable_mem_low_power.bits.dmcu) {
		// Force ERAM to shutdown if DMCU is not enabled
		if (dc->debug.disable_dmcu || dc->config.disable_dmcu) {
			REG_UPDATE(DMU_MEM_PWR_CNTL, DMCU_ERAM_MEM_PWR_FORCE, 3);
		}
	}

	// Set default OPTC memory power states
	if (dc->debug.enable_mem_low_power.bits.optc) {
		// Shutdown when unassigned and light sleep in VBLANK
		REG_SET_2(ODM_MEM_PWR_CTRL3, 0, ODM_MEM_UNASSIGNED_PWR_MODE, 3, ODM_MEM_VBLANK_PWR_MODE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.vga) {
		// Power down VGA memory
		REG_UPDATE(MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.mpc &&
		dc->res_pool->mpc->funcs->set_mpc_mem_lp_mode)
		dc->res_pool->mpc->funcs->set_mpc_mem_lp_mode(dc->res_pool->mpc);


	if (dc->debug.enable_mem_low_power.bits.vpg && dc->res_pool->stream_enc[0]->vpg->funcs->vpg_powerdown) {
		// Power down VPGs
		for (i = 0; i < dc->res_pool->stream_enc_count; i++)
			if (dc->res_pool->stream_enc[i]->vpg)
				dc->res_pool->stream_enc[i]->vpg->funcs->vpg_powerdown(dc->res_pool->stream_enc[i]->vpg);
#if defined(CONFIG_DRM_AMD_DC_FP)
		for (i = 0; i < dc->res_pool->hpo_dp_stream_enc_count; i++)
			dc->res_pool->hpo_dp_stream_enc[i]->vpg->funcs->vpg_powerdown(dc->res_pool->hpo_dp_stream_enc[i]->vpg);
#endif
	}

}

void dcn31_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	uint32_t user_level = MAX_BACKLIGHT_LEVEL;
	int i;

	if (dc->clk_mgr && dc->clk_mgr->funcs->init_clocks)
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		hws->funcs.bios_golden_init(dc);
		if (hws->funcs.disable_vga)
			hws->funcs.disable_vga(dc->hwseq);
	}
	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	enable_memory_low_power(dc);

	if (dc->ctx->dc_bios->fw_info_valid) {
		res_pool->ref_clocks.xtalin_clock_inKhz =
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;

		if (res_pool->dccg && res_pool->hubbub) {

			(res_pool->dccg->funcs->get_dccg_ref_freq)(res_pool->dccg,
					dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency,
					&res_pool->ref_clocks.dccg_ref_clock_inKhz);

			(res_pool->hubbub->funcs->get_dchub_ref_freq)(res_pool->hubbub,
					res_pool->ref_clocks.dccg_ref_clock_inKhz,
					&res_pool->ref_clocks.dchub_ref_clock_inKhz);
		} else {
			// Not all ASICs have DCCG sw component
			res_pool->ref_clocks.dccg_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
			res_pool->ref_clocks.dchub_ref_clock_inKhz =
					res_pool->ref_clocks.xtalin_clock_inKhz;
		}
	} else
		ASSERT_CRITICAL(false);

	for (i = 0; i < dc->link_count; i++) {
		/* Power up AND update implementation according to the
		 * required signal (which may be different from the
		 * default signal on connector).
		 */
		struct dc_link *link = dc->links[i];

		if (link->ep_type != DISPLAY_ENDPOINT_PHY)
			continue;

		link->link_enc->funcs->hw_init(link->link_enc);

		/* Check for enabled DIG to identify enabled display */
		if (link->link_enc->funcs->is_dig_enabled &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
			link->link_status.link_active = true;
			if (link->link_enc->funcs->fec_is_active &&
					link->link_enc->funcs->fec_is_active(link->link_enc))
				link->fec_state = dc_link_fec_enabled;
		}
	}

	/* we want to turn off all dp displays before doing detection */
	dc->link_srv->blank_all_dp_displays(dc);

	if (hws->funcs.enable_power_gating_plane)
		hws->funcs.enable_power_gating_plane(dc->hwseq, true);

	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || !dc->config.seamless_boot_edp_requested) {

		// we want to turn off edp displays if odm is enabled and no seamless boot
		if (!dc->caps.seamless_odm) {
			for (i = 0; i < dc->res_pool->timing_generator_count; i++) {
				struct timing_generator *tg = dc->res_pool->timing_generators[i];
				uint32_t num_opps, opp_id_src0, opp_id_src1;

				num_opps = 1;
				if (tg) {
					if (tg->funcs->is_tg_enabled(tg) && tg->funcs->get_optc_source) {
						tg->funcs->get_optc_source(tg, &num_opps,
								&opp_id_src0, &opp_id_src1);
					}
				}

				if (num_opps > 1) {
					dc->link_srv->blank_all_edp_displays(dc);
					break;
				}
			}
		}

		hws->funcs.init_pipes(dc, dc->current_state);
		if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
					!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);
	}

	for (i = 0; i < res_pool->audio_count; i++) {
		struct audio *audio = res_pool->audios[i];

		audio->funcs->hw_init(audio);
	}

	for (i = 0; i < dc->link_count; i++) {
		struct dc_link *link = dc->links[i];

		if (link->panel_cntl) {
			backlight = link->panel_cntl->funcs->hw_init(link->panel_cntl);
			user_level = link->panel_cntl->stored_backlight_registers.USER_LEVEL;
		}
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (abms[i] != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight, user_level);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	REG_WRITE(DIO_MEM_PWR_CTRL, 0);

	// Set i2c to light sleep until engine is setup
	if (dc->debug.enable_mem_low_power.bits.i2c)
		REG_UPDATE(DIO_MEM_PWR_CTRL, I2C_LIGHT_SLEEP_FORCE, 1);

	if (hws->funcs.setup_hpo_hw_control)
		hws->funcs.setup_hpo_hw_control(hws, false);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		REG_WRITE(DCCG_GATE_DISABLE_CNTL, 0);

		REG_WRITE(DCCG_GATE_DISABLE_CNTL2, 0);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	if (!dcb->funcs->is_accelerated_mode(dcb) && dc->res_pool->hubbub->funcs->init_watermarks)
		dc->res_pool->hubbub->funcs->init_watermarks(dc->res_pool->hubbub);

	if (dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	if (dc->clk_mgr->funcs->set_hard_max_memclk && !dc->clk_mgr->dc_mode_softmax_enabled)
		dc->clk_mgr->funcs->set_hard_max_memclk(dc->clk_mgr);

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
				dc->res_pool->hubbub, false, false);
#if defined(CONFIG_DRM_AMD_DC_FP)
	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);
#endif

	// Get DMCUB capabilities
	dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
	dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
	dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch;
}

void dcn31_dsc_pg_control(
		struct dce_hwseq *hws,
		unsigned int dsc_inst,
		bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl = 0;

	if (hws->ctx->dc->debug.disable_dsc_power_gate)
		return;

	if (hws->ctx->dc->debug.root_clock_optimization.bits.dsc &&
		hws->ctx->dc->res_pool->dccg->funcs->enable_dsc &&
		power_on)
		hws->ctx->dc->res_pool->dccg->funcs->enable_dsc(
			hws->ctx->dc->res_pool->dccg, dsc_inst);

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (dsc_inst) {
	case 0: /* DSC0 */
		REG_UPDATE(DOMAIN16_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN16_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 1: /* DSC1 */
		REG_UPDATE(DOMAIN17_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN17_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	case 2: /* DSC2 */
		REG_UPDATE(DOMAIN18_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN18_PG_STATUS,
				DOMAIN_PGFSM_PWR_STATUS, pwr_status,
				1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);

	if (hws->ctx->dc->debug.root_clock_optimization.bits.dsc) {
		if (hws->ctx->dc->res_pool->dccg->funcs->disable_dsc && !power_on)
			hws->ctx->dc->res_pool->dccg->funcs->disable_dsc(
				hws->ctx->dc->res_pool->dccg, dsc_inst);
	}

}


void dcn31_enable_power_gating_plane(
	struct dce_hwseq *hws,
	bool enable)
{
	bool force_on = true; /* disable power gating */
	uint32_t org_ip_request_cntl = 0;

	if (enable && !hws->ctx->dc->debug.disable_hubp_power_gate)
		force_on = false;

	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);
	/* DCHUBP0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN0_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN2_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	/* DPP0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN1_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN3_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);

	force_on = true; /* disable power gating */
	if (enable && !hws->ctx->dc->debug.disable_dsc_power_gate)
		force_on = false;

	/* DCS0/1/2/3/4/5 */
	REG_UPDATE(DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}

void dcn31_update_info_frame(struct pipe_ctx *pipe_ctx)
{
	bool is_hdmi_tmds;
	bool is_dp;

	ASSERT(pipe_ctx->stream);

	if (pipe_ctx->stream_res.stream_enc == NULL)
		return;  /* this is not root pipe */

	is_hdmi_tmds = dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal);
	is_dp = dc_is_dp_signal(pipe_ctx->stream->signal);

	if (!is_hdmi_tmds && !is_dp)
		return;

	if (is_hdmi_tmds)
		pipe_ctx->stream_res.stream_enc->funcs->update_hdmi_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
	else if (pipe_ctx->stream->ctx->dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		pipe_ctx->stream_res.hpo_dp_stream_enc->funcs->update_dp_info_packets(
				pipe_ctx->stream_res.hpo_dp_stream_enc,
				&pipe_ctx->stream_res.encoder_info_frame);
		return;
	} else {
		if (pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets_sdp_line_num)
			pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets_sdp_line_num(
				pipe_ctx->stream_res.stream_enc,
				&pipe_ctx->stream_res.encoder_info_frame);

		pipe_ctx->stream_res.stream_enc->funcs->update_dp_info_packets(
			pipe_ctx->stream_res.stream_enc,
			&pipe_ctx->stream_res.encoder_info_frame);
	}
}
void dcn31_z10_save_init(struct dc *dc)
{
	union dmub_rb_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.dcn_restore.header.type = DMUB_CMD__IDLE_OPT;
	cmd.dcn_restore.header.sub_type = DMUB_CMD__IDLE_OPT_DCN_SAVE_INIT;

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dcn31_z10_restore(const struct dc *dc)
{
	union dmub_rb_cmd cmd;

	/*
	 * DMUB notifies whether restore is required.
	 * Optimization to avoid sending commands when not required.
	 */
	if (!dc_dmub_srv_is_restore_required(dc->ctx->dmub_srv))
		return;

	memset(&cmd, 0, sizeof(cmd));
	cmd.dcn_restore.header.type = DMUB_CMD__IDLE_OPT;
	cmd.dcn_restore.header.sub_type = DMUB_CMD__IDLE_OPT_DCN_RESTORE;

	dc_wake_and_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

void dcn31_hubp_pg_control(struct dce_hwseq *hws, unsigned int hubp_inst, bool power_on)
{
	uint32_t power_gate = power_on ? 0 : 1;
	uint32_t pwr_status = power_on ? 0 : 2;
	uint32_t org_ip_request_cntl;
	if (hws->ctx->dc->debug.disable_hubp_power_gate)
		return;

	if (REG(DOMAIN0_PG_CONFIG) == 0)
		return;
	REG_GET(DC_IP_REQUEST_CNTL, IP_REQUEST_EN, &org_ip_request_cntl);
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 1);

	switch (hubp_inst) {
	case 0:
		REG_SET(DOMAIN0_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 1:
		REG_SET(DOMAIN1_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 2:
		REG_SET(DOMAIN2_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	case 3:
		REG_SET(DOMAIN3_PG_CONFIG, 0, DOMAIN_POWER_GATE, power_gate);
		REG_WAIT(DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, pwr_status, 1, 1000);
		break;
	default:
		BREAK_TO_DEBUGGER();
		break;
	}
	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}

int dcn31_init_sys_ctx(struct dce_hwseq *hws, struct dc *dc, struct dc_phy_addr_space_config *pa_config)
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

	if (pa_config->gart_config.base_addr_is_mc_addr) {
		/* Convert from MC address to offset into FB */
		config.gart_config.page_table_base_addr = pa_config->gart_config.page_table_base_addr -
				pa_config->system_aperture.fb_base +
				pa_config->system_aperture.fb_offset;
	} else
		config.gart_config.page_table_base_addr = pa_config->gart_config.page_table_base_addr;

	return dc->res_pool->hubbub->funcs->init_dchub_sys_ctx(dc->res_pool->hubbub, &config);
}

static void dcn31_reset_back_end_for_pipe(
		struct dc *dc,
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context)
{
	struct dc_link *link;

	DC_LOGGER_INIT(dc->ctx->logger);
	if (pipe_ctx->stream_res.stream_enc == NULL) {
		pipe_ctx->stream = NULL;
		return;
	}
	ASSERT(!pipe_ctx->top_pipe);

	dc->hwss.set_abm_immediate_disable(pipe_ctx);

	pipe_ctx->stream_res.tg->funcs->set_dsc_config(
			pipe_ctx->stream_res.tg,
			OPTC_DSC_DISABLED, 0, 0);
	pipe_ctx->stream_res.tg->funcs->disable_crtc(pipe_ctx->stream_res.tg);
	pipe_ctx->stream_res.tg->funcs->enable_optc_clock(pipe_ctx->stream_res.tg, false);
	if (pipe_ctx->stream_res.tg->funcs->set_odm_bypass)
		pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);
	if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream->link->phy_state.symclk_ref_cnts.otg = 0;

	if (pipe_ctx->stream_res.tg->funcs->set_drr)
		pipe_ctx->stream_res.tg->funcs->set_drr(
				pipe_ctx->stream_res.tg, NULL);

	link = pipe_ctx->stream->link;
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

	pipe_ctx->stream = NULL;
	DC_LOG_DEBUG("Reset back end for pipe %d, tg:%d\n",
					pipe_ctx->pipe_idx, pipe_ctx->stream_res.tg->inst);
}

void dcn31_reset_hw_ctx_wrap(
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

			/* Reset pipe which is seamless boot stream. */
			if (!pipe_ctx_old->plane_state &&
				dc->res_pool->hubbub->funcs->program_det_size &&
				dc->res_pool->hubbub->funcs->wait_for_det_apply) {
				dc->res_pool->hubbub->funcs->program_det_size(
					dc->res_pool->hubbub, pipe_ctx_old->plane_res.hubp->inst, 0);
				/* Wait det size changed. */
				dc->res_pool->hubbub->funcs->wait_for_det_apply(
					dc->res_pool->hubbub, pipe_ctx_old->plane_res.hubp->inst);
			}

			dcn31_reset_back_end_for_pipe(dc, pipe_ctx_old, dc->current_state);
			if (hws->funcs.enable_stream_gating)
				hws->funcs.enable_stream_gating(dc, pipe_ctx_old);
			if (old_clk)
				old_clk->funcs->cs_power_down(old_clk);
		}
	}

	/* New dc_state in the process of being applied to hardware. */
	link_enc_cfg_set_transient_mode(dc, dc->current_state, context);
}

void dcn31_setup_hpo_hw_control(const struct dce_hwseq *hws, bool enable)
{
	if (hws->ctx->dc->debug.hpo_optimization)
		REG_UPDATE(HPO_TOP_HW_CONTROL, HPO_IO_EN, !!enable);
}
