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
#include "dcn30_hpo_frl_stream_encoder.h"
#include "reg_helper.h"
#include "hw_shared.h"
#include "dcn_calc_math.h"
#include "dml/dcn30/dcn30_fpu.h"

#undef DC_LOGGER
#define DC_LOGGER enc3->base.ctx->logger

#define DTRACE(str, ...) {DC_LOG_HDMI_FRL(str, ##__VA_ARGS__); }

#define DEBUG_FRL_CAP_CHK 1

#define REG(reg) (enc3->regs->reg)

#undef FN
#define FN(reg_name, field_name) enc3->hpo_se_shift->field_name, enc3->hpo_se_mask->field_name

#define CTX enc3->base.ctx

#define VBI_LINE_0 0

void hpo_enc3_enable(struct hpo_frl_stream_encoder *enc, int otg_inst)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	DC_LOG_HDMI_FRL("Entering [%s]\n", __func__);

	/* Enable DISPCLK, SOCCLK, and HDMISTREAMCLK */
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_CONTROL,
		   HDMI_STREAM_ENC_CLOCK_EN, 1);

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

	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL2,
		   FIFO_DB_DISABLE, 1);

	/* TODO: confirm if need to set HDMI_DB_DISABLE -- HW team only setting FIFO_DB_DISABLE */
	REG_UPDATE(HDMI_TB_ENC_DB_CONTROL,
		   HDMI_DB_DISABLE, 1);

	/* Set the input mux to select OTG source */
	REG_UPDATE(HDMI_STREAM_ENC_INPUT_MUX_CONTROL,
		   HDMI_STREAM_ENC_INPUT_MUX_SOURCE_SEL, otg_inst);

	DC_LOG_HDMI_FRL("Exiting [%s]\n", __func__);
}

void hpo_enc3_unblank(struct hpo_frl_stream_encoder *enc, int otg_inst)
{
	(void)otg_inst;
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

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

bool hpo_enc3_fifo_odm_enabled(struct hpo_frl_stream_encoder *enc)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
	uint32_t fifo_odm_combine_mode;

	REG_GET(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_ODM_COMBINE_MODE, &fifo_odm_combine_mode);

	return (fifo_odm_combine_mode != 0);
}

void hpo_enc3_blank(struct hpo_frl_stream_encoder *enc)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	/* Disable Clock Ramp Adjuster FIFO */
	REG_UPDATE_2(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
		   FIFO_ENABLE, 0,
		   FIFO_ODM_COMBINE_MODE, 0);

	/* Disable HDMI Tribyte Encoder */
	REG_UPDATE(HDMI_TB_ENC_CONTROL,
		   HDMI_TB_ENC_EN, 0);

	/* Disable DISPCLK, SOCCLK, and HDMISTREAMCLK */
	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_CONTROL,
		   HDMI_STREAM_ENC_CLOCK_EN, 0);
}

/* Setup stream encoder in hdmi mode
 * - Precondition: link is trained
 */
void hpo_enc3_set_hdmi_stream_attribute(struct hpo_frl_stream_encoder *enc,
					struct dc_crtc_timing *crtc_timing,
					struct frl_borrow_params *borrow_params,
					int odm_combine_num_segments)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
	uint32_t h_active;
	uint32_t h_blank;

	DC_LOG_HDMI_FRL("Entering [%s]\n", __func__);

	/* Configure pixel encoding */
	switch (crtc_timing->pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
			   HDMI_PIXEL_ENCODING, 1);
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			   FIFO_PIXEL_ENCODING, 1);
		break;
	case PIXEL_ENCODING_YCBCR420:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
			   HDMI_PIXEL_ENCODING, 2);
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			   FIFO_PIXEL_ENCODING, 2);
		break;
	default:
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
			   HDMI_PIXEL_ENCODING, 0);
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			   FIFO_PIXEL_ENCODING, 0);
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
	} else {
		REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
			HDMI_DSC_MODE, 0);
	}

	/* Configure ODM combine mode */
	switch (odm_combine_num_segments) {
	case 1:
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			   FIFO_ODM_COMBINE_MODE, 0);

		if (enc3->hpo_se_mask->HDMI_ODM_COMBINE_MODE)
			REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
				   HDMI_ODM_COMBINE_MODE, 0);
		break;
	case 2:
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			   FIFO_ODM_COMBINE_MODE, 1);

		if (enc3->hpo_se_mask->HDMI_ODM_COMBINE_MODE)
			REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
				   HDMI_ODM_COMBINE_MODE, 1);
		break;
	case 4:
		REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			   FIFO_ODM_COMBINE_MODE, 3);
		break;
	default:
		break;
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
	REG_UPDATE(HDMI_TB_ENC_VBI_PACKET_CONTROL1,
		   HDMI_ACP_SEND, 0);

	/* Enable Audio InfoFrame packet transmission. */
	REG_UPDATE(HDMI_TB_ENC_VBI_PACKET_CONTROL1,
		   HDMI_AUDIO_INFO_SEND, 1);

	/* update double-buffered AUDIO_INFO registers immediately */
	ASSERT(enc->afmt);
	enc->afmt->funcs->audio_info_immediate_update(enc->afmt);

	/* Select line number on which to send Audio InfoFrame packets */
	REG_UPDATE(HDMI_TB_ENC_VBI_PACKET_CONTROL1, HDMI_AUDIO_INFO_LINE,
		   VBI_LINE_0 + 2);

	/* set HDMI GC AVMUTE */
	REG_UPDATE(HDMI_TB_ENC_GC_CONTROL,
		   HDMI_GC_AVMUTE, 0);

	DC_LOG_HDMI_FRL("Exiting [%s]\n", __func__);
}

void hpo_enc3_update_hdmi_info_packets(struct hpo_frl_stream_encoder *enc,
				       const struct encoder_info_frame *info_frame)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	hpo_enc3_update_hdmi_info_packet(enc3, 0, &info_frame->avi);
	hpo_enc3_update_hdmi_info_packet(enc3, 1, &info_frame->vendor);
	hpo_enc3_update_hdmi_info_packet(enc3, 2, &info_frame->gamut);
	hpo_enc3_update_hdmi_info_packet(enc3, 3, &info_frame->spd);
	hpo_enc3_update_hdmi_info_packet(enc3, 4, &info_frame->hdrsmd);

	/* 5-10 used by dsc */
	hpo_enc3_update_hdmi_info_packet(enc3, 11, &info_frame->hfvsif);
	hpo_enc3_update_hdmi_info_packet(enc3, 12, &info_frame->vtem);
}

void hpo_enc3_update_hdmi_info_packet(struct dcn30_hpo_frl_stream_encoder *enc3,
				      uint32_t packet_index,
				      const struct dc_info_packet *info_packet)
{
	uint32_t cont, send, line;

	if (info_packet->valid) {
		enc3->base.vpg->funcs->update_generic_info_packet(
				enc3->base.vpg,
				packet_index,
				info_packet,
				true);

		/* enable transmission of packet(s) -
		 * packet transmission begins on the next frame */
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
		DC_LOG_WARNING("Invalid HW packet index: %s()\n", __func__);
		return;
	}
}

void hpo_enc3_hdmi_set_dsc_config(
	struct hpo_frl_stream_encoder *enc,
	struct dc_crtc_timing *timing,
	uint8_t *dsc_packed_pps)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
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

	REG_UPDATE(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_DSC_MODE, dsc_mode);

	REG_UPDATE(HDMI_TB_ENC_PIXEL_FORMAT,
			HDMI_DSC_MODE, dsc_mode);

	/* 5 packets for hdmi 2.1, use generic packets 5-10 to transmit*/
	/* TODO: do we change new bit to 0 after first transmission? Do we set End bit when exiting dsc? */
	if (dsc_mode != OPTC_DSC_DISABLED) {
		struct dc_info_packet emp_packet = {0};
		uint32_t dsc_pic_width = timing->h_addressable + timing->h_border_left + timing->h_border_right;
		uint32_t h_back = timing->h_total - dsc_pic_width - timing->h_sync_width - timing->h_front_porch;
		/* HCactivebytes = Slices * ceil(SliceWidth * bpp/8)
		 * use ... + 15) / 16 to achieve ceil since bpp is stored as 16x actual value
		 */
		uint32_t h_cactive_bytes = timing->dsc_cfg.num_slices_h * (
				(dsc_pic_width / timing->dsc_cfg.num_slices_h * timing->dsc_cfg.bits_per_pixel / 8 + 15) / 16);

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
		hpo_enc3_update_hdmi_info_packet(enc3, 5, &emp_packet);

		/* Packets 1-3 */
		emp_packet.hb1 = 0; /* Not first or last*/
		for (i = 1; i < 4; i++) {
			emp_packet.hb2 = i; /* Sequence index */
			memcpy(&emp_packet.sb[0], &dsc_packed_pps[21 + 28 * (i - 1)], 28);
			hpo_enc3_update_hdmi_info_packet(enc3, 5 + i, &emp_packet);
		}

		/* Packet 4 */
		emp_packet.hb2 = 4; /* Sequence index */
		memcpy(&emp_packet.sb[0], &dsc_packed_pps[105], 23);
		emp_packet.sb[23] = (uint8_t)timing->h_front_porch; /* Hfront[7:0] */
		emp_packet.sb[24] = (uint8_t)(timing->h_front_porch >> 8); /* Hfront[15:8] */
		emp_packet.sb[25] = (uint8_t)timing->h_sync_width; /* Hsync[7:0] */
		emp_packet.sb[26] = (uint8_t)(timing->h_sync_width >> 8); /* Hsync[15:8] */
		emp_packet.sb[27] = (uint8_t)h_back; /* Hback[7:0] */
		hpo_enc3_update_hdmi_info_packet(enc3, 9, &emp_packet);

		/* Packet 5 */
		emp_packet.hb1 = (1 << 6); /* Last */
		emp_packet.hb2 = 5;
		emp_packet.sb[0] = (uint8_t)(h_back >> 8); /* Hback[15:8] */
		emp_packet.sb[1] = (uint8_t)h_cactive_bytes; /* HCactive_bytes[7:0] */
		emp_packet.sb[2] = (uint8_t)(h_cactive_bytes >> 8); /* HCactive_bytes[15:8] */
		hpo_enc3_update_hdmi_info_packet(enc3, 10, &emp_packet);

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
		hpo_enc3_update_hdmi_info_packet(enc3, 5, &emp_packet);
	}
}

void hpo_enc3_stop_hdmi_info_packets(
	struct hpo_frl_stream_encoder *enc)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

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

void hpo_enc3_setup_hdmi_audio(
	struct hpo_frl_stream_encoder *enc,
	const struct audio_crtc_info *crtc_info)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
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

void hpo_enc3_hdmi_audio_setup(
	struct hpo_frl_stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info)
{
	hpo_enc3_setup_hdmi_audio(enc, audio_crtc_info);
	ASSERT (enc->afmt);
	enc->afmt->funcs->se_audio_setup(enc->afmt, az_inst, info);
}

void hpo_enc3_hdmi_audio_disable(
	struct hpo_frl_stream_encoder *enc)
{
	ASSERT(enc->afmt);
	if (enc->afmt->funcs->afmt_powerdown)
		enc->afmt->funcs->afmt_powerdown(enc->afmt);
}

void hpo_enc3_audio_mute_control(
	struct hpo_frl_stream_encoder *enc,
	bool mute)
{
	ASSERT (enc->afmt);
	enc->afmt->funcs->audio_mute_control(enc->afmt, mute);
}

void enc3_stream_encoder_set_avmute(
	struct hpo_frl_stream_encoder *enc,
	bool enable)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);
	unsigned int value = enable ? 1 : 0;

	REG_UPDATE(HDMI_TB_ENC_GC_CONTROL, HDMI_GC_AVMUTE, value);
}

static enum frl_cap_chk_result frl_cap_chk_uncompressed(
		struct hpo_frl_stream_encoder *enc,
		struct frl_cap_chk_params *params,
		struct frl_cap_chk_intermediates *inter)
{
	int res;
	DC_FP_START();
	res = frl_fpu_cap_chk_uncompressed(enc, params, inter);
	DC_FP_END();
	return res;
}

static enum frl_cap_chk_result frl_cap_chk_compressed(
		struct hpo_frl_stream_encoder *enc,
		struct frl_cap_chk_params *params,
		struct frl_cap_chk_intermediates *inter)
{
	int res;
	DC_FP_START();
	res = frl_fpu_cap_chk_compressed(enc, params, inter);
	DC_FP_END();
	return res;
}

static bool hpo_enc3_frl_cap_chk(
		struct hpo_frl_stream_encoder *enc,
		struct frl_cap_chk_params *params)
{
	struct frl_cap_chk_intermediates   inter;
	enum frl_cap_chk_result res;

	if (params->compressed)
		res = frl_cap_chk_compressed(enc, params, &inter);
	else
		res = frl_cap_chk_uncompressed(enc, params, &inter);

	return (res == FRL_CAP_CHK_OK);
}

bool hpo_enc3_validate_hdmi_frl_output(
	struct hpo_frl_stream_encoder *enc,
	const struct dc_crtc_timing *timing,
	const struct audio_check *audio,
	struct dc_hdmi_frl_link_settings *frl_link_settings,
	unsigned int dsc_max_rate)
{
	struct frl_cap_chk_params frl_params = {0};
	bool frl_check_res = false;

	/* Set inputs for FRL check */
	frl_params.lanes = frl_link_settings->frl_num_lanes;
	DC_FP_START();
	hpo_fpu_enc3_validate_hdmi_frl_output_link(enc,
						   frl_link_settings,
						   &frl_params,
						   timing,
						   dsc_max_rate);
	DC_FP_END();

	if (timing->display_color_depth == COLOR_DEPTH_888)
		frl_params.bpc = 8;
	else if (timing->display_color_depth == COLOR_DEPTH_101010)
		frl_params.bpc = 10;
	else
		frl_params.bpc = 12;

	switch (timing->hdmi_vic) {
	case 1:
		frl_params.vic = 95;
		break;
	case 2:
		frl_params.vic = 94;
		break;
	case 3:
		frl_params.vic = 93;
		break;
	case 4:
		frl_params.vic = 98;
		break;
	default:
		break;
	}
	frl_params.allow_all_bpp = timing->dsc_cfg.is_vic_all_bpp;
	if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR420)
		frl_params.pixel_encoding = HDMI_FRL_PIXEL_ENCODING_420;
	else if (timing->pixel_encoding == PIXEL_ENCODING_YCBCR422)
		frl_params.pixel_encoding = HDMI_FRL_PIXEL_ENCODING_422;
	else
		frl_params.pixel_encoding = HDMI_FRL_PIXEL_ENCODING_444;
	/* DSC parameters */
	frl_params.bypass_hc_target_calc = false;
	DC_FP_START();
	hpo_fpu_enc3_validate_hdmi_frl_output_timing(timing, audio, &frl_params);
	DC_FP_END();
	/* Audio parameters */
	/* TODO: set Audio parameters */

	if (audio->audio_packet_type == 2) {
		if (audio->max_channel_count <= 2
			  || (timing->v_addressable + timing->v_border_top + timing->v_border_bottom) <= 576)
			frl_params.layout = 0;
		else
			frl_params.layout = 1;

	}

	/* Check HDMI FRL Capacity and compute borrow parameters */
	frl_check_res = hpo_enc3_frl_cap_chk(enc, &frl_params);
	/* Save borrow parameters and average tribyte rate (for capture sideband) */
	if (frl_check_res) {
		frl_link_settings->borrow_params.audio_packets_line =
				frl_params.borrow_params.audio_packets_line;
		frl_link_settings->borrow_params.hc_active_target =
				frl_params.borrow_params.hc_active_target;
		frl_link_settings->borrow_params.hc_blank_target =
				frl_params.borrow_params.hc_blank_target;
		frl_link_settings->borrow_params.borrow_mode =
				(unsigned int) frl_params.borrow_params.borrow_mode;
		frl_link_settings->average_tribyte_rate = frl_params.average_tribyte_rate;
	}

	return frl_check_res;
}

void hpo_enc3_read_state(
	struct hpo_frl_stream_encoder *enc,
	struct hpo_frl_stream_encoder_state *state)
{
	int pixel_encoding;
	int color_depth;
	int odm_combine;
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

	ASSERT(state);

	REG_GET(HDMI_TB_ENC_CONTROL,
			HDMI_TB_ENC_EN, &state->stream_enc_enabled);

	REG_GET(HDMI_STREAM_ENC_INPUT_MUX_CONTROL,
			HDMI_STREAM_ENC_INPUT_MUX_SOURCE_SEL, &state->otg_inst);

	REG_GET_2(HDMI_TB_ENC_PIXEL_FORMAT,
			HDMI_PIXEL_ENCODING, &pixel_encoding,
			HDMI_DEEP_COLOR_DEPTH, &color_depth);

	REG_GET(HDMI_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_ODM_COMBINE_MODE, &odm_combine);

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

	state->num_odm_segments = odm_combine + 1;
}

/* Set Dynamic Metadata-configuration.
 *   enable_dme:         TRUE: enables Dynamic Metadata Enfine, FALSE: disables DME
 *   hubp_requestor_id:  HUBP physical instance that is the source of dynamic metadata
 *                       only needs to be set when enable_dme is TRUE
 *   dmdata_mode:        dynamic metadata packet type: DP, HDMI, or Dolby Vision
 *
 *   Ensure the OTG master update lock is set when changing DME configuration.
 */
void hpo_enc3_set_dynamic_metadata(struct hpo_frl_stream_encoder *enc,
	bool enable_dme,
	uint32_t hubp_requestor_id,
	enum dynamic_metadata_mode dmdata_mode)
{
	struct dcn30_hpo_frl_stream_encoder *enc3 = DCN30_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(enc);

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

static const struct hpo_frl_stream_encoder_funcs dcn30_str_enc_funcs = {
	.hdmi_frl_enable		= hpo_enc3_enable,
	.hdmi_frl_unblank		= hpo_enc3_unblank,
	.hdmi_frl_blank			= hpo_enc3_blank,
	.hdmi_frl_set_stream_attribute	= hpo_enc3_set_hdmi_stream_attribute,
	.update_hdmi_info_packets	= hpo_enc3_update_hdmi_info_packets,
	.stop_hdmi_info_packets		= hpo_enc3_stop_hdmi_info_packets,
	.audio_mute_control		= hpo_enc3_audio_mute_control,
	.hdmi_audio_setup		= hpo_enc3_hdmi_audio_setup,
	.hdmi_audio_disable		= hpo_enc3_hdmi_audio_disable,
	.set_avmute			= enc3_stream_encoder_set_avmute,
	.validate_hdmi_frl_output	= hpo_enc3_validate_hdmi_frl_output,
	.read_state			= hpo_enc3_read_state,
	.hdmi_frl_set_dsc_config	= hpo_enc3_hdmi_set_dsc_config,
	.set_dynamic_metadata           = hpo_enc3_set_dynamic_metadata,
	.hdmi_frl_fifo_odm_enabled = hpo_enc3_fifo_odm_enabled,
};

void dcn30_hpo_frl_stream_encoder_construct(
	struct dcn30_hpo_frl_stream_encoder *enc3,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct afmt *afmt,
	const struct dcn30_hpo_frl_stream_enc_registers *regs,
	const struct dcn30_hpo_frl_stream_encoder_shift *hpo_se_shift,
	const struct dcn30_hpo_frl_stream_encoder_mask *hpo_se_mask)
{
	enc3->base.funcs = &dcn30_str_enc_funcs;
	enc3->base.ctx = ctx;
	enc3->base.id = eng_id;
	enc3->base.bp = bp;
	enc3->base.vpg = vpg;
	enc3->base.afmt = afmt;
	enc3->regs = regs;
	enc3->hpo_se_shift = hpo_se_shift;
	enc3->hpo_se_mask = hpo_se_mask;
	enc3->base.stream_enc_inst = vpg->inst;
}
