/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/rm.h>

#include "nvrm/gsp.h"

static const struct nvkm_rm_wpr
r570_wpr_libos2 = {
	.os_carveout_size = GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS2,
	.base_size = GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X,
	.heap_size_min = GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS2_MIN_MB,
};

static const struct nvkm_rm_wpr
r570_wpr_libos3 = {
	.os_carveout_size = GSP_FW_HEAP_PARAM_OS_SIZE_LIBOS3_BAREMETAL,
	.base_size = GSP_FW_HEAP_PARAM_BASE_RM_SIZE_TU10X,
	.heap_size_min = GSP_FW_HEAP_SIZE_OVERRIDE_LIBOS3_BAREMETAL_MIN_MB,
};

static const struct nvkm_rm_api
r570_api = {
	.gsp = &r570_gsp,
	.rpc = &r535_rpc,
	.ctrl = &r535_ctrl,
	.alloc = &r535_alloc,
	.client = &r570_client,
	.device = &r535_device,
	.fbsr = &r570_fbsr,
	.disp = &r570_disp,
	.fifo = &r570_fifo,
	.ce = &r535_ce,
	.gr = &r570_gr,
	.nvdec = &r535_nvdec,
	.nvenc = &r535_nvenc,
	.nvjpg = &r535_nvjpg,
	.ofa = &r570_ofa,
};

const struct nvkm_rm_impl
r570_rm_tu102 = {
	.wpr = &r570_wpr_libos2,
	.api = &r570_api,
};

const struct nvkm_rm_impl
r570_rm_ga102 = {
	.wpr = &r570_wpr_libos3,
	.api = &r570_api,
};
