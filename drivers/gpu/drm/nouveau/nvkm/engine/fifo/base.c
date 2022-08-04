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
#include "chan.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <core/notify.h>
#include <subdev/mc.h>

#include <nvif/event.h>
#include <nvif/cl0080.h>
#include <nvif/unpack.h>

void
nvkm_fifo_recover_chan(struct nvkm_fifo *fifo, int chid)
{
	unsigned long flags;
	if (WARN_ON(!fifo->func->recover_chan))
		return;
	spin_lock_irqsave(&fifo->lock, flags);
	fifo->func->recover_chan(fifo, chid);
	spin_unlock_irqrestore(&fifo->lock, flags);
}

void
nvkm_fifo_pause(struct nvkm_fifo *fifo, unsigned long *flags)
{
	return fifo->func->pause(fifo, flags);
}

void
nvkm_fifo_start(struct nvkm_fifo *fifo, unsigned long *flags)
{
	return fifo->func->start(fifo, flags);
}

void
nvkm_fifo_fault(struct nvkm_fifo *fifo, struct nvkm_fault_data *info)
{
	return fifo->func->fault(fifo, info);
}

void
nvkm_fifo_chan_put(struct nvkm_fifo *fifo, unsigned long flags,
		   struct nvkm_fifo_chan **pchan)
{
	struct nvkm_fifo_chan *chan = *pchan;
	if (likely(chan)) {
		*pchan = NULL;
		spin_unlock_irqrestore(&fifo->lock, flags);
	}
}

struct nvkm_fifo_chan *
nvkm_fifo_chan_inst_locked(struct nvkm_fifo *fifo, u64 inst)
{
	struct nvkm_fifo_chan *chan;
	list_for_each_entry(chan, &fifo->chan, head) {
		if (chan->inst->addr == inst) {
			list_del(&chan->head);
			list_add(&chan->head, &fifo->chan);
			return chan;
		}
	}
	return NULL;
}

struct nvkm_fifo_chan *
nvkm_fifo_chan_inst(struct nvkm_fifo *fifo, u64 inst, unsigned long *rflags)
{
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	spin_lock_irqsave(&fifo->lock, flags);
	if ((chan = nvkm_fifo_chan_inst_locked(fifo, inst))) {
		*rflags = flags;
		return chan;
	}
	spin_unlock_irqrestore(&fifo->lock, flags);
	return NULL;
}

struct nvkm_fifo_chan *
nvkm_fifo_chan_chid(struct nvkm_fifo *fifo, int chid, unsigned long *rflags)
{
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	spin_lock_irqsave(&fifo->lock, flags);
	list_for_each_entry(chan, &fifo->chan, head) {
		if (chan->chid == chid) {
			list_del(&chan->head);
			list_add(&chan->head, &fifo->chan);
			*rflags = flags;
			return chan;
		}
	}
	spin_unlock_irqrestore(&fifo->lock, flags);
	return NULL;
}

void
nvkm_fifo_kevent(struct nvkm_fifo *fifo, int chid)
{
	nvkm_event_send(&fifo->kevent, 1, chid, NULL, 0);
}

static int
nvkm_fifo_kevent_ctor(struct nvkm_object *object, void *data, u32 size,
		      struct nvkm_notify *notify)
{
	struct nvkm_fifo_chan *chan = nvkm_fifo_chan(object);
	if (size == 0) {
		notify->size  = 0;
		notify->types = 1;
		notify->index = chan->chid;
		return 0;
	}
	return -ENOSYS;
}

static const struct nvkm_event_func
nvkm_fifo_kevent_func = {
	.ctor = nvkm_fifo_kevent_ctor,
};

static void
nvkm_fifo_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	fifo->func->uevent_fini(fifo);
}

static void
nvkm_fifo_uevent_init(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	fifo->func->uevent_init(fifo);
}

static int
nvkm_fifo_uevent_ctor(struct nvkm_object *object, void *data, u32 size,
		      struct nvkm_notify *notify)
{
	union {
		struct nvif_notify_uevent_req none;
	} *req = data;
	int ret = -ENOSYS;

	if (!(ret = nvif_unvers(ret, &data, &size, req->none))) {
		notify->size  = sizeof(struct nvif_notify_uevent_rep);
		notify->types = 1;
		notify->index = 0;
	}

	return ret;
}

static const struct nvkm_event_func
nvkm_fifo_uevent_func = {
	.ctor = nvkm_fifo_uevent_ctor,
	.init = nvkm_fifo_uevent_init,
	.fini = nvkm_fifo_uevent_fini,
};

void
nvkm_fifo_uevent(struct nvkm_fifo *fifo)
{
	struct nvif_notify_uevent_rep rep = {
	};
	nvkm_event_send(&fifo->uevent, 1, 0, &rep, sizeof(rep));
}

static int
nvkm_fifo_class_new_(struct nvkm_device *device,
		     const struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nvkm_fifo *fifo = nvkm_fifo(oclass->engine);
	return fifo->func->class_new(fifo, oclass, data, size, pobject);
}

static const struct nvkm_device_oclass
nvkm_fifo_class_ = {
	.ctor = nvkm_fifo_class_new_,
};

static int
nvkm_fifo_class_new(struct nvkm_device *device,
		    const struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	const struct nvkm_fifo_chan_oclass *sclass = oclass->engn;
	struct nvkm_fifo *fifo = nvkm_fifo(oclass->engine);
	return sclass->ctor(fifo, oclass, data, size, pobject);
}

static const struct nvkm_device_oclass
nvkm_fifo_class = {
	.ctor = nvkm_fifo_class_new,
};

static int
nvkm_fifo_class_get(struct nvkm_oclass *oclass, int index,
		    const struct nvkm_device_oclass **class)
{
	struct nvkm_fifo *fifo = nvkm_fifo(oclass->engine);
	const struct nvkm_fifo_chan_oclass *sclass;
	int c = 0;

	if (fifo->func->class_get) {
		int ret = fifo->func->class_get(fifo, index, oclass);
		if (ret == 0)
			*class = &nvkm_fifo_class_;
		return ret;
	}

	while ((sclass = fifo->func->chan[c])) {
		if (c++ == index) {
			oclass->base = sclass->base;
			oclass->engn = sclass;
			*class = &nvkm_fifo_class;
			return 0;
		}
	}

	return c;
}

static void
nvkm_fifo_intr(struct nvkm_engine *engine)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	fifo->func->intr(fifo);
}

static int
nvkm_fifo_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	if (fifo->func->fini)
		fifo->func->fini(fifo);
	return 0;
}

static int
nvkm_fifo_info(struct nvkm_engine *engine, u64 mthd, u64 *data)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	switch (mthd) {
	case NV_DEVICE_HOST_CHANNELS: *data = fifo->nr; return 0;
	default:
		if (fifo->func->info)
			return fifo->func->info(fifo, mthd, data);
		break;
	}
	return -ENOSYS;
}

static int
nvkm_fifo_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	if (fifo->func->oneinit)
		return fifo->func->oneinit(fifo);
	return 0;
}

static void
nvkm_fifo_preinit(struct nvkm_engine *engine)
{
	nvkm_mc_reset(engine->subdev.device, NVKM_ENGINE_FIFO, 0);
}

static int
nvkm_fifo_init(struct nvkm_engine *engine)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	fifo->func->init(fifo);
	return 0;
}

static void *
nvkm_fifo_dtor(struct nvkm_engine *engine)
{
	struct nvkm_fifo *fifo = nvkm_fifo(engine);
	void *data = fifo;
	if (fifo->func->dtor)
		data = fifo->func->dtor(fifo);
	nvkm_event_fini(&fifo->kevent);
	nvkm_event_fini(&fifo->uevent);
	mutex_destroy(&fifo->mutex);
	return data;
}

static const struct nvkm_engine_func
nvkm_fifo = {
	.dtor = nvkm_fifo_dtor,
	.preinit = nvkm_fifo_preinit,
	.oneinit = nvkm_fifo_oneinit,
	.info = nvkm_fifo_info,
	.init = nvkm_fifo_init,
	.fini = nvkm_fifo_fini,
	.intr = nvkm_fifo_intr,
	.base.sclass = nvkm_fifo_class_get,
};

int
nvkm_fifo_ctor(const struct nvkm_fifo_func *func, struct nvkm_device *device,
	       enum nvkm_subdev_type type, int inst, int nr, struct nvkm_fifo *fifo)
{
	int ret;

	fifo->func = func;
	INIT_LIST_HEAD(&fifo->chan);
	spin_lock_init(&fifo->lock);
	mutex_init(&fifo->mutex);

	if (WARN_ON(fifo->nr > NVKM_FIFO_CHID_NR))
		fifo->nr = NVKM_FIFO_CHID_NR;
	else
		fifo->nr = nr;
	bitmap_clear(fifo->mask, 0, fifo->nr);

	ret = nvkm_engine_ctor(&nvkm_fifo, device, type, inst, true, &fifo->engine);
	if (ret)
		return ret;

	if (func->uevent_init) {
		ret = nvkm_event_init(&nvkm_fifo_uevent_func, 1, 1,
				      &fifo->uevent);
		if (ret)
			return ret;
	}

	return nvkm_event_init(&nvkm_fifo_kevent_func, 1, nr, &fifo->kevent);
}
