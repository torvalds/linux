// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe TLP Log handling
 *
 * Copyright (C) 2024 Intel Corporation
 */

#include <linux/aer.h>
#include <linux/pci.h>
#include <linux/string.h>

#include "../pci.h"

/**
 * pcie_read_tlp_log - read TLP Header Log
 * @dev: PCIe device
 * @where: PCI Config offset of TLP Header Log
 * @tlp_log: TLP Log structure to fill
 *
 * Fill @tlp_log from TLP Header Log registers, e.g., AER or DPC.
 *
 * Return: 0 on success and filled TLP Log structure, <0 on error.
 */
int pcie_read_tlp_log(struct pci_dev *dev, int where,
		      struct pcie_tlp_log *tlp_log)
{
	int i, ret;

	memset(tlp_log, 0, sizeof(*tlp_log));

	for (i = 0; i < 4; i++) {
		ret = pci_read_config_dword(dev, where + i * 4,
					    &tlp_log->dw[i]);
		if (ret)
			return pcibios_err_to_errno(ret);
	}

	return 0;
}
