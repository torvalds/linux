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
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "runl.h"

#include <core/memory.h>
#include <subdev/gsp.h>
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
	.inst = &gf100_chan_inst,
	.userd = &gv100_chan_userd,
	.ramfc = &gv100_chan_ramfc,
	.bind = gk104_chan_bind_inst,
	.unbind = gk104_chan_unbind,
	.start = tu102_chan_start,
	.stop = gk104_chan_stop,
	.preempt = gk110_chan_preempt,
	.doorbell_handle = tu102_chan_doorbell_handle,
};

static bool
tu102_runl_pending(struct nvkm_runl *runl)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;

	return nvkm_rd32(device, 0x002b0c + (runl->id * 0x10)) & 0x00008000;
}

static void
tu102_runl_commit(struct nvkm_runl *runl, struct nvkm_memory *memory, u32 start, int count)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	u64 addr = nvkm_memory_addr(memory) + start;
	/*XXX: target? */

	nvkm_wr32(device, 0x002b00 + (runl->id * 0x10), lower_32_bits(addr));
	nvkm_wr32(device, 0x002b04 + (runl->id * 0x10), upper_32_bits(addr));
	nvkm_wr32(device, 0x002b08 + (runl->id * 0x10), count);
}

static const struct nvkm_runl_func
tu102_runl = {
	.runqs = 2,
	.size = 16,
	.update = nv50_runl_update,
	.insert_cgrp = gv100_runl_insert_cgrp,
	.insert_chan = gv100_runl_insert_chan,
	.commit = tu102_runl_commit,
	.wait = nv50_runl_wait,
	.pending = tu102_runl_pending,
	.block = gk104_runl_block,
	.allow = gk104_runl_allow,
	.preempt = gv100_runl_preempt,
	.preempt_pending = gf100_runl_preempt_pending,
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

const struct nvkm_fifo_func_mmu_fault
tu102_fifo_mmu_fault = {
	.recover = gf100_fifo_mmu_fault_recover,
	.access = gv100_fifo_mmu_fault_access,
	.engine = tu102_fifo_mmu_fault_engine,
	.reason = gv100_fifo_mmu_fault_reason,
	.hubclient = gv100_fifo_mmu_fault_hubclient,
	.gpcclient = gv100_fifo_mmu_fault_gpcclient,
};

void
tu102_fifo_intr_ctxsw_timeout_info(struct nvkm_engn *engn, u32 info)
{
	struct nvkm_runl *runl = engn->runl;
	struct nvkm_cgrp *cgrp;
	unsigned long flags;

	/* Check that engine hasn't become unstuck since timeout raised. */
	ENGN_DEBUG(engn, "CTXSW_TIMEOUT %08x", info);
	if (info & 0xc0000000)
		return;

	/* Determine channel group the engine is stuck on, and schedule recovery. */
	switch (info & 0x0000c000) {
	case 0x00004000: /* LOAD */
		cgrp = nvkm_runl_cgrp_get_cgid(runl, info & 0x3fff0000, &flags);
		break;
	case 0x00008000: /* SAVE */
	case 0x0000c000: /* SWITCH */
		cgrp = nvkm_runl_cgrp_get_cgid(runl, info & 0x00003fff, &flags);
		break;
	default:
		cgrp = NULL;
		break;
	}

	if (!WARN_ON(!cgrp)) {
		nvkm_runl_rc_cgrp(cgrp);
		nvkm_cgrp_put(&cgrp, flags);
	}
}

static void
tu102_fifo_intr_ctxsw_timeout(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	u32 engm = nvkm_rd32(device, 0x002a30);
	u32 info;

	nvkm_runl_foreach(runl, fifo) {
		nvkm_runl_foreach_engn_cond(engn, runl, engm & BIT(engn->id)) {
			info = nvkm_rd32(device, 0x003200 + (engn->id * 4));
			tu102_fifo_intr_ctxsw_timeout_info(engn, info);
		}
	}

	nvkm_wr32(device, 0x002a30, engm);
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
	.chid_nr = gm200_fifo_chid_nr,
	.chid_ctor = gk110_fifo_chid_ctor,
	.runq_nr = gm200_fifo_runq_nr,
	.runl_ctor = gk104_fifo_runl_ctor,
	.init = gk104_fifo_init,
	.init_pbdmas = tu102_fifo_init_pbdmas,
	.intr = tu102_fifo_intr,
	.mmu_fault = &tu102_fifo_mmu_fault,
	.nonstall = &gf100_fifo_nonstall,
	.runl = &tu102_runl,
	.runq = &gv100_runq,
	.engn = &gv100_engn,
	.engn_ce = &gv100_engn_ce,
	.cgrp = {{ 0, 0, KEPLER_CHANNEL_GROUP_A  }, &gk110_cgrp, .force = true },
	.chan = {{ 0, 0, TURING_CHANNEL_GPFIFO_A }, &tu102_chan },
};

int
tu102_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_fifo_new(&tu102_fifo, device, type, inst, pfifo);

	return nvkm_fifo_new_(&tu102_fifo, device, type, inst, pfifo);
}
