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
#include "channv50.h"
#include "rootnv50.h"

#include <nvif/class.h>
#include <nvif/event.h>
#include <nvif/unpack.h>

static void
nv50_disp_mthd_list(struct nv50_disp *disp, int debug, u32 base, int c,
		    const struct nv50_disp_mthd_list *list, int inst)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int i;

	for (i = 0; list->data[i].mthd; i++) {
		if (list->data[i].addr) {
			u32 next = nvkm_rd32(device, list->data[i].addr + base + 0);
			u32 prev = nvkm_rd32(device, list->data[i].addr + base + c);
			u32 mthd = list->data[i].mthd + (list->mthd * inst);
			const char *name = list->data[i].name;
			char mods[16];

			if (prev != next)
				snprintf(mods, sizeof(mods), "-> %08x", next);
			else
				snprintf(mods, sizeof(mods), "%13c", ' ');

			nvkm_printk_(subdev, debug, info,
				     "\t%04x: %08x %s%s%s\n",
				     mthd, prev, mods, name ? " // " : "",
				     name ? name : "");
		}
	}
}

void
nv50_disp_mthd_chan(struct nv50_disp *disp, int debug, int head,
		    const struct nv50_disp_mthd_chan *chan)
{
	struct nvkm_object *object = nv_object(disp);
	const struct nv50_disp_impl *impl = (void *)object->oclass;
	const struct nv50_disp_mthd_list *list;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	int i, j;

	if (debug > nv_subdev(disp)->debug)
		return;

	for (i = 0; (list = chan->data[i].mthd) != NULL; i++) {
		u32 base = head * chan->addr;
		for (j = 0; j < chan->data[i].nr; j++, base += list->addr) {
			const char *cname = chan->name;
			const char *sname = "";
			char cname_[16], sname_[16];

			if (chan->addr) {
				snprintf(cname_, sizeof(cname_), "%s %d",
					 chan->name, head);
				cname = cname_;
			}

			if (chan->data[i].nr > 1) {
				snprintf(sname_, sizeof(sname_), " - %s %d",
					 chan->data[i].name, j);
				sname = sname_;
			}

			nvkm_printk_(subdev, debug, info, "%s%s:\n", cname, sname);
			nv50_disp_mthd_list(disp, debug, base, impl->mthd.prev,
					    list, j);
		}
	}
}

static void
nv50_disp_chan_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nv50_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_mask(device, 0x610028, 0x00000001 << index, 0x00000000 << index);
	nvkm_wr32(device, 0x610020, 0x00000001 << index);
}

static void
nv50_disp_chan_uevent_init(struct nvkm_event *event, int types, int index)
{
	struct nv50_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_wr32(device, 0x610020, 0x00000001 << index);
	nvkm_mask(device, 0x610028, 0x00000001 << index, 0x00000001 << index);
}

void
nv50_disp_chan_uevent_send(struct nv50_disp *disp, int chid)
{
	struct nvif_notify_uevent_rep {
	} rep;

	nvkm_event_send(&disp->uevent, 1, chid, &rep, sizeof(rep));
}

int
nv50_disp_chan_uevent_ctor(struct nvkm_object *object, void *data, u32 size,
			   struct nvkm_notify *notify)
{
	struct nv50_disp_dmac *dmac = (void *)object;
	union {
		struct nvif_notify_uevent_req none;
	} *args = data;
	int ret;

	if (nvif_unvers(args->none)) {
		notify->size  = sizeof(struct nvif_notify_uevent_rep);
		notify->types = 1;
		notify->index = dmac->base.chid;
		return 0;
	}

	return ret;
}

const struct nvkm_event_func
nv50_disp_chan_uevent = {
	.ctor = nv50_disp_chan_uevent_ctor,
	.init = nv50_disp_chan_uevent_init,
	.fini = nv50_disp_chan_uevent_fini,
};

int
nv50_disp_chan_ntfy(struct nvkm_object *object, u32 type,
		    struct nvkm_event **pevent)
{
	struct nv50_disp *disp = (void *)object->engine;
	switch (type) {
	case NV50_DISP_CORE_CHANNEL_DMA_V0_NTFY_UEVENT:
		*pevent = &disp->uevent;
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

int
nv50_disp_chan_map(struct nvkm_object *object, u64 *addr, u32 *size)
{
	struct nv50_disp_chan *chan = (void *)object;
	*addr = nv_device_resource_start(nv_device(object), 0) +
		0x640000 + (chan->chid * 0x1000);
	*size = 0x001000;
	return 0;
}

u32
nv50_disp_chan_rd32(struct nvkm_object *object, u64 addr)
{
	struct nv50_disp_chan *chan = (void *)object;
	struct nvkm_device *device = object->engine->subdev.device;
	return nvkm_rd32(device, 0x640000 + (chan->chid * 0x1000) + addr);
}

void
nv50_disp_chan_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	struct nv50_disp_chan *chan = (void *)object;
	struct nvkm_device *device = object->engine->subdev.device;
	nvkm_wr32(device, 0x640000 + (chan->chid * 0x1000) + addr, data);
}

void
nv50_disp_chan_destroy(struct nv50_disp_chan *chan)
{
	struct nv50_disp_root *root = (void *)nv_object(chan)->parent;
	root->chan &= ~(1 << chan->chid);
	nvkm_namedb_destroy(&chan->base);
}

int
nv50_disp_chan_create_(struct nvkm_object *parent,
		       struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, int head,
		       int length, void **pobject)
{
	const struct nv50_disp_chan_impl *impl = (void *)oclass->ofuncs;
	struct nv50_disp_root *root = (void *)parent;
	struct nv50_disp_chan *chan;
	int chid = impl->chid + head;
	int ret;

	if (root->chan & (1 << chid))
		return -EBUSY;
	root->chan |= (1 << chid);

	ret = nvkm_namedb_create_(parent, engine, oclass, 0, NULL,
				  (1ULL << NVDEV_ENGINE_DMAOBJ),
				  length, pobject);
	chan = *pobject;
	if (ret)
		return ret;
	chan->chid = chid;

	nv_parent(chan)->object_attach = impl->attach;
	nv_parent(chan)->object_detach = impl->detach;
	return 0;
}
