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
#include "runl.h"

#include <core/gpuobj.h>
#include <subdev/fault.h>

#include <nvif/class.h>

static void
gp100_runl_insert_chan(struct nvkm_chan *chan, struct nvkm_memory *memory, u64 offset)
{
	nvkm_wo32(memory, offset + 0, chan->id | chan->runq << 14);
	nvkm_wo32(memory, offset + 4, chan->inst->addr >> 12);
}

static const struct nvkm_runl_func
gp100_runl = {
	.runqs = 2,
	.size = 8,
	.update = nv50_runl_update,
	.insert_cgrp = gk110_runl_insert_cgrp,
	.insert_chan = gp100_runl_insert_chan,
	.commit = gk104_runl_commit,
	.wait = nv50_runl_wait,
	.pending = gk104_runl_pending,
	.block = gk104_runl_block,
	.allow = gk104_runl_allow,
	.fault_clear = gk104_runl_fault_clear,
	.preempt_pending = gf100_runl_preempt_pending,
};

static const struct nvkm_enum
gp100_fifo_mmu_fault_engine[] = {
	{ 0x01, "DISPLAY" },
	{ 0x03, "IFB", NULL, NVKM_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0x05, "BAR2", NULL, NVKM_SUBDEV_INSTMEM },
	{ 0x06, "HOST0" },
	{ 0x07, "HOST1" },
	{ 0x08, "HOST2" },
	{ 0x09, "HOST3" },
	{ 0x0a, "HOST4" },
	{ 0x0b, "HOST5" },
	{ 0x0c, "HOST6" },
	{ 0x0d, "HOST7" },
	{ 0x0e, "HOST8" },
	{ 0x0f, "HOST9" },
	{ 0x10, "HOST10" },
	{ 0x13, "PERF" },
	{ 0x17, "PMU" },
	{ 0x18, "PTP" },
	{ 0x1f, "PHYSICAL" },
	{}
};

static const struct nvkm_fifo_func_mmu_fault
gp100_fifo_mmu_fault = {
	.recover = gf100_fifo_mmu_fault_recover,
	.access = gf100_fifo_mmu_fault_access,
	.engine = gp100_fifo_mmu_fault_engine,
	.reason = gk104_fifo_mmu_fault_reason,
	.hubclient = gk104_fifo_mmu_fault_hubclient,
	.gpcclient = gk104_fifo_mmu_fault_gpcclient,
};

static void
gp100_fifo_intr_mmu_fault_unit(struct nvkm_fifo *fifo, int unit)
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
	info.hub    = (type & 0x00100000) >> 20;
	info.access = (type & 0x00070000) >> 16;
	info.client = (type & 0x00007f00) >> 8;
	info.reason = (type & 0x0000001f);

	nvkm_fifo_fault(fifo, &info);
}

static const struct nvkm_fifo_func
gp100_fifo = {
	.chid_nr = gm200_fifo_chid_nr,
	.chid_ctor = gk110_fifo_chid_ctor,
	.runq_nr = gm200_fifo_runq_nr,
	.runl_ctor = gk104_fifo_runl_ctor,
	.init = gk104_fifo_init,
	.init_pbdmas = gk104_fifo_init_pbdmas,
	.intr = gk104_fifo_intr,
	.intr_mmu_fault_unit = gp100_fifo_intr_mmu_fault_unit,
	.intr_ctxsw_timeout = gf100_fifo_intr_ctxsw_timeout,
	.mmu_fault = &gp100_fifo_mmu_fault,
	.nonstall = &gf100_fifo_nonstall,
	.runl = &gp100_runl,
	.runq = &gk208_runq,
	.engn = &gk104_engn,
	.engn_ce = &gk104_engn_ce,
	.cgrp = {{ 0, 0, KEPLER_CHANNEL_GROUP_A  }, &gk110_cgrp, .force = true },
	.chan = {{ 0, 0, PASCAL_CHANNEL_GPFIFO_A }, &gm107_chan },
};

int
gp100_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&gp100_fifo, device, type, inst, pfifo);
}
