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
 * aer_tlp_log_len - Calculate AER Capability TLP Header/Prefix Log length
 * @dev: PCIe device
 * @aercc: AER Capabilities and Control register value
 *
 * Return: TLP Header/Prefix Log length
 */
unsigned int aer_tlp_log_len(struct pci_dev *dev, u32 aercc)
{
	return PCIE_STD_NUM_TLP_HEADERLOG +
	       ((aercc & PCI_ERR_CAP_PREFIX_LOG_PRESENT) ?
		dev->eetlp_prefix_max : 0);
}

#ifdef CONFIG_PCIE_DPC
/**
 * dpc_tlp_log_len - Calculate DPC RP PIO TLP Header/Prefix Log length
 * @dev: PCIe device
 *
 * Return: TLP Header/Prefix Log length
 */
unsigned int dpc_tlp_log_len(struct pci_dev *dev)
{
	/* Remove ImpSpec Log register from the count */
	if (dev->dpc_rp_log_size >= PCIE_STD_NUM_TLP_HEADERLOG + 1)
		return dev->dpc_rp_log_size - 1;

	return dev->dpc_rp_log_size;
}
#endif

/**
 * pcie_read_tlp_log - read TLP Header Log
 * @dev: PCIe device
 * @where: PCI Config offset of TLP Header Log
 * @where2: PCI Config offset of TLP Prefix Log
 * @tlp_len: TLP Log length (Header Log + TLP Prefix Log in DWORDs)
 * @log: TLP Log structure to fill
 *
 * Fill @log from TLP Header Log registers, e.g., AER or DPC.
 *
 * Return: 0 on success and filled TLP Log structure, <0 on error.
 */
int pcie_read_tlp_log(struct pci_dev *dev, int where, int where2,
		      unsigned int tlp_len, struct pcie_tlp_log *log)
{
	unsigned int i;
	int off, ret;
	u32 *to;

	memset(log, 0, sizeof(*log));

	for (i = 0; i < tlp_len; i++) {
		if (i < PCIE_STD_NUM_TLP_HEADERLOG) {
			off = where + i * 4;
			to = &log->dw[i];
		} else {
			off = where2 + (i - PCIE_STD_NUM_TLP_HEADERLOG) * 4;
			to = &log->prefix[i - PCIE_STD_NUM_TLP_HEADERLOG];
		}

		ret = pci_read_config_dword(dev, off, to);
		if (ret)
			return pcibios_err_to_errno(ret);
	}

	return 0;
}
