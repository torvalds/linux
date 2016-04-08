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

#include <core/option.h>

void
nvkm_mc_unk260(struct nvkm_mc *mc, u32 data)
{
	if (mc->func->unk260)
		mc->func->unk260(mc, data);
}

void
nvkm_mc_intr_unarm(struct nvkm_mc *mc)
{
	return mc->func->intr_unarm(mc);
}

void
nvkm_mc_intr_rearm(struct nvkm_mc *mc)
{
	return mc->func->intr_rearm(mc);
}

static u32
nvkm_mc_intr_mask(struct nvkm_mc *mc)
{
	u32 intr = mc->func->intr_mask(mc);
	if (WARN_ON_ONCE(intr == 0xffffffff))
		intr = 0; /* likely fallen off the bus */
	return intr;
}

void
nvkm_mc_intr(struct nvkm_mc *mc, bool *handled)
{
	struct nvkm_device *device = mc->subdev.device;
	struct nvkm_subdev *subdev;
	const struct nvkm_mc_map *map = mc->func->intr;
	u32 stat, intr;

	stat = intr = nvkm_mc_intr_mask(mc);
	while (map->stat) {
		if (intr & map->stat) {
			subdev = nvkm_device_subdev(device, map->unit);
			if (subdev)
				nvkm_subdev_intr(subdev);
			stat &= ~map->stat;
		}
		map++;
	}

	if (stat)
		nvkm_error(&mc->subdev, "intr %08x\n", stat);
	*handled = intr != 0;
}

static void
nvkm_mc_reset_(struct nvkm_mc *mc, enum nvkm_devidx devidx)
{
	struct nvkm_device *device = mc->subdev.device;
	struct nvkm_subdev *subdev = nvkm_device_subdev(device, devidx);
	u64 pmc_enable = subdev->pmc_enable;
	if (pmc_enable) {
		nvkm_mask(device, 0x000200, pmc_enable, 0x00000000);
		nvkm_mask(device, 0x000200, pmc_enable, pmc_enable);
		nvkm_rd32(device, 0x000200);
	}
}

void
nvkm_mc_reset(struct nvkm_mc *mc, enum nvkm_devidx devidx)
{
	if (likely(mc))
		nvkm_mc_reset_(mc, devidx);
}

static int
nvkm_mc_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_mc *mc = nvkm_mc(subdev);
	nvkm_mc_intr_unarm(mc);
	return 0;
}

static int
nvkm_mc_init(struct nvkm_subdev *subdev)
{
	struct nvkm_mc *mc = nvkm_mc(subdev);
	if (mc->func->init)
		mc->func->init(mc);
	nvkm_mc_intr_rearm(mc);
	return 0;
}

static void *
nvkm_mc_dtor(struct nvkm_subdev *subdev)
{
	return nvkm_mc(subdev);
}

static const struct nvkm_subdev_func
nvkm_mc = {
	.dtor = nvkm_mc_dtor,
	.init = nvkm_mc_init,
	.fini = nvkm_mc_fini,
};

int
nvkm_mc_new_(const struct nvkm_mc_func *func, struct nvkm_device *device,
	     int index, struct nvkm_mc **pmc)
{
	struct nvkm_mc *mc;

	if (!(mc = *pmc = kzalloc(sizeof(*mc), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_mc, device, index, 0, &mc->subdev);
	mc->func = func;
	return 0;
}
