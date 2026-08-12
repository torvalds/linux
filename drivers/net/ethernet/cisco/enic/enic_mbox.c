// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Cisco Systems, Inc.  All rights reserved.

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_cq.h"
#include "enic.h"
#include "enic_admin.h"
#include "enic_mbox.h"
#include "wq_enet_desc.h"

#define ENIC_MBOX_POLL_TIMEOUT_US	5000000
#define ENIC_MBOX_POLL_INTERVAL_US	100

static void enic_mbox_fill_hdr(struct enic *enic, struct enic_mbox_hdr *hdr,
			       u8 msg_type, u16 dst_vnic_id, u16 msg_len)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->dst_vnic_id = cpu_to_le16(dst_vnic_id);
	hdr->msg_type = msg_type;
	hdr->msg_len = cpu_to_le16(msg_len);
	hdr->msg_num = cpu_to_le64(++enic->mbox_msg_num);
}

int enic_mbox_send_msg(struct enic *enic, u8 msg_type, u16 dst_vnic_id,
		       void *payload, u16 payload_len)
{
	size_t total_len = sizeof(struct enic_mbox_hdr) + payload_len;
	struct vnic_wq *wq = &enic->admin_wq;
	struct wq_enet_desc *desc;
	unsigned long timeout;
	dma_addr_t dma_addr;
	u16 vlan_tag;
	void *buf;
	int err;

	/* Reject payloads that cannot fit in a single admin buffer.  Checked
	 * before taking mbox_lock; total_len is computed as size_t so the
	 * sizeof() + payload_len sum cannot wrap.
	 */
	if (payload_len > ENIC_ADMIN_BUF_SIZE - sizeof(struct enic_mbox_hdr))
		return -EINVAL;

	/* Serialize MBOX sends. The admin channel is a low-frequency
	 * control path; holding the mutex across the poll is acceptable.
	 */
	mutex_lock(&enic->mbox_lock);

	if (!enic->has_admin_channel || READ_ONCE(enic->mbox_send_disabled)) {
		err = -ENODEV;
		goto unlock;
	}

	if (vnic_wq_desc_avail(wq) == 0) {
		err = -ENOSPC;
		goto unlock;
	}

	buf = kmalloc(total_len, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto unlock;
	}

	enic_mbox_fill_hdr(enic, buf, msg_type, dst_vnic_id, total_len);
	if (payload_len) {
		void *dst = buf + sizeof(struct enic_mbox_hdr);

		memcpy(dst, payload, payload_len);
	}

	dma_addr = dma_map_single(&enic->pdev->dev, buf, total_len,
				  DMA_TO_DEVICE);
	if (dma_mapping_error(&enic->pdev->dev, dma_addr)) {
		kfree(buf);
		err = -ENOMEM;
		goto unlock;
	}

	/* Firmware uses vlan field for routing: 0 = PF, 1-based = VF index */
	if (dst_vnic_id == ENIC_MBOX_DST_PF)
		vlan_tag = 0;
	else
		vlan_tag = dst_vnic_id + 1;

	desc = vnic_wq_next_desc(wq);
	wq_enet_desc_enc(desc, (u64)dma_addr | VNIC_PADDR_TARGET,
			 total_len,
			 0, 0, 0,       /* mss, hdr_len, offload_mode */
			 1, 1,          /* eop, cq_entry */
			 0,             /* fcoe_encap */
			 1, vlan_tag,   /* vlan_tag_insert, vlan_tag */
			 0);            /* loopback */
	vnic_wq_post(wq, buf, dma_addr, total_len,
		     1, 1,              /* sop, eop */
		     1, 1,              /* desc_skip_cnt, cq_entry */
		     0, 0);             /* compressed_send, wrid */
	vnic_wq_doorbell(wq);

	timeout = jiffies + usecs_to_jiffies(ENIC_MBOX_POLL_TIMEOUT_US);
	err = -ETIMEDOUT;
	while (time_before(jiffies, timeout)) {
		if (enic_admin_wq_cq_service(enic)) {
			err = 0;
			break;
		}
		usleep_range(ENIC_MBOX_POLL_INTERVAL_US,
			     ENIC_MBOX_POLL_INTERVAL_US + 50);
	}
	/* Final check in case completion arrived during the last sleep */
	if (err && enic_admin_wq_cq_service(enic))
		err = 0;

	if (!err) {
		wq->to_clean = wq->to_clean->next;
		wq->ring.desc_avail++;
		dma_unmap_single(&enic->pdev->dev, dma_addr, total_len,
				 DMA_TO_DEVICE);
		kfree(buf);
	} else {
		netdev_err(enic->netdev,
			   "MBOX send timed out (type %u dst %u), disabling channel\n",
			   msg_type, dst_vnic_id);
		/*
		 * The WQ descriptor is still live in hardware. Do not unmap
		 * or free the buffer: the device may still DMA from dma_addr.
		 * Mark the channel unusable so no further sends are attempted.
		 */
		WRITE_ONCE(enic->mbox_send_disabled, true);
	}

	netdev_dbg(enic->netdev,
		   "MBOX send msg_type %u dst %u vlan %u err %d\n",
		   msg_type, dst_vnic_id, vlan_tag, err);
unlock:
	mutex_unlock(&enic->mbox_lock);
	return err;
}

static void enic_mbox_recv_handler(struct enic *enic, void *buf,
				   unsigned int len)
{
	struct enic_mbox_hdr *hdr = buf;

	if (len < sizeof(*hdr)) {
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "MBOX: truncated message (len %u < %zu)\n",
				    len, sizeof(*hdr));
		return;
	}

	if (hdr->msg_type >= ENIC_MBOX_MAX) {
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "MBOX: unknown msg type %u\n",
				    hdr->msg_type);
		return;
	}

	netdev_dbg(enic->netdev,
		   "MBOX recv: type %u from vnic %u len %u\n",
		   hdr->msg_type, le16_to_cpu(hdr->src_vnic_id),
		   le16_to_cpu(hdr->msg_len));
}

void enic_mbox_init(struct enic *enic)
{
	enic->mbox_msg_num = 0;
	mutex_init(&enic->mbox_lock);
	enic->admin_rq_handler = enic_mbox_recv_handler;
}
