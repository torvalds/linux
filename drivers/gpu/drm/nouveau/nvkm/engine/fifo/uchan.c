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
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"

#include <core/gpuobj.h>
#include <core/oproxy.h>
#include <subdev/mmu.h>
#include <engine/dma.h>

#include <nvif/if0020.h>

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
		return nvkm_uevent_add(uevent, &runl->fifo->nonstall.event, runl->id,
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
	int hash;
};

static int
nvkm_uchan_object_fini_1(struct nvkm_oproxy *oproxy, bool suspend)
{
	struct nvkm_uobj *uobj = container_of(oproxy, typeof(*uobj), oproxy);
	struct nvkm_chan *chan = uobj->chan;
	struct nvkm_cctx *cctx = uobj->cctx;
	struct nvkm_ectx *ectx = cctx->vctx->ectx;

	if (!ectx->object)
		return 0;

	/* Unbind engine context from channel, if no longer required. */
	if (refcount_dec_and_mutex_lock(&cctx->uses, &chan->cgrp->mutex)) {
		nvkm_chan_cctx_bind(chan, ectx->engn, NULL);

		if (refcount_dec_and_test(&ectx->uses))
			nvkm_object_fini(ectx->object, false);
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
	struct nvkm_ectx *ectx = cctx->vctx->ectx;
	int ret = 0;

	if (!ectx->object)
		return 0;

	/* Bind engine context to channel, if it hasn't been already. */
	if (!refcount_inc_not_zero(&cctx->uses)) {
		mutex_lock(&chan->cgrp->mutex);
		if (!refcount_inc_not_zero(&cctx->uses)) {
			if (!refcount_inc_not_zero(&ectx->uses)) {
				ret = nvkm_object_init(ectx->object);
				if (ret == 0)
					refcount_set(&ectx->uses, 1);
			}

			if (ret == 0) {
				nvkm_chan_cctx_bind(chan, ectx->engn, cctx);
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
	struct nvkm_engn *engn;

	if (!uobj->cctx)
		return;

	engn = uobj->cctx->vctx->ectx->engn;
	if (engn->func->ramht_del)
		engn->func->ramht_del(uobj->chan, uobj->hash);

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
	ret = oclass->base.ctor(&(const struct nvkm_oclass) {
					.base = oclass->base,
					.engn = oclass->engn,
					.handle = oclass->handle,
					.object = oclass->object,
					.client = oclass->client,
					.parent = uobj->cctx->vctx->ectx->object ?: oclass->parent,
					.engine = engn->engine,
				 }, argv, argc, &uobj->oproxy.object);
	if (ret)
		return ret;

	if (engn->func->ramht_add) {
		uobj->hash = engn->func->ramht_add(engn, uobj->oproxy.object, uobj->chan);
		if (uobj->hash < 0)
			return uobj->hash;
	}

	return 0;
}

static int
nvkm_uchan_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *oclass)
{
	struct nvkm_chan *chan = nvkm_uchan(object)->chan;
	struct nvkm_engn *engn;
	int ret, runq = 0;

	nvkm_runl_foreach_engn(engn, chan->cgrp->runl) {
		struct nvkm_engine *engine = engn->engine;
		int c = 0;

		/* Each runqueue, on runlists with multiple, has its own LCE. */
		if (engn->runl->func->runqs) {
			if (engine->subdev.type == NVKM_ENGINE_CE) {
				if (chan->runq != runq++)
					continue;
			}
		}

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
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	if (!chan->func->userd->bar)
		return -ENOSYS;

	*type = NVKM_OBJECT_MAP_IO;
	*addr = device->func->resource_addr(device, chan->func->userd->bar) +
		chan->func->userd->base + chan->userd.base;
	*size = chan->func->userd->size;
	return 0;
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

struct nvkm_chan *
nvkm_uchan_chan(struct nvkm_object *object)
{
	if (WARN_ON(object->func != &nvkm_uchan))
		return NULL;

	return nvkm_uchan(object)->chan;
}

int
nvkm_uchan_new(struct nvkm_fifo *fifo, struct nvkm_cgrp *cgrp, const struct nvkm_oclass *oclass,
	       void *argv, u32 argc, struct nvkm_object **pobject)
{
	union nvif_chan_args *args = argv;
	struct nvkm_runl *runl;
	struct nvkm_vmm *vmm = NULL;
	struct nvkm_dmaobj *ctxdma = NULL;
	struct nvkm_memory *userd = NULL;
	struct nvkm_uchan *uchan;
	struct nvkm_chan *chan;
	int ret;

	if (argc < sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	argc -= sizeof(args->v0);

	if (args->v0.namelen != argc)
		return -EINVAL;

	/* Lookup objects referenced in args. */
	runl = nvkm_runl_get(fifo, args->v0.runlist, 0);
	if (!runl)
		return -EINVAL;

	if (args->v0.vmm) {
		vmm = nvkm_uvmm_search(oclass->client, args->v0.vmm);
		if (IS_ERR(vmm))
			return PTR_ERR(vmm);
	}

	if (args->v0.ctxdma) {
		ctxdma = nvkm_dmaobj_search(oclass->client, args->v0.ctxdma);
		if (IS_ERR(ctxdma)) {
			ret = PTR_ERR(ctxdma);
			goto done;
		}
	}

	if (args->v0.huserd) {
		userd = nvkm_umem_search(oclass->client, args->v0.huserd);
		if (IS_ERR(userd)) {
			ret = PTR_ERR(userd);
			userd = NULL;
			goto done;
		}
	}

	/* Allocate channel. */
	if (!(uchan = kzalloc(sizeof(*uchan), GFP_KERNEL))) {
		ret = -ENOMEM;
		goto done;
	}

	nvkm_object_ctor(&nvkm_uchan, oclass, &uchan->object);
	*pobject = &uchan->object;

	ret = nvkm_chan_new_(fifo->func->chan.func, runl, args->v0.runq, cgrp, args->v0.name,
			     args->v0.priv != 0, args->v0.devm, vmm, ctxdma, args->v0.offset,
			     args->v0.length, userd, args->v0.ouserd, &uchan->chan);
	if (ret)
		goto done;

	chan = uchan->chan;

	/* Return channel info to caller. */
	if (chan->func->doorbell_handle)
		args->v0.token = chan->func->doorbell_handle(chan);
	else
		args->v0.token = ~0;

	args->v0.chid = chan->id;

	switch (nvkm_memory_target(chan->inst->memory)) {
	case NVKM_MEM_TARGET_INST: args->v0.aper = NVIF_CHAN_V0_INST_APER_INST; break;
	case NVKM_MEM_TARGET_VRAM: args->v0.aper = NVIF_CHAN_V0_INST_APER_VRAM; break;
	case NVKM_MEM_TARGET_HOST: args->v0.aper = NVIF_CHAN_V0_INST_APER_HOST; break;
	case NVKM_MEM_TARGET_NCOH: args->v0.aper = NVIF_CHAN_V0_INST_APER_NCOH; break;
	default:
		WARN_ON(1);
		ret = -EFAULT;
		break;
	}

	args->v0.inst = nvkm_memory_addr(chan->inst->memory);
done:
	nvkm_memory_unref(&userd);
	nvkm_vmm_unref(&vmm);
	return ret;
}
