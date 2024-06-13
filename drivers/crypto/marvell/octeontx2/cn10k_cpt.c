// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 Marvell. */

#include <linux/soc/marvell/octeontx2/asm.h>
#include "otx2_cptpf.h"
#include "otx2_cptvf.h"
#include "otx2_cptlf.h"
#include "cn10k_cpt.h"

static void cn10k_cpt_send_cmd(union otx2_cpt_inst_s *cptinst, u32 insts_num,
			       struct otx2_cptlf_info *lf);

static struct cpt_hw_ops otx2_hw_ops = {
	.send_cmd = otx2_cpt_send_cmd,
	.cpt_get_compcode = otx2_cpt_get_compcode,
	.cpt_get_uc_compcode = otx2_cpt_get_uc_compcode,
};

static struct cpt_hw_ops cn10k_hw_ops = {
	.send_cmd = cn10k_cpt_send_cmd,
	.cpt_get_compcode = cn10k_cpt_get_compcode,
	.cpt_get_uc_compcode = cn10k_cpt_get_uc_compcode,
};

static void cn10k_cpt_send_cmd(union otx2_cpt_inst_s *cptinst, u32 insts_num,
			       struct otx2_cptlf_info *lf)
{
	void __iomem *lmtline = lf->lmtline;
	u64 val = (lf->slot & 0x7FF);
	u64 tar_addr = 0;

	/* tar_addr<6:4> = Size of first LMTST - 1 in units of 128b. */
	tar_addr |= (__force u64)lf->ioreg |
		    (((OTX2_CPT_INST_SIZE/16) - 1) & 0x7) << 4;
	/*
	 * Make sure memory areas pointed in CPT_INST_S
	 * are flushed before the instruction is sent to CPT
	 */
	dma_wmb();

	/* Copy CPT command to LMTLINE */
	memcpy_toio(lmtline, cptinst, insts_num * OTX2_CPT_INST_SIZE);
	cn10k_lmt_flush(val, tar_addr);
}

int cn10k_cptpf_lmtst_init(struct otx2_cptpf_dev *cptpf)
{
	struct pci_dev *pdev = cptpf->pdev;
	resource_size_t size;
	u64 lmt_base;

	if (!test_bit(CN10K_LMTST, &cptpf->cap_flag)) {
		cptpf->lfs.ops = &otx2_hw_ops;
		return 0;
	}

	cptpf->lfs.ops = &cn10k_hw_ops;
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
EXPORT_SYMBOL_NS_GPL(cn10k_cptpf_lmtst_init, CRYPTO_DEV_OCTEONTX2_CPT);

int cn10k_cptvf_lmtst_init(struct otx2_cptvf_dev *cptvf)
{
	struct pci_dev *pdev = cptvf->pdev;
	resource_size_t offset, size;

	if (!test_bit(CN10K_LMTST, &cptvf->cap_flag)) {
		cptvf->lfs.ops = &otx2_hw_ops;
		return 0;
	}

	cptvf->lfs.ops = &cn10k_hw_ops;
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
EXPORT_SYMBOL_NS_GPL(cn10k_cptvf_lmtst_init, CRYPTO_DEV_OCTEONTX2_CPT);
