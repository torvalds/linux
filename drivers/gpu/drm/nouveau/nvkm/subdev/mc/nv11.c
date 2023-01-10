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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "priv.h"

static const struct nvkm_intr_data
nv11_mc_intrs[] = {
	{ NVKM_ENGINE_DISP , 0, 0, 0x03010000, true },
	{ NVKM_ENGINE_GR   , 0, 0, 0x00001000, true },
	{ NVKM_ENGINE_FIFO , 0, 0, 0x00000100 },
	{ NVKM_SUBDEV_BUS  , 0, 0, 0x10000000, true },
	{ NVKM_SUBDEV_TIMER, 0, 0, 0x00100000, true },
	{}
};

static const struct nvkm_mc_func
nv11_mc = {
	.init = nv04_mc_init,
	.intr = &nv04_mc_intr,
	.intrs = nv11_mc_intrs,
	.device = &nv04_mc_device,
	.reset = nv04_mc_reset,
};

int
nv11_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&nv11_mc, device, type, inst, pmc);
}
