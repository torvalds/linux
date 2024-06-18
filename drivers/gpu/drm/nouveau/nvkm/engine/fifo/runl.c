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
#include "runq.h"

#include <core/gpuobj.h>
#include <subdev/timer.h>
#include <subdev/top.h>

static struct nvkm_cgrp *
nvkm_engn_cgrp_get(struct nvkm_engn *engn, unsigned long *pirqflags)
{
	struct nvkm_cgrp *cgrp = NULL;
	struct nvkm_chan *chan;
	bool cgid;
	int id;

	id = engn->func->cxid(engn, &cgid);
	if (id < 0)
		return NULL;

	if (!cgid) {
		chan = nvkm_runl_chan_get_chid(engn->runl, id, pirqflags);
		if (chan)
			cgrp = chan->cgrp;
	} else {
		cgrp = nvkm_runl_cgrp_get_cgid(engn->runl, id, pirqflags);
	}

	WARN_ON(!cgrp);
	return cgrp;
}

static void
nvkm_runl_rc(struct nvkm_runl *runl)
{
	struct nvkm_fifo *fifo = runl->fifo;
	struct nvkm_cgrp *cgrp, *gtmp;
	struct nvkm_chan *chan, *ctmp;
	struct nvkm_engn *engn;
	unsigned long flags;
	int rc, state, i;
	bool reset;

	/* Runlist is blocked before scheduling recovery - fetch count. */
	BUG_ON(!mutex_is_locked(&runl->mutex));
	rc = atomic_xchg(&runl->rc_pending, 0);
	if (!rc)
		return;

	/* Look for channel groups flagged for RC. */
	nvkm_runl_foreach_cgrp_safe(cgrp, gtmp, runl) {
		state = atomic_cmpxchg(&cgrp->rc, NVKM_CGRP_RC_PENDING, NVKM_CGRP_RC_RUNNING);
		if (state == NVKM_CGRP_RC_PENDING) {
			/* Disable all channels in them, and remove from runlist. */
			nvkm_cgrp_foreach_chan_safe(chan, ctmp, cgrp) {
				nvkm_chan_error(chan, false);
				nvkm_chan_remove_locked(chan);
			}
		}
	}

	/* On GPUs with runlist preempt, wait for PBDMA(s) servicing runlist to go idle. */
	if (runl->func->preempt) {
		for (i = 0; i < runl->runq_nr; i++) {
			struct nvkm_runq *runq = runl->runq[i];

			if (runq) {
				nvkm_msec(fifo->engine.subdev.device, 2000,
					if (runq->func->idle(runq))
						break;
				);
			}
		}
	}

	/* Look for engines that are still on flagged channel groups - reset them. */
	nvkm_runl_foreach_engn_cond(engn, runl, engn->func->cxid) {
		cgrp = nvkm_engn_cgrp_get(engn, &flags);
		if (!cgrp) {
			ENGN_DEBUG(engn, "cxid not valid");
			continue;
		}

		reset = atomic_read(&cgrp->rc) == NVKM_CGRP_RC_RUNNING;
		nvkm_cgrp_put(&cgrp, flags);
		if (!reset) {
			ENGN_DEBUG(engn, "cxid not in recovery");
			continue;
		}

		ENGN_DEBUG(engn, "resetting...");
		/*TODO: can we do something less of a potential catastrophe on failure? */
		WARN_ON(nvkm_engine_reset(engn->engine));
	}

	/* Submit runlist update, and clear any remaining exception state. */
	runl->func->update(runl);
	if (runl->func->fault_clear)
		runl->func->fault_clear(runl);

	/* Unblock runlist processing. */
	while (rc--)
		nvkm_runl_allow(runl);
	runl->func->wait(runl);
}

static void
nvkm_runl_rc_runl(struct nvkm_runl *runl)
{
	RUNL_ERROR(runl, "rc scheduled");

	nvkm_runl_block(runl);
	if (runl->func->preempt)
		runl->func->preempt(runl);

	atomic_inc(&runl->rc_pending);
	schedule_work(&runl->work);
}

void
nvkm_runl_rc_cgrp(struct nvkm_cgrp *cgrp)
{
	if (atomic_cmpxchg(&cgrp->rc, NVKM_CGRP_RC_NONE, NVKM_CGRP_RC_PENDING) != NVKM_CGRP_RC_NONE)
		return;

	CGRP_ERROR(cgrp, "rc scheduled");
	nvkm_runl_rc_runl(cgrp->runl);
}

void
nvkm_runl_rc_engn(struct nvkm_runl *runl, struct nvkm_engn *engn)
{
	struct nvkm_cgrp *cgrp;
	unsigned long flags;

	/* Lookup channel group currently on engine. */
	cgrp = nvkm_engn_cgrp_get(engn, &flags);
	if (!cgrp) {
		ENGN_DEBUG(engn, "rc skipped, not on channel");
		return;
	}

	nvkm_runl_rc_cgrp(cgrp);
	nvkm_cgrp_put(&cgrp, flags);
}

static void
nvkm_runl_work(struct work_struct *work)
{
	struct nvkm_runl *runl = container_of(work, typeof(*runl), work);

	mutex_lock(&runl->mutex);
	nvkm_runl_rc(runl);
	mutex_unlock(&runl->mutex);

}

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

struct nvkm_cgrp *
nvkm_runl_cgrp_get_cgid(struct nvkm_runl *runl, int id, unsigned long *pirqflags)
{
	struct nvkm_chid *cgid = runl->cgid;
	struct nvkm_cgrp *cgrp;
	unsigned long flags;

	spin_lock_irqsave(&cgid->lock, flags);
	if (!WARN_ON(id >= cgid->nr)) {
		cgrp = cgid->data[id];
		if (likely(cgrp)) {
			spin_lock(&cgrp->lock);
			*pirqflags = flags;
			spin_unlock(&cgid->lock);
			return cgrp;
		}
	}
	spin_unlock_irqrestore(&cgid->lock, flags);
	return NULL;
}

int
nvkm_runl_preempt_wait(struct nvkm_runl *runl)
{
	return nvkm_msec(runl->fifo->engine.subdev.device, runl->fifo->timeout.chan_msec,
		if (!runl->func->preempt_pending(runl))
			break;

		nvkm_runl_rc(runl);
		usleep_range(1, 2);
	) < 0 ? -ETIMEDOUT : 0;
}

bool
nvkm_runl_update_pending(struct nvkm_runl *runl)
{
	if (!runl->func->pending(runl))
		return false;

	nvkm_runl_rc(runl);
	return true;
}

void
nvkm_runl_update_locked(struct nvkm_runl *runl, bool wait)
{
	if (atomic_xchg(&runl->changed, 0) && runl->func->update) {
		runl->func->update(runl);
		if (wait)
			runl->func->wait(runl);
	}
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
nvkm_runl_fini(struct nvkm_runl *runl)
{
	if (runl->func->fini)
		runl->func->fini(runl);

	flush_work(&runl->work);
}

void
nvkm_runl_del(struct nvkm_runl *runl)
{
	struct nvkm_engn *engn, *engt;

	nvkm_memory_unref(&runl->mem);

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
		return ERR_PTR(-ENOMEM);

	runl->func = fifo->func->runl;
	runl->fifo = fifo;
	runl->id = runi;
	runl->addr = addr;
	INIT_LIST_HEAD(&runl->engns);
	INIT_LIST_HEAD(&runl->cgrps);
	atomic_set(&runl->changed, 0);
	mutex_init(&runl->mutex);
	INIT_WORK(&runl->work, nvkm_runl_work);
	atomic_set(&runl->rc_triggered, 0);
	atomic_set(&runl->rc_pending, 0);
	list_add_tail(&runl->head, &fifo->runls);

	if (!fifo->chid) {
		if ((ret = nvkm_chid_new(&nvkm_chan_event, subdev, id_nr, 0, id_nr, &runl->cgid)) ||
		    (ret = nvkm_chid_new(&nvkm_chan_event, subdev, id_nr, 0, id_nr, &runl->chid))) {
			RUNL_ERROR(runl, "cgid/chid: %d", ret);
			nvkm_runl_del(runl);
			return ERR_PTR(ret);
		}
	} else {
		runl->cgid = nvkm_chid_ref(fifo->cgid);
		runl->chid = nvkm_chid_ref(fifo->chid);
	}

	return runl;
}
