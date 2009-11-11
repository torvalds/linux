/**
 * Copyright (C) 2005 - 2009 ServerEngines
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Written by: Jayamohan Kallickal (jayamohank@serverengines.com)
 *
 * Contact Information:
 * linux-drivers@serverengines.com
 *
 *  ServerEngines
 * 209 N. Fair Oaks Ave
 * Sunnyvale, CA 94085
 *
 */
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>

#include <scsi/libiscsi.h>
#include <scsi/scsi_transport_iscsi.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include "be_main.h"
#include "be_iscsi.h"
#include "be_mgmt.h"

static unsigned int be_iopoll_budget = 10;
static unsigned int be_max_phys_size = 64;
static unsigned int enable_msix = 1;
static unsigned int ring_mode;

MODULE_DEVICE_TABLE(pci, beiscsi_pci_id_table);
MODULE_DESCRIPTION(DRV_DESC " " BUILD_STR);
MODULE_AUTHOR("ServerEngines Corporation");
MODULE_LICENSE("GPL");
module_param(be_iopoll_budget, int, 0);
module_param(enable_msix, int, 0);
module_param(be_max_phys_size, uint, S_IRUGO);
MODULE_PARM_DESC(be_max_phys_size, "Maximum Size (In Kilobytes) of physically"
				   "contiguous memory that can be allocated."
				   "Range is 16 - 128");

static int beiscsi_slave_configure(struct scsi_device *sdev)
{
	blk_queue_max_segment_size(sdev->request_queue, 65536);
	return 0;
}

/*------------------- PCI Driver operations and data ----------------- */
static DEFINE_PCI_DEVICE_TABLE(beiscsi_pci_id_table) = {
	{ PCI_DEVICE(BE_VENDOR_ID, BE_DEVICE_ID1) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID1) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID2) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID3) },
	{ PCI_DEVICE(BE_VENDOR_ID, OC_DEVICE_ID4) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, beiscsi_pci_id_table);

static struct scsi_host_template beiscsi_sht = {
	.module = THIS_MODULE,
	.name = "ServerEngines 10Gbe open-iscsi Initiator Driver",
	.proc_name = DRV_NAME,
	.queuecommand = iscsi_queuecommand,
	.eh_abort_handler = iscsi_eh_abort,
	.change_queue_depth = iscsi_change_queue_depth,
	.slave_configure = beiscsi_slave_configure,
	.target_alloc = iscsi_target_alloc,
	.eh_device_reset_handler = iscsi_eh_device_reset,
	.eh_target_reset_handler = iscsi_eh_target_reset,
	.sg_tablesize = BEISCSI_SGLIST_ELEMENTS,
	.can_queue = BE2_IO_DEPTH,
	.this_id = -1,
	.max_sectors = BEISCSI_MAX_SECTORS,
	.cmd_per_lun = BEISCSI_CMD_PER_LUN,
	.use_clustering = ENABLE_CLUSTERING,
};

static struct scsi_transport_template *beiscsi_scsi_transport;

static struct beiscsi_hba *beiscsi_hba_alloc(struct pci_dev *pcidev)
{
	struct beiscsi_hba *phba;
	struct Scsi_Host *shost;

	shost = iscsi_host_alloc(&beiscsi_sht, sizeof(*phba), 0);
	if (!shost) {
		dev_err(&pcidev->dev, "beiscsi_hba_alloc -"
			"iscsi_host_alloc failed \n");
		return NULL;
	}
	shost->dma_boundary = pcidev->dma_mask;
	shost->max_id = BE2_MAX_SESSIONS;
	shost->max_channel = 0;
	shost->max_cmd_len = BEISCSI_MAX_CMD_LEN;
	shost->max_lun = BEISCSI_NUM_MAX_LUN;
	shost->transportt = beiscsi_scsi_transport;
	phba = iscsi_host_priv(shost);
	memset(phba, 0, sizeof(*phba));
	phba->shost = shost;
	phba->pcidev = pci_dev_get(pcidev);

	if (iscsi_host_add(shost, &phba->pcidev->dev))
		goto free_devices;
	return phba;

free_devices:
	pci_dev_put(phba->pcidev);
	iscsi_host_free(phba->shost);
	return NULL;
}

static void beiscsi_unmap_pci_function(struct beiscsi_hba *phba)
{
	if (phba->csr_va) {
		iounmap(phba->csr_va);
		phba->csr_va = NULL;
	}
	if (phba->db_va) {
		iounmap(phba->db_va);
		phba->db_va = NULL;
	}
	if (phba->pci_va) {
		iounmap(phba->pci_va);
		phba->pci_va = NULL;
	}
}

static int beiscsi_map_pci_bars(struct beiscsi_hba *phba,
				struct pci_dev *pcidev)
{
	u8 __iomem *addr;

	addr = ioremap_nocache(pci_resource_start(pcidev, 2),
			       pci_resource_len(pcidev, 2));
	if (addr == NULL)
		return -ENOMEM;
	phba->ctrl.csr = addr;
	phba->csr_va = addr;
	phba->csr_pa.u.a64.address = pci_resource_start(pcidev, 2);

	addr = ioremap_nocache(pci_resource_start(pcidev, 4), 128 * 1024);
	if (addr == NULL)
		goto pci_map_err;
	phba->ctrl.db = addr;
	phba->db_va = addr;
	phba->db_pa.u.a64.address =  pci_resource_start(pcidev, 4);

	addr = ioremap_nocache(pci_resource_start(pcidev, 1),
			       pci_resource_len(pcidev, 1));
	if (addr == NULL)
		goto pci_map_err;
	phba->ctrl.pcicfg = addr;
	phba->pci_va = addr;
	phba->pci_pa.u.a64.address = pci_resource_start(pcidev, 1);
	return 0;

pci_map_err:
	beiscsi_unmap_pci_function(phba);
	return -ENOMEM;
}

static int beiscsi_enable_pci(struct pci_dev *pcidev)
{
	int ret;

	ret = pci_enable_device(pcidev);
	if (ret) {
		dev_err(&pcidev->dev, "beiscsi_enable_pci - enable device "
			"failed. Returning -ENODEV\n");
		return ret;
	}

	pci_set_master(pcidev);
	if (pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(64))) {
		ret = pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pcidev->dev, "Could not set PCI DMA Mask\n");
			pci_disable_device(pcidev);
			return ret;
		}
	}
	return 0;
}

static int be_ctrl_init(struct beiscsi_hba *phba, struct pci_dev *pdev)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_dma_mem *mbox_mem_alloc = &ctrl->mbox_mem_alloced;
	struct be_dma_mem *mbox_mem_align = &ctrl->mbox_mem;
	int status = 0;

	ctrl->pdev = pdev;
	status = beiscsi_map_pci_bars(phba, pdev);
	if (status)
		return status;
	mbox_mem_alloc->size = sizeof(struct be_mcc_mailbox) + 16;
	mbox_mem_alloc->va = pci_alloc_consistent(pdev,
						  mbox_mem_alloc->size,
						  &mbox_mem_alloc->dma);
	if (!mbox_mem_alloc->va) {
		beiscsi_unmap_pci_function(phba);
		status = -ENOMEM;
		return status;
	}

	mbox_mem_align->size = sizeof(struct be_mcc_mailbox);
	mbox_mem_align->va = PTR_ALIGN(mbox_mem_alloc->va, 16);
	mbox_mem_align->dma = PTR_ALIGN(mbox_mem_alloc->dma, 16);
	memset(mbox_mem_align->va, 0, sizeof(struct be_mcc_mailbox));
	spin_lock_init(&ctrl->mbox_lock);
	spin_lock_init(&phba->ctrl.mcc_lock);
	spin_lock_init(&phba->ctrl.mcc_cq_lock);

	return status;
}

static void beiscsi_get_params(struct beiscsi_hba *phba)
{
	phba->params.ios_per_ctrl = BE2_IO_DEPTH;
	phba->params.cxns_per_ctrl = BE2_MAX_SESSIONS;
	phba->params.asyncpdus_per_ctrl = BE2_ASYNCPDUS;
	phba->params.icds_per_ctrl = BE2_MAX_ICDS / 2;
	phba->params.num_sge_per_io = BE2_SGE;
	phba->params.defpdu_hdr_sz = BE2_DEFPDU_HDR_SZ;
	phba->params.defpdu_data_sz = BE2_DEFPDU_DATA_SZ;
	phba->params.eq_timer = 64;
	phba->params.num_eq_entries =
	    (((BE2_CMDS_PER_CXN * 2 + BE2_LOGOUTS + BE2_TMFS + BE2_ASYNCPDUS) /
								512) + 1) * 512;
	phba->params.num_eq_entries = (phba->params.num_eq_entries < 1024)
				? 1024 : phba->params.num_eq_entries;
	SE_DEBUG(DBG_LVL_8, "phba->params.num_eq_entries=%d \n",
		 phba->params.num_eq_entries);
	phba->params.num_cq_entries =
	    (((BE2_CMDS_PER_CXN * 2 + BE2_LOGOUTS + BE2_TMFS + BE2_ASYNCPDUS) /
								512) + 1) * 512;
	SE_DEBUG(DBG_LVL_8,
		"phba->params.num_cq_entries=%d BE2_CMDS_PER_CXN=%d"
		"BE2_LOGOUTS=%d BE2_TMFS=%d BE2_ASYNCPDUS=%d \n",
		phba->params.num_cq_entries, BE2_CMDS_PER_CXN,
		BE2_LOGOUTS, BE2_TMFS, BE2_ASYNCPDUS);
	phba->params.wrbs_per_cxn = 256;
}

static void hwi_ring_eq_db(struct beiscsi_hba *phba,
			   unsigned int id, unsigned int clr_interrupt,
			   unsigned int num_processed,
			   unsigned char rearm, unsigned char event)
{
	u32 val = 0;
	val |= id & DB_EQ_RING_ID_MASK;
	if (rearm)
		val |= 1 << DB_EQ_REARM_SHIFT;
	if (clr_interrupt)
		val |= 1 << DB_EQ_CLR_SHIFT;
	if (event)
		val |= 1 << DB_EQ_EVNT_SHIFT;
	val |= num_processed << DB_EQ_NUM_POPPED_SHIFT;
	iowrite32(val, phba->db_va + DB_EQ_OFFSET);
}

/**
 * be_isr_mcc - The isr routine of the driver.
 * @irq: Not used
 * @dev_id: Pointer to host adapter structure
 */
static irqreturn_t be_isr_mcc(int irq, void *dev_id)
{
	struct beiscsi_hba *phba;
	struct be_eq_entry *eqe = NULL;
	struct be_queue_info *eq;
	struct be_queue_info *mcc;
	unsigned int num_eq_processed;
	struct be_eq_obj *pbe_eq;
	unsigned long flags;

	pbe_eq = dev_id;
	eq = &pbe_eq->q;
	phba =  pbe_eq->phba;
	mcc = &phba->ctrl.mcc_obj.cq;
	eqe = queue_tail_node(eq);
	if (!eqe)
		SE_DEBUG(DBG_LVL_1, "eqe is NULL\n");

	num_eq_processed = 0;

	while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
				& EQE_VALID_MASK) {
		if (((eqe->dw[offsetof(struct amap_eq_entry,
		     resource_id) / 32] &
		     EQE_RESID_MASK) >> 16) == mcc->id) {
			spin_lock_irqsave(&phba->isr_lock, flags);
			phba->todo_mcc_cq = 1;
			spin_unlock_irqrestore(&phba->isr_lock, flags);
		}
		AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
		queue_tail_inc(eq);
		eqe = queue_tail_node(eq);
		num_eq_processed++;
	}
	if (phba->todo_mcc_cq)
		queue_work(phba->wq, &phba->work_cqs);
	if (num_eq_processed)
		hwi_ring_eq_db(phba, eq->id, 1,	num_eq_processed, 1, 1);

	return IRQ_HANDLED;
}

/**
 * be_isr_msix - The isr routine of the driver.
 * @irq: Not used
 * @dev_id: Pointer to host adapter structure
 */
static irqreturn_t be_isr_msix(int irq, void *dev_id)
{
	struct beiscsi_hba *phba;
	struct be_eq_entry *eqe = NULL;
	struct be_queue_info *eq;
	struct be_queue_info *cq;
	unsigned int num_eq_processed;
	struct be_eq_obj *pbe_eq;
	unsigned long flags;

	pbe_eq = dev_id;
	eq = &pbe_eq->q;
	cq = pbe_eq->cq;
	eqe = queue_tail_node(eq);
	if (!eqe)
		SE_DEBUG(DBG_LVL_1, "eqe is NULL\n");

	phba = pbe_eq->phba;
	num_eq_processed = 0;
	if (blk_iopoll_enabled) {
		while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
					& EQE_VALID_MASK) {
			if (!blk_iopoll_sched_prep(&pbe_eq->iopoll))
				blk_iopoll_sched(&pbe_eq->iopoll);

			AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
			queue_tail_inc(eq);
			eqe = queue_tail_node(eq);
			num_eq_processed++;
		}
		if (num_eq_processed)
			hwi_ring_eq_db(phba, eq->id, 1,	num_eq_processed, 0, 1);

		return IRQ_HANDLED;
	} else {
		while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
						& EQE_VALID_MASK) {
			spin_lock_irqsave(&phba->isr_lock, flags);
			phba->todo_cq = 1;
			spin_unlock_irqrestore(&phba->isr_lock, flags);
			AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
			queue_tail_inc(eq);
			eqe = queue_tail_node(eq);
			num_eq_processed++;
		}
		if (phba->todo_cq)
			queue_work(phba->wq, &phba->work_cqs);

		if (num_eq_processed)
			hwi_ring_eq_db(phba, eq->id, 1, num_eq_processed, 1, 1);

		return IRQ_HANDLED;
	}
}

/**
 * be_isr - The isr routine of the driver.
 * @irq: Not used
 * @dev_id: Pointer to host adapter structure
 */
static irqreturn_t be_isr(int irq, void *dev_id)
{
	struct beiscsi_hba *phba;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_eq_entry *eqe = NULL;
	struct be_queue_info *eq;
	struct be_queue_info *cq;
	struct be_queue_info *mcc;
	unsigned long flags, index;
	unsigned int num_mcceq_processed, num_ioeq_processed;
	struct be_ctrl_info *ctrl;
	struct be_eq_obj *pbe_eq;
	int isr;

	phba = dev_id;
	ctrl = &phba->ctrl;;
	isr = ioread32(ctrl->csr + CEV_ISR0_OFFSET +
		       (PCI_FUNC(ctrl->pdev->devfn) * CEV_ISR_SIZE));
	if (!isr)
		return IRQ_NONE;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	pbe_eq = &phwi_context->be_eq[0];

	eq = &phwi_context->be_eq[0].q;
	mcc = &phba->ctrl.mcc_obj.cq;
	index = 0;
	eqe = queue_tail_node(eq);
	if (!eqe)
		SE_DEBUG(DBG_LVL_1, "eqe is NULL\n");

	num_ioeq_processed = 0;
	num_mcceq_processed = 0;
	if (blk_iopoll_enabled) {
		while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
					& EQE_VALID_MASK) {
			if (((eqe->dw[offsetof(struct amap_eq_entry,
			     resource_id) / 32] &
			     EQE_RESID_MASK) >> 16) == mcc->id) {
				spin_lock_irqsave(&phba->isr_lock, flags);
				phba->todo_mcc_cq = 1;
				spin_unlock_irqrestore(&phba->isr_lock, flags);
				num_mcceq_processed++;
			} else {
				if (!blk_iopoll_sched_prep(&pbe_eq->iopoll))
					blk_iopoll_sched(&pbe_eq->iopoll);
				num_ioeq_processed++;
			}
			AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
			queue_tail_inc(eq);
			eqe = queue_tail_node(eq);
		}
		if (num_ioeq_processed || num_mcceq_processed) {
			if (phba->todo_mcc_cq)
				queue_work(phba->wq, &phba->work_cqs);

		if ((num_mcceq_processed) && (!num_ioeq_processed))
				hwi_ring_eq_db(phba, eq->id, 0,
					      (num_ioeq_processed +
					       num_mcceq_processed) , 1, 1);
			else
				hwi_ring_eq_db(phba, eq->id, 0,
					       (num_ioeq_processed +
						num_mcceq_processed), 0, 1);

			return IRQ_HANDLED;
		} else
			return IRQ_NONE;
	} else {
		cq = &phwi_context->be_cq[0];
		while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
						& EQE_VALID_MASK) {

			if (((eqe->dw[offsetof(struct amap_eq_entry,
			     resource_id) / 32] &
			     EQE_RESID_MASK) >> 16) != cq->id) {
				spin_lock_irqsave(&phba->isr_lock, flags);
				phba->todo_mcc_cq = 1;
				spin_unlock_irqrestore(&phba->isr_lock, flags);
			} else {
				spin_lock_irqsave(&phba->isr_lock, flags);
				phba->todo_cq = 1;
				spin_unlock_irqrestore(&phba->isr_lock, flags);
			}
			AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
			queue_tail_inc(eq);
			eqe = queue_tail_node(eq);
			num_ioeq_processed++;
		}
		if (phba->todo_cq || phba->todo_mcc_cq)
			queue_work(phba->wq, &phba->work_cqs);

		if (num_ioeq_processed) {
			hwi_ring_eq_db(phba, eq->id, 0,
				       num_ioeq_processed, 1, 1);
			return IRQ_HANDLED;
		} else
			return IRQ_NONE;
	}
}

static int beiscsi_init_irqs(struct beiscsi_hba *phba)
{
	struct pci_dev *pcidev = phba->pcidev;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	int ret, msix_vec, i = 0;
	char desc[32];

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	if (phba->msix_enabled) {
		for (i = 0; i < phba->num_cpus; i++) {
			sprintf(desc, "beiscsi_msix_%04x", i);
			msix_vec = phba->msix_entries[i].vector;
			ret = request_irq(msix_vec, be_isr_msix, 0, desc,
					  &phwi_context->be_eq[i]);
		}
		msix_vec = phba->msix_entries[i].vector;
		ret = request_irq(msix_vec, be_isr_mcc, 0, "beiscsi_msix_mcc",
				  &phwi_context->be_eq[i]);
	} else {
		ret = request_irq(pcidev->irq, be_isr, IRQF_SHARED,
				  "beiscsi", phba);
		if (ret) {
			shost_printk(KERN_ERR, phba->shost, "beiscsi_init_irqs-"
				     "Failed to register irq\\n");
			return ret;
		}
	}
	return 0;
}

static void hwi_ring_cq_db(struct beiscsi_hba *phba,
			   unsigned int id, unsigned int num_processed,
			   unsigned char rearm, unsigned char event)
{
	u32 val = 0;
	val |= id & DB_CQ_RING_ID_MASK;
	if (rearm)
		val |= 1 << DB_CQ_REARM_SHIFT;
	val |= num_processed << DB_CQ_NUM_POPPED_SHIFT;
	iowrite32(val, phba->db_va + DB_CQ_OFFSET);
}

static unsigned int
beiscsi_process_async_pdu(struct beiscsi_conn *beiscsi_conn,
			  struct beiscsi_hba *phba,
			  unsigned short cid,
			  struct pdu_base *ppdu,
			  unsigned long pdu_len,
			  void *pbuffer, unsigned long buf_len)
{
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct iscsi_session *session = conn->session;
	struct iscsi_task *task;
	struct beiscsi_io_task *io_task;
	struct iscsi_hdr *login_hdr;

	switch (ppdu->dw[offsetof(struct amap_pdu_base, opcode) / 32] &
						PDUBASE_OPCODE_MASK) {
	case ISCSI_OP_NOOP_IN:
		pbuffer = NULL;
		buf_len = 0;
		break;
	case ISCSI_OP_ASYNC_EVENT:
		break;
	case ISCSI_OP_REJECT:
		WARN_ON(!pbuffer);
		WARN_ON(!(buf_len == 48));
		SE_DEBUG(DBG_LVL_1, "In ISCSI_OP_REJECT\n");
		break;
	case ISCSI_OP_LOGIN_RSP:
		task = conn->login_task;
		io_task = task->dd_data;
		login_hdr = (struct iscsi_hdr *)ppdu;
		login_hdr->itt = io_task->libiscsi_itt;
		break;
	default:
		shost_printk(KERN_WARNING, phba->shost,
			     "Unrecognized opcode 0x%x in async msg \n",
			     (ppdu->
			     dw[offsetof(struct amap_pdu_base, opcode) / 32]
						& PDUBASE_OPCODE_MASK));
		return 1;
	}

	spin_lock_bh(&session->lock);
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)ppdu, pbuffer, buf_len);
	spin_unlock_bh(&session->lock);
	return 0;
}

static struct sgl_handle *alloc_io_sgl_handle(struct beiscsi_hba *phba)
{
	struct sgl_handle *psgl_handle;

	if (phba->io_sgl_hndl_avbl) {
		SE_DEBUG(DBG_LVL_8,
			 "In alloc_io_sgl_handle,io_sgl_alloc_index=%d \n",
			 phba->io_sgl_alloc_index);
		psgl_handle = phba->io_sgl_hndl_base[phba->
						io_sgl_alloc_index];
		phba->io_sgl_hndl_base[phba->io_sgl_alloc_index] = NULL;
		phba->io_sgl_hndl_avbl--;
		if (phba->io_sgl_alloc_index == (phba->params.
						 ios_per_ctrl - 1))
			phba->io_sgl_alloc_index = 0;
		else
			phba->io_sgl_alloc_index++;
	} else
		psgl_handle = NULL;
	return psgl_handle;
}

static void
free_io_sgl_handle(struct beiscsi_hba *phba, struct sgl_handle *psgl_handle)
{
	SE_DEBUG(DBG_LVL_8, "In free_,io_sgl_free_index=%d \n",
		 phba->io_sgl_free_index);
	if (phba->io_sgl_hndl_base[phba->io_sgl_free_index]) {
		/*
		 * this can happen if clean_task is called on a task that
		 * failed in xmit_task or alloc_pdu.
		 */
		 SE_DEBUG(DBG_LVL_8,
			 "Double Free in IO SGL io_sgl_free_index=%d,"
			 "value there=%p \n", phba->io_sgl_free_index,
			 phba->io_sgl_hndl_base[phba->io_sgl_free_index]);
		return;
	}
	phba->io_sgl_hndl_base[phba->io_sgl_free_index] = psgl_handle;
	phba->io_sgl_hndl_avbl++;
	if (phba->io_sgl_free_index == (phba->params.ios_per_ctrl - 1))
		phba->io_sgl_free_index = 0;
	else
		phba->io_sgl_free_index++;
}

/**
 * alloc_wrb_handle - To allocate a wrb handle
 * @phba: The hba pointer
 * @cid: The cid to use for allocation
 * @index: index allocation and wrb index
 *
 * This happens under session_lock until submission to chip
 */
struct wrb_handle *alloc_wrb_handle(struct beiscsi_hba *phba, unsigned int cid,
				    int index)
{
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;
	struct wrb_handle *pwrb_handle;

	phwi_ctrlr = phba->phwi_ctrlr;
	pwrb_context = &phwi_ctrlr->wrb_context[cid];
	if (pwrb_context->wrb_handles_available) {
		pwrb_handle = pwrb_context->pwrb_handle_base[
					    pwrb_context->alloc_index];
		pwrb_context->wrb_handles_available--;
		pwrb_handle->nxt_wrb_index = pwrb_handle->wrb_index;
		if (pwrb_context->alloc_index ==
						(phba->params.wrbs_per_cxn - 1))
			pwrb_context->alloc_index = 0;
		else
			pwrb_context->alloc_index++;
	} else
		pwrb_handle = NULL;
	return pwrb_handle;
}

/**
 * free_wrb_handle - To free the wrb handle back to pool
 * @phba: The hba pointer
 * @pwrb_context: The context to free from
 * @pwrb_handle: The wrb_handle to free
 *
 * This happens under session_lock until submission to chip
 */
static void
free_wrb_handle(struct beiscsi_hba *phba, struct hwi_wrb_context *pwrb_context,
		struct wrb_handle *pwrb_handle)
{
	if (!ring_mode)
		pwrb_context->pwrb_handle_base[pwrb_context->free_index] =
					       pwrb_handle;
	pwrb_context->wrb_handles_available++;
	if (pwrb_context->free_index == (phba->params.wrbs_per_cxn - 1))
		pwrb_context->free_index = 0;
	else
		pwrb_context->free_index++;

	SE_DEBUG(DBG_LVL_8,
		 "FREE WRB: pwrb_handle=%p free_index=0x%x"
		 "wrb_handles_available=%d \n",
		 pwrb_handle, pwrb_context->free_index,
		 pwrb_context->wrb_handles_available);
}

static struct sgl_handle *alloc_mgmt_sgl_handle(struct beiscsi_hba *phba)
{
	struct sgl_handle *psgl_handle;

	if (phba->eh_sgl_hndl_avbl) {
		psgl_handle = phba->eh_sgl_hndl_base[phba->eh_sgl_alloc_index];
		phba->eh_sgl_hndl_base[phba->eh_sgl_alloc_index] = NULL;
		SE_DEBUG(DBG_LVL_8, "mgmt_sgl_alloc_index=%d=0x%x \n",
			 phba->eh_sgl_alloc_index, phba->eh_sgl_alloc_index);
		phba->eh_sgl_hndl_avbl--;
		if (phba->eh_sgl_alloc_index ==
		    (phba->params.icds_per_ctrl - phba->params.ios_per_ctrl -
		     1))
			phba->eh_sgl_alloc_index = 0;
		else
			phba->eh_sgl_alloc_index++;
	} else
		psgl_handle = NULL;
	return psgl_handle;
}

void
free_mgmt_sgl_handle(struct beiscsi_hba *phba, struct sgl_handle *psgl_handle)
{

	SE_DEBUG(DBG_LVL_8, "In  free_mgmt_sgl_handle,eh_sgl_free_index=%d \n",
			     phba->eh_sgl_free_index);
	if (phba->eh_sgl_hndl_base[phba->eh_sgl_free_index]) {
		/*
		 * this can happen if clean_task is called on a task that
		 * failed in xmit_task or alloc_pdu.
		 */
		SE_DEBUG(DBG_LVL_8,
			 "Double Free in eh SGL ,eh_sgl_free_index=%d \n",
			 phba->eh_sgl_free_index);
		return;
	}
	phba->eh_sgl_hndl_base[phba->eh_sgl_free_index] = psgl_handle;
	phba->eh_sgl_hndl_avbl++;
	if (phba->eh_sgl_free_index ==
	    (phba->params.icds_per_ctrl - phba->params.ios_per_ctrl - 1))
		phba->eh_sgl_free_index = 0;
	else
		phba->eh_sgl_free_index++;
}

static void
be_complete_io(struct beiscsi_conn *beiscsi_conn,
	       struct iscsi_task *task, struct sol_cqe *psol)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct be_status_bhs *sts_bhs =
				(struct be_status_bhs *)io_task->cmd_bhs;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	unsigned int sense_len;
	unsigned char *sense;
	u32 resid = 0, exp_cmdsn, max_cmdsn;
	u8 rsp, status, flags;

	exp_cmdsn = (psol->
			dw[offsetof(struct amap_sol_cqe, i_exp_cmd_sn) / 32]
			& SOL_EXP_CMD_SN_MASK);
	max_cmdsn = ((psol->
			dw[offsetof(struct amap_sol_cqe, i_exp_cmd_sn) / 32]
			& SOL_EXP_CMD_SN_MASK) +
			((psol->dw[offsetof(struct amap_sol_cqe, i_cmd_wnd)
				/ 32] & SOL_CMD_WND_MASK) >> 24) - 1);
	rsp = ((psol->dw[offsetof(struct amap_sol_cqe, i_resp) / 32]
						& SOL_RESP_MASK) >> 16);
	status = ((psol->dw[offsetof(struct amap_sol_cqe, i_sts) / 32]
						& SOL_STS_MASK) >> 8);
	flags = ((psol->dw[offsetof(struct amap_sol_cqe, i_flags) / 32]
					& SOL_FLAGS_MASK) >> 24) | 0x80;

	task->sc->result = (DID_OK << 16) | status;
	if (rsp != ISCSI_STATUS_CMD_COMPLETED) {
		task->sc->result = DID_ERROR << 16;
		goto unmap;
	}

	/* bidi not initially supported */
	if (flags & (ISCSI_FLAG_CMD_UNDERFLOW | ISCSI_FLAG_CMD_OVERFLOW)) {
		resid = (psol->dw[offsetof(struct amap_sol_cqe, i_res_cnt) /
				32] & SOL_RES_CNT_MASK);

		if (!status && (flags & ISCSI_FLAG_CMD_OVERFLOW))
			task->sc->result = DID_ERROR << 16;

		if (flags & ISCSI_FLAG_CMD_UNDERFLOW) {
			scsi_set_resid(task->sc, resid);
			if (!status && (scsi_bufflen(task->sc) - resid <
			    task->sc->underflow))
				task->sc->result = DID_ERROR << 16;
		}
	}

	if (status == SAM_STAT_CHECK_CONDITION) {
		unsigned short *slen = (unsigned short *)sts_bhs->sense_info;
		sense = sts_bhs->sense_info + sizeof(unsigned short);
		sense_len =  cpu_to_be16(*slen);
		memcpy(task->sc->sense_buffer, sense,
		       min_t(u16, sense_len, SCSI_SENSE_BUFFERSIZE));
	}
	if (io_task->cmd_bhs->iscsi_hdr.flags & ISCSI_FLAG_CMD_READ) {
		if (psol->dw[offsetof(struct amap_sol_cqe, i_res_cnt) / 32]
							& SOL_RES_CNT_MASK)
			 conn->rxdata_octets += (psol->
			     dw[offsetof(struct amap_sol_cqe, i_res_cnt) / 32]
			     & SOL_RES_CNT_MASK);
	}
unmap:
	scsi_dma_unmap(io_task->scsi_cmnd);
	iscsi_complete_scsi_task(task, exp_cmdsn, max_cmdsn);
}

static void
be_complete_logout(struct beiscsi_conn *beiscsi_conn,
		   struct iscsi_task *task, struct sol_cqe *psol)
{
	struct iscsi_logout_rsp *hdr;
	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = beiscsi_conn->conn;

	hdr = (struct iscsi_logout_rsp *)task->hdr;
	hdr->t2wait = 5;
	hdr->t2retain = 0;
	hdr->flags = ((psol->dw[offsetof(struct amap_sol_cqe, i_flags) / 32]
					& SOL_FLAGS_MASK) >> 24) | 0x80;
	hdr->response = (psol->dw[offsetof(struct amap_sol_cqe, i_resp) /
					32] & SOL_RESP_MASK);
	hdr->exp_cmdsn = cpu_to_be32(psol->
			dw[offsetof(struct amap_sol_cqe, i_exp_cmd_sn) / 32]
					& SOL_EXP_CMD_SN_MASK);
	hdr->max_cmdsn = be32_to_cpu((psol->
			 dw[offsetof(struct amap_sol_cqe, i_exp_cmd_sn) / 32]
					& SOL_EXP_CMD_SN_MASK) +
			((psol->dw[offsetof(struct amap_sol_cqe, i_cmd_wnd)
					/ 32] & SOL_CMD_WND_MASK) >> 24) - 1);
	hdr->hlength = 0;
	hdr->itt = io_task->libiscsi_itt;
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, NULL, 0);
}

static void
be_complete_tmf(struct beiscsi_conn *beiscsi_conn,
		struct iscsi_task *task, struct sol_cqe *psol)
{
	struct iscsi_tm_rsp *hdr;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct beiscsi_io_task *io_task = task->dd_data;

	hdr = (struct iscsi_tm_rsp *)task->hdr;
	hdr->flags = ((psol->dw[offsetof(struct amap_sol_cqe, i_flags) / 32]
					& SOL_FLAGS_MASK) >> 24) | 0x80;
	hdr->response = (psol->dw[offsetof(struct amap_sol_cqe, i_resp) /
					32] & SOL_RESP_MASK);
	hdr->exp_cmdsn = cpu_to_be32(psol->dw[offsetof(struct amap_sol_cqe,
				    i_exp_cmd_sn) / 32] & SOL_EXP_CMD_SN_MASK);
	hdr->max_cmdsn = be32_to_cpu((psol->dw[offsetof(struct amap_sol_cqe,
			i_exp_cmd_sn) / 32] & SOL_EXP_CMD_SN_MASK) +
			((psol->dw[offsetof(struct amap_sol_cqe, i_cmd_wnd)
			/ 32] & SOL_CMD_WND_MASK) >> 24) - 1);
	hdr->itt = io_task->libiscsi_itt;
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, NULL, 0);
}

static void
hwi_complete_drvr_msgs(struct beiscsi_conn *beiscsi_conn,
		       struct beiscsi_hba *phba, struct sol_cqe *psol)
{
	struct hwi_wrb_context *pwrb_context;
	struct wrb_handle *pwrb_handle = NULL;
	struct sgl_handle *psgl_handle = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct iscsi_task *task;
	struct beiscsi_io_task *io_task;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct iscsi_session *session = conn->session;

	phwi_ctrlr = phba->phwi_ctrlr;
	if (ring_mode) {
		psgl_handle = phba->sgl_hndl_array[((psol->
			      dw[offsetof(struct amap_sol_cqe_ring, icd_index) /
				32] & SOL_ICD_INDEX_MASK) >> 6)];
		pwrb_context = &phwi_ctrlr->wrb_context[psgl_handle->cid];
		task = psgl_handle->task;
		pwrb_handle = NULL;
	} else {
		pwrb_context = &phwi_ctrlr->wrb_context[((psol->
				dw[offsetof(struct amap_sol_cqe, cid) / 32] &
				SOL_CID_MASK) >> 6)];
		pwrb_handle = pwrb_context->pwrb_handle_basestd[((psol->
				dw[offsetof(struct amap_sol_cqe, wrb_index) /
				32] & SOL_WRB_INDEX_MASK) >> 16)];
		task = pwrb_handle->pio_handle;
	}

	io_task = task->dd_data;
	spin_lock(&phba->mgmt_sgl_lock);
	free_mgmt_sgl_handle(phba, io_task->psgl_handle);
	spin_unlock(&phba->mgmt_sgl_lock);
	spin_lock_bh(&session->lock);
	free_wrb_handle(phba, pwrb_context, pwrb_handle);
	spin_unlock_bh(&session->lock);
}

static void
be_complete_nopin_resp(struct beiscsi_conn *beiscsi_conn,
		       struct iscsi_task *task, struct sol_cqe *psol)
{
	struct iscsi_nopin *hdr;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct beiscsi_io_task *io_task = task->dd_data;

	hdr = (struct iscsi_nopin *)task->hdr;
	hdr->flags = ((psol->dw[offsetof(struct amap_sol_cqe, i_flags) / 32]
			& SOL_FLAGS_MASK) >> 24) | 0x80;
	hdr->exp_cmdsn = cpu_to_be32(psol->dw[offsetof(struct amap_sol_cqe,
				     i_exp_cmd_sn) / 32] & SOL_EXP_CMD_SN_MASK);
	hdr->max_cmdsn = be32_to_cpu((psol->dw[offsetof(struct amap_sol_cqe,
			i_exp_cmd_sn) / 32] & SOL_EXP_CMD_SN_MASK) +
			((psol->dw[offsetof(struct amap_sol_cqe, i_cmd_wnd)
			/ 32] & SOL_CMD_WND_MASK) >> 24) - 1);
	hdr->opcode = ISCSI_OP_NOOP_IN;
	hdr->itt = io_task->libiscsi_itt;
	__iscsi_complete_pdu(conn, (struct iscsi_hdr *)hdr, NULL, 0);
}

static void hwi_complete_cmd(struct beiscsi_conn *beiscsi_conn,
			     struct beiscsi_hba *phba, struct sol_cqe *psol)
{
	struct hwi_wrb_context *pwrb_context;
	struct wrb_handle *pwrb_handle;
	struct iscsi_wrb *pwrb = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct iscsi_task *task;
	struct sgl_handle *psgl_handle = NULL;
	unsigned int type;
	struct iscsi_conn *conn = beiscsi_conn->conn;
	struct iscsi_session *session = conn->session;

	phwi_ctrlr = phba->phwi_ctrlr;
	if (ring_mode) {
		psgl_handle = phba->sgl_hndl_array[((psol->
			      dw[offsetof(struct amap_sol_cqe_ring, icd_index) /
			      32] & SOL_ICD_INDEX_MASK) >> 6)];
		task = psgl_handle->task;
		type = psgl_handle->type;
	} else {
		pwrb_context = &phwi_ctrlr->
				wrb_context[((psol->dw[offsetof
				(struct amap_sol_cqe, cid) / 32]
				& SOL_CID_MASK) >> 6)];
		pwrb_handle = pwrb_context->pwrb_handle_basestd[((psol->
				dw[offsetof(struct amap_sol_cqe, wrb_index) /
				32] & SOL_WRB_INDEX_MASK) >> 16)];
		task = pwrb_handle->pio_handle;
		pwrb = pwrb_handle->pwrb;
		type = (pwrb->dw[offsetof(struct amap_iscsi_wrb, type) / 32] &
			 WRB_TYPE_MASK) >> 28;
	}
	spin_lock_bh(&session->lock);
	switch (type) {
	case HWH_TYPE_IO:
	case HWH_TYPE_IO_RD:
		if ((task->hdr->opcode & ISCSI_OPCODE_MASK) ==
		    ISCSI_OP_NOOP_OUT) {
			be_complete_nopin_resp(beiscsi_conn, task, psol);
		} else
			be_complete_io(beiscsi_conn, task, psol);
		break;

	case HWH_TYPE_LOGOUT:
		be_complete_logout(beiscsi_conn, task, psol);
		break;

	case HWH_TYPE_LOGIN:
		SE_DEBUG(DBG_LVL_1,
			 "\t\t No HWH_TYPE_LOGIN Expected in hwi_complete_cmd"
			 "- Solicited path \n");
		break;

	case HWH_TYPE_TMF:
		be_complete_tmf(beiscsi_conn, task, psol);
		break;

	case HWH_TYPE_NOP:
		be_complete_nopin_resp(beiscsi_conn, task, psol);
		break;

	default:
		if (ring_mode)
			shost_printk(KERN_WARNING, phba->shost,
				"In hwi_complete_cmd, unknown type = %d"
				"icd_index 0x%x CID 0x%x\n", type,
				((psol->dw[offsetof(struct amap_sol_cqe_ring,
				icd_index) / 32] & SOL_ICD_INDEX_MASK) >> 6),
				psgl_handle->cid);
		else
			shost_printk(KERN_WARNING, phba->shost,
				"In hwi_complete_cmd, unknown type = %d"
				"wrb_index 0x%x CID 0x%x\n", type,
				((psol->dw[offsetof(struct amap_iscsi_wrb,
				type) / 32] & SOL_WRB_INDEX_MASK) >> 16),
				((psol->dw[offsetof(struct amap_sol_cqe,
				cid) / 32] & SOL_CID_MASK) >> 6));
		break;
	}

	spin_unlock_bh(&session->lock);
}

static struct list_head *hwi_get_async_busy_list(struct hwi_async_pdu_context
					  *pasync_ctx, unsigned int is_header,
					  unsigned int host_write_ptr)
{
	if (is_header)
		return &pasync_ctx->async_entry[host_write_ptr].
		    header_busy_list;
	else
		return &pasync_ctx->async_entry[host_write_ptr].data_busy_list;
}

static struct async_pdu_handle *
hwi_get_async_handle(struct beiscsi_hba *phba,
		     struct beiscsi_conn *beiscsi_conn,
		     struct hwi_async_pdu_context *pasync_ctx,
		     struct i_t_dpdu_cqe *pdpdu_cqe, unsigned int *pcq_index)
{
	struct be_bus_address phys_addr;
	struct list_head *pbusy_list;
	struct async_pdu_handle *pasync_handle = NULL;
	int buffer_len = 0;
	unsigned char buffer_index = -1;
	unsigned char is_header = 0;

	phys_addr.u.a32.address_lo =
	    pdpdu_cqe->dw[offsetof(struct amap_i_t_dpdu_cqe, db_addr_lo) / 32] -
	    ((pdpdu_cqe->dw[offsetof(struct amap_i_t_dpdu_cqe, dpl) / 32]
						& PDUCQE_DPL_MASK) >> 16);
	phys_addr.u.a32.address_hi =
	    pdpdu_cqe->dw[offsetof(struct amap_i_t_dpdu_cqe, db_addr_hi) / 32];

	phys_addr.u.a64.address =
			*((unsigned long long *)(&phys_addr.u.a64.address));

	switch (pdpdu_cqe->dw[offsetof(struct amap_i_t_dpdu_cqe, code) / 32]
			& PDUCQE_CODE_MASK) {
	case UNSOL_HDR_NOTIFY:
		is_header = 1;

		pbusy_list = hwi_get_async_busy_list(pasync_ctx, 1,
			(pdpdu_cqe->dw[offsetof(struct amap_i_t_dpdu_cqe,
			index) / 32] & PDUCQE_INDEX_MASK));

		buffer_len = (unsigned int)(phys_addr.u.a64.address -
				pasync_ctx->async_header.pa_base.u.a64.address);

		buffer_index = buffer_len /
				pasync_ctx->async_header.buffer_size;

		break;
	case UNSOL_DATA_NOTIFY:
		pbusy_list = hwi_get_async_busy_list(pasync_ctx, 0, (pdpdu_cqe->
					dw[offsetof(struct amap_i_t_dpdu_cqe,
					index) / 32] & PDUCQE_INDEX_MASK));
		buffer_len = (unsigned long)(phys_addr.u.a64.address -
					pasync_ctx->async_data.pa_base.u.
					a64.address);
		buffer_index = buffer_len / pasync_ctx->async_data.buffer_size;
		break;
	default:
		pbusy_list = NULL;
		shost_printk(KERN_WARNING, phba->shost,
			"Unexpected code=%d \n",
			 pdpdu_cqe->dw[offsetof(struct amap_i_t_dpdu_cqe,
					code) / 32] & PDUCQE_CODE_MASK);
		return NULL;
	}

	WARN_ON(!(buffer_index <= pasync_ctx->async_data.num_entries));
	WARN_ON(list_empty(pbusy_list));
	list_for_each_entry(pasync_handle, pbusy_list, link) {
		WARN_ON(pasync_handle->consumed);
		if (pasync_handle->index == buffer_index)
			break;
	}

	WARN_ON(!pasync_handle);

	pasync_handle->cri = (unsigned short)beiscsi_conn->beiscsi_conn_cid;
	pasync_handle->is_header = is_header;
	pasync_handle->buffer_len = ((pdpdu_cqe->
			dw[offsetof(struct amap_i_t_dpdu_cqe, dpl) / 32]
			& PDUCQE_DPL_MASK) >> 16);

	*pcq_index = (pdpdu_cqe->dw[offsetof(struct amap_i_t_dpdu_cqe,
			index) / 32] & PDUCQE_INDEX_MASK);
	return pasync_handle;
}

static unsigned int
hwi_update_async_writables(struct hwi_async_pdu_context *pasync_ctx,
			   unsigned int is_header, unsigned int cq_index)
{
	struct list_head *pbusy_list;
	struct async_pdu_handle *pasync_handle;
	unsigned int num_entries, writables = 0;
	unsigned int *pep_read_ptr, *pwritables;


	if (is_header) {
		pep_read_ptr = &pasync_ctx->async_header.ep_read_ptr;
		pwritables = &pasync_ctx->async_header.writables;
		num_entries = pasync_ctx->async_header.num_entries;
	} else {
		pep_read_ptr = &pasync_ctx->async_data.ep_read_ptr;
		pwritables = &pasync_ctx->async_data.writables;
		num_entries = pasync_ctx->async_data.num_entries;
	}

	while ((*pep_read_ptr) != cq_index) {
		(*pep_read_ptr)++;
		*pep_read_ptr = (*pep_read_ptr) % num_entries;

		pbusy_list = hwi_get_async_busy_list(pasync_ctx, is_header,
						     *pep_read_ptr);
		if (writables == 0)
			WARN_ON(list_empty(pbusy_list));

		if (!list_empty(pbusy_list)) {
			pasync_handle = list_entry(pbusy_list->next,
						   struct async_pdu_handle,
						   link);
			WARN_ON(!pasync_handle);
			pasync_handle->consumed = 1;
		}

		writables++;
	}

	if (!writables) {
		SE_DEBUG(DBG_LVL_1,
			 "Duplicate notification received - index 0x%x!!\n",
			 cq_index);
		WARN_ON(1);
	}

	*pwritables = *pwritables + writables;
	return 0;
}

static unsigned int hwi_free_async_msg(struct beiscsi_hba *phba,
				       unsigned int cri)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_async_pdu_context *pasync_ctx;
	struct async_pdu_handle *pasync_handle, *tmp_handle;
	struct list_head *plist;
	unsigned int i = 0;

	phwi_ctrlr = phba->phwi_ctrlr;
	pasync_ctx = HWI_GET_ASYNC_PDU_CTX(phwi_ctrlr);

	plist  = &pasync_ctx->async_entry[cri].wait_queue.list;

	list_for_each_entry_safe(pasync_handle, tmp_handle, plist, link) {
		list_del(&pasync_handle->link);

		if (i == 0) {
			list_add_tail(&pasync_handle->link,
				      &pasync_ctx->async_header.free_list);
			pasync_ctx->async_header.free_entries++;
			i++;
		} else {
			list_add_tail(&pasync_handle->link,
				      &pasync_ctx->async_data.free_list);
			pasync_ctx->async_data.free_entries++;
			i++;
		}
	}

	INIT_LIST_HEAD(&pasync_ctx->async_entry[cri].wait_queue.list);
	pasync_ctx->async_entry[cri].wait_queue.hdr_received = 0;
	pasync_ctx->async_entry[cri].wait_queue.bytes_received = 0;
	return 0;
}

static struct phys_addr *
hwi_get_ring_address(struct hwi_async_pdu_context *pasync_ctx,
		     unsigned int is_header, unsigned int host_write_ptr)
{
	struct phys_addr *pasync_sge = NULL;

	if (is_header)
		pasync_sge = pasync_ctx->async_header.ring_base;
	else
		pasync_sge = pasync_ctx->async_data.ring_base;

	return pasync_sge + host_write_ptr;
}

static void hwi_post_async_buffers(struct beiscsi_hba *phba,
				   unsigned int is_header)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_async_pdu_context *pasync_ctx;
	struct async_pdu_handle *pasync_handle;
	struct list_head *pfree_link, *pbusy_list;
	struct phys_addr *pasync_sge;
	unsigned int ring_id, num_entries;
	unsigned int host_write_num;
	unsigned int writables;
	unsigned int i = 0;
	u32 doorbell = 0;

	phwi_ctrlr = phba->phwi_ctrlr;
	pasync_ctx = HWI_GET_ASYNC_PDU_CTX(phwi_ctrlr);

	if (is_header) {
		num_entries = pasync_ctx->async_header.num_entries;
		writables = min(pasync_ctx->async_header.writables,
				pasync_ctx->async_header.free_entries);
		pfree_link = pasync_ctx->async_header.free_list.next;
		host_write_num = pasync_ctx->async_header.host_write_ptr;
		ring_id = phwi_ctrlr->default_pdu_hdr.id;
	} else {
		num_entries = pasync_ctx->async_data.num_entries;
		writables = min(pasync_ctx->async_data.writables,
				pasync_ctx->async_data.free_entries);
		pfree_link = pasync_ctx->async_data.free_list.next;
		host_write_num = pasync_ctx->async_data.host_write_ptr;
		ring_id = phwi_ctrlr->default_pdu_data.id;
	}

	writables = (writables / 8) * 8;
	if (writables) {
		for (i = 0; i < writables; i++) {
			pbusy_list =
			    hwi_get_async_busy_list(pasync_ctx, is_header,
						    host_write_num);
			pasync_handle =
			    list_entry(pfree_link, struct async_pdu_handle,
								link);
			WARN_ON(!pasync_handle);
			pasync_handle->consumed = 0;

			pfree_link = pfree_link->next;

			pasync_sge = hwi_get_ring_address(pasync_ctx,
						is_header, host_write_num);

			pasync_sge->hi = pasync_handle->pa.u.a32.address_lo;
			pasync_sge->lo = pasync_handle->pa.u.a32.address_hi;

			list_move(&pasync_handle->link, pbusy_list);

			host_write_num++;
			host_write_num = host_write_num % num_entries;
		}

		if (is_header) {
			pasync_ctx->async_header.host_write_ptr =
							host_write_num;
			pasync_ctx->async_header.free_entries -= writables;
			pasync_ctx->async_header.writables -= writables;
			pasync_ctx->async_header.busy_entries += writables;
		} else {
			pasync_ctx->async_data.host_write_ptr = host_write_num;
			pasync_ctx->async_data.free_entries -= writables;
			pasync_ctx->async_data.writables -= writables;
			pasync_ctx->async_data.busy_entries += writables;
		}

		doorbell |= ring_id & DB_DEF_PDU_RING_ID_MASK;
		doorbell |= 1 << DB_DEF_PDU_REARM_SHIFT;
		doorbell |= 0 << DB_DEF_PDU_EVENT_SHIFT;
		doorbell |= (writables & DB_DEF_PDU_CQPROC_MASK)
					<< DB_DEF_PDU_CQPROC_SHIFT;

		iowrite32(doorbell, phba->db_va + DB_RXULP0_OFFSET);
	}
}

static void hwi_flush_default_pdu_buffer(struct beiscsi_hba *phba,
					 struct beiscsi_conn *beiscsi_conn,
					 struct i_t_dpdu_cqe *pdpdu_cqe)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_async_pdu_context *pasync_ctx;
	struct async_pdu_handle *pasync_handle = NULL;
	unsigned int cq_index = -1;

	phwi_ctrlr = phba->phwi_ctrlr;
	pasync_ctx = HWI_GET_ASYNC_PDU_CTX(phwi_ctrlr);

	pasync_handle = hwi_get_async_handle(phba, beiscsi_conn, pasync_ctx,
					     pdpdu_cqe, &cq_index);
	BUG_ON(pasync_handle->is_header != 0);
	if (pasync_handle->consumed == 0)
		hwi_update_async_writables(pasync_ctx, pasync_handle->is_header,
					   cq_index);

	hwi_free_async_msg(phba, pasync_handle->cri);
	hwi_post_async_buffers(phba, pasync_handle->is_header);
}

static unsigned int
hwi_fwd_async_msg(struct beiscsi_conn *beiscsi_conn,
		  struct beiscsi_hba *phba,
		  struct hwi_async_pdu_context *pasync_ctx, unsigned short cri)
{
	struct list_head *plist;
	struct async_pdu_handle *pasync_handle;
	void *phdr = NULL;
	unsigned int hdr_len = 0, buf_len = 0;
	unsigned int status, index = 0, offset = 0;
	void *pfirst_buffer = NULL;
	unsigned int num_buf = 0;

	plist = &pasync_ctx->async_entry[cri].wait_queue.list;

	list_for_each_entry(pasync_handle, plist, link) {
		if (index == 0) {
			phdr = pasync_handle->pbuffer;
			hdr_len = pasync_handle->buffer_len;
		} else {
			buf_len = pasync_handle->buffer_len;
			if (!num_buf) {
				pfirst_buffer = pasync_handle->pbuffer;
				num_buf++;
			}
			memcpy(pfirst_buffer + offset,
			       pasync_handle->pbuffer, buf_len);
			offset = buf_len;
		}
		index++;
	}

	status = beiscsi_process_async_pdu(beiscsi_conn, phba,
					   beiscsi_conn->beiscsi_conn_cid,
					   phdr, hdr_len, pfirst_buffer,
					   buf_len);

	if (status == 0)
		hwi_free_async_msg(phba, cri);
	return 0;
}

static unsigned int
hwi_gather_async_pdu(struct beiscsi_conn *beiscsi_conn,
		     struct beiscsi_hba *phba,
		     struct async_pdu_handle *pasync_handle)
{
	struct hwi_async_pdu_context *pasync_ctx;
	struct hwi_controller *phwi_ctrlr;
	unsigned int bytes_needed = 0, status = 0;
	unsigned short cri = pasync_handle->cri;
	struct pdu_base *ppdu;

	phwi_ctrlr = phba->phwi_ctrlr;
	pasync_ctx = HWI_GET_ASYNC_PDU_CTX(phwi_ctrlr);

	list_del(&pasync_handle->link);
	if (pasync_handle->is_header) {
		pasync_ctx->async_header.busy_entries--;
		if (pasync_ctx->async_entry[cri].wait_queue.hdr_received) {
			hwi_free_async_msg(phba, cri);
			BUG();
		}

		pasync_ctx->async_entry[cri].wait_queue.bytes_received = 0;
		pasync_ctx->async_entry[cri].wait_queue.hdr_received = 1;
		pasync_ctx->async_entry[cri].wait_queue.hdr_len =
				(unsigned short)pasync_handle->buffer_len;
		list_add_tail(&pasync_handle->link,
			      &pasync_ctx->async_entry[cri].wait_queue.list);

		ppdu = pasync_handle->pbuffer;
		bytes_needed = ((((ppdu->dw[offsetof(struct amap_pdu_base,
			data_len_hi) / 32] & PDUBASE_DATALENHI_MASK) << 8) &
			0xFFFF0000) | ((be16_to_cpu((ppdu->
			dw[offsetof(struct amap_pdu_base, data_len_lo) / 32]
			& PDUBASE_DATALENLO_MASK) >> 16)) & 0x0000FFFF));

		if (status == 0) {
			pasync_ctx->async_entry[cri].wait_queue.bytes_needed =
			    bytes_needed;

			if (bytes_needed == 0)
				status = hwi_fwd_async_msg(beiscsi_conn, phba,
							   pasync_ctx, cri);
		}
	} else {
		pasync_ctx->async_data.busy_entries--;
		if (pasync_ctx->async_entry[cri].wait_queue.hdr_received) {
			list_add_tail(&pasync_handle->link,
				      &pasync_ctx->async_entry[cri].wait_queue.
				      list);
			pasync_ctx->async_entry[cri].wait_queue.
				bytes_received +=
				(unsigned short)pasync_handle->buffer_len;

			if (pasync_ctx->async_entry[cri].wait_queue.
			    bytes_received >=
			    pasync_ctx->async_entry[cri].wait_queue.
			    bytes_needed)
				status = hwi_fwd_async_msg(beiscsi_conn, phba,
							   pasync_ctx, cri);
		}
	}
	return status;
}

static void hwi_process_default_pdu_ring(struct beiscsi_conn *beiscsi_conn,
					 struct beiscsi_hba *phba,
					 struct i_t_dpdu_cqe *pdpdu_cqe)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_async_pdu_context *pasync_ctx;
	struct async_pdu_handle *pasync_handle = NULL;
	unsigned int cq_index = -1;

	phwi_ctrlr = phba->phwi_ctrlr;
	pasync_ctx = HWI_GET_ASYNC_PDU_CTX(phwi_ctrlr);
	pasync_handle = hwi_get_async_handle(phba, beiscsi_conn, pasync_ctx,
					     pdpdu_cqe, &cq_index);

	if (pasync_handle->consumed == 0)
		hwi_update_async_writables(pasync_ctx, pasync_handle->is_header,
					   cq_index);
	hwi_gather_async_pdu(beiscsi_conn, phba, pasync_handle);
	hwi_post_async_buffers(phba, pasync_handle->is_header);
}


static unsigned int beiscsi_process_cq(struct be_eq_obj *pbe_eq)
{
	struct be_queue_info *cq;
	struct sol_cqe *sol;
	struct dmsg_cqe *dmsg;
	unsigned int num_processed = 0;
	unsigned int tot_nump = 0;
	struct beiscsi_conn *beiscsi_conn;
	struct sgl_handle *psgl_handle = NULL;
	struct beiscsi_hba *phba;

	cq = pbe_eq->cq;
	sol = queue_tail_node(cq);
	phba = pbe_eq->phba;

	while (sol->dw[offsetof(struct amap_sol_cqe, valid) / 32] &
	       CQE_VALID_MASK) {
		be_dws_le_to_cpu(sol, sizeof(struct sol_cqe));

		if (ring_mode) {
			psgl_handle = phba->sgl_hndl_array[((sol->
				      dw[offsetof(struct amap_sol_cqe_ring,
				      icd_index) / 32] & SOL_ICD_INDEX_MASK)
				      >> 6)];
			beiscsi_conn = phba->conn_table[psgl_handle->cid];
			if (!beiscsi_conn || !beiscsi_conn->ep) {
				shost_printk(KERN_WARNING, phba->shost,
				     "Connection table empty for cid = %d\n",
				      psgl_handle->cid);
				return 0;
			}

		} else {
			beiscsi_conn = phba->conn_table[(u32) (sol->
				 dw[offsetof(struct amap_sol_cqe, cid) / 32] &
				 SOL_CID_MASK) >> 6];

			if (!beiscsi_conn || !beiscsi_conn->ep) {
				shost_printk(KERN_WARNING, phba->shost,
				     "Connection table empty for cid = %d\n",
				     (u32)(sol->dw[offsetof(struct amap_sol_cqe,
				     cid) / 32] & SOL_CID_MASK) >> 6);
				return 0;
			}
		}

		if (num_processed >= 32) {
			hwi_ring_cq_db(phba, cq->id,
					num_processed, 0, 0);
			tot_nump += num_processed;
			num_processed = 0;
		}

		switch ((u32) sol->dw[offsetof(struct amap_sol_cqe, code) /
			32] & CQE_CODE_MASK) {
		case SOL_CMD_COMPLETE:
			hwi_complete_cmd(beiscsi_conn, phba, sol);
			break;
		case DRIVERMSG_NOTIFY:
			SE_DEBUG(DBG_LVL_8, "Received DRIVERMSG_NOTIFY \n");
			dmsg = (struct dmsg_cqe *)sol;
			hwi_complete_drvr_msgs(beiscsi_conn, phba, sol);
			break;
		case UNSOL_HDR_NOTIFY:
			SE_DEBUG(DBG_LVL_8, "Received UNSOL_HDR_ NOTIFY\n");
			hwi_process_default_pdu_ring(beiscsi_conn, phba,
					     (struct i_t_dpdu_cqe *)sol);
			break;
		case UNSOL_DATA_NOTIFY:
			SE_DEBUG(DBG_LVL_8, "Received UNSOL_DATA_NOTIFY\n");
			hwi_process_default_pdu_ring(beiscsi_conn, phba,
					     (struct i_t_dpdu_cqe *)sol);
			break;
		case CXN_INVALIDATE_INDEX_NOTIFY:
		case CMD_INVALIDATED_NOTIFY:
		case CXN_INVALIDATE_NOTIFY:
			SE_DEBUG(DBG_LVL_1,
				 "Ignoring CQ Error notification for cmd/cxn"
				 "invalidate\n");
			break;
		case SOL_CMD_KILLED_DATA_DIGEST_ERR:
		case CMD_KILLED_INVALID_STATSN_RCVD:
		case CMD_KILLED_INVALID_R2T_RCVD:
		case CMD_CXN_KILLED_LUN_INVALID:
		case CMD_CXN_KILLED_ICD_INVALID:
		case CMD_CXN_KILLED_ITT_INVALID:
		case CMD_CXN_KILLED_SEQ_OUTOFORDER:
		case CMD_CXN_KILLED_INVALID_DATASN_RCVD:
			if (ring_mode) {
				SE_DEBUG(DBG_LVL_1,
				 "CQ Error notification for cmd.. "
				 "code %d cid 0x%x\n",
				 sol->dw[offsetof(struct amap_sol_cqe, code) /
				 32] & CQE_CODE_MASK, psgl_handle->cid);
			} else {
				SE_DEBUG(DBG_LVL_1,
				 "CQ Error notification for cmd.. "
				 "code %d cid 0x%x\n",
				 sol->dw[offsetof(struct amap_sol_cqe, code) /
				 32] & CQE_CODE_MASK,
				 (sol->dw[offsetof(struct amap_sol_cqe, cid) /
				 32] & SOL_CID_MASK));
			}
			break;
		case UNSOL_DATA_DIGEST_ERROR_NOTIFY:
			SE_DEBUG(DBG_LVL_1,
				 "Digest error on def pdu ring, dropping..\n");
			hwi_flush_default_pdu_buffer(phba, beiscsi_conn,
					     (struct i_t_dpdu_cqe *) sol);
			break;
		case CXN_KILLED_PDU_SIZE_EXCEEDS_DSL:
		case CXN_KILLED_BURST_LEN_MISMATCH:
		case CXN_KILLED_AHS_RCVD:
		case CXN_KILLED_HDR_DIGEST_ERR:
		case CXN_KILLED_UNKNOWN_HDR:
		case CXN_KILLED_STALE_ITT_TTT_RCVD:
		case CXN_KILLED_INVALID_ITT_TTT_RCVD:
		case CXN_KILLED_TIMED_OUT:
		case CXN_KILLED_FIN_RCVD:
		case CXN_KILLED_BAD_UNSOL_PDU_RCVD:
		case CXN_KILLED_BAD_WRB_INDEX_ERROR:
		case CXN_KILLED_OVER_RUN_RESIDUAL:
		case CXN_KILLED_UNDER_RUN_RESIDUAL:
		case CXN_KILLED_CMND_DATA_NOT_ON_SAME_CONN:
			if (ring_mode) {
				SE_DEBUG(DBG_LVL_1, "CQ Error %d, reset CID "
				 "0x%x...\n",
				 sol->dw[offsetof(struct amap_sol_cqe, code) /
				 32] & CQE_CODE_MASK, psgl_handle->cid);
			} else {
				SE_DEBUG(DBG_LVL_1, "CQ Error %d, reset CID "
				 "0x%x...\n",
				 sol->dw[offsetof(struct amap_sol_cqe, code) /
				 32] & CQE_CODE_MASK,
				 sol->dw[offsetof(struct amap_sol_cqe, cid) /
				 32] & CQE_CID_MASK);
			}
			iscsi_conn_failure(beiscsi_conn->conn,
					   ISCSI_ERR_CONN_FAILED);
			break;
		case CXN_KILLED_RST_SENT:
		case CXN_KILLED_RST_RCVD:
			if (ring_mode) {
				SE_DEBUG(DBG_LVL_1, "CQ Error %d, reset"
				"received/sent on CID 0x%x...\n",
				 sol->dw[offsetof(struct amap_sol_cqe, code) /
				 32] & CQE_CODE_MASK, psgl_handle->cid);
			} else {
				SE_DEBUG(DBG_LVL_1, "CQ Error %d, reset"
				"received/sent on CID 0x%x...\n",
				 sol->dw[offsetof(struct amap_sol_cqe, code) /
				 32] & CQE_CODE_MASK,
				 sol->dw[offsetof(struct amap_sol_cqe, cid) /
				 32] & CQE_CID_MASK);
			}
			iscsi_conn_failure(beiscsi_conn->conn,
					   ISCSI_ERR_CONN_FAILED);
			break;
		default:
			SE_DEBUG(DBG_LVL_1, "CQ Error Invalid code= %d "
				 "received on CID 0x%x...\n",
				 sol->dw[offsetof(struct amap_sol_cqe, code) /
				 32] & CQE_CODE_MASK,
				 sol->dw[offsetof(struct amap_sol_cqe, cid) /
				 32] & CQE_CID_MASK);
			break;
		}

		AMAP_SET_BITS(struct amap_sol_cqe, valid, sol, 0);
		queue_tail_inc(cq);
		sol = queue_tail_node(cq);
		num_processed++;
	}

	if (num_processed > 0) {
		tot_nump += num_processed;
		hwi_ring_cq_db(phba, cq->id, num_processed, 1, 0);
	}
	return tot_nump;
}

static void beiscsi_process_all_cqs(struct work_struct *work)
{
	unsigned long flags;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_eq_obj *pbe_eq;
	struct beiscsi_hba *phba =
	    container_of(work, struct beiscsi_hba, work_cqs);

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	if (phba->msix_enabled)
		pbe_eq = &phwi_context->be_eq[phba->num_cpus];
	else
		pbe_eq = &phwi_context->be_eq[0];

	if (phba->todo_mcc_cq) {
		spin_lock_irqsave(&phba->isr_lock, flags);
		phba->todo_mcc_cq = 0;
		spin_unlock_irqrestore(&phba->isr_lock, flags);
	}

	if (phba->todo_cq) {
		spin_lock_irqsave(&phba->isr_lock, flags);
		phba->todo_cq = 0;
		spin_unlock_irqrestore(&phba->isr_lock, flags);
		beiscsi_process_cq(pbe_eq);
	}
}

static int be_iopoll(struct blk_iopoll *iop, int budget)
{
	static unsigned int ret;
	struct beiscsi_hba *phba;
	struct be_eq_obj *pbe_eq;

	pbe_eq = container_of(iop, struct be_eq_obj, iopoll);
	ret = beiscsi_process_cq(pbe_eq);
	if (ret < budget) {
		phba = pbe_eq->phba;
		blk_iopoll_complete(iop);
		SE_DEBUG(DBG_LVL_8, "rearm pbe_eq->q.id =%d\n", pbe_eq->q.id);
		hwi_ring_eq_db(phba, pbe_eq->q.id, 0, 0, 1, 1);
	}
	return ret;
}

static void
hwi_write_sgl(struct iscsi_wrb *pwrb, struct scatterlist *sg,
	      unsigned int num_sg, struct beiscsi_io_task *io_task)
{
	struct iscsi_sge *psgl;
	unsigned short sg_len, index;
	unsigned int sge_len = 0;
	unsigned long long addr;
	struct scatterlist *l_sg;
	unsigned int offset;

	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_lo, pwrb,
				      io_task->bhs_pa.u.a32.address_lo);
	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_hi, pwrb,
				      io_task->bhs_pa.u.a32.address_hi);

	l_sg = sg;
	for (index = 0; (index < num_sg) && (index < 2); index++, sg_next(sg)) {
		if (index == 0) {
			sg_len = sg_dma_len(sg);
			addr = (u64) sg_dma_address(sg);
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_lo, pwrb,
							(addr & 0xFFFFFFFF));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_hi, pwrb,
							(addr >> 32));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_len, pwrb,
							sg_len);
			sge_len = sg_len;
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_last, pwrb,
							1);
		} else {
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_last, pwrb,
							0);
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_r2t_offset,
							pwrb, sge_len);
			sg_len = sg_dma_len(sg);
			addr = (u64) sg_dma_address(sg);
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_addr_lo, pwrb,
							(addr & 0xFFFFFFFF));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_addr_hi, pwrb,
							(addr >> 32));
			AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_len, pwrb,
							sg_len);
		}
	}
	psgl = (struct iscsi_sge *)io_task->psgl_handle->pfrag;
	memset(psgl, 0, sizeof(*psgl) * BE2_SGE);

	AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, io_task->bhs_len - 2);

	AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
			io_task->bhs_pa.u.a32.address_hi);
	AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
			io_task->bhs_pa.u.a32.address_lo);

	if (num_sg == 2)
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge1_last, pwrb, 1);
	sg = l_sg;
	psgl++;
	psgl++;
	offset = 0;
	for (index = 0; index < num_sg; index++, sg_next(sg), psgl++) {
		sg_len = sg_dma_len(sg);
		addr = (u64) sg_dma_address(sg);
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
						(addr & 0xFFFFFFFF));
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
						(addr >> 32));
		AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, sg_len);
		AMAP_SET_BITS(struct amap_iscsi_sge, sge_offset, psgl, offset);
		AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 0);
		offset += sg_len;
	}
	psgl--;
	AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 1);
}

static void hwi_write_buffer(struct iscsi_wrb *pwrb, struct iscsi_task *task)
{
	struct iscsi_sge *psgl;
	unsigned long long addr;
	struct beiscsi_io_task *io_task = task->dd_data;
	struct beiscsi_conn *beiscsi_conn = io_task->conn;
	struct beiscsi_hba *phba = beiscsi_conn->phba;

	io_task->bhs_len = sizeof(struct be_nonio_bhs) - 2;
	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_lo, pwrb,
				io_task->bhs_pa.u.a32.address_lo);
	AMAP_SET_BITS(struct amap_iscsi_wrb, iscsi_bhs_addr_hi, pwrb,
				io_task->bhs_pa.u.a32.address_hi);

	if (task->data) {
		if (task->data_count) {
			AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 1);
			addr = (u64) pci_map_single(phba->pcidev,
						    task->data,
						    task->data_count, 1);
		} else {
			AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 0);
			addr = 0;
		}
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_lo, pwrb,
						(addr & 0xFFFFFFFF));
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_addr_hi, pwrb,
						(addr >> 32));
		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_len, pwrb,
						task->data_count);

		AMAP_SET_BITS(struct amap_iscsi_wrb, sge0_last, pwrb, 1);
	} else {
		AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 0);
		addr = 0;
	}

	psgl = (struct iscsi_sge *)io_task->psgl_handle->pfrag;

	AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, io_task->bhs_len);

	AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
		      io_task->bhs_pa.u.a32.address_hi);
	AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
		      io_task->bhs_pa.u.a32.address_lo);
	if (task->data) {
		psgl++;
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, sge_offset, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, rsvd0, psgl, 0);
		AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 0);

		psgl++;
		if (task->data) {
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, psgl,
						(addr & 0xFFFFFFFF));
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, psgl,
						(addr >> 32));
		}
		AMAP_SET_BITS(struct amap_iscsi_sge, len, psgl, 0x106);
	}
	AMAP_SET_BITS(struct amap_iscsi_sge, last_sge, psgl, 1);
}

static void beiscsi_find_mem_req(struct beiscsi_hba *phba)
{
	unsigned int num_cq_pages, num_async_pdu_buf_pages;
	unsigned int num_async_pdu_data_pages, wrb_sz_per_cxn;
	unsigned int num_async_pdu_buf_sgl_pages, num_async_pdu_data_sgl_pages;

	num_cq_pages = PAGES_REQUIRED(phba->params.num_cq_entries * \
				      sizeof(struct sol_cqe));
	num_async_pdu_buf_pages =
			PAGES_REQUIRED(phba->params.asyncpdus_per_ctrl * \
				       phba->params.defpdu_hdr_sz);
	num_async_pdu_buf_sgl_pages =
			PAGES_REQUIRED(phba->params.asyncpdus_per_ctrl * \
				       sizeof(struct phys_addr));
	num_async_pdu_data_pages =
			PAGES_REQUIRED(phba->params.asyncpdus_per_ctrl * \
				       phba->params.defpdu_data_sz);
	num_async_pdu_data_sgl_pages =
			PAGES_REQUIRED(phba->params.asyncpdus_per_ctrl * \
				       sizeof(struct phys_addr));

	phba->params.hwi_ws_sz = sizeof(struct hwi_controller);

	phba->mem_req[ISCSI_MEM_GLOBAL_HEADER] = 2 *
						 BE_ISCSI_PDU_HEADER_SIZE;
	phba->mem_req[HWI_MEM_ADDN_CONTEXT] =
					    sizeof(struct hwi_context_memory);


	phba->mem_req[HWI_MEM_WRB] = sizeof(struct iscsi_wrb)
	    * (phba->params.wrbs_per_cxn)
	    * phba->params.cxns_per_ctrl;
	wrb_sz_per_cxn =  sizeof(struct wrb_handle) *
				 (phba->params.wrbs_per_cxn);
	phba->mem_req[HWI_MEM_WRBH] = roundup_pow_of_two((wrb_sz_per_cxn) *
				phba->params.cxns_per_ctrl);

	phba->mem_req[HWI_MEM_SGLH] = sizeof(struct sgl_handle) *
		phba->params.icds_per_ctrl;
	phba->mem_req[HWI_MEM_SGE] = sizeof(struct iscsi_sge) *
		phba->params.num_sge_per_io * phba->params.icds_per_ctrl;

	phba->mem_req[HWI_MEM_ASYNC_HEADER_BUF] =
		num_async_pdu_buf_pages * PAGE_SIZE;
	phba->mem_req[HWI_MEM_ASYNC_DATA_BUF] =
		num_async_pdu_data_pages * PAGE_SIZE;
	phba->mem_req[HWI_MEM_ASYNC_HEADER_RING] =
		num_async_pdu_buf_sgl_pages * PAGE_SIZE;
	phba->mem_req[HWI_MEM_ASYNC_DATA_RING] =
		num_async_pdu_data_sgl_pages * PAGE_SIZE;
	phba->mem_req[HWI_MEM_ASYNC_HEADER_HANDLE] =
		phba->params.asyncpdus_per_ctrl *
		sizeof(struct async_pdu_handle);
	phba->mem_req[HWI_MEM_ASYNC_DATA_HANDLE] =
		phba->params.asyncpdus_per_ctrl *
		sizeof(struct async_pdu_handle);
	phba->mem_req[HWI_MEM_ASYNC_PDU_CONTEXT] =
		sizeof(struct hwi_async_pdu_context) +
		(phba->params.cxns_per_ctrl * sizeof(struct hwi_async_entry));
}

static int beiscsi_alloc_mem(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr;
	dma_addr_t bus_add;
	struct mem_array *mem_arr, *mem_arr_orig;
	unsigned int i, j, alloc_size, curr_alloc_size;

	phba->phwi_ctrlr = kmalloc(phba->params.hwi_ws_sz, GFP_KERNEL);
	if (!phba->phwi_ctrlr)
		return -ENOMEM;

	phba->init_mem = kcalloc(SE_MEM_MAX, sizeof(*mem_descr),
				 GFP_KERNEL);
	if (!phba->init_mem) {
		kfree(phba->phwi_ctrlr);
		return -ENOMEM;
	}

	mem_arr_orig = kmalloc(sizeof(*mem_arr_orig) * BEISCSI_MAX_FRAGS_INIT,
			       GFP_KERNEL);
	if (!mem_arr_orig) {
		kfree(phba->init_mem);
		kfree(phba->phwi_ctrlr);
		return -ENOMEM;
	}

	mem_descr = phba->init_mem;
	for (i = 0; i < SE_MEM_MAX; i++) {
		j = 0;
		mem_arr = mem_arr_orig;
		alloc_size = phba->mem_req[i];
		memset(mem_arr, 0, sizeof(struct mem_array) *
		       BEISCSI_MAX_FRAGS_INIT);
		curr_alloc_size = min(be_max_phys_size * 1024, alloc_size);
		do {
			mem_arr->virtual_address = pci_alloc_consistent(
							phba->pcidev,
							curr_alloc_size,
							&bus_add);
			if (!mem_arr->virtual_address) {
				if (curr_alloc_size <= BE_MIN_MEM_SIZE)
					goto free_mem;
				if (curr_alloc_size -
					rounddown_pow_of_two(curr_alloc_size))
					curr_alloc_size = rounddown_pow_of_two
							     (curr_alloc_size);
				else
					curr_alloc_size = curr_alloc_size / 2;
			} else {
				mem_arr->bus_address.u.
				    a64.address = (__u64) bus_add;
				mem_arr->size = curr_alloc_size;
				alloc_size -= curr_alloc_size;
				curr_alloc_size = min(be_max_phys_size *
						      1024, alloc_size);
				j++;
				mem_arr++;
			}
		} while (alloc_size);
		mem_descr->num_elements = j;
		mem_descr->size_in_bytes = phba->mem_req[i];
		mem_descr->mem_array = kmalloc(sizeof(*mem_arr) * j,
					       GFP_KERNEL);
		if (!mem_descr->mem_array)
			goto free_mem;

		memcpy(mem_descr->mem_array, mem_arr_orig,
		       sizeof(struct mem_array) * j);
		mem_descr++;
	}
	kfree(mem_arr_orig);
	return 0;
free_mem:
	mem_descr->num_elements = j;
	while ((i) || (j)) {
		for (j = mem_descr->num_elements; j > 0; j--) {
			pci_free_consistent(phba->pcidev,
					    mem_descr->mem_array[j - 1].size,
					    mem_descr->mem_array[j - 1].
					    virtual_address,
					    mem_descr->mem_array[j - 1].
					    bus_address.u.a64.address);
		}
		if (i) {
			i--;
			kfree(mem_descr->mem_array);
			mem_descr--;
		}
	}
	kfree(mem_arr_orig);
	kfree(phba->init_mem);
	kfree(phba->phwi_ctrlr);
	return -ENOMEM;
}

static int beiscsi_get_memory(struct beiscsi_hba *phba)
{
	beiscsi_find_mem_req(phba);
	return beiscsi_alloc_mem(phba);
}

static void iscsi_init_global_templates(struct beiscsi_hba *phba)
{
	struct pdu_data_out *pdata_out;
	struct pdu_nop_out *pnop_out;
	struct be_mem_descriptor *mem_descr;

	mem_descr = phba->init_mem;
	mem_descr += ISCSI_MEM_GLOBAL_HEADER;
	pdata_out =
	    (struct pdu_data_out *)mem_descr->mem_array[0].virtual_address;
	memset(pdata_out, 0, BE_ISCSI_PDU_HEADER_SIZE);

	AMAP_SET_BITS(struct amap_pdu_data_out, opcode, pdata_out,
		      IIOC_SCSI_DATA);

	pnop_out =
	    (struct pdu_nop_out *)((unsigned char *)mem_descr->mem_array[0].
				   virtual_address + BE_ISCSI_PDU_HEADER_SIZE);

	memset(pnop_out, 0, BE_ISCSI_PDU_HEADER_SIZE);
	AMAP_SET_BITS(struct amap_pdu_nop_out, ttt, pnop_out, 0xFFFFFFFF);
	AMAP_SET_BITS(struct amap_pdu_nop_out, f_bit, pnop_out, 1);
	AMAP_SET_BITS(struct amap_pdu_nop_out, i_bit, pnop_out, 0);
}

static void beiscsi_init_wrb_handle(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr_wrbh, *mem_descr_wrb;
	struct wrb_handle *pwrb_handle;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_wrb_context *pwrb_context;
	struct iscsi_wrb *pwrb;
	unsigned int num_cxn_wrbh;
	unsigned int num_cxn_wrb, j, idx, index;

	mem_descr_wrbh = phba->init_mem;
	mem_descr_wrbh += HWI_MEM_WRBH;

	mem_descr_wrb = phba->init_mem;
	mem_descr_wrb += HWI_MEM_WRB;

	idx = 0;
	pwrb_handle = mem_descr_wrbh->mem_array[idx].virtual_address;
	num_cxn_wrbh = ((mem_descr_wrbh->mem_array[idx].size) /
			((sizeof(struct wrb_handle)) *
			 phba->params.wrbs_per_cxn));
	phwi_ctrlr = phba->phwi_ctrlr;

	for (index = 0; index < phba->params.cxns_per_ctrl * 2; index += 2) {
		pwrb_context = &phwi_ctrlr->wrb_context[index];
		pwrb_context->pwrb_handle_base =
				kzalloc(sizeof(struct wrb_handle *) *
					phba->params.wrbs_per_cxn, GFP_KERNEL);
		pwrb_context->pwrb_handle_basestd =
				kzalloc(sizeof(struct wrb_handle *) *
					phba->params.wrbs_per_cxn, GFP_KERNEL);
		if (num_cxn_wrbh) {
			pwrb_context->alloc_index = 0;
			pwrb_context->wrb_handles_available = 0;
			for (j = 0; j < phba->params.wrbs_per_cxn; j++) {
				pwrb_context->pwrb_handle_base[j] = pwrb_handle;
				pwrb_context->pwrb_handle_basestd[j] =
								pwrb_handle;
				pwrb_context->wrb_handles_available++;
				pwrb_handle->wrb_index = j;
				pwrb_handle++;
			}
			pwrb_context->free_index = 0;
			num_cxn_wrbh--;
		} else {
			idx++;
			pwrb_handle =
			    mem_descr_wrbh->mem_array[idx].virtual_address;
			num_cxn_wrbh =
			    ((mem_descr_wrbh->mem_array[idx].size) /
			     ((sizeof(struct wrb_handle)) *
			      phba->params.wrbs_per_cxn));
			pwrb_context->alloc_index = 0;
			for (j = 0; j < phba->params.wrbs_per_cxn; j++) {
				pwrb_context->pwrb_handle_base[j] = pwrb_handle;
				pwrb_context->pwrb_handle_basestd[j] =
				    pwrb_handle;
				pwrb_context->wrb_handles_available++;
				pwrb_handle->wrb_index = j;
				pwrb_handle++;
			}
			pwrb_context->free_index = 0;
			num_cxn_wrbh--;
		}
	}
	idx = 0;
	pwrb = mem_descr_wrb->mem_array[idx].virtual_address;
	num_cxn_wrb =
	    ((mem_descr_wrb->mem_array[idx].size) / (sizeof(struct iscsi_wrb)) *
	     phba->params.wrbs_per_cxn);

	for (index = 0; index < phba->params.cxns_per_ctrl; index += 2) {
		pwrb_context = &phwi_ctrlr->wrb_context[index];
		if (num_cxn_wrb) {
			for (j = 0; j < phba->params.wrbs_per_cxn; j++) {
				pwrb_handle = pwrb_context->pwrb_handle_base[j];
				pwrb_handle->pwrb = pwrb;
				pwrb++;
			}
			num_cxn_wrb--;
		} else {
			idx++;
			pwrb = mem_descr_wrb->mem_array[idx].virtual_address;
			num_cxn_wrb = ((mem_descr_wrb->mem_array[idx].size) /
					(sizeof(struct iscsi_wrb)) *
					phba->params.wrbs_per_cxn);
			for (j = 0; j < phba->params.wrbs_per_cxn; j++) {
				pwrb_handle = pwrb_context->pwrb_handle_base[j];
				pwrb_handle->pwrb = pwrb;
				pwrb++;
			}
			num_cxn_wrb--;
		}
	}
}

static void hwi_init_async_pdu_ctx(struct beiscsi_hba *phba)
{
	struct hwi_controller *phwi_ctrlr;
	struct hba_parameters *p = &phba->params;
	struct hwi_async_pdu_context *pasync_ctx;
	struct async_pdu_handle *pasync_header_h, *pasync_data_h;
	unsigned int index;
	struct be_mem_descriptor *mem_descr;

	mem_descr = (struct be_mem_descriptor *)phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_PDU_CONTEXT;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_ctrlr->phwi_ctxt->pasync_ctx = (struct hwi_async_pdu_context *)
				mem_descr->mem_array[0].virtual_address;
	pasync_ctx = phwi_ctrlr->phwi_ctxt->pasync_ctx;
	memset(pasync_ctx, 0, sizeof(*pasync_ctx));

	pasync_ctx->async_header.num_entries = p->asyncpdus_per_ctrl;
	pasync_ctx->async_header.buffer_size = p->defpdu_hdr_sz;
	pasync_ctx->async_data.buffer_size = p->defpdu_data_sz;
	pasync_ctx->async_data.num_entries = p->asyncpdus_per_ctrl;

	mem_descr = (struct be_mem_descriptor *)phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_HEADER_BUF;
	if (mem_descr->mem_array[0].virtual_address) {
		SE_DEBUG(DBG_LVL_8,
			 "hwi_init_async_pdu_ctx HWI_MEM_ASYNC_HEADER_BUF"
			 "va=%p \n", mem_descr->mem_array[0].virtual_address);
	} else
		shost_printk(KERN_WARNING, phba->shost,
			     "No Virtual address \n");

	pasync_ctx->async_header.va_base =
			mem_descr->mem_array[0].virtual_address;

	pasync_ctx->async_header.pa_base.u.a64.address =
			mem_descr->mem_array[0].bus_address.u.a64.address;

	mem_descr = (struct be_mem_descriptor *)phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_HEADER_RING;
	if (mem_descr->mem_array[0].virtual_address) {
		SE_DEBUG(DBG_LVL_8,
			 "hwi_init_async_pdu_ctx HWI_MEM_ASYNC_HEADER_RING"
			 "va=%p \n", mem_descr->mem_array[0].virtual_address);
	} else
		shost_printk(KERN_WARNING, phba->shost,
			    "No Virtual address \n");
	pasync_ctx->async_header.ring_base =
			mem_descr->mem_array[0].virtual_address;

	mem_descr = (struct be_mem_descriptor *)phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_HEADER_HANDLE;
	if (mem_descr->mem_array[0].virtual_address) {
		SE_DEBUG(DBG_LVL_8,
			 "hwi_init_async_pdu_ctx HWI_MEM_ASYNC_HEADER_HANDLE"
			 "va=%p \n", mem_descr->mem_array[0].virtual_address);
	} else
		shost_printk(KERN_WARNING, phba->shost,
			    "No Virtual address \n");

	pasync_ctx->async_header.handle_base =
			mem_descr->mem_array[0].virtual_address;
	pasync_ctx->async_header.writables = 0;
	INIT_LIST_HEAD(&pasync_ctx->async_header.free_list);

	mem_descr = (struct be_mem_descriptor *)phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_DATA_BUF;
	if (mem_descr->mem_array[0].virtual_address) {
		SE_DEBUG(DBG_LVL_8,
			 "hwi_init_async_pdu_ctx HWI_MEM_ASYNC_DATA_BUF"
			 "va=%p \n", mem_descr->mem_array[0].virtual_address);
	} else
		shost_printk(KERN_WARNING, phba->shost,
			    "No Virtual address \n");
	pasync_ctx->async_data.va_base =
			mem_descr->mem_array[0].virtual_address;
	pasync_ctx->async_data.pa_base.u.a64.address =
			mem_descr->mem_array[0].bus_address.u.a64.address;

	mem_descr = (struct be_mem_descriptor *)phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_DATA_RING;
	if (mem_descr->mem_array[0].virtual_address) {
		SE_DEBUG(DBG_LVL_8,
			 "hwi_init_async_pdu_ctx HWI_MEM_ASYNC_DATA_RING"
			 "va=%p \n", mem_descr->mem_array[0].virtual_address);
	} else
		shost_printk(KERN_WARNING, phba->shost,
			     "No Virtual address \n");

	pasync_ctx->async_data.ring_base =
			mem_descr->mem_array[0].virtual_address;

	mem_descr = (struct be_mem_descriptor *)phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_DATA_HANDLE;
	if (!mem_descr->mem_array[0].virtual_address)
		shost_printk(KERN_WARNING, phba->shost,
			    "No Virtual address \n");

	pasync_ctx->async_data.handle_base =
			mem_descr->mem_array[0].virtual_address;
	pasync_ctx->async_data.writables = 0;
	INIT_LIST_HEAD(&pasync_ctx->async_data.free_list);

	pasync_header_h =
		(struct async_pdu_handle *)pasync_ctx->async_header.handle_base;
	pasync_data_h =
		(struct async_pdu_handle *)pasync_ctx->async_data.handle_base;

	for (index = 0; index < p->asyncpdus_per_ctrl; index++) {
		pasync_header_h->cri = -1;
		pasync_header_h->index = (char)index;
		INIT_LIST_HEAD(&pasync_header_h->link);
		pasync_header_h->pbuffer =
			(void *)((unsigned long)
			(pasync_ctx->async_header.va_base) +
			(p->defpdu_hdr_sz * index));

		pasync_header_h->pa.u.a64.address =
			pasync_ctx->async_header.pa_base.u.a64.address +
			(p->defpdu_hdr_sz * index);

		list_add_tail(&pasync_header_h->link,
				&pasync_ctx->async_header.free_list);
		pasync_header_h++;
		pasync_ctx->async_header.free_entries++;
		pasync_ctx->async_header.writables++;

		INIT_LIST_HEAD(&pasync_ctx->async_entry[index].wait_queue.list);
		INIT_LIST_HEAD(&pasync_ctx->async_entry[index].
			       header_busy_list);
		pasync_data_h->cri = -1;
		pasync_data_h->index = (char)index;
		INIT_LIST_HEAD(&pasync_data_h->link);
		pasync_data_h->pbuffer =
			(void *)((unsigned long)
			(pasync_ctx->async_data.va_base) +
			(p->defpdu_data_sz * index));

		pasync_data_h->pa.u.a64.address =
		    pasync_ctx->async_data.pa_base.u.a64.address +
		    (p->defpdu_data_sz * index);

		list_add_tail(&pasync_data_h->link,
			      &pasync_ctx->async_data.free_list);
		pasync_data_h++;
		pasync_ctx->async_data.free_entries++;
		pasync_ctx->async_data.writables++;

		INIT_LIST_HEAD(&pasync_ctx->async_entry[index].data_busy_list);
	}

	pasync_ctx->async_header.host_write_ptr = 0;
	pasync_ctx->async_header.ep_read_ptr = -1;
	pasync_ctx->async_data.host_write_ptr = 0;
	pasync_ctx->async_data.ep_read_ptr = -1;
}

static int
be_sgl_create_contiguous(void *virtual_address,
			 u64 physical_address, u32 length,
			 struct be_dma_mem *sgl)
{
	WARN_ON(!virtual_address);
	WARN_ON(!physical_address);
	WARN_ON(!length > 0);
	WARN_ON(!sgl);

	sgl->va = virtual_address;
	sgl->dma = physical_address;
	sgl->size = length;

	return 0;
}

static void be_sgl_destroy_contiguous(struct be_dma_mem *sgl)
{
	memset(sgl, 0, sizeof(*sgl));
}

static void
hwi_build_be_sgl_arr(struct beiscsi_hba *phba,
		     struct mem_array *pmem, struct be_dma_mem *sgl)
{
	if (sgl->va)
		be_sgl_destroy_contiguous(sgl);

	be_sgl_create_contiguous(pmem->virtual_address,
				 pmem->bus_address.u.a64.address,
				 pmem->size, sgl);
}

static void
hwi_build_be_sgl_by_offset(struct beiscsi_hba *phba,
			   struct mem_array *pmem, struct be_dma_mem *sgl)
{
	if (sgl->va)
		be_sgl_destroy_contiguous(sgl);

	be_sgl_create_contiguous((unsigned char *)pmem->virtual_address,
				 pmem->bus_address.u.a64.address,
				 pmem->size, sgl);
}

static int be_fill_queue(struct be_queue_info *q,
		u16 len, u16 entry_size, void *vaddress)
{
	struct be_dma_mem *mem = &q->dma_mem;

	memset(q, 0, sizeof(*q));
	q->len = len;
	q->entry_size = entry_size;
	mem->size = len * entry_size;
	mem->va = vaddress;
	if (!mem->va)
		return -ENOMEM;
	memset(mem->va, 0, mem->size);
	return 0;
}

static int beiscsi_create_eqs(struct beiscsi_hba *phba,
			     struct hwi_context_memory *phwi_context)
{
	unsigned int i, num_eq_pages;
	int ret, eq_for_mcc;
	struct be_queue_info *eq;
	struct be_dma_mem *mem;
	void *eq_vaddress;
	dma_addr_t paddr;

	num_eq_pages = PAGES_REQUIRED(phba->params.num_eq_entries * \
				      sizeof(struct be_eq_entry));

	if (phba->msix_enabled)
		eq_for_mcc = 1;
	else
		eq_for_mcc = 0;
	for (i = 0; i < (phba->num_cpus + eq_for_mcc); i++) {
		eq = &phwi_context->be_eq[i].q;
		mem = &eq->dma_mem;
		phwi_context->be_eq[i].phba = phba;
		eq_vaddress = pci_alloc_consistent(phba->pcidev,
						     num_eq_pages * PAGE_SIZE,
						     &paddr);
		if (!eq_vaddress)
			goto create_eq_error;

		mem->va = eq_vaddress;
		ret = be_fill_queue(eq, phba->params.num_eq_entries,
				    sizeof(struct be_eq_entry), eq_vaddress);
		if (ret) {
			shost_printk(KERN_ERR, phba->shost,
				     "be_fill_queue Failed for EQ \n");
			goto create_eq_error;
		}

		mem->dma = paddr;
		ret = beiscsi_cmd_eq_create(&phba->ctrl, eq,
					    phwi_context->cur_eqd);
		if (ret) {
			shost_printk(KERN_ERR, phba->shost,
				     "beiscsi_cmd_eq_create"
				     "Failedfor EQ \n");
			goto create_eq_error;
		}
		SE_DEBUG(DBG_LVL_8, "eqid = %d\n", phwi_context->be_eq[i].q.id);
	}
	return 0;
create_eq_error:
	for (i = 0; i < (phba->num_cpus + 1); i++) {
		eq = &phwi_context->be_eq[i].q;
		mem = &eq->dma_mem;
		if (mem->va)
			pci_free_consistent(phba->pcidev, num_eq_pages
					    * PAGE_SIZE,
					    mem->va, mem->dma);
	}
	return ret;
}

static int beiscsi_create_cqs(struct beiscsi_hba *phba,
			     struct hwi_context_memory *phwi_context)
{
	unsigned int i, num_cq_pages;
	int ret;
	struct be_queue_info *cq, *eq;
	struct be_dma_mem *mem;
	struct be_eq_obj *pbe_eq;
	void *cq_vaddress;
	dma_addr_t paddr;

	num_cq_pages = PAGES_REQUIRED(phba->params.num_cq_entries * \
				      sizeof(struct sol_cqe));

	for (i = 0; i < phba->num_cpus; i++) {
		cq = &phwi_context->be_cq[i];
		eq = &phwi_context->be_eq[i].q;
		pbe_eq = &phwi_context->be_eq[i];
		pbe_eq->cq = cq;
		pbe_eq->phba = phba;
		mem = &cq->dma_mem;
		cq_vaddress = pci_alloc_consistent(phba->pcidev,
						     num_cq_pages * PAGE_SIZE,
						     &paddr);
		if (!cq_vaddress)
			goto create_cq_error;
		ret = be_fill_queue(cq, phba->params.icds_per_ctrl / 2,
				    sizeof(struct sol_cqe), cq_vaddress);
		if (ret) {
			shost_printk(KERN_ERR, phba->shost,
				     "be_fill_queue Failed for ISCSI CQ \n");
			goto create_cq_error;
		}

		mem->dma = paddr;
		ret = beiscsi_cmd_cq_create(&phba->ctrl, cq, eq, false,
					    false, 0);
		if (ret) {
			shost_printk(KERN_ERR, phba->shost,
				     "beiscsi_cmd_eq_create"
				     "Failed for ISCSI CQ \n");
			goto create_cq_error;
		}
		SE_DEBUG(DBG_LVL_8, "iscsi cq_id is %d for eq_id %d\n",
						 cq->id, eq->id);
		SE_DEBUG(DBG_LVL_8, "ISCSI CQ CREATED\n");
	}
	return 0;

create_cq_error:
	for (i = 0; i < phba->num_cpus; i++) {
		cq = &phwi_context->be_cq[i];
		mem = &cq->dma_mem;
		if (mem->va)
			pci_free_consistent(phba->pcidev, num_cq_pages
					    * PAGE_SIZE,
					    mem->va, mem->dma);
	}
	return ret;

}

static int
beiscsi_create_def_hdr(struct beiscsi_hba *phba,
		       struct hwi_context_memory *phwi_context,
		       struct hwi_controller *phwi_ctrlr,
		       unsigned int def_pdu_ring_sz)
{
	unsigned int idx;
	int ret;
	struct be_queue_info *dq, *cq;
	struct be_dma_mem *mem;
	struct be_mem_descriptor *mem_descr;
	void *dq_vaddress;

	idx = 0;
	dq = &phwi_context->be_def_hdrq;
	cq = &phwi_context->be_cq[0];
	mem = &dq->dma_mem;
	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_HEADER_RING;
	dq_vaddress = mem_descr->mem_array[idx].virtual_address;
	ret = be_fill_queue(dq, mem_descr->mem_array[0].size /
			    sizeof(struct phys_addr),
			    sizeof(struct phys_addr), dq_vaddress);
	if (ret) {
		shost_printk(KERN_ERR, phba->shost,
			     "be_fill_queue Failed for DEF PDU HDR\n");
		return ret;
	}
	mem->dma = mem_descr->mem_array[idx].bus_address.u.a64.address;
	ret = be_cmd_create_default_pdu_queue(&phba->ctrl, cq, dq,
					      def_pdu_ring_sz,
					      phba->params.defpdu_hdr_sz);
	if (ret) {
		shost_printk(KERN_ERR, phba->shost,
			     "be_cmd_create_default_pdu_queue Failed DEFHDR\n");
		return ret;
	}
	phwi_ctrlr->default_pdu_hdr.id = phwi_context->be_def_hdrq.id;
	SE_DEBUG(DBG_LVL_8, "iscsi def pdu id is %d\n",
		 phwi_context->be_def_hdrq.id);
	hwi_post_async_buffers(phba, 1);
	return 0;
}

static int
beiscsi_create_def_data(struct beiscsi_hba *phba,
			struct hwi_context_memory *phwi_context,
			struct hwi_controller *phwi_ctrlr,
			unsigned int def_pdu_ring_sz)
{
	unsigned int idx;
	int ret;
	struct be_queue_info *dataq, *cq;
	struct be_dma_mem *mem;
	struct be_mem_descriptor *mem_descr;
	void *dq_vaddress;

	idx = 0;
	dataq = &phwi_context->be_def_dataq;
	cq = &phwi_context->be_cq[0];
	mem = &dataq->dma_mem;
	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_ASYNC_DATA_RING;
	dq_vaddress = mem_descr->mem_array[idx].virtual_address;
	ret = be_fill_queue(dataq, mem_descr->mem_array[0].size /
			    sizeof(struct phys_addr),
			    sizeof(struct phys_addr), dq_vaddress);
	if (ret) {
		shost_printk(KERN_ERR, phba->shost,
			     "be_fill_queue Failed for DEF PDU DATA\n");
		return ret;
	}
	mem->dma = mem_descr->mem_array[idx].bus_address.u.a64.address;
	ret = be_cmd_create_default_pdu_queue(&phba->ctrl, cq, dataq,
					      def_pdu_ring_sz,
					      phba->params.defpdu_data_sz);
	if (ret) {
		shost_printk(KERN_ERR, phba->shost,
			     "be_cmd_create_default_pdu_queue Failed"
			     " for DEF PDU DATA\n");
		return ret;
	}
	phwi_ctrlr->default_pdu_data.id = phwi_context->be_def_dataq.id;
	SE_DEBUG(DBG_LVL_8, "iscsi def data id is %d\n",
		 phwi_context->be_def_dataq.id);
	hwi_post_async_buffers(phba, 0);
	SE_DEBUG(DBG_LVL_8, "DEFAULT PDU DATA RING CREATED \n");
	return 0;
}

static int
beiscsi_post_pages(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr;
	struct mem_array *pm_arr;
	unsigned int page_offset, i;
	struct be_dma_mem sgl;
	int status;

	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_SGE;
	pm_arr = mem_descr->mem_array;

	page_offset = (sizeof(struct iscsi_sge) * phba->params.num_sge_per_io *
			phba->fw_config.iscsi_icd_start) / PAGE_SIZE;
	for (i = 0; i < mem_descr->num_elements; i++) {
		hwi_build_be_sgl_arr(phba, pm_arr, &sgl);
		status = be_cmd_iscsi_post_sgl_pages(&phba->ctrl, &sgl,
						page_offset,
						(pm_arr->size / PAGE_SIZE));
		page_offset += pm_arr->size / PAGE_SIZE;
		if (status != 0) {
			shost_printk(KERN_ERR, phba->shost,
				     "post sgl failed.\n");
			return status;
		}
		pm_arr++;
	}
	SE_DEBUG(DBG_LVL_8, "POSTED PAGES \n");
	return 0;
}

static void be_queue_free(struct beiscsi_hba *phba, struct be_queue_info *q)
{
	struct be_dma_mem *mem = &q->dma_mem;
	if (mem->va)
		pci_free_consistent(phba->pcidev, mem->size,
			mem->va, mem->dma);
}

static int be_queue_alloc(struct beiscsi_hba *phba, struct be_queue_info *q,
		u16 len, u16 entry_size)
{
	struct be_dma_mem *mem = &q->dma_mem;

	memset(q, 0, sizeof(*q));
	q->len = len;
	q->entry_size = entry_size;
	mem->size = len * entry_size;
	mem->va = pci_alloc_consistent(phba->pcidev, mem->size, &mem->dma);
	if (!mem->va)
		return -1;
	memset(mem->va, 0, mem->size);
	return 0;
}

static int
beiscsi_create_wrb_rings(struct beiscsi_hba *phba,
			 struct hwi_context_memory *phwi_context,
			 struct hwi_controller *phwi_ctrlr)
{
	unsigned int wrb_mem_index, offset, size, num_wrb_rings;
	u64 pa_addr_lo;
	unsigned int idx, num, i;
	struct mem_array *pwrb_arr;
	void *wrb_vaddr;
	struct be_dma_mem sgl;
	struct be_mem_descriptor *mem_descr;
	int status;

	idx = 0;
	mem_descr = phba->init_mem;
	mem_descr += HWI_MEM_WRB;
	pwrb_arr = kmalloc(sizeof(*pwrb_arr) * phba->params.cxns_per_ctrl,
			   GFP_KERNEL);
	if (!pwrb_arr) {
		shost_printk(KERN_ERR, phba->shost,
			     "Memory alloc failed in create wrb ring.\n");
		return -ENOMEM;
	}
	wrb_vaddr = mem_descr->mem_array[idx].virtual_address;
	pa_addr_lo = mem_descr->mem_array[idx].bus_address.u.a64.address;
	num_wrb_rings = mem_descr->mem_array[idx].size /
		(phba->params.wrbs_per_cxn * sizeof(struct iscsi_wrb));

	for (num = 0; num < phba->params.cxns_per_ctrl; num++) {
		if (num_wrb_rings) {
			pwrb_arr[num].virtual_address = wrb_vaddr;
			pwrb_arr[num].bus_address.u.a64.address	= pa_addr_lo;
			pwrb_arr[num].size = phba->params.wrbs_per_cxn *
					    sizeof(struct iscsi_wrb);
			wrb_vaddr += pwrb_arr[num].size;
			pa_addr_lo += pwrb_arr[num].size;
			num_wrb_rings--;
		} else {
			idx++;
			wrb_vaddr = mem_descr->mem_array[idx].virtual_address;
			pa_addr_lo = mem_descr->mem_array[idx].\
					bus_address.u.a64.address;
			num_wrb_rings = mem_descr->mem_array[idx].size /
					(phba->params.wrbs_per_cxn *
					sizeof(struct iscsi_wrb));
			pwrb_arr[num].virtual_address = wrb_vaddr;
			pwrb_arr[num].bus_address.u.a64.address\
						= pa_addr_lo;
			pwrb_arr[num].size = phba->params.wrbs_per_cxn *
						 sizeof(struct iscsi_wrb);
			wrb_vaddr += pwrb_arr[num].size;
			pa_addr_lo   += pwrb_arr[num].size;
			num_wrb_rings--;
		}
	}
	for (i = 0; i < phba->params.cxns_per_ctrl; i++) {
		wrb_mem_index = 0;
		offset = 0;
		size = 0;

		hwi_build_be_sgl_by_offset(phba, &pwrb_arr[i], &sgl);
		status = be_cmd_wrbq_create(&phba->ctrl, &sgl,
					    &phwi_context->be_wrbq[i]);
		if (status != 0) {
			shost_printk(KERN_ERR, phba->shost,
				     "wrbq create failed.");
			return status;
		}
		phwi_ctrlr->wrb_context[i].cid = phwi_context->be_wrbq[i].id;
	}
	kfree(pwrb_arr);
	return 0;
}

static void free_wrb_handles(struct beiscsi_hba *phba)
{
	unsigned int index;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_wrb_context *pwrb_context;

	phwi_ctrlr = phba->phwi_ctrlr;
	for (index = 0; index < phba->params.cxns_per_ctrl * 2; index += 2) {
		pwrb_context = &phwi_ctrlr->wrb_context[index];
		kfree(pwrb_context->pwrb_handle_base);
		kfree(pwrb_context->pwrb_handle_basestd);
	}
}

static void be_mcc_queues_destroy(struct beiscsi_hba *phba)
{
	struct be_queue_info *q;
	struct be_ctrl_info *ctrl = &phba->ctrl;

	q = &phba->ctrl.mcc_obj.q;
	if (q->created)
		beiscsi_cmd_q_destroy(ctrl, q, QTYPE_MCCQ);
	be_queue_free(phba, q);

	q = &phba->ctrl.mcc_obj.cq;
	if (q->created)
		beiscsi_cmd_q_destroy(ctrl, q, QTYPE_CQ);
	be_queue_free(phba, q);
}

static void hwi_cleanup(struct beiscsi_hba *phba)
{
	struct be_queue_info *q;
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	int i, eq_num;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	for (i = 0; i < phba->params.cxns_per_ctrl; i++) {
		q = &phwi_context->be_wrbq[i];
		if (q->created)
			beiscsi_cmd_q_destroy(ctrl, q, QTYPE_WRBQ);
	}
	free_wrb_handles(phba);

	q = &phwi_context->be_def_hdrq;
	if (q->created)
		beiscsi_cmd_q_destroy(ctrl, q, QTYPE_DPDUQ);

	q = &phwi_context->be_def_dataq;
	if (q->created)
		beiscsi_cmd_q_destroy(ctrl, q, QTYPE_DPDUQ);

	beiscsi_cmd_q_destroy(ctrl, NULL, QTYPE_SGL);

	for (i = 0; i < (phba->num_cpus); i++) {
		q = &phwi_context->be_cq[i];
		if (q->created)
			beiscsi_cmd_q_destroy(ctrl, q, QTYPE_CQ);
	}
	if (phba->msix_enabled)
		eq_num = 1;
	else
		eq_num = 0;
	for (i = 0; i < (phba->num_cpus + eq_num); i++) {
		q = &phwi_context->be_eq[i].q;
		if (q->created)
			beiscsi_cmd_q_destroy(ctrl, q, QTYPE_EQ);
	}
	be_mcc_queues_destroy(phba);
}

static int be_mcc_queues_create(struct beiscsi_hba *phba,
				struct hwi_context_memory *phwi_context)
{
	struct be_queue_info *q, *cq;
	struct be_ctrl_info *ctrl = &phba->ctrl;

	/* Alloc MCC compl queue */
	cq = &phba->ctrl.mcc_obj.cq;
	if (be_queue_alloc(phba, cq, MCC_CQ_LEN,
			sizeof(struct be_mcc_compl)))
		goto err;
	/* Ask BE to create MCC compl queue; */
	if (phba->msix_enabled) {
		if (beiscsi_cmd_cq_create(ctrl, cq, &phwi_context->be_eq
					 [phba->num_cpus].q, false, true, 0))
		goto mcc_cq_free;
	} else {
		if (beiscsi_cmd_cq_create(ctrl, cq, &phwi_context->be_eq[0].q,
					  false, true, 0))
		goto mcc_cq_free;
	}

	/* Alloc MCC queue */
	q = &phba->ctrl.mcc_obj.q;
	if (be_queue_alloc(phba, q, MCC_Q_LEN, sizeof(struct be_mcc_wrb)))
		goto mcc_cq_destroy;

	/* Ask BE to create MCC queue */
	if (beiscsi_cmd_mccq_create(phba, q, cq))
		goto mcc_q_free;

	return 0;

mcc_q_free:
	be_queue_free(phba, q);
mcc_cq_destroy:
	beiscsi_cmd_q_destroy(ctrl, cq, QTYPE_CQ);
mcc_cq_free:
	be_queue_free(phba, cq);
err:
	return -1;
}

static int find_num_cpus(void)
{
	int  num_cpus = 0;

	num_cpus = num_online_cpus();
	if (num_cpus >= MAX_CPUS)
		num_cpus = MAX_CPUS - 1;

	SE_DEBUG(DBG_LVL_8, "num_cpus = %d \n", num_cpus);
	return num_cpus;
}

static int hwi_init_port(struct beiscsi_hba *phba)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	unsigned int def_pdu_ring_sz;
	struct be_ctrl_info *ctrl = &phba->ctrl;
	int status;

	def_pdu_ring_sz =
		phba->params.asyncpdus_per_ctrl * sizeof(struct phys_addr);
	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	phwi_context->max_eqd = 0;
	phwi_context->min_eqd = 0;
	phwi_context->cur_eqd = 64;
	be_cmd_fw_initialize(&phba->ctrl);

	status = beiscsi_create_eqs(phba, phwi_context);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost, "EQ not created \n");
		goto error;
	}

	status = be_mcc_queues_create(phba, phwi_context);
	if (status != 0)
		goto error;

	status = mgmt_check_supported_fw(ctrl, phba);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "Unsupported fw version \n");
		goto error;
	}

	if (phba->fw_config.iscsi_features == 0x1)
		ring_mode = 1;
	else
		ring_mode = 0;
	status = mgmt_get_fw_config(ctrl, phba);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "Error getting fw config\n");
		goto error;
	}

	status = beiscsi_create_cqs(phba, phwi_context);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost, "CQ not created\n");
		goto error;
	}

	status = beiscsi_create_def_hdr(phba, phwi_context, phwi_ctrlr,
					def_pdu_ring_sz);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "Default Header not created\n");
		goto error;
	}

	status = beiscsi_create_def_data(phba, phwi_context,
					 phwi_ctrlr, def_pdu_ring_sz);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "Default Data not created\n");
		goto error;
	}

	status = beiscsi_post_pages(phba);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost, "Post SGL Pages Failed\n");
		goto error;
	}

	status = beiscsi_create_wrb_rings(phba,	phwi_context, phwi_ctrlr);
	if (status != 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "WRB Rings not created\n");
		goto error;
	}

	SE_DEBUG(DBG_LVL_8, "hwi_init_port success\n");
	return 0;

error:
	shost_printk(KERN_ERR, phba->shost, "hwi_init_port failed");
	hwi_cleanup(phba);
	return -ENOMEM;
}

static int hwi_init_controller(struct beiscsi_hba *phba)
{
	struct hwi_controller *phwi_ctrlr;

	phwi_ctrlr = phba->phwi_ctrlr;
	if (1 == phba->init_mem[HWI_MEM_ADDN_CONTEXT].num_elements) {
		phwi_ctrlr->phwi_ctxt = (struct hwi_context_memory *)phba->
		    init_mem[HWI_MEM_ADDN_CONTEXT].mem_array[0].virtual_address;
		SE_DEBUG(DBG_LVL_8, " phwi_ctrlr->phwi_ctxt=%p \n",
			 phwi_ctrlr->phwi_ctxt);
	} else {
		shost_printk(KERN_ERR, phba->shost,
			     "HWI_MEM_ADDN_CONTEXT is more than one element."
			     "Failing to load\n");
		return -ENOMEM;
	}

	iscsi_init_global_templates(phba);
	beiscsi_init_wrb_handle(phba);
	hwi_init_async_pdu_ctx(phba);
	if (hwi_init_port(phba) != 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "hwi_init_controller failed\n");
		return -ENOMEM;
	}
	return 0;
}

static void beiscsi_free_mem(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr;
	int i, j;

	mem_descr = phba->init_mem;
	i = 0;
	j = 0;
	for (i = 0; i < SE_MEM_MAX; i++) {
		for (j = mem_descr->num_elements; j > 0; j--) {
			pci_free_consistent(phba->pcidev,
			  mem_descr->mem_array[j - 1].size,
			  mem_descr->mem_array[j - 1].virtual_address,
			  mem_descr->mem_array[j - 1].bus_address.
				u.a64.address);
		}
		kfree(mem_descr->mem_array);
		mem_descr++;
	}
	kfree(phba->init_mem);
	kfree(phba->phwi_ctrlr);
}

static int beiscsi_init_controller(struct beiscsi_hba *phba)
{
	int ret = -ENOMEM;

	ret = beiscsi_get_memory(phba);
	if (ret < 0) {
		shost_printk(KERN_ERR, phba->shost, "beiscsi_dev_probe -"
			     "Failed in beiscsi_alloc_memory \n");
		return ret;
	}

	ret = hwi_init_controller(phba);
	if (ret)
		goto free_init;
	SE_DEBUG(DBG_LVL_8, "Return success from beiscsi_init_controller");
	return 0;

free_init:
	beiscsi_free_mem(phba);
	return -ENOMEM;
}

static int beiscsi_init_sgl_handle(struct beiscsi_hba *phba)
{
	struct be_mem_descriptor *mem_descr_sglh, *mem_descr_sg;
	struct sgl_handle *psgl_handle;
	struct iscsi_sge *pfrag;
	unsigned int arr_index, i, idx;

	phba->io_sgl_hndl_avbl = 0;
	phba->eh_sgl_hndl_avbl = 0;

	if (ring_mode) {
		phba->sgl_hndl_array = kzalloc(sizeof(struct sgl_handle *) *
					      phba->params.icds_per_ctrl,
						 GFP_KERNEL);
		if (!phba->sgl_hndl_array) {
			shost_printk(KERN_ERR, phba->shost,
			     "Mem Alloc Failed. Failing to load\n");
			return -ENOMEM;
		}
	}

	mem_descr_sglh = phba->init_mem;
	mem_descr_sglh += HWI_MEM_SGLH;
	if (1 == mem_descr_sglh->num_elements) {
		phba->io_sgl_hndl_base = kzalloc(sizeof(struct sgl_handle *) *
						 phba->params.ios_per_ctrl,
						 GFP_KERNEL);
		if (!phba->io_sgl_hndl_base) {
			if (ring_mode)
				kfree(phba->sgl_hndl_array);
			shost_printk(KERN_ERR, phba->shost,
				     "Mem Alloc Failed. Failing to load\n");
			return -ENOMEM;
		}
		phba->eh_sgl_hndl_base = kzalloc(sizeof(struct sgl_handle *) *
						 (phba->params.icds_per_ctrl -
						 phba->params.ios_per_ctrl),
						 GFP_KERNEL);
		if (!phba->eh_sgl_hndl_base) {
			kfree(phba->io_sgl_hndl_base);
			shost_printk(KERN_ERR, phba->shost,
				     "Mem Alloc Failed. Failing to load\n");
			return -ENOMEM;
		}
	} else {
		shost_printk(KERN_ERR, phba->shost,
			     "HWI_MEM_SGLH is more than one element."
			     "Failing to load\n");
		return -ENOMEM;
	}

	arr_index = 0;
	idx = 0;
	while (idx < mem_descr_sglh->num_elements) {
		psgl_handle = mem_descr_sglh->mem_array[idx].virtual_address;

		for (i = 0; i < (mem_descr_sglh->mem_array[idx].size /
		      sizeof(struct sgl_handle)); i++) {
			if (arr_index < phba->params.ios_per_ctrl) {
				phba->io_sgl_hndl_base[arr_index] = psgl_handle;
				phba->io_sgl_hndl_avbl++;
				arr_index++;
			} else {
				phba->eh_sgl_hndl_base[arr_index -
					phba->params.ios_per_ctrl] =
								psgl_handle;
				arr_index++;
				phba->eh_sgl_hndl_avbl++;
			}
			psgl_handle++;
		}
		idx++;
	}
	SE_DEBUG(DBG_LVL_8,
		 "phba->io_sgl_hndl_avbl=%d"
		 "phba->eh_sgl_hndl_avbl=%d \n",
		 phba->io_sgl_hndl_avbl,
		 phba->eh_sgl_hndl_avbl);
	mem_descr_sg = phba->init_mem;
	mem_descr_sg += HWI_MEM_SGE;
	SE_DEBUG(DBG_LVL_8, "\n mem_descr_sg->num_elements=%d \n",
		 mem_descr_sg->num_elements);
	arr_index = 0;
	idx = 0;
	while (idx < mem_descr_sg->num_elements) {
		pfrag = mem_descr_sg->mem_array[idx].virtual_address;

		for (i = 0;
		     i < (mem_descr_sg->mem_array[idx].size) /
		     (sizeof(struct iscsi_sge) * phba->params.num_sge_per_io);
		     i++) {
			if (arr_index < phba->params.ios_per_ctrl)
				psgl_handle = phba->io_sgl_hndl_base[arr_index];
			else
				psgl_handle = phba->eh_sgl_hndl_base[arr_index -
						phba->params.ios_per_ctrl];
			psgl_handle->pfrag = pfrag;
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_hi, pfrag, 0);
			AMAP_SET_BITS(struct amap_iscsi_sge, addr_lo, pfrag, 0);
			pfrag += phba->params.num_sge_per_io;
			psgl_handle->sgl_index =
				phba->fw_config.iscsi_cid_start + arr_index++;
		}
		idx++;
	}
	phba->io_sgl_free_index = 0;
	phba->io_sgl_alloc_index = 0;
	phba->eh_sgl_free_index = 0;
	phba->eh_sgl_alloc_index = 0;
	return 0;
}

static int hba_setup_cid_tbls(struct beiscsi_hba *phba)
{
	int i, new_cid;

	phba->cid_array = kmalloc(sizeof(void *) * phba->params.cxns_per_ctrl,
				  GFP_KERNEL);
	if (!phba->cid_array) {
		shost_printk(KERN_ERR, phba->shost,
			     "Failed to allocate memory in "
			     "hba_setup_cid_tbls\n");
		return -ENOMEM;
	}
	phba->ep_array = kmalloc(sizeof(struct iscsi_endpoint *) *
				 phba->params.cxns_per_ctrl * 2, GFP_KERNEL);
	if (!phba->ep_array) {
		shost_printk(KERN_ERR, phba->shost,
			     "Failed to allocate memory in "
			     "hba_setup_cid_tbls \n");
		kfree(phba->cid_array);
		return -ENOMEM;
	}
	new_cid = phba->fw_config.iscsi_icd_start;
	for (i = 0; i < phba->params.cxns_per_ctrl; i++) {
		phba->cid_array[i] = new_cid;
		new_cid += 2;
	}
	phba->avlbl_cids = phba->params.cxns_per_ctrl;
	return 0;
}

static unsigned char hwi_enable_intr(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_queue_info *eq;
	u8 __iomem *addr;
	u32 reg, i;
	u32 enabled;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;

	addr = (u8 __iomem *) ((u8 __iomem *) ctrl->pcicfg +
			PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET);
	reg = ioread32(addr);
	SE_DEBUG(DBG_LVL_8, "reg =x%08x \n", reg);

	enabled = reg & MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
	if (!enabled) {
		reg |= MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
		SE_DEBUG(DBG_LVL_8, "reg =x%08x addr=%p \n", reg, addr);
		iowrite32(reg, addr);
		for (i = 0; i <= phba->num_cpus; i++) {
			eq = &phwi_context->be_eq[i].q;
			SE_DEBUG(DBG_LVL_8, "eq->id=%d \n", eq->id);
			hwi_ring_eq_db(phba, eq->id, 0, 0, 1, 1);
		}
	} else
		shost_printk(KERN_WARNING, phba->shost,
			     "In hwi_enable_intr, Not Enabled \n");
	return true;
}

static void hwi_disable_intr(struct beiscsi_hba *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;

	u8 __iomem *addr = ctrl->pcicfg + PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET;
	u32 reg = ioread32(addr);

	u32 enabled = reg & MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
	if (enabled) {
		reg &= ~MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
		iowrite32(reg, addr);
	} else
		shost_printk(KERN_WARNING, phba->shost,
			     "In hwi_disable_intr, Already Disabled \n");
}

static int beiscsi_init_port(struct beiscsi_hba *phba)
{
	int ret;

	ret = beiscsi_init_controller(phba);
	if (ret < 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "beiscsi_dev_probe - Failed in"
			     "beiscsi_init_controller \n");
		return ret;
	}
	ret = beiscsi_init_sgl_handle(phba);
	if (ret < 0) {
		shost_printk(KERN_ERR, phba->shost,
			     "beiscsi_dev_probe - Failed in"
			     "beiscsi_init_sgl_handle \n");
		goto do_cleanup_ctrlr;
	}

	if (hba_setup_cid_tbls(phba)) {
		shost_printk(KERN_ERR, phba->shost,
			     "Failed in hba_setup_cid_tbls\n");
		if (ring_mode)
			kfree(phba->sgl_hndl_array);
		kfree(phba->io_sgl_hndl_base);
		kfree(phba->eh_sgl_hndl_base);
		goto do_cleanup_ctrlr;
	}

	return ret;

do_cleanup_ctrlr:
	hwi_cleanup(phba);
	return ret;
}

static void hwi_purge_eq(struct beiscsi_hba *phba)
{
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_queue_info *eq;
	struct be_eq_entry *eqe = NULL;
	int i, eq_msix;

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	if (phba->msix_enabled)
		eq_msix = 1;
	else
		eq_msix = 0;

	for (i = 0; i < (phba->num_cpus + eq_msix); i++) {
		eq = &phwi_context->be_eq[i].q;
		eqe = queue_tail_node(eq);

		while (eqe->dw[offsetof(struct amap_eq_entry, valid) / 32]
					& EQE_VALID_MASK) {
			AMAP_SET_BITS(struct amap_eq_entry, valid, eqe, 0);
			queue_tail_inc(eq);
			eqe = queue_tail_node(eq);
		}
	}
}

static void beiscsi_clean_port(struct beiscsi_hba *phba)
{
	unsigned char mgmt_status;

	mgmt_status = mgmt_epfw_cleanup(phba, CMD_CONNECTION_CHUTE_0);
	if (mgmt_status)
		shost_printk(KERN_WARNING, phba->shost,
			     "mgmt_epfw_cleanup FAILED \n");
	hwi_cleanup(phba);
	hwi_purge_eq(phba);
	if (ring_mode)
		kfree(phba->sgl_hndl_array);
	kfree(phba->io_sgl_hndl_base);
	kfree(phba->eh_sgl_hndl_base);
	kfree(phba->cid_array);
	kfree(phba->ep_array);
}

void
beiscsi_offload_connection(struct beiscsi_conn *beiscsi_conn,
			   struct beiscsi_offload_params *params)
{
	struct wrb_handle *pwrb_handle;
	struct iscsi_target_context_update_wrb *pwrb = NULL;
	struct be_mem_descriptor *mem_descr;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	u32 doorbell = 0;

	/*
	 * We can always use 0 here because it is reserved by libiscsi for
	 * login/startup related tasks.
	 */
	pwrb_handle = alloc_wrb_handle(phba, beiscsi_conn->beiscsi_conn_cid, 0);
	pwrb = (struct iscsi_target_context_update_wrb *)pwrb_handle->pwrb;
	memset(pwrb, 0, sizeof(*pwrb));
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      max_burst_length, pwrb, params->dw[offsetof
		      (struct amap_beiscsi_offload_params,
		      max_burst_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      max_send_data_segment_length, pwrb,
		      params->dw[offsetof(struct amap_beiscsi_offload_params,
		      max_send_data_segment_length) / 32]);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
		      first_burst_length,
		      pwrb,
		      params->dw[offsetof(struct amap_beiscsi_offload_params,
		      first_burst_length) / 32]);

	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, erl, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      erl) / 32] & OFFLD_PARAMS_ERL));
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, dde, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      dde) / 32] & OFFLD_PARAMS_DDE) >> 2);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, hde, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      hde) / 32] & OFFLD_PARAMS_HDE) >> 3);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, ir2t, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      ir2t) / 32] & OFFLD_PARAMS_IR2T) >> 4);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, imd, pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		       imd) / 32] & OFFLD_PARAMS_IMD) >> 5);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, stat_sn,
		      pwrb,
		      (params->dw[offsetof(struct amap_beiscsi_offload_params,
		      exp_statsn) / 32] + 1));
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, type, pwrb,
		      0x7);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, wrb_idx,
		      pwrb, pwrb_handle->wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, ptr2nextwrb,
		      pwrb, pwrb_handle->nxt_wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
			session_state, pwrb, 0);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, compltonack,
		      pwrb, 1);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, notpredblq,
		      pwrb, 0);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb, mode, pwrb,
		      0);

	mem_descr = phba->init_mem;
	mem_descr += ISCSI_MEM_GLOBAL_HEADER;

	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
			pad_buffer_addr_hi, pwrb,
		      mem_descr->mem_array[0].bus_address.u.a32.address_hi);
	AMAP_SET_BITS(struct amap_iscsi_target_context_update_wrb,
			pad_buffer_addr_lo, pwrb,
		      mem_descr->mem_array[0].bus_address.u.a32.address_lo);

	be_dws_le_to_cpu(pwrb, sizeof(struct iscsi_target_context_update_wrb));

	doorbell |= beiscsi_conn->beiscsi_conn_cid & DB_WRB_POST_CID_MASK;
	if (!ring_mode)
		doorbell |= (pwrb_handle->wrb_index & DB_DEF_PDU_WRB_INDEX_MASK)
			     << DB_DEF_PDU_WRB_INDEX_SHIFT;
	doorbell |= 1 << DB_DEF_PDU_NUM_POSTED_SHIFT;

	iowrite32(doorbell, phba->db_va + DB_TXULP0_OFFSET);
}

static void beiscsi_parse_pdu(struct iscsi_conn *conn, itt_t itt,
			      int *index, int *age)
{
	*index = (int)itt;
	if (age)
		*age = conn->session->age;
}

/**
 * beiscsi_alloc_pdu - allocates pdu and related resources
 * @task: libiscsi task
 * @opcode: opcode of pdu for task
 *
 * This is called with the session lock held. It will allocate
 * the wrb and sgl if needed for the command. And it will prep
 * the pdu's itt. beiscsi_parse_pdu will later translate
 * the pdu itt to the libiscsi task itt.
 */
static int beiscsi_alloc_pdu(struct iscsi_task *task, uint8_t opcode)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;
	itt_t itt;
	struct beiscsi_session *beiscsi_sess = beiscsi_conn->beiscsi_sess;
	dma_addr_t paddr;

	io_task->cmd_bhs = pci_pool_alloc(beiscsi_sess->bhs_pool,
					  GFP_KERNEL, &paddr);
	if (!io_task->cmd_bhs)
		return -ENOMEM;
	io_task->bhs_pa.u.a64.address = paddr;
	io_task->libiscsi_itt = (itt_t)task->itt;
	io_task->pwrb_handle = alloc_wrb_handle(phba,
						beiscsi_conn->beiscsi_conn_cid,
						task->itt);
	io_task->conn = beiscsi_conn;

	task->hdr = (struct iscsi_hdr *)&io_task->cmd_bhs->iscsi_hdr;
	task->hdr_max = sizeof(struct be_cmd_bhs);

	if (task->sc) {
		spin_lock(&phba->io_sgl_lock);
		io_task->psgl_handle = alloc_io_sgl_handle(phba);
		spin_unlock(&phba->io_sgl_lock);
		if (!io_task->psgl_handle)
			goto free_hndls;
	} else {
		io_task->scsi_cmnd = NULL;
		if ((task->hdr->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGIN) {
			if (!beiscsi_conn->login_in_progress) {
				spin_lock(&phba->mgmt_sgl_lock);
				io_task->psgl_handle = (struct sgl_handle *)
						alloc_mgmt_sgl_handle(phba);
				spin_unlock(&phba->mgmt_sgl_lock);
				if (!io_task->psgl_handle)
					goto free_hndls;

				beiscsi_conn->login_in_progress = 1;
				beiscsi_conn->plogin_sgl_handle =
							io_task->psgl_handle;
			} else {
				io_task->psgl_handle =
						beiscsi_conn->plogin_sgl_handle;
			}
		} else {
			spin_lock(&phba->mgmt_sgl_lock);
			io_task->psgl_handle = alloc_mgmt_sgl_handle(phba);
			spin_unlock(&phba->mgmt_sgl_lock);
			if (!io_task->psgl_handle)
				goto free_hndls;
		}
	}
	itt = (itt_t) cpu_to_be32(((unsigned int)io_task->pwrb_handle->
				 wrb_index << 16) | (unsigned int)
				(io_task->psgl_handle->sgl_index));
	if (ring_mode) {
		phba->sgl_hndl_array[io_task->psgl_handle->sgl_index -
				     phba->fw_config.iscsi_cid_start] =
				     io_task->psgl_handle;
		io_task->psgl_handle->task = task;
		io_task->psgl_handle->cid = beiscsi_conn->beiscsi_conn_cid;
	} else
		io_task->pwrb_handle->pio_handle = task;

	io_task->cmd_bhs->iscsi_hdr.itt = itt;
	return 0;

free_hndls:
	phwi_ctrlr = phba->phwi_ctrlr;
	pwrb_context = &phwi_ctrlr->wrb_context[beiscsi_conn->beiscsi_conn_cid];
	free_wrb_handle(phba, pwrb_context, io_task->pwrb_handle);
	io_task->pwrb_handle = NULL;
	pci_pool_free(beiscsi_sess->bhs_pool, io_task->cmd_bhs,
		      io_task->bhs_pa.u.a64.address);
	SE_DEBUG(DBG_LVL_1, "Alloc of SGL_ICD Failed \n");
	return -ENOMEM;
}

static void beiscsi_cleanup_task(struct iscsi_task *task)
{
	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct beiscsi_session *beiscsi_sess = beiscsi_conn->beiscsi_sess;
	struct hwi_wrb_context *pwrb_context;
	struct hwi_controller *phwi_ctrlr;

	phwi_ctrlr = phba->phwi_ctrlr;
	pwrb_context = &phwi_ctrlr->wrb_context[beiscsi_conn->beiscsi_conn_cid];
	if (io_task->pwrb_handle) {
		free_wrb_handle(phba, pwrb_context, io_task->pwrb_handle);
		io_task->pwrb_handle = NULL;
	}

	if (io_task->cmd_bhs) {
		pci_pool_free(beiscsi_sess->bhs_pool, io_task->cmd_bhs,
			      io_task->bhs_pa.u.a64.address);
	}

	if (task->sc) {
		if (io_task->psgl_handle) {
			spin_lock(&phba->io_sgl_lock);
			free_io_sgl_handle(phba, io_task->psgl_handle);
			spin_unlock(&phba->io_sgl_lock);
			io_task->psgl_handle = NULL;
		}
	} else {
		if ((task->hdr->opcode & ISCSI_OPCODE_MASK) == ISCSI_OP_LOGIN)
			return;
		if (io_task->psgl_handle) {
			spin_lock(&phba->mgmt_sgl_lock);
			free_mgmt_sgl_handle(phba, io_task->psgl_handle);
			spin_unlock(&phba->mgmt_sgl_lock);
			io_task->psgl_handle = NULL;
		}
	}
}

static int beiscsi_iotask(struct iscsi_task *task, struct scatterlist *sg,
			  unsigned int num_sg, unsigned int xferlen,
			  unsigned int writedir)
{

	struct beiscsi_io_task *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct iscsi_wrb *pwrb = NULL;
	unsigned int doorbell = 0;

	pwrb = io_task->pwrb_handle->pwrb;
	io_task->cmd_bhs->iscsi_hdr.exp_statsn = 0;
	io_task->bhs_len = sizeof(struct be_cmd_bhs);

	if (writedir) {
		memset(&io_task->cmd_bhs->iscsi_data_pdu, 0, 48);
		AMAP_SET_BITS(struct amap_pdu_data_out, itt,
			      &io_task->cmd_bhs->iscsi_data_pdu,
			      (unsigned int)io_task->cmd_bhs->iscsi_hdr.itt);
		AMAP_SET_BITS(struct amap_pdu_data_out, opcode,
			      &io_task->cmd_bhs->iscsi_data_pdu,
			      ISCSI_OPCODE_SCSI_DATA_OUT);
		AMAP_SET_BITS(struct amap_pdu_data_out, final_bit,
			      &io_task->cmd_bhs->iscsi_data_pdu, 1);
		if (ring_mode)
			io_task->psgl_handle->type = INI_WR_CMD;
		else
			AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
				      INI_WR_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 1);
	} else {
		if (ring_mode)
			io_task->psgl_handle->type = INI_RD_CMD;
		else
			AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
				      INI_RD_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 0);
	}
	memcpy(&io_task->cmd_bhs->iscsi_data_pdu.
	       dw[offsetof(struct amap_pdu_data_out, lun) / 32],
	       io_task->cmd_bhs->iscsi_hdr.lun, sizeof(struct scsi_lun));

	AMAP_SET_BITS(struct amap_iscsi_wrb, lun, pwrb,
		      cpu_to_be16((unsigned short)io_task->cmd_bhs->iscsi_hdr.
				  lun[0]));
	AMAP_SET_BITS(struct amap_iscsi_wrb, r2t_exp_dtl, pwrb, xferlen);
	AMAP_SET_BITS(struct amap_iscsi_wrb, wrb_idx, pwrb,
		      io_task->pwrb_handle->wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_wrb, cmdsn_itt, pwrb,
		      be32_to_cpu(task->cmdsn));
	AMAP_SET_BITS(struct amap_iscsi_wrb, sgl_icd_idx, pwrb,
		      io_task->psgl_handle->sgl_index);

	hwi_write_sgl(pwrb, sg, num_sg, io_task);

	AMAP_SET_BITS(struct amap_iscsi_wrb, ptr2nextwrb, pwrb,
		      io_task->pwrb_handle->nxt_wrb_index);
	be_dws_le_to_cpu(pwrb, sizeof(struct iscsi_wrb));

	doorbell |= beiscsi_conn->beiscsi_conn_cid & DB_WRB_POST_CID_MASK;
	if (!ring_mode)
		doorbell |= (io_task->pwrb_handle->wrb_index &
		     DB_DEF_PDU_WRB_INDEX_MASK) << DB_DEF_PDU_WRB_INDEX_SHIFT;
	doorbell |= 1 << DB_DEF_PDU_NUM_POSTED_SHIFT;

	iowrite32(doorbell, phba->db_va + DB_TXULP0_OFFSET);
	return 0;
}

static int beiscsi_mtask(struct iscsi_task *task)
{
	struct beiscsi_io_task *aborted_io_task, *io_task = task->dd_data;
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct beiscsi_hba *phba = beiscsi_conn->phba;
	struct iscsi_session *session;
	struct iscsi_wrb *pwrb = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_wrb_context *pwrb_context;
	struct wrb_handle *pwrb_handle;
	unsigned int doorbell = 0;
	unsigned int i, cid;
	struct iscsi_task *aborted_task;

	cid = beiscsi_conn->beiscsi_conn_cid;
	pwrb = io_task->pwrb_handle->pwrb;
	AMAP_SET_BITS(struct amap_iscsi_wrb, cmdsn_itt, pwrb,
		      be32_to_cpu(task->cmdsn));
	AMAP_SET_BITS(struct amap_iscsi_wrb, wrb_idx, pwrb,
		      io_task->pwrb_handle->wrb_index);
	AMAP_SET_BITS(struct amap_iscsi_wrb, sgl_icd_idx, pwrb,
		      io_task->psgl_handle->sgl_index);

	switch (task->hdr->opcode & ISCSI_OPCODE_MASK) {
	case ISCSI_OP_LOGIN:
		if (ring_mode)
			io_task->psgl_handle->type = TGT_DM_CMD;
		else
			AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
				      TGT_DM_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb, dmsg, pwrb, 0);
		AMAP_SET_BITS(struct amap_iscsi_wrb, cmdsn_itt, pwrb, 1);
		hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_NOOP_OUT:
		if (ring_mode)
			io_task->psgl_handle->type = INI_RD_CMD;
		else
			AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
				      INI_RD_CMD);
		hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_TEXT:
		if (ring_mode)
			io_task->psgl_handle->type = INI_WR_CMD;
		else
			AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
				      INI_WR_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb, dsp, pwrb, 1);
		hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		session = conn->session;
		i = ((struct iscsi_tm *)task->hdr)->rtt;
		phwi_ctrlr = phba->phwi_ctrlr;
		pwrb_context = &phwi_ctrlr->wrb_context[cid];
		pwrb_handle = pwrb_context->pwrb_handle_basestd[be32_to_cpu(i)
								>> 16];
		aborted_task = pwrb_handle->pio_handle;
		 if (!aborted_task)
			return 0;

		aborted_io_task = aborted_task->dd_data;
		if (!aborted_io_task->scsi_cmnd)
			return 0;

		mgmt_invalidate_icds(phba,
				     aborted_io_task->psgl_handle->sgl_index,
				     cid);
		if (ring_mode)
			io_task->psgl_handle->type = INI_TMF_CMD;
		else
			AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
				      INI_TMF_CMD);
		AMAP_SET_BITS(struct amap_iscsi_wrb, dmsg, pwrb, 0);
		hwi_write_buffer(pwrb, task);
		break;
	case ISCSI_OP_LOGOUT:
		AMAP_SET_BITS(struct amap_iscsi_wrb, dmsg, pwrb, 0);
		if (ring_mode)
			io_task->psgl_handle->type = HWH_TYPE_LOGOUT;
		else
		AMAP_SET_BITS(struct amap_iscsi_wrb, type, pwrb,
				HWH_TYPE_LOGOUT);
		hwi_write_buffer(pwrb, task);
		break;

	default:
		SE_DEBUG(DBG_LVL_1, "opcode =%d Not supported \n",
			 task->hdr->opcode & ISCSI_OPCODE_MASK);
		return -EINVAL;
	}

	AMAP_SET_BITS(struct amap_iscsi_wrb, r2t_exp_dtl, pwrb,
		      be32_to_cpu(task->data_count));
	AMAP_SET_BITS(struct amap_iscsi_wrb, ptr2nextwrb, pwrb,
		      io_task->pwrb_handle->nxt_wrb_index);
	be_dws_le_to_cpu(pwrb, sizeof(struct iscsi_wrb));

	doorbell |= cid & DB_WRB_POST_CID_MASK;
	if (!ring_mode)
		doorbell |= (io_task->pwrb_handle->wrb_index &
		     DB_DEF_PDU_WRB_INDEX_MASK) << DB_DEF_PDU_WRB_INDEX_SHIFT;
	doorbell |= 1 << DB_DEF_PDU_NUM_POSTED_SHIFT;
	iowrite32(doorbell, phba->db_va + DB_TXULP0_OFFSET);
	return 0;
}

static int beiscsi_task_xmit(struct iscsi_task *task)
{
	struct iscsi_conn *conn = task->conn;
	struct beiscsi_io_task *io_task = task->dd_data;
	struct scsi_cmnd *sc = task->sc;
	struct beiscsi_conn *beiscsi_conn = conn->dd_data;
	struct scatterlist *sg;
	int num_sg;
	unsigned int  writedir = 0, xferlen = 0;

	SE_DEBUG(DBG_LVL_4, "\n cid=%d In beiscsi_task_xmit task=%p conn=%p \t"
		 "beiscsi_conn=%p \n", beiscsi_conn->beiscsi_conn_cid,
		 task, conn, beiscsi_conn);
	if (!sc)
		return beiscsi_mtask(task);

	io_task->scsi_cmnd = sc;
	num_sg = scsi_dma_map(sc);
	if (num_sg < 0) {
		SE_DEBUG(DBG_LVL_1, " scsi_dma_map Failed\n")
		return num_sg;
	}
	SE_DEBUG(DBG_LVL_4, "xferlen=0x%08x scmd=%p num_sg=%d sernum=%lu\n",
		  (scsi_bufflen(sc)), sc, num_sg, sc->serial_number);
	xferlen = scsi_bufflen(sc);
	sg = scsi_sglist(sc);
	if (sc->sc_data_direction == DMA_TO_DEVICE) {
		writedir = 1;
		SE_DEBUG(DBG_LVL_4, "task->imm_count=0x%08x \n",
			 task->imm_count);
	} else
		writedir = 0;
	return beiscsi_iotask(task, sg, num_sg, xferlen, writedir);
}


static void beiscsi_remove(struct pci_dev *pcidev)
{
	struct beiscsi_hba *phba = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_eq_obj *pbe_eq;
	unsigned int i, msix_vec;

	phba = (struct beiscsi_hba *)pci_get_drvdata(pcidev);
	if (!phba) {
		dev_err(&pcidev->dev, "beiscsi_remove called with no phba \n");
		return;
	}

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	hwi_disable_intr(phba);
	if (phba->msix_enabled) {
		for (i = 0; i <= phba->num_cpus; i++) {
			msix_vec = phba->msix_entries[i].vector;
			free_irq(msix_vec, &phwi_context->be_eq[i]);
		}
	} else
		if (phba->pcidev->irq)
			free_irq(phba->pcidev->irq, phba);
	pci_disable_msix(phba->pcidev);
	destroy_workqueue(phba->wq);
	if (blk_iopoll_enabled)
		for (i = 0; i < phba->num_cpus; i++) {
			pbe_eq = &phwi_context->be_eq[i];
			blk_iopoll_disable(&pbe_eq->iopoll);
		}

	beiscsi_clean_port(phba);
	beiscsi_free_mem(phba);
	beiscsi_unmap_pci_function(phba);
	pci_free_consistent(phba->pcidev,
			    phba->ctrl.mbox_mem_alloced.size,
			    phba->ctrl.mbox_mem_alloced.va,
			    phba->ctrl.mbox_mem_alloced.dma);
	iscsi_host_remove(phba->shost);
	pci_dev_put(phba->pcidev);
	iscsi_host_free(phba->shost);
}

static void beiscsi_msix_enable(struct beiscsi_hba *phba)
{
	int i, status;

	for (i = 0; i <= phba->num_cpus; i++)
		phba->msix_entries[i].entry = i;

	status = pci_enable_msix(phba->pcidev, phba->msix_entries,
				 (phba->num_cpus + 1));
	if (!status)
		phba->msix_enabled = true;

	return;
}

static int __devinit beiscsi_dev_probe(struct pci_dev *pcidev,
				const struct pci_device_id *id)
{
	struct beiscsi_hba *phba = NULL;
	struct hwi_controller *phwi_ctrlr;
	struct hwi_context_memory *phwi_context;
	struct be_eq_obj *pbe_eq;
	int ret, msix_vec, num_cpus, i;

	ret = beiscsi_enable_pci(pcidev);
	if (ret < 0) {
		shost_printk(KERN_ERR, phba->shost, "beiscsi_dev_probe-"
			     "Failed to enable pci device \n");
		return ret;
	}

	phba = beiscsi_hba_alloc(pcidev);
	if (!phba) {
		dev_err(&pcidev->dev, "beiscsi_dev_probe-"
			" Failed in beiscsi_hba_alloc \n");
		goto disable_pci;
	}
	SE_DEBUG(DBG_LVL_8, " phba = %p \n", phba);

	pci_set_drvdata(pcidev, phba);
	if (enable_msix)
		num_cpus = find_num_cpus();
	else
		num_cpus = 1;
	phba->num_cpus = num_cpus;
	SE_DEBUG(DBG_LVL_8, "num_cpus = %d \n", phba->num_cpus);

	if (enable_msix)
		beiscsi_msix_enable(phba);
	ret = be_ctrl_init(phba, pcidev);
	if (ret) {
		shost_printk(KERN_ERR, phba->shost, "beiscsi_dev_probe-"
				"Failed in be_ctrl_init\n");
		goto hba_free;
	}

	spin_lock_init(&phba->io_sgl_lock);
	spin_lock_init(&phba->mgmt_sgl_lock);
	spin_lock_init(&phba->isr_lock);
	beiscsi_get_params(phba);
	ret = beiscsi_init_port(phba);
	if (ret < 0) {
		shost_printk(KERN_ERR, phba->shost, "beiscsi_dev_probe-"
			     "Failed in beiscsi_init_port\n");
		goto free_port;
	}

	snprintf(phba->wq_name, sizeof(phba->wq_name), "beiscsi_q_irq%u",
		 phba->shost->host_no);
	phba->wq = create_workqueue(phba->wq_name);
	if (!phba->wq) {
		shost_printk(KERN_ERR, phba->shost, "beiscsi_dev_probe-"
				"Failed to allocate work queue\n");
		goto free_twq;
	}

	INIT_WORK(&phba->work_cqs, beiscsi_process_all_cqs);

	phwi_ctrlr = phba->phwi_ctrlr;
	phwi_context = phwi_ctrlr->phwi_ctxt;
	if (blk_iopoll_enabled) {
		for (i = 0; i < phba->num_cpus; i++) {
			pbe_eq = &phwi_context->be_eq[i];
			blk_iopoll_init(&pbe_eq->iopoll, be_iopoll_budget,
					be_iopoll);
			blk_iopoll_enable(&pbe_eq->iopoll);
		}
	}
	ret = beiscsi_init_irqs(phba);
	if (ret < 0) {
		shost_printk(KERN_ERR, phba->shost, "beiscsi_dev_probe-"
			     "Failed to beiscsi_init_irqs\n");
		goto free_blkenbld;
	}
	ret = hwi_enable_intr(phba);
	if (ret < 0) {
		shost_printk(KERN_ERR, phba->shost, "beiscsi_dev_probe-"
			     "Failed to hwi_enable_intr\n");
		goto free_ctrlr;
	}
	SE_DEBUG(DBG_LVL_8, "\n\n\n SUCCESS - DRIVER LOADED \n\n\n");
	return 0;

free_ctrlr:
	if (phba->msix_enabled) {
		for (i = 0; i <= phba->num_cpus; i++) {
			msix_vec = phba->msix_entries[i].vector;
			free_irq(msix_vec, &phwi_context->be_eq[i]);
		}
	} else
		if (phba->pcidev->irq)
			free_irq(phba->pcidev->irq, phba);
	pci_disable_msix(phba->pcidev);
free_blkenbld:
	destroy_workqueue(phba->wq);
	if (blk_iopoll_enabled)
		for (i = 0; i < phba->num_cpus; i++) {
			pbe_eq = &phwi_context->be_eq[i];
			blk_iopoll_disable(&pbe_eq->iopoll);
		}
free_twq:
	beiscsi_clean_port(phba);
	beiscsi_free_mem(phba);
free_port:
	pci_free_consistent(phba->pcidev,
			    phba->ctrl.mbox_mem_alloced.size,
			    phba->ctrl.mbox_mem_alloced.va,
			   phba->ctrl.mbox_mem_alloced.dma);
	beiscsi_unmap_pci_function(phba);
hba_free:
	iscsi_host_remove(phba->shost);
	pci_dev_put(phba->pcidev);
	iscsi_host_free(phba->shost);
disable_pci:
	pci_disable_device(pcidev);
	return ret;
}

struct iscsi_transport beiscsi_iscsi_transport = {
	.owner = THIS_MODULE,
	.name = DRV_NAME,
	.caps = CAP_RECOVERY_L0 | CAP_HDRDGST |
		CAP_MULTI_R2T | CAP_DATADGST | CAP_DATA_PATH_OFFLOAD,
	.param_mask = ISCSI_MAX_RECV_DLENGTH |
		ISCSI_MAX_XMIT_DLENGTH |
		ISCSI_HDRDGST_EN |
		ISCSI_DATADGST_EN |
		ISCSI_INITIAL_R2T_EN |
		ISCSI_MAX_R2T |
		ISCSI_IMM_DATA_EN |
		ISCSI_FIRST_BURST |
		ISCSI_MAX_BURST |
		ISCSI_PDU_INORDER_EN |
		ISCSI_DATASEQ_INORDER_EN |
		ISCSI_ERL |
		ISCSI_CONN_PORT |
		ISCSI_CONN_ADDRESS |
		ISCSI_EXP_STATSN |
		ISCSI_PERSISTENT_PORT |
		ISCSI_PERSISTENT_ADDRESS |
		ISCSI_TARGET_NAME | ISCSI_TPGT |
		ISCSI_USERNAME | ISCSI_PASSWORD |
		ISCSI_USERNAME_IN | ISCSI_PASSWORD_IN |
		ISCSI_FAST_ABORT | ISCSI_ABORT_TMO |
		ISCSI_LU_RESET_TMO | ISCSI_TGT_RESET_TMO |
		ISCSI_PING_TMO | ISCSI_RECV_TMO |
		ISCSI_IFACE_NAME | ISCSI_INITIATOR_NAME,
	.host_param_mask = ISCSI_HOST_HWADDRESS | ISCSI_HOST_IPADDRESS |
				ISCSI_HOST_INITIATOR_NAME,
	.create_session = beiscsi_session_create,
	.destroy_session = beiscsi_session_destroy,
	.create_conn = beiscsi_conn_create,
	.bind_conn = beiscsi_conn_bind,
	.destroy_conn = iscsi_conn_teardown,
	.set_param = beiscsi_set_param,
	.get_conn_param = beiscsi_conn_get_param,
	.get_session_param = iscsi_session_get_param,
	.get_host_param = beiscsi_get_host_param,
	.start_conn = beiscsi_conn_start,
	.stop_conn = beiscsi_conn_stop,
	.send_pdu = iscsi_conn_send_pdu,
	.xmit_task = beiscsi_task_xmit,
	.cleanup_task = beiscsi_cleanup_task,
	.alloc_pdu = beiscsi_alloc_pdu,
	.parse_pdu_itt = beiscsi_parse_pdu,
	.get_stats = beiscsi_conn_get_stats,
	.ep_connect = beiscsi_ep_connect,
	.ep_poll = beiscsi_ep_poll,
	.ep_disconnect = beiscsi_ep_disconnect,
	.session_recovery_timedout = iscsi_session_recovery_timedout,
};

static struct pci_driver beiscsi_pci_driver = {
	.name = DRV_NAME,
	.probe = beiscsi_dev_probe,
	.remove = beiscsi_remove,
	.id_table = beiscsi_pci_id_table
};


static int __init beiscsi_module_init(void)
{
	int ret;

	beiscsi_scsi_transport =
			iscsi_register_transport(&beiscsi_iscsi_transport);
	if (!beiscsi_scsi_transport) {
		SE_DEBUG(DBG_LVL_1,
			 "beiscsi_module_init - Unable to  register beiscsi"
			 "transport.\n");
		ret = -ENOMEM;
	}
	SE_DEBUG(DBG_LVL_8, "In beiscsi_module_init, tt=%p \n",
		 &beiscsi_iscsi_transport);

	ret = pci_register_driver(&beiscsi_pci_driver);
	if (ret) {
		SE_DEBUG(DBG_LVL_1,
			 "beiscsi_module_init - Unable to  register"
			 "beiscsi pci driver.\n");
		goto unregister_iscsi_transport;
	}
	ring_mode = 0;
	return 0;

unregister_iscsi_transport:
	iscsi_unregister_transport(&beiscsi_iscsi_transport);
	return ret;
}

static void __exit beiscsi_module_exit(void)
{
	pci_unregister_driver(&beiscsi_pci_driver);
	iscsi_unregister_transport(&beiscsi_iscsi_transport);
}

module_init(beiscsi_module_init);
module_exit(beiscsi_module_exit);
