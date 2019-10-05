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
#include "gk104.h"
#include "cgrp.h"
#include "changk104.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/bar.h>
#include <subdev/fault.h>
#include <subdev/timer.h>
#include <subdev/top.h>
#include <engine/sw.h>

#include <nvif/class.h>
#include <nvif/cl0080.h>

struct gk104_fifo_engine_status {
	bool busy;
	bool faulted;
	bool chsw;
	bool save;
	bool load;
	struct {
		bool tsg;
		u32 id;
	} prev, next, *chan;
};

static void
gk104_fifo_engine_status(struct gk104_fifo *fifo, int engn,
			 struct gk104_fifo_engine_status *status)
{
	struct nvkm_engine *engine = fifo->engine[engn].engine;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x002640 + (engn * 0x08));

	status->busy     = !!(stat & 0x80000000);
	status->faulted  = !!(stat & 0x40000000);
	status->next.tsg = !!(stat & 0x10000000);
	status->next.id  =   (stat & 0x0fff0000) >> 16;
	status->chsw     = !!(stat & 0x00008000);
	status->save     = !!(stat & 0x00004000);
	status->load     = !!(stat & 0x00002000);
	status->prev.tsg = !!(stat & 0x00001000);
	status->prev.id  =   (stat & 0x00000fff);
	status->chan     = NULL;

	if (status->busy && status->chsw) {
		if (status->load && status->save) {
			if (engine && nvkm_engine_chsw_load(engine))
				status->chan = &status->next;
			else
				status->chan = &status->prev;
		} else
		if (status->load) {
			status->chan = &status->next;
		} else {
			status->chan = &status->prev;
		}
	} else
	if (status->load) {
		status->chan = &status->prev;
	}

	nvkm_debug(subdev, "engine %02d: busy %d faulted %d chsw %d "
			   "save %d load %d %sid %d%s-> %sid %d%s\n",
		   engn, status->busy, status->faulted,
		   status->chsw, status->save, status->load,
		   status->prev.tsg ? "tsg" : "ch", status->prev.id,
		   status->chan == &status->prev ? "*" : " ",
		   status->next.tsg ? "tsg" : "ch", status->next.id,
		   status->chan == &status->next ? "*" : " ");
}

static int
gk104_fifo_class_new(struct nvkm_fifo *base, const struct nvkm_oclass *oclass,
		     void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	if (oclass->engn == &fifo->func->chan) {
		const struct gk104_fifo_chan_user *user = oclass->engn;
		return user->ctor(fifo, oclass, argv, argc, pobject);
	} else
	if (oclass->engn == &fifo->func->user) {
		const struct gk104_fifo_user_user *user = oclass->engn;
		return user->ctor(oclass, argv, argc, pobject);
	}
	WARN_ON(1);
	return -EINVAL;
}

static int
gk104_fifo_class_get(struct nvkm_fifo *base, int index,
		     struct nvkm_oclass *oclass)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	int c = 0;

	if (fifo->func->user.ctor && c++ == index) {
		oclass->base =  fifo->func->user.user;
		oclass->engn = &fifo->func->user;
		return 0;
	}

	if (fifo->func->chan.ctor && c++ == index) {
		oclass->base =  fifo->func->chan.user;
		oclass->engn = &fifo->func->chan;
		return 0;
	}

	return c;
}

static void
gk104_fifo_uevent_fini(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x00000000);
}

static void
gk104_fifo_uevent_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x80000000);
}

void
gk104_fifo_runlist_commit(struct gk104_fifo *fifo, int runl,
			  struct nvkm_memory *mem, int nr)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int target;

	switch (nvkm_memory_target(mem)) {
	case NVKM_MEM_TARGET_VRAM: target = 0; break;
	case NVKM_MEM_TARGET_NCOH: target = 3; break;
	default:
		WARN_ON(1);
		return;
	}

	nvkm_wr32(device, 0x002270, (nvkm_memory_addr(mem) >> 12) |
				    (target << 28));
	nvkm_wr32(device, 0x002274, (runl << 20) | nr);

	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x002284 + (runl * 0x08)) & 0x00100000))
			break;
	) < 0)
		nvkm_error(subdev, "runlist %d update timeout\n", runl);
}

void
gk104_fifo_runlist_update(struct gk104_fifo *fifo, int runl)
{
	const struct gk104_fifo_runlist_func *func = fifo->func->runlist;
	struct gk104_fifo_chan *chan;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_memory *mem;
	struct nvkm_fifo_cgrp *cgrp;
	int nr = 0;

	mutex_lock(&subdev->mutex);
	mem = fifo->runlist[runl].mem[fifo->runlist[runl].next];
	fifo->runlist[runl].next = !fifo->runlist[runl].next;

	nvkm_kmap(mem);
	list_for_each_entry(chan, &fifo->runlist[runl].chan, head) {
		func->chan(chan, mem, nr++ * func->size);
	}

	list_for_each_entry(cgrp, &fifo->runlist[runl].cgrp, head) {
		func->cgrp(cgrp, mem, nr++ * func->size);
		list_for_each_entry(chan, &cgrp->chan, head) {
			func->chan(chan, mem, nr++ * func->size);
		}
	}
	nvkm_done(mem);

	func->commit(fifo, runl, mem, nr);
	mutex_unlock(&subdev->mutex);
}

void
gk104_fifo_runlist_remove(struct gk104_fifo *fifo, struct gk104_fifo_chan *chan)
{
	struct nvkm_fifo_cgrp *cgrp = chan->cgrp;
	mutex_lock(&fifo->base.engine.subdev.mutex);
	if (!list_empty(&chan->head)) {
		list_del_init(&chan->head);
		if (cgrp && !--cgrp->chan_nr)
			list_del_init(&cgrp->head);
	}
	mutex_unlock(&fifo->base.engine.subdev.mutex);
}

void
gk104_fifo_runlist_insert(struct gk104_fifo *fifo, struct gk104_fifo_chan *chan)
{
	struct nvkm_fifo_cgrp *cgrp = chan->cgrp;
	mutex_lock(&fifo->base.engine.subdev.mutex);
	if (cgrp) {
		if (!cgrp->chan_nr++)
			list_add_tail(&cgrp->head, &fifo->runlist[chan->runl].cgrp);
		list_add_tail(&chan->head, &cgrp->chan);
	} else {
		list_add_tail(&chan->head, &fifo->runlist[chan->runl].chan);
	}
	mutex_unlock(&fifo->base.engine.subdev.mutex);
}

void
gk104_fifo_runlist_chan(struct gk104_fifo_chan *chan,
			struct nvkm_memory *memory, u32 offset)
{
	nvkm_wo32(memory, offset + 0, chan->base.chid);
	nvkm_wo32(memory, offset + 4, 0x00000000);
}

const struct gk104_fifo_runlist_func
gk104_fifo_runlist = {
	.size = 8,
	.chan = gk104_fifo_runlist_chan,
	.commit = gk104_fifo_runlist_commit,
};

void
gk104_fifo_pbdma_init(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	nvkm_wr32(device, 0x000204, (1 << fifo->pbdma_nr) - 1);
}

int
gk104_fifo_pbdma_nr(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	/* Determine number of PBDMAs by checking valid enable bits. */
	nvkm_wr32(device, 0x000204, 0xffffffff);
	return hweight32(nvkm_rd32(device, 0x000204));
}

const struct gk104_fifo_pbdma_func
gk104_fifo_pbdma = {
	.nr = gk104_fifo_pbdma_nr,
	.init = gk104_fifo_pbdma_init,
};

static void
gk104_fifo_recover_work(struct work_struct *w)
{
	struct gk104_fifo *fifo = container_of(w, typeof(*fifo), recover.work);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_engine *engine;
	unsigned long flags;
	u32 engm, runm, todo;
	int engn, runl;

	spin_lock_irqsave(&fifo->base.lock, flags);
	runm = fifo->recover.runm;
	engm = fifo->recover.engm;
	fifo->recover.engm = 0;
	fifo->recover.runm = 0;
	spin_unlock_irqrestore(&fifo->base.lock, flags);

	nvkm_mask(device, 0x002630, runm, runm);

	for (todo = engm; engn = __ffs(todo), todo; todo &= ~BIT(engn)) {
		if ((engine = fifo->engine[engn].engine)) {
			nvkm_subdev_fini(&engine->subdev, false);
			WARN_ON(nvkm_subdev_init(&engine->subdev));
		}
	}

	for (todo = runm; runl = __ffs(todo), todo; todo &= ~BIT(runl))
		gk104_fifo_runlist_update(fifo, runl);

	nvkm_wr32(device, 0x00262c, runm);
	nvkm_mask(device, 0x002630, runm, 0x00000000);
}

static void gk104_fifo_recover_engn(struct gk104_fifo *fifo, int engn);

static void
gk104_fifo_recover_runl(struct gk104_fifo *fifo, int runl)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 runm = BIT(runl);

	assert_spin_locked(&fifo->base.lock);
	if (fifo->recover.runm & runm)
		return;
	fifo->recover.runm |= runm;

	/* Block runlist to prevent channel assignment(s) from changing. */
	nvkm_mask(device, 0x002630, runm, runm);

	/* Schedule recovery. */
	nvkm_warn(subdev, "runlist %d: scheduled for recovery\n", runl);
	schedule_work(&fifo->recover.work);
}

static struct gk104_fifo_chan *
gk104_fifo_recover_chid(struct gk104_fifo *fifo, int runl, int chid)
{
	struct gk104_fifo_chan *chan;
	struct nvkm_fifo_cgrp *cgrp;

	list_for_each_entry(chan, &fifo->runlist[runl].chan, head) {
		if (chan->base.chid == chid) {
			list_del_init(&chan->head);
			return chan;
		}
	}

	list_for_each_entry(cgrp, &fifo->runlist[runl].cgrp, head) {
		if (cgrp->id == chid) {
			chan = list_first_entry(&cgrp->chan, typeof(*chan), head);
			list_del_init(&chan->head);
			if (!--cgrp->chan_nr)
				list_del_init(&cgrp->head);
			return chan;
		}
	}

	return NULL;
}

static void
gk104_fifo_recover_chan(struct nvkm_fifo *base, int chid)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32  stat = nvkm_rd32(device, 0x800004 + (chid * 0x08));
	const u32  runl = (stat & 0x000f0000) >> 16;
	const bool used = (stat & 0x00000001);
	unsigned long engn, engm = fifo->runlist[runl].engm;
	struct gk104_fifo_chan *chan;

	assert_spin_locked(&fifo->base.lock);
	if (!used)
		return;

	/* Lookup SW state for channel, and mark it as dead. */
	chan = gk104_fifo_recover_chid(fifo, runl, chid);
	if (chan) {
		chan->killed = true;
		nvkm_fifo_kevent(&fifo->base, chid);
	}

	/* Disable channel. */
	nvkm_wr32(device, 0x800004 + (chid * 0x08), stat | 0x00000800);
	nvkm_warn(subdev, "channel %d: killed\n", chid);

	/* Block channel assignments from changing during recovery. */
	gk104_fifo_recover_runl(fifo, runl);

	/* Schedule recovery for any engines the channel is on. */
	for_each_set_bit(engn, &engm, fifo->engine_nr) {
		struct gk104_fifo_engine_status status;
		gk104_fifo_engine_status(fifo, engn, &status);
		if (!status.chan || status.chan->id != chid)
			continue;
		gk104_fifo_recover_engn(fifo, engn);
	}
}

static void
gk104_fifo_recover_engn(struct gk104_fifo *fifo, int engn)
{
	struct nvkm_engine *engine = fifo->engine[engn].engine;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 runl = fifo->engine[engn].runl;
	const u32 engm = BIT(engn);
	struct gk104_fifo_engine_status status;
	int mmui = -1;

	assert_spin_locked(&fifo->base.lock);
	if (fifo->recover.engm & engm)
		return;
	fifo->recover.engm |= engm;

	/* Block channel assignments from changing during recovery. */
	gk104_fifo_recover_runl(fifo, runl);

	/* Determine which channel (if any) is currently on the engine. */
	gk104_fifo_engine_status(fifo, engn, &status);
	if (status.chan) {
		/* The channel is not longer viable, kill it. */
		gk104_fifo_recover_chan(&fifo->base, status.chan->id);
	}

	/* Determine MMU fault ID for the engine, if we're not being
	 * called from the fault handler already.
	 */
	if (!status.faulted && engine) {
		mmui = nvkm_top_fault_id(device, engine->subdev.index);
		if (mmui < 0) {
			const struct nvkm_enum *en = fifo->func->fault.engine;
			for (; en && en->name; en++) {
				if (en->data2 == engine->subdev.index) {
					mmui = en->value;
					break;
				}
			}
		}
		WARN_ON(mmui < 0);
	}

	/* Trigger a MMU fault for the engine.
	 *
	 * No good idea why this is needed, but nvgpu does something similar,
	 * and it makes recovery from CTXSW_TIMEOUT a lot more reliable.
	 */
	if (mmui >= 0) {
		nvkm_wr32(device, 0x002a30 + (engn * 0x04), 0x00000100 | mmui);

		/* Wait for fault to trigger. */
		nvkm_msec(device, 2000,
			gk104_fifo_engine_status(fifo, engn, &status);
			if (status.faulted)
				break;
		);

		/* Release MMU fault trigger, and ACK the fault. */
		nvkm_wr32(device, 0x002a30 + (engn * 0x04), 0x00000000);
		nvkm_wr32(device, 0x00259c, BIT(mmui));
		nvkm_wr32(device, 0x002100, 0x10000000);
	}

	/* Schedule recovery. */
	nvkm_warn(subdev, "engine %d: scheduled for recovery\n", engn);
	schedule_work(&fifo->recover.work);
}

static void
gk104_fifo_fault(struct nvkm_fifo *base, struct nvkm_fault_data *info)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const struct nvkm_enum *er, *ee, *ec, *ea;
	struct nvkm_engine *engine = NULL;
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	char ct[8] = "HUB/", en[16] = "";
	int engn;

	er = nvkm_enum_find(fifo->func->fault.reason, info->reason);
	ee = nvkm_enum_find(fifo->func->fault.engine, info->engine);
	if (info->hub) {
		ec = nvkm_enum_find(fifo->func->fault.hubclient, info->client);
	} else {
		ec = nvkm_enum_find(fifo->func->fault.gpcclient, info->client);
		snprintf(ct, sizeof(ct), "GPC%d/", info->gpc);
	}
	ea = nvkm_enum_find(fifo->func->fault.access, info->access);

	if (ee && ee->data2) {
		switch (ee->data2) {
		case NVKM_SUBDEV_BAR:
			nvkm_bar_bar1_reset(device);
			break;
		case NVKM_SUBDEV_INSTMEM:
			nvkm_bar_bar2_reset(device);
			break;
		case NVKM_ENGINE_IFB:
			nvkm_mask(device, 0x001718, 0x00000000, 0x00000000);
			break;
		default:
			engine = nvkm_device_engine(device, ee->data2);
			break;
		}
	}

	if (ee == NULL) {
		enum nvkm_devidx engidx = nvkm_top_fault(device, info->engine);
		if (engidx < NVKM_SUBDEV_NR) {
			const char *src = nvkm_subdev_name[engidx];
			char *dst = en;
			do {
				*dst++ = toupper(*src++);
			} while(*src);
			engine = nvkm_device_engine(device, engidx);
		}
	} else {
		snprintf(en, sizeof(en), "%s", ee->name);
	}

	spin_lock_irqsave(&fifo->base.lock, flags);
	chan = nvkm_fifo_chan_inst_locked(&fifo->base, info->inst);

	nvkm_error(subdev,
		   "fault %02x [%s] at %016llx engine %02x [%s] client %02x "
		   "[%s%s] reason %02x [%s] on channel %d [%010llx %s]\n",
		   info->access, ea ? ea->name : "", info->addr,
		   info->engine, ee ? ee->name : en,
		   info->client, ct, ec ? ec->name : "",
		   info->reason, er ? er->name : "", chan ? chan->chid : -1,
		   info->inst, chan ? chan->object.client->name : "unknown");

	/* Kill the channel that caused the fault. */
	if (chan)
		gk104_fifo_recover_chan(&fifo->base, chan->chid);

	/* Channel recovery will probably have already done this for the
	 * correct engine(s), but just in case we can't find the channel
	 * information...
	 */
	for (engn = 0; engn < fifo->engine_nr && engine; engn++) {
		if (fifo->engine[engn].engine == engine) {
			gk104_fifo_recover_engn(fifo, engn);
			break;
		}
	}

	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

static const struct nvkm_enum
gk104_fifo_bind_reason[] = {
	{ 0x01, "BIND_NOT_UNBOUND" },
	{ 0x02, "SNOOP_WITHOUT_BAR1" },
	{ 0x03, "UNBIND_WHILE_RUNNING" },
	{ 0x05, "INVALID_RUNLIST" },
	{ 0x06, "INVALID_CTX_TGT" },
	{ 0x0b, "UNBIND_WHILE_PARKED" },
	{}
};

static void
gk104_fifo_intr_bind(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x00252c);
	u32 code = intr & 0x000000ff;
	const struct nvkm_enum *en =
		nvkm_enum_find(gk104_fifo_bind_reason, code);

	nvkm_error(subdev, "BIND_ERROR %02x [%s]\n", code, en ? en->name : "");
}

static const struct nvkm_enum
gk104_fifo_sched_reason[] = {
	{ 0x0a, "CTXSW_TIMEOUT" },
	{}
};

static void
gk104_fifo_intr_sched_ctxsw(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	unsigned long flags, engm = 0;
	u32 engn;

	/* We need to ACK the SCHED_ERROR here, and prevent it reasserting,
	 * as MMU_FAULT cannot be triggered while it's pending.
	 */
	spin_lock_irqsave(&fifo->base.lock, flags);
	nvkm_mask(device, 0x002140, 0x00000100, 0x00000000);
	nvkm_wr32(device, 0x002100, 0x00000100);

	for (engn = 0; engn < fifo->engine_nr; engn++) {
		struct gk104_fifo_engine_status status;

		gk104_fifo_engine_status(fifo, engn, &status);
		if (!status.busy || !status.chsw)
			continue;

		engm |= BIT(engn);
	}

	for_each_set_bit(engn, &engm, fifo->engine_nr)
		gk104_fifo_recover_engn(fifo, engn);

	nvkm_mask(device, 0x002140, 0x00000100, 0x00000100);
	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

static void
gk104_fifo_intr_sched(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x00254c);
	u32 code = intr & 0x000000ff;
	const struct nvkm_enum *en =
		nvkm_enum_find(gk104_fifo_sched_reason, code);

	nvkm_error(subdev, "SCHED_ERROR %02x [%s]\n", code, en ? en->name : "");

	switch (code) {
	case 0x0a:
		gk104_fifo_intr_sched_ctxsw(fifo);
		break;
	default:
		break;
	}
}

static void
gk104_fifo_intr_chsw(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x00256c);
	nvkm_error(subdev, "CHSW_ERROR %08x\n", stat);
	nvkm_wr32(device, 0x00256c, stat);
}

static void
gk104_fifo_intr_dropped_fault(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x00259c);
	nvkm_error(subdev, "DROPPED_MMU_FAULT %08x\n", stat);
}

static const struct nvkm_bitfield gk104_fifo_pbdma_intr_0[] = {
	{ 0x00000001, "MEMREQ" },
	{ 0x00000002, "MEMACK_TIMEOUT" },
	{ 0x00000004, "MEMACK_EXTRA" },
	{ 0x00000008, "MEMDAT_TIMEOUT" },
	{ 0x00000010, "MEMDAT_EXTRA" },
	{ 0x00000020, "MEMFLUSH" },
	{ 0x00000040, "MEMOP" },
	{ 0x00000080, "LBCONNECT" },
	{ 0x00000100, "LBREQ" },
	{ 0x00000200, "LBACK_TIMEOUT" },
	{ 0x00000400, "LBACK_EXTRA" },
	{ 0x00000800, "LBDAT_TIMEOUT" },
	{ 0x00001000, "LBDAT_EXTRA" },
	{ 0x00002000, "GPFIFO" },
	{ 0x00004000, "GPPTR" },
	{ 0x00008000, "GPENTRY" },
	{ 0x00010000, "GPCRC" },
	{ 0x00020000, "PBPTR" },
	{ 0x00040000, "PBENTRY" },
	{ 0x00080000, "PBCRC" },
	{ 0x00100000, "XBARCONNECT" },
	{ 0x00200000, "METHOD" },
	{ 0x00400000, "METHODCRC" },
	{ 0x00800000, "DEVICE" },
	{ 0x02000000, "SEMAPHORE" },
	{ 0x04000000, "ACQUIRE" },
	{ 0x08000000, "PRI" },
	{ 0x20000000, "NO_CTXSW_SEG" },
	{ 0x40000000, "PBSEG" },
	{ 0x80000000, "SIGNATURE" },
	{}
};

static void
gk104_fifo_intr_pbdma_0(struct gk104_fifo *fifo, int unit)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x04010c + (unit * 0x2000));
	u32 stat = nvkm_rd32(device, 0x040108 + (unit * 0x2000)) & mask;
	u32 addr = nvkm_rd32(device, 0x0400c0 + (unit * 0x2000));
	u32 data = nvkm_rd32(device, 0x0400c4 + (unit * 0x2000));
	u32 chid = nvkm_rd32(device, 0x040120 + (unit * 0x2000)) & 0xfff;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	char msg[128];

	if (stat & 0x00800000) {
		if (device->sw) {
			if (nvkm_sw_mthd(device->sw, chid, subc, mthd, data))
				show &= ~0x00800000;
		}
	}

	nvkm_wr32(device, 0x0400c0 + (unit * 0x2000), 0x80600008);

	if (show) {
		nvkm_snprintbf(msg, sizeof(msg), gk104_fifo_pbdma_intr_0, show);
		chan = nvkm_fifo_chan_chid(&fifo->base, chid, &flags);
		nvkm_error(subdev, "PBDMA%d: %08x [%s] ch %d [%010llx %s] "
				   "subc %d mthd %04x data %08x\n",
			   unit, show, msg, chid, chan ? chan->inst->addr : 0,
			   chan ? chan->object.client->name : "unknown",
			   subc, mthd, data);
		nvkm_fifo_chan_put(&fifo->base, flags, &chan);
	}

	nvkm_wr32(device, 0x040108 + (unit * 0x2000), stat);
}

static const struct nvkm_bitfield gk104_fifo_pbdma_intr_1[] = {
	{ 0x00000001, "HCE_RE_ILLEGAL_OP" },
	{ 0x00000002, "HCE_RE_ALIGNB" },
	{ 0x00000004, "HCE_PRIV" },
	{ 0x00000008, "HCE_ILLEGAL_MTHD" },
	{ 0x00000010, "HCE_ILLEGAL_CLASS" },
	{}
};

static void
gk104_fifo_intr_pbdma_1(struct gk104_fifo *fifo, int unit)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x04014c + (unit * 0x2000));
	u32 stat = nvkm_rd32(device, 0x040148 + (unit * 0x2000)) & mask;
	u32 chid = nvkm_rd32(device, 0x040120 + (unit * 0x2000)) & 0xfff;
	char msg[128];

	if (stat) {
		nvkm_snprintbf(msg, sizeof(msg), gk104_fifo_pbdma_intr_1, stat);
		nvkm_error(subdev, "PBDMA%d: %08x [%s] ch %d %08x %08x\n",
			   unit, stat, msg, chid,
			   nvkm_rd32(device, 0x040150 + (unit * 0x2000)),
			   nvkm_rd32(device, 0x040154 + (unit * 0x2000)));
	}

	nvkm_wr32(device, 0x040148 + (unit * 0x2000), stat);
}

static void
gk104_fifo_intr_runlist(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 mask = nvkm_rd32(device, 0x002a00);
	while (mask) {
		int runl = __ffs(mask);
		wake_up(&fifo->runlist[runl].wait);
		nvkm_wr32(device, 0x002a00, 1 << runl);
		mask &= ~(1 << runl);
	}
}

static void
gk104_fifo_intr_engine(struct gk104_fifo *fifo)
{
	nvkm_fifo_uevent(&fifo->base);
}

static void
gk104_fifo_intr(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x002140);
	u32 stat = nvkm_rd32(device, 0x002100) & mask;

	if (stat & 0x00000001) {
		gk104_fifo_intr_bind(fifo);
		nvkm_wr32(device, 0x002100, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000010) {
		nvkm_error(subdev, "PIO_ERROR\n");
		nvkm_wr32(device, 0x002100, 0x00000010);
		stat &= ~0x00000010;
	}

	if (stat & 0x00000100) {
		gk104_fifo_intr_sched(fifo);
		nvkm_wr32(device, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x00010000) {
		gk104_fifo_intr_chsw(fifo);
		nvkm_wr32(device, 0x002100, 0x00010000);
		stat &= ~0x00010000;
	}

	if (stat & 0x00800000) {
		nvkm_error(subdev, "FB_FLUSH_TIMEOUT\n");
		nvkm_wr32(device, 0x002100, 0x00800000);
		stat &= ~0x00800000;
	}

	if (stat & 0x01000000) {
		nvkm_error(subdev, "LB_ERROR\n");
		nvkm_wr32(device, 0x002100, 0x01000000);
		stat &= ~0x01000000;
	}

	if (stat & 0x08000000) {
		gk104_fifo_intr_dropped_fault(fifo);
		nvkm_wr32(device, 0x002100, 0x08000000);
		stat &= ~0x08000000;
	}

	if (stat & 0x10000000) {
		u32 mask = nvkm_rd32(device, 0x00259c);
		while (mask) {
			u32 unit = __ffs(mask);
			fifo->func->intr.fault(&fifo->base, unit);
			nvkm_wr32(device, 0x00259c, (1 << unit));
			mask &= ~(1 << unit);
		}
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 mask = nvkm_rd32(device, 0x0025a0);
		while (mask) {
			u32 unit = __ffs(mask);
			gk104_fifo_intr_pbdma_0(fifo, unit);
			gk104_fifo_intr_pbdma_1(fifo, unit);
			nvkm_wr32(device, 0x0025a0, (1 << unit));
			mask &= ~(1 << unit);
		}
		stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		gk104_fifo_intr_runlist(fifo);
		stat &= ~0x40000000;
	}

	if (stat & 0x80000000) {
		nvkm_wr32(device, 0x002100, 0x80000000);
		gk104_fifo_intr_engine(fifo);
		stat &= ~0x80000000;
	}

	if (stat) {
		nvkm_error(subdev, "INTR %08x\n", stat);
		nvkm_mask(device, 0x002140, stat, 0x00000000);
		nvkm_wr32(device, 0x002100, stat);
	}
}

static void
gk104_fifo_fini(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	flush_work(&fifo->recover.work);
	/* allow mmu fault interrupts, even when we're not using fifo */
	nvkm_mask(device, 0x002140, 0x10000000, 0x10000000);
}

static int
gk104_fifo_info(struct nvkm_fifo *base, u64 mthd, u64 *data)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	switch (mthd) {
	case NV_DEVICE_FIFO_RUNLISTS:
		*data = (1ULL << fifo->runlist_nr) - 1;
		return 0;
	case NV_DEVICE_FIFO_RUNLIST_ENGINES(0)...
	     NV_DEVICE_FIFO_RUNLIST_ENGINES(63): {
		int runl = mthd - NV_DEVICE_FIFO_RUNLIST_ENGINES(0), engn;
		if (runl < fifo->runlist_nr) {
			unsigned long engm = fifo->runlist[runl].engm;
			struct nvkm_engine *engine;
			*data = 0;
			for_each_set_bit(engn, &engm, fifo->engine_nr) {
				if ((engine = fifo->engine[engn].engine))
					*data |= BIT_ULL(engine->subdev.index);
			}
			return 0;
		}
	}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int
gk104_fifo_oneinit(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_vmm *bar = nvkm_bar_bar1_vmm(device);
	int engn, runl, pbid, ret, i, j;
	enum nvkm_devidx engidx;
	u32 *map;

	fifo->pbdma_nr = fifo->func->pbdma->nr(fifo);
	nvkm_debug(subdev, "%d PBDMA(s)\n", fifo->pbdma_nr);

	/* Read PBDMA->runlist(s) mapping from HW. */
	if (!(map = kcalloc(fifo->pbdma_nr, sizeof(*map), GFP_KERNEL)))
		return -ENOMEM;

	for (i = 0; i < fifo->pbdma_nr; i++)
		map[i] = nvkm_rd32(device, 0x002390 + (i * 0x04));

	/* Determine runlist configuration from topology device info. */
	i = 0;
	while ((int)(engidx = nvkm_top_engine(device, i++, &runl, &engn)) >= 0) {
		/* Determine which PBDMA handles requests for this engine. */
		for (j = 0, pbid = -1; j < fifo->pbdma_nr; j++) {
			if (map[j] & (1 << runl)) {
				pbid = j;
				break;
			}
		}

		nvkm_debug(subdev, "engine %2d: runlist %2d pbdma %2d (%s)\n",
			   engn, runl, pbid, nvkm_subdev_name[engidx]);

		fifo->engine[engn].engine = nvkm_device_engine(device, engidx);
		fifo->engine[engn].runl = runl;
		fifo->engine[engn].pbid = pbid;
		fifo->engine_nr = max(fifo->engine_nr, engn + 1);
		fifo->runlist[runl].engm |= 1 << engn;
		fifo->runlist_nr = max(fifo->runlist_nr, runl + 1);
	}

	kfree(map);

	for (i = 0; i < fifo->runlist_nr; i++) {
		for (j = 0; j < ARRAY_SIZE(fifo->runlist[i].mem); j++) {
			ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
					      fifo->base.nr * 2/* TSG+chan */ *
					      fifo->func->runlist->size,
					      0x1000, false,
					      &fifo->runlist[i].mem[j]);
			if (ret)
				return ret;
		}

		init_waitqueue_head(&fifo->runlist[i].wait);
		INIT_LIST_HEAD(&fifo->runlist[i].cgrp);
		INIT_LIST_HEAD(&fifo->runlist[i].chan);
	}

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
			      fifo->base.nr * 0x200, 0x1000, true,
			      &fifo->user.mem);
	if (ret)
		return ret;

	ret = nvkm_vmm_get(bar, 12, nvkm_memory_size(fifo->user.mem),
			   &fifo->user.bar);
	if (ret)
		return ret;

	return nvkm_memory_map(fifo->user.mem, 0, bar, fifo->user.bar, NULL, 0);
}

static void
gk104_fifo_init(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int i;

	/* Enable PBDMAs. */
	fifo->func->pbdma->init(fifo);

	/* PBDMA[n] */
	for (i = 0; i < fifo->pbdma_nr; i++) {
		nvkm_mask(device, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nvkm_wr32(device, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nvkm_wr32(device, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTREN */
	}

	/* PBDMA[n].HCE */
	for (i = 0; i < fifo->pbdma_nr; i++) {
		nvkm_wr32(device, 0x040148 + (i * 0x2000), 0xffffffff); /* INTR */
		nvkm_wr32(device, 0x04014c + (i * 0x2000), 0xffffffff); /* INTREN */
	}

	nvkm_wr32(device, 0x002254, 0x10000000 | fifo->user.bar->addr >> 12);

	if (fifo->func->pbdma->init_timeout)
		fifo->func->pbdma->init_timeout(fifo);

	nvkm_wr32(device, 0x002100, 0xffffffff);
	nvkm_wr32(device, 0x002140, 0x7fffffff);
}

static void *
gk104_fifo_dtor(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int i;

	nvkm_vmm_put(nvkm_bar_bar1_vmm(device), &fifo->user.bar);
	nvkm_memory_unref(&fifo->user.mem);

	for (i = 0; i < fifo->runlist_nr; i++) {
		nvkm_memory_unref(&fifo->runlist[i].mem[1]);
		nvkm_memory_unref(&fifo->runlist[i].mem[0]);
	}

	return fifo;
}

static const struct nvkm_fifo_func
gk104_fifo_ = {
	.dtor = gk104_fifo_dtor,
	.oneinit = gk104_fifo_oneinit,
	.info = gk104_fifo_info,
	.init = gk104_fifo_init,
	.fini = gk104_fifo_fini,
	.intr = gk104_fifo_intr,
	.fault = gk104_fifo_fault,
	.uevent_init = gk104_fifo_uevent_init,
	.uevent_fini = gk104_fifo_uevent_fini,
	.recover_chan = gk104_fifo_recover_chan,
	.class_get = gk104_fifo_class_get,
	.class_new = gk104_fifo_class_new,
};

int
gk104_fifo_new_(const struct gk104_fifo_func *func, struct nvkm_device *device,
		int index, int nr, struct nvkm_fifo **pfifo)
{
	struct gk104_fifo *fifo;

	if (!(fifo = kzalloc(sizeof(*fifo), GFP_KERNEL)))
		return -ENOMEM;
	fifo->func = func;
	INIT_WORK(&fifo->recover.work, gk104_fifo_recover_work);
	*pfifo = &fifo->base;

	return nvkm_fifo_ctor(&gk104_fifo_, device, index, nr, &fifo->base);
}

const struct nvkm_enum
gk104_fifo_fault_access[] = {
	{ 0x0, "READ" },
	{ 0x1, "WRITE" },
	{}
};

const struct nvkm_enum
gk104_fifo_fault_engine[] = {
	{ 0x00, "GR", NULL, NVKM_ENGINE_GR },
	{ 0x01, "DISPLAY" },
	{ 0x02, "CAPTURE" },
	{ 0x03, "IFB", NULL, NVKM_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0x05, "BAR2", NULL, NVKM_SUBDEV_INSTMEM },
	{ 0x06, "SCHED" },
	{ 0x07, "HOST0", NULL, NVKM_ENGINE_FIFO },
	{ 0x08, "HOST1", NULL, NVKM_ENGINE_FIFO },
	{ 0x09, "HOST2", NULL, NVKM_ENGINE_FIFO },
	{ 0x0a, "HOST3", NULL, NVKM_ENGINE_FIFO },
	{ 0x0b, "HOST4", NULL, NVKM_ENGINE_FIFO },
	{ 0x0c, "HOST5", NULL, NVKM_ENGINE_FIFO },
	{ 0x0d, "HOST6", NULL, NVKM_ENGINE_FIFO },
	{ 0x0e, "HOST7", NULL, NVKM_ENGINE_FIFO },
	{ 0x0f, "HOSTSR" },
	{ 0x10, "MSVLD", NULL, NVKM_ENGINE_MSVLD },
	{ 0x11, "MSPPP", NULL, NVKM_ENGINE_MSPPP },
	{ 0x13, "PERF" },
	{ 0x14, "MSPDEC", NULL, NVKM_ENGINE_MSPDEC },
	{ 0x15, "CE0", NULL, NVKM_ENGINE_CE0 },
	{ 0x16, "CE1", NULL, NVKM_ENGINE_CE1 },
	{ 0x17, "PMU" },
	{ 0x18, "PTP" },
	{ 0x19, "MSENC", NULL, NVKM_ENGINE_MSENC },
	{ 0x1b, "CE2", NULL, NVKM_ENGINE_CE2 },
	{}
};

const struct nvkm_enum
gk104_fifo_fault_reason[] = {
	{ 0x00, "PDE" },
	{ 0x01, "PDE_SIZE" },
	{ 0x02, "PTE" },
	{ 0x03, "VA_LIMIT_VIOLATION" },
	{ 0x04, "UNBOUND_INST_BLOCK" },
	{ 0x05, "PRIV_VIOLATION" },
	{ 0x06, "RO_VIOLATION" },
	{ 0x07, "WO_VIOLATION" },
	{ 0x08, "PITCH_MASK_VIOLATION" },
	{ 0x09, "WORK_CREATION" },
	{ 0x0a, "UNSUPPORTED_APERTURE" },
	{ 0x0b, "COMPRESSION_FAILURE" },
	{ 0x0c, "UNSUPPORTED_KIND" },
	{ 0x0d, "REGION_VIOLATION" },
	{ 0x0e, "BOTH_PTES_VALID" },
	{ 0x0f, "INFO_TYPE_POISONED" },
	{}
};

const struct nvkm_enum
gk104_fifo_fault_hubclient[] = {
	{ 0x00, "VIP" },
	{ 0x01, "CE0" },
	{ 0x02, "CE1" },
	{ 0x03, "DNISO" },
	{ 0x04, "FE" },
	{ 0x05, "FECS" },
	{ 0x06, "HOST" },
	{ 0x07, "HOST_CPU" },
	{ 0x08, "HOST_CPU_NB" },
	{ 0x09, "ISO" },
	{ 0x0a, "MMU" },
	{ 0x0b, "MSPDEC" },
	{ 0x0c, "MSPPP" },
	{ 0x0d, "MSVLD" },
	{ 0x0e, "NISO" },
	{ 0x0f, "P2P" },
	{ 0x10, "PD" },
	{ 0x11, "PERF" },
	{ 0x12, "PMU" },
	{ 0x13, "RASTERTWOD" },
	{ 0x14, "SCC" },
	{ 0x15, "SCC_NB" },
	{ 0x16, "SEC" },
	{ 0x17, "SSYNC" },
	{ 0x18, "GR_CE" },
	{ 0x19, "CE2" },
	{ 0x1a, "XV" },
	{ 0x1b, "MMU_NB" },
	{ 0x1c, "MSENC" },
	{ 0x1d, "DFALCON" },
	{ 0x1e, "SKED" },
	{ 0x1f, "AFALCON" },
	{}
};

const struct nvkm_enum
gk104_fifo_fault_gpcclient[] = {
	{ 0x00, "L1_0" }, { 0x01, "T1_0" }, { 0x02, "PE_0" },
	{ 0x03, "L1_1" }, { 0x04, "T1_1" }, { 0x05, "PE_1" },
	{ 0x06, "L1_2" }, { 0x07, "T1_2" }, { 0x08, "PE_2" },
	{ 0x09, "L1_3" }, { 0x0a, "T1_3" }, { 0x0b, "PE_3" },
	{ 0x0c, "RAST" },
	{ 0x0d, "GCC" },
	{ 0x0e, "GPCCS" },
	{ 0x0f, "PROP_0" },
	{ 0x10, "PROP_1" },
	{ 0x11, "PROP_2" },
	{ 0x12, "PROP_3" },
	{ 0x13, "L1_4" }, { 0x14, "T1_4" }, { 0x15, "PE_4" },
	{ 0x16, "L1_5" }, { 0x17, "T1_5" }, { 0x18, "PE_5" },
	{ 0x19, "L1_6" }, { 0x1a, "T1_6" }, { 0x1b, "PE_6" },
	{ 0x1c, "L1_7" }, { 0x1d, "T1_7" }, { 0x1e, "PE_7" },
	{ 0x1f, "GPM" },
	{ 0x20, "LTP_UTLB_0" },
	{ 0x21, "LTP_UTLB_1" },
	{ 0x22, "LTP_UTLB_2" },
	{ 0x23, "LTP_UTLB_3" },
	{ 0x24, "GPC_RGG_UTLB" },
	{}
};

static const struct gk104_fifo_func
gk104_fifo = {
	.intr.fault = gf100_fifo_intr_fault,
	.pbdma = &gk104_fifo_pbdma,
	.fault.access = gk104_fifo_fault_access,
	.fault.engine = gk104_fifo_fault_engine,
	.fault.reason = gk104_fifo_fault_reason,
	.fault.hubclient = gk104_fifo_fault_hubclient,
	.fault.gpcclient = gk104_fifo_fault_gpcclient,
	.runlist = &gk104_fifo_runlist,
	.chan = {{0,0,KEPLER_CHANNEL_GPFIFO_A}, gk104_fifo_gpfifo_new },
};

int
gk104_fifo_new(struct nvkm_device *device, int index, struct nvkm_fifo **pfifo)
{
	return gk104_fifo_new_(&gk104_fifo, device, index, 4096, pfifo);
}
