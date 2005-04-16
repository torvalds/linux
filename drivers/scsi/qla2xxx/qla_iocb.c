/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

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
		dma_addr_t	req_dma;
		struct page	*page;
		unsigned long	offset;

		page = virt_to_page(cmd->request_buffer);
		offset = ((unsigned long)cmd->request_buffer & ~PAGE_MASK);
		req_dma = pci_map_page(ha->pdev, page, offset,
		    cmd->request_bufflen, cmd->sc_data_direction);

		sp->dma_handle = req_dma;

		*cur_dsd++ = cpu_to_le32(req_dma);
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
		dma_addr_t	req_dma;
		struct page	*page;
		unsigned long	offset;

		page = virt_to_page(cmd->request_buffer);
		offset = ((unsigned long)cmd->request_buffer & ~PAGE_MASK);
		req_dma = pci_map_page(ha->pdev, page, offset,
		    cmd->request_bufflen, cmd->sc_data_direction);

		sp->dma_handle = req_dma;

		*cur_dsd++ = cpu_to_le32(LSD(req_dma));
		*cur_dsd++ = cpu_to_le32(MSD(req_dma));
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
	fc_lun_t	*fclun;
	struct scsi_cmnd *cmd;
	uint32_t	*clr_ptr;
	uint32_t        index;
	uint32_t	handle;
	cmd_entry_t	*cmd_pkt;
	uint32_t        timeout;
	struct scatterlist *sg;
	uint16_t	cnt;
	uint16_t	req_cnt;
	uint16_t	tot_dsds;
	device_reg_t __iomem *reg;
	char		tag[2];

	/* Setup device pointers. */
	ret = 0;
	fclun = sp->lun_queue->fclun;
	ha = fclun->fcport->ha;
	reg = ha->iobase;
	cmd = sp->cmd;

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

	/* Calculate the number of request entries needed. */
	req_cnt = (ha->calc_request_entries)(cmd->request->nr_hw_segments);
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

	/* Finally, we have enough space, now perform mappings. */
	tot_dsds = 0;
	if (cmd->use_sg) {
		sg = (struct scatterlist *) cmd->request_buffer;
		tot_dsds = pci_map_sg(ha->pdev, sg, cmd->use_sg,
		    cmd->sc_data_direction);
		if (tot_dsds == 0)
			goto queuing_error;
	} else if (cmd->request_bufflen) {
	    tot_dsds++;
	}
	req_cnt = (ha->calc_request_entries)(tot_dsds);

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

	/* Set target ID */
	SET_TARGET_ID(ha, cmd_pkt->target, fclun->fcport->loop_id);

	/* Set LUN number*/
	cmd_pkt->lun = cpu_to_le16(fclun->lun);

	/* Update tagged queuing modifier */
	cmd_pkt->control_flags = __constant_cpu_to_le16(CF_SIMPLE_TAG);
	if (scsi_populate_tag_msg(cmd, tag)) {
		switch (tag[0]) {
		case MSG_HEAD_TAG:
			cmd_pkt->control_flags =
			    __constant_cpu_to_le16(CF_HEAD_TAG);
			break;
		case MSG_ORDERED_TAG:
			cmd_pkt->control_flags =
			    __constant_cpu_to_le16(CF_ORDERED_TAG);
			break;
		}
	}

	/*
	 * Allocate at least 5 (+ QLA_CMD_TIMER_DELTA) seconds for RISC timeout.
	 */
	timeout = (uint32_t)(cmd->timeout_per_command / HZ);
	if (timeout > 65535)
		cmd_pkt->timeout = __constant_cpu_to_le16(0);
	else if (timeout > 25)
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout -
		    (5 + QLA_CMD_TIMER_DELTA));
	else
		cmd_pkt->timeout = cpu_to_le16((uint16_t)timeout);

	/* Load SCSI command packet. */
	memcpy(cmd_pkt->scsi_cdb, cmd->cmnd, cmd->cmd_len);
	cmd_pkt->byte_count = cpu_to_le32((uint32_t)cmd->request_bufflen);

	/* Build IOCB segments */
	(ha->build_scsi_iocbs)(sp, cmd_pkt, tot_dsds);

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

	ha->actthreads++;
	ha->total_ios++;
	sp->lun_queue->out_cnt++;
	sp->flags |= SRB_DMA_VALID;
	sp->state = SRB_ACTIVE_STATE;
	sp->u_start = jiffies;

	/* Set chip new ring index. */
	WRT_REG_WORD(ISP_REQ_Q_IN(ha, reg), ha->req_ring_index);
	RD_REG_WORD_RELAXED(ISP_REQ_Q_IN(ha, reg));	/* PCI Posting. */

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	return (QLA_SUCCESS);

queuing_error:
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
	mrk_entry_t	*pkt;

	pkt = (mrk_entry_t *)qla2x00_req_pkt(ha);
	if (pkt == NULL) {
		DEBUG2_3(printk("%s(): **** FAILED ****\n", __func__));

		return (QLA_FUNCTION_FAILED);
	}

	pkt->entry_type = MARKER_TYPE;
	pkt->modifier = type;

	if (type != MK_SYNC_ALL) {
		pkt->lun = cpu_to_le16(lun);
		SET_TARGET_ID(ha, pkt->target, loop_id);
	}
	wmb();

	/* Issue command to ISP */
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
			cnt = qla2x00_debounce_register(ISP_REQ_Q_OUT(ha, reg));
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
	WRT_REG_WORD(ISP_REQ_Q_IN(ha, reg), ha->req_ring_index);
	RD_REG_WORD_RELAXED(ISP_REQ_Q_IN(ha, reg));	/* PCI Posting. */
}
