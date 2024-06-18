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
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"
#include "runq.h"

#include <core/gpuobj.h>
#include <subdev/gsp.h>
#include <subdev/top.h>
#include <subdev/vfn.h>

#include <nvif/class.h>

static u32
ga100_chan_doorbell_handle(struct nvkm_chan *chan)
{
	return (chan->cgrp->runl->doorbell << 16) | chan->id;
}

static void
ga100_chan_stop(struct nvkm_chan *chan)
{
	struct nvkm_runl *runl = chan->cgrp->runl;

	nvkm_wr32(runl->fifo->engine.subdev.device, runl->chan + (chan->id * 4), 0x00000003);
}

static void
ga100_chan_start(struct nvkm_chan *chan)
{
	struct nvkm_runl *runl = chan->cgrp->runl;
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	const int gfid = 0;

	nvkm_wr32(device, runl->chan + (chan->id * 4), 0x00000002);
	nvkm_wr32(device, runl->addr + 0x0090, (gfid << 16) | chan->id); /* INTERNAL_DOORBELL. */
}

static void
ga100_chan_unbind(struct nvkm_chan *chan)
{
	struct nvkm_runl *runl = chan->cgrp->runl;

	nvkm_wr32(runl->fifo->engine.subdev.device, runl->chan + (chan->id * 4), 0xffffffff);
}

static int
ga100_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	const u32 limit2 = ilog2(length / 8);

	nvkm_kmap(chan->inst);
	nvkm_wo32(chan->inst, 0x010, 0x0000face);
	nvkm_wo32(chan->inst, 0x030, 0x7ffff902);
	nvkm_wo32(chan->inst, 0x048, lower_32_bits(offset));
	nvkm_wo32(chan->inst, 0x04c, upper_32_bits(offset) | (limit2 << 16));
	nvkm_wo32(chan->inst, 0x084, 0x20400000);
	nvkm_wo32(chan->inst, 0x094, 0x30000000 | devm);
	nvkm_wo32(chan->inst, 0x0e4, priv ? 0x00000020 : 0x00000000);
	nvkm_wo32(chan->inst, 0x0e8, chan->id);
	nvkm_wo32(chan->inst, 0x0f4, 0x00001000 | (priv ? 0x00000100 : 0x00000000));
	nvkm_wo32(chan->inst, 0x0f8, 0x80000000 | chan->cgrp->runl->nonstall.vector);
	nvkm_mo32(chan->inst, 0x218, 0x00000000, 0x00000000);
	nvkm_done(chan->inst);
	return 0;
}

static const struct nvkm_chan_func_ramfc
ga100_chan_ramfc = {
	.write = ga100_chan_ramfc_write,
	.devm = 0xfff,
	.priv = true,
};

const struct nvkm_chan_func
ga100_chan = {
	.inst = &gf100_chan_inst,
	.userd = &gv100_chan_userd,
	.ramfc = &ga100_chan_ramfc,
	.unbind = ga100_chan_unbind,
	.start = ga100_chan_start,
	.stop = ga100_chan_stop,
	.preempt = gk110_chan_preempt,
	.doorbell_handle = ga100_chan_doorbell_handle,
};

static void
ga100_cgrp_preempt(struct nvkm_cgrp *cgrp)
{
	struct nvkm_runl *runl = cgrp->runl;

	nvkm_wr32(runl->fifo->engine.subdev.device, runl->addr + 0x098, 0x01000000 | cgrp->id);
}

const struct nvkm_cgrp_func
ga100_cgrp = {
	.preempt = ga100_cgrp_preempt,
};

static int
ga100_engn_cxid(struct nvkm_engn *engn, bool *cgid)
{
	struct nvkm_runl *runl = engn->runl;
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	u32 stat = nvkm_rd32(device, runl->addr + 0x200 + engn->id * 0x40);

	ENGN_DEBUG(engn, "status %08x", stat);
	*cgid = true;

	switch ((stat & 0x0000e000) >> 13) {
	case 0 /* INVALID */: return -ENODEV;
	case 1 /*   VALID */:
	case 5 /*    SAVE */: return (stat & 0x00000fff);
	case 6 /*    LOAD */: return (stat & 0x0fff0000) >> 16;
	case 7 /*  SWITCH */:
		if (nvkm_engine_chsw_load(engn->engine))
			return (stat & 0x0fff0000) >> 16;
		return (stat & 0x00000fff);
	default:
		WARN_ON(1);
		break;
	}

	return -ENODEV;
}

static int
ga100_engn_nonstall(struct nvkm_engn *engn)
{
	struct nvkm_engine *engine = engn->engine;

	if (WARN_ON(!engine->func->nonstall))
		return -EINVAL;

	return engine->func->nonstall(engine);
}

const struct nvkm_engn_func
ga100_engn = {
	.nonstall = ga100_engn_nonstall,
	.cxid = ga100_engn_cxid,
	.ctor = gk104_ectx_ctor,
	.bind = gv100_ectx_bind,
};

const struct nvkm_engn_func
ga100_engn_ce = {
	.nonstall = ga100_engn_nonstall,
	.cxid = ga100_engn_cxid,
	.ctor = gv100_ectx_ce_ctor,
	.bind = gv100_ectx_ce_bind,
};

static bool
ga100_runq_idle(struct nvkm_runq *runq)
{
	struct nvkm_device *device = runq->fifo->engine.subdev.device;

	return !(nvkm_rd32(device, 0x04015c + (runq->id * 0x800)) & 0x0000e000);
}

static bool
ga100_runq_intr_1(struct nvkm_runq *runq, struct nvkm_runl *runl)
{
	struct nvkm_device *device = runq->fifo->engine.subdev.device;
	u32 inte = nvkm_rd32(device, 0x040180 + (runq->id * 0x800));
	u32 intr = nvkm_rd32(device, 0x040148 + (runq->id * 0x800));
	u32 stat = intr & inte;

	if (!stat) {
		RUNQ_DEBUG(runq, "inte1 %08x %08x", intr, inte);
		return false;
	}

	if (stat & 0x80000000) {
		u32 chid = nvkm_rd32(device, 0x040120 + (runq->id * 0x0800)) & runl->chid->mask;
		struct nvkm_chan *chan;
		unsigned long flags;

		RUNQ_ERROR(runq, "CTXNOTVALID chid:%d", chid);
		chan = nvkm_runl_chan_get_chid(runl, chid, &flags);
		if (chan) {
			nvkm_chan_error(chan, true);
			nvkm_chan_put(&chan, flags);
		}

		nvkm_mask(device, 0x0400ac + (runq->id * 0x800), 0x00030000, 0x00030000);
		stat &= ~0x80000000;
	}

	if (stat) {
		RUNQ_ERROR(runq, "intr1 %08x", stat);
		nvkm_wr32(device, 0x0401a0 + (runq->id * 0x800), stat);
	}

	nvkm_wr32(device, 0x040148 + (runq->id * 0x800), intr);
	return true;
}

static bool
ga100_runq_intr_0(struct nvkm_runq *runq, struct nvkm_runl *runl)
{
	struct nvkm_device *device = runq->fifo->engine.subdev.device;
	u32 inte = nvkm_rd32(device, 0x040170 + (runq->id * 0x800));
	u32 intr = nvkm_rd32(device, 0x040108 + (runq->id * 0x800));
	u32 stat = intr & inte;

	if (!stat) {
		RUNQ_DEBUG(runq, "inte0 %08x %08x", intr, inte);
		return false;
	}

	/*TODO: expand on this when fixing up gf100's version. */
	if (stat & 0xc6afe000) {
		u32 chid = nvkm_rd32(device, 0x040120 + (runq->id * 0x0800)) & runl->chid->mask;
		struct nvkm_chan *chan;
		unsigned long flags;

		RUNQ_ERROR(runq, "intr0 %08x", stat);
		chan = nvkm_runl_chan_get_chid(runl, chid, &flags);
		if (chan) {
			nvkm_chan_error(chan, true);
			nvkm_chan_put(&chan, flags);
		}

		stat &= ~0xc6afe000;
	}

	if (stat) {
		RUNQ_ERROR(runq, "intr0 %08x", stat);
		nvkm_wr32(device, 0x040190 + (runq->id * 0x800), stat);
	}

	nvkm_wr32(device, 0x040108 + (runq->id * 0x800), intr);
	return true;
}

static bool
ga100_runq_intr(struct nvkm_runq *runq, struct nvkm_runl *runl)
{
	bool intr0 = ga100_runq_intr_0(runq, runl);
	bool intr1 = ga100_runq_intr_1(runq, runl);

	return intr0 || intr1;
}

static void
ga100_runq_init(struct nvkm_runq *runq)
{
	struct nvkm_device *device = runq->fifo->engine.subdev.device;

	nvkm_wr32(device, 0x040108 + (runq->id * 0x800), 0xffffffff); /* INTR_0 */
	nvkm_wr32(device, 0x040148 + (runq->id * 0x800), 0xffffffff); /* INTR_1 */
	nvkm_wr32(device, 0x040170 + (runq->id * 0x800), 0xffffffff); /* INTR_0_EN_SET_TREE */
	nvkm_wr32(device, 0x040180 + (runq->id * 0x800), 0xffffffff); /* INTR_1_EN_SET_TREE */
}

const struct nvkm_runq_func
ga100_runq = {
	.init = ga100_runq_init,
	.intr = ga100_runq_intr,
	.idle = ga100_runq_idle,
};

static bool
ga100_runl_preempt_pending(struct nvkm_runl *runl)
{
	return nvkm_rd32(runl->fifo->engine.subdev.device, runl->addr + 0x098) & 0x00100000;
}

static void
ga100_runl_preempt(struct nvkm_runl *runl)
{
	nvkm_wr32(runl->fifo->engine.subdev.device, runl->addr + 0x098, 0x00000000);
}

static void
ga100_runl_allow(struct nvkm_runl *runl, u32 engm)
{
	nvkm_mask(runl->fifo->engine.subdev.device, runl->addr + 0x094, 0x00000001, 0x00000000);
}

static void
ga100_runl_block(struct nvkm_runl *runl, u32 engm)
{
	nvkm_mask(runl->fifo->engine.subdev.device, runl->addr + 0x094, 0x00000001, 0x00000001);
}

static bool
ga100_runl_pending(struct nvkm_runl *runl)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;

	return nvkm_rd32(device, runl->addr + 0x08c) & 0x00008000;
}

static void
ga100_runl_commit(struct nvkm_runl *runl, struct nvkm_memory *memory, u32 start, int count)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	u64 addr = nvkm_memory_addr(memory) + start;

	nvkm_wr32(device, runl->addr + 0x080, lower_32_bits(addr));
	nvkm_wr32(device, runl->addr + 0x084, upper_32_bits(addr));
	nvkm_wr32(device, runl->addr + 0x088, count);
}

static irqreturn_t
ga100_runl_intr(struct nvkm_inth *inth)
{
	struct nvkm_runl *runl = container_of(inth, typeof(*runl), inth);
	struct nvkm_engn *engn;
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	u32 inte = nvkm_rd32(device, runl->addr + 0x120);
	u32 intr = nvkm_rd32(device, runl->addr + 0x100);
	u32 stat = intr & inte;
	u32 info;

	if (!stat) {
		RUNL_DEBUG(runl, "inte %08x %08x", intr, inte);
		return IRQ_NONE;
	}

	if (stat & 0x00000007) {
		nvkm_runl_foreach_engn_cond(engn, runl, stat & BIT(engn->id)) {
			info = nvkm_rd32(device, runl->addr + 0x224 + (engn->id * 0x40));

			tu102_fifo_intr_ctxsw_timeout_info(engn, info);

			nvkm_wr32(device, runl->addr + 0x100, BIT(engn->id));
			stat &= ~BIT(engn->id);
		}
	}

	if (stat & 0x00000300) {
		nvkm_wr32(device, runl->addr + 0x100, stat & 0x00000300);
		stat &= ~0x00000300;
	}

	if (stat & 0x00010000) {
		if (runl->runq[0]) {
			if (runl->runq[0]->func->intr(runl->runq[0], runl))
				stat &= ~0x00010000;
		}
	}

	if (stat & 0x00020000) {
		if (runl->runq[1]) {
			if (runl->runq[1]->func->intr(runl->runq[1], runl))
				stat &= ~0x00020000;
		}
	}

	if (stat) {
		RUNL_ERROR(runl, "intr %08x", stat);
		nvkm_wr32(device, runl->addr + 0x140, stat);
	}

	nvkm_wr32(device, runl->addr + 0x180, 0x00000001);
	return IRQ_HANDLED;
}

static void
ga100_runl_fini(struct nvkm_runl *runl)
{
	nvkm_mask(runl->fifo->engine.subdev.device, runl->addr + 0x300, 0x80000000, 0x00000000);
	nvkm_inth_block(&runl->inth);
}

static void
ga100_runl_init(struct nvkm_runl *runl)
{
	struct nvkm_fifo *fifo = runl->fifo;
	struct nvkm_runq *runq;
	struct nvkm_device *device = fifo->engine.subdev.device;
	int i;

	/* Submit NULL runlist and preempt. */
	nvkm_wr32(device, runl->addr + 0x088, 0x00000000);
	runl->func->preempt(runl);

	/* Enable doorbell. */
	nvkm_mask(device, runl->addr + 0x300, 0x80000000, 0x80000000);

	nvkm_wr32(device, runl->addr + 0x100, 0xffffffff); /* INTR_0 */
	nvkm_wr32(device, runl->addr + 0x140, 0xffffffff); /* INTR_0_EN_CLEAR_TREE(0) */
	nvkm_wr32(device, runl->addr + 0x120, 0x000f1307); /* INTR_0_EN_SET_TREE(0) */
	nvkm_wr32(device, runl->addr + 0x148, 0xffffffff); /* INTR_0_EN_CLEAR_TREE(1) */
	nvkm_wr32(device, runl->addr + 0x128, 0x00000000); /* INTR_0_EN_SET_TREE(1) */

	/* Init PBDMA(s). */
	for (i = 0; i < runl->runq_nr; i++) {
		runq = runl->runq[i];
		runq->func->init(runq);
	}

	nvkm_inth_allow(&runl->inth);
}

const struct nvkm_runl_func
ga100_runl = {
	.init = ga100_runl_init,
	.fini = ga100_runl_fini,
	.size = 16,
	.update = nv50_runl_update,
	.insert_cgrp = gv100_runl_insert_cgrp,
	.insert_chan = gv100_runl_insert_chan,
	.commit = ga100_runl_commit,
	.wait = nv50_runl_wait,
	.pending = ga100_runl_pending,
	.block = ga100_runl_block,
	.allow = ga100_runl_allow,
	.preempt = ga100_runl_preempt,
	.preempt_pending = ga100_runl_preempt_pending,
};

static int
ga100_runl_new(struct nvkm_fifo *fifo, int id, u32 addr, struct nvkm_runl **prunl)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_top_device *tdev;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	u32 chcfg  = nvkm_rd32(device, addr + 0x004);
	u32 chnum  = 1 << (chcfg & 0x0000000f);
	u32 chaddr = (chcfg & 0xfffffff0);
	u32 dbcfg  = nvkm_rd32(device, addr + 0x008);
	u32 vector = nvkm_rd32(device, addr + 0x160);
	int i, ret;

	runl = nvkm_runl_new(fifo, id, addr, chnum);
	if (IS_ERR(runl))
		return PTR_ERR(runl);

	*prunl = runl;

	for (i = 0; i < 2; i++) {
		u32 pbcfg = nvkm_rd32(device, addr + 0x010 + (i * 0x04));
		if (pbcfg & 0x80000000) {
			runl->runq[runl->runq_nr] =
				nvkm_runq_new(fifo, ((pbcfg & 0x03fffc00) - 0x040000) / 0x800);
			if (!runl->runq[runl->runq_nr]) {
				RUNL_ERROR(runl, "runq %d", runl->runq_nr);
				return -ENOMEM;
			}

			runl->runq_nr++;
		}
	}

	nvkm_list_foreach(tdev, &device->top->device, head, tdev->runlist == runl->addr) {
		if (tdev->engine < 0) {
			RUNL_DEBUG(runl, "engn !top");
			return -EINVAL;
		}

		engn = nvkm_runl_add(runl, tdev->engine, (tdev->type == NVKM_ENGINE_CE) ?
				     fifo->func->engn_ce : fifo->func->engn,
				     tdev->type, tdev->inst);
		if (!engn)
			return -EINVAL;

		if (!engn->engine->func->nonstall) {
			RUNL_DEBUG(runl, "engn %s !nonstall", engn->engine->subdev.name);
			return -EINVAL;
		}
	}

	if (list_empty(&runl->engns)) {
		RUNL_DEBUG(runl, "!engns");
		return -EINVAL;
	}

	ret = nvkm_inth_add(&device->vfn->intr, vector & 0x00000fff, NVKM_INTR_PRIO_NORMAL,
			    &fifo->engine.subdev, ga100_runl_intr, &runl->inth);
	if (ret) {
		RUNL_ERROR(runl, "inth %d", ret);
		return ret;
	}

	runl->chan = chaddr;
	runl->doorbell = dbcfg >> 16;
	return 0;
}

static irqreturn_t
ga100_fifo_nonstall_intr(struct nvkm_inth *inth)
{
	struct nvkm_runl *runl = container_of(inth, typeof(*runl), nonstall.inth);

	nvkm_event_ntfy(&runl->fifo->nonstall.event, runl->id, NVKM_FIFO_NONSTALL_EVENT);
	return IRQ_HANDLED;
}

static void
ga100_fifo_nonstall_block(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), nonstall.event);
	struct nvkm_runl *runl = nvkm_runl_get(fifo, index, 0);

	nvkm_inth_block(&runl->nonstall.inth);
}

static void
ga100_fifo_nonstall_allow(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), nonstall.event);
	struct nvkm_runl *runl = nvkm_runl_get(fifo, index, 0);

	nvkm_inth_allow(&runl->nonstall.inth);
}

const struct nvkm_event_func
ga100_fifo_nonstall = {
	.init = ga100_fifo_nonstall_allow,
	.fini = ga100_fifo_nonstall_block,
};

int
ga100_fifo_nonstall_ctor(struct nvkm_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_vfn *vfn = subdev->device->vfn;
	struct nvkm_runl *runl;
	int ret, nr = 0;

	nvkm_runl_foreach(runl, fifo) {
		struct nvkm_engn *engn = list_first_entry(&runl->engns, typeof(*engn), head);

		runl->nonstall.vector = engn->func->nonstall(engn);

		/* if no nonstall vector just keep going */
		if (runl->nonstall.vector == -1)
			continue;
		if (runl->nonstall.vector < 0) {
			RUNL_ERROR(runl, "nonstall %d", runl->nonstall.vector);
			return runl->nonstall.vector;
		}

		ret = nvkm_inth_add(&vfn->intr, runl->nonstall.vector, NVKM_INTR_PRIO_NORMAL,
				    subdev, ga100_fifo_nonstall_intr, &runl->nonstall.inth);
		if (ret)
			return ret;

		nr = max(nr, runl->id + 1);
	}

	return nr;
}

int
ga100_fifo_runl_ctor(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_top_device *tdev;
	struct nvkm_runl *runl;
	int id = 0, ret;

	nvkm_list_foreach(tdev, &device->top->device, head, tdev->runlist >= 0) {
		runl = nvkm_runl_get(fifo, -1, tdev->runlist);
		if (!runl) {
			ret = ga100_runl_new(fifo, id++, tdev->runlist, &runl);
			if (ret) {
				if (runl)
					nvkm_runl_del(runl);

				continue;
			}
		}
	}

	return 0;
}

static const struct nvkm_fifo_func
ga100_fifo = {
	.runl_ctor = ga100_fifo_runl_ctor,
	.mmu_fault = &tu102_fifo_mmu_fault,
	.nonstall_ctor = ga100_fifo_nonstall_ctor,
	.nonstall = &ga100_fifo_nonstall,
	.runl = &ga100_runl,
	.runq = &ga100_runq,
	.engn = &ga100_engn,
	.engn_ce = &ga100_engn_ce,
	.cgrp = {{ 0, 0, KEPLER_CHANNEL_GROUP_A  }, &ga100_cgrp, .force = true },
	.chan = {{ 0, 0, AMPERE_CHANNEL_GPFIFO_A }, &ga100_chan },
};

int
ga100_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_fifo_new(&ga100_fifo, device, type, inst, pfifo);

	return nvkm_fifo_new_(&ga100_fifo, device, type, inst, pfifo);
}
