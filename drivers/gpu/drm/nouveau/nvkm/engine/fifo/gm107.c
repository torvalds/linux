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

const struct nvkm_enum
gm107_fifo_fault_engine[] = {
	{ 0x01, "DISPLAY" },
	{ 0x02, "CAPTURE" },
	{ 0x03, "IFB", NULL, NVKM_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVKM_SUBDEV_BAR },
	{ 0x05, "BAR2", NULL, NVKM_SUBDEV_INSTMEM },
	{ 0x06, "SCHED" },
	{ 0x07, "HOST0", NULL, NVKM_ENGINE_FIFO },
	{ 0x08, "HOST1", NULL, NVKM_ENGINE_FIFO },
	{ 0x09, "HOST2", NULL, NVKM_ENGINE_FIFO },
	{ 0x0a, "HOST3", NULL, NVKM_ENGINE_FIFO },
	{ 0x0b, "HOST4", NULL, NVKM_ENGINE_FIFO },
	{ 0x0c, "HOST5", NULL, NVKM_ENGINE_FIFO },
	{ 0x0d, "HOST6", NULL, NVKM_ENGINE_FIFO },
	{ 0x0e, "HOST7", NULL, NVKM_ENGINE_FIFO },
	{ 0x0f, "HOSTSR" },
	{ 0x13, "PERF" },
	{ 0x17, "PMU" },
	{ 0x18, "PTP" },
	{}
};

static const struct gk104_fifo_func
gm107_fifo = {
	.fault.engine = gm107_fifo_fault_engine,
	.fault.reason = gk104_fifo_fault_reason,
	.fault.hubclient = gk104_fifo_fault_hubclient,
	.fault.gpcclient = gk104_fifo_fault_gpcclient,
	.chan = {
		&gk110_fifo_gpfifo_oclass,
		NULL
	},
};

int
gm107_fifo_new(struct nvkm_device *device, int index, struct nvkm_fifo **pfifo)
{
	return gk104_fifo_new_(&gm107_fifo, device, index, 2048, pfifo);
}
