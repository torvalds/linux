/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "priv.h"

#include <nvhw/drf.h>
#include <nvhw/ref/gh100/dev_xtl_ep_pri.h>

static void
gh100_pci_msi_rearm(struct nvkm_pci *pci)
{
	/* Handled by top-level intr ACK. */
}

static const struct nvkm_pci_func
gh100_pci = {
	.cfg = {
		.addr = DRF_LO(NV_EP_PCFGM),
		.size = DRF_HI(NV_EP_PCFGM) - DRF_LO(NV_EP_PCFGM) + 1,
	},
	.msi_rearm = gh100_pci_msi_rearm,
};

int
gh100_pci_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&gh100_pci, device, type, inst, ppci);
}
