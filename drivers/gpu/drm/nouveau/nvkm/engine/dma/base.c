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

#include <nvif/class.h>

struct nvkm_dmaobj *
nvkm_dma_search(struct nvkm_dma *dma, struct nvkm_client *client, u64 object)
{
	struct rb_node *node = client->dmaroot.rb_node;
	while (node) {
		struct nvkm_dmaobj *dmaobj =
			container_of(node, typeof(*dmaobj), rb);
		if (object < dmaobj->handle)
			node = node->rb_left;
		else
		if (object > dmaobj->handle)
			node = node->rb_right;
		else
			return dmaobj;
	}
	return NULL;
}

static int
nvkm_dma_oclass_new(struct nvkm_device *device,
		    const struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	struct nvkm_dma *dma = nvkm_dma(oclass->engine);
	struct nvkm_dma_impl *impl = (void *)dma->engine.subdev.object.oclass;
	struct nvkm_dmaobj *dmaobj = NULL;
	struct nvkm_client *client = oclass->client;
	struct rb_node **ptr = &client->dmaroot.rb_node;
	struct rb_node *parent = NULL;
	int ret;

	ret = impl->class_new(dma, oclass, data, size, &dmaobj);
	if (dmaobj)
		*pobject = &dmaobj->object;
	if (ret)
		return ret;

	dmaobj->handle = oclass->object;

	while (*ptr) {
		struct nvkm_dmaobj *obj = container_of(*ptr, typeof(*obj), rb);
		parent = *ptr;
		if (dmaobj->handle < obj->handle)
			ptr = &parent->rb_left;
		else
		if (dmaobj->handle > obj->handle)
			ptr = &parent->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&dmaobj->rb, parent, ptr);
	rb_insert_color(&dmaobj->rb, &client->dmaroot);
	return 0;
}

static const struct nvkm_device_oclass
nvkm_dma_oclass_base = {
	.ctor = nvkm_dma_oclass_new,
};

static const struct nvkm_sclass
nvkm_dma_sclass[] = {
	{ 0, 0, NV_DMA_FROM_MEMORY },
	{ 0, 0, NV_DMA_TO_MEMORY },
	{ 0, 0, NV_DMA_IN_MEMORY },
};

static int
nvkm_dma_oclass_base_get(struct nvkm_oclass *sclass, int index,
			 const struct nvkm_device_oclass **class)
{
	const int count = ARRAY_SIZE(nvkm_dma_sclass);
	if (index < count) {
		const struct nvkm_sclass *oclass = &nvkm_dma_sclass[index];
		sclass->base = oclass[0];
		sclass->engn = oclass;
		*class = &nvkm_dma_oclass_base;
		return index;
	}
	return count;
}

static const struct nvkm_engine_func
nvkm_dma = {
	.base.sclass = nvkm_dma_oclass_base_get,
};

#include <core/gpuobj.h>

static struct nvkm_oclass empty = {
	.ofuncs = &(struct nvkm_ofuncs) {
		.dtor = nvkm_object_destroy,
		.init = _nvkm_object_init,
		.fini = _nvkm_object_fini,
	},
};

static int
nvkm_dmaobj_compat_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
			struct nvkm_oclass *oclass, void *data, u32 size,
			struct nvkm_object **pobject)
{
	struct nvkm_oclass hack = {
		.base.oclass = oclass->handle,
		.client = nvkm_client(parent),
		.parent = parent,
		.engine = nv_engine(engine),
	};
	struct nvkm_dma *dma = (void *)engine;
	struct nvkm_dma_impl *impl = (void *)dma->engine.subdev.object.oclass;
	struct nvkm_dmaobj *dmaobj = NULL;
	struct nvkm_gpuobj *gpuobj;
	int ret;

	ret = impl->class_new(dma, &hack, data, size, &dmaobj);
	if (dmaobj)
		*pobject = &dmaobj->object;
	if (ret)
		return ret;

	gpuobj = (void *)nv_pclass(parent, NV_GPUOBJ_CLASS);

	ret = dmaobj->func->bind(dmaobj, gpuobj, 16, &gpuobj);
	nvkm_object_ref(NULL, pobject);
	if (ret)
		return ret;

	ret = nvkm_object_create(parent, engine, &empty, 0, pobject);
	if (ret)
		return ret;

	gpuobj->object.parent = *pobject;
	gpuobj->object.engine = &dma->engine;
	gpuobj->object.oclass = oclass;
	gpuobj->object.pclass = NV_GPUOBJ_CLASS;
#if CONFIG_NOUVEAU_DEBUG >= NV_DBG_PARANOIA
	gpuobj->object._magic = NVKM_OBJECT_MAGIC;
#endif
	*pobject = &gpuobj->object;
	return 0;
}

static void
nvkm_dmaobj_compat_dtor(struct nvkm_object *object)
{
	struct nvkm_object *parent = object->parent;
	struct nvkm_gpuobj *gpuobj = (void *)object;
	nvkm_gpuobj_del(&gpuobj);
	nvkm_object_ref(NULL, &parent);
}

static struct nvkm_ofuncs
nvkm_dmaobj_compat_ofuncs = {
	.ctor = nvkm_dmaobj_compat_ctor,
	.dtor = nvkm_dmaobj_compat_dtor,
	.init = _nvkm_object_init,
	.fini = _nvkm_object_fini,
};

static struct nvkm_oclass
nvkm_dma_compat_sclass[] = {
	{ NV_DMA_FROM_MEMORY, &nvkm_dmaobj_compat_ofuncs },
	{ NV_DMA_TO_MEMORY, &nvkm_dmaobj_compat_ofuncs },
	{ NV_DMA_IN_MEMORY, &nvkm_dmaobj_compat_ofuncs },
	{}
};

int
_nvkm_dma_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_dma *dmaeng;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true, "DMAOBJ",
				 "dmaobj", &dmaeng);
	*pobject = nv_object(dmaeng);
	if (ret)
		return ret;

	dmaeng->engine.sclass = nvkm_dma_compat_sclass;
	dmaeng->engine.func = &nvkm_dma;
	return 0;
}
