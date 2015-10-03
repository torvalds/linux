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

static const struct nvkm_pci_func
g84_pci_func = {
	.init = g84_pci_init,
	.rd32 = nv40_pci_rd32,
	.wr08 = nv40_pci_wr08,
	.wr32 = nv40_pci_wr32,
	.msi_rearm = nv46_pci_msi_rearm,
};

int
g84_pci_new(struct nvkm_device *device, int index, struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&g84_pci_func, device, index, ppci);
}
