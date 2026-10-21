// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dc_bios_types.h"
#include "core_types.h"
#include "dcn60_hpo_frl_stream_encoder.h"
#include "dcn30/dcn30_hpo_frl_stream_encoder.h"
#include "dcn401/dcn401_hpo_frl_stream_encoder.h"
#include "reg_helper.h"
#include "hw_shared.h"
#include "dcn_calc_math.h"
#include "dml/dcn30/dcn30_fpu.h"

#undef DC_LOGGER
#define DC_LOGGER \
		enc401->base.ctx->logger

#define DTRACE(str, ...) {DC_LOG_HDMI_FRL(str, ##__VA_ARGS__); }

#define DEBUG_FRL_CAP_CHK 1

#define REG(reg)\
	(enc401->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc401->hpo_se_shift->field_name, enc401->hpo_se_mask->field_name

#define CTX \
	enc401->base.ctx

#define VBI_LINE_0 0

/* setup stream encoder in hdmi mode */
/* Precondition: link is trained */
static void hpo_enc60_set_hdmi_stream_attribute(
	struct hpo_frl_stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	struct frl_borrow_params *borrow_params,
	int odm_combine_num_segments)
{
	(void)odm_combine_num_segments;
	uint32_t h_active;
	uint32_t h_blank;
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	DC_LOG_DEBUG("Entering [%s]\n", __func__);

	/* Configure pixel encoding */
	switch (crtc_timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
				HDMI_PIXEL_ENCODING, 1);
		REG_UPDATE_2(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_PIXEL_ENCODING_TYPE, 0,
				FIFO_UNCOMPRESSED_PIXEL_FORMAT, 0);
		break;
	case PIXEL_ENCODING_YCBCR420:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
				HDMI_PIXEL_ENCODING, 2);
		REG_UPDATE_2(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_PIXEL_ENCODING_TYPE, 0,
				FIFO_UNCOMPRESSED_PIXEL_FORMAT, 1);
		break;
	default:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
				HDMI_PIXEL_ENCODING, 0);
		REG_UPDATE_2(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_PIXEL_ENCODING_TYPE, 0,
				FIFO_UNCOMPRESSED_PIXEL_FORMAT, 0);
		break;
	}

	/* Configure color depth */
	switch (crtc_timing->display_color_depth) {
	case COLOR_DEPTH_888:
		REG_UPDATE_2(HDMI_TB_ENC_PIXEL_FORMAT,
				HDMI_DEEP_COLOR_DEPTH, 0,
				HDMI_DEEP_COLOR_ENABLE, 0);
		break;
	case COLOR_DEPTH_101010:
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
			REG_UPDATE_2(HDMI_TB_ENC_PIXEL_FORMAT,
					HDMI_DEEP_COLOR_DEPTH, 1,
					HDMI_DEEP_COLOR_ENABLE, 0);
		} else {
			REG_UPDATE_2(HDMI_TB_ENC_PIXEL_FORMAT,
					HDMI_DEEP_COLOR_DEPTH, 1,
					HDMI_DEEP_COLOR_ENABLE, 1);
		}
		break;
	case COLOR_DEPTH_121212:
		if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
			REG_UPDATE_2(HDMI_TB_ENC_PIXEL_FORMAT,
					HDMI_DEEP_COLOR_DEPTH, 2,
					HDMI_DEEP_COLOR_ENABLE, 0);
		} else {
			REG_UPDATE_2(HDMI_TB_ENC_PIXEL_FORMAT,
					HDMI_DEEP_COLOR_DEPTH, 2,
					HDMI_DEEP_COLOR_ENABLE, 1);
		}
		break;
	default:
		break;
	}

	/* When compression active, CD/PP/Phase field shall be zero in GCP */
	if (crtc_timing->flags.DSC) {
		REG_UPDATE_2(HDMI_TB_ENC_PIXEL_FORMAT,
				HDMI_DEEP_COLOR_DEPTH, 0,
				HDMI_DEEP_COLOR_ENABLE, 0);
	}

	/* Configure horizontal active and blank size */
	h_active = crtc_timing->h_addressable + crtc_timing->h_border_left + crtc_timing->h_border_right;
	h_blank = crtc_timing->h_total - h_active;

	if (crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 ||
			crtc_timing->pixel_encoding == PIXEL_ENCODING_YCBCR422) {
		h_active /= 2;
		h_blank /= 2;
	}

	REG_SET_2(HDMI_TB_ENC_H_ACTIVE_BLANK, 0,
			HDMI_H_ACTIVE, h_active,
			HDMI_H_BLANK, h_blank);

	/* Configure borrow parameters */
	REG_UPDATE(HDMI_TB_ENC_MODE,
			HDMI_BORROW_MODE, borrow_params->borrow_mode);
	REG_UPDATE(HDMI_TB_ENC_PACKET_CONTROL,
			HDMI_MAX_PACKETS_PER_LINE, borrow_params->audio_packets_line);
	REG_SET_2(HDMI_TB_ENC_HC_ACTIVE_BLANK, 0,
			HDMI_HC_ACTIVE, borrow_params->hc_active_target,
			HDMI_HC_BLANK, borrow_params->hc_blank_target);

	/* Enable transmission of General Control packet on every frame */
	REG_UPDATE_2(HDMI_TB_ENC_VBI_PACKET_CONTROL1,
		HDMI_GC_CONT, 1,
		HDMI_GC_SEND, 1);

	/* Disable Audio Content Protection packet transmission */
	/* TODO: review if this needs to be here */
	REG_UPDATE(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_ACP_SEND, 0);

	/* Enable Audio InfoFrame packet transmission. */
	REG_UPDATE(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_AUDIO_INFO_SEND, 1);

	/* update double-buffered AUDIO_INFO registers immediately */
//	ASSERT(enc->afmt);
//	enc->afmt->funcs->audio_info_immediate_update(enc->afmt);

	/* Select line number on which to send Audio InfoFrame packets */
	REG_UPDATE(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_AUDIO_INFO_LINE,
				VBI_LINE_0 + 2);

	/* set HDMI GC AVMUTE */
	REG_UPDATE(HDMI_TB_ENC_GC_CONTROL, HDMI_GC_AVMUTE, 0);

	DC_LOG_DEBUG("Exiting [%s]\n", __func__);
}

static void hpo_enc60_audio_mute_control(
	struct hpo_frl_stream_encoder *enc,
	bool mute)
{
	ASSERT (enc->apg);
	if (mute)
		enc->apg->funcs->disable_apg(enc->apg);
	else
		enc->apg->funcs->enable_apg(enc->apg);
}

//Covered both, rounding up or rounding down from FRL Link Rate /18.
static const struct frl_audio_clock_info frl_audio_clock_info_table[16] = {
	{166666, 4224, 171875, 5292, 156250, 5760, 156250},
	{166667, 4224, 171875, 5292, 156250, 5760, 156250},
	{333333, 4032, 328125, 5292, 312500, 6048, 328125},
	{333334, 4032, 328125, 5292, 312500, 6048, 328125},
	{444444, 4032, 437500, 3969, 312500, 6048, 437500},
	{444445, 4032, 437500, 3969, 312500, 6048, 437500},
	{555555, 3456, 468750, 3969, 390625, 5184, 468750},
	{555556, 3456, 468750, 3969, 390625, 5184, 468750},
	{666666, 3072, 500000, 3969, 468750, 4752, 515625},
	{666667, 3072, 500000, 3969, 468750, 4752, 515625},
	{888888, 4032, 875000, 3969, 625000, 6048, 875000},
	{888889, 4032, 875000, 3969, 625000, 6048, 875000},
	{1111110, 3456, 937500, 3969, 781250, 5184, 937500},
	{1111111, 3456, 937500, 3969, 781250, 5184, 937500},
	{1333332, 3072, 1000000, 3969, 937500, 4752, 1031250},
	{1333333, 3072, 1000000, 3969, 937500, 4752, 1031250}
};

static void get_audio_clock_info(
	enum dc_color_depth color_depth,
	uint32_t frl_character_clock_kHz,
	struct frl_audio_clock_info *audio_clock_info)
{
	(void)color_depth;
	const struct frl_audio_clock_info *clock_info;
	uint32_t index;
	uint32_t audio_array_size;

	clock_info = frl_audio_clock_info_table;
	audio_array_size = ARRAY_SIZE(
			frl_audio_clock_info_table);

	if (clock_info != NULL) {
		/* search for exact frl character clock in table */
		for (index = 0; index < audio_array_size; index++) {
			if (clock_info[index].frl_character_clock_kHz >
				frl_character_clock_kHz)
				break;  /* not match */
			else if (clock_info[index].frl_character_clock_kHz ==
					frl_character_clock_kHz) {
				/* match found */
				*audio_clock_info = clock_info[index];
				return;
			}
		}
	}
	/*Only 3, 6, 8, 10 and 12 Gbps are used for FRL Link rates with character
	 *clocks of 166.667, 333.333, 444.444, 555.555 and 666.667 MHz are used
	 *so entry should be found in above table if no bugs */
	BREAK_TO_DEBUGGER();
}

static void hpo_enc60_setup_hdmi_audio(
	struct hpo_frl_stream_encoder *enc,
	const struct audio_crtc_info *crtc_info)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
	struct frl_audio_clock_info audio_clock_info = {0};

	DC_LOG_DEBUG("Entering [%s]\n", __func__);

	/* TODO:  HDMI_AUDIO_DELAY_EN bit only in DIG -- not in HPO? */
	/* HDMI_AUDIO_PACKET_CONTROL */
	//REG_UPDATE(HDMI_AUDIO_PACKET_CONTROL,
	//		HDMI_AUDIO_DELAY_EN, 1);

	/* TODO: Same programming, but using HDMI_TB_ENC register */
	/* HDMI_ACR_PACKET_CONTROL */
	REG_UPDATE_3(HDMI_TB_ENC_ACR_PACKET_CONTROL,
			HDMI_ACR_AUTO_SEND, 1,
			HDMI_ACR_SOURCE, 0,
			HDMI_ACR_AUDIO_PRIORITY, 0);

	/* N/CTS computed relative to FRL rate instead of video rate (TMDS character clock). */
	/* Program audio clock sample/regeneration parameters */
	get_audio_clock_info(crtc_info->color_depth,
			     crtc_info->frl_character_clock_kHz,
			     &audio_clock_info);
	DC_LOG_HW_AUDIO(
			"\n%s:Input::requested_pixel_clock_100Hz = %d"	\
			"calculated_pixel_clock_100Hz = %d \n", __func__,	\
			crtc_info->requested_pixel_clock_100Hz,		\
			crtc_info->calculated_pixel_clock_100Hz);

	/* Same register definition, but using HDMI_TB_ENC register */
	/* HDMI_ACR_32_0__HDMI_ACR_CTS_32_MASK */
	REG_UPDATE(HDMI_TB_ENC_ACR_32_0, HDMI_ACR_CTS_32, audio_clock_info.cts_32khz);

	/* HDMI_ACR_32_1__HDMI_ACR_N_32_MASK */
	REG_UPDATE(HDMI_TB_ENC_ACR_32_1, HDMI_ACR_N_32, audio_clock_info.n_32khz);

	/* HDMI_ACR_44_0__HDMI_ACR_CTS_44_MASK */
	REG_UPDATE(HDMI_TB_ENC_ACR_44_0, HDMI_ACR_CTS_44, audio_clock_info.cts_44khz);

	/* HDMI_ACR_44_1__HDMI_ACR_N_44_MASK */
	REG_UPDATE(HDMI_TB_ENC_ACR_44_1, HDMI_ACR_N_44, audio_clock_info.n_44khz);

	/* HDMI_ACR_48_0__HDMI_ACR_CTS_48_MASK */
	REG_UPDATE(HDMI_TB_ENC_ACR_48_0, HDMI_ACR_CTS_48, audio_clock_info.cts_48khz);

	/* HDMI_ACR_48_1__HDMI_ACR_N_48_MASK */
	REG_UPDATE(HDMI_TB_ENC_ACR_48_1, HDMI_ACR_N_48, audio_clock_info.n_48khz);

	/* TODO: HDMI_TB_ENC_ACR_PACKET_CONTROL::ACR_N_MULTIPLE
	 *       Same register definition, but using HDMI_TB_ENC register*/

	/* Video driver cannot know in advance which sample rate will
	 * be used by HD Audio driver
	 * HDMI_ACR_PACKET_CONTROL__HDMI_ACR_N_MULTIPLE field is
	 * programmed below in interrupt callback
	 */
	DC_LOG_DEBUG("Exiting [%s]\n", __func__);
}

static void hpo_enc60_hdmi_audio_setup(
	struct hpo_frl_stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	REG_UPDATE_2(HDMI_STREAM_ENC_AUDIO_CONTROL,
			HDMI_STREAM_ENC_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL, az_inst,
			HDMI_STREAM_ENC_APG_CLOCK_EN, 1);

	hpo_enc60_setup_hdmi_audio(enc, audio_crtc_info);
	ASSERT (enc->apg);
	enc->apg->funcs->se_audio_setup(enc->apg, az_inst, info);
}

static void hpo_enc60_hdmi_audio_disable(
	struct hpo_frl_stream_encoder *enc)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	ASSERT (enc->apg);
	if (enc->apg && enc->apg->funcs->disable_apg)
		enc->apg->funcs->disable_apg(enc->apg);

	REG_UPDATE(HDMI_STREAM_ENC_AUDIO_CONTROL, HDMI_STREAM_ENC_APG_CLOCK_EN, 0);
}

static const struct hpo_frl_stream_encoder_funcs dcn401_str_enc_funcs = {
	.hdmi_frl_enable		= hpo_enc401_enable,
	.hdmi_frl_unblank		= hpo_enc401_unblank,
	.hdmi_frl_blank			= hpo_enc401_blank,
	.hdmi_frl_set_stream_attribute	= hpo_enc60_set_hdmi_stream_attribute,
	.validate_hdmi_frl_output	= hpo_enc3_validate_hdmi_frl_output,
	.update_hdmi_info_packets	= hpo_enc401_update_hdmi_info_packets,
	.stop_hdmi_info_packets		= hpo_enc401_stop_hdmi_info_packets,
	.audio_mute_control		= hpo_enc60_audio_mute_control,
	.hdmi_audio_setup		= hpo_enc60_hdmi_audio_setup,
	.hdmi_audio_disable		= hpo_enc60_hdmi_audio_disable,
	.set_avmute			= enc401_stream_encoder_set_avmute,
	.read_state			= hpo_enc401_read_state,
	.hdmi_frl_set_dsc_config	= hpo_enc401_hdmi_set_dsc_config,
	.set_dynamic_metadata           = hpo_enc401_set_dynamic_metadata,
};

void dcn60_hpo_frl_stream_encoder_construct(
	struct dcn401_hpo_frl_stream_encoder *enc401,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct apg *apg,
	const struct dcn30_hpo_frl_stream_enc_registers *regs,
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift,
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask)
{
	enc401->base.funcs = &dcn401_str_enc_funcs;
	enc401->base.ctx = ctx;
	enc401->base.id = eng_id;
	enc401->base.bp = bp;
	enc401->base.vpg = vpg;
	enc401->base.apg = apg;
	enc401->regs = regs;
	enc401->hpo_se_shift = hpo_se_shift;
	enc401->hpo_se_mask = hpo_se_mask;
	enc401->base.stream_enc_inst = vpg->inst;
}
