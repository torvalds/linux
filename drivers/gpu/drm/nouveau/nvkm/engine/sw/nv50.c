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
#include "nv50.h"

#include <core/device.h>
#include <core/handle.h>
#include <core/namedb.h>
#include <engine/disp.h>
#include <subdev/bar.h>

#include <nvif/event.h>

/*******************************************************************************
 * software object classes
 ******************************************************************************/

static int
nv50_sw_mthd_dma_vblsem(struct nvkm_object *object, u32 mthd,
			void *args, u32 size)
{
	struct nv50_sw_chan *chan = (void *)nv_engctx(object->parent);
	struct nvkm_fifo_chan *fifo = (void *)nv_object(chan)->parent;
	struct nvkm_handle *handle;
	int ret = -EINVAL;

	handle = nvkm_namedb_get(nv_namedb(fifo), *(u32 *)args);
	if (!handle)
		return -ENOENT;

	if (nv_iclass(handle->object, NV_GPUOBJ_CLASS)) {
		struct nvkm_gpuobj *gpuobj = nv_gpuobj(handle->object);
		chan->vblank.ctxdma = gpuobj->node->offset >> 4;
		ret = 0;
	}
	nvkm_namedb_put(handle);
	return ret;
}

static int
nv50_sw_mthd_vblsem_offset(struct nvkm_object *object, u32 mthd,
			   void *args, u32 size)
{
	struct nv50_sw_chan *chan = (void *)nv_engctx(object->parent);
	chan->vblank.offset = *(u32 *)args;
	return 0;
}

int
nv50_sw_mthd_vblsem_value(struct nvkm_object *object, u32 mthd,
			  void *args, u32 size)
{
	struct nv50_sw_chan *chan = (void *)nv_engctx(object->parent);
	chan->vblank.value = *(u32 *)args;
	return 0;
}

int
nv50_sw_mthd_vblsem_release(struct nvkm_object *object, u32 mthd,
			    void *args, u32 size)
{
	struct nv50_sw_chan *chan = (void *)nv_engctx(object->parent);
	u32 head = *(u32 *)args;
	if (head >= nvkm_disp(chan)->vblank.index_nr)
		return -EINVAL;

	nvkm_notify_get(&chan->vblank.notify[head]);
	return 0;
}

int
nv50_sw_mthd_flip(struct nvkm_object *object, u32 mthd, void *args, u32 size)
{
	struct nv50_sw_chan *chan = (void *)nv_engctx(object->parent);
	if (chan->base.flip)
		return chan->base.flip(chan->base.flip_data);
	return -EINVAL;
}

static struct nvkm_omthds
nv50_sw_omthds[] = {
	{ 0x018c, 0x018c, nv50_sw_mthd_dma_vblsem },
	{ 0x0400, 0x0400, nv50_sw_mthd_vblsem_offset },
	{ 0x0404, 0x0404, nv50_sw_mthd_vblsem_value },
	{ 0x0408, 0x0408, nv50_sw_mthd_vblsem_release },
	{ 0x0500, 0x0500, nv50_sw_mthd_flip },
	{}
};

static struct nvkm_oclass
nv50_sw_sclass[] = {
	{ 0x506e, &nvkm_object_ofuncs, nv50_sw_omthds },
	{}
};

/*******************************************************************************
 * software context
 ******************************************************************************/

static int
nv50_sw_vblsem_release(struct nvkm_notify *notify)
{
	struct nv50_sw_chan *chan =
		container_of(notify, typeof(*chan), vblank.notify[notify->index]);
	struct nv50_sw_priv *priv = (void *)nv_object(chan)->engine;
	struct nvkm_bar *bar = nvkm_bar(priv);

	nv_wr32(priv, 0x001704, chan->vblank.channel);
	nv_wr32(priv, 0x001710, 0x80000000 | chan->vblank.ctxdma);
	bar->flush(bar);

	if (nv_device(priv)->chipset == 0x50) {
		nv_wr32(priv, 0x001570, chan->vblank.offset);
		nv_wr32(priv, 0x001574, chan->vblank.value);
	} else {
		nv_wr32(priv, 0x060010, chan->vblank.offset);
		nv_wr32(priv, 0x060014, chan->vblank.value);
	}

	return NVKM_NOTIFY_DROP;
}

void
nv50_sw_context_dtor(struct nvkm_object *object)
{
	struct nv50_sw_chan *chan = (void *)object;
	int i;

	for (i = 0; i < ARRAY_SIZE(chan->vblank.notify); i++)
		nvkm_notify_fini(&chan->vblank.notify[i]);

	nvkm_sw_context_destroy(&chan->base);
}

int
nv50_sw_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nvkm_disp *pdisp = nvkm_disp(parent);
	struct nv50_sw_cclass *pclass = (void *)oclass;
	struct nv50_sw_chan *chan;
	int ret, i;

	ret = nvkm_sw_context_create(parent, engine, oclass, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	for (i = 0; pdisp && i < pdisp->vblank.index_nr; i++) {
		ret = nvkm_notify_init(NULL, &pdisp->vblank, pclass->vblank,
				       false,
				       &(struct nvif_notify_head_req_v0) {
					.head = i,
				       },
				       sizeof(struct nvif_notify_head_req_v0),
				       sizeof(struct nvif_notify_head_rep_v0),
				       &chan->vblank.notify[i]);
		if (ret)
			return ret;
	}

	chan->vblank.channel = nv_gpuobj(parent->parent)->addr >> 12;
	return 0;
}

static struct nv50_sw_cclass
nv50_sw_cclass = {
	.base.handle = NV_ENGCTX(SW, 0x50),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_sw_context_ctor,
		.dtor = nv50_sw_context_dtor,
		.init = _nvkm_sw_context_init,
		.fini = _nvkm_sw_context_fini,
	},
	.vblank = nv50_sw_vblsem_release,
};

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

int
nv50_sw_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct nv50_sw_oclass *pclass = (void *)oclass;
	struct nv50_sw_priv *priv;
	int ret;

	ret = nvkm_sw_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = pclass->cclass;
	nv_engine(priv)->sclass = pclass->sclass;
	nv_subdev(priv)->intr = nv04_sw_intr;
	return 0;
}

struct nvkm_oclass *
nv50_sw_oclass = &(struct nv50_sw_oclass) {
	.base.handle = NV_ENGINE(SW, 0x50),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_sw_ctor,
		.dtor = _nvkm_sw_dtor,
		.init = _nvkm_sw_init,
		.fini = _nvkm_sw_fini,
	},
	.cclass = &nv50_sw_cclass.base,
	.sclass =  nv50_sw_sclass,
}.base;
