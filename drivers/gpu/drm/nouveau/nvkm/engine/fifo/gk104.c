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
#include "changk104.h"

#include <core/client.h>
#include <core/enum.h>
#include <core/gpuobj.h>
#include <subdev/bar.h>
#include <engine/sw.h>

#include <nvif/class.h>

void
gk104_fifo_uevent_fini(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x00000000);
}

void
gk104_fifo_uevent_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x80000000);
}

void
gk104_fifo_runlist_commit(struct gk104_fifo *fifo, u32 engine)
{
	struct gk104_fifo_engn *engn = &fifo->engine[engine];
	struct gk104_fifo_chan *chan;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_memory *cur;
	int nr = 0;
	int target;

	mutex_lock(&subdev->mutex);
	cur = engn->runlist[engn->cur_runlist];
	engn->cur_runlist = !engn->cur_runlist;

	nvkm_kmap(cur);
	list_for_each_entry(chan, &engn->chan, head) {
		nvkm_wo32(cur, (nr * 8) + 0, chan->base.chid);
		nvkm_wo32(cur, (nr * 8) + 4, 0x00000000);
		nr++;
	}
	nvkm_done(cur);

	target = (nvkm_memory_target(cur) == NVKM_MEM_TARGET_HOST) ? 0x3 : 0x0;

	nvkm_wr32(device, 0x002270, (nvkm_memory_addr(cur) >> 12) |
				    (target << 28));
	nvkm_wr32(device, 0x002274, (engine << 20) | nr);

	if (wait_event_timeout(engn->wait, !(nvkm_rd32(device, 0x002284 +
			       (engine * 0x08)) & 0x00100000),
				msecs_to_jiffies(2000)) == 0)
		nvkm_error(subdev, "runlist %d update timeout\n", engine);
	mutex_unlock(&subdev->mutex);
}

void
gk104_fifo_runlist_remove(struct gk104_fifo *fifo, struct gk104_fifo_chan *chan)
{
	mutex_lock(&fifo->base.engine.subdev.mutex);
	list_del_init(&chan->head);
	mutex_unlock(&fifo->base.engine.subdev.mutex);
}

void
gk104_fifo_runlist_insert(struct gk104_fifo *fifo, struct gk104_fifo_chan *chan)
{
	mutex_lock(&fifo->base.engine.subdev.mutex);
	list_add_tail(&chan->head, &fifo->engine[chan->engine].chan);
	mutex_unlock(&fifo->base.engine.subdev.mutex);
}

static inline struct nvkm_engine *
gk104_fifo_engine(struct gk104_fifo *fifo, u32 engn)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u64 subdevs = gk104_fifo_engine_subdev(engn);
	if (subdevs)
		return nvkm_device_engine(device, __ffs(subdevs));
	return NULL;
}

static void
gk104_fifo_recover_work(struct work_struct *work)
{
	struct gk104_fifo *fifo = container_of(work, typeof(*fifo), fault);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_engine *engine;
	unsigned long flags;
	u32 engn, engm = 0;
	u64 mask, todo;

	spin_lock_irqsave(&fifo->base.lock, flags);
	mask = fifo->mask;
	fifo->mask = 0ULL;
	spin_unlock_irqrestore(&fifo->base.lock, flags);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn))
		engm |= 1 << gk104_fifo_subdev_engine(engn);
	nvkm_mask(device, 0x002630, engm, engm);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn)) {
		if ((engine = nvkm_device_engine(device, engn))) {
			nvkm_subdev_fini(&engine->subdev, false);
			WARN_ON(nvkm_subdev_init(&engine->subdev));
		}
		gk104_fifo_runlist_commit(fifo, gk104_fifo_subdev_engine(engn));
	}

	nvkm_wr32(device, 0x00262c, engm);
	nvkm_mask(device, 0x002630, engm, 0x00000000);
}

static void
gk104_fifo_recover(struct gk104_fifo *fifo, struct nvkm_engine *engine,
		  struct gk104_fifo_chan *chan)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 chid = chan->base.chid;

	nvkm_error(subdev, "%s engine fault on channel %d, recovering...\n",
		   nvkm_subdev_name[engine->subdev.index], chid);
	assert_spin_locked(&fifo->base.lock);

	nvkm_mask(device, 0x800004 + (chid * 0x08), 0x00000800, 0x00000800);
	list_del_init(&chan->head);
	chan->killed = true;

	fifo->mask |= 1ULL << engine->subdev.index;
	schedule_work(&fifo->fault);
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
	struct nvkm_engine *engine;
	struct gk104_fifo_chan *chan;
	unsigned long flags;
	u32 engn;

	spin_lock_irqsave(&fifo->base.lock, flags);
	for (engn = 0; engn < ARRAY_SIZE(fifo->engine); engn++) {
		u32 stat = nvkm_rd32(device, 0x002640 + (engn * 0x08));
		u32 busy = (stat & 0x80000000);
		u32 next = (stat & 0x0fff0000) >> 16;
		u32 chsw = (stat & 0x00008000);
		u32 save = (stat & 0x00004000);
		u32 load = (stat & 0x00002000);
		u32 prev = (stat & 0x00000fff);
		u32 chid = load ? next : prev;
		(void)save;

		if (busy && chsw) {
			list_for_each_entry(chan, &fifo->engine[engn].chan, head) {
				if (chan->base.chid == chid) {
					engine = gk104_fifo_engine(fifo, engn);
					if (!engine)
						break;
					gk104_fifo_recover(fifo, engine, chan);
					break;
				}
			}
		}
	}
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

static const struct nvkm_enum
gk104_fifo_fault_engine[] = {
	{ 0x00, "GR", NULL, NVKM_ENGINE_GR },
	{ 0x03, "IFB", NULL, NVKM_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0x05, "BAR3", NULL, NVKM_SUBDEV_INSTMEM },
	{ 0x07, "PBDMA0", NULL, NVKM_ENGINE_FIFO },
	{ 0x08, "PBDMA1", NULL, NVKM_ENGINE_FIFO },
	{ 0x09, "PBDMA2", NULL, NVKM_ENGINE_FIFO },
	{ 0x10, "MSVLD", NULL, NVKM_ENGINE_MSVLD },
	{ 0x11, "MSPPP", NULL, NVKM_ENGINE_MSPPP },
	{ 0x13, "PERF" },
	{ 0x14, "MSPDEC", NULL, NVKM_ENGINE_MSPDEC },
	{ 0x15, "CE0", NULL, NVKM_ENGINE_CE0 },
	{ 0x16, "CE1", NULL, NVKM_ENGINE_CE1 },
	{ 0x17, "PMU" },
	{ 0x19, "MSENC", NULL, NVKM_ENGINE_MSENC },
	{ 0x1b, "CE2", NULL, NVKM_ENGINE_CE2 },
	{}
};

static const struct nvkm_enum
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

static const struct nvkm_enum
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

static const struct nvkm_enum
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

static void
gk104_fifo_intr_fault(struct gk104_fifo *fifo, int unit)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 inst = nvkm_rd32(device, 0x002800 + (unit * 0x10));
	u32 valo = nvkm_rd32(device, 0x002804 + (unit * 0x10));
	u32 vahi = nvkm_rd32(device, 0x002808 + (unit * 0x10));
	u32 stat = nvkm_rd32(device, 0x00280c + (unit * 0x10));
	u32 gpc    = (stat & 0x1f000000) >> 24;
	u32 client = (stat & 0x00001f00) >> 8;
	u32 write  = (stat & 0x00000080);
	u32 hub    = (stat & 0x00000040);
	u32 reason = (stat & 0x0000000f);
	const struct nvkm_enum *er, *eu, *ec;
	struct nvkm_engine *engine = NULL;
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	char gpcid[8] = "";

	er = nvkm_enum_find(gk104_fifo_fault_reason, reason);
	eu = nvkm_enum_find(gk104_fifo_fault_engine, unit);
	if (hub) {
		ec = nvkm_enum_find(gk104_fifo_fault_hubclient, client);
	} else {
		ec = nvkm_enum_find(gk104_fifo_fault_gpcclient, client);
		snprintf(gpcid, sizeof(gpcid), "GPC%d/", gpc);
	}

	if (eu) {
		switch (eu->data2) {
		case NVKM_SUBDEV_BAR:
			nvkm_mask(device, 0x001704, 0x00000000, 0x00000000);
			break;
		case NVKM_SUBDEV_INSTMEM:
			nvkm_mask(device, 0x001714, 0x00000000, 0x00000000);
			break;
		case NVKM_ENGINE_IFB:
			nvkm_mask(device, 0x001718, 0x00000000, 0x00000000);
			break;
		default:
			engine = nvkm_device_engine(device, eu->data2);
			break;
		}
	}

	chan = nvkm_fifo_chan_inst(&fifo->base, (u64)inst << 12, &flags);

	nvkm_error(subdev,
		   "%s fault at %010llx engine %02x [%s] client %02x [%s%s] "
		   "reason %02x [%s] on channel %d [%010llx %s]\n",
		   write ? "write" : "read", (u64)vahi << 32 | valo,
		   unit, eu ? eu->name : "", client, gpcid, ec ? ec->name : "",
		   reason, er ? er->name : "", chan ? chan->chid : -1,
		   (u64)inst << 12,
		   chan ? chan->object.client->name : "unknown");

	if (engine && chan)
		gk104_fifo_recover(fifo, engine, (void *)chan);
	nvkm_fifo_chan_put(&fifo->base, flags, &chan);
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
		nvkm_wr32(device, 0x0400c0 + (unit * 0x2000), 0x80600008);
	}

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
		u32 engn = __ffs(mask);
		wake_up(&fifo->engine[engn].wait);
		nvkm_wr32(device, 0x002a00, 1 << engn);
		mask &= ~(1 << engn);
	}
}

static void
gk104_fifo_intr_engine(struct gk104_fifo *fifo)
{
	nvkm_fifo_uevent(&fifo->base);
}

void
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
			gk104_fifo_intr_fault(fifo, unit);
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

void
gk104_fifo_fini(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	flush_work(&fifo->fault);
	/* allow mmu fault interrupts, even when we're not using fifo */
	nvkm_mask(device, 0x002140, 0x10000000, 0x10000000);
}

int
gk104_fifo_oneinit(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(fifo->engine); i++) {
		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      0x8000, 0x1000, false,
				      &fifo->engine[i].runlist[0]);
		if (ret)
			return ret;

		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      0x8000, 0x1000, false,
				      &fifo->engine[i].runlist[1]);
		if (ret)
			return ret;

		init_waitqueue_head(&fifo->engine[i].wait);
		INIT_LIST_HEAD(&fifo->engine[i].chan);
	}

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
			      fifo->base.nr * 0x200, 0x1000, true,
			      &fifo->user.mem);
	if (ret)
		return ret;

	ret = nvkm_bar_umap(device->bar, fifo->base.nr * 0x200, 12,
			    &fifo->user.bar);
	if (ret)
		return ret;

	nvkm_memory_map(fifo->user.mem, &fifo->user.bar, 0);
	return 0;
}

void
gk104_fifo_init(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int i;

	/* enable all available PBDMA units */
	nvkm_wr32(device, 0x000204, 0xffffffff);
	fifo->spoon_nr = hweight32(nvkm_rd32(device, 0x000204));
	nvkm_debug(subdev, "%d PBDMA unit(s)\n", fifo->spoon_nr);

	/* PBDMA[n] */
	for (i = 0; i < fifo->spoon_nr; i++) {
		nvkm_mask(device, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nvkm_wr32(device, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nvkm_wr32(device, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTREN */
	}

	/* PBDMA[n].HCE */
	for (i = 0; i < fifo->spoon_nr; i++) {
		nvkm_wr32(device, 0x040148 + (i * 0x2000), 0xffffffff); /* INTR */
		nvkm_wr32(device, 0x04014c + (i * 0x2000), 0xffffffff); /* INTREN */
	}

	nvkm_wr32(device, 0x002254, 0x10000000 | fifo->user.bar.offset >> 12);

	nvkm_wr32(device, 0x002100, 0xffffffff);
	nvkm_wr32(device, 0x002140, 0x7fffffff);
}

void *
gk104_fifo_dtor(struct nvkm_fifo *base)
{
	struct gk104_fifo *fifo = gk104_fifo(base);
	int i;

	nvkm_vm_put(&fifo->user.bar);
	nvkm_memory_del(&fifo->user.mem);

	for (i = 0; i < ARRAY_SIZE(fifo->engine); i++) {
		nvkm_memory_del(&fifo->engine[i].runlist[1]);
		nvkm_memory_del(&fifo->engine[i].runlist[0]);
	}

	return fifo;
}

int
gk104_fifo_new_(const struct nvkm_fifo_func *func, struct nvkm_device *device,
		int index, int nr, struct nvkm_fifo **pfifo)
{
	struct gk104_fifo *fifo;

	if (!(fifo = kzalloc(sizeof(*fifo), GFP_KERNEL)))
		return -ENOMEM;
	INIT_WORK(&fifo->fault, gk104_fifo_recover_work);
	*pfifo = &fifo->base;

	return nvkm_fifo_ctor(func, device, index, nr, &fifo->base);
}

static const struct nvkm_fifo_func
gk104_fifo = {
	.dtor = gk104_fifo_dtor,
	.oneinit = gk104_fifo_oneinit,
	.init = gk104_fifo_init,
	.fini = gk104_fifo_fini,
	.intr = gk104_fifo_intr,
	.uevent_init = gk104_fifo_uevent_init,
	.uevent_fini = gk104_fifo_uevent_fini,
	.chan = {
		&gk104_fifo_gpfifo_oclass,
		NULL
	},
};

int
gk104_fifo_new(struct nvkm_device *device, int index, struct nvkm_fifo **pfifo)
{
	return gk104_fifo_new_(&gk104_fifo, device, index, 4096, pfifo);
}
