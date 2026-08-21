// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "resource.h"
#include "dccg.h"
#include "dce/dce_hwseq.h"
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
#include "link_hwss.h"
#include "dpcd_defs.h"
#include "clk_mgr.h"
#include "dsc.h"
#include "link_service.h"

#include "dce/dmub_hw_lock_mgr.h"
#include "dcn10/dcn10_cm_common.h"
#include "dcn20/dcn20_optc.h"
#include "dcn30/dcn30_cm_common.h"
#include "dce110/dce110_hwseq.h"
#include "dcn32/dcn32_hwseq.h"
#include "dcn401/dcn401_hwseq.h"
#include "dcn50/dcn50_hwseq.h"
#include "dcn60_hwseq.h"
#include "dcn401/dcn401_resource.h"
#include "dcn60/dcn60_resource.h"
#include "dc_state_priv.h"
#include "link_enc_cfg.h"
#include "dio/dcn10/dcn10_dio.h"

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

static void dcn60_build_audio_output(
	struct dc_state *state,
	const struct pipe_ctx *pipe_ctx,
	struct audio_output *audio_output)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
	audio_output->engine_id = pipe_ctx->stream_res.stream_enc->id;

	audio_output->signal = pipe_ctx->stream->signal;

	/* audio_crtc_info  */

	audio_output->crtc_info.h_total =
		stream->timing.h_total;

	/*
	 * Audio packets are sent during actual CRTC blank physical signal, we
	 * need to specify actual active signal portion
	 */
	audio_output->crtc_info.h_active =
			stream->timing.h_addressable
			+ stream->timing.h_border_left
			+ stream->timing.h_border_right;

	audio_output->crtc_info.v_active =
			stream->timing.v_addressable
			+ stream->timing.v_border_top
			+ stream->timing.v_border_bottom;

	audio_output->crtc_info.pixel_repetition = 1;

	audio_output->crtc_info.interlaced =
			(stream->timing.flags.INTERLACE != 0);

	audio_output->crtc_info.refresh_rate =
		(uint16_t)((stream->timing.pix_clk_100hz*100)/
		(stream->timing.h_total*stream->timing.v_total));

	audio_output->crtc_info.color_depth =
		stream->timing.display_color_depth;

	audio_output->crtc_info.requested_pixel_clock_100Hz =
			pipe_ctx->stream_res.pix_clk_params.requested_pix_clk_100hz;

	audio_output->crtc_info.calculated_pixel_clock_100Hz =
			pipe_ctx->stream_res.pix_clk_params.requested_pix_clk_100hz;

	audio_output->crtc_info.pixel_encoding =
		stream->timing.pixel_encoding;

	audio_output->crtc_info.dsc_bits_per_pixel =
			stream->timing.dsc_cfg.bits_per_pixel;

	audio_output->crtc_info.dsc_num_slices =
			stream->timing.dsc_cfg.num_slices_h;

/*for HDMI, audio ACR is with deep color ratio factor*/
	if (dc_is_hdmi_tmds_signal(pipe_ctx->stream->signal) &&
		audio_output->crtc_info.requested_pixel_clock_100Hz ==
				(stream->timing.pix_clk_100hz)) {
		if (pipe_ctx->stream_res.pix_clk_params.pixel_encoding == PIXEL_ENCODING_YCBCR420) {
			audio_output->crtc_info.requested_pixel_clock_100Hz =
					audio_output->crtc_info.requested_pixel_clock_100Hz/2;
			audio_output->crtc_info.calculated_pixel_clock_100Hz =
					pipe_ctx->stream_res.pix_clk_params.requested_pix_clk_100hz/2;

		}
	}
	if (pipe_ctx->stream->signal == SIGNAL_TYPE_HDMI_FRL) {
		switch (pipe_ctx->stream->link->frl_link_settings.frl_link_rate) {
		case HDMI_FRL_LINK_RATE_3GBPS:
			audio_output->crtc_info.frl_character_clock_kHz = 166667;
			break;
		case HDMI_FRL_LINK_RATE_6GBPS:
		case HDMI_FRL_LINK_RATE_6GBPS_4LANE:
			audio_output->crtc_info.frl_character_clock_kHz = 333333;
			break;
		case HDMI_FRL_LINK_RATE_8GBPS:
			audio_output->crtc_info.frl_character_clock_kHz = 444444;
			break;
		case HDMI_FRL_LINK_RATE_10GBPS:
			audio_output->crtc_info.frl_character_clock_kHz = 555555;
			break;
		case HDMI_FRL_LINK_RATE_12GBPS:
			audio_output->crtc_info.frl_character_clock_kHz = 666667;
			break;
		case HDMI_FRL_LINK_RATE_16GBPS:
			audio_output->crtc_info.frl_character_clock_kHz = 888889;
			break;
		case HDMI_FRL_LINK_RATE_20GBPS:
		default:
			audio_output->crtc_info.frl_character_clock_kHz = 1111111;
			break;
	}
	} else
			audio_output->crtc_info.frl_character_clock_kHz = 0;

	if (state->clk_mgr &&
		(pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT ||
			pipe_ctx->stream->signal == SIGNAL_TYPE_HDMI_FRL ||
			pipe_ctx->stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST)) {
		audio_output->pll_info.audio_dto_source_clock_in_khz =
				state->clk_mgr->funcs->get_dp_ref_clk_frequency(
						state->clk_mgr);
	}

	audio_output->pll_info.dto_source =
		translate_to_dto_source(
			pipe_ctx->stream_res.tg->inst + 1);

	/* TODO hard code to enable for now. Need get from stream */
	audio_output->pll_info.ss_enabled = true;

	audio_output->pll_info.ss_percentage =
			pipe_ctx->pll_settings.ss_percentage;

	if (dc_is_dp_signal(pipe_ctx->stream->signal)) {
		populate_audio_dp_link_info(pipe_ctx, &audio_output->dp_link_info);
	}
}

enum dc_status dcn60_apply_single_controller_ctx_to_hw(
		struct pipe_ctx *pipe_ctx,
		struct dc_state *context,
		struct dc *dc)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct drr_params params = {0};
	unsigned int event_triggers = 0;
	struct pipe_ctx *odm_pipe = pipe_ctx->next_odm_pipe;
	struct dce_hwseq *hws = dc->hwseq;
	const struct link_hwss *link_hwss = get_link_hwss(
			link, &pipe_ctx->link_res);

	if (hws->funcs.disable_stream_gating) {
		hws->funcs.disable_stream_gating(dc, pipe_ctx);
	}

	if (pipe_ctx->stream_res.audio != NULL) {
		struct audio_output audio_output = {0};

		dcn60_build_audio_output(context, pipe_ctx, &audio_output);

		link_hwss->setup_audio_output(pipe_ctx, &audio_output,
				pipe_ctx->stream_res.audio->inst);

		pipe_ctx->stream_res.audio->funcs->az_configure(
				pipe_ctx->stream_res.audio,
				pipe_ctx->stream->signal,
				&audio_output.crtc_info,
				&pipe_ctx->stream->audio_info,
				&audio_output.dp_link_info);

		if (dc->config.disable_hbr_audio_dp2)
			if (pipe_ctx->stream_res.audio->funcs->az_disable_hbr_audio &&
					dc->link_srv->dp_is_128b_132b_signal(pipe_ctx))
				pipe_ctx->stream_res.audio->funcs->az_disable_hbr_audio(pipe_ctx->stream_res.audio);
	}

	/* make sure no pipes syncd to the pipe being enabled */
	if (!pipe_ctx->stream->apply_seamless_boot_optimization && dc->config.use_pipe_ctx_sync_logic)
		check_syncd_pipes_for_disabled_master_pipe(dc, context, pipe_ctx->pipe_idx);

	pipe_ctx->stream_res.opp->funcs->opp_program_fmt(
		pipe_ctx->stream_res.opp,
		&stream->bit_depth_params,
		&stream->clamping);

	pipe_ctx->stream_res.opp->funcs->opp_set_dyn_expansion(
			pipe_ctx->stream_res.opp,
			COLOR_SPACE_YCBCR601,
			stream->timing.display_color_depth,
			stream->signal);

	while (odm_pipe) {
		odm_pipe->stream_res.opp->funcs->opp_set_dyn_expansion(
				odm_pipe->stream_res.opp,
				COLOR_SPACE_YCBCR601,
				stream->timing.display_color_depth,
				stream->signal);

		odm_pipe->stream_res.opp->funcs->opp_program_fmt(
				odm_pipe->stream_res.opp,
				&stream->bit_depth_params,
				&stream->clamping);
		odm_pipe = odm_pipe->next_odm_pipe;
	}

	/* DCN3.1 FPGA Workaround
	 * Need to enable HPO DP Stream Encoder before setting OTG master enable.
	 * To do so, move calling function enable_stream_timing to only be done AFTER calling
	 * function core_link_enable_stream
	 */
	if (!(hws->wa.dp_hpo_and_otg_sequence && dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)))
		/*  */
		/* Do not touch stream timing on seamless boot optimization. */
		if (!pipe_ctx->stream->apply_seamless_boot_optimization)
			hws->funcs.enable_stream_timing(pipe_ctx, context, dc);

	if (hws->funcs.setup_vupdate_interrupt)
		hws->funcs.setup_vupdate_interrupt(dc, pipe_ctx);

	params.vertical_total_min = stream->adjust.v_total_min;
	params.vertical_total_max = stream->adjust.v_total_max;
	set_drr_and_clear_adjust_pending(pipe_ctx, stream, &params);

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

	if (!dc_is_virtual_signal(pipe_ctx->stream->signal)
		&& !dc_is_hdmi_frl_signal(pipe_ctx->stream->signal))
		pipe_ctx->stream_res.stream_enc->funcs->dig_connect_to_otg(
			pipe_ctx->stream_res.stream_enc,
			pipe_ctx->stream_res.tg->inst);

	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dc->link_srv->dp_trace_source_sequence(link, DPCD_SOURCE_SEQ_AFTER_CONNECT_DIG_FE_OTG);

	/* Temporary workaround to perform DSC programming ahead of stream enablement
	 * for smartmux/SPRS
	 * TODO: Remove SmartMux/SPRS checks once movement of DSC programming is generalized
	 */
	if (pipe_ctx->stream->timing.flags.DSC) {
		if ((pipe_ctx->stream->signal == SIGNAL_TYPE_EDP &&
			((link->dc->config.smart_mux_version && link->dc->is_switch_in_progress_dest)
			|| link->is_dds || link->skip_implict_edp_power_control)) &&
			(dc_is_dp_signal(pipe_ctx->stream->signal) ||
			dc_is_virtual_signal(pipe_ctx->stream->signal)))
			dc->link_srv->set_dsc_enable(pipe_ctx, true);
	}
	if (!stream->dpms_off)
		dc->link_srv->set_dpms_on(context, pipe_ctx);

	/* DCN3.1 FPGA Workaround
	 * Need to enable HPO DP Stream Encoder before setting OTG master enable.
	 * To do so, move calling function enable_stream_timing to only be done AFTER calling
	 * function core_link_enable_stream
	 */
	if (hws->wa.dp_hpo_and_otg_sequence && dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
		if (!pipe_ctx->stream->apply_seamless_boot_optimization)
			hws->funcs.enable_stream_timing(pipe_ctx, context, dc);
	}

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = pipe_ctx->bottom_pipe != NULL;

	/* Phantom and main stream share the same link (because the stream
	 * is constructed with the same sink). Make sure not to override
	 * and link programming on the main.
	 */
	if (dc_state_get_pipe_subvp_type(context, pipe_ctx) != SUBVP_PHANTOM) {
		pipe_ctx->stream->link->psr_settings.psr_feature_enabled = false;
		pipe_ctx->stream->link->replay_settings.replay_feature_enabled = false;
	}
	return DC_OK;
}

static void dcn60_setup_audio_dto(
		struct dc *dc,
		struct dc_state *context)
{
	unsigned int i;

	/* program audio wall clock. use HDMI as clock source if HDMI
	 * audio active. Otherwise, use DP as clock source
	 * first, loop to find any HDMI audio, if not, loop find DP audio
	 */
	/* Setup audio rate clock source */
	/* Issue:
	* Audio lag happened on DP monitor when unplug a HDMI monitor
	*
	* Cause:
	* In case of DP and HDMI connected or HDMI only, DCCG_AUDIO_DTO_SEL
	* is set to either dto0 or dto1, audio should work fine.
	* In case of DP connected only, DCCG_AUDIO_DTO_SEL should be dto1,
	* set to dto0 will cause audio lag.
	*
	* Solution:
	* Not optimized audio wall dto setup. When mode set, iterate pipe_ctx,
	* find first available pipe with audio, setup audio wall DTO per topology
	* instead of per pipe.
	*/
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		if (pipe_ctx->top_pipe)
			continue;
		if (pipe_ctx->stream->signal != SIGNAL_TYPE_HDMI_TYPE_A &&
			pipe_ctx->stream->signal != SIGNAL_TYPE_HDMI_FRL)
			continue;
		if (pipe_ctx->stream_res.audio != NULL) {
			struct audio_output audio_output;

			dcn60_build_audio_output(context, pipe_ctx, &audio_output);

			if (dc->res_pool->dccg && dc->res_pool->dccg->funcs->set_audio_dtbclk_dto) {
				struct dtbclk_dto_params dto_params = {0};
				dto_params.ref_dtbclk_khz = dc->clk_mgr->funcs->get_dtb_ref_clk_frequency(dc->clk_mgr);

				if (pipe_ctx->stream->signal == SIGNAL_TYPE_HDMI_FRL) {
					/* For DCN3.1, audio to HPO FRL encoder is using audio DTBCLK DTO */
					/* set audio DTBCLK DTO to 24MHz */
					dto_params.req_audio_dtbclk_khz = 24000;
					dc->res_pool->dccg->funcs->set_audio_dtbclk_dto(
						dc->res_pool->dccg,
						&dto_params);
				} else {
					/* Audio DTBCLK params default to disabled */
					dc->res_pool->dccg->funcs->set_audio_dtbclk_dto(
						dc->res_pool->dccg,
						&dto_params);

					pipe_ctx->stream_res.audio->funcs->wall_dto_setup(
						pipe_ctx->stream_res.audio,
						pipe_ctx->stream->signal,
						&audio_output.crtc_info,
						&audio_output.pll_info);
				}
			} else
				pipe_ctx->stream_res.audio->funcs->wall_dto_setup(
					pipe_ctx->stream_res.audio,
					pipe_ctx->stream->signal,
					&audio_output.crtc_info,
					&audio_output.pll_info);
			break;
		}
	}

	/* no HDMI audio is found, try DP audio */
	if (i == dc->res_pool->pipe_count) {
		for (i = 0; i < dc->res_pool->pipe_count; i++) {
			struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

			if (pipe_ctx->stream == NULL)
				continue;

			if (pipe_ctx->top_pipe)
				continue;

			if (!dc_is_dp_signal(pipe_ctx->stream->signal))
				continue;

			if (pipe_ctx->stream_res.audio != NULL) {
				struct audio_output audio_output = {0};

				dcn60_build_audio_output(context, pipe_ctx, &audio_output);

				/* Audio to HPO DP encoder is using audio DTBCLK DTO */
				if (dc->res_pool->dccg && dc->res_pool->dccg->funcs->set_audio_dtbclk_dto) {
					struct dtbclk_dto_params dto_params = {0};
					dto_params.ref_dtbclk_khz =
							dc->clk_mgr->funcs->get_dtb_ref_clk_frequency(dc->clk_mgr);

					if (dc->link_srv->dp_is_128b_132b_signal(pipe_ctx)) {
						/* set audio DTBCLK DTO to 24MHz */
						dto_params.req_audio_dtbclk_khz = 24000;
						dc->res_pool->dccg->funcs->set_audio_dtbclk_dto(
							dc->res_pool->dccg,
							&dto_params);
					} else {
						/* Audio DTBCLK params default to disabled */
						dc->res_pool->dccg->funcs->set_audio_dtbclk_dto(
							dc->res_pool->dccg,
							&dto_params);

						pipe_ctx->stream_res.audio->funcs->wall_dto_setup(
							pipe_ctx->stream_res.audio,
							pipe_ctx->stream->signal,
							&audio_output.crtc_info,
							&audio_output.pll_info);
					}
				} else {
					pipe_ctx->stream_res.audio->funcs->wall_dto_setup(
						pipe_ctx->stream_res.audio,
						pipe_ctx->stream->signal,
						&audio_output.crtc_info,
						&audio_output.pll_info);
				}
				break;
			}
		}
	}
}

enum dc_status dcn60_apply_ctx_to_hw(
		struct dc *dc,
		struct dc_state *context)
{
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	enum dc_status status;
	uint8_t i;
	bool was_hpo_acquired = resource_is_hpo_acquired(dc->current_state);
	bool is_hpo_acquired = resource_is_hpo_acquired(context);

	/* reset syncd pipes from disabled pipes */
	if (dc->config.use_pipe_ctx_sync_logic)
		reset_syncd_pipes_from_disabled_pipes(dc, context);

	/* Reset old context */
	/* look up the targets that have been removed since last commit */
	hws->funcs.reset_hw_ctx_wrap(dc, context);

	/* Skip applying if no targets */
	if (context->stream_count <= 0)
		return DC_OK;

	/* Apply new context */
	dcb->funcs->set_scratch_critical_state(dcb, true);

	/* below is for real asic only */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx_old =
					&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL || pipe_ctx->top_pipe)
			continue;

		if (pipe_ctx->stream == pipe_ctx_old->stream) {
			if (pipe_ctx_old->clock_source != pipe_ctx->clock_source)
				dce_crtc_switch_to_clk_src(dc->hwseq,
						pipe_ctx->clock_source, i);
			continue;
		}

		hws->funcs.enable_display_power_gating(
				dc, i, dc->ctx->dc_bios,
				PIPE_GATING_CONTROL_DISABLE);
	}

	dcn60_setup_audio_dto(dc, context);

	if (dc->hwseq->funcs.setup_hpo_hw_control && was_hpo_acquired != is_hpo_acquired) {
		dc->hwseq->funcs.setup_hpo_hw_control(dc->hwseq, is_hpo_acquired);
	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		struct pipe_ctx *pipe_ctx_old =
					&dc->current_state->res_ctx.pipe_ctx[i];
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->stream == NULL)
			continue;

		if (pipe_ctx->stream == pipe_ctx_old->stream &&
			pipe_ctx->stream->link->link_state_valid) {
			continue;
		}

		if (pipe_ctx_old->stream && !pipe_need_reprogram(pipe_ctx_old, pipe_ctx))
			continue;

		if (pipe_ctx->top_pipe || pipe_ctx->prev_odm_pipe)
			continue;

		status = dcn60_apply_single_controller_ctx_to_hw(
				pipe_ctx,
				context,
				dc);

		if (DC_OK != status)
			return status;

#ifdef CONFIG_DRM_AMD_DC_FP
		if (hws->funcs.resync_fifo_dccg_dio)
			hws->funcs.resync_fifo_dccg_dio(hws, dc, context, i);
#endif
	}

	dcb->funcs->set_scratch_critical_state(dcb, false);

	return DC_OK;
}

void dcn60_init_hw(struct dc *dc)
{
	struct abm **abms = dc->res_pool->multiple_abms;
	struct dce_hwseq *hws = dc->hwseq;
	struct dc_bios *dcb = dc->ctx->dc_bios;
	struct resource_pool *res_pool = dc->res_pool;
	unsigned int i;
	unsigned int edp_num;
	uint32_t backlight = MAX_BACKLIGHT_LEVEL;
	uint32_t user_level = MAX_BACKLIGHT_LEVEL;

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->init_clocks) {
		dc->clk_mgr->funcs->init_clocks(dc->clk_mgr);

		// mark dcmode limits present if any clock has distinct AC and DC values from SMU
		dc->caps.dcmode_power_limits_present = dc->clk_mgr->funcs->is_dc_mode_present &&
			dc->clk_mgr->funcs->is_dc_mode_present(dc->clk_mgr);
	}

	// Initialize the dccg
	if (res_pool->dccg->funcs->dccg_init)
		res_pool->dccg->funcs->dccg_init(res_pool->dccg);

	// Set default OPTC memory power states
	if (dc->debug.enable_mem_low_power.bits.optc) {
		// Shutdown when unassigned and light sleep in VBLANK
		REG_SET_2(ODM_MEM_PWR_CTRL3, 0, ODM_MEM_UNASSIGNED_PWR_MODE, 3, ODM_MEM_VBLANK_PWR_MODE, 1);
	}

	if (dc->debug.enable_mem_low_power.bits.vga) {
		// Power down VGA memory
		REG_UPDATE(MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, 1);
	}

	for (i = 0; i < (unsigned int)dc->res_pool->res_cap->num_dsc; i++) {
		struct display_stream_compressor *dsc = dc->res_pool->dscs[i];

		if (dsc->funcs->set_fgcg)
			dsc->funcs->set_fgcg(dsc, dc->ctx->dc->debug.enable_fine_grain_clock_gating.bits.dsc);
	}

	if (dc->ctx->dc_bios->fw_info_valid) {
		res_pool->ref_clocks.xtalin_clock_inKhz =
			dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;

		if (res_pool->hubbub) {
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

		link->link_enc->funcs->hw_init(link->link_enc);

		/* Check for enabled DIG to identify enabled display */
		if (link->link_enc->funcs->is_dig_enabled &&
			link->link_enc->funcs->is_dig_enabled(link->link_enc)) {
			link->link_status.link_active = true;
			link->phy_state.symclk_state = SYMCLK_ON_TX_ON;
			if (link->link_enc->funcs->fec_is_active &&
				link->link_enc->funcs->fec_is_active(link->link_enc))
				link->fec_state = dc_link_fec_enabled;
		}
	}

	/* we want to turn off all dp displays before doing detection */
	dc->link_srv->blank_all_dp_displays(dc);

	/* If taking control over from VBIOS, we may want to optimize our first
	 * mode set, so we need to skip powering down pipes until we know which
	 * pipes we want to use.
	 * Otherwise, if taking control is not possible, we need to power
	 * everything down.
	 */
	if (dcb->funcs->is_accelerated_mode(dcb) || !dc->config.seamless_boot_edp_requested) {
		/* Disable boot optimizations means power down everything including PHY, DIG,
		 * and OTG (i.e. the boot is not optimized because we do a full power down).
		 */
		if (dc->hwss.enable_accelerated_mode && dc->debug.disable_boot_optimizations)
			dc->hwss.enable_accelerated_mode(dc, dc->current_state);
		else
			hws->funcs.init_pipes(dc, dc->current_state);

		if (dc->res_pool->hubbub->funcs->allow_self_refresh_control)
			dc->res_pool->hubbub->funcs->allow_self_refresh_control(dc->res_pool->hubbub,
				!dc->res_pool->hubbub->ctx->dc->debug.disable_stutter);

		dcn401_initialize_min_clocks(dc);

		/* On HW init, allow idle optimizations after pipes have been turned off.
		 *
		 * In certain D3 cases (i.e. BOCO / BOMACO) it's possible that hardware state
		 * is reset (i.e. not in idle at the time hw init is called), but software state
		 * still has idle_optimizations = true, so we must disable idle optimizations first
		 * (i.e. set false), then re-enable (set true).
		 */
		dc_allow_idle_optimizations(dc, false);
		dc_allow_idle_optimizations(dc, true);
	}

	/* In headless boot cases, DIG may be turned
	 * on which causes HW/SW discrepancies.
	 * To avoid this, power down hardware on boot
	 * if DIG is turned on and seamless boot not enabled
	 */
	if (!dc->config.seamless_boot_edp_requested) {
		struct dc_link *edp_links[MAX_NUM_EDP];
		struct dc_link *edp_link;

		dc_get_edp_links(dc, edp_links, &edp_num);
		if (edp_num) {
			for (i = 0; i < edp_num; i++) {
				edp_link = edp_links[i];
				if (edp_link->link_enc->funcs->is_dig_enabled &&
					edp_link->link_enc->funcs->is_dig_enabled(edp_link->link_enc) &&
					dc->hwss.edp_backlight_control &&
					hws->funcs.power_down &&
					dc->hwss.edp_power_control) {
					dc->hwss.edp_backlight_control(edp_link, false);
					hws->funcs.power_down(dc);
					dc->hwss.edp_power_control(edp_link, false);
				}
			}
		} else {
			for (i = 0; i < dc->link_count; i++) {
				struct dc_link *link = dc->links[i];

				if (link->link_enc->funcs->is_dig_enabled &&
					link->link_enc->funcs->is_dig_enabled(link->link_enc) &&
					hws->funcs.power_down) {
					hws->funcs.power_down(dc);
					break;
				}

			}
		}
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

		if (link->force_to_use_aux) {
			//Setup corresponding HDCP XFER DEST interrupt to go to DMUCB
			dc_dmub_srv_ihc_set_dig_hdcp_interrupt_dest(
				dc->ctx->dmub_srv,
				link->eng_id,
				true);
		}

	}

	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		if (abms[i] != NULL && abms[i]->funcs != NULL)
			abms[i]->funcs->abm_init(abms[i], backlight, user_level);
	}

	/* power AFMT HDMI memory TODO: may move to dis/en output save power*/
	if (dc->res_pool->dio && dc->res_pool->dio->funcs->mem_pwr_ctrl)
		dc->res_pool->dio->funcs->mem_pwr_ctrl(dc->res_pool->dio, false);

	if (!dc->debug.disable_clock_gate) {
		/* enable all DCN clock gating */
		if (dc->res_pool->dccg && dc->res_pool->dccg->funcs && dc->res_pool->dccg->funcs->allow_clock_gating)
			dc->res_pool->dccg->funcs->allow_clock_gating(dc->res_pool->dccg, true);

		REG_UPDATE(DCFCLK_CNTL, DCFCLK_GATE_DIS, 0);
	}

	dcn401_setup_hpo_hw_control(hws, true);

	if (!dcb->funcs->is_accelerated_mode(dcb) && dc->res_pool->hubbub->funcs->init_watermarks)
		dc->res_pool->hubbub->funcs->init_watermarks(dc->res_pool->hubbub);

	if (dc->clk_mgr && dc->clk_mgr->funcs && dc->clk_mgr->funcs->notify_wm_ranges)
		dc->clk_mgr->funcs->notify_wm_ranges(dc->clk_mgr);

	if (dc->res_pool->hubbub->funcs->force_pstate_change_control)
		dc->res_pool->hubbub->funcs->force_pstate_change_control(
			dc->res_pool->hubbub, false, false);

	if (dc->res_pool->hubbub->funcs->init_crb)
		dc->res_pool->hubbub->funcs->init_crb(dc->res_pool->hubbub);

	if (dc->res_pool->hubbub->funcs->set_request_limit && dc->config.sdpif_request_limit_words_per_umc > 0)
		dc->res_pool->hubbub->funcs->set_request_limit(dc->res_pool->hubbub, dc->ctx->dc_bios->vram_info.num_chans, dc->config.sdpif_request_limit_words_per_umc);

	// Get DMCUB capabilities
	if (dc->ctx->dmub_srv) {
		dc_dmub_srv_query_caps_cmd(dc->ctx->dmub_srv);
		dc->caps.dmub_caps.psr = dc->ctx->dmub_srv->dmub->feature_caps.psr;
		dc->caps.dmub_caps.mclk_sw = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver > 0;
		dc->caps.dmub_caps.fams_ver = dc->ctx->dmub_srv->dmub->feature_caps.fw_assisted_mclk_switch_ver;
		dc->debug.fams2_config.bits.enable &=
			dc->caps.dmub_caps.fams_ver == dc->debug.fams_version.ver; // sw & fw fams versions must match for support
		if (dc->res_pool->funcs->update_bw_bounding_box) {
			/* For DCN6 re-update unconditionally to propagate Alt-Ch address info into DML */
			if (dc->clk_mgr)
				dc->res_pool->funcs->update_bw_bounding_box(dc, dc->clk_mgr->bw_params);
		}
	}
}

void dcn60_set_cursor_attribute(struct pipe_ctx *pipe_ctx)
{
	struct dc_cursor_attributes *attributes = &pipe_ctx->stream->cursor_attributes;

	attributes->force_cursor_to_disp_pref = pipe_ctx->hubp_regs.dlg_regs.force_cursor_to_disp_pref;
	pipe_ctx->plane_res.hubp->funcs->set_cursor_attributes(
			pipe_ctx->plane_res.hubp, attributes);
	pipe_ctx->plane_res.dpp->funcs->set_cursor_attributes(
		pipe_ctx->plane_res.dpp, attributes);
}

void dcn60_update_cursor_offload_pipe(struct dc *dc, const struct pipe_ctx *pipe)
{
	volatile struct dmub_cursor_offload_v1 *cs = dc->ctx->dmub_srv->dmub->cursor_offload_v1;
	const struct pipe_ctx *top_pipe = resource_get_otg_master(pipe);
	const struct hubp *hubp = pipe->plane_res.hubp;
	const struct dpp *dpp = pipe->plane_res.dpp;
	volatile struct dmub_cursor_offload_pipe_data_dcn60_v1 *p;
	uint32_t stream_idx, write_idx, payload_idx;

	if (!top_pipe || !hubp || !dpp)
		return;

	stream_idx = top_pipe->pipe_idx;
	write_idx = cs->offload_streams[stream_idx].write_idx + 1; /*  new payload (+1) */
	payload_idx = write_idx % ARRAY_SIZE(cs->offload_streams[stream_idx].payloads);

	p = &cs->offload_streams[stream_idx].payloads[payload_idx].pipe_data[pipe->pipe_idx].dcn60;

	p->CURSOR0_0_CURSOR_SURFACE_ADDRESS = hubp->att.SURFACE_ADDR;
	p->CURSOR0_0_CURSOR_SURFACE_ADDRESS_HIGH = hubp->att.SURFACE_ADDR_HIGH;
	p->CURSOR0_0_CURSOR_SIZE__CURSOR_WIDTH = hubp->att.size.bits.width;
	p->CURSOR0_0_CURSOR_SIZE__CURSOR_HEIGHT = hubp->att.size.bits.height;
	p->CURSOR0_0_CURSOR_POSITION__CURSOR_X_POSITION = hubp->pos.position.bits.x_pos;
	p->CURSOR0_0_CURSOR_POSITION__CURSOR_Y_POSITION = hubp->pos.position.bits.y_pos;
	p->CURSOR0_0_CURSOR_HOT_SPOT__CURSOR_HOT_SPOT_X = hubp->pos.hot_spot.bits.x_hot;
	p->CURSOR0_0_CURSOR_HOT_SPOT__CURSOR_HOT_SPOT_Y = hubp->pos.hot_spot.bits.y_hot;
	p->CURSOR0_0_CURSOR_DST_OFFSET__CURSOR_DST_X_OFFSET = hubp->pos.dst_offset.bits.dst_x_offset;
	p->CURSOR0_0_CURSOR_CONTROL__CURSOR_ENABLE = hubp->pos.cur_ctl.bits.cur_enable;
	p->CURSOR0_0_CURSOR_CONTROL__CURSOR_MODE = hubp->att.cur_ctl.bits.mode;
	p->CURSOR0_0_CURSOR_CONTROL__CURSOR_2X_MAGNIFY = hubp->pos.cur_ctl.bits.cur_2x_magnify;
	p->CURSOR0_0_CURSOR_CONTROL__CURSOR_PITCH = hubp->att.cur_ctl.bits.pitch;
	p->CURSOR0_0_CURSOR_CONTROL__CURSOR_LINES_PER_CHUNK = hubp->att.cur_ctl.bits.line_per_chunk;

	p->CM_CUR0_CURSOR0_CONTROL__CUR0_ENABLE = dpp->att.cur0_ctl.bits.cur0_enable;
	p->CM_CUR0_CURSOR0_CONTROL__CUR0_MODE = dpp->att.cur0_ctl.bits.mode;
	p->CM_CUR0_CURSOR0_CONTROL__CUR0_EXPANSION_MODE = dpp->att.cur0_ctl.bits.expansion_mode;
	p->CM_CUR0_CURSOR0_CONTROL__CUR0_ROM_EN = dpp->att.cur0_ctl.bits.cur0_rom_en;
	p->CM_CUR0_CURSOR0_COLOR0__CUR0_COLOR0 = 0x000000;
	p->CM_CUR0_CURSOR0_COLOR1__CUR0_COLOR1 = 0xFFFFFF;

	p->CM_CUR0_CURSOR0_FP_SCALE_BIAS_G_Y__CUR0_FP_BIAS_G_Y =
		dpp->att.fp_scale_bias_g_y.bits.fp_bias_g_y;
	p->CM_CUR0_CURSOR0_FP_SCALE_BIAS_G_Y__CUR0_FP_SCALE_G_Y =
		dpp->att.fp_scale_bias_g_y.bits.fp_scale_g_y;
	p->CM_CUR0_CURSOR0_FP_SCALE_BIAS_RB_CRCB__CUR0_FP_BIAS_RB_CRCB =
		dpp->att.fp_scale_bias_rb_crcb.bits.fp_bias_rb_crcb;
	p->CM_CUR0_CURSOR0_FP_SCALE_BIAS_RB_CRCB__CUR0_FP_SCALE_RB_CRCB =
		dpp->att.fp_scale_bias_rb_crcb.bits.fp_scale_rb_crcb;

	p->HUBPREQ0_CURSOR_SETTINGS__CURSOR0_DST_Y_OFFSET = hubp->att.settings.bits.dst_y_offset;
	p->HUBPREQ0_CURSOR_SETTINGS__CURSOR0_CHUNK_HDL_ADJUST = hubp->att.settings.bits.chunk_hdl_adjust;
	p->HUBPREQ0_CURSOR_SETTINGS__FORCE_CURSOR_TO_DISP_PREF = hubp->att.settings.bits.force_cursor_to_disp_pref;

	cs->offload_streams[stream_idx].payloads[payload_idx].pipe_mask |= (1u << pipe->pipe_idx);
}

/**
 * dcn60_get_ref_tg_for_hubbub_probe - Resolve the OTG master for hubbub probing.
 * @context: committed dc state to resolve streams from
 *
 * All probe types gate their measurement window to frame edges of the OTG
 * master of stream 0. Returns NULL when no active stream is present.
 */
static struct timing_generator *dcn60_get_ref_tg_for_hubbub_probe(
		struct dc_state *context)
{
	struct pipe_ctx *otg_pipe;

	if (!context || !context->stream_count)
		return NULL;

	otg_pipe = resource_get_otg_master_for_stream(&context->res_ctx,
			context->streams[0]);
	if (!otg_pipe)
		return NULL;

	return otg_pipe->stream_res.tg;
}

/**
 * dcn60_build_hubbub_perfmon_sequence - Build the hubbub perfmon BLS sequence.
 * @dc:             DC structure
 * @context:        Committed dc state to resolve streams from
 * @probe:          Probe state to build sequence for
 * @status:         Perfmon status to update with probe results
 * @block_sequence: Block sequence to append steps to
 * @num_steps:      Number of steps in the block sequence
 *
 * Appends BLS steps for the given probe into @block_sequence. No steps are
 * added when the probe type is unsupported or prerequisites are not met.
 */
static void dcn60_build_hubbub_perfmon_sequence(
		struct dc *dc,
		struct dc_state *context,
		const struct dc_probe_state *probe,
		struct dc_probe_status *status,
		struct block_sequence *block_sequence,
		unsigned int *num_steps)
{
	struct hubbub *hubbub = dc->res_pool->hubbub;
	uint32_t refclk_mhz = dc->res_pool->ref_clocks.dchub_ref_clock_inKhz / 1000;
	struct timing_generator *ref_tg = dcn60_get_ref_tg_for_hubbub_probe(context);
	struct block_sequence_state seq_state = { .steps = block_sequence, .num_steps = num_steps };
	uint32_t duration_ns = 0;

	if (!hubbub || !hubbub->funcs || !hubbub->funcs->perfmon.reset)
		return;

	status->type = probe->type;

	if (probe->target_state == DC_PROBE_NOT_MEASURING) {
		hwss_add_hubbub_perfmon_reset(&seq_state, hubbub);
		return;
	}

	if (probe->target_state != DC_PROBE_MEASURED || !ref_tg)
		return;

	/* Peak BW needs a single timing group. The out-of-order counter spans one
	 * prefetch window, which is meaningless when streams in separate timing
	 * groups have non-overlapping prefetch windows. */
	if (probe->type == DC_PROBE_PEAK_MEM_BW) {
		int group_size = context->stream_status[0].timing_sync_info.group_size;

		if (group_size != context->stream_count)
			return;
	}

	switch (probe->type) {
	case DC_PROBE_PEAK_MEM_BW:
		/* Start at the vblank edge and stop at the next vactive so the counter
		 * spans exactly one prefetch window, capturing prefetch traffic only. */
		if (!hubbub->funcs->perfmon.arm_measuring_out_of_order_bandwidth ||
				!hubbub->funcs->perfmon.start_measuring_out_of_order_bandwidth ||
				!hubbub->funcs->perfmon.get_out_of_order_bandwidth_mbps)
			return;

		hwss_add_hubbub_perfmon_reset(&seq_state, hubbub);
		hwss_add_hubbub_perfmon_arm_out_of_order_bw(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_start_out_of_order_bw(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_hubbub_perfmon_get_out_of_order_bw(&seq_state, hubbub,
				refclk_mhz, &status->u.bandwidth_mbps, &duration_ns);
		break;

	case DC_PROBE_AVG_MEM_BW:
		/* In-order counter accumulates over a full frame, so no timing group
		 * restriction applies (unlike the prefetch-windowed peak BW above). */
		if (!hubbub->funcs->perfmon.start_measuring_in_order_bandwidth ||
				!hubbub->funcs->perfmon.get_in_order_bandwidth_mbps)
			return;

		hwss_add_hubbub_perfmon_reset(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_start_in_order_bw(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_get_in_order_bw(&seq_state, hubbub,
				refclk_mhz, 0, &status->u.bandwidth_mbps, &duration_ns);
		break;

	case DC_PROBE_MEM_LATENCY:
		if (!hubbub->funcs->perfmon.start_measuring_memory_latencies ||
				!hubbub->funcs->perfmon.get_memory_latencies_ns)
			return;

		hwss_add_hubbub_perfmon_reset(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_start_memory_latencies(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_get_memory_latencies(&seq_state, hubbub,
				refclk_mhz, &status->u.latency);
		break;

	case DC_PROBE_URGENT_ASSERTION_COUNT:
		if (!hubbub->funcs->perfmon.start_measuring_urgent_assertion_count ||
				!hubbub->funcs->perfmon.get_urgent_assertion_count)
			return;

		hwss_add_hubbub_perfmon_reset(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_start_urgent_assertion_count(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_get_urgent_assertion_count(&seq_state, hubbub,
				refclk_mhz, &status->u.urgent_assertion_count);
		break;

	case DC_PROBE_PREFETCH_DATA_SIZE:
		if (!hubbub->funcs->perfmon.start_measuring_prefetch_data_size ||
				!hubbub->funcs->perfmon.get_prefetch_data_size)
			return;

		hwss_add_hubbub_perfmon_reset(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_start_prefetch_data_size(&seq_state, hubbub);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VACTIVE);
		hwss_add_tg_wait_for_state(&seq_state, ref_tg, CRTC_STATE_VBLANK);
		hwss_add_hubbub_perfmon_get_prefetch_data_size(&seq_state, hubbub,
				&status->u.prefetch_data_size);
		break;

	case DC_PROBE_URGENT_RAMP_LATENCY:
		/* Requires caller-supplied window params not available in probe model. */
		return;

	default:
		return;
	}
}

/**
 * dcn60_update_probe_status - Set the valid flag on a latched probe result.
 * @status: result sink whose u was written by the GET BLS step during execute
 */
static void dcn60_update_probe_status(struct dc_probe_status *status)
{
	switch (status->type) {
	case DC_PROBE_PEAK_MEM_BW:
	case DC_PROBE_AVG_MEM_BW:
		/* Zero bandwidth means the counter did not fire — treat as invalid. */
		status->valid = (status->u.bandwidth_mbps != 0);
		break;
	case DC_PROBE_MEM_LATENCY:
	case DC_PROBE_URGENT_ASSERTION_COUNT:
	case DC_PROBE_PREFETCH_DATA_SIZE:
		status->valid = true;
		break;
	default:
		status->valid = false;
		break;
	}
}

/**
 * is_probe_measurement_type_for_hubbub - Returns true if the probe type is
 * served by the hubbub perfmon block on DCN60.
 */
static bool is_probe_measurement_type_for_hubbub(enum dc_probe_type type)
{
	switch (type) {
	case DC_PROBE_PEAK_MEM_BW:
	case DC_PROBE_AVG_MEM_BW:
	case DC_PROBE_MEM_LATENCY:
	case DC_PROBE_URGENT_ASSERTION_COUNT:
	case DC_PROBE_PREFETCH_DATA_SIZE:
	case DC_PROBE_URGENT_RAMP_LATENCY:
		return true;
	default:
		return false;
	}
}

/**
 * dcn60_program_perfmon - Program/transition perfmon probes for a commit.
 * @dc:      DC structure
 * @context: target state; probes, probe_count, and probe_status are
 *           read from and written to this object
 *
 * Routes each probe to the HW-block builder that owns its measurement type,
 * builds a single combined BLS sequence, executes it once, then updates
 * context->probe_status in plain C.
 */
void dcn60_program_perfmon(struct dc *dc, struct dc_state *context)
{
	int i;

	if (!context)
		return;

	context->block_sequence_steps = 0;
	memset(context->probe_status, 0, sizeof(context->probe_status));

	for (i = 0; i < context->probe_count; i++) {
		if (is_probe_measurement_type_for_hubbub(context->probes[i].type))
			dcn60_build_hubbub_perfmon_sequence(dc, context, &context->probes[i],
					&context->probe_status[i],
					context->block_sequence,
					&context->block_sequence_steps);
	}

	hwss_execute_sequence(dc, context->block_sequence, context->block_sequence_steps);

	for (i = 0; i < context->probe_count; i++)
		dcn60_update_probe_status(&context->probe_status[i]);
}

static bool dcn60_has_active_memory_request(const struct dc *dc)
{
	int i;

	/* Check for any streams with active planes but no static panel power features. */
	for (i = 0; i < dc->current_state->stream_count; i++) {
		const struct dc_link *link = dc->current_state->streams[i]->link;
		bool panel_power_feature =
			link && (link->psr_settings.psr_version != DC_PSR_VERSION_UNSUPPORTED ||
				 link->replay_settings.replay_feature_enabled);

		if (dc->current_state->stream_status[i].plane_count && !panel_power_feature)
			return true;
	}

	return false;
}

static bool dcn60_has_active_display(const struct dc *dc)
{
	int i;

	for (i = 0; i < dc->current_state->stream_count; ++i) {
		const struct dc_stream_state *stream = dc->current_state->streams[i];

		if (dc_is_virtual_signal(stream->signal) ||
		    dc_is_hdmi_tmds_signal(stream->signal) ||
		    (dc_is_dp_signal(stream->signal) && !stream->dpms_off)) {
			return true;
		}
	}

	for (i = 0; i < dc->link_count; i++) {
		const struct dc_link *link = dc->links[i];

		if (link->link_status.link_active ||
		    (link->link_enc && link->link_enc->funcs->is_dig_enabled &&
		     link->link_enc->funcs->is_dig_enabled(link->link_enc))) {
			return true;
		}
	}

	return false;
}

static void dcn60_notify_dmub_of_cab_status(struct dc *dc, bool enable)
{
	union dmub_rb_cmd cmd;

	if (!dc->ctx->dmub_srv || !dc->current_state)
		return;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cab.header.type = DMUB_CMD__CAB_FOR_SS;
	cmd.cab.header.payload_bytes = sizeof(cmd.cab) - sizeof(cmd.cab.header);

	if (enable) {
		if (!dcn60_has_active_memory_request(dc)) {
			DC_LOG_MALL("sending CAB action NO_DCN_REQ\n");
			cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_DCN_REQ;
		} else {
			cmd.cab.header.sub_type = DMUB_CMD__CAB_DCN_SS_NOT_FIT_IN_CAB;
			DC_LOG_MALL("MALL unsupported, frame does not fit in CAB\n");
		}
	} else {
		/* Disable CAB */
		cmd.cab.header.sub_type = DMUB_CMD__CAB_NO_IDLE_OPTIMIZATION;
		DC_LOG_MALL("CAB idle optimization disabled\n");
	}

	dm_execute_dmub_cmd(dc->ctx, &cmd, DM_DMUB_WAIT_TYPE_WAIT);
}

bool dcn60_apply_idle_power_optimizations(struct dc *dc, bool enable)
{
	struct clk_mgr *clk_mgr = dc->clk_mgr;

	dcn60_notify_dmub_of_cab_status(dc, enable);

	/* Notify clock manager and PMFW to disable PHY refclk or DF coupling. */
	if (dc->clk_mgr && dc->clk_mgr->funcs->set_idle_power_optimizations) {
		const bool allow_idle = enable && !dcn60_has_active_display(dc);

		clk_mgr->funcs->set_idle_power_optimizations(clk_mgr, allow_idle);
	}

	return true;
}
