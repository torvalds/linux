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

#include <core/client.h>
#include <core/ramht.h>
#include <engine/dma.h>

#include <nvif/cl507d.h>
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
nv50_disp_chan_mthd(struct nv50_disp_chan *chan, int debug)
{
	struct nv50_disp *disp = chan->root->disp;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	const struct nv50_disp_chan_mthd *mthd = chan->mthd;
	const struct nv50_disp_mthd_list *list;
	int i, j;

	if (debug > subdev->debug)
		return;

	for (i = 0; (list = mthd->data[i].mthd) != NULL; i++) {
		u32 base = chan->head * mthd->addr;
		for (j = 0; j < mthd->data[i].nr; j++, base += list->addr) {
			const char *cname = mthd->name;
			const char *sname = "";
			char cname_[16], sname_[16];

			if (mthd->addr) {
				snprintf(cname_, sizeof(cname_), "%s %d",
					 mthd->name, chan->chid.user);
				cname = cname_;
			}

			if (mthd->data[i].nr > 1) {
				snprintf(sname_, sizeof(sname_), " - %s %d",
					 mthd->data[i].name, j);
				sname = sname_;
			}

			nvkm_printk_(subdev, debug, info, "%s%s:\n", cname, sname);
			nv50_disp_mthd_list(disp, debug, base, mthd->prev,
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
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	union {
		struct nvif_notify_uevent_req none;
	} *args = data;
	int ret = -ENOSYS;

	if (!(ret = nvif_unvers(ret, &data, &size, args->none))) {
		notify->size  = sizeof(struct nvif_notify_uevent_rep);
		notify->types = 1;
		notify->index = chan->chid.user;
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

static int
nv50_disp_chan_rd32(struct nvkm_object *object, u64 addr, u32 *data)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	struct nv50_disp *disp = chan->root->disp;
	struct nvkm_device *device = disp->base.engine.subdev.device;
	*data = nvkm_rd32(device, 0x640000 + (chan->chid.user * 0x1000) + addr);
	return 0;
}

static int
nv50_disp_chan_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	struct nv50_disp *disp = chan->root->disp;
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_wr32(device, 0x640000 + (chan->chid.user * 0x1000) + addr, data);
	return 0;
}

static int
nv50_disp_chan_ntfy(struct nvkm_object *object, u32 type,
		    struct nvkm_event **pevent)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	struct nv50_disp *disp = chan->root->disp;
	switch (type) {
	case NV50_DISP_CORE_CHANNEL_DMA_V0_NTFY_UEVENT:
		*pevent = &disp->uevent;
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

static int
nv50_disp_chan_map(struct nvkm_object *object, u64 *addr, u32 *size)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	struct nv50_disp *disp = chan->root->disp;
	struct nvkm_device *device = disp->base.engine.subdev.device;
	*addr = device->func->resource_addr(device, 0) +
		0x640000 + (chan->chid.user * 0x1000);
	*size = 0x001000;
	return 0;
}

static int
nv50_disp_chan_child_new(const struct nvkm_oclass *oclass,
			 void *data, u32 size, struct nvkm_object **pobject)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(oclass->parent);
	return chan->func->child_new(chan, oclass, data, size, pobject);
}

static int
nv50_disp_chan_child_get(struct nvkm_object *object, int index,
			 struct nvkm_oclass *oclass)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	if (chan->func->child_get) {
		int ret = chan->func->child_get(chan, index, oclass);
		if (ret == 0)
			oclass->ctor = nv50_disp_chan_child_new;
		return ret;
	}
	return -EINVAL;
}

static int
nv50_disp_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	chan->func->fini(chan);
	return 0;
}

static int
nv50_disp_chan_init(struct nvkm_object *object)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	return chan->func->init(chan);
}

static void *
nv50_disp_chan_dtor(struct nvkm_object *object)
{
	struct nv50_disp_chan *chan = nv50_disp_chan(object);
	struct nv50_disp *disp = chan->root->disp;
	if (chan->chid.user >= 0)
		disp->chan[chan->chid.user] = NULL;
	return chan->func->dtor ? chan->func->dtor(chan) : chan;
}

static const struct nvkm_object_func
nv50_disp_chan = {
	.dtor = nv50_disp_chan_dtor,
	.init = nv50_disp_chan_init,
	.fini = nv50_disp_chan_fini,
	.rd32 = nv50_disp_chan_rd32,
	.wr32 = nv50_disp_chan_wr32,
	.ntfy = nv50_disp_chan_ntfy,
	.map = nv50_disp_chan_map,
	.sclass = nv50_disp_chan_child_get,
};

int
nv50_disp_chan_ctor(const struct nv50_disp_chan_func *func,
		    const struct nv50_disp_chan_mthd *mthd,
		    struct nv50_disp_root *root, int ctrl, int user, int head,
		    const struct nvkm_oclass *oclass,
		    struct nv50_disp_chan *chan)
{
	struct nv50_disp *disp = root->disp;

	nvkm_object_ctor(&nv50_disp_chan, oclass, &chan->object);
	chan->func = func;
	chan->mthd = mthd;
	chan->root = root;
	chan->chid.ctrl = ctrl;
	chan->chid.user = user;
	chan->head = head;

	if (disp->chan[chan->chid.user]) {
		chan->chid.user = -1;
		return -EBUSY;
	}
	disp->chan[chan->chid.user] = chan;
	return 0;
}

int
nv50_disp_chan_new_(const struct nv50_disp_chan_func *func,
		    const struct nv50_disp_chan_mthd *mthd,
		    struct nv50_disp_root *root, int ctrl, int user, int head,
		    const struct nvkm_oclass *oclass,
		    struct nvkm_object **pobject)
{
	struct nv50_disp_chan *chan;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->object;

	return nv50_disp_chan_ctor(func, mthd, root, ctrl, user,
				   head, oclass, chan);
}
