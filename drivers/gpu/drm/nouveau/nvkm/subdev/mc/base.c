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
#include <subdev/top.h>

void
nvkm_mc_unk260(struct nvkm_device *device, u32 data)
{
	struct nvkm_mc *mc = device->mc;
	if (likely(mc) && mc->func->unk260)
		mc->func->unk260(mc, data);
}

void
nvkm_mc_intr_mask(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, bool en)
{
	struct nvkm_mc *mc = device->mc;
	const struct nvkm_mc_map *map;
	if (likely(mc) && mc->func->intr_mask) {
		u32 mask = nvkm_top_intr_mask(device, type, inst);
		for (map = mc->func->intr; !mask && map->stat; map++) {
			if (map->type == type && map->inst == inst)
				mask = map->stat;
		}
		mc->func->intr_mask(mc, mask, en ? mask : 0);
	}
}

void
nvkm_mc_intr_unarm(struct nvkm_device *device)
{
	struct nvkm_mc *mc = device->mc;
	if (likely(mc))
		mc->func->intr_unarm(mc);
}

void
nvkm_mc_intr_rearm(struct nvkm_device *device)
{
	struct nvkm_mc *mc = device->mc;
	if (likely(mc))
		mc->func->intr_rearm(mc);
}

static u32
nvkm_mc_intr_stat(struct nvkm_mc *mc)
{
	u32 intr = mc->func->intr_stat(mc);
	if (WARN_ON_ONCE(intr == 0xffffffff))
		intr = 0; /* likely fallen off the bus */
	return intr;
}

void
nvkm_mc_intr(struct nvkm_device *device, bool *handled)
{
	struct nvkm_mc *mc = device->mc;
	struct nvkm_top *top = device->top;
	struct nvkm_top_device *tdev;
	struct nvkm_subdev *subdev;
	const struct nvkm_mc_map *map;
	u32 stat, intr;

	if (unlikely(!mc))
		return;

	stat = intr = nvkm_mc_intr_stat(mc);

	if (top) {
		list_for_each_entry(tdev, &top->device, head) {
			if (tdev->intr >= 0 && (stat & BIT(tdev->intr))) {
				subdev = nvkm_device_subdev(device, tdev->type, tdev->inst);
				if (subdev) {
					nvkm_subdev_intr(subdev);
					stat &= ~BIT(tdev->intr);
					if (!stat)
						break;
				}
			}
		}
	}

	for (map = mc->func->intr; map->stat; map++) {
		if (intr & map->stat) {
			subdev = nvkm_device_subdev(device, map->type, map->inst);
			if (subdev)
				nvkm_subdev_intr(subdev);
			stat &= ~map->stat;
		}
	}

	if (stat)
		nvkm_error(&mc->subdev, "intr %08x\n", stat);
	*handled = intr != 0;
}

static u32
nvkm_mc_reset_mask(struct nvkm_device *device, bool isauto, enum nvkm_subdev_type type, int inst)
{
	struct nvkm_mc *mc = device->mc;
	const struct nvkm_mc_map *map;
	u64 pmc_enable = 0;
	if (likely(mc)) {
		if (!(pmc_enable = nvkm_top_reset(device, type, inst))) {
			for (map = mc->func->reset; map && map->stat; map++) {
				if (!isauto || !map->noauto) {
					if (map->type == type && map->inst == inst) {
						pmc_enable = map->stat;
						break;
					}
				}
			}
		}
	}
	return pmc_enable;
}

void
nvkm_mc_reset(struct nvkm_device *device, enum nvkm_subdev_type type, int inst)
{
	u64 pmc_enable = nvkm_mc_reset_mask(device, true, type, inst);
	if (pmc_enable) {
		nvkm_mask(device, 0x000200, pmc_enable, 0x00000000);
		nvkm_mask(device, 0x000200, pmc_enable, pmc_enable);
		nvkm_rd32(device, 0x000200);
	}
}

void
nvkm_mc_disable(struct nvkm_device *device, enum nvkm_subdev_type type, int inst)
{
	u64 pmc_enable = nvkm_mc_reset_mask(device, false, type, inst);
	if (pmc_enable)
		nvkm_mask(device, 0x000200, pmc_enable, 0x00000000);
}

void
nvkm_mc_enable(struct nvkm_device *device, enum nvkm_subdev_type type, int inst)
{
	u64 pmc_enable = nvkm_mc_reset_mask(device, false, type, inst);
	if (pmc_enable) {
		nvkm_mask(device, 0x000200, pmc_enable, pmc_enable);
		nvkm_rd32(device, 0x000200);
	}
}

bool
nvkm_mc_enabled(struct nvkm_device *device, enum nvkm_subdev_type type, int inst)
{
	u64 pmc_enable = nvkm_mc_reset_mask(device, false, type, inst);

	return (pmc_enable != 0) &&
	       ((nvkm_rd32(device, 0x000200) & pmc_enable) == pmc_enable);
}


static int
nvkm_mc_fini(struct nvkm_subdev *subdev, bool suspend)
{
	nvkm_mc_intr_unarm(subdev->device);
	return 0;
}

static int
nvkm_mc_init(struct nvkm_subdev *subdev)
{
	struct nvkm_mc *mc = nvkm_mc(subdev);
	if (mc->func->init)
		mc->func->init(mc);
	nvkm_mc_intr_rearm(subdev->device);
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

void
nvkm_mc_ctor(const struct nvkm_mc_func *func, struct nvkm_device *device,
	     enum nvkm_subdev_type type, int inst, struct nvkm_mc *mc)
{
	nvkm_subdev_ctor(&nvkm_mc, device, type, inst, &mc->subdev);
	mc->func = func;
}

int
nvkm_mc_new_(const struct nvkm_mc_func *func, struct nvkm_device *device,
	     enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	struct nvkm_mc *mc;
	if (!(mc = *pmc = kzalloc(sizeof(*mc), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_mc_ctor(func, device, type, inst, *pmc);
	return 0;
}
