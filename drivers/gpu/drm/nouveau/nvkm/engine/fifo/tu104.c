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
#include "gk104.h"
#include "cgrp.h"
#include "changk104.h"
#include "user.h"

#include <core/gpuobj.h>

#include <nvif/class.h>

static void
tu104_fifo_runlist_commit(struct gk104_fifo *fifo, int runl,
			  struct nvkm_memory *mem, int nr)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u64 addr = nvkm_memory_addr(mem);
	/*XXX: target? */

	nvkm_wr32(device, 0x002b00 + (runl * 0x10), lower_32_bits(addr));
	nvkm_wr32(device, 0x002b04 + (runl * 0x10), upper_32_bits(addr));
	nvkm_wr32(device, 0x002b08 + (runl * 0x10), nr);

	/*XXX: how to wait? can you even wait? */
}

const struct gk104_fifo_runlist_func
tu104_fifo_runlist = {
	.size = 16,
	.cgrp = gv100_fifo_runlist_cgrp,
	.chan = gv100_fifo_runlist_chan,
	.commit = tu104_fifo_runlist_commit,
};

static const struct nvkm_enum
tu104_fifo_fault_engine[] = {
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

static void
tu104_fifo_pbdma_init(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	const u32 mask = (1 << fifo->pbdma_nr) - 1;
	/*XXX: this is a bit of a guess at this point in time. */
	nvkm_mask(device, 0xb65000, 0x80000fff, 0x80000000 | mask);
}

static const struct gk104_fifo_pbdma_func
tu104_fifo_pbdma = {
	.nr = gm200_fifo_pbdma_nr,
	.init = tu104_fifo_pbdma_init,
	.init_timeout = gk208_fifo_pbdma_init_timeout,
};

static const struct gk104_fifo_func
tu104_fifo = {
	.pbdma = &tu104_fifo_pbdma,
	.fault.access = gv100_fifo_fault_access,
	.fault.engine = tu104_fifo_fault_engine,
	.fault.reason = gv100_fifo_fault_reason,
	.fault.hubclient = gv100_fifo_fault_hubclient,
	.fault.gpcclient = gv100_fifo_fault_gpcclient,
	.runlist = &tu104_fifo_runlist,
	.user = {{-1,-1,VOLTA_USERMODE_A       }, tu104_fifo_user_new   },
	.chan = {{ 0, 0,TURING_CHANNEL_GPFIFO_A}, tu104_fifo_gpfifo_new },
	.cgrp_force = true,
};

int
tu104_fifo_new(struct nvkm_device *device, int index, struct nvkm_fifo **pfifo)
{
	return gk104_fifo_new_(&tu104_fifo, device, index, 4096, pfifo);
}
