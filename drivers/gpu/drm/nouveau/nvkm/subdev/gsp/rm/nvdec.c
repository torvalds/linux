/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "engine.h"
#include <engine/nvdec.h>

static void *
nvkm_rm_nvdec_dtor(struct nvkm_engine *engine)
{
	return container_of(engine, struct nvkm_nvdec, engine);
}

int
nvkm_rm_nvdec_new(struct nvkm_rm *rm, int inst)
{
	struct nvkm_nvdec *nvdec;
	int ret;

	nvdec = kzalloc(sizeof(*nvdec), GFP_KERNEL);
	if (!nvdec)
		return -ENOMEM;

	ret = nvkm_rm_engine_ctor(nvkm_rm_nvdec_dtor, rm, NVKM_ENGINE_NVDEC, inst,
				  &rm->gpu->nvdec.class, 1, &nvdec->engine);
	if (ret) {
		kfree(nvdec);
		return ret;
	}

	rm->device->nvdec[inst] = nvdec;
	return 0;
}
