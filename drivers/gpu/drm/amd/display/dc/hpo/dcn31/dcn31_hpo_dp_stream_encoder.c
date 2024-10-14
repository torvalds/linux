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
#include "dcn31_hpo_dp_stream_encoder.h"
#include "reg_helper.h"
#include "dc.h"

#define DC_LOGGER \
		enc3->base.ctx->logger

#define REG(reg)\
	(enc3->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	enc3->hpo_se_shift->field_name, enc3->hpo_se_mask->field_name

#define CTX \
	enc3->base.ctx


enum dp2_pixel_encoding {
	DP_SYM32_ENC_PIXEL_ENCODING_RGB_YCBCR444,
	DP_SYM32_ENC_PIXEL_ENCODING_YCBCR422,
	DP_SYM32_ENC_PIXEL_ENCODING_YCBCR420,
	DP_SYM32_ENC_PIXEL_ENCODING_Y_ONLY
};

enum dp2_uncompressed_component_depth {
	DP_SYM32_ENC_COMPONENT_DEPTH_6BPC,
	DP_SYM32_ENC_COMPONENT_DEPTH_8BPC,
	DP_SYM32_ENC_COMPONENT_DEPTH_10BPC,
	DP_SYM32_ENC_COMPONENT_DEPTH_12BPC
};


static void dcn31_hpo_dp_stream_enc_enable_stream(
		struct hpo_dp_stream_encoder *enc)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	/* Enable all clocks in the DP_STREAM_ENC */
	REG_UPDATE(DP_STREAM_ENC_CLOCK_CONTROL,
			DP_STREAM_ENC_CLOCK_EN, 1);

	/* Assert reset to the DP_SYM32_ENC logic */
	REG_UPDATE(DP_SYM32_ENC_CONTROL,
			DP_SYM32_ENC_RESET, 1);
	/* Wait for reset to complete (to assert) */
	REG_WAIT(DP_SYM32_ENC_CONTROL,
			DP_SYM32_ENC_RESET_DONE, 1,
			1, 10);

	/* De-assert reset to the DP_SYM32_ENC logic */
	REG_UPDATE(DP_SYM32_ENC_CONTROL,
			DP_SYM32_ENC_RESET, 0);
	/* Wait for reset to de-assert */
	REG_WAIT(DP_SYM32_ENC_CONTROL,
			DP_SYM32_ENC_RESET_DONE, 0,
			1, 10);

	/* Enable idle pattern generation */
	REG_UPDATE(DP_SYM32_ENC_CONTROL,
			DP_SYM32_ENC_ENABLE, 1);
}

static void dcn31_hpo_dp_stream_enc_dp_unblank(
		struct hpo_dp_stream_encoder *enc,
		uint32_t stream_source)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	/* Set the input mux for video stream source */
	REG_UPDATE(DP_STREAM_ENC_INPUT_MUX_CONTROL,
			DP_STREAM_ENC_INPUT_MUX_PIXEL_STREAM_SOURCE_SEL, stream_source);

	/* Enable video transmission in main framer */
	REG_UPDATE(DP_SYM32_ENC_VID_STREAM_CONTROL,
			VID_STREAM_ENABLE, 1);

	/* Reset and Enable Pixel to Symbol FIFO */
	REG_UPDATE(DP_SYM32_ENC_VID_FIFO_CONTROL,
			PIXEL_TO_SYMBOL_FIFO_RESET, 1);
	REG_WAIT(DP_SYM32_ENC_VID_FIFO_CONTROL,
			PIXEL_TO_SYMBOL_FIFO_RESET_DONE, 1,
			1, 10);
	REG_UPDATE(DP_SYM32_ENC_VID_FIFO_CONTROL,
			PIXEL_TO_SYMBOL_FIFO_RESET, 0);
	REG_WAIT(DP_SYM32_ENC_VID_FIFO_CONTROL,	/* Disable Clock Ramp Adjuster FIFO */
			PIXEL_TO_SYMBOL_FIFO_RESET_DONE, 0,
			1, 10);
	REG_UPDATE(DP_SYM32_ENC_VID_FIFO_CONTROL,
			PIXEL_TO_SYMBOL_FIFO_ENABLE, 1);

	/* Reset and Enable Clock Ramp Adjuster FIFO */
	REG_UPDATE(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_RESET, 1);
	REG_WAIT(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_RESET_DONE, 1,
			1, 10);
	REG_UPDATE(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_RESET, 0);
	REG_WAIT(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_RESET_DONE, 0,
			1, 10);

	/* For Debug -- Enable CRC */
	REG_UPDATE_2(DP_SYM32_ENC_VID_CRC_CONTROL,
			CRC_ENABLE, 1,
			CRC_CONT_MODE_ENABLE, 1);

	REG_UPDATE(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_ENABLE, 1);
}

static void dcn31_hpo_dp_stream_enc_dp_blank(
		struct hpo_dp_stream_encoder *enc)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	/* Disable video transmission */
	REG_UPDATE(DP_SYM32_ENC_VID_STREAM_CONTROL,
			VID_STREAM_ENABLE, 0);

	/* Wait for video stream transmission disabled
	 * Larger delay to wait until VBLANK - use max retry of
	 * 10us*5000=50ms. This covers 41.7ms of minimum 24 Hz mode +
	 * a little more because we may not trust delay accuracy.
	 */
	REG_WAIT(DP_SYM32_ENC_VID_STREAM_CONTROL,
			VID_STREAM_STATUS, 0,
			10, 5000);

	/* Disable SDP transmission */
	REG_UPDATE(DP_SYM32_ENC_SDP_CONTROL,
			SDP_STREAM_ENABLE, 0);

	/* Disable Pixel to Symbol FIFO */
	REG_UPDATE(DP_SYM32_ENC_VID_FIFO_CONTROL,
			PIXEL_TO_SYMBOL_FIFO_ENABLE, 0);

	/* Disable Clock Ramp Adjuster FIFO */
	REG_UPDATE(DP_STREAM_ENC_CLOCK_RAMP_ADJUSTER_FIFO_STATUS_CONTROL0,
			FIFO_ENABLE, 0);
}

static void dcn31_hpo_dp_stream_enc_disable(
		struct hpo_dp_stream_encoder *enc)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	/* Disable DP_SYM32_ENC */
	REG_UPDATE(DP_SYM32_ENC_CONTROL,
			DP_SYM32_ENC_ENABLE, 0);

	/* Disable clocks in the DP_STREAM_ENC */
	REG_UPDATE(DP_STREAM_ENC_CLOCK_CONTROL,
			DP_STREAM_ENC_CLOCK_EN, 0);
}

static void dcn31_hpo_dp_stream_enc_set_stream_attribute(
		struct hpo_dp_stream_encoder *enc,
		struct dc_crtc_timing *crtc_timing,
		enum dc_color_space output_color_space,
		bool use_vsc_sdp_for_colorimetry,
		bool compressed_format,
		bool double_buffer_en)
{
	enum dp2_pixel_encoding pixel_encoding;
	enum dp2_uncompressed_component_depth component_depth;
	uint32_t h_active_start;
	uint32_t v_active_start;
	uint32_t h_blank;
	uint32_t h_back_porch;
	uint32_t h_width;
	uint32_t v_height;
	uint64_t v_freq;
	uint8_t misc0 = 0;
	uint8_t misc1 = 0;
	uint8_t hsp;
	uint8_t vsp;

	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);
	struct dc_crtc_timing hw_crtc_timing = *crtc_timing;

	/* MISC0[0]   = 0    video and link clocks are asynchronous
	 * MISC1[0]   = 0    interlace not supported
	 * MISC1[2:1] = 0    stereo field is handled by hardware
	 * MISC1[5:3] = 0    Reserved
	 */

	/* Interlaced not supported */
	if (hw_crtc_timing.flags.INTERLACE) {
		BREAK_TO_DEBUGGER();
	}

	/* Double buffer enable for MSA and pixel format registers
	 * Only double buffer for changing stream attributes for active streams
	 * Do not double buffer when initially enabling a stream
	 */
	REG_UPDATE(DP_SYM32_ENC_VID_MSA_DOUBLE_BUFFER_CONTROL,
			MSA_DOUBLE_BUFFER_ENABLE, double_buffer_en);
	REG_UPDATE(DP_SYM32_ENC_VID_PIXEL_FORMAT_DOUBLE_BUFFER_CONTROL,
			PIXEL_FORMAT_DOUBLE_BUFFER_ENABLE, double_buffer_en);

	/* Pixel Encoding */
	switch (hw_crtc_timing.pixel_encoding) {
	case PIXEL_ENCODING_YCBCR422:
		pixel_encoding = DP_SYM32_ENC_PIXEL_ENCODING_YCBCR422;
		misc0 = misc0 | 0x2;  // MISC0[2:1] = 01
		break;
	case PIXEL_ENCODING_YCBCR444:
		pixel_encoding = DP_SYM32_ENC_PIXEL_ENCODING_RGB_YCBCR444;
		misc0 = misc0 | 0x4;  // MISC0[2:1] = 10

		if (hw_crtc_timing.flags.Y_ONLY) {
			pixel_encoding =  DP_SYM32_ENC_PIXEL_ENCODING_Y_ONLY;
			if (hw_crtc_timing.display_color_depth != COLOR_DEPTH_666) {
				/* HW testing only, no use case yet.
				 * Color depth of Y-only could be
				 * 8, 10, 12, 16 bits
				 */
				misc1 = misc1 | 0x80;  // MISC1[7] = 1
			}
		}
		break;
	case PIXEL_ENCODING_YCBCR420:
		pixel_encoding = DP_SYM32_ENC_PIXEL_ENCODING_YCBCR420;
		misc1 = misc1 | 0x40;   // MISC1[6] = 1
		break;
	case PIXEL_ENCODING_RGB:
	default:
		pixel_encoding = DP_SYM32_ENC_PIXEL_ENCODING_RGB_YCBCR444;
		break;
	}

	/* For YCbCr420 and BT2020 Colorimetry Formats, VSC SDP shall be used.
	 * When MISC1, bit 6, is Set to 1, a Source device uses a VSC SDP to indicate the
	 * Pixel Encoding/Colorimetry Format and that a Sink device shall ignore MISC1, bit 7,
	 * and MISC0, bits 7:1 (MISC1, bit 7, and MISC0, bits 7:1, become "don't care").
	 */
	if (use_vsc_sdp_for_colorimetry)
		misc1 = misc1 | 0x40;
	else
		misc1 = misc1 & ~0x40;

	/* Color depth */
	switch (hw_crtc_timing.display_color_depth) {
	case COLOR_DEPTH_666:
		component_depth = DP_SYM32_ENC_COMPONENT_DEPTH_6BPC;
		// MISC0[7:5] = 000
		break;
	case COLOR_DEPTH_888:
		component_depth = DP_SYM32_ENC_COMPONENT_DEPTH_8BPC;
		misc0 = misc0 | 0x20;  // MISC0[7:5] = 001
		break;
	case COLOR_DEPTH_101010:
		component_depth = DP_SYM32_ENC_COMPONENT_DEPTH_10BPC;
		misc0 = misc0 | 0x40;  // MISC0[7:5] = 010
		break;
	case COLOR_DEPTH_121212:
		component_depth = DP_SYM32_ENC_COMPONENT_DEPTH_12BPC;
		misc0 = misc0 | 0x60;  // MISC0[7:5] = 011
		break;
	default:
		component_depth = DP_SYM32_ENC_COMPONENT_DEPTH_6BPC;
		break;
	}

	REG_UPDATE_3(DP_SYM32_ENC_VID_PIXEL_FORMAT,
			PIXEL_ENCODING_TYPE, compressed_format,
			UNCOMPRESSED_PIXEL_ENCODING, pixel_encoding,
			UNCOMPRESSED_COMPONENT_DEPTH, component_depth);

	switch (output_color_space) {
	case COLOR_SPACE_SRGB:
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		break;
	case COLOR_SPACE_SRGB_LIMITED:
		misc0 = misc0 | 0x8; /* bit3=1 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		break;
	case COLOR_SPACE_YCBCR601:
	case COLOR_SPACE_YCBCR601_LIMITED:
		misc0 = misc0 | 0x8; /* bit3=1, bit4=0 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
			misc0 = misc0 | 0x2; /* bit2=0, bit1=1 */
		else if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR444)
			misc0 = misc0 | 0x4; /* bit2=1, bit1=0 */
		break;
	case COLOR_SPACE_YCBCR709:
	case COLOR_SPACE_YCBCR709_LIMITED:
		misc0 = misc0 | 0x18; /* bit3=1, bit4=1 */
		misc1 = misc1 & ~0x80; /* bit7 = 0*/
		if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
			misc0 = misc0 | 0x2; /* bit2=0, bit1=1 */
		else if (hw_crtc_timing.pixel_encoding == PIXEL_ENCODING_YCBCR444)
			misc0 = misc0 | 0x4; /* bit2=1, bit1=0 */
		break;
	case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
	case COLOR_SPACE_2020_RGB_FULLRANGE:
	case COLOR_SPACE_2020_YCBCR:
	case COLOR_SPACE_XR_RGB:
	case COLOR_SPACE_MSREF_SCRGB:
	case COLOR_SPACE_ADOBERGB:
	case COLOR_SPACE_DCIP3:
	case COLOR_SPACE_XV_YCC_709:
	case COLOR_SPACE_XV_YCC_601:
	case COLOR_SPACE_DISPLAYNATIVE:
	case COLOR_SPACE_DOLBYVISION:
	case COLOR_SPACE_APPCTRL:
	case COLOR_SPACE_CUSTOMPOINTS:
	case COLOR_SPACE_UNKNOWN:
	case COLOR_SPACE_YCBCR709_BLACK:
		/* do nothing */
		break;
	}

	/* calculate from vesa timing parameters
	 * h_active_start related to leading edge of sync
	 */
	h_blank = hw_crtc_timing.h_total - hw_crtc_timing.h_border_left -
			hw_crtc_timing.h_addressable - hw_crtc_timing.h_border_right;

	h_back_porch = h_blank - hw_crtc_timing.h_front_porch -
			hw_crtc_timing.h_sync_width;

	/* start at beginning of left border */
	h_active_start = hw_crtc_timing.h_sync_width + h_back_porch;

	v_active_start = hw_crtc_timing.v_total - hw_crtc_timing.v_border_top -
			hw_crtc_timing.v_addressable - hw_crtc_timing.v_border_bottom -
			hw_crtc_timing.v_front_porch;

	h_width = hw_crtc_timing.h_border_left + hw_crtc_timing.h_addressable + hw_crtc_timing.h_border_right;
	v_height = hw_crtc_timing.v_border_top + hw_crtc_timing.v_addressable + hw_crtc_timing.v_border_bottom;
	hsp = hw_crtc_timing.flags.HSYNC_POSITIVE_POLARITY ? 0 : 0x80;
	vsp = hw_crtc_timing.flags.VSYNC_POSITIVE_POLARITY ? 0 : 0x80;
	v_freq = (uint64_t)hw_crtc_timing.pix_clk_100hz * 100;

	/*   MSA Packet Mapping to 32-bit Link Symbols - DP2 spec, section 2.7.4.1
	 *
	 *                      Lane 0           Lane 1          Lane 2         Lane 3
	 *    MSA[0] = {             0,               0,              0,  VFREQ[47:40]}
	 *    MSA[1] = {             0,               0,              0,  VFREQ[39:32]}
	 *    MSA[2] = {             0,               0,              0,  VFREQ[31:24]}
	 *    MSA[3] = {  HTotal[15:8],    HStart[15:8],   HWidth[15:8],  VFREQ[23:16]}
	 *    MSA[4] = {  HTotal[ 7:0],    HStart[ 7:0],   HWidth[ 7:0],  VFREQ[15: 8]}
	 *    MSA[5] = {  VTotal[15:8],    VStart[15:8],  VHeight[15:8],  VFREQ[ 7: 0]}
	 *    MSA[6] = {  VTotal[ 7:0],    VStart[ 7:0],  VHeight[ 7:0],  MISC0[ 7: 0]}
	 *    MSA[7] = { HSP|HSW[14:8],   VSP|VSW[14:8],              0,  MISC1[ 7: 0]}
	 *    MSA[8] = {     HSW[ 7:0],       VSW[ 7:0],              0,             0}
	 */
	REG_SET_4(DP_SYM32_ENC_VID_MSA0, 0,
			MSA_DATA_LANE_0, 0,
			MSA_DATA_LANE_1, 0,
			MSA_DATA_LANE_2, 0,
			MSA_DATA_LANE_3, v_freq >> 40);

	REG_SET_4(DP_SYM32_ENC_VID_MSA1, 0,
			MSA_DATA_LANE_0, 0,
			MSA_DATA_LANE_1, 0,
			MSA_DATA_LANE_2, 0,
			MSA_DATA_LANE_3, (v_freq >> 32) & 0xff);

	REG_SET_4(DP_SYM32_ENC_VID_MSA2, 0,
			MSA_DATA_LANE_0, 0,
			MSA_DATA_LANE_1, 0,
			MSA_DATA_LANE_2, 0,
			MSA_DATA_LANE_3, (v_freq >> 24) & 0xff);

	REG_SET_4(DP_SYM32_ENC_VID_MSA3, 0,
			MSA_DATA_LANE_0, hw_crtc_timing.h_total >> 8,
			MSA_DATA_LANE_1, h_active_start >> 8,
			MSA_DATA_LANE_2, h_width >> 8,
			MSA_DATA_LANE_3, (v_freq >> 16) & 0xff);

	REG_SET_4(DP_SYM32_ENC_VID_MSA4, 0,
			MSA_DATA_LANE_0, hw_crtc_timing.h_total & 0xff,
			MSA_DATA_LANE_1, h_active_start & 0xff,
			MSA_DATA_LANE_2, h_width & 0xff,
			MSA_DATA_LANE_3, (v_freq >> 8) & 0xff);

	REG_SET_4(DP_SYM32_ENC_VID_MSA5, 0,
			MSA_DATA_LANE_0, hw_crtc_timing.v_total >> 8,
			MSA_DATA_LANE_1, v_active_start >> 8,
			MSA_DATA_LANE_2, v_height >> 8,
			MSA_DATA_LANE_3, v_freq & 0xff);

	REG_SET_4(DP_SYM32_ENC_VID_MSA6, 0,
			MSA_DATA_LANE_0, hw_crtc_timing.v_total & 0xff,
			MSA_DATA_LANE_1, v_active_start & 0xff,
			MSA_DATA_LANE_2, v_height & 0xff,
			MSA_DATA_LANE_3, misc0);

	REG_SET_4(DP_SYM32_ENC_VID_MSA7, 0,
			MSA_DATA_LANE_0, hsp | (hw_crtc_timing.h_sync_width >> 8),
			MSA_DATA_LANE_1, vsp | (hw_crtc_timing.v_sync_width >> 8),
			MSA_DATA_LANE_2, 0,
			MSA_DATA_LANE_3, misc1);

	REG_SET_4(DP_SYM32_ENC_VID_MSA8, 0,
			MSA_DATA_LANE_0, hw_crtc_timing.h_sync_width & 0xff,
			MSA_DATA_LANE_1, hw_crtc_timing.v_sync_width & 0xff,
			MSA_DATA_LANE_2, 0,
			MSA_DATA_LANE_3, 0);
}

static void dcn31_hpo_dp_stream_enc_update_dp_info_packets_sdp_line_num(
		struct hpo_dp_stream_encoder *enc,
		struct encoder_info_frame *info_frame)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	if (info_frame->adaptive_sync.valid == true &&
		info_frame->sdp_line_num.adaptive_sync_line_num_valid == true) {
		//00: REFER_TO_DP_SOF, 01: REFER_TO_OTG_SOF
		REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL5, GSP_SOF_REFERENCE, 1);

		REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL5, GSP_TRANSMISSION_LINE_NUMBER,
					info_frame->sdp_line_num.adaptive_sync_line_num);
	}
}

static void dcn31_hpo_dp_stream_enc_update_dp_info_packets(
		struct hpo_dp_stream_encoder *enc,
		const struct encoder_info_frame *info_frame)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);
	uint32_t dmdata_packet_enabled = 0;

	if (info_frame->vsc.valid)
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				0,  /* packetIndex */
				&info_frame->vsc,
				true);

	if (info_frame->spd.valid)
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				2,  /* packetIndex */
				&info_frame->spd,
				true);

	if (info_frame->hdrsmd.valid)
		enc->vpg->funcs->update_generic_info_packet(
				enc->vpg,
				3,  /* packetIndex */
				&info_frame->hdrsmd,
				true);

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
	REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL0, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, info_frame->vsc.valid);
	REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL2, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, info_frame->spd.valid);
	REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL3, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, info_frame->hdrsmd.valid);
	REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL5, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, info_frame->adaptive_sync.valid);

	/* check if dynamic metadata packet transmission is enabled */
	REG_GET(DP_SYM32_ENC_SDP_METADATA_PACKET_CONTROL,
			METADATA_PACKET_ENABLE, &dmdata_packet_enabled);

	/* Enable secondary data path */
	REG_UPDATE(DP_SYM32_ENC_SDP_CONTROL,
			SDP_STREAM_ENABLE, 1);
}

static void dcn31_hpo_dp_stream_enc_stop_dp_info_packets(
	struct hpo_dp_stream_encoder *enc)
{
	/* stop generic packets on DP */
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);
	uint32_t asp_enable = 0;
	uint32_t atp_enable = 0;
	uint32_t aip_enable = 0;
	uint32_t acm_enable = 0;

	REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL0, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, 0);
	REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL2, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, 0);
	REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL3, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, 0);

	/* Disable secondary data path if audio is also disabled */
	REG_GET_4(DP_SYM32_ENC_SDP_AUDIO_CONTROL0,
			ASP_ENABLE, &asp_enable,
			ATP_ENABLE, &atp_enable,
			AIP_ENABLE, &aip_enable,
			ACM_ENABLE, &acm_enable);
	if (!(asp_enable || atp_enable || aip_enable || acm_enable))
		REG_UPDATE(DP_SYM32_ENC_SDP_CONTROL,
				SDP_STREAM_ENABLE, 0);
}

static uint32_t hpo_dp_is_gsp_enabled(
		struct hpo_dp_stream_encoder *enc)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);
	uint32_t gsp0_enabled = 0;
	uint32_t gsp2_enabled = 0;
	uint32_t gsp3_enabled = 0;
	uint32_t gsp11_enabled = 0;

	REG_GET(DP_SYM32_ENC_SDP_GSP_CONTROL0, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, &gsp0_enabled);
	REG_GET(DP_SYM32_ENC_SDP_GSP_CONTROL2, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, &gsp2_enabled);
	REG_GET(DP_SYM32_ENC_SDP_GSP_CONTROL3, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, &gsp3_enabled);
	REG_GET(DP_SYM32_ENC_SDP_GSP_CONTROL11, GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, &gsp11_enabled);

	return (gsp0_enabled || gsp2_enabled || gsp3_enabled || gsp11_enabled);
}

static void dcn31_hpo_dp_stream_enc_set_dsc_pps_info_packet(
		struct hpo_dp_stream_encoder *enc,
		bool enable,
		uint8_t *dsc_packed_pps,
		bool immediate_update)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	if (enable) {
		struct dc_info_packet pps_sdp;
		int i;

		/* Configure for PPS packet size (128 bytes) */
		REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL11,
				GSP_PAYLOAD_SIZE, 3);

		/* Load PPS into infoframe (SDP) registers */
		pps_sdp.valid = true;
		pps_sdp.hb0 = 0;
		pps_sdp.hb1 = DC_DP_INFOFRAME_TYPE_PPS;
		pps_sdp.hb2 = 127;
		pps_sdp.hb3 = 0;

		for (i = 0; i < 4; i++) {
			memcpy(pps_sdp.sb, &dsc_packed_pps[i * 32], 32);
			enc3->base.vpg->funcs->update_generic_info_packet(
							enc3->base.vpg,
							11 + i,
							&pps_sdp,
							immediate_update);
		}

		/* SW should make sure VBID[6] update line number is bigger
		 * than PPS transmit line number
		 */
		REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL11,
				GSP_TRANSMISSION_LINE_NUMBER, 2);

		REG_UPDATE_2(DP_SYM32_ENC_VID_VBID_CONTROL,
				VBID_6_COMPRESSEDSTREAM_FLAG_SOF_REFERENCE, 0,
				VBID_6_COMPRESSEDSTREAM_FLAG_LINE_NUMBER, 3);

		/* Send PPS data at the line number specified above. */
		REG_UPDATE(DP_SYM32_ENC_SDP_GSP_CONTROL11,
				GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, 1);
		REG_UPDATE(DP_SYM32_ENC_SDP_CONTROL,
				SDP_STREAM_ENABLE, 1);
	} else {
		/* Disable Generic Stream Packet 11 (GSP) transmission */
		REG_UPDATE_2(DP_SYM32_ENC_SDP_GSP_CONTROL11,
				GSP_VIDEO_CONTINUOUS_TRANSMISSION_ENABLE, 0,
				GSP_PAYLOAD_SIZE, 0);
	}
}

static void dcn31_hpo_dp_stream_enc_map_stream_to_link(
		struct hpo_dp_stream_encoder *enc,
		uint32_t stream_enc_inst,
		uint32_t link_enc_inst)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	ASSERT(stream_enc_inst < 4 && link_enc_inst < 2);

	switch (stream_enc_inst) {
	case 0:
		REG_UPDATE(DP_STREAM_MAPPER_CONTROL0,
				DP_STREAM_LINK_TARGET, link_enc_inst);
		break;
	case 1:
		REG_UPDATE(DP_STREAM_MAPPER_CONTROL1,
				DP_STREAM_LINK_TARGET, link_enc_inst);
		break;
	case 2:
		REG_UPDATE(DP_STREAM_MAPPER_CONTROL2,
				DP_STREAM_LINK_TARGET, link_enc_inst);
		break;
	case 3:
		REG_UPDATE(DP_STREAM_MAPPER_CONTROL3,
				DP_STREAM_LINK_TARGET, link_enc_inst);
		break;
	}
}

static void dcn31_hpo_dp_stream_enc_audio_setup(
	struct hpo_dp_stream_encoder *enc,
	unsigned int az_inst,
	struct audio_info *info)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	/* Set the input mux for video stream source */
	REG_UPDATE(DP_STREAM_ENC_AUDIO_CONTROL,
			DP_STREAM_ENC_INPUT_MUX_AUDIO_STREAM_SOURCE_SEL, az_inst);

	ASSERT(enc->apg);
	enc->apg->funcs->se_audio_setup(enc->apg, az_inst, info);
}

static void dcn31_hpo_dp_stream_enc_audio_enable(
	struct hpo_dp_stream_encoder *enc)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	/* Enable Audio packets */
	REG_UPDATE(DP_SYM32_ENC_SDP_AUDIO_CONTROL0, ASP_ENABLE, 1);

	/* Program the ATP and AIP next */
	REG_UPDATE_2(DP_SYM32_ENC_SDP_AUDIO_CONTROL0,
			ATP_ENABLE, 1,
			AIP_ENABLE, 1);

	/* Enable secondary data path */
	REG_UPDATE(DP_SYM32_ENC_SDP_CONTROL,
			SDP_STREAM_ENABLE, 1);

	/* Enable APG block */
	enc->apg->funcs->enable_apg(enc->apg);
}

static void dcn31_hpo_dp_stream_enc_audio_disable(
	struct hpo_dp_stream_encoder *enc)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	/* Disable Audio packets */
	REG_UPDATE_4(DP_SYM32_ENC_SDP_AUDIO_CONTROL0,
			ASP_ENABLE, 0,
			ATP_ENABLE, 0,
			AIP_ENABLE, 0,
			ACM_ENABLE, 0);

	/* Disable STP Stream Enable if other SDP GSP are also disabled */
	if (!(hpo_dp_is_gsp_enabled(enc)))
		REG_UPDATE(DP_SYM32_ENC_SDP_CONTROL,
				SDP_STREAM_ENABLE, 0);

	/* Disable APG block */
	enc->apg->funcs->disable_apg(enc->apg);
}

static void dcn31_hpo_dp_stream_enc_read_state(
		struct hpo_dp_stream_encoder *enc,
		struct hpo_dp_stream_encoder_state *s)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	REG_GET(DP_SYM32_ENC_CONTROL,
			DP_SYM32_ENC_ENABLE, &s->stream_enc_enabled);
	REG_GET(DP_SYM32_ENC_VID_STREAM_CONTROL,
			VID_STREAM_ENABLE, &s->vid_stream_enabled);
	REG_GET(DP_STREAM_ENC_INPUT_MUX_CONTROL,
			DP_STREAM_ENC_INPUT_MUX_PIXEL_STREAM_SOURCE_SEL, &s->otg_inst);

	REG_GET_3(DP_SYM32_ENC_VID_PIXEL_FORMAT,
			PIXEL_ENCODING_TYPE, &s->compressed_format,
			UNCOMPRESSED_PIXEL_ENCODING, &s->pixel_encoding,
			UNCOMPRESSED_COMPONENT_DEPTH, &s->component_depth);

	REG_GET(DP_SYM32_ENC_SDP_CONTROL,
			SDP_STREAM_ENABLE, &s->sdp_enabled);

	switch (enc->inst) {
	case 0:
		REG_GET(DP_STREAM_MAPPER_CONTROL0,
				DP_STREAM_LINK_TARGET, &s->mapped_to_link_enc);
		break;
	case 1:
		REG_GET(DP_STREAM_MAPPER_CONTROL1,
				DP_STREAM_LINK_TARGET, &s->mapped_to_link_enc);
		break;
	case 2:
		REG_GET(DP_STREAM_MAPPER_CONTROL2,
				DP_STREAM_LINK_TARGET, &s->mapped_to_link_enc);
		break;
	case 3:
		REG_GET(DP_STREAM_MAPPER_CONTROL3,
				DP_STREAM_LINK_TARGET, &s->mapped_to_link_enc);
		break;
	}
}

static void dcn31_set_hblank_min_symbol_width(
		struct hpo_dp_stream_encoder *enc,
		uint16_t width)
{
	struct dcn31_hpo_dp_stream_encoder *enc3 = DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(enc);

	REG_SET(DP_SYM32_ENC_HBLANK_CONTROL, 0,
			HBLANK_MINIMUM_SYMBOL_WIDTH, width);
}

static const struct hpo_dp_stream_encoder_funcs dcn30_str_enc_funcs = {
	.enable_stream = dcn31_hpo_dp_stream_enc_enable_stream,
	.dp_unblank = dcn31_hpo_dp_stream_enc_dp_unblank,
	.dp_blank = dcn31_hpo_dp_stream_enc_dp_blank,
	.disable = dcn31_hpo_dp_stream_enc_disable,
	.set_stream_attribute = dcn31_hpo_dp_stream_enc_set_stream_attribute,
	.update_dp_info_packets_sdp_line_num = dcn31_hpo_dp_stream_enc_update_dp_info_packets_sdp_line_num,
	.update_dp_info_packets = dcn31_hpo_dp_stream_enc_update_dp_info_packets,
	.stop_dp_info_packets = dcn31_hpo_dp_stream_enc_stop_dp_info_packets,
	.dp_set_dsc_pps_info_packet = dcn31_hpo_dp_stream_enc_set_dsc_pps_info_packet,
	.map_stream_to_link = dcn31_hpo_dp_stream_enc_map_stream_to_link,
	.dp_audio_setup = dcn31_hpo_dp_stream_enc_audio_setup,
	.dp_audio_enable = dcn31_hpo_dp_stream_enc_audio_enable,
	.dp_audio_disable = dcn31_hpo_dp_stream_enc_audio_disable,
	.read_state = dcn31_hpo_dp_stream_enc_read_state,
	.set_hblank_min_symbol_width = dcn31_set_hblank_min_symbol_width,
};

void dcn31_hpo_dp_stream_encoder_construct(
	struct dcn31_hpo_dp_stream_encoder *enc3,
	struct dc_context *ctx,
	struct dc_bios *bp,
	uint32_t inst,
	enum engine_id eng_id,
	struct vpg *vpg,
	struct apg *apg,
	const struct dcn31_hpo_dp_stream_encoder_registers *regs,
	const struct dcn31_hpo_dp_stream_encoder_shift *hpo_se_shift,
	const struct dcn31_hpo_dp_stream_encoder_mask *hpo_se_mask)
{
	enc3->base.funcs = &dcn30_str_enc_funcs;
	enc3->base.ctx = ctx;
	enc3->base.inst = inst;
	enc3->base.id = eng_id;
	enc3->base.bp = bp;
	enc3->base.vpg = vpg;
	enc3->base.apg = apg;
	enc3->regs = regs;
	enc3->hpo_se_shift = hpo_se_shift;
	enc3->hpo_se_mask = hpo_se_mask;
}
