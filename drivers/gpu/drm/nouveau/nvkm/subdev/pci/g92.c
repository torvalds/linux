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

int
g92_pcie_version_supported(struct nvkm_pci *pci)
{
	if ((nvkm_pci_rd32(pci, 0x460) & 0x200) == 0x200)
		return 2;
	return 1;
}

static const struct nvkm_pci_func
g92_pci_func = {
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
	.pcie.version_supported = g92_pcie_version_supported,
};

int
g92_pci_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	    struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&g92_pci_func, device, type, inst, ppci);
}
