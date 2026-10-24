// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Marek Czernohous
 *
 * MCP79/MCP7A (NVAC): like g94, but MSI re-arm goes through real PCI
 * config space.  The MMIO-mirror re-arm is unreliable on this IGP and a
 * missed re-arm kills the interrupt line (see the nv46 comment; g92
 * already re-arms through config space for the same reason).
 */
#include "priv.h"

static const struct nvkm_pci_func
mcp79_pci_func = {
	.cfg = { .addr = 0x088000, .size = 0x1000 },

	.init = g84_pci_init,
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
mcp79_pci_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&mcp79_pci_func, device, type, inst, ppci);
}
