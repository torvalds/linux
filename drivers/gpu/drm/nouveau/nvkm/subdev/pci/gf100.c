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

static void
gf100_pci_msi_rearm(struct nvkm_pci *pci)
{
	nvkm_pci_wr08(pci, 0x0704, 0xff);
}

void
gf100_pcie_set_version(struct nvkm_pci *pci, u8 ver)
{
	struct nvkm_device *device = pci->subdev.device;
	nvkm_mask(device, 0x02241c, 0x1, ver > 1 ? 1 : 0);
}

int
gf100_pcie_version(struct nvkm_pci *pci)
{
	struct nvkm_device *device = pci->subdev.device;
	return (nvkm_rd32(device, 0x02241c) & 0x1) + 1;
}

void
gf100_pcie_set_cap_speed(struct nvkm_pci *pci, bool full_speed)
{
	struct nvkm_device *device = pci->subdev.device;
	nvkm_mask(device, 0x02241c, 0x80, full_speed ? 0x80 : 0x0);
}

int
gf100_pcie_cap_speed(struct nvkm_pci *pci)
{
	struct nvkm_device *device = pci->subdev.device;
	u8 punits_pci_cap_speed = nvkm_rd32(device, 0x02241c) & 0x80;
	if (punits_pci_cap_speed == 0x80)
		return 1;
	return 0;
}

int
gf100_pcie_init(struct nvkm_pci *pci)
{
	bool full_speed = g84_pcie_cur_speed(pci) == NVKM_PCIE_SPEED_5_0;
	gf100_pcie_set_cap_speed(pci, full_speed);
	return 0;
}

int
gf100_pcie_set_link(struct nvkm_pci *pci, enum nvkm_pcie_speed speed, u8 width)
{
	gf100_pcie_set_cap_speed(pci, speed == NVKM_PCIE_SPEED_5_0);
	g84_pcie_set_link_speed(pci, speed);
	return 0;
}

static const struct nvkm_pci_func
gf100_pci_func = {
	.cfg = { .addr = 0x088000, .size = 0x1000 },

	.init = g84_pci_init,
	.msi_rearm = gf100_pci_msi_rearm,

	.pcie.init = gf100_pcie_init,
	.pcie.set_link = gf100_pcie_set_link,

	.pcie.max_speed = g84_pcie_max_speed,
	.pcie.cur_speed = g84_pcie_cur_speed,

	.pcie.set_version = gf100_pcie_set_version,
	.pcie.version = gf100_pcie_version,
	.pcie.version_supported = g92_pcie_version_supported,
};

int
gf100_pci_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&gf100_pci_func, device, type, inst, ppci);
}
