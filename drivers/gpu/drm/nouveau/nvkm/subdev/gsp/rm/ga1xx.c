/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <engine/fifo/priv.h>

#include <nvif/class.h>

const struct nvkm_rm_gpu
ga1xx_gpu = {
	.disp.class = {
		.root = GA102_DISP,
		.caps = GV100_DISP_CAPS,
		.core = GA102_DISP_CORE_CHANNEL_DMA,
		.wndw = GA102_DISP_WINDOW_CHANNEL_DMA,
		.wimm = GA102_DISP_WINDOW_IMM_CHANNEL_DMA,
		.curs = GA102_DISP_CURSOR,
	},

	.usermode.class = AMPERE_USERMODE_A,

	.fifo.chan = {
		.class = AMPERE_CHANNEL_GPFIFO_A,
		.doorbell_handle = tu102_chan_doorbell_handle,
	},

	.ce.class = AMPERE_DMA_COPY_B,
	.gr.class = {
		.i2m = KEPLER_INLINE_TO_MEMORY_B,
		.twod = FERMI_TWOD_A,
		.threed = AMPERE_B,
		.compute = AMPERE_COMPUTE_B,
	},
	.nvdec.class = NVC7B0_VIDEO_DECODER,
	.nvenc.class = NVC7B7_VIDEO_ENCODER,
	.ofa.class = NVC7FA_VIDEO_OFA,
};
