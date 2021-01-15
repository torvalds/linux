/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPTPF_H
#define __OTX2_CPTPF_H

struct otx2_cptpf_dev {
	void __iomem *reg_base;		/* CPT PF registers start address */
	struct pci_dev *pdev;		/* PCI device handle */
};

#endif /* __OTX2_CPTPF_H */
