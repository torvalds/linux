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

#include "regsnv04.h"

#include <core/ramht.h>
#include <subdev/instmem.h>
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <engine/sw.h>

#include <nvif/class.h>

void
nv04_chan_stop(struct nvkm_chan *chan)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_memory *fctx = device->imem->ramfc;
	const struct nvkm_ramfc_layout *c;
	unsigned long flags;
	u32 data = chan->ramfc_offset;
	u32 chid;

	/* prevent fifo context switches */
	spin_lock_irqsave(&fifo->lock, flags);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 0);

	/* if this channel is active, replace it with a null context */
	chid = nvkm_rd32(device, NV03_PFIFO_CACHE1_PUSH1) & fifo->chid->mask;
	if (chid == chan->id) {
		nvkm_mask(device, NV04_PFIFO_CACHE1_DMA_PUSH, 0x00000001, 0);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 0);
		nvkm_mask(device, NV04_PFIFO_CACHE1_PULL0, 0x00000001, 0);

		c = chan->func->ramfc->layout;
		nvkm_kmap(fctx);
		do {
			u32 rm = ((1ULL << c->bits) - 1) << c->regs;
			u32 cm = ((1ULL << c->bits) - 1) << c->ctxs;
			u32 rv = (nvkm_rd32(device, c->regp) &  rm) >> c->regs;
			u32 cv = (nvkm_ro32(fctx, c->ctxp + data) & ~cm);
			nvkm_wo32(fctx, c->ctxp + data, cv | (rv << c->ctxs));
		} while ((++c)->bits);
		nvkm_done(fctx);

		c = chan->func->ramfc->layout;
		do {
			nvkm_wr32(device, c->regp, 0x00000000);
		} while ((++c)->bits);

		nvkm_wr32(device, NV03_PFIFO_CACHE1_GET, 0);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUT, 0);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH1, fifo->chid->mask);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 1);
		nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	}

	/* restore normal operation, after disabling dma mode */
	nvkm_mask(device, NV04_PFIFO_MODE, BIT(chan->id), 0);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 1);
	spin_unlock_irqrestore(&fifo->lock, flags);
}

void
nv04_chan_start(struct nvkm_chan *chan)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	nvkm_mask(fifo->engine.subdev.device, NV04_PFIFO_MODE, BIT(chan->id), BIT(chan->id));
	spin_unlock_irqrestore(&fifo->lock, flags);
}

void
nv04_chan_ramfc_clear(struct nvkm_chan *chan)
{
	struct nvkm_memory *ramfc = chan->cgrp->runl->fifo->engine.subdev.device->imem->ramfc;
	const struct nvkm_ramfc_layout *c = chan->func->ramfc->layout;

	nvkm_kmap(ramfc);
	do {
		nvkm_wo32(ramfc, chan->ramfc_offset + c->ctxp, 0x00000000);
	} while ((++c)->bits);
	nvkm_done(ramfc);
}

static int
nv04_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	struct nvkm_memory *ramfc = chan->cgrp->runl->fifo->engine.subdev.device->imem->ramfc;
	const u32 base = chan->id * 32;

	chan->ramfc_offset = base;

	nvkm_kmap(ramfc);
	nvkm_wo32(ramfc, base + 0x00, offset);
	nvkm_wo32(ramfc, base + 0x04, offset);
	nvkm_wo32(ramfc, base + 0x08, chan->push->addr >> 4);
	nvkm_wo32(ramfc, base + 0x10, NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
				      NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
				      NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
				      NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nvkm_done(ramfc);
	return 0;
}

static const struct nvkm_chan_func_ramfc
nv04_chan_ramfc = {
	.layout = (const struct nvkm_ramfc_layout[]) {
		{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
		{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
		{ 16,  0, 0x08,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
		{ 16, 16, 0x08,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
		{ 32,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_STATE },
		{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
		{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_ENGINE },
		{ 32,  0, 0x18,  0, NV04_PFIFO_CACHE1_PULL1 },
		{}
	},
	.write = nv04_chan_ramfc_write,
	.clear = nv04_chan_ramfc_clear,
	.ctxdma = true,
};

const struct nvkm_chan_func_userd
nv04_chan_userd = {
	.bar = 0,
	.base = 0x800000,
	.size = 0x010000,
};

const struct nvkm_chan_func_inst
nv04_chan_inst = {
	.size = 0x1000,
};

static const struct nvkm_chan_func
nv04_chan = {
	.inst = &nv04_chan_inst,
	.userd = &nv04_chan_userd,
	.ramfc = &nv04_chan_ramfc,
	.start = nv04_chan_start,
	.stop = nv04_chan_stop,
};

const struct nvkm_cgrp_func
nv04_cgrp = {
};

void
nv04_eobj_ramht_del(struct nvkm_chan *chan, int hash)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_instmem *imem = fifo->engine.subdev.device->imem;

	mutex_lock(&fifo->mutex);
	nvkm_ramht_remove(imem->ramht, hash);
	mutex_unlock(&fifo->mutex);
}

static int
nv04_eobj_ramht_add(struct nvkm_engn *engn, struct nvkm_object *eobj, struct nvkm_chan *chan)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_instmem *imem = fifo->engine.subdev.device->imem;
	u32 context = 0x80000000 | chan->id << 24 | engn->id << 16;
	int hash;

	mutex_lock(&fifo->mutex);
	hash = nvkm_ramht_insert(imem->ramht, eobj, chan->id, 4, eobj->handle, context);
	mutex_unlock(&fifo->mutex);
	return hash;
}

const struct nvkm_engn_func
nv04_engn = {
	.ramht_add = nv04_eobj_ramht_add,
	.ramht_del = nv04_eobj_ramht_del,
};

void
nv04_fifo_pause(struct nvkm_fifo *fifo, unsigned long *pflags)
__acquires(fifo->lock)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	*pflags = flags;

	nvkm_wr32(device, NV03_PFIFO_CACHES, 0x00000000);
	nvkm_mask(device, NV04_PFIFO_CACHE1_PULL0, 0x00000001, 0x00000000);

	/* in some cases the puller may be left in an inconsistent state
	 * if you try to stop it while it's busy translating handles.
	 * sometimes you get a CACHE_ERROR, sometimes it just fails
	 * silently; sending incorrect instance offsets to PGRAPH after
	 * it's started up again.
	 *
	 * to avoid this, we invalidate the most recently calculated
	 * instance.
	 */
	nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, NV04_PFIFO_CACHE1_PULL0);
		if (!(tmp & NV04_PFIFO_CACHE1_PULL0_HASH_BUSY))
			break;
	);

	if (nvkm_rd32(device, NV04_PFIFO_CACHE1_PULL0) &
			  NV04_PFIFO_CACHE1_PULL0_HASH_FAILED)
		nvkm_wr32(device, NV03_PFIFO_INTR_0, NV_PFIFO_INTR_CACHE_ERROR);

	nvkm_wr32(device, NV04_PFIFO_CACHE1_HASH, 0x00000000);
}

void
nv04_fifo_start(struct nvkm_fifo *fifo, unsigned long *pflags)
__releases(fifo->lock)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	unsigned long flags = *pflags;

	nvkm_mask(device, NV04_PFIFO_CACHE1_PULL0, 0x00000001, 0x00000001);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 0x00000001);

	spin_unlock_irqrestore(&fifo->lock, flags);
}

const struct nvkm_runl_func
nv04_runl = {
};

static const char *
nv_dma_state_err(u32 state)
{
	static const char * const desc[] = {
		"NONE", "CALL_SUBR_ACTIVE", "INVALID_MTHD", "RET_SUBR_INACTIVE",
		"INVALID_CMD", "IB_EMPTY"/* NV50+ */, "MEM_FAULT", "UNK"
	};
	return desc[(state >> 29) & 0x7];
}

static bool
nv04_fifo_swmthd(struct nvkm_device *device, u32 chid, u32 addr, u32 data)
{
	struct nvkm_sw *sw = device->sw;
	const int subc = (addr & 0x0000e000) >> 13;
	const int mthd = (addr & 0x00001ffc);
	const u32 mask = 0x0000000f << (subc * 4);
	u32 engine = nvkm_rd32(device, 0x003280);
	bool handled = false;

	switch (mthd) {
	case 0x0000 ... 0x0000: /* subchannel's engine -> software */
		nvkm_wr32(device, 0x003280, (engine &= ~mask));
		fallthrough;
	case 0x0180 ... 0x01fc: /* handle -> instance */
		data = nvkm_rd32(device, 0x003258) & 0x0000ffff;
		fallthrough;
	case 0x0100 ... 0x017c:
	case 0x0200 ... 0x1ffc: /* pass method down to sw */
		if (!(engine & mask) && sw)
			handled = nvkm_sw_mthd(sw, chid, subc, mthd, data);
		break;
	default:
		break;
	}

	return handled;
}

static void
nv04_fifo_intr_cache_error(struct nvkm_fifo *fifo, u32 chid, u32 get)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_chan *chan;
	unsigned long flags;
	u32 pull0 = nvkm_rd32(device, 0x003250);
	u32 mthd, data;
	int ptr;

	/* NV_PFIFO_CACHE1_GET actually goes to 0xffc before wrapping on my
	 * G80 chips, but CACHE1 isn't big enough for this much data.. Tests
	 * show that it wraps around to the start at GET=0x800.. No clue as to
	 * why..
	 */
	ptr = (get & 0x7ff) >> 2;

	if (device->card_type < NV_40) {
		mthd = nvkm_rd32(device, NV04_PFIFO_CACHE1_METHOD(ptr));
		data = nvkm_rd32(device, NV04_PFIFO_CACHE1_DATA(ptr));
	} else {
		mthd = nvkm_rd32(device, NV40_PFIFO_CACHE1_METHOD(ptr));
		data = nvkm_rd32(device, NV40_PFIFO_CACHE1_DATA(ptr));
	}

	if (!(pull0 & 0x00000100) ||
	    !nv04_fifo_swmthd(device, chid, mthd, data)) {
		chan = nvkm_chan_get_chid(&fifo->engine, chid, &flags);
		nvkm_error(subdev, "CACHE_ERROR - "
			   "ch %d [%s] subc %d mthd %04x data %08x\n",
			   chid, chan ? chan->name : "unknown",
			   (mthd >> 13) & 7, mthd & 0x1ffc, data);
		nvkm_chan_put(&chan, flags);
	}

	nvkm_wr32(device, NV04_PFIFO_CACHE1_DMA_PUSH, 0);
	nvkm_wr32(device, NV03_PFIFO_INTR_0, NV_PFIFO_INTR_CACHE_ERROR);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0,
		nvkm_rd32(device, NV03_PFIFO_CACHE1_PUSH0) & ~1);
	nvkm_wr32(device, NV03_PFIFO_CACHE1_GET, get + 4);
	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0,
		nvkm_rd32(device, NV03_PFIFO_CACHE1_PUSH0) | 1);
	nvkm_wr32(device, NV04_PFIFO_CACHE1_HASH, 0);

	nvkm_wr32(device, NV04_PFIFO_CACHE1_DMA_PUSH,
		nvkm_rd32(device, NV04_PFIFO_CACHE1_DMA_PUSH) | 1);
	nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
}

static void
nv04_fifo_intr_dma_pusher(struct nvkm_fifo *fifo, u32 chid)
{
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 dma_get = nvkm_rd32(device, 0x003244);
	u32 dma_put = nvkm_rd32(device, 0x003240);
	u32 push = nvkm_rd32(device, 0x003220);
	u32 state = nvkm_rd32(device, 0x003228);
	struct nvkm_chan *chan;
	unsigned long flags;
	const char *name;

	chan = nvkm_chan_get_chid(&fifo->engine, chid, &flags);
	name = chan ? chan->name : "unknown";
	if (device->card_type == NV_50) {
		u32 ho_get = nvkm_rd32(device, 0x003328);
		u32 ho_put = nvkm_rd32(device, 0x003320);
		u32 ib_get = nvkm_rd32(device, 0x003334);
		u32 ib_put = nvkm_rd32(device, 0x003330);

		nvkm_error(subdev, "DMA_PUSHER - "
			   "ch %d [%s] get %02x%08x put %02x%08x ib_get %08x "
			   "ib_put %08x state %08x (err: %s) push %08x\n",
			   chid, name, ho_get, dma_get, ho_put, dma_put,
			   ib_get, ib_put, state, nv_dma_state_err(state),
			   push);

		/* METHOD_COUNT, in DMA_STATE on earlier chipsets */
		nvkm_wr32(device, 0x003364, 0x00000000);
		if (dma_get != dma_put || ho_get != ho_put) {
			nvkm_wr32(device, 0x003244, dma_put);
			nvkm_wr32(device, 0x003328, ho_put);
		} else
		if (ib_get != ib_put)
			nvkm_wr32(device, 0x003334, ib_put);
	} else {
		nvkm_error(subdev, "DMA_PUSHER - ch %d [%s] get %08x put %08x "
				   "state %08x (err: %s) push %08x\n",
			   chid, name, dma_get, dma_put, state,
			   nv_dma_state_err(state), push);

		if (dma_get != dma_put)
			nvkm_wr32(device, 0x003244, dma_put);
	}
	nvkm_chan_put(&chan, flags);

	nvkm_wr32(device, 0x003228, 0x00000000);
	nvkm_wr32(device, 0x003220, 0x00000001);
	nvkm_wr32(device, 0x002100, NV_PFIFO_INTR_DMA_PUSHER);
}

irqreturn_t
nv04_fifo_intr(struct nvkm_inth *inth)
{
	struct nvkm_fifo *fifo = container_of(inth, typeof(*fifo), engine.subdev.inth);
	struct nvkm_subdev *subdev = &fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, NV03_PFIFO_INTR_EN_0);
	u32 stat = nvkm_rd32(device, NV03_PFIFO_INTR_0) & mask;
	u32 reassign, chid, get, sem;

	reassign = nvkm_rd32(device, NV03_PFIFO_CACHES) & 1;
	nvkm_wr32(device, NV03_PFIFO_CACHES, 0);

	chid = nvkm_rd32(device, NV03_PFIFO_CACHE1_PUSH1) & fifo->chid->mask;
	get  = nvkm_rd32(device, NV03_PFIFO_CACHE1_GET);

	if (stat & NV_PFIFO_INTR_CACHE_ERROR) {
		nv04_fifo_intr_cache_error(fifo, chid, get);
		stat &= ~NV_PFIFO_INTR_CACHE_ERROR;
	}

	if (stat & NV_PFIFO_INTR_DMA_PUSHER) {
		nv04_fifo_intr_dma_pusher(fifo, chid);
		stat &= ~NV_PFIFO_INTR_DMA_PUSHER;
	}

	if (stat & NV_PFIFO_INTR_SEMAPHORE) {
		stat &= ~NV_PFIFO_INTR_SEMAPHORE;
		nvkm_wr32(device, NV03_PFIFO_INTR_0, NV_PFIFO_INTR_SEMAPHORE);

		sem = nvkm_rd32(device, NV10_PFIFO_CACHE1_SEMAPHORE);
		nvkm_wr32(device, NV10_PFIFO_CACHE1_SEMAPHORE, sem | 0x1);

		nvkm_wr32(device, NV03_PFIFO_CACHE1_GET, get + 4);
		nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	}

	if (device->card_type == NV_50) {
		if (stat & 0x00000010) {
			stat &= ~0x00000010;
			nvkm_wr32(device, 0x002100, 0x00000010);
		}

		if (stat & 0x40000000) {
			nvkm_wr32(device, 0x002100, 0x40000000);
			nvkm_event_ntfy(&fifo->nonstall.event, 0, NVKM_FIFO_NONSTALL_EVENT);
			stat &= ~0x40000000;
		}
	}

	if (stat) {
		nvkm_warn(subdev, "intr %08x\n", stat);
		nvkm_mask(device, NV03_PFIFO_INTR_EN_0, stat, 0x00000000);
		nvkm_wr32(device, NV03_PFIFO_INTR_0, stat);
	}

	nvkm_wr32(device, NV03_PFIFO_CACHES, reassign);
	return IRQ_HANDLED;
}

void
nv04_fifo_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_instmem *imem = device->imem;
	struct nvkm_ramht *ramht = imem->ramht;
	struct nvkm_memory *ramro = imem->ramro;
	struct nvkm_memory *ramfc = imem->ramfc;

	nvkm_wr32(device, NV04_PFIFO_DELAY_0, 0x000000ff);
	nvkm_wr32(device, NV04_PFIFO_DMA_TIMESLICE, 0x0101ffff);

	nvkm_wr32(device, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
					    ((ramht->bits - 9) << 16) |
					    (ramht->gpuobj->addr >> 8));
	nvkm_wr32(device, NV03_PFIFO_RAMRO, nvkm_memory_addr(ramro) >> 8);
	nvkm_wr32(device, NV03_PFIFO_RAMFC, nvkm_memory_addr(ramfc) >> 8);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH1, fifo->chid->mask);

	nvkm_wr32(device, NV03_PFIFO_INTR_0, 0xffffffff);
	nvkm_wr32(device, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 1);
	nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 1);
}

int
nv04_fifo_runl_ctor(struct nvkm_fifo *fifo)
{
	struct nvkm_runl *runl;

	runl = nvkm_runl_new(fifo, 0, 0, 0);
	if (IS_ERR(runl))
		return PTR_ERR(runl);

	nvkm_runl_add(runl, 0, fifo->func->engn_sw, NVKM_ENGINE_SW, 0);
	nvkm_runl_add(runl, 0, fifo->func->engn_sw, NVKM_ENGINE_DMAOBJ, 0);
	nvkm_runl_add(runl, 1, fifo->func->engn   , NVKM_ENGINE_GR, 0);
	nvkm_runl_add(runl, 2, fifo->func->engn   , NVKM_ENGINE_MPEG, 0); /* NV31- */
	return 0;
}

int
nv04_fifo_chid_ctor(struct nvkm_fifo *fifo, int nr)
{
	/* The last CHID is reserved by HW as a "channel invalid" marker. */
	return nvkm_chid_new(&nvkm_chan_event, &fifo->engine.subdev, nr, 0, nr - 1, &fifo->chid);
}

static int
nv04_fifo_chid_nr(struct nvkm_fifo *fifo)
{
	return 16;
}

static const struct nvkm_fifo_func
nv04_fifo = {
	.chid_nr = nv04_fifo_chid_nr,
	.chid_ctor = nv04_fifo_chid_ctor,
	.runl_ctor = nv04_fifo_runl_ctor,
	.init = nv04_fifo_init,
	.intr = nv04_fifo_intr,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.runl = &nv04_runl,
	.engn = &nv04_engn,
	.engn_sw = &nv04_engn,
	.cgrp = {{                        }, &nv04_cgrp },
	.chan = {{ 0, 0, NV03_CHANNEL_DMA }, &nv04_chan },
};

int
nv04_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&nv04_fifo, device, type, inst, pfifo);
}
