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
#include <core/parent.h>
#include <core/client.h>
#include <core/engine.h>

int
nvkm_parent_sclass(struct nvkm_object *parent, u16 handle,
		   struct nvkm_object **pengine,
		   struct nvkm_oclass **poclass)
{
	struct nvkm_sclass *sclass;
	struct nvkm_engine *engine;
	struct nvkm_oclass *oclass;
	u64 mask;

	sclass = nv_parent(parent)->sclass;
	while (sclass) {
		if ((sclass->oclass->handle & 0xffff) == handle) {
			*pengine = &parent->engine->subdev.object;
			*poclass = sclass->oclass;
			return 0;
		}

		sclass = sclass->sclass;
	}

	mask = nv_parent(parent)->engine;
	while (mask) {
		int i = __ffs64(mask);

		if (nv_iclass(parent, NV_CLIENT_CLASS))
			engine = nv_engine(nv_client(parent)->device);
		else
			engine = nvkm_engine(parent, i);

		if (engine) {
			oclass = engine->sclass;
			while (oclass->ofuncs) {
				if ((oclass->handle & 0xffff) == handle) {
					*pengine = nv_object(engine);
					*poclass = oclass;
					return 0;
				}
				oclass++;
			}
		}

		mask &= ~(1ULL << i);
	}

	return -EINVAL;
}

int
nvkm_parent_lclass(struct nvkm_object *parent, u32 *lclass, int size)
{
	struct nvkm_sclass *sclass;
	struct nvkm_engine *engine;
	struct nvkm_oclass *oclass;
	int nr = -1, i;
	u64 mask;

	sclass = nv_parent(parent)->sclass;
	while (sclass) {
		if (++nr < size)
			lclass[nr] = sclass->oclass->handle & 0xffff;
		sclass = sclass->sclass;
	}

	mask = nv_parent(parent)->engine;
	while (i = __ffs64(mask), mask) {
		engine = nvkm_engine(parent, i);
		if (engine && (oclass = engine->sclass)) {
			while (oclass->ofuncs) {
				if (++nr < size)
					lclass[nr] = oclass->handle & 0xffff;
				oclass++;
			}
		}

		mask &= ~(1ULL << i);
	}

	return nr + 1;
}

int
nvkm_parent_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, u32 pclass,
		    struct nvkm_oclass *sclass, u64 engcls,
		    int size, void **pobject)
{
	struct nvkm_parent *object;
	struct nvkm_sclass *nclass;
	int ret;

	ret = nvkm_object_create_(parent, engine, oclass, pclass |
				  NV_PARENT_CLASS, size, pobject);
	object = *pobject;
	if (ret)
		return ret;

	while (sclass && sclass->ofuncs) {
		nclass = kzalloc(sizeof(*nclass), GFP_KERNEL);
		if (!nclass)
			return -ENOMEM;

		nclass->sclass = object->sclass;
		object->sclass = nclass;
		nclass->engine = engine ? nv_engine(engine) : NULL;
		nclass->oclass = sclass;
		sclass++;
	}

	object->engine = engcls;
	return 0;
}

void
nvkm_parent_destroy(struct nvkm_parent *parent)
{
	struct nvkm_sclass *sclass;

	while ((sclass = parent->sclass)) {
		parent->sclass = sclass->sclass;
		kfree(sclass);
	}

	nvkm_object_destroy(&parent->object);
}


void
_nvkm_parent_dtor(struct nvkm_object *object)
{
	nvkm_parent_destroy(nv_parent(object));
}
