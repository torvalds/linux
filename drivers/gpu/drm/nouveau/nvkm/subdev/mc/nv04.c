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

const struct nvkm_mc_map
nv04_mc_reset[] = {
	{ 0x00001000, NVKM_ENGINE_GR },
	{ 0x00000100, NVKM_ENGINE_FIFO },
	{}
};

static void
nv04_mc_device_disable(struct nvkm_mc *mc, u32 mask)
{
	nvkm_mask(mc->subdev.device, 0x000200, mask, 0x00000000);
}

static void
nv04_mc_device_enable(struct nvkm_mc *mc, u32 mask)
{
	struct nvkm_device *device = mc->subdev.device;

	nvkm_mask(device, 0x000200, mask, mask);
	nvkm_rd32(device, 0x000200);
}

static bool
nv04_mc_device_enabled(struct nvkm_mc *mc, u32 mask)
{
	return (nvkm_rd32(mc->subdev.device, 0x000200) & mask) == mask;
}

const struct nvkm_mc_device_func
nv04_mc_device = {
	.enabled = nv04_mc_device_enabled,
	.enable = nv04_mc_device_enable,
	.disable = nv04_mc_device_disable,
};

static const struct nvkm_intr_data
nv04_mc_intrs[] = {
	{ NVKM_ENGINE_DISP , 0, 0, 0x01010000, true },
	{ NVKM_ENGINE_GR   , 0, 0, 0x00001000, true },
	{ NVKM_ENGINE_FIFO , 0, 0, 0x00000100 },
	{ NVKM_SUBDEV_BUS  , 0, 0, 0x10000000, true },
	{ NVKM_SUBDEV_TIMER, 0, 0, 0x00100000, true },
	{}
};

void
nv04_mc_intr_rearm(struct nvkm_intr *intr)
{
	struct nvkm_mc *mc = container_of(intr, typeof(*mc), intr);
	int leaf;

	for (leaf = 0; leaf < intr->leaves; leaf++)
		nvkm_wr32(mc->subdev.device, 0x000140 + (leaf * 4), 0x00000001);
}

void
nv04_mc_intr_unarm(struct nvkm_intr *intr)
{
	struct nvkm_mc *mc = container_of(intr, typeof(*mc), intr);
	int leaf;

	for (leaf = 0; leaf < intr->leaves; leaf++)
		nvkm_wr32(mc->subdev.device, 0x000140 + (leaf * 4), 0x00000000);

	nvkm_rd32(mc->subdev.device, 0x000140);
}

bool
nv04_mc_intr_pending(struct nvkm_intr *intr)
{
	struct nvkm_mc *mc = container_of(intr, typeof(*mc), intr);
	bool pending = false;
	int leaf;

	for (leaf = 0; leaf < intr->leaves; leaf++) {
		intr->stat[leaf] = nvkm_rd32(mc->subdev.device, 0x000100 + (leaf * 4));
		if (intr->stat[leaf])
			pending = true;
	}

	return pending;
}

const struct nvkm_intr_func
nv04_mc_intr = {
	.pending = nv04_mc_intr_pending,
	.unarm = nv04_mc_intr_unarm,
	.rearm = nv04_mc_intr_rearm,
};

void
nv04_mc_init(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	nvkm_wr32(device, 0x000200, 0xffffffff); /* everything enabled */
	nvkm_wr32(device, 0x001850, 0x00000001); /* disable rom access */
}

static const struct nvkm_mc_func
nv04_mc = {
	.init = nv04_mc_init,
	.intr = &nv04_mc_intr,
	.intrs = nv04_mc_intrs,
	.device = &nv04_mc_device,
	.reset = nv04_mc_reset,
};

int
nv04_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&nv04_mc, device, type, inst, pmc);
}
