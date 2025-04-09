/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#ifndef _CQ_ENET_DESC_H_
#define _CQ_ENET_DESC_H_

#include "cq_desc.h"

/* Ethernet completion queue descriptor: 16B */
struct cq_enet_wq_desc {
	__le16 completed_index;
	__le16 q_number;
	u8 reserved[11];
	u8 type_color;
};

/*
 * Defines and Capabilities for CMD_CQ_ENTRY_SIZE_SET
 */
#define VNIC_RQ_ALL (~0ULL)

#define VNIC_RQ_CQ_ENTRY_SIZE_16 0
#define VNIC_RQ_CQ_ENTRY_SIZE_32 1
#define VNIC_RQ_CQ_ENTRY_SIZE_64 2

#define VNIC_RQ_CQ_ENTRY_SIZE_16_CAPABLE BIT(VNIC_RQ_CQ_ENTRY_SIZE_16)
#define VNIC_RQ_CQ_ENTRY_SIZE_32_CAPABLE BIT(VNIC_RQ_CQ_ENTRY_SIZE_32)
#define VNIC_RQ_CQ_ENTRY_SIZE_64_CAPABLE BIT(VNIC_RQ_CQ_ENTRY_SIZE_64)

#define VNIC_RQ_CQ_ENTRY_SIZE_ALL_BIT  (VNIC_RQ_CQ_ENTRY_SIZE_16_CAPABLE | \
					VNIC_RQ_CQ_ENTRY_SIZE_32_CAPABLE | \
					VNIC_RQ_CQ_ENTRY_SIZE_64_CAPABLE)

/* Completion queue descriptor: Ethernet receive queue, 16B */
struct cq_enet_rq_desc {
	__le16 completed_index_flags;
	__le16 q_number_rss_type_flags;
	__le32 rss_hash;
	__le16 bytes_written_flags;
	__le16 vlan;
	__le16 checksum_fcoe;
	u8 flags;
	u8 type_color;
};

/* Completion queue descriptor: Ethernet receive queue, 32B */
struct cq_enet_rq_desc_32 {
	__le16 completed_index_flags;
	__le16 q_number_rss_type_flags;
	__le32 rss_hash;
	__le16 bytes_written_flags;
	__le16 vlan;
	__le16 checksum_fcoe;
	u8 flags;
	u8 fetch_index_flags;
	__le32 time_stamp;
	__le16 time_stamp2;
	__le16 pie_info;
	__le32 pie_info2;
	__le16 pie_info3;
	u8 pie_info4;
	u8 type_color;
};

/* Completion queue descriptor: Ethernet receive queue, 64B */
struct cq_enet_rq_desc_64 {
	__le16 completed_index_flags;
	__le16 q_number_rss_type_flags;
	__le32 rss_hash;
	__le16 bytes_written_flags;
	__le16 vlan;
	__le16 checksum_fcoe;
	u8 flags;
	u8 fetch_index_flags;
	__le32 time_stamp;
	__le16 time_stamp2;
	__le16 pie_info;
	__le32 pie_info2;
	__le16 pie_info3;
	u8 pie_info4;
	u8 reserved[32];
	u8 type_color;
};

#define CQ_ENET_RQ_DESC_FLAGS_INGRESS_PORT          (0x1 << 12)
#define CQ_ENET_RQ_DESC_FLAGS_FCOE                  (0x1 << 13)
#define CQ_ENET_RQ_DESC_FLAGS_EOP                   (0x1 << 14)
#define CQ_ENET_RQ_DESC_FLAGS_SOP                   (0x1 << 15)

#define CQ_ENET_RQ_DESC_RSS_TYPE_BITS               4
#define CQ_ENET_RQ_DESC_RSS_TYPE_MASK \
	((1 << CQ_ENET_RQ_DESC_RSS_TYPE_BITS) - 1)
#define CQ_ENET_RQ_DESC_RSS_TYPE_NONE               0
#define CQ_ENET_RQ_DESC_RSS_TYPE_IPv4               1
#define CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv4           2
#define CQ_ENET_RQ_DESC_RSS_TYPE_IPv6               3
#define CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv6           4
#define CQ_ENET_RQ_DESC_RSS_TYPE_IPv6_EX            5
#define CQ_ENET_RQ_DESC_RSS_TYPE_TCP_IPv6_EX        6

#define CQ_ENET_RQ_DESC_FLAGS_CSUM_NOT_CALC         (0x1 << 14)

#define CQ_ENET_RQ_DESC_BYTES_WRITTEN_BITS          14
#define CQ_ENET_RQ_DESC_BYTES_WRITTEN_MASK \
	((1 << CQ_ENET_RQ_DESC_BYTES_WRITTEN_BITS) - 1)
#define CQ_ENET_RQ_DESC_FLAGS_TRUNCATED             (0x1 << 14)
#define CQ_ENET_RQ_DESC_FLAGS_VLAN_STRIPPED         (0x1 << 15)

#define CQ_ENET_RQ_DESC_VLAN_TCI_VLAN_BITS          12
#define CQ_ENET_RQ_DESC_VLAN_TCI_VLAN_MASK \
	((1 << CQ_ENET_RQ_DESC_VLAN_TCI_VLAN_BITS) - 1)
#define CQ_ENET_RQ_DESC_VLAN_TCI_CFI_MASK           (0x1 << 12)
#define CQ_ENET_RQ_DESC_VLAN_TCI_USER_PRIO_BITS     3
#define CQ_ENET_RQ_DESC_VLAN_TCI_USER_PRIO_MASK \
	((1 << CQ_ENET_RQ_DESC_VLAN_TCI_USER_PRIO_BITS) - 1)
#define CQ_ENET_RQ_DESC_VLAN_TCI_USER_PRIO_SHIFT    13

#define CQ_ENET_RQ_DESC_FCOE_SOF_BITS               8
#define CQ_ENET_RQ_DESC_FCOE_SOF_MASK \
	((1 << CQ_ENET_RQ_DESC_FCOE_SOF_BITS) - 1)
#define CQ_ENET_RQ_DESC_FCOE_EOF_BITS               8
#define CQ_ENET_RQ_DESC_FCOE_EOF_MASK \
	((1 << CQ_ENET_RQ_DESC_FCOE_EOF_BITS) - 1)
#define CQ_ENET_RQ_DESC_FCOE_EOF_SHIFT              8

#define CQ_ENET_RQ_DESC_FLAGS_TCP_UDP_CSUM_OK       (0x1 << 0)
#define CQ_ENET_RQ_DESC_FCOE_FC_CRC_OK              (0x1 << 0)
#define CQ_ENET_RQ_DESC_FLAGS_UDP                   (0x1 << 1)
#define CQ_ENET_RQ_DESC_FCOE_ENC_ERROR              (0x1 << 1)
#define CQ_ENET_RQ_DESC_FLAGS_TCP                   (0x1 << 2)
#define CQ_ENET_RQ_DESC_FLAGS_IPV4_CSUM_OK          (0x1 << 3)
#define CQ_ENET_RQ_DESC_FLAGS_IPV6                  (0x1 << 4)
#define CQ_ENET_RQ_DESC_FLAGS_IPV4                  (0x1 << 5)
#define CQ_ENET_RQ_DESC_FLAGS_IPV4_FRAGMENT         (0x1 << 6)
#define CQ_ENET_RQ_DESC_FLAGS_FCS_OK                (0x1 << 7)

#endif /* _CQ_ENET_DESC_H_ */
