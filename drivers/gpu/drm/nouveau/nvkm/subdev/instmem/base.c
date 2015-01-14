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

#include <core/engine.h>

/******************************************************************************
 * instmem object base implementation
 *****************************************************************************/

void
_nvkm_instobj_dtor(struct nvkm_object *object)
{
	struct nvkm_instmem *imem = nvkm_instmem(object);
	struct nvkm_instobj *iobj = (void *)object;

	mutex_lock(&nv_subdev(imem)->mutex);
	list_del(&iobj->head);
	mutex_unlock(&nv_subdev(imem)->mutex);

	return nvkm_object_destroy(&iobj->base);
}

int
nvkm_instobj_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_instmem *imem = nvkm_instmem(parent);
	struct nvkm_instobj *iobj;
	int ret;

	ret = nvkm_object_create_(parent, engine, oclass, NV_MEMOBJ_CLASS,
				  length, pobject);
	iobj = *pobject;
	if (ret)
		return ret;

	mutex_lock(&imem->base.mutex);
	list_add(&iobj->head, &imem->list);
	mutex_unlock(&imem->base.mutex);
	return 0;
}

/******************************************************************************
 * instmem subdev base implementation
 *****************************************************************************/

static int
nvkm_instmem_alloc(struct nvkm_instmem *imem, struct nvkm_object *parent,
		   u32 size, u32 align, struct nvkm_object **pobject)
{
	struct nvkm_instmem_impl *impl = (void *)imem->base.object.oclass;
	struct nvkm_instobj_args args = { .size = size, .align = align };
	return nvkm_object_ctor(parent, &parent->engine->subdev.object,
				impl->instobj, &args, sizeof(args), pobject);
}

int
_nvkm_instmem_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_instmem *imem = (void *)object;
	struct nvkm_instobj *iobj;
	int i, ret = 0;

	if (suspend) {
		mutex_lock(&imem->base.mutex);
		list_for_each_entry(iobj, &imem->list, head) {
			iobj->suspend = vmalloc(iobj->size);
			if (!iobj->suspend) {
				ret = -ENOMEM;
				break;
			}

			for (i = 0; i < iobj->size; i += 4)
				iobj->suspend[i / 4] = nv_ro32(iobj, i);
		}
		mutex_unlock(&imem->base.mutex);
		if (ret)
			return ret;
	}

	return nvkm_subdev_fini(&imem->base, suspend);
}

int
_nvkm_instmem_init(struct nvkm_object *object)
{
	struct nvkm_instmem *imem = (void *)object;
	struct nvkm_instobj *iobj;
	int ret, i;

	ret = nvkm_subdev_init(&imem->base);
	if (ret)
		return ret;

	mutex_lock(&imem->base.mutex);
	list_for_each_entry(iobj, &imem->list, head) {
		if (iobj->suspend) {
			for (i = 0; i < iobj->size; i += 4)
				nv_wo32(iobj, i, iobj->suspend[i / 4]);
			vfree(iobj->suspend);
			iobj->suspend = NULL;
		}
	}
	mutex_unlock(&imem->base.mutex);
	return 0;
}

int
nvkm_instmem_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_instmem *imem;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "INSTMEM",
				  "instmem", length, pobject);
	imem = *pobject;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&imem->list);
	imem->alloc = nvkm_instmem_alloc;
	return 0;
}
