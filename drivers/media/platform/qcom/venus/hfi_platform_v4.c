// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */
#include "hfi_platform.h"

static const struct hfi_platform_codec_freq_data codec_freq_data[] =  {
	{ V4L2_PIX_FMT_H264, VIDC_SESSION_TYPE_ENC, 675, 10 },
	{ V4L2_PIX_FMT_HEVC, VIDC_SESSION_TYPE_ENC, 675, 10 },
	{ V4L2_PIX_FMT_VP8, VIDC_SESSION_TYPE_ENC, 675, 10 },
	{ V4L2_PIX_FMT_MPEG2, VIDC_SESSION_TYPE_DEC, 200, 10 },
	{ V4L2_PIX_FMT_H264, VIDC_SESSION_TYPE_DEC, 200, 10 },
	{ V4L2_PIX_FMT_HEVC, VIDC_SESSION_TYPE_DEC, 200, 10 },
	{ V4L2_PIX_FMT_VP8, VIDC_SESSION_TYPE_DEC, 200, 10 },
	{ V4L2_PIX_FMT_VP9, VIDC_SESSION_TYPE_DEC, 200, 10 },
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

const struct hfi_platform hfi_plat_v4 = {
	.codec_vpp_freq = codec_vpp_freq,
	.codec_vsp_freq = codec_vsp_freq,
};
