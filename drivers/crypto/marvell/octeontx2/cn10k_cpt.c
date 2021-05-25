// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 Marvell. */

#include "otx2_cptpf.h"
#include "otx2_cptvf.h"
#include "otx2_cptlf.h"
#include "cn10k_cpt.h"

int cn10k_cptpf_lmtst_init(struct otx2_cptpf_dev *cptpf)
{
	struct pci_dev *pdev = cptpf->pdev;
	resource_size_t size;
	u64 lmt_base;

	if (!test_bit(CN10K_LMTST, &cptpf->cap_flag))
		return 0;

	lmt_base = readq(cptpf->reg_base + RVU_PF_LMTLINE_ADDR);
	if (!lmt_base) {
		dev_err(&pdev->dev, "PF LMTLINE address not configured\n");
		return -ENOMEM;
	}
	size = pci_resource_len(pdev, PCI_MBOX_BAR_NUM);
	size -= ((1 + cptpf->max_vfs) * MBOX_SIZE);
	cptpf->lfs.lmt_base = devm_ioremap_wc(&pdev->dev, lmt_base, size);
	if (!cptpf->lfs.lmt_base) {
		dev_err(&pdev->dev,
			"Mapping of PF LMTLINE address failed\n");
		return -ENOMEM;
	}

	return 0;
}

int cn10k_cptvf_lmtst_init(struct otx2_cptvf_dev *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	resource_size_t offset, size;

	if (!test_bit(CN10K_LMTST, &cptvf->cap_flag))
		return 0;

	offset = pci_resource_start(pdev, PCI_MBOX_BAR_NUM);
	size = pci_resource_len(pdev, PCI_MBOX_BAR_NUM);
	/* Map VF LMILINE region */
	cptvf->lfs.lmt_base = devm_ioremap_wc(&pdev->dev, offset, size);
	if (!cptvf->lfs.lmt_base) {
		dev_err(&pdev->dev, "Unable to map BAR4\n");
		return -ENOMEM;
	}

	return 0;
}
