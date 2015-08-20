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

static inline void
nvkm_mc_unk260(struct nvkm_mc *mc, u32 data)
{
	const struct nvkm_mc_oclass *impl = (void *)nv_oclass(mc);
	if (impl->unk260)
		impl->unk260(mc, data);
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
	const struct nvkm_mc_oclass *oclass = (void *)nv_object(mc)->oclass;
	const struct nvkm_mc_intr *map = oclass->intr;
	struct nvkm_subdev *unit;
	u32 intr;

	nvkm_wr32(device, 0x000140, 0x00000000);
	nvkm_rd32(device, 0x000140);
	intr = nvkm_mc_intr_mask(mc);
	if (mc->use_msi)
		oclass->msi_rearm(mc);

	if (intr) {
		u32 stat = intr = nvkm_mc_intr_mask(mc);
		while (map->stat) {
			if (intr & map->stat) {
				unit = nvkm_subdev(mc, map->unit);
				if (unit && unit->intr)
					unit->intr(unit);
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

int
_nvkm_mc_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_mc *mc = (void *)object;
	struct nvkm_device *device = mc->subdev.device;
	nvkm_wr32(device, 0x000140, 0x00000000);
	return nvkm_subdev_fini_old(&mc->subdev, suspend);
}

int
_nvkm_mc_init(struct nvkm_object *object)
{
	struct nvkm_mc *mc = (void *)object;
	struct nvkm_device *device = mc->subdev.device;
	int ret = nvkm_subdev_init_old(&mc->subdev);
	if (ret)
		return ret;
	nvkm_wr32(device, 0x000140, 0x00000001);
	return 0;
}

void
_nvkm_mc_dtor(struct nvkm_object *object)
{
	struct nvkm_mc *mc = (void *)object;
	struct nvkm_device *device = mc->subdev.device;
	free_irq(mc->irq, mc);
	if (mc->use_msi)
		pci_disable_msi(device->pdev);
	nvkm_subdev_destroy(&mc->subdev);
}

int
nvkm_mc_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *bclass, int length, void **pobject)
{
	const struct nvkm_mc_oclass *oclass = (void *)bclass;
	struct nvkm_device *device = (void *)parent;
	struct nvkm_mc *mc;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, bclass, 0, "PMC",
				  "master", length, pobject);
	mc = *pobject;
	if (ret)
		return ret;

	mc->unk260 = nvkm_mc_unk260;

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

		if (mc->use_msi && oclass->msi_rearm) {
			mc->use_msi = pci_enable_msi(device->pdev) == 0;
			if (mc->use_msi) {
				nvkm_debug(&mc->subdev, "MSI enabled\n");
				oclass->msi_rearm(mc);
			}
		} else {
			mc->use_msi = false;
		}
	}

	ret = nv_device_get_irq(device, true);
	if (ret < 0)
		return ret;
	mc->irq = ret;

	ret = request_irq(mc->irq, nvkm_mc_intr, IRQF_SHARED, "nvkm", mc);
	if (ret < 0)
		return ret;

	return 0;
}
