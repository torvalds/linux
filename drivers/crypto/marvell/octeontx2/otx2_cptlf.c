// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2020 Marvell. */

#include "otx2_cpt_common.h"
#include "otx2_cptlf.h"
#include "rvu_reg.h"

#define CPT_TIMER_HOLD 0x03F
#define CPT_COUNT_HOLD 32

static void cptlf_do_set_done_time_wait(struct otx2_cptlf_info *lf,
					int time_wait)
{
	union otx2_cptx_lf_done_wait done_wait;

	done_wait.u = otx2_cpt_read64(lf->lfs->reg_base, lf->lfs->blkaddr,
				      lf->slot, OTX2_CPT_LF_DONE_WAIT);
	done_wait.s.time_wait = time_wait;
	otx2_cpt_write64(lf->lfs->reg_base, lf->lfs->blkaddr, lf->slot,
			 OTX2_CPT_LF_DONE_WAIT, done_wait.u);
}

static void cptlf_do_set_done_num_wait(struct otx2_cptlf_info *lf, int num_wait)
{
	union otx2_cptx_lf_done_wait done_wait;

	done_wait.u = otx2_cpt_read64(lf->lfs->reg_base, lf->lfs->blkaddr,
				      lf->slot, OTX2_CPT_LF_DONE_WAIT);
	done_wait.s.num_wait = num_wait;
	otx2_cpt_write64(lf->lfs->reg_base, lf->lfs->blkaddr, lf->slot,
			 OTX2_CPT_LF_DONE_WAIT, done_wait.u);
}

static void cptlf_set_done_time_wait(struct otx2_cptlfs_info *lfs,
				     int time_wait)
{
	int slot;

	for (slot = 0; slot < lfs->lfs_num; slot++)
		cptlf_do_set_done_time_wait(&lfs->lf[slot], time_wait);
}

static void cptlf_set_done_num_wait(struct otx2_cptlfs_info *lfs, int num_wait)
{
	int slot;

	for (slot = 0; slot < lfs->lfs_num; slot++)
		cptlf_do_set_done_num_wait(&lfs->lf[slot], num_wait);
}

static int cptlf_set_pri(struct otx2_cptlf_info *lf, int pri)
{
	struct otx2_cptlfs_info *lfs = lf->lfs;
	union otx2_cptx_af_lf_ctrl lf_ctrl;
	int ret;

	ret = otx2_cpt_read_af_reg(lfs->mbox, lfs->pdev,
				   CPT_AF_LFX_CTL(lf->slot),
				   &lf_ctrl.u, lfs->blkaddr);
	if (ret)
		return ret;

	lf_ctrl.s.pri = pri ? 1 : 0;

	ret = otx2_cpt_write_af_reg(lfs->mbox, lfs->pdev,
				    CPT_AF_LFX_CTL(lf->slot),
				    lf_ctrl.u, lfs->blkaddr);
	return ret;
}

static int cptlf_set_eng_grps_mask(struct otx2_cptlf_info *lf,
				   int eng_grps_mask)
{
	struct otx2_cptlfs_info *lfs = lf->lfs;
	union otx2_cptx_af_lf_ctrl lf_ctrl;
	int ret;

	ret = otx2_cpt_read_af_reg(lfs->mbox, lfs->pdev,
				   CPT_AF_LFX_CTL(lf->slot),
				   &lf_ctrl.u, lfs->blkaddr);
	if (ret)
		return ret;

	lf_ctrl.s.grp = eng_grps_mask;

	ret = otx2_cpt_write_af_reg(lfs->mbox, lfs->pdev,
				    CPT_AF_LFX_CTL(lf->slot),
				    lf_ctrl.u, lfs->blkaddr);
	return ret;
}

static int cptlf_set_grp_and_pri(struct otx2_cptlfs_info *lfs,
				 int eng_grp_mask, int pri)
{
	int slot, ret = 0;

	for (slot = 0; slot < lfs->lfs_num; slot++) {
		ret = cptlf_set_pri(&lfs->lf[slot], pri);
		if (ret)
			return ret;

		ret = cptlf_set_eng_grps_mask(&lfs->lf[slot], eng_grp_mask);
		if (ret)
			return ret;
	}
	return ret;
}

static int cptlf_set_ctx_ilen(struct otx2_cptlfs_info *lfs, int ctx_ilen)
{
	union otx2_cptx_af_lf_ctrl lf_ctrl;
	struct otx2_cptlf_info *lf;
	int slot, ret = 0;

	for (slot = 0; slot < lfs->lfs_num; slot++) {
		lf = &lfs->lf[slot];

		ret = otx2_cpt_read_af_reg(lfs->mbox, lfs->pdev,
					   CPT_AF_LFX_CTL(lf->slot),
					   &lf_ctrl.u, lfs->blkaddr);
		if (ret)
			return ret;

		lf_ctrl.s.ctx_ilen = ctx_ilen;

		ret = otx2_cpt_write_af_reg(lfs->mbox, lfs->pdev,
					    CPT_AF_LFX_CTL(lf->slot),
					    lf_ctrl.u, lfs->blkaddr);
		if (ret)
			return ret;
	}
	return ret;
}

static void cptlf_hw_init(struct otx2_cptlfs_info *lfs)
{
	/* Disable instruction queues */
	otx2_cptlf_disable_iqueues(lfs);

	/* Set instruction queues base addresses */
	otx2_cptlf_set_iqueues_base_addr(lfs);

	/* Set instruction queues sizes */
	otx2_cptlf_set_iqueues_size(lfs);

	/* Set done interrupts time wait */
	cptlf_set_done_time_wait(lfs, CPT_TIMER_HOLD);

	/* Set done interrupts num wait */
	cptlf_set_done_num_wait(lfs, CPT_COUNT_HOLD);

	/* Enable instruction queues */
	otx2_cptlf_enable_iqueues(lfs);
}

static void cptlf_hw_cleanup(struct otx2_cptlfs_info *lfs)
{
	/* Disable instruction queues */
	otx2_cptlf_disable_iqueues(lfs);
}

static void cptlf_set_misc_intrs(struct otx2_cptlfs_info *lfs, u8 enable)
{
	union otx2_cptx_lf_misc_int_ena_w1s irq_misc = { .u = 0x0 };
	u64 reg = enable ? OTX2_CPT_LF_MISC_INT_ENA_W1S :
			   OTX2_CPT_LF_MISC_INT_ENA_W1C;
	int slot;

	irq_misc.s.fault = 0x1;
	irq_misc.s.hwerr = 0x1;
	irq_misc.s.irde = 0x1;
	irq_misc.s.nqerr = 0x1;
	irq_misc.s.nwrp = 0x1;

	for (slot = 0; slot < lfs->lfs_num; slot++)
		otx2_cpt_write64(lfs->reg_base, lfs->blkaddr, slot, reg,
				 irq_misc.u);
}

static void cptlf_set_done_intrs(struct otx2_cptlfs_info *lfs, u8 enable)
{
	u64 reg = enable ? OTX2_CPT_LF_DONE_INT_ENA_W1S :
			   OTX2_CPT_LF_DONE_INT_ENA_W1C;
	int slot;

	for (slot = 0; slot < lfs->lfs_num; slot++)
		otx2_cpt_write64(lfs->reg_base, lfs->blkaddr, slot, reg, 0x1);
}

static inline int cptlf_read_done_cnt(struct otx2_cptlf_info *lf)
{
	union otx2_cptx_lf_done irq_cnt;

	irq_cnt.u = otx2_cpt_read64(lf->lfs->reg_base, lf->lfs->blkaddr, lf->slot,
				    OTX2_CPT_LF_DONE);
	return irq_cnt.s.done;
}

static irqreturn_t cptlf_misc_intr_handler(int __always_unused irq, void *arg)
{
	union otx2_cptx_lf_misc_int irq_misc, irq_misc_ack;
	struct otx2_cptlf_info *lf = arg;
	struct device *dev;

	dev = &lf->lfs->pdev->dev;
	irq_misc.u = otx2_cpt_read64(lf->lfs->reg_base, lf->lfs->blkaddr,
				     lf->slot, OTX2_CPT_LF_MISC_INT);
	irq_misc_ack.u = 0x0;

	if (irq_misc.s.fault) {
		dev_err(dev, "Memory error detected while executing CPT_INST_S, LF %d.\n",
			lf->slot);
		irq_misc_ack.s.fault = 0x1;

	} else if (irq_misc.s.hwerr) {
		dev_err(dev, "HW error from an engine executing CPT_INST_S, LF %d.",
			lf->slot);
		irq_misc_ack.s.hwerr = 0x1;

	} else if (irq_misc.s.nwrp) {
		dev_err(dev, "SMMU fault while writing CPT_RES_S to CPT_INST_S[RES_ADDR], LF %d.\n",
			lf->slot);
		irq_misc_ack.s.nwrp = 0x1;

	} else if (irq_misc.s.irde) {
		dev_err(dev, "Memory error when accessing instruction memory queue CPT_LF_Q_BASE[ADDR].\n");
		irq_misc_ack.s.irde = 0x1;

	} else if (irq_misc.s.nqerr) {
		dev_err(dev, "Error enqueuing an instruction received at CPT_LF_NQ.\n");
		irq_misc_ack.s.nqerr = 0x1;

	} else {
		dev_err(dev, "Unhandled interrupt in CPT LF %d\n", lf->slot);
		return IRQ_NONE;
	}

	/* Acknowledge interrupts */
	otx2_cpt_write64(lf->lfs->reg_base, lf->lfs->blkaddr, lf->slot,
			 OTX2_CPT_LF_MISC_INT, irq_misc_ack.u);

	return IRQ_HANDLED;
}

static irqreturn_t cptlf_done_intr_handler(int irq, void *arg)
{
	union otx2_cptx_lf_done_wait done_wait;
	struct otx2_cptlf_info *lf = arg;
	int irq_cnt;

	/* Read the number of completed requests */
	irq_cnt = cptlf_read_done_cnt(lf);
	if (irq_cnt) {
		done_wait.u = otx2_cpt_read64(lf->lfs->reg_base, lf->lfs->blkaddr,
					      lf->slot, OTX2_CPT_LF_DONE_WAIT);
		/* Acknowledge the number of completed requests */
		otx2_cpt_write64(lf->lfs->reg_base, lf->lfs->blkaddr, lf->slot,
				 OTX2_CPT_LF_DONE_ACK, irq_cnt);

		otx2_cpt_write64(lf->lfs->reg_base, lf->lfs->blkaddr, lf->slot,
				 OTX2_CPT_LF_DONE_WAIT, done_wait.u);
		if (unlikely(!lf->wqe)) {
			dev_err(&lf->lfs->pdev->dev, "No work for LF %d\n",
				lf->slot);
			return IRQ_NONE;
		}

		/* Schedule processing of completed requests */
		tasklet_hi_schedule(&lf->wqe->work);
	}
	return IRQ_HANDLED;
}

void otx2_cptlf_unregister_misc_interrupts(struct otx2_cptlfs_info *lfs)
{
	int i, irq_offs, vector;

	irq_offs = OTX2_CPT_LF_INT_VEC_E_MISC;
	for (i = 0; i < lfs->lfs_num; i++) {
		if (!lfs->lf[i].is_irq_reg[irq_offs])
			continue;

		vector = pci_irq_vector(lfs->pdev,
					lfs->lf[i].msix_offset + irq_offs);
		free_irq(vector, &lfs->lf[i]);
		lfs->lf[i].is_irq_reg[irq_offs] = false;
	}

	cptlf_set_misc_intrs(lfs, false);
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_unregister_misc_interrupts, "CRYPTO_DEV_OCTEONTX2_CPT");

void otx2_cptlf_unregister_done_interrupts(struct otx2_cptlfs_info *lfs)
{
	int i, irq_offs, vector;

	irq_offs = OTX2_CPT_LF_INT_VEC_E_DONE;
	for (i = 0; i < lfs->lfs_num; i++) {
		if (!lfs->lf[i].is_irq_reg[irq_offs])
			continue;

		vector = pci_irq_vector(lfs->pdev,
					lfs->lf[i].msix_offset + irq_offs);
		free_irq(vector, &lfs->lf[i]);
		lfs->lf[i].is_irq_reg[irq_offs] = false;
	}

	cptlf_set_done_intrs(lfs, false);
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_unregister_done_interrupts, "CRYPTO_DEV_OCTEONTX2_CPT");

static int cptlf_do_register_interrrupts(struct otx2_cptlfs_info *lfs,
					 int lf_num, int irq_offset,
					 irq_handler_t handler)
{
	int ret, vector;

	vector = pci_irq_vector(lfs->pdev, lfs->lf[lf_num].msix_offset +
				irq_offset);
	ret = request_irq(vector, handler, 0,
			  lfs->lf[lf_num].irq_name[irq_offset],
			  &lfs->lf[lf_num]);
	if (ret)
		return ret;

	lfs->lf[lf_num].is_irq_reg[irq_offset] = true;

	return ret;
}

int otx2_cptlf_register_misc_interrupts(struct otx2_cptlfs_info *lfs)
{
	bool is_cpt1 = (lfs->blkaddr == BLKADDR_CPT1);
	int irq_offs, ret, i;

	irq_offs = OTX2_CPT_LF_INT_VEC_E_MISC;
	for (i = 0; i < lfs->lfs_num; i++) {
		snprintf(lfs->lf[i].irq_name[irq_offs], 32, "CPT%dLF Misc%d",
			 is_cpt1, i);
		ret = cptlf_do_register_interrrupts(lfs, i, irq_offs,
						    cptlf_misc_intr_handler);
		if (ret)
			goto free_irq;
	}
	cptlf_set_misc_intrs(lfs, true);
	return 0;

free_irq:
	otx2_cptlf_unregister_misc_interrupts(lfs);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_register_misc_interrupts, "CRYPTO_DEV_OCTEONTX2_CPT");

int otx2_cptlf_register_done_interrupts(struct otx2_cptlfs_info *lfs)
{
	bool is_cpt1 = (lfs->blkaddr == BLKADDR_CPT1);
	int irq_offs, ret, i;

	irq_offs = OTX2_CPT_LF_INT_VEC_E_DONE;
	for (i = 0; i < lfs->lfs_num; i++) {
		snprintf(lfs->lf[i].irq_name[irq_offs], 32,
			 "OTX2_CPT%dLF Done%d", is_cpt1, i);
		ret = cptlf_do_register_interrrupts(lfs, i, irq_offs,
						    cptlf_done_intr_handler);
		if (ret)
			goto free_irq;
	}
	cptlf_set_done_intrs(lfs, true);
	return 0;

free_irq:
	otx2_cptlf_unregister_done_interrupts(lfs);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_register_done_interrupts, "CRYPTO_DEV_OCTEONTX2_CPT");

void otx2_cptlf_free_irqs_affinity(struct otx2_cptlfs_info *lfs)
{
	int slot, offs;

	for (slot = 0; slot < lfs->lfs_num; slot++) {
		for (offs = 0; offs < OTX2_CPT_LF_MSIX_VECTORS; offs++)
			irq_set_affinity_hint(pci_irq_vector(lfs->pdev,
					      lfs->lf[slot].msix_offset +
					      offs), NULL);
		free_cpumask_var(lfs->lf[slot].affinity_mask);
	}
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_free_irqs_affinity, "CRYPTO_DEV_OCTEONTX2_CPT");

int otx2_cptlf_set_irqs_affinity(struct otx2_cptlfs_info *lfs)
{
	struct otx2_cptlf_info *lf = lfs->lf;
	int slot, offs, ret;

	for (slot = 0; slot < lfs->lfs_num; slot++) {
		if (!zalloc_cpumask_var(&lf[slot].affinity_mask, GFP_KERNEL)) {
			dev_err(&lfs->pdev->dev,
				"cpumask allocation failed for LF %d", slot);
			ret = -ENOMEM;
			goto free_affinity_mask;
		}

		cpumask_set_cpu(cpumask_local_spread(slot,
				dev_to_node(&lfs->pdev->dev)),
				lf[slot].affinity_mask);

		for (offs = 0; offs < OTX2_CPT_LF_MSIX_VECTORS; offs++) {
			ret = irq_set_affinity_hint(pci_irq_vector(lfs->pdev,
						lf[slot].msix_offset + offs),
						lf[slot].affinity_mask);
			if (ret)
				goto free_affinity_mask;
		}
	}
	return 0;

free_affinity_mask:
	otx2_cptlf_free_irqs_affinity(lfs);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_set_irqs_affinity, "CRYPTO_DEV_OCTEONTX2_CPT");

int otx2_cptlf_init(struct otx2_cptlfs_info *lfs, u8 eng_grp_mask, int pri,
		    int lfs_num)
{
	int slot, ret;

	if (!lfs->pdev || !lfs->reg_base)
		return -EINVAL;

	lfs->lfs_num = lfs_num;
	for (slot = 0; slot < lfs->lfs_num; slot++) {
		lfs->lf[slot].lfs = lfs;
		lfs->lf[slot].slot = slot;
		if (!lfs->lmt_info.base)
			lfs->lf[slot].lmtline = lfs->reg_base +
				OTX2_CPT_RVU_FUNC_ADDR_S(BLKADDR_LMT, slot,
						 OTX2_CPT_LMT_LF_LMTLINEX(0));

		lfs->lf[slot].ioreg = lfs->reg_base +
			OTX2_CPT_RVU_FUNC_ADDR_S(lfs->blkaddr, slot,
						 OTX2_CPT_LF_NQX(0));
	}
	/* Send request to attach LFs */
	ret = otx2_cpt_attach_rscrs_msg(lfs);
	if (ret)
		goto clear_lfs_num;

	ret = otx2_cpt_alloc_instruction_queues(lfs);
	if (ret) {
		dev_err(&lfs->pdev->dev,
			"Allocating instruction queues failed\n");
		goto detach_rsrcs;
	}
	cptlf_hw_init(lfs);
	/*
	 * Allow each LF to execute requests destined to any of 8 engine
	 * groups and set queue priority of each LF to high
	 */
	ret = cptlf_set_grp_and_pri(lfs, eng_grp_mask, pri);
	if (ret)
		goto free_iq;

	if (lfs->ctx_ilen_ovrd) {
		ret = cptlf_set_ctx_ilen(lfs, lfs->ctx_ilen);
		if (ret)
			goto free_iq;
	}

	return 0;

free_iq:
	cptlf_hw_cleanup(lfs);
	otx2_cpt_free_instruction_queues(lfs);
detach_rsrcs:
	otx2_cpt_detach_rsrcs_msg(lfs);
clear_lfs_num:
	lfs->lfs_num = 0;
	return ret;
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_init, "CRYPTO_DEV_OCTEONTX2_CPT");

void otx2_cptlf_shutdown(struct otx2_cptlfs_info *lfs)
{
	/* Cleanup LFs hardware side */
	cptlf_hw_cleanup(lfs);
	/* Free instruction queues */
	otx2_cpt_free_instruction_queues(lfs);
	/* Send request to detach LFs */
	otx2_cpt_detach_rsrcs_msg(lfs);
	lfs->lfs_num = 0;
}
EXPORT_SYMBOL_NS_GPL(otx2_cptlf_shutdown, "CRYPTO_DEV_OCTEONTX2_CPT");

MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell RVU CPT Common module");
MODULE_LICENSE("GPL");
