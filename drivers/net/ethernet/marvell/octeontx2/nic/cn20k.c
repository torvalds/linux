// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include "otx2_common.h"
#include "otx2_reg.h"
#include "otx2_struct.h"
#include "cn10k.h"

static struct dev_hw_ops cn20k_hw_ops = {
	.pfaf_mbox_intr_handler = cn20k_pfaf_mbox_intr_handler,
	.vfaf_mbox_intr_handler = cn20k_vfaf_mbox_intr_handler,
	.pfvf_mbox_intr_handler = cn20k_pfvf_mbox_intr_handler,
};

void cn20k_init(struct otx2_nic *pfvf)
{
	pfvf->hw_ops = &cn20k_hw_ops;
}
EXPORT_SYMBOL(cn20k_init);
/* CN20K mbox AF => PFx irq handler */
irqreturn_t cn20k_pfaf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct otx2_nic *pf = pf_irq;
	struct mbox *mw = &pf->mbox;
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	u64 pf_trig_val;

	pf_trig_val = otx2_read64(pf, RVU_PF_INT) & 0x3ULL;

	/* Clear the IRQ */
	otx2_write64(pf, RVU_PF_INT, pf_trig_val);

	if (pf_trig_val & BIT_ULL(0)) {
		mbox = &mw->mbox_up;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(pf->mbox_wq, &mw->mbox_up_wrk);

		trace_otx2_msg_interrupt(pf->pdev, "UP message from AF to PF",
					 BIT_ULL(0));
	}

	if (pf_trig_val & BIT_ULL(1)) {
		mbox = &mw->mbox;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(pf->mbox_wq, &mw->mbox_wrk);
		trace_otx2_msg_interrupt(pf->pdev, "DOWN reply from AF to PF",
					 BIT_ULL(1));
	}

	return IRQ_HANDLED;
}

irqreturn_t cn20k_vfaf_mbox_intr_handler(int irq, void *vf_irq)
{
	struct otx2_nic *vf = vf_irq;
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	u64 vf_trig_val;

	vf_trig_val = otx2_read64(vf, RVU_VF_INT) & 0x3ULL;
	/* Clear the IRQ */
	otx2_write64(vf, RVU_VF_INT, vf_trig_val);

	/* Read latest mbox data */
	smp_rmb();

	if (vf_trig_val & BIT_ULL(1)) {
		/* Check for PF => VF response messages */
		mbox = &vf->mbox.mbox;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(vf->mbox_wq, &vf->mbox.mbox_wrk);

		trace_otx2_msg_interrupt(mbox->pdev, "DOWN reply from PF0 to VF",
					 BIT_ULL(1));
	}

	if (vf_trig_val & BIT_ULL(0)) {
		/* Check for PF => VF notification messages */
		mbox = &vf->mbox.mbox_up;
		mdev = &mbox->dev[0];
		otx2_sync_mbox_bbuf(mbox, 0);

		hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
		if (hdr->num_msgs)
			queue_work(vf->mbox_wq, &vf->mbox.mbox_up_wrk);

		trace_otx2_msg_interrupt(mbox->pdev, "UP message from PF0 to VF",
					 BIT_ULL(0));
	}

	return IRQ_HANDLED;
}

void cn20k_enable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	/* Clear PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(1), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(1), ~0ull);

	/* Enable PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1SX(0), INTR_MASK(numvfs));
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1SX(0), INTR_MASK(numvfs));
	if (numvfs > 64) {
		numvfs -= 64;
		otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1SX(1),
			     INTR_MASK(numvfs));
		otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1SX(1),
			     INTR_MASK(numvfs));
	}
}

void cn20k_disable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	int vector, intr_vec, vec = 0;

	/* Disable PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1CX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF_INT_ENA_W1CX(1), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1CX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INT_ENA_W1CX(1), ~0ull);

	otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(0), ~0ull);
	otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(0), ~0ull);

	if (numvfs > 64) {
		otx2_write64(pf, RVU_MBOX_PF_VFPF_INTX(1), ~0ull);
		otx2_write64(pf, RVU_MBOX_PF_VFPF1_INTX(1), ~0ull);
	}

	for (intr_vec = RVU_MBOX_PF_INT_VEC_VFPF_MBOX0; intr_vec <=
			RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1; intr_vec++, vec++) {
		vector = pci_irq_vector(pf->pdev, intr_vec);
		free_irq(vector, pf->hw.pfvf_irq_devid[vec]);
	}
}

irqreturn_t cn20k_pfvf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct pf_irq_data *irq_data = pf_irq;
	struct otx2_nic *pf = irq_data->pf;
	struct mbox *mbox;
	u64 intr;

	/* Sync with mbox memory region */
	rmb();

	/* Clear interrupts */
	intr = otx2_read64(pf, irq_data->intr_status);
	otx2_write64(pf, irq_data->intr_status, intr);
	mbox = pf->mbox_pfvf;

	if (intr)
		trace_otx2_msg_interrupt(pf->pdev, "VF(s) to PF", intr);

	irq_data->pf_queue_work_hdlr(mbox, pf->mbox_pfvf_wq, irq_data->start,
				     irq_data->mdevs, intr);

	return IRQ_HANDLED;
}

int cn20k_register_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	struct otx2_hw *hw = &pf->hw;
	struct pf_irq_data *irq_data;
	int intr_vec, ret, vec = 0;
	char *irq_name;

	/* irq data for 4 PF intr vectors */
	irq_data = devm_kcalloc(pf->dev, 4,
				sizeof(struct pf_irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	for (intr_vec = RVU_MBOX_PF_INT_VEC_VFPF_MBOX0; intr_vec <=
			RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1; intr_vec++, vec++) {
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
			irq_data[vec].mdevs = 96;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF1_MBOX0:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF1_INTX(0);
			irq_data[vec].start = 0;
			irq_data[vec].mdevs = 64;
			break;
		case RVU_MBOX_PF_INT_VEC_VFPF1_MBOX1:
			irq_data[vec].intr_status =
						RVU_MBOX_PF_VFPF1_INTX(1);
			irq_data[vec].start = 64;
			irq_data[vec].mdevs = 96;
			break;
		}
		irq_data[vec].pf_queue_work_hdlr = otx2_queue_vf_work;
		irq_data[vec].vec_num = intr_vec;
		irq_data[vec].pf = pf;

		/* Register mailbox interrupt handler */
		irq_name = &hw->irq_name[intr_vec * NAME_SIZE];
		if (pf->pcifunc)
			snprintf(irq_name, NAME_SIZE,
				 "RVUPF%d_VF%d Mbox%d", rvu_get_pf(pf->pdev,
				 pf->pcifunc), vec / 2, vec % 2);
		else
			snprintf(irq_name, NAME_SIZE, "RVUPF_VF%d Mbox%d",
				 vec / 2, vec % 2);

		hw->pfvf_irq_devid[vec] = &irq_data[vec];
		ret = request_irq(pci_irq_vector(pf->pdev, intr_vec),
				  pf->hw_ops->pfvf_mbox_intr_handler, 0,
				  irq_name,
				  &irq_data[vec]);
		if (ret) {
			dev_err(pf->dev,
				"RVUPF: IRQ registration failed for PFVF mbox0 irq\n");
			return ret;
		}
	}

	cn20k_enable_pfvf_mbox_intr(pf, numvfs);

	return 0;
}
