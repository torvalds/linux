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
#include "chan.h"

#include <core/oproxy.h>
#include <core/ramht.h>

#include <nvif/if0014.h>

static int
nvkm_disp_chan_ntfy(struct nvkm_object *object, u32 type, struct nvkm_event **pevent)
{
	struct nvkm_disp_chan *chan = nvkm_disp_chan(object);
	struct nvkm_disp *disp = chan->disp;

	switch (type) {
	case 0:
		*pevent = &disp->uevent;
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

static int
nvkm_disp_chan_map(struct nvkm_object *object, void *argv, u32 argc,
		   enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct nvkm_disp_chan *chan = nvkm_disp_chan(object);
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u64 base = device->func->resource_addr(device, 0);

	*type = NVKM_OBJECT_MAP_IO;
	*addr = base + chan->func->user(chan, size);
	return 0;
}

struct nvkm_disp_chan_object {
	struct nvkm_oproxy oproxy;
	struct nvkm_disp *disp;
	int hash;
};

static void
nvkm_disp_chan_child_del_(struct nvkm_oproxy *base)
{
	struct nvkm_disp_chan_object *object = container_of(base, typeof(*object), oproxy);

	nvkm_ramht_remove(object->disp->ramht, object->hash);
}

static const struct nvkm_oproxy_func
nvkm_disp_chan_child_func_ = {
	.dtor[0] = nvkm_disp_chan_child_del_,
};

static int
nvkm_disp_chan_child_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
			 struct nvkm_object **pobject)
{
	struct nvkm_disp_chan *chan = nvkm_disp_chan(oclass->parent);
	struct nvkm_disp *disp = chan->disp;
	struct nvkm_device *device = disp->engine.subdev.device;
	const struct nvkm_device_oclass *sclass = oclass->priv;
	struct nvkm_disp_chan_object *object;
	int ret;

	if (!(object = kzalloc(sizeof(*object), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_oproxy_ctor(&nvkm_disp_chan_child_func_, oclass, &object->oproxy);
	object->disp = disp;
	*pobject = &object->oproxy.base;

	ret = sclass->ctor(device, oclass, argv, argc, &object->oproxy.object);
	if (ret)
		return ret;

	object->hash = chan->func->bind(chan, object->oproxy.object, oclass->handle);
	if (object->hash < 0)
		return object->hash;

	return 0;
}

static int
nvkm_disp_chan_child_get(struct nvkm_object *object, int index, struct nvkm_oclass *sclass)
{
	struct nvkm_disp_chan *chan = nvkm_disp_chan(object);
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const struct nvkm_device_oclass *oclass = NULL;

	if (chan->func->bind)
		sclass->engine = nvkm_device_engine(device, NVKM_ENGINE_DMAOBJ, 0);
	else
		sclass->engine = NULL;

	if (sclass->engine && sclass->engine->func->base.sclass) {
		sclass->engine->func->base.sclass(sclass, index, &oclass);
		if (oclass) {
			sclass->ctor = nvkm_disp_chan_child_new;
			sclass->priv = oclass;
			return 0;
		}
	}

	return -EINVAL;
}

static int
nvkm_disp_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_disp_chan *chan = nvkm_disp_chan(object);

	chan->func->fini(chan);
	chan->func->intr(chan, false);
	return 0;
}

static int
nvkm_disp_chan_init(struct nvkm_object *object)
{
	struct nvkm_disp_chan *chan = nvkm_disp_chan(object);

	chan->func->intr(chan, true);
	return chan->func->init(chan);
}

static void *
nvkm_disp_chan_dtor(struct nvkm_object *object)
{
	struct nvkm_disp_chan *chan = nvkm_disp_chan(object);
	struct nvkm_disp *disp = chan->disp;

	spin_lock(&disp->client.lock);
	if (disp->chan[chan->chid.user] == chan)
		disp->chan[chan->chid.user] = NULL;
	spin_unlock(&disp->client.lock);

	nvkm_memory_unref(&chan->memory);
	return chan;
}

static const struct nvkm_object_func
nvkm_disp_chan = {
	.dtor = nvkm_disp_chan_dtor,
	.init = nvkm_disp_chan_init,
	.fini = nvkm_disp_chan_fini,
	.ntfy = nvkm_disp_chan_ntfy,
	.map = nvkm_disp_chan_map,
	.sclass = nvkm_disp_chan_child_get,
};

static int
nvkm_disp_chan_new_(struct nvkm_disp *disp, int nr, const struct nvkm_oclass *oclass,
		    void *argv, u32 argc, struct nvkm_object **pobject)
{
	const struct nvkm_disp_chan_user *user = NULL;
	struct nvkm_disp_chan *chan;
	union nvif_disp_chan_args *args = argv;
	int ret, i;

	for (i = 0; disp->func->user[i].ctor; i++) {
		if (disp->func->user[i].base.oclass == oclass->base.oclass) {
			user = disp->func->user[i].chan;
			break;
		}
	}

	if (WARN_ON(!user))
		return -EINVAL;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (args->v0.id >= nr || !args->v0.pushbuf != !user->func->push)
		return -EINVAL;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->object;

	nvkm_object_ctor(&nvkm_disp_chan, oclass, &chan->object);
	chan->func = user->func;
	chan->mthd = user->mthd;
	chan->disp = disp;
	chan->chid.ctrl = user->ctrl + args->v0.id;
	chan->chid.user = user->user + args->v0.id;
	chan->head = args->v0.id;

	if (chan->func->push) {
		ret = chan->func->push(chan, args->v0.pushbuf);
		if (ret)
			return ret;
	}

	spin_lock(&disp->client.lock);
	if (disp->chan[chan->chid.user]) {
		spin_unlock(&disp->client.lock);
		return -EBUSY;
	}
	disp->chan[chan->chid.user] = chan;
	spin_unlock(&disp->client.lock);
	return 0;
}

int
nvkm_disp_wndw_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		   struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_udisp(oclass->parent);

	return nvkm_disp_chan_new_(disp, disp->wndw.nr, oclass, argv, argc, pobject);
}

int
nvkm_disp_chan_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		   struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_udisp(oclass->parent);

	return nvkm_disp_chan_new_(disp, disp->head.nr, oclass, argv, argc, pobject);
}

int
nvkm_disp_core_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		   struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_udisp(oclass->parent);

	return nvkm_disp_chan_new_(disp, 1, oclass, argv, argc, pobject);
}
