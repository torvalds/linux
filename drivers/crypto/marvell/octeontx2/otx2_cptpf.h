/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPTPF_H
#define __OTX2_CPTPF_H

#include "otx2_cpt_common.h"

struct otx2_cptpf_dev {
	void __iomem *reg_base;		/* CPT PF registers start address */
	void __iomem *afpf_mbox_base;	/* PF-AF mbox start address */
	struct pci_dev *pdev;		/* PCI device handle */
	/* AF <=> PF mbox */
	struct otx2_mbox	afpf_mbox;
	struct work_struct	afpf_mbox_work;
	struct workqueue_struct *afpf_mbox_wq;

	u8 pf_id;               /* RVU PF number */
};

irqreturn_t otx2_cptpf_afpf_mbox_intr(int irq, void *arg);
void otx2_cptpf_afpf_mbox_handler(struct work_struct *work);

#endif /* __OTX2_CPTPF_H */
