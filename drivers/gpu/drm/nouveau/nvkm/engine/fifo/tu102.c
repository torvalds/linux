/*
 * Copyright 2018 Red Hat Inc.
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
#include "runl.h"

#include "gk104.h"
#include "cgrp.h"
#include "changk104.h"

#include <core/memory.h>
#include <subdev/mc.h>
#include <subdev/vfn.h>

#include <nvif/class.h>

static u32
tu102_chan_doorbell_handle(struct nvkm_chan *chan)
{
	return (chan->cgrp->runl->id << 16) | chan->id;
}

static void
tu102_chan_start(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	gk104_chan_start(chan);
	nvkm_wr32(device, device->vfn->addr.user + 0x0090, chan->func->doorbell_handle(chan));
}

static const struct nvkm_chan_func
tu102_chan = {
	.bind = gk104_chan_bind_inst,
	.unbind = gk104_chan_unbind,
	.start = tu102_chan_start,
	.stop = gk104_chan_stop,
	.doorbell_handle = tu102_chan_doorbell_handle,
};

static bool
tu102_runl_pending(struct nvkm_runl *runl)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;

	return nvkm_rd32(device, 0x002b0c + (runl->id * 0x10)) & 0x00008000;
}

static void
tu102_fifo_runlist_commit(struct gk104_fifo *fifo, int runl,
			  struct nvkm_memory *mem, int nr)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u64 addr = nvkm_memory_addr(mem);
	/*XXX: target? */

	nvkm_wr32(device, 0x002b00 + (runl * 0x10), lower_32_bits(addr));
	nvkm_wr32(device, 0x002b04 + (runl * 0x10), upper_32_bits(addr));
	nvkm_wr32(device, 0x002b08 + (runl * 0x10), nr);
}

static const struct gk104_fifo_runlist_func
tu102_fifo_runlist = {
	.size = 16,
	.cgrp = gv100_fifo_runlist_cgrp,
	.chan = gv100_fifo_runlist_chan,
	.commit = tu102_fifo_runlist_commit,
};

static const struct nvkm_runl_func
tu102_runl = {
	.wait = nv50_runl_wait,
	.pending = tu102_runl_pending,
	.block = gk104_runl_block,
	.allow = gk104_runl_allow,
};

static const struct nvkm_enum
tu102_fifo_mmu_fault_engine[] = {
	{ 0x01, "DISPLAY" },
	{ 0x03, "PTP" },
	{ 0x06, "PWR_PMU" },
	{ 0x08, "IFB", NULL, NVKM_ENGINE_IFB },
	{ 0x09, "PERF" },
	{ 0x1f, "PHYSICAL" },
	{ 0x20, "HOST0" },
	{ 0x21, "HOST1" },
	{ 0x22, "HOST2" },
	{ 0x23, "HOST3" },
	{ 0x24, "HOST4" },
	{ 0x25, "HOST5" },
	{ 0x26, "HOST6" },
	{ 0x27, "HOST7" },
	{ 0x28, "HOST8" },
	{ 0x29, "HOST9" },
	{ 0x2a, "HOST10" },
	{ 0x2b, "HOST11" },
	{ 0x2c, "HOST12" },
	{ 0x2d, "HOST13" },
	{ 0x2e, "HOST14" },
	{ 0x80, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0xc0, "BAR2", NULL, NVKM_SUBDEV_INSTMEM },
	{}
};

static void
tu102_fifo_recover_work(struct work_struct *w)
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

	nvkm_mask(device, 0x002630, runm, 0x00000000);
}

static void tu102_fifo_recover_engn(struct gk104_fifo *fifo, int engn);

static void
tu102_fifo_recover_runl(struct gk104_fifo *fifo, int runl)
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
tu102_fifo_recover_chid(struct gk104_fifo *fifo, int runl, int chid)
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
tu102_fifo_recover_chan(struct nvkm_fifo *base, int chid)
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
	chan = tu102_fifo_recover_chid(fifo, runl, chid);
	if (chan) {
		chan->killed = true;
		nvkm_chan_error(&chan->base, false);
	}

	/* Block channel assignments from changing during recovery. */
	tu102_fifo_recover_runl(fifo, runl);

	/* Schedule recovery for any engines the channel is on. */
	for_each_set_bit(engn, &engm, fifo->engine_nr) {
		struct gk104_fifo_engine_status status;

		gk104_fifo_engine_status(fifo, engn, &status);
		if (!status.chan || status.chan->id != chid)
			continue;
		tu102_fifo_recover_engn(fifo, engn);
	}
}

static void
tu102_fifo_recover_engn(struct gk104_fifo *fifo, int engn)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 runl = fifo->engine[engn].runl;
	const u32 engm = BIT(engn);
	struct gk104_fifo_engine_status status;

	assert_spin_locked(&fifo->base.lock);
	if (fifo->recover.engm & engm)
		return;
	fifo->recover.engm |= engm;

	/* Block channel assignments from changing during recovery. */
	tu102_fifo_recover_runl(fifo, runl);

	/* Determine which channel (if any) is currently on the engine. */
	gk104_fifo_engine_status(fifo, engn, &status);
	if (status.chan) {
		/* The channel is not longer viable, kill it. */
		tu102_fifo_recover_chan(&fifo->base, status.chan->id);
	}

	/* Preempt the runlist */
	nvkm_wr32(device, 0x2638, BIT(runl));

	/* Schedule recovery. */
	nvkm_warn(subdev, "engine %d: scheduled for recovery\n", engn);
	schedule_work(&fifo->recover.work);
}

const struct nvkm_fifo_func_mmu_fault
tu102_fifo_mmu_fault = {
	.recover = gf100_fifo_mmu_fault_recover,
	.access = gv100_fifo_mmu_fault_access,
	.engine = tu102_fifo_mmu_fault_engine,
	.reason = gv100_fifo_mmu_fault_reason,
	.hubclient = gv100_fifo_mmu_fault_hubclient,
	.gpcclient = gv100_fifo_mmu_fault_gpcclient,
};

static void
tu102_fifo_intr_ctxsw_timeout(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	unsigned long flags, engm;
	u32 engn;

	spin_lock_irqsave(&fifo->base.lock, flags);

	engm = nvkm_rd32(device, 0x2a30);
	nvkm_wr32(device, 0x2a30, engm);

	for_each_set_bit(engn, &engm, 32)
		tu102_fifo_recover_engn(fifo, engn);

	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

static void
tu102_fifo_intr_sched(struct nvkm_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	u32 intr = nvkm_rd32(subdev->device, 0x00254c);
	u32 code = intr & 0x000000ff;

	nvkm_error(subdev, "SCHED_ERROR %02x\n", code);
}

static irqreturn_t
tu102_fifo_intr(struct nvkm_inth *inth)
{
	struct nvkm_fifo *fifo = container_of(inth, typeof(*fifo), engine.subdev.inth);
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x002140);
	u32 stat = nvkm_rd32(device, 0x002100) & mask;

	if (stat & 0x00000001) {
		gk104_fifo_intr_bind(fifo);
		nvkm_wr32(device, 0x002100, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000002) {
		tu102_fifo_intr_ctxsw_timeout(gk104_fifo(fifo));
		stat &= ~0x00000002;
	}

	if (stat & 0x00000100) {
		tu102_fifo_intr_sched(fifo);
		nvkm_wr32(device, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x00010000) {
		gk104_fifo_intr_chsw(fifo);
		nvkm_wr32(device, 0x002100, 0x00010000);
		stat &= ~0x00010000;
	}

	if (stat & 0x20000000) {
		if (gf100_fifo_intr_pbdma(fifo))
			stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		gk104_fifo_intr_runlist(fifo);
		stat &= ~0x40000000;
	}

	if (stat & 0x80000000) {
		nvkm_wr32(device, 0x002100, 0x80000000);
		nvkm_event_ntfy(&fifo->nonstall.event, 0, NVKM_FIFO_NONSTALL_EVENT);
		stat &= ~0x80000000;
	}

	if (stat) {
		nvkm_error(subdev, "INTR %08x\n", stat);
		spin_lock(&fifo->lock);
		nvkm_mask(device, 0x002140, stat, 0x00000000);
		spin_unlock(&fifo->lock);
		nvkm_wr32(device, 0x002100, stat);
	}

	return IRQ_HANDLED;
}

static void
tu102_fifo_init_pbdmas(struct nvkm_fifo *fifo, u32 mask)
{
	/* Not directly related to PBDMAs, but, enables doorbell to function. */
	nvkm_mask(fifo->engine.subdev.device, 0xb65000, 0x80000000, 0x80000000);
}

static const struct nvkm_fifo_func
tu102_fifo = {
	.dtor = gk104_fifo_dtor,
	.oneinit = gk104_fifo_oneinit,
	.chid_nr = gm200_fifo_chid_nr,
	.chid_ctor = gk110_fifo_chid_ctor,
	.runq_nr = gm200_fifo_runq_nr,
	.runl_ctor = gk104_fifo_runl_ctor,
	.init = gk104_fifo_init,
	.init_pbdmas = tu102_fifo_init_pbdmas,
	.fini = gk104_fifo_fini,
	.intr = tu102_fifo_intr,
	.mmu_fault = &tu102_fifo_mmu_fault,
	.engine_id = gk104_fifo_engine_id,
	.recover_chan = tu102_fifo_recover_chan,
	.runlist = &tu102_fifo_runlist,
	.nonstall = &gf100_fifo_nonstall,
	.runl = &tu102_runl,
	.runq = &gv100_runq,
	.engn = &gv100_engn,
	.engn_ce = &gv100_engn_ce,
	.cgrp = {{ 0, 0, KEPLER_CHANNEL_GROUP_A  }, &gk110_cgrp, .force = true },
	.chan = {{ 0, 0, TURING_CHANNEL_GPFIFO_A }, &tu102_chan, .ctor = tu102_fifo_gpfifo_new },
};

int
tu102_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	struct gk104_fifo *fifo;

	if (!(fifo = kzalloc(sizeof(*fifo), GFP_KERNEL)))
		return -ENOMEM;
	fifo->func = &tu102_fifo;
	INIT_WORK(&fifo->recover.work, tu102_fifo_recover_work);
	*pfifo = &fifo->base;

	return nvkm_fifo_ctor(&tu102_fifo, device, type, inst, &fifo->base);
}
