/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Alibaba Elastic Ethernet Adapter.
 *
 * Copyright (C) 2025 Alibaba Inc.
 */

#ifndef __EEA_DESC_H__
#define __EEA_DESC_H__

#define EEA_DESC_TS_MASK GENMASK_ULL(47, 0)
#define EEA_DESC_TS(desc) (le64_to_cpu((desc)->ts) & EEA_DESC_TS_MASK)

struct eea_aq_desc {
	__le16 flags;
	__le16 id;
	__le16 reserved;
	u8 classid;
	u8 command;
	__le64 data_addr;
	__le64 reply_addr;
	__le32 data_len;
	__le32 reply_len;
};

struct eea_aq_cdesc {
	__le16 flags;
	__le16 id;
#define EEA_OK     0
#define EEA_ERR    0xffffffff
	__le32 status;
	__le32 reply_len;
	__le32 reserved1;

	__le64 reserved2;
	__le64 reserved3;
};

struct eea_rx_desc_no_hdr {
	__le16 flags;
	__le16 id;
	__le16 len;
	__le16 reserved1;

	__le64 addr;
};

struct eea_rx_desc {
	__le16 flags;
	__le16 id;
	__le16 len;
	__le16 reserved1;

	__le64 addr;

	__le64 hdr_addr;
	__le32 reserved2;
	__le32 reserved3;
};

#define EEA_RX_CDESC_HDR_LEN_MASK GENMASK_ULL(9, 0)

struct eea_rx_cdesc {
#define EEA_DESC_F_DATA_VALID	BIT(6)
#define EEA_DESC_F_SPLIT_HDR	BIT(5)
	__le16 flags;
	__le16 id;
	__le16 len;
#define EEA_NET_PT_NONE      0
#define EEA_NET_PT_IPv4      1
#define EEA_NET_PT_TCPv4     2
#define EEA_NET_PT_UDPv4     3
#define EEA_NET_PT_IPv6      4
#define EEA_NET_PT_TCPv6     5
#define EEA_NET_PT_UDPv6     6
#define EEA_NET_PT_IPv6_EX   7
#define EEA_NET_PT_TCPv6_EX  8
#define EEA_NET_PT_UDPv6_EX  9
	/* [9:0] is packet type. */
	__le16 type;

	/* hw timestamp [0:47]: ts */
	__le64 ts;

	__le32 hash;

	/* 0-9: hdr_len  split header
	 * 10-15: reserved1
	 */
	__le16 len_ex;
	__le16 reserved2;

	__le32 reserved3;
	__le32 reserved4;
};

#define EEA_TX_GSO_NONE   0
#define EEA_TX_GSO_TCPV4  1
#define EEA_TX_GSO_TCPV6  4
#define EEA_TX_GSO_UDP_L4 5
#define EEA_TX_GSO_ECN    0x80

struct eea_tx_desc {
#define EEA_DESC_F_DO_CSUM	BIT(6)
	__le16 flags;
	__le16 id;
	__le16 len;
	__le16 reserved1;

	__le64 addr;

	__le16 csum_start;
	__le16 csum_offset;
	u8 gso_type;
	u8 reserved2;
	__le16 gso_size;
	__le64 reserved3;
};

struct eea_tx_cdesc {
	__le16 flags;
	__le16 id;
	__le16 len;
	__le16 reserved1;

	/* hw timestamp [0:47]: ts */
	__le64 ts;
};

#define EEA_DB_FLAGS_OFF      0
#define EEA_DB_IDX_OFF        (2 * 8)
#define EEA_DB_TX_CQ_HEAD_OFF (4 * 8)
#define EEA_DB_RX_CQ_HEAD_OFF (6 * 8)

#define EEA_IDX_PRESENT   BIT(0)
#define EEA_IRQ_MASK      BIT(1)
#define EEA_IRQ_UNMASK    BIT(2)
#endif
