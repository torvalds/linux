/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __HFI_PLATFORM_H__
#define __HFI_PLATFORM_H__

#include <linux/types.h>
#include <linux/videodev2.h>

#include "hfi.h"
#include "hfi_plat_bufs.h"
#include "hfi_helper.h"

#define MAX_PLANES		4
#define MAX_FMT_ENTRIES		32
#define MAX_CAP_ENTRIES		32
#define MAX_ALLOC_MODE_ENTRIES	16
#define MAX_CODEC_NUM		32
#define MAX_SESSIONS		16

struct raw_formats {
	u32 buftype;
	u32 fmt;
};

struct hfi_plat_caps {
	u32 codec;
	u32 domain;
	bool cap_bufs_mode_dynamic;
	unsigned int num_caps;
	struct hfi_capability caps[MAX_CAP_ENTRIES];
	unsigned int num_pl;
	struct hfi_profile_level pl[HFI_MAX_PROFILE_COUNT];
	unsigned int num_fmts;
	struct raw_formats fmts[MAX_FMT_ENTRIES];
	bool valid;	/* used only for Venus v1xx */
};

struct hfi_platform_codec_freq_data {
	u32 pixfmt;
	u32 session_type;
	unsigned long vpp_freq;
	unsigned long vsp_freq;
	unsigned long low_power_freq;
};

struct hfi_platform {
	unsigned long (*codec_vpp_freq)(u32 session_type, u32 codec);
	unsigned long (*codec_vsp_freq)(u32 session_type, u32 codec);
	unsigned long (*codec_lp_freq)(u32 session_type, u32 codec);
	void (*codecs)(u32 *enc_codecs, u32 *dec_codecs, u32 *count);
	const struct hfi_plat_caps *(*capabilities)(unsigned int *entries);
	int (*bufreq)(struct hfi_plat_buffers_params *params, u32 session_type,
		      u32 buftype, struct hfi_buffer_requirements *bufreq);
};

extern const struct hfi_platform hfi_plat_v4;
extern const struct hfi_platform hfi_plat_v6;

const struct hfi_platform *hfi_platform_get(enum hfi_version version);
unsigned long hfi_platform_get_codec_vpp_freq(enum hfi_version version, u32 codec,
					      u32 session_type);
unsigned long hfi_platform_get_codec_vsp_freq(enum hfi_version version, u32 codec,
					      u32 session_type);
unsigned long hfi_platform_get_codec_lp_freq(enum hfi_version version, u32 codec,
					     u32 session_type);
#endif
