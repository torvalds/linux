// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DC_HPO_FRL_STREAM_ENCODER_DCN42_H__
#define __DC_HPO_FRL_STREAM_ENCODER_DCN42_H__

#include "dcn30/dcn30_vpg.h"
#include "dcn31/dcn31_apg.h"
#include "dcn30/dcn30_hpo_frl_stream_encoder.h"
#include "dcn401/dcn401_hpo_frl_stream_encoder.h"

#include "stream_encoder.h"
#include "dml/dml1_frl_cap_chk.h"

#define DCN42_HDMI_STREAM_ENC_MASK_SH_LIST(mask_sh)\
	DCN401_HPO_STREAM_ENC_MASK_SH_LIST(mask_sh),\
	SE_SF(HDMI_STREAM_ENC_AUDIO_CONTROL, HDMI_STREAM_ENC_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_AUDIO_CONTROL, HDMI_STREAM_ENC_APG_CLOCK_EN, mask_sh)
struct dcn42_hpo_frl_stream_encoder {
	struct hpo_frl_stream_encoder base;
	const struct dcn30_hpo_frl_stream_enc_registers *regs;
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift;
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask;
};

void hpo_enc42_unblank(
	struct hpo_frl_stream_encoder *enc,
	int otg_inst);

void hpo_enc42_setup_hdmi_audio(
	struct hpo_frl_stream_encoder *enc,
	const struct audio_crtc_info *crtc_info);

void hpo_enc42_hdmi_audio_setup(
	struct hpo_frl_stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info);

void hpo_enc42_hdmi_audio_disable(
	struct hpo_frl_stream_encoder *enc);

void hpo_enc42_audio_mute_control(
	struct hpo_frl_stream_encoder *enc,
	bool mute);

void dcn42_hpo_frl_stream_encoder_construct(
	struct dcn42_hpo_frl_stream_encoder *enc42,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct apg *apg,
	const struct dcn30_hpo_frl_stream_enc_registers *regs,
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift,
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask);

#endif /* __DC_HPO_STREAM_ENCODER_DCN42_H__ */
