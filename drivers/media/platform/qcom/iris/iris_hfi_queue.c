// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>

#include "iris_core.h"
#include "iris_hfi_queue.h"
#include "iris_vpu_common.h"

static int iris_hfi_queue_write(struct iris_iface_q_info *qinfo, void *packet, u32 packet_size)
{
	struct iris_hfi_queue_header *queue = qinfo->qhdr;
	u32 write_idx = queue->write_idx * sizeof(u32);
	u32 read_idx = queue->read_idx * sizeof(u32);
	u32 empty_space, new_write_idx, residue;
	u32 *write_ptr;

	if (write_idx < read_idx)
		empty_space = read_idx - write_idx;
	else
		empty_space = IFACEQ_QUEUE_SIZE - (write_idx -  read_idx);
	if (empty_space < packet_size)
		return -ENOSPC;

	queue->tx_req =  0;

	new_write_idx = write_idx + packet_size;
	write_ptr = (u32 *)((u8 *)qinfo->kernel_vaddr + write_idx);

	if (write_ptr < (u32 *)qinfo->kernel_vaddr ||
	    write_ptr > (u32 *)(qinfo->kernel_vaddr +
	    IFACEQ_QUEUE_SIZE))
		return -EINVAL;

	if (new_write_idx < IFACEQ_QUEUE_SIZE) {
		memcpy(write_ptr, packet, packet_size);
	} else {
		residue = new_write_idx - IFACEQ_QUEUE_SIZE;
		memcpy(write_ptr, packet, (packet_size - residue));
		memcpy(qinfo->kernel_vaddr,
		       packet + (packet_size - residue), residue);
		new_write_idx = residue;
	}

	/* Make sure packet is written before updating the write index */
	mb();
	queue->write_idx = new_write_idx / sizeof(u32);

	/* Make sure write index is updated before an interrupt is raised */
	mb();

	return 0;
}

static int iris_hfi_queue_read(struct iris_iface_q_info *qinfo, void *packet)
{
	struct iris_hfi_queue_header *queue = qinfo->qhdr;
	u32 write_idx = queue->write_idx * sizeof(u32);
	u32 read_idx = queue->read_idx * sizeof(u32);
	u32 packet_size, receive_request = 0;
	u32 new_read_idx, residue;
	u32 *read_ptr;
	int ret = 0;

	if (queue->queue_type == IFACEQ_MSGQ_ID)
		receive_request = 1;

	if (read_idx == write_idx) {
		queue->rx_req = receive_request;
		/* Ensure qhdr is updated in main memory */
		mb();
		return -ENODATA;
	}

	read_ptr = qinfo->kernel_vaddr + read_idx;
	if (read_ptr < (u32 *)qinfo->kernel_vaddr ||
	    read_ptr > (u32 *)(qinfo->kernel_vaddr +
	    IFACEQ_QUEUE_SIZE - sizeof(*read_ptr)))
		return -ENODATA;

	packet_size = *read_ptr;
	if (!packet_size)
		return -EINVAL;

	new_read_idx = read_idx + packet_size;
	if (packet_size <= IFACEQ_CORE_PKT_SIZE) {
		if (new_read_idx < IFACEQ_QUEUE_SIZE) {
			memcpy(packet, read_ptr, packet_size);
		} else {
			residue = new_read_idx - IFACEQ_QUEUE_SIZE;
			memcpy(packet, read_ptr, (packet_size - residue));
			memcpy((packet + (packet_size - residue)),
			       qinfo->kernel_vaddr, residue);
			new_read_idx = residue;
		}
	} else {
		new_read_idx = write_idx;
		ret = -EBADMSG;
	}

	queue->rx_req = receive_request;

	queue->read_idx = new_read_idx / sizeof(u32);
	/* Ensure qhdr is updated in main memory */
	mb();

	return ret;
}

int iris_hfi_queue_cmd_write_locked(struct iris_core *core, void *pkt, u32 pkt_size)
{
	struct iris_iface_q_info *q_info = &core->command_queue;

	if (core->state == IRIS_CORE_ERROR || core->state == IRIS_CORE_DEINIT)
		return -EINVAL;

	if (!iris_hfi_queue_write(q_info, pkt, pkt_size)) {
		iris_vpu_raise_interrupt(core);
	} else {
		dev_err(core->dev, "queue full\n");
		return -ENODATA;
	}

	return 0;
}

int iris_hfi_queue_cmd_write(struct iris_core *core, void *pkt, u32 pkt_size)
{
	int ret;

	ret = pm_runtime_resume_and_get(core->dev);
	if (ret < 0)
		goto exit;

	mutex_lock(&core->lock);
	ret = iris_hfi_queue_cmd_write_locked(core, pkt, pkt_size);
	if (ret) {
		mutex_unlock(&core->lock);
		goto exit;
	}
	mutex_unlock(&core->lock);

	pm_runtime_put_autosuspend(core->dev);

	return 0;

exit:
	pm_runtime_put_sync(core->dev);

	return ret;
}

int iris_hfi_queue_msg_read(struct iris_core *core, void *pkt)
{
	struct iris_iface_q_info *q_info = &core->message_queue;
	int ret = 0;

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT) {
		ret = -EINVAL;
		goto unlock;
	}

	if (iris_hfi_queue_read(q_info, pkt)) {
		ret = -ENODATA;
		goto unlock;
	}

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

int iris_hfi_queue_dbg_read(struct iris_core *core, void *pkt)
{
	struct iris_iface_q_info *q_info = &core->debug_queue;
	int ret = 0;

	mutex_lock(&core->lock);
	if (core->state != IRIS_CORE_INIT) {
		ret = -EINVAL;
		goto unlock;
	}

	if (iris_hfi_queue_read(q_info, pkt)) {
		ret = -ENODATA;
		goto unlock;
	}

unlock:
	mutex_unlock(&core->lock);

	return ret;
}

static void iris_hfi_queue_set_header(struct iris_core *core, u32 queue_id,
				      struct iris_iface_q_info *iface_q)
{
	iface_q->qhdr->status = 0x1;
	iface_q->qhdr->start_addr = iface_q->device_addr;
	iface_q->qhdr->header_type = IFACEQ_DFLT_QHDR;
	iface_q->qhdr->queue_type = queue_id;
	iface_q->qhdr->q_size = IFACEQ_QUEUE_SIZE / sizeof(u32);
	iface_q->qhdr->pkt_size = 0; /* variable packet size */
	iface_q->qhdr->rx_wm = 0x1;
	iface_q->qhdr->tx_wm = 0x1;
	iface_q->qhdr->rx_req = 0x1;
	iface_q->qhdr->tx_req = 0x0;
	iface_q->qhdr->rx_irq_status = 0x0;
	iface_q->qhdr->tx_irq_status = 0x0;
	iface_q->qhdr->read_idx = 0x0;
	iface_q->qhdr->write_idx = 0x0;

	/*
	 * Set receive request to zero on debug queue as there is no
	 * need of interrupt from video hardware for debug messages
	 */
	if (queue_id == IFACEQ_DBGQ_ID)
		iface_q->qhdr->rx_req = 0;
}

static void
iris_hfi_queue_init(struct iris_core *core, u32 queue_id, struct iris_iface_q_info *iface_q)
{
	struct iris_hfi_queue_table_header *q_tbl_hdr = core->iface_q_table_vaddr;
	u32 offset = sizeof(*q_tbl_hdr) + (queue_id * IFACEQ_QUEUE_SIZE);

	iface_q->device_addr = core->iface_q_table_daddr + offset;
	iface_q->kernel_vaddr =
			(void *)((char *)core->iface_q_table_vaddr + offset);
	iface_q->qhdr = &q_tbl_hdr->q_hdr[queue_id];

	iris_hfi_queue_set_header(core, queue_id, iface_q);
}

static void iris_hfi_queue_deinit(struct iris_iface_q_info *iface_q)
{
	iface_q->qhdr = NULL;
	iface_q->kernel_vaddr = NULL;
	iface_q->device_addr = 0;
}

int iris_hfi_queues_init(struct iris_core *core)
{
	struct iris_hfi_queue_table_header *q_tbl_hdr;
	u32 queue_size;

	/* Iris hardware requires 4K queue alignment */
	queue_size = ALIGN((sizeof(*q_tbl_hdr) + (IFACEQ_QUEUE_SIZE * IFACEQ_NUMQ)), SZ_4K);
	core->iface_q_table_vaddr = dma_alloc_attrs(core->dev, queue_size,
						    &core->iface_q_table_daddr,
						    GFP_KERNEL, DMA_ATTR_WRITE_COMBINE);
	if (!core->iface_q_table_vaddr) {
		dev_err(core->dev, "queues alloc and map failed\n");
		return -ENOMEM;
	}

	core->sfr_vaddr = dma_alloc_attrs(core->dev, SFR_SIZE,
					  &core->sfr_daddr,
					  GFP_KERNEL, DMA_ATTR_WRITE_COMBINE);
	if (!core->sfr_vaddr) {
		dev_err(core->dev, "sfr alloc and map failed\n");
		dma_free_attrs(core->dev, sizeof(*q_tbl_hdr), core->iface_q_table_vaddr,
			       core->iface_q_table_daddr, DMA_ATTR_WRITE_COMBINE);
		return -ENOMEM;
	}

	iris_hfi_queue_init(core, IFACEQ_CMDQ_ID, &core->command_queue);
	iris_hfi_queue_init(core, IFACEQ_MSGQ_ID, &core->message_queue);
	iris_hfi_queue_init(core, IFACEQ_DBGQ_ID, &core->debug_queue);

	q_tbl_hdr = (struct iris_hfi_queue_table_header *)core->iface_q_table_vaddr;
	q_tbl_hdr->version = 0;
	q_tbl_hdr->device_addr = (void *)core;
	strscpy(q_tbl_hdr->name, "iris-hfi-queues", sizeof(q_tbl_hdr->name));
	q_tbl_hdr->size = sizeof(*q_tbl_hdr);
	q_tbl_hdr->qhdr0_offset = sizeof(*q_tbl_hdr) -
		(IFACEQ_NUMQ * sizeof(struct iris_hfi_queue_header));
	q_tbl_hdr->qhdr_size = sizeof(q_tbl_hdr->q_hdr[0]);
	q_tbl_hdr->num_q = IFACEQ_NUMQ;
	q_tbl_hdr->num_active_q = IFACEQ_NUMQ;

	 /* Write sfr size in first word to be used by firmware */
	*((u32 *)core->sfr_vaddr) = SFR_SIZE;

	return 0;
}

void iris_hfi_queues_deinit(struct iris_core *core)
{
	u32 queue_size;

	if (!core->iface_q_table_vaddr)
		return;

	iris_hfi_queue_deinit(&core->debug_queue);
	iris_hfi_queue_deinit(&core->message_queue);
	iris_hfi_queue_deinit(&core->command_queue);

	dma_free_attrs(core->dev, SFR_SIZE, core->sfr_vaddr,
		       core->sfr_daddr, DMA_ATTR_WRITE_COMBINE);

	core->sfr_vaddr = NULL;
	core->sfr_daddr = 0;

	queue_size = ALIGN(sizeof(struct iris_hfi_queue_table_header) +
		(IFACEQ_QUEUE_SIZE * IFACEQ_NUMQ), SZ_4K);

	dma_free_attrs(core->dev, queue_size, core->iface_q_table_vaddr,
		       core->iface_q_table_daddr, DMA_ATTR_WRITE_COMBINE);

	core->iface_q_table_vaddr = NULL;
	core->iface_q_table_daddr = 0;
}
