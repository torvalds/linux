/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/blkdev.h>
#include <linux/delay.h>

#include <scsi/scsi_tcq.h>

static inline uint16_t qla2x00_get_cmd_direction(struct scsi_cmnd *cmd);
static inline cont_entry_t *qla2x00_prep_cont_type0_iocb(scsi_qla_host_t *);
static inline cont_a64_entry_t *qla2x00_prep_cont_type1_iocb(scsi_qla_host_t *);
static request_t *qla2x00_req_pkt(scsi_qla_host_t *ha);

/**
 * qla2x00_get_cmd_direction() - Determine control_flag data direction.
 * @cmd: SCSI command
 *
 * Returns the proper CF_* direction based on CDB.
 */
static inline uint16_t
qla2x00_get_cmd_direction(struct scsi_cmnd *cmd)
{
	uint16_t cflags;

	cflags = 0;

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE)
		cflags = CF_WRITE;
	else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		cflags = CF_READ;
	return (cflags);
}

/**
 * qla2x00_calc_iocbs_32() - Determine number of Command Type 2 and
 * Continuation Type 0 IOCBs to allocate.
 *
 * @dsds: number of data segment decriptors needed
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
 * @dsds: number of data segment decriptors needed
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
 * @ha: HA context
 *
 * Returns a pointer to the Continuation Type 0 IOCB packet.
 */
static inline cont_entry_t *
qla2x00_prep_cont_type0_iocb(scsi_qla_host_t *ha)
{
	cont_entry_t *cont_pkt;

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == ha->request_q_length) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else {
		ha->request_ring_ptr++;
	}

	cont_pkt = (cont_entry_t *)ha->request_ring_ptr;

	/* Load packet defaults. */
	*((uint32_t *)(&cont_pkt->entry_type)) =
	    __constant_cpu_to_le32(CONTINUE_TYPE);

	return (cont_pkt);
}

/**
 * qla2x00_prep_cont_type1_iocb() - Initialize a Continuation Type 1 IOCB.
 * @ha: HA context
 *
 * Returns a pointer to the continuation type 1 IOCB packet.
 */
static inline cont_a64_entry_t *
qla2x00_prep_cont_type1_iocb(scsi_qla_host_t *ha)
{
	cont_a64_entry_t *cont_pkt;

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == ha->request_q_length) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else {
		ha->request_ring_ptr++;
	}

	cont_pkt = (cont_a64_entry_t *)ha->request_ring_ptr;

	/* Load packet defaults. */
	*((uint32_t *)(&cont_pkt->entry_type)) =
	    __constant_cpu_to_le32(CONTINUE_A64_TYPE);

	return (cont_pkt);
}

/**
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
	uint32_t	*cur_dsd;
	scsi_qla_host_t	*ha;
	struct scsi_cmnd *cmd;

	cmd = sp->cmd;

	/* Update entry type to indicate Command Type 2 IOCB */
	*((uint32_t *)(&cmd_pkt->entry_type)) =
	    __constant_cpu_to_le32(COMMAND_TYPE);

	/* No data transfer */
	if (cmd->request_bufflen == 0 || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = __constant_cpu_to_le32(0);
		return;
	}

	ha = sp->ha;

	cmd_pkt->control_flags |= cpu_to_le16(qla2x00_get_cmd_direction(cmd));

	/* Three DSDs are available in the Command Type 2 IOCB */
	avail_dsds = 3;
	cur_dsd = (uint32_t *)&cmd_pkt->dseg_0_address;

	/* Load data segments */
	if (cmd->use_sg != 0) {
		struct	scatterlist *cur_seg;
		struct	scatterlist *end_seg;

		cur_seg = (struct scatterlist *)cmd->request_buffer;
		end_seg = cur_seg + tot_dsds;
		while (cur_seg < end_seg) {
			cont_entry_t	*cont_pkt;

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				/*
				 * Seven DSDs are available in the Continuation
				 * Type 0 IOCB.
				 */
				cont_pkt = qla2x00_prep_cont_type0_iocb(ha);
				cur_dsd = (uint32_t *)&cont_pkt->dseg_0_address;
				avail_dsds = 7;
			}

			*cur_dsd++ = cpu_to_le32(sg_dma_address(cur_seg));
			*cur_dsd++ = cpu_to_le32(sg_dma_len(cur_seg));
			avail_dsds--;

			cur_seg++;
		}
	} else {
		*cur_dsd++ = cpu_to_le32(sp->dma_handle);
		*cur_dsd++ = cpu_to_le32(cmd->request_bufflen);
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
	uint32_t	*cur_dsd;
	scsi_qla_host_t	*ha;
	struct scsi_cmnd *cmd;

	cmd = sp->cmd;

	/* Update entry type to indicate Command Type 3 IOCB */
	*((uint32_t *)(&cmd_pkt->entry_type)) =
	    __constant_cpu_to_le32(COMMAND_A64_TYPE);

	/* No data transfer */
	if (cmd->request_bufflen == 0 || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = __constant_cpu_to_le32(0);
		return;
	}

	ha = sp->ha;

	cmd_pkt->control_flags |= cpu_to_le16(qla2x00_get_cmd_direction(cmd));

	/* Two DSDs are available in the Command Type 3 IOCB */
	avail_dsds = 2;
	cur_dsd = (uint32_t *)&cmd_pkt->dseg_0_address;

	/* Load data segments */
	if (cmd->use_sg != 0) {
		struct	scatterlist *cur_seg;
		struct	scatterlist *end_seg;

		cur_seg = (struct scatterlist *)cmd->request_buffer;
		end_seg = cur_seg + tot_dsds;
		while (cur_seg < end_seg) {
			dma_addr_t	sle_dma;
			cont_a64_entry_t *cont_pkt;

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				/*
				 * Five DSDs are available in the Continuation
				 * Type 1 IOCB.
				 */
				cont_pkt = qla2x00_prep_cont_type1_iocb(ha);
				cur_dsd = (uint32_t *)cont_pkt->dseg_0_address;
				avail_dsds = 5;
			}

			sle_dma = sg_dma_address(cur_seg);
			*cur_dsd++ = cpu_to_le32(LSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(MSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(sg_dma_len(cur_seg));
			avail_dsds--;

			cur_seg++;
		}
	} else {
		*cur_dsd++ = cpu_to_le32(LSD(sp->dma_handle));
		*cur_dsd++ = cpu_to_le32(MSD(sp->dma_handle));
		*cur_dsd++ = cpu_to_le32(cmd->request_bufflen);
	}
}

/**
 * qla2x00_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occured, else zero.
 */
int
qla2x00_start_scsi(srb_t *sp)
{
	int		ret;
	unsigned long   flags;
	scsi_qla_host_t	*ha;
	struct scsi_cmnd *cmd;
	uint32_t	*clr_ptr;
	uint32_t        index;
	uint32_t	handle;
	cmd_entry_t	*cmd_pkt;
	struct scatterlist *sg;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct device_reg_2xxx __iomem *reg;

	/* Setup device pointers. */
	ret = 0;
	ha = sp->ha;
	reg = &ha->iobase->isp;
	cmd = sp->cmd;
	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (ha->marker_needed != 0) {
		if (qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) != QLA_SUCCESS) {
			return (QLA_FUNCTION_FAILED);
		}
		ha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Check for room in outstanding command list. */
	handle = ha->current_outstanding_cmd;
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		handle++;
		if (handle == MAX_OUTSTANDING_COMMANDS)
			handle = 1;
		if (ha->outstanding_cmds[handle] == 0)
			break;
	}
	if (index == MAX_OUTSTANDING_COMMANDS)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (cmd->use_sg) {
		sg = (struct scatterlist *) cmd->request_buffer;
		tot_dsds = pci_map_sg(ha->pdev, sg, cmd->use_sg,
		    cmd->sc_data_direction);
		if (tot_dsds == 0)
			goto queuing_error;
	} else if (cmd->request_bufflen) {
		dma_addr_t	req_dma;

		req_dma = pci_map_single(ha->pdev, cmd->request_buffer,
		    cmd->request_bufflen, cmd->sc_data_direction);
		if (dma_mapping_error(req_dma))
			goto queuing_error;

		sp->dma_handle = req_dma;
		tot_dsds = 1;
	}

	/* Calculate the number of request entries needed. */
	req_cnt = ha->isp_ops.calc_req_entries(tot_dsds);
	if (ha->req_q_cnt < (req_cnt + 2)) {
		cnt = RD_REG_WORD_RELAXED(ISP_REQ_Q_OUT(ha, reg));
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt = ha->request_q_length -
			    (ha->req_ring_index - cnt);
	}
	if (ha->req_q_cnt < (req_cnt + 2))
		goto queuing_error;

	/* Build command packet */
	ha->current_outstanding_cmd = handle;
	ha->outstanding_cmds[handle] = sp;
	sp->ha = ha;
	sp->cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	ha->req_q_cnt -= req_cnt;

	cmd_pkt = (cmd_entry_t *)ha->request_ring_ptr;
	cmd_pkt->handle = handle;
	/* Zero out remaining portion of packet. */
	clr_ptr = (uint32_t *)cmd_pkt + 2;
	memset(clr_ptr, 0, REQUEST_ENTRY_SIZE - 8);
	cmd_pkt->dseg_count = cpu_to_le16(tot_dsds);

	/* Set target ID and LUN number*/
	SET_TARGET_ID(ha, cmd_pkt->target, sp->fcport->loop_id);
	cmd_pkt->lun = cpu_to_le16(sp->cmd->device->lun);

	/* Update tagged queuing modifier */
	cmd_pkt->control_flags = __constant_cpu_to_le16(CF_SIMPLE_TAG);

	/* Load SCSI command packet. */
	memcpy(cmd_pkt->scsi_cdb, cmd->cmnd, cmd->cmd_len);
	cmd_pkt->byte_count = cpu_to_le32((uint32_t)cmd->request_bufflen);

	/* Build IOCB segments */
	ha->isp_ops.build_iocbs(sp, cmd_pkt, tot_dsds);

	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	wmb();

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == ha->request_q_length) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	sp->flags |= SRB_DMA_VALID;
	sp->state = SRB_ACTIVE_STATE;

	/* Set chip new ring index. */
	WRT_REG_WORD(ISP_REQ_Q_IN(ha, reg), ha->req_ring_index);
	RD_REG_WORD_RELAXED(ISP_REQ_Q_IN(ha, reg));	/* PCI Posting. */

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (ha->flags.process_response_queue &&
	    ha->response_ring_ptr->signature != RESPONSE_PROCESSED)
		qla2x00_process_response_queue(ha);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (QLA_SUCCESS);

queuing_error:
	if (cmd->use_sg && tot_dsds) {
		sg = (struct scatterlist *) cmd->request_buffer;
		pci_unmap_sg(ha->pdev, sg, cmd->use_sg,
		    cmd->sc_data_direction);
	} else if (tot_dsds) {
		pci_unmap_single(ha->pdev, sp->dma_handle,
		    cmd->request_bufflen, cmd->sc_data_direction);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (QLA_FUNCTION_FAILED);
}

/**
 * qla2x00_marker() - Send a marker IOCB to the firmware.
 * @ha: HA context
 * @loop_id: loop ID
 * @lun: LUN
 * @type: marker modifier
 *
 * Can be called from both normal and interrupt context.
 *
 * Returns non-zero if a failure occured, else zero.
 */
int
__qla2x00_marker(scsi_qla_host_t *ha, uint16_t loop_id, uint16_t lun,
    uint8_t type)
{
	mrk_entry_t *mrk;
	struct mrk_entry_24xx *mrk24;

	mrk24 = NULL;
	mrk = (mrk_entry_t *)qla2x00_req_pkt(ha);
	if (mrk == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Marker IOCB.\n",
		    __func__, ha->host_no));

		return (QLA_FUNCTION_FAILED);
	}

	mrk->entry_type = MARKER_TYPE;
	mrk->modifier = type;
	if (type != MK_SYNC_ALL) {
		if (IS_QLA24XX(ha) || IS_QLA25XX(ha)) {
			mrk24 = (struct mrk_entry_24xx *) mrk;
			mrk24->nport_handle = cpu_to_le16(loop_id);
			mrk24->lun[1] = LSB(lun);
			mrk24->lun[2] = MSB(lun);
		} else {
			SET_TARGET_ID(ha, mrk->target, loop_id);
			mrk->lun = cpu_to_le16(lun);
		}
	}
	wmb();

	qla2x00_isp_cmd(ha);

	return (QLA_SUCCESS);
}

int
qla2x00_marker(scsi_qla_host_t *ha, uint16_t loop_id, uint16_t lun,
    uint8_t type)
{
	int ret;
	unsigned long flags = 0;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ret = __qla2x00_marker(ha, loop_id, lun, type);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return (ret);
}

/**
 * qla2x00_req_pkt() - Retrieve a request packet from the request ring.
 * @ha: HA context
 *
 * Note: The caller must hold the hardware lock before calling this routine.
 *
 * Returns NULL if function failed, else, a pointer to the request packet.
 */
static request_t *
qla2x00_req_pkt(scsi_qla_host_t *ha)
{
	device_reg_t __iomem *reg = ha->iobase;
	request_t	*pkt = NULL;
	uint16_t	cnt;
	uint32_t	*dword_ptr;
	uint32_t	timer;
	uint16_t	req_cnt = 1;

	/* Wait 1 second for slot. */
	for (timer = HZ; timer; timer--) {
		if ((req_cnt + 2) >= ha->req_q_cnt) {
			/* Calculate number of free request entries. */
			if (IS_QLA24XX(ha) || IS_QLA25XX(ha))
				cnt = (uint16_t)RD_REG_DWORD(
				    &reg->isp24.req_q_out);
			else
				cnt = qla2x00_debounce_register(
				    ISP_REQ_Q_OUT(ha, &reg->isp));
			if  (ha->req_ring_index < cnt)
				ha->req_q_cnt = cnt - ha->req_ring_index;
			else
				ha->req_q_cnt = ha->request_q_length -
				    (ha->req_ring_index - cnt);
		}
		/* If room for request in request ring. */
		if ((req_cnt + 2) < ha->req_q_cnt) {
			ha->req_q_cnt--;
			pkt = ha->request_ring_ptr;

			/* Zero out packet. */
			dword_ptr = (uint32_t *)pkt;
			for (cnt = 0; cnt < REQUEST_ENTRY_SIZE / 4; cnt++)
				*dword_ptr++ = 0;

			/* Set system defined field. */
			pkt->sys_define = (uint8_t)ha->req_ring_index;

			/* Set entry count. */
			pkt->entry_count = 1;

			break;
		}

		/* Release ring specific lock */
		spin_unlock(&ha->hardware_lock);

		udelay(2);   /* 2 us */

		/* Check for pending interrupts. */
		/* During init we issue marker directly */
		if (!ha->marker_needed)
			qla2x00_poll(ha);

		spin_lock_irq(&ha->hardware_lock);
	}
	if (!pkt) {
		DEBUG2_3(printk("%s(): **** FAILED ****\n", __func__));
	}

	return (pkt);
}

/**
 * qla2x00_isp_cmd() - Modify the request ring pointer.
 * @ha: HA context
 *
 * Note: The caller must hold the hardware lock before calling this routine.
 */
void
qla2x00_isp_cmd(scsi_qla_host_t *ha)
{
	device_reg_t __iomem *reg = ha->iobase;

	DEBUG5(printk("%s(): IOCB data:\n", __func__));
	DEBUG5(qla2x00_dump_buffer(
	    (uint8_t *)ha->request_ring_ptr, REQUEST_ENTRY_SIZE));

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == ha->request_q_length) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
	if (IS_QLA24XX(ha) || IS_QLA25XX(ha)) {
		WRT_REG_DWORD(&reg->isp24.req_q_in, ha->req_ring_index);
		RD_REG_DWORD_RELAXED(&reg->isp24.req_q_in);
	} else {
		WRT_REG_WORD(ISP_REQ_Q_IN(ha, &reg->isp), ha->req_ring_index);
		RD_REG_WORD_RELAXED(ISP_REQ_Q_IN(ha, &reg->isp));
	}

}

/**
 * qla24xx_calc_iocbs() - Determine number of Command Type 3 and
 * Continuation Type 1 IOCBs to allocate.
 *
 * @dsds: number of data segment decriptors needed
 *
 * Returns the number of IOCB entries needed to store @dsds.
 */
static inline uint16_t
qla24xx_calc_iocbs(uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > 1) {
		iocbs += (dsds - 1) / 5;
		if ((dsds - 1) % 5)
			iocbs++;
	}
	return iocbs;
}

/**
 * qla24xx_build_scsi_iocbs() - Build IOCB command utilizing Command Type 7
 * IOCB types.
 *
 * @sp: SRB command to process
 * @cmd_pkt: Command type 3 IOCB
 * @tot_dsds: Total number of segments to transfer
 */
static inline void
qla24xx_build_scsi_iocbs(srb_t *sp, struct cmd_type_7 *cmd_pkt,
    uint16_t tot_dsds)
{
	uint16_t	avail_dsds;
	uint32_t	*cur_dsd;
	scsi_qla_host_t	*ha;
	struct scsi_cmnd *cmd;

	cmd = sp->cmd;

	/* Update entry type to indicate Command Type 3 IOCB */
	*((uint32_t *)(&cmd_pkt->entry_type)) =
	    __constant_cpu_to_le32(COMMAND_TYPE_7);

	/* No data transfer */
	if (cmd->request_bufflen == 0 || cmd->sc_data_direction == DMA_NONE) {
		cmd_pkt->byte_count = __constant_cpu_to_le32(0);
		return;
	}

	ha = sp->ha;

	/* Set transfer direction */
	if (cmd->sc_data_direction == DMA_TO_DEVICE)
		cmd_pkt->task_mgmt_flags =
		    __constant_cpu_to_le16(TMF_WRITE_DATA);
	else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
		cmd_pkt->task_mgmt_flags =
		    __constant_cpu_to_le16(TMF_READ_DATA);

	/* One DSD is available in the Command Type 3 IOCB */
	avail_dsds = 1;
	cur_dsd = (uint32_t *)&cmd_pkt->dseg_0_address;

	/* Load data segments */
	if (cmd->use_sg != 0) {
		struct	scatterlist *cur_seg;
		struct	scatterlist *end_seg;

		cur_seg = (struct scatterlist *)cmd->request_buffer;
		end_seg = cur_seg + tot_dsds;
		while (cur_seg < end_seg) {
			dma_addr_t	sle_dma;
			cont_a64_entry_t *cont_pkt;

			/* Allocate additional continuation packets? */
			if (avail_dsds == 0) {
				/*
				 * Five DSDs are available in the Continuation
				 * Type 1 IOCB.
				 */
				cont_pkt = qla2x00_prep_cont_type1_iocb(ha);
				cur_dsd = (uint32_t *)cont_pkt->dseg_0_address;
				avail_dsds = 5;
			}

			sle_dma = sg_dma_address(cur_seg);
			*cur_dsd++ = cpu_to_le32(LSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(MSD(sle_dma));
			*cur_dsd++ = cpu_to_le32(sg_dma_len(cur_seg));
			avail_dsds--;

			cur_seg++;
		}
	} else {
		*cur_dsd++ = cpu_to_le32(LSD(sp->dma_handle));
		*cur_dsd++ = cpu_to_le32(MSD(sp->dma_handle));
		*cur_dsd++ = cpu_to_le32(cmd->request_bufflen);
	}
}


/**
 * qla24xx_start_scsi() - Send a SCSI command to the ISP
 * @sp: command to send to the ISP
 *
 * Returns non-zero if a failure occured, else zero.
 */
int
qla24xx_start_scsi(srb_t *sp)
{
	int		ret;
	unsigned long   flags;
	scsi_qla_host_t	*ha;
	struct scsi_cmnd *cmd;
	uint32_t	*clr_ptr;
	uint32_t        index;
	uint32_t	handle;
	struct cmd_type_7 *cmd_pkt;
	struct scatterlist *sg;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	struct device_reg_24xx __iomem *reg;

	/* Setup device pointers. */
	ret = 0;
	ha = sp->ha;
	reg = &ha->iobase->isp24;
	cmd = sp->cmd;
	/* So we know we haven't pci_map'ed anything yet */
	tot_dsds = 0;

	/* Send marker if required */
	if (ha->marker_needed != 0) {
		if (qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) != QLA_SUCCESS) {
			return QLA_FUNCTION_FAILED;
		}
		ha->marker_needed = 0;
	}

	/* Acquire ring specific lock */
	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Check for room in outstanding command list. */
	handle = ha->current_outstanding_cmd;
	for (index = 1; index < MAX_OUTSTANDING_COMMANDS; index++) {
		handle++;
		if (handle == MAX_OUTSTANDING_COMMANDS)
			handle = 1;
		if (ha->outstanding_cmds[handle] == 0)
			break;
	}
	if (index == MAX_OUTSTANDING_COMMANDS)
		goto queuing_error;

	/* Map the sg table so we have an accurate count of sg entries needed */
	if (cmd->use_sg) {
		sg = (struct scatterlist *) cmd->request_buffer;
		tot_dsds = pci_map_sg(ha->pdev, sg, cmd->use_sg,
		    cmd->sc_data_direction);
		if (tot_dsds == 0)
			goto queuing_error;
	} else if (cmd->request_bufflen) {
		dma_addr_t      req_dma;

		req_dma = pci_map_single(ha->pdev, cmd->request_buffer,
		    cmd->request_bufflen, cmd->sc_data_direction);
		if (dma_mapping_error(req_dma))
			goto queuing_error;

		sp->dma_handle = req_dma;
		tot_dsds = 1;
	}

	req_cnt = qla24xx_calc_iocbs(tot_dsds);
	if (ha->req_q_cnt < (req_cnt + 2)) {
		cnt = (uint16_t)RD_REG_DWORD_RELAXED(&reg->req_q_out);
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt = ha->request_q_length -
				(ha->req_ring_index - cnt);
	}
	if (ha->req_q_cnt < (req_cnt + 2))
		goto queuing_error;

	/* Build command packet. */
	ha->current_outstanding_cmd = handle;
	ha->outstanding_cmds[handle] = sp;
	sp->ha = ha;
	sp->cmd->host_scribble = (unsigned char *)(unsigned long)handle;
	ha->req_q_cnt -= req_cnt;

	cmd_pkt = (struct cmd_type_7 *)ha->request_ring_ptr;
	cmd_pkt->handle = handle;

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

	int_to_scsilun(sp->cmd->device->lun, &cmd_pkt->lun);

	/* Load SCSI command packet. */
	memcpy(cmd_pkt->fcp_cdb, cmd->cmnd, cmd->cmd_len);
	host_to_fcp_swap(cmd_pkt->fcp_cdb, sizeof(cmd_pkt->fcp_cdb));

	cmd_pkt->byte_count = cpu_to_le32((uint32_t)cmd->request_bufflen);

	/* Build IOCB segments */
	qla24xx_build_scsi_iocbs(sp, cmd_pkt, tot_dsds);

	/* Set total data segment count. */
	cmd_pkt->entry_count = (uint8_t)req_cnt;
	wmb();

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == ha->request_q_length) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	sp->flags |= SRB_DMA_VALID;
	sp->state = SRB_ACTIVE_STATE;

	/* Set chip new ring index. */
	WRT_REG_DWORD(&reg->req_q_in, ha->req_ring_index);
	RD_REG_DWORD_RELAXED(&reg->req_q_in);		/* PCI Posting. */

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (ha->flags.process_response_queue &&
	    ha->response_ring_ptr->signature != RESPONSE_PROCESSED)
		qla24xx_process_response_queue(ha);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return QLA_SUCCESS;

queuing_error:
	if (cmd->use_sg && tot_dsds) {
		sg = (struct scatterlist *) cmd->request_buffer;
		pci_unmap_sg(ha->pdev, sg, cmd->use_sg,
		    cmd->sc_data_direction);
	} else if (tot_dsds) {
		pci_unmap_single(ha->pdev, sp->dma_handle,
		    cmd->request_bufflen, cmd->sc_data_direction);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return QLA_FUNCTION_FAILED;
}
