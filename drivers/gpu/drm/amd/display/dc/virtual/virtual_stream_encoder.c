/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
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

#include <linux/slab.h>

#include "dm_services.h"
#include "virtual_stream_encoder.h"

static void virtual_stream_encoder_dp_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	enum dc_color_space output_color_space,
	uint32_t enable_sdp_splitting) {}

static void virtual_stream_encoder_hdmi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	int actual_pix_clk_khz,
	bool enable_audio) {}

static void virtual_stream_encoder_dvi_set_stream_attribute(
	struct stream_encoder *enc,
	struct dc_crtc_timing *crtc_timing,
	bool is_dual_link) {}

static void virtual_stream_encoder_set_mst_bandwidth(
	struct stream_encoder *enc,
	struct fixed31_32 avg_time_slots_per_mtp) {}

static void virtual_stream_encoder_update_hdmi_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame) {}

static void virtual_stream_encoder_stop_hdmi_info_packets(
	struct stream_encoder *enc) {}

static void virtual_stream_encoder_set_avmute(
	struct stream_encoder *enc,
	bool enable) {}
static void virtual_stream_encoder_update_dp_info_packets(
	struct stream_encoder *enc,
	const struct encoder_info_frame *info_frame) {}

static void virtual_stream_encoder_stop_dp_info_packets(
	struct stream_encoder *enc) {}

static void virtual_stream_encoder_dp_blank(
	struct stream_encoder *enc) {}

static void virtual_stream_encoder_dp_unblank(
	struct stream_encoder *enc,
	const struct encoder_unblank_param *param) {}

static void virtual_audio_mute_control(
	struct stream_encoder *enc,
	bool mute) {}

static void virtual_stream_encoder_reset_hdmi_stream_attribute(
		struct stream_encoder *enc)
{}

#ifdef CONFIG_DRM_AMD_DC_DCN2_0
#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
static void virtual_enc_dp_set_odm_combine(
	struct stream_encoder *enc,
	bool odm_combine)
{}
#endif
#endif

static const struct stream_encoder_funcs virtual_str_enc_funcs = {
#ifdef CONFIG_DRM_AMD_DC_DCN2_0
#ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
	.dp_set_odm_combine =
		virtual_enc_dp_set_odm_combine,
#endif
#endif
	.dp_set_stream_attribute =
		virtual_stream_encoder_dp_set_stream_attribute,
	.hdmi_set_stream_attribute =
		virtual_stream_encoder_hdmi_set_stream_attribute,
	.dvi_set_stream_attribute =
		virtual_stream_encoder_dvi_set_stream_attribute,
	.set_mst_bandwidth =
		virtual_stream_encoder_set_mst_bandwidth,
	.update_hdmi_info_packets =
		virtual_stream_encoder_update_hdmi_info_packets,
	.stop_hdmi_info_packets =
		virtual_stream_encoder_stop_hdmi_info_packets,
	.update_dp_info_packets =
		virtual_stream_encoder_update_dp_info_packets,
	.stop_dp_info_packets =
		virtual_stream_encoder_stop_dp_info_packets,
	.dp_blank =
		virtual_stream_encoder_dp_blank,
	.dp_unblank =
		virtual_stream_encoder_dp_unblank,

	.audio_mute_control = virtual_audio_mute_control,
	.set_avmute = virtual_stream_encoder_set_avmute,
	.hdmi_reset_stream_attribute = virtual_stream_encoder_reset_hdmi_stream_attribute,
};

bool virtual_stream_encoder_construct(
	struct stream_encoder *enc,
	struct dc_context *ctx,
	struct dc_bios *bp)
{
	if (!enc)
		return false;
	if (!bp)
		return false;

	enc->funcs = &virtual_str_enc_funcs;
	enc->ctx = ctx;
	enc->id = ENGINE_ID_VIRTUAL;
	enc->bp = bp;

	return true;
}

struct stream_encoder *virtual_stream_encoder_create(
	struct dc_context *ctx, struct dc_bios *bp)
{
	struct stream_encoder *enc = kzalloc(sizeof(*enc), GFP_KERNEL);

	if (!enc)
		return NULL;

	if (virtual_stream_encoder_construct(enc, ctx, bp))
		return enc;

	BREAK_TO_DEBUGGER();
	kfree(enc);
	return NULL;
}

