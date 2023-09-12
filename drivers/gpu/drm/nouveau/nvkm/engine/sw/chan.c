/*
 * Copyright 2015 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "chan.h"

#include <engine/fifo.h>

#include <nvif/event.h>
#include <nvif/unpack.h>

bool
nvkm_sw_chan_mthd(struct nvkm_sw_chan *chan, int subc, u32 mthd, u32 data)
{
	switch (mthd) {
	case 0x0000:
		return true;
	case 0x0500:
		nvkm_event_ntfy(&chan->event, 0, NVKM_SW_CHAN_EVENT_PAGE_FLIP);
		return true;
	default:
		if (chan->func->mthd)
			return chan->func->mthd(chan, subc, mthd, data);
		break;
	}
	return false;
}

static const struct nvkm_event_func
nvkm_sw_chan_event = {
};

static void *
nvkm_sw_chan_dtor(struct nvkm_object *object)
{
	struct nvkm_sw_chan *chan = nvkm_sw_chan(object);
	struct nvkm_sw *sw = chan->sw;
	unsigned long flags;
	void *data = chan;

	if (chan->func->dtor)
		data = chan->func->dtor(chan);
	nvkm_event_fini(&chan->event);

	spin_lock_irqsave(&sw->engine.lock, flags);
	list_del(&chan->head);
	spin_unlock_irqrestore(&sw->engine.lock, flags);
	return data;
}

static const struct nvkm_object_func
nvkm_sw_chan = {
	.dtor = nvkm_sw_chan_dtor,
};

int
nvkm_sw_chan_ctor(const struct nvkm_sw_chan_func *func, struct nvkm_sw *sw,
		  struct nvkm_chan *fifo, const struct nvkm_oclass *oclass,
		  struct nvkm_sw_chan *chan)
{
	unsigned long flags;

	nvkm_object_ctor(&nvkm_sw_chan, oclass, &chan->object);
	chan->func = func;
	chan->sw = sw;
	chan->fifo = fifo;
	spin_lock_irqsave(&sw->engine.lock, flags);
	list_add(&chan->head, &sw->chan);
	spin_unlock_irqrestore(&sw->engine.lock, flags);

	return nvkm_event_init(&nvkm_sw_chan_event, &sw->engine.subdev, 1, 1, &chan->event);
}
