/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "engine.h"
#include "gpu.h"

#include <core/object.h>
#include <engine/fifo/chan.h>

struct nvkm_rm_engine {
	struct nvkm_engine engine;

	struct nvkm_engine_func func;
};

struct nvkm_rm_engine_obj {
	struct nvkm_object object;
	struct nvkm_gsp_object rm;
};

static void*
nvkm_rm_engine_obj_dtor(struct nvkm_object *object)
{
	struct nvkm_rm_engine_obj *obj = container_of(object, typeof(*obj), object);

	nvkm_gsp_rm_free(&obj->rm);
	return obj;
}

static const struct nvkm_object_func
nvkm_rm_engine_obj = {
	.dtor = nvkm_rm_engine_obj_dtor,
};

int
nvkm_rm_engine_obj_new(struct nvkm_gsp_object *chan, int chid, const struct nvkm_oclass *oclass,
		       struct nvkm_object **pobject)
{
	struct nvkm_rm *rm = chan->client->gsp->rm;
	const int inst = oclass->engine->subdev.inst;
	const u32 class = oclass->base.oclass;
	const u32 handle = oclass->handle;
	struct nvkm_rm_engine_obj *obj;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	switch (oclass->engine->subdev.type) {
	case NVKM_ENGINE_CE:
		ret = rm->api->ce->alloc(chan, handle, class, inst, &obj->rm);
		break;
	case NVKM_ENGINE_GR:
		ret = nvkm_gsp_rm_alloc(chan, handle, class, 0, &obj->rm);
		break;
	case NVKM_ENGINE_NVDEC:
		ret = rm->api->nvdec->alloc(chan, handle, class, inst, &obj->rm);
		break;
	case NVKM_ENGINE_NVENC:
		ret = rm->api->nvenc->alloc(chan, handle, class, inst, &obj->rm);
		break;
	case NVKM_ENGINE_NVJPG:
		ret = rm->api->nvjpg->alloc(chan, handle, class, inst, &obj->rm);
		break;
	case NVKM_ENGINE_OFA:
		ret = rm->api->ofa->alloc(chan, handle, class, inst, &obj->rm);
		break;
	default:
		ret = -EINVAL;
		WARN_ON(1);
		break;
	}

	if (ret) {
		kfree(obj);
		return ret;
	}

	nvkm_object_ctor(&nvkm_rm_engine_obj, oclass, &obj->object);
	*pobject = &obj->object;
	return 0;
}

static int
nvkm_rm_engine_obj_ctor(const struct nvkm_oclass *oclass, void *argv, u32 argc,
			struct nvkm_object **pobject)
{
	struct nvkm_chan *chan = nvkm_uchan_chan(oclass->parent);

	return nvkm_rm_engine_obj_new(&chan->rm.object, chan->id, oclass, pobject);
}

static void *
nvkm_rm_engine_dtor(struct nvkm_engine *engine)
{
	kfree(engine->func);
	return engine;
}

int
nvkm_rm_engine_ctor(void *(*dtor)(struct nvkm_engine *), struct nvkm_rm *rm,
		    enum nvkm_subdev_type type, int inst,
		    const u32 *class, int nclass, struct nvkm_engine *engine)
{
	struct nvkm_engine_func *func;

	func = kzalloc(struct_size(func, sclass, nclass + 1), GFP_KERNEL);
	if (!func)
		return -ENOMEM;

	func->dtor = dtor;

	for (int i = 0; i < nclass; i++) {
		func->sclass[i].oclass = class[i];
		func->sclass[i].minver = -1;
		func->sclass[i].maxver = 0;
		func->sclass[i].ctor = nvkm_rm_engine_obj_ctor;
	}

	nvkm_engine_ctor(func, rm->device, type, inst, true, engine);
	return 0;
}

static int
nvkm_rm_engine_new_(struct nvkm_rm *rm, enum nvkm_subdev_type type, int inst, u32 class,
		    struct nvkm_engine **pengine)
{
	struct nvkm_engine *engine;
	int ret;

	engine = kzalloc(sizeof(*engine), GFP_KERNEL);
	if (!engine)
		return -ENOMEM;

	ret = nvkm_rm_engine_ctor(nvkm_rm_engine_dtor, rm, type, inst, &class, 1, engine);
	if (ret) {
		kfree(engine);
		return ret;
	}

	*pengine = engine;
	return 0;
}

int
nvkm_rm_engine_new(struct nvkm_rm *rm, enum nvkm_subdev_type type, int inst)
{
	const struct nvkm_rm_gpu *gpu = rm->gpu;
	struct nvkm_device *device = rm->device;

	switch (type) {
	case NVKM_ENGINE_CE:
		if (WARN_ON(inst >= ARRAY_SIZE(device->ce)))
			return -EINVAL;

		return nvkm_rm_engine_new_(rm, type, inst, gpu->ce.class, &device->ce[inst]);
	case NVKM_ENGINE_GR:
		if (inst != 0)
			return -ENODEV; /* MiG not supported, just ignore. */

		return nvkm_rm_gr_new(rm);
	case NVKM_ENGINE_NVDEC:
		if (WARN_ON(inst >= ARRAY_SIZE(device->nvdec)))
			return -EINVAL;

		return nvkm_rm_nvdec_new(rm, inst);
	case NVKM_ENGINE_NVENC:
		if (WARN_ON(inst >= ARRAY_SIZE(device->nvenc)))
			return -EINVAL;

		return nvkm_rm_nvenc_new(rm, inst);
	case NVKM_ENGINE_NVJPG:
		if (WARN_ON(inst >= ARRAY_SIZE(device->nvjpg)))
			return -EINVAL;

		return nvkm_rm_engine_new_(rm, type, inst, gpu->nvjpg.class, &device->nvjpg[inst]);
	case NVKM_ENGINE_OFA:
		if (WARN_ON(inst >= ARRAY_SIZE(device->ofa)))
			return -EINVAL;

		return nvkm_rm_engine_new_(rm, type, inst, gpu->ofa.class, &device->ofa[inst]);
	default:
		break;
	}

	return -ENODEV;
}
