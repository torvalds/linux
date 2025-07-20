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
#include "priv.h"
#include "agp.h"

#include <core/option.h>
#include <core/pci.h>

void
nvkm_pci_msi_rearm(struct nvkm_device *device)
{
	struct nvkm_pci *pci = device->pci;

	if (pci && pci->msi)
		pci->func->msi_rearm(pci);
}

u32
nvkm_pci_rd32(struct nvkm_pci *pci, u16 addr)
{
	return nvkm_rd32(pci->subdev.device, pci->func->cfg.addr + addr);
}

void
nvkm_pci_wr08(struct nvkm_pci *pci, u16 addr, u8 data)
{
	nvkm_wr08(pci->subdev.device, pci->func->cfg.addr + addr, data);
}

void
nvkm_pci_wr32(struct nvkm_pci *pci, u16 addr, u32 data)
{
	nvkm_wr32(pci->subdev.device, pci->func->cfg.addr + addr, data);
}

u32
nvkm_pci_mask(struct nvkm_pci *pci, u16 addr, u32 mask, u32 value)
{
	u32 data = nvkm_pci_rd32(pci, addr);
	nvkm_pci_wr32(pci, addr, (data & ~mask) | value);
	return data;
}

void
nvkm_pci_rom_shadow(struct nvkm_pci *pci, bool shadow)
{
	u32 data = nvkm_pci_rd32(pci, 0x0050);
	if (shadow)
		data |=  0x00000001;
	else
		data &= ~0x00000001;
	nvkm_pci_wr32(pci, 0x0050, data);
}

static int
nvkm_pci_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_pci *pci = nvkm_pci(subdev);

	if (pci->agp.bridge)
		nvkm_agp_fini(pci);

	return 0;
}

static int
nvkm_pci_preinit(struct nvkm_subdev *subdev)
{
	struct nvkm_pci *pci = nvkm_pci(subdev);
	if (pci->agp.bridge)
		nvkm_agp_preinit(pci);
	return 0;
}

static int
nvkm_pci_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_pci *pci = nvkm_pci(subdev);
	int ret;

	if (pci_is_pcie(pci->pdev)) {
		ret = nvkm_pcie_oneinit(pci);
		if (ret)
			return ret;
	}

	return 0;
}

static int
nvkm_pci_init(struct nvkm_subdev *subdev)
{
	struct nvkm_pci *pci = nvkm_pci(subdev);
	int ret;

	if (pci->agp.bridge) {
		ret = nvkm_agp_init(pci);
		if (ret)
			return ret;
	} else if (pci_is_pcie(pci->pdev)) {
		nvkm_pcie_init(pci);
	}

	if (pci->func->init)
		pci->func->init(pci);

	/* Ensure MSI interrupts are armed, for the case where there are
	 * already interrupts pending (for whatever reason) at load time.
	 */
	if (pci->msi)
		pci->func->msi_rearm(pci);

	return 0;
}

static void *
nvkm_pci_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_pci *pci = nvkm_pci(subdev);

	nvkm_agp_dtor(pci);

	if (pci->msi)
		pci_disable_msi(pci->pdev);

	return nvkm_pci(subdev);
}

static const struct nvkm_subdev_func
nvkm_pci_func = {
	.dtor = nvkm_pci_dtor,
	.oneinit = nvkm_pci_oneinit,
	.preinit = nvkm_pci_preinit,
	.init = nvkm_pci_init,
	.fini = nvkm_pci_fini,
};

int
nvkm_pci_new_(const struct nvkm_pci_func *func, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_pci **ppci)
{
	struct nvkm_pci *pci;

	if (!(pci = *ppci = kzalloc(sizeof(**ppci), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_pci_func, device, type, inst, &pci->subdev);
	pci->func = func;
	pci->pdev = device->func->pci(device)->pdev;
	pci->pcie.speed = -1;
	pci->pcie.width = -1;

	if (device->type == NVKM_DEVICE_AGP)
		nvkm_agp_ctor(pci);

	switch (pci->pdev->device & 0x0ff0) {
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
			pci->msi = true;
			break;
		}
	}

#ifdef __BIG_ENDIAN
	pci->msi = false;
#endif

	pci->msi = nvkm_boolopt(device->cfgopt, "NvMSI", pci->msi);
	if (pci->msi && func->msi_rearm) {
		pci->msi = pci_enable_msi(pci->pdev) == 0;
		if (pci->msi)
			nvkm_debug(&pci->subdev, "MSI enabled\n");
	} else {
		pci->msi = false;
	}

	return 0;
}
