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
#include <core/notify.h>

#include <nvif/event.h>
#include <nvif/unpack.h>

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
nvkm_fifo_chan_inst(struct nvkm_fifo *fifo, u64 inst, unsigned long *rflags)
{
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	spin_lock_irqsave(&fifo->lock, flags);
	list_for_each_entry(chan, &fifo->chan, head) {
		if (chan->inst->addr == inst) {
			list_del(&chan->head);
			list_add(&chan->head, &fifo->chan);
			*rflags = flags;
			return chan;
		}
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

static int
nvkm_fifo_event_ctor(struct nvkm_object *object, void *data, u32 size,
		     struct nvkm_notify *notify)
{
	if (size == 0) {
		notify->size  = 0;
		notify->types = 1;
		notify->index = 0;
		return 0;
	}
	return -ENOSYS;
}

static const struct nvkm_event_func
nvkm_fifo_event_func = {
	.ctor = nvkm_fifo_event_ctor,
};

int
nvkm_fifo_uevent_ctor(struct nvkm_object *object, void *data, u32 size,
		      struct nvkm_notify *notify)
{
	union {
		struct nvif_notify_uevent_req none;
	} *req = data;
	int ret;

	if (nvif_unvers(req->none)) {
		notify->size  = sizeof(struct nvif_notify_uevent_rep);
		notify->types = 1;
		notify->index = 0;
	}

	return ret;
}

void
nvkm_fifo_uevent(struct nvkm_fifo *fifo)
{
	struct nvif_notify_uevent_rep rep = {
	};
	nvkm_event_send(&fifo->uevent, 1, 0, &rep, sizeof(rep));
}

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

void
nvkm_fifo_destroy(struct nvkm_fifo *fifo)
{
	nvkm_event_fini(&fifo->uevent);
	nvkm_event_fini(&fifo->cevent);
	nvkm_engine_destroy(&fifo->engine);
}

static const struct nvkm_engine_func
nvkm_fifo_func = {
	.base.sclass = nvkm_fifo_class_get,
};

int
nvkm_fifo_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass,
		  int min, int max, int length, void **pobject)
{
	struct nvkm_fifo *fifo;
	int  nr = max + 1;
	int cnt = nr - min;
	int ret;

	ret = nvkm_engine_create_(parent, engine, oclass, true, "PFIFO",
				  "fifo", length, pobject);
	fifo = *pobject;
	if (ret)
		return ret;

	fifo->engine.func = &nvkm_fifo_func;
	INIT_LIST_HEAD(&fifo->chan);

	fifo->nr = nr;
	if (WARN_ON(fifo->nr > NVKM_FIFO_CHID_NR)) {
		fifo->nr = NVKM_FIFO_CHID_NR;
		cnt = fifo->nr - min;
	}
	bitmap_fill(fifo->mask, NVKM_FIFO_CHID_NR);
	bitmap_clear(fifo->mask, min, cnt);

	ret = nvkm_event_init(&nvkm_fifo_event_func, 1, 1, &fifo->cevent);
	if (ret)
		return ret;

	spin_lock_init(&fifo->lock);
	return 0;
}
