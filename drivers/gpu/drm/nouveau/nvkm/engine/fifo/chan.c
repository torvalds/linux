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
#include "chan.h"

#include <core/client.h>
#include <engine/dma.h>

#include <nvif/class.h>

int
_nvkm_fifo_channel_ntfy(struct nvkm_object *object, u32 type,
			struct nvkm_event **event)
{
	struct nvkm_fifo *fifo = (void *)object->engine;
	switch (type) {
	case G82_CHANNEL_DMA_V0_NTFY_UEVENT:
		if (nv_mclass(object) >= G82_CHANNEL_DMA) {
			*event = &fifo->uevent;
			return 0;
		}
		break;
	default:
		break;
	}
	return -EINVAL;
}

int
_nvkm_fifo_channel_map(struct nvkm_object *object, u64 *addr, u32 *size)
{
	struct nvkm_fifo_chan *chan = (void *)object;
	*addr = chan->addr;
	*size = chan->size;
	return 0;
}

u32
_nvkm_fifo_channel_rd32(struct nvkm_object *object, u64 addr)
{
	struct nvkm_fifo_chan *chan = (void *)object;
	if (unlikely(!chan->user)) {
		chan->user = ioremap(chan->addr, chan->size);
		if (WARN_ON_ONCE(chan->user == NULL))
			return 0;
	}
	return ioread32_native(chan->user + addr);
}

void
_nvkm_fifo_channel_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	struct nvkm_fifo_chan *chan = (void *)object;
	if (unlikely(!chan->user)) {
		chan->user = ioremap(chan->addr, chan->size);
		if (WARN_ON_ONCE(chan->user == NULL))
			return;
	}
	iowrite32_native(data, chan->user + addr);
}

void
nvkm_fifo_channel_destroy(struct nvkm_fifo_chan *chan)
{
	struct nvkm_fifo *fifo = (void *)nv_object(chan)->engine;
	unsigned long flags;

	if (chan->user)
		iounmap(chan->user);

	spin_lock_irqsave(&fifo->lock, flags);
	fifo->channel[chan->chid] = NULL;
	spin_unlock_irqrestore(&fifo->lock, flags);

	nvkm_gpuobj_del(&chan->pushgpu);
	nvkm_namedb_destroy(&chan->namedb);
}

void
_nvkm_fifo_channel_dtor(struct nvkm_object *object)
{
	struct nvkm_fifo_chan *chan = (void *)object;
	nvkm_fifo_channel_destroy(chan);
}

int
nvkm_fifo_channel_create_(struct nvkm_object *parent,
			  struct nvkm_object *engine,
			  struct nvkm_oclass *oclass,
			  int bar, u32 addr, u32 size, u64 pushbuf,
			  u64 engmask, int len, void **ptr)
{
	struct nvkm_client *client = nvkm_client(parent);
	struct nvkm_fifo *fifo = (void *)engine;
	struct nvkm_fifo_base *base = (void *)parent;
	struct nvkm_fifo_chan *chan;
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_dmaobj *dmaobj;
	unsigned long flags;
	int ret;

	/* create base object class */
	ret = nvkm_namedb_create_(parent, engine, oclass, 0, NULL,
				  engmask, len, ptr);
	chan = *ptr;
	if (ret)
		return ret;

	/* validate dma object representing push buffer */
	if (pushbuf) {
		dmaobj = nvkm_dma_search(device->dma, client, pushbuf);
		if (!dmaobj)
			return -ENOENT;

		ret = nvkm_object_bind(&dmaobj->object, &base->gpuobj, 16,
				       &chan->pushgpu);
		if (ret)
			return ret;
	}

	/* find a free fifo channel */
	spin_lock_irqsave(&fifo->lock, flags);
	for (chan->chid = fifo->min; chan->chid < fifo->max; chan->chid++) {
		if (!fifo->channel[chan->chid]) {
			fifo->channel[chan->chid] = nv_object(chan);
			break;
		}
	}
	spin_unlock_irqrestore(&fifo->lock, flags);

	if (chan->chid == fifo->max) {
		nvkm_error(subdev, "no free channels\n");
		return -ENOSPC;
	}

	chan->addr = nv_device_resource_start(device, bar) +
		     addr + size * chan->chid;
	chan->size = size;
	nvkm_event_send(&fifo->cevent, 1, 0, NULL, 0);
	return 0;
}
