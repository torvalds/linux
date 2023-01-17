/*
 * Copyright 2021 Red Hat Inc.
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
#include "priv.h"
#include "chan.h"
#include "runl.h"

#include <nvif/class.h>

static int
g98_fifo_runl_ctor(struct nvkm_fifo *fifo)
{
	struct nvkm_runl *runl;

	runl = nvkm_runl_new(fifo, 0, 0, 0);
	if (IS_ERR(runl))
		return PTR_ERR(runl);

	nvkm_runl_add(runl, 0, fifo->func->engn_sw, NVKM_ENGINE_SW, 0);
	nvkm_runl_add(runl, 0, fifo->func->engn_sw, NVKM_ENGINE_DMAOBJ, 0);
	nvkm_runl_add(runl, 1, fifo->func->engn, NVKM_ENGINE_GR, 0);
	nvkm_runl_add(runl, 2, fifo->func->engn, NVKM_ENGINE_MSPPP, 0);
	nvkm_runl_add(runl, 3, fifo->func->engn, NVKM_ENGINE_CE, 0);
	nvkm_runl_add(runl, 4, fifo->func->engn, NVKM_ENGINE_MSPDEC, 0);
	nvkm_runl_add(runl, 5, fifo->func->engn, NVKM_ENGINE_SEC, 0);
	nvkm_runl_add(runl, 6, fifo->func->engn, NVKM_ENGINE_MSVLD, 0);
	return 0;
}

static const struct nvkm_fifo_func
g98_fifo = {
	.chid_nr = nv50_fifo_chid_nr,
	.chid_ctor = nv50_fifo_chid_ctor,
	.runl_ctor = g98_fifo_runl_ctor,
	.init = nv50_fifo_init,
	.intr = nv04_fifo_intr,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.nonstall = &g84_fifo_nonstall,
	.runl = &nv50_runl,
	.engn = &g84_engn,
	.engn_sw = &nv50_engn_sw,
	.cgrp = {{                          }, &nv04_cgrp },
	.chan = {{ 0, 0, G82_CHANNEL_GPFIFO }, &g84_chan },
};

int
g98_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&g98_fifo, device, type, inst, pfifo);
}
