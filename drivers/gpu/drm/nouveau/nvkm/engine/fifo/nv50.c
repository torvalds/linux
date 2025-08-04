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

#include <core/ramht.h>
#include <subdev/timer.h>

#include <nvif/class.h>

void
nv50_eobj_ramht_del(struct nvkm_chan *chan, int hash)
{
	nvkm_ramht_remove(chan->ramht, hash);
}

int
nv50_eobj_ramht_add(struct nvkm_engn *engn, struct nvkm_object *eobj, struct nvkm_chan *chan)
{
	return nvkm_ramht_insert(chan->ramht, eobj, 0, 4, eobj->handle, engn->id << 20);
}

void
nv50_chan_stop(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_mask(device, 0x002600 + (chan->id * 4), 0x80000000, 0x00000000);
}

void
nv50_chan_start(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_mask(device, 0x002600 + (chan->id * 4), 0x80000000, 0x80000000);
}

void
nv50_chan_unbind(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_wr32(device, 0x002600 + (chan->id * 4), 0x00000000);
}

static void
nv50_chan_bind(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_wr32(device, 0x002600 + (chan->id * 4), chan->ramfc->addr >> 12);
}

static int
nv50_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;
	const u32 limit2 = ilog2(length / 8);
	int ret;

	ret = nvkm_gpuobj_new(device, 0x0200, 0x1000, true, chan->inst, &chan->ramfc);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x1200, 0, true, chan->inst, &chan->eng);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x4000, 0, false, chan->inst, &chan->pgd);
	if (ret)
		return ret;

	ret = nvkm_ramht_new(device, 0x8000, 16, chan->inst, &chan->ramht);
	if (ret)
		return ret;

	nvkm_kmap(chan->ramfc);
	nvkm_wo32(chan->ramfc, 0x3c, 0x403f6078);
	nvkm_wo32(chan->ramfc, 0x44, 0x01003fff);
	nvkm_wo32(chan->ramfc, 0x48, chan->push->node->offset >> 4);
	nvkm_wo32(chan->ramfc, 0x50, lower_32_bits(offset));
	nvkm_wo32(chan->ramfc, 0x54, upper_32_bits(offset) | (limit2 << 16));
	nvkm_wo32(chan->ramfc, 0x60, 0x7fffffff);
	nvkm_wo32(chan->ramfc, 0x78, 0x00000000);
	nvkm_wo32(chan->ramfc, 0x7c, 0x30000000 | devm);
	nvkm_wo32(chan->ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
				     (4 << 24) /* SEARCH_FULL */ |
				     (chan->ramht->gpuobj->node->offset >> 4));
	nvkm_done(chan->ramfc);
	return 0;
}

static const struct nvkm_chan_func_ramfc
nv50_chan_ramfc = {
	.write = nv50_chan_ramfc_write,
	.ctxdma = true,
	.devm = 0xfff,
};

const struct nvkm_chan_func_userd
nv50_chan_userd = {
	.bar = NVKM_BAR0_PRI,
	.base = 0xc00000,
	.size = 0x002000,
};

const struct nvkm_chan_func_inst
nv50_chan_inst = {
	.size = 0x10000,
	.vmm = true,
};

static const struct nvkm_chan_func
nv50_chan = {
	.inst = &nv50_chan_inst,
	.userd = &nv50_chan_userd,
	.ramfc = &nv50_chan_ramfc,
	.bind = nv50_chan_bind,
	.unbind = nv50_chan_unbind,
	.start = nv50_chan_start,
	.stop = nv50_chan_stop,
};

static void
nv50_ectx_bind(struct nvkm_engn *engn, struct nvkm_cctx *cctx, struct nvkm_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->cgrp->runl->fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u64 start = 0, limit = 0;
	u32 flags = 0, ptr0, save;

	switch (engn->engine->subdev.type) {
	case NVKM_ENGINE_GR    : ptr0 = 0x0000; break;
	case NVKM_ENGINE_MPEG  : ptr0 = 0x0060; break;
	default:
		WARN_ON(1);
		return;
	}

	if (!cctx) {
		/* HW bug workaround:
		 *
		 * PFIFO will hang forever if the connected engines don't report
		 * that they've processed the context switch request.
		 *
		 * In order for the kickoff to work, we need to ensure all the
		 * connected engines are in a state where they can answer.
		 *
		 * Newer chipsets don't seem to suffer from this issue, and well,
		 * there's also a "ignore these engines" bitmask reg we can use
		 * if we hit the issue there..
		 */
		save = nvkm_mask(device, 0x00b860, 0x00000001, 0x00000001);

		/* Tell engines to save out contexts. */
		nvkm_wr32(device, 0x0032fc, chan->inst->addr >> 12);
		nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x0032fc) != 0xffffffff)
				break;
		);
		nvkm_wr32(device, 0x00b860, save);
	} else {
		flags = 0x00190000;
		start = cctx->vctx->inst->addr;
		limit = start + cctx->vctx->inst->size - 1;
	}

	nvkm_kmap(chan->eng);
	nvkm_wo32(chan->eng, ptr0 + 0x00, flags);
	nvkm_wo32(chan->eng, ptr0 + 0x04, lower_32_bits(limit));
	nvkm_wo32(chan->eng, ptr0 + 0x08, lower_32_bits(start));
	nvkm_wo32(chan->eng, ptr0 + 0x0c, upper_32_bits(limit) << 24 |
					  lower_32_bits(start));
	nvkm_wo32(chan->eng, ptr0 + 0x10, 0x00000000);
	nvkm_wo32(chan->eng, ptr0 + 0x14, 0x00000000);
	nvkm_done(chan->eng);
}

static const struct nvkm_engn_func
nv50_engn = {
	.bind = nv50_ectx_bind,
	.ramht_add = nv50_eobj_ramht_add,
	.ramht_del = nv50_eobj_ramht_del,
};

const struct nvkm_engn_func
nv50_engn_sw = {
	.ramht_add = nv50_eobj_ramht_add,
	.ramht_del = nv50_eobj_ramht_del,
};

static bool
nv50_runl_pending(struct nvkm_runl *runl)
{
	return nvkm_rd32(runl->fifo->engine.subdev.device, 0x0032ec) & 0x00000100;
}

int
nv50_runl_wait(struct nvkm_runl *runl)
{
	struct nvkm_fifo *fifo = runl->fifo;

	nvkm_msec(fifo->engine.subdev.device, fifo->timeout.chan_msec,
		if (!nvkm_runl_update_pending(runl))
			return 0;
		usleep_range(1, 2);
	);

	return -ETIMEDOUT;
}

static void
nv50_runl_commit(struct nvkm_runl *runl, struct nvkm_memory *memory, u32 start, int count)
{
	struct nvkm_device *device = runl->fifo->engine.subdev.device;
	u64 addr = nvkm_memory_addr(memory) + start;

	nvkm_wr32(device, 0x0032f4, addr >> 12);
	nvkm_wr32(device, 0x0032ec, count);
}

static void
nv50_runl_insert_chan(struct nvkm_chan *chan, struct nvkm_memory *memory, u64 offset)
{
	nvkm_wo32(memory, offset, chan->id);
}

static struct nvkm_memory *
nv50_runl_alloc(struct nvkm_runl *runl, u32 *offset)
{
	const u32 segment = ALIGN((runl->cgrp_nr + runl->chan_nr) * runl->func->size, 0x1000);
	const u32 maxsize = (runl->cgid ? runl->cgid->nr : 0) + runl->chid->nr;
	int ret;

	if (unlikely(!runl->mem)) {
		ret = nvkm_memory_new(runl->fifo->engine.subdev.device, NVKM_MEM_TARGET_INST,
				      maxsize * 2 * runl->func->size, 0, false, &runl->mem);
		if (ret) {
			RUNL_ERROR(runl, "alloc %d\n", ret);
			return ERR_PTR(ret);
		}
	} else {
		if (runl->offset + segment >= nvkm_memory_size(runl->mem)) {
			ret = runl->func->wait(runl);
			if (ret) {
				RUNL_DEBUG(runl, "rewind timeout");
				return ERR_PTR(ret);
			}

			runl->offset = 0;
		}
	}

	*offset = runl->offset;
	runl->offset += segment;
	return runl->mem;
}

int
nv50_runl_update(struct nvkm_runl *runl)
{
	struct nvkm_memory *memory;
	struct nvkm_cgrp *cgrp;
	struct nvkm_chan *chan;
	u32 start, offset, count;

	/*TODO: prio, interleaving. */

	RUNL_TRACE(runl, "RAMRL: update cgrps:%d chans:%d", runl->cgrp_nr, runl->chan_nr);
	memory = nv50_runl_alloc(runl, &start);
	if (IS_ERR(memory))
		return PTR_ERR(memory);

	RUNL_TRACE(runl, "RAMRL: update start:%08x", start);
	offset = start;

	nvkm_kmap(memory);
	nvkm_runl_foreach_cgrp(cgrp, runl) {
		if (cgrp->hw) {
			CGRP_TRACE(cgrp, "     RAMRL+%08x: chans:%d", offset, cgrp->chan_nr);
			runl->func->insert_cgrp(cgrp, memory, offset);
			offset += runl->func->size;
		}

		nvkm_cgrp_foreach_chan(chan, cgrp) {
			CHAN_TRACE(chan, "RAMRL+%08x: [%s]", offset, chan->name);
			runl->func->insert_chan(chan, memory, offset);
			offset += runl->func->size;
		}
	}
	nvkm_done(memory);

	/*TODO: look into using features on newer HW to guarantee forward progress. */
	list_rotate_left(&runl->cgrps);

	count = (offset - start) / runl->func->size;
	RUNL_TRACE(runl, "RAMRL: commit start:%08x count:%d", start, count);

	runl->func->commit(runl, memory, start, count);
	return 0;
}

const struct nvkm_runl_func
nv50_runl = {
	.size = 4,
	.update = nv50_runl_update,
	.insert_chan = nv50_runl_insert_chan,
	.commit = nv50_runl_commit,
	.wait = nv50_runl_wait,
	.pending = nv50_runl_pending,
};

void
nv50_fifo_init(struct nvkm_fifo *fifo)
{
	struct nvkm_runl *runl = nvkm_runl_first(fifo);
	struct nvkm_device *device = fifo->engine.subdev.device;
	int i;

	nvkm_mask(device, 0x000200, 0x00000100, 0x00000000);
	nvkm_mask(device, 0x000200, 0x00000100, 0x00000100);
	nvkm_wr32(device, 0x00250c, 0x6f3cfc34);
	nvkm_wr32(device, 0x002044, 0x01003fff);

	nvkm_wr32(device, 0x002100, 0xffffffff);
	nvkm_wr32(device, 0x002140, 0xbfffffff);

	for (i = 0; i < 128; i++)
		nvkm_wr32(device, 0x002600 + (i * 4), 0x00000000);

	atomic_set(&runl->changed, 1);
	runl->func->update(runl);

	nvkm_wr32(device, 0x003200, 0x00000001);
	nvkm_wr32(device, 0x003250, 0x00000001);
	nvkm_wr32(device, 0x002500, 0x00000001);
}

int
nv50_fifo_chid_ctor(struct nvkm_fifo *fifo, int nr)
{
	/* CHID 0 is unusable (some kind of PIO channel?), 127 is "channel invalid". */
	return nvkm_chid_new(&nvkm_chan_event, &fifo->engine.subdev, nr, 1, nr - 2, &fifo->chid);
}

int
nv50_fifo_chid_nr(struct nvkm_fifo *fifo)
{
	return 128;
}

static const struct nvkm_fifo_func
nv50_fifo = {
	.chid_nr = nv50_fifo_chid_nr,
	.chid_ctor = nv50_fifo_chid_ctor,
	.runl_ctor = nv04_fifo_runl_ctor,
	.init = nv50_fifo_init,
	.intr = nv04_fifo_intr,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.runl = &nv50_runl,
	.engn = &nv50_engn,
	.engn_sw = &nv50_engn_sw,
	.cgrp = {{                           }, &nv04_cgrp },
	.chan = {{ 0, 0, NV50_CHANNEL_GPFIFO }, &nv50_chan },
};

int
nv50_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&nv50_fifo, device, type, inst, pfifo);
}
