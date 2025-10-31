// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024-2025 Intel Corporation. All rights reserved. */

/* PCIe r7.0 section 6.33 Integrity & Data Encryption (IDE) */

#define dev_fmt(fmt) "PCI/IDE: " fmt
#include <linux/bitfield.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

#include "pci.h"

static int __sel_ide_offset(u16 ide_cap, u8 nr_link_ide, u8 stream_index,
			    u8 nr_ide_mem)
{
	u32 offset = ide_cap + PCI_IDE_LINK_STREAM_0 +
		     nr_link_ide * PCI_IDE_LINK_BLOCK_SIZE;

	/*
	 * Assume a constant number of address association resources per stream
	 * index
	 */
	return offset + stream_index * PCI_IDE_SEL_BLOCK_SIZE(nr_ide_mem);
}

void pci_ide_init(struct pci_dev *pdev)
{
	u16 nr_link_ide, nr_ide_mem, nr_streams;
	u16 ide_cap;
	u32 val;

	if (!pci_is_pcie(pdev))
		return;

	ide_cap = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_IDE);
	if (!ide_cap)
		return;

	pci_read_config_dword(pdev, ide_cap + PCI_IDE_CAP, &val);
	if ((val & PCI_IDE_CAP_SELECTIVE) == 0)
		return;

	/*
	 * Require endpoint IDE capability to be paired with IDE Root Port IDE
	 * capability.
	 */
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ENDPOINT) {
		struct pci_dev *rp = pcie_find_root_port(pdev);

		if (!rp->ide_cap)
			return;
	}

	pdev->ide_cfg = FIELD_GET(PCI_IDE_CAP_SEL_CFG, val);
	pdev->ide_tee_limit = FIELD_GET(PCI_IDE_CAP_TEE_LIMITED, val);

	if (val & PCI_IDE_CAP_LINK)
		nr_link_ide = 1 + FIELD_GET(PCI_IDE_CAP_LINK_TC_NUM, val);
	else
		nr_link_ide = 0;

	nr_ide_mem = 0;
	nr_streams = 1 + FIELD_GET(PCI_IDE_CAP_SEL_NUM, val);
	for (u16 i = 0; i < nr_streams; i++) {
		int pos = __sel_ide_offset(ide_cap, nr_link_ide, i, nr_ide_mem);
		int nr_assoc;
		u32 val;

		pci_read_config_dword(pdev, pos + PCI_IDE_SEL_CAP, &val);

		/*
		 * Let's not entertain streams that do not have a constant
		 * number of address association blocks
		 */
		nr_assoc = FIELD_GET(PCI_IDE_SEL_CAP_ASSOC_NUM, val);
		if (i && (nr_assoc != nr_ide_mem)) {
			pci_info(pdev, "Unsupported Selective Stream %d capability, SKIP the rest\n", i);
			nr_streams = i;
			break;
		}

		nr_ide_mem = nr_assoc;
	}

	pdev->ide_cap = ide_cap;
	pdev->nr_link_ide = nr_link_ide;
	pdev->nr_ide_mem = nr_ide_mem;
}
