// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Cisco Systems, Inc.  All rights reserved.

#include <net/netdev_queues.h>
#include "enic_res.h"
#include "enic.h"
#include "enic_wq.h"

#define ENET_CQ_DESC_COMP_NDX_BITS 14
#define ENET_CQ_DESC_COMP_NDX_MASK GENMASK(ENET_CQ_DESC_COMP_NDX_BITS - 1, 0)

static void enic_wq_cq_desc_dec(const struct cq_desc *desc_arg, bool ext_wq,
				u8 *type, u8 *color, u16 *q_number,
				u16 *completed_index)
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

	if (ext_wq)
		*completed_index = le16_to_cpu(desc->completed_index) &
			ENET_CQ_DESC_COMP_NDX_MASK;
	else
		*completed_index = le16_to_cpu(desc->completed_index) &
			CQ_DESC_COMP_NDX_MASK;
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

static void enic_wq_service(struct vnic_dev *vdev, struct cq_desc *cq_desc,
			    u8 type, u16 q_number, u16 completed_index)
{
	struct enic *enic = vnic_dev_priv(vdev);

	spin_lock(&enic->wq[q_number].lock);

	vnic_wq_service(&enic->wq[q_number].vwq, cq_desc,
			completed_index, enic_wq_free_buf, NULL);

	if (netif_tx_queue_stopped(netdev_get_tx_queue(enic->netdev, q_number))
	    && vnic_wq_desc_avail(&enic->wq[q_number].vwq) >=
	    (MAX_SKB_FRAGS + ENIC_DESC_MAX_SPLITS)) {
		netif_wake_subqueue(enic->netdev, q_number);
		enic->wq[q_number].stats.wake++;
	}

	spin_unlock(&enic->wq[q_number].lock);
}

unsigned int enic_wq_cq_service(struct enic *enic, unsigned int cq_index,
				unsigned int work_to_do)
{
	struct vnic_cq *cq = &enic->cq[cq_index];
	u16 q_number, completed_index;
	unsigned int work_done = 0;
	struct cq_desc *cq_desc;
	u8 type, color;
	bool ext_wq;

	ext_wq = cq->ring.size > ENIC_MAX_WQ_DESCS_DEFAULT;

	cq_desc = (struct cq_desc *)vnic_cq_to_clean(cq);
	enic_wq_cq_desc_dec(cq_desc, ext_wq, &type, &color,
			    &q_number, &completed_index);

	while (color != cq->last_color) {
		enic_wq_service(cq->vdev, cq_desc, type, q_number,
				completed_index);

		vnic_cq_inc_to_clean(cq);

		if (++work_done >= work_to_do)
			break;

		cq_desc = (struct cq_desc *)vnic_cq_to_clean(cq);
		enic_wq_cq_desc_dec(cq_desc, ext_wq, &type, &color,
				    &q_number, &completed_index);
	}

	return work_done;
}
