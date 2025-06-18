/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/rm.h>

#include "nvrm/gsp.h"

static const struct nvkm_rm_wpr
r535_wpr_libos2 = {
	.os_carveout_size = GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS2,
	.base_size = GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X,
	.heap_size_min = GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MIN_MB,
};

static const struct nvkm_rm_wpr
r535_wpr_libos3 = {
	.os_carveout_size = GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS3,
	.base_size = GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X,
	.heap_size_min = GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MIN_MB,
};

static const struct nvkm_rm_api
r535_api = {
	.gsp = &r535_gsp,
	.rpc = &r535_rpc,
	.ctrl = &r535_ctrl,
	.alloc = &r535_alloc,
	.client = &r535_client,
	.device = &r535_device,
	.fbsr = &r535_fbsr,
	.disp = &r535_disp,
	.fifo = &r535_fifo,
	.ce = &r535_ce,
	.gr = &r535_gr,
	.nvdec = &r535_nvdec,
	.nvenc = &r535_nvenc,
	.nvjpg = &r535_nvjpg,
	.ofa = &r535_ofa,
};

const struct nvkm_rm_impl
r535_rm_tu102 = {
	.wpr = &r535_wpr_libos2,
	.api = &r535_api,
};

const struct nvkm_rm_impl
r535_rm_ga102 = {
	.wpr = &r535_wpr_libos3,
	.api = &r535_api,
};
