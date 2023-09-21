/*
 * Copyright 2023 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <core/object.h>
#include <subdev/gsp.h>
#include <subdev/mmu.h>
#include <engine/fifo.h>

#include <nvrm/nvtypes.h>
#include <nvrm/535.113.01/common/sdk/nvidia/inc/nvos.h>

struct r535_ofa_obj {
	struct nvkm_object object;
	struct nvkm_gsp_object rm;
};

static void *
r535_ofa_obj_dtor(struct nvkm_object *object)
{
	struct r535_ofa_obj *obj = container_of(object, typeof(*obj), object);

	nvkm_gsp_rm_free(&obj->rm);
	return obj;
}

static const struct nvkm_object_func
r535_ofa_obj = {
	.dtor = r535_ofa_obj_dtor,
};

static int
r535_ofa_obj_ctor(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		 struct nvkm_object **pobject)
{
	struct nvkm_chan *chan = nvkm_uchan_chan(oclass->parent);
	struct r535_ofa_obj *obj;
	NV_OFA_ALLOCATION_PARAMETERS *args;

	if (!(obj = kzalloc(sizeof(*obj), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&r535_ofa_obj, oclass, &obj->object);
	*pobject = &obj->object;

	args = nvkm_gsp_rm_alloc_get(&chan->rm.object, oclass->handle, oclass->base.oclass,
				     sizeof(*args), &obj->rm);
	if (WARN_ON(IS_ERR(args)))
		return PTR_ERR(args);

	args->size = sizeof(*args);

	return nvkm_gsp_rm_alloc_wr(&obj->rm, args);
}

static void *
r535_ofa_dtor(struct nvkm_engine *engine)
{
	kfree(engine->func);
	return engine;
}

int
r535_ofa_new(const struct nvkm_engine_func *hw, struct nvkm_device *device,
	     enum nvkm_subdev_type type, int inst, struct nvkm_engine **pengine)
{
	struct nvkm_engine_func *rm;
	int nclass, ret;

	for (nclass = 0; hw->sclass[nclass].oclass; nclass++);

	if (!(rm = kzalloc(sizeof(*rm) + (nclass + 1) * sizeof(rm->sclass[0]), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_ofa_dtor;
	for (int i = 0; i < nclass; i++) {
		rm->sclass[i].minver = hw->sclass[i].minver;
		rm->sclass[i].maxver = hw->sclass[i].maxver;
		rm->sclass[i].oclass = hw->sclass[i].oclass;
		rm->sclass[i].ctor = r535_ofa_obj_ctor;
	}

	ret = nvkm_engine_new_(rm, device, type, inst, true, pengine);
	if (ret)
		kfree(rm);

	return ret;
}
