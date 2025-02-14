/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <rm/rm.h>

static const struct nvkm_rm_api
r535_api = {
	.rpc = &r535_rpc,
	.ctrl = &r535_ctrl,
	.alloc = &r535_alloc,
	.client = &r535_client,
	.device = &r535_device,
};

const struct nvkm_rm_impl
r535_rm_tu102 = {
	.api = &r535_api,
};

const struct nvkm_rm_impl
r535_rm_ga102 = {
	.api = &r535_api,
};
