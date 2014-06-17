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

/******************************************************************************
 * instmem object base implementation
 *****************************************************************************/

void
_nouveau_instobj_dtor(struct nouveau_object *object)
{
	struct nouveau_instmem *imem = (void *)object->engine;
	struct nouveau_instobj *iobj = (void *)object;

	mutex_lock(&nv_subdev(imem)->mutex);
	list_del(&iobj->head);
	mutex_unlock(&nv_subdev(imem)->mutex);

	return nouveau_object_destroy(&iobj->base);
}

int
nouveau_instobj_create_(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass,
			int length, void **pobject)
{
	struct nouveau_instmem *imem = (void *)engine;
	struct nouveau_instobj *iobj;
	int ret;

	ret = nouveau_object_create_(parent, engine, oclass, NV_MEMOBJ_CLASS,
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
nouveau_instmem_alloc(struct nouveau_instmem *imem,
		      struct nouveau_object *parent, u32 size, u32 align,
		      struct nouveau_object **pobject)
{
	struct nouveau_object *engine = nv_object(imem);
	struct nouveau_instmem_impl *impl = (void *)engine->oclass;
	struct nouveau_instobj_args args = { .size = size, .align = align };
	return nouveau_object_ctor(parent, engine, impl->instobj, &args,
				   sizeof(args), pobject);
}

int
_nouveau_instmem_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_instmem *imem = (void *)object;
	struct nouveau_instobj *iobj;
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

	return nouveau_subdev_fini(&imem->base, suspend);
}

int
_nouveau_instmem_init(struct nouveau_object *object)
{
	struct nouveau_instmem *imem = (void *)object;
	struct nouveau_instobj *iobj;
	int ret, i;

	ret = nouveau_subdev_init(&imem->base);
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
nouveau_instmem_create_(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass,
			int length, void **pobject)
{
	struct nouveau_instmem *imem;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0,
				     "INSTMEM", "instmem", length, pobject);
	imem = *pobject;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&imem->list);
	imem->alloc = nouveau_instmem_alloc;
	return 0;
}
