/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include "qla_target.h"

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/t10-pi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_bsg_fc.h>
#include <scsi/scsi_eh.h>
#include <scsi/fc/fc_fs.h>
#include <linux/nvme-fc-driver.h>

static void qla2x00_mbx_completion(scsi_qla_host_t *, uint16_t);
static void qla2x00_status_entry(scsi_qla_host_t *, struct rsp_que *, void *);
static void qla2x00_status_cont_entry(struct rsp_que *, sts_cont_entry_t *);
static int qla2x00_error_entry(scsi_qla_host_t *, struct rsp_que *,
	sts_entry_t *);
static void qla27xx_process_purex_fpin(struct scsi_qla_host *vha,
	struct purex_item *item);
static struct purex_item *qla24xx_alloc_purex_item(scsi_qla_host_t *vha,
	uint16_t size);
static struct purex_item *qla24xx_copy_std_pkt(struct scsi_qla_host *vha,
	void *pkt);
static struct purex_item *qla27xx_copy_fpin_pkt(struct scsi_qla_host *vha,
	void **pkt, struct rsp_que **rsp);

static void
qla27xx_process_purex_fpin(struct scsi_qla_host *vha, struct purex_item *item)
{
	void *pkt = &item->iocb;
	uint16_t pkt_size = item->size;

	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x508d,
	       "%s: Enter\n", __func__);

	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x508e,
	       "-------- ELS REQ -------\n");
	ql_dump_buffer(ql_dbg_init + ql_dbg_verbose, vha, 0x508f,
		       pkt, pkt_size);

	fc_host_fpin_rcv(vha->host, pkt_size, (char *)pkt);
}

const char *const port_state_str[] = {
	"Unknown",
	"UNCONFIGURED",
	"DEAD",
	"LOST",
	"ONLINE"
};

static void
qla24xx_process_abts(struct scsi_qla_host *vha, struct purex_item *pkt)
{
	struct abts_entry_24xx *abts =
	    (struct abts_entry_24xx *)&pkt->iocb;
	struct qla_hw_data *ha = vha->hw;
	struct els_entry_24xx *rsp_els;
	struct abts_entry_24xx *abts_rsp;
	dma_addr_t dma;
	uint32_t fctl;
	int rval;

	ql_dbg(ql_dbg_init, vha, 0x0286, "%s: entered.\n", __func__);

	ql_log(ql_log_warn, vha, 0x0287,
	    "Processing ABTS xchg=%#x oxid=%#x rxid=%#x seqid=%#x seqcnt=%#x\n",
	    abts->rx_xch_addr_to_abort, abts->ox_id, abts->rx_id,
	    abts->seq_id, abts->seq_cnt);
	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x0287,
	    "-------- ABTS RCV -------\n");
	ql_dump_buffer(ql_dbg_init + ql_dbg_verbose, vha, 0x0287,
	    (uint8_t *)abts, sizeof(*abts));

	rsp_els = dma_alloc_coherent(&ha->pdev->dev, sizeof(*rsp_els), &dma,
	    GFP_KERNEL);
	if (!rsp_els) {
		ql_log(ql_log_warn, vha, 0x0287,
		    "Failed allocate dma buffer ABTS/ELS RSP.\n");
		return;
	}

	/* terminate exchange */
	rsp_els->entry_type = ELS_IOCB_TYPE;
	rsp_els->entry_count = 1;
	rsp_els->nport_handle = cpu_to_le16(~0);
	rsp_els->rx_xchg_address = abts->rx_xch_addr_to_abort;
	rsp_els->control_flags = cpu_to_le16(EPD_RX_XCHG);
	ql_dbg(ql_dbg_init, vha, 0x0283,
	    "Sending ELS Response to terminate exchange %#x...\n",
	    abts->rx_xch_addr_to_abort);
	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x0283,
	    "-------- ELS RSP -------\n");
	ql_dump_buffer(ql_dbg_init + ql_dbg_verbose, vha, 0x0283,
	    (uint8_t *)rsp_els, sizeof(*rsp_els));
	rval = qla2x00_issue_iocb(vha, rsp_els, dma, 0);
	if (rval) {
		ql_log(ql_log_warn, vha, 0x0288,
		    "%s: iocb failed to execute -> %x\n", __func__, rval);
	} else if (rsp_els->comp_status) {
		ql_log(ql_log_warn, vha, 0x0289,
		    "%s: iocb failed to complete -> completion=%#x subcode=(%#x,%#x)\n",
		    __func__, rsp_els->comp_status,
		    rsp_els->error_subcode_1, rsp_els->error_subcode_2);
	} else {
		ql_dbg(ql_dbg_init, vha, 0x028a,
		    "%s: abort exchange done.\n", __func__);
	}

	/* send ABTS response */
	abts_rsp = (void *)rsp_els;
	memset(abts_rsp, 0, sizeof(*abts_rsp));
	abts_rsp->entry_type = ABTS_RSP_TYPE;
	abts_rsp->entry_count = 1;
	abts_rsp->nport_handle = abts->nport_handle;
	abts_rsp->vp_idx = abts->vp_idx;
	abts_rsp->sof_type = abts->sof_type & 0xf0;
	abts_rsp->rx_xch_addr = abts->rx_xch_addr;
	abts_rsp->d_id[0] = abts->s_id[0];
	abts_rsp->d_id[1] = abts->s_id[1];
	abts_rsp->d_id[2] = abts->s_id[2];
	abts_rsp->r_ctl = FC_ROUTING_BLD | FC_R_CTL_BLD_BA_ACC;
	abts_rsp->s_id[0] = abts->d_id[0];
	abts_rsp->s_id[1] = abts->d_id[1];
	abts_rsp->s_id[2] = abts->d_id[2];
	abts_rsp->cs_ctl = abts->cs_ctl;
	/* include flipping bit23 in fctl */
	fctl = ~(abts->f_ctl[2] | 0x7F) << 16 |
	    FC_F_CTL_LAST_SEQ | FC_F_CTL_END_SEQ | FC_F_CTL_SEQ_INIT;
	abts_rsp->f_ctl[0] = fctl >> 0 & 0xff;
	abts_rsp->f_ctl[1] = fctl >> 8 & 0xff;
	abts_rsp->f_ctl[2] = fctl >> 16 & 0xff;
	abts_rsp->type = FC_TYPE_BLD;
	abts_rsp->rx_id = abts->rx_id;
	abts_rsp->ox_id = abts->ox_id;
	abts_rsp->payload.ba_acc.aborted_rx_id = abts->rx_id;
	abts_rsp->payload.ba_acc.aborted_ox_id = abts->ox_id;
	abts_rsp->payload.ba_acc.high_seq_cnt = cpu_to_le16(~0);
	abts_rsp->rx_xch_addr_to_abort = abts->rx_xch_addr_to_abort;
	ql_dbg(ql_dbg_init, vha, 0x028b,
	    "Sending BA ACC response to ABTS %#x...\n",
	    abts->rx_xch_addr_to_abort);
	ql_dbg(ql_dbg_init + ql_dbg_verbose, vha, 0x028b,
	    "-------- ELS RSP -------\n");
	ql_dump_buffer(ql_dbg_init + ql_dbg_verbose, vha, 0x028b,
	    (uint8_t *)abts_rsp, sizeof(*abts_rsp));
	rval = qla2x00_issue_iocb(vha, abts_rsp, dma, 0);
	if (rval) {
		ql_log(ql_log_warn, vha, 0x028c,
		    "%s: iocb failed to execute -> %x\n", __func__, rval);
	} else if (abts_rsp->comp_status) {
		ql_log(ql_log_warn, vha, 0x028d,
		    "%s: iocb failed to complete -> completion=%#x subcode=(%#x,%#x)\n",
		    __func__, abts_rsp->comp_status,
		    abts_rsp->payload.error.subcode1,
		    abts_rsp->payload.error.subcode2);
	} else {
		ql_dbg(ql_dbg_init, vha, 0x028ea,
		    "%s: done.\n", __func__);
	}

	dma_free_coherent(&ha->pdev->dev, sizeof(*rsp_els), rsp_els, dma);
}

/**
 * qla2100_intr_handler() - Process interrupts for the ISP2100 and ISP2200.
 * @irq: interrupt number
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
	uint16_t	mb[8];
	struct rsp_que *rsp;
	unsigned long	flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		ql_log(ql_log_info, NULL, 0x505d,
		    "%s: NULL response queue pointer.\n", __func__);
		return (IRQ_NONE);
	}

	ha = rsp->hw;
	reg = &ha->iobase->isp;
	status = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	for (iter = 50; iter--; ) {
		hccr = rd_reg_word(&reg->hccr);
		if (qla2x00_check_reg16_for_disconnect(vha, hccr))
			break;
		if (hccr & HCCR_RISC_PAUSE) {
			if (pci_channel_offline(ha->pdev))
				break;

			/*
			 * Issue a "HARD" reset in order for the RISC interrupt
			 * bit to be cleared.  Schedule a big hammer to get
			 * out of the RISC PAUSED state.
			 */
			wrt_reg_word(&reg->hccr, HCCR_RESET_RISC);
			rd_reg_word(&reg->hccr);

			ha->isp_ops->fw_dump(vha);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			break;
		} else if ((rd_reg_word(&reg->istatus) & ISR_RISC_INT) == 0)
			break;

		if (rd_reg_word(&reg->semaphore) & BIT_0) {
			wrt_reg_word(&reg->hccr, HCCR_CLR_RISC_INT);
			rd_reg_word(&reg->hccr);

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
			wrt_reg_word(&reg->semaphore, 0);
			rd_reg_word(&reg->semaphore);
		} else {
			qla2x00_process_response_queue(rsp);

			wrt_reg_word(&reg->hccr, HCCR_CLR_RISC_INT);
			rd_reg_word(&reg->hccr);
		}
	}
	qla2x00_handle_mbx_completion(ha, status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (IRQ_HANDLED);
}

bool
qla2x00_check_reg32_for_disconnect(scsi_qla_host_t *vha, uint32_t reg)
{
	/* Check for PCI disconnection */
	if (reg == 0xffffffff && !pci_channel_offline(vha->hw->pdev)) {
		if (!test_and_set_bit(PFLG_DISCONNECTED, &vha->pci_flags) &&
		    !test_bit(PFLG_DRIVER_REMOVING, &vha->pci_flags) &&
		    !test_bit(PFLG_DRIVER_PROBING, &vha->pci_flags)) {
			/*
			 * Schedule this (only once) on the default system
			 * workqueue so that all the adapter workqueues and the
			 * DPC thread can be shutdown cleanly.
			 */
			schedule_work(&vha->hw->board_disable);
		}
		return true;
	} else
		return false;
}

bool
qla2x00_check_reg16_for_disconnect(scsi_qla_host_t *vha, uint16_t reg)
{
	return qla2x00_check_reg32_for_disconnect(vha, 0xffff0000 | reg);
}

/**
 * qla2300_intr_handler() - Process interrupts for the ISP23xx and ISP63xx.
 * @irq: interrupt number
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
	uint16_t	mb[8];
	struct rsp_que *rsp;
	struct qla_hw_data *ha;
	unsigned long	flags;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		ql_log(ql_log_info, NULL, 0x5058,
		    "%s: NULL response queue pointer.\n", __func__);
		return (IRQ_NONE);
	}

	ha = rsp->hw;
	reg = &ha->iobase->isp;
	status = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	for (iter = 50; iter--; ) {
		stat = rd_reg_dword(&reg->u.isp2300.host_status);
		if (qla2x00_check_reg32_for_disconnect(vha, stat))
			break;
		if (stat & HSR_RISC_PAUSED) {
			if (unlikely(pci_channel_offline(ha->pdev)))
				break;

			hccr = rd_reg_word(&reg->hccr);

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
			wrt_reg_word(&reg->hccr, HCCR_RESET_RISC);
			rd_reg_word(&reg->hccr);

			ha->isp_ops->fw_dump(vha);
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
			wrt_reg_word(&reg->semaphore, 0);
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
		wrt_reg_word(&reg->hccr, HCCR_CLR_RISC_INT);
		rd_reg_word_relaxed(&reg->hccr);
	}
	qla2x00_handle_mbx_completion(ha, status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (IRQ_HANDLED);
}

/**
 * qla2x00_mbx_completion() - Process mailbox command completions.
 * @vha: SCSI driver HA context
 * @mb0: Mailbox0 register
 */
static void
qla2x00_mbx_completion(scsi_qla_host_t *vha, uint16_t mb0)
{
	uint16_t	cnt;
	uint32_t	mboxes;
	__le16 __iomem *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Read all mbox registers? */
	WARN_ON_ONCE(ha->mbx_count > 32);
	mboxes = (1ULL << ha->mbx_count) - 1;
	if (!ha->mcp)
		ql_dbg(ql_dbg_async, vha, 0x5001, "MBX pointer ERROR.\n");
	else
		mboxes = ha->mcp->in_mb;

	/* Load return mailbox registers. */
	ha->flags.mbox_int = 1;
	ha->mailbox_out[0] = mb0;
	mboxes >>= 1;
	wptr = MAILBOX_REG(ha, reg, 1);

	for (cnt = 1; cnt < ha->mbx_count; cnt++) {
		if (IS_QLA2200(ha) && cnt == 8)
			wptr = MAILBOX_REG(ha, reg, 8);
		if ((cnt == 4 || cnt == 5) && (mboxes & BIT_0))
			ha->mailbox_out[cnt] = qla2x00_debounce_register(wptr);
		else if (mboxes & BIT_0)
			ha->mailbox_out[cnt] = rd_reg_word(wptr);

		wptr++;
		mboxes >>= 1;
	}
}

static void
qla81xx_idc_event(scsi_qla_host_t *vha, uint16_t aen, uint16_t descr)
{
	static char *event[] =
		{ "Complete", "Request Notification", "Time Extension" };
	int rval;
	struct device_reg_24xx __iomem *reg24 = &vha->hw->iobase->isp24;
	struct device_reg_82xx __iomem *reg82 = &vha->hw->iobase->isp82;
	__le16 __iomem *wptr;
	uint16_t cnt, timeout, mb[QLA_IDC_ACK_REGS];

	/* Seed data -- mailbox1 -> mailbox7. */
	if (IS_QLA81XX(vha->hw) || IS_QLA83XX(vha->hw))
		wptr = &reg24->mailbox1;
	else if (IS_QLA8044(vha->hw))
		wptr = &reg82->mailbox_out[1];
	else
		return;

	for (cnt = 0; cnt < QLA_IDC_ACK_REGS; cnt++, wptr++)
		mb[cnt] = rd_reg_word(wptr);

	ql_dbg(ql_dbg_async, vha, 0x5021,
	    "Inter-Driver Communication %s -- "
	    "%04x %04x %04x %04x %04x %04x %04x.\n",
	    event[aen & 0xff], mb[0], mb[1], mb[2], mb[3],
	    mb[4], mb[5], mb[6]);
	switch (aen) {
	/* Handle IDC Error completion case. */
	case MBA_IDC_COMPLETE:
		if (mb[1] >> 15) {
			vha->hw->flags.idc_compl_status = 1;
			if (vha->hw->notify_dcbx_comp && !vha->vp_idx)
				complete(&vha->hw->dcbx_comp);
		}
		break;

	case MBA_IDC_NOTIFY:
		/* Acknowledgement needed? [Notify && non-zero timeout]. */
		timeout = (descr >> 8) & 0xf;
		ql_dbg(ql_dbg_async, vha, 0x5022,
		    "%lu Inter-Driver Communication %s -- ACK timeout=%d.\n",
		    vha->host_no, event[aen & 0xff], timeout);

		if (!timeout)
			return;
		rval = qla2x00_post_idc_ack_work(vha, mb);
		if (rval != QLA_SUCCESS)
			ql_log(ql_log_warn, vha, 0x5023,
			    "IDC failed to post ACK.\n");
		break;
	case MBA_IDC_TIME_EXT:
		vha->hw->idc_extend_tmo = descr;
		ql_dbg(ql_dbg_async, vha, 0x5087,
		    "%lu Inter-Driver Communication %s -- "
		    "Extend timeout by=%d.\n",
		    vha->host_no, event[aen & 0xff], vha->hw->idc_extend_tmo);
		break;
	}
}

#define LS_UNKNOWN	2
const char *
qla2x00_get_link_speed_str(struct qla_hw_data *ha, uint16_t speed)
{
	static const char *const link_speeds[] = {
		"1", "2", "?", "4", "8", "16", "32", "10"
	};
#define	QLA_LAST_SPEED (ARRAY_SIZE(link_speeds) - 1)

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		return link_speeds[0];
	else if (speed == 0x13)
		return link_speeds[QLA_LAST_SPEED];
	else if (speed < QLA_LAST_SPEED)
		return link_speeds[speed];
	else
		return link_speeds[LS_UNKNOWN];
}

static void
qla83xx_handle_8200_aen(scsi_qla_host_t *vha, uint16_t *mb)
{
	struct qla_hw_data *ha = vha->hw;

	/*
	 * 8200 AEN Interpretation:
	 * mb[0] = AEN code
	 * mb[1] = AEN Reason code
	 * mb[2] = LSW of Peg-Halt Status-1 Register
	 * mb[6] = MSW of Peg-Halt Status-1 Register
	 * mb[3] = LSW of Peg-Halt Status-2 register
	 * mb[7] = MSW of Peg-Halt Status-2 register
	 * mb[4] = IDC Device-State Register value
	 * mb[5] = IDC Driver-Presence Register value
	 */
	ql_dbg(ql_dbg_async, vha, 0x506b, "AEN Code: mb[0] = 0x%x AEN reason: "
	    "mb[1] = 0x%x PH-status1: mb[2] = 0x%x PH-status1: mb[6] = 0x%x.\n",
	    mb[0], mb[1], mb[2], mb[6]);
	ql_dbg(ql_dbg_async, vha, 0x506c, "PH-status2: mb[3] = 0x%x "
	    "PH-status2: mb[7] = 0x%x Device-State: mb[4] = 0x%x "
	    "Drv-Presence: mb[5] = 0x%x.\n", mb[3], mb[7], mb[4], mb[5]);

	if (mb[1] & (IDC_PEG_HALT_STATUS_CHANGE | IDC_NIC_FW_REPORTED_FAILURE |
				IDC_HEARTBEAT_FAILURE)) {
		ha->flags.nic_core_hung = 1;
		ql_log(ql_log_warn, vha, 0x5060,
		    "83XX: F/W Error Reported: Check if reset required.\n");

		if (mb[1] & IDC_PEG_HALT_STATUS_CHANGE) {
			uint32_t protocol_engine_id, fw_err_code, err_level;

			/*
			 * IDC_PEG_HALT_STATUS_CHANGE interpretation:
			 *  - PEG-Halt Status-1 Register:
			 *	(LSW = mb[2], MSW = mb[6])
			 *	Bits 0-7   = protocol-engine ID
			 *	Bits 8-28  = f/w error code
			 *	Bits 29-31 = Error-level
			 *	    Error-level 0x1 = Non-Fatal error
			 *	    Error-level 0x2 = Recoverable Fatal error
			 *	    Error-level 0x4 = UnRecoverable Fatal error
			 *  - PEG-Halt Status-2 Register:
			 *	(LSW = mb[3], MSW = mb[7])
			 */
			protocol_engine_id = (mb[2] & 0xff);
			fw_err_code = (((mb[2] & 0xff00) >> 8) |
			    ((mb[6] & 0x1fff) << 8));
			err_level = ((mb[6] & 0xe000) >> 13);
			ql_log(ql_log_warn, vha, 0x5061, "PegHalt Status-1 "
			    "Register: protocol_engine_id=0x%x "
			    "fw_err_code=0x%x err_level=0x%x.\n",
			    protocol_engine_id, fw_err_code, err_level);
			ql_log(ql_log_warn, vha, 0x5062, "PegHalt Status-2 "
			    "Register: 0x%x%x.\n", mb[7], mb[3]);
			if (err_level == ERR_LEVEL_NON_FATAL) {
				ql_log(ql_log_warn, vha, 0x5063,
				    "Not a fatal error, f/w has recovered itself.\n");
			} else if (err_level == ERR_LEVEL_RECOVERABLE_FATAL) {
				ql_log(ql_log_fatal, vha, 0x5064,
				    "Recoverable Fatal error: Chip reset "
				    "required.\n");
				qla83xx_schedule_work(vha,
				    QLA83XX_NIC_CORE_RESET);
			} else if (err_level == ERR_LEVEL_UNRECOVERABLE_FATAL) {
				ql_log(ql_log_fatal, vha, 0x5065,
				    "Unrecoverable Fatal error: Set FAILED "
				    "state, reboot required.\n");
				qla83xx_schedule_work(vha,
				    QLA83XX_NIC_CORE_UNRECOVERABLE);
			}
		}

		if (mb[1] & IDC_NIC_FW_REPORTED_FAILURE) {
			uint16_t peg_fw_state, nw_interface_link_up;
			uint16_t nw_interface_signal_detect, sfp_status;
			uint16_t htbt_counter, htbt_monitor_enable;
			uint16_t sfp_additional_info, sfp_multirate;
			uint16_t sfp_tx_fault, link_speed, dcbx_status;

			/*
			 * IDC_NIC_FW_REPORTED_FAILURE interpretation:
			 *  - PEG-to-FC Status Register:
			 *	(LSW = mb[2], MSW = mb[6])
			 *	Bits 0-7   = Peg-Firmware state
			 *	Bit 8      = N/W Interface Link-up
			 *	Bit 9      = N/W Interface signal detected
			 *	Bits 10-11 = SFP Status
			 *	  SFP Status 0x0 = SFP+ transceiver not expected
			 *	  SFP Status 0x1 = SFP+ transceiver not present
			 *	  SFP Status 0x2 = SFP+ transceiver invalid
			 *	  SFP Status 0x3 = SFP+ transceiver present and
			 *	  valid
			 *	Bits 12-14 = Heartbeat Counter
			 *	Bit 15     = Heartbeat Monitor Enable
			 *	Bits 16-17 = SFP Additional Info
			 *	  SFP info 0x0 = Unregocnized transceiver for
			 *	  Ethernet
			 *	  SFP info 0x1 = SFP+ brand validation failed
			 *	  SFP info 0x2 = SFP+ speed validation failed
			 *	  SFP info 0x3 = SFP+ access error
			 *	Bit 18     = SFP Multirate
			 *	Bit 19     = SFP Tx Fault
			 *	Bits 20-22 = Link Speed
			 *	Bits 23-27 = Reserved
			 *	Bits 28-30 = DCBX Status
			 *	  DCBX Status 0x0 = DCBX Disabled
			 *	  DCBX Status 0x1 = DCBX Enabled
			 *	  DCBX Status 0x2 = DCBX Exchange error
			 *	Bit 31     = Reserved
			 */
			peg_fw_state = (mb[2] & 0x00ff);
			nw_interface_link_up = ((mb[2] & 0x0100) >> 8);
			nw_interface_signal_detect = ((mb[2] & 0x0200) >> 9);
			sfp_status = ((mb[2] & 0x0c00) >> 10);
			htbt_counter = ((mb[2] & 0x7000) >> 12);
			htbt_monitor_enable = ((mb[2] & 0x8000) >> 15);
			sfp_additional_info = (mb[6] & 0x0003);
			sfp_multirate = ((mb[6] & 0x0004) >> 2);
			sfp_tx_fault = ((mb[6] & 0x0008) >> 3);
			link_speed = ((mb[6] & 0x0070) >> 4);
			dcbx_status = ((mb[6] & 0x7000) >> 12);

			ql_log(ql_log_warn, vha, 0x5066,
			    "Peg-to-Fc Status Register:\n"
			    "peg_fw_state=0x%x, nw_interface_link_up=0x%x, "
			    "nw_interface_signal_detect=0x%x"
			    "\nsfp_statis=0x%x.\n ", peg_fw_state,
			    nw_interface_link_up, nw_interface_signal_detect,
			    sfp_status);
			ql_log(ql_log_warn, vha, 0x5067,
			    "htbt_counter=0x%x, htbt_monitor_enable=0x%x, "
			    "sfp_additional_info=0x%x, sfp_multirate=0x%x.\n ",
			    htbt_counter, htbt_monitor_enable,
			    sfp_additional_info, sfp_multirate);
			ql_log(ql_log_warn, vha, 0x5068,
			    "sfp_tx_fault=0x%x, link_state=0x%x, "
			    "dcbx_status=0x%x.\n", sfp_tx_fault, link_speed,
			    dcbx_status);

			qla83xx_schedule_work(vha, QLA83XX_NIC_CORE_RESET);
		}

		if (mb[1] & IDC_HEARTBEAT_FAILURE) {
			ql_log(ql_log_warn, vha, 0x5069,
			    "Heartbeat Failure encountered, chip reset "
			    "required.\n");

			qla83xx_schedule_work(vha, QLA83XX_NIC_CORE_RESET);
		}
	}

	if (mb[1] & IDC_DEVICE_STATE_CHANGE) {
		ql_log(ql_log_info, vha, 0x506a,
		    "IDC Device-State changed = 0x%x.\n", mb[4]);
		if (ha->flags.nic_core_reset_owner)
			return;
		qla83xx_schedule_work(vha, MBA_IDC_AEN);
	}
}

int
qla2x00_is_a_vp_did(scsi_qla_host_t *vha, uint32_t rscn_entry)
{
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *vp;
	uint32_t vp_did;
	unsigned long flags;
	int ret = 0;

	if (!ha->num_vhosts)
		return ret;

	spin_lock_irqsave(&ha->vport_slock, flags);
	list_for_each_entry(vp, &ha->vp_list, list) {
		vp_did = vp->d_id.b24;
		if (vp_did == rscn_entry) {
			ret = 1;
			break;
		}
	}
	spin_unlock_irqrestore(&ha->vport_slock, flags);

	return ret;
}

fc_port_t *
qla2x00_find_fcport_by_loopid(scsi_qla_host_t *vha, uint16_t loop_id)
{
	fc_port_t *f, *tf;

	f = tf = NULL;
	list_for_each_entry_safe(f, tf, &vha->vp_fcports, list)
		if (f->loop_id == loop_id)
			return f;
	return NULL;
}

fc_port_t *
qla2x00_find_fcport_by_wwpn(scsi_qla_host_t *vha, u8 *wwpn, u8 incl_deleted)
{
	fc_port_t *f, *tf;

	f = tf = NULL;
	list_for_each_entry_safe(f, tf, &vha->vp_fcports, list) {
		if (memcmp(f->port_name, wwpn, WWN_SIZE) == 0) {
			if (incl_deleted)
				return f;
			else if (f->deleted == 0)
				return f;
		}
	}
	return NULL;
}

fc_port_t *
qla2x00_find_fcport_by_nportid(scsi_qla_host_t *vha, port_id_t *id,
	u8 incl_deleted)
{
	fc_port_t *f, *tf;

	f = tf = NULL;
	list_for_each_entry_safe(f, tf, &vha->vp_fcports, list) {
		if (f->d_id.b24 == id->b24) {
			if (incl_deleted)
				return f;
			else if (f->deleted == 0)
				return f;
		}
	}
	return NULL;
}

/* Shall be called only on supported adapters. */
static void
qla27xx_handle_8200_aen(scsi_qla_host_t *vha, uint16_t *mb)
{
	struct qla_hw_data *ha = vha->hw;
	bool reset_isp_needed = 0;

	ql_log(ql_log_warn, vha, 0x02f0,
	       "MPI Heartbeat stop. MPI reset is%s needed. "
	       "MB0[%xh] MB1[%xh] MB2[%xh] MB3[%xh]\n",
	       mb[1] & BIT_8 ? "" : " not",
	       mb[0], mb[1], mb[2], mb[3]);

	if ((mb[1] & BIT_8) == 0)
		return;

	ql_log(ql_log_warn, vha, 0x02f1,
	       "MPI Heartbeat stop. FW dump needed\n");

	if (ql2xfulldump_on_mpifail) {
		ha->isp_ops->fw_dump(vha);
		reset_isp_needed = 1;
	}

	ha->isp_ops->mpi_fw_dump(vha, 1);

	if (reset_isp_needed) {
		vha->hw->flags.fw_init_done = 0;
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
	}
}

static struct purex_item *
qla24xx_alloc_purex_item(scsi_qla_host_t *vha, uint16_t size)
{
	struct purex_item *item = NULL;
	uint8_t item_hdr_size = sizeof(*item);

	if (size > QLA_DEFAULT_PAYLOAD_SIZE) {
		item = kzalloc(item_hdr_size +
		    (size - QLA_DEFAULT_PAYLOAD_SIZE), GFP_ATOMIC);
	} else {
		if (atomic_inc_return(&vha->default_item.in_use) == 1) {
			item = &vha->default_item;
			goto initialize_purex_header;
		} else {
			item = kzalloc(item_hdr_size, GFP_ATOMIC);
		}
	}
	if (!item) {
		ql_log(ql_log_warn, vha, 0x5092,
		       ">> Failed allocate purex list item.\n");

		return NULL;
	}

initialize_purex_header:
	item->vha = vha;
	item->size = size;
	return item;
}

static void
qla24xx_queue_purex_item(scsi_qla_host_t *vha, struct purex_item *pkt,
			 void (*process_item)(struct scsi_qla_host *vha,
					      struct purex_item *pkt))
{
	struct purex_list *list = &vha->purex_list;
	ulong flags;

	pkt->process_item = process_item;

	spin_lock_irqsave(&list->lock, flags);
	list_add_tail(&pkt->list, &list->head);
	spin_unlock_irqrestore(&list->lock, flags);

	set_bit(PROCESS_PUREX_IOCB, &vha->dpc_flags);
}

/**
 * qla24xx_copy_std_pkt() - Copy over purex ELS which is
 * contained in a single IOCB.
 * purex packet.
 * @vha: SCSI driver HA context
 * @pkt: ELS packet
 */
static struct purex_item
*qla24xx_copy_std_pkt(struct scsi_qla_host *vha, void *pkt)
{
	struct purex_item *item;

	item = qla24xx_alloc_purex_item(vha,
					QLA_DEFAULT_PAYLOAD_SIZE);
	if (!item)
		return item;

	memcpy(&item->iocb, pkt, sizeof(item->iocb));
	return item;
}

/**
 * qla27xx_copy_fpin_pkt() - Copy over fpin packets that can
 * span over multiple IOCBs.
 * @vha: SCSI driver HA context
 * @pkt: ELS packet
 * @rsp: Response queue
 */
static struct purex_item *
qla27xx_copy_fpin_pkt(struct scsi_qla_host *vha, void **pkt,
		      struct rsp_que **rsp)
{
	struct purex_entry_24xx *purex = *pkt;
	struct rsp_que *rsp_q = *rsp;
	sts_cont_entry_t *new_pkt;
	uint16_t no_bytes = 0, total_bytes = 0, pending_bytes = 0;
	uint16_t buffer_copy_offset = 0;
	uint16_t entry_count, entry_count_remaining;
	struct purex_item *item;
	void *fpin_pkt = NULL;

	total_bytes = (le16_to_cpu(purex->frame_size) & 0x0FFF)
	    - PURX_ELS_HEADER_SIZE;
	pending_bytes = total_bytes;
	entry_count = entry_count_remaining = purex->entry_count;
	no_bytes = (pending_bytes > sizeof(purex->els_frame_payload))  ?
		   sizeof(purex->els_frame_payload) : pending_bytes;
	ql_log(ql_log_info, vha, 0x509a,
	       "FPIN ELS, frame_size 0x%x, entry count %d\n",
	       total_bytes, entry_count);

	item = qla24xx_alloc_purex_item(vha, total_bytes);
	if (!item)
		return item;

	fpin_pkt = &item->iocb;

	memcpy(fpin_pkt, &purex->els_frame_payload[0], no_bytes);
	buffer_copy_offset += no_bytes;
	pending_bytes -= no_bytes;
	--entry_count_remaining;

	((response_t *)purex)->signature = RESPONSE_PROCESSED;
	wmb();

	do {
		while ((total_bytes > 0) && (entry_count_remaining > 0)) {
			if (rsp_q->ring_ptr->signature == RESPONSE_PROCESSED) {
				ql_dbg(ql_dbg_async, vha, 0x5084,
				       "Ran out of IOCBs, partial data 0x%x\n",
				       buffer_copy_offset);
				cpu_relax();
				continue;
			}

			new_pkt = (sts_cont_entry_t *)rsp_q->ring_ptr;
			*pkt = new_pkt;

			if (new_pkt->entry_type != STATUS_CONT_TYPE) {
				ql_log(ql_log_warn, vha, 0x507a,
				       "Unexpected IOCB type, partial data 0x%x\n",
				       buffer_copy_offset);
				break;
			}

			rsp_q->ring_index++;
			if (rsp_q->ring_index == rsp_q->length) {
				rsp_q->ring_index = 0;
				rsp_q->ring_ptr = rsp_q->ring;
			} else {
				rsp_q->ring_ptr++;
			}
			no_bytes = (pending_bytes > sizeof(new_pkt->data)) ?
			    sizeof(new_pkt->data) : pending_bytes;
			if ((buffer_copy_offset + no_bytes) <= total_bytes) {
				memcpy(((uint8_t *)fpin_pkt +
				    buffer_copy_offset), new_pkt->data,
				    no_bytes);
				buffer_copy_offset += no_bytes;
				pending_bytes -= no_bytes;
				--entry_count_remaining;
			} else {
				ql_log(ql_log_warn, vha, 0x5044,
				       "Attempt to copy more that we got, optimizing..%x\n",
				       buffer_copy_offset);
				memcpy(((uint8_t *)fpin_pkt +
				    buffer_copy_offset), new_pkt->data,
				    total_bytes - buffer_copy_offset);
			}

			((response_t *)new_pkt)->signature = RESPONSE_PROCESSED;
			wmb();
		}

		if (pending_bytes != 0 || entry_count_remaining != 0) {
			ql_log(ql_log_fatal, vha, 0x508b,
			       "Dropping partial FPIN, underrun bytes = 0x%x, entry cnts 0x%x\n",
			       total_bytes, entry_count_remaining);
			qla24xx_free_purex_item(item);
			return NULL;
		}
	} while (entry_count_remaining > 0);
	host_to_fcp_swap((uint8_t *)&item->iocb, total_bytes);
	return item;
}

/**
 * qla2x00_async_event() - Process aynchronous events.
 * @vha: SCSI driver HA context
 * @rsp: response queue
 * @mb: Mailbox registers (0 - 3)
 */
void
qla2x00_async_event(scsi_qla_host_t *vha, struct rsp_que *rsp, uint16_t *mb)
{
	uint16_t	handle_cnt;
	uint16_t	cnt, mbx;
	uint32_t	handles[5];
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	struct device_reg_24xx __iomem *reg24 = &ha->iobase->isp24;
	struct device_reg_82xx __iomem *reg82 = &ha->iobase->isp82;
	uint32_t	rscn_entry, host_pid;
	unsigned long	flags;
	fc_port_t	*fcport = NULL;

	if (!vha->hw->flags.fw_started)
		return;

	/* Setup to process RIO completion. */
	handle_cnt = 0;
	if (IS_CNA_CAPABLE(ha))
		goto skip_rio;
	switch (mb[0]) {
	case MBA_SCSI_COMPLETION:
		handles[0] = make_handle(mb[2], mb[1]);
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
		handles[0] = make_handle(mb[2], mb[1]);
		handles[1] = make_handle(RD_MAILBOX_REG(ha, reg, 7),
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
		mbx = 0;
		if (IS_QLA81XX(ha) || IS_QLA83XX(ha) ||
		    IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
			u16 m[4];

			m[0] = rd_reg_word(&reg24->mailbox4);
			m[1] = rd_reg_word(&reg24->mailbox5);
			m[2] = rd_reg_word(&reg24->mailbox6);
			mbx = m[3] = rd_reg_word(&reg24->mailbox7);

			ql_log(ql_log_warn, vha, 0x5003,
			    "ISP System Error - mbx1=%xh mbx2=%xh mbx3=%xh mbx4=%xh mbx5=%xh mbx6=%xh mbx7=%xh.\n",
			    mb[1], mb[2], mb[3], m[0], m[1], m[2], m[3]);
		} else
			ql_log(ql_log_warn, vha, 0x5003,
			    "ISP System Error - mbx1=%xh mbx2=%xh mbx3=%xh.\n ",
			    mb[1], mb[2], mb[3]);

		if ((IS_QLA27XX(ha) || IS_QLA28XX(ha)) &&
		    rd_reg_word(&reg24->mailbox7) & BIT_8)
			ha->isp_ops->mpi_fw_dump(vha, 1);
		ha->isp_ops->fw_dump(vha);
		ha->flags.fw_init_done = 0;
		QLA_FW_STOPPED(ha);

		if (IS_FWI2_CAPABLE(ha)) {
			if (mb[1] == 0 && mb[2] == 0) {
				ql_log(ql_log_fatal, vha, 0x5004,
				    "Unrecoverable Hardware Error: adapter "
				    "marked OFFLINE!\n");
				vha->flags.online = 0;
				vha->device_flags |= DFLG_DEV_FAILED;
			} else {
				/* Check to see if MPI timeout occurred */
				if ((mbx & MBX_3) && (ha->port_no == 0))
					set_bit(MPI_RESET_NEEDED,
					    &vha->dpc_flags);

				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			}
		} else if (mb[1] == 0) {
			ql_log(ql_log_fatal, vha, 0x5005,
			    "Unrecoverable Hardware Error: adapter marked "
			    "OFFLINE!\n");
			vha->flags.online = 0;
			vha->device_flags |= DFLG_DEV_FAILED;
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
		    "ISP Response Transfer Error (%x).\n", mb[1]);

		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;

	case MBA_WAKEUP_THRES:		/* Request Queue Wake-up */
		ql_dbg(ql_dbg_async, vha, 0x5008,
		    "Asynchronous WAKEUP_THRES (%x).\n", mb[1]);
		break;

	case MBA_LOOP_INIT_ERR:
		ql_log(ql_log_warn, vha, 0x5090,
		    "LOOP INIT ERROR (%x).\n", mb[1]);
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		break;

	case MBA_LIP_OCCURRED:		/* Loop Initialization Procedure */
		ha->flags.lip_ae = 1;

		ql_dbg(ql_dbg_async, vha, 0x5009,
		    "LIP occurred (%x).\n", mb[1]);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
			qla2x00_mark_all_devices_lost(vha);
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
		if (IS_QLA2100(ha) || IS_QLA2200(ha))
			ha->link_data_rate = PORT_SPEED_1GB;
		else
			ha->link_data_rate = mb[1];

		ql_log(ql_log_info, vha, 0x500a,
		    "LOOP UP detected (%s Gbps).\n",
		    qla2x00_get_link_speed_str(ha, ha->link_data_rate));

		if (IS_QLA83XX(ha) || IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
			if (mb[2] & BIT_0)
				ql_log(ql_log_info, vha, 0x11a0,
				    "FEC=enabled (link up).\n");
		}

		vha->flags.management_server_logged_in = 0;
		qla2x00_post_aen_work(vha, FCH_EVT_LINKUP, ha->link_data_rate);

		break;

	case MBA_LOOP_DOWN:		/* Loop Down Event */
		SAVE_TOPO(ha);
		ha->flags.lip_ae = 0;
		ha->current_topology = 0;

		mbx = (IS_QLA81XX(ha) || IS_QLA8031(ha))
			? rd_reg_word(&reg24->mailbox4) : 0;
		mbx = (IS_P3P_TYPE(ha)) ? rd_reg_word(&reg82->mailbox_out[4])
			: mbx;
		ql_log(ql_log_info, vha, 0x500b,
		    "LOOP DOWN detected (%x %x %x %x).\n",
		    mb[1], mb[2], mb[3], mbx);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
			/*
			 * In case of loop down, restore WWPN from
			 * NVRAM in case of FA-WWPN capable ISP
			 * Restore for Physical Port only
			 */
			if (!vha->vp_idx) {
				if (ha->flags.fawwpn_enabled &&
				    (ha->current_topology == ISP_CFG_F)) {
					void *wwpn = ha->init_cb->port_name;

					memcpy(vha->port_name, wwpn, WWN_SIZE);
					fc_host_port_name(vha->host) =
					    wwn_to_u64(vha->port_name);
					ql_dbg(ql_dbg_init + ql_dbg_verbose,
					    vha, 0x00d8, "LOOP DOWN detected,"
					    "restore WWPN %016llx\n",
					    wwn_to_u64(vha->port_name));
				}

				clear_bit(VP_CONFIG_OK, &vha->vp_flags);
			}

			vha->device_flags |= DFLG_NO_CABLE;
			qla2x00_mark_all_devices_lost(vha);
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
		ql_dbg(ql_dbg_async, vha, 0x500c,
		    "LIP reset occurred (%x).\n", mb[1]);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
			qla2x00_mark_all_devices_lost(vha);
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
		ha->flags.lip_ae = 0;

		if (IS_QLA2100(ha))
			break;

		if (IS_CNA_CAPABLE(ha)) {
			ql_dbg(ql_dbg_async, vha, 0x500d,
			    "DCBX Completed -- %04x %04x %04x.\n",
			    mb[1], mb[2], mb[3]);
			if (ha->notify_dcbx_comp && !vha->vp_idx)
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
			if (!N2N_TOPO(ha))
				qla2x00_mark_all_devices_lost(vha);
		}

		if (vha->vp_idx) {
			atomic_set(&vha->vp_state, VP_FAILED);
			fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		}

		if (!(test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags)))
			set_bit(RESET_MARKER_NEEDED, &vha->dpc_flags);

		set_bit(REGISTER_FC4_NEEDED, &vha->dpc_flags);
		set_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags);

		vha->flags.management_server_logged_in = 0;
		break;

	case MBA_CHG_IN_CONNECTION:	/* Change in connection mode */
		if (IS_QLA2100(ha))
			break;

		ql_dbg(ql_dbg_async, vha, 0x500f,
		    "Configuration change detected: value=%x.\n", mb[1]);

		if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
			atomic_set(&vha->loop_state, LOOP_DOWN);
			if (!atomic_read(&vha->loop_down_timer))
				atomic_set(&vha->loop_down_timer,
				    LOOP_DOWN_TIME);
			qla2x00_mark_all_devices_lost(vha);
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

		if (mb[2] == 0x7) {
			ql_dbg(ql_dbg_async, vha, 0x5010,
			    "Port %s %04x %04x %04x.\n",
			    mb[1] == 0xffff ? "unavailable" : "logout",
			    mb[1], mb[2], mb[3]);

			if (mb[1] == 0xffff)
				goto global_port_update;

			if (mb[1] == NPH_SNS_LID(ha)) {
				set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
				break;
			}

			/* use handle_cnt for loop id/nport handle */
			if (IS_FWI2_CAPABLE(ha))
				handle_cnt = NPH_SNS;
			else
				handle_cnt = SIMPLE_NAME_SERVER;
			if (mb[1] == handle_cnt) {
				set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
				set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
				break;
			}

			/* Port logout */
			fcport = qla2x00_find_fcport_by_loopid(vha, mb[1]);
			if (!fcport)
				break;
			if (atomic_read(&fcport->state) != FCS_ONLINE)
				break;
			ql_dbg(ql_dbg_async, vha, 0x508a,
			    "Marking port lost loopid=%04x portid=%06x.\n",
			    fcport->loop_id, fcport->d_id.b24);
			if (qla_ini_mode_enabled(vha)) {
				fcport->logout_on_delete = 0;
				qlt_schedule_sess_for_deletion(fcport);
			}
			break;

global_port_update:
			if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
				atomic_set(&vha->loop_state, LOOP_DOWN);
				atomic_set(&vha->loop_down_timer,
				    LOOP_DOWN_TIME);
				vha->device_flags |= DFLG_NO_CABLE;
				qla2x00_mark_all_devices_lost(vha);
			}

			if (vha->vp_idx) {
				atomic_set(&vha->vp_state, VP_FAILED);
				fc_vport_set_state(vha->fc_vport,
				    FC_VPORT_FAILED);
				qla2x00_mark_all_devices_lost(vha);
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
			!ha->flags.n2n_ae  &&
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
		vha->scan.scan_retry = 0;

		set_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags);
		set_bit(LOCAL_LOOP_UPDATE, &vha->dpc_flags);
		set_bit(VP_CONFIG_OK, &vha->vp_flags);
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

		/* Skip RSCNs for virtual ports on the same physical port */
		if (qla2x00_is_a_vp_did(vha, rscn_entry))
			break;

		atomic_set(&vha->loop_down_timer, 0);
		vha->flags.management_server_logged_in = 0;
		{
			struct event_arg ea;

			memset(&ea, 0, sizeof(ea));
			ea.id.b24 = rscn_entry;
			ea.id.b.rsvd_1 = rscn_entry >> 24;
			qla2x00_handle_rscn(vha, &ea);
			qla2x00_post_aen_work(vha, FCH_EVT_RSCN, rscn_entry);
		}
		break;
	case MBA_CONGN_NOTI_RECV:
		if (!ha->flags.scm_enabled ||
		    mb[1] != QLA_CON_PRIMITIVE_RECEIVED)
			break;

		if (mb[2] == QLA_CONGESTION_ARB_WARNING) {
			ql_dbg(ql_dbg_async, vha, 0x509b,
			       "Congestion Warning %04x %04x.\n", mb[1], mb[2]);
		} else if (mb[2] == QLA_CONGESTION_ARB_ALARM) {
			ql_log(ql_log_warn, vha, 0x509b,
			       "Congestion Alarm %04x %04x.\n", mb[1], mb[2]);
		}
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
	case MBA_IDC_NOTIFY:
		if (IS_QLA8031(vha->hw) || IS_QLA8044(ha)) {
			mb[4] = rd_reg_word(&reg24->mailbox4);
			if (((mb[2] & 0x7fff) == MBC_PORT_RESET ||
			    (mb[2] & 0x7fff) == MBC_SET_PORT_CONFIG) &&
			    (mb[4] & INTERNAL_LOOPBACK_MASK) != 0) {
				set_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags);
				/*
				 * Extend loop down timer since port is active.
				 */
				if (atomic_read(&vha->loop_state) == LOOP_DOWN)
					atomic_set(&vha->loop_down_timer,
					    LOOP_DOWN_TIME);
				qla2xxx_wake_dpc(vha);
			}
		}
		/* fall through */
	case MBA_IDC_COMPLETE:
		if (ha->notify_lb_portup_comp && !vha->vp_idx)
			complete(&ha->lb_portup_comp);
		/* Fallthru */
	case MBA_IDC_TIME_EXT:
		if (IS_QLA81XX(vha->hw) || IS_QLA8031(vha->hw) ||
		    IS_QLA8044(ha))
			qla81xx_idc_event(vha, mb[0], mb[1]);
		break;

	case MBA_IDC_AEN:
		if (IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
			qla27xx_handle_8200_aen(vha, mb);
		} else if (IS_QLA83XX(ha)) {
			mb[4] = rd_reg_word(&reg24->mailbox4);
			mb[5] = rd_reg_word(&reg24->mailbox5);
			mb[6] = rd_reg_word(&reg24->mailbox6);
			mb[7] = rd_reg_word(&reg24->mailbox7);
			qla83xx_handle_8200_aen(vha, mb);
		} else {
			ql_dbg(ql_dbg_async, vha, 0x5052,
			    "skip Heartbeat processing mb0-3=[0x%04x] [0x%04x] [0x%04x] [0x%04x]\n",
			    mb[0], mb[1], mb[2], mb[3]);
		}
		break;

	case MBA_DPORT_DIAGNOSTICS:
		ql_dbg(ql_dbg_async, vha, 0x5052,
		    "D-Port Diagnostics: %04x %04x %04x %04x\n",
		    mb[0], mb[1], mb[2], mb[3]);
		memcpy(vha->dport_data, mb, sizeof(vha->dport_data));
		if (IS_QLA83XX(ha) || IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
			static char *results[] = {
			    "start", "done(pass)", "done(error)", "undefined" };
			static char *types[] = {
			    "none", "dynamic", "static", "other" };
			uint result = mb[1] >> 0 & 0x3;
			uint type = mb[1] >> 6 & 0x3;
			uint sw = mb[1] >> 15 & 0x1;
			ql_dbg(ql_dbg_async, vha, 0x5052,
			    "D-Port Diagnostics: result=%s type=%s [sw=%u]\n",
			    results[result], types[type], sw);
			if (result == 2) {
				static char *reasons[] = {
				    "reserved", "unexpected reject",
				    "unexpected phase", "retry exceeded",
				    "timed out", "not supported",
				    "user stopped" };
				uint reason = mb[2] >> 0 & 0xf;
				uint phase = mb[2] >> 12 & 0xf;
				ql_dbg(ql_dbg_async, vha, 0x5052,
				    "D-Port Diagnostics: reason=%s phase=%u \n",
				    reason < 7 ? reasons[reason] : "other",
				    phase >> 1);
			}
		}
		break;

	case MBA_TEMPERATURE_ALERT:
		ql_dbg(ql_dbg_async, vha, 0x505e,
		    "TEMPERATURE ALERT: %04x %04x %04x\n", mb[1], mb[2], mb[3]);
		if (mb[1] == 0x12)
			schedule_work(&ha->board_disable);
		break;

	case MBA_TRANS_INSERT:
		ql_dbg(ql_dbg_async, vha, 0x5091,
		    "Transceiver Insertion: %04x\n", mb[1]);
		set_bit(DETECT_SFP_CHANGE, &vha->dpc_flags);
		break;

	case MBA_TRANS_REMOVE:
		ql_dbg(ql_dbg_async, vha, 0x5091, "Transceiver Removal\n");
		break;

	default:
		ql_dbg(ql_dbg_async, vha, 0x5057,
		    "Unknown AEN:%04x %04x %04x %04x\n",
		    mb[0], mb[1], mb[2], mb[3]);
	}

	qlt_async_event(mb[0], vha, mb);

	if (!vha->vp_idx && ha->num_vhosts)
		qla2x00_alert_all_vps(rsp, mb);
}

/**
 * qla2x00_process_completed_request() - Process a Fast Post response.
 * @vha: SCSI driver HA context
 * @req: request queue
 * @index: SRB index
 */
void
qla2x00_process_completed_request(struct scsi_qla_host *vha,
				  struct req_que *req, uint32_t index)
{
	srb_t *sp;
	struct qla_hw_data *ha = vha->hw;

	/* Validate handle. */
	if (index >= req->num_outstanding_cmds) {
		ql_log(ql_log_warn, vha, 0x3014,
		    "Invalid SCSI command index (%x).\n", index);

		if (IS_P3P_TYPE(ha))
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
		sp->done(sp, DID_OK << 16);
	} else {
		ql_log(ql_log_warn, vha, 0x3016, "Invalid SCSI SRB.\n");

		if (IS_P3P_TYPE(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	}
}

srb_t *
qla2x00_get_sp_from_handle(scsi_qla_host_t *vha, const char *func,
    struct req_que *req, void *iocb)
{
	struct qla_hw_data *ha = vha->hw;
	sts_entry_t *pkt = iocb;
	srb_t *sp;
	uint16_t index;

	index = LSW(pkt->handle);
	if (index >= req->num_outstanding_cmds) {
		ql_log(ql_log_warn, vha, 0x5031,
			   "%s: Invalid command index (%x) type %8ph.\n",
			   func, index, iocb);
		if (IS_P3P_TYPE(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		return NULL;
	}
	sp = req->outstanding_cmds[index];
	if (!sp) {
		ql_log(ql_log_warn, vha, 0x5032,
			"%s: Invalid completion handle (%x) -- timed-out.\n",
			func, index);
		return NULL;
	}
	if (sp->handle != index) {
		ql_log(ql_log_warn, vha, 0x5033,
			"%s: SRB handle (%x) mismatch %x.\n", func,
			sp->handle, index);
		return NULL;
	}

	req->outstanding_cmds[index] = NULL;
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
	uint16_t *data;
	uint16_t status;

	sp = qla2x00_get_sp_from_handle(vha, func, req, mbx);
	if (!sp)
		return;

	lio = &sp->u.iocb_cmd;
	type = sp->name;
	fcport = sp->fcport;
	data = lio->u.logio.data;

	data[0] = MBS_COMMAND_ERROR;
	data[1] = lio->u.logio.flags & SRB_LOGIN_RETRIED ?
	    QLA_LOGIO_LOGIN_RETRIED : 0;
	if (mbx->entry_status) {
		ql_dbg(ql_dbg_async, vha, 0x5043,
		    "Async-%s error entry - hdl=%x portid=%02x%02x%02x "
		    "entry-status=%x status=%x state-flag=%x "
		    "status-flags=%x.\n", type, sp->handle,
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, mbx->entry_status,
		    le16_to_cpu(mbx->status), le16_to_cpu(mbx->state_flags),
		    le16_to_cpu(mbx->status_flags));

		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x5029,
		    mbx, sizeof(*mbx));

		goto logio_done;
	}

	status = le16_to_cpu(mbx->status);
	if (status == 0x30 && sp->type == SRB_LOGIN_CMD &&
	    le16_to_cpu(mbx->mb0) == MBS_COMMAND_COMPLETE)
		status = 0;
	if (!status && le16_to_cpu(mbx->mb0) == MBS_COMMAND_COMPLETE) {
		ql_dbg(ql_dbg_async, vha, 0x5045,
		    "Async-%s complete - hdl=%x portid=%02x%02x%02x mbx1=%x.\n",
		    type, sp->handle, fcport->d_id.b.domain,
		    fcport->d_id.b.area, fcport->d_id.b.al_pa,
		    le16_to_cpu(mbx->mb1));

		data[0] = MBS_COMMAND_COMPLETE;
		if (sp->type == SRB_LOGIN_CMD) {
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
	    "Async-%s failed - hdl=%x portid=%02x%02x%02x status=%x "
	    "mb0=%x mb1=%x mb2=%x mb6=%x mb7=%x.\n", type, sp->handle,
	    fcport->d_id.b.domain, fcport->d_id.b.area, fcport->d_id.b.al_pa,
	    status, le16_to_cpu(mbx->mb0), le16_to_cpu(mbx->mb1),
	    le16_to_cpu(mbx->mb2), le16_to_cpu(mbx->mb6),
	    le16_to_cpu(mbx->mb7));

logio_done:
	sp->done(sp, 0);
}

static void
qla24xx_mbx_iocb_entry(scsi_qla_host_t *vha, struct req_que *req,
    struct mbx_24xx_entry *pkt)
{
	const char func[] = "MBX-IOCB2";
	srb_t *sp;
	struct srb_iocb *si;
	u16 sz, i;
	int res;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;

	si = &sp->u.iocb_cmd;
	sz = min(ARRAY_SIZE(pkt->mb), ARRAY_SIZE(sp->u.iocb_cmd.u.mbx.in_mb));

	for (i = 0; i < sz; i++)
		si->u.mbx.in_mb[i] = pkt->mb[i];

	res = (si->u.mbx.in_mb[0] & MBS_MASK);

	sp->done(sp, res);
}

static void
qla24xxx_nack_iocb_entry(scsi_qla_host_t *vha, struct req_que *req,
    struct nack_to_isp *pkt)
{
	const char func[] = "nack";
	srb_t *sp;
	int res = 0;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;

	if (pkt->u.isp2x.status != cpu_to_le16(NOTIFY_ACK_SUCCESS))
		res = QLA_FUNCTION_FAILED;

	sp->done(sp, res);
}

static void
qla2x00_ct_entry(scsi_qla_host_t *vha, struct req_que *req,
    sts_entry_t *pkt, int iocb_type)
{
	const char func[] = "CT_IOCB";
	const char *type;
	srb_t *sp;
	struct bsg_job *bsg_job;
	struct fc_bsg_reply *bsg_reply;
	uint16_t comp_status;
	int res = 0;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;

	switch (sp->type) {
	case SRB_CT_CMD:
	    bsg_job = sp->u.bsg_job;
	    bsg_reply = bsg_job->reply;

	    type = "ct pass-through";

	    comp_status = le16_to_cpu(pkt->comp_status);

	    /*
	     * return FC_CTELS_STATUS_OK and leave the decoding of the ELS/CT
	     * fc payload  to the caller
	     */
	    bsg_reply->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	    bsg_job->reply_len = sizeof(struct fc_bsg_reply);

	    if (comp_status != CS_COMPLETE) {
		    if (comp_status == CS_DATA_UNDERRUN) {
			    res = DID_OK << 16;
			    bsg_reply->reply_payload_rcv_len =
				le16_to_cpu(pkt->rsp_info_len);

			    ql_log(ql_log_warn, vha, 0x5048,
				"CT pass-through-%s error comp_status=0x%x total_byte=0x%x.\n",
				type, comp_status,
				bsg_reply->reply_payload_rcv_len);
		    } else {
			    ql_log(ql_log_warn, vha, 0x5049,
				"CT pass-through-%s error comp_status=0x%x.\n",
				type, comp_status);
			    res = DID_ERROR << 16;
			    bsg_reply->reply_payload_rcv_len = 0;
		    }
		    ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x5035,
			pkt, sizeof(*pkt));
	    } else {
		    res = DID_OK << 16;
		    bsg_reply->reply_payload_rcv_len =
			bsg_job->reply_payload.payload_len;
		    bsg_job->reply_len = 0;
	    }
	    break;
	case SRB_CT_PTHRU_CMD:
	    /*
	     * borrowing sts_entry_24xx.comp_status.
	     * same location as ct_entry_24xx.comp_status
	     */
	     res = qla2x00_chk_ms_status(vha, (ms_iocb_entry_t *)pkt,
		 (struct ct_sns_rsp *)sp->u.iocb_cmd.u.ctarg.rsp,
		 sp->name);
	     break;
	}

	sp->done(sp, res);
}

static void
qla24xx_els_ct_entry(scsi_qla_host_t *vha, struct req_que *req,
    struct sts_entry_24xx *pkt, int iocb_type)
{
	struct els_sts_entry_24xx *ese = (struct els_sts_entry_24xx *)pkt;
	const char func[] = "ELS_CT_IOCB";
	const char *type;
	srb_t *sp;
	struct bsg_job *bsg_job;
	struct fc_bsg_reply *bsg_reply;
	uint16_t comp_status;
	uint32_t fw_status[3];
	int res;
	struct srb_iocb *els;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;

	type = NULL;
	switch (sp->type) {
	case SRB_ELS_CMD_RPT:
	case SRB_ELS_CMD_HST:
		type = "els";
		break;
	case SRB_CT_CMD:
		type = "ct pass-through";
		break;
	case SRB_ELS_DCMD:
		type = "Driver ELS logo";
		if (iocb_type != ELS_IOCB_TYPE) {
			ql_dbg(ql_dbg_user, vha, 0x5047,
			    "Completing %s: (%p) type=%d.\n",
			    type, sp, sp->type);
			sp->done(sp, 0);
			return;
		}
		break;
	case SRB_CT_PTHRU_CMD:
		/* borrowing sts_entry_24xx.comp_status.
		   same location as ct_entry_24xx.comp_status
		 */
		res = qla2x00_chk_ms_status(sp->vha, (ms_iocb_entry_t *)pkt,
			(struct ct_sns_rsp *)sp->u.iocb_cmd.u.ctarg.rsp,
			sp->name);
		sp->done(sp, res);
		return;
	default:
		ql_dbg(ql_dbg_user, vha, 0x503e,
		    "Unrecognized SRB: (%p) type=%d.\n", sp, sp->type);
		return;
	}

	comp_status = fw_status[0] = le16_to_cpu(pkt->comp_status);
	fw_status[1] = le32_to_cpu(ese->error_subcode_1);
	fw_status[2] = le32_to_cpu(ese->error_subcode_2);

	if (iocb_type == ELS_IOCB_TYPE) {
		els = &sp->u.iocb_cmd;
		els->u.els_plogi.fw_status[0] = cpu_to_le32(fw_status[0]);
		els->u.els_plogi.fw_status[1] = cpu_to_le32(fw_status[1]);
		els->u.els_plogi.fw_status[2] = cpu_to_le32(fw_status[2]);
		els->u.els_plogi.comp_status = cpu_to_le16(fw_status[0]);
		if (comp_status == CS_COMPLETE) {
			res =  DID_OK << 16;
		} else {
			if (comp_status == CS_DATA_UNDERRUN) {
				res =  DID_OK << 16;
				els->u.els_plogi.len = cpu_to_le16(le32_to_cpu(
					ese->total_byte_count));
			} else {
				els->u.els_plogi.len = 0;
				res = DID_ERROR << 16;
			}
		}
		ql_dbg(ql_dbg_disc, vha, 0x503f,
		    "ELS IOCB Done -%s hdl=%x comp_status=0x%x error subcode 1=0x%x error subcode 2=0x%x total_byte=0x%x\n",
		    type, sp->handle, comp_status, fw_status[1], fw_status[2],
		    le32_to_cpu(ese->total_byte_count));
		goto els_ct_done;
	}

	/* return FC_CTELS_STATUS_OK and leave the decoding of the ELS/CT
	 * fc payload  to the caller
	 */
	bsg_job = sp->u.bsg_job;
	bsg_reply = bsg_job->reply;
	bsg_reply->reply_data.ctels_reply.status = FC_CTELS_STATUS_OK;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply) + sizeof(fw_status);

	if (comp_status != CS_COMPLETE) {
		if (comp_status == CS_DATA_UNDERRUN) {
			res = DID_OK << 16;
			bsg_reply->reply_payload_rcv_len =
				le32_to_cpu(ese->total_byte_count);

			ql_dbg(ql_dbg_user, vha, 0x503f,
			    "ELS-CT pass-through-%s error hdl=%x comp_status-status=0x%x "
			    "error subcode 1=0x%x error subcode 2=0x%x total_byte = 0x%x.\n",
			    type, sp->handle, comp_status, fw_status[1], fw_status[2],
			    le32_to_cpu(ese->total_byte_count));
		} else {
			ql_dbg(ql_dbg_user, vha, 0x5040,
			    "ELS-CT pass-through-%s error hdl=%x comp_status-status=0x%x "
			    "error subcode 1=0x%x error subcode 2=0x%x.\n",
			    type, sp->handle, comp_status,
			    le32_to_cpu(ese->error_subcode_1),
			    le32_to_cpu(ese->error_subcode_2));
			res = DID_ERROR << 16;
			bsg_reply->reply_payload_rcv_len = 0;
		}
		memcpy(bsg_job->reply + sizeof(struct fc_bsg_reply),
		       fw_status, sizeof(fw_status));
		ql_dump_buffer(ql_dbg_user + ql_dbg_buffer, vha, 0x5056,
		    pkt, sizeof(*pkt));
	}
	else {
		res =  DID_OK << 16;
		bsg_reply->reply_payload_rcv_len = bsg_job->reply_payload.payload_len;
		bsg_job->reply_len = 0;
	}
els_ct_done:

	sp->done(sp, res);
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
	uint16_t *data;
	uint32_t iop[2];

	sp = qla2x00_get_sp_from_handle(vha, func, req, logio);
	if (!sp)
		return;

	lio = &sp->u.iocb_cmd;
	type = sp->name;
	fcport = sp->fcport;
	data = lio->u.logio.data;

	data[0] = MBS_COMMAND_ERROR;
	data[1] = lio->u.logio.flags & SRB_LOGIN_RETRIED ?
		QLA_LOGIO_LOGIN_RETRIED : 0;
	if (logio->entry_status) {
		ql_log(ql_log_warn, fcport->vha, 0x5034,
		    "Async-%s error entry - %8phC hdl=%x"
		    "portid=%02x%02x%02x entry-status=%x.\n",
		    type, fcport->port_name, sp->handle, fcport->d_id.b.domain,
		    fcport->d_id.b.area, fcport->d_id.b.al_pa,
		    logio->entry_status);
		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, vha, 0x504d,
		    logio, sizeof(*logio));

		goto logio_done;
	}

	if (le16_to_cpu(logio->comp_status) == CS_COMPLETE) {
		ql_dbg(ql_dbg_async, sp->vha, 0x5036,
		    "Async-%s complete: handle=%x pid=%06x wwpn=%8phC iop0=%x\n",
		    type, sp->handle, fcport->d_id.b24, fcport->port_name,
		    le32_to_cpu(logio->io_parameter[0]));

		vha->hw->exch_starvation = 0;
		data[0] = MBS_COMMAND_COMPLETE;

		if (sp->type == SRB_PRLI_CMD) {
			lio->u.logio.iop[0] =
			    le32_to_cpu(logio->io_parameter[0]);
			lio->u.logio.iop[1] =
			    le32_to_cpu(logio->io_parameter[1]);
			goto logio_done;
		}

		if (sp->type != SRB_LOGIN_CMD)
			goto logio_done;

		iop[0] = le32_to_cpu(logio->io_parameter[0]);
		if (iop[0] & BIT_4) {
			fcport->port_type = FCT_TARGET;
			if (iop[0] & BIT_8)
				fcport->flags |= FCF_FCP2_DEVICE;
		} else if (iop[0] & BIT_5)
			fcport->port_type = FCT_INITIATOR;

		if (iop[0] & BIT_7)
			fcport->flags |= FCF_CONF_COMP_SUPPORTED;

		if (logio->io_parameter[7] || logio->io_parameter[8])
			fcport->supported_classes |= FC_COS_CLASS2;
		if (logio->io_parameter[9] || logio->io_parameter[10])
			fcport->supported_classes |= FC_COS_CLASS3;

		goto logio_done;
	}

	iop[0] = le32_to_cpu(logio->io_parameter[0]);
	iop[1] = le32_to_cpu(logio->io_parameter[1]);
	lio->u.logio.iop[0] = iop[0];
	lio->u.logio.iop[1] = iop[1];
	switch (iop[0]) {
	case LSC_SCODE_PORTID_USED:
		data[0] = MBS_PORT_ID_USED;
		data[1] = LSW(iop[1]);
		break;
	case LSC_SCODE_NPORT_USED:
		data[0] = MBS_LOOP_ID_USED;
		break;
	case LSC_SCODE_CMD_FAILED:
		if (iop[1] == 0x0606) {
			/*
			 * PLOGI/PRLI Completed. We must have Recv PLOGI/PRLI,
			 * Target side acked.
			 */
			data[0] = MBS_COMMAND_COMPLETE;
			goto logio_done;
		}
		data[0] = MBS_COMMAND_ERROR;
		break;
	case LSC_SCODE_NOXCB:
		vha->hw->exch_starvation++;
		if (vha->hw->exch_starvation > 5) {
			ql_log(ql_log_warn, vha, 0xd046,
			    "Exchange starvation. Resetting RISC\n");

			vha->hw->exch_starvation = 0;

			if (IS_P3P_TYPE(vha->hw))
				set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
			else
				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		/* fall through */
	default:
		data[0] = MBS_COMMAND_ERROR;
		break;
	}

	ql_dbg(ql_dbg_async, sp->vha, 0x5037,
	    "Async-%s failed: handle=%x pid=%06x wwpn=%8phC comp_status=%x iop0=%x iop1=%x\n",
	    type, sp->handle, fcport->d_id.b24, fcport->port_name,
	    le16_to_cpu(logio->comp_status),
	    le32_to_cpu(logio->io_parameter[0]),
	    le32_to_cpu(logio->io_parameter[1]));

logio_done:
	sp->done(sp, 0);
}

static void
qla24xx_tm_iocb_entry(scsi_qla_host_t *vha, struct req_que *req, void *tsk)
{
	const char func[] = "TMF-IOCB";
	const char *type;
	fc_port_t *fcport;
	srb_t *sp;
	struct srb_iocb *iocb;
	struct sts_entry_24xx *sts = (struct sts_entry_24xx *)tsk;

	sp = qla2x00_get_sp_from_handle(vha, func, req, tsk);
	if (!sp)
		return;

	iocb = &sp->u.iocb_cmd;
	type = sp->name;
	fcport = sp->fcport;
	iocb->u.tmf.data = QLA_SUCCESS;

	if (sts->entry_status) {
		ql_log(ql_log_warn, fcport->vha, 0x5038,
		    "Async-%s error - hdl=%x entry-status(%x).\n",
		    type, sp->handle, sts->entry_status);
		iocb->u.tmf.data = QLA_FUNCTION_FAILED;
	} else if (sts->comp_status != cpu_to_le16(CS_COMPLETE)) {
		ql_log(ql_log_warn, fcport->vha, 0x5039,
		    "Async-%s error - hdl=%x completion status(%x).\n",
		    type, sp->handle, sts->comp_status);
		iocb->u.tmf.data = QLA_FUNCTION_FAILED;
	} else if ((le16_to_cpu(sts->scsi_status) &
	    SS_RESPONSE_INFO_LEN_VALID)) {
		if (le32_to_cpu(sts->rsp_data_len) < 4) {
			ql_log(ql_log_warn, fcport->vha, 0x503b,
			    "Async-%s error - hdl=%x not enough response(%d).\n",
			    type, sp->handle, sts->rsp_data_len);
		} else if (sts->data[3]) {
			ql_log(ql_log_warn, fcport->vha, 0x503c,
			    "Async-%s error - hdl=%x response(%x).\n",
			    type, sp->handle, sts->data[3]);
			iocb->u.tmf.data = QLA_FUNCTION_FAILED;
		}
	}

	if (iocb->u.tmf.data != QLA_SUCCESS)
		ql_dump_buffer(ql_dbg_async + ql_dbg_buffer, sp->vha, 0x5055,
		    sts, sizeof(*sts));

	sp->done(sp, 0);
}

static void qla24xx_nvme_iocb_entry(scsi_qla_host_t *vha, struct req_que *req,
    void *tsk, srb_t *sp)
{
	fc_port_t *fcport;
	struct srb_iocb *iocb;
	struct sts_entry_24xx *sts = (struct sts_entry_24xx *)tsk;
	uint16_t        state_flags;
	struct nvmefc_fcp_req *fd;
	uint16_t        ret = QLA_SUCCESS;
	__le16		comp_status = sts->comp_status;
	int		logit = 0;

	iocb = &sp->u.iocb_cmd;
	fcport = sp->fcport;
	iocb->u.nvme.comp_status = comp_status;
	state_flags  = le16_to_cpu(sts->state_flags);
	fd = iocb->u.nvme.desc;

	if (unlikely(iocb->u.nvme.aen_op))
		atomic_dec(&sp->vha->hw->nvme_active_aen_cnt);

	if (unlikely(comp_status != CS_COMPLETE))
		logit = 1;

	fd->transferred_length = fd->payload_length -
	    le32_to_cpu(sts->residual_len);

	/*
	 * State flags: Bit 6 and 0.
	 * If 0 is set, we don't care about 6.
	 * both cases resp was dma'd to host buffer
	 * if both are 0, that is good path case.
	 * if six is set and 0 is clear, we need to
	 * copy resp data from status iocb to resp buffer.
	 */
	if (!(state_flags & (SF_FCP_RSP_DMA | SF_NVME_ERSP))) {
		iocb->u.nvme.rsp_pyld_len = 0;
	} else if ((state_flags & (SF_FCP_RSP_DMA | SF_NVME_ERSP)) ==
			(SF_FCP_RSP_DMA | SF_NVME_ERSP)) {
		/* Response already DMA'd to fd->rspaddr. */
		iocb->u.nvme.rsp_pyld_len = sts->nvme_rsp_pyld_len;
	} else if ((state_flags & SF_FCP_RSP_DMA)) {
		/*
		 * Non-zero value in first 12 bytes of NVMe_RSP IU, treat this
		 * as an error.
		 */
		iocb->u.nvme.rsp_pyld_len = 0;
		fd->transferred_length = 0;
		ql_dbg(ql_dbg_io, fcport->vha, 0x307a,
			"Unexpected values in NVMe_RSP IU.\n");
		logit = 1;
	} else if (state_flags & SF_NVME_ERSP) {
		uint32_t *inbuf, *outbuf;
		uint16_t iter;

		inbuf = (uint32_t *)&sts->nvme_ersp_data;
		outbuf = (uint32_t *)fd->rspaddr;
		iocb->u.nvme.rsp_pyld_len = sts->nvme_rsp_pyld_len;
		if (unlikely(le16_to_cpu(iocb->u.nvme.rsp_pyld_len) >
		    sizeof(struct nvme_fc_ersp_iu))) {
			if (ql_mask_match(ql_dbg_io)) {
				WARN_ONCE(1, "Unexpected response payload length %u.\n",
				    iocb->u.nvme.rsp_pyld_len);
				ql_log(ql_log_warn, fcport->vha, 0x5100,
				    "Unexpected response payload length %u.\n",
				    iocb->u.nvme.rsp_pyld_len);
			}
			iocb->u.nvme.rsp_pyld_len =
				cpu_to_le16(sizeof(struct nvme_fc_ersp_iu));
		}
		iter = le16_to_cpu(iocb->u.nvme.rsp_pyld_len) >> 2;
		for (; iter; iter--)
			*outbuf++ = swab32(*inbuf++);
	}

	if (state_flags & SF_NVME_ERSP) {
		struct nvme_fc_ersp_iu *rsp_iu = fd->rspaddr;
		u32 tgt_xfer_len;

		tgt_xfer_len = be32_to_cpu(rsp_iu->xfrd_len);
		if (fd->transferred_length != tgt_xfer_len) {
			ql_dbg(ql_dbg_io, fcport->vha, 0x3079,
				"Dropped frame(s) detected (sent/rcvd=%u/%u).\n",
				tgt_xfer_len, fd->transferred_length);
			logit = 1;
		} else if (le16_to_cpu(comp_status) == CS_DATA_UNDERRUN) {
			/*
			 * Do not log if this is just an underflow and there
			 * is no data loss.
			 */
			logit = 0;
		}
	}

	if (unlikely(logit))
		ql_log(ql_log_warn, fcport->vha, 0x5060,
		   "NVME-%s ERR Handling - hdl=%x status(%x) tr_len:%x resid=%x  ox_id=%x\n",
		   sp->name, sp->handle, comp_status,
		   fd->transferred_length, le32_to_cpu(sts->residual_len),
		   sts->ox_id);

	/*
	 * If transport error then Failure (HBA rejects request)
	 * otherwise transport will handle.
	 */
	switch (le16_to_cpu(comp_status)) {
	case CS_COMPLETE:
		break;

	case CS_RESET:
	case CS_PORT_UNAVAILABLE:
	case CS_PORT_LOGGED_OUT:
		fcport->nvme_flag |= NVME_FLAG_RESETTING;
		/* fall through */
	case CS_ABORTED:
	case CS_PORT_BUSY:
		fd->transferred_length = 0;
		iocb->u.nvme.rsp_pyld_len = 0;
		ret = QLA_ABORTED;
		break;
	case CS_DATA_UNDERRUN:
		break;
	default:
		ret = QLA_FUNCTION_FAILED;
		break;
	}
	sp->done(sp, ret);
}

static void qla_ctrlvp_completed(scsi_qla_host_t *vha, struct req_que *req,
    struct vp_ctrl_entry_24xx *vce)
{
	const char func[] = "CTRLVP-IOCB";
	srb_t *sp;
	int rval = QLA_SUCCESS;

	sp = qla2x00_get_sp_from_handle(vha, func, req, vce);
	if (!sp)
		return;

	if (vce->entry_status != 0) {
		ql_dbg(ql_dbg_vport, vha, 0x10c4,
		    "%s: Failed to complete IOCB -- error status (%x)\n",
		    sp->name, vce->entry_status);
		rval = QLA_FUNCTION_FAILED;
	} else if (vce->comp_status != cpu_to_le16(CS_COMPLETE)) {
		ql_dbg(ql_dbg_vport, vha, 0x10c5,
		    "%s: Failed to complete IOCB -- completion status (%x) vpidx %x\n",
		    sp->name, le16_to_cpu(vce->comp_status),
		    le16_to_cpu(vce->vp_idx_failed));
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_vport, vha, 0x10c6,
		    "Done %s.\n", __func__);
	}

	sp->rc = rval;
	sp->done(sp, rval);
}

/* Process a single response queue entry. */
static void qla2x00_process_response_entry(struct scsi_qla_host *vha,
					   struct rsp_que *rsp,
					   sts_entry_t *pkt)
{
	sts21_entry_t *sts21_entry;
	sts22_entry_t *sts22_entry;
	uint16_t handle_cnt;
	uint16_t cnt;

	switch (pkt->entry_type) {
	case STATUS_TYPE:
		qla2x00_status_entry(vha, rsp, pkt);
		break;
	case STATUS_TYPE_21:
		sts21_entry = (sts21_entry_t *)pkt;
		handle_cnt = sts21_entry->handle_count;
		for (cnt = 0; cnt < handle_cnt; cnt++)
			qla2x00_process_completed_request(vha, rsp->req,
						sts21_entry->handle[cnt]);
		break;
	case STATUS_TYPE_22:
		sts22_entry = (sts22_entry_t *)pkt;
		handle_cnt = sts22_entry->handle_count;
		for (cnt = 0; cnt < handle_cnt; cnt++)
			qla2x00_process_completed_request(vha, rsp->req,
						sts22_entry->handle[cnt]);
		break;
	case STATUS_CONT_TYPE:
		qla2x00_status_cont_entry(rsp, (sts_cont_entry_t *)pkt);
		break;
	case MBX_IOCB_TYPE:
		qla2x00_mbx_iocb_entry(vha, rsp->req, (struct mbx_entry *)pkt);
		break;
	case CT_IOCB_TYPE:
		qla2x00_ct_entry(vha, rsp->req, pkt, CT_IOCB_TYPE);
		break;
	default:
		/* Type Not Supported. */
		ql_log(ql_log_warn, vha, 0x504a,
		       "Received unknown response pkt type %x entry status=%x.\n",
		       pkt->entry_type, pkt->entry_status);
		break;
	}
}

/**
 * qla2x00_process_response_queue() - Process response queue entries.
 * @rsp: response queue
 */
void
qla2x00_process_response_queue(struct rsp_que *rsp)
{
	struct scsi_qla_host *vha;
	struct qla_hw_data *ha = rsp->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	sts_entry_t	*pkt;

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
			qla2x00_error_entry(vha, rsp, pkt);
			((response_t *)pkt)->signature = RESPONSE_PROCESSED;
			wmb();
			continue;
		}

		qla2x00_process_response_entry(vha, rsp, pkt);
		((response_t *)pkt)->signature = RESPONSE_PROCESSED;
		wmb();
	}

	/* Adjust ring index */
	wrt_reg_word(ISP_RSP_Q_OUT(ha, reg), rsp->ring_index);
}

static inline void
qla2x00_handle_sense(srb_t *sp, uint8_t *sense_data, uint32_t par_sense_len,
		     uint32_t sense_len, struct rsp_que *rsp, int res)
{
	struct scsi_qla_host *vha = sp->vha;
	struct scsi_cmnd *cp = GET_CMD_SP(sp);
	uint32_t track_sense_len;

	if (sense_len >= SCSI_SENSE_BUFFERSIZE)
		sense_len = SCSI_SENSE_BUFFERSIZE;

	SET_CMD_SENSE_LEN(sp, sense_len);
	SET_CMD_SENSE_PTR(sp, cp->sense_buffer);
	track_sense_len = sense_len;

	if (sense_len > par_sense_len)
		sense_len = par_sense_len;

	memcpy(cp->sense_buffer, sense_data, sense_len);

	SET_CMD_SENSE_PTR(sp, cp->sense_buffer + sense_len);
	track_sense_len -= sense_len;
	SET_CMD_SENSE_LEN(sp, track_sense_len);

	if (track_sense_len != 0) {
		rsp->status_srb = sp;
		cp->result = res;
	}

	if (sense_len) {
		ql_dbg(ql_dbg_io + ql_dbg_buffer, vha, 0x301c,
		    "Check condition Sense data, nexus%ld:%d:%llu cmd=%p.\n",
		    sp->vha->host_no, cp->device->id, cp->device->lun,
		    cp);
		ql_dump_buffer(ql_dbg_io + ql_dbg_buffer, vha, 0x302b,
		    cp->sense_buffer, sense_len);
	}
}

struct scsi_dif_tuple {
	__be16 guard;       /* Checksum */
	__be16 app_tag;         /* APPL identifier */
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
	struct scsi_qla_host *vha = sp->vha;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	uint8_t		*ap = &sts24->data[12];
	uint8_t		*ep = &sts24->data[20];
	uint32_t	e_ref_tag, a_ref_tag;
	uint16_t	e_app_tag, a_app_tag;
	uint16_t	e_guard, a_guard;

	/*
	 * swab32 of the "data" field in the beginning of qla2x00_status_entry()
	 * would make guard field appear at offset 2
	 */
	a_guard   = get_unaligned_le16(ap + 2);
	a_app_tag = get_unaligned_le16(ap + 0);
	a_ref_tag = get_unaligned_le32(ap + 4);
	e_guard   = get_unaligned_le16(ep + 2);
	e_app_tag = get_unaligned_le16(ep + 0);
	e_ref_tag = get_unaligned_le32(ep + 4);

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
	if (a_app_tag == be16_to_cpu(T10_PI_APP_ESCAPE) &&
	    (scsi_get_prot_type(cmd) != SCSI_PROT_DIF_TYPE3 ||
	     a_ref_tag == be32_to_cpu(T10_PI_REF_ESCAPE))) {
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
			struct t10_pi_tuple *spt;

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
				ql_log(ql_log_warn, vha, 0x302f,
				    "unexpected tag values tag:lba=%x:%llx)\n",
				    e_ref_tag, (unsigned long long)lba_s);
				return 1;
			}

			spt = page_address(sg_page(sg)) + sg->offset;
			spt += j;

			spt->app_tag = T10_PI_APP_ESCAPE;
			if (scsi_get_prot_type(cmd) == SCSI_PROT_DIF_TYPE3)
				spt->ref_tag = T10_PI_REF_ESCAPE;
		}

		return 0;
	}

	/* check guard */
	if (e_guard != a_guard) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
		    0x10, 0x1);
		set_driver_byte(cmd, DRIVER_SENSE);
		set_host_byte(cmd, DID_ABORT);
		cmd->result |= SAM_STAT_CHECK_CONDITION;
		return 1;
	}

	/* check ref tag */
	if (e_ref_tag != a_ref_tag) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
		    0x10, 0x3);
		set_driver_byte(cmd, DRIVER_SENSE);
		set_host_byte(cmd, DID_ABORT);
		cmd->result |= SAM_STAT_CHECK_CONDITION;
		return 1;
	}

	/* check appl tag */
	if (e_app_tag != a_app_tag) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
		    0x10, 0x2);
		set_driver_byte(cmd, DRIVER_SENSE);
		set_host_byte(cmd, DID_ABORT);
		cmd->result |= SAM_STAT_CHECK_CONDITION;
		return 1;
	}

	return 1;
}

static void
qla25xx_process_bidir_status_iocb(scsi_qla_host_t *vha, void *pkt,
				  struct req_que *req, uint32_t index)
{
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	uint16_t thread_id;
	uint32_t rval = EXT_STATUS_OK;
	struct bsg_job *bsg_job = NULL;
	struct fc_bsg_request *bsg_request;
	struct fc_bsg_reply *bsg_reply;
	sts_entry_t *sts = pkt;
	struct sts_entry_24xx *sts24 = pkt;

	/* Validate handle. */
	if (index >= req->num_outstanding_cmds) {
		ql_log(ql_log_warn, vha, 0x70af,
		    "Invalid SCSI completion handle 0x%x.\n", index);
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		return;
	}

	sp = req->outstanding_cmds[index];
	if (!sp) {
		ql_log(ql_log_warn, vha, 0x70b0,
		    "Req:%d: Invalid ISP SCSI completion handle(0x%x)\n",
		    req->id, index);

		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		return;
	}

	/* Free outstanding command slot. */
	req->outstanding_cmds[index] = NULL;
	bsg_job = sp->u.bsg_job;
	bsg_request = bsg_job->request;
	bsg_reply = bsg_job->reply;

	if (IS_FWI2_CAPABLE(ha)) {
		comp_status = le16_to_cpu(sts24->comp_status);
		scsi_status = le16_to_cpu(sts24->scsi_status) & SS_MASK;
	} else {
		comp_status = le16_to_cpu(sts->comp_status);
		scsi_status = le16_to_cpu(sts->scsi_status) & SS_MASK;
	}

	thread_id = bsg_request->rqst_data.h_vendor.vendor_cmd[1];
	switch (comp_status) {
	case CS_COMPLETE:
		if (scsi_status == 0) {
			bsg_reply->reply_payload_rcv_len =
					bsg_job->reply_payload.payload_len;
			vha->qla_stats.input_bytes +=
				bsg_reply->reply_payload_rcv_len;
			vha->qla_stats.input_requests++;
			rval = EXT_STATUS_OK;
		}
		goto done;

	case CS_DATA_OVERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b1,
		    "Command completed with data overrun thread_id=%d\n",
		    thread_id);
		rval = EXT_STATUS_DATA_OVERRUN;
		break;

	case CS_DATA_UNDERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b2,
		    "Command completed with data underrun thread_id=%d\n",
		    thread_id);
		rval = EXT_STATUS_DATA_UNDERRUN;
		break;
	case CS_BIDIR_RD_OVERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b3,
		    "Command completed with read data overrun thread_id=%d\n",
		    thread_id);
		rval = EXT_STATUS_DATA_OVERRUN;
		break;

	case CS_BIDIR_RD_WR_OVERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b4,
		    "Command completed with read and write data overrun "
		    "thread_id=%d\n", thread_id);
		rval = EXT_STATUS_DATA_OVERRUN;
		break;

	case CS_BIDIR_RD_OVERRUN_WR_UNDERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b5,
		    "Command completed with read data over and write data "
		    "underrun thread_id=%d\n", thread_id);
		rval = EXT_STATUS_DATA_OVERRUN;
		break;

	case CS_BIDIR_RD_UNDERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b6,
		    "Command completed with read data underrun "
		    "thread_id=%d\n", thread_id);
		rval = EXT_STATUS_DATA_UNDERRUN;
		break;

	case CS_BIDIR_RD_UNDERRUN_WR_OVERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b7,
		    "Command completed with read data under and write data "
		    "overrun thread_id=%d\n", thread_id);
		rval = EXT_STATUS_DATA_UNDERRUN;
		break;

	case CS_BIDIR_RD_WR_UNDERRUN:
		ql_dbg(ql_dbg_user, vha, 0x70b8,
		    "Command completed with read and write data underrun "
		    "thread_id=%d\n", thread_id);
		rval = EXT_STATUS_DATA_UNDERRUN;
		break;

	case CS_BIDIR_DMA:
		ql_dbg(ql_dbg_user, vha, 0x70b9,
		    "Command completed with data DMA error thread_id=%d\n",
		    thread_id);
		rval = EXT_STATUS_DMA_ERR;
		break;

	case CS_TIMEOUT:
		ql_dbg(ql_dbg_user, vha, 0x70ba,
		    "Command completed with timeout thread_id=%d\n",
		    thread_id);
		rval = EXT_STATUS_TIMEOUT;
		break;
	default:
		ql_dbg(ql_dbg_user, vha, 0x70bb,
		    "Command completed with completion status=0x%x "
		    "thread_id=%d\n", comp_status, thread_id);
		rval = EXT_STATUS_ERR;
		break;
	}
	bsg_reply->reply_payload_rcv_len = 0;

done:
	/* Return the vendor specific reply to API */
	bsg_reply->reply_data.vendor_reply.vendor_rsp[0] = rval;
	bsg_job->reply_len = sizeof(struct fc_bsg_reply);
	/* Always return DID_OK, bsg will send the vendor specific response
	 * in this case only */
	sp->done(sp, DID_OK << 16);

}

/**
 * qla2x00_status_entry() - Process a Status IOCB entry.
 * @vha: SCSI driver HA context
 * @rsp: response queue
 * @pkt: Entry pointer
 */
static void
qla2x00_status_entry(scsi_qla_host_t *vha, struct rsp_que *rsp, void *pkt)
{
	srb_t		*sp;
	fc_port_t	*fcport;
	struct scsi_cmnd *cp;
	sts_entry_t *sts = pkt;
	struct sts_entry_24xx *sts24 = pkt;
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
	int res = 0;
	uint16_t state_flags = 0;
	uint16_t sts_qual = 0;

	if (IS_FWI2_CAPABLE(ha)) {
		comp_status = le16_to_cpu(sts24->comp_status);
		scsi_status = le16_to_cpu(sts24->scsi_status) & SS_MASK;
		state_flags = le16_to_cpu(sts24->state_flags);
	} else {
		comp_status = le16_to_cpu(sts->comp_status);
		scsi_status = le16_to_cpu(sts->scsi_status) & SS_MASK;
	}
	handle = (uint32_t) LSW(sts->handle);
	que = MSW(sts->handle);
	req = ha->req_q_map[que];

	/* Check for invalid queue pointer */
	if (req == NULL ||
	    que >= find_first_zero_bit(ha->req_qid_map, ha->max_req_queues)) {
		ql_dbg(ql_dbg_io, vha, 0x3059,
		    "Invalid status handle (0x%x): Bad req pointer. req=%p, "
		    "que=%u.\n", sts->handle, req, que);
		return;
	}

	/* Validate handle. */
	if (handle < req->num_outstanding_cmds) {
		sp = req->outstanding_cmds[handle];
		if (!sp) {
			ql_dbg(ql_dbg_io, vha, 0x3075,
			    "%s(%ld): Already returned command for status handle (0x%x).\n",
			    __func__, vha->host_no, sts->handle);
			return;
		}
	} else {
		ql_dbg(ql_dbg_io, vha, 0x3017,
		    "Invalid status handle, out of range (0x%x).\n",
		    sts->handle);

		if (!test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags)) {
			if (IS_P3P_TYPE(ha))
				set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
			else
				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		}
		return;
	}
	qla_put_iocbs(sp->qpair, &sp->iores);

	if (sp->cmd_type != TYPE_SRB) {
		req->outstanding_cmds[handle] = NULL;
		ql_dbg(ql_dbg_io, vha, 0x3015,
		    "Unknown sp->cmd_type %x %p).\n",
		    sp->cmd_type, sp);
		return;
	}

	/* NVME completion. */
	if (sp->type == SRB_NVME_CMD) {
		req->outstanding_cmds[handle] = NULL;
		qla24xx_nvme_iocb_entry(vha, req, pkt, sp);
		return;
	}

	if (unlikely((state_flags & BIT_1) && (sp->type == SRB_BIDI_CMD))) {
		qla25xx_process_bidir_status_iocb(vha, pkt, req, handle);
		return;
	}

	/* Task Management completion. */
	if (sp->type == SRB_TM_CMD) {
		qla24xx_tm_iocb_entry(vha, req, pkt);
		return;
	}

	/* Fast path completion. */
	if (comp_status == CS_COMPLETE && scsi_status == 0) {
		qla2x00_process_completed_request(vha, req, handle);

		return;
	}

	req->outstanding_cmds[handle] = NULL;
	cp = GET_CMD_SP(sp);
	if (cp == NULL) {
		ql_dbg(ql_dbg_io, vha, 0x3018,
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
		sts_qual = le16_to_cpu(sts24->status_qualifier);
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
			ql_dbg(ql_dbg_io, fcport->vha, 0x3019,
			    "FCP I/O protocol failure (0x%x/0x%x).\n",
			    rsp_info_len, rsp_info[3]);

			res = DID_BUS_BUSY << 16;
			goto out;
		}
	}

	/* Check for overrun. */
	if (IS_FWI2_CAPABLE(ha) && comp_status == CS_COMPLETE &&
	    scsi_status & SS_RESIDUAL_OVER)
		comp_status = CS_DATA_OVERRUN;

	/*
	 * Check retry_delay_timer value if we receive a busy or
	 * queue full.
	 */
	if (unlikely(lscsi_status == SAM_STAT_TASK_SET_FULL ||
		     lscsi_status == SAM_STAT_BUSY))
		qla2x00_set_retry_delay_timestamp(fcport, sts_qual);

	/*
	 * Based on Host and scsi status generate status code for Linux
	 */
	switch (comp_status) {
	case CS_COMPLETE:
	case CS_QUEUE_FULL:
		if (scsi_status == 0) {
			res = DID_OK << 16;
			break;
		}
		if (scsi_status & (SS_RESIDUAL_UNDER | SS_RESIDUAL_OVER)) {
			resid = resid_len;
			scsi_set_resid(cp, resid);

			if (!lscsi_status &&
			    ((unsigned)(scsi_bufflen(cp) - resid) <
			     cp->underflow)) {
				ql_dbg(ql_dbg_io, fcport->vha, 0x301a,
				    "Mid-layer underflow detected (0x%x of 0x%x bytes).\n",
				    resid, scsi_bufflen(cp));

				res = DID_ERROR << 16;
				break;
			}
		}
		res = DID_OK << 16 | lscsi_status;

		if (lscsi_status == SAM_STAT_TASK_SET_FULL) {
			ql_dbg(ql_dbg_io, fcport->vha, 0x301b,
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
		    rsp, res);
		break;

	case CS_DATA_UNDERRUN:
		/* Use F/W calculated residual length. */
		resid = IS_FWI2_CAPABLE(ha) ? fw_resid_len : resid_len;
		scsi_set_resid(cp, resid);
		if (scsi_status & SS_RESIDUAL_UNDER) {
			if (IS_FWI2_CAPABLE(ha) && fw_resid_len != resid_len) {
				ql_dbg(ql_dbg_io, fcport->vha, 0x301d,
				    "Dropped frame(s) detected (0x%x of 0x%x bytes).\n",
				    resid, scsi_bufflen(cp));

				res = DID_ERROR << 16 | lscsi_status;
				goto check_scsi_status;
			}

			if (!lscsi_status &&
			    ((unsigned)(scsi_bufflen(cp) - resid) <
			    cp->underflow)) {
				ql_dbg(ql_dbg_io, fcport->vha, 0x301e,
				    "Mid-layer underflow detected (0x%x of 0x%x bytes).\n",
				    resid, scsi_bufflen(cp));

				res = DID_ERROR << 16;
				break;
			}
		} else if (lscsi_status != SAM_STAT_TASK_SET_FULL &&
			    lscsi_status != SAM_STAT_BUSY) {
			/*
			 * scsi status of task set and busy are considered to be
			 * task not completed.
			 */

			ql_dbg(ql_dbg_io, fcport->vha, 0x301f,
			    "Dropped frame(s) detected (0x%x of 0x%x bytes).\n",
			    resid, scsi_bufflen(cp));

			res = DID_ERROR << 16 | lscsi_status;
			goto check_scsi_status;
		} else {
			ql_dbg(ql_dbg_io, fcport->vha, 0x3030,
			    "scsi_status: 0x%x, lscsi_status: 0x%x\n",
			    scsi_status, lscsi_status);
		}

		res = DID_OK << 16 | lscsi_status;
		logit = 0;

check_scsi_status:
		/*
		 * Check to see if SCSI Status is non zero. If so report SCSI
		 * Status.
		 */
		if (lscsi_status != 0) {
			if (lscsi_status == SAM_STAT_TASK_SET_FULL) {
				ql_dbg(ql_dbg_io, fcport->vha, 0x3020,
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
			    sense_len, rsp, res);
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
		res = DID_TRANSPORT_DISRUPTED << 16;

		if (comp_status == CS_TIMEOUT) {
			if (IS_FWI2_CAPABLE(ha))
				break;
			else if ((le16_to_cpu(sts->status_flags) &
			    SF_LOGOUT_SENT) == 0)
				break;
		}

		if (atomic_read(&fcport->state) == FCS_ONLINE) {
			ql_dbg(ql_dbg_disc, fcport->vha, 0x3021,
				"Port to be marked lost on fcport=%02x%02x%02x, current "
				"port state= %s comp_status %x.\n", fcport->d_id.b.domain,
				fcport->d_id.b.area, fcport->d_id.b.al_pa,
				port_state_str[FCS_ONLINE],
				comp_status);

			qlt_schedule_sess_for_deletion(fcport);
		}

		break;

	case CS_ABORTED:
		res = DID_RESET << 16;
		break;

	case CS_DIF_ERROR:
		logit = qla2x00_handle_dif_error(sp, sts24);
		res = cp->result;
		break;

	case CS_TRANSPORT:
		res = DID_ERROR << 16;

		if (!IS_PI_SPLIT_DET_CAPABLE(ha))
			break;

		if (state_flags & BIT_4)
			scmd_printk(KERN_WARNING, cp,
			    "Unsupported device '%s' found.\n",
			    cp->device->vendor);
		break;

	case CS_DMA:
		ql_log(ql_log_info, fcport->vha, 0x3022,
		    "CS_DMA error: 0x%x-0x%x (0x%x) nexus=%ld:%d:%llu portid=%06x oxid=0x%x cdb=%10phN len=0x%x rsp_info=0x%x resid=0x%x fw_resid=0x%x sp=%p cp=%p.\n",
		    comp_status, scsi_status, res, vha->host_no,
		    cp->device->id, cp->device->lun, fcport->d_id.b24,
		    ox_id, cp->cmnd, scsi_bufflen(cp), rsp_info_len,
		    resid_len, fw_resid_len, sp, cp);
		ql_dump_buffer(ql_dbg_tgt + ql_dbg_verbose, vha, 0xe0ee,
		    pkt, sizeof(*sts24));
		res = DID_ERROR << 16;
		break;
	default:
		res = DID_ERROR << 16;
		break;
	}

out:
	if (logit)
		ql_dbg(ql_dbg_io, fcport->vha, 0x3022,
		    "FCP command status: 0x%x-0x%x (0x%x) nexus=%ld:%d:%llu "
		    "portid=%02x%02x%02x oxid=0x%x cdb=%10phN len=0x%x "
		    "rsp_info=0x%x resid=0x%x fw_resid=0x%x sp=%p cp=%p.\n",
		    comp_status, scsi_status, res, vha->host_no,
		    cp->device->id, cp->device->lun, fcport->d_id.b.domain,
		    fcport->d_id.b.area, fcport->d_id.b.al_pa, ox_id,
		    cp->cmnd, scsi_bufflen(cp), rsp_info_len,
		    resid_len, fw_resid_len, sp, cp);

	if (rsp->status_srb == NULL)
		sp->done(sp, res);
}

/**
 * qla2x00_status_cont_entry() - Process a Status Continuations entry.
 * @rsp: response queue
 * @pkt: Entry pointer
 *
 * Extended sense data.
 */
static void
qla2x00_status_cont_entry(struct rsp_que *rsp, sts_cont_entry_t *pkt)
{
	uint8_t	sense_sz = 0;
	struct qla_hw_data *ha = rsp->hw;
	struct scsi_qla_host *vha = pci_get_drvdata(ha->pdev);
	srb_t *sp = rsp->status_srb;
	struct scsi_cmnd *cp;
	uint32_t sense_len;
	uint8_t *sense_ptr;

	if (!sp || !GET_CMD_SENSE_LEN(sp))
		return;

	sense_len = GET_CMD_SENSE_LEN(sp);
	sense_ptr = GET_CMD_SENSE_PTR(sp);

	cp = GET_CMD_SP(sp);
	if (cp == NULL) {
		ql_log(ql_log_warn, vha, 0x3025,
		    "cmd is NULL: already returned to OS (sp=%p).\n", sp);

		rsp->status_srb = NULL;
		return;
	}

	if (sense_len > sizeof(pkt->data))
		sense_sz = sizeof(pkt->data);
	else
		sense_sz = sense_len;

	/* Move sense data. */
	if (IS_FWI2_CAPABLE(ha))
		host_to_fcp_swap(pkt->data, sizeof(pkt->data));
	memcpy(sense_ptr, pkt->data, sense_sz);
	ql_dump_buffer(ql_dbg_io + ql_dbg_buffer, vha, 0x302c,
		sense_ptr, sense_sz);

	sense_len -= sense_sz;
	sense_ptr += sense_sz;

	SET_CMD_SENSE_PTR(sp, sense_ptr);
	SET_CMD_SENSE_LEN(sp, sense_len);

	/* Place command on done queue. */
	if (sense_len == 0) {
		rsp->status_srb = NULL;
		sp->done(sp, cp->result);
	}
}

/**
 * qla2x00_error_entry() - Process an error entry.
 * @vha: SCSI driver HA context
 * @rsp: response queue
 * @pkt: Entry pointer
 * return : 1=allow further error analysis. 0=no additional error analysis.
 */
static int
qla2x00_error_entry(scsi_qla_host_t *vha, struct rsp_que *rsp, sts_entry_t *pkt)
{
	srb_t *sp;
	struct qla_hw_data *ha = vha->hw;
	const char func[] = "ERROR-IOCB";
	uint16_t que = MSW(pkt->handle);
	struct req_que *req = NULL;
	int res = DID_ERROR << 16;

	ql_dbg(ql_dbg_async, vha, 0x502a,
	    "iocb type %xh with error status %xh, handle %xh, rspq id %d\n",
	    pkt->entry_type, pkt->entry_status, pkt->handle, rsp->id);

	if (que >= ha->max_req_queues || !ha->req_q_map[que])
		goto fatal;

	req = ha->req_q_map[que];

	if (pkt->entry_status & RF_BUSY)
		res = DID_BUS_BUSY << 16;

	if ((pkt->handle & ~QLA_TGT_HANDLE_MASK) == QLA_TGT_SKIP_HANDLE)
		return 0;

	switch (pkt->entry_type) {
	case NOTIFY_ACK_TYPE:
	case STATUS_TYPE:
	case STATUS_CONT_TYPE:
	case LOGINOUT_PORT_IOCB_TYPE:
	case CT_IOCB_TYPE:
	case ELS_IOCB_TYPE:
	case ABORT_IOCB_TYPE:
	case MBX_IOCB_TYPE:
	default:
		sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
		if (sp) {
			qla_put_iocbs(sp->qpair, &sp->iores);
			sp->done(sp, res);
			return 0;
		}
		break;

	case ABTS_RESP_24XX:
	case CTIO_TYPE7:
	case CTIO_CRC2:
		return 1;
	}
fatal:
	ql_log(ql_log_warn, vha, 0x5030,
	    "Error entry - invalid handle/queue (%04x).\n", que);
	return 0;
}

/**
 * qla24xx_mbx_completion() - Process mailbox command completions.
 * @vha: SCSI driver HA context
 * @mb0: Mailbox0 register
 */
static void
qla24xx_mbx_completion(scsi_qla_host_t *vha, uint16_t mb0)
{
	uint16_t	cnt;
	uint32_t	mboxes;
	__le16 __iomem *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	/* Read all mbox registers? */
	WARN_ON_ONCE(ha->mbx_count > 32);
	mboxes = (1ULL << ha->mbx_count) - 1;
	if (!ha->mcp)
		ql_dbg(ql_dbg_async, vha, 0x504e, "MBX pointer ERROR.\n");
	else
		mboxes = ha->mcp->in_mb;

	/* Load return mailbox registers. */
	ha->flags.mbox_int = 1;
	ha->mailbox_out[0] = mb0;
	mboxes >>= 1;
	wptr = &reg->mailbox1;

	for (cnt = 1; cnt < ha->mbx_count; cnt++) {
		if (mboxes & BIT_0)
			ha->mailbox_out[cnt] = rd_reg_word(wptr);

		mboxes >>= 1;
		wptr++;
	}
}

static void
qla24xx_abort_iocb_entry(scsi_qla_host_t *vha, struct req_que *req,
	struct abort_entry_24xx *pkt)
{
	const char func[] = "ABT_IOCB";
	srb_t *sp;
	struct srb_iocb *abt;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;

	abt = &sp->u.iocb_cmd;
	abt->u.abt.comp_status = pkt->nport_handle;
	sp->done(sp, 0);
}

void qla24xx_nvme_ls4_iocb(struct scsi_qla_host *vha,
    struct pt_ls4_request *pkt, struct req_que *req)
{
	srb_t *sp;
	const char func[] = "LS4_IOCB";
	uint16_t comp_status;

	sp = qla2x00_get_sp_from_handle(vha, func, req, pkt);
	if (!sp)
		return;

	comp_status = le16_to_cpu(pkt->status);
	sp->done(sp, comp_status);
}

static void qla24xx_process_mbx_iocb_response(struct scsi_qla_host *vha,
	struct rsp_que *rsp, struct sts_entry_24xx *pkt)
{
	struct qla_hw_data *ha = vha->hw;
	srb_t *sp;
	static const char func[] = "MBX-IOCB2";

	sp = qla2x00_get_sp_from_handle(vha, func, rsp->req, pkt);
	if (!sp)
		return;

	if (sp->type == SRB_SCSI_CMD ||
	    sp->type == SRB_NVME_CMD ||
	    sp->type == SRB_TM_CMD) {
		ql_log(ql_log_warn, vha, 0x509d,
			"Inconsistent event entry type %d\n", sp->type);
		if (IS_P3P_TYPE(ha))
			set_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags);
		else
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		return;
	}

	qla24xx_mbx_iocb_entry(vha, rsp->req, (struct mbx_24xx_entry *)pkt);
}

/**
 * qla24xx_process_response_queue() - Process response queue entries.
 * @vha: SCSI driver HA context
 * @rsp: response queue
 */
void qla24xx_process_response_queue(struct scsi_qla_host *vha,
	struct rsp_que *rsp)
{
	struct sts_entry_24xx *pkt;
	struct qla_hw_data *ha = vha->hw;
	struct purex_entry_24xx *purex_entry;
	struct purex_item *pure_item;

	if (!ha->flags.fw_started)
		return;

	if (rsp->qpair->cpuid != smp_processor_id() || !rsp->qpair->rcv_intr) {
		rsp->qpair->rcv_intr = 1;
		qla_cpu_update(rsp->qpair, smp_processor_id());
	}

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
			if (qla2x00_error_entry(vha, rsp, (sts_entry_t *) pkt))
				goto process_err;

			((response_t *)pkt)->signature = RESPONSE_PROCESSED;
			wmb();
			continue;
		}
process_err:

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
		case CT_IOCB_TYPE:
			qla24xx_els_ct_entry(vha, rsp->req, pkt, CT_IOCB_TYPE);
			break;
		case ELS_IOCB_TYPE:
			qla24xx_els_ct_entry(vha, rsp->req, pkt, ELS_IOCB_TYPE);
			break;
		case ABTS_RECV_24XX:
			if (qla_ini_mode_enabled(vha)) {
				pure_item = qla24xx_copy_std_pkt(vha, pkt);
				if (!pure_item)
					break;
				qla24xx_queue_purex_item(vha, pure_item,
							 qla24xx_process_abts);
				break;
			}
			if (IS_QLA83XX(ha) || IS_QLA27XX(ha) ||
			    IS_QLA28XX(ha)) {
				/* ensure that the ATIO queue is empty */
				qlt_handle_abts_recv(vha, rsp,
				    (response_t *)pkt);
				break;
			} else {
				qlt_24xx_process_atio_queue(vha, 1);
			}
			/* fall through */
		case ABTS_RESP_24XX:
		case CTIO_TYPE7:
		case CTIO_CRC2:
			qlt_response_pkt_all_vps(vha, rsp, (response_t *)pkt);
			break;
		case PT_LS4_REQUEST:
			qla24xx_nvme_ls4_iocb(vha, (struct pt_ls4_request *)pkt,
			    rsp->req);
			break;
		case NOTIFY_ACK_TYPE:
			if (pkt->handle == QLA_TGT_SKIP_HANDLE)
				qlt_response_pkt_all_vps(vha, rsp,
				    (response_t *)pkt);
			else
				qla24xxx_nack_iocb_entry(vha, rsp->req,
					(struct nack_to_isp *)pkt);
			break;
		case MARKER_TYPE:
			/* Do nothing in this case, this check is to prevent it
			 * from falling into default case
			 */
			break;
		case ABORT_IOCB_TYPE:
			qla24xx_abort_iocb_entry(vha, rsp->req,
			    (struct abort_entry_24xx *)pkt);
			break;
		case MBX_IOCB_TYPE:
			qla24xx_process_mbx_iocb_response(vha, rsp, pkt);
			break;
		case VP_CTRL_IOCB_TYPE:
			qla_ctrlvp_completed(vha, rsp->req,
			    (struct vp_ctrl_entry_24xx *)pkt);
			break;
		case PUREX_IOCB_TYPE:
			purex_entry = (void *)pkt;
			switch (purex_entry->els_frame_payload[3]) {
			case ELS_RDP:
				pure_item = qla24xx_copy_std_pkt(vha, pkt);
				if (!pure_item)
					break;
				qla24xx_queue_purex_item(vha, pure_item,
						 qla24xx_process_purex_rdp);
				break;
			case ELS_FPIN:
				if (!vha->hw->flags.scm_enabled) {
					ql_log(ql_log_warn, vha, 0x5094,
					       "SCM not active for this port\n");
					break;
				}
				pure_item = qla27xx_copy_fpin_pkt(vha,
							  (void **)&pkt, &rsp);
				if (!pure_item)
					break;
				qla24xx_queue_purex_item(vha, pure_item,
						 qla27xx_process_purex_fpin);
				break;

			default:
				ql_log(ql_log_warn, vha, 0x509c,
				       "Discarding ELS Request opcode 0x%x\n",
				       purex_entry->els_frame_payload[3]);
			}
			break;
		default:
			/* Type Not Supported. */
			ql_dbg(ql_dbg_async, vha, 0x5042,
			       "Received unknown response pkt type 0x%x entry status=%x.\n",
			       pkt->entry_type, pkt->entry_status);
			break;
		}
		((response_t *)pkt)->signature = RESPONSE_PROCESSED;
		wmb();
	}

	/* Adjust ring index */
	if (IS_P3P_TYPE(ha)) {
		struct device_reg_82xx __iomem *reg = &ha->iobase->isp82;

		wrt_reg_dword(&reg->rsp_q_out[0], rsp->ring_index);
	} else {
		wrt_reg_dword(rsp->rsp_q_out, rsp->ring_index);
	}
}

static void
qla2xxx_check_risc_status(scsi_qla_host_t *vha)
{
	int rval;
	uint32_t cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (!IS_QLA25XX(ha) && !IS_QLA81XX(ha) && !IS_QLA83XX(ha) &&
	    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return;

	rval = QLA_SUCCESS;
	wrt_reg_dword(&reg->iobase_addr, 0x7C00);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_window, 0x0001);
	for (cnt = 10000; (rd_reg_dword(&reg->iobase_window) & BIT_0) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt) {
			wrt_reg_dword(&reg->iobase_window, 0x0001);
			udelay(10);
		} else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	if (rval == QLA_SUCCESS)
		goto next_test;

	rval = QLA_SUCCESS;
	wrt_reg_dword(&reg->iobase_window, 0x0003);
	for (cnt = 100; (rd_reg_dword(&reg->iobase_window) & BIT_0) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt) {
			wrt_reg_dword(&reg->iobase_window, 0x0003);
			udelay(10);
		} else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	if (rval != QLA_SUCCESS)
		goto done;

next_test:
	if (rd_reg_dword(&reg->iobase_c8) & BIT_3)
		ql_log(ql_log_info, vha, 0x504c,
		    "Additional code -- 0x55AA.\n");

done:
	wrt_reg_dword(&reg->iobase_window, 0x0000);
	rd_reg_dword(&reg->iobase_window);
}

/**
 * qla24xx_intr_handler() - Process interrupts for the ISP23xx and ISP24xx.
 * @irq: interrupt number
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
	uint16_t	mb[8];
	struct rsp_que *rsp;
	unsigned long	flags;
	bool process_atio = false;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		ql_log(ql_log_info, NULL, 0x5059,
		    "%s: NULL response queue pointer.\n", __func__);
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
		stat = rd_reg_dword(&reg->host_status);
		if (qla2x00_check_reg32_for_disconnect(vha, stat))
			break;
		if (stat & HSRX_RISC_PAUSED) {
			if (unlikely(pci_channel_offline(ha->pdev)))
				break;

			hccr = rd_reg_dword(&reg->hccr);

			ql_log(ql_log_warn, vha, 0x504b,
			    "RISC paused -- HCCR=%x, Dumping firmware.\n",
			    hccr);

			qla2xxx_check_risc_status(vha);

			ha->isp_ops->fw_dump(vha);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			break;
		} else if ((stat & HSRX_RISC_INT) == 0)
			break;

		switch (stat & 0xff) {
		case INTR_ROM_MB_SUCCESS:
		case INTR_ROM_MB_FAILED:
		case INTR_MB_SUCCESS:
		case INTR_MB_FAILED:
			qla24xx_mbx_completion(vha, MSW(stat));
			status |= MBX_INTERRUPT;

			break;
		case INTR_ASYNC_EVENT:
			mb[0] = MSW(stat);
			mb[1] = rd_reg_word(&reg->mailbox1);
			mb[2] = rd_reg_word(&reg->mailbox2);
			mb[3] = rd_reg_word(&reg->mailbox3);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case INTR_RSP_QUE_UPDATE:
		case INTR_RSP_QUE_UPDATE_83XX:
			qla24xx_process_response_queue(vha, rsp);
			break;
		case INTR_ATIO_QUE_UPDATE_27XX:
		case INTR_ATIO_QUE_UPDATE:
			process_atio = true;
			break;
		case INTR_ATIO_RSP_QUE_UPDATE:
			process_atio = true;
			qla24xx_process_response_queue(vha, rsp);
			break;
		default:
			ql_dbg(ql_dbg_async, vha, 0x504f,
			    "Unrecognized interrupt type (%d).\n", stat * 0xff);
			break;
		}
		wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
		rd_reg_dword_relaxed(&reg->hccr);
		if (unlikely(IS_QLA83XX(ha) && (ha->pdev->revision == 1)))
			ndelay(3500);
	}
	qla2x00_handle_mbx_completion(ha, status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (process_atio) {
		spin_lock_irqsave(&ha->tgt.atio_lock, flags);
		qlt_24xx_process_atio_queue(vha, 0);
		spin_unlock_irqrestore(&ha->tgt.atio_lock, flags);
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
		ql_log(ql_log_info, NULL, 0x505a,
		    "%s: NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;
	reg = &ha->iobase->isp24;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	vha = pci_get_drvdata(ha->pdev);
	qla24xx_process_response_queue(vha, rsp);
	if (!ha->flags.disable_msix_handshake) {
		wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
		rd_reg_dword_relaxed(&reg->hccr);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

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
	uint16_t	mb[8];
	unsigned long flags;
	bool process_atio = false;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		ql_log(ql_log_info, NULL, 0x505c,
		    "%s: NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;
	reg = &ha->iobase->isp24;
	status = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	vha = pci_get_drvdata(ha->pdev);
	do {
		stat = rd_reg_dword(&reg->host_status);
		if (qla2x00_check_reg32_for_disconnect(vha, stat))
			break;
		if (stat & HSRX_RISC_PAUSED) {
			if (unlikely(pci_channel_offline(ha->pdev)))
				break;

			hccr = rd_reg_dword(&reg->hccr);

			ql_log(ql_log_info, vha, 0x5050,
			    "RISC paused -- HCCR=%x, Dumping firmware.\n",
			    hccr);

			qla2xxx_check_risc_status(vha);

			ha->isp_ops->fw_dump(vha);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			break;
		} else if ((stat & HSRX_RISC_INT) == 0)
			break;

		switch (stat & 0xff) {
		case INTR_ROM_MB_SUCCESS:
		case INTR_ROM_MB_FAILED:
		case INTR_MB_SUCCESS:
		case INTR_MB_FAILED:
			qla24xx_mbx_completion(vha, MSW(stat));
			status |= MBX_INTERRUPT;

			break;
		case INTR_ASYNC_EVENT:
			mb[0] = MSW(stat);
			mb[1] = rd_reg_word(&reg->mailbox1);
			mb[2] = rd_reg_word(&reg->mailbox2);
			mb[3] = rd_reg_word(&reg->mailbox3);
			qla2x00_async_event(vha, rsp, mb);
			break;
		case INTR_RSP_QUE_UPDATE:
		case INTR_RSP_QUE_UPDATE_83XX:
			qla24xx_process_response_queue(vha, rsp);
			break;
		case INTR_ATIO_QUE_UPDATE_27XX:
		case INTR_ATIO_QUE_UPDATE:
			process_atio = true;
			break;
		case INTR_ATIO_RSP_QUE_UPDATE:
			process_atio = true;
			qla24xx_process_response_queue(vha, rsp);
			break;
		default:
			ql_dbg(ql_dbg_async, vha, 0x5051,
			    "Unrecognized interrupt type (%d).\n", stat & 0xff);
			break;
		}
		wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
	} while (0);
	qla2x00_handle_mbx_completion(ha, status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (process_atio) {
		spin_lock_irqsave(&ha->tgt.atio_lock, flags);
		qlt_24xx_process_atio_queue(vha, 0);
		spin_unlock_irqrestore(&ha->tgt.atio_lock, flags);
	}

	return IRQ_HANDLED;
}

irqreturn_t
qla2xxx_msix_rsp_q(int irq, void *dev_id)
{
	struct qla_hw_data *ha;
	struct qla_qpair *qpair;

	qpair = dev_id;
	if (!qpair) {
		ql_log(ql_log_info, NULL, 0x505b,
		    "%s: NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = qpair->hw;

	queue_work_on(smp_processor_id(), ha->wq, &qpair->q_work);

	return IRQ_HANDLED;
}

irqreturn_t
qla2xxx_msix_rsp_q_hs(int irq, void *dev_id)
{
	struct qla_hw_data *ha;
	struct qla_qpair *qpair;
	struct device_reg_24xx __iomem *reg;
	unsigned long flags;

	qpair = dev_id;
	if (!qpair) {
		ql_log(ql_log_info, NULL, 0x505b,
		    "%s: NULL response queue pointer.\n", __func__);
		return IRQ_NONE;
	}
	ha = qpair->hw;

	reg = &ha->iobase->isp24;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	queue_work_on(smp_processor_id(), ha->wq, &qpair->q_work);

	return IRQ_HANDLED;
}

/* Interrupt handling helpers. */

struct qla_init_msix_entry {
	const char *name;
	irq_handler_t handler;
};

static const struct qla_init_msix_entry msix_entries[] = {
	{ "default", qla24xx_msix_default },
	{ "rsp_q", qla24xx_msix_rsp_q },
	{ "atio_q", qla83xx_msix_atio_q },
	{ "qpair_multiq", qla2xxx_msix_rsp_q },
	{ "qpair_multiq_hs", qla2xxx_msix_rsp_q_hs },
};

static const struct qla_init_msix_entry qla82xx_msix_entries[] = {
	{ "qla2xxx (default)", qla82xx_msix_default },
	{ "qla2xxx (rsp_q)", qla82xx_msix_rsp_q },
};

static int
qla24xx_enable_msix(struct qla_hw_data *ha, struct rsp_que *rsp)
{
	int i, ret;
	struct qla_msix_entry *qentry;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	int min_vecs = QLA_BASE_VECTORS;
	struct irq_affinity desc = {
		.pre_vectors = QLA_BASE_VECTORS,
	};

	if (QLA_TGT_MODE_ENABLED() && (ql2xenablemsix != 0) &&
	    IS_ATIO_MSIX_CAPABLE(ha)) {
		desc.pre_vectors++;
		min_vecs++;
	}

	if (USER_CTRL_IRQ(ha) || !ha->mqiobase) {
		/* user wants to control IRQ setting for target mode */
		ret = pci_alloc_irq_vectors(ha->pdev, min_vecs,
		    ha->msix_count, PCI_IRQ_MSIX);
	} else
		ret = pci_alloc_irq_vectors_affinity(ha->pdev, min_vecs,
		    ha->msix_count, PCI_IRQ_MSIX | PCI_IRQ_AFFINITY,
		    &desc);

	if (ret < 0) {
		ql_log(ql_log_fatal, vha, 0x00c7,
		    "MSI-X: Failed to enable support, "
		    "giving   up -- %d/%d.\n",
		    ha->msix_count, ret);
		goto msix_out;
	} else if (ret < ha->msix_count) {
		ql_log(ql_log_info, vha, 0x00c6,
		    "MSI-X: Using %d vectors\n", ret);
		ha->msix_count = ret;
		/* Recalculate queue values */
		if (ha->mqiobase && (ql2xmqsupport || ql2xnvmeenable)) {
			ha->max_req_queues = ha->msix_count - 1;

			/* ATIOQ needs 1 vector. That's 1 less QPair */
			if (QLA_TGT_MODE_ENABLED())
				ha->max_req_queues--;

			ha->max_rsp_queues = ha->max_req_queues;

			ha->max_qpairs = ha->max_req_queues - 1;
			ql_dbg_pci(ql_dbg_init, ha->pdev, 0x0190,
			    "Adjusted Max no of queues pairs: %d.\n", ha->max_qpairs);
		}
	}
	vha->irq_offset = desc.pre_vectors;
	ha->msix_entries = kcalloc(ha->msix_count,
				   sizeof(struct qla_msix_entry),
				   GFP_KERNEL);
	if (!ha->msix_entries) {
		ql_log(ql_log_fatal, vha, 0x00c8,
		    "Failed to allocate memory for ha->msix_entries.\n");
		ret = -ENOMEM;
		goto free_irqs;
	}
	ha->flags.msix_enabled = 1;

	for (i = 0; i < ha->msix_count; i++) {
		qentry = &ha->msix_entries[i];
		qentry->vector = pci_irq_vector(ha->pdev, i);
		qentry->entry = i;
		qentry->have_irq = 0;
		qentry->in_use = 0;
		qentry->handle = NULL;
	}

	/* Enable MSI-X vectors for the base queue */
	for (i = 0; i < QLA_BASE_VECTORS; i++) {
		qentry = &ha->msix_entries[i];
		qentry->handle = rsp;
		rsp->msix = qentry;
		scnprintf(qentry->name, sizeof(qentry->name),
		    "qla2xxx%lu_%s", vha->host_no, msix_entries[i].name);
		if (IS_P3P_TYPE(ha))
			ret = request_irq(qentry->vector,
				qla82xx_msix_entries[i].handler,
				0, qla82xx_msix_entries[i].name, rsp);
		else
			ret = request_irq(qentry->vector,
				msix_entries[i].handler,
				0, qentry->name, rsp);
		if (ret)
			goto msix_register_fail;
		qentry->have_irq = 1;
		qentry->in_use = 1;
	}

	/*
	 * If target mode is enable, also request the vector for the ATIO
	 * queue.
	 */
	if (QLA_TGT_MODE_ENABLED() && (ql2xenablemsix != 0) &&
	    IS_ATIO_MSIX_CAPABLE(ha)) {
		qentry = &ha->msix_entries[QLA_ATIO_VECTOR];
		rsp->msix = qentry;
		qentry->handle = rsp;
		scnprintf(qentry->name, sizeof(qentry->name),
		    "qla2xxx%lu_%s", vha->host_no,
		    msix_entries[QLA_ATIO_VECTOR].name);
		qentry->in_use = 1;
		ret = request_irq(qentry->vector,
			msix_entries[QLA_ATIO_VECTOR].handler,
			0, qentry->name, rsp);
		qentry->have_irq = 1;
	}

msix_register_fail:
	if (ret) {
		ql_log(ql_log_fatal, vha, 0x00cb,
		    "MSI-X: unable to register handler -- %x/%d.\n",
		    qentry->vector, ret);
		qla2x00_free_irqs(vha);
		ha->mqenable = 0;
		goto msix_out;
	}

	/* Enable MSI-X vector for response queue update for queue 0 */
	if (IS_QLA83XX(ha) || IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
		if (ha->msixbase && ha->mqiobase &&
		    (ha->max_rsp_queues > 1 || ha->max_req_queues > 1 ||
		     ql2xmqsupport))
			ha->mqenable = 1;
	} else
		if (ha->mqiobase &&
		    (ha->max_rsp_queues > 1 || ha->max_req_queues > 1 ||
		     ql2xmqsupport))
			ha->mqenable = 1;
	ql_dbg(ql_dbg_multiq, vha, 0xc005,
	    "mqiobase=%p, max_rsp_queues=%d, max_req_queues=%d.\n",
	    ha->mqiobase, ha->max_rsp_queues, ha->max_req_queues);
	ql_dbg(ql_dbg_init, vha, 0x0055,
	    "mqiobase=%p, max_rsp_queues=%d, max_req_queues=%d.\n",
	    ha->mqiobase, ha->max_rsp_queues, ha->max_req_queues);

msix_out:
	return ret;

free_irqs:
	pci_free_irq_vectors(ha->pdev);
	goto msix_out;
}

int
qla2x00_request_irqs(struct qla_hw_data *ha, struct rsp_que *rsp)
{
	int ret = QLA_FUNCTION_FAILED;
	device_reg_t *reg = ha->iobase;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	/* If possible, enable MSI-X. */
	if (ql2xenablemsix == 0 || (!IS_QLA2432(ha) && !IS_QLA2532(ha) &&
	    !IS_QLA8432(ha) && !IS_CNA_CAPABLE(ha) && !IS_QLA2031(ha) &&
	    !IS_QLAFX00(ha) && !IS_QLA27XX(ha) && !IS_QLA28XX(ha)))
		goto skip_msi;

	if (ql2xenablemsix == 2)
		goto skip_msix;

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

skip_msix:

	ql_log(ql_log_info, vha, 0x0037,
	    "Falling back-to MSI mode -- ret=%d.\n", ret);

	if (!IS_QLA24XX(ha) && !IS_QLA2532(ha) && !IS_QLA8432(ha) &&
	    !IS_QLA8001(ha) && !IS_P3P_TYPE(ha) && !IS_QLAFX00(ha) &&
	    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		goto skip_msi;

	ret = pci_alloc_irq_vectors(ha->pdev, 1, 1, PCI_IRQ_MSI);
	if (ret > 0) {
		ql_dbg(ql_dbg_init, vha, 0x0038,
		    "MSI: Enabled.\n");
		ha->flags.msi_enabled = 1;
	} else
		ql_log(ql_log_warn, vha, 0x0039,
		    "Falling back-to INTa mode -- ret=%d.\n", ret);
skip_msi:

	/* Skip INTx on ISP82xx. */
	if (!ha->flags.msi_enabled && IS_QLA82XX(ha))
		return QLA_FUNCTION_FAILED;

	ret = request_irq(ha->pdev->irq, ha->isp_ops->intr_handler,
	    ha->flags.msi_enabled ? 0 : IRQF_SHARED,
	    QLA2XXX_DRIVER_NAME, rsp);
	if (ret) {
		ql_log(ql_log_warn, vha, 0x003a,
		    "Failed to reserve interrupt %d already in use.\n",
		    ha->pdev->irq);
		goto fail;
	} else if (!ha->flags.msi_enabled) {
		ql_dbg(ql_dbg_init, vha, 0x0125,
		    "INTa mode: Enabled.\n");
		ha->flags.mr_intr_valid = 1;
	}

clear_risc_ints:
	if (IS_FWI2_CAPABLE(ha) || IS_QLAFX00(ha))
		goto fail;

	spin_lock_irq(&ha->hardware_lock);
	wrt_reg_word(&reg->isp.semaphore, 0);
	spin_unlock_irq(&ha->hardware_lock);

fail:
	return ret;
}

void
qla2x00_free_irqs(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct rsp_que *rsp;
	struct qla_msix_entry *qentry;
	int i;

	/*
	 * We need to check that ha->rsp_q_map is valid in case we are called
	 * from a probe failure context.
	 */
	if (!ha->rsp_q_map || !ha->rsp_q_map[0])
		goto free_irqs;
	rsp = ha->rsp_q_map[0];

	if (ha->flags.msix_enabled) {
		for (i = 0; i < ha->msix_count; i++) {
			qentry = &ha->msix_entries[i];
			if (qentry->have_irq) {
				irq_set_affinity_notifier(qentry->vector, NULL);
				free_irq(pci_irq_vector(ha->pdev, i), qentry->handle);
			}
		}
		kfree(ha->msix_entries);
		ha->msix_entries = NULL;
		ha->flags.msix_enabled = 0;
		ql_dbg(ql_dbg_init, vha, 0x0042,
			"Disabled MSI-X.\n");
	} else {
		free_irq(pci_irq_vector(ha->pdev, 0), rsp);
	}

free_irqs:
	pci_free_irq_vectors(ha->pdev);
}

int qla25xx_request_irq(struct qla_hw_data *ha, struct qla_qpair *qpair,
	struct qla_msix_entry *msix, int vector_type)
{
	const struct qla_init_msix_entry *intr = &msix_entries[vector_type];
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);
	int ret;

	scnprintf(msix->name, sizeof(msix->name),
	    "qla2xxx%lu_qpair%d", vha->host_no, qpair->id);
	ret = request_irq(msix->vector, intr->handler, 0, msix->name, qpair);
	if (ret) {
		ql_log(ql_log_fatal, vha, 0x00e6,
		    "MSI-X: Unable to register handler -- %x/%d.\n",
		    msix->vector, ret);
		return ret;
	}
	msix->have_irq = 1;
	msix->handle = qpair;
	return ret;
}
