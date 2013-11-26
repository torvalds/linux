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

#include <core/os.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/namedb.h>
#include <core/handle.h>
#include <core/gpuobj.h>
#include <core/event.h>

#include <subdev/bar.h>

#include <engine/disp.h>

#include "nv50.h"

/*******************************************************************************
 * software object classes
 ******************************************************************************/

static int
nv50_software_mthd_dma_vblsem(struct nouveau_object *object, u32 mthd,
			      void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	struct nouveau_fifo_chan *fifo = (void *)nv_object(chan)->parent;
	struct nouveau_handle *handle;
	int ret = -EINVAL;

	handle = nouveau_namedb_get(nv_namedb(fifo), *(u32 *)args);
	if (!handle)
		return -ENOENT;

	if (nv_iclass(handle->object, NV_GPUOBJ_CLASS)) {
		struct nouveau_gpuobj *gpuobj = nv_gpuobj(handle->object);
		chan->vblank.ctxdma = gpuobj->node->offset >> 4;
		ret = 0;
	}
	nouveau_namedb_put(handle);
	return ret;
}

static int
nv50_software_mthd_vblsem_offset(struct nouveau_object *object, u32 mthd,
				 void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	chan->vblank.offset = *(u32 *)args;
	return 0;
}

int
nv50_software_mthd_vblsem_value(struct nouveau_object *object, u32 mthd,
				void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	chan->vblank.value = *(u32 *)args;
	return 0;
}

int
nv50_software_mthd_vblsem_release(struct nouveau_object *object, u32 mthd,
				  void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	u32 head = *(u32 *)args;
	if (head >= chan->vblank.nr_event)
		return -EINVAL;

	nouveau_event_get(chan->vblank.event[head]);
	return 0;
}

int
nv50_software_mthd_flip(struct nouveau_object *object, u32 mthd,
			void *args, u32 size)
{
	struct nv50_software_chan *chan = (void *)nv_engctx(object->parent);
	if (chan->base.flip)
		return chan->base.flip(chan->base.flip_data);
	return -EINVAL;
}

static struct nouveau_omthds
nv50_software_omthds[] = {
	{ 0x018c, 0x018c, nv50_software_mthd_dma_vblsem },
	{ 0x0400, 0x0400, nv50_software_mthd_vblsem_offset },
	{ 0x0404, 0x0404, nv50_software_mthd_vblsem_value },
	{ 0x0408, 0x0408, nv50_software_mthd_vblsem_release },
	{ 0x0500, 0x0500, nv50_software_mthd_flip },
	{}
};

static struct nouveau_oclass
nv50_software_sclass[] = {
	{ 0x506e, &nouveau_object_ofuncs, nv50_software_omthds },
	{}
};

/*******************************************************************************
 * software context
 ******************************************************************************/

static int
nv50_software_vblsem_release(void *data, int head)
{
	struct nv50_software_chan *chan = data;
	struct nv50_software_priv *priv = (void *)nv_object(chan)->engine;
	struct nouveau_bar *bar = nouveau_bar(priv);

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

	return NVKM_EVENT_DROP;
}

void
nv50_software_context_dtor(struct nouveau_object *object)
{
	struct nv50_software_chan *chan = (void *)object;
	int i;

	if (chan->vblank.event) {
		for (i = 0; i < chan->vblank.nr_event; i++)
			nouveau_event_ref(NULL, &chan->vblank.event[i]);
		kfree(chan->vblank.event);
	}

	nouveau_software_context_destroy(&chan->base);
}

int
nv50_software_context_ctor(struct nouveau_object *parent,
			   struct nouveau_object *engine,
			   struct nouveau_oclass *oclass, void *data, u32 size,
			   struct nouveau_object **pobject)
{
	struct nouveau_disp *pdisp = nouveau_disp(parent);
	struct nv50_software_cclass *pclass = (void *)oclass;
	struct nv50_software_chan *chan;
	int ret, i;

	ret = nouveau_software_context_create(parent, engine, oclass, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->vblank.nr_event = pdisp ? pdisp->vblank->index_nr : 0;
	chan->vblank.event = kzalloc(chan->vblank.nr_event *
				     sizeof(*chan->vblank.event), GFP_KERNEL);
	if (!chan->vblank.event)
		return -ENOMEM;

	for (i = 0; i < chan->vblank.nr_event; i++) {
		ret = nouveau_event_new(pdisp->vblank, i, pclass->vblank,
					chan, &chan->vblank.event[i]);
		if (ret)
			return ret;
	}

	chan->vblank.channel = nv_gpuobj(parent->parent)->addr >> 12;
	return 0;
}

static struct nv50_software_cclass
nv50_software_cclass = {
	.base.handle = NV_ENGCTX(SW, 0x50),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_software_context_ctor,
		.dtor = _nouveau_software_context_dtor,
		.init = _nouveau_software_context_init,
		.fini = _nouveau_software_context_fini,
	},
	.vblank = nv50_software_vblsem_release,
};

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

int
nv50_software_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		   struct nouveau_oclass *oclass, void *data, u32 size,
		   struct nouveau_object **pobject)
{
	struct nv50_software_oclass *pclass = (void *)oclass;
	struct nv50_software_priv *priv;
	int ret;

	ret = nouveau_software_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = pclass->cclass;
	nv_engine(priv)->sclass = pclass->sclass;
	nv_subdev(priv)->intr = nv04_software_intr;
	return 0;
}

struct nouveau_oclass *
nv50_software_oclass = &(struct nv50_software_oclass) {
	.base.handle = NV_ENGINE(SW, 0x50),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_software_ctor,
		.dtor = _nouveau_software_dtor,
		.init = _nouveau_software_init,
		.fini = _nouveau_software_fini,
	},
	.cclass = &nv50_software_cclass.base,
	.sclass =  nv50_software_sclass,
}.base;
