// SPDX-License-Identifier: GPL-2.0
/*
 * Root Complex Event Collector Support
 *
 * Authors:
 *  Sean V Kelley <sean.v.kelley@intel.com>
 *  Qiuxu Zhuo <qiuxu.zhuo@intel.com>
 *
 * Copyright (C) 2020 Intel Corp.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

#include "../pci.h"

void pci_rcec_init(struct pci_dev *dev)
{
	struct rcec_ea *rcec_ea;
	u32 rcec, hdr, busn;
	u8 ver;

	/* Only for Root Complex Event Collectors */
	if (pci_pcie_type(dev) != PCI_EXP_TYPE_RC_EC)
		return;

	rcec = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_RCEC);
	if (!rcec)
		return;

	rcec_ea = kzalloc(sizeof(*rcec_ea), GFP_KERNEL);
	if (!rcec_ea)
		return;

	pci_read_config_dword(dev, rcec + PCI_RCEC_RCIEP_BITMAP,
			      &rcec_ea->bitmap);

	/* Check whether RCEC BUSN register is present */
	pci_read_config_dword(dev, rcec, &hdr);
	ver = PCI_EXT_CAP_VER(hdr);
	if (ver >= PCI_RCEC_BUSN_REG_VER) {
		pci_read_config_dword(dev, rcec + PCI_RCEC_BUSN, &busn);
		rcec_ea->nextbusn = PCI_RCEC_BUSN_NEXT(busn);
		rcec_ea->lastbusn = PCI_RCEC_BUSN_LAST(busn);
	} else {
		/* Avoid later ver check by setting nextbusn */
		rcec_ea->nextbusn = 0xff;
		rcec_ea->lastbusn = 0x00;
	}

	dev->rcec_ea = rcec_ea;
}

void pci_rcec_exit(struct pci_dev *dev)
{
	kfree(dev->rcec_ea);
	dev->rcec_ea = NULL;
}
