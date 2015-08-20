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
#include <core/pci.h>
#include "priv.h"

static struct nvkm_device_pci *
nvkm_device_pci(struct nvkm_device *device)
{
	return container_of(device, struct nvkm_device_pci, device);
}

static resource_size_t
nvkm_device_pci_resource_addr(struct nvkm_device *device, unsigned bar)
{
	struct nvkm_device_pci *pdev = nvkm_device_pci(device);
	return pci_resource_start(pdev->pdev, bar);
}

static resource_size_t
nvkm_device_pci_resource_size(struct nvkm_device *device, unsigned bar)
{
	struct nvkm_device_pci *pdev = nvkm_device_pci(device);
	return pci_resource_len(pdev->pdev, bar);
}

static void
nvkm_device_pci_fini(struct nvkm_device *device, bool suspend)
{
	struct nvkm_device_pci *pdev = nvkm_device_pci(device);
	if (suspend) {
		pci_disable_device(pdev->pdev);
		pdev->suspend = true;
	}
}

static int
nvkm_device_pci_preinit(struct nvkm_device *device)
{
	struct nvkm_device_pci *pdev = nvkm_device_pci(device);
	if (pdev->suspend) {
		int ret = pci_enable_device(pdev->pdev);
		if (ret)
			return ret;
		pci_set_master(pdev->pdev);
		pdev->suspend = false;
	}
	return 0;
}

static void *
nvkm_device_pci_dtor(struct nvkm_device *device)
{
	struct nvkm_device_pci *pdev = nvkm_device_pci(device);
	pci_disable_device(pdev->pdev);
	return pdev;
}

static const struct nvkm_device_func
nvkm_device_pci_func = {
	.pci = nvkm_device_pci,
	.dtor = nvkm_device_pci_dtor,
	.preinit = nvkm_device_pci_preinit,
	.fini = nvkm_device_pci_fini,
	.resource_addr = nvkm_device_pci_resource_addr,
	.resource_size = nvkm_device_pci_resource_size,
};

int
nvkm_device_pci_new(struct pci_dev *pci_dev, const char *cfg, const char *dbg,
		    bool detect, bool mmio, u64 subdev_mask,
		    struct nvkm_device **pdevice)
{
	struct nvkm_device_pci *pdev;
	int ret;

	ret = pci_enable_device(pci_dev);
	if (ret)
		return ret;

	if (!(pdev = kzalloc(sizeof(*pdev), GFP_KERNEL))) {
		pci_disable_device(pci_dev);
		return -ENOMEM;
	}
	*pdevice = &pdev->device;
	pdev->pdev = pci_dev;

	return nvkm_device_ctor(&nvkm_device_pci_func, NULL,
				pci_dev, NVKM_BUS_PCI,
				(u64)pci_domain_nr(pci_dev->bus) << 32 |
				     pci_dev->bus->number << 16 |
				     PCI_SLOT(pci_dev->devfn) << 8 |
				     PCI_FUNC(pci_dev->devfn), NULL,
				cfg, dbg, detect, mmio, subdev_mask,
				&pdev->device);
}
