/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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


#include "dc_bios_types.h"
#include "core_types.h"
#include "dcn401_hpo_frl_stream_encoder.h"
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

void hpo_enc401_enable(
	struct hpo_frl_stream_encoder *enc,
	int otg_inst)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	DC_LOG_DEBUG("Entering [%s]\n", __func__);

	/* Enable DISPCLK, SOCCLK, and HDMISTREAMCLK */
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_CONTROL, HDMI_STREAM_ENC_CLOCK_EN, 1);

	/* Reset */
	REG_UPDATE_2(HDMI_TB_ENC_CONTROL,
			HDMI_RESET, 1,
			HDMI_TB_ENC_EN, 0);
	REG_WAIT(HDMI_TB_ENC_CONTROL, HDMI_RESET_DONE,
			1, 10, 100);
	REG_UPDATE(HDMI_TB_ENC_CONTROL,
			HDMI_RESET, 0);

	/* FOR DEBUG:  enable CRC */
	REG_UPDATE_2(HDMI_TB_ENC_CRC_CNTL,
			HDMI_CRC_EN, 1,
			HDMI_CRC_CONT_EN, 1);

//	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL2, FIFO_DB_DISABLE, 1);
	/* TODO: confirm if need to set HDMI_DB_DISABLE -- HW team only setting FIFO_DB_DISABLE */
	REG_UPDATE(HDMI_TB_ENC_DB_CONTROL, HDMI_DB_DISABLE, 1);

	/* Set the input mux to select OTG source */
	REG_UPDATE(HDMI_STREAM_ENC_INPUT_MUX_CONTROL, HDMI_STREAM_ENC_INPUT_MUX_SOURCE_SEL, otg_inst);

	DC_LOG_DEBUG("Exiting [%s]\n", __func__);
}

void hpo_enc401_unblank(struct hpo_frl_stream_encoder *enc, int otg_inst)
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

void hpo_enc401_blank(struct hpo_frl_stream_encoder *enc)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	/* Disable Clock Ramp Adjuster FIFO */
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
		   FIFO_ENABLE, 0);

	/* Disable HDMI Tribyte Encoder */
	REG_UPDATE(HDMI_TB_ENC_CONTROL,
		   HDMI_TB_ENC_EN, 0);

	/* Disable DISPCLK, SOCCLK, and HDMISTREAMCLK */
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_CONTROL,
		   HDMI_STREAM_ENC_CLOCK_EN, 0);
}

void hpo_enc401_read_state(
	struct hpo_frl_stream_encoder *enc,
	struct hpo_frl_stream_encoder_state *state)
{
	uint32_t pixel_encoding;
	uint32_t color_depth;
//	int odm_combine;
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	ASSERT(state);

	REG_GET(HDMI_TB_ENC_CONTROL,
			HDMI_TB_ENC_EN, &state->stream_enc_enabled);

	REG_GET(HDMI_STREAM_ENC_INPUT_MUX_CONTROL,
			HDMI_STREAM_ENC_INPUT_MUX_SOURCE_SEL, &state->otg_inst);

	REG_GET_2(HDMI_TB_ENC_PIXEL_FORMAT,
			HDMI_PIXEL_ENCODING, &pixel_encoding,
			HDMI_DEEP_COLOR_DEPTH, &color_depth);

//	REG_GET(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
	//		FIFO_ODM_COMBINE_MODE, &odm_combine);

	REG_GET_2(HDMI_TB_ENC_H_ACTIVE_BLANK,
			HDMI_H_ACTIVE, &state->h_active,
			HDMI_H_BLANK, &state->h_blank);

	REG_GET(HDMI_TB_ENC_MODE,
			HDMI_BORROW_MODE, &state->borrow_mode);

	if (pixel_encoding == 0)
		state->pixel_format = PIXEL_ENCODING_YCBCR444;
	else if (pixel_encoding == 1)
		state->pixel_format = PIXEL_ENCODING_YCBCR422;
	else
		state->pixel_format = PIXEL_ENCODING_YCBCR420;

	if (color_depth == 0)
		state->color_depth = 8;
	else if (color_depth == 1)
		state->color_depth = 10;
	else
		state->color_depth = 12;

//	state->num_odm_segments = odm_combine + 1;
}

/* setup stream encoder in hdmi mode */
/* Precondition: link is trained */
void hpo_enc401_set_hdmi_stream_attribute(
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
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_UNCOMPRESSED_PIXEL_FORMAT, 0);
		break;
	case PIXEL_ENCODING_YCBCR420:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
				HDMI_PIXEL_ENCODING, 2);
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_UNCOMPRESSED_PIXEL_FORMAT, 1);
		break;
	default:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
				HDMI_PIXEL_ENCODING, 0);
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
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
	if (enc->afmt && enc->afmt->funcs->audio_info_immediate_update)
		enc->afmt->funcs->audio_info_immediate_update(enc->afmt);

	/* Select line number on which to send Audio InfoFrame packets */
	REG_UPDATE(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_AUDIO_INFO_LINE,
				VBI_LINE_0 + 2);

	/* set HDMI GC AVMUTE */
	REG_UPDATE(HDMI_TB_ENC_GC_CONTROL, HDMI_GC_AVMUTE, 0);

	DC_LOG_DEBUG("Exiting [%s]\n", __func__);
}

void hpo_enc401_update_hdmi_info_packet(
	struct dcn401_hpo_frl_stream_encoder *enc401,
	uint32_t packet_index,
	const struct dc_info_packet *info_packet)
{
	uint32_t cont, send, line;

	if (info_packet->valid) {
		enc401->base.vpg->funcs->update_generic_info_packet(
				enc401->base.vpg,
				packet_index,
				info_packet,
				true);

		/* enable transmission of packet(s) -
		 * packet transmission begins on the next frame
		 */
		cont = 1;
		/* send packet(s) every frame */
		send = 1;
		/* select line number to send packets on */
		/* TODO: check if line 2 is correct */
		line = 2;
	} else {
		cont = 0;
		send = 0;
		line = 0;
	}

	/* TODO: set bit to indicate if packet is Extended Metadata Packet. */
	/* TODO: In DCN3, there are 0-14 generic packets */

	/* choose which generic packet control to use */
	switch (packet_index) {
	case 0:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC0_CONT, cont,
				HDMI_GENERIC0_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET0_1_LINE,
				HDMI_GENERIC0_LINE, line);
		break;
	case 1:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC1_CONT, cont,
				HDMI_GENERIC1_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET0_1_LINE,
				HDMI_GENERIC1_LINE, line);
		break;
	case 2:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC2_CONT, cont,
				HDMI_GENERIC2_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET2_3_LINE,
				HDMI_GENERIC2_LINE, line);
		break;
	case 3:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC3_CONT, cont,
				HDMI_GENERIC3_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET2_3_LINE,
				HDMI_GENERIC3_LINE, line);
		break;
	case 4:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC4_CONT, cont,
				HDMI_GENERIC4_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET4_5_LINE,
				HDMI_GENERIC4_LINE, line);
		break;
	case 5:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC5_CONT, cont,
				HDMI_GENERIC5_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET4_5_LINE,
				HDMI_GENERIC5_LINE, line);
		break;
	case 6:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC6_CONT, cont,
				HDMI_GENERIC6_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET6_7_LINE,
				HDMI_GENERIC6_LINE, line);
		break;
	case 7:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC7_CONT, cont,
				HDMI_GENERIC7_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET6_7_LINE,
				HDMI_GENERIC7_LINE, line);
		break;
	case 8:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC8_CONT, cont,
				HDMI_GENERIC8_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET8_9_LINE,
				HDMI_GENERIC8_LINE, line);
		break;
	case 9:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC9_CONT, cont,
				HDMI_GENERIC9_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET8_9_LINE,
				HDMI_GENERIC9_LINE, line);
		break;
	case 10:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC10_CONT, cont,
				HDMI_GENERIC10_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET10_11_LINE,
				HDMI_GENERIC10_LINE, line);
		break;
	case 11:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC11_CONT, cont,
				HDMI_GENERIC11_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET10_11_LINE,
				HDMI_GENERIC11_LINE, line);
		break;
	case 12:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC12_CONT, cont,
				HDMI_GENERIC12_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET12_13_LINE,
				HDMI_GENERIC12_LINE, line);
		break;
	case 13:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC13_CONT, cont,
				HDMI_GENERIC13_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET12_13_LINE,
				HDMI_GENERIC13_LINE, line);
		break;
	case 14:
		REG_UPDATE_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC14_CONT, cont,
				HDMI_GENERIC14_SEND, send);
		REG_UPDATE(HDMI_TB_ENC_GENERIC_PACKET14_LINE,
				HDMI_GENERIC14_LINE, line);
		break;
	default:
		/* invalid HW packet index */
		DC_LOG_WARNING(
			"Invalid HW packet index: %s()\n",
			__func__);
		return;
	}
}

void hpo_enc401_update_hdmi_info_packets(struct hpo_frl_stream_encoder *enc,
				       const struct encoder_info_frame *info_frame)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	hpo_enc401_update_hdmi_info_packet(enc401, 0, &info_frame->avi);
	hpo_enc401_update_hdmi_info_packet(enc401, 1, &info_frame->vendor);
	hpo_enc401_update_hdmi_info_packet(enc401, 2, &info_frame->gamut);
	hpo_enc401_update_hdmi_info_packet(enc401, 3, &info_frame->spd);
	hpo_enc401_update_hdmi_info_packet(enc401, 4, &info_frame->hdrsmd);

	/* 5-10 used by dsc */
	hpo_enc401_update_hdmi_info_packet(enc401, 11, &info_frame->hfvsif);
	hpo_enc401_update_hdmi_info_packet(enc401, 12, &info_frame->vtem);
}

void hpo_enc401_hdmi_set_dsc_config(
	struct hpo_frl_stream_encoder *enc,
	struct dc_crtc_timing *timing,
	uint8_t *dsc_packed_pps)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
	enum optc_dsc_mode dsc_mode = OPTC_DSC_DISABLED;
	uint8_t i;

	if (dsc_packed_pps) {
		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 ||
				(timing->pixel_encoding == PIXEL_ENCODING_YCBCR422
					&& !timing->dsc_cfg.ycbcr422_simple))
			dsc_mode = OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED;
		else
			dsc_mode = OPTC_DSC_ENABLED_444;
	}

	switch (dsc_mode) {
	case OPTC_DSC_DISABLED:
		REG_UPDATE_2(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_PIXEL_ENCODING_TYPE, 0,
				FIFO_COMPRESSED_PIXEL_FORMAT, 0);
		break;
	case OPTC_DSC_ENABLED_NATIVE_SUBSAMPLED:
		REG_UPDATE_2(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_PIXEL_ENCODING_TYPE, 1,
				FIFO_COMPRESSED_PIXEL_FORMAT, 1);
		break;
	case OPTC_DSC_ENABLED_444:
		REG_UPDATE_2(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
				FIFO_PIXEL_ENCODING_TYPE, 1,
				FIFO_COMPRESSED_PIXEL_FORMAT, 0);
		break;
	}

	REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
			HDMI_DSC_MODE, dsc_mode);

	/* 5 packets for hdmi 2.1, use generic packets 5-10 to transmit*/
	/* TODO: do we change new bit to 0 after first transmission? Do we set End bit when exiting dsc? */
	if (dsc_mode != OPTC_DSC_DISABLED) {
		struct dc_info_packet emp_packet = {0};
		/* Need to find the padded h_total to recover the expected h_back*/
		uint32_t h_active_padding = timing->h_addressable % timing->dsc_cfg.num_slices_h;
		if (h_active_padding != 0)
			h_active_padding = timing->dsc_cfg.num_slices_h - h_active_padding;
		/* if YCBCR420, ensure slice width is even */
		uint32_t slice_width = (timing->h_addressable + h_active_padding) / timing->dsc_cfg.num_slices_h;
		if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420 &&
			slice_width % 2 != 0)
			h_active_padding += timing->dsc_cfg.num_slices_h;
		uint32_t dsc_pic_width = timing->h_addressable + timing->h_border_left + timing->h_border_right + h_active_padding;
		uint32_t h_back = timing->h_total - dsc_pic_width - timing->h_sync_width - timing->h_front_porch + h_active_padding;
		/* HCactivebytes = Slices * ceil(SliceWidth * bpp/8)
		 * Since bpp is stored as 16x actual value do (sliceWidth * bpp + 127) / 128 to ceil
		 */
		uint32_t h_cactive_bytes = timing->dsc_cfg.num_slices_h * (
				(dsc_pic_width /
						timing->dsc_cfg.num_slices_h * timing->dsc_cfg.bits_per_pixel + 127) / 128);

		/* Packet 0 */
		emp_packet.valid = true;
		emp_packet.hb0 = 0x7F; /* Default */
		emp_packet.hb1 = (1 << 7); /* First */
		emp_packet.hb2 = 0; /* Sequence index */
		emp_packet.sb[0] = (1 << 1) | (1 << 2) | (1 << 7); /* Sync[1] = 1, VFR[2] = 1, New[7] = 1*/
		emp_packet.sb[2] = 1; /* Organization_ID = 1 (Vesa spec)*/
		emp_packet.sb[4] = 2; /* Data_Set_Tag(LSB) = 2*/
		emp_packet.sb[6] = 136; /* Data_Set_Length(LSB) = 136*/
		memcpy(&emp_packet.sb[7], dsc_packed_pps, 21);
		hpo_enc401_update_hdmi_info_packet(enc401, 5, &emp_packet);

		/* Packets 1-3 */
		emp_packet.hb1 = 0; /* Not first or last*/
		for (i = 1; i < 4; i++) {
			emp_packet.hb2 = i; /* Sequence index */
			memcpy(&emp_packet.sb[0], &dsc_packed_pps[21 + 28 * (i - 1)], 28);
			hpo_enc401_update_hdmi_info_packet(enc401, 5 + i, &emp_packet);
		}

		/* Packet 4 */
		emp_packet.hb2 = 4; /* Sequence index */
		memcpy(&emp_packet.sb[0], &dsc_packed_pps[105], 23);
		emp_packet.sb[23] = (uint8_t)timing->h_front_porch; /* Hfront[7:0] */
		emp_packet.sb[24] = (uint8_t)(timing->h_front_porch >> 8); /* Hfront[15:8] */
		emp_packet.sb[25] = (uint8_t)timing->h_sync_width; /* Hsync[7:0] */
		emp_packet.sb[26] = (uint8_t)(timing->h_sync_width >> 8); /* Hsync[15:8] */
		emp_packet.sb[27] = (uint8_t)h_back; /* Hback[7:0] */
		hpo_enc401_update_hdmi_info_packet(enc401, 9, &emp_packet);

		/* Packet 5 */
		emp_packet.hb1 = (1 << 6); /* Last */
		emp_packet.hb2 = 5;
		emp_packet.sb[0] = (uint8_t)(h_back >> 8); /* Hback[15:8] */
		emp_packet.sb[1] = (uint8_t)h_cactive_bytes; /* HCactive_bytes[7:0] */
		emp_packet.sb[2] = (uint8_t)(h_cactive_bytes >> 8); /* HCactive_bytes[15:8] */
		hpo_enc401_update_hdmi_info_packet(enc401, 10, &emp_packet);

		/* Packet 0 - Clear New[7] */
		emp_packet.valid = true;
		emp_packet.hb0 = 0x7F; /* Default */
		emp_packet.hb1 = (1 << 7); /* First */
		emp_packet.hb2 = 0; /* Sequence index */
		emp_packet.sb[0] = (1 << 1) | (1 << 2); /* Sync[1] = 1, VFR[2] = 1*/
		emp_packet.sb[2] = 1; /* Organization_ID = 1 (Vesa spec)*/
		emp_packet.sb[4] = 2; /* Data_Set_Tag(LSB) = 2*/
		emp_packet.sb[6] = 136; /* Data_Set_Length(LSB) = 136*/
		memcpy(&emp_packet.sb[7], dsc_packed_pps, 21);
		hpo_enc401_update_hdmi_info_packet(enc401, 5, &emp_packet);
	}
}

void hpo_enc401_stop_hdmi_info_packets(
	struct hpo_frl_stream_encoder *enc)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	/* TODO: should also set extended metadata packet bit back to 0? */

	/* stop generic packets 0,1 on HDMI */
	REG_SET_4(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC0_CONT, 0,
		HDMI_GENERIC0_SEND, 0,
		HDMI_GENERIC1_CONT, 0,
		HDMI_GENERIC1_SEND, 0);
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET0_1_LINE, 0,
		HDMI_GENERIC0_LINE, 0,
		HDMI_GENERIC1_LINE, 0);

	/* stop generic packets 2,3 on HDMI */
	REG_SET_4(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC2_CONT, 0,
		HDMI_GENERIC2_SEND, 0,
		HDMI_GENERIC3_CONT, 0,
		HDMI_GENERIC3_SEND, 0);
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET2_3_LINE, 0,
		HDMI_GENERIC2_LINE, 0,
		HDMI_GENERIC3_LINE, 0);

	/* stop generic packets 4,5 on HDMI */
	REG_SET_4(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC4_CONT, 0,
		HDMI_GENERIC4_SEND, 0,
		HDMI_GENERIC5_CONT, 0,
		HDMI_GENERIC5_SEND, 0);
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET4_5_LINE, 0,
		HDMI_GENERIC4_LINE, 0,
		HDMI_GENERIC5_LINE, 0);

	/* stop generic packets 6,7 on HDMI */
	REG_SET_4(HDMI_TB_ENC_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC6_CONT, 0,
		HDMI_GENERIC6_SEND, 0,
		HDMI_GENERIC7_CONT, 0,
		HDMI_GENERIC7_SEND, 0);
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET6_7_LINE, 0,
		HDMI_GENERIC6_LINE, 0,
		HDMI_GENERIC7_LINE, 0);

	/* stop generic packets 8,9 on HDMI */
	REG_SET_4(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, 0,
		HDMI_GENERIC8_CONT, 0,
		HDMI_GENERIC8_SEND, 0,
		HDMI_GENERIC9_CONT, 0,
		HDMI_GENERIC9_SEND, 0);
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET8_9_LINE, 0,
		HDMI_GENERIC8_LINE, 0,
		HDMI_GENERIC9_LINE, 0);

	/* stop generic packets 10,11 on HDMI */
	REG_SET_4(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, 0,
		HDMI_GENERIC10_CONT, 0,
		HDMI_GENERIC10_SEND, 0,
		HDMI_GENERIC11_CONT, 0,
		HDMI_GENERIC11_SEND, 0);
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET10_11_LINE, 0,
		HDMI_GENERIC10_LINE, 0,
		HDMI_GENERIC11_LINE, 0);

	/* stop generic packets 12,13 on HDMI */
	REG_SET_4(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, 0,
		HDMI_GENERIC12_CONT, 0,
		HDMI_GENERIC12_SEND, 0,
		HDMI_GENERIC13_CONT, 0,
		HDMI_GENERIC13_SEND, 0);
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET12_13_LINE, 0,
		HDMI_GENERIC12_LINE, 0,
		HDMI_GENERIC13_LINE, 0);

	/* stop generic packets 14 on HDMI */
	REG_SET_2(HDMI_TB_ENC_GENERIC_PACKET_CONTROL1, 0,
		HDMI_GENERIC14_CONT, 0,
		HDMI_GENERIC14_SEND, 0);
	REG_SET(HDMI_TB_ENC_GENERIC_PACKET14_LINE, 0,
		HDMI_GENERIC14_LINE, 0);

}

//Covered both, rounding up or rounding down from FRL Link Rate /18.
static const struct frl_audio_clock_info frl_audio_clock_info_table[10] = {
	{166666, 4224, 171875, 5292, 156250, 5760, 156250},
	{166667, 4224, 171875, 5292, 156250, 5760, 156250},
	{333333, 4032, 328125, 5292, 312500, 6048, 328125},
	{333334, 4032, 328125, 5292, 312500, 6048, 328125},
	{444444, 4032, 437500, 3969, 312500, 6048, 437500},
	{444445, 4032, 437500, 3969, 312500, 6048, 437500},
	{555555, 3456, 468750, 3969, 390625, 5184, 468750},
	{555556, 3456, 468750, 3969, 390625, 5184, 468750},
	{666666, 3072, 500000, 3969, 468750, 4752, 515625},
	{666667, 3072, 500000, 3969, 468750, 4752, 515625}
};

void frl_get_audio_clock_info(
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

void hpo_enc401_setup_hdmi_audio(
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

	/* Setup audio in AFMT - program AFMT block associated with HPO */
	ASSERT (enc->afmt);
	enc->afmt->funcs->setup_hdmi_audio(enc->afmt);

	/* TODO: Same programming, but using HDMI_TB_ENC register */
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

void hpo_enc401_hdmi_audio_setup(
	struct hpo_frl_stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info)
{
	hpo_enc401_setup_hdmi_audio(enc, audio_crtc_info);
	ASSERT (enc->afmt);
	enc->afmt->funcs->se_audio_setup(enc->afmt, az_inst, info);
}

void hpo_enc401_hdmi_audio_disable(
	struct hpo_frl_stream_encoder *enc)
{
	ASSERT(enc->afmt);
	if (enc->afmt->funcs->afmt_powerdown)
		enc->afmt->funcs->afmt_powerdown(enc->afmt);
}

void hpo_enc401_audio_mute_control(
	struct hpo_frl_stream_encoder *enc,
	bool mute)
{
	ASSERT (enc->afmt);
	enc->afmt->funcs->audio_mute_control(enc->afmt, mute);
}

void enc401_stream_encoder_set_avmute(
	struct hpo_frl_stream_encoder *enc,
	bool enable)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
	unsigned int value = enable ? 1 : 0;

	REG_UPDATE(HDMI_TB_ENC_GC_CONTROL, HDMI_GC_AVMUTE, value);
}

/* Set Dynamic Metadata-configuration.
 *   enable_dme:         TRUE: enables Dynamic Metadata Enfine, FALSE: disables DME
 *   hubp_requestor_id:  HUBP physical instance that is the source of dynamic metadata
 *                       only needs to be set when enable_dme is TRUE
 *   dmdata_mode:        dynamic metadata packet type: DP, HDMI, or Dolby Vision
 *
 *   Ensure the OTG master update lock is set when changing DME configuration.
 */
void hpo_enc401_set_dynamic_metadata(struct hpo_frl_stream_encoder *enc,
	bool enable_dme,
	uint32_t hubp_requestor_id,
	enum dynamic_metadata_mode dmdata_mode)
{
	struct dcn401_hpo_frl_stream_encoder *enc401 = DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	if (enable_dme) {
		REG_UPDATE_2(DME_CONTROL,
			METADATA_HUBP_REQUESTOR_ID, hubp_requestor_id,
			METADATA_STREAM_TYPE, (dmdata_mode == dmdata_dolby_vision) ? 1 : 0);

		REG_UPDATE_3(HDMI_TB_ENC_METADATA_PACKET_CONTROL,
			HDMI_METADATA_PACKET_ENABLE, 1,
			HDMI_METADATA_PACKET_LINE_REFERENCE, 0,
			HDMI_METADATA_PACKET_LINE, 2);

		REG_UPDATE(DME_CONTROL,
			METADATA_ENGINE_EN, 1);
	} else {
		REG_UPDATE(DME_CONTROL,
			METADATA_ENGINE_EN, 0);

		REG_UPDATE(HDMI_TB_ENC_METADATA_PACKET_CONTROL,
			HDMI_METADATA_PACKET_ENABLE, 0);
	}
}

static const struct hpo_frl_stream_encoder_funcs dcn401_str_enc_funcs = {
	.hdmi_frl_enable		= hpo_enc401_enable,
	.hdmi_frl_unblank		= hpo_enc401_unblank,
	.hdmi_frl_blank			= hpo_enc401_blank,
	.hdmi_frl_set_stream_attribute	= hpo_enc401_set_hdmi_stream_attribute,
	.validate_hdmi_frl_output	= hpo_enc3_validate_hdmi_frl_output,
	.update_hdmi_info_packets	= hpo_enc401_update_hdmi_info_packets,
	.stop_hdmi_info_packets		= hpo_enc401_stop_hdmi_info_packets,
	.audio_mute_control		= hpo_enc401_audio_mute_control,
	.hdmi_audio_setup		= hpo_enc401_hdmi_audio_setup,
	.hdmi_audio_disable		= hpo_enc401_hdmi_audio_disable,
	.set_avmute			= enc401_stream_encoder_set_avmute,
	.read_state			= hpo_enc401_read_state,
	.hdmi_frl_set_dsc_config	= hpo_enc401_hdmi_set_dsc_config,
	.set_dynamic_metadata           = hpo_enc401_set_dynamic_metadata,
};

void dcn401_hpo_frl_stream_encoder_construct(
	struct dcn401_hpo_frl_stream_encoder *enc401,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct afmt *afmt,
	const struct dcn30_hpo_frl_stream_enc_registers *regs,
	const struct dcn401_hpo_frl_stream_encoder_shift *hpo_se_shift,
	const struct dcn401_hpo_frl_stream_encoder_mask *hpo_se_mask)
{
	enc401->base.funcs = &dcn401_str_enc_funcs;
	enc401->base.ctx = ctx;
	enc401->base.id = eng_id;
	enc401->base.bp = bp;
	enc401->base.vpg = vpg;
	enc401->base.afmt = afmt;
	enc401->regs = regs;
	enc401->hpo_se_shift = hpo_se_shift;
	enc401->hpo_se_mask = hpo_se_mask;
	enc401->base.stream_enc_inst = vpg->inst;
}
