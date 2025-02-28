/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#include "dcn30_dio_stream_encoder.h"
#include "reg_helper.h"
#include "hw_shared.h"
#include "dc.h"

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


void enc3_update_hdmi_info_packet(
	struct dcn10_stream_encoder *enc1,
	uint32_t packet_index,
	const struct dc_info_packet *info_packet)
{
	uint32_t cont, send, line;

	if (info_packet->valid) {
		enc1->base.vpg->funcs->update_generic_info_packet(
				enc1->base.vpg,
				packet_index,
				info_packet,
				true);

		/* enable transmission of packet(s) -
		 * packet transmission begins on the next frame */
		cont = 1;
		/* send packet(s) every frame */
		send = 1;
		/* select line number to send packets on */
		line = 2;
	} else {
		cont = 0;
		send = 0;
		line = 0;
	}

	/* DP_SEC_GSP[x]_LINE_REFERENCE - keep default value REFER_TO_DP_SOF */

	/* choose which generic packet control to use */
	switch (packet_index) {
	case 0:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC0_CONT, cont,
				HDMI_GENERIC0_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC0_LINE, line);
		break;
	case 1:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC1_CONT, cont,
				HDMI_GENERIC1_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL1,
				HDMI_GENERIC1_LINE, line);
		break;
	case 2:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC2_CONT, cont,
				HDMI_GENERIC2_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL2,
				HDMI_GENERIC2_LINE, line);
		break;
	case 3:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC3_CONT, cont,
				HDMI_GENERIC3_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL2,
				HDMI_GENERIC3_LINE, line);
		break;
	case 4:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC4_CONT, cont,
				HDMI_GENERIC4_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL3,
				HDMI_GENERIC4_LINE, line);
		break;
	case 5:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC5_CONT, cont,
				HDMI_GENERIC5_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL3,
				HDMI_GENERIC5_LINE, line);
		break;
	case 6:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC6_CONT, cont,
				HDMI_GENERIC6_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL4,
				HDMI_GENERIC6_LINE, line);
		break;
	case 7:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL0,
				HDMI_GENERIC7_CONT, cont,
				HDMI_GENERIC7_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL4,
				HDMI_GENERIC7_LINE, line);
		break;
	case 8:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL6,
				HDMI_GENERIC8_CONT, cont,
				HDMI_GENERIC8_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL7,
				HDMI_GENERIC8_LINE, line);
		break;
	case 9:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL6,
				HDMI_GENERIC9_CONT, cont,
				HDMI_GENERIC9_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL7,
				HDMI_GENERIC9_LINE, line);
		break;
	case 10:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL6,
				HDMI_GENERIC10_CONT, cont,
				HDMI_GENERIC10_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL8,
				HDMI_GENERIC10_LINE, line);
		break;
	case 11:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL6,
				HDMI_GENERIC11_CONT, cont,
				HDMI_GENERIC11_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL8,
				HDMI_GENERIC11_LINE, line);
		break;
	case 12:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL6,
				HDMI_GENERIC12_CONT, cont,
				HDMI_GENERIC12_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL9,
				HDMI_GENERIC12_LINE, line);
		break;
	case 13:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL6,
				HDMI_GENERIC13_CONT, cont,
				HDMI_GENERIC13_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL9,
				HDMI_GENERIC13_LINE, line);
		break;
	case 14:
		REG_UPDATE_2(HDMI_GENERIC_PACKET_CONTROL6,
				HDMI_GENERIC14_CONT, cont,
				HDMI_GENERIC14_SEND, send);
		REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL10,
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

void enc3_stream_encoder_update_hdmi_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* for bring up, disable dp double  TODO */
	REG_UPDATE(HDMI_DB_CONTROL, HDMI_DB_DISABLE, 1);
	REG_UPDATE(AFMT_CNTL, AFMT_AUDIO_CLOCK_EN, 1);

	/*Always add mandatory packets first followed by optional ones*/
	enc3_update_hdmi_info_packet(enc1, 0, &info_frame->avi);
	enc3_update_hdmi_info_packet(enc1, 5, &info_frame->hfvsif);
	enc3_update_hdmi_info_packet(enc1, 2, &info_frame->gamut);
	enc3_update_hdmi_info_packet(enc1, 1, &info_frame->vendor);
	enc3_update_hdmi_info_packet(enc1, 3, &info_frame->spd);
	enc3_update_hdmi_info_packet(enc1, 4, &info_frame->hdrsmd);
	enc3_update_hdmi_info_packet(enc1, 6, &info_frame->vtem);
}

void enc3_stream_encoder_stop_hdmi_info_packets(
	struct stream_encoder *enc)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	/* stop generic packets 0,1 on HDMI */
	REG_SET_4(HDMI_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC0_CONT, 0,
		HDMI_GENERIC0_SEND, 0,
		HDMI_GENERIC1_CONT, 0,
		HDMI_GENERIC1_SEND, 0);
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL1, 0,
		HDMI_GENERIC0_LINE, 0,
		HDMI_GENERIC1_LINE, 0);

	/* stop generic packets 2,3 on HDMI */
	REG_SET_4(HDMI_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC2_CONT, 0,
		HDMI_GENERIC2_SEND, 0,
		HDMI_GENERIC3_CONT, 0,
		HDMI_GENERIC3_SEND, 0);
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL2, 0,
		HDMI_GENERIC2_LINE, 0,
		HDMI_GENERIC3_LINE, 0);

	/* stop generic packets 4,5 on HDMI */
	REG_SET_4(HDMI_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC4_CONT, 0,
		HDMI_GENERIC4_SEND, 0,
		HDMI_GENERIC5_CONT, 0,
		HDMI_GENERIC5_SEND, 0);
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL3, 0,
		HDMI_GENERIC4_LINE, 0,
		HDMI_GENERIC5_LINE, 0);

	/* stop generic packets 6,7 on HDMI */
	REG_SET_4(HDMI_GENERIC_PACKET_CONTROL0, 0,
		HDMI_GENERIC6_CONT, 0,
		HDMI_GENERIC6_SEND, 0,
		HDMI_GENERIC7_CONT, 0,
		HDMI_GENERIC7_SEND, 0);
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL4, 0,
		HDMI_GENERIC6_LINE, 0,
		HDMI_GENERIC7_LINE, 0);

	/* stop generic packets 8,9 on HDMI */
	REG_SET_4(HDMI_GENERIC_PACKET_CONTROL6, 0,
		HDMI_GENERIC8_CONT, 0,
		HDMI_GENERIC8_SEND, 0,
		HDMI_GENERIC9_CONT, 0,
		HDMI_GENERIC9_SEND, 0);
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL7, 0,
		HDMI_GENERIC8_LINE, 0,
		HDMI_GENERIC9_LINE, 0);

	/* stop generic packets 10,11 on HDMI */
	REG_SET_4(HDMI_GENERIC_PACKET_CONTROL6, 0,
		HDMI_GENERIC10_CONT, 0,
		HDMI_GENERIC10_SEND, 0,
		HDMI_GENERIC11_CONT, 0,
		HDMI_GENERIC11_SEND, 0);
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL8, 0,
		HDMI_GENERIC10_LINE, 0,
		HDMI_GENERIC11_LINE, 0);

	/* stop generic packets 12,13 on HDMI */
	REG_SET_4(HDMI_GENERIC_PACKET_CONTROL6, 0,
		HDMI_GENERIC12_CONT, 0,
		HDMI_GENERIC12_SEND, 0,
		HDMI_GENERIC13_CONT, 0,
		HDMI_GENERIC13_SEND, 0);
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL9, 0,
		HDMI_GENERIC12_LINE, 0,
		HDMI_GENERIC13_LINE, 0);

	/* stop generic packet 14 on HDMI */
	REG_SET_2(HDMI_GENERIC_PACKET_CONTROL6, 0,
		HDMI_GENERIC14_CONT, 0,
		HDMI_GENERIC14_SEND, 0);
	REG_UPDATE(HDMI_GENERIC_PACKET_CONTROL10,
		HDMI_GENERIC14_LINE, 0);
}

/* Set DSC-related configuration.
 *   dsc_mode: 0 disables DSC, other values enable DSC in specified format
 *   sc_bytes_per_pixel: Bytes per pixel in u3.28 format
 *   dsc_slice_width: Slice width in pixels
 */
static void enc3_dp_set_dsc_config(struct stream_encoder *enc,
					enum optc_dsc_mode dsc_mode,
					uint32_t dsc_bytes_per_pixel,
					uint32_t dsc_slice_width)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE_2(DP_DSC_CNTL,
			DP_DSC_MODE, dsc_mode,
			DP_DSC_SLICE_WIDTH, dsc_slice_width);

	REG_SET(DP_DSC_BYTES_PER_PIXEL, 0,
		DP_DSC_BYTES_PER_PIXEL, dsc_bytes_per_pixel);
}


void enc3_dp_set_dsc_pps_info_packet(struct stream_encoder *enc,
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

		/* We need turn on clock before programming AFMT block
		 *
		 * TODO: We may not need this here anymore since update_generic_info_packet
		 * no longer touches AFMT
		 */
		REG_UPDATE(AFMT_CNTL, AFMT_AUDIO_CLOCK_EN, 1);

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


/* this function read dsc related register fields to be logged later in dcn10_log_hw_state
 * into a dcn_dsc_state struct.
 */
static void enc3_read_state(struct stream_encoder *enc, struct enc_state *s)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	//if dsc is enabled, continue to read
	REG_GET(DP_DSC_CNTL, DP_DSC_MODE, &s->dsc_mode);
	if (s->dsc_mode) {
		REG_GET(DP_DSC_CNTL, DP_DSC_SLICE_WIDTH, &s->dsc_slice_width);
		REG_GET(DP_GSP11_CNTL, DP_SEC_GSP11_LINE_NUM, &s->sec_gsp_pps_line_num);

		REG_GET(DP_MSA_VBID_MISC, DP_VBID6_LINE_REFERENCE, &s->vbid6_line_reference);
		REG_GET(DP_MSA_VBID_MISC, DP_VBID6_LINE_NUM, &s->vbid6_line_num);

		REG_GET(DP_GSP11_CNTL, DP_SEC_GSP11_ENABLE, &s->sec_gsp_pps_enable);
		REG_GET(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, &s->sec_stream_enable);
	}
}

void enc3_stream_encoder_update_dp_info_packets_sdp_line_num(
		struct stream_encoder *enc,
		struct encoder_info_frame *info_frame)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (info_frame->adaptive_sync.valid == true &&
		info_frame->sdp_line_num.adaptive_sync_line_num_valid == true) {
		//00: REFER_TO_DP_SOF, 01: REFER_TO_OTG_SOF
		REG_UPDATE(DP_SEC_CNTL1, DP_SEC_GSP5_LINE_REFERENCE, 1);

		REG_UPDATE(DP_SEC_CNTL5, DP_SEC_GSP5_LINE_NUM,
					info_frame->sdp_line_num.adaptive_sync_line_num);
	}
}

void enc3_stream_encoder_update_dp_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);
	uint32_t value = 0;
	uint32_t dmdata_packet_enabled = 0;

	if (info_frame->vsc.valid) {
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				0,  /* packetIndex */
				&info_frame->vsc,
				true);
	}
	/* TODO: VSC SDP at packetIndex 1 should be retricted only if PSR-SU on.
	 * There should have another Infopacket type (e.g. vsc_psrsu) for PSR_SU.
	 * In addition, currently the driver check the valid bit then update and
	 * send the corresponding Infopacket. For PSR-SU, the SDP only be sent
	 * while entering PSR-SU mode. So we need another parameter(e.g. send)
	 * in dc_info_packet to indicate which infopacket should be enabled by
	 * default here.
	 */
	if (info_frame->vsc.valid) {
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				1,  /* packetIndex */
				&info_frame->vsc,
				true);
	}
	/* TODO: VSC SDP at packetIndex 1 should be restricted only if PSR-SU on.
	 * There should have another Infopacket type (e.g. vsc_psrsu) for PSR_SU.
	 * In addition, currently the driver check the valid bit then update and
	 * send the corresponding Infopacket. For PSR-SU, the SDP only be sent
	 * while entering PSR-SU mode. So we need another parameter(e.g. send)
	 * in dc_info_packet to indicate which infopacket should be enabled by
	 * default here.
	 */
	if (info_frame->vsc.valid) {
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				1,  /* packetIndex */
				&info_frame->vsc,
				true);
	}
	if (info_frame->spd.valid) {
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				2,  /* packetIndex */
				&info_frame->spd,
				true);
	}
	if (info_frame->hdrsmd.valid) {
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				3,  /* packetIndex */
				&info_frame->hdrsmd,
				true);
	}
	/* packetIndex 4 is used for send immediate sdp message, and please
	 * use other packetIndex (such as 5,6) for other info packet
	 */

	if (info_frame->adaptive_sync.valid)
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				5,  /* packetIndex */
				&info_frame->adaptive_sync,
				true);

	/* enable/disable transmission of packet(s).
	 * If enabled, packet transmission begins on the next frame
	 */
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_GSP0_ENABLE, info_frame->vsc.valid);
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_GSP2_ENABLE, info_frame->spd.valid);
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_GSP3_ENABLE, info_frame->hdrsmd.valid);
	REG_UPDATE(DP_SEC_CNTL, DP_SEC_GSP5_ENABLE, info_frame->adaptive_sync.valid);

	/* This bit is the master enable bit.
	 * When enabling secondary stream engine,
	 * this master bit must also be set.
	 * This register shared with audio info frame.
	 * Therefore we need to enable master bit
	 * if at least on of the fields is not 0
	 */
	value = REG_READ(DP_SEC_CNTL);
	if (value)
		REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);

	/* check if dynamic metadata packet transmission is enabled */
	REG_GET(DP_SEC_METADATA_TRANSMISSION,
			DP_SEC_METADATA_PACKET_ENABLE, &dmdata_packet_enabled);

	if (dmdata_packet_enabled)
		REG_UPDATE(DP_SEC_CNTL, DP_SEC_STREAM_ENABLE, 1);
}

static void enc3_dp_set_odm_combine(
	struct stream_encoder *enc,
	bool odm_combine)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	REG_UPDATE(DP_PIXEL_FORMAT, DP_PIXEL_COMBINE, odm_combine);
}

/* setup stream encoder in dvi mode */
static void enc3_stream_encoder_dvi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	bool is_dual_link)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	if (!enc->ctx->dc->debug.avoid_vbios_exec_table) {
		struct bp_encoder_control cntl = {0};

		cntl.action = ENCODER_CONTROL_SETUP;
		cntl.engine_id = enc1->base.id;
		cntl.signal = is_dual_link ?
			SIGNAL_TYPE_DVI_DUAL_LINK : SIGNAL_TYPE_DVI_SINGLE_LINK;
		cntl.enable_dp_audio = false;
		cntl.pixel_clock = crtc_timing->pix_clk_100hz / 10;
		cntl.lanes_number = (is_dual_link) ? LANE_COUNT_EIGHT : LANE_COUNT_FOUR;

		if (enc1->base.bp->funcs->encoder_control(
				enc1->base.bp, &cntl) != BP_RESULT_OK)
			return;

	} else {

		//Set pattern for clock channel, default vlue 0x63 does not work
		REG_UPDATE(DIG_CLOCK_PATTERN, DIG_CLOCK_PATTERN, 0x1F);

		//DIG_BE_TMDS_DVI_MODE : TMDS-DVI mode is already set in link_encoder_setup

		//DIG_SOURCE_SELECT is already set in dig_connect_to_otg

		/* set DIG_START to 0x1 to reset FIFO */
		REG_UPDATE(DIG_FE_CNTL, DIG_START, 1);
		udelay(1);

		/* write 0 to take the FIFO out of reset */
		REG_UPDATE(DIG_FE_CNTL, DIG_START, 0);
		udelay(1);
	}

	ASSERT(crtc_timing->pixel_encoding == PIXEL_ENCODING_RGB);
	ASSERT(crtc_timing->display_color_depth == COLOR_DEPTH_888);
	enc1_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);
}

/* setup stream encoder in hdmi mode */
static void enc3_stream_encoder_hdmi_set_stream_attribute(
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

		/* set DIG_START to 0x1 to reset FIFO */
		REG_UPDATE(DIG_FE_CNTL, DIG_START, 1);
		udelay(1);

		/* write 0 to take the FIFO out of reset */
		REG_UPDATE(DIG_FE_CNTL, DIG_START, 0);
		udelay(1);
	}

	/* Configure pixel encoding */
	enc1_stream_encoder_set_stream_attribute_helper(enc1, crtc_timing);

	/* setup HDMI engine */
	REG_UPDATE_6(HDMI_CONTROL,
		HDMI_PACKET_GEN_VERSION, 1,
		HDMI_KEEPOUT_MODE, 1,
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

	/* update double-buffered AUDIO_INFO registers immediately */
	ASSERT (enc->afmt);
	enc->afmt->funcs->audio_info_immediate_update(enc->afmt);

	/* Select line number on which to send Audio InfoFrame packets */
	REG_UPDATE(HDMI_INFOFRAME_CONTROL1, HDMI_AUDIO_INFO_LINE,
				VBI_LINE_0 + 2);

	/* set HDMI GC AVMUTE */
	REG_UPDATE(HDMI_GC, HDMI_GC_AVMUTE, 0);
}

void enc3_audio_mute_control(
	struct stream_encoder *enc,
	bool mute)
{
	ASSERT (enc->afmt);
	enc->afmt->funcs->audio_mute_control(enc->afmt, mute);
}

void enc3_se_dp_audio_setup(
	struct stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info)
{
	ASSERT (enc->afmt);
	enc->afmt->funcs->se_audio_setup(enc->afmt, az_inst, info);
}

#define DP_SEC_AUD_N__DP_SEC_AUD_N__DEFAULT 0x8000
#define DP_SEC_TIMESTAMP__DP_SEC_TIMESTAMP_MODE__AUTO_CALC 1

static void enc3_se_setup_dp_audio(
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

	ASSERT (enc->afmt);
	enc->afmt->funcs->setup_dp_audio(enc->afmt);
}

void enc3_se_dp_audio_enable(
	struct stream_encoder *enc)
{
	enc1_se_enable_audio_clock(enc, true);
	enc3_se_setup_dp_audio(enc);
	enc1_se_enable_dp_audio(enc);
}

static void enc3_se_setup_hdmi_audio(
	struct stream_encoder *enc,
	const struct audio_crtc_info *crtc_info)
{
	struct dcn10_stream_encoder *enc1 = DCN10STRENC_FROM_STRENC(enc);

	struct audio_clock_info audio_clock_info = {0};

	/* Setup audio in AFMT - program AFMT block associated with DIO */
	ASSERT (enc->afmt);
	enc->afmt->funcs->setup_hdmi_audio(enc->afmt);

	/* HDMI_AUDIO_PACKET_CONTROL */
	REG_UPDATE(HDMI_AUDIO_PACKET_CONTROL,
			HDMI_AUDIO_DELAY_EN, 1);

	/* HDMI_ACR_PACKET_CONTROL */
	REG_UPDATE_3(HDMI_ACR_PACKET_CONTROL,
			HDMI_ACR_AUTO_SEND, 1,
			HDMI_ACR_SOURCE, 0,
			HDMI_ACR_AUDIO_PRIORITY, 0);

	/* Program audio clock sample/regeneration parameters */
	get_audio_clock_info(crtc_info->color_depth,
			     crtc_info->requested_pixel_clock_100Hz,
			     crtc_info->calculated_pixel_clock_100Hz,
			     &audio_clock_info);
	DC_LOG_HW_AUDIO(
			"\n%s:Input::requested_pixel_clock_100Hz = %d"	\
			"calculated_pixel_clock_100Hz = %d \n", __func__,	\
			crtc_info->requested_pixel_clock_100Hz,		\
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

void enc3_se_hdmi_audio_setup(
	struct stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info,
	struct audio_crtc_info *audio_crtc_info)
{
	enc1_se_enable_audio_clock(enc, true);
	enc3_se_setup_hdmi_audio(enc, audio_crtc_info);
	ASSERT (enc->afmt);
	enc->afmt->funcs->se_audio_setup(enc->afmt, az_inst, info);
}


static const struct stream_encoder_funcs dcn30_str_enc_funcs = {
	.dp_set_odm_combine =
		enc3_dp_set_odm_combine,
	.dp_set_stream_attribute =
		enc2_stream_encoder_dp_set_stream_attribute,
	.hdmi_set_stream_attribute =
		enc3_stream_encoder_hdmi_set_stream_attribute,
	.dvi_set_stream_attribute =
		enc3_stream_encoder_dvi_set_stream_attribute,
	.set_throttled_vcp_size =
		enc1_stream_encoder_set_throttled_vcp_size,
	.update_hdmi_info_packets =
		enc3_stream_encoder_update_hdmi_info_packets,
	.stop_hdmi_info_packets =
		enc3_stream_encoder_stop_hdmi_info_packets,
	.update_dp_info_packets_sdp_line_num =
		enc3_stream_encoder_update_dp_info_packets_sdp_line_num,
	.update_dp_info_packets =
		enc3_stream_encoder_update_dp_info_packets,
	.stop_dp_info_packets =
		enc1_stream_encoder_stop_dp_info_packets,
	.dp_blank =
		enc1_stream_encoder_dp_blank,
	.dp_unblank =
		enc2_stream_encoder_dp_unblank,
	.audio_mute_control = enc3_audio_mute_control,

	.dp_audio_setup = enc3_se_dp_audio_setup,
	.dp_audio_enable = enc3_se_dp_audio_enable,
	.dp_audio_disable = enc1_se_dp_audio_disable,

	.hdmi_audio_setup = enc3_se_hdmi_audio_setup,
	.hdmi_audio_disable = enc1_se_hdmi_audio_disable,
	.setup_stereo_sync  = enc1_setup_stereo_sync,
	.set_avmute = enc1_stream_encoder_set_avmute,
	.dig_connect_to_otg = enc1_dig_connect_to_otg,
	.dig_source_otg = enc1_dig_source_otg,

	.dp_get_pixel_format  = enc1_stream_encoder_dp_get_pixel_format,

	.enc_read_state = enc3_read_state,
	.dp_set_dsc_config = enc3_dp_set_dsc_config,
	.dp_set_dsc_pps_info_packet = enc3_dp_set_dsc_pps_info_packet,
	.set_dynamic_metadata = enc2_set_dynamic_metadata,
	.hdmi_reset_stream_attribute = enc1_reset_hdmi_stream_attribute,

	.get_fifo_cal_average_level = enc2_get_fifo_cal_average_level,
};

void dcn30_dio_stream_encoder_construct(
	struct dcn10_stream_encoder *enc1,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct afmt *afmt,
	const struct dcn10_stream_enc_registers *regs,
	const struct dcn10_stream_encoder_shift *se_shift,
	const struct dcn10_stream_encoder_mask *se_mask)
{
	enc1->base.funcs = &dcn30_str_enc_funcs;
	enc1->base.ctx = ctx;
	enc1->base.id = eng_id;
	enc1->base.bp = bp;
	enc1->base.vpg = vpg;
	enc1->base.afmt = afmt;
	enc1->regs = regs;
	enc1->se_shift = se_shift;
	enc1->se_mask = se_mask;
	enc1->base.stream_enc_inst = vpg->inst;
}

