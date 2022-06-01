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
#include "chid.h"
#include "cgrp.h"
#include "runl.h"
#include "priv.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <core/oproxy.h>
#include <subdev/mmu.h>
#include <engine/dma.h>

#include <nvif/if0020.h>

const struct nvkm_event_func
nvkm_chan_event = {
};

struct nvkm_fifo_chan_object {
	struct nvkm_oproxy oproxy;
	struct nvkm_fifo_chan *chan;
	int hash;
};

static struct nvkm_fifo_engn *
nvkm_fifo_chan_engn(struct nvkm_fifo_chan *chan, struct nvkm_engine *engine)
{
	int engi = chan->fifo->func->engine_id(chan->fifo, engine);
	if (engi >= 0)
		return &chan->engn[engi];
	return NULL;
}

static int
nvkm_fifo_chan_child_fini(struct nvkm_oproxy *base, bool suspend)
{
	struct nvkm_fifo_chan_object *object =
		container_of(base, typeof(*object), oproxy);
	struct nvkm_engine *engine  = object->oproxy.object->engine;
	struct nvkm_fifo_chan *chan = object->chan;
	struct nvkm_fifo_engn *engn = nvkm_fifo_chan_engn(chan, engine);
	const char *name = engine->subdev.name;
	int ret = 0;

	if (--engn->usecount)
		return 0;

	if (chan->func->engine_fini) {
		ret = chan->func->engine_fini(chan, engine, suspend);
		if (ret) {
			nvif_error(&chan->object,
				   "detach %s failed, %d\n", name, ret);
			return ret;
		}
	}

	if (engn->object) {
		ret = nvkm_object_fini(engn->object, suspend);
		if (ret && suspend)
			return ret;
	}

	nvif_trace(&chan->object, "detached %s\n", name);
	return ret;
}

static int
nvkm_fifo_chan_child_init(struct nvkm_oproxy *base)
{
	struct nvkm_fifo_chan_object *object =
		container_of(base, typeof(*object), oproxy);
	struct nvkm_engine *engine  = object->oproxy.object->engine;
	struct nvkm_fifo_chan *chan = object->chan;
	struct nvkm_fifo_engn *engn = nvkm_fifo_chan_engn(chan, engine);
	const char *name = engine->subdev.name;
	int ret;

	if (engn->usecount++)
		return 0;

	if (engn->object) {
		ret = nvkm_object_init(engn->object);
		if (ret)
			return ret;
	}

	if (chan->func->engine_init) {
		ret = chan->func->engine_init(chan, engine);
		if (ret) {
			nvif_error(&chan->object,
				   "attach %s failed, %d\n", name, ret);
			return ret;
		}
	}

	nvif_trace(&chan->object, "attached %s\n", name);
	return 0;
}

static void
nvkm_fifo_chan_child_del(struct nvkm_oproxy *base)
{
	struct nvkm_fifo_chan_object *object =
		container_of(base, typeof(*object), oproxy);
	struct nvkm_engine *engine  = object->oproxy.base.engine;
	struct nvkm_fifo_chan *chan = object->chan;
	struct nvkm_fifo_engn *engn = nvkm_fifo_chan_engn(chan, engine);

	if (chan->func->object_dtor)
		chan->func->object_dtor(chan, object->hash);

	if (!--engn->refcount) {
		if (chan->func->engine_dtor)
			chan->func->engine_dtor(chan, engine);
		nvkm_object_del(&engn->object);
		if (chan->vmm)
			atomic_dec(&chan->vmm->engref[engine->subdev.type]);
	}
}

static const struct nvkm_oproxy_func
nvkm_fifo_chan_child_func = {
	.dtor[0] = nvkm_fifo_chan_child_del,
	.init[0] = nvkm_fifo_chan_child_init,
	.fini[0] = nvkm_fifo_chan_child_fini,
};

int
nvkm_fifo_chan_child_new(const struct nvkm_oclass *oclass, void *data, u32 size,
			 struct nvkm_object **pobject)
{
	struct nvkm_engine *engine = oclass->engine;
	struct nvkm_fifo_chan *chan = nvkm_fifo_chan(oclass->parent);
	struct nvkm_fifo_engn *engn = nvkm_fifo_chan_engn(chan, engine);
	struct nvkm_fifo_chan_object *object;
	int ret = 0;

	if (!(object = kzalloc(sizeof(*object), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_oproxy_ctor(&nvkm_fifo_chan_child_func, oclass, &object->oproxy);
	object->chan = chan;
	*pobject = &object->oproxy.base;

	if (!engn->refcount++) {
		struct nvkm_oclass cclass = {
			.client = oclass->client,
			.engine = oclass->engine,
		};

		if (chan->vmm)
			atomic_inc(&chan->vmm->engref[engine->subdev.type]);

		if (engine->func->fifo.cclass) {
			ret = engine->func->fifo.cclass(chan, &cclass,
							&engn->object);
		} else
		if (engine->func->cclass) {
			ret = nvkm_object_new_(engine->func->cclass, &cclass,
					       NULL, 0, &engn->object);
		}
		if (ret)
			return ret;

		if (chan->func->engine_ctor) {
			ret = chan->func->engine_ctor(chan, oclass->engine,
						      engn->object);
			if (ret)
				return ret;
		}
	}

	ret = oclass->base.ctor(&(const struct nvkm_oclass) {
					.base = oclass->base,
					.engn = oclass->engn,
					.handle = oclass->handle,
					.object = oclass->object,
					.client = oclass->client,
					.parent = engn->object ?
						  engn->object :
						  oclass->parent,
					.engine = engine,
				}, data, size, &object->oproxy.object);
	if (ret)
		return ret;

	if (chan->func->object_ctor) {
		object->hash =
			chan->func->object_ctor(chan, object->oproxy.object);
		if (object->hash < 0)
			return object->hash;
	}

	return 0;
}

static int
nvkm_fifo_chan_uevent(struct nvkm_object *object, void *argv, u32 argc, struct nvkm_uevent *uevent)
{
	struct nvkm_fifo_chan *chan = nvkm_fifo_chan(object);
	union nvif_chan_event_args *args = argv;

	switch (args->v0.type) {
	case NVIF_CHAN_EVENT_V0_NON_STALL_INTR:
		return nvkm_uevent_add(uevent, &chan->fifo->uevent, 0,
				       NVKM_FIFO_EVENT_NON_STALL_INTR, NULL);
	case NVIF_CHAN_EVENT_V0_KILLED:
		return nvkm_uevent_add(uevent, &chan->fifo->kevent, chan->chid,
				       NVKM_FIFO_EVENT_KILLED, NULL);
	default:
		break;
	}

	return -ENOSYS;
}

static int
nvkm_fifo_chan_map(struct nvkm_object *object, void *argv, u32 argc,
		   enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct nvkm_fifo_chan *chan = nvkm_fifo_chan(object);
	*type = NVKM_OBJECT_MAP_IO;
	*addr = chan->addr;
	*size = chan->size;
	return 0;
}

static int
nvkm_fifo_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_fifo_chan *chan = nvkm_fifo_chan(object);
	chan->func->fini(chan);
	return 0;
}

static int
nvkm_fifo_chan_init(struct nvkm_object *object)
{
	struct nvkm_fifo_chan *chan = nvkm_fifo_chan(object);
	chan->func->init(chan);
	return 0;
}

void
nvkm_chan_del(struct nvkm_chan **pchan)
{
	struct nvkm_chan *chan = *pchan;

	if (!chan)
		return;

	if (chan->cgrp) {
		nvkm_chid_put(chan->cgrp->runl->chid, chan->id, &chan->cgrp->lock);
		nvkm_cgrp_unref(&chan->cgrp);
	}

	chan = nvkm_object_dtor(&chan->object);
	kfree(chan);
}

static void *
nvkm_fifo_chan_dtor(struct nvkm_object *object)
{
	struct nvkm_fifo_chan *chan = nvkm_fifo_chan(object);
	struct nvkm_fifo *fifo = chan->fifo;
	void *data = chan->func->dtor(chan);
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	if (!list_empty(&chan->head)) {
		list_del(&chan->head);
	}
	spin_unlock_irqrestore(&fifo->lock, flags);

	if (chan->vmm) {
		nvkm_vmm_part(chan->vmm, chan->inst->memory);
		nvkm_vmm_unref(&chan->vmm);
	}

	nvkm_gpuobj_del(&chan->push);
	nvkm_gpuobj_del(&chan->inst);
	kfree(chan->func);
	return data;
}

static const struct nvkm_object_func
nvkm_fifo_chan_func = {
	.dtor = nvkm_fifo_chan_dtor,
	.init = nvkm_fifo_chan_init,
	.fini = nvkm_fifo_chan_fini,
	.map = nvkm_fifo_chan_map,
	.uevent = nvkm_fifo_chan_uevent,
};

int
nvkm_fifo_chan_ctor(const struct nvkm_fifo_chan_func *fn,
		    struct nvkm_fifo *fifo, u32 size, u32 align, bool zero,
		    u64 hvmm, u64 push, u32 engm, int bar, u32 base,
		    u32 user, const struct nvkm_oclass *oclass,
		    struct nvkm_fifo_chan *chan)
{
	struct nvkm_chan_func *func;
	struct nvkm_client *client = oclass->client;
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_dmaobj *dmaobj;
	struct nvkm_cgrp *cgrp = NULL;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn = NULL;
	struct nvkm_vmm *vmm = NULL;
	unsigned long flags;
	int ret;

	nvkm_runl_foreach(runl, fifo) {
		engn = nvkm_runl_find_engn(engn, runl, engm & BIT(engn->id));
		if (engn)
			break;
	}

	if (!engn)
		return -EINVAL;

	/*FIXME: temp kludge to ease transition, remove later */
	if (!(func = kmalloc(sizeof(*func), GFP_KERNEL)))
		return -ENOMEM;

	*func = *fifo->func->chan.func;
	func->dtor = fn->dtor;
	func->init = fn->init;
	func->fini = fn->fini;
	func->engine_ctor = fn->engine_ctor;
	func->engine_dtor = fn->engine_dtor;
	func->engine_init = fn->engine_init;
	func->engine_fini = fn->engine_fini;
	func->object_ctor = fn->object_ctor;
	func->object_dtor = fn->object_dtor;
	func->submit_token = fn->submit_token;

	chan->func = func;
	chan->id = -1;

	nvkm_object_ctor(&nvkm_fifo_chan_func, oclass, &chan->object);
	chan->fifo = fifo;
	INIT_LIST_HEAD(&chan->head);

	/* Join channel group.
	 *
	 * GK110 and newer support channel groups (aka TSGs), where individual channels
	 * share a timeslice, and, engine context(s).
	 *
	 * As such, engine contexts are tracked in nvkm_cgrp and we need them even when
	 * channels aren't in an API channel group, and on HW that doesn't support TSGs.
	 */
	if (!cgrp) {
		ret = nvkm_cgrp_new(runl, chan->name, vmm, fifo->func->cgrp.force, &chan->cgrp);
		if (ret) {
			RUNL_DEBUG(runl, "cgrp %d", ret);
			return ret;
		}

		cgrp = chan->cgrp;
	} else {
		if (cgrp->runl != runl || cgrp->vmm != vmm) {
			RUNL_DEBUG(runl, "cgrp %d %d", cgrp->runl != runl, cgrp->vmm != vmm);
			return -EINVAL;
		}

		chan->cgrp = nvkm_cgrp_ref(cgrp);
	}

	/* instance memory */
	ret = nvkm_gpuobj_new(device, size, align, zero, NULL, &chan->inst);
	if (ret)
		return ret;

	/* allocate push buffer ctxdma instance */
	if (push) {
		dmaobj = nvkm_dmaobj_search(client, push);
		if (IS_ERR(dmaobj))
			return PTR_ERR(dmaobj);

		ret = nvkm_object_bind(&dmaobj->object, chan->inst, -16,
				       &chan->push);
		if (ret)
			return ret;
	}

	/* channel address space */
	if (hvmm) {
		struct nvkm_vmm *vmm = nvkm_uvmm_search(client, hvmm);
		if (IS_ERR(vmm))
			return PTR_ERR(vmm);

		if (vmm->mmu != device->mmu)
			return -EINVAL;

		ret = nvkm_vmm_join(vmm, chan->inst->memory);
		if (ret)
			return ret;

		chan->vmm = nvkm_vmm_ref(vmm);
	}

	/* Allocate channel ID. */
	if (runl->cgid) {
		chan->id = chan->cgrp->id;
		runl->chid->data[chan->id] = chan;
		set_bit(chan->id, runl->chid->used);
		goto temp_hack_until_no_chid_eq_cgid_req;
	}

	chan->id = nvkm_chid_get(runl->chid, chan);
	if (chan->id < 0) {
		RUNL_ERROR(runl, "!chids");
		return -ENOSPC;
	}

temp_hack_until_no_chid_eq_cgid_req:
	spin_lock_irqsave(&fifo->lock, flags);
	list_add(&chan->head, &fifo->chan);
	spin_unlock_irqrestore(&fifo->lock, flags);

	/* determine address of this channel's user registers */
	chan->addr = device->func->resource_addr(device, bar) +
		     base + user * chan->chid;
	chan->size = user;
	return 0;
}
