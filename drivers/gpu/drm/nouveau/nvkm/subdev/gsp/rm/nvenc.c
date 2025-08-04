/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "engine.h"
#include <engine/nvenc.h>

static void *
nvkm_rm_nvenc_dtor(struct nvkm_engine *engine)
{
	return container_of(engine, struct nvkm_nvenc, engine);
}

int
nvkm_rm_nvenc_new(struct nvkm_rm *rm, int inst)
{
	struct nvkm_nvenc *nvenc;
	int ret;

	nvenc = kzalloc(sizeof(*nvenc), GFP_KERNEL);
	if (!nvenc)
		return -ENOMEM;

	ret = nvkm_rm_engine_ctor(nvkm_rm_nvenc_dtor, rm, NVKM_ENGINE_NVENC, inst,
				  &rm->gpu->nvenc.class, 1, &nvenc->engine);
	if (ret) {
		kfree(nvenc);
		return ret;
	}

	rm->device->nvenc[inst] = nvenc;
	return 0;
}
