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
#include "chid.h"
#include "runl.h"

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
	struct nvkm_runl *runl = chan->cgrp->runl;
	union nvif_chan_event_args *args = argv;

	if (!uevent)
		return 0;
	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	switch (args->v0.type) {
	case NVIF_CHAN_EVENT_V0_NON_STALL_INTR:
		return nvkm_uevent_add(uevent, &runl->fifo->nonstall.event, 0,
				       NVKM_FIFO_NONSTALL_EVENT, NULL);
	case NVIF_CHAN_EVENT_V0_KILLED:
		return nvkm_uevent_add(uevent, &runl->chid->event, chan->id,
				       NVKM_CHAN_EVENT_ERRORED, NULL);
	default:
		break;
	}

	return -ENOSYS;
}

struct nvkm_uobj {
	struct nvkm_oproxy oproxy;
	struct nvkm_chan *chan;
	struct nvkm_cctx *cctx;
};

static int
nvkm_uchan_object_fini_1(struct nvkm_oproxy *oproxy, bool suspend)
{
	struct nvkm_uobj *uobj = container_of(oproxy, typeof(*uobj), oproxy);
	struct nvkm_chan *chan = uobj->chan;
	struct nvkm_cctx *cctx = uobj->cctx;

	/* Unbind engine context from channel, if no longer required. */
	if (refcount_dec_and_mutex_lock(&cctx->uses, &chan->cgrp->mutex)) {
		nvkm_chan_cctx_bind(chan, oproxy, NULL);
		mutex_unlock(&chan->cgrp->mutex);
	}

	return 0;
}

static int
nvkm_uchan_object_init_0(struct nvkm_oproxy *oproxy)
{
	struct nvkm_uobj *uobj = container_of(oproxy, typeof(*uobj), oproxy);
	struct nvkm_chan *chan = uobj->chan;
	struct nvkm_cctx *cctx = uobj->cctx;
	int ret = 0;

	/* Bind engine context to channel, if it hasn't been already. */
	if (!refcount_inc_not_zero(&cctx->uses)) {
		mutex_lock(&chan->cgrp->mutex);
		if (!refcount_inc_not_zero(&cctx->uses)) {
			if (ret == 0) {
				nvkm_chan_cctx_bind(chan, oproxy, cctx);
				refcount_set(&cctx->uses, 1);
			}
		}
		mutex_unlock(&chan->cgrp->mutex);
	}

	return ret;
}

static void
nvkm_uchan_object_dtor(struct nvkm_oproxy *oproxy)
{
	struct nvkm_uobj *uobj = container_of(oproxy, typeof(*uobj), oproxy);

	nvkm_chan_cctx_put(uobj->chan, &uobj->cctx);
}

static const struct nvkm_oproxy_func
nvkm_uchan_object = {
	.dtor[1] = nvkm_uchan_object_dtor,
	.init[0] = nvkm_uchan_object_init_0,
	.fini[1] = nvkm_uchan_object_fini_1,
};

static int
nvkm_uchan_object_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		      struct nvkm_object **pobject)
{
	struct nvkm_chan *chan = nvkm_uchan(oclass->parent)->chan;
	struct nvkm_cgrp *cgrp = chan->cgrp;
	struct nvkm_engn *engn;
	struct nvkm_uobj *uobj;
	struct nvkm_oclass _oclass;
	int ret;

	/* Lookup host engine state for target engine. */
	engn = nvkm_runl_find_engn(engn, cgrp->runl, engn->engine == oclass->engine);
	if (WARN_ON(!engn))
		return -EINVAL;

	/* Allocate SW object. */
	if (!(uobj = kzalloc(sizeof(*uobj), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_oproxy_ctor(&nvkm_uchan_object, oclass, &uobj->oproxy);
	uobj->chan = chan;
	*pobject = &uobj->oproxy.base;

	/* Ref. channel context for target engine.*/
	ret = nvkm_chan_cctx_get(chan, engn, &uobj->cctx, oclass->client);
	if (ret)
		return ret;

	/* Allocate HW object. */
	_oclass = *oclass;
	_oclass.parent = &chan->object;
	return nvkm_fifo_chan_child_new(&_oclass, argv, argc, &uobj->oproxy.object);
}

static int
nvkm_uchan_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *oclass)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;
	struct nvkm_engn *engn;
	int ret;

	nvkm_runl_foreach_engn(engn, chan->cgrp->runl) {
		struct nvkm_engine *engine = engn->engine;
		int c = 0;

		oclass->engine = engine;
		oclass->base.oclass = 0;

		if (engine->func->fifo.sclass) {
			ret = engine->func->fifo.sclass(oclass, index);
			if (oclass->base.oclass) {
				if (!oclass->base.ctor)
					oclass->base.ctor = nvkm_object_new;
				oclass->ctor = nvkm_uchan_object_new;
				return 0;
			}

			index -= ret;
			continue;
		}

		while (engine->func->sclass[c].oclass) {
			if (c++ == index) {
				oclass->base = engine->func->sclass[index];
				if (!oclass->base.ctor)
					oclass->base.ctor = nvkm_object_new;
				oclass->ctor = nvkm_uchan_object_new;
				return 0;
			}
		}

		index -= c;
	}

	return -EINVAL;
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

	nvkm_chan_block(chan);
	nvkm_chan_remove(chan, true);

	if (chan->func->unbind)
		chan->func->unbind(chan);

	return 0;
}

static int
nvkm_uchan_init(struct nvkm_object *object)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;

	if (atomic_read(&chan->errored))
		return 0;

	if (chan->func->bind)
		chan->func->bind(chan);

	nvkm_chan_allow(chan);
	nvkm_chan_insert(chan);
	return 0;
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

	if (fifo->func->chan.ctor)
		ret = fifo->func->chan.ctor(gk104_fifo(fifo), oclass, argv, argc, &object);
	else
		ret = fifo->func->chan.oclass->ctor(fifo, oclass, argv, argc, &object);
	if (!object)
		return ret;

	uchan->chan = container_of(object, typeof(*uchan->chan), object);
	return ret;
}
