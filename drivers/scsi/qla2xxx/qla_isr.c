/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/delay.h>
#include <linux/slab.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_bsg_fc.h>
#include <scsi/scsi_eh.h>

static void qla2x00_mbx_completion(scsi_qla_host_t *, uint16_t);
static void qla2x00_process_completed_request(struct scsi_qla_host *,
	struct req_que *, uint32_t);
static void qla2x00_status_entry(scsi_qla_host_t *, struct rsp_que *, void *);
static void qla2x00_status_cont_entry(struct rsp_que *, sts_cont_entry_t *);
static void qla2x00_error_entry(scsi_qla_host_t *, struct rsp_que *,
	sts_entry_t *);

/**
 * qla2100_intr_handler() - Process interrupts for the ISP2100 and ISP2200.
 * @irq:
 * @dev_id: SCSI driver HA context
 *
 * Called by system whenever the host adapter generates an interrupt.
 *
 * Returns handled flag.
 */
irqreturn_t
qla2100_intr_handler(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct device_reg_2xxx __iomem *reg;
	int		status;
	unsigned long	iter;
	uint16_t	hccr;
	uint16_t	mb[4];
	struct rsp_que *rsp;
	unsigned long	flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
		    "%s(): NULL response queue pointer.\n", __func__);
		return (IRQ_NONE);
	}

	ha = rsp->hw;
	reg = &ha->iobase->isp;
	status = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	for (iter = 50; iter--; ) {
		hccr = RD_REG_WORD(&reg->hccr);
		if (hccr & HCCR_RISC_PAUSE) {
			if (pci_channel_offline(ha->pdev))
				break;

			/*
			 * Issue a "HARD" reset in order for the RISC interrupt
			 * bit to be cleared.  Schedule a big hammer to get
			 * out of the RISC PAUSED state.
			 */
			WRT_REG_WORD(&reg->hccr, HCCR_RESET_RISC);
			RD_REG_WORD(&reg->hccr);

			ha->isp_ops->fw_dump(vha, 1);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			break;
		} else if ((RD_REG_WORD(&reg->istatus) & ISR_RISC_INT) == 0)
			break;

		if (RD_REG_WORD(&reg->semaphore) & BIT_0) {
			WRT_REG_WORD(&reg->hccr, HCCR_CLR_RISC_INT);
			RD_REG_WORD(&reg->hccr);

			/* Get mailbox data. */
			mb[0] = RD_MAILBOX_REG(ha, reg, 0);
			if (mb[0] > 0x3fff && mb[0] < 0x8000) {
				qla2x00_mbx_completion(vha, mb[0]);
				status |= MBX_INTERRUPT;
			} else if (mb[0] > 0x7fff && mb[0] < 0xc000) {
				mb[1] = RD_MAILBOX_REG(ha, reg, 1);
				mb[2] = RD_MAILBOX_REG(ha, reg, 2);
				mb[3] = RD_MAILBOX_REG(ha, reg, 3);
				qla2x00_async_event(vha, rsp, mb);
			} else {
				/*EMPTY*/
				ql_dbg(ql_dbg_async, vha, 0x5025,
				    "Unrecognized interrupt type (%d).\n",
				    mb[0]);
			}
			/* Release mailbox registers. */
			WRT_REG_WORD(&reg->semaphore, 0);
			RD_REG_WORD(&reg->semaphore);
		} else {
			qla2x00_process_response_queue(rsp);

			WRT_REG_WORD(&reg->hccr, HCCR_CLR_RISC_INT);
			RD_REG_WORD(&reg->hccr);
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}

	return (IRQ_HANDLED);
}

/**
 * qla2300_intr_handler() - Process interrupts for the ISP23xx and ISP63xx.
 * @irq:
 * @dev_id: SCSI driver HA context
 *
 * Called by system whenever the host adapter generates an interrupt.
 *
 * Returns handled flag.
 */
irqreturn_t
qla2300_intr_handler(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct device_reg_2xxx __iomem *reg;
	int		status;
	unsigned long	iter;
	uint32_t	stat;
	uint16_t	hccr;
	uint16_t	mb[4];
	struct rsp_que *rsp;
	struct qla_hw_data *ha;
	unsigned long	flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
		    "%s(): NULL response queue pointer.\n", __func__);
		return (IRQ_NONE);
	}

	ha = rsp->hw;
	reg = &ha->iobase->isp;
	status = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	for (iter = 50; iter--; ) {
		stat = RD_REG_DWORD(&reg->u.isp2300.host_status);
		if (stat & HSR_RISC_PAUSED) {
			if (unlikely(pci_channel_offline(ha->pdev)))
				break;

			hccr = RD_REG_WORD(&reg->hccr);
			if (hccr & (BIT_15 | BIT_13 | BIT_11 | BIT_8))
				ql_log(ql_log_warn, vha, 0x5026,
				    "Parity error -- HCCR=%x, Dumping "
				    "firmware.\n", hccr);
			else
				ql_log(ql_log_warn, vha, 0x5027,
				    "RISC paused -- HCCR=%x, Dumping "
				    "firmware.\n", hccr);

			/*
			 * Issue a "HARD" reset in order for the RISC
			 * interrupt bit to be cleared.  Schedule a big
			 * hammer to get out of the RISC PAUSED state.
			 */
			WRT_REG_WORD(&reg->hccr, HCCR_RESET_RISC);
			RD_REG_WORD(&reg->hccr);

			ha->isp_ops->fw_dump(vha, 1);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			break;
		} else if ((stat & HSR_RISC_INT) == 0)
			break;

		switch (stat & 0xff) {
		case 0x1:
		case 0x2:
		case 0x10:
		case 0x11:
			qla2x00_mbx_completion(vha, MSW(stat));
			status |= MBX_INTERRUPT;

			/* Release mailbox registers. */
			WRT_REG_WORD(&reg->semaphore, 0);
			break;
		case 0x12:
			mb[0] = MSW(stat);
			mb[1] = RD_MAILBOX_REG(ha, reg, 1);
			mb[2] = RD_MAILBOX_REG(ha, reg, 2);
			mb[3] = RD_MAILBOX_REG(ha, reg, 3);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case 0x13:
			qla2x00_process_response_queue(rsp);
			break;
		case 0x15:
			mb[0] = MBA_CMPLT_1_16BIT;
			mb[1] = MSW(stat);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case 0x16:
			mb[0] = MBA_SCSI_COMPLETION;
			mb[1] = MSW(stat);
			mb[2] = RD_MAILBOX_REG(ha, reg, 2);
			qla2x00_async_event(vha, rsp, mb);
			break;
		default:
			ql_dbg(ql_dbg_async, vha, 0x5028,
			    "Unrecognized interrupt type (%d).\n", stat & 0xff);
			break;
		}
		WRT_REG_WORD(&reg->hccr, HCCR_CLR_RISC_INT);
		RD_REG_WORD_RELAXED(&reg->hccr);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}

	return (IRQ_HANDLED);
}

/**
 * qla2x00_mbx_completion() - Process mailbox command completions.
 * @ha: SCSI driver HA context
 * @mb0: Mailbox0 register
 */
static void
qla2x00_mbx_completion(scsi_qla_host_t *vha, uint16_t mb0)
{
	uint16_t	cnt;
	uint16_t __iomem *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Load return mailbox registers. */
	ha->flags.mbox_int = 1;
	ha->mailbox_out[0] = mb0;
	wptr = (uint16_t __iomem *)MAILBOX_REG(ha, reg, 1);

	for (cnt = 1; cnt < ha->mbx_count; cnt++) {
		if (IS_QLA2200(ha) && cnt == 8)
			wptr = (uint16_t __iomem *)MAILBOX_REG(ha, reg, 8);
		if (cnt == 4 || cnt == 5)
			ha->mailbox_out[cnt] = qla2x00_debounce_register(wptr);
		else
			ha->mailbox_out[cnt] = RD_REG_WORD(wptr);

		wptr++;
	}

	if (ha->mcp) {
		ql_dbg(ql_dbg_async, vha, 0x5000,
		    "Got mbx completion. cmd=%x.\n", ha->mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_async, vha, 0x5001,
		    "MBX pointer ERROR.\n");
	}
}

static void
qla81xx_idc_event(scsi_qla_host_t *vha, uint16_t aen, uint16_t descr)
{
	static char *event[] =
		{ "Complete", "Request Notification", "Time Extension" };
	int rval;
	struct device_reg_24xx __iomem *reg24 = &vha->hw->iobase->isp24;
	uint16_t __iomem *wptr;
	uint16_t cnt, timeout, mb[QLA_IDC_ACK_REGS];

	/* Seed data -- mailbox1 -> mailbox7. */
	wptr = (uint16_t __iomem *)&reg24->mailbox1;
	for (cnt = 0; cnt < QLA_IDC_ACK_REGS; cnt++, wptr++)
		mb[cnt] = RD_REG_WORD(wptr);

	ql_dbg(ql_dbg_async, vha, 0x5021,
	    "Inter-Driver Commucation %s -- "
	    "%04x %04x %04x %04x %04x %04x %04x.\n",
	    event[aen & 0xff], mb[0], mb[1], mb[2], mb[3],
	    mb[4], mb[5], mb[6]);

	/* Acknowledgement needed? [Notify && non-zero timeout]. */
	timeout = (descr >> 8) & 0xf;
	if (aen != MBA_IDC_NOTIFY || !timeout)
		return;

	ql_dbg(ql_dbg_async, vha, 0x5022,
	    "Inter-Driver Commucation %s -- ACK timeout=%d.\n",
	    vha->host_no, event[aen & 0xff], timeout);

	rval = qla2x00_post_idc_ack_work(vha, mb);
	if (rval != QLA_SUCCESS)
		ql_log(ql_log_warn, vha, 0x5023,
		    "IDC failed to post ACK.\n");
}

/**
 * qla2x00_async_event() - Process aynchronous events.
 * @ha: SCSI driver HA context
 * @mb: Mailbox registers (0 - 3)
 */
void
qla2x00_async_event(scsi_qla_host_t *vha, struct rsp_que *rsp, uint16_t *mb)
{
#define LS_UNKNOWN	2
	static char	*link_speeds[] = { "1", "2", "?", "4", "8", "10" };
	char		*link_speed;
	uint16_t	handle_cnt;
	uint16_t	cnt, mbx;
	uint32_t	handles[5];
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	struct device_reg_24xx __iomem *reg24 = &ha->iobase->isp24;
	struct device_reg_82xx __iomem *reg82 = &ha->iobase->isp82;
	uint32_t	rscn_entry, host_pid;
	uint8_t		rscn_queue_index;
	unsigned long	flags;

	/* Setup to process RIO completion. */
	handle_cnt = 0;
	if (IS_QLA8XXX_TYPE(ha))
		goto skip_rio;
	switch (mb[0]) {
	case MBA_SCSI_COMPLETION:
		handles[0] = le32_to_cpu((uint32_t)((mb[2] << 16) | mb[1]));
		handle_cnt = 1;
		break;
	case MBA_CMPLT_1_16BIT:
		handles[0] = mb[1];
		handle_cnt = 1;
		mb[0] = MBA_SCSI_COMPLETION;
		break;
	case MBA_CMPLT_2_16BIT:
		handles[0] = mb[1];
		handles[1] = mb[2];
		handle_cnt = 2;
		mb[0] = MBA_SCSI_COMPLETION;
		break;
	case MBA_CMPLT_3_16BIT:
		handles[0] = mb[1];
		handles[1] = mb[2];
		handles[2] = mb[3];
		handle_cnt = 3;
		mb[0] = MBA_SCSI_COMPLETION;
		break;
	case MBA_CMPLT_4_16BIT:
		handles[0] = mb[1];
		handles[1] = mb[2];
		handles[2] = mb[3];
		handles[3] = (uint32_t)RD_MAILBOX_REG(ha, reg, 6);
		handle_cnt = 4;
		mb[0] = MBA_SCSI_COMPLETION;
		break;
	case MBA_CMPLT_5_16BIT:
		handles[0] = mb[1];
		handles[1] = mb[2];
		handles[2] = mb[3];
		handles[3] = (uint32_t)RD_MAILBOX_REG(ha, reg, 6);
		handles[4] = (uint32_t)RD_MAILBOX_REG(ha, reg, 7);
		handle_cnt = 5;
		mb[0] = MBA_SCSI_COMPLETION;
		break;
	case MBA_CMPLT_2_32BIT:
		handles[0] = le32_to_cpu((uint32_t)((mb[2] << 16) | mb[1]));
		handles[1] = le32_to_cpu(
		    ((uint32_t)(RD_MAILBOX_REG(ha, reg, 7) << 16)) |
		    RD_MAILBOX_REG(ha, reg, 6));
		handle_cnt = 2;
		mb[0] = MBA_SCSI_COMPLETION;
		break;
	default:
		break;
	}
skip_rio:
	switch (mb[0]) {
	case MBA_SCSI_COMPLETION:	/* Fast Post */
		if (!vha->flags.online)
			break;

		for (cnt = 0; cnt < handle_cnt; cnt++)
			qla2x00_process_completed_request(vha, rsp->req,
				handles[cnt]);
		break;

	case MBA_RESET:			/* Reset */
		ql_dbg(ql_dbg_async, vha, 0x5002,
		    "Asynchronous RESET.\n");

		set_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);
		break;

	case MBA_SYSTEM_ERR:		/* System Error */
		mbx = IS_QLA81XX(ha) ? RD_REG_WORD(&reg24->mailbox7) : 0;
		ql_log(ql_log_warn, vha, 0x5003,
		    "ISP System Error - mbx1=%xh mbx2=%xh mbx3=%xh "
		    "mbx7=%xh.\n", mb[1], mb[2], mb[3], mbx);

		ha->isp_ops->fw_dump(vha, 1);

		if (IS_FWI2_CAPABLE(ha)) {
			if (mb[1] == 0 && mb[2] == 0) {
				ql_log(ql_log_fatal, vha, 0x5004,
				    "Unrecoverable Hardware Error: adapter "
				    "marked OFFLINE!\n");
				vha->flags.online = 0;
			} else {
				/* Check to see if MPI timeout occurred */
				if ((mbx & MBX_3) && (ha->flags.port0))
					set_bit(MPI_RESET_NEEDED,
					    &vha->dpc_flags);

				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			}
		} else if (mb[1] == 0) {
			ql_log(ql_log_fatal, vha, 0x5005,
			    "Unrecoverable Hardware Error: adapter marked "
			    "OFFLINE!\n");
			vha->flags.online = 0;
		} else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;

	case MBA_REQ_TRANSFER_ERR:	/* Request Transfer Error */
		ql_log(ql_log_warn, vha, 0x5006,
		    "ISP Request Transfer Error (%x).\n",  mb[1]);

		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;

	case MBA_RSP_TRANSFER_ERR:	/* Response Transfer Error */
		ql_log(ql_log_warn, vha, 0x5007,
		    "ISP Response Transfer Error.\n");

		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;

	case MBA_WAKEUP_THRES:		/* Request Queue Wake-up */
		ql_dbg(ql_dbg_async, vha, 0x5008,
		    "Asynchronous WAKEUP_THRES.\n");
		break;

	case MBA_LIP_OCCURRED:		/* Loop Initialization Procedure */
		ql_log(ql_log_info, vha, 0x5009,
		    "LIP occurred (%x).\n", mb[1]);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
			qla2x00_mark_all_devices_lost(vha, 1);
		}

		if (vha->vp_idx) {
			atomic_set(&vha->vp_state, VP_FAILED);
			fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		}

		set_bit(REGISTER_FC4_NEEDED, &vha->dpc_flags);
		set_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags);

		vha->flags.management_server_logged_in = 0;
		qla2x00_post_aen_work(vha, FCH_EVT_LIP, mb[1]);
		break;

	case MBA_LOOP_UP:		/* Loop Up Event */
		if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
			link_speed = link_speeds[0];
			ha->link_data_rate = PORT_SPEED_1GB;
		} else {
			link_speed = link_speeds[LS_UNKNOWN];
			if (mb[1] < 5)
				link_speed = link_speeds[mb[1]];
			else if (mb[1] == 0x13)
				link_speed = link_speeds[5];
			ha->link_data_rate = mb[1];
		}

		ql_log(ql_log_info, vha, 0x500a,
		    "LOOP UP detected (%s Gbps).\n", link_speed);

		vha->flags.management_server_logged_in = 0;
		qla2x00_post_aen_work(vha, FCH_EVT_LINKUP, ha->link_data_rate);
		break;

	case MBA_LOOP_DOWN:		/* Loop Down Event */
		mbx = IS_QLA81XX(ha) ? RD_REG_WORD(&reg24->mailbox4) : 0;
		mbx = IS_QLA82XX(ha) ? RD_REG_WORD(&reg82->mailbox_out[4]) : mbx;
		ql_log(ql_log_info, vha, 0x500b,
		    "LOOP DOWN detected (%x %x %x %x).\n",
		    mb[1], mb[2], mb[3], mbx);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
			vha->device_flags |= DFLG_NO_CABLE;
			qla2x00_mark_all_devices_lost(vha, 1);
		}

		if (vha->vp_idx) {
			atomic_set(&vha->vp_state, VP_FAILED);
			fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		}

		vha->flags.management_server_logged_in = 0;
		ha->link_data_rate = PORT_SPEED_UNKNOWN;
		qla2x00_post_aen_work(vha, FCH_EVT_LINKDOWN, 0);
		break;

	case MBA_LIP_RESET:		/* LIP reset occurred */
		ql_log(ql_log_info, vha, 0x500c,
		    "LIP reset occurred (%x).\n", mb[1]);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
			qla2x00_mark_all_devices_lost(vha, 1);
		}

		if (vha->vp_idx) {
			atomic_set(&vha->vp_state, VP_FAILED);
			fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		}

		set_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);

		ha->operating_mode = LOOP;
		vha->flags.management_server_logged_in = 0;
		qla2x00_post_aen_work(vha, FCH_EVT_LIPRESET, mb[1]);
		break;

	/* case MBA_DCBX_COMPLETE: */
	case MBA_POINT_TO_POINT:	/* Point-to-Point */
		if (IS_QLA2100(ha))
			break;

		if (IS_QLA8XXX_TYPE(ha)) {
			ql_dbg(ql_dbg_async, vha, 0x500d,
			    "DCBX Completed -- %04x %04x %04x.\n",
			    mb[1], mb[2], mb[3]);
			if (ha->notify_dcbx_comp)
				complete(&ha->dcbx_comp);

		} else
			ql_dbg(ql_dbg_async, vha, 0x500e,
			    "Asynchronous P2P MODE received.\n");

		/*
		 * Until there's a transition from loop down to loop up, treat
		 * this as loop down only.
		 */
		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			if (!atomic_read(&vha->loop_down_timer))
				atomic_set(&vha->loop_down_timer,
				    LOOP_DOWN_TIME);
			qla2x00_mark_all_devices_lost(vha, 1);
		}

		if (vha->vp_idx) {
			atomic_set(&vha->vp_state, VP_FAILED);
			fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		}

		if (!(test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags)))
			set_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);

		set_bit(REGISTER_FC4_NEEDED, &vha->dpc_flags);
		set_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags);

		ha->flags.gpsc_supported = 1;
		vha->flags.management_server_logged_in = 0;
		break;

	case MBA_CHG_IN_CONNECTION:	/* Change in connection mode */
		if (IS_QLA2100(ha))
			break;

		ql_log(ql_log_info, vha, 0x500f,
		    "Configuration change detected: value=%x.\n", mb[1]);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			if (!atomic_read(&vha->loop_down_timer))
				atomic_set(&vha->loop_down_timer,
				    LOOP_DOWN_TIME);
			qla2x00_mark_all_devices_lost(vha, 1);
		}

		if (vha->vp_idx) {
			atomic_set(&vha->vp_state, VP_FAILED);
			fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		}

		set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
		set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
		break;

	case MBA_PORT_UPDATE:		/* Port database update */
		/*
		 * Handle only global and vn-port update events
		 *
		 * Relevant inputs:
		 * mb[1] = N_Port handle of changed port
		 * OR 0xffff for global event
		 * mb[2] = New login state
		 * 7 = Port logged out
		 * mb[3] = LSB is vp_idx, 0xff = all vps
		 *
		 * Skip processing if:
		 *       Event is global, vp_idx is NOT all vps,
		 *           vp_idx does not match
		 *       Event is not global, vp_idx does not match
		 */
		if (IS_QLA2XXX_MIDTYPE(ha) &&
		    ((mb[1] == 0xffff && (mb[3] & 0xff) != 0xff) ||
			(mb[1] != 0xffff)) && vha->vp_idx != (mb[3] & 0xff))
			break;

		/* Global event -- port logout or port unavailable. */
		if (mb[1] == 0xffff && mb[2] == 0x7) {
			ql_dbg(ql_dbg_async, vha, 0x5010,
			    "Port unavailable %04x %04x %04x.\n",
			    mb[1], mb[2], mb[3]);

			if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
				atomic_set(&vha->loop_state, LOOP_DOWN);
				atomic_set(&vha->loop_down_timer,
				    LOOP_DOWN_TIME);
				vha->device_flags |= DFLG_NO_CABLE;
				qla2x00_mark_all_devices_lost(vha, 1);
			}

			if (vha->vp_idx) {
				atomic_set(&vha->vp_state, VP_FAILED);
				fc_vport_set_state(vha->fc_vport,
				    FC_VPORT_FAILED);
				qla2x00_mark_all_devices_lost(vha, 1);
			}

			vha->flags.management_server_logged_in = 0;
			ha->link_data_rate = PORT_SPEED_UNKNOWN;
			break;
		}

		/*
		 * If PORT UPDATE is global (received LIP_OCCURRED/LIP_RESET
		 * event etc. earlier indicating loop is down) then process
		 * it.  Otherwise ignore it and Wait for RSCN to come in.
		 */
		atomic_set(&vha->loop_down_timer, 0);
		if (atomic_read(&vha->loop_state) != LOOP_DOWN &&
		    atomic_read(&vha->loop_state) != LOOP_DEAD) {
			ql_dbg(ql_dbg_async, vha, 0x5011,
			    "Asynchronous PORT UPDATE ignored %04x/%04x/%04x.\n",
			    mb[1], mb[2], mb[3]);
			break;
		}

		ql_dbg(ql_dbg_async, vha, 0x5012,
		    "Port database changed %04x %04x %04x.\n",
		    mb[1], mb[2], mb[3]);

		/*
		 * Mark all devices as missing so we will login again.
		 */
		atomic_set(&vha->loop_state, LOOP_UP);

		qla2x00_mark_all_devices_lost(vha, 1);

		vha->flags.rscn_queue_overflow = 1;

		set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
		set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
		break;

	case MBA_RSCN_UPDATE:		/* State Change Registration */
		/* Check if the Vport has issued a SCR */
		if (vha->vp_idx && test_bit(VP_SCR_NEEDED, &vha->vp_flags))
			break;
		/* Only handle SCNs for our Vport index. */
		if (ha->flags.npiv_supported && vha->vp_idx != (mb[3] & 0xff))
			break;

		ql_dbg(ql_dbg_async, vha, 0x5013,
		    "RSCN database changed -- %04x %04x %04x.\n",
		    mb[1], mb[2], mb[3]);

		rscn_entry = ((mb[1] & 0xff) << 16) | mb[2];
		host_pid = (vha->d_id.b.domain << 16) | (vha->d_id.b.area << 8)
				| vha->d_id.b.al_pa;
		if (rscn_entry == host_pid) {
			ql_dbg(ql_dbg_async, vha, 0x5014,
			    "Ignoring RSCN update to local host "
			    "port ID (%06x).\n", host_pid);
			break;
		}

		/* Ignore reserved bits from RSCN-payload. */
		rscn_entry = ((mb[1] & 0x3ff) << 16) | mb[2];
		rscn_queue_index = vha->rscn_in_ptr + 1;
		if (rscn_queue_index == MAX_RSCN_COUNT)
			rscn_queue_index = 0;
		if (rscn_queue_index != vha->rscn_out_ptr) {
			vha->rscn_queue[vha->rscn_in_ptr] = rscn_entry;
			vha->rscn_in_ptr = rscn_queue_index;
		} else {
			vha->flags.rscn_queue_overflow = 1;
		}

		atomic_set(&vha->loop_down_timer, 0);
		vha->flags.management_server_logged_in = 0;

		set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
		set_bit(RSCN_UPDATE, &vha->dpc_flags);
		qla2x00_post_aen_work(vha, FCH_EVT_RSCN, rscn_entry);
		break;

	/* case MBA_RIO_RESPONSE: */
	case MBA_ZIO_RESPONSE:
		ql_dbg(ql_dbg_async, vha, 0x5015,
		    "[R|Z]IO update completion.\n");

		if (IS_FWI2_CAPABLE(ha))
			qla24xx_process_response_queue(vha, rsp);
		else
			qla2x00_process_response_queue(rsp);
		break;

	case MBA_DISCARD_RND_FRAME:
		ql_dbg(ql_dbg_async, vha, 0x5016,
		    "Discard RND Frame -- %04x %04x %04x.\n",
		    mb[1], mb[2], mb[3]);
		break;

	case MBA_TRACE_NOTIFICATION:
		ql_dbg(ql_dbg_async, vha, 0x5017,
		    "Trace Notification -- %04x %04x.\n", mb[1], mb[2]);
		break;

	case MBA_ISP84XX_ALERT:
		ql_dbg(ql_dbg_async, vha, 0x5018,
		    "ISP84XX Alert Notification -- %04x %04x %04x.\n",
		    mb[1], mb[2], mb[3]);

		spin_lock_irqsave(&ha->cs84xx->access_lock, flags);
		switch (mb[1]) {
		case A84_PANIC_RECOVERY:
			ql_log(ql_log_info, vha, 0x5019,
			    "Alert 84XX: panic recovery %04x %04x.\n",
			    mb[2], mb[3]);
			break;
		case A84_OP_LOGIN_COMPLETE:
			ha->cs84xx->op_fw_version = mb[3] << 16 | mb[2];
			ql_log(ql_log_info, vha, 0x501a,
			    "Alert 84XX: firmware version %x.\n",
			    ha->cs84xx->op_fw_version);
			break;
		case A84_DIAG_LOGIN_COMPLETE:
			ha->cs84xx->diag_fw_version = mb[3] << 16 | mb[2];
			ql_log(ql_log_info, vha, 0x501b,
			    "Alert 84XX: diagnostic firmware version %x.\n",
			    ha->cs84xx->diag_fw_version);
			break;
		case A84_GOLD_LOGIN_COMPLETE:
			ha->cs84xx->diag_fw_version = mb[3] << 16 | mb[2];
			ha->cs84xx->fw_update = 1;
			ql_log(ql_log_info, vha, 0x501c,
			    "Alert 84XX: gold firmware version %x.\n",
			    ha->cs84xx->gold_fw_version);
			break;
		default:
			ql_log(ql_log_warn, vha, 0x501d,
			    "Alert 84xx: Invalid Alert %04x %04x %04x.\n",
			    mb[1], mb[2], mb[3]);
		}
		spin_unlock_irqrestore(&ha->cs84xx->access_lock, flags);
		break;
	case MBA_DCBX_START:
		ql_dbg(ql_dbg_async, vha, 0x501e,
		    "DCBX Started -- %04x %04x %04x.\n",
		    mb[1], mb[2], mb[3]);
		break;
	case MBA_DCBX_PARAM_UPDATE:
		ql_dbg(ql_dbg_async, vha, 0x501f,
		    "DCBX Parameters Updated -- %04x %04x %04x.\n",
		    mb[1], mb[2], mb[3]);
		break;
	case MBA_FCF_CONF_ERR:
		ql_dbg(ql_dbg_async, vha, 0x5020,
		    "FCF Configuration Error -- %04x %04x %04x.\n",
		    mb[1], mb[2], mb[3]);
		break;
	case MBA_IDC_COMPLETE:
	case MBA_IDC_NOTIFY:
	case MBA_IDC_TIME_EXT:
		qla81xx_idc_event(vha, mb[0], mb[1]);
		break;
	}

	if (!vha->vp_idx && ha->num_vhosts)
		qla2x00_alert_all_vps(rsp, mb);
}

/**
 * qla2x00_process_completed_request() - Process a Fast Post response.
 * @ha: SCSI driver HA context
 * @index: SRB index
 */
static void
qla2x00_process_completed_request(struct scsi_qla_host *vha,
				struct req_que *req, uint32_t index)
{
	srb_t *sp;
	struct qla_hw_data *ha = vha->hw;

	/* Validate handle. */
	if (index >= MAX_OUTSTANDING_COMMANDS) {
		ql_log(ql_log_warn, vha, 0x3014,
		    "Invalid SCSI command index (%x).\n", index);

		if (IS_QLA82XX(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		return;
	}

	sp = req->outstanding_cmds[index];
	if (sp) {
		/* Free outstanding command slot. */
		req->outstanding_cmds[index] = NULL;

		/* Save ISP completion status */
		sp->cmd->result = DID_OK << 16;
		qla2x00_sp_compl(ha, sp);
	} else {
		ql_log(ql_log_warn, vha, 0x3016, "Invalid SCSI SRB.\n");

		if (IS_QLA82XX(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	}
}

static srb_t *
qla2x00_get_sp_from_handle(scsi_qla_host_t *vha, const char *func,
    struct req_que *req, void *iocb)
{
	struct qla_hw_data *ha = vha->hw;
	sts_entry_t *pkt = iocb;
	srb_t *sp = NULL;
	uint16_t index;

	index = LSW(pkt->handle);
	if (index >= MAX_OUTSTANDING_COMMANDS) {
		ql_log(ql_log_warn, vha, 0x5031,
		    "Invalid command index (%x).\n", index);
		if (IS_QLA82XX(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		goto done;
	}
	sp = req->outstanding_cmds[index];
	if (!sp) {
		ql_log(ql_log_warn, vha, 0x5032,
		    "Invalid completion handle (%x) -- timed-out.\n", index);
		return sp;
	}
	if (sp->handle != index) {
		ql_log(ql_log_warn, vha, 0x5033,
		    "SRB handle (%x) mismatch %x.\n", sp->handle, index);
		return NULL;
	}

	req->outstanding_cmds[index] = NULL;

done:
	return sp;
}

static void
qla2x00_mbx_iocb_entry(scsi_qla_host_t *vha, struct req_que *req,
    struct mbx_entry *mbx)
{
	const char func[] = "MBX-IOCB";
	const char *type;
	fc_port_t *fcport;
	srb_t *sp;
	struct srb_iocb *lio;
	struct srb_ctx *ctx;
	uint16_t *data;
	uint16_t status;

	sp = qla2x00_get_sp_from_handle(vha, func, req, mbx);
	if (!sp)
		return;

	ctx = sp->ctx;
	lio = ctx->u.iocb_cmd;
	type = ctx->name;
	fcport = sp->fcport;
	data = lio->u.logio.data;

	data[0] = MBS_COMMAND_ERROR;
	data[1] = lio->u.logio.flags & SRB_LOGIN_RETRIED ?
	    QLA_LOGIO_LOGIN_RETRIED : 0;
	if (mbx->entry_status) {
		ql_dbg(ql_dbg_async, vha, 0x5043,
		    "Async-%s error entry - portid=%02x%02x%02x "
		    "entry-status=%x status=%x state-flag=%x "
		    "status-flags=%x.\n",
		    type, fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, mbx->entry_status,
		    le16_to_cpu(mbx->status), le16_to_cpu(mbx->state_flags),
		    le16_to_cpu(mbx->status_flags));

		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x5057,
		    (uint8_t *)mbx, sizeof(*mbx));

		goto logio_done;
	}

	status = le16_to_cpu(mbx->status);
	if (status == 0x30 && ctx->type == SRB_LOGIN_CMD &&
	    le16_to_cpu(mbx->mb0) == MBS_COMMAND_COMPLETE)
		status = 0;
	if (!status && le16_to_cpu(mbx->mb0) == MBS_COMMAND_COMPLETE) {
		ql_dbg(ql_dbg_async, vha, 0x5045,
		    "Async-%s complete - portid=%02x%02x%02x mbx1=%x.\n",
		    type, fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, le16_to_cpu(mbx->mb1));

		data[0] = MBS_COMMAND_COMPLETE;
		if (ctx->type == SRB_LOGIN_CMD) {
			fcport->port_type = FCT_TARGET;
			if (le16_to_cpu(mbx->mb1) & BIT_0)
				fcport->port_type = FCT_INITIATOR;
			else if (le16_to_cpu(mbx->mb1) & BIT_1)
				fcport->flags |= FCF_FCP2_DEVICE;
		}
		goto logio_done;
	}

	data[0] = le16_to_cpu(mbx->mb0);
	switch (data[0]) {
	case MBS_PORT_ID_USED:
		data[1] = le16_to_cpu(mbx->mb1);
		break;
	case MBS_LOOP_ID_USED:
		break;
	default:
		data[0] = MBS_COMMAND_ERROR;
		break;
	}

	ql_log(ql_log_warn, vha, 0x5046,
	    "Async-%s failed - portid=%02x%02x%02x status=%x "
	    "mb0=%x mb1=%x mb2=%x mb6=%x mb7=%x.\n",
	    type, fcport->d_id.b.domain,
	    fcport->d_id.b.area, fcport->d_id.b.al_pa, status,
	    le16_to_cpu(mbx->mb0), le16_to_cpu(mbx->mb1),
	    le16_to_cpu(mbx->mb2), le16_to_cpu(mbx->mb6),
	    le16_to_cpu(mbx->mb7));

logio_done:
	lio->done(sp);
}

static void
qla2x00_ct_entry(scsi_qla_host_t *vha, struct req_que *req,
    sts_entry_t *pkt, int iocb_type)
{
	const char func[] = "CT_IOCB";
	const char *type;
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	struct srb_ctx *sp_bsg;
	struct fc_bsg_job *bsg_job;
	uint16_t comp_status;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;

	sp_bsg = sp->ctx;
	bsg_job = sp_bsg->u.bsg_job;

	type = NULL;
	switch (sp_bsg->type) {
	case SRB_CT_CMD:
		type = "ct pass-through";
		break;
	default:
		ql_log(ql_log_warn, vha, 0x5047,
		    "Unrecognized SRB: (%p) type=%d.\n", sp, sp_bsg->type);
		return;
	}

	comp_status = le16_to_cpu(pkt->comp_status);

	/* return FC_CTELS_STATUS_OK and leave the decoding of the ELS/CT
	 * fc payload  to the caller
	 */
	bsg_job->reply->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);

	if (comp_status != CS_COMPLETE) {
		if (comp_status == CS_DATA_UNDERRUN) {
			bsg_job->reply->result = DID_OK << 16;
			bsg_job->reply->reply_payload_rcv_len =
			    le16_to_cpu(((sts_entry_t *)pkt)->rsp_info_len);

			ql_log(ql_log_warn, vha, 0x5048,
			    "CT pass-through-%s error "
			    "comp_status-status=0x%x total_byte = 0x%x.\n",
			    type, comp_status,
			    bsg_job->reply->reply_payload_rcv_len);
		} else {
			ql_log(ql_log_warn, vha, 0x5049,
			    "CT pass-through-%s error "
			    "comp_status-status=0x%x.\n", type, comp_status);
			bsg_job->reply->result = DID_ERROR << 16;
			bsg_job->reply->reply_payload_rcv_len = 0;
		}
		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x5058,
		    (uint8_t *)pkt, sizeof(*pkt));
	} else {
		bsg_job->reply->result =  DID_OK << 16;
		bsg_job->reply->reply_payload_rcv_len =
		    bsg_job->reply_payload.payload_len;
		bsg_job->reply_len = 0;
	}

	dma_unmap_sg(&ha->pdev->dev, bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);

	dma_unmap_sg(&ha->pdev->dev, bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

	if (sp_bsg->type == SRB_ELS_CMD_HST || sp_bsg->type == SRB_CT_CMD)
		kfree(sp->fcport);

	kfree(sp->ctx);
	mempool_free(sp, ha->srb_mempool);
	bsg_job->job_done(bsg_job);
}

static void
qla24xx_els_ct_entry(scsi_qla_host_t *vha, struct req_que *req,
    struct sts_entry_24xx *pkt, int iocb_type)
{
	const char func[] = "ELS_CT_IOCB";
	const char *type;
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	struct srb_ctx *sp_bsg;
	struct fc_bsg_job *bsg_job;
	uint16_t comp_status;
	uint32_t fw_status[3];
	uint8_t* fw_sts_ptr;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;
	sp_bsg = sp->ctx;
	bsg_job = sp_bsg->u.bsg_job;

	type = NULL;
	switch (sp_bsg->type) {
	case SRB_ELS_CMD_RPT:
	case SRB_ELS_CMD_HST:
		type = "els";
		break;
	case SRB_CT_CMD:
		type = "ct pass-through";
		break;
	default:
		ql_log(ql_log_warn, vha, 0x503e,
		    "Unrecognized SRB: (%p) type=%d.\n", sp, sp_bsg->type);
		return;
	}

	comp_status = fw_status[0] = le16_to_cpu(pkt->comp_status);
	fw_status[1] = le16_to_cpu(((struct els_sts_entry_24xx*)pkt)->error_subcode_1);
	fw_status[2] = le16_to_cpu(((struct els_sts_entry_24xx*)pkt)->error_subcode_2);

	/* return FC_CTELS_STATUS_OK and leave the decoding of the ELS/CT
	 * fc payload  to the caller
	 */
	bsg_job->reply->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply) + sizeof(fw_status);

	if (comp_status != CS_COMPLETE) {
		if (comp_status == CS_DATA_UNDERRUN) {
			bsg_job->reply->result = DID_OK << 16;
			bsg_job->reply->reply_payload_rcv_len =
				le16_to_cpu(((struct els_sts_entry_24xx*)pkt)->total_byte_count);

			ql_log(ql_log_info, vha, 0x503f,
			    "ELS-CT pass-through-%s error comp_status-status=0x%x "
			    "error subcode 1=0x%x error subcode 2=0x%x total_byte = 0x%x.\n",
			    type, comp_status, fw_status[1], fw_status[2],
			    le16_to_cpu(((struct els_sts_entry_24xx *)
				pkt)->total_byte_count));
			fw_sts_ptr = ((uint8_t*)bsg_job->req->sense) + sizeof(struct fc_bsg_reply);
			memcpy( fw_sts_ptr, fw_status, sizeof(fw_status));
		}
		else {
			ql_log(ql_log_info, vha, 0x5040,
			    "ELS-CT pass-through-%s error comp_status-status=0x%x "
			    "error subcode 1=0x%x error subcode 2=0x%x.\n",
			    type, comp_status,
			    le16_to_cpu(((struct els_sts_entry_24xx *)
				pkt)->error_subcode_1),
			    le16_to_cpu(((struct els_sts_entry_24xx *)
				    pkt)->error_subcode_2));
			bsg_job->reply->result = DID_ERROR << 16;
			bsg_job->reply->reply_payload_rcv_len = 0;
			fw_sts_ptr = ((uint8_t*)bsg_job->req->sense) + sizeof(struct fc_bsg_reply);
			memcpy( fw_sts_ptr, fw_status, sizeof(fw_status));
		}
		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x5056,
				(uint8_t *)pkt, sizeof(*pkt));
	}
	else {
		bsg_job->reply->result =  DID_OK << 16;
		bsg_job->reply->reply_payload_rcv_len = bsg_job->reply_payload.payload_len;
		bsg_job->reply_len = 0;
	}

	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->request_payload.sg_list,
	    bsg_job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(&ha->pdev->dev,
	    bsg_job->reply_payload.sg_list,
	    bsg_job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	if ((sp_bsg->type == SRB_ELS_CMD_HST) ||
	    (sp_bsg->type == SRB_CT_CMD))
		kfree(sp->fcport);
	kfree(sp->ctx);
	mempool_free(sp, ha->srb_mempool);
	bsg_job->job_done(bsg_job);
}

static void
qla24xx_logio_entry(scsi_qla_host_t *vha, struct req_que *req,
    struct logio_entry_24xx *logio)
{
	const char func[] = "LOGIO-IOCB";
	const char *type;
	fc_port_t *fcport;
	srb_t *sp;
	struct srb_iocb *lio;
	struct srb_ctx *ctx;
	uint16_t *data;
	uint32_t iop[2];

	sp = qla2x00_get_sp_from_handle(vha, func, req, logio);
	if (!sp)
		return;

	ctx = sp->ctx;
	lio = ctx->u.iocb_cmd;
	type = ctx->name;
	fcport = sp->fcport;
	data = lio->u.logio.data;

	data[0] = MBS_COMMAND_ERROR;
	data[1] = lio->u.logio.flags & SRB_LOGIN_RETRIED ?
		QLA_LOGIO_LOGIN_RETRIED : 0;
	if (logio->entry_status) {
		ql_log(ql_log_warn, vha, 0x5034,
		    "Async-%s error entry - "
		    "portid=%02x%02x%02x entry-status=%x.\n",
		    type, fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, logio->entry_status);
		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x5059,
		    (uint8_t *)logio, sizeof(*logio));

		goto logio_done;
	}

	if (le16_to_cpu(logio->comp_status) == CS_COMPLETE) {
		ql_dbg(ql_dbg_async, vha, 0x5036,
		    "Async-%s complete - portid=%02x%02x%02x "
		    "iop0=%x.\n",
		    type, fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa,
		    le32_to_cpu(logio->io_parameter[0]));

		data[0] = MBS_COMMAND_COMPLETE;
		if (ctx->type != SRB_LOGIN_CMD)
			goto logio_done;

		iop[0] = le32_to_cpu(logio->io_parameter[0]);
		if (iop[0] & BIT_4) {
			fcport->port_type = FCT_TARGET;
			if (iop[0] & BIT_8)
				fcport->flags |= FCF_FCP2_DEVICE;
		} else if (iop[0] & BIT_5)
			fcport->port_type = FCT_INITIATOR;

		if (logio->io_parameter[7] || logio->io_parameter[8])
			fcport->supported_classes |= FC_COS_CLASS2;
		if (logio->io_parameter[9] || logio->io_parameter[10])
			fcport->supported_classes |= FC_COS_CLASS3;

		goto logio_done;
	}

	iop[0] = le32_to_cpu(logio->io_parameter[0]);
	iop[1] = le32_to_cpu(logio->io_parameter[1]);
	switch (iop[0]) {
	case LSC_SCODE_PORTID_USED:
		data[0] = MBS_PORT_ID_USED;
		data[1] = LSW(iop[1]);
		break;
	case LSC_SCODE_NPORT_USED:
		data[0] = MBS_LOOP_ID_USED;
		break;
	default:
		data[0] = MBS_COMMAND_ERROR;
		break;
	}

	ql_dbg(ql_dbg_async, vha, 0x5037,
	    "Async-%s failed - portid=%02x%02x%02x comp=%x "
	    "iop0=%x iop1=%x.\n",
	    type, fcport->d_id.b.domain,
	    fcport->d_id.b.area, fcport->d_id.b.al_pa,
	    le16_to_cpu(logio->comp_status),
	    le32_to_cpu(logio->io_parameter[0]),
	    le32_to_cpu(logio->io_parameter[1]));

logio_done:
	lio->done(sp);
}

static void
qla24xx_tm_iocb_entry(scsi_qla_host_t *vha, struct req_que *req,
    struct tsk_mgmt_entry *tsk)
{
	const char func[] = "TMF-IOCB";
	const char *type;
	fc_port_t *fcport;
	srb_t *sp;
	struct srb_iocb *iocb;
	struct srb_ctx *ctx;
	struct sts_entry_24xx *sts = (struct sts_entry_24xx *)tsk;
	int error = 1;

	sp = qla2x00_get_sp_from_handle(vha, func, req, tsk);
	if (!sp)
		return;

	ctx = sp->ctx;
	iocb = ctx->u.iocb_cmd;
	type = ctx->name;
	fcport = sp->fcport;

	if (sts->entry_status) {
		ql_log(ql_log_warn, vha, 0x5038,
		    "Async-%s error - entry-status(%x).\n",
		    type, sts->entry_status);
	} else if (sts->comp_status != __constant_cpu_to_le16(CS_COMPLETE)) {
		ql_log(ql_log_warn, vha, 0x5039,
		    "Async-%s error - completion status(%x).\n",
		    type, sts->comp_status);
	} else if (!(le16_to_cpu(sts->scsi_status) &
	    SS_RESPONSE_INFO_LEN_VALID)) {
		ql_log(ql_log_warn, vha, 0x503a,
		    "Async-%s error - no response info(%x).\n",
		    type, sts->scsi_status);
	} else if (le32_to_cpu(sts->rsp_data_len) < 4) {
		ql_log(ql_log_warn, vha, 0x503b,
		    "Async-%s error - not enough response(%d).\n",
		    type, sts->rsp_data_len);
	} else if (sts->data[3]) {
		ql_log(ql_log_warn, vha, 0x503c,
		    "Async-%s error - response(%x).\n",
		    type, sts->data[3]);
	} else {
		error = 0;
	}

	if (error) {
		iocb->u.tmf.data = error;
		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x5055,
		    (uint8_t *)sts, sizeof(*sts));
	}

	iocb->done(sp);
}

/**
 * qla2x00_process_response_queue() - Process response queue entries.
 * @ha: SCSI driver HA context
 */
void
qla2x00_process_response_queue(struct rsp_que *rsp)
{
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha = rsp->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	sts_entry_t	*pkt;
	uint16_t        handle_cnt;
	uint16_t        cnt;

	vha = pci_get_drvdata(ha->pdev);

	if (!vha->flags.online)
		return;

	while (rsp->ring_ptr->signature != RESPONSE_PROCESSED) {
		pkt = (sts_entry_t *)rsp->ring_ptr;

		rsp->ring_index++;
		if (rsp->ring_index == rsp->length) {
			rsp->ring_index = 0;
			rsp->ring_ptr = rsp->ring;
		} else {
			rsp->ring_ptr++;
		}

		if (pkt->entry_status != 0) {
			ql_log(ql_log_warn, vha, 0x5035,
			    "Process error entry.\n");

			qla2x00_error_entry(vha, rsp, pkt);
			((response_t *)pkt)->signature = RESPONSE_PROCESSED;
			wmb();
			continue;
		}

		switch (pkt->entry_type) {
		case STATUS_TYPE:
			qla2x00_status_entry(vha, rsp, pkt);
			break;
		case STATUS_TYPE_21:
			handle_cnt = ((sts21_entry_t *)pkt)->handle_count;
			for (cnt = 0; cnt < handle_cnt; cnt++) {
				qla2x00_process_completed_request(vha, rsp->req,
				    ((sts21_entry_t *)pkt)->handle[cnt]);
			}
			break;
		case STATUS_TYPE_22:
			handle_cnt = ((sts22_entry_t *)pkt)->handle_count;
			for (cnt = 0; cnt < handle_cnt; cnt++) {
				qla2x00_process_completed_request(vha, rsp->req,
				    ((sts22_entry_t *)pkt)->handle[cnt]);
			}
			break;
		case STATUS_CONT_TYPE:
			qla2x00_status_cont_entry(rsp, (sts_cont_entry_t *)pkt);
			break;
		case MBX_IOCB_TYPE:
			qla2x00_mbx_iocb_entry(vha, rsp->req,
			    (struct mbx_entry *)pkt);
			break;
		case CT_IOCB_TYPE:
			qla2x00_ct_entry(vha, rsp->req, pkt, CT_IOCB_TYPE);
			break;
		default:
			/* Type Not Supported. */
			ql_log(ql_log_warn, vha, 0x504a,
			    "Received unknown response pkt type %x "
			    "entry status=%x.\n",
			    pkt->entry_type, pkt->entry_status);
			break;
		}
		((response_t *)pkt)->signature = RESPONSE_PROCESSED;
		wmb();
	}

	/* Adjust ring index */
	WRT_REG_WORD(ISP_RSP_Q_OUT(ha, reg), rsp->ring_index);
}

static inline void

qla2x00_handle_sense(srb_t *sp, uint8_t *sense_data, uint32_t par_sense_len,
    uint32_t sense_len, struct rsp_que *rsp)
{
	struct scsi_qla_host *vha = sp->fcport->vha;
	struct scsi_cmnd *cp = sp->cmd;

	if (sense_len >= SCSI_SENSE_BUFFERSIZE)
		sense_len = SCSI_SENSE_BUFFERSIZE;

	sp->request_sense_length = sense_len;
	sp->request_sense_ptr = cp->sense_buffer;
	if (sp->request_sense_length > par_sense_len)
		sense_len = par_sense_len;

	memcpy(cp->sense_buffer, sense_data, sense_len);

	sp->request_sense_ptr += sense_len;
	sp->request_sense_length -= sense_len;
	if (sp->request_sense_length != 0)
		rsp->status_srb = sp;

	ql_dbg(ql_dbg_io, vha, 0x301c,
	    "Check condition Sense data, scsi(%ld:%d:%d:%d) cmd=%p.\n",
	    sp->fcport->vha->host_no, cp->device->channel, cp->device->id,
	    cp->device->lun, cp);
	if (sense_len)
		ql_dump_buffer(ql_dbg_io + ql_dbg_buffer, vha, 0x302b,
		    cp->sense_buffer, sense_len);
}

struct scsi_dif_tuple {
	__be16 guard;       /* Checksum */
	__be16 app_tag;         /* APPL identifer */
	__be32 ref_tag;         /* Target LBA or indirect LBA */
};

/*
 * Checks the guard or meta-data for the type of error
 * detected by the HBA. In case of errors, we set the
 * ASC/ASCQ fields in the sense buffer with ILLEGAL_REQUEST
 * to indicate to the kernel that the HBA detected error.
 */
static inline int
qla2x00_handle_dif_error(srb_t *sp, struct sts_entry_24xx *sts24)
{
	struct scsi_qla_host *vha = sp->fcport->vha;
	struct scsi_cmnd *cmd = sp->cmd;
	uint8_t		*ap = &sts24->data[12];
	uint8_t		*ep = &sts24->data[20];
	uint32_t	e_ref_tag, a_ref_tag;
	uint16_t	e_app_tag, a_app_tag;
	uint16_t	e_guard, a_guard;

	/*
	 * swab32 of the "data" field in the beginning of qla2x00_status_entry()
	 * would make guard field appear at offset 2
	 */
	a_guard   = le16_to_cpu(*(uint16_t *)(ap + 2));
	a_app_tag = le16_to_cpu(*(uint16_t *)(ap + 0));
	a_ref_tag = le32_to_cpu(*(uint32_t *)(ap + 4));
	e_guard   = le16_to_cpu(*(uint16_t *)(ep + 2));
	e_app_tag = le16_to_cpu(*(uint16_t *)(ep + 0));
	e_ref_tag = le32_to_cpu(*(uint32_t *)(ep + 4));

	ql_dbg(ql_dbg_io, vha, 0x3023,
	    "iocb(s) %p Returned STATUS.\n", sts24);

	ql_dbg(ql_dbg_io, vha, 0x3024,
	    "DIF ERROR in cmd 0x%x lba 0x%llx act ref"
	    " tag=0x%x, exp ref_tag=0x%x, act app tag=0x%x, exp app"
	    " tag=0x%x, act guard=0x%x, exp guard=0x%x.\n",
	    cmd->cmnd[0], (u64)scsi_get_lba(cmd), a_ref_tag, e_ref_tag,
	    a_app_tag, e_app_tag, a_guard, e_guard);

	/*
	 * Ignore sector if:
	 * For type     3: ref & app tag is all 'f's
	 * For type 0,1,2: app tag is all 'f's
	 */
	if ((a_app_tag == 0xffff) &&
	    ((scsi_get_prot_type(cmd) != SCSI_PROT_DIF_TYPE3) ||
	     (a_ref_tag == 0xffffffff))) {
		uint32_t blocks_done, resid;
		sector_t lba_s = scsi_get_lba(cmd);

		/* 2TB boundary case covered automatically with this */
		blocks_done = e_ref_tag - (uint32_t)lba_s + 1;

		resid = scsi_bufflen(cmd) - (blocks_done *
		    cmd->device->sector_size);

		scsi_set_resid(cmd, resid);
		cmd->result = DID_OK << 16;

		/* Update protection tag */
		if (scsi_prot_sg_count(cmd)) {
			uint32_t i, j = 0, k = 0, num_ent;
			struct scatterlist *sg;
			struct sd_dif_tuple *spt;

			/* Patch the corresponding protection tags */
			scsi_for_each_prot_sg(cmd, sg,
			    scsi_prot_sg_count(cmd), i) {
				num_ent = sg_dma_len(sg) / 8;
				if (k + num_ent < blocks_done) {
					k += num_ent;
					continue;
				}
				j = blocks_done - k - 1;
				k = blocks_done;
				break;
			}

			if (k != blocks_done) {
				qla_printk(KERN_WARNING, sp->fcport->vha->hw,
				    "unexpected tag values tag:lba=%x:%llx)\n",
				    e_ref_tag, (unsigned long long)lba_s);
				return 1;
			}

			spt = page_address(sg_page(sg)) + sg->offset;
			spt += j;

			spt->app_tag = 0xffff;
			if (scsi_get_prot_type(cmd) == SCSI_PROT_DIF_TYPE3)
				spt->ref_tag = 0xffffffff;
		}

		return 0;
	}

	/* check guard */
	if (e_guard != a_guard) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
		    0x10, 0x1);
		set_driver_byte(cmd, DRIVER_SENSE);
		set_host_byte(cmd, DID_ABORT);
		cmd->result |= SAM_STAT_CHECK_CONDITION << 1;
		return 1;
	}

	/* check ref tag */
	if (e_ref_tag != a_ref_tag) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
		    0x10, 0x3);
		set_driver_byte(cmd, DRIVER_SENSE);
		set_host_byte(cmd, DID_ABORT);
		cmd->result |= SAM_STAT_CHECK_CONDITION << 1;
		return 1;
	}

	/* check appl tag */
	if (e_app_tag != a_app_tag) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
		    0x10, 0x2);
		set_driver_byte(cmd, DRIVER_SENSE);
		set_host_byte(cmd, DID_ABORT);
		cmd->result |= SAM_STAT_CHECK_CONDITION << 1;
		return 1;
	}

	return 1;
}

/**
 * qla2x00_status_entry() - Process a Status IOCB entry.
 * @ha: SCSI driver HA context
 * @pkt: Entry pointer
 */
static void
qla2x00_status_entry(scsi_qla_host_t *vha, struct rsp_que *rsp, void *pkt)
{
	srb_t		*sp;
	fc_port_t	*fcport;
	struct scsi_cmnd *cp;
	sts_entry_t *sts;
	struct sts_entry_24xx *sts24;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	uint16_t	ox_id;
	uint8_t		lscsi_status;
	int32_t		resid;
	uint32_t sense_len, par_sense_len, rsp_info_len, resid_len,
	    fw_resid_len;
	uint8_t		*rsp_info, *sense_data;
	struct qla_hw_data *ha = vha->hw;
	uint32_t handle;
	uint16_t que;
	struct req_que *req;
	int logit = 1;

	sts = (sts_entry_t *) pkt;
	sts24 = (struct sts_entry_24xx *) pkt;
	if (IS_FWI2_CAPABLE(ha)) {
		comp_status = le16_to_cpu(sts24->comp_status);
		scsi_status = le16_to_cpu(sts24->scsi_status) & SS_MASK;
	} else {
		comp_status = le16_to_cpu(sts->comp_status);
		scsi_status = le16_to_cpu(sts->scsi_status) & SS_MASK;
	}
	handle = (uint32_t) LSW(sts->handle);
	que = MSW(sts->handle);
	req = ha->req_q_map[que];

	/* Fast path completion. */
	if (comp_status == CS_COMPLETE && scsi_status == 0) {
		qla2x00_process_completed_request(vha, req, handle);

		return;
	}

	/* Validate handle. */
	if (handle < MAX_OUTSTANDING_COMMANDS) {
		sp = req->outstanding_cmds[handle];
		req->outstanding_cmds[handle] = NULL;
	} else
		sp = NULL;

	if (sp == NULL) {
		ql_log(ql_log_warn, vha, 0x3017,
		    "Invalid status handle (0x%x).\n", sts->handle);

		if (IS_QLA82XX(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
		return;
	}
	cp = sp->cmd;
	if (cp == NULL) {
		ql_log(ql_log_warn, vha, 0x3018,
		    "Command already returned (0x%x/%p).\n",
		    sts->handle, sp);

		return;
	}

  	lscsi_status = scsi_status & STATUS_MASK;

	fcport = sp->fcport;

	ox_id = 0;
	sense_len = par_sense_len = rsp_info_len = resid_len =
	    fw_resid_len = 0;
	if (IS_FWI2_CAPABLE(ha)) {
		if (scsi_status & SS_SENSE_LEN_VALID)
			sense_len = le32_to_cpu(sts24->sense_len);
		if (scsi_status & SS_RESPONSE_INFO_LEN_VALID)
			rsp_info_len = le32_to_cpu(sts24->rsp_data_len);
		if (scsi_status & (SS_RESIDUAL_UNDER | SS_RESIDUAL_OVER))
			resid_len = le32_to_cpu(sts24->rsp_residual_count);
		if (comp_status == CS_DATA_UNDERRUN)
			fw_resid_len = le32_to_cpu(sts24->residual_len);
		rsp_info = sts24->data;
		sense_data = sts24->data;
		host_to_fcp_swap(sts24->data, sizeof(sts24->data));
		ox_id = le16_to_cpu(sts24->ox_id);
		par_sense_len = sizeof(sts24->data);
	} else {
		if (scsi_status & SS_SENSE_LEN_VALID)
			sense_len = le16_to_cpu(sts->req_sense_length);
		if (scsi_status & SS_RESPONSE_INFO_LEN_VALID)
			rsp_info_len = le16_to_cpu(sts->rsp_info_len);
		resid_len = le32_to_cpu(sts->residual_length);
		rsp_info = sts->rsp_info;
		sense_data = sts->req_sense_data;
		par_sense_len = sizeof(sts->req_sense_data);
	}

	/* Check for any FCP transport errors. */
	if (scsi_status & SS_RESPONSE_INFO_LEN_VALID) {
		/* Sense data lies beyond any FCP RESPONSE data. */
		if (IS_FWI2_CAPABLE(ha)) {
			sense_data += rsp_info_len;
			par_sense_len -= rsp_info_len;
		}
		if (rsp_info_len > 3 && rsp_info[3]) {
			ql_log(ql_log_warn, vha, 0x3019,
			    "FCP I/O protocol failure (0x%x/0x%x).\n",
			    rsp_info_len, rsp_info[3]);

			cp->result = DID_BUS_BUSY << 16;
			goto out;
		}
	}

	/* Check for overrun. */
	if (IS_FWI2_CAPABLE(ha) && comp_status == CS_COMPLETE &&
	    scsi_status & SS_RESIDUAL_OVER)
		comp_status = CS_DATA_OVERRUN;

	/*
	 * Based on Host and scsi status generate status code for Linux
	 */
	switch (comp_status) {
	case CS_COMPLETE:
	case CS_QUEUE_FULL:
		if (scsi_status == 0) {
			cp->result = DID_OK << 16;
			break;
		}
		if (scsi_status & (SS_RESIDUAL_UNDER | SS_RESIDUAL_OVER)) {
			resid = resid_len;
			scsi_set_resid(cp, resid);

			if (!lscsi_status &&
			    ((unsigned)(scsi_bufflen(cp) - resid) <
			     cp->underflow)) {
				ql_log(ql_log_warn, vha, 0x301a,
				    "Mid-layer underflow "
				    "detected (0x%x of 0x%x bytes).\n",
				    resid, scsi_bufflen(cp));

				cp->result = DID_ERROR << 16;
				break;
			}
		}
		cp->result = DID_OK << 16 | lscsi_status;

		if (lscsi_status == SAM_STAT_TASK_SET_FULL) {
			ql_log(ql_log_warn, vha, 0x301b,
			    "QUEUE FULL detected.\n");
			break;
		}
		logit = 0;
		if (lscsi_status != SS_CHECK_CONDITION)
			break;

		memset(cp->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
		if (!(scsi_status & SS_SENSE_LEN_VALID))
			break;

		qla2x00_handle_sense(sp, sense_data, par_sense_len, sense_len,
		    rsp);
		break;

	case CS_DATA_UNDERRUN:
		/* Use F/W calculated residual length. */
		resid = IS_FWI2_CAPABLE(ha) ? fw_resid_len : resid_len;
		scsi_set_resid(cp, resid);
		if (scsi_status & SS_RESIDUAL_UNDER) {
			if (IS_FWI2_CAPABLE(ha) && fw_resid_len != resid_len) {
				ql_log(ql_log_warn, vha, 0x301d,
				    "Dropped frame(s) detected "
				    "(0x%x of 0x%x bytes).\n",
				    resid, scsi_bufflen(cp));

				cp->result = DID_ERROR << 16 | lscsi_status;
				break;
			}

			if (!lscsi_status &&
			    ((unsigned)(scsi_bufflen(cp) - resid) <
			    cp->underflow)) {
				ql_log(ql_log_warn, vha, 0x301e,
				    "Mid-layer underflow "
				    "detected (0x%x of 0x%x bytes).\n",
				    resid, scsi_bufflen(cp));

				cp->result = DID_ERROR << 16;
				break;
			}
		} else {
			ql_log(ql_log_warn, vha, 0x301f,
			    "Dropped frame(s) detected (0x%x "
			    "of 0x%x bytes).\n", resid, scsi_bufflen(cp));

			cp->result = DID_ERROR << 16 | lscsi_status;
			goto check_scsi_status;
		}

		cp->result = DID_OK << 16 | lscsi_status;
		logit = 0;

check_scsi_status:
		/*
		 * Check to see if SCSI Status is non zero. If so report SCSI
		 * Status.
		 */
		if (lscsi_status != 0) {
			if (lscsi_status == SAM_STAT_TASK_SET_FULL) {
				ql_log(ql_log_warn, vha, 0x3020,
				    "QUEUE FULL detected.\n");
				logit = 1;
				break;
			}
			if (lscsi_status != SS_CHECK_CONDITION)
				break;

			memset(cp->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
			if (!(scsi_status & SS_SENSE_LEN_VALID))
				break;

			qla2x00_handle_sense(sp, sense_data, par_sense_len,
			    sense_len, rsp);
		}
		break;

	case CS_PORT_LOGGED_OUT:
	case CS_PORT_CONFIG_CHG:
	case CS_PORT_BUSY:
	case CS_INCOMPLETE:
	case CS_PORT_UNAVAILABLE:
	case CS_TIMEOUT:
	case CS_RESET:

		/*
		 * We are going to have the fc class block the rport
		 * while we try to recover so instruct the mid layer
		 * to requeue until the class decides how to handle this.
		 */
		cp->result = DID_TRANSPORT_DISRUPTED << 16;

		if (comp_status == CS_TIMEOUT) {
			if (IS_FWI2_CAPABLE(ha))
				break;
			else if ((le16_to_cpu(sts->status_flags) &
			    SF_LOGOUT_SENT) == 0)
				break;
		}

		ql_dbg(ql_dbg_io, vha, 0x3021,
		    "Port down status: port-state=0x%x.\n",
		    atomic_read(&fcport->state));

		if (atomic_read(&fcport->state) == FCS_ONLINE)
			qla2x00_mark_device_lost(fcport->vha, fcport, 1, 1);
		break;

	case CS_ABORTED:
		cp->result = DID_RESET << 16;
		break;

	case CS_DIF_ERROR:
		logit = qla2x00_handle_dif_error(sp, sts24);
		break;
	default:
		cp->result = DID_ERROR << 16;
		break;
	}

out:
	if (logit)
		ql_dbg(ql_dbg_io, vha, 0x3022,
		    "FCP command status: 0x%x-0x%x (0x%x) "
		    "oxid=0x%x cdb=%02x%02x%02x len=0x%x "
		    "rsp_info=0x%x resid=0x%x fw_resid=0x%x.\n",
		    comp_status, scsi_status, cp->result, ox_id, cp->cmnd[0],
		    cp->cmnd[1], cp->cmnd[2], scsi_bufflen(cp), rsp_info_len,
		    resid_len, fw_resid_len);

	if (rsp->status_srb == NULL)
		qla2x00_sp_compl(ha, sp);
}

/**
 * qla2x00_status_cont_entry() - Process a Status Continuations entry.
 * @ha: SCSI driver HA context
 * @pkt: Entry pointer
 *
 * Extended sense data.
 */
static void
qla2x00_status_cont_entry(struct rsp_que *rsp, sts_cont_entry_t *pkt)
{
	uint8_t		sense_sz = 0;
	struct qla_hw_data *ha = rsp->hw;
	struct scsi_qla_host *vha = pci_get_drvdata(ha->pdev);
	srb_t		*sp = rsp->status_srb;
	struct scsi_cmnd *cp;

	if (sp != NULL && sp->request_sense_length != 0) {
		cp = sp->cmd;
		if (cp == NULL) {
			ql_log(ql_log_warn, vha, 0x3025,
			    "cmd is NULL: already returned to OS (sp=%p).\n",
			    sp);

			rsp->status_srb = NULL;
			return;
		}

		if (sp->request_sense_length > sizeof(pkt->data)) {
			sense_sz = sizeof(pkt->data);
		} else {
			sense_sz = sp->request_sense_length;
		}

		/* Move sense data. */
		if (IS_FWI2_CAPABLE(ha))
			host_to_fcp_swap(pkt->data, sizeof(pkt->data));
		memcpy(sp->request_sense_ptr, pkt->data, sense_sz);
		ql_dump_buffer(ql_dbg_io + ql_dbg_buffer, vha, 0x302c,
			sp->request_sense_ptr, sense_sz);

		sp->request_sense_ptr += sense_sz;
		sp->request_sense_length -= sense_sz;

		/* Place command on done queue. */
		if (sp->request_sense_length == 0) {
			rsp->status_srb = NULL;
			qla2x00_sp_compl(ha, sp);
		}
	}
}

/**
 * qla2x00_error_entry() - Process an error entry.
 * @ha: SCSI driver HA context
 * @pkt: Entry pointer
 */
static void
qla2x00_error_entry(scsi_qla_host_t *vha, struct rsp_que *rsp, sts_entry_t *pkt)
{
	srb_t *sp;
	struct qla_hw_data *ha = vha->hw;
	uint32_t handle = LSW(pkt->handle);
	uint16_t que = MSW(pkt->handle);
	struct req_que *req = ha->req_q_map[que];

	if (pkt->entry_status & RF_INV_E_ORDER)
		ql_dbg(ql_dbg_async, vha, 0x502a,
		    "Invalid Entry Order.\n");
	else if (pkt->entry_status & RF_INV_E_COUNT)
		ql_dbg(ql_dbg_async, vha, 0x502b,
		    "Invalid Entry Count.\n");
	else if (pkt->entry_status & RF_INV_E_PARAM)
		ql_dbg(ql_dbg_async, vha, 0x502c,
		    "Invalid Entry Parameter.\n");
	else if (pkt->entry_status & RF_INV_E_TYPE)
		ql_dbg(ql_dbg_async, vha, 0x502d,
		    "Invalid Entry Type.\n");
	else if (pkt->entry_status & RF_BUSY)
		ql_dbg(ql_dbg_async, vha, 0x502e,
		    "Busy.\n");
	else
		ql_dbg(ql_dbg_async, vha, 0x502f,
		    "UNKNOWN flag error.\n");

	/* Validate handle. */
	if (handle < MAX_OUTSTANDING_COMMANDS)
		sp = req->outstanding_cmds[handle];
	else
		sp = NULL;

	if (sp) {
		/* Free outstanding command slot. */
		req->outstanding_cmds[handle] = NULL;

		/* Bad payload or header */
		if (pkt->entry_status &
		    (RF_INV_E_ORDER | RF_INV_E_COUNT |
		     RF_INV_E_PARAM | RF_INV_E_TYPE)) {
			sp->cmd->result = DID_ERROR << 16;
		} else if (pkt->entry_status & RF_BUSY) {
			sp->cmd->result = DID_BUS_BUSY << 16;
		} else {
			sp->cmd->result = DID_ERROR << 16;
		}
		qla2x00_sp_compl(ha, sp);

	} else if (pkt->entry_type == COMMAND_A64_TYPE || pkt->entry_type ==
		COMMAND_TYPE || pkt->entry_type == COMMAND_TYPE_7
		|| pkt->entry_type == COMMAND_TYPE_6) {
		ql_log(ql_log_warn, vha, 0x5030,
		    "Error entry - invalid handle.\n");

		if (IS_QLA82XX(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
	}
}

/**
 * qla24xx_mbx_completion() - Process mailbox command completions.
 * @ha: SCSI driver HA context
 * @mb0: Mailbox0 register
 */
static void
qla24xx_mbx_completion(scsi_qla_host_t *vha, uint16_t mb0)
{
	uint16_t	cnt;
	uint16_t __iomem *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	/* Load return mailbox registers. */
	ha->flags.mbox_int = 1;
	ha->mailbox_out[0] = mb0;
	wptr = (uint16_t __iomem *)&reg->mailbox1;

	for (cnt = 1; cnt < ha->mbx_count; cnt++) {
		ha->mailbox_out[cnt] = RD_REG_WORD(wptr);
		wptr++;
	}

	if (ha->mcp) {
		ql_dbg(ql_dbg_async, vha, 0x504d,
		    "Got mailbox completion. cmd=%x.\n", ha->mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_async, vha, 0x504e,
		    "MBX pointer ERROR.\n");
	}
}

/**
 * qla24xx_process_response_queue() - Process response queue entries.
 * @ha: SCSI driver HA context
 */
void qla24xx_process_response_queue(struct scsi_qla_host *vha,
	struct rsp_que *rsp)
{
	struct sts_entry_24xx *pkt;
	struct qla_hw_data *ha = vha->hw;

	if (!vha->flags.online)
		return;

	while (rsp->ring_ptr->signature != RESPONSE_PROCESSED) {
		pkt = (struct sts_entry_24xx *)rsp->ring_ptr;

		rsp->ring_index++;
		if (rsp->ring_index == rsp->length) {
			rsp->ring_index = 0;
			rsp->ring_ptr = rsp->ring;
		} else {
			rsp->ring_ptr++;
		}

		if (pkt->entry_status != 0) {
			ql_dbg(ql_dbg_async, vha, 0x5029,
			    "Process error entry.\n");

			qla2x00_error_entry(vha, rsp, (sts_entry_t *) pkt);
			((response_t *)pkt)->signature = RESPONSE_PROCESSED;
			wmb();
			continue;
		}

		switch (pkt->entry_type) {
		case STATUS_TYPE:
			qla2x00_status_entry(vha, rsp, pkt);
			break;
		case STATUS_CONT_TYPE:
			qla2x00_status_cont_entry(rsp, (sts_cont_entry_t *)pkt);
			break;
		case VP_RPT_ID_IOCB_TYPE:
			qla24xx_report_id_acquisition(vha,
			    (struct vp_rpt_id_entry_24xx *)pkt);
			break;
		case LOGINOUT_PORT_IOCB_TYPE:
			qla24xx_logio_entry(vha, rsp->req,
			    (struct logio_entry_24xx *)pkt);
			break;
		case TSK_MGMT_IOCB_TYPE:
			qla24xx_tm_iocb_entry(vha, rsp->req,
			    (struct tsk_mgmt_entry *)pkt);
			break;
                case CT_IOCB_TYPE:
			qla24xx_els_ct_entry(vha, rsp->req, pkt, CT_IOCB_TYPE);
			clear_bit(MBX_INTERRUPT, &vha->hw->mbx_cmd_flags);
			break;
                case ELS_IOCB_TYPE:
			qla24xx_els_ct_entry(vha, rsp->req, pkt, ELS_IOCB_TYPE);
			break;
		case MARKER_TYPE:
			/* Do nothing in this case, this check is to prevent it
			 * from falling into default case
			 */
			break;
		default:
			/* Type Not Supported. */
			ql_dbg(ql_dbg_async, vha, 0x5042,
			    "Received unknown response pkt type %x "
			    "entry status=%x.\n",
			    pkt->entry_type, pkt->entry_status);
			break;
		}
		((response_t *)pkt)->signature = RESPONSE_PROCESSED;
		wmb();
	}

	/* Adjust ring index */
	if (IS_QLA82XX(ha)) {
		struct device_reg_82xx __iomem *reg = &ha->iobase->isp82;
		WRT_REG_DWORD(&reg->rsp_q_out[0], rsp->ring_index);
	} else
		WRT_REG_DWORD(rsp->rsp_q_out, rsp->ring_index);
}

static void
qla2xxx_check_risc_status(scsi_qla_host_t *vha)
{
	int rval;
	uint32_t cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (!IS_QLA25XX(ha) && !IS_QLA81XX(ha))
		return;

	rval = QLA_SUCCESS;
	WRT_REG_DWORD(&reg->iobase_addr, 0x7C00);
	RD_REG_DWORD(&reg->iobase_addr);
	WRT_REG_DWORD(&reg->iobase_window, 0x0001);
	for (cnt = 10000; (RD_REG_DWORD(&reg->iobase_window) & BIT_0) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt) {
			WRT_REG_DWORD(&reg->iobase_window, 0x0001);
			udelay(10);
		} else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	if (rval == QLA_SUCCESS)
		goto next_test;

	WRT_REG_DWORD(&reg->iobase_window, 0x0003);
	for (cnt = 100; (RD_REG_DWORD(&reg->iobase_window) & BIT_0) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt) {
			WRT_REG_DWORD(&reg->iobase_window, 0x0003);
			udelay(10);
		} else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	if (rval != QLA_SUCCESS)
		goto done;

next_test:
	if (RD_REG_DWORD(&reg->iobase_c8) & BIT_3)
		ql_log(ql_log_info, vha, 0x504c,
		    "Additional code -- 0x55AA.\n");

done:
	WRT_REG_DWORD(&reg->iobase_window, 0x0000);
	RD_REG_DWORD(&reg->iobase_window);
}

/**
 * qla24xx_intr_handler() - Process interrupts for the ISP23xx and ISP63xx.
 * @irq:
 * @dev_id: SCSI driver HA context
 *
 * Called by system whenever the host adapter generates an interrupt.
 *
 * Returns handled flag.
 */
irqreturn_t
qla24xx_intr_handler(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct device_reg_24xx __iomem *reg;
	int		status;
	unsigned long	iter;
	uint32_t	stat;
	uint32_t	hccr;
	uint16_t	mb[4];
	struct rsp_que *rsp;
	unsigned long	flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
		    "%s(): NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}

	ha = rsp->hw;
	reg = &ha->iobase->isp24;
	status = 0;

	if (unlikely(pci_channel_offline(ha->pdev)))
		return IRQ_HANDLED;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	for (iter = 50; iter--; ) {
		stat = RD_REG_DWORD(&reg->host_status);
		if (stat & HSRX_RISC_PAUSED) {
			if (unlikely(pci_channel_offline(ha->pdev)))
				break;

			hccr = RD_REG_DWORD(&reg->hccr);

			ql_log(ql_log_warn, vha, 0x504b,
			    "RISC paused -- HCCR=%x, Dumping firmware.\n",
			    hccr);

			qla2xxx_check_risc_status(vha);

			ha->isp_ops->fw_dump(vha, 1);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			break;
		} else if ((stat & HSRX_RISC_INT) == 0)
			break;

		switch (stat & 0xff) {
		case 0x1:
		case 0x2:
		case 0x10:
		case 0x11:
			qla24xx_mbx_completion(vha, MSW(stat));
			status |= MBX_INTERRUPT;

			break;
		case 0x12:
			mb[0] = MSW(stat);
			mb[1] = RD_REG_WORD(&reg->mailbox1);
			mb[2] = RD_REG_WORD(&reg->mailbox2);
			mb[3] = RD_REG_WORD(&reg->mailbox3);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case 0x13:
		case 0x14:
			qla24xx_process_response_queue(vha, rsp);
			break;
		default:
			ql_dbg(ql_dbg_async, vha, 0x504f,
			    "Unrecognized interrupt type (%d).\n", stat * 0xff);
			break;
		}
		WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
		RD_REG_DWORD_RELAXED(&reg->hccr);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}

	return IRQ_HANDLED;
}

static irqreturn_t
qla24xx_msix_rsp_q(int irq, void *dev_id)
{
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_24xx __iomem *reg;
	struct scsi_qla_host *vha;
	unsigned long flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
		"%s(): NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;
	reg = &ha->iobase->isp24;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	vha = pci_get_drvdata(ha->pdev);
	qla24xx_process_response_queue(vha, rsp);
	if (!ha->flags.disable_msix_handshake) {
		WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
		RD_REG_DWORD_RELAXED(&reg->hccr);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t
qla25xx_msix_rsp_q(int irq, void *dev_id)
{
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_24xx __iomem *reg;
	unsigned long flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;

	/* Clear the interrupt, if enabled, for this response queue */
	if (!ha->flags.disable_msix_handshake) {
		reg = &ha->iobase->isp24;
		spin_lock_irqsave(&ha->hardware_lock, flags);
		WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
		RD_REG_DWORD_RELAXED(&reg->hccr);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}
	queue_work_on((int) (rsp->id - 1), ha->wq, &rsp->q_work);

	return IRQ_HANDLED;
}

static irqreturn_t
qla24xx_msix_default(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_24xx __iomem *reg;
	int		status;
	uint32_t	stat;
	uint32_t	hccr;
	uint16_t	mb[4];
	unsigned long flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		printk(KERN_INFO
			"%s(): NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;
	reg = &ha->iobase->isp24;
	status = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	do {
		stat = RD_REG_DWORD(&reg->host_status);
		if (stat & HSRX_RISC_PAUSED) {
			if (unlikely(pci_channel_offline(ha->pdev)))
				break;

			hccr = RD_REG_DWORD(&reg->hccr);

			ql_log(ql_log_info, vha, 0x5050,
			    "RISC paused -- HCCR=%x, Dumping firmware.\n",
			    hccr);

			qla2xxx_check_risc_status(vha);

			ha->isp_ops->fw_dump(vha, 1);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			break;
		} else if ((stat & HSRX_RISC_INT) == 0)
			break;

		switch (stat & 0xff) {
		case 0x1:
		case 0x2:
		case 0x10:
		case 0x11:
			qla24xx_mbx_completion(vha, MSW(stat));
			status |= MBX_INTERRUPT;

			break;
		case 0x12:
			mb[0] = MSW(stat);
			mb[1] = RD_REG_WORD(&reg->mailbox1);
			mb[2] = RD_REG_WORD(&reg->mailbox2);
			mb[3] = RD_REG_WORD(&reg->mailbox3);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case 0x13:
		case 0x14:
			qla24xx_process_response_queue(vha, rsp);
			break;
		default:
			ql_dbg(ql_dbg_async, vha, 0x5051,
			    "Unrecognized interrupt type (%d).\n", stat & 0xff);
			break;
		}
		WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
	} while (0);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}
	return IRQ_HANDLED;
}

/* Interrupt handling helpers. */

struct qla_init_msix_entry {
	const char *name;
	irq_handler_t handler;
};

static struct qla_init_msix_entry msix_entries[3] = {
	{ "qla2xxx (default)", qla24xx_msix_default },
	{ "qla2xxx (rsp_q)", qla24xx_msix_rsp_q },
	{ "qla2xxx (multiq)", qla25xx_msix_rsp_q },
};

static struct qla_init_msix_entry qla82xx_msix_entries[2] = {
	{ "qla2xxx (default)", qla82xx_msix_default },
	{ "qla2xxx (rsp_q)", qla82xx_msix_rsp_q },
};

static void
qla24xx_disable_msix(struct qla_hw_data *ha)
{
	int i;
	struct qla_msix_entry *qentry;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	for (i = 0; i < ha->msix_count; i++) {
		qentry = &ha->msix_entries[i];
		if (qentry->have_irq)
			free_irq(qentry->vector, qentry->rsp);
	}
	pci_disable_msix(ha->pdev);
	kfree(ha->msix_entries);
	ha->msix_entries = NULL;
	ha->flags.msix_enabled = 0;
	ql_dbg(ql_dbg_init, vha, 0x0042,
	    "Disabled the MSI.\n");
}

static int
qla24xx_enable_msix(struct qla_hw_data *ha, struct rsp_que *rsp)
{
#define MIN_MSIX_COUNT	2
	int i, ret;
	struct msix_entry *entries;
	struct qla_msix_entry *qentry;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	entries = kzalloc(sizeof(struct msix_entry) * ha->msix_count,
			GFP_KERNEL);
	if (!entries) {
		ql_log(ql_log_warn, vha, 0x00bc,
		    "Failed to allocate memory for msix_entry.\n");
		return -ENOMEM;
	}

	for (i = 0; i < ha->msix_count; i++)
		entries[i].entry = i;

	ret = pci_enable_msix(ha->pdev, entries, ha->msix_count);
	if (ret) {
		if (ret < MIN_MSIX_COUNT)
			goto msix_failed;

		ql_log(ql_log_warn, vha, 0x00c6,
		    "MSI-X: Failed to enable support "
		    "-- %d/%d\n Retry with %d vectors.\n",
		    ha->msix_count, ret, ret);
		ha->msix_count = ret;
		ret = pci_enable_msix(ha->pdev, entries, ha->msix_count);
		if (ret) {
msix_failed:
			ql_log(ql_log_fatal, vha, 0x00c7,
			    "MSI-X: Failed to enable support, "
			    "giving   up -- %d/%d.\n",
			    ha->msix_count, ret);
			goto msix_out;
		}
		ha->max_rsp_queues = ha->msix_count - 1;
	}
	ha->msix_entries = kzalloc(sizeof(struct qla_msix_entry) *
				ha->msix_count, GFP_KERNEL);
	if (!ha->msix_entries) {
		ql_log(ql_log_fatal, vha, 0x00c8,
		    "Failed to allocate memory for ha->msix_entries.\n");
		ret = -ENOMEM;
		goto msix_out;
	}
	ha->flags.msix_enabled = 1;

	for (i = 0; i < ha->msix_count; i++) {
		qentry = &ha->msix_entries[i];
		qentry->vector = entries[i].vector;
		qentry->entry = entries[i].entry;
		qentry->have_irq = 0;
		qentry->rsp = NULL;
	}

	/* Enable MSI-X vectors for the base queue */
	for (i = 0; i < 2; i++) {
		qentry = &ha->msix_entries[i];
		if (IS_QLA82XX(ha)) {
			ret = request_irq(qentry->vector,
				qla82xx_msix_entries[i].handler,
				0, qla82xx_msix_entries[i].name, rsp);
		} else {
			ret = request_irq(qentry->vector,
				msix_entries[i].handler,
				0, msix_entries[i].name, rsp);
		}
		if (ret) {
			ql_log(ql_log_fatal, vha, 0x00cb,
			    "MSI-X: unable to register handler -- %x/%d.\n",
			    qentry->vector, ret);
			qla24xx_disable_msix(ha);
			ha->mqenable = 0;
			goto msix_out;
		}
		qentry->have_irq = 1;
		qentry->rsp = rsp;
		rsp->msix = qentry;
	}

	/* Enable MSI-X vector for response queue update for queue 0 */
	if (ha->mqiobase &&  (ha->max_rsp_queues > 1 || ha->max_req_queues > 1))
		ha->mqenable = 1;
	ql_dbg(ql_dbg_multiq, vha, 0xc005,
	    "mqiobase=%p, max_rsp_queues=%d, max_req_queues=%d.\n",
	    ha->mqiobase, ha->max_rsp_queues, ha->max_req_queues);
	ql_dbg(ql_dbg_init, vha, 0x0055,
	    "mqiobase=%p, max_rsp_queues=%d, max_req_queues=%d.\n",
	    ha->mqiobase, ha->max_rsp_queues, ha->max_req_queues);

msix_out:
	kfree(entries);
	return ret;
}

int
qla2x00_request_irqs(struct qla_hw_data *ha, struct rsp_que *rsp)
{
	int ret;
	device_reg_t __iomem *reg = ha->iobase;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	/* If possible, enable MSI-X. */
	if (!IS_QLA2432(ha) && !IS_QLA2532(ha) &&
		!IS_QLA8432(ha) && !IS_QLA8XXX_TYPE(ha))
		goto skip_msi;

	if (ha->pdev->subsystem_vendor == PCI_VENDOR_ID_HP &&
		(ha->pdev->subsystem_device == 0x7040 ||
		ha->pdev->subsystem_device == 0x7041 ||
		ha->pdev->subsystem_device == 0x1705)) {
		ql_log(ql_log_warn, vha, 0x0034,
		    "MSI-X: Unsupported ISP 2432 SSVID/SSDID (0x%X,0x%X).\n",
			ha->pdev->subsystem_vendor,
			ha->pdev->subsystem_device);
		goto skip_msi;
	}

	if (IS_QLA2432(ha) && (ha->pdev->revision < QLA_MSIX_CHIP_REV_24XX)) {
		ql_log(ql_log_warn, vha, 0x0035,
		    "MSI-X; Unsupported ISP2432 (0x%X, 0x%X).\n",
		    ha->pdev->revision, QLA_MSIX_CHIP_REV_24XX);
		goto skip_msix;
	}

	ret = qla24xx_enable_msix(ha, rsp);
	if (!ret) {
		ql_dbg(ql_dbg_init, vha, 0x0036,
		    "MSI-X: Enabled (0x%X, 0x%X).\n",
		    ha->chip_revision, ha->fw_attributes);
		goto clear_risc_ints;
	}
	ql_log(ql_log_info, vha, 0x0037,
	    "MSI-X Falling back-to MSI mode -%d.\n", ret);
skip_msix:

	if (!IS_QLA24XX(ha) && !IS_QLA2532(ha) && !IS_QLA8432(ha) &&
	    !IS_QLA8001(ha))
		goto skip_msi;

	ret = pci_enable_msi(ha->pdev);
	if (!ret) {
		ql_dbg(ql_dbg_init, vha, 0x0038,
		    "MSI: Enabled.\n");
		ha->flags.msi_enabled = 1;
	} else
		ql_log(ql_log_warn, vha, 0x0039,
		    "MSI-X; Falling back-to INTa mode -- %d.\n", ret);
skip_msi:

	ret = request_irq(ha->pdev->irq, ha->isp_ops->intr_handler,
	    ha->flags.msi_enabled ? 0 : IRQF_SHARED,
	    QLA2XXX_DRIVER_NAME, rsp);
	if (ret) {
		ql_log(ql_log_warn, vha, 0x003a,
		    "Failed to reserve interrupt %d already in use.\n",
		    ha->pdev->irq);
		goto fail;
	}

clear_risc_ints:

	/*
	 * FIXME: Noted that 8014s were being dropped during NK testing.
	 * Timing deltas during MSI-X/INTa transitions?
	 */
	if (IS_QLA81XX(ha) || IS_QLA82XX(ha))
		goto fail;
	spin_lock_irq(&ha->hardware_lock);
	if (IS_FWI2_CAPABLE(ha)) {
		WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_CLR_HOST_INT);
		WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_CLR_RISC_INT);
	} else {
		WRT_REG_WORD(&reg->isp.semaphore, 0);
		WRT_REG_WORD(&reg->isp.hccr, HCCR_CLR_RISC_INT);
		WRT_REG_WORD(&reg->isp.hccr, HCCR_CLR_HOST_INT);
	}
	spin_unlock_irq(&ha->hardware_lock);

fail:
	return ret;
}

void
qla2x00_free_irqs(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct rsp_que *rsp = ha->rsp_q_map[0];

	if (ha->flags.msix_enabled)
		qla24xx_disable_msix(ha);
	else if (ha->flags.msi_enabled) {
		free_irq(ha->pdev->irq, rsp);
		pci_disable_msi(ha->pdev);
	} else
		free_irq(ha->pdev->irq, rsp);
}


int qla25xx_request_irq(struct rsp_que *rsp)
{
	struct qla_hw_data *ha = rsp->hw;
	struct qla_init_msix_entry *intr = &msix_entries[2];
	struct qla_msix_entry *msix = rsp->msix;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	int ret;

	ret = request_irq(msix->vector, intr->handler, 0, intr->name, rsp);
	if (ret) {
		ql_log(ql_log_fatal, vha, 0x00e6,
		    "MSI-X: Unable to register handler -- %x/%d.\n",
		    msix->vector, ret);
		return ret;
	}
	msix->have_irq = 1;
	msix->rsp = rsp;
	return ret;
}
