/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <nvif/class.h>

const struct nvkm_rm_gpu
ad10x_gpu = {
	.disp.class = {
		.root = AD102_DISP,
		.caps = GV100_DISP_CAPS,
		.core = AD102_DISP_CORE_CHANNEL_DMA,
		.wndw = GA102_DISP_WINDOW_CHANNEL_DMA,
		.wimm = GA102_DISP_WINDOW_IMM_CHANNEL_DMA,
		.curs = GA102_DISP_CURSOR,
	},

	.usermode.class = AMPERE_USERMODE_A,

	.fifo.chan = {
		.class = AMPERE_CHANNEL_GPFIFO_A,
	},
};
