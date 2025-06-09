/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <engine/fifo/priv.h>

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

	.fifo.chan = {
		.class = TURING_CHANNEL_GPFIFO_A,
		.doorbell_handle = tu102_chan_doorbell_handle,
	},

	.ce.class = TURING_DMA_COPY_A,
	.gr.class = {
		.i2m = KEPLER_INLINE_TO_MEMORY_B,
		.twod = FERMI_TWOD_A,
		.threed = TURING_A,
		.compute = TURING_COMPUTE_A,
	},
	.nvdec.class = NVC4B0_VIDEO_DECODER,
	.nvenc.class = NVC4B7_VIDEO_ENCODER,
};
