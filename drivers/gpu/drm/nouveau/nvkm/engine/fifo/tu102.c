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
#include "gk104.h"
#include "cgrp.h"
#include "changk104.h"
#include "user.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/bar.h>
#include <subdev/fault.h>
#include <subdev/top.h>
#include <subdev/timer.h>
#include <engine/sw.h>

#include <nvif/class.h>

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

	/*XXX: how to wait? can you even wait? */
}

static const struct gk104_fifo_runlist_func
tu102_fifo_runlist = {
	.size = 16,
	.cgrp = gv100_fifo_runlist_cgrp,
	.chan = gv100_fifo_runlist_chan,
	.commit = tu102_fifo_runlist_commit,
};

static const struct nvkm_enum
tu102_fifo_fault_engine[] = {
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
tu102_fifo_pbdma_init(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	const u32 mask = (1 << fifo->pbdma_nr) - 1;
	/*XXX: this is a bit of a guess at this point in time. */
	nvkm_mask(device, 0xb65000, 0x80000fff, 0x80000000 | mask);
}

static const struct gk104_fifo_pbdma_func
tu102_fifo_pbdma = {
	.nr = gm200_fifo_pbdma_nr,
	.init = tu102_fifo_pbdma_init,
	.init_timeout = gk208_fifo_pbdma_init_timeout,
};

static const struct gk104_fifo_func
tu102_fifo = {
	.pbdma = &tu102_fifo_pbdma,
	.fault.access = gv100_fifo_fault_access,
	.fault.engine = tu102_fifo_fault_engine,
	.fault.reason = gv100_fifo_fault_reason,
	.fault.hubclient = gv100_fifo_fault_hubclient,
	.fault.gpcclient = gv100_fifo_fault_gpcclient,
	.runlist = &tu102_fifo_runlist,
	.user = {{-1,-1,VOLTA_USERMODE_A       }, tu102_fifo_user_new   },
	.chan = {{ 0, 0,TURING_CHANNEL_GPFIFO_A}, tu102_fifo_gpfifo_new },
	.cgrp_force = true,
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
		nvkm_fifo_kevent(&fifo->base, chid);
	}

	/* Disable channel. */
	nvkm_wr32(device, 0x800004 + (chid * 0x08), stat | 0x00000800);
	nvkm_warn(subdev, "channel %d: killed\n", chid);

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

static void
tu102_fifo_fault(struct nvkm_fifo *base, struct nvkm_fault_data *info)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const struct nvkm_enum *er, *ee, *ec, *ea;
	struct nvkm_engine *engine = NULL;
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	const char *en = "";
	char ct[8] = "HUB/";
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
			engine = nvkm_device_engine(device, ee->data2, 0);
			break;
		}
	}

	if (ee == NULL) {
		struct nvkm_subdev *subdev = nvkm_top_fault(device, info->engine);
		if (subdev) {
			if (subdev->func == &nvkm_engine)
				engine = container_of(subdev, typeof(*engine), subdev);
			en = engine->subdev.name;
		}
	} else {
		en = ee->name;
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
		tu102_fifo_recover_chan(&fifo->base, chan->chid);

	/* Channel recovery will probably have already done this for the
	 * correct engine(s), but just in case we can't find the channel
	 * information...
	 */
	for (engn = 0; engn < fifo->engine_nr && engine; engn++) {
		if (fifo->engine[engn].engine == engine) {
			tu102_fifo_recover_engn(fifo, engn);
			break;
		}
	}

	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

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
tu102_fifo_intr_sched(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x00254c);
	u32 code = intr & 0x000000ff;

	nvkm_error(subdev, "SCHED_ERROR %02x\n", code);
}

static void
tu102_fifo_intr(struct nvkm_fifo *base)
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

	if (stat & 0x00000002) {
		tu102_fifo_intr_ctxsw_timeout(fifo);
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

static const struct nvkm_fifo_func
tu102_fifo_ = {
	.dtor = gk104_fifo_dtor,
	.oneinit = gk104_fifo_oneinit,
	.info = gk104_fifo_info,
	.init = gk104_fifo_init,
	.fini = gk104_fifo_fini,
	.intr = tu102_fifo_intr,
	.fault = tu102_fifo_fault,
	.engine_id = gk104_fifo_engine_id,
	.id_engine = gk104_fifo_id_engine,
	.uevent_init = gk104_fifo_uevent_init,
	.uevent_fini = gk104_fifo_uevent_fini,
	.recover_chan = tu102_fifo_recover_chan,
	.class_get = gk104_fifo_class_get,
	.class_new = gk104_fifo_class_new,
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

	return nvkm_fifo_ctor(&tu102_fifo_, device, type, inst, 4096, &fifo->base);
}
