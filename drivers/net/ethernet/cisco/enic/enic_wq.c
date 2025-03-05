// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Cisco Systems, Inc.  All rights reserved.

#include <net/netdev_queues.h>
#include "enic_res.h"
#include "enic.h"
#include "enic_wq.h"

static void cq_desc_dec(const struct cq_desc *desc_arg, u8 *type, u8 *color,
			u16 *q_number, u16 *completed_index)
{
	const struct cq_desc *desc = desc_arg;
	const u8 type_color = desc->type_color;

	*color = (type_color >> CQ_DESC_COLOR_SHIFT) & CQ_DESC_COLOR_MASK;

	/*
	 * Make sure color bit is read from desc *before* other fields
	 * are read from desc.  Hardware guarantees color bit is last
	 * bit (byte) written.  Adding the rmb() prevents the compiler
	 * and/or CPU from reordering the reads which would potentially
	 * result in reading stale values.
	 */
	rmb();

	*type = type_color & CQ_DESC_TYPE_MASK;
	*q_number = le16_to_cpu(desc->q_number) & CQ_DESC_Q_NUM_MASK;
	*completed_index = le16_to_cpu(desc->completed_index) &
		CQ_DESC_COMP_NDX_MASK;
}

unsigned int vnic_cq_service(struct vnic_cq *cq, unsigned int work_to_do,
			     int (*q_service)(struct vnic_dev *vdev,
					      struct cq_desc *cq_desc, u8 type,
					      u16 q_number, u16 completed_index,
					      void *opaque), void *opaque)
{
	struct cq_desc *cq_desc;
	unsigned int work_done = 0;
	u16 q_number, completed_index;
	u8 type, color;

	cq_desc = (struct cq_desc *)((u8 *)cq->ring.descs +
		   cq->ring.desc_size * cq->to_clean);
	cq_desc_dec(cq_desc, &type, &color,
		    &q_number, &completed_index);

	while (color != cq->last_color) {
		if ((*q_service)(cq->vdev, cq_desc, type, q_number,
				 completed_index, opaque))
			break;

		cq->to_clean++;
		if (cq->to_clean == cq->ring.desc_count) {
			cq->to_clean = 0;
			cq->last_color = cq->last_color ? 0 : 1;
		}

		cq_desc = (struct cq_desc *)((u8 *)cq->ring.descs +
			cq->ring.desc_size * cq->to_clean);
		cq_desc_dec(cq_desc, &type, &color,
			    &q_number, &completed_index);

		work_done++;
		if (work_done >= work_to_do)
			break;
	}

	return work_done;
}

void enic_free_wq_buf(struct vnic_wq *wq, struct vnic_wq_buf *buf)
{
	struct enic *enic = vnic_dev_priv(wq->vdev);

	if (buf->sop)
		dma_unmap_single(&enic->pdev->dev, buf->dma_addr, buf->len,
				 DMA_TO_DEVICE);
	else
		dma_unmap_page(&enic->pdev->dev, buf->dma_addr, buf->len,
			       DMA_TO_DEVICE);

	if (buf->os_buf)
		dev_kfree_skb_any(buf->os_buf);
}

static void enic_wq_free_buf(struct vnic_wq *wq, struct cq_desc *cq_desc,
			     struct vnic_wq_buf *buf, void *opaque)
{
	struct enic *enic = vnic_dev_priv(wq->vdev);

	enic->wq[wq->index].stats.cq_work++;
	enic->wq[wq->index].stats.cq_bytes += buf->len;
	enic_free_wq_buf(wq, buf);
}

int enic_wq_service(struct vnic_dev *vdev, struct cq_desc *cq_desc, u8 type,
		    u16 q_number, u16 completed_index, void *opaque)
{
	struct enic *enic = vnic_dev_priv(vdev);

	spin_lock(&enic->wq[q_number].lock);

	vnic_wq_service(&enic->wq[q_number].vwq, cq_desc,
			completed_index, enic_wq_free_buf, opaque);

	if (netif_tx_queue_stopped(netdev_get_tx_queue(enic->netdev, q_number))
	    && vnic_wq_desc_avail(&enic->wq[q_number].vwq) >=
	    (MAX_SKB_FRAGS + ENIC_DESC_MAX_SPLITS)) {
		netif_wake_subqueue(enic->netdev, q_number);
		enic->wq[q_number].stats.wake++;
	}

	spin_unlock(&enic->wq[q_number].lock);

	return 0;
}

