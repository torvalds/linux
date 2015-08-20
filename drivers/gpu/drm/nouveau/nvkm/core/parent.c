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

#include <nvif/ioctl.h>

int
nvkm_parent_sclass(struct nvkm_object *parent, s32 handle,
		   struct nvkm_object **pengine,
		   struct nvkm_oclass **poclass)
{
	struct nvkm_oclass *sclass, *oclass;
	struct nvkm_engine *engine;
	u64 mask;
	int i;

	sclass = nv_parent(parent)->sclass;
	while ((oclass = sclass++) && oclass->ofuncs) {
		if (oclass->handle == handle) {
			*pengine = &parent->engine->subdev.object;
			*poclass = oclass;
			return 0;
		}
	}

	mask = nv_parent(parent)->engine;
	while (i = __ffs64(mask), mask) {
		engine = nvkm_engine(parent, i);
		if (engine) {
			oclass = engine->sclass;
			while (oclass->ofuncs) {
				if (oclass->handle == handle) {
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
nvkm_parent_lclass(struct nvkm_object *parent, void *data, int size)
{
	struct nvif_ioctl_sclass_oclass_v0 *lclass = data;
	struct nvkm_oclass *sclass, *oclass;
	struct nvkm_engine *engine;
	int nr = -1, i;
	u64 mask;

	sclass = nv_parent(parent)->sclass;
	while ((oclass = sclass++) && oclass->ofuncs) {
		if (++nr < size) {
			lclass[nr].oclass = oclass->handle;
			lclass[nr].minver = -2;
			lclass[nr].maxver = -2;
		}
	}

	mask = nv_parent(parent)->engine;
	while (i = __ffs64(mask), mask) {
		engine = nvkm_engine(parent, i);
		if (engine && (oclass = engine->sclass)) {
			while (oclass->ofuncs) {
				if (++nr < size) {
					lclass[nr].oclass = oclass->handle;
					lclass[nr].minver = -2;
					lclass[nr].maxver = -2;
				}
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
	int ret;

	ret = nvkm_object_create_(parent, engine, oclass, pclass |
				  NV_PARENT_CLASS, size, pobject);
	object = *pobject;
	if (ret)
		return ret;

	object->sclass = sclass;
	object->engine = engcls;
	return 0;
}

void
nvkm_parent_destroy(struct nvkm_parent *parent)
{
	nvkm_object_destroy(&parent->object);
}


void
_nvkm_parent_dtor(struct nvkm_object *object)
{
	nvkm_parent_destroy(nv_parent(object));
}
