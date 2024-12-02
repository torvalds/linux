/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/cpumask.h>
#include <linux/string.h>

#include "csio_init.h"
#include "csio_hw.h"

static irqreturn_t
csio_nondata_isr(int irq, void *dev_id)
{
	struct csio_hw *hw = (struct csio_hw *) dev_id;
	int rv;
	unsigned long flags;

	if (unlikely(!hw))
		return IRQ_NONE;

	if (unlikely(pci_channel_offline(hw->pdev))) {
		CSIO_INC_STATS(hw, n_pcich_offline);
		return IRQ_NONE;
	}

	spin_lock_irqsave(&hw->lock, flags);
	csio_hw_slow_intr_handler(hw);
	rv = csio_mb_isr_handler(hw);

	if (rv == 0 && !(hw->flags & CSIO_HWF_FWEVT_PENDING)) {
		hw->flags |= CSIO_HWF_FWEVT_PENDING;
		spin_unlock_irqrestore(&hw->lock, flags);
		schedule_work(&hw->evtq_work);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&hw->lock, flags);
	return IRQ_HANDLED;
}

/*
 * csio_fwevt_handler - Common FW event handler routine.
 * @hw: HW module.
 *
 * This is the ISR for FW events. It is shared b/w MSIX
 * and INTx handlers.
 */
static void
csio_fwevt_handler(struct csio_hw *hw)
{
	int rv;
	unsigned long flags;

	rv = csio_fwevtq_handler(hw);

	spin_lock_irqsave(&hw->lock, flags);
	if (rv == 0 && !(hw->flags & CSIO_HWF_FWEVT_PENDING)) {
		hw->flags |= CSIO_HWF_FWEVT_PENDING;
		spin_unlock_irqrestore(&hw->lock, flags);
		schedule_work(&hw->evtq_work);
		return;
	}
	spin_unlock_irqrestore(&hw->lock, flags);

} /* csio_fwevt_handler */

/*
 * csio_fwevt_isr() - FW events MSIX ISR
 * @irq:
 * @dev_id:
 *
 * Process WRs on the FW event queue.
 *
 */
static irqreturn_t
csio_fwevt_isr(int irq, void *dev_id)
{
	struct csio_hw *hw = (struct csio_hw *) dev_id;

	if (unlikely(!hw))
		return IRQ_NONE;

	if (unlikely(pci_channel_offline(hw->pdev))) {
		CSIO_INC_STATS(hw, n_pcich_offline);
		return IRQ_NONE;
	}

	csio_fwevt_handler(hw);

	return IRQ_HANDLED;
}

/*
 * csio_fwevt_isr() - INTx wrapper for handling FW events.
 * @irq:
 * @dev_id:
 */
void
csio_fwevt_intx_handler(struct csio_hw *hw, void *wr, uint32_t len,
			   struct csio_fl_dma_buf *flb, void *priv)
{
	csio_fwevt_handler(hw);
} /* csio_fwevt_intx_handler */

/*
 * csio_process_scsi_cmpl - Process a SCSI WR completion.
 * @hw: HW module.
 * @wr: The completed WR from the ingress queue.
 * @len: Length of the WR.
 * @flb: Freelist buffer array.
 *
 */
static void
csio_process_scsi_cmpl(struct csio_hw *hw, void *wr, uint32_t len,
			struct csio_fl_dma_buf *flb, void *cbfn_q)
{
	struct csio_ioreq *ioreq;
	uint8_t *scsiwr;
	uint8_t subop;
	void *cmnd;
	unsigned long flags;

	ioreq = csio_scsi_cmpl_handler(hw, wr, len, flb, NULL, &scsiwr);
	if (likely(ioreq)) {
		if (unlikely(*scsiwr == FW_SCSI_ABRT_CLS_WR)) {
			subop = FW_SCSI_ABRT_CLS_WR_SUB_OPCODE_GET(
					((struct fw_scsi_abrt_cls_wr *)
					    scsiwr)->sub_opcode_to_chk_all_io);

			csio_dbg(hw, "%s cmpl recvd ioreq:%p status:%d\n",
				    subop ? "Close" : "Abort",
				    ioreq, ioreq->wr_status);

			spin_lock_irqsave(&hw->lock, flags);
			if (subop)
				csio_scsi_closed(ioreq,
						 (struct list_head *)cbfn_q);
			else
				csio_scsi_aborted(ioreq,
						  (struct list_head *)cbfn_q);
			/*
			 * We call scsi_done for I/Os that driver thinks aborts
			 * have timed out. If there is a race caused by FW
			 * completing abort at the exact same time that the
			 * driver has deteced the abort timeout, the following
			 * check prevents calling of scsi_done twice for the
			 * same command: once from the eh_abort_handler, another
			 * from csio_scsi_isr_handler(). This also avoids the
			 * need to check if csio_scsi_cmnd(req) is NULL in the
			 * fast path.
			 */
			cmnd = csio_scsi_cmnd(ioreq);
			if (unlikely(cmnd == NULL))
				list_del_init(&ioreq->sm.sm_list);

			spin_unlock_irqrestore(&hw->lock, flags);

			if (unlikely(cmnd == NULL))
				csio_put_scsi_ioreq_lock(hw,
						csio_hw_to_scsim(hw), ioreq);
		} else {
			spin_lock_irqsave(&hw->lock, flags);
			csio_scsi_completed(ioreq, (struct list_head *)cbfn_q);
			spin_unlock_irqrestore(&hw->lock, flags);
		}
	}
}

/*
 * csio_scsi_isr_handler() - Common SCSI ISR handler.
 * @iq: Ingress queue pointer.
 *
 * Processes SCSI completions on the SCSI IQ indicated by scm->iq_idx
 * by calling csio_wr_process_iq_idx. If there are completions on the
 * isr_cbfn_q, yank them out into a local queue and call their io_cbfns.
 * Once done, add these completions onto the freelist.
 * This routine is shared b/w MSIX and INTx.
 */
static inline irqreturn_t
csio_scsi_isr_handler(struct csio_q *iq)
{
	struct csio_hw *hw = (struct csio_hw *)iq->owner;
	LIST_HEAD(cbfn_q);
	struct list_head *tmp;
	struct csio_scsim *scm;
	struct csio_ioreq *ioreq;
	int isr_completions = 0;

	scm = csio_hw_to_scsim(hw);

	if (unlikely(csio_wr_process_iq(hw, iq, csio_process_scsi_cmpl,
					&cbfn_q) != 0))
		return IRQ_NONE;

	/* Call back the completion routines */
	list_for_each(tmp, &cbfn_q) {
		ioreq = (struct csio_ioreq *)tmp;
		isr_completions++;
		ioreq->io_cbfn(hw, ioreq);
		/* Release ddp buffer if used for this req */
		if (unlikely(ioreq->dcopy))
			csio_put_scsi_ddp_list_lock(hw, scm, &ioreq->gen_list,
						    ioreq->nsge);
	}

	if (isr_completions) {
		/* Return the ioreqs back to ioreq->freelist */
		csio_put_scsi_ioreq_list_lock(hw, scm, &cbfn_q,
					      isr_completions);
	}

	return IRQ_HANDLED;
}

/*
 * csio_scsi_isr() - SCSI MSIX handler
 * @irq:
 * @dev_id:
 *
 * This is the top level SCSI MSIX handler. Calls csio_scsi_isr_handler()
 * for handling SCSI completions.
 */
static irqreturn_t
csio_scsi_isr(int irq, void *dev_id)
{
	struct csio_q *iq = (struct csio_q *) dev_id;
	struct csio_hw *hw;

	if (unlikely(!iq))
		return IRQ_NONE;

	hw = (struct csio_hw *)iq->owner;

	if (unlikely(pci_channel_offline(hw->pdev))) {
		CSIO_INC_STATS(hw, n_pcich_offline);
		return IRQ_NONE;
	}

	csio_scsi_isr_handler(iq);

	return IRQ_HANDLED;
}

/*
 * csio_scsi_intx_handler() - SCSI INTx handler
 * @irq:
 * @dev_id:
 *
 * This is the top level SCSI INTx handler. Calls csio_scsi_isr_handler()
 * for handling SCSI completions.
 */
void
csio_scsi_intx_handler(struct csio_hw *hw, void *wr, uint32_t len,
			struct csio_fl_dma_buf *flb, void *priv)
{
	struct csio_q *iq = priv;

	csio_scsi_isr_handler(iq);

} /* csio_scsi_intx_handler */

/*
 * csio_fcoe_isr() - INTx/MSI interrupt service routine for FCoE.
 * @irq:
 * @dev_id:
 *
 *
 */
static irqreturn_t
csio_fcoe_isr(int irq, void *dev_id)
{
	struct csio_hw *hw = (struct csio_hw *) dev_id;
	struct csio_q *intx_q = NULL;
	int rv;
	irqreturn_t ret = IRQ_NONE;
	unsigned long flags;

	if (unlikely(!hw))
		return IRQ_NONE;

	if (unlikely(pci_channel_offline(hw->pdev))) {
		CSIO_INC_STATS(hw, n_pcich_offline);
		return IRQ_NONE;
	}

	/* Disable the interrupt for this PCI function. */
	if (hw->intr_mode == CSIO_IM_INTX)
		csio_wr_reg32(hw, 0, MYPF_REG(PCIE_PF_CLI_A));

	/*
	 * The read in the following function will flush the
	 * above write.
	 */
	if (csio_hw_slow_intr_handler(hw))
		ret = IRQ_HANDLED;

	/* Get the INTx Forward interrupt IQ. */
	intx_q = csio_get_q(hw, hw->intr_iq_idx);

	CSIO_DB_ASSERT(intx_q);

	/* IQ handler is not possible for intx_q, hence pass in NULL */
	if (likely(csio_wr_process_iq(hw, intx_q, NULL, NULL) == 0))
		ret = IRQ_HANDLED;

	spin_lock_irqsave(&hw->lock, flags);
	rv = csio_mb_isr_handler(hw);
	if (rv == 0 && !(hw->flags & CSIO_HWF_FWEVT_PENDING)) {
		hw->flags |= CSIO_HWF_FWEVT_PENDING;
		spin_unlock_irqrestore(&hw->lock, flags);
		schedule_work(&hw->evtq_work);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&hw->lock, flags);

	return ret;
}

static void
csio_add_msix_desc(struct csio_hw *hw)
{
	int i;
	struct csio_msix_entries *entryp = &hw->msix_entries[0];
	int k = CSIO_EXTRA_VECS;
	int len = sizeof(entryp->desc) - 1;
	int cnt = hw->num_sqsets + k;

	/* Non-data vector */
	memset(entryp->desc, 0, len + 1);
	snprintf(entryp->desc, len, "csio-%02x:%02x:%x-nondata",
		 CSIO_PCI_BUS(hw), CSIO_PCI_DEV(hw), CSIO_PCI_FUNC(hw));

	entryp++;
	memset(entryp->desc, 0, len + 1);
	snprintf(entryp->desc, len, "csio-%02x:%02x:%x-fwevt",
		 CSIO_PCI_BUS(hw), CSIO_PCI_DEV(hw), CSIO_PCI_FUNC(hw));
	entryp++;

	/* Name SCSI vecs */
	for (i = k; i < cnt; i++, entryp++) {
		memset(entryp->desc, 0, len + 1);
		snprintf(entryp->desc, len, "csio-%02x:%02x:%x-scsi%d",
			 CSIO_PCI_BUS(hw), CSIO_PCI_DEV(hw),
			 CSIO_PCI_FUNC(hw), i - CSIO_EXTRA_VECS);
	}
}

int
csio_request_irqs(struct csio_hw *hw)
{
	int rv, i, j, k = 0;
	struct csio_msix_entries *entryp = &hw->msix_entries[0];
	struct csio_scsi_cpu_info *info;
	struct pci_dev *pdev = hw->pdev;

	if (hw->intr_mode != CSIO_IM_MSIX) {
		rv = request_irq(pci_irq_vector(pdev, 0), csio_fcoe_isr,
				hw->intr_mode == CSIO_IM_MSI ? 0 : IRQF_SHARED,
				KBUILD_MODNAME, hw);
		if (rv) {
			csio_err(hw, "Failed to allocate interrupt line.\n");
			goto out_free_irqs;
		}

		goto out;
	}

	/* Add the MSIX vector descriptions */
	csio_add_msix_desc(hw);

	rv = request_irq(pci_irq_vector(pdev, k), csio_nondata_isr, 0,
			 entryp[k].desc, hw);
	if (rv) {
		csio_err(hw, "IRQ request failed for vec %d err:%d\n",
			 pci_irq_vector(pdev, k), rv);
		goto out_free_irqs;
	}

	entryp[k++].dev_id = hw;

	rv = request_irq(pci_irq_vector(pdev, k), csio_fwevt_isr, 0,
			 entryp[k].desc, hw);
	if (rv) {
		csio_err(hw, "IRQ request failed for vec %d err:%d\n",
			 pci_irq_vector(pdev, k), rv);
		goto out_free_irqs;
	}

	entryp[k++].dev_id = (void *)hw;

	/* Allocate IRQs for SCSI */
	for (i = 0; i < hw->num_pports; i++) {
		info = &hw->scsi_cpu_info[i];
		for (j = 0; j < info->max_cpus; j++, k++) {
			struct csio_scsi_qset *sqset = &hw->sqset[i][j];
			struct csio_q *q = hw->wrm.q_arr[sqset->iq_idx];

			rv = request_irq(pci_irq_vector(pdev, k), csio_scsi_isr, 0,
					 entryp[k].desc, q);
			if (rv) {
				csio_err(hw,
				       "IRQ request failed for vec %d err:%d\n",
				       pci_irq_vector(pdev, k), rv);
				goto out_free_irqs;
			}

			entryp[k].dev_id = q;

		} /* for all scsi cpus */
	} /* for all ports */

out:
	hw->flags |= CSIO_HWF_HOST_INTR_ENABLED;
	return 0;

out_free_irqs:
	for (i = 0; i < k; i++)
		free_irq(pci_irq_vector(pdev, i), hw->msix_entries[i].dev_id);
	pci_free_irq_vectors(hw->pdev);
	return -EINVAL;
}

/* Reduce per-port max possible CPUs */
static void
csio_reduce_sqsets(struct csio_hw *hw, int cnt)
{
	int i;
	struct csio_scsi_cpu_info *info;

	while (cnt < hw->num_sqsets) {
		for (i = 0; i < hw->num_pports; i++) {
			info = &hw->scsi_cpu_info[i];
			if (info->max_cpus > 1) {
				info->max_cpus--;
				hw->num_sqsets--;
				if (hw->num_sqsets <= cnt)
					break;
			}
		}
	}

	csio_dbg(hw, "Reduced sqsets to %d\n", hw->num_sqsets);
}

static void csio_calc_sets(struct irq_affinity *affd, unsigned int nvecs)
{
	struct csio_hw *hw = affd->priv;
	u8 i;

	if (!nvecs)
		return;

	if (nvecs < hw->num_pports) {
		affd->nr_sets = 1;
		affd->set_size[0] = nvecs;
		return;
	}

	affd->nr_sets = hw->num_pports;
	for (i = 0; i < hw->num_pports; i++)
		affd->set_size[i] = nvecs / hw->num_pports;
}

static int
csio_enable_msix(struct csio_hw *hw)
{
	int i, j, k, n, min, cnt;
	int extra = CSIO_EXTRA_VECS;
	struct csio_scsi_cpu_info *info;
	struct irq_affinity desc = {
		.pre_vectors = CSIO_EXTRA_VECS,
		.calc_sets = csio_calc_sets,
		.priv = hw,
	};

	if (hw->num_pports > IRQ_AFFINITY_MAX_SETS)
		return -ENOSPC;

	min = hw->num_pports + extra;
	cnt = hw->num_sqsets + extra;

	/* Max vectors required based on #niqs configured in fw */
	if (hw->flags & CSIO_HWF_USING_SOFT_PARAMS || !csio_is_hw_master(hw))
		cnt = min_t(uint8_t, hw->cfg_niq, cnt);

	csio_dbg(hw, "FW supp #niq:%d, trying %d msix's\n", hw->cfg_niq, cnt);

	cnt = pci_alloc_irq_vectors_affinity(hw->pdev, min, cnt,
			PCI_IRQ_MSIX | PCI_IRQ_AFFINITY, &desc);
	if (cnt < 0)
		return cnt;

	if (cnt < (hw->num_sqsets + extra)) {
		csio_dbg(hw, "Reducing sqsets to %d\n", cnt - extra);
		csio_reduce_sqsets(hw, cnt - extra);
	}

	/* Distribute vectors */
	k = 0;
	csio_set_nondata_intr_idx(hw, k);
	csio_set_mb_intr_idx(csio_hw_to_mbm(hw), k++);
	csio_set_fwevt_intr_idx(hw, k++);

	for (i = 0; i < hw->num_pports; i++) {
		info = &hw->scsi_cpu_info[i];

		for (j = 0; j < hw->num_scsi_msix_cpus; j++) {
			n = (j % info->max_cpus) +  k;
			hw->sqset[i][j].intr_idx = n;
		}

		k += info->max_cpus;
	}

	return 0;
}

void
csio_intr_enable(struct csio_hw *hw)
{
	hw->intr_mode = CSIO_IM_NONE;
	hw->flags &= ~CSIO_HWF_HOST_INTR_ENABLED;

	/* Try MSIX, then MSI or fall back to INTx */
	if ((csio_msi == 2) && !csio_enable_msix(hw))
		hw->intr_mode = CSIO_IM_MSIX;
	else {
		/* Max iqs required based on #niqs configured in fw */
		if (hw->flags & CSIO_HWF_USING_SOFT_PARAMS ||
			!csio_is_hw_master(hw)) {
			int extra = CSIO_EXTRA_MSI_IQS;

			if (hw->cfg_niq < (hw->num_sqsets + extra)) {
				csio_dbg(hw, "Reducing sqsets to %d\n",
					 hw->cfg_niq - extra);
				csio_reduce_sqsets(hw, hw->cfg_niq - extra);
			}
		}

		if ((csio_msi == 1) && !pci_enable_msi(hw->pdev))
			hw->intr_mode = CSIO_IM_MSI;
		else
			hw->intr_mode = CSIO_IM_INTX;
	}

	csio_dbg(hw, "Using %s interrupt mode.\n",
		(hw->intr_mode == CSIO_IM_MSIX) ? "MSIX" :
		((hw->intr_mode == CSIO_IM_MSI) ? "MSI" : "INTx"));
}

void
csio_intr_disable(struct csio_hw *hw, bool free)
{
	csio_hw_intr_disable(hw);

	if (free) {
		int i;

		switch (hw->intr_mode) {
		case CSIO_IM_MSIX:
			for (i = 0; i < hw->num_sqsets + CSIO_EXTRA_VECS; i++) {
				free_irq(pci_irq_vector(hw->pdev, i),
					 hw->msix_entries[i].dev_id);
			}
			break;
		case CSIO_IM_MSI:
		case CSIO_IM_INTX:
			free_irq(pci_irq_vector(hw->pdev, 0), hw);
			break;
		default:
			break;
		}
	}

	pci_free_irq_vectors(hw->pdev);
	hw->intr_mode = CSIO_IM_NONE;
	hw->flags &= ~CSIO_HWF_HOST_INTR_ENABLED;
}
