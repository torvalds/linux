/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"

#include <scsi/scsi_tcq.h>

/**
 * qla4xxx_get_req_pkt - returns a valid entry in request queue.
 * @ha: Pointer to host adapter structure.
 * @queue_entry: Pointer to pointer to queue entry structure
 *
 * This routine performs the following tasks:
 *	- returns the current request_in pointer (if queue not full)
 *	- advances the request_in pointer
 *	- checks for queue full
 **/
int qla4xxx_get_req_pkt(struct scsi_qla_host *ha,
			struct queue_entry **queue_entry)
{
	uint16_t request_in;
	uint8_t status = QLA_SUCCESS;

	*queue_entry = ha->request_ptr;

	/* get the latest request_in and request_out index */
	request_in = ha->request_in;
	ha->request_out = (uint16_t) le32_to_cpu(ha->shadow_regs->req_q_out);

	/* Advance request queue pointer and check for queue full */
	if (request_in == (REQUEST_QUEUE_DEPTH - 1)) {
		request_in = 0;
		ha->request_ptr = ha->request_ring;
	} else {
		request_in++;
		ha->request_ptr++;
	}

	/* request queue is full, try again later */
	if ((ha->iocb_cnt + 1) >= ha->iocb_hiwat) {
		/* restore request pointer */
		ha->request_ptr = *queue_entry;
		status = QLA_ERROR;
	} else {
		ha->request_in = request_in;
		memset(*queue_entry, 0, sizeof(**queue_entry));
	}

	return status;
}

/**
 * qla4xxx_send_marker_iocb - issues marker iocb to HBA
 * @ha: Pointer to host adapter structure.
 * @ddb_entry: Pointer to device database entry
 * @lun: SCSI LUN
 * @marker_type: marker identifier
 *
 * This routine issues a marker IOCB.
 **/
int qla4xxx_send_marker_iocb(struct scsi_qla_host *ha,
			     struct ddb_entry *ddb_entry, int lun)
{
	struct marker_entry *marker_entry;
	unsigned long flags = 0;
	uint8_t status = QLA_SUCCESS;

	/* Acquire hardware specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Get pointer to the queue entry for the marker */
	if (qla4xxx_get_req_pkt(ha, (struct queue_entry **) &marker_entry) !=
	    QLA_SUCCESS) {
		status = QLA_ERROR;
		goto exit_send_marker;
	}

	/* Put the marker in the request queue */
	marker_entry->hdr.entryType = ET_MARKER;
	marker_entry->hdr.entryCount = 1;
	marker_entry->target = cpu_to_le16(ddb_entry->fw_ddb_index);
	marker_entry->modifier = cpu_to_le16(MM_LUN_RESET);
	int_to_scsilun(lun, &marker_entry->lun);
	wmb();

	/* Tell ISP it's got a new I/O request */
	writel(ha->request_in, &ha->reg->req_q_in);
	readl(&ha->reg->req_q_in);

exit_send_marker:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return status;
}

struct continuation_t1_entry* qla4xxx_alloc_cont_entry(
	struct scsi_qla_host *ha)
{
	struct continuation_t1_entry *cont_entry;

	cont_entry = (struct continuation_t1_entry *)ha->request_ptr;

	/* Advance request queue pointer */
	if (ha->request_in == (REQUEST_QUEUE_DEPTH - 1)) {
		ha->request_in = 0;
		ha->request_ptr = ha->request_ring;
	} else {
		ha->request_in++;
		ha->request_ptr++;
	}

	/* Load packet defaults */
	cont_entry->hdr.entryType = ET_CONTINUE;
	cont_entry->hdr.entryCount = 1;
	cont_entry->hdr.systemDefined = (uint8_t) cpu_to_le16(ha->request_in);

	return cont_entry;
}

uint16_t qla4xxx_calc_request_entries(uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > COMMAND_SEG) {
		iocbs += (dsds - COMMAND_SEG) / CONTINUE_SEG;
		if ((dsds - COMMAND_SEG) % CONTINUE_SEG)
			iocbs++;
	}
	return iocbs;
}

void qla4xxx_build_scsi_iocbs(struct srb *srb,
			      struct command_t3_entry *cmd_entry,
			      uint16_t tot_dsds)
{
	struct scsi_qla_host *ha;
	uint16_t avail_dsds;
	struct data_seg_a64 *cur_dsd;
	struct scsi_cmnd *cmd;

	cmd = srb->cmd;
	ha = srb->ha;

	if (cmd->request_bufflen == 0 || cmd->sc_data_direction == DMA_NONE) {
		/* No data being transferred */
		cmd_entry->ttlByteCnt = __constant_cpu_to_le32(0);
		return;
	}

	avail_dsds = COMMAND_SEG;
	cur_dsd = (struct data_seg_a64 *) & (cmd_entry->dataseg[0]);

	/* Load data segments */
	if (cmd->use_sg) {
		struct scatterlist *cur_seg;
		struct scatterlist *end_seg;

		cur_seg = (struct scatterlist *)cmd->request_buffer;
		end_seg = cur_seg + tot_dsds;
		while (cur_seg < end_seg) {
			dma_addr_t sle_dma;

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				struct continuation_t1_entry *cont_entry;

				cont_entry = qla4xxx_alloc_cont_entry(ha);
				cur_dsd =
					(struct data_seg_a64 *)
					&cont_entry->dataseg[0];
				avail_dsds = CONTINUE_SEG;
			}

			sle_dma = sg_dma_address(cur_seg);
			cur_dsd->base.addrLow = cpu_to_le32(LSDW(sle_dma));
			cur_dsd->base.addrHigh = cpu_to_le32(MSDW(sle_dma));
			cur_dsd->count = cpu_to_le32(sg_dma_len(cur_seg));
			avail_dsds--;

			cur_dsd++;
			cur_seg++;
		}
	} else {
		cur_dsd->base.addrLow = cpu_to_le32(LSDW(srb->dma_handle));
		cur_dsd->base.addrHigh = cpu_to_le32(MSDW(srb->dma_handle));
		cur_dsd->count = cpu_to_le32(cmd->request_bufflen);
	}
}

/**
 * qla4xxx_send_command_to_isp - issues command to HBA
 * @ha: pointer to host adapter structure.
 * @srb: pointer to SCSI Request Block to be sent to ISP
 *
 * This routine is called by qla4xxx_queuecommand to build an ISP
 * command and pass it to the ISP for execution.
 **/
int qla4xxx_send_command_to_isp(struct scsi_qla_host *ha, struct srb * srb)
{
	struct scsi_cmnd *cmd = srb->cmd;
	struct ddb_entry *ddb_entry;
	struct command_t3_entry *cmd_entry;
	struct scatterlist *sg = NULL;

	uint16_t tot_dsds;
	uint16_t req_cnt;

	unsigned long flags;
	uint16_t cnt;
	uint32_t index;
	char tag[2];

	/* Get real lun and adapter */
	ddb_entry = srb->ddb;

	/* Send marker(s) if needed. */
	if (ha->marker_needed == 1) {
		if (qla4xxx_send_marker_iocb(ha, ddb_entry,
					     cmd->device->lun) != QLA_SUCCESS)
			return QLA_ERROR;

		ha->marker_needed = 0;
	}
	tot_dsds = 0;

	/* Acquire hardware specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	index = (uint32_t)cmd->request->tag;

	/* Calculate the number of request entries needed. */
	if (cmd->use_sg) {
		sg = (struct scatterlist *)cmd->request_buffer;
		tot_dsds = pci_map_sg(ha->pdev, sg, cmd->use_sg,
				      cmd->sc_data_direction);
		if (tot_dsds == 0)
			goto queuing_error;
	} else if (cmd->request_bufflen) {
		dma_addr_t	req_dma;

		req_dma = pci_map_single(ha->pdev, cmd->request_buffer,
					 cmd->request_bufflen,
					 cmd->sc_data_direction);
		if (dma_mapping_error(req_dma))
			goto queuing_error;

		srb->dma_handle = req_dma;
		tot_dsds = 1;
	}
	req_cnt = qla4xxx_calc_request_entries(tot_dsds);

	if (ha->req_q_count < (req_cnt + 2)) {
		cnt = (uint16_t) le32_to_cpu(ha->shadow_regs->req_q_out);
		if (ha->request_in < cnt)
			ha->req_q_count = cnt - ha->request_in;
		else
			ha->req_q_count = REQUEST_QUEUE_DEPTH -
				(ha->request_in - cnt);
	}

	if (ha->req_q_count < (req_cnt + 2))
		goto queuing_error;

	/* total iocbs active */
	if ((ha->iocb_cnt + req_cnt) >= REQUEST_QUEUE_DEPTH)
		goto queuing_error;

	/* Build command packet */
	cmd_entry = (struct command_t3_entry *) ha->request_ptr;
	memset(cmd_entry, 0, sizeof(struct command_t3_entry));
	cmd_entry->hdr.entryType = ET_COMMAND;
	cmd_entry->handle = cpu_to_le32(index);
	cmd_entry->target = cpu_to_le16(ddb_entry->fw_ddb_index);
	cmd_entry->connection_id = cpu_to_le16(ddb_entry->connection_id);

	int_to_scsilun(cmd->device->lun, &cmd_entry->lun);
	cmd_entry->cmdSeqNum = cpu_to_le32(ddb_entry->CmdSn);
	cmd_entry->ttlByteCnt = cpu_to_le32(cmd->request_bufflen);
	memcpy(cmd_entry->cdb, cmd->cmnd, cmd->cmd_len);
	cmd_entry->dataSegCnt = cpu_to_le16(tot_dsds);
	cmd_entry->hdr.entryCount = req_cnt;

	/* Set data transfer direction control flags
	 * NOTE: Look at data_direction bits iff there is data to be
	 *	 transferred, as the data direction bit is sometimed filled
	 *	 in when there is no data to be transferred */
	cmd_entry->control_flags = CF_NO_DATA;
	if (cmd->request_bufflen) {
		if (cmd->sc_data_direction == DMA_TO_DEVICE)
			cmd_entry->control_flags = CF_WRITE;
		else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
			cmd_entry->control_flags = CF_READ;
	}

	/* Set tagged queueing control flags */
	cmd_entry->control_flags |= CF_SIMPLE_TAG;
	if (scsi_populate_tag_msg(cmd, tag))
		switch (tag[0]) {
		case MSG_HEAD_TAG:
			cmd_entry->control_flags |= CF_HEAD_TAG;
			break;
		case MSG_ORDERED_TAG:
			cmd_entry->control_flags |= CF_ORDERED_TAG;
			break;
		}


	/* Advance request queue pointer */
	ha->request_in++;
	if (ha->request_in == REQUEST_QUEUE_DEPTH) {
		ha->request_in = 0;
		ha->request_ptr = ha->request_ring;
	} else
		ha->request_ptr++;


	qla4xxx_build_scsi_iocbs(srb, cmd_entry, tot_dsds);
	wmb();

	/*
	 * Check to see if adapter is online before placing request on
	 * request queue.  If a reset occurs and a request is in the queue,
	 * the firmware will still attempt to process the request, retrieving
	 * garbage for pointers.
	 */
	if (!test_bit(AF_ONLINE, &ha->flags)) {
		DEBUG2(printk("scsi%ld: %s: Adapter OFFLINE! "
			      "Do not issue command.\n",
			      ha->host_no, __func__));
		goto queuing_error;
	}

	srb->cmd->host_scribble = (unsigned char *)srb;

	/* update counters */
	srb->state = SRB_ACTIVE_STATE;
	srb->flags |= SRB_DMA_VALID;

	/* Track IOCB used */
	ha->iocb_cnt += req_cnt;
	srb->iocb_cnt = req_cnt;
	ha->req_q_count -= req_cnt;

	/* Debug print statements */
	writel(ha->request_in, &ha->reg->req_q_in);
	readl(&ha->reg->req_q_in);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_SUCCESS;

queuing_error:

	if (cmd->use_sg && tot_dsds) {
		sg = (struct scatterlist *) cmd->request_buffer;
		pci_unmap_sg(ha->pdev, sg, cmd->use_sg,
			     cmd->sc_data_direction);
	} else if (tot_dsds)
		pci_unmap_single(ha->pdev, srb->dma_handle,
				 cmd->request_bufflen, cmd->sc_data_direction);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_ERROR;
}

