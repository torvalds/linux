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
	.cpt_sg_info_create = otx2_sg_info_create,
};

static struct cpt_hw_ops cn10k_hw_ops = {
	.send_cmd = cn10k_cpt_send_cmd,
	.cpt_get_compcode = cn10k_cpt_get_compcode,
	.cpt_get_uc_compcode = cn10k_cpt_get_uc_compcode,
	.cpt_sg_info_create = otx2_sg_info_create,
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
EXPORT_SYMBOL_NS_GPL(cn10k_cptvf_lmtst_init, CRYPTO_DEV_OCTEONTX2_CPT);

void cn10k_cpt_hw_ctx_clear(struct pci_dev *pdev,
			    struct cn10k_cpt_errata_ctx *er_ctx)
{
	u64 cptr_dma;

	if (!is_dev_cn10ka_ax(pdev))
		return;

	cptr_dma = er_ctx->cptr_dma & ~(BIT_ULL(60));
	cn10k_cpt_ctx_flush(pdev, cptr_dma, true);
	dma_unmap_single(&pdev->dev, cptr_dma, CN10K_CPT_HW_CTX_SIZE,
			 DMA_BIDIRECTIONAL);
	kfree(er_ctx->hw_ctx);
}
EXPORT_SYMBOL_NS_GPL(cn10k_cpt_hw_ctx_clear, CRYPTO_DEV_OCTEONTX2_CPT);

void cn10k_cpt_hw_ctx_set(union cn10k_cpt_hw_ctx *hctx, u16 ctx_sz)
{
	hctx->w0.aop_valid = 1;
	hctx->w0.ctx_hdr_sz = 0;
	hctx->w0.ctx_sz = ctx_sz;
	hctx->w0.ctx_push_sz = 1;
}
EXPORT_SYMBOL_NS_GPL(cn10k_cpt_hw_ctx_set, CRYPTO_DEV_OCTEONTX2_CPT);

int cn10k_cpt_hw_ctx_init(struct pci_dev *pdev,
			  struct cn10k_cpt_errata_ctx *er_ctx)
{
	union cn10k_cpt_hw_ctx *hctx;
	u64 cptr_dma;

	er_ctx->cptr_dma = 0;
	er_ctx->hw_ctx = NULL;

	if (!is_dev_cn10ka_ax(pdev))
		return 0;

	hctx = kmalloc(CN10K_CPT_HW_CTX_SIZE, GFP_KERNEL);
	if (unlikely(!hctx))
		return -ENOMEM;
	cptr_dma = dma_map_single(&pdev->dev, hctx, CN10K_CPT_HW_CTX_SIZE,
				  DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&pdev->dev, cptr_dma)) {
		kfree(hctx);
		return -ENOMEM;
	}

	cn10k_cpt_hw_ctx_set(hctx, 1);
	er_ctx->hw_ctx = hctx;
	er_ctx->cptr_dma = cptr_dma | BIT_ULL(60);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cn10k_cpt_hw_ctx_init, CRYPTO_DEV_OCTEONTX2_CPT);

void cn10k_cpt_ctx_flush(struct pci_dev *pdev, u64 cptr, bool inval)
{
	struct otx2_cptvf_dev *cptvf = pci_get_drvdata(pdev);
	struct otx2_cptlfs_info *lfs = &cptvf->lfs;
	u64 reg;

	reg = (uintptr_t)cptr >> 7;
	if (inval)
		reg = reg | BIT_ULL(46);

	otx2_cpt_write64(lfs->reg_base, lfs->blkaddr, lfs->lf[0].slot,
			 OTX2_CPT_LF_CTX_FLUSH, reg);
	/* Make sure that the FLUSH operation is complete */
	wmb();
	otx2_cpt_read64(lfs->reg_base, lfs->blkaddr, lfs->lf[0].slot,
			OTX2_CPT_LF_CTX_ERR);
}
EXPORT_SYMBOL_NS_GPL(cn10k_cpt_ctx_flush, CRYPTO_DEV_OCTEONTX2_CPT);

void cptvf_hw_ops_get(struct otx2_cptvf_dev *cptvf)
{
	if (test_bit(CN10K_LMTST, &cptvf->cap_flag))
		cptvf->lfs.ops = &cn10k_hw_ops;
	else
		cptvf->lfs.ops = &otx2_hw_ops;
}
EXPORT_SYMBOL_NS_GPL(cptvf_hw_ops_get, CRYPTO_DEV_OCTEONTX2_CPT);
