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

#include <core/notify.h>
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
		nvkm_event_send(&chan->event, 1, 0, NULL, 0);
		return true;
	default:
		if (chan->func->mthd)
			return chan->func->mthd(chan, subc, mthd, data);
		break;
	}
	return false;
}

static int
nvkm_sw_chan_event_ctor(struct nvkm_object *object, void *data, u32 size,
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

static const struct nvkm_event_func
nvkm_sw_chan_event = {
	.ctor = nvkm_sw_chan_event_ctor,
};

void
nvkm_sw_chan_dtor(struct nvkm_object *base)
{
	struct nvkm_sw_chan *chan = (void *)base;
	list_del(&chan->head);
	nvkm_event_fini(&chan->event);
	nvkm_engctx_destroy(&chan->base);
}

int
nvkm_sw_chan_ctor(const struct nvkm_sw_chan_func *func,
		  struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_sw *sw = (void *)engine;
	struct nvkm_sw_chan *chan;
	int ret;

	ret = nvkm_engctx_create_(parent, engine, oclass, parent,
				  0, 0, 0, length, pobject);
	chan = *pobject;
	if (ret)
		return ret;

	chan->func = func;
	chan->fifo = nvkm_fifo_chan(parent);
	list_add(&chan->head, &sw->chan);

	return nvkm_event_init(&nvkm_sw_chan_event, 1, 1, &chan->event);
}
