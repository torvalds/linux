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
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"
#include "runq.h"

#include <core/gpuobj.h>
#include <subdev/bar.h>
#include <subdev/fault.h>
#include <subdev/mc.h>
#include <subdev/mmu.h>
#include <engine/sw.h>

#include <nvif/class.h>

void
gf100_chan_preempt(struct nvkm_chan *chan)
{
	nvkm_wr32(chan->cgrp->runl->fifo->engine.subdev.device, 0x002634, chan->id);
}

static void
gf100_chan_stop(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_mask(device, 0x003004 + (chan->id * 8), 0x00000001, 0x00000000);
}

static void
gf100_chan_start(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_wr32(device, 0x003004 + (chan->id * 8), 0x001f0001);
}

static void gf100_fifo_intr_engine(struct nvkm_fifo *);

static void
gf100_chan_unbind(struct nvkm_chan *chan)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_device *device = fifo->engine.subdev.device;

	/*TODO: Is this cargo-culted, or necessary? RM does *something* here... Why? */
	gf100_fifo_intr_engine(fifo);

	nvkm_wr32(device, 0x003000 + (chan->id * 8), 0x00000000);
}

static void
gf100_chan_bind(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_wr32(device, 0x003000 + (chan->id * 8), 0xc0000000 | chan->inst->addr >> 12);
}

static int
gf100_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	const u64 userd = nvkm_memory_addr(chan->userd.mem) + chan->userd.base;
	const u32 limit2 = ilog2(length / 8);

	nvkm_kmap(chan->inst);
	nvkm_wo32(chan->inst, 0x08, lower_32_bits(userd));
	nvkm_wo32(chan->inst, 0x0c, upper_32_bits(userd));
	nvkm_wo32(chan->inst, 0x10, 0x0000face);
	nvkm_wo32(chan->inst, 0x30, 0xfffff902);
	nvkm_wo32(chan->inst, 0x48, lower_32_bits(offset));
	nvkm_wo32(chan->inst, 0x4c, upper_32_bits(offset) | (limit2 << 16));
	nvkm_wo32(chan->inst, 0x54, 0x00000002);
	nvkm_wo32(chan->inst, 0x84, 0x20400000);
	nvkm_wo32(chan->inst, 0x94, 0x30000000 | devm);
	nvkm_wo32(chan->inst, 0x9c, 0x00000100);
	nvkm_wo32(chan->inst, 0xa4, 0x1f1f1f1f);
	nvkm_wo32(chan->inst, 0xa8, 0x1f1f1f1f);
	nvkm_wo32(chan->inst, 0xac, 0x0000001f);
	nvkm_wo32(chan->inst, 0xb8, 0xf8000000);
	nvkm_wo32(chan->inst, 0xf8, 0x10003080); /* 0x002310 */
	nvkm_wo32(chan->inst, 0xfc, 0x10000010); /* 0x002350 */
	nvkm_done(chan->inst);
	return 0;
}

static const struct nvkm_chan_func_ramfc
gf100_chan_ramfc = {
	.write = gf100_chan_ramfc_write,
	.devm = 0xfff,
};

void
gf100_chan_userd_clear(struct nvkm_chan *chan)
{
	nvkm_kmap(chan->userd.mem);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x040, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x044, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x048, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x04c, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x050, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x058, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x05c, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x060, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x088, 0x00000000);
	nvkm_wo32(chan->userd.mem, chan->userd.base + 0x08c, 0x00000000);
	nvkm_done(chan->userd.mem);
}

static const struct nvkm_chan_func_userd
gf100_chan_userd = {
	.bar = NVKM_BAR1_FB,
	.size = 0x1000,
	.clear = gf100_chan_userd_clear,
};

const struct nvkm_chan_func_inst
gf100_chan_inst = {
	.size = 0x1000,
	.zero = true,
	.vmm = true,
};

static const struct nvkm_chan_func
gf100_chan = {
	.inst = &gf100_chan_inst,
	.userd = &gf100_chan_userd,
	.ramfc = &gf100_chan_ramfc,
	.bind = gf100_chan_bind,
	.unbind = gf100_chan_unbind,
	.start = gf100_chan_start,
	.stop = gf100_chan_stop,
	.preempt = gf100_chan_preempt,
};

static void
gf100_ectx_bind(struct nvkm_engn *engn, struct nvkm_cctx *cctx, struct nvkm_chan *chan)
{
	u64 addr = 0ULL;
	u32 ptr0;

	switch (engn->engine->subdev.type) {
	case NVKM_ENGINE_SW    : return;
	case NVKM_ENGINE_GR    : ptr0 = 0x0210; break;
	case NVKM_ENGINE_CE    : ptr0 = 0x0230 + (engn->engine->subdev.inst * 0x10); break;
	case NVKM_ENGINE_MSPDEC: ptr0 = 0x0250; break;
	case NVKM_ENGINE_MSPPP : ptr0 = 0x0260; break;
	case NVKM_ENGINE_MSVLD : ptr0 = 0x0270; break;
	default:
		WARN_ON(1);
		return;
	}

	if (cctx) {
		addr  = cctx->vctx->vma->addr;
		addr |= 4ULL;
	}

	nvkm_kmap(chan->inst);
	nvkm_wo32(chan->inst, ptr0 + 0, lower_32_bits(addr));
	nvkm_wo32(chan->inst, ptr0 + 4, upper_32_bits(addr));
	nvkm_done(chan->inst);
}

static int
gf100_ectx_ctor(struct nvkm_engn *engn, struct nvkm_vctx *vctx)
{
	int ret;

	ret = nvkm_vmm_get(vctx->vmm, 12, vctx->inst->size, &vctx->vma);
	if (ret)
		return ret;

	return nvkm_memory_map(vctx->inst, 0, vctx->vmm, vctx->vma, NULL, 0);
}

bool
gf100_engn_mmu_fault_triggered(struct nvkm_engn *engn)
{
	struct nvkm_runl *runl = engn->runl;
	struct nvkm_fifo *fifo = runl->fifo;
	struct nvkm_device *device = fifo->engine.subdev.device;
	u32 data = nvkm_rd32(device, 0x002a30 + (engn->id * 4));

	ENGN_DEBUG(engn, "%08x: mmu fault triggered", data);
	if (!(data & 0x00000100))
		return false;

	spin_lock(&fifo->lock);
	nvkm_mask(device, 0x002a30 + (engn->id * 4), 0x00000100, 0x00000000);
	if (atomic_dec_and_test(&runl->rc_triggered))
		nvkm_mask(device, 0x002140, 0x00000100, 0x00000100);
	spin_unlock(&fifo->lock);
	return true;
}

void
gf100_engn_mmu_fault_trigger(struct nvkm_engn *engn)
{
	struct nvkm_runl *runl = engn->runl;
	struct nvkm_fifo *fifo = runl->fifo;
	struct nvkm_device *device = fifo->engine.subdev.device;

	ENGN_DEBUG(engn, "triggering mmu fault on 0x%02x", engn->fault);
	spin_lock(&fifo->lock);
	if (atomic_inc_return(&runl->rc_triggered) == 1)
		nvkm_mask(device, 0x002140, 0x00000100, 0x00000000);
	nvkm_wr32(device, 0x002100, 0x00000100);
	nvkm_wr32(device, 0x002a30 + (engn->id * 4), 0x00000100 | engn->fault);
	spin_unlock(&fifo->lock);
}

/*TODO: clean all this up. */
struct gf100_engn_status {
	bool busy;
	bool save;
	bool unk0;
	bool unk1;
	u8   chid;
};

static void
gf100_engn_status(struct nvkm_engn *engn, struct gf100_engn_status *status)
{
	u32 stat = nvkm_rd32(engn->engine->subdev.device, 0x002640 + (engn->id * 4));

	status->busy = (stat & 0x10000000);
	status->save = (stat & 0x00100000);
	status->unk0 = (stat & 0x00004000);
	status->unk1 = (stat & 0x00001000);
	status->chid = (stat & 0x0000007f);

	ENGN_DEBUG(engn, "%08x: busy %d save %d unk0 %d unk1 %d chid %d",
		   stat, status->busy, status->save, status->unk0, status->unk1, status->chid);
}

static int
gf100_engn_cxid(struct nvkm_engn *engn, bool *cgid)
{
	struct gf100_engn_status status;

	gf100_engn_status(engn, &status);
	if (status.busy) {
		*cgid = false;
		return status.chid;
	}

	return -ENODEV;
}

static bool
gf100_engn_chsw(struct nvkm_engn *engn)
{
	struct gf100_engn_status status;

	gf100_engn_status(engn, &status);
	if (status.busy && (status.unk0 || status.unk1))
		return true;

	return false;
}

static const struct nvkm_engn_func
gf100_engn = {
	.chsw = gf100_engn_chsw,
	.cxid = gf100_engn_cxid,
	.mmu_fault_trigger = gf100_engn_mmu_fault_trigger,
	.mmu_fault_triggered = gf100_engn_mmu_fault_triggered,
	.ctor = gf100_ectx_ctor,
	.bind = gf100_ectx_bind,
};

const struct nvkm_engn_func
gf100_engn_sw = {
};

static const struct nvkm_bitfield
gf100_runq_intr_0_names[] = {
/*	{ 0x00008000, "" }	seen with null ib push */
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

bool
gf100_runq_intr(struct nvkm_runq *runq, struct nvkm_runl *null)
{
	struct nvkm_subdev *subdev = &runq->fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x04010c + (runq->id * 0x2000));
	u32 stat = nvkm_rd32(device, 0x040108 + (runq->id * 0x2000)) & mask;
	u32 addr = nvkm_rd32(device, 0x0400c0 + (runq->id * 0x2000));
	u32 data = nvkm_rd32(device, 0x0400c4 + (runq->id * 0x2000));
	u32 chid = nvkm_rd32(device, 0x040120 + (runq->id * 0x2000)) & runq->fifo->chid->mask;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;
	struct nvkm_chan *chan;
	unsigned long flags;
	char msg[128];

	if (stat & 0x00800000) {
		if (device->sw) {
			if (nvkm_sw_mthd(device->sw, chid, subc, mthd, data))
				show &= ~0x00800000;
		}
	}

	if (show) {
		nvkm_snprintbf(msg, sizeof(msg), runq->func->intr_0_names, show);
		chan = nvkm_chan_get_chid(&runq->fifo->engine, chid, &flags);
		nvkm_error(subdev, "PBDMA%d: %08x [%s] ch %d [%010llx %s] "
				   "subc %d mthd %04x data %08x\n",
			   runq->id, show, msg, chid, chan ? chan->inst->addr : 0,
			   chan ? chan->name : "unknown", subc, mthd, data);

		/*TODO: use proper procedure for clearing each exception / debug output */
		if ((stat & 0xc67fe000) && chan)
			nvkm_chan_error(chan, true);
		nvkm_chan_put(&chan, flags);
	}

	nvkm_wr32(device, 0x0400c0 + (runq->id * 0x2000), 0x80600008);
	nvkm_wr32(device, 0x040108 + (runq->id * 0x2000), stat);
	return true;
}

void
gf100_runq_init(struct nvkm_runq *runq)
{
	struct nvkm_device *device = runq->fifo->engine.subdev.device;

	nvkm_mask(device, 0x04013c + (runq->id * 0x2000), 0x10000100, 0x00000000);
	nvkm_wr32(device, 0x040108 + (runq->id * 0x2000), 0xffffffff); /* INTR */
	nvkm_wr32(device, 0x04010c + (runq->id * 0x2000), 0xfffffeff); /* INTREN */
}

static const struct nvkm_runq_func
gf100_runq = {
	.init = gf100_runq_init,
	.intr = gf100_runq_intr,
	.intr_0_names = gf100_runq_intr_0_names,
};

bool
gf100_runl_preempt_pending(struct nvkm_runl *runl)
{
	return nvkm_rd32(runl->fifo->engine.subdev.device, 0x002634) & 0x00100000;
}

static void
gf100_runl_fault_clear(struct nvkm_runl *runl)
{
	nvkm_mask(runl->fifo->engine.subdev.device, 0x00262c, 0x00000000, 0x00000000);
}

static void
gf100_runl_allow(struct nvkm_runl *runl, u32 engm)
{
	nvkm_mask(runl->fifo->engine.subdev.device, 0x002630, engm, 0x00000000);
}

static void
gf100_runl_block(struct nvkm_runl *runl, u32 engm)
{
	nvkm_mask(runl->fifo->engine.subdev.device, 0x002630, engm, engm);
}

static bool
gf100_runl_pending(struct nvkm_runl *runl)
{
	return nvkm_rd32(runl->fifo->engine.subdev.device, 0x00227c) & 0x00100000;
}

static void
gf100_runl_commit(struct nvkm_runl *runl, struct nvkm_memory *memory, u32 start, int count)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	u64 addr = nvkm_memory_addr(memory) + start;
	int target;

	switch (nvkm_memory_target(memory)) {
	case NVKM_MEM_TARGET_VRAM: target = 0; break;
	case NVKM_MEM_TARGET_NCOH: target = 3; break;
	default:
		WARN_ON(1);
		return;
	}

	nvkm_wr32(device, 0x002270, (target << 28) | (addr >> 12));
	nvkm_wr32(device, 0x002274, 0x01f00000 | count);
}

static void
gf100_runl_insert_chan(struct nvkm_chan *chan, struct nvkm_memory *memory, u64 offset)
{
	nvkm_wo32(memory, offset + 0, chan->id);
	nvkm_wo32(memory, offset + 4, 0x00000004);
}

static const struct nvkm_runl_func
gf100_runl = {
	.size = 8,
	.update = nv50_runl_update,
	.insert_chan = gf100_runl_insert_chan,
	.commit = gf100_runl_commit,
	.wait = nv50_runl_wait,
	.pending = gf100_runl_pending,
	.block = gf100_runl_block,
	.allow = gf100_runl_allow,
	.fault_clear = gf100_runl_fault_clear,
	.preempt_pending = gf100_runl_preempt_pending,
};

static void
gf100_fifo_nonstall_allow(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), nonstall.event);
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	nvkm_mask(fifo->engine.subdev.device, 0x002140, 0x80000000, 0x80000000);
	spin_unlock_irqrestore(&fifo->lock, flags);
}

static void
gf100_fifo_nonstall_block(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), nonstall.event);
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	nvkm_mask(fifo->engine.subdev.device, 0x002140, 0x80000000, 0x00000000);
	spin_unlock_irqrestore(&fifo->lock, flags);
}

const struct nvkm_event_func
gf100_fifo_nonstall = {
	.init = gf100_fifo_nonstall_allow,
	.fini = gf100_fifo_nonstall_block,
};

static const struct nvkm_enum
gf100_fifo_mmu_fault_engine[] = {
	{ 0x00, "PGRAPH", NULL, NVKM_ENGINE_GR },
	{ 0x03, "PEEPHOLE", NULL, NVKM_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0x05, "BAR3", NULL, NVKM_SUBDEV_INSTMEM },
	{ 0x07, "PFIFO" },
	{ 0x10, "PMSVLD", NULL, NVKM_ENGINE_MSVLD },
	{ 0x11, "PMSPPP", NULL, NVKM_ENGINE_MSPPP },
	{ 0x13, "PCOUNTER" },
	{ 0x14, "PMSPDEC", NULL, NVKM_ENGINE_MSPDEC },
	{ 0x15, "PCE0", NULL, NVKM_ENGINE_CE, 0 },
	{ 0x16, "PCE1", NULL, NVKM_ENGINE_CE, 1 },
	{ 0x17, "PMU" },
	{}
};

static const struct nvkm_enum
gf100_fifo_mmu_fault_reason[] = {
	{ 0x00, "PT_NOT_PRESENT" },
	{ 0x01, "PT_TOO_SHORT" },
	{ 0x02, "PAGE_NOT_PRESENT" },
	{ 0x03, "VM_LIMIT_EXCEEDED" },
	{ 0x04, "NO_CHANNEL" },
	{ 0x05, "PAGE_SYSTEM_ONLY" },
	{ 0x06, "PAGE_READ_ONLY" },
	{ 0x0a, "COMPRESSED_SYSRAM" },
	{ 0x0c, "INVALID_STORAGE_TYPE" },
	{}
};

static const struct nvkm_enum
gf100_fifo_mmu_fault_hubclient[] = {
	{ 0x01, "PCOPY0" },
	{ 0x02, "PCOPY1" },
	{ 0x04, "DISPATCH" },
	{ 0x05, "CTXCTL" },
	{ 0x06, "PFIFO" },
	{ 0x07, "BAR_READ" },
	{ 0x08, "BAR_WRITE" },
	{ 0x0b, "PVP" },
	{ 0x0c, "PMSPPP" },
	{ 0x0d, "PMSVLD" },
	{ 0x11, "PCOUNTER" },
	{ 0x12, "PMU" },
	{ 0x14, "CCACHE" },
	{ 0x15, "CCACHE_POST" },
	{}
};

static const struct nvkm_enum
gf100_fifo_mmu_fault_gpcclient[] = {
	{ 0x01, "TEX" },
	{ 0x0c, "ESETUP" },
	{ 0x0e, "CTXCTL" },
	{ 0x0f, "PROP" },
	{}
};

const struct nvkm_enum
gf100_fifo_mmu_fault_access[] = {
	{ 0x00, "READ" },
	{ 0x01, "WRITE" },
	{}
};

void
gf100_fifo_mmu_fault_recover(struct nvkm_fifo *fifo, struct nvkm_fault_data *info)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const struct nvkm_enum *er, *ee, *ec, *ea;
	struct nvkm_engine *engine = NULL;
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	struct nvkm_chan *chan;
	unsigned long flags;
	char ct[8] = "HUB/";

	/* Lookup engine by MMU fault ID. */
	nvkm_runl_foreach(runl, fifo) {
		engn = nvkm_runl_find_engn(engn, runl, engn->fault == info->engine);
		if (engn) {
			/* Fault triggered by CTXSW_TIMEOUT recovery procedure. */
			if (engn->func->mmu_fault_triggered &&
			    engn->func->mmu_fault_triggered(engn)) {
				nvkm_runl_rc_engn(runl, engn);
				return;
			}

			engine = engn->engine;
			break;
		}
	}

	er = nvkm_enum_find(fifo->func->mmu_fault->reason, info->reason);
	ee = nvkm_enum_find(fifo->func->mmu_fault->engine, info->engine);
	if (info->hub) {
		ec = nvkm_enum_find(fifo->func->mmu_fault->hubclient, info->client);
	} else {
		ec = nvkm_enum_find(fifo->func->mmu_fault->gpcclient, info->client);
		snprintf(ct, sizeof(ct), "GPC%d/", info->gpc);
	}
	ea = nvkm_enum_find(fifo->func->mmu_fault->access, info->access);

	/* Handle BAR faults. */
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
			break;
		}
	}

	chan = nvkm_chan_get_inst(&fifo->engine, info->inst, &flags);

	nvkm_error(subdev,
		   "fault %02x [%s] at %016llx engine %02x [%s] client %02x "
		   "[%s%s] reason %02x [%s] on channel %d [%010llx %s]\n",
		   info->access, ea ? ea->name : "", info->addr,
		   info->engine, ee ? ee->name : engine ? engine->subdev.name : "",
		   info->client, ct, ec ? ec->name : "",
		   info->reason, er ? er->name : "",
		   chan ? chan->id : -1, info->inst, chan ? chan->name : "unknown");

	/* Handle host/engine faults. */
	if (chan)
		nvkm_runl_rc_cgrp(chan->cgrp);

	nvkm_chan_put(&chan, flags);
}

static const struct nvkm_fifo_func_mmu_fault
gf100_fifo_mmu_fault = {
	.recover = gf100_fifo_mmu_fault_recover,
	.access = gf100_fifo_mmu_fault_access,
	.engine = gf100_fifo_mmu_fault_engine,
	.reason = gf100_fifo_mmu_fault_reason,
	.hubclient = gf100_fifo_mmu_fault_hubclient,
	.gpcclient = gf100_fifo_mmu_fault_gpcclient,
};

void
gf100_fifo_intr_ctxsw_timeout(struct nvkm_fifo *fifo, u32 engm)
{
	struct nvkm_runl *runl;
	struct nvkm_engn *engn, *engn2;
	bool cgid, cgid2;
	int id, id2;

	nvkm_runl_foreach(runl, fifo) {
		/* Stop the runlist, and go through all engines serving it. */
		nvkm_runl_block(runl);
		nvkm_runl_foreach_engn_cond(engn, runl, engm & BIT(engn->id)) {
			/* Determine what channel (group) the engine is on. */
			id = engn->func->cxid(engn, &cgid);
			if (id >= 0) {
				/* Trigger MMU fault on any engine(s) on that channel (group). */
				nvkm_runl_foreach_engn_cond(engn2, runl, engn2->func->cxid) {
					id2 = engn2->func->cxid(engn2, &cgid2);
					if (cgid2 == cgid && id2 == id)
						engn2->func->mmu_fault_trigger(engn2);
				}
			}
		}
		nvkm_runl_allow(runl); /* HW will keep runlist blocked via ERROR_SCHED_DISABLE. */
	}
}

static void
gf100_fifo_intr_sched_ctxsw(struct nvkm_fifo *fifo)
{
	struct nvkm_runl *runl;
	struct nvkm_engn *engn;
	u32 engm = 0;

	/* Look for any engines that are busy, and awaiting chsw ack. */
	nvkm_runl_foreach(runl, fifo) {
		nvkm_runl_foreach_engn_cond(engn, runl, engn->func->chsw) {
			if (WARN_ON(engn->fault < 0) || !engn->func->chsw(engn))
				continue;

			engm |= BIT(engn->id);
		}
	}

	if (!engm)
		return;

	fifo->func->intr_ctxsw_timeout(fifo, engm);
}

static const struct nvkm_enum
gf100_fifo_intr_sched_names[] = {
	{ 0x0a, "CTXSW_TIMEOUT" },
	{}
};

void
gf100_fifo_intr_sched(struct nvkm_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x00254c);
	u32 code = intr & 0x000000ff;
	const struct nvkm_enum *en;

	en = nvkm_enum_find(gf100_fifo_intr_sched_names, code);

	nvkm_error(subdev, "SCHED_ERROR %02x [%s]\n", code, en ? en->name : "");

	switch (code) {
	case 0x0a:
		gf100_fifo_intr_sched_ctxsw(fifo);
		break;
	default:
		break;
	}
}

void
gf100_fifo_intr_mmu_fault_unit(struct nvkm_fifo *fifo, int unit)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	u32 inst = nvkm_rd32(device, 0x002800 + (unit * 0x10));
	u32 valo = nvkm_rd32(device, 0x002804 + (unit * 0x10));
	u32 vahi = nvkm_rd32(device, 0x002808 + (unit * 0x10));
	u32 type = nvkm_rd32(device, 0x00280c + (unit * 0x10));
	struct nvkm_fault_data info;

	info.inst   =  (u64)inst << 12;
	info.addr   = ((u64)vahi << 32) | valo;
	info.time   = 0;
	info.engine = unit;
	info.valid  = 1;
	info.gpc    = (type & 0x1f000000) >> 24;
	info.client = (type & 0x00001f00) >> 8;
	info.access = (type & 0x00000080) >> 7;
	info.hub    = (type & 0x00000040) >> 6;
	info.reason = (type & 0x0000000f);

	nvkm_fifo_fault(fifo, &info);
}

void
gf100_fifo_intr_mmu_fault(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	unsigned long mask = nvkm_rd32(device, 0x00259c);
	int unit;

	for_each_set_bit(unit, &mask, 32) {
		fifo->func->intr_mmu_fault_unit(fifo, unit);
		nvkm_wr32(device, 0x00259c, BIT(unit));
	}
}

bool
gf100_fifo_intr_pbdma(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_runq *runq;
	u32 mask = nvkm_rd32(device, 0x0025a0);
	bool handled = false;

	nvkm_runq_foreach_cond(runq, fifo, mask & BIT(runq->id)) {
		if (runq->func->intr(runq, NULL))
			handled = true;

		nvkm_wr32(device, 0x0025a0, BIT(runq->id));
	}

	return handled;
}

static void
gf100_fifo_intr_runlist(struct nvkm_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x002a00);

	if (intr & 0x10000000) {
		nvkm_wr32(device, 0x002a00, 0x10000000);
		intr &= ~0x10000000;
	}

	if (intr) {
		nvkm_error(subdev, "RUNLIST %08x\n", intr);
		nvkm_wr32(device, 0x002a00, intr);
	}
}

static void
gf100_fifo_intr_engine_unit(struct nvkm_fifo *fifo, int engn)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x0025a8 + (engn * 0x04));
	u32 inte = nvkm_rd32(device, 0x002628);
	u32 unkn;

	nvkm_wr32(device, 0x0025a8 + (engn * 0x04), intr);

	for (unkn = 0; unkn < 8; unkn++) {
		u32 ints = (intr >> (unkn * 0x04)) & inte;
		if (ints & 0x1) {
			nvkm_event_ntfy(&fifo->nonstall.event, 0, NVKM_FIFO_NONSTALL_EVENT);
			ints &= ~1;
		}
		if (ints) {
			nvkm_error(subdev, "ENGINE %d %d %01x", engn, unkn, ints);
			nvkm_mask(device, 0x002628, ints, 0);
		}
	}
}

static void
gf100_fifo_intr_engine(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	u32 mask = nvkm_rd32(device, 0x0025a4);

	while (mask) {
		u32 unit = __ffs(mask);
		gf100_fifo_intr_engine_unit(fifo, unit);
		mask &= ~(1 << unit);
	}
}

static irqreturn_t
gf100_fifo_intr(struct nvkm_inth *inth)
{
	struct nvkm_fifo *fifo = container_of(inth, typeof(*fifo), engine.subdev.inth);
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x002140);
	u32 stat = nvkm_rd32(device, 0x002100) & mask;

	if (stat & 0x00000001) {
		u32 intr = nvkm_rd32(device, 0x00252c);
		nvkm_warn(subdev, "INTR 00000001: %08x\n", intr);
		nvkm_wr32(device, 0x002100, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000100) {
		gf100_fifo_intr_sched(fifo);
		nvkm_wr32(device, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x00010000) {
		u32 intr = nvkm_rd32(device, 0x00256c);
		nvkm_warn(subdev, "INTR 00010000: %08x\n", intr);
		nvkm_wr32(device, 0x002100, 0x00010000);
		stat &= ~0x00010000;
	}

	if (stat & 0x01000000) {
		u32 intr = nvkm_rd32(device, 0x00258c);
		nvkm_warn(subdev, "INTR 01000000: %08x\n", intr);
		nvkm_wr32(device, 0x002100, 0x01000000);
		stat &= ~0x01000000;
	}

	if (stat & 0x10000000) {
		gf100_fifo_intr_mmu_fault(fifo);
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		if (gf100_fifo_intr_pbdma(fifo))
			stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		gf100_fifo_intr_runlist(fifo);
		stat &= ~0x40000000;
	}

	if (stat & 0x80000000) {
		gf100_fifo_intr_engine(fifo);
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
gf100_fifo_init_pbdmas(struct nvkm_fifo *fifo, u32 mask)
{
	struct nvkm_device *device = fifo->engine.subdev.device;

	/* Enable PBDMAs. */
	nvkm_wr32(device, 0x000204, mask);
	nvkm_wr32(device, 0x002204, mask);

	/* Assign engines to PBDMAs. */
	if ((mask & 7) == 7) {
		nvkm_wr32(device, 0x002208, ~(1 << 0)); /* PGRAPH */
		nvkm_wr32(device, 0x00220c, ~(1 << 1)); /* PVP */
		nvkm_wr32(device, 0x002210, ~(1 << 1)); /* PMSPP */
		nvkm_wr32(device, 0x002214, ~(1 << 1)); /* PMSVLD */
		nvkm_wr32(device, 0x002218, ~(1 << 2)); /* PCE0 */
		nvkm_wr32(device, 0x00221c, ~(1 << 1)); /* PCE1 */
	}

	nvkm_mask(device, 0x002a04, 0xbfffffff, 0xbfffffff);
}

static void
gf100_fifo_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;

	nvkm_mask(device, 0x002200, 0x00000001, 0x00000001);
	nvkm_wr32(device, 0x002254, 0x10000000 | fifo->userd.bar1->addr >> 12);

	nvkm_wr32(device, 0x002100, 0xffffffff);
	nvkm_wr32(device, 0x002140, 0x7fffffff);
	nvkm_wr32(device, 0x002628, 0x00000001); /* ENGINE_INTR_EN */
}

static int
gf100_fifo_runl_ctor(struct nvkm_fifo *fifo)
{
	struct nvkm_runl *runl;

	runl = nvkm_runl_new(fifo, 0, 0, 0);
	if (IS_ERR(runl))
		return PTR_ERR(runl);

	nvkm_runl_add(runl,  0, fifo->func->engn, NVKM_ENGINE_GR, 0);
	nvkm_runl_add(runl,  1, fifo->func->engn, NVKM_ENGINE_MSPDEC, 0);
	nvkm_runl_add(runl,  2, fifo->func->engn, NVKM_ENGINE_MSPPP, 0);
	nvkm_runl_add(runl,  3, fifo->func->engn, NVKM_ENGINE_MSVLD, 0);
	nvkm_runl_add(runl,  4, fifo->func->engn, NVKM_ENGINE_CE, 0);
	nvkm_runl_add(runl,  5, fifo->func->engn, NVKM_ENGINE_CE, 1);
	nvkm_runl_add(runl, 15,   &gf100_engn_sw, NVKM_ENGINE_SW, 0);
	return 0;
}

int
gf100_fifo_runq_nr(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	u32 save;

	/* Determine number of PBDMAs by checking valid enable bits. */
	save = nvkm_mask(device, 0x000204, 0xffffffff, 0xffffffff);
	save = nvkm_mask(device, 0x000204, 0xffffffff, save);
	return hweight32(save);
}

int
gf100_fifo_chid_ctor(struct nvkm_fifo *fifo, int nr)
{
	return nvkm_chid_new(&nvkm_chan_event, &fifo->engine.subdev, nr, 0, nr, &fifo->chid);
}

static const struct nvkm_fifo_func
gf100_fifo = {
	.chid_nr = nv50_fifo_chid_nr,
	.chid_ctor = gf100_fifo_chid_ctor,
	.runq_nr = gf100_fifo_runq_nr,
	.runl_ctor = gf100_fifo_runl_ctor,
	.init = gf100_fifo_init,
	.init_pbdmas = gf100_fifo_init_pbdmas,
	.intr = gf100_fifo_intr,
	.intr_mmu_fault_unit = gf100_fifo_intr_mmu_fault_unit,
	.intr_ctxsw_timeout = gf100_fifo_intr_ctxsw_timeout,
	.mmu_fault = &gf100_fifo_mmu_fault,
	.nonstall = &gf100_fifo_nonstall,
	.runl = &gf100_runl,
	.runq = &gf100_runq,
	.engn = &gf100_engn,
	.cgrp = {{                            }, &nv04_cgrp },
	.chan = {{ 0, 0, FERMI_CHANNEL_GPFIFO }, &gf100_chan },
};

int
gf100_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&gf100_fifo, device, type, inst, pfifo);
}
