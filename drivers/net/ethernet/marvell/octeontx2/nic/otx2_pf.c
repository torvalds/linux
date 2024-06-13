// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Physical Function ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/of.h>
#include <linux/if_vlan.h>
#include <linux/iommu.h>
#include <net/ip.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/bitfield.h>

#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_txrx.h"
#include "otx2_struct.h"
#include "otx2_ptp.h"
#include "cn10k.h"
#include "qos.h"
#include <rvu_trace.h>

#define DRV_NAME	"rvu_nicpf"
#define DRV_STRING	"Marvell RVU NIC Physical Function Driver"

/* Supported devices */
static const struct pci_device_id otx2_pf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_RVU_PF) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Sunil Goutham <sgoutham@marvell.com>");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(pci, otx2_pf_id_table);

static void otx2_vf_link_event_task(struct work_struct *work);

enum {
	TYPE_PFAF,
	TYPE_PFVF,
};

static int otx2_config_hw_tx_tstamp(struct otx2_nic *pfvf, bool enable);
static int otx2_config_hw_rx_tstamp(struct otx2_nic *pfvf, bool enable);

static int otx2_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	bool if_up = netif_running(netdev);
	int err = 0;

	if (pf->xdp_prog && new_mtu > MAX_XDP_MTU) {
		netdev_warn(netdev, "Jumbo frames not yet supported with XDP, current MTU %d.\n",
			    netdev->mtu);
		return -EINVAL;
	}
	if (if_up)
		otx2_stop(netdev);

	netdev_info(netdev, "Changing MTU from %d to %d\n",
		    netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;

	if (if_up)
		err = otx2_open(netdev);

	return err;
}

static void otx2_disable_flr_me_intr(struct otx2_nic *pf)
{
	int irq, vfs = pf->total_vfs;

	/* Disable VFs ME interrupts */
	otx2_write64(pf, RVU_PF_VFME_INT_ENA_W1CX(0), INTR_MASK(vfs));
	irq = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFME0);
	free_irq(irq, pf);

	/* Disable VFs FLR interrupts */
	otx2_write64(pf, RVU_PF_VFFLR_INT_ENA_W1CX(0), INTR_MASK(vfs));
	irq = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFFLR0);
	free_irq(irq, pf);

	if (vfs <= 64)
		return;

	otx2_write64(pf, RVU_PF_VFME_INT_ENA_W1CX(1), INTR_MASK(vfs - 64));
	irq = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFME1);
	free_irq(irq, pf);

	otx2_write64(pf, RVU_PF_VFFLR_INT_ENA_W1CX(1), INTR_MASK(vfs - 64));
	irq = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFFLR1);
	free_irq(irq, pf);
}

static void otx2_flr_wq_destroy(struct otx2_nic *pf)
{
	if (!pf->flr_wq)
		return;
	destroy_workqueue(pf->flr_wq);
	pf->flr_wq = NULL;
	devm_kfree(pf->dev, pf->flr_wrk);
}

static void otx2_flr_handler(struct work_struct *work)
{
	struct flr_work *flrwork = container_of(work, struct flr_work, work);
	struct otx2_nic *pf = flrwork->pf;
	struct mbox *mbox = &pf->mbox;
	struct msg_req *req;
	int vf, reg = 0;

	vf = flrwork - pf->flr_wrk;

	mutex_lock(&mbox->lock);
	req = otx2_mbox_alloc_msg_vf_flr(mbox);
	if (!req) {
		mutex_unlock(&mbox->lock);
		return;
	}
	req->hdr.pcifunc &= RVU_PFVF_FUNC_MASK;
	req->hdr.pcifunc |= (vf + 1) & RVU_PFVF_FUNC_MASK;

	if (!otx2_sync_mbox_msg(&pf->mbox)) {
		if (vf >= 64) {
			reg = 1;
			vf = vf - 64;
		}
		/* clear transcation pending bit */
		otx2_write64(pf, RVU_PF_VFTRPENDX(reg), BIT_ULL(vf));
		otx2_write64(pf, RVU_PF_VFFLR_INT_ENA_W1SX(reg), BIT_ULL(vf));
	}

	mutex_unlock(&mbox->lock);
}

static irqreturn_t otx2_pf_flr_intr_handler(int irq, void *pf_irq)
{
	struct otx2_nic *pf = (struct otx2_nic *)pf_irq;
	int reg, dev, vf, start_vf, num_reg = 1;
	u64 intr;

	if (pf->total_vfs > 64)
		num_reg = 2;

	for (reg = 0; reg < num_reg; reg++) {
		intr = otx2_read64(pf, RVU_PF_VFFLR_INTX(reg));
		if (!intr)
			continue;
		start_vf = 64 * reg;
		for (vf = 0; vf < 64; vf++) {
			if (!(intr & BIT_ULL(vf)))
				continue;
			dev = vf + start_vf;
			queue_work(pf->flr_wq, &pf->flr_wrk[dev].work);
			/* Clear interrupt */
			otx2_write64(pf, RVU_PF_VFFLR_INTX(reg), BIT_ULL(vf));
			/* Disable the interrupt */
			otx2_write64(pf, RVU_PF_VFFLR_INT_ENA_W1CX(reg),
				     BIT_ULL(vf));
		}
	}
	return IRQ_HANDLED;
}

static irqreturn_t otx2_pf_me_intr_handler(int irq, void *pf_irq)
{
	struct otx2_nic *pf = (struct otx2_nic *)pf_irq;
	int vf, reg, num_reg = 1;
	u64 intr;

	if (pf->total_vfs > 64)
		num_reg = 2;

	for (reg = 0; reg < num_reg; reg++) {
		intr = otx2_read64(pf, RVU_PF_VFME_INTX(reg));
		if (!intr)
			continue;
		for (vf = 0; vf < 64; vf++) {
			if (!(intr & BIT_ULL(vf)))
				continue;
			/* clear trpend bit */
			otx2_write64(pf, RVU_PF_VFTRPENDX(reg), BIT_ULL(vf));
			/* clear interrupt */
			otx2_write64(pf, RVU_PF_VFME_INTX(reg), BIT_ULL(vf));
		}
	}
	return IRQ_HANDLED;
}

static int otx2_register_flr_me_intr(struct otx2_nic *pf, int numvfs)
{
	struct otx2_hw *hw = &pf->hw;
	char *irq_name;
	int ret;

	/* Register ME interrupt handler*/
	irq_name = &hw->irq_name[RVU_PF_INT_VEC_VFME0 * NAME_SIZE];
	snprintf(irq_name, NAME_SIZE, "RVUPF%d_ME0", rvu_get_pf(pf->pcifunc));
	ret = request_irq(pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFME0),
			  otx2_pf_me_intr_handler, 0, irq_name, pf);
	if (ret) {
		dev_err(pf->dev,
			"RVUPF: IRQ registration failed for ME0\n");
	}

	/* Register FLR interrupt handler */
	irq_name = &hw->irq_name[RVU_PF_INT_VEC_VFFLR0 * NAME_SIZE];
	snprintf(irq_name, NAME_SIZE, "RVUPF%d_FLR0", rvu_get_pf(pf->pcifunc));
	ret = request_irq(pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFFLR0),
			  otx2_pf_flr_intr_handler, 0, irq_name, pf);
	if (ret) {
		dev_err(pf->dev,
			"RVUPF: IRQ registration failed for FLR0\n");
		return ret;
	}

	if (numvfs > 64) {
		irq_name = &hw->irq_name[RVU_PF_INT_VEC_VFME1 * NAME_SIZE];
		snprintf(irq_name, NAME_SIZE, "RVUPF%d_ME1",
			 rvu_get_pf(pf->pcifunc));
		ret = request_irq(pci_irq_vector
				  (pf->pdev, RVU_PF_INT_VEC_VFME1),
				  otx2_pf_me_intr_handler, 0, irq_name, pf);
		if (ret) {
			dev_err(pf->dev,
				"RVUPF: IRQ registration failed for ME1\n");
		}
		irq_name = &hw->irq_name[RVU_PF_INT_VEC_VFFLR1 * NAME_SIZE];
		snprintf(irq_name, NAME_SIZE, "RVUPF%d_FLR1",
			 rvu_get_pf(pf->pcifunc));
		ret = request_irq(pci_irq_vector
				  (pf->pdev, RVU_PF_INT_VEC_VFFLR1),
				  otx2_pf_flr_intr_handler, 0, irq_name, pf);
		if (ret) {
			dev_err(pf->dev,
				"RVUPF: IRQ registration failed for FLR1\n");
			return ret;
		}
	}

	/* Enable ME interrupt for all VFs*/
	otx2_write64(pf, RVU_PF_VFME_INTX(0), INTR_MASK(numvfs));
	otx2_write64(pf, RVU_PF_VFME_INT_ENA_W1SX(0), INTR_MASK(numvfs));

	/* Enable FLR interrupt for all VFs*/
	otx2_write64(pf, RVU_PF_VFFLR_INTX(0), INTR_MASK(numvfs));
	otx2_write64(pf, RVU_PF_VFFLR_INT_ENA_W1SX(0), INTR_MASK(numvfs));

	if (numvfs > 64) {
		numvfs -= 64;

		otx2_write64(pf, RVU_PF_VFME_INTX(1), INTR_MASK(numvfs));
		otx2_write64(pf, RVU_PF_VFME_INT_ENA_W1SX(1),
			     INTR_MASK(numvfs));

		otx2_write64(pf, RVU_PF_VFFLR_INTX(1), INTR_MASK(numvfs));
		otx2_write64(pf, RVU_PF_VFFLR_INT_ENA_W1SX(1),
			     INTR_MASK(numvfs));
	}
	return 0;
}

static int otx2_pf_flr_init(struct otx2_nic *pf, int num_vfs)
{
	int vf;

	pf->flr_wq = alloc_workqueue("otx2_pf_flr_wq",
				     WQ_UNBOUND | WQ_HIGHPRI, 1);
	if (!pf->flr_wq)
		return -ENOMEM;

	pf->flr_wrk = devm_kcalloc(pf->dev, num_vfs,
				   sizeof(struct flr_work), GFP_KERNEL);
	if (!pf->flr_wrk) {
		destroy_workqueue(pf->flr_wq);
		return -ENOMEM;
	}

	for (vf = 0; vf < num_vfs; vf++) {
		pf->flr_wrk[vf].pf = pf;
		INIT_WORK(&pf->flr_wrk[vf].work, otx2_flr_handler);
	}

	return 0;
}

static void otx2_queue_work(struct mbox *mw, struct workqueue_struct *mbox_wq,
			    int first, int mdevs, u64 intr, int type)
{
	struct otx2_mbox_dev *mdev;
	struct otx2_mbox *mbox;
	struct mbox_hdr *hdr;
	int i;

	for (i = first; i < mdevs; i++) {
		/* start from 0 */
		if (!(intr & BIT_ULL(i - first)))
			continue;

		mbox = &mw->mbox;
		mdev = &mbox->dev[i];
		if (type == TYPE_PFAF)
			otx2_sync_mbox_bbuf(mbox, i);
		hdr = mdev->mbase + mbox->rx_start;
		/* The hdr->num_msgs is set to zero immediately in the interrupt
		 * handler to  ensure that it holds a correct value next time
		 * when the interrupt handler is called.
		 * pf->mbox.num_msgs holds the data for use in pfaf_mbox_handler
		 * pf>mbox.up_num_msgs holds the data for use in
		 * pfaf_mbox_up_handler.
		 */
		if (hdr->num_msgs) {
			mw[i].num_msgs = hdr->num_msgs;
			hdr->num_msgs = 0;
			if (type == TYPE_PFAF)
				memset(mbox->hwbase + mbox->rx_start, 0,
				       ALIGN(sizeof(struct mbox_hdr),
					     sizeof(u64)));

			queue_work(mbox_wq, &mw[i].mbox_wrk);
		}

		mbox = &mw->mbox_up;
		mdev = &mbox->dev[i];
		if (type == TYPE_PFAF)
			otx2_sync_mbox_bbuf(mbox, i);
		hdr = mdev->mbase + mbox->rx_start;
		if (hdr->num_msgs) {
			mw[i].up_num_msgs = hdr->num_msgs;
			hdr->num_msgs = 0;
			if (type == TYPE_PFAF)
				memset(mbox->hwbase + mbox->rx_start, 0,
				       ALIGN(sizeof(struct mbox_hdr),
					     sizeof(u64)));

			queue_work(mbox_wq, &mw[i].mbox_up_wrk);
		}
	}
}

static void otx2_forward_msg_pfvf(struct otx2_mbox_dev *mdev,
				  struct otx2_mbox *pfvf_mbox, void *bbuf_base,
				  int devid)
{
	struct otx2_mbox_dev *src_mdev = mdev;
	int offset;

	/* Msgs are already copied, trigger VF's mbox irq */
	smp_wmb();

	offset = pfvf_mbox->trigger | (devid << pfvf_mbox->tr_shift);
	writeq(1, (void __iomem *)pfvf_mbox->reg_base + offset);

	/* Restore VF's mbox bounce buffer region address */
	src_mdev->mbase = bbuf_base;
}

static int otx2_forward_vf_mbox_msgs(struct otx2_nic *pf,
				     struct otx2_mbox *src_mbox,
				     int dir, int vf, int num_msgs)
{
	struct otx2_mbox_dev *src_mdev, *dst_mdev;
	struct mbox_hdr *mbox_hdr;
	struct mbox_hdr *req_hdr;
	struct mbox *dst_mbox;
	int dst_size, err;

	if (dir == MBOX_DIR_PFAF) {
		/* Set VF's mailbox memory as PF's bounce buffer memory, so
		 * that explicit copying of VF's msgs to PF=>AF mbox region
		 * and AF=>PF responses to VF's mbox region can be avoided.
		 */
		src_mdev = &src_mbox->dev[vf];
		mbox_hdr = src_mbox->hwbase +
				src_mbox->rx_start + (vf * MBOX_SIZE);

		dst_mbox = &pf->mbox;
		dst_size = dst_mbox->mbox.tx_size -
				ALIGN(sizeof(*mbox_hdr), MBOX_MSG_ALIGN);
		/* Check if msgs fit into destination area and has valid size */
		if (mbox_hdr->msg_size > dst_size || !mbox_hdr->msg_size)
			return -EINVAL;

		dst_mdev = &dst_mbox->mbox.dev[0];

		mutex_lock(&pf->mbox.lock);
		dst_mdev->mbase = src_mdev->mbase;
		dst_mdev->msg_size = mbox_hdr->msg_size;
		dst_mdev->num_msgs = num_msgs;
		err = otx2_sync_mbox_msg(dst_mbox);
		/* Error code -EIO indicate there is a communication failure
		 * to the AF. Rest of the error codes indicate that AF processed
		 * VF messages and set the error codes in response messages
		 * (if any) so simply forward responses to VF.
		 */
		if (err == -EIO) {
			dev_warn(pf->dev,
				 "AF not responding to VF%d messages\n", vf);
			/* restore PF mbase and exit */
			dst_mdev->mbase = pf->mbox.bbuf_base;
			mutex_unlock(&pf->mbox.lock);
			return err;
		}
		/* At this point, all the VF messages sent to AF are acked
		 * with proper responses and responses are copied to VF
		 * mailbox hence raise interrupt to VF.
		 */
		req_hdr = (struct mbox_hdr *)(dst_mdev->mbase +
					      dst_mbox->mbox.rx_start);
		req_hdr->num_msgs = num_msgs;

		otx2_forward_msg_pfvf(dst_mdev, &pf->mbox_pfvf[0].mbox,
				      pf->mbox.bbuf_base, vf);
		mutex_unlock(&pf->mbox.lock);
	} else if (dir == MBOX_DIR_PFVF_UP) {
		src_mdev = &src_mbox->dev[0];
		mbox_hdr = src_mbox->hwbase + src_mbox->rx_start;
		req_hdr = (struct mbox_hdr *)(src_mdev->mbase +
					      src_mbox->rx_start);
		req_hdr->num_msgs = num_msgs;

		dst_mbox = &pf->mbox_pfvf[0];
		dst_size = dst_mbox->mbox_up.tx_size -
				ALIGN(sizeof(*mbox_hdr), MBOX_MSG_ALIGN);
		/* Check if msgs fit into destination area */
		if (mbox_hdr->msg_size > dst_size)
			return -EINVAL;

		dst_mdev = &dst_mbox->mbox_up.dev[vf];
		dst_mdev->mbase = src_mdev->mbase;
		dst_mdev->msg_size = mbox_hdr->msg_size;
		dst_mdev->num_msgs = mbox_hdr->num_msgs;
		err = otx2_sync_mbox_up_msg(dst_mbox, vf);
		if (err) {
			dev_warn(pf->dev,
				 "VF%d is not responding to mailbox\n", vf);
			return err;
		}
	} else if (dir == MBOX_DIR_VFPF_UP) {
		req_hdr = (struct mbox_hdr *)(src_mbox->dev[0].mbase +
					      src_mbox->rx_start);
		req_hdr->num_msgs = num_msgs;
		otx2_forward_msg_pfvf(&pf->mbox_pfvf->mbox_up.dev[vf],
				      &pf->mbox.mbox_up,
				      pf->mbox_pfvf[vf].bbuf_base,
				      0);
	}

	return 0;
}

static void otx2_pfvf_mbox_handler(struct work_struct *work)
{
	struct mbox_msghdr *msg = NULL;
	int offset, vf_idx, id, err;
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *req_hdr;
	struct otx2_mbox *mbox;
	struct mbox *vf_mbox;
	struct otx2_nic *pf;

	vf_mbox = container_of(work, struct mbox, mbox_wrk);
	pf = vf_mbox->pfvf;
	vf_idx = vf_mbox - pf->mbox_pfvf;

	mbox = &pf->mbox_pfvf[0].mbox;
	mdev = &mbox->dev[vf_idx];
	req_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);

	offset = ALIGN(sizeof(*req_hdr), MBOX_MSG_ALIGN);

	for (id = 0; id < vf_mbox->num_msgs; id++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + mbox->rx_start +
					     offset);

		if (msg->sig != OTX2_MBOX_REQ_SIG)
			goto inval_msg;

		/* Set VF's number in each of the msg */
		msg->pcifunc &= RVU_PFVF_FUNC_MASK;
		msg->pcifunc |= (vf_idx + 1) & RVU_PFVF_FUNC_MASK;
		offset = msg->next_msgoff;
	}
	err = otx2_forward_vf_mbox_msgs(pf, mbox, MBOX_DIR_PFAF, vf_idx,
					vf_mbox->num_msgs);
	if (err)
		goto inval_msg;
	return;

inval_msg:
	otx2_reply_invalid_msg(mbox, vf_idx, 0, msg->id);
	otx2_mbox_msg_send(mbox, vf_idx);
}

static void otx2_pfvf_mbox_up_handler(struct work_struct *work)
{
	struct mbox *vf_mbox = container_of(work, struct mbox, mbox_up_wrk);
	struct otx2_nic *pf = vf_mbox->pfvf;
	struct otx2_mbox_dev *mdev;
	int offset, id, vf_idx = 0;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	struct otx2_mbox *mbox;

	vf_idx = vf_mbox - pf->mbox_pfvf;
	mbox = &pf->mbox_pfvf[0].mbox_up;
	mdev = &mbox->dev[vf_idx];

	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);
	offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);

	for (id = 0; id < vf_mbox->up_num_msgs; id++) {
		msg = mdev->mbase + offset;

		if (msg->id >= MBOX_MSG_MAX) {
			dev_err(pf->dev,
				"Mbox msg with unknown ID 0x%x\n", msg->id);
			goto end;
		}

		if (msg->sig != OTX2_MBOX_RSP_SIG) {
			dev_err(pf->dev,
				"Mbox msg with wrong signature %x, ID 0x%x\n",
				msg->sig, msg->id);
			goto end;
		}

		switch (msg->id) {
		case MBOX_MSG_CGX_LINK_EVENT:
			break;
		default:
			if (msg->rc)
				dev_err(pf->dev,
					"Mbox msg response has err %d, ID 0x%x\n",
					msg->rc, msg->id);
			break;
		}

end:
		offset = mbox->rx_start + msg->next_msgoff;
		if (mdev->msgs_acked == (vf_mbox->up_num_msgs - 1))
			__otx2_mbox_reset(mbox, 0);
		mdev->msgs_acked++;
	}
}

static irqreturn_t otx2_pfvf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct otx2_nic *pf = (struct otx2_nic *)(pf_irq);
	int vfs = pf->total_vfs;
	struct mbox *mbox;
	u64 intr;

	mbox = pf->mbox_pfvf;
	/* Handle VF interrupts */
	if (vfs > 64) {
		intr = otx2_read64(pf, RVU_PF_VFPF_MBOX_INTX(1));
		otx2_write64(pf, RVU_PF_VFPF_MBOX_INTX(1), intr);
		otx2_queue_work(mbox, pf->mbox_pfvf_wq, 64, vfs, intr,
				TYPE_PFVF);
		if (intr)
			trace_otx2_msg_interrupt(mbox->mbox.pdev, "VF(s) to PF", intr);
		vfs = 64;
	}

	intr = otx2_read64(pf, RVU_PF_VFPF_MBOX_INTX(0));
	otx2_write64(pf, RVU_PF_VFPF_MBOX_INTX(0), intr);

	otx2_queue_work(mbox, pf->mbox_pfvf_wq, 0, vfs, intr, TYPE_PFVF);

	if (intr)
		trace_otx2_msg_interrupt(mbox->mbox.pdev, "VF(s) to PF", intr);

	return IRQ_HANDLED;
}

static int otx2_pfvf_mbox_init(struct otx2_nic *pf, int numvfs)
{
	void __iomem *hwbase;
	struct mbox *mbox;
	int err, vf;
	u64 base;

	if (!numvfs)
		return -EINVAL;

	pf->mbox_pfvf = devm_kcalloc(&pf->pdev->dev, numvfs,
				     sizeof(struct mbox), GFP_KERNEL);
	if (!pf->mbox_pfvf)
		return -ENOMEM;

	pf->mbox_pfvf_wq = alloc_workqueue("otx2_pfvf_mailbox",
					   WQ_UNBOUND | WQ_HIGHPRI |
					   WQ_MEM_RECLAIM, 1);
	if (!pf->mbox_pfvf_wq)
		return -ENOMEM;

	/* On CN10K platform, PF <-> VF mailbox region follows after
	 * PF <-> AF mailbox region.
	 */
	if (test_bit(CN10K_MBOX, &pf->hw.cap_flag))
		base = pci_resource_start(pf->pdev, PCI_MBOX_BAR_NUM) +
		       MBOX_SIZE;
	else
		base = readq((void __iomem *)((u64)pf->reg_base +
					      RVU_PF_VF_BAR4_ADDR));

	hwbase = ioremap_wc(base, MBOX_SIZE * pf->total_vfs);
	if (!hwbase) {
		err = -ENOMEM;
		goto free_wq;
	}

	mbox = &pf->mbox_pfvf[0];
	err = otx2_mbox_init(&mbox->mbox, hwbase, pf->pdev, pf->reg_base,
			     MBOX_DIR_PFVF, numvfs);
	if (err)
		goto free_iomem;

	err = otx2_mbox_init(&mbox->mbox_up, hwbase, pf->pdev, pf->reg_base,
			     MBOX_DIR_PFVF_UP, numvfs);
	if (err)
		goto free_iomem;

	for (vf = 0; vf < numvfs; vf++) {
		mbox->pfvf = pf;
		INIT_WORK(&mbox->mbox_wrk, otx2_pfvf_mbox_handler);
		INIT_WORK(&mbox->mbox_up_wrk, otx2_pfvf_mbox_up_handler);
		mbox++;
	}

	return 0;

free_iomem:
	if (hwbase)
		iounmap(hwbase);
free_wq:
	destroy_workqueue(pf->mbox_pfvf_wq);
	return err;
}

static void otx2_pfvf_mbox_destroy(struct otx2_nic *pf)
{
	struct mbox *mbox = &pf->mbox_pfvf[0];

	if (!mbox)
		return;

	if (pf->mbox_pfvf_wq) {
		destroy_workqueue(pf->mbox_pfvf_wq);
		pf->mbox_pfvf_wq = NULL;
	}

	if (mbox->mbox.hwbase)
		iounmap(mbox->mbox.hwbase);

	otx2_mbox_destroy(&mbox->mbox);
}

static void otx2_enable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	/* Clear PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_PF_VFPF_MBOX_INTX(0), ~0ull);
	otx2_write64(pf, RVU_PF_VFPF_MBOX_INTX(1), ~0ull);

	/* Enable PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_PF_VFPF_MBOX_INT_ENA_W1SX(0), INTR_MASK(numvfs));
	if (numvfs > 64) {
		numvfs -= 64;
		otx2_write64(pf, RVU_PF_VFPF_MBOX_INT_ENA_W1SX(1),
			     INTR_MASK(numvfs));
	}
}

static void otx2_disable_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	int vector;

	/* Disable PF <=> VF mailbox IRQ */
	otx2_write64(pf, RVU_PF_VFPF_MBOX_INT_ENA_W1CX(0), ~0ull);
	otx2_write64(pf, RVU_PF_VFPF_MBOX_INT_ENA_W1CX(1), ~0ull);

	otx2_write64(pf, RVU_PF_VFPF_MBOX_INTX(0), ~0ull);
	vector = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFPF_MBOX0);
	free_irq(vector, pf);

	if (numvfs > 64) {
		otx2_write64(pf, RVU_PF_VFPF_MBOX_INTX(1), ~0ull);
		vector = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFPF_MBOX1);
		free_irq(vector, pf);
	}
}

static int otx2_register_pfvf_mbox_intr(struct otx2_nic *pf, int numvfs)
{
	struct otx2_hw *hw = &pf->hw;
	char *irq_name;
	int err;

	/* Register MBOX0 interrupt handler */
	irq_name = &hw->irq_name[RVU_PF_INT_VEC_VFPF_MBOX0 * NAME_SIZE];
	if (pf->pcifunc)
		snprintf(irq_name, NAME_SIZE,
			 "RVUPF%d_VF Mbox0", rvu_get_pf(pf->pcifunc));
	else
		snprintf(irq_name, NAME_SIZE, "RVUPF_VF Mbox0");
	err = request_irq(pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_VFPF_MBOX0),
			  otx2_pfvf_mbox_intr_handler, 0, irq_name, pf);
	if (err) {
		dev_err(pf->dev,
			"RVUPF: IRQ registration failed for PFVF mbox0 irq\n");
		return err;
	}

	if (numvfs > 64) {
		/* Register MBOX1 interrupt handler */
		irq_name = &hw->irq_name[RVU_PF_INT_VEC_VFPF_MBOX1 * NAME_SIZE];
		if (pf->pcifunc)
			snprintf(irq_name, NAME_SIZE,
				 "RVUPF%d_VF Mbox1", rvu_get_pf(pf->pcifunc));
		else
			snprintf(irq_name, NAME_SIZE, "RVUPF_VF Mbox1");
		err = request_irq(pci_irq_vector(pf->pdev,
						 RVU_PF_INT_VEC_VFPF_MBOX1),
						 otx2_pfvf_mbox_intr_handler,
						 0, irq_name, pf);
		if (err) {
			dev_err(pf->dev,
				"RVUPF: IRQ registration failed for PFVF mbox1 irq\n");
			return err;
		}
	}

	otx2_enable_pfvf_mbox_intr(pf, numvfs);

	return 0;
}

static void otx2_process_pfaf_mbox_msg(struct otx2_nic *pf,
				       struct mbox_msghdr *msg)
{
	int devid;

	if (msg->id >= MBOX_MSG_MAX) {
		dev_err(pf->dev,
			"Mbox msg with unknown ID 0x%x\n", msg->id);
		return;
	}

	if (msg->sig != OTX2_MBOX_RSP_SIG) {
		dev_err(pf->dev,
			"Mbox msg with wrong signature %x, ID 0x%x\n",
			 msg->sig, msg->id);
		return;
	}

	/* message response heading VF */
	devid = msg->pcifunc & RVU_PFVF_FUNC_MASK;
	if (devid) {
		struct otx2_vf_config *config = &pf->vf_configs[devid - 1];
		struct delayed_work *dwork;

		switch (msg->id) {
		case MBOX_MSG_NIX_LF_START_RX:
			config->intf_down = false;
			dwork = &config->link_event_work;
			schedule_delayed_work(dwork, msecs_to_jiffies(100));
			break;
		case MBOX_MSG_NIX_LF_STOP_RX:
			config->intf_down = true;
			break;
		}

		return;
	}

	switch (msg->id) {
	case MBOX_MSG_READY:
		pf->pcifunc = msg->pcifunc;
		break;
	case MBOX_MSG_MSIX_OFFSET:
		mbox_handler_msix_offset(pf, (struct msix_offset_rsp *)msg);
		break;
	case MBOX_MSG_NPA_LF_ALLOC:
		mbox_handler_npa_lf_alloc(pf, (struct npa_lf_alloc_rsp *)msg);
		break;
	case MBOX_MSG_NIX_LF_ALLOC:
		mbox_handler_nix_lf_alloc(pf, (struct nix_lf_alloc_rsp *)msg);
		break;
	case MBOX_MSG_NIX_BP_ENABLE:
		mbox_handler_nix_bp_enable(pf, (struct nix_bp_cfg_rsp *)msg);
		break;
	case MBOX_MSG_CGX_STATS:
		mbox_handler_cgx_stats(pf, (struct cgx_stats_rsp *)msg);
		break;
	case MBOX_MSG_CGX_FEC_STATS:
		mbox_handler_cgx_fec_stats(pf, (struct cgx_fec_stats_rsp *)msg);
		break;
	default:
		if (msg->rc)
			dev_err(pf->dev,
				"Mbox msg response has err %d, ID 0x%x\n",
				msg->rc, msg->id);
		break;
	}
}

static void otx2_pfaf_mbox_handler(struct work_struct *work)
{
	struct otx2_mbox_dev *mdev;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;
	struct otx2_mbox *mbox;
	struct mbox *af_mbox;
	struct otx2_nic *pf;
	int offset, id;

	af_mbox = container_of(work, struct mbox, mbox_wrk);
	mbox = &af_mbox->mbox;
	mdev = &mbox->dev[0];
	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);

	offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);
	pf = af_mbox->pfvf;

	for (id = 0; id < af_mbox->num_msgs; id++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + offset);
		otx2_process_pfaf_mbox_msg(pf, msg);
		offset = mbox->rx_start + msg->next_msgoff;
		if (mdev->msgs_acked == (af_mbox->num_msgs - 1))
			__otx2_mbox_reset(mbox, 0);
		mdev->msgs_acked++;
	}

}

static void otx2_handle_link_event(struct otx2_nic *pf)
{
	struct cgx_link_user_info *linfo = &pf->linfo;
	struct net_device *netdev = pf->netdev;

	pr_info("%s NIC Link is %s %d Mbps %s duplex\n", netdev->name,
		linfo->link_up ? "UP" : "DOWN", linfo->speed,
		linfo->full_duplex ? "Full" : "Half");
	if (linfo->link_up) {
		netif_carrier_on(netdev);
		netif_tx_start_all_queues(netdev);
	} else {
		netif_tx_stop_all_queues(netdev);
		netif_carrier_off(netdev);
	}
}

int otx2_mbox_up_handler_mcs_intr_notify(struct otx2_nic *pf,
					 struct mcs_intr_info *event,
					 struct msg_rsp *rsp)
{
	cn10k_handle_mcs_event(pf, event);

	return 0;
}

int otx2_mbox_up_handler_cgx_link_event(struct otx2_nic *pf,
					struct cgx_link_info_msg *msg,
					struct msg_rsp *rsp)
{
	int i;

	/* Copy the link info sent by AF */
	pf->linfo = msg->link_info;

	/* notify VFs about link event */
	for (i = 0; i < pci_num_vf(pf->pdev); i++) {
		struct otx2_vf_config *config = &pf->vf_configs[i];
		struct delayed_work *dwork = &config->link_event_work;

		if (config->intf_down)
			continue;

		schedule_delayed_work(dwork, msecs_to_jiffies(100));
	}

	/* interface has not been fully configured yet */
	if (pf->flags & OTX2_FLAG_INTF_DOWN)
		return 0;

	otx2_handle_link_event(pf);
	return 0;
}

static int otx2_process_mbox_msg_up(struct otx2_nic *pf,
				    struct mbox_msghdr *req)
{
	/* Check if valid, if not reply with a invalid msg */
	if (req->sig != OTX2_MBOX_REQ_SIG) {
		otx2_reply_invalid_msg(&pf->mbox.mbox_up, 0, 0, req->id);
		return -ENODEV;
	}

	switch (req->id) {
#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
	case _id: {							\
		struct _rsp_type *rsp;					\
		int err;						\
									\
		rsp = (struct _rsp_type *)otx2_mbox_alloc_msg(		\
			&pf->mbox.mbox_up, 0,				\
			sizeof(struct _rsp_type));			\
		if (!rsp)						\
			return -ENOMEM;					\
									\
		rsp->hdr.id = _id;					\
		rsp->hdr.sig = OTX2_MBOX_RSP_SIG;			\
		rsp->hdr.pcifunc = 0;					\
		rsp->hdr.rc = 0;					\
									\
		err = otx2_mbox_up_handler_ ## _fn_name(		\
			pf, (struct _req_type *)req, rsp);		\
		return err;						\
	}
MBOX_UP_CGX_MESSAGES
MBOX_UP_MCS_MESSAGES
#undef M
		break;
	default:
		otx2_reply_invalid_msg(&pf->mbox.mbox_up, 0, 0, req->id);
		return -ENODEV;
	}
	return 0;
}

static void otx2_pfaf_mbox_up_handler(struct work_struct *work)
{
	struct mbox *af_mbox = container_of(work, struct mbox, mbox_up_wrk);
	struct otx2_mbox *mbox = &af_mbox->mbox_up;
	struct otx2_mbox_dev *mdev = &mbox->dev[0];
	struct otx2_nic *pf = af_mbox->pfvf;
	int offset, id, devid = 0;
	struct mbox_hdr *rsp_hdr;
	struct mbox_msghdr *msg;

	rsp_hdr = (struct mbox_hdr *)(mdev->mbase + mbox->rx_start);

	offset = mbox->rx_start + ALIGN(sizeof(*rsp_hdr), MBOX_MSG_ALIGN);

	for (id = 0; id < af_mbox->up_num_msgs; id++) {
		msg = (struct mbox_msghdr *)(mdev->mbase + offset);

		devid = msg->pcifunc & RVU_PFVF_FUNC_MASK;
		/* Skip processing VF's messages */
		if (!devid)
			otx2_process_mbox_msg_up(pf, msg);
		offset = mbox->rx_start + msg->next_msgoff;
	}
	if (devid) {
		otx2_forward_vf_mbox_msgs(pf, &pf->mbox.mbox_up,
					  MBOX_DIR_PFVF_UP, devid - 1,
					  af_mbox->up_num_msgs);
		return;
	}

	otx2_mbox_msg_send(mbox, 0);
}

static irqreturn_t otx2_pfaf_mbox_intr_handler(int irq, void *pf_irq)
{
	struct otx2_nic *pf = (struct otx2_nic *)pf_irq;
	struct mbox *mbox;

	/* Clear the IRQ */
	otx2_write64(pf, RVU_PF_INT, BIT_ULL(0));

	mbox = &pf->mbox;

	trace_otx2_msg_interrupt(mbox->mbox.pdev, "AF to PF", BIT_ULL(0));

	otx2_queue_work(mbox, pf->mbox_wq, 0, 1, 1, TYPE_PFAF);

	return IRQ_HANDLED;
}

static void otx2_disable_mbox_intr(struct otx2_nic *pf)
{
	int vector = pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_AFPF_MBOX);

	/* Disable AF => PF mailbox IRQ */
	otx2_write64(pf, RVU_PF_INT_ENA_W1C, BIT_ULL(0));
	free_irq(vector, pf);
}

static int otx2_register_mbox_intr(struct otx2_nic *pf, bool probe_af)
{
	struct otx2_hw *hw = &pf->hw;
	struct msg_req *req;
	char *irq_name;
	int err;

	/* Register mailbox interrupt handler */
	irq_name = &hw->irq_name[RVU_PF_INT_VEC_AFPF_MBOX * NAME_SIZE];
	snprintf(irq_name, NAME_SIZE, "RVUPFAF Mbox");
	err = request_irq(pci_irq_vector(pf->pdev, RVU_PF_INT_VEC_AFPF_MBOX),
			  otx2_pfaf_mbox_intr_handler, 0, irq_name, pf);
	if (err) {
		dev_err(pf->dev,
			"RVUPF: IRQ registration failed for PFAF mbox irq\n");
		return err;
	}

	/* Enable mailbox interrupt for msgs coming from AF.
	 * First clear to avoid spurious interrupts, if any.
	 */
	otx2_write64(pf, RVU_PF_INT, BIT_ULL(0));
	otx2_write64(pf, RVU_PF_INT_ENA_W1S, BIT_ULL(0));

	if (!probe_af)
		return 0;

	/* Check mailbox communication with AF */
	req = otx2_mbox_alloc_msg_ready(&pf->mbox);
	if (!req) {
		otx2_disable_mbox_intr(pf);
		return -ENOMEM;
	}
	err = otx2_sync_mbox_msg(&pf->mbox);
	if (err) {
		dev_warn(pf->dev,
			 "AF not responding to mailbox, deferring probe\n");
		otx2_disable_mbox_intr(pf);
		return -EPROBE_DEFER;
	}

	return 0;
}

static void otx2_pfaf_mbox_destroy(struct otx2_nic *pf)
{
	struct mbox *mbox = &pf->mbox;

	if (pf->mbox_wq) {
		destroy_workqueue(pf->mbox_wq);
		pf->mbox_wq = NULL;
	}

	if (mbox->mbox.hwbase)
		iounmap((void __iomem *)mbox->mbox.hwbase);

	otx2_mbox_destroy(&mbox->mbox);
	otx2_mbox_destroy(&mbox->mbox_up);
}

static int otx2_pfaf_mbox_init(struct otx2_nic *pf)
{
	struct mbox *mbox = &pf->mbox;
	void __iomem *hwbase;
	int err;

	mbox->pfvf = pf;
	pf->mbox_wq = alloc_workqueue("otx2_pfaf_mailbox",
				      WQ_UNBOUND | WQ_HIGHPRI |
				      WQ_MEM_RECLAIM, 1);
	if (!pf->mbox_wq)
		return -ENOMEM;

	/* Mailbox is a reserved memory (in RAM) region shared between
	 * admin function (i.e AF) and this PF, shouldn't be mapped as
	 * device memory to allow unaligned accesses.
	 */
	hwbase = ioremap_wc(pci_resource_start(pf->pdev, PCI_MBOX_BAR_NUM),
			    MBOX_SIZE);
	if (!hwbase) {
		dev_err(pf->dev, "Unable to map PFAF mailbox region\n");
		err = -ENOMEM;
		goto exit;
	}

	err = otx2_mbox_init(&mbox->mbox, hwbase, pf->pdev, pf->reg_base,
			     MBOX_DIR_PFAF, 1);
	if (err)
		goto exit;

	err = otx2_mbox_init(&mbox->mbox_up, hwbase, pf->pdev, pf->reg_base,
			     MBOX_DIR_PFAF_UP, 1);
	if (err)
		goto exit;

	err = otx2_mbox_bbuf_init(mbox, pf->pdev);
	if (err)
		goto exit;

	INIT_WORK(&mbox->mbox_wrk, otx2_pfaf_mbox_handler);
	INIT_WORK(&mbox->mbox_up_wrk, otx2_pfaf_mbox_up_handler);
	mutex_init(&mbox->lock);

	return 0;
exit:
	otx2_pfaf_mbox_destroy(pf);
	return err;
}

static int otx2_cgx_config_linkevents(struct otx2_nic *pf, bool enable)
{
	struct msg_req *msg;
	int err;

	mutex_lock(&pf->mbox.lock);
	if (enable)
		msg = otx2_mbox_alloc_msg_cgx_start_linkevents(&pf->mbox);
	else
		msg = otx2_mbox_alloc_msg_cgx_stop_linkevents(&pf->mbox);

	if (!msg) {
		mutex_unlock(&pf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pf->mbox);
	mutex_unlock(&pf->mbox.lock);
	return err;
}

static int otx2_cgx_config_loopback(struct otx2_nic *pf, bool enable)
{
	struct msg_req *msg;
	int err;

	if (enable && !bitmap_empty(pf->flow_cfg->dmacflt_bmap,
				    pf->flow_cfg->dmacflt_max_flows))
		netdev_warn(pf->netdev,
			    "CGX/RPM internal loopback might not work as DMAC filters are active\n");

	mutex_lock(&pf->mbox.lock);
	if (enable)
		msg = otx2_mbox_alloc_msg_cgx_intlbk_enable(&pf->mbox);
	else
		msg = otx2_mbox_alloc_msg_cgx_intlbk_disable(&pf->mbox);

	if (!msg) {
		mutex_unlock(&pf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pf->mbox);
	mutex_unlock(&pf->mbox.lock);
	return err;
}

int otx2_set_real_num_queues(struct net_device *netdev,
			     int tx_queues, int rx_queues)
{
	int err;

	err = netif_set_real_num_tx_queues(netdev, tx_queues);
	if (err) {
		netdev_err(netdev,
			   "Failed to set no of Tx queues: %d\n", tx_queues);
		return err;
	}

	err = netif_set_real_num_rx_queues(netdev, rx_queues);
	if (err)
		netdev_err(netdev,
			   "Failed to set no of Rx queues: %d\n", rx_queues);
	return err;
}
EXPORT_SYMBOL(otx2_set_real_num_queues);

static char *nix_sqoperr_e_str[NIX_SQOPERR_MAX] = {
	"NIX_SQOPERR_OOR",
	"NIX_SQOPERR_CTX_FAULT",
	"NIX_SQOPERR_CTX_POISON",
	"NIX_SQOPERR_DISABLED",
	"NIX_SQOPERR_SIZE_ERR",
	"NIX_SQOPERR_OFLOW",
	"NIX_SQOPERR_SQB_NULL",
	"NIX_SQOPERR_SQB_FAULT",
	"NIX_SQOPERR_SQE_SZ_ZERO",
};

static char *nix_mnqerr_e_str[NIX_MNQERR_MAX] = {
	"NIX_MNQERR_SQ_CTX_FAULT",
	"NIX_MNQERR_SQ_CTX_POISON",
	"NIX_MNQERR_SQB_FAULT",
	"NIX_MNQERR_SQB_POISON",
	"NIX_MNQERR_TOTAL_ERR",
	"NIX_MNQERR_LSO_ERR",
	"NIX_MNQERR_CQ_QUERY_ERR",
	"NIX_MNQERR_MAX_SQE_SIZE_ERR",
	"NIX_MNQERR_MAXLEN_ERR",
	"NIX_MNQERR_SQE_SIZEM1_ZERO",
};

static char *nix_snd_status_e_str[NIX_SND_STATUS_MAX] =  {
	[NIX_SND_STATUS_GOOD] = "NIX_SND_STATUS_GOOD",
	[NIX_SND_STATUS_SQ_CTX_FAULT] = "NIX_SND_STATUS_SQ_CTX_FAULT",
	[NIX_SND_STATUS_SQ_CTX_POISON] = "NIX_SND_STATUS_SQ_CTX_POISON",
	[NIX_SND_STATUS_SQB_FAULT] = "NIX_SND_STATUS_SQB_FAULT",
	[NIX_SND_STATUS_SQB_POISON] = "NIX_SND_STATUS_SQB_POISON",
	[NIX_SND_STATUS_HDR_ERR] = "NIX_SND_STATUS_HDR_ERR",
	[NIX_SND_STATUS_EXT_ERR] = "NIX_SND_STATUS_EXT_ERR",
	[NIX_SND_STATUS_JUMP_FAULT] = "NIX_SND_STATUS_JUMP_FAULT",
	[NIX_SND_STATUS_JUMP_POISON] = "NIX_SND_STATUS_JUMP_POISON",
	[NIX_SND_STATUS_CRC_ERR] = "NIX_SND_STATUS_CRC_ERR",
	[NIX_SND_STATUS_IMM_ERR] = "NIX_SND_STATUS_IMM_ERR",
	[NIX_SND_STATUS_SG_ERR] = "NIX_SND_STATUS_SG_ERR",
	[NIX_SND_STATUS_MEM_ERR] = "NIX_SND_STATUS_MEM_ERR",
	[NIX_SND_STATUS_INVALID_SUBDC] = "NIX_SND_STATUS_INVALID_SUBDC",
	[NIX_SND_STATUS_SUBDC_ORDER_ERR] = "NIX_SND_STATUS_SUBDC_ORDER_ERR",
	[NIX_SND_STATUS_DATA_FAULT] = "NIX_SND_STATUS_DATA_FAULT",
	[NIX_SND_STATUS_DATA_POISON] = "NIX_SND_STATUS_DATA_POISON",
	[NIX_SND_STATUS_NPC_DROP_ACTION] = "NIX_SND_STATUS_NPC_DROP_ACTION",
	[NIX_SND_STATUS_LOCK_VIOL] = "NIX_SND_STATUS_LOCK_VIOL",
	[NIX_SND_STATUS_NPC_UCAST_CHAN_ERR] = "NIX_SND_STAT_NPC_UCAST_CHAN_ERR",
	[NIX_SND_STATUS_NPC_MCAST_CHAN_ERR] = "NIX_SND_STAT_NPC_MCAST_CHAN_ERR",
	[NIX_SND_STATUS_NPC_MCAST_ABORT] = "NIX_SND_STATUS_NPC_MCAST_ABORT",
	[NIX_SND_STATUS_NPC_VTAG_PTR_ERR] = "NIX_SND_STATUS_NPC_VTAG_PTR_ERR",
	[NIX_SND_STATUS_NPC_VTAG_SIZE_ERR] = "NIX_SND_STATUS_NPC_VTAG_SIZE_ERR",
	[NIX_SND_STATUS_SEND_MEM_FAULT] = "NIX_SND_STATUS_SEND_MEM_FAULT",
	[NIX_SND_STATUS_SEND_STATS_ERR] = "NIX_SND_STATUS_SEND_STATS_ERR",
};

static irqreturn_t otx2_q_intr_handler(int irq, void *data)
{
	struct otx2_nic *pf = data;
	struct otx2_snd_queue *sq;
	u64 val, *ptr;
	u64 qidx = 0;

	/* CQ */
	for (qidx = 0; qidx < pf->qset.cq_cnt; qidx++) {
		ptr = otx2_get_regaddr(pf, NIX_LF_CQ_OP_INT);
		val = otx2_atomic64_add((qidx << 44), ptr);

		otx2_write64(pf, NIX_LF_CQ_OP_INT, (qidx << 44) |
			     (val & NIX_CQERRINT_BITS));
		if (!(val & (NIX_CQERRINT_BITS | BIT_ULL(42))))
			continue;

		if (val & BIT_ULL(42)) {
			netdev_err(pf->netdev,
				   "CQ%lld: error reading NIX_LF_CQ_OP_INT, NIX_LF_ERR_INT 0x%llx\n",
				   qidx, otx2_read64(pf, NIX_LF_ERR_INT));
		} else {
			if (val & BIT_ULL(NIX_CQERRINT_DOOR_ERR))
				netdev_err(pf->netdev, "CQ%lld: Doorbell error",
					   qidx);
			if (val & BIT_ULL(NIX_CQERRINT_CQE_FAULT))
				netdev_err(pf->netdev,
					   "CQ%lld: Memory fault on CQE write to LLC/DRAM",
					   qidx);
		}

		schedule_work(&pf->reset_task);
	}

	/* SQ */
	for (qidx = 0; qidx < otx2_get_total_tx_queues(pf); qidx++) {
		u64 sq_op_err_dbg, mnq_err_dbg, snd_err_dbg;
		u8 sq_op_err_code, mnq_err_code, snd_err_code;

		sq = &pf->qset.sq[qidx];
		if (!sq->sqb_ptrs)
			continue;

		/* Below debug registers captures first errors corresponding to
		 * those registers. We don't have to check against SQ qid as
		 * these are fatal errors.
		 */

		ptr = otx2_get_regaddr(pf, NIX_LF_SQ_OP_INT);
		val = otx2_atomic64_add((qidx << 44), ptr);
		otx2_write64(pf, NIX_LF_SQ_OP_INT, (qidx << 44) |
			     (val & NIX_SQINT_BITS));

		if (val & BIT_ULL(42)) {
			netdev_err(pf->netdev,
				   "SQ%lld: error reading NIX_LF_SQ_OP_INT, NIX_LF_ERR_INT 0x%llx\n",
				   qidx, otx2_read64(pf, NIX_LF_ERR_INT));
			goto done;
		}

		sq_op_err_dbg = otx2_read64(pf, NIX_LF_SQ_OP_ERR_DBG);
		if (!(sq_op_err_dbg & BIT(44)))
			goto chk_mnq_err_dbg;

		sq_op_err_code = FIELD_GET(GENMASK(7, 0), sq_op_err_dbg);
		netdev_err(pf->netdev,
			   "SQ%lld: NIX_LF_SQ_OP_ERR_DBG(0x%llx)  err=%s(%#x)\n",
			   qidx, sq_op_err_dbg,
			   nix_sqoperr_e_str[sq_op_err_code],
			   sq_op_err_code);

		otx2_write64(pf, NIX_LF_SQ_OP_ERR_DBG, BIT_ULL(44));

		if (sq_op_err_code == NIX_SQOPERR_SQB_NULL)
			goto chk_mnq_err_dbg;

		/* Err is not NIX_SQOPERR_SQB_NULL, call aq function to read SQ structure.
		 * TODO: But we are in irq context. How to call mbox functions which does sleep
		 */

chk_mnq_err_dbg:
		mnq_err_dbg = otx2_read64(pf, NIX_LF_MNQ_ERR_DBG);
		if (!(mnq_err_dbg & BIT(44)))
			goto chk_snd_err_dbg;

		mnq_err_code = FIELD_GET(GENMASK(7, 0), mnq_err_dbg);
		netdev_err(pf->netdev,
			   "SQ%lld: NIX_LF_MNQ_ERR_DBG(0x%llx)  err=%s(%#x)\n",
			   qidx, mnq_err_dbg,  nix_mnqerr_e_str[mnq_err_code],
			   mnq_err_code);
		otx2_write64(pf, NIX_LF_MNQ_ERR_DBG, BIT_ULL(44));

chk_snd_err_dbg:
		snd_err_dbg = otx2_read64(pf, NIX_LF_SEND_ERR_DBG);
		if (snd_err_dbg & BIT(44)) {
			snd_err_code = FIELD_GET(GENMASK(7, 0), snd_err_dbg);
			netdev_err(pf->netdev,
				   "SQ%lld: NIX_LF_SND_ERR_DBG:0x%llx err=%s(%#x)\n",
				   qidx, snd_err_dbg,
				   nix_snd_status_e_str[snd_err_code],
				   snd_err_code);
			otx2_write64(pf, NIX_LF_SEND_ERR_DBG, BIT_ULL(44));
		}

done:
		/* Print values and reset */
		if (val & BIT_ULL(NIX_SQINT_SQB_ALLOC_FAIL))
			netdev_err(pf->netdev, "SQ%lld: SQB allocation failed",
				   qidx);

		schedule_work(&pf->reset_task);
	}

	return IRQ_HANDLED;
}

static irqreturn_t otx2_cq_intr_handler(int irq, void *cq_irq)
{
	struct otx2_cq_poll *cq_poll = (struct otx2_cq_poll *)cq_irq;
	struct otx2_nic *pf = (struct otx2_nic *)cq_poll->dev;
	int qidx = cq_poll->cint_idx;

	/* Disable interrupts.
	 *
	 * Completion interrupts behave in a level-triggered interrupt
	 * fashion, and hence have to be cleared only after it is serviced.
	 */
	otx2_write64(pf, NIX_LF_CINTX_ENA_W1C(qidx), BIT_ULL(0));

	/* Schedule NAPI */
	pf->napi_events++;
	napi_schedule_irqoff(&cq_poll->napi);

	return IRQ_HANDLED;
}

static void otx2_disable_napi(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct otx2_cq_poll *cq_poll;
	int qidx;

	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		cq_poll = &qset->napi[qidx];
		cancel_work_sync(&cq_poll->dim.work);
		napi_disable(&cq_poll->napi);
		netif_napi_del(&cq_poll->napi);
	}
}

static void otx2_free_cq_res(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct otx2_cq_queue *cq;
	int qidx;

	/* Disable CQs */
	otx2_ctx_disable(&pf->mbox, NIX_AQ_CTYPE_CQ, false);
	for (qidx = 0; qidx < qset->cq_cnt; qidx++) {
		cq = &qset->cq[qidx];
		qmem_free(pf->dev, cq->cqe);
	}
}

static void otx2_free_sq_res(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct otx2_snd_queue *sq;
	int qidx;

	/* Disable SQs */
	otx2_ctx_disable(&pf->mbox, NIX_AQ_CTYPE_SQ, false);
	/* Free SQB pointers */
	otx2_sq_free_sqbs(pf);
	for (qidx = 0; qidx < otx2_get_total_tx_queues(pf); qidx++) {
		sq = &qset->sq[qidx];
		qmem_free(pf->dev, sq->sqe);
		qmem_free(pf->dev, sq->tso_hdrs);
		kfree(sq->sg);
		kfree(sq->sqb_ptrs);
	}
}

static int otx2_get_rbuf_size(struct otx2_nic *pf, int mtu)
{
	int frame_size;
	int total_size;
	int rbuf_size;

	if (pf->hw.rbuf_len)
		return ALIGN(pf->hw.rbuf_len, OTX2_ALIGN) + OTX2_HEAD_ROOM;

	/* The data transferred by NIX to memory consists of actual packet
	 * plus additional data which has timestamp and/or EDSA/HIGIG2
	 * headers if interface is configured in corresponding modes.
	 * NIX transfers entire data using 6 segments/buffers and writes
	 * a CQE_RX descriptor with those segment addresses. First segment
	 * has additional data prepended to packet. Also software omits a
	 * headroom of 128 bytes in each segment. Hence the total size of
	 * memory needed to receive a packet with 'mtu' is:
	 * frame size =  mtu + additional data;
	 * memory = frame_size + headroom * 6;
	 * each receive buffer size = memory / 6;
	 */
	frame_size = mtu + OTX2_ETH_HLEN + OTX2_HW_TIMESTAMP_LEN;
	total_size = frame_size + OTX2_HEAD_ROOM * 6;
	rbuf_size = total_size / 6;

	return ALIGN(rbuf_size, 2048);
}

static int otx2_init_hw_resources(struct otx2_nic *pf)
{
	struct nix_lf_free_req *free_req;
	struct mbox *mbox = &pf->mbox;
	struct otx2_hw *hw = &pf->hw;
	struct msg_req *req;
	int err = 0, lvl;

	/* Set required NPA LF's pool counts
	 * Auras and Pools are used in a 1:1 mapping,
	 * so, aura count = pool count.
	 */
	hw->rqpool_cnt = hw->rx_queues;
	hw->sqpool_cnt = otx2_get_total_tx_queues(pf);
	hw->pool_cnt = hw->rqpool_cnt + hw->sqpool_cnt;

	/* Maximum hardware supported transmit length */
	pf->tx_max_pktlen = pf->netdev->max_mtu + OTX2_ETH_HLEN;

	pf->rbsize = otx2_get_rbuf_size(pf, pf->netdev->mtu);

	mutex_lock(&mbox->lock);
	/* NPA init */
	err = otx2_config_npa(pf);
	if (err)
		goto exit;

	/* NIX init */
	err = otx2_config_nix(pf);
	if (err)
		goto err_free_npa_lf;

	/* Enable backpressure for CGX mapped PF/VFs */
	if (!is_otx2_lbkvf(pf->pdev))
		otx2_nix_config_bp(pf, true);

	/* Init Auras and pools used by NIX RQ, for free buffer ptrs */
	err = otx2_rq_aura_pool_init(pf);
	if (err) {
		mutex_unlock(&mbox->lock);
		goto err_free_nix_lf;
	}
	/* Init Auras and pools used by NIX SQ, for queueing SQEs */
	err = otx2_sq_aura_pool_init(pf);
	if (err) {
		mutex_unlock(&mbox->lock);
		goto err_free_rq_ptrs;
	}

	err = otx2_txsch_alloc(pf);
	if (err) {
		mutex_unlock(&mbox->lock);
		goto err_free_sq_ptrs;
	}

#ifdef CONFIG_DCB
	if (pf->pfc_en) {
		err = otx2_pfc_txschq_alloc(pf);
		if (err) {
			mutex_unlock(&mbox->lock);
			goto err_free_sq_ptrs;
		}
	}
#endif

	err = otx2_config_nix_queues(pf);
	if (err) {
		mutex_unlock(&mbox->lock);
		goto err_free_txsch;
	}

	for (lvl = 0; lvl < NIX_TXSCH_LVL_CNT; lvl++) {
		err = otx2_txschq_config(pf, lvl, 0, false);
		if (err) {
			mutex_unlock(&mbox->lock);
			goto err_free_nix_queues;
		}
	}

#ifdef CONFIG_DCB
	if (pf->pfc_en) {
		err = otx2_pfc_txschq_config(pf);
		if (err) {
			mutex_unlock(&mbox->lock);
			goto err_free_nix_queues;
		}
	}
#endif

	mutex_unlock(&mbox->lock);
	return err;

err_free_nix_queues:
	otx2_free_sq_res(pf);
	otx2_free_cq_res(pf);
	otx2_ctx_disable(mbox, NIX_AQ_CTYPE_RQ, false);
err_free_txsch:
	otx2_txschq_stop(pf);
err_free_sq_ptrs:
	otx2_sq_free_sqbs(pf);
err_free_rq_ptrs:
	otx2_free_aura_ptr(pf, AURA_NIX_RQ);
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_POOL, true);
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_AURA, true);
	otx2_aura_pool_free(pf);
err_free_nix_lf:
	mutex_lock(&mbox->lock);
	free_req = otx2_mbox_alloc_msg_nix_lf_free(mbox);
	if (free_req) {
		free_req->flags = NIX_LF_DISABLE_FLOWS;
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free nixlf\n", __func__);
	}
err_free_npa_lf:
	/* Reset NPA LF */
	req = otx2_mbox_alloc_msg_npa_lf_free(mbox);
	if (req) {
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free npalf\n", __func__);
	}
exit:
	mutex_unlock(&mbox->lock);
	return err;
}

static void otx2_free_hw_resources(struct otx2_nic *pf)
{
	struct otx2_qset *qset = &pf->qset;
	struct nix_lf_free_req *free_req;
	struct mbox *mbox = &pf->mbox;
	struct otx2_cq_queue *cq;
	struct msg_req *req;
	int qidx;

	/* Ensure all SQE are processed */
	otx2_sqb_flush(pf);

	/* Stop transmission */
	otx2_txschq_stop(pf);

#ifdef CONFIG_DCB
	if (pf->pfc_en)
		otx2_pfc_txschq_stop(pf);
#endif

	mutex_lock(&mbox->lock);
	/* Disable backpressure */
	if (!(pf->pcifunc & RVU_PFVF_FUNC_MASK))
		otx2_nix_config_bp(pf, false);
	mutex_unlock(&mbox->lock);

	/* Disable RQs */
	otx2_ctx_disable(mbox, NIX_AQ_CTYPE_RQ, false);

	/*Dequeue all CQEs */
	for (qidx = 0; qidx < qset->cq_cnt; qidx++) {
		cq = &qset->cq[qidx];
		if (cq->cq_type == CQ_RX)
			otx2_cleanup_rx_cqes(pf, cq);
		else
			otx2_cleanup_tx_cqes(pf, cq);
	}
	otx2_free_pending_sqe(pf);

	otx2_free_sq_res(pf);

	/* Free RQ buffer pointers*/
	otx2_free_aura_ptr(pf, AURA_NIX_RQ);

	otx2_free_cq_res(pf);

	/* Free all ingress bandwidth profiles allocated */
	cn10k_free_all_ipolicers(pf);

	mutex_lock(&mbox->lock);
	/* Reset NIX LF */
	free_req = otx2_mbox_alloc_msg_nix_lf_free(mbox);
	if (free_req) {
		free_req->flags = NIX_LF_DISABLE_FLOWS;
		if (!(pf->flags & OTX2_FLAG_PF_SHUTDOWN))
			free_req->flags |= NIX_LF_DONT_FREE_TX_VTAG;
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free nixlf\n", __func__);
	}
	mutex_unlock(&mbox->lock);

	/* Disable NPA Pool and Aura hw context */
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_POOL, true);
	otx2_ctx_disable(mbox, NPA_AQ_CTYPE_AURA, true);
	otx2_aura_pool_free(pf);

	mutex_lock(&mbox->lock);
	/* Reset NPA LF */
	req = otx2_mbox_alloc_msg_npa_lf_free(mbox);
	if (req) {
		if (otx2_sync_mbox_msg(mbox))
			dev_err(pf->dev, "%s failed to free npalf\n", __func__);
	}
	mutex_unlock(&mbox->lock);
}

static bool otx2_promisc_use_mce_list(struct otx2_nic *pfvf)
{
	int vf;

	/* The AF driver will determine whether to allow the VF netdev or not */
	if (is_otx2_vf(pfvf->pcifunc))
		return true;

	/* check if there are any trusted VFs associated with the PF netdev */
	for (vf = 0; vf < pci_num_vf(pfvf->pdev); vf++)
		if (pfvf->vf_configs[vf].trusted)
			return true;
	return false;
}

static void otx2_do_set_rx_mode(struct otx2_nic *pf)
{
	struct net_device *netdev = pf->netdev;
	struct nix_rx_mode *req;
	bool promisc = false;

	if (!(netdev->flags & IFF_UP))
		return;

	if ((netdev->flags & IFF_PROMISC) ||
	    (netdev_uc_count(netdev) > OTX2_MAX_UNICAST_FLOWS)) {
		promisc = true;
	}

	/* Write unicast address to mcam entries or del from mcam */
	if (!promisc && netdev->priv_flags & IFF_UNICAST_FLT)
		__dev_uc_sync(netdev, otx2_add_macfilter, otx2_del_macfilter);

	mutex_lock(&pf->mbox.lock);
	req = otx2_mbox_alloc_msg_nix_set_rx_mode(&pf->mbox);
	if (!req) {
		mutex_unlock(&pf->mbox.lock);
		return;
	}

	req->mode = NIX_RX_MODE_UCAST;

	if (promisc)
		req->mode |= NIX_RX_MODE_PROMISC;
	if (netdev->flags & (IFF_ALLMULTI | IFF_MULTICAST))
		req->mode |= NIX_RX_MODE_ALLMULTI;

	if (otx2_promisc_use_mce_list(pf))
		req->mode |= NIX_RX_MODE_USE_MCE;

	otx2_sync_mbox_msg(&pf->mbox);
	mutex_unlock(&pf->mbox.lock);
}

static void otx2_set_irq_coalesce(struct otx2_nic *pfvf)
{
	int cint;

	for (cint = 0; cint < pfvf->hw.cint_cnt; cint++)
		otx2_config_irq_coalescing(pfvf, cint);
}

static void otx2_dim_work(struct work_struct *w)
{
	struct dim_cq_moder cur_moder;
	struct otx2_cq_poll *cq_poll;
	struct otx2_nic *pfvf;
	struct dim *dim;

	dim = container_of(w, struct dim, work);
	cur_moder = net_dim_get_rx_moderation(dim->mode, dim->profile_ix);
	cq_poll = container_of(dim, struct otx2_cq_poll, dim);
	pfvf = (struct otx2_nic *)cq_poll->dev;
	pfvf->hw.cq_time_wait = (cur_moder.usec > CQ_TIMER_THRESH_MAX) ?
		CQ_TIMER_THRESH_MAX : cur_moder.usec;
	pfvf->hw.cq_ecount_wait = (cur_moder.pkts > NAPI_POLL_WEIGHT) ?
		NAPI_POLL_WEIGHT : cur_moder.pkts;
	otx2_set_irq_coalesce(pfvf);
	dim->state = DIM_START_MEASURE;
}

int otx2_open(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct otx2_cq_poll *cq_poll = NULL;
	struct otx2_qset *qset = &pf->qset;
	int err = 0, qidx, vec;
	char *irq_name;

	netif_carrier_off(netdev);

	/* RQ and SQs are mapped to different CQs,
	 * so find out max CQ IRQs (i.e CINTs) needed.
	 */
	pf->hw.cint_cnt = max3(pf->hw.rx_queues, pf->hw.tx_queues,
			       pf->hw.tc_tx_queues);

	pf->qset.cq_cnt = pf->hw.rx_queues + otx2_get_total_tx_queues(pf);

	qset->napi = kcalloc(pf->hw.cint_cnt, sizeof(*cq_poll), GFP_KERNEL);
	if (!qset->napi)
		return -ENOMEM;

	/* CQ size of RQ */
	qset->rqe_cnt = qset->rqe_cnt ? qset->rqe_cnt : Q_COUNT(Q_SIZE_256);
	/* CQ size of SQ */
	qset->sqe_cnt = qset->sqe_cnt ? qset->sqe_cnt : Q_COUNT(Q_SIZE_4K);

	err = -ENOMEM;
	qset->cq = kcalloc(pf->qset.cq_cnt,
			   sizeof(struct otx2_cq_queue), GFP_KERNEL);
	if (!qset->cq)
		goto err_free_mem;

	qset->sq = kcalloc(pf->hw.non_qos_queues,
			   sizeof(struct otx2_snd_queue), GFP_KERNEL);
	if (!qset->sq)
		goto err_free_mem;

	qset->rq = kcalloc(pf->hw.rx_queues,
			   sizeof(struct otx2_rcv_queue), GFP_KERNEL);
	if (!qset->rq)
		goto err_free_mem;

	err = otx2_init_hw_resources(pf);
	if (err)
		goto err_free_mem;

	/* Register NAPI handler */
	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		cq_poll = &qset->napi[qidx];
		cq_poll->cint_idx = qidx;
		/* RQ0 & SQ0 are mapped to CINT0 and so on..
		 * 'cq_ids[0]' points to RQ's CQ and
		 * 'cq_ids[1]' points to SQ's CQ and
		 * 'cq_ids[2]' points to XDP's CQ and
		 */
		cq_poll->cq_ids[CQ_RX] =
			(qidx <  pf->hw.rx_queues) ? qidx : CINT_INVALID_CQ;
		cq_poll->cq_ids[CQ_TX] = (qidx < pf->hw.tx_queues) ?
				      qidx + pf->hw.rx_queues : CINT_INVALID_CQ;
		if (pf->xdp_prog)
			cq_poll->cq_ids[CQ_XDP] = (qidx < pf->hw.xdp_queues) ?
						  (qidx + pf->hw.rx_queues +
						  pf->hw.tx_queues) :
						  CINT_INVALID_CQ;
		else
			cq_poll->cq_ids[CQ_XDP] = CINT_INVALID_CQ;

		cq_poll->cq_ids[CQ_QOS] = (qidx < pf->hw.tc_tx_queues) ?
					  (qidx + pf->hw.rx_queues +
					   pf->hw.non_qos_queues) :
					  CINT_INVALID_CQ;

		cq_poll->dev = (void *)pf;
		cq_poll->dim.mode = DIM_CQ_PERIOD_MODE_START_FROM_CQE;
		INIT_WORK(&cq_poll->dim.work, otx2_dim_work);
		netif_napi_add(netdev, &cq_poll->napi, otx2_napi_handler);
		napi_enable(&cq_poll->napi);
	}

	/* Set maximum frame size allowed in HW */
	err = otx2_hw_set_mtu(pf, netdev->mtu);
	if (err)
		goto err_disable_napi;

	/* Setup segmentation algorithms, if failed, clear offload capability */
	otx2_setup_segmentation(pf);

	/* Initialize RSS */
	err = otx2_rss_init(pf);
	if (err)
		goto err_disable_napi;

	/* Register Queue IRQ handlers */
	vec = pf->hw.nix_msixoff + NIX_LF_QINT_VEC_START;
	irq_name = &pf->hw.irq_name[vec * NAME_SIZE];

	snprintf(irq_name, NAME_SIZE, "%s-qerr", pf->netdev->name);

	err = request_irq(pci_irq_vector(pf->pdev, vec),
			  otx2_q_intr_handler, 0, irq_name, pf);
	if (err) {
		dev_err(pf->dev,
			"RVUPF%d: IRQ registration failed for QERR\n",
			rvu_get_pf(pf->pcifunc));
		goto err_disable_napi;
	}

	/* Enable QINT IRQ */
	otx2_write64(pf, NIX_LF_QINTX_ENA_W1S(0), BIT_ULL(0));

	/* Register CQ IRQ handlers */
	vec = pf->hw.nix_msixoff + NIX_LF_CINT_VEC_START;
	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		irq_name = &pf->hw.irq_name[vec * NAME_SIZE];

		snprintf(irq_name, NAME_SIZE, "%s-rxtx-%d", pf->netdev->name,
			 qidx);

		err = request_irq(pci_irq_vector(pf->pdev, vec),
				  otx2_cq_intr_handler, 0, irq_name,
				  &qset->napi[qidx]);
		if (err) {
			dev_err(pf->dev,
				"RVUPF%d: IRQ registration failed for CQ%d\n",
				rvu_get_pf(pf->pcifunc), qidx);
			goto err_free_cints;
		}
		vec++;

		otx2_config_irq_coalescing(pf, qidx);

		/* Enable CQ IRQ */
		otx2_write64(pf, NIX_LF_CINTX_INT(qidx), BIT_ULL(0));
		otx2_write64(pf, NIX_LF_CINTX_ENA_W1S(qidx), BIT_ULL(0));
	}

	otx2_set_cints_affinity(pf);

	if (pf->flags & OTX2_FLAG_RX_VLAN_SUPPORT)
		otx2_enable_rxvlan(pf, true);

	/* When reinitializing enable time stamping if it is enabled before */
	if (pf->flags & OTX2_FLAG_TX_TSTAMP_ENABLED) {
		pf->flags &= ~OTX2_FLAG_TX_TSTAMP_ENABLED;
		otx2_config_hw_tx_tstamp(pf, true);
	}
	if (pf->flags & OTX2_FLAG_RX_TSTAMP_ENABLED) {
		pf->flags &= ~OTX2_FLAG_RX_TSTAMP_ENABLED;
		otx2_config_hw_rx_tstamp(pf, true);
	}

	pf->flags &= ~OTX2_FLAG_INTF_DOWN;
	/* 'intf_down' may be checked on any cpu */
	smp_wmb();

	/* we have already received link status notification */
	if (pf->linfo.link_up && !(pf->pcifunc & RVU_PFVF_FUNC_MASK))
		otx2_handle_link_event(pf);

	/* Install DMAC Filters */
	if (pf->flags & OTX2_FLAG_DMACFLTR_SUPPORT)
		otx2_dmacflt_reinstall_flows(pf);

	otx2_tc_apply_ingress_police_rules(pf);

	err = otx2_rxtx_enable(pf, true);
	/* If a mbox communication error happens at this point then interface
	 * will end up in a state such that it is in down state but hardware
	 * mcam entries are enabled to receive the packets. Hence disable the
	 * packet I/O.
	 */
	if (err == EIO)
		goto err_disable_rxtx;
	else if (err)
		goto err_tx_stop_queues;

	otx2_do_set_rx_mode(pf);

	return 0;

err_disable_rxtx:
	otx2_rxtx_enable(pf, false);
err_tx_stop_queues:
	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);
	pf->flags |= OTX2_FLAG_INTF_DOWN;
err_free_cints:
	otx2_free_cints(pf, qidx);
	vec = pci_irq_vector(pf->pdev,
			     pf->hw.nix_msixoff + NIX_LF_QINT_VEC_START);
	otx2_write64(pf, NIX_LF_QINTX_ENA_W1C(0), BIT_ULL(0));
	free_irq(vec, pf);
err_disable_napi:
	otx2_disable_napi(pf);
	otx2_free_hw_resources(pf);
err_free_mem:
	kfree(qset->sq);
	kfree(qset->cq);
	kfree(qset->rq);
	kfree(qset->napi);
	return err;
}
EXPORT_SYMBOL(otx2_open);

int otx2_stop(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct otx2_cq_poll *cq_poll = NULL;
	struct otx2_qset *qset = &pf->qset;
	struct otx2_rss_info *rss;
	int qidx, vec, wrk;

	/* If the DOWN flag is set resources are already freed */
	if (pf->flags & OTX2_FLAG_INTF_DOWN)
		return 0;

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	pf->flags |= OTX2_FLAG_INTF_DOWN;
	/* 'intf_down' may be checked on any cpu */
	smp_wmb();

	/* First stop packet Rx/Tx */
	otx2_rxtx_enable(pf, false);

	/* Clear RSS enable flag */
	rss = &pf->hw.rss_info;
	rss->enable = false;
	if (!netif_is_rxfh_configured(netdev))
		kfree(rss->rss_ctx[DEFAULT_RSS_CONTEXT_GROUP]);

	/* Cleanup Queue IRQ */
	vec = pci_irq_vector(pf->pdev,
			     pf->hw.nix_msixoff + NIX_LF_QINT_VEC_START);
	otx2_write64(pf, NIX_LF_QINTX_ENA_W1C(0), BIT_ULL(0));
	free_irq(vec, pf);

	/* Cleanup CQ NAPI and IRQ */
	vec = pf->hw.nix_msixoff + NIX_LF_CINT_VEC_START;
	for (qidx = 0; qidx < pf->hw.cint_cnt; qidx++) {
		/* Disable interrupt */
		otx2_write64(pf, NIX_LF_CINTX_ENA_W1C(qidx), BIT_ULL(0));

		synchronize_irq(pci_irq_vector(pf->pdev, vec));

		cq_poll = &qset->napi[qidx];
		napi_synchronize(&cq_poll->napi);
		vec++;
	}

	netif_tx_disable(netdev);

	otx2_free_hw_resources(pf);
	otx2_free_cints(pf, pf->hw.cint_cnt);
	otx2_disable_napi(pf);

	for (qidx = 0; qidx < netdev->num_tx_queues; qidx++)
		netdev_tx_reset_queue(netdev_get_tx_queue(netdev, qidx));

	for (wrk = 0; wrk < pf->qset.cq_cnt; wrk++)
		cancel_delayed_work_sync(&pf->refill_wrk[wrk].pool_refill_work);
	devm_kfree(pf->dev, pf->refill_wrk);

	kfree(qset->sq);
	kfree(qset->cq);
	kfree(qset->rq);
	kfree(qset->napi);
	/* Do not clear RQ/SQ ringsize settings */
	memset_startat(qset, 0, sqe_cnt);
	return 0;
}
EXPORT_SYMBOL(otx2_stop);

static netdev_tx_t otx2_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	int qidx = skb_get_queue_mapping(skb);
	struct otx2_snd_queue *sq;
	struct netdev_queue *txq;
	int sq_idx;

	/* XDP SQs are not mapped with TXQs
	 * advance qid to derive correct sq mapped with QOS
	 */
	sq_idx = (qidx >= pf->hw.tx_queues) ? (qidx + pf->hw.xdp_queues) : qidx;

	/* Check for minimum and maximum packet length */
	if (skb->len <= ETH_HLEN ||
	    (!skb_shinfo(skb)->gso_size && skb->len > pf->tx_max_pktlen)) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	sq = &pf->qset.sq[sq_idx];
	txq = netdev_get_tx_queue(netdev, qidx);

	if (!otx2_sq_append_skb(netdev, sq, skb, qidx)) {
		netif_tx_stop_queue(txq);

		/* Check again, incase SQBs got freed up */
		smp_mb();
		if (((sq->num_sqbs - *sq->aura_fc_addr) * sq->sqe_per_sqb)
							> sq->sqe_thresh)
			netif_tx_wake_queue(txq);

		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

u16 otx2_select_queue(struct net_device *netdev, struct sk_buff *skb,
		      struct net_device *sb_dev)
{
#ifdef CONFIG_DCB
	struct otx2_nic *pf = netdev_priv(netdev);
	u8 vlan_prio;
#endif

#ifdef CONFIG_DCB
	if (!skb->vlan_present)
		goto pick_tx;

	vlan_prio = skb->vlan_tci >> 13;
	if ((vlan_prio > pf->hw.tx_queues - 1) ||
	    !pf->pfc_alloc_status[vlan_prio])
		goto pick_tx;

	return vlan_prio;

pick_tx:
#endif
	return netdev_pick_tx(netdev, skb, NULL);
}
EXPORT_SYMBOL(otx2_select_queue);

static netdev_features_t otx2_fix_features(struct net_device *dev,
					   netdev_features_t features)
{
	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		features |= NETIF_F_HW_VLAN_STAG_RX;
	else
		features &= ~NETIF_F_HW_VLAN_STAG_RX;

	return features;
}

static void otx2_set_rx_mode(struct net_device *netdev)
{
	struct otx2_nic *pf = netdev_priv(netdev);

	queue_work(pf->otx2_wq, &pf->rx_mode_work);
}

static void otx2_rx_mode_wrk_handler(struct work_struct *work)
{
	struct otx2_nic *pf = container_of(work, struct otx2_nic, rx_mode_work);

	otx2_do_set_rx_mode(pf);
}

static int otx2_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	netdev_features_t changed = features ^ netdev->features;
	struct otx2_nic *pf = netdev_priv(netdev);

	if ((changed & NETIF_F_LOOPBACK) && netif_running(netdev))
		return otx2_cgx_config_loopback(pf,
						features & NETIF_F_LOOPBACK);

	if ((changed & NETIF_F_HW_VLAN_CTAG_RX) && netif_running(netdev))
		return otx2_enable_rxvlan(pf,
					  features & NETIF_F_HW_VLAN_CTAG_RX);

	return otx2_handle_ntuple_tc_features(netdev, features);
}

static void otx2_reset_task(struct work_struct *work)
{
	struct otx2_nic *pf = container_of(work, struct otx2_nic, reset_task);

	if (!netif_running(pf->netdev))
		return;

	rtnl_lock();
	otx2_stop(pf->netdev);
	pf->reset_count++;
	otx2_open(pf->netdev);
	netif_trans_update(pf->netdev);
	rtnl_unlock();
}

static int otx2_config_hw_rx_tstamp(struct otx2_nic *pfvf, bool enable)
{
	struct msg_req *req;
	int err;

	if (pfvf->flags & OTX2_FLAG_RX_TSTAMP_ENABLED && enable)
		return 0;

	mutex_lock(&pfvf->mbox.lock);
	if (enable)
		req = otx2_mbox_alloc_msg_cgx_ptp_rx_enable(&pfvf->mbox);
	else
		req = otx2_mbox_alloc_msg_cgx_ptp_rx_disable(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		mutex_unlock(&pfvf->mbox.lock);
		return err;
	}

	mutex_unlock(&pfvf->mbox.lock);
	if (enable)
		pfvf->flags |= OTX2_FLAG_RX_TSTAMP_ENABLED;
	else
		pfvf->flags &= ~OTX2_FLAG_RX_TSTAMP_ENABLED;
	return 0;
}

static int otx2_config_hw_tx_tstamp(struct otx2_nic *pfvf, bool enable)
{
	struct msg_req *req;
	int err;

	if (pfvf->flags & OTX2_FLAG_TX_TSTAMP_ENABLED && enable)
		return 0;

	mutex_lock(&pfvf->mbox.lock);
	if (enable)
		req = otx2_mbox_alloc_msg_nix_lf_ptp_tx_enable(&pfvf->mbox);
	else
		req = otx2_mbox_alloc_msg_nix_lf_ptp_tx_disable(&pfvf->mbox);
	if (!req) {
		mutex_unlock(&pfvf->mbox.lock);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		mutex_unlock(&pfvf->mbox.lock);
		return err;
	}

	mutex_unlock(&pfvf->mbox.lock);
	if (enable)
		pfvf->flags |= OTX2_FLAG_TX_TSTAMP_ENABLED;
	else
		pfvf->flags &= ~OTX2_FLAG_TX_TSTAMP_ENABLED;
	return 0;
}

int otx2_config_hwtstamp(struct net_device *netdev, struct ifreq *ifr)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	struct hwtstamp_config config;

	if (!pfvf->ptp)
		return -ENODEV;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		if (pfvf->flags & OTX2_FLAG_PTP_ONESTEP_SYNC)
			pfvf->flags &= ~OTX2_FLAG_PTP_ONESTEP_SYNC;

		cancel_delayed_work(&pfvf->ptp->synctstamp_work);
		otx2_config_hw_tx_tstamp(pfvf, false);
		break;
	case HWTSTAMP_TX_ONESTEP_SYNC:
		if (!test_bit(CN10K_PTP_ONESTEP, &pfvf->hw.cap_flag))
			return -ERANGE;
		pfvf->flags |= OTX2_FLAG_PTP_ONESTEP_SYNC;
		schedule_delayed_work(&pfvf->ptp->synctstamp_work,
				      msecs_to_jiffies(500));
		fallthrough;
	case HWTSTAMP_TX_ON:
		otx2_config_hw_tx_tstamp(pfvf, true);
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		otx2_config_hw_rx_tstamp(pfvf, false);
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		otx2_config_hw_rx_tstamp(pfvf, true);
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	memcpy(&pfvf->tstamp, &config, sizeof(config));

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}
EXPORT_SYMBOL(otx2_config_hwtstamp);

int otx2_ioctl(struct net_device *netdev, struct ifreq *req, int cmd)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	struct hwtstamp_config *cfg = &pfvf->tstamp;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return otx2_config_hwtstamp(netdev, req);
	case SIOCGHWTSTAMP:
		return copy_to_user(req->ifr_data, cfg,
				    sizeof(*cfg)) ? -EFAULT : 0;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(otx2_ioctl);

static int otx2_do_set_vf_mac(struct otx2_nic *pf, int vf, const u8 *mac)
{
	struct npc_install_flow_req *req;
	int err;

	mutex_lock(&pf->mbox.lock);
	req = otx2_mbox_alloc_msg_npc_install_flow(&pf->mbox);
	if (!req) {
		err = -ENOMEM;
		goto out;
	}

	ether_addr_copy(req->packet.dmac, mac);
	eth_broadcast_addr((u8 *)&req->mask.dmac);
	req->features = BIT_ULL(NPC_DMAC);
	req->channel = pf->hw.rx_chan_base;
	req->intf = NIX_INTF_RX;
	req->default_rule = 1;
	req->append = 1;
	req->vf = vf + 1;
	req->op = NIX_RX_ACTION_DEFAULT;

	err = otx2_sync_mbox_msg(&pf->mbox);
out:
	mutex_unlock(&pf->mbox.lock);
	return err;
}

static int otx2_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct pci_dev *pdev = pf->pdev;
	struct otx2_vf_config *config;
	int ret;

	if (!netif_running(netdev))
		return -EAGAIN;

	if (vf >= pf->total_vfs)
		return -EINVAL;

	if (!is_valid_ether_addr(mac))
		return -EINVAL;

	config = &pf->vf_configs[vf];
	ether_addr_copy(config->mac, mac);

	ret = otx2_do_set_vf_mac(pf, vf, mac);
	if (ret == 0)
		dev_info(&pdev->dev,
			 "Load/Reload VF driver\n");

	return ret;
}

static int otx2_do_set_vf_vlan(struct otx2_nic *pf, int vf, u16 vlan, u8 qos,
			       __be16 proto)
{
	struct otx2_flow_config *flow_cfg = pf->flow_cfg;
	struct nix_vtag_config_rsp *vtag_rsp;
	struct npc_delete_flow_req *del_req;
	struct nix_vtag_config *vtag_req;
	struct npc_install_flow_req *req;
	struct otx2_vf_config *config;
	int err = 0;
	u32 idx;

	config = &pf->vf_configs[vf];

	if (!vlan && !config->vlan)
		goto out;

	mutex_lock(&pf->mbox.lock);

	/* free old tx vtag entry */
	if (config->vlan) {
		vtag_req = otx2_mbox_alloc_msg_nix_vtag_cfg(&pf->mbox);
		if (!vtag_req) {
			err = -ENOMEM;
			goto out;
		}
		vtag_req->cfg_type = 0;
		vtag_req->tx.free_vtag0 = 1;
		vtag_req->tx.vtag0_idx = config->tx_vtag_idx;

		err = otx2_sync_mbox_msg(&pf->mbox);
		if (err)
			goto out;
	}

	if (!vlan && config->vlan) {
		/* rx */
		del_req = otx2_mbox_alloc_msg_npc_delete_flow(&pf->mbox);
		if (!del_req) {
			err = -ENOMEM;
			goto out;
		}
		idx = ((vf * OTX2_PER_VF_VLAN_FLOWS) + OTX2_VF_VLAN_RX_INDEX);
		del_req->entry =
			flow_cfg->def_ent[flow_cfg->vf_vlan_offset + idx];
		err = otx2_sync_mbox_msg(&pf->mbox);
		if (err)
			goto out;

		/* tx */
		del_req = otx2_mbox_alloc_msg_npc_delete_flow(&pf->mbox);
		if (!del_req) {
			err = -ENOMEM;
			goto out;
		}
		idx = ((vf * OTX2_PER_VF_VLAN_FLOWS) + OTX2_VF_VLAN_TX_INDEX);
		del_req->entry =
			flow_cfg->def_ent[flow_cfg->vf_vlan_offset + idx];
		err = otx2_sync_mbox_msg(&pf->mbox);

		goto out;
	}

	/* rx */
	req = otx2_mbox_alloc_msg_npc_install_flow(&pf->mbox);
	if (!req) {
		err = -ENOMEM;
		goto out;
	}

	idx = ((vf * OTX2_PER_VF_VLAN_FLOWS) + OTX2_VF_VLAN_RX_INDEX);
	req->entry = flow_cfg->def_ent[flow_cfg->vf_vlan_offset + idx];
	req->packet.vlan_tci = htons(vlan);
	req->mask.vlan_tci = htons(VLAN_VID_MASK);
	/* af fills the destination mac addr */
	eth_broadcast_addr((u8 *)&req->mask.dmac);
	req->features = BIT_ULL(NPC_OUTER_VID) | BIT_ULL(NPC_DMAC);
	req->channel = pf->hw.rx_chan_base;
	req->intf = NIX_INTF_RX;
	req->vf = vf + 1;
	req->op = NIX_RX_ACTION_DEFAULT;
	req->vtag0_valid = true;
	req->vtag0_type = NIX_AF_LFX_RX_VTAG_TYPE7;
	req->set_cntr = 1;

	err = otx2_sync_mbox_msg(&pf->mbox);
	if (err)
		goto out;

	/* tx */
	vtag_req = otx2_mbox_alloc_msg_nix_vtag_cfg(&pf->mbox);
	if (!vtag_req) {
		err = -ENOMEM;
		goto out;
	}

	/* configure tx vtag params */
	vtag_req->vtag_size = VTAGSIZE_T4;
	vtag_req->cfg_type = 0; /* tx vlan cfg */
	vtag_req->tx.cfg_vtag0 = 1;
	vtag_req->tx.vtag0 = ((u64)ntohs(proto) << 16) | vlan;

	err = otx2_sync_mbox_msg(&pf->mbox);
	if (err)
		goto out;

	vtag_rsp = (struct nix_vtag_config_rsp *)otx2_mbox_get_rsp
			(&pf->mbox.mbox, 0, &vtag_req->hdr);
	if (IS_ERR(vtag_rsp)) {
		err = PTR_ERR(vtag_rsp);
		goto out;
	}
	config->tx_vtag_idx = vtag_rsp->vtag0_idx;

	req = otx2_mbox_alloc_msg_npc_install_flow(&pf->mbox);
	if (!req) {
		err = -ENOMEM;
		goto out;
	}

	eth_zero_addr((u8 *)&req->mask.dmac);
	idx = ((vf * OTX2_PER_VF_VLAN_FLOWS) + OTX2_VF_VLAN_TX_INDEX);
	req->entry = flow_cfg->def_ent[flow_cfg->vf_vlan_offset + idx];
	req->features = BIT_ULL(NPC_DMAC);
	req->channel = pf->hw.tx_chan_base;
	req->intf = NIX_INTF_TX;
	req->vf = vf + 1;
	req->op = NIX_TX_ACTIONOP_UCAST_DEFAULT;
	req->vtag0_def = vtag_rsp->vtag0_idx;
	req->vtag0_op = VTAG_INSERT;
	req->set_cntr = 1;

	err = otx2_sync_mbox_msg(&pf->mbox);
out:
	config->vlan = vlan;
	mutex_unlock(&pf->mbox.lock);
	return err;
}

static int otx2_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos,
			    __be16 proto)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct pci_dev *pdev = pf->pdev;

	if (!netif_running(netdev))
		return -EAGAIN;

	if (vf >= pci_num_vf(pdev))
		return -EINVAL;

	/* qos is currently unsupported */
	if (vlan >= VLAN_N_VID || qos)
		return -EINVAL;

	if (proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;

	if (!(pf->flags & OTX2_FLAG_VF_VLAN_SUPPORT))
		return -EOPNOTSUPP;

	return otx2_do_set_vf_vlan(pf, vf, vlan, qos, proto);
}

static int otx2_get_vf_config(struct net_device *netdev, int vf,
			      struct ifla_vf_info *ivi)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct pci_dev *pdev = pf->pdev;
	struct otx2_vf_config *config;

	if (!netif_running(netdev))
		return -EAGAIN;

	if (vf >= pci_num_vf(pdev))
		return -EINVAL;

	config = &pf->vf_configs[vf];
	ivi->vf = vf;
	ether_addr_copy(ivi->mac, config->mac);
	ivi->vlan = config->vlan;
	ivi->trusted = config->trusted;

	return 0;
}

static int otx2_xdp_xmit_tx(struct otx2_nic *pf, struct xdp_frame *xdpf,
			    int qidx)
{
	struct page *page;
	u64 dma_addr;
	int err = 0;

	dma_addr = otx2_dma_map_page(pf, virt_to_page(xdpf->data),
				     offset_in_page(xdpf->data), xdpf->len,
				     DMA_TO_DEVICE);
	if (dma_mapping_error(pf->dev, dma_addr))
		return -ENOMEM;

	err = otx2_xdp_sq_append_pkt(pf, dma_addr, xdpf->len, qidx);
	if (!err) {
		otx2_dma_unmap_page(pf, dma_addr, xdpf->len, DMA_TO_DEVICE);
		page = virt_to_page(xdpf->data);
		put_page(page);
		return -ENOMEM;
	}
	return 0;
}

static int otx2_xdp_xmit(struct net_device *netdev, int n,
			 struct xdp_frame **frames, u32 flags)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	int qidx = smp_processor_id();
	struct otx2_snd_queue *sq;
	int drops = 0, i;

	if (!netif_running(netdev))
		return -ENETDOWN;

	qidx += pf->hw.tx_queues;
	sq = pf->xdp_prog ? &pf->qset.sq[qidx] : NULL;

	/* Abort xmit if xdp queue is not */
	if (unlikely(!sq))
		return -ENXIO;

	if (unlikely(flags & ~XDP_XMIT_FLAGS_MASK))
		return -EINVAL;

	for (i = 0; i < n; i++) {
		struct xdp_frame *xdpf = frames[i];
		int err;

		err = otx2_xdp_xmit_tx(pf, xdpf, qidx);
		if (err)
			drops++;
	}
	return n - drops;
}

static int otx2_xdp_setup(struct otx2_nic *pf, struct bpf_prog *prog)
{
	struct net_device *dev = pf->netdev;
	bool if_up = netif_running(pf->netdev);
	struct bpf_prog *old_prog;

	if (prog && dev->mtu > MAX_XDP_MTU) {
		netdev_warn(dev, "Jumbo frames not yet supported with XDP\n");
		return -EOPNOTSUPP;
	}

	if (if_up)
		otx2_stop(pf->netdev);

	old_prog = xchg(&pf->xdp_prog, prog);

	if (old_prog)
		bpf_prog_put(old_prog);

	if (pf->xdp_prog)
		bpf_prog_add(pf->xdp_prog, pf->hw.rx_queues - 1);

	/* Network stack and XDP shared same rx queues.
	 * Use separate tx queues for XDP and network stack.
	 */
	if (pf->xdp_prog)
		pf->hw.xdp_queues = pf->hw.rx_queues;
	else
		pf->hw.xdp_queues = 0;

	pf->hw.non_qos_queues += pf->hw.xdp_queues;

	if (if_up)
		otx2_open(pf->netdev);

	return 0;
}

static int otx2_xdp(struct net_device *netdev, struct netdev_bpf *xdp)
{
	struct otx2_nic *pf = netdev_priv(netdev);

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return otx2_xdp_setup(pf, xdp->prog);
	default:
		return -EINVAL;
	}
}

static int otx2_set_vf_permissions(struct otx2_nic *pf, int vf,
				   int req_perm)
{
	struct set_vf_perm *req;
	int rc;

	mutex_lock(&pf->mbox.lock);
	req = otx2_mbox_alloc_msg_set_vf_perm(&pf->mbox);
	if (!req) {
		rc = -ENOMEM;
		goto out;
	}

	/* Let AF reset VF permissions as sriov is disabled */
	if (req_perm == OTX2_RESET_VF_PERM) {
		req->flags |= RESET_VF_PERM;
	} else if (req_perm == OTX2_TRUSTED_VF) {
		if (pf->vf_configs[vf].trusted)
			req->flags |= VF_TRUSTED;
	}

	req->vf = vf;
	rc = otx2_sync_mbox_msg(&pf->mbox);
out:
	mutex_unlock(&pf->mbox.lock);
	return rc;
}

static int otx2_ndo_set_vf_trust(struct net_device *netdev, int vf,
				 bool enable)
{
	struct otx2_nic *pf = netdev_priv(netdev);
	struct pci_dev *pdev = pf->pdev;
	int rc;

	if (vf >= pci_num_vf(pdev))
		return -EINVAL;

	if (pf->vf_configs[vf].trusted == enable)
		return 0;

	pf->vf_configs[vf].trusted = enable;
	rc = otx2_set_vf_permissions(pf, vf, OTX2_TRUSTED_VF);

	if (rc) {
		pf->vf_configs[vf].trusted = !enable;
	} else {
		netdev_info(pf->netdev, "VF %d is %strusted\n",
			    vf, enable ? "" : "not ");
		otx2_set_rx_mode(netdev);
	}

	return rc;
}

static const struct net_device_ops otx2_netdev_ops = {
	.ndo_open		= otx2_open,
	.ndo_stop		= otx2_stop,
	.ndo_start_xmit		= otx2_xmit,
	.ndo_select_queue	= otx2_select_queue,
	.ndo_fix_features	= otx2_fix_features,
	.ndo_set_mac_address    = otx2_set_mac_address,
	.ndo_change_mtu		= otx2_change_mtu,
	.ndo_set_rx_mode	= otx2_set_rx_mode,
	.ndo_set_features	= otx2_set_features,
	.ndo_tx_timeout		= otx2_tx_timeout,
	.ndo_get_stats64	= otx2_get_stats64,
	.ndo_eth_ioctl		= otx2_ioctl,
	.ndo_set_vf_mac		= otx2_set_vf_mac,
	.ndo_set_vf_vlan	= otx2_set_vf_vlan,
	.ndo_get_vf_config	= otx2_get_vf_config,
	.ndo_bpf		= otx2_xdp,
	.ndo_xdp_xmit           = otx2_xdp_xmit,
	.ndo_setup_tc		= otx2_setup_tc,
	.ndo_set_vf_trust	= otx2_ndo_set_vf_trust,
};

static int otx2_wq_init(struct otx2_nic *pf)
{
	pf->otx2_wq = create_singlethread_workqueue("otx2_wq");
	if (!pf->otx2_wq)
		return -ENOMEM;

	INIT_WORK(&pf->rx_mode_work, otx2_rx_mode_wrk_handler);
	INIT_WORK(&pf->reset_task, otx2_reset_task);
	return 0;
}

static int otx2_check_pf_usable(struct otx2_nic *nic)
{
	u64 rev;

	rev = otx2_read64(nic, RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_RVUM));
	rev = (rev >> 12) & 0xFF;
	/* Check if AF has setup revision for RVUM block,
	 * otherwise this driver probe should be deferred
	 * until AF driver comes up.
	 */
	if (!rev) {
		dev_warn(nic->dev,
			 "AF is not initialized, deferring probe\n");
		return -EPROBE_DEFER;
	}
	return 0;
}

static int otx2_realloc_msix_vectors(struct otx2_nic *pf)
{
	struct otx2_hw *hw = &pf->hw;
	int num_vec, err;

	/* NPA interrupts are inot registered, so alloc only
	 * upto NIX vector offset.
	 */
	num_vec = hw->nix_msixoff;
	num_vec += NIX_LF_CINT_VEC_START + hw->max_queues;

	otx2_disable_mbox_intr(pf);
	pci_free_irq_vectors(hw->pdev);
	err = pci_alloc_irq_vectors(hw->pdev, num_vec, num_vec, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(pf->dev, "%s: Failed to realloc %d IRQ vectors\n",
			__func__, num_vec);
		return err;
	}

	return otx2_register_mbox_intr(pf, false);
}

static int otx2_sriov_vfcfg_init(struct otx2_nic *pf)
{
	int i;

	pf->vf_configs = devm_kcalloc(pf->dev, pf->total_vfs,
				      sizeof(struct otx2_vf_config),
				      GFP_KERNEL);
	if (!pf->vf_configs)
		return -ENOMEM;

	for (i = 0; i < pf->total_vfs; i++) {
		pf->vf_configs[i].pf = pf;
		pf->vf_configs[i].intf_down = true;
		pf->vf_configs[i].trusted = false;
		INIT_DELAYED_WORK(&pf->vf_configs[i].link_event_work,
				  otx2_vf_link_event_task);
	}

	return 0;
}

static void otx2_sriov_vfcfg_cleanup(struct otx2_nic *pf)
{
	int i;

	if (!pf->vf_configs)
		return;

	for (i = 0; i < pf->total_vfs; i++) {
		cancel_delayed_work_sync(&pf->vf_configs[i].link_event_work);
		otx2_set_vf_permissions(pf, i, OTX2_RESET_VF_PERM);
	}
}

static int otx2_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	int err, qcount, qos_txqs;
	struct net_device *netdev;
	struct otx2_nic *pf;
	struct otx2_hw *hw;
	int num_vec;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "DMA mask config failed, abort\n");
		goto err_release_regions;
	}

	pci_set_master(pdev);

	/* Set number of queues */
	qcount = min_t(int, num_online_cpus(), OTX2_MAX_CQ_CNT);
	qos_txqs = min_t(int, qcount, OTX2_QOS_MAX_LEAF_NODES);

	netdev = alloc_etherdev_mqs(sizeof(*pf), qcount + qos_txqs, qcount);
	if (!netdev) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	pci_set_drvdata(pdev, netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);
	pf = netdev_priv(netdev);
	pf->netdev = netdev;
	pf->pdev = pdev;
	pf->dev = dev;
	pf->total_vfs = pci_sriov_get_totalvfs(pdev);
	pf->flags |= OTX2_FLAG_INTF_DOWN;

	hw = &pf->hw;
	hw->pdev = pdev;
	hw->rx_queues = qcount;
	hw->tx_queues = qcount;
	hw->non_qos_queues = qcount;
	hw->max_queues = qcount;
	hw->rbuf_len = OTX2_DEFAULT_RBUF_LEN;
	/* Use CQE of 128 byte descriptor size by default */
	hw->xqe_size = 128;

	num_vec = pci_msix_vec_count(pdev);
	hw->irq_name = devm_kmalloc_array(&hw->pdev->dev, num_vec, NAME_SIZE,
					  GFP_KERNEL);
	if (!hw->irq_name) {
		err = -ENOMEM;
		goto err_free_netdev;
	}

	hw->affinity_mask = devm_kcalloc(&hw->pdev->dev, num_vec,
					 sizeof(cpumask_var_t), GFP_KERNEL);
	if (!hw->affinity_mask) {
		err = -ENOMEM;
		goto err_free_netdev;
	}

	/* Map CSRs */
	pf->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!pf->reg_base) {
		dev_err(dev, "Unable to map physical function CSRs, aborting\n");
		err = -ENOMEM;
		goto err_free_netdev;
	}

	err = otx2_check_pf_usable(pf);
	if (err)
		goto err_free_netdev;

	err = pci_alloc_irq_vectors(hw->pdev, RVU_PF_INT_VEC_CNT,
				    RVU_PF_INT_VEC_CNT, PCI_IRQ_MSIX);
	if (err < 0) {
		dev_err(dev, "%s: Failed to alloc %d IRQ vectors\n",
			__func__, num_vec);
		goto err_free_netdev;
	}

	otx2_setup_dev_hw_settings(pf);

	/* Init PF <=> AF mailbox stuff */
	err = otx2_pfaf_mbox_init(pf);
	if (err)
		goto err_free_irq_vectors;

	/* Register mailbox interrupt */
	err = otx2_register_mbox_intr(pf, true);
	if (err)
		goto err_mbox_destroy;

	/* Request AF to attach NPA and NIX LFs to this PF.
	 * NIX and NPA LFs are needed for this PF to function as a NIC.
	 */
	err = otx2_attach_npa_nix(pf);
	if (err)
		goto err_disable_mbox_intr;

	err = otx2_realloc_msix_vectors(pf);
	if (err)
		goto err_detach_rsrc;

	err = otx2_set_real_num_queues(netdev, hw->tx_queues, hw->rx_queues);
	if (err)
		goto err_detach_rsrc;

	err = cn10k_lmtst_init(pf);
	if (err)
		goto err_detach_rsrc;

	/* Assign default mac address */
	otx2_get_mac_from_af(netdev);

	/* Don't check for error.  Proceed without ptp */
	otx2_ptp_init(pf);

	/* NPA's pool is a stack to which SW frees buffer pointers via Aura.
	 * HW allocates buffer pointer from stack and uses it for DMA'ing
	 * ingress packet. In some scenarios HW can free back allocated buffer
	 * pointers to pool. This makes it impossible for SW to maintain a
	 * parallel list where physical addresses of buffer pointers (IOVAs)
	 * given to HW can be saved for later reference.
	 *
	 * So the only way to convert Rx packet's buffer address is to use
	 * IOMMU's iova_to_phys() handler which translates the address by
	 * walking through the translation tables.
	 */
	pf->iommu_domain = iommu_get_domain_for_dev(dev);

	netdev->hw_features = (NETIF_F_RXCSUM | NETIF_F_IP_CSUM |
			       NETIF_F_IPV6_CSUM | NETIF_F_RXHASH |
			       NETIF_F_SG | NETIF_F_TSO | NETIF_F_TSO6 |
			       NETIF_F_GSO_UDP_L4);
	netdev->features |= netdev->hw_features;

	err = otx2_mcam_flow_init(pf);
	if (err)
		goto err_ptp_destroy;

	err = cn10k_mcs_init(pf);
	if (err)
		goto err_del_mcam_entries;

	if (pf->flags & OTX2_FLAG_NTUPLE_SUPPORT)
		netdev->hw_features |= NETIF_F_NTUPLE;

	if (pf->flags & OTX2_FLAG_UCAST_FLTR_SUPPORT)
		netdev->priv_flags |= IFF_UNICAST_FLT;

	/* Support TSO on tag interface */
	netdev->vlan_features |= netdev->features;
	netdev->hw_features  |= NETIF_F_HW_VLAN_CTAG_TX |
				NETIF_F_HW_VLAN_STAG_TX;
	if (pf->flags & OTX2_FLAG_RX_VLAN_SUPPORT)
		netdev->hw_features |= NETIF_F_HW_VLAN_CTAG_RX |
				       NETIF_F_HW_VLAN_STAG_RX;
	netdev->features |= netdev->hw_features;

	/* HW supports tc offload but mutually exclusive with n-tuple filters */
	if (pf->flags & OTX2_FLAG_TC_FLOWER_SUPPORT)
		netdev->hw_features |= NETIF_F_HW_TC;

	netdev->hw_features |= NETIF_F_LOOPBACK | NETIF_F_RXALL;

	netif_set_tso_max_segs(netdev, OTX2_MAX_GSO_SEGS);
	netdev->watchdog_timeo = OTX2_TX_TIMEOUT;

	netdev->netdev_ops = &otx2_netdev_ops;

	netdev->min_mtu = OTX2_MIN_MTU;
	netdev->max_mtu = otx2_get_max_mtu(pf);

	err = register_netdev(netdev);
	if (err) {
		dev_err(dev, "Failed to register netdevice\n");
		goto err_mcs_free;
	}

	err = otx2_wq_init(pf);
	if (err)
		goto err_unreg_netdev;

	otx2_set_ethtool_ops(netdev);

	err = otx2_init_tc(pf);
	if (err)
		goto err_mcam_flow_del;

	err = otx2_register_dl(pf);
	if (err)
		goto err_mcam_flow_del;

	/* Initialize SR-IOV resources */
	err = otx2_sriov_vfcfg_init(pf);
	if (err)
		goto err_pf_sriov_init;

	/* Enable link notifications */
	otx2_cgx_config_linkevents(pf, true);

#ifdef CONFIG_DCB
	err = otx2_dcbnl_set_ops(netdev);
	if (err)
		goto err_pf_sriov_init;
#endif

	otx2_qos_init(pf, qos_txqs);

	return 0;

err_pf_sriov_init:
	otx2_shutdown_tc(pf);
err_mcam_flow_del:
	otx2_mcam_flow_del(pf);
err_unreg_netdev:
	unregister_netdev(netdev);
err_mcs_free:
	cn10k_mcs_free(pf);
err_del_mcam_entries:
	otx2_mcam_flow_del(pf);
err_ptp_destroy:
	otx2_ptp_destroy(pf);
err_detach_rsrc:
	if (pf->hw.lmt_info)
		free_percpu(pf->hw.lmt_info);
	if (test_bit(CN10K_LMTST, &pf->hw.cap_flag))
		qmem_free(pf->dev, pf->dync_lmt);
	otx2_detach_resources(&pf->mbox);
err_disable_mbox_intr:
	otx2_disable_mbox_intr(pf);
err_mbox_destroy:
	otx2_pfaf_mbox_destroy(pf);
err_free_irq_vectors:
	pci_free_irq_vectors(hw->pdev);
err_free_netdev:
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
err_release_regions:
	pci_release_regions(pdev);
	return err;
}

static void otx2_vf_link_event_task(struct work_struct *work)
{
	struct otx2_vf_config *config;
	struct cgx_link_info_msg *req;
	struct mbox_msghdr *msghdr;
	struct otx2_nic *pf;
	int vf_idx;

	config = container_of(work, struct otx2_vf_config,
			      link_event_work.work);
	vf_idx = config - config->pf->vf_configs;
	pf = config->pf;

	msghdr = otx2_mbox_alloc_msg_rsp(&pf->mbox_pfvf[0].mbox_up, vf_idx,
					 sizeof(*req), sizeof(struct msg_rsp));
	if (!msghdr) {
		dev_err(pf->dev, "Failed to create VF%d link event\n", vf_idx);
		return;
	}

	req = (struct cgx_link_info_msg *)msghdr;
	req->hdr.id = MBOX_MSG_CGX_LINK_EVENT;
	req->hdr.sig = OTX2_MBOX_REQ_SIG;
	memcpy(&req->link_info, &pf->linfo, sizeof(req->link_info));

	otx2_sync_mbox_up_msg(&pf->mbox_pfvf[0], vf_idx);
}

static int otx2_sriov_enable(struct pci_dev *pdev, int numvfs)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct otx2_nic *pf = netdev_priv(netdev);
	int ret;

	/* Init PF <=> VF mailbox stuff */
	ret = otx2_pfvf_mbox_init(pf, numvfs);
	if (ret)
		return ret;

	ret = otx2_register_pfvf_mbox_intr(pf, numvfs);
	if (ret)
		goto free_mbox;

	ret = otx2_pf_flr_init(pf, numvfs);
	if (ret)
		goto free_intr;

	ret = otx2_register_flr_me_intr(pf, numvfs);
	if (ret)
		goto free_flr;

	ret = pci_enable_sriov(pdev, numvfs);
	if (ret)
		goto free_flr_intr;

	return numvfs;
free_flr_intr:
	otx2_disable_flr_me_intr(pf);
free_flr:
	otx2_flr_wq_destroy(pf);
free_intr:
	otx2_disable_pfvf_mbox_intr(pf, numvfs);
free_mbox:
	otx2_pfvf_mbox_destroy(pf);
	return ret;
}

static int otx2_sriov_disable(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct otx2_nic *pf = netdev_priv(netdev);
	int numvfs = pci_num_vf(pdev);

	if (!numvfs)
		return 0;

	pci_disable_sriov(pdev);

	otx2_disable_flr_me_intr(pf);
	otx2_flr_wq_destroy(pf);
	otx2_disable_pfvf_mbox_intr(pf, numvfs);
	otx2_pfvf_mbox_destroy(pf);

	return 0;
}

static int otx2_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	if (numvfs == 0)
		return otx2_sriov_disable(pdev);
	else
		return otx2_sriov_enable(pdev, numvfs);
}

static void otx2_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct otx2_nic *pf;

	if (!netdev)
		return;

	pf = netdev_priv(netdev);

	pf->flags |= OTX2_FLAG_PF_SHUTDOWN;

	if (pf->flags & OTX2_FLAG_TX_TSTAMP_ENABLED)
		otx2_config_hw_tx_tstamp(pf, false);
	if (pf->flags & OTX2_FLAG_RX_TSTAMP_ENABLED)
		otx2_config_hw_rx_tstamp(pf, false);

	/* Disable 802.3x pause frames */
	if (pf->flags & OTX2_FLAG_RX_PAUSE_ENABLED ||
	    (pf->flags & OTX2_FLAG_TX_PAUSE_ENABLED)) {
		pf->flags &= ~OTX2_FLAG_RX_PAUSE_ENABLED;
		pf->flags &= ~OTX2_FLAG_TX_PAUSE_ENABLED;
		otx2_config_pause_frm(pf);
	}

#ifdef CONFIG_DCB
	/* Disable PFC config */
	if (pf->pfc_en) {
		pf->pfc_en = 0;
		otx2_config_priority_flow_ctrl(pf);
	}
#endif
	cancel_work_sync(&pf->reset_task);
	/* Disable link notifications */
	otx2_cgx_config_linkevents(pf, false);

	otx2_unregister_dl(pf);
	unregister_netdev(netdev);
	cn10k_mcs_free(pf);
	otx2_sriov_disable(pf->pdev);
	otx2_sriov_vfcfg_cleanup(pf);
	if (pf->otx2_wq)
		destroy_workqueue(pf->otx2_wq);

	otx2_ptp_destroy(pf);
	otx2_mcam_flow_del(pf);
	otx2_shutdown_tc(pf);
	otx2_detach_resources(&pf->mbox);
	if (pf->hw.lmt_info)
		free_percpu(pf->hw.lmt_info);
	if (test_bit(CN10K_LMTST, &pf->hw.cap_flag))
		qmem_free(pf->dev, pf->dync_lmt);
	otx2_disable_mbox_intr(pf);
	otx2_pfaf_mbox_destroy(pf);
	pci_free_irq_vectors(pf->pdev);
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);

	pci_release_regions(pdev);
}

static struct pci_driver otx2_pf_driver = {
	.name = DRV_NAME,
	.id_table = otx2_pf_id_table,
	.probe = otx2_probe,
	.shutdown = otx2_remove,
	.remove = otx2_remove,
	.sriov_configure = otx2_sriov_configure
};

static int __init otx2_rvupf_init_module(void)
{
	pr_info("%s: %s\n", DRV_NAME, DRV_STRING);

	return pci_register_driver(&otx2_pf_driver);
}

static void __exit otx2_rvupf_cleanup_module(void)
{
	pci_unregister_driver(&otx2_pf_driver);
}

module_init(otx2_rvupf_init_module);
module_exit(otx2_rvupf_cleanup_module);
