/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <subdev/gsp.h>
#ifndef __NVKM_RM_H__
#define __NVKM_RM_H__

struct nvkm_rm_api {
	const struct nvkm_rm_api_rpc {
		void *(*get)(struct nvkm_gsp *, u32 fn, u32 argc);
		void *(*push)(struct nvkm_gsp *gsp, void *argv,
			      enum nvkm_gsp_rpc_reply_policy policy, u32 repc);
		void (*done)(struct nvkm_gsp *gsp, void *repv);
	} *rpc;
};

extern const struct nvkm_rm_api r535_rm;
extern const struct nvkm_rm_api_rpc r535_rpc;
#endif
