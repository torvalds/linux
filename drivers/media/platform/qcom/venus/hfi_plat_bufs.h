/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __HFI_PLATFORM_BUFFERS_H__
#define __HFI_PLATFORM_BUFFERS_H__

#include <linux/types.h>
#include "hfi_helper.h"

struct hfi_plat_buffers_params {
	u32 width;
	u32 height;
	u32 codec;
	u32 hfi_color_fmt;
	enum hfi_version version;
	u32 num_vpp_pipes;
	union {
		struct {
			u32 max_mbs_per_frame;
			u32 buffer_size_limit;
			bool is_secondary_output;
			bool is_interlaced;
		} dec;
		struct {
			u32 work_mode;
			u32 rc_type;
			u32 num_b_frames;
			bool is_tenbit;
		} enc;
	};
};

int hfi_plat_bufreq_v6(struct hfi_plat_buffers_params *params, u32 session_type,
		       u32 buftype, struct hfi_buffer_requirements *bufreq);

#endif
