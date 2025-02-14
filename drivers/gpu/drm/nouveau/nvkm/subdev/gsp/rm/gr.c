/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "gr.h"

#include <engine/fifo.h>
#include <engine/gr/priv.h>

static int
nvkm_rm_gr_obj_ctor(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	struct r535_gr_chan *chan = container_of(oclass->parent, typeof(*chan), object);

	return nvkm_rm_engine_obj_new(&chan->chan->rm.object, chan->chan->id, oclass, pobject);
}

int
nvkm_rm_gr_new(struct nvkm_rm *rm)
{
	const u32 classes[] = {
		rm->gpu->gr.class.i2m,
		rm->gpu->gr.class.twod,
		rm->gpu->gr.class.threed,
		rm->gpu->gr.class.compute,
	};
	struct nvkm_gr_func *func;
	struct r535_gr *gr;

	func = kzalloc(struct_size(func, sclass, ARRAY_SIZE(classes) + 1), GFP_KERNEL);
	if (!func)
		return -ENOMEM;

	func->dtor = r535_gr_dtor;
	func->oneinit = r535_gr_oneinit;
	func->units = r535_gr_units;
	func->chan_new = r535_gr_chan_new;

	for (int i = 0; i < ARRAY_SIZE(classes); i++) {
		func->sclass[i].oclass = classes[i];
		func->sclass[i].minver = -1;
		func->sclass[i].maxver = 0;
		func->sclass[i].ctor = nvkm_rm_gr_obj_ctor;
	}

	gr = kzalloc(sizeof(*gr), GFP_KERNEL);
	if (!gr) {
		kfree(func);
		return -ENOMEM;
	}

	nvkm_gr_ctor(func, rm->device, NVKM_ENGINE_GR, 0, true, &gr->base);
	rm->device->gr = &gr->base;
	return 0;
}
