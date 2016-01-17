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

#include <core/pci.h>

static int
g84_pcie_version_supported(struct nvkm_pci *pci)
{
	/* g84 and g86 report wrong information about what they support */
	return 1;
}

int
g84_pcie_version(struct nvkm_pci *pci)
{
	struct nvkm_device *device = pci->subdev.device;
	return (nvkm_rd32(device, 0x00154c) & 0x1) + 1;
}

void
g84_pcie_set_version(struct nvkm_pci *pci, u8 ver)
{
	struct nvkm_device *device = pci->subdev.device;
	nvkm_mask(device, 0x00154c, 0x1, (ver >= 2 ? 0x1 : 0x0));
}

static void
g84_pcie_set_cap_speed(struct nvkm_pci *pci, bool full_speed)
{
	struct nvkm_device *device = pci->subdev.device;
	nvkm_mask(device, 0x00154c, 0x80, full_speed ? 0x80 : 0x0);
}

enum nvkm_pcie_speed
g84_pcie_cur_speed(struct nvkm_pci *pci)
{
	u32 reg_v = nvkm_pci_rd32(pci, 0x88) & 0x30000;
	switch (reg_v) {
	case 0x30000:
		return NVKM_PCIE_SPEED_8_0;
	case 0x20000:
		return NVKM_PCIE_SPEED_5_0;
	case 0x10000:
	default:
		return NVKM_PCIE_SPEED_2_5;
	}
}

enum nvkm_pcie_speed
g84_pcie_max_speed(struct nvkm_pci *pci)
{
	u32 reg_v = nvkm_pci_rd32(pci, 0x460) & 0x3300;
	if (reg_v == 0x2200)
		return NVKM_PCIE_SPEED_5_0;
	return NVKM_PCIE_SPEED_2_5;
}

void
g84_pcie_set_link_speed(struct nvkm_pci *pci, enum nvkm_pcie_speed speed)
{
	u32 mask_value;

	if (speed == NVKM_PCIE_SPEED_5_0)
		mask_value = 0x20;
	else
		mask_value = 0x10;

	nvkm_pci_mask(pci, 0x460, 0x30, mask_value);
	nvkm_pci_mask(pci, 0x460, 0x1, 0x1);
}

int
g84_pcie_set_link(struct nvkm_pci *pci, enum nvkm_pcie_speed speed, u8 width)
{
	g84_pcie_set_cap_speed(pci, speed == NVKM_PCIE_SPEED_5_0);
	g84_pcie_set_link_speed(pci, speed);
	return 0;
}

void
g84_pci_init(struct nvkm_pci *pci)
{
	/* The following only concerns PCIe cards. */
	if (!pci_is_pcie(pci->pdev))
		return;

	/* Tag field is 8-bit long, regardless of EXT_TAG.
	 * However, if EXT_TAG is disabled, only the lower 5 bits of the tag
	 * field should be used, limiting the number of request to 32.
	 *
	 * Apparently, 0x041c stores some limit on the number of requests
	 * possible, so if EXT_TAG is disabled, limit that requests number to
	 * 32
	 *
	 * Fixes fdo#86537
	 */
	if (nvkm_pci_rd32(pci, 0x007c) & 0x00000020)
		nvkm_pci_mask(pci, 0x0080, 0x00000100, 0x00000100);
	else
		nvkm_pci_mask(pci, 0x041c, 0x00000060, 0x00000000);
}

int
g84_pcie_init(struct nvkm_pci *pci)
{
	bool full_speed = g84_pcie_cur_speed(pci) == NVKM_PCIE_SPEED_5_0;
	g84_pcie_set_cap_speed(pci, full_speed);
	return 0;
}

static const struct nvkm_pci_func
g84_pci_func = {
	.init = g84_pci_init,
	.rd32 = nv40_pci_rd32,
	.wr08 = nv40_pci_wr08,
	.wr32 = nv40_pci_wr32,
	.msi_rearm = nv46_pci_msi_rearm,

	.pcie.init = g84_pcie_init,
	.pcie.set_link = g84_pcie_set_link,

	.pcie.max_speed = g84_pcie_max_speed,
	.pcie.cur_speed = g84_pcie_cur_speed,

	.pcie.set_version = g84_pcie_set_version,
	.pcie.version = g84_pcie_version,
	.pcie.version_supported = g84_pcie_version_supported,
};

int
g84_pci_new(struct nvkm_device *device, int index, struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&g84_pci_func, device, index, ppci);
}
