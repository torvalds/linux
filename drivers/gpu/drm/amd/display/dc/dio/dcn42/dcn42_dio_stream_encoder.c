// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dc_bios_types.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn32/dcn32_dio_stream_encoder.h"
#include "dcn35/dcn35_dio_stream_encoder.h"
#include "dcn401/dcn401_dio_stream_encoder.h"
#include "dcn31/dcn31_apg.h"

#include "dcn42_dio_stream_encoder.h"
#include "reg_helper.h"
#include "hw_shared.h"
#include "link_service.h"
#include "dpcd_defs.h"

#define DC_LOGGER \
		enc1->base.ctx->logger

#define REG(reg)\
	(enc1->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc1->se_shift->field_name, enc1->se_mask->field_name

#define VBI_LINE_0 0
#define HDMI_CLOCK_CHANNEL_RATE_MORE_340M 340000

#define CTX \
	enc1->base.ctx

/* setup stream encoder in hdmi mode */
static void enc42_stream_encoder_hdmi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	int actual_pix_clk_khz,
	bool enable_audio)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (!enc->ctx->dc->debug.avoid_vbios_exec_table) {
		struct bp_encoder_control cntl = {0};

		cntl.action = ENCODER_CONTROL_SETUP;
		cntl.engine_id = enc1->base.id;
		cntl.signal = SIGNAL_TYPE_HDMI_TYPE_A;
		cntl.enable_dp_audio = enable_audio;
		cntl.pixel_clock = actual_pix_clk_khz;
		cntl.lanes_number = LANE_COUNT_FOUR;

		if (enc1->base.bp->funcs->encoder_control(
				enc1->base.bp, &cntl) != BP_RESULT_OK)
			return;

	} else {

		//Set pattern for clock channel, default vlue 0x63 does not work
		REG_UPDATE(DIG_CLOCK_PATTERN, DIG_CLOCK_PATTERN, 0x1F);

		//DIG_BE_TMDS_HDMI_MODE : TMDS-HDMI mode is already set in link_encoder_setup

		//DIG_SOURCE_SELECT is already set in dig_connect_to_otg

		/* DIG_START is removed from the register spec */
	}

	/* Configure pixel encoding */
	enc401_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);

	/* setup HDMI engine */
	REG_UPDATE_4(HDMI_CONTROL,
		HDMI_DEEP_COLOR_ENABLE, 0,
		HDMI_DATA_SCRAMBLE_EN, 0,
		HDMI_NO_EXTRA_NULL_PACKET_FILLED, 1,
		HDMI_CLOCK_CHANNEL_RATE, 0);

	/* Configure color depth */
	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_888:
		REG_UPDATE(HDMI_CONTROL, HDMI_DEEP_COLOR_DEPTH, 0);
		break;
	case COLOR_DEPTH_101010:
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 1,
					HDMI_DEEP_COLOR_ENABLE, 0);
		} else {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 1,
					HDMI_DEEP_COLOR_ENABLE, 1);
			}
		break;
	case COLOR_DEPTH_121212:
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 2,
					HDMI_DEEP_COLOR_ENABLE, 0);
		} else {
			REG_UPDATE_2(HDMI_CONTROL,
					HDMI_DEEP_COLOR_DEPTH, 2,
					HDMI_DEEP_COLOR_ENABLE, 1);
			}
		break;
	case COLOR_DEPTH_161616:
		REG_UPDATE_2(HDMI_CONTROL,
				HDMI_DEEP_COLOR_DEPTH, 3,
				HDMI_DEEP_COLOR_ENABLE, 1);
		break;
	default:
		break;
	}

	if (actual_pix_clk_khz >= HDMI_CLOCK_CHANNEL_RATE_MORE_340M) {
		/* enable HDMI data scrambler
		 * HDMI_CLOCK_CHANNEL_RATE_MORE_340M
		 * Clock channel frequency is 1/4 of character rate.
		 */
		REG_UPDATE_2(HDMI_CONTROL,
			HDMI_DATA_SCRAMBLE_EN, 1,
			HDMI_CLOCK_CHANNEL_RATE, 1);
	} else if (crtc_timing->flags.LTE_340MCSC_SCRAMBLE) {

		/* TODO: New feature for DCE11, still need to implement */

		/* enable HDMI data scrambler
		 * HDMI_CLOCK_CHANNEL_FREQ_EQUAL_TO_CHAR_RATE
		 * Clock channel frequency is the same
		 * as character rate
		 */
		REG_UPDATE_2(HDMI_CONTROL,
			HDMI_DATA_SCRAMBLE_EN, 1,
			HDMI_CLOCK_CHANNEL_RATE, 0);
	}

	/* Enable transmission of General Control packet on every frame */
	REG_UPDATE_3(HDMI_VBI_PACKET_CONTROL,
		HDMI_GC_CONT, 1,
		HDMI_GC_SEND, 1,
		HDMI_NULL_SEND, 1);

	/* Disable Audio Content Protection packet transmission */
	REG_UPDATE(HDMI_VBI_PACKET_CONTROL, HDMI_ACP_SEND, 0);
	/* following belongs to audio */
	/* Enable Audio InfoFrame packet transmission. */
	REG_UPDATE(HDMI_INFOFRAME_CONTROL0, HDMI_AUDIO_INFO_SEND, 1);

	/* Select line number on which to send Audio InfoFrame packets */
	REG_UPDATE(HDMI_INFOFRAME_CONTROL0, HDMI_AUDIO_INFO_LINE,
				VBI_LINE_0 + 2);

	/* set HDMI GC AVMUTE */
	REG_UPDATE(HDMI_GC, HDMI_GC_AVMUTE, 0);
}

static void enc42_stream_encoder_stop_dp_info_packets(
	struct stream_encoder *enc)
{
	/* stop generic packets on DP */
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t value = 0;

	REG_SET_9(DP_SEC_CNTL, 0,
		DP_SEC_GSP0_ENABLE, 0,
		DP_SEC_GSP1_ENABLE, 0,
		DP_SEC_GSP2_ENABLE, 0,
		DP_SEC_GSP3_ENABLE, 0,
		DP_SEC_GSP4_ENABLE, 0,
		DP_SEC_GSP5_ENABLE, 0,
		DP_SEC_GSP6_ENABLE, 0,
		DP_SEC_GSP7_ENABLE, 0,
		DP_SEC_STREAM_ENABLE, 0);

	/* this register shared with audio info frame.
	 * therefore we need to keep master enabled
	 * if at least one of the fields is not 0
	 */
	value = REG_READ(DP_SEC_CNTL);
	if (value)
		REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);
}

static void enc42_stream_encoder_update_hdmi_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE(HDMI_DB_CONTROL, HDMI_DB_DISABLE, 1);
	REG_UPDATE(DIG_FE_AUDIO_CNTL, APG_CLOCK_ENABLE, 1);

	/*Always add mandatory packets first followed by optional ones*/
	enc3_update_hdmi_info_packet(enc1, 0, &info_frame->avi);
	enc3_update_hdmi_info_packet(enc1, 5, &info_frame->hfvsif);
	enc3_update_hdmi_info_packet(enc1, 2, &info_frame->gamut);
	enc3_update_hdmi_info_packet(enc1, 1, &info_frame->vendor);
	enc3_update_hdmi_info_packet(enc1, 3, &info_frame->spd);
	enc3_update_hdmi_info_packet(enc1, 4, &info_frame->hdrsmd);
	enc3_update_hdmi_info_packet(enc1, 6, &info_frame->vtem);
}

static void enc42_dp_set_dsc_pps_info_packet(struct stream_encoder *enc,
					bool enable,
					uint8_t *dsc_packed_pps,
					bool immediate_update)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (enable) {
		struct dc_info_packet pps_sdp;
		int i;

		/* Configure for PPS packet size (128 bytes) */
		REG_UPDATE(DP_SEC_CNTL2, DP_SEC_GSP11_PPS, 1);

		/* Load PPS into infoframe (SDP) registers */
		pps_sdp.valid = true;
		pps_sdp.hb0 = 0;
		pps_sdp.hb1 = DC_DP_INFOFRAME_TYPE_PPS;
		pps_sdp.hb2 = 127;
		pps_sdp.hb3 = 0;

		for (i = 0; i < 4; i++) {
			memcpy(pps_sdp.sb, &dsc_packed_pps[i * 32], 32);
			enc1->base.vpg->funcs->update_generic_info_packet(
							enc1->base.vpg,
							11 + i,
							&pps_sdp,
							immediate_update);
		}

		/* SW should make sure VBID[6] update line number is bigger
		 * than PPS transmit line number
		 */
		REG_UPDATE(DP_GSP11_CNTL,
				DP_SEC_GSP11_LINE_NUM, 2);
		REG_UPDATE_2(DP_MSA_VBID_MISC,
				DP_VBID6_LINE_REFERENCE, 0,
				DP_VBID6_LINE_NUM, 3);

		/* Send PPS data at the line number specified above.
		 * DP spec requires PPS to be sent only when it changes, however since
		 * decoder has to be able to handle its change on every frame, we're
		 * sending it always (i.e. on every frame) to reduce the chance it'd be
		 * missed by decoder. If it turns out required to send PPS only when it
		 * changes, we can use DP_SEC_GSP11_SEND register.
		 */
		REG_UPDATE(DP_GSP11_CNTL,
			DP_SEC_GSP11_ENABLE, 1);
		REG_UPDATE(DP_SEC_CNTL,
			DP_SEC_STREAM_ENABLE, 1);
	} else {
		/* Disable Generic Stream Packet 11 (GSP) transmission */
		REG_UPDATE(DP_GSP11_CNTL, DP_SEC_GSP11_ENABLE, 0);
		REG_UPDATE(DP_SEC_CNTL2, DP_SEC_GSP11_PPS, 0);
	}
}

static void enc42_se_dp_audio_setup(
	struct stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	enc42_se_enable_audio_clock(enc, true);

	REG_UPDATE(DIG_FE_AUDIO_CNTL,
			DIG_FE_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL, az_inst);

	enc->apg->funcs->se_audio_setup(enc->apg, az_inst, info);
}

#define DP_SEC_AUD_N__DP_SEC_AUD_N__DEFAULT 0x8000
#define DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUTO_CALC 1

static void enc42_se_setup_dp_audio(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* --- DP Audio packet configurations --- */

	/* ATP Configuration */
	REG_SET(DP_SEC_AUD_N, 0,
			DP_SEC_AUD_N, DP_SEC_AUD_N__DP_SEC_AUD_N__DEFAULT);

	/* Async/auto-calc timestamp mode */
	REG_SET(DP_SEC_TIMESTAMP, 0, DP_SEC_TIMESTAMP_MODE,
			DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUTO_CALC);
}

static void enc42_se_dp_audio_enable(
	struct stream_encoder *enc)
{
	ASSERT (enc->apg);
	enc42_se_enable_audio_clock(enc, true);
	/*APG_CONTROL2 for DP*/
	enc42_se_setup_dp_audio(enc);
	/*DP_SEC_CNTL programming*/
	enc1_se_enable_dp_audio(enc);
	/*APG_en*/
	enc->apg->funcs->enable_apg(enc->apg);

}

static void enc42_se_disable_dp_audio(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t value = 0;

	/* Disable Audio packets */
	REG_UPDATE_5(DP_SEC_CNTL,
			DP_SEC_ASP_ENABLE, 0,
			DP_SEC_ATP_ENABLE, 0,
			DP_SEC_AIP_ENABLE, 0,
			DP_SEC_ACM_ENABLE, 0,
			DP_SEC_STREAM_ENABLE, 0);

	/* This register shared with encoder info frame. Therefore we need to
	 * keep master enabled if at least on of the fields is not 0
	 */
	value = REG_READ(DP_SEC_CNTL);
	if (value != 0)
		REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);
}

static void enc42_se_dp_audio_disable(
	struct stream_encoder *enc)
{

	enc->apg->funcs->disable_apg(enc->apg);
	enc42_se_disable_dp_audio(enc);
	enc42_se_enable_audio_clock(enc, false);
}

static void enc42_se_setup_hdmi_audio(
	struct stream_encoder *enc,
	const struct audio_crtc_info *crtc_info)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	struct audio_clock_info audio_clock_info = {0};

	/* HDMI_AUDIO_PACKET_CONTROL */
	REG_UPDATE(HDMI_AUDIO_PACKET_CONTROL,
			HDMI_AUDIO_DELAY_EN, 1);

	/* HDMI_ACR_PACKET_CONTROL */
	REG_UPDATE_2(HDMI_ACR_PACKET_CONTROL,
			HDMI_ACR_AUTO_SEND, 1,
			HDMI_ACR_SOURCE, 0);

	/* Program audio clock sample/regeneration parameters */
	get_audio_clock_info(crtc_info->color_depth,
			     crtc_info->requested_pixel_clock_100Hz,
			     crtc_info->calculated_pixel_clock_100Hz,
			     &audio_clock_info);
	DC_LOG_HW_AUDIO("\n%s:Input::requested_pixel_clock_100Hz = %d" \
			"calculated_pixel_clock_100Hz = %d\n", __func__,\
			crtc_info->requested_pixel_clock_100Hz, \
			crtc_info->calculated_pixel_clock_100Hz);

	/* HDMI_ACR_32_0__HDMI_ACR_CTS_32_MASK */
	REG_UPDATE(HDMI_ACR_32_0, HDMI_ACR_CTS_32, audio_clock_info.cts_32khz);

	/* HDMI_ACR_32_1__HDMI_ACR_N_32_MASK */
	REG_UPDATE(HDMI_ACR_32_1, HDMI_ACR_N_32, audio_clock_info.n_32khz);

	/* HDMI_ACR_44_0__HDMI_ACR_CTS_44_MASK */
	REG_UPDATE(HDMI_ACR_44_0, HDMI_ACR_CTS_44, audio_clock_info.cts_44khz);

	/* HDMI_ACR_44_1__HDMI_ACR_N_44_MASK */
	REG_UPDATE(HDMI_ACR_44_1, HDMI_ACR_N_44, audio_clock_info.n_44khz);

	/* HDMI_ACR_48_0__HDMI_ACR_CTS_48_MASK */
	REG_UPDATE(HDMI_ACR_48_0, HDMI_ACR_CTS_48, audio_clock_info.cts_48khz);

	/* HDMI_ACR_48_1__HDMI_ACR_N_48_MASK */
	REG_UPDATE(HDMI_ACR_48_1, HDMI_ACR_N_48, audio_clock_info.n_48khz);

	/* Video driver cannot know in advance which sample rate will
	 * be used by HD Audio driver
	 * HDMI_ACR_PACKET_CONTROL__HDMI_ACR_N_MULTIPLE field is
	 * programmed below in interruppt callback
	 */
}

static void enc42_se_hdmi_audio_disable(
	struct stream_encoder *enc)
{
	ASSERT (enc->apg);
	enc->apg->funcs->disable_apg(enc->apg);
	enc42_se_enable_audio_clock(enc, false);
}

void enc42_se_enable_audio_clock(
	struct stream_encoder *enc,
	bool enable)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE(DIG_FE_AUDIO_CNTL, APG_CLOCK_ENABLE, !!enable);
}


static void enc42_se_hdmi_audio_setup(
	struct stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	ASSERT (enc->apg);
	enc42_se_enable_audio_clock(enc, true);
	/*ACR setup*/
	enc42_se_setup_hdmi_audio(enc, audio_crtc_info);

	REG_UPDATE(DIG_FE_AUDIO_CNTL,
				DIG_FE_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL, az_inst);

	/*APG_DBG_GEN_CONTROL*/
	enc->apg->funcs->se_audio_setup(enc->apg, az_inst, info);
	/*APG enable*/
	enc->apg->funcs->enable_apg(enc->apg);
}

static void enc42_audio_mute_control(
	struct stream_encoder *enc,
	bool mute)
{
	if (mute)
		enc->apg->funcs->disable_apg(enc->apg);
	else
		enc->apg->funcs->enable_apg(enc->apg);
}
void enc42_reset_hdmi_stream_attribute(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE_3(HDMI_CONTROL,
		HDMI_DEEP_COLOR_ENABLE, 0,
		HDMI_DATA_SCRAMBLE_EN, 0,
		HDMI_CLOCK_CHANNEL_RATE, 0);
}

static const struct stream_encoder_funcs dcn42_str_enc_funcs = {
	.dp_set_stream_attribute =
		enc401_stream_encoder_dp_set_stream_attribute,
	.hdmi_set_stream_attribute =
		enc42_stream_encoder_hdmi_set_stream_attribute,
	.dvi_set_stream_attribute =
		enc401_stream_encoder_dvi_set_stream_attribute,
	.set_throttled_vcp_size =
		enc1_stream_encoder_set_throttled_vcp_size,
	.update_hdmi_info_packets =
		enc42_stream_encoder_update_hdmi_info_packets,
	.stop_hdmi_info_packets =
		enc3_stream_encoder_stop_hdmi_info_packets,
	.update_dp_info_packets_sdp_line_num =
		enc3_stream_encoder_update_dp_info_packets_sdp_line_num,
	.update_dp_info_packets =
		enc3_stream_encoder_update_dp_info_packets,
	.stop_dp_info_packets =
		enc42_stream_encoder_stop_dp_info_packets,
	.dp_blank =
		enc1_stream_encoder_dp_blank,
	.dp_unblank =
		enc401_stream_encoder_dp_unblank,
	.audio_mute_control = enc42_audio_mute_control,

	.dp_audio_setup = enc42_se_dp_audio_setup,
	.dp_audio_enable = enc42_se_dp_audio_enable,
	.dp_audio_disable = enc42_se_dp_audio_disable,

	.hdmi_audio_setup = enc42_se_hdmi_audio_setup,
	.hdmi_audio_disable = enc42_se_hdmi_audio_disable,
	.setup_stereo_sync  = enc1_setup_stereo_sync,
	.set_avmute = enc1_stream_encoder_set_avmute,
	.dig_connect_to_otg = enc1_dig_connect_to_otg,
	.dig_source_otg = enc1_dig_source_otg,

	.dp_get_pixel_format  = enc1_stream_encoder_dp_get_pixel_format,

	.enc_read_state = enc401_read_state,
	.dp_set_dsc_config = NULL,
	.dp_set_dsc_pps_info_packet = enc42_dp_set_dsc_pps_info_packet,
	.set_dynamic_metadata = enc401_set_dynamic_metadata,
	.hdmi_reset_stream_attribute = enc42_reset_hdmi_stream_attribute,
	.enable_stream = enc401_stream_encoder_enable,

	.set_input_mode = enc401_set_dig_input_mode,
	.enable_fifo = enc35_enable_fifo,
	.disable_fifo = enc35_disable_fifo,
	.map_stream_to_link = enc401_stream_encoder_map_to_link,
};

void dcn42_dio_stream_encoder_construct(
	struct dcn10_stream_encoder *enc1,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct apg *apg,
	const struct dcn10_stream_enc_registers *regs,
	const struct dcn10_stream_encoder_shift *se_shift,
	const struct dcn10_stream_encoder_mask *se_mask)
{
	enc1->base.funcs = &dcn42_str_enc_funcs;
	enc1->base.ctx = ctx;
	enc1->base.id = eng_id;
	enc1->base.bp = bp;
	enc1->base.vpg = vpg;
	enc1->base.apg = apg;
	enc1->regs = regs;
	enc1->se_shift = se_shift;
	enc1->se_mask = se_mask;
	enc1->base.stream_enc_inst = vpg->inst;
}
