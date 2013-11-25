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

#include <subdev/mc.h>
#include <core/option.h>

static inline u32
nouveau_mc_intr_mask(struct nouveau_mc *pmc)
{
	u32 intr = nv_rd32(pmc, 0x000100);
	if (intr == 0xffffffff) /* likely fallen off the bus */
		intr = 0x00000000;
	return intr;
}

static irqreturn_t
nouveau_mc_intr(int irq, void *arg)
{
	struct nouveau_mc *pmc = arg;
	const struct nouveau_mc_oclass *oclass = (void *)nv_object(pmc)->oclass;
	const struct nouveau_mc_intr *map = oclass->intr;
	struct nouveau_subdev *unit;
	u32 intr;

	nv_wr32(pmc, 0x000140, 0x00000000);
	nv_rd32(pmc, 0x000140);
	intr = nouveau_mc_intr_mask(pmc);
	if (pmc->use_msi)
		oclass->msi_rearm(pmc);

	if (intr) {
		u32 stat = intr = nouveau_mc_intr_mask(pmc);
		while (map->stat) {
			if (intr & map->stat) {
				unit = nouveau_subdev(pmc, map->unit);
				if (unit && unit->intr)
					unit->intr(unit);
				stat &= ~map->stat;
			}
			map++;
		}

		if (stat)
			nv_error(pmc, "unknown intr 0x%08x\n", stat);
	}

	nv_wr32(pmc, 0x000140, 0x00000001);
	return intr ? IRQ_HANDLED : IRQ_NONE;
}

int
_nouveau_mc_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_mc *pmc = (void *)object;
	nv_wr32(pmc, 0x000140, 0x00000000);
	return nouveau_subdev_fini(&pmc->base, suspend);
}

int
_nouveau_mc_init(struct nouveau_object *object)
{
	struct nouveau_mc *pmc = (void *)object;
	int ret = nouveau_subdev_init(&pmc->base);
	if (ret)
		return ret;
	nv_wr32(pmc, 0x000140, 0x00000001);
	return 0;
}

void
_nouveau_mc_dtor(struct nouveau_object *object)
{
	struct nouveau_device *device = nv_device(object);
	struct nouveau_mc *pmc = (void *)object;
	free_irq(device->pdev->irq, pmc);
	if (pmc->use_msi)
		pci_disable_msi(device->pdev);
	nouveau_subdev_destroy(&pmc->base);
}

int
nouveau_mc_create_(struct nouveau_object *parent, struct nouveau_object *engine,
		   struct nouveau_oclass *bclass, int length, void **pobject)
{
	const struct nouveau_mc_oclass *oclass = (void *)bclass;
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_mc *pmc;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, bclass, 0, "PMC",
				     "master", length, pobject);
	pmc = *pobject;
	if (ret)
		return ret;

	switch (device->pdev->device & 0x0ff0) {
	case 0x00f0:
	case 0x02e0:
		/* BR02? NFI how these would be handled yet exactly */
		break;
	default:
		switch (device->chipset) {
		case 0xaa: break; /* reported broken, nv also disable it */
		default:
			pmc->use_msi = true;
			break;
		}
	}

	pmc->use_msi = nouveau_boolopt(device->cfgopt, "NvMSI", pmc->use_msi);
	if (pmc->use_msi && oclass->msi_rearm) {
		pmc->use_msi = pci_enable_msi(device->pdev) == 0;
		if (pmc->use_msi) {
			nv_info(pmc, "MSI interrupts enabled\n");
			oclass->msi_rearm(pmc);
		}
	} else {
		pmc->use_msi = false;
	}

	ret = request_irq(device->pdev->irq, nouveau_mc_intr,
			  IRQF_SHARED, "nouveau", pmc);
	if (ret < 0)
		return ret;

	return 0;
}
