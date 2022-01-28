/* Copyright 2015 Advanced Micro Devices, Inc. */

#include "link_hwss.h"
#include "dm_helpers.h"
#include "core_types.h"
#include "dccg.h"
#include "link_enc_cfg.h"
#include "dc_link_dp.h"

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

static void virtual_setup_stream_encoder(struct pipe_ctx *pipe_ctx);
static void virtual_reset_stream_encoder(struct pipe_ctx *pipe_ctx);

/************************* below goes to dio_link_hwss ************************/
static void set_dio_throttled_vcp_size(struct pipe_ctx *pipe_ctx,
		struct fixed31_32 throttled_vcp_size)
{
	struct stream_encoder *stream_encoder = pipe_ctx->stream_res.stream_enc;

	stream_encoder->funcs->set_throttled_vcp_size(
				stream_encoder,
				throttled_vcp_size);
}

static void setup_dio_stream_encoder(struct pipe_ctx *pipe_ctx)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);

	link_enc->funcs->connect_dig_be_to_fe(link_enc,
			pipe_ctx->stream_res.stream_enc->id, true);
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_CONNECT_DIG_FE_BE);
}

static void reset_dio_stream_encoder(struct pipe_ctx *pipe_ctx)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(pipe_ctx->stream->link);

	link_enc->funcs->connect_dig_be_to_fe(
			link_enc,
			pipe_ctx->stream_res.stream_enc->id,
			false);
	if (dc_is_dp_signal(pipe_ctx->stream->signal))
		dp_source_sequence_trace(pipe_ctx->stream->link,
				DPCD_SOURCE_SEQ_AFTER_DISCONNECT_DIG_FE_BE);

}

static void enable_dio_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	if (dc_is_dp_sst_signal(signal))
		link_enc->funcs->enable_dp_output(
				link_enc,
				link_settings,
				clock_source);
	else
		link_enc->funcs->enable_dp_mst_output(
				link_enc,
				link_settings,
				clock_source);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_ENABLE_LINK_PHY);
}


static void disable_dio_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->disable_output(link_enc, signal);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_DISABLE_LINK_PHY);
}

static void set_dio_dp_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	ASSERT(link_enc);
	link_enc->funcs->dp_set_phy_pattern(link_enc, tp_params);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_SET_SOURCE_PATTERN);
}

static void set_dio_dp_lane_settings(struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	struct link_encoder *link_enc = link_enc_cfg_get_link_enc(link);

	link_enc->funcs->dp_set_lane_settings(link_enc, link_settings, lane_settings);
}

static const struct link_hwss dio_link_hwss = {
	.setup_stream_encoder = setup_dio_stream_encoder,
	.reset_stream_encoder = reset_dio_stream_encoder,
	.ext = {
		.set_throttled_vcp_size = set_dio_throttled_vcp_size,
		.enable_dp_link_output = enable_dio_dp_link_output,
		.disable_dp_link_output = disable_dio_dp_link_output,
		.set_dp_link_test_pattern = set_dio_dp_link_test_pattern,
		.set_dp_lane_settings = set_dio_dp_lane_settings,
	},
};

bool can_use_dio_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	return link->link_enc != NULL;
}

const struct link_hwss *get_dio_link_hwss(void)
{
	return &dio_link_hwss;
}

/*********************** below goes to hpo_dp_link_hwss ***********************/
static void set_hpo_dp_throttled_vcp_size(struct pipe_ctx *pipe_ctx,
		struct fixed31_32 throttled_vcp_size)
{
	struct hpo_dp_stream_encoder *hpo_dp_stream_encoder =
			pipe_ctx->stream_res.hpo_dp_stream_enc;
	struct hpo_dp_link_encoder *hpo_dp_link_encoder =
			pipe_ctx->link_res.hpo_dp_link_enc;

	hpo_dp_link_encoder->funcs->set_throttled_vcp_size(hpo_dp_link_encoder,
			hpo_dp_stream_encoder->inst,
			throttled_vcp_size);
}

static void set_hpo_dp_hblank_min_symbol_width(struct pipe_ctx *pipe_ctx,
		const struct dc_link_settings *link_settings,
		struct fixed31_32 throttled_vcp_size)
{
	struct hpo_dp_stream_encoder *hpo_dp_stream_encoder =
			pipe_ctx->stream_res.hpo_dp_stream_enc;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;
	struct fixed31_32 h_blank_in_ms, time_slot_in_ms, mtp_cnt_per_h_blank;
	uint32_t link_bw_in_kbps =
			dc_link_bandwidth_kbps(pipe_ctx->stream->link, link_settings);
	uint16_t hblank_min_symbol_width = 0;

	if (link_bw_in_kbps > 0) {
		h_blank_in_ms = dc_fixpt_div(dc_fixpt_from_int(
				timing->h_total - timing->h_addressable),
				dc_fixpt_from_fraction(timing->pix_clk_100hz, 10));
		time_slot_in_ms = dc_fixpt_from_fraction(32 * 4, link_bw_in_kbps);
		mtp_cnt_per_h_blank = dc_fixpt_div(h_blank_in_ms,
				dc_fixpt_mul_int(time_slot_in_ms, 64));
		hblank_min_symbol_width = dc_fixpt_floor(
				dc_fixpt_mul(mtp_cnt_per_h_blank, throttled_vcp_size));
	}

	hpo_dp_stream_encoder->funcs->set_hblank_min_symbol_width(hpo_dp_stream_encoder,
			hblank_min_symbol_width);
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

static void setup_hpo_dp_stream_encoder(struct pipe_ctx *pipe_ctx)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct hpo_dp_stream_encoder *stream_enc = pipe_ctx->stream_res.hpo_dp_stream_enc;
	struct hpo_dp_link_encoder *link_enc = pipe_ctx->link_res.hpo_dp_link_enc;
	struct dccg *dccg = dc->res_pool->dccg;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;
	int odm_segment_count = get_odm_segment_count(pipe_ctx);
	enum phyd32clk_clock_source phyd32clk = get_phyd32clk_src(pipe_ctx->stream->link);

	dccg->funcs->set_dpstreamclk(dccg, DTBCLK0, tg->inst);
	dccg->funcs->enable_symclk32_se(dccg, stream_enc->inst, phyd32clk);
	dccg->funcs->set_dtbclk_dto(dccg, tg->inst, pipe_ctx->stream->phy_pix_clk,
			odm_segment_count,
			&pipe_ctx->stream->timing);
	stream_enc->funcs->enable_stream(stream_enc);
	stream_enc->funcs->map_stream_to_link(stream_enc, stream_enc->inst, link_enc->inst);
}

static void reset_hpo_dp_stream_encoder(struct pipe_ctx *pipe_ctx)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	struct hpo_dp_stream_encoder *stream_enc = pipe_ctx->stream_res.hpo_dp_stream_enc;
	struct dccg *dccg = dc->res_pool->dccg;
	struct timing_generator *tg = pipe_ctx->stream_res.tg;

	stream_enc->funcs->disable(stream_enc);
	dccg->funcs->set_dtbclk_dto(dccg, tg->inst, 0, 0, &pipe_ctx->stream->timing);
	dccg->funcs->disable_symclk32_se(dccg, stream_enc->inst);
	dccg->funcs->set_dpstreamclk(dccg, REFCLK, tg->inst);
}

static void enable_hpo_dp_fpga_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings)
{
	const struct dc *dc = link->dc;
	enum phyd32clk_clock_source phyd32clk = get_phyd32clk_src(link);
	int phyd32clk_freq_khz = link_settings->link_rate == LINK_RATE_UHBR10 ? 312500 :
			link_settings->link_rate == LINK_RATE_UHBR13_5 ? 412875 :
			link_settings->link_rate == LINK_RATE_UHBR20 ? 625000 : 0;

	dm_set_phyd32clk(dc->ctx, phyd32clk_freq_khz);
	dc->res_pool->dccg->funcs->set_physymclk(
			dc->res_pool->dccg,
			link->link_enc_hw_inst,
			PHYSYMCLK_FORCE_SRC_PHYD32CLK,
			true);
	dc->res_pool->dccg->funcs->enable_symclk32_le(
			dc->res_pool->dccg,
			link_res->hpo_dp_link_enc->inst,
			phyd32clk);
	link_res->hpo_dp_link_enc->funcs->link_enable(
			link_res->hpo_dp_link_enc,
			link_settings->lane_count);

}

static void enable_hpo_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal,
		enum clock_source_id clock_source,
		const struct dc_link_settings *link_settings)
{
	if (IS_FPGA_MAXIMUS_DC(link->dc->ctx->dce_environment))
		enable_hpo_dp_fpga_link_output(link, link_res, signal,
				clock_source, link_settings);
	else
		link_res->hpo_dp_link_enc->funcs->enable_link_phy(
				link_res->hpo_dp_link_enc,
				link_settings,
				link->link_enc->transmitter,
				link->link_enc->hpd_source);
}


static void disable_hpo_dp_fpga_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	const struct dc *dc = link->dc;

	link_res->hpo_dp_link_enc->funcs->link_disable(link_res->hpo_dp_link_enc);
	dc->res_pool->dccg->funcs->disable_symclk32_le(
			dc->res_pool->dccg,
			link_res->hpo_dp_link_enc->inst);
	dc->res_pool->dccg->funcs->set_physymclk(
			dc->res_pool->dccg,
			link->link_enc_hw_inst,
			PHYSYMCLK_FORCE_SRC_SYMCLK,
			false);
	dm_set_phyd32clk(dc->ctx, 0);
}

static void disable_hpo_dp_link_output(struct dc_link *link,
		const struct link_resource *link_res,
		enum signal_type signal)
{
	if (IS_FPGA_MAXIMUS_DC(link->dc->ctx->dce_environment)) {
		disable_hpo_dp_fpga_link_output(link, link_res, signal);
	} else {
		link_res->hpo_dp_link_enc->funcs->link_disable(link_res->hpo_dp_link_enc);
		link_res->hpo_dp_link_enc->funcs->disable_link_phy(
				link_res->hpo_dp_link_enc, signal);
	}
}

static void set_hpo_dp_link_test_pattern(struct dc_link *link,
		const struct link_resource *link_res,
		struct encoder_set_dp_phy_pattern_param *tp_params)
{
	link_res->hpo_dp_link_enc->funcs->set_link_test_pattern(
			link_res->hpo_dp_link_enc, tp_params);
	dp_source_sequence_trace(link, DPCD_SOURCE_SEQ_AFTER_SET_SOURCE_PATTERN);
}

static void set_hpo_dp_lane_settings(struct dc_link *link,
		const struct link_resource *link_res,
		const struct dc_link_settings *link_settings,
		const struct dc_lane_settings lane_settings[LANE_COUNT_DP_MAX])
{
	link_res->hpo_dp_link_enc->funcs->set_ffe(
			link_res->hpo_dp_link_enc,
			link_settings,
			lane_settings[0].FFE_PRESET.raw);
}

static const struct link_hwss hpo_dp_link_hwss = {
	.setup_stream_encoder = setup_hpo_dp_stream_encoder,
	.reset_stream_encoder = reset_hpo_dp_stream_encoder,
	.ext = {
		.set_throttled_vcp_size = set_hpo_dp_throttled_vcp_size,
		.set_hblank_min_symbol_width = set_hpo_dp_hblank_min_symbol_width,
		.enable_dp_link_output = enable_hpo_dp_link_output,
		.disable_dp_link_output = disable_hpo_dp_link_output,
		.set_dp_link_test_pattern  = set_hpo_dp_link_test_pattern,
		.set_dp_lane_settings = set_hpo_dp_lane_settings,
	},
};

bool can_use_hpo_dp_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	return link_res->hpo_dp_link_enc != NULL;
}

const struct link_hwss *get_hpo_dp_link_hwss(void)
{
	return &hpo_dp_link_hwss;
}

/*********************** below goes to dpia_link_hwss *************************/
static const struct link_hwss dpia_link_hwss = {
	.setup_stream_encoder = setup_dio_stream_encoder,
	.reset_stream_encoder = reset_dio_stream_encoder,
	.ext = {
		.set_throttled_vcp_size = set_dio_throttled_vcp_size,
		.enable_dp_link_output = enable_dio_dp_link_output,
		.disable_dp_link_output = disable_dio_dp_link_output,
		.set_dp_link_test_pattern = set_dio_dp_link_test_pattern,
		.set_dp_lane_settings = set_dio_dp_lane_settings,
	},
};

bool can_use_dpia_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	return link->is_dig_mapping_flexible &&
			link->dc->res_pool->funcs->link_encs_assign;
}

const struct link_hwss *get_dpia_link_hwss(void)
{
	return &dpia_link_hwss;
}
/*********************** below goes to virtual_link_hwss ******************************/
static void virtual_setup_stream_encoder(struct pipe_ctx *pipe_ctx)
{
}

static void virtual_reset_stream_encoder(struct pipe_ctx *pipe_ctx)
{
}
static const struct link_hwss virtual_link_hwss = {
	.setup_stream_encoder = virtual_setup_stream_encoder,
	.reset_stream_encoder = virtual_reset_stream_encoder,
};

const struct link_hwss *get_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	if (can_use_dp_hpo_link_hwss(link, link_res))
		/* TODO: some assumes that if decided link settings is 128b/132b
		 * channel coding format hpo_dp_link_enc should be used.
		 * Others believe that if hpo_dp_link_enc is available in link
		 * resource then hpo_dp_link_enc must be used. This bound between
		 * hpo_dp_link_enc != NULL and decided link settings is loosely coupled
		 * with a premise that both hpo_dp_link_enc pointer and decided link
		 * settings are determined based on single policy function like
		 * "decide_link_settings" from upper layer. This "convention"
		 * cannot be maintained and enforced at current level.
		 * Therefore a refactor is due so we can enforce a strong bound
		 * between those two parameters at this level.
		 *
		 * To put it simple, we want to make enforcement at low level so that
		 * we will not return link hwss if caller plans to do 8b/10b
		 * with an hpo encoder. Or we can return a very dummy one that doesn't
		 * do work for all functions
		 */
		return &hpo_dp_link_hwss;
	else if (can_use_dpia_link_hwss(link, link_res))
		return &dpia_link_hwss;
	else if (can_use_dio_link_hwss(link, link_res))
		return &dio_link_hwss;
	else
		return &virtual_link_hwss;
}

#undef DC_LOGGER
