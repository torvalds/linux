/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <engine/fifo/priv.h>

#include <nvif/class.h>

const struct nvkm_rm_gpu
gb10x_gpu = {
	.usermode.class = HOPPER_USERMODE_A,

	.fifo.chan = {
		.class = BLACKWELL_CHANNEL_GPFIFO_A,
		.doorbell_handle = tu102_chan_doorbell_handle,
	},

	.ce.class = BLACKWELL_DMA_COPY_A,
	.gr.class = {
		.i2m = BLACKWELL_INLINE_TO_MEMORY_A,
		.twod = FERMI_TWOD_A,
		.threed = BLACKWELL_A,
		.compute = BLACKWELL_COMPUTE_A,
	},
	.nvdec.class = NVCDB0_VIDEO_DECODER,
	.nvjpg.class = NVCDD1_VIDEO_NVJPG,
	.ofa.class = NVCDFA_VIDEO_OFA,
};
