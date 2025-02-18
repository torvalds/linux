/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <nvif/class.h>

const struct nvkm_rm_gpu
tu1xx_gpu = {
	.disp.class = {
		.root = TU102_DISP,
		.caps = GV100_DISP_CAPS,
		.core = TU102_DISP_CORE_CHANNEL_DMA,
		.wndw = TU102_DISP_WINDOW_CHANNEL_DMA,
		.wimm = TU102_DISP_WINDOW_IMM_CHANNEL_DMA,
		.curs = TU102_DISP_CURSOR,
	},

	.usermode.class = TURING_USERMODE_A,
};
