// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dc_bios_types.h"
#include "core_types.h"
#include "dcn42_hpo_frl_stream_encoder.h"
#include "dcn31/dcn31_apg.h"
#include "dcn401/dcn401_hpo_frl_stream_encoder.h"
#include "dcn30/dcn30_hpo_frl_stream_encoder.h"
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

void hpo_enc42_unblank(struct hpo_frl_stream_encoder *enc, int otg_inst)
{
	(void)otg_inst;
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	DC_LOG_HDMI_FRL("Entering [%s]\n", __func__);

	/*make sure FIFO_VIDEO_STREAM_ACTIVE =1*/
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
		   FIFO_ENABLE, 0);

	/* Reset */
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
		   FIFO_RESET, 1);
	REG_WAIT(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_RESET_DONE,
		 1, 10, 1000);
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
		   FIFO_RESET, 0);
	REG_WAIT(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_RESET_DONE,
		 0, 10, 1000);

	/* Enable HDMI Tribyte Encoder */
	REG_UPDATE(HDMI_TB_ENC_CONTROL,
		   HDMI_TB_ENC_EN, 1);

	/* Enable Clock Ramp Adjuster FIFO */
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
		   FIFO_ENABLE, 1);

	DC_LOG_HDMI_FRL("Exiting [%s]\n", __func__);
}

void hpo_enc42_setup_hdmi_audio(
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

	/* Setup audio in APG - program APG block associated with HPO */
	ASSERT(enc->apg);

	/* HDMI_ACR_PACKET_CONTROL */
	REG_UPDATE_3(HDMI_TB_ENC_ACR_PACKET_CONTROL,
			HDMI_ACR_AUTO_SEND, 1,
			HDMI_ACR_SOURCE, 0,
			HDMI_ACR_AUDIO_PRIORITY, 0);

	/* N/CTS computed relative to FRL rate instead of video rate (TMDS character clock). */
	/* Program audio clock sample/regeneration parameters */
	frl_get_audio_clock_info(crtc_info->color_depth,
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

void hpo_enc42_hdmi_audio_setup(
	struct hpo_frl_stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	REG_UPDATE_2(HDMI_STREAM_ENC_AUDIO_CONTROL,
			HDMI_STREAM_ENC_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL, az_inst,
			HDMI_STREAM_ENC_APG_CLOCK_EN, 1);

	hpo_enc42_setup_hdmi_audio(enc, audio_crtc_info);
	ASSERT (enc->apg);
	enc->apg->funcs->se_audio_setup(enc->apg, az_inst, info);
}

void hpo_enc42_hdmi_audio_disable(
	struct hpo_frl_stream_encoder *enc)
{
	ASSERT(enc->apg);
	if (enc->apg->funcs->disable_apg)
		enc->apg->funcs->disable_apg(enc->apg);
}

void hpo_enc42_audio_mute_control(
	struct hpo_frl_stream_encoder *enc,
	bool mute)
{
	ASSERT (enc->apg);
	if (mute)
		enc->apg->funcs->disable_apg(enc->apg);
	else
		enc->apg->funcs->enable_apg(enc->apg);
}

static const struct hpo_frl_stream_encoder_funcs dcn42_str_enc_funcs = {
	.hdmi_frl_enable		= hpo_enc401_enable,
	.hdmi_frl_unblank		= hpo_enc42_unblank,
	.hdmi_frl_blank			= hpo_enc401_blank,
	.hdmi_frl_set_stream_attribute	= hpo_enc401_set_hdmi_stream_attribute,
	.validate_hdmi_frl_output	= hpo_enc3_validate_hdmi_frl_output,
	.update_hdmi_info_packets	= hpo_enc401_update_hdmi_info_packets,
	.stop_hdmi_info_packets		= hpo_enc401_stop_hdmi_info_packets,
	.audio_mute_control		= hpo_enc42_audio_mute_control,
	.hdmi_audio_setup		= hpo_enc42_hdmi_audio_setup,
	.hdmi_audio_disable		= hpo_enc42_hdmi_audio_disable,
	.set_avmute			= enc401_stream_encoder_set_avmute,
	.read_state			= hpo_enc401_read_state,
	.hdmi_frl_set_dsc_config	= hpo_enc401_hdmi_set_dsc_config,
	.set_dynamic_metadata           = hpo_enc401_set_dynamic_metadata,
};

void dcn42_hpo_frl_stream_encoder_construct(
	struct dcn42_hpo_frl_stream_encoder *enc42,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct apg *apg,
	const struct dcn30_hpo_frl_stream_enc_registers *regs,
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift,
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask)
{
	enc42->base.funcs = &dcn42_str_enc_funcs;
	enc42->base.ctx = ctx;
	enc42->base.id = eng_id;
	enc42->base.bp = bp;
	enc42->base.vpg = vpg;
	enc42->base.apg = apg;
	enc42->regs = regs;
	enc42->hpo_se_shift = hpo_se_shift;
	enc42->hpo_se_mask = hpo_se_mask;
	enc42->base.stream_enc_inst = vpg->inst;
}
