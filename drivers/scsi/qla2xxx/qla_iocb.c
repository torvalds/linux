// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */
#include "qla_def.h"
#include "qla_target.h"

#include <linux/blkdev.h>
#include <linux/delay.h>

#include <scsi/scsi_tcq.h>

/**
 * qla2x00_get_cmd_direction() - Determine control_flag data direction.
 * @sp: SCSI command
 *
 * Returns the proper CF_* direction based on CDB.
 */
static inline uint16_t
qla2x00_get_cmd_direction(srb_t *sp)
{
	uint16_t cflags;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	struct scsi_qla_host *vha = sp->vha;

	cflags = 0;

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		cflags = CF_WRITE;
		vha->qla_stats.output_bytes += scsi_bufflen(cmd);
		vha->qla_stats.output_requests++;
	} else if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		cflags = CF_READ;
		vha->qla_stats.input_bytes += scsi_bufflen(cmd);
		vha->qla_stats.input_requests++;
	}
	return (cflags);
}

/**
 * qla2x00_calc_iocbs_32() - Determine number of Command Type 2 and
 * Continuation Type 0 IOCBs to allocate.
 *
 * @dsds: number of data segment descriptors needed
 *
 * Returns the number of IOCB entries needed to store @dsds.
 */
uint16_t
qla2x00_calc_iocbs_32(uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > 3) {
		iocbs += (dsds - 3) / 7;
		if ((dsds - 3) % 7)
			iocbs++;
	}
	return (iocbs);
}

/**
 * qla2x00_calc_iocbs_64() - Determine number of Command Type 3 and
 * Continuation Type 1 IOCBs to allocate.
 *
 * @dsds: number of data segment descriptors needed
 *
 * Returns the number of IOCB entries needed to store @dsds.
 */
uint16_t
qla2x00_calc_iocbs_64(uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > 2) {
		iocbs += (dsds - 2) / 5;
		if ((dsds - 2) % 5)
			iocbs++;
	}
	return (iocbs);
}

/**
 * qla2x00_prep_cont_type0_iocb() - Initialize a Continuation Type 0 IOCB.
 * @vha: HA context
 *
 * Returns a pointer to the Continuation Type 0 IOCB packet.
 */
static inline cont_entry_t *
qla2x00_prep_cont_type0_iocb(struct scsi_qla_host *vha)
{
	cont_entry_t *cont_pkt;
	struct req_que *req = vha->req;
	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else {
		req->ring_ptr++;
	}

	cont_pkt = (cont_entry_t *)req->ring_ptr;

	/* Load packet defaults. */
	put_unaligned_le32(CONTINUE_TYPE, &cont_pkt->entry_type);

	return (cont_pkt);
}

/**
 * qla2x00_prep_cont_type1_iocb() - Initialize a Continuation Type 1 IOCB.
 * @vha: HA context
 * @req: request queue
 *
 * Returns a pointer to the continuation type 1 IOCB packet.
 */
cont_a64_entry_t *
qla2x00_prep_cont_type1_iocb(scsi_qla_host_t *vha, struct req_que *req)
{
	cont_a64_entry_t *cont_pkt;

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else {
		req->ring_ptr++;
	}

	cont_pkt = (cont_a64_entry_t *)req->ring_ptr;

	/* Load packet defaults. */
	put_unaligned_le32(IS_QLAFX00(vha->hw) ? CONTINUE_A64_TYPE_FX00 :
			   CONTINUE_A64_TYPE, &cont_pkt->entry_type);

	return (cont_pkt);
}

inline int
qla24xx_configure_prot_mode(srb_t *sp, uint16_t *fw_prot_opts)
{
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);

	/* We always use DIFF Bundling for best performance */
	*fw_prot_opts = 0;

	/* Translate SCSI opcode to a protection opcode */
	switch (scsi_get_prot_op(cmd)) {
	case SCSI_PROT_READ_STRIP:
		*fw_prot_opts |= PO_MODE_DIF_REMOVE;
		break;
	case SCSI_PROT_WRITE_INSERT:
		*fw_prot_opts |= PO_MODE_DIF_INSERT;
		break;
	case SCSI_PROT_READ_INSERT:
		*fw_prot_opts |= PO_MODE_DIF_INSERT;
		break;
	case SCSI_PROT_WRITE_STRIP:
		*fw_prot_opts |= PO_MODE_DIF_REMOVE;
		break;
	case SCSI_PROT_READ_PASS:
	case SCSI_PROT_WRITE_PASS:
		if (cmd->prot_flags & SCSI_PROT_IP_CHECKSUM)
			*fw_prot_opts |= PO_MODE_DIF_TCP_CKSUM;
		else
			*fw_prot_opts |= PO_MODE_DIF_PASS;
		break;
	default:	/* Normal Request */
		*fw_prot_opts |= PO_MODE_DIF_PASS;
		break;
	}

	if (!(cmd->prot_flags & SCSI_PROT_GUARD_CHECK))
		*fw_prot_opts |= PO_DISABLE_GUARD_CHECK;

	return scsi_prot_sg_count(cmd);
}

/*
 * qla2x00_build_scsi_iocbs_32() - Build IOCB command utilizing 32bit
 * capable IOCB types.
 *
 * @sp: SRB command to process
 * @cmd_pkt: Command type 2 IOCB
 * @tot_dsds: Total number of segments to transfer
 */
void qla2x00_build_scsi_iocbs_32(srb_t *sp, cmd_entry_t *cmd_pkt,
    uint16_t tot_dsds)
{
	uint16_t	avail_dsds;
	struct dsd32	*cur_dsd;
	scsi_qla_host_t	*vha;
	struct scsi_cmnd *cmd;
	struct scatterlist *sg;
	int i;

	cmd = GET_CMD_SP(sp);

	/* Update entry type to indicate Command Type 2 IOCB */
	put_unaligned_le32(COMMAND_TYPE, &cmd_pkt->entry_type);

	/* No data transfer */
	if (!scsi_bufflen(cmd) || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = cpu_to_le32(0);
		return;
	}

	vha = sp->vha;
	cmd_pkt->control_flags |= cpu_to_le16(qla2x00_get_cmd_direction(sp));

	/* Three DSDs are available in the Command Type 2 IOCB */
	avail_dsds = ARRAY_SIZE(cmd_pkt->dsd32);
	cur_dsd = cmd_pkt->dsd32;

	/* Load data segments */
	scsi_for_each_sg(cmd, sg, tot_dsds, i) {
		cont_entry_t *cont_pkt;

		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			 * Seven DSDs are available in the Continuation
			 * Type 0 IOCB.
			 */
			cont_pkt = qla2x00_prep_cont_type0_iocb(vha);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = ARRAY_SIZE(cont_pkt->dsd);
		}

		append_dsd32(&cur_dsd, sg);
		avail_dsds--;
	}
}

/**
 * qla2x00_build_scsi_iocbs_64() - Build IOCB command utilizing 64bit
 * capable IOCB types.
 *
 * @sp: SRB command to process
 * @cmd_pkt: Command type 3 IOCB
 * @tot_dsds: Total number of segments to transfer
 */
void qla2x00_build_scsi_iocbs_64(srb_t *sp, cmd_entry_t *cmd_pkt,
    uint16_t tot_dsds)
{
	uint16_t	avail_dsds;
	struct dsd64	*cur_dsd;
	scsi_qla_host_t	*vha;
	struct scsi_cmnd *cmd;
	struct scatterlist *sg;
	int i;

	cmd = GET_CMD_SP(sp);

	/* Update entry type to indicate Command Type 3 IOCB */
	put_unaligned_le32(COMMAND_A64_TYPE, &cmd_pkt->entry_type);

	/* No data transfer */
	if (!scsi_bufflen(cmd) || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = cpu_to_le32(0);
		return;
	}

	vha = sp->vha;
	cmd_pkt->control_flags |= cpu_to_le16(qla2x00_get_cmd_direction(sp));

	/* Two DSDs are available in the Command Type 3 IOCB */
	avail_dsds = ARRAY_SIZE(cmd_pkt->dsd64);
	cur_dsd = cmd_pkt->dsd64;

	/* Load data segments */
	scsi_for_each_sg(cmd, sg, tot_dsds, i) {
		cont_a64_entry_t *cont_pkt;

		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			 * Five DSDs are available in the Continuation
			 * Type 1 IOCB.
			 */
			cont_pkt = qla2x00_prep_cont_type1_iocb(vha, vha->req);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = ARRAY_SIZE(cont_pkt->dsd);
		}

		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}
}

/*
 * Find the first handle that is not in use, starting from
 * req->current_outstanding_cmd + 1. The caller must hold the lock that is
 * associated with @req.
 */
uint32_t qla2xxx_get_next_handle(struct req_que *req)
{
	uint32_t index, handle = req->current_outstanding_cmd;

	for (index = 1; index < req->num_outstanding_cmds; index++) {
		handle++;
		if (handle == req->num_outstanding_cmds)
			handle = 1;
		if (!req->outstanding_cmds[handle])
			return handle;
	}

	return 0;
}

/**
 * qla2x00_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occurred, else zero.
 */
int
qla2x00_start_scsi(srb_t *sp)
{
	int		nseg;
	unsigned long   flags;
	scsi_qla_host_t	*vha;
	struct scsi_cmnd *cmd;
	uint32_t	*clr_ptr;
	uint32_t	handle;
	cmd_entry_t	*cmd_pkt;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct device_reg_2xxx __iomem *reg;
	struct qla_hw_data *ha;
	struct req_que *req;
	struct rsp_que *rsp;

	/* Setup device pointers. */
	vha = sp->vha;
	ha = vha->hw;
	reg = &ha->iobase->isp;
	cmd = GET_CMD_SP(sp);
	req = ha->req_q_map[0];
	rsp = ha->rsp_q_map[0];
	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (qla2x00_marker(vha, ha->base_qpair, 0, 0, MK_SYNC_ALL) !=
		    QLA_SUCCESS) {
			return (QLA_FUNCTION_FAILED);
		}
		vha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
	} else
		nseg = 0;

	tot_dsds = nseg;

	/* Calculate the number of request entries needed. */
	req_cnt = ha->isp_ops->calc_req_entries(tot_dsds);
	if (req->cnt < (req_cnt + 2)) {
		cnt = rd_reg_word_relaxed(ISP_REQ_Q_OUT(ha, reg));
		if (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
			    (req->ring_index - cnt);
		/* If still no head room then bail out */
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;
	}

	/* Build command packet */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;

	cmd_pkt = (cmd_entry_t *)req->ring_ptr;
	cmd_pkt->handle = handle;
	/* Zero out remaining portion of packet. */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* Set target ID and LUN number*/
	SET_TARGET_ID(ha, cmd_pkt->target, sp->fcport->loop_id);
	cmd_pkt->lun = cpu_to_le16(cmd->device->lun);
	cmd_pkt->control_flags = cpu_to_le16(CF_SIMPLE_TAG);

	/* Load SCSI command packet. */
	memcpy(cmd_pkt->scsi_cdb, cmd->cmnd, cmd->cmd_len);
	cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));

	/* Build IOCB segments */
	ha->isp_ops->build_iocbs(sp, cmd_pkt, tot_dsds);

	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	wmb();

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	sp->flags |= SRB_DMA_VALID;

	/* Set chip new ring index. */
	wrt_reg_word(ISP_REQ_Q_IN(ha, reg), req->ring_index);
	rd_reg_word_relaxed(ISP_REQ_Q_IN(ha, reg));	/* PCI Posting. */

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla2x00_process_response_queue(rsp);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (QLA_SUCCESS);

queuing_error:
	if (tot_dsds)
		scsi_dma_unmap(cmd);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (QLA_FUNCTION_FAILED);
}

/**
 * qla2x00_start_iocbs() - Execute the IOCB command
 * @vha: HA context
 * @req: request queue
 */
void
qla2x00_start_iocbs(struct scsi_qla_host *vha, struct req_que *req)
{
	struct qla_hw_data *ha = vha->hw;
	device_reg_t *reg = ISP_QUE_REG(ha, req->id);

	if (IS_P3P_TYPE(ha)) {
		qla82xx_start_iocbs(vha);
	} else {
		/* Adjust ring index. */
		req->ring_index++;
		if (req->ring_index == req->length) {
			req->ring_index = 0;
			req->ring_ptr = req->ring;
		} else
			req->ring_ptr++;

		/* Set chip new ring index. */
		if (ha->mqenable || IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
			wrt_reg_dword(req->req_q_in, req->ring_index);
		} else if (IS_QLA83XX(ha)) {
			wrt_reg_dword(req->req_q_in, req->ring_index);
			rd_reg_dword_relaxed(&ha->iobase->isp24.hccr);
		} else if (IS_QLAFX00(ha)) {
			wrt_reg_dword(&reg->ispfx00.req_q_in, req->ring_index);
			rd_reg_dword_relaxed(&reg->ispfx00.req_q_in);
			QLAFX00_SET_HST_INTR(ha, ha->rqstq_intr_code);
		} else if (IS_FWI2_CAPABLE(ha)) {
			wrt_reg_dword(&reg->isp24.req_q_in, req->ring_index);
			rd_reg_dword_relaxed(&reg->isp24.req_q_in);
		} else {
			wrt_reg_word(ISP_REQ_Q_IN(ha, &reg->isp),
				req->ring_index);
			rd_reg_word_relaxed(ISP_REQ_Q_IN(ha, &reg->isp));
		}
	}
}

/**
 * __qla2x00_marker() - Send a marker IOCB to the firmware.
 * @vha: HA context
 * @qpair: queue pair pointer
 * @loop_id: loop ID
 * @lun: LUN
 * @type: marker modifier
 *
 * Can be called from both normal and interrupt context.
 *
 * Returns non-zero if a failure occurred, else zero.
 */
static int
__qla2x00_marker(struct scsi_qla_host *vha, struct qla_qpair *qpair,
    uint16_t loop_id, uint64_t lun, uint8_t type)
{
	mrk_entry_t *mrk;
	struct mrk_entry_24xx *mrk24 = NULL;
	struct req_que *req = qpair->req;
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	mrk = (mrk_entry_t *)__qla2x00_alloc_iocbs(qpair, NULL);
	if (mrk == NULL) {
		ql_log(ql_log_warn, base_vha, 0x3026,
		    "Failed to allocate Marker IOCB.\n");

		return (QLA_FUNCTION_FAILED);
	}

	mrk24 = (struct mrk_entry_24xx *)mrk;

	mrk->entry_type = MARKER_TYPE;
	mrk->modifier = type;
	if (type != MK_SYNC_ALL) {
		if (IS_FWI2_CAPABLE(ha)) {
			mrk24->nport_handle = cpu_to_le16(loop_id);
			int_to_scsilun(lun, (struct scsi_lun *)&mrk24->lun);
			host_to_fcp_swap(mrk24->lun, sizeof(mrk24->lun));
			mrk24->vp_index = vha->vp_idx;
		} else {
			SET_TARGET_ID(ha, mrk->target, loop_id);
			mrk->lun = cpu_to_le16((uint16_t)lun);
		}
	}

	if (IS_FWI2_CAPABLE(ha))
		mrk24->handle = QLA_SKIP_HANDLE;

	wmb();

	qla2x00_start_iocbs(vha, req);

	return (QLA_SUCCESS);
}

int
qla2x00_marker(struct scsi_qla_host *vha, struct qla_qpair *qpair,
    uint16_t loop_id, uint64_t lun, uint8_t type)
{
	int ret;
	unsigned long flags = 0;

	spin_lock_irqsave(qpair->qp_lock_ptr, flags);
	ret = __qla2x00_marker(vha, qpair, loop_id, lun, type);
	spin_unlock_irqrestore(qpair->qp_lock_ptr, flags);

	return (ret);
}

/*
 * qla2x00_issue_marker
 *
 * Issue marker
 * Caller CAN have hardware lock held as specified by ha_locked parameter.
 * Might release it, then reaquire.
 */
int qla2x00_issue_marker(scsi_qla_host_t *vha, int ha_locked)
{
	if (ha_locked) {
		if (__qla2x00_marker(vha, vha->hw->base_qpair, 0, 0,
					MK_SYNC_ALL) != QLA_SUCCESS)
			return QLA_FUNCTION_FAILED;
	} else {
		if (qla2x00_marker(vha, vha->hw->base_qpair, 0, 0,
					MK_SYNC_ALL) != QLA_SUCCESS)
			return QLA_FUNCTION_FAILED;
	}
	vha->marker_needed = 0;

	return QLA_SUCCESS;
}

static inline int
qla24xx_build_scsi_type_6_iocbs(srb_t *sp, struct cmd_type_6 *cmd_pkt,
	uint16_t tot_dsds)
{
	struct dsd64 *cur_dsd = NULL, *next_dsd;
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct scsi_cmnd *cmd;
	struct	scatterlist *cur_seg;
	uint8_t avail_dsds;
	uint8_t first_iocb = 1;
	uint32_t dsd_list_len;
	struct dsd_dma *dsd_ptr;
	struct ct6_dsd *ctx;
	struct qla_qpair *qpair = sp->qpair;

	cmd = GET_CMD_SP(sp);

	/* Update entry type to indicate Command Type 3 IOCB */
	put_unaligned_le32(COMMAND_TYPE_6, &cmd_pkt->entry_type);

	/* No data transfer */
	if (!scsi_bufflen(cmd) || cmd->sc_data_direction == DMA_NONE ||
	    tot_dsds == 0) {
		cmd_pkt->byte_count = cpu_to_le32(0);
		return 0;
	}

	vha = sp->vha;
	ha = vha->hw;

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		cmd_pkt->control_flags = cpu_to_le16(CF_WRITE_DATA);
		qpair->counters.output_bytes += scsi_bufflen(cmd);
		qpair->counters.output_requests++;
	} else if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		cmd_pkt->control_flags = cpu_to_le16(CF_READ_DATA);
		qpair->counters.input_bytes += scsi_bufflen(cmd);
		qpair->counters.input_requests++;
	}

	cur_seg = scsi_sglist(cmd);
	ctx = &sp->u.scmd.ct6_ctx;

	while (tot_dsds) {
		avail_dsds = (tot_dsds > QLA_DSDS_PER_IOCB) ?
		    QLA_DSDS_PER_IOCB : tot_dsds;
		tot_dsds -= avail_dsds;
		dsd_list_len = (avail_dsds + 1) * QLA_DSD_SIZE;

		dsd_ptr = list_first_entry(&ha->gbl_dsd_list,
		    struct dsd_dma, list);
		next_dsd = dsd_ptr->dsd_addr;
		list_del(&dsd_ptr->list);
		ha->gbl_dsd_avail--;
		list_add_tail(&dsd_ptr->list, &ctx->dsd_list);
		ctx->dsd_use_cnt++;
		ha->gbl_dsd_inuse++;

		if (first_iocb) {
			first_iocb = 0;
			put_unaligned_le64(dsd_ptr->dsd_list_dma,
					   &cmd_pkt->fcp_dsd.address);
			cmd_pkt->fcp_dsd.length = cpu_to_le32(dsd_list_len);
		} else {
			put_unaligned_le64(dsd_ptr->dsd_list_dma,
					   &cur_dsd->address);
			cur_dsd->length = cpu_to_le32(dsd_list_len);
			cur_dsd++;
		}
		cur_dsd = next_dsd;
		while (avail_dsds) {
			append_dsd64(&cur_dsd, cur_seg);
			cur_seg = sg_next(cur_seg);
			avail_dsds--;
		}
	}

	/* Null termination */
	cur_dsd->address = 0;
	cur_dsd->length = 0;
	cur_dsd++;
	cmd_pkt->control_flags |= cpu_to_le16(CF_DATA_SEG_DESCR_ENABLE);
	return 0;
}

/*
 * qla24xx_calc_dsd_lists() - Determine number of DSD list required
 * for Command Type 6.
 *
 * @dsds: number of data segment descriptors needed
 *
 * Returns the number of dsd list needed to store @dsds.
 */
static inline uint16_t
qla24xx_calc_dsd_lists(uint16_t dsds)
{
	uint16_t dsd_lists = 0;

	dsd_lists = (dsds/QLA_DSDS_PER_IOCB);
	if (dsds % QLA_DSDS_PER_IOCB)
		dsd_lists++;
	return dsd_lists;
}


/**
 * qla24xx_build_scsi_iocbs() - Build IOCB command utilizing Command Type 7
 * IOCB types.
 *
 * @sp: SRB command to process
 * @cmd_pkt: Command type 3 IOCB
 * @tot_dsds: Total number of segments to transfer
 * @req: pointer to request queue
 */
inline void
qla24xx_build_scsi_iocbs(srb_t *sp, struct cmd_type_7 *cmd_pkt,
	uint16_t tot_dsds, struct req_que *req)
{
	uint16_t	avail_dsds;
	struct dsd64	*cur_dsd;
	scsi_qla_host_t	*vha;
	struct scsi_cmnd *cmd;
	struct scatterlist *sg;
	int i;
	struct qla_qpair *qpair = sp->qpair;

	cmd = GET_CMD_SP(sp);

	/* Update entry type to indicate Command Type 3 IOCB */
	put_unaligned_le32(COMMAND_TYPE_7, &cmd_pkt->entry_type);

	/* No data transfer */
	if (!scsi_bufflen(cmd) || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = cpu_to_le32(0);
		return;
	}

	vha = sp->vha;

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		cmd_pkt->task_mgmt_flags = cpu_to_le16(TMF_WRITE_DATA);
		qpair->counters.output_bytes += scsi_bufflen(cmd);
		qpair->counters.output_requests++;
	} else if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		cmd_pkt->task_mgmt_flags = cpu_to_le16(TMF_READ_DATA);
		qpair->counters.input_bytes += scsi_bufflen(cmd);
		qpair->counters.input_requests++;
	}

	/* One DSD is available in the Command Type 3 IOCB */
	avail_dsds = 1;
	cur_dsd = &cmd_pkt->dsd;

	/* Load data segments */

	scsi_for_each_sg(cmd, sg, tot_dsds, i) {
		cont_a64_entry_t *cont_pkt;

		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			 * Five DSDs are available in the Continuation
			 * Type 1 IOCB.
			 */
			cont_pkt = qla2x00_prep_cont_type1_iocb(vha, req);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = ARRAY_SIZE(cont_pkt->dsd);
		}

		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}
}

struct fw_dif_context {
	__le32	ref_tag;
	__le16	app_tag;
	uint8_t ref_tag_mask[4];	/* Validation/Replacement Mask*/
	uint8_t app_tag_mask[2];	/* Validation/Replacement Mask*/
};

/*
 * qla24xx_set_t10dif_tags_from_cmd - Extract Ref and App tags from SCSI command
 *
 */
static inline void
qla24xx_set_t10dif_tags(srb_t *sp, struct fw_dif_context *pkt,
    unsigned int protcnt)
{
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);

	pkt->ref_tag = cpu_to_le32(scsi_prot_ref_tag(cmd));

	if (cmd->prot_flags & SCSI_PROT_REF_CHECK &&
	    qla2x00_hba_err_chk_enabled(sp)) {
		pkt->ref_tag_mask[0] = 0xff;
		pkt->ref_tag_mask[1] = 0xff;
		pkt->ref_tag_mask[2] = 0xff;
		pkt->ref_tag_mask[3] = 0xff;
	}

	pkt->app_tag = cpu_to_le16(0);
	pkt->app_tag_mask[0] = 0x0;
	pkt->app_tag_mask[1] = 0x0;
}

int
qla24xx_get_one_block_sg(uint32_t blk_sz, struct qla2_sgx *sgx,
	uint32_t *partial)
{
	struct scatterlist *sg;
	uint32_t cumulative_partial, sg_len;
	dma_addr_t sg_dma_addr;

	if (sgx->num_bytes == sgx->tot_bytes)
		return 0;

	sg = sgx->cur_sg;
	cumulative_partial = sgx->tot_partial;

	sg_dma_addr = sg_dma_address(sg);
	sg_len = sg_dma_len(sg);

	sgx->dma_addr = sg_dma_addr + sgx->bytes_consumed;

	if ((cumulative_partial + (sg_len - sgx->bytes_consumed)) >= blk_sz) {
		sgx->dma_len = (blk_sz - cumulative_partial);
		sgx->tot_partial = 0;
		sgx->num_bytes += blk_sz;
		*partial = 0;
	} else {
		sgx->dma_len = sg_len - sgx->bytes_consumed;
		sgx->tot_partial += sgx->dma_len;
		*partial = 1;
	}

	sgx->bytes_consumed += sgx->dma_len;

	if (sg_len == sgx->bytes_consumed) {
		sg = sg_next(sg);
		sgx->num_sg++;
		sgx->cur_sg = sg;
		sgx->bytes_consumed = 0;
	}

	return 1;
}

int
qla24xx_walk_and_build_sglist_no_difb(struct qla_hw_data *ha, srb_t *sp,
	struct dsd64 *dsd, uint16_t tot_dsds, struct qla_tc_param *tc)
{
	void *next_dsd;
	uint8_t avail_dsds = 0;
	uint32_t dsd_list_len;
	struct dsd_dma *dsd_ptr;
	struct scatterlist *sg_prot;
	struct dsd64 *cur_dsd = dsd;
	uint16_t	used_dsds = tot_dsds;
	uint32_t	prot_int; /* protection interval */
	uint32_t	partial;
	struct qla2_sgx sgx;
	dma_addr_t	sle_dma;
	uint32_t	sle_dma_len, tot_prot_dma_len = 0;
	struct scsi_cmnd *cmd;

	memset(&sgx, 0, sizeof(struct qla2_sgx));
	if (sp) {
		cmd = GET_CMD_SP(sp);
		prot_int = scsi_prot_interval(cmd);

		sgx.tot_bytes = scsi_bufflen(cmd);
		sgx.cur_sg = scsi_sglist(cmd);
		sgx.sp = sp;

		sg_prot = scsi_prot_sglist(cmd);
	} else if (tc) {
		prot_int      = tc->blk_sz;
		sgx.tot_bytes = tc->bufflen;
		sgx.cur_sg    = tc->sg;
		sg_prot	      = tc->prot_sg;
	} else {
		BUG();
		return 1;
	}

	while (qla24xx_get_one_block_sg(prot_int, &sgx, &partial)) {

		sle_dma = sgx.dma_addr;
		sle_dma_len = sgx.dma_len;
alloc_and_fill:
		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			avail_dsds = (used_dsds > QLA_DSDS_PER_IOCB) ?
					QLA_DSDS_PER_IOCB : used_dsds;
			dsd_list_len = (avail_dsds + 1) * 12;
			used_dsds -= avail_dsds;

			/* allocate tracking DS */
			dsd_ptr = kzalloc(sizeof(struct dsd_dma), GFP_ATOMIC);
			if (!dsd_ptr)
				return 1;

			/* allocate new list */
			dsd_ptr->dsd_addr = next_dsd =
			    dma_pool_alloc(ha->dl_dma_pool, GFP_ATOMIC,
				&dsd_ptr->dsd_list_dma);

			if (!next_dsd) {
				/*
				 * Need to cleanup only this dsd_ptr, rest
				 * will be done by sp_free_dma()
				 */
				kfree(dsd_ptr);
				return 1;
			}

			if (sp) {
				list_add_tail(&dsd_ptr->list,
					      &sp->u.scmd.crc_ctx->dsd_list);

				sp->flags |= SRB_CRC_CTX_DSD_VALID;
			} else {
				list_add_tail(&dsd_ptr->list,
				    &(tc->ctx->dsd_list));
				*tc->ctx_dsd_alloced = 1;
			}


			/* add new list to cmd iocb or last list */
			put_unaligned_le64(dsd_ptr->dsd_list_dma,
					   &cur_dsd->address);
			cur_dsd->length = cpu_to_le32(dsd_list_len);
			cur_dsd = next_dsd;
		}
		put_unaligned_le64(sle_dma, &cur_dsd->address);
		cur_dsd->length = cpu_to_le32(sle_dma_len);
		cur_dsd++;
		avail_dsds--;

		if (partial == 0) {
			/* Got a full protection interval */
			sle_dma = sg_dma_address(sg_prot) + tot_prot_dma_len;
			sle_dma_len = 8;

			tot_prot_dma_len += sle_dma_len;
			if (tot_prot_dma_len == sg_dma_len(sg_prot)) {
				tot_prot_dma_len = 0;
				sg_prot = sg_next(sg_prot);
			}

			partial = 1; /* So as to not re-enter this block */
			goto alloc_and_fill;
		}
	}
	/* Null termination */
	cur_dsd->address = 0;
	cur_dsd->length = 0;
	cur_dsd++;
	return 0;
}

int
qla24xx_walk_and_build_sglist(struct qla_hw_data *ha, srb_t *sp,
	struct dsd64 *dsd, uint16_t tot_dsds, struct qla_tc_param *tc)
{
	void *next_dsd;
	uint8_t avail_dsds = 0;
	uint32_t dsd_list_len;
	struct dsd_dma *dsd_ptr;
	struct scatterlist *sg, *sgl;
	struct dsd64 *cur_dsd = dsd;
	int	i;
	uint16_t	used_dsds = tot_dsds;
	struct scsi_cmnd *cmd;

	if (sp) {
		cmd = GET_CMD_SP(sp);
		sgl = scsi_sglist(cmd);
	} else if (tc) {
		sgl = tc->sg;
	} else {
		BUG();
		return 1;
	}


	for_each_sg(sgl, sg, tot_dsds, i) {
		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			avail_dsds = (used_dsds > QLA_DSDS_PER_IOCB) ?
					QLA_DSDS_PER_IOCB : used_dsds;
			dsd_list_len = (avail_dsds + 1) * 12;
			used_dsds -= avail_dsds;

			/* allocate tracking DS */
			dsd_ptr = kzalloc(sizeof(struct dsd_dma), GFP_ATOMIC);
			if (!dsd_ptr)
				return 1;

			/* allocate new list */
			dsd_ptr->dsd_addr = next_dsd =
			    dma_pool_alloc(ha->dl_dma_pool, GFP_ATOMIC,
				&dsd_ptr->dsd_list_dma);

			if (!next_dsd) {
				/*
				 * Need to cleanup only this dsd_ptr, rest
				 * will be done by sp_free_dma()
				 */
				kfree(dsd_ptr);
				return 1;
			}

			if (sp) {
				list_add_tail(&dsd_ptr->list,
					      &sp->u.scmd.crc_ctx->dsd_list);

				sp->flags |= SRB_CRC_CTX_DSD_VALID;
			} else {
				list_add_tail(&dsd_ptr->list,
				    &(tc->ctx->dsd_list));
				*tc->ctx_dsd_alloced = 1;
			}

			/* add new list to cmd iocb or last list */
			put_unaligned_le64(dsd_ptr->dsd_list_dma,
					   &cur_dsd->address);
			cur_dsd->length = cpu_to_le32(dsd_list_len);
			cur_dsd = next_dsd;
		}
		append_dsd64(&cur_dsd, sg);
		avail_dsds--;

	}
	/* Null termination */
	cur_dsd->address = 0;
	cur_dsd->length = 0;
	cur_dsd++;
	return 0;
}

int
qla24xx_walk_and_build_prot_sglist(struct qla_hw_data *ha, srb_t *sp,
	struct dsd64 *cur_dsd, uint16_t tot_dsds, struct qla_tgt_cmd *tc)
{
	struct dsd_dma *dsd_ptr = NULL, *dif_dsd, *nxt_dsd;
	struct scatterlist *sg, *sgl;
	struct crc_context *difctx = NULL;
	struct scsi_qla_host *vha;
	uint dsd_list_len;
	uint avail_dsds = 0;
	uint used_dsds = tot_dsds;
	bool dif_local_dma_alloc = false;
	bool direction_to_device = false;
	int i;

	if (sp) {
		struct scsi_cmnd *cmd = GET_CMD_SP(sp);

		sgl = scsi_prot_sglist(cmd);
		vha = sp->vha;
		difctx = sp->u.scmd.crc_ctx;
		direction_to_device = cmd->sc_data_direction == DMA_TO_DEVICE;
		ql_dbg(ql_dbg_tgt + ql_dbg_verbose, vha, 0xe021,
		  "%s: scsi_cmnd: %p, crc_ctx: %p, sp: %p\n",
			__func__, cmd, difctx, sp);
	} else if (tc) {
		vha = tc->vha;
		sgl = tc->prot_sg;
		difctx = tc->ctx;
		direction_to_device = tc->dma_data_direction == DMA_TO_DEVICE;
	} else {
		BUG();
		return 1;
	}

	ql_dbg(ql_dbg_tgt + ql_dbg_verbose, vha, 0xe021,
	    "%s: enter (write=%u)\n", __func__, direction_to_device);

	/* if initiator doing write or target doing read */
	if (direction_to_device) {
		for_each_sg(sgl, sg, tot_dsds, i) {
			u64 sle_phys = sg_phys(sg);

			/* If SGE addr + len flips bits in upper 32-bits */
			if (MSD(sle_phys + sg->length) ^ MSD(sle_phys)) {
				ql_dbg(ql_dbg_tgt + ql_dbg_verbose, vha, 0xe022,
				    "%s: page boundary crossing (phys=%llx len=%x)\n",
				    __func__, sle_phys, sg->length);

				if (difctx) {
					ha->dif_bundle_crossed_pages++;
					dif_local_dma_alloc = true;
				} else {
					ql_dbg(ql_dbg_tgt + ql_dbg_verbose,
					    vha, 0xe022,
					    "%s: difctx pointer is NULL\n",
					    __func__);
				}
				break;
			}
		}
		ha->dif_bundle_writes++;
	} else {
		ha->dif_bundle_reads++;
	}

	if (ql2xdifbundlinginternalbuffers)
		dif_local_dma_alloc = direction_to_device;

	if (dif_local_dma_alloc) {
		u32 track_difbundl_buf = 0;
		u32 ldma_sg_len = 0;
		u8 ldma_needed = 1;

		difctx->no_dif_bundl = 0;
		difctx->dif_bundl_len = 0;

		/* Track DSD buffers */
		INIT_LIST_HEAD(&difctx->ldif_dsd_list);
		/* Track local DMA buffers */
		INIT_LIST_HEAD(&difctx->ldif_dma_hndl_list);

		for_each_sg(sgl, sg, tot_dsds, i) {
			u32 sglen = sg_dma_len(sg);

			ql_dbg(ql_dbg_tgt + ql_dbg_verbose, vha, 0xe023,
			    "%s: sg[%x] (phys=%llx sglen=%x) ldma_sg_len: %x dif_bundl_len: %x ldma_needed: %x\n",
			    __func__, i, (u64)sg_phys(sg), sglen, ldma_sg_len,
			    difctx->dif_bundl_len, ldma_needed);

			while (sglen) {
				u32 xfrlen = 0;

				if (ldma_needed) {
					/*
					 * Allocate list item to store
					 * the DMA buffers
					 */
					dsd_ptr = kzalloc(sizeof(*dsd_ptr),
					    GFP_ATOMIC);
					if (!dsd_ptr) {
						ql_dbg(ql_dbg_tgt, vha, 0xe024,
						    "%s: failed alloc dsd_ptr\n",
						    __func__);
						return 1;
					}
					ha->dif_bundle_kallocs++;

					/* allocate dma buffer */
					dsd_ptr->dsd_addr = dma_pool_alloc
						(ha->dif_bundl_pool, GFP_ATOMIC,
						 &dsd_ptr->dsd_list_dma);
					if (!dsd_ptr->dsd_addr) {
						ql_dbg(ql_dbg_tgt, vha, 0xe024,
						    "%s: failed alloc ->dsd_ptr\n",
						    __func__);
						/*
						 * need to cleanup only this
						 * dsd_ptr rest will be done
						 * by sp_free_dma()
						 */
						kfree(dsd_ptr);
						ha->dif_bundle_kallocs--;
						return 1;
					}
					ha->dif_bundle_dma_allocs++;
					ldma_needed = 0;
					difctx->no_dif_bundl++;
					list_add_tail(&dsd_ptr->list,
					    &difctx->ldif_dma_hndl_list);
				}

				/* xfrlen is min of dma pool size and sglen */
				xfrlen = (sglen >
				   (DIF_BUNDLING_DMA_POOL_SIZE - ldma_sg_len)) ?
				    DIF_BUNDLING_DMA_POOL_SIZE - ldma_sg_len :
				    sglen;

				/* replace with local allocated dma buffer */
				sg_pcopy_to_buffer(sgl, sg_nents(sgl),
				    dsd_ptr->dsd_addr + ldma_sg_len, xfrlen,
				    difctx->dif_bundl_len);
				difctx->dif_bundl_len += xfrlen;
				sglen -= xfrlen;
				ldma_sg_len += xfrlen;
				if (ldma_sg_len == DIF_BUNDLING_DMA_POOL_SIZE ||
				    sg_is_last(sg)) {
					ldma_needed = 1;
					ldma_sg_len = 0;
				}
			}
		}

		track_difbundl_buf = used_dsds = difctx->no_dif_bundl;
		ql_dbg(ql_dbg_tgt + ql_dbg_verbose, vha, 0xe025,
		    "dif_bundl_len=%x, no_dif_bundl=%x track_difbundl_buf: %x\n",
		    difctx->dif_bundl_len, difctx->no_dif_bundl,
		    track_difbundl_buf);

		if (sp)
			sp->flags |= SRB_DIF_BUNDL_DMA_VALID;
		else
			tc->prot_flags = DIF_BUNDL_DMA_VALID;

		list_for_each_entry_safe(dif_dsd, nxt_dsd,
		    &difctx->ldif_dma_hndl_list, list) {
			u32 sglen = (difctx->dif_bundl_len >
			    DIF_BUNDLING_DMA_POOL_SIZE) ?
			    DIF_BUNDLING_DMA_POOL_SIZE : difctx->dif_bundl_len;

			BUG_ON(track_difbundl_buf == 0);

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				ql_dbg(ql_dbg_tgt + ql_dbg_verbose, vha,
				    0xe024,
				    "%s: adding continuation iocb's\n",
				    __func__);
				avail_dsds = (used_dsds > QLA_DSDS_PER_IOCB) ?
				    QLA_DSDS_PER_IOCB : used_dsds;
				dsd_list_len = (avail_dsds + 1) * 12;
				used_dsds -= avail_dsds;

				/* allocate tracking DS */
				dsd_ptr = kzalloc(sizeof(*dsd_ptr), GFP_ATOMIC);
				if (!dsd_ptr) {
					ql_dbg(ql_dbg_tgt, vha, 0xe026,
					    "%s: failed alloc dsd_ptr\n",
					    __func__);
					return 1;
				}
				ha->dif_bundle_kallocs++;

				difctx->no_ldif_dsd++;
				/* allocate new list */
				dsd_ptr->dsd_addr =
				    dma_pool_alloc(ha->dl_dma_pool, GFP_ATOMIC,
					&dsd_ptr->dsd_list_dma);
				if (!dsd_ptr->dsd_addr) {
					ql_dbg(ql_dbg_tgt, vha, 0xe026,
					    "%s: failed alloc ->dsd_addr\n",
					    __func__);
					/*
					 * need to cleanup only this dsd_ptr
					 *  rest will be done by sp_free_dma()
					 */
					kfree(dsd_ptr);
					ha->dif_bundle_kallocs--;
					return 1;
				}
				ha->dif_bundle_dma_allocs++;

				if (sp) {
					list_add_tail(&dsd_ptr->list,
					    &difctx->ldif_dsd_list);
					sp->flags |= SRB_CRC_CTX_DSD_VALID;
				} else {
					list_add_tail(&dsd_ptr->list,
					    &difctx->ldif_dsd_list);
					tc->ctx_dsd_alloced = 1;
				}

				/* add new list to cmd iocb or last list */
				put_unaligned_le64(dsd_ptr->dsd_list_dma,
						   &cur_dsd->address);
				cur_dsd->length = cpu_to_le32(dsd_list_len);
				cur_dsd = dsd_ptr->dsd_addr;
			}
			put_unaligned_le64(dif_dsd->dsd_list_dma,
					   &cur_dsd->address);
			cur_dsd->length = cpu_to_le32(sglen);
			cur_dsd++;
			avail_dsds--;
			difctx->dif_bundl_len -= sglen;
			track_difbundl_buf--;
		}

		ql_dbg(ql_dbg_tgt + ql_dbg_verbose, vha, 0xe026,
		    "%s: no_ldif_dsd:%x, no_dif_bundl:%x\n", __func__,
			difctx->no_ldif_dsd, difctx->no_dif_bundl);
	} else {
		for_each_sg(sgl, sg, tot_dsds, i) {
			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				avail_dsds = (used_dsds > QLA_DSDS_PER_IOCB) ?
				    QLA_DSDS_PER_IOCB : used_dsds;
				dsd_list_len = (avail_dsds + 1) * 12;
				used_dsds -= avail_dsds;

				/* allocate tracking DS */
				dsd_ptr = kzalloc(sizeof(*dsd_ptr), GFP_ATOMIC);
				if (!dsd_ptr) {
					ql_dbg(ql_dbg_tgt + ql_dbg_verbose,
					    vha, 0xe027,
					    "%s: failed alloc dsd_dma...\n",
					    __func__);
					return 1;
				}

				/* allocate new list */
				dsd_ptr->dsd_addr =
				    dma_pool_alloc(ha->dl_dma_pool, GFP_ATOMIC,
					&dsd_ptr->dsd_list_dma);
				if (!dsd_ptr->dsd_addr) {
					/* need to cleanup only this dsd_ptr */
					/* rest will be done by sp_free_dma() */
					kfree(dsd_ptr);
					return 1;
				}

				if (sp) {
					list_add_tail(&dsd_ptr->list,
					    &difctx->dsd_list);
					sp->flags |= SRB_CRC_CTX_DSD_VALID;
				} else {
					list_add_tail(&dsd_ptr->list,
					    &difctx->dsd_list);
					tc->ctx_dsd_alloced = 1;
				}

				/* add new list to cmd iocb or last list */
				put_unaligned_le64(dsd_ptr->dsd_list_dma,
						   &cur_dsd->address);
				cur_dsd->length = cpu_to_le32(dsd_list_len);
				cur_dsd = dsd_ptr->dsd_addr;
			}
			append_dsd64(&cur_dsd, sg);
			avail_dsds--;
		}
	}
	/* Null termination */
	cur_dsd->address = 0;
	cur_dsd->length = 0;
	cur_dsd++;
	return 0;
}

/**
 * qla24xx_build_scsi_crc_2_iocbs() - Build IOCB command utilizing Command
 *							Type 6 IOCB types.
 *
 * @sp: SRB command to process
 * @cmd_pkt: Command type 3 IOCB
 * @tot_dsds: Total number of segments to transfer
 * @tot_prot_dsds: Total number of segments with protection information
 * @fw_prot_opts: Protection options to be passed to firmware
 */
static inline int
qla24xx_build_scsi_crc_2_iocbs(srb_t *sp, struct cmd_type_crc_2 *cmd_pkt,
    uint16_t tot_dsds, uint16_t tot_prot_dsds, uint16_t fw_prot_opts)
{
	struct dsd64		*cur_dsd;
	__be32			*fcp_dl;
	scsi_qla_host_t		*vha;
	struct scsi_cmnd	*cmd;
	uint32_t		total_bytes = 0;
	uint32_t		data_bytes;
	uint32_t		dif_bytes;
	uint8_t			bundling = 1;
	uint16_t		blk_size;
	struct crc_context	*crc_ctx_pkt = NULL;
	struct qla_hw_data	*ha;
	uint8_t			additional_fcpcdb_len;
	uint16_t		fcp_cmnd_len;
	struct fcp_cmnd		*fcp_cmnd;
	dma_addr_t		crc_ctx_dma;

	cmd = GET_CMD_SP(sp);

	/* Update entry type to indicate Command Type CRC_2 IOCB */
	put_unaligned_le32(COMMAND_TYPE_CRC_2, &cmd_pkt->entry_type);

	vha = sp->vha;
	ha = vha->hw;

	/* No data transfer */
	data_bytes = scsi_bufflen(cmd);
	if (!data_bytes || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = cpu_to_le32(0);
		return QLA_SUCCESS;
	}

	cmd_pkt->vp_index = sp->vha->vp_idx;

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		cmd_pkt->control_flags =
		    cpu_to_le16(CF_WRITE_DATA);
	} else if (cmd->sc_data_direction == DMA_FROM_DEVICE) {
		cmd_pkt->control_flags =
		    cpu_to_le16(CF_READ_DATA);
	}

	if ((scsi_get_prot_op(cmd) == SCSI_PROT_READ_INSERT) ||
	    (scsi_get_prot_op(cmd) == SCSI_PROT_WRITE_STRIP) ||
	    (scsi_get_prot_op(cmd) == SCSI_PROT_READ_STRIP) ||
	    (scsi_get_prot_op(cmd) == SCSI_PROT_WRITE_INSERT))
		bundling = 0;

	/* Allocate CRC context from global pool */
	crc_ctx_pkt = sp->u.scmd.crc_ctx =
	    dma_pool_zalloc(ha->dl_dma_pool, GFP_ATOMIC, &crc_ctx_dma);

	if (!crc_ctx_pkt)
		goto crc_queuing_error;

	crc_ctx_pkt->crc_ctx_dma = crc_ctx_dma;

	sp->flags |= SRB_CRC_CTX_DMA_VALID;

	/* Set handle */
	crc_ctx_pkt->handle = cmd_pkt->handle;

	INIT_LIST_HEAD(&crc_ctx_pkt->dsd_list);

	qla24xx_set_t10dif_tags(sp, (struct fw_dif_context *)
	    &crc_ctx_pkt->ref_tag, tot_prot_dsds);

	put_unaligned_le64(crc_ctx_dma, &cmd_pkt->crc_context_address);
	cmd_pkt->crc_context_len = cpu_to_le16(CRC_CONTEXT_LEN_FW);

	/* Determine SCSI command length -- align to 4 byte boundary */
	if (cmd->cmd_len > 16) {
		additional_fcpcdb_len = cmd->cmd_len - 16;
		if ((cmd->cmd_len % 4) != 0) {
			/* SCSI cmd > 16 bytes must be multiple of 4 */
			goto crc_queuing_error;
		}
		fcp_cmnd_len = 12 + cmd->cmd_len + 4;
	} else {
		additional_fcpcdb_len = 0;
		fcp_cmnd_len = 12 + 16 + 4;
	}

	fcp_cmnd = &crc_ctx_pkt->fcp_cmnd;

	fcp_cmnd->additional_cdb_len = additional_fcpcdb_len;
	if (cmd->sc_data_direction == DMA_TO_DEVICE)
		fcp_cmnd->additional_cdb_len |= 1;
	else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		fcp_cmnd->additional_cdb_len |= 2;

	int_to_scsilun(cmd->device->lun, &fcp_cmnd->lun);
	memcpy(fcp_cmnd->cdb, cmd->cmnd, cmd->cmd_len);
	cmd_pkt->fcp_cmnd_dseg_len = cpu_to_le16(fcp_cmnd_len);
	put_unaligned_le64(crc_ctx_dma + CRC_CONTEXT_FCPCMND_OFF,
			   &cmd_pkt->fcp_cmnd_dseg_address);
	fcp_cmnd->task_management = 0;
	fcp_cmnd->task_attribute = TSK_SIMPLE;

	cmd_pkt->fcp_rsp_dseg_len = 0; /* Let response come in status iocb */

	/* Compute dif len and adjust data len to incude protection */
	dif_bytes = 0;
	blk_size = cmd->device->sector_size;
	dif_bytes = (data_bytes / blk_size) * 8;

	switch (scsi_get_prot_op(GET_CMD_SP(sp))) {
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
		total_bytes = data_bytes;
		data_bytes += dif_bytes;
		break;

	case SCSI_PROT_READ_STRIP:
	case SCSI_PROT_WRITE_INSERT:
	case SCSI_PROT_READ_PASS:
	case SCSI_PROT_WRITE_PASS:
		total_bytes = data_bytes + dif_bytes;
		break;
	default:
		BUG();
	}

	if (!qla2x00_hba_err_chk_enabled(sp))
		fw_prot_opts |= 0x10; /* Disable Guard tag checking */
	/* HBA error checking enabled */
	else if (IS_PI_UNINIT_CAPABLE(ha)) {
		if ((scsi_get_prot_type(GET_CMD_SP(sp)) == SCSI_PROT_DIF_TYPE1)
		    || (scsi_get_prot_type(GET_CMD_SP(sp)) ==
			SCSI_PROT_DIF_TYPE2))
			fw_prot_opts |= BIT_10;
		else if (scsi_get_prot_type(GET_CMD_SP(sp)) ==
		    SCSI_PROT_DIF_TYPE3)
			fw_prot_opts |= BIT_11;
	}

	if (!bundling) {
		cur_dsd = &crc_ctx_pkt->u.nobundling.data_dsd[0];
	} else {
		/*
		 * Configure Bundling if we need to fetch interlaving
		 * protection PCI accesses
		 */
		fw_prot_opts |= PO_ENABLE_DIF_BUNDLING;
		crc_ctx_pkt->u.bundling.dif_byte_count = cpu_to_le32(dif_bytes);
		crc_ctx_pkt->u.bundling.dseg_count = cpu_to_le16(tot_dsds -
							tot_prot_dsds);
		cur_dsd = &crc_ctx_pkt->u.bundling.data_dsd[0];
	}

	/* Finish the common fields of CRC pkt */
	crc_ctx_pkt->blk_size = cpu_to_le16(blk_size);
	crc_ctx_pkt->prot_opts = cpu_to_le16(fw_prot_opts);
	crc_ctx_pkt->byte_count = cpu_to_le32(data_bytes);
	crc_ctx_pkt->guard_seed = cpu_to_le16(0);
	/* Fibre channel byte count */
	cmd_pkt->byte_count = cpu_to_le32(total_bytes);
	fcp_dl = (__be32 *)(crc_ctx_pkt->fcp_cmnd.cdb + 16 +
	    additional_fcpcdb_len);
	*fcp_dl = htonl(total_bytes);

	if (!data_bytes || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = cpu_to_le32(0);
		return QLA_SUCCESS;
	}
	/* Walks data segments */

	cmd_pkt->control_flags |= cpu_to_le16(CF_DATA_SEG_DESCR_ENABLE);

	if (!bundling && tot_prot_dsds) {
		if (qla24xx_walk_and_build_sglist_no_difb(ha, sp,
			cur_dsd, tot_dsds, NULL))
			goto crc_queuing_error;
	} else if (qla24xx_walk_and_build_sglist(ha, sp, cur_dsd,
			(tot_dsds - tot_prot_dsds), NULL))
		goto crc_queuing_error;

	if (bundling && tot_prot_dsds) {
		/* Walks dif segments */
		cmd_pkt->control_flags |= cpu_to_le16(CF_DIF_SEG_DESCR_ENABLE);
		cur_dsd = &crc_ctx_pkt->u.bundling.dif_dsd;
		if (qla24xx_walk_and_build_prot_sglist(ha, sp, cur_dsd,
				tot_prot_dsds, NULL))
			goto crc_queuing_error;
	}
	return QLA_SUCCESS;

crc_queuing_error:
	/* Cleanup will be performed by the caller */

	return QLA_FUNCTION_FAILED;
}

/**
 * qla24xx_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occurred, else zero.
 */
int
qla24xx_start_scsi(srb_t *sp)
{
	int		nseg;
	unsigned long   flags;
	uint32_t	*clr_ptr;
	uint32_t	handle;
	struct cmd_type_7 *cmd_pkt;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct req_que *req = NULL;
	struct rsp_que *rsp;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	struct scsi_qla_host *vha = sp->vha;
	struct qla_hw_data *ha = vha->hw;

	if (sp->fcport->edif.enable  && (sp->fcport->flags & FCF_FCSP_DEVICE))
		return qla28xx_start_scsi_edif(sp);

	/* Setup device pointers. */
	req = vha->req;
	rsp = req->rsp;

	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (qla2x00_marker(vha, ha->base_qpair, 0, 0, MK_SYNC_ALL) !=
		    QLA_SUCCESS)
			return QLA_FUNCTION_FAILED;
		vha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
	} else
		nseg = 0;

	tot_dsds = nseg;
	req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);

	sp->iores.res_type = RESOURCE_IOCB | RESOURCE_EXCH;
	sp->iores.exch_cnt = 1;
	sp->iores.iocb_cnt = req_cnt;
	if (qla_get_fw_resources(sp->qpair, &sp->iores))
		goto queuing_error;

	if (req->cnt < (req_cnt + 2)) {
		if (IS_SHADOW_REG_CAPABLE(ha)) {
			cnt = *req->out_ptr;
		} else {
			cnt = rd_reg_dword_relaxed(req->req_q_out);
			if (qla2x00_check_reg16_for_disconnect(vha, cnt))
				goto queuing_error;
		}

		if (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
				(req->ring_index - cnt);
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;
	}

	/* Build command packet. */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;

	cmd_pkt = (struct cmd_type_7 *)req->ring_ptr;
	cmd_pkt->handle = make_handle(req->id, handle);

	/* Zero out remaining portion of packet. */
	/*    tagged queuing modifier -- default is TSK_SIMPLE (0). */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* Set NPORT-ID and LUN number*/
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
	cmd_pkt->vp_index = sp->vha->vp_idx;

	int_to_scsilun(cmd->device->lun, &cmd_pkt->lun);
	host_to_fcp_swap((uint8_t *)&cmd_pkt->lun, sizeof(cmd_pkt->lun));

	cmd_pkt->task = TSK_SIMPLE;

	/* Load SCSI command packet. */
	memcpy(cmd_pkt->fcp_cdb, cmd->cmnd, cmd->cmd_len);
	host_to_fcp_swap(cmd_pkt->fcp_cdb, sizeof(cmd_pkt->fcp_cdb));

	cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));

	/* Build IOCB segments */
	qla24xx_build_scsi_iocbs(sp, cmd_pkt, tot_dsds, req);

	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	wmb();
	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	sp->qpair->cmd_cnt++;
	sp->flags |= SRB_DMA_VALID;

	/* Set chip new ring index. */
	wrt_reg_dword(req->req_q_in, req->ring_index);

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(vha, rsp);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return QLA_SUCCESS;

queuing_error:
	if (tot_dsds)
		scsi_dma_unmap(cmd);

	qla_put_fw_resources(sp->qpair, &sp->iores);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_FUNCTION_FAILED;
}

/**
 * qla24xx_dif_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occurred, else zero.
 */
int
qla24xx_dif_start_scsi(srb_t *sp)
{
	int			nseg;
	unsigned long		flags;
	uint32_t		*clr_ptr;
	uint32_t		handle;
	uint16_t		cnt;
	uint16_t		req_cnt = 0;
	uint16_t		tot_dsds;
	uint16_t		tot_prot_dsds;
	uint16_t		fw_prot_opts = 0;
	struct req_que		*req = NULL;
	struct rsp_que		*rsp = NULL;
	struct scsi_cmnd	*cmd = GET_CMD_SP(sp);
	struct scsi_qla_host	*vha = sp->vha;
	struct qla_hw_data	*ha = vha->hw;
	struct cmd_type_crc_2	*cmd_pkt;
	uint32_t		status = 0;

#define QDSS_GOT_Q_SPACE	BIT_0

	/* Only process protection or >16 cdb in this routine */
	if (scsi_get_prot_op(cmd) == SCSI_PROT_NORMAL) {
		if (cmd->cmd_len <= 16)
			return qla24xx_start_scsi(sp);
	}

	/* Setup device pointers. */
	req = vha->req;
	rsp = req->rsp;

	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (qla2x00_marker(vha, ha->base_qpair, 0, 0, MK_SYNC_ALL) !=
		    QLA_SUCCESS)
			return QLA_FUNCTION_FAILED;
		vha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0)
		goto queuing_error;

	/* Compute number of required data segments */
	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
		else
			sp->flags |= SRB_DMA_VALID;

		if ((scsi_get_prot_op(cmd) == SCSI_PROT_READ_INSERT) ||
		    (scsi_get_prot_op(cmd) == SCSI_PROT_WRITE_STRIP)) {
			struct qla2_sgx sgx;
			uint32_t	partial;

			memset(&sgx, 0, sizeof(struct qla2_sgx));
			sgx.tot_bytes = scsi_bufflen(cmd);
			sgx.cur_sg = scsi_sglist(cmd);
			sgx.sp = sp;

			nseg = 0;
			while (qla24xx_get_one_block_sg(
			    cmd->device->sector_size, &sgx, &partial))
				nseg++;
		}
	} else
		nseg = 0;

	/* number of required data segments */
	tot_dsds = nseg;

	/* Compute number of required protection segments */
	if (qla24xx_configure_prot_mode(sp, &fw_prot_opts)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_prot_sglist(cmd),
		    scsi_prot_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
		else
			sp->flags |= SRB_CRC_PROT_DMA_VALID;

		if ((scsi_get_prot_op(cmd) == SCSI_PROT_READ_INSERT) ||
		    (scsi_get_prot_op(cmd) == SCSI_PROT_WRITE_STRIP)) {
			nseg = scsi_bufflen(cmd) / cmd->device->sector_size;
		}
	} else {
		nseg = 0;
	}

	req_cnt = 1;
	/* Total Data and protection sg segment(s) */
	tot_prot_dsds = nseg;
	tot_dsds += nseg;

	sp->iores.res_type = RESOURCE_IOCB | RESOURCE_EXCH;
	sp->iores.exch_cnt = 1;
	sp->iores.iocb_cnt = qla24xx_calc_iocbs(vha, tot_dsds);
	if (qla_get_fw_resources(sp->qpair, &sp->iores))
		goto queuing_error;

	if (req->cnt < (req_cnt + 2)) {
		if (IS_SHADOW_REG_CAPABLE(ha)) {
			cnt = *req->out_ptr;
		} else {
			cnt = rd_reg_dword_relaxed(req->req_q_out);
			if (qla2x00_check_reg16_for_disconnect(vha, cnt))
				goto queuing_error;
		}
		if (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
				(req->ring_index - cnt);
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;
	}

	status |= QDSS_GOT_Q_SPACE;

	/* Build header part of command packet (excluding the OPCODE). */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;

	/* Fill-in common area */
	cmd_pkt = (struct cmd_type_crc_2 *)req->ring_ptr;
	cmd_pkt->handle = make_handle(req->id, handle);

	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);

	/* Set NPORT-ID and LUN number*/
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;

	int_to_scsilun(cmd->device->lun, &cmd_pkt->lun);
	host_to_fcp_swap((uint8_t *)&cmd_pkt->lun, sizeof(cmd_pkt->lun));

	/* Total Data and protection segment(s) */
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* Build IOCB segments and adjust for data protection segments */
	if (qla24xx_build_scsi_crc_2_iocbs(sp, (struct cmd_type_crc_2 *)
	    req->ring_ptr, tot_dsds, tot_prot_dsds, fw_prot_opts) !=
		QLA_SUCCESS)
		goto queuing_error;

	cmd_pkt->entry_count = (uint8_t)req_cnt;
	/* Specify response queue number where completion should happen */
	cmd_pkt->entry_status = (uint8_t) rsp->id;
	cmd_pkt->timeout = cpu_to_le16(0);
	wmb();

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	sp->qpair->cmd_cnt++;
	/* Set chip new ring index. */
	wrt_reg_dword(req->req_q_in, req->ring_index);

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(vha, rsp);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_SUCCESS;

queuing_error:
	if (status & QDSS_GOT_Q_SPACE) {
		req->outstanding_cmds[handle] = NULL;
		req->cnt += req_cnt;
	}
	/* Cleanup will be performed by the caller (queuecommand) */

	qla_put_fw_resources(sp->qpair, &sp->iores);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_FUNCTION_FAILED;
}

/**
 * qla2xxx_start_scsi_mq() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occurred, else zero.
 */
static int
qla2xxx_start_scsi_mq(srb_t *sp)
{
	int		nseg;
	unsigned long   flags;
	uint32_t	*clr_ptr;
	uint32_t	handle;
	struct cmd_type_7 *cmd_pkt;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct req_que *req = NULL;
	struct rsp_que *rsp;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);
	struct scsi_qla_host *vha = sp->fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_qpair *qpair = sp->qpair;

	if (sp->fcport->edif.enable && (sp->fcport->flags & FCF_FCSP_DEVICE))
		return qla28xx_start_scsi_edif(sp);

	/* Acquire qpair specific lock */
	spin_lock_irqsave(&qpair->qp_lock, flags);

	/* Setup qpair pointers */
	req = qpair->req;
	rsp = qpair->rsp;

	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (__qla2x00_marker(vha, qpair, 0, 0, MK_SYNC_ALL) !=
		    QLA_SUCCESS) {
			spin_unlock_irqrestore(&qpair->qp_lock, flags);
			return QLA_FUNCTION_FAILED;
		}
		vha->marker_needed = 0;
	}

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
	} else
		nseg = 0;

	tot_dsds = nseg;
	req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);

	sp->iores.res_type = RESOURCE_IOCB | RESOURCE_EXCH;
	sp->iores.exch_cnt = 1;
	sp->iores.iocb_cnt = req_cnt;
	if (qla_get_fw_resources(sp->qpair, &sp->iores))
		goto queuing_error;

	if (req->cnt < (req_cnt + 2)) {
		if (IS_SHADOW_REG_CAPABLE(ha)) {
			cnt = *req->out_ptr;
		} else {
			cnt = rd_reg_dword_relaxed(req->req_q_out);
			if (qla2x00_check_reg16_for_disconnect(vha, cnt))
				goto queuing_error;
		}

		if (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
				(req->ring_index - cnt);
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;
	}

	/* Build command packet. */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;

	cmd_pkt = (struct cmd_type_7 *)req->ring_ptr;
	cmd_pkt->handle = make_handle(req->id, handle);

	/* Zero out remaining portion of packet. */
	/*    tagged queuing modifier -- default is TSK_SIMPLE (0). */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* Set NPORT-ID and LUN number*/
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
	cmd_pkt->vp_index = sp->fcport->vha->vp_idx;

	int_to_scsilun(cmd->device->lun, &cmd_pkt->lun);
	host_to_fcp_swap((uint8_t *)&cmd_pkt->lun, sizeof(cmd_pkt->lun));

	cmd_pkt->task = TSK_SIMPLE;

	/* Load SCSI command packet. */
	memcpy(cmd_pkt->fcp_cdb, cmd->cmnd, cmd->cmd_len);
	host_to_fcp_swap(cmd_pkt->fcp_cdb, sizeof(cmd_pkt->fcp_cdb));

	cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));

	/* Build IOCB segments */
	qla24xx_build_scsi_iocbs(sp, cmd_pkt, tot_dsds, req);

	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	wmb();
	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	sp->qpair->cmd_cnt++;
	sp->flags |= SRB_DMA_VALID;

	/* Set chip new ring index. */
	wrt_reg_dword(req->req_q_in, req->ring_index);

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(vha, rsp);

	spin_unlock_irqrestore(&qpair->qp_lock, flags);
	return QLA_SUCCESS;

queuing_error:
	if (tot_dsds)
		scsi_dma_unmap(cmd);

	qla_put_fw_resources(sp->qpair, &sp->iores);
	spin_unlock_irqrestore(&qpair->qp_lock, flags);

	return QLA_FUNCTION_FAILED;
}


/**
 * qla2xxx_dif_start_scsi_mq() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occurred, else zero.
 */
int
qla2xxx_dif_start_scsi_mq(srb_t *sp)
{
	int			nseg;
	unsigned long		flags;
	uint32_t		*clr_ptr;
	uint32_t		handle;
	uint16_t		cnt;
	uint16_t		req_cnt = 0;
	uint16_t		tot_dsds;
	uint16_t		tot_prot_dsds;
	uint16_t		fw_prot_opts = 0;
	struct req_que		*req = NULL;
	struct rsp_que		*rsp = NULL;
	struct scsi_cmnd	*cmd = GET_CMD_SP(sp);
	struct scsi_qla_host	*vha = sp->fcport->vha;
	struct qla_hw_data	*ha = vha->hw;
	struct cmd_type_crc_2	*cmd_pkt;
	uint32_t		status = 0;
	struct qla_qpair	*qpair = sp->qpair;

#define QDSS_GOT_Q_SPACE	BIT_0

	/* Check for host side state */
	if (!qpair->online) {
		cmd->result = DID_NO_CONNECT << 16;
		return QLA_INTERFACE_ERROR;
	}

	if (!qpair->difdix_supported &&
		scsi_get_prot_op(cmd) != SCSI_PROT_NORMAL) {
		cmd->result = DID_NO_CONNECT << 16;
		return QLA_INTERFACE_ERROR;
	}

	/* Only process protection or >16 cdb in this routine */
	if (scsi_get_prot_op(cmd) == SCSI_PROT_NORMAL) {
		if (cmd->cmd_len <= 16)
			return qla2xxx_start_scsi_mq(sp);
	}

	spin_lock_irqsave(&qpair->qp_lock, flags);

	/* Setup qpair pointers */
	rsp = qpair->rsp;
	req = qpair->req;

	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (__qla2x00_marker(vha, qpair, 0, 0, MK_SYNC_ALL) !=
		    QLA_SUCCESS) {
			spin_unlock_irqrestore(&qpair->qp_lock, flags);
			return QLA_FUNCTION_FAILED;
		}
		vha->marker_needed = 0;
	}

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0)
		goto queuing_error;

	/* Compute number of required data segments */
	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
		else
			sp->flags |= SRB_DMA_VALID;

		if ((scsi_get_prot_op(cmd) == SCSI_PROT_READ_INSERT) ||
		    (scsi_get_prot_op(cmd) == SCSI_PROT_WRITE_STRIP)) {
			struct qla2_sgx sgx;
			uint32_t	partial;

			memset(&sgx, 0, sizeof(struct qla2_sgx));
			sgx.tot_bytes = scsi_bufflen(cmd);
			sgx.cur_sg = scsi_sglist(cmd);
			sgx.sp = sp;

			nseg = 0;
			while (qla24xx_get_one_block_sg(
			    cmd->device->sector_size, &sgx, &partial))
				nseg++;
		}
	} else
		nseg = 0;

	/* number of required data segments */
	tot_dsds = nseg;

	/* Compute number of required protection segments */
	if (qla24xx_configure_prot_mode(sp, &fw_prot_opts)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_prot_sglist(cmd),
		    scsi_prot_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
		else
			sp->flags |= SRB_CRC_PROT_DMA_VALID;

		if ((scsi_get_prot_op(cmd) == SCSI_PROT_READ_INSERT) ||
		    (scsi_get_prot_op(cmd) == SCSI_PROT_WRITE_STRIP)) {
			nseg = scsi_bufflen(cmd) / cmd->device->sector_size;
		}
	} else {
		nseg = 0;
	}

	req_cnt = 1;
	/* Total Data and protection sg segment(s) */
	tot_prot_dsds = nseg;
	tot_dsds += nseg;

	sp->iores.res_type = RESOURCE_IOCB | RESOURCE_EXCH;
	sp->iores.exch_cnt = 1;
	sp->iores.iocb_cnt = qla24xx_calc_iocbs(vha, tot_dsds);
	if (qla_get_fw_resources(sp->qpair, &sp->iores))
		goto queuing_error;

	if (req->cnt < (req_cnt + 2)) {
		if (IS_SHADOW_REG_CAPABLE(ha)) {
			cnt = *req->out_ptr;
		} else {
			cnt = rd_reg_dword_relaxed(req->req_q_out);
			if (qla2x00_check_reg16_for_disconnect(vha, cnt))
				goto queuing_error;
		}

		if (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
				(req->ring_index - cnt);
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;
	}

	status |= QDSS_GOT_Q_SPACE;

	/* Build header part of command packet (excluding the OPCODE). */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;

	/* Fill-in common area */
	cmd_pkt = (struct cmd_type_crc_2 *)req->ring_ptr;
	cmd_pkt->handle = make_handle(req->id, handle);

	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);

	/* Set NPORT-ID and LUN number*/
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
	cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
	cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;

	int_to_scsilun(cmd->device->lun, &cmd_pkt->lun);
	host_to_fcp_swap((uint8_t *)&cmd_pkt->lun, sizeof(cmd_pkt->lun));

	/* Total Data and protection segment(s) */
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* Build IOCB segments and adjust for data protection segments */
	if (qla24xx_build_scsi_crc_2_iocbs(sp, (struct cmd_type_crc_2 *)
	    req->ring_ptr, tot_dsds, tot_prot_dsds, fw_prot_opts) !=
		QLA_SUCCESS)
		goto queuing_error;

	cmd_pkt->entry_count = (uint8_t)req_cnt;
	cmd_pkt->timeout = cpu_to_le16(0);
	wmb();

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	sp->qpair->cmd_cnt++;
	/* Set chip new ring index. */
	wrt_reg_dword(req->req_q_in, req->ring_index);

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(vha, rsp);

	spin_unlock_irqrestore(&qpair->qp_lock, flags);

	return QLA_SUCCESS;

queuing_error:
	if (status & QDSS_GOT_Q_SPACE) {
		req->outstanding_cmds[handle] = NULL;
		req->cnt += req_cnt;
	}
	/* Cleanup will be performed by the caller (queuecommand) */

	qla_put_fw_resources(sp->qpair, &sp->iores);
	spin_unlock_irqrestore(&qpair->qp_lock, flags);

	return QLA_FUNCTION_FAILED;
}

/* Generic Control-SRB manipulation functions. */

/* hardware_lock assumed to be held. */

void *
__qla2x00_alloc_iocbs(struct qla_qpair *qpair, srb_t *sp)
{
	scsi_qla_host_t *vha = qpair->vha;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = qpair->req;
	device_reg_t *reg = ISP_QUE_REG(ha, req->id);
	uint32_t handle;
	request_t *pkt;
	uint16_t cnt, req_cnt;

	pkt = NULL;
	req_cnt = 1;
	handle = 0;

	if (sp && (sp->type != SRB_SCSI_CMD)) {
		/* Adjust entry-counts as needed. */
		req_cnt = sp->iocbs;
	}

	/* Check for room on request queue. */
	if (req->cnt < req_cnt + 2) {
		if (qpair->use_shadow_reg)
			cnt = *req->out_ptr;
		else if (ha->mqenable || IS_QLA83XX(ha) || IS_QLA27XX(ha) ||
		    IS_QLA28XX(ha))
			cnt = rd_reg_dword(&reg->isp25mq.req_q_out);
		else if (IS_P3P_TYPE(ha))
			cnt = rd_reg_dword(reg->isp82.req_q_out);
		else if (IS_FWI2_CAPABLE(ha))
			cnt = rd_reg_dword(&reg->isp24.req_q_out);
		else if (IS_QLAFX00(ha))
			cnt = rd_reg_dword(&reg->ispfx00.req_q_out);
		else
			cnt = qla2x00_debounce_register(
			    ISP_REQ_Q_OUT(ha, &reg->isp));

		if (!qpair->use_shadow_reg && cnt == ISP_REG16_DISCONNECT) {
			qla_schedule_eeh_work(vha);
			return NULL;
		}

		if  (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
			    (req->ring_index - cnt);
	}
	if (req->cnt < req_cnt + 2)
		goto queuing_error;

	if (sp) {
		handle = qla2xxx_get_next_handle(req);
		if (handle == 0) {
			ql_log(ql_log_warn, vha, 0x700b,
			    "No room on outstanding cmd array.\n");
			goto queuing_error;
		}

		/* Prep command array. */
		req->current_outstanding_cmd = handle;
		req->outstanding_cmds[handle] = sp;
		sp->handle = handle;
	}

	/* Prep packet */
	req->cnt -= req_cnt;
	pkt = req->ring_ptr;
	memset(pkt, 0, REQUEST_ENTRY_SIZE);
	if (IS_QLAFX00(ha)) {
		wrt_reg_byte((u8 __force __iomem *)&pkt->entry_count, req_cnt);
		wrt_reg_dword((__le32 __force __iomem *)&pkt->handle, handle);
	} else {
		pkt->entry_count = req_cnt;
		pkt->handle = handle;
	}

	return pkt;

queuing_error:
	qpair->tgt_counters.num_alloc_iocb_failed++;
	return pkt;
}

void *
qla2x00_alloc_iocbs_ready(struct qla_qpair *qpair, srb_t *sp)
{
	scsi_qla_host_t *vha = qpair->vha;

	if (qla2x00_reset_active(vha))
		return NULL;

	return __qla2x00_alloc_iocbs(qpair, sp);
}

void *
qla2x00_alloc_iocbs(struct scsi_qla_host *vha, srb_t *sp)
{
	return __qla2x00_alloc_iocbs(vha->hw->base_qpair, sp);
}

static void
qla24xx_prli_iocb(srb_t *sp, struct logio_entry_24xx *logio)
{
	struct srb_iocb *lio = &sp->u.iocb_cmd;

	logio->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	logio->control_flags = cpu_to_le16(LCF_COMMAND_PRLI);
	if (lio->u.logio.flags & SRB_LOGIN_NVME_PRLI) {
		logio->control_flags |= cpu_to_le16(LCF_NVME_PRLI);
		if (sp->vha->flags.nvme_first_burst)
			logio->io_parameter[0] =
				cpu_to_le32(NVME_PRLI_SP_FIRST_BURST);
		if (sp->vha->flags.nvme2_enabled) {
			/* Set service parameter BIT_7 for NVME CONF support */
			logio->io_parameter[0] |=
				cpu_to_le32(NVME_PRLI_SP_CONF);
			/* Set service parameter BIT_8 for SLER support */
			logio->io_parameter[0] |=
				cpu_to_le32(NVME_PRLI_SP_SLER);
			/* Set service parameter BIT_9 for PI control support */
			logio->io_parameter[0] |=
				cpu_to_le32(NVME_PRLI_SP_PI_CTRL);
		}
	}

	logio->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	logio->port_id[0] = sp->fcport->d_id.b.al_pa;
	logio->port_id[1] = sp->fcport->d_id.b.area;
	logio->port_id[2] = sp->fcport->d_id.b.domain;
	logio->vp_index = sp->vha->vp_idx;
}

static void
qla24xx_login_iocb(srb_t *sp, struct logio_entry_24xx *logio)
{
	struct srb_iocb *lio = &sp->u.iocb_cmd;

	logio->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	logio->control_flags = cpu_to_le16(LCF_COMMAND_PLOGI);

	if (lio->u.logio.flags & SRB_LOGIN_PRLI_ONLY) {
		logio->control_flags = cpu_to_le16(LCF_COMMAND_PRLI);
	} else {
		logio->control_flags = cpu_to_le16(LCF_COMMAND_PLOGI);
		if (lio->u.logio.flags & SRB_LOGIN_COND_PLOGI)
			logio->control_flags |= cpu_to_le16(LCF_COND_PLOGI);
		if (lio->u.logio.flags & SRB_LOGIN_SKIP_PRLI)
			logio->control_flags |= cpu_to_le16(LCF_SKIP_PRLI);
		if (lio->u.logio.flags & SRB_LOGIN_FCSP) {
			logio->control_flags |=
			    cpu_to_le16(LCF_COMMON_FEAT | LCF_SKIP_PRLI);
			logio->io_parameter[0] =
			    cpu_to_le32(LIO_COMM_FEAT_FCSP | LIO_COMM_FEAT_CIO);
		}
	}
	logio->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	logio->port_id[0] = sp->fcport->d_id.b.al_pa;
	logio->port_id[1] = sp->fcport->d_id.b.area;
	logio->port_id[2] = sp->fcport->d_id.b.domain;
	logio->vp_index = sp->vha->vp_idx;
}

static void
qla2x00_login_iocb(srb_t *sp, struct mbx_entry *mbx)
{
	struct qla_hw_data *ha = sp->vha->hw;
	struct srb_iocb *lio = &sp->u.iocb_cmd;
	uint16_t opts;

	mbx->entry_type = MBX_IOCB_TYPE;
	SET_TARGET_ID(ha, mbx->loop_id, sp->fcport->loop_id);
	mbx->mb0 = cpu_to_le16(MBC_LOGIN_FABRIC_PORT);
	opts = lio->u.logio.flags & SRB_LOGIN_COND_PLOGI ? BIT_0 : 0;
	opts |= lio->u.logio.flags & SRB_LOGIN_SKIP_PRLI ? BIT_1 : 0;
	if (HAS_EXTENDED_IDS(ha)) {
		mbx->mb1 = cpu_to_le16(sp->fcport->loop_id);
		mbx->mb10 = cpu_to_le16(opts);
	} else {
		mbx->mb1 = cpu_to_le16((sp->fcport->loop_id << 8) | opts);
	}
	mbx->mb2 = cpu_to_le16(sp->fcport->d_id.b.domain);
	mbx->mb3 = cpu_to_le16(sp->fcport->d_id.b.area << 8 |
	    sp->fcport->d_id.b.al_pa);
	mbx->mb9 = cpu_to_le16(sp->vha->vp_idx);
}

static void
qla24xx_logout_iocb(srb_t *sp, struct logio_entry_24xx *logio)
{
	u16 control_flags = LCF_COMMAND_LOGO;
	logio->entry_type = LOGINOUT_PORT_IOCB_TYPE;

	if (sp->fcport->explicit_logout) {
		control_flags |= LCF_EXPL_LOGO|LCF_FREE_NPORT;
	} else {
		control_flags |= LCF_IMPL_LOGO;

		if (!sp->fcport->keep_nport_handle)
			control_flags |= LCF_FREE_NPORT;
	}

	logio->control_flags = cpu_to_le16(control_flags);
	logio->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	logio->port_id[0] = sp->fcport->d_id.b.al_pa;
	logio->port_id[1] = sp->fcport->d_id.b.area;
	logio->port_id[2] = sp->fcport->d_id.b.domain;
	logio->vp_index = sp->vha->vp_idx;
}

static void
qla2x00_logout_iocb(srb_t *sp, struct mbx_entry *mbx)
{
	struct qla_hw_data *ha = sp->vha->hw;

	mbx->entry_type = MBX_IOCB_TYPE;
	SET_TARGET_ID(ha, mbx->loop_id, sp->fcport->loop_id);
	mbx->mb0 = cpu_to_le16(MBC_LOGOUT_FABRIC_PORT);
	mbx->mb1 = HAS_EXTENDED_IDS(ha) ?
	    cpu_to_le16(sp->fcport->loop_id) :
	    cpu_to_le16(sp->fcport->loop_id << 8);
	mbx->mb2 = cpu_to_le16(sp->fcport->d_id.b.domain);
	mbx->mb3 = cpu_to_le16(sp->fcport->d_id.b.area << 8 |
	    sp->fcport->d_id.b.al_pa);
	mbx->mb9 = cpu_to_le16(sp->vha->vp_idx);
	/* Implicit: mbx->mbx10 = 0. */
}

static void
qla24xx_adisc_iocb(srb_t *sp, struct logio_entry_24xx *logio)
{
	logio->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	logio->control_flags = cpu_to_le16(LCF_COMMAND_ADISC);
	logio->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	logio->vp_index = sp->vha->vp_idx;
}

static void
qla2x00_adisc_iocb(srb_t *sp, struct mbx_entry *mbx)
{
	struct qla_hw_data *ha = sp->vha->hw;

	mbx->entry_type = MBX_IOCB_TYPE;
	SET_TARGET_ID(ha, mbx->loop_id, sp->fcport->loop_id);
	mbx->mb0 = cpu_to_le16(MBC_GET_PORT_DATABASE);
	if (HAS_EXTENDED_IDS(ha)) {
		mbx->mb1 = cpu_to_le16(sp->fcport->loop_id);
		mbx->mb10 = cpu_to_le16(BIT_0);
	} else {
		mbx->mb1 = cpu_to_le16((sp->fcport->loop_id << 8) | BIT_0);
	}
	mbx->mb2 = cpu_to_le16(MSW(ha->async_pd_dma));
	mbx->mb3 = cpu_to_le16(LSW(ha->async_pd_dma));
	mbx->mb6 = cpu_to_le16(MSW(MSD(ha->async_pd_dma)));
	mbx->mb7 = cpu_to_le16(LSW(MSD(ha->async_pd_dma)));
	mbx->mb9 = cpu_to_le16(sp->vha->vp_idx);
}

static void
qla24xx_tm_iocb(srb_t *sp, struct tsk_mgmt_entry *tsk)
{
	uint32_t flags;
	uint64_t lun;
	struct fc_port *fcport = sp->fcport;
	scsi_qla_host_t *vha = fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	struct srb_iocb *iocb = &sp->u.iocb_cmd;
	struct req_que *req = sp->qpair->req;

	flags = iocb->u.tmf.flags;
	lun = iocb->u.tmf.lun;

	tsk->entry_type = TSK_MGMT_IOCB_TYPE;
	tsk->entry_count = 1;
	tsk->handle = make_handle(req->id, tsk->handle);
	tsk->nport_handle = cpu_to_le16(fcport->loop_id);
	tsk->timeout = cpu_to_le16(ha->r_a_tov / 10 * 2);
	tsk->control_flags = cpu_to_le32(flags);
	tsk->port_id[0] = fcport->d_id.b.al_pa;
	tsk->port_id[1] = fcport->d_id.b.area;
	tsk->port_id[2] = fcport->d_id.b.domain;
	tsk->vp_index = fcport->vha->vp_idx;

	if (flags & (TCF_LUN_RESET | TCF_ABORT_TASK_SET|
	    TCF_CLEAR_TASK_SET|TCF_CLEAR_ACA)) {
		int_to_scsilun(lun, &tsk->lun);
		host_to_fcp_swap((uint8_t *)&tsk->lun,
			sizeof(tsk->lun));
	}
}

static void
qla2x00_async_done(struct srb *sp, int res)
{
	if (del_timer(&sp->u.iocb_cmd.timer)) {
		/*
		 * Successfully cancelled the timeout handler
		 * ref: TMR
		 */
		if (kref_put(&sp->cmd_kref, qla2x00_sp_release))
			return;
	}
	sp->async_done(sp, res);
}

void
qla2x00_sp_release(struct kref *kref)
{
	struct srb *sp = container_of(kref, struct srb, cmd_kref);

	sp->free(sp);
}

void
qla2x00_init_async_sp(srb_t *sp, unsigned long tmo,
		     void (*done)(struct srb *sp, int res))
{
	timer_setup(&sp->u.iocb_cmd.timer, qla2x00_sp_timeout, 0);
	sp->done = qla2x00_async_done;
	sp->async_done = done;
	sp->free = qla2x00_sp_free;
	sp->u.iocb_cmd.timeout = qla2x00_async_iocb_timeout;
	sp->u.iocb_cmd.timer.expires = jiffies + tmo * HZ;
	if (IS_QLAFX00(sp->vha->hw) && sp->type == SRB_FXIOCB_DCMD)
		init_completion(&sp->u.iocb_cmd.u.fxiocb.fxiocb_comp);
	sp->start_timer = 1;
}

static void qla2x00_els_dcmd_sp_free(srb_t *sp)
{
	struct srb_iocb *elsio = &sp->u.iocb_cmd;

	kfree(sp->fcport);

	if (elsio->u.els_logo.els_logo_pyld)
		dma_free_coherent(&sp->vha->hw->pdev->dev, DMA_POOL_SIZE,
		    elsio->u.els_logo.els_logo_pyld,
		    elsio->u.els_logo.els_logo_pyld_dma);

	del_timer(&elsio->timer);
	qla2x00_rel_sp(sp);
}

static void
qla2x00_els_dcmd_iocb_timeout(void *data)
{
	srb_t *sp = data;
	fc_port_t *fcport = sp->fcport;
	struct scsi_qla_host *vha = sp->vha;
	struct srb_iocb *lio = &sp->u.iocb_cmd;
	unsigned long flags = 0;
	int res, h;

	ql_dbg(ql_dbg_io, vha, 0x3069,
	    "%s Timeout, hdl=%x, portid=%02x%02x%02x\n",
	    sp->name, sp->handle, fcport->d_id.b.domain, fcport->d_id.b.area,
	    fcport->d_id.b.al_pa);

	/* Abort the exchange */
	res = qla24xx_async_abort_cmd(sp, false);
	if (res) {
		ql_dbg(ql_dbg_io, vha, 0x3070,
		    "mbx abort_command failed.\n");
		spin_lock_irqsave(sp->qpair->qp_lock_ptr, flags);
		for (h = 1; h < sp->qpair->req->num_outstanding_cmds; h++) {
			if (sp->qpair->req->outstanding_cmds[h] == sp) {
				sp->qpair->req->outstanding_cmds[h] = NULL;
				break;
			}
		}
		spin_unlock_irqrestore(sp->qpair->qp_lock_ptr, flags);
		complete(&lio->u.els_logo.comp);
	} else {
		ql_dbg(ql_dbg_io, vha, 0x3071,
		    "mbx abort_command success.\n");
	}
}

static void qla2x00_els_dcmd_sp_done(srb_t *sp, int res)
{
	fc_port_t *fcport = sp->fcport;
	struct srb_iocb *lio = &sp->u.iocb_cmd;
	struct scsi_qla_host *vha = sp->vha;

	ql_dbg(ql_dbg_io, vha, 0x3072,
	    "%s hdl=%x, portid=%02x%02x%02x done\n",
	    sp->name, sp->handle, fcport->d_id.b.domain,
	    fcport->d_id.b.area, fcport->d_id.b.al_pa);

	complete(&lio->u.els_logo.comp);
}

int
qla24xx_els_dcmd_iocb(scsi_qla_host_t *vha, int els_opcode,
    port_id_t remote_did)
{
	srb_t *sp;
	fc_port_t *fcport = NULL;
	struct srb_iocb *elsio = NULL;
	struct qla_hw_data *ha = vha->hw;
	struct els_logo_payload logo_pyld;
	int rval = QLA_SUCCESS;

	fcport = qla2x00_alloc_fcport(vha, GFP_KERNEL);
	if (!fcport) {
	       ql_log(ql_log_info, vha, 0x70e5, "fcport allocation failed\n");
	       return -ENOMEM;
	}

	/* Alloc SRB structure
	 * ref: INIT
	 */
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		kfree(fcport);
		ql_log(ql_log_info, vha, 0x70e6,
		 "SRB allocation failed\n");
		return -ENOMEM;
	}

	elsio = &sp->u.iocb_cmd;
	fcport->loop_id = 0xFFFF;
	fcport->d_id.b.domain = remote_did.b.domain;
	fcport->d_id.b.area = remote_did.b.area;
	fcport->d_id.b.al_pa = remote_did.b.al_pa;

	ql_dbg(ql_dbg_io, vha, 0x3073, "portid=%02x%02x%02x done\n",
	    fcport->d_id.b.domain, fcport->d_id.b.area, fcport->d_id.b.al_pa);

	sp->type = SRB_ELS_DCMD;
	sp->name = "ELS_DCMD";
	sp->fcport = fcport;
	qla2x00_init_async_sp(sp, ELS_DCMD_TIMEOUT,
			      qla2x00_els_dcmd_sp_done);
	sp->free = qla2x00_els_dcmd_sp_free;
	sp->u.iocb_cmd.timeout = qla2x00_els_dcmd_iocb_timeout;
	init_completion(&sp->u.iocb_cmd.u.els_logo.comp);

	elsio->u.els_logo.els_logo_pyld = dma_alloc_coherent(&ha->pdev->dev,
			    DMA_POOL_SIZE, &elsio->u.els_logo.els_logo_pyld_dma,
			    GFP_KERNEL);

	if (!elsio->u.els_logo.els_logo_pyld) {
		/* ref: INIT */
		kref_put(&sp->cmd_kref, qla2x00_sp_release);
		return QLA_FUNCTION_FAILED;
	}

	memset(&logo_pyld, 0, sizeof(struct els_logo_payload));

	elsio->u.els_logo.els_cmd = els_opcode;
	logo_pyld.opcode = els_opcode;
	logo_pyld.s_id[0] = vha->d_id.b.al_pa;
	logo_pyld.s_id[1] = vha->d_id.b.area;
	logo_pyld.s_id[2] = vha->d_id.b.domain;
	host_to_fcp_swap(logo_pyld.s_id, sizeof(uint32_t));
	memcpy(&logo_pyld.wwpn, vha->port_name, WWN_SIZE);

	memcpy(elsio->u.els_logo.els_logo_pyld, &logo_pyld,
	    sizeof(struct els_logo_payload));
	ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x3075, "LOGO buffer:");
	ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x010a,
		       elsio->u.els_logo.els_logo_pyld,
		       sizeof(*elsio->u.els_logo.els_logo_pyld));

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		/* ref: INIT */
		kref_put(&sp->cmd_kref, qla2x00_sp_release);
		return QLA_FUNCTION_FAILED;
	}

	ql_dbg(ql_dbg_io, vha, 0x3074,
	    "%s LOGO sent, hdl=%x, loopid=%x, portid=%02x%02x%02x.\n",
	    sp->name, sp->handle, fcport->loop_id, fcport->d_id.b.domain,
	    fcport->d_id.b.area, fcport->d_id.b.al_pa);

	wait_for_completion(&elsio->u.els_logo.comp);

	/* ref: INIT */
	kref_put(&sp->cmd_kref, qla2x00_sp_release);
	return rval;
}

static void
qla24xx_els_logo_iocb(srb_t *sp, struct els_entry_24xx *els_iocb)
{
	scsi_qla_host_t *vha = sp->vha;
	struct srb_iocb *elsio = &sp->u.iocb_cmd;

	els_iocb->entry_type = ELS_IOCB_TYPE;
	els_iocb->entry_count = 1;
	els_iocb->sys_define = 0;
	els_iocb->entry_status = 0;
	els_iocb->handle = sp->handle;
	els_iocb->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	els_iocb->tx_dsd_count = cpu_to_le16(1);
	els_iocb->vp_index = vha->vp_idx;
	els_iocb->sof_type = EST_SOFI3;
	els_iocb->rx_dsd_count = 0;
	els_iocb->opcode = elsio->u.els_logo.els_cmd;

	els_iocb->d_id[0] = sp->fcport->d_id.b.al_pa;
	els_iocb->d_id[1] = sp->fcport->d_id.b.area;
	els_iocb->d_id[2] = sp->fcport->d_id.b.domain;
	/* For SID the byte order is different than DID */
	els_iocb->s_id[1] = vha->d_id.b.al_pa;
	els_iocb->s_id[2] = vha->d_id.b.area;
	els_iocb->s_id[0] = vha->d_id.b.domain;

	if (elsio->u.els_logo.els_cmd == ELS_DCMD_PLOGI) {
		if (vha->hw->flags.edif_enabled)
			els_iocb->control_flags = cpu_to_le16(ECF_SEC_LOGIN);
		else
			els_iocb->control_flags = 0;
		els_iocb->tx_byte_count = els_iocb->tx_len =
			cpu_to_le32(sizeof(struct els_plogi_payload));
		put_unaligned_le64(elsio->u.els_plogi.els_plogi_pyld_dma,
				   &els_iocb->tx_address);
		els_iocb->rx_dsd_count = cpu_to_le16(1);
		els_iocb->rx_byte_count = els_iocb->rx_len =
			cpu_to_le32(sizeof(struct els_plogi_payload));
		put_unaligned_le64(elsio->u.els_plogi.els_resp_pyld_dma,
				   &els_iocb->rx_address);

		ql_dbg(ql_dbg_io + ql_dbg_buffer, vha, 0x3073,
		    "PLOGI ELS IOCB:\n");
		ql_dump_buffer(ql_log_info, vha, 0x0109,
		    (uint8_t *)els_iocb,
		    sizeof(*els_iocb));
	} else {
		els_iocb->tx_byte_count =
			cpu_to_le32(sizeof(struct els_logo_payload));
		put_unaligned_le64(elsio->u.els_logo.els_logo_pyld_dma,
				   &els_iocb->tx_address);
		els_iocb->tx_len = cpu_to_le32(sizeof(struct els_logo_payload));

		els_iocb->rx_byte_count = 0;
		els_iocb->rx_address = 0;
		els_iocb->rx_len = 0;
		ql_dbg(ql_dbg_io + ql_dbg_buffer, vha, 0x3076,
		       "LOGO ELS IOCB:");
		ql_dump_buffer(ql_log_info, vha, 0x010b,
			       els_iocb,
			       sizeof(*els_iocb));
	}

	sp->vha->qla_stats.control_requests++;
}

void
qla2x00_els_dcmd2_iocb_timeout(void *data)
{
	srb_t *sp = data;
	fc_port_t *fcport = sp->fcport;
	struct scsi_qla_host *vha = sp->vha;
	unsigned long flags = 0;
	int res, h;

	ql_dbg(ql_dbg_io + ql_dbg_disc, vha, 0x3069,
	    "%s hdl=%x ELS Timeout, %8phC portid=%06x\n",
	    sp->name, sp->handle, fcport->port_name, fcport->d_id.b24);

	/* Abort the exchange */
	res = qla24xx_async_abort_cmd(sp, false);
	ql_dbg(ql_dbg_io, vha, 0x3070,
	    "mbx abort_command %s\n",
	    (res == QLA_SUCCESS) ? "successful" : "failed");
	if (res) {
		spin_lock_irqsave(sp->qpair->qp_lock_ptr, flags);
		for (h = 1; h < sp->qpair->req->num_outstanding_cmds; h++) {
			if (sp->qpair->req->outstanding_cmds[h] == sp) {
				sp->qpair->req->outstanding_cmds[h] = NULL;
				break;
			}
		}
		spin_unlock_irqrestore(sp->qpair->qp_lock_ptr, flags);
		sp->done(sp, QLA_FUNCTION_TIMEOUT);
	}
}

void qla2x00_els_dcmd2_free(scsi_qla_host_t *vha, struct els_plogi *els_plogi)
{
	if (els_plogi->els_plogi_pyld)
		dma_free_coherent(&vha->hw->pdev->dev,
				  els_plogi->tx_size,
				  els_plogi->els_plogi_pyld,
				  els_plogi->els_plogi_pyld_dma);

	if (els_plogi->els_resp_pyld)
		dma_free_coherent(&vha->hw->pdev->dev,
				  els_plogi->rx_size,
				  els_plogi->els_resp_pyld,
				  els_plogi->els_resp_pyld_dma);
}

static void qla2x00_els_dcmd2_sp_done(srb_t *sp, int res)
{
	fc_port_t *fcport = sp->fcport;
	struct srb_iocb *lio = &sp->u.iocb_cmd;
	struct scsi_qla_host *vha = sp->vha;
	struct event_arg ea;
	struct qla_work_evt *e;
	struct fc_port *conflict_fcport;
	port_id_t cid;	/* conflict Nport id */
	const __le32 *fw_status = sp->u.iocb_cmd.u.els_plogi.fw_status;
	u16 lid;

	ql_dbg(ql_dbg_disc, vha, 0x3072,
	    "%s ELS done rc %d hdl=%x, portid=%06x %8phC\n",
	    sp->name, res, sp->handle, fcport->d_id.b24, fcport->port_name);

	fcport->flags &= ~(FCF_ASYNC_SENT|FCF_ASYNC_ACTIVE);
	/* For edif, set logout on delete to ensure any residual key from FW is flushed.*/
	fcport->logout_on_delete = 1;
	fcport->chip_reset = vha->hw->base_qpair->chip_reset;

	if (sp->flags & SRB_WAKEUP_ON_COMP)
		complete(&lio->u.els_plogi.comp);
	else {
		switch (le32_to_cpu(fw_status[0])) {
		case CS_DATA_UNDERRUN:
		case CS_COMPLETE:
			memset(&ea, 0, sizeof(ea));
			ea.fcport = fcport;
			ea.rc = res;
			qla_handle_els_plogi_done(vha, &ea);
			break;

		case CS_IOCB_ERROR:
			switch (le32_to_cpu(fw_status[1])) {
			case LSC_SCODE_PORTID_USED:
				lid = le32_to_cpu(fw_status[2]) & 0xffff;
				qlt_find_sess_invalidate_other(vha,
				    wwn_to_u64(fcport->port_name),
				    fcport->d_id, lid, &conflict_fcport);
				if (conflict_fcport) {
					/*
					 * Another fcport shares the same
					 * loop_id & nport id; conflict
					 * fcport needs to finish cleanup
					 * before this fcport can proceed
					 * to login.
					 */
					conflict_fcport->conflict = fcport;
					fcport->login_pause = 1;
					ql_dbg(ql_dbg_disc, vha, 0x20ed,
					    "%s %d %8phC pid %06x inuse with lid %#x.\n",
					    __func__, __LINE__,
					    fcport->port_name,
					    fcport->d_id.b24, lid);
				} else {
					ql_dbg(ql_dbg_disc, vha, 0x20ed,
					    "%s %d %8phC pid %06x inuse with lid %#x sched del\n",
					    __func__, __LINE__,
					    fcport->port_name,
					    fcport->d_id.b24, lid);
					qla2x00_clear_loop_id(fcport);
					set_bit(lid, vha->hw->loop_id_map);
					fcport->loop_id = lid;
					fcport->keep_nport_handle = 0;
					qlt_schedule_sess_for_deletion(fcport);
				}
				break;

			case LSC_SCODE_NPORT_USED:
				cid.b.domain = (le32_to_cpu(fw_status[2]) >> 16)
					& 0xff;
				cid.b.area   = (le32_to_cpu(fw_status[2]) >>  8)
					& 0xff;
				cid.b.al_pa  = le32_to_cpu(fw_status[2]) & 0xff;
				cid.b.rsvd_1 = 0;

				ql_dbg(ql_dbg_disc, vha, 0x20ec,
				    "%s %d %8phC lid %#x in use with pid %06x post gnl\n",
				    __func__, __LINE__, fcport->port_name,
				    fcport->loop_id, cid.b24);
				set_bit(fcport->loop_id,
				    vha->hw->loop_id_map);
				fcport->loop_id = FC_NO_LOOP_ID;
				qla24xx_post_gnl_work(vha, fcport);
				break;

			case LSC_SCODE_NOXCB:
				vha->hw->exch_starvation++;
				if (vha->hw->exch_starvation > 5) {
					ql_log(ql_log_warn, vha, 0xd046,
					    "Exchange starvation. Resetting RISC\n");
					vha->hw->exch_starvation = 0;
					set_bit(ISP_ABORT_NEEDED,
					    &vha->dpc_flags);
					qla2xxx_wake_dpc(vha);
					break;
				}
				fallthrough;
			default:
				ql_dbg(ql_dbg_disc, vha, 0x20eb,
				    "%s %8phC cmd error fw_status 0x%x 0x%x 0x%x\n",
				    __func__, sp->fcport->port_name,
				    fw_status[0], fw_status[1], fw_status[2]);

				fcport->flags &= ~FCF_ASYNC_SENT;
				qlt_schedule_sess_for_deletion(fcport);
				break;
			}
			break;

		default:
			ql_dbg(ql_dbg_disc, vha, 0x20eb,
			    "%s %8phC cmd error 2 fw_status 0x%x 0x%x 0x%x\n",
			    __func__, sp->fcport->port_name,
			    fw_status[0], fw_status[1], fw_status[2]);

			sp->fcport->flags &= ~FCF_ASYNC_SENT;
			qlt_schedule_sess_for_deletion(fcport);
			break;
		}

		e = qla2x00_alloc_work(vha, QLA_EVT_UNMAP);
		if (!e) {
			struct srb_iocb *elsio = &sp->u.iocb_cmd;

			qla2x00_els_dcmd2_free(vha, &elsio->u.els_plogi);
			/* ref: INIT */
			kref_put(&sp->cmd_kref, qla2x00_sp_release);
			return;
		}
		e->u.iosb.sp = sp;
		qla2x00_post_work(vha, e);
	}
}

int
qla24xx_els_dcmd2_iocb(scsi_qla_host_t *vha, int els_opcode,
    fc_port_t *fcport, bool wait)
{
	srb_t *sp;
	struct srb_iocb *elsio = NULL;
	struct qla_hw_data *ha = vha->hw;
	int rval = QLA_SUCCESS;
	void	*ptr, *resp_ptr;

	/* Alloc SRB structure
	 * ref: INIT
	 */
	sp = qla2x00_get_sp(vha, fcport, GFP_KERNEL);
	if (!sp) {
		ql_log(ql_log_info, vha, 0x70e6,
		 "SRB allocation failed\n");
		fcport->flags &= ~FCF_ASYNC_ACTIVE;
		return -ENOMEM;
	}

	fcport->flags |= FCF_ASYNC_SENT;
	qla2x00_set_fcport_disc_state(fcport, DSC_LOGIN_PEND);
	elsio = &sp->u.iocb_cmd;
	ql_dbg(ql_dbg_io, vha, 0x3073,
	       "%s Enter: PLOGI portid=%06x\n", __func__, fcport->d_id.b24);

	if (wait)
		sp->flags = SRB_WAKEUP_ON_COMP;

	sp->type = SRB_ELS_DCMD;
	sp->name = "ELS_DCMD";
	sp->fcport = fcport;
	qla2x00_init_async_sp(sp, ELS_DCMD_TIMEOUT + 2,
			     qla2x00_els_dcmd2_sp_done);
	sp->u.iocb_cmd.timeout = qla2x00_els_dcmd2_iocb_timeout;

	elsio->u.els_plogi.tx_size = elsio->u.els_plogi.rx_size = DMA_POOL_SIZE;

	ptr = elsio->u.els_plogi.els_plogi_pyld =
	    dma_alloc_coherent(&ha->pdev->dev, elsio->u.els_plogi.tx_size,
		&elsio->u.els_plogi.els_plogi_pyld_dma, GFP_KERNEL);

	if (!elsio->u.els_plogi.els_plogi_pyld) {
		rval = QLA_FUNCTION_FAILED;
		goto out;
	}

	resp_ptr = elsio->u.els_plogi.els_resp_pyld =
	    dma_alloc_coherent(&ha->pdev->dev, elsio->u.els_plogi.rx_size,
		&elsio->u.els_plogi.els_resp_pyld_dma, GFP_KERNEL);

	if (!elsio->u.els_plogi.els_resp_pyld) {
		rval = QLA_FUNCTION_FAILED;
		goto out;
	}

	ql_dbg(ql_dbg_io, vha, 0x3073, "PLOGI %p %p\n", ptr, resp_ptr);

	memset(ptr, 0, sizeof(struct els_plogi_payload));
	memset(resp_ptr, 0, sizeof(struct els_plogi_payload));
	memcpy(elsio->u.els_plogi.els_plogi_pyld->data,
	    &ha->plogi_els_payld.fl_csp, LOGIN_TEMPLATE_SIZE);

	elsio->u.els_plogi.els_cmd = els_opcode;
	elsio->u.els_plogi.els_plogi_pyld->opcode = els_opcode;

	if (els_opcode == ELS_DCMD_PLOGI && DBELL_ACTIVE(vha)) {
		struct fc_els_flogi *p = ptr;

		p->fl_csp.sp_features |= cpu_to_be16(FC_SP_FT_SEC);
	}

	ql_dbg(ql_dbg_disc + ql_dbg_buffer, vha, 0x3073, "PLOGI buffer:\n");
	ql_dump_buffer(ql_dbg_disc + ql_dbg_buffer, vha, 0x0109,
	    (uint8_t *)elsio->u.els_plogi.els_plogi_pyld,
	    sizeof(*elsio->u.els_plogi.els_plogi_pyld));

	init_completion(&elsio->u.els_plogi.comp);
	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_disc, vha, 0x3074,
		    "%s PLOGI sent, hdl=%x, loopid=%x, to port_id %06x from port_id %06x\n",
		    sp->name, sp->handle, fcport->loop_id,
		    fcport->d_id.b24, vha->d_id.b24);
	}

	if (wait) {
		wait_for_completion(&elsio->u.els_plogi.comp);

		if (elsio->u.els_plogi.comp_status != CS_COMPLETE)
			rval = QLA_FUNCTION_FAILED;
	} else {
		goto done;
	}

out:
	fcport->flags &= ~(FCF_ASYNC_SENT | FCF_ASYNC_ACTIVE);
	qla2x00_els_dcmd2_free(vha, &elsio->u.els_plogi);
	/* ref: INIT */
	kref_put(&sp->cmd_kref, qla2x00_sp_release);
done:
	return rval;
}

/* it is assume qpair lock is held */
void qla_els_pt_iocb(struct scsi_qla_host *vha,
	struct els_entry_24xx *els_iocb,
	struct qla_els_pt_arg *a)
{
	els_iocb->entry_type = ELS_IOCB_TYPE;
	els_iocb->entry_count = 1;
	els_iocb->sys_define = 0;
	els_iocb->entry_status = 0;
	els_iocb->handle = QLA_SKIP_HANDLE;
	els_iocb->nport_handle = a->nport_handle;
	els_iocb->rx_xchg_address = a->rx_xchg_address;
	els_iocb->tx_dsd_count = cpu_to_le16(1);
	els_iocb->vp_index = a->vp_idx;
	els_iocb->sof_type = EST_SOFI3;
	els_iocb->rx_dsd_count = cpu_to_le16(0);
	els_iocb->opcode = a->els_opcode;

	els_iocb->d_id[0] = a->did.b.al_pa;
	els_iocb->d_id[1] = a->did.b.area;
	els_iocb->d_id[2] = a->did.b.domain;
	/* For SID the byte order is different than DID */
	els_iocb->s_id[1] = vha->d_id.b.al_pa;
	els_iocb->s_id[2] = vha->d_id.b.area;
	els_iocb->s_id[0] = vha->d_id.b.domain;

	els_iocb->control_flags = cpu_to_le16(a->control_flags);

	els_iocb->tx_byte_count = cpu_to_le32(a->tx_byte_count);
	els_iocb->tx_len = cpu_to_le32(a->tx_len);
	put_unaligned_le64(a->tx_addr, &els_iocb->tx_address);

	els_iocb->rx_byte_count = cpu_to_le32(a->rx_byte_count);
	els_iocb->rx_len = cpu_to_le32(a->rx_len);
	put_unaligned_le64(a->rx_addr, &els_iocb->rx_address);
}

static void
qla24xx_els_iocb(srb_t *sp, struct els_entry_24xx *els_iocb)
{
	struct bsg_job *bsg_job = sp->u.bsg_job;
	struct fc_bsg_request *bsg_request = bsg_job->request;

        els_iocb->entry_type = ELS_IOCB_TYPE;
        els_iocb->entry_count = 1;
        els_iocb->sys_define = 0;
        els_iocb->entry_status = 0;
        els_iocb->handle = sp->handle;
	els_iocb->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	els_iocb->tx_dsd_count = cpu_to_le16(bsg_job->request_payload.sg_cnt);
	els_iocb->vp_index = sp->vha->vp_idx;
        els_iocb->sof_type = EST_SOFI3;
	els_iocb->rx_dsd_count = cpu_to_le16(bsg_job->reply_payload.sg_cnt);

	els_iocb->opcode =
	    sp->type == SRB_ELS_CMD_RPT ?
	    bsg_request->rqst_data.r_els.els_code :
	    bsg_request->rqst_data.h_els.command_code;
	els_iocb->d_id[0] = sp->fcport->d_id.b.al_pa;
	els_iocb->d_id[1] = sp->fcport->d_id.b.area;
	els_iocb->d_id[2] = sp->fcport->d_id.b.domain;
        els_iocb->control_flags = 0;
        els_iocb->rx_byte_count =
            cpu_to_le32(bsg_job->reply_payload.payload_len);
        els_iocb->tx_byte_count =
            cpu_to_le32(bsg_job->request_payload.payload_len);

	put_unaligned_le64(sg_dma_address(bsg_job->request_payload.sg_list),
			   &els_iocb->tx_address);
        els_iocb->tx_len = cpu_to_le32(sg_dma_len
            (bsg_job->request_payload.sg_list));

	put_unaligned_le64(sg_dma_address(bsg_job->reply_payload.sg_list),
			   &els_iocb->rx_address);
        els_iocb->rx_len = cpu_to_le32(sg_dma_len
            (bsg_job->reply_payload.sg_list));

	sp->vha->qla_stats.control_requests++;
}

static void
qla2x00_ct_iocb(srb_t *sp, ms_iocb_entry_t *ct_iocb)
{
	uint16_t        avail_dsds;
	struct dsd64	*cur_dsd;
	struct scatterlist *sg;
	int index;
	uint16_t tot_dsds;
	scsi_qla_host_t *vha = sp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct bsg_job *bsg_job = sp->u.bsg_job;
	int entry_count = 1;

	memset(ct_iocb, 0, sizeof(ms_iocb_entry_t));
	ct_iocb->entry_type = CT_IOCB_TYPE;
	ct_iocb->entry_status = 0;
	ct_iocb->handle1 = sp->handle;
	SET_TARGET_ID(ha, ct_iocb->loop_id, sp->fcport->loop_id);
	ct_iocb->status = cpu_to_le16(0);
	ct_iocb->control_flags = cpu_to_le16(0);
	ct_iocb->timeout = 0;
	ct_iocb->cmd_dsd_count =
	    cpu_to_le16(bsg_job->request_payload.sg_cnt);
	ct_iocb->total_dsd_count =
	    cpu_to_le16(bsg_job->request_payload.sg_cnt + 1);
	ct_iocb->req_bytecount =
	    cpu_to_le32(bsg_job->request_payload.payload_len);
	ct_iocb->rsp_bytecount =
	    cpu_to_le32(bsg_job->reply_payload.payload_len);

	put_unaligned_le64(sg_dma_address(bsg_job->request_payload.sg_list),
			   &ct_iocb->req_dsd.address);
	ct_iocb->req_dsd.length = ct_iocb->req_bytecount;

	put_unaligned_le64(sg_dma_address(bsg_job->reply_payload.sg_list),
			   &ct_iocb->rsp_dsd.address);
	ct_iocb->rsp_dsd.length = ct_iocb->rsp_bytecount;

	avail_dsds = 1;
	cur_dsd = &ct_iocb->rsp_dsd;
	index = 0;
	tot_dsds = bsg_job->reply_payload.sg_cnt;

	for_each_sg(bsg_job->reply_payload.sg_list, sg, tot_dsds, index) {
		cont_a64_entry_t *cont_pkt;

		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			* Five DSDs are available in the Cont.
			* Type 1 IOCB.
			       */
			cont_pkt = qla2x00_prep_cont_type1_iocb(vha,
			    vha->hw->req_q_map[0]);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = 5;
			entry_count++;
		}

		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}
	ct_iocb->entry_count = entry_count;

	sp->vha->qla_stats.control_requests++;
}

static void
qla24xx_ct_iocb(srb_t *sp, struct ct_entry_24xx *ct_iocb)
{
	uint16_t        avail_dsds;
	struct dsd64	*cur_dsd;
	struct scatterlist *sg;
	int index;
	uint16_t cmd_dsds, rsp_dsds;
	scsi_qla_host_t *vha = sp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct bsg_job *bsg_job = sp->u.bsg_job;
	int entry_count = 1;
	cont_a64_entry_t *cont_pkt = NULL;

	ct_iocb->entry_type = CT_IOCB_TYPE;
        ct_iocb->entry_status = 0;
        ct_iocb->sys_define = 0;
        ct_iocb->handle = sp->handle;

	ct_iocb->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	ct_iocb->vp_index = sp->vha->vp_idx;
	ct_iocb->comp_status = cpu_to_le16(0);

	cmd_dsds = bsg_job->request_payload.sg_cnt;
	rsp_dsds = bsg_job->reply_payload.sg_cnt;

	ct_iocb->cmd_dsd_count = cpu_to_le16(cmd_dsds);
        ct_iocb->timeout = 0;
	ct_iocb->rsp_dsd_count = cpu_to_le16(rsp_dsds);
        ct_iocb->cmd_byte_count =
            cpu_to_le32(bsg_job->request_payload.payload_len);

	avail_dsds = 2;
	cur_dsd = ct_iocb->dsd;
	index = 0;

	for_each_sg(bsg_job->request_payload.sg_list, sg, cmd_dsds, index) {
		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			 * Five DSDs are available in the Cont.
			 * Type 1 IOCB.
			 */
			cont_pkt = qla2x00_prep_cont_type1_iocb(
			    vha, ha->req_q_map[0]);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = 5;
			entry_count++;
		}

		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}

	index = 0;

	for_each_sg(bsg_job->reply_payload.sg_list, sg, rsp_dsds, index) {
		/* Allocate additional continuation packets? */
		if (avail_dsds == 0) {
			/*
			* Five DSDs are available in the Cont.
			* Type 1 IOCB.
			       */
			cont_pkt = qla2x00_prep_cont_type1_iocb(vha,
			    ha->req_q_map[0]);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = 5;
			entry_count++;
		}

		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}
        ct_iocb->entry_count = entry_count;
}

/*
 * qla82xx_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occurred, else zero.
 */
int
qla82xx_start_scsi(srb_t *sp)
{
	int		nseg;
	unsigned long   flags;
	struct scsi_cmnd *cmd;
	uint32_t	*clr_ptr;
	uint32_t	handle;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct device_reg_82xx __iomem *reg;
	uint32_t dbval;
	__be32 *fcp_dl;
	uint8_t additional_cdb_len;
	struct ct6_dsd *ctx;
	struct scsi_qla_host *vha = sp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = NULL;
	struct rsp_que *rsp = NULL;

	/* Setup device pointers. */
	reg = &ha->iobase->isp82;
	cmd = GET_CMD_SP(sp);
	req = vha->req;
	rsp = ha->rsp_q_map[0];

	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	dbval = 0x04 | (ha->portnum << 5);

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (qla2x00_marker(vha, ha->base_qpair,
			0, 0, MK_SYNC_ALL) != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x300c,
			    "qla2x00_marker failed for cmd=%p.\n", cmd);
			return QLA_FUNCTION_FAILED;
		}
		vha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (scsi_sg_count(cmd)) {
		nseg = dma_map_sg(&ha->pdev->dev, scsi_sglist(cmd),
		    scsi_sg_count(cmd), cmd->sc_data_direction);
		if (unlikely(!nseg))
			goto queuing_error;
	} else
		nseg = 0;

	tot_dsds = nseg;

	if (tot_dsds > ql2xshiftctondsd) {
		struct cmd_type_6 *cmd_pkt;
		uint16_t more_dsd_lists = 0;
		struct dsd_dma *dsd_ptr;
		uint16_t i;

		more_dsd_lists = qla24xx_calc_dsd_lists(tot_dsds);
		if ((more_dsd_lists + ha->gbl_dsd_inuse) >= NUM_DSD_CHAIN) {
			ql_dbg(ql_dbg_io, vha, 0x300d,
			    "Num of DSD list %d is than %d for cmd=%p.\n",
			    more_dsd_lists + ha->gbl_dsd_inuse, NUM_DSD_CHAIN,
			    cmd);
			goto queuing_error;
		}

		if (more_dsd_lists <= ha->gbl_dsd_avail)
			goto sufficient_dsds;
		else
			more_dsd_lists -= ha->gbl_dsd_avail;

		for (i = 0; i < more_dsd_lists; i++) {
			dsd_ptr = kzalloc(sizeof(struct dsd_dma), GFP_ATOMIC);
			if (!dsd_ptr) {
				ql_log(ql_log_fatal, vha, 0x300e,
				    "Failed to allocate memory for dsd_dma "
				    "for cmd=%p.\n", cmd);
				goto queuing_error;
			}

			dsd_ptr->dsd_addr = dma_pool_alloc(ha->dl_dma_pool,
				GFP_ATOMIC, &dsd_ptr->dsd_list_dma);
			if (!dsd_ptr->dsd_addr) {
				kfree(dsd_ptr);
				ql_log(ql_log_fatal, vha, 0x300f,
				    "Failed to allocate memory for dsd_addr "
				    "for cmd=%p.\n", cmd);
				goto queuing_error;
			}
			list_add_tail(&dsd_ptr->list, &ha->gbl_dsd_list);
			ha->gbl_dsd_avail++;
		}

sufficient_dsds:
		req_cnt = 1;

		if (req->cnt < (req_cnt + 2)) {
			cnt = (uint16_t)rd_reg_dword_relaxed(
				&reg->req_q_out[0]);
			if (req->ring_index < cnt)
				req->cnt = cnt - req->ring_index;
			else
				req->cnt = req->length -
					(req->ring_index - cnt);
			if (req->cnt < (req_cnt + 2))
				goto queuing_error;
		}

		ctx = &sp->u.scmd.ct6_ctx;

		memset(ctx, 0, sizeof(struct ct6_dsd));
		ctx->fcp_cmnd = dma_pool_zalloc(ha->fcp_cmnd_dma_pool,
			GFP_ATOMIC, &ctx->fcp_cmnd_dma);
		if (!ctx->fcp_cmnd) {
			ql_log(ql_log_fatal, vha, 0x3011,
			    "Failed to allocate fcp_cmnd for cmd=%p.\n", cmd);
			goto queuing_error;
		}

		/* Initialize the DSD list and dma handle */
		INIT_LIST_HEAD(&ctx->dsd_list);
		ctx->dsd_use_cnt = 0;

		if (cmd->cmd_len > 16) {
			additional_cdb_len = cmd->cmd_len - 16;
			if ((cmd->cmd_len % 4) != 0) {
				/* SCSI command bigger than 16 bytes must be
				 * multiple of 4
				 */
				ql_log(ql_log_warn, vha, 0x3012,
				    "scsi cmd len %d not multiple of 4 "
				    "for cmd=%p.\n", cmd->cmd_len, cmd);
				goto queuing_error_fcp_cmnd;
			}
			ctx->fcp_cmnd_len = 12 + cmd->cmd_len + 4;
		} else {
			additional_cdb_len = 0;
			ctx->fcp_cmnd_len = 12 + 16 + 4;
		}

		cmd_pkt = (struct cmd_type_6 *)req->ring_ptr;
		cmd_pkt->handle = make_handle(req->id, handle);

		/* Zero out remaining portion of packet. */
		/*    tagged queuing modifier -- default is TSK_SIMPLE (0). */
		clr_ptr = (uint32_t *)cmd_pkt + 2;
		memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
		cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

		/* Set NPORT-ID and LUN number*/
		cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
		cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
		cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
		cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
		cmd_pkt->vp_index = sp->vha->vp_idx;

		/* Build IOCB segments */
		if (qla24xx_build_scsi_type_6_iocbs(sp, cmd_pkt, tot_dsds))
			goto queuing_error_fcp_cmnd;

		int_to_scsilun(cmd->device->lun, &cmd_pkt->lun);
		host_to_fcp_swap((uint8_t *)&cmd_pkt->lun, sizeof(cmd_pkt->lun));

		/* build FCP_CMND IU */
		int_to_scsilun(cmd->device->lun, &ctx->fcp_cmnd->lun);
		ctx->fcp_cmnd->additional_cdb_len = additional_cdb_len;

		if (cmd->sc_data_direction == DMA_TO_DEVICE)
			ctx->fcp_cmnd->additional_cdb_len |= 1;
		else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
			ctx->fcp_cmnd->additional_cdb_len |= 2;

		/* Populate the FCP_PRIO. */
		if (ha->flags.fcp_prio_enabled)
			ctx->fcp_cmnd->task_attribute |=
			    sp->fcport->fcp_prio << 3;

		memcpy(ctx->fcp_cmnd->cdb, cmd->cmnd, cmd->cmd_len);

		fcp_dl = (__be32 *)(ctx->fcp_cmnd->cdb + 16 +
		    additional_cdb_len);
		*fcp_dl = htonl((uint32_t)scsi_bufflen(cmd));

		cmd_pkt->fcp_cmnd_dseg_len = cpu_to_le16(ctx->fcp_cmnd_len);
		put_unaligned_le64(ctx->fcp_cmnd_dma,
				   &cmd_pkt->fcp_cmnd_dseg_address);

		sp->flags |= SRB_FCP_CMND_DMA_VALID;
		cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));
		/* Set total data segment count. */
		cmd_pkt->entry_count = (uint8_t)req_cnt;
		/* Specify response queue number where
		 * completion should happen
		 */
		cmd_pkt->entry_status = (uint8_t) rsp->id;
	} else {
		struct cmd_type_7 *cmd_pkt;

		req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);
		if (req->cnt < (req_cnt + 2)) {
			cnt = (uint16_t)rd_reg_dword_relaxed(
			    &reg->req_q_out[0]);
			if (req->ring_index < cnt)
				req->cnt = cnt - req->ring_index;
			else
				req->cnt = req->length -
					(req->ring_index - cnt);
		}
		if (req->cnt < (req_cnt + 2))
			goto queuing_error;

		cmd_pkt = (struct cmd_type_7 *)req->ring_ptr;
		cmd_pkt->handle = make_handle(req->id, handle);

		/* Zero out remaining portion of packet. */
		/* tagged queuing modifier -- default is TSK_SIMPLE (0).*/
		clr_ptr = (uint32_t *)cmd_pkt + 2;
		memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
		cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

		/* Set NPORT-ID and LUN number*/
		cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
		cmd_pkt->port_id[0] = sp->fcport->d_id.b.al_pa;
		cmd_pkt->port_id[1] = sp->fcport->d_id.b.area;
		cmd_pkt->port_id[2] = sp->fcport->d_id.b.domain;
		cmd_pkt->vp_index = sp->vha->vp_idx;

		int_to_scsilun(cmd->device->lun, &cmd_pkt->lun);
		host_to_fcp_swap((uint8_t *)&cmd_pkt->lun,
		    sizeof(cmd_pkt->lun));

		/* Populate the FCP_PRIO. */
		if (ha->flags.fcp_prio_enabled)
			cmd_pkt->task |= sp->fcport->fcp_prio << 3;

		/* Load SCSI command packet. */
		memcpy(cmd_pkt->fcp_cdb, cmd->cmnd, cmd->cmd_len);
		host_to_fcp_swap(cmd_pkt->fcp_cdb, sizeof(cmd_pkt->fcp_cdb));

		cmd_pkt->byte_count = cpu_to_le32((uint32_t)scsi_bufflen(cmd));

		/* Build IOCB segments */
		qla24xx_build_scsi_iocbs(sp, cmd_pkt, tot_dsds, req);

		/* Set total data segment count. */
		cmd_pkt->entry_count = (uint8_t)req_cnt;
		/* Specify response queue number where
		 * completion should happen.
		 */
		cmd_pkt->entry_status = (uint8_t) rsp->id;

	}
	/* Build command packet. */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	req->cnt -= req_cnt;
	wmb();

	/* Adjust ring index. */
	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	sp->flags |= SRB_DMA_VALID;

	/* Set chip new ring index. */
	/* write, read and verify logic */
	dbval = dbval | (req->id << 8) | (req->ring_index << 16);
	if (ql2xdbwr)
		qla82xx_wr_32(ha, (uintptr_t __force)ha->nxdb_wr_ptr, dbval);
	else {
		wrt_reg_dword(ha->nxdb_wr_ptr, dbval);
		wmb();
		while (rd_reg_dword(ha->nxdb_rd_ptr) != dbval) {
			wrt_reg_dword(ha->nxdb_wr_ptr, dbval);
			wmb();
		}
	}

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (vha->flags.process_response_queue &&
	    rsp->ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(vha, rsp);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return QLA_SUCCESS;

queuing_error_fcp_cmnd:
	dma_pool_free(ha->fcp_cmnd_dma_pool, ctx->fcp_cmnd, ctx->fcp_cmnd_dma);
queuing_error:
	if (tot_dsds)
		scsi_dma_unmap(cmd);

	if (sp->u.scmd.crc_ctx) {
		mempool_free(sp->u.scmd.crc_ctx, ha->ctx_mempool);
		sp->u.scmd.crc_ctx = NULL;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_FUNCTION_FAILED;
}

static void
qla24xx_abort_iocb(srb_t *sp, struct abort_entry_24xx *abt_iocb)
{
	struct srb_iocb *aio = &sp->u.iocb_cmd;
	scsi_qla_host_t *vha = sp->vha;
	struct req_que *req = sp->qpair->req;
	srb_t *orig_sp = sp->cmd_sp;

	memset(abt_iocb, 0, sizeof(struct abort_entry_24xx));
	abt_iocb->entry_type = ABORT_IOCB_TYPE;
	abt_iocb->entry_count = 1;
	abt_iocb->handle = make_handle(req->id, sp->handle);
	if (sp->fcport) {
		abt_iocb->nport_handle = cpu_to_le16(sp->fcport->loop_id);
		abt_iocb->port_id[0] = sp->fcport->d_id.b.al_pa;
		abt_iocb->port_id[1] = sp->fcport->d_id.b.area;
		abt_iocb->port_id[2] = sp->fcport->d_id.b.domain;
	}
	abt_iocb->handle_to_abort =
		make_handle(le16_to_cpu(aio->u.abt.req_que_no),
			    aio->u.abt.cmd_hndl);
	abt_iocb->vp_index = vha->vp_idx;
	abt_iocb->req_que_no = aio->u.abt.req_que_no;

	/* need to pass original sp */
	if (orig_sp)
		qla_nvme_abort_set_option(abt_iocb, orig_sp);

	/* Send the command to the firmware */
	wmb();
}

static void
qla2x00_mb_iocb(srb_t *sp, struct mbx_24xx_entry *mbx)
{
	int i, sz;

	mbx->entry_type = MBX_IOCB_TYPE;
	mbx->handle = sp->handle;
	sz = min(ARRAY_SIZE(mbx->mb), ARRAY_SIZE(sp->u.iocb_cmd.u.mbx.out_mb));

	for (i = 0; i < sz; i++)
		mbx->mb[i] = sp->u.iocb_cmd.u.mbx.out_mb[i];
}

static void
qla2x00_ctpthru_cmd_iocb(srb_t *sp, struct ct_entry_24xx *ct_pkt)
{
	sp->u.iocb_cmd.u.ctarg.iocb = ct_pkt;
	qla24xx_prep_ms_iocb(sp->vha, &sp->u.iocb_cmd.u.ctarg);
	ct_pkt->handle = sp->handle;
}

static void qla2x00_send_notify_ack_iocb(srb_t *sp,
	struct nack_to_isp *nack)
{
	struct imm_ntfy_from_isp *ntfy = sp->u.iocb_cmd.u.nack.ntfy;

	nack->entry_type = NOTIFY_ACK_TYPE;
	nack->entry_count = 1;
	nack->ox_id = ntfy->ox_id;

	nack->u.isp24.handle = sp->handle;
	nack->u.isp24.nport_handle = ntfy->u.isp24.nport_handle;
	if (le16_to_cpu(ntfy->u.isp24.status) == IMM_NTFY_ELS) {
		nack->u.isp24.flags = ntfy->u.isp24.flags &
			cpu_to_le16(NOTIFY24XX_FLAGS_PUREX_IOCB);
	}
	nack->u.isp24.srr_rx_id = ntfy->u.isp24.srr_rx_id;
	nack->u.isp24.status = ntfy->u.isp24.status;
	nack->u.isp24.status_subcode = ntfy->u.isp24.status_subcode;
	nack->u.isp24.fw_handle = ntfy->u.isp24.fw_handle;
	nack->u.isp24.exchange_address = ntfy->u.isp24.exchange_address;
	nack->u.isp24.srr_rel_offs = ntfy->u.isp24.srr_rel_offs;
	nack->u.isp24.srr_ui = ntfy->u.isp24.srr_ui;
	nack->u.isp24.srr_flags = 0;
	nack->u.isp24.srr_reject_code = 0;
	nack->u.isp24.srr_reject_code_expl = 0;
	nack->u.isp24.vp_index = ntfy->u.isp24.vp_index;

	if (ntfy->u.isp24.status_subcode == ELS_PLOGI &&
	    (le16_to_cpu(ntfy->u.isp24.flags) & NOTIFY24XX_FLAGS_FCSP) &&
	    sp->vha->hw->flags.edif_enabled) {
		ql_dbg(ql_dbg_disc, sp->vha, 0x3074,
		    "%s PLOGI NACK sent with FC SECURITY bit, hdl=%x, loopid=%x, to pid %06x\n",
		    sp->name, sp->handle, sp->fcport->loop_id,
		    sp->fcport->d_id.b24);
		nack->u.isp24.flags |= cpu_to_le16(NOTIFY_ACK_FLAGS_FCSP);
	}
}

/*
 * Build NVME LS request
 */
static void
qla_nvme_ls(srb_t *sp, struct pt_ls4_request *cmd_pkt)
{
	struct srb_iocb *nvme;

	nvme = &sp->u.iocb_cmd;
	cmd_pkt->entry_type = PT_LS4_REQUEST;
	cmd_pkt->entry_count = 1;
	cmd_pkt->control_flags = cpu_to_le16(CF_LS4_ORIGINATOR << CF_LS4_SHIFT);

	cmd_pkt->timeout = cpu_to_le16(nvme->u.nvme.timeout_sec);
	cmd_pkt->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	cmd_pkt->vp_index = sp->fcport->vha->vp_idx;

	cmd_pkt->tx_dseg_count = cpu_to_le16(1);
	cmd_pkt->tx_byte_count = cpu_to_le32(nvme->u.nvme.cmd_len);
	cmd_pkt->dsd[0].length = cpu_to_le32(nvme->u.nvme.cmd_len);
	put_unaligned_le64(nvme->u.nvme.cmd_dma, &cmd_pkt->dsd[0].address);

	cmd_pkt->rx_dseg_count = cpu_to_le16(1);
	cmd_pkt->rx_byte_count = cpu_to_le32(nvme->u.nvme.rsp_len);
	cmd_pkt->dsd[1].length = cpu_to_le32(nvme->u.nvme.rsp_len);
	put_unaligned_le64(nvme->u.nvme.rsp_dma, &cmd_pkt->dsd[1].address);
}

static void
qla25xx_ctrlvp_iocb(srb_t *sp, struct vp_ctrl_entry_24xx *vce)
{
	int map, pos;

	vce->entry_type = VP_CTRL_IOCB_TYPE;
	vce->handle = sp->handle;
	vce->entry_count = 1;
	vce->command = cpu_to_le16(sp->u.iocb_cmd.u.ctrlvp.cmd);
	vce->vp_count = cpu_to_le16(1);

	/*
	 * index map in firmware starts with 1; decrement index
	 * this is ok as we never use index 0
	 */
	map = (sp->u.iocb_cmd.u.ctrlvp.vp_index - 1) / 8;
	pos = (sp->u.iocb_cmd.u.ctrlvp.vp_index - 1) & 7;
	vce->vp_idx_map[map] |= 1 << pos;
}

static void
qla24xx_prlo_iocb(srb_t *sp, struct logio_entry_24xx *logio)
{
	logio->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	logio->control_flags =
	    cpu_to_le16(LCF_COMMAND_PRLO|LCF_IMPL_PRLO);

	logio->nport_handle = cpu_to_le16(sp->fcport->loop_id);
	logio->port_id[0] = sp->fcport->d_id.b.al_pa;
	logio->port_id[1] = sp->fcport->d_id.b.area;
	logio->port_id[2] = sp->fcport->d_id.b.domain;
	logio->vp_index = sp->fcport->vha->vp_idx;
}

static int qla_get_iocbs_resource(struct srb *sp)
{
	bool get_exch;
	bool push_it_through = false;

	if (!ql2xenforce_iocb_limit) {
		sp->iores.res_type = RESOURCE_NONE;
		return 0;
	}
	sp->iores.res_type = RESOURCE_NONE;

	switch (sp->type) {
	case SRB_TM_CMD:
	case SRB_PRLI_CMD:
	case SRB_ADISC_CMD:
		push_it_through = true;
		fallthrough;
	case SRB_LOGIN_CMD:
	case SRB_ELS_CMD_RPT:
	case SRB_ELS_CMD_HST:
	case SRB_ELS_CMD_HST_NOLOGIN:
	case SRB_CT_CMD:
	case SRB_NVME_LS:
	case SRB_ELS_DCMD:
		get_exch = true;
		break;

	case SRB_FXIOCB_DCMD:
	case SRB_FXIOCB_BCMD:
		sp->iores.res_type = RESOURCE_NONE;
		return 0;

	case SRB_SA_UPDATE:
	case SRB_SA_REPLACE:
	case SRB_MB_IOCB:
	case SRB_ABT_CMD:
	case SRB_NACK_PLOGI:
	case SRB_NACK_PRLI:
	case SRB_NACK_LOGO:
	case SRB_LOGOUT_CMD:
	case SRB_CTRL_VP:
	case SRB_MARKER:
	default:
		push_it_through = true;
		get_exch = false;
	}

	sp->iores.res_type |= RESOURCE_IOCB;
	sp->iores.iocb_cnt = 1;
	if (get_exch) {
		sp->iores.res_type |= RESOURCE_EXCH;
		sp->iores.exch_cnt = 1;
	}
	if (push_it_through)
		sp->iores.res_type |= RESOURCE_FORCE;

	return qla_get_fw_resources(sp->qpair, &sp->iores);
}

static void
qla_marker_iocb(srb_t *sp, struct mrk_entry_24xx *mrk)
{
	mrk->entry_type = MARKER_TYPE;
	mrk->modifier = sp->u.iocb_cmd.u.tmf.modifier;
	if (sp->u.iocb_cmd.u.tmf.modifier != MK_SYNC_ALL) {
		mrk->nport_handle = cpu_to_le16(sp->u.iocb_cmd.u.tmf.loop_id);
		int_to_scsilun(sp->u.iocb_cmd.u.tmf.lun, (struct scsi_lun *)&mrk->lun);
		host_to_fcp_swap(mrk->lun, sizeof(mrk->lun));
		mrk->vp_index = sp->u.iocb_cmd.u.tmf.vp_index;
	}
}

int
qla2x00_start_sp(srb_t *sp)
{
	int rval = QLA_SUCCESS;
	scsi_qla_host_t *vha = sp->vha;
	struct qla_hw_data *ha = vha->hw;
	struct qla_qpair *qp = sp->qpair;
	void *pkt;
	unsigned long flags;

	if (vha->hw->flags.eeh_busy)
		return -EIO;

	spin_lock_irqsave(qp->qp_lock_ptr, flags);
	rval = qla_get_iocbs_resource(sp);
	if (rval) {
		spin_unlock_irqrestore(qp->qp_lock_ptr, flags);
		return -EAGAIN;
	}

	pkt = __qla2x00_alloc_iocbs(sp->qpair, sp);
	if (!pkt) {
		rval = EAGAIN;
		ql_log(ql_log_warn, vha, 0x700c,
		    "qla2x00_alloc_iocbs failed.\n");
		goto done;
	}

	switch (sp->type) {
	case SRB_LOGIN_CMD:
		IS_FWI2_CAPABLE(ha) ?
		    qla24xx_login_iocb(sp, pkt) :
		    qla2x00_login_iocb(sp, pkt);
		break;
	case SRB_PRLI_CMD:
		qla24xx_prli_iocb(sp, pkt);
		break;
	case SRB_LOGOUT_CMD:
		IS_FWI2_CAPABLE(ha) ?
		    qla24xx_logout_iocb(sp, pkt) :
		    qla2x00_logout_iocb(sp, pkt);
		break;
	case SRB_ELS_CMD_RPT:
	case SRB_ELS_CMD_HST:
		qla24xx_els_iocb(sp, pkt);
		break;
	case SRB_ELS_CMD_HST_NOLOGIN:
		qla_els_pt_iocb(sp->vha, pkt,  &sp->u.bsg_cmd.u.els_arg);
		((struct els_entry_24xx *)pkt)->handle = sp->handle;
		break;
	case SRB_CT_CMD:
		IS_FWI2_CAPABLE(ha) ?
		    qla24xx_ct_iocb(sp, pkt) :
		    qla2x00_ct_iocb(sp, pkt);
		break;
	case SRB_ADISC_CMD:
		IS_FWI2_CAPABLE(ha) ?
		    qla24xx_adisc_iocb(sp, pkt) :
		    qla2x00_adisc_iocb(sp, pkt);
		break;
	case SRB_TM_CMD:
		IS_QLAFX00(ha) ?
		    qlafx00_tm_iocb(sp, pkt) :
		    qla24xx_tm_iocb(sp, pkt);
		break;
	case SRB_FXIOCB_DCMD:
	case SRB_FXIOCB_BCMD:
		qlafx00_fxdisc_iocb(sp, pkt);
		break;
	case SRB_NVME_LS:
		qla_nvme_ls(sp, pkt);
		break;
	case SRB_ABT_CMD:
		IS_QLAFX00(ha) ?
			qlafx00_abort_iocb(sp, pkt) :
			qla24xx_abort_iocb(sp, pkt);
		break;
	case SRB_ELS_DCMD:
		qla24xx_els_logo_iocb(sp, pkt);
		break;
	case SRB_CT_PTHRU_CMD:
		qla2x00_ctpthru_cmd_iocb(sp, pkt);
		break;
	case SRB_MB_IOCB:
		qla2x00_mb_iocb(sp, pkt);
		break;
	case SRB_NACK_PLOGI:
	case SRB_NACK_PRLI:
	case SRB_NACK_LOGO:
		qla2x00_send_notify_ack_iocb(sp, pkt);
		break;
	case SRB_CTRL_VP:
		qla25xx_ctrlvp_iocb(sp, pkt);
		break;
	case SRB_PRLO_CMD:
		qla24xx_prlo_iocb(sp, pkt);
		break;
	case SRB_SA_UPDATE:
		qla24xx_sa_update_iocb(sp, pkt);
		break;
	case SRB_SA_REPLACE:
		qla24xx_sa_replace_iocb(sp, pkt);
		break;
	case SRB_MARKER:
		qla_marker_iocb(sp, pkt);
		break;
	default:
		break;
	}

	if (sp->start_timer) {
		/* ref: TMR timer ref
		 * this code should be just before start_iocbs function
		 * This will make sure that caller function don't to do
		 * kref_put even on failure
		 */
		kref_get(&sp->cmd_kref);
		add_timer(&sp->u.iocb_cmd.timer);
	}

	wmb();
	qla2x00_start_iocbs(vha, qp->req);
done:
	if (rval)
		qla_put_fw_resources(sp->qpair, &sp->iores);
	spin_unlock_irqrestore(qp->qp_lock_ptr, flags);
	return rval;
}

static void
qla25xx_build_bidir_iocb(srb_t *sp, struct scsi_qla_host *vha,
				struct cmd_bidir *cmd_pkt, uint32_t tot_dsds)
{
	uint16_t avail_dsds;
	struct dsd64 *cur_dsd;
	uint32_t req_data_len = 0;
	uint32_t rsp_data_len = 0;
	struct scatterlist *sg;
	int index;
	int entry_count = 1;
	struct bsg_job *bsg_job = sp->u.bsg_job;

	/*Update entry type to indicate bidir command */
	put_unaligned_le32(COMMAND_BIDIRECTIONAL, &cmd_pkt->entry_type);

	/* Set the transfer direction, in this set both flags
	 * Also set the BD_WRAP_BACK flag, firmware will take care
	 * assigning DID=SID for outgoing pkts.
	 */
	cmd_pkt->wr_dseg_count = cpu_to_le16(bsg_job->request_payload.sg_cnt);
	cmd_pkt->rd_dseg_count = cpu_to_le16(bsg_job->reply_payload.sg_cnt);
	cmd_pkt->control_flags = cpu_to_le16(BD_WRITE_DATA | BD_READ_DATA |
							BD_WRAP_BACK);

	req_data_len = rsp_data_len = bsg_job->request_payload.payload_len;
	cmd_pkt->wr_byte_count = cpu_to_le32(req_data_len);
	cmd_pkt->rd_byte_count = cpu_to_le32(rsp_data_len);
	cmd_pkt->timeout = cpu_to_le16(qla2x00_get_async_timeout(vha) + 2);

	vha->bidi_stats.transfer_bytes += req_data_len;
	vha->bidi_stats.io_count++;

	vha->qla_stats.output_bytes += req_data_len;
	vha->qla_stats.output_requests++;

	/* Only one dsd is available for bidirectional IOCB, remaining dsds
	 * are bundled in continuation iocb
	 */
	avail_dsds = 1;
	cur_dsd = &cmd_pkt->fcp_dsd;

	index = 0;

	for_each_sg(bsg_job->request_payload.sg_list, sg,
				bsg_job->request_payload.sg_cnt, index) {
		cont_a64_entry_t *cont_pkt;

		/* Allocate additional continuation packets */
		if (avail_dsds == 0) {
			/* Continuation type 1 IOCB can accomodate
			 * 5 DSDS
			 */
			cont_pkt = qla2x00_prep_cont_type1_iocb(vha, vha->req);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = 5;
			entry_count++;
		}
		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}
	/* For read request DSD will always goes to continuation IOCB
	 * and follow the write DSD. If there is room on the current IOCB
	 * then it is added to that IOCB else new continuation IOCB is
	 * allocated.
	 */
	for_each_sg(bsg_job->reply_payload.sg_list, sg,
				bsg_job->reply_payload.sg_cnt, index) {
		cont_a64_entry_t *cont_pkt;

		/* Allocate additional continuation packets */
		if (avail_dsds == 0) {
			/* Continuation type 1 IOCB can accomodate
			 * 5 DSDS
			 */
			cont_pkt = qla2x00_prep_cont_type1_iocb(vha, vha->req);
			cur_dsd = cont_pkt->dsd;
			avail_dsds = 5;
			entry_count++;
		}
		append_dsd64(&cur_dsd, sg);
		avail_dsds--;
	}
	/* This value should be same as number of IOCB required for this cmd */
	cmd_pkt->entry_count = entry_count;
}

int
qla2x00_start_bidir(srb_t *sp, struct scsi_qla_host *vha, uint32_t tot_dsds)
{

	struct qla_hw_data *ha = vha->hw;
	unsigned long flags;
	uint32_t handle;
	uint16_t req_cnt;
	uint16_t cnt;
	uint32_t *clr_ptr;
	struct cmd_bidir *cmd_pkt = NULL;
	struct rsp_que *rsp;
	struct req_que *req;
	int rval = EXT_STATUS_OK;

	rval = QLA_SUCCESS;

	rsp = ha->rsp_q_map[0];
	req = vha->req;

	/* Send marker if required */
	if (vha->marker_needed != 0) {
		if (qla2x00_marker(vha, ha->base_qpair,
			0, 0, MK_SYNC_ALL) != QLA_SUCCESS)
			return EXT_STATUS_MAILBOX;
		vha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	handle = qla2xxx_get_next_handle(req);
	if (handle == 0) {
		rval = EXT_STATUS_BUSY;
		goto queuing_error;
	}

	/* Calculate number of IOCB required */
	req_cnt = qla24xx_calc_iocbs(vha, tot_dsds);

	/* Check for room on request queue. */
	if (req->cnt < req_cnt + 2) {
		if (IS_SHADOW_REG_CAPABLE(ha)) {
			cnt = *req->out_ptr;
		} else {
			cnt = rd_reg_dword_relaxed(req->req_q_out);
			if (qla2x00_check_reg16_for_disconnect(vha, cnt))
				goto queuing_error;
		}

		if  (req->ring_index < cnt)
			req->cnt = cnt - req->ring_index;
		else
			req->cnt = req->length -
				(req->ring_index - cnt);
	}
	if (req->cnt < req_cnt + 2) {
		rval = EXT_STATUS_BUSY;
		goto queuing_error;
	}

	cmd_pkt = (struct cmd_bidir *)req->ring_ptr;
	cmd_pkt->handle = make_handle(req->id, handle);

	/* Zero out remaining portion of packet. */
	/* tagged queuing modifier -- default is TSK_SIMPLE (0).*/
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);

	/* Set NPORT-ID  (of vha)*/
	cmd_pkt->nport_handle = cpu_to_le16(vha->self_login_loop_id);
	cmd_pkt->port_id[0] = vha->d_id.b.al_pa;
	cmd_pkt->port_id[1] = vha->d_id.b.area;
	cmd_pkt->port_id[2] = vha->d_id.b.domain;

	qla25xx_build_bidir_iocb(sp, vha, cmd_pkt, tot_dsds);
	cmd_pkt->entry_status = (uint8_t) rsp->id;
	/* Build command packet. */
	req->current_outstanding_cmd = handle;
	req->outstanding_cmds[handle] = sp;
	sp->handle = handle;
	req->cnt -= req_cnt;

	/* Send the command to the firmware */
	wmb();
	qla2x00_start_iocbs(vha, req);
queuing_error:
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return rval;
}
