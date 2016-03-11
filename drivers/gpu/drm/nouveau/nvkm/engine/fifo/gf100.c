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
#include "gf100.h"
#include "changf100.h"

#include <core/client.h>
#include <core/enum.h>
#include <core/gpuobj.h>
#include <subdev/bar.h>
#include <engine/sw.h>

#include <nvif/class.h>

static void
gf100_fifo_uevent_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x80000000);
}

static void
gf100_fifo_uevent_fini(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x00000000);
}

void
gf100_fifo_runlist_commit(struct gf100_fifo *fifo)
{
	struct gf100_fifo_chan *chan;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_memory *cur;
	int nr = 0;
	int target;

	mutex_lock(&subdev->mutex);
	cur = fifo->runlist.mem[fifo->runlist.active];
	fifo->runlist.active = !fifo->runlist.active;

	nvkm_kmap(cur);
	list_for_each_entry(chan, &fifo->chan, head) {
		nvkm_wo32(cur, (nr * 8) + 0, chan->base.chid);
		nvkm_wo32(cur, (nr * 8) + 4, 0x00000004);
		nr++;
	}
	nvkm_done(cur);

	target = (nvkm_memory_target(cur) == NVKM_MEM_TARGET_HOST) ? 0x3 : 0x0;

	nvkm_wr32(device, 0x002270, (nvkm_memory_addr(cur) >> 12) |
				    (target << 28));
	nvkm_wr32(device, 0x002274, 0x01f00000 | nr);

	if (wait_event_timeout(fifo->runlist.wait,
			       !(nvkm_rd32(device, 0x00227c) & 0x00100000),
			       msecs_to_jiffies(2000)) == 0)
		nvkm_error(subdev, "runlist update timeout\n");
	mutex_unlock(&subdev->mutex);
}

void
gf100_fifo_runlist_remove(struct gf100_fifo *fifo, struct gf100_fifo_chan *chan)
{
	mutex_lock(&fifo->base.engine.subdev.mutex);
	list_del_init(&chan->head);
	mutex_unlock(&fifo->base.engine.subdev.mutex);
}

void
gf100_fifo_runlist_insert(struct gf100_fifo *fifo, struct gf100_fifo_chan *chan)
{
	mutex_lock(&fifo->base.engine.subdev.mutex);
	list_add_tail(&chan->head, &fifo->chan);
	mutex_unlock(&fifo->base.engine.subdev.mutex);
}

static inline int
gf100_fifo_engidx(struct gf100_fifo *fifo, u32 engn)
{
	switch (engn) {
	case NVKM_ENGINE_GR    : engn = 0; break;
	case NVKM_ENGINE_MSVLD : engn = 1; break;
	case NVKM_ENGINE_MSPPP : engn = 2; break;
	case NVKM_ENGINE_MSPDEC: engn = 3; break;
	case NVKM_ENGINE_CE0   : engn = 4; break;
	case NVKM_ENGINE_CE1   : engn = 5; break;
	default:
		return -1;
	}

	return engn;
}

static inline struct nvkm_engine *
gf100_fifo_engine(struct gf100_fifo *fifo, u32 engn)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;

	switch (engn) {
	case 0: engn = NVKM_ENGINE_GR; break;
	case 1: engn = NVKM_ENGINE_MSVLD; break;
	case 2: engn = NVKM_ENGINE_MSPPP; break;
	case 3: engn = NVKM_ENGINE_MSPDEC; break;
	case 4: engn = NVKM_ENGINE_CE0; break;
	case 5: engn = NVKM_ENGINE_CE1; break;
	default:
		return NULL;
	}

	return nvkm_device_engine(device, engn);
}

static void
gf100_fifo_recover_work(struct work_struct *w)
{
	struct gf100_fifo *fifo = container_of(w, typeof(*fifo), recover.work);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_engine *engine;
	unsigned long flags;
	u32 engn, engm = 0;
	u64 mask, todo;

	spin_lock_irqsave(&fifo->base.lock, flags);
	mask = fifo->recover.mask;
	fifo->recover.mask = 0ULL;
	spin_unlock_irqrestore(&fifo->base.lock, flags);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn))
		engm |= 1 << gf100_fifo_engidx(fifo, engn);
	nvkm_mask(device, 0x002630, engm, engm);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn)) {
		if ((engine = nvkm_device_engine(device, engn))) {
			nvkm_subdev_fini(&engine->subdev, false);
			WARN_ON(nvkm_subdev_init(&engine->subdev));
		}
	}

	gf100_fifo_runlist_commit(fifo);
	nvkm_wr32(device, 0x00262c, engm);
	nvkm_mask(device, 0x002630, engm, 0x00000000);
}

static void
gf100_fifo_recover(struct gf100_fifo *fifo, struct nvkm_engine *engine,
		   struct gf100_fifo_chan *chan)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 chid = chan->base.chid;

	nvkm_error(subdev, "%s engine fault on channel %d, recovering...\n",
		   nvkm_subdev_name[engine->subdev.index], chid);
	assert_spin_locked(&fifo->base.lock);

	nvkm_mask(device, 0x003004 + (chid * 0x08), 0x00000001, 0x00000000);
	list_del_init(&chan->head);
	chan->killed = true;

	fifo->recover.mask |= 1ULL << engine->subdev.index;
	schedule_work(&fifo->recover.work);
}

static const struct nvkm_enum
gf100_fifo_sched_reason[] = {
	{ 0x0a, "CTXSW_TIMEOUT" },
	{}
};

static void
gf100_fifo_intr_sched_ctxsw(struct gf100_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_engine *engine;
	struct gf100_fifo_chan *chan;
	unsigned long flags;
	u32 engn;

	spin_lock_irqsave(&fifo->base.lock, flags);
	for (engn = 0; engn < 6; engn++) {
		u32 stat = nvkm_rd32(device, 0x002640 + (engn * 0x04));
		u32 busy = (stat & 0x80000000);
		u32 save = (stat & 0x00100000); /* maybe? */
		u32 unk0 = (stat & 0x00040000);
		u32 unk1 = (stat & 0x00001000);
		u32 chid = (stat & 0x0000007f);
		(void)save;

		if (busy && unk0 && unk1) {
			list_for_each_entry(chan, &fifo->chan, head) {
				if (chan->base.chid == chid) {
					engine = gf100_fifo_engine(fifo, engn);
					if (!engine)
						break;
					gf100_fifo_recover(fifo, engine, chan);
					break;
				}
			}
		}
	}
	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

static void
gf100_fifo_intr_sched(struct gf100_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x00254c);
	u32 code = intr & 0x000000ff;
	const struct nvkm_enum *en;

	en = nvkm_enum_find(gf100_fifo_sched_reason, code);

	nvkm_error(subdev, "SCHED_ERROR %02x [%s]\n", code, en ? en->name : "");

	switch (code) {
	case 0x0a:
		gf100_fifo_intr_sched_ctxsw(fifo);
		break;
	default:
		break;
	}
}

static const struct nvkm_enum
gf100_fifo_fault_engine[] = {
	{ 0x00, "PGRAPH", NULL, NVKM_ENGINE_GR },
	{ 0x03, "PEEPHOLE", NULL, NVKM_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0x05, "BAR3", NULL, NVKM_SUBDEV_INSTMEM },
	{ 0x07, "PFIFO", NULL, NVKM_ENGINE_FIFO },
	{ 0x10, "PMSVLD", NULL, NVKM_ENGINE_MSVLD },
	{ 0x11, "PMSPPP", NULL, NVKM_ENGINE_MSPPP },
	{ 0x13, "PCOUNTER" },
	{ 0x14, "PMSPDEC", NULL, NVKM_ENGINE_MSPDEC },
	{ 0x15, "PCE0", NULL, NVKM_ENGINE_CE0 },
	{ 0x16, "PCE1", NULL, NVKM_ENGINE_CE1 },
	{ 0x17, "PMU" },
	{}
};

static const struct nvkm_enum
gf100_fifo_fault_reason[] = {
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
gf100_fifo_fault_hubclient[] = {
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
gf100_fifo_fault_gpcclient[] = {
	{ 0x01, "TEX" },
	{ 0x0c, "ESETUP" },
	{ 0x0e, "CTXCTL" },
	{ 0x0f, "PROP" },
	{}
};

static void
gf100_fifo_intr_fault(struct gf100_fifo *fifo, int unit)
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

	er = nvkm_enum_find(gf100_fifo_fault_reason, reason);
	eu = nvkm_enum_find(gf100_fifo_fault_engine, unit);
	if (hub) {
		ec = nvkm_enum_find(gf100_fifo_fault_hubclient, client);
	} else {
		ec = nvkm_enum_find(gf100_fifo_fault_gpcclient, client);
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
		gf100_fifo_recover(fifo, engine, (void *)chan);
	nvkm_fifo_chan_put(&fifo->base, flags, &chan);
}

static const struct nvkm_bitfield
gf100_fifo_pbdma_intr[] = {
/*	{ 0x00008000, "" }	seen with null ib push */
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

static void
gf100_fifo_intr_pbdma(struct gf100_fifo *fifo, int unit)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x040108 + (unit * 0x2000));
	u32 addr = nvkm_rd32(device, 0x0400c0 + (unit * 0x2000));
	u32 data = nvkm_rd32(device, 0x0400c4 + (unit * 0x2000));
	u32 chid = nvkm_rd32(device, 0x040120 + (unit * 0x2000)) & 0x7f;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00003ffc);
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	u32 show= stat;
	char msg[128];

	if (stat & 0x00800000) {
		if (device->sw) {
			if (nvkm_sw_mthd(device->sw, chid, subc, mthd, data))
				show &= ~0x00800000;
		}
	}

	if (show) {
		nvkm_snprintbf(msg, sizeof(msg), gf100_fifo_pbdma_intr, show);
		chan = nvkm_fifo_chan_chid(&fifo->base, chid, &flags);
		nvkm_error(subdev, "PBDMA%d: %08x [%s] ch %d [%010llx %s] "
				   "subc %d mthd %04x data %08x\n",
			   unit, show, msg, chid, chan ? chan->inst->addr : 0,
			   chan ? chan->object.client->name : "unknown",
			   subc, mthd, data);
		nvkm_fifo_chan_put(&fifo->base, flags, &chan);
	}

	nvkm_wr32(device, 0x0400c0 + (unit * 0x2000), 0x80600008);
	nvkm_wr32(device, 0x040108 + (unit * 0x2000), stat);
}

static void
gf100_fifo_intr_runlist(struct gf100_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x002a00);

	if (intr & 0x10000000) {
		wake_up(&fifo->runlist.wait);
		nvkm_wr32(device, 0x002a00, 0x10000000);
		intr &= ~0x10000000;
	}

	if (intr) {
		nvkm_error(subdev, "RUNLIST %08x\n", intr);
		nvkm_wr32(device, 0x002a00, intr);
	}
}

static void
gf100_fifo_intr_engine_unit(struct gf100_fifo *fifo, int engn)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x0025a8 + (engn * 0x04));
	u32 inte = nvkm_rd32(device, 0x002628);
	u32 unkn;

	nvkm_wr32(device, 0x0025a8 + (engn * 0x04), intr);

	for (unkn = 0; unkn < 8; unkn++) {
		u32 ints = (intr >> (unkn * 0x04)) & inte;
		if (ints & 0x1) {
			nvkm_fifo_uevent(&fifo->base);
			ints &= ~1;
		}
		if (ints) {
			nvkm_error(subdev, "ENGINE %d %d %01x",
				   engn, unkn, ints);
			nvkm_mask(device, 0x002628, ints, 0);
		}
	}
}

void
gf100_fifo_intr_engine(struct gf100_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 mask = nvkm_rd32(device, 0x0025a4);
	while (mask) {
		u32 unit = __ffs(mask);
		gf100_fifo_intr_engine_unit(fifo, unit);
		mask &= ~(1 << unit);
	}
}

static void
gf100_fifo_intr(struct nvkm_fifo *base)
{
	struct gf100_fifo *fifo = gf100_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
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
		u32 mask = nvkm_rd32(device, 0x00259c);
		while (mask) {
			u32 unit = __ffs(mask);
			gf100_fifo_intr_fault(fifo, unit);
			nvkm_wr32(device, 0x00259c, (1 << unit));
			mask &= ~(1 << unit);
		}
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 mask = nvkm_rd32(device, 0x0025a0);
		while (mask) {
			u32 unit = __ffs(mask);
			gf100_fifo_intr_pbdma(fifo, unit);
			nvkm_wr32(device, 0x0025a0, (1 << unit));
			mask &= ~(1 << unit);
		}
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
		nvkm_mask(device, 0x002140, stat, 0x00000000);
		nvkm_wr32(device, 0x002100, stat);
	}
}

static int
gf100_fifo_oneinit(struct nvkm_fifo *base)
{
	struct gf100_fifo *fifo = gf100_fifo(base);
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ret;

	/* Determine number of PBDMAs by checking valid enable bits. */
	nvkm_wr32(device, 0x002204, 0xffffffff);
	fifo->pbdma_nr = hweight32(nvkm_rd32(device, 0x002204));
	nvkm_debug(subdev, "%d PBDMA(s)\n", fifo->pbdma_nr);


	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0x1000,
			      false, &fifo->runlist.mem[0]);
	if (ret)
		return ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0x1000,
			      false, &fifo->runlist.mem[1]);
	if (ret)
		return ret;

	init_waitqueue_head(&fifo->runlist.wait);

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 128 * 0x1000,
			      0x1000, false, &fifo->user.mem);
	if (ret)
		return ret;

	ret = nvkm_bar_umap(device->bar, 128 * 0x1000, 12, &fifo->user.bar);
	if (ret)
		return ret;

	nvkm_memory_map(fifo->user.mem, &fifo->user.bar, 0);
	return 0;
}

static void
gf100_fifo_fini(struct nvkm_fifo *base)
{
	struct gf100_fifo *fifo = gf100_fifo(base);
	flush_work(&fifo->recover.work);
}

static void
gf100_fifo_init(struct nvkm_fifo *base)
{
	struct gf100_fifo *fifo = gf100_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int i;

	/* Enable PBDMAs. */
	nvkm_wr32(device, 0x000204, (1 << fifo->pbdma_nr) - 1);
	nvkm_wr32(device, 0x002204, (1 << fifo->pbdma_nr) - 1);

	/* Assign engines to PBDMAs. */
	if (fifo->pbdma_nr >= 3) {
		nvkm_wr32(device, 0x002208, ~(1 << 0)); /* PGRAPH */
		nvkm_wr32(device, 0x00220c, ~(1 << 1)); /* PVP */
		nvkm_wr32(device, 0x002210, ~(1 << 1)); /* PMSPP */
		nvkm_wr32(device, 0x002214, ~(1 << 1)); /* PMSVLD */
		nvkm_wr32(device, 0x002218, ~(1 << 2)); /* PCE0 */
		nvkm_wr32(device, 0x00221c, ~(1 << 1)); /* PCE1 */
	}

	/* PBDMA[n] */
	for (i = 0; i < fifo->pbdma_nr; i++) {
		nvkm_mask(device, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nvkm_wr32(device, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nvkm_wr32(device, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTREN */
	}

	nvkm_mask(device, 0x002200, 0x00000001, 0x00000001);
	nvkm_wr32(device, 0x002254, 0x10000000 | fifo->user.bar.offset >> 12);

	nvkm_wr32(device, 0x002100, 0xffffffff);
	nvkm_wr32(device, 0x002140, 0x7fffffff);
	nvkm_wr32(device, 0x002628, 0x00000001); /* ENGINE_INTR_EN */
}

static void *
gf100_fifo_dtor(struct nvkm_fifo *base)
{
	struct gf100_fifo *fifo = gf100_fifo(base);
	nvkm_vm_put(&fifo->user.bar);
	nvkm_memory_del(&fifo->user.mem);
	nvkm_memory_del(&fifo->runlist.mem[0]);
	nvkm_memory_del(&fifo->runlist.mem[1]);
	return fifo;
}

static const struct nvkm_fifo_func
gf100_fifo = {
	.dtor = gf100_fifo_dtor,
	.oneinit = gf100_fifo_oneinit,
	.init = gf100_fifo_init,
	.fini = gf100_fifo_fini,
	.intr = gf100_fifo_intr,
	.uevent_init = gf100_fifo_uevent_init,
	.uevent_fini = gf100_fifo_uevent_fini,
	.chan = {
		&gf100_fifo_gpfifo_oclass,
		NULL
	},
};

int
gf100_fifo_new(struct nvkm_device *device, int index, struct nvkm_fifo **pfifo)
{
	struct gf100_fifo *fifo;

	if (!(fifo = kzalloc(sizeof(*fifo), GFP_KERNEL)))
		return -ENOMEM;
	INIT_LIST_HEAD(&fifo->chan);
	INIT_WORK(&fifo->recover.work, gf100_fifo_recover_work);
	*pfifo = &fifo->base;

	return nvkm_fifo_ctor(&gf100_fifo, device, index, 128, &fifo->base);
}
