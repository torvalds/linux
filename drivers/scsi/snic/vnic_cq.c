// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2014 Cisco Systems, Inc.  All rights reserved.

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include "vnic_dev.h"
#include "vnic_cq.h"

void svnic_cq_free(struct vnic_cq *cq)
{
	svnic_dev_free_desc_ring(cq->vdev, &cq->ring);

	cq->ctrl = NULL;
}

int svnic_cq_alloc(struct vnic_dev *vdev, struct vnic_cq *cq,
	unsigned int index, unsigned int desc_count, unsigned int desc_size)
{
	cq->index = index;
	cq->vdev = vdev;

	cq->ctrl = svnic_dev_get_res(vdev, RES_TYPE_CQ, index);
	if (!cq->ctrl) {
		pr_err("Failed to hook CQ[%d] resource\n", index);

		return -EINVAL;
	}

	return svnic_dev_alloc_desc_ring(vdev, &cq->ring, desc_count, desc_size);
}

void svnic_cq_init(struct vnic_cq *cq, unsigned int flow_control_enable,
	unsigned int color_enable, unsigned int cq_head, unsigned int cq_tail,
	unsigned int cq_tail_color, unsigned int interrupt_enable,
	unsigned int cq_entry_enable, unsigned int cq_message_enable,
	unsigned int interrupt_offset, u64 cq_message_addr)
{
	u64 paddr;

	paddr = (u64)cq->ring.base_addr | VNIC_PADDR_TARGET;
	writeq(paddr, &cq->ctrl->ring_base);
	iowrite32(cq->ring.desc_count, &cq->ctrl->ring_size);
	iowrite32(flow_control_enable, &cq->ctrl->flow_control_enable);
	iowrite32(color_enable, &cq->ctrl->color_enable);
	iowrite32(cq_head, &cq->ctrl->cq_head);
	iowrite32(cq_tail, &cq->ctrl->cq_tail);
	iowrite32(cq_tail_color, &cq->ctrl->cq_tail_color);
	iowrite32(interrupt_enable, &cq->ctrl->interrupt_enable);
	iowrite32(cq_entry_enable, &cq->ctrl->cq_entry_enable);
	iowrite32(cq_message_enable, &cq->ctrl->cq_message_enable);
	iowrite32(interrupt_offset, &cq->ctrl->interrupt_offset);
	writeq(cq_message_addr, &cq->ctrl->cq_message_addr);
}

void svnic_cq_clean(struct vnic_cq *cq)
{
	cq->to_clean = 0;
	cq->last_color = 0;

	iowrite32(0, &cq->ctrl->cq_head);
	iowrite32(0, &cq->ctrl->cq_tail);
	iowrite32(1, &cq->ctrl->cq_tail_color);

	svnic_dev_clear_desc_ring(&cq->ring);
}
