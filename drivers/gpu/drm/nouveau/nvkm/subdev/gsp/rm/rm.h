/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <subdev/gsp.h>
#ifndef __NVKM_RM_H__
#define __NVKM_RM_H__
#include "handles.h"

struct nvkm_rm_impl {
	const struct nvkm_rm_wpr *wpr;
	const struct nvkm_rm_api *api;
};

struct nvkm_rm {
	struct nvkm_device *device;
	const struct nvkm_rm_gpu *gpu;
	const struct nvkm_rm_wpr *wpr;
	const struct nvkm_rm_api *api;
};

struct nvkm_rm_wpr {
	u32 os_carveout_size;
	u32 base_size;
	u64 heap_size_min;
};

struct nvkm_rm_api {
	const struct nvkm_rm_api_gsp {
		int (*set_system_info)(struct nvkm_gsp *);
		int (*get_static_info)(struct nvkm_gsp *);
		bool (*xlat_mc_engine_idx)(u32 mc_engine_idx, enum nvkm_subdev_type *, int *inst);
	} *gsp;

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

	const struct nvkm_rm_api_client {
		int (*ctor)(struct nvkm_gsp *, struct nvkm_gsp_client *);
		void (*dtor)(struct nvkm_gsp_client *);
	} *client;

	const struct nvkm_rm_api_device {
		int (*ctor)(struct nvkm_gsp_client *, struct nvkm_gsp_device *);
		void (*dtor)(struct nvkm_gsp_device *);

		struct {
			int (*ctor)(struct nvkm_gsp_device *, u32 handle, u32 id,
				    nvkm_gsp_event_func, struct nvkm_gsp_event *);
			void (*dtor)(struct nvkm_gsp_event *);
		} event;
	} *device;

	const struct nvkm_rm_api_engine {
		int (*alloc)(struct nvkm_gsp_object *chan, u32 handle, u32 class, int inst,
			     struct nvkm_gsp_object *);
	} *ce, *nvdec, *nvenc, *nvjpg, *ofa;
};

extern const struct nvkm_rm_impl r535_rm_tu102;
extern const struct nvkm_rm_impl r535_rm_ga102;
extern const struct nvkm_rm_api_gsp r535_gsp;
extern const struct nvkm_rm_api_rpc r535_rpc;
extern const struct nvkm_rm_api_ctrl r535_ctrl;
extern const struct nvkm_rm_api_alloc r535_alloc;
extern const struct nvkm_rm_api_client r535_client;
extern const struct nvkm_rm_api_device r535_device;
extern const struct nvkm_rm_api_engine r535_ce;
void *r535_gr_dtor(struct nvkm_gr *);
int r535_gr_oneinit(struct nvkm_gr *);
u64 r535_gr_units(struct nvkm_gr *);
int r535_gr_chan_new(struct nvkm_gr *, struct nvkm_chan *, const struct nvkm_oclass *,
		     struct nvkm_object **);
extern const struct nvkm_rm_api_engine r535_nvdec;
extern const struct nvkm_rm_api_engine r535_nvenc;
extern const struct nvkm_rm_api_engine r535_nvjpg;
extern const struct nvkm_rm_api_engine r535_ofa;
#endif
