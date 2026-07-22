// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#ifndef __DC_HPO_FRL_STREAM_ENCODER_DCN60_H__
#define __DC_HPO_FRL_STREAM_ENCODER_DCN60_H__

#include "dcn30/dcn30_vpg.h"
#include "dcn30/dcn30_afmt.h"
#include "dcn31/dcn31_apg.h"
#include "dcn30/dcn30_hpo_frl_stream_encoder.h"
#include "dcn401/dcn401_hpo_frl_stream_encoder.h"
#include "stream_encoder.h"
#include "dml/dml1_frl_cap_chk.h"

#define DCN60_HDMI_STREAM_ENC_MASK_SH_LIST(mask_sh)\
	DCN401_HPO_STREAM_ENC_MASK_SH_LIST(mask_sh),\
	SE_SF(HDMI_STREAM_ENC_AUDIO_CONTROL, HDMI_STREAM_ENC_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_AUDIO_CONTROL, HDMI_STREAM_ENC_APG_CLOCK_EN, mask_sh)

void dcn60_hpo_frl_stream_encoder_construct(
	struct dcn401_hpo_frl_stream_encoder *enc401,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct apg *apg,
	const struct dcn30_hpo_frl_stream_enc_registers *regs,
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift,
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask);

#endif /* __DC_HPO_STREAM_ENCODER_DCN60_H__ */
