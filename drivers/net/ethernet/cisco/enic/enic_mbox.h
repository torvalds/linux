/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2025 Cisco Systems, Inc.  All rights reserved. */

#ifndef _ENIC_MBOX_H_
#define _ENIC_MBOX_H_

#include <linux/bits.h>
#include <linux/types.h>

/*
 * Mailbox protocol for PF-VF communication over the admin channel.
 *
 * Even numbers are requests, odd numbers are replies/acks.
 * The prefix indicates the initiator: VF_ = VF-initiated, PF_ = PF-initiated.
 */
enum enic_mbox_msg_type {
	ENIC_MBOX_VF_CAPABILITY_REQUEST		= 0,
	ENIC_MBOX_VF_CAPABILITY_REPLY		= 1,
	ENIC_MBOX_VF_REGISTER_REQUEST		= 2,
	ENIC_MBOX_VF_REGISTER_REPLY		= 3,
	ENIC_MBOX_VF_UNREGISTER_REQUEST		= 4,
	ENIC_MBOX_VF_UNREGISTER_REPLY		= 5,
	ENIC_MBOX_PF_LINK_STATE_NOTIF		= 6,
	ENIC_MBOX_PF_LINK_STATE_ACK		= 7,
	ENIC_MBOX_MAX
};

struct enic_mbox_hdr {
	__le16 src_vnic_id;
	__le16 dst_vnic_id;
	u8 msg_type;
	u8 flags;
	__le16 msg_len;
	__le64 msg_num;
};

struct enic_mbox_generic_reply {
	__le16 ret_major;
	__le16 ret_minor;
};

#define ENIC_MBOX_ERR_GENERIC		BIT(0)
#define ENIC_MBOX_ERR_VF_NOT_REGISTERED	BIT(1)
#define ENIC_MBOX_ERR_MSG_NOT_SUPPORTED	BIT(2)

/* ENIC_MBOX_VF_CAPABILITY_REQUEST / _REPLY */
#define ENIC_MBOX_CAP_VERSION_0		0
#define ENIC_MBOX_CAP_VERSION_1		1

struct enic_mbox_vf_capability_msg {
	__le32 version;
	__le32 reserved[32];
};

/* The embedded enic_mbox_generic_reply has 2-byte alignment, but the
 * __le32 members give this struct 4-byte natural alignment.  Receive
 * buffers come from kmalloc (>= 8-byte aligned), so there is no
 * misaligned access risk when casting from the receive buffer.
 */
struct enic_mbox_vf_capability_reply_msg {
	struct enic_mbox_generic_reply reply;
	__le32 version;
	__le32 reserved[32];
};

/* ENIC_MBOX_VF_REGISTER / _UNREGISTER */
struct enic_mbox_vf_register_reply_msg {
	struct enic_mbox_generic_reply reply;
};

/* ENIC_MBOX_PF_LINK_STATE_NOTIF / _ACK */
#define ENIC_MBOX_LINK_STATE_DISABLE	0
#define ENIC_MBOX_LINK_STATE_ENABLE	1

struct enic_mbox_pf_link_state_notif_msg {
	__le32 link_state;
};

struct enic_mbox_pf_link_state_ack_msg {
	struct enic_mbox_generic_reply ack;
};

#define ENIC_MBOX_DST_PF	0xFFFF

struct enic;

void enic_mbox_init(struct enic *enic);
int enic_mbox_send_msg(struct enic *enic, u8 msg_type, u16 dst_vnic_id,
		       void *payload, u16 payload_len);
int enic_mbox_send_link_state(struct enic *enic, u16 vf_id, u32 link_state);

#endif /* _ENIC_MBOX_H_ */
