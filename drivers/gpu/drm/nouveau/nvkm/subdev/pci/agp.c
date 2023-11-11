/*
 * Copyright 2015 Nouveau Project
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
#include "agp.h"
#ifdef __NVKM_PCI_AGP_H__
#include <core/option.h>

struct nvkm_device_agp_quirk {
	u16 hostbridge_vendor;
	u16 hostbridge_device;
	u16 chip_vendor;
	u16 chip_device;
	int mode;
};

static const struct nvkm_device_agp_quirk
nvkm_device_agp_quirks[] = {
	/* VIA Apollo PRO133x / GeForce FX 5600 Ultra - fdo#20341 */
	{ PCI_VENDOR_ID_VIA, 0x0691, PCI_VENDOR_ID_NVIDIA, 0x0311, 2 },
	/* SiS 761 does not support AGP cards, use PCI mode */
	{ PCI_VENDOR_ID_SI, 0x0761, PCI_ANY_ID, PCI_ANY_ID, 0 },
	{},
};

void
nvkm_agp_fini(struct nvkm_pci *pci)
{
	if (pci->agp.acquired) {
		agp_backend_release(pci->agp.bridge);
		pci->agp.acquired = false;
	}
}

/* Ensure AGP controller is in a consistent state in case we need to
 * execute the VBIOS DEVINIT scripts.
 */
void
nvkm_agp_preinit(struct nvkm_pci *pci)
{
	struct nvkm_device *device = pci->subdev.device;
	u32 mode = nvkm_pci_rd32(pci, 0x004c);
	u32 save[2];

	/* First of all, disable fast writes, otherwise if it's already
	 * enabled in the AGP bridge and we disable the card's AGP
	 * controller we might be locking ourselves out of it.
	 */
	if ((mode | pci->agp.mode) & PCI_AGP_COMMAND_FW) {
		mode = pci->agp.mode & ~PCI_AGP_COMMAND_FW;
		agp_enable(pci->agp.bridge, mode);
	}

	/* clear busmaster bit, and disable AGP */
	save[0] = nvkm_pci_rd32(pci, 0x0004);
	nvkm_pci_wr32(pci, 0x0004, save[0] & ~0x00000004);
	nvkm_pci_wr32(pci, 0x004c, 0x00000000);

	/* reset PGRAPH, PFIFO and PTIMER */
	save[1] = nvkm_mask(device, 0x000200, 0x00011100, 0x00000000);
	nvkm_mask(device, 0x000200, 0x00011100, save[1]);

	/* and restore busmaster bit (gives effect of resetting AGP) */
	nvkm_pci_wr32(pci, 0x0004, save[0]);
}

int
nvkm_agp_init(struct nvkm_pci *pci)
{
	if (!agp_backend_acquire(pci->pdev)) {
		nvkm_error(&pci->subdev, "failed to acquire agp\n");
		return -ENODEV;
	}

	agp_enable(pci->agp.bridge, pci->agp.mode);
	pci->agp.acquired = true;
	return 0;
}

void
nvkm_agp_dtor(struct nvkm_pci *pci)
{
	arch_phys_wc_del(pci->agp.mtrr);
}

void
nvkm_agp_ctor(struct nvkm_pci *pci)
{
	const struct nvkm_device_agp_quirk *quirk = nvkm_device_agp_quirks;
	struct nvkm_subdev *subdev = &pci->subdev;
	struct nvkm_device *device = subdev->device;
	struct agp_kern_info info;
	int mode = -1;

#ifdef __powerpc__
	/* Disable AGP by default on all PowerPC machines for now -- At
	 * least some UniNorth-2 AGP bridges are known to be broken:
	 * DMA from the host to the card works just fine, but writeback
	 * from the card to the host goes straight to memory
	 * untranslated bypassing that GATT somehow, making them quite
	 * painful to deal with...
	 */
	mode = 0;
#endif
	mode = nvkm_longopt(device->cfgopt, "NvAGP", mode);

	/* acquire bridge temporarily, so that we can copy its info */
	if (!(pci->agp.bridge = agp_backend_acquire(pci->pdev))) {
		nvkm_warn(subdev, "failed to acquire agp\n");
		return;
	}
	agp_copy_info(pci->agp.bridge, &info);
	agp_backend_release(pci->agp.bridge);

	pci->agp.mode = info.mode;
	pci->agp.base = info.aper_base;
	pci->agp.size = info.aper_size * 1024 * 1024;
	pci->agp.cma  = info.cant_use_aperture;
	pci->agp.mtrr = -1;

	/* determine if bridge + chipset combination needs a workaround */
	while (quirk->hostbridge_vendor) {
		if (info.device->vendor == quirk->hostbridge_vendor &&
		    info.device->device == quirk->hostbridge_device &&
		    (quirk->chip_vendor == (u16)PCI_ANY_ID ||
		    pci->pdev->vendor == quirk->chip_vendor) &&
		    (quirk->chip_device == (u16)PCI_ANY_ID ||
		    pci->pdev->device == quirk->chip_device)) {
			nvkm_info(subdev, "forcing default agp mode to %dX, "
					  "use NvAGP=<mode> to override\n",
				  quirk->mode);
			mode = quirk->mode;
			break;
		}
		quirk++;
	}

	/* apply quirk / user-specified mode */
	if (mode >= 1) {
		if (pci->agp.mode & 0x00000008)
			mode /= 4; /* AGPv3 */
		pci->agp.mode &= ~0x00000007;
		pci->agp.mode |= (mode & 0x7);
	} else
	if (mode == 0) {
		pci->agp.bridge = NULL;
		return;
	}

	/* fast writes appear to be broken on nv18, they make the card
	 * lock up randomly.
	 */
	if (device->chipset == 0x18)
		pci->agp.mode &= ~PCI_AGP_COMMAND_FW;

	pci->agp.mtrr = arch_phys_wc_add(pci->agp.base, pci->agp.size);
}
#endif
