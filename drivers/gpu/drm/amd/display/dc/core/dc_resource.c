/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "resource.h"
#include "include/irq_service_interface.h"
#include "link_encoder.h"
#include "stream_encoder.h"
#include "opp.h"
#include "timing_generator.h"
#include "transform.h"
#include "dccg.h"
#include "dchubbub.h"
#include "dpp.h"
#include "core_types.h"
#include "set_mode_types.h"
#include "virtual/virtual_stream_encoder.h"
#include "dpcd_defs.h"
#include "link_enc_cfg.h"
#include "link.h"
#include "clk_mgr.h"
#include "dc_state_priv.h"
#include "dc_stream_priv.h"

#include "virtual/virtual_link_hwss.h"
#include "link/hwss/link_hwss_dio.h"
#include "link/hwss/link_hwss_dpia.h"
#include "link/hwss/link_hwss_hpo_dp.h"
#include "link/hwss/link_hwss_dio_fixed_vs_pe_retimer.h"
#include "link/hwss/link_hwss_hpo_fixed_vs_pe_retimer_dp.h"

#if defined(CONFIG_DRM_AMD_DC_SI)
#include "dce60/dce60_resource.h"
#endif
#include "dce80/dce80_resource.h"
#include "dce100/dce100_resource.h"
#include "dce110/dce110_resource.h"
#include "dce112/dce112_resource.h"
#include "dce120/dce120_resource.h"
#include "dcn10/dcn10_resource.h"
#include "dcn20/dcn20_resource.h"
#include "dcn21/dcn21_resource.h"
#include "dcn201/dcn201_resource.h"
#include "dcn30/dcn30_resource.h"
#include "dcn301/dcn301_resource.h"
#include "dcn302/dcn302_resource.h"
#include "dcn303/dcn303_resource.h"
#include "dcn31/dcn31_resource.h"
#include "dcn314/dcn314_resource.h"
#include "dcn315/dcn315_resource.h"
#include "dcn316/dcn316_resource.h"
#include "dcn32/dcn32_resource.h"
#include "dcn321/dcn321_resource.h"
#include "dcn35/dcn35_resource.h"
#include "dcn351/dcn351_resource.h"
#include "dcn401/dcn401_resource.h"
#if defined(CONFIG_DRM_AMD_DC_FP)
#include "dc_spl_translate.h"
#endif

#define VISUAL_CONFIRM_BASE_DEFAULT 3
#define VISUAL_CONFIRM_BASE_MIN 1
#define VISUAL_CONFIRM_BASE_MAX 10
/* we choose 240 because it is a common denominator of common v addressable
 * such as 2160, 1440, 1200, 960. So we take 1/240 portion of v addressable as
 * the visual confirm dpp offset height. So visual confirm height can stay
 * relatively the same independent from timing used.
 */
#define VISUAL_CONFIRM_DPP_OFFSET_DENO 240

#define DC_LOGGER \
	dc->ctx->logger
#define DC_LOGGER_INIT(logger)

#include "dml2/dml2_wrapper.h"

#define UNABLE_TO_SPLIT -1

enum dce_version resource_parse_asic_id(struct hw_asic_id asic_id)
{
	enum dce_version dc_version = DCE_VERSION_UNKNOWN;

	switch (asic_id.chip_family) {

#if defined(CONFIG_DRM_AMD_DC_SI)
	case FAMILY_SI:
		if (ASIC_REV_IS_TAHITI_P(asic_id.hw_internal_rev) ||
		    ASIC_REV_IS_PITCAIRN_PM(asic_id.hw_internal_rev) ||
		    ASIC_REV_IS_CAPEVERDE_M(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_6_0;
		else if (ASIC_REV_IS_OLAND_M(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_6_4;
		else
			dc_version = DCE_VERSION_6_1;
		break;
#endif
	case FAMILY_CI:
		dc_version = DCE_VERSION_8_0;
		break;
	case FAMILY_KV:
		if (ASIC_REV_IS_KALINDI(asic_id.hw_internal_rev) ||
		    ASIC_REV_IS_BHAVANI(asic_id.hw_internal_rev) ||
		    ASIC_REV_IS_GODAVARI(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_8_3;
		else
			dc_version = DCE_VERSION_8_1;
		break;
	case FAMILY_CZ:
		dc_version = DCE_VERSION_11_0;
		break;

	case FAMILY_VI:
		if (ASIC_REV_IS_TONGA_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_FIJI_P(asic_id.hw_internal_rev)) {
			dc_version = DCE_VERSION_10_0;
			break;
		}
		if (ASIC_REV_IS_POLARIS10_P(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS11_M(asic_id.hw_internal_rev) ||
				ASIC_REV_IS_POLARIS12_V(asic_id.hw_internal_rev)) {
			dc_version = DCE_VERSION_11_2;
		}
		if (ASIC_REV_IS_VEGAM(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_11_22;
		break;
	case FAMILY_AI:
		if (ASICREV_IS_VEGA20_P(asic_id.hw_internal_rev))
			dc_version = DCE_VERSION_12_1;
		else
			dc_version = DCE_VERSION_12_0;
		break;
	case FAMILY_RV:
		dc_version = DCN_VERSION_1_0;
		if (ASICREV_IS_RAVEN2(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_1_01;
		if (ASICREV_IS_RENOIR(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_2_1;
		if (ASICREV_IS_GREEN_SARDINE(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_2_1;
		break;

	case FAMILY_NV:
		dc_version = DCN_VERSION_2_0;
		if (asic_id.chip_id == DEVICE_ID_NV_13FE || asic_id.chip_id == DEVICE_ID_NV_143F) {
			dc_version = DCN_VERSION_2_01;
			break;
		}
		if (ASICREV_IS_SIENNA_CICHLID_P(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_0;
		if (ASICREV_IS_DIMGREY_CAVEFISH_P(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_02;
		if (ASICREV_IS_BEIGE_GOBY_P(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_03;
		break;

	case FAMILY_VGH:
		dc_version = DCN_VERSION_3_01;
		break;

	case FAMILY_YELLOW_CARP:
		if (ASICREV_IS_YELLOW_CARP(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_1;
		break;
	case AMDGPU_FAMILY_GC_10_3_6:
		if (ASICREV_IS_GC_10_3_6(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_15;
		break;
	case AMDGPU_FAMILY_GC_10_3_7:
		if (ASICREV_IS_GC_10_3_7(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_16;
		break;
	case AMDGPU_FAMILY_GC_11_0_0:
		dc_version = DCN_VERSION_3_2;
		if (ASICREV_IS_GC_11_0_2(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_21;
		break;
	case AMDGPU_FAMILY_GC_11_0_1:
		dc_version = DCN_VERSION_3_14;
		break;
	case AMDGPU_FAMILY_GC_11_5_0:
		dc_version = DCN_VERSION_3_5;
		if (ASICREV_IS_GC_11_0_4(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_3_51;
		break;
	case AMDGPU_FAMILY_GC_12_0_0:
		if (ASICREV_IS_GC_12_0_1_A0(asic_id.hw_internal_rev) ||
			ASICREV_IS_GC_12_0_0_A0(asic_id.hw_internal_rev))
			dc_version = DCN_VERSION_4_01;
		break;
	default:
		dc_version = DCE_VERSION_UNKNOWN;
		break;
	}
	return dc_version;
}

struct resource_pool *dc_create_resource_pool(struct dc  *dc,
					      const struct dc_init_data *init_data,
					      enum dce_version dc_version)
{
	struct resource_pool *res_pool = NULL;

	switch (dc_version) {
#if defined(CONFIG_DRM_AMD_DC_SI)
	case DCE_VERSION_6_0:
		res_pool = dce60_create_resource_pool(
			init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_6_1:
		res_pool = dce61_create_resource_pool(
			init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_6_4:
		res_pool = dce64_create_resource_pool(
			init_data->num_virtual_links, dc);
		break;
#endif
	case DCE_VERSION_8_0:
		res_pool = dce80_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_8_1:
		res_pool = dce81_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_8_3:
		res_pool = dce83_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_10_0:
		res_pool = dce100_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_11_0:
		res_pool = dce110_create_resource_pool(
				init_data->num_virtual_links, dc,
				init_data->asic_id);
		break;
	case DCE_VERSION_11_2:
	case DCE_VERSION_11_22:
		res_pool = dce112_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;
	case DCE_VERSION_12_0:
	case DCE_VERSION_12_1:
		res_pool = dce120_create_resource_pool(
				init_data->num_virtual_links, dc);
		break;

#if defined(CONFIG_DRM_AMD_DC_FP)
	case DCN_VERSION_1_0:
	case DCN_VERSION_1_01:
		res_pool = dcn10_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_2_0:
		res_pool = dcn20_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_2_1:
		res_pool = dcn21_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_2_01:
		res_pool = dcn201_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_0:
		res_pool = dcn30_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_01:
		res_pool = dcn301_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_02:
		res_pool = dcn302_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_03:
		res_pool = dcn303_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_1:
		res_pool = dcn31_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_14:
		res_pool = dcn314_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_15:
		res_pool = dcn315_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_16:
		res_pool = dcn316_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_2:
		res_pool = dcn32_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_21:
		res_pool = dcn321_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_5:
		res_pool = dcn35_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_3_51:
		res_pool = dcn351_create_resource_pool(init_data, dc);
		break;
	case DCN_VERSION_4_01:
		res_pool = dcn401_create_resource_pool(init_data, dc);
		break;
#endif /* CONFIG_DRM_AMD_DC_FP */
	default:
		break;
	}

	if (res_pool != NULL) {
		if (dc->ctx->dc_bios->fw_info_valid) {
			res_pool->ref_clocks.xtalin_clock_inKhz =
				dc->ctx->dc_bios->fw_info.pll_info.crystal_frequency;
			/* initialize with firmware data first, no all
			 * ASIC have DCCG SW component. FPGA or
			 * simulation need initialization of
			 * dccg_ref_clock_inKhz, dchub_ref_clock_inKhz
			 * with xtalin_clock_inKhz
			 */
			res_pool->ref_clocks.dccg_ref_clock_inKhz =
				res_pool->ref_clocks.xtalin_clock_inKhz;
			res_pool->ref_clocks.dchub_ref_clock_inKhz =
				res_pool->ref_clocks.xtalin_clock_inKhz;
		} else
			ASSERT_CRITICAL(false);
	}

	return res_pool;
}

void dc_destroy_resource_pool(struct dc *dc)
{
	if (dc) {
		if (dc->res_pool)
			dc->res_pool->funcs->destroy(&dc->res_pool);

		kfree(dc->hwseq);
	}
}

static void update_num_audio(
	const struct resource_straps *straps,
	unsigned int *num_audio,
	struct audio_support *aud_support)
{
	aud_support->dp_audio = true;
	aud_support->hdmi_audio_native = false;
	aud_support->hdmi_audio_on_dongle = false;

	if (straps->hdmi_disable == 0) {
		if (straps->dc_pinstraps_audio & 0x2) {
			aud_support->hdmi_audio_on_dongle = true;
			aud_support->hdmi_audio_native = true;
		}
	}

	switch (straps->audio_stream_number) {
	case 0: /* multi streams supported */
		break;
	case 1: /* multi streams not supported */
		*num_audio = 1;
		break;
	default:
		DC_ERR("DC: unexpected audio fuse!\n");
	}
}

bool resource_construct(
	unsigned int num_virtual_links,
	struct dc  *dc,
	struct resource_pool *pool,
	const struct resource_create_funcs *create_funcs)
{
	struct dc_context *ctx = dc->ctx;
	const struct resource_caps *caps = pool->res_cap;
	int i;
	unsigned int num_audio = caps->num_audio;
	struct resource_straps straps = {0};

	if (create_funcs->read_dce_straps)
		create_funcs->read_dce_straps(dc->ctx, &straps);

	pool->audio_count = 0;
	if (create_funcs->create_audio) {
		/* find the total number of streams available via the
		 * AZALIA_F0_CODEC_PIN_CONTROL_RESPONSE_CONFIGURATION_DEFAULT
		 * registers (one for each pin) starting from pin 1
		 * up to the max number of audio pins.
		 * We stop on the first pin where
		 * PORT_CONNECTIVITY == 1 (as instructed by HW team).
		 */
		update_num_audio(&straps, &num_audio, &pool->audio_support);
		for (i = 0; i < caps->num_audio; i++) {
			struct audio *aud = create_funcs->create_audio(ctx, i);

			if (aud == NULL) {
				DC_ERR("DC: failed to create audio!\n");
				return false;
			}
			if (!aud->funcs->endpoint_valid(aud)) {
				aud->funcs->destroy(&aud);
				break;
			}
			pool->audios[i] = aud;
			pool->audio_count++;
		}
	}

	pool->stream_enc_count = 0;
	if (create_funcs->create_stream_encoder) {
		for (i = 0; i < caps->num_stream_encoder; i++) {
			pool->stream_enc[i] = create_funcs->create_stream_encoder(i, ctx);
			if (pool->stream_enc[i] == NULL)
				DC_ERR("DC: failed to create stream_encoder!\n");
			pool->stream_enc_count++;
		}
	}

	pool->hpo_dp_stream_enc_count = 0;
	if (create_funcs->create_hpo_dp_stream_encoder) {
		for (i = 0; i < caps->num_hpo_dp_stream_encoder; i++) {
			pool->hpo_dp_stream_enc[i] = create_funcs->create_hpo_dp_stream_encoder(i+ENGINE_ID_HPO_DP_0, ctx);
			if (pool->hpo_dp_stream_enc[i] == NULL)
				DC_ERR("DC: failed to create HPO DP stream encoder!\n");
			pool->hpo_dp_stream_enc_count++;

		}
	}

	pool->hpo_dp_link_enc_count = 0;
	if (create_funcs->create_hpo_dp_link_encoder) {
		for (i = 0; i < caps->num_hpo_dp_link_encoder; i++) {
			pool->hpo_dp_link_enc[i] = create_funcs->create_hpo_dp_link_encoder(i, ctx);
			if (pool->hpo_dp_link_enc[i] == NULL)
				DC_ERR("DC: failed to create HPO DP link encoder!\n");
			pool->hpo_dp_link_enc_count++;
		}
	}

	for (i = 0; i < caps->num_mpc_3dlut; i++) {
		pool->mpc_lut[i] = dc_create_3dlut_func();
		if (pool->mpc_lut[i] == NULL)
			DC_ERR("DC: failed to create MPC 3dlut!\n");
		pool->mpc_shaper[i] = dc_create_transfer_func();
		if (pool->mpc_shaper[i] == NULL)
			DC_ERR("DC: failed to create MPC shaper!\n");
	}

	dc->caps.dynamic_audio = false;
	if (pool->audio_count < pool->stream_enc_count) {
		dc->caps.dynamic_audio = true;
	}
	for (i = 0; i < num_virtual_links; i++) {
		pool->stream_enc[pool->stream_enc_count] =
			virtual_stream_encoder_create(
					ctx, ctx->dc_bios);
		if (pool->stream_enc[pool->stream_enc_count] == NULL) {
			DC_ERR("DC: failed to create stream_encoder!\n");
			return false;
		}
		pool->stream_enc_count++;
	}

	dc->hwseq = create_funcs->create_hwseq(ctx);

	return true;
}
static int find_matching_clock_source(
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{

	int i;

	for (i = 0; i < pool->clk_src_count; i++) {
		if (pool->clock_sources[i] == clock_source)
			return i;
	}
	return -1;
}

void resource_unreference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{
	int i = find_matching_clock_source(pool, clock_source);

	if (i > -1)
		res_ctx->clock_source_ref_count[i]--;

	if (pool->dp_clock_source == clock_source)
		res_ctx->dp_clock_source_ref_count--;
}

void resource_reference_clock_source(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{
	int i = find_matching_clock_source(pool, clock_source);

	if (i > -1)
		res_ctx->clock_source_ref_count[i]++;

	if (pool->dp_clock_source == clock_source)
		res_ctx->dp_clock_source_ref_count++;
}

int resource_get_clock_source_reference(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct clock_source *clock_source)
{
	int i = find_matching_clock_source(pool, clock_source);

	if (i > -1)
		return res_ctx->clock_source_ref_count[i];

	if (pool->dp_clock_source == clock_source)
		return res_ctx->dp_clock_source_ref_count;

	return -1;
}

bool resource_are_vblanks_synchronizable(
	struct dc_stream_state *stream1,
	struct dc_stream_state *stream2)
{
	uint32_t base60_refresh_rates[] = {10, 20, 5};
	uint8_t i;
	uint8_t rr_count = ARRAY_SIZE(base60_refresh_rates);
	uint64_t frame_time_diff;

	if (stream1->ctx->dc->config.vblank_alignment_dto_params &&
		stream1->ctx->dc->config.vblank_alignment_max_frame_time_diff > 0 &&
		dc_is_dp_signal(stream1->signal) &&
		dc_is_dp_signal(stream2->signal) &&
		false == stream1->has_non_synchronizable_pclk &&
		false == stream2->has_non_synchronizable_pclk &&
		stream1->timing.flags.VBLANK_SYNCHRONIZABLE &&
		stream2->timing.flags.VBLANK_SYNCHRONIZABLE) {
		/* disable refresh rates higher than 60Hz for now */
		if (stream1->timing.pix_clk_100hz*100/stream1->timing.h_total/
				stream1->timing.v_total > 60)
			return false;
		if (stream2->timing.pix_clk_100hz*100/stream2->timing.h_total/
				stream2->timing.v_total > 60)
			return false;
		frame_time_diff = (uint64_t)10000 *
			stream1->timing.h_total *
			stream1->timing.v_total *
			stream2->timing.pix_clk_100hz;
		frame_time_diff = div_u64(frame_time_diff, stream1->timing.pix_clk_100hz);
		frame_time_diff = div_u64(frame_time_diff, stream2->timing.h_total);
		frame_time_diff = div_u64(frame_time_diff, stream2->timing.v_total);
		for (i = 0; i < rr_count; i++) {
			int64_t diff = (int64_t)div_u64(frame_time_diff * base60_refresh_rates[i], 10) - 10000;

			if (diff < 0)
				diff = -diff;
			if (diff < stream1->ctx->dc->config.vblank_alignment_max_frame_time_diff)
				return true;
		}
	}
	return false;
}

bool resource_are_streams_timing_synchronizable(
	struct dc_stream_state *stream1,
	struct dc_stream_state *stream2)
{
	if (stream1->timing.h_total != stream2->timing.h_total)
		return false;

	if (stream1->timing.v_total != stream2->timing.v_total)
		return false;

	if (stream1->timing.h_addressable
				!= stream2->timing.h_addressable)
		return false;

	if (stream1->timing.v_addressable
				!= stream2->timing.v_addressable)
		return false;

	if (stream1->timing.v_front_porch
				!= stream2->timing.v_front_porch)
		return false;

	if (stream1->timing.pix_clk_100hz
				!= stream2->timing.pix_clk_100hz)
		return false;

	if (stream1->clamping.c_depth != stream2->clamping.c_depth)
		return false;

	if (stream1->phy_pix_clk != stream2->phy_pix_clk
			&& (!dc_is_dp_signal(stream1->signal)
			|| !dc_is_dp_signal(stream2->signal)))
		return false;

	if (stream1->view_format != stream2->view_format)
		return false;

	if (stream1->ignore_msa_timing_param || stream2->ignore_msa_timing_param)
		return false;

	return true;
}
static bool is_dp_and_hdmi_sharable(
		struct dc_stream_state *stream1,
		struct dc_stream_state *stream2)
{
	if (stream1->ctx->dc->caps.disable_dp_clk_share)
		return false;

	if (stream1->clamping.c_depth != COLOR_DEPTH_888 ||
		stream2->clamping.c_depth != COLOR_DEPTH_888)
		return false;

	return true;

}

static bool is_sharable_clk_src(
	const struct pipe_ctx *pipe_with_clk_src,
	const struct pipe_ctx *pipe)
{
	if (pipe_with_clk_src->clock_source == NULL)
		return false;

	if (pipe_with_clk_src->stream->signal == SIGNAL_TYPE_VIRTUAL)
		return false;

	if (dc_is_dp_signal(pipe_with_clk_src->stream->signal) ||
		(dc_is_dp_signal(pipe->stream->signal) &&
		!is_dp_and_hdmi_sharable(pipe_with_clk_src->stream,
				     pipe->stream)))
		return false;

	if (dc_is_hdmi_signal(pipe_with_clk_src->stream->signal)
			&& dc_is_dual_link_signal(pipe->stream->signal))
		return false;

	if (dc_is_hdmi_signal(pipe->stream->signal)
			&& dc_is_dual_link_signal(pipe_with_clk_src->stream->signal))
		return false;

	if (!resource_are_streams_timing_synchronizable(
			pipe_with_clk_src->stream, pipe->stream))
		return false;

	return true;
}

struct clock_source *resource_find_used_clk_src_for_sharing(
					struct resource_context *res_ctx,
					struct pipe_ctx *pipe_ctx)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (is_sharable_clk_src(&res_ctx->pipe_ctx[i], pipe_ctx))
			return res_ctx->pipe_ctx[i].clock_source;
	}

	return NULL;
}

static enum pixel_format convert_pixel_format_to_dalsurface(
		enum surface_pixel_format surface_pixel_format)
{
	enum pixel_format dal_pixel_format = PIXEL_FORMAT_UNKNOWN;

	switch (surface_pixel_format) {
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		dal_pixel_format = PIXEL_FORMAT_INDEX8;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
		dal_pixel_format = PIXEL_FORMAT_RGB565;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
		dal_pixel_format = PIXEL_FORMAT_ARGB8888;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
		dal_pixel_format = PIXEL_FORMAT_ARGB2101010_XRBIAS;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
		dal_pixel_format = PIXEL_FORMAT_FP16;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		dal_pixel_format = PIXEL_FORMAT_420BPP8;
		break;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		dal_pixel_format = PIXEL_FORMAT_420BPP10;
		break;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	default:
		dal_pixel_format = PIXEL_FORMAT_UNKNOWN;
		break;
	}
	return dal_pixel_format;
}

static inline void get_vp_scan_direction(
	enum dc_rotation_angle rotation,
	bool horizontal_mirror,
	bool *orthogonal_rotation,
	bool *flip_vert_scan_dir,
	bool *flip_horz_scan_dir)
{
	*orthogonal_rotation = false;
	*flip_vert_scan_dir = false;
	*flip_horz_scan_dir = false;
	if (rotation == ROTATION_ANGLE_180) {
		*flip_vert_scan_dir = true;
		*flip_horz_scan_dir = true;
	} else if (rotation == ROTATION_ANGLE_90) {
		*orthogonal_rotation = true;
		*flip_horz_scan_dir = true;
	} else if (rotation == ROTATION_ANGLE_270) {
		*orthogonal_rotation = true;
		*flip_vert_scan_dir = true;
	}

	if (horizontal_mirror)
		*flip_horz_scan_dir = !*flip_horz_scan_dir;
}

static struct rect intersect_rec(const struct rect *r0, const struct rect *r1)
{
	struct rect rec;
	int r0_x_end = r0->x + r0->width;
	int r1_x_end = r1->x + r1->width;
	int r0_y_end = r0->y + r0->height;
	int r1_y_end = r1->y + r1->height;

	rec.x = r0->x > r1->x ? r0->x : r1->x;
	rec.width = r0_x_end > r1_x_end ? r1_x_end - rec.x : r0_x_end - rec.x;
	rec.y = r0->y > r1->y ? r0->y : r1->y;
	rec.height = r0_y_end > r1_y_end ? r1_y_end - rec.y : r0_y_end - rec.y;

	/* in case that there is no intersection */
	if (rec.width < 0 || rec.height < 0)
		memset(&rec, 0, sizeof(rec));

	return rec;
}

static struct rect shift_rec(const struct rect *rec_in, int x, int y)
{
	struct rect rec_out = *rec_in;

	rec_out.x += x;
	rec_out.y += y;

	return rec_out;
}

static struct rect calculate_plane_rec_in_timing_active(
		struct pipe_ctx *pipe_ctx,
		const struct rect *rec_in)
{
	/*
	 * The following diagram shows an example where we map a 1920x1200
	 * desktop to a 2560x1440 timing with a plane rect in the middle
	 * of the screen. To map a plane rect from Stream Source to Timing
	 * Active space, we first multiply stream scaling ratios (i.e 2304/1920
	 * horizontal and 1440/1200 vertical) to the plane's x and y, then
	 * we add stream destination offsets (i.e 128 horizontal, 0 vertical).
	 * This will give us a plane rect's position in Timing Active. However
	 * we have to remove the fractional. The rule is that we find left/right
	 * and top/bottom positions and round the value to the adjacent integer.
	 *
	 * Stream Source Space
	 * ------------
	 *        __________________________________________________
	 *       |Stream Source (1920 x 1200) ^                     |
	 *       |                            y                     |
	 *       |         <------- w --------|>                    |
	 *       |          __________________V                     |
	 *       |<-- x -->|Plane//////////////| ^                  |
	 *       |         |(pre scale)////////| |                  |
	 *       |         |///////////////////| |                  |
	 *       |         |///////////////////| h                  |
	 *       |         |///////////////////| |                  |
	 *       |         |///////////////////| |                  |
	 *       |         |///////////////////| V                  |
	 *       |                                                  |
	 *       |                                                  |
	 *       |__________________________________________________|
	 *
	 *
	 * Timing Active Space
	 * ---------------------------------
	 *
	 *       Timing Active (2560 x 1440)
	 *        __________________________________________________
	 *       |*****|  Stteam Destination (2304 x 1440)    |*****|
	 *       |*****|                                      |*****|
	 *       |<128>|                                      |*****|
	 *       |*****|     __________________               |*****|
	 *       |*****|    |Plane/////////////|              |*****|
	 *       |*****|    |(post scale)//////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|    |//////////////////|              |*****|
	 *       |*****|                                      |*****|
	 *       |*****|                                      |*****|
	 *       |*****|                                      |*****|
	 *       |*****|______________________________________|*****|
	 *
	 * So the resulting formulas are shown below:
	 *
	 * recout_x = 128 + round(plane_x * 2304 / 1920)
	 * recout_w = 128 + round((plane_x + plane_w) * 2304 / 1920) - recout_x
	 * recout_y = 0 + round(plane_y * 1440 / 1280)
	 * recout_h = 0 + round((plane_y + plane_h) * 1440 / 1200) - recout_y
	 *
	 * NOTE: fixed point division is not error free. To reduce errors
	 * introduced by fixed point division, we divide only after
	 * multiplication is complete.
	 */
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct rect rec_out = {0};
	struct fixed31_32 temp;

	temp = dc_fixpt_from_fraction(rec_in->x * (long long)stream->dst.width,
			stream->src.width);
	rec_out.x = stream->dst.x + dc_fixpt_round(temp);

	temp = dc_fixpt_from_fraction(
			(rec_in->x + rec_in->width) * (long long)stream->dst.width,
			stream->src.width);
	rec_out.width = stream->dst.x + dc_fixpt_round(temp) - rec_out.x;

	temp = dc_fixpt_from_fraction(rec_in->y * (long long)stream->dst.height,
			stream->src.height);
	rec_out.y = stream->dst.y + dc_fixpt_round(temp);

	temp = dc_fixpt_from_fraction(
			(rec_in->y + rec_in->height) * (long long)stream->dst.height,
			stream->src.height);
	rec_out.height = stream->dst.y + dc_fixpt_round(temp) - rec_out.y;

	return rec_out;
}

static struct rect calculate_mpc_slice_in_timing_active(
		struct pipe_ctx *pipe_ctx,
		struct rect *plane_clip_rec)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
	int mpc_slice_count = resource_get_mpc_slice_count(pipe_ctx);
	int mpc_slice_idx = resource_get_mpc_slice_index(pipe_ctx);
	int epimo = mpc_slice_count - plane_clip_rec->width % mpc_slice_count - 1;
	struct rect mpc_rec;

	mpc_rec.width = plane_clip_rec->width / mpc_slice_count;
	mpc_rec.x = plane_clip_rec->x + mpc_rec.width * mpc_slice_idx;
	mpc_rec.height = plane_clip_rec->height;
	mpc_rec.y = plane_clip_rec->y;
	ASSERT(mpc_slice_count == 1 ||
			stream->view_format != VIEW_3D_FORMAT_SIDE_BY_SIDE ||
			mpc_rec.width % 2 == 0);

	if (stream->view_format == VIEW_3D_FORMAT_SIDE_BY_SIDE)
		mpc_rec.x -= (mpc_rec.width * mpc_slice_idx);

	/* extra pixels in the division remainder need to go to pipes after
	 * the extra pixel index minus one(epimo) defined here as:
	 */
	if (mpc_slice_idx > epimo) {
		mpc_rec.x += mpc_slice_idx - epimo - 1;
		mpc_rec.width += 1;
	}

	if (stream->view_format == VIEW_3D_FORMAT_TOP_AND_BOTTOM) {
		ASSERT(mpc_rec.height % 2 == 0);
		mpc_rec.height /= 2;
	}
	return mpc_rec;
}

static void calculate_adjust_recout_for_visual_confirm(struct pipe_ctx *pipe_ctx,
	int *base_offset, int *dpp_offset)
{
	struct dc *dc = pipe_ctx->stream->ctx->dc;
	*base_offset = 0;
	*dpp_offset = 0;

	if (dc->debug.visual_confirm == VISUAL_CONFIRM_DISABLE || !pipe_ctx->plane_res.dpp)
		return;

	*dpp_offset = pipe_ctx->stream->timing.v_addressable / VISUAL_CONFIRM_DPP_OFFSET_DENO;
	*dpp_offset *= pipe_ctx->plane_res.dpp->inst;

	if ((dc->debug.visual_confirm_rect_height >= VISUAL_CONFIRM_BASE_MIN) &&
			dc->debug.visual_confirm_rect_height <= VISUAL_CONFIRM_BASE_MAX)
		*base_offset = dc->debug.visual_confirm_rect_height;
	else
		*base_offset = VISUAL_CONFIRM_BASE_DEFAULT;
}

static void adjust_recout_for_visual_confirm(struct rect *recout,
		struct pipe_ctx *pipe_ctx)
{
	int dpp_offset, base_offset;

	calculate_adjust_recout_for_visual_confirm(pipe_ctx, &base_offset,
		&dpp_offset);
	recout->height -= base_offset;
	recout->height -= dpp_offset;
}

/*
 * The function maps a plane clip from Stream Source Space to ODM Slice Space
 * and calculates the rec of the overlapping area of MPC slice of the plane
 * clip, ODM slice associated with the pipe context and stream destination rec.
 */
static void calculate_recout(struct pipe_ctx *pipe_ctx)
{
	/*
	 * A plane clip represents the desired plane size and position in Stream
	 * Source Space. Stream Source is the destination where all planes are
	 * blended (i.e. positioned, scaled and overlaid). It is a canvas where
	 * all planes associated with the current stream are drawn together.
	 * After Stream Source is completed, we will further scale and
	 * reposition the entire canvas of the stream source to Stream
	 * Destination in Timing Active Space. This could be due to display
	 * overscan adjustment where we will need to rescale and reposition all
	 * the planes so they can fit into a TV with overscan or downscale
	 * upscale features such as GPU scaling or VSR.
	 *
	 * This two step blending is a virtual procedure in software. In
	 * hardware there is no such thing as Stream Source. all planes are
	 * blended once in Timing Active Space. Software virtualizes a Stream
	 * Source space to decouple the math complicity so scaling param
	 * calculation focuses on one step at a time.
	 *
	 * In the following two diagrams, user applied 10% overscan adjustment
	 * so the Stream Source needs to be scaled down a little before mapping
	 * to Timing Active Space. As a result the Plane Clip is also scaled
	 * down by the same ratio, Plane Clip position (i.e. x and y) with
	 * respect to Stream Source is also scaled down. To map it in Timing
	 * Active Space additional x and y offsets from Stream Destination are
	 * added to Plane Clip as well.
	 *
	 * Stream Source Space
	 * ------------
	 *        __________________________________________________
	 *       |Stream Source (3840 x 2160) ^                     |
	 *       |                            y                     |
	 *       |                            |                     |
	 *       |          __________________V                     |
	 *       |<-- x -->|Plane Clip/////////|                    |
	 *       |         |(pre scale)////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |         |///////////////////|                    |
	 *       |                                                  |
	 *       |                                                  |
	 *       |__________________________________________________|
	 *
	 *
	 * Timing Active Space (3840 x 2160)
	 * ---------------------------------
	 *
	 *       Timing Active
	 *        __________________________________________________
	 *       | y_____________________________________________   |
	 *       |x |Stream Destination (3456 x 1944)            |  |
	 *       |  |                                            |  |
	 *       |  |        __________________                  |  |
	 *       |  |       |Plane Clip////////|                 |  |
	 *       |  |       |(post scale)//////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |       |//////////////////|                 |  |
	 *       |  |                                            |  |
	 *       |  |                                            |  |
	 *       |  |____________________________________________|  |
	 *       |__________________________________________________|
	 *
	 *
	 * In Timing Active Space a plane clip could be further sliced into
	 * pieces called MPC slices. Each Pipe Context is responsible for
	 * processing only one MPC slice so the plane processing workload can be
	 * distributed to multiple DPP Pipes. MPC slices could be blended
	 * together to a single ODM slice. Each ODM slice is responsible for
	 * processing a portion of Timing Active divided horizontally so the
	 * output pixel processing workload can be distributed to multiple OPP
	 * pipes. All ODM slices are mapped together in ODM block so all MPC
	 * slices belong to different ODM slices could be pieced together to
	 * form a single image in Timing Active. MPC slices must belong to
	 * single ODM slice. If an MPC slice goes across ODM slice boundary, it
	 * needs to be divided into two MPC slices one for each ODM slice.
	 *
	 * In the following diagram the output pixel processing workload is
	 * divided horizontally into two ODM slices one for each OPP blend tree.
	 * OPP0 blend tree is responsible for processing left half of Timing
	 * Active, while OPP2 blend tree is responsible for processing right
	 * half.
	 *
	 * The plane has two MPC slices. However since the right MPC slice goes
	 * across ODM boundary, two DPP pipes are needed one for each OPP blend
	 * tree. (i.e. DPP1 for OPP0 blend tree and DPP2 for OPP2 blend tree).
	 *
	 * Assuming that we have a Pipe Context associated with OPP0 and DPP1
	 * working on processing the plane in the diagram. We want to know the
	 * width and height of the shaded rectangle and its relative position
	 * with respect to the ODM slice0. This is called the recout of the pipe
	 * context.
	 *
	 * Planes can be at arbitrary size and position and there could be an
	 * arbitrary number of MPC and ODM slices. The algorithm needs to take
	 * all scenarios into account.
	 *
	 * Timing Active Space (3840 x 2160)
	 * ---------------------------------
	 *
	 *       Timing Active
	 *        __________________________________________________
	 *       |OPP0(ODM slice0)^        |OPP2(ODM slice1)        |
	 *       |                y        |                        |
	 *       |                |  <- w ->                        |
	 *       |           _____V________|____                    |
	 *       |          |DPP0 ^  |DPP1 |DPP2|                   |
	 *       |<------ x |-----|->|/////|    |                   |
	 *       |          |     |  |/////|    |                   |
	 *       |          |     h  |/////|    |                   |
	 *       |          |     |  |/////|    |                   |
	 *       |          |_____V__|/////|____|                   |
	 *       |                         |                        |
	 *       |                         |                        |
	 *       |                         |                        |
	 *       |_________________________|________________________|
	 *
	 *
	 */
	struct rect plane_clip;
	struct rect mpc_slice_of_plane_clip;
	struct rect odm_slice_src;
	struct rect overlapping_area;

	plane_clip = calculate_plane_rec_in_timing_active(pipe_ctx,
			&pipe_ctx->plane_state->clip_rect);
	/* guard plane clip from drawing beyond stream dst here */
	plane_clip = intersect_rec(&plane_clip,
				&pipe_ctx->stream->dst);
	mpc_slice_of_plane_clip = calculate_mpc_slice_in_timing_active(
			pipe_ctx, &plane_clip);
	odm_slice_src = resource_get_odm_slice_src_rect(pipe_ctx);
	overlapping_area = intersect_rec(&mpc_slice_of_plane_clip, &odm_slice_src);
	if (overlapping_area.height > 0 &&
			overlapping_area.width > 0) {
		/* shift the overlapping area so it is with respect to current
		 * ODM slice source's position
		 */
		pipe_ctx->plane_res.scl_data.recout = shift_rec(
				&overlapping_area,
				-odm_slice_src.x, -odm_slice_src.y);
		adjust_recout_for_visual_confirm(
				&pipe_ctx->plane_res.scl_data.recout,
				pipe_ctx);
	} else {
		/* if there is no overlap, zero recout */
		memset(&pipe_ctx->plane_res.scl_data.recout, 0,
				sizeof(struct rect));
	}

}

static void calculate_scaling_ratios(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct rect surf_src = plane_state->src_rect;
	const int in_w = stream->src.width;
	const int in_h = stream->src.height;
	const int out_w = stream->dst.width;
	const int out_h = stream->dst.height;

	/*Swap surf_src height and width since scaling ratios are in recout rotation*/
	if (pipe_ctx->plane_state->rotation == ROTATION_ANGLE_90 ||
			pipe_ctx->plane_state->rotation == ROTATION_ANGLE_270)
		swap(surf_src.height, surf_src.width);

	pipe_ctx->plane_res.scl_data.ratios.horz = dc_fixpt_from_fraction(
					surf_src.width,
					plane_state->dst_rect.width);
	pipe_ctx->plane_res.scl_data.ratios.vert = dc_fixpt_from_fraction(
					surf_src.height,
					plane_state->dst_rect.height);

	if (stream->view_format == VIEW_3D_FORMAT_SIDE_BY_SIDE)
		pipe_ctx->plane_res.scl_data.ratios.horz.value *= 2;
	else if (stream->view_format == VIEW_3D_FORMAT_TOP_AND_BOTTOM)
		pipe_ctx->plane_res.scl_data.ratios.vert.value *= 2;

	pipe_ctx->plane_res.scl_data.ratios.vert.value = div64_s64(
		pipe_ctx->plane_res.scl_data.ratios.vert.value * in_h, out_h);
	pipe_ctx->plane_res.scl_data.ratios.horz.value = div64_s64(
		pipe_ctx->plane_res.scl_data.ratios.horz.value * in_w, out_w);

	pipe_ctx->plane_res.scl_data.ratios.horz_c = pipe_ctx->plane_res.scl_data.ratios.horz;
	pipe_ctx->plane_res.scl_data.ratios.vert_c = pipe_ctx->plane_res.scl_data.ratios.vert;

	if (pipe_ctx->plane_res.scl_data.format == PIXEL_FORMAT_420BPP8
			|| pipe_ctx->plane_res.scl_data.format == PIXEL_FORMAT_420BPP10) {
		pipe_ctx->plane_res.scl_data.ratios.horz_c.value /= 2;
		pipe_ctx->plane_res.scl_data.ratios.vert_c.value /= 2;
	}
	pipe_ctx->plane_res.scl_data.ratios.horz = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.horz, 19);
	pipe_ctx->plane_res.scl_data.ratios.vert = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.vert, 19);
	pipe_ctx->plane_res.scl_data.ratios.horz_c = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.horz_c, 19);
	pipe_ctx->plane_res.scl_data.ratios.vert_c = dc_fixpt_truncate(
			pipe_ctx->plane_res.scl_data.ratios.vert_c, 19);
}


/*
 * We completely calculate vp offset, size and inits here based entirely on scaling
 * ratios and recout for pixel perfect pipe combine.
 */
static void calculate_init_and_vp(
		bool flip_scan_dir,
		int recout_offset_within_recout_full,
		int recout_size,
		int src_size,
		int taps,
		struct fixed31_32 ratio,
		struct fixed31_32 *init,
		int *vp_offset,
		int *vp_size)
{
	struct fixed31_32 temp;
	int int_part;

	/*
	 * First of the taps starts sampling pixel number <init_int_part> corresponding to recout
	 * pixel 1. Next recout pixel samples int part of <init + scaling ratio> and so on.
	 * All following calculations are based on this logic.
	 *
	 * Init calculated according to formula:
	 * 	init = (scaling_ratio + number_of_taps + 1) / 2
	 * 	init_bot = init + scaling_ratio
	 * 	to get pixel perfect combine add the fraction from calculating vp offset
	 */
	temp = dc_fixpt_mul_int(ratio, recout_offset_within_recout_full);
	*vp_offset = dc_fixpt_floor(temp);
	temp.value &= 0xffffffff;
	*init = dc_fixpt_truncate(dc_fixpt_add(dc_fixpt_div_int(
			dc_fixpt_add_int(ratio, taps + 1), 2), temp), 19);
	/*
	 * If viewport has non 0 offset and there are more taps than covered by init then
	 * we should decrease the offset and increase init so we are never sampling
	 * outside of viewport.
	 */
	int_part = dc_fixpt_floor(*init);
	if (int_part < taps) {
		int_part = taps - int_part;
		if (int_part > *vp_offset)
			int_part = *vp_offset;
		*vp_offset -= int_part;
		*init = dc_fixpt_add_int(*init, int_part);
	}
	/*
	 * If taps are sampling outside of viewport at end of recout and there are more pixels
	 * available in the surface we should increase the viewport size, regardless set vp to
	 * only what is used.
	 */
	temp = dc_fixpt_add(*init, dc_fixpt_mul_int(ratio, recout_size - 1));
	*vp_size = dc_fixpt_floor(temp);
	if (*vp_size + *vp_offset > src_size)
		*vp_size = src_size - *vp_offset;

	/* We did all the math assuming we are scanning same direction as display does,
	 * however mirror/rotation changes how vp scans vs how it is offset. If scan direction
	 * is flipped we simply need to calculate offset from the other side of plane.
	 * Note that outside of viewport all scaling hardware works in recout space.
	 */
	if (flip_scan_dir)
		*vp_offset = src_size - *vp_offset - *vp_size;
}

static void calculate_inits_and_viewports(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct scaler_data *data = &pipe_ctx->plane_res.scl_data;
	struct rect src = plane_state->src_rect;
	struct rect recout_dst_in_active_timing;
	struct rect recout_clip_in_active_timing;
	struct rect recout_clip_in_recout_dst;
	struct rect overlap_in_active_timing;
	struct rect odm_slice_src = resource_get_odm_slice_src_rect(pipe_ctx);
	int vpc_div = (data->format == PIXEL_FORMAT_420BPP8
				|| data->format == PIXEL_FORMAT_420BPP10) ? 2 : 1;
	bool orthogonal_rotation, flip_vert_scan_dir, flip_horz_scan_dir;

	recout_clip_in_active_timing = shift_rec(
			&data->recout, odm_slice_src.x, odm_slice_src.y);
	recout_dst_in_active_timing = calculate_plane_rec_in_timing_active(
			pipe_ctx, &plane_state->dst_rect);
	overlap_in_active_timing = intersect_rec(&recout_clip_in_active_timing,
			&recout_dst_in_active_timing);
	if (overlap_in_active_timing.width > 0 &&
			overlap_in_active_timing.height > 0)
		recout_clip_in_recout_dst = shift_rec(&overlap_in_active_timing,
				-recout_dst_in_active_timing.x,
				-recout_dst_in_active_timing.y);
	else
		memset(&recout_clip_in_recout_dst, 0, sizeof(struct rect));

	/*
	 * Work in recout rotation since that requires less transformations
	 */
	get_vp_scan_direction(
			plane_state->rotation,
			plane_state->horizontal_mirror,
			&orthogonal_rotation,
			&flip_vert_scan_dir,
			&flip_horz_scan_dir);

	if (orthogonal_rotation) {
		swap(src.width, src.height);
		swap(flip_vert_scan_dir, flip_horz_scan_dir);
	}

	calculate_init_and_vp(
			flip_horz_scan_dir,
			recout_clip_in_recout_dst.x,
			data->recout.width,
			src.width,
			data->taps.h_taps,
			data->ratios.horz,
			&data->inits.h,
			&data->viewport.x,
			&data->viewport.width);
	calculate_init_and_vp(
			flip_horz_scan_dir,
			recout_clip_in_recout_dst.x,
			data->recout.width,
			src.width / vpc_div,
			data->taps.h_taps_c,
			data->ratios.horz_c,
			&data->inits.h_c,
			&data->viewport_c.x,
			&data->viewport_c.width);
	calculate_init_and_vp(
			flip_vert_scan_dir,
			recout_clip_in_recout_dst.y,
			data->recout.height,
			src.height,
			data->taps.v_taps,
			data->ratios.vert,
			&data->inits.v,
			&data->viewport.y,
			&data->viewport.height);
	calculate_init_and_vp(
			flip_vert_scan_dir,
			recout_clip_in_recout_dst.y,
			data->recout.height,
			src.height / vpc_div,
			data->taps.v_taps_c,
			data->ratios.vert_c,
			&data->inits.v_c,
			&data->viewport_c.y,
			&data->viewport_c.height);
	if (orthogonal_rotation) {
		swap(data->viewport.x, data->viewport.y);
		swap(data->viewport.width, data->viewport.height);
		swap(data->viewport_c.x, data->viewport_c.y);
		swap(data->viewport_c.width, data->viewport_c.height);
	}
	data->viewport.x += src.x;
	data->viewport.y += src.y;
	ASSERT(src.x % vpc_div == 0 && src.y % vpc_div == 0);
	data->viewport_c.x += src.x / vpc_div;
	data->viewport_c.y += src.y / vpc_div;
}

static bool is_subvp_high_refresh_candidate(struct dc_stream_state *stream)
{
	uint32_t refresh_rate;
	struct dc *dc = stream->ctx->dc;

	refresh_rate = (stream->timing.pix_clk_100hz * (uint64_t)100 +
		stream->timing.v_total * stream->timing.h_total - (uint64_t)1);
	refresh_rate = div_u64(refresh_rate, stream->timing.v_total);
	refresh_rate = div_u64(refresh_rate, stream->timing.h_total);

	/* If there's any stream that fits the SubVP high refresh criteria,
	 * we must return true. This is because cursor updates are asynchronous
	 * with full updates, so we could transition into a SubVP config and
	 * remain in HW cursor mode if there's no cursor update which will
	 * then cause corruption.
	 */
	if ((refresh_rate >= 120 && refresh_rate <= 175 &&
			stream->timing.v_addressable >= 1080 &&
			stream->timing.v_addressable <= 2160) &&
			(dc->current_state->stream_count > 1 ||
			(dc->current_state->stream_count == 1 && !stream->allow_freesync)))
		return true;

	return false;
}

static enum controller_dp_test_pattern convert_dp_to_controller_test_pattern(
				enum dp_test_pattern test_pattern)
{
	enum controller_dp_test_pattern controller_test_pattern;

	switch (test_pattern) {
	case DP_TEST_PATTERN_COLOR_SQUARES:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_COLORSQUARES;
	break;
	case DP_TEST_PATTERN_COLOR_SQUARES_CEA:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_COLORSQUARES_CEA;
	break;
	case DP_TEST_PATTERN_VERTICAL_BARS:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_VERTICALBARS;
	break;
	case DP_TEST_PATTERN_HORIZONTAL_BARS:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_HORIZONTALBARS;
	break;
	case DP_TEST_PATTERN_COLOR_RAMP:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_COLORRAMP;
	break;
	default:
		controller_test_pattern =
				CONTROLLER_DP_TEST_PATTERN_VIDEOMODE;
	break;
	}

	return controller_test_pattern;
}

static enum controller_dp_color_space convert_dp_to_controller_color_space(
		enum dp_test_pattern_color_space color_space)
{
	enum controller_dp_color_space controller_color_space;

	switch (color_space) {
	case DP_TEST_PATTERN_COLOR_SPACE_RGB:
		controller_color_space = CONTROLLER_DP_COLOR_SPACE_RGB;
		break;
	case DP_TEST_PATTERN_COLOR_SPACE_YCBCR601:
		controller_color_space = CONTROLLER_DP_COLOR_SPACE_YCBCR601;
		break;
	case DP_TEST_PATTERN_COLOR_SPACE_YCBCR709:
		controller_color_space = CONTROLLER_DP_COLOR_SPACE_YCBCR709;
		break;
	case DP_TEST_PATTERN_COLOR_SPACE_UNDEFINED:
	default:
		controller_color_space = CONTROLLER_DP_COLOR_SPACE_UDEFINED;
		break;
	}

	return controller_color_space;
}

void resource_build_test_pattern_params(struct resource_context *res_ctx,
				struct pipe_ctx *otg_master)
{
	struct pipe_ctx *opp_heads[MAX_PIPES];
	struct test_pattern_params *params;
	int odm_cnt;
	enum controller_dp_test_pattern controller_test_pattern;
	enum controller_dp_color_space controller_color_space;
	enum dc_color_depth color_depth = otg_master->stream->timing.display_color_depth;
	struct rect odm_slice_src;
	int i;

	controller_test_pattern = convert_dp_to_controller_test_pattern(
			otg_master->stream->test_pattern.type);
	controller_color_space = convert_dp_to_controller_color_space(
			otg_master->stream->test_pattern.color_space);

	if (controller_test_pattern == CONTROLLER_DP_TEST_PATTERN_VIDEOMODE)
		return;

	odm_cnt = resource_get_opp_heads_for_otg_master(otg_master, res_ctx, opp_heads);

	for (i = 0; i < odm_cnt; i++) {
		odm_slice_src = resource_get_odm_slice_src_rect(opp_heads[i]);
		params = &opp_heads[i]->stream_res.test_pattern_params;
		params->test_pattern = controller_test_pattern;
		params->color_space = controller_color_space;
		params->color_depth = color_depth;
		params->height = odm_slice_src.height;
		params->offset = odm_slice_src.x;
		params->width = odm_slice_src.width;
	}
}

bool resource_build_scaling_params(struct pipe_ctx *pipe_ctx)
{
	const struct dc_plane_state *plane_state = pipe_ctx->plane_state;
	struct dc_crtc_timing *timing = &pipe_ctx->stream->timing;
	const struct rect odm_slice_src = resource_get_odm_slice_src_rect(pipe_ctx);
	struct scaling_taps temp = {0};
	bool res = false;

	DC_LOGGER_INIT(pipe_ctx->stream->ctx->logger);

	/* Invalid input */
	if (!plane_state ||
			!plane_state->dst_rect.width ||
			!plane_state->dst_rect.height ||
			!plane_state->src_rect.width ||
			!plane_state->src_rect.height) {
		ASSERT(0);
		return false;
	}

	/* Timing borders are part of vactive that we are also supposed to skip in addition
	 * to any stream dst offset. Since dm logic assumes dst is in addressable
	 * space we need to add the left and top borders to dst offsets temporarily.
	 * TODO: fix in DM, stream dst is supposed to be in vactive
	 */
	pipe_ctx->stream->dst.x += timing->h_border_left;
	pipe_ctx->stream->dst.y += timing->v_border_top;

	/* Calculate H and V active size */
	pipe_ctx->plane_res.scl_data.h_active = odm_slice_src.width;
	pipe_ctx->plane_res.scl_data.v_active = odm_slice_src.height;
	pipe_ctx->plane_res.scl_data.format = convert_pixel_format_to_dalsurface(
			pipe_ctx->plane_state->format);

#if defined(CONFIG_DRM_AMD_DC_FP)
	if ((pipe_ctx->stream->ctx->dc->config.use_spl)	&& (!pipe_ctx->stream->ctx->dc->debug.disable_spl)) {
		struct spl_in *spl_in = &pipe_ctx->plane_res.spl_in;
		struct spl_out *spl_out = &pipe_ctx->plane_res.spl_out;

		if (plane_state->ctx->dce_version > DCE_VERSION_MAX)
			pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_36BPP;
		else
			pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_30BPP;

		pipe_ctx->plane_res.scl_data.lb_params.alpha_en = plane_state->per_pixel_alpha;

		// Convert pipe_ctx to respective input params for SPL
		translate_SPL_in_params_from_pipe_ctx(pipe_ctx, spl_in);
		/* Pass visual confirm debug information */
		calculate_adjust_recout_for_visual_confirm(pipe_ctx,
			&spl_in->debug.visual_confirm_base_offset,
			&spl_in->debug.visual_confirm_dpp_offset);
		// Set SPL output parameters to dscl_prog_data to be used for hw registers
		spl_out->dscl_prog_data = resource_get_dscl_prog_data(pipe_ctx);
		// Calculate scaler parameters from SPL
		res = spl_calculate_scaler_params(spl_in, spl_out);
		// Convert respective out params from SPL to scaler data
		translate_SPL_out_params_to_pipe_ctx(pipe_ctx, spl_out);

		/* Ignore scaler failure if pipe context plane is phantom plane */
		if (!res && plane_state->is_phantom)
			res = true;
	} else {
#endif
	/* depends on h_active */
	calculate_recout(pipe_ctx);
	/* depends on pixel format */
	calculate_scaling_ratios(pipe_ctx);

	/*
	 * LB calculations depend on vp size, h/v_active and scaling ratios
	 * Setting line buffer pixel depth to 24bpp yields banding
	 * on certain displays, such as the Sharp 4k. 36bpp is needed
	 * to support SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616 and
	 * SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616 with actual > 10 bpc
	 * precision on DCN display engines, but apparently not for DCE, as
	 * far as testing on DCE-11.2 and DCE-8 showed. Various DCE parts have
	 * problems: Carrizo with DCE_VERSION_11_0 does not like 36 bpp lb depth,
	 * neither do DCE-8 at 4k resolution, or DCE-11.2 (broken identify pixel
	 * passthrough). Therefore only use 36 bpp on DCN where it is actually needed.
	 */
	if (plane_state->ctx->dce_version > DCE_VERSION_MAX)
		pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_36BPP;
	else
		pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_30BPP;

	pipe_ctx->plane_res.scl_data.lb_params.alpha_en = plane_state->per_pixel_alpha;

	// get TAP value with 100x100 dummy data for max scaling qualify, override
	// if a new scaling quality required
	pipe_ctx->plane_res.scl_data.viewport.width = 100;
	pipe_ctx->plane_res.scl_data.viewport.height = 100;
	pipe_ctx->plane_res.scl_data.viewport_c.width = 100;
	pipe_ctx->plane_res.scl_data.viewport_c.height = 100;
	if (pipe_ctx->plane_res.xfm != NULL)
		res = pipe_ctx->plane_res.xfm->funcs->transform_get_optimal_number_of_taps(
				pipe_ctx->plane_res.xfm, &pipe_ctx->plane_res.scl_data, &plane_state->scaling_quality);

	if (pipe_ctx->plane_res.dpp != NULL)
		res = pipe_ctx->plane_res.dpp->funcs->dpp_get_optimal_number_of_taps(
				pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data, &plane_state->scaling_quality);

	temp = pipe_ctx->plane_res.scl_data.taps;

	calculate_inits_and_viewports(pipe_ctx);

	if (pipe_ctx->plane_res.xfm != NULL)
		res = pipe_ctx->plane_res.xfm->funcs->transform_get_optimal_number_of_taps(
				pipe_ctx->plane_res.xfm, &pipe_ctx->plane_res.scl_data, &plane_state->scaling_quality);

	if (pipe_ctx->plane_res.dpp != NULL)
		res = pipe_ctx->plane_res.dpp->funcs->dpp_get_optimal_number_of_taps(
				pipe_ctx->plane_res.dpp, &pipe_ctx->plane_res.scl_data, &plane_state->scaling_quality);


	if (!res) {
		/* Try 24 bpp linebuffer */
		pipe_ctx->plane_res.scl_data.lb_params.depth = LB_PIXEL_DEPTH_24BPP;

		if (pipe_ctx->plane_res.xfm != NULL)
			res = pipe_ctx->plane_res.xfm->funcs->transform_get_optimal_number_of_taps(
					pipe_ctx->plane_res.xfm,
					&pipe_ctx->plane_res.scl_data,
					&plane_state->scaling_quality);

		if (pipe_ctx->plane_res.dpp != NULL)
			res = pipe_ctx->plane_res.dpp->funcs->dpp_get_optimal_number_of_taps(
					pipe_ctx->plane_res.dpp,
					&pipe_ctx->plane_res.scl_data,
					&plane_state->scaling_quality);
	}

	/* Ignore scaler failure if pipe context plane is phantom plane */
	if (!res && plane_state->is_phantom)
		res = true;

	if (res && (pipe_ctx->plane_res.scl_data.taps.v_taps != temp.v_taps ||
		pipe_ctx->plane_res.scl_data.taps.h_taps != temp.h_taps ||
		pipe_ctx->plane_res.scl_data.taps.v_taps_c != temp.v_taps_c ||
		pipe_ctx->plane_res.scl_data.taps.h_taps_c != temp.h_taps_c))
		calculate_inits_and_viewports(pipe_ctx);

	/*
	 * Handle side by side and top bottom 3d recout offsets after vp calculation
	 * since 3d is special and needs to calculate vp as if there is no recout offset
	 * This may break with rotation, good thing we aren't mixing hw rotation and 3d
	 */
	if (pipe_ctx->top_pipe && pipe_ctx->top_pipe->plane_state == plane_state) {
		ASSERT(plane_state->rotation == ROTATION_ANGLE_0 ||
			(pipe_ctx->stream->view_format != VIEW_3D_FORMAT_TOP_AND_BOTTOM &&
				pipe_ctx->stream->view_format != VIEW_3D_FORMAT_SIDE_BY_SIDE));
		if (pipe_ctx->stream->view_format == VIEW_3D_FORMAT_TOP_AND_BOTTOM)
			pipe_ctx->plane_res.scl_data.recout.y += pipe_ctx->plane_res.scl_data.recout.height;
		else if (pipe_ctx->stream->view_format == VIEW_3D_FORMAT_SIDE_BY_SIDE)
			pipe_ctx->plane_res.scl_data.recout.x += pipe_ctx->plane_res.scl_data.recout.width;
	}

	/* Clamp minimum viewport size */
	if (pipe_ctx->plane_res.scl_data.viewport.height < MIN_VIEWPORT_SIZE)
		pipe_ctx->plane_res.scl_data.viewport.height = MIN_VIEWPORT_SIZE;
	if (pipe_ctx->plane_res.scl_data.viewport.width < MIN_VIEWPORT_SIZE)
		pipe_ctx->plane_res.scl_data.viewport.width = MIN_VIEWPORT_SIZE;
#ifdef CONFIG_DRM_AMD_DC_FP
	}
#endif
	DC_LOG_SCALER("%s pipe %d:\nViewport: height:%d width:%d x:%d y:%d  Recout: height:%d width:%d x:%d y:%d  HACTIVE:%d VACTIVE:%d\n"
			"src_rect: height:%d width:%d x:%d y:%d  dst_rect: height:%d width:%d x:%d y:%d  clip_rect: height:%d width:%d x:%d y:%d\n",
			__func__,
			pipe_ctx->pipe_idx,
			pipe_ctx->plane_res.scl_data.viewport.height,
			pipe_ctx->plane_res.scl_data.viewport.width,
			pipe_ctx->plane_res.scl_data.viewport.x,
			pipe_ctx->plane_res.scl_data.viewport.y,
			pipe_ctx->plane_res.scl_data.recout.height,
			pipe_ctx->plane_res.scl_data.recout.width,
			pipe_ctx->plane_res.scl_data.recout.x,
			pipe_ctx->plane_res.scl_data.recout.y,
			pipe_ctx->plane_res.scl_data.h_active,
			pipe_ctx->plane_res.scl_data.v_active,
			plane_state->src_rect.height,
			plane_state->src_rect.width,
			plane_state->src_rect.x,
			plane_state->src_rect.y,
			plane_state->dst_rect.height,
			plane_state->dst_rect.width,
			plane_state->dst_rect.x,
			plane_state->dst_rect.y,
			plane_state->clip_rect.height,
			plane_state->clip_rect.width,
			plane_state->clip_rect.x,
			plane_state->clip_rect.y);

	pipe_ctx->stream->dst.x -= timing->h_border_left;
	pipe_ctx->stream->dst.y -= timing->v_border_top;

	return res;
}


enum dc_status resource_build_scaling_params_for_context(
	const struct dc  *dc,
	struct dc_state *context)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (context->res_ctx.pipe_ctx[i].plane_state != NULL &&
				context->res_ctx.pipe_ctx[i].stream != NULL)
			if (!resource_build_scaling_params(&context->res_ctx.pipe_ctx[i]))
				return DC_FAIL_SCALING;
	}

	return DC_OK;
}

struct pipe_ctx *resource_find_free_secondary_pipe_legacy(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		const struct pipe_ctx *primary_pipe)
{
	int i;
	struct pipe_ctx *secondary_pipe = NULL;

	/*
	 * We add a preferred pipe mapping to avoid the chance that
	 * MPCCs already in use will need to be reassigned to other trees.
	 * For example, if we went with the strict, assign backwards logic:
	 *
	 * (State 1)
	 * Display A on, no surface, top pipe = 0
	 * Display B on, no surface, top pipe = 1
	 *
	 * (State 2)
	 * Display A on, no surface, top pipe = 0
	 * Display B on, surface enable, top pipe = 1, bottom pipe = 5
	 *
	 * (State 3)
	 * Display A on, surface enable, top pipe = 0, bottom pipe = 5
	 * Display B on, surface enable, top pipe = 1, bottom pipe = 4
	 *
	 * The state 2->3 transition requires remapping MPCC 5 from display B
	 * to display A.
	 *
	 * However, with the preferred pipe logic, state 2 would look like:
	 *
	 * (State 2)
	 * Display A on, no surface, top pipe = 0
	 * Display B on, surface enable, top pipe = 1, bottom pipe = 4
	 *
	 * This would then cause 2->3 to not require remapping any MPCCs.
	 */
	if (primary_pipe) {
		int preferred_pipe_idx = (pool->pipe_count - 1) - primary_pipe->pipe_idx;
		if (res_ctx->pipe_ctx[preferred_pipe_idx].stream == NULL) {
			secondary_pipe = &res_ctx->pipe_ctx[preferred_pipe_idx];
			secondary_pipe->pipe_idx = preferred_pipe_idx;
		}
	}

	/*
	 * search backwards for the second pipe to keep pipe
	 * assignment more consistent
	 */
	if (!secondary_pipe)
		for (i = pool->pipe_count - 1; i >= 0; i--) {
			if (res_ctx->pipe_ctx[i].stream == NULL) {
				secondary_pipe = &res_ctx->pipe_ctx[i];
				secondary_pipe->pipe_idx = i;
				break;
			}
		}

	return secondary_pipe;
}

int resource_find_free_pipe_used_as_sec_opp_head_by_cur_otg_master(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct pipe_ctx *cur_otg_master)
{
	const struct pipe_ctx *cur_sec_opp_head = cur_otg_master->next_odm_pipe;
	struct pipe_ctx *new_pipe;
	int free_pipe_idx = FREE_PIPE_INDEX_NOT_FOUND;

	while (cur_sec_opp_head) {
		new_pipe = &new_res_ctx->pipe_ctx[cur_sec_opp_head->pipe_idx];
		if (resource_is_pipe_type(new_pipe, FREE_PIPE)) {
			free_pipe_idx = cur_sec_opp_head->pipe_idx;
			break;
		}
		cur_sec_opp_head = cur_sec_opp_head->next_odm_pipe;
	}

	return free_pipe_idx;
}

int resource_find_free_pipe_used_in_cur_mpc_blending_tree(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct pipe_ctx *cur_opp_head)
{
	const struct pipe_ctx *cur_sec_dpp = cur_opp_head->bottom_pipe;
	struct pipe_ctx *new_pipe;
	int free_pipe_idx = FREE_PIPE_INDEX_NOT_FOUND;

	while (cur_sec_dpp) {
		/* find a free pipe used in current opp blend tree,
		 * this is to avoid MPO pipe switching to different opp blending
		 * tree
		 */
		new_pipe = &new_res_ctx->pipe_ctx[cur_sec_dpp->pipe_idx];
		if (resource_is_pipe_type(new_pipe, FREE_PIPE)) {
			free_pipe_idx = cur_sec_dpp->pipe_idx;
			break;
		}
		cur_sec_dpp = cur_sec_dpp->bottom_pipe;
	}

	return free_pipe_idx;
}

int recource_find_free_pipe_not_used_in_cur_res_ctx(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool)
{
	int free_pipe_idx = FREE_PIPE_INDEX_NOT_FOUND;
	const struct pipe_ctx *new_pipe, *cur_pipe;
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		cur_pipe = &cur_res_ctx->pipe_ctx[i];
		new_pipe = &new_res_ctx->pipe_ctx[i];

		if (resource_is_pipe_type(cur_pipe, FREE_PIPE) &&
				resource_is_pipe_type(new_pipe, FREE_PIPE)) {
			free_pipe_idx = i;
			break;
		}
	}

	return free_pipe_idx;
}

int recource_find_free_pipe_used_as_otg_master_in_cur_res_ctx(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool)
{
	int free_pipe_idx = FREE_PIPE_INDEX_NOT_FOUND;
	const struct pipe_ctx *new_pipe, *cur_pipe;
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		cur_pipe = &cur_res_ctx->pipe_ctx[i];
		new_pipe = &new_res_ctx->pipe_ctx[i];

		if (resource_is_pipe_type(cur_pipe, OTG_MASTER) &&
				resource_is_pipe_type(new_pipe, FREE_PIPE)) {
			free_pipe_idx = i;
			break;
		}
	}

	return free_pipe_idx;
}

int resource_find_free_pipe_used_as_cur_sec_dpp(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool)
{
	int free_pipe_idx = FREE_PIPE_INDEX_NOT_FOUND;
	const struct pipe_ctx *new_pipe, *cur_pipe;
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		cur_pipe = &cur_res_ctx->pipe_ctx[i];
		new_pipe = &new_res_ctx->pipe_ctx[i];

		if (resource_is_pipe_type(cur_pipe, DPP_PIPE) &&
				!resource_is_pipe_type(cur_pipe, OPP_HEAD) &&
				resource_is_pipe_type(new_pipe, FREE_PIPE)) {
			free_pipe_idx = i;
			break;
		}
	}

	return free_pipe_idx;
}

int resource_find_free_pipe_used_as_cur_sec_dpp_in_mpcc_combine(
		const struct resource_context *cur_res_ctx,
		struct resource_context *new_res_ctx,
		const struct resource_pool *pool)
{
	int free_pipe_idx = FREE_PIPE_INDEX_NOT_FOUND;
	const struct pipe_ctx *new_pipe, *cur_pipe;
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		cur_pipe = &cur_res_ctx->pipe_ctx[i];
		new_pipe = &new_res_ctx->pipe_ctx[i];

		if (resource_is_pipe_type(cur_pipe, DPP_PIPE) &&
				!resource_is_pipe_type(cur_pipe, OPP_HEAD) &&
				resource_get_mpc_slice_index(cur_pipe) > 0 &&
				resource_is_pipe_type(new_pipe, FREE_PIPE)) {
			free_pipe_idx = i;
			break;
		}
	}

	return free_pipe_idx;
}

int resource_find_any_free_pipe(struct resource_context *new_res_ctx,
		const struct resource_pool *pool)
{
	int free_pipe_idx = FREE_PIPE_INDEX_NOT_FOUND;
	const struct pipe_ctx *new_pipe;
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		new_pipe = &new_res_ctx->pipe_ctx[i];

		if (resource_is_pipe_type(new_pipe, FREE_PIPE)) {
			free_pipe_idx = i;
			break;
		}
	}

	return free_pipe_idx;
}

bool resource_is_pipe_type(const struct pipe_ctx *pipe_ctx, enum pipe_type type)
{
	switch (type) {
	case OTG_MASTER:
		return !pipe_ctx->prev_odm_pipe &&
				!pipe_ctx->top_pipe &&
				pipe_ctx->stream;
	case OPP_HEAD:
		return !pipe_ctx->top_pipe && pipe_ctx->stream;
	case DPP_PIPE:
		return pipe_ctx->plane_state && pipe_ctx->stream;
	case FREE_PIPE:
		return !pipe_ctx->plane_state && !pipe_ctx->stream;
	default:
		return false;
	}
}

struct pipe_ctx *resource_get_otg_master_for_stream(
		struct resource_context *res_ctx,
		const struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (res_ctx->pipe_ctx[i].stream == stream &&
				resource_is_pipe_type(&res_ctx->pipe_ctx[i], OTG_MASTER))
			return &res_ctx->pipe_ctx[i];
	}
	return NULL;
}

int resource_get_opp_heads_for_otg_master(const struct pipe_ctx *otg_master,
		struct resource_context *res_ctx,
		struct pipe_ctx *opp_heads[MAX_PIPES])
{
	struct pipe_ctx *opp_head = &res_ctx->pipe_ctx[otg_master->pipe_idx];
	struct dc *dc = otg_master->stream->ctx->dc;
	int i = 0;

	DC_LOGGER_INIT(dc->ctx->logger);

	if (!resource_is_pipe_type(otg_master, OTG_MASTER)) {
		DC_LOG_WARNING("%s called from a non OTG master, something "
			       "is wrong in the pipe configuration",
			       __func__);
		ASSERT(0);
		return 0;
	}
	while (opp_head) {
		ASSERT(i < MAX_PIPES);
		opp_heads[i++] = opp_head;
		opp_head = opp_head->next_odm_pipe;
	}
	return i;
}

int resource_get_dpp_pipes_for_opp_head(const struct pipe_ctx *opp_head,
		struct resource_context *res_ctx,
		struct pipe_ctx *dpp_pipes[MAX_PIPES])
{
	struct pipe_ctx *pipe = &res_ctx->pipe_ctx[opp_head->pipe_idx];
	int i = 0;

	if (!resource_is_pipe_type(opp_head, OPP_HEAD)) {
		ASSERT(0);
		return 0;
	}
	while (pipe && resource_is_pipe_type(pipe, DPP_PIPE)) {
		ASSERT(i < MAX_PIPES);
		dpp_pipes[i++] = pipe;
		pipe = pipe->bottom_pipe;
	}
	return i;
}

int resource_get_dpp_pipes_for_plane(const struct dc_plane_state *plane,
		struct resource_context *res_ctx,
		struct pipe_ctx *dpp_pipes[MAX_PIPES])
{
	int i = 0, j;
	struct pipe_ctx *pipe;

	for (j = 0; j < MAX_PIPES; j++) {
		pipe = &res_ctx->pipe_ctx[j];
		if (pipe->plane_state == plane && pipe->prev_odm_pipe == NULL) {
			if (resource_is_pipe_type(pipe, OPP_HEAD) ||
					pipe->top_pipe->plane_state != plane)
				break;
		}
	}

	if (j < MAX_PIPES) {
		if (pipe->next_odm_pipe)
			while (pipe) {
				dpp_pipes[i++] = pipe;
				pipe = pipe->next_odm_pipe;
			}
		else
			while (pipe && pipe->plane_state == plane) {
				dpp_pipes[i++] = pipe;
				pipe = pipe->bottom_pipe;
			}
	}
	return i;
}

struct pipe_ctx *resource_get_otg_master(const struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *otg_master = resource_get_opp_head(pipe_ctx);

	while (otg_master->prev_odm_pipe)
		otg_master = otg_master->prev_odm_pipe;
	return otg_master;
}

struct pipe_ctx *resource_get_opp_head(const struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *opp_head = (struct pipe_ctx *) pipe_ctx;

	ASSERT(!resource_is_pipe_type(opp_head, FREE_PIPE));
	while (opp_head->top_pipe)
		opp_head = opp_head->top_pipe;
	return opp_head;
}

struct pipe_ctx *resource_get_primary_dpp_pipe(const struct pipe_ctx *dpp_pipe)
{
	struct pipe_ctx *pri_dpp_pipe = (struct pipe_ctx *) dpp_pipe;

	ASSERT(resource_is_pipe_type(dpp_pipe, DPP_PIPE));
	while (pri_dpp_pipe->prev_odm_pipe)
		pri_dpp_pipe = pri_dpp_pipe->prev_odm_pipe;
	while (pri_dpp_pipe->top_pipe &&
			pri_dpp_pipe->top_pipe->plane_state == pri_dpp_pipe->plane_state)
		pri_dpp_pipe = pri_dpp_pipe->top_pipe;
	return pri_dpp_pipe;
}


int resource_get_mpc_slice_index(const struct pipe_ctx *pipe_ctx)
{
	struct pipe_ctx *split_pipe = pipe_ctx->top_pipe;
	int index = 0;

	while (split_pipe && split_pipe->plane_state == pipe_ctx->plane_state) {
		index++;
		split_pipe = split_pipe->top_pipe;
	}

	return index;
}

int resource_get_mpc_slice_count(const struct pipe_ctx *pipe)
{
	int mpc_split_count = 1;
	const struct pipe_ctx *other_pipe = pipe->bottom_pipe;

	while (other_pipe && other_pipe->plane_state == pipe->plane_state) {
		mpc_split_count++;
		other_pipe = other_pipe->bottom_pipe;
	}
	other_pipe = pipe->top_pipe;
	while (other_pipe && other_pipe->plane_state == pipe->plane_state) {
		mpc_split_count++;
		other_pipe = other_pipe->top_pipe;
	}

	return mpc_split_count;
}

int resource_get_odm_slice_count(const struct pipe_ctx *pipe)
{
	int odm_split_count = 1;

	pipe = resource_get_otg_master(pipe);

	while (pipe->next_odm_pipe) {
		odm_split_count++;
		pipe = pipe->next_odm_pipe;
	}
	return odm_split_count;
}

int resource_get_odm_slice_index(const struct pipe_ctx *pipe_ctx)
{
	int index = 0;

	pipe_ctx = resource_get_opp_head(pipe_ctx);
	if (!pipe_ctx)
		return 0;

	while (pipe_ctx->prev_odm_pipe) {
		index++;
		pipe_ctx = pipe_ctx->prev_odm_pipe;
	}

	return index;
}

int resource_get_odm_slice_dst_width(struct pipe_ctx *otg_master,
		bool is_last_segment)
{
	const struct dc_crtc_timing *timing;
	int count;
	int h_active;
	int width;
	bool two_pixel_alignment_required = false;

	if (!otg_master || !otg_master->stream)
		return 0;

	timing = &otg_master->stream->timing;
	count = resource_get_odm_slice_count(otg_master);
	h_active = timing->h_addressable +
			timing->h_border_left +
			timing->h_border_right +
			otg_master->hblank_borrow;
	width = h_active / count;

	if (otg_master->stream_res.tg)
		two_pixel_alignment_required =
				otg_master->stream_res.tg->funcs->is_two_pixels_per_container(timing) ||
				/*
				 * 422 is sub-sampled horizontally. 1 set of chromas
				 * (Cb/Cr) is shared for 2 lumas (i.e 2 Y values).
				 * Therefore even if 422 is still 1 pixel per container,
				 * ODM segment width still needs to be 2 pixel aligned.
				 */
				timing->pixel_encoding == PIXEL_ENCODING_YCBCR422;
	if ((width % 2) && two_pixel_alignment_required)
		width++;

	return is_last_segment ?
			h_active - width * (count - 1) :
			width;
}

struct rect resource_get_odm_slice_dst_rect(struct pipe_ctx *pipe_ctx)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
	bool is_last_odm_slice = pipe_ctx->next_odm_pipe == NULL;
	struct pipe_ctx *otg_master = resource_get_otg_master(pipe_ctx);
	int odm_slice_idx = resource_get_odm_slice_index(pipe_ctx);
	int odm_segment_offset = resource_get_odm_slice_dst_width(otg_master, false);
	struct rect odm_slice_dst;

	odm_slice_dst.x = odm_segment_offset * odm_slice_idx;
	odm_slice_dst.width = resource_get_odm_slice_dst_width(otg_master, is_last_odm_slice);
	odm_slice_dst.y = 0;
	odm_slice_dst.height = stream->timing.v_addressable +
			stream->timing.v_border_bottom +
			stream->timing.v_border_top;

	return odm_slice_dst;
}

struct rect resource_get_odm_slice_src_rect(struct pipe_ctx *pipe_ctx)
{
	struct rect odm_slice_dst;
	struct rect odm_slice_src;
	struct pipe_ctx *opp_head = resource_get_opp_head(pipe_ctx);
	struct output_pixel_processor *opp = opp_head->stream_res.opp;
	uint32_t left_edge_extra_pixel_count;

	odm_slice_dst = resource_get_odm_slice_dst_rect(opp_head);
	odm_slice_src = odm_slice_dst;

	if (opp && opp->funcs->opp_get_left_edge_extra_pixel_count)
		left_edge_extra_pixel_count =
				opp->funcs->opp_get_left_edge_extra_pixel_count(
						opp, pipe_ctx->stream->timing.pixel_encoding,
						resource_is_pipe_type(opp_head, OTG_MASTER));
	else
		left_edge_extra_pixel_count = 0;

	odm_slice_src.x -= left_edge_extra_pixel_count;
	odm_slice_src.width += left_edge_extra_pixel_count;

	return odm_slice_src;
}

bool resource_is_pipe_topology_changed(const struct dc_state *state_a,
		const struct dc_state *state_b)
{
	int i;
	const struct pipe_ctx *pipe_a, *pipe_b;

	if (state_a->stream_count != state_b->stream_count)
		return true;

	for (i = 0; i < MAX_PIPES; i++) {
		pipe_a = &state_a->res_ctx.pipe_ctx[i];
		pipe_b = &state_b->res_ctx.pipe_ctx[i];

		if (pipe_a->stream && !pipe_b->stream)
			return true;
		else if (!pipe_a->stream && pipe_b->stream)
			return true;

		if (pipe_a->plane_state && !pipe_b->plane_state)
			return true;
		else if (!pipe_a->plane_state && pipe_b->plane_state)
			return true;

		if (pipe_a->bottom_pipe && pipe_b->bottom_pipe) {
			if (pipe_a->bottom_pipe->pipe_idx != pipe_b->bottom_pipe->pipe_idx)
				return true;
			if ((pipe_a->bottom_pipe->plane_state == pipe_a->plane_state) &&
					(pipe_b->bottom_pipe->plane_state != pipe_b->plane_state))
				return true;
			else if ((pipe_a->bottom_pipe->plane_state != pipe_a->plane_state) &&
					(pipe_b->bottom_pipe->plane_state == pipe_b->plane_state))
				return true;
		} else if (pipe_a->bottom_pipe || pipe_b->bottom_pipe) {
			return true;
		}

		if (pipe_a->next_odm_pipe && pipe_b->next_odm_pipe) {
			if (pipe_a->next_odm_pipe->pipe_idx != pipe_b->next_odm_pipe->pipe_idx)
				return true;
		} else if (pipe_a->next_odm_pipe || pipe_b->next_odm_pipe) {
			return true;
		}
	}
	return false;
}

bool resource_is_odm_topology_changed(const struct pipe_ctx *otg_master_a,
		const struct pipe_ctx *otg_master_b)
{
	const struct pipe_ctx *opp_head_a = otg_master_a;
	const struct pipe_ctx *opp_head_b = otg_master_b;

	if (!resource_is_pipe_type(otg_master_a, OTG_MASTER) ||
			!resource_is_pipe_type(otg_master_b, OTG_MASTER))
		return true;

	while (opp_head_a && opp_head_b) {
		if (opp_head_a->stream_res.opp != opp_head_b->stream_res.opp)
			return true;
		if ((opp_head_a->next_odm_pipe && !opp_head_b->next_odm_pipe) ||
				(!opp_head_a->next_odm_pipe && opp_head_b->next_odm_pipe))
			return true;
		opp_head_a = opp_head_a->next_odm_pipe;
		opp_head_b = opp_head_b->next_odm_pipe;
	}

	return false;
}

/*
 * Sample log:
 *    pipe topology update
 *  ________________________
 * | plane0  slice0  stream0|
 * |DPP0----OPP0----OTG0----| <--- case 0 (OTG master pipe with plane)
 * | plane1 |       |       |
 * |DPP1----|       |       | <--- case 5 (DPP pipe not in last slice)
 * | plane0  slice1 |       |
 * |DPP2----OPP2----|       | <--- case 2 (OPP head pipe with plane)
 * | plane1 |               |
 * |DPP3----|               | <--- case 4 (DPP pipe in last slice)
 * |         slice0  stream1|
 * |DPG4----OPP4----OTG4----| <--- case 1 (OTG master pipe without plane)
 * |         slice1 |       |
 * |DPG5----OPP5----|       | <--- case 3 (OPP head pipe without plane)
 * |________________________|
 */

static void resource_log_pipe(struct dc *dc, struct pipe_ctx *pipe,
		int stream_idx, int slice_idx, int plane_idx, int slice_count,
		bool is_primary)
{
	DC_LOGGER_INIT(dc->ctx->logger);

	if (slice_idx == 0 && plane_idx == 0 && is_primary) {
		/* case 0 (OTG master pipe with plane) */
		DC_LOG_DC(" | plane%d  slice%d  stream%d|",
				plane_idx, slice_idx, stream_idx);
		DC_LOG_DC(" |DPP%d----OPP%d----OTG%d----|",
				pipe->plane_res.dpp->inst,
				pipe->stream_res.opp->inst,
				pipe->stream_res.tg->inst);
	} else if (slice_idx == 0 && plane_idx == -1) {
		/* case 1 (OTG master pipe without plane) */
		DC_LOG_DC(" |         slice%d  stream%d|",
				slice_idx, stream_idx);
		DC_LOG_DC(" |DPG%d----OPP%d----OTG%d----|",
				pipe->stream_res.opp->inst,
				pipe->stream_res.opp->inst,
				pipe->stream_res.tg->inst);
	} else if (slice_idx != 0 && plane_idx == 0 && is_primary) {
		/* case 2 (OPP head pipe with plane) */
		DC_LOG_DC(" | plane%d  slice%d |       |",
				plane_idx, slice_idx);
		DC_LOG_DC(" |DPP%d----OPP%d----|       |",
				pipe->plane_res.dpp->inst,
				pipe->stream_res.opp->inst);
	} else if (slice_idx != 0 && plane_idx == -1) {
		/* case 3 (OPP head pipe without plane) */
		DC_LOG_DC(" |         slice%d |       |", slice_idx);
		DC_LOG_DC(" |DPG%d----OPP%d----|       |",
				pipe->plane_res.dpp->inst,
				pipe->stream_res.opp->inst);
	} else if (slice_idx == slice_count - 1) {
		/* case 4 (DPP pipe in last slice) */
		DC_LOG_DC(" | plane%d |               |", plane_idx);
		DC_LOG_DC(" |DPP%d----|               |",
				pipe->plane_res.dpp->inst);
	} else {
		/* case 5 (DPP pipe not in last slice) */
		DC_LOG_DC(" | plane%d |       |       |", plane_idx);
		DC_LOG_DC(" |DPP%d----|       |       |",
				pipe->plane_res.dpp->inst);
	}
}

static void resource_log_pipe_for_stream(struct dc *dc, struct dc_state *state,
		struct pipe_ctx *otg_master, int stream_idx)
{
	struct pipe_ctx *opp_heads[MAX_PIPES];
	struct pipe_ctx *dpp_pipes[MAX_PIPES];

	int slice_idx, dpp_idx, plane_idx, slice_count, dpp_count;
	bool is_primary;
	DC_LOGGER_INIT(dc->ctx->logger);

	slice_count = resource_get_opp_heads_for_otg_master(otg_master,
			&state->res_ctx, opp_heads);
	for (slice_idx = 0; slice_idx < slice_count; slice_idx++) {
		plane_idx = -1;
		if (opp_heads[slice_idx]->plane_state) {
			dpp_count = resource_get_dpp_pipes_for_opp_head(
					opp_heads[slice_idx],
					&state->res_ctx,
					dpp_pipes);
			for (dpp_idx = 0; dpp_idx < dpp_count; dpp_idx++) {
				is_primary = !dpp_pipes[dpp_idx]->top_pipe ||
						dpp_pipes[dpp_idx]->top_pipe->plane_state != dpp_pipes[dpp_idx]->plane_state;
				if (is_primary)
					plane_idx++;
				resource_log_pipe(dc, dpp_pipes[dpp_idx],
						stream_idx, slice_idx,
						plane_idx, slice_count,
						is_primary);
			}
		} else {
			resource_log_pipe(dc, opp_heads[slice_idx],
					stream_idx, slice_idx, plane_idx,
					slice_count, true);
		}

	}
}

static int resource_stream_to_stream_idx(struct dc_state *state,
		struct dc_stream_state *stream)
{
	int i, stream_idx = -1;

	for (i = 0; i < state->stream_count; i++)
		if (state->streams[i] == stream) {
			stream_idx = i;
			break;
		}

	/* never return negative array index */
	if (stream_idx == -1) {
		ASSERT(0);
		return 0;
	}

	return stream_idx;
}

void resource_log_pipe_topology_update(struct dc *dc, struct dc_state *state)
{
	struct pipe_ctx *otg_master;
	int stream_idx, phantom_stream_idx;
	DC_LOGGER_INIT(dc->ctx->logger);

	DC_LOG_DC("    pipe topology update");
	DC_LOG_DC("  ________________________");
	for (stream_idx = 0; stream_idx < state->stream_count; stream_idx++) {
		if (state->streams[stream_idx]->is_phantom)
			continue;

		otg_master = resource_get_otg_master_for_stream(
				&state->res_ctx, state->streams[stream_idx]);

		if (!otg_master)
			continue;

		resource_log_pipe_for_stream(dc, state, otg_master, stream_idx);
	}
	if (state->phantom_stream_count > 0) {
		DC_LOG_DC(" |    (phantom pipes)     |");
		for (stream_idx = 0; stream_idx < state->stream_count; stream_idx++) {
			if (state->stream_status[stream_idx].mall_stream_config.type != SUBVP_MAIN)
				continue;

			phantom_stream_idx = resource_stream_to_stream_idx(state,
					state->stream_status[stream_idx].mall_stream_config.paired_stream);
			otg_master = resource_get_otg_master_for_stream(
					&state->res_ctx, state->streams[phantom_stream_idx]);
			if (!otg_master)
				continue;

			resource_log_pipe_for_stream(dc, state, otg_master, stream_idx);
		}
	}
	DC_LOG_DC(" |________________________|\n");
}

static struct pipe_ctx *get_tail_pipe(
		struct pipe_ctx *head_pipe)
{
	struct pipe_ctx *tail_pipe = head_pipe->bottom_pipe;

	while (tail_pipe) {
		head_pipe = tail_pipe;
		tail_pipe = tail_pipe->bottom_pipe;
	}

	return head_pipe;
}

static struct pipe_ctx *get_last_opp_head(
		struct pipe_ctx *opp_head)
{
	ASSERT(resource_is_pipe_type(opp_head, OPP_HEAD));
	while (opp_head->next_odm_pipe)
		opp_head = opp_head->next_odm_pipe;
	return opp_head;
}

static struct pipe_ctx *get_last_dpp_pipe_in_mpcc_combine(
		struct pipe_ctx *dpp_pipe)
{
	ASSERT(resource_is_pipe_type(dpp_pipe, DPP_PIPE));
	while (dpp_pipe->bottom_pipe &&
			dpp_pipe->plane_state == dpp_pipe->bottom_pipe->plane_state)
		dpp_pipe = dpp_pipe->bottom_pipe;
	return dpp_pipe;
}

static bool update_pipe_params_after_odm_slice_count_change(
		struct pipe_ctx *otg_master,
		struct dc_state *context,
		const struct resource_pool *pool)
{
	int i;
	struct pipe_ctx *pipe;
	bool result = true;

	for (i = 0; i < pool->pipe_count && result; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];
		if (pipe->stream == otg_master->stream && pipe->plane_state)
			result = resource_build_scaling_params(pipe);
	}

	if (pool->funcs->build_pipe_pix_clk_params)
		pool->funcs->build_pipe_pix_clk_params(otg_master);

	resource_build_test_pattern_params(&context->res_ctx, otg_master);

	return result;
}

static bool update_pipe_params_after_mpc_slice_count_change(
		const struct dc_plane_state *plane,
		struct dc_state *context,
		const struct resource_pool *pool)
{
	int i;
	struct pipe_ctx *pipe;
	bool result = true;

	for (i = 0; i < pool->pipe_count && result; i++) {
		pipe = &context->res_ctx.pipe_ctx[i];
		if (pipe->plane_state == plane)
			result = resource_build_scaling_params(pipe);
	}
	return result;
}

static int acquire_first_split_pipe(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < pool->pipe_count; i++) {
		struct pipe_ctx *split_pipe = &res_ctx->pipe_ctx[i];

		if (split_pipe->top_pipe &&
				split_pipe->top_pipe->plane_state == split_pipe->plane_state) {
			split_pipe->top_pipe->bottom_pipe = split_pipe->bottom_pipe;
			if (split_pipe->bottom_pipe)
				split_pipe->bottom_pipe->top_pipe = split_pipe->top_pipe;

			if (split_pipe->top_pipe->plane_state)
				resource_build_scaling_params(split_pipe->top_pipe);

			memset(split_pipe, 0, sizeof(*split_pipe));
			split_pipe->stream_res.tg = pool->timing_generators[i];
			split_pipe->plane_res.hubp = pool->hubps[i];
			split_pipe->plane_res.ipp = pool->ipps[i];
			split_pipe->plane_res.dpp = pool->dpps[i];
			split_pipe->stream_res.opp = pool->opps[i];
			split_pipe->plane_res.mpcc_inst = pool->dpps[i]->inst;
			split_pipe->pipe_idx = i;

			split_pipe->stream = stream;
			return i;
		}
	}
	return FREE_PIPE_INDEX_NOT_FOUND;
}

static void update_stream_engine_usage(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct stream_encoder *stream_enc,
		bool acquired)
{
	int i;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i] == stream_enc)
			res_ctx->is_stream_enc_acquired[i] = acquired;
	}
}

static void update_hpo_dp_stream_engine_usage(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct hpo_dp_stream_encoder *hpo_dp_stream_enc,
		bool acquired)
{
	int i;

	for (i = 0; i < pool->hpo_dp_stream_enc_count; i++) {
		if (pool->hpo_dp_stream_enc[i] == hpo_dp_stream_enc)
			res_ctx->is_hpo_dp_stream_enc_acquired[i] = acquired;
	}
}

static inline int find_acquired_hpo_dp_link_enc_for_link(
		const struct resource_context *res_ctx,
		const struct dc_link *link)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(res_ctx->hpo_dp_link_enc_to_link_idx); i++)
		if (res_ctx->hpo_dp_link_enc_ref_cnts[i] > 0 &&
				res_ctx->hpo_dp_link_enc_to_link_idx[i] == link->link_index)
			return i;

	return -1;
}

static inline int find_free_hpo_dp_link_enc(const struct resource_context *res_ctx,
		const struct resource_pool *pool)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(res_ctx->hpo_dp_link_enc_ref_cnts); i++)
		if (res_ctx->hpo_dp_link_enc_ref_cnts[i] == 0)
			break;

	return (i < ARRAY_SIZE(res_ctx->hpo_dp_link_enc_ref_cnts) &&
			i < pool->hpo_dp_link_enc_count) ? i : -1;
}

static inline void acquire_hpo_dp_link_enc(
		struct resource_context *res_ctx,
		unsigned int link_index,
		int enc_index)
{
	res_ctx->hpo_dp_link_enc_to_link_idx[enc_index] = link_index;
	res_ctx->hpo_dp_link_enc_ref_cnts[enc_index] = 1;
}

static inline void retain_hpo_dp_link_enc(
		struct resource_context *res_ctx,
		int enc_index)
{
	res_ctx->hpo_dp_link_enc_ref_cnts[enc_index]++;
}

static inline void release_hpo_dp_link_enc(
		struct resource_context *res_ctx,
		int enc_index)
{
	ASSERT(res_ctx->hpo_dp_link_enc_ref_cnts[enc_index] > 0);
	res_ctx->hpo_dp_link_enc_ref_cnts[enc_index]--;
}

static bool add_hpo_dp_link_enc_to_ctx(struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_state *stream)
{
	int enc_index;

	enc_index = find_acquired_hpo_dp_link_enc_for_link(res_ctx, stream->link);

	if (enc_index >= 0) {
		retain_hpo_dp_link_enc(res_ctx, enc_index);
	} else {
		enc_index = find_free_hpo_dp_link_enc(res_ctx, pool);
		if (enc_index >= 0)
			acquire_hpo_dp_link_enc(res_ctx, stream->link->link_index, enc_index);
	}

	if (enc_index >= 0)
		pipe_ctx->link_res.hpo_dp_link_enc = pool->hpo_dp_link_enc[enc_index];

	return pipe_ctx->link_res.hpo_dp_link_enc != NULL;
}

static void remove_hpo_dp_link_enc_from_ctx(struct resource_context *res_ctx,
		struct pipe_ctx *pipe_ctx,
		struct dc_stream_state *stream)
{
	int enc_index;

	enc_index = find_acquired_hpo_dp_link_enc_for_link(res_ctx, stream->link);

	if (enc_index >= 0) {
		release_hpo_dp_link_enc(res_ctx, enc_index);
		pipe_ctx->link_res.hpo_dp_link_enc = NULL;
	}
}

static int get_num_of_free_pipes(const struct resource_pool *pool, const struct dc_state *context)
{
	int i;
	int count = 0;

	for (i = 0; i < pool->pipe_count; i++)
		if (resource_is_pipe_type(&context->res_ctx.pipe_ctx[i], FREE_PIPE))
			count++;
	return count;
}

enum dc_status resource_add_otg_master_for_stream_output(struct dc_state *new_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct dc *dc = stream->ctx->dc;

	return dc->res_pool->funcs->add_stream_to_ctx(dc, new_ctx, stream);
}

void resource_remove_otg_master_for_stream_output(struct dc_state *context,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct pipe_ctx *otg_master = resource_get_otg_master_for_stream(
			&context->res_ctx, stream);

	if (!otg_master)
		return;

	ASSERT(resource_get_odm_slice_count(otg_master) == 1);
	ASSERT(otg_master->plane_state == NULL);
	ASSERT(otg_master->stream_res.stream_enc);
	update_stream_engine_usage(
			&context->res_ctx,
			pool,
			otg_master->stream_res.stream_enc,
			false);

	if (stream->ctx->dc->link_srv->dp_is_128b_132b_signal(otg_master)) {
		update_hpo_dp_stream_engine_usage(
			&context->res_ctx, pool,
			otg_master->stream_res.hpo_dp_stream_enc,
			false);
		remove_hpo_dp_link_enc_from_ctx(
				&context->res_ctx, otg_master, stream);
	}
	if (otg_master->stream_res.audio)
		update_audio_usage(
			&context->res_ctx,
			pool,
			otg_master->stream_res.audio,
			false);

	resource_unreference_clock_source(&context->res_ctx,
					  pool,
					  otg_master->clock_source);

	if (pool->funcs->remove_stream_from_ctx)
		pool->funcs->remove_stream_from_ctx(
				stream->ctx->dc, context, stream);
	memset(otg_master, 0, sizeof(*otg_master));
}

/* For each OPP head of an OTG master, add top plane at plane index 0.
 *
 * In the following example, the stream has 2 ODM slices without a top plane.
 * By adding a plane 0 to OPP heads, we are configuring our hardware to render
 * plane 0 by using each OPP head's DPP.
 *
 *       Inter-pipe Relation (Before Adding Plane)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |               | slice 0   |             |
 *       |   0    |               |blank ----ODM----------- |
 *       |        |               | slice 1 | |             |
 *       |   1    |               |blank ---- |             |
 *       |________|_______________|___________|_____________|
 *
 *       Inter-pipe Relation (After Adding Plane)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------------------ODM----------- |
 *       |        |  plane 0      | slice 1 | |             |
 *       |   1    | ------------------------- |             |
 *       |________|_______________|___________|_____________|
 */
static bool add_plane_to_opp_head_pipes(struct pipe_ctx *otg_master_pipe,
		struct dc_plane_state *plane_state,
		struct dc_state *context)
{
	struct pipe_ctx *opp_head_pipe = otg_master_pipe;

	while (opp_head_pipe) {
		if (opp_head_pipe->plane_state) {
			ASSERT(0);
			return false;
		}
		opp_head_pipe->plane_state = plane_state;
		opp_head_pipe = opp_head_pipe->next_odm_pipe;
	}

	return true;
}

/* For each OPP head of an OTG master, acquire a secondary DPP pipe and add
 * the plane. So the plane is added to all ODM slices associated with the OTG
 * master pipe in the bottom layer.
 *
 * In the following example, the stream has 2 ODM slices and a top plane 0.
 * By acquiring secondary DPP pipes and adding a plane 1, we are configuring our
 * hardware to render the plane 1 by acquiring a new pipe for each ODM slice and
 * render plane 1 using new pipes' DPP in the Z axis below plane 0.
 *
 *       Inter-pipe Relation (Before Adding Plane)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------------------ODM----------- |
 *       |        |  plane 0      | slice 1 | |             |
 *       |   1    | ------------------------- |             |
 *       |________|_______________|___________|_____________|
 *
 *       Inter-pipe Relation (After Acquiring and Adding Plane)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------MPC---------ODM----------- |
 *       |        |  plane 1    | |         | |             |
 *       |   2    | ------------- |         | |             |
 *       |        |  plane 0      | slice 1 | |             |
 *       |   1    | -------------MPC--------- |             |
 *       |        |  plane 1    | |           |             |
 *       |   3    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 */
static bool acquire_secondary_dpp_pipes_and_add_plane(
		struct pipe_ctx *otg_master_pipe,
		struct dc_plane_state *plane_state,
		struct dc_state *new_ctx,
		struct dc_state *cur_ctx,
		struct resource_pool *pool)
{
	struct pipe_ctx *sec_pipe, *tail_pipe;
	struct pipe_ctx *opp_heads[MAX_PIPES];
	int opp_head_count;
	int i;

	if (!pool->funcs->acquire_free_pipe_as_secondary_dpp_pipe) {
		ASSERT(0);
		return false;
	}

	opp_head_count = resource_get_opp_heads_for_otg_master(otg_master_pipe,
			&new_ctx->res_ctx, opp_heads);
	if (get_num_of_free_pipes(pool, new_ctx) < opp_head_count)
		/* not enough free pipes */
		return false;

	for (i = 0; i < opp_head_count; i++) {
		sec_pipe = pool->funcs->acquire_free_pipe_as_secondary_dpp_pipe(
				cur_ctx,
				new_ctx,
				pool,
				opp_heads[i]);
		ASSERT(sec_pipe);
		sec_pipe->plane_state = plane_state;

		/* establish pipe relationship */
		tail_pipe = get_tail_pipe(opp_heads[i]);
		tail_pipe->bottom_pipe = sec_pipe;
		sec_pipe->top_pipe = tail_pipe;
		sec_pipe->bottom_pipe = NULL;
		if (tail_pipe->prev_odm_pipe) {
			ASSERT(tail_pipe->prev_odm_pipe->bottom_pipe);
			sec_pipe->prev_odm_pipe = tail_pipe->prev_odm_pipe->bottom_pipe;
			tail_pipe->prev_odm_pipe->bottom_pipe->next_odm_pipe = sec_pipe;
		} else {
			sec_pipe->prev_odm_pipe = NULL;
		}
	}
	return true;
}

bool resource_append_dpp_pipes_for_plane_composition(
		struct dc_state *new_ctx,
		struct dc_state *cur_ctx,
		struct resource_pool *pool,
		struct pipe_ctx *otg_master_pipe,
		struct dc_plane_state *plane_state)
{
	bool success;

	if (otg_master_pipe->plane_state == NULL)
		success = add_plane_to_opp_head_pipes(otg_master_pipe,
				plane_state, new_ctx);
	else
		success = acquire_secondary_dpp_pipes_and_add_plane(
				otg_master_pipe, plane_state, new_ctx,
				cur_ctx, pool);
	if (success) {
		/* when appending a plane mpc slice count changes from 0 to 1 */
		success = update_pipe_params_after_mpc_slice_count_change(
				plane_state, new_ctx, pool);
		if (!success)
			resource_remove_dpp_pipes_for_plane_composition(new_ctx,
					pool, plane_state);
	}

	return success;
}

void resource_remove_dpp_pipes_for_plane_composition(
		struct dc_state *context,
		const struct resource_pool *pool,
		const struct dc_plane_state *plane_state)
{
	int i;

	for (i = pool->pipe_count - 1; i >= 0; i--) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (pipe_ctx->plane_state == plane_state) {
			if (pipe_ctx->top_pipe)
				pipe_ctx->top_pipe->bottom_pipe = pipe_ctx->bottom_pipe;

			/* Second condition is to avoid setting NULL to top pipe
			 * of tail pipe making it look like head pipe in subsequent
			 * deletes
			 */
			if (pipe_ctx->bottom_pipe && pipe_ctx->top_pipe)
				pipe_ctx->bottom_pipe->top_pipe = pipe_ctx->top_pipe;

			/*
			 * For head pipe detach surfaces from pipe for tail
			 * pipe just zero it out
			 */
			if (!pipe_ctx->top_pipe)
				pipe_ctx->plane_state = NULL;
			else
				memset(pipe_ctx, 0, sizeof(*pipe_ctx));
		}
	}
}

/*
 * Increase ODM slice count by 1 by acquiring pipes and adding a new ODM slice
 * at the last index.
 * return - true if a new ODM slice is added and required pipes are acquired.
 * false if new_ctx is no longer a valid state after new ODM slice is added.
 *
 * This is achieved by duplicating MPC blending tree from previous ODM slice.
 * In the following example, we have a single MPC tree and 1 ODM slice 0. We
 * want to add a new odm slice by duplicating the MPC blending tree and add
 * ODM slice 1.
 *
 *       Inter-pipe Relation (Before Acquiring and Adding ODM Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------MPC---------ODM----------- |
 *       |        |  plane 1    | |           |             |
 *       |   1    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 *
 *       Inter-pipe Relation (After Acquiring and Adding ODM Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------MPC---------ODM----------- |
 *       |        |  plane 1    | |         | |             |
 *       |   1    | ------------- |         | |             |
 *       |        |  plane 0      | slice 1 | |             |
 *       |   2    | -------------MPC--------- |             |
 *       |        |  plane 1    | |           |             |
 *       |   3    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 */
static bool acquire_pipes_and_add_odm_slice(
		struct pipe_ctx *otg_master_pipe,
		struct dc_state *new_ctx,
		const struct dc_state *cur_ctx,
		const struct resource_pool *pool)
{
	struct pipe_ctx *last_opp_head = get_last_opp_head(otg_master_pipe);
	struct pipe_ctx *new_opp_head;
	struct pipe_ctx *last_top_dpp_pipe, *last_bottom_dpp_pipe,
			*new_top_dpp_pipe, *new_bottom_dpp_pipe;

	if (!pool->funcs->acquire_free_pipe_as_secondary_opp_head) {
		ASSERT(0);
		return false;
	}
	new_opp_head = pool->funcs->acquire_free_pipe_as_secondary_opp_head(
					cur_ctx, new_ctx, pool,
					otg_master_pipe);
	if (!new_opp_head)
		return false;

	last_opp_head->next_odm_pipe = new_opp_head;
	new_opp_head->prev_odm_pipe = last_opp_head;
	new_opp_head->next_odm_pipe = NULL;
	new_opp_head->plane_state = last_opp_head->plane_state;
	last_top_dpp_pipe = last_opp_head;
	new_top_dpp_pipe = new_opp_head;

	while (last_top_dpp_pipe->bottom_pipe) {
		last_bottom_dpp_pipe = last_top_dpp_pipe->bottom_pipe;
		new_bottom_dpp_pipe = pool->funcs->acquire_free_pipe_as_secondary_dpp_pipe(
				cur_ctx, new_ctx, pool,
				new_opp_head);
		if (!new_bottom_dpp_pipe)
			return false;

		new_bottom_dpp_pipe->plane_state = last_bottom_dpp_pipe->plane_state;
		new_top_dpp_pipe->bottom_pipe = new_bottom_dpp_pipe;
		new_bottom_dpp_pipe->top_pipe = new_top_dpp_pipe;
		last_bottom_dpp_pipe->next_odm_pipe = new_bottom_dpp_pipe;
		new_bottom_dpp_pipe->prev_odm_pipe = last_bottom_dpp_pipe;
		new_bottom_dpp_pipe->next_odm_pipe = NULL;
		last_top_dpp_pipe = last_bottom_dpp_pipe;
	}

	return true;
}

/*
 * Decrease ODM slice count by 1 by releasing pipes and removing the ODM slice
 * at the last index.
 * return - true if the last ODM slice is removed and related pipes are
 * released. false if there is no removable ODM slice.
 *
 * In the following example, we have 2 MPC trees and ODM slice 0 and slice 1.
 * We want to remove the last ODM i.e slice 1. We are releasing secondary DPP
 * pipe 3 and OPP head pipe 2.
 *
 *       Inter-pipe Relation (Before Releasing and Removing ODM Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------MPC---------ODM----------- |
 *       |        |  plane 1    | |         | |             |
 *       |   1    | ------------- |         | |             |
 *       |        |  plane 0      | slice 1 | |             |
 *       |   2    | -------------MPC--------- |             |
 *       |        |  plane 1    | |           |             |
 *       |   3    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 *
 *       Inter-pipe Relation (After Releasing and Removing ODM Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      | slice 0   |             |
 *       |   0    | -------------MPC---------ODM----------- |
 *       |        |  plane 1    | |           |             |
 *       |   1    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 */
static bool release_pipes_and_remove_odm_slice(
		struct pipe_ctx *otg_master_pipe,
		struct dc_state *context,
		const struct resource_pool *pool)
{
	struct pipe_ctx *last_opp_head = get_last_opp_head(otg_master_pipe);
	struct pipe_ctx *tail_pipe = get_tail_pipe(last_opp_head);

	if (!pool->funcs->release_pipe) {
		ASSERT(0);
		return false;
	}

	if (resource_is_pipe_type(last_opp_head, OTG_MASTER))
		return false;

	while (tail_pipe->top_pipe) {
		tail_pipe->prev_odm_pipe->next_odm_pipe = NULL;
		tail_pipe = tail_pipe->top_pipe;
		pool->funcs->release_pipe(context, tail_pipe->bottom_pipe, pool);
		tail_pipe->bottom_pipe = NULL;
	}
	last_opp_head->prev_odm_pipe->next_odm_pipe = NULL;
	pool->funcs->release_pipe(context, last_opp_head, pool);

	return true;
}

/*
 * Increase MPC slice count by 1 by acquiring a new DPP pipe and add it as the
 * last MPC slice of the plane associated with dpp_pipe.
 *
 * return - true if a new MPC slice is added and required pipes are acquired.
 * false if new_ctx is no longer a valid state after new MPC slice is added.
 *
 * In the following example, we add a new MPC slice for plane 0 into the
 * new_ctx. To do so we pass pipe 0 as dpp_pipe. The function acquires a new DPP
 * pipe 2 for plane 0 as the bottom most pipe for plane 0.
 *
 *       Inter-pipe Relation (Before Acquiring and Adding MPC Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      |           |             |
 *       |   0    | -------------MPC----------------------- |
 *       |        |  plane 1    | |           |             |
 *       |   1    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 *
 *       Inter-pipe Relation (After Acquiring and Adding MPC Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      |           |             |
 *       |   0    | -------------MPC----------------------- |
 *       |        |  plane 0    | |           |             |
 *       |   2    | ------------- |           |             |
 *       |        |  plane 1    | |           |             |
 *       |   1    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 */
static bool acquire_dpp_pipe_and_add_mpc_slice(
		struct pipe_ctx *dpp_pipe,
		struct dc_state *new_ctx,
		const struct dc_state *cur_ctx,
		const struct resource_pool *pool)
{
	struct pipe_ctx *last_dpp_pipe =
			get_last_dpp_pipe_in_mpcc_combine(dpp_pipe);
	struct pipe_ctx *opp_head = resource_get_opp_head(dpp_pipe);
	struct pipe_ctx *new_dpp_pipe;

	if (!pool->funcs->acquire_free_pipe_as_secondary_dpp_pipe) {
		ASSERT(0);
		return false;
	}
	new_dpp_pipe = pool->funcs->acquire_free_pipe_as_secondary_dpp_pipe(
			cur_ctx, new_ctx, pool, opp_head);
	if (!new_dpp_pipe || resource_get_odm_slice_count(dpp_pipe) > 1)
		return false;

	new_dpp_pipe->bottom_pipe = last_dpp_pipe->bottom_pipe;
	if (new_dpp_pipe->bottom_pipe)
		new_dpp_pipe->bottom_pipe->top_pipe = new_dpp_pipe;
	new_dpp_pipe->top_pipe = last_dpp_pipe;
	last_dpp_pipe->bottom_pipe = new_dpp_pipe;
	new_dpp_pipe->plane_state = last_dpp_pipe->plane_state;

	return true;
}

/*
 * Reduce MPC slice count by 1 by releasing the bottom DPP pipe in MPCC combine
 * with dpp_pipe and removing last MPC slice of the plane associated with
 * dpp_pipe.
 *
 * return - true if the last MPC slice of the plane associated with dpp_pipe is
 * removed and last DPP pipe in MPCC combine with dpp_pipe is released.
 * false if there is no removable MPC slice.
 *
 * In the following example, we remove an MPC slice for plane 0 from the
 * context. To do so we pass pipe 0 as dpp_pipe. The function releases pipe 1 as
 * it is the last pipe for plane 0.
 *
 *       Inter-pipe Relation (Before Releasing and Removing MPC Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      |           |             |
 *       |   0    | -------------MPC----------------------- |
 *       |        |  plane 0    | |           |             |
 *       |   1    | ------------- |           |             |
 *       |        |  plane 1    | |           |             |
 *       |   2    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 *
 *       Inter-pipe Relation (After Releasing and Removing MPC Slice)
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |  plane 0      |           |             |
 *       |   0    | -------------MPC----------------------- |
 *       |        |  plane 1    | |           |             |
 *       |   2    | ------------- |           |             |
 *       |________|_______________|___________|_____________|
 */
static bool release_dpp_pipe_and_remove_mpc_slice(
		struct pipe_ctx *dpp_pipe,
		struct dc_state *context,
		const struct resource_pool *pool)
{
	struct pipe_ctx *last_dpp_pipe =
			get_last_dpp_pipe_in_mpcc_combine(dpp_pipe);

	if (!pool->funcs->release_pipe) {
		ASSERT(0);
		return false;
	}

	if (resource_is_pipe_type(last_dpp_pipe, OPP_HEAD) ||
			resource_get_odm_slice_count(dpp_pipe) > 1)
		return false;

	last_dpp_pipe->top_pipe->bottom_pipe = last_dpp_pipe->bottom_pipe;
	if (last_dpp_pipe->bottom_pipe)
		last_dpp_pipe->bottom_pipe->top_pipe = last_dpp_pipe->top_pipe;
	pool->funcs->release_pipe(context, last_dpp_pipe, pool);

	return true;
}

bool resource_update_pipes_for_stream_with_slice_count(
		struct dc_state *new_ctx,
		const struct dc_state *cur_ctx,
		const struct resource_pool *pool,
		const struct dc_stream_state *stream,
		int new_slice_count)
{
	int i;
	struct pipe_ctx *otg_master = resource_get_otg_master_for_stream(
			&new_ctx->res_ctx, stream);
	int cur_slice_count;
	bool result = true;

	if (!otg_master)
		return false;

	cur_slice_count = resource_get_odm_slice_count(otg_master);

	if (new_slice_count == cur_slice_count)
		return result;

	if (new_slice_count > cur_slice_count)
		for (i = 0; i < new_slice_count - cur_slice_count && result; i++)
			result = acquire_pipes_and_add_odm_slice(
					otg_master, new_ctx, cur_ctx, pool);
	else
		for (i = 0; i < cur_slice_count - new_slice_count && result; i++)
			result = release_pipes_and_remove_odm_slice(
					otg_master, new_ctx, pool);
	if (result)
		result = update_pipe_params_after_odm_slice_count_change(
				otg_master, new_ctx, pool);
	return result;
}

bool resource_update_pipes_for_plane_with_slice_count(
		struct dc_state *new_ctx,
		const struct dc_state *cur_ctx,
		const struct resource_pool *pool,
		const struct dc_plane_state *plane,
		int new_slice_count)
{
	int i;
	int dpp_pipe_count;
	int cur_slice_count;
	struct pipe_ctx *dpp_pipes[MAX_PIPES] = {0};
	bool result = true;

	dpp_pipe_count = resource_get_dpp_pipes_for_plane(plane,
			&new_ctx->res_ctx, dpp_pipes);
	ASSERT(dpp_pipe_count > 0);
	cur_slice_count = resource_get_mpc_slice_count(dpp_pipes[0]);

	if (new_slice_count == cur_slice_count)
		return result;

	if (new_slice_count > cur_slice_count)
		for (i = 0; i < new_slice_count - cur_slice_count && result; i++)
			result = acquire_dpp_pipe_and_add_mpc_slice(
					dpp_pipes[0], new_ctx, cur_ctx, pool);
	else
		for (i = 0; i < cur_slice_count - new_slice_count && result; i++)
			result = release_dpp_pipe_and_remove_mpc_slice(
					dpp_pipes[0], new_ctx, pool);
	if (result)
		result = update_pipe_params_after_mpc_slice_count_change(
				dpp_pipes[0]->plane_state, new_ctx, pool);
	return result;
}

bool dc_is_timing_changed(struct dc_stream_state *cur_stream,
		       struct dc_stream_state *new_stream)
{
	if (cur_stream == NULL)
		return true;

	/* If output color space is changed, need to reprogram info frames */
	if (cur_stream->output_color_space != new_stream->output_color_space)
		return true;

	return memcmp(
		&cur_stream->timing,
		&new_stream->timing,
		sizeof(struct dc_crtc_timing)) != 0;
}

static bool are_stream_backends_same(
	struct dc_stream_state *stream_a, struct dc_stream_state *stream_b)
{
	if (stream_a == stream_b)
		return true;

	if (stream_a == NULL || stream_b == NULL)
		return false;

	if (dc_is_timing_changed(stream_a, stream_b))
		return false;

	if (stream_a->signal != stream_b->signal)
		return false;

	if (stream_a->dpms_off != stream_b->dpms_off)
		return false;

	return true;
}

/*
 * dc_is_stream_unchanged() - Compare two stream states for equivalence.
 *
 * Checks if there a difference between the two states
 * that would require a mode change.
 *
 * Does not compare cursor position or attributes.
 */
bool dc_is_stream_unchanged(
	struct dc_stream_state *old_stream, struct dc_stream_state *stream)
{
	if (!old_stream || !stream)
		return false;

	if (!are_stream_backends_same(old_stream, stream))
		return false;

	if (old_stream->ignore_msa_timing_param != stream->ignore_msa_timing_param)
		return false;

	/*compare audio info*/
	if (memcmp(&old_stream->audio_info, &stream->audio_info, sizeof(stream->audio_info)) != 0)
		return false;

	return true;
}

/*
 * dc_is_stream_scaling_unchanged() - Compare scaling rectangles of two streams.
 */
bool dc_is_stream_scaling_unchanged(struct dc_stream_state *old_stream,
				    struct dc_stream_state *stream)
{
	if (old_stream == stream)
		return true;

	if (old_stream == NULL || stream == NULL)
		return false;

	if (memcmp(&old_stream->src,
			&stream->src,
			sizeof(struct rect)) != 0)
		return false;

	if (memcmp(&old_stream->dst,
			&stream->dst,
			sizeof(struct rect)) != 0)
		return false;

	return true;
}

/* TODO: release audio object */
void update_audio_usage(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct audio *audio,
		bool acquired)
{
	int i;
	for (i = 0; i < pool->audio_count; i++) {
		if (pool->audios[i] == audio)
			res_ctx->is_audio_acquired[i] = acquired;
	}
}

static struct hpo_dp_stream_encoder *find_first_free_match_hpo_dp_stream_enc_for_link(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	int i;

	for (i = 0; i < pool->hpo_dp_stream_enc_count; i++) {
		if (!res_ctx->is_hpo_dp_stream_enc_acquired[i] &&
				pool->hpo_dp_stream_enc[i]) {

			return pool->hpo_dp_stream_enc[i];
		}
	}

	return NULL;
}

static struct audio *find_first_free_audio(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		enum engine_id id,
		enum dce_version dc_version)
{
	int i, available_audio_count;

	if (id == ENGINE_ID_UNKNOWN)
		return NULL;

	available_audio_count = pool->audio_count;

	for (i = 0; i < available_audio_count; i++) {
		if ((res_ctx->is_audio_acquired[i] == false) && (res_ctx->is_stream_enc_acquired[i] == true)) {
			/*we have enough audio endpoint, find the matching inst*/
			if (id != i)
				continue;
			return pool->audios[i];
		}
	}

	/* use engine id to find free audio */
	if ((id < available_audio_count) && (res_ctx->is_audio_acquired[id] == false)) {
		return pool->audios[id];
	}
	/*not found the matching one, first come first serve*/
	for (i = 0; i < available_audio_count; i++) {
		if (res_ctx->is_audio_acquired[i] == false) {
			return pool->audios[i];
		}
	}
	return NULL;
}

static struct dc_stream_state *find_pll_sharable_stream(
		struct dc_stream_state *stream_needs_pll,
		struct dc_state *context)
{
	int i;

	for (i = 0; i < context->stream_count; i++) {
		struct dc_stream_state *stream_has_pll = context->streams[i];

		/* We are looking for non dp, non virtual stream */
		if (resource_are_streams_timing_synchronizable(
			stream_needs_pll, stream_has_pll)
			&& !dc_is_dp_signal(stream_has_pll->signal)
			&& stream_has_pll->link->connector_signal
			!= SIGNAL_TYPE_VIRTUAL)
			return stream_has_pll;

	}

	return NULL;
}

static int get_norm_pix_clk(const struct dc_crtc_timing *timing)
{
	uint32_t pix_clk = timing->pix_clk_100hz;
	uint32_t normalized_pix_clk = pix_clk;

	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		pix_clk /= 2;
	if (timing->pixel_encoding != PIXEL_ENCODING_YCBCR422) {
		switch (timing->display_color_depth) {
		case COLOR_DEPTH_666:
		case COLOR_DEPTH_888:
			normalized_pix_clk = pix_clk;
			break;
		case COLOR_DEPTH_101010:
			normalized_pix_clk = (pix_clk * 30) / 24;
			break;
		case COLOR_DEPTH_121212:
			normalized_pix_clk = (pix_clk * 36) / 24;
			break;
		case COLOR_DEPTH_141414:
			normalized_pix_clk = (pix_clk * 42) / 24;
			break;
		case COLOR_DEPTH_161616:
			normalized_pix_clk = (pix_clk * 48) / 24;
			break;
		default:
			ASSERT(0);
		break;
		}
	}
	return normalized_pix_clk;
}

static void calculate_phy_pix_clks(struct dc_stream_state *stream)
{
	/* update actual pixel clock on all streams */
	if (dc_is_hdmi_signal(stream->signal))
		stream->phy_pix_clk = get_norm_pix_clk(
			&stream->timing) / 10;
	else
		stream->phy_pix_clk =
			stream->timing.pix_clk_100hz / 10;

	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		stream->phy_pix_clk *= 2;
}

static int acquire_resource_from_hw_enabled_state(
		struct resource_context *res_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	struct dc_link *link = stream->link;
	unsigned int i, inst, tg_inst = 0;
	uint32_t numPipes = 1;
	uint32_t id_src[4] = {0};

	/* Check for enabled DIG to identify enabled display */
	if (!link->link_enc->funcs->is_dig_enabled(link->link_enc))
		return -1;

	inst = link->link_enc->funcs->get_dig_frontend(link->link_enc);

	if (inst == ENGINE_ID_UNKNOWN)
		return -1;

	for (i = 0; i < pool->stream_enc_count; i++) {
		if (pool->stream_enc[i]->id == inst) {
			tg_inst = pool->stream_enc[i]->funcs->dig_source_otg(
				pool->stream_enc[i]);
			break;
		}
	}

	// tg_inst not found
	if (i == pool->stream_enc_count)
		return -1;

	if (tg_inst >= pool->timing_generator_count)
		return -1;

	if (!res_ctx->pipe_ctx[tg_inst].stream) {
		struct pipe_ctx *pipe_ctx = &res_ctx->pipe_ctx[tg_inst];

		pipe_ctx->stream_res.tg = pool->timing_generators[tg_inst];
		id_src[0] = tg_inst;

		if (pipe_ctx->stream_res.tg->funcs->get_optc_source)
			pipe_ctx->stream_res.tg->funcs->get_optc_source(pipe_ctx->stream_res.tg,
						&numPipes, &id_src[0], &id_src[1]);

		if (id_src[0] == 0xf && id_src[1] == 0xf) {
			id_src[0] = tg_inst;
			numPipes = 1;
		}

		for (i = 0; i < numPipes; i++) {
			//Check if src id invalid
			if (id_src[i] == 0xf)
				return -1;

			pipe_ctx = &res_ctx->pipe_ctx[id_src[i]];

			pipe_ctx->stream_res.tg = pool->timing_generators[tg_inst];
			pipe_ctx->plane_res.mi = pool->mis[id_src[i]];
			pipe_ctx->plane_res.hubp = pool->hubps[id_src[i]];
			pipe_ctx->plane_res.ipp = pool->ipps[id_src[i]];
			pipe_ctx->plane_res.xfm = pool->transforms[id_src[i]];
			pipe_ctx->plane_res.dpp = pool->dpps[id_src[i]];
			pipe_ctx->stream_res.opp = pool->opps[id_src[i]];

			if (pool->dpps[id_src[i]]) {
				pipe_ctx->plane_res.mpcc_inst = pool->dpps[id_src[i]]->inst;

				if (pool->mpc->funcs->read_mpcc_state) {
					struct mpcc_state s = {0};

					pool->mpc->funcs->read_mpcc_state(pool->mpc, pipe_ctx->plane_res.mpcc_inst, &s);

					if (s.dpp_id < MAX_MPCC)
						pool->mpc->mpcc_array[pipe_ctx->plane_res.mpcc_inst].dpp_id =
								s.dpp_id;

					if (s.bot_mpcc_id < MAX_MPCC)
						pool->mpc->mpcc_array[pipe_ctx->plane_res.mpcc_inst].mpcc_bot =
								&pool->mpc->mpcc_array[s.bot_mpcc_id];

					if (s.opp_id < MAX_OPP)
						pipe_ctx->stream_res.opp->mpc_tree_params.opp_id = s.opp_id;
				}
			}
			pipe_ctx->pipe_idx = id_src[i];

			if (id_src[i] >= pool->timing_generator_count) {
				id_src[i] = pool->timing_generator_count - 1;

				pipe_ctx->stream_res.tg = pool->timing_generators[id_src[i]];
				pipe_ctx->stream_res.opp = pool->opps[id_src[i]];
			}

			pipe_ctx->stream = stream;
		}

		if (numPipes == 2) {
			stream->apply_boot_odm_mode = dm_odm_combine_policy_2to1;
			res_ctx->pipe_ctx[id_src[0]].next_odm_pipe = &res_ctx->pipe_ctx[id_src[1]];
			res_ctx->pipe_ctx[id_src[0]].prev_odm_pipe = NULL;
			res_ctx->pipe_ctx[id_src[1]].next_odm_pipe = NULL;
			res_ctx->pipe_ctx[id_src[1]].prev_odm_pipe = &res_ctx->pipe_ctx[id_src[0]];
		} else
			stream->apply_boot_odm_mode = dm_odm_combine_mode_disabled;

		return id_src[0];
	}

	return -1;
}

static void mark_seamless_boot_stream(
		const struct dc  *dc,
		struct dc_stream_state *stream)
{
	struct dc_bios *dcb = dc->ctx->dc_bios;

	if (dc->config.allow_seamless_boot_optimization &&
			!dcb->funcs->is_accelerated_mode(dcb)) {
		if (dc_validate_boot_timing(dc, stream->sink, &stream->timing))
			stream->apply_seamless_boot_optimization = true;
	}
}

/*
 * Acquire a pipe as OTG master and assign to the stream in new dc context.
 * return - true if OTG master pipe is acquired and new dc context is updated.
 * false if it fails to acquire an OTG master pipe for this stream.
 *
 * In the example below, we acquired pipe 0 as OTG master pipe for the stream.
 * After the function its Inter-pipe Relation is represented by the diagram
 * below.
 *
 *       Inter-pipe Relation
 *        __________________________________________________
 *       |PIPE IDX|   DPP PIPES   | OPP HEADS | OTG MASTER  |
 *       |        |               |           |             |
 *       |   0    |               |blank ------------------ |
 *       |________|_______________|___________|_____________|
 */
static bool acquire_otg_master_pipe_for_stream(
		const struct dc_state *cur_ctx,
		struct dc_state *new_ctx,
		const struct resource_pool *pool,
		struct dc_stream_state *stream)
{
	/* TODO: Move this function to DCN specific resource file and acquire
	 * DSC resource here. The reason is that the function should have the
	 * same level of responsibility as when we acquire secondary OPP head.
	 * We acquire DSC when we acquire secondary OPP head, so we should
	 * acquire DSC when we acquire OTG master.
	 */
	int pipe_idx;
	struct pipe_ctx *pipe_ctx = NULL;

	/*
	 * Upper level code is responsible to optimize unnecessary addition and
	 * removal for unchanged streams. So unchanged stream will keep the same
	 * OTG master instance allocated. When current stream is removed and a
	 * new stream is added, we want to reuse the OTG instance made available
	 * by the removed stream first. If not found, we try to avoid of using
	 * any free pipes already used in current context as this could tear
	 * down exiting ODM/MPC/MPO configuration unnecessarily.
	 */

	/*
	 * Try to acquire the same OTG master already in use. This is not
	 * optimal because resetting an enabled OTG master pipe for a new stream
	 * requires an extra frame of wait. However there are test automation
	 * and eDP assumptions that rely on reusing the same OTG master pipe
	 * during mode change. We have to keep this logic as is for now.
	 */
	pipe_idx = recource_find_free_pipe_used_as_otg_master_in_cur_res_ctx(
			&cur_ctx->res_ctx, &new_ctx->res_ctx, pool);
	/*
	 * Try to acquire a pipe not used in current resource context to avoid
	 * pipe swapping.
	 */
	if (pipe_idx == FREE_PIPE_INDEX_NOT_FOUND)
		pipe_idx = recource_find_free_pipe_not_used_in_cur_res_ctx(
				&cur_ctx->res_ctx, &new_ctx->res_ctx, pool);
	/*
	 * If pipe swapping is unavoidable, try to acquire pipe used as
	 * secondary DPP pipe in current state as we prioritize to support more
	 * streams over supporting MPO planes.
	 */
	if (pipe_idx == FREE_PIPE_INDEX_NOT_FOUND)
		pipe_idx = resource_find_free_pipe_used_as_cur_sec_dpp(
				&cur_ctx->res_ctx, &new_ctx->res_ctx, pool);
	if (pipe_idx == FREE_PIPE_INDEX_NOT_FOUND)
		pipe_idx = resource_find_any_free_pipe(&new_ctx->res_ctx, pool);
	if (pipe_idx != FREE_PIPE_INDEX_NOT_FOUND) {
		pipe_ctx = &new_ctx->res_ctx.pipe_ctx[pipe_idx];
		memset(pipe_ctx, 0, sizeof(*pipe_ctx));
		pipe_ctx->pipe_idx = pipe_idx;
		pipe_ctx->stream_res.tg = pool->timing_generators[pipe_idx];
		pipe_ctx->plane_res.mi = pool->mis[pipe_idx];
		pipe_ctx->plane_res.hubp = pool->hubps[pipe_idx];
		pipe_ctx->plane_res.ipp = pool->ipps[pipe_idx];
		pipe_ctx->plane_res.xfm = pool->transforms[pipe_idx];
		pipe_ctx->plane_res.dpp = pool->dpps[pipe_idx];
		pipe_ctx->stream_res.opp = pool->opps[pipe_idx];
		if (pool->dpps[pipe_idx])
			pipe_ctx->plane_res.mpcc_inst = pool->dpps[pipe_idx]->inst;

		if (pipe_idx >= pool->timing_generator_count && pool->timing_generator_count != 0) {
			int tg_inst = pool->timing_generator_count - 1;

			pipe_ctx->stream_res.tg = pool->timing_generators[tg_inst];
			pipe_ctx->stream_res.opp = pool->opps[tg_inst];
		}

		pipe_ctx->stream = stream;
	} else {
		pipe_idx = acquire_first_split_pipe(&new_ctx->res_ctx, pool, stream);
	}

	return pipe_idx != FREE_PIPE_INDEX_NOT_FOUND;
}

enum dc_status resource_map_pool_resources(
		const struct dc  *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	const struct resource_pool *pool = dc->res_pool;
	int i;
	struct dc_context *dc_ctx = dc->ctx;
	struct pipe_ctx *pipe_ctx = NULL;
	int pipe_idx = -1;
	bool acquired = false;

	calculate_phy_pix_clks(stream);

	mark_seamless_boot_stream(dc, stream);

	if (stream->apply_seamless_boot_optimization) {
		pipe_idx = acquire_resource_from_hw_enabled_state(
				&context->res_ctx,
				pool,
				stream);
		if (pipe_idx < 0)
			/* hw resource was assigned to other stream */
			stream->apply_seamless_boot_optimization = false;
		else
			acquired = true;
	}

	if (!acquired)
		/* acquire new resources */
		acquired = acquire_otg_master_pipe_for_stream(dc->current_state,
				context, pool, stream);

	pipe_ctx = resource_get_otg_master_for_stream(&context->res_ctx, stream);

	if (!pipe_ctx || pipe_ctx->stream_res.tg == NULL)
		return DC_NO_CONTROLLER_RESOURCE;

	pipe_ctx->stream_res.stream_enc =
		dc->res_pool->funcs->find_first_free_match_stream_enc_for_link(
			&context->res_ctx, pool, stream);

	if (!pipe_ctx->stream_res.stream_enc)
		return DC_NO_STREAM_ENC_RESOURCE;

	update_stream_engine_usage(
		&context->res_ctx, pool,
		pipe_ctx->stream_res.stream_enc,
		true);

	/* Allocate DP HPO Stream Encoder based on signal, hw capabilities
	 * and link settings
	 */
	if (dc_is_dp_signal(stream->signal) ||
			dc_is_virtual_signal(stream->signal)) {
		if (!dc->link_srv->dp_decide_link_settings(stream,
				&pipe_ctx->link_config.dp_link_settings))
			return DC_FAIL_DP_LINK_BANDWIDTH;
		if (dc->link_srv->dp_get_encoding_format(
				&pipe_ctx->link_config.dp_link_settings) == DP_128b_132b_ENCODING) {
			pipe_ctx->stream_res.hpo_dp_stream_enc =
					find_first_free_match_hpo_dp_stream_enc_for_link(
							&context->res_ctx, pool, stream);

			if (!pipe_ctx->stream_res.hpo_dp_stream_enc)
				return DC_NO_STREAM_ENC_RESOURCE;

			update_hpo_dp_stream_engine_usage(
					&context->res_ctx, pool,
					pipe_ctx->stream_res.hpo_dp_stream_enc,
					true);
			if (!add_hpo_dp_link_enc_to_ctx(&context->res_ctx, pool, pipe_ctx, stream))
				return DC_NO_LINK_ENC_RESOURCE;
		}
	}

	/* TODO: Add check if ASIC support and EDID audio */
	if (!stream->converter_disable_audio &&
	    dc_is_audio_capable_signal(pipe_ctx->stream->signal) &&
	    stream->audio_info.mode_count && stream->audio_info.flags.all) {
		pipe_ctx->stream_res.audio = find_first_free_audio(
		&context->res_ctx, pool, pipe_ctx->stream_res.stream_enc->id, dc_ctx->dce_version);

		/*
		 * Audio assigned in order first come first get.
		 * There are asics which has number of audio
		 * resources less then number of pipes
		 */
		if (pipe_ctx->stream_res.audio)
			update_audio_usage(&context->res_ctx, pool,
					   pipe_ctx->stream_res.audio, true);
	}

	/* Add ABM to the resource if on EDP */
	if (pipe_ctx->stream && dc_is_embedded_signal(pipe_ctx->stream->signal)) {
		if (pool->abm)
			pipe_ctx->stream_res.abm = pool->abm;
		else
			pipe_ctx->stream_res.abm = pool->multiple_abms[pipe_ctx->stream_res.tg->inst];
	}

	for (i = 0; i < context->stream_count; i++)
		if (context->streams[i] == stream) {
			context->stream_status[i].primary_otg_inst = pipe_ctx->stream_res.tg->inst;
			context->stream_status[i].stream_enc_inst = pipe_ctx->stream_res.stream_enc->stream_enc_inst;
			context->stream_status[i].audio_inst =
				pipe_ctx->stream_res.audio ? pipe_ctx->stream_res.audio->inst : -1;

			return DC_OK;
		}

	DC_ERROR("Stream %p not found in new ctx!\n", stream);
	return DC_ERROR_UNEXPECTED;
}

bool dc_resource_is_dsc_encoding_supported(const struct dc *dc)
{
	if (dc->res_pool == NULL)
		return false;

	return dc->res_pool->res_cap->num_dsc > 0;
}

static bool planes_changed_for_existing_stream(struct dc_state *context,
					       struct dc_stream_state *stream,
					       const struct dc_validation_set set[],
					       int set_count)
{
	int i, j;
	struct dc_stream_status *stream_status = NULL;

	for (i = 0; i < context->stream_count; i++) {
		if (context->streams[i] == stream) {
			stream_status = &context->stream_status[i];
			break;
		}
	}

	if (!stream_status) {
		ASSERT(0);
		return false;
	}

	for (i = 0; i < set_count; i++)
		if (set[i].stream == stream)
			break;

	if (i == set_count)
		ASSERT(0);

	if (set[i].plane_count != stream_status->plane_count)
		return true;

	for (j = 0; j < set[i].plane_count; j++)
		if (set[i].plane_states[j] != stream_status->plane_states[j])
			return true;

	return false;
}

static bool add_all_planes_for_stream(
		const struct dc *dc,
		struct dc_stream_state *stream,
		const struct dc_validation_set set[],
		int set_count,
		struct dc_state *state)
{
	int i, j;

	for (i = 0; i < set_count; i++)
		if (set[i].stream == stream)
			break;

	if (i == set_count) {
		dm_error("Stream %p not found in set!\n", stream);
		return false;
	}

	for (j = 0; j < set[i].plane_count; j++)
		if (!dc_state_add_plane(dc, stream, set[i].plane_states[j], state))
			return false;

	return true;
}

/**
 * dc_validate_with_context - Validate and update the potential new stream in the context object
 *
 * @dc: Used to get the current state status
 * @set: An array of dc_validation_set with all the current streams reference
 * @set_count: Total of streams
 * @context: New context
 * @fast_validate: Enable or disable fast validation
 *
 * This function updates the potential new stream in the context object. It
 * creates multiple lists for the add, remove, and unchanged streams. In
 * particular, if the unchanged streams have a plane that changed, it is
 * necessary to remove all planes from the unchanged streams. In summary, this
 * function is responsible for validating the new context.
 *
 * Return:
 * In case of success, return DC_OK (1), otherwise, return a DC error.
 */
enum dc_status dc_validate_with_context(struct dc *dc,
					const struct dc_validation_set set[],
					int set_count,
					struct dc_state *context,
					bool fast_validate)
{
	struct dc_stream_state *unchanged_streams[MAX_PIPES] = { 0 };
	struct dc_stream_state *del_streams[MAX_PIPES] = { 0 };
	struct dc_stream_state *add_streams[MAX_PIPES] = { 0 };
	int old_stream_count = context->stream_count;
	enum dc_status res = DC_ERROR_UNEXPECTED;
	int unchanged_streams_count = 0;
	int del_streams_count = 0;
	int add_streams_count = 0;
	bool found = false;
	int i, j, k;

	DC_LOGGER_INIT(dc->ctx->logger);

	/* First build a list of streams to be remove from current context */
	for (i = 0; i < old_stream_count; i++) {
		struct dc_stream_state *stream = context->streams[i];

		for (j = 0; j < set_count; j++) {
			if (stream == set[j].stream) {
				found = true;
				break;
			}
		}

		if (!found)
			del_streams[del_streams_count++] = stream;

		found = false;
	}

	/* Second, build a list of new streams */
	for (i = 0; i < set_count; i++) {
		struct dc_stream_state *stream = set[i].stream;

		for (j = 0; j < old_stream_count; j++) {
			if (stream == context->streams[j]) {
				found = true;
				break;
			}
		}

		if (!found)
			add_streams[add_streams_count++] = stream;

		found = false;
	}

	/* Build a list of unchanged streams which is necessary for handling
	 * planes change such as added, removed, and updated.
	 */
	for (i = 0; i < set_count; i++) {
		/* Check if stream is part of the delete list */
		for (j = 0; j < del_streams_count; j++) {
			if (set[i].stream == del_streams[j]) {
				found = true;
				break;
			}
		}

		if (!found) {
			/* Check if stream is part of the add list */
			for (j = 0; j < add_streams_count; j++) {
				if (set[i].stream == add_streams[j]) {
					found = true;
					break;
				}
			}
		}

		if (!found)
			unchanged_streams[unchanged_streams_count++] = set[i].stream;

		found = false;
	}

	/* Remove all planes for unchanged streams if planes changed */
	for (i = 0; i < unchanged_streams_count; i++) {
		if (planes_changed_for_existing_stream(context,
						       unchanged_streams[i],
						       set,
						       set_count)) {

			if (!dc_state_rem_all_planes_for_stream(dc,
							  unchanged_streams[i],
							  context)) {
				res = DC_FAIL_DETACH_SURFACES;
				goto fail;
			}
		}
	}

	/* Remove all planes for removed streams and then remove the streams */
	for (i = 0; i < del_streams_count; i++) {
		/* Need to cpy the dwb data from the old stream in order to efc to work */
		if (del_streams[i]->num_wb_info > 0) {
			for (j = 0; j < add_streams_count; j++) {
				if (del_streams[i]->sink == add_streams[j]->sink) {
					add_streams[j]->num_wb_info = del_streams[i]->num_wb_info;
					for (k = 0; k < del_streams[i]->num_wb_info; k++)
						add_streams[j]->writeback_info[k] = del_streams[i]->writeback_info[k];
				}
			}
		}

		if (dc_state_get_stream_subvp_type(context, del_streams[i]) == SUBVP_PHANTOM) {
			/* remove phantoms specifically */
			if (!dc_state_rem_all_phantom_planes_for_stream(dc, del_streams[i], context, true)) {
				res = DC_FAIL_DETACH_SURFACES;
				goto fail;
			}

			res = dc_state_remove_phantom_stream(dc, context, del_streams[i]);
			dc_state_release_phantom_stream(dc, context, del_streams[i]);
		} else {
			if (!dc_state_rem_all_planes_for_stream(dc, del_streams[i], context)) {
				res = DC_FAIL_DETACH_SURFACES;
				goto fail;
			}

			res = dc_state_remove_stream(dc, context, del_streams[i]);
		}

		if (res != DC_OK)
			goto fail;
	}

	/* Swap seamless boot stream to pipe 0 (if needed) to ensure pipe_ctx
	 * matches. This may change in the future if seamless_boot_stream can be
	 * multiple.
	 */
	for (i = 0; i < add_streams_count; i++) {
		mark_seamless_boot_stream(dc, add_streams[i]);
		if (add_streams[i]->apply_seamless_boot_optimization && i != 0) {
			struct dc_stream_state *temp = add_streams[0];

			add_streams[0] = add_streams[i];
			add_streams[i] = temp;
			break;
		}
	}

	/* Add new streams and then add all planes for the new stream */
	for (i = 0; i < add_streams_count; i++) {
		calculate_phy_pix_clks(add_streams[i]);
		res = dc_state_add_stream(dc, context, add_streams[i]);
		if (res != DC_OK)
			goto fail;

		if (!add_all_planes_for_stream(dc, add_streams[i], set, set_count, context)) {
			res = DC_FAIL_ATTACH_SURFACES;
			goto fail;
		}
	}

	/* Add all planes for unchanged streams if planes changed */
	for (i = 0; i < unchanged_streams_count; i++) {
		if (planes_changed_for_existing_stream(context,
						       unchanged_streams[i],
						       set,
						       set_count)) {
			if (!add_all_planes_for_stream(dc, unchanged_streams[i], set, set_count, context)) {
				res = DC_FAIL_ATTACH_SURFACES;
				goto fail;
			}
		}
	}

	res = dc_validate_global_state(dc, context, fast_validate);

	/* calculate pixel rate divider after deciding pxiel clock & odm combine  */
	if ((dc->hwss.calculate_pix_rate_divider) && (res == DC_OK)) {
		for (i = 0; i < add_streams_count; i++)
			dc->hwss.calculate_pix_rate_divider(dc, context, add_streams[i]);
	}

fail:
	if (res != DC_OK)
		DC_LOG_WARNING("%s:resource validation failed, dc_status:%d\n",
			       __func__,
			       res);

	return res;
}

/**
 * decide_hblank_borrow - Decides the horizontal blanking borrow value for a given pipe context.
 * @pipe_ctx: Pointer to the pipe context structure.
 *
 * This function calculates the horizontal blanking borrow value for a given pipe context based on the
 * display stream compression (DSC) configuration. If the horizontal active pixels (hactive) are less
 * than the total width of the DSC slices, it sets the hblank_borrow value to the difference. If the
 * total horizontal timing minus the hblank_borrow value is less than 32, it resets the hblank_borrow
 * value to 0.
 */
static void decide_hblank_borrow(struct pipe_ctx *pipe_ctx)
{
	uint32_t hactive;
	uint32_t ceil_slice_width;
	struct dc_stream_state *stream = NULL;

	if (!pipe_ctx)
		return;

	stream = pipe_ctx->stream;

	if (stream->timing.flags.DSC) {
		hactive = stream->timing.h_addressable + stream->timing.h_border_left + stream->timing.h_border_right;

		/* Assume if determined slices does not divide Hactive evenly, Hborrow is needed for padding*/
		if (hactive % stream->timing.dsc_cfg.num_slices_h != 0) {
			ceil_slice_width = (hactive / stream->timing.dsc_cfg.num_slices_h) + 1;
			pipe_ctx->hblank_borrow = ceil_slice_width * stream->timing.dsc_cfg.num_slices_h - hactive;

			if (stream->timing.h_total - hactive - pipe_ctx->hblank_borrow < 32)
				pipe_ctx->hblank_borrow = 0;
		}
	}
}

/**
 * dc_validate_global_state() - Determine if hardware can support a given state
 *
 * @dc: dc struct for this driver
 * @new_ctx: state to be validated
 * @fast_validate: set to true if only yes/no to support matters
 *
 * Checks hardware resource availability and bandwidth requirement.
 *
 * Return:
 * DC_OK if the result can be programmed. Otherwise, an error code.
 */
enum dc_status dc_validate_global_state(
		struct dc *dc,
		struct dc_state *new_ctx,
		bool fast_validate)
{
	enum dc_status result = DC_ERROR_UNEXPECTED;
	int i, j;

	if (!new_ctx)
		return DC_ERROR_UNEXPECTED;

	if (dc->res_pool->funcs->validate_global) {
		result = dc->res_pool->funcs->validate_global(dc, new_ctx);
		if (result != DC_OK)
			return result;
	}

	for (i = 0; i < new_ctx->stream_count; i++) {
		struct dc_stream_state *stream = new_ctx->streams[i];

		for (j = 0; j < dc->res_pool->pipe_count; j++) {
			struct pipe_ctx *pipe_ctx = &new_ctx->res_ctx.pipe_ctx[j];

			if (pipe_ctx->stream != stream)
				continue;

			/* Decide whether hblank borrow is needed and save it in pipe_ctx */
			if (dc->debug.enable_hblank_borrow)
				decide_hblank_borrow(pipe_ctx);

			if (dc->res_pool->funcs->patch_unknown_plane_state &&
					pipe_ctx->plane_state &&
					pipe_ctx->plane_state->tiling_info.gfx9.swizzle == DC_SW_UNKNOWN) {
				result = dc->res_pool->funcs->patch_unknown_plane_state(pipe_ctx->plane_state);
				if (result != DC_OK)
					return result;
			}

			/* Switch to dp clock source only if there is
			 * no non dp stream that shares the same timing
			 * with the dp stream.
			 */
			if (dc_is_dp_signal(pipe_ctx->stream->signal) &&
				!find_pll_sharable_stream(stream, new_ctx)) {

				resource_unreference_clock_source(
						&new_ctx->res_ctx,
						dc->res_pool,
						pipe_ctx->clock_source);

				pipe_ctx->clock_source = dc->res_pool->dp_clock_source;
				resource_reference_clock_source(
						&new_ctx->res_ctx,
						dc->res_pool,
						 pipe_ctx->clock_source);
			}
		}
	}

	result = resource_build_scaling_params_for_context(dc, new_ctx);

	if (result == DC_OK)
		if (!dc->res_pool->funcs->validate_bandwidth(dc, new_ctx, fast_validate))
			result = DC_FAIL_BANDWIDTH_VALIDATE;

	return result;
}

static void patch_gamut_packet_checksum(
		struct dc_info_packet *gamut_packet)
{
	/* For gamut we recalc checksum */
	if (gamut_packet->valid) {
		uint8_t chk_sum = 0;
		uint8_t *ptr;
		uint8_t i;

		/*start of the Gamut data. */
		ptr = &gamut_packet->sb[3];

		for (i = 0; i <= gamut_packet->sb[1]; i++)
			chk_sum += ptr[i];

		gamut_packet->sb[2] = (uint8_t) (0x100 - chk_sum);
	}
}

static void set_avi_info_frame(
		struct dc_info_packet *info_packet,
		struct pipe_ctx *pipe_ctx)
{
	struct dc_stream_state *stream = pipe_ctx->stream;
	enum dc_color_space color_space = COLOR_SPACE_UNKNOWN;
	uint32_t pixel_encoding = 0;
	enum scanning_type scan_type = SCANNING_TYPE_NODATA;
	enum dc_aspect_ratio aspect = ASPECT_RATIO_NO_DATA;
	uint8_t *check_sum = NULL;
	uint8_t byte_index = 0;
	union hdmi_info_packet hdmi_info;
	unsigned int vic = pipe_ctx->stream->timing.vic;
	unsigned int rid = pipe_ctx->stream->timing.rid;
	unsigned int fr_ind = pipe_ctx->stream->timing.fr_index;
	enum dc_timing_3d_format format;

	memset(&hdmi_info, 0, sizeof(union hdmi_info_packet));

	color_space = pipe_ctx->stream->output_color_space;
	if (color_space == COLOR_SPACE_UNKNOWN)
		color_space = (stream->timing.pixel_encoding == PIXEL_ENCODING_RGB) ?
			COLOR_SPACE_SRGB:COLOR_SPACE_YCBCR709;

	/* Initialize header */
	hdmi_info.bits.header.info_frame_type = HDMI_INFOFRAME_TYPE_AVI;
	/* InfoFrameVersion_3 is defined by CEA861F (Section 6.4), but shall
	* not be used in HDMI 2.0 (Section 10.1) */
	hdmi_info.bits.header.version = 2;
	hdmi_info.bits.header.length = HDMI_AVI_INFOFRAME_SIZE;

	/*
	 * IDO-defined (Y2,Y1,Y0 = 1,1,1) shall not be used by devices built
	 * according to HDMI 2.0 spec (Section 10.1)
	 */

	switch (stream->timing.pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		pixel_encoding = 1;
		break;

	case PIXEL_ENCODING_YCBCR444:
		pixel_encoding = 2;
		break;
	case PIXEL_ENCODING_YCBCR420:
		pixel_encoding = 3;
		break;

	case PIXEL_ENCODING_RGB:
	default:
		pixel_encoding = 0;
	}

	/* Y0_Y1_Y2 : The pixel encoding */
	/* H14b AVI InfoFrame has extension on Y-field from 2 bits to 3 bits */
	hdmi_info.bits.Y0_Y1_Y2 = pixel_encoding;

	/* A0 = 1 Active Format Information valid */
	hdmi_info.bits.A0 = ACTIVE_FORMAT_VALID;

	/* B0, B1 = 3; Bar info data is valid */
	hdmi_info.bits.B0_B1 = BAR_INFO_BOTH_VALID;

	hdmi_info.bits.SC0_SC1 = PICTURE_SCALING_UNIFORM;

	/* S0, S1 : Underscan / Overscan */
	/* TODO: un-hardcode scan type */
	scan_type = SCANNING_TYPE_UNDERSCAN;
	hdmi_info.bits.S0_S1 = scan_type;

	/* C0, C1 : Colorimetry */
	switch (color_space) {
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR709_LIMITED:
		hdmi_info.bits.C0_C1 = COLORIMETRY_ITU709;
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR601_LIMITED:
		hdmi_info.bits.C0_C1 = COLORIMETRY_ITU601;
		break;
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	case COLOR_SPACE_2020_YCBCR:
		hdmi_info.bits.EC0_EC2 = COLORIMETRYEX_BT2020RGBYCBCR;
		hdmi_info.bits.C0_C1   = COLORIMETRY_EXTENDED;
		break;
	case COLOR_SPACE_ADOBERGB:
		hdmi_info.bits.EC0_EC2 = COLORIMETRYEX_ADOBERGB;
		hdmi_info.bits.C0_C1   = COLORIMETRY_EXTENDED;
		break;
	case COLOR_SPACE_SRGB:
	default:
		hdmi_info.bits.C0_C1 = COLORIMETRY_NO_DATA;
		break;
	}

	if (pixel_encoding && color_space == COLOR_SPACE_2020_YCBCR &&
			stream->out_transfer_func.tf == TRANSFER_FUNCTION_GAMMA22) {
		hdmi_info.bits.EC0_EC2 = 0;
		hdmi_info.bits.C0_C1 = COLORIMETRY_ITU709;
	}

	/* TODO: un-hardcode aspect ratio */
	aspect = stream->timing.aspect_ratio;

	switch (aspect) {
	case ASPECT_RATIO_4_3:
	case ASPECT_RATIO_16_9:
		hdmi_info.bits.M0_M1 = aspect;
		break;

	case ASPECT_RATIO_NO_DATA:
	case ASPECT_RATIO_64_27:
	case ASPECT_RATIO_256_135:
	default:
		hdmi_info.bits.M0_M1 = 0;
	}

	/* Active Format Aspect ratio - same as Picture Aspect Ratio. */
	hdmi_info.bits.R0_R3 = ACTIVE_FORMAT_ASPECT_RATIO_SAME_AS_PICTURE;

	switch (stream->content_type) {
	case DISPLAY_CONTENT_TYPE_NO_DATA:
		hdmi_info.bits.CN0_CN1 = 0;
		hdmi_info.bits.ITC = 1;
		break;
	case DISPLAY_CONTENT_TYPE_GRAPHICS:
		hdmi_info.bits.CN0_CN1 = 0;
		hdmi_info.bits.ITC = 1;
		break;
	case DISPLAY_CONTENT_TYPE_PHOTO:
		hdmi_info.bits.CN0_CN1 = 1;
		hdmi_info.bits.ITC = 1;
		break;
	case DISPLAY_CONTENT_TYPE_CINEMA:
		hdmi_info.bits.CN0_CN1 = 2;
		hdmi_info.bits.ITC = 1;
		break;
	case DISPLAY_CONTENT_TYPE_GAME:
		hdmi_info.bits.CN0_CN1 = 3;
		hdmi_info.bits.ITC = 1;
		break;
	}

	if (stream->qs_bit == 1) {
		if (color_space == COLOR_SPACE_SRGB ||
			color_space == COLOR_SPACE_2020_RGB_FULLRANGE)
			hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_FULL_RANGE;
		else if (color_space == COLOR_SPACE_SRGB_LIMITED ||
					color_space == COLOR_SPACE_2020_RGB_LIMITEDRANGE)
			hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_LIMITED_RANGE;
		else
			hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_DEFAULT_RANGE;
	} else
		hdmi_info.bits.Q0_Q1   = RGB_QUANTIZATION_DEFAULT_RANGE;

	/* TODO : We should handle YCC quantization */
	/* but we do not have matrix calculation */
	hdmi_info.bits.YQ0_YQ1 = YYC_QUANTIZATION_LIMITED_RANGE;

	///VIC
	if (pipe_ctx->stream->timing.hdmi_vic != 0)
		vic = 0;
	format = stream->timing.timing_3d_format;
	/*todo, add 3DStereo support*/
	if (format != TIMING_3D_FORMAT_NONE) {
		// Based on HDMI specs hdmi vic needs to be converted to cea vic when 3D is enabled
		switch (pipe_ctx->stream->timing.hdmi_vic) {
		case 1:
			vic = 95;
			break;
		case 2:
			vic = 94;
			break;
		case 3:
			vic = 93;
			break;
		case 4:
			vic = 98;
			break;
		default:
			break;
		}
	}
	/* If VIC >= 128, the Source shall use AVI InfoFrame Version 3*/
	hdmi_info.bits.VIC0_VIC7 = vic;
	if (vic >= 128)
		hdmi_info.bits.header.version = 3;
	/* If (C1, C0)=(1, 1) and (EC2, EC1, EC0)=(1, 1, 1),
	 * the Source shall use 20 AVI InfoFrame Version 4
	 */
	if (hdmi_info.bits.C0_C1 == COLORIMETRY_EXTENDED &&
			hdmi_info.bits.EC0_EC2 == COLORIMETRYEX_RESERVED) {
		hdmi_info.bits.header.version = 4;
		hdmi_info.bits.header.length = 14;
	}

	if (rid != 0 && fr_ind != 0) {
		hdmi_info.bits.header.version = 4;
		hdmi_info.bits.header.length = 15;

		hdmi_info.bits.FR0_FR3 = fr_ind & 0xF;
		hdmi_info.bits.FR4 = (fr_ind >> 4) & 0x1;
		hdmi_info.bits.RID0_RID5 = rid;
	}

	/* pixel repetition
	 * PR0 - PR3 start from 0 whereas pHwPathMode->mode.timing.flags.pixel
	 * repetition start from 1 */
	hdmi_info.bits.PR0_PR3 = 0;

	/* Bar Info
	 * barTop:    Line Number of End of Top Bar.
	 * barBottom: Line Number of Start of Bottom Bar.
	 * barLeft:   Pixel Number of End of Left Bar.
	 * barRight:  Pixel Number of Start of Right Bar. */
	hdmi_info.bits.bar_top = stream->timing.v_border_top;
	hdmi_info.bits.bar_bottom = (stream->timing.v_total
			- stream->timing.v_border_bottom + 1);
	hdmi_info.bits.bar_left  = stream->timing.h_border_left;
	hdmi_info.bits.bar_right = (stream->timing.h_total
			- stream->timing.h_border_right + 1);

    /* Additional Colorimetry Extension
     * Used in conduction with C0-C1 and EC0-EC2
     * 0 = DCI-P3 RGB (D65)
     * 1 = DCI-P3 RGB (theater)
     */
	hdmi_info.bits.ACE0_ACE3 = 0;

	/* check_sum - Calculate AFMT_AVI_INFO0 ~ AFMT_AVI_INFO3 */
	check_sum = &hdmi_info.packet_raw_data.sb[0];

	*check_sum = HDMI_INFOFRAME_TYPE_AVI + hdmi_info.bits.header.length + hdmi_info.bits.header.version;

	for (byte_index = 1; byte_index <= hdmi_info.bits.header.length; byte_index++)
		*check_sum += hdmi_info.packet_raw_data.sb[byte_index];

	/* one byte complement */
	*check_sum = (uint8_t) (0x100 - *check_sum);

	/* Store in hw_path_mode */
	info_packet->hb0 = hdmi_info.packet_raw_data.hb0;
	info_packet->hb1 = hdmi_info.packet_raw_data.hb1;
	info_packet->hb2 = hdmi_info.packet_raw_data.hb2;

	for (byte_index = 0; byte_index < sizeof(hdmi_info.packet_raw_data.sb); byte_index++)
		info_packet->sb[byte_index] = hdmi_info.packet_raw_data.sb[byte_index];

	info_packet->valid = true;
}

static void set_vendor_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	/* SPD info packet for FreeSync */

	/* Check if Freesync is supported. Return if false. If true,
	 * set the corresponding bit in the info packet
	 */
	if (!stream->vsp_infopacket.valid)
		return;

	*info_packet = stream->vsp_infopacket;
}

static void set_spd_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	/* SPD info packet for FreeSync */

	/* Check if Freesync is supported. Return if false. If true,
	 * set the corresponding bit in the info packet
	 */
	if (!stream->vrr_infopacket.valid)
		return;

	*info_packet = stream->vrr_infopacket;
}

static void set_hdr_static_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	/* HDR Static Metadata info packet for HDR10 */

	if (!stream->hdr_static_metadata.valid ||
			stream->use_dynamic_meta)
		return;

	*info_packet = stream->hdr_static_metadata;
}

static void set_vsc_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	if (!stream->vsc_infopacket.valid)
		return;

	*info_packet = stream->vsc_infopacket;
}
static void set_hfvs_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	if (!stream->hfvsif_infopacket.valid)
		return;

	*info_packet = stream->hfvsif_infopacket;
}

static void adaptive_sync_override_dp_info_packets_sdp_line_num(
		const struct dc_crtc_timing *timing,
		struct enc_sdp_line_num *sdp_line_num,
		unsigned int vstartup_start)
{
	uint32_t asic_blank_start = 0;
	uint32_t asic_blank_end   = 0;
	uint32_t v_update = 0;

	const struct dc_crtc_timing *tg = timing;

	/* blank_start = frame end - front porch */
	asic_blank_start = tg->v_total - tg->v_front_porch;

	/* blank_end = blank_start - active */
	asic_blank_end = (asic_blank_start - tg->v_border_bottom -
						tg->v_addressable - tg->v_border_top);

	if (vstartup_start > asic_blank_end) {
		v_update = (tg->v_total - (vstartup_start - asic_blank_end));
		sdp_line_num->adaptive_sync_line_num_valid = true;
		sdp_line_num->adaptive_sync_line_num = (tg->v_total - v_update - 1);
	} else {
		sdp_line_num->adaptive_sync_line_num_valid = false;
		sdp_line_num->adaptive_sync_line_num = 0;
	}
}

static void set_adaptive_sync_info_packet(
		struct dc_info_packet *info_packet,
		const struct dc_stream_state *stream,
		struct encoder_info_frame *info_frame,
		unsigned int vstartup_start)
{
	if (!stream->adaptive_sync_infopacket.valid)
		return;

	adaptive_sync_override_dp_info_packets_sdp_line_num(
			&stream->timing,
			&info_frame->sdp_line_num,
			vstartup_start);

	*info_packet = stream->adaptive_sync_infopacket;
}

static void set_vtem_info_packet(
		struct dc_info_packet *info_packet,
		struct dc_stream_state *stream)
{
	if (!stream->vtem_infopacket.valid)
		return;

	*info_packet = stream->vtem_infopacket;
}

struct clock_source *dc_resource_find_first_free_pll(
		struct resource_context *res_ctx,
		const struct resource_pool *pool)
{
	int i;

	for (i = 0; i < pool->clk_src_count; ++i) {
		if (res_ctx->clock_source_ref_count[i] == 0)
			return pool->clock_sources[i];
	}

	return NULL;
}

void resource_build_info_frame(struct pipe_ctx *pipe_ctx)
{
	enum signal_type signal = SIGNAL_TYPE_NONE;
	struct encoder_info_frame *info = &pipe_ctx->stream_res.encoder_info_frame;
	unsigned int vstartup_start = 0;

	/* default all packets to invalid */
	info->avi.valid = false;
	info->gamut.valid = false;
	info->vendor.valid = false;
	info->spd.valid = false;
	info->hdrsmd.valid = false;
	info->vsc.valid = false;
	info->hfvsif.valid = false;
	info->vtem.valid = false;
	info->adaptive_sync.valid = false;
	signal = pipe_ctx->stream->signal;

	if (pipe_ctx->stream->ctx->dc->res_pool->funcs->get_vstartup_for_pipe)
		vstartup_start = pipe_ctx->stream->ctx->dc->res_pool->funcs->get_vstartup_for_pipe(pipe_ctx);

	/* HDMi and DP have different info packets*/
	if (dc_is_hdmi_signal(signal)) {
		set_avi_info_frame(&info->avi, pipe_ctx);

		set_vendor_info_packet(&info->vendor, pipe_ctx->stream);
		set_hfvs_info_packet(&info->hfvsif, pipe_ctx->stream);
		set_vtem_info_packet(&info->vtem, pipe_ctx->stream);

		set_spd_info_packet(&info->spd, pipe_ctx->stream);

		set_hdr_static_info_packet(&info->hdrsmd, pipe_ctx->stream);

	} else if (dc_is_dp_signal(signal)) {
		set_vsc_info_packet(&info->vsc, pipe_ctx->stream);

		set_spd_info_packet(&info->spd, pipe_ctx->stream);

		set_hdr_static_info_packet(&info->hdrsmd, pipe_ctx->stream);
		set_adaptive_sync_info_packet(&info->adaptive_sync,
										pipe_ctx->stream,
										info,
										vstartup_start);
	}

	patch_gamut_packet_checksum(&info->gamut);
}

enum dc_status resource_map_clock_resources(
		const struct dc  *dc,
		struct dc_state *context,
		struct dc_stream_state *stream)
{
	/* acquire new resources */
	const struct resource_pool *pool = dc->res_pool;
	struct pipe_ctx *pipe_ctx = resource_get_otg_master_for_stream(
				&context->res_ctx, stream);

	if (!pipe_ctx)
		return DC_ERROR_UNEXPECTED;

	if (dc_is_dp_signal(pipe_ctx->stream->signal)
		|| pipe_ctx->stream->signal == SIGNAL_TYPE_VIRTUAL)
		pipe_ctx->clock_source = pool->dp_clock_source;
	else {
		pipe_ctx->clock_source = NULL;

		if (!dc->config.disable_disp_pll_sharing)
			pipe_ctx->clock_source = resource_find_used_clk_src_for_sharing(
				&context->res_ctx,
				pipe_ctx);

		if (pipe_ctx->clock_source == NULL)
			pipe_ctx->clock_source =
				dc_resource_find_first_free_pll(
					&context->res_ctx,
					pool);
	}

	if (pipe_ctx->clock_source == NULL)
		return DC_NO_CLOCK_SOURCE_RESOURCE;

	resource_reference_clock_source(
		&context->res_ctx, pool,
		pipe_ctx->clock_source);

	return DC_OK;
}

/*
 * Note: We need to disable output if clock sources change,
 * since bios does optimization and doesn't apply if changing
 * PHY when not already disabled.
 */
bool pipe_need_reprogram(
		struct pipe_ctx *pipe_ctx_old,
		struct pipe_ctx *pipe_ctx)
{
	if (!pipe_ctx_old->stream)
		return false;

	if (pipe_ctx_old->stream->sink != pipe_ctx->stream->sink)
		return true;

	if (pipe_ctx_old->stream->signal != pipe_ctx->stream->signal)
		return true;

	if (pipe_ctx_old->stream_res.audio != pipe_ctx->stream_res.audio)
		return true;

	if (pipe_ctx_old->clock_source != pipe_ctx->clock_source
			&& pipe_ctx_old->stream != pipe_ctx->stream)
		return true;

	if (pipe_ctx_old->stream_res.stream_enc != pipe_ctx->stream_res.stream_enc)
		return true;

	if (dc_is_timing_changed(pipe_ctx_old->stream, pipe_ctx->stream))
		return true;

	if (pipe_ctx_old->stream->dpms_off != pipe_ctx->stream->dpms_off)
		return true;

	if (false == pipe_ctx_old->stream->link->link_state_valid &&
		false == pipe_ctx_old->stream->dpms_off)
		return true;

	if (pipe_ctx_old->stream_res.dsc != pipe_ctx->stream_res.dsc)
		return true;

	if (pipe_ctx_old->stream_res.hpo_dp_stream_enc != pipe_ctx->stream_res.hpo_dp_stream_enc)
		return true;
	if (pipe_ctx_old->link_res.hpo_dp_link_enc != pipe_ctx->link_res.hpo_dp_link_enc)
		return true;

	/* DIG link encoder resource assignment for stream changed. */
	if (pipe_ctx_old->stream->ctx->dc->res_pool->funcs->link_encs_assign) {
		bool need_reprogram = false;
		struct dc *dc = pipe_ctx_old->stream->ctx->dc;
		struct link_encoder *link_enc_prev =
			link_enc_cfg_get_link_enc_used_by_stream_current(dc, pipe_ctx_old->stream);

		if (link_enc_prev != pipe_ctx->stream->link_enc)
			need_reprogram = true;

		return need_reprogram;
	}

	return false;
}

void resource_build_bit_depth_reduction_params(struct dc_stream_state *stream,
		struct bit_depth_reduction_params *fmt_bit_depth)
{
	enum dc_dither_option option = stream->dither_option;
	enum dc_pixel_encoding pixel_encoding =
			stream->timing.pixel_encoding;

	memset(fmt_bit_depth, 0, sizeof(*fmt_bit_depth));

	if (option == DITHER_OPTION_DEFAULT) {
		switch (stream->timing.display_color_depth) {
		case COLOR_DEPTH_666:
			option = DITHER_OPTION_SPATIAL6;
			break;
		case COLOR_DEPTH_888:
			option = DITHER_OPTION_SPATIAL8;
			break;
		case COLOR_DEPTH_101010:
			option = DITHER_OPTION_TRUN10;
			break;
		default:
			option = DITHER_OPTION_DISABLE;
		}
	}

	if (option == DITHER_OPTION_DISABLE)
		return;

	if (option == DITHER_OPTION_TRUN6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 0;
	} else if (option == DITHER_OPTION_TRUN8 ||
			option == DITHER_OPTION_TRUN8_SPATIAL6 ||
			option == DITHER_OPTION_TRUN8_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 1;
	} else if (option == DITHER_OPTION_TRUN10        ||
			option == DITHER_OPTION_TRUN10_SPATIAL6   ||
			option == DITHER_OPTION_TRUN10_SPATIAL8   ||
			option == DITHER_OPTION_TRUN10_FM8     ||
			option == DITHER_OPTION_TRUN10_FM6     ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 2;
		if (option == DITHER_OPTION_TRUN10)
			fmt_bit_depth->flags.TRUNCATE_MODE = 1;
	}

	/* special case - Formatter can only reduce by 4 bits at most.
	 * When reducing from 12 to 6 bits,
	 * HW recommends we use trunc with round mode
	 * (if we did nothing, trunc to 10 bits would be used)
	 * note that any 12->10 bit reduction is ignored prior to DCE8,
	 * as the input was 10 bits.
	 */
	if (option == DITHER_OPTION_SPATIAL6_FRAME_RANDOM ||
			option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_FM6) {
		fmt_bit_depth->flags.TRUNCATE_ENABLED = 1;
		fmt_bit_depth->flags.TRUNCATE_DEPTH = 2;
		fmt_bit_depth->flags.TRUNCATE_MODE = 1;
	}

	/* spatial dither
	 * note that spatial modes 1-3 are never used
	 */
	if (option == DITHER_OPTION_SPATIAL6_FRAME_RANDOM            ||
			option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_TRUN10_SPATIAL6      ||
			option == DITHER_OPTION_TRUN8_SPATIAL6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 0;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (option == DITHER_OPTION_SPATIAL8_FRAME_RANDOM            ||
			option == DITHER_OPTION_SPATIAL8 ||
			option == DITHER_OPTION_SPATIAL8_FM6        ||
			option == DITHER_OPTION_TRUN10_SPATIAL8      ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 1;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	} else if (option == DITHER_OPTION_SPATIAL10_FRAME_RANDOM ||
			option == DITHER_OPTION_SPATIAL10 ||
			option == DITHER_OPTION_SPATIAL10_FM8 ||
			option == DITHER_OPTION_SPATIAL10_FM6) {
		fmt_bit_depth->flags.SPATIAL_DITHER_ENABLED = 1;
		fmt_bit_depth->flags.SPATIAL_DITHER_DEPTH = 2;
		fmt_bit_depth->flags.HIGHPASS_RANDOM = 1;
		fmt_bit_depth->flags.RGB_RANDOM =
				(pixel_encoding == PIXEL_ENCODING_RGB) ? 1 : 0;
	}

	if (option == DITHER_OPTION_SPATIAL6 ||
			option == DITHER_OPTION_SPATIAL8 ||
			option == DITHER_OPTION_SPATIAL10) {
		fmt_bit_depth->flags.FRAME_RANDOM = 0;
	} else {
		fmt_bit_depth->flags.FRAME_RANDOM = 1;
	}

	//////////////////////
	//// temporal dither
	//////////////////////
	if (option == DITHER_OPTION_FM6           ||
			option == DITHER_OPTION_SPATIAL8_FM6     ||
			option == DITHER_OPTION_SPATIAL10_FM6     ||
			option == DITHER_OPTION_TRUN10_FM6     ||
			option == DITHER_OPTION_TRUN8_FM6      ||
			option == DITHER_OPTION_TRUN10_SPATIAL8_FM6) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 0;
	} else if (option == DITHER_OPTION_FM8        ||
			option == DITHER_OPTION_SPATIAL10_FM8  ||
			option == DITHER_OPTION_TRUN10_FM8) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 1;
	} else if (option == DITHER_OPTION_FM10) {
		fmt_bit_depth->flags.FRAME_MODULATION_ENABLED = 1;
		fmt_bit_depth->flags.FRAME_MODULATION_DEPTH = 2;
	}

	fmt_bit_depth->pixel_encoding = pixel_encoding;
}

enum dc_status dc_validate_stream(struct dc *dc, struct dc_stream_state *stream)
{
	if (dc == NULL || stream == NULL)
		return DC_ERROR_UNEXPECTED;

	struct dc_link *link = stream->link;
	struct timing_generator *tg = dc->res_pool->timing_generators[0];
	enum dc_status res = DC_OK;

	calculate_phy_pix_clks(stream);

	if (!tg->funcs->validate_timing(tg, &stream->timing))
		res = DC_FAIL_CONTROLLER_VALIDATE;

	if (res == DC_OK) {
		if (link->ep_type == DISPLAY_ENDPOINT_PHY &&
				!link->link_enc->funcs->validate_output_with_stream(
						link->link_enc, stream))
			res = DC_FAIL_ENC_VALIDATE;
	}

	/* TODO: validate audio ASIC caps, encoder */

	if (res == DC_OK)
		res = dc->link_srv->validate_mode_timing(stream,
		      link,
		      &stream->timing);

	return res;
}

enum dc_status dc_validate_plane(struct dc *dc, const struct dc_plane_state *plane_state)
{
	enum dc_status res = DC_OK;

	/* check if surface has invalid dimensions */
	if (plane_state->src_rect.width == 0 || plane_state->src_rect.height == 0 ||
		plane_state->dst_rect.width == 0 || plane_state->dst_rect.height == 0)
		return DC_FAIL_SURFACE_VALIDATE;

	/* TODO For now validates pixel format only */
	if (dc->res_pool->funcs->validate_plane)
		return dc->res_pool->funcs->validate_plane(plane_state, &dc->caps);

	return res;
}

unsigned int resource_pixel_format_to_bpp(enum surface_pixel_format format)
{
	switch (format) {
	case SURFACE_PIXEL_FORMAT_GRPH_PALETA_256_COLORS:
		return 8;
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
		return 12;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB1555:
	case SURFACE_PIXEL_FORMAT_GRPH_RGB565:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr:
	case SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
		return 16;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010_XR_BIAS:
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE:
	case SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA:
		return 32;
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
	case SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
	case SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
		return 64;
	default:
		ASSERT_CRITICAL(false);
		return -1;
	}
}
static unsigned int get_max_audio_sample_rate(struct audio_mode *modes)
{
	if (modes) {
		if (modes->sample_rates.rate.RATE_192)
			return 192000;
		if (modes->sample_rates.rate.RATE_176_4)
			return 176400;
		if (modes->sample_rates.rate.RATE_96)
			return 96000;
		if (modes->sample_rates.rate.RATE_88_2)
			return 88200;
		if (modes->sample_rates.rate.RATE_48)
			return 48000;
		if (modes->sample_rates.rate.RATE_44_1)
			return 44100;
		if (modes->sample_rates.rate.RATE_32)
			return 32000;
	}
	/*original logic when no audio info*/
	return 441000;
}

void get_audio_check(struct audio_info *aud_modes,
	struct audio_check *audio_chk)
{
	unsigned int i;
	unsigned int max_sample_rate = 0;

	if (aud_modes) {
		audio_chk->audio_packet_type = 0x2;/*audio sample packet AP = .25 for layout0, 1 for layout1*/

		audio_chk->max_audiosample_rate = 0;
		for (i = 0; i < aud_modes->mode_count; i++) {
			max_sample_rate = get_max_audio_sample_rate(&aud_modes->modes[i]);
			if (audio_chk->max_audiosample_rate < max_sample_rate)
				audio_chk->max_audiosample_rate = max_sample_rate;
			/*dts takes the same as type 2: AP = 0.25*/
		}
		/*check which one take more bandwidth*/
		if (audio_chk->max_audiosample_rate > 192000)
			audio_chk->audio_packet_type = 0x9;/*AP =1*/
		audio_chk->acat = 0;/*not support*/
	}
}

static struct hpo_dp_link_encoder *get_temp_hpo_dp_link_enc(
		const struct resource_context *res_ctx,
		const struct resource_pool *const pool,
		const struct dc_link *link)
{
	struct hpo_dp_link_encoder *hpo_dp_link_enc = NULL;
	int enc_index;

	enc_index = find_acquired_hpo_dp_link_enc_for_link(res_ctx, link);

	if (enc_index < 0)
		enc_index = find_free_hpo_dp_link_enc(res_ctx, pool);

	if (enc_index >= 0)
		hpo_dp_link_enc = pool->hpo_dp_link_enc[enc_index];

	return hpo_dp_link_enc;
}

bool get_temp_dp_link_res(struct dc_link *link,
		struct link_resource *link_res,
		struct dc_link_settings *link_settings)
{
	const struct dc *dc  = link->dc;
	const struct resource_context *res_ctx = &dc->current_state->res_ctx;

	memset(link_res, 0, sizeof(*link_res));

	if (dc->link_srv->dp_get_encoding_format(link_settings) == DP_128b_132b_ENCODING) {
		link_res->hpo_dp_link_enc = get_temp_hpo_dp_link_enc(res_ctx,
				dc->res_pool, link);
		if (!link_res->hpo_dp_link_enc)
			return false;
	}
	return true;
}

void reset_syncd_pipes_from_disabled_pipes(struct dc *dc,
		struct dc_state *context)
{
	int i, j;
	struct pipe_ctx *pipe_ctx_old, *pipe_ctx, *pipe_ctx_syncd;

	/* If pipe backend is reset, need to reset pipe syncd status */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe_ctx_old =	&dc->current_state->res_ctx.pipe_ctx[i];
		pipe_ctx = &context->res_ctx.pipe_ctx[i];

		if (!resource_is_pipe_type(pipe_ctx_old, OTG_MASTER))
			continue;

		if (!pipe_ctx->stream ||
				pipe_need_reprogram(pipe_ctx_old, pipe_ctx)) {

			/* Reset all the syncd pipes from the disabled pipe */
			for (j = 0; j < dc->res_pool->pipe_count; j++) {
				pipe_ctx_syncd = &context->res_ctx.pipe_ctx[j];
				if ((GET_PIPE_SYNCD_FROM_PIPE(pipe_ctx_syncd) == pipe_ctx_old->pipe_idx) ||
					!IS_PIPE_SYNCD_VALID(pipe_ctx_syncd))
					SET_PIPE_SYNCD_TO_PIPE(pipe_ctx_syncd, j);
			}
		}
	}
}

void check_syncd_pipes_for_disabled_master_pipe(struct dc *dc,
	struct dc_state *context,
	uint8_t disabled_master_pipe_idx)
{
	int i;
	struct pipe_ctx *pipe_ctx, *pipe_ctx_check;

	pipe_ctx = &context->res_ctx.pipe_ctx[disabled_master_pipe_idx];
	if ((GET_PIPE_SYNCD_FROM_PIPE(pipe_ctx) != disabled_master_pipe_idx) ||
		!IS_PIPE_SYNCD_VALID(pipe_ctx))
		SET_PIPE_SYNCD_TO_PIPE(pipe_ctx, disabled_master_pipe_idx);

	/* for the pipe disabled, check if any slave pipe exists and assert */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe_ctx_check = &context->res_ctx.pipe_ctx[i];

		if ((GET_PIPE_SYNCD_FROM_PIPE(pipe_ctx_check) == disabled_master_pipe_idx) &&
		    IS_PIPE_SYNCD_VALID(pipe_ctx_check) && (i != disabled_master_pipe_idx)) {
			struct pipe_ctx *first_pipe = pipe_ctx_check;

			while (first_pipe->prev_odm_pipe)
				first_pipe = first_pipe->prev_odm_pipe;
			/* When ODM combine is enabled, this case is expected. If the disabled pipe
			 * is part of the ODM tree, then we should not print an error.
			 * */
			if (first_pipe->pipe_idx == disabled_master_pipe_idx)
				continue;

			DC_ERR("DC: Failure: pipe_idx[%d] syncd with disabled master pipe_idx[%d]\n",
				   i, disabled_master_pipe_idx);
		}
	}
}

void reset_sync_context_for_pipe(const struct dc *dc,
	struct dc_state *context,
	uint8_t pipe_idx)
{
	int i;
	struct pipe_ctx *pipe_ctx_reset;

	/* reset the otg sync context for the pipe and its slave pipes if any */
	for (i = 0; i < dc->res_pool->pipe_count; i++) {
		pipe_ctx_reset = &context->res_ctx.pipe_ctx[i];

		if (((GET_PIPE_SYNCD_FROM_PIPE(pipe_ctx_reset) == pipe_idx) &&
			IS_PIPE_SYNCD_VALID(pipe_ctx_reset)) || (i == pipe_idx))
			SET_PIPE_SYNCD_TO_PIPE(pipe_ctx_reset, i);
	}
}

uint8_t resource_transmitter_to_phy_idx(const struct dc *dc, enum transmitter transmitter)
{
	/* TODO - get transmitter to phy idx mapping from DMUB */
	uint8_t phy_idx = transmitter - TRANSMITTER_UNIPHY_A;

	if (dc->ctx->dce_version == DCN_VERSION_3_1 &&
			dc->ctx->asic_id.hw_internal_rev == YELLOW_CARP_B0) {
		switch (transmitter) {
		case TRANSMITTER_UNIPHY_A:
			phy_idx = 0;
			break;
		case TRANSMITTER_UNIPHY_B:
			phy_idx = 1;
			break;
		case TRANSMITTER_UNIPHY_C:
			phy_idx = 5;
			break;
		case TRANSMITTER_UNIPHY_D:
			phy_idx = 6;
			break;
		case TRANSMITTER_UNIPHY_E:
			phy_idx = 4;
			break;
		default:
			phy_idx = 0;
			break;
		}
	}

	return phy_idx;
}

const struct link_hwss *get_link_hwss(const struct dc_link *link,
		const struct link_resource *link_res)
{
	/* Link_hwss is only accessible by getter function instead of accessing
	 * by pointers in dc with the intent to protect against breaking polymorphism.
	 */
	if (can_use_hpo_dp_link_hwss(link, link_res))
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
		return (requires_fixed_vs_pe_retimer_hpo_link_hwss(link) ?
				get_hpo_fixed_vs_pe_retimer_dp_link_hwss() : get_hpo_dp_link_hwss());
	else if (can_use_dpia_link_hwss(link, link_res))
		return get_dpia_link_hwss();
	else if (can_use_dio_link_hwss(link, link_res))
		return (requires_fixed_vs_pe_retimer_dio_link_hwss(link)) ?
				get_dio_fixed_vs_pe_retimer_link_hwss() : get_dio_link_hwss();
	else
		return get_virtual_link_hwss();
}

bool is_h_timing_divisible_by_2(struct dc_stream_state *stream)
{
	bool divisible = false;
	uint16_t h_blank_start = 0;
	uint16_t h_blank_end = 0;

	if (stream) {
		h_blank_start = stream->timing.h_total - stream->timing.h_front_porch;
		h_blank_end = h_blank_start - stream->timing.h_addressable;

		/* HTOTAL, Hblank start/end, and Hsync start/end all must be
		 * divisible by 2 in order for the horizontal timing params
		 * to be considered divisible by 2. Hsync start is always 0.
		 */
		divisible = (stream->timing.h_total % 2 == 0) &&
				(h_blank_start % 2 == 0) &&
				(h_blank_end % 2 == 0) &&
				(stream->timing.h_sync_width % 2 == 0);
	}
	return divisible;
}

/* This interface is deprecated for new DCNs. It is replaced by the following
 * new interfaces. These two interfaces encapsulate pipe selection priority
 * with DCN specific minimum hardware transition optimization algorithm. With
 * the new interfaces caller no longer needs to know the implementation detail
 * of a pipe topology.
 *
 * resource_update_pipes_with_odm_slice_count
 * resource_update_pipes_with_mpc_slice_count
 *
 */
bool dc_resource_acquire_secondary_pipe_for_mpc_odm_legacy(
		const struct dc *dc,
		struct dc_state *state,
		struct pipe_ctx *pri_pipe,
		struct pipe_ctx *sec_pipe,
		bool odm)
{
	int pipe_idx = sec_pipe->pipe_idx;
	struct pipe_ctx *sec_top, *sec_bottom, *sec_next, *sec_prev;
	const struct resource_pool *pool = dc->res_pool;

	sec_top = sec_pipe->top_pipe;
	sec_bottom = sec_pipe->bottom_pipe;
	sec_next = sec_pipe->next_odm_pipe;
	sec_prev = sec_pipe->prev_odm_pipe;

	if (pri_pipe == NULL)
		return false;

	*sec_pipe = *pri_pipe;

	sec_pipe->top_pipe = sec_top;
	sec_pipe->bottom_pipe = sec_bottom;
	sec_pipe->next_odm_pipe = sec_next;
	sec_pipe->prev_odm_pipe = sec_prev;

	sec_pipe->pipe_idx = pipe_idx;
	sec_pipe->plane_res.mi = pool->mis[pipe_idx];
	sec_pipe->plane_res.hubp = pool->hubps[pipe_idx];
	sec_pipe->plane_res.ipp = pool->ipps[pipe_idx];
	sec_pipe->plane_res.xfm = pool->transforms[pipe_idx];
	sec_pipe->plane_res.dpp = pool->dpps[pipe_idx];
	sec_pipe->plane_res.mpcc_inst = pool->dpps[pipe_idx]->inst;
	sec_pipe->stream_res.dsc = NULL;
	if (odm) {
		if (!sec_pipe->top_pipe)
			sec_pipe->stream_res.opp = pool->opps[pipe_idx];
		else
			sec_pipe->stream_res.opp = sec_pipe->top_pipe->stream_res.opp;
		if (sec_pipe->stream->timing.flags.DSC == 1) {
#if defined(CONFIG_DRM_AMD_DC_FP)
			dcn20_acquire_dsc(dc, &state->res_ctx, &sec_pipe->stream_res.dsc, sec_pipe->stream_res.opp->inst);
#endif
			ASSERT(sec_pipe->stream_res.dsc);
			if (sec_pipe->stream_res.dsc == NULL)
				return false;
		}
#if defined(CONFIG_DRM_AMD_DC_FP)
		dcn20_build_mapped_resource(dc, state, sec_pipe->stream);
#endif
	}

	return true;
}

enum dc_status update_dp_encoder_resources_for_test_harness(const struct dc *dc,
		struct dc_state *context,
		struct pipe_ctx *pipe_ctx)
{
	if (dc->link_srv->dp_get_encoding_format(&pipe_ctx->link_config.dp_link_settings) == DP_128b_132b_ENCODING) {
		if (pipe_ctx->stream_res.hpo_dp_stream_enc == NULL) {
			pipe_ctx->stream_res.hpo_dp_stream_enc =
					find_first_free_match_hpo_dp_stream_enc_for_link(
							&context->res_ctx, dc->res_pool, pipe_ctx->stream);

			if (!pipe_ctx->stream_res.hpo_dp_stream_enc)
				return DC_NO_STREAM_ENC_RESOURCE;

			update_hpo_dp_stream_engine_usage(
					&context->res_ctx, dc->res_pool,
					pipe_ctx->stream_res.hpo_dp_stream_enc,
					true);
		}

		if (pipe_ctx->link_res.hpo_dp_link_enc == NULL) {
			if (!add_hpo_dp_link_enc_to_ctx(&context->res_ctx, dc->res_pool, pipe_ctx, pipe_ctx->stream))
				return DC_NO_LINK_ENC_RESOURCE;
		}
	} else {
		if (pipe_ctx->stream_res.hpo_dp_stream_enc) {
			update_hpo_dp_stream_engine_usage(
					&context->res_ctx, dc->res_pool,
					pipe_ctx->stream_res.hpo_dp_stream_enc,
					false);
			pipe_ctx->stream_res.hpo_dp_stream_enc = NULL;
		}
		if (pipe_ctx->link_res.hpo_dp_link_enc)
			remove_hpo_dp_link_enc_from_ctx(&context->res_ctx, pipe_ctx, pipe_ctx->stream);
	}

	return DC_OK;
}

bool check_subvp_sw_cursor_fallback_req(const struct dc *dc, struct dc_stream_state *stream)
{
	if (!dc->debug.disable_subvp_high_refresh && is_subvp_high_refresh_candidate(stream))
		return true;
	if (dc->current_state->stream_count == 1 && stream->timing.v_addressable >= 2880 &&
			((stream->timing.pix_clk_100hz * 100) / stream->timing.v_total / stream->timing.h_total) < 120)
		return true;
	else if (dc->current_state->stream_count > 1 && stream->timing.v_addressable >= 1080 &&
			((stream->timing.pix_clk_100hz * 100) / stream->timing.v_total / stream->timing.h_total) < 120)
		return true;

	return false;
}

struct dscl_prog_data *resource_get_dscl_prog_data(struct pipe_ctx *pipe_ctx)
{
	return &pipe_ctx->plane_res.scl_data.dscl_prog_data;
}

void resource_init_common_dml2_callbacks(struct dc *dc, struct dml2_configuration_options *dml2_options)
{
	dml2_options->callbacks.dc = dc;
	dml2_options->callbacks.build_scaling_params = &resource_build_scaling_params;
	dml2_options->callbacks.build_test_pattern_params = &resource_build_test_pattern_params;
	dml2_options->callbacks.acquire_secondary_pipe_for_mpc_odm = &dc_resource_acquire_secondary_pipe_for_mpc_odm_legacy;
	dml2_options->callbacks.update_pipes_for_stream_with_slice_count = &resource_update_pipes_for_stream_with_slice_count;
	dml2_options->callbacks.update_pipes_for_plane_with_slice_count = &resource_update_pipes_for_plane_with_slice_count;
	dml2_options->callbacks.get_mpc_slice_index = &resource_get_mpc_slice_index;
	dml2_options->callbacks.get_mpc_slice_count = &resource_get_mpc_slice_count;
	dml2_options->callbacks.get_odm_slice_index = &resource_get_odm_slice_index;
	dml2_options->callbacks.get_odm_slice_count = &resource_get_odm_slice_count;
	dml2_options->callbacks.get_opp_head = &resource_get_opp_head;
	dml2_options->callbacks.get_otg_master_for_stream = &resource_get_otg_master_for_stream;
	dml2_options->callbacks.get_opp_heads_for_otg_master = &resource_get_opp_heads_for_otg_master;
	dml2_options->callbacks.get_dpp_pipes_for_plane = &resource_get_dpp_pipes_for_plane;
	dml2_options->callbacks.get_stream_status = &dc_state_get_stream_status;
	dml2_options->callbacks.get_stream_from_id = &dc_state_get_stream_from_id;
	dml2_options->callbacks.get_max_flickerless_instant_vtotal_increase = &dc_stream_get_max_flickerless_instant_vtotal_increase;

	dml2_options->svp_pstate.callbacks.dc = dc;
	dml2_options->svp_pstate.callbacks.add_phantom_plane = &dc_state_add_phantom_plane;
	dml2_options->svp_pstate.callbacks.add_phantom_stream = &dc_state_add_phantom_stream;
	dml2_options->svp_pstate.callbacks.build_scaling_params = &resource_build_scaling_params;
	dml2_options->svp_pstate.callbacks.create_phantom_plane = &dc_state_create_phantom_plane;
	dml2_options->svp_pstate.callbacks.remove_phantom_plane = &dc_state_remove_phantom_plane;
	dml2_options->svp_pstate.callbacks.remove_phantom_stream = &dc_state_remove_phantom_stream;
	dml2_options->svp_pstate.callbacks.create_phantom_stream = &dc_state_create_phantom_stream;
	dml2_options->svp_pstate.callbacks.release_phantom_plane = &dc_state_release_phantom_plane;
	dml2_options->svp_pstate.callbacks.release_phantom_stream = &dc_state_release_phantom_stream;
	dml2_options->svp_pstate.callbacks.get_pipe_subvp_type = &dc_state_get_pipe_subvp_type;
	dml2_options->svp_pstate.callbacks.get_stream_subvp_type = &dc_state_get_stream_subvp_type;
	dml2_options->svp_pstate.callbacks.get_paired_subvp_stream = &dc_state_get_paired_subvp_stream;
	dml2_options->svp_pstate.callbacks.remove_phantom_streams_and_planes = &dc_state_remove_phantom_streams_and_planes;
	dml2_options->svp_pstate.callbacks.release_phantom_streams_and_planes = &dc_state_release_phantom_streams_and_planes;
}

/* Returns number of DET segments allocated for a given OTG_MASTER pipe */
int resource_calculate_det_for_stream(struct dc_state *state, struct pipe_ctx *otg_master)
{
	struct pipe_ctx *opp_heads[MAX_PIPES];
	struct pipe_ctx *dpp_pipes[MAX_PIPES];

	int dpp_count = 0;
	int det_segments = 0;

	if (!otg_master->stream)
		return 0;

	int slice_count = resource_get_opp_heads_for_otg_master(otg_master,
			&state->res_ctx, opp_heads);

	for (int slice_idx = 0; slice_idx < slice_count; slice_idx++) {
		if (opp_heads[slice_idx]->plane_state) {
			dpp_count = resource_get_dpp_pipes_for_opp_head(
					opp_heads[slice_idx],
					&state->res_ctx,
					dpp_pipes);
			for (int dpp_idx = 0; dpp_idx < dpp_count; dpp_idx++)
				det_segments += dpp_pipes[dpp_idx]->hubp_regs.det_size;
		}
	}
	return det_segments;
}

bool resource_is_hpo_acquired(struct dc_state *context)
{
	int i;

	for (i = 0; i < MAX_HPO_DP2_ENCODERS; i++) {
		if (context->res_ctx.is_hpo_dp_stream_enc_acquired[i]) {
			return true;
		}
	}

	return false;
}
