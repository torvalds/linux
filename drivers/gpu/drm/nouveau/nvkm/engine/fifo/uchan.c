/*
 * Copyright 2021 Red Hat Inc.
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
 */
#define nvkm_uchan(p) container_of((p), struct nvkm_uchan, object)
#include "cgrp.h"
#include "chan.h"

#include <core/oproxy.h>

#include <nvif/if0020.h>

#include "gk104.h"

struct nvkm_uchan {
	struct nvkm_object object;
	struct nvkm_chan *chan;
};

static int
nvkm_uchan_uevent(struct nvkm_object *object, void *argv, u32 argc, struct nvkm_uevent *uevent)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;
	union nvif_chan_event_args *args = argv;

	if (!uevent)
		return 0;
	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	switch (args->v0.type) {
	case NVIF_CHAN_EVENT_V0_NON_STALL_INTR:
	case NVIF_CHAN_EVENT_V0_KILLED:
		return chan->object.func->uevent(&chan->object, argv, argc, uevent);
	default:
		break;
	}

	return -ENOSYS;
}

struct nvkm_uobj {
	struct nvkm_oproxy oproxy;
	struct nvkm_chan *chan;
};

static const struct nvkm_oproxy_func
nvkm_uchan_object = {
};

static int
nvkm_uchan_object_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		      struct nvkm_object **pobject)
{
	struct nvkm_chan *chan = nvkm_uchan(oclass->parent)->chan;
	struct nvkm_uobj *uobj;
	struct nvkm_oclass _oclass;

	if (!(uobj = kzalloc(sizeof(*uobj), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_oproxy_ctor(&nvkm_uchan_object, oclass, &uobj->oproxy);
	uobj->chan = chan;
	*pobject = &uobj->oproxy.base;

	_oclass = *oclass;
	_oclass.parent = &chan->object;
	return nvkm_fifo_chan_child_new(&_oclass, argv, argc, &uobj->oproxy.object);
}

static int
nvkm_uchan_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *oclass)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;
	int ret;

	ret = chan->object.func->sclass(&chan->object, index, oclass);
	if (ret)
		return ret;

	oclass->ctor = nvkm_uchan_object_new;
	return 0;
}

static int
nvkm_uchan_map(struct nvkm_object *object, void *argv, u32 argc,
	       enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;

	return chan->object.func->map(&chan->object, argv, argc, type, addr, size);
}

static int
nvkm_uchan_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;
	int ret;

	ret = chan->object.func->fini(&chan->object, suspend);
	if (ret && suspend)
		return ret;

	return 0;
}

static int
nvkm_uchan_init(struct nvkm_object *object)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;

	return chan->object.func->init(&chan->object);
}

static void *
nvkm_uchan_dtor(struct nvkm_object *object)
{
	struct nvkm_uchan *uchan = nvkm_uchan(object);

	nvkm_chan_del(&uchan->chan);
	return uchan;
}

static const struct nvkm_object_func
nvkm_uchan = {
	.dtor = nvkm_uchan_dtor,
	.init = nvkm_uchan_init,
	.fini = nvkm_uchan_fini,
	.map = nvkm_uchan_map,
	.sclass = nvkm_uchan_sclass,
	.uevent = nvkm_uchan_uevent,
};

int
nvkm_uchan_new(struct nvkm_fifo *fifo, struct nvkm_cgrp *cgrp, const struct nvkm_oclass *oclass,
	       void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_object *object = NULL;
	struct nvkm_uchan *uchan;
	int ret;

	if (!(uchan = kzalloc(sizeof(*uchan), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_uchan, oclass, &uchan->object);
	*pobject = &uchan->object;

	if (!fifo->func->chan.func)
		ret = gk104_fifo(fifo)->func->chan.ctor(gk104_fifo(fifo), oclass, argv, argc, &object);
	else
		ret = fifo->func->chan.oclass->ctor(fifo, oclass, argv, argc, &object);
	if (!object)
		return ret;

	uchan->chan = container_of(object, typeof(*uchan->chan), object);
	return ret;
}
