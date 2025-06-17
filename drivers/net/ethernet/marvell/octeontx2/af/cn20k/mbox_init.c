// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/interrupt.h>
#include <linux/irq.h>

#include "rvu_trace.h"
#include "mbox.h"
#include "reg.h"
#include "api.h"

static irqreturn_t cn20k_afvf_mbox_intr_handler(int irq, void *rvu_irq)
{
	struct rvu_irq_data *rvu_irq_data = rvu_irq;
	struct rvu *rvu = rvu_irq_data->rvu;
	u64 intr;

	/* Sync with mbox memory region */
	rmb();

	/* Clear interrupts */
	intr = rvupf_read64(rvu, rvu_irq_data->intr_status);
	rvupf_write64(rvu, rvu_irq_data->intr_status, intr);

	if (intr)
		trace_otx2_msg_interrupt(rvu->pdev, "VF(s) to AF", intr);

	rvu_irq_data->afvf_queue_work_hdlr(&rvu->afvf_wq_info, rvu_irq_data->start,
					   rvu_irq_data->mdevs, intr);

	return IRQ_HANDLED;
}

int cn20k_register_afvf_mbox_intr(struct rvu *rvu, int pf_vec_start)
{
	struct rvu_irq_data *irq_data;
	int intr_vec, offset, vec = 0;
	int err;

	/* irq data for 4 VFPF intr vectors */
	irq_data = devm_kcalloc(rvu->dev, 4,
				sizeof(struct rvu_irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	for (intr_vec = RVU_MBOX_PF_INT_VEC_VFPF_MBOX0; intr_vec <=
					RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1;
					intr_vec++, vec++) {
		switch (intr_vec) {
		case RVU_MBOX_PF_INT_VEC_VFPF_MBOX0:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF_INTX(0);
			irq_data[vec].start = 0;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF_MBOX1:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF_INTX(1);
			irq_data[vec].start = 64;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF1_MBOX0:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF1_INTX(0);
			irq_data[vec].start = 0;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1:
			irq_data[vec].intr_status = RVU_MBOX_PF_VFPF1_INTX(1);
			irq_data[vec].start = 64;
			irq_data[vec].mdevs = 64;
			break;
		}
		irq_data[vec].afvf_queue_work_hdlr =
						rvu_queue_work;
		offset = pf_vec_start + intr_vec;
		irq_data[vec].vec_num = offset;
		irq_data[vec].rvu = rvu;

		sprintf(&rvu->irq_name[offset * NAME_SIZE], "RVUAF VFAF%d Mbox%d",
			vec / 2, vec % 2);
		err = request_irq(pci_irq_vector(rvu->pdev, offset),
				  rvu->ng_rvu->rvu_mbox_ops->afvf_intr_handler, 0,
				  &rvu->irq_name[offset * NAME_SIZE],
				  &irq_data[vec]);
		if (err) {
			dev_err(rvu->dev,
				"RVUAF: IRQ registration failed for AFVF mbox irq\n");
			return err;
		}
		rvu->irq_allocated[offset] = true;
	}

	return 0;
}

/* CN20K mbox PFx => AF irq handler */
static irqreturn_t cn20k_mbox_pf_common_intr_handler(int irq, void *rvu_irq)
{
	struct rvu_irq_data *rvu_irq_data = rvu_irq;
	struct rvu *rvu = rvu_irq_data->rvu;
	u64 intr;

	/* Clear interrupts */
	intr = rvu_read64(rvu, BLKADDR_RVUM, rvu_irq_data->intr_status);
	rvu_write64(rvu, BLKADDR_RVUM, rvu_irq_data->intr_status, intr);

	if (intr)
		trace_otx2_msg_interrupt(rvu->pdev, "PF(s) to AF", intr);

	/* Sync with mbox memory region */
	rmb();

	rvu_irq_data->rvu_queue_work_hdlr(&rvu->afpf_wq_info,
					  rvu_irq_data->start,
					  rvu_irq_data->mdevs, intr);

	return IRQ_HANDLED;
}

void cn20k_rvu_enable_mbox_intr(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;

	/* Clear spurious irqs, if any */
	rvu_write64(rvu, BLKADDR_RVUM,
		    RVU_MBOX_AF_PFAF_INT(0), INTR_MASK(hw->total_pfs));

	rvu_write64(rvu, BLKADDR_RVUM,
		    RVU_MBOX_AF_PFAF_INT(1), INTR_MASK(hw->total_pfs - 64));

	rvu_write64(rvu, BLKADDR_RVUM,
		    RVU_MBOX_AF_PFAF1_INT(0), INTR_MASK(hw->total_pfs));

	rvu_write64(rvu, BLKADDR_RVUM,
		    RVU_MBOX_AF_PFAF1_INT(1), INTR_MASK(hw->total_pfs - 64));

	/* Enable mailbox interrupt for all PFs except PF0 i.e AF itself */
	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF_INT_ENA_W1S(0),
		    INTR_MASK(hw->total_pfs) & ~1ULL);

	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF_INT_ENA_W1S(1),
		    INTR_MASK(hw->total_pfs - 64));

	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF1_INT_ENA_W1S(0),
		    INTR_MASK(hw->total_pfs) & ~1ULL);

	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF1_INT_ENA_W1S(1),
		    INTR_MASK(hw->total_pfs - 64));
}

void cn20k_rvu_unregister_interrupts(struct rvu *rvu)
{
	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF_INT_ENA_W1C(0),
		    INTR_MASK(rvu->hw->total_pfs) & ~1ULL);

	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF_INT_ENA_W1C(1),
		    INTR_MASK(rvu->hw->total_pfs - 64));

	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF1_INT_ENA_W1C(0),
		    INTR_MASK(rvu->hw->total_pfs) & ~1ULL);

	rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFAF1_INT_ENA_W1C(1),
		    INTR_MASK(rvu->hw->total_pfs - 64));
}

int cn20k_register_afpf_mbox_intr(struct rvu *rvu)
{
	struct rvu_irq_data *irq_data;
	int intr_vec, ret, vec = 0;

	/* irq data for 4 PF intr vectors */
	irq_data = devm_kcalloc(rvu->dev, 4,
				sizeof(struct rvu_irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	for (intr_vec = RVU_AF_CN20K_INT_VEC_PFAF_MBOX0; intr_vec <=
				RVU_AF_CN20K_INT_VEC_PFAF1_MBOX1; intr_vec++,
				vec++) {
		switch (intr_vec) {
		case RVU_AF_CN20K_INT_VEC_PFAF_MBOX0:
			irq_data[vec].intr_status =
						RVU_MBOX_AF_PFAF_INT(0);
			irq_data[vec].start = 0;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_AF_CN20K_INT_VEC_PFAF_MBOX1:
			irq_data[vec].intr_status =
						RVU_MBOX_AF_PFAF_INT(1);
			irq_data[vec].start = 64;
			irq_data[vec].mdevs = 96;
			break;
		case RVU_AF_CN20K_INT_VEC_PFAF1_MBOX0:
			irq_data[vec].intr_status =
						RVU_MBOX_AF_PFAF1_INT(0);
			irq_data[vec].start = 0;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_AF_CN20K_INT_VEC_PFAF1_MBOX1:
			irq_data[vec].intr_status =
						RVU_MBOX_AF_PFAF1_INT(1);
			irq_data[vec].start = 64;
			irq_data[vec].mdevs = 96;
			break;
		}
		irq_data[vec].rvu_queue_work_hdlr = rvu_queue_work;
		irq_data[vec].vec_num = intr_vec;
		irq_data[vec].rvu = rvu;

		/* Register mailbox interrupt handler */
		sprintf(&rvu->irq_name[intr_vec * NAME_SIZE],
			"RVUAF PFAF%d Mbox%d",
			vec / 2, vec % 2);
		ret = request_irq(pci_irq_vector(rvu->pdev, intr_vec),
				  rvu->ng_rvu->rvu_mbox_ops->pf_intr_handler, 0,
				  &rvu->irq_name[intr_vec * NAME_SIZE],
				  &irq_data[vec]);
		if (ret)
			return ret;

		rvu->irq_allocated[intr_vec] = true;
	}

	return 0;
}

int cn20k_rvu_get_mbox_regions(struct rvu *rvu, void **mbox_addr,
			       int num, int type, unsigned long *pf_bmap)
{
	int region;
	u64 bar;

	if (type == TYPE_AFVF) {
		for (region = 0; region < num; region++) {
			if (!test_bit(region, pf_bmap))
				continue;

			bar = (u64)phys_to_virt((u64)rvu->ng_rvu->vf_mbox_addr->base);
			bar += region * MBOX_SIZE;
			mbox_addr[region] = (void *)bar;

			if (!mbox_addr[region])
				return -ENOMEM;
		}
		return 0;
	}

	for (region = 0; region < num; region++) {
		if (!test_bit(region, pf_bmap))
			continue;

		bar = (u64)phys_to_virt((u64)rvu->ng_rvu->pf_mbox_addr->base);
		bar += region * MBOX_SIZE;

		mbox_addr[region] = (void *)bar;

		if (!mbox_addr[region])
			return -ENOMEM;
	}
	return 0;
}

static int rvu_alloc_mbox_memory(struct rvu *rvu, int type,
				 int ndevs, int mbox_size)
{
	struct qmem *mbox_addr;
	dma_addr_t iova;
	int pf, err;

	/* Allocate contiguous memory for mailbox communication.
	 * eg: AF <=> PFx mbox memory
	 * This allocated memory is split into chunks of MBOX_SIZE
	 * and setup into each of the RVU PFs. In HW this memory will
	 * get aliased to an offset within BAR2 of those PFs.
	 *
	 * AF will access mbox memory using direct physical addresses
	 * and PFs will access the same shared memory from BAR2.
	 *
	 * PF <=> VF mbox memory also works in the same fashion.
	 * AFPF, PFVF requires IOVA to be used to maintain the mailbox msgs
	 */

	err = qmem_alloc(rvu->dev, &mbox_addr, ndevs, mbox_size);
	if (err)
		return -ENOMEM;

	switch (type) {
	case TYPE_AFPF:
		rvu->ng_rvu->pf_mbox_addr = mbox_addr;
		iova = (u64)mbox_addr->iova;
		for (pf = 0; pf < ndevs; pf++) {
			rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_AF_PFX_ADDR(pf),
				    (u64)iova);
			iova += mbox_size;
		}
		break;
	case TYPE_AFVF:
		rvu->ng_rvu->vf_mbox_addr = mbox_addr;
		rvupf_write64(rvu, RVU_PF_VF_MBOX_ADDR, (u64)mbox_addr->iova);
		break;
	default:
		return 0;
	}

	return 0;
}

static struct mbox_ops cn20k_mbox_ops = {
	.pf_intr_handler = cn20k_mbox_pf_common_intr_handler,
	.afvf_intr_handler = cn20k_afvf_mbox_intr_handler,
};

int cn20k_rvu_mbox_init(struct rvu *rvu, int type, int ndevs)
{
	int dev;

	if (!is_cn20k(rvu->pdev))
		return 0;

	rvu->ng_rvu->rvu_mbox_ops = &cn20k_mbox_ops;

	if (type == TYPE_AFVF) {
		rvu_write64(rvu, BLKADDR_RVUM, RVU_MBOX_PF_VF_CFG, ilog2(MBOX_SIZE));
	} else {
		for (dev = 0; dev < ndevs; dev++)
			rvu_write64(rvu, BLKADDR_RVUM,
				    RVU_MBOX_AF_PFX_CFG(dev), ilog2(MBOX_SIZE));
	}

	return rvu_alloc_mbox_memory(rvu, type, ndevs, MBOX_SIZE);
}

void cn20k_free_mbox_memory(struct rvu *rvu)
{
	if (!is_cn20k(rvu->pdev))
		return;

	qmem_free(rvu->dev, rvu->ng_rvu->pf_mbox_addr);
	qmem_free(rvu->dev, rvu->ng_rvu->vf_mbox_addr);
}

void cn20k_rvu_disable_afvf_intr(struct rvu *rvu, int vfs)
{
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF_INT_ENA_W1CX(0), INTR_MASK(vfs));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF1_INT_ENA_W1CX(0), INTR_MASK(vfs));
	rvupf_write64(rvu, RVU_PF_VFFLR_INT_ENA_W1CX(0), INTR_MASK(vfs));
	rvupf_write64(rvu, RVU_PF_VFME_INT_ENA_W1CX(0), INTR_MASK(vfs));

	if (vfs <= 64)
		return;

	rvupf_write64(rvu, RVU_MBOX_PF_VFPF_INT_ENA_W1CX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF1_INT_ENA_W1CX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_PF_VFFLR_INT_ENA_W1CX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_PF_VFME_INT_ENA_W1CX(1), INTR_MASK(vfs - 64));
}

void cn20k_rvu_enable_afvf_intr(struct rvu *rvu, int vfs)
{
	/* Clear any pending interrupts and enable AF VF interrupts for
	 * the first 64 VFs.
	 */
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF_INTX(0), INTR_MASK(vfs));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF_INT_ENA_W1SX(0), INTR_MASK(vfs));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF1_INTX(0), INTR_MASK(vfs));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF1_INT_ENA_W1SX(0), INTR_MASK(vfs));

	/* FLR */
	rvupf_write64(rvu, RVU_PF_VFFLR_INTX(0), INTR_MASK(vfs));
	rvupf_write64(rvu, RVU_PF_VFFLR_INT_ENA_W1SX(0), INTR_MASK(vfs));

	/* Same for remaining VFs, if any. */
	if (vfs <= 64)
		return;

	rvupf_write64(rvu, RVU_MBOX_PF_VFPF_INTX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF_INT_ENA_W1SX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF1_INTX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_MBOX_PF_VFPF1_INT_ENA_W1SX(1), INTR_MASK(vfs - 64));

	rvupf_write64(rvu, RVU_PF_VFFLR_INTX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_PF_VFFLR_INT_ENA_W1SX(1), INTR_MASK(vfs - 64));
	rvupf_write64(rvu, RVU_PF_VFME_INT_ENA_W1SX(1), INTR_MASK(vfs - 64));
}

int rvu_alloc_cint_qint_mem(struct rvu *rvu, struct rvu_pfvf *pfvf,
			    int blkaddr, int nixlf)
{
	int qints, hwctx_size, err;
	u64 cfg, ctx_cfg;

	if (is_rvu_otx2(rvu) || is_cn20k(rvu->pdev))
		return 0;

	ctx_cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST3);
	/* Alloc memory for CQINT's HW contexts */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST2);
	qints = (cfg >> 24) & 0xFFF;
	hwctx_size = 1UL << ((ctx_cfg >> 24) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->cq_ints_ctx, qints, hwctx_size);
	if (err)
		return -ENOMEM;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_CINTS_BASE(nixlf),
		    (u64)pfvf->cq_ints_ctx->iova);

	/* Alloc memory for QINT's HW contexts */
	cfg = rvu_read64(rvu, blkaddr, NIX_AF_CONST2);
	qints = (cfg >> 12) & 0xFFF;
	hwctx_size = 1UL << ((ctx_cfg >> 20) & 0xF);
	err = qmem_alloc(rvu->dev, &pfvf->nix_qints_ctx, qints, hwctx_size);
	if (err)
		return -ENOMEM;

	rvu_write64(rvu, blkaddr, NIX_AF_LFX_QINTS_BASE(nixlf),
		    (u64)pfvf->nix_qints_ctx->iova);

	return 0;
}
