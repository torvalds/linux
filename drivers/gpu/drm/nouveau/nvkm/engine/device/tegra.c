/*
 * Copyright 2015 Red Hat Inc.
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
#include <core/tegra.h>
#ifdef CONFIG_NOUVEAU_PLATFORM_DRIVER
#include "priv.h"

static struct nvkm_device_tegra *
nvkm_device_tegra(struct nvkm_device *obj)
{
	return container_of(obj, struct nvkm_device_tegra, device);
}

static irqreturn_t
nvkm_device_tegra_intr(int irq, void *arg)
{
	struct nvkm_device_tegra *tdev = arg;
	struct nvkm_mc *mc = tdev->device.mc;
	bool handled = false;
	if (likely(mc)) {
		nvkm_mc_intr_unarm(mc);
		nvkm_mc_intr(mc, &handled);
		nvkm_mc_intr_rearm(mc);
	}
	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static void
nvkm_device_tegra_fini(struct nvkm_device *device, bool suspend)
{
	struct nvkm_device_tegra *tdev = nvkm_device_tegra(device);
	if (tdev->irq) {
		free_irq(tdev->irq, tdev);
		tdev->irq = 0;
	};
}

static int
nvkm_device_tegra_init(struct nvkm_device *device)
{
	struct nvkm_device_tegra *tdev = nvkm_device_tegra(device);
	int irq, ret;

	irq = platform_get_irq_byname(tdev->pdev, "stall");
	if (irq < 0)
		return irq;

	ret = request_irq(irq, nvkm_device_tegra_intr,
			  IRQF_SHARED, "nvkm", tdev);
	if (ret)
		return ret;

	tdev->irq = irq;
	return 0;
}

static const struct nvkm_device_func
nvkm_device_tegra_func = {
	.tegra = nvkm_device_tegra,
	.init = nvkm_device_tegra_init,
	.fini = nvkm_device_tegra_fini,
};

int
nvkm_device_tegra_new(struct platform_device *pdev,
		      const char *cfg, const char *dbg,
		      bool detect, bool mmio, u64 subdev_mask,
		      struct nvkm_device **pdevice)
{
	struct nvkm_device_tegra *tdev;

	if (!(tdev = kzalloc(sizeof(*tdev), GFP_KERNEL)))
		return -ENOMEM;
	*pdevice = &tdev->device;
	tdev->pdev = pdev;
	tdev->irq = -1;

	return nvkm_device_ctor(&nvkm_device_tegra_func, NULL, pdev,
				NVKM_BUS_PLATFORM, pdev->id, NULL,
				cfg, dbg, detect, mmio, subdev_mask,
				&tdev->device);
}
#else
int
nvkm_device_tegra_new(struct platform_device *pdev,
		      const char *cfg, const char *dbg,
		      bool detect, bool mmio, u64 subdev_mask,
		      struct nvkm_device **pdevice)
{
	return -ENOSYS;
}
#endif
