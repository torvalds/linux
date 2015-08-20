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

static inline u32
nvkm_mc_intr_mask(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	u32 intr = nvkm_rd32(device, 0x000100);
	if (intr == 0xffffffff) /* likely fallen off the bus */
		intr = 0x00000000;
	return intr;
}

static irqreturn_t
nvkm_mc_intr(int irq, void *arg)
{
	struct nvkm_mc *mc = arg;
	struct nvkm_subdev *subdev = &mc->subdev;
	struct nvkm_device *device = subdev->device;
	const struct nvkm_mc_intr *map = mc->func->intr;
	struct nvkm_subdev *unit;
	u32 intr;

	nvkm_wr32(device, 0x000140, 0x00000000);
	nvkm_rd32(device, 0x000140);
	intr = nvkm_mc_intr_mask(mc);
	if (mc->use_msi)
		mc->func->msi_rearm(mc);

	if (intr) {
		u32 stat = intr = nvkm_mc_intr_mask(mc);
		while (map->stat) {
			if (intr & map->stat) {
				unit = nvkm_device_subdev(device, map->unit);
				if (unit)
					nvkm_subdev_intr(unit);
				stat &= ~map->stat;
			}
			map++;
		}

		if (stat)
			nvkm_error(subdev, "unknown intr %08x\n", stat);
	}

	nvkm_wr32(device, 0x000140, 0x00000001);
	return intr ? IRQ_HANDLED : IRQ_NONE;
}

static int
nvkm_mc_fini(struct nvkm_subdev *subdev, bool suspend)
{
	nvkm_wr32(subdev->device, 0x000140, 0x00000000);
	return 0;
}

static int
nvkm_mc_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_mc *mc = nvkm_mc(subdev);
	return request_irq(mc->irq, nvkm_mc_intr, IRQF_SHARED, "nvkm", mc);
}

static int
nvkm_mc_init(struct nvkm_subdev *subdev)
{
	struct nvkm_mc *mc = nvkm_mc(subdev);
	struct nvkm_device *device = mc->subdev.device;
	if (mc->func->init)
		mc->func->init(mc);
	nvkm_wr32(device, 0x000140, 0x00000001);
	return 0;
}

static void *
nvkm_mc_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_mc *mc = nvkm_mc(subdev);
	struct nvkm_device *device = mc->subdev.device;
	free_irq(mc->irq, mc);
	if (mc->use_msi)
		pci_disable_msi(device->pdev);
	return mc;
}

static const struct nvkm_subdev_func
nvkm_mc = {
	.dtor = nvkm_mc_dtor,
	.oneinit = nvkm_mc_oneinit,
	.init = nvkm_mc_init,
	.fini = nvkm_mc_fini,
};

int
nvkm_mc_new_(const struct nvkm_mc_func *func, struct nvkm_device *device,
	     int index, struct nvkm_mc **pmc)
{
	struct nvkm_mc *mc;
	int ret;

	if (!(mc = *pmc = kzalloc(sizeof(*mc), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_mc, device, index, 0, &mc->subdev);
	mc->func = func;

	if (nv_device_is_pci(device)) {
		switch (device->pdev->device & 0x0ff0) {
		case 0x00f0:
		case 0x02e0:
			/* BR02? NFI how these would be handled yet exactly */
			break;
		default:
			switch (device->chipset) {
			case 0xaa:
				/* reported broken, nv also disable it */
				break;
			default:
				mc->use_msi = true;
				break;
			}
		}

		mc->use_msi = nvkm_boolopt(device->cfgopt, "NvMSI",
					    mc->use_msi);

		if (mc->use_msi && mc->func->msi_rearm) {
			mc->use_msi = pci_enable_msi(device->pdev) == 0;
			if (mc->use_msi) {
				nvkm_debug(&mc->subdev, "MSI enabled\n");
				mc->func->msi_rearm(mc);
			}
		} else {
			mc->use_msi = false;
		}
	}

	ret = nv_device_get_irq(device, true);
	if (ret < 0)
		return ret;
	mc->irq = ret;
	return 0;
}
