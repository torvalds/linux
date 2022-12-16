/*
 * Copyright 2016 Red Hat Inc.
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

#include <core/memory.h>
#include <subdev/timer.h>

#include <nvif/class.h>

void
gk110_chan_preempt(struct nvkm_chan *chan)
{
	struct nvkm_cgrp *cgrp = chan->cgrp;

	if (cgrp->hw) {
		cgrp->func->preempt(cgrp);
		return;
	}

	gf100_chan_preempt(chan);
}

const struct nvkm_chan_func
gk110_chan = {
	.inst = &gf100_chan_inst,
	.userd = &gk104_chan_userd,
	.ramfc = &gk104_chan_ramfc,
	.bind = gk104_chan_bind,
	.unbind = gk104_chan_unbind,
	.start = gk104_chan_start,
	.stop = gk104_chan_stop,
	.preempt = gk110_chan_preempt,
};

static void
gk110_cgrp_preempt(struct nvkm_cgrp *cgrp)
{
	nvkm_wr32(cgrp->runl->fifo->engine.subdev.device, 0x002634, 0x01000000 | cgrp->id);
}

const struct nvkm_cgrp_func
gk110_cgrp = {
	.preempt = gk110_cgrp_preempt,
};

void
gk110_runl_insert_cgrp(struct nvkm_cgrp *cgrp, struct nvkm_memory *memory, u64 offset)
{
	nvkm_wo32(memory, offset + 0, (cgrp->chan_nr << 26) | (128 << 18) |
				      (3 << 14) | 0x00002000 | cgrp->id);
	nvkm_wo32(memory, offset + 4, 0x00000000);
}

const struct nvkm_runl_func
gk110_runl = {
	.size = 8,
	.update = nv50_runl_update,
	.insert_cgrp = gk110_runl_insert_cgrp,
	.insert_chan = gk104_runl_insert_chan,
	.commit = gk104_runl_commit,
	.wait = nv50_runl_wait,
	.pending = gk104_runl_pending,
	.block = gk104_runl_block,
	.allow = gk104_runl_allow,
	.fault_clear = gk104_runl_fault_clear,
	.preempt_pending = gf100_runl_preempt_pending,
};

int
gk110_fifo_chid_ctor(struct nvkm_fifo *fifo, int nr)
{
	int ret;

	ret = nvkm_chid_new(&nvkm_chan_event, &fifo->engine.subdev, nr, 0, nr, &fifo->cgid);
	if (ret)
		return ret;

	return gf100_fifo_chid_ctor(fifo, nr);
}

static const struct nvkm_fifo_func
gk110_fifo = {
	.chid_nr = gk104_fifo_chid_nr,
	.chid_ctor = gk110_fifo_chid_ctor,
	.runq_nr = gf100_fifo_runq_nr,
	.runl_ctor = gk104_fifo_runl_ctor,
	.init = gk104_fifo_init,
	.init_pbdmas = gk104_fifo_init_pbdmas,
	.intr = gk104_fifo_intr,
	.intr_mmu_fault_unit = gf100_fifo_intr_mmu_fault_unit,
	.intr_ctxsw_timeout = gf100_fifo_intr_ctxsw_timeout,
	.mmu_fault = &gk104_fifo_mmu_fault,
	.nonstall = &gf100_fifo_nonstall,
	.runl = &gk110_runl,
	.runq = &gk104_runq,
	.engn = &gk104_engn,
	.engn_ce = &gk104_engn_ce,
	.cgrp = {{ 0, 0, KEPLER_CHANNEL_GROUP_A  }, &gk110_cgrp },
	.chan = {{ 0, 0, KEPLER_CHANNEL_GPFIFO_B }, &gk110_chan },
};

int
gk110_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&gk110_fifo, device, type, inst, pfifo);
}
