/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2012 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"

/**
 * qla4xxx_copy_sense - copy sense data	into cmd sense buffer
 * @ha: Pointer to host adapter structure.
 * @sts_entry: Pointer to status entry structure.
 * @srb: Pointer to srb structure.
 **/
static void qla4xxx_copy_sense(struct scsi_qla_host *ha,
                               struct status_entry *sts_entry,
                               struct srb *srb)
{
	struct scsi_cmnd *cmd = srb->cmd;
	uint16_t sense_len;

	memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
	sense_len = le16_to_cpu(sts_entry->senseDataByteCnt);
	if (sense_len == 0) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "scsi%ld:%d:%d:%d: %s:"
				  " sense len 0\n", ha->host_no,
				  cmd->device->channel, cmd->device->id,
				  cmd->device->lun, __func__));
		ha->status_srb = NULL;
		return;
	}
	/* Save total available sense length,
	 * not to exceed cmd's sense buffer size */
	sense_len = min_t(uint16_t, sense_len, SCSI_SENSE_BUFFERSIZE);
	srb->req_sense_ptr = cmd->sense_buffer;
	srb->req_sense_len = sense_len;

	/* Copy sense from sts_entry pkt */
	sense_len = min_t(uint16_t, sense_len, IOCB_MAX_SENSEDATA_LEN);
	memcpy(cmd->sense_buffer, sts_entry->senseData, sense_len);

	DEBUG2(printk(KERN_INFO "scsi%ld:%d:%d:%d: %s: sense key = %x, "
		"ASL= %02x, ASC/ASCQ = %02x/%02x\n", ha->host_no,
		cmd->device->channel, cmd->device->id,
		cmd->device->lun, __func__,
		sts_entry->senseData[2] & 0x0f,
		sts_entry->senseData[7],
		sts_entry->senseData[12],
		sts_entry->senseData[13]));

	DEBUG5(qla4xxx_dump_buffer(cmd->sense_buffer, sense_len));
	srb->flags |= SRB_GOT_SENSE;

	/* Update srb, in case a sts_cont pkt follows */
	srb->req_sense_ptr += sense_len;
	srb->req_sense_len -= sense_len;
	if (srb->req_sense_len != 0)
		ha->status_srb = srb;
	else
		ha->status_srb = NULL;
}

/**
 * qla4xxx_status_cont_entry - Process a Status Continuations entry.
 * @ha: SCSI driver HA context
 * @sts_cont: Entry pointer
 *
 * Extended sense data.
 */
static void
qla4xxx_status_cont_entry(struct scsi_qla_host *ha,
			  struct status_cont_entry *sts_cont)
{
	struct srb *srb = ha->status_srb;
	struct scsi_cmnd *cmd;
	uint16_t sense_len;

	if (srb == NULL)
		return;

	cmd = srb->cmd;
	if (cmd == NULL) {
		DEBUG2(printk(KERN_INFO "scsi%ld: %s: Cmd already returned "
			"back to OS srb=%p srb->state:%d\n", ha->host_no,
			__func__, srb, srb->state));
		ha->status_srb = NULL;
		return;
	}

	/* Copy sense data. */
	sense_len = min_t(uint16_t, srb->req_sense_len,
			  IOCB_MAX_EXT_SENSEDATA_LEN);
	memcpy(srb->req_sense_ptr, sts_cont->ext_sense_data, sense_len);
	DEBUG5(qla4xxx_dump_buffer(srb->req_sense_ptr, sense_len));

	srb->req_sense_ptr += sense_len;
	srb->req_sense_len -= sense_len;

	/* Place command on done queue. */
	if (srb->req_sense_len == 0) {
		kref_put(&srb->srb_ref, qla4xxx_srb_compl);
		ha->status_srb = NULL;
	}
}

/**
 * qla4xxx_status_entry - processes status IOCBs
 * @ha: Pointer to host adapter structure.
 * @sts_entry: Pointer to status entry structure.
 **/
static void qla4xxx_status_entry(struct scsi_qla_host *ha,
				 struct status_entry *sts_entry)
{
	uint8_t scsi_status;
	struct scsi_cmnd *cmd;
	struct srb *srb;
	struct ddb_entry *ddb_entry;
	uint32_t residual;

	srb = qla4xxx_del_from_active_array(ha, le32_to_cpu(sts_entry->handle));
	if (!srb) {
		ql4_printk(KERN_WARNING, ha, "%s invalid status entry: "
			   "handle=0x%0x, srb=%p\n", __func__,
			   sts_entry->handle, srb);
		if (is_qla80XX(ha))
			set_bit(DPC_RESET_HA_FW_CONTEXT, &ha->dpc_flags);
		else
			set_bit(DPC_RESET_HA, &ha->dpc_flags);
		return;
	}

	cmd = srb->cmd;
	if (cmd == NULL) {
		DEBUG2(printk("scsi%ld: %s: Command already returned back to "
			      "OS pkt->handle=%d srb=%p srb->state:%d\n",
			      ha->host_no, __func__, sts_entry->handle,
			      srb, srb->state));
		ql4_printk(KERN_WARNING, ha, "Command is NULL:"
		    " already returned to OS (srb=%p)\n", srb);
		return;
	}

	ddb_entry = srb->ddb;
	if (ddb_entry == NULL) {
		cmd->result = DID_NO_CONNECT << 16;
		goto status_entry_exit;
	}

	residual = le32_to_cpu(sts_entry->residualByteCnt);

	/* Translate ISP error to a Linux SCSI error. */
	scsi_status = sts_entry->scsiStatus;
	switch (sts_entry->completionStatus) {
	case SCS_COMPLETE:

		if (sts_entry->iscsiFlags & ISCSI_FLAG_RESIDUAL_OVER) {
			cmd->result = DID_ERROR << 16;
			break;
		}

		if (sts_entry->iscsiFlags &ISCSI_FLAG_RESIDUAL_UNDER) {
			scsi_set_resid(cmd, residual);
			if (!scsi_status && ((scsi_bufflen(cmd) - residual) <
				cmd->underflow)) {

				cmd->result = DID_ERROR << 16;

				DEBUG2(printk("scsi%ld:%d:%d:%d: %s: "
					"Mid-layer Data underrun0, "
					"xferlen = 0x%x, "
					"residual = 0x%x\n", ha->host_no,
					cmd->device->channel,
					cmd->device->id,
					cmd->device->lun, __func__,
					scsi_bufflen(cmd), residual));
				break;
			}
		}

		cmd->result = DID_OK << 16 | scsi_status;

		if (scsi_status != SCSI_CHECK_CONDITION)
			break;

		/* Copy Sense Data into sense buffer. */
		qla4xxx_copy_sense(ha, sts_entry, srb);
		break;

	case SCS_INCOMPLETE:
		/* Always set the status to DID_ERROR, since
		 * all conditions result in that status anyway */
		cmd->result = DID_ERROR << 16;
		break;

	case SCS_RESET_OCCURRED:
		DEBUG2(printk("scsi%ld:%d:%d:%d: %s: Device RESET occurred\n",
			      ha->host_no, cmd->device->channel,
			      cmd->device->id, cmd->device->lun, __func__));

		cmd->result = DID_RESET << 16;
		break;

	case SCS_ABORTED:
		DEBUG2(printk("scsi%ld:%d:%d:%d: %s: Abort occurred\n",
			      ha->host_no, cmd->device->channel,
			      cmd->device->id, cmd->device->lun, __func__));

		cmd->result = DID_RESET << 16;
		break;

	case SCS_TIMEOUT:
		DEBUG2(printk(KERN_INFO "scsi%ld:%d:%d:%d: Timeout\n",
			      ha->host_no, cmd->device->channel,
			      cmd->device->id, cmd->device->lun));

		cmd->result = DID_TRANSPORT_DISRUPTED << 16;

		/*
		 * Mark device missing so that we won't continue to send
		 * I/O to this device.	We should get a ddb state change
		 * AEN soon.
		 */
		if (iscsi_is_session_online(ddb_entry->sess))
			qla4xxx_mark_device_missing(ddb_entry->sess);
		break;

	case SCS_DATA_UNDERRUN:
	case SCS_DATA_OVERRUN:
		if ((sts_entry->iscsiFlags & ISCSI_FLAG_RESIDUAL_OVER) ||
		     (sts_entry->completionStatus == SCS_DATA_OVERRUN)) {
			DEBUG2(printk("scsi%ld:%d:%d:%d: %s: " "Data overrun\n",
				      ha->host_no,
				      cmd->device->channel, cmd->device->id,
				      cmd->device->lun, __func__));

			cmd->result = DID_ERROR << 16;
			break;
		}

		scsi_set_resid(cmd, residual);

		if (sts_entry->iscsiFlags & ISCSI_FLAG_RESIDUAL_UNDER) {

			/* Both the firmware and target reported UNDERRUN:
			 *
			 * MID-LAYER UNDERFLOW case:
			 * Some kernels do not properly detect midlayer
			 * underflow, so we manually check it and return
			 * ERROR if the minimum required data was not
			 * received.
			 *
			 * ALL OTHER cases:
			 * Fall thru to check scsi_status
			 */
			if (!scsi_status && (scsi_bufflen(cmd) - residual) <
			    cmd->underflow) {
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "scsi%ld:%d:%d:%d: %s: Mid-layer Data underrun, xferlen = 0x%x,residual = 0x%x\n",
						   ha->host_no,
						   cmd->device->channel,
						   cmd->device->id,
						   cmd->device->lun, __func__,
						   scsi_bufflen(cmd),
						   residual));

				cmd->result = DID_ERROR << 16;
				break;
			}

		} else if (scsi_status != SAM_STAT_TASK_SET_FULL &&
			   scsi_status != SAM_STAT_BUSY) {

			/*
			 * The firmware reports UNDERRUN, but the target does
			 * not report it:
			 *
			 *   scsi_status     |    host_byte       device_byte
			 *                   |     (19:16)          (7:0)
			 *   =============   |    =========       ===========
			 *   TASK_SET_FULL   |    DID_OK          scsi_status
			 *   BUSY            |    DID_OK          scsi_status
			 *   ALL OTHERS      |    DID_ERROR       scsi_status
			 *
			 *   Note: If scsi_status is task set full or busy,
			 *   then this else if would fall thru to check the
			 *   scsi_status and return DID_OK.
			 */

			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "scsi%ld:%d:%d:%d: %s: Dropped frame(s) detected (0x%x of 0x%x bytes).\n",
					  ha->host_no,
					  cmd->device->channel,
					  cmd->device->id,
					  cmd->device->lun, __func__,
					  residual,
					  scsi_bufflen(cmd)));

			cmd->result = DID_ERROR << 16 | scsi_status;
			goto check_scsi_status;
		}

		cmd->result = DID_OK << 16 | scsi_status;

check_scsi_status:
		if (scsi_status == SAM_STAT_CHECK_CONDITION)
			qla4xxx_copy_sense(ha, sts_entry, srb);

		break;

	case SCS_DEVICE_LOGGED_OUT:
	case SCS_DEVICE_UNAVAILABLE:
		DEBUG2(printk(KERN_INFO "scsi%ld:%d:%d:%d: SCS_DEVICE "
		    "state: 0x%x\n", ha->host_no,
		    cmd->device->channel, cmd->device->id,
		    cmd->device->lun, sts_entry->completionStatus));
		/*
		 * Mark device missing so that we won't continue to
		 * send I/O to this device.  We should get a ddb
		 * state change AEN soon.
		 */
		if (iscsi_is_session_online(ddb_entry->sess))
			qla4xxx_mark_device_missing(ddb_entry->sess);

		cmd->result = DID_TRANSPORT_DISRUPTED << 16;
		break;

	case SCS_QUEUE_FULL:
		/*
		 * SCSI Mid-Layer handles device queue full
		 */
		cmd->result = DID_OK << 16 | sts_entry->scsiStatus;
		DEBUG2(printk("scsi%ld:%d:%d: %s: QUEUE FULL detected "
			      "compl=%02x, scsi=%02x, state=%02x, iFlags=%02x,"
			      " iResp=%02x\n", ha->host_no, cmd->device->id,
			      cmd->device->lun, __func__,
			      sts_entry->completionStatus,
			      sts_entry->scsiStatus, sts_entry->state_flags,
			      sts_entry->iscsiFlags,
			      sts_entry->iscsiResponse));
		break;

	default:
		cmd->result = DID_ERROR << 16;
		break;
	}

status_entry_exit:

	/* complete the request, if not waiting for status_continuation pkt */
	srb->cc_stat = sts_entry->completionStatus;
	if (ha->status_srb == NULL)
		kref_put(&srb->srb_ref, qla4xxx_srb_compl);
}

/**
 * qla4xxx_passthru_status_entry - processes passthru status IOCBs (0x3C)
 * @ha: Pointer to host adapter structure.
 * @sts_entry: Pointer to status entry structure.
 **/
static void qla4xxx_passthru_status_entry(struct scsi_qla_host *ha,
					  struct passthru_status *sts_entry)
{
	struct iscsi_task *task;
	struct ddb_entry *ddb_entry;
	struct ql4_task_data *task_data;
	struct iscsi_cls_conn *cls_conn;
	struct iscsi_conn *conn;
	itt_t itt;
	uint32_t fw_ddb_index;

	itt = sts_entry->handle;
	fw_ddb_index = le32_to_cpu(sts_entry->target);

	ddb_entry = qla4xxx_lookup_ddb_by_fw_index(ha, fw_ddb_index);

	if (ddb_entry == NULL) {
		ql4_printk(KERN_ERR, ha, "%s: Invalid target index = 0x%x\n",
			   __func__, sts_entry->target);
		return;
	}

	cls_conn = ddb_entry->conn;
	conn = cls_conn->dd_data;
	spin_lock(&conn->session->lock);
	task = iscsi_itt_to_task(conn, itt);
	spin_unlock(&conn->session->lock);

	if (task == NULL) {
		ql4_printk(KERN_ERR, ha, "%s: Task is NULL\n", __func__);
		return;
	}

	task_data = task->dd_data;
	memcpy(&task_data->sts, sts_entry, sizeof(struct passthru_status));
	ha->req_q_count += task_data->iocb_req_cnt;
	ha->iocb_cnt -= task_data->iocb_req_cnt;
	queue_work(ha->task_wq, &task_data->task_work);
}

static struct mrb *qla4xxx_del_mrb_from_active_array(struct scsi_qla_host *ha,
						     uint32_t index)
{
	struct mrb *mrb = NULL;

	/* validate handle and remove from active array */
	if (index >= MAX_MRB)
		return mrb;

	mrb = ha->active_mrb_array[index];
	ha->active_mrb_array[index] = NULL;
	if (!mrb)
		return mrb;

	/* update counters */
	ha->req_q_count += mrb->iocb_cnt;
	ha->iocb_cnt -= mrb->iocb_cnt;

	return mrb;
}

static void qla4xxx_mbox_status_entry(struct scsi_qla_host *ha,
				      struct mbox_status_iocb *mbox_sts_entry)
{
	struct mrb *mrb;
	uint32_t status;
	uint32_t data_size;

	mrb = qla4xxx_del_mrb_from_active_array(ha,
					le32_to_cpu(mbox_sts_entry->handle));

	if (mrb == NULL) {
		ql4_printk(KERN_WARNING, ha, "%s: mrb[%d] is null\n", __func__,
			   mbox_sts_entry->handle);
		return;
	}

	switch (mrb->mbox_cmd) {
	case MBOX_CMD_PING:
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: mbox_cmd = 0x%x, "
				  "mbox_sts[0] = 0x%x, mbox_sts[6] = 0x%x\n",
				  __func__, mrb->mbox_cmd,
				  mbox_sts_entry->out_mbox[0],
				  mbox_sts_entry->out_mbox[6]));

		if (mbox_sts_entry->out_mbox[0] == MBOX_STS_COMMAND_COMPLETE)
			status = ISCSI_PING_SUCCESS;
		else
			status = mbox_sts_entry->out_mbox[6];

		data_size = sizeof(mbox_sts_entry->out_mbox);

		qla4xxx_post_ping_evt_work(ha, status, mrb->pid, data_size,
					(uint8_t *) mbox_sts_entry->out_mbox);
		break;

	default:
		DEBUG2(ql4_printk(KERN_WARNING, ha, "%s: invalid mbox_cmd = "
				  "0x%x\n", __func__, mrb->mbox_cmd));
	}

	kfree(mrb);
	return;
}

/**
 * qla4xxx_process_response_queue - process response queue completions
 * @ha: Pointer to host adapter structure.
 *
 * This routine process response queue completions in interrupt context.
 * Hardware_lock locked upon entry
 **/
void qla4xxx_process_response_queue(struct scsi_qla_host *ha)
{
	uint32_t count = 0;
	struct srb *srb = NULL;
	struct status_entry *sts_entry;

	/* Process all responses from response queue */
	while ((ha->response_ptr->signature != RESPONSE_PROCESSED)) {
		sts_entry = (struct status_entry *) ha->response_ptr;
		count++;

		/* Advance pointers for next entry */
		if (ha->response_out == (RESPONSE_QUEUE_DEPTH - 1)) {
			ha->response_out = 0;
			ha->response_ptr = ha->response_ring;
		} else {
			ha->response_out++;
			ha->response_ptr++;
		}

		/* process entry */
		switch (sts_entry->hdr.entryType) {
		case ET_STATUS:
			/* Common status */
			qla4xxx_status_entry(ha, sts_entry);
			break;

		case ET_PASSTHRU_STATUS:
			if (sts_entry->hdr.systemDefined == SD_ISCSI_PDU)
				qla4xxx_passthru_status_entry(ha,
					(struct passthru_status *)sts_entry);
			else
				ql4_printk(KERN_ERR, ha,
					   "%s: Invalid status received\n",
					   __func__);

			break;

		case ET_STATUS_CONTINUATION:
			qla4xxx_status_cont_entry(ha,
				(struct status_cont_entry *) sts_entry);
			break;

		case ET_COMMAND:
			/* ISP device queue is full. Command not
			 * accepted by ISP.  Queue command for
			 * later */

			srb = qla4xxx_del_from_active_array(ha,
						    le32_to_cpu(sts_entry->
								handle));
			if (srb == NULL)
				goto exit_prq_invalid_handle;

			DEBUG2(printk("scsi%ld: %s: FW device queue full, "
				      "srb %p\n", ha->host_no, __func__, srb));

			/* ETRY normally by sending it back with
			 * DID_BUS_BUSY */
			srb->cmd->result = DID_BUS_BUSY << 16;
			kref_put(&srb->srb_ref, qla4xxx_srb_compl);
			break;

		case ET_CONTINUE:
			/* Just throw away the continuation entries */
			DEBUG2(printk("scsi%ld: %s: Continuation entry - "
				      "ignoring\n", ha->host_no, __func__));
			break;

		case ET_MBOX_STATUS:
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "%s: mbox status IOCB\n", __func__));
			qla4xxx_mbox_status_entry(ha,
					(struct mbox_status_iocb *)sts_entry);
			break;

		default:
			/*
			 * Invalid entry in response queue, reset RISC
			 * firmware.
			 */
			DEBUG2(printk("scsi%ld: %s: Invalid entry %x in "
				      "response queue \n", ha->host_no,
				      __func__,
				      sts_entry->hdr.entryType));
			goto exit_prq_error;
		}
		((struct response *)sts_entry)->signature = RESPONSE_PROCESSED;
		wmb();
	}

	/*
	 * Tell ISP we're done with response(s). This also clears the interrupt.
	 */
	ha->isp_ops->complete_iocb(ha);

	return;

exit_prq_invalid_handle:
	DEBUG2(printk("scsi%ld: %s: Invalid handle(srb)=%p type=%x IOCS=%x\n",
		      ha->host_no, __func__, srb, sts_entry->hdr.entryType,
		      sts_entry->completionStatus));

exit_prq_error:
	ha->isp_ops->complete_iocb(ha);
	set_bit(DPC_RESET_HA, &ha->dpc_flags);
}

/**
 * qla4_83xx_loopback_in_progress: Is loopback in progress?
 * @ha: Pointer to host adapter structure.
 * @ret: 1 = loopback in progress, 0 = loopback not in progress
 **/
static int qla4_83xx_loopback_in_progress(struct scsi_qla_host *ha)
{
	int rval = 1;

	if (is_qla8032(ha)) {
		if ((ha->idc_info.info2 & ENABLE_INTERNAL_LOOPBACK) ||
		    (ha->idc_info.info2 & ENABLE_EXTERNAL_LOOPBACK)) {
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "%s: Loopback diagnostics in progress\n",
					  __func__));
			rval = 1;
		} else {
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "%s: Loopback diagnostics not in progress\n",
					  __func__));
			rval = 0;
		}
	}

	return rval;
}

/**
 * qla4xxx_isr_decode_mailbox - decodes mailbox status
 * @ha: Pointer to host adapter structure.
 * @mailbox_status: Mailbox status.
 *
 * This routine decodes the mailbox status during the ISR.
 * Hardware_lock locked upon entry. runs in interrupt context.
 **/
static void qla4xxx_isr_decode_mailbox(struct scsi_qla_host * ha,
				       uint32_t mbox_status)
{
	int i;
	uint32_t mbox_sts[MBOX_AEN_REG_COUNT];
	__le32 __iomem *mailbox_out;

	if (is_qla8032(ha))
		mailbox_out = &ha->qla4_83xx_reg->mailbox_out[0];
	else if (is_qla8022(ha))
		mailbox_out = &ha->qla4_82xx_reg->mailbox_out[0];
	else
		mailbox_out = &ha->reg->mailbox[0];

	if ((mbox_status == MBOX_STS_BUSY) ||
	    (mbox_status == MBOX_STS_INTERMEDIATE_COMPLETION) ||
	    (mbox_status >> 12 == MBOX_COMPLETION_STATUS)) {
		ha->mbox_status[0] = mbox_status;

		if (test_bit(AF_MBOX_COMMAND, &ha->flags)) {
			/*
			 * Copy all mailbox registers to a temporary
			 * location and set mailbox command done flag
			 */
			for (i = 0; i < ha->mbox_status_count; i++)
				ha->mbox_status[i] = readl(&mailbox_out[i]);

			set_bit(AF_MBOX_COMMAND_DONE, &ha->flags);

			if (test_bit(AF_MBOX_COMMAND_NOPOLL, &ha->flags))
				complete(&ha->mbx_intr_comp);
		}
	} else if (mbox_status >> 12 == MBOX_ASYNC_EVENT_STATUS) {
		for (i = 0; i < MBOX_AEN_REG_COUNT; i++)
			mbox_sts[i] = readl(&mailbox_out[i]);

		/* Immediately process the AENs that don't require much work.
		 * Only queue the database_changed AENs */
		if (ha->aen_log.count < MAX_AEN_ENTRIES) {
			for (i = 0; i < MBOX_AEN_REG_COUNT; i++)
				ha->aen_log.entry[ha->aen_log.count].mbox_sts[i] =
				    mbox_sts[i];
			ha->aen_log.count++;
		}
		switch (mbox_status) {
		case MBOX_ASTS_SYSTEM_ERROR:
			/* Log Mailbox registers */
			ql4_printk(KERN_INFO, ha, "%s: System Err\n", __func__);
			qla4xxx_dump_registers(ha);

			if ((is_qla8022(ha) && ql4xdontresethba) ||
			    (is_qla8032(ha) && qla4_83xx_idc_dontreset(ha))) {
				DEBUG2(printk("scsi%ld: %s:Don't Reset HBA\n",
				    ha->host_no, __func__));
			} else {
				set_bit(AF_GET_CRASH_RECORD, &ha->flags);
				set_bit(DPC_RESET_HA, &ha->dpc_flags);
			}
			break;

		case MBOX_ASTS_REQUEST_TRANSFER_ERROR:
		case MBOX_ASTS_RESPONSE_TRANSFER_ERROR:
		case MBOX_ASTS_NVRAM_INVALID:
		case MBOX_ASTS_IP_ADDRESS_CHANGED:
		case MBOX_ASTS_DHCP_LEASE_EXPIRED:
			DEBUG2(printk("scsi%ld: AEN %04x, ERROR Status, "
				      "Reset HA\n", ha->host_no, mbox_status));
			if (is_qla80XX(ha))
				set_bit(DPC_RESET_HA_FW_CONTEXT,
					&ha->dpc_flags);
			else
				set_bit(DPC_RESET_HA, &ha->dpc_flags);
			break;

		case MBOX_ASTS_LINK_UP:
			set_bit(AF_LINK_UP, &ha->flags);
			if (test_bit(AF_INIT_DONE, &ha->flags))
				set_bit(DPC_LINK_CHANGED, &ha->dpc_flags);

			ql4_printk(KERN_INFO, ha, "%s: LINK UP\n", __func__);
			qla4xxx_post_aen_work(ha, ISCSI_EVENT_LINKUP,
					      sizeof(mbox_sts),
					      (uint8_t *) mbox_sts);
			break;

		case MBOX_ASTS_LINK_DOWN:
			clear_bit(AF_LINK_UP, &ha->flags);
			if (test_bit(AF_INIT_DONE, &ha->flags)) {
				set_bit(DPC_LINK_CHANGED, &ha->dpc_flags);
				qla4xxx_wake_dpc(ha);
			}

			ql4_printk(KERN_INFO, ha, "%s: LINK DOWN\n", __func__);
			qla4xxx_post_aen_work(ha, ISCSI_EVENT_LINKDOWN,
					      sizeof(mbox_sts),
					      (uint8_t *) mbox_sts);
			break;

		case MBOX_ASTS_HEARTBEAT:
			ha->seconds_since_last_heartbeat = 0;
			break;

		case MBOX_ASTS_DHCP_LEASE_ACQUIRED:
			DEBUG2(printk("scsi%ld: AEN %04x DHCP LEASE "
				      "ACQUIRED\n", ha->host_no, mbox_status));
			set_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags);
			break;

		case MBOX_ASTS_PROTOCOL_STATISTIC_ALARM:
		case MBOX_ASTS_SCSI_COMMAND_PDU_REJECTED: /* Target
							   * mode
							   * only */
		case MBOX_ASTS_UNSOLICITED_PDU_RECEIVED:  /* Connection mode */
		case MBOX_ASTS_IPSEC_SYSTEM_FATAL_ERROR:
		case MBOX_ASTS_SUBNET_STATE_CHANGE:
		case MBOX_ASTS_DUPLICATE_IP:
			/* No action */
			DEBUG2(printk("scsi%ld: AEN %04x\n", ha->host_no,
				      mbox_status));
			break;

		case MBOX_ASTS_IP_ADDR_STATE_CHANGED:
			printk("scsi%ld: AEN %04x, mbox_sts[2]=%04x, "
			    "mbox_sts[3]=%04x\n", ha->host_no, mbox_sts[0],
			    mbox_sts[2], mbox_sts[3]);

			/* mbox_sts[2] = Old ACB state
			 * mbox_sts[3] = new ACB state */
			if ((mbox_sts[3] == ACB_STATE_VALID) &&
			    ((mbox_sts[2] == ACB_STATE_TENTATIVE) ||
			    (mbox_sts[2] == ACB_STATE_ACQUIRING)))
				set_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags);
			else if ((mbox_sts[3] == ACB_STATE_ACQUIRING) &&
				 (mbox_sts[2] == ACB_STATE_VALID)) {
				if (is_qla80XX(ha))
					set_bit(DPC_RESET_HA_FW_CONTEXT,
						&ha->dpc_flags);
				else
					set_bit(DPC_RESET_HA, &ha->dpc_flags);
			} else if ((mbox_sts[3] == ACB_STATE_UNCONFIGURED))
				complete(&ha->disable_acb_comp);
			break;

		case MBOX_ASTS_MAC_ADDRESS_CHANGED:
		case MBOX_ASTS_DNS:
			/* No action */
			DEBUG2(printk(KERN_INFO "scsi%ld: AEN %04x, "
				      "mbox_sts[1]=%04x, mbox_sts[2]=%04x\n",
				      ha->host_no, mbox_sts[0],
				      mbox_sts[1], mbox_sts[2]));
			break;

		case MBOX_ASTS_SELF_TEST_FAILED:
		case MBOX_ASTS_LOGIN_FAILED:
			/* No action */
			DEBUG2(printk("scsi%ld: AEN %04x, mbox_sts[1]=%04x, "
				      "mbox_sts[2]=%04x, mbox_sts[3]=%04x\n",
				      ha->host_no, mbox_sts[0], mbox_sts[1],
				      mbox_sts[2], mbox_sts[3]));
			break;

		case MBOX_ASTS_DATABASE_CHANGED:
			/* Queue AEN information and process it in the DPC
			 * routine */
			if (ha->aen_q_count > 0) {

				/* decrement available counter */
				ha->aen_q_count--;

				for (i = 0; i < MBOX_AEN_REG_COUNT; i++)
					ha->aen_q[ha->aen_in].mbox_sts[i] =
					    mbox_sts[i];

				/* print debug message */
				DEBUG2(printk("scsi%ld: AEN[%d] %04x queued "
					      "mb1:0x%x mb2:0x%x mb3:0x%x "
					      "mb4:0x%x mb5:0x%x\n",
					      ha->host_no, ha->aen_in,
					      mbox_sts[0], mbox_sts[1],
					      mbox_sts[2], mbox_sts[3],
					      mbox_sts[4], mbox_sts[5]));

				/* advance pointer */
				ha->aen_in++;
				if (ha->aen_in == MAX_AEN_ENTRIES)
					ha->aen_in = 0;

				/* The DPC routine will process the aen */
				set_bit(DPC_AEN, &ha->dpc_flags);
			} else {
				DEBUG2(printk("scsi%ld: %s: aen %04x, queue "
					      "overflowed!  AEN LOST!!\n",
					      ha->host_no, __func__,
					      mbox_sts[0]));

				DEBUG2(printk("scsi%ld: DUMP AEN QUEUE\n",
					      ha->host_no));

				for (i = 0; i < MAX_AEN_ENTRIES; i++) {
					DEBUG2(printk("AEN[%d] %04x %04x %04x "
						      "%04x\n", i, mbox_sts[0],
						      mbox_sts[1], mbox_sts[2],
						      mbox_sts[3]));
				}
			}
			break;

		case MBOX_ASTS_TXSCVR_INSERTED:
			DEBUG2(printk(KERN_WARNING
			    "scsi%ld: AEN %04x Transceiver"
			    " inserted\n",  ha->host_no, mbox_sts[0]));
			break;

		case MBOX_ASTS_TXSCVR_REMOVED:
			DEBUG2(printk(KERN_WARNING
			    "scsi%ld: AEN %04x Transceiver"
			    " removed\n",  ha->host_no, mbox_sts[0]));
			break;

		case MBOX_ASTS_IDC_REQUEST_NOTIFICATION:
		{
			uint32_t opcode;
			if (is_qla8032(ha)) {
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "scsi%ld: AEN %04x, mbox_sts[1]=%08x, mbox_sts[2]=%08x, mbox_sts[3]=%08x, mbox_sts[4]=%08x\n",
						  ha->host_no, mbox_sts[0],
						  mbox_sts[1], mbox_sts[2],
						  mbox_sts[3], mbox_sts[4]));
				opcode = mbox_sts[1] >> 16;
				if ((opcode == MBOX_CMD_SET_PORT_CONFIG) ||
				    (opcode == MBOX_CMD_PORT_RESET)) {
					set_bit(DPC_POST_IDC_ACK,
						&ha->dpc_flags);
					ha->idc_info.request_desc = mbox_sts[1];
					ha->idc_info.info1 = mbox_sts[2];
					ha->idc_info.info2 = mbox_sts[3];
					ha->idc_info.info3 = mbox_sts[4];
					qla4xxx_wake_dpc(ha);
				}
			}
			break;
		}

		case MBOX_ASTS_IDC_COMPLETE:
			if (is_qla8032(ha)) {
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "scsi%ld: AEN %04x, mbox_sts[1]=%08x, mbox_sts[2]=%08x, mbox_sts[3]=%08x, mbox_sts[4]=%08x\n",
						  ha->host_no, mbox_sts[0],
						  mbox_sts[1], mbox_sts[2],
						  mbox_sts[3], mbox_sts[4]));
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "scsi:%ld: AEN %04x IDC Complete notification\n",
						  ha->host_no, mbox_sts[0]));

				if (qla4_83xx_loopback_in_progress(ha))
					set_bit(AF_LOOPBACK, &ha->flags);
				else
					clear_bit(AF_LOOPBACK, &ha->flags);
			}
			break;

		default:
			DEBUG2(printk(KERN_WARNING
				      "scsi%ld: AEN %04x UNKNOWN\n",
				      ha->host_no, mbox_sts[0]));
			break;
		}
	} else {
		DEBUG2(printk("scsi%ld: Unknown mailbox status %08X\n",
			      ha->host_no, mbox_status));

		ha->mbox_status[0] = mbox_status;
	}
}

void qla4_83xx_interrupt_service_routine(struct scsi_qla_host *ha,
					 uint32_t intr_status)
{
	/* Process mailbox/asynch event interrupt.*/
	if (intr_status) {
		qla4xxx_isr_decode_mailbox(ha,
				readl(&ha->qla4_83xx_reg->mailbox_out[0]));
		/* clear the interrupt */
		writel(0, &ha->qla4_83xx_reg->risc_intr);
	} else {
		qla4xxx_process_response_queue(ha);
	}

	/* clear the interrupt */
	writel(0, &ha->qla4_83xx_reg->mb_int_mask);
}

/**
 * qla4_82xx_interrupt_service_routine - isr
 * @ha: pointer to host adapter structure.
 *
 * This is the main interrupt service routine.
 * hardware_lock locked upon entry. runs in interrupt context.
 **/
void qla4_82xx_interrupt_service_routine(struct scsi_qla_host *ha,
    uint32_t intr_status)
{
	/* Process response queue interrupt. */
	if (intr_status & HSRX_RISC_IOCB_INT)
		qla4xxx_process_response_queue(ha);

	/* Process mailbox/asynch event interrupt.*/
	if (intr_status & HSRX_RISC_MB_INT)
		qla4xxx_isr_decode_mailbox(ha,
		    readl(&ha->qla4_82xx_reg->mailbox_out[0]));

	/* clear the interrupt */
	writel(0, &ha->qla4_82xx_reg->host_int);
	readl(&ha->qla4_82xx_reg->host_int);
}

/**
 * qla4xxx_interrupt_service_routine - isr
 * @ha: pointer to host adapter structure.
 *
 * This is the main interrupt service routine.
 * hardware_lock locked upon entry. runs in interrupt context.
 **/
void qla4xxx_interrupt_service_routine(struct scsi_qla_host * ha,
				       uint32_t intr_status)
{
	/* Process response queue interrupt. */
	if (intr_status & CSR_SCSI_COMPLETION_INTR)
		qla4xxx_process_response_queue(ha);

	/* Process mailbox/asynch event	 interrupt.*/
	if (intr_status & CSR_SCSI_PROCESSOR_INTR) {
		qla4xxx_isr_decode_mailbox(ha,
					   readl(&ha->reg->mailbox[0]));

		/* Clear Mailbox Interrupt */
		writel(set_rmask(CSR_SCSI_PROCESSOR_INTR),
		       &ha->reg->ctrl_status);
		readl(&ha->reg->ctrl_status);
	}
}

/**
 * qla4_82xx_spurious_interrupt - processes spurious interrupt
 * @ha: pointer to host adapter structure.
 * @reqs_count: .
 *
 **/
static void qla4_82xx_spurious_interrupt(struct scsi_qla_host *ha,
    uint8_t reqs_count)
{
	if (reqs_count)
		return;

	DEBUG2(ql4_printk(KERN_INFO, ha, "Spurious Interrupt\n"));
	if (is_qla8022(ha)) {
		writel(0, &ha->qla4_82xx_reg->host_int);
		if (test_bit(AF_INTx_ENABLED, &ha->flags))
			qla4_82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg,
			    0xfbff);
	}
	ha->spurious_int_count++;
}

/**
 * qla4xxx_intr_handler - hardware interrupt handler.
 * @irq: Unused
 * @dev_id: Pointer to host adapter structure
 **/
irqreturn_t qla4xxx_intr_handler(int irq, void *dev_id)
{
	struct scsi_qla_host *ha;
	uint32_t intr_status;
	unsigned long flags = 0;
	uint8_t reqs_count = 0;

	ha = (struct scsi_qla_host *) dev_id;
	if (!ha) {
		DEBUG2(printk(KERN_INFO
			      "qla4xxx: Interrupt with NULL host ptr\n"));
		return IRQ_NONE;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);

	ha->isr_count++;
	/*
	 * Repeatedly service interrupts up to a maximum of
	 * MAX_REQS_SERVICED_PER_INTR
	 */
	while (1) {
		/*
		 * Read interrupt status
		 */
		if (ha->isp_ops->rd_shdw_rsp_q_in(ha) !=
		    ha->response_out)
			intr_status = CSR_SCSI_COMPLETION_INTR;
		else
			intr_status = readl(&ha->reg->ctrl_status);

		if ((intr_status &
		    (CSR_SCSI_RESET_INTR|CSR_FATAL_ERROR|INTR_PENDING)) == 0) {
			if (reqs_count == 0)
				ha->spurious_int_count++;
			break;
		}

		if (intr_status & CSR_FATAL_ERROR) {
			DEBUG2(printk(KERN_INFO "scsi%ld: Fatal Error, "
				      "Status 0x%04x\n", ha->host_no,
				      readl(isp_port_error_status (ha))));

			/* Issue Soft Reset to clear this error condition.
			 * This will prevent the RISC from repeatedly
			 * interrupting the driver; thus, allowing the DPC to
			 * get scheduled to continue error recovery.
			 * NOTE: Disabling RISC interrupts does not work in
			 * this case, as CSR_FATAL_ERROR overrides
			 * CSR_SCSI_INTR_ENABLE */
			if ((readl(&ha->reg->ctrl_status) &
			     CSR_SCSI_RESET_INTR) == 0) {
				writel(set_rmask(CSR_SOFT_RESET),
				       &ha->reg->ctrl_status);
				readl(&ha->reg->ctrl_status);
			}

			writel(set_rmask(CSR_FATAL_ERROR),
			       &ha->reg->ctrl_status);
			readl(&ha->reg->ctrl_status);

			__qla4xxx_disable_intrs(ha);

			set_bit(DPC_RESET_HA, &ha->dpc_flags);

			break;
		} else if (intr_status & CSR_SCSI_RESET_INTR) {
			clear_bit(AF_ONLINE, &ha->flags);
			__qla4xxx_disable_intrs(ha);

			writel(set_rmask(CSR_SCSI_RESET_INTR),
			       &ha->reg->ctrl_status);
			readl(&ha->reg->ctrl_status);

			if (!test_bit(AF_HA_REMOVAL, &ha->flags))
				set_bit(DPC_RESET_HA_INTR, &ha->dpc_flags);

			break;
		} else if (intr_status & INTR_PENDING) {
			ha->isp_ops->interrupt_service_routine(ha, intr_status);
			ha->total_io_count++;
			if (++reqs_count == MAX_REQS_SERVICED_PER_INTR)
				break;
		}
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return IRQ_HANDLED;
}

/**
 * qla4_82xx_intr_handler - hardware interrupt handler.
 * @irq: Unused
 * @dev_id: Pointer to host adapter structure
 **/
irqreturn_t qla4_82xx_intr_handler(int irq, void *dev_id)
{
	struct scsi_qla_host *ha = dev_id;
	uint32_t intr_status;
	uint32_t status;
	unsigned long flags = 0;
	uint8_t reqs_count = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return IRQ_HANDLED;

	ha->isr_count++;
	status = qla4_82xx_rd_32(ha, ISR_INT_VECTOR);
	if (!(status & ha->nx_legacy_intr.int_vec_bit))
		return IRQ_NONE;

	status = qla4_82xx_rd_32(ha, ISR_INT_STATE_REG);
	if (!ISR_IS_LEGACY_INTR_TRIGGERED(status)) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
		    "%s legacy Int not triggered\n", __func__));
		return IRQ_NONE;
	}

	/* clear the interrupt */
	qla4_82xx_wr_32(ha, ha->nx_legacy_intr.tgt_status_reg, 0xffffffff);

	/* read twice to ensure write is flushed */
	qla4_82xx_rd_32(ha, ISR_INT_VECTOR);
	qla4_82xx_rd_32(ha, ISR_INT_VECTOR);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	while (1) {
		if (!(readl(&ha->qla4_82xx_reg->host_int) &
		    ISRX_82XX_RISC_INT)) {
			qla4_82xx_spurious_interrupt(ha, reqs_count);
			break;
		}
		intr_status =  readl(&ha->qla4_82xx_reg->host_status);
		if ((intr_status &
		    (HSRX_RISC_MB_INT | HSRX_RISC_IOCB_INT)) == 0)  {
			qla4_82xx_spurious_interrupt(ha, reqs_count);
			break;
		}

		ha->isp_ops->interrupt_service_routine(ha, intr_status);

		/* Enable Interrupt */
		qla4_82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0xfbff);

		if (++reqs_count == MAX_REQS_SERVICED_PER_INTR)
			break;
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return IRQ_HANDLED;
}

#define LEG_INT_PTR_B31		(1 << 31)
#define LEG_INT_PTR_B30		(1 << 30)
#define PF_BITS_MASK		(0xF << 16)

/**
 * qla4_83xx_intr_handler - hardware interrupt handler.
 * @irq: Unused
 * @dev_id: Pointer to host adapter structure
 **/
irqreturn_t qla4_83xx_intr_handler(int irq, void *dev_id)
{
	struct scsi_qla_host *ha = dev_id;
	uint32_t leg_int_ptr = 0;
	unsigned long flags = 0;

	ha->isr_count++;
	leg_int_ptr = readl(&ha->qla4_83xx_reg->leg_int_ptr);

	/* Legacy interrupt is valid if bit31 of leg_int_ptr is set */
	if (!(leg_int_ptr & LEG_INT_PTR_B31)) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Legacy Interrupt Bit 31 not set, spurious interrupt!\n",
				  __func__));
		return IRQ_NONE;
	}

	/* Validate the PCIE function ID set in leg_int_ptr bits [19..16] */
	if ((leg_int_ptr & PF_BITS_MASK) != ha->pf_bit) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Incorrect function ID 0x%x in legacy interrupt register, ha->pf_bit = 0x%x\n",
				  __func__, (leg_int_ptr & PF_BITS_MASK),
				  ha->pf_bit));
		return IRQ_NONE;
	}

	/* To de-assert legacy interrupt, write 0 to Legacy Interrupt Trigger
	 * Control register and poll till Legacy Interrupt Pointer register
	 * bit30 is 0.
	 */
	writel(0, &ha->qla4_83xx_reg->leg_int_trig);
	do {
		leg_int_ptr = readl(&ha->qla4_83xx_reg->leg_int_ptr);
		if ((leg_int_ptr & PF_BITS_MASK) != ha->pf_bit)
			break;
	} while (leg_int_ptr & LEG_INT_PTR_B30);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	leg_int_ptr = readl(&ha->qla4_83xx_reg->risc_intr);
	ha->isp_ops->interrupt_service_routine(ha, leg_int_ptr);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return IRQ_HANDLED;
}

irqreturn_t
qla4_8xxx_msi_handler(int irq, void *dev_id)
{
	struct scsi_qla_host *ha;

	ha = (struct scsi_qla_host *) dev_id;
	if (!ha) {
		DEBUG2(printk(KERN_INFO
		    "qla4xxx: MSIX: Interrupt with NULL host ptr\n"));
		return IRQ_NONE;
	}

	ha->isr_count++;
	/* clear the interrupt */
	qla4_82xx_wr_32(ha, ha->nx_legacy_intr.tgt_status_reg, 0xffffffff);

	/* read twice to ensure write is flushed */
	qla4_82xx_rd_32(ha, ISR_INT_VECTOR);
	qla4_82xx_rd_32(ha, ISR_INT_VECTOR);

	return qla4_8xxx_default_intr_handler(irq, dev_id);
}

static irqreturn_t qla4_83xx_mailbox_intr_handler(int irq, void *dev_id)
{
	struct scsi_qla_host *ha = dev_id;
	unsigned long flags;
	uint32_t ival = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	ival = readl(&ha->qla4_83xx_reg->risc_intr);
	if (ival == 0) {
		ql4_printk(KERN_INFO, ha,
			   "%s: It is a spurious mailbox interrupt!\n",
			   __func__);
		ival = readl(&ha->qla4_83xx_reg->mb_int_mask);
		ival &= ~INT_MASK_FW_MB;
		writel(ival, &ha->qla4_83xx_reg->mb_int_mask);
		goto exit;
	}

	qla4xxx_isr_decode_mailbox(ha,
				   readl(&ha->qla4_83xx_reg->mailbox_out[0]));
	writel(0, &ha->qla4_83xx_reg->risc_intr);
	ival = readl(&ha->qla4_83xx_reg->mb_int_mask);
	ival &= ~INT_MASK_FW_MB;
	writel(ival, &ha->qla4_83xx_reg->mb_int_mask);
	ha->isr_count++;
exit:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return IRQ_HANDLED;
}

/**
 * qla4_8xxx_default_intr_handler - hardware interrupt handler.
 * @irq: Unused
 * @dev_id: Pointer to host adapter structure
 *
 * This interrupt handler is called directly for MSI-X, and
 * called indirectly for MSI.
 **/
irqreturn_t
qla4_8xxx_default_intr_handler(int irq, void *dev_id)
{
	struct scsi_qla_host *ha = dev_id;
	unsigned long   flags;
	uint32_t intr_status;
	uint8_t reqs_count = 0;

	if (is_qla8032(ha)) {
		qla4_83xx_mailbox_intr_handler(irq, dev_id);
	} else {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		while (1) {
			if (!(readl(&ha->qla4_82xx_reg->host_int) &
			    ISRX_82XX_RISC_INT)) {
				qla4_82xx_spurious_interrupt(ha, reqs_count);
				break;
			}

			intr_status =  readl(&ha->qla4_82xx_reg->host_status);
			if ((intr_status &
			    (HSRX_RISC_MB_INT | HSRX_RISC_IOCB_INT)) == 0) {
				qla4_82xx_spurious_interrupt(ha, reqs_count);
				break;
			}

			ha->isp_ops->interrupt_service_routine(ha, intr_status);

			if (++reqs_count == MAX_REQS_SERVICED_PER_INTR)
				break;
		}
		ha->isr_count++;
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}
	return IRQ_HANDLED;
}

irqreturn_t
qla4_8xxx_msix_rsp_q(int irq, void *dev_id)
{
	struct scsi_qla_host *ha = dev_id;
	unsigned long flags;
	uint32_t ival = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (is_qla8032(ha)) {
		ival = readl(&ha->qla4_83xx_reg->iocb_int_mask);
		if (ival == 0) {
			ql4_printk(KERN_INFO, ha, "%s: It is a spurious iocb interrupt!\n",
				   __func__);
			goto exit_msix_rsp_q;
		}
		qla4xxx_process_response_queue(ha);
		writel(0, &ha->qla4_83xx_reg->iocb_int_mask);
	} else {
		qla4xxx_process_response_queue(ha);
		writel(0, &ha->qla4_82xx_reg->host_int);
	}
	ha->isr_count++;
exit_msix_rsp_q:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return IRQ_HANDLED;
}

/**
 * qla4xxx_process_aen - processes AENs generated by firmware
 * @ha: pointer to host adapter structure.
 * @process_aen: type of AENs to process
 *
 * Processes specific types of Asynchronous Events generated by firmware.
 * The type of AENs to process is specified by process_aen and can be
 *	PROCESS_ALL_AENS	 0
 *	FLUSH_DDB_CHANGED_AENS	 1
 *	RELOGIN_DDB_CHANGED_AENS 2
 **/
void qla4xxx_process_aen(struct scsi_qla_host * ha, uint8_t process_aen)
{
	uint32_t mbox_sts[MBOX_AEN_REG_COUNT];
	struct aen *aen;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	while (ha->aen_out != ha->aen_in) {
		aen = &ha->aen_q[ha->aen_out];
		/* copy aen information to local structure */
		for (i = 0; i < MBOX_AEN_REG_COUNT; i++)
			mbox_sts[i] = aen->mbox_sts[i];

		ha->aen_q_count++;
		ha->aen_out++;

		if (ha->aen_out == MAX_AEN_ENTRIES)
			ha->aen_out = 0;

		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		DEBUG2(printk("qla4xxx(%ld): AEN[%d]=0x%08x, mbx1=0x%08x mbx2=0x%08x"
			" mbx3=0x%08x mbx4=0x%08x\n", ha->host_no,
			(ha->aen_out ? (ha->aen_out-1): (MAX_AEN_ENTRIES-1)),
			mbox_sts[0], mbox_sts[1], mbox_sts[2],
			mbox_sts[3], mbox_sts[4]));

		switch (mbox_sts[0]) {
		case MBOX_ASTS_DATABASE_CHANGED:
			switch (process_aen) {
			case FLUSH_DDB_CHANGED_AENS:
				DEBUG2(printk("scsi%ld: AEN[%d] %04x, index "
					      "[%d] state=%04x FLUSHED!\n",
					      ha->host_no, ha->aen_out,
					      mbox_sts[0], mbox_sts[2],
					      mbox_sts[3]));
				break;
			case PROCESS_ALL_AENS:
			default:
				/* Specific device. */
				if (mbox_sts[1] == 1)
					qla4xxx_process_ddb_changed(ha,
						mbox_sts[2], mbox_sts[3],
						mbox_sts[4]);
				break;
			}
		}
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

int qla4xxx_request_irqs(struct scsi_qla_host *ha)
{
	int ret;

	if (is_qla40XX(ha))
		goto try_intx;

	if (ql4xenablemsix == 2) {
		/* Note: MSI Interrupts not supported for ISP8324 */
		if (is_qla8032(ha)) {
			ql4_printk(KERN_INFO, ha, "%s: MSI Interrupts not supported for ISP8324, Falling back-to INTx mode\n",
				   __func__);
			goto try_intx;
		}
		goto try_msi;
	}

	if (ql4xenablemsix == 0 || ql4xenablemsix != 1)
		goto try_intx;

	/* Trying MSI-X */
	ret = qla4_8xxx_enable_msix(ha);
	if (!ret) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
		    "MSI-X: Enabled (0x%X).\n", ha->revision_id));
		goto irq_attached;
	} else {
		if (is_qla8032(ha)) {
			ql4_printk(KERN_INFO, ha, "%s: ISP8324: MSI-X: Falling back-to INTx mode. ret = %d\n",
				   __func__, ret);
			goto try_intx;
		}
	}

	ql4_printk(KERN_WARNING, ha,
	    "MSI-X: Falling back-to MSI mode -- %d.\n", ret);

try_msi:
	/* Trying MSI */
	ret = pci_enable_msi(ha->pdev);
	if (!ret) {
		ret = request_irq(ha->pdev->irq, qla4_8xxx_msi_handler,
			0, DRIVER_NAME, ha);
		if (!ret) {
			DEBUG2(ql4_printk(KERN_INFO, ha, "MSI: Enabled.\n"));
			set_bit(AF_MSI_ENABLED, &ha->flags);
			goto irq_attached;
		} else {
			ql4_printk(KERN_WARNING, ha,
			    "MSI: Failed to reserve interrupt %d "
			    "already in use.\n", ha->pdev->irq);
			pci_disable_msi(ha->pdev);
		}
	}

	/*
	 * Prevent interrupts from falling back to INTx mode in cases where
	 * interrupts cannot get acquired through MSI-X or MSI mode.
	 */
	if (is_qla8022(ha)) {
		ql4_printk(KERN_WARNING, ha, "IRQ not attached -- %d.\n", ret);
		goto irq_not_attached;
	}
try_intx:
	/* Trying INTx */
	ret = request_irq(ha->pdev->irq, ha->isp_ops->intr_handler,
	    IRQF_SHARED, DRIVER_NAME, ha);
	if (!ret) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "INTx: Enabled.\n"));
		set_bit(AF_INTx_ENABLED, &ha->flags);
		goto irq_attached;

	} else {
		ql4_printk(KERN_WARNING, ha,
		    "INTx: Failed to reserve interrupt %d already in"
		    " use.\n", ha->pdev->irq);
		goto irq_not_attached;
	}

irq_attached:
	set_bit(AF_IRQ_ATTACHED, &ha->flags);
	ha->host->irq = ha->pdev->irq;
	ql4_printk(KERN_INFO, ha, "%s: irq %d attached\n",
	    __func__, ha->pdev->irq);
irq_not_attached:
	return ret;
}

void qla4xxx_free_irqs(struct scsi_qla_host *ha)
{
	if (test_and_clear_bit(AF_IRQ_ATTACHED, &ha->flags)) {
		if (test_bit(AF_MSIX_ENABLED, &ha->flags)) {
			qla4_8xxx_disable_msix(ha);
		} else if (test_and_clear_bit(AF_MSI_ENABLED, &ha->flags)) {
			free_irq(ha->pdev->irq, ha);
			pci_disable_msi(ha->pdev);
		} else if (test_and_clear_bit(AF_INTx_ENABLED, &ha->flags)) {
			free_irq(ha->pdev->irq, ha);
		}
	}
}
