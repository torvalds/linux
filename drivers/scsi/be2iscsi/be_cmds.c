/**
 * Copyright (C) 2005 - 2013 Emulex
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.  The full GNU General
 * Public License is included in this distribution in the file called COPYING.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <scsi/iscsi_proto.h>

#include "be.h"
#include "be_mgmt.h"
#include "be_main.h"

int beiscsi_pci_soft_reset(struct beiscsi_hba *phba)
{
	u32 sreset;
	u8 *pci_reset_offset = 0;
	u8 *pci_online0_offset = 0;
	u8 *pci_online1_offset = 0;
	u32 pconline0 = 0;
	u32 pconline1 = 0;
	u32 i;

	pci_reset_offset = (u8 *)phba->pci_va + BE2_SOFT_RESET;
	pci_online0_offset = (u8 *)phba->pci_va + BE2_PCI_ONLINE0;
	pci_online1_offset = (u8 *)phba->pci_va + BE2_PCI_ONLINE1;
	sreset = readl((void *)pci_reset_offset);
	sreset |= BE2_SET_RESET;
	writel(sreset, (void *)pci_reset_offset);

	i = 0;
	while (sreset & BE2_SET_RESET) {
		if (i > 64)
			break;
		msleep(100);
		sreset = readl((void *)pci_reset_offset);
		i++;
	}

	if (sreset & BE2_SET_RESET) {
		printk(KERN_ERR DRV_NAME
		       " Soft Reset  did not deassert\n");
		return -EIO;
	}
	pconline1 = BE2_MPU_IRAM_ONLINE;
	writel(pconline0, (void *)pci_online0_offset);
	writel(pconline1, (void *)pci_online1_offset);

	sreset |= BE2_SET_RESET;
	writel(sreset, (void *)pci_reset_offset);

	i = 0;
	while (sreset & BE2_SET_RESET) {
		if (i > 64)
			break;
		msleep(1);
		sreset = readl((void *)pci_reset_offset);
		i++;
	}
	if (sreset & BE2_SET_RESET) {
		printk(KERN_ERR DRV_NAME
		       " MPU Online Soft Reset did not deassert\n");
		return -EIO;
	}
	return 0;
}

int be_chk_reset_complete(struct beiscsi_hba *phba)
{
	unsigned int num_loop;
	u8 *mpu_sem = 0;
	u32 status;

	num_loop = 1000;
	mpu_sem = (u8 *)phba->csr_va + MPU_EP_SEMAPHORE;
	msleep(5000);

	while (num_loop) {
		status = readl((void *)mpu_sem);

		if ((status & 0x80000000) || (status & 0x0000FFFF) == 0xC000)
			break;
		msleep(60);
		num_loop--;
	}

	if ((status & 0x80000000) || (!num_loop)) {
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BC_%d : Failed in be_chk_reset_complete"
			    "status = 0x%x\n", status);
		return -EIO;
	}

	return 0;
}

void be_mcc_notify(struct beiscsi_hba *phba)
{
	struct be_queue_info *mccq = &phba->ctrl.mcc_obj.q;
	u32 val = 0;

	val |= mccq->id & DB_MCCQ_RING_ID_MASK;
	val |= 1 << DB_MCCQ_NUM_POSTED_SHIFT;
	iowrite32(val, phba->db_va + DB_MCCQ_OFFSET);
}

unsigned int alloc_mcc_tag(struct beiscsi_hba *phba)
{
	unsigned int tag = 0;

	if (phba->ctrl.mcc_tag_available) {
		tag = phba->ctrl.mcc_tag[phba->ctrl.mcc_alloc_index];
		phba->ctrl.mcc_tag[phba->ctrl.mcc_alloc_index] = 0;
		phba->ctrl.mcc_numtag[tag] = 0;
	}
	if (tag) {
		phba->ctrl.mcc_tag_available--;
		if (phba->ctrl.mcc_alloc_index == (MAX_MCC_CMD - 1))
			phba->ctrl.mcc_alloc_index = 0;
		else
			phba->ctrl.mcc_alloc_index++;
	}
	return tag;
}

/*
 * beiscsi_mccq_compl()- Wait for completion of MBX
 * @phba: Driver private structure
 * @tag: Tag for the MBX Command
 * @wrb: the WRB used for the MBX Command
 * @cmd_hdr: IOCTL Hdr for the MBX Cmd
 *
 * Waits for MBX completion with the passed TAG.
 *
 * return
 * Success: 0
 * Failure: Non-Zero
 **/
int beiscsi_mccq_compl(struct beiscsi_hba *phba,
		uint32_t tag, struct be_mcc_wrb **wrb,
		void *cmd_hdr)
{
	int rc = 0;
	uint32_t mcc_tag_response;
	uint16_t status = 0, addl_status = 0, wrb_num = 0;
	struct be_mcc_wrb *temp_wrb;
	struct be_cmd_req_hdr *ioctl_hdr;
	struct be_cmd_resp_hdr *ioctl_resp_hdr;
	struct be_queue_info *mccq = &phba->ctrl.mcc_obj.q;

	if (beiscsi_error(phba))
		return -EIO;

	/* wait for the mccq completion */
	rc = wait_event_interruptible_timeout(
				phba->ctrl.mcc_wait[tag],
				phba->ctrl.mcc_numtag[tag],
				msecs_to_jiffies(
				BEISCSI_HOST_MBX_TIMEOUT));

	if (rc <= 0) {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_INIT | BEISCSI_LOG_EH |
			    BEISCSI_LOG_CONFIG,
			    "BC_%d : MBX Cmd Completion timed out\n");
		rc = -EAGAIN;
		goto release_mcc_tag;
	} else
		rc = 0;

	mcc_tag_response = phba->ctrl.mcc_numtag[tag];
	status = (mcc_tag_response & CQE_STATUS_MASK);
	addl_status = ((mcc_tag_response & CQE_STATUS_ADDL_MASK) >>
			CQE_STATUS_ADDL_SHIFT);

	if (cmd_hdr) {
		ioctl_hdr = (struct be_cmd_req_hdr *)cmd_hdr;
	} else {
		wrb_num = (mcc_tag_response & CQE_STATUS_WRB_MASK) >>
			   CQE_STATUS_WRB_SHIFT;
		temp_wrb = (struct be_mcc_wrb *)queue_get_wrb(mccq, wrb_num);
		ioctl_hdr = embedded_payload(temp_wrb);

		if (wrb)
			*wrb = temp_wrb;
	}

	if (status || addl_status) {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_INIT | BEISCSI_LOG_EH |
			    BEISCSI_LOG_CONFIG,
			    "BC_%d : MBX Cmd Failed for "
			    "Subsys : %d Opcode : %d with "
			    "Status : %d and Extd_Status : %d\n",
			    ioctl_hdr->subsystem,
			    ioctl_hdr->opcode,
			    status, addl_status);

		if (status == MCC_STATUS_INSUFFICIENT_BUFFER) {
			ioctl_resp_hdr = (struct be_cmd_resp_hdr *) ioctl_hdr;
			if (ioctl_resp_hdr->response_length)
				goto release_mcc_tag;
		}
		rc = -EAGAIN;
	}

release_mcc_tag:
	/* Release the MCC entry */
	free_mcc_tag(&phba->ctrl, tag);

	return rc;
}

void free_mcc_tag(struct be_ctrl_info *ctrl, unsigned int tag)
{
	spin_lock(&ctrl->mbox_lock);
	tag = tag & 0x000000FF;
	ctrl->mcc_tag[ctrl->mcc_free_index] = tag;
	if (ctrl->mcc_free_index == (MAX_MCC_CMD - 1))
		ctrl->mcc_free_index = 0;
	else
		ctrl->mcc_free_index++;
	ctrl->mcc_tag_available++;
	spin_unlock(&ctrl->mbox_lock);
}

bool is_link_state_evt(u32 trailer)
{
	return (((trailer >> ASYNC_TRAILER_EVENT_CODE_SHIFT) &
		  ASYNC_TRAILER_EVENT_CODE_MASK) ==
		  ASYNC_EVENT_CODE_LINK_STATE);
}

static inline bool be_mcc_compl_is_new(struct be_mcc_compl *compl)
{
	if (compl->flags != 0) {
		compl->flags = le32_to_cpu(compl->flags);
		WARN_ON((compl->flags & CQE_FLAGS_VALID_MASK) == 0);
		return true;
	} else
		return false;
}

static inline void be_mcc_compl_use(struct be_mcc_compl *compl)
{
	compl->flags = 0;
}

/*
 * be_mcc_compl_process()- Check the MBX comapletion status
 * @ctrl: Function specific MBX data structure
 * @compl: Completion status of MBX Command
 *
 * Check for the MBX completion status when BMBX method used
 *
 * return
 * Success: Zero
 * Failure: Non-Zero
 **/
static int be_mcc_compl_process(struct be_ctrl_info *ctrl,
				struct be_mcc_compl *compl)
{
	u16 compl_status, extd_status;
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	struct be_cmd_req_hdr *hdr = embedded_payload(wrb);
	struct be_cmd_resp_hdr *resp_hdr;

	be_dws_le_to_cpu(compl, 4);

	compl_status = (compl->status >> CQE_STATUS_COMPL_SHIFT) &
					CQE_STATUS_COMPL_MASK;
	if (compl_status != MCC_STATUS_SUCCESS) {
		extd_status = (compl->status >> CQE_STATUS_EXTD_SHIFT) &
						CQE_STATUS_EXTD_MASK;

		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BC_%d : error in cmd completion: "
			    "Subsystem : %d Opcode : %d "
			    "status(compl/extd)=%d/%d\n",
			    hdr->subsystem, hdr->opcode,
			    compl_status, extd_status);

		if (compl_status == MCC_STATUS_INSUFFICIENT_BUFFER) {
			resp_hdr = (struct be_cmd_resp_hdr *) hdr;
			if (resp_hdr->response_length)
				return 0;
		}
		return -EBUSY;
	}
	return 0;
}

int be_mcc_compl_process_isr(struct be_ctrl_info *ctrl,
				    struct be_mcc_compl *compl)
{
	u16 compl_status, extd_status;
	unsigned short tag;

	be_dws_le_to_cpu(compl, 4);

	compl_status = (compl->status >> CQE_STATUS_COMPL_SHIFT) &
					CQE_STATUS_COMPL_MASK;
	/* The ctrl.mcc_numtag[tag] is filled with
	 * [31] = valid, [30:24] = Rsvd, [23:16] = wrb, [15:8] = extd_status,
	 * [7:0] = compl_status
	 */
	tag = (compl->tag0 & 0x000000FF);
	extd_status = (compl->status >> CQE_STATUS_EXTD_SHIFT) &
					CQE_STATUS_EXTD_MASK;

	ctrl->mcc_numtag[tag]  = 0x80000000;
	ctrl->mcc_numtag[tag] |= (compl->tag0 & 0x00FF0000);
	ctrl->mcc_numtag[tag] |= (extd_status & 0x000000FF) << 8;
	ctrl->mcc_numtag[tag] |= (compl_status & 0x000000FF);
	wake_up_interruptible(&ctrl->mcc_wait[tag]);
	return 0;
}

static struct be_mcc_compl *be_mcc_compl_get(struct beiscsi_hba *phba)
{
	struct be_queue_info *mcc_cq = &phba->ctrl.mcc_obj.cq;
	struct be_mcc_compl *compl = queue_tail_node(mcc_cq);

	if (be_mcc_compl_is_new(compl)) {
		queue_tail_inc(mcc_cq);
		return compl;
	}
	return NULL;
}

static void be2iscsi_fail_session(struct iscsi_cls_session *cls_session)
{
	iscsi_session_failure(cls_session->dd_data, ISCSI_ERR_CONN_FAILED);
}

void beiscsi_async_link_state_process(struct beiscsi_hba *phba,
		struct be_async_event_link_state *evt)
{
	if ((evt->port_link_status == ASYNC_EVENT_LINK_DOWN) ||
	    ((evt->port_link_status & ASYNC_EVENT_LOGICAL) &&
	     (evt->port_fault != BEISCSI_PHY_LINK_FAULT_NONE))) {
		phba->state = BE_ADAPTER_LINK_DOWN;

		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_INIT,
			    "BC_%d : Link Down on Port %d\n",
			    evt->physical_port);

		iscsi_host_for_each_session(phba->shost,
					    be2iscsi_fail_session);
	} else if ((evt->port_link_status & ASYNC_EVENT_LINK_UP) ||
		    ((evt->port_link_status & ASYNC_EVENT_LOGICAL) &&
		     (evt->port_fault == BEISCSI_PHY_LINK_FAULT_NONE))) {
		phba->state = BE_ADAPTER_UP;

		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_INIT,
			    "BC_%d : Link UP on Port %d\n",
			    evt->physical_port);
	}
}

static void beiscsi_cq_notify(struct beiscsi_hba *phba, u16 qid, bool arm,
		       u16 num_popped)
{
	u32 val = 0;
	val |= qid & DB_CQ_RING_ID_MASK;
	if (arm)
		val |= 1 << DB_CQ_REARM_SHIFT;
	val |= num_popped << DB_CQ_NUM_POPPED_SHIFT;
	iowrite32(val, phba->db_va + DB_CQ_OFFSET);
}


int beiscsi_process_mcc(struct beiscsi_hba *phba)
{
	struct be_mcc_compl *compl;
	int num = 0, status = 0;
	struct be_ctrl_info *ctrl = &phba->ctrl;

	spin_lock_bh(&phba->ctrl.mcc_cq_lock);
	while ((compl = be_mcc_compl_get(phba))) {
		if (compl->flags & CQE_FLAGS_ASYNC_MASK) {
			/* Interpret flags as an async trailer */
			if (is_link_state_evt(compl->flags))
				/* Interpret compl as a async link evt */
				beiscsi_async_link_state_process(phba,
				   (struct be_async_event_link_state *) compl);
			else
				beiscsi_log(phba, KERN_ERR,
					    BEISCSI_LOG_CONFIG |
					    BEISCSI_LOG_MBOX,
					    "BC_%d : Unsupported Async Event, flags"
					    " = 0x%08x\n", compl->flags);

		} else if (compl->flags & CQE_FLAGS_COMPLETED_MASK) {
				status = be_mcc_compl_process(ctrl, compl);
				atomic_dec(&phba->ctrl.mcc_obj.q.used);
		}
		be_mcc_compl_use(compl);
		num++;
	}

	if (num)
		beiscsi_cq_notify(phba, phba->ctrl.mcc_obj.cq.id, true, num);

	spin_unlock_bh(&phba->ctrl.mcc_cq_lock);
	return status;
}

/*
 * be_mcc_wait_compl()- Wait for MBX completion
 * @phba: driver private structure
 *
 * Wait till no more pending mcc requests are present
 *
 * return
 * Success: 0
 * Failure: Non-Zero
 *
 **/
static int be_mcc_wait_compl(struct beiscsi_hba *phba)
{
	int i, status;
	for (i = 0; i < mcc_timeout; i++) {
		if (beiscsi_error(phba))
			return -EIO;

		status = beiscsi_process_mcc(phba);
		if (status)
			return status;

		if (atomic_read(&phba->ctrl.mcc_obj.q.used) == 0)
			break;
		udelay(100);
	}
	if (i == mcc_timeout) {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BC_%d : FW Timed Out\n");
		phba->fw_timeout = true;
		beiscsi_ue_detect(phba);
		return -EBUSY;
	}
	return 0;
}

/*
 * be_mcc_notify_wait()- Notify and wait for Compl
 * @phba: driver private structure
 *
 * Notify MCC requests and wait for completion
 *
 * return
 * Success: 0
 * Failure: Non-Zero
 **/
int be_mcc_notify_wait(struct beiscsi_hba *phba)
{
	be_mcc_notify(phba);
	return be_mcc_wait_compl(phba);
}

/*
 * be_mbox_db_ready_wait()- Check ready status
 * @ctrl: Function specific MBX data structure
 *
 * Check for the ready status of FW to send BMBX
 * commands to adapter.
 *
 * return
 * Success: 0
 * Failure: Non-Zero
 **/
static int be_mbox_db_ready_wait(struct be_ctrl_info *ctrl)
{
	void __iomem *db = ctrl->db + MPU_MAILBOX_DB_OFFSET;
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	uint32_t wait = 0;
	u32 ready;

	do {

		if (beiscsi_error(phba))
			return -EIO;

		ready = ioread32(db) & MPU_MAILBOX_DB_RDY_MASK;
		if (ready)
			break;

		if (wait > BEISCSI_HOST_MBX_TIMEOUT) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
				    "BC_%d : FW Timed Out\n");
			phba->fw_timeout = true;
			beiscsi_ue_detect(phba);
			return -EBUSY;
		}

		mdelay(1);
		wait++;
	} while (true);
	return 0;
}

/*
 * be_mbox_notify: Notify adapter of new BMBX command
 * @ctrl: Function specific MBX data structure
 *
 * Ring doorbell to inform adapter of a BMBX command
 * to process
 *
 * return
 * Success: 0
 * Failure: Non-Zero
 **/
int be_mbox_notify(struct be_ctrl_info *ctrl)
{
	int status;
	u32 val = 0;
	void __iomem *db = ctrl->db + MPU_MAILBOX_DB_OFFSET;
	struct be_dma_mem *mbox_mem = &ctrl->mbox_mem;
	struct be_mcc_mailbox *mbox = mbox_mem->va;
	struct be_mcc_compl *compl = &mbox->compl;
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);

	status = be_mbox_db_ready_wait(ctrl);
	if (status)
		return status;

	val &= ~MPU_MAILBOX_DB_RDY_MASK;
	val |= MPU_MAILBOX_DB_HI_MASK;
	val |= (upper_32_bits(mbox_mem->dma) >> 2) << 2;
	iowrite32(val, db);

	status = be_mbox_db_ready_wait(ctrl);
	if (status)
		return status;

	val = 0;
	val &= ~MPU_MAILBOX_DB_RDY_MASK;
	val &= ~MPU_MAILBOX_DB_HI_MASK;
	val |= (u32) (mbox_mem->dma >> 4) << 2;
	iowrite32(val, db);

	status = be_mbox_db_ready_wait(ctrl);
	if (status)
		return status;

	if (be_mcc_compl_is_new(compl)) {
		status = be_mcc_compl_process(ctrl, &mbox->compl);
		be_mcc_compl_use(compl);
		if (status) {
			beiscsi_log(phba, KERN_ERR,
				    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
				    "BC_%d : After be_mcc_compl_process\n");

			return status;
		}
	} else {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BC_%d : Invalid Mailbox Completion\n");

		return -EBUSY;
	}
	return 0;
}

/*
 * Insert the mailbox address into the doorbell in two steps
 * Polls on the mbox doorbell till a command completion (or a timeout) occurs
 */
static int be_mbox_notify_wait(struct beiscsi_hba *phba)
{
	int status;
	u32 val = 0;
	void __iomem *db = phba->ctrl.db + MPU_MAILBOX_DB_OFFSET;
	struct be_dma_mem *mbox_mem = &phba->ctrl.mbox_mem;
	struct be_mcc_mailbox *mbox = mbox_mem->va;
	struct be_mcc_compl *compl = &mbox->compl;
	struct be_ctrl_info *ctrl = &phba->ctrl;

	status = be_mbox_db_ready_wait(ctrl);
	if (status)
		return status;

	val |= MPU_MAILBOX_DB_HI_MASK;
	/* at bits 2 - 31 place mbox dma addr msb bits 34 - 63 */
	val |= (upper_32_bits(mbox_mem->dma) >> 2) << 2;
	iowrite32(val, db);

	/* wait for ready to be set */
	status = be_mbox_db_ready_wait(ctrl);
	if (status != 0)
		return status;

	val = 0;
	/* at bits 2 - 31 place mbox dma addr lsb bits 4 - 33 */
	val |= (u32)(mbox_mem->dma >> 4) << 2;
	iowrite32(val, db);

	status = be_mbox_db_ready_wait(ctrl);
	if (status != 0)
		return status;

	/* A cq entry has been made now */
	if (be_mcc_compl_is_new(compl)) {
		status = be_mcc_compl_process(ctrl, &mbox->compl);
		be_mcc_compl_use(compl);
		if (status)
			return status;
	} else {
		beiscsi_log(phba, KERN_ERR,
			    BEISCSI_LOG_CONFIG | BEISCSI_LOG_MBOX,
			    "BC_%d : invalid mailbox completion\n");

		return -EBUSY;
	}
	return 0;
}

void be_wrb_hdr_prepare(struct be_mcc_wrb *wrb, int payload_len,
				bool embedded, u8 sge_cnt)
{
	if (embedded)
		wrb->embedded |= MCC_WRB_EMBEDDED_MASK;
	else
		wrb->embedded |= (sge_cnt & MCC_WRB_SGE_CNT_MASK) <<
						MCC_WRB_SGE_CNT_SHIFT;
	wrb->payload_length = payload_len;
	be_dws_cpu_to_le(wrb, 8);
}

void be_cmd_hdr_prepare(struct be_cmd_req_hdr *req_hdr,
			u8 subsystem, u8 opcode, int cmd_len)
{
	req_hdr->opcode = opcode;
	req_hdr->subsystem = subsystem;
	req_hdr->request_length = cpu_to_le32(cmd_len - sizeof(*req_hdr));
	req_hdr->timeout = BEISCSI_FW_MBX_TIMEOUT;
}

static void be_cmd_page_addrs_prepare(struct phys_addr *pages, u32 max_pages,
							struct be_dma_mem *mem)
{
	int i, buf_pages;
	u64 dma = (u64) mem->dma;

	buf_pages = min(PAGES_4K_SPANNED(mem->va, mem->size), max_pages);
	for (i = 0; i < buf_pages; i++) {
		pages[i].lo = cpu_to_le32(dma & 0xFFFFFFFF);
		pages[i].hi = cpu_to_le32(upper_32_bits(dma));
		dma += PAGE_SIZE_4K;
	}
}

static u32 eq_delay_to_mult(u32 usec_delay)
{
#define MAX_INTR_RATE 651042
	const u32 round = 10;
	u32 multiplier;

	if (usec_delay == 0)
		multiplier = 0;
	else {
		u32 interrupt_rate = 1000000 / usec_delay;
		if (interrupt_rate == 0)
			multiplier = 1023;
		else {
			multiplier = (MAX_INTR_RATE - interrupt_rate) * round;
			multiplier /= interrupt_rate;
			multiplier = (multiplier + round / 2) / round;
			multiplier = min(multiplier, (u32) 1023);
		}
	}
	return multiplier;
}

struct be_mcc_wrb *wrb_from_mbox(struct be_dma_mem *mbox_mem)
{
	return &((struct be_mcc_mailbox *)(mbox_mem->va))->wrb;
}

struct be_mcc_wrb *wrb_from_mccq(struct beiscsi_hba *phba)
{
	struct be_queue_info *mccq = &phba->ctrl.mcc_obj.q;
	struct be_mcc_wrb *wrb;

	BUG_ON(atomic_read(&mccq->used) >= mccq->len);
	wrb = queue_head_node(mccq);
	memset(wrb, 0, sizeof(*wrb));
	wrb->tag0 = (mccq->head & 0x000000FF) << 16;
	queue_head_inc(mccq);
	atomic_inc(&mccq->used);
	return wrb;
}


int beiscsi_cmd_eq_create(struct be_ctrl_info *ctrl,
			  struct be_queue_info *eq, int eq_delay)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_cmd_req_eq_create *req = embedded_payload(wrb);
	struct be_cmd_resp_eq_create *resp = embedded_payload(wrb);
	struct be_dma_mem *q_mem = &eq->dma_mem;
	int status;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			OPCODE_COMMON_EQ_CREATE, sizeof(*req));

	req->num_pages = cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));

	AMAP_SET_BITS(struct amap_eq_context, func, req->context,
						PCI_FUNC(ctrl->pdev->devfn));
	AMAP_SET_BITS(struct amap_eq_context, valid, req->context, 1);
	AMAP_SET_BITS(struct amap_eq_context, size, req->context, 0);
	AMAP_SET_BITS(struct amap_eq_context, count, req->context,
					__ilog2_u32(eq->len / 256));
	AMAP_SET_BITS(struct amap_eq_context, delaymult, req->context,
					eq_delay_to_mult(eq_delay));
	be_dws_cpu_to_le(req->context, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify(ctrl);
	if (!status) {
		eq->id = le16_to_cpu(resp->eq_id);
		eq->created = true;
	}
	spin_unlock(&ctrl->mbox_lock);
	return status;
}

/**
 * be_cmd_fw_initialize()- Initialize FW
 * @ctrl: Pointer to function control structure
 *
 * Send FW initialize pattern for the function.
 *
 * return
 * Success: 0
 * Failure: Non-Zero value
 **/
int be_cmd_fw_initialize(struct be_ctrl_info *ctrl)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	int status;
	u8 *endian_check;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	endian_check = (u8 *) wrb;
	*endian_check++ = 0xFF;
	*endian_check++ = 0x12;
	*endian_check++ = 0x34;
	*endian_check++ = 0xFF;
	*endian_check++ = 0xFF;
	*endian_check++ = 0x56;
	*endian_check++ = 0x78;
	*endian_check++ = 0xFF;
	be_dws_cpu_to_le(wrb, sizeof(*wrb));

	status = be_mbox_notify(ctrl);
	if (status)
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BC_%d : be_cmd_fw_initialize Failed\n");

	spin_unlock(&ctrl->mbox_lock);
	return status;
}

/**
 * be_cmd_fw_uninit()- Uinitialize FW
 * @ctrl: Pointer to function control structure
 *
 * Send FW uninitialize pattern for the function
 *
 * return
 * Success: 0
 * Failure: Non-Zero value
 **/
int be_cmd_fw_uninit(struct be_ctrl_info *ctrl)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	int status;
	u8 *endian_check;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	endian_check = (u8 *) wrb;
	*endian_check++ = 0xFF;
	*endian_check++ = 0xAA;
	*endian_check++ = 0xBB;
	*endian_check++ = 0xFF;
	*endian_check++ = 0xFF;
	*endian_check++ = 0xCC;
	*endian_check++ = 0xDD;
	*endian_check = 0xFF;

	be_dws_cpu_to_le(wrb, sizeof(*wrb));

	status = be_mbox_notify(ctrl);
	if (status)
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BC_%d : be_cmd_fw_uninit Failed\n");

	spin_unlock(&ctrl->mbox_lock);
	return status;
}

int beiscsi_cmd_cq_create(struct be_ctrl_info *ctrl,
			  struct be_queue_info *cq, struct be_queue_info *eq,
			  bool sol_evts, bool no_delay, int coalesce_wm)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_cmd_req_cq_create *req = embedded_payload(wrb);
	struct be_cmd_resp_cq_create *resp = embedded_payload(wrb);
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	struct be_dma_mem *q_mem = &cq->dma_mem;
	void *ctxt = &req->context;
	int status;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			OPCODE_COMMON_CQ_CREATE, sizeof(*req));

	req->num_pages = cpu_to_le16(PAGES_4K_SPANNED(q_mem->va, q_mem->size));
	if (is_chip_be2_be3r(phba)) {
		AMAP_SET_BITS(struct amap_cq_context, coalescwm,
			      ctxt, coalesce_wm);
		AMAP_SET_BITS(struct amap_cq_context, nodelay, ctxt, no_delay);
		AMAP_SET_BITS(struct amap_cq_context, count, ctxt,
			      __ilog2_u32(cq->len / 256));
		AMAP_SET_BITS(struct amap_cq_context, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context, solevent, ctxt, sol_evts);
		AMAP_SET_BITS(struct amap_cq_context, eventable, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context, eqid, ctxt, eq->id);
		AMAP_SET_BITS(struct amap_cq_context, armed, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context, func, ctxt,
			      PCI_FUNC(ctrl->pdev->devfn));
	} else {
		req->hdr.version = MBX_CMD_VER2;
		req->page_size = 1;
		AMAP_SET_BITS(struct amap_cq_context_v2, coalescwm,
			      ctxt, coalesce_wm);
		AMAP_SET_BITS(struct amap_cq_context_v2, nodelay,
			      ctxt, no_delay);
		AMAP_SET_BITS(struct amap_cq_context_v2, count, ctxt,
			      __ilog2_u32(cq->len / 256));
		AMAP_SET_BITS(struct amap_cq_context_v2, valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_v2, eventable, ctxt, 1);
		AMAP_SET_BITS(struct amap_cq_context_v2, eqid, ctxt, eq->id);
		AMAP_SET_BITS(struct amap_cq_context_v2, armed, ctxt, 1);
	}

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify(ctrl);
	if (!status) {
		cq->id = le16_to_cpu(resp->cq_id);
		cq->created = true;
	} else
		beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
			    "BC_%d : In be_cmd_cq_create, status=ox%08x\n",
			    status);

	spin_unlock(&ctrl->mbox_lock);

	return status;
}

static u32 be_encoded_q_len(int q_len)
{
	u32 len_encoded = fls(q_len);	/* log2(len) + 1 */
	if (len_encoded == 16)
		len_encoded = 0;
	return len_encoded;
}

int beiscsi_cmd_mccq_create(struct beiscsi_hba *phba,
			struct be_queue_info *mccq,
			struct be_queue_info *cq)
{
	struct be_mcc_wrb *wrb;
	struct be_cmd_req_mcc_create *req;
	struct be_dma_mem *q_mem = &mccq->dma_mem;
	struct be_ctrl_info *ctrl;
	void *ctxt;
	int status;

	spin_lock(&phba->ctrl.mbox_lock);
	ctrl = &phba->ctrl;
	wrb = wrb_from_mbox(&ctrl->mbox_mem);
	memset(wrb, 0, sizeof(*wrb));
	req = embedded_payload(wrb);
	ctxt = &req->context;

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			OPCODE_COMMON_MCC_CREATE, sizeof(*req));

	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);

	AMAP_SET_BITS(struct amap_mcc_context, fid, ctxt,
		      PCI_FUNC(phba->pcidev->devfn));
	AMAP_SET_BITS(struct amap_mcc_context, valid, ctxt, 1);
	AMAP_SET_BITS(struct amap_mcc_context, ring_size, ctxt,
		be_encoded_q_len(mccq->len));
	AMAP_SET_BITS(struct amap_mcc_context, cq_id, ctxt, cq->id);

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify_wait(phba);
	if (!status) {
		struct be_cmd_resp_mcc_create *resp = embedded_payload(wrb);
		mccq->id = le16_to_cpu(resp->id);
		mccq->created = true;
	}
	spin_unlock(&phba->ctrl.mbox_lock);

	return status;
}

int beiscsi_cmd_q_destroy(struct be_ctrl_info *ctrl, struct be_queue_info *q,
			  int queue_type)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_cmd_req_q_destroy *req = embedded_payload(wrb);
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	u8 subsys = 0, opcode = 0;
	int status;

	beiscsi_log(phba, KERN_INFO, BEISCSI_LOG_INIT,
		    "BC_%d : In beiscsi_cmd_q_destroy "
		    "queue_type : %d\n", queue_type);

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));
	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);

	switch (queue_type) {
	case QTYPE_EQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_EQ_DESTROY;
		break;
	case QTYPE_CQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_CQ_DESTROY;
		break;
	case QTYPE_MCCQ:
		subsys = CMD_SUBSYSTEM_COMMON;
		opcode = OPCODE_COMMON_MCC_DESTROY;
		break;
	case QTYPE_WRBQ:
		subsys = CMD_SUBSYSTEM_ISCSI;
		opcode = OPCODE_COMMON_ISCSI_WRBQ_DESTROY;
		break;
	case QTYPE_DPDUQ:
		subsys = CMD_SUBSYSTEM_ISCSI;
		opcode = OPCODE_COMMON_ISCSI_DEFQ_DESTROY;
		break;
	case QTYPE_SGL:
		subsys = CMD_SUBSYSTEM_ISCSI;
		opcode = OPCODE_COMMON_ISCSI_CFG_REMOVE_SGL_PAGES;
		break;
	default:
		spin_unlock(&ctrl->mbox_lock);
		BUG();
		return -ENXIO;
	}
	be_cmd_hdr_prepare(&req->hdr, subsys, opcode, sizeof(*req));
	if (queue_type != QTYPE_SGL)
		req->id = cpu_to_le16(q->id);

	status = be_mbox_notify(ctrl);

	spin_unlock(&ctrl->mbox_lock);
	return status;
}

int be_cmd_create_default_pdu_queue(struct be_ctrl_info *ctrl,
				    struct be_queue_info *cq,
				    struct be_queue_info *dq, int length,
				    int entry_size)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_defq_create_req *req = embedded_payload(wrb);
	struct be_dma_mem *q_mem = &dq->dma_mem;
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	void *ctxt = &req->context;
	int status;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			   OPCODE_COMMON_ISCSI_DEFQ_CREATE, sizeof(*req));

	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);

	if (is_chip_be2_be3r(phba)) {
		AMAP_SET_BITS(struct amap_be_default_pdu_context,
			      rx_pdid, ctxt, 0);
		AMAP_SET_BITS(struct amap_be_default_pdu_context,
			      rx_pdid_valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_be_default_pdu_context,
			      pci_func_id, ctxt, PCI_FUNC(ctrl->pdev->devfn));
		AMAP_SET_BITS(struct amap_be_default_pdu_context,
			      ring_size, ctxt,
			      be_encoded_q_len(length /
			      sizeof(struct phys_addr)));
		AMAP_SET_BITS(struct amap_be_default_pdu_context,
			      default_buffer_size, ctxt, entry_size);
		AMAP_SET_BITS(struct amap_be_default_pdu_context,
			      cq_id_recv, ctxt,	cq->id);
	} else {
		AMAP_SET_BITS(struct amap_default_pdu_context_ext,
			      rx_pdid, ctxt, 0);
		AMAP_SET_BITS(struct amap_default_pdu_context_ext,
			      rx_pdid_valid, ctxt, 1);
		AMAP_SET_BITS(struct amap_default_pdu_context_ext,
			      ring_size, ctxt,
			      be_encoded_q_len(length /
			      sizeof(struct phys_addr)));
		AMAP_SET_BITS(struct amap_default_pdu_context_ext,
			      default_buffer_size, ctxt, entry_size);
		AMAP_SET_BITS(struct amap_default_pdu_context_ext,
			      cq_id_recv, ctxt, cq->id);
	}

	be_dws_cpu_to_le(ctxt, sizeof(req->context));

	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify(ctrl);
	if (!status) {
		struct be_defq_create_resp *resp = embedded_payload(wrb);

		dq->id = le16_to_cpu(resp->id);
		dq->created = true;
	}
	spin_unlock(&ctrl->mbox_lock);

	return status;
}

int be_cmd_wrbq_create(struct be_ctrl_info *ctrl, struct be_dma_mem *q_mem,
		       struct be_queue_info *wrbq)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_wrbq_create_req *req = embedded_payload(wrb);
	struct be_wrbq_create_resp *resp = embedded_payload(wrb);
	int status;

	spin_lock(&ctrl->mbox_lock);
	memset(wrb, 0, sizeof(*wrb));

	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);

	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
		OPCODE_COMMON_ISCSI_WRBQ_CREATE, sizeof(*req));
	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);
	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify(ctrl);
	if (!status) {
		wrbq->id = le16_to_cpu(resp->cid);
		wrbq->created = true;
	}
	spin_unlock(&ctrl->mbox_lock);
	return status;
}

int be_cmd_iscsi_post_template_hdr(struct be_ctrl_info *ctrl,
				    struct be_dma_mem *q_mem)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_post_template_pages_req *req = embedded_payload(wrb);
	int status;

	spin_lock(&ctrl->mbox_lock);

	memset(wrb, 0, sizeof(*wrb));
	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			   OPCODE_COMMON_ADD_TEMPLATE_HEADER_BUFFERS,
			   sizeof(*req));

	req->num_pages = PAGES_4K_SPANNED(q_mem->va, q_mem->size);
	req->type = BEISCSI_TEMPLATE_HDR_TYPE_ISCSI;
	be_cmd_page_addrs_prepare(req->pages, ARRAY_SIZE(req->pages), q_mem);

	status = be_mbox_notify(ctrl);
	spin_unlock(&ctrl->mbox_lock);
	return status;
}

int be_cmd_iscsi_remove_template_hdr(struct be_ctrl_info *ctrl)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_remove_template_pages_req *req = embedded_payload(wrb);
	int status;

	spin_lock(&ctrl->mbox_lock);

	memset(wrb, 0, sizeof(*wrb));
	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			   OPCODE_COMMON_REMOVE_TEMPLATE_HEADER_BUFFERS,
			   sizeof(*req));

	req->type = BEISCSI_TEMPLATE_HDR_TYPE_ISCSI;

	status = be_mbox_notify(ctrl);
	spin_unlock(&ctrl->mbox_lock);
	return status;
}

int be_cmd_iscsi_post_sgl_pages(struct be_ctrl_info *ctrl,
				struct be_dma_mem *q_mem,
				u32 page_offset, u32 num_pages)
{
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_post_sgl_pages_req *req = embedded_payload(wrb);
	struct beiscsi_hba *phba = pci_get_drvdata(ctrl->pdev);
	int status;
	unsigned int curr_pages;
	u32 internal_page_offset = 0;
	u32 temp_num_pages = num_pages;

	if (num_pages == 0xff)
		num_pages = 1;

	spin_lock(&ctrl->mbox_lock);
	do {
		memset(wrb, 0, sizeof(*wrb));
		be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
		be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
				   OPCODE_COMMON_ISCSI_CFG_POST_SGL_PAGES,
				   sizeof(*req));
		curr_pages = BE_NUMBER_OF_FIELD(struct be_post_sgl_pages_req,
						pages);
		req->num_pages = min(num_pages, curr_pages);
		req->page_offset = page_offset;
		be_cmd_page_addrs_prepare(req->pages, req->num_pages, q_mem);
		q_mem->dma = q_mem->dma + (req->num_pages * PAGE_SIZE);
		internal_page_offset += req->num_pages;
		page_offset += req->num_pages;
		num_pages -= req->num_pages;

		if (temp_num_pages == 0xff)
			req->num_pages = temp_num_pages;

		status = be_mbox_notify(ctrl);
		if (status) {
			beiscsi_log(phba, KERN_ERR, BEISCSI_LOG_INIT,
				    "BC_%d : FW CMD to map iscsi frags failed.\n");

			goto error;
		}
	} while (num_pages > 0);
error:
	spin_unlock(&ctrl->mbox_lock);
	if (status != 0)
		beiscsi_cmd_q_destroy(ctrl, NULL, QTYPE_SGL);
	return status;
}

int beiscsi_cmd_reset_function(struct beiscsi_hba  *phba)
{
	struct be_ctrl_info *ctrl = &phba->ctrl;
	struct be_mcc_wrb *wrb = wrb_from_mbox(&ctrl->mbox_mem);
	struct be_post_sgl_pages_req *req = embedded_payload(wrb);
	int status;

	spin_lock(&ctrl->mbox_lock);

	req = embedded_payload(wrb);
	be_wrb_hdr_prepare(wrb, sizeof(*req), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_COMMON,
			   OPCODE_COMMON_FUNCTION_RESET, sizeof(*req));
	status = be_mbox_notify_wait(phba);

	spin_unlock(&ctrl->mbox_lock);
	return status;
}

/**
 * be_cmd_set_vlan()- Configure VLAN paramters on the adapter
 * @phba: device priv structure instance
 * @vlan_tag: TAG to be set
 *
 * Set the VLAN_TAG for the adapter or Disable VLAN on adapter
 *
 * returns
 *	TAG for the MBX Cmd
 * **/
int be_cmd_set_vlan(struct beiscsi_hba *phba,
		     uint16_t vlan_tag)
{
	unsigned int tag = 0;
	struct be_mcc_wrb *wrb;
	struct be_cmd_set_vlan_req *req;
	struct be_ctrl_info *ctrl = &phba->ctrl;

	spin_lock(&ctrl->mbox_lock);
	tag = alloc_mcc_tag(phba);
	if (!tag) {
		spin_unlock(&ctrl->mbox_lock);
		return tag;
	}

	wrb = wrb_from_mccq(phba);
	req = embedded_payload(wrb);
	wrb->tag0 |= tag;
	be_wrb_hdr_prepare(wrb, sizeof(*wrb), true, 0);
	be_cmd_hdr_prepare(&req->hdr, CMD_SUBSYSTEM_ISCSI,
			   OPCODE_COMMON_ISCSI_NTWK_SET_VLAN,
			   sizeof(*req));

	req->interface_hndl = phba->interface_handle;
	req->vlan_priority = vlan_tag;

	be_mcc_notify(phba);
	spin_unlock(&ctrl->mbox_lock);

	return tag;
}
