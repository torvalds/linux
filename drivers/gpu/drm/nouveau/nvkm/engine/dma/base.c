/*
 * Copyright 2012 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 */
#include "priv.h"

#include <core/client.h>
#include <core/gpuobj.h>

struct hack {
	struct nvkm_gpuobj object;
	struct nvkm_gpuobj *parent;
};

static void
dtor(struct nvkm_object *object)
{
	struct hack *hack = (void *)object;
	nvkm_gpuobj_del(&hack->parent);
	nvkm_object_destroy(&hack->object.object);
}

static struct nvkm_oclass
hack = {
	.handle = NV_GPUOBJ_CLASS,
	.ofuncs = &(struct nvkm_ofuncs) {
		.dtor = dtor,
		.init = _nvkm_object_init,
		.fini = _nvkm_object_fini,
	},
};

static int
nvkm_dmaobj_bind(struct nvkm_dmaobj *dmaobj, struct nvkm_gpuobj *pargpu,
		 struct nvkm_gpuobj **pgpuobj)
{
	const struct nvkm_dma_impl *impl = (void *)
		nv_oclass(nv_object(dmaobj)->engine);
	int ret = 0;

	if (&dmaobj->base == &pargpu->object) { /* ctor bind */
		struct nvkm_object *parent = (void *)pargpu;
		struct hack *object;

		if (parent->parent->parent == &nvkm_client(parent)->object) {
			/* delayed, or no, binding */
			return 0;
		}

		pargpu = (void *)nv_pclass((void *)pargpu, NV_GPUOBJ_CLASS);

		ret = nvkm_object_create(parent, NULL, &hack, NV_GPUOBJ_CLASS, &object);
		if (ret == 0) {
			nvkm_object_ref(NULL, &parent);
			*pgpuobj = &object->object;

			ret = impl->bind(dmaobj, pargpu, &object->parent);
			if (ret)
				return ret;

			object->object.node = object->parent->node;
			object->object.addr = object->parent->addr;
			object->object.size = object->parent->size;
			return 0;
		}

		return ret;
	}

	return impl->bind(dmaobj, pargpu, pgpuobj);
}

int
_nvkm_dma_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	const struct nvkm_dma_impl *impl = (void *)oclass;
	struct nvkm_dma *dmaeng;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true, "DMAOBJ",
				 "dmaobj", &dmaeng);
	*pobject = nv_object(dmaeng);
	if (ret)
		return ret;

	nv_engine(dmaeng)->sclass = impl->sclass;
	dmaeng->bind = nvkm_dmaobj_bind;
	return 0;
}
