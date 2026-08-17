/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
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

#ifndef __DC_HPO_FRL_STREAM_ENCODER_DCN401_H__
#define __DC_HPO_FRL_STREAM_ENCODER_DCN401_H__

#include "dcn30/dcn30_vpg.h"
#include "dcn30/dcn30_afmt.h"
#include "dcn30/dcn30_hpo_frl_stream_encoder.h"
#include "stream_encoder.h"
#include "dml/dml1_frl_cap_chk.h"

#define DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(hpo_frl_stream_encoder)\
	container_of(hpo_frl_stream_encoder, struct dcn401_hpo_frl_stream_encoder, base)

#define SE_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DCN401_HDMI_STREAM_ENC_MASK_SH_LIST(mask_sh)\
	SE_SF(HDMI_STREAM_ENC_INPUT_MUX_CONTROL, HDMI_STREAM_ENC_INPUT_MUX_SOURCE_SEL, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_ENABLE, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_RESET, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_PIXEL_ENCODING_TYPE, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_UNCOMPRESSED_PIXEL_FORMAT, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_COMPRESSED_PIXEL_FORMAT, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0, FIFO_RESET_DONE, mask_sh),\
	SE_SF(HDMI_STREAM_ENC_CLOCK_CONTROL, HDMI_STREAM_ENC_CLOCK_EN, mask_sh),\
	SE_SF(DME0_DME_CONTROL, METADATA_HUBP_REQUESTOR_ID, mask_sh),\
	SE_SF(DME0_DME_CONTROL, METADATA_ENGINE_EN, mask_sh),\
	SE_SF(DME0_DME_CONTROL, METADATA_STREAM_TYPE, mask_sh),\
	SE_SF(HDMI_TB_ENC_METADATA_PACKET_CONTROL, HDMI_METADATA_PACKET_ENABLE, mask_sh),\
	SE_SF(HDMI_TB_ENC_METADATA_PACKET_CONTROL, HDMI_METADATA_PACKET_LINE_REFERENCE, mask_sh),\
	SE_SF(HDMI_TB_ENC_METADATA_PACKET_CONTROL, HDMI_METADATA_PACKET_MISSED, mask_sh),\
	SE_SF(HDMI_TB_ENC_METADATA_PACKET_CONTROL, HDMI_METADATA_PACKET_LINE, mask_sh)

#define DCN401_HDMI_TB_ENC_MASK_SH_LIST(mask_sh)\
	SE_SF(HDMI_TB_ENC_CONTROL, HDMI_TB_ENC_EN, mask_sh),\
	SE_SF(HDMI_TB_ENC_CONTROL, HDMI_RESET, mask_sh),\
	SE_SF(HDMI_TB_ENC_CONTROL, HDMI_RESET_DONE, mask_sh),\
	SE_SF(HDMI_TB_ENC_MODE, HDMI_BORROW_MODE, mask_sh),\
	SE_SF(HDMI_TB_ENC_H_ACTIVE_BLANK, HDMI_H_ACTIVE, mask_sh),\
	SE_SF(HDMI_TB_ENC_H_ACTIVE_BLANK, HDMI_H_BLANK, mask_sh),\
	SE_SF(HDMI_TB_ENC_HC_ACTIVE_BLANK, HDMI_HC_ACTIVE, mask_sh),\
	SE_SF(HDMI_TB_ENC_HC_ACTIVE_BLANK, HDMI_HC_BLANK, mask_sh),\
	SE_SF(HDMI_TB_ENC_PACKET_CONTROL, HDMI_MAX_PACKETS_PER_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_DB_CONTROL, HDMI_DB_DISABLE, mask_sh),\
	SE_SF(HDMI_TB_ENC_PIXEL_FORMAT, HDMI_PIXEL_ENCODING, mask_sh),\
	SE_SF(HDMI_TB_ENC_PIXEL_FORMAT, HDMI_DEEP_COLOR_DEPTH, mask_sh),\
	SE_SF(HDMI_TB_ENC_PIXEL_FORMAT, HDMI_DEEP_COLOR_ENABLE, mask_sh),\
	SE_SF(HDMI_TB_ENC_PIXEL_FORMAT, HDMI_DSC_MODE, mask_sh),\
	SE_SF(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_GC_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_GC_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_ACP_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_AUDIO_INFO_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_AUDIO_INFO_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GC_CONTROL, HDMI_GC_AVMUTE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC0_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC1_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC2_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC3_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC4_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC5_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC6_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC7_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC8_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC9_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC10_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC11_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC12_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC13_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC14_CONT, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC0_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC1_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC2_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC3_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC4_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC5_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC6_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, HDMI_GENERIC7_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC8_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC9_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC10_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC11_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC12_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC13_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, HDMI_GENERIC14_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET0_1_LINE, HDMI_GENERIC0_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET0_1_LINE, HDMI_GENERIC1_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET2_3_LINE, HDMI_GENERIC2_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET2_3_LINE, HDMI_GENERIC3_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET4_5_LINE, HDMI_GENERIC4_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET4_5_LINE, HDMI_GENERIC5_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET6_7_LINE, HDMI_GENERIC6_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET6_7_LINE, HDMI_GENERIC7_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET8_9_LINE, HDMI_GENERIC8_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET8_9_LINE, HDMI_GENERIC9_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET10_11_LINE, HDMI_GENERIC10_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET10_11_LINE, HDMI_GENERIC11_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET12_13_LINE, HDMI_GENERIC12_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET12_13_LINE, HDMI_GENERIC13_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_GENERIC_PACKET14_LINE, HDMI_GENERIC14_LINE, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_PACKET_CONTROL, HDMI_ACR_AUTO_SEND, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_PACKET_CONTROL, HDMI_ACR_SOURCE, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_PACKET_CONTROL, HDMI_ACR_AUDIO_PRIORITY, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_32_0, HDMI_ACR_CTS_32, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_32_1, HDMI_ACR_N_32, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_44_0, HDMI_ACR_CTS_44, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_44_1, HDMI_ACR_N_44, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_48_0, HDMI_ACR_CTS_48, mask_sh),\
	SE_SF(HDMI_TB_ENC_ACR_48_1, HDMI_ACR_N_48, mask_sh),\
	SE_SF(HDMI_TB_ENC_CRC_CNTL, HDMI_CRC_EN, mask_sh),\
	SE_SF(HDMI_TB_ENC_CRC_CNTL, HDMI_CRC_CONT_EN, mask_sh)

#define DCN401_HDMI_TB_ENC_REG_FIELD_LIST(type) \
	type HDMI_TB_ENC_EN;\
	type HDMI_RESET;\
	type HDMI_RESET_DONE;\
	type HDMI_STREAM_ENC_CLOCK_EN;\
	type HDMI_STREAM_ENC_INPUT_MUX_SOURCE_SEL;\
	type HDMI_MAX_PACKETS_PER_LINE;\
	type FIFO_ENABLE;\
	type FIFO_RESET;\
	type FIFO_PIXEL_ENCODING_TYPE;\
	type FIFO_UNCOMPRESSED_PIXEL_FORMAT;\
	type FIFO_COMPRESSED_PIXEL_FORMAT;\
	type FIFO_RESET_DONE;\
	type HDMI_BORROW_MODE;\
	type HDMI_H_ACTIVE;\
	type HDMI_H_BLANK;\
	type HDMI_HC_ACTIVE;\
	type HDMI_HC_BLANK;\
	type HDMI_DB_DISABLE;\
	type HDMI_PIXEL_ENCODING;\
	type HDMI_DEEP_COLOR_DEPTH;\
	type HDMI_DEEP_COLOR_ENABLE;\
	type HDMI_ODM_COMBINE_MODE;\
	type HDMI_DSC_MODE;\
	type HDMI_GC_CONT;\
	type HDMI_GC_SEND;\
	type HDMI_ACP_SEND;\
	type HDMI_AUDIO_INFO_SEND;\
	type HDMI_AUDIO_INFO_LINE;\
	type HDMI_GC_AVMUTE;\
	type HDMI_GENERIC0_CONT;\
	type HDMI_GENERIC0_SEND;\
	type HDMI_GENERIC0_LINE;\
	type HDMI_GENERIC1_CONT;\
	type HDMI_GENERIC1_SEND;\
	type HDMI_GENERIC1_LINE;\
	type HDMI_GENERIC2_CONT;\
	type HDMI_GENERIC2_SEND;\
	type HDMI_GENERIC2_LINE;\
	type HDMI_GENERIC3_CONT;\
	type HDMI_GENERIC3_SEND;\
	type HDMI_GENERIC3_LINE;\
	type HDMI_GENERIC4_CONT;\
	type HDMI_GENERIC4_SEND;\
	type HDMI_GENERIC4_LINE;\
	type HDMI_GENERIC5_CONT;\
	type HDMI_GENERIC5_SEND;\
	type HDMI_GENERIC5_LINE;\
	type HDMI_GENERIC6_CONT;\
	type HDMI_GENERIC6_SEND;\
	type HDMI_GENERIC6_LINE;\
	type HDMI_GENERIC7_CONT;\
	type HDMI_GENERIC7_SEND;\
	type HDMI_GENERIC7_LINE;\
	type HDMI_GENERIC8_CONT;\
	type HDMI_GENERIC8_SEND;\
	type HDMI_GENERIC8_LINE;\
	type HDMI_GENERIC9_CONT;\
	type HDMI_GENERIC9_SEND;\
	type HDMI_GENERIC9_LINE;\
	type HDMI_GENERIC10_CONT;\
	type HDMI_GENERIC10_SEND;\
	type HDMI_GENERIC10_LINE;\
	type HDMI_GENERIC11_CONT;\
	type HDMI_GENERIC11_SEND;\
	type HDMI_GENERIC11_LINE;\
	type HDMI_GENERIC12_CONT;\
	type HDMI_GENERIC12_SEND;\
	type HDMI_GENERIC12_LINE;\
	type HDMI_GENERIC13_CONT;\
	type HDMI_GENERIC13_SEND;\
	type HDMI_GENERIC13_LINE;\
	type HDMI_GENERIC14_CONT;\
	type HDMI_GENERIC14_SEND;\
	type HDMI_GENERIC14_LINE;\
	type HDMI_ACR_AUTO_SEND;\
	type HDMI_ACR_SOURCE;\
	type HDMI_ACR_AUDIO_PRIORITY;\
	type HDMI_ACR_CTS_32;\
	type HDMI_ACR_N_32;\
	type HDMI_ACR_CTS_44;\
	type HDMI_ACR_N_44;\
	type HDMI_ACR_CTS_48;\
	type HDMI_ACR_N_48;\
	type HDMI_CRC_EN;\
	type HDMI_CRC_CONT_EN;\
	type METADATA_HUBP_REQUESTOR_ID;\
	type METADATA_ENGINE_EN;\
	type METADATA_STREAM_TYPE;\
	type HDMI_METADATA_PACKET_ENABLE;\
	type HDMI_METADATA_PACKET_LINE_REFERENCE;\
	type HDMI_METADATA_PACKET_MISSED;\
	type HDMI_METADATA_PACKET_LINE

#define DCN401_HPO_STREAM_ENC_MASK_SH_LIST(mask_sh)\
	DCN401_HDMI_STREAM_ENC_MASK_SH_LIST(mask_sh),\
	DCN401_HDMI_TB_ENC_MASK_SH_LIST(mask_sh)

#define DCN42_HDMI_TB_ENC_REG_FIELD_LIST(type) \
	type HDMI_STREAM_ENC_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL;\
	type HDMI_STREAM_ENC_APG_CLOCK_EN

struct dcn401_hpo_frl_stream_encoder_shift {
	DCN401_HDMI_TB_ENC_REG_FIELD_LIST(uint8_t);
	DCN42_HDMI_TB_ENC_REG_FIELD_LIST(uint8_t);
};

struct dcn401_hpo_frl_stream_encoder_mask {
	DCN401_HDMI_TB_ENC_REG_FIELD_LIST(uint32_t);
	DCN42_HDMI_TB_ENC_REG_FIELD_LIST(uint32_t);
};

struct dcn401_hpo_frl_stream_encoder {
	struct hpo_frl_stream_encoder base;
	const struct dcn30_hpo_frl_stream_enc_registers *regs;
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift;
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask;
};

void hpo_enc401_enable(
		struct hpo_frl_stream_encoder *enc,
		int otg_inst);

void hpo_enc401_unblank(
	struct hpo_frl_stream_encoder *enc,
	int otg_inst);

void hpo_enc401_read_state(
		struct hpo_frl_stream_encoder *enc,
		struct hpo_frl_stream_encoder_state *state);

void hpo_enc401_blank(
	struct hpo_frl_stream_encoder *enc);

void hpo_enc401_set_hdmi_stream_attribute(
	struct hpo_frl_stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	struct frl_borrow_params *borrow_params,
	int odm_combine_num_segments);

void hpo_enc401_update_hdmi_info_packet(
	struct dcn401_hpo_frl_stream_encoder *enc401,
	uint32_t packet_index,
	const struct dc_info_packet *info_packet);

void hpo_enc401_update_hdmi_info_packets(
	struct hpo_frl_stream_encoder *enc,
	const struct encoder_info_frame *info_frame);

void hpo_enc401_hdmi_set_dsc_config(
  struct hpo_frl_stream_encoder *enc,
  struct dc_crtc_timing *timing,
  uint8_t *dsc_packed_pps);

void hpo_enc401_stop_hdmi_info_packets(
	struct hpo_frl_stream_encoder *enc);

void hpo_enc401_setup_hdmi_audio(
	struct hpo_frl_stream_encoder *enc,
	const struct audio_crtc_info *crtc_info);

void hpo_enc401_hdmi_audio_setup(
	struct hpo_frl_stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info);

void hpo_enc401_hdmi_audio_disable(
	struct hpo_frl_stream_encoder *enc);

void hpo_enc401_audio_mute_control(
	struct hpo_frl_stream_encoder *enc,
	bool mute);

void enc401_stream_encoder_set_avmute(
	struct hpo_frl_stream_encoder *enc,
	bool enable);

void hpo_enc401_set_dynamic_metadata(
	struct hpo_frl_stream_encoder *enc,
	bool enable_dme,
	uint32_t hubp_requestor_id,
	enum dynamic_metadata_mode dmdata_mode);
void frl_get_audio_clock_info(
	enum dc_color_depth color_depth,
	uint32_t frl_character_clock_kHz,
	struct frl_audio_clock_info *audio_clock_info);

void dcn401_hpo_frl_stream_encoder_construct(
	struct dcn401_hpo_frl_stream_encoder *enc401,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct afmt *afmt,
	const struct dcn30_hpo_frl_stream_enc_registers *regs,
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift,
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask);

#endif
