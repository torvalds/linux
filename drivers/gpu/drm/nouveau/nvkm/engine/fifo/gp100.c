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
#include "gk104.h"
#include "changk104.h"

#include <subdev/fault.h>

#include <nvif/class.h>

const struct nvkm_enum
gp100_fifo_fault_engine[] = {
	{ 0x01, "DISPLAY" },
	{ 0x03, "IFB", NULL, NVKM_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0x05, "BAR2", NULL, NVKM_SUBDEV_INSTMEM },
	{ 0x06, "HOST0", NULL, NVKM_ENGINE_FIFO },
	{ 0x07, "HOST1", NULL, NVKM_ENGINE_FIFO },
	{ 0x08, "HOST2", NULL, NVKM_ENGINE_FIFO },
	{ 0x09, "HOST3", NULL, NVKM_ENGINE_FIFO },
	{ 0x0a, "HOST4", NULL, NVKM_ENGINE_FIFO },
	{ 0x0b, "HOST5", NULL, NVKM_ENGINE_FIFO },
	{ 0x0c, "HOST6", NULL, NVKM_ENGINE_FIFO },
	{ 0x0d, "HOST7", NULL, NVKM_ENGINE_FIFO },
	{ 0x0e, "HOST8", NULL, NVKM_ENGINE_FIFO },
	{ 0x0f, "HOST9", NULL, NVKM_ENGINE_FIFO },
	{ 0x10, "HOST10", NULL, NVKM_ENGINE_FIFO },
	{ 0x13, "PERF" },
	{ 0x17, "PMU" },
	{ 0x18, "PTP" },
	{ 0x1f, "PHYSICAL" },
	{}
};

void
gp100_fifo_intr_fault(struct nvkm_fifo *fifo, int unit)
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

static const struct gk104_fifo_func
gp100_fifo = {
	.intr.fault = gp100_fifo_intr_fault,
	.pbdma = &gm200_fifo_pbdma,
	.fault.access = gk104_fifo_fault_access,
	.fault.engine = gp100_fifo_fault_engine,
	.fault.reason = gk104_fifo_fault_reason,
	.fault.hubclient = gk104_fifo_fault_hubclient,
	.fault.gpcclient = gk104_fifo_fault_gpcclient,
	.runlist = &gm107_fifo_runlist,
	.chan = {{0,0,PASCAL_CHANNEL_GPFIFO_A}, gk104_fifo_gpfifo_new },
	.cgrp_force = true,
};

int
gp100_fifo_new(struct nvkm_device *device, int index, struct nvkm_fifo **pfifo)
{
	return gk104_fifo_new_(&gp100_fifo, device, index, 4096, pfifo);
}
