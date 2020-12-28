// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aic94xx SAS/SATA Tasks
 *
 * Copyright (C) 2005 Adaptec, Inc.  All rights reserved.
 * Copyright (C) 2005 Luben Tuikov <luben_tuikov@adaptec.com>
 */

#include <linux/spinlock.h>
#include "aic94xx.h"
#include "aic94xx_sas.h"
#include "aic94xx_hwi.h"

static void asd_unbuild_ata_ascb(struct asd_ascb *a);
static void asd_unbuild_smp_ascb(struct asd_ascb *a);
static void asd_unbuild_ssp_ascb(struct asd_ascb *a);

static void asd_can_dequeue(struct asd_ha_struct *asd_ha, int num)
{
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->seq.pend_q_lock, flags);
	asd_ha->seq.can_queue += num;
	spin_unlock_irqrestore(&asd_ha->seq.pend_q_lock, flags);
}

/* DMA_... to our direction translation.
 */
static const u8 data_dir_flags[] = {
	[DMA_BIDIRECTIONAL]	= DATA_DIR_BYRECIPIENT,	/* UNSPECIFIED */
	[DMA_TO_DEVICE]		= DATA_DIR_OUT,		/* OUTBOUND */
	[DMA_FROM_DEVICE]	= DATA_DIR_IN,		/* INBOUND */
	[DMA_NONE]		= DATA_DIR_NONE,	/* NO TRANSFER */
};

static int asd_map_scatterlist(struct sas_task *task,
			       struct sg_el *sg_arr,
			       gfp_t gfp_flags)
{
	struct asd_ascb *ascb = task->lldd_task;
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct scatterlist *sc;
	int num_sg, res;

	if (task->data_dir == DMA_NONE)
		return 0;

	if (task->num_scatter == 0) {
		void *p = task->scatter;
		dma_addr_t dma = dma_map_single(&asd_ha->pcidev->dev, p,
						task->total_xfer_len,
						task->data_dir);
		sg_arr[0].bus_addr = cpu_to_le64((u64)dma);
		sg_arr[0].size = cpu_to_le32(task->total_xfer_len);
		sg_arr[0].flags |= ASD_SG_EL_LIST_EOL;
		return 0;
	}

	/* STP tasks come from libata which has already mapped
	 * the SG list */
	if (sas_protocol_ata(task->task_proto))
		num_sg = task->num_scatter;
	else
		num_sg = dma_map_sg(&asd_ha->pcidev->dev, task->scatter,
				    task->num_scatter, task->data_dir);
	if (num_sg == 0)
		return -ENOMEM;

	if (num_sg > 3) {
		int i;

		ascb->sg_arr = asd_alloc_coherent(asd_ha,
						  num_sg*sizeof(struct sg_el),
						  gfp_flags);
		if (!ascb->sg_arr) {
			res = -ENOMEM;
			goto err_unmap;
		}
		for_each_sg(task->scatter, sc, num_sg, i) {
			struct sg_el *sg =
				&((struct sg_el *)ascb->sg_arr->vaddr)[i];
			sg->bus_addr = cpu_to_le64((u64)sg_dma_address(sc));
			sg->size = cpu_to_le32((u32)sg_dma_len(sc));
			if (i == num_sg-1)
				sg->flags |= ASD_SG_EL_LIST_EOL;
		}

		for_each_sg(task->scatter, sc, 2, i) {
			sg_arr[i].bus_addr =
				cpu_to_le64((u64)sg_dma_address(sc));
			sg_arr[i].size = cpu_to_le32((u32)sg_dma_len(sc));
		}
		sg_arr[1].next_sg_offs = 2 * sizeof(*sg_arr);
		sg_arr[1].flags |= ASD_SG_EL_LIST_EOS;

		memset(&sg_arr[2], 0, sizeof(*sg_arr));
		sg_arr[2].bus_addr=cpu_to_le64((u64)ascb->sg_arr->dma_handle);
	} else {
		int i;
		for_each_sg(task->scatter, sc, num_sg, i) {
			sg_arr[i].bus_addr =
				cpu_to_le64((u64)sg_dma_address(sc));
			sg_arr[i].size = cpu_to_le32((u32)sg_dma_len(sc));
		}
		sg_arr[i-1].flags |= ASD_SG_EL_LIST_EOL;
	}

	return 0;
err_unmap:
	if (sas_protocol_ata(task->task_proto))
		dma_unmap_sg(&asd_ha->pcidev->dev, task->scatter,
			     task->num_scatter, task->data_dir);
	return res;
}

static void asd_unmap_scatterlist(struct asd_ascb *ascb)
{
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct sas_task *task = ascb->uldd_task;

	if (task->data_dir == DMA_NONE)
		return;

	if (task->num_scatter == 0) {
		dma_addr_t dma = (dma_addr_t)
		       le64_to_cpu(ascb->scb->ssp_task.sg_element[0].bus_addr);
		dma_unmap_single(&ascb->ha->pcidev->dev, dma,
				 task->total_xfer_len, task->data_dir);
		return;
	}

	asd_free_coherent(asd_ha, ascb->sg_arr);
	if (task->task_proto != SAS_PROTOCOL_STP)
		dma_unmap_sg(&asd_ha->pcidev->dev, task->scatter,
			     task->num_scatter, task->data_dir);
}

/* ---------- Task complete tasklet ---------- */

static void asd_get_response_tasklet(struct asd_ascb *ascb,
				     struct done_list_struct *dl)
{
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct sas_task *task = ascb->uldd_task;
	struct task_status_struct *ts = &task->task_status;
	unsigned long flags;
	struct tc_resp_sb_struct {
		__le16 index_escb;
		u8     len_lsb;
		u8     flags;
	} __attribute__ ((packed)) *resp_sb = (void *) dl->status_block;

/* 	int  size   = ((resp_sb->flags & 7) << 8) | resp_sb->len_lsb; */
	int  edb_id = ((resp_sb->flags & 0x70) >> 4)-1;
	struct asd_ascb *escb;
	struct asd_dma_tok *edb;
	void *r;

	spin_lock_irqsave(&asd_ha->seq.tc_index_lock, flags);
	escb = asd_tc_index_find(&asd_ha->seq,
				 (int)le16_to_cpu(resp_sb->index_escb));
	spin_unlock_irqrestore(&asd_ha->seq.tc_index_lock, flags);

	if (!escb) {
		ASD_DPRINTK("Uh-oh! No escb for this dl?!\n");
		return;
	}

	ts->buf_valid_size = 0;
	edb = asd_ha->seq.edb_arr[edb_id + escb->edb_index];
	r = edb->vaddr;
	if (task->task_proto == SAS_PROTOCOL_SSP) {
		struct ssp_response_iu *iu =
			r + 16 + sizeof(struct ssp_frame_hdr);

		ts->residual = le32_to_cpu(*(__le32 *)r);

		sas_ssp_task_response(&asd_ha->pcidev->dev, task, iu);
	}  else {
		struct ata_task_resp *resp = (void *) &ts->buf[0];

		ts->residual = le32_to_cpu(*(__le32 *)r);

		if (SAS_STATUS_BUF_SIZE >= sizeof(*resp)) {
			resp->frame_len = le16_to_cpu(*(__le16 *)(r+6));
			memcpy(&resp->ending_fis[0], r+16, ATA_RESP_FIS_SIZE);
			ts->buf_valid_size = sizeof(*resp);
		}
	}

	asd_invalidate_edb(escb, edb_id);
}

static void asd_task_tasklet_complete(struct asd_ascb *ascb,
				      struct done_list_struct *dl)
{
	struct sas_task *task = ascb->uldd_task;
	struct task_status_struct *ts = &task->task_status;
	unsigned long flags;
	u8 opcode = dl->opcode;

	asd_can_dequeue(ascb->ha, 1);

Again:
	switch (opcode) {
	case TC_NO_ERROR:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAM_STAT_GOOD;
		break;
	case TC_UNDERRUN:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_UNDERRUN;
		ts->residual = le32_to_cpu(*(__le32 *)dl->status_block);
		break;
	case TC_OVERRUN:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_DATA_OVERRUN;
		ts->residual = 0;
		break;
	case TC_SSP_RESP:
	case TC_ATA_RESP:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_PROTO_RESPONSE;
		asd_get_response_tasklet(ascb, dl);
		break;
	case TF_OPEN_REJECT:
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_OPEN_REJECT;
		if (dl->status_block[1] & 2)
			ts->open_rej_reason = 1 + dl->status_block[2];
		else if (dl->status_block[1] & 1)
			ts->open_rej_reason = (dl->status_block[2] >> 4)+10;
		else
			ts->open_rej_reason = SAS_OREJ_UNKNOWN;
		break;
	case TF_OPEN_TO:
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_OPEN_TO;
		break;
	case TF_PHY_DOWN:
	case TU_PHY_DOWN:
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PHY_DOWN;
		break;
	case TI_PHY_DOWN:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_PHY_DOWN;
		break;
	case TI_BREAK:
	case TI_PROTO_ERR:
	case TI_NAK:
	case TI_ACK_NAK_TO:
	case TF_SMP_XMIT_RCV_ERR:
	case TC_ATA_R_ERR_RECV:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_INTERRUPTED;
		break;
	case TF_BREAK:
	case TU_BREAK:
	case TU_ACK_NAK_TO:
	case TF_SMPRSP_TO:
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_DEV_NO_RESPONSE;
		break;
	case TF_NAK_RECV:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_NAK_R_ERR;
		break;
	case TA_I_T_NEXUS_LOSS:
		opcode = dl->status_block[0];
		goto Again;
	case TF_INV_CONN_HANDLE:
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_DEVICE_UNKNOWN;
		break;
	case TF_REQUESTED_N_PENDING:
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_PENDING;
		break;
	case TC_TASK_CLEARED:
	case TA_ON_REQ:
		ts->resp = SAS_TASK_COMPLETE;
		ts->stat = SAS_ABORTED_TASK;
		break;

	case TF_NO_SMP_CONN:
	case TF_TMF_NO_CTX:
	case TF_TMF_NO_TAG:
	case TF_TMF_TAG_FREE:
	case TF_TMF_TASK_DONE:
	case TF_TMF_NO_CONN_HANDLE:
	case TF_IRTT_TO:
	case TF_IU_SHORT:
	case TF_DATA_OFFS_ERR:
		ts->resp = SAS_TASK_UNDELIVERED;
		ts->stat = SAS_DEV_NO_RESPONSE;
		break;

	case TC_LINK_ADM_RESP:
	case TC_CONTROL_PHY:
	case TC_RESUME:
	case TC_PARTIAL_SG_LIST:
	default:
		ASD_DPRINTK("%s: dl opcode: 0x%x?\n", __func__, opcode);
		break;
	}

	switch (task->task_proto) {
	case SAS_PROTOCOL_SATA:
	case SAS_PROTOCOL_STP:
		asd_unbuild_ata_ascb(ascb);
		break;
	case SAS_PROTOCOL_SMP:
		asd_unbuild_smp_ascb(ascb);
		break;
	case SAS_PROTOCOL_SSP:
		asd_unbuild_ssp_ascb(ascb);
		break;
	default:
		break;
	}

	spin_lock_irqsave(&task->task_state_lock, flags);
	task->task_state_flags &= ~SAS_TASK_STATE_PENDING;
	task->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
	task->task_state_flags |= SAS_TASK_STATE_DONE;
	if (unlikely((task->task_state_flags & SAS_TASK_STATE_ABORTED))) {
		struct completion *completion = ascb->completion;
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		ASD_DPRINTK("task 0x%p done with opcode 0x%x resp 0x%x "
			    "stat 0x%x but aborted by upper layer!\n",
			    task, opcode, ts->resp, ts->stat);
		if (completion)
			complete(completion);
	} else {
		spin_unlock_irqrestore(&task->task_state_lock, flags);
		task->lldd_task = NULL;
		asd_ascb_free(ascb);
		mb();
		task->task_done(task);
	}
}

/* ---------- ATA ---------- */

static int asd_build_ata_ascb(struct asd_ascb *ascb, struct sas_task *task,
			      gfp_t gfp_flags)
{
	struct domain_device *dev = task->dev;
	struct scb *scb;
	u8     flags;
	int    res = 0;

	scb = ascb->scb;

	if (unlikely(task->ata_task.device_control_reg_update))
		scb->header.opcode = CONTROL_ATA_DEV;
	else if (dev->sata_dev.class == ATA_DEV_ATAPI)
		scb->header.opcode = INITIATE_ATAPI_TASK;
	else
		scb->header.opcode = INITIATE_ATA_TASK;

	scb->ata_task.proto_conn_rate = (1 << 5); /* STP */
	if (dev->port->oob_mode == SAS_OOB_MODE)
		scb->ata_task.proto_conn_rate |= dev->linkrate;

	scb->ata_task.total_xfer_len = cpu_to_le32(task->total_xfer_len);
	scb->ata_task.fis = task->ata_task.fis;
	if (likely(!task->ata_task.device_control_reg_update))
		scb->ata_task.fis.flags |= 0x80; /* C=1: update ATA cmd reg */
	scb->ata_task.fis.flags &= 0xF0; /* PM_PORT field shall be 0 */
	if (dev->sata_dev.class == ATA_DEV_ATAPI)
		memcpy(scb->ata_task.atapi_packet, task->ata_task.atapi_packet,
		       16);
	scb->ata_task.sister_scb = cpu_to_le16(0xFFFF);
	scb->ata_task.conn_handle = cpu_to_le16(
		(u16)(unsigned long)dev->lldd_dev);

	if (likely(!task->ata_task.device_control_reg_update)) {
		flags = 0;
		if (task->ata_task.dma_xfer)
			flags |= DATA_XFER_MODE_DMA;
		if (task->ata_task.use_ncq &&
		    dev->sata_dev.class != ATA_DEV_ATAPI)
			flags |= ATA_Q_TYPE_NCQ;
		flags |= data_dir_flags[task->data_dir];
		scb->ata_task.ata_flags = flags;

		scb->ata_task.retry_count = task->ata_task.retry_count;

		flags = 0;
		if (task->ata_task.set_affil_pol)
			flags |= SET_AFFIL_POLICY;
		if (task->ata_task.stp_affil_pol)
			flags |= STP_AFFIL_POLICY;
		scb->ata_task.flags = flags;
	}
	ascb->tasklet_complete = asd_task_tasklet_complete;

	if (likely(!task->ata_task.device_control_reg_update))
		res = asd_map_scatterlist(task, scb->ata_task.sg_element,
					  gfp_flags);

	return res;
}

static void asd_unbuild_ata_ascb(struct asd_ascb *a)
{
	asd_unmap_scatterlist(a);
}

/* ---------- SMP ---------- */

static int asd_build_smp_ascb(struct asd_ascb *ascb, struct sas_task *task,
			      gfp_t gfp_flags)
{
	struct asd_ha_struct *asd_ha = ascb->ha;
	struct domain_device *dev = task->dev;
	struct scb *scb;

	dma_map_sg(&asd_ha->pcidev->dev, &task->smp_task.smp_req, 1,
		   DMA_TO_DEVICE);
	dma_map_sg(&asd_ha->pcidev->dev, &task->smp_task.smp_resp, 1,
		   DMA_FROM_DEVICE);

	scb = ascb->scb;

	scb->header.opcode = INITIATE_SMP_TASK;

	scb->smp_task.proto_conn_rate = dev->linkrate;

	scb->smp_task.smp_req.bus_addr =
		cpu_to_le64((u64)sg_dma_address(&task->smp_task.smp_req));
	scb->smp_task.smp_req.size =
		cpu_to_le32((u32)sg_dma_len(&task->smp_task.smp_req)-4);

	scb->smp_task.smp_resp.bus_addr =
		cpu_to_le64((u64)sg_dma_address(&task->smp_task.smp_resp));
	scb->smp_task.smp_resp.size =
		cpu_to_le32((u32)sg_dma_len(&task->smp_task.smp_resp)-4);

	scb->smp_task.sister_scb = cpu_to_le16(0xFFFF);
	scb->smp_task.conn_handle = cpu_to_le16((u16)
						(unsigned long)dev->lldd_dev);

	ascb->tasklet_complete = asd_task_tasklet_complete;

	return 0;
}

static void asd_unbuild_smp_ascb(struct asd_ascb *a)
{
	struct sas_task *task = a->uldd_task;

	BUG_ON(!task);
	dma_unmap_sg(&a->ha->pcidev->dev, &task->smp_task.smp_req, 1,
		     DMA_TO_DEVICE);
	dma_unmap_sg(&a->ha->pcidev->dev, &task->smp_task.smp_resp, 1,
		     DMA_FROM_DEVICE);
}

/* ---------- SSP ---------- */

static int asd_build_ssp_ascb(struct asd_ascb *ascb, struct sas_task *task,
			      gfp_t gfp_flags)
{
	struct domain_device *dev = task->dev;
	struct scb *scb;
	int    res = 0;

	scb = ascb->scb;

	scb->header.opcode = INITIATE_SSP_TASK;

	scb->ssp_task.proto_conn_rate  = (1 << 4); /* SSP */
	scb->ssp_task.proto_conn_rate |= dev->linkrate;
	scb->ssp_task.total_xfer_len = cpu_to_le32(task->total_xfer_len);
	scb->ssp_task.ssp_frame.frame_type = SSP_DATA;
	memcpy(scb->ssp_task.ssp_frame.hashed_dest_addr, dev->hashed_sas_addr,
	       HASHED_SAS_ADDR_SIZE);
	memcpy(scb->ssp_task.ssp_frame.hashed_src_addr,
	       dev->port->ha->hashed_sas_addr, HASHED_SAS_ADDR_SIZE);
	scb->ssp_task.ssp_frame.tptt = cpu_to_be16(0xFFFF);

	memcpy(scb->ssp_task.ssp_cmd.lun, task->ssp_task.LUN, 8);
	if (task->ssp_task.enable_first_burst)
		scb->ssp_task.ssp_cmd.efb_prio_attr |= EFB_MASK;
	scb->ssp_task.ssp_cmd.efb_prio_attr |= (task->ssp_task.task_prio << 3);
	scb->ssp_task.ssp_cmd.efb_prio_attr |= (task->ssp_task.task_attr & 7);
	memcpy(scb->ssp_task.ssp_cmd.cdb, task->ssp_task.cmd->cmnd,
	       task->ssp_task.cmd->cmd_len);

	scb->ssp_task.sister_scb = cpu_to_le16(0xFFFF);
	scb->ssp_task.conn_handle = cpu_to_le16(
		(u16)(unsigned long)dev->lldd_dev);
	scb->ssp_task.data_dir = data_dir_flags[task->data_dir];
	scb->ssp_task.retry_count = scb->ssp_task.retry_count;

	ascb->tasklet_complete = asd_task_tasklet_complete;

	res = asd_map_scatterlist(task, scb->ssp_task.sg_element, gfp_flags);

	return res;
}

static void asd_unbuild_ssp_ascb(struct asd_ascb *a)
{
	asd_unmap_scatterlist(a);
}

/* ---------- Execute Task ---------- */

static int asd_can_queue(struct asd_ha_struct *asd_ha, int num)
{
	int res = 0;
	unsigned long flags;

	spin_lock_irqsave(&asd_ha->seq.pend_q_lock, flags);
	if ((asd_ha->seq.can_queue - num) < 0)
		res = -SAS_QUEUE_FULL;
	else
		asd_ha->seq.can_queue -= num;
	spin_unlock_irqrestore(&asd_ha->seq.pend_q_lock, flags);

	return res;
}

int asd_execute_task(struct sas_task *task, gfp_t gfp_flags)
{
	int res = 0;
	LIST_HEAD(alist);
	struct sas_task *t = task;
	struct asd_ascb *ascb = NULL, *a;
	struct asd_ha_struct *asd_ha = task->dev->port->ha->lldd_ha;
	unsigned long flags;

	res = asd_can_queue(asd_ha, 1);
	if (res)
		return res;

	res = 1;
	ascb = asd_ascb_alloc_list(asd_ha, &res, gfp_flags);
	if (res) {
		res = -ENOMEM;
		goto out_err;
	}

	__list_add(&alist, ascb->list.prev, &ascb->list);
	list_for_each_entry(a, &alist, list) {
		a->uldd_task = t;
		t->lldd_task = a;
		break;
	}
	list_for_each_entry(a, &alist, list) {
		t = a->uldd_task;
		a->uldd_timer = 1;
		if (t->task_proto & SAS_PROTOCOL_STP)
			t->task_proto = SAS_PROTOCOL_STP;
		switch (t->task_proto) {
		case SAS_PROTOCOL_SATA:
		case SAS_PROTOCOL_STP:
			res = asd_build_ata_ascb(a, t, gfp_flags);
			break;
		case SAS_PROTOCOL_SMP:
			res = asd_build_smp_ascb(a, t, gfp_flags);
			break;
		case SAS_PROTOCOL_SSP:
			res = asd_build_ssp_ascb(a, t, gfp_flags);
			break;
		default:
			asd_printk("unknown sas_task proto: 0x%x\n",
				   t->task_proto);
			res = -ENOMEM;
			break;
		}
		if (res)
			goto out_err_unmap;

		spin_lock_irqsave(&t->task_state_lock, flags);
		t->task_state_flags |= SAS_TASK_AT_INITIATOR;
		spin_unlock_irqrestore(&t->task_state_lock, flags);
	}
	list_del_init(&alist);

	res = asd_post_ascb_list(asd_ha, ascb, 1);
	if (unlikely(res)) {
		a = NULL;
		__list_add(&alist, ascb->list.prev, &ascb->list);
		goto out_err_unmap;
	}

	return 0;
out_err_unmap:
	{
		struct asd_ascb *b = a;
		list_for_each_entry(a, &alist, list) {
			if (a == b)
				break;
			t = a->uldd_task;
			spin_lock_irqsave(&t->task_state_lock, flags);
			t->task_state_flags &= ~SAS_TASK_AT_INITIATOR;
			spin_unlock_irqrestore(&t->task_state_lock, flags);
			switch (t->task_proto) {
			case SAS_PROTOCOL_SATA:
			case SAS_PROTOCOL_STP:
				asd_unbuild_ata_ascb(a);
				break;
			case SAS_PROTOCOL_SMP:
				asd_unbuild_smp_ascb(a);
				break;
			case SAS_PROTOCOL_SSP:
				asd_unbuild_ssp_ascb(a);
				break;
			default:
				break;
			}
			t->lldd_task = NULL;
		}
	}
	list_del_init(&alist);
out_err:
	if (ascb)
		asd_ascb_free_list(ascb);
	asd_can_dequeue(asd_ha, 1);
	return res;
}
