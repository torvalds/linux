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

static int
nvkm_rm_gr_fini(struct nvkm_gr *base, bool suspend)
{
	struct nvkm_rm *rm = base->engine.subdev.device->gsp->rm;
	struct r535_gr *gr = container_of(base, typeof(*gr), base);

	if (rm->api->gr->scrubber.fini)
		rm->api->gr->scrubber.fini(gr);

	return 0;
}

static int
nvkm_rm_gr_init(struct nvkm_gr *base)
{
	struct nvkm_rm *rm = base->engine.subdev.device->gsp->rm;
	struct r535_gr *gr = container_of(base, typeof(*gr), base);
	int ret;

	if (rm->api->gr->scrubber.init) {
		ret = rm->api->gr->scrubber.init(gr);
		if (ret)
			return ret;
	}

	return 0;
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
	func->init = nvkm_rm_gr_init;
	func->fini = nvkm_rm_gr_fini;
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
	gr->scrubber.chid = -1;
	rm->device->gr = &gr->base;
	return 0;
}
