/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_PLATFORM_QCS8300_H__
#define __IRIS_PLATFORM_QCS8300_H__

static struct platform_inst_caps platform_inst_cap_qcs8300 = {
	.min_frame_width = 96,
	.max_frame_width = 4096,
	.min_frame_height = 96,
	.max_frame_height = 4096,
	.max_mbpf = (4096 * 2176) / 256,
	.mb_cycles_vpp = 200,
	.mb_cycles_fw = 326389,
	.mb_cycles_fw_vpp = 44156,
	.num_comv = 0,
	.max_frame_rate = MAXIMUM_FPS,
	.max_operating_rate = MAXIMUM_FPS,
};

#endif
