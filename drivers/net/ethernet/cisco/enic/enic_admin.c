// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Cisco Systems, Inc.  All rights reserved.

#include <linux/kernel.h>
#include <linux/netdevice.h>

#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "vnic_cq.h"
#include "vnic_intr.h"
#include "vnic_resource.h"
#include "vnic_devcmd.h"
#include "enic.h"
#include "enic_admin.h"
#include "cq_desc.h"
#include "wq_enet_desc.h"
#include "rq_enet_desc.h"

/* Clean up any admin WQ buffers still held by hardware at close time.
 * Normally buffers are freed inline after send completion, but a timed-out
 * send intentionally leaves the buffer live until the queue is stopped.
 */
static void enic_admin_wq_buf_clean(struct vnic_wq *wq,
				    struct vnic_wq_buf *buf)
{
	struct enic *enic = vnic_dev_priv(wq->vdev);

	if (buf->os_buf) {
		dma_unmap_single(&enic->pdev->dev, buf->dma_addr,
				 buf->len, DMA_TO_DEVICE);
		kfree(buf->os_buf);
		buf->os_buf = NULL;
	}
}

/* No-op: admin RQ buffer teardown is handled in enic_admin_channel_close */
static void enic_admin_rq_buf_clean(struct vnic_rq *rq,
				    struct vnic_rq_buf *buf)
{
}

static int enic_admin_qp_type_set(struct enic *enic, u32 enable)
{
	u64 a0 = QP_TYPE_ADMIN, a1 = enable;
	int wait = 1000;
	int err;

	spin_lock_bh(&enic->devcmd_lock);
	err = vnic_dev_cmd(enic->vdev, CMD_QP_TYPE_SET, &a0, &a1, wait);
	spin_unlock_bh(&enic->devcmd_lock);

	return err;
}

static int enic_admin_alloc_resources(struct enic *enic)
{
	int err;

	err = vnic_wq_alloc_with_type(enic->vdev, &enic->admin_wq, 0,
				      ENIC_ADMIN_DESC_COUNT,
				      sizeof(struct wq_enet_desc),
				      RES_TYPE_ADMIN_WQ);
	if (err)
		return err;

	err = vnic_rq_alloc_with_type(enic->vdev, &enic->admin_rq, 0,
				      ENIC_ADMIN_DESC_COUNT,
				      sizeof(struct rq_enet_desc),
				      RES_TYPE_ADMIN_RQ);
	if (err)
		goto free_wq;

	/* admin_cq[0] is the WQ completion queue.  WQ CQEs are always
	 * 16 bytes wide; firmware always writes 16-byte CQEs for WQ
	 * completions on every WQ, including the admin channel WQ.
	 * Use sizeof(struct cq_desc) accordingly.
	 */
	err = vnic_cq_alloc_with_type(enic->vdev, &enic->admin_cq[0], 0,
				      ENIC_ADMIN_DESC_COUNT,
				      sizeof(struct cq_desc),
				      RES_TYPE_ADMIN_CQ);
	if (err)
		goto free_rq;

	/* admin_cq[1] is the RQ completion queue.  Its descriptor size
	 * must match what firmware writes.  enic_ext_cq() called earlier
	 * in probe issues CMD_CQ_ENTRY_SIZE_SET for VNIC_RQ_ALL,
	 * programming firmware to write CQ entries of (16 << enic->ext_cq)
	 * bytes for every RQ CQ on the vNIC, including the admin RQ CQ.
	 * Allocating with the same size keeps the host poller and
	 * firmware in lockstep:
	 *
	 *   - The color/valid bit lives at byte (desc_size - 1) of every
	 *     cq_enet_rq_desc[_32|_64] variant, so enic_admin_cq_color()
	 *     reads it from the correct offset.
	 *   - Only the first 15 bytes of the descriptor (vlan,
	 *     bytes_written_flags, ...) are accessed by the admin path;
	 *     these fields are identical across all three variants (see
	 *     comment in enic_rq.c above cq_enet_rq_desc_dec()).
	 */
	err = vnic_cq_alloc_with_type(enic->vdev, &enic->admin_cq[1], 1,
				      ENIC_ADMIN_DESC_COUNT,
				      16 << enic->ext_cq,
				      RES_TYPE_ADMIN_CQ);
	if (err)
		goto free_cq0;

	return 0;

free_cq0:
	vnic_cq_free(&enic->admin_cq[0]);
free_rq:
	vnic_rq_free(&enic->admin_rq);
free_wq:
	vnic_wq_free(&enic->admin_wq);
	return err;
}

static void enic_admin_free_resources(struct enic *enic)
{
	vnic_cq_free(&enic->admin_cq[1]);
	vnic_cq_free(&enic->admin_cq[0]);
	vnic_rq_free(&enic->admin_rq);
	vnic_wq_free(&enic->admin_wq);
}

static void enic_admin_init_resources(struct enic *enic)
{
	vnic_wq_init(&enic->admin_wq,
		     0, 0, 0); /* cq_index, err_intr_enable, err_intr_offset */
	vnic_rq_init(&enic->admin_rq,
		     1, 0, 0); /* cq_index, err_intr_enable, err_intr_offset */
	vnic_cq_init(&enic->admin_cq[0],
		     VNIC_CQ_FC_DISABLE,
		     VNIC_CQ_COLOR_ENABLE,
		     0, 0, 1, /* cq_head, cq_tail, cq_tail_color */
		     VNIC_CQ_INTR_DISABLE,
		     VNIC_CQ_ENTRY_ENABLE,
		     VNIC_CQ_MSG_DISABLE,
		     0, /* interrupt_offset */
		     0 /* cq_message_addr */);
	vnic_cq_init(&enic->admin_cq[1],
		     VNIC_CQ_FC_DISABLE,
		     VNIC_CQ_COLOR_ENABLE,
		     0, 0, 1, /* cq_head, cq_tail, cq_tail_color */
		     VNIC_CQ_INTR_DISABLE,
		     VNIC_CQ_ENTRY_ENABLE,
		     VNIC_CQ_MSG_DISABLE,
		     0, /* interrupt_offset */
		     0 /* cq_message_addr */);
}

int enic_admin_channel_open(struct enic *enic)
{
	int err;

	if (!enic->has_admin_channel)
		return -ENODEV;

	err = enic_admin_alloc_resources(enic);
	if (err) {
		netdev_err(enic->netdev,
			   "Failed to alloc admin channel resources: %d\n",
			   err);
		return err;
	}

	enic_admin_init_resources(enic);

	vnic_wq_enable(&enic->admin_wq);
	vnic_rq_enable(&enic->admin_rq);

	err = enic_admin_qp_type_set(enic, QP_ENABLE);
	if (err) {
		netdev_err(enic->netdev,
			   "Failed to set admin QP type: %d\n", err);
		goto disable_queues;
	}

	enic->admin_chan_up = true;

	return 0;

disable_queues:
	enic_admin_qp_type_set(enic, QP_DISABLE);
	if (vnic_wq_disable(&enic->admin_wq))
		netdev_warn(enic->netdev, "Failed to disable admin WQ\n");
	if (vnic_rq_disable(&enic->admin_rq))
		netdev_warn(enic->netdev, "Failed to disable admin RQ\n");
	enic_admin_free_resources(enic);
	return err;
}

void enic_admin_channel_close(struct enic *enic)
{
	int err;

	if (!enic->has_admin_channel)
		return;

	/* Nothing to tear down if the channel was never (re)opened, e.g. a
	 * failed enic_admin_channel_open() in probe or in the reset path;
	 * otherwise the disable/clean calls below dereference freed resources.
	 */
	if (!enic->admin_chan_up)
		return;

	enic_admin_qp_type_set(enic, QP_DISABLE);

	err = vnic_wq_disable(&enic->admin_wq);
	if (err)
		netdev_warn(enic->netdev,
			    "Failed to disable admin WQ: %d\n", err);
	err = vnic_rq_disable(&enic->admin_rq);
	if (err)
		netdev_warn(enic->netdev,
			    "Failed to disable admin RQ: %d\n", err);

	vnic_wq_clean(&enic->admin_wq, enic_admin_wq_buf_clean);
	vnic_rq_clean(&enic->admin_rq, enic_admin_rq_buf_clean);
	vnic_cq_clean(&enic->admin_cq[0]);
	vnic_cq_clean(&enic->admin_cq[1]);
	enic_admin_free_resources(enic);

	enic->admin_chan_up = false;
}
