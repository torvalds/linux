/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __HFI_PLATFORM_H__
#define __HFI_PLATFORM_H__

#include <linux/types.h>
#include <linux/videodev2.h>

#include "hfi.h"
#include "hfi_helper.h"

struct hfi_platform_codec_freq_data {
	u32 pixfmt;
	u32 session_type;
	unsigned long vpp_freq;
	unsigned long vsp_freq;
};

struct hfi_platform {
	unsigned long (*codec_vpp_freq)(u32 session_type, u32 codec);
	unsigned long (*codec_vsp_freq)(u32 session_type, u32 codec);
};

extern const struct hfi_platform hfi_plat_v4;

const struct hfi_platform *hfi_platform_get(enum hfi_version version);
unsigned long hfi_platform_get_codec_vpp_freq(enum hfi_version version, u32 codec,
					      u32 session_type);
unsigned long hfi_platform_get_codec_vsp_freq(enum hfi_version version, u32 codec,
					      u32 session_type);
#endif
