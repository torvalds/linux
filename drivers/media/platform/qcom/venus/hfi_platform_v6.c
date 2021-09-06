// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */
#include "hfi_platform.h"

static const struct hfi_plat_caps caps[] = {
{
	.codec = HFI_VIDEO_CODEC_H264,
	.domain = VIDC_SESSION_TYPE_DEC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 8192, 1},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 8192, 1},
	/* ((5760 * 2880) / 256) */
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 138240, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 220000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 65536, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 65536, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 7833600, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 960, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.num_caps = 9,
	.pl[0] = {HFI_H264_PROFILE_BASELINE, HFI_H264_LEVEL_52},
	.pl[1] = {HFI_H264_PROFILE_MAIN, HFI_H264_LEVEL_52},
	.pl[2] = {HFI_H264_PROFILE_HIGH, HFI_H264_LEVEL_52},
	.pl[3] = {HFI_H264_PROFILE_CONSTRAINED_BASE, HFI_H264_LEVEL_52},
	.pl[4] = {HFI_H264_PROFILE_CONSTRAINED_HIGH, HFI_H264_LEVEL_52},
	.num_pl = 5,
	.fmts[0] = {HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[1] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[2] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12},
	.fmts[3] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV21},
	.num_fmts = 4,
}, {
	.codec = HFI_VIDEO_CODEC_HEVC,
	.domain = VIDC_SESSION_TYPE_DEC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 8192, 1},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 8192, 1},
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 138240, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 220000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 65536, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 65536, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 7833600, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 960, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.caps[9] = {HFI_CAPABILITY_MAX_WORKMODES, 1, 3, 1},
	.num_caps = 10,
	.pl[0] = {HFI_HEVC_PROFILE_MAIN, HFI_HEVC_LEVEL_6 | HFI_HEVC_TIER_HIGH0},
	.pl[1] = {HFI_HEVC_PROFILE_MAIN10, HFI_HEVC_LEVEL_6 | HFI_HEVC_TIER_HIGH0},
	.num_pl = 2,
	.fmts[0] = {HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[1] = {HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_YUV420_TP10_UBWC},
	.fmts[2] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[3] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12},
	.fmts[4] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV21},
	.fmts[5] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_P010},
	.fmts[6] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_YUV420_TP10},
	.num_fmts = 7,
}, {
	.codec = HFI_VIDEO_CODEC_VP8,
	.domain = VIDC_SESSION_TYPE_DEC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 4096, 1},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 4096, 1},
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 36864, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 100000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 65536, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 65536, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 4423680, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 120, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.caps[9] = {HFI_CAPABILITY_MAX_WORKMODES, 1, 3, 1},
	.num_caps = 10,
	.pl[0] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_0},
	.pl[1] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_1},
	.pl[2] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_2},
	.pl[3] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_3},
	.num_pl = 4,
	.fmts[0] = {HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[1] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[2] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12},
	.fmts[3] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV21},
	.num_fmts = 4,
}, {
	.codec = HFI_VIDEO_CODEC_VP9,
	.domain = VIDC_SESSION_TYPE_DEC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 8192, 1},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 8192, 1},
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 138240, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 220000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 65536, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 65536, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 7833600, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 960, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.caps[9] = {HFI_CAPABILITY_MAX_WORKMODES, 1, 3, 1},
	.num_caps = 10,
	.pl[0] = {HFI_VP9_PROFILE_P0, 200},
	.pl[1] = {HFI_VP9_PROFILE_P2_10B, 200},
	.num_pl = 2,
	.fmts[0] = {HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[1] = {HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_YUV420_TP10_UBWC},
	.fmts[2] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[3] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12},
	.fmts[4] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV21},
	.fmts[5] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_P010},
	.fmts[6] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_YUV420_TP10},
	.num_fmts = 7,
}, {
	.codec = HFI_VIDEO_CODEC_MPEG2,
	.domain = VIDC_SESSION_TYPE_DEC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 1920, 1},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 1920, 1},
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 8160, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 40000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 65536, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 65536, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 7833600, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 30, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.caps[9] = {HFI_CAPABILITY_MAX_WORKMODES, 1, 1, 1},
	.num_caps = 10,
	.pl[0] = {HFI_MPEG2_PROFILE_SIMPLE, HFI_MPEG2_LEVEL_H14},
	.pl[1] = {HFI_MPEG2_PROFILE_MAIN, HFI_MPEG2_LEVEL_H14},
	.num_pl = 2,
	.fmts[0] = {HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[1] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[2] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV12},
	.fmts[3] = {HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_NV21},
	.num_fmts = 4,
}, {
	.codec = HFI_VIDEO_CODEC_H264,
	.domain = VIDC_SESSION_TYPE_ENC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 8192, 1},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 8192, 1},
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 138240, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 220000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 8192, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 8192, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 7833600, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 960, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.caps[9] = {HFI_CAPABILITY_PEAKBITRATE, 32000, 160000000, 1},
	.caps[10] = {HFI_CAPABILITY_HIER_P_NUM_ENH_LAYERS, 0, 6, 1},
	.caps[11] = {HFI_CAPABILITY_ENC_LTR_COUNT, 0, 2, 1},
	.caps[12] = {HFI_CAPABILITY_LCU_SIZE, 16, 16, 1},
	.caps[13] = {HFI_CAPABILITY_BFRAME, 0, 1, 1},
	.caps[14] = {HFI_CAPABILITY_HIER_P_HYBRID_NUM_ENH_LAYERS, 0, 6, 1},
	.caps[15] = {HFI_CAPABILITY_I_FRAME_QP, 0, 51, 1},
	.caps[16] = {HFI_CAPABILITY_P_FRAME_QP, 0, 51, 1},
	.caps[17] = {HFI_CAPABILITY_B_FRAME_QP, 0, 51, 1},
	.caps[18] = {HFI_CAPABILITY_MAX_WORKMODES, 1, 2, 1},
	.caps[19] = {HFI_CAPABILITY_RATE_CONTROL_MODES, 0x1000001, 0x1000005, 1},
	.caps[20] = {HFI_CAPABILITY_COLOR_SPACE_CONVERSION, 0, 2, 1},
	.num_caps = 21,
	.pl[0] = {HFI_H264_PROFILE_BASELINE, HFI_H264_LEVEL_52},
	.pl[1] = {HFI_H264_PROFILE_MAIN, HFI_H264_LEVEL_52},
	.pl[2] = {HFI_H264_PROFILE_HIGH, HFI_H264_LEVEL_52},
	.pl[3] = {HFI_H264_PROFILE_CONSTRAINED_BASE, HFI_H264_LEVEL_52},
	.pl[4] = {HFI_H264_PROFILE_CONSTRAINED_HIGH, HFI_H264_LEVEL_52},
	.num_pl = 5,
	.fmts[0] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_NV12},
	.fmts[1] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[2] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_YUV420_TP10_UBWC},
	.fmts[3] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_P010},
	.num_fmts = 4,
}, {
	.codec = HFI_VIDEO_CODEC_HEVC,
	.domain = VIDC_SESSION_TYPE_ENC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 8192, 16},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 8192, 16},
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 138240, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 160000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 8192, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 8192, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 7833600, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 960, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.caps[9] = {HFI_CAPABILITY_PEAKBITRATE, 32000, 160000000, 1},
	.caps[10] = {HFI_CAPABILITY_HIER_P_NUM_ENH_LAYERS, 0, 5, 1},
	.caps[11] = {HFI_CAPABILITY_ENC_LTR_COUNT, 0, 2, 1},
	.caps[12] = {HFI_CAPABILITY_LCU_SIZE, 32, 32, 1},
	.caps[13] = {HFI_CAPABILITY_BFRAME, 0, 1, 1},
	.caps[14] = {HFI_CAPABILITY_HIER_P_HYBRID_NUM_ENH_LAYERS, 0, 5, 1},
	.caps[15] = {HFI_CAPABILITY_I_FRAME_QP, 0, 51, 1},
	.caps[16] = {HFI_CAPABILITY_P_FRAME_QP, 0, 51, 1},
	.caps[17] = {HFI_CAPABILITY_B_FRAME_QP, 0, 51, 1},
	.caps[18] = {HFI_CAPABILITY_MAX_WORKMODES, 1, 2, 1},
	.caps[19] = {HFI_CAPABILITY_RATE_CONTROL_MODES, 0x1000001, 0x1000005, 1},
	.caps[20] = {HFI_CAPABILITY_COLOR_SPACE_CONVERSION, 0, 2, 1},
	.caps[21] = {HFI_CAPABILITY_ROTATION, 1, 4, 90},
	.caps[22] = {HFI_CAPABILITY_BLUR_WIDTH, 96, 4096, 16},
	.caps[23] = {HFI_CAPABILITY_BLUR_HEIGHT, 96, 4096, 16},
	.num_caps = 24,
	.pl[0] = {HFI_HEVC_PROFILE_MAIN, HFI_HEVC_LEVEL_6 | HFI_HEVC_TIER_HIGH0},
	.pl[1] = {HFI_HEVC_PROFILE_MAIN10, HFI_HEVC_LEVEL_6 | HFI_HEVC_TIER_HIGH0},
	.num_pl = 2,
	.fmts[0] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_NV12},
	.fmts[1] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[2] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_YUV420_TP10_UBWC},
	.fmts[3] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_P010},
	.num_fmts = 4,
}, {
	.codec = HFI_VIDEO_CODEC_VP8,
	.domain = VIDC_SESSION_TYPE_ENC,
	.cap_bufs_mode_dynamic = true,
	.caps[0] = {HFI_CAPABILITY_FRAME_WIDTH, 128, 4096, 16},
	.caps[1] = {HFI_CAPABILITY_FRAME_HEIGHT, 128, 4096, 16},
	.caps[2] = {HFI_CAPABILITY_MBS_PER_FRAME, 64, 36864, 1},
	.caps[3] = {HFI_CAPABILITY_BITRATE, 1, 74000000, 1},
	.caps[4] = {HFI_CAPABILITY_SCALE_X, 8192, 65536, 1},
	.caps[5] = {HFI_CAPABILITY_SCALE_Y, 8192, 65536, 1},
	.caps[6] = {HFI_CAPABILITY_MBS_PER_SECOND, 64, 4423680, 1},
	.caps[7] = {HFI_CAPABILITY_FRAMERATE, 1, 120, 1},
	.caps[8] = {HFI_CAPABILITY_MAX_VIDEOCORES, 0, 1, 1},
	.caps[9] = {HFI_CAPABILITY_PEAKBITRATE, 32000, 160000000, 1},
	.caps[10] = {HFI_CAPABILITY_HIER_P_NUM_ENH_LAYERS, 0, 3, 1},
	.caps[11] = {HFI_CAPABILITY_ENC_LTR_COUNT, 0, 2, 1},
	.caps[12] = {HFI_CAPABILITY_LCU_SIZE, 16, 16, 1},
	.caps[13] = {HFI_CAPABILITY_BFRAME, 0, 0, 1},
	.caps[14] = {HFI_CAPABILITY_HIER_P_HYBRID_NUM_ENH_LAYERS, 0, 5, 1},
	.caps[15] = {HFI_CAPABILITY_I_FRAME_QP, 0, 127, 1},
	.caps[16] = {HFI_CAPABILITY_P_FRAME_QP, 0, 127, 1},
	.caps[17] = {HFI_CAPABILITY_MAX_WORKMODES, 1, 2, 1},
	.caps[18] = {HFI_CAPABILITY_RATE_CONTROL_MODES, 0x1000001, 0x1000005, 1},
	.caps[19] = {HFI_CAPABILITY_BLUR_WIDTH, 96, 4096, 16},
	.caps[20] = {HFI_CAPABILITY_BLUR_HEIGHT, 96, 4096, 16},
	.caps[21] = {HFI_CAPABILITY_COLOR_SPACE_CONVERSION, 0, 2, 1},
	.caps[22] = {HFI_CAPABILITY_ROTATION, 1, 4, 90},
	.num_caps = 23,
	.pl[0] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_0},
	.pl[1] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_1},
	.pl[2] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_2},
	.pl[3] = {HFI_VPX_PROFILE_MAIN, HFI_VPX_LEVEL_VERSION_3},
	.num_pl = 4,
	.fmts[0] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_NV12},
	.fmts[1] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_NV12_UBWC},
	.fmts[2] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_YUV420_TP10_UBWC},
	.fmts[3] = {HFI_BUFFER_INPUT, HFI_COLOR_FORMAT_P010},
	.num_fmts = 4,
} };

static const struct hfi_plat_caps *get_capabilities(unsigned int *entries)
{
	*entries = ARRAY_SIZE(caps);
	return caps;
}

static void get_codecs(u32 *enc_codecs, u32 *dec_codecs, u32 *count)
{
	*enc_codecs = HFI_VIDEO_CODEC_H264 | HFI_VIDEO_CODEC_HEVC |
		      HFI_VIDEO_CODEC_VP8;
	*dec_codecs = HFI_VIDEO_CODEC_H264 | HFI_VIDEO_CODEC_HEVC |
		      HFI_VIDEO_CODEC_VP8 | HFI_VIDEO_CODEC_VP9 |
		      HFI_VIDEO_CODEC_MPEG2;
	*count = 8;
}

static const struct hfi_platform_codec_freq_data codec_freq_data[] = {
	{ V4L2_PIX_FMT_H264, VIDC_SESSION_TYPE_ENC, 675, 25 },
	{ V4L2_PIX_FMT_HEVC, VIDC_SESSION_TYPE_ENC, 675, 25 },
	{ V4L2_PIX_FMT_VP8, VIDC_SESSION_TYPE_ENC, 675, 60 },
	{ V4L2_PIX_FMT_MPEG2, VIDC_SESSION_TYPE_DEC, 200, 25 },
	{ V4L2_PIX_FMT_H264, VIDC_SESSION_TYPE_DEC, 200, 25 },
	{ V4L2_PIX_FMT_HEVC, VIDC_SESSION_TYPE_DEC, 200, 25 },
	{ V4L2_PIX_FMT_VP8, VIDC_SESSION_TYPE_DEC, 200, 60 },
	{ V4L2_PIX_FMT_VP9, VIDC_SESSION_TYPE_DEC, 200, 60 },
};

static const struct hfi_platform_codec_freq_data *
get_codec_freq_data(u32 session_type, u32 pixfmt)
{
	const struct hfi_platform_codec_freq_data *data = codec_freq_data;
	unsigned int i, data_size = ARRAY_SIZE(codec_freq_data);
	const struct hfi_platform_codec_freq_data *found = NULL;

	for (i = 0; i < data_size; i++) {
		if (data[i].pixfmt == pixfmt && data[i].session_type == session_type) {
			found = &data[i];
			break;
		}
	}

	return found;
}

static unsigned long codec_vpp_freq(u32 session_type, u32 codec)
{
	const struct hfi_platform_codec_freq_data *data;

	data = get_codec_freq_data(session_type, codec);
	if (data)
		return data->vpp_freq;

	return 0;
}

static unsigned long codec_vsp_freq(u32 session_type, u32 codec)
{
	const struct hfi_platform_codec_freq_data *data;

	data = get_codec_freq_data(session_type, codec);
	if (data)
		return data->vsp_freq;

	return 0;
}

static u8 num_vpp_pipes(void)
{
	return 4;
}

const struct hfi_platform hfi_plat_v6 = {
	.codec_vpp_freq = codec_vpp_freq,
	.codec_vsp_freq = codec_vsp_freq,
	.codecs = get_codecs,
	.capabilities = get_capabilities,
	.num_vpp_pipes = num_vpp_pipes,
	.bufreq = hfi_plat_bufreq_v6,
};
