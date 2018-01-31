/*
 * Copyright 2013 Red Hat Inc.
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
#include "ram.h"

int
nv1a_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	struct pci_dev *bridge;
	u32 mem, mib;
	int domain = 0;
	struct pci_dev *pdev = NULL;

	if (dev_is_pci(fb->subdev.device->dev))
		pdev = to_pci_dev(fb->subdev.device->dev);

	if (pdev)
		domain = pci_domain_nr(pdev->bus);

	bridge = pci_get_domain_bus_and_slot(domain, 0, PCI_DEVFN(0, 1));
	if (!bridge) {
		nvkm_error(&fb->subdev, "no bridge device\n");
		return -ENODEV;
	}

	if (fb->subdev.device->chipset == 0x1a) {
		pci_read_config_dword(bridge, 0x7c, &mem);
		mib = ((mem >> 6) & 31) + 1;
	} else {
		pci_read_config_dword(bridge, 0x84, &mem);
		mib = ((mem >> 4) & 127) + 1;
	}

	return nvkm_ram_new_(&nv04_ram_func, fb, NVKM_RAM_TYPE_STOLEN,
			     mib * 1024 * 1024, pram);
}
