/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gpu.h"

#include <engine/ce/priv.h>
#include <engine/fifo/priv.h>

#include <nvif/class.h>

const struct nvkm_rm_gpu
gb20x_gpu = {
	.disp.class = {
		.root = GB202_DISP,
		.caps = GB202_DISP_CAPS,
		.core = GB202_DISP_CORE_CHANNEL_DMA,
		.wndw = GB202_DISP_WINDOW_CHANNEL_DMA,
		.wimm = GB202_DISP_WINDOW_IMM_CHANNEL_DMA,
		.curs = GB202_DISP_CURSOR,
	},

	.usermode.class = BLACKWELL_USERMODE_A,

	.fifo.chan = {
		.class = BLACKWELL_CHANNEL_GPFIFO_B,
		.doorbell_handle = gb202_chan_doorbell_handle,
	},

	.ce = {
		.class = BLACKWELL_DMA_COPY_B,
		.grce_mask = gb202_ce_grce_mask,
	},
	.gr.class = {
		.i2m = BLACKWELL_INLINE_TO_MEMORY_A,
		.twod = FERMI_TWOD_A,
		.threed = BLACKWELL_B,
		.compute = BLACKWELL_COMPUTE_B,
	},
	.nvdec.class = NVCFB0_VIDEO_DECODER,
	.nvenc.class = NVCFB7_VIDEO_ENCODER,
	.nvjpg.class = NVCFD1_VIDEO_NVJPG,
	.ofa.class = NVCFFA_VIDEO_OFA,
};
