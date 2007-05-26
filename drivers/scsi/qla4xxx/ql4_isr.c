/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"

/**
 * qla2x00_process_completed_request() - Process a Fast Post response.
 * @ha: SCSI driver HA context
 * @index: SRB index
 **/
static void qla4xxx_process_completed_request(struct scsi_qla_host *ha,
					      uint32_t index)
{
	struct srb *srb;

	srb = qla4xxx_del_from_active_array(ha, index);
	if (srb) {
		/* Save ISP completion status */
		srb->cmd->result = DID_OK << 16;
		qla4xxx_srb_compl(ha, srb);
	} else {
		DEBUG2(printk("scsi%ld: Invalid ISP SCSI completion handle = "
			      "%d\n", ha->host_no, index));
		set_bit(DPC_RESET_HA, &ha->dpc_flags);
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
	uint16_t sensebytecnt;

	if (sts_entry->completionStatus == SCS_COMPLETE &&
	    sts_entry->scsiStatus == 0) {
		qla4xxx_process_completed_request(ha,
						  le32_to_cpu(sts_entry->
							      handle));
		return;
	}

	srb = qla4xxx_del_from_active_array(ha, le32_to_cpu(sts_entry->handle));
	if (!srb) {
		/* FIXMEdg: Don't we need to reset ISP in this case??? */
		DEBUG2(printk(KERN_WARNING "scsi%ld: %s: Status Entry invalid "
			      "handle 0x%x, sp=%p. This cmd may have already "
			      "been completed.\n", ha->host_no, __func__,
			      le32_to_cpu(sts_entry->handle), srb));
		return;
	}

	cmd = srb->cmd;
	if (cmd == NULL) {
		DEBUG2(printk("scsi%ld: %s: Command already returned back to "
			      "OS pkt->handle=%d srb=%p srb->state:%d\n",
			      ha->host_no, __func__, sts_entry->handle,
			      srb, srb->state));
		dev_warn(&ha->pdev->dev, "Command is NULL:"
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
		if (scsi_status == 0) {
			cmd->result = DID_OK << 16;
			break;
		}

		if (sts_entry->iscsiFlags &
		    (ISCSI_FLAG_RESIDUAL_OVER|ISCSI_FLAG_RESIDUAL_UNDER))
			scsi_set_resid(cmd, residual);

		cmd->result = DID_OK << 16 | scsi_status;

		if (scsi_status != SCSI_CHECK_CONDITION)
			break;

		/* Copy Sense Data into sense buffer. */
		memset(cmd->sense_buffer, 0, sizeof(cmd->sense_buffer));

		sensebytecnt = le16_to_cpu(sts_entry->senseDataByteCnt);
		if (sensebytecnt == 0)
			break;

		memcpy(cmd->sense_buffer, sts_entry->senseData,
		       min(sensebytecnt,
			   (uint16_t) sizeof(cmd->sense_buffer)));

		DEBUG2(printk("scsi%ld:%d:%d:%d: %s: sense key = %x, "
			      "ASC/ASCQ = %02x/%02x\n", ha->host_no,
			      cmd->device->channel, cmd->device->id,
			      cmd->device->lun, __func__,
			      sts_entry->senseData[2] & 0x0f,
			      sts_entry->senseData[12],
			      sts_entry->senseData[13]));

		srb->flags |= SRB_GOT_SENSE;
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

		cmd->result = DID_BUS_BUSY << 16;

		/*
		 * Mark device missing so that we won't continue to send
		 * I/O to this device.	We should get a ddb state change
		 * AEN soon.
		 */
		if (atomic_read(&ddb_entry->state) == DDB_STATE_ONLINE)
			qla4xxx_mark_device_missing(ha, ddb_entry);
		break;

	case SCS_DATA_UNDERRUN:
	case SCS_DATA_OVERRUN:
		if (sts_entry->iscsiFlags & ISCSI_FLAG_RESIDUAL_OVER) {
			DEBUG2(printk("scsi%ld:%d:%d:%d: %s: " "Data overrun, "
				      "residual = 0x%x\n", ha->host_no,
				      cmd->device->channel, cmd->device->id,
				      cmd->device->lun, __func__, residual));

			cmd->result = DID_ERROR << 16;
			break;
		}

		if ((sts_entry->iscsiFlags & ISCSI_FLAG_RESIDUAL_UNDER) == 0) {
			/*
			 * Firmware detected a SCSI transport underrun
			 * condition
			 */
			scsi_set_resid(cmd, residual);
			DEBUG2(printk("scsi%ld:%d:%d:%d: %s: UNDERRUN status "
				      "detected, xferlen = 0x%x, residual = "
				      "0x%x\n",
				      ha->host_no, cmd->device->channel,
				      cmd->device->id,
				      cmd->device->lun, __func__,
				      scsi_bufflen(cmd),
				      residual));
		}

		/*
		 * If there is scsi_status, it takes precedense over
		 * underflow condition.
		 */
		if (scsi_status != 0) {
			cmd->result = DID_OK << 16 | scsi_status;

			if (scsi_status != SCSI_CHECK_CONDITION)
				break;

			/* Copy Sense Data into sense buffer. */
			memset(cmd->sense_buffer, 0,
			       sizeof(cmd->sense_buffer));

			sensebytecnt =
				le16_to_cpu(sts_entry->senseDataByteCnt);
			if (sensebytecnt == 0)
				break;

			memcpy(cmd->sense_buffer, sts_entry->senseData,
			       min(sensebytecnt,
				   (uint16_t) sizeof(cmd->sense_buffer)));

			DEBUG2(printk("scsi%ld:%d:%d:%d: %s: sense key = %x, "
				      "ASC/ASCQ = %02x/%02x\n", ha->host_no,
				      cmd->device->channel, cmd->device->id,
				      cmd->device->lun, __func__,
				      sts_entry->senseData[2] & 0x0f,
				      sts_entry->senseData[12],
				      sts_entry->senseData[13]));
		} else {
			/*
			 * If RISC reports underrun and target does not
			 * report it then we must have a lost frame, so
			 * tell upper layer to retry it by reporting a
			 * bus busy.
			 */
			if ((sts_entry->iscsiFlags &
			     ISCSI_FLAG_RESIDUAL_UNDER) == 0) {
				cmd->result = DID_BUS_BUSY << 16;
			} else if ((scsi_bufflen(cmd) - residual) <
				   cmd->underflow) {
				/*
				 * Handle mid-layer underflow???
				 *
				 * For kernels less than 2.4, the driver must
				 * return an error if an underflow is detected.
				 * For kernels equal-to and above 2.4, the
				 * mid-layer will appearantly handle the
				 * underflow by detecting the residual count --
				 * unfortunately, we do not see where this is
				 * actually being done.	 In the interim, we
				 * will return DID_ERROR.
				 */
				DEBUG2(printk("scsi%ld:%d:%d:%d: %s: "
					      "Mid-layer Data underrun, "
					      "xferlen = 0x%x, "
					      "residual = 0x%x\n", ha->host_no,
					      cmd->device->channel,
					      cmd->device->id,
					      cmd->device->lun, __func__,
					      scsi_bufflen(cmd), residual));

				cmd->result = DID_ERROR << 16;
			} else {
				cmd->result = DID_OK << 16;
			}
		}
		break;

	case SCS_DEVICE_LOGGED_OUT:
	case SCS_DEVICE_UNAVAILABLE:
		/*
		 * Mark device missing so that we won't continue to
		 * send I/O to this device.  We should get a ddb
		 * state change AEN soon.
		 */
		if (atomic_read(&ddb_entry->state) == DDB_STATE_ONLINE)
			qla4xxx_mark_device_missing(ha, ddb_entry);

		cmd->result = DID_BUS_BUSY << 16;
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

	/* complete the request */
	srb->cc_stat = sts_entry->completionStatus;
	qla4xxx_srb_compl(ha, srb);
}

/**
 * qla4xxx_process_response_queue - process response queue completions
 * @ha: Pointer to host adapter structure.
 *
 * This routine process response queue completions in interrupt context.
 * Hardware_lock locked upon entry
 **/
static void qla4xxx_process_response_queue(struct scsi_qla_host * ha)
{
	uint32_t count = 0;
	struct srb *srb = NULL;
	struct status_entry *sts_entry;

	/* Process all responses from response queue */
	while ((ha->response_in =
		(uint16_t)le32_to_cpu(ha->shadow_regs->rsp_q_in)) !=
	       ha->response_out) {
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
			/*
			 * Common status - Single completion posted in single
			 * IOSB.
			 */
			qla4xxx_status_entry(ha, sts_entry);
			break;

		case ET_PASSTHRU_STATUS:
			break;

		case ET_STATUS_CONTINUATION:
			/* Just throw away the status continuation entries */
			DEBUG2(printk("scsi%ld: %s: Status Continuation entry "
				      "- ignoring\n", ha->host_no, __func__));
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
			qla4xxx_srb_compl(ha, srb);
			break;

		case ET_CONTINUE:
			/* Just throw away the continuation entries */
			DEBUG2(printk("scsi%ld: %s: Continuation entry - "
				      "ignoring\n", ha->host_no, __func__));
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
	}

	/*
	 * Done with responses, update the ISP For QLA4010, this also clears
	 * the interrupt.
	 */
	writel(ha->response_out, &ha->reg->rsp_q_out);
	readl(&ha->reg->rsp_q_out);

	return;

exit_prq_invalid_handle:
	DEBUG2(printk("scsi%ld: %s: Invalid handle(srb)=%p type=%x IOCS=%x\n",
		      ha->host_no, __func__, srb, sts_entry->hdr.entryType,
		      sts_entry->completionStatus));

exit_prq_error:
	writel(ha->response_out, &ha->reg->rsp_q_out);
	readl(&ha->reg->rsp_q_out);

	set_bit(DPC_RESET_HA, &ha->dpc_flags);
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
	uint32_t mbox_stat2, mbox_stat3;

	if ((mbox_status == MBOX_STS_BUSY) ||
	    (mbox_status == MBOX_STS_INTERMEDIATE_COMPLETION) ||
	    (mbox_status >> 12 == MBOX_COMPLETION_STATUS)) {
		ha->mbox_status[0] = mbox_status;

		if (test_bit(AF_MBOX_COMMAND, &ha->flags)) {
			/*
			 * Copy all mailbox registers to a temporary
			 * location and set mailbox command done flag
			 */
			for (i = 1; i < ha->mbox_status_count; i++)
				ha->mbox_status[i] =
					readl(&ha->reg->mailbox[i]);

			set_bit(AF_MBOX_COMMAND_DONE, &ha->flags);
		}
	} else if (mbox_status >> 12 == MBOX_ASYNC_EVENT_STATUS) {
		/* Immediately process the AENs that don't require much work.
		 * Only queue the database_changed AENs */
		if (ha->aen_log.count < MAX_AEN_ENTRIES) {
			for (i = 0; i < MBOX_AEN_REG_COUNT; i++)
				ha->aen_log.entry[ha->aen_log.count].mbox_sts[i] =
					readl(&ha->reg->mailbox[i]);
			ha->aen_log.count++;
		}
		switch (mbox_status) {
		case MBOX_ASTS_SYSTEM_ERROR:
			/* Log Mailbox registers */
			if (ql4xdontresethba) {
				DEBUG2(printk("%s:Dont Reset HBA\n",
					      __func__));
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
			set_bit(DPC_RESET_HA, &ha->dpc_flags);
			break;

		case MBOX_ASTS_LINK_UP:
			DEBUG2(printk("scsi%ld: AEN %04x Adapter LINK UP\n",
				      ha->host_no, mbox_status));
			set_bit(AF_LINK_UP, &ha->flags);
			break;

		case MBOX_ASTS_LINK_DOWN:
			DEBUG2(printk("scsi%ld: AEN %04x Adapter LINK DOWN\n",
				      ha->host_no, mbox_status));
			clear_bit(AF_LINK_UP, &ha->flags);
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
			/* No action */
			DEBUG2(printk("scsi%ld: AEN %04x\n", ha->host_no,
				      mbox_status));
			break;

		case MBOX_ASTS_IP_ADDR_STATE_CHANGED:
			mbox_stat2 = readl(&ha->reg->mailbox[2]);
			mbox_stat3 = readl(&ha->reg->mailbox[3]);

			if ((mbox_stat3 == 5) && (mbox_stat2 == 3))
				set_bit(DPC_GET_DHCP_IP_ADDR, &ha->dpc_flags);
			else if ((mbox_stat3 == 2) && (mbox_stat2 == 5))
				set_bit(DPC_RESET_HA, &ha->dpc_flags);
			break;

		case MBOX_ASTS_MAC_ADDRESS_CHANGED:
		case MBOX_ASTS_DNS:
			/* No action */
			DEBUG2(printk(KERN_INFO "scsi%ld: AEN %04x, "
				      "mbox_sts[1]=%04x, mbox_sts[2]=%04x\n",
				      ha->host_no, mbox_status,
				      readl(&ha->reg->mailbox[1]),
				      readl(&ha->reg->mailbox[2])));
			break;

		case MBOX_ASTS_SELF_TEST_FAILED:
		case MBOX_ASTS_LOGIN_FAILED:
			/* No action */
			DEBUG2(printk("scsi%ld: AEN %04x, mbox_sts[1]=%04x, "
				      "mbox_sts[2]=%04x, mbox_sts[3]=%04x\n",
				      ha->host_no, mbox_status,
				      readl(&ha->reg->mailbox[1]),
				      readl(&ha->reg->mailbox[2]),
				      readl(&ha->reg->mailbox[3])));
			break;

		case MBOX_ASTS_DATABASE_CHANGED:
			/* Queue AEN information and process it in the DPC
			 * routine */
			if (ha->aen_q_count > 0) {

				/* decrement available counter */
				ha->aen_q_count--;

				for (i = 1; i < MBOX_AEN_REG_COUNT; i++)
					ha->aen_q[ha->aen_in].mbox_sts[i] =
						readl(&ha->reg->mailbox[i]);

				ha->aen_q[ha->aen_in].mbox_sts[0] = mbox_status;

				/* print debug message */
				DEBUG2(printk("scsi%ld: AEN[%d] %04x queued"
					      " mb1:0x%x mb2:0x%x mb3:0x%x mb4:0x%x\n",
					      ha->host_no, ha->aen_in,
					      mbox_status,
					      ha->aen_q[ha->aen_in].mbox_sts[1],
					      ha->aen_q[ha->aen_in].mbox_sts[2],
					      ha->aen_q[ha->aen_in].mbox_sts[3],
					      ha->aen_q[ha->aen_in].  mbox_sts[4]));
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
					      mbox_status));

				DEBUG2(printk("scsi%ld: DUMP AEN QUEUE\n",
					      ha->host_no));

				for (i = 0; i < MAX_AEN_ENTRIES; i++) {
					DEBUG2(printk("AEN[%d] %04x %04x %04x "
						      "%04x\n", i,
						      ha->aen_q[i].mbox_sts[0],
						      ha->aen_q[i].mbox_sts[1],
						      ha->aen_q[i].mbox_sts[2],
						      ha->aen_q[i].mbox_sts[3]));
				}
			}
			break;

		default:
			DEBUG2(printk(KERN_WARNING
				      "scsi%ld: AEN %04x UNKNOWN\n",
				      ha->host_no, mbox_status));
			break;
		}
	} else {
		DEBUG2(printk("scsi%ld: Unknown mailbox status %08X\n",
			      ha->host_no, mbox_status));

		ha->mbox_status[0] = mbox_status;
	}
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
		if (le32_to_cpu(ha->shadow_regs->rsp_q_in) !=
		    ha->response_out)
			intr_status = CSR_SCSI_COMPLETION_INTR;
		else
			intr_status = readl(&ha->reg->ctrl_status);

		if ((intr_status &
		     (CSR_SCSI_RESET_INTR|CSR_FATAL_ERROR|INTR_PENDING)) ==
		    0) {
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

			if (!ql4_mod_unload)
				set_bit(DPC_RESET_HA_INTR, &ha->dpc_flags);

			break;
		} else if (intr_status & INTR_PENDING) {
			qla4xxx_interrupt_service_routine(ha, intr_status);
			ha->total_io_count++;
			if (++reqs_count == MAX_REQS_SERVICED_PER_INTR)
				break;

			intr_status = 0;
		}
	}

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
			if (process_aen == FLUSH_DDB_CHANGED_AENS) {
				DEBUG2(printk("scsi%ld: AEN[%d] %04x, index "
					      "[%d] state=%04x FLUSHED!\n",
					      ha->host_no, ha->aen_out,
					      mbox_sts[0], mbox_sts[2],
					      mbox_sts[3]));
				break;
			} else if (process_aen == RELOGIN_DDB_CHANGED_AENS) {
				/* for use during init time, we only want to
				 * relogin non-active ddbs */
				struct ddb_entry *ddb_entry;

				ddb_entry =
					/* FIXME: name length? */
					qla4xxx_lookup_ddb_by_fw_index(ha,
								       mbox_sts[2]);
				if (!ddb_entry)
					break;

				ddb_entry->dev_scan_wait_to_complete_relogin =
					0;
				ddb_entry->dev_scan_wait_to_start_relogin =
					jiffies +
					((ddb_entry->default_time2wait +
					  4) * HZ);

				DEBUG2(printk("scsi%ld: ddb index [%d] initate"
					      " RELOGIN after %d seconds\n",
					      ha->host_no,
					      ddb_entry->fw_ddb_index,
					      ddb_entry->default_time2wait +
					      4));
				break;
			}

			if (mbox_sts[1] == 0) {	/* Global DB change. */
				qla4xxx_reinitialize_ddb_list(ha);
			} else if (mbox_sts[1] == 1) {	/* Specific device. */
				qla4xxx_process_ddb_changed(ha, mbox_sts[2],
							    mbox_sts[3]);
			}
			break;
		}
		spin_lock_irqsave(&ha->hardware_lock, flags);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

