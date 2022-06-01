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
#include "cgrp.h"
#include "chan.h"
#include "chid.h"
#include "runl.h"

#include "gk104.h"
#include "changk104.h"

#include <core/memory.h>

#include <nvif/class.h>

const struct nvkm_chan_func
gk110_chan = {
};

const struct nvkm_cgrp_func
gk110_cgrp = {
};

void
gk110_fifo_runlist_cgrp(struct nvkm_fifo_cgrp *cgrp,
			struct nvkm_memory *memory, u32 offset)
{
	nvkm_wo32(memory, offset + 0, (cgrp->chan_nr << 26) | (128 << 18) |
				      (3 << 14) | 0x00002000 | cgrp->id);
	nvkm_wo32(memory, offset + 4, 0x00000000);
}

const struct gk104_fifo_runlist_func
gk110_fifo_runlist = {
	.size = 8,
	.cgrp = gk110_fifo_runlist_cgrp,
	.chan = gk104_fifo_runlist_chan,
	.commit = gk104_fifo_runlist_commit,
};

const struct nvkm_runl_func
gk110_runl = {
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
	.dtor = gk104_fifo_dtor,
	.oneinit = gk104_fifo_oneinit,
	.chid_nr = gk104_fifo_chid_nr,
	.chid_ctor = gk110_fifo_chid_ctor,
	.runq_nr = gf100_fifo_runq_nr,
	.runl_ctor = gk104_fifo_runl_ctor,
	.init = gk104_fifo_init,
	.fini = gk104_fifo_fini,
	.intr = gk104_fifo_intr,
	.intr_mmu_fault_unit = gf100_fifo_intr_mmu_fault_unit,
	.mmu_fault = &gk104_fifo_mmu_fault,
	.fault.access = gk104_fifo_fault_access,
	.fault.engine = gk104_fifo_fault_engine,
	.fault.reason = gk104_fifo_fault_reason,
	.fault.hubclient = gk104_fifo_fault_hubclient,
	.fault.gpcclient = gk104_fifo_fault_gpcclient,
	.engine_id = gk104_fifo_engine_id,
	.recover_chan = gk104_fifo_recover_chan,
	.runlist = &gk110_fifo_runlist,
	.pbdma = &gk104_fifo_pbdma,
	.nonstall = &gf100_fifo_nonstall,
	.runl = &gk110_runl,
	.runq = &gk104_runq,
	.engn = &gk104_engn,
	.engn_ce = &gk104_engn_ce,
	.cgrp = {{ 0, 0, KEPLER_CHANNEL_GROUP_A  }, &gk110_cgrp },
	.chan = {{ 0, 0, KEPLER_CHANNEL_GPFIFO_B }, &gk110_chan, .ctor = &gk104_fifo_gpfifo_new },
};

int
gk110_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	return gk104_fifo_new_(&gk110_fifo, device, type, inst, 0, pfifo);
}
