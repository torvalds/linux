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

#include <subdev/instmem.h>

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

	list_add(&iobj->head, &imem->list);
	return 0;
}

void
nouveau_instobj_destroy(struct nouveau_instobj *iobj)
{
	if (iobj->head.prev)
		list_del(&iobj->head);
	return nouveau_object_destroy(&iobj->base);
}

void
_nouveau_instobj_dtor(struct nouveau_object *object)
{
	struct nouveau_instobj *iobj = (void *)object;
	return nouveau_instobj_destroy(iobj);
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
	return 0;
}

int
nouveau_instmem_init(struct nouveau_instmem *imem)
{
	struct nouveau_instobj *iobj;
	int ret, i;

	ret = nouveau_subdev_init(&imem->base);
	if (ret)
		return ret;

	list_for_each_entry(iobj, &imem->list, head) {
		if (iobj->suspend) {
			for (i = 0; i < iobj->size; i += 4)
				nv_wo32(iobj, i, iobj->suspend[i / 4]);
			vfree(iobj->suspend);
			iobj->suspend = NULL;
		}
	}

	return 0;
}

int
nouveau_instmem_fini(struct nouveau_instmem *imem, bool suspend)
{
	struct nouveau_instobj *iobj;
	int i;

	if (suspend) {
		list_for_each_entry(iobj, &imem->list, head) {
			iobj->suspend = vmalloc(iobj->size);
			if (iobj->suspend) {
				for (i = 0; i < iobj->size; i += 4)
					iobj->suspend[i / 4] = nv_ro32(iobj, i);
			} else
				return -ENOMEM;
		}
	}

	return nouveau_subdev_fini(&imem->base, suspend);
}

int
_nouveau_instmem_init(struct nouveau_object *object)
{
	struct nouveau_instmem *imem = (void *)object;
	return nouveau_instmem_init(imem);
}

int
_nouveau_instmem_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_instmem *imem = (void *)object;
	return nouveau_instmem_fini(imem, suspend);
}
