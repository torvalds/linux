/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DC_STREAM_ENCODER_DCN20_H__
#define __DC_STREAM_ENCODER_DCN20_H__

#include "stream_encoder.h"
#include "dcn10/dcn10_stream_encoder.h"


#define SE_DCN2_REG_LIST(id)\
	SE_COMMON_DCN_REG_LIST(id),\
	SRI(HDMI_GENERIC_PACKET_CONTROL4, DIG, id), \
	SRI(HDMI_GENERIC_PACKET_CONTROL5, DIG, id), \
	SRI(DP_DSC_CNTL, DP, id), \
	SRI(DP_DSC_BYTES_PER_PIXEL, DP, id), \
	SRI(DME_CONTROL, DIG, id),\
	SRI(DP_SEC_METADATA_TRANSMISSION, DP, id), \
	SRI(HDMI_METADATA_PACKET_CONTROL, DIG, id), \
	SRI(DP_SEC_FRAMING4, DP, id)

#define SE_COMMON_MASK_SH_LIST_DCN20(mask_sh)\
	SE_COMMON_MASK_SH_LIST_SOC(mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC0_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC0_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC1_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC1_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC2_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC2_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC3_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC3_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC4_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC4_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC5_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC5_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC6_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC6_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC7_CONT, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL0, HDMI_GENERIC7_SEND, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC0_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL1, HDMI_GENERIC1_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL2, HDMI_GENERIC2_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL2, HDMI_GENERIC3_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL3, HDMI_GENERIC4_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL3, HDMI_GENERIC5_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL4, HDMI_GENERIC6_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_GENERIC_PACKET_CONTROL4, HDMI_GENERIC7_LINE, mask_sh),\
	SE_SF(DP0_DP_DSC_CNTL, DP_DSC_MODE, mask_sh),\
	SE_SF(DP0_DP_DSC_CNTL, DP_DSC_SLICE_WIDTH, mask_sh),\
	SE_SF(DP0_DP_DSC_BYTES_PER_PIXEL, DP_DSC_BYTES_PER_PIXEL, mask_sh),\
	SE_SF(DP0_DP_MSA_VBID_MISC, DP_VBID6_LINE_REFERENCE, mask_sh),\
	SE_SF(DP0_DP_MSA_VBID_MISC, DP_VBID6_LINE_NUM, mask_sh),\
	SE_SF(DIG0_DME_CONTROL, METADATA_ENGINE_EN, mask_sh),\
	SE_SF(DIG0_DME_CONTROL, METADATA_HUBP_REQUESTOR_ID, mask_sh),\
	SE_SF(DIG0_DME_CONTROL, METADATA_STREAM_TYPE, mask_sh),\
	SE_SF(DP0_DP_SEC_METADATA_TRANSMISSION, DP_SEC_METADATA_PACKET_ENABLE, mask_sh),\
	SE_SF(DP0_DP_SEC_METADATA_TRANSMISSION, DP_SEC_METADATA_PACKET_LINE_REFERENCE, mask_sh),\
	SE_SF(DP0_DP_SEC_METADATA_TRANSMISSION, DP_SEC_METADATA_PACKET_LINE, mask_sh),\
	SE_SF(DIG0_HDMI_METADATA_PACKET_CONTROL, HDMI_METADATA_PACKET_ENABLE, mask_sh),\
	SE_SF(DIG0_HDMI_METADATA_PACKET_CONTROL, HDMI_METADATA_PACKET_LINE_REFERENCE, mask_sh),\
	SE_SF(DIG0_HDMI_METADATA_PACKET_CONTROL, HDMI_METADATA_PACKET_LINE, mask_sh),\
	SE_SF(DIG0_DIG_FE_CNTL, DOLBY_VISION_EN, mask_sh),\
	SE_SF(DP0_DP_PIXEL_FORMAT, DP_PIXEL_COMBINE, mask_sh),\
	SE_SF(DP0_DP_SEC_CNTL1, DP_SEC_GSP5_LINE_REFERENCE, mask_sh),\
	SE_SF(DP0_DP_SEC_CNTL5, DP_SEC_GSP5_LINE_NUM, mask_sh),\
	SE_SF(DP0_DP_SEC_FRAMING4, DP_SST_SDP_SPLITTING, mask_sh)

void dcn20_stream_encoder_construct(
	struct dcn10_stream_encoder *enc1,
	struct dc_context *ctx,
	struct dc_bios *bp,
	enum engine_id eng_id,
	const struct dcn10_stream_enc_registers *regs,
	const struct dcn10_stream_encoder_shift *se_shift,
	const struct dcn10_stream_encoder_mask *se_mask);

void enc2_stream_encoder_dp_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	enum dc_color_space output_color_space,
	bool use_vsc_sdp_for_colorimetry,
	uint32_t enable_sdp_splitting);

void enc2_stream_encoder_dp_unblank(
	struct stream_encoder *enc,
	const struct encoder_unblank_param *param);

void enc2_set_dynamic_metadata(struct stream_encoder *enc,
		bool enable_dme,
		uint32_t hubp_requestor_id,
		enum dynamic_metadata_mode dmdata_mode);

#endif /* __DC_STREAM_ENCODER_DCN20_H__ */
