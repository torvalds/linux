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

u32
nvkm_pci_rd32(struct nvkm_pci *pci, u16 addr)
{
	return pci->func->rd32(pci, addr);
}

void
nvkm_pci_wr08(struct nvkm_pci *pci, u16 addr, u8 data)
{
	pci->func->wr08(pci, addr, data);
}

void
nvkm_pci_wr32(struct nvkm_pci *pci, u16 addr, u32 data)
{
	pci->func->wr32(pci, addr, data);
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

void
nvkm_pci_msi_rearm(struct nvkm_pci *pci)
{
	pci->func->msi_rearm(pci);
}

static void *
nvkm_pci_dtor(struct nvkm_subdev *subdev)
{
	return nvkm_pci(subdev);
}

static const struct nvkm_subdev_func
nvkm_pci_func = {
	.dtor = nvkm_pci_dtor,
};

int
nvkm_pci_new_(const struct nvkm_pci_func *func, struct nvkm_device *device,
	      int index, struct nvkm_pci **ppci)
{
	struct nvkm_pci *pci;
	if (!(pci = *ppci = kzalloc(sizeof(**ppci), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_pci_func, device, index, 0, &pci->subdev);
	pci->func = func;
	return 0;
}
