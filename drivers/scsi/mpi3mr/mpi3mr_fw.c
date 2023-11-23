// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Broadcom MPI3 Storage Controllers
 *
 * Copyright (C) 2017-2023 Broadcom Inc.
 *  (mailto: mpi3mr-linuxdrv.pdl@broadcom.com)
 *
 */

#include "mpi3mr.h"
#include <linux/io-64-nonatomic-lo-hi.h>

static int
mpi3mr_issue_reset(struct mpi3mr_ioc *mrioc, u16 reset_type, u32 reset_reason);
static int mpi3mr_setup_admin_qpair(struct mpi3mr_ioc *mrioc);
static void mpi3mr_process_factsdata(struct mpi3mr_ioc *mrioc,
	struct mpi3_ioc_facts_data *facts_data);
static void mpi3mr_pel_wait_complete(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd);

static int poll_queues;
module_param(poll_queues, int, 0444);
MODULE_PARM_DESC(poll_queues, "Number of queues for io_uring poll mode. (Range 1 - 126)");

#if defined(writeq) && defined(CONFIG_64BIT)
static inline void mpi3mr_writeq(__u64 b, volatile void __iomem *addr)
{
	writeq(b, addr);
}
#else
static inline void mpi3mr_writeq(__u64 b, volatile void __iomem *addr)
{
	__u64 data_out = b;

	writel((u32)(data_out), addr);
	writel((u32)(data_out >> 32), (addr + 4));
}
#endif

static inline bool
mpi3mr_check_req_qfull(struct op_req_qinfo *op_req_q)
{
	u16 pi, ci, max_entries;
	bool is_qfull = false;

	pi = op_req_q->pi;
	ci = READ_ONCE(op_req_q->ci);
	max_entries = op_req_q->num_requests;

	if ((ci == (pi + 1)) || ((!ci) && (pi == (max_entries - 1))))
		is_qfull = true;

	return is_qfull;
}

static void mpi3mr_sync_irqs(struct mpi3mr_ioc *mrioc)
{
	u16 i, max_vectors;

	max_vectors = mrioc->intr_info_count;

	for (i = 0; i < max_vectors; i++)
		synchronize_irq(pci_irq_vector(mrioc->pdev, i));
}

void mpi3mr_ioc_disable_intr(struct mpi3mr_ioc *mrioc)
{
	mrioc->intr_enabled = 0;
	mpi3mr_sync_irqs(mrioc);
}

void mpi3mr_ioc_enable_intr(struct mpi3mr_ioc *mrioc)
{
	mrioc->intr_enabled = 1;
}

static void mpi3mr_cleanup_isr(struct mpi3mr_ioc *mrioc)
{
	u16 i;

	mpi3mr_ioc_disable_intr(mrioc);

	if (!mrioc->intr_info)
		return;

	for (i = 0; i < mrioc->intr_info_count; i++)
		free_irq(pci_irq_vector(mrioc->pdev, i),
		    (mrioc->intr_info + i));

	kfree(mrioc->intr_info);
	mrioc->intr_info = NULL;
	mrioc->intr_info_count = 0;
	mrioc->is_intr_info_set = false;
	pci_free_irq_vectors(mrioc->pdev);
}

void mpi3mr_add_sg_single(void *paddr, u8 flags, u32 length,
	dma_addr_t dma_addr)
{
	struct mpi3_sge_common *sgel = paddr;

	sgel->flags = flags;
	sgel->length = cpu_to_le32(length);
	sgel->address = cpu_to_le64(dma_addr);
}

void mpi3mr_build_zero_len_sge(void *paddr)
{
	u8 sgl_flags = MPI3MR_SGEFLAGS_SYSTEM_SIMPLE_END_OF_LIST;

	mpi3mr_add_sg_single(paddr, sgl_flags, 0, -1);
}

void *mpi3mr_get_reply_virt_addr(struct mpi3mr_ioc *mrioc,
	dma_addr_t phys_addr)
{
	if (!phys_addr)
		return NULL;

	if ((phys_addr < mrioc->reply_buf_dma) ||
	    (phys_addr > mrioc->reply_buf_dma_max_address))
		return NULL;

	return mrioc->reply_buf + (phys_addr - mrioc->reply_buf_dma);
}

void *mpi3mr_get_sensebuf_virt_addr(struct mpi3mr_ioc *mrioc,
	dma_addr_t phys_addr)
{
	if (!phys_addr)
		return NULL;

	return mrioc->sense_buf + (phys_addr - mrioc->sense_buf_dma);
}

static void mpi3mr_repost_reply_buf(struct mpi3mr_ioc *mrioc,
	u64 reply_dma)
{
	u32 old_idx = 0;
	unsigned long flags;

	spin_lock_irqsave(&mrioc->reply_free_queue_lock, flags);
	old_idx  =  mrioc->reply_free_queue_host_index;
	mrioc->reply_free_queue_host_index = (
	    (mrioc->reply_free_queue_host_index ==
	    (mrioc->reply_free_qsz - 1)) ? 0 :
	    (mrioc->reply_free_queue_host_index + 1));
	mrioc->reply_free_q[old_idx] = cpu_to_le64(reply_dma);
	writel(mrioc->reply_free_queue_host_index,
	    &mrioc->sysif_regs->reply_free_host_index);
	spin_unlock_irqrestore(&mrioc->reply_free_queue_lock, flags);
}

void mpi3mr_repost_sense_buf(struct mpi3mr_ioc *mrioc,
	u64 sense_buf_dma)
{
	u32 old_idx = 0;
	unsigned long flags;

	spin_lock_irqsave(&mrioc->sbq_lock, flags);
	old_idx  =  mrioc->sbq_host_index;
	mrioc->sbq_host_index = ((mrioc->sbq_host_index ==
	    (mrioc->sense_buf_q_sz - 1)) ? 0 :
	    (mrioc->sbq_host_index + 1));
	mrioc->sense_buf_q[old_idx] = cpu_to_le64(sense_buf_dma);
	writel(mrioc->sbq_host_index,
	    &mrioc->sysif_regs->sense_buffer_free_host_index);
	spin_unlock_irqrestore(&mrioc->sbq_lock, flags);
}

static void mpi3mr_print_event_data(struct mpi3mr_ioc *mrioc,
	struct mpi3_event_notification_reply *event_reply)
{
	char *desc = NULL;
	u16 event;

	event = event_reply->event;

	switch (event) {
	case MPI3_EVENT_LOG_DATA:
		desc = "Log Data";
		break;
	case MPI3_EVENT_CHANGE:
		desc = "Event Change";
		break;
	case MPI3_EVENT_GPIO_INTERRUPT:
		desc = "GPIO Interrupt";
		break;
	case MPI3_EVENT_CABLE_MGMT:
		desc = "Cable Management";
		break;
	case MPI3_EVENT_ENERGY_PACK_CHANGE:
		desc = "Energy Pack Change";
		break;
	case MPI3_EVENT_DEVICE_ADDED:
	{
		struct mpi3_device_page0 *event_data =
		    (struct mpi3_device_page0 *)event_reply->event_data;
		ioc_info(mrioc, "Device Added: dev=0x%04x Form=0x%x\n",
		    event_data->dev_handle, event_data->device_form);
		return;
	}
	case MPI3_EVENT_DEVICE_INFO_CHANGED:
	{
		struct mpi3_device_page0 *event_data =
		    (struct mpi3_device_page0 *)event_reply->event_data;
		ioc_info(mrioc, "Device Info Changed: dev=0x%04x Form=0x%x\n",
		    event_data->dev_handle, event_data->device_form);
		return;
	}
	case MPI3_EVENT_DEVICE_STATUS_CHANGE:
	{
		struct mpi3_event_data_device_status_change *event_data =
		    (struct mpi3_event_data_device_status_change *)event_reply->event_data;
		ioc_info(mrioc, "Device status Change: dev=0x%04x RC=0x%x\n",
		    event_data->dev_handle, event_data->reason_code);
		return;
	}
	case MPI3_EVENT_SAS_DISCOVERY:
	{
		struct mpi3_event_data_sas_discovery *event_data =
		    (struct mpi3_event_data_sas_discovery *)event_reply->event_data;
		ioc_info(mrioc, "SAS Discovery: (%s) status (0x%08x)\n",
		    (event_data->reason_code == MPI3_EVENT_SAS_DISC_RC_STARTED) ?
		    "start" : "stop",
		    le32_to_cpu(event_data->discovery_status));
		return;
	}
	case MPI3_EVENT_SAS_BROADCAST_PRIMITIVE:
		desc = "SAS Broadcast Primitive";
		break;
	case MPI3_EVENT_SAS_NOTIFY_PRIMITIVE:
		desc = "SAS Notify Primitive";
		break;
	case MPI3_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE:
		desc = "SAS Init Device Status Change";
		break;
	case MPI3_EVENT_SAS_INIT_TABLE_OVERFLOW:
		desc = "SAS Init Table Overflow";
		break;
	case MPI3_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		desc = "SAS Topology Change List";
		break;
	case MPI3_EVENT_ENCL_DEVICE_STATUS_CHANGE:
		desc = "Enclosure Device Status Change";
		break;
	case MPI3_EVENT_ENCL_DEVICE_ADDED:
		desc = "Enclosure Added";
		break;
	case MPI3_EVENT_HARD_RESET_RECEIVED:
		desc = "Hard Reset Received";
		break;
	case MPI3_EVENT_SAS_PHY_COUNTER:
		desc = "SAS PHY Counter";
		break;
	case MPI3_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
		desc = "SAS Device Discovery Error";
		break;
	case MPI3_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
		desc = "PCIE Topology Change List";
		break;
	case MPI3_EVENT_PCIE_ENUMERATION:
	{
		struct mpi3_event_data_pcie_enumeration *event_data =
		    (struct mpi3_event_data_pcie_enumeration *)event_reply->event_data;
		ioc_info(mrioc, "PCIE Enumeration: (%s)",
		    (event_data->reason_code ==
		    MPI3_EVENT_PCIE_ENUM_RC_STARTED) ? "start" : "stop");
		if (event_data->enumeration_status)
			ioc_info(mrioc, "enumeration_status(0x%08x)\n",
			    le32_to_cpu(event_data->enumeration_status));
		return;
	}
	case MPI3_EVENT_PREPARE_FOR_RESET:
		desc = "Prepare For Reset";
		break;
	}

	if (!desc)
		return;

	ioc_info(mrioc, "%s\n", desc);
}

static void mpi3mr_handle_events(struct mpi3mr_ioc *mrioc,
	struct mpi3_default_reply *def_reply)
{
	struct mpi3_event_notification_reply *event_reply =
	    (struct mpi3_event_notification_reply *)def_reply;

	mrioc->change_count = le16_to_cpu(event_reply->ioc_change_count);
	mpi3mr_print_event_data(mrioc, event_reply);
	mpi3mr_os_handle_events(mrioc, event_reply);
}

static struct mpi3mr_drv_cmd *
mpi3mr_get_drv_cmd(struct mpi3mr_ioc *mrioc, u16 host_tag,
	struct mpi3_default_reply *def_reply)
{
	u16 idx;

	switch (host_tag) {
	case MPI3MR_HOSTTAG_INITCMDS:
		return &mrioc->init_cmds;
	case MPI3MR_HOSTTAG_CFG_CMDS:
		return &mrioc->cfg_cmds;
	case MPI3MR_HOSTTAG_BSG_CMDS:
		return &mrioc->bsg_cmds;
	case MPI3MR_HOSTTAG_BLK_TMS:
		return &mrioc->host_tm_cmds;
	case MPI3MR_HOSTTAG_PEL_ABORT:
		return &mrioc->pel_abort_cmd;
	case MPI3MR_HOSTTAG_PEL_WAIT:
		return &mrioc->pel_cmds;
	case MPI3MR_HOSTTAG_TRANSPORT_CMDS:
		return &mrioc->transport_cmds;
	case MPI3MR_HOSTTAG_INVALID:
		if (def_reply && def_reply->function ==
		    MPI3_FUNCTION_EVENT_NOTIFICATION)
			mpi3mr_handle_events(mrioc, def_reply);
		return NULL;
	default:
		break;
	}
	if (host_tag >= MPI3MR_HOSTTAG_DEVRMCMD_MIN &&
	    host_tag <= MPI3MR_HOSTTAG_DEVRMCMD_MAX) {
		idx = host_tag - MPI3MR_HOSTTAG_DEVRMCMD_MIN;
		return &mrioc->dev_rmhs_cmds[idx];
	}

	if (host_tag >= MPI3MR_HOSTTAG_EVTACKCMD_MIN &&
	    host_tag <= MPI3MR_HOSTTAG_EVTACKCMD_MAX) {
		idx = host_tag - MPI3MR_HOSTTAG_EVTACKCMD_MIN;
		return &mrioc->evtack_cmds[idx];
	}

	return NULL;
}

static void mpi3mr_process_admin_reply_desc(struct mpi3mr_ioc *mrioc,
	struct mpi3_default_reply_descriptor *reply_desc, u64 *reply_dma)
{
	u16 reply_desc_type, host_tag = 0;
	u16 ioc_status = MPI3_IOCSTATUS_SUCCESS;
	u32 ioc_loginfo = 0;
	struct mpi3_status_reply_descriptor *status_desc;
	struct mpi3_address_reply_descriptor *addr_desc;
	struct mpi3_success_reply_descriptor *success_desc;
	struct mpi3_default_reply *def_reply = NULL;
	struct mpi3mr_drv_cmd *cmdptr = NULL;
	struct mpi3_scsi_io_reply *scsi_reply;
	u8 *sense_buf = NULL;

	*reply_dma = 0;
	reply_desc_type = le16_to_cpu(reply_desc->reply_flags) &
	    MPI3_REPLY_DESCRIPT_FLAGS_TYPE_MASK;
	switch (reply_desc_type) {
	case MPI3_REPLY_DESCRIPT_FLAGS_TYPE_STATUS:
		status_desc = (struct mpi3_status_reply_descriptor *)reply_desc;
		host_tag = le16_to_cpu(status_desc->host_tag);
		ioc_status = le16_to_cpu(status_desc->ioc_status);
		if (ioc_status &
		    MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_LOGINFOAVAIL)
			ioc_loginfo = le32_to_cpu(status_desc->ioc_log_info);
		ioc_status &= MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_STATUS_MASK;
		break;
	case MPI3_REPLY_DESCRIPT_FLAGS_TYPE_ADDRESS_REPLY:
		addr_desc = (struct mpi3_address_reply_descriptor *)reply_desc;
		*reply_dma = le64_to_cpu(addr_desc->reply_frame_address);
		def_reply = mpi3mr_get_reply_virt_addr(mrioc, *reply_dma);
		if (!def_reply)
			goto out;
		host_tag = le16_to_cpu(def_reply->host_tag);
		ioc_status = le16_to_cpu(def_reply->ioc_status);
		if (ioc_status &
		    MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_LOGINFOAVAIL)
			ioc_loginfo = le32_to_cpu(def_reply->ioc_log_info);
		ioc_status &= MPI3_REPLY_DESCRIPT_STATUS_IOCSTATUS_STATUS_MASK;
		if (def_reply->function == MPI3_FUNCTION_SCSI_IO) {
			scsi_reply = (struct mpi3_scsi_io_reply *)def_reply;
			sense_buf = mpi3mr_get_sensebuf_virt_addr(mrioc,
			    le64_to_cpu(scsi_reply->sense_data_buffer_address));
		}
		break;
	case MPI3_REPLY_DESCRIPT_FLAGS_TYPE_SUCCESS:
		success_desc = (struct mpi3_success_reply_descriptor *)reply_desc;
		host_tag = le16_to_cpu(success_desc->host_tag);
		break;
	default:
		break;
	}

	cmdptr = mpi3mr_get_drv_cmd(mrioc, host_tag, def_reply);
	if (cmdptr) {
		if (cmdptr->state & MPI3MR_CMD_PENDING) {
			cmdptr->state |= MPI3MR_CMD_COMPLETE;
			cmdptr->ioc_loginfo = ioc_loginfo;
			cmdptr->ioc_status = ioc_status;
			cmdptr->state &= ~MPI3MR_CMD_PENDING;
			if (def_reply) {
				cmdptr->state |= MPI3MR_CMD_REPLY_VALID;
				memcpy((u8 *)cmdptr->reply, (u8 *)def_reply,
				    mrioc->reply_sz);
			}
			if (sense_buf && cmdptr->sensebuf) {
				cmdptr->is_sense = 1;
				memcpy(cmdptr->sensebuf, sense_buf,
				       MPI3MR_SENSE_BUF_SZ);
			}
			if (cmdptr->is_waiting) {
				complete(&cmdptr->done);
				cmdptr->is_waiting = 0;
			} else if (cmdptr->callback)
				cmdptr->callback(mrioc, cmdptr);
		}
	}
out:
	if (sense_buf)
		mpi3mr_repost_sense_buf(mrioc,
		    le64_to_cpu(scsi_reply->sense_data_buffer_address));
}

int mpi3mr_process_admin_reply_q(struct mpi3mr_ioc *mrioc)
{
	u32 exp_phase = mrioc->admin_reply_ephase;
	u32 admin_reply_ci = mrioc->admin_reply_ci;
	u32 num_admin_replies = 0;
	u64 reply_dma = 0;
	struct mpi3_default_reply_descriptor *reply_desc;

	if (!atomic_add_unless(&mrioc->admin_reply_q_in_use, 1, 1))
		return 0;

	reply_desc = (struct mpi3_default_reply_descriptor *)mrioc->admin_reply_base +
	    admin_reply_ci;

	if ((le16_to_cpu(reply_desc->reply_flags) &
	    MPI3_REPLY_DESCRIPT_FLAGS_PHASE_MASK) != exp_phase) {
		atomic_dec(&mrioc->admin_reply_q_in_use);
		return 0;
	}

	do {
		if (mrioc->unrecoverable)
			break;

		mrioc->admin_req_ci = le16_to_cpu(reply_desc->request_queue_ci);
		mpi3mr_process_admin_reply_desc(mrioc, reply_desc, &reply_dma);
		if (reply_dma)
			mpi3mr_repost_reply_buf(mrioc, reply_dma);
		num_admin_replies++;
		if (++admin_reply_ci == mrioc->num_admin_replies) {
			admin_reply_ci = 0;
			exp_phase ^= 1;
		}
		reply_desc =
		    (struct mpi3_default_reply_descriptor *)mrioc->admin_reply_base +
		    admin_reply_ci;
		if ((le16_to_cpu(reply_desc->reply_flags) &
		    MPI3_REPLY_DESCRIPT_FLAGS_PHASE_MASK) != exp_phase)
			break;
	} while (1);

	writel(admin_reply_ci, &mrioc->sysif_regs->admin_reply_queue_ci);
	mrioc->admin_reply_ci = admin_reply_ci;
	mrioc->admin_reply_ephase = exp_phase;
	atomic_dec(&mrioc->admin_reply_q_in_use);

	return num_admin_replies;
}

/**
 * mpi3mr_get_reply_desc - get reply descriptor frame corresponding to
 *	queue's consumer index from operational reply descriptor queue.
 * @op_reply_q: op_reply_qinfo object
 * @reply_ci: operational reply descriptor's queue consumer index
 *
 * Returns reply descriptor frame address
 */
static inline struct mpi3_default_reply_descriptor *
mpi3mr_get_reply_desc(struct op_reply_qinfo *op_reply_q, u32 reply_ci)
{
	void *segment_base_addr;
	struct segments *segments = op_reply_q->q_segments;
	struct mpi3_default_reply_descriptor *reply_desc = NULL;

	segment_base_addr =
	    segments[reply_ci / op_reply_q->segment_qd].segment;
	reply_desc = (struct mpi3_default_reply_descriptor *)segment_base_addr +
	    (reply_ci % op_reply_q->segment_qd);
	return reply_desc;
}

/**
 * mpi3mr_process_op_reply_q - Operational reply queue handler
 * @mrioc: Adapter instance reference
 * @op_reply_q: Operational reply queue info
 *
 * Checks the specific operational reply queue and drains the
 * reply queue entries until the queue is empty and process the
 * individual reply descriptors.
 *
 * Return: 0 if queue is already processed,or number of reply
 *	    descriptors processed.
 */
int mpi3mr_process_op_reply_q(struct mpi3mr_ioc *mrioc,
	struct op_reply_qinfo *op_reply_q)
{
	struct op_req_qinfo *op_req_q;
	u32 exp_phase;
	u32 reply_ci;
	u32 num_op_reply = 0;
	u64 reply_dma = 0;
	struct mpi3_default_reply_descriptor *reply_desc;
	u16 req_q_idx = 0, reply_qidx;

	reply_qidx = op_reply_q->qid - 1;

	if (!atomic_add_unless(&op_reply_q->in_use, 1, 1))
		return 0;

	exp_phase = op_reply_q->ephase;
	reply_ci = op_reply_q->ci;

	reply_desc = mpi3mr_get_reply_desc(op_reply_q, reply_ci);
	if ((le16_to_cpu(reply_desc->reply_flags) &
	    MPI3_REPLY_DESCRIPT_FLAGS_PHASE_MASK) != exp_phase) {
		atomic_dec(&op_reply_q->in_use);
		return 0;
	}

	do {
		if (mrioc->unrecoverable)
			break;

		req_q_idx = le16_to_cpu(reply_desc->request_queue_id) - 1;
		op_req_q = &mrioc->req_qinfo[req_q_idx];

		WRITE_ONCE(op_req_q->ci, le16_to_cpu(reply_desc->request_queue_ci));
		mpi3mr_process_op_reply_desc(mrioc, reply_desc, &reply_dma,
		    reply_qidx);
		atomic_dec(&op_reply_q->pend_ios);
		if (reply_dma)
			mpi3mr_repost_reply_buf(mrioc, reply_dma);
		num_op_reply++;

		if (++reply_ci == op_reply_q->num_replies) {
			reply_ci = 0;
			exp_phase ^= 1;
		}

		reply_desc = mpi3mr_get_reply_desc(op_reply_q, reply_ci);

		if ((le16_to_cpu(reply_desc->reply_flags) &
		    MPI3_REPLY_DESCRIPT_FLAGS_PHASE_MASK) != exp_phase)
			break;
#ifndef CONFIG_PREEMPT_RT
		/*
		 * Exit completion loop to avoid CPU lockup
		 * Ensure remaining completion happens from threaded ISR.
		 */
		if (num_op_reply > mrioc->max_host_ios) {
			op_reply_q->enable_irq_poll = true;
			break;
		}
#endif
	} while (1);

	writel(reply_ci,
	    &mrioc->sysif_regs->oper_queue_indexes[reply_qidx].consumer_index);
	op_reply_q->ci = reply_ci;
	op_reply_q->ephase = exp_phase;

	atomic_dec(&op_reply_q->in_use);
	return num_op_reply;
}

/**
 * mpi3mr_blk_mq_poll - Operational reply queue handler
 * @shost: SCSI Host reference
 * @queue_num: Request queue number (w.r.t OS it is hardware context number)
 *
 * Checks the specific operational reply queue and drains the
 * reply queue entries until the queue is empty and process the
 * individual reply descriptors.
 *
 * Return: 0 if queue is already processed,or number of reply
 *	    descriptors processed.
 */
int mpi3mr_blk_mq_poll(struct Scsi_Host *shost, unsigned int queue_num)
{
	int num_entries = 0;
	struct mpi3mr_ioc *mrioc;

	mrioc = (struct mpi3mr_ioc *)shost->hostdata;

	if ((mrioc->reset_in_progress || mrioc->prepare_for_reset ||
	    mrioc->unrecoverable))
		return 0;

	num_entries = mpi3mr_process_op_reply_q(mrioc,
			&mrioc->op_reply_qinfo[queue_num]);

	return num_entries;
}

static irqreturn_t mpi3mr_isr_primary(int irq, void *privdata)
{
	struct mpi3mr_intr_info *intr_info = privdata;
	struct mpi3mr_ioc *mrioc;
	u16 midx;
	u32 num_admin_replies = 0, num_op_reply = 0;

	if (!intr_info)
		return IRQ_NONE;

	mrioc = intr_info->mrioc;

	if (!mrioc->intr_enabled)
		return IRQ_NONE;

	midx = intr_info->msix_index;

	if (!midx)
		num_admin_replies = mpi3mr_process_admin_reply_q(mrioc);
	if (intr_info->op_reply_q)
		num_op_reply = mpi3mr_process_op_reply_q(mrioc,
		    intr_info->op_reply_q);

	if (num_admin_replies || num_op_reply)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

#ifndef CONFIG_PREEMPT_RT

static irqreturn_t mpi3mr_isr(int irq, void *privdata)
{
	struct mpi3mr_intr_info *intr_info = privdata;
	int ret;

	if (!intr_info)
		return IRQ_NONE;

	/* Call primary ISR routine */
	ret = mpi3mr_isr_primary(irq, privdata);

	/*
	 * If more IOs are expected, schedule IRQ polling thread.
	 * Otherwise exit from ISR.
	 */
	if (!intr_info->op_reply_q)
		return ret;

	if (!intr_info->op_reply_q->enable_irq_poll ||
	    !atomic_read(&intr_info->op_reply_q->pend_ios))
		return ret;

	disable_irq_nosync(intr_info->os_irq);

	return IRQ_WAKE_THREAD;
}

/**
 * mpi3mr_isr_poll - Reply queue polling routine
 * @irq: IRQ
 * @privdata: Interrupt info
 *
 * poll for pending I/O completions in a loop until pending I/Os
 * present or controller queue depth I/Os are processed.
 *
 * Return: IRQ_NONE or IRQ_HANDLED
 */
static irqreturn_t mpi3mr_isr_poll(int irq, void *privdata)
{
	struct mpi3mr_intr_info *intr_info = privdata;
	struct mpi3mr_ioc *mrioc;
	u16 midx;
	u32 num_op_reply = 0;

	if (!intr_info || !intr_info->op_reply_q)
		return IRQ_NONE;

	mrioc = intr_info->mrioc;
	midx = intr_info->msix_index;

	/* Poll for pending IOs completions */
	do {
		if (!mrioc->intr_enabled || mrioc->unrecoverable)
			break;

		if (!midx)
			mpi3mr_process_admin_reply_q(mrioc);
		if (intr_info->op_reply_q)
			num_op_reply +=
			    mpi3mr_process_op_reply_q(mrioc,
				intr_info->op_reply_q);

		usleep_range(MPI3MR_IRQ_POLL_SLEEP, 10 * MPI3MR_IRQ_POLL_SLEEP);

	} while (atomic_read(&intr_info->op_reply_q->pend_ios) &&
	    (num_op_reply < mrioc->max_host_ios));

	intr_info->op_reply_q->enable_irq_poll = false;
	enable_irq(intr_info->os_irq);

	return IRQ_HANDLED;
}

#endif

/**
 * mpi3mr_request_irq - Request IRQ and register ISR
 * @mrioc: Adapter instance reference
 * @index: IRQ vector index
 *
 * Request threaded ISR with primary ISR and secondary
 *
 * Return: 0 on success and non zero on failures.
 */
static inline int mpi3mr_request_irq(struct mpi3mr_ioc *mrioc, u16 index)
{
	struct pci_dev *pdev = mrioc->pdev;
	struct mpi3mr_intr_info *intr_info = mrioc->intr_info + index;
	int retval = 0;

	intr_info->mrioc = mrioc;
	intr_info->msix_index = index;
	intr_info->op_reply_q = NULL;

	snprintf(intr_info->name, MPI3MR_NAME_LENGTH, "%s%d-msix%d",
	    mrioc->driver_name, mrioc->id, index);

#ifndef CONFIG_PREEMPT_RT
	retval = request_threaded_irq(pci_irq_vector(pdev, index), mpi3mr_isr,
	    mpi3mr_isr_poll, IRQF_SHARED, intr_info->name, intr_info);
#else
	retval = request_threaded_irq(pci_irq_vector(pdev, index), mpi3mr_isr_primary,
	    NULL, IRQF_SHARED, intr_info->name, intr_info);
#endif
	if (retval) {
		ioc_err(mrioc, "%s: Unable to allocate interrupt %d!\n",
		    intr_info->name, pci_irq_vector(pdev, index));
		return retval;
	}

	intr_info->os_irq = pci_irq_vector(pdev, index);
	return retval;
}

static void mpi3mr_calc_poll_queues(struct mpi3mr_ioc *mrioc, u16 max_vectors)
{
	if (!mrioc->requested_poll_qcount)
		return;

	/* Reserved for Admin and Default Queue */
	if (max_vectors > 2 &&
		(mrioc->requested_poll_qcount < max_vectors - 2)) {
		ioc_info(mrioc,
		    "enabled polled queues (%d) msix (%d)\n",
		    mrioc->requested_poll_qcount, max_vectors);
	} else {
		ioc_info(mrioc,
		    "disabled polled queues (%d) msix (%d) because of no resources for default queue\n",
		    mrioc->requested_poll_qcount, max_vectors);
		mrioc->requested_poll_qcount = 0;
	}
}

/**
 * mpi3mr_setup_isr - Setup ISR for the controller
 * @mrioc: Adapter instance reference
 * @setup_one: Request one IRQ or more
 *
 * Allocate IRQ vectors and call mpi3mr_request_irq to setup ISR
 *
 * Return: 0 on success and non zero on failures.
 */
static int mpi3mr_setup_isr(struct mpi3mr_ioc *mrioc, u8 setup_one)
{
	unsigned int irq_flags = PCI_IRQ_MSIX;
	int max_vectors, min_vec;
	int retval;
	int i;
	struct irq_affinity desc = { .pre_vectors =  1, .post_vectors = 1 };

	if (mrioc->is_intr_info_set)
		return 0;

	mpi3mr_cleanup_isr(mrioc);

	if (setup_one || reset_devices) {
		max_vectors = 1;
		retval = pci_alloc_irq_vectors(mrioc->pdev,
		    1, max_vectors, irq_flags);
		if (retval < 0) {
			ioc_err(mrioc, "cannot allocate irq vectors, ret %d\n",
			    retval);
			goto out_failed;
		}
	} else {
		max_vectors =
		    min_t(int, mrioc->cpu_count + 1 +
			mrioc->requested_poll_qcount, mrioc->msix_count);

		mpi3mr_calc_poll_queues(mrioc, max_vectors);

		ioc_info(mrioc,
		    "MSI-X vectors supported: %d, no of cores: %d,",
		    mrioc->msix_count, mrioc->cpu_count);
		ioc_info(mrioc,
		    "MSI-x vectors requested: %d poll_queues %d\n",
		    max_vectors, mrioc->requested_poll_qcount);

		desc.post_vectors = mrioc->requested_poll_qcount;
		min_vec = desc.pre_vectors + desc.post_vectors;
		irq_flags |= PCI_IRQ_AFFINITY | PCI_IRQ_ALL_TYPES;

		retval = pci_alloc_irq_vectors_affinity(mrioc->pdev,
			min_vec, max_vectors, irq_flags, &desc);

		if (retval < 0) {
			ioc_err(mrioc, "cannot allocate irq vectors, ret %d\n",
			    retval);
			goto out_failed;
		}


		/*
		 * If only one MSI-x is allocated, then MSI-x 0 will be shared
		 * between Admin queue and operational queue
		 */
		if (retval == min_vec)
			mrioc->op_reply_q_offset = 0;
		else if (retval != (max_vectors)) {
			ioc_info(mrioc,
			    "allocated vectors (%d) are less than configured (%d)\n",
			    retval, max_vectors);
		}

		max_vectors = retval;
		mrioc->op_reply_q_offset = (max_vectors > 1) ? 1 : 0;

		mpi3mr_calc_poll_queues(mrioc, max_vectors);

	}

	mrioc->intr_info = kzalloc(sizeof(struct mpi3mr_intr_info) * max_vectors,
	    GFP_KERNEL);
	if (!mrioc->intr_info) {
		retval = -ENOMEM;
		pci_free_irq_vectors(mrioc->pdev);
		goto out_failed;
	}
	for (i = 0; i < max_vectors; i++) {
		retval = mpi3mr_request_irq(mrioc, i);
		if (retval) {
			mrioc->intr_info_count = i;
			goto out_failed;
		}
	}
	if (reset_devices || !setup_one)
		mrioc->is_intr_info_set = true;
	mrioc->intr_info_count = max_vectors;
	mpi3mr_ioc_enable_intr(mrioc);
	return 0;

out_failed:
	mpi3mr_cleanup_isr(mrioc);

	return retval;
}

static const struct {
	enum mpi3mr_iocstate value;
	char *name;
} mrioc_states[] = {
	{ MRIOC_STATE_READY, "ready" },
	{ MRIOC_STATE_FAULT, "fault" },
	{ MRIOC_STATE_RESET, "reset" },
	{ MRIOC_STATE_BECOMING_READY, "becoming ready" },
	{ MRIOC_STATE_RESET_REQUESTED, "reset requested" },
	{ MRIOC_STATE_UNRECOVERABLE, "unrecoverable error" },
};

static const char *mpi3mr_iocstate_name(enum mpi3mr_iocstate mrioc_state)
{
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(mrioc_states); i++) {
		if (mrioc_states[i].value == mrioc_state) {
			name = mrioc_states[i].name;
			break;
		}
	}
	return name;
}

/* Reset reason to name mapper structure*/
static const struct {
	enum mpi3mr_reset_reason value;
	char *name;
} mpi3mr_reset_reason_codes[] = {
	{ MPI3MR_RESET_FROM_BRINGUP, "timeout in bringup" },
	{ MPI3MR_RESET_FROM_FAULT_WATCH, "fault" },
	{ MPI3MR_RESET_FROM_APP, "application invocation" },
	{ MPI3MR_RESET_FROM_EH_HOS, "error handling" },
	{ MPI3MR_RESET_FROM_TM_TIMEOUT, "TM timeout" },
	{ MPI3MR_RESET_FROM_APP_TIMEOUT, "application command timeout" },
	{ MPI3MR_RESET_FROM_MUR_FAILURE, "MUR failure" },
	{ MPI3MR_RESET_FROM_CTLR_CLEANUP, "timeout in controller cleanup" },
	{ MPI3MR_RESET_FROM_CIACTIV_FAULT, "component image activation fault" },
	{ MPI3MR_RESET_FROM_PE_TIMEOUT, "port enable timeout" },
	{ MPI3MR_RESET_FROM_TSU_TIMEOUT, "time stamp update timeout" },
	{ MPI3MR_RESET_FROM_DELREQQ_TIMEOUT, "delete request queue timeout" },
	{ MPI3MR_RESET_FROM_DELREPQ_TIMEOUT, "delete reply queue timeout" },
	{
		MPI3MR_RESET_FROM_CREATEREPQ_TIMEOUT,
		"create request queue timeout"
	},
	{
		MPI3MR_RESET_FROM_CREATEREQQ_TIMEOUT,
		"create reply queue timeout"
	},
	{ MPI3MR_RESET_FROM_IOCFACTS_TIMEOUT, "IOC facts timeout" },
	{ MPI3MR_RESET_FROM_IOCINIT_TIMEOUT, "IOC init timeout" },
	{ MPI3MR_RESET_FROM_EVTNOTIFY_TIMEOUT, "event notify timeout" },
	{ MPI3MR_RESET_FROM_EVTACK_TIMEOUT, "event acknowledgment timeout" },
	{
		MPI3MR_RESET_FROM_CIACTVRST_TIMER,
		"component image activation timeout"
	},
	{
		MPI3MR_RESET_FROM_GETPKGVER_TIMEOUT,
		"get package version timeout"
	},
	{ MPI3MR_RESET_FROM_SYSFS, "sysfs invocation" },
	{ MPI3MR_RESET_FROM_SYSFS_TIMEOUT, "sysfs TM timeout" },
	{ MPI3MR_RESET_FROM_FIRMWARE, "firmware asynchronous reset" },
	{ MPI3MR_RESET_FROM_CFG_REQ_TIMEOUT, "configuration request timeout"},
	{ MPI3MR_RESET_FROM_SAS_TRANSPORT_TIMEOUT, "timeout of a SAS transport layer request" },
};

/**
 * mpi3mr_reset_rc_name - get reset reason code name
 * @reason_code: reset reason code value
 *
 * Map reset reason to an NULL terminated ASCII string
 *
 * Return: name corresponding to reset reason value or NULL.
 */
static const char *mpi3mr_reset_rc_name(enum mpi3mr_reset_reason reason_code)
{
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(mpi3mr_reset_reason_codes); i++) {
		if (mpi3mr_reset_reason_codes[i].value == reason_code) {
			name = mpi3mr_reset_reason_codes[i].name;
			break;
		}
	}
	return name;
}

/* Reset type to name mapper structure*/
static const struct {
	u16 reset_type;
	char *name;
} mpi3mr_reset_types[] = {
	{ MPI3_SYSIF_HOST_DIAG_RESET_ACTION_SOFT_RESET, "soft" },
	{ MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT, "diag fault" },
};

/**
 * mpi3mr_reset_type_name - get reset type name
 * @reset_type: reset type value
 *
 * Map reset type to an NULL terminated ASCII string
 *
 * Return: name corresponding to reset type value or NULL.
 */
static const char *mpi3mr_reset_type_name(u16 reset_type)
{
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(mpi3mr_reset_types); i++) {
		if (mpi3mr_reset_types[i].reset_type == reset_type) {
			name = mpi3mr_reset_types[i].name;
			break;
		}
	}
	return name;
}

/**
 * mpi3mr_print_fault_info - Display fault information
 * @mrioc: Adapter instance reference
 *
 * Display the controller fault information if there is a
 * controller fault.
 *
 * Return: Nothing.
 */
void mpi3mr_print_fault_info(struct mpi3mr_ioc *mrioc)
{
	u32 ioc_status, code, code1, code2, code3;

	ioc_status = readl(&mrioc->sysif_regs->ioc_status);

	if (ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT) {
		code = readl(&mrioc->sysif_regs->fault);
		code1 = readl(&mrioc->sysif_regs->fault_info[0]);
		code2 = readl(&mrioc->sysif_regs->fault_info[1]);
		code3 = readl(&mrioc->sysif_regs->fault_info[2]);

		ioc_info(mrioc,
		    "fault code(0x%08X): Additional code: (0x%08X:0x%08X:0x%08X)\n",
		    code, code1, code2, code3);
	}
}

/**
 * mpi3mr_get_iocstate - Get IOC State
 * @mrioc: Adapter instance reference
 *
 * Return a proper IOC state enum based on the IOC status and
 * IOC configuration and unrcoverable state of the controller.
 *
 * Return: Current IOC state.
 */
enum mpi3mr_iocstate mpi3mr_get_iocstate(struct mpi3mr_ioc *mrioc)
{
	u32 ioc_status, ioc_config;
	u8 ready, enabled;

	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);

	if (mrioc->unrecoverable)
		return MRIOC_STATE_UNRECOVERABLE;
	if (ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT)
		return MRIOC_STATE_FAULT;

	ready = (ioc_status & MPI3_SYSIF_IOC_STATUS_READY);
	enabled = (ioc_config & MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC);

	if (ready && enabled)
		return MRIOC_STATE_READY;
	if ((!ready) && (!enabled))
		return MRIOC_STATE_RESET;
	if ((!ready) && (enabled))
		return MRIOC_STATE_BECOMING_READY;

	return MRIOC_STATE_RESET_REQUESTED;
}

/**
 * mpi3mr_clear_reset_history - clear reset history
 * @mrioc: Adapter instance reference
 *
 * Write the reset history bit in IOC status to clear the bit,
 * if it is already set.
 *
 * Return: Nothing.
 */
static inline void mpi3mr_clear_reset_history(struct mpi3mr_ioc *mrioc)
{
	u32 ioc_status;

	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	if (ioc_status & MPI3_SYSIF_IOC_STATUS_RESET_HISTORY)
		writel(ioc_status, &mrioc->sysif_regs->ioc_status);
}

/**
 * mpi3mr_issue_and_process_mur - Message unit Reset handler
 * @mrioc: Adapter instance reference
 * @reset_reason: Reset reason code
 *
 * Issue Message unit Reset to the controller and wait for it to
 * be complete.
 *
 * Return: 0 on success, -1 on failure.
 */
static int mpi3mr_issue_and_process_mur(struct mpi3mr_ioc *mrioc,
	u32 reset_reason)
{
	u32 ioc_config, timeout, ioc_status;
	int retval = -1;

	ioc_info(mrioc, "Issuing Message unit Reset(MUR)\n");
	if (mrioc->unrecoverable) {
		ioc_info(mrioc, "IOC is unrecoverable MUR not issued\n");
		return retval;
	}
	mpi3mr_clear_reset_history(mrioc);
	writel(reset_reason, &mrioc->sysif_regs->scratchpad[0]);
	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	ioc_config &= ~MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC;
	writel(ioc_config, &mrioc->sysif_regs->ioc_configuration);

	timeout = MPI3MR_MUR_TIMEOUT * 10;
	do {
		ioc_status = readl(&mrioc->sysif_regs->ioc_status);
		if ((ioc_status & MPI3_SYSIF_IOC_STATUS_RESET_HISTORY)) {
			mpi3mr_clear_reset_history(mrioc);
			break;
		}
		if (ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT) {
			mpi3mr_print_fault_info(mrioc);
			break;
		}
		msleep(100);
	} while (--timeout);

	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	if (timeout && !((ioc_status & MPI3_SYSIF_IOC_STATUS_READY) ||
	      (ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT) ||
	      (ioc_config & MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC)))
		retval = 0;

	ioc_info(mrioc, "Base IOC Sts/Config after %s MUR is (0x%x)/(0x%x)\n",
	    (!retval) ? "successful" : "failed", ioc_status, ioc_config);
	return retval;
}

/**
 * mpi3mr_revalidate_factsdata - validate IOCFacts parameters
 * during reset/resume
 * @mrioc: Adapter instance reference
 *
 * Return zero if the new IOCFacts parameters value is compatible with
 * older values else return -EPERM
 */
static int
mpi3mr_revalidate_factsdata(struct mpi3mr_ioc *mrioc)
{
	unsigned long *removepend_bitmap;

	if (mrioc->facts.reply_sz > mrioc->reply_sz) {
		ioc_err(mrioc,
		    "cannot increase reply size from %d to %d\n",
		    mrioc->reply_sz, mrioc->facts.reply_sz);
		return -EPERM;
	}

	if (mrioc->facts.max_op_reply_q < mrioc->num_op_reply_q) {
		ioc_err(mrioc,
		    "cannot reduce number of operational reply queues from %d to %d\n",
		    mrioc->num_op_reply_q,
		    mrioc->facts.max_op_reply_q);
		return -EPERM;
	}

	if (mrioc->facts.max_op_req_q < mrioc->num_op_req_q) {
		ioc_err(mrioc,
		    "cannot reduce number of operational request queues from %d to %d\n",
		    mrioc->num_op_req_q, mrioc->facts.max_op_req_q);
		return -EPERM;
	}

	if (mrioc->shost->max_sectors != (mrioc->facts.max_data_length / 512))
		ioc_err(mrioc, "Warning: The maximum data transfer length\n"
			    "\tchanged after reset: previous(%d), new(%d),\n"
			    "the driver cannot change this at run time\n",
			    mrioc->shost->max_sectors * 512, mrioc->facts.max_data_length);

	if ((mrioc->sas_transport_enabled) && (mrioc->facts.ioc_capabilities &
	    MPI3_IOCFACTS_CAPABILITY_MULTIPATH_ENABLED))
		ioc_err(mrioc,
		    "critical error: multipath capability is enabled at the\n"
		    "\tcontroller while sas transport support is enabled at the\n"
		    "\tdriver, please reboot the system or reload the driver\n");

	if (mrioc->facts.max_devhandle > mrioc->dev_handle_bitmap_bits) {
		removepend_bitmap = bitmap_zalloc(mrioc->facts.max_devhandle,
						  GFP_KERNEL);
		if (!removepend_bitmap) {
			ioc_err(mrioc,
				"failed to increase removepend_bitmap bits from %d to %d\n",
				mrioc->dev_handle_bitmap_bits,
				mrioc->facts.max_devhandle);
			return -EPERM;
		}
		bitmap_free(mrioc->removepend_bitmap);
		mrioc->removepend_bitmap = removepend_bitmap;
		ioc_info(mrioc,
			 "increased bits of dev_handle_bitmap from %d to %d\n",
			 mrioc->dev_handle_bitmap_bits,
			 mrioc->facts.max_devhandle);
		mrioc->dev_handle_bitmap_bits = mrioc->facts.max_devhandle;
	}

	return 0;
}

/**
 * mpi3mr_bring_ioc_ready - Bring controller to ready state
 * @mrioc: Adapter instance reference
 *
 * Set Enable IOC bit in IOC configuration register and wait for
 * the controller to become ready.
 *
 * Return: 0 on success, appropriate error on failure.
 */
static int mpi3mr_bring_ioc_ready(struct mpi3mr_ioc *mrioc)
{
	u32 ioc_config, ioc_status, timeout, host_diagnostic;
	int retval = 0;
	enum mpi3mr_iocstate ioc_state;
	u64 base_info;

	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	base_info = lo_hi_readq(&mrioc->sysif_regs->ioc_information);
	ioc_info(mrioc, "ioc_status(0x%08x), ioc_config(0x%08x), ioc_info(0x%016llx) at the bringup\n",
	    ioc_status, ioc_config, base_info);

	/*The timeout value is in 2sec unit, changing it to seconds*/
	mrioc->ready_timeout =
	    ((base_info & MPI3_SYSIF_IOC_INFO_LOW_TIMEOUT_MASK) >>
	    MPI3_SYSIF_IOC_INFO_LOW_TIMEOUT_SHIFT) * 2;

	ioc_info(mrioc, "ready timeout: %d seconds\n", mrioc->ready_timeout);

	ioc_state = mpi3mr_get_iocstate(mrioc);
	ioc_info(mrioc, "controller is in %s state during detection\n",
	    mpi3mr_iocstate_name(ioc_state));

	if (ioc_state == MRIOC_STATE_BECOMING_READY ||
	    ioc_state == MRIOC_STATE_RESET_REQUESTED) {
		timeout = mrioc->ready_timeout * 10;
		do {
			msleep(100);
		} while (--timeout);

		if (!pci_device_is_present(mrioc->pdev)) {
			mrioc->unrecoverable = 1;
			ioc_err(mrioc,
			    "controller is not present while waiting to reset\n");
			retval = -1;
			goto out_device_not_present;
		}

		ioc_state = mpi3mr_get_iocstate(mrioc);
		ioc_info(mrioc,
		    "controller is in %s state after waiting to reset\n",
		    mpi3mr_iocstate_name(ioc_state));
	}

	if (ioc_state == MRIOC_STATE_READY) {
		ioc_info(mrioc, "issuing message unit reset (MUR) to bring to reset state\n");
		retval = mpi3mr_issue_and_process_mur(mrioc,
		    MPI3MR_RESET_FROM_BRINGUP);
		ioc_state = mpi3mr_get_iocstate(mrioc);
		if (retval)
			ioc_err(mrioc,
			    "message unit reset failed with error %d current state %s\n",
			    retval, mpi3mr_iocstate_name(ioc_state));
	}
	if (ioc_state != MRIOC_STATE_RESET) {
		if (ioc_state == MRIOC_STATE_FAULT) {
			timeout = MPI3_SYSIF_DIAG_SAVE_TIMEOUT * 10;
			mpi3mr_print_fault_info(mrioc);
			do {
				host_diagnostic =
					readl(&mrioc->sysif_regs->host_diagnostic);
				if (!(host_diagnostic &
				      MPI3_SYSIF_HOST_DIAG_SAVE_IN_PROGRESS))
					break;
				if (!pci_device_is_present(mrioc->pdev)) {
					mrioc->unrecoverable = 1;
					ioc_err(mrioc, "controller is not present at the bringup\n");
					goto out_device_not_present;
				}
				msleep(100);
			} while (--timeout);
		}
		mpi3mr_print_fault_info(mrioc);
		ioc_info(mrioc, "issuing soft reset to bring to reset state\n");
		retval = mpi3mr_issue_reset(mrioc,
		    MPI3_SYSIF_HOST_DIAG_RESET_ACTION_SOFT_RESET,
		    MPI3MR_RESET_FROM_BRINGUP);
		if (retval) {
			ioc_err(mrioc,
			    "soft reset failed with error %d\n", retval);
			goto out_failed;
		}
	}
	ioc_state = mpi3mr_get_iocstate(mrioc);
	if (ioc_state != MRIOC_STATE_RESET) {
		ioc_err(mrioc,
		    "cannot bring controller to reset state, current state: %s\n",
		    mpi3mr_iocstate_name(ioc_state));
		goto out_failed;
	}
	mpi3mr_clear_reset_history(mrioc);
	retval = mpi3mr_setup_admin_qpair(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to setup admin queues: error %d\n",
		    retval);
		goto out_failed;
	}

	ioc_info(mrioc, "bringing controller to ready state\n");
	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	ioc_config |= MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC;
	writel(ioc_config, &mrioc->sysif_regs->ioc_configuration);

	timeout = mrioc->ready_timeout * 10;
	do {
		ioc_state = mpi3mr_get_iocstate(mrioc);
		if (ioc_state == MRIOC_STATE_READY) {
			ioc_info(mrioc,
			    "successfully transitioned to %s state\n",
			    mpi3mr_iocstate_name(ioc_state));
			return 0;
		}
		if (!pci_device_is_present(mrioc->pdev)) {
			mrioc->unrecoverable = 1;
			ioc_err(mrioc,
			    "controller is not present at the bringup\n");
			retval = -1;
			goto out_device_not_present;
		}
		msleep(100);
	} while (--timeout);

out_failed:
	ioc_state = mpi3mr_get_iocstate(mrioc);
	ioc_err(mrioc,
	    "failed to bring to ready state,  current state: %s\n",
	    mpi3mr_iocstate_name(ioc_state));
out_device_not_present:
	return retval;
}

/**
 * mpi3mr_soft_reset_success - Check softreset is success or not
 * @ioc_status: IOC status register value
 * @ioc_config: IOC config register value
 *
 * Check whether the soft reset is successful or not based on
 * IOC status and IOC config register values.
 *
 * Return: True when the soft reset is success, false otherwise.
 */
static inline bool
mpi3mr_soft_reset_success(u32 ioc_status, u32 ioc_config)
{
	if (!((ioc_status & MPI3_SYSIF_IOC_STATUS_READY) ||
	    (ioc_config & MPI3_SYSIF_IOC_CONFIG_ENABLE_IOC)))
		return true;
	return false;
}

/**
 * mpi3mr_diagfault_success - Check diag fault is success or not
 * @mrioc: Adapter reference
 * @ioc_status: IOC status register value
 *
 * Check whether the controller hit diag reset fault code.
 *
 * Return: True when there is diag fault, false otherwise.
 */
static inline bool mpi3mr_diagfault_success(struct mpi3mr_ioc *mrioc,
	u32 ioc_status)
{
	u32 fault;

	if (!(ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT))
		return false;
	fault = readl(&mrioc->sysif_regs->fault) & MPI3_SYSIF_FAULT_CODE_MASK;
	if (fault == MPI3_SYSIF_FAULT_CODE_DIAG_FAULT_RESET) {
		mpi3mr_print_fault_info(mrioc);
		return true;
	}
	return false;
}

/**
 * mpi3mr_set_diagsave - Set diag save bit for snapdump
 * @mrioc: Adapter reference
 *
 * Set diag save bit in IOC configuration register to enable
 * snapdump.
 *
 * Return: Nothing.
 */
static inline void mpi3mr_set_diagsave(struct mpi3mr_ioc *mrioc)
{
	u32 ioc_config;

	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	ioc_config |= MPI3_SYSIF_IOC_CONFIG_DIAG_SAVE;
	writel(ioc_config, &mrioc->sysif_regs->ioc_configuration);
}

/**
 * mpi3mr_issue_reset - Issue reset to the controller
 * @mrioc: Adapter reference
 * @reset_type: Reset type
 * @reset_reason: Reset reason code
 *
 * Unlock the host diagnostic registers and write the specific
 * reset type to that, wait for reset acknowledgment from the
 * controller, if the reset is not successful retry for the
 * predefined number of times.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_issue_reset(struct mpi3mr_ioc *mrioc, u16 reset_type,
	u32 reset_reason)
{
	int retval = -1;
	u8 unlock_retry_count = 0;
	u32 host_diagnostic, ioc_status, ioc_config;
	u32 timeout = MPI3MR_RESET_ACK_TIMEOUT * 10;

	if ((reset_type != MPI3_SYSIF_HOST_DIAG_RESET_ACTION_SOFT_RESET) &&
	    (reset_type != MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT))
		return retval;
	if (mrioc->unrecoverable)
		return retval;
	if (reset_reason == MPI3MR_RESET_FROM_FIRMWARE) {
		retval = 0;
		return retval;
	}

	ioc_info(mrioc, "%s reset due to %s(0x%x)\n",
	    mpi3mr_reset_type_name(reset_type),
	    mpi3mr_reset_rc_name(reset_reason), reset_reason);

	mpi3mr_clear_reset_history(mrioc);
	do {
		ioc_info(mrioc,
		    "Write magic sequence to unlock host diag register (retry=%d)\n",
		    ++unlock_retry_count);
		if (unlock_retry_count >= MPI3MR_HOSTDIAG_UNLOCK_RETRY_COUNT) {
			ioc_err(mrioc,
			    "%s reset failed due to unlock failure, host_diagnostic(0x%08x)\n",
			    mpi3mr_reset_type_name(reset_type),
			    host_diagnostic);
			mrioc->unrecoverable = 1;
			return retval;
		}

		writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_FLUSH,
		    &mrioc->sysif_regs->write_sequence);
		writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_1ST,
		    &mrioc->sysif_regs->write_sequence);
		writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_2ND,
		    &mrioc->sysif_regs->write_sequence);
		writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_3RD,
		    &mrioc->sysif_regs->write_sequence);
		writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_4TH,
		    &mrioc->sysif_regs->write_sequence);
		writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_5TH,
		    &mrioc->sysif_regs->write_sequence);
		writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_6TH,
		    &mrioc->sysif_regs->write_sequence);
		usleep_range(1000, 1100);
		host_diagnostic = readl(&mrioc->sysif_regs->host_diagnostic);
		ioc_info(mrioc,
		    "wrote magic sequence: retry_count(%d), host_diagnostic(0x%08x)\n",
		    unlock_retry_count, host_diagnostic);
	} while (!(host_diagnostic & MPI3_SYSIF_HOST_DIAG_DIAG_WRITE_ENABLE));

	writel(reset_reason, &mrioc->sysif_regs->scratchpad[0]);
	writel(host_diagnostic | reset_type,
	    &mrioc->sysif_regs->host_diagnostic);
	switch (reset_type) {
	case MPI3_SYSIF_HOST_DIAG_RESET_ACTION_SOFT_RESET:
		do {
			ioc_status = readl(&mrioc->sysif_regs->ioc_status);
			ioc_config =
			    readl(&mrioc->sysif_regs->ioc_configuration);
			if ((ioc_status & MPI3_SYSIF_IOC_STATUS_RESET_HISTORY)
			    && mpi3mr_soft_reset_success(ioc_status, ioc_config)
			    ) {
				mpi3mr_clear_reset_history(mrioc);
				retval = 0;
				break;
			}
			msleep(100);
		} while (--timeout);
		mpi3mr_print_fault_info(mrioc);
		break;
	case MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT:
		do {
			ioc_status = readl(&mrioc->sysif_regs->ioc_status);
			if (mpi3mr_diagfault_success(mrioc, ioc_status)) {
				retval = 0;
				break;
			}
			msleep(100);
		} while (--timeout);
		break;
	default:
		break;
	}

	writel(MPI3_SYSIF_WRITE_SEQUENCE_KEY_VALUE_2ND,
	    &mrioc->sysif_regs->write_sequence);

	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	ioc_info(mrioc,
	    "ioc_status/ioc_onfig after %s reset is (0x%x)/(0x%x)\n",
	    (!retval)?"successful":"failed", ioc_status,
	    ioc_config);
	if (retval)
		mrioc->unrecoverable = 1;
	return retval;
}

/**
 * mpi3mr_admin_request_post - Post request to admin queue
 * @mrioc: Adapter reference
 * @admin_req: MPI3 request
 * @admin_req_sz: Request size
 * @ignore_reset: Ignore reset in process
 *
 * Post the MPI3 request into admin request queue and
 * inform the controller, if the queue is full return
 * appropriate error.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_admin_request_post(struct mpi3mr_ioc *mrioc, void *admin_req,
	u16 admin_req_sz, u8 ignore_reset)
{
	u16 areq_pi = 0, areq_ci = 0, max_entries = 0;
	int retval = 0;
	unsigned long flags;
	u8 *areq_entry;

	if (mrioc->unrecoverable) {
		ioc_err(mrioc, "%s : Unrecoverable controller\n", __func__);
		return -EFAULT;
	}

	spin_lock_irqsave(&mrioc->admin_req_lock, flags);
	areq_pi = mrioc->admin_req_pi;
	areq_ci = mrioc->admin_req_ci;
	max_entries = mrioc->num_admin_req;
	if ((areq_ci == (areq_pi + 1)) || ((!areq_ci) &&
	    (areq_pi == (max_entries - 1)))) {
		ioc_err(mrioc, "AdminReqQ full condition detected\n");
		retval = -EAGAIN;
		goto out;
	}
	if (!ignore_reset && mrioc->reset_in_progress) {
		ioc_err(mrioc, "AdminReqQ submit reset in progress\n");
		retval = -EAGAIN;
		goto out;
	}
	areq_entry = (u8 *)mrioc->admin_req_base +
	    (areq_pi * MPI3MR_ADMIN_REQ_FRAME_SZ);
	memset(areq_entry, 0, MPI3MR_ADMIN_REQ_FRAME_SZ);
	memcpy(areq_entry, (u8 *)admin_req, admin_req_sz);

	if (++areq_pi == max_entries)
		areq_pi = 0;
	mrioc->admin_req_pi = areq_pi;

	writel(mrioc->admin_req_pi, &mrioc->sysif_regs->admin_request_queue_pi);

out:
	spin_unlock_irqrestore(&mrioc->admin_req_lock, flags);

	return retval;
}

/**
 * mpi3mr_free_op_req_q_segments - free request memory segments
 * @mrioc: Adapter instance reference
 * @q_idx: operational request queue index
 *
 * Free memory segments allocated for operational request queue
 *
 * Return: Nothing.
 */
static void mpi3mr_free_op_req_q_segments(struct mpi3mr_ioc *mrioc, u16 q_idx)
{
	u16 j;
	int size;
	struct segments *segments;

	segments = mrioc->req_qinfo[q_idx].q_segments;
	if (!segments)
		return;

	if (mrioc->enable_segqueue) {
		size = MPI3MR_OP_REQ_Q_SEG_SIZE;
		if (mrioc->req_qinfo[q_idx].q_segment_list) {
			dma_free_coherent(&mrioc->pdev->dev,
			    MPI3MR_MAX_SEG_LIST_SIZE,
			    mrioc->req_qinfo[q_idx].q_segment_list,
			    mrioc->req_qinfo[q_idx].q_segment_list_dma);
			mrioc->req_qinfo[q_idx].q_segment_list = NULL;
		}
	} else
		size = mrioc->req_qinfo[q_idx].segment_qd *
		    mrioc->facts.op_req_sz;

	for (j = 0; j < mrioc->req_qinfo[q_idx].num_segments; j++) {
		if (!segments[j].segment)
			continue;
		dma_free_coherent(&mrioc->pdev->dev,
		    size, segments[j].segment, segments[j].segment_dma);
		segments[j].segment = NULL;
	}
	kfree(mrioc->req_qinfo[q_idx].q_segments);
	mrioc->req_qinfo[q_idx].q_segments = NULL;
	mrioc->req_qinfo[q_idx].qid = 0;
}

/**
 * mpi3mr_free_op_reply_q_segments - free reply memory segments
 * @mrioc: Adapter instance reference
 * @q_idx: operational reply queue index
 *
 * Free memory segments allocated for operational reply queue
 *
 * Return: Nothing.
 */
static void mpi3mr_free_op_reply_q_segments(struct mpi3mr_ioc *mrioc, u16 q_idx)
{
	u16 j;
	int size;
	struct segments *segments;

	segments = mrioc->op_reply_qinfo[q_idx].q_segments;
	if (!segments)
		return;

	if (mrioc->enable_segqueue) {
		size = MPI3MR_OP_REP_Q_SEG_SIZE;
		if (mrioc->op_reply_qinfo[q_idx].q_segment_list) {
			dma_free_coherent(&mrioc->pdev->dev,
			    MPI3MR_MAX_SEG_LIST_SIZE,
			    mrioc->op_reply_qinfo[q_idx].q_segment_list,
			    mrioc->op_reply_qinfo[q_idx].q_segment_list_dma);
			mrioc->op_reply_qinfo[q_idx].q_segment_list = NULL;
		}
	} else
		size = mrioc->op_reply_qinfo[q_idx].segment_qd *
		    mrioc->op_reply_desc_sz;

	for (j = 0; j < mrioc->op_reply_qinfo[q_idx].num_segments; j++) {
		if (!segments[j].segment)
			continue;
		dma_free_coherent(&mrioc->pdev->dev,
		    size, segments[j].segment, segments[j].segment_dma);
		segments[j].segment = NULL;
	}

	kfree(mrioc->op_reply_qinfo[q_idx].q_segments);
	mrioc->op_reply_qinfo[q_idx].q_segments = NULL;
	mrioc->op_reply_qinfo[q_idx].qid = 0;
}

/**
 * mpi3mr_delete_op_reply_q - delete operational reply queue
 * @mrioc: Adapter instance reference
 * @qidx: operational reply queue index
 *
 * Delete operatinal reply queue by issuing MPI request
 * through admin queue.
 *
 * Return:  0 on success, non-zero on failure.
 */
static int mpi3mr_delete_op_reply_q(struct mpi3mr_ioc *mrioc, u16 qidx)
{
	struct mpi3_delete_reply_queue_request delq_req;
	struct op_reply_qinfo *op_reply_q = mrioc->op_reply_qinfo + qidx;
	int retval = 0;
	u16 reply_qid = 0, midx;

	reply_qid = op_reply_q->qid;

	midx = REPLY_QUEUE_IDX_TO_MSIX_IDX(qidx, mrioc->op_reply_q_offset);

	if (!reply_qid)	{
		retval = -1;
		ioc_err(mrioc, "Issue DelRepQ: called with invalid ReqQID\n");
		goto out;
	}

	(op_reply_q->qtype == MPI3MR_DEFAULT_QUEUE) ? mrioc->default_qcount-- :
	    mrioc->active_poll_qcount--;

	memset(&delq_req, 0, sizeof(delq_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "Issue DelRepQ: Init command is in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	delq_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	delq_req.function = MPI3_FUNCTION_DELETE_REPLY_QUEUE;
	delq_req.queue_id = cpu_to_le16(reply_qid);

	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &delq_req, sizeof(delq_req),
	    1);
	if (retval) {
		ioc_err(mrioc, "Issue DelRepQ: Admin Post failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "delete reply queue timed out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_DELREPQ_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "Issue DelRepQ: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}
	mrioc->intr_info[midx].op_reply_q = NULL;

	mpi3mr_free_op_reply_q_segments(mrioc, qidx);
out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
out:

	return retval;
}

/**
 * mpi3mr_alloc_op_reply_q_segments -Alloc segmented reply pool
 * @mrioc: Adapter instance reference
 * @qidx: request queue index
 *
 * Allocate segmented memory pools for operational reply
 * queue.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_alloc_op_reply_q_segments(struct mpi3mr_ioc *mrioc, u16 qidx)
{
	struct op_reply_qinfo *op_reply_q = mrioc->op_reply_qinfo + qidx;
	int i, size;
	u64 *q_segment_list_entry = NULL;
	struct segments *segments;

	if (mrioc->enable_segqueue) {
		op_reply_q->segment_qd =
		    MPI3MR_OP_REP_Q_SEG_SIZE / mrioc->op_reply_desc_sz;

		size = MPI3MR_OP_REP_Q_SEG_SIZE;

		op_reply_q->q_segment_list = dma_alloc_coherent(&mrioc->pdev->dev,
		    MPI3MR_MAX_SEG_LIST_SIZE, &op_reply_q->q_segment_list_dma,
		    GFP_KERNEL);
		if (!op_reply_q->q_segment_list)
			return -ENOMEM;
		q_segment_list_entry = (u64 *)op_reply_q->q_segment_list;
	} else {
		op_reply_q->segment_qd = op_reply_q->num_replies;
		size = op_reply_q->num_replies * mrioc->op_reply_desc_sz;
	}

	op_reply_q->num_segments = DIV_ROUND_UP(op_reply_q->num_replies,
	    op_reply_q->segment_qd);

	op_reply_q->q_segments = kcalloc(op_reply_q->num_segments,
	    sizeof(struct segments), GFP_KERNEL);
	if (!op_reply_q->q_segments)
		return -ENOMEM;

	segments = op_reply_q->q_segments;
	for (i = 0; i < op_reply_q->num_segments; i++) {
		segments[i].segment =
		    dma_alloc_coherent(&mrioc->pdev->dev,
		    size, &segments[i].segment_dma, GFP_KERNEL);
		if (!segments[i].segment)
			return -ENOMEM;
		if (mrioc->enable_segqueue)
			q_segment_list_entry[i] =
			    (unsigned long)segments[i].segment_dma;
	}

	return 0;
}

/**
 * mpi3mr_alloc_op_req_q_segments - Alloc segmented req pool.
 * @mrioc: Adapter instance reference
 * @qidx: request queue index
 *
 * Allocate segmented memory pools for operational request
 * queue.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_alloc_op_req_q_segments(struct mpi3mr_ioc *mrioc, u16 qidx)
{
	struct op_req_qinfo *op_req_q = mrioc->req_qinfo + qidx;
	int i, size;
	u64 *q_segment_list_entry = NULL;
	struct segments *segments;

	if (mrioc->enable_segqueue) {
		op_req_q->segment_qd =
		    MPI3MR_OP_REQ_Q_SEG_SIZE / mrioc->facts.op_req_sz;

		size = MPI3MR_OP_REQ_Q_SEG_SIZE;

		op_req_q->q_segment_list = dma_alloc_coherent(&mrioc->pdev->dev,
		    MPI3MR_MAX_SEG_LIST_SIZE, &op_req_q->q_segment_list_dma,
		    GFP_KERNEL);
		if (!op_req_q->q_segment_list)
			return -ENOMEM;
		q_segment_list_entry = (u64 *)op_req_q->q_segment_list;

	} else {
		op_req_q->segment_qd = op_req_q->num_requests;
		size = op_req_q->num_requests * mrioc->facts.op_req_sz;
	}

	op_req_q->num_segments = DIV_ROUND_UP(op_req_q->num_requests,
	    op_req_q->segment_qd);

	op_req_q->q_segments = kcalloc(op_req_q->num_segments,
	    sizeof(struct segments), GFP_KERNEL);
	if (!op_req_q->q_segments)
		return -ENOMEM;

	segments = op_req_q->q_segments;
	for (i = 0; i < op_req_q->num_segments; i++) {
		segments[i].segment =
		    dma_alloc_coherent(&mrioc->pdev->dev,
		    size, &segments[i].segment_dma, GFP_KERNEL);
		if (!segments[i].segment)
			return -ENOMEM;
		if (mrioc->enable_segqueue)
			q_segment_list_entry[i] =
			    (unsigned long)segments[i].segment_dma;
	}

	return 0;
}

/**
 * mpi3mr_create_op_reply_q - create operational reply queue
 * @mrioc: Adapter instance reference
 * @qidx: operational reply queue index
 *
 * Create operatinal reply queue by issuing MPI request
 * through admin queue.
 *
 * Return:  0 on success, non-zero on failure.
 */
static int mpi3mr_create_op_reply_q(struct mpi3mr_ioc *mrioc, u16 qidx)
{
	struct mpi3_create_reply_queue_request create_req;
	struct op_reply_qinfo *op_reply_q = mrioc->op_reply_qinfo + qidx;
	int retval = 0;
	u16 reply_qid = 0, midx;

	reply_qid = op_reply_q->qid;

	midx = REPLY_QUEUE_IDX_TO_MSIX_IDX(qidx, mrioc->op_reply_q_offset);

	if (reply_qid) {
		retval = -1;
		ioc_err(mrioc, "CreateRepQ: called for duplicate qid %d\n",
		    reply_qid);

		return retval;
	}

	reply_qid = qidx + 1;
	op_reply_q->num_replies = MPI3MR_OP_REP_Q_QD;
	if ((mrioc->pdev->device == MPI3_MFGPAGE_DEVID_SAS4116) &&
		!mrioc->pdev->revision)
		op_reply_q->num_replies = MPI3MR_OP_REP_Q_QD4K;
	op_reply_q->ci = 0;
	op_reply_q->ephase = 1;
	atomic_set(&op_reply_q->pend_ios, 0);
	atomic_set(&op_reply_q->in_use, 0);
	op_reply_q->enable_irq_poll = false;

	if (!op_reply_q->q_segments) {
		retval = mpi3mr_alloc_op_reply_q_segments(mrioc, qidx);
		if (retval) {
			mpi3mr_free_op_reply_q_segments(mrioc, qidx);
			goto out;
		}
	}

	memset(&create_req, 0, sizeof(create_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "CreateRepQ: Init command is in use\n");
		goto out_unlock;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	create_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	create_req.function = MPI3_FUNCTION_CREATE_REPLY_QUEUE;
	create_req.queue_id = cpu_to_le16(reply_qid);

	if (midx < (mrioc->intr_info_count - mrioc->requested_poll_qcount))
		op_reply_q->qtype = MPI3MR_DEFAULT_QUEUE;
	else
		op_reply_q->qtype = MPI3MR_POLL_QUEUE;

	if (op_reply_q->qtype == MPI3MR_DEFAULT_QUEUE) {
		create_req.flags =
			MPI3_CREATE_REPLY_QUEUE_FLAGS_INT_ENABLE_ENABLE;
		create_req.msix_index =
			cpu_to_le16(mrioc->intr_info[midx].msix_index);
	} else {
		create_req.msix_index = cpu_to_le16(mrioc->intr_info_count - 1);
		ioc_info(mrioc, "create reply queue(polled): for qid(%d), midx(%d)\n",
			reply_qid, midx);
		if (!mrioc->active_poll_qcount)
			disable_irq_nosync(pci_irq_vector(mrioc->pdev,
			    mrioc->intr_info_count - 1));
	}

	if (mrioc->enable_segqueue) {
		create_req.flags |=
		    MPI3_CREATE_REQUEST_QUEUE_FLAGS_SEGMENTED_SEGMENTED;
		create_req.base_address = cpu_to_le64(
		    op_reply_q->q_segment_list_dma);
	} else
		create_req.base_address = cpu_to_le64(
		    op_reply_q->q_segments[0].segment_dma);

	create_req.size = cpu_to_le16(op_reply_q->num_replies);

	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &create_req,
	    sizeof(create_req), 1);
	if (retval) {
		ioc_err(mrioc, "CreateRepQ: Admin Post failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "create reply queue timed out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_CREATEREPQ_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "CreateRepQ: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}
	op_reply_q->qid = reply_qid;
	if (midx < mrioc->intr_info_count)
		mrioc->intr_info[midx].op_reply_q = op_reply_q;

	(op_reply_q->qtype == MPI3MR_DEFAULT_QUEUE) ? mrioc->default_qcount++ :
	    mrioc->active_poll_qcount++;

out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
out:

	return retval;
}

/**
 * mpi3mr_create_op_req_q - create operational request queue
 * @mrioc: Adapter instance reference
 * @idx: operational request queue index
 * @reply_qid: Reply queue ID
 *
 * Create operatinal request queue by issuing MPI request
 * through admin queue.
 *
 * Return:  0 on success, non-zero on failure.
 */
static int mpi3mr_create_op_req_q(struct mpi3mr_ioc *mrioc, u16 idx,
	u16 reply_qid)
{
	struct mpi3_create_request_queue_request create_req;
	struct op_req_qinfo *op_req_q = mrioc->req_qinfo + idx;
	int retval = 0;
	u16 req_qid = 0;

	req_qid = op_req_q->qid;

	if (req_qid) {
		retval = -1;
		ioc_err(mrioc, "CreateReqQ: called for duplicate qid %d\n",
		    req_qid);

		return retval;
	}
	req_qid = idx + 1;

	op_req_q->num_requests = MPI3MR_OP_REQ_Q_QD;
	op_req_q->ci = 0;
	op_req_q->pi = 0;
	op_req_q->reply_qid = reply_qid;
	spin_lock_init(&op_req_q->q_lock);

	if (!op_req_q->q_segments) {
		retval = mpi3mr_alloc_op_req_q_segments(mrioc, idx);
		if (retval) {
			mpi3mr_free_op_req_q_segments(mrioc, idx);
			goto out;
		}
	}

	memset(&create_req, 0, sizeof(create_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "CreateReqQ: Init command is in use\n");
		goto out_unlock;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	create_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	create_req.function = MPI3_FUNCTION_CREATE_REQUEST_QUEUE;
	create_req.queue_id = cpu_to_le16(req_qid);
	if (mrioc->enable_segqueue) {
		create_req.flags =
		    MPI3_CREATE_REQUEST_QUEUE_FLAGS_SEGMENTED_SEGMENTED;
		create_req.base_address = cpu_to_le64(
		    op_req_q->q_segment_list_dma);
	} else
		create_req.base_address = cpu_to_le64(
		    op_req_q->q_segments[0].segment_dma);
	create_req.reply_queue_id = cpu_to_le16(reply_qid);
	create_req.size = cpu_to_le16(op_req_q->num_requests);

	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &create_req,
	    sizeof(create_req), 1);
	if (retval) {
		ioc_err(mrioc, "CreateReqQ: Admin Post failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "create request queue timed out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_CREATEREQQ_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "CreateReqQ: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}
	op_req_q->qid = req_qid;

out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
out:

	return retval;
}

/**
 * mpi3mr_create_op_queues - create operational queue pairs
 * @mrioc: Adapter instance reference
 *
 * Allocate memory for operational queue meta data and call
 * create request and reply queue functions.
 *
 * Return: 0 on success, non-zero on failures.
 */
static int mpi3mr_create_op_queues(struct mpi3mr_ioc *mrioc)
{
	int retval = 0;
	u16 num_queues = 0, i = 0, msix_count_op_q = 1;

	num_queues = min_t(int, mrioc->facts.max_op_reply_q,
	    mrioc->facts.max_op_req_q);

	msix_count_op_q =
	    mrioc->intr_info_count - mrioc->op_reply_q_offset;
	if (!mrioc->num_queues)
		mrioc->num_queues = min_t(int, num_queues, msix_count_op_q);
	/*
	 * During reset set the num_queues to the number of queues
	 * that was set before the reset.
	 */
	num_queues = mrioc->num_op_reply_q ?
	    mrioc->num_op_reply_q : mrioc->num_queues;
	ioc_info(mrioc, "trying to create %d operational queue pairs\n",
	    num_queues);

	if (!mrioc->req_qinfo) {
		mrioc->req_qinfo = kcalloc(num_queues,
		    sizeof(struct op_req_qinfo), GFP_KERNEL);
		if (!mrioc->req_qinfo) {
			retval = -1;
			goto out_failed;
		}

		mrioc->op_reply_qinfo = kzalloc(sizeof(struct op_reply_qinfo) *
		    num_queues, GFP_KERNEL);
		if (!mrioc->op_reply_qinfo) {
			retval = -1;
			goto out_failed;
		}
	}

	if (mrioc->enable_segqueue)
		ioc_info(mrioc,
		    "allocating operational queues through segmented queues\n");

	for (i = 0; i < num_queues; i++) {
		if (mpi3mr_create_op_reply_q(mrioc, i)) {
			ioc_err(mrioc, "Cannot create OP RepQ %d\n", i);
			break;
		}
		if (mpi3mr_create_op_req_q(mrioc, i,
		    mrioc->op_reply_qinfo[i].qid)) {
			ioc_err(mrioc, "Cannot create OP ReqQ %d\n", i);
			mpi3mr_delete_op_reply_q(mrioc, i);
			break;
		}
	}

	if (i == 0) {
		/* Not even one queue is created successfully*/
		retval = -1;
		goto out_failed;
	}
	mrioc->num_op_reply_q = mrioc->num_op_req_q = i;
	ioc_info(mrioc,
	    "successfully created %d operational queue pairs(default/polled) queue = (%d/%d)\n",
	    mrioc->num_op_reply_q, mrioc->default_qcount,
	    mrioc->active_poll_qcount);

	return retval;
out_failed:
	kfree(mrioc->req_qinfo);
	mrioc->req_qinfo = NULL;

	kfree(mrioc->op_reply_qinfo);
	mrioc->op_reply_qinfo = NULL;

	return retval;
}

/**
 * mpi3mr_op_request_post - Post request to operational queue
 * @mrioc: Adapter reference
 * @op_req_q: Operational request queue info
 * @req: MPI3 request
 *
 * Post the MPI3 request into operational request queue and
 * inform the controller, if the queue is full return
 * appropriate error.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_op_request_post(struct mpi3mr_ioc *mrioc,
	struct op_req_qinfo *op_req_q, u8 *req)
{
	u16 pi = 0, max_entries, reply_qidx = 0, midx;
	int retval = 0;
	unsigned long flags;
	u8 *req_entry;
	void *segment_base_addr;
	u16 req_sz = mrioc->facts.op_req_sz;
	struct segments *segments = op_req_q->q_segments;

	reply_qidx = op_req_q->reply_qid - 1;

	if (mrioc->unrecoverable)
		return -EFAULT;

	spin_lock_irqsave(&op_req_q->q_lock, flags);
	pi = op_req_q->pi;
	max_entries = op_req_q->num_requests;

	if (mpi3mr_check_req_qfull(op_req_q)) {
		midx = REPLY_QUEUE_IDX_TO_MSIX_IDX(
		    reply_qidx, mrioc->op_reply_q_offset);
		mpi3mr_process_op_reply_q(mrioc, mrioc->intr_info[midx].op_reply_q);

		if (mpi3mr_check_req_qfull(op_req_q)) {
			retval = -EAGAIN;
			goto out;
		}
	}

	if (mrioc->reset_in_progress) {
		ioc_err(mrioc, "OpReqQ submit reset in progress\n");
		retval = -EAGAIN;
		goto out;
	}

	segment_base_addr = segments[pi / op_req_q->segment_qd].segment;
	req_entry = (u8 *)segment_base_addr +
	    ((pi % op_req_q->segment_qd) * req_sz);

	memset(req_entry, 0, req_sz);
	memcpy(req_entry, req, MPI3MR_ADMIN_REQ_FRAME_SZ);

	if (++pi == max_entries)
		pi = 0;
	op_req_q->pi = pi;

#ifndef CONFIG_PREEMPT_RT
	if (atomic_inc_return(&mrioc->op_reply_qinfo[reply_qidx].pend_ios)
	    > MPI3MR_IRQ_POLL_TRIGGER_IOCOUNT)
		mrioc->op_reply_qinfo[reply_qidx].enable_irq_poll = true;
#else
	atomic_inc_return(&mrioc->op_reply_qinfo[reply_qidx].pend_ios);
#endif

	writel(op_req_q->pi,
	    &mrioc->sysif_regs->oper_queue_indexes[reply_qidx].producer_index);

out:
	spin_unlock_irqrestore(&op_req_q->q_lock, flags);
	return retval;
}

/**
 * mpi3mr_check_rh_fault_ioc - check reset history and fault
 * controller
 * @mrioc: Adapter instance reference
 * @reason_code: reason code for the fault.
 *
 * This routine will save snapdump and fault the controller with
 * the given reason code if it is not already in the fault or
 * not asynchronosuly reset. This will be used to handle
 * initilaization time faults/resets/timeout as in those cases
 * immediate soft reset invocation is not required.
 *
 * Return:  None.
 */
void mpi3mr_check_rh_fault_ioc(struct mpi3mr_ioc *mrioc, u32 reason_code)
{
	u32 ioc_status, host_diagnostic, timeout;

	if (mrioc->unrecoverable) {
		ioc_err(mrioc, "controller is unrecoverable\n");
		return;
	}

	if (!pci_device_is_present(mrioc->pdev)) {
		mrioc->unrecoverable = 1;
		ioc_err(mrioc, "controller is not present\n");
		return;
	}

	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	if ((ioc_status & MPI3_SYSIF_IOC_STATUS_RESET_HISTORY) ||
	    (ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT)) {
		mpi3mr_print_fault_info(mrioc);
		return;
	}
	mpi3mr_set_diagsave(mrioc);
	mpi3mr_issue_reset(mrioc, MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT,
	    reason_code);
	timeout = MPI3_SYSIF_DIAG_SAVE_TIMEOUT * 10;
	do {
		host_diagnostic = readl(&mrioc->sysif_regs->host_diagnostic);
		if (!(host_diagnostic & MPI3_SYSIF_HOST_DIAG_SAVE_IN_PROGRESS))
			break;
		msleep(100);
	} while (--timeout);
}

/**
 * mpi3mr_sync_timestamp - Issue time stamp sync request
 * @mrioc: Adapter reference
 *
 * Issue IO unit control MPI request to synchornize firmware
 * timestamp with host time.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_sync_timestamp(struct mpi3mr_ioc *mrioc)
{
	ktime_t current_time;
	struct mpi3_iounit_control_request iou_ctrl;
	int retval = 0;

	memset(&iou_ctrl, 0, sizeof(iou_ctrl));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "Issue IOUCTL time_stamp: command is in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	iou_ctrl.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	iou_ctrl.function = MPI3_FUNCTION_IO_UNIT_CONTROL;
	iou_ctrl.operation = MPI3_CTRL_OP_UPDATE_TIMESTAMP;
	current_time = ktime_get_real();
	iou_ctrl.param64[0] = cpu_to_le64(ktime_to_ms(current_time));

	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &iou_ctrl,
	    sizeof(iou_ctrl), 0);
	if (retval) {
		ioc_err(mrioc, "Issue IOUCTL time_stamp: Admin Post failed\n");
		goto out_unlock;
	}

	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "Issue IOUCTL time_stamp: command timed out\n");
		mrioc->init_cmds.is_waiting = 0;
		if (!(mrioc->init_cmds.state & MPI3MR_CMD_RESET))
			mpi3mr_check_rh_fault_ioc(mrioc,
			    MPI3MR_RESET_FROM_TSU_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "Issue IOUCTL time_stamp: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}

out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);

out:
	return retval;
}

/**
 * mpi3mr_print_pkg_ver - display controller fw package version
 * @mrioc: Adapter reference
 *
 * Retrieve firmware package version from the component image
 * header of the controller flash and display it.
 *
 * Return: 0 on success and non-zero on failure.
 */
static int mpi3mr_print_pkg_ver(struct mpi3mr_ioc *mrioc)
{
	struct mpi3_ci_upload_request ci_upload;
	int retval = -1;
	void *data = NULL;
	dma_addr_t data_dma;
	struct mpi3_ci_manifest_mpi *manifest;
	u32 data_len = sizeof(struct mpi3_ci_manifest_mpi);
	u8 sgl_flags = MPI3MR_SGEFLAGS_SYSTEM_SIMPLE_END_OF_LIST;

	data = dma_alloc_coherent(&mrioc->pdev->dev, data_len, &data_dma,
	    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memset(&ci_upload, 0, sizeof(ci_upload));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		ioc_err(mrioc, "sending get package version failed due to command in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	ci_upload.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	ci_upload.function = MPI3_FUNCTION_CI_UPLOAD;
	ci_upload.msg_flags = MPI3_CI_UPLOAD_MSGFLAGS_LOCATION_PRIMARY;
	ci_upload.signature1 = cpu_to_le32(MPI3_IMAGE_HEADER_SIGNATURE1_MANIFEST);
	ci_upload.image_offset = cpu_to_le32(MPI3_IMAGE_HEADER_SIZE);
	ci_upload.segment_size = cpu_to_le32(data_len);

	mpi3mr_add_sg_single(&ci_upload.sgl, sgl_flags, data_len,
	    data_dma);
	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &ci_upload,
	    sizeof(ci_upload), 1);
	if (retval) {
		ioc_err(mrioc, "posting get package version failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "get package version timed out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_GETPKGVER_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    == MPI3_IOCSTATUS_SUCCESS) {
		manifest = (struct mpi3_ci_manifest_mpi *) data;
		if (manifest->manifest_type == MPI3_CI_MANIFEST_TYPE_MPI) {
			ioc_info(mrioc,
			    "firmware package version(%d.%d.%d.%d.%05d-%05d)\n",
			    manifest->package_version.gen_major,
			    manifest->package_version.gen_minor,
			    manifest->package_version.phase_major,
			    manifest->package_version.phase_minor,
			    manifest->package_version.customer_id,
			    manifest->package_version.build_num);
		}
	}
	retval = 0;
out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);

out:
	if (data)
		dma_free_coherent(&mrioc->pdev->dev, data_len, data,
		    data_dma);
	return retval;
}

/**
 * mpi3mr_watchdog_work - watchdog thread to monitor faults
 * @work: work struct
 *
 * Watch dog work periodically executed (1 second interval) to
 * monitor firmware fault and to issue periodic timer sync to
 * the firmware.
 *
 * Return: Nothing.
 */
static void mpi3mr_watchdog_work(struct work_struct *work)
{
	struct mpi3mr_ioc *mrioc =
	    container_of(work, struct mpi3mr_ioc, watchdog_work.work);
	unsigned long flags;
	enum mpi3mr_iocstate ioc_state;
	u32 fault, host_diagnostic, ioc_status;
	u32 reset_reason = MPI3MR_RESET_FROM_FAULT_WATCH;

	if (mrioc->reset_in_progress)
		return;

	if (!mrioc->unrecoverable && !pci_device_is_present(mrioc->pdev)) {
		ioc_err(mrioc, "watchdog could not detect the controller\n");
		mrioc->unrecoverable = 1;
	}

	if (mrioc->unrecoverable) {
		ioc_err(mrioc,
		    "flush pending commands for unrecoverable controller\n");
		mpi3mr_flush_cmds_for_unrecovered_controller(mrioc);
		return;
	}

	if (mrioc->ts_update_counter++ >= MPI3MR_TSUPDATE_INTERVAL) {
		mrioc->ts_update_counter = 0;
		mpi3mr_sync_timestamp(mrioc);
	}

	if ((mrioc->prepare_for_reset) &&
	    ((mrioc->prepare_for_reset_timeout_counter++) >=
	     MPI3MR_PREPARE_FOR_RESET_TIMEOUT)) {
		mpi3mr_soft_reset_handler(mrioc,
		    MPI3MR_RESET_FROM_CIACTVRST_TIMER, 1);
		return;
	}

	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	if (ioc_status & MPI3_SYSIF_IOC_STATUS_RESET_HISTORY) {
		mpi3mr_soft_reset_handler(mrioc, MPI3MR_RESET_FROM_FIRMWARE, 0);
		return;
	}

	/*Check for fault state every one second and issue Soft reset*/
	ioc_state = mpi3mr_get_iocstate(mrioc);
	if (ioc_state != MRIOC_STATE_FAULT)
		goto schedule_work;

	fault = readl(&mrioc->sysif_regs->fault) & MPI3_SYSIF_FAULT_CODE_MASK;
	host_diagnostic = readl(&mrioc->sysif_regs->host_diagnostic);
	if (host_diagnostic & MPI3_SYSIF_HOST_DIAG_SAVE_IN_PROGRESS) {
		if (!mrioc->diagsave_timeout) {
			mpi3mr_print_fault_info(mrioc);
			ioc_warn(mrioc, "diag save in progress\n");
		}
		if ((mrioc->diagsave_timeout++) <= MPI3_SYSIF_DIAG_SAVE_TIMEOUT)
			goto schedule_work;
	}

	mpi3mr_print_fault_info(mrioc);
	mrioc->diagsave_timeout = 0;

	switch (fault) {
	case MPI3_SYSIF_FAULT_CODE_COMPLETE_RESET_NEEDED:
	case MPI3_SYSIF_FAULT_CODE_POWER_CYCLE_REQUIRED:
		ioc_warn(mrioc,
		    "controller requires system power cycle, marking controller as unrecoverable\n");
		mrioc->unrecoverable = 1;
		goto schedule_work;
	case MPI3_SYSIF_FAULT_CODE_SOFT_RESET_IN_PROGRESS:
		goto schedule_work;
	case MPI3_SYSIF_FAULT_CODE_CI_ACTIVATION_RESET:
		reset_reason = MPI3MR_RESET_FROM_CIACTIV_FAULT;
		break;
	default:
		break;
	}
	mpi3mr_soft_reset_handler(mrioc, reset_reason, 0);
	return;

schedule_work:
	spin_lock_irqsave(&mrioc->watchdog_lock, flags);
	if (mrioc->watchdog_work_q)
		queue_delayed_work(mrioc->watchdog_work_q,
		    &mrioc->watchdog_work,
		    msecs_to_jiffies(MPI3MR_WATCHDOG_INTERVAL));
	spin_unlock_irqrestore(&mrioc->watchdog_lock, flags);
	return;
}

/**
 * mpi3mr_start_watchdog - Start watchdog
 * @mrioc: Adapter instance reference
 *
 * Create and start the watchdog thread to monitor controller
 * faults.
 *
 * Return: Nothing.
 */
void mpi3mr_start_watchdog(struct mpi3mr_ioc *mrioc)
{
	if (mrioc->watchdog_work_q)
		return;

	INIT_DELAYED_WORK(&mrioc->watchdog_work, mpi3mr_watchdog_work);
	snprintf(mrioc->watchdog_work_q_name,
	    sizeof(mrioc->watchdog_work_q_name), "watchdog_%s%d", mrioc->name,
	    mrioc->id);
	mrioc->watchdog_work_q =
	    create_singlethread_workqueue(mrioc->watchdog_work_q_name);
	if (!mrioc->watchdog_work_q) {
		ioc_err(mrioc, "%s: failed (line=%d)\n", __func__, __LINE__);
		return;
	}

	if (mrioc->watchdog_work_q)
		queue_delayed_work(mrioc->watchdog_work_q,
		    &mrioc->watchdog_work,
		    msecs_to_jiffies(MPI3MR_WATCHDOG_INTERVAL));
}

/**
 * mpi3mr_stop_watchdog - Stop watchdog
 * @mrioc: Adapter instance reference
 *
 * Stop the watchdog thread created to monitor controller
 * faults.
 *
 * Return: Nothing.
 */
void mpi3mr_stop_watchdog(struct mpi3mr_ioc *mrioc)
{
	unsigned long flags;
	struct workqueue_struct *wq;

	spin_lock_irqsave(&mrioc->watchdog_lock, flags);
	wq = mrioc->watchdog_work_q;
	mrioc->watchdog_work_q = NULL;
	spin_unlock_irqrestore(&mrioc->watchdog_lock, flags);
	if (wq) {
		if (!cancel_delayed_work_sync(&mrioc->watchdog_work))
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

/**
 * mpi3mr_setup_admin_qpair - Setup admin queue pair
 * @mrioc: Adapter instance reference
 *
 * Allocate memory for admin queue pair if required and register
 * the admin queue with the controller.
 *
 * Return: 0 on success, non-zero on failures.
 */
static int mpi3mr_setup_admin_qpair(struct mpi3mr_ioc *mrioc)
{
	int retval = 0;
	u32 num_admin_entries = 0;

	mrioc->admin_req_q_sz = MPI3MR_ADMIN_REQ_Q_SIZE;
	mrioc->num_admin_req = mrioc->admin_req_q_sz /
	    MPI3MR_ADMIN_REQ_FRAME_SZ;
	mrioc->admin_req_ci = mrioc->admin_req_pi = 0;

	mrioc->admin_reply_q_sz = MPI3MR_ADMIN_REPLY_Q_SIZE;
	mrioc->num_admin_replies = mrioc->admin_reply_q_sz /
	    MPI3MR_ADMIN_REPLY_FRAME_SZ;
	mrioc->admin_reply_ci = 0;
	mrioc->admin_reply_ephase = 1;
	atomic_set(&mrioc->admin_reply_q_in_use, 0);

	if (!mrioc->admin_req_base) {
		mrioc->admin_req_base = dma_alloc_coherent(&mrioc->pdev->dev,
		    mrioc->admin_req_q_sz, &mrioc->admin_req_dma, GFP_KERNEL);

		if (!mrioc->admin_req_base) {
			retval = -1;
			goto out_failed;
		}

		mrioc->admin_reply_base = dma_alloc_coherent(&mrioc->pdev->dev,
		    mrioc->admin_reply_q_sz, &mrioc->admin_reply_dma,
		    GFP_KERNEL);

		if (!mrioc->admin_reply_base) {
			retval = -1;
			goto out_failed;
		}
	}

	num_admin_entries = (mrioc->num_admin_replies << 16) |
	    (mrioc->num_admin_req);
	writel(num_admin_entries, &mrioc->sysif_regs->admin_queue_num_entries);
	mpi3mr_writeq(mrioc->admin_req_dma,
	    &mrioc->sysif_regs->admin_request_queue_address);
	mpi3mr_writeq(mrioc->admin_reply_dma,
	    &mrioc->sysif_regs->admin_reply_queue_address);
	writel(mrioc->admin_req_pi, &mrioc->sysif_regs->admin_request_queue_pi);
	writel(mrioc->admin_reply_ci, &mrioc->sysif_regs->admin_reply_queue_ci);
	return retval;

out_failed:

	if (mrioc->admin_reply_base) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->admin_reply_q_sz,
		    mrioc->admin_reply_base, mrioc->admin_reply_dma);
		mrioc->admin_reply_base = NULL;
	}
	if (mrioc->admin_req_base) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->admin_req_q_sz,
		    mrioc->admin_req_base, mrioc->admin_req_dma);
		mrioc->admin_req_base = NULL;
	}
	return retval;
}

/**
 * mpi3mr_issue_iocfacts - Send IOC Facts
 * @mrioc: Adapter instance reference
 * @facts_data: Cached IOC facts data
 *
 * Issue IOC Facts MPI request through admin queue and wait for
 * the completion of it or time out.
 *
 * Return: 0 on success, non-zero on failures.
 */
static int mpi3mr_issue_iocfacts(struct mpi3mr_ioc *mrioc,
	struct mpi3_ioc_facts_data *facts_data)
{
	struct mpi3_ioc_facts_request iocfacts_req;
	void *data = NULL;
	dma_addr_t data_dma;
	u32 data_len = sizeof(*facts_data);
	int retval = 0;
	u8 sgl_flags = MPI3MR_SGEFLAGS_SYSTEM_SIMPLE_END_OF_LIST;

	data = dma_alloc_coherent(&mrioc->pdev->dev, data_len, &data_dma,
	    GFP_KERNEL);

	if (!data) {
		retval = -1;
		goto out;
	}

	memset(&iocfacts_req, 0, sizeof(iocfacts_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "Issue IOCFacts: Init command is in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	iocfacts_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	iocfacts_req.function = MPI3_FUNCTION_IOC_FACTS;

	mpi3mr_add_sg_single(&iocfacts_req.sgl, sgl_flags, data_len,
	    data_dma);

	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &iocfacts_req,
	    sizeof(iocfacts_req), 1);
	if (retval) {
		ioc_err(mrioc, "Issue IOCFacts: Admin Post failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "ioc_facts timed out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_IOCFACTS_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "Issue IOCFacts: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}
	memcpy(facts_data, (u8 *)data, data_len);
	mpi3mr_process_factsdata(mrioc, facts_data);
out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);

out:
	if (data)
		dma_free_coherent(&mrioc->pdev->dev, data_len, data, data_dma);

	return retval;
}

/**
 * mpi3mr_check_reset_dma_mask - Process IOC facts data
 * @mrioc: Adapter instance reference
 *
 * Check whether the new DMA mask requested through IOCFacts by
 * firmware needs to be set, if so set it .
 *
 * Return: 0 on success, non-zero on failure.
 */
static inline int mpi3mr_check_reset_dma_mask(struct mpi3mr_ioc *mrioc)
{
	struct pci_dev *pdev = mrioc->pdev;
	int r;
	u64 facts_dma_mask = DMA_BIT_MASK(mrioc->facts.dma_mask);

	if (!mrioc->facts.dma_mask || (mrioc->dma_mask <= facts_dma_mask))
		return 0;

	ioc_info(mrioc, "Changing DMA mask from 0x%016llx to 0x%016llx\n",
	    mrioc->dma_mask, facts_dma_mask);

	r = dma_set_mask_and_coherent(&pdev->dev, facts_dma_mask);
	if (r) {
		ioc_err(mrioc, "Setting DMA mask to 0x%016llx failed: %d\n",
		    facts_dma_mask, r);
		return r;
	}
	mrioc->dma_mask = facts_dma_mask;
	return r;
}

/**
 * mpi3mr_process_factsdata - Process IOC facts data
 * @mrioc: Adapter instance reference
 * @facts_data: Cached IOC facts data
 *
 * Convert IOC facts data into cpu endianness and cache it in
 * the driver .
 *
 * Return: Nothing.
 */
static void mpi3mr_process_factsdata(struct mpi3mr_ioc *mrioc,
	struct mpi3_ioc_facts_data *facts_data)
{
	u32 ioc_config, req_sz, facts_flags;

	if ((le16_to_cpu(facts_data->ioc_facts_data_length)) !=
	    (sizeof(*facts_data) / 4)) {
		ioc_warn(mrioc,
		    "IOCFactsdata length mismatch driver_sz(%zu) firmware_sz(%d)\n",
		    sizeof(*facts_data),
		    le16_to_cpu(facts_data->ioc_facts_data_length) * 4);
	}

	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	req_sz = 1 << ((ioc_config & MPI3_SYSIF_IOC_CONFIG_OPER_REQ_ENT_SZ) >>
	    MPI3_SYSIF_IOC_CONFIG_OPER_REQ_ENT_SZ_SHIFT);
	if (le16_to_cpu(facts_data->ioc_request_frame_size) != (req_sz / 4)) {
		ioc_err(mrioc,
		    "IOCFacts data reqFrameSize mismatch hw_size(%d) firmware_sz(%d)\n",
		    req_sz / 4, le16_to_cpu(facts_data->ioc_request_frame_size));
	}

	memset(&mrioc->facts, 0, sizeof(mrioc->facts));

	facts_flags = le32_to_cpu(facts_data->flags);
	mrioc->facts.op_req_sz = req_sz;
	mrioc->op_reply_desc_sz = 1 << ((ioc_config &
	    MPI3_SYSIF_IOC_CONFIG_OPER_RPY_ENT_SZ) >>
	    MPI3_SYSIF_IOC_CONFIG_OPER_RPY_ENT_SZ_SHIFT);

	mrioc->facts.ioc_num = facts_data->ioc_number;
	mrioc->facts.who_init = facts_data->who_init;
	mrioc->facts.max_msix_vectors = le16_to_cpu(facts_data->max_msix_vectors);
	mrioc->facts.personality = (facts_flags &
	    MPI3_IOCFACTS_FLAGS_PERSONALITY_MASK);
	mrioc->facts.dma_mask = (facts_flags &
	    MPI3_IOCFACTS_FLAGS_DMA_ADDRESS_WIDTH_MASK) >>
	    MPI3_IOCFACTS_FLAGS_DMA_ADDRESS_WIDTH_SHIFT;
	mrioc->facts.protocol_flags = facts_data->protocol_flags;
	mrioc->facts.mpi_version = le32_to_cpu(facts_data->mpi_version.word);
	mrioc->facts.max_reqs = le16_to_cpu(facts_data->max_outstanding_requests);
	mrioc->facts.product_id = le16_to_cpu(facts_data->product_id);
	mrioc->facts.reply_sz = le16_to_cpu(facts_data->reply_frame_size) * 4;
	mrioc->facts.exceptions = le16_to_cpu(facts_data->ioc_exceptions);
	mrioc->facts.max_perids = le16_to_cpu(facts_data->max_persistent_id);
	mrioc->facts.max_vds = le16_to_cpu(facts_data->max_vds);
	mrioc->facts.max_hpds = le16_to_cpu(facts_data->max_host_pds);
	mrioc->facts.max_advhpds = le16_to_cpu(facts_data->max_adv_host_pds);
	mrioc->facts.max_raid_pds = le16_to_cpu(facts_data->max_raid_pds);
	mrioc->facts.max_nvme = le16_to_cpu(facts_data->max_nvme);
	mrioc->facts.max_pcie_switches =
	    le16_to_cpu(facts_data->max_pcie_switches);
	mrioc->facts.max_sasexpanders =
	    le16_to_cpu(facts_data->max_sas_expanders);
	mrioc->facts.max_data_length = le16_to_cpu(facts_data->max_data_length);
	mrioc->facts.max_sasinitiators =
	    le16_to_cpu(facts_data->max_sas_initiators);
	mrioc->facts.max_enclosures = le16_to_cpu(facts_data->max_enclosures);
	mrioc->facts.min_devhandle = le16_to_cpu(facts_data->min_dev_handle);
	mrioc->facts.max_devhandle = le16_to_cpu(facts_data->max_dev_handle);
	mrioc->facts.max_op_req_q =
	    le16_to_cpu(facts_data->max_operational_request_queues);
	mrioc->facts.max_op_reply_q =
	    le16_to_cpu(facts_data->max_operational_reply_queues);
	mrioc->facts.ioc_capabilities =
	    le32_to_cpu(facts_data->ioc_capabilities);
	mrioc->facts.fw_ver.build_num =
	    le16_to_cpu(facts_data->fw_version.build_num);
	mrioc->facts.fw_ver.cust_id =
	    le16_to_cpu(facts_data->fw_version.customer_id);
	mrioc->facts.fw_ver.ph_minor = facts_data->fw_version.phase_minor;
	mrioc->facts.fw_ver.ph_major = facts_data->fw_version.phase_major;
	mrioc->facts.fw_ver.gen_minor = facts_data->fw_version.gen_minor;
	mrioc->facts.fw_ver.gen_major = facts_data->fw_version.gen_major;
	mrioc->msix_count = min_t(int, mrioc->msix_count,
	    mrioc->facts.max_msix_vectors);
	mrioc->facts.sge_mod_mask = facts_data->sge_modifier_mask;
	mrioc->facts.sge_mod_value = facts_data->sge_modifier_value;
	mrioc->facts.sge_mod_shift = facts_data->sge_modifier_shift;
	mrioc->facts.shutdown_timeout =
	    le16_to_cpu(facts_data->shutdown_timeout);

	mrioc->facts.max_dev_per_tg =
	    facts_data->max_devices_per_throttle_group;
	mrioc->facts.io_throttle_data_length =
	    le16_to_cpu(facts_data->io_throttle_data_length);
	mrioc->facts.max_io_throttle_group =
	    le16_to_cpu(facts_data->max_io_throttle_group);
	mrioc->facts.io_throttle_low = le16_to_cpu(facts_data->io_throttle_low);
	mrioc->facts.io_throttle_high =
	    le16_to_cpu(facts_data->io_throttle_high);

	if (mrioc->facts.max_data_length ==
	    MPI3_IOCFACTS_MAX_DATA_LENGTH_NOT_REPORTED)
		mrioc->facts.max_data_length = MPI3MR_DEFAULT_MAX_IO_SIZE;
	else
		mrioc->facts.max_data_length *= MPI3MR_PAGE_SIZE_4K;
	/* Store in 512b block count */
	if (mrioc->facts.io_throttle_data_length)
		mrioc->io_throttle_data_length =
		    (mrioc->facts.io_throttle_data_length * 2 * 4);
	else
		/* set the length to 1MB + 1K to disable throttle */
		mrioc->io_throttle_data_length = (mrioc->facts.max_data_length / 512) + 2;

	mrioc->io_throttle_high = (mrioc->facts.io_throttle_high * 2 * 1024);
	mrioc->io_throttle_low = (mrioc->facts.io_throttle_low * 2 * 1024);

	ioc_info(mrioc, "ioc_num(%d), maxopQ(%d), maxopRepQ(%d), maxdh(%d),",
	    mrioc->facts.ioc_num, mrioc->facts.max_op_req_q,
	    mrioc->facts.max_op_reply_q, mrioc->facts.max_devhandle);
	ioc_info(mrioc,
	    "maxreqs(%d), mindh(%d) maxvectors(%d) maxperids(%d)\n",
	    mrioc->facts.max_reqs, mrioc->facts.min_devhandle,
	    mrioc->facts.max_msix_vectors, mrioc->facts.max_perids);
	ioc_info(mrioc, "SGEModMask 0x%x SGEModVal 0x%x SGEModShift 0x%x ",
	    mrioc->facts.sge_mod_mask, mrioc->facts.sge_mod_value,
	    mrioc->facts.sge_mod_shift);
	ioc_info(mrioc, "DMA mask %d InitialPE status 0x%x max_data_len (%d)\n",
	    mrioc->facts.dma_mask, (facts_flags &
	    MPI3_IOCFACTS_FLAGS_INITIAL_PORT_ENABLE_MASK), mrioc->facts.max_data_length);
	ioc_info(mrioc,
	    "max_dev_per_throttle_group(%d), max_throttle_groups(%d)\n",
	    mrioc->facts.max_dev_per_tg, mrioc->facts.max_io_throttle_group);
	ioc_info(mrioc,
	   "io_throttle_data_len(%dKiB), io_throttle_high(%dMiB), io_throttle_low(%dMiB)\n",
	   mrioc->facts.io_throttle_data_length * 4,
	   mrioc->facts.io_throttle_high, mrioc->facts.io_throttle_low);
}

/**
 * mpi3mr_alloc_reply_sense_bufs - Send IOC Init
 * @mrioc: Adapter instance reference
 *
 * Allocate and initialize the reply free buffers, sense
 * buffers, reply free queue and sense buffer queue.
 *
 * Return: 0 on success, non-zero on failures.
 */
static int mpi3mr_alloc_reply_sense_bufs(struct mpi3mr_ioc *mrioc)
{
	int retval = 0;
	u32 sz, i;

	if (mrioc->init_cmds.reply)
		return retval;

	mrioc->init_cmds.reply = kzalloc(mrioc->reply_sz, GFP_KERNEL);
	if (!mrioc->init_cmds.reply)
		goto out_failed;

	mrioc->bsg_cmds.reply = kzalloc(mrioc->reply_sz, GFP_KERNEL);
	if (!mrioc->bsg_cmds.reply)
		goto out_failed;

	mrioc->transport_cmds.reply = kzalloc(mrioc->reply_sz, GFP_KERNEL);
	if (!mrioc->transport_cmds.reply)
		goto out_failed;

	for (i = 0; i < MPI3MR_NUM_DEVRMCMD; i++) {
		mrioc->dev_rmhs_cmds[i].reply = kzalloc(mrioc->reply_sz,
		    GFP_KERNEL);
		if (!mrioc->dev_rmhs_cmds[i].reply)
			goto out_failed;
	}

	for (i = 0; i < MPI3MR_NUM_EVTACKCMD; i++) {
		mrioc->evtack_cmds[i].reply = kzalloc(mrioc->reply_sz,
		    GFP_KERNEL);
		if (!mrioc->evtack_cmds[i].reply)
			goto out_failed;
	}

	mrioc->host_tm_cmds.reply = kzalloc(mrioc->reply_sz, GFP_KERNEL);
	if (!mrioc->host_tm_cmds.reply)
		goto out_failed;

	mrioc->pel_cmds.reply = kzalloc(mrioc->reply_sz, GFP_KERNEL);
	if (!mrioc->pel_cmds.reply)
		goto out_failed;

	mrioc->pel_abort_cmd.reply = kzalloc(mrioc->reply_sz, GFP_KERNEL);
	if (!mrioc->pel_abort_cmd.reply)
		goto out_failed;

	mrioc->dev_handle_bitmap_bits = mrioc->facts.max_devhandle;
	mrioc->removepend_bitmap = bitmap_zalloc(mrioc->dev_handle_bitmap_bits,
						 GFP_KERNEL);
	if (!mrioc->removepend_bitmap)
		goto out_failed;

	mrioc->devrem_bitmap = bitmap_zalloc(MPI3MR_NUM_DEVRMCMD, GFP_KERNEL);
	if (!mrioc->devrem_bitmap)
		goto out_failed;

	mrioc->evtack_cmds_bitmap = bitmap_zalloc(MPI3MR_NUM_EVTACKCMD,
						  GFP_KERNEL);
	if (!mrioc->evtack_cmds_bitmap)
		goto out_failed;

	mrioc->num_reply_bufs = mrioc->facts.max_reqs + MPI3MR_NUM_EVT_REPLIES;
	mrioc->reply_free_qsz = mrioc->num_reply_bufs + 1;
	mrioc->num_sense_bufs = mrioc->facts.max_reqs / MPI3MR_SENSEBUF_FACTOR;
	mrioc->sense_buf_q_sz = mrioc->num_sense_bufs + 1;

	/* reply buffer pool, 16 byte align */
	sz = mrioc->num_reply_bufs * mrioc->reply_sz;
	mrioc->reply_buf_pool = dma_pool_create("reply_buf pool",
	    &mrioc->pdev->dev, sz, 16, 0);
	if (!mrioc->reply_buf_pool) {
		ioc_err(mrioc, "reply buf pool: dma_pool_create failed\n");
		goto out_failed;
	}

	mrioc->reply_buf = dma_pool_zalloc(mrioc->reply_buf_pool, GFP_KERNEL,
	    &mrioc->reply_buf_dma);
	if (!mrioc->reply_buf)
		goto out_failed;

	mrioc->reply_buf_dma_max_address = mrioc->reply_buf_dma + sz;

	/* reply free queue, 8 byte align */
	sz = mrioc->reply_free_qsz * 8;
	mrioc->reply_free_q_pool = dma_pool_create("reply_free_q pool",
	    &mrioc->pdev->dev, sz, 8, 0);
	if (!mrioc->reply_free_q_pool) {
		ioc_err(mrioc, "reply_free_q pool: dma_pool_create failed\n");
		goto out_failed;
	}
	mrioc->reply_free_q = dma_pool_zalloc(mrioc->reply_free_q_pool,
	    GFP_KERNEL, &mrioc->reply_free_q_dma);
	if (!mrioc->reply_free_q)
		goto out_failed;

	/* sense buffer pool,  4 byte align */
	sz = mrioc->num_sense_bufs * MPI3MR_SENSE_BUF_SZ;
	mrioc->sense_buf_pool = dma_pool_create("sense_buf pool",
	    &mrioc->pdev->dev, sz, 4, 0);
	if (!mrioc->sense_buf_pool) {
		ioc_err(mrioc, "sense_buf pool: dma_pool_create failed\n");
		goto out_failed;
	}
	mrioc->sense_buf = dma_pool_zalloc(mrioc->sense_buf_pool, GFP_KERNEL,
	    &mrioc->sense_buf_dma);
	if (!mrioc->sense_buf)
		goto out_failed;

	/* sense buffer queue, 8 byte align */
	sz = mrioc->sense_buf_q_sz * 8;
	mrioc->sense_buf_q_pool = dma_pool_create("sense_buf_q pool",
	    &mrioc->pdev->dev, sz, 8, 0);
	if (!mrioc->sense_buf_q_pool) {
		ioc_err(mrioc, "sense_buf_q pool: dma_pool_create failed\n");
		goto out_failed;
	}
	mrioc->sense_buf_q = dma_pool_zalloc(mrioc->sense_buf_q_pool,
	    GFP_KERNEL, &mrioc->sense_buf_q_dma);
	if (!mrioc->sense_buf_q)
		goto out_failed;

	return retval;

out_failed:
	retval = -1;
	return retval;
}

/**
 * mpimr_initialize_reply_sbuf_queues - initialize reply sense
 * buffers
 * @mrioc: Adapter instance reference
 *
 * Helper function to initialize reply and sense buffers along
 * with some debug prints.
 *
 * Return:  None.
 */
static void mpimr_initialize_reply_sbuf_queues(struct mpi3mr_ioc *mrioc)
{
	u32 sz, i;
	dma_addr_t phy_addr;

	sz = mrioc->num_reply_bufs * mrioc->reply_sz;
	ioc_info(mrioc,
	    "reply buf pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB), reply_dma(0x%llx)\n",
	    mrioc->reply_buf, mrioc->num_reply_bufs, mrioc->reply_sz,
	    (sz / 1024), (unsigned long long)mrioc->reply_buf_dma);
	sz = mrioc->reply_free_qsz * 8;
	ioc_info(mrioc,
	    "reply_free_q pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB), reply_dma(0x%llx)\n",
	    mrioc->reply_free_q, mrioc->reply_free_qsz, 8, (sz / 1024),
	    (unsigned long long)mrioc->reply_free_q_dma);
	sz = mrioc->num_sense_bufs * MPI3MR_SENSE_BUF_SZ;
	ioc_info(mrioc,
	    "sense_buf pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB), sense_dma(0x%llx)\n",
	    mrioc->sense_buf, mrioc->num_sense_bufs, MPI3MR_SENSE_BUF_SZ,
	    (sz / 1024), (unsigned long long)mrioc->sense_buf_dma);
	sz = mrioc->sense_buf_q_sz * 8;
	ioc_info(mrioc,
	    "sense_buf_q pool(0x%p): depth(%d), frame_size(%d), pool_size(%d kB), sense_dma(0x%llx)\n",
	    mrioc->sense_buf_q, mrioc->sense_buf_q_sz, 8, (sz / 1024),
	    (unsigned long long)mrioc->sense_buf_q_dma);

	/* initialize Reply buffer Queue */
	for (i = 0, phy_addr = mrioc->reply_buf_dma;
	    i < mrioc->num_reply_bufs; i++, phy_addr += mrioc->reply_sz)
		mrioc->reply_free_q[i] = cpu_to_le64(phy_addr);
	mrioc->reply_free_q[i] = cpu_to_le64(0);

	/* initialize Sense Buffer Queue */
	for (i = 0, phy_addr = mrioc->sense_buf_dma;
	    i < mrioc->num_sense_bufs; i++, phy_addr += MPI3MR_SENSE_BUF_SZ)
		mrioc->sense_buf_q[i] = cpu_to_le64(phy_addr);
	mrioc->sense_buf_q[i] = cpu_to_le64(0);
}

/**
 * mpi3mr_issue_iocinit - Send IOC Init
 * @mrioc: Adapter instance reference
 *
 * Issue IOC Init MPI request through admin queue and wait for
 * the completion of it or time out.
 *
 * Return: 0 on success, non-zero on failures.
 */
static int mpi3mr_issue_iocinit(struct mpi3mr_ioc *mrioc)
{
	struct mpi3_ioc_init_request iocinit_req;
	struct mpi3_driver_info_layout *drv_info;
	dma_addr_t data_dma;
	u32 data_len = sizeof(*drv_info);
	int retval = 0;
	ktime_t current_time;

	drv_info = dma_alloc_coherent(&mrioc->pdev->dev, data_len, &data_dma,
	    GFP_KERNEL);
	if (!drv_info) {
		retval = -1;
		goto out;
	}
	mpimr_initialize_reply_sbuf_queues(mrioc);

	drv_info->information_length = cpu_to_le32(data_len);
	strscpy(drv_info->driver_signature, "Broadcom", sizeof(drv_info->driver_signature));
	strscpy(drv_info->os_name, utsname()->sysname, sizeof(drv_info->os_name));
	strscpy(drv_info->os_version, utsname()->release, sizeof(drv_info->os_version));
	strscpy(drv_info->driver_name, MPI3MR_DRIVER_NAME, sizeof(drv_info->driver_name));
	strscpy(drv_info->driver_version, MPI3MR_DRIVER_VERSION, sizeof(drv_info->driver_version));
	strscpy(drv_info->driver_release_date, MPI3MR_DRIVER_RELDATE,
	    sizeof(drv_info->driver_release_date));
	drv_info->driver_capabilities = 0;
	memcpy((u8 *)&mrioc->driver_info, (u8 *)drv_info,
	    sizeof(mrioc->driver_info));

	memset(&iocinit_req, 0, sizeof(iocinit_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "Issue IOCInit: Init command is in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	iocinit_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	iocinit_req.function = MPI3_FUNCTION_IOC_INIT;
	iocinit_req.mpi_version.mpi3_version.dev = MPI3_VERSION_DEV;
	iocinit_req.mpi_version.mpi3_version.unit = MPI3_VERSION_UNIT;
	iocinit_req.mpi_version.mpi3_version.major = MPI3_VERSION_MAJOR;
	iocinit_req.mpi_version.mpi3_version.minor = MPI3_VERSION_MINOR;
	iocinit_req.who_init = MPI3_WHOINIT_HOST_DRIVER;
	iocinit_req.reply_free_queue_depth = cpu_to_le16(mrioc->reply_free_qsz);
	iocinit_req.reply_free_queue_address =
	    cpu_to_le64(mrioc->reply_free_q_dma);
	iocinit_req.sense_buffer_length = cpu_to_le16(MPI3MR_SENSE_BUF_SZ);
	iocinit_req.sense_buffer_free_queue_depth =
	    cpu_to_le16(mrioc->sense_buf_q_sz);
	iocinit_req.sense_buffer_free_queue_address =
	    cpu_to_le64(mrioc->sense_buf_q_dma);
	iocinit_req.driver_information_address = cpu_to_le64(data_dma);

	current_time = ktime_get_real();
	iocinit_req.time_stamp = cpu_to_le64(ktime_to_ms(current_time));

	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &iocinit_req,
	    sizeof(iocinit_req), 1);
	if (retval) {
		ioc_err(mrioc, "Issue IOCInit: Admin Post failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_IOCINIT_TIMEOUT);
		ioc_err(mrioc, "ioc_init timed out\n");
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "Issue IOCInit: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}

	mrioc->reply_free_queue_host_index = mrioc->num_reply_bufs;
	writel(mrioc->reply_free_queue_host_index,
	    &mrioc->sysif_regs->reply_free_host_index);

	mrioc->sbq_host_index = mrioc->num_sense_bufs;
	writel(mrioc->sbq_host_index,
	    &mrioc->sysif_regs->sense_buffer_free_host_index);
out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);

out:
	if (drv_info)
		dma_free_coherent(&mrioc->pdev->dev, data_len, drv_info,
		    data_dma);

	return retval;
}

/**
 * mpi3mr_unmask_events - Unmask events in event mask bitmap
 * @mrioc: Adapter instance reference
 * @event: MPI event ID
 *
 * Un mask the specific event by resetting the event_mask
 * bitmap.
 *
 * Return: 0 on success, non-zero on failures.
 */
static void mpi3mr_unmask_events(struct mpi3mr_ioc *mrioc, u16 event)
{
	u32 desired_event;
	u8 word;

	if (event >= 128)
		return;

	desired_event = (1 << (event % 32));
	word = event / 32;

	mrioc->event_masks[word] &= ~desired_event;
}

/**
 * mpi3mr_issue_event_notification - Send event notification
 * @mrioc: Adapter instance reference
 *
 * Issue event notification MPI request through admin queue and
 * wait for the completion of it or time out.
 *
 * Return: 0 on success, non-zero on failures.
 */
static int mpi3mr_issue_event_notification(struct mpi3mr_ioc *mrioc)
{
	struct mpi3_event_notification_request evtnotify_req;
	int retval = 0;
	u8 i;

	memset(&evtnotify_req, 0, sizeof(evtnotify_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "Issue EvtNotify: Init command is in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	evtnotify_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	evtnotify_req.function = MPI3_FUNCTION_EVENT_NOTIFICATION;
	for (i = 0; i < MPI3_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		evtnotify_req.event_masks[i] =
		    cpu_to_le32(mrioc->event_masks[i]);
	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &evtnotify_req,
	    sizeof(evtnotify_req), 1);
	if (retval) {
		ioc_err(mrioc, "Issue EvtNotify: Admin Post failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "event notification timed out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_EVTNOTIFY_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "Issue EvtNotify: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}

out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
out:
	return retval;
}

/**
 * mpi3mr_process_event_ack - Process event acknowledgment
 * @mrioc: Adapter instance reference
 * @event: MPI3 event ID
 * @event_ctx: event context
 *
 * Send event acknowledgment through admin queue and wait for
 * it to complete.
 *
 * Return: 0 on success, non-zero on failures.
 */
int mpi3mr_process_event_ack(struct mpi3mr_ioc *mrioc, u8 event,
	u32 event_ctx)
{
	struct mpi3_event_ack_request evtack_req;
	int retval = 0;

	memset(&evtack_req, 0, sizeof(evtack_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "Send EvtAck: Init command is in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->init_cmds.is_waiting = 1;
	mrioc->init_cmds.callback = NULL;
	evtack_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	evtack_req.function = MPI3_FUNCTION_EVENT_ACK;
	evtack_req.event = event;
	evtack_req.event_context = cpu_to_le32(event_ctx);

	init_completion(&mrioc->init_cmds.done);
	retval = mpi3mr_admin_request_post(mrioc, &evtack_req,
	    sizeof(evtack_req), 1);
	if (retval) {
		ioc_err(mrioc, "Send EvtAck: Admin Post failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->init_cmds.done,
	    (MPI3MR_INTADMCMD_TIMEOUT * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "Issue EvtNotify: command timed out\n");
		if (!(mrioc->init_cmds.state & MPI3MR_CMD_RESET))
			mpi3mr_check_rh_fault_ioc(mrioc,
			    MPI3MR_RESET_FROM_EVTACK_TIMEOUT);
		retval = -1;
		goto out_unlock;
	}
	if ((mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK)
	    != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc,
		    "Send EvtAck: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
		    (mrioc->init_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK),
		    mrioc->init_cmds.ioc_loginfo);
		retval = -1;
		goto out_unlock;
	}

out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
out:
	return retval;
}

/**
 * mpi3mr_alloc_chain_bufs - Allocate chain buffers
 * @mrioc: Adapter instance reference
 *
 * Allocate chain buffers and set a bitmap to indicate free
 * chain buffers. Chain buffers are used to pass the SGE
 * information along with MPI3 SCSI IO requests for host I/O.
 *
 * Return: 0 on success, non-zero on failure
 */
static int mpi3mr_alloc_chain_bufs(struct mpi3mr_ioc *mrioc)
{
	int retval = 0;
	u32 sz, i;
	u16 num_chains;

	if (mrioc->chain_sgl_list)
		return retval;

	num_chains = mrioc->max_host_ios / MPI3MR_CHAINBUF_FACTOR;

	if (prot_mask & (SHOST_DIX_TYPE0_PROTECTION
	    | SHOST_DIX_TYPE1_PROTECTION
	    | SHOST_DIX_TYPE2_PROTECTION
	    | SHOST_DIX_TYPE3_PROTECTION))
		num_chains += (num_chains / MPI3MR_CHAINBUFDIX_FACTOR);

	mrioc->chain_buf_count = num_chains;
	sz = sizeof(struct chain_element) * num_chains;
	mrioc->chain_sgl_list = kzalloc(sz, GFP_KERNEL);
	if (!mrioc->chain_sgl_list)
		goto out_failed;

	if (mrioc->max_sgl_entries > (mrioc->facts.max_data_length /
		MPI3MR_PAGE_SIZE_4K))
		mrioc->max_sgl_entries = mrioc->facts.max_data_length /
			MPI3MR_PAGE_SIZE_4K;
	sz = mrioc->max_sgl_entries * sizeof(struct mpi3_sge_common);
	ioc_info(mrioc, "number of sgl entries=%d chain buffer size=%dKB\n",
			mrioc->max_sgl_entries, sz/1024);

	mrioc->chain_buf_pool = dma_pool_create("chain_buf pool",
	    &mrioc->pdev->dev, sz, 16, 0);
	if (!mrioc->chain_buf_pool) {
		ioc_err(mrioc, "chain buf pool: dma_pool_create failed\n");
		goto out_failed;
	}

	for (i = 0; i < num_chains; i++) {
		mrioc->chain_sgl_list[i].addr =
		    dma_pool_zalloc(mrioc->chain_buf_pool, GFP_KERNEL,
		    &mrioc->chain_sgl_list[i].dma_addr);

		if (!mrioc->chain_sgl_list[i].addr)
			goto out_failed;
	}
	mrioc->chain_bitmap = bitmap_zalloc(num_chains, GFP_KERNEL);
	if (!mrioc->chain_bitmap)
		goto out_failed;
	return retval;
out_failed:
	retval = -1;
	return retval;
}

/**
 * mpi3mr_port_enable_complete - Mark port enable complete
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * Call back for asynchronous port enable request sets the
 * driver command to indicate port enable request is complete.
 *
 * Return: Nothing
 */
static void mpi3mr_port_enable_complete(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	drv_cmd->callback = NULL;
	mrioc->scan_started = 0;
	if (drv_cmd->state & MPI3MR_CMD_RESET)
		mrioc->scan_failed = MPI3_IOCSTATUS_INTERNAL_ERROR;
	else
		mrioc->scan_failed = drv_cmd->ioc_status;
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
}

/**
 * mpi3mr_issue_port_enable - Issue Port Enable
 * @mrioc: Adapter instance reference
 * @async: Flag to wait for completion or not
 *
 * Issue Port Enable MPI request through admin queue and if the
 * async flag is not set wait for the completion of the port
 * enable or time out.
 *
 * Return: 0 on success, non-zero on failures.
 */
int mpi3mr_issue_port_enable(struct mpi3mr_ioc *mrioc, u8 async)
{
	struct mpi3_port_enable_request pe_req;
	int retval = 0;
	u32 pe_timeout = MPI3MR_PORTENABLE_TIMEOUT;

	memset(&pe_req, 0, sizeof(pe_req));
	mutex_lock(&mrioc->init_cmds.mutex);
	if (mrioc->init_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "Issue PortEnable: Init command is in use\n");
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}
	mrioc->init_cmds.state = MPI3MR_CMD_PENDING;
	if (async) {
		mrioc->init_cmds.is_waiting = 0;
		mrioc->init_cmds.callback = mpi3mr_port_enable_complete;
	} else {
		mrioc->init_cmds.is_waiting = 1;
		mrioc->init_cmds.callback = NULL;
		init_completion(&mrioc->init_cmds.done);
	}
	pe_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_INITCMDS);
	pe_req.function = MPI3_FUNCTION_PORT_ENABLE;

	retval = mpi3mr_admin_request_post(mrioc, &pe_req, sizeof(pe_req), 1);
	if (retval) {
		ioc_err(mrioc, "Issue PortEnable: Admin Post failed\n");
		goto out_unlock;
	}
	if (async) {
		mutex_unlock(&mrioc->init_cmds.mutex);
		goto out;
	}

	wait_for_completion_timeout(&mrioc->init_cmds.done, (pe_timeout * HZ));
	if (!(mrioc->init_cmds.state & MPI3MR_CMD_COMPLETE)) {
		ioc_err(mrioc, "port enable timed out\n");
		retval = -1;
		mpi3mr_check_rh_fault_ioc(mrioc, MPI3MR_RESET_FROM_PE_TIMEOUT);
		goto out_unlock;
	}
	mpi3mr_port_enable_complete(mrioc, &mrioc->init_cmds);

out_unlock:
	mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->init_cmds.mutex);
out:
	return retval;
}

/* Protocol type to name mapper structure */
static const struct {
	u8 protocol;
	char *name;
} mpi3mr_protocols[] = {
	{ MPI3_IOCFACTS_PROTOCOL_SCSI_INITIATOR, "Initiator" },
	{ MPI3_IOCFACTS_PROTOCOL_SCSI_TARGET, "Target" },
	{ MPI3_IOCFACTS_PROTOCOL_NVME, "NVMe attachment" },
};

/* Capability to name mapper structure*/
static const struct {
	u32 capability;
	char *name;
} mpi3mr_capabilities[] = {
	{ MPI3_IOCFACTS_CAPABILITY_RAID_CAPABLE, "RAID" },
	{ MPI3_IOCFACTS_CAPABILITY_MULTIPATH_ENABLED, "MultiPath" },
};

/**
 * mpi3mr_print_ioc_info - Display controller information
 * @mrioc: Adapter instance reference
 *
 * Display controller personalit, capability, supported
 * protocols etc.
 *
 * Return: Nothing
 */
static void
mpi3mr_print_ioc_info(struct mpi3mr_ioc *mrioc)
{
	int i = 0, bytes_written = 0;
	char personality[16];
	char protocol[50] = {0};
	char capabilities[100] = {0};
	struct mpi3mr_compimg_ver *fwver = &mrioc->facts.fw_ver;

	switch (mrioc->facts.personality) {
	case MPI3_IOCFACTS_FLAGS_PERSONALITY_EHBA:
		strncpy(personality, "Enhanced HBA", sizeof(personality));
		break;
	case MPI3_IOCFACTS_FLAGS_PERSONALITY_RAID_DDR:
		strncpy(personality, "RAID", sizeof(personality));
		break;
	default:
		strncpy(personality, "Unknown", sizeof(personality));
		break;
	}

	ioc_info(mrioc, "Running in %s Personality", personality);

	ioc_info(mrioc, "FW version(%d.%d.%d.%d.%d.%d)\n",
	    fwver->gen_major, fwver->gen_minor, fwver->ph_major,
	    fwver->ph_minor, fwver->cust_id, fwver->build_num);

	for (i = 0; i < ARRAY_SIZE(mpi3mr_protocols); i++) {
		if (mrioc->facts.protocol_flags &
		    mpi3mr_protocols[i].protocol) {
			bytes_written += scnprintf(protocol + bytes_written,
				    sizeof(protocol) - bytes_written, "%s%s",
				    bytes_written ? "," : "",
				    mpi3mr_protocols[i].name);
		}
	}

	bytes_written = 0;
	for (i = 0; i < ARRAY_SIZE(mpi3mr_capabilities); i++) {
		if (mrioc->facts.protocol_flags &
		    mpi3mr_capabilities[i].capability) {
			bytes_written += scnprintf(capabilities + bytes_written,
				    sizeof(capabilities) - bytes_written, "%s%s",
				    bytes_written ? "," : "",
				    mpi3mr_capabilities[i].name);
		}
	}

	ioc_info(mrioc, "Protocol=(%s), Capabilities=(%s)\n",
		 protocol, capabilities);
}

/**
 * mpi3mr_cleanup_resources - Free PCI resources
 * @mrioc: Adapter instance reference
 *
 * Unmap PCI device memory and disable PCI device.
 *
 * Return: 0 on success and non-zero on failure.
 */
void mpi3mr_cleanup_resources(struct mpi3mr_ioc *mrioc)
{
	struct pci_dev *pdev = mrioc->pdev;

	mpi3mr_cleanup_isr(mrioc);

	if (mrioc->sysif_regs) {
		iounmap((void __iomem *)mrioc->sysif_regs);
		mrioc->sysif_regs = NULL;
	}

	if (pci_is_enabled(pdev)) {
		if (mrioc->bars)
			pci_release_selected_regions(pdev, mrioc->bars);
		pci_disable_device(pdev);
	}
}

/**
 * mpi3mr_setup_resources - Enable PCI resources
 * @mrioc: Adapter instance reference
 *
 * Enable PCI device memory, MSI-x registers and set DMA mask.
 *
 * Return: 0 on success and non-zero on failure.
 */
int mpi3mr_setup_resources(struct mpi3mr_ioc *mrioc)
{
	struct pci_dev *pdev = mrioc->pdev;
	u32 memap_sz = 0;
	int i, retval = 0, capb = 0;
	u16 message_control;
	u64 dma_mask = mrioc->dma_mask ? mrioc->dma_mask :
	    ((sizeof(dma_addr_t) > 4) ? DMA_BIT_MASK(64) : DMA_BIT_MASK(32));

	if (pci_enable_device_mem(pdev)) {
		ioc_err(mrioc, "pci_enable_device_mem: failed\n");
		retval = -ENODEV;
		goto out_failed;
	}

	capb = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	if (!capb) {
		ioc_err(mrioc, "Unable to find MSI-X Capabilities\n");
		retval = -ENODEV;
		goto out_failed;
	}
	mrioc->bars = pci_select_bars(pdev, IORESOURCE_MEM);

	if (pci_request_selected_regions(pdev, mrioc->bars,
	    mrioc->driver_name)) {
		ioc_err(mrioc, "pci_request_selected_regions: failed\n");
		retval = -ENODEV;
		goto out_failed;
	}

	for (i = 0; (i < DEVICE_COUNT_RESOURCE); i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
			mrioc->sysif_regs_phys = pci_resource_start(pdev, i);
			memap_sz = pci_resource_len(pdev, i);
			mrioc->sysif_regs =
			    ioremap(mrioc->sysif_regs_phys, memap_sz);
			break;
		}
	}

	pci_set_master(pdev);

	retval = dma_set_mask_and_coherent(&pdev->dev, dma_mask);
	if (retval) {
		if (dma_mask != DMA_BIT_MASK(32)) {
			ioc_warn(mrioc, "Setting 64 bit DMA mask failed\n");
			dma_mask = DMA_BIT_MASK(32);
			retval = dma_set_mask_and_coherent(&pdev->dev,
			    dma_mask);
		}
		if (retval) {
			mrioc->dma_mask = 0;
			ioc_err(mrioc, "Setting 32 bit DMA mask also failed\n");
			goto out_failed;
		}
	}
	mrioc->dma_mask = dma_mask;

	if (!mrioc->sysif_regs) {
		ioc_err(mrioc,
		    "Unable to map adapter memory or resource not found\n");
		retval = -EINVAL;
		goto out_failed;
	}

	pci_read_config_word(pdev, capb + 2, &message_control);
	mrioc->msix_count = (message_control & 0x3FF) + 1;

	pci_save_state(pdev);

	pci_set_drvdata(pdev, mrioc->shost);

	mpi3mr_ioc_disable_intr(mrioc);

	ioc_info(mrioc, "iomem(0x%016llx), mapped(0x%p), size(%d)\n",
	    (unsigned long long)mrioc->sysif_regs_phys,
	    mrioc->sysif_regs, memap_sz);
	ioc_info(mrioc, "Number of MSI-X vectors found in capabilities: (%d)\n",
	    mrioc->msix_count);

	if (!reset_devices && poll_queues > 0)
		mrioc->requested_poll_qcount = min_t(int, poll_queues,
				mrioc->msix_count - 2);
	return retval;

out_failed:
	mpi3mr_cleanup_resources(mrioc);
	return retval;
}

/**
 * mpi3mr_enable_events - Enable required events
 * @mrioc: Adapter instance reference
 *
 * This routine unmasks the events required by the driver by
 * sennding appropriate event mask bitmapt through an event
 * notification request.
 *
 * Return: 0 on success and non-zero on failure.
 */
static int mpi3mr_enable_events(struct mpi3mr_ioc *mrioc)
{
	int retval = 0;
	u32  i;

	for (i = 0; i < MPI3_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		mrioc->event_masks[i] = -1;

	mpi3mr_unmask_events(mrioc, MPI3_EVENT_DEVICE_ADDED);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_DEVICE_INFO_CHANGED);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_DEVICE_STATUS_CHANGE);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_ENCL_DEVICE_STATUS_CHANGE);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_ENCL_DEVICE_ADDED);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_SAS_TOPOLOGY_CHANGE_LIST);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_SAS_DISCOVERY);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_SAS_DEVICE_DISCOVERY_ERROR);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_SAS_BROADCAST_PRIMITIVE);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_PCIE_TOPOLOGY_CHANGE_LIST);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_PCIE_ENUMERATION);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_PREPARE_FOR_RESET);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_CABLE_MGMT);
	mpi3mr_unmask_events(mrioc, MPI3_EVENT_ENERGY_PACK_CHANGE);

	retval = mpi3mr_issue_event_notification(mrioc);
	if (retval)
		ioc_err(mrioc, "failed to issue event notification %d\n",
		    retval);
	return retval;
}

/**
 * mpi3mr_init_ioc - Initialize the controller
 * @mrioc: Adapter instance reference
 *
 * This the controller initialization routine, executed either
 * after soft reset or from pci probe callback.
 * Setup the required resources, memory map the controller
 * registers, create admin and operational reply queue pairs,
 * allocate required memory for reply pool, sense buffer pool,
 * issue IOC init request to the firmware, unmask the events and
 * issue port enable to discover SAS/SATA/NVMe devies and RAID
 * volumes.
 *
 * Return: 0 on success and non-zero on failure.
 */
int mpi3mr_init_ioc(struct mpi3mr_ioc *mrioc)
{
	int retval = 0;
	u8 retry = 0;
	struct mpi3_ioc_facts_data facts_data;
	u32 sz;

retry_init:
	retval = mpi3mr_bring_ioc_ready(mrioc);
	if (retval) {
		ioc_err(mrioc, "Failed to bring ioc ready: error %d\n",
		    retval);
		goto out_failed_noretry;
	}

	retval = mpi3mr_setup_isr(mrioc, 1);
	if (retval) {
		ioc_err(mrioc, "Failed to setup ISR error %d\n",
		    retval);
		goto out_failed_noretry;
	}

	retval = mpi3mr_issue_iocfacts(mrioc, &facts_data);
	if (retval) {
		ioc_err(mrioc, "Failed to Issue IOC Facts %d\n",
		    retval);
		goto out_failed;
	}

	mrioc->max_host_ios = mrioc->facts.max_reqs - MPI3MR_INTERNAL_CMDS_RESVD;
	mrioc->shost->max_sectors = mrioc->facts.max_data_length / 512;
	mrioc->num_io_throttle_group = mrioc->facts.max_io_throttle_group;
	atomic_set(&mrioc->pend_large_data_sz, 0);

	if (reset_devices)
		mrioc->max_host_ios = min_t(int, mrioc->max_host_ios,
		    MPI3MR_HOST_IOS_KDUMP);

	if (!(mrioc->facts.ioc_capabilities &
	    MPI3_IOCFACTS_CAPABILITY_MULTIPATH_ENABLED)) {
		mrioc->sas_transport_enabled = 1;
		mrioc->scsi_device_channel = 1;
		mrioc->shost->max_channel = 1;
		mrioc->shost->transportt = mpi3mr_transport_template;
	}

	mrioc->reply_sz = mrioc->facts.reply_sz;

	retval = mpi3mr_check_reset_dma_mask(mrioc);
	if (retval) {
		ioc_err(mrioc, "Resetting dma mask failed %d\n",
		    retval);
		goto out_failed_noretry;
	}

	mpi3mr_print_ioc_info(mrioc);

	if (!mrioc->cfg_page) {
		dprint_init(mrioc, "allocating config page buffers\n");
		mrioc->cfg_page_sz = MPI3MR_DEFAULT_CFG_PAGE_SZ;
		mrioc->cfg_page = dma_alloc_coherent(&mrioc->pdev->dev,
		    mrioc->cfg_page_sz, &mrioc->cfg_page_dma, GFP_KERNEL);
		if (!mrioc->cfg_page) {
			retval = -1;
			goto out_failed_noretry;
		}
	}

	if (!mrioc->init_cmds.reply) {
		retval = mpi3mr_alloc_reply_sense_bufs(mrioc);
		if (retval) {
			ioc_err(mrioc,
			    "%s :Failed to allocated reply sense buffers %d\n",
			    __func__, retval);
			goto out_failed_noretry;
		}
	}

	if (!mrioc->chain_sgl_list) {
		retval = mpi3mr_alloc_chain_bufs(mrioc);
		if (retval) {
			ioc_err(mrioc, "Failed to allocated chain buffers %d\n",
			    retval);
			goto out_failed_noretry;
		}
	}

	retval = mpi3mr_issue_iocinit(mrioc);
	if (retval) {
		ioc_err(mrioc, "Failed to Issue IOC Init %d\n",
		    retval);
		goto out_failed;
	}

	retval = mpi3mr_print_pkg_ver(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to get package version\n");
		goto out_failed;
	}

	retval = mpi3mr_setup_isr(mrioc, 0);
	if (retval) {
		ioc_err(mrioc, "Failed to re-setup ISR, error %d\n",
		    retval);
		goto out_failed_noretry;
	}

	retval = mpi3mr_create_op_queues(mrioc);
	if (retval) {
		ioc_err(mrioc, "Failed to create OpQueues error %d\n",
		    retval);
		goto out_failed;
	}

	if (!mrioc->pel_seqnum_virt) {
		dprint_init(mrioc, "allocating memory for pel_seqnum_virt\n");
		mrioc->pel_seqnum_sz = sizeof(struct mpi3_pel_seq);
		mrioc->pel_seqnum_virt = dma_alloc_coherent(&mrioc->pdev->dev,
		    mrioc->pel_seqnum_sz, &mrioc->pel_seqnum_dma,
		    GFP_KERNEL);
		if (!mrioc->pel_seqnum_virt) {
			retval = -ENOMEM;
			goto out_failed_noretry;
		}
	}

	if (!mrioc->throttle_groups && mrioc->num_io_throttle_group) {
		dprint_init(mrioc, "allocating memory for throttle groups\n");
		sz = sizeof(struct mpi3mr_throttle_group_info);
		mrioc->throttle_groups = kcalloc(mrioc->num_io_throttle_group, sz, GFP_KERNEL);
		if (!mrioc->throttle_groups) {
			retval = -1;
			goto out_failed_noretry;
		}
	}

	retval = mpi3mr_enable_events(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to enable events %d\n",
		    retval);
		goto out_failed;
	}

	ioc_info(mrioc, "controller initialization completed successfully\n");
	return retval;
out_failed:
	if (retry < 2) {
		retry++;
		ioc_warn(mrioc, "retrying controller initialization, retry_count:%d\n",
		    retry);
		mpi3mr_memset_buffers(mrioc);
		goto retry_init;
	}
	retval = -1;
out_failed_noretry:
	ioc_err(mrioc, "controller initialization failed\n");
	mpi3mr_issue_reset(mrioc, MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT,
	    MPI3MR_RESET_FROM_CTLR_CLEANUP);
	mrioc->unrecoverable = 1;
	return retval;
}

/**
 * mpi3mr_reinit_ioc - Re-Initialize the controller
 * @mrioc: Adapter instance reference
 * @is_resume: Called from resume or reset path
 *
 * This the controller re-initialization routine, executed from
 * the soft reset handler or resume callback. Creates
 * operational reply queue pairs, allocate required memory for
 * reply pool, sense buffer pool, issue IOC init request to the
 * firmware, unmask the events and issue port enable to discover
 * SAS/SATA/NVMe devices and RAID volumes.
 *
 * Return: 0 on success and non-zero on failure.
 */
int mpi3mr_reinit_ioc(struct mpi3mr_ioc *mrioc, u8 is_resume)
{
	int retval = 0;
	u8 retry = 0;
	struct mpi3_ioc_facts_data facts_data;
	u32 pe_timeout, ioc_status;

retry_init:
	pe_timeout =
	    (MPI3MR_PORTENABLE_TIMEOUT / MPI3MR_PORTENABLE_POLL_INTERVAL);

	dprint_reset(mrioc, "bringing up the controller to ready state\n");
	retval = mpi3mr_bring_ioc_ready(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to bring to ready state\n");
		goto out_failed_noretry;
	}

	if (is_resume) {
		dprint_reset(mrioc, "setting up single ISR\n");
		retval = mpi3mr_setup_isr(mrioc, 1);
		if (retval) {
			ioc_err(mrioc, "failed to setup ISR\n");
			goto out_failed_noretry;
		}
	} else
		mpi3mr_ioc_enable_intr(mrioc);

	dprint_reset(mrioc, "getting ioc_facts\n");
	retval = mpi3mr_issue_iocfacts(mrioc, &facts_data);
	if (retval) {
		ioc_err(mrioc, "failed to get ioc_facts\n");
		goto out_failed;
	}

	dprint_reset(mrioc, "validating ioc_facts\n");
	retval = mpi3mr_revalidate_factsdata(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to revalidate ioc_facts data\n");
		goto out_failed_noretry;
	}

	mpi3mr_print_ioc_info(mrioc);

	dprint_reset(mrioc, "sending ioc_init\n");
	retval = mpi3mr_issue_iocinit(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to send ioc_init\n");
		goto out_failed;
	}

	dprint_reset(mrioc, "getting package version\n");
	retval = mpi3mr_print_pkg_ver(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to get package version\n");
		goto out_failed;
	}

	if (is_resume) {
		dprint_reset(mrioc, "setting up multiple ISR\n");
		retval = mpi3mr_setup_isr(mrioc, 0);
		if (retval) {
			ioc_err(mrioc, "failed to re-setup ISR\n");
			goto out_failed_noretry;
		}
	}

	dprint_reset(mrioc, "creating operational queue pairs\n");
	retval = mpi3mr_create_op_queues(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to create operational queue pairs\n");
		goto out_failed;
	}

	if (!mrioc->pel_seqnum_virt) {
		dprint_reset(mrioc, "allocating memory for pel_seqnum_virt\n");
		mrioc->pel_seqnum_sz = sizeof(struct mpi3_pel_seq);
		mrioc->pel_seqnum_virt = dma_alloc_coherent(&mrioc->pdev->dev,
		    mrioc->pel_seqnum_sz, &mrioc->pel_seqnum_dma,
		    GFP_KERNEL);
		if (!mrioc->pel_seqnum_virt) {
			retval = -ENOMEM;
			goto out_failed_noretry;
		}
	}

	if (mrioc->shost->nr_hw_queues > mrioc->num_op_reply_q) {
		ioc_err(mrioc,
		    "cannot create minimum number of operational queues expected:%d created:%d\n",
		    mrioc->shost->nr_hw_queues, mrioc->num_op_reply_q);
		retval = -1;
		goto out_failed_noretry;
	}

	dprint_reset(mrioc, "enabling events\n");
	retval = mpi3mr_enable_events(mrioc);
	if (retval) {
		ioc_err(mrioc, "failed to enable events\n");
		goto out_failed;
	}

	mrioc->device_refresh_on = 1;
	mpi3mr_add_event_wait_for_device_refresh(mrioc);

	ioc_info(mrioc, "sending port enable\n");
	retval = mpi3mr_issue_port_enable(mrioc, 1);
	if (retval) {
		ioc_err(mrioc, "failed to issue port enable\n");
		goto out_failed;
	}
	do {
		ssleep(MPI3MR_PORTENABLE_POLL_INTERVAL);
		if (mrioc->init_cmds.state == MPI3MR_CMD_NOTUSED)
			break;
		if (!pci_device_is_present(mrioc->pdev))
			mrioc->unrecoverable = 1;
		if (mrioc->unrecoverable) {
			retval = -1;
			goto out_failed_noretry;
		}
		ioc_status = readl(&mrioc->sysif_regs->ioc_status);
		if ((ioc_status & MPI3_SYSIF_IOC_STATUS_RESET_HISTORY) ||
		    (ioc_status & MPI3_SYSIF_IOC_STATUS_FAULT)) {
			mpi3mr_print_fault_info(mrioc);
			mrioc->init_cmds.is_waiting = 0;
			mrioc->init_cmds.callback = NULL;
			mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
			goto out_failed;
		}
	} while (--pe_timeout);

	if (!pe_timeout) {
		ioc_err(mrioc, "port enable timed out\n");
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_PE_TIMEOUT);
		mrioc->init_cmds.is_waiting = 0;
		mrioc->init_cmds.callback = NULL;
		mrioc->init_cmds.state = MPI3MR_CMD_NOTUSED;
		goto out_failed;
	} else if (mrioc->scan_failed) {
		ioc_err(mrioc,
		    "port enable failed with status=0x%04x\n",
		    mrioc->scan_failed);
	} else
		ioc_info(mrioc, "port enable completed successfully\n");

	ioc_info(mrioc, "controller %s completed successfully\n",
	    (is_resume)?"resume":"re-initialization");
	return retval;
out_failed:
	if (retry < 2) {
		retry++;
		ioc_warn(mrioc, "retrying controller %s, retry_count:%d\n",
		    (is_resume)?"resume":"re-initialization", retry);
		mpi3mr_memset_buffers(mrioc);
		goto retry_init;
	}
	retval = -1;
out_failed_noretry:
	ioc_err(mrioc, "controller %s is failed\n",
	    (is_resume)?"resume":"re-initialization");
	mpi3mr_issue_reset(mrioc, MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT,
	    MPI3MR_RESET_FROM_CTLR_CLEANUP);
	mrioc->unrecoverable = 1;
	return retval;
}

/**
 * mpi3mr_memset_op_reply_q_buffers - memset the operational reply queue's
 *					segments
 * @mrioc: Adapter instance reference
 * @qidx: Operational reply queue index
 *
 * Return: Nothing.
 */
static void mpi3mr_memset_op_reply_q_buffers(struct mpi3mr_ioc *mrioc, u16 qidx)
{
	struct op_reply_qinfo *op_reply_q = mrioc->op_reply_qinfo + qidx;
	struct segments *segments;
	int i, size;

	if (!op_reply_q->q_segments)
		return;

	size = op_reply_q->segment_qd * mrioc->op_reply_desc_sz;
	segments = op_reply_q->q_segments;
	for (i = 0; i < op_reply_q->num_segments; i++)
		memset(segments[i].segment, 0, size);
}

/**
 * mpi3mr_memset_op_req_q_buffers - memset the operational request queue's
 *					segments
 * @mrioc: Adapter instance reference
 * @qidx: Operational request queue index
 *
 * Return: Nothing.
 */
static void mpi3mr_memset_op_req_q_buffers(struct mpi3mr_ioc *mrioc, u16 qidx)
{
	struct op_req_qinfo *op_req_q = mrioc->req_qinfo + qidx;
	struct segments *segments;
	int i, size;

	if (!op_req_q->q_segments)
		return;

	size = op_req_q->segment_qd * mrioc->facts.op_req_sz;
	segments = op_req_q->q_segments;
	for (i = 0; i < op_req_q->num_segments; i++)
		memset(segments[i].segment, 0, size);
}

/**
 * mpi3mr_memset_buffers - memset memory for a controller
 * @mrioc: Adapter instance reference
 *
 * clear all the memory allocated for a controller, typically
 * called post reset to reuse the memory allocated during the
 * controller init.
 *
 * Return: Nothing.
 */
void mpi3mr_memset_buffers(struct mpi3mr_ioc *mrioc)
{
	u16 i;
	struct mpi3mr_throttle_group_info *tg;

	mrioc->change_count = 0;
	mrioc->active_poll_qcount = 0;
	mrioc->default_qcount = 0;
	if (mrioc->admin_req_base)
		memset(mrioc->admin_req_base, 0, mrioc->admin_req_q_sz);
	if (mrioc->admin_reply_base)
		memset(mrioc->admin_reply_base, 0, mrioc->admin_reply_q_sz);
	atomic_set(&mrioc->admin_reply_q_in_use, 0);

	if (mrioc->init_cmds.reply) {
		memset(mrioc->init_cmds.reply, 0, sizeof(*mrioc->init_cmds.reply));
		memset(mrioc->bsg_cmds.reply, 0,
		    sizeof(*mrioc->bsg_cmds.reply));
		memset(mrioc->host_tm_cmds.reply, 0,
		    sizeof(*mrioc->host_tm_cmds.reply));
		memset(mrioc->pel_cmds.reply, 0,
		    sizeof(*mrioc->pel_cmds.reply));
		memset(mrioc->pel_abort_cmd.reply, 0,
		    sizeof(*mrioc->pel_abort_cmd.reply));
		memset(mrioc->transport_cmds.reply, 0,
		    sizeof(*mrioc->transport_cmds.reply));
		for (i = 0; i < MPI3MR_NUM_DEVRMCMD; i++)
			memset(mrioc->dev_rmhs_cmds[i].reply, 0,
			    sizeof(*mrioc->dev_rmhs_cmds[i].reply));
		for (i = 0; i < MPI3MR_NUM_EVTACKCMD; i++)
			memset(mrioc->evtack_cmds[i].reply, 0,
			    sizeof(*mrioc->evtack_cmds[i].reply));
		bitmap_clear(mrioc->removepend_bitmap, 0,
			     mrioc->dev_handle_bitmap_bits);
		bitmap_clear(mrioc->devrem_bitmap, 0, MPI3MR_NUM_DEVRMCMD);
		bitmap_clear(mrioc->evtack_cmds_bitmap, 0,
			     MPI3MR_NUM_EVTACKCMD);
	}

	for (i = 0; i < mrioc->num_queues; i++) {
		mrioc->op_reply_qinfo[i].qid = 0;
		mrioc->op_reply_qinfo[i].ci = 0;
		mrioc->op_reply_qinfo[i].num_replies = 0;
		mrioc->op_reply_qinfo[i].ephase = 0;
		atomic_set(&mrioc->op_reply_qinfo[i].pend_ios, 0);
		atomic_set(&mrioc->op_reply_qinfo[i].in_use, 0);
		mpi3mr_memset_op_reply_q_buffers(mrioc, i);

		mrioc->req_qinfo[i].ci = 0;
		mrioc->req_qinfo[i].pi = 0;
		mrioc->req_qinfo[i].num_requests = 0;
		mrioc->req_qinfo[i].qid = 0;
		mrioc->req_qinfo[i].reply_qid = 0;
		spin_lock_init(&mrioc->req_qinfo[i].q_lock);
		mpi3mr_memset_op_req_q_buffers(mrioc, i);
	}

	atomic_set(&mrioc->pend_large_data_sz, 0);
	if (mrioc->throttle_groups) {
		tg = mrioc->throttle_groups;
		for (i = 0; i < mrioc->num_io_throttle_group; i++, tg++) {
			tg->id = 0;
			tg->fw_qd = 0;
			tg->modified_qd = 0;
			tg->io_divert = 0;
			tg->need_qd_reduction = 0;
			tg->high = 0;
			tg->low = 0;
			tg->qd_reduction = 0;
			atomic_set(&tg->pend_large_data_sz, 0);
		}
	}
}

/**
 * mpi3mr_free_mem - Free memory allocated for a controller
 * @mrioc: Adapter instance reference
 *
 * Free all the memory allocated for a controller.
 *
 * Return: Nothing.
 */
void mpi3mr_free_mem(struct mpi3mr_ioc *mrioc)
{
	u16 i;
	struct mpi3mr_intr_info *intr_info;

	mpi3mr_free_enclosure_list(mrioc);

	if (mrioc->sense_buf_pool) {
		if (mrioc->sense_buf)
			dma_pool_free(mrioc->sense_buf_pool, mrioc->sense_buf,
			    mrioc->sense_buf_dma);
		dma_pool_destroy(mrioc->sense_buf_pool);
		mrioc->sense_buf = NULL;
		mrioc->sense_buf_pool = NULL;
	}
	if (mrioc->sense_buf_q_pool) {
		if (mrioc->sense_buf_q)
			dma_pool_free(mrioc->sense_buf_q_pool,
			    mrioc->sense_buf_q, mrioc->sense_buf_q_dma);
		dma_pool_destroy(mrioc->sense_buf_q_pool);
		mrioc->sense_buf_q = NULL;
		mrioc->sense_buf_q_pool = NULL;
	}

	if (mrioc->reply_buf_pool) {
		if (mrioc->reply_buf)
			dma_pool_free(mrioc->reply_buf_pool, mrioc->reply_buf,
			    mrioc->reply_buf_dma);
		dma_pool_destroy(mrioc->reply_buf_pool);
		mrioc->reply_buf = NULL;
		mrioc->reply_buf_pool = NULL;
	}
	if (mrioc->reply_free_q_pool) {
		if (mrioc->reply_free_q)
			dma_pool_free(mrioc->reply_free_q_pool,
			    mrioc->reply_free_q, mrioc->reply_free_q_dma);
		dma_pool_destroy(mrioc->reply_free_q_pool);
		mrioc->reply_free_q = NULL;
		mrioc->reply_free_q_pool = NULL;
	}

	for (i = 0; i < mrioc->num_op_req_q; i++)
		mpi3mr_free_op_req_q_segments(mrioc, i);

	for (i = 0; i < mrioc->num_op_reply_q; i++)
		mpi3mr_free_op_reply_q_segments(mrioc, i);

	for (i = 0; i < mrioc->intr_info_count; i++) {
		intr_info = mrioc->intr_info + i;
		intr_info->op_reply_q = NULL;
	}

	kfree(mrioc->req_qinfo);
	mrioc->req_qinfo = NULL;
	mrioc->num_op_req_q = 0;

	kfree(mrioc->op_reply_qinfo);
	mrioc->op_reply_qinfo = NULL;
	mrioc->num_op_reply_q = 0;

	kfree(mrioc->init_cmds.reply);
	mrioc->init_cmds.reply = NULL;

	kfree(mrioc->bsg_cmds.reply);
	mrioc->bsg_cmds.reply = NULL;

	kfree(mrioc->host_tm_cmds.reply);
	mrioc->host_tm_cmds.reply = NULL;

	kfree(mrioc->pel_cmds.reply);
	mrioc->pel_cmds.reply = NULL;

	kfree(mrioc->pel_abort_cmd.reply);
	mrioc->pel_abort_cmd.reply = NULL;

	for (i = 0; i < MPI3MR_NUM_EVTACKCMD; i++) {
		kfree(mrioc->evtack_cmds[i].reply);
		mrioc->evtack_cmds[i].reply = NULL;
	}

	bitmap_free(mrioc->removepend_bitmap);
	mrioc->removepend_bitmap = NULL;

	bitmap_free(mrioc->devrem_bitmap);
	mrioc->devrem_bitmap = NULL;

	bitmap_free(mrioc->evtack_cmds_bitmap);
	mrioc->evtack_cmds_bitmap = NULL;

	bitmap_free(mrioc->chain_bitmap);
	mrioc->chain_bitmap = NULL;

	kfree(mrioc->transport_cmds.reply);
	mrioc->transport_cmds.reply = NULL;

	for (i = 0; i < MPI3MR_NUM_DEVRMCMD; i++) {
		kfree(mrioc->dev_rmhs_cmds[i].reply);
		mrioc->dev_rmhs_cmds[i].reply = NULL;
	}

	if (mrioc->chain_buf_pool) {
		for (i = 0; i < mrioc->chain_buf_count; i++) {
			if (mrioc->chain_sgl_list[i].addr) {
				dma_pool_free(mrioc->chain_buf_pool,
				    mrioc->chain_sgl_list[i].addr,
				    mrioc->chain_sgl_list[i].dma_addr);
				mrioc->chain_sgl_list[i].addr = NULL;
			}
		}
		dma_pool_destroy(mrioc->chain_buf_pool);
		mrioc->chain_buf_pool = NULL;
	}

	kfree(mrioc->chain_sgl_list);
	mrioc->chain_sgl_list = NULL;

	if (mrioc->admin_reply_base) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->admin_reply_q_sz,
		    mrioc->admin_reply_base, mrioc->admin_reply_dma);
		mrioc->admin_reply_base = NULL;
	}
	if (mrioc->admin_req_base) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->admin_req_q_sz,
		    mrioc->admin_req_base, mrioc->admin_req_dma);
		mrioc->admin_req_base = NULL;
	}
	if (mrioc->cfg_page) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->cfg_page_sz,
		    mrioc->cfg_page, mrioc->cfg_page_dma);
		mrioc->cfg_page = NULL;
	}
	if (mrioc->pel_seqnum_virt) {
		dma_free_coherent(&mrioc->pdev->dev, mrioc->pel_seqnum_sz,
		    mrioc->pel_seqnum_virt, mrioc->pel_seqnum_dma);
		mrioc->pel_seqnum_virt = NULL;
	}

	kfree(mrioc->throttle_groups);
	mrioc->throttle_groups = NULL;

	kfree(mrioc->logdata_buf);
	mrioc->logdata_buf = NULL;

}

/**
 * mpi3mr_issue_ioc_shutdown - shutdown controller
 * @mrioc: Adapter instance reference
 *
 * Send shutodwn notification to the controller and wait for the
 * shutdown_timeout for it to be completed.
 *
 * Return: Nothing.
 */
static void mpi3mr_issue_ioc_shutdown(struct mpi3mr_ioc *mrioc)
{
	u32 ioc_config, ioc_status;
	u8 retval = 1;
	u32 timeout = MPI3MR_DEFAULT_SHUTDOWN_TIME * 10;

	ioc_info(mrioc, "Issuing shutdown Notification\n");
	if (mrioc->unrecoverable) {
		ioc_warn(mrioc,
		    "IOC is unrecoverable shutdown is not issued\n");
		return;
	}
	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	if ((ioc_status & MPI3_SYSIF_IOC_STATUS_SHUTDOWN_MASK)
	    == MPI3_SYSIF_IOC_STATUS_SHUTDOWN_IN_PROGRESS) {
		ioc_info(mrioc, "shutdown already in progress\n");
		return;
	}

	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);
	ioc_config |= MPI3_SYSIF_IOC_CONFIG_SHUTDOWN_NORMAL;
	ioc_config |= MPI3_SYSIF_IOC_CONFIG_DEVICE_SHUTDOWN_SEND_REQ;

	writel(ioc_config, &mrioc->sysif_regs->ioc_configuration);

	if (mrioc->facts.shutdown_timeout)
		timeout = mrioc->facts.shutdown_timeout * 10;

	do {
		ioc_status = readl(&mrioc->sysif_regs->ioc_status);
		if ((ioc_status & MPI3_SYSIF_IOC_STATUS_SHUTDOWN_MASK)
		    == MPI3_SYSIF_IOC_STATUS_SHUTDOWN_COMPLETE) {
			retval = 0;
			break;
		}
		msleep(100);
	} while (--timeout);

	ioc_status = readl(&mrioc->sysif_regs->ioc_status);
	ioc_config = readl(&mrioc->sysif_regs->ioc_configuration);

	if (retval) {
		if ((ioc_status & MPI3_SYSIF_IOC_STATUS_SHUTDOWN_MASK)
		    == MPI3_SYSIF_IOC_STATUS_SHUTDOWN_IN_PROGRESS)
			ioc_warn(mrioc,
			    "shutdown still in progress after timeout\n");
	}

	ioc_info(mrioc,
	    "Base IOC Sts/Config after %s shutdown is (0x%x)/(0x%x)\n",
	    (!retval) ? "successful" : "failed", ioc_status,
	    ioc_config);
}

/**
 * mpi3mr_cleanup_ioc - Cleanup controller
 * @mrioc: Adapter instance reference
 *
 * controller cleanup handler, Message unit reset or soft reset
 * and shutdown notification is issued to the controller.
 *
 * Return: Nothing.
 */
void mpi3mr_cleanup_ioc(struct mpi3mr_ioc *mrioc)
{
	enum mpi3mr_iocstate ioc_state;

	dprint_exit(mrioc, "cleaning up the controller\n");
	mpi3mr_ioc_disable_intr(mrioc);

	ioc_state = mpi3mr_get_iocstate(mrioc);

	if ((!mrioc->unrecoverable) && (!mrioc->reset_in_progress) &&
	    (ioc_state == MRIOC_STATE_READY)) {
		if (mpi3mr_issue_and_process_mur(mrioc,
		    MPI3MR_RESET_FROM_CTLR_CLEANUP))
			mpi3mr_issue_reset(mrioc,
			    MPI3_SYSIF_HOST_DIAG_RESET_ACTION_SOFT_RESET,
			    MPI3MR_RESET_FROM_MUR_FAILURE);
		mpi3mr_issue_ioc_shutdown(mrioc);
	}
	dprint_exit(mrioc, "controller cleanup completed\n");
}

/**
 * mpi3mr_drv_cmd_comp_reset - Flush a internal driver command
 * @mrioc: Adapter instance reference
 * @cmdptr: Internal command tracker
 *
 * Complete an internal driver commands with state indicating it
 * is completed due to reset.
 *
 * Return: Nothing.
 */
static inline void mpi3mr_drv_cmd_comp_reset(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *cmdptr)
{
	if (cmdptr->state & MPI3MR_CMD_PENDING) {
		cmdptr->state |= MPI3MR_CMD_RESET;
		cmdptr->state &= ~MPI3MR_CMD_PENDING;
		if (cmdptr->is_waiting) {
			complete(&cmdptr->done);
			cmdptr->is_waiting = 0;
		} else if (cmdptr->callback)
			cmdptr->callback(mrioc, cmdptr);
	}
}

/**
 * mpi3mr_flush_drv_cmds - Flush internaldriver commands
 * @mrioc: Adapter instance reference
 *
 * Flush all internal driver commands post reset
 *
 * Return: Nothing.
 */
void mpi3mr_flush_drv_cmds(struct mpi3mr_ioc *mrioc)
{
	struct mpi3mr_drv_cmd *cmdptr;
	u8 i;

	cmdptr = &mrioc->init_cmds;
	mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);

	cmdptr = &mrioc->cfg_cmds;
	mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);

	cmdptr = &mrioc->bsg_cmds;
	mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);
	cmdptr = &mrioc->host_tm_cmds;
	mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);

	for (i = 0; i < MPI3MR_NUM_DEVRMCMD; i++) {
		cmdptr = &mrioc->dev_rmhs_cmds[i];
		mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);
	}

	for (i = 0; i < MPI3MR_NUM_EVTACKCMD; i++) {
		cmdptr = &mrioc->evtack_cmds[i];
		mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);
	}

	cmdptr = &mrioc->pel_cmds;
	mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);

	cmdptr = &mrioc->pel_abort_cmd;
	mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);

	cmdptr = &mrioc->transport_cmds;
	mpi3mr_drv_cmd_comp_reset(mrioc, cmdptr);
}

/**
 * mpi3mr_pel_wait_post - Issue PEL Wait
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * Issue PEL Wait MPI request through admin queue and return.
 *
 * Return: Nothing.
 */
static void mpi3mr_pel_wait_post(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	struct mpi3_pel_req_action_wait pel_wait;

	mrioc->pel_abort_requested = false;

	memset(&pel_wait, 0, sizeof(pel_wait));
	drv_cmd->state = MPI3MR_CMD_PENDING;
	drv_cmd->is_waiting = 0;
	drv_cmd->callback = mpi3mr_pel_wait_complete;
	drv_cmd->ioc_status = 0;
	drv_cmd->ioc_loginfo = 0;
	pel_wait.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_PEL_WAIT);
	pel_wait.function = MPI3_FUNCTION_PERSISTENT_EVENT_LOG;
	pel_wait.action = MPI3_PEL_ACTION_WAIT;
	pel_wait.starting_sequence_number = cpu_to_le32(mrioc->pel_newest_seqnum);
	pel_wait.locale = cpu_to_le16(mrioc->pel_locale);
	pel_wait.class = cpu_to_le16(mrioc->pel_class);
	pel_wait.wait_time = MPI3_PEL_WAITTIME_INFINITE_WAIT;
	dprint_bsg_info(mrioc, "sending pel_wait seqnum(%d), class(%d), locale(0x%08x)\n",
	    mrioc->pel_newest_seqnum, mrioc->pel_class, mrioc->pel_locale);

	if (mpi3mr_admin_request_post(mrioc, &pel_wait, sizeof(pel_wait), 0)) {
		dprint_bsg_err(mrioc,
			    "Issuing PELWait: Admin post failed\n");
		drv_cmd->state = MPI3MR_CMD_NOTUSED;
		drv_cmd->callback = NULL;
		drv_cmd->retry_count = 0;
		mrioc->pel_enabled = false;
	}
}

/**
 * mpi3mr_pel_get_seqnum_post - Issue PEL Get Sequence number
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * Issue PEL get sequence number MPI request through admin queue
 * and return.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_pel_get_seqnum_post(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	struct mpi3_pel_req_action_get_sequence_numbers pel_getseq_req;
	u8 sgl_flags = MPI3MR_SGEFLAGS_SYSTEM_SIMPLE_END_OF_LIST;
	int retval = 0;

	memset(&pel_getseq_req, 0, sizeof(pel_getseq_req));
	mrioc->pel_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->pel_cmds.is_waiting = 0;
	mrioc->pel_cmds.ioc_status = 0;
	mrioc->pel_cmds.ioc_loginfo = 0;
	mrioc->pel_cmds.callback = mpi3mr_pel_get_seqnum_complete;
	pel_getseq_req.host_tag = cpu_to_le16(MPI3MR_HOSTTAG_PEL_WAIT);
	pel_getseq_req.function = MPI3_FUNCTION_PERSISTENT_EVENT_LOG;
	pel_getseq_req.action = MPI3_PEL_ACTION_GET_SEQNUM;
	mpi3mr_add_sg_single(&pel_getseq_req.sgl, sgl_flags,
	    mrioc->pel_seqnum_sz, mrioc->pel_seqnum_dma);

	retval = mpi3mr_admin_request_post(mrioc, &pel_getseq_req,
			sizeof(pel_getseq_req), 0);
	if (retval) {
		if (drv_cmd) {
			drv_cmd->state = MPI3MR_CMD_NOTUSED;
			drv_cmd->callback = NULL;
			drv_cmd->retry_count = 0;
		}
		mrioc->pel_enabled = false;
	}

	return retval;
}

/**
 * mpi3mr_pel_wait_complete - PELWait Completion callback
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * This is a callback handler for the PELWait request and
 * firmware completes a PELWait request when it is aborted or a
 * new PEL entry is available. This sends AEN to the application
 * and if the PELwait completion is not due to PELAbort then
 * this will send a request for new PEL Sequence number
 *
 * Return: Nothing.
 */
static void mpi3mr_pel_wait_complete(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	struct mpi3_pel_reply *pel_reply = NULL;
	u16 ioc_status, pe_log_status;
	bool do_retry = false;

	if (drv_cmd->state & MPI3MR_CMD_RESET)
		goto cleanup_drv_cmd;

	ioc_status = drv_cmd->ioc_status & MPI3_IOCSTATUS_STATUS_MASK;
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "%s: Failed ioc_status(0x%04x) Loginfo(0x%08x)\n",
			__func__, ioc_status, drv_cmd->ioc_loginfo);
		dprint_bsg_err(mrioc,
		    "pel_wait: failed with ioc_status(0x%04x), log_info(0x%08x)\n",
		    ioc_status, drv_cmd->ioc_loginfo);
		do_retry = true;
	}

	if (drv_cmd->state & MPI3MR_CMD_REPLY_VALID)
		pel_reply = (struct mpi3_pel_reply *)drv_cmd->reply;

	if (!pel_reply) {
		dprint_bsg_err(mrioc,
		    "pel_wait: failed due to no reply\n");
		goto out_failed;
	}

	pe_log_status = le16_to_cpu(pel_reply->pe_log_status);
	if ((pe_log_status != MPI3_PEL_STATUS_SUCCESS) &&
	    (pe_log_status != MPI3_PEL_STATUS_ABORTED)) {
		ioc_err(mrioc, "%s: Failed pe_log_status(0x%04x)\n",
			__func__, pe_log_status);
		dprint_bsg_err(mrioc,
		    "pel_wait: failed due to pel_log_status(0x%04x)\n",
		    pe_log_status);
		do_retry = true;
	}

	if (do_retry) {
		if (drv_cmd->retry_count < MPI3MR_PEL_RETRY_COUNT) {
			drv_cmd->retry_count++;
			dprint_bsg_err(mrioc, "pel_wait: retrying(%d)\n",
			    drv_cmd->retry_count);
			mpi3mr_pel_wait_post(mrioc, drv_cmd);
			return;
		}
		dprint_bsg_err(mrioc,
		    "pel_wait: failed after all retries(%d)\n",
		    drv_cmd->retry_count);
		goto out_failed;
	}
	atomic64_inc(&event_counter);
	if (!mrioc->pel_abort_requested) {
		mrioc->pel_cmds.retry_count = 0;
		mpi3mr_pel_get_seqnum_post(mrioc, &mrioc->pel_cmds);
	}

	return;
out_failed:
	mrioc->pel_enabled = false;
cleanup_drv_cmd:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	drv_cmd->callback = NULL;
	drv_cmd->retry_count = 0;
}

/**
 * mpi3mr_pel_get_seqnum_complete - PELGetSeqNum Completion callback
 * @mrioc: Adapter instance reference
 * @drv_cmd: Internal command tracker
 *
 * This is a callback handler for the PEL get sequence number
 * request and a new PEL wait request will be issued to the
 * firmware from this
 *
 * Return: Nothing.
 */
void mpi3mr_pel_get_seqnum_complete(struct mpi3mr_ioc *mrioc,
	struct mpi3mr_drv_cmd *drv_cmd)
{
	struct mpi3_pel_reply *pel_reply = NULL;
	struct mpi3_pel_seq *pel_seqnum_virt;
	u16 ioc_status;
	bool do_retry = false;

	pel_seqnum_virt = (struct mpi3_pel_seq *)mrioc->pel_seqnum_virt;

	if (drv_cmd->state & MPI3MR_CMD_RESET)
		goto cleanup_drv_cmd;

	ioc_status = drv_cmd->ioc_status & MPI3_IOCSTATUS_STATUS_MASK;
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		dprint_bsg_err(mrioc,
		    "pel_get_seqnum: failed with ioc_status(0x%04x), log_info(0x%08x)\n",
		    ioc_status, drv_cmd->ioc_loginfo);
		do_retry = true;
	}

	if (drv_cmd->state & MPI3MR_CMD_REPLY_VALID)
		pel_reply = (struct mpi3_pel_reply *)drv_cmd->reply;
	if (!pel_reply) {
		dprint_bsg_err(mrioc,
		    "pel_get_seqnum: failed due to no reply\n");
		goto out_failed;
	}

	if (le16_to_cpu(pel_reply->pe_log_status) != MPI3_PEL_STATUS_SUCCESS) {
		dprint_bsg_err(mrioc,
		    "pel_get_seqnum: failed due to pel_log_status(0x%04x)\n",
		    le16_to_cpu(pel_reply->pe_log_status));
		do_retry = true;
	}

	if (do_retry) {
		if (drv_cmd->retry_count < MPI3MR_PEL_RETRY_COUNT) {
			drv_cmd->retry_count++;
			dprint_bsg_err(mrioc,
			    "pel_get_seqnum: retrying(%d)\n",
			    drv_cmd->retry_count);
			mpi3mr_pel_get_seqnum_post(mrioc, drv_cmd);
			return;
		}

		dprint_bsg_err(mrioc,
		    "pel_get_seqnum: failed after all retries(%d)\n",
		    drv_cmd->retry_count);
		goto out_failed;
	}
	mrioc->pel_newest_seqnum = le32_to_cpu(pel_seqnum_virt->newest) + 1;
	drv_cmd->retry_count = 0;
	mpi3mr_pel_wait_post(mrioc, drv_cmd);

	return;
out_failed:
	mrioc->pel_enabled = false;
cleanup_drv_cmd:
	drv_cmd->state = MPI3MR_CMD_NOTUSED;
	drv_cmd->callback = NULL;
	drv_cmd->retry_count = 0;
}

/**
 * mpi3mr_soft_reset_handler - Reset the controller
 * @mrioc: Adapter instance reference
 * @reset_reason: Reset reason code
 * @snapdump: Flag to generate snapdump in firmware or not
 *
 * This is an handler for recovering controller by issuing soft
 * reset are diag fault reset.  This is a blocking function and
 * when one reset is executed if any other resets they will be
 * blocked. All BSG requests will be blocked during the reset. If
 * controller reset is successful then the controller will be
 * reinitalized, otherwise the controller will be marked as not
 * recoverable
 *
 * In snapdump bit is set, the controller is issued with diag
 * fault reset so that the firmware can create a snap dump and
 * post that the firmware will result in F000 fault and the
 * driver will issue soft reset to recover from that.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_soft_reset_handler(struct mpi3mr_ioc *mrioc,
	u32 reset_reason, u8 snapdump)
{
	int retval = 0, i;
	unsigned long flags;
	u32 host_diagnostic, timeout = MPI3_SYSIF_DIAG_SAVE_TIMEOUT * 10;

	/* Block the reset handler until diag save in progress*/
	dprint_reset(mrioc,
	    "soft_reset_handler: check and block on diagsave_timeout(%d)\n",
	    mrioc->diagsave_timeout);
	while (mrioc->diagsave_timeout)
		ssleep(1);
	/*
	 * Block new resets until the currently executing one is finished and
	 * return the status of the existing reset for all blocked resets
	 */
	dprint_reset(mrioc, "soft_reset_handler: acquiring reset_mutex\n");
	if (!mutex_trylock(&mrioc->reset_mutex)) {
		ioc_info(mrioc,
		    "controller reset triggered by %s is blocked due to another reset in progress\n",
		    mpi3mr_reset_rc_name(reset_reason));
		do {
			ssleep(1);
		} while (mrioc->reset_in_progress == 1);
		ioc_info(mrioc,
		    "returning previous reset result(%d) for the reset triggered by %s\n",
		    mrioc->prev_reset_result,
		    mpi3mr_reset_rc_name(reset_reason));
		return mrioc->prev_reset_result;
	}
	ioc_info(mrioc, "controller reset is triggered by %s\n",
	    mpi3mr_reset_rc_name(reset_reason));

	mrioc->device_refresh_on = 0;
	mrioc->reset_in_progress = 1;
	mrioc->stop_bsgs = 1;
	mrioc->prev_reset_result = -1;

	if ((!snapdump) && (reset_reason != MPI3MR_RESET_FROM_FAULT_WATCH) &&
	    (reset_reason != MPI3MR_RESET_FROM_FIRMWARE) &&
	    (reset_reason != MPI3MR_RESET_FROM_CIACTIV_FAULT)) {
		for (i = 0; i < MPI3_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
			mrioc->event_masks[i] = -1;

		dprint_reset(mrioc, "soft_reset_handler: masking events\n");
		mpi3mr_issue_event_notification(mrioc);
	}

	mpi3mr_wait_for_host_io(mrioc, MPI3MR_RESET_HOST_IOWAIT_TIMEOUT);

	mpi3mr_ioc_disable_intr(mrioc);

	if (snapdump) {
		mpi3mr_set_diagsave(mrioc);
		retval = mpi3mr_issue_reset(mrioc,
		    MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT, reset_reason);
		if (!retval) {
			do {
				host_diagnostic =
				    readl(&mrioc->sysif_regs->host_diagnostic);
				if (!(host_diagnostic &
				    MPI3_SYSIF_HOST_DIAG_SAVE_IN_PROGRESS))
					break;
				msleep(100);
			} while (--timeout);
		}
	}

	retval = mpi3mr_issue_reset(mrioc,
	    MPI3_SYSIF_HOST_DIAG_RESET_ACTION_SOFT_RESET, reset_reason);
	if (retval) {
		ioc_err(mrioc, "Failed to issue soft reset to the ioc\n");
		goto out;
	}
	if (mrioc->num_io_throttle_group !=
	    mrioc->facts.max_io_throttle_group) {
		ioc_err(mrioc,
		    "max io throttle group doesn't match old(%d), new(%d)\n",
		    mrioc->num_io_throttle_group,
		    mrioc->facts.max_io_throttle_group);
		retval = -EPERM;
		goto out;
	}

	mpi3mr_flush_delayed_cmd_lists(mrioc);
	mpi3mr_flush_drv_cmds(mrioc);
	bitmap_clear(mrioc->devrem_bitmap, 0, MPI3MR_NUM_DEVRMCMD);
	bitmap_clear(mrioc->removepend_bitmap, 0,
		     mrioc->dev_handle_bitmap_bits);
	bitmap_clear(mrioc->evtack_cmds_bitmap, 0, MPI3MR_NUM_EVTACKCMD);
	mpi3mr_flush_host_io(mrioc);
	mpi3mr_cleanup_fwevt_list(mrioc);
	mpi3mr_invalidate_devhandles(mrioc);
	mpi3mr_free_enclosure_list(mrioc);

	if (mrioc->prepare_for_reset) {
		mrioc->prepare_for_reset = 0;
		mrioc->prepare_for_reset_timeout_counter = 0;
	}
	mpi3mr_memset_buffers(mrioc);
	retval = mpi3mr_reinit_ioc(mrioc, 0);
	if (retval) {
		pr_err(IOCNAME "reinit after soft reset failed: reason %d\n",
		    mrioc->name, reset_reason);
		goto out;
	}
	ssleep(MPI3MR_RESET_TOPOLOGY_SETTLE_TIME);

out:
	if (!retval) {
		mrioc->diagsave_timeout = 0;
		mrioc->reset_in_progress = 0;
		mrioc->pel_abort_requested = 0;
		if (mrioc->pel_enabled) {
			mrioc->pel_cmds.retry_count = 0;
			mpi3mr_pel_wait_post(mrioc, &mrioc->pel_cmds);
		}

		mrioc->device_refresh_on = 0;

		mrioc->ts_update_counter = 0;
		spin_lock_irqsave(&mrioc->watchdog_lock, flags);
		if (mrioc->watchdog_work_q)
			queue_delayed_work(mrioc->watchdog_work_q,
			    &mrioc->watchdog_work,
			    msecs_to_jiffies(MPI3MR_WATCHDOG_INTERVAL));
		spin_unlock_irqrestore(&mrioc->watchdog_lock, flags);
		mrioc->stop_bsgs = 0;
		if (mrioc->pel_enabled)
			atomic64_inc(&event_counter);
	} else {
		mpi3mr_issue_reset(mrioc,
		    MPI3_SYSIF_HOST_DIAG_RESET_ACTION_DIAG_FAULT, reset_reason);
		mrioc->device_refresh_on = 0;
		mrioc->unrecoverable = 1;
		mrioc->reset_in_progress = 0;
		retval = -1;
		mpi3mr_flush_cmds_for_unrecovered_controller(mrioc);
	}
	mrioc->prev_reset_result = retval;
	mutex_unlock(&mrioc->reset_mutex);
	ioc_info(mrioc, "controller reset is %s\n",
	    ((retval == 0) ? "successful" : "failed"));
	return retval;
}


/**
 * mpi3mr_free_config_dma_memory - free memory for config page
 * @mrioc: Adapter instance reference
 * @mem_desc: memory descriptor structure
 *
 * Check whether the size of the buffer specified by the memory
 * descriptor is greater than the default page size if so then
 * free the memory pointed by the descriptor.
 *
 * Return: Nothing.
 */
static void mpi3mr_free_config_dma_memory(struct mpi3mr_ioc *mrioc,
	struct dma_memory_desc *mem_desc)
{
	if ((mem_desc->size > mrioc->cfg_page_sz) && mem_desc->addr) {
		dma_free_coherent(&mrioc->pdev->dev, mem_desc->size,
		    mem_desc->addr, mem_desc->dma_addr);
		mem_desc->addr = NULL;
	}
}

/**
 * mpi3mr_alloc_config_dma_memory - Alloc memory for config page
 * @mrioc: Adapter instance reference
 * @mem_desc: Memory descriptor to hold dma memory info
 *
 * This function allocates new dmaable memory or provides the
 * default config page dmaable memory based on the memory size
 * described by the descriptor.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_alloc_config_dma_memory(struct mpi3mr_ioc *mrioc,
	struct dma_memory_desc *mem_desc)
{
	if (mem_desc->size > mrioc->cfg_page_sz) {
		mem_desc->addr = dma_alloc_coherent(&mrioc->pdev->dev,
		    mem_desc->size, &mem_desc->dma_addr, GFP_KERNEL);
		if (!mem_desc->addr)
			return -ENOMEM;
	} else {
		mem_desc->addr = mrioc->cfg_page;
		mem_desc->dma_addr = mrioc->cfg_page_dma;
		memset(mem_desc->addr, 0, mrioc->cfg_page_sz);
	}
	return 0;
}

/**
 * mpi3mr_post_cfg_req - Issue config requests and wait
 * @mrioc: Adapter instance reference
 * @cfg_req: Configuration request
 * @timeout: Timeout in seconds
 * @ioc_status: Pointer to return ioc status
 *
 * A generic function for posting MPI3 configuration request to
 * the firmware. This blocks for the completion of request for
 * timeout seconds and if the request times out this function
 * faults the controller with proper reason code.
 *
 * On successful completion of the request this function returns
 * appropriate ioc status from the firmware back to the caller.
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_post_cfg_req(struct mpi3mr_ioc *mrioc,
	struct mpi3_config_request *cfg_req, int timeout, u16 *ioc_status)
{
	int retval = 0;

	mutex_lock(&mrioc->cfg_cmds.mutex);
	if (mrioc->cfg_cmds.state & MPI3MR_CMD_PENDING) {
		retval = -1;
		ioc_err(mrioc, "sending config request failed due to command in use\n");
		mutex_unlock(&mrioc->cfg_cmds.mutex);
		goto out;
	}
	mrioc->cfg_cmds.state = MPI3MR_CMD_PENDING;
	mrioc->cfg_cmds.is_waiting = 1;
	mrioc->cfg_cmds.callback = NULL;
	mrioc->cfg_cmds.ioc_status = 0;
	mrioc->cfg_cmds.ioc_loginfo = 0;

	cfg_req->host_tag = cpu_to_le16(MPI3MR_HOSTTAG_CFG_CMDS);
	cfg_req->function = MPI3_FUNCTION_CONFIG;

	init_completion(&mrioc->cfg_cmds.done);
	dprint_cfg_info(mrioc, "posting config request\n");
	if (mrioc->logging_level & MPI3_DEBUG_CFG_INFO)
		dprint_dump(cfg_req, sizeof(struct mpi3_config_request),
		    "mpi3_cfg_req");
	retval = mpi3mr_admin_request_post(mrioc, cfg_req, sizeof(*cfg_req), 1);
	if (retval) {
		ioc_err(mrioc, "posting config request failed\n");
		goto out_unlock;
	}
	wait_for_completion_timeout(&mrioc->cfg_cmds.done, (timeout * HZ));
	if (!(mrioc->cfg_cmds.state & MPI3MR_CMD_COMPLETE)) {
		mpi3mr_check_rh_fault_ioc(mrioc,
		    MPI3MR_RESET_FROM_CFG_REQ_TIMEOUT);
		ioc_err(mrioc, "config request timed out\n");
		retval = -1;
		goto out_unlock;
	}
	*ioc_status = mrioc->cfg_cmds.ioc_status & MPI3_IOCSTATUS_STATUS_MASK;
	if ((*ioc_status) != MPI3_IOCSTATUS_SUCCESS)
		dprint_cfg_err(mrioc,
		    "cfg_page request returned with ioc_status(0x%04x), log_info(0x%08x)\n",
		    *ioc_status, mrioc->cfg_cmds.ioc_loginfo);

out_unlock:
	mrioc->cfg_cmds.state = MPI3MR_CMD_NOTUSED;
	mutex_unlock(&mrioc->cfg_cmds.mutex);

out:
	return retval;
}

/**
 * mpi3mr_process_cfg_req - config page request processor
 * @mrioc: Adapter instance reference
 * @cfg_req: Configuration request
 * @cfg_hdr: Configuration page header
 * @timeout: Timeout in seconds
 * @ioc_status: Pointer to return ioc status
 * @cfg_buf: Memory pointer to copy config page or header
 * @cfg_buf_sz: Size of the memory to get config page or header
 *
 * This is handler for config page read, write and config page
 * header read operations.
 *
 * This function expects the cfg_req to be populated with page
 * type, page number, action for the header read and with page
 * address for all other operations.
 *
 * The cfg_hdr can be passed as null for reading required header
 * details for read/write pages the cfg_hdr should point valid
 * configuration page header.
 *
 * This allocates dmaable memory based on the size of the config
 * buffer and set the SGE of the cfg_req.
 *
 * For write actions, the config page data has to be passed in
 * the cfg_buf and size of the data has to be mentioned in the
 * cfg_buf_sz.
 *
 * For read/header actions, on successful completion of the
 * request with successful ioc_status the data will be copied
 * into the cfg_buf limited to a minimum of actual page size and
 * cfg_buf_sz
 *
 *
 * Return: 0 on success, non-zero on failure.
 */
static int mpi3mr_process_cfg_req(struct mpi3mr_ioc *mrioc,
	struct mpi3_config_request *cfg_req,
	struct mpi3_config_page_header *cfg_hdr, int timeout, u16 *ioc_status,
	void *cfg_buf, u32 cfg_buf_sz)
{
	struct dma_memory_desc mem_desc;
	int retval = -1;
	u8 invalid_action = 0;
	u8 sgl_flags = MPI3MR_SGEFLAGS_SYSTEM_SIMPLE_END_OF_LIST;

	memset(&mem_desc, 0, sizeof(struct dma_memory_desc));

	if (cfg_req->action == MPI3_CONFIG_ACTION_PAGE_HEADER)
		mem_desc.size = sizeof(struct mpi3_config_page_header);
	else {
		if (!cfg_hdr) {
			ioc_err(mrioc, "null config header passed for config action(%d), page_type(0x%02x), page_num(%d)\n",
			    cfg_req->action, cfg_req->page_type,
			    cfg_req->page_number);
			goto out;
		}
		switch (cfg_hdr->page_attribute & MPI3_CONFIG_PAGEATTR_MASK) {
		case MPI3_CONFIG_PAGEATTR_READ_ONLY:
			if (cfg_req->action
			    != MPI3_CONFIG_ACTION_READ_CURRENT)
				invalid_action = 1;
			break;
		case MPI3_CONFIG_PAGEATTR_CHANGEABLE:
			if ((cfg_req->action ==
			     MPI3_CONFIG_ACTION_READ_PERSISTENT) ||
			    (cfg_req->action ==
			     MPI3_CONFIG_ACTION_WRITE_PERSISTENT))
				invalid_action = 1;
			break;
		case MPI3_CONFIG_PAGEATTR_PERSISTENT:
		default:
			break;
		}
		if (invalid_action) {
			ioc_err(mrioc,
			    "config action(%d) is not allowed for page_type(0x%02x), page_num(%d) with page_attribute(0x%02x)\n",
			    cfg_req->action, cfg_req->page_type,
			    cfg_req->page_number, cfg_hdr->page_attribute);
			goto out;
		}
		mem_desc.size = le16_to_cpu(cfg_hdr->page_length) * 4;
		cfg_req->page_length = cfg_hdr->page_length;
		cfg_req->page_version = cfg_hdr->page_version;
	}
	if (mpi3mr_alloc_config_dma_memory(mrioc, &mem_desc))
		goto out;

	mpi3mr_add_sg_single(&cfg_req->sgl, sgl_flags, mem_desc.size,
	    mem_desc.dma_addr);

	if ((cfg_req->action == MPI3_CONFIG_ACTION_WRITE_PERSISTENT) ||
	    (cfg_req->action == MPI3_CONFIG_ACTION_WRITE_CURRENT)) {
		memcpy(mem_desc.addr, cfg_buf, min_t(u16, mem_desc.size,
		    cfg_buf_sz));
		dprint_cfg_info(mrioc, "config buffer to be written\n");
		if (mrioc->logging_level & MPI3_DEBUG_CFG_INFO)
			dprint_dump(mem_desc.addr, mem_desc.size, "cfg_buf");
	}

	if (mpi3mr_post_cfg_req(mrioc, cfg_req, timeout, ioc_status))
		goto out;

	retval = 0;
	if ((*ioc_status == MPI3_IOCSTATUS_SUCCESS) &&
	    (cfg_req->action != MPI3_CONFIG_ACTION_WRITE_PERSISTENT) &&
	    (cfg_req->action != MPI3_CONFIG_ACTION_WRITE_CURRENT)) {
		memcpy(cfg_buf, mem_desc.addr, min_t(u16, mem_desc.size,
		    cfg_buf_sz));
		dprint_cfg_info(mrioc, "config buffer read\n");
		if (mrioc->logging_level & MPI3_DEBUG_CFG_INFO)
			dprint_dump(mem_desc.addr, mem_desc.size, "cfg_buf");
	}

out:
	mpi3mr_free_config_dma_memory(mrioc, &mem_desc);
	return retval;
}

/**
 * mpi3mr_cfg_get_dev_pg0 - Read current device page0
 * @mrioc: Adapter instance reference
 * @ioc_status: Pointer to return ioc status
 * @dev_pg0: Pointer to return device page 0
 * @pg_sz: Size of the memory allocated to the page pointer
 * @form: The form to be used for addressing the page
 * @form_spec: Form specific information like device handle
 *
 * This is handler for config page read for a specific device
 * page0. The ioc_status has the controller returned ioc_status.
 * This routine doesn't check ioc_status to decide whether the
 * page read is success or not and it is the callers
 * responsibility.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_dev_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_device_page0 *dev_pg0, u16 pg_sz, u32 form, u32 form_spec)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u32 page_address;

	memset(dev_pg0, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_DEVICE;
	cfg_req.page_number = 0;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "device page0 header read failed\n");
		goto out_failed;
	}
	if (*ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "device page0 header read failed with ioc_status(0x%04x)\n",
		    *ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;
	page_address = ((form & MPI3_DEVICE_PGAD_FORM_MASK) |
	    (form_spec & MPI3_DEVICE_PGAD_HANDLE_MASK));
	cfg_req.page_address = cpu_to_le32(page_address);
	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, dev_pg0, pg_sz)) {
		ioc_err(mrioc, "device page0 read failed\n");
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}


/**
 * mpi3mr_cfg_get_sas_phy_pg0 - Read current SAS Phy page0
 * @mrioc: Adapter instance reference
 * @ioc_status: Pointer to return ioc status
 * @phy_pg0: Pointer to return SAS Phy page 0
 * @pg_sz: Size of the memory allocated to the page pointer
 * @form: The form to be used for addressing the page
 * @form_spec: Form specific information like phy number
 *
 * This is handler for config page read for a specific SAS Phy
 * page0. The ioc_status has the controller returned ioc_status.
 * This routine doesn't check ioc_status to decide whether the
 * page read is success or not and it is the callers
 * responsibility.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_sas_phy_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_phy_page0 *phy_pg0, u16 pg_sz, u32 form,
	u32 form_spec)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u32 page_address;

	memset(phy_pg0, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_SAS_PHY;
	cfg_req.page_number = 0;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "sas phy page0 header read failed\n");
		goto out_failed;
	}
	if (*ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas phy page0 header read failed with ioc_status(0x%04x)\n",
		    *ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;
	page_address = ((form & MPI3_SAS_PHY_PGAD_FORM_MASK) |
	    (form_spec & MPI3_SAS_PHY_PGAD_PHY_NUMBER_MASK));
	cfg_req.page_address = cpu_to_le32(page_address);
	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, phy_pg0, pg_sz)) {
		ioc_err(mrioc, "sas phy page0 read failed\n");
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}

/**
 * mpi3mr_cfg_get_sas_phy_pg1 - Read current SAS Phy page1
 * @mrioc: Adapter instance reference
 * @ioc_status: Pointer to return ioc status
 * @phy_pg1: Pointer to return SAS Phy page 1
 * @pg_sz: Size of the memory allocated to the page pointer
 * @form: The form to be used for addressing the page
 * @form_spec: Form specific information like phy number
 *
 * This is handler for config page read for a specific SAS Phy
 * page1. The ioc_status has the controller returned ioc_status.
 * This routine doesn't check ioc_status to decide whether the
 * page read is success or not and it is the callers
 * responsibility.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_sas_phy_pg1(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_phy_page1 *phy_pg1, u16 pg_sz, u32 form,
	u32 form_spec)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u32 page_address;

	memset(phy_pg1, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_SAS_PHY;
	cfg_req.page_number = 1;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "sas phy page1 header read failed\n");
		goto out_failed;
	}
	if (*ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas phy page1 header read failed with ioc_status(0x%04x)\n",
		    *ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;
	page_address = ((form & MPI3_SAS_PHY_PGAD_FORM_MASK) |
	    (form_spec & MPI3_SAS_PHY_PGAD_PHY_NUMBER_MASK));
	cfg_req.page_address = cpu_to_le32(page_address);
	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, phy_pg1, pg_sz)) {
		ioc_err(mrioc, "sas phy page1 read failed\n");
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}


/**
 * mpi3mr_cfg_get_sas_exp_pg0 - Read current SAS Expander page0
 * @mrioc: Adapter instance reference
 * @ioc_status: Pointer to return ioc status
 * @exp_pg0: Pointer to return SAS Expander page 0
 * @pg_sz: Size of the memory allocated to the page pointer
 * @form: The form to be used for addressing the page
 * @form_spec: Form specific information like device handle
 *
 * This is handler for config page read for a specific SAS
 * Expander page0. The ioc_status has the controller returned
 * ioc_status. This routine doesn't check ioc_status to decide
 * whether the page read is success or not and it is the callers
 * responsibility.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_sas_exp_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_expander_page0 *exp_pg0, u16 pg_sz, u32 form,
	u32 form_spec)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u32 page_address;

	memset(exp_pg0, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_SAS_EXPANDER;
	cfg_req.page_number = 0;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "expander page0 header read failed\n");
		goto out_failed;
	}
	if (*ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "expander page0 header read failed with ioc_status(0x%04x)\n",
		    *ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;
	page_address = ((form & MPI3_SAS_EXPAND_PGAD_FORM_MASK) |
	    (form_spec & (MPI3_SAS_EXPAND_PGAD_PHYNUM_MASK |
	    MPI3_SAS_EXPAND_PGAD_HANDLE_MASK)));
	cfg_req.page_address = cpu_to_le32(page_address);
	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, exp_pg0, pg_sz)) {
		ioc_err(mrioc, "expander page0 read failed\n");
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}

/**
 * mpi3mr_cfg_get_sas_exp_pg1 - Read current SAS Expander page1
 * @mrioc: Adapter instance reference
 * @ioc_status: Pointer to return ioc status
 * @exp_pg1: Pointer to return SAS Expander page 1
 * @pg_sz: Size of the memory allocated to the page pointer
 * @form: The form to be used for addressing the page
 * @form_spec: Form specific information like phy number
 *
 * This is handler for config page read for a specific SAS
 * Expander page1. The ioc_status has the controller returned
 * ioc_status. This routine doesn't check ioc_status to decide
 * whether the page read is success or not and it is the callers
 * responsibility.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_sas_exp_pg1(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_sas_expander_page1 *exp_pg1, u16 pg_sz, u32 form,
	u32 form_spec)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u32 page_address;

	memset(exp_pg1, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_SAS_EXPANDER;
	cfg_req.page_number = 1;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "expander page1 header read failed\n");
		goto out_failed;
	}
	if (*ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "expander page1 header read failed with ioc_status(0x%04x)\n",
		    *ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;
	page_address = ((form & MPI3_SAS_EXPAND_PGAD_FORM_MASK) |
	    (form_spec & (MPI3_SAS_EXPAND_PGAD_PHYNUM_MASK |
	    MPI3_SAS_EXPAND_PGAD_HANDLE_MASK)));
	cfg_req.page_address = cpu_to_le32(page_address);
	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, exp_pg1, pg_sz)) {
		ioc_err(mrioc, "expander page1 read failed\n");
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}

/**
 * mpi3mr_cfg_get_enclosure_pg0 - Read current Enclosure page0
 * @mrioc: Adapter instance reference
 * @ioc_status: Pointer to return ioc status
 * @encl_pg0: Pointer to return Enclosure page 0
 * @pg_sz: Size of the memory allocated to the page pointer
 * @form: The form to be used for addressing the page
 * @form_spec: Form specific information like device handle
 *
 * This is handler for config page read for a specific Enclosure
 * page0. The ioc_status has the controller returned ioc_status.
 * This routine doesn't check ioc_status to decide whether the
 * page read is success or not and it is the callers
 * responsibility.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_enclosure_pg0(struct mpi3mr_ioc *mrioc, u16 *ioc_status,
	struct mpi3_enclosure_page0 *encl_pg0, u16 pg_sz, u32 form,
	u32 form_spec)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u32 page_address;

	memset(encl_pg0, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_ENCLOSURE;
	cfg_req.page_number = 0;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "enclosure page0 header read failed\n");
		goto out_failed;
	}
	if (*ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "enclosure page0 header read failed with ioc_status(0x%04x)\n",
		    *ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;
	page_address = ((form & MPI3_ENCLOS_PGAD_FORM_MASK) |
	    (form_spec & MPI3_ENCLOS_PGAD_HANDLE_MASK));
	cfg_req.page_address = cpu_to_le32(page_address);
	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, ioc_status, encl_pg0, pg_sz)) {
		ioc_err(mrioc, "enclosure page0 read failed\n");
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}


/**
 * mpi3mr_cfg_get_sas_io_unit_pg0 - Read current SASIOUnit page0
 * @mrioc: Adapter instance reference
 * @sas_io_unit_pg0: Pointer to return SAS IO Unit page 0
 * @pg_sz: Size of the memory allocated to the page pointer
 *
 * This is handler for config page read for the SAS IO Unit
 * page0. This routine checks ioc_status to decide whether the
 * page read is success or not.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_sas_io_unit_pg0(struct mpi3mr_ioc *mrioc,
	struct mpi3_sas_io_unit_page0 *sas_io_unit_pg0, u16 pg_sz)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u16 ioc_status = 0;

	memset(sas_io_unit_pg0, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_SAS_IO_UNIT;
	cfg_req.page_number = 0;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "sas io unit page0 header read failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas io unit page0 header read failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, sas_io_unit_pg0, pg_sz)) {
		ioc_err(mrioc, "sas io unit page0 read failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas io unit page0 read failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}

/**
 * mpi3mr_cfg_get_sas_io_unit_pg1 - Read current SASIOUnit page1
 * @mrioc: Adapter instance reference
 * @sas_io_unit_pg1: Pointer to return SAS IO Unit page 1
 * @pg_sz: Size of the memory allocated to the page pointer
 *
 * This is handler for config page read for the SAS IO Unit
 * page1. This routine checks ioc_status to decide whether the
 * page read is success or not.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_sas_io_unit_pg1(struct mpi3mr_ioc *mrioc,
	struct mpi3_sas_io_unit_page1 *sas_io_unit_pg1, u16 pg_sz)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u16 ioc_status = 0;

	memset(sas_io_unit_pg1, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_SAS_IO_UNIT;
	cfg_req.page_number = 1;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "sas io unit page1 header read failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas io unit page1 header read failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, sas_io_unit_pg1, pg_sz)) {
		ioc_err(mrioc, "sas io unit page1 read failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas io unit page1 read failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}

/**
 * mpi3mr_cfg_set_sas_io_unit_pg1 - Write SASIOUnit page1
 * @mrioc: Adapter instance reference
 * @sas_io_unit_pg1: Pointer to the SAS IO Unit page 1 to write
 * @pg_sz: Size of the memory allocated to the page pointer
 *
 * This is handler for config page write for the SAS IO Unit
 * page1. This routine checks ioc_status to decide whether the
 * page read is success or not. This will modify both current
 * and persistent page.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_set_sas_io_unit_pg1(struct mpi3mr_ioc *mrioc,
	struct mpi3_sas_io_unit_page1 *sas_io_unit_pg1, u16 pg_sz)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u16 ioc_status = 0;

	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_SAS_IO_UNIT;
	cfg_req.page_number = 1;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "sas io unit page1 header read failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas io unit page1 header read failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_WRITE_CURRENT;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, sas_io_unit_pg1, pg_sz)) {
		ioc_err(mrioc, "sas io unit page1 write current failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas io unit page1 write current failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}

	cfg_req.action = MPI3_CONFIG_ACTION_WRITE_PERSISTENT;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, sas_io_unit_pg1, pg_sz)) {
		ioc_err(mrioc, "sas io unit page1 write persistent failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "sas io unit page1 write persistent failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}

/**
 * mpi3mr_cfg_get_driver_pg1 - Read current Driver page1
 * @mrioc: Adapter instance reference
 * @driver_pg1: Pointer to return Driver page 1
 * @pg_sz: Size of the memory allocated to the page pointer
 *
 * This is handler for config page read for the Driver page1.
 * This routine checks ioc_status to decide whether the page
 * read is success or not.
 *
 * Return: 0 on success, non-zero on failure.
 */
int mpi3mr_cfg_get_driver_pg1(struct mpi3mr_ioc *mrioc,
	struct mpi3_driver_page1 *driver_pg1, u16 pg_sz)
{
	struct mpi3_config_page_header cfg_hdr;
	struct mpi3_config_request cfg_req;
	u16 ioc_status = 0;

	memset(driver_pg1, 0, pg_sz);
	memset(&cfg_hdr, 0, sizeof(cfg_hdr));
	memset(&cfg_req, 0, sizeof(cfg_req));

	cfg_req.function = MPI3_FUNCTION_CONFIG;
	cfg_req.action = MPI3_CONFIG_ACTION_PAGE_HEADER;
	cfg_req.page_type = MPI3_CONFIG_PAGETYPE_DRIVER;
	cfg_req.page_number = 1;
	cfg_req.page_address = 0;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, NULL,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, &cfg_hdr, sizeof(cfg_hdr))) {
		ioc_err(mrioc, "driver page1 header read failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "driver page1 header read failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	cfg_req.action = MPI3_CONFIG_ACTION_READ_CURRENT;

	if (mpi3mr_process_cfg_req(mrioc, &cfg_req, &cfg_hdr,
	    MPI3MR_INTADMCMD_TIMEOUT, &ioc_status, driver_pg1, pg_sz)) {
		ioc_err(mrioc, "driver page1 read failed\n");
		goto out_failed;
	}
	if (ioc_status != MPI3_IOCSTATUS_SUCCESS) {
		ioc_err(mrioc, "driver page1 read failed with ioc_status(0x%04x)\n",
		    ioc_status);
		goto out_failed;
	}
	return 0;
out_failed:
	return -1;
}
