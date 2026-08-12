// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2025 Cisco Systems, Inc.  All rights reserved.

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/completion.h>

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

static int enic_mbox_wait_reply(struct enic *enic, unsigned long timeout_ms)
{
	unsigned long left;

	left = wait_for_completion_timeout(&enic->mbox_comp,
					   msecs_to_jiffies(timeout_ms));

	return left ? 0 : -ETIMEDOUT;
}

int enic_mbox_send_link_state(struct enic *enic, u16 vf_id, u32 link_state)
{
	struct enic_mbox_pf_link_state_notif_msg notif = {};

	if (!enic->vf_state || vf_id >= enic->num_vfs ||
	    !enic->vf_state[vf_id].registered) {
		netdev_dbg(enic->netdev,
			   "MBOX: skip link state to unregistered VF %u\n",
			   vf_id);
		return 0;
	}

	notif.link_state = cpu_to_le32(link_state);
	return enic_mbox_send_msg(enic, ENIC_MBOX_PF_LINK_STATE_NOTIF, vf_id,
				  &notif, sizeof(notif));
}

static int enic_mbox_pf_handle_capability(struct enic *enic, void *msg,
					  u16 vf_id, u64 msg_num)
{
	struct enic_mbox_vf_capability_reply_msg reply = {};

	reply.reply.ret_major = cpu_to_le16(0);
	reply.version = cpu_to_le32(ENIC_MBOX_CAP_VERSION_1);

	return enic_mbox_send_msg(enic, ENIC_MBOX_VF_CAPABILITY_REPLY, vf_id,
				  &reply, sizeof(reply));
}

static int enic_mbox_pf_handle_register(struct enic *enic, void *msg,
					u16 vf_id, u64 msg_num)
{
	struct enic_mbox_vf_register_reply_msg reply = {};
	u32 link_state;
	int err;

	if (!enic->vf_state || vf_id >= enic->num_vfs) {
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "MBOX: register from invalid VF %u\n",
				    vf_id);
		return -EINVAL;
	}

	/* VF re-registering (e.g. guest reboot without clean unregister):
	 * mark the previous registration inactive before accepting the new one.
	 */
	if (enic->vf_state[vf_id].registered) {
		netdev_dbg(enic->netdev,
			   "MBOX: VF %u re-register, cleaning previous state\n",
			   vf_id);
		enic->vf_state[vf_id].registered = false;
	}

	reply.reply.ret_major = cpu_to_le16(0);
	err = enic_mbox_send_msg(enic, ENIC_MBOX_VF_REGISTER_REPLY, vf_id,
				 &reply, sizeof(reply));
	if (err)
		return err;

	enic->vf_state[vf_id].registered = true;
	if (net_ratelimit())
		netdev_info(enic->netdev, "VF %u registered via MBOX\n", vf_id);

	link_state = netif_carrier_ok(enic->netdev) ?
		ENIC_MBOX_LINK_STATE_ENABLE :
		ENIC_MBOX_LINK_STATE_DISABLE;
	err = enic_mbox_send_link_state(enic, vf_id, link_state);
	if (err && net_ratelimit())
		netdev_warn(enic->netdev,
			    "VF %u: failed to send initial link state: %d\n",
			    vf_id, err);
	/* Registration succeeded; initial link state notification attempted
	 * above.  Subsequent link state changes are sent from the PF
	 * when enic_link_check() detects carrier changes.
	 */
	return 0;
}

static int enic_mbox_pf_handle_unregister(struct enic *enic, void *msg,
					  u16 vf_id, u64 msg_num)
{
	struct enic_mbox_vf_register_reply_msg reply = {};
	int err;

	if (!enic->vf_state || vf_id >= enic->num_vfs) {
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "MBOX: unregister from invalid VF %u\n",
				    vf_id);
		return -EINVAL;
	}

	/* VF is unloading; clear local state regardless of whether
	 * the reply is successfully delivered to avoid the PF treating
	 * a dead VF as still registered.
	 */
	enic->vf_state[vf_id].registered = false;

	reply.reply.ret_major = cpu_to_le16(0);
	err = enic_mbox_send_msg(enic, ENIC_MBOX_VF_UNREGISTER_REPLY, vf_id,
				 &reply, sizeof(reply));

	if (net_ratelimit())
		netdev_info(enic->netdev,
			    "VF %u unregistered via MBOX\n", vf_id);

	return err;
}

static void enic_mbox_pf_process_msg(struct enic *enic,
				     struct enic_mbox_hdr *hdr, void *payload)
{
	u16 vf_id = le16_to_cpu(hdr->src_vnic_id);
	u16 msg_len = le16_to_cpu(hdr->msg_len);
	int err = 0;

	if (!enic->vf_state) {
		netdev_dbg(enic->netdev,
			   "MBOX: PF received msg but SRIOV not active\n");
		return;
	}

	if (vf_id >= enic->num_vfs) {
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "MBOX: PF received msg from invalid VF %u\n",
				    vf_id);
		return;
	}

	switch (hdr->msg_type) {
	case ENIC_MBOX_VF_CAPABILITY_REQUEST:
		err = enic_mbox_pf_handle_capability(enic, payload, vf_id,
						     le64_to_cpu(hdr->msg_num));
		break;
	case ENIC_MBOX_VF_REGISTER_REQUEST:
		err = enic_mbox_pf_handle_register(enic, payload, vf_id,
						   le64_to_cpu(hdr->msg_num));
		break;
	case ENIC_MBOX_VF_UNREGISTER_REQUEST:
		err = enic_mbox_pf_handle_unregister(enic, payload, vf_id,
						     le64_to_cpu(hdr->msg_num));
		break;
	case ENIC_MBOX_PF_LINK_STATE_ACK: {
		struct enic_mbox_pf_link_state_ack_msg *ack = payload;

		if (msg_len < sizeof(*hdr) + sizeof(*ack))
			break;
		if (le16_to_cpu(ack->ack.ret_major) && net_ratelimit())
			netdev_warn(enic->netdev,
				    "MBOX: VF %u link state ACK error %u/%u\n",
				    vf_id,
				    le16_to_cpu(ack->ack.ret_major),
				    le16_to_cpu(ack->ack.ret_minor));
		break;
	}
	default:
		netdev_dbg(enic->netdev,
			   "MBOX: PF unhandled msg type %u from VF %u\n",
			   hdr->msg_type, vf_id);
		err = -EOPNOTSUPP;
		break;
	}

	if (err && net_ratelimit())
		netdev_warn(enic->netdev,
			    "MBOX: PF handler for msg type %u from VF %u failed: %d\n",
			    hdr->msg_type, vf_id, err);
}

static void enic_mbox_vf_handle_capability_reply(struct enic *enic,
						 void *payload)
{
	struct enic_mbox_vf_capability_reply_msg *reply = payload;

	if (READ_ONCE(enic->mbox_expected_reply) != ENIC_MBOX_VF_CAPABILITY_REPLY) {
		netdev_warn(enic->netdev,
			    "MBOX: stale capability reply (expected %u), drop\n",
			    READ_ONCE(enic->mbox_expected_reply));
		return;
	}

	if (le16_to_cpu(reply->reply.ret_major) == 0)
		enic->pf_cap_version = le32_to_cpu(reply->version);
	else
		netdev_warn(enic->netdev,
			    "MBOX: PF rejected capability request: %u/%u\n",
			    le16_to_cpu(reply->reply.ret_major),
			    le16_to_cpu(reply->reply.ret_minor));
	complete(&enic->mbox_comp);
}

static void enic_mbox_vf_handle_register_reply(struct enic *enic,
					       void *payload)
{
	struct enic_mbox_vf_register_reply_msg *reply = payload;

	if (READ_ONCE(enic->mbox_expected_reply) != ENIC_MBOX_VF_REGISTER_REPLY) {
		netdev_warn(enic->netdev,
			    "MBOX: stale register reply (expected %u), drop\n",
			    READ_ONCE(enic->mbox_expected_reply));
		return;
	}

	if (le16_to_cpu(reply->reply.ret_major)) {
		netdev_warn(enic->netdev,
			    "MBOX: VF register rejected by PF: %u/%u\n",
			    le16_to_cpu(reply->reply.ret_major),
			    le16_to_cpu(reply->reply.ret_minor));
	} else {
		enic->vf_registered = true;
	}
	complete(&enic->mbox_comp);
}

static void enic_mbox_vf_handle_unregister_reply(struct enic *enic,
						 void *payload)
{
	struct enic_mbox_vf_register_reply_msg *reply = payload;

	if (READ_ONCE(enic->mbox_expected_reply) != ENIC_MBOX_VF_UNREGISTER_REPLY) {
		netdev_warn(enic->netdev,
			    "MBOX: stale unregister reply (expected %u), drop\n",
			    READ_ONCE(enic->mbox_expected_reply));
		return;
	}

	if (le16_to_cpu(reply->reply.ret_major)) {
		netdev_warn(enic->netdev,
			    "MBOX: VF unregister rejected by PF: %u/%u\n",
			    le16_to_cpu(reply->reply.ret_major),
			    le16_to_cpu(reply->reply.ret_minor));
	} else {
		enic->vf_registered = false;
	}
	complete(&enic->mbox_comp);
}

static void enic_mbox_vf_handle_link_state(struct enic *enic, void *payload)
{
	struct enic_mbox_pf_link_state_notif_msg *notif = payload;
	struct enic_mbox_pf_link_state_ack_msg ack = {};
	int err;

	switch (le32_to_cpu(notif->link_state)) {
	case ENIC_MBOX_LINK_STATE_ENABLE:
		if (!netif_carrier_ok(enic->netdev))
			netif_carrier_on(enic->netdev);
		netdev_dbg(enic->netdev, "MBOX: link state -> UP\n");
		break;
	case ENIC_MBOX_LINK_STATE_DISABLE:
		if (netif_carrier_ok(enic->netdev))
			netif_carrier_off(enic->netdev);
		netdev_dbg(enic->netdev, "MBOX: link state -> DOWN\n");
		break;
	default:
		netdev_warn(enic->netdev, "MBOX: unknown link state %u\n",
			    le32_to_cpu(notif->link_state));
		ack.ack.ret_major = cpu_to_le16(ENIC_MBOX_ERR_GENERIC);
		break;
	}

	err = enic_mbox_send_msg(enic, ENIC_MBOX_PF_LINK_STATE_ACK,
				 ENIC_MBOX_DST_PF, &ack, sizeof(ack));
	if (err && net_ratelimit())
		netdev_warn(enic->netdev,
			    "MBOX: failed to send link state ACK: %d\n", err);
}

static bool enic_mbox_vf_payload_ok(struct enic *enic, u8 msg_type,
				    u16 payload_len, size_t min_len)
{
	if (payload_len < min_len) {
		netdev_warn(enic->netdev,
			    "MBOX: short payload for type %u (%u < %zu)\n",
			    msg_type, payload_len, min_len);
		return false;
	}
	return true;
}

static void enic_mbox_vf_process_msg(struct enic *enic,
				     struct enic_mbox_hdr *hdr, void *payload,
				     u16 payload_len)
{
	switch (hdr->msg_type) {
	case ENIC_MBOX_VF_CAPABILITY_REPLY: {
		size_t exp = sizeof(struct enic_mbox_vf_capability_reply_msg);

		if (!enic_mbox_vf_payload_ok(enic, hdr->msg_type,
					     payload_len, exp))
			return;
		enic_mbox_vf_handle_capability_reply(enic, payload);
		break;
	}
	case ENIC_MBOX_VF_REGISTER_REPLY: {
		size_t exp = sizeof(struct enic_mbox_vf_register_reply_msg);

		if (!enic_mbox_vf_payload_ok(enic, hdr->msg_type,
					     payload_len, exp))
			return;
		enic_mbox_vf_handle_register_reply(enic, payload);
		break;
	}
	case ENIC_MBOX_VF_UNREGISTER_REPLY: {
		size_t exp = sizeof(struct enic_mbox_vf_register_reply_msg);

		if (!enic_mbox_vf_payload_ok(enic, hdr->msg_type,
					     payload_len, exp))
			return;
		enic_mbox_vf_handle_unregister_reply(enic, payload);
		break;
	}
	case ENIC_MBOX_PF_LINK_STATE_NOTIF: {
		size_t exp = sizeof(struct enic_mbox_pf_link_state_notif_msg);

		if (!enic_mbox_vf_payload_ok(enic, hdr->msg_type,
					     payload_len, exp))
			return;
		enic_mbox_vf_handle_link_state(enic, payload);
		break;
	}
	default:
		netdev_dbg(enic->netdev,
			   "MBOX: VF unhandled msg type %u\n",
			   hdr->msg_type);
		break;
	}
}

static void enic_mbox_recv_handler(struct enic *enic, void *buf,
				   unsigned int len)
{
	struct enic_mbox_hdr *hdr = buf;
	void *payload;
	u16 msg_len;

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

	msg_len = le16_to_cpu(hdr->msg_len);
	if (msg_len < sizeof(*hdr) || msg_len > len) {
		if (net_ratelimit())
			netdev_warn(enic->netdev,
				    "MBOX: invalid msg_len %u (buf len %u)\n",
				    msg_len, len);
		return;
	}

	netdev_dbg(enic->netdev,
		   "MBOX recv: type %u from vnic %u len %u\n",
		   hdr->msg_type, le16_to_cpu(hdr->src_vnic_id), msg_len);

	payload = buf + sizeof(*hdr);

	if (enic->vf_state) {
		enic_mbox_pf_process_msg(enic, hdr, payload);
	} else if (le16_to_cpu(hdr->src_vnic_id) == ENIC_MBOX_DST_PF) {
		/* src_vnic_id was overwritten from the hardware-verified CQ
		 * VLAN sender field, so a VF only accepts messages that the
		 * adapter attributes to the PF.  Its sole admin-channel peer is
		 * the PF; drop anything else as a spoofed notification.
		 */
		enic_mbox_vf_process_msg(enic, hdr, payload,
					 msg_len - (u16)sizeof(*hdr));
	} else if (net_ratelimit()) {
		netdev_warn(enic->netdev,
			    "MBOX: VF dropping non-PF message from vnic %u\n",
			    le16_to_cpu(hdr->src_vnic_id));
	}
}

int enic_mbox_vf_capability_check(struct enic *enic)
{
	struct enic_mbox_vf_capability_msg req = {};
	int err;

	enic->pf_cap_version = 0;
	reinit_completion(&enic->mbox_comp);
	WRITE_ONCE(enic->mbox_expected_reply, ENIC_MBOX_VF_CAPABILITY_REPLY);
	req.version = cpu_to_le32(ENIC_MBOX_CAP_VERSION_1);

	err = enic_mbox_send_msg(enic, ENIC_MBOX_VF_CAPABILITY_REQUEST,
				 ENIC_MBOX_DST_PF, &req, sizeof(req));
	if (err) {
		WRITE_ONCE(enic->mbox_expected_reply, 0);
		return err;
	}

	err = enic_mbox_wait_reply(enic, 3000);
	WRITE_ONCE(enic->mbox_expected_reply, 0);
	if (err) {
		netdev_warn(enic->netdev,
			    "MBOX: no capability reply from PF\n");
		return err;
	}

	if (enic->pf_cap_version < ENIC_MBOX_CAP_VERSION_1) {
		netdev_warn(enic->netdev,
			    "MBOX: PF rejected capability request or reported unsupported version %u\n",
			    enic->pf_cap_version);
		return -EOPNOTSUPP;
	}

	return 0;
}

int enic_mbox_vf_register(struct enic *enic)
{
	int err;

	enic->vf_registered = false;
	reinit_completion(&enic->mbox_comp);
	WRITE_ONCE(enic->mbox_expected_reply, ENIC_MBOX_VF_REGISTER_REPLY);

	err = enic_mbox_send_msg(enic, ENIC_MBOX_VF_REGISTER_REQUEST,
				 ENIC_MBOX_DST_PF, NULL, 0);
	if (err) {
		WRITE_ONCE(enic->mbox_expected_reply, 0);
		return err;
	}

	err = enic_mbox_wait_reply(enic, 3000);
	WRITE_ONCE(enic->mbox_expected_reply, 0);
	if (err) {
		netdev_warn(enic->netdev,
			    "MBOX: VF registration with PF timed out\n");
		return err;
	}

	if (!enic->vf_registered)
		return -ENODEV;

	return 0;
}

int enic_mbox_vf_unregister(struct enic *enic)
{
	int err;

	if (!enic->vf_registered)
		return 0;

	reinit_completion(&enic->mbox_comp);
	WRITE_ONCE(enic->mbox_expected_reply, ENIC_MBOX_VF_UNREGISTER_REPLY);

	err = enic_mbox_send_msg(enic, ENIC_MBOX_VF_UNREGISTER_REQUEST,
				 ENIC_MBOX_DST_PF, NULL, 0);
	if (err) {
		WRITE_ONCE(enic->mbox_expected_reply, 0);
		return err;
	}

	err = enic_mbox_wait_reply(enic, 3000);
	WRITE_ONCE(enic->mbox_expected_reply, 0);
	if (err)
		return err;
	if (enic->vf_registered)
		return -EACCES;
	return 0;
}

void enic_mbox_init(struct enic *enic)
{
	/* mbox_lock and mbox_comp must be initialized exactly once per
	 * device lifetime; the PF sriov_configure path can re-enter this
	 * on each enable cycle where these primitives are already set up.
	 */
	if (!enic->mbox_initialized) {
		mutex_init(&enic->mbox_lock);
		init_completion(&enic->mbox_comp);
		enic->mbox_initialized = true;
	} else {
		reinit_completion(&enic->mbox_comp);
	}
	enic->mbox_msg_num = 0;
	enic->admin_rq_handler = enic_mbox_recv_handler;
}
