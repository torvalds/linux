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
nv40_pci_rd32(struct nvkm_pci *pci, u16 addr)
{
	struct nvkm_device *device = pci->subdev.device;
	return nvkm_rd32(device, 0x088000 + addr);
}

void
nv40_pci_wr08(struct nvkm_pci *pci, u16 addr, u8 data)
{
	struct nvkm_device *device = pci->subdev.device;
	nvkm_wr08(device, 0x088000 + addr, data);
}

void
nv40_pci_wr32(struct nvkm_pci *pci, u16 addr, u32 data)
{
	struct nvkm_device *device = pci->subdev.device;
	nvkm_wr32(device, 0x088000 + addr, data);
}

void
nv40_pci_msi_rearm(struct nvkm_pci *pci)
{
	nvkm_pci_wr08(pci, 0x0068, 0xff);
}

static const struct nvkm_pci_func
nv40_pci_func = {
	.rd32 = nv40_pci_rd32,
	.wr08 = nv40_pci_wr08,
	.wr32 = nv40_pci_wr32,
	.msi_rearm = nv40_pci_msi_rearm,
};

int
nv40_pci_new(struct nvkm_device *device, int index, struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&nv40_pci_func, device, index, ppci);
}
