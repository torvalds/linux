// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "dcn314_hwseq.h"
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "dce/dmub_outbox.h"
#include "link.h"
#include "dcn10/dcn10_hw_sequencer.h"
#include "inc/link_enc_cfg.h"
#include "dcn30/dcn30_vpg.h"
#include "dce/dce_i2c_hw.h"
#include "dsc.h"
#include "dcn20/dcn20_optc.h"
#include "dcn30/dcn30_cm_common.h"

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

static int calc_mpc_flow_ctrl_cnt(const struct dc_stream_state *stream,
		int opp_cnt)
{
	bool hblank_halved = optc2_is_two_pixels_per_containter(&stream->timing);
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

static void update_dsc_on_stream(struct pipe_ctx *pipe_ctx, bool enable)
{
	struct display_stream_compressor *dsc = pipe_ctx->stream_res.dsc;
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;

	ASSERT(dsc);
	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
		opp_cnt++;

	if (enable) {
		struct dsc_config dsc_cfg;
		struct dsc_optc_config dsc_optc_cfg;
		enum optc_dsc_mode optc_dsc_mode;

		/* Enable DSC hw block */
		dsc_cfg.pic_width = (stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right) / opp_cnt;
		dsc_cfg.pic_height = stream->timing.v_addressable + stream->timing.v_border_top + stream->timing.v_border_bottom;
		dsc_cfg.pixel_encoding = stream->timing.pixel_encoding;
		dsc_cfg.color_depth = stream->timing.display_color_depth;
		dsc_cfg.is_odm = pipe_ctx->next_odm_pipe ? true : false;
		dsc_cfg.dc_dsc_cfg = stream->timing.dsc_cfg;
		ASSERT(dsc_cfg.dc_dsc_cfg.num_slices_h % opp_cnt == 0);
		dsc_cfg.dc_dsc_cfg.num_slices_h /= opp_cnt;

		dsc->funcs->dsc_set_config(dsc, &dsc_cfg, &dsc_optc_cfg);
		dsc->funcs->dsc_enable(dsc, pipe_ctx->stream_res.opp->inst);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			struct display_stream_compressor *odm_dsc = odm_pipe->stream_res.dsc;

			ASSERT(odm_dsc);
			odm_dsc->funcs->dsc_set_config(odm_dsc, &dsc_cfg, &dsc_optc_cfg);
			odm_dsc->funcs->dsc_enable(odm_dsc, odm_pipe->stream_res.opp->inst);
		}
		dsc_cfg.dc_dsc_cfg.num_slices_h *= opp_cnt;
		dsc_cfg.pic_width *= opp_cnt;

		optc_dsc_mode = dsc_optc_cfg.is_pixel_format_444 ? OPTC_DSC_ENABLED_444 : OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;

		/* Enable DSC in OPTC */
		DC_LOG_DSC("Setting optc DSC config for tg instance %d:", pipe_ctx->stream_res.tg->inst);
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(pipe_ctx->stream_res.tg,
							optc_dsc_mode,
							dsc_optc_cfg.bytes_per_pixel,
							dsc_optc_cfg.slice_width);
	} else {
		/* disable DSC in OPTC */
		pipe_ctx->stream_res.tg->funcs->set_dsc_config(
				pipe_ctx->stream_res.tg,
				OPTC_DSC_DISABLED, 0, 0);

		/* disable DSC block */
		dsc->funcs->dsc_disable(pipe_ctx->stream_res.dsc);
		for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
			ASSERT(odm_pipe->stream_res.dsc);
			odm_pipe->stream_res.dsc->funcs->dsc_disable(odm_pipe->stream_res.dsc);
		}
	}
}

// Given any pipe_ctx, return the total ODM combine factor, and optionally return
// the OPPids which are used
static unsigned int get_odm_config(struct pipe_ctx *pipe_ctx, unsigned int *opp_instances)
{
	unsigned int opp_count = 1;
	struct pipe_ctx *odm_pipe;

	// First get to the top pipe
	for (odm_pipe = pipe_ctx; odm_pipe->prev_odm_pipe; odm_pipe = odm_pipe->prev_odm_pipe)
		;

	// First pipe is always used
	if (opp_instances)
		opp_instances[0] = odm_pipe->stream_res.opp->inst;

	// Find and count odm pipes, if any
	for (odm_pipe = odm_pipe->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		if (opp_instances)
			opp_instances[opp_count] = odm_pipe->stream_res.opp->inst;
		opp_count++;
	}

	return opp_count;
}

void dcn314_update_odm(struct dc *dc, struct dc_state *context, struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 0;
	int opp_inst[MAX_PIPES] = {0};
	bool rate_control_2x_pclk = (pipe_ctx->stream->timing.flags.INTERLACE || optc2_is_two_pixels_per_containter(&pipe_ctx->stream->timing));
	struct mpc_dwb_flow_control flow_control;
	struct mpc *mpc = dc->res_pool->mpc;
	int i;

	opp_cnt = get_odm_config(pipe_ctx, opp_inst);

	if (opp_cnt > 1)
		pipe_ctx->stream_res.tg->funcs->set_odm_combine(
				pipe_ctx->stream_res.tg,
				opp_inst, opp_cnt,
				&pipe_ctx->stream->timing);
	else
		pipe_ctx->stream_res.tg->funcs->set_odm_bypass(
				pipe_ctx->stream_res.tg, &pipe_ctx->stream->timing);

	rate_control_2x_pclk = rate_control_2x_pclk || opp_cnt > 1;
	flow_control.flow_ctrl_mode = 0;
	flow_control.flow_ctrl_cnt0 = 0x80;
	flow_control.flow_ctrl_cnt1 = calc_mpc_flow_ctrl_cnt(pipe_ctx->stream, opp_cnt);
	if (mpc->funcs->set_out_rate_control) {
		for (i = 0; i < opp_cnt; ++i) {
			mpc->funcs->set_out_rate_control(
					mpc, opp_inst[i],
					true,
					rate_control_2x_pclk,
					&flow_control);
		}
	}

	for (odm_pipe = pipe_ctx->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe) {
		odm_pipe->stream_res.opp->funcs->opp_pipe_clock_control(
				odm_pipe->stream_res.opp,
				true);
	}

	if (pipe_ctx->stream_res.dsc) {
		struct pipe_ctx *current_pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[pipe_ctx->pipe_idx];

		update_dsc_on_stream(pipe_ctx, pipe_ctx->stream->timing.flags.DSC);

		/* Check if no longer using pipe for ODM, then need to disconnect DSC for that pipe */
		if (!pipe_ctx->next_odm_pipe && current_pipe_ctx->next_odm_pipe &&
				current_pipe_ctx->next_odm_pipe->stream_res.dsc) {
			struct display_stream_compressor *dsc = current_pipe_ctx->next_odm_pipe->stream_res.dsc;
			/* disconnect DSC block from stream */
			dsc->funcs->dsc_disconnect(dsc);
		}
	}
}

void dcn314_dsc_pg_control(
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
	case 3: /* DSC3 */
		REG_UPDATE(DOMAIN19_PG_CONFIG,
				DOMAIN_POWER_GATE, power_gate);

		REG_WAIT(DOMAIN19_PG_STATUS,
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

void dcn314_enable_power_gating_plane(struct dce_hwseq *hws, bool enable)
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

	/* DCS0/1/2/3/4 */
	REG_UPDATE(DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);
	REG_UPDATE(DOMAIN19_PG_CONFIG, DOMAIN_POWER_FORCEON, force_on);

	if (org_ip_request_cntl == 0)
		REG_SET(DC_IP_REQUEST_CNTL, 0, IP_REQUEST_EN, 0);
}

unsigned int dcn314_calculate_dccg_k1_k2_values(struct pipe_ctx *pipe_ctx, unsigned int *k1_div, unsigned int *k2_div)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	unsigned int odm_combine_factor = 0;
	bool two_pix_per_container = false;

	two_pix_per_container = optc2_is_two_pixels_per_containter(&stream->timing);
	odm_combine_factor = get_odm_config(pipe_ctx, NULL);

	if (stream->ctx->dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		*k1_div = PIXEL_RATE_DIV_BY_1;
		*k2_div = PIXEL_RATE_DIV_BY_1;
	} else if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal) || dc_is_dvi_signal(pipe_ctx->stream->signal)) {
		*k1_div = PIXEL_RATE_DIV_BY_1;
		if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
			*k2_div = PIXEL_RATE_DIV_BY_2;
		else
			*k2_div = PIXEL_RATE_DIV_BY_4;
	} else if (dc_is_dp_signal(pipe_ctx->stream->signal) || dc_is_virtual_signal(pipe_ctx->stream->signal)) {
		if (two_pix_per_container) {
			*k1_div = PIXEL_RATE_DIV_BY_1;
			*k2_div = PIXEL_RATE_DIV_BY_2;
		} else {
			*k1_div = PIXEL_RATE_DIV_BY_1;
			*k2_div = PIXEL_RATE_DIV_BY_4;
			if (odm_combine_factor == 2)
				*k2_div = PIXEL_RATE_DIV_BY_2;
		}
	}

	if ((*k1_div == PIXEL_RATE_DIV_NA) && (*k2_div == PIXEL_RATE_DIV_NA))
		ASSERT(false);

	return odm_combine_factor;
}

void dcn314_set_pixels_per_cycle(struct pipe_ctx *pipe_ctx)
{
	uint32_t pix_per_cycle = 1;
	uint32_t odm_combine_factor = 1;

	if (!pipe_ctx || !pipe_ctx->stream || !pipe_ctx->stream_res.stream_enc)
		return;

	odm_combine_factor = get_odm_config(pipe_ctx, NULL);
	if (optc2_is_two_pixels_per_containter(&pipe_ctx->stream->timing) || odm_combine_factor > 1)
		pix_per_cycle = 2;

	if (pipe_ctx->stream_res.stream_enc->funcs->set_input_mode)
		pipe_ctx->stream_res.stream_enc->funcs->set_input_mode(pipe_ctx->stream_res.stream_enc,
				pix_per_cycle);
}

void dcn314_resync_fifo_dccg_dio(struct dce_hwseq *hws, struct dc *dc, struct dc_state *context)
{
	unsigned int i;
	struct pipe_ctx *pipe = NULL;
	bool otg_disabled[MAX_PIPES] = {false};

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (pipe->top_pipe || pipe->prev_odm_pipe)
			continue;

		if (pipe->stream && (pipe->stream->dpms_off || dc_is_virtual_signal(pipe->stream->signal))) {
			pipe->stream_res.tg->funcs->disable_crtc(pipe->stream_res.tg);
			reset_sync_context_for_pipe(dc, context, i);
			otg_disabled[i] = true;
		}
	}

	hws->ctx->dc->res_pool->dccg->funcs->trigger_dio_fifo_resync(hws->ctx->dc->res_pool->dccg);

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];

		if (otg_disabled[i])
			pipe->stream_res.tg->funcs->enable_crtc(pipe->stream_res.tg);
	}
}

void dcn314_dpp_root_clock_control(struct dce_hwseq *hws, unsigned int dpp_inst, bool clock_on)
{
	if (!hws->ctx->dc->debug.root_clock_optimization.bits.dpp)
		return;

	if (hws->ctx->dc->res_pool->dccg->funcs->dpp_root_clock_control)
		hws->ctx->dc->res_pool->dccg->funcs->dpp_root_clock_control(
			hws->ctx->dc->res_pool->dccg, dpp_inst, clock_on);
}

static void apply_symclk_on_tx_off_wa(struct dc_link *link)
{
	/* There are use cases where SYMCLK is referenced by OTG. For instance
	 * for TMDS signal, OTG relies SYMCLK even if TX video output is off.
	 * However current link interface will power off PHY when disabling link
	 * output. This will turn off SYMCLK generated by PHY. The workaround is
	 * to identify such case where SYMCLK is still in use by OTG when we
	 * power off PHY. When this is detected, we will temporarily power PHY
	 * back on and move PHY's SYMCLK state to SYMCLK_ON_TX_OFF by calling
	 * program_pix_clk interface. When OTG is disabled, we will then power
	 * off PHY by calling disable link output again.
	 *
	 * In future dcn generations, we plan to rework transmitter control
	 * interface so that we could have an option to set SYMCLK ON TX OFF
	 * state in one step without this workaround
	 */

	struct dc *dc = link->ctx->dc;
	struct pipe_ctx *pipe_ctx = NULL;
	uint8_t i;

	if (link->phy_state.symclk_ref_cnts.otg > 0) {
		for (i = 0; i < MAX_PIPES; i++) {
			pipe_ctx = &dc->current_state->res_ctx.pipe_ctx[i];
			if (pipe_ctx->stream && pipe_ctx->stream->link == link && pipe_ctx->top_pipe == NULL) {
				pipe_ctx->clock_source->funcs->program_pix_clk(
						pipe_ctx->clock_source,
						&pipe_ctx->stream_res.pix_clk_params,
						dc->link_srv->dp_get_encoding_format(
								&pipe_ctx->link_config.dp_link_settings),
						&pipe_ctx->pll_settings);
				link->phy_state.symclk_state = SYMCLK_ON_TX_OFF;
				break;
			}
		}
	}
}

void dcn314_disable_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct dc *dc = link->ctx->dc;
	const struct link_hwss *link_hwss = get_link_hwss(link, link_res);
	struct dmcu *dmcu = dc->res_pool->dmcu;

	if (signal == SIGNAL_TYPE_EDP &&
			link->dc->hwss.edp_backlight_control &&
			!link->skip_implict_edp_power_control)
		link->dc->hwss.edp_backlight_control(link, false);
	else if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->lock_phy(dmcu);

	link_hwss->disable_link_output(link, link_res, signal);
	link->phy_state.symclk_state = SYMCLK_OFF_TX_OFF;
	/*
	 * Add the logic to extract BOTH power up and power down sequences
	 * from enable/disable link output and only call edp panel control
	 * in enable_link_dp and disable_link_dp once.
	 */
	if (dmcu != NULL && dmcu->funcs->lock_phy)
		dmcu->funcs->unlock_phy(dmcu);
	dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_DISABLE_LINK_PHY);

	apply_symclk_on_tx_off_wa(link);
}
