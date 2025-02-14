/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <nvif/class.h>

const struct nvkm_rm_gpu
ga100_gpu = {
	.usermode.class = AMPERE_USERMODE_A,

	.fifo.chan = {
		.class = AMPERE_CHANNEL_GPFIFO_A,
	},

	.ce.class = AMPERE_DMA_COPY_A,
	.gr.class = {
		.i2m = KEPLER_INLINE_TO_MEMORY_B,
		.twod = FERMI_TWOD_A,
		.threed = AMPERE_A,
		.compute = AMPERE_COMPUTE_A,
	},
	.nvdec.class = NVC6B0_VIDEO_DECODER,
};
