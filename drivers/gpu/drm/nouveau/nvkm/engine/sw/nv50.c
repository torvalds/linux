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

#include <core/handle.h>
#include <engine/disp.h>
#include <engine/fifo/chan.h>
#include <subdev/bar.h>

#include <nvif/event.h>
#include <nvif/ioctl.h>

/*******************************************************************************
 * software object classes
 ******************************************************************************/

static struct nvkm_oclass
nv50_sw_sclass[] = {
	{ NVIF_IOCTL_NEW_V0_SW_NV50, &nvkm_nvsw_ofuncs },
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
	struct nvkm_sw *sw = (void *)nv_object(chan)->engine;
	struct nvkm_device *device = sw->engine.subdev.device;
	struct nvkm_bar *bar = device->bar;

	nvkm_wr32(device, 0x001704, chan->vblank.channel);
	nvkm_wr32(device, 0x001710, 0x80000000 | chan->vblank.ctxdma);
	bar->flush(bar);

	if (nv_device(sw)->chipset == 0x50) {
		nvkm_wr32(device, 0x001570, chan->vblank.offset);
		nvkm_wr32(device, 0x001574, chan->vblank.value);
	} else {
		nvkm_wr32(device, 0x060010, chan->vblank.offset);
		nvkm_wr32(device, 0x060014, chan->vblank.value);
	}

	return NVKM_NOTIFY_DROP;
}

static bool
nv50_sw_chan_mthd(struct nvkm_sw_chan *base, int subc, u32 mthd, u32 data)
{
	struct nv50_sw_chan *chan = nv50_sw_chan(base);
	struct nvkm_engine *engine = chan->base.base.gpuobj.object.engine;
	struct nvkm_device *device = engine->subdev.device;
	switch (mthd) {
	case 0x018c: chan->vblank.ctxdma = data; return true;
	case 0x0400: chan->vblank.offset = data; return true;
	case 0x0404: chan->vblank.value  = data; return true;
	case 0x0408:
		if (data < device->disp->vblank.index_nr) {
			nvkm_notify_get(&chan->vblank.notify[data]);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}

static const struct nvkm_sw_chan_func
nv50_sw_chan_func = {
	.mthd = nv50_sw_chan_mthd,
};

void
nv50_sw_context_dtor(struct nvkm_object *object)
{
	struct nv50_sw_chan *chan = (void *)object;
	int i;

	for (i = 0; i < ARRAY_SIZE(chan->vblank.notify); i++)
		nvkm_notify_fini(&chan->vblank.notify[i]);

	nvkm_sw_chan_dtor(&chan->base.base.gpuobj.object);
}

int
nv50_sw_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = parent->engine->subdev.device->disp;
	struct nv50_sw_cclass *pclass = (void *)oclass;
	struct nv50_sw_chan *chan;
	int ret, i;

	ret = nvkm_sw_context_create(pclass->chan, parent, engine, oclass, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	for (i = 0; disp && i < disp->vblank.index_nr; i++) {
		ret = nvkm_notify_init(NULL, &disp->vblank, pclass->vblank,
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

	chan->vblank.channel = nvkm_fifo_chan(parent)->inst->addr >> 12;
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
	.chan = &nv50_sw_chan_func,
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
	struct nvkm_sw *sw;
	int ret;

	ret = nvkm_sw_create(parent, engine, oclass, &sw);
	*pobject = nv_object(sw);
	if (ret)
		return ret;

	nv_engine(sw)->cclass = pclass->cclass;
	nv_engine(sw)->sclass = pclass->sclass;
	nv_subdev(sw)->intr = nv04_sw_intr;
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
