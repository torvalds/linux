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
#include "chid.h"
#include "runl.h"
#include "priv.h"

#include <core/ramht.h>
#include <subdev/mmu.h>
#include <engine/dma.h>

#include <nvif/if0020.h>

const struct nvkm_event_func
nvkm_chan_event = {
};

void
nvkm_chan_cctx_bind(struct nvkm_chan *chan, struct nvkm_engn *engn, struct nvkm_cctx *cctx)
{
	struct nvkm_cgrp *cgrp = chan->cgrp;
	struct nvkm_runl *runl = cgrp->runl;
	struct nvkm_engine *engine = engn->engine;

	if (!engn->func->bind)
		return;

	CHAN_TRACE(chan, "%sbind cctx %d[%s]", cctx ? "" : "un", engn->id, engine->subdev.name);

	/* Prevent any channel in channel group from being rescheduled, kick them
	 * off host and any engine(s) they're loaded on.
	 */
	if (cgrp->hw)
		nvkm_runl_block(runl);
	else
		nvkm_chan_block(chan);
	nvkm_chan_preempt(chan, true);

	/* Update context pointer. */
	engn->func->bind(engn, cctx, chan);

	/* Resume normal operation. */
	if (cgrp->hw)
		nvkm_runl_allow(runl);
	else
		nvkm_chan_allow(chan);
}

void
nvkm_chan_cctx_put(struct nvkm_chan *chan, struct nvkm_cctx **pcctx)
{
	struct nvkm_cctx *cctx = *pcctx;

	if (cctx) {
		struct nvkm_engn *engn = cctx->vctx->ectx->engn;

		if (refcount_dec_and_mutex_lock(&cctx->refs, &chan->cgrp->mutex)) {
			CHAN_TRACE(chan, "dtor cctx %d[%s]", engn->id, engn->engine->subdev.name);
			nvkm_cgrp_vctx_put(chan->cgrp, &cctx->vctx);
			list_del(&cctx->head);
			kfree(cctx);
			mutex_unlock(&chan->cgrp->mutex);
		}

		*pcctx = NULL;
	}
}

int
nvkm_chan_cctx_get(struct nvkm_chan *chan, struct nvkm_engn *engn, struct nvkm_cctx **pcctx,
		   struct nvkm_client *client)
{
	struct nvkm_cgrp *cgrp = chan->cgrp;
	struct nvkm_vctx *vctx;
	struct nvkm_cctx *cctx;
	int ret;

	/* Look for an existing channel context for this engine+VEID. */
	mutex_lock(&cgrp->mutex);
	cctx = nvkm_list_find(cctx, &chan->cctxs, head,
			      cctx->vctx->ectx->engn == engn && cctx->vctx->vmm == chan->vmm);
	if (cctx) {
		refcount_inc(&cctx->refs);
		*pcctx = cctx;
		mutex_unlock(&chan->cgrp->mutex);
		return 0;
	}

	/* Nope - create a fresh one.  But, sub-context first. */
	ret = nvkm_cgrp_vctx_get(cgrp, engn, chan, &vctx, client);
	if (ret) {
		CHAN_ERROR(chan, "vctx %d[%s]: %d", engn->id, engn->engine->subdev.name, ret);
		goto done;
	}

	/* Now, create the channel context - to track engine binding. */
	CHAN_TRACE(chan, "ctor cctx %d[%s]", engn->id, engn->engine->subdev.name);
	if (!(cctx = *pcctx = kzalloc(sizeof(*cctx), GFP_KERNEL))) {
		nvkm_cgrp_vctx_put(cgrp, &vctx);
		ret = -ENOMEM;
		goto done;
	}

	cctx->vctx = vctx;
	refcount_set(&cctx->refs, 1);
	refcount_set(&cctx->uses, 0);
	list_add_tail(&cctx->head, &chan->cctxs);
done:
	mutex_unlock(&cgrp->mutex);
	return ret;
}

int
nvkm_chan_preempt_locked(struct nvkm_chan *chan, bool wait)
{
	struct nvkm_runl *runl = chan->cgrp->runl;

	CHAN_TRACE(chan, "preempt");
	chan->func->preempt(chan);
	if (!wait)
		return 0;

	return nvkm_runl_preempt_wait(runl);
}

int
nvkm_chan_preempt(struct nvkm_chan *chan, bool wait)
{
	int ret;

	if (!chan->func->preempt)
		return 0;

	mutex_lock(&chan->cgrp->runl->mutex);
	ret = nvkm_chan_preempt_locked(chan, wait);
	mutex_unlock(&chan->cgrp->runl->mutex);
	return ret;
}

void
nvkm_chan_remove_locked(struct nvkm_chan *chan)
{
	struct nvkm_cgrp *cgrp = chan->cgrp;
	struct nvkm_runl *runl = cgrp->runl;

	if (list_empty(&chan->head))
		return;

	CHAN_TRACE(chan, "remove");
	if (!--cgrp->chan_nr) {
		runl->cgrp_nr--;
		list_del(&cgrp->head);
	}
	runl->chan_nr--;
	list_del_init(&chan->head);
	atomic_set(&runl->changed, 1);
}

void
nvkm_chan_remove(struct nvkm_chan *chan, bool preempt)
{
	struct nvkm_runl *runl = chan->cgrp->runl;

	mutex_lock(&runl->mutex);
	if (preempt && chan->func->preempt)
		nvkm_chan_preempt_locked(chan, true);
	nvkm_chan_remove_locked(chan);
	nvkm_runl_update_locked(runl, true);
	mutex_unlock(&runl->mutex);
}

void
nvkm_chan_insert(struct nvkm_chan *chan)
{
	struct nvkm_cgrp *cgrp = chan->cgrp;
	struct nvkm_runl *runl = cgrp->runl;

	mutex_lock(&runl->mutex);
	if (WARN_ON(!list_empty(&chan->head))) {
		mutex_unlock(&runl->mutex);
		return;
	}

	CHAN_TRACE(chan, "insert");
	list_add_tail(&chan->head, &cgrp->chans);
	runl->chan_nr++;
	if (!cgrp->chan_nr++) {
		list_add_tail(&cgrp->head, &cgrp->runl->cgrps);
		runl->cgrp_nr++;
	}
	atomic_set(&runl->changed, 1);
	nvkm_runl_update_locked(runl, true);
	mutex_unlock(&runl->mutex);
}

static void
nvkm_chan_block_locked(struct nvkm_chan *chan)
{
	CHAN_TRACE(chan, "block %d", atomic_read(&chan->blocked));
	if (atomic_inc_return(&chan->blocked) == 1)
		chan->func->stop(chan);
}

void
nvkm_chan_error(struct nvkm_chan *chan, bool preempt)
{
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);
	if (atomic_inc_return(&chan->errored) == 1) {
		CHAN_ERROR(chan, "errored - disabling channel");
		nvkm_chan_block_locked(chan);
		if (preempt)
			chan->func->preempt(chan);
		nvkm_event_ntfy(&chan->cgrp->runl->chid->event, chan->id, NVKM_CHAN_EVENT_ERRORED);
	}
	spin_unlock_irqrestore(&chan->lock, flags);
}

void
nvkm_chan_block(struct nvkm_chan *chan)
{
	spin_lock_irq(&chan->lock);
	nvkm_chan_block_locked(chan);
	spin_unlock_irq(&chan->lock);
}

void
nvkm_chan_allow(struct nvkm_chan *chan)
{
	spin_lock_irq(&chan->lock);
	CHAN_TRACE(chan, "allow %d", atomic_read(&chan->blocked));
	if (atomic_dec_and_test(&chan->blocked))
		chan->func->start(chan);
	spin_unlock_irq(&chan->lock);
}

void
nvkm_chan_del(struct nvkm_chan **pchan)
{
	struct nvkm_chan *chan = *pchan;

	if (!chan)
		return;

	if (chan->func->ramfc->clear)
		chan->func->ramfc->clear(chan);

	nvkm_ramht_del(&chan->ramht);
	nvkm_gpuobj_del(&chan->pgd);
	nvkm_gpuobj_del(&chan->eng);
	nvkm_gpuobj_del(&chan->cache);
	nvkm_gpuobj_del(&chan->ramfc);

	nvkm_memory_unref(&chan->userd.mem);

	if (chan->cgrp) {
		nvkm_chid_put(chan->cgrp->runl->chid, chan->id, &chan->cgrp->lock);
		nvkm_cgrp_unref(&chan->cgrp);
	}

	if (chan->vmm) {
		nvkm_vmm_part(chan->vmm, chan->inst->memory);
		nvkm_vmm_unref(&chan->vmm);
	}

	nvkm_gpuobj_del(&chan->push);
	nvkm_gpuobj_del(&chan->inst);
	kfree(chan);
}

void
nvkm_chan_put(struct nvkm_chan **pchan, unsigned long irqflags)
{
	struct nvkm_chan *chan = *pchan;

	if (!chan)
		return;

	*pchan = NULL;
	spin_unlock_irqrestore(&chan->cgrp->lock, irqflags);
}

struct nvkm_chan *
nvkm_chan_get_inst(struct nvkm_engine *engine, u64 inst, unsigned long *pirqflags)
{
	struct nvkm_fifo *fifo = engine->subdev.device->fifo;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	struct nvkm_chan *chan;

	nvkm_runl_foreach(runl, fifo) {
		nvkm_runl_foreach_engn(engn, runl) {
			if (engine == &fifo->engine || engn->engine == engine) {
				chan = nvkm_runl_chan_get_inst(runl, inst, pirqflags);
				if (chan || engn->engine == engine)
					return chan;
			}
		}
	}

	return NULL;
}

struct nvkm_chan *
nvkm_chan_get_chid(struct nvkm_engine *engine, int id, unsigned long *pirqflags)
{
	struct nvkm_fifo *fifo = engine->subdev.device->fifo;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;

	nvkm_runl_foreach(runl, fifo) {
		nvkm_runl_foreach_engn(engn, runl) {
			if (fifo->chid || engn->engine == engine)
				return nvkm_runl_chan_get_chid(runl, id, pirqflags);
		}
	}

	return NULL;
}

int
nvkm_chan_new_(const struct nvkm_chan_func *func, struct nvkm_runl *runl, int runq,
	       struct nvkm_cgrp *cgrp, const char *name, bool priv, u32 devm, struct nvkm_vmm *vmm,
	       struct nvkm_dmaobj *dmaobj, u64 offset, u64 length,
	       struct nvkm_memory *userd, u64 ouserd, struct nvkm_chan **pchan)
{
	struct nvkm_fifo *fifo = runl->fifo;
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_chan *chan;
	int ret;

	/* Validate arguments against class requirements. */
	if ((runq && runq >= runl->func->runqs) ||
	    (!func->inst->vmm != !vmm) ||
	    ((func->userd->bar < 0) == !userd) ||
	    (!func->ramfc->ctxdma != !dmaobj) ||
	    ((func->ramfc->devm < devm) && devm != BIT(0)) ||
	    (!func->ramfc->priv && priv)) {
		RUNL_DEBUG(runl, "args runq:%d:%d vmm:%d:%p userd:%d:%p "
				 "push:%d:%p devm:%08x:%08x priv:%d:%d",
			   runl->func->runqs, runq, func->inst->vmm, vmm,
			   func->userd->bar < 0, userd, func->ramfc->ctxdma, dmaobj,
			   func->ramfc->devm, devm, func->ramfc->priv, priv);
		return -EINVAL;
	}

	if (!(chan = *pchan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;

	chan->func = func;
	strscpy(chan->name, name, sizeof(chan->name));
	chan->runq = runq;
	chan->id = -1;
	spin_lock_init(&chan->lock);
	atomic_set(&chan->blocked, 1);
	atomic_set(&chan->errored, 0);
	INIT_LIST_HEAD(&chan->cctxs);
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

	/* Allocate instance block. */
	ret = nvkm_gpuobj_new(device, func->inst->size, 0x1000, func->inst->zero, NULL,
			      &chan->inst);
	if (ret) {
		RUNL_DEBUG(runl, "inst %d", ret);
		return ret;
	}

	/* Initialise virtual address-space. */
	if (func->inst->vmm) {
		if (WARN_ON(vmm->mmu != device->mmu))
			return -EINVAL;

		ret = nvkm_vmm_join(vmm, chan->inst->memory);
		if (ret) {
			RUNL_DEBUG(runl, "vmm %d", ret);
			return ret;
		}

		chan->vmm = nvkm_vmm_ref(vmm);
	}

	/* Allocate HW ctxdma for push buffer. */
	if (func->ramfc->ctxdma) {
		ret = nvkm_object_bind(&dmaobj->object, chan->inst, -16, &chan->push);
		if (ret) {
			RUNL_DEBUG(runl, "bind %d", ret);
			return ret;
		}
	}

	/* Allocate channel ID. */
	chan->id = nvkm_chid_get(runl->chid, chan);
	if (chan->id < 0) {
		RUNL_ERROR(runl, "!chids");
		return -ENOSPC;
	}

	if (cgrp->id < 0)
		cgrp->id = chan->id;

	/* Initialise USERD. */
	if (func->userd->bar < 0) {
		if (ouserd + chan->func->userd->size >= nvkm_memory_size(userd)) {
			RUNL_DEBUG(runl, "ouserd %llx", ouserd);
			return -EINVAL;
		}

		ret = nvkm_memory_kmap(userd, &chan->userd.mem);
		if (ret) {
			RUNL_DEBUG(runl, "userd %d", ret);
			return ret;
		}

		chan->userd.base = ouserd;
	} else {
		chan->userd.mem = nvkm_memory_ref(fifo->userd.mem);
		chan->userd.base = chan->id * chan->func->userd->size;
	}

	if (chan->func->userd->clear)
		chan->func->userd->clear(chan);

	/* Initialise RAMFC. */
	ret = chan->func->ramfc->write(chan, offset, length, devm, priv);
	if (ret) {
		RUNL_DEBUG(runl, "ramfc %d", ret);
		return ret;
	}

	return 0;
}
