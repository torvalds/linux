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

#include <core/gpuobj.h>
#include <engine/disp.h>
#include <engine/fifo/chan.h>
#include <subdev/bar.h>

#include <nvif/class.h>
#include <nvif/event.h>

/*******************************************************************************
 * software context
 ******************************************************************************/

static int
nv50_sw_chan_vblsem_release(struct nvkm_event_ntfy *notify, u32 bits)
{
	struct nv50_sw_chan *chan =
		container_of(notify, typeof(*chan), vblank.notify[notify->id]);
	struct nvkm_sw *sw = chan->base.sw;
	struct nvkm_device *device = sw->engine.subdev.device;

	nvkm_wr32(device, 0x001704, chan->base.fifo->inst->addr >> 12);
	nvkm_wr32(device, 0x001710, 0x80000000 | chan->vblank.ctxdma);
	nvkm_bar_flush(device->bar);

	if (device->chipset == 0x50) {
		nvkm_wr32(device, 0x001570, chan->vblank.offset);
		nvkm_wr32(device, 0x001574, chan->vblank.value);
	} else {
		nvkm_wr32(device, 0x060010, chan->vblank.offset);
		nvkm_wr32(device, 0x060014, chan->vblank.value);
	}

	return NVKM_EVENT_DROP;
}

static bool
nv50_sw_chan_mthd(struct nvkm_sw_chan *base, int subc, u32 mthd, u32 data)
{
	struct nv50_sw_chan *chan = nv50_sw_chan(base);
	struct nvkm_engine *engine = chan->base.object.engine;
	struct nvkm_device *device = engine->subdev.device;
	switch (mthd) {
	case 0x018c: chan->vblank.ctxdma = data; return true;
	case 0x0400: chan->vblank.offset = data; return true;
	case 0x0404: chan->vblank.value  = data; return true;
	case 0x0408:
		if (data < device->disp->vblank.index_nr) {
			nvkm_event_ntfy_allow(&chan->vblank.notify[data]);
			return true;
		}
		break;
	default:
		break;
	}
	return false;
}

void *
nv50_sw_chan_dtor(struct nvkm_sw_chan *base)
{
	struct nv50_sw_chan *chan = nv50_sw_chan(base);
	int i;

	for (i = 0; i < ARRAY_SIZE(chan->vblank.notify); i++)
		nvkm_event_ntfy_del(&chan->vblank.notify[i]);

	return chan;
}

static const struct nvkm_sw_chan_func
nv50_sw_chan = {
	.dtor = nv50_sw_chan_dtor,
	.mthd = nv50_sw_chan_mthd,
};

static int
nv50_sw_chan_new(struct nvkm_sw *sw, struct nvkm_chan *fifoch,
		 const struct nvkm_oclass *oclass, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = sw->engine.subdev.device->disp;
	struct nv50_sw_chan *chan;
	int ret, i;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;

	ret = nvkm_sw_chan_ctor(&nv50_sw_chan, sw, fifoch, oclass, &chan->base);
	if (ret)
		return ret;

	for (i = 0; disp && i < disp->vblank.index_nr; i++) {
		nvkm_event_ntfy_add(&disp->vblank, i, NVKM_DISP_HEAD_EVENT_VBLANK, true,
				    nv50_sw_chan_vblsem_release, &chan->vblank.notify[i]);
	}

	return 0;
}

/*******************************************************************************
 * software engine/subdev functions
 ******************************************************************************/

static const struct nvkm_sw_func
nv50_sw = {
	.chan_new = nv50_sw_chan_new,
	.sclass = {
		{ nvkm_nvsw_new, { -1, -1, NVIF_CLASS_SW_NV50 } },
		{}
	}
};

int
nv50_sw_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_sw **psw)
{
	return nvkm_sw_new_(&nv50_sw, device, type, inst, psw);
}
