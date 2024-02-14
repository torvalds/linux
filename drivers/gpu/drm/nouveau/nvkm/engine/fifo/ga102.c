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

#include <subdev/gsp.h>

#include <nvif/class.h>

static const struct nvkm_fifo_func
ga102_fifo = {
	.runl_ctor = ga100_fifo_runl_ctor,
	.mmu_fault = &tu102_fifo_mmu_fault,
	.nonstall_ctor = ga100_fifo_nonstall_ctor,
	.nonstall = &ga100_fifo_nonstall,
	.runl = &ga100_runl,
	.runq = &ga100_runq,
	.engn = &ga100_engn,
	.engn_ce = &ga100_engn_ce,
	.cgrp = {{ 0, 0, KEPLER_CHANNEL_GROUP_A  }, &ga100_cgrp, .force = true },
	.chan = {{ 0, 0, AMPERE_CHANNEL_GPFIFO_A }, &ga100_chan },
};

int
ga102_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_fifo_new(&ga102_fifo, device, type, inst, pfifo);

	return nvkm_fifo_new_(&ga102_fifo, device, type, inst, pfifo);
}
