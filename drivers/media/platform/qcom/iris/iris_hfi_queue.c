// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_core.h"
#include "iris_hfi_queue.h"

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
