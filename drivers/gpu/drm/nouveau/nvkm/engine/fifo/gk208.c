/*
 * Copyright 2013 Red Hat Inc.
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

#include <nvif/class.h>

void
gk208_fifo_pbdma_init_timeout(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int i;

	for (i = 0; i < fifo->pbdma_nr; i++)
		nvkm_wr32(device, 0x04012c + (i * 0x2000), 0x0000ffff);
}

const struct gk104_fifo_pbdma_func
gk208_fifo_pbdma = {
	.nr = gk104_fifo_pbdma_nr,
	.init = gk104_fifo_pbdma_init,
	.init_timeout = gk208_fifo_pbdma_init_timeout,
};

static const struct gk104_fifo_func
gk208_fifo = {
	.intr.fault = gf100_fifo_intr_fault,
	.pbdma = &gk208_fifo_pbdma,
	.fault.access = gk104_fifo_fault_access,
	.fault.engine = gk104_fifo_fault_engine,
	.fault.reason = gk104_fifo_fault_reason,
	.fault.hubclient = gk104_fifo_fault_hubclient,
	.fault.gpcclient = gk104_fifo_fault_gpcclient,
	.runlist = &gk110_fifo_runlist,
	.chan = {{0,0,KEPLER_CHANNEL_GPFIFO_A}, gk104_fifo_gpfifo_new },
};

int
gk208_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	return gk104_fifo_new_(&gk208_fifo, device, type, inst, 1024, pfifo);
}
