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

	const struct nvkm_rm_api_ctrl {
		void *(*get)(struct nvkm_gsp_object *, u32 cmd, u32 params_size);
		int (*push)(struct nvkm_gsp_object *, void **params, u32 repc);
		void (*done)(struct nvkm_gsp_object *, void *params);
	} *ctrl;

	const struct nvkm_rm_api_alloc {
		void *(*get)(struct nvkm_gsp_object *, u32 oclass, u32 params_size);
		void *(*push)(struct nvkm_gsp_object *, void *params);
		void (*done)(struct nvkm_gsp_object *, void *params);

		int (*free)(struct nvkm_gsp_object *);
	} *alloc;
};

extern const struct nvkm_rm_api r535_rm;
extern const struct nvkm_rm_api_rpc r535_rpc;
extern const struct nvkm_rm_api_ctrl r535_ctrl;
extern const struct nvkm_rm_api_alloc r535_alloc;
#endif
