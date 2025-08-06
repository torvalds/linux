/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVKM_RM_ENGINE_H__
#define __NVKM_RM_ENGINE_H__
#include "gpu.h"

int nvkm_rm_engine_ctor(void *(*dtor)(struct nvkm_engine *), struct nvkm_rm *,
			enum nvkm_subdev_type type, int inst,
			const u32 *class, int nclass, struct nvkm_engine *);
int nvkm_rm_engine_new(struct nvkm_rm *, enum nvkm_subdev_type, int inst);

int nvkm_rm_engine_obj_new(struct nvkm_gsp_object *chan, int chid, const struct nvkm_oclass *,
			   struct nvkm_object **);

int nvkm_rm_gr_new(struct nvkm_rm *);
int nvkm_rm_nvdec_new(struct nvkm_rm *, int inst);
int nvkm_rm_nvenc_new(struct nvkm_rm *, int inst);
#endif
