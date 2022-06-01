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
#include "runl.h"
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "priv.h"

#include <core/gpuobj.h>
#include <subdev/top.h>

struct nvkm_chan *
nvkm_runl_chan_get_inst(struct nvkm_runl *runl, u64 inst, unsigned long *pirqflags)
{
	struct nvkm_chid *chid = runl->chid;
	struct nvkm_chan *chan;
	unsigned long flags;
	int id;

	spin_lock_irqsave(&chid->lock, flags);
	for_each_set_bit(id, chid->used, chid->nr) {
		chan = chid->data[id];
		if (likely(chan)) {
			if (chan->inst->addr == inst) {
				spin_lock(&chan->cgrp->lock);
				*pirqflags = flags;
				spin_unlock(&chid->lock);
				return chan;
			}
		}
	}
	spin_unlock_irqrestore(&chid->lock, flags);
	return NULL;
}

struct nvkm_chan *
nvkm_runl_chan_get_chid(struct nvkm_runl *runl, int id, unsigned long *pirqflags)
{
	struct nvkm_chid *chid = runl->chid;
	struct nvkm_chan *chan;
	unsigned long flags;

	spin_lock_irqsave(&chid->lock, flags);
	if (!WARN_ON(id >= chid->nr)) {
		chan = chid->data[id];
		if (likely(chan)) {
			spin_lock(&chan->cgrp->lock);
			*pirqflags = flags;
			spin_unlock(&chid->lock);
			return chan;
		}
	}
	spin_unlock_irqrestore(&chid->lock, flags);
	return NULL;
}

bool
nvkm_runl_update_pending(struct nvkm_runl *runl)
{
	if (!runl->func->pending(runl))
		return false;

	return true;
}

void
nvkm_runl_allow(struct nvkm_runl *runl)
{
	struct nvkm_fifo *fifo = runl->fifo;
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	if (!--runl->blocked) {
		RUNL_TRACE(runl, "running");
		runl->func->allow(runl, ~0);
	}
	spin_unlock_irqrestore(&fifo->lock, flags);
}

void
nvkm_runl_block(struct nvkm_runl *runl)
{
	struct nvkm_fifo *fifo = runl->fifo;
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	if (!runl->blocked++) {
		RUNL_TRACE(runl, "stopped");
		runl->func->block(runl, ~0);
	}
	spin_unlock_irqrestore(&fifo->lock, flags);
}

void
nvkm_runl_del(struct nvkm_runl *runl)
{
	struct nvkm_engn *engn, *engt;

	list_for_each_entry_safe(engn, engt, &runl->engns, head) {
		list_del(&engn->head);
		kfree(engn);
	}

	nvkm_chid_unref(&runl->chid);
	nvkm_chid_unref(&runl->cgid);

	list_del(&runl->head);
	mutex_destroy(&runl->mutex);
	kfree(runl);
}

struct nvkm_engn *
nvkm_runl_add(struct nvkm_runl *runl, int engi, const struct nvkm_engn_func *func,
	      enum nvkm_subdev_type type, int inst)
{
	struct nvkm_fifo *fifo = runl->fifo;
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_engine *engine;
	struct nvkm_engn *engn;

	engine = nvkm_device_engine(device, type, inst);
	if (!engine) {
		RUNL_DEBUG(runl, "engn %d.%d[%s] not found", engi, inst, nvkm_subdev_type[type]);
		return NULL;
	}

	if (!(engn = kzalloc(sizeof(*engn), GFP_KERNEL)))
		return NULL;

	engn->func = func;
	engn->runl = runl;
	engn->id = engi;
	engn->engine = engine;
	engn->fault = -1;
	list_add_tail(&engn->head, &runl->engns);

	/* Lookup MMU engine ID for fault handling. */
	if (device->top)
		engn->fault = nvkm_top_fault_id(device, engine->subdev.type, engine->subdev.inst);

	if (engn->fault < 0 && fifo->func->mmu_fault) {
		const struct nvkm_enum *map = fifo->func->mmu_fault->engine;

		while (map->name) {
			if (map->data2 == engine->subdev.type && map->inst == engine->subdev.inst) {
				engn->fault = map->value;
				break;
			}
			map++;
		}
	}

	return engn;
}

struct nvkm_runl *
nvkm_runl_get(struct nvkm_fifo *fifo, int runi, u32 addr)
{
	struct nvkm_runl *runl;

	nvkm_runl_foreach(runl, fifo) {
		if ((runi >= 0 && runl->id == runi) || (runi < 0 && runl->addr == addr))
			return runl;
	}

	return NULL;
}

struct nvkm_runl *
nvkm_runl_new(struct nvkm_fifo *fifo, int runi, u32 addr, int id_nr)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_runl *runl;
	int ret;

	if (!(runl = kzalloc(sizeof(*runl), GFP_KERNEL)))
		return NULL;

	runl->func = fifo->func->runl;
	runl->fifo = fifo;
	runl->id = runi;
	runl->addr = addr;
	INIT_LIST_HEAD(&runl->engns);
	INIT_LIST_HEAD(&runl->cgrps);
	mutex_init(&runl->mutex);
	list_add_tail(&runl->head, &fifo->runls);

	if (!fifo->chid) {
		if ((ret = nvkm_chid_new(&nvkm_chan_event, subdev, id_nr, 0, id_nr, &runl->cgid)) ||
		    (ret = nvkm_chid_new(&nvkm_chan_event, subdev, id_nr, 0, id_nr, &runl->chid))) {
			RUNL_ERROR(runl, "cgid/chid: %d", ret);
			nvkm_runl_del(runl);
			return NULL;
		}
	} else {
		runl->cgid = nvkm_chid_ref(fifo->cgid);
		runl->chid = nvkm_chid_ref(fifo->chid);
	}

	return runl;
}
