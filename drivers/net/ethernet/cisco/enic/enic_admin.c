// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Cisco Systems, Inc.  All rights reserved.

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

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
#include "cq_enet_desc.h"
#include "wq_enet_desc.h"
#include "rq_enet_desc.h"
#include "enic_mbox.h"

/* Retry interval for a failed admin RQ refill.  Short so the control
 * channel recovers quickly once memory is available again.
 */
#define ENIC_ADMIN_RQ_REFILL_RETRY_MS	100

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

static void enic_admin_rq_buf_clean(struct vnic_rq *rq,
				    struct vnic_rq_buf *buf)
{
	struct enic *enic = vnic_dev_priv(rq->vdev);

	if (!buf->os_buf)
		return;

	dma_unmap_single(&enic->pdev->dev, buf->dma_addr, buf->len,
			 DMA_FROM_DEVICE);
	kfree(buf->os_buf);
	buf->os_buf = NULL;
}

static int enic_admin_rq_post_one(struct enic *enic, gfp_t gfp)
{
	struct vnic_rq *rq = &enic->admin_rq;
	struct rq_enet_desc *desc;
	dma_addr_t dma_addr;
	void *buf;

	buf = kzalloc(ENIC_ADMIN_BUF_SIZE, gfp);
	if (!buf)
		return -ENOMEM;

	dma_addr = dma_map_single(&enic->pdev->dev, buf, ENIC_ADMIN_BUF_SIZE,
				  DMA_FROM_DEVICE);
	if (dma_mapping_error(&enic->pdev->dev, dma_addr)) {
		kfree(buf);
		return -ENOMEM;
	}

	desc = vnic_rq_next_desc(rq);
	rq_enet_desc_enc(desc, (u64)dma_addr | VNIC_PADDR_TARGET,
			 RQ_ENET_TYPE_ONLY_SOP, ENIC_ADMIN_BUF_SIZE);
	vnic_rq_post(rq, buf, 0, dma_addr, ENIC_ADMIN_BUF_SIZE, 0);

	return 0;
}

static int enic_admin_rq_fill(struct enic *enic, gfp_t gfp)
{
	struct vnic_rq *rq = &enic->admin_rq;
	int err;

	while (vnic_rq_desc_avail(rq) > 0) {
		err = enic_admin_rq_post_one(enic, gfp);
		if (err)
			return err;
	}

	return 0;
}

static void enic_admin_rq_drain(struct enic *enic)
{
	vnic_rq_clean(&enic->admin_rq, enic_admin_rq_buf_clean);
}

static unsigned int enic_admin_cq_color(void *cq_desc, unsigned int desc_size)
{
	u8 type_color = *((u8 *)cq_desc + desc_size - 1);

	return (type_color >> CQ_DESC_COLOR_SHIFT) & CQ_DESC_COLOR_MASK;
}

unsigned int enic_admin_wq_cq_service(struct enic *enic)
{
	struct vnic_cq *cq = &enic->admin_cq[0];
	unsigned int work = 0;
	void *desc;

	desc = vnic_cq_to_clean(cq);
	while (enic_admin_cq_color(desc, cq->ring.desc_size) !=
	       cq->last_color) {
		vnic_cq_inc_to_clean(cq);
		work++;
		desc = vnic_cq_to_clean(cq);
	}

	return work;
}

/* Upper bound on pending admin messages.  A buggy or hostile VF could flood
 * the PF admin channel faster than admin_msg_work drains it; cap the backlog
 * so a guest cannot drive the host out of memory.
 */
#define ENIC_ADMIN_MSG_MAX	256

static void enic_admin_msg_enqueue(struct enic *enic, void *buf,
				   unsigned int len)
{
	struct enic_admin_msg *msg;

	msg = kmalloc(struct_size(msg, data, len), GFP_KERNEL);
	if (!msg)
		return;

	msg->len = len;
	memcpy(msg->data, buf, len);

	spin_lock(&enic->admin_msg_lock);
	if (enic->admin_msg_count >= ENIC_ADMIN_MSG_MAX) {
		spin_unlock(&enic->admin_msg_lock);
		kfree(msg);
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "admin msg backlog full (%u); dropping\n",
				    ENIC_ADMIN_MSG_MAX);
		return;
	}
	list_add_tail(&msg->list, &enic->admin_msg_list);
	enic->admin_msg_count++;
	spin_unlock(&enic->admin_msg_lock);
}

unsigned int enic_admin_rq_cq_service(struct enic *enic)
{
	struct vnic_cq *cq = &enic->admin_cq[1];
	struct vnic_rq *rq = &enic->admin_rq;
	struct cq_enet_rq_desc *rq_desc;
	struct vnic_rq_buf *buf;
	u16 bwf, bytes_written;
	unsigned int work = 0;
	void *desc;

	/* The admin RQ and its CQ form a single in-order channel: firmware
	 * posts exactly one CQE per consumed RQ descriptor, in submission
	 * order.  Each CQE therefore pairs with rq->to_clean below without a
	 * completed_index cross-check, mirroring the in-order assumption of
	 * the main enic RX path.
	 */
	desc = vnic_cq_to_clean(cq);
	while (enic_admin_cq_color(desc, cq->ring.desc_size) !=
	       cq->last_color) {
		/* Ensure DMA descriptor fields are read after
		 * the color/valid check.  dma_rmb() is the
		 * correct barrier for DMA-written descriptors.
		 */
		dma_rmb();
		buf = rq->to_clean;

		/* Decode the actual number of bytes hardware wrote into
		 * the RX buffer.  buf->len is the static allocation size
		 * (ENIC_ADMIN_BUF_SIZE); copying that many bytes would read
		 * beyond the actual DMA payload.  bytes_written_flags is at
		 * the same offset in every cq_enet_rq_desc[_32|_64] variant.
		 */
		rq_desc = desc;
		bwf = le16_to_cpu(rq_desc->bytes_written_flags);
		bytes_written = bwf & CQ_ENET_RQ_DESC_BYTES_WRITTEN_MASK;
		if (bytes_written > buf->len)
			goto next_desc;

		dma_sync_single_for_cpu(&enic->pdev->dev,
					buf->dma_addr, buf->len,
					DMA_FROM_DEVICE);

		/* Drop on hardware error indications.  Admin messages
		 * are internal to the VIC, not received over the wire.
		 * Firmware sets TRUNCATED when the message does not fit
		 * in the posted buffer, and FCS_OK is always set on
		 * healthy admin completions.
		 */
		if (bwf & CQ_ENET_RQ_DESC_FLAGS_TRUNCATED) {
			netdev_warn_once(enic->netdev,
					 "admin RQ: truncated message dropped\n");
			goto next_desc;
		}
		if (!(rq_desc->flags & CQ_ENET_RQ_DESC_FLAGS_FCS_OK)) {
			netdev_warn_once(enic->netdev,
					 "admin RQ: bad FCS, dropping message\n");
			goto next_desc;
		}

		if (enic->admin_rq_handler) {
			u16 sender_vlan;

			/* Firmware sets the CQ VLAN field to identify the
			 * sender: 0 = PF, 1-based = VF index.  Overwrite
			 * the untrusted src_vnic_id in the MBOX header with
			 * the hardware-verified value.
			 */
			sender_vlan = le16_to_cpu(rq_desc->vlan);
			if (bytes_written >= sizeof(struct enic_mbox_hdr)) {
				struct enic_mbox_hdr *hdr = buf->os_buf;

				hdr->src_vnic_id = (sender_vlan == 0) ?
					cpu_to_le16(ENIC_MBOX_DST_PF) :
					cpu_to_le16(sender_vlan - 1);
			}

			enic_admin_msg_enqueue(enic, buf->os_buf,
					       bytes_written);
		}

next_desc:
		enic_admin_rq_buf_clean(rq, rq->to_clean);
		rq->to_clean = rq->to_clean->next;
		rq->ring.desc_avail++;

		vnic_cq_inc_to_clean(cq);
		work++;
		desc = vnic_cq_to_clean(cq);
	}

	if (enic_admin_rq_fill(enic, GFP_KERNEL)) {
		/* Some RX buffers could not be reposted (transient memory
		 * pressure).  If the ring is left empty the channel would stall
		 * with no completion to drive the next refill, so arm a delayed
		 * re-run of this poll work (the sole owner of the admin RQ ring)
		 * to repost buffers and re-arm the RQ from a single context.
		 */
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "admin RQ refill failed; scheduling retry\n");
		schedule_delayed_work(&enic->admin_poll_work,
				      msecs_to_jiffies(ENIC_ADMIN_RQ_REFILL_RETRY_MS));
	}

	return work;
}

static irqreturn_t enic_admin_isr_msix(int irq, void *data)
{
	struct enic *enic = data;

	schedule_delayed_work(&enic->admin_poll_work, 0);

	return IRQ_HANDLED;
}

static void enic_admin_msg_work_handler(struct work_struct *work)
{
	struct enic *enic = container_of(work, struct enic, admin_msg_work);
	struct enic_admin_msg *msg, *tmp;
	LIST_HEAD(local_list);

	spin_lock_bh(&enic->admin_msg_lock);
	list_splice_init(&enic->admin_msg_list, &local_list);
	enic->admin_msg_count = 0;
	spin_unlock_bh(&enic->admin_msg_lock);

	list_for_each_entry_safe(msg, tmp, &local_list, list) {
		if (enic->admin_rq_handler)
			enic->admin_rq_handler(enic, msg->data, msg->len);
		list_del(&msg->list);
		kfree(msg);
	}
}

static void enic_admin_poll_work_handler(struct work_struct *work)
{
	struct enic *enic = container_of(to_delayed_work(work), struct enic,
					 admin_poll_work);
	unsigned int credits;
	unsigned int rq_work;

	/* Snapshot the pending credit count before draining so we acknowledge
	 * exactly what the hardware reported for this interrupt.  Credits that
	 * accrue while enic_admin_rq_cq_service() runs are left for the next
	 * interrupt, which is harmless on this low-rate control path.
	 */
	credits = vnic_intr_credits(&enic->admin_intr);

	rq_work = enic_admin_rq_cq_service(enic);

	if (rq_work > 0)
		schedule_work(&enic->admin_msg_work);

	/* Acknowledge the snapshotted credits and unmask the vector.  Unlike
	 * the NAPI data path, the admin channel is not re-polled, so the vector
	 * must be re-armed here to receive the next completion.  The unmask is
	 * applied through the interrupt mask register independently of the
	 * credit count, so returning zero credits on a spurious wakeup still
	 * re-arms the vector.
	 */
	vnic_intr_return_credits(&enic->admin_intr,
				 credits,
				 1 /* unmask */, 0);
}

static int enic_admin_setup_intr(struct enic *enic)
{
	unsigned int intr_index = enic->intr_count;
	int err;

	if (vnic_dev_get_intr_mode(enic->vdev) != VNIC_DEV_INTR_MODE_MSIX ||
	    intr_index >= enic->intr_avail)
		return -ENODEV;

	/* The admin INTR uses a slot in the same RES_TYPE_INTR_CTRL
	 * strided array of per-vector control blocks (mask, coalescing
	 * timer, credit return) that the data-path IRQs occupy in BAR0.
	 * vnic_intr_alloc() defaults to RES_TYPE_INTR_CTRL, which is what
	 * we want here.
	 */
	err = vnic_intr_alloc(enic->vdev, &enic->admin_intr, intr_index);
	if (err) {
		netdev_warn(enic->netdev,
			    "Failed to alloc admin intr at index %u: %d\n",
			    intr_index, err);
		return err;
	}

	enic->admin_intr_index = intr_index;

	/* Mask the admin vector before requesting the IRQ so an early or
	 * spurious completion cannot run the poll handler against the
	 * not-yet-initialised admin rings.  enic_admin_channel_open() unmasks
	 * it only after the rings are initialised and filled.
	 */
	vnic_intr_mask(&enic->admin_intr);

	/* A V2 VF opens the admin channel during probe, before
	 * register_netdev() resolves the "eth%d" name template, so using
	 * netdev->name here would register the literal "eth%d-admin" in
	 * /proc/interrupts.  Use the already-stable PCI device name instead.
	 */
	snprintf(enic->msix[intr_index].devname,
		 sizeof(enic->msix[intr_index].devname),
		 "%s-admin", pci_name(enic->pdev));
	enic->msix[intr_index].isr = enic_admin_isr_msix;
	enic->msix[intr_index].devid = enic;

	err = request_irq(enic->msix_entry[intr_index].vector,
			  enic->msix[intr_index].isr, 0,
			  enic->msix[intr_index].devname,
			  enic->msix[intr_index].devid);
	if (err) {
		netdev_warn(enic->netdev,
			    "Failed to request admin MSI-X irq: %d\n", err);
		vnic_intr_free(&enic->admin_intr);
		return err;
	}

	enic->msix[intr_index].requested = 1;

	netdev_dbg(enic->netdev,
		   "admin channel using MSI-X interrupt (index %u)\n",
		   intr_index);

	return 0;
}

static void enic_admin_teardown_intr(struct enic *enic)
{
	unsigned int intr_index = enic->admin_intr_index;

	free_irq(enic->msix_entry[intr_index].vector,
		 enic->msix[intr_index].devid);
	cancel_delayed_work_sync(&enic->admin_poll_work);
	enic->msix[intr_index].requested = 0;
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
	vnic_intr_free(&enic->admin_intr);
	vnic_cq_free(&enic->admin_cq[1]);
	vnic_cq_free(&enic->admin_cq[0]);
	vnic_rq_free(&enic->admin_rq);
	vnic_wq_free(&enic->admin_wq);
}

static void enic_admin_init_resources(struct enic *enic)
{
	unsigned int intr_offset = enic->admin_intr_index;

	vnic_wq_init(&enic->admin_wq,
		     0, 0, 0); /* cq_index, err_intr_enable, err_intr_offset */
	vnic_rq_init(&enic->admin_rq,
		     1, 0, 0); /* cq_index, err_intr_enable, err_intr_offset */
	vnic_cq_init(&enic->admin_cq[0],
		     VNIC_CQ_FC_DISABLE,
		     VNIC_CQ_COLOR_ENABLE,
		     0, 0, 1, /* cq_head, cq_tail, cq_tail_color */
		     VNIC_CQ_INTR_DISABLE, /* polled synchronously by mbox send */
		     VNIC_CQ_ENTRY_ENABLE,
		     VNIC_CQ_MSG_DISABLE,
		     intr_offset,
		     0 /* cq_message_addr */);
	vnic_cq_init(&enic->admin_cq[1],
		     VNIC_CQ_FC_DISABLE,
		     VNIC_CQ_COLOR_ENABLE,
		     0, 0, 1, /* cq_head, cq_tail, cq_tail_color */
		     VNIC_CQ_INTR_ENABLE,
		     VNIC_CQ_ENTRY_ENABLE,
		     VNIC_CQ_MSG_DISABLE,
		     intr_offset,
		     0 /* cq_message_addr */);
	/* coalescing_timer, coalescing_type, mask_on_assertion */
	vnic_intr_init(&enic->admin_intr,
		       0, 0, 1);
}

static void enic_admin_msg_drain(struct enic *enic)
{
	struct enic_admin_msg *msg, *tmp;

	spin_lock_bh(&enic->admin_msg_lock);
	list_for_each_entry_safe(msg, tmp, &enic->admin_msg_list, list) {
		list_del(&msg->list);
		kfree(msg);
	}
	enic->admin_msg_count = 0;
	spin_unlock_bh(&enic->admin_msg_lock);
}

int enic_admin_channel_open(struct enic *enic)
{
	int err;

	if (!enic->has_admin_channel)
		return -ENODEV;

	/* Keep MBOX sends disabled for the entire open sequence.  It is
	 * cleared only after every resource is allocated and enabled below,
	 * so any early error return here leaves sends disabled and a
	 * concurrent sender cannot touch a half-open or freed admin_wq.
	 */
	WRITE_ONCE(enic->mbox_send_disabled, true);

	err = enic_admin_alloc_resources(enic);
	if (err) {
		netdev_err(enic->netdev,
			   "Failed to alloc admin channel resources: %d\n",
			   err);
		return err;
	}

	spin_lock_init(&enic->admin_msg_lock);
	INIT_LIST_HEAD(&enic->admin_msg_list);
	INIT_WORK(&enic->admin_msg_work, enic_admin_msg_work_handler);
	INIT_DELAYED_WORK(&enic->admin_poll_work, enic_admin_poll_work_handler);

	err = enic_admin_setup_intr(enic);
	if (err) {
		netdev_err(enic->netdev,
			   "Admin channel requires MSI-X, SR-IOV unavailable: %d\n",
			   err);
		goto free_resources;
	}

	enic_admin_init_resources(enic);

	vnic_wq_enable(&enic->admin_wq);
	vnic_rq_enable(&enic->admin_rq);

	err = enic_admin_rq_fill(enic, GFP_KERNEL);
	if (err) {
		netdev_err(enic->netdev,
			   "Failed to fill admin RQ buffers: %d\n", err);
		goto disable_queues;
	}

	err = enic_admin_qp_type_set(enic, QP_ENABLE);
	if (err) {
		netdev_err(enic->netdev,
			   "Failed to set admin QP type: %d\n", err);
		goto disable_queues;
	}

	vnic_intr_unmask(&enic->admin_intr);

	/* Only now that the admin WQ/RQ/CQ and interrupt are fully allocated,
	 * programmed and enabled is it safe to allow MBOX sends.  Clearing this
	 * earlier opened a window where a concurrent sender (e.g. link-notify
	 * work scheduled by a post-reset link-up) could call enic_mbox_send_msg()
	 * against a not-yet-allocated admin_wq and crash.
	 */
	WRITE_ONCE(enic->mbox_send_disabled, false);

	netdev_dbg(enic->netdev,
		   "admin channel open: intr=%u wq_avail=%u rq_avail=%u cq0_color=%u cq1_color=%u\n",
		   enic->admin_intr_index,
		   vnic_wq_desc_avail(&enic->admin_wq),
		   vnic_rq_desc_avail(&enic->admin_rq),
		   enic->admin_cq[0].last_color,
		   enic->admin_cq[1].last_color);

	enic->admin_chan_up = true;

	return 0;

disable_queues:
	enic_admin_teardown_intr(enic);
	enic_admin_qp_type_set(enic, QP_DISABLE);
	if (vnic_wq_disable(&enic->admin_wq))
		netdev_warn(enic->netdev, "Failed to disable admin WQ\n");
	if (vnic_rq_disable(&enic->admin_rq))
		netdev_warn(enic->netdev, "Failed to disable admin RQ\n");
	cancel_work_sync(&enic->admin_msg_work);
	enic_admin_msg_drain(enic);
	enic_admin_rq_drain(enic);
free_resources:
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

	WRITE_ONCE(enic->mbox_send_disabled, true);

	netdev_dbg(enic->netdev, "admin channel close\n");

	vnic_intr_mask(&enic->admin_intr);
	enic_admin_teardown_intr(enic);
	cancel_work_sync(&enic->link_notify_work);
	cancel_work_sync(&enic->admin_msg_work);
	enic_admin_msg_drain(enic);

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
	enic_admin_rq_drain(enic);
	vnic_cq_clean(&enic->admin_cq[0]);
	vnic_cq_clean(&enic->admin_cq[1]);
	vnic_intr_clean(&enic->admin_intr);

	enic->admin_rq_handler = NULL;
	enic_admin_free_resources(enic);

	enic->admin_chan_up = false;
}
