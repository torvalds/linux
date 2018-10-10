/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RVU_H
#define RVU_H

/* PCI device IDs */
#define	PCI_DEVID_OCTEONTX2_RVU_AF		0xA065

/* PCI BAR nos */
#define	PCI_AF_REG_BAR_NUM			0
#define	PCI_PF_REG_BAR_NUM			2
#define	PCI_MBOX_BAR_NUM			4

#define NAME_SIZE				32

struct rvu {
	void __iomem		*afreg_base;
	void __iomem		*pfreg_base;
	struct pci_dev		*pdev;
	struct device		*dev;
};

#endif /* RVU_H */
