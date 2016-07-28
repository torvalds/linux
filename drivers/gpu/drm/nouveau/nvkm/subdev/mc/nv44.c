/*
 * Copyright 2012 Red Hat Inc.
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

void
nv44_mc_init(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	u32 tmp = nvkm_rd32(device, 0x10020c);

	nvkm_wr32(device, 0x000200, 0xffffffff); /* everything enabled */

	nvkm_wr32(device, 0x001700, tmp);
	nvkm_wr32(device, 0x001704, 0);
	nvkm_wr32(device, 0x001708, 0);
	nvkm_wr32(device, 0x00170c, tmp);
}

static const struct nvkm_mc_func
nv44_mc = {
	.init = nv44_mc_init,
	.intr = nv17_mc_intr,
	.intr_unarm = nv04_mc_intr_unarm,
	.intr_rearm = nv04_mc_intr_rearm,
	.intr_mask = nv04_mc_intr_mask,
	.reset = nv17_mc_reset,
};

int
nv44_mc_new(struct nvkm_device *device, int index, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&nv44_mc, device, index, pmc);
}
