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

static void
ga100_mc_intr_unarm(struct nvkm_mc *mc)
{
	nvkm_wr32(mc->subdev.device, 0xb81610, 0x00000004);
}

static void
ga100_mc_intr_rearm(struct nvkm_mc *mc)
{
	nvkm_wr32(mc->subdev.device, 0xb81608, 0x00000004);
}

static void
ga100_mc_intr_mask(struct nvkm_mc *mc, u32 mask, u32 intr)
{
	nvkm_wr32(mc->subdev.device, 0xb81210,          mask & intr );
	nvkm_wr32(mc->subdev.device, 0xb81410, mask & ~(mask & intr));
}

static u32
ga100_mc_intr_stat(struct nvkm_mc *mc)
{
	u32 intr_top = nvkm_rd32(mc->subdev.device, 0xb81600), intr = 0x00000000;
	if (intr_top & 0x00000004)
		intr = nvkm_mask(mc->subdev.device, 0xb81010, 0x00000000, 0x00000000);
	return intr;
}

static void
ga100_mc_init(struct nvkm_mc *mc)
{
	nv50_mc_init(mc);
	nvkm_wr32(mc->subdev.device, 0xb81210, 0xffffffff);
}

static const struct nvkm_mc_func
ga100_mc = {
	.init = ga100_mc_init,
	.intr = gp100_mc_intr,
	.intr_unarm = ga100_mc_intr_unarm,
	.intr_rearm = ga100_mc_intr_rearm,
	.intr_mask = ga100_mc_intr_mask,
	.intr_stat = ga100_mc_intr_stat,
	.reset = gk104_mc_reset,
};

int
ga100_mc_new(struct nvkm_device *device, int index, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&ga100_mc, device, index, pmc);
}
