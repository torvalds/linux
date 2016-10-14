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
#include <engine/fifo.h>

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
	struct nvkm_dmaobj *dmaobj = NULL;
	struct nvkm_client *client = oclass->client;
	struct rb_node **ptr = &client->dmaroot.rb_node;
	struct rb_node *parent = NULL;
	int ret;

	ret = dma->func->class_new(dma, oclass, data, size, &dmaobj);
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

static int
nvkm_dma_oclass_fifo_new(const struct nvkm_oclass *oclass, void *data, u32 size,
			 struct nvkm_object **pobject)
{
	return nvkm_dma_oclass_new(oclass->engine->subdev.device,
				   oclass, data, size, pobject);
}

static const struct nvkm_sclass
nvkm_dma_sclass[] = {
	{ 0, 0, NV_DMA_FROM_MEMORY, NULL, nvkm_dma_oclass_fifo_new },
	{ 0, 0, NV_DMA_TO_MEMORY, NULL, nvkm_dma_oclass_fifo_new },
	{ 0, 0, NV_DMA_IN_MEMORY, NULL, nvkm_dma_oclass_fifo_new },
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

static int
nvkm_dma_oclass_fifo_get(struct nvkm_oclass *oclass, int index)
{
	const int count = ARRAY_SIZE(nvkm_dma_sclass);
	if (index < count) {
		oclass->base = nvkm_dma_sclass[index];
		return index;
	}
	return count;
}

static void *
nvkm_dma_dtor(struct nvkm_engine *engine)
{
	return nvkm_dma(engine);
}

static const struct nvkm_engine_func
nvkm_dma = {
	.dtor = nvkm_dma_dtor,
	.base.sclass = nvkm_dma_oclass_base_get,
	.fifo.sclass = nvkm_dma_oclass_fifo_get,
};

int
nvkm_dma_new_(const struct nvkm_dma_func *func, struct nvkm_device *device,
	      int index, struct nvkm_dma **pdma)
{
	struct nvkm_dma *dma;

	if (!(dma = *pdma = kzalloc(sizeof(*dma), GFP_KERNEL)))
		return -ENOMEM;
	dma->func = func;

	return nvkm_engine_ctor(&nvkm_dma, device, index, true, &dma->engine);
}
