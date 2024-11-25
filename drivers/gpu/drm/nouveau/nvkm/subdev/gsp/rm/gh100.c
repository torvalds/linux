/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <nvif/class.h>

const struct nvkm_rm_gpu
gh100_gpu = {
	.usermode.class = HOPPER_USERMODE_A,

	.fifo.chan = {
		.class = HOPPER_CHANNEL_GPFIFO_A,
	},

	.ce.class = HOPPER_DMA_COPY_A,
	.gr.class = {
		.i2m = KEPLER_INLINE_TO_MEMORY_B,
		.twod = FERMI_TWOD_A,
		.threed = HOPPER_A,
		.compute = HOPPER_COMPUTE_A,
	},
	.nvdec.class = NVB8B0_VIDEO_DECODER,
	.nvjpg.class = NVB8D1_VIDEO_NVJPG,
	.ofa.class = NVB8FA_VIDEO_OFA,
};
